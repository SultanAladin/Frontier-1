/*====================================================================================================================================
                                                       PROJECTEDCURVE.CPP
====================================================================================================================================*/
// 🧩 SP-curves for section curves: exact line / ellipse when the chart image is affine, exact projection otherwise

#include "Engine/GeometrySpace/Parametric/Geometry/ProjectedCurve.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace Frontier::GeometrySpace::Parametric
{

namespace
{
    constexpr int    UnwrapIntervals = 64;                                                   // [-] - Table resolution
    constexpr double AffineTolerance = 1.0e-12;                                              // [-] - Relative affine-fit test

    [[nodiscard]] double FoldInto(double Value, double Low, double Period) noexcept
    {
        if (Period <= 0.0) return Value;
        return Value - Period * std::floor((Value - Low) / Period);
    }

    [[nodiscard]] Vector2 InterpolateTable(const ProjectedParameterCurve& Curve, double τ) noexcept
    {
        const double Span     = Curve.High - Curve.Low;                                      // [-] - Table width
        const double Position = std::clamp((τ - Curve.Low) / Span, 0.0, 1.0) * UnwrapIntervals;
        const int    Index    = std::min(static_cast<int>(Position), UnwrapIntervals - 1);   // [-] - Lower table index
        const double Fraction = Position - Index;                                            // [-] - Within the interval
        return Curve.Unwrap[Index] * (1.0 - Fraction) + Curve.Unwrap[Index + 1] * Fraction;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

ParameterSample EvaluateProjectedParameterCurve(const ProjectedParameterCurve& Curve, double τ) noexcept
{
    const CurveSample   Point     = EvaluateCurve(Curve.Curve, τ);
    const Vector2       Parameter = ProjectOntoSurfaceNear(Curve.Surface, Point.Position, InterpolateTable(Curve, τ));
    const SurfaceSample Sample    = EvaluateSurface(Curve.Surface, Parameter);
    const double        Guu       = Dot(Sample.DerivativeU, Sample.DerivativeU);             // [len²] - Metric
    const double        Guv       = Dot(Sample.DerivativeU, Sample.DerivativeV);             // [len²] - Metric
    const double        Gvv       = Dot(Sample.DerivativeV, Sample.DerivativeV);             // [len²] - Metric
    const double        Determinant = Guu * Gvv - Guv * Guv;                                 // [len⁴] - Zero on singular rows
    if (!(Determinant > 1.0e-24 * (Guu + Gvv) * (Guu + Gvv))) return { Parameter, {} };
    const double Bu = Dot(Sample.DerivativeU, Point.Derivative);                             // [len²] - Jᵀ C′
    const double Bv = Dot(Sample.DerivativeV, Point.Derivative);                             // [len²] - Jᵀ C′
    return { Parameter, { (Gvv * Bu - Guv * Bv) / Determinant, (Guu * Bv - Guv * Bu) / Determinant } };
}

ParameterCurve FitSurfaceCurve(const SurfaceGeometry& Surface, const CurveGeometry& Curve, double Low, double High, const ParameterWindow& Window, std::optional<Vector2> Anchor)
{
    // ① Plane: the chart is an orthonormal frame, so lines, circles and ellipses map exactly.
    const auto* Plane = std::get_if<PlaneSurface>(&Surface);
    if (Plane && !std::holds_alternative<IntersectionCurve>(Curve) && !std::holds_alternative<ContactCurve>(Curve) && !std::holds_alternative<NurbsCurve>(Curve))
    {
        const OrthonormalBasis& Frame = Plane->Frame;
        const auto Chart = [&](Vector3 Direction) { return Vector2{ Dot(Direction, Frame.XAxis), Dot(Direction, Frame.YAxis) }; };
        if (const auto* Line = std::get_if<StraightLine>(&Curve))
        {
            return ParameterLine{ Chart(Line->Origin - Frame.Origin), Chart(Line->Direction) };
        }
        const ConicForm Conic = *ResolveConicForm(Curve);
        return ParameterEllipse{ Chart(Conic.Centre - Frame.Origin), Chart(Conic.CosineAxis), Chart(Conic.SineAxis), 1.0 };
    }

    // ② Unwrap table from the midpoint outwards; the midpoint image is folded into the window.
    auto Table = std::make_shared<ProjectedParameterCurve>();
    Table->Curve   = Curve;
    Table->Surface = Surface;
    Table->Low     = Low;
    Table->High    = High;
    Table->Unwrap.resize(UnwrapIntervals + 1);
    const SurfacePeriodicity Period = ResolvePeriodicity(Surface);
    const Vector2 Centre = (Window.Low + Window.High) * 0.5;                                // [-] - Window centre
    const auto    Abscissa = [&](int Index) { return Low + (High - Low) * Index / UnwrapIntervals; };
    const int     Middle   = UnwrapIntervals / 2;                                           // [-] - Seed index
    Vector2       Seed     = ProjectOntoSurface(Surface, EvaluateCurve(Curve, Abscissa(Middle)).Position, Centre);
    if (Anchor.has_value())
    {
        Seed.X = FoldInto(Seed.X, Anchor->X - 0.5 * Period.PeriodU, Period.PeriodU);
        Seed.Y = FoldInto(Seed.Y, Anchor->Y - 0.5 * Period.PeriodV, Period.PeriodV);
    }
    else
    {
        Seed.X = FoldInto(Seed.X, Window.Low.X, Period.PeriodU);
        Seed.Y = FoldInto(Seed.Y, Window.Low.Y, Period.PeriodV);
    }
    Table->Unwrap[Middle] = Seed;
    for (int Index = Middle + 1; Index <= UnwrapIntervals; ++Index)
    {
        Table->Unwrap[Index] = ProjectOntoSurface(Surface, EvaluateCurve(Curve, Abscissa(Index)).Position, Table->Unwrap[Index - 1]);
    }
    for (int Index = Middle - 1; Index >= 0; --Index)
    {
        Table->Unwrap[Index] = ProjectOntoSurface(Surface, EvaluateCurve(Curve, Abscissa(Index)).Position, Table->Unwrap[Index + 1]);
    }

    // ③ Affine image test (iso-lines, cone / cylinder generators): exact ParameterLine.
    const Vector2 First     = Table->Unwrap.front();                                         // [-] - q(Low)
    const Vector2 Direction = (Table->Unwrap.back() - First) * (1.0 / (High - Low));         // [-] - dq/dτ if affine
    bool AffineCondition = true;                                                             // [-] - All samples on the line
    for (int Index = 0; Index <= UnwrapIntervals && AffineCondition; ++Index)
    {
        const Vector2 Expected = First + Direction * (Abscissa(Index) - Low);               // [-] - Affine prediction
        const double  Scale    = 1.0 + Length(Expected);                                    // [-] - Relative scale
        AffineCondition = Distance(Expected, Table->Unwrap[Index]) <= AffineTolerance * Scale * 64.0;
    }
    if (AffineCondition) return ParameterLine{ First - Direction * Low, Direction };
    return ParameterProjection{ std::move(Table) };
}

double MeasureSurfaceCurveDeviation(const SurfaceGeometry& Surface, const CurveGeometry& Curve, const ParameterCurve& SurfaceCurve, double Low, double High, int Samples)
{
    double Largest = 0.0;                                                                    // [len] - Worst deviation
    for (int Index = 0; Index <= Samples; ++Index)
    {
        const double τ = Low + (High - Low) * Index / Samples;                               // [-] - Sample parameter
        const Vector3 OnSurface = EvaluateSurface(Surface, EvaluateParameterCurve(SurfaceCurve, τ).Position).Position;
        Largest = std::max(Largest, Distance(OnSurface, EvaluateCurve(Curve, τ).Position));
    }
    return Largest;
}

} // namespace Frontier::GeometrySpace::Parametric
