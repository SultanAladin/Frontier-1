/*============================================================================================================================================
                                                           CAMERACOMPONENT.CPP
============================================================================================================================================*/
// 🧩 Implementation of CameraComponent across Orbit, Fly, FirstPerson, Pan2D, FollowTarget & SplineRail modes

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/CameraComponent.h"

#include "Engine/Components/Camera/Constraints/BoundsConstraint.h"
#include "Engine/Components/Camera/Constraints/PitchConstraint.h"
#include "Engine/Components/Camera/Modes/FirstPersonMode.h"
#include "Engine/Components/Camera/Modes/FlyMode.h"
#include "Engine/Components/Camera/Modes/FollowTargetMode.h"
#include "Engine/Components/Camera/Modes/OrbitMode.h"
#include "Engine/Components/Camera/Modes/Pan2DMode.h"
#include "Engine/Components/Camera/Modes/SplineRailMode.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Components
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

CameraComponent::CameraComponent() noexcept
{
    Configuration.WorldBasis = Mathematics::QueryActiveWorldBasis();
    ApplySensorGateDimensions(Configuration.Optics);
    Configuration.Optics.FocalLengthMillimeters =
        Projection::DeriveFocalLengthFromVerticalFov(42.0f, Configuration.Optics.SensorHeightMillimeters);
    SnapToTargets(16.0f / 9.0f);
}

void CameraComponent::Configure(CameraConfiguration NewConfig) noexcept
{
    Configuration = std::move(NewConfig);
    ApplySensorGateDimensions(Configuration.Optics);
    Projection::RefreshOpticalTelemetry(Configuration.Optics, Posture);
}

void CameraComponent::AssignOperatingMode(CameraOperatingMode Mode) noexcept
{
    if (Configuration.OperatingMode == Mode)
    {
        return;
    }
    if (Mode == CameraOperatingMode::Fly || Mode == CameraOperatingMode::FirstPerson)
    {
        Posture.TargetEyePosition = Posture.CurrentEyePosition;
    }
    else if (Mode == CameraOperatingMode::Orbit)
    {
        Posture.TargetPivot = Posture.CurrentPivot;
    }
    Configuration.OperatingMode = Mode;
}

void CameraComponent::AssignProjectionMode(CameraProjectionMode Mode) noexcept
{
    Configuration.ProjectionMode = Mode;
    if (Mode == CameraProjectionMode::Orthographic)
    {
        const float TanHalf = std::tan(0.5f * Posture.VerticalFovDegrees * Constants::DegToRadFloat);
        Configuration.Optics.OrthographicSpanMeters = std::max(0.15f, 2.0f * Posture.CurrentDistanceMeters * TanHalf);
    }
}

void CameraComponent::AssignSensorGate(SensorGatePreset Gate) noexcept
{
    Configuration.Optics.SensorGate = Gate;
    ApplySensorGateDimensions(Configuration.Optics);
    Projection::RefreshOpticalTelemetry(Configuration.Optics, Posture);
}

void CameraComponent::AssignFocalLengthMillimeters(float FocalMillimeters) noexcept
{
    Configuration.Optics.FocalLengthMillimeters = std::clamp(FocalMillimeters, 4.0f, 1200.0f);
    Projection::RefreshOpticalTelemetry(Configuration.Optics, Posture);
}

void CameraComponent::AssignVerticalFovDegrees(float VerticalFovDegrees) noexcept
{
    Configuration.Optics.FocalLengthMillimeters = Projection::DeriveFocalLengthFromVerticalFov(
        VerticalFovDegrees,
        Configuration.Optics.SensorHeightMillimeters);
    Projection::RefreshOpticalTelemetry(Configuration.Optics, Posture);
}

