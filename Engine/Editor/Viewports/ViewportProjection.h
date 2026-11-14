/*============================================================================================================================================
                                                           VIEWPORTPROJECTION.H
============================================================================================================================================*/
// 🧩 Reusable 3D spatial orbit/pan/frame camera & 2D aspect-locked planar projection state shared across WorkspaceDocument sessions

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Editor/Viewports/TransformGizmo.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/CoordinateSpace.h"

#include <cstdint>

namespace Frontier::Editor
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ViewportSplitMode : uint8_t
{
    SplitPlanarAndSpatial = 0u, // [-] - Side-by-side 2D Planar/Tile canvas + 3D Spatial viewport
    Planar2DOnly          = 1u, // [-] - Full-pane 2D Planar/Tile canvas
    Spatial3DOnly         = 2u, // [-] - Full-pane 3D Spatial viewport
};

enum class SpatialProjectionPreset : uint8_t
{
    Perspective  = 0u, // [-] - Perspective orbit camera
    Orthographic = 1u, // [-] - Parallel orthographic projection
    Top          = 2u, // [-] - Top-down (+Y) axial view
    Front        = 3u, // [-] - Front (+Z) axial view
    Right        = 4u, // [-] - Right (+X) axial view
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SpatialCameraRig
{
    Mathematics::Axis3D               TargetPivot    { 0.0f, 0.0f, 0.42f };                              // [m]   - Orbit target point
    float                             YawRadians     = 0.68f;                                            // [rad] - Horizontal orbit angle
    float                             PitchRadians   = 0.38f;                                            // [rad] - Vertical elevation angle
    float                             DistanceMeters = 6.20f;                                            // [m]   - Camera distance from pivot
    float                             VerticalFovDeg = 42.0f;                                            // [deg] - Vertical perspective field of view
    SpatialProjectionPreset           Preset         = SpatialProjectionPreset::Perspective;             // [-]   - Active camera projection preset
    Mathematics::WorldCoordinateBasis Basis          = Mathematics::WorldCoordinateBasis::RightHandedZUp;// [-]   - Active RHR world coordinate basis
};

struct PlanarWindowRig
{
    float               SpanU       = 1.0f;            // [uv]  - Horizontal domain span (e.g. UDIM tile count)
    float               SpanV       = 1.0f;            // [uv]  - Vertical domain span (1.0 per UDIM row)
    float               PaddingUV   = 0.08f;           // [uv]  - Safe margin around domain bounds
    float               ZoomScale   = 1.0f;            // [-]   - Zoom multiplier (1.0 = fit domain)
    Mathematics::UVAxes PanOffsetUV { 0.0f, 0.0f };    // [uv]  - Planar pan offset
};

class ViewportProjection
{
public:
    ViewportProjection() noexcept = default;

    void ConfigureSplitMode(ViewportSplitMode Mode) noexcept;
    void ConfigureSplitRatio(float NormalizedRatio) noexcept;
    void ConfigureSpatialCamera(const SpatialCameraRig& Rig) noexcept;
    void ConfigurePlanarWindow(const PlanarWindowRig& Rig) noexcept;
    void ConfigurePlanarDomainSpan(float SpanU, float SpanV = 1.0f) noexcept;

    void ApplySpatialPreset(SpatialProjectionPreset Preset) noexcept;
    void OrbitSpatialCamera(float DeltaYawRad, float DeltaPitchRad) noexcept;
    void PanSpatialCamera(float DeltaRightMeters, float DeltaUpMeters) noexcept;
    void DollySpatialCamera(float DistanceMultiplier) noexcept;
    void FitSpatialExtents(Mathematics::Axis3D MinBounds, Mathematics::Axis3D MaxBounds) noexcept;

    void PanPlanarWindow(Mathematics::UVAxes DeltaUV) noexcept;
    void ZoomPlanarWindow(float ZoomMultiplier) noexcept;
    void ZoomPlanarAroundPoint(
        uint32_t            CanvasWidthPx,
        uint32_t            CanvasHeightPx,
        float               LocalPixelX,
        float               LocalPixelY,
        float               ZoomMultiplier) noexcept;
    void FitPlanarExtents(Mathematics::UVAxes MinBounds, Mathematics::UVAxes MaxBounds) noexcept;

    [[nodiscard]] ViewportSplitMode       QuerySplitMode() const noexcept { return SplitMode; }
    [[nodiscard]] float                   QuerySplitRatio() const noexcept { return SplitRatio; }
    [[nodiscard]] SpatialCameraRig&       AccessSpatialCamera() noexcept { return SpatialCamera; }
    [[nodiscard]] const SpatialCameraRig& InspectSpatialCamera() const noexcept { return SpatialCamera; }
    [[nodiscard]] PlanarWindowRig&        AccessPlanarWindow() noexcept { return PlanarWindow; }
    [[nodiscard]] const PlanarWindowRig&  InspectPlanarWindow() const noexcept { return PlanarWindow; }

    [[nodiscard]] Mathematics::Axis3D EvaluateCameraEyePosition() const noexcept;
    void EvaluateCameraBasis(
        Mathematics::Axis3D& OutRight,
        Mathematics::Axis3D& OutUp,
        Mathematics::Axis3D& OutForward) const noexcept;

    [[nodiscard]] ViewportRay3D BuildSpatialRay(
        uint32_t ViewportWidthPx,
        uint32_t ViewportHeightPx,
        float    LocalPixelX,
        float    LocalPixelY) const noexcept;

    // Computes aspect-ratio-preserving [OutMinUV .. OutMaxUV] so 1x1 UDIM tiles & 2D gizmo rings remain geometrically square.
    void EvaluateAspectLockedPlanarBounds(
        uint32_t             CanvasWidthPx,
        uint32_t             CanvasHeightPx,
        Mathematics::UVAxes& OutMinUV,
        Mathematics::UVAxes& OutMaxUV) const noexcept;

    [[nodiscard]] Mathematics::UVAxes ProjectPixelToPlanarUV(
        uint32_t CanvasWidthPx,
        uint32_t CanvasHeightPx,
        float    LocalPixelX,
        float    LocalPixelY) const noexcept;

    [[nodiscard]] ViewportRay3D BuildPlanarRay(
        uint32_t CanvasWidthPx,
        uint32_t CanvasHeightPx,
        float    LocalPixelX,
        float    LocalPixelY) const noexcept;

private:
    ViewportSplitMode SplitMode     = ViewportSplitMode::SplitPlanarAndSpatial; // [-]   - Single vs 2D+3D split mode
    float             SplitRatio    = 0.50f;                                    // [0-1] - Normalized split divider ratio
    SpatialCameraRig  SpatialCamera {};                                         // [-]   - 3D orbit/ortho camera configuration
    PlanarWindowRig   PlanarWindow  {};                                         // [-]   - 2D aspect-locked tile/canvas window
};

} // namespace Frontier::Editor
