/*==========================================================================================================================================
                                                     MATERIALVIEWPORTHOST.H
==========================================================================================================================================*/
// 🧩 Center Direct Studio OpenPBR viewport turntable with specimen geometry switcher, Render Mode bar, click-to-impact & live playback

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Material/MaterialPreviewRenderer.h"
#include "Engine/Workspaces/Material/MaterialWorkspaceSession.h"

#include <imgui.h>

#include <vector>

namespace Frontier::Workspaces
{

class MaterialViewportHost
{
public:
    MaterialViewportHost() = default;
    ~MaterialViewportHost();

    MaterialViewportHost(const MaterialViewportHost&)            = delete;
    MaterialViewportHost& operator=(const MaterialViewportHost&) = delete;

    [[nodiscard]] MaterialPreviewRenderer& AccessRenderer() noexcept { return PreviewRenderer; }

    void Render(
        PixelSpace&               Surface,
        Rectangle2D               Bounds,
        MaterialWorkspaceSession& Session,
        const ControlPointer&     Pointer) noexcept;

    [[nodiscard]] Rectangle2D QueryLastPreviewImageRect() const noexcept { return LastImageRect; }

private:
    void UploadRgbaTexture(const uint8_t* Pixels, uint32_t WidthPx, uint32_t HeightPx) noexcept;

    MaterialPreviewRenderer PreviewRenderer    {};
    ImTextureData           PreviewTexture     {};
    std::vector<uint8_t>    CachedPreviewRgba8 {};
    uint32_t                CachedWidthPx      = 0u;
    uint32_t                CachedHeightPx     = 0u;
    Rectangle2D             LastImageRect      {};
};

} // namespace Frontier::Workspaces
