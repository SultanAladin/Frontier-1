/*====================================================================================================================================
                                                        GENERALFUSE.CPP
====================================================================================================================================*/
// 🧩 General Fuse Booleans: interferences → pave blocks → split faces → GWN classification → selection → assembly

#include "Engine/GeometrySpace/Parametric/Boolean/GeneralFuse.h"

#include "Engine/GeometrySpace/Parametric/Diagnostics/OperationTrace.h"

#include "Engine/GeometrySpace/Parametric/Audit/EnclosedVolume.h"
#include "Engine/GeometrySpace/Parametric/Audit/ModelAudit.h"
#include "Engine/GeometrySpace/Parametric/Construction/BodyArrangement.h"
#include "Engine/GeometrySpace/Parametric/Intersection/CurveSurfaceIntersection.h"
#include "Engine/GeometrySpace/Parametric/Intersection/IntersectionCurve.h"
#include "Engine/GeometrySpace/Parametric/Local/RedundantConnectivity.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <set>

namespace Frontier::GeometrySpace::Parametric
{

namespace
{
    constexpr double IntegralTolerance = 1.0e-3;                                     // [-]   - Max |w − round(w)| for a fragment probe
    constexpr double FullTurn          = 2.0 * std::numbers::pi;                     // [rad] - 2π
    constexpr double JunctionGap       = 1.0e-7;                                     // [-]   - Chart distance of one junction

    using Clock = std::chrono::steady_clock;

    [[nodiscard]] double Elapsed(Clock::time_point Start) { return std::chrono::duration<double, std::milli>(Clock::now() - Start).count(); }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                BOUNDS
    //--------------------------------------------------------------------------------------------------------------------

    struct SpatialBounds
    {
        Vector3 Low  = {  std::numeric_limits<double>::max(),  std::numeric_limits<double>::max(),  std::numeric_limits<double>::max() };
        Vector3 High = { -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max() };

        void Include(Vector3 Point)
        {
            Low  = { std::min(Low.X, Point.X), std::min(Low.Y, Point.Y), std::min(Low.Z, Point.Z) };
            High = { std::max(High.X, Point.X), std::max(High.Y, Point.Y), std::max(High.Z, Point.Z) };
        }
        void Inflate(double Margin)
        {
            Low  = Low - Vector3{ Margin, Margin, Margin };
            High = High + Vector3{ Margin, Margin, Margin };
        }
        [[nodiscard]] bool Overlaps(const SpatialBounds& Other) const
        {
            return Low.X <= Other.High.X && Other.Low.X <= High.X && Low.Y <= Other.High.Y && Other.Low.Y <= High.Y && Low.Z <= Other.High.Z && Other.Low.Z <= High.Z;
        }
        [[nodiscard]] bool Contains(Vector3 Point) const
        {
            return Point.X >= Low.X && Point.X <= High.X && Point.Y >= Low.Y && Point.Y <= High.Y && Point.Z >= Low.Z && Point.Z <= High.Z;
        }
    };

    [[nodiscard]] SpatialBounds BoundCurve(const CurveGeometry& Curve, double Low, double High, double Margin)
    {
        SpatialBounds Bounds;
        const bool Straight = std::holds_alternative<StraightLine>(Curve);                   // [-] - Two samples suffice
        const int  Samples  = Straight ? 1 : 32;                                             // [-] - Pieces
        for (int Index = 0; Index <= Samples; ++Index) Bounds.Include(EvaluateCurve(Curve, Low + (High - Low) * Index / Samples).Position);
        const double Bulge = MeasureCurveBulge(Curve, Low, High, Samples);
        Bounds.Inflate(Bulge + Margin);
        return Bounds;
    }

