/*============================================================================================================================================
                                                          AUDIOBUSACCUMULATOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of zero-alloc real-time DSP mixing, Catmull-Rom Doppler, tunnel waveguides, Hadamard FDN reverb, ducking & limiting

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/AudioSpace/AudioBusAccumulator.h"
#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace Frontier::AudioSpace
{

namespace
{

[[nodiscard]] float FlushDenormal(float Sample) noexcept
{
    return (std::abs(Sample) < 1.0e-18f) ? 0.0f : Sample;
}

[[nodiscard]] uint32_t StepRng(uint32_t& Seed) noexcept
{
    uint32_t X = (Seed == 0u) ? 0x243F6A88u : Seed;
    X ^= X << 13u;
    X ^= X >> 17u;
    X ^= X << 5u;
    Seed = X;
    return X;
}

[[nodiscard]] float NextSignedUnitFloat(uint32_t& Seed) noexcept
{
    return (static_cast<float>(StepRng(Seed) & 0x00FFFFFFu) / 8388608.0f) - 1.0f;
}

[[nodiscard]] float EvaluateOnePoleHighCut(float Input, float CutoffHz, float SampleRateHz, float& Memory) noexcept
{
    if (CutoffHz >= SampleRateHz * 0.44f)
    {
        Memory = Input;
        return Input;
    }
    const float ClampedCutoff = std::clamp(CutoffHz, 20.0f, SampleRateHz * 0.44f);
    const float Omega         = Constants::TauFloat * ClampedCutoff / SampleRateHz;
    const float Alpha         = Omega / (1.0f + Omega);
    Memory                    = FlushDenormal(Memory + Alpha * (Input - Memory));
    return Memory;
}

[[nodiscard]] float EvaluateBiquadHighCut(
    float  Input,
    float  CutoffHz,
    float  SampleRateHz,
    float& X1,
    float& X2,
    float& Y1,
    float& Y2) noexcept
{
    const float Fc     = std::clamp(CutoffHz, 40.0f, SampleRateHz * 0.45f);
    const float Omega  = Constants::TauFloat * Fc / SampleRateHz;
    const float CosW   = std::cos(Omega);
    const float SinW   = std::sin(Omega);
    const float Alpha  = SinW / (2.0f * 0.70710678f);
    const float InvA0  = 1.0f / (1.0f + Alpha);
    const float B1     = (1.0f - CosW) * InvA0;
    const float B0     = 0.5f * B1;
    const float B2     = B0;
    const float A1     = (-2.0f * CosW) * InvA0;
    const float A2     = (1.0f - Alpha) * InvA0;

    const float Out = FlushDenormal(B0 * Input + B1 * X1 + B2 * X2 - A1 * Y1 - A2 * Y2);
    X2 = X1;
    X1 = Input;
    Y2 = Y1;
    Y1 = Out;
    return Out;
}

[[nodiscard]] float EvaluateBiquadPeaking(
    float  Input,
    float  CenterHz,
    float  QFactor,
    float  BoostDb,
    float  SampleRateHz,
    float& X1,
    float& X2,
    float& Y1,
    float& Y2) noexcept
{
    const float GainA  = std::pow(10.0f, BoostDb / 40.0f);
    const float Omega  = Constants::TauFloat * std::clamp(CenterHz, 30.0f, SampleRateHz * 0.45f) / SampleRateHz;
    const float CosW   = std::cos(Omega);
    const float SinW   = std::sin(Omega);
    const float Alpha  = SinW / (2.0f * std::max(0.25f, QFactor));
    const float InvA0  = 1.0f / (1.0f + Alpha / GainA);
    const float B0     = (1.0f + Alpha * GainA) * InvA0;
    const float B1     = (-2.0f * CosW) * InvA0;
    const float B2     = (1.0f - Alpha * GainA) * InvA0;
    const float A1     = B1;
    const float A2     = (1.0f - Alpha / GainA) * InvA0;

    const float Out = FlushDenormal(B0 * Input + B1 * X1 + B2 * X2 - A1 * Y1 - A2 * Y2);
    X2 = X1;
    X1 = Input;
    Y2 = Y1;
    Y1 = Out;
    return Out;
}

[[nodiscard]] float SampleMonoWithLoopCrossfade(
    const WaveformBuffer& Buffer,
    double                Playhead,
    bool                  WrapLoop,
    uint32_t              CrossfadeCount) noexcept
{
    auto ReadMonoAt = [&](double Pos) noexcept -> float
    {
        if (Buffer.ChannelCount == 1u)
        {
            return WaveformDecoder::SampleChannelCubic(Buffer, 0u, Pos, WrapLoop);
        }
        const float L = WaveformDecoder::SampleChannelCubic(Buffer, 0u, Pos, WrapLoop);
        const float R = WaveformDecoder::SampleChannelCubic(Buffer, 1u, Pos, WrapLoop);
        return 0.5f * (L + R);
    };

    const float MainSample = ReadMonoAt(Playhead);
    if (!WrapLoop || CrossfadeCount == 0u)
    {
        return MainSample;
    }

    const uint32_t LoopStart = Buffer.LoopEnabled ? Buffer.LoopStartSamplePoint : 0u;
    const uint32_t LoopEnd   = (Buffer.LoopEnabled && Buffer.LoopEndSamplePoint > LoopStart)
                                   ? std::min(Buffer.LoopEndSamplePoint, Buffer.SamplePointCount)
                                   : Buffer.SamplePointCount;
    const uint32_t LoopSpan  = (LoopEnd > LoopStart) ? (LoopEnd - LoopStart) : 1u;
    const uint32_t SafeXfade = std::min(CrossfadeCount, LoopSpan / 4u);
    if (SafeXfade < 2u)
    {
        return MainSample;
    }

    const double TailStart = static_cast<double>(LoopEnd - SafeXfade);
    if (Playhead >= TailStart && Playhead < static_cast<double>(LoopEnd))
    {
        const float  Ratio    = std::clamp(static_cast<float>(Playhead - TailStart) / static_cast<float>(SafeXfade), 0.0f, 1.0f);
        const float  WeightTail = std::cos(Ratio * 0.5f * Constants::PiFloat);
        const float  WeightHead = std::sin(Ratio * 0.5f * Constants::PiFloat);
        const double HeadPos    = static_cast<double>(LoopStart) + (Playhead - TailStart);
        return MainSample * WeightTail + ReadMonoAt(HeadPos) * WeightHead;
    }
    return MainSample;
}

[[nodiscard]] float SoftLimitSample(float Input) noexcept
{
    constexpr float Knee = 0.85f;
    constexpr float Ceiling = Constants::MasterLimiterCeilingLinear;
    const float AbsVal = std::abs(Input);
    if (AbsVal <= Knee)
    {
        return Input;
    }
    const float Excess   = (AbsVal - Knee) / std::max(0.01f, Ceiling - Knee);
    const float Shaped   = Knee + (Ceiling - Knee) * std::tanh(Excess);
    return (Input >= 0.0f) ? Shaped : -Shaped;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  CONSTRUCTOR & SETUP
//------------------------------------------------------------------------------------------------------------------------

AudioBusAccumulator::AudioBusAccumulator() noexcept
    : CommandQueue(Constants::AudioCommandQueueCapacity)
{
    for (uint32_t SlotIdx = 0u; SlotIdx < Constants::MaxAcousticVoices; ++SlotIdx)
    {
        SlotClaimed[SlotIdx].store(0u, std::memory_order_relaxed);
        SlotGenerations[SlotIdx].store(1u, std::memory_order_relaxed);
    }
    ConfigureFormat(
        Constants::DefaultAcousticSampleRateHz,
        SpatialSpeakerLayout::Stereo,
        Constants::DefaultAcousticBlockSamplePoints);
}

void AudioBusAccumulator::ConfigureFormat(
    uint32_t             TargetSampleRateHz,
    SpatialSpeakerLayout SpeakerLayout,
    uint32_t             MaxBlockSamplePoints)
{
    SampleRateHz       = std::max(8000u, TargetSampleRateHz);
    ActiveLayout       = SpeakerLayout;
    ConfiguredBlockMax = std::clamp(MaxBlockSamplePoints, 32u, 4096u);

    const std::size_t MaxInterleavedFloats = static_cast<std::size_t>(ConfiguredBlockMax) * Constants::MaxAcousticOutputChannels;
    VoiceMonoScratch.assign(ConfiguredBlockMax, 0.0f);
    TunnelBusScratch.assign(ConfiguredBlockMax, 0.0f);
    ReverbBusScratch.assign(ConfiguredBlockMax, 0.0f);
    MasterMixScratch.assign(MaxInterleavedFloats, 0.0f);
    for (std::vector<float>& BusBuffer : SubmixBusScratch)
    {
        BusBuffer.assign(MaxInterleavedFloats, 0.0f);
    }

    Reset();
}

void AudioBusAccumulator::AttachWaveformRepository(const WaveformRepository* Repository) noexcept
{
    WaveformRepo = Repository;
}

void AudioBusAccumulator::Reset() noexcept
{
    ActiveReceiver = {};
    FallbackPreset = AcousticEnvironmentPreset::OpenField;

    for (AcousticBusControl& Bus : BusControls)
    {
        Bus.GainLinear = 1.0f;
        Bus.Muted      = false;
    }

    AcousticCommandEnvelope Discarded{};
    while (CommandQueue.TryDequeue(Discarded))
    {
    }

    for (uint32_t Idx = 0u; Idx < Constants::MaxAcousticVoices; ++Idx)
    {
        VoiceSlots[Idx]             = {};
        VoiceSlots[Idx].EnvelopePhase = VoiceEnvelopePhase::Inactive;
        SlotClaimed[Idx].store(0u, std::memory_order_release);
        SlotGenerations[Idx].store(1u, std::memory_order_release);
    }

    for (ZoneTripleBufferSlot& Slot : ZoneBuffers)
    {
        Slot.ActiveCount = 0u;
    }
    PublishedZoneSlot.store(0u, std::memory_order_release);
    PendingWriteZoneSlot.store(1u, std::memory_order_release);

    for (TunnelCombMemory& Comb : TunnelCombLines)
    {
        Comb.DelayLine.fill(0.0f);
        Comb.WriteCursor   = 0u;
        Comb.DampingMemory = 0.0f;
    }
    TunnelBoomBiquad  = {};
    TunnelPostDamping = {};

    auto ClearFdn = [](HadamardFdnNetwork& Network) noexcept
    {
        for (auto& Line : Network.DelayLines)
        {
            Line.fill(0.0f);
        }
        Network.WriteCursors.fill(0u);
        Network.DampingMemory.fill(0.0f);
        Network.LfoPhaseRadians = 0.0;
    };
    ClearFdn(TunnelFdn);
    ClearFdn(AmbientFdn);
    RetainedTunnelParams = {};
    RetainedTunnelParams.TunnelWidthMeters      = 6.0f;
    RetainedTunnelParams.TunnelHeightMeters     = 4.5f;
    RetainedTunnelParams.TunnelLengthMeters     = 35.0f;
    RetainedTunnelParams.TunnelWallReflectivity = 0.84f;
    RetainedTunnelParams.TunnelWallDampingHz    = 3200.0f;
    TunnelTailActiveSeconds = 0.0f;
    UnderwaterFilterLpf.fill(BiquadMemory{});

    AmbienceDuckingGain = 1.0f;
    DuckingDetectorEnv  = 0.0f;

    TelemetryMixedBlocks.store(0ull, std::memory_order_release);
    TelemetryRenderedPoints.store(0ull, std::memory_order_release);
    TelemetryActiveMixedVoices.store(0u, std::memory_order_release);
    TelemetryVirtualizedVoices.store(0u, std::memory_order_release);
    TelemetryStolenVoices.store(0u, std::memory_order_release);
    TelemetryDroppedCommands.store(0u, std::memory_order_release);
    TelemetryPeakOutput.store(0.0f, std::memory_order_release);
    TelemetryRmsOutput.store(0.0f, std::memory_order_release);
    TelemetryDominantTunnel.store(0.0f, std::memory_order_release);
    TelemetryActiveDoppler.store(1.0f, std::memory_order_release);
    TelemetryCpuMixMicros.store(0.0f, std::memory_order_release);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              LOCK-FREE COMMAND ENQUEUEING
//------------------------------------------------------------------------------------------------------------------------

bool AudioBusAccumulator::EnqueueCommand(const AcousticCommandEnvelope& Command) noexcept
{
    if (!CommandQueue.TryEnqueue(Command))
    {
        TelemetryDroppedCommands.fetch_add(1u, std::memory_order_relaxed);
        return false;
    }
    return true;
}

void AudioBusAccumulator::PublishZones(std::span<const AcousticZoneDescriptor> Zones) noexcept
{
    const uint32_t ActivePublished = PublishedZoneSlot.load(std::memory_order_acquire);
    uint32_t       WriteSlot       = PendingWriteZoneSlot.load(std::memory_order_relaxed);
    if (WriteSlot == ActivePublished)
    {
        WriteSlot = (WriteSlot + 1u) % 3u;
    }

    const uint32_t ClampedCount      = static_cast<uint32_t>(std::min<std::size_t>(Zones.size(), Constants::MaxAcousticZones));
    ZoneBuffers[WriteSlot].ActiveCount = ClampedCount;
    for (uint32_t Idx = 0u; Idx < ClampedCount; ++Idx)
    {
        ZoneBuffers[WriteSlot].Zones[Idx] = Zones[Idx];
    }

    const uint32_t PreviousPublished = PublishedZoneSlot.exchange(WriteSlot, std::memory_order_acq_rel);
    PendingWriteZoneSlot.store(PreviousPublished, std::memory_order_release);
}

AcousticVoiceToken AudioBusAccumulator::TriggerVoice(
    const AcousticVoiceDescriptor& Descriptor,
    uint32_t                       RandomSeed) noexcept
{
    AcousticVoiceToken ReservedToken{};
    for (uint32_t Idx = 0u; Idx < Constants::MaxAcousticVoices; ++Idx)
    {
        uint8_t Expected = 0u;
        if (SlotClaimed[Idx].compare_exchange_strong(Expected, 1u, std::memory_order_acq_rel))
        {
            const uint32_t Gen = SlotGenerations[Idx].load(std::memory_order_acquire);
            ReservedToken      = { Idx + 1u, Gen };
            break;
        }
    }

    AcousticCommandEnvelope Command{};
    Command.Operation       = AcousticCommandOp::TriggerVoice;
    Command.VoiceToken      = ReservedToken;
    Command.VoiceDescriptor = Descriptor;
    Command.RandomSeed      = (RandomSeed != 0u) ? RandomSeed : NextAllocSeed.fetch_add(1u, std::memory_order_relaxed);

    if (!EnqueueCommand(Command))
    {
        if (ReservedToken)
        {
            SlotClaimed[ReservedToken.SlotIndex - 1u].store(0u, std::memory_order_release);
        }
        return {};
    }

    return ReservedToken;
}

bool AudioBusAccumulator::ReleaseVoice(AcousticVoiceToken Token) noexcept
{
    if (!Token)
    {
        return false;
    }
    AcousticCommandEnvelope Command{};
    Command.Operation  = AcousticCommandOp::ReleaseVoice;
    Command.VoiceToken = Token;
    return EnqueueCommand(Command);
}

bool AudioBusAccumulator::StopVoiceImmediate(AcousticVoiceToken Token) noexcept
{
    if (!Token)
    {
        return false;
    }
    AcousticCommandEnvelope Command{};
    Command.Operation  = AcousticCommandOp::StopVoiceImmediate;
    Command.VoiceToken = Token;
    return EnqueueCommand(Command);
}

bool AudioBusAccumulator::AssignVoiceTransform(
    AcousticVoiceToken  Token,
    Mathematics::Axis3D PositionMeters,
    Mathematics::Axis3D VelocityMps,
    float               OcclusionFactor) noexcept
{
    if (!Token)
    {
        return false;
    }
    AcousticCommandEnvelope Command{};
    Command.Operation      = AcousticCommandOp::AssignVoiceTransform;
    Command.VoiceToken     = Token;
    Command.PositionMeters = PositionMeters;
    Command.VelocityMps    = VelocityMps;
    Command.ScalarC        = OcclusionFactor;
    return EnqueueCommand(Command);
}

bool AudioBusAccumulator::AssignVoiceContinuousDrive(
    AcousticVoiceToken Token,
    float              GainMultiplier,
    float              PitchMultiplier,
    float              OcclusionFactor) noexcept
{
    if (!Token)
    {
        return false;
    }
    AcousticCommandEnvelope Command{};
    Command.Operation  = AcousticCommandOp::AssignVoiceContinuousDrive;
    Command.VoiceToken = Token;
    Command.ScalarA    = GainMultiplier;
    Command.ScalarB    = PitchMultiplier;
    Command.ScalarC    = OcclusionFactor;
    return EnqueueCommand(Command);
}

bool AudioBusAccumulator::AssignReceiverPose(const AcousticReceiverPose& Pose) noexcept
{
    AcousticCommandEnvelope Command{};
    Command.Operation    = AcousticCommandOp::AssignReceiverPose;
    Command.ReceiverPose = Pose;
    return EnqueueCommand(Command);
}

bool AudioBusAccumulator::AssignBusGain(AudioBusCategory Bus, float LinearGain) noexcept
{
    AcousticCommandEnvelope Command{};
    Command.Operation = AcousticCommandOp::AssignBusGain;
    Command.Bus       = Bus;
    Command.ScalarA   = std::clamp(LinearGain, 0.0f, 4.0f);
    return EnqueueCommand(Command);
}

bool AudioBusAccumulator::AssignBusMute(AudioBusCategory Bus, bool Muted) noexcept
{
    AcousticCommandEnvelope Command{};
    Command.Operation       = AcousticCommandOp::AssignBusMute;
    Command.Bus             = Bus;
    Command.ToggleCondition = Muted;
    return EnqueueCommand(Command);
}

bool AudioBusAccumulator::AssignEnvironmentPreset(AcousticEnvironmentPreset Preset) noexcept
{
    AcousticCommandEnvelope Command{};
    Command.Operation         = AcousticCommandOp::AssignEnvironmentPreset;
    Command.EnvironmentPreset = Preset;
    return EnqueueCommand(Command);
}

bool AudioBusAccumulator::AssignSpeakerLayout(SpatialSpeakerLayout Layout) noexcept
{
    AcousticCommandEnvelope Command{};
    Command.Operation     = AcousticCommandOp::AssignSpeakerLayout;
    Command.SpeakerLayout = Layout;
    return EnqueueCommand(Command);
}

//------------------------------------------------------------------------------------------------------------------------
//                                             REAL-TIME DSP VOICE & BUS MIXING
//------------------------------------------------------------------------------------------------------------------------

void AudioBusAccumulator::ReclaimVoiceSlot(uint32_t SlotIdx) noexcept
{
    VoiceSlots[SlotIdx].EnvelopePhase     = VoiceEnvelopePhase::Inactive;
    VoiceSlots[SlotIdx].Token             = {};
    VoiceSlots[SlotIdx].SpatialInitialized = false;
    const uint32_t NextGen = SlotGenerations[SlotIdx].fetch_add(1u, std::memory_order_acq_rel) + 1u;
    if (NextGen == 0u)
    {
        SlotGenerations[SlotIdx].store(1u, std::memory_order_release);
    }
    SlotClaimed[SlotIdx].store(0u, std::memory_order_release);
}

bool AudioBusAccumulator::ActivateVoiceInSlot(
    uint32_t                       SlotIdx,
    uint32_t                       ExpectedGeneration,
    const AcousticCommandEnvelope& Command) noexcept
{
    AcousticVoiceSlot& Slot = VoiceSlots[SlotIdx];
    Slot                    = {};
    Slot.Token              = { SlotIdx + 1u, ExpectedGeneration };
    Slot.Descriptor         = Command.VoiceDescriptor;
    Slot.EnvelopePhase      = (Command.VoiceDescriptor.AttackSeconds > 0.0005f)
                                  ? VoiceEnvelopePhase::Attack
                                  : VoiceEnvelopePhase::Sustain;
    Slot.EnvelopeGain       = (Slot.EnvelopePhase == VoiceEnvelopePhase::Attack) ? 0.0f : 1.0f;

    uint32_t RngSeed       = Command.RandomSeed;
    const uint32_t VarMax  = std::clamp(Slot.Descriptor.VariationCount, 1u, Constants::MaxVoiceVariationSlots);
    uint32_t ChosenVarIdx  = 0u;
    for (uint32_t Attempt = 0u; Attempt < VarMax; ++Attempt)
    {
        const uint32_t CandidateIdx = (StepRng(RngSeed) + Attempt) % VarMax;
        if (Slot.Descriptor.WaveformSlots[CandidateIdx])
        {
            ChosenVarIdx = CandidateIdx;
            break;
        }
    }
    Slot.SelectedWaveform = Slot.Descriptor.WaveformSlots[ChosenVarIdx];

    const float PitchJitter = NextSignedUnitFloat(RngSeed) * std::clamp(Slot.Descriptor.PitchJitterRange, 0.0f, 0.5f);
    const float GainJitter  = NextSignedUnitFloat(RngSeed) * std::clamp(Slot.Descriptor.GainJitterRange, 0.0f, 0.5f);
    Slot.JitterPitch        = std::max(0.25f, 1.0f + PitchJitter);
    Slot.JitterGain         = std::max(0.05f, 1.0f + GainJitter);
    Slot.SmoothedDopplerRatio = 1.0f;
    Slot.SpatialInitialized   = false;
    return true;
}

void AudioBusAccumulator::ApplySingleCommand(const AcousticCommandEnvelope& Command) noexcept
{
    switch (Command.Operation)
    {
        case AcousticCommandOp::TriggerVoice:
        {
            if (Command.VoiceToken)
            {
                const uint32_t Idx = Command.VoiceToken.SlotIndex - 1u;
                if (Idx < Constants::MaxAcousticVoices)
                {
                    (void)ActivateVoiceInSlot(Idx, Command.VoiceToken.Generation, Command);
                }
            }
            else
            {
                // Preallocated polyphony array full: attempt priority-based voice stealing
                uint32_t VictimIdx   = Constants::MaxAcousticVoices;
                uint32_t LowestScore = 0xFFFFFFFFu;
                for (uint32_t Idx = 0u; Idx < Constants::MaxAcousticVoices; ++Idx)
                {
                    if (VoiceSlots[Idx].EnvelopePhase == VoiceEnvelopePhase::Inactive)
                    {
                        VictimIdx = Idx;
                        break;
                    }
                    const uint32_t AudibilityScore = static_cast<uint32_t>(std::clamp(VoiceSlots[Idx].LastAudibility, 0.0f, 1.0f) * 255.0f);
                    const uint32_t CompositeScore  = (static_cast<uint32_t>(VoiceSlots[Idx].Descriptor.Priority) << 16u) |
                                                     ((VoiceSlots[Idx].EnvelopePhase == VoiceEnvelopePhase::Release ? 0u : 1u) << 8u) |
                                                     AudibilityScore;
                    if (CompositeScore < LowestScore)
                    {
                        LowestScore = CompositeScore;
                        VictimIdx   = Idx;
                    }
                }

                if (VictimIdx < Constants::MaxAcousticVoices)
                {
                    const bool PreviouslyOccupied = (VoiceSlots[VictimIdx].EnvelopePhase != VoiceEnvelopePhase::Inactive);
                    if (!PreviouslyOccupied || Command.VoiceDescriptor.Priority >= VoiceSlots[VictimIdx].Descriptor.Priority)
                    {
                        if (PreviouslyOccupied)
                        {
                            TelemetryStolenVoices.fetch_add(1u, std::memory_order_relaxed);
                            ReclaimVoiceSlot(VictimIdx);
                        }
                        SlotClaimed[VictimIdx].store(1u, std::memory_order_release);
                        const uint32_t Gen = SlotGenerations[VictimIdx].load(std::memory_order_acquire);
                        (void)ActivateVoiceInSlot(VictimIdx, Gen, Command);
                    }
                }
            }
            break;
        }
        case AcousticCommandOp::ReleaseVoice:
        {
            if (Command.VoiceToken && Command.VoiceToken.SlotIndex <= Constants::MaxAcousticVoices)
            {
                AcousticVoiceSlot& Slot = VoiceSlots[Command.VoiceToken.SlotIndex - 1u];
                if (Slot.EnvelopePhase != VoiceEnvelopePhase::Inactive && Slot.Token == Command.VoiceToken)
                {
                    Slot.EnvelopePhase = VoiceEnvelopePhase::Release;
                }
            }
            break;
        }
        case AcousticCommandOp::StopVoiceImmediate:
        {
            if (Command.VoiceToken && Command.VoiceToken.SlotIndex <= Constants::MaxAcousticVoices)
            {
                const uint32_t     Idx  = Command.VoiceToken.SlotIndex - 1u;
                AcousticVoiceSlot& Slot = VoiceSlots[Idx];
                if (Slot.EnvelopePhase != VoiceEnvelopePhase::Inactive && Slot.Token == Command.VoiceToken)
                {
                    ReclaimVoiceSlot(Idx);
                }
            }
            break;
        }
        case AcousticCommandOp::AssignVoiceTransform:
        {
            if (Command.VoiceToken && Command.VoiceToken.SlotIndex <= Constants::MaxAcousticVoices)
            {
                AcousticVoiceSlot& Slot = VoiceSlots[Command.VoiceToken.SlotIndex - 1u];
                if (Slot.EnvelopePhase != VoiceEnvelopePhase::Inactive && Slot.Token == Command.VoiceToken)
                {
                    Slot.Descriptor.PositionMeters  = Command.PositionMeters;
                    Slot.Descriptor.VelocityMps     = Command.VelocityMps;
                    Slot.Descriptor.OcclusionFactor = std::clamp(Command.ScalarC, 0.0f, 1.0f);
                }
            }
            break;
        }
        case AcousticCommandOp::AssignVoiceContinuousDrive:
        {
            if (Command.VoiceToken && Command.VoiceToken.SlotIndex <= Constants::MaxAcousticVoices)
            {
                AcousticVoiceSlot& Slot = VoiceSlots[Command.VoiceToken.SlotIndex - 1u];
                if (Slot.EnvelopePhase != VoiceEnvelopePhase::Inactive && Slot.Token == Command.VoiceToken)
                {
                    Slot.DriveGainMultiplier        = std::clamp(Command.ScalarA, 0.0f, 4.0f);
                    Slot.DrivePitchMultiplier       = std::clamp(Command.ScalarB, 0.25f, 4.0f);
                    Slot.Descriptor.OcclusionFactor = std::clamp(Command.ScalarC, 0.0f, 1.0f);
                }
            }
            break;
        }
        case AcousticCommandOp::AssignBusGain:
        {
            const uint32_t BusIdx = static_cast<uint32_t>(Command.Bus);
            if (BusIdx < Constants::MaxAudioBusCount)
            {
                BusControls[BusIdx].GainLinear = std::clamp(Command.ScalarA, 0.0f, 4.0f);
            }
            break;
        }
        case AcousticCommandOp::AssignBusMute:
        {
            const uint32_t BusIdx = static_cast<uint32_t>(Command.Bus);
            if (BusIdx < Constants::MaxAudioBusCount)
            {
                BusControls[BusIdx].Muted = Command.ToggleCondition;
            }
            break;
        }
        case AcousticCommandOp::AssignReceiverPose:
        {
            ActiveReceiver = Command.ReceiverPose;
            break;
        }
        case AcousticCommandOp::AssignEnvironmentPreset:
        {
            FallbackPreset = Command.EnvironmentPreset;
            break;
        }
        case AcousticCommandOp::AssignSpeakerLayout:
        {
            ActiveLayout = Command.SpeakerLayout;
            break;
        }
    }
}

void AudioBusAccumulator::DrainLockFreeCommands() noexcept
{
    AcousticCommandEnvelope Command{};
    while (CommandQueue.TryDequeue(Command))
    {
        ApplySingleCommand(Command);
    }
}

void AudioBusAccumulator::ResolveActiveVoicePriority(
    std::array<uint32_t, Constants::MaxAcousticVoices>& ActiveOrder,
    uint32_t&                                           OutTotalActiveCount,
    uint32_t&                                           OutMixedCount,
    uint32_t&                                           OutVirtualizedCount) noexcept
{
    OutTotalActiveCount = 0u;
    OutMixedCount       = 0u;
    OutVirtualizedCount = 0u;

    for (uint32_t Idx = 0u; Idx < Constants::MaxAcousticVoices; ++Idx)
    {
        if (VoiceSlots[Idx].EnvelopePhase != VoiceEnvelopePhase::Inactive)
        {
            ActiveOrder[OutTotalActiveCount++] = Idx;
        }
    }

    if (OutTotalActiveCount > Constants::MaxActiveMixedVoices)
    {
        std::sort(
            ActiveOrder.begin(),
            ActiveOrder.begin() + static_cast<std::ptrdiff_t>(OutTotalActiveCount),
            [&](uint32_t LeftIdx, uint32_t RightIdx) noexcept
            {
                const AcousticVoiceSlot& A = VoiceSlots[LeftIdx];
                const AcousticVoiceSlot& B = VoiceSlots[RightIdx];
                if (A.Descriptor.Priority != B.Descriptor.Priority)
                {
                    return A.Descriptor.Priority > B.Descriptor.Priority;
                }
                return A.LastAudibility > B.LastAudibility;
            });
        OutMixedCount       = Constants::MaxActiveMixedVoices;
        OutVirtualizedCount = OutTotalActiveCount - Constants::MaxActiveMixedVoices;
    }
    else
    {
        OutMixedCount       = OutTotalActiveCount;
        OutVirtualizedCount = 0u;
    }
}

void AudioBusAccumulator::ApplyTunnelWaveguideToBuffers(
    const AcousticSpatialSolution& DominantTunnelParams,
    uint32_t                       ChannelCount,
    uint32_t                       SamplePointCount) noexcept
{
    const float SampleRateF = static_cast<float>(SampleRateHz);
    const float SoundSpeedF = DominantTunnelParams.SubmergedActive
                                  ? Constants::WaterSoundSpeedMps
                                  : Constants::DryAirSoundSpeedMps;

    // 4 coprime transverse/oblique waveguide bounce modes driven by tunnel cross-section geometry
    const float InnerDiameterM   = 0.5f * (DominantTunnelParams.TunnelWidthMeters + DominantTunnelParams.TunnelHeightMeters);
    const std::array<float, Constants::TunnelCombTapCount> ModeMultipliers = { 1.00f, 1.49f, 2.23f, 3.17f };
    std::array<uint32_t, Constants::TunnelCombTapCount>    CombDelaySamples {};
    for (uint32_t Tap = 0u; Tap < Constants::TunnelCombTapCount; ++Tap)
    {
        const float DelaySeconds = (InnerDiameterM * ModeMultipliers[Tap]) / SoundSpeedF;
        CombDelaySamples[Tap]    = std::clamp(
            static_cast<uint32_t>(DelaySeconds * SampleRateF),
            64u,
            TunnelCombMemory::MaxCombLineSamples - 2u);
    }

    const float Reflectivity = std::clamp(DominantTunnelParams.TunnelWallReflectivity * 0.88f, 0.0f, 0.90f);
    const float LengthNorm   = std::clamp(DominantTunnelParams.TunnelLengthMeters / 40.0f, 0.4f, 2.5f);
    const float FdnFeedback  = std::clamp(0.42f + 0.34f * DominantTunnelParams.TunnelWallReflectivity * std::min(LengthNorm, 1.5f), 0.15f, 0.86f);

    constexpr std::array<uint32_t, Constants::ReverbFdnLineCount> TunnelFdnLengths = { 1187u, 1741u, 2393u, 3169u };

    for (uint32_t S = 0u; S < SamplePointCount; ++S)
    {
        const float WetIn = TunnelBusScratch[S];

        // Phase 1: Tube chest boom peaking EQ around 195 Hz
        const float Boomed = EvaluateBiquadPeaking(
            WetIn,
            195.0f,
            1.35f,
            4.5f,
            SampleRateF,
            TunnelBoomBiquad.X1,
            TunnelBoomBiquad.X2,
            TunnelBoomBiquad.Y1,
            TunnelBoomBiquad.Y2);

        // Phase 2: Parallel cylindrical waveguide combs with wall treble absorption
        float CombSum = 0.0f;
        for (uint32_t Tap = 0u; Tap < Constants::TunnelCombTapCount; ++Tap)
        {
            TunnelCombMemory& Comb       = TunnelCombLines[Tap];
            const uint32_t    ReadCursor = (Comb.WriteCursor + TunnelCombMemory::MaxCombLineSamples - CombDelaySamples[Tap]) %
                                           TunnelCombMemory::MaxCombLineSamples;
            const float Delayed = Comb.DelayLine[ReadCursor];
            const float Damped  = EvaluateOnePoleHighCut(
                Delayed,
                DominantTunnelParams.TunnelWallDampingHz,
                SampleRateF,
                Comb.DampingMemory);
            Comb.DelayLine[Comb.WriteCursor] = FlushDenormal(Boomed + Damped * Reflectivity);
            Comb.WriteCursor                 = (Comb.WriteCursor + 1u) % TunnelCombMemory::MaxCombLineSamples;
            CombSum                         += Delayed;
        }
        const float WaveguideBody = EvaluateOnePoleHighCut(
            CombSum * 0.28f,
            DominantTunnelParams.TunnelWallDampingHz * 1.15f,
            SampleRateF,
            TunnelPostDamping.PreviousY);

        // Phase 3: 4x4 Hadamard FDN longitudinal corridor decay
        std::array<float, Constants::ReverbFdnLineCount> Reads{};
        for (uint32_t Line = 0u; Line < Constants::ReverbFdnLineCount; ++Line)
        {
            const uint32_t Len = TunnelFdnLengths[Line];
            const uint32_t Rd  = (TunnelFdn.WriteCursors[Line] + HadamardFdnNetwork::MaxDelayLineSamples - Len) %
                                 HadamardFdnNetwork::MaxDelayLineSamples;
            Reads[Line] = EvaluateOnePoleHighCut(
                TunnelFdn.DelayLines[Line][Rd],
                DominantTunnelParams.TunnelWallDampingHz,
                SampleRateF,
                TunnelFdn.DampingMemory[Line]);
        }

        // Normalized 4x4 Hadamard transform: H4 * 0.5
        const float H0 = 0.5f * (Reads[0] + Reads[1] + Reads[2] + Reads[3]);
        const float H1 = 0.5f * (Reads[0] - Reads[1] + Reads[2] - Reads[3]);
        const float H2 = 0.5f * (Reads[0] + Reads[1] - Reads[2] - Reads[3]);
        const float H3 = 0.5f * (Reads[0] - Reads[1] - Reads[2] + Reads[3]);

        const float Inject = (WaveguideBody + Boomed * 0.35f) * 0.5f;
        TunnelFdn.DelayLines[0][TunnelFdn.WriteCursors[0]] = FlushDenormal(Inject + H0 * FdnFeedback);
        TunnelFdn.DelayLines[1][TunnelFdn.WriteCursors[1]] = FlushDenormal(Inject + H1 * FdnFeedback);
        TunnelFdn.DelayLines[2][TunnelFdn.WriteCursors[2]] = FlushDenormal(-Inject + H2 * FdnFeedback);
        TunnelFdn.DelayLines[3][TunnelFdn.WriteCursors[3]] = FlushDenormal(Inject + H3 * FdnFeedback);

        for (uint32_t Line = 0u; Line < Constants::ReverbFdnLineCount; ++Line)
        {
            TunnelFdn.WriteCursors[Line] = (TunnelFdn.WriteCursors[Line] + 1u) % HadamardFdnNetwork::MaxDelayLineSamples;
        }

        const float LeftTail  = WaveguideBody * 0.55f + (Reads[0] + Reads[2]) * 0.38f;
        const float RightTail = WaveguideBody * 0.55f + (Reads[1] + Reads[3]) * 0.38f;

        if (ChannelCount == 1u)
        {
            MasterMixScratch[S] += 0.5f * (LeftTail + RightTail);
        }
        else
        {
            const std::size_t DelayOffset = static_cast<std::size_t>(S) * ChannelCount;
            MasterMixScratch[DelayOffset + 0u] += LeftTail;
            MasterMixScratch[DelayOffset + 1u] += RightTail;
            if (ChannelCount >= 6u)
            {
                MasterMixScratch[DelayOffset + 2u] += 0.35f * (LeftTail + RightTail); // C subtle depth
                MasterMixScratch[DelayOffset + 4u] += Reads[2] * 0.45f;               // SL
                MasterMixScratch[DelayOffset + 5u] += Reads[3] * 0.45f;               // SR
                if (ChannelCount >= 8u)
                {
                    MasterMixScratch[DelayOffset + 6u] += Reads[0] * 0.40f;           // RL
                    MasterMixScratch[DelayOffset + 7u] += Reads[1] * 0.40f;           // RR
                }
            }
        }
    }
}

void AudioBusAccumulator::ApplyDiffuseReverbToBuffers(
    float    ReverbDecaySeconds,
    float    WallDampingHz,
    uint32_t ChannelCount,
    uint32_t SamplePointCount) noexcept
{
    const float SampleRateF = static_cast<float>(SampleRateHz);
    const float SafeDecay   = std::clamp(ReverbDecaySeconds, 0.25f, 8.0f);
    constexpr std::array<uint32_t, Constants::ReverbFdnLineCount> FdnDelaySamples = { 1487u, 2089u, 2791u, 3541u };
    const double LfoStep    = Constants::Tau * 0.45 / static_cast<double>(SampleRateHz);

    std::array<float, Constants::ReverbFdnLineCount> LineFeedback {};
    for (uint32_t Line = 0u; Line < Constants::ReverbFdnLineCount; ++Line)
    {
        const float DelaySec  = static_cast<float>(FdnDelaySamples[Line]) / SampleRateF;
        LineFeedback[Line]    = std::clamp(std::pow(10.0f, (-3.0f * DelaySec) / SafeDecay), 0.1f, 0.94f);
    }

    for (uint32_t S = 0u; S < SamplePointCount; ++S)
    {
        const float WetIn = ReverbBusScratch[S];
        AmbientFdn.LfoPhaseRadians += LfoStep;
        if (AmbientFdn.LfoPhaseRadians >= Constants::Tau)
        {
            AmbientFdn.LfoPhaseRadians -= Constants::Tau;
        }
        const int32_t LfoOffset = static_cast<int32_t>(std::round(5.0 * std::sin(AmbientFdn.LfoPhaseRadians)));

        std::array<float, Constants::ReverbFdnLineCount> Reads{};
        for (uint32_t Line = 0u; Line < Constants::ReverbFdnLineCount; ++Line)
        {
            const uint32_t ModLen = static_cast<uint32_t>(static_cast<int32_t>(FdnDelaySamples[Line]) + (Line == 0u ? LfoOffset : -LfoOffset));
            const uint32_t Rd     = (AmbientFdn.WriteCursors[Line] + HadamardFdnNetwork::MaxDelayLineSamples - ModLen) %
                                    HadamardFdnNetwork::MaxDelayLineSamples;
            Reads[Line] = EvaluateOnePoleHighCut(
                AmbientFdn.DelayLines[Line][Rd],
                WallDampingHz,
                SampleRateF,
                AmbientFdn.DampingMemory[Line]);
        }

        const float H0 = 0.5f * (Reads[0] + Reads[1] + Reads[2] + Reads[3]);
        const float H1 = 0.5f * (Reads[0] - Reads[1] + Reads[2] - Reads[3]);
        const float H2 = 0.5f * (Reads[0] + Reads[1] - Reads[2] - Reads[3]);
        const float H3 = 0.5f * (Reads[0] - Reads[1] - Reads[2] + Reads[3]);

        AmbientFdn.DelayLines[0][AmbientFdn.WriteCursors[0]] = FlushDenormal(WetIn * 0.45f + H0 * LineFeedback[0]);
        AmbientFdn.DelayLines[1][AmbientFdn.WriteCursors[1]] = FlushDenormal(WetIn * 0.45f + H1 * LineFeedback[1]);
        AmbientFdn.DelayLines[2][AmbientFdn.WriteCursors[2]] = FlushDenormal(-WetIn * 0.45f + H2 * LineFeedback[2]);
        AmbientFdn.DelayLines[3][AmbientFdn.WriteCursors[3]] = FlushDenormal(WetIn * 0.45f + H3 * LineFeedback[3]);

        for (uint32_t Line = 0u; Line < Constants::ReverbFdnLineCount; ++Line)
        {
            AmbientFdn.WriteCursors[Line] = (AmbientFdn.WriteCursors[Line] + 1u) % HadamardFdnNetwork::MaxDelayLineSamples;
        }

        const float LeftVerb  = (Reads[0] + Reads[2]) * 0.35f;
        const float RightVerb = (Reads[1] + Reads[3]) * 0.35f;
        if (ChannelCount == 1u)
        {
            MasterMixScratch[S] += 0.5f * (LeftVerb + RightVerb);
        }
        else
        {
            const std::size_t DelayOffset = static_cast<std::size_t>(S) * ChannelCount;
            MasterMixScratch[DelayOffset + 0u] += LeftVerb;
            MasterMixScratch[DelayOffset + 1u] += RightVerb;
            if (ChannelCount >= 6u)
            {
                MasterMixScratch[DelayOffset + 4u] += Reads[2] * 0.32f;
                MasterMixScratch[DelayOffset + 5u] += Reads[3] * 0.32f;
                if (ChannelCount >= 8u)
                {
                    MasterMixScratch[DelayOffset + 6u] += Reads[0] * 0.28f;
                    MasterMixScratch[DelayOffset + 7u] += Reads[1] * 0.28f;
                }
            }
        }
    }
}

void AudioBusAccumulator::AccumulateMixBlock(
    float*   OutInterleavedSamples,
    uint32_t SamplePointCount) noexcept
{
    if (OutInterleavedSamples == nullptr || SamplePointCount == 0u)
    {
        return;
    }

    uint32_t RemainingPoints = SamplePointCount;
    uint32_t OutputOffset    = 0u;

    while (RemainingPoints > 0u)
    {
        const uint32_t ChunkCount   = std::min(RemainingPoints, ConfiguredBlockMax);
        const auto     ClockStart   = std::chrono::steady_clock::now();
        const uint32_t ChannelCount = ResolveChannelCount(ActiveLayout);
        const float    SampleRateF  = static_cast<float>(SampleRateHz);
        const float    BlockSec     = static_cast<float>(ChunkCount) / SampleRateF;

        DrainLockFreeCommands();

        const uint32_t                 ZoneSlotIdx = PublishedZoneSlot.load(std::memory_order_acquire);
        const ZoneTripleBufferSlot&    ZoneSlice   = ZoneBuffers[ZoneSlotIdx];
        const std::span<const AcousticZoneDescriptor> ActiveZones(ZoneSlice.Zones.data(), ZoneSlice.ActiveCount);

        std::array<uint32_t, Constants::MaxAcousticVoices> ActiveOrder {};
        uint32_t TotalActiveCount = 0u;
        uint32_t MixedCount       = 0u;
        uint32_t VirtualCount     = 0u;
        ResolveActiveVoicePriority(ActiveOrder, TotalActiveCount, MixedCount, VirtualCount);

        const std::size_t ChunkInterleavedFloats = static_cast<std::size_t>(ChunkCount) * ChannelCount;
        std::fill_n(TunnelBusScratch.data(), ChunkCount, 0.0f);
        std::fill_n(ReverbBusScratch.data(), ChunkCount, 0.0f);
        std::fill_n(MasterMixScratch.data(), ChunkInterleavedFloats, 0.0f);
        for (std::vector<float>& BusBuffer : SubmixBusScratch)
        {
            std::fill_n(BusBuffer.data(), ChunkInterleavedFloats, 0.0f);
        }

        float                   MaxTunnelStrength      = 0.0f;
        float                   StrongestDopplerShift  = 1.0f;
        float                   StrongestDopplerDeviat = 0.0f;
        AcousticSpatialSolution DominantTunnelSolution {};
        DominantTunnelSolution.TunnelWidthMeters       = 6.0f;
        DominantTunnelSolution.TunnelHeightMeters      = 4.5f;
        DominantTunnelSolution.TunnelLengthMeters      = 35.0f;
        DominantTunnelSolution.TunnelWallReflectivity  = 0.84f;
        DominantTunnelSolution.TunnelWallDampingHz     = 3200.0f;
        DominantTunnelSolution.SubmergedActive         = ActiveReceiver.SubmergedCondition || (FallbackPreset == AcousticEnvironmentPreset::Underwater);

        for (uint32_t OrderIdx = 0u; OrderIdx < TotalActiveCount; ++OrderIdx)
        {
            const uint32_t     SlotIdx = ActiveOrder[OrderIdx];
            AcousticVoiceSlot& Slot    = VoiceSlots[SlotIdx];

            const WaveformBuffer* Buffer = (WaveformRepo != nullptr)
                                               ? WaveformRepo->ResolveResidentWaveform(Slot.SelectedWaveform)
                                               : nullptr;
            if (Buffer == nullptr || Buffer->SamplePointCount == 0u)
            {
                ReclaimVoiceSlot(SlotIdx);
                continue;
            }

            const AcousticSpatialSolution Spatial = SpatialAcoustics::SolveSpatialAcoustics(
                ActiveReceiver,
                Slot.Descriptor,
                ActiveLayout,
                FallbackPreset,
                ActiveZones);
            Slot.LastAudibility = Spatial.EffectiveAudibility * Slot.DriveGainMultiplier;

            if (Spatial.TunnelMix > MaxTunnelStrength)
            {
                MaxTunnelStrength      = Spatial.TunnelMix;
                DominantTunnelSolution = Spatial;
            }

            const float DopplerDeviat = std::abs(Spatial.DopplerPitchTarget - 1.0f);
            if (DopplerDeviat > StrongestDopplerDeviat)
            {
                StrongestDopplerDeviat = DopplerDeviat;
                StrongestDopplerShift  = Spatial.DopplerPitchTarget;
            }

            const bool LoopingEnabled = (Slot.Descriptor.PlaybackCycle == PlaybackCycleCategory::ContinuousLoop ||
                                         Slot.Descriptor.PlaybackCycle == PlaybackCycleCategory::DrivenContinuous);
            const bool WrapLoop       = LoopingEnabled;
            const double RateRatio      = static_cast<double>(Buffer->SampleRateHz) / static_cast<double>(SampleRateHz);

            // Virtualized voices silently track phase with zero DSP work
            if (OrderIdx >= Constants::MaxActiveMixedVoices && Slot.Descriptor.VirtualizeWhenQuiet)
            {
                Slot.SmoothedDopplerRatio = Spatial.DopplerPitchTarget;
                const double Step = static_cast<double>(Slot.Descriptor.NominalPitch * Slot.JitterPitch *
                                                        Slot.DrivePitchMultiplier * Slot.SmoothedDopplerRatio) * RateRatio;
                Slot.PlayheadSamplePoint += Step * static_cast<double>(ChunkCount);

                const uint32_t LoopStart = Buffer->LoopEnabled ? Buffer->LoopStartSamplePoint : 0u;
                const uint32_t LoopEnd   = (Buffer->LoopEnabled && Buffer->LoopEndSamplePoint > LoopStart)
                                               ? std::min(Buffer->LoopEndSamplePoint, Buffer->SamplePointCount)
                                               : Buffer->SamplePointCount;
                const double   Span      = static_cast<double>(std::max(1u, LoopEnd - LoopStart));
                if (WrapLoop)
                {
                    if (Slot.PlayheadSamplePoint >= static_cast<double>(LoopEnd))
                    {
                        Slot.PlayheadSamplePoint = static_cast<double>(LoopStart) +
                                                   std::fmod(Slot.PlayheadSamplePoint - static_cast<double>(LoopStart), Span);
                    }
                }
                else if (Slot.PlayheadSamplePoint >= static_cast<double>(Buffer->SamplePointCount))
                {
                    ReclaimVoiceSlot(SlotIdx);
                }
                continue;
            }

            // First block snaps smoothed spatial gains directly to avoid artificial fade-in from silence
            if (!Slot.SpatialInitialized)
            {
                Slot.SmoothedDopplerRatio = Spatial.DopplerPitchTarget;
                Slot.SmoothedSpeakerGains = Spatial.SpeakerGains;
                Slot.SmoothedLfeGain      = Spatial.LfeGain;
                Slot.SmoothedDistanceGain = Spatial.DistanceGain;
                Slot.SmoothedTunnelMix    = Spatial.TunnelMix;
                Slot.SmoothedReverbSend   = Spatial.ReverbSend;
                Slot.SpatialInitialized   = true;
            }

            const float TargetDoppler = Spatial.DopplerPitchTarget;
            const float NextDoppler   = SpatialAcoustics::SmoothDopplerPitchRatio(
                Slot.SmoothedDopplerRatio,
                TargetDoppler,
                BlockSec);

            const float DopplerStepPerSample   = (NextDoppler - Slot.SmoothedDopplerRatio) / static_cast<float>(ChunkCount);
            const float DistanceStepPerSample  = (Spatial.DistanceGain - Slot.SmoothedDistanceGain) / static_cast<float>(ChunkCount);
            const float TunnelMixStepPerSample = (Spatial.TunnelMix - Slot.SmoothedTunnelMix) / static_cast<float>(ChunkCount);
            const float ReverbStepPerSample    = (Spatial.ReverbSend - Slot.SmoothedReverbSend) / static_cast<float>(ChunkCount);
            const float LfeStepPerSample       = (Spatial.LfeGain - Slot.SmoothedLfeGain) / static_cast<float>(ChunkCount);

            std::array<float, Constants::MaxAcousticOutputChannels> GainSteps {};
            for (uint32_t Ch = 0u; Ch < ChannelCount; ++Ch)
            {
                GainSteps[Ch] = (Spatial.SpeakerGains[Ch] - Slot.SmoothedSpeakerGains[Ch]) / static_cast<float>(ChunkCount);
            }

            const float AttackInc  = 1.0f / std::max(1.0f, Slot.Descriptor.AttackSeconds * SampleRateF);
            const float ReleaseDec = 1.0f / std::max(1.0f, Slot.Descriptor.ReleaseSeconds * SampleRateF);

            const float CombinedCutoffHz = std::min(Spatial.OcclusionHighCutHz, Spatial.AirAbsorptionHighCutHz);
            const float ItdOffsetSamples = (ActiveLayout == SpatialSpeakerLayout::BinauralHrtf)
                                               ? SpatialAcoustics::EvaluateBinauralItdSamplePoints(
                                                     Spatial.BinauralAzimuthRadians,
                                                     Spatial.BinauralElevationRadians,
                                                     SampleRateHz)
                                               : 0.0f;
            const float LeftHeadCutoffHz  = SpatialAcoustics::EvaluateBinauralHeadShadowCutoffHz(Spatial.BinauralAzimuthRadians, false);
            const float RightHeadCutoffHz = SpatialAcoustics::EvaluateBinauralHeadShadowCutoffHz(Spatial.BinauralAzimuthRadians, true);

            const uint32_t TargetBusIdx = std::min<uint32_t>(static_cast<uint32_t>(Slot.Descriptor.Bus), Constants::MaxAudioBusCount - 1u);
            std::vector<float>& BusBuffer = SubmixBusScratch[TargetBusIdx];

            const uint32_t LoopStart = Buffer->LoopEnabled ? Buffer->LoopStartSamplePoint : 0u;
            const uint32_t LoopEnd   = (Buffer->LoopEnabled && Buffer->LoopEndSamplePoint > LoopStart)
                                           ? std::min(Buffer->LoopEndSamplePoint, Buffer->SamplePointCount)
                                           : Buffer->SamplePointCount;
            const double   LoopSpan  = static_cast<double>(std::max(1u, LoopEnd - LoopStart));

            bool ReclaimedMidBlock = false;
            for (uint32_t S = 0u; S < ChunkCount; ++S)
            {
                if (Slot.EnvelopePhase == VoiceEnvelopePhase::Attack)
                {
                    Slot.EnvelopeGain = std::min(1.0f, Slot.EnvelopeGain + AttackInc);
                    if (Slot.EnvelopeGain >= 1.0f)
                    {
                        Slot.EnvelopePhase = VoiceEnvelopePhase::Sustain;
                    }
                }
                else if (Slot.EnvelopePhase == VoiceEnvelopePhase::Release)
                {
                    Slot.EnvelopeGain = std::max(0.0f, Slot.EnvelopeGain - ReleaseDec);
                    if (Slot.EnvelopeGain <= 1.0e-5f)
                    {
                        ReclaimedMidBlock = true;
                        break;
                    }
                }

                if (!WrapLoop && Slot.PlayheadSamplePoint >= static_cast<double>(Buffer->SamplePointCount - 1u))
                {
                    ReclaimedMidBlock = true;
                    break;
                }

                const float RawMono = SampleMonoWithLoopCrossfade(
                    *Buffer,
                    Slot.PlayheadSamplePoint,
                    WrapLoop,
                    Slot.Descriptor.LoopCrossfadeSamplePoints);

                const float NominalSample = RawMono * Slot.EnvelopeGain * Slot.Descriptor.NominalGain *
                                            Slot.JitterGain * Slot.DriveGainMultiplier;

                Slot.SmoothedDopplerRatio += DopplerStepPerSample;
                Slot.SmoothedDistanceGain += DistanceStepPerSample;
                Slot.SmoothedTunnelMix    += TunnelMixStepPerSample;
                Slot.SmoothedReverbSend   += ReverbStepPerSample;
                Slot.SmoothedLfeGain      += LfeStepPerSample;

                // Direct path receives full occlusion + air absorption high-cut filtering
                const float AirFiltered = EvaluateOnePoleHighCut(
                    NominalSample,
                    Spatial.AirAbsorptionHighCutHz,
                    SampleRateF,
                    Slot.AirAbsorptionLpf.PreviousY);
                const float DirectFiltered = EvaluateOnePoleHighCut(
                    AirFiltered,
                    CombinedCutoffHz,
                    SampleRateF,
                    Slot.OcclusionFilterLpf.PreviousY);

                const float AttenuatedDirect = DirectFiltered * Slot.SmoothedDistanceGain;

                // Tunnel & diffuse reverb feeds preserve spatial distance drop while bypassing tight barrier shadowing
                const float EnvSendDry = AirFiltered * Slot.SmoothedDistanceGain;
                TunnelBusScratch[S]   += EnvSendDry * Slot.SmoothedTunnelMix;
                ReverbBusScratch[S]   += EnvSendDry * Slot.SmoothedReverbSend;

                if (ActiveLayout == SpatialSpeakerLayout::BinauralHrtf)
                {
                    Slot.ItdDelayLine[Slot.ItdWriteIndex] = AttenuatedDirect;

                    // Positive ITD = sound on left => delay right ear; Negative ITD = sound on right => delay left ear
                    const float LeftLagSamples  = (ItdOffsetSamples < 0.0f) ? std::min(-ItdOffsetSamples, 96.0f) : 0.0f;
                    const float RightLagSamples = (ItdOffsetSamples > 0.0f) ? std::min(ItdOffsetSamples, 96.0f) : 0.0f;

                    auto ReadFractionalItd = [&](float LagSamples) noexcept -> float
                    {
                        const uint32_t I0   = static_cast<uint32_t>(std::floor(LagSamples));
                        const uint32_t I1   = I0 + 1u;
                        const float    Frac = LagSamples - static_cast<float>(I0);
                        const uint32_t P0   = (Slot.ItdWriteIndex + Constants::MaxBinauralDelaySamplePoints - I0) % Constants::MaxBinauralDelaySamplePoints;
                        const uint32_t P1   = (Slot.ItdWriteIndex + Constants::MaxBinauralDelaySamplePoints - I1) % Constants::MaxBinauralDelaySamplePoints;
                        return Slot.ItdDelayLine[P0] * (1.0f - Frac) + Slot.ItdDelayLine[P1] * Frac;
                    };

                    const float LeftEarRaw  = ReadFractionalItd(LeftLagSamples);
                    const float RightEarRaw = ReadFractionalItd(RightLagSamples);
                    Slot.ItdWriteIndex      = (Slot.ItdWriteIndex + 1u) % Constants::MaxBinauralDelaySamplePoints;

                    const float LeftEar  = EvaluateOnePoleHighCut(LeftEarRaw, LeftHeadCutoffHz, SampleRateF, Slot.BinauralLeftShadow.PreviousY);
                    const float RightEar = EvaluateOnePoleHighCut(RightEarRaw, RightHeadCutoffHz, SampleRateF, Slot.BinauralRightShadow.PreviousY);

                    Slot.SmoothedSpeakerGains[0] += GainSteps[0];
                    Slot.SmoothedSpeakerGains[1] += GainSteps[1];
                    const std::size_t DelayOffset        = static_cast<std::size_t>(S) * 2u;
                    BusBuffer[DelayOffset + 0u]         += LeftEar * Slot.SmoothedSpeakerGains[0];
                    BusBuffer[DelayOffset + 1u]         += RightEar * Slot.SmoothedSpeakerGains[1];
                }
                else
                {
                    const std::size_t DelayOffset = static_cast<std::size_t>(S) * ChannelCount;
                    for (uint32_t Ch = 0u; Ch < ChannelCount; ++Ch)
                    {
                        Slot.SmoothedSpeakerGains[Ch] += GainSteps[Ch];
                        const float Gain = (Ch == 3u && ChannelCount >= 6u) ? Slot.SmoothedLfeGain : Slot.SmoothedSpeakerGains[Ch];
                        BusBuffer[DelayOffset + Ch] += AttenuatedDirect * Gain;
                    }
                }

                const double PhaseIncrement = static_cast<double>(
                    Slot.Descriptor.NominalPitch * Slot.JitterPitch * Slot.DrivePitchMultiplier * Slot.SmoothedDopplerRatio) * RateRatio;
                Slot.PlayheadSamplePoint += PhaseIncrement;

                if (WrapLoop && Slot.PlayheadSamplePoint >= static_cast<double>(LoopEnd))
                {
                    const uint32_t SafeXfade = (Slot.Descriptor.LoopCrossfadeSamplePoints > 0u && LoopSpan >= 8u)
                                                   ? std::min<uint32_t>(Slot.Descriptor.LoopCrossfadeSamplePoints, static_cast<uint32_t>(LoopSpan) / 4u)
                                                   : 0u;
                    const double WrapAnchor  = static_cast<double>(LoopStart + SafeXfade);
                    const double WrapSpan    = std::max(1.0, static_cast<double>(LoopEnd) - WrapAnchor);
                    Slot.PlayheadSamplePoint = WrapAnchor + std::fmod(Slot.PlayheadSamplePoint - static_cast<double>(LoopEnd), WrapSpan);
                }
            }

            if (ReclaimedMidBlock)
            {
                ReclaimVoiceSlot(SlotIdx);
            }
        }

        // Apply tunnel waveguide & ambient FDN reverberation (retaining decay tail after tunnel cue stops)
        if (MaxTunnelStrength > 0.005f)
        {
            RetainedTunnelParams    = DominantTunnelSolution;
            TunnelTailActiveSeconds = 2.5f;
            ApplyTunnelWaveguideToBuffers(DominantTunnelSolution, ChannelCount, ChunkCount);
        }
        else if (TunnelTailActiveSeconds > 0.0f)
        {
            TunnelTailActiveSeconds = std::max(0.0f, TunnelTailActiveSeconds - BlockSec);
            ApplyTunnelWaveguideToBuffers(RetainedTunnelParams, ChannelCount, ChunkCount);
        }

        const float AmbientDecay = (FallbackPreset == AcousticEnvironmentPreset::Cavern) ? 3.2f :
                                   (FallbackPreset == AcousticEnvironmentPreset::ConcreteHangar) ? 1.8f : 0.85f;
        const float AmbientDamp  = (FallbackPreset == AcousticEnvironmentPreset::Cavern) ? 2600.0f : 5200.0f;
        ApplyDiffuseReverbToBuffers(AmbientDecay, AmbientDamp, ChannelCount, ChunkCount);

        // Side-chain envelope detection from Weapons + Dialogue buses to duck Ambience smoothly
        float DuckingEnergy = 0.0f;
        const std::size_t WeaponsIdx  = static_cast<std::size_t>(AudioBusCategory::Weapons);
        const std::size_t DialogueIdx = static_cast<std::size_t>(AudioBusCategory::Dialogue);
        for (std::size_t Idx = 0u; Idx < ChunkInterleavedFloats; ++Idx)
        {
            const float SideSignal = std::abs(SubmixBusScratch[WeaponsIdx][Idx]) * 0.7f +
                                     std::abs(SubmixBusScratch[DialogueIdx][Idx]);
            DuckingEnergy = std::max(DuckingEnergy, SideSignal);
        }
        const float DuckCoeff = (DuckingEnergy > DuckingDetectorEnv)
                                    ? (1.0f - std::exp(-BlockSec / 0.012f))
                                    : (1.0f - std::exp(-BlockSec / 0.220f));
        DuckingDetectorEnv  += DuckCoeff * (DuckingEnergy - DuckingDetectorEnv);
        AmbienceDuckingGain  = std::clamp(1.0f - 0.50f * std::min(1.0f, DuckingDetectorEnv * 2.5f), 0.50f, 1.0f);

        // Sum submix buses into MasterMixScratch
        for (uint32_t BusIdx = 1u; BusIdx < Constants::MaxAudioBusCount; ++BusIdx)
        {
            if (BusControls[BusIdx].Muted || BusControls[BusIdx].GainLinear <= 0.0f)
            {
                continue;
            }
            const float EffectiveBusGain = BusControls[BusIdx].GainLinear *
                                           ((BusIdx == static_cast<uint32_t>(AudioBusCategory::Ambience)) ? AmbienceDuckingGain : 1.0f);
            const std::vector<float>& BusBuffer = SubmixBusScratch[BusIdx];
            for (std::size_t Idx = 0u; Idx < ChunkInterleavedFloats; ++Idx)
            {
                MasterMixScratch[Idx] += BusBuffer[Idx] * EffectiveBusGain;
            }
        }

        // Apply Underwater 450 Hz 2-pole LPF if submerged
        const bool ApplyUnderwaterLpf = ActiveReceiver.SubmergedCondition || (FallbackPreset == AcousticEnvironmentPreset::Underwater);
        if (ApplyUnderwaterLpf)
        {
            for (uint32_t S = 0u; S < ChunkCount; ++S)
            {
                const std::size_t DelayOffset = static_cast<std::size_t>(S) * ChannelCount;
                for (uint32_t Ch = 0u; Ch < ChannelCount; ++Ch)
                {
                    BiquadMemory& Mem = UnderwaterFilterLpf[Ch];
                    MasterMixScratch[DelayOffset + Ch] = EvaluateBiquadHighCut(
                        MasterMixScratch[DelayOffset + Ch],
                        450.0f,
                        SampleRateF,
                        Mem.X1,
                        Mem.X2,
                        Mem.Y1,
                        Mem.Y2);
                }
            }
        }

        // Master bus gain + soft-knee limiter -> write final samples
        const float MasterGain = BusControls[0].Muted ? 0.0f : BusControls[0].GainLinear;
        float       BlockPeak  = 0.0f;
        double      BlockSqSum = 0.0;

        float* DestPtr = OutInterleavedSamples + static_cast<std::size_t>(OutputOffset) * ChannelCount;
        for (std::size_t Idx = 0u; Idx < ChunkInterleavedFloats; ++Idx)
        {
            const float Scaled  = MasterMixScratch[Idx] * MasterGain;
            const float Limited = std::isfinite(Scaled) ? SoftLimitSample(Scaled) : 0.0f;
            DestPtr[Idx]        = Limited;
            const float Mag     = std::abs(Limited);
            BlockPeak           = std::max(BlockPeak, Mag);
            BlockSqSum         += static_cast<double>(Limited) * static_cast<double>(Limited);
        }

        const float BlockRms = (ChunkInterleavedFloats > 0u)
                                   ? static_cast<float>(std::sqrt(BlockSqSum / static_cast<double>(ChunkInterleavedFloats)))
                                   : 0.0f;

        const auto ElapsedUs = std::chrono::duration<float, std::micro>(std::chrono::steady_clock::now() - ClockStart).count();

        TelemetryMixedBlocks.fetch_add(1ull, std::memory_order_relaxed);
        TelemetryRenderedPoints.fetch_add(ChunkCount, std::memory_order_relaxed);
        TelemetryActiveMixedVoices.store(MixedCount, std::memory_order_relaxed);
        TelemetryVirtualizedVoices.store(VirtualCount, std::memory_order_relaxed);
        TelemetryPeakOutput.store(BlockPeak, std::memory_order_relaxed);
        TelemetryRmsOutput.store(BlockRms, std::memory_order_relaxed);
        TelemetryDominantTunnel.store(MaxTunnelStrength, std::memory_order_relaxed);
        TelemetryActiveDoppler.store(StrongestDopplerShift, std::memory_order_relaxed);
        TelemetryCpuMixMicros.store(ElapsedUs, std::memory_order_relaxed);

        RemainingPoints -= ChunkCount;
        OutputOffset    += ChunkCount;
    }
}

uint32_t AudioBusAccumulator::QuerySampleRateHz() const noexcept
{
    return SampleRateHz;
}

SpatialSpeakerLayout AudioBusAccumulator::QuerySpeakerLayout() const noexcept
{
    return ActiveLayout;
}

AcousticTelemetryMetrics AudioBusAccumulator::InspectTelemetryMetrics() const noexcept
{
    AcousticTelemetryMetrics Metrics{};
    Metrics.MixedSampleBlocks       = TelemetryMixedBlocks.load(std::memory_order_acquire);
    Metrics.RenderedSamplePoints    = TelemetryRenderedPoints.load(std::memory_order_acquire);
    Metrics.ActiveMixedVoices       = TelemetryActiveMixedVoices.load(std::memory_order_acquire);
    Metrics.VirtualizedVoices       = TelemetryVirtualizedVoices.load(std::memory_order_acquire);
    Metrics.StolenVoices            = TelemetryStolenVoices.load(std::memory_order_acquire);
    Metrics.DroppedCommandCount     = TelemetryDroppedCommands.load(std::memory_order_acquire);
    Metrics.PeakOutputLinear        = TelemetryPeakOutput.load(std::memory_order_acquire);
    Metrics.RmsOutputLinear         = TelemetryRmsOutput.load(std::memory_order_acquire);
    Metrics.DominantTunnelMix       = TelemetryDominantTunnel.load(std::memory_order_acquire);
    Metrics.ActiveDopplerShiftRatio = TelemetryActiveDoppler.load(std::memory_order_acquire);
    Metrics.CpuMixMicros            = TelemetryCpuMixMicros.load(std::memory_order_acquire);
    return Metrics;
}

} // namespace Frontier::AudioSpace
