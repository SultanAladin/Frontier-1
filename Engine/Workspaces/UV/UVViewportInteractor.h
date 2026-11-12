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

    void ProcessViewportInput(const ViewportInteractionReading& Reading);

    [[nodiscard]] ModalUVTransformMode QueryModalTransformMode() const noexcept { return ModalMode; }
    [[nodiscard]] RegionSelectTool     QueryRegionSelectTool() const noexcept { return ActiveRegionTool; }

private:
    void HandleKeyboardShortcuts(const ViewportInteractionReading& Reading, bool InsidePlanar, bool InsideSpatial);
    void HandleModalTransformStep(const ViewportInteractionReading& Reading, bool InsidePlanar);
    void HandleGizmoOrSurfaceInteraction(const ViewportInteractionReading& Reading, bool InsidePlanar, bool InsideSpatial);
    void ApplySurfaceHitSelection(const GeometricRaster::ViewportPickHit& Hit, const PaneInputReading& Input, bool PlanarMetric);

    UVWorkspaceSession*   Session              = nullptr;
    bool                  DraggingGizmo2D      = false;
    bool                  DraggingGizmo3D      = false;
    std::vector<float>    DragBaselineUVStream {};
    std::vector<uint32_t> DragTargetFaces      {};
    Mathematics::UVAxes           DragPivotUV          { 0.5f, 0.5f };
    Mathematics::SpatialTransform ActiveGizmoTransform {};

    ModalUVTransformMode  ModalMode            = ModalUVTransformMode::Inactive;
    ModalAxisConstraint   ModalConstraint      = ModalAxisConstraint::Unconstrained;
    std::string           ModalNumericEntry    {};
    float                 ModalStartPointerX   = 0.0f;
    float                 ModalStartPointerY   = 0.0f;

    RegionSelectTool      ActiveRegionTool     = RegionSelectTool::Inactive;
    bool                  RegionStrokeActive   = false;
    float                 MarqueeAnchorLocalX  = 0.0f;
    float                 MarqueeAnchorLocalY  = 0.0f;
    float                 BrushRadiusPixels    = 22.0f;
};

} // namespace Frontier::Workspaces