void CameraComponent::AssignWorldBasis(Mathematics::WorldCoordinateBasis Basis) noexcept
{
    Configuration.WorldBasis = Basis;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              SPATIAL RIG INTEROPERABILITY
//------------------------------------------------------------------------------------------------------------------------

void CameraComponent::AlignFromSpatialRig(const Editor::SpatialCameraRig& Rig, bool SnapImmediately) noexcept
{
    Configuration.WorldBasis     = Rig.Basis;
    Configuration.ProjectionMode = (Rig.Preset == Editor::SpatialProjectionPreset::Orthographic)
                                       ? CameraProjectionMode::Orthographic
                                       : CameraProjectionMode::Perspective;
    AssignVerticalFovDegrees(Rig.VerticalFovDeg);

    Posture.TargetPivot          = Rig.TargetPivot;
    Posture.TargetYawRadians     = Rig.YawRadians;
    Posture.TargetPitchRadians   = Constraints::ClampElevationPitchRadians(Rig.PitchRadians, Configuration.Kinematics.PitchLimitRadians);
    Posture.TargetDistanceMeters = std::clamp(Rig.DistanceMeters, Configuration.Kinematics.MinDistanceMeters, Configuration.Kinematics.MaxDistanceMeters);
    Configuration.Optics.FocusDistanceMeters = Posture.TargetDistanceMeters;

    if (Configuration.ProjectionMode == CameraProjectionMode::Orthographic)
    {
        const float TanHalf = std::tan(0.5f * Rig.VerticalFovDeg * Constants::DegToRadFloat);
        Configuration.Optics.OrthographicSpanMeters = std::max(0.15f, 2.0f * Posture.TargetDistanceMeters * TanHalf);
    }

    if (SnapImmediately)
    {
        SnapToTargets(16.0f / 9.0f);
    }
}

Editor::SpatialCameraRig CameraComponent::ExtractSpatialRig() const noexcept
{
    Editor::SpatialCameraRig Rig {};
    Rig.TargetPivot    = Posture.CurrentPivot;
    Rig.YawRadians     = Posture.CurrentYawRadians;
    Rig.PitchRadians   = Posture.CurrentPitchRadians;
    Rig.DistanceMeters = Posture.CurrentDistanceMeters;
    Rig.VerticalFovDeg = Posture.VerticalFovDegrees;
    Rig.Basis          = Configuration.WorldBasis;
    Rig.Preset         = (Configuration.ProjectionMode == CameraProjectionMode::Orthographic)
                             ? Editor::SpatialProjectionPreset::Orthographic
                             : Editor::SpatialProjectionPreset::Perspective;
    return Rig;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 GESTURE INPUT COMMANDS
//------------------------------------------------------------------------------------------------------------------------

void CameraComponent::ApplyOrbitAnglesDelta(float DeltaYawRadians, float DeltaPitchRadians) noexcept
{
    Posture.TargetYawRadians   += DeltaYawRadians;
    Posture.TargetPitchRadians  = Constraints::ClampElevationPitchRadians(
        Posture.TargetPitchRadians + DeltaPitchRadians,
        Configuration.Kinematics.PitchLimitRadians);
}

void CameraComponent::ApplySpatialPanPixels(
    float    DeltaPixelX,
    float    DeltaPixelY,
    uint32_t ViewportWidthPx,
    uint32_t ViewportHeightPx,
    float    PanSensitivityScale) noexcept
{
    (void)ViewportWidthPx;
    const float SafeH   = static_cast<float>(std::max(16u, ViewportHeightPx));
    const float TanHalf = std::tan(0.5f * Posture.VerticalFovDegrees * Constants::DegToRadFloat);
    const float WorldPerPixel = (Configuration.ProjectionMode == CameraProjectionMode::Orthographic)
                                    ? (Configuration.Optics.OrthographicSpanMeters / SafeH)
                                    : ((2.0f * Posture.CurrentDistanceMeters * TanHalf) / SafeH);
    const float Scale = WorldPerPixel * PanSensitivityScale;

    const Mathematics::Axis3D DeltaWorld =
        Posture.RightAxis * (-DeltaPixelX * Scale) +
        Posture.UpAxis * (DeltaPixelY * Scale);

    if (Configuration.OperatingMode == CameraOperatingMode::Fly ||
        Configuration.OperatingMode == CameraOperatingMode::FirstPerson)
    {
        Posture.TargetEyePosition = Constraints::ClampSpatialPositionToBounds(
            Posture.TargetEyePosition + DeltaWorld,
            Configuration.Bounds);
    }
    else
    {
        Posture.TargetPivot = Constraints::ClampSpatialPositionToBounds(
            Posture.TargetPivot + DeltaWorld,
            Configuration.Bounds);
    }
}

void CameraComponent::ApplyLogarithmicDolly(float LogStepDelta) noexcept
{
    const float Factor = std::exp(-LogStepDelta);
    Posture.TargetDistanceMeters = std::clamp(
        Posture.TargetDistanceMeters * Factor,
        Configuration.Kinematics.MinDistanceMeters,
        Configuration.Kinematics.MaxDistanceMeters);
    if (Configuration.ProjectionMode == CameraProjectionMode::Orthographic)
    {
        Configuration.Optics.OrthographicSpanMeters = std::clamp(
            Configuration.Optics.OrthographicSpanMeters * Factor,
            0.10f,
            500.0f);
    }
}

void CameraComponent::ApplyFlyStride(
    float ForwardInput,
    float RightInput,
    float VerticalInput,
    bool  SprintBoost,
    float DeltaSeconds) noexcept
{
    if (Configuration.OperatingMode == CameraOperatingMode::FirstPerson)
    {
        Modes::ApplyFirstPersonHorizontalStride(
            Configuration,
            Posture,
            ForwardInput,
            RightInput,
            VerticalInput,
            SprintBoost,
            DeltaSeconds);
        return;
    }

    const float Speed = Configuration.Kinematics.FlySpeedMetersPerSec *
                        (SprintBoost ? Configuration.Kinematics.FlyBoostMultiplier : 1.0f) *
                        std::max(0.0f, DeltaSeconds);
    const Mathematics::Axis3D WorldUp = Mathematics::QueryWorldUpAxis(Configuration.WorldBasis);
    const Mathematics::Axis3D Fwd     = Modes::EvaluateForwardFromLookAngles(
        Posture.TargetYawRadians,
        Posture.TargetPitchRadians,
        Configuration.WorldBasis);
    const Mathematics::Axis3D Right = Mathematics::NormalizeAxis(
        Mathematics::EvaluateCross(Fwd, WorldUp),
        { 1.0f, 0.0f, 0.0f });

    Posture.TargetEyePosition = Constraints::ClampSpatialPositionToBounds(
        Posture.TargetEyePosition + Fwd * (ForwardInput * Speed) + Right * (RightInput * Speed) + WorldUp * (VerticalInput * Speed),
        Configuration.Bounds);
}

void CameraComponent::ApplyPlanarPanDelta(float DeltaU, float DeltaV) noexcept
{
    Posture.TargetPlanarCenter = Constraints::ClampPlanarCenterToBounds(
        { Posture.TargetPlanarCenter.U + DeltaU, Posture.TargetPlanarCenter.V + DeltaV },
        Configuration.Bounds);
}

void CameraComponent::ApplyPlanarLogZoom(float LogStepDelta) noexcept
{
    Posture.TargetPlanarZoom = std::clamp(Posture.TargetPlanarZoom * std::exp(LogStepDelta), 0.15f, 32.0f);
}

void CameraComponent::AssignFollowTargetAnchor(Mathematics::Axis3D WorldTargetAnchor) noexcept
{
    Posture.TargetPivot = WorldTargetAnchor;
}

void CameraComponent::AssignSplineProgress(float NormalizedProgress) noexcept
{
    Posture.TargetSplineParameter = std::clamp(NormalizedProgress, 0.0f, 1.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  KINEMATIC ADVANCEMENT
//------------------------------------------------------------------------------------------------------------------------

void CameraComponent::AdvanceKinematics(float DeltaSeconds, float ViewportAspectRatio) noexcept
{
    Projection::RefreshOpticalTelemetry(Configuration.Optics, Posture);

    switch (Configuration.OperatingMode)
    {
        case CameraOperatingMode::Orbit:
            Modes::AdvanceOrbitKinematics(Configuration, Posture, DeltaSeconds);
            break;
        case CameraOperatingMode::Fly:
            Modes::AdvanceFlyKinematics(Configuration, Posture, DeltaSeconds);
            break;
        case CameraOperatingMode::FirstPerson:
            Modes::AdvanceFirstPersonKinematics(Configuration, Posture, DeltaSeconds);
            break;
        case CameraOperatingMode::Pan2D:
            Modes::AdvancePan2DKinematics(Configuration, Posture, DeltaSeconds);
            Modes::AdvanceOrbitKinematics(Configuration, Posture, DeltaSeconds);
            break;
        case CameraOperatingMode::FollowTarget:
            Modes::AdvanceFollowTargetKinematics(Configuration, Posture, DeltaSeconds);
            break;
        case CameraOperatingMode::SplineRail:
            Modes::AdvanceSplineRailKinematics(Configuration, Posture, DeltaSeconds);
            break;
    }

    Posture.ViewClip = Projection::EvaluateViewClipTransform(Configuration, Posture, ViewportAspectRatio);
}

void CameraComponent::SnapToTargets(float ViewportAspectRatio) noexcept
{
    Posture.TargetPitchRadians     = Constraints::ClampElevationPitchRadians(Posture.TargetPitchRadians, Configuration.Kinematics.PitchLimitRadians);
    Posture.TargetDistanceMeters   = std::clamp(Posture.TargetDistanceMeters, Configuration.Kinematics.MinDistanceMeters, Configuration.Kinematics.MaxDistanceMeters);
    Posture.TargetPivot            = Constraints::ClampSpatialPositionToBounds(Posture.TargetPivot, Configuration.Bounds);
    Posture.TargetEyePosition      = Constraints::ClampSpatialPositionToBounds(Posture.TargetEyePosition, Configuration.Bounds);
    Posture.TargetPlanarCenter     = Constraints::ClampPlanarCenterToBounds(Posture.TargetPlanarCenter, Configuration.Bounds);
    Posture.TargetPlanarZoom       = std::clamp(Posture.TargetPlanarZoom, 0.15f, 32.0f);
    Posture.TargetSplineParameter  = std::clamp(Posture.TargetSplineParameter, 0.0f, 1.0f);

    Posture.CurrentPivot           = Posture.TargetPivot;
    Posture.CurrentEyePosition     = Posture.TargetEyePosition;
    Posture.CurrentYawRadians      = Posture.TargetYawRadians;
    Posture.CurrentPitchRadians    = Posture.TargetPitchRadians;
    Posture.CurrentRollRadians     = Posture.TargetRollRadians;
    Posture.CurrentDistanceMeters  = Posture.TargetDistanceMeters;
    Posture.CurrentPlanarCenter    = Posture.TargetPlanarCenter;
    Posture.CurrentPlanarZoom      = Posture.TargetPlanarZoom;
    Posture.CurrentSplineParameter = Posture.TargetSplineParameter;
    Posture.FollowPivotVelocity    = { 0.0f, 0.0f, 0.0f };

    AdvanceKinematics(10.0f, ViewportAspectRatio);
}

bool CameraComponent::QuerySettlingMotion() const noexcept
{
    const float YawErr   = std::abs(Posture.TargetYawRadians - Posture.CurrentYawRadians);
    const float PitchErr = std::abs(Posture.TargetPitchRadians - Posture.CurrentPitchRadians);
    const float DistErr  = std::abs(Posture.TargetDistanceMeters - Posture.CurrentDistanceMeters);
    const float PivotErr = Mathematics::EvaluateLength(Posture.TargetPivot - Posture.CurrentPivot);
    const float Zoom2DEr = std::abs(Posture.TargetPlanarZoom - Posture.CurrentPlanarZoom);
    const float Pan2DErr = Mathematics::EvaluateUVDistance(Posture.TargetPlanarCenter, Posture.CurrentPlanarCenter);

    if (Configuration.OperatingMode == CameraOperatingMode::Fly ||
        Configuration.OperatingMode == CameraOperatingMode::FirstPerson)
    {
        const float EyeErr = Mathematics::EvaluateLength(Posture.TargetEyePosition - Posture.CurrentEyePosition);
        return (YawErr > 2.0e-4f) || (PitchErr > 2.0e-4f) || (EyeErr > 2.0e-4f);
    }
    return (YawErr > 2.0e-4f) || (PitchErr > 2.0e-4f) || (DistErr > 3.0e-4f) ||
           (PivotErr > 2.0e-4f) || (Zoom2DEr > 3.0e-4f) || (Pan2DErr > 2.0e-4f);
}

const CameraConfiguration& CameraComponent::InspectConfiguration() const noexcept
{
    return Configuration;
}

CameraConfiguration& CameraComponent::AccessConfiguration() noexcept
{
    return Configuration;
}

const CameraPosture& CameraComponent::InspectPosture() const noexcept
{
    return Posture;
}

CameraPosture& CameraComponent::AccessPosture() noexcept
{
    return Posture;
}

Projection::SpatialPickingRay CameraComponent::ConstructPickingRay(
    uint32_t ViewportWidthPx,
    uint32_t ViewportHeightPx,
    float    LocalPixelX,
    float    LocalPixelY) const noexcept
{
    return Projection::ConstructSpatialPickingRay(
        Configuration,
        Posture,
        ViewportWidthPx,
        ViewportHeightPx,
        LocalPixelX,
        LocalPixelY);
}

#if !FRONTIER_PROFILE_SHIPPING
Diagnostics::FrustumOutlineGeometry CameraComponent::ConstructFrustumDiagnostics(float AspectRatio) const noexcept
{
    return Diagnostics::ConstructFrustumOutline(Configuration, Posture, AspectRatio);
}

Diagnostics::FocusPlaneQuad CameraComponent::ConstructFocusPlaneDiagnostics(float AspectRatio) const noexcept
{
    return Diagnostics::ConstructFocusPlaneIndicator(Configuration, Posture, AspectRatio);
}

std::vector<Diagnostics::NormalizedLineSegment> CameraComponent::BuildCompositionDiagnostics() const
{
    return Diagnostics::BuildCompositionGuideSegments(Configuration.Diagnostics.CompositionGuide);
}
#endif

} // namespace Frontier::Components
