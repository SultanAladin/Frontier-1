/*============================================================================================================================================
                                                         PATTERNSTAMPTOOLHEADERPANE.H
============================================================================================================================================*/
// 🧩 Central Pattern Studio / Stamp Studio viewport & chrome pane: Band 1 title/res, Band 2 circular tool dock + More tools overlay sheet,
//    View/Undo/Redo/Save/Bridge, crisp ruler chrome, GPU VectorCanvasRasterizer via ViewportProjectionPane (Planar2DOnly), and zoom controls.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Panes/ViewportProjectionPane.h"
#include "Engine/GeometricRaster/Vector/VectorCanvasRasterizer.h"
#include "Engine/Workspaces/Texturing/PatternStampStackSession.h"
#include "Engine/Workspaces/Texturing/TexturingPaneAppearance.h"
#include "Engine/Workspaces/Texturing/VectorViewportInteractor.h"

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  CLASS DECLARATION
//------------------------------------------------------------------------------------------------------------------------

class PatternStampToolHeaderPane
{
public:
    PatternStampToolHeaderPane();
    ~PatternStampToolHeaderPane() noexcept;

    void AttachSession(PatternStampWorkspaceSession* TargetSession) noexcept;
    void AttachVulkanRasterizer(GeometricRaster::VectorCanvasRasterizer* TargetRasterizer) noexcept;
    void AssignBridgeCallback(std::function<void(std::string_view CommandTag)> Callback) noexcept
    {
        BridgeCallback = std::move(Callback);
    }
    void AssignCanvasBoundsCallback(std::function<void(Rectangle2D WindowLogicalBounds)> Callback) noexcept
    {
        CanvasBoundsCallback = std::move(Callback);
    }

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  DeltaSeconds);

    void PresentInImGuiPane(double DeltaSeconds, float ScaleFactor = 1.0f);

    [[nodiscard]] VectorViewportInteractor&       AccessInteractor() noexcept       { return Interactor; }
    [[nodiscard]] const VectorViewportInteractor& InspectInteractor() const noexcept { return Interactor; }
    [[nodiscard]] Rectangle2D                     QueryCanvasWindowLogicalBounds() const noexcept { return LatestCanvasWindowLogicalRect; }
    [[nodiscard]] Rectangle2D                     QueryLatestCanvasLogicalRect() const noexcept { return LatestCanvasLogicalRect; }
    [[nodiscard]] GeometricRaster::VectorCanvasRasterizer* QueryAttachedRasterizer() const noexcept { return Rasterizer; }

public:
    bool MoreToolsSheetOpen  = false;
    bool PresetsSheetOpen    = false;
    bool ResolutionSheetOpen = false;
    bool ViewSheetOpen       = false;
    bool SaveSheetOpen       = false;
    bool SnapSheetOpen       = false;

private:
    void CloseAllSheets() noexcept;

    PatternStampWorkspaceSession*                          Session                       = nullptr;
    GeometricRaster::VectorCanvasRasterizer*               Rasterizer                    = nullptr;
    ViewportProjectionPane                                 ProjectionPane                {};
    VectorViewportInteractor                               Interactor                    {};
    std::function<void(std::string_view CommandTag)>       BridgeCallback                {};
    std::function<void(Rectangle2D WindowLogicalBounds)>   CanvasBoundsCallback          {};
    TextEditBuffer                                         NameEditBuffer                {};
    float                                                  ViewSheetScrollY              = 0.0f;
    std::vector<uint8_t>                                   LatestCanvasRGBA              {};
    uint32_t                                               LatestCanvasW                 = 0u;
    uint32_t                                               LatestCanvasH                 = 0u;
    Rectangle2D                                            LatestCanvasLogicalRect       { 24.0f, 122.0f, 700.0f, 600.0f };
    Rectangle2D                                            LatestCanvasWindowLogicalRect { 360.0f, 140.0f, 700.0f, 600.0f };
};

} // namespace Frontier::Workspaces
