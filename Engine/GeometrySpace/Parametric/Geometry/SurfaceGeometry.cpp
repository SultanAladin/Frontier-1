/*====================================================================================================================================
                                                      SURFACEGEOMETRY.CPP
====================================================================================================================================*/
// 🧩 Analytic surfaces (plane, cylinder, cone, sphere, torus): evaluation, closed-form normals, closed-form inversion; dispatch to procedural and NURBS carriers

#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"

#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/ParallelCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/RollingBallCarrier.h"

#include <cmath>
#include <numbers>
#include <type_traits>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double FullTurn = 2.0 * std::numbers::pi;                                      // [rad] - 2π

    // Radial unit direction cos 𝑢·X + sin 𝑢·Y and its 𝑢-derivative.
    struct RadialDirection
    {
        Vector3 Radial  = {};                                                                // [-] - cos 𝑢·X + sin 𝑢·Y
        Vector3 Tangent = {};                                                                // [-] - −sin 𝑢·X + cos 𝑢·Y
    };

    [[nodiscard]] RadialDirection EvaluateRadial(const OrthonormalFrame& Frame, double 𝑢) noexcept
    {
        const double Cosine = std::cos(𝑢);                                                   // [-] - cos 𝑢
        const double Sine   = std::sin(𝑢);                                                   // [-] - sin 𝑢
        return { Frame.XAxis * Cosine + Frame.YAxis * Sine, Frame.XAxis * -Sine + Frame.YAxis * Cosine };
    }

    // Local coordinates of a point in a frame.
    [[nodiscard]] Vector3 LocalCoordinates(const OrthonormalFrame& Frame, Vector3 Position) noexcept
    {
        const Vector3 Offset = Position - Frame.Origin;                                      // [len] - From the origin
        return { Dot(Offset, Frame.XAxis), Dot(Offset, Frame.YAxis), Dot(Offset, Frame.ZAxis) };
    }

    // Representative of a periodic angle nearest to a reference angle.
    [[nodiscard]] double FoldNearest(double Angle, double Reference, double Period) noexcept
    {
        return Angle + Period * std::round((Reference - Angle) / Period);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SurfaceSample EvaluateSurface(const SurfaceGeometry& Surface, Vector2 Parameter) noexcept
{
    if (const auto* Carrier = std::get_if<RollingBallSurface>(&Surface)) return EvaluateRollingBall(*Carrier, Parameter);
    if (const auto* Spline = std::get_if<NurbsSurface>(&Surface)) return EvaluateNurbsSurface(*Spline, Parameter);
    if (const auto* Offset = std::get_if<ParallelSurface>(&Surface)) return EvaluateParallelSurface(*Offset, Parameter);
    const double 𝑢 = Parameter.X;                                                            // [-] - First surface parameter
    const double 𝑣 = Parameter.Y;                                                            // [-] - Second surface parameter
    if (const auto* Plane = std::get_if<PlaneSurface>(&Surface))
    {
        return { Plane->Frame.Origin + Plane->Frame.XAxis * 𝑢 + Plane->Frame.YAxis * 𝑣, Plane->Frame.XAxis, Plane->Frame.YAxis };
    }
    if (const auto* Cylinder = std::get_if<CylinderSurface>(&Surface))
    {
        const RadialDirection Direction = EvaluateRadial(Cylinder->Frame, 𝑢);
        return {
            Cylinder->Frame.Origin + Direction.Radial * Cylinder->Radius + Cylinder->Frame.ZAxis * 𝑣,
            Direction.Tangent * Cylinder->Radius,
            Cylinder->Frame.ZAxis,
        };
    }
    if (const auto* Cone = std::get_if<ConeSurface>(&Surface))
    {
        const RadialDirection Direction = EvaluateRadial(Cone->Frame, 𝑢);
        const double          ρ         = Cone->OriginRadius + 𝑣 * Cone->RadialSlope;          // [len] - Local radius
        return {
            Cone->Frame.Origin + Direction.Radial * ρ + Cone->Frame.ZAxis * 𝑣,
            Direction.Tangent * ρ,
            Direction.Radial * Cone->RadialSlope + Cone->Frame.ZAxis,
        };
    }
    if (const auto* Sphere = std::get_if<SphereSurface>(&Surface))
    {
        const RadialDirection Direction = EvaluateRadial(Sphere->Frame, 𝑢);
        const double          r         = Sphere->Radius;                                    // [len] - Radius
        return {
            Sphere->Frame.Origin + (Direction.Radial * std::cos(𝑣) + Sphere->Frame.ZAxis * std::sin(𝑣)) * r,
            Direction.Tangent * (r * std::cos(𝑣)),
            (Direction.Radial * -std::sin(𝑣) + Sphere->Frame.ZAxis * std::cos(𝑣)) * r,
        };
    }
    const auto&           Torus     = std::get<TorusSurface>(Surface);
    const RadialDirection Direction = EvaluateRadial(Torus.Frame, 𝑢);
    const double          ρ         = Torus.MajorRadius + Torus.MinorRadius * std::cos(𝑣);    // [len] - Distance from the axis
    return {
        Torus.Frame.Origin + Direction.Radial * ρ + Torus.Frame.ZAxis * (Torus.MinorRadius * std::sin(𝑣)),
        Direction.Tangent * ρ,
        (Direction.Radial * -std::sin(𝑣) + Torus.Frame.ZAxis * std::cos(𝑣)) * Torus.MinorRadius,
    };
}

Vector3 EvaluateSurfaceNormal(const SurfaceGeometry& Surface, Vector2 Parameter) noexcept
{
    if (const auto* Carrier = std::get_if<RollingBallSurface>(&Surface)) return EvaluateRollingBallNormal(*Carrier, Parameter);
    if (const auto* Spline = std::get_if<NurbsSurface>(&Surface)) return EvaluateNurbsNormal(*Spline, Parameter);
    if (const auto* Offset = std::get_if<ParallelSurface>(&Surface)) return EvaluateParallelNormal(*Offset, Parameter);
    const double 𝑢 = Parameter.X;                                                            // [-] - First surface parameter
    const double 𝑣 = Parameter.Y;                                                            // [-] - Second surface parameter
    if (const auto* Plane = std::get_if<PlaneSurface>(&Surface)) return Plane->Frame.ZAxis;
    if (const auto* Cylinder = std::get_if<CylinderSurface>(&Surface)) return EvaluateRadial(Cylinder->Frame, 𝑢).Radial;
    if (const auto* Cone = std::get_if<ConeSurface>(&Surface))
    {
        // 📝 (Radial − s·Z)/√(1 + s²) — the generator's outward normal, constant along it (valid at the apex too).
        const Vector3 Radial = EvaluateRadial(Cone->Frame, 𝑢).Radial;
        return Normalize(Radial - Cone->Frame.ZAxis * Cone->RadialSlope);
    }
    if (const auto* Sphere = std::get_if<SphereSurface>(&Surface))
    {
        return EvaluateRadial(Sphere->Frame, 𝑢).Radial * std::cos(𝑣) + Sphere->Frame.ZAxis * std::sin(𝑣);
    }
    const auto& Torus = std::get<TorusSurface>(Surface);
    return EvaluateRadial(Torus.Frame, 𝑢).Radial * std::cos(𝑣) + Torus.Frame.ZAxis * std::sin(𝑣);
}

Vector2 ProjectOntoSurfaceNear(const SurfaceGeometry& Surface, Vector3 Position, Vector2 Reference) noexcept
{
    if (const auto* Carrier = std::get_if<RollingBallSurface>(&Surface))
        return std::isfinite(Reference.X) && std::isfinite(Reference.Y) ? ProjectOntoRollingBallNear(*Carrier, Position, Reference)
                                                                        : ProjectOntoRollingBall(*Carrier, Position, Reference);
    const auto*         Offset = std::get_if<ParallelSurface>(&Surface);
    const NurbsSurface* Spline = Offset ? &Offset->Definition->Basis : std::get_if<NurbsSurface>(&Surface);
    if (Spline == nullptr || !std::isfinite(Reference.X) || !std::isfinite(Reference.Y)) return ProjectOntoSurface(Surface, Position, Reference);
    const std::optional<Vector2> Local = PolishNurbsFootLocally(*Spline, Position, Reference);
    if (!Local) return ProjectOntoSurface(Surface, Position, Reference);
    const SurfacePeriodicity Period = ResolveNurbsPeriodicity(*Spline);
    const auto Fold = [](double Value, double Near, double Cycle) { return Cycle > 0.0 ? Value + Cycle * std::round((Near - Value) / Cycle) : Value; };
    return { Fold(Local->X, Reference.X, Period.PeriodU), Fold(Local->Y, Reference.Y, Period.PeriodV) };
}

Vector2 ProjectOntoSurface(const SurfaceGeometry& Surface, Vector3 Position, Vector2 Reference) noexcept
{
    if (const auto* Carrier = std::get_if<RollingBallSurface>(&Surface)) return ProjectOntoRollingBall(*Carrier, Position, Reference);
    if (const auto* Spline = std::get_if<NurbsSurface>(&Surface)) return ProjectOntoNurbsSurface(*Spline, Position, Reference);
    if (const auto* Offset = std::get_if<ParallelSurface>(&Surface)) return ProjectOntoParallelSurface(*Offset, Position, Reference);
    if (const auto* Plane = std::get_if<PlaneSurface>(&Surface))
    {
        const Vector3 Local = LocalCoordinates(Plane->Frame, Position);
        return { Local.X, Local.Y };
    }
    const OrthonormalFrame& Frame = std::visit([](const auto& Typed) -> const OrthonormalFrame&
    {
        using Kind = std::decay_t<decltype(Typed)>;
        if constexpr (std::is_same_v<Kind, RollingBallSurface> || std::is_same_v<Kind, NurbsSurface> || std::is_same_v<Kind, ParallelSurface>) { static const OrthonormalFrame Unused{}; return Unused; }   // Returned above
        else return Typed.Frame;
    }, Surface);
    const Vector3 Local  = LocalCoordinates(Frame, Position);                                // [len] - Frame coordinates
    const double  ρ      = std::hypot(Local.X, Local.Y);                                     // [len] - Distance from the axis
    // ⚠️ On the axis the azimuth is undefined; the reference azimuth is kept (poles, apex).
    const double  𝑢      = ρ > 1.0e-14 ? FoldNearest(std::atan2(Local.Y, Local.X), Reference.X, FullTurn) : Reference.X;
    if (std::holds_alternative<CylinderSurface>(Surface)) return { 𝑢, Local.Z };
    if (const auto* Cone = std::get_if<ConeSurface>(&Surface))
    {
        // 📝 Foot of the perpendicular on the generator line (r₀ + 𝑣·s, 𝑣) in the meridian half-plane.
        const double s = Cone->RadialSlope;                                                  // [-] - dr/d𝑣
        return { 𝑢, ((ρ - Cone->OriginRadius) * s + Local.Z) / (1.0 + s * s) };
    }
    if (std::holds_alternative<SphereSurface>(Surface)) return { 𝑢, std::atan2(Local.Z, ρ) };
    const auto&  Torus = std::get<TorusSurface>(Surface);
    const double 𝑣     = std::atan2(Local.Z, ρ - Torus.MajorRadius);                          // [rad] - Tube angle
    return { 𝑢, FoldNearest(𝑣, Reference.Y, FullTurn) };
}

SurfacePeriodicity ResolvePeriodicity(const SurfaceGeometry& Surface) noexcept
{
    if (std::holds_alternative<PlaneSurface>(Surface)) return { 0.0, 0.0 };
    if (std::holds_alternative<TorusSurface>(Surface)) return { FullTurn, FullTurn };
    if (const auto* Carrier = std::get_if<RollingBallSurface>(&Surface)) return ResolveRollingBallPeriodicity(*Carrier);
    if (const NurbsSurface* Spline = ResolveNurbsChart(Surface)) return ResolveNurbsPeriodicity(*Spline);
    return { FullTurn, 0.0 };
}

std::vector<double> CollectSurfaceBreakpoints(const SurfaceGeometry& Surface, double Low, double High)
{
    if (const auto* Ball = std::get_if<RollingBallSurface>(&Surface)) return CollectCurveBreakpoints(Ball->Definition->Spine, Low, High);
    if (const NurbsSurface* Spline = ResolveNurbsChart(Surface)) return CollectNurbsSurfaceKnots(*Spline, true, Low, High);
    return {};
}

std::vector<double> CollectSurfaceBreakpointsAlongV(const SurfaceGeometry& Surface, double Low, double High)
{
    if (const NurbsSurface* Spline = ResolveNurbsChart(Surface)) return CollectNurbsSurfaceKnots(*Spline, false, Low, High);
    return {};
}

double MeasureDerivativeNoise(const SurfaceGeometry& Surface) noexcept
{
    return std::holds_alternative<RollingBallSurface>(Surface) ? RollingBallDerivativeNoise : 0.0;
}

const char* SurfaceCategoryLabel(const SurfaceGeometry& Surface) noexcept
{
    constexpr const char* Labels[] = { "Plane", "Cylinder", "Cone", "Sphere", "Torus", "RollingBall", "Nurbs", "Parallel" };
    return Labels[Surface.index()];
}

const NurbsSurface* ResolveNurbsChart(const SurfaceGeometry& Surface) noexcept
{
    if (const auto* Spline = std::get_if<NurbsSurface>(&Surface)) return Spline;
    if (const auto* Offset = std::get_if<ParallelSurface>(&Surface)) return &Offset->Definition->Basis;
    return nullptr;
}

bool ProceduralSurfaceCondition(const SurfaceGeometry& Surface) noexcept
{
    return std::holds_alternative<RollingBallSurface>(Surface) || std::holds_alternative<NurbsSurface>(Surface) || std::holds_alternative<ParallelSurface>(Surface);
}

std::optional<SurfaceGeometry> OffsetSurface(const SurfaceGeometry& Surface, double Distance) noexcept
{
    if (const auto* Carrier = std::get_if<RollingBallSurface>(&Surface)) return OffsetRollingBall(*Carrier, Distance);
    if (const auto* Spline = std::get_if<NurbsSurface>(&Surface))
    {
        if (Distance == 0.0) return Surface;
        return ConstructParallelSurface(*Spline, Distance);
    }
    if (const auto* Offset = std::get_if<ParallelSurface>(&Surface))
    {
        const double Total = Offset->Definition->Distance + Distance;                         // [len] - Distances add
        if (Total == 0.0) return SurfaceGeometry{ Offset->Definition->Basis };
        return ConstructParallelSurface(Offset->Definition->Basis, Total);
    }
    if (const auto* Plane = std::get_if<PlaneSurface>(&Surface))
    {
        PlaneSurface Offset = *Plane;
        Offset.Frame.Origin = Plane->Frame.Origin + Plane->Frame.ZAxis * Distance;
        return Offset;
    }
    if (const auto* Cylinder = std::get_if<CylinderSurface>(&Surface))
    {
        if (!(Cylinder->Radius + Distance > 0.0)) return std::nullopt;
        return CylinderSurface{ Cylinder->Frame, Cylinder->Radius + Distance };
    }
    if (const auto* Cone = std::get_if<ConeSurface>(&Surface))
    {
        // 📝 n = (ρ̂ − s·ẑ)/√(1 + s²): a point at height 𝑣 moves to radius r + δ/√(1 + s²) at height 𝑣 − δs/√(1 + s²),
        //    which lies on r₀′ + 𝑣′·s with r₀′ = r₀ + δ·√(1 + s²).
        const double OriginRadius = Cone->OriginRadius + Distance * std::sqrt(1.0 + Cone->RadialSlope * Cone->RadialSlope);
        if (!(OriginRadius > 0.0)) return std::nullopt;
        return ConeSurface{ Cone->Frame, OriginRadius, Cone->RadialSlope };
    }
    if (const auto* Sphere = std::get_if<SphereSurface>(&Surface))
    {
        if (!(Sphere->Radius + Distance > 0.0)) return std::nullopt;
        return SphereSurface{ Sphere->Frame, Sphere->Radius + Distance };
    }
    const auto& Torus = std::get<TorusSurface>(Surface);
    const double Minor = Torus.MinorRadius + Distance;                                       // [len] - Offset tube radius
    if (!(Minor > 0.0) || !(Minor < Torus.MajorRadius)) return std::nullopt;
    return TorusSurface{ Torus.Frame, Torus.MajorRadius, Minor };
}

} // namespace Frontier::GeometrySpace::Parametric
