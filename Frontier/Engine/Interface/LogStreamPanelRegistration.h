/*==========================================================================================================================================
                                                       LOG STREAM PANEL REGISTRATION
                                     ------------------------------------------------------------------
                        Factory that registers the Log Stream panel with the shared dockspace (interface SDK builds
                                                                   only).
==========================================================================================================================================*/

// 🧩 Factory that registers the Log Stream panel with the shared dockspace (interface SDK builds only).

#pragma once

#include "Engine/Interface/PanelDockspace.h"

namespace Frontier
{

class LogStreamPanel;

// Builds the dock registration for one Log Stream panel. The host owns the panel instance and calls
// LogStreamPanel::Synchronize each frame before PanelDockspace::ConstructInterface draws it.
[[nodiscard]] DockPanelSpecification MakeLogStreamPanelSpecification(LogStreamPanel& Panel);

} // namespace Frontier
