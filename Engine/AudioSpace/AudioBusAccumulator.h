/*============================================================================================================================================
                                                           AUDIOBUSACCUMULATOR.H
============================================================================================================================================*/
// 🧩 Lock-free real-time DSP mixer: zero-alloc voice slots, Catmull-Rom Doppler, tunnel waveguide combs, Hadamard FDN, ducking & limiter

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/AudioSpace/AcousticTypes.h"
#include "Engine/AudioSpace/SpatialAcoustics.h"
#include "Engine/AudioSpace/WaveformDecoder.h"
#include "Engine/Threading/Sync/AtomicQueue.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::AudioSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class VoiceEnvelopePhase : uint8_t
{
    Inactive = 0u, // [-] - Slot free and ready for reuse
    Attack   = 1u, // [-] - Ramping up over AttackSeconds
    Sustain  = 2u, // [-] - Steady active playback or cyclic loop sustain
    Release  = 3u, // [-] - Smoothly decaying over ReleaseSeconds before slot recycling
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct AcousticBusControl
{
    float GainLinear = 1.0f;  // [-] - Target linear bus fader gain
    bool  Muted      = false; // [-] - True when bus output is silenced
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class AudioBusAccumulator
{
public:
    AudioBusAccumulator() noexcept;
    ~AudioBusAccumulator() noexcept = default;

    AudioBusAccumulator(const AudioBusAccumulator&)            = delete;
    AudioBusAccumulator& operator=(const AudioBusAccumulator&) = delete;

    void ConfigureFormat(
        uint32_t             SampleRateHz,
        SpatialSpeakerLayout SpeakerLayout,
        uint32_t             MaxBlockSamplePoints = Constants::DefaultAcousticBlockSamplePoints);

    void AttachWaveformRepository(const WaveformRepository* Repository) noexcept;

    void Reset() noexcept;

    // Lock-free main-thread -> audio-thread control methods
    [[nodiscard]] bool EnqueueCommand(const AcousticCommandEnvelope& Command) noexcept;
    void               PublishZones(std::span<const AcousticZoneDescriptor> Zones) noexcept;

    // Convenience main-thread helpers pushing lock-free commands directly
    [[nodiscard]] AcousticVoiceToken TriggerVoice(
        const AcousticVoiceDescriptor& Descriptor,
        uint32_t                       RandomSeed = 0u) noexcept;
    bool ReleaseVoice(AcousticVoiceToken Token) noexcept;
    bool StopVoiceImmediate(AcousticVoiceToken Token) noexcept;
    bool AssignVoiceTransform(
        AcousticVoiceToken  Token,
        Mathematics::Axis3D PositionMeters,
        Mathematics::Axis3D VelocityMps,
        float               OcclusionFactor = 0.0f) noexcept;
    bool AssignVoiceContinuousDrive(
        AcousticVoiceToken Token,
        float              GainMultiplier,
        float              PitchMultiplier,
        float              OcclusionFactor = 0.0f) noexcept;
    bool AssignReceiverPose(const AcousticReceiverPose& Pose) noexcept;
    bool AssignBusGain(AudioBusCategory Bus, float LinearGain) noexcept;
    bool AssignBusMute(AudioBusCategory Bus, bool Muted) noexcept;
    bool AssignEnvironmentPreset(AcousticEnvironmentPreset Preset) noexcept;
    bool AssignSpeakerLayout(SpatialSpeakerLayout Layout) noexcept;

    // Wait-free / allocation-free DSP mix pull invoked exclusively on AudioThread (or deterministic test loop)
    void AccumulateMixBlock(
        float*   OutInterleavedSamples,
        uint32_t SamplePointCount) noexcept;

    [[nodiscard]] uint32_t                 QuerySampleRateHz() const noexcept;
    [[nodiscard]] SpatialSpeakerLayout     QuerySpeakerLayout() const noexcept;
    [[nodiscard]] AcousticTelemetryMetrics InspectTelemetryMetrics() const noexcept;

private:
    struct OnePoleMemory
    {
        float PreviousY = 0.0f;
    };

    struct BiquadMemory
    {
        float X1 = 0.0f;
        float X2 = 0.0f;
        float Y1 = 0.0f;
        float Y2 = 0.0f;
    };

    struct AcousticVoiceSlot
    {
        AcousticVoiceToken      Token                {};
        VoiceEnvelopePhase      EnvelopePhase        = VoiceEnvelopePhase::Inactive;
        AcousticVoiceDescriptor Descriptor           {};
        WaveformToken           SelectedWaveform     {};
        double                  PlayheadSamplePoint  = 0.0;
        float                   EnvelopeGain         = 0.0f;
        float                   DriveGainMultiplier  = 1.0f;
        float                   DrivePitchMultiplier = 1.0f;
        float                   JitterGain           = 1.0f;
        float                   JitterPitch          = 1.0f;
        float                   SmoothedDopplerRatio = 1.0f;
        std::array<float, Constants::MaxAcousticOutputChannels> SmoothedSpeakerGains {};
        float                   SmoothedLfeGain      = 0.0f;
        float                   SmoothedDistanceGain = 0.0f;
        float                   SmoothedTunnelMix    = 0.0f;
        float                   SmoothedReverbSend   = 0.0f;
        float                   LastAudibility       = 0.0f;
        OnePoleMemory           OcclusionFilterLpf   {};
        OnePoleMemory           AirAbsorptionLpf     {};
        OnePoleMemory           BinauralLeftShadow   {};
        OnePoleMemory           BinauralRightShadow  {};
        std::array<float, Constants::MaxBinauralDelaySamplePoints> ItdDelayLine {};
        uint32_t                ItdWriteIndex        = 0u;
        bool                    SpatialInitialized   = false;
    };

    struct TunnelCombMemory
    {
        static constexpr uint32_t MaxCombLineSamples = 4096u;
        std::array<float, MaxCombLineSamples> DelayLine     {};
        uint32_t                              WriteCursor   = 0u;
        float                                 DampingMemory = 0.0f;
    };

    struct HadamardFdnNetwork
    {
        static constexpr uint32_t MaxDelayLineSamples = 4608u;
        std::array<std::array<float, MaxDelayLineSamples>, Constants::ReverbFdnLineCount> DelayLines {};
        std::array<uint32_t, Constants::ReverbFdnLineCount> WriteCursors {};
        std::array<float, Constants::ReverbFdnLineCount>    DampingMemory {};
        double LfoPhaseRadians = 0.0;
    };

    struct ZoneTripleBufferSlot
    {
        std::array<AcousticZoneDescriptor, Constants::MaxAcousticZones> Zones {};
        uint32_t ActiveCount = 0u;
    };

    void               DrainLockFreeCommands() noexcept;
    void               ApplySingleCommand(const AcousticCommandEnvelope& Command) noexcept;
    [[nodiscard]] bool ActivateVoiceInSlot(uint32_t SlotIdx, uint32_t ExpectedGeneration, const AcousticCommandEnvelope& Command) noexcept;
    void               ReclaimVoiceSlot(uint32_t SlotIdx) noexcept;
    void               ResolveActiveVoicePriority(
        std::array<uint32_t, Constants::MaxAcousticVoices>& ActiveOrder,
        uint32_t&                                           OutTotalActiveCount,
        uint32_t&                                           OutMixedCount,
        uint32_t&                                           OutVirtualizedCount) noexcept;
    void               ApplyTunnelWaveguideToBuffers(
        const AcousticSpatialSolution& DominantTunnelParams,
        uint32_t                       ChannelCount,
        uint32_t                       SamplePointCount) noexcept;
    void               ApplyDiffuseReverbToBuffers(
        float    ReverbDecaySeconds,
        float    WallDampingHz,
        uint32_t ChannelCount,
        uint32_t SamplePointCount) noexcept;

    uint32_t             SampleRateHz       = Constants::DefaultAcousticSampleRateHz;
    SpatialSpeakerLayout ActiveLayout       = SpatialSpeakerLayout::Stereo;
    uint32_t             ConfiguredBlockMax = Constants::DefaultAcousticBlockSamplePoints;

    const WaveformRepository*                       WaveformRepo = nullptr;
    Threading::AtomicQueue<AcousticCommandEnvelope> CommandQueue;

    AcousticReceiverPose      ActiveReceiver {};
    AcousticEnvironmentPreset FallbackPreset = AcousticEnvironmentPreset::OpenField;

    std::array<AcousticBusControl, Constants::MaxAudioBusCount> BusControls {};
    std::array<AcousticVoiceSlot, Constants::MaxAcousticVoices> VoiceSlots  {};
    std::array<std::atomic<uint8_t>, Constants::MaxAcousticVoices>  SlotClaimed     {};
    std::array<std::atomic<uint32_t>, Constants::MaxAcousticVoices> SlotGenerations {};
    std::atomic<uint32_t>                                           NextAllocSeed   { 1u };

    // Triple-buffered spatial zones published lock-free from main thread
    std::array<ZoneTripleBufferSlot, 3u> ZoneBuffers          {};
    std::atomic<uint32_t>                PublishedZoneSlot    { 0u };
    std::atomic<uint32_t>                PendingWriteZoneSlot { 1u };

    // Preallocated scratch DSP buffers (sized during ConfigureFormat, zero allocations on audio thread)
    std::vector<float> VoiceMonoScratch   {};
    std::vector<float> TunnelBusScratch   {};
    std::vector<float> ReverbBusScratch   {};
    std::vector<float> MasterMixScratch   {};
    std::array<std::vector<float>, Constants::MaxAudioBusCount> SubmixBusScratch {};

    // Tunnel waveguide & FDN reverberation filter memory
    std::array<TunnelCombMemory, Constants::TunnelCombTapCount> TunnelCombLines {};
    BiquadMemory                                                TunnelBoomBiquad {};
    OnePoleMemory                                               TunnelPostDamping {};
    HadamardFdnNetwork                                          TunnelFdn {};
    HadamardFdnNetwork                                          AmbientFdn {};
    AcousticSpatialSolution                                     RetainedTunnelParams {};
    float                                                       TunnelTailActiveSeconds = 0.0f;
    std::array<BiquadMemory, Constants::MaxAcousticOutputChannels> UnderwaterFilterLpf {};

    // Side-chain ducking envelope (Weapons & Dialogue duck Ambience)
    float AmbienceDuckingGain = 1.0f;
    float DuckingDetectorEnv  = 0.0f;

    // Wait-free telemetry mirror read by main/diagnostics thread
    std::atomic<uint64_t> TelemetryMixedBlocks       { 0ull };
    std::atomic<uint64_t> TelemetryRenderedPoints    { 0ull };
    std::atomic<uint32_t> TelemetryActiveMixedVoices { 0u };
    std::atomic<uint32_t> TelemetryVirtualizedVoices { 0u };
    std::atomic<uint32_t> TelemetryStolenVoices      { 0u };
    std::atomic<uint32_t> TelemetryDroppedCommands   { 0u };
    std::atomic<float>    TelemetryPeakOutput        { 0.0f };
    std::atomic<float>    TelemetryRmsOutput         { 0.0f };
    std::atomic<float>    TelemetryDominantTunnel    { 0.0f };
    std::atomic<float>    TelemetryActiveDoppler     { 1.0f };
    std::atomic<float>    TelemetryCpuMixMicros      { 0.0f };
};

} // namespace Frontier::AudioSpace
