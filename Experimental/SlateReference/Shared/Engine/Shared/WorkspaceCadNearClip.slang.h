//============================================================================================================================================
//                                                      WORKSPACECADNEARCLIP.SLANG.H
//============================================================================================================================================
// 🧩 Shared near-plane clipping for the CAD pass's projected fills and outline segments.
//
// 🔴 WHY THIS EXISTS. The CAD shader projects sketch geometry into screen space by dividing projected x
//    and y by w. When one corner or one endpoint falls at or behind the near plane, dividing the surviving
//    points by their own positive w while the bad one is merely dropped does not clip the geometry — a fill
//    stretches into a giant wedge and an outline pops away at the eye. The fix is geometric, not
//    presentational: clip the triangle or segment against the near plane FIRST, then draw only the part
//    that survives.
//
// 📝 The arithmetic lives in Shared/ so the shader and the host proofs compile the SAME clipper. The
//    clipping plane is `w = WorkspaceCadNearDepth`, because the CAD projection rows carry camera depth in
//    `w` and interpolate it linearly along each edge.

#pragma once

#include "Shared/ToolchainInterchange.slang.h"

namespace Slate
{

SLATE_CONSTANT Real32 WorkspaceCadNearDepth = 0.01f;

struct WorkspaceCadProjectedPoint
{
    Real32 X = 0.0f;
    Real32 Y = 0.0f;
    Real32 W = 1.0f;
};

struct WorkspaceCadScreenPoint
{
    Real32 X = 0.0f;
    Real32 Y = 0.0f;
};

SLATE_SHARED bool WorkspaceCadProjectedFront(WorkspaceCadProjectedPoint Point,
                                             Real32 NearDepth = WorkspaceCadNearDepth)
{
    return Point.W > NearDepth;
}

SLATE_SHARED WorkspaceCadProjectedPoint BlendWorkspaceCadProjectedPoint(
    WorkspaceCadProjectedPoint From,
    WorkspaceCadProjectedPoint Toward,
    Real32 Fraction)
{
    WorkspaceCadProjectedPoint Blended;
    Blended.X = From.X + (Toward.X - From.X) * Fraction;
    Blended.Y = From.Y + (Toward.Y - From.Y) * Fraction;
    Blended.W = From.W + (Toward.W - From.W) * Fraction;
    return Blended;
}

SLATE_SHARED WorkspaceCadProjectedPoint IntersectWorkspaceCadNear(
    WorkspaceCadProjectedPoint From,
    WorkspaceCadProjectedPoint Toward,
    Real32 NearDepth = WorkspaceCadNearDepth)
{
    const Real32 Reach = Toward.W - From.W;
    const Real32 Fraction = Reach != 0.0f
        ? Real32(BoundedMagnitude((NearDepth - From.W) / Reach, 0.0, 1.0))
        : 0.0f;

    WorkspaceCadProjectedPoint Hit = BlendWorkspaceCadProjectedPoint(From, Toward, Fraction);
    Hit.W = NearDepth;
    return Hit;
}

/// 🧩 Clips one projected segment against the CAD near plane.
/// out   Returned   [-] false when the segment lies wholly behind the plane; otherwise `Start` and `End`
///                     are rewritten to the surviving segment, with any crossing endpoint moved onto the
///                     plane itself.
SLATE_SHARED bool ClipWorkspaceCadSegmentNear(
    SLATE_INOUT(WorkspaceCadProjectedPoint) Start,
    SLATE_INOUT(WorkspaceCadProjectedPoint) End,
    Real32 NearDepth = WorkspaceCadNearDepth)
{
    const bool StartFront = WorkspaceCadProjectedFront(Start, NearDepth);
    const bool EndFront = WorkspaceCadProjectedFront(End, NearDepth);

    if (StartFront && EndFront)
        return true;
    if (!StartFront && !EndFront)
        return false;

    const WorkspaceCadProjectedPoint Hit = IntersectWorkspaceCadNear(Start, End, NearDepth);
    if (!StartFront)
        Start = Hit;
    else
        End = Hit;
    return true;
}

/// 🧩 Clips one projected fill triangle against the CAD near plane.
/// out   Returned   [-] 0 when the triangle lies wholly behind the plane, 3 when it remains a triangle,
///                     4 when clipping produces a quad that must be triangulated into two triangles.
SLATE_SHARED Unsigned32 ClipWorkspaceCadFillTriangleNear(
    WorkspaceCadProjectedPoint First,
    WorkspaceCadProjectedPoint Second,
    WorkspaceCadProjectedPoint Third,
    SLATE_INOUT_SPAN(WorkspaceCadProjectedPoint, Clipped, 4),
    Real32 NearDepth = WorkspaceCadNearDepth)
{
    WorkspaceCadProjectedPoint Input[3] = { First, Second, Third };
    Unsigned32 Count = 0u;

    WorkspaceCadProjectedPoint From = Input[2];
    bool FromFront = WorkspaceCadProjectedFront(From, NearDepth);

    for (Unsigned32 Index = 0u; Index < 3u; ++Index)
    {
        const WorkspaceCadProjectedPoint Toward = Input[Index];
        const bool TowardFront = WorkspaceCadProjectedFront(Toward, NearDepth);

        if (FromFront && TowardFront)
        {
            Clipped[Count++] = Toward;
        }
        else if (FromFront && !TowardFront)
        {
            Clipped[Count++] = IntersectWorkspaceCadNear(From, Toward, NearDepth);
        }
        else if (!FromFront && TowardFront)
        {
            Clipped[Count++] = IntersectWorkspaceCadNear(From, Toward, NearDepth);
            Clipped[Count++] = Toward;
        }

        From = Toward;
        FromFront = TowardFront;
    }

    return Count;
}

SLATE_SHARED WorkspaceCadScreenPoint ResolveWorkspaceCadScreenPoint(WorkspaceCadProjectedPoint Point)
{
    WorkspaceCadScreenPoint Screen;
    Screen.X = Point.X / Point.W;
    Screen.Y = Point.Y / Point.W;
    return Screen;
}

} // namespace Slate
