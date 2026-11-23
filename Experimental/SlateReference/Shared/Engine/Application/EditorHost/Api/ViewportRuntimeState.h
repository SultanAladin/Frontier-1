//============================================================================================================================================
//                                                   VIEWPORTRUNTIMESTATE.H
//============================================================================================================================================
// Per-leaf viewport state. Application-wide frame inputs live in EditorFrameContext; this type owns the
// state that must not leak between split viewport leaves.

#pragma once

#include <cmath>

#include "Shared/OverlayGeometry.slang.h"
#include "SlateWorkspace/Discipline/ViewportNavigation/Api/ViewportNavigation.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/CadProjection.h"
#include "SlateWorkspace/Discipline/WorkplaneCatalogue/Api/WorkplaneCatalogue.h"

namespace Slate
{

struct ViewportRuntimeState
{
    ProjectionTransit       Projection = {};
    ViewportStanding        Standing = {};
    WorkplaneName           ActiveWorkplane = {};
    ResolvedCamera          Camera = {};
    OverlayGeometry         Overlay = {};
    WorkspaceCadProjection  CadProjection = {};

    // 🔴 THE NAVIGATION GESTURE IS PER LEAF AND MUST SURVIVE THE FRAME. A press is not a drag until it
    //    has travelled, so the press point has to be remembered somewhere; and the leaf that owns a
    //    gesture is the one it started in, which cannot be known from a single tick's pointer sample.
    NavigationGesture       Navigation = {};

    bool                    WasParallel = false;
    std::uint32_t           UploadedOverlayGeneration = 0u;
    float                   UploadedOverlayScale = 0.0f;

    void BeginFrame()
    {
        Overlay.Reset();
    }

    void InvalidateOverlayUpload()
    {
        UploadedOverlayGeneration = 0u;
        UploadedOverlayScale = 0.0f;
    }

    bool NeedsOverlayUpload(float DrawablePixelScale) const
    {
        return Overlay.Generation != UploadedOverlayGeneration
            || std::fabs(UploadedOverlayScale - DrawablePixelScale) > 1.0e-6f;
    }

    void MarkOverlayUploaded(float DrawablePixelScale)
    {
        UploadedOverlayGeneration = Overlay.Generation;
        UploadedOverlayScale = DrawablePixelScale;
    }
};

} // namespace Slate
