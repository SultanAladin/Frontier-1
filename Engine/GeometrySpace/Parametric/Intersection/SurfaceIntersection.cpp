/*====================================================================================================================================
                                                   SURFACEINTERSECTION.CPP
====================================================================================================================================*/
// 🧩 Analytic dispatch (meridians, cross-sections, plane–plane) and hybrid lattice / normal-cone / marching intersection

#include "Engine/GeometrySpace/Parametric/Intersection/SurfaceIntersection.h"
#include "Engine/GeometrySpace/Parametric/Intersection/CurveSurfaceIntersection.h"
#include "Engine/GeometrySpace/Parametric/Intersection/ImplicitSurface.h"
#include "Engine/GeometrySpace/Parametric/Intersection/IsoParameterCurve.h"
#include "Engine/GeometrySpace/Parametric/Geometry/RollingBallCarrier.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <tuple>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS — SHARED
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double FullTurn       = 2.0 * std::numbers::pi;                                // [rad] - 2π
    constexpr double ParallelSine   = 1.0e-12;                                               // [-]   - Axes / normals parallel below
    constexpr double ParameterSlack = 1.0e-9;                                                // [-]   - Box membership slack

    [[nodiscard]] const OrthonormalFrame& FrameOf(const SurfaceGeometry& Surface) noexcept
    {
        return std::visit([](const auto& Typed) -> const OrthonormalFrame&
        {
            using Kind = std::decay_t<decltype(Typed)>;
            if constexpr (std::is_same_v<Kind, RollingBallSurface> || std::is_same_v<Kind, NurbsSurface> || std::is_same_v<Kind, ParallelSurface>) { static const OrthonormalFrame Unused{}; return Unused; }   // Never an axis candidate
            else return Typed.Frame;
        }, Surface);
    }

    [[nodiscard]] Vector2 BoxCentre(const ParameterBox& Box) noexcept { return (Box.Low + Box.High) * 0.5; }

    // Everything the marching and the analytic clipper share about the pair.
    struct IntersectionContext
    {
        const SurfaceGeometry&             First;
        const ParameterBox&                FirstBox;
        const SurfaceGeometry&             Second;
        const ParameterBox&                SecondBox;
        const SurfaceIntersectionSettings& Settings;
        double                             Scale;                                            // [len] - Model size of the boxes
        SurfaceIntersectionOutcome&        Outcome;
    };

    [[nodiscard]] bool InsideBoth(const IntersectionContext& Context, Vector3 Position)
    {
        return InsideParameterBox(Context.First, Context.FirstBox, ProjectOntoSurface(Context.First, Position, BoxCentre(Context.FirstBox)), ParameterSlack)
            && InsideParameterBox(Context.Second, Context.SecondBox, ProjectOntoSurface(Context.Second, Position, BoxCentre(Context.SecondBox)), ParameterSlack);
    }

    // World-space sample grid of a box (for extents and line ranges).
    [[nodiscard]] std::vector<Vector3> SampleBox(const SurfaceGeometry& Surface, const ParameterBox& Box, int Count)
    {
        std::vector<Vector3> Samples;
        for (int Row = 0; Row <= Count; ++Row)
        {
            for (int Column = 0; Column <= Count; ++Column)
            {
                const Vector2 Parameter{ Box.Low.X + (Box.High.X - Box.Low.X) * Column / Count, Box.Low.Y + (Box.High.Y - Box.Low.Y) * Row / Count };
                Samples.push_back(EvaluateSurface(Surface, Parameter).Position);
            }
        }
        return Samples;
    }

    // ---- Pair probes and correctors -----------------------------------------------------------------------------------

    struct PairProbe
    {
        SurfaceDistance First  = {};                                                         // [-] - Distance to the first surface
        SurfaceDistance Second = {};                                                         // [-] - Distance to the second surface
        Vector3         Cross  = {};                                                         // [-] - N₁ × N₂ (curve direction)
        double          Sine   = 0.0;                                                        // [-] - |N₁ × N₂|
    };

    [[nodiscard]] PairProbe ProbePair(const IntersectionContext& Context, Vector3 Position)
    {
        PairProbe Probe;
        Probe.First  = MeasureSurfaceDistance(Context.First, Position);
        Probe.Second = MeasureSurfaceDistance(Context.Second, Position);
        Probe.Cross  = Cross(Probe.First.Normal, Probe.Second.Normal);
        Probe.Sine   = Length(Probe.Cross);
        return Probe;
    }

    [[nodiscard]] PairProbe ProbePairNear(const IntersectionContext& Context, Vector3 Position, Vector2& FirstHint, Vector2& SecondHint)
    {
        PairProbe Probe;
        Probe.First  = MeasureSurfaceDistanceNear(Context.First, Position, FirstHint);
        Probe.Second = MeasureSurfaceDistanceNear(Context.Second, Position, SecondHint);
        Probe.Cross  = Cross(Probe.First.Normal, Probe.Second.Normal);
        Probe.Sine   = Length(Probe.Cross);
        return Probe;
    }

    // 📝 Newton in 3D on { d₁(X) = 0, d₂(X) = 0, Π·(X − Q) = 0 } with rows N₁, N₂, Π — parameter-free, so poles and the
    //    apex are ordinary points. Inverse of the row matrix: (r₀·(b×c) + r₁·(c×a) + r₂·(a×b)) / (a·(b×c)).
    [[nodiscard]] bool CorrectOntoBoth(const IntersectionContext& Context, Vector3& Position, Vector3 PlaneNormal, Vector3 PlanePoint, Vector2 FirstSeed = { NAN, NAN },
                                       Vector2 SecondSeed = { NAN, NAN })
    {
        Context.Outcome.Statistics.Corrections += 1;
        Vector2 FirstHint  = FirstSeed;
        Vector2 SecondHint = SecondSeed;
        for (int Iteration = 0; Iteration < 16; ++Iteration)
        {
            const PairProbe Probe       = ProbePairNear(Context, Position, FirstHint, SecondHint);
            const Vector3&  A           = Probe.First.Normal;
            const Vector3&  B           = Probe.Second.Normal;
            const double    Determinant = Dot(A, Cross(B, PlaneNormal));                     // [-] - a·(b×c)
            if (std::fabs(Determinant) < 1.0e-12) return false;
            const double  PlaneResidual = Dot(PlaneNormal, Position - PlanePoint);            // [len] - Off the plane
            const Vector3 Step = (Cross(B, PlaneNormal) * Probe.First.Distance + Cross(PlaneNormal, A) * Probe.Second.Distance + Cross(A, B) * PlaneResidual) * (1.0 / Determinant);
            Position = Position - Step;
            if (Length(Step) <= 1.0e-15 * (1.0 + Length(Position))) break;
        }
        const PairProbe Final     = ProbePairNear(Context, Position, FirstHint, SecondHint);
        const double    Tolerance = std::max(1.0e-11 * (1.0 + Context.Scale), 1.0e-3 * Context.Settings.Precision);
        return std::fabs(Final.First.Distance) <= Tolerance && std::fabs(Final.Second.Distance) <= Tolerance;
    }

    // 📝 Singular / touch point: Gauss–Newton on R(X) = (d₁, d₂, L·(N₁ × N₂)) — both distances zero AND normals parallel.
    [[nodiscard]] bool RefineSpecialPoint(const IntersectionContext& Context, Vector3& Position, Vector2 FirstSeed = { NAN, NAN }, Vector2 SecondSeed = { NAN, NAN })
    {
        const double Weight     = std::max(1.0, Context.Scale);                              // [len] - Balances units
        Vector2      FirstHint  = FirstSeed;
        Vector2      SecondHint = SecondSeed;
        const auto   Residual   = [&](Vector3 Probe, Vector2& HintA, Vector2& HintB)
        {
            const PairProbe Pair = ProbePairNear(Context, Probe, HintA, HintB);
            return std::array<double, 5>{ Pair.First.Distance, Pair.Second.Distance, Pair.Cross.X * Weight, Pair.Cross.Y * Weight, Pair.Cross.Z * Weight };
        };
        const double Increment = 1.0e-7 * (1.0 + Context.Scale);                             // [len] - Difference step
        for (int Iteration = 0; Iteration < 40; ++Iteration)
        {
            const std::array<double, 5> Value = Residual(Position, FirstHint, SecondHint);
            std::array<std::array<double, 3>, 5> Jacobian{};
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Vector3 Forward = Position, Backward = Position;
                (Axis == 0 ? Forward.X : Axis == 1 ? Forward.Y : Forward.Z) += Increment;
                (Axis == 0 ? Backward.X : Axis == 1 ? Backward.Y : Backward.Z) -= Increment;
                Vector2 PlusA = FirstHint, PlusB = SecondHint, MinusA = FirstHint, MinusB = SecondHint;
                const std::array<double, 5> Plus = Residual(Forward, PlusA, PlusB), Minus = Residual(Backward, MinusA, MinusB);
                for (int Row = 0; Row < 5; ++Row) Jacobian[Row][Axis] = (Plus[Row] - Minus[Row]) / (2.0 * Increment);
            }
            double Normal[3][3] = {}, Gradient[3] = {};                                      // [-] - JᵀJ, JᵀR
            for (int Row = 0; Row < 5; ++Row)
            {
                for (int Column = 0; Column < 3; ++Column)
                {
                    Gradient[Column] += Jacobian[Row][Column] * Value[Row];
                    for (int Other = 0; Other < 3; ++Other) Normal[Column][Other] += Jacobian[Row][Column] * Jacobian[Row][Other];
                }
            }
            const double Damping = 1.0e-12 * (Normal[0][0] + Normal[1][1] + Normal[2][2]);
            for (int Diagonal = 0; Diagonal < 3; ++Diagonal) Normal[Diagonal][Diagonal] += Damping;
            const Vector3 RowA{ Normal[0][0], Normal[0][1], Normal[0][2] }, RowB{ Normal[1][0], Normal[1][1], Normal[1][2] }, RowC{ Normal[2][0], Normal[2][1], Normal[2][2] };
            const double  Determinant = Dot(RowA, Cross(RowB, RowC));
            if (std::fabs(Determinant) < 1.0e-300) return false;
            const Vector3 Step = (Cross(RowB, RowC) * Gradient[0] + Cross(RowC, RowA) * Gradient[1] + Cross(RowA, RowB) * Gradient[2]) * (1.0 / Determinant);
            Position = Position - Step;
            if (Length(Step) <= 1.0e-14 * (1.0 + Context.Scale)) break;
        }
        const PairProbe Final = ProbePairNear(Context, Position, FirstHint, SecondHint);
        return std::fabs(Final.First.Distance) <= 1.0e-9 * (1.0 + Context.Scale) && std::fabs(Final.Second.Distance) <= 1.0e-9 * (1.0 + Context.Scale) && Final.Sine <= 1.0e-5;
    }

    void RegisterSpecialPoint(const IntersectionContext& Context, Vector3 Position, SpecialPointCategory Category)
    {
        for (const IntersectionSpecialPoint& Known : Context.Outcome.SpecialPoints)
        {
            if (Distance(Known.Position, Position) <= 1.0e-6 * (1.0 + Context.Scale)) return;
        }
        if (InsideBoth(Context, Position)) Context.Outcome.SpecialPoints.push_back({ Position, Category });
    }

    [[nodiscard]] double PolylineDistance(const std::vector<IntersectionChartPoint>& Chart, bool ClosedCondition, Vector3 Position)
    {
        double Nearest = Chart.empty() ? 1.0e300 : Distance(Chart.front().Position, Position);
        const size_t SegmentCount = Chart.size() < 2 ? 0 : Chart.size() - (ClosedCondition ? 0 : 1);
        for (size_t SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
        {
            const Vector3 Start = Chart[SegmentIndex].Position;
            const Vector3 Span  = Chart[(SegmentIndex + 1) % Chart.size()].Position - Start;
            const double  λ     = std::clamp(Dot(Position - Start, Span) / std::max(Dot(Span, Span), 1.0e-300), 0.0, 1.0);
            Nearest = std::min(Nearest, Distance(Start + Span * λ, Position));
        }
        return Nearest;
    }

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS — ANALYTIC DISPATCH
//------------------------------------------------------------------------------------------------------------------------

    // 📝 A planar primitive: a line (Point, unit Direction) or a circle (Point = centre, Radius).
    struct PlanarPrimitive
    {
        bool    CircleCondition = false;                                                     // [-]   - Circle, else line
        Vector2 Point           = {};                                                        // [len] - Line point / circle centre
        Vector2 Direction       = { 1.0, 0.0 };                                              // [-]   - Unit line direction
        double  Radius          = 0.0;                                                       // [len] - Circle radius
    };

    struct PlanarHit
    {
        Vector2 Position         = {};                                                       // [len] - Intersection point
        bool    TangentCondition = false;                                                    // [-]   - Primitives touch
    };

    struct PlanarOutcome
    {
        std::vector<PlanarHit> Hits                = {};                                     // [-] - Points
        bool                   CoincidentCondition = false;                                  // [-] - Same primitive
    };

    [[nodiscard]] Vector2 Perpendicular(Vector2 Vector) noexcept { return { -Vector.Y, Vector.X }; }

    [[nodiscard]] PlanarOutcome IntersectPlanarPrimitives(const PlanarPrimitive& A, const PlanarPrimitive& B, double Precision)
    {
        PlanarOutcome Outcome;
        if (!A.CircleCondition && !B.CircleCondition)
        {
            const double Denominator = Cross(A.Direction, B.Direction);
            if (std::fabs(Denominator) <= ParallelSine)
            {
                Outcome.CoincidentCondition = std::fabs(Cross(B.Point - A.Point, A.Direction)) <= Precision;
                return Outcome;
            }
            Outcome.Hits.push_back({ A.Point + A.Direction * (Cross(B.Point - A.Point, B.Direction) / Denominator), false });
            return Outcome;
        }
        if (A.CircleCondition != B.CircleCondition)
        {
            const PlanarPrimitive& Line   = A.CircleCondition ? B : A;
            const PlanarPrimitive& Circle = A.CircleCondition ? A : B;
            const Vector2 Foot  = Line.Point + Line.Direction * Dot(Circle.Point - Line.Point, Line.Direction);
            const double  Gap   = Distance(Foot, Circle.Point);                               // [len] - Centre to line
            if (Gap > Circle.Radius + Precision) return Outcome;
            if (std::fabs(Gap - Circle.Radius) <= Precision)
            {
                const Vector2 Touch = Gap > 0.0 ? Circle.Point + (Foot - Circle.Point) * (Circle.Radius / Gap) : Foot;
                Outcome.Hits.push_back({ Touch, true });
                return Outcome;
            }
            const double Half = std::sqrt(Circle.Radius * Circle.Radius - Gap * Gap);        // [len] - Half chord
            Outcome.Hits.push_back({ Foot - Line.Direction * Half, false });
            Outcome.Hits.push_back({ Foot + Line.Direction * Half, false });
            return Outcome;
        }
        const double Separation = Distance(A.Point, B.Point);                                // [len] - Centre distance
        if (Separation <= Precision)
        {
            Outcome.CoincidentCondition = std::fabs(A.Radius - B.Radius) <= Precision;
            return Outcome;
        }
        if (Separation > A.Radius + B.Radius + Precision || Separation < std::fabs(A.Radius - B.Radius) - Precision) return Outcome;
        const Vector2 Axis = (B.Point - A.Point) * (1.0 / Separation);                       // [-] - Centre line
        if (std::fabs(Separation - (A.Radius + B.Radius)) <= Precision)
        {
            Outcome.Hits.push_back({ A.Point + Axis * A.Radius, true });
            return Outcome;
        }
        if (std::fabs(Separation - std::fabs(A.Radius - B.Radius)) <= Precision)
        {
            Outcome.Hits.push_back({ A.Point + Axis * (A.Radius >= B.Radius ? A.Radius : -A.Radius), true });
            return Outcome;
        }
        const double Along = (Separation * Separation + A.Radius * A.Radius - B.Radius * B.Radius) / (2.0 * Separation);
        const double Half  = std::sqrt(std::max(0.0, A.Radius * A.Radius - Along * Along));  // [len] - Half chord
        const Vector2 Middle = A.Point + Axis * Along;
        Outcome.Hits.push_back({ Middle - Perpendicular(Axis) * Half, false });
        Outcome.Hits.push_back({ Middle + Perpendicular(Axis) * Half, false });
        return Outcome;
    }

    // 📝 Meridian primitives of a surface of revolution about Axis, in (ρ, z) with z along Axis.ZAxis from Axis.Origin.
    //    Mirrors (ρ → −ρ) are included so the union is symmetric and every ρ ≥ 0 hit is found.
    [[nodiscard]] std::optional<std::vector<PlanarPrimitive>> RevolveAbout(const SurfaceGeometry& Surface, const OrthonormalFrame& Axis, double Precision)
    {
        const auto OnAxis = [&](Vector3 Point) { return Length(Cross(Point - Axis.Origin, Axis.ZAxis)) <= Precision; };
        const auto Height = [&](Vector3 Point) { return Dot(Point - Axis.Origin, Axis.ZAxis); };
        if (ProceduralSurfaceCondition(Surface)) return std::nullopt;                       // Not an analytic revolution
        if (const auto* Plane = std::get_if<PlaneSurface>(&Surface))
        {
            if (Length(Cross(Plane->Frame.ZAxis, Axis.ZAxis)) > ParallelSine) return std::nullopt;
            return std::vector<PlanarPrimitive>{ { false, { 0.0, Height(Plane->Frame.Origin) }, { 1.0, 0.0 }, 0.0 } };
        }
        if (const auto* Sphere = std::get_if<SphereSurface>(&Surface))
        {
            if (!OnAxis(Sphere->Frame.Origin)) return std::nullopt;
            return std::vector<PlanarPrimitive>{ { true, { 0.0, Height(Sphere->Frame.Origin) }, {}, Sphere->Radius } };
        }
        const OrthonormalFrame& Frame = FrameOf(Surface);
        if (Length(Cross(Frame.ZAxis, Axis.ZAxis)) > ParallelSine || !OnAxis(Frame.Origin)) return std::nullopt;
        const double h = Height(Frame.Origin);                                               // [len] - Frame height on the axis
        const double σ = Dot(Frame.ZAxis, Axis.ZAxis) > 0.0 ? 1.0 : -1.0;                    // [-]   - Axis orientation
        if (const auto* Cylinder = std::get_if<CylinderSurface>(&Surface))
        {
            return std::vector<PlanarPrimitive>{ { false, { Cylinder->Radius, h }, { 0.0, 1.0 }, 0.0 }, { false, { -Cylinder->Radius, h }, { 0.0, 1.0 }, 0.0 } };
        }
        if (const auto* Cone = std::get_if<ConeSurface>(&Surface))
        {
            const double s = Cone->RadialSlope;                                              // [-] - dr/d𝑣
            return std::vector<PlanarPrimitive>{ { false, { Cone->OriginRadius, h }, Normalize(Vector2{ s, σ }), 0.0 },
                                                 { false, { -Cone->OriginRadius, h }, Normalize(Vector2{ -s, σ }), 0.0 } };
        }
        const auto& Torus = std::get<TorusSurface>(Surface);
        return std::vector<PlanarPrimitive>{ { true, { Torus.MajorRadius, h }, {}, Torus.MinorRadius }, { true, { -Torus.MajorRadius, h }, {}, Torus.MinorRadius } };
    }

    [[nodiscard]] Vector3 AnyPerpendicular(Vector3 Axis) noexcept
    {
        return std::fabs(Axis.X) < 0.9 ? Cross(Axis, Vector3{ 1.0, 0.0, 0.0 }) : Cross(Axis, Vector3{ 0.0, 1.0, 0.0 });
    }

    // Candidate common axes of revolution for the pair.
    [[nodiscard]] std::vector<OrthonormalFrame> CandidateAxes(const SurfaceGeometry& First, const SurfaceGeometry& Second)
    {
        std::vector<OrthonormalFrame> Axes;
        for (const SurfaceGeometry* Surface : { &First, &Second })
        {
            if (std::holds_alternative<CylinderSurface>(*Surface) || std::holds_alternative<ConeSurface>(*Surface) || std::holds_alternative<TorusSurface>(*Surface)) Axes.push_back(FrameOf(*Surface));
        }
        const auto* FirstSphere  = std::get_if<SphereSurface>(&First);
        const auto* SecondSphere = std::get_if<SphereSurface>(&Second);
        const auto* FirstPlane   = std::get_if<PlaneSurface>(&First);
        const auto* SecondPlane  = std::get_if<PlaneSurface>(&Second);
        if (FirstSphere && SecondSphere)
        {
            const Vector3 Join = SecondSphere->Frame.Origin - FirstSphere->Frame.Origin;     // [len] - Centre to centre
            Axes.push_back(Length(Join) > 0.0 ? ConstructFrame(FirstSphere->Frame.Origin, Join, AnyPerpendicular(Normalize(Join))) : FirstSphere->Frame);
        }
        const SphereSurface* Sphere = FirstSphere ? FirstSphere : SecondSphere;
        const PlaneSurface*  Plane  = FirstPlane ? FirstPlane : SecondPlane;
        if (Sphere && Plane) Axes.push_back(ConstructFrame(Sphere->Frame.Origin, Plane->Frame.ZAxis, Plane->Frame.XAxis));
        return Axes;
    }

    // 📝 Clip an exact curve to both boxes: dense membership samples + bisection of every transition.
    struct CurveInterval
    {
        double Low  = 0.0;                                                                   // [-] - τ start
        double High = 0.0;                                                                   // [-] - τ end
    };

    [[nodiscard]] std::vector<CurveInterval> ClipCurveToBoxes(const IntersectionContext& Context, const CurveGeometry& Curve, double Low, double High, bool PeriodicCurve)
    {
        constexpr int SampleCount = 720;
        const auto Inside = [&](double τ) { return InsideBoth(Context, EvaluateCurve(Curve, τ).Position); };
        std::vector<CurveInterval> Intervals;
        bool   Previous      = Inside(Low);
        double IntervalStart = Low;                                                          // [-] - Open interval start
        for (int Sample = 1; Sample <= SampleCount; ++Sample)
        {
            const double τ       = Low + (High - Low) * Sample / SampleCount;
            const bool   Current = Inside(τ);
            if (Current == Previous) continue;
            double Outside = Current ? Low + (High - Low) * (Sample - 1) / SampleCount : τ;  // [-] - Bisection bracket
            double Within  = Current ? τ : Low + (High - Low) * (Sample - 1) / SampleCount;
            for (int Halving = 0; Halving < 60; ++Halving)
            {
                const double Middle = 0.5 * (Outside + Within);
                (Inside(Middle) ? Within : Outside) = Middle;
            }
            if (Current) IntervalStart = Within;
            else Intervals.push_back({ IntervalStart, Within });
            Previous = Current;
        }
        if (Previous) Intervals.push_back({ IntervalStart, High });
        // A closed curve inside at both ends: the last interval wraps into the first.
        if (PeriodicCurve && Intervals.size() > 1 && Intervals.front().Low == Low && Intervals.back().High == High)
        {
            Intervals.front().Low = Intervals.back().Low - (High - Low);
            Intervals.pop_back();
        }
        return Intervals;
    }

    void RegisterAnalyticBranch(const IntersectionContext& Context, const CurveGeometry& Curve, double Low, double High, bool TangentialCondition)
    {
        const bool PeriodicCurve = PeriodicCondition(Curve);
        for (const CurveInterval& Interval : ClipCurveToBoxes(Context, Curve, Low, High, PeriodicCurve))
        {
            IntersectionBranch Branch;
            Branch.Category            = PeriodicCurve ? BranchCategory::Circle : BranchCategory::Line;
            Branch.Curve               = Curve;
            Branch.CurveLow            = Interval.Low;
            Branch.CurveHigh           = Interval.High;
            Branch.TangentialCondition = TangentialCondition;
            Branch.ClosedCondition     = PeriodicCurve && Interval.High - Interval.Low >= FullTurn - 1.0e-12;
            Branch.StartTermination    = Branch.ClosedCondition ? BranchTermination::Closed : BranchTermination::Boundary;
            Branch.EndTermination      = Branch.StartTermination;
            // 📝 Chart spacing from the chord tolerance: Δτ = 2·acos(1 − ε/r) on circles; lines need only the ends.
            int Steps = 1;                                                                   // [-] - Chart segments
            const double Bend = std::holds_alternative<CircularArc>(Curve)   ? std::get<CircularArc>(Curve).Radius
                              : std::holds_alternative<EllipticalArc>(Curve) ? std::get<EllipticalArc>(Curve).MajorRadius : 0.0;  // [len] - Largest radius of curvature scale
            if (Bend > 0.0)
            {
                const double Spacing = std::min(std::numbers::pi / 24.0, 2.0 * std::acos(std::max(-1.0, 1.0 - Context.Settings.ChordTolerance / Bend)));
                Steps = std::max(4, static_cast<int>(std::ceil((Interval.High - Interval.Low) / Spacing)));
            }
            Vector2 FirstReference = BoxCentre(Context.FirstBox), SecondReference = BoxCentre(Context.SecondBox);
            const int Last = Branch.ClosedCondition ? Steps - 1 : Steps;                     // [-] - Closed: no repeated end
            for (int Step = 0; Step <= Last; ++Step)
            {
                const Vector3 Position = EvaluateCurve(Curve, Interval.Low + (Interval.High - Interval.Low) * Step / Steps).Position;
                FirstReference  = ProjectOntoSurface(Context.First, Position, FirstReference);
                SecondReference = ProjectOntoSurface(Context.Second, Position, SecondReference);
                Branch.Chart.push_back({ Position, FirstReference, SecondReference });
            }
            Context.Outcome.Branches.push_back(std::move(Branch));
        }
    }

    // Finite parameter range of a line inside both boxes (projection of box samples), widened a little.
    [[nodiscard]] std::optional<CurveInterval> LineRange(const IntersectionContext& Context, const StraightLine& Line)
    {
        CurveInterval Range{ -1.0e300, 1.0e300 };
        for (const auto& [Surface, Box] : { std::pair{ &Context.First, &Context.FirstBox }, std::pair{ &Context.Second, &Context.SecondBox } })
        {
            double Low = 1.0e300, High = -1.0e300;                                           // [len] - Projection extent
            for (const Vector3& Sample : SampleBox(*Surface, *Box, 12))
            {
                const double τ = Dot(Sample - Line.Origin, Line.Direction);
                Low = std::min(Low, τ); High = std::max(High, τ);
            }
            const double Margin = 0.05 * (High - Low) + 1.0e-9;
            Range.Low  = std::max(Range.Low, Low - Margin);
            Range.High = std::min(Range.High, High + Margin);
        }
        if (Range.High <= Range.Low) return std::nullopt;
        return Range;
    }

    void RegisterLine(const IntersectionContext& Context, const StraightLine& Line, bool TangentialCondition)
    {
        if (const auto Range = LineRange(Context, Line)) RegisterAnalyticBranch(Context, Line, Range->Low, Range->High, TangentialCondition);
    }

    // ① Coaxial surfaces of revolution: meridian intersection in (ρ, z).
    [[nodiscard]] bool SolveCoaxial(const IntersectionContext& Context)
    {
        const double Precision = Context.Settings.Precision;
        for (const OrthonormalFrame& Axis : CandidateAxes(Context.First, Context.Second))
        {
            const auto FirstMeridian  = RevolveAbout(Context.First, Axis, Precision);
            const auto SecondMeridian = RevolveAbout(Context.Second, Axis, Precision);
            if (!FirstMeridian || !SecondMeridian) continue;
            std::vector<PlanarHit> Hits;
            for (const PlanarPrimitive& A : *FirstMeridian)
            {
                for (const PlanarPrimitive& B : *SecondMeridian)
                {
                    const PlanarOutcome Planar = IntersectPlanarPrimitives(A, B, Precision);
                    if (Planar.CoincidentCondition)
                    {
                        Context.Outcome.CoincidentCondition = true;
                        Context.Outcome.Method = "Analytic · coaxial meridians (coincident)";
                        return true;
                    }
                    for (const PlanarHit& Hit : Planar.Hits)
                    {
                        if (Hit.Position.X < -Precision) continue;
                        const bool Known = std::any_of(Hits.begin(), Hits.end(), [&](const PlanarHit& Other) { return Distance(Other.Position, Hit.Position) <= Precision; });
                        if (!Known) Hits.push_back(Hit);
                    }
                }
            }
            for (const PlanarHit& Hit : Hits)
            {
                const Vector3 Centre = Axis.Origin + Axis.ZAxis * Hit.Position.Y;
                if (Hit.Position.X <= Precision) { RegisterSpecialPoint(Context, Centre, SpecialPointCategory::Touch); continue; }
                RegisterAnalyticBranch(Context, CircularArc{ { Centre, Axis.XAxis, Axis.YAxis, Axis.ZAxis }, Hit.Position.X }, 0.0, FullTurn, Hit.TangentCondition);
            }
            Context.Outcome.Method = "Analytic · coaxial meridians";
            return true;
        }
        return false;
    }

    // ② Parallel cylinders, or a plane parallel to a cylinder axis: cross-section ⟂ the axis.
    [[nodiscard]] bool SolveParallelCrossSection(const IntersectionContext& Context)
    {
        const auto* FirstCylinder  = std::get_if<CylinderSurface>(&Context.First);
        const auto* SecondCylinder = std::get_if<CylinderSurface>(&Context.Second);
        const CylinderSurface* Cylinder = FirstCylinder ? FirstCylinder : SecondCylinder;
        if (!Cylinder) return false;
        const SurfaceGeometry& Other = FirstCylinder ? Context.Second : Context.First;
        const OrthonormalFrame& Frame = Cylinder->Frame;
        const auto Section = [&](Vector3 Point) { return Vector2{ Dot(Point - Frame.Origin, Frame.XAxis), Dot(Point - Frame.Origin, Frame.YAxis) }; };
        PlanarPrimitive OtherSection;
        if (const auto* OtherCylinder = std::get_if<CylinderSurface>(&Other))
        {
            if (Length(Cross(OtherCylinder->Frame.ZAxis, Frame.ZAxis)) > ParallelSine) return false;
            OtherSection = { true, Section(OtherCylinder->Frame.Origin), {}, OtherCylinder->Radius };
        }
        else if (const auto* Plane = std::get_if<PlaneSurface>(&Other))
        {
            if (std::fabs(Dot(Plane->Frame.ZAxis, Frame.ZAxis)) > ParallelSine) return false;
            const Vector2 Normal{ Dot(Plane->Frame.ZAxis, Frame.XAxis), Dot(Plane->Frame.ZAxis, Frame.YAxis) };
            OtherSection = { false, Section(Plane->Frame.Origin), Perpendicular(Normalize(Normal)), 0.0 };
        }
        else return false;
        const PlanarOutcome Planar = IntersectPlanarPrimitives({ true, { 0.0, 0.0 }, {}, Cylinder->Radius }, OtherSection, Context.Settings.Precision);
        Context.Outcome.Method = "Analytic · parallel cross-section";
        if (Planar.CoincidentCondition) { Context.Outcome.CoincidentCondition = true; return true; }
        for (const PlanarHit& Hit : Planar.Hits) RegisterLine(Context, StraightLine{ Frame.Origin + Frame.XAxis * Hit.Position.X + Frame.YAxis * Hit.Position.Y, Frame.ZAxis }, Hit.TangentCondition);
        return true;
    }

    // ③ A plane containing a cone or torus axis: two generator lines / two meridian circles.
    [[nodiscard]] bool SolvePlaneThroughAxis(const IntersectionContext& Context)
    {
        const auto* FirstPlane = std::get_if<PlaneSurface>(&Context.First);
        const PlaneSurface* Plane = FirstPlane ? FirstPlane : std::get_if<PlaneSurface>(&Context.Second);
        if (!Plane) return false;
        const SurfaceGeometry& Other = FirstPlane ? Context.Second : Context.First;
        if (!std::holds_alternative<ConeSurface>(Other) && !std::holds_alternative<TorusSurface>(Other)) return false;
        const OrthonormalFrame& Frame = FrameOf(Other);
        if (std::fabs(Dot(Plane->Frame.ZAxis, Frame.ZAxis)) > ParallelSine || std::fabs(Dot(Frame.Origin - Plane->Frame.Origin, Plane->Frame.ZAxis)) > Context.Settings.Precision) return false;
        const Vector3 InPlane = Normalize(Cross(Plane->Frame.ZAxis, Frame.ZAxis));           // [-] - Radial direction in the plane
        Context.Outcome.Method = "Analytic · plane through axis";
        if (const auto* Cone = std::get_if<ConeSurface>(&Other))
        {
            for (const double Side : { 1.0, -1.0 })
            {
                RegisterLine(Context, StraightLine{ Frame.Origin + InPlane * (Side * Cone->OriginRadius), Normalize(InPlane * (Side * Cone->RadialSlope) + Frame.ZAxis) }, false);
            }
            return true;
        }
        const auto& Torus = std::get<TorusSurface>(Other);
        for (const double Side : { 1.0, -1.0 })
        {
            const Vector3 Radial = InPlane * Side;                                           // [-] - Meridian circle X axis
            RegisterAnalyticBranch(Context, CircularArc{ { Frame.Origin + Radial * Torus.MajorRadius, Radial, Frame.ZAxis, Cross(Radial, Frame.ZAxis) }, Torus.MinorRadius }, 0.0, FullTurn, false);
        }
        return true;
    }

    // ⑤ Equal-radius cylinders whose axes meet (Monge): for X on both, ((X − C)·𝐚)² = ((X − C)·𝐛)², so the curve is the
    //    two ellipses in the bisector planes (X − C)·(𝐚 ∓ 𝐛) = 0. Both share the minor axis ±r·𝐧 (𝐧 = 𝐚 × 𝐛 unit), where
    //    N₁ ∥ N₂: the singular points that a marcher cannot cross reliably, registered here so the boolean splits there.
    //    Plane ⟂ (𝐚 − 𝐛): major r / sin(θ/2) along 𝐚 + 𝐛; plane ⟂ (𝐚 + 𝐛): major r / cos(θ/2) along 𝐚 − 𝐛.
    [[nodiscard]] bool SolveEqualCylinders(const IntersectionContext& Context)
    {
        const auto* First  = std::get_if<CylinderSurface>(&Context.First);
        const auto* Second = std::get_if<CylinderSurface>(&Context.Second);
        if (!First || !Second) return false;
        const double Precision = Context.Settings.Precision;
        if (std::fabs(First->Radius - Second->Radius) > Precision) return false;
        const Vector3 AxisA = First->Frame.ZAxis, AxisB = Second->Frame.ZAxis;
        const Vector3 Normal = Cross(AxisA, AxisB);                                          // [-]   - 𝐚 × 𝐛 (|·| = sin θ)
        const double  Sine = Length(Normal);                                                 // [-]   - sin θ
        if (Sine <= ParallelSine) return false;
        const Vector3 Join = Second->Frame.Origin - First->Frame.Origin;                     // [len] - o₂ − o₁
        if (std::fabs(Dot(Join, Normal)) / Sine > Precision) return false;                  // Skew axes: a general quartic
        // Meeting point C = o₁ + s·𝐚 with s from the 2×2 normal equations of the closest approach.
        const double  Cosine = Dot(AxisA, AxisB);                                            // [-]   - cos θ
        const double  Reach  = (Dot(Join, AxisA) - Cosine * Dot(Join, AxisB)) / (Sine * Sine);  // [len] - s along 𝐚
        const Vector3 Centre = First->Frame.Origin + AxisA * Reach;                         // [len] - C
        const Vector3 Minor  = Normal * (1.0 / Sine);                                        // [-]   - 𝐧
        const double  Radius = 0.5 * (First->Radius + Second->Radius);                       // [len] - r
        // 📝 Each ellipse is registered as its two halves between the singular points (τ = ±π/2 on the minor axis), the
        //    same contract as marching's split at singular points: four open arcs with Singular ends.
        const size_t Before = Context.Outcome.Branches.size();                               // [-] - First new branch
        for (const double Side : { -1.0, 1.0 })
        {
            const Vector3 Major = Normalize(AxisA - AxisB * Side);                            // [-]   - In-plane major direction
            const double  Lean  = std::fabs(Dot(AxisA, Normalize(AxisA + AxisB * Side)));    // [-]   - |𝐚 · plane normal|
            const OrthonormalFrame Frame{ Centre, Major, Minor, Cross(Major, Minor) };
            for (const double Low : { -0.5 * std::numbers::pi, 0.5 * std::numbers::pi }) RegisterAnalyticBranch(Context, EllipticalArc{ Frame, Radius / Lean, Radius }, Low, Low + std::numbers::pi, false);
        }
        std::vector<Vector3> Singular;                                                       // [len] - The two crossing points
        for (const double Side : { -1.0, 1.0 })
        {
            Singular.push_back(Centre + Minor * (Side * Radius));
            RegisterSpecialPoint(Context, Singular.back(), SpecialPointCategory::Singular);
        }
        for (size_t Index = Before; Index < Context.Outcome.Branches.size(); ++Index)
        {
            IntersectionBranch& Branch = Context.Outcome.Branches[Index];
            const auto AtSingular = [&](double τ)
            {
                const Vector3 End = EvaluateCurve(Branch.Curve, τ).Position;
                return std::any_of(Singular.begin(), Singular.end(), [&](Vector3 Point) { return Distance(Point, End) <= Precision; });
            };
            if (AtSingular(Branch.CurveLow))  Branch.StartTermination = BranchTermination::Singular;
            if (AtSingular(Branch.CurveHigh)) Branch.EndTermination   = BranchTermination::Singular;
        }
        Context.Outcome.Method = "Analytic · equal cylinders with meeting axes (Monge ellipses)";
        return true;
    }

    // ④ Plane–plane.
    [[nodiscard]] bool SolvePlanePlane(const IntersectionContext& Context)
    {
        const auto* FirstPlane  = std::get_if<PlaneSurface>(&Context.First);
        const auto* SecondPlane = std::get_if<PlaneSurface>(&Context.Second);
        if (!FirstPlane || !SecondPlane) return false;
        Context.Outcome.Method = "Analytic · plane–plane";
        const Vector3 FirstNormal = FirstPlane->Frame.ZAxis, SecondNormal = SecondPlane->Frame.ZAxis;
        const Vector3 Direction   = Cross(FirstNormal, SecondNormal);                        // [-] - Line direction
        const double  FirstHeight = Dot(FirstNormal, FirstPlane->Frame.Origin), SecondHeight = Dot(SecondNormal, SecondPlane->Frame.Origin);
        if (Length(Direction) <= ParallelSine)
        {
            Context.Outcome.CoincidentCondition = std::fabs(Dot(SecondPlane->Frame.Origin - FirstPlane->Frame.Origin, FirstNormal)) <= Context.Settings.Precision;
            return true;
        }
        const Vector3 Point = (Cross(SecondNormal, Direction) * FirstHeight + Cross(Direction, FirstNormal) * SecondHeight) * (1.0 / Dot(Direction, Direction));
        RegisterLine(Context, StraightLine{ Point, Normalize(Direction) }, false);
        return true;
    }

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS — MARCHING
//------------------------------------------------------------------------------------------------------------------------

    struct HalfTrace
    {
        std::vector<IntersectionChartPoint> Chart       = {};                                // [-] - From the seed outward
        BranchTermination                   Termination = BranchTermination::Limit;          // [-] - How it ended
    };

    [[nodiscard]] IntersectionChartPoint ChartPoint(const IntersectionContext& Context, Vector3 Position, Vector2 FirstReference, Vector2 SecondReference)
    {
        return { Position, ProjectOntoSurface(Context.First, Position, FirstReference), ProjectOntoSurface(Context.Second, Position, SecondReference) };
    }

    [[nodiscard]] IntersectionChartPoint ChartPointNear(const IntersectionContext& Context, Vector3 Position, Vector2 FirstReference, Vector2 SecondReference)
    {
        return { Position, ProjectOntoSurfaceNear(Context.First, Position, FirstReference), ProjectOntoSurfaceNear(Context.Second, Position, SecondReference) };
    }

    // 💡 NURBS and open rolling-ball feet are confined to their domains, so a point marched past a NURBS edge or open
    //    spine end still projects inside the box (and its signed distance there can stay ≈ 0 on the clamped end-ball):
    //    the point is on the carrier only when its evaluated foot actually reaches it (≤ 10·precision).
    [[nodiscard]] bool FootReaches(const IntersectionContext& Context, const SurfaceGeometry& Surface, Vector2 Foot, Vector3 Position)
    {
        if (ResolveNurbsChart(Surface) == nullptr && !std::holds_alternative<RollingBallSurface>(Surface)) return true;
        return Distance(EvaluateSurface(Surface, Foot).Position, Position) <= 10.0 * Context.Settings.Precision;
    }

    [[nodiscard]] bool InsideBoxes(const IntersectionContext& Context, const IntersectionChartPoint& Point)
    {
        return InsideParameterBox(Context.First, Context.FirstBox, Point.First, ParameterSlack) && InsideParameterBox(Context.Second, Context.SecondBox, Point.Second, ParameterSlack)
            && FootReaches(Context, Context.First, Point.First, Point.Position) && FootReaches(Context, Context.Second, Point.Second, Point.Position);
    }

    [[nodiscard]] HalfTrace TraceHalf(const IntersectionContext& Context, const IntersectionChartPoint& Seed, double Sign)
    {
        const SurfaceIntersectionSettings& Settings = Context.Settings;
        const double MinimumStep = 1.0e-9 * (1.0 + Context.Scale);                           // [len] - Give-up step
        HalfTrace    Trace;
        Trace.Chart.push_back(Seed);
        IntersectionChartPoint Current   = Seed;
        Vector2                SeedA     = Seed.First;
        Vector2                SeedB     = Seed.Second;
        Vector3                Tangent   = Normalize(ProbePairNear(Context, Seed.Position, SeedA, SeedB).Cross) * Sign;
        double                 Step      = 0.25 * Settings.MaximumStep;                      // [len] - Current step
        double                 Travel    = 0.0;                                              // [len] - Arc length so far
        int                    TinySteps = 0;                                                // [-]   - Consecutive near-minimum steps
        while (static_cast<int>(Trace.Chart.size()) < Settings.PointLimit)
        {
            // ---- Predict, correct, and judge the step.
            Vector3 Corrected = Current.Position + Tangent * Step;
            Vector3 NextTangent{};
            Vector2 StepHintA = Current.First;
            Vector2 StepHintB = Current.Second;
            bool    Accepted  = CorrectOntoBoth(Context, Corrected, Tangent, Current.Position + Tangent * Step, StepHintA, StepHintB);
            if (Accepted)
            {
                const PairProbe Probe = ProbePairNear(Context, Corrected, StepHintA, StepHintB);
                if (Probe.Sine < Settings.SingularSine)
                {
                    // 📝 Approaching N₁ ∥ N₂: locate the special point and end the branch there.
                    Vector3 Special = Corrected;
                    if (RefineSpecialPoint(Context, Special, StepHintA, StepHintB) && Distance(Special, Current.Position) <= 4.0 * Step)
                    {
                        Trace.Chart.push_back(ChartPointNear(Context, Special, StepHintA, StepHintB));
                        RegisterSpecialPoint(Context, Special, SpecialPointCategory::Singular);
                        Trace.Termination = BranchTermination::Singular;
                        return Trace;
                    }
                    Accepted = false;
                }
                else
                {
                    NextTangent = Normalize(Probe.Cross);
                    if (Dot(NextTangent, Tangent) < 0.0) NextTangent = -NextTangent;
                    const double Turn = std::acos(std::clamp(Dot(NextTangent, Tangent), -1.0, 1.0));
                    Accepted = Turn <= Settings.MaximumTurn && Distance(Corrected, Current.Position + Tangent * Step) <= 0.5 * Step;
                    if (Accepted)
                    {
                        const Vector3 Chord    = Corrected - Current.Position;
                        const Vector3 Midpoint = (Corrected + Current.Position) * 0.5;
                        Vector3       OnCurve  = Midpoint;
                        Vector2       MidHintA = Current.First;
                        Vector2       MidHintB = Current.Second;
                        Accepted = CorrectOntoBoth(Context, OnCurve, Normalize(Chord), Midpoint, MidHintA, MidHintB) && Distance(OnCurve, Midpoint) <= Settings.ChordTolerance;
                    }
                }
            }
            if (!Accepted)
            {
                Context.Outcome.Statistics.StepRejections += 1;
                Step *= 0.5;
                if (Step >= MinimumStep) continue;
                Vector3 Special      = Current.Position;
                Vector2 SpecialHintA = Current.First;
                Vector2 SpecialHintB = Current.Second;
                if (RefineSpecialPoint(Context, Special, SpecialHintA, SpecialHintB) && Distance(Special, Current.Position) <= 1.0e-6 * (1.0 + Context.Scale))
                {
                    RegisterSpecialPoint(Context, Special, SpecialPointCategory::Singular);
                }
                Trace.Termination = BranchTermination::Singular;
                return Trace;
            }

            IntersectionChartPoint Next = ChartPointNear(Context, Corrected, StepHintA, StepHintB);

            // ---- Boundary: bisect the step to the box edge.
            if (!InsideBoxes(Context, Next))
            {
                double Within = 0.0, Outside = Step;                                         // [len] - Bisection bracket
                IntersectionChartPoint Edge = Current;
                for (int Halving = 0; Halving < 60; ++Halving)
                {
                    const double Middle     = 0.5 * (Within + Outside);
                    Vector3      Probe      = Current.Position + Tangent * Middle;
                    Vector2      ProbeHintA = Edge.First;
                    Vector2      ProbeHintB = Edge.Second;
                    if (!CorrectOntoBoth(Context, Probe, Tangent, Probe, ProbeHintA, ProbeHintB)) { Outside = Middle; continue; }
                    const IntersectionChartPoint Candidate = ChartPointNear(Context, Probe, ProbeHintA, ProbeHintB);
                    if (InsideBoxes(Context, Candidate)) { Within = Middle; Edge = Candidate; }
                    else Outside = Middle;
                }
                Edge = ChartPoint(Context, Edge.Position, Edge.First, Edge.Second);
                if (Distance(Edge.Position, Current.Position) > 1.0e-9 * (1.0 + Context.Scale)) Trace.Chart.push_back(Edge);
                Trace.Termination = BranchTermination::Boundary;
                return Trace;
            }

            // ---- Closure: the step passes the seed again.
            const double Moved = Distance(Corrected, Current.Position);
            Travel += Moved;
            if (Trace.Chart.size() >= 3 && Travel > 2.0 * Step)
            {
                const Vector3 Span = Corrected - Current.Position;
                const double  λ    = Dot(Seed.Position - Current.Position, Span) / std::max(Dot(Span, Span), 1.0e-300);
                if (λ > 0.0 && λ <= 1.0 + 1.0e-9 && Distance(Current.Position + Span * λ, Seed.Position) <= std::max(2.0 * Settings.ChordTolerance, 1.0e-6))
                {
                    Trace.Termination = BranchTermination::Closed;
                    return Trace;
                }
            }

            // ---- Stagnation guard: prevent 4096 microscopic steps oscillating near MinimumStep at a singular pinch.
            if (Moved <= 8.0 * MinimumStep)
            {
                if (++TinySteps >= 32)
                {
                    Trace.Termination = BranchTermination::Singular;
                    return Trace;
                }
            }
            else TinySteps = 0;

            Trace.Chart.push_back(Next);
            Current = Next;
            Tangent = NextTangent;
            Step    = std::min(1.5 * Step, Settings.MaximumStep);
        }
        Trace.Termination = BranchTermination::Limit;
        return Trace;
    }

    [[nodiscard]] bool OnKnownBranch(const IntersectionContext& Context, Vector3 Position)
    {
        const double Reach = 1.5 * Context.Settings.ChordTolerance + 10.0 * Context.Settings.Precision;
        for (const IntersectionBranch& Branch : Context.Outcome.Branches)
        {
            if (PolylineDistance(Branch.Chart, Branch.ClosedCondition, Position) <= Reach) return true;
        }
        return false;
    }

    void TraceFromSeed(const IntersectionContext& Context, Vector3 Position)
    {
        if (OnKnownBranch(Context, Position)) { Context.Outcome.Statistics.SeedsSkipped += 1; return; }
        const PairProbe Probe = ProbePair(Context, Position);
        if (Probe.Sine < Context.Settings.SingularSine)
        {
            Vector3 Special = Position;
            if (RefineSpecialPoint(Context, Special)) RegisterSpecialPoint(Context, Special, SpecialPointCategory::Touch);
            return;
        }
        const IntersectionChartPoint Seed = ChartPoint(Context, Position, BoxCentre(Context.FirstBox), BoxCentre(Context.SecondBox));
        if (!InsideBoxes(Context, Seed)) return;
        Context.Outcome.Statistics.SeedsTraced += 1;
        IntersectionBranch Branch;
        Branch.Category = BranchCategory::Traced;
        const HalfTrace Forward = TraceHalf(Context, Seed, 1.0);
        if (Forward.Termination == BranchTermination::Closed)
        {
            Branch.Chart            = Forward.Chart;
            Branch.ClosedCondition  = true;
            Branch.StartTermination = Branch.EndTermination = BranchTermination::Closed;
        }
        else
        {
            const HalfTrace Backward = TraceHalf(Context, Seed, -1.0);
            Branch.Chart.assign(Backward.Chart.rbegin(), Backward.Chart.rend());
            Branch.Chart.insert(Branch.Chart.end(), Forward.Chart.begin() + 1, Forward.Chart.end());
            Branch.StartTermination = Backward.Termination;
            Branch.EndTermination   = Forward.Termination;
        }
        if (Branch.Chart.size() >= 2) Context.Outcome.Branches.push_back(std::move(Branch));
    }

    // 📝 Start points from exact curve–surface intersection: every iso-curve of the first surface's lattice, and the
    //    boundary iso-curves of the second box, against the other (untrimmed) surface, filtered to both boxes.
    [[nodiscard]] std::vector<Vector3> CollectLatticeSeeds(const IntersectionContext& Context)
    {
        std::vector<Vector3> Seeds;
        const auto Harvest = [&](const SurfaceGeometry& CurveSurface, const ParameterBox& CurveBox, const SurfaceGeometry& Target, IsoDirection Direction, double Value)
        {
            const IsoParameterCurve Iso = ExtractIsoParameterCurve(CurveSurface, Direction, Value);
            if (Iso.DegenerateCondition) return;
            const double RunLow  = Direction == IsoDirection::FixedU ? CurveBox.Low.Y : CurveBox.Low.X;
            const double RunHigh = Direction == IsoDirection::FixedU ? CurveBox.High.Y : CurveBox.High.X;
            Context.Outcome.Statistics.LatticeCurves += 1;
            const CurveSurfaceOutcome Hits = IntersectCurveSurface(Iso.Curve, Iso.Offset + Iso.Scale * RunLow, Iso.Offset + Iso.Scale * RunHigh, Target, { Context.Settings.Precision, 1.0e-13 });
            for (const CurveSurfaceCrossing& Hit : Hits.Crossings)
            {
                Vector3 Position = Hit.Position;
                // 📝 A polyline stand-in (parallel carrier) crosses within its sag of the curve: correct onto both exact
                //    carriers in the plane across the branch, as loop seeds are, before the foot gate sees it.
                if (Iso.ApproximateCondition)
                {
                    const PairProbe Probe = ProbePair(Context, Position);
                    if (Probe.Sine < Context.Settings.SingularSine || !CorrectOntoBoth(Context, Position, Normalize(Probe.Cross), Position)) continue;
                }
                if (!InsideBoth(Context, Position)) continue;
                Context.Outcome.Statistics.LatticeCrossings += 1;
                Seeds.push_back(Position);
            }
        };
        const int               Divisions   = std::max(1, Context.Settings.LatticeDivisions);
        const SurfacePeriodicity Periodicity = ResolvePeriodicity(Context.First);
        const ParameterBox&     Box         = Context.FirstBox;
        const bool FullU = Periodicity.PeriodU > 0.0 && Box.High.X - Box.Low.X >= Periodicity.PeriodU - 1.0e-12;
        const bool FullV = Periodicity.PeriodV > 0.0 && Box.High.Y - Box.Low.Y >= Periodicity.PeriodV - 1.0e-12;
        for (int Line = 0; Line <= Divisions; ++Line)
        {
            if (!(FullU && Line == Divisions)) Harvest(Context.First, Box, Context.Second, IsoDirection::FixedU, Box.Low.X + (Box.High.X - Box.Low.X) * Line / Divisions);
            if (!(FullV && Line == Divisions)) Harvest(Context.First, Box, Context.Second, IsoDirection::FixedV, Box.Low.Y + (Box.High.Y - Box.Low.Y) * Line / Divisions);
        }
        const SurfacePeriodicity OtherPeriodicity = ResolvePeriodicity(Context.Second);
        const ParameterBox&      OtherBox         = Context.SecondBox;
        // ⚠️ A first surface with a missing lattice direction (rolling-ball 𝑣-iso-curves have no exact carrier) cannot see a
        //    loop that wraps that direction — a pipe's cross-section in a plane crosses no 𝑢-circle and no box edge, and
        //    its normals are never parallel to the plane's, so normal cones miss it too. The second surface's interior
        //    lattice then seeds as well. (A pole's point iso-curve is not such a gap: sphere and cone keep their lattice.)
        if (std::holds_alternative<RollingBallSurface>(Context.First))
        {
            for (int Line = 1; Line < Divisions; ++Line)
            {
                Harvest(Context.Second, OtherBox, Context.First, IsoDirection::FixedU, OtherBox.Low.X + (OtherBox.High.X - OtherBox.Low.X) * Line / Divisions);
                Harvest(Context.Second, OtherBox, Context.First, IsoDirection::FixedV, OtherBox.Low.Y + (OtherBox.High.Y - OtherBox.Low.Y) * Line / Divisions);
            }
        }
        if (!(OtherPeriodicity.PeriodU > 0.0 && OtherBox.High.X - OtherBox.Low.X >= OtherPeriodicity.PeriodU - 1.0e-12))
        {
            Harvest(Context.Second, OtherBox, Context.First, IsoDirection::FixedU, OtherBox.Low.X);
            Harvest(Context.Second, OtherBox, Context.First, IsoDirection::FixedU, OtherBox.High.X);
        }
        if (!(OtherPeriodicity.PeriodV > 0.0 && OtherBox.High.Y - OtherBox.Low.Y >= OtherPeriodicity.PeriodV - 1.0e-12))
        {
            Harvest(Context.Second, OtherBox, Context.First, IsoDirection::FixedV, OtherBox.Low.Y);
            Harvest(Context.Second, OtherBox, Context.First, IsoDirection::FixedV, OtherBox.High.Y);
        }
        return Seeds;
    }

    // ---- Normal-cone loop detection ------------------------------------------------------------------------------------

    // 📝 A patch summarised by a bounding sphere and a cone of normals (axis + half-angle), both from a 5×5 sample grid
    //    widened by the spread between neighbouring samples (the sampling's own resolution).
    struct PatchCell
    {
        Vector2 Low     = {};                                                                // [-]   - Parameter corner
        Vector2 High    = {};                                                                // [-]   - Parameter corner
        Vector3 Centre  = {};                                                                // [len] - Bounding-sphere centre
        double  Radius  = 0.0;                                                               // [len] - Bounding-sphere radius
        Vector3 BoxLow  = { INFINITY, INFINITY, INFINITY };                                  // [len] - Conservative 3D box low
        Vector3 BoxHigh = { -INFINITY, -INFINITY, -INFINITY };                               // [len] - Conservative 3D box high
        Vector3 Axis    = {};                                                                // [-]   - Normal-cone axis
        double  Spread  = 0.0;                                                               // [rad] - Normal-cone half-angle
    };

    [[nodiscard]] PatchCell DescribeCell(const SurfaceGeometry& Surface, Vector2 Low, Vector2 High)
    {
        constexpr int Count = 4;
        PatchCell     Cell{ Low, High };
        Vector3       Positions[Count + 1][Count + 1], Normals[Count + 1][Count + 1];
        Vector3       NormalSum{};
        for (int Row = 0; Row <= Count; ++Row)
        {
            for (int Column = 0; Column <= Count; ++Column)
            {
                const Vector2 Parameter{ Low.X + (High.X - Low.X) * Column / Count, Low.Y + (High.Y - Low.Y) * Row / Count };
                Positions[Row][Column] = EvaluateSurface(Surface, Parameter).Position;
                Normals[Row][Column]   = EvaluateSurfaceNormal(Surface, Parameter);
                Cell.Centre  = Cell.Centre + Positions[Row][Column] * (1.0 / ((Count + 1) * (Count + 1)));
                Cell.BoxLow  = { std::min(Cell.BoxLow.X, Positions[Row][Column].X), std::min(Cell.BoxLow.Y, Positions[Row][Column].Y), std::min(Cell.BoxLow.Z, Positions[Row][Column].Z) };
                Cell.BoxHigh = { std::max(Cell.BoxHigh.X, Positions[Row][Column].X), std::max(Cell.BoxHigh.Y, Positions[Row][Column].Y), std::max(Cell.BoxHigh.Z, Positions[Row][Column].Z) };
                NormalSum    = NormalSum + Normals[Row][Column];
            }
        }
        Cell.Axis = Normalize(NormalSum);
        double Gap = 0.0, Turn = 0.0;                                                        // [len], [rad] - Neighbour spreads
        for (int Row = 0; Row <= Count; ++Row)
        {
            for (int Column = 0; Column <= Count; ++Column)
            {
                Cell.Radius = std::max(Cell.Radius, Distance(Positions[Row][Column], Cell.Centre));
                Cell.Spread = std::max(Cell.Spread, std::acos(std::clamp(Dot(Normals[Row][Column], Cell.Axis), -1.0, 1.0)));
                if (Column < Count)
                {
                    Gap  = std::max(Gap, Distance(Positions[Row][Column], Positions[Row][Column + 1]));
                    Turn = std::max(Turn, std::acos(std::clamp(Dot(Normals[Row][Column], Normals[Row][Column + 1]), -1.0, 1.0)));
                }
                if (Row < Count)
                {
                    Gap  = std::max(Gap, Distance(Positions[Row][Column], Positions[Row + 1][Column]));
                    Turn = std::max(Turn, std::acos(std::clamp(Dot(Normals[Row][Column], Normals[Row + 1][Column]), -1.0, 1.0)));
                }
            }
        }
        if (Length(NormalSum) < 1.0e-9) Cell.Spread = std::numbers::pi;
        const double Pad = 0.5 * Gap;
        Cell.Radius  += Pad;
        Cell.BoxLow   = Cell.BoxLow - Vector3{ Pad, Pad, Pad };
        Cell.BoxHigh  = Cell.BoxHigh + Vector3{ Pad, Pad, Pad };
        Cell.Spread  += Turn;
        return Cell;
    }

    [[nodiscard]] bool ConesOverlap(const PatchCell& A, const PatchCell& B)
    {
        // 📝 Sederberg–Meyers: a closed loop inside A ∩ B needs a point pair with N_A ∥ N_B (either sense).
        const double Angle = std::acos(std::clamp(Dot(A.Axis, B.Axis), -1.0, 1.0));          // [rad] - Axis separation
        const double Limit = A.Spread + B.Spread;                                            // [rad] - Cone reach
        return Angle <= Limit || std::numbers::pi - Angle <= Limit;
    }

    struct PatchNode
    {
        PatchCell                                  Cell     = {};
        std::unique_ptr<std::array<PatchNode, 4>>  Children = nullptr;
    };

    std::array<PatchNode, 4>& EnsureChildren(const SurfaceGeometry& Surface, PatchNode& Node)
    {
        if (!Node.Children)
        {
            const Vector2 Middle = (Node.Cell.Low + Node.Cell.High) * 0.5;
            Node.Children = std::make_unique<std::array<PatchNode, 4>>(std::array<PatchNode, 4>{
                PatchNode{ DescribeCell(Surface, Node.Cell.Low, Middle), nullptr },
                PatchNode{ DescribeCell(Surface, { Middle.X, Node.Cell.Low.Y }, { Node.Cell.High.X, Middle.Y }), nullptr },
                PatchNode{ DescribeCell(Surface, { Node.Cell.Low.X, Middle.Y }, { Middle.X, Node.Cell.High.Y }), nullptr },
                PatchNode{ DescribeCell(Surface, Middle, Node.Cell.High), nullptr }
            });
        }
        return *Node.Children;
    }

    void SearchLoops(const IntersectionContext& Context, PatchNode& NodeA, PatchNode& NodeB, int Depth, std::vector<Vector3>& Seeds, int& Budget)
    {
        if (Budget <= 0) return;
        const PatchCell& A = NodeA.Cell;
        const PatchCell& B = NodeB.Cell;
        const double Slack = Context.Settings.Precision;
        if (A.BoxHigh.X + Slack < B.BoxLow.X || B.BoxHigh.X + Slack < A.BoxLow.X ||
            A.BoxHigh.Y + Slack < B.BoxLow.Y || B.BoxHigh.Y + Slack < A.BoxLow.Y ||
            A.BoxHigh.Z + Slack < B.BoxLow.Z || B.BoxHigh.Z + Slack < A.BoxLow.Z) return;
        if (Distance(A.Centre, B.Centre) > A.Radius + B.Radius + Slack) return;
        if (!ConesOverlap(A, B)) return;
        if (Depth >= Context.Settings.LoopDepth)
        {
            Budget -= 1;
            Context.Outcome.Statistics.LoopCellPairs += 1;
            // 📝 Alternating projection between the surfaces from A's centre (seeded by both cell parameter centres so
            //    NURBS / parallel / rolling-ball carriers use fast local Newton instead of global lattice scans), then the
            //    corrector (or the special-point solver where the normals are parallel).
            Vector2 HintA    = (A.Low + A.High) * 0.5;
            Vector2 HintB    = (B.Low + B.High) * 0.5;
            Vector3 Position = EvaluateSurface(Context.First, HintA).Position;
            for (int Iteration = 0; Iteration < 60; ++Iteration)
            {
                const Vector3 OnSecond = MeasureSurfaceDistanceNear(Context.Second, Position, HintB).Foot;
                const Vector3 OnFirst  = MeasureSurfaceDistanceNear(Context.First, OnSecond, HintA).Foot;
                const double  Moved    = Distance(OnFirst, Position);
                Position = OnFirst;
                if (Moved <= 1.0e-13 * (1.0 + Context.Scale)) break;
                if (Iteration >= 3 && Distance(OnFirst, OnSecond) <= Context.Settings.ChordTolerance && OnKnownBranch(Context, Position)) return;
                if (Iteration >= 8 && Distance(Position, A.Centre) > 4.0 * (A.Radius + B.Radius)) return;
            }
            const PairProbe Probe = ProbePairNear(Context, Position, HintA, HintB);
            if (std::fabs(Probe.Second.Distance) > 2.0 * (A.Radius + B.Radius)) return;
            if (Probe.Sine < Context.Settings.SingularSine)
            {
                Vector3 Special = Position;
                if (RefineSpecialPoint(Context, Special, HintA, HintB) && Distance(Special, A.Centre) <= 2.0 * (A.Radius + B.Radius)) RegisterSpecialPoint(Context, Special, SpecialPointCategory::Touch);
                return;
            }
            if (CorrectOntoBoth(Context, Position, Normalize(Probe.Cross), Position, HintA, HintB) && Distance(Position, A.Centre) <= 2.0 * (A.Radius + B.Radius))
            {
                Context.Outcome.Statistics.LoopSeeds += 1;
                Seeds.push_back(Position);
            }
            return;
        }
        auto& ChildrenA = EnsureChildren(Context.First, NodeA);
        auto& ChildrenB = EnsureChildren(Context.Second, NodeB);
        for (PatchNode& SubA : ChildrenA)
        {
            for (PatchNode& SubB : ChildrenB) SearchLoops(Context, SubA, SubB, Depth + 1, Seeds, Budget);
        }
    }

    [[nodiscard]] std::vector<Vector3> CollectLoopSeeds(const IntersectionContext& Context)
    {
        std::vector<Vector3> Seeds;
        const int Divisions = std::max(1, Context.Settings.LatticeDivisions);
        std::vector<PatchNode> FirstCells, SecondCells;
        for (const auto& [Surface, Box, Cells] : { std::tuple{ &Context.First, &Context.FirstBox, &FirstCells }, std::tuple{ &Context.Second, &Context.SecondBox, &SecondCells } })
        {
            for (int Row = 0; Row < Divisions; ++Row)
            {
                for (int Column = 0; Column < Divisions; ++Column)
                {
                    const Vector2 Low{ Box->Low.X + (Box->High.X - Box->Low.X) * Column / Divisions, Box->Low.Y + (Box->High.Y - Box->Low.Y) * Row / Divisions };
                    const Vector2 High{ Box->Low.X + (Box->High.X - Box->Low.X) * (Column + 1) / Divisions, Box->Low.Y + (Box->High.Y - Box->Low.Y) * (Row + 1) / Divisions };
                    Cells->push_back({ DescribeCell(*Surface, Low, High), nullptr });
                }
            }
        }
        int Budget = 4096;                                                                   // [-] - Leaf pairs examined at most
        for (PatchNode& A : FirstCells)
        {
            for (PatchNode& B : SecondCells) SearchLoops(Context, A, B, 0, Seeds, Budget);
        }
        return Seeds;
    }

    // 📝 A marching step can pass straight over a crossing point (Steinmetz, Viviani's double point) where |N₁ × N₂|
    //    only dips. Every local minimum of |N₁ × N₂| along a traced chart is offered to the special-point solver, which
    //    converges only where the normals really are parallel; found points are inserted exactly and the branch is
    //    split there, so branch ends coincide with true singular points (future vertices).
    // 📝 The singular point replaced a marched point, so its two segments were never chord-checked: bisect them with the
    //    corrector (plane ⟂ chord through the midpoint) until the bulge is within the chord tolerance.
    void RefineSingularChord(const IntersectionContext& Context, std::vector<IntersectionChartPoint>& Chart, size_t Segment, int Depth)
    {
        if (Depth > 10 || Segment + 1 >= Chart.size()) return;
        const IntersectionChartPoint& Start = Chart[Segment];
        const IntersectionChartPoint& End   = Chart[Segment + 1];
        const Vector3 Chord    = End.Position - Start.Position;
        const Vector3 Midpoint = (Start.Position + End.Position) * 0.5;
        if (Length(Chord) <= 1.0e-9 * (1.0 + Context.Scale)) return;
        Vector3       OnCurve   = Midpoint;
        const Vector2 MidFirst  = (Start.First + End.First) * 0.5;
        const Vector2 MidSecond = (Start.Second + End.Second) * 0.5;
        if (!CorrectOntoBoth(Context, OnCurve, Normalize(Chord), Midpoint, MidFirst, MidSecond)) return;
        if (Distance(OnCurve, Midpoint) <= 0.5 * Context.Settings.ChordTolerance) return;
        const IntersectionChartPoint Inserted = ChartPointNear(Context, OnCurve, MidFirst, MidSecond);
        Chart.insert(Chart.begin() + static_cast<std::ptrdiff_t>(Segment) + 1, Inserted);
        RefineSingularChord(Context, Chart, Segment + 1, Depth + 1);
        RefineSingularChord(Context, Chart, Segment, Depth + 1);
    }

    void SplitAtSingularPoints(const IntersectionContext& Context)
    {
        std::vector<IntersectionBranch> Result;
        for (IntersectionBranch& Branch : Context.Outcome.Branches)
        {
            if (Branch.Category != BranchCategory::Traced || Branch.Chart.size() < 3) { Result.push_back(std::move(Branch)); continue; }
            const size_t Count = Branch.Chart.size();
            std::vector<double> Sines(Count);
            for (size_t Index = 0; Index < Count; ++Index)
            {
                Vector2 HintA = Branch.Chart[Index].First, HintB = Branch.Chart[Index].Second;
                Sines[Index]  = ProbePairNear(Context, Branch.Chart[Index].Position, HintA, HintB).Sine;
            }
            std::vector<size_t> Splits;                                                      // [idx] - Chart indices of singular points
            for (size_t Index = 0; Index < Count; ++Index)
            {
                const bool Interior = Branch.ClosedCondition || (Index > 0 && Index + 1 < Count);
                if (!Interior) continue;
                const double Before = Sines[(Index + Count - 1) % Count], After = Sines[(Index + 1) % Count];
                if (Sines[Index] > Before || Sines[Index] > After || Sines[Index] > 0.5) continue;
                Vector3 Special = Branch.Chart[Index].Position;
                if (!RefineSpecialPoint(Context, Special, Branch.Chart[Index].First, Branch.Chart[Index].Second)) continue;
                const double Reach = std::max(Distance(Branch.Chart[(Index + 1) % Count].Position, Branch.Chart[Index].Position),
                                              Distance(Branch.Chart[(Index + Count - 1) % Count].Position, Branch.Chart[Index].Position));
                if (Distance(Special, Branch.Chart[Index].Position) > Reach) continue;
                Branch.Chart[Index] = ChartPointNear(Context, Special, Branch.Chart[Index].First, Branch.Chart[Index].Second);
                RegisterSpecialPoint(Context, Special, SpecialPointCategory::Singular);
                if (Splits.empty() || Splits.back() != Index) Splits.push_back(Index);
            }
            if (Splits.empty()) { Result.push_back(std::move(Branch)); continue; }
            const auto Piece = [&](size_t From, size_t To, BranchTermination Start, BranchTermination End)
            {
                IntersectionBranch Part;
                Part.Category = BranchCategory::Traced;
                for (size_t Index = From;; Index = (Index + 1) % Count)
                {
                    Part.Chart.push_back(Branch.Chart[Index]);
                    if (Index == To && Part.Chart.size() > 1) break;
                }
                Part.StartTermination = Start;
                Part.EndTermination   = End;
                if (Part.Chart.size() < 2) return;
                if (End == BranchTermination::Singular) RefineSingularChord(Context, Part.Chart, Part.Chart.size() - 2, 0);
                if (Start == BranchTermination::Singular) RefineSingularChord(Context, Part.Chart, 0, 0);
                Result.push_back(std::move(Part));
            };
            if (Branch.ClosedCondition)
            {
                for (size_t Split = 0; Split < Splits.size(); ++Split) Piece(Splits[Split], Splits[(Split + 1) % Splits.size()], BranchTermination::Singular, BranchTermination::Singular);
            }
            else
            {
                Piece(0, Splits.front(), Branch.StartTermination, BranchTermination::Singular);
                for (size_t Split = 0; Split + 1 < Splits.size(); ++Split) Piece(Splits[Split], Splits[Split + 1], BranchTermination::Singular, BranchTermination::Singular);
                Piece(Splits.back(), Count - 1, BranchTermination::Singular, Branch.EndTermination);
            }
        }
        Context.Outcome.Branches = std::move(Result);
    }

    void ClassifySpecialPoints(SurfaceIntersectionOutcome& Outcome, double Scale)
    {
        for (IntersectionSpecialPoint& Point : Outcome.SpecialPoints)
        {
            bool EndCondition = false;                                                       // [-] - A branch ends / passes here
            for (const IntersectionBranch& Branch : Outcome.Branches)
            {
                // Exact branches are measured on their carrier: the chart is only a chord-tolerance polyline, which can
                // miss a crossing by more than the acceptance when no chart point falls on it (oblique Monge ellipses).
                const double Gap = Branch.Category == BranchCategory::Traced
                                 ? PolylineDistance(Branch.Chart, Branch.ClosedCondition, Point.Position)
                                 : Distance(EvaluateCurve(Branch.Curve, ProjectOntoCurve(Branch.Curve, Point.Position, Branch.CurveLow)).Position, Point.Position);
                if (Gap <= 1.0e-6 * (1.0 + Scale)) EndCondition = true;
            }
            Point.Category = EndCondition ? SpecialPointCategory::Singular : SpecialPointCategory::Touch;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InsideParameterBox(const SurfaceGeometry& Surface, const ParameterBox& Box, Vector2 Parameter, double Slack) noexcept
{
    const SurfacePeriodicity Periodicity = ResolvePeriodicity(Surface);
    const auto Within = [&](double Value, double Low, double High, double Period)
    {
        if (Period > 0.0)
        {
            if (High - Low >= Period - 1.0e-12) return true;
            const double Folded = Value - Period * std::floor((Value - (Low - Slack)) / Period);   // [-] - In [Low − slack, Low − slack + P)
            return Folded <= High + Slack;
        }
        return Value >= Low - Slack && Value <= High + Slack;
    };
    return Within(Parameter.X, Box.Low.X, Box.High.X, Periodicity.PeriodU) && Within(Parameter.Y, Box.Low.Y, Box.High.Y, Periodicity.PeriodV);
}

SurfaceIntersectionOutcome IntersectSurfaces(const SurfaceGeometry& First, const ParameterBox& FirstBox, const SurfaceGeometry& Second, const ParameterBox& SecondBox, const SurfaceIntersectionSettings& Settings)
{
    SurfaceIntersectionOutcome Outcome;
    double Scale = 0.0;                                                                      // [len] - Box extent
    for (const auto& [Surface, Box] : { std::pair{ &First, &FirstBox }, std::pair{ &Second, &SecondBox } })
    {
        const std::vector<Vector3> Samples = SampleBox(*Surface, *Box, 4);
        for (const Vector3& Sample : Samples) Scale = std::max(Scale, Distance(Sample, Samples.front()));
    }
    const IntersectionContext Context{ First, FirstBox, Second, SecondBox, Settings, Scale, Outcome };

    if (Settings.Policy == IntersectionPolicy::Hybrid && (SolvePlanePlane(Context) || SolveCoaxial(Context) || SolveParallelCrossSection(Context) || SolvePlaneThroughAxis(Context) || SolveEqualCylinders(Context)))
    {
        ClassifySpecialPoints(Outcome, Scale);
        return Outcome;
    }

    const char* Prefix = Settings.Policy == IntersectionPolicy::MarchingOnly ? "Marching" : "Hybrid";   // [-] - Which policy reached marching
    Outcome.Method = std::string(Prefix) + (Settings.LoopDetectionEnabled ? " · lattice + normal-cone loops + marching" : " · lattice + marching (loop detection off)");
    for (const Vector3& Seed : CollectLatticeSeeds(Context)) TraceFromSeed(Context, Seed);
    if (Settings.LoopDetectionEnabled)
    {
        for (const Vector3& Seed : CollectLoopSeeds(Context)) TraceFromSeed(Context, Seed);
    }
    SplitAtSingularPoints(Context);
    ClassifySpecialPoints(Outcome, Scale);
    return Outcome;
}

const char* BranchTerminationLabel(BranchTermination Termination) noexcept
{
    constexpr const char* Labels[] = { "Closed", "Boundary", "Singular", "Limit" };
    return Labels[static_cast<int>(Termination)];
}

} // namespace Frontier::GeometrySpace::Parametric
