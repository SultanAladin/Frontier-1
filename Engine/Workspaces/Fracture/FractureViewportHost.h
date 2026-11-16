/*============================================================================================================================================
                                                          FRACTUREVIEWPORTHOST.H
============================================================================================================================================*/
// 🧩 Central Fracture Studio viewport host: 42px viewport bar, OpenPBR/Energy/Cells/Normals 3D canvas, Reticle, ExecutionModeStrip & Timeline

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/PixelSpace.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Workspaces/Fracture/FractureViewportRenderer.h"
#include "Engine/Workspaces/Fracture/FractureWorkspaceSession.h"

#include <imgui.h>

#include <cstdint>

namespace Frontier::Workspaces::Fracture
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class FractureViewportHost
{
public:
    FractureViewportHost() = default;
    ~FractureViewportHost() noexcept;

    FractureViewportHost(const FractureViewportHost&)            = delete;
    FractureViewportHost& operator=(const FractureViewportHost&) = delete;

    void AttachRenderer(FractureViewportRenderer* TargetRenderer) noexcept;
    void InvalidateRenderCache() noexcept;

    void Render(
        PixelSpace&               Surface,
        Rectangle2D               Bounds,
        FractureWorkspaceSession& Session,
        const ControlPointer&     Pointer,
        float                     WheelDeltaY = 0.0f) noexcept;

private:
    void UploadRgbaTexture(const std::vector<uint8_t>& RgbaPixels, uint32_t WidthPx, uint32_t HeightPx);

    FractureViewportRenderer* Renderer          = nullptr;
    FractureViewportRenderer  FallbackRenderer  {};
    ImTextureData             ViewportTexture   {};
    uint64_t                  CachedSignature   = 0ull;
    uint32_t                  CachedWidthPx     = 0u;
    uint32_t                  CachedHeightPx    = 0u;
    bool                      OrbitDragging     = false;
    float                     LastDragX         = 0.0f;
    float                     LastDragY         = 0.0f;
    bool                      ExplodeSliderDrag = false;
};

} // namespace Frontier::Workspaces::Fracture
