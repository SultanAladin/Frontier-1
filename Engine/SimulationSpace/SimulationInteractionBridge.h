/*============================================================================================================================================
                                               SIMULATIONINTERACTIONBRIDGE.H
============================================================================================================================================*/
// 🧩 Phase J01 Zero-Allocation Interactive Manipulation Bridge connecting viewport/gameplay raycasts, soft-body grab/drag,
//    localized fluid brush strokes, and coupled radial blast impulses across Fluid & Deformable simulation subsystems.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/SimulationCoordinatorExtension.h"

#include <array>
#include <cstdint>

namespace Frontier::SimulationSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                              INTERACTION HANDLES & REPORTS
//------------------------------------------------------------------------------------------------------------------------

struct SoftBodyGrabHandle
{
    uint32_t             InstanceId        = 0u;
    int32_t              PointIndex        = -1;
    float                RayDistanceMeters = 0.0f;
    std::array<float, 3> HitWorldMeters    { 0.0f, 0.0f, 0.0f };
    bool                 Valid             = false;
};

struct RadialBlastReport
{
    uint32_t AffectedFluidVoxels     = 0u;
    uint32_t AffectedSoftBodyPoints  = 0u;
    float    PeakInjectedSpeedMps    = 0.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                            SIMULATION INTERACTION BRIDGE
//------------------------------------------------------------------------------------------------------------------------

class SimulationInteractionBridge
{
public:
    /// Ray-picks the nearest mobile (InverseMass > 0) vertex across all enabled world SoftBody instances (0 heap allocs).
    [[nodiscard]] static SoftBodyGrabHandle PickSoftBodyPoint(
        const SimulationCoordinatorExtension& coordinator,
        const std::array<float, 3>& rayOriginMeters,
        const std::array<float, 3>& rayDirectionUnit,
        float pickRadiusMeters = 0.25f,
        float maxRayDistanceMeters = 50.0f) noexcept;

    /// Pins the picked SoftBody point to `targetWorldMeters` for interactive cursor/VR dragging (0 heap allocs).
    [[nodiscard]] static bool BeginSoftBodyGrab(
        SimulationCoordinatorExtension& coordinator,
        const SoftBodyGrabHandle& handle) noexcept;

    /// Moves an active SoftBody grab target to `targetWorldMeters` (0 heap allocs).
    [[nodiscard]] static bool MoveSoftBodyGrab(
        SimulationCoordinatorExtension& coordinator,
        const SoftBodyGrabHandle& handle,
        const std::array<float, 3>& targetWorldMeters) noexcept;

    /// Releases an active SoftBody grab target (0 heap allocs).
    static void ReleaseSoftBodyGrab(
        SimulationCoordinatorExtension& coordinator,
        const SoftBodyGrabHandle& handle) noexcept;

    /// Injects a localized smoke/heat/velocity stroke directly into the 3D Fluid grid (0 heap allocs).
    /// Returns the number of modified fluid voxels.
    [[nodiscard]] static uint32_t InjectFluidBrushStroke(
        SimulationCoordinatorExtension& coordinator,
        const std::array<float, 3>& worldCentreMeters,
        const std::array<float, 3>& velocityMps,
        float densityAdd,
        float temperatureAdd,
        float radiusMeters) noexcept;

    /// Simultaneously injects an outward radial blast impulse into nearby Fluid voxels and mobile SoftBody vertices (0 allocs).
    [[nodiscard]] static RadialBlastReport InjectRadialBlastImpulse(
        SimulationCoordinatorExtension& coordinator,
        const std::array<float, 3>& blastCentreMeters,
        float blastRadiusMeters,
        float peakDeltaSpeedMps,
        float fluidTemperatureAdd = 1.8f) noexcept;
};

} // namespace Frontier::SimulationSpace
