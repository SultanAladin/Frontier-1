/*============================================================================================================================================
                                                          FRACTUREVIEWPORTHOST.H
============================================================================================================================================*/
// 🧩 Central Fracture Studio viewport host: ExecutionModeStrip, OpenPBR/Energy/Cells/Normals 3D canvas, DiagnosticsOverlay & SimulationTimelineFooter

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"
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
        float                     WheelDeltaY = 0.0f,
        float                     Opacity     = 1.0f) noexcept;

private:
    void UploadRgbaTexture(const std::vector<uint8_t>& RgbaPixels, uint32_t WidthPx, uint32_t HeightPx);
    void PopulateDiagnosticsChannels(
        const FractureWorkspaceSession&        Session,
        std::vector<DiagnosticsMetricChannel>& OutChannels) const;

    FractureViewportRenderer* Renderer         = nullptr;
    ImTextureData             ViewportTexture  {};
    uint64_t                  LastSignature    = 0ull;
    uint32_t                  LastWidthPx      = 0u;
    uint32_t                  LastHeightPx     = 0u;
    bool                      OrbitDragging    = false;
    bool                      PanDragging      = false;
    Point2D                   DragStartPos     {};
    Point2D                   LastDragPos      {};
    float                     DragMovedPx      = 0.0f;
};

} // namespace Frontier::Workspaces::Fracture
