/*============================================================================================================================================
                                                             AUDIOEXTENSION.CPP
============================================================================================================================================*/
// 🧩 Implementation of SuspendableParticipant AudioExtension orchestrating real-time AudioThread, Mixer, HardwareOutput, Zones & Physics/SDF

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/AudioSpace/AudioExtension.h"

#include <algorithm>
#include <array>

namespace Frontier::AudioSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                             LIFECYCLE & INITIALIZATION
//------------------------------------------------------------------------------------------------------------------------

AudioExtension::AudioExtension(const AudioExtensionConfiguration& InitialConfig) noexcept
    : Config(InitialConfig)
{
}

AudioExtension::~AudioExtension() noexcept
{
    Finalize();
}

void AudioExtension::ConfigureExtension(const AudioExtensionConfiguration& Configuration) noexcept
{
    if (!ExtensionInitialized)
    {
        Config = Configuration;
    }
}

bool AudioExtension::Initialize() noexcept
{
    if (ExtensionInitialized)
    {
        return true;
    }

    const uint32_t SampleRate  = std::max(8000u, Config.SampleRateHz);
    const uint32_t BlockPoints = std::clamp(Config.BlockSamplePoints, 32u, 4096u);
    const uint32_t Channels    = ResolveChannelCount(Config.SpeakerLayout);

    Mixer.ConfigureFormat(SampleRate, Config.SpeakerLayout, BlockPoints);
    Mixer.AttachWaveformRepository(&Waveforms);

    AudioEndpointConfiguration EndpointConfig{};
    EndpointConfig.RequestedDriver         = Config.RequestedDriver;
    EndpointConfig.SampleRateHz             = SampleRate;
    EndpointConfig.ChannelCount             = Channels;
    EndpointConfig.BlockSamplePoints        = BlockPoints;
    EndpointConfig.RealTimePacingInHeadless = false;

    if (!HardwareOutput.OpenEndpoint(EndpointConfig))
    {
        return false;
    }

    HardwareOutput.ConfigureCaptureBuffer(Config.CaptureOutputForExport, Config.MaxCapturedSamplePoints);

    Threading::AudioThreadConfiguration ThreadConfig{};
    ThreadConfig.SampleRateHz           = SampleRate;
    ThreadConfig.ChannelCount           = Channels;
    ThreadConfig.BlockSamplePoints      = BlockPoints;
    ThreadConfig.SpawnDedicatedOsThread = Config.SpawnDedicatedOsThread;
    ThreadConfig.PullMixCallback        = &AudioExtension::PullMixBridge;
    ThreadConfig.PushSinkCallback       = &AudioExtension::PushSinkBridge;
    ThreadConfig.OpaqueHost             = this;

    if (!RealTimeThread.Initialize(ThreadConfig, Config.ProgressionMonitor))
    {
        HardwareOutput.CloseEndpoint();
        return false;
    }

    ElapsedRuntimeSeconds = 0.0;
    ExtensionSuspended    = false;
    ExtensionInitialized  = true;
    PublishActiveZonesToMixer();
    return true;
}

void AudioExtension::Tick(double Δt) noexcept
{
    if (!ExtensionInitialized)
    {
        return;
    }

    if (Δt > 0.0)
    {
        ElapsedRuntimeSeconds += Δt;
    }

    if (!ExtensionSuspended && ZonesDirty)
    {
        PublishActiveZonesToMixer();
        ZonesDirty = false;
    }
}

void AudioExtension::Suspend() noexcept
{
    if (!ExtensionInitialized || ExtensionSuspended)
    {
        return;
    }
    ExtensionSuspended = true;
    RealTimeThread.SuspendAudioThread();
}

void AudioExtension::Resume() noexcept
{
    if (!ExtensionInitialized || !ExtensionSuspended)
    {
        return;
    }
    ExtensionSuspended = false;
    RealTimeThread.ResumeAudioThread();
}

