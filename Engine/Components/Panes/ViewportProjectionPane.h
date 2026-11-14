/*============================================================================================================================================
                                                        VIEWPORTPROJECTIONPANE.H
============================================================================================================================================*/
// 🧩 Reusable PixelSpace single/split 2D+3D GPU viewport pane bound to WorkspaceDocument & blitted via PixelSpace::FillTexturedRectangle

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Editor/Viewports/ViewportProjection.h"
#include "Engine/Workspaces/WorkspaceDocument.h"

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ViewportToolbarChip
{
    std::string           Caption      {};            // [-] - Button label rendered in top projection strip
    bool                  PrimaryStyle = false;       // [-] - Highlighted command capsule when true
    std::function<void()> ActivationCallback  {};            // [-] - Invoked on primary click
};

struct ViewportRasterBuffer
{
    const uint8_t* RgbaPixels = nullptr; // [-]  - Row-major RGBA8 GPU framebuffer bytes
    uint32_t       WidthPx    = 0u;      // [px] - Framebuffer pixel width
    uint32_t       HeightPx   = 0u;      // [px] - Framebuffer pixel height
};

struct DualViewportFramebuffers
{
    ViewportRasterBuffer Spatial3D {}; // [-] - 3D spatial framebuffer slice
    ViewportRasterBuffer Planar2D  {}; // [-] - 2D planar/tile framebuffer slice
};

struct ViewportInteractionReading
{
    Rectangle2D             PlanarBounds  {}; // [px] - Logical rectangle of 2D planar canvas (0 width when hidden)
    Rectangle2D             SpatialBounds {}; // [px] - Logical rectangle of 3D spatial viewport (0 width when hidden)
    const PaneInputReading& Input;            // [-]  - Current step pointer, wheel, modifier & keyboard reading
};

class ViewportProjectionPane
{
public:
    using RasterBufferSyncCallback = std::function<DualViewportFramebuffers(
        uint32_t SpatialWidthPx,
        uint32_t SpatialHeightPx,
        uint32_t PlanarWidthPx,
        uint32_t PlanarHeightPx)>;

    using SplitModeChangedCallback    = std::function<void(Editor::ViewportSplitMode NewMode)>;
    using SplitRatioChangedCallback   = std::function<void(float NewRatio)>;
    using ViewportInteractionCallback = std::function<void(const ViewportInteractionReading& Reading)>;

    ViewportProjectionPane();
    ~ViewportProjectionPane();

    ViewportProjectionPane(const ViewportProjectionPane&)            = delete;
    ViewportProjectionPane& operator=(const ViewportProjectionPane&) = delete;

    void AttachDocument(Workspaces::WorkspaceDocument* TargetDocument) noexcept;
    void ConfigurePaneIndicators(std::string_view PlanarIndicator, std::string_view SpatialIndicator);
    void ConfigureToolbarChips(std::vector<ViewportToolbarChip> Chips);
    void ConfigureStatusBanner(std::string_view BannerText);
    void ConfigureLockedSplitMode(std::optional<Editor::ViewportSplitMode> Mode) noexcept { LockedSplitMode = Mode; }
    void AssignRasterBufferSource(RasterBufferSyncCallback SourceCallback);
    void AssignSplitModeCallback(SplitModeChangedCallback Callback);
    void AssignSplitRatioCallback(SplitRatioChangedCallback Callback);
    void AssignInteractionCallback(ViewportInteractionCallback Callback);

    [[nodiscard]] Editor::ViewportProjection&       AccessProjection() noexcept;
    [[nodiscard]] const Editor::ViewportProjection& InspectProjection() const noexcept;
    [[nodiscard]] Editor::ViewportSplitMode         QueryEffectiveSplitMode() const noexcept;
    [[nodiscard]] bool                              QuerySplitterDragActive() const noexcept { return SplitterDragActive; }

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  Δt);

    void RenderViewportCanvas(
        PixelSpace&             Surface,
        Rectangle2D             CanvasBounds,
        const PaneInputReading& Input,
        bool                    AllowInteraction,
        Rectangle2D*            OutPlanarRect  = nullptr,
        Rectangle2D*            OutSpatialRect = nullptr);

    void PresentInImGuiPane(double Δt, float ScaleFactor = 1.0f);

private:
    static void UploadTexturePixels(ImTextureData& TargetTex, const ViewportRasterBuffer& Source);

    Workspaces::WorkspaceDocument*   BoundDocument        = nullptr;
    Editor::ViewportProjection       FallbackProjection   {};
    std::string                      PlanarIndicatorTitle     { "2D PLANAR CANVAS + GIZMO" };
    std::string                      SpatialIndicatorTitle    { "3D PROJECTION + MATCAP + GIZMO" };
    std::string                      StatusBanner         {};
    std::vector<ViewportToolbarChip> DomainChips          {};
    RasterBufferSyncCallback          RasterBufferSource       {};
    SplitModeChangedCallback         SplitChangedNotifier     {};
    SplitRatioChangedCallback        SplitRatioNotifier       {};
    ViewportInteractionCallback      InteractionCallback      {};
    ImTextureData                    PlanarTexture            {};
    ImTextureData                    SpatialTexture           {};
    std::optional<Editor::ViewportSplitMode> LockedSplitMode  {};
    bool                             SplitterDragActive       = false;
};

} // namespace Frontier
