/*============================================================================================================================================
                                                             BAKINGVIEWPORTPANE.H
============================================================================================================================================*/
// 🧩 Center pane for Baking Studio: toolbar controls, real GPU 3D / 2D PBR map viewports & right-click / Q Quick Overlay.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Texturing/BakingWorkspaceSession.h"

#include <imgui.h>

#include <cstdint>
#include <vector>

namespace Frontier::Workspaces
{

struct SpatialRenderCacheKey
{
    uint32_t        WidthPx               = 0u;
    uint32_t        HeightPx              = 0u;
    BakerStudyShape Study                 = BakerStudyShape::UtahTeapot;
    BakerTargetId   InspectedTarget       = BakerTargetId::TangentNormal;
    bool            SurfaceBaked          = false;
    bool            ShowHighWireOverlay   = false;
    bool            ShowCageOverlay       = false;
    float           FrontCageOffsetMeters = 0.0f;
    float           CameraYawRadians      = 0.0f;
    float           CameraPitchRadians    = 0.0f;
    float           CameraDistanceMeters  = 0.0f;
    float           CameraPivotX          = 0.0f;
    float           CameraPivotY          = 0.0f;
    float           CameraPivotZ          = 0.0f;
    uint64_t        AssetVersionTag       = 0u;

    [[nodiscard]] bool operator==(const SpatialRenderCacheKey&) const noexcept = default;
};

class BakingViewportPane
{
public:
    BakingViewportPane() noexcept = default;
    ~BakingViewportPane() noexcept;

    void AttachSession(BakingWorkspaceSession* TargetSession) noexcept;
    void PresentInImGuiPane(double DeltaSeconds, float ScaleFactor);

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  DeltaSeconds);

    void RenderTopLevelOverlays(
        PixelSpace&             Surface,
        Rectangle2D             WorkspaceBounds,
        const PaneInputReading& Input) noexcept;

    void ConfigureQuickOverlayOpen(bool Open) noexcept;
    [[nodiscard]] bool QueryQuickOverlayOpen() const noexcept;
    [[nodiscard]] bool QueryAnyOverlayOpen() const noexcept;
    void CloseAllOverlays() noexcept;
    void InvalidateCachedFrames() noexcept;

private:
    void RenderThreeDViewport(
        PixelSpace&             Surface,
        Rectangle2D             ViewportRect,
        const PaneInputReading& Input,
        bool                    OverlayUp);

    void RenderInspectedTwoDPreview(
        PixelSpace& Surface,
        Rectangle2D PreviewArea);

    static void UploadTexturePixels(
        ImTextureData& TargetTex,
        const uint8_t* RgbaPixels,
        uint32_t       WidthPx,
        uint32_t       HeightPx);

    BakingWorkspaceSession* Session            = nullptr;
    bool                    StudyDropdownOpen  = false;
    bool                    LayoutDropdownOpen = false;
    bool                    QuickOverlayOpen   = false;
    bool                    OrbitDragActive    = false;
    bool                    PanDragActive      = false;
    Point2D                 LastPointerPos     { 0.0f, 0.0f };
    float                   SheetScrollY       = 0.0f;
    Point2D                 QuickOverlayAnchor { 0.0f, 0.0f };
    Rectangle2D             LastStudyAnchor    {};
    Rectangle2D             LastLayoutAnchor   {};
    Rectangle2D             LastCanvasBounds   {};

    ImTextureData           SpatialTexture     {};
    ImTextureData           PlanarTexture      {};
    std::vector<uint8_t>    CachedSpatialRgba  {};
    SpatialRenderCacheKey   LastSpatialKey     {};
    uint64_t                LocalAssetRevision = 1u;
};

} // namespace Frontier::Workspaces
