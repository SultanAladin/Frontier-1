/*============================================================================================================================================
                                                         COMPONENTCONTENTTRAY.H
============================================================================================================================================*/
// 🧩 Left-dock Content tab: Import object (OBJ/glTF/GLB/STL/PLY/FCT1), compiled asset tiles with 128x128 thumbnails, status chips & actions

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

class ComponentContentTray
{
public:
    void Render(
        PixelSpace&                Surface,
        Rectangle2D                Bounds,
        ComponentWorkspaceSession& Session,
        const PaneInputReading&    Input) noexcept;

private:
    ImTextureData* AcquireThumbnailTexture(
        const std::string&               AssetId,
        const ComponentWorkspaceSession& Session) noexcept;

    float ScrollOffsetPx = 0.0f;
    std::unordered_map<std::string, ImTextureData> ThumbnailTextures {};
};

} // namespace Frontier::Workspaces::ComponentEditor
