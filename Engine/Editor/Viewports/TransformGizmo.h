/*============================================================================================================================================
                                                             TRANSFORMGIZMO.H
============================================================================================================================================*/
// 🧩 Modular 3D & 2D Slate GizmoPRO manipulator reusable across UV, Geometry, Material, Vector & Simulation workspaces

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Configuration/WorkspaceConfiguration.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::Editor
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class GizmoDimensionMode : uint8_t
{
    Spatial3D = 0u, // [-] - Full 13-handle 3D GizmoPRO (X/Y/Z Cones, Cylinders, YZ/XZ/XY Planes, 3 Arcs, Billboard Ring)
    Planar2D  = 1u, // [-] - Planar 6-handle 2D UV/Vector Gizmo (+U/+V Cones, +U/+V Scale Cylinders, UV Quad, 45° Arc, Ring)
};

enum class GizmoHandleFilter : uint8_t
{
    Universal     = 0u,
    TranslateOnly = 1u,
    RotateOnly    = 2u,
    ScaleOnly     = 3u,
};

enum class GizmoHandleOrdinal : uint32_t
{
    None            = 0u,
    TranslateX      = 1u,  // #E01414 Cone at +X * 0.95L (or +U in 2D)
    TranslateY      = 2u,  // #12D40A Cone at +Y * 0.95L (or +V in 2D)
    TranslateZ      = 3u,  // #1560E0 Cone at +Z * 0.95L
    ScaleX          = 4u,  // #E01414 Cylinder at +X * 0.67L
    ScaleY          = 5u,  // #12D40A Cylinder at +Y * 0.67L
    ScaleZ          = 6u,  // #1560E0 Cylinder at +Z * 0.67L
    PlaneYZ         = 7u,  // #1FC7C7 Cyan quad + 2 outer edges
    PlaneXZ         = 8u,  // #C81EC8 Magenta quad + 2 outer edges
    PlaneXY         = 9u,  // #E0CD12 Yellow quad + 2 outer edges (UV free-move quad in 2D)
    RotateX         = 10u, // #E01414 31° annular arc in YZ plane at 45°
    RotateY         = 11u, // #12D40A 31° annular arc in XZ plane at 45°
    RotateZ         = 12u, // #1560E0 31° annular arc in XY plane at 45° (UV rotation arc in 2D)
    CenterRing      = 13u, // #FFFFFF Camera-billboarded torus ring (screen-plane free move / uniform reset)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) GizmoGPUVertex
{
    Mathematics::GPUAxis4 PositionAndAlpha  {}; // xyz = World/UV Position, w = BaseAlpha (1.0 or 0.28)
    Mathematics::GPUAxis4 NormalAndHandle   {}; // xyz = Surface Normal,    w = float(GizmoHandleOrdinal)
    Mathematics::GPUAxis4 ColorRGBAndShaded {}; // rgb = Slate Accent RGB,  w = LitShadingWeight (0 or 1)
};

static_assert(sizeof(GizmoGPUVertex) == 48u, "GizmoGPUVertex must match GizmoRasterization.slang 48-byte stride.");

struct ViewportRay3D
{
    Mathematics::Axis3D Origin    { 0.0f, 0.0f, 5.0f };  // [m] - Ray origin in world or UV-canvas space
    Mathematics::Axis3D Direction { 0.0f, 0.0f, -1.0f }; // [-] - Normalized ray direction
};

struct GizmoInteractionDelta
{
    Mathematics::Axis3D   TranslationDelta {};                      // [m/uv] - Incremental or total translation offset
    Mathematics::Axis3D   RotationDegrees  {};                      // [°]    - Euler/planar rotation delta in degrees
    Mathematics::Scalar3D ScaleMultiplier  { 1.0f, 1.0f, 1.0f };    // [-]    - Multiplicative scale factor (clamped >= 0.05)
    bool                  TransformChanged = false;                 // [-]    - True when active drag modified the transform
    std::string           HudReadout       {};                      // [-]    - Live HUD status (e.g. "X move +0.50")
};

class TransformGizmo
{
public:
    TransformGizmo() = default;

