/*============================================================================================================================================
                                                            CAMERACOMPONENT.H
============================================================================================================================================*/
// 🧩 Unified 6-mode physical thin-lens CameraComponent with exponential smoothing, constraints, and non-Shipping diagnostics

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/CameraConfiguration.h"
#include "Engine/Components/Camera/CameraPosture.h"
#include "Engine/Components/Camera/Projection/ViewTransform.h"
#include "Engine/Editor/Viewports/ViewportProjection.h"

#if !FRONTIER_PROFILE_SHIPPING
#include "Engine/Components/Camera/Diagnostics/CompositionOverlay.h"
#include "Engine/Components/Camera/Diagnostics/FocusPlaneIndicator.h"
#include "Engine/Components/Camera/Diagnostics/FrustumVisualizer.h"
#endif

#include <cstdint>

namespace Frontier::Components
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         CLASSES
//------------------------------------------------------------------------------------------------------------------------

class CameraComponent
{
public:
    CameraComponent() noexcept;

    void Configure(CameraConfiguration NewConfig) noexcept;
    void AssignOperatingMode(CameraOperatingMode Mode) noexcept;
    void AssignProjectionMode(CameraProjectionMode Mode) noexcept;
    void AssignSensorGate(SensorGatePreset Gate) noexcept;
    void AssignFocalLengthMillimeters(float FocalMillimeters) noexcept;
    void AssignVerticalFovDegrees(float VerticalFovDegrees) noexcept;
    void AssignWorldBasis(Mathematics::WorldCoordinateBasis Basis) noexcept;

    // Synchronize directly from or into Editor::SpatialCameraRig (preserving backward compatibility)
    void AlignFromSpatialRig(const Editor::SpatialCameraRig& Rig, bool SnapImmediately = true) noexcept;
    [[nodiscard]] Editor::SpatialCameraRig ExtractSpatialRig() const noexcept;

    // Navigation gesture inputs (update Target* fields; smoothed into Current* during AdvanceKinematics)
    void ApplyOrbitAnglesDelta(float DeltaYawRadians, float DeltaPitchRadians) noexcept;
    void ApplySpatialPanPixels(
        float    DeltaPixelX,
        float    DeltaPixelY,
        uint32_t ViewportWidthPx,
        uint32_t ViewportHeightPx,
        float    PanSensitivityScale = 1.0f) noexcept;
    void ApplyLogarithmicDolly(float LogStepDelta) noexcept;
    void ApplyFlyStride(
        float ForwardInput,
        float RightInput,
        float VerticalInput,
        bool  SprintBoost,
        float DeltaSeconds) noexcept;
    void ApplyPlanarPanDelta(float DeltaU, float DeltaV) noexcept;
    void ApplyPlanarLogZoom(float LogStepDelta) noexcept;
    void AssignFollowTargetAnchor(Mathematics::Axis3D WorldTargetAnchor) noexcept;
    void AssignSplineProgress(float NormalizedProgress) noexcept;

    // Step-rate-independent kinematic advancement & immediate convergence
    void AdvanceKinematics(float DeltaSeconds, float ViewportAspectRatio = 1.777778f) noexcept;
    void SnapToTargets(float ViewportAspectRatio = 1.777778f) noexcept;

    [[nodiscard]] bool                       QuerySettlingMotion() const noexcept;
    [[nodiscard]] const CameraConfiguration& InspectConfiguration() const noexcept;
    [[nodiscard]] CameraConfiguration&       AccessConfiguration() noexcept;
    [[nodiscard]] const CameraPosture&       InspectPosture() const noexcept;
    [[nodiscard]] CameraPosture&             AccessPosture() noexcept;

    [[nodiscard]] Projection::SpatialPickingRay ConstructPickingRay(
        uint32_t ViewportWidthPx,
        uint32_t ViewportHeightPx,
        float    LocalPixelX,
        float    LocalPixelY) const noexcept;

#if !FRONTIER_PROFILE_SHIPPING
    [[nodiscard]] Diagnostics::FrustumOutlineGeometry ConstructFrustumDiagnostics(float AspectRatio) const noexcept;
    [[nodiscard]] Diagnostics::FocusPlaneQuad         ConstructFocusPlaneDiagnostics(float AspectRatio) const noexcept;
    [[nodiscard]] std::vector<Diagnostics::NormalizedLineSegment> BuildCompositionDiagnostics() const;
#endif

private:
    CameraConfiguration Configuration {};
    CameraPosture       Posture       {};
};

} // namespace Frontier::Components
