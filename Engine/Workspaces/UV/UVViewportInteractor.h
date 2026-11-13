/*============================================================================================================================================
                                                          UVVIEWPORTINTERACTOR.H
============================================================================================================================================*/
// 🧩 Interactive 2D/3D UV viewport interactor driving camera orbit/pan/zoom, GPU hover/click/marquee/brush picking, gizmo drag & G/R/S

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/ViewportProjectionPane.h"
#include "Engine/Workspaces/UV/UVWorkspaceSession.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ModalUVTransformMode : uint8_t
{
    Inactive      = 0u,
    GrabTranslate = 1u,
    RotateAngle   = 2u,
    ScaleUniform  = 3u,
};

enum class ModalAxisConstraint : uint8_t
{
    Unconstrained = 0u,
    LockU         = 1u,
    LockV         = 2u,
};

enum class RegionSelectTool : uint8_t
{
    Inactive    = 0u,
    BoxMarquee  = 1u,
    CircleBrush = 2u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      DECLARATIONS
//------------------------------------------------------------------------------------------------------------------------

class UVViewportInteractor
{
public:
    UVViewportInteractor() = default;

    void AttachSession(UVWorkspaceSession* TargetSession) noexcept;

    void ProcessViewportInput(const ViewportInteractionReading& Reading, uint64_t HistoryTickSequence = 0u);

    void ActivateBoxMarquee(bool Active) noexcept;
    void ActivateCircleBrush(bool Active) noexcept;
    void ResetActiveTools() noexcept;

    [[nodiscard]] ModalUVTransformMode QueryModalTransformMode() const noexcept { return ModalMode; }
    [[nodiscard]] RegionSelectTool     QueryRegionSelectTool() const noexcept { return ActiveRegionTool; }
    [[nodiscard]] bool                 QueryRegionStrokeActive() const noexcept { return RegionStrokeActive; }
    [[nodiscard]] bool                 QueryRegionStrokePlanar() const noexcept { return RegionTargetPlanar; }
    [[nodiscard]] bool                 QueryRegionBrushHoverActive() const noexcept
    {
        return ActiveRegionTool == RegionSelectTool::CircleBrush && RegionHoverValid;
    }
    [[nodiscard]] Rectangle2D          QueryMarqueeOverlayRect() const noexcept;
    [[nodiscard]] Point2D              QueryBrushOverlayCenter() const noexcept { return LastRegionCursorLogical; }
    [[nodiscard]] float                QueryBrushRadiusPixels() const noexcept { return BrushRadiusPixels; }

private:
    void HandleKeyboardShortcuts(const ViewportInteractionReading& Reading, bool InsidePlanar, bool InsideSpatial);
    void HandleModalTransformStep(const ViewportInteractionReading& Reading, bool InsidePlanar);
    void HandleGizmoOrSurfaceInteraction(const ViewportInteractionReading& Reading, bool InsidePlanar, bool InsideSpatial);
    void ApplySurfaceHitSelection(const GeometricRaster::ViewportPickHit& Hit, const PaneInputReading& Input, bool PlanarMetric);

    UVWorkspaceSession*   Session              = nullptr;
    bool                  DraggingGizmo2D      = false;
    bool                  DraggingGizmo3D      = false;
    std::vector<float>                 DragBaselineUVStream       {};
    std::vector<uint32_t>              DragTargetFaces             {};
    std::vector<Mathematics::Axis3D>   DragBaselineObjectVertices {};
    Mathematics::UVAxes                DragPivotUV                 { 0.5f, 0.5f };
    Mathematics::Axis3D                DragPivot3D                 {};
    Mathematics::SpatialTransform      LastAppliedObjectTransform  {};
    Mathematics::SpatialTransform      ActiveGizmoTransform        {};

    ModalUVTransformMode  ModalMode            = ModalUVTransformMode::Inactive;
    ModalAxisConstraint   ModalConstraint      = ModalAxisConstraint::Unconstrained;
    std::string           ModalNumericEntry    {};
    float                 ModalStartPointerX   = 0.0f;
    float                 ModalStartPointerY   = 0.0f;

    RegionSelectTool      ActiveRegionTool     = RegionSelectTool::Inactive;
    bool                  RegionStrokeActive   = false;
    bool                  RegionTargetPlanar   = true;
    bool                  RegionHoverValid     = false;
    float                 MarqueeAnchorLocalX  = 0.0f;
    float                 MarqueeAnchorLocalY  = 0.0f;
    Point2D               LastRegionCursorLogical {};
    float                 BrushRadiusPixels    = 22.0f;
    uint64_t              FallbackTickSequence = 0u;
};

} // namespace Frontier::Workspaces
