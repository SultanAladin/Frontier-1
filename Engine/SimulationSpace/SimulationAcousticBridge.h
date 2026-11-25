/*============================================================================================================================================
                                                 SIMULATIONACOUSTICBRIDGE.H
============================================================================================================================================*/
// 🧩 Phase L02: Zero-allocation coupled AudioSpace & haptic telemetry synthesizer extracting deterministic spatial
//    acoustic cues (wind/fluid shear, soft-body tear/snap transients, shockwave blasts & tire rumble) per tick.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/SimulationCoordinatorExtension.h"
#include "Engine/SimulationSpace/SimulationSpatialQuery.h"

#include <array>
#include <cstdint>

namespace Frontier::SimulationSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                           ACOUSTIC & HAPTIC CUE POD PACKETS
//------------------------------------------------------------------------------------------------------------------------

enum class AcousticCueKind : uint8_t
{
    None            = 0u,
    WindFluidShear  = 1u,
    SoftBodyTear    = 2u,
    SoftBodySnap    = 3u,
    ShockwaveBlast  = 4u,
    TireScrubRumble = 5u
};

struct SimulationAcousticCue
{
    AcousticCueKind      Kind              = AcousticCueKind::None;
    uint8_t              Padding_[3]       { 0u, 0u, 0u };
    uint32_t             SourceInstanceId  = 0u;
    std::array<float, 3> WorldPosMeters    { 0.0f, 0.0f, 0.0f };
    float                GainNormalized    = 0.0f; // [0..1] distance-attenuated loudness
    float                PitchMultiplier   = 1.0f; // [0.5..2.0] spectral pitch modulation
    float                HapticLowFreq     = 0.0f; // [0..1] low-frequency rumble motor drive
    float                HapticHighFreq    = 0.0f; // [0..1] crisp transient actuator drive
};

struct SimulationAcousticPacket
{
    static constexpr uint32_t kMaxCuesPerTick = 16u;

    uint64_t                                          TickIndex            = 0ull;
    std::array<float, 3>                              ListenerWorldMeters  { 0.0f, 0.0f, 0.0f };
    float                                             AmbientWindIntensity = 0.0f; // [0..1]
    float                                             AmbientThermalRoar   = 0.0f; // [0..1]
    float                                             PeakSoftBodyStrain   = 0.0f;
    uint32_t                                          NewTearEvents        = 0u;
    uint32_t                                          CueCount             = 0u;
    std::array<SimulationAcousticCue, kMaxCuesPerTick> Cues                {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                              SIMULATION ACOUSTIC BRIDGE
//------------------------------------------------------------------------------------------------------------------------

class SimulationAcousticBridge
{
public:
    static constexpr uint32_t kMaxTrackedBodies = 64u;

    void Reset() noexcept;

    /// Registers an instantaneous radial shockwave blast to be emitted & attenuated on the next ExtractPacket call (0 allocs).
    void NotifyRadialBlast(const std::array<float, 3>& worldPosMeters, float peakSpeedMps) noexcept;

    /// Extracts a zero-allocation POD acoustic & haptic cue packet relative to `listenerWorldMeters`.
    [[nodiscard]] SimulationAcousticPacket ExtractPacket(
        const SimulationCoordinatorExtension& coordinator,
        const std::array<float, 3>& listenerWorldMeters) noexcept;

private:
    static float ComputeDistanceAttenuation(
        const std::array<float, 3>& sourcePos,
        const std::array<float, 3>& listenerPos,
        float referenceDistanceMeters = 2.0f) noexcept;

    static void PushCue(SimulationAcousticPacket& packet, const SimulationAcousticCue& cue) noexcept;

    std::array<uint32_t, kMaxTrackedBodies> TrackedInstanceIds_ {};
    std::array<uint32_t, kMaxTrackedBodies> PrevSeveredCounts_  {};
    uint32_t                                TrackedCount_       = 0u;
    bool                                    PendingBlast_       = false;
    std::array<float, 3>                    PendingBlastPos_    { 0.0f, 0.0f, 0.0f };
    float                                   PendingBlastSpeed_  = 0.0f;
};

} // namespace Frontier::SimulationSpace
