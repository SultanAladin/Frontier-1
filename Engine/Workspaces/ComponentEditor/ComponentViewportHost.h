/*============================================================================================================================================
                                                         COMPONENTVIEWPORTHOST.H
============================================================================================================================================*/
// 🧩 Centre Viewport & Assembly Sequence host: class header, 7-channel inspection bar, Vulkan .slang canvas, axis orb & SimHud

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/GeometricRaster/ComponentEditor/ComponentViewportRenderer.h"
#include "Engine/Workspaces/ComponentEditor/ComponentWorkspaceSession.h"

#include <imgui.h>
#include <cstdint>
#include <vector>

namespace Frontier::Workspaces::ComponentEditor
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ComponentViewportHost
{
public:
    void AttachRenderer(GeometricRaster::ComponentEditor::ComponentViewportRenderer* RendererPtr) noexcept;

    void Render(
        PixelSpace&                Surface,
        Rectangle2D                Bounds,
        ComponentWorkspaceSession& Session,
        const PaneInputReading&    Input) noexcept;

private:
    void UploadRgbaTexture(const std::vector<uint8_t>& Rgba, uint32_t W, uint32_t H) noexcept;

    GeometricRaster::ComponentEditor::ComponentViewportRenderer* Renderer = nullptr;
    ImTextureData                                                ViewportTexture {};
    uint64_t                                                     CachedSignature = 0ull;
    uint32_t                                                     CachedWidthPx   = 0u;
    uint32_t                                                     CachedHeightPx  = 0u;
    bool                                                         OrbitDragging   = false;
    bool                                                         PanDragging     = false;
};

} // namespace Frontier::Workspaces::ComponentEditor