    [[nodiscard]] double ParameterScale(const CurveGeometry& Curve) noexcept
    {
        return MeasureParameterScale(Curve);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                CURVE–CURVE
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Closed-form intersection points of two analytic carriers (coincident overlaps return nothing: their ends are
    //    found as vertex-on-edge paves).
    // Distance from a point to a carrier (closest point by exact inversion).
    [[nodiscard]] double MeasureCarrierDistance(const CurveGeometry& Curve, Vector3 Point)
    {
        return Distance(Point, EvaluateCurve(Curve, ProjectOntoCurve(Curve, Point, 0.0)).Position);
    }

    // 📝 Pairs with an ellipse: a carrier crossing the conic's plane gives at most two candidates (trigonometric equation
    //    K + A·cos τ + B·sin τ = 0, or the line's piercing point), accepted on the other carrier by exact inversion.
    //    Coplanar pairs: local minima of the distance over 720 samples of the first carrier, polished by golden section.
    [[nodiscard]] std::vector<Vector3> IntersectEllipticCarriers(const CurveGeometry& First, const CurveGeometry& Second, double Precision)
    {
        std::vector<Vector3> Points;
        const auto Accept = [&](Vector3 Point)
        {
            if (MeasureCarrierDistance(First, Point) > Precision || MeasureCarrierDistance(Second, Point) > Precision) return;
            for (const Vector3& Known : Points) if (Distance(Known, Point) <= Precision) return;
            Points.push_back(Point);
        };
        const std::optional<ConicForm> ConicA = ResolveConicForm(First);
        const std::optional<ConicForm> ConicB = ResolveConicForm(Second);
        const CurveGeometry& Planar = ConicB ? Second : First;                               // [-] - A conic whose plane is used
        const CurveGeometry& Other  = ConicB ? First : Second;                               // [-] - The carrier cut by that plane
        const ConicForm      Plane  = ConicB ? *ConicB : *ConicA;
        bool Coplanar = false;
        if (const auto* Line = std::get_if<StraightLine>(&Other))
        {
            const double Climb = Dot(Line->Direction, Plane.Normal);                         // [-] - d·N
            const double Height = Dot(Line->Origin - Plane.Centre, Plane.Normal);            // [len] - (o − c)·N
            if (std::fabs(Climb) > 1.0e-12) Accept(Line->Origin + Line->Direction * (-Height / Climb));
            else Coplanar = std::fabs(Height) <= Precision;
        }
        else
        {
            const ConicForm Cut = *ResolveConicForm(Other);
            const double Constant = Dot(Cut.Centre - Plane.Centre, Plane.Normal);            // [len] - K
            const double CosTerm  = Dot(Cut.CosineAxis, Plane.Normal);                       // [len] - A
            const double SinTerm  = Dot(Cut.SineAxis, Plane.Normal);                         // [len] - B
            const double Amplitude = std::hypot(CosTerm, SinTerm);                           // [len] - √(A² + B²)
            if (Amplitude <= 1.0e-12 * MeasureParameterScale(Other)) Coplanar = std::fabs(Constant) <= Precision;
            else if (std::fabs(Constant) <= Amplitude * (1.0 + 1.0e-12))
            {
                const double Phase  = std::atan2(SinTerm, CosTerm);                          // [rad] - φ
                const double Spread = std::acos(std::clamp(-Constant / Amplitude, -1.0, 1.0)); // [rad] - ± offset
                for (double τ : { Phase + Spread, Phase - Spread }) Accept(EvaluateCurve(Other, τ).Position);
            }
        }
        if (!Coplanar) return Points;
        constexpr int SampleCount = 720;                                                     // [-] - Samples of the first carrier
        const bool    Straight    = std::holds_alternative<StraightLine>(First);
        const double  Reach       = 4.0 * (MeasureParameterScale(Planar) + Distance(Plane.Centre, EvaluateCurve(First, 0.0).Position));
        const double  Low         = Straight ? -Reach : 0.0;                                 // [-] - τ window of the first carrier
        const double  High        = Straight ? Reach : 2.0 * std::numbers::pi;
        const auto    Gap         = [&](double τ) { return MeasureCarrierDistance(Second, EvaluateCurve(First, τ).Position); };
        std::vector<double> Values(SampleCount + 1);
        for (int Index = 0; Index <= SampleCount; ++Index) Values[Index] = Gap(Low + (High - Low) * Index / SampleCount);
        for (int Index = 0; Index <= SampleCount; ++Index)
        {
            const double Before = Values[Index == 0 ? (Straight ? 0 : SampleCount - 1) : Index - 1];
            const double After  = Values[Index == SampleCount ? (Straight ? SampleCount : 1) : Index + 1];
            if (Values[Index] > Before || Values[Index] > After) continue;
            double Left = Low + (High - Low) * (Index - 1) / SampleCount;                    // [-] - Bracket
            double Right = Low + (High - Low) * (Index + 1) / SampleCount;                   // [-] - Bracket
            constexpr double Golden = 0.6180339887498949;                                    // [-] - (√5 − 1)/2
            for (int Iteration = 0; Iteration < 80; ++Iteration)
            {
                const double Inner = Right - Golden * (Right - Left);                        // [-] - Lower probe
                const double Outer = Left + Golden * (Right - Left);                         // [-] - Upper probe
                if (Gap(Inner) < Gap(Outer)) Right = Outer;
                else                         Left  = Inner;
            }
            Accept(EvaluateCurve(First, 0.5 * (Left + Right)).Position);
        }
        return Points;
    }

    [[nodiscard]] std::vector<Vector3> IntersectCarriers(const CurveGeometry& First, const CurveGeometry& Second, double Precision)
    {
        std::vector<Vector3> Points;
        // 📝 Intersection curves cross other carriers only where they cross a face of the other argument, which the
        //    edge–face pass finds exactly (Newton on the carrier), so the edge–edge pass skips them.
        if (std::holds_alternative<IntersectionCurve>(First) || std::holds_alternative<IntersectionCurve>(Second)) return Points;
        if (std::holds_alternative<ContactCurve>(First) || std::holds_alternative<ContactCurve>(Second)) return Points;   // Same for rails
        if (std::holds_alternative<NurbsCurve>(First) || std::holds_alternative<NurbsCurve>(Second)) return Points;       // Same for NURBS edges
        if (std::holds_alternative<EllipticalArc>(First) || std::holds_alternative<EllipticalArc>(Second))
        {
            return IntersectEllipticCarriers(First, Second, Precision);
        }
        const auto* LineA   = std::get_if<StraightLine>(&First);
        const auto* LineB   = std::get_if<StraightLine>(&Second);
        if (LineA && LineB)
        {
            const Vector3 Offset = LineA->Origin - LineB->Origin;                            // [len] - o₁ − o₂
            const double  Cosine = Dot(LineA->Direction, LineB->Direction);                  // [-]   - d₁·d₂
            const double  Denominator = 1.0 - Cosine * Cosine;                               // [-]   - sin²
            if (Denominator < 1.0e-20) return Points;
            const double  ParameterA = (Cosine * Dot(LineB->Direction, Offset) - Dot(LineA->Direction, Offset)) / Denominator;
            const double  ParameterB = Dot(LineB->Direction, Offset) + ParameterA * Cosine;
            const Vector3 PointA = LineA->Origin + LineA->Direction * ParameterA;
            const Vector3 PointB = LineB->Origin + LineB->Direction * ParameterB;
            if (Distance(PointA, PointB) <= Precision) Points.push_back((PointA + PointB) * 0.5);
            return Points;
        }
        if (LineA || LineB)
        {
            const StraightLine& Line = LineA ? *LineA : *LineB;
            const CircularArc&  Arc  = std::get<CircularArc>(LineA ? Second : First);
            const Vector3 Relative = Line.Origin - Arc.Frame.Origin;                         // [len] - o − c
            const double  Climb    = Dot(Line.Direction, Arc.Frame.ZAxis);                   // [-]   - d·Z
            const double  Height   = Dot(Relative, Arc.Frame.ZAxis);                         // [len] - (o − c)·Z
            if (std::fabs(Climb) > 1.0e-12)
            {
                const Vector3 Point = Line.Origin + Line.Direction * (-Height / Climb);      // [len] - Plane piercing
                if (std::fabs(Distance(Point, Arc.Frame.Origin) - Arc.Radius) <= Precision) Points.push_back(Point);
                return Points;
            }
            if (std::fabs(Height) > Precision) return Points;
            // Coplanar: |o + s d − c|² = r².
            const double Half  = Dot(Line.Direction, Relative);                              // [len]  - b/2
            const double Delta = Half * Half - (Dot(Relative, Relative) - Arc.Radius * Arc.Radius);
            if (Delta < -2.0 * Precision * Arc.Radius) return Points;
            const double Root = std::sqrt(std::max(Delta, 0.0));                             // [len] - √Δ
            Points.push_back(Line.Origin + Line.Direction * (-Half - Root));
            if (Root * Root > 4.0 * Precision * Precision) Points.push_back(Line.Origin + Line.Direction * (-Half + Root));
            return Points;
        }
        const CircularArc& ArcA = std::get<CircularArc>(First);
        const CircularArc& ArcB = std::get<CircularArc>(Second);
        const Vector3 Join = ArcB.Frame.Origin - ArcA.Frame.Origin;                          // [len] - c₂ − c₁
        if (Length(Cross(ArcA.Frame.ZAxis, ArcB.Frame.ZAxis)) < 1.0e-12)
        {
            if (std::fabs(Dot(Join, ArcA.Frame.ZAxis)) > Precision) return Points;
            const double Separation = Length(Join);                                          // [len] - Centre distance
            if (Separation < Precision) return Points;                                       // Concentric (coincident or disjoint)
            const Vector3 Along  = Join * (1.0 / Separation);                                // [-]   - Unit c₁ → c₂
            const Vector3 Across = Cross(ArcA.Frame.ZAxis, Along);                           // [-]   - In-plane normal
            const double  Foot   = (ArcA.Radius * ArcA.Radius - ArcB.Radius * ArcB.Radius + Separation * Separation) / (2.0 * Separation);
            const double  Square = ArcA.Radius * ArcA.Radius - Foot * Foot;                  // [len²] - h²
            if (Square < -2.0 * Precision * ArcA.Radius) return Points;
            const double  Rise   = std::sqrt(std::max(Square, 0.0));                         // [len]  - h
            Points.push_back(ArcA.Frame.Origin + Along * Foot + Across * Rise);
            if (Rise > Precision) Points.push_back(ArcA.Frame.Origin + Along * Foot - Across * Rise);
            return Points;
        }
        // Non-coplanar: points of circle A on the plane of circle B, then on circle B.
        const double Constant = Dot(ArcA.Frame.Origin - ArcB.Frame.Origin, ArcB.Frame.ZAxis); // [len] - K
        const double CosTerm  = ArcA.Radius * Dot(ArcA.Frame.XAxis, ArcB.Frame.ZAxis);         // [len] - A
        const double SinTerm  = ArcA.Radius * Dot(ArcA.Frame.YAxis, ArcB.Frame.ZAxis);         // [len] - B
        const double Amplitude = std::hypot(CosTerm, SinTerm);                                 // [len] - √(A² + B²)
        if (Amplitude < 1.0e-300) return Points;
        const double Ratio = -Constant / Amplitude;                                           // [-]   - cos(τ − φ)
        if (std::fabs(Ratio) > 1.0 + 1.0e-12) return Points;
        const double Phase  = std::atan2(SinTerm, CosTerm);                                   // [rad] - φ
        const double Spread = std::acos(std::clamp(Ratio, -1.0, 1.0));                        // [rad] - ± offset
        for (double τ : { Phase + Spread, Phase - Spread })
        {
            const Vector3 Point = EvaluateCurve(ArcA, τ).Position;
            if (std::fabs(Distance(Point, ArcB.Frame.Origin) - ArcB.Radius) <= Precision
                && std::fabs(Dot(Point - ArcB.Frame.Origin, ArcB.Frame.ZAxis)) <= Precision
                && (Points.empty() || Distance(Points.back(), Point) > Precision))
            {
                Points.push_back(Point);
            }
        }
        return Points;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                BUILDER STATE
    //--------------------------------------------------------------------------------------------------------------------

    struct ArgumentEdge
    {
        int                                 Argument = 0;                                    // [-] - 0 / 1
        EdgeToken                           Token    = {};                                   // [-] - Source edge
        CurveGeometry                       Curve    = StraightLine{};                       // [-] - Carrier
        double                              Low      = 0.0;                                  // [-] - Interval
        double                              High     = 1.0;                                  // [-] - Interval
        int                                 Start    = -1;                                   // [-] - Registry vertex
        int                                 End      = -1;                                   // [-] - Registry vertex
        SpatialBounds                       Bounds   = {};                                   // [-] - Pruning box
        std::vector<std::pair<double, int>> Paves    = {};                                   // [-] - (τ, vertex) interior
        std::vector<int>                    Pieces   = {};                                   // [-] - Pave blocks in τ order
        std::vector<std::pair<double, double>> PieceRanges = {};                             // [-] - τ range per piece (own carrier)
    };

    struct ArgumentFace
    {
        int              Argument = 0;                                                       // [-] - 0 / 1
        FaceToken        Token    = {};                                                      // [-] - Source face
        FaceDomain       Domain   = {};                                                      // [-] - Chart domain
        SpatialBounds    Bounds   = {};                                                      // [-] - Pruning box
        std::vector<int> Sections = {};                                                      // [-] - Imprinted pave blocks
        std::set<int>    Boundary = {};                                                      // [-] - Pave blocks on its loops
    };

    struct SectionCandidate
    {
        CurveGeometry Curve  = StraightLine{};                                               // [-] - Exact carrier
        double        Low    = 0.0;                                                          // [-] - τ range
        double        High   = 1.0;                                                          // [-] - τ range
        bool          Closed = false;                                                        // [-] - Full circle
        int           FaceA  = -1;                                                           // [-] - ArgumentFace index
        int           FaceB  = -1;                                                           // [-] - ArgumentFace index
    };

    // 📝 Directed chart arc used by face finding.
    struct ChartArc
    {
        FuseUse Use       = {};                                                              // [-] - The use it represents
        Vector2 From      = {};                                                              // [-] - Chart start
        Vector2 To        = {};                                                              // [-] - Chart end
        int     JunctionFrom  = -1;                                                              // [-] - Start junction
        int     JunctionTo    = -1;                                                              // [-] - End junction
        int     Twin      = -1;                                                              // [-] - Opposite arc of a section
        bool    Consumed  = false;                                                           // [-] - Already in a cycle
    };

    class FuseBuilder
    {
    public:
        FuseBuilder(const ModelPartition& Partition, BodyToken First, BodyToken Second, const GeneralFuseSettings& Settings)
            : Partition(Partition), Settings(Settings), Arguments{ First, Second }
        {
        }

        GeneralFuseOutcome Run()
        {
            const auto IntersectStart = Clock::now();
            CollectArguments();
            IntersectEdgesWithEdges();
            IntersectEdgesWithFaces();
            if (!IntersectFacesWithFaces()) return std::move(Outcome);
            PlacePaves();
            SplitSections();
            ImprintEdges();
            Outcome.Statistics.IntersectMilliseconds = Elapsed(IntersectStart);

            const auto SplitStart = Clock::now();
            for (size_t Index = 0; Index < Faces.size(); ++Index)
            {
                if (!SplitFace(static_cast<int>(Index))) return std::move(Outcome);
            }
            Outcome.Statistics.SplitMilliseconds = Elapsed(SplitStart);

            const auto ClassifyStart = Clock::now();
            if (!ClassifyFragments()) return std::move(Outcome);
            Outcome.Statistics.ClassifyMilliseconds = Elapsed(ClassifyStart);
            return std::move(Outcome);
        }

    private:
        const ModelPartition&       Partition;
        const GeneralFuseSettings&  Settings;
        BodyToken                   Arguments[2];
        GeneralFuseOutcome          Outcome;
        std::vector<ArgumentEdge>   Edges;
        std::vector<ArgumentFace>   Faces;
        std::vector<SectionCandidate> Sections;
        std::map<uint32_t, int>     EdgeIndexBySlot[2];
        std::map<std::pair<int, int>, std::vector<int>> EdgesByEnds;
        SpatialBounds               ArgumentBounds[2];
        SpatialBounds               OverlapBounds;
        bool                        OverlapCondition = false;
        int                         ArgumentFaceCounts[2] = { 0, 0 };

        //----------------------------------------------------------------------------------------------------------------
        // Registry
        //----------------------------------------------------------------------------------------------------------------

        int RegisterVertex(Vector3 Position)
        {
            for (size_t Index = 0; Index < Outcome.Vertices.size(); ++Index)
            {
                if (Distance(Outcome.Vertices[Index].Position, Position) <= Settings.Precision) return static_cast<int>(Index);
            }
            Outcome.Vertices.push_back({ Position });
            return static_cast<int>(Outcome.Vertices.size()) - 1;
        }

        // 💡 Same ends, another parametrisation (a traced section along a spline edge, or a spline edge along a circle):
        //    the parameter midpoints are different points of one curve, so the block coincides when three interior
        //    points of it lie on the existing block, inside that block's range.
        [[nodiscard]] bool CoincidentBlock(const FuseEdge& Other, const CurveGeometry& Curve, double Low, double High) const
        {
            const double Slack = Settings.Precision / ParameterScale(Other.Curve);           // [-] - Parameter slack
            for (const double Fraction : { 0.25, 0.5, 0.75 })
            {
                const Vector3 Point     = EvaluateCurve(Curve, Low + Fraction * (High - Low)).Position;
                const double  Parameter = ProjectOntoCurve(Other.Curve, Point, Other.Low);
                if (Parameter < Other.Low - Slack || Parameter > Other.High + Slack) return false;
                if (Distance(EvaluateCurve(Other.Curve, Parameter).Position, Point) > 10.0 * Settings.Precision) return false;
            }
            return true;
        }

        // Pave block registration with coincidence merge (same end vertices, same midpoint or same points) → common blocks.
        int RegisterEdge(const CurveGeometry& Curve, double Low, double High, int Start, int End, int Argument, bool Section)
        {
            const Vector3 Middle = EvaluateCurve(Curve, 0.5 * (Low + High)).Position;        // [len] - Block midpoint
            const auto    Key    = std::minmax(Start, End);
            for (int Existing : EdgesByEnds[{ Key.first, Key.second }])
            {
                const FuseEdge& Other = Outcome.Edges[Existing];
                if (Distance(EvaluateCurve(Other.Curve, 0.5 * (Other.Low + Other.High)).Position, Middle) <= 10.0 * Settings.Precision ||
                    CoincidentBlock(Other, Curve, Low, High))
                {
                    if (Argument >= 0 && Other.Argument >= 0 && Other.Argument != Argument) ++Outcome.Statistics.CommonBlocks;
                    return Existing;
                }
            }
            Outcome.Edges.push_back({ Curve, Low, High, Start, End, Argument, Section });
            const int Index = static_cast<int>(Outcome.Edges.size()) - 1;
            EdgesByEnds[{ Key.first, Key.second }].push_back(Index);
            return Index;
        }

        [[nodiscard]] bool LocateOnEdge(const ArgumentEdge& Edge, Vector3 Position, double& Parameter) const
        {
            Parameter = ProjectOntoCurve(Edge.Curve, Position, Edge.Low);
            const double Slack = Settings.Precision / ParameterScale(Edge.Curve);           // [-] - Parameter slack
            if (Parameter < Edge.Low - Slack || Parameter > Edge.High + Slack) return false;
            return Distance(EvaluateCurve(Edge.Curve, Parameter).Position, Position) <= Settings.Precision;
        }

        //----------------------------------------------------------------------------------------------------------------
        // ① Arguments
        //----------------------------------------------------------------------------------------------------------------

        void CollectArguments()
        {
            for (int Argument = 0; Argument < 2; ++Argument)
            {
                const BodyInventory Inventory = Partition.CollectInventory(Arguments[Argument]);
                for (VertexToken Vertex : Inventory.Vertices) RegisterVertex(Partition.Vertices.Resolve(Vertex).Position);
                for (EdgeToken Token : Inventory.Edges)
                {
                    const EdgeProfile& Profile = Partition.Edges.Resolve(Token);
                    ArgumentEdge Edge;
                    Edge.Argument = Argument;
                    Edge.Token    = Token;
                    Edge.Curve    = Partition.Curves.Resolve(Profile.Curve);
                    Edge.Low      = Profile.IntervalLow;
                    Edge.High     = Profile.IntervalHigh;
                    Edge.Start    = RegisterVertex(Partition.Vertices.Resolve(Profile.Start).Position);
                    Edge.End      = RegisterVertex(Partition.Vertices.Resolve(Profile.End).Position);
                    Edge.Bounds   = BoundCurve(Edge.Curve, Edge.Low, Edge.High, 4.0 * Settings.Precision);
                    ArgumentBounds[Argument].Include(Edge.Bounds.Low);
                    ArgumentBounds[Argument].Include(Edge.Bounds.High);
                    EdgeIndexBySlot[Argument][Token.Slot] = static_cast<int>(Edges.size());
                    Edges.push_back(std::move(Edge));
                }
                for (FaceToken Token : Inventory.Faces)
                {
                    ArgumentFace Face;
                    Face.Argument = Argument;
                    Face.Token    = Token;
                    Face.Domain   = DescribeFaceDomain(Partition, Token);
                    Face.Bounds   = { Face.Domain.BoundsLow, Face.Domain.BoundsHigh };
                    Face.Bounds.Inflate(4.0 * Settings.Precision);
                    ArgumentBounds[Argument].Include(Face.Bounds.Low);
                    ArgumentBounds[Argument].Include(Face.Bounds.High);
                    ++ArgumentFaceCounts[Argument];
                    Faces.push_back(std::move(Face));
                }
            }
            OverlapCondition = ArgumentBounds[0].Overlaps(ArgumentBounds[1]);
            if (OverlapCondition)
            {
                OverlapBounds.Low  = { std::max(ArgumentBounds[0].Low.X, ArgumentBounds[1].Low.X),
                                       std::max(ArgumentBounds[0].Low.Y, ArgumentBounds[1].Low.Y),
                                       std::max(ArgumentBounds[0].Low.Z, ArgumentBounds[1].Low.Z) };
                OverlapBounds.High = { std::min(ArgumentBounds[0].High.X, ArgumentBounds[1].High.X),
                                       std::min(ArgumentBounds[0].High.Y, ArgumentBounds[1].High.Y),
                                       std::min(ArgumentBounds[0].High.Z, ArgumentBounds[1].High.Z) };
                OverlapBounds.Inflate(4.0 * Settings.Precision);
            }
        }

        //----------------------------------------------------------------------------------------------------------------
        // ② Interferences
        //----------------------------------------------------------------------------------------------------------------

        void IntersectEdgesWithEdges()
        {
            if (!OverlapCondition) return;
            for (const ArgumentEdge& First : Edges)
            {
                if (First.Argument != 0 || !First.Bounds.Overlaps(OverlapBounds)) continue;
                for (const ArgumentEdge& Second : Edges)
                {
                    if (Second.Argument != 1 || !Second.Bounds.Overlaps(OverlapBounds) || !First.Bounds.Overlaps(Second.Bounds)) continue;
                    ++Outcome.Statistics.EdgeEdgeTests;
                    for (const Vector3& Point : IntersectCarriers(First.Curve, Second.Curve, Settings.Precision))
                    {
                        double ParameterA = 0.0, ParameterB = 0.0;
                        if (LocateOnEdge(First, Point, ParameterA) && LocateOnEdge(Second, Point, ParameterB)) NoteInterference(Point);
                    }
                }
            }
        }

        void IntersectEdgesWithFaces()
        {
            if (!OverlapCondition) return;
            for (const ArgumentEdge& Edge : Edges)
            {
                if (!Edge.Bounds.Overlaps(OverlapBounds)) continue;
                for (const ArgumentFace& Face : Faces)
                {
                    if (Face.Argument == Edge.Argument || !Face.Bounds.Overlaps(OverlapBounds) || !Edge.Bounds.Overlaps(Face.Bounds)) continue;
                    ++Outcome.Statistics.EdgeFaceTests;
                    const CurveSurfaceOutcome Hits = IntersectCurveSurface(Edge.Curve, Edge.Low, Edge.High, Face.Domain.Surface);
                    for (const CurveSurfaceCrossing& Hit : Hits.Crossings)
                    {
                        if (ClassifyFacePosition(Face.Domain, Hit.Position, Settings.Precision).Membership != PointMembership::Outside) NoteInterference(Hit.Position);
                    }
                }
            }
        }

        void NoteInterference(Vector3 Point)
        {
            const size_t Before = Outcome.Vertices.size();
            RegisterVertex(Point);
            Outcome.Statistics.InterferenceVertices += Outcome.Vertices.size() > Before ? 1 : 0;
        }

        [[nodiscard]] ParameterBox WindowBox(const FaceDomain& Domain) const { return { Domain.Window.Low, Domain.Window.High }; }

        bool IntersectFacesWithFaces()
        {
            if (!OverlapCondition)
            {
                Outcome.Statistics.FacePairs += ArgumentFaceCounts[0] * ArgumentFaceCounts[1];
                return true;
            }
            for (size_t IndexA = 0; IndexA < Faces.size(); ++IndexA)
            {
                if (Faces[IndexA].Argument != 0) continue;
                if (!Faces[IndexA].Bounds.Overlaps(OverlapBounds))
                {
                    Outcome.Statistics.FacePairs += ArgumentFaceCounts[1];
                    continue;
                }
                for (size_t IndexB = 0; IndexB < Faces.size(); ++IndexB)
                {
                    if (Faces[IndexB].Argument != 1) continue;
                    ++Outcome.Statistics.FacePairs;
                    const ArgumentFace& FaceA = Faces[IndexA];
                    const ArgumentFace& FaceB = Faces[IndexB];
                    if (!FaceB.Bounds.Overlaps(OverlapBounds) || !FaceA.Bounds.Overlaps(FaceB.Bounds)) continue;
                    ++Outcome.Statistics.FacePairsTested;
                    const SurfaceIntersectionOutcome Section = IntersectSurfaces(FaceA.Domain.Surface, WindowBox(FaceA.Domain), FaceB.Domain.Surface, WindowBox(FaceB.Domain), Settings.Intersection);
                    if (Section.CoincidentCondition) { ++Outcome.Statistics.CoincidentPairs; continue; }
                    // 💡 Singular points (branches cross, N₁ ∥ N₂) become vertices, so every branch through them splits
                    //    there and the face splitter sees the true degree-4 crossing instead of two smooth passes.
                    for (const IntersectionSpecialPoint& Special : Section.SpecialPoints)
                    {
                        if (Special.Category != SpecialPointCategory::Singular) continue;
                        if (ClassifyFacePosition(FaceA.Domain, Special.Position, Settings.Precision).Membership == PointMembership::Outside) continue;
                        if (ClassifyFacePosition(FaceB.Domain, Special.Position, Settings.Precision).Membership == PointMembership::Outside) continue;
                        RegisterVertex(Special.Position);
                        ++Outcome.Statistics.SingularVertices;
                    }
                    for (const IntersectionBranch& Branch : Section.Branches)
                    {
                        if (Branch.Category == BranchCategory::Traced)
                        {
                            // 💡 A marched branch becomes an exact intersection-curve carrier (guide = the chart). Branches
                            //    that never come within reach of both faces are dropped here, as exact ones are by splitting.
                            bool Relevant = false;
                            for (const IntersectionChartPoint& Point : Branch.Chart)
                            {
                                if (ClassifyFacePosition(FaceA.Domain, Point.Position, Settings.Precision).Membership != PointMembership::Outside
                                    && ClassifyFacePosition(FaceB.Domain, Point.Position, Settings.Precision).Membership != PointMembership::Outside) { Relevant = true; break; }
                            }
                            if (!Relevant || Branch.Chart.size() < 2) continue;
                            if (Branch.StartTermination == BranchTermination::Limit || Branch.EndTermination == BranchTermination::Limit)
                            {
                                Outcome.Refusal = std::string("GeneralFuse: marched section between ") + SurfaceCategoryLabel(FaceA.Domain.Surface) + " and "
                                                + SurfaceCategoryLabel(FaceB.Domain.Surface) + " faces ran out of points";
                                return false;
                            }
                            std::vector<Vector3> Guide;
                            for (const IntersectionChartPoint& Point : Branch.Chart) Guide.push_back(Point.Position);
                            const CurveGeometry Carrier = BuildIntersectionCurve(FaceA.Domain.Surface, FaceB.Domain.Surface, Guide, Branch.ClosedCondition);
                            const auto [Low, High] = MeasureIntersectionCurveRange(*std::get<IntersectionCurve>(Carrier).Trace);
                            ++Outcome.Statistics.SectionBranches;
                            ++Outcome.Statistics.TracedSections;
                            Sections.push_back({ Carrier, Low, High, Branch.ClosedCondition, static_cast<int>(IndexA), static_cast<int>(IndexB) });
                            continue;
                        }
                        ++Outcome.Statistics.SectionBranches;
                        const bool Closed = PeriodicCondition(Branch.Curve) && Branch.CurveHigh - Branch.CurveLow >= FullTurn - 1.0e-9;
                        Sections.push_back({ Branch.Curve, Branch.CurveLow, Branch.CurveHigh, Closed, static_cast<int>(IndexA), static_cast<int>(IndexB) });
                    }
                }
            }
            return true;
        }

        //----------------------------------------------------------------------------------------------------------------
        // ③ Paves and pave blocks
        //----------------------------------------------------------------------------------------------------------------

        void PlacePaves()
        {
            for (int Argument = 0; Argument < 2; ++Argument)
            {
                for (ArgumentEdge& Edge : Edges)
                {
                    if (Edge.Argument != Argument) continue;
                    for (size_t Vertex = 0; Vertex < Outcome.Vertices.size(); ++Vertex)
                    {
                        const int     Candidate = static_cast<int>(Vertex);
                        const Vector3 Position  = Outcome.Vertices[Vertex].Position;
                        if (Candidate == Edge.Start || Candidate == Edge.End || !Edge.Bounds.Contains(Position)) continue;
                        double Parameter = 0.0;
                        if (!LocateOnEdge(Edge, Position, Parameter)) continue;
                        if (Distance(Position, Outcome.Vertices[Edge.Start].Position) <= Settings.Precision) continue;
                        if (Distance(Position, Outcome.Vertices[Edge.End].Position) <= Settings.Precision) continue;
                        Edge.Paves.push_back({ std::clamp(Parameter, Edge.Low, Edge.High), Candidate });
                    }
                    std::sort(Edge.Paves.begin(), Edge.Paves.end());
                    Outcome.Statistics.Paves += static_cast<int>(Edge.Paves.size());

                    std::vector<std::pair<double, int>> Chain = { { Edge.Low, Edge.Start } };
                    Chain.insert(Chain.end(), Edge.Paves.begin(), Edge.Paves.end());
                    Chain.push_back({ Edge.High, Edge.End });
                    for (size_t Index = 0; Index + 1 < Chain.size(); ++Index)
                    {
                        const int Piece = RegisterEdge(Edge.Curve, Chain[Index].first, Chain[Index + 1].first, Chain[Index].second, Chain[Index + 1].second, Argument, false);
                        Edge.Pieces.push_back(Piece);
                        Edge.PieceRanges.push_back({ Chain[Index].first, Chain[Index + 1].first });
                    }
                    Outcome.Statistics.SplitEdges += static_cast<int>(Edge.Pieces.size());
                }
            }
        }

        void AddSection(ArgumentFace& Face, int Edge)
        {
            if (Face.Boundary.count(Edge) != 0 || std::find(Face.Sections.begin(), Face.Sections.end(), Edge) != Face.Sections.end()) return;
            Face.Sections.push_back(Edge);
        }

        void CollectFaceBoundaries()
        {
            for (ArgumentFace& Face : Faces)
            {
                for (LoopToken Loop : Partition.Faces.Resolve(Face.Token).Loops)
                {
                    Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
                    {
                        const ArgumentEdge& Edge = Edges[EdgeIndexBySlot[Face.Argument].at(Partition.Fins.Resolve(Fin).Edge.Slot)];
                        Face.Boundary.insert(Edge.Pieces.begin(), Edge.Pieces.end());
                    });
                }
            }
        }

        void SplitSections()
        {
            CollectFaceBoundaries();
            for (const SectionCandidate& Candidate : Sections)
            {
                const double  Scale  = ParameterScale(Candidate.Curve);                     // [len] - dP/dτ magnitude
                const double  Slack  = Settings.Precision / Scale;                           // [-]   - Parameter slack
                const SpatialBounds Bounds = BoundCurve(Candidate.Curve, Candidate.Low, Candidate.High, 4.0 * Settings.Precision);
                std::vector<std::pair<double, int>> Splits;
                for (size_t Vertex = 0; Vertex < Outcome.Vertices.size(); ++Vertex)
                {
                    const Vector3 Position = Outcome.Vertices[Vertex].Position;
                    if (!Bounds.Contains(Position)) continue;
                    double Parameter = ProjectOntoCurve(Candidate.Curve, Position, Candidate.Low);
                    if (Distance(EvaluateCurve(Candidate.Curve, Parameter).Position, Position) > Settings.Precision) continue;
                    if (!Candidate.Closed && (Parameter < Candidate.Low - Slack || Parameter > Candidate.High + Slack)) continue;
                    Splits.push_back({ Parameter, static_cast<int>(Vertex) });
                }
                std::sort(Splits.begin(), Splits.end());

                // Pieces (τa, va) → (τb, vb); vertex −1 = open end without a registry vertex.
                std::vector<std::array<double, 2>> Ranges;
                std::vector<std::array<int, 2>>    Ends;
                if (Candidate.Closed)
                {
                    if (Splits.empty())
                    {
                        const int Seed = RegisterVertex(EvaluateCurve(Candidate.Curve, Candidate.Low).Position);
                        Ranges.push_back({ Candidate.Low, Candidate.Low + FullTurn });
                        Ends.push_back({ Seed, Seed });
                    }
                    for (size_t Index = 0; Index < Splits.size(); ++Index)
                    {
                        const auto& From = Splits[Index];
                        const auto& To   = Splits[(Index + 1) % Splits.size()];
                        Ranges.push_back({ From.first, Index + 1 < Splits.size() ? To.first : To.first + FullTurn });
                        Ends.push_back({ From.second, To.second });
                    }
                }
                else
                {
                    std::vector<std::pair<double, int>> Chain = { { Candidate.Low, -1 } };
                    for (const auto& Split : Splits)
                    {
                        if (std::fabs(Split.first - Chain.back().first) <= Slack) Chain.back() = Split;
                        else Chain.push_back(Split);
                    }
                    if (std::fabs(Candidate.High - Chain.back().first) > Slack) Chain.push_back({ Candidate.High, -1 });
                    // 💡 A marched branch ends where its march stopped, ~10⁻⁸ off the exact vertex of the boundary crossing it
                    //    found (block edge piercing a cylinder: 2·10⁻⁸). The open end belongs to that vertex: a piece from an
                    //    unregistered end shorter than the degenerate-piece bound (10·precision, as below) is dropped instead
                    //    of becoming a 2·10⁻⁸ edge, which later joins collapsed to a zero-length closed edge (seq 6 · 5).
                    const auto Stub = [&](size_t Open, size_t Split) { return Chain[Open].second < 0 && Chain[Split].second >= 0 && std::fabs(Chain[Split].first - Chain[Open].first) * Scale <= 10.0 * Settings.Precision; };
                    if (Chain.size() >= 3 && Stub(0, 1)) Chain.erase(Chain.begin());
                    if (Chain.size() >= 3 && Stub(Chain.size() - 1, Chain.size() - 2)) Chain.pop_back();
                    for (size_t Index = 0; Index + 1 < Chain.size(); ++Index)
                    {
                        Ranges.push_back({ Chain[Index].first, Chain[Index + 1].first });
                        Ends.push_back({ Chain[Index].second, Chain[Index + 1].second });
                    }
                }

                for (size_t Index = 0; Index < Ranges.size(); ++Index)
                {
                    const double Low = Ranges[Index][0], High = Ranges[Index][1];            // [-] - Piece range
                    if (High - Low <= Slack || (Ends[Index][0] == Ends[Index][1] && Ends[Index][0] >= 0 && High - Low < FullTurn - 1.0e-9 && (High - Low) * Scale <= 10.0 * Settings.Precision)) continue;
                    const Vector3 Middle = EvaluateCurve(Candidate.Curve, 0.5 * (Low + High)).Position;
                    const PointMembership OnA = ClassifyFacePosition(Faces[Candidate.FaceA].Domain, Middle, Settings.Precision).Membership;
                    const PointMembership OnB = ClassifyFacePosition(Faces[Candidate.FaceB].Domain, Middle, Settings.Precision).Membership;
                    if (OnA == PointMembership::Outside || OnB == PointMembership::Outside) continue;
                    const int Start = Ends[Index][0] >= 0 ? Ends[Index][0] : RegisterVertex(EvaluateCurve(Candidate.Curve, Low).Position);
                    const int End   = Ends[Index][1] >= 0 ? Ends[Index][1] : RegisterVertex(EvaluateCurve(Candidate.Curve, High).Position);
                    const size_t Before = Outcome.Edges.size();
                    const int Edge = RegisterEdge(Candidate.Curve, Low, High, Start, End, -1, true);
                    Outcome.Statistics.SectionEdges += Outcome.Edges.size() > Before ? 1 : 0;
                    if (OnA == PointMembership::Inside) AddSection(Faces[Candidate.FaceA], Edge);
                    if (OnB == PointMembership::Inside) AddSection(Faces[Candidate.FaceB], Edge);
                }
            }
        }

        // Argument pave blocks lying inside a face of the other argument become sections of that face.
        void ImprintEdges()
        {
            if (!OverlapCondition) return;
            for (const ArgumentEdge& Edge : Edges)
            {
                if (!Edge.Bounds.Overlaps(OverlapBounds)) continue;
                for (size_t Index = 0; Index < Edge.Pieces.size(); ++Index)
                {
                    const Vector3 Middle = EvaluateCurve(Edge.Curve, 0.5 * (Edge.PieceRanges[Index].first + Edge.PieceRanges[Index].second)).Position;
                    if (!OverlapBounds.Contains(Middle)) continue;
                    for (ArgumentFace& Face : Faces)
                    {
                        if (Face.Argument == Edge.Argument || !Face.Bounds.Contains(Middle)) continue;
                        if (ClassifyFacePosition(Face.Domain, Middle, Settings.Precision).Membership != PointMembership::Inside) continue;
                        const size_t Before = Face.Sections.size();
                        AddSection(Face, Edge.Pieces[Index]);
                        Outcome.Statistics.ImprintedEdges += Face.Sections.size() > Before ? 1 : 0;
                    }
                }
            }
        }

        //----------------------------------------------------------------------------------------------------------------
        // ④ Face splitting
        //----------------------------------------------------------------------------------------------------------------

        [[nodiscard]] Vector3 EdgeTangent(const FuseEdge& Edge, double τ) const { return EvaluateCurve(Edge.Curve, τ).Derivative; }

        // Boundary uses of one face in loop order (virtual closures between fins that do not meet in the chart).
        [[nodiscard]] std::vector<FuseUse> CollectBoundaryUses(const ArgumentFace& Face) const
        {
            std::vector<FuseUse> Uses;
            for (LoopToken Loop : Partition.Faces.Resolve(Face.Token).Loops)
            {
                std::vector<FuseUse> LoopUses;
                Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
                {
                    const FinProfile&   FinRecord = Partition.Fins.Resolve(Fin);
                    const ArgumentEdge& Edge      = Edges[EdgeIndexBySlot[Face.Argument].at(FinRecord.Edge.Slot)];
                    const bool          Forward   = FinRecord.Sense == Orientation::Forward;
                    for (size_t Step = 0; Step < Edge.Pieces.size(); ++Step)
                    {
                        const size_t   Index = Forward ? Step : Edge.Pieces.size() - 1 - Step;
                        const FuseEdge& Block = Outcome.Edges[Edge.Pieces[Index]];
                        const double   OwnLow  = Edge.PieceRanges[Index].first;                 // [-] - τ on the fin's carrier
                        const double   OwnHigh = Edge.PieceRanges[Index].second;                // [-] - τ on the fin's carrier
                        FuseUse Use;
                        Use.Edge = Edge.Pieces[Index];
                        const bool SameCarrier = Block.Argument == Face.Argument && std::fabs(Block.Low - OwnLow) <= 1.0e-12 * (1.0 + std::fabs(OwnLow))
                                              && std::fabs(Block.High - OwnHigh) <= 1.0e-12 * (1.0 + std::fabs(OwnHigh))
                                              && Distance(EvaluateCurve(Block.Curve, 0.5 * (OwnLow + OwnHigh)).Position, EvaluateCurve(Edge.Curve, 0.5 * (OwnLow + OwnHigh)).Position) <= Settings.Precision;
                        if (SameCarrier)
                        {
                            Use.Sense        = FinRecord.Sense;
                            Use.SurfaceCurve = *FinRecord.SurfaceCurve;
                        }
                        else
                        {
                            // Common block owned by the other argument's carrier: re-fit the SP-curve on that carrier, in this fin's image.
                            const double  OwnMiddle  = 0.5 * (OwnLow + OwnHigh);                 // [-] - Midpoint on the fin carrier
                            const Vector3 OwnTangent = EvaluateCurve(Edge.Curve, OwnMiddle).Derivative * (Forward ? 1.0 : -1.0);
                            Use.Sense        = Dot(OwnTangent, EdgeTangent(Block, 0.5 * (Block.Low + Block.High))) > 0.0 ? Orientation::Forward : Orientation::Reversed;
                            Use.SurfaceCurve = FitSurfaceCurve(Face.Domain.Surface, Block.Curve, Block.Low, Block.High, Face.Domain.Window,
                                                               EvaluateParameterCurve(*FinRecord.SurfaceCurve, OwnMiddle).Position);
                        }
                        const bool Along = Use.Sense == Orientation::Forward;
                        Use.Start       = Along ? Block.Low : Block.High;
                        Use.End         = Along ? Block.High : Block.Low;
                        Use.VertexStart = Along ? Block.Start : Block.End;
                        Use.VertexEnd   = Along ? Block.End : Block.Start;
                        LoopUses.push_back(std::move(Use));
                    }
                });
                for (size_t Index = 0; Index < LoopUses.size(); ++Index)
                {
                    const FuseUse& Current = LoopUses[Index];
                    const FuseUse& Next    = LoopUses[(Index + 1) % LoopUses.size()];
                    Uses.push_back(Current);
                    const Vector2 Gap0 = EvaluateParameterCurve(Current.SurfaceCurve, Current.End).Position;
                    const Vector2 Gap1 = EvaluateParameterCurve(Next.SurfaceCurve, Next.Start).Position;
                    if (Distance(Gap0, Gap1) > 1.0e-9)
                    {
                        FuseUse Closure;
                        Closure.Edge         = -1;
                        Closure.SurfaceCurve = ParameterLine{ Gap0, Gap1 - Gap0 };
                        Closure.Start        = 0.0;
                        Closure.End          = 1.0;
                        Closure.VertexStart  = Current.VertexEnd;
                        Closure.VertexEnd    = Current.VertexEnd;
                        Uses.push_back(std::move(Closure));
                    }
                }
            }
            return Uses;
        }

        // Direction leaving a junction along an arc (chart, u mirrored for reversed faces), with a short chord as tie-break.
        [[nodiscard]] static std::pair<double, double> DepartureKey(const ChartArc& Arc, Vector2 Reference, double Mirror, bool Leaving)
        {
            const FuseUse& Use  = Arc.Use;
            const double   Sign = Use.End >= Use.Start ? 1.0 : -1.0;                         // [-] - dτ direction
            const double   τ    = Leaving ? Use.Start : Use.End;                             // [-] - At the junction
            Vector2 Tangent = EvaluateParameterCurve(Use.SurfaceCurve, τ).Derivative * (Leaving ? Sign : -Sign);
            const double Step   = 1.0e-3 * (Use.End - Use.Start) * (Leaving ? 1.0 : -1.0);   // [-] - Chord step
            Vector2 Chord = EvaluateParameterCurve(Use.SurfaceCurve, τ + Step).Position - EvaluateParameterCurve(Use.SurfaceCurve, τ).Position;
            if (Length(Tangent) < 1.0e-14) Tangent = Chord;
            Tangent = { Tangent.X * Mirror, Tangent.Y };
            Chord   = { Chord.X * Mirror, Chord.Y };
            const auto Clockwise = [&](Vector2 Direction)
            {
                double Angle = std::atan2(-Cross(Reference, Direction), Dot(Reference, Direction));
                if (Angle <= 1.0e-12) Angle += FullTurn;
                return Angle;
            };
            return { Clockwise(Tangent), Clockwise(Chord) };
        }

        bool SplitFace(int FaceIndex)
        {
            const ArgumentFace& Face   = Faces[FaceIndex];
            const double        Mirror = Face.Domain.Sense == Orientation::Forward ? 1.0 : -1.0;    // [-] - Interior on the left

            // Fast path: an untouched single-loop face with unsplit own-carrier edges emits its loop directly.
            const FaceProfile& SourceProfile = Partition.Faces.Resolve(Face.Token);
            if (Face.Sections.empty() && SourceProfile.Loops.size() == 1)
            {
                bool Untouched = true;
                Partition.EnumerateLoopFins(SourceProfile.Loops[0], [&](FinToken Fin)
                {
                    const ArgumentEdge& Edge = Edges[EdgeIndexBySlot[Face.Argument].at(Partition.Fins.Resolve(Fin).Edge.Slot)];
                    if (Edge.Pieces.size() != 1 || Outcome.Edges[Edge.Pieces[0]].Argument != Face.Argument) Untouched = false;
                });
                if (Untouched)
                {
                    FuseFragment Fragment;
                    Fragment.Argument = Face.Argument;
                    Fragment.Source   = Face.Token;
                    Fragment.Surface  = Face.Domain.Surface;
                    Fragment.Sense    = Face.Domain.Sense;
                    Fragment.Loops.push_back(CollectBoundaryUses(Face));
                    Outcome.Fragments.push_back(std::move(Fragment));
                    ++Outcome.Statistics.Fragments[Face.Argument];
                    return true;
                }
            }

            std::vector<ChartArc> Arcs;
            for (const FuseUse& Use : CollectBoundaryUses(Face)) Arcs.push_back({ Use });
            for (int Edge : Face.Sections)
            {
                const FuseEdge& Block = Outcome.Edges[Edge];
                FuseUse Use;
                Use.Edge         = Edge;
                Use.SurfaceCurve = FitSurfaceCurve(Face.Domain.Surface, Block.Curve, Block.Low, Block.High, Face.Domain.Window);
                Use.Sense = Orientation::Forward;  Use.Start = Block.Low;  Use.End = Block.High; Use.VertexStart = Block.Start; Use.VertexEnd = Block.End;
                Arcs.push_back({ Use });
                Use.Sense = Orientation::Reversed; Use.Start = Block.High; Use.End = Block.Low;  Use.VertexStart = Block.End;   Use.VertexEnd = Block.Start;
                Arcs.push_back({ Use });
                Arcs[Arcs.size() - 2].Twin = static_cast<int>(Arcs.size()) - 1;
                Arcs.back().Twin           = static_cast<int>(Arcs.size()) - 2;
            }

            // Junctions: (vertex, chart position) clusters.
            std::vector<std::pair<int, Vector2>> Junctions;
            const auto FindJunction = [&](int Vertex, Vector2 Position)
            {
                for (size_t Index = 0; Index < Junctions.size(); ++Index)
                {
                    if (Junctions[Index].first == Vertex && Distance(Junctions[Index].second, Position) <= JunctionGap * (1.0 + Length(Position))) return static_cast<int>(Index);
                }
                Junctions.push_back({ Vertex, Position });
                return static_cast<int>(Junctions.size()) - 1;
            };
            for (ChartArc& Arc : Arcs)
            {
                Arc.From     = EvaluateParameterCurve(Arc.Use.SurfaceCurve, Arc.Use.Start).Position;
                Arc.To       = EvaluateParameterCurve(Arc.Use.SurfaceCurve, Arc.Use.End).Position;
                Arc.JunctionFrom = FindJunction(Arc.Use.VertexStart, Arc.From);
                Arc.JunctionTo   = FindJunction(Arc.Use.VertexEnd, Arc.To);
            }
            // Virtual closures are split at junctions of their vertex lying on them (sections ending on a singular row).
            for (size_t Index = 0; Index < Arcs.size(); ++Index)
            {
                if (Arcs[Index].Use.Edge >= 0) continue;
                const Vector2 From = Arcs[Index].From, To = Arcs[Index].To;
                std::vector<std::pair<double, int>> Inner;
                for (size_t Junction = 0; Junction < Junctions.size(); ++Junction)
                {
                    if (Junctions[Junction].first != Arcs[Index].Use.VertexStart) continue;
                    const Vector2 Span = To - From;
                    const double  Fraction = Dot(Junctions[Junction].second - From, Span) / Dot(Span, Span);
                    if (Fraction <= 1.0e-9 || Fraction >= 1.0 - 1.0e-9) continue;
                    if (Distance(From + Span * Fraction, Junctions[Junction].second) > JunctionGap * (1.0 + Length(From))) continue;
                    Inner.push_back({ Fraction, static_cast<int>(Junction) });
                }
                if (Inner.empty()) continue;
                std::sort(Inner.begin(), Inner.end());
                int     PreviousJunction = Arcs[Index].JunctionFrom;
                Vector2 Previous     = From;
                const int FinalJunction  = Arcs[Index].JunctionTo;
                const auto MakeClosure = [&](Vector2 A, Vector2 B, int JunctionA, int JunctionB)
                {
                    ChartArc Piece = Arcs[Index];
                    Piece.Use.SurfaceCurve = ParameterLine{ A, B - A };
                    Piece.From = A; Piece.To = B; Piece.JunctionFrom = JunctionA; Piece.JunctionTo = JunctionB;
                    return Piece;
                };
                std::vector<ChartArc> Pieces;
                for (const auto& [Fraction, Junction] : Inner)
                {
                    Pieces.push_back(MakeClosure(Previous, Junctions[Junction].second, PreviousJunction, Junction));
                    Previous     = Junctions[Junction].second;
                    PreviousJunction = Junction;
                }
                Pieces.push_back(MakeClosure(Previous, To, PreviousJunction, FinalJunction));
                Arcs[Index] = Pieces.front();
                for (size_t Piece = 1; Piece < Pieces.size(); ++Piece) Arcs.push_back(Pieces[Piece]);
            }

            // Outgoing arcs per junction.
            std::vector<std::vector<int>> Outgoing(Junctions.size());
            for (size_t Index = 0; Index < Arcs.size(); ++Index) Outgoing[Arcs[Index].JunctionFrom].push_back(static_cast<int>(Index));

            // Smallest clockwise turn from the reversed incoming direction keeps the interior on the left.
            std::vector<std::vector<int>> Cycles;
            for (size_t Seed = 0; Seed < Arcs.size(); ++Seed)
            {
                if (Arcs[Seed].Consumed) continue;
                std::vector<int> Cycle;
                int Current = static_cast<int>(Seed);
                bool Closed = false;
                for (size_t Guard = 0; Guard <= Arcs.size(); ++Guard)
                {
                    Arcs[Current].Consumed = true;
                    Cycle.push_back(Current);
                    const ChartArc& Arc = Arcs[Current];
                    const std::pair<double, double> Back = DepartureKey(Arc, { 1.0, 0.0 }, Mirror, false);
                    const Vector2 Reference = { std::cos(-Back.first), std::sin(-Back.first) };  // [-] - Reversed incoming direction
                    int    Chosen = -1;
                    std::pair<double, double> Best = { std::numeric_limits<double>::max(), 0.0 };
                    for (int Candidate : Outgoing[Arc.JunctionTo])
                    {
                        if (Arcs[Candidate].Consumed && Candidate != static_cast<int>(Seed)) continue;
                        std::pair<double, double> Key = DepartureKey(Arcs[Candidate], Reference, Mirror, true);
                        if (Candidate == Arc.Twin) Key = { FullTurn + 1.0, FullTurn + 1.0 };
                        const bool Better = Key.first < Best.first - 1.0e-9 || (std::fabs(Key.first - Best.first) <= 1.0e-9 && Key.second < Best.second);
                        if (Better) { Best = Key; Chosen = Candidate; }
                    }
                    if (Chosen < 0) break;
                    if (Chosen == static_cast<int>(Seed)) { Closed = true; break; }
                    Current = Chosen;
                }
                if (!Closed)
                {
                    Outcome.Refusal = "GeneralFuse: face splitting found an open chain (inconsistent section imprint)";
                    return false;
                }
                Cycles.push_back(std::move(Cycle));
            }

            // Outer cycles (positive oriented area) and holes (negative), holes assigned by containment.
            struct TracedCycle { std::vector<DomainSegment> Segments; double Area = 0.0; std::vector<int> Arcs; };
            std::vector<TracedCycle> Outers, Holes;
            for (const std::vector<int>& Cycle : Cycles)
            {
                TracedCycle Traced;
                Traced.Arcs = Cycle;
                bool RealEdge = false;
                for (int Index : Cycle)
                {
                    const FuseUse& Use = Arcs[Index].Use;
                    Traced.Segments.push_back({ Use.SurfaceCurve, Use.Start, Use.End, Use.Edge < 0 });
                    RealEdge = RealEdge || Use.Edge >= 0;
                }
                Traced.Area = MeasureLoopArea(Traced.Segments) * Mirror;
                const double Window = (Face.Domain.Window.High.X - Face.Domain.Window.Low.X) * (Face.Domain.Window.High.Y - Face.Domain.Window.Low.Y);
                if (!RealEdge || std::fabs(Traced.Area) <= 1.0e-12 * Window) { ++Outcome.Statistics.DroppedCycles; continue; }
                (Traced.Area > 0.0 ? Outers : Holes).push_back(std::move(Traced));
            }
            std::vector<std::vector<int>> HolesOf(Outers.size());
            for (size_t Hole = 0; Hole < Holes.size(); ++Hole)
            {
                // 📝 Witness: a point of the hole displaced slightly to the LEFT of its traversal (the material side by
                //    construction), so it lies strictly inside the owning fragment and strictly outside the hole's twin
                //    cycle (a point ON the twin would give an ambiguous winding of ±½).
                const DomainSegment* Longest = &Holes[Hole].Segments.front();
                double LongestSpan = 0.0;                                                    // [-] - Chart chord
                for (const DomainSegment& Segment : Holes[Hole].Segments)
                {
                    if (Segment.Virtual) continue;
                    const double Span = Distance(EvaluateParameterCurve(Segment.Curve, Segment.Start).Position, EvaluateParameterCurve(Segment.Curve, Segment.End).Position)
                                      + Distance(EvaluateParameterCurve(Segment.Curve, 0.5 * (Segment.Start + Segment.End)).Position, EvaluateParameterCurve(Segment.Curve, Segment.Start).Position);
                    if (Span > LongestSpan) { LongestSpan = Span; Longest = &Segment; }
                }
                const double          Middle = Longest->Start + 0.4142 * (Longest->End - Longest->Start);   // [-] - Off the bisection junctions
                const ParameterSample Sample = EvaluateParameterCurve(Longest->Curve, Middle);
                const Vector2 Heading = Normalize(Vector2{ Sample.Derivative.X * Mirror, Sample.Derivative.Y } * (Longest->End >= Longest->Start ? 1.0 : -1.0));
                const Vector2 Left    = { -Heading.Y * Mirror, Heading.X };                  // [-] - Left normal, back in the chart
                const double  Offset  = 1.0e-5 * Length(Face.Domain.Window.High - Face.Domain.Window.Low);
                const Vector2 Witness = Sample.Position + Left * Offset;
                int    Owner = -1;
                double OwnerArea = std::numeric_limits<double>::max();
                for (size_t Outer = 0; Outer < Outers.size(); ++Outer)
                {
                    FaceDomain Single;
                    Single.Surface = Face.Domain.Surface;
                    Single.Loops   = { Outers[Outer].Segments };
                    if (std::fabs(MeasureParameterWinding(Single, Witness)) > 0.5 && Outers[Outer].Area < OwnerArea)
                    {
                        Owner     = static_cast<int>(Outer);
                        OwnerArea = Outers[Outer].Area;
                    }
                }
                if (Owner < 0)
                {
                    Outcome.Refusal = "GeneralFuse: a hole cycle has no enclosing outer cycle";
                    return false;
                }
                HolesOf[Owner].push_back(static_cast<int>(Hole));
            }
            for (size_t Outer = 0; Outer < Outers.size(); ++Outer)
            {
                FuseFragment Fragment;
                Fragment.Argument = Face.Argument;
                Fragment.Source   = Face.Token;
                Fragment.Surface  = Face.Domain.Surface;
                Fragment.Sense    = Face.Domain.Sense;
                const auto AppendLoop = [&](const TracedCycle& Cycle)
                {
                    std::vector<FuseUse> Loop;
                    for (int Index : Cycle.Arcs) Loop.push_back(Arcs[Index].Use);
                    Fragment.Loops.push_back(std::move(Loop));
                };
                AppendLoop(Outers[Outer]);
                for (int Hole : HolesOf[Outer]) AppendLoop(Holes[Hole]);
                Outcome.Fragments.push_back(std::move(Fragment));
                ++Outcome.Statistics.Fragments[Face.Argument];
            }
            return true;
        }

        //----------------------------------------------------------------------------------------------------------------
        // ⑤ Classification
        //----------------------------------------------------------------------------------------------------------------

        bool ClassifyFragments()
        {
            const BodyContainment Containment[2] = { PrepareBodyContainment(Partition, Arguments[0]), PrepareBodyContainment(Partition, Arguments[1]) };
            // 💡 State propagation (OCCT builder): two fragments of one argument that share a clean pave block — an argument
            //    edge piece no fragment of the OTHER argument uses (not a section, not a common block, not imprinted) — lie
            //    on the same side of the other solid, since its boundary neither crosses (that splits the edge at a pave)
            //    nor contains (that imprints it) the block. One winding per connected group replaces one per fragment
            //    (55 % of a Boolean's time). Only Inside / Outside propagate: a representative found ON (coincident faces)
            //    sends every member of its group through its own classification, as before.
            const size_t Count = Outcome.Fragments.size();
            std::vector<std::vector<int>> Users(Outcome.Edges.size());
            for (size_t Index = 0; Index < Count; ++Index)
                for (const auto& Loop : Outcome.Fragments[Index].Loops)
                    for (const FuseUse& Use : Loop)
                        if (Use.Edge >= 0 && (Users[Use.Edge].empty() || Users[Use.Edge].back() != static_cast<int>(Index))) Users[Use.Edge].push_back(static_cast<int>(Index));
            std::vector<int> Group(Count);
            std::iota(Group.begin(), Group.end(), 0);
            const std::function<int(int)> Root = [&](int Index) { return Group[Index] == Index ? Index : Group[Index] = Root(Group[Index]); };
            for (size_t Edge = 0; Edge < Outcome.Edges.size(); ++Edge)
            {
                const FuseEdge& Block = Outcome.Edges[Edge];
                if (Block.Section || Block.Argument < 0 || Users[Edge].size() < 2) continue;
                const bool Clean = std::all_of(Users[Edge].begin(), Users[Edge].end(), [&](int User) { return Outcome.Fragments[User].Argument == Block.Argument; });
                if (!Clean) continue;
                for (size_t User = 1; User < Users[Edge].size(); ++User) Group[Root(Users[Edge][User])] = Root(Users[Edge][0]);
            }
            std::vector<int> Decided(Count, -1);                                             // [-] - Root → FragmentCondition; −1 open, −2 never
            for (size_t Index = 0; Index < Count; ++Index)
            {
                FuseFragment& Fragment = Outcome.Fragments[Index];
                // 💡 Sheet cuts keep only A fragments: B's are never classified against the open A (no integral winding).
                if (Fragment.Argument == 1 && !Settings.SecondClassificationEnabled) continue;
                const int Owner = Root(static_cast<int>(Index));
                if (Decided[Owner] >= 0)
                {
                    Fragment.State = static_cast<FragmentCondition>(Decided[Owner]);
                    ++Outcome.Statistics.States[Decided[Owner]];
                    ++Outcome.Statistics.PropagatedStates;
                    continue;
                }
                FaceDomain Domain;
                Domain.Surface = Fragment.Surface;
                Domain.Sense   = Fragment.Sense;
                for (const auto& Loop : Fragment.Loops)
                {
                    std::vector<DomainSegment> Segments;
                    for (const FuseUse& Use : Loop) Segments.push_back({ Use.SurfaceCurve, Use.Start, Use.End, Use.Edge < 0 });
                    Domain.Loops.push_back(std::move(Segments));
                }
                CompleteFaceDomain(Domain);
                const std::optional<Vector2> Interior = LocateFaceInterior(Domain);
                if (!Interior.has_value())
                {
                    Outcome.Refusal = "GeneralFuse: no interior point found on a split face";
                    return false;
                }
                Fragment.Probe = EvaluateSurface(Fragment.Surface, *Interior).Position;
                const BodyContainment& Other = Containment[1 - Fragment.Argument];
                // 💡 Two-tier winding: membership needs only the nearest integer, so a coarse quadrature (10⁻⁵ / 10⁻⁶ per panel)
                //    screens first; a result within ScreenResidual of an integer (10× inside the refusal gate, 5000× inside
                //    the ½ decision margin) is kept, anything else is recomputed at the strict settings, so an open argument
                //    still shows its true residual and is refused. Freeform faces near the probe made the strict pass cost
                //    ~6·10⁶ evaluations per fragment (BMW wheel arch: 76 s).
                const SolidPointClass Class = ClassifySolidPointScreened(Other, Fragment.Probe, Settings.Containment);
                // ⚠️ A closed argument gives an integral winding off its boundary; anything else (open argument, probe on an
                //    unmerged boundary) is refused rather than rounded.
                if (Class.Method == ContainmentMethod::Winding && Class.IntegerResidual > IntegralTolerance)
                {
                    Outcome.Refusal = "GeneralFuse: fragment winding " + std::to_string(Class.Winding) + " is not integral (argument not closed?)";
                    return false;
                }
                if (Class.Membership == PointMembership::Inside)       Fragment.State = FragmentCondition::Inside;
                else if (Class.Membership == PointMembership::Outside) Fragment.State = FragmentCondition::Outside;
                else
                {
                    const FaceDomain& Touched = Other.Faces[Class.BoundaryFace];
                    const Vector2 Foot = ProjectOntoSurface(Touched.Surface, Fragment.Probe, (Touched.Window.Low + Touched.Window.High) * 0.5);
                    const Vector3 OwnNormal   = EvaluateSurfaceNormal(Fragment.Surface, *Interior) * (Fragment.Sense == Orientation::Forward ? 1.0 : -1.0);
                    const Vector3 OtherNormal = EvaluateSurfaceNormal(Touched.Surface, Foot) * (Touched.Sense == Orientation::Forward ? 1.0 : -1.0);
                    Fragment.State = Dot(OwnNormal, OtherNormal) > 0.0 ? FragmentCondition::OnSame : FragmentCondition::OnOpposite;
                }
                ++Outcome.Statistics.States[static_cast<int>(Fragment.State)];
                // A group with an ON member breaks the clean-block premise somewhere: every later member classifies itself.
                if (Decided[Owner] == -1) Decided[Owner] = Fragment.State == FragmentCondition::Inside || Fragment.State == FragmentCondition::Outside ? static_cast<int>(Fragment.State) : -2;
            }
            return true;
        }
    };

    //--------------------------------------------------------------------------------------------------------------------
    //                                                SELECTION
    //--------------------------------------------------------------------------------------------------------------------

    // Keep / flip rule per operation (OCCT BOP selection over GF fragments).
    [[nodiscard]] std::pair<bool, bool> SelectFragment(BooleanOperation Operation, int Argument, FragmentCondition State)
    {
        using enum FragmentCondition;
        const bool First = Argument == 0;
        switch (Operation)
        {
            case BooleanOperation::Union:              return { State == Outside || (First && State == OnSame), false };
            case BooleanOperation::Intersection:       return { State == Inside || (First && State == OnSame), false };
            case BooleanOperation::Difference:         return First ? std::pair{ State == Outside || State == OnOpposite, false } : std::pair{ State == Inside, true };
            case BooleanOperation::ReversedDifference: return First ? std::pair{ State == Inside, true } : std::pair{ State == Outside || State == OnOpposite, false };
            case BooleanOperation::FirstInside:        return { First && State == Inside, false };
            case BooleanOperation::FirstOutside:       return { First && State == Outside, false };
            case BooleanOperation::Imprint:            return { First, false };
        }
        return { false, false };
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

GeneralFuseOutcome RunGeneralFuse(const ModelPartition& Partition, BodyToken First, BodyToken Second, const GeneralFuseSettings& Settings)
{
    FuseBuilder Builder(Partition, First, Second, Settings);
    return Builder.Run();
}

BooleanOutcome AssembleBoolean(ModelPartition& Partition, const GeneralFuseOutcome& Fuse, BooleanOperation Operation, bool MaximalFacesEnabled)
{
    BooleanOutcome Outcome;
    if (!Fuse.Refusal.empty()) { Outcome.Refusal = Fuse.Refusal; return Outcome; }

    // ① Selection (BOP = GF + selection): kept fragments become blueprint faces, flipped ones with reversed loops.
    std::vector<std::pair<int, bool>> Kept;                                                  // [-] - (fragment, flipped)
    for (size_t Index = 0; Index < Fuse.Fragments.size(); ++Index)
    {
        const auto [Keep, Flip] = SelectFragment(Operation, Fuse.Fragments[Index].Argument, Fuse.Fragments[Index].State);
        if (Keep) Kept.push_back({ static_cast<int>(Index), Flip });
    }
    if (Kept.empty()) { Outcome.Refusal = "Boolean: empty result"; return Outcome; }
    BodyBlueprint Blueprint;
    if (Operation == BooleanOperation::FirstInside || Operation == BooleanOperation::FirstOutside) Blueprint.Category = BodyCategory::Sheet;
    for (const FuseVertex& Vertex : Fuse.Vertices) Blueprint.Vertices.push_back({ Vertex.Position });
    for (const FuseEdge& Block : Fuse.Edges) Blueprint.Edges.push_back({ Block.Curve, Block.Low, Block.High, Block.Start, Block.End });
    for (const auto& [Index, Flip] : Kept)
    {
        const FuseFragment& Fragment = Fuse.Fragments[Index];
        BlueprintFace Face{ Fragment.Surface, Flip ? InvertOrientation(Fragment.Sense) : Fragment.Sense, {} };
        for (const auto& LoopUses : Fragment.Loops)
        {
            std::vector<BlueprintUse> Loop;
            for (const FuseUse& Use : LoopUses)
            {
                if (Use.Edge < 0) continue;                                                  // Virtual closure along a singular row
                Loop.push_back({ Use.Edge, Flip ? InvertOrientation(Use.Sense) : Use.Sense, Use.SurfaceCurve });
            }
            if (Flip) std::reverse(Loop.begin(), Loop.end());
            Face.Loops.push_back(std::move(Loop));
        }
        Blueprint.Faces.push_back(std::move(Face));
    }

    // ② Assembly, then OCCT-style history: every result face is Modified from one argument face.
    const ArrangementOutcome Assembly = AssembleBody(Partition, Blueprint, "Boolean");
    if (!Assembly.Refusal.empty()) { Outcome.Refusal = Assembly.Refusal; return Outcome; }
    // ⚠️ Topology gate (T1–T5, no geometry: ~1 % of the assembly): every edge used twice is not enough — two argument
    //    faces tangent at a point (crossing cylinders with r₁ + offset = r₂) leave the result pinched at one vertex,
    //    V − E + 2F − L odd. A non-manifold solid is refused here rather than returned unsound.
    if (Blueprint.Category != BodyCategory::Sheet)
    {
        const AuditReport Topology = AuditBody(Partition, Assembly.Body, AuditScope::Topology);
        if (!Topology.SoundCondition())
        {
            const AuditFinding& First = Topology.Findings.front();
            Outcome.Refusal = "Boolean: non-manifold result (" + std::string(First.Code) + " on " + First.Subject + ": " + First.Detail + "; tangent contact?)";
            return Outcome;
        }
    }
    for (size_t Slot = 0; Slot < Kept.size(); ++Slot)
    {
        const FuseFragment& Fragment = Fuse.Fragments[Kept[Slot].first];
        Outcome.FaceHistories.push_back({ Assembly.Faces[Slot], Fragment.Argument, Fragment.Source, Fragment.State, Kept[Slot].second });
    }
    for (int Index : Assembly.EdgeOrder) Outcome.EdgeHistories.push_back({ Assembly.Edges[Index], Fuse.Edges[Index].Argument });
    Outcome.Body   = Assembly.Body;
    Outcome.Solids = Assembly.Solids;
    Outcome.Shells = Assembly.Shells;
    Outcome.Faces  = static_cast<int>(Kept.size());
    if (!MaximalFacesEnabled) return Outcome;

    // ③ Maximal faces: merged fragments hand their history to the surviving face (OCCT: Modified → the merged face).
    const RedundancyOutcome Redundancy = RemoveRedundantConnectivity(Partition, Outcome.Body);
    for (const auto& [Absorbed, Survivor] : Redundancy.FaceMerges)
    {
        for (FaceHistory& History : Outcome.FaceHistories) if (History.Result == Absorbed) History.Result = Survivor;
    }
    std::erase_if(Outcome.EdgeHistories, [&](const EdgeHistory& History)
    {
        return std::find(Redundancy.EdgesRemoved.begin(), Redundancy.EdgesRemoved.end(), History.Result) != Redundancy.EdgesRemoved.end();
    });
    Outcome.Faces      -= Redundancy.FacesMerged;
    Outcome.FacesMerged = Redundancy.FacesMerged;
    Outcome.EdgesJoined = Redundancy.EdgesJoined;
    return Outcome;
}

static BooleanOutcome ComputeBooleanUntraced(ModelPartition& Partition, BodyToken First, BodyToken Second, BooleanOperation Operation, const GeneralFuseSettings& Settings, GeneralFuseStatistics* Statistics)
{
    const GeneralFuseOutcome Fuse = RunGeneralFuse(Partition, First, Second, Settings);
    if (Statistics != nullptr) *Statistics = Fuse.Statistics;
    return AssembleBoolean(Partition, Fuse, Operation, Settings.MaximalFacesEnabled);
}

// 📝 Traced entry (Diagnostics/OperationTrace.h): the request on entry, the outcome before the duration line.

BooleanOutcome ComputeBoolean(ModelPartition& Partition, BodyToken First, BodyToken Second, BooleanOperation Operation, const GeneralFuseSettings& Settings, GeneralFuseStatistics* Statistics)
{
    static const char* Verbs[] = { "Union", "Intersection", "Difference", "ReversedDifference", "FirstInside", "FirstOutside", "Imprint" };
    const size_t Verb = static_cast<size_t>(Operation);
    FRONTIER_TRACE_SCOPE("Boolean", std::string(Verb < 7 ? Verbs[Verb] : "Boolean") + " · bodies " + std::to_string(First.Slot) + " and " + std::to_string(Second.Slot));
    BooleanOutcome Outcome = ComputeBooleanUntraced(Partition, First, Second, Operation, Settings, Statistics);
    FRONTIER_TRACE("Boolean", Outcome.Refusal.empty() ? std::to_string(Outcome.Solids) + " solid(s) · " + std::to_string(Outcome.Faces) + " face(s)"
                                                      : "refused: " + Outcome.Refusal);
    return Outcome;
}


const char* BooleanOperationLabel(BooleanOperation Operation) noexcept
{
    constexpr const char* Labels[] = { "Union", "Intersection", "Difference", "ReversedDifference", "FirstInside", "FirstOutside", "Imprint" };
    return Labels[static_cast<int>(Operation)];
}

BooleanOutcome ImprintBody(ModelPartition& Partition, BodyToken Body, BodyToken Tool, const Vector3& PlaneOrigin, const Vector3& PlaneNormal, int& Faces)
{
    FRONTIER_TRACE_SCOPE("Boolean", "Imprint · body " + std::to_string(Body.Slot) + " by " + std::to_string(Tool.Slot));
    Faces = static_cast<int>(Partition.CollectInventory(Body).Faces.size());
    GeneralFuseSettings Settings;
    Settings.MaximalFacesEnabled = false;
    const GeneralFuseOutcome Fuse    = RunGeneralFuse(Partition, Body, Tool, Settings);
    BooleanOutcome           Outcome = AssembleBoolean(Partition, Fuse, BooleanOperation::Imprint, false);
    if (!Outcome.Refusal.empty()) { FRONTIER_TRACE("Boolean", "refused: " + Outcome.Refusal); return Outcome; }

    // Heal every split not on the plane: an edge stays only when a face it bounds lies in the plane (the imprinted face);
    // splits on faces across the plane (the tool grazing, or its start cap passing inside, a raised neighbour) merge back.
    const double  Tolerance = 1.0e-6 * std::max(1.0, Length(PlaneOrigin));                   // [len] - In-plane tolerance (join)
    const Vector3 Normal    = Normalize(PlaneNormal);
    const auto Coplanar = [&](FaceToken Face)
    {
        const auto* Plane = std::get_if<PlaneSurface>(&Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface));
        return Plane != nullptr && std::fabs(Dot(Plane->Frame.ZAxis, Normal)) >= 1.0 - 1.0e-9 && std::fabs(Dot(Plane->Frame.Origin - PlaneOrigin, Normal)) <= Tolerance;
    };
    const RedundancyOutcome Redundancy = RemoveRedundantConnectivity(Partition, Outcome.Body, SessionPrecision, [&](EdgeToken Edge)
    {
        const FinToken First    = Partition.Edges.Resolve(Edge).RadialFin;
        if (!First.PresentCondition()) return true;
        const size_t   FinLimit = Partition.Fins.OccupiedCount() + 1;
        FinToken       Fin      = First;
        size_t         Steps    = 0;
        do
        {
            if (Coplanar(Partition.Loops.Resolve(Partition.Fins.Resolve(Fin).Loop).Face)) return false;
            Fin = Partition.Fins.Resolve(Fin).RadialSuccessor;
        } while (Fin.PresentCondition() && !(Fin == First) && ++Steps <= FinLimit);
        return true;
    });
    for (const auto& [Absorbed, Survivor] : Redundancy.FaceMerges)
    {
        for (FaceHistory& History : Outcome.FaceHistories) if (History.Result == Absorbed) History.Result = Survivor;
    }
    std::erase_if(Outcome.EdgeHistories, [&](const EdgeHistory& History)
    {
        return std::find(Redundancy.EdgesRemoved.begin(), Redundancy.EdgesRemoved.end(), History.Result) != Redundancy.EdgesRemoved.end();
    });
    Outcome.Faces      -= Redundancy.FacesMerged;
    Outcome.FacesMerged = Redundancy.FacesMerged;
    Outcome.EdgesJoined = Redundancy.EdgesJoined;
    FRONTIER_TRACE("Boolean", std::to_string(Faces) + " → " + std::to_string(Outcome.Faces) + " face(s) · " + std::to_string(Redundancy.FacesMerged) + " off-plane split(s) healed");
    return Outcome;
}

const char* FragmentStateLabel(FragmentCondition State) noexcept
{
    constexpr const char* Labels[] = { "Inside", "Outside", "OnSame", "OnOpposite" };
    return Labels[static_cast<int>(State)];
}

} // namespace Frontier::GeometrySpace::Parametric
