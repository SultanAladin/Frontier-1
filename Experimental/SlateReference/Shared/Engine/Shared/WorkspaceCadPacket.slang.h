//============================================================================================================================================
//                                                       WORKSPACECADPACKET.SLANG.H
//============================================================================================================================================
// 🧩 The parametric workspace's CPU-side CAD drawing record — bounded planar segments, fill triangles and
//    markers the dedicated CAD pass consumes. The exact sketch/profile/solid declarations remain authoritative;
//    this record is presentational only.

#pragma once

#include "Shared/ToolchainInterchange.slang.h"

namespace Slate
{

enum class WorkspaceCadMarkerSubject : Unsigned32
{
    SketchPoint = 0u,
    SketchControl = 1u,
    Dimension = 2u,
    Constraint = 3u,
    Snap = 4u,
    SubjectCount = 5u
};

struct WorkspaceCadSegment
{
    Real32 Along0 = 0.0f;
    Real32 Across0 = 0.0f;
    Real32 Along1 = 0.0f;
    Real32 Across1 = 0.0f;
    Unsigned32 Packed = 0xFFFFFFFFu;
    Real32 Thickness = 1.0f;
};

struct WorkspaceCadFillTriangle
{
    Real32 Along0 = 0.0f;
    Real32 Across0 = 0.0f;
    Real32 Along1 = 0.0f;
    Real32 Across1 = 0.0f;
    Real32 Along2 = 0.0f;
    Real32 Across2 = 0.0f;
    Unsigned32 Packed = 0xFFFFFFFFu;
};

struct WorkspaceCadMarker
{
    Real32 Along = 0.0f;
    Real32 Across = 0.0f;
    Unsigned32 Packed = 0xFFFFFFFFu;
    Real32 Radius = 1.0f;
    Unsigned32 Subject = 0u;
};

SLATE_SHARED Unsigned32 PackWorkspaceCadColour(Unsigned32 Red, Unsigned32 Green,
                                               Unsigned32 Blue, Unsigned32 Alpha)
{
    return (Alpha << 24u) | (Red << 16u) | (Green << 8u) | Blue;
}

// 🧩 How a point in the sketch plane becomes a point on the display.
// 📝 ⚠️ Lives beside the packet rather than in `SlateVulkan` because it is six floats and a size: the pass
//    CONSUMES it, but computing it is geometry, and putting it here is what lets the workspace build one
//    without depending on the graphics device.
struct WorkspaceCadProjection
{
    Real32 DisplayWidth = 1.0f;
    Real32 DisplayHeight = 1.0f;
    Real32 Projection0[4] = {};
    Real32 Projection1[4] = {};
    Real32 Projection2[4] = {};
};

struct WorkspaceCadPacket
{
    static constexpr Unsigned32 SegmentLimit = 4096u;
    static constexpr Unsigned32 FillLimit = 1024u;
    static constexpr Unsigned32 MarkerLimit = 1024u;

    WorkspaceCadSegment Segments[SegmentLimit] = {};
    Unsigned32 SegmentCount = 0u;
    WorkspaceCadFillTriangle Fills[FillLimit] = {};
    Unsigned32 FillCount = 0u;
    WorkspaceCadMarker Markers[MarkerLimit] = {};
    Unsigned32 MarkerCount = 0u;
    Unsigned32 Generation = 0u;

    Real32 MinimumAlong = 0.0f;
    Real32 MinimumAcross = 0.0f;
    Real32 MaximumAlong = 0.0f;
    Real32 MaximumAcross = 0.0f;
    bool ExtentStanding = false;

    void Reset()
    {
        SegmentCount = 0u;
        FillCount = 0u;
        MarkerCount = 0u;
        MinimumAlong = 0.0f;
        MinimumAcross = 0.0f;
        MaximumAlong = 0.0f;
        MaximumAcross = 0.0f;
        ExtentStanding = false;
        ++Generation;
    }

    void Extend(Real32 Along, Real32 Across)
    {
        if (!ExtentStanding)
        {
            MinimumAlong = MaximumAlong = Along;
            MinimumAcross = MaximumAcross = Across;
            ExtentStanding = true;
            return;
        }

        if (Along < MinimumAlong) MinimumAlong = Along;
        if (Across < MinimumAcross) MinimumAcross = Across;
        if (Along > MaximumAlong) MaximumAlong = Along;
        if (Across > MaximumAcross) MaximumAcross = Across;
    }

    void AddSegment(Real32 Along0, Real32 Across0,
                    Real32 Along1, Real32 Across1,
                    Unsigned32 Packed, Real32 Thickness = 1.0f)
    {
        if (SegmentCount >= SegmentLimit)
            return;

        WorkspaceCadSegment& Written = Segments[SegmentCount++];
        Written.Along0 = Along0;
        Written.Across0 = Across0;
        Written.Along1 = Along1;
        Written.Across1 = Across1;
        Written.Packed = Packed;
        Written.Thickness = Thickness;
        Extend(Along0, Across0);
        Extend(Along1, Across1);
        ++Generation;
    }

    void AddFill(Real32 Along0, Real32 Across0,
                 Real32 Along1, Real32 Across1,
                 Real32 Along2, Real32 Across2,
                 Unsigned32 Packed)
    {
        if (FillCount >= FillLimit)
            return;

        WorkspaceCadFillTriangle& Written = Fills[FillCount++];
        Written.Along0 = Along0;
        Written.Across0 = Across0;
        Written.Along1 = Along1;
        Written.Across1 = Across1;
        Written.Along2 = Along2;
        Written.Across2 = Across2;
        Written.Packed = Packed;
        Extend(Along0, Across0);
        Extend(Along1, Across1);
        Extend(Along2, Across2);
        ++Generation;
    }

    void AddMarker(Real32 Along, Real32 Across,
                   Unsigned32 Packed, Real32 Radius,
                   WorkspaceCadMarkerSubject Subject)
    {
        if (MarkerCount >= MarkerLimit)
            return;

        WorkspaceCadMarker& Written = Markers[MarkerCount++];
        Written.Along = Along;
        Written.Across = Across;
        Written.Packed = Packed;
        Written.Radius = Radius;
        Written.Subject = Unsigned32(Subject);
        Extend(Along - Radius, Across - Radius);
        Extend(Along + Radius, Across + Radius);
        ++Generation;
    }
};

} // namespace Slate
