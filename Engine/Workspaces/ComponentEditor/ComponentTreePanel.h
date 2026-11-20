/*============================================================================================================================================
                                                          COMPONENTTREEPANEL.H
============================================================================================================================================*/
// 🧩 Left-dock Components hierarchy tree (metric cards, + Add grouped sheet, SpatialOrigin tree, Behaviours, drag-reparent, 3-hint footer)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/ComponentEditor/ComponentWorkspaceSession.h"

#include <imgui.h>
#include <string>
#include <unordered_map>

namespace Frontier::Workspaces::ComponentEditor
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ComponentTreePanel
{
public:
    void Render(
        PixelSpace&                Surface,
        Rectangle2D                Bounds,
        ComponentWorkspaceSession& Session,
        const PaneInputReading&    Input,
        double                     DeltaSeconds) noexcept;

private:
    ImTextureData* AcquireThumbnailTexture(
        const std::string&               AssetId,
        const ComponentWorkspaceSession& Session) noexcept;

    float ScrollOffsetPx = 0.0f;
    ComponentClass::ComponentInstanceToken DraggedToken {};
    std::unordered_map<uint32_t, bool> CollapsedNodes {};
    std::unordered_map<std::string, ImTextureData> ThumbnailTextures {};
};

} // namespace Frontier::Workspaces::ComponentEditor
