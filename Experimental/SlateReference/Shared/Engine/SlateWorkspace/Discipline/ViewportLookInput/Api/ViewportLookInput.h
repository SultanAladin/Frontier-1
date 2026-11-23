//============================================================================================================================================
//                                                     VIEWPORTLOOKINPUT.H
//============================================================================================================================================
// 🧩 Arbitration between the viewport's secondary look gesture and sketch keyboard tools. The host owns
//    pointer sampling and camera lifetime; this unit owns the device-neutral input transformation.
//
// 🔴 WASDEQ belongs to the fly camera only while the look gesture is held. Filtering it at the workspace
//    boundary prevents the same key from also starting a sketch tool or transform in that tick.

#pragma once

#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"

namespace Slate
{

bool IsViewportLookNavigationKey(char Character);
TextInputCondition FilterViewportLookTextInput(const TextInputCondition& Incoming,
                                               bool LookHeld);

} // namespace Slate
