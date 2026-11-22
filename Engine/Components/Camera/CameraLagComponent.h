/*============================================================================================================================================
                                                         CAMERALAGCOMPONENT.H
============================================================================================================================================*/
// 🧩 Reusable camera lag component: decoupled position damping and unit-sphere S^2 view-direction damping

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"

namespace Frontier::Components
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct CameraLagSettings
{
    bool  LocationLagEnabled       = true;  // [-]   - Damp camera position toward the desired position
    bool  RotationLagEnabled       = true;  // [-]   - Damp camera unit forward direction on S^2
    float LocationLagSpeed         = 14.0f; // [1/s] - Position convergence rate (higher = tighter follow)
    float RotationLagSpeed         = 18.0f; // [1/s] - Orientation convergence rate (higher = tighter follow)
    float MaximumLagDistanceMeters = 8.0f;  // [m]   - Hard cap on position trailing distance (<= 0 disables cap)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 CAMERA LAG COMPONENT
//------------------------------------------------------------------------------------------------------------------------

class CameraLagComponent
{
public:
    void Configure(const CameraLagSettings& NewSettings) noexcept;
    [[nodiscard]] const CameraLagSettings& InspectSettings() const noexcept;
    [[nodiscard]] CameraLagSettings&       AccessSettings() noexcept;

    void SnapTo(Mathematics::Axis3D Position, Mathematics::Axis3D Target) noexcept;
    void SnapPose(Mathematics::Axis3D Position, Mathematics::Axis3D Forward, float FocusDistanceMeters) noexcept;
    [[nodiscard]] bool QueryPrimed() const noexcept;

    bool AdvanceLag(Mathematics::Axis3D DesiredPosition, Mathematics::Axis3D DesiredTarget, float DeltaSeconds) noexcept;
    bool AdvancePose(
        Mathematics::Axis3D DesiredPosition,
        Mathematics::Axis3D DesiredForward,
        float               DesiredFocusDistanceMeters,
        float               DeltaSeconds) noexcept;

    [[nodiscard]] Mathematics::Axis3D QueryPosition() const noexcept;
    [[nodiscard]] Mathematics::Axis3D QueryForward() const noexcept;
    [[nodiscard]] Mathematics::Axis3D QueryTarget() const noexcept;
    [[nodiscard]] float               QueryFocusDistance() const noexcept;

private:
    CameraLagSettings   Settings              {};
    Mathematics::Axis3D SmoothedPosition      { 0.0f, 0.0f, 0.0f }; // [m] - Damped camera position
    Mathematics::Axis3D SmoothedForward       { 0.0f, 1.0f, 0.0f }; // [-] - Damped unit view direction on S^2
    float               SmoothedFocusDistance = 5.0f;               // [m] - Damped target distance along SmoothedForward
    bool                Primed                = false;              // [-] - True after SnapTo / first Advance
};

//------------------------------------------------------------------------------------------------------------------------
//                                               INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline void CameraLagComponent::Configure(const CameraLagSettings& NewSettings) noexcept
{
    Settings = NewSettings;
}

inline const CameraLagSettings& CameraLagComponent::InspectSettings() const noexcept
{
    return Settings;
}

inline CameraLagSettings& CameraLagComponent::AccessSettings() noexcept
{
    return Settings;
}

inline bool CameraLagComponent::QueryPrimed() const noexcept
{
    return Primed;
}

inline Mathematics::Axis3D CameraLagComponent::QueryPosition() const noexcept
{
    return SmoothedPosition;
}

inline Mathematics::Axis3D CameraLagComponent::QueryForward() const noexcept
{
    return SmoothedForward;
}

inline Mathematics::Axis3D CameraLagComponent::QueryTarget() const noexcept
{
    return SmoothedPosition + SmoothedForward * SmoothedFocusDistance;
}

inline float CameraLagComponent::QueryFocusDistance() const noexcept
{
    return SmoothedFocusDistance;
}

} // namespace Frontier::Components