void AudioExtension::Finalize() noexcept
{
    if (!ExtensionInitialized)
    {
        return;
    }

    RealTimeThread.Finalize();
    HardwareOutput.CloseEndpoint();
    Mixer.Reset();
    Waveforms.Reset();

    for (ZoneEntry& Entry : ZoneSlots)
    {
        Entry.Occupied = false;
    }
    ZonesDirty           = false;
    ExtensionSuspended   = false;
    ExtensionInitialized = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           WAVEFORMS, TRIGGERS & CONTINUOUS LOOPS
//------------------------------------------------------------------------------------------------------------------------

WaveformToken AudioExtension::RegisterWaveform(WaveformBuffer Waveform) noexcept
{
    return Waveforms.RegisterWaveform(std::move(Waveform), Mixer.QuerySampleRateHz());
}

WaveformToken AudioExtension::LoadWavWaveform(
    const std::filesystem::path& FilePath,
    std::string&                 OutDiagnostic)
{
    WaveformBuffer Decoded{};
    if (!WaveformDecoder::LoadWavFromFile(FilePath, Decoded, OutDiagnostic))
    {
        return {};
    }
    WaveformToken Token = RegisterWaveform(std::move(Decoded));
    if (!Token)
    {
        OutDiagnostic = "Waveform repository capacity exhausted while registering: " + FilePath.string();
    }
    return Token;
}

bool AudioExtension::ReclaimWaveform(WaveformToken Token) noexcept
{
    return Waveforms.ReclaimWaveform(Token);
}

AcousticVoiceToken AudioExtension::TriggerOneShot(
    WaveformToken       Waveform,
    Mathematics::Axis3D PositionMeters,
    Mathematics::Axis3D VelocityMps,
    AudioBusCategory    Bus,
    float               NominalGain,
    uint8_t             Priority)
{
    if (!ExtensionInitialized || !Waveform)
    {
        return {};
    }

    AcousticVoiceDescriptor Desc{};
    Desc.WaveformSlots[0] = Waveform;
    Desc.VariationCount   = 1u;
    Desc.Bus              = Bus;
    Desc.PlaybackCycle          = PlaybackCycleCategory::OneShotTrigger;
    Desc.PositionMeters   = PositionMeters;
    Desc.VelocityMps      = VelocityMps;
    Desc.NominalGain         = NominalGain;
    Desc.Priority         = Priority;
    Desc.AttackSeconds    = 0.002f;
    Desc.ReleaseSeconds   = 0.035f;
    return Mixer.TriggerVoice(Desc);
}

AcousticVoiceToken AudioExtension::TriggerVariationCue(
    const AcousticVoiceDescriptor& Descriptor,
    uint32_t                       RandomSeed) noexcept
{
    if (!ExtensionInitialized)
    {
        return {};
    }
    return Mixer.TriggerVoice(Descriptor, RandomSeed);
}

AcousticVoiceToken AudioExtension::StartContinuousLoop(
    WaveformToken       Waveform,
    Mathematics::Axis3D PositionMeters,
    Mathematics::Axis3D VelocityMps,
    AudioBusCategory    Bus,
    float               NominalGain,
    uint8_t             Priority)
{
    if (!ExtensionInitialized || !Waveform)
    {
        return {};
    }

    AcousticVoiceDescriptor Desc{};
    Desc.WaveformSlots[0]          = Waveform;
    Desc.VariationCount            = 1u;
    Desc.Bus                       = Bus;
    Desc.PlaybackCycle                   = PlaybackCycleCategory::ContinuousLoop;
    Desc.PositionMeters            = PositionMeters;
    Desc.VelocityMps               = VelocityMps;
    Desc.NominalGain                  = NominalGain;
    Desc.Priority                  = Priority;
    Desc.AttackSeconds             = 0.015f;
    Desc.ReleaseSeconds            = 0.060f;
    Desc.LoopCrossfadeSamplePoints = 512u;
    Desc.VirtualizeWhenQuiet       = true;
    return Mixer.TriggerVoice(Desc);
}

AcousticVoiceToken AudioExtension::StartDrivenLoop(const AcousticVoiceDescriptor& Descriptor) noexcept
{
    if (!ExtensionInitialized)
    {
        return {};
    }
    AcousticVoiceDescriptor Driven = Descriptor;
    Driven.PlaybackCycle                 = PlaybackCycleCategory::DrivenContinuous;
    Driven.VirtualizeWhenQuiet     = true;
    return Mixer.TriggerVoice(Driven);
}

bool AudioExtension::ReleaseVoice(AcousticVoiceToken Token) noexcept
{
    return ExtensionInitialized && Mixer.ReleaseVoice(Token);
}

bool AudioExtension::StopVoiceImmediate(AcousticVoiceToken Token) noexcept
{
    return ExtensionInitialized && Mixer.StopVoiceImmediate(Token);
}

bool AudioExtension::AssignVoiceTransform(
    AcousticVoiceToken  Token,
    Mathematics::Axis3D PositionMeters,
    Mathematics::Axis3D VelocityMps,
    float               OcclusionFactor) noexcept
{
    return ExtensionInitialized && Mixer.AssignVoiceTransform(Token, PositionMeters, VelocityMps, OcclusionFactor);
}

bool AudioExtension::AssignVoiceContinuousDrive(
    AcousticVoiceToken Token,
    float              GainMultiplier,
    float              PitchMultiplier,
    float              OcclusionFactor) noexcept
{
    return ExtensionInitialized && Mixer.AssignVoiceContinuousDrive(Token, GainMultiplier, PitchMultiplier, OcclusionFactor);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           RECEIVER, LAYOUTS, BUSES & ZONES
//------------------------------------------------------------------------------------------------------------------------

bool AudioExtension::AssignReceiverPose(const AcousticReceiverPose& Pose) noexcept
{
    CachedReceiver = Pose;
    return ExtensionInitialized && Mixer.AssignReceiverPose(Pose);
}

bool AudioExtension::AssignReceiverFromCamera(const Components::CameraPosture& Posture) noexcept
{
    const AcousticReceiverPose Pose = SpatialAcoustics::BuildReceiverPoseFromCamera(Posture);
    return AssignReceiverPose(Pose);
}

bool AudioExtension::AssignBusGain(AudioBusCategory Bus, float LinearGain) noexcept
{
    return ExtensionInitialized && Mixer.AssignBusGain(Bus, LinearGain);
}

bool AudioExtension::AssignBusMute(AudioBusCategory Bus, bool Muted) noexcept
{
    return ExtensionInitialized && Mixer.AssignBusMute(Bus, Muted);
}

bool AudioExtension::AssignEnvironmentPreset(AcousticEnvironmentPreset Preset) noexcept
{
    return ExtensionInitialized && Mixer.AssignEnvironmentPreset(Preset);
}

bool AudioExtension::AssignSpeakerLayout(SpatialSpeakerLayout Layout) noexcept
{
    if (!ExtensionInitialized)
    {
        Config.SpeakerLayout = Layout;
        return true;
    }

    const bool Enqueued = Mixer.AssignSpeakerLayout(Layout);
    RealTimeThread.ConfigureActiveChannelCount(ResolveChannelCount(Layout));
    return Enqueued;
}

AcousticZoneToken AudioExtension::RegisterAcousticZone(const AcousticZoneDescriptor& Descriptor) noexcept
{
    for (uint32_t Idx = 0u; Idx < Constants::MaxAcousticZones; ++Idx)
    {
        if (!ZoneSlots[Idx].Occupied)
        {
            ZoneSlots[Idx].Occupied   = true;
            ZoneSlots[Idx].Descriptor = Descriptor;
            ZonesDirty                = true;
            if (ExtensionInitialized)
            {
                PublishActiveZonesToMixer();
                ZonesDirty = false;
            }
            return AcousticZoneToken{ Idx + 1u, ZoneSlots[Idx].Generation };
        }
    }
    return {};
}

bool AudioExtension::ReassignAcousticZone(AcousticZoneToken Token, const AcousticZoneDescriptor& Descriptor) noexcept
{
    if (!Token || Token.SlotIndex > Constants::MaxAcousticZones)
    {
        return false;
    }
    ZoneEntry& Entry = ZoneSlots[Token.SlotIndex - 1u];
    if (!Entry.Occupied || Entry.Generation != Token.Generation)
    {
        return false;
    }
    Entry.Descriptor = Descriptor;
    ZonesDirty       = true;
    if (ExtensionInitialized)
    {
        PublishActiveZonesToMixer();
        ZonesDirty = false;
    }
    return true;
}

bool AudioExtension::ReclaimAcousticZone(AcousticZoneToken Token) noexcept
{
    if (!Token || Token.SlotIndex > Constants::MaxAcousticZones)
    {
        return false;
    }
    ZoneEntry& Entry = ZoneSlots[Token.SlotIndex - 1u];
    if (!Entry.Occupied || Entry.Generation != Token.Generation)
    {
        return false;
    }
    Entry.Occupied   = false;
    Entry.Generation = (Entry.Generation + 1u == 0u) ? 1u : (Entry.Generation + 1u);
    ZonesDirty       = true;
    if (ExtensionInitialized)
    {
        PublishActiveZonesToMixer();
        ZonesDirty = false;
    }
    return true;
}

float AudioExtension::ApplyPhysicsOcclusionQuery(
    const SimulationSpace::PhysicsSpace::PhysicsExtension& Physics,
    AcousticVoiceToken                                     Token,
    Mathematics::Axis3D                                    OriginPositionMeters,
    Mathematics::Axis3D                                    OriginVelocityMps,
    uint32_t                                               CollisionChannelMask) noexcept
{
    const float Occlusion = SpatialAcoustics::EvaluatePhysicsOcclusion(
        Physics,
        CachedReceiver.PositionMeters,
        OriginPositionMeters,
        CollisionChannelMask);
    if (ExtensionInitialized && Token)
    {
        (void)AssignVoiceTransform(Token, OriginPositionMeters, OriginVelocityMps, Occlusion);
    }
    return Occlusion;
}

SdfAcousticFieldReading AudioExtension::ApplyRayAcousticField(
    const SpatialAcceleration::WideBoundingTree& SceneTree,
    AcousticVoiceToken                           Token,
    Mathematics::Axis3D                          OriginPositionMeters,
    Mathematics::Axis3D                          OriginVelocityMps) noexcept
{
    const SdfAcousticFieldReading Reading = SpatialAcoustics::EvaluateRayAcousticField(
        SceneTree,
        CachedReceiver.PositionMeters,
        OriginPositionMeters);
    if (ExtensionInitialized && Token)
    {
        (void)AssignVoiceTransform(Token, OriginPositionMeters, OriginVelocityMps, Reading.OcclusionFactor);
    }
    return Reading;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             DETERMINISTIC MIX PUMP & BRIDGES
//------------------------------------------------------------------------------------------------------------------------

uint32_t AudioExtension::PumpAudioBlocks(uint32_t BlockCount) noexcept
{
    if (!ExtensionInitialized || ExtensionSuspended)
    {
        return 0u;
    }

    uint32_t Pumped = 0u;
    for (uint32_t Idx = 0u; Idx < BlockCount; ++Idx)
    {
        if (RealTimeThread.PumpSingleMixBlock())
        {
            ++Pumped;
        }
    }
    return Pumped;
}

AcousticTelemetryMetrics AudioExtension::InspectTelemetryMetrics() const noexcept
{
    AcousticTelemetryMetrics Metrics = Mixer.InspectTelemetryMetrics();
    Metrics.UnderrunCount            = HardwareOutput.QueryUnderrunCount();
    Metrics.AudioThreadIterations    = RealTimeThread.QueryIterationCount();
    return Metrics;
}

bool AudioExtension::WriteCapturedWavFile(
    const std::filesystem::path& FilePath,
    std::string&                 OutDiagnostic) const
{
    return HardwareOutput.WriteCapturedWavFile(FilePath, OutDiagnostic);
}

void AudioExtension::PullMixBridge(void* OpaqueHost, float* OutInterleavedSamples, uint32_t SamplePointCount) noexcept
{
    if (OpaqueHost != nullptr)
    {
        static_cast<AudioExtension*>(OpaqueHost)->Mixer.AccumulateMixBlock(OutInterleavedSamples, SamplePointCount);
    }
}

bool AudioExtension::PushSinkBridge(void* OpaqueHost, const float* InterleavedSamples, uint32_t SamplePointCount) noexcept
{
    if (OpaqueHost == nullptr)
    {
        return false;
    }
    return static_cast<AudioExtension*>(OpaqueHost)->HardwareOutput.SubmitInterleavedBlock(InterleavedSamples, SamplePointCount);
}

void AudioExtension::PublishActiveZonesToMixer() noexcept
{
    std::array<AcousticZoneDescriptor, Constants::MaxAcousticZones> StagingZones{};
    uint32_t ActiveCount = 0u;
    for (const ZoneEntry& Entry : ZoneSlots)
    {
        if (Entry.Occupied && ActiveCount < Constants::MaxAcousticZones)
        {
            StagingZones[ActiveCount++] = Entry.Descriptor;
        }
    }
    Mixer.PublishZones(std::span<const AcousticZoneDescriptor>(StagingZones.data(), ActiveCount));
}

} // namespace Frontier::AudioSpace
