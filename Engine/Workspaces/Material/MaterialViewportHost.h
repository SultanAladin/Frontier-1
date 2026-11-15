/*============================================================================================================================================
                                                            MATERIALVIEWPORTHOST.H
============================================================================================================================================*/
// 🧩 Native Frontier Material Viewport pane styled identically to Pattern/Baking Studio (Direct OpenPBR preview + orbit + impact pulse)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Material/MaterialPreviewRenderer.h"
#include "Engine/Workspaces/Material/MaterialWorkspaceSession.h"

#include <imgui.h>

#include <cstdint>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          TYPES
//------------------------------------------------------------------------------------------------------------------------

class MaterialViewportHost
{
public:
    MaterialViewportHost() = default;
    ~MaterialViewportHost() noexcept;

    MaterialViewportHost(const MaterialViewportHost&)            = delete;
    MaterialViewportHost& operator=(const MaterialViewportHost&) = delete;

    void AttachSession(MaterialWorkspaceSession* TargetSession) noexcept;
    void AttachRenderer(MaterialPreviewRenderer* TargetRenderer) noexcept;
    void AssignBottomOcclusionHeight(float OcclusionLogicalPx) noexcept;
    void InvalidatePreviewCache() noexcept;

    void PresentImGuiPane(double DeltaSeconds, float ScaleFactor = 1.0f);

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  DeltaSeconds);

    [[nodiscard]] Rectangle2D QueryCanvasLogicalBounds() const noexcept { return LastCanvasBounds; }
    [[nodiscard]] Rectangle2D QueryScreenBounds() const noexcept { return LastScreenBounds; }
    [[nodiscard]] bool        QueryAnyDropdownOpen() const noexcept;
    void                      CloseDropdowns() noexcept;

private:
    void UploadPreviewTexture(const std::vector<uint8_t>& RgbaPixels, uint32_t WidthPx, uint32_t HeightPx);

    MaterialWorkspaceSession* Session          = nullptr;
    MaterialPreviewRenderer*  PreviewRenderer  = nullptr;

    ImTextureData             PreviewTexture   {};
    uint32_t                  CachedWidthPx    = 0u;
    uint32_t                  CachedHeightPx   = 0u;
    uint64_t                  LastSignature    = 0u;
    double                    LastRenderClock  = -1.0;
    float                     LastYawRad       = 0.0f;
    float                     LastPitchRad     = 0.0f;
    float                     LastDistMeters   = 0.0f;
    uint8_t                   LastSpecimenCode = 255u;
    uint8_t                   LastModeCode     = 255u;

    float                     BottomOcclusionPx    = 0.0f;
    bool                      PresetDropdownOpen   = false;
    bool                      SpecimenDropdownOpen = false;
    bool                      LightingDropdownOpen = false;
    Rectangle2D               PresetAnchor         {};
    Rectangle2D               SpecimenAnchor       {};
    Rectangle2D               LightingAnchor       {};
    float                     SheetScrollY         = 0.0f;

    bool                      OrbitDragging    = false;
    float                     DragAnchorX      = 0.0f;
    float                     DragAnchorY      = 0.0f;
    float                     DragStartYaw     = 0.0f;
    float                     DragStartPitch   = 0.0f;
    Rectangle2D               LastCanvasBounds {};
    Rectangle2D               LastScreenBounds {};
};

} // namespace Frontier::Workspaces
