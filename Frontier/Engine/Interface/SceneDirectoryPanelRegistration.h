/*==========================================================================================================================================
                                                  SCENE DIRECTORY PANEL REGISTRATION
                                     ------------------------------------------------------------------
                          Factory that registers the Outliner panel with the shared dockspace (interface SDK
                                                                   builds only).
==========================================================================================================================================*/

// 🧩 Factory that registers the Outliner panel with the shared dockspace (interface SDK builds only).

#pragma once

#include "Engine/Interface/PanelDockspace.h"

namespace Frontier
{

class SceneDirectoryPanel;

// Builds the dock registration for one Outliner panel. The host owns the panel instance, calls
// SceneDirectoryPanel::Synchronize each frame before PanelDockspace::ConstructInterface draws it, and
// services TakeSpawnRequest() (records are engine objects; the panel never invents them).
[[nodiscard]] DockPanelSpecification MakeSceneDirectoryPanelSpecification(SceneDirectoryPanel& Panel);

} // namespace Frontier
