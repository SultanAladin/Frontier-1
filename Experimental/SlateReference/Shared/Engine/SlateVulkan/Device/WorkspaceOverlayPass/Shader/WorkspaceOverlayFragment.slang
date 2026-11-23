//============================================================================================================================================
//                                                         OVERLAYFRAGMENT.SLANG
//============================================================================================================================================
// 🧩 The overlay pass's fragment stage: writes the primitive's straight-alpha
//    colour whole, with NO tone mapping and NO premultiplication — the pipeline
//    blends `src_alpha / one_minus_src_alpha`, which is what keeps a vivid gizmo
//    colour vivid over a bright sky (the premultiplied interface blend washed
//    low-alpha lines out). The colour the vertex stage expanded is display-
//    referred, exactly as the interface's own is; nothing here scales it.
//
//    Ground-grid kind is different in kind: it receives ONE quad and solves the ground
//    lattice analytically, per pixel. See `ResolveGround` below.

#include "Shared/ToolchainInterchange.slang.h"

namespace Slate
{

static const Unsigned32 WorkspaceOverlayLineDraw       = 0u;
static const Unsigned32 WorkspaceOverlayDotDraw        = 1u;
static const Unsigned32 WorkspaceOverlayTriangleDraw   = 2u;
static const Unsigned32 WorkspaceOverlayGroundGridDraw = 3u;

struct WorkspaceOverlayVertexOutput
{
    Real32x4   Position : SV_Position;
    Real32x4   Colour   : COLOR0;
    Real32x2   Local    : TEXCOORD0;
};

// 📐 The same push constant the vertex stage declares — Slang requires the
//    layout to agree, and the fragment stage reads the camera terms.
struct OverlayPushConstant
{
    Unsigned32 Draw;
    Real32     DisplayWidth;
    Real32     DisplayHeight;
    Real32     FadeRadiusMetres;

