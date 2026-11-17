/*====================================================================================================================================
                                                     ISOPARAMETERCURVE.CPP
====================================================================================================================================*/
// 🧩 Iso-parameter lines and circles per surface type

#include "Engine/GeometrySpace/Parametric/Intersection/IsoParameterCurve.h"
#include "Engine/GeometrySpace/Parametric/Geometry/ParallelCarrier.h"

#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/RollingBallCarrier.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double DegenerateRadius = 1.0e-14;                                             // [len] - Circle collapses below

    [[nodiscard]] Vector3 Radial(const OrthonormalFrame& Frame, double 𝑢) noexcept
    {
        return Frame.XAxis * std::cos(𝑢) + Frame.YAxis * std::sin(𝑢);
    }

    // 📝 Latitude circle P = Centre + ρ·(cos 𝑢·X + sin 𝑢·Y), τ = 𝑢. A negative ρ is the same circle turned by π:
    //    flipping X and Y keeps Z = X × Y and keeps τ = 𝑢.
    [[nodiscard]] IsoParameterCurve LatitudeCircle(const OrthonormalFrame& Frame, Vector3 Centre, double ρ) noexcept
    {
        IsoParameterCurve Iso;
        const double      Sign = ρ < 0.0 ? -1.0 : 1.0;                                       // [-] - Orientation of the frame
        Iso.Curve               = CircularArc{ { Centre, Frame.XAxis * Sign, Frame.YAxis * Sign, Frame.ZAxis }, std::fabs(ρ) };
        Iso.DegenerateCondition = std::fabs(ρ) <= DegenerateRadius;
        return Iso;
    }

    // 📝 Meridian circle in the half-plane of azimuth 𝑢: P = Centre + r·(cos 𝑣·radial + sin 𝑣·Z), τ = 𝑣.
    [[nodiscard]] IsoParameterCurve MeridianCircle(const OrthonormalFrame& Frame, double 𝑢, Vector3 Centre, double Radius) noexcept
    {
        const Vector3 Direction = Radial(Frame, 𝑢);                                          // [-] - In-plane X axis
        IsoParameterCurve Iso;
        Iso.Curve = CircularArc{ { Centre, Direction, Frame.ZAxis, Cross(Direction, Frame.ZAxis) }, Radius };
        return Iso;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

IsoParameterCurve ExtractIsoParameterCurve(const SurfaceGeometry& Surface, IsoDirection Direction, double Value) noexcept
{
    if (const auto* Carrier = std::get_if<RollingBallSurface>(&Surface))
    {
        // 📝 𝑢 = const is the exact cross-section circle (τ = 𝑣); 𝑣 = const has no exact carrier and is skipped.
        IsoParameterCurve Iso;
        if (Direction == IsoDirection::FixedU && Carrier->Definition->Section == RollingBallSection::Circle) Iso.Curve = ExtractRollingBallSection(*Carrier, Value);
        else Iso.DegenerateCondition = true;
        return Iso;
    }
    if (const auto* Offset = std::get_if<ParallelSurface>(&Surface))
    {
        // 📝 An offset's iso-curve is not rational: a dense polyline through exact offset points over the basis domain
        //    (τ = running parameter), flagged approximate — good for seeding, never for geometry.
        const NurbsSurfaceDefinition& Basis = *Offset->Definition->Basis.Definition;
        const NurbsDomain Run = MeasureNurbsSurfaceDomain(Basis, Direction != IsoDirection::FixedU);
        IsoParameterCurve Iso;
        Iso.Curve                = ExtractParallelIsoPolyline(*Offset, Direction == IsoDirection::FixedU, Value, Run.Low, Run.High);
        Iso.ApproximateCondition = true;
        return Iso;
    }
    if (const auto* Spline = std::get_if<NurbsSurface>(&Surface))
    {
        // 📝 Exact NURBS iso-curve (τ = running parameter); a collapsed pole row (sphere pole, cone apex) is a point.
        IsoParameterCurve Iso;
        const NurbsCurve  Curve = ExtractNurbsIsoCurve(*Spline, Direction == IsoDirection::FixedU, Value);
        double Spread = 0.0;
        for (const Vector3& Pole : Curve.Definition->Poles) Spread = std::max(Spread, Distance(Pole, Curve.Definition->Poles.front()));
        Iso.Curve               = Curve;
        Iso.DegenerateCondition = Spread <= 1.0e-12 * Spline->Definition->Extent;
        return Iso;
    }
    const bool FixedU = Direction == IsoDirection::FixedU;                                   // [-] - 𝑣 runs along the curve
    if (const auto* Plane = std::get_if<PlaneSurface>(&Surface))
    {
        const OrthonormalFrame& Frame = Plane->Frame;
        IsoParameterCurve Iso;
        Iso.Curve = FixedU ? StraightLine{ Frame.Origin + Frame.XAxis * Value, Frame.YAxis } : StraightLine{ Frame.Origin + Frame.YAxis * Value, Frame.XAxis };
        return Iso;
    }
    if (const auto* Cylinder = std::get_if<CylinderSurface>(&Surface))
    {
        const OrthonormalFrame& Frame = Cylinder->Frame;
        if (FixedU)
        {
            IsoParameterCurve Iso;
            Iso.Curve = StraightLine{ Frame.Origin + Radial(Frame, Value) * Cylinder->Radius, Frame.ZAxis };
            return Iso;
        }
        return LatitudeCircle(Frame, Frame.Origin + Frame.ZAxis * Value, Cylinder->Radius);
    }
    if (const auto* Cone = std::get_if<ConeSurface>(&Surface))
    {
        const OrthonormalFrame& Frame = Cone->Frame;
        const double            s     = Cone->RadialSlope;                                   // [-] - dr/d𝑣
        if (FixedU)
        {
            // 📝 Generator O + r₀·radial + 𝑣·(s·radial + Z); unit direction needs τ = 𝑣·√(1 + s²).
            const Vector3     Direction = Radial(Frame, Value);
            IsoParameterCurve Iso;
            Iso.Curve = StraightLine{ Frame.Origin + Direction * Cone->OriginRadius, Normalize(Direction * s + Frame.ZAxis) };
            Iso.Scale = std::sqrt(1.0 + s * s);
            return Iso;
        }
        return LatitudeCircle(Frame, Frame.Origin + Frame.ZAxis * Value, Cone->OriginRadius + s * Value);
    }
    if (const auto* Sphere = std::get_if<SphereSurface>(&Surface))
    {
        const OrthonormalFrame& Frame = Sphere->Frame;
        if (FixedU) return MeridianCircle(Frame, Value, Frame.Origin, Sphere->Radius);
        return LatitudeCircle(Frame, Frame.Origin + Frame.ZAxis * (Sphere->Radius * std::sin(Value)), Sphere->Radius * std::cos(Value));
    }
    const auto&             Torus = std::get<TorusSurface>(Surface);
    const OrthonormalFrame& Frame = Torus.Frame;
    if (FixedU) return MeridianCircle(Frame, Value, Frame.Origin + Radial(Frame, Value) * Torus.MajorRadius, Torus.MinorRadius);
    return LatitudeCircle(Frame, Frame.Origin + Frame.ZAxis * (Torus.MinorRadius * std::sin(Value)), Torus.MajorRadius + Torus.MinorRadius * std::cos(Value));
}

} // namespace Frontier::GeometrySpace::Parametric
