/*====================================================================================================================================
                                                       WINDINGNUMBER.CPP
====================================================================================================================================*/
// 🧩 Generalized winding number of trimmed analytic faces: point-in-solid with precision-aware boundary detection

#include "Engine/GeometrySpace/Parametric/Containment/WindingNumber.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace Frontier::GeometrySpace::Parametric
{

namespace
{
    constexpr double FullSphere  = 4.0 * std::numbers::pi;                                   // [sr]  - 4π
    constexpr double PanelAngle  = std::numbers::pi / 8.0;                                   // [-]   - Initial panel on curved SP-curves
    constexpr int    LinePanels  = 4;                                                        // [-]   - Initial panels on lines
    constexpr double FloorTolerance = 1.0e-15;                                               // [-]   - Smallest panel tolerance
    constexpr double RelativeFloor  = 1.0e-11;                                               // [-]   - Panel relative acceptance

    // 📝 Gauss–Kronrod 7/15 (abscissae of the 15-point rule; odd entries are the 7-point Gauss abscissae).
    constexpr std::array<double, 8> KronrodAbscissae = {
        0.991455371120812639206854697526329, 0.949107912342758524526189684047851, 0.864864423359769072789712788640926,
        0.741531185599394439863864773280788, 0.586087235467691130294144845693013, 0.405845151377397166906606412076961,
        0.207784955007898467600689403773245, 0.0 };
    constexpr std::array<double, 8> KronrodWeights = {
        0.022935322010529224963732008058970, 0.063092092629978553290700663189204, 0.104790010322250183839876322541518,
        0.140653259715525918745189590510238, 0.169004726639267902826583426598550, 0.190350578064785409913256402421014,
        0.204432940075298892414161999234649, 0.209482141084727828012999174891714 };
    constexpr std::array<double, 4> GaussWeights = {
        0.129484966168869693270611432679082, 0.279705391489276667901467771423780, 0.381830050505118944950369775488975,
        0.417959183673469387755102040816327 };

    template <typename Integrand>
    [[nodiscard]] double IntegrateAdaptive(double Low, double High, const Integrand& Function, double Tolerance, int Depth)
    {
        const double Centre = 0.5 * (Low + High);                                            // [-] - Panel centre
        const double Half   = 0.5 * (High - Low);                                            // [-] - Signed half-width
        const double Middle = Function(Centre);                                              // [-] - f(centre)
        double Kronrod = KronrodWeights[7] * Middle;                                         // [-] - 15-point sum
        double Gauss   = GaussWeights[3] * Middle;                                           // [-] - 7-point sum
        for (int Index = 0; Index < 7; ++Index)
        {
            const double Offset = Half * KronrodAbscissae[Index];                            // [-] - Abscissa offset
            const double Pair   = Function(Centre - Offset) + Function(Centre + Offset);     // [-] - Symmetric pair
            Kronrod += KronrodWeights[Index] * Pair;
            if (Index % 2 == 1) Gauss += GaussWeights[Index / 2] * Pair;
        }
        Kronrod *= Half;
        Gauss   *= Half;
        // ⚠️ Near a surface the panel values reach ≈ 2/distance; an absolute tolerance alone is unreachable in double
        //    precision there, so each panel also accepts a relative error of RelativeFloor·|K|.
        if (std::fabs(Kronrod - Gauss) <= std::max(Tolerance, RelativeFloor * std::fabs(Kronrod)) || Depth <= 0) return Kronrod;
        const double Next = std::max(0.5 * Tolerance, FloorTolerance);                       // [-] - Half-panel tolerance
        return IntegrateAdaptive(Low, Centre, Function, Next, Depth - 1) + IntegrateAdaptive(Centre, High, Function, Next, Depth - 1);
    }

    // Integral over [Low, High] split at the given interior breakpoints.
    template <typename Integrand>
    [[nodiscard]] double IntegrateBroken(double Low, double High, std::vector<double> Breaks, const Integrand& Function, double Tolerance, int Depth)
    {
        const double Lower = std::min(Low, High);                                            // [-] - Ascending range
        const double Upper = std::max(Low, High);                                            // [-] - Ascending range
        std::vector<double> Points = { Lower };
        std::sort(Breaks.begin(), Breaks.end());
        for (double Break : Breaks)
        {
            if (Break > Points.back() + 1.0e-14 * (1.0 + std::fabs(Break)) && Break < Upper - 1.0e-14 * (1.0 + std::fabs(Break))) Points.push_back(Break);
        }
        Points.push_back(Upper);
        double Total = 0.0;                                                                  // [-] - Ascending integral
        for (size_t Index = 0; Index + 1 < Points.size(); ++Index)
        {
            Total += IntegrateAdaptive(Points[Index], Points[Index + 1], Function, Tolerance, Depth);
        }
        return High >= Low ? Total : -Total;
    }

    // Images of a periodic coordinate in [Low, High].
    [[nodiscard]] std::vector<double> CollectImages(double Value, double Low, double High, double Period)
    {
        if (Period <= 0.0) return { Value };
        std::vector<double> Images;
        for (double Image = Value - Period * std::ceil((Value - Low) / Period); Image <= High; Image += Period)
        {
            if (Image >= Low) Images.push_back(Image);
        }
        return Images;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

BodyContainment PrepareFaceContainment(const ModelPartition& Partition, const std::vector<FaceToken>& Faces)
{
    BodyContainment Containment;
    Containment.Faces.reserve(Faces.size());
    for (FaceToken Face : Faces)
    {
        FaceDomain Domain = DescribeFaceDomain(Partition, Face);
        Containment.BoundsLow  = { std::min(Containment.BoundsLow.X, Domain.BoundsLow.X), std::min(Containment.BoundsLow.Y, Domain.BoundsLow.Y), std::min(Containment.BoundsLow.Z, Domain.BoundsLow.Z) };
        Containment.BoundsHigh = { std::max(Containment.BoundsHigh.X, Domain.BoundsHigh.X), std::max(Containment.BoundsHigh.Y, Domain.BoundsHigh.Y), std::max(Containment.BoundsHigh.Z, Domain.BoundsHigh.Z) };
        Containment.Faces.push_back(std::move(Domain));
    }
    return Containment;
}

BodyContainment PrepareBodyContainment(const ModelPartition& Partition, BodyToken Body)
{
    const BodyInventory Inventory = Partition.CollectInventory(Body);
    BodyContainment     Containment = PrepareFaceContainment(Partition, Inventory.Faces);
    if (Partition.Bodies.Authenticate(Body) && Partition.Bodies.Resolve(Body).Category == BodyCategory::Solid && !Inventory.Faces.empty())
    {
        bool Closed = true;
        for (EdgeToken Edge : Inventory.Edges)
        {
            const FinToken First = Partition.Edges.Resolve(Edge).RadialFin;
            if (!First.PresentCondition()) { Closed = false; break; }
            const FinToken Second = Partition.Fins.Resolve(First).RadialSuccessor;
            if (!Second.PresentCondition() || Second == First || !(Partition.Fins.Resolve(Second).RadialSuccessor == First) ||
                Partition.Fins.Resolve(First).Sense == Partition.Fins.Resolve(Second).Sense)
            {
                Closed = false;
                break;
            }
        }
        Containment.ClosedCondition = Closed;
    }
    return Containment;
}

double MeasureFaceSolidAngle(const FaceDomain& Domain, Vector3 Position, const WindingSettings& Settings, WindingTally& Tally)
{
    const PlaneSurface* Flat       = std::get_if<PlaneSurface>(&Domain.Surface);
    const double        Reference  = Domain.Window.Low.X;                                    // [-] - 𝑢ref (inside the window)
    const Vector3       Clamped    = { std::clamp(Position.X, Domain.BoundsLow.X, Domain.BoundsHigh.X),
                                       std::clamp(Position.Y, Domain.BoundsLow.Y, Domain.BoundsHigh.Y),
                                       std::clamp(Position.Z, Domain.BoundsLow.Z, Domain.BoundsHigh.Z) };
    const double        BoxGap     = Distance(Position, Clamped);                            // [len] - Distance to the face box
    const double        BoxSpan    = Length(Domain.BoundsHigh - Domain.BoundsLow);           // [len] - Face box diagonal
    const bool          FarCurved  = Flat == nullptr && BoxGap > 1.5 * std::max(BoxSpan, Settings.Precision);
    std::vector<double> FootU, FootV;
    if (!FarCurved && Flat == nullptr)
    {
        const SurfacePeriodicity Period = ResolvePeriodicity(Domain.Surface);
        const Vector2            Centre = (Domain.Window.Low + Domain.Window.High) * 0.5;   // [-] - Projection reference
        const Vector2            Foot   = ProjectOntoSurface(Domain.Surface, Position, Centre);
        const double             SpanV  = Domain.Window.High.Y - Domain.Window.Low.Y;        // [-] - Window height
        FootU = CollectImages(Foot.X, Domain.Window.Low.X - 1.0, Domain.Window.High.X + 1.0, Period.PeriodU);
        FootV = CollectImages(Foot.Y, Domain.Window.Low.Y - SpanV, Domain.Window.High.Y + SpanV, Period.PeriodV);
    }
    else if (Flat != nullptr)
    {
        const double FootY = Dot(Position - Flat->Frame.Origin, Flat->Frame.YAxis);
        const double SpanV = Domain.Window.High.Y - Domain.Window.Low.Y;
        if (FootY >= Domain.Window.Low.Y - SpanV && FootY <= Domain.Window.High.Y + SpanV) FootV.push_back(FootY);
    }

    // Solid-angle density on the chart.
    const auto Density = [&](double ParameterU, double ParameterV)
    {
        ++Tally.Evaluations;
        const SurfaceSample Sample  = EvaluateSurface(Domain.Surface, { ParameterU, ParameterV });
        const Vector3       Offset  = Sample.Position - Position;                            // [len] - S − P
        const double        Radius  = Length(Offset);                                        // [len] - |S − P|
        if (!(Radius > 1.0e-300)) return 0.0;
        return Dot(Offset, Cross(Sample.DerivativeU, Sample.DerivativeV)) / (Radius * Radius * Radius);
    };
    // 📝 Plane chart P = O + 𝑢X + 𝑣Y: S − P = (𝑢 − 𝑢f)X + (𝑣 − 𝑣f)Y + h·n with n = X × Y, so the density is h / r³ with
    //    r² = (𝑢 − 𝑢f)² + c², c² = (𝑣 − 𝑣f)² + h², and its 𝑢-primitive is closed form: h·t / (c²·√(t² + c²)), t = 𝑢 − 𝑢f.
    //    Same integrand as the adaptive rule (which it replaces on planes), exact instead of to InnerTolerance.
    const Vector3       Normal = Flat != nullptr ? Cross(Flat->Frame.XAxis, Flat->Frame.YAxis) : Vector3{};
    const Vector3       Lever  = Flat != nullptr ? Position - Flat->Frame.Origin : Vector3{};  // [len] - P − O
    const double        Height = Flat != nullptr ? -Dot(Lever, Normal) : 0.0;                // [len] - h = (O − P)·n
    const double        FootX  = Flat != nullptr ? Dot(Lever, Flat->Frame.XAxis) : 0.0;      // [len] - 𝑢f
    const double        FootY  = Flat != nullptr ? Dot(Lever, Flat->Frame.YAxis) : 0.0;      // [len] - 𝑣f
    const auto FlatPrimitive = [&](double ParameterU, double ParameterV)
    {
        ++Tally.Evaluations;
        const double Across = ParameterV - FootY;                                            // [len] - 𝑣 − 𝑣f
        const double Square = Across * Across + Height * Height;                             // [len²] - c²
        if (Height == 0.0 || !(Square > 1.0e-300)) return 0.0;
        const auto Antiderivative = [&](double Along) { return Height * Along / (Square * std::sqrt(Along * Along + Square)); };
        return Antiderivative(ParameterU - FootX) - Antiderivative(Reference - FootX);
    };
    // G(𝑢, 𝑣) = ∫_{𝑢ref}^{𝑢} density ds, split at the foot images.
    const auto Primitive = [&](double ParameterU, double ParameterV)
    {
        if (Flat != nullptr) return FlatPrimitive(ParameterU, ParameterV);
        return IntegrateBroken(Reference, ParameterU, FootU, [&](double Abscissa) { return Density(Abscissa, ParameterV); },
                               Settings.InnerTolerance, Settings.DepthLimit);
    };

    double Total = 0.0;                                                                      // [sr] - ∮ G d𝑣
    for (const auto& Loop : Domain.Loops)
    {
        for (const DomainSegment& Segment : Loop)
        {
            if (Segment.Virtual) continue;
            const bool Straight = std::holds_alternative<ParameterLine>(Segment.Curve);
            if (Straight && std::get<ParameterLine>(Segment.Curve).Direction.Y == 0.0) continue;

            // Breakpoints: initial panels plus every τ where 𝑣(τ) crosses a foot image (the outer peak).
            const double Span   = Segment.End - Segment.Start;                               // [-] - Signed τ span
            const int    Panels = Straight ? LinePanels : std::max(2, static_cast<int>(std::ceil(std::fabs(Span) / PanelAngle)));
            std::vector<double> Breaks;
            std::vector<double> Abscissae(Panels * 4 + 1);
            std::vector<double> Heights(Abscissae.size());
            for (size_t Index = 0; Index < Abscissae.size(); ++Index)
            {
                Abscissae[Index] = Segment.Start + Span * static_cast<double>(Index) / static_cast<double>(Abscissae.size() - 1);
                Heights[Index]   = EvaluateParameterCurve(Segment.Curve, Abscissae[Index]).Position.Y;
                if (Index % 4 == 0 && Index > 0 && Index + 1 < Abscissae.size()) Breaks.push_back(Abscissae[Index]);
            }
            for (double Level : FootV)
            {
                for (size_t Index = 0; Index + 1 < Abscissae.size(); ++Index)
                {
                    double LowerT = Abscissae[Index], UpperT = Abscissae[Index + 1];         // [-] - Bracket
                    double LowerH = Heights[Index] - Level, UpperH = Heights[Index + 1] - Level;
                    if (LowerH == 0.0) { Breaks.push_back(LowerT); continue; }
                    if ((LowerH < 0.0) == (UpperH < 0.0)) continue;
                    for (int Step = 0; Step < 60; ++Step)
                    {
                        const double MiddleT = 0.5 * (LowerT + UpperT);                      // [-] - Bisection
                        const double MiddleH = EvaluateParameterCurve(Segment.Curve, MiddleT).Position.Y - Level;
                        if ((MiddleH < 0.0) == (LowerH < 0.0)) { LowerT = MiddleT; LowerH = MiddleH; } else { UpperT = MiddleT; }
                    }
                    Breaks.push_back(0.5 * (LowerT + UpperT));
                }
            }
            Total += IntegrateBroken(Segment.Start, Segment.End, Breaks, [&](double τ)
            {
                const ParameterSample Sample = EvaluateParameterCurve(Segment.Curve, τ);
                if (Sample.Derivative.Y == 0.0) return 0.0;
                return Primitive(Sample.Position.X, Sample.Position.Y) * Sample.Derivative.Y;
            }, Settings.OuterTolerance, Settings.DepthLimit);
        }
    }
    return Total;
}

double MeasureWindingNumber(const BodyContainment& Containment, Vector3 Position, const WindingSettings& Settings, WindingTally& Tally)
{
    double Total = 0.0;                                                                      // [sr] - Σ Ω_F
    for (const FaceDomain& Domain : Containment.Faces) Total += MeasureFaceSolidAngle(Domain, Position, Settings, Tally);
    return Total / FullSphere;
}

SolidPointClass ClassifySolidPoint(const BodyContainment& Containment, Vector3 Position, const WindingSettings& Settings)
{
    SolidPointClass Outcome;
    const double Slack = std::max(Settings.Precision, Settings.NearDistance);                // [len] - Bounds slack
    if (Containment.ClosedCondition &&
        (Position.X < Containment.BoundsLow.X - Slack || Position.X > Containment.BoundsHigh.X + Slack ||
         Position.Y < Containment.BoundsLow.Y - Slack || Position.Y > Containment.BoundsHigh.Y + Slack ||
         Position.Z < Containment.BoundsLow.Z - Slack || Position.Z > Containment.BoundsHigh.Z + Slack))
    {
        return Outcome;
    }
    double NearestFace  = std::numeric_limits<double>::infinity();                           // [len] - Closest interior foot
    double NearestEdge  = std::numeric_limits<double>::infinity();                           // [len] - Closest boundary curve
    double NearestSide  = 0.0;                                                               // [len] - (P − S)·N at that foot
    for (size_t Index = 0; Index < Containment.Faces.size(); ++Index)
    {
        const FaceDomain& Domain = Containment.Faces[Index];
        if (Position.X < Domain.BoundsLow.X - Slack || Position.X > Domain.BoundsHigh.X + Slack) continue;
        if (Position.Y < Domain.BoundsLow.Y - Slack || Position.Y > Domain.BoundsHigh.Y + Slack) continue;
        if (Position.Z < Domain.BoundsLow.Z - Slack || Position.Z > Domain.BoundsHigh.Z + Slack) continue;
        const FacePositionClass Foot = ClassifyFacePosition(Domain, Position, Settings.Precision);
        if (Foot.Membership == PointMembership::Boundary || (Foot.Membership == PointMembership::Inside && Foot.SurfaceDistance <= Settings.Precision))
        {
            Outcome.Membership   = PointMembership::Boundary;
            Outcome.Method       = ContainmentMethod::Proximity;
            Outcome.Winding      = std::nan("");
            Outcome.BoundaryFace = static_cast<int>(Index);
            return Outcome;
        }
        for (const DomainBoundaryCurve& Edge : Domain.BoundaryCurves)
        {
            const double τ = ProjectOntoCurve(Edge.Curve, Position, Edge.Low);              // [-] - Closest parameter
            double Gap = std::min(Distance(EvaluateCurve(Edge.Curve, Edge.Low).Position, Position), Distance(EvaluateCurve(Edge.Curve, Edge.High).Position, Position));
            if (τ >= Edge.Low && τ <= Edge.High) Gap = std::min(Gap, Distance(EvaluateCurve(Edge.Curve, τ).Position, Position));
            NearestEdge = std::min(NearestEdge, Gap);
        }
        for (const Vector3& Point : Domain.BoundaryPoints) NearestEdge = std::min(NearestEdge, Distance(Point, Position));
        if (Foot.SurfaceDistance < NearestFace && Foot.SurfaceDistance <= Settings.NearDistance
            && ClassifyParameterPoint(Domain, Foot.Parameter) == PointMembership::Inside)
        {
            const Vector3 Normal = EvaluateSurfaceNormal(Domain.Surface, Foot.Parameter);
            NearestFace = Foot.SurfaceDistance;
            NearestSide = Dot(Position - EvaluateSurface(Domain.Surface, Foot.Parameter).Position, Domain.Sense == Orientation::Forward ? Normal : Normal * -1.0);
        }
    }
    if (NearestFace < 0.5 * NearestEdge)
    {
        Outcome.Method          = ContainmentMethod::NearestNormal;
        Outcome.Membership      = NearestSide < 0.0 ? PointMembership::Inside : PointMembership::Outside;
        Outcome.Winding         = Outcome.Membership == PointMembership::Inside ? 1.0 : 0.0;
        Outcome.IntegerResidual = 0.0;
        return Outcome;
    }
    WindingTally Tally;
    Outcome.Winding         = MeasureWindingNumber(Containment, Position, Settings, Tally);
    Outcome.IntegerResidual = std::fabs(Outcome.Winding - std::round(Outcome.Winding));
    Outcome.Membership      = std::fabs(Outcome.Winding) > 0.5 ? PointMembership::Inside : PointMembership::Outside;
    Outcome.Evaluations     = Tally.Evaluations;
    return Outcome;
}

SolidPointClass ClassifySolidPointScreened(const BodyContainment& Containment, Vector3 Position, const WindingSettings& Settings)
{
    constexpr double ScreenResidual = 1.0e-4;                                                // [-]  - Coarse winding accepted within this of an integer
    constexpr double ScreenOuter    = 1.0e-5;                                                // [sr] - Coarse Kronrod tolerance along loops
    constexpr double ScreenInner    = 1.0e-6;                                                // [-]  - Coarse Kronrod tolerance along 𝑢
    WindingSettings Screen = Settings;
    Screen.OuterTolerance  = std::max(Screen.OuterTolerance, ScreenOuter);
    Screen.InnerTolerance  = std::max(Screen.InnerTolerance, ScreenInner);
    SolidPointClass Class  = ClassifySolidPoint(Containment, Position, Screen);
    if (Class.Method == ContainmentMethod::Winding && Class.IntegerResidual > ScreenResidual)
    {
        const int64_t Screened = Class.Evaluations;
        Class = ClassifySolidPoint(Containment, Position, Settings);
        Class.Evaluations += Screened;
    }
    return Class;
}

const char* ContainmentMethodLabel(ContainmentMethod Method) noexcept
{
    constexpr const char* Labels[] = { "Winding", "NearestNormal", "Proximity" };
    return Labels[static_cast<int>(Method)];
}

const char* PointMembershipLabel(PointMembership Membership) noexcept
{
    constexpr const char* Labels[] = { "Outside", "Inside", "Boundary" };
    return Labels[static_cast<int>(Membership)];
}

} // namespace Frontier::GeometrySpace::Parametric
