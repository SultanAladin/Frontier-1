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
    bool  RotationLagEnabled       = true;  // [-]   - Damp camera view direction toward the desired target point
    float LocationLagSpeed         = 12.0f; // [1/s] - Position convergence rate (higher = tighter follow)
    float RotationLagSpeed         = 16.0f; // [1/s] - View-direction convergence rate (higher = tighter follow)
    float MaximumLagDistanceMeters = 8.0f;  // [m]   - Hard cap on position trailing distance (<= 0 disables the cap)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

// Trails a desired (position, target-point) pose with step-rate-independent exponential damping.
// Any camera (editor fly camera, follow camera, cinematic camera) can own one instance:
// feed the desired pose every tick via AdvanceLag, then render from QueryPosition / QueryTarget.
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

private:
    CameraLagSettings   Settings         {};
    Mathematics::Axis3D SmoothedPosition { 0.0f, 0.0f, 0.0f }; // [m] - Damped camera position
    Mathematics::Axis3D SmoothedTarget   { 0.0f, 1.0f, 0.0f }; // [m] - Damped camera target point
    bool                Primed           = false;              // [-] - True after SnapTo / first AdvanceLag
};

} // namespace Frontier::Components
