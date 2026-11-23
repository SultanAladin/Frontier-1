//============================================================================================================================================
//                                                     EDITORFRAMECONTEXT.H
//============================================================================================================================================
// A frame-wide, immutable snapshot shared by the editor coordinators. Viewport-leaf state belongs in the
// next refactor; this type deliberately contains only values that describe the current host tick.

#pragma once

#include "SlateRuntime/Session/SessionSequence/Api/SessionSequence.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/DrawableScale.h"
#include "Application/EditorHost/Api/PointerDispatch.h"

namespace Slate
{

/// The frame inputs which must not be independently recomputed by UI, camera, interaction, and rendering.
///
/// The host owns the snapshot for one tick. Consumers may read it, but must not manufacture a second
/// pointer/display-scale interpretation from the underlying interface during the same tick.
struct EditorFrameContext
{
    SessionPass       Pass = {};
    PointerCondition  Pointer = {};
    PointerCondition  BackgroundPointer = {};
    DrawableScale     Drawable = {};
    PlaneExtent       Whole = {};
    PlaneExtent       NorthInterior = {};
    PlaneExtent       SouthInterior = {};
    bool              PointerOverViewport = false;
    bool              PointerBehindDrawer = false;
    PointerDispatchResult Dispatch = {};
};

} // namespace Slate
