/*====================================================================================================================================
                                                       CURVEGEOMETRY.CPP
====================================================================================================================================*/
// 🧩 Analytic 3D curves (line, circle, ellipse) and 2D parameter-space curves (SP-curves on faces) with exact inversion

#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"

#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/RollingBallCarrier.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    3D CURVES
//------------------------------------------------------------------------------------------------------------------------

std::optional<ConicForm> ResolveConicForm(const CurveGeometry& Curve) noexcept
{
    if (const auto* Arc = std::get_if<CircularArc>(&Curve))
    {
        return ConicForm{ Arc->Frame.Origin, Arc->Frame.XAxis * Arc->Radius, Arc->Frame.YAxis * Arc->Radius, Arc->Frame.ZAxis };
    }
    if (const auto* Ellipse = std::get_if<EllipticalArc>(&Curve))
    {
        return ConicForm{ Ellipse->Frame.Origin, Ellipse->Frame.XAxis * Ellipse->MajorRadius, Ellipse->Frame.YAxis * Ellipse->MinorRadius, Ellipse->Frame.ZAxis };
    }
    return std::nullopt;
}

CurveSample EvaluateCurve(const CurveGeometry& Curve, double τ) noexcept
{
    if (const auto* Line = std::get_if<StraightLine>(&Curve))
    {
        return { Line->Origin + Line->Direction * τ, Line->Direction };
    }
    if (const auto* Traced = std::get_if<IntersectionCurve>(&Curve)) return EvaluateIntersectionCurve(*Traced->Trace, τ);
    if (const auto* Contact = std::get_if<ContactCurve>(&Curve)) return EvaluateContactCurve(*Contact->Definition, τ);
    if (const auto* Spline = std::get_if<NurbsCurve>(&Curve)) return EvaluateNurbsCurve(*Spline->Definition, τ);
    const ConicForm Conic  = *ResolveConicForm(Curve);
    const double    Cosine = std::cos(τ);                                                    // [-] - cos τ
    const double    Sine   = std::sin(τ);                                                    // [-] - sin τ
    return { Conic.Centre + Conic.CosineAxis * Cosine + Conic.SineAxis * Sine, Conic.SineAxis * Cosine - Conic.CosineAxis * Sine };
}

double ProjectOntoCurve(const CurveGeometry& Curve, Vector3 Position, double IntervalLow) noexcept
{
    if (const auto* Line = std::get_if<StraightLine>(&Curve))
    {
        return Dot(Position - Line->Origin, Line->Direction);
    }
    if (const auto* Traced = std::get_if<IntersectionCurve>(&Curve)) return ProjectOntoIntersectionCurve(*Traced->Trace, Position, IntervalLow);
    if (const auto* Contact = std::get_if<ContactCurve>(&Curve)) return ProjectOntoContactCurve(*Contact->Definition, Position, IntervalLow);
    if (const auto* Spline = std::get_if<NurbsCurve>(&Curve)) return ProjectOntoNurbsCurve(*Spline->Definition, Position, IntervalLow);
    const ConicForm Conic  = *ResolveConicForm(Curve);
    const Vector3   Offset = Position - Conic.Centre;                                        // [len] - From the centre
    const double    Major  = Length(Conic.CosineAxis);                                       // [len] - a
    const double    Minor  = Length(Conic.SineAxis);                                         // [len] - b
    const double    Abscissa = Dot(Offset, Conic.CosineAxis) / Major;                        // [len] - x in the conic plane
    const double    Ordinate = Dot(Offset, Conic.SineAxis) / Minor;                          // [len] - y in the conic plane
    double τ = std::atan2(Ordinate * Major, Abscissa * Minor);                               // [rad] - Exact for a circle
    if (std::fabs(Major - Minor) > 1.0e-14 * Major)
    {
        // 📝 Ellipse foot point: g(τ) = (a² − b²)·sin τ·cos τ − x·a·sin τ + y·b·cos τ = 0, Newton from the affine angle;
        //    the affine seed lies in the correct quadrant, where g is monotone between its roots.
        const double Excess = Major * Major - Minor * Minor;                                 // [len²] - a² − b²
        for (int Iteration = 0; Iteration < 40; ++Iteration)
        {
            const double Cosine = std::cos(τ), Sine = std::sin(τ);                           // [-] - cos τ, sin τ
            const double Value  = Excess * Sine * Cosine - Abscissa * Major * Sine + Ordinate * Minor * Cosine;
            const double Slope  = Excess * (Cosine * Cosine - Sine * Sine) - Abscissa * Major * Cosine - Ordinate * Minor * Sine;
            if (std::fabs(Slope) < 1.0e-300) break;
            const double Step = Value / Slope;                                               // [rad] - Newton correction
            τ -= std::clamp(Step, -0.5, 0.5);
            if (std::fabs(Step) < 1.0e-15) break;
        }
    }
    constexpr double Period = 2.0 * std::numbers::pi;                                        // [rad] - Full turn
    // 📝 Fold into [IntervalLow, IntervalLow + 2π); a hair below the low end maps to the low end, not to the high end.
    τ = IntervalLow + std::fmod(std::fmod(τ - IntervalLow, Period) + Period, Period);
    if (τ - IntervalLow > Period - 1.0e-12) τ = IntervalLow;
    return τ;
}

