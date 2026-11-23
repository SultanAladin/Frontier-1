//============================================================================================================================================
//                                                        TRANSFORMGIZMO.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/TransformGizmo/Api/TransformGizmo.h"

#include <algorithm>
#include <cmath>

namespace Slate
{

namespace
{

constexpr double GizmoPi = 3.14159265358979323846;

double DistanceSquared(float X0, float Y0, float X1, float Y1)
{
    const double DX = static_cast<double>(X1) - static_cast<double>(X0);
    const double DY = static_cast<double>(Y1) - static_cast<double>(Y0);
    return DX * DX + DY * DY;
}

/// 🧩 How far a point lies from a segment, squared.
/// note 📝 Squared throughout so no square root is taken to answer a comparison. Every caller compares
///       against a squared reach.
double DistanceToSegmentSquared(float PX, float PY, float AX, float AY, float BX, float BY)
{
    const double DX = static_cast<double>(BX) - static_cast<double>(AX);
    const double DY = static_cast<double>(BY) - static_cast<double>(AY);
    const double LengthSquared = DX * DX + DY * DY;
    if (LengthSquared <= 1.0e-9)
        return DistanceSquared(PX, PY, AX, AY);

    double Parameter = ((static_cast<double>(PX) - AX) * DX + (static_cast<double>(PY) - AY) * DY) / LengthSquared;
    Parameter = Parameter < 0.0 ? 0.0 : (Parameter > 1.0 ? 1.0 : Parameter);

    const double ClosestX = AX + DX * Parameter;
    const double ClosestY = AY + DY * Parameter;
    const double OffsetX = static_cast<double>(PX) - ClosestX;
    const double OffsetY = static_cast<double>(PY) - ClosestY;
    return OffsetX * OffsetX + OffsetY * OffsetY;
}

double DistanceToArcBarSquared(float PX,
                               float PY,
                               float OriginX,
                               float OriginY,
                               float UX,
                               float UY,
                               float VX,
                               float VY)
{
    const double Start = GizmoPi * 0.25 - GizmoMeasure::RotateSweepRadians * 0.5;
    double Best = 1.0e30;
    for (std::uint32_t Segment = 0u; Segment < GizmoMeasure::RotateSegments; ++Segment)
    {
        const double A0 = Start + GizmoMeasure::RotateSweepRadians
                                * static_cast<double>(Segment)
                                / static_cast<double>(GizmoMeasure::RotateSegments);
        const double A1 = Start + GizmoMeasure::RotateSweepRadians
                                * static_cast<double>(Segment + 1u)
                                / static_cast<double>(GizmoMeasure::RotateSegments);

        const float X0 = OriginX
                       + (UX * static_cast<float>(std::cos(A0)) + VX * static_cast<float>(std::sin(A0)))
                         * static_cast<float>(GizmoMeasure::RotateRadius);
        const float Y0 = OriginY
                       + (UY * static_cast<float>(std::cos(A0)) + VY * static_cast<float>(std::sin(A0)))
                         * static_cast<float>(GizmoMeasure::RotateRadius);
        const float X1 = OriginX
                       + (UX * static_cast<float>(std::cos(A1)) + VX * static_cast<float>(std::sin(A1)))
                         * static_cast<float>(GizmoMeasure::RotateRadius);
        const float Y1 = OriginY
                       + (UY * static_cast<float>(std::cos(A1)) + VY * static_cast<float>(std::sin(A1)))
                         * static_cast<float>(GizmoMeasure::RotateRadius);
        const double Candidate = DistanceToSegmentSquared(PX, PY, X0, Y0, X1, Y1);
        if (Candidate < Best)
            Best = Candidate;
    }
    return Best;
}

}   // namespace

bool ResolveGizmoScreenBasis(const SpatialBasis& Basis,
                             const ViewportStanding& View,
                             bool Perspective,
                             const PlaneExtent& Extent,
                             const SpatialPoint& Pivot,
                             GizmoScreenBasis& Resolved)
{
    Resolved = {};
    if (!ProjectSpatialPoint(Basis, View, Perspective, Extent, Pivot, Resolved.PivotX, Resolved.PivotY))
    {
        // 🔴 A CAD manipulator is a screen control, not scene geometry. At extreme zoom the
        //    selected pivot can cross the near clip or sit behind the eye; rejecting the whole
        //    gizmo made it disappear exactly when close inspection was needed. Keep the control
        //    seated at the leaf centre and retain the camera-facing axis directions. The transform
        //    session still uses the real pivot and camera for its math; this is only the visual
        //    fallback that keeps the handles reachable.
        if (!Perspective)
            return false;
        Resolved.PivotX = (Extent.MinimumX + Extent.MaximumX) * 0.5f;
        Resolved.PivotY = (Extent.MinimumY + Extent.MaximumY) * 0.5f;
        const ViewFrame FallbackFrame = ResolveViewportFrame(Basis, View, true);
        const double TanHalf = std::tan(View.FieldOfViewDegrees * 0.5 * ProjectionPi / 180.0);
        const double Height = std::max(static_cast<double>(Extent.Height()), 1.0);
        Resolved.WorldPerPixel = std::clamp(
            std::fabs(View.Distance) * TanHalf / (Height * 0.5), 1.0e-5, 1.0e3);
        const auto FallbackDirection = [&](const SpatialDirection& Axis, float& X, float& Y)
        {
            const double ProjectedX = Dot(Axis, FallbackFrame.Right);
            const double ProjectedY = -Dot(Axis, FallbackFrame.Up);
            const double Length = std::sqrt(ProjectedX * ProjectedX + ProjectedY * ProjectedY);
            if (Length > 1.0e-6)
            {
                X = static_cast<float>(ProjectedX / Length);
                Y = static_cast<float>(ProjectedY / Length);
            }
        };
        FallbackDirection(Basis.Along, Resolved.AlongX, Resolved.AlongY);
        FallbackDirection(Basis.Across, Resolved.AcrossX, Resolved.AcrossY);
        FallbackDirection(Basis.Normal, Resolved.NormalX, Resolved.NormalY);
        return true;
    }

    if (!Perspective)
    {
        // 🔴 ORTHOGRAPHIC SCALE IS ALREADY A SCREEN RULER. Do not infer it from a world probe: a
        //    stale or nearly edge-on basis can make that probe disappear and fall back to one world
        //    unit per pixel, which is exactly the zoom-dependent balloon/collapse reported for the
        //    gizmo. The viewport measure is pixels per world unit, so its reciprocal is authoritative.
        const double Scale = std::fabs(View.OrthoScale);
        Resolved.WorldPerPixel = 1.0 / std::max(Scale, 1.0e-6);

        const auto Direction = [&](const SpatialDirection& Axis, float& X, float& Y)
        {
            float ProjectedX = 0.0f;
            float ProjectedY = 0.0f;
            if (!ProjectSpatialPoint(Basis, View, false, Extent,
                                     Added(Pivot, Axis), ProjectedX, ProjectedY))
                return;

            const double DX = static_cast<double>(ProjectedX) - Resolved.PivotX;
            const double DY = static_cast<double>(ProjectedY) - Resolved.PivotY;
            const double Length = std::sqrt(DX * DX + DY * DY);
            if (Length > 1.0e-4)
            {
                X = static_cast<float>(DX / Length);
                Y = static_cast<float>(DY / Length);
            }
        };
        Direction(Basis.Along, Resolved.AlongX, Resolved.AlongY);
        Direction(Basis.Across, Resolved.AcrossX, Resolved.AcrossY);
        Direction(Basis.Normal, Resolved.NormalX, Resolved.NormalY);
        return true;
    }

    // 🔴 THE PROBE IS BOTH A DIRECTION AND A RULER. Projecting a known world offset and measuring how
    //    many pixels it covered gives the world-per-pixel conversion directly, with no assumption about
    //    which projection is in use or how it is parameterised.
    //
    // ⚠️ IT MUST BE MEASURED OVER THE SPAN IT WILL BE USED FOR. Perspective is not linear, so a ruler read
    //    over a long offset under-reports the rate near the pivot. So: read a rough ruler, work out how
    //    far the gizmo actually reaches in world units, and read it again over exactly that.
    double ProbeWorld = 24.0;
    bool Measured = false;

    const auto Probe = [&](const SpatialDirection& Direction, float& DirX, float& DirY)
    {
        float X = 0.0f;
        float Y = 0.0f;
        if (!ProjectSpatialPoint(Basis, View, Perspective, Extent,
                                 Added(Pivot, Scaled(Direction, ProbeWorld)), X, Y))
            return;

        const double DX = static_cast<double>(X) - Resolved.PivotX;
        const double DY = static_cast<double>(Y) - Resolved.PivotY;
        const double Length = std::sqrt(DX * DX + DY * DY);
        if (Length <= 1.0e-4)
            return;

        DirX = static_cast<float>(DX / Length);
        DirY = static_cast<float>(DY / Length);

        // 🔴 TAKE THE SMALLEST WORLD-PER-PIXEL OF THE VISIBLE AXES, WHICH IS THE ONE THAT PROJECTS LONGEST.
        //    That guarantees none of the HTML-sized handles overshoots its own screen footprint.
        const double Candidate = ProbeWorld / Length;
        if (!Measured || Candidate < Resolved.WorldPerPixel)
        {
            Resolved.WorldPerPixel = Candidate;
            Measured = true;
        }
    };

    Probe(Basis.Along, Resolved.AlongX, Resolved.AlongY);
    Probe(Basis.Across, Resolved.AcrossX, Resolved.AcrossY);
    Probe(Basis.Normal, Resolved.NormalX, Resolved.NormalY);

    // 🔴 PERSPECTIVE IS NON-LINEAR. A long probe can cross enough depth that the first estimate
    //    describes the wrong end of the handle. Re-measure the actual 82 px span a few times; the
    //    fixed point is the world length whose projected reach is the table's screen length. The
    //    orthographic case converges in one pass, while close perspective pivots get the same bounded
    //    footprint instead of growing or shrinking with zoom.
    for (std::uint32_t Iteration = 0u; Measured && Iteration < 4u; ++Iteration)
    {
        ProbeWorld = Resolved.WorldPerPixel * GizmoMeasure::AxisEnd;
        if (ProbeWorld <= 1.0e-6)
            break;

        Measured = false;
        Probe(Basis.Along, Resolved.AlongX, Resolved.AlongY);
        Probe(Basis.Across, Resolved.AcrossX, Resolved.AcrossY);
        Probe(Basis.Normal, Resolved.NormalX, Resolved.NormalY);
    }

    if (!Measured)
        Resolved.WorldPerPixel = 1.0;

    return true;
}

bool ResolveGizmoScreenBasis(const ResolvedCamera& Camera,
                             const PlaneExtent& Extent,
                             const SpatialPoint& Pivot,
                             GizmoScreenBasis& Resolved)
{
    Resolved = {};
    if (!ProjectFromCamera(Camera, Extent, Pivot, Resolved.PivotX, Resolved.PivotY))
        return false;

    if (!Camera.Perspective)
    {
        // 🔴 The orthographic camera carries the authoritative pixel-to-world ruler. Using it
        //    directly prevents the gizmo from inheriting numerical or basis failures from a probe.
        const double Scale = std::fabs(Camera.OrthoScale);
        Resolved.WorldPerPixel = 1.0 / std::max(Scale, 1.0e-6);

        const auto Direction = [&](const SpatialDirection& Axis, float& X, float& Y)
        {
            float ProjectedX = 0.0f;
            float ProjectedY = 0.0f;
            if (!ProjectFromCamera(Camera, Extent, Added(Pivot, Axis), ProjectedX, ProjectedY))
                return;

            const double DX = static_cast<double>(ProjectedX) - Resolved.PivotX;
            const double DY = static_cast<double>(ProjectedY) - Resolved.PivotY;
            const double Length = std::sqrt(DX * DX + DY * DY);
            if (Length > 1.0e-4)
            {
                X = static_cast<float>(DX / Length);
                Y = static_cast<float>(DY / Length);
            }
        };
        Direction(Camera.Basis.Along, Resolved.AlongX, Resolved.AlongY);
        Direction(Camera.Basis.Across, Resolved.AcrossX, Resolved.AcrossY);
        Direction(Camera.Basis.Normal, Resolved.NormalX, Resolved.NormalY);
        return true;
    }

    const double CameraZ = Dot(Difference(Camera.Frame.Eye, Pivot), Camera.Frame.Forward);
    if (CameraZ <= 0.01)
        return false;

    const double TanHalf = std::tan(Camera.FieldOfViewDegrees * 0.5 * ProjectionPi / 180.0);
    const double Focal   = (Extent.Height() * 0.5) / std::max(TanHalf, 1.0e-5);
    Resolved.WorldPerPixel = std::clamp(CameraZ / Focal, 1.0e-5, 100.0);

    const auto Direction = [&](const SpatialDirection& Axis, float& X, float& Y)
    {
        float ProjectedX = 0.0f;
        float ProjectedY = 0.0f;
        const double Probe = Resolved.WorldPerPixel * 40.0;
        if (!ProjectFromCamera(Camera, Extent, Added(Pivot, Scaled(Axis, Probe)), ProjectedX, ProjectedY))
            return;

        const double DX = static_cast<double>(ProjectedX) - Resolved.PivotX;
        const double DY = static_cast<double>(ProjectedY) - Resolved.PivotY;
        const double Length = std::sqrt(DX * DX + DY * DY);
        if (Length > 1.0e-4)
        {
            X = static_cast<float>(DX / Length);
            Y = static_cast<float>(DY / Length);
        }
    };
    Direction(Camera.Basis.Along, Resolved.AlongX, Resolved.AlongY);
    Direction(Camera.Basis.Across, Resolved.AcrossX, Resolved.AcrossY);
    Direction(Camera.Basis.Normal, Resolved.NormalX, Resolved.NormalY);
    return true;
}

GizmoHandle ResolveGizmoHandle(const GizmoScreenBasis& Screen,
                               TransformManner Manner,
                               float PointerX,
                               float PointerY)
{
    const double CentreDistanceSquared = DistanceSquared(PointerX, PointerY, Screen.PivotX, Screen.PivotY);

    const auto ConeDistanceSquared = [&](float DirX, float DirY)
    {
        const float BaseX = Screen.PivotX + DirX * 12.0f;
        const float BaseY = Screen.PivotY + DirY * 12.0f;
        const float TipX  = Screen.PivotX + DirX * static_cast<float>(GizmoMeasure::AxisEnd);
        const float TipY  = Screen.PivotY + DirY * static_cast<float>(GizmoMeasure::AxisEnd);
        return DistanceToSegmentSquared(PointerX, PointerY, BaseX, BaseY, TipX, TipY);
    };

    const auto CylinderDistanceSquared = [&](float DirX, float DirY)
    {
        const float StartX = Screen.PivotX + DirX * 12.0f;
        const float StartY = Screen.PivotY + DirY * 12.0f;
        const float EndX   = Screen.PivotX + DirX * static_cast<float>(GizmoMeasure::AxisEnd);
        const float EndY   = Screen.PivotY + DirY * static_cast<float>(GizmoMeasure::AxisEnd);
        return DistanceToSegmentSquared(PointerX, PointerY, StartX, StartY, EndX, EndY);
    };

    if (Manner == TransformManner::Move)
    {
        // 🔴 TEST AXES BEFORE THE PLANAR SQUARE. The green normal arrow is also an edge of the
        //    visible YZ square in a front-facing view; testing that square first made the arrow look
        //    present but dead because the same pixels were always classified as free move.
        if (ConeDistanceSquared(Screen.AlongX, Screen.AlongY) <= GizmoMeasure::MoveGrab * GizmoMeasure::MoveGrab)
            return GizmoHandle::MoveX;
        if (ConeDistanceSquared(Screen.AcrossX, Screen.AcrossY) <= GizmoMeasure::MoveGrab * GizmoMeasure::MoveGrab)
            return GizmoHandle::MoveZ;
        if (ConeDistanceSquared(Screen.NormalX, Screen.NormalY) <= GizmoMeasure::MoveGrab * GizmoMeasure::MoveGrab)
            return GizmoHandle::MoveY;

        // 📝 The sketch's free-move plane is the HTML gizmo's XZ square — the square normal to +Y.
        const auto PlaneHit = [&](float Ux, float Uy, float Vx, float Vy)
        {
            const float PlaneX = Screen.PivotX + (Ux + Vx) * static_cast<float>(GizmoMeasure::PlaneCentre);
            const float PlaneY = Screen.PivotY + (Uy + Vy) * static_cast<float>(GizmoMeasure::PlaneCentre);
            const double LocalU = (PointerX - PlaneX) * Ux + (PointerY - PlaneY) * Uy;
            const double LocalV = (PointerX - PlaneX) * Vx + (PointerY - PlaneY) * Vy;
            const double Grab = GizmoMeasure::PlaneHalf + 8.0;
            return std::fabs(LocalU) <= Grab && std::fabs(LocalV) <= Grab;
        };
        // 🟡 The planar pads are intentionally forgiving: the visible fill is small, but
        //    the interaction radius extends beyond its edge so a CAD pointer does not need
        //    sub-pixel precision to highlight or grab the pad.
        if (PlaneHit(Screen.NormalX, Screen.NormalY, Screen.AcrossX, Screen.AcrossY))
            return GizmoHandle::MoveYZ;
        if (PlaneHit(Screen.AlongX, Screen.AlongY, Screen.AcrossX, Screen.AcrossY))
            return GizmoHandle::MoveFree;
        if (PlaneHit(Screen.AlongX, Screen.AlongY, Screen.NormalX, Screen.NormalY))
            return GizmoHandle::MoveXY;

        if (CentreDistanceSquared <= GizmoMeasure::CentreGrab * GizmoMeasure::CentreGrab * 0.25)
            return GizmoHandle::MoveFree;
    }
    else if (Manner == TransformManner::Rotate)
    {
        const double ReachSquared = GizmoMeasure::RotateGrab * GizmoMeasure::RotateGrab;
        if (DistanceToArcBarSquared(PointerX, PointerY,
                                    Screen.PivotX, Screen.PivotY,
                                    Screen.AcrossX, Screen.AcrossY,
                                    Screen.NormalX, Screen.NormalY) <= ReachSquared)
            return GizmoHandle::Rotate;
        if (DistanceToArcBarSquared(PointerX, PointerY,
                                    Screen.PivotX, Screen.PivotY,
                                    Screen.AlongX, Screen.AlongY,
                                    Screen.AcrossX, Screen.AcrossY) <= ReachSquared)
            return GizmoHandle::Rotate;
        if (DistanceToArcBarSquared(PointerX, PointerY,
                                    Screen.PivotX, Screen.PivotY,
                                    Screen.AlongX, Screen.AlongY,
                                    Screen.NormalX, Screen.NormalY) <= ReachSquared)
            return GizmoHandle::Rotate;

        if (CentreDistanceSquared <= GizmoMeasure::CentreGrab * GizmoMeasure::CentreGrab * 0.25)
            return GizmoHandle::Rotate;
    }
    else
    {
        if (CylinderDistanceSquared(Screen.AlongX, Screen.AlongY) <= GizmoMeasure::ScaleGrab * GizmoMeasure::ScaleGrab)
            return GizmoHandle::ScaleX;
        if (CylinderDistanceSquared(Screen.AcrossX, Screen.AcrossY) <= GizmoMeasure::ScaleGrab * GizmoMeasure::ScaleGrab)
            return GizmoHandle::ScaleZ;
        if (CylinderDistanceSquared(Screen.NormalX, Screen.NormalY) <= GizmoMeasure::ScaleGrab * GizmoMeasure::ScaleGrab)
            return GizmoHandle::ScaleY;
        if (CentreDistanceSquared <= GizmoMeasure::CentreGrab * GizmoMeasure::CentreGrab * 0.25)
            return GizmoHandle::ScaleFree;
    }

    return GizmoHandle::None;
}

TransformManner ResolveHandleManner(GizmoHandle Handle)
{
    switch (Handle)
    {
        case GizmoHandle::MoveFree:
        case GizmoHandle::MoveX:
        case GizmoHandle::MoveY:
        case GizmoHandle::MoveZ:
        case GizmoHandle::MoveYZ:
        case GizmoHandle::MoveXZ:
        case GizmoHandle::MoveXY:     return TransformManner::Move;
        case GizmoHandle::Rotate:     return TransformManner::Rotate;
        case GizmoHandle::ScaleFree:
        case GizmoHandle::ScaleX:
        case GizmoHandle::ScaleY:
        case GizmoHandle::ScaleZ:     return TransformManner::Scale;
        case GizmoHandle::None:       return TransformManner::Move;
    }
    return TransformManner::Move;
}

TransformRestriction ResolveHandleRestriction(GizmoHandle Handle)
{
    switch (Handle)
    {
        case GizmoHandle::MoveX:
        case GizmoHandle::ScaleX:     return TransformRestriction::AxisX;
        case GizmoHandle::MoveY:
        case GizmoHandle::ScaleY:     return TransformRestriction::AxisY;
        case GizmoHandle::MoveZ:
        case GizmoHandle::ScaleZ:     return TransformRestriction::AxisZ;
        case GizmoHandle::Rotate:     return TransformRestriction::Screen;
        case GizmoHandle::MoveFree:
        case GizmoHandle::MoveYZ:
        case GizmoHandle::MoveXZ:
        case GizmoHandle::MoveXY:
        case GizmoHandle::ScaleFree:
        case GizmoHandle::None:       return TransformRestriction::Free;
    }
    return TransformRestriction::Free;
}

}   // namespace Slate
