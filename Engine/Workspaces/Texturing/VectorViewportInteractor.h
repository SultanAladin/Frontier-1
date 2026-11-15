/*============================================================================================================================================
                                                          VECTORVIEWPORTINTERACTOR.H
============================================================================================================================================*/
// 🧩 Interactive 2D vector viewport interactor: GPU pixel/region picking, Planar2D TransformGizmo, Pen/Curve/EditPoints/PointGradient,
//    Pan/Zoom, modal Grab/Rotate/Scale, and Pattern/Stamp keyboard shortcuts.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/ViewportProjectionPane.h"
#include "Engine/GeometricRaster/Gizmo/TransformGizmo.h"
#include "Engine/GeometricRaster/Vector/VectorCanvasRasterizer.h"
#include "Engine/Workspaces/Texturing/PatternStampStackSession.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ModalVectorTransformMode : uint8_t
{
    Inactive      = 0u,
    GrabTranslate = 1u,
    RotateAngle   = 2u,
    ScaleUniform  = 3u,
};

enum class VectorAxisConstraint : uint8_t
{
    Unconstrained = 0u,
    LockX         = 1u,
    LockY         = 2u,
};

enum class KnotControlPart : uint8_t
{
    AnchorPoint = 0u,
    InTangent   = 1u,
    OutTangent  = 2u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct LayerTransformBaseline
{
    uint32_t                                        LayerIdentity = 0u;
    float                                           CenterX       = 256.0f;
    float                                           CenterY       = 256.0f;
    float                                           WidthPx       = 128.0f;
    float                                           HeightPx      = 128.0f;
    float                                           RotationDeg   = 0.0f;
    float                                           FontSizePx    = 54.0f;
    std::vector<GeometricRaster::VectorCubicSubpath> Subpaths     {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  CLASS DECLARATION
//------------------------------------------------------------------------------------------------------------------------

class VectorViewportInteractor
{
public:
    VectorViewportInteractor();

    void AttachSession(PatternStampWorkspaceSession* TargetSession) noexcept;
    void AttachRasterizer(GeometricRaster::VectorCanvasRasterizer* TargetRasterizer) noexcept;
    void AssignSvgPasteRequestCallback(std::function<bool(std::string_view, float, float)> Callback);

    void SynchronizeGizmoAnchor() noexcept;
    void ProcessViewportInput(const ViewportInteractionReading& Reading);
    void ResetActiveTools() noexcept;

    [[nodiscard]] bool QueryDragCaptureActive() const noexcept
    {
        return DraggingGizmo2D ||
               DraggingDirectLayer ||
               DraggingKnotControl ||
               DraggingPointPin ||
               CreatingShape ||
               PanningCanvas ||
               PenAuthoringActive ||
               (ModalMode != ModalVectorTransformMode::Inactive) ||
               (Session != nullptr && Session->MarqueeActive);
    }

    [[nodiscard]] ModalVectorTransformMode QueryModalTransformMode() const noexcept { return ModalMode; }
    [[nodiscard]] const Editor::TransformGizmo& InspectGizmo2D() const noexcept { return Gizmo2D; }
    [[nodiscard]] Editor::TransformGizmo&       AccessGizmo2D() noexcept { return Gizmo2D; }
    [[nodiscard]] bool                          QueryPenAuthoringActive() const noexcept { return PenAuthoringActive; }
    [[nodiscard]] std::string                   QueryModalStatusSummary() const;

private:
    void RouteKeyboardShortcuts(const ViewportInteractionReading& Reading, bool InsidePlanar, uint32_t ViewW, uint32_t ViewH);
    void AdvanceModalTransformStep(const ViewportInteractionReading& Reading, uint32_t ViewW, uint32_t ViewH);
    void DispatchPointerInteraction(const ViewportInteractionReading& Reading, uint32_t ViewW, uint32_t ViewH);

    void CaptureLayerBaseline(const PatternStampLayerSpec& Layer);
    void ApplyBaselineTransform(PatternStampLayerSpec& Layer, float DeltaX, float DeltaY, float ScaleX, float ScaleY, float DeltaRotDeg) const;
    [[nodiscard]] float ApplyOptionalSnap(float CoordDocPx, bool SnapActive) const noexcept;
    [[nodiscard]] int32_t FindNearestKnot(
        const PatternStampLayerSpec& Layer,
        float                        DocX,
        float                        DocY,
        float                        MaxDistancePx,
        KnotControlPart&            OutTarget) const noexcept;
    [[nodiscard]] int32_t FindNearestPointPin(
        const PatternStampLayerSpec& Layer,
        float                        DocX,
        float                        DocY,
        float                        MaxDistancePx) const noexcept;

    PatternStampWorkspaceSession*                          Session                = nullptr;
    GeometricRaster::VectorCanvasRasterizer*               Rasterizer             = nullptr;
    Editor::TransformGizmo                                 Gizmo2D                {};
    std::function<bool(std::string_view, float, float)>    SvgPasteCallback       {};
    uint32_t                                               LastViewportW          = 720u;
    uint32_t                                               LastViewportH          = 720u;

    bool                         DraggingGizmo2D      = false;
    bool                         DraggingDirectLayer  = false;
    bool                         DraggingKnotControl   = false;
    bool                         DraggingPointPin     = false;
    bool                         CreatingShape        = false;
    bool                         PanningCanvas        = false;
    bool                         PenAuthoringActive   = false;

    uint32_t                     CreatingLayerId      = 0u;
    float                        CreateOriginDocX     = 256.0f;
    float                        CreateOriginDocY     = 256.0f;

    float                        PanStartU            = 0.0f;
    float                        PanStartV            = 0.0f;
    float                        PanStartCursorX      = 0.0f;
    float                        PanStartCursorY      = 0.0f;

    float                        DragStartDocX        = 256.0f;
    float                        DragStartDocY        = 256.0f;
    int32_t                      ActiveKnotGlobalIdx  = -1;
    KnotControlPart             ActiveKnotTarget     = KnotControlPart::AnchorPoint;
    int32_t                      ActivePinSlot        = -1;
    uint32_t                     PenTargetLayerId     = 0u;
    bool                         PenDraggingHandles   = false;

    LayerTransformBaseline       Baseline             {};
    Mathematics::SpatialTransform ActiveGizmoTransform {};

    ModalVectorTransformMode     ModalMode            = ModalVectorTransformMode::Inactive;
    VectorAxisConstraint         ModalConstraint      = VectorAxisConstraint::Unconstrained;
    std::string                  ModalNumericEntry    {};
    float                        ModalStartDocX       = 256.0f;
    float                        ModalStartDocY       = 256.0f;
};

} // namespace Frontier::Workspaces