    Real32x4   EyeAndCell;
    Real32x4   ForwardAndTanH;
    Real32x4   RightAndTanV;
    Real32x4   UpAndPresent;
    Real32x4   LatticeColour;
    Real32x4   WeightsAndDot;
    Real32x4   LeafRect;
};

[[vk::push_constant]]
ConstantBuffer<OverlayPushConstant> Push;

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE ANALYTIC GROUND
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How close a coordinate is to a lattice line, in units of that line's own
///    screen-space width — so the answer is a coverage, not a distance, and the
///    line stays one pixel wide however far away it is.
/// note  📐 `fwidth` is the screen-space derivative: it IS the level of detail.
///        The CPU path had to guess a sample step and a density threshold and
///        got both wrong; here the hardware reports the exact figure per pixel.
Real32 LatticeCoverage(Real32 Coordinate, Real32 Cell, Real32 Weight)
{
    const Real32 Scaled  = Coordinate / Cell;
    const Real32 Spacing = fwidth(Scaled);

    if (Spacing <= 0.0)
        return 0.0;

    // 📐 Distance to the nearest whole multiple, in cells, divided by how much a
    //    pixel spans — the classic derivative-normalised grid.
    const Real32 Nearest = abs(frac(Scaled - 0.5) - 0.5) / Spacing;

    return 1.0 - min(Nearest / max(Weight, 0.25), 1.0);
}

/// 🧩 One octave of the lattice, faded out as its lines crowd together.
/// note  📐 An octave whose cells fall below a few pixels apart would be a grey
///        wash; the fade is on the SAME derivative that draws it, so an octave
///        never pops — the defect the CPU path's density threshold produced.
Real32 OctaveCoverage(Real32x2 Ground, Real32 Cell, Real32 Weight)
{
    const Real32 SpanX = fwidth(Ground.x / Cell);
    const Real32 SpanY = fwidth(Ground.y / Cell);
    const Real32 Span  = max(SpanX, SpanY);

    // 📐 Below one cell per two pixels the octave is retired, over a full octave
    //    of travel so the handover between two octaves is continuous.
    const Real32 Fade = 1.0 - smoothstep(0.25, 0.5, Span);

    if (Fade <= 0.0)
        return 0.0;

    const Real32 Along = LatticeCoverage(Ground.x, Cell, Weight);
    const Real32 Cross = LatticeCoverage(Ground.y, Cell, Weight);

    return max(Along, Cross) * Fade;
}

/// 🧩 The ground plane and world axes solved 100% on the GPU: the ray/plane intersection,
///    unified minor & major grid lines fading together, and Red/Green/Blue XYZ axes.
Real32x4 ResolveGround(Real32x2 Corner)
{
    const Real32x3 Eye       = Push.EyeAndCell.xyz;
    const Real32   Cell      = max(Push.EyeAndCell.w, 0.0001);
    const Real32x3 Forward   = Push.ForwardAndTanH.xyz;
    const Real32x3 Right     = Push.RightAndTanV.xyz;
    const Real32x3 Up        = Push.UpAndPresent.xyz;
    const Real32   TanH      = Push.ForwardAndTanH.w;
    const Real32   TanV      = Push.RightAndTanV.w;
    const Real32   Weight    = max(Push.WeightsAndDot.x, 0.25);
    const Real32   DotRadius = max(Push.WeightsAndDot.y, 0.25);
    const Real32   MajorStep = max(Push.WeightsAndDot.z, 2.0);
    const Unsigned32 AxisPack = Unsigned32(Push.WeightsAndDot.w + 0.5);
    const Unsigned32 AxisMask = AxisPack & 0xFFu;
    const Unsigned32 ViewedOrientation = (AxisPack >> 8u) & 0xFFu;
    const bool ShowAxisX = (AxisMask & 1u) != 0u;
    const bool ShowAxisY = (AxisMask & 2u) != 0u;
    const bool ShowAxisZ = (AxisMask & 4u) != 0u;
    const Unsigned32 Present = Unsigned32(Push.UpAndPresent.w + 0.5) & 0xFFu;

    // ① The pixel's ray. Perspective rays fan out from one eye; orthographic rays are PARALLEL and
    //    leave from different origins, so the eye moves per pixel rather than the direction changing.
    //    The grid used to ray-march a pinhole camera unconditionally, so it stayed in perspective
    //    while the geometry drawn on it went orthographic.
    const Real32 NdcX = Corner.x * 2.0 - 1.0;
    const Real32 NdcY = 1.0 - Corner.y * 2.0;

    // 📐 THE PUSH BLOCK IS FULL AT VULKAN'S GUARANTEED 128 BYTES, so the orthographic scale rides in
    //    the high bits of `Presentation` rather than widening it: the presentation enum needs 8 bits,
    //    and the scale is quantised into the rest as hundredths of a pixel per unit. Zero means
    //    perspective, which is what every existing caller pushes.
    const Real32 OrthoScale = Real32(Unsigned32(Push.UpAndPresent.w + 0.5) >> 8u) * 0.01;
    const bool   Parallel   = OrthoScale > 0.0;

    Real32x3 RayOrigin = Eye;
    Real32x3 Ray       = Real32x3(0.0, 0.0, 1.0);

    if (Parallel)
    {
        // 📐 Half the leaf in world units at this scale; the pixel's offset from the centre displaces
        //    the origin, and every ray runs along the camera's forward direction.
        // ⚠️ The LEAF's size, not the display's. `Corner` is already normalised across the leaf, so
        //    using the whole display would scale the lattice by the ratio between them -- the grid
        //    would change size when a panel is split.
        const Real32 HalfWidth  = ((Push.LeafRect.z - Push.LeafRect.x) * 0.5) / OrthoScale;
        const Real32 HalfHeight = ((Push.LeafRect.w - Push.LeafRect.y) * 0.5) / OrthoScale;
        RayOrigin = Eye + Right * (NdcX * HalfWidth) + Up * (NdcY * HalfHeight);
        Ray       = normalize(Forward);
    }
    else
    {
        Ray = normalize(Right * (NdcX * TanH) + Up * (NdcY * TanV) + Forward);
    }

    // 📐 Two independent authored limits: one bounds the lattice around world centre, the other
    //    follows the camera and softens nearby detail into nothing at its radius.
    const Real32 WorldExtent = max(Push.LatticeColour.a, Cell);
    const Real32 FadeRadius  = max(Push.FadeRadiusMetres, Cell);
    const Real32 LatticeAlpha = 0.55;

    Real32x3 FinalTone  = Push.LatticeColour.rgb;
    Real32   FinalAlpha = 0.0;

    // ② THE VIEW GRID APPEARS ONLY FOR THE EXACT ORTHOGRAPHIC VIEWS the cube/widget snaps to. Letting it
    //    chase the dominant camera axis while the artist drags would make the plane flip underneath them.
    bool PlaneStanding = !Parallel;
    Real32x3 PlaneNormal = Real32x3(0.0, 1.0, 0.0);
    Real32x3 PlaneAlong  = Real32x3(1.0, 0.0, 0.0);
    Real32x3 PlaneAcross = Real32x3(0.0, 0.0, 1.0);
    Unsigned32 PlaneAxisAlong = 0u;
    Unsigned32 PlaneAxisAcross = 2u;

    if (Parallel)
    {
        PlaneStanding = true;
        if (ViewedOrientation == 0u || ViewedOrientation == 1u)
        {
            PlaneNormal = Real32x3(0.0, 1.0, 0.0);
            PlaneAlong  = Real32x3(1.0, 0.0, 0.0);
            PlaneAcross = Real32x3(0.0, 0.0, 1.0);
            PlaneAxisAlong = 0u;   // X
            PlaneAxisAcross = 2u;  // Z
        }
        else if (ViewedOrientation == 2u || ViewedOrientation == 3u)
        {
            PlaneNormal = Real32x3(0.0, 0.0, 1.0);
            PlaneAlong  = Real32x3(1.0, 0.0, 0.0);
            PlaneAcross = Real32x3(0.0, 1.0, 0.0);
            PlaneAxisAlong = 0u;   // X
            PlaneAxisAcross = 1u;  // Y
        }
        else if (ViewedOrientation == 4u || ViewedOrientation == 5u)
        {
            PlaneNormal = Real32x3(1.0, 0.0, 0.0);
            PlaneAlong  = Real32x3(0.0, 0.0, 1.0);
            PlaneAcross = Real32x3(0.0, 1.0, 0.0);
            PlaneAxisAlong = 2u;   // Z
            PlaneAxisAcross = 1u;  // Y
        }
        else
        {
            // 🔴 THE FLOOR, AND ONLY THE FLOOR, ONCE THE VIEW IS OFF-AXIS. Free orthographic
            //    navigation has no cube face to name, so it must fall back to the same ground
            //    every other free view stands on.
            //
            // 🔴 This branch USED to name (0,0,1) -- the FRONT wall -- while claiming in its own
            //    comment to "match perspective and use the world floor". It did neither. The world
            //    here is Y-UP (`SpatialPoint` is Left/Up/Forward = X/Y/Z), so the floor is Y = 0
            //    with normal (0,1,0), which is exactly what the perspective default above and the
            //    Top/Bottom arm both use. Naming Z made the grid stand up on edge like a wall the
            //    moment the artist orbited off an exact axis view, and it disagreed with the very
            //    plane the same shader draws in perspective.
            PlaneNormal = Real32x3(0.0, 1.0, 0.0);
            PlaneAlong  = Real32x3(1.0, 0.0, 0.0);
            PlaneAcross = Real32x3(0.0, 0.0, 1.0);
            PlaneAxisAlong = 0u;   // X
            PlaneAxisAcross = 2u;  // Z
        }
    }

    const Real32 Denominator = dot(Ray, PlaneNormal);
    if (PlaneStanding && abs(Denominator) > 1e-6)
    {
        // Orthographic rays are mathematical lines, not half-rays. The camera can be on either side of
        // the selected plane, and an exact axis view can place the ray origin directly on it (Distance
        // equals zero). Rejecting non-positive distances made the XY plane at Z=0 disappear from Front
        // and Back, and made the result depend on which side of a plane the camera happened to occupy.
        const Real32 Distance = -dot(RayOrigin, PlaneNormal) / Denominator;
        if (Parallel || Distance > 0.0)
        {
            const Real32x3 Hit = RayOrigin + Ray * Distance;
            const Real32x2 Ground = Real32x2(dot(Hit, PlaneAlong),
                                             dot(Hit, PlaneAcross));
            const Real32x2 EyePlane = Real32x2(dot(Eye, PlaneAlong),
                                               dot(Eye, PlaneAcross));

            const Real32 MinorCell = Cell;
            const Real32 MajorCell = Cell * MajorStep;

            Real32 GridCoverage = 0.0;

            if (Present == 1u || Present == 3u)
            {
                const Real32 MinorAlong = LatticeCoverage(Ground.x, MinorCell, Weight);
                const Real32 MinorCross = LatticeCoverage(Ground.y, MinorCell, Weight);
                const Real32 MinorGrid  = max(MinorAlong, MinorCross);

                const Real32 MajorAlong = LatticeCoverage(Ground.x, MajorCell, Weight * 1.4);
                const Real32 MajorCross = LatticeCoverage(Ground.y, MajorCell, Weight * 1.4);
                const Real32 MajorGrid  = max(MajorAlong, MajorCross);

                GridCoverage = max(MajorGrid * 0.85, MinorGrid * 0.35);
            }

            if (Present == 2u || Present == 3u)
            {
                const Real32 Coarse    = MajorCell;
                const Real32x2 Scaled  = Ground / Coarse;
                const Real32x2 Span    = Real32x2(fwidth(Scaled.x), fwidth(Scaled.y));
                const Real32x2 Nearest = abs(frac(Scaled - 0.5) - 0.5) / max(Span, Real32x2(1e-6, 1e-6));
                const Real32   Radial  = length(Nearest);
                const Real32   Node    = 1.0 - min(Radial / DotRadius, 1.0);

                GridCoverage = max(GridCoverage, Node);
            }

            const Real32 FromWorldCentre = length(Ground);
            const Real32 FromCamera = length(Ground - EyePlane);
            const Real32 CentreFade = 1.0 - smoothstep(WorldExtent * 0.82, WorldExtent,
                                                       FromWorldCentre);
            const Real32 CameraFade = 1.0 - smoothstep(FadeRadius * 0.62, FadeRadius,
                                                       FromCamera);
            // 🔴 Orthographic authoring is a view, not a finite camera billboard. Its rays cover the
            //    entire leaf at a constant scale, so applying the perspective camera-radius fade here
            //    made the grid randomly disappear after a fly or pan. Keep the user's line/dot settings,
            //    but never let camera distance decide whether an orthographic reference plane exists.
            //    Perspective retains both authored fades so its floor remains finite like Blender's
            //    perspective grid.
            const Real32 SpatialFade = Parallel ? 1.0 : CentreFade * CameraFade;
            FinalAlpha = GridCoverage * LatticeAlpha * SpatialFade;

            const Real32 SpanAlong = fwidth(Ground.x);
            const Real32 SpanAcross = fwidth(Ground.y);
            const Real32 PixelDistAlong = abs(Ground.x) / max(SpanAlong, 1e-6);
            const Real32 PixelDistAcross = abs(Ground.y) / max(SpanAcross, 1e-6);
            const Real32 AxisAcross = 1.0 - min(PixelDistAlong / max(Weight * 1.8, 0.5), 1.0);
            const Real32 AxisAlong = 1.0 - min(PixelDistAcross / max(Weight * 1.8, 0.5), 1.0);

            const Real32x3 RedAxis   = Real32x3(0.95, 0.24, 0.28);
            const Real32x3 GreenAxis = Real32x3(0.19, 0.82, 0.35);
            const Real32x3 BlueAxis  = Real32x3(0.24, 0.44, 0.95);

            if (AxisAlong > 0.0)
            {
                if (PlaneAxisAlong == 0u && ShowAxisX)
                {
                    FinalTone  = lerp(FinalTone, RedAxis, AxisAlong);
                    FinalAlpha = max(FinalAlpha, AxisAlong * 0.95 * SpatialFade);
                }
                else if (PlaneAxisAlong == 1u && ShowAxisY)
                {
                    FinalTone  = lerp(FinalTone, GreenAxis, AxisAlong);
                    FinalAlpha = max(FinalAlpha, AxisAlong * 0.95 * SpatialFade);
                }
                else if (PlaneAxisAlong == 2u && ShowAxisZ)
                {
                    FinalTone  = lerp(FinalTone, BlueAxis, AxisAlong);
                    FinalAlpha = max(FinalAlpha, AxisAlong * 0.95 * SpatialFade);
                }
            }

            if (AxisAcross > 0.0)
            {
                if (PlaneAxisAcross == 0u && ShowAxisX)
                {
                    FinalTone  = lerp(FinalTone, RedAxis, AxisAcross);
                    FinalAlpha = max(FinalAlpha, AxisAcross * 0.95 * SpatialFade);
                }
                else if (PlaneAxisAcross == 1u && ShowAxisY)
                {
                    FinalTone  = lerp(FinalTone, GreenAxis, AxisAcross);
                    FinalAlpha = max(FinalAlpha, AxisAcross * 0.95 * SpatialFade);
                }
                else if (PlaneAxisAcross == 2u && ShowAxisZ)
                {
                    FinalTone  = lerp(FinalTone, BlueAxis, AxisAcross);
                    FinalAlpha = max(FinalAlpha, AxisAcross * 0.95 * SpatialFade);
                }
            }
        }
    }

    // ③ The out-of-plane green Y axis is useful in perspective, where the ground is a reference surface.
    //    In orthographic plane views the in-plane axes above are the wanted read, and a second axis through
    //    the screen centre is only a duplicate or an occluding stripe.
    if (ShowAxisY && !Parallel)
    {
        const Real32 RayXZ2 = Ray.x * Ray.x + Ray.z * Ray.z;
        if (RayXZ2 > 1e-7)
        {
            const Real32 TClosest = -(RayOrigin.x * Ray.x + RayOrigin.z * Ray.z) / RayXZ2;
            if (TClosest > 0.05)
            {
                const Real32 ClosestX = RayOrigin.x + Ray.x * TClosest;
                const Real32 ClosestZ = RayOrigin.z + Ray.z * TClosest;
                const Real32 DistPerp = sqrt(ClosestX * ClosestX + ClosestZ * ClosestZ);

                const Real32 PixelWorldSize = Parallel
                                            ? (1.0 / max(OrthoScale, 1e-6))
                                            : TClosest * (2.0 * TanH / max(Push.DisplayWidth, 1.0));
                const Real32 PixelDist = DistPerp / max(PixelWorldSize, 1e-6);

                Real32 AxisY = 1.0 - min(PixelDist / max(Weight * 1.8, 0.5), 1.0);
                const Real32 ClosestY = RayOrigin.y + Ray.y * TClosest;
                const Real32 CentreFadeY = 1.0 - smoothstep(WorldExtent * 0.82, WorldExtent,
                                                            abs(ClosestY));
                const Real32 CameraFadeY = 1.0 - smoothstep(FadeRadius * 0.62, FadeRadius,
                                                            TClosest);
                AxisY *= CentreFadeY * CameraFadeY;

                if (AxisY > 0.0)
                {
                    const Real32x3 GreenAxis = Real32x3(0.19, 0.82, 0.35);
                    FinalTone  = lerp(FinalTone, GreenAxis, AxisY);
                    FinalAlpha = max(FinalAlpha, AxisY * 0.95);
                }
            }
        }
    }

    if (FinalAlpha <= 0.0)
        return Real32x4(0.0, 0.0, 0.0, 0.0);

    return Real32x4(FinalTone, FinalAlpha);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

// 📐 The entry is named `main`: slangc emits the name the [shader(...)] attribute carries, and the
//    engine's ShaderCodec::Stage always binds the "main" entry of a lowered module.
[shader("fragment")]
Real32x4 main(WorkspaceOverlayVertexOutput Input) : SV_Target
{
    if (Push.Draw == WorkspaceOverlayGroundGridDraw)
        return ResolveGround(Input.Colour.xy);

    if (Push.Draw == WorkspaceOverlayDotDraw)
    {
        const Real32 DistSquared = dot(Input.Local, Input.Local);
        if (DistSquared > 1.0)
            discard;

        const Real32 RadialDistance = sqrt(DistSquared);
        const Real32 Delta          = fwidth(RadialDistance);
        const Real32 Coverage       = 1.0 - smoothstep(1.0 - max(Delta, 0.001), 1.0, RadialDistance);
        return Real32x4(Input.Colour.rgb, Input.Colour.a * Coverage);
    }

    return Input.Colour;
}

} // namespace Slate