bool PeriodicCondition(const CurveGeometry& Curve) noexcept
{
    if (const auto* Traced = std::get_if<IntersectionCurve>(&Curve)) return IntersectionCurveClosedCondition(*Traced->Trace);
    if (const auto* Contact = std::get_if<ContactCurve>(&Curve)) return Contact->Definition->Period > 0.0;
    if (const auto* Spline = std::get_if<NurbsCurve>(&Curve)) return Spline->Definition->Period > 0.0;
    return !std::holds_alternative<StraightLine>(Curve);
}

const char* CurveCategoryLabel(const CurveGeometry& Curve) noexcept
{
    if (std::holds_alternative<StraightLine>(Curve))      return "Line";
    if (std::holds_alternative<IntersectionCurve>(Curve)) return "Intersection";
    if (std::holds_alternative<ContactCurve>(Curve))      return "Contact";
    if (std::holds_alternative<NurbsCurve>(Curve))        return "Nurbs";
    return std::holds_alternative<CircularArc>(Curve) ? "Circle" : "Ellipse";
}

double MeasureCurveBulge(const CurveGeometry& Curve, double Low, double High, int Pieces) noexcept
{
    if (const auto* Traced = std::get_if<IntersectionCurve>(&Curve)) return MeasureIntersectionCurveBulge(*Traced->Trace, Low, High, Pieces);
    if (const auto* Contact = std::get_if<ContactCurve>(&Curve)) return MeasureContactCurveBulge(*Contact->Definition, Low, High, Pieces);
    if (const auto* Spline = std::get_if<NurbsCurve>(&Curve)) return MeasureNurbsCurveBulge(*Spline->Definition, Low, High, Pieces);
    const std::optional<ConicForm> Conic = ResolveConicForm(Curve);
    if (!Conic) return 0.0;
    const double Largest = std::max(Length(Conic->CosineAxis), Length(Conic->SineAxis));    // [len] - Largest semi-axis
    const double Smallest = std::min(Length(Conic->CosineAxis), Length(Conic->SineAxis));   // [len] - Smallest semi-axis
    // 📝 Curvature ≤ a/b² on an ellipse; the sagitta of a piece of sweep Δτ is ≤ κ_max·(a·Δτ)²/8.
    const double Sweep = (High - Low) / std::max(Pieces, 1);                                // [rad] - Δτ per piece
    if (Largest - Smallest <= 1.0e-14 * Largest) return Largest * (1.0 - std::cos(0.5 * Sweep));
    return Largest / (Smallest * Smallest) * (Largest * Sweep) * (Largest * Sweep) / 8.0;
}

double MeasureParameterScale(const CurveGeometry& Curve) noexcept
{
    if (const auto* Traced = std::get_if<IntersectionCurve>(&Curve)) return MeasureIntersectionCurveScale(*Traced->Trace);
    if (const auto* Contact = std::get_if<ContactCurve>(&Curve)) return MeasureParameterScale(Contact->Definition->Spine);
    if (const auto* Spline = std::get_if<NurbsCurve>(&Curve)) return MeasureNurbsCurveScale(*Spline->Definition);
    const std::optional<ConicForm> Conic = ResolveConicForm(Curve);
    return Conic ? std::max(Length(Conic->CosineAxis), Length(Conic->SineAxis)) : 1.0;
}

std::vector<double> CollectCurveBreakpoints(const CurveGeometry& Curve, double Low, double High)
{
    if (const auto* Traced = std::get_if<IntersectionCurve>(&Curve)) return CollectIntersectionCurveKnots(*Traced->Trace, std::min(Low, High), std::max(Low, High));
    if (const auto* Contact = std::get_if<ContactCurve>(&Curve)) return CollectContactCurveBreakpoints(*Contact->Definition, Low, High);
    if (const auto* Spline = std::get_if<NurbsCurve>(&Curve)) return CollectNurbsCurveKnots(*Spline->Definition, Low, High);
    return {};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    2D PARAMETER-SPACE CURVES
//------------------------------------------------------------------------------------------------------------------------

ParameterSample EvaluateParameterCurve(const ParameterCurve& Curve, double τ) noexcept
{
    if (const auto* Line = std::get_if<ParameterLine>(&Curve))
    {
        return { Line->Origin + Line->Direction * τ, Line->Direction };
    }
    if (const auto* Projected = std::get_if<ParameterProjection>(&Curve))
    {
        return EvaluateProjectedParameterCurve(*Projected->Projection, τ);
    }
    const auto&  Ellipse = std::get<ParameterEllipse>(Curve);
    const double Cosine  = std::cos(τ);                                                      // [-] - cos τ
    const double Sine    = std::sin(τ);                                                      // [-] - sin τ
    return {
        Ellipse.Centre + (Ellipse.FirstAxis * Cosine + Ellipse.SecondAxis * Sine) * Ellipse.Radius,
        (Ellipse.FirstAxis * -Sine + Ellipse.SecondAxis * Cosine) * Ellipse.Radius,
    };
}

} // namespace Frontier::GeometrySpace::Parametric
