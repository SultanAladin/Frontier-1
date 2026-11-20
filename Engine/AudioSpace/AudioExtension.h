/*============================================================================================================================================
                                                              AUDIOEXTENSION.H
============================================================================================================================================*/
// 🧩 SuspendableParticipant audio orchestrator unifying WaveformRepository, SpatialAcoustics, AudioBusAccumulator, AudioThread & HardwareOutput

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/AudioSpace/AcousticTypes.h"
#include "Engine/AudioSpace/AudioBusAccumulator.h"
#include "Engine/AudioSpace/AudioHardwareOutput.h"
#include "Engine/AudioSpace/SpatialAcoustics.h"
#include "Engine/AudioSpace/WaveformDecoder.h"
#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"
#include "Engine/Threading/Sync/ThreadProgressionTracker.h"
#include "Engine/Threading/Threads/Audio/AudioThread.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace Frontier::AudioSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct AudioExtensionConfiguration
{
    uint32_t                            SampleRateHz            = Constants::DefaultAcousticSampleRateHz;      // [Hz]
    SpatialSpeakerLayout                SpeakerLayout           = SpatialSpeakerLayout::Stereo;                // [-]
    uint32_t                            BlockSamplePoints       = Constants::DefaultAcousticBlockSamplePoints; // [count]
    AudioOutputDriverCategory          RequestedDriver        = AudioOutputDriverCategory::AutoSelect;      // [-]
    bool                                SpawnDedicatedOsThread  = true;                                        // [-]
    Threading::ThreadProgressionTracker* ProgressionMonitor     = nullptr;                                     // [-]
    bool                                CaptureOutputForExport  = false;                                       // [-]
    uint32_t                            MaxCapturedSamplePoints = 480000u;                                     // [count]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class AudioExtension final
{
public:
    AudioExtension() noexcept = default;
    explicit AudioExtension(const AudioExtensionConfiguration& InitialConfig) noexcept;
    ~AudioExtension() noexcept;

    AudioExtension(const AudioExtension&)            = delete;
    AudioExtension& operator=(const AudioExtension&) = delete;

    // Lifecycle::SuspendableParticipant interface
    [[nodiscard]] bool Initialize() noexcept;
    void               Tick(double Δt) noexcept;
    void               Suspend() noexcept;
    void               Resume() noexcept;
    void               Finalize() noexcept;

    void ConfigureExtension(const AudioExtensionConfiguration& Configuration) noexcept;

    // Waveform registration & disk loading
    [[nodiscard]] WaveformToken RegisterWaveform(WaveformBuffer Waveform) noexcept;
    [[nodiscard]] WaveformToken LoadWavWaveform(
        const std::filesystem::path& FilePath,
        std::string&                 OutDiagnostic);
    bool                        ReclaimWaveform(WaveformToken Token) noexcept;

    // One-shot & continuous playback API
    [[nodiscard]] AcousticVoiceToken TriggerOneShot(
        WaveformToken       Waveform,
        Mathematics::Axis3D PositionMeters,
        Mathematics::Axis3D VelocityMps     = { 0.0f, 0.0f, 0.0f },
        AudioBusCategory    Bus             = AudioBusCategory::Effects,
        float               NominalGain        = 1.0f,
        uint8_t             Priority        = 128u);

    [[nodiscard]] AcousticVoiceToken TriggerVariationCue(
        const AcousticVoiceDescriptor& Descriptor,
        uint32_t                       RandomSeed = 0u) noexcept;

    [[nodiscard]] AcousticVoiceToken StartContinuousLoop(
        WaveformToken       Waveform,
        Mathematics::Axis3D PositionMeters,
        Mathematics::Axis3D VelocityMps = { 0.0f, 0.0f, 0.0f },
        AudioBusCategory    Bus         = AudioBusCategory::Ambience,
        float               NominalGain    = 0.85f,
        uint8_t             Priority    = 160u);

    [[nodiscard]] AcousticVoiceToken StartDrivenLoop(
        const AcousticVoiceDescriptor& Descriptor) noexcept;

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
        float               OcclusionFactor = 0.0f) noexcept;

    // Receiver, layout, buses, presets & tunnel / environment zones
    bool AssignReceiverPose(const AcousticReceiverPose& Pose) noexcept;
    bool AssignReceiverFromCamera(const Components::CameraPosture& Posture) noexcept;
    bool AssignBusGain(AudioBusCategory Bus, float LinearGain) noexcept;
    bool AssignBusMute(AudioBusCategory Bus, bool Muted) noexcept;
    bool AssignEnvironmentPreset(AcousticEnvironmentPreset Preset) noexcept;
    bool AssignSpeakerLayout(SpatialSpeakerLayout Layout) noexcept;

    [[nodiscard]] AcousticZoneToken RegisterAcousticZone(const AcousticZoneDescriptor& Descriptor) noexcept;
    bool                            ReassignAcousticZone(AcousticZoneToken Token, const AcousticZoneDescriptor& Descriptor) noexcept;
    bool                            ReclaimAcousticZone(AcousticZoneToken Token) noexcept;

    // Synchronous physics & SDF coupling helpers
    [[nodiscard]] float ApplyPhysicsOcclusionQuery(
        const SimulationSpace::PhysicsSpace::PhysicsExtension& Physics,
        AcousticVoiceToken                                     Token,
        Mathematics::Axis3D                                    OriginPositionMeters,
        Mathematics::Axis3D                                    OriginVelocityMps,
        uint32_t                                               CollisionChannelMask = 0xFFFFFFFFu) noexcept;

    [[nodiscard]] SdfAcousticFieldReading ApplyRayAcousticField(
        const SpatialAcceleration::WideBoundingTree& SceneTree,
        AcousticVoiceToken                           Token,
        Mathematics::Axis3D                          OriginPositionMeters,
        Mathematics::Axis3D                          OriginVelocityMps) noexcept;

    // Deterministic synchronous mix pump (used for non-threaded CTest verification or offline rendering)
    uint32_t PumpAudioBlocks(uint32_t BlockCount) noexcept;

    [[nodiscard]] bool                     QueryInitialized() const noexcept { return ExtensionInitialized; }
    [[nodiscard]] bool                     QuerySuspended() const noexcept   { return ExtensionSuspended; }
    [[nodiscard]] AcousticTelemetryMetrics InspectTelemetryMetrics() const noexcept;
    [[nodiscard]] bool                     WriteCapturedWavFile(
        const std::filesystem::path& FilePath,
        std::string&                 OutDiagnostic) const;

    [[nodiscard]] WaveformRepository&        AccessWaveformRepository() noexcept { return Waveforms; }
    [[nodiscard]] AudioBusAccumulator&       AccessBusAccumulator() noexcept     { return Mixer; }
    [[nodiscard]] AudioHardwareOutput&       AccessHardwareOutput() noexcept     { return HardwareOutput; }
    [[nodiscard]] Threading::AudioThread&    AccessAudioThread() noexcept        { return RealTimeThread; }

private:
    static void PullMixBridge(void* OpaqueHost, float* OutInterleavedSamples, uint32_t SamplePointCount) noexcept;
    static bool PushSinkBridge(void* OpaqueHost, const float* InterleavedSamples, uint32_t SamplePointCount) noexcept;

    void PublishActiveZonesToMixer() noexcept;

    struct ZoneEntry
    {
        bool                   Occupied   = false;
        uint32_t               Generation = 1u;
        AcousticZoneDescriptor Descriptor {};
    };

    AudioExtensionConfiguration Config           {};
    WaveformRepository          Waveforms        {};
    AudioBusAccumulator         Mixer            {};
    AudioHardwareOutput         HardwareOutput   {};
    Threading::AudioThread      RealTimeThread   {};

    std::array<ZoneEntry, Constants::MaxAcousticZones> ZoneSlots  {};
    bool                                               ZonesDirty = false;

    AcousticReceiverPose CachedReceiver       {};
    double               ElapsedRuntimeSeconds = 0.0;
    bool                 ExtensionInitialized  = false;
    bool                 ExtensionSuspended    = false;
};

static_assert(Lifecycle::SuspendableParticipant<AudioExtension>, "AudioExtension must satisfy Lifecycle::SuspendableParticipant");

} // namespace Frontier::AudioSpace
