/*============================================================================================================================================
                                                    DEFORMABLETHREADEXCHANGE.H
============================================================================================================================================*/
// 🧩 Phase D12 Wait-free cross-thread simulation/render boundary built on Frontier::Threading::ThreadBoundaryExchange
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §4 (D12)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/DeformablePhysicsCoupling.h"
#include "Engine/SimulationSpace/Deformable/DeformableTypes.h"
#include "Engine/Threading/Sync/ThreadBoundaryExchange.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                            CROSS-THREAD DIRECTIVES & READINGS
//------------------------------------------------------------------------------------------------------------------------

enum class DeformableDirectiveCategory : uint8_t
{
    SwitchPreset       = 0u, // Load one of the 9 canonical DeformablePresetKind scenes
    ApplyConfiguration = 1u, // Update live solver / material / wind / tire configuration
    TriggerTireDrop    = 2u, // Lift tire axle vertically for impact drop test
    CutAlongSegment    = 3u, // Sever distance constraints near 3D line segment
    SetGrabTarget      = 4u, // Pin/drag interactive mass point toward world coordinate
    ReleaseGrab        = 5u, // Release grabbed mass point
    SetTireDeformMode  = 6u  // Switch between LiveGpuCage, BakedMotionAtlas, CoarseCage
};

struct DeformableControlDirective
{
    DeformableDirectiveCategory       Category       = DeformableDirectiveCategory::ApplyConfiguration;
    DeformablePresetKind              PresetKind     = DeformablePresetKind::SportTireFem;
    DeformableSimulationConfiguration Configuration  {};
    TireDeformMode                    TireMode       = TireDeformMode::LiveGpuCage;
    float                             DropLiftMeters = 0.85f;
    std::array<float, 3>              SegmentA       { 0.0f, 0.0f, 0.0f };
    std::array<float, 3>              SegmentB       { 0.0f, 0.0f, 0.0f };
    float                             CutRadius      = 0.2f;
    int32_t                           GrabPointIndex = -1;
};

struct DeformablePublishedReading
{
    uint64_t             StepOrdinal         = 0ull;
    double               SimulationSeconds   = 0.0;
    DeformablePresetKind ActivePreset        = DeformablePresetKind::SportTireFem;
    TireDeformMode       ActiveTireMode      = TireDeformMode::LiveGpuCage;
    uint32_t             PointCount          = 0u;
    uint32_t             TetCount            = 0u;
    uint32_t             SeveredCount        = 0u;
    float                PeakStrain          = 0.0f;
    float                AxleX               = 0.0f;
    float                AxleY               = 0.0f;
    float                SpinAngleRad        = 0.0f;
    std::array<float, 3> BoundsMinMeters     { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> BoundsMaxMeters     { 0.0f, 0.0f, 0.0f };
    std::vector<float>   Positions           {}; // 3 * PointCount
    std::vector<float>   PointStrain         {}; // PointCount
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 DEFORMABLE THREAD EXCHANGE
//------------------------------------------------------------------------------------------------------------------------

/// Owns a DeformableBody on the Physics/Simulation thread while allowing wait-free render reads and lock-free UI/Game directives.
class DeformableThreadExchange
{
public:
    explicit DeformableThreadExchange(DeformablePresetKind initialPreset = DeformablePresetKind::SportTireFem);

    // Lock-free directive submission (callable concurrently from GameThread / EditorThread / RenderThread)
    [[nodiscard]] bool EnqueueDirective(DeformableControlDirective directive) noexcept;

    // Wait-free multi-reader lease (callable concurrently from RenderThread / UI / Diagnostics)
    [[nodiscard]] Threading::ReadingLease<DeformablePublishedReading> AcquireReadLease() const noexcept;

    [[nodiscard]] uint64_t QueryPublishedGeneration() const noexcept;
    [[nodiscard]] uint64_t QueryDrainedDirectiveCount() const noexcept;

    // Authoritative owner-thread tick: drains directives, steps XPBD/3D FEM solver, and publishes wait-free reading.
    void StepAndPublishOnOwnerThread(
        float dtSeconds,
        float windPulse = 0.0f,
        const DeformableExternalCoupling* externalCoupling = nullptr);

    [[nodiscard]] const DeformableBody& InspectOwnerBody() const noexcept { return Body_; }
    [[nodiscard]] const DeformableSimulationConfiguration& InspectOwnerConfig() const noexcept { return Configuration_; }

private:
    void ApplyDirectiveOnOwnerThread(const DeformableControlDirective& directive);
    void PublishCurrentReading();

    Threading::ThreadBoundaryExchange<DeformablePublishedReading, DeformableControlDirective> Boundary_;
    DeformableBody                    Body_             {};
    DeformableSimulationConfiguration Configuration_    {};
    TireDeformMode                    ActiveTireMode_   = TireDeformMode::LiveGpuCage;
    uint64_t                          StepOrdinal_      = 0ull;
    double                            SimulationSeconds_ = 0.0;
};

} // namespace Frontier::SimulationSpace::Deformable
