/*============================================================================================================================================
                                                SIMULATIONACOUSTICBRIDGE.CPP
============================================================================================================================================*/

#include "Engine/SimulationSpace/SimulationAcousticBridge.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace
{

void SimulationAcousticBridge::Reset() noexcept
{
    TrackedInstanceIds_.fill(0u);
    PrevSeveredCounts_.fill(0u);
    TrackedCount_      = 0u;
    PendingBlast_      = false;
    PendingBlastPos_   = { 0.0f, 0.0f, 0.0f };
    PendingBlastSpeed_ = 0.0f;
}

void SimulationAcousticBridge::NotifyRadialBlast(
    const std::array<float, 3>& worldPosMeters,
    float peakSpeedMps) noexcept
{
    PendingBlast_      = true;
    PendingBlastPos_   = worldPosMeters;
    PendingBlastSpeed_ = std::max(0.0f, peakSpeedMps);
}

float SimulationAcousticBridge::ComputeDistanceAttenuation(
    const std::array<float, 3>& sourcePos,
    const std::array<float, 3>& listenerPos,
    float referenceDistanceMeters) noexcept
{
    const float dx = sourcePos[0] - listenerPos[0];
    const float dy = sourcePos[1] - listenerPos[1];
    const float dz = sourcePos[2] - listenerPos[2];
    const float distSq = dx * dx + dy * dy + dz * dz;
    const float refSq  = std::max(0.25f, referenceDistanceMeters * referenceDistanceMeters);
    return refSq / (refSq + distSq);
}

void SimulationAcousticBridge::PushCue(SimulationAcousticPacket& packet, const SimulationAcousticCue& cue) noexcept
{
    if (packet.CueCount < SimulationAcousticPacket::kMaxCuesPerTick && cue.GainNormalized > 0.005f)
    {
        packet.Cues[packet.CueCount++] = cue;
    }
}

SimulationAcousticPacket SimulationAcousticBridge::ExtractPacket(
    const SimulationCoordinatorExtension& coordinator,
    const std::array<float, 3>& listenerWorldMeters) noexcept
{
    SimulationAcousticPacket packet {};
    packet.TickIndex           = coordinator.InspectTelemetry().CompletedTicks;
    packet.ListenerWorldMeters = listenerWorldMeters;

    // 1. Sample coupled Fluid + ForceField environment at listener world coordinates (0 allocs)
    const SimulationPointProbe probe = SimulationSpatialQuery::SampleWorldPoint(coordinator, listenerWorldMeters);
    const float combinedWind = probe.FluidSpeedMps + probe.ForceMagnitudeMps2 * 0.25f;
    packet.AmbientWindIntensity = std::clamp(combinedWind / 12.0f, 0.0f, 1.0f);
    packet.AmbientThermalRoar   = std::clamp(probe.FluidTemperature / 5.0f, 0.0f, 1.0f);

    if (packet.AmbientWindIntensity > 0.08f)
    {
        SimulationAcousticCue windCue {};
        windCue.Kind            = AcousticCueKind::WindFluidShear;
        windCue.WorldPosMeters  = listenerWorldMeters;
        windCue.GainNormalized  = packet.AmbientWindIntensity;
        windCue.PitchMultiplier = std::clamp(0.75f + packet.AmbientWindIntensity * 0.85f, 0.5f, 2.0f);
        windCue.HapticLowFreq   = packet.AmbientWindIntensity * 0.25f;
        windCue.HapticHighFreq  = packet.AmbientWindIntensity * 0.15f;
        PushCue(packet, windCue);
    }

    // 2. Emit pending radial blast shockwave cue (attenuated by distance to listener)
    if (PendingBlast_)
    {
        const float atten = ComputeDistanceAttenuation(PendingBlastPos_, listenerWorldMeters, 3.0f);
        const float energy = std::clamp(PendingBlastSpeed_ / 10.0f, 0.0f, 1.0f);
        SimulationAcousticCue blastCue {};
        blastCue.Kind            = AcousticCueKind::ShockwaveBlast;
        blastCue.WorldPosMeters  = PendingBlastPos_;
        blastCue.GainNormalized  = std::clamp(energy * atten, 0.0f, 1.0f);
        blastCue.PitchMultiplier = std::clamp(0.65f + (1.0f - energy) * 0.35f, 0.5f, 2.0f);
        blastCue.HapticLowFreq   = blastCue.GainNormalized;
        blastCue.HapticHighFreq  = std::clamp(blastCue.GainNormalized * 0.85f, 0.0f, 1.0f);
        PushCue(packet, blastCue);
        PendingBlast_ = false;
    }

    // 3. Scan world SoftBodies for tearing deltas & extreme strain snaps (0 allocs)
    const auto instances = coordinator.InspectDeformables().InspectWorldRegistry().EnumerateInstances();
    for (const auto& inst : instances)
    {
        if (!inst.Enabled) continue;

        uint32_t slot = TrackedCount_;
        for (uint32_t k = 0u; k < TrackedCount_; ++k)
        {
            if (TrackedInstanceIds_[k] == inst.InstanceId)
            {
                slot = k;
                break;
            }
        }

        uint32_t prevSevered = inst.Body.SeveredCount;
        if (slot < TrackedCount_)
        {
            prevSevered = PrevSeveredCounts_[slot];
            PrevSeveredCounts_[slot] = inst.Body.SeveredCount;
        }
        else if (TrackedCount_ < kMaxTrackedBodies)
        {
            TrackedInstanceIds_[TrackedCount_] = inst.InstanceId;
            PrevSeveredCounts_[TrackedCount_]  = inst.Body.SeveredCount;
            ++TrackedCount_;
        }

        if (inst.Body.SeveredCount > prevSevered)
        {
            const uint32_t deltaTears = inst.Body.SeveredCount - prevSevered;
            packet.NewTearEvents += deltaTears;
            const float atten = ComputeDistanceAttenuation(inst.WorldOriginMeters, listenerWorldMeters, 2.5f);
            SimulationAcousticCue tearCue {};
            tearCue.Kind             = AcousticCueKind::SoftBodyTear;
            tearCue.SourceInstanceId = inst.InstanceId;
            tearCue.WorldPosMeters   = inst.WorldOriginMeters;
            tearCue.GainNormalized   = std::clamp((0.45f + 0.15f * static_cast<float>(deltaTears)) * atten, 0.0f, 1.0f);
            tearCue.PitchMultiplier  = std::clamp(1.0f + 0.08f * static_cast<float>(deltaTears), 0.7f, 1.8f);
            tearCue.HapticLowFreq    = tearCue.GainNormalized * 0.35f;
            tearCue.HapticHighFreq   = tearCue.GainNormalized;
            PushCue(packet, tearCue);
        }

        float instPeakStrain = 0.0f;
        for (float s : inst.Body.PointStrain)
        {
            if (s > instPeakStrain) instPeakStrain = s;
        }
        if (instPeakStrain > packet.PeakSoftBodyStrain)
        {
            packet.PeakSoftBodyStrain = instPeakStrain;
        }

        if (instPeakStrain > 0.35f)
        {
            const float atten = ComputeDistanceAttenuation(inst.WorldOriginMeters, listenerWorldMeters, 2.0f);
            const float norm  = std::clamp((instPeakStrain - 0.35f) / 0.65f, 0.0f, 1.0f);
            SimulationAcousticCue snapCue {};
            snapCue.Kind             = AcousticCueKind::SoftBodySnap;
            snapCue.SourceInstanceId = inst.InstanceId;
            snapCue.WorldPosMeters   = inst.WorldOriginMeters;
            snapCue.GainNormalized   = norm * atten;
            snapCue.PitchMultiplier  = std::clamp(0.9f + norm * 0.6f, 0.5f, 2.0f);
            snapCue.HapticLowFreq    = snapCue.GainNormalized * 0.4f;
            snapCue.HapticHighFreq   = snapCue.GainNormalized * 0.75f;
            PushCue(packet, snapCue);
        }
    }

    return packet;
}

} // namespace Frontier::SimulationSpace
