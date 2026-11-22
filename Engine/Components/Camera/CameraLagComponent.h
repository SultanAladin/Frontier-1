/*============================================================================================================================================
                                                         CAMERALAGCOMPONENT.H
============================================================================================================================================*/
// 🧩 Reusable camera lag: exponentially-damped follow of a desired camera pose (Unreal-style lag speed)

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
    bool  RotationLagEnabled       = true;  // [-]   - Damp camera view direction toward the desired orientation
    float LocationLagSpeed         = 14.0f; // [1/s] - Position convergence rate (higher = tighter follow)
    float RotationLagSpeed         = 32.0f; // [1/s] - Angular view-direction convergence rate (higher = tighter follow)
    float MaximumLagDistanceMeters = 8.0f;  // [m]   - Hard cap on position trailing distance (<= 0 disables the cap)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

// Trails a desired (position, target-point) pose with step-rate-independent exponential damping.
// Position lag and unit-sphere view-direction SLERP are decoupled so pure translation never twists orientation.
class CameraLagComponent
{
public:
    void Configure(const CameraLagSettings& NewSettings) noexcept { Settings = NewSettings; }
    [[nodiscard]] const CameraLagSettings& InspectSettings() const noexcept { return Settings; }
    [[nodiscard]] CameraLagSettings&       AccessSettings() noexcept { return Settings; }

    // Primes the smoothed pose exactly at the given pose (no trailing sweep on the next AdvanceLag)
    void SnapTo(Mathematics::Axis3D Position, Mathematics::Axis3D Target) noexcept;
    [[nodiscard]] bool QueryPrimed() const noexcept { return Primed; }

    // Advances the smoothed pose toward the desired pose; returns true while the smoothed pose is still settling
    bool AdvanceLag(Mathematics::Axis3D DesiredPosition, Mathematics::Axis3D DesiredTarget, float DeltaSeconds) noexcept;

    [[nodiscard]] Mathematics::Axis3D QueryPosition() const noexcept { return SmoothedPosition; }
    [[nodiscard]] Mathematics::Axis3D QueryTarget() const noexcept { return SmoothedTarget; }
    [[nodiscard]] Mathematics::Axis3D QueryForwardDirection() const noexcept { return SmoothedForward; }
    [[nodiscard]] float               QueryFocusDistanceMeters() const noexcept { return SmoothedDistance; }

private:
    CameraLagSettings   Settings         {};
    Mathematics::Axis3D SmoothedPosition { 0.0f, 0.0f, 0.0f }; // [m] - Damped camera position
    Mathematics::Axis3D SmoothedTarget   { 0.0f, 1.0f, 0.0f }; // [m] - Reconstructed damped camera target point
    Mathematics::Axis3D SmoothedForward  { 0.0f, 1.0f, 0.0f }; // [-] - Unit-sphere damped forward direction
    float               SmoothedDistance = 1.0f;               // [m] - Log-smoothed focus distance
    bool                Primed           = false;              // [-] - True after SnapTo / first AdvanceLag
};

} // namespace Frontier::Components