    void ConfigureDimensionMode(GizmoDimensionMode Mode) noexcept;
    void ConfigureHandleFilter(GizmoHandleFilter Filter) noexcept;
    [[nodiscard]] GizmoHandleFilter QueryHandleFilter() const noexcept { return HandleFilter; }
    void ConfigureAnchor(Mathematics::Axis3D PivotOrigin, float ConstantWorldRadius = 1.0f) noexcept;
    void AssignHoveredHandle(GizmoHandleOrdinal Handle) noexcept;

    [[nodiscard]] std::vector<GizmoGPUVertex> BuildTriangleStream(
        Mathematics::Axis3D CameraRight   = { 1.0f, 0.0f, 0.0f },
        Mathematics::Axis3D CameraUp      = { 0.0f, 1.0f, 0.0f },
        Mathematics::Axis3D CameraForward = { 0.0f, 0.0f, -1.0f }) const;

    bool BeginDragInteraction(
        GizmoHandleOrdinal                  PickedHandle,
        const ViewportRay3D&                InitialRay,
        const Mathematics::SpatialTransform& BaselineTransform,
        Mathematics::Axis3D                 CameraRight = { 1.0f, 0.0f, 0.0f },
        Mathematics::Axis3D                 CameraUp    = { 0.0f, 1.0f, 0.0f }) noexcept;

    [[nodiscard]] GizmoInteractionDelta EvaluateDragInteraction(
        const ViewportRay3D&                    CurrentRay,
        bool                                    SnapModifierActive,
        const Configuration::GizmoSnapSettings& SnapSettings,
        Mathematics::SpatialTransform&          InOutTransform) noexcept;

    void EndDragInteraction() noexcept;

    [[nodiscard]] GizmoDimensionMode  QueryDimensionMode() const noexcept { return DimensionMode; }
    [[nodiscard]] GizmoHandleOrdinal  QueryHoveredHandle() const noexcept { return HoveredHandle; }
    [[nodiscard]] GizmoHandleOrdinal  QueryActiveHandle() const noexcept  { return ActiveDragHandle; }
    [[nodiscard]] bool                QueryDragging() const noexcept      { return ActiveDragHandle != GizmoHandleOrdinal::None; }
    [[nodiscard]] Mathematics::Axis3D QueryPivotOrigin() const noexcept   { return PivotPosition; }
    [[nodiscard]] float               QueryWorldRadius() const noexcept   { return WorldRadius; }

    [[nodiscard]] static float EvaluateAxisRayParameter(
        Mathematics::Axis3D AxisOrigin,
        Mathematics::Axis3D AxisDirection,
        const ViewportRay3D& Ray) noexcept;

    [[nodiscard]] static bool EvaluatePlaneRayIntersection(
        Mathematics::Axis3D  PlaneOrigin,
        Mathematics::Axis3D  PlaneNormal,
        const ViewportRay3D& Ray,
        Mathematics::Axis3D& OutIntersection) noexcept;

private:
    GizmoDimensionMode            DimensionMode       = GizmoDimensionMode::Spatial3D;
    GizmoHandleFilter             HandleFilter        = GizmoHandleFilter::Universal;
    Mathematics::Axis3D           PivotPosition       { 0.0f, 0.0f, 0.0f };
    float                         WorldRadius         = 1.0f;
    GizmoHandleOrdinal            HoveredHandle       = GizmoHandleOrdinal::None;
    GizmoHandleOrdinal            ActiveDragHandle    = GizmoHandleOrdinal::None;
    Mathematics::SpatialTransform DragStartTransform  {};
    Mathematics::Axis3D           DragStartPivotPosition {};
    Mathematics::Axis3D           DragAxisPrimary     { 1.0f, 0.0f, 0.0f };
    Mathematics::Axis3D           DragAxisSecondary   { 0.0f, 1.0f, 0.0f };
    Mathematics::Axis3D           DragPlaneNormal     { 0.0f, 0.0f, 1.0f };
    Mathematics::Axis3D           DragInitialPlaneHit {};
    float                         DragInitialAxisParam = 0.0f;
    float                         DragInitialAngleRad  = 0.0f;
};

} // namespace Frontier::Editor
