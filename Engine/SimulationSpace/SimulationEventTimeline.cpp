/*============================================================================================================================================
                                                 SIMULATIONEVENTTIMELINE.CPP
============================================================================================================================================*/
// 🧩 Phase K01 Implementation of zero-allocation SimulationEventTimeline & FNV-1a binary persistence.

#include "Engine/SimulationSpace/SimulationEventTimeline.h"

#include <cstring>
#include <fstream>
#include <vector>

namespace Frontier::SimulationSpace
{

namespace
{

constexpr uint32_t kTimelineMagic   = 0x4C544D53u; // "SMTL"
constexpr uint32_t kTimelineVersion = 1u;

uint64_t Fnv1aMix(uint64_t hash, const void* data, std::size_t bytes) noexcept
{
    const auto* p = static_cast<const uint8_t*>(data);
    for (std::size_t i = 0u; i < bytes; ++i)
    {
        hash ^= static_cast<uint64_t>(p[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

struct SerializedTimelineRecord
{
    double   TriggerTimeSeconds = 0.0;
    uint8_t  ActionCode         = 0u;
    uint8_t  Pad[3]             { 0u, 0u, 0u };
    uint32_t TargetIndex        = 0u;
    float    WorldCentre[3]     { 0.0f, 0.0f, 0.0f };
    float    VectorParam[3]     { 0.0f, 0.0f, 0.0f };
    float    RadiusMeters       = 0.0f;
    float    Magnitude          = 0.0f;
    float    SecondaryParam     = 0.0f;
    uint32_t TailPad            = 0u;
};
static_assert(sizeof(SerializedTimelineRecord) == 56u, "SerializedTimelineRecord must have zero implicit padding");

struct TimelineFileHeader
{
    uint32_t Magic      = kTimelineMagic;
    uint32_t Version    = kTimelineVersion;
    uint32_t EventCount = 0u;
    uint32_t Reserved   = 0u;
    uint64_t Checksum   = 0ull;
};

} // namespace

void SimulationEventTimeline::Clear() noexcept
{
    EventCount_    = 0u;
    ExecutedCount_ = 0u;
}

bool SimulationEventTimeline::ScheduleEvent(const TimelineEventEntry& entry) noexcept
{
    if (EventCount_ >= kMaxTimelineEvents || entry.Action == TimelineActionKind::None)
    {
        return false;
    }

    // Insertion-sort in-place by TriggerTimeSeconds (stable for equal timestamps, 0 heap allocations)
    uint32_t insertAt = EventCount_;
    while (insertAt > 0u && Events_[insertAt - 1u].TriggerTimeSeconds > entry.TriggerTimeSeconds)
    {
        Events_[insertAt] = Events_[insertAt - 1u];
        --insertAt;
    }
    Events_[insertAt] = entry;
    Events_[insertAt].Executed = false;
    ++EventCount_;
    return true;
}

bool SimulationEventTimeline::RecordRadialBlast(
    double triggerTimeSeconds,
    const std::array<float, 3>& centreMeters,
    float radiusMeters,
    float peakSpeedMps,
    float fluidTempBoost) noexcept
{
    TimelineEventEntry e {};
    e.TriggerTimeSeconds = triggerTimeSeconds;
    e.Action             = TimelineActionKind::InjectRadialBlast;
    e.WorldCentreMeters  = centreMeters;
    e.RadiusMeters       = radiusMeters;
    e.Magnitude          = peakSpeedMps;
    e.SecondaryParam     = fluidTempBoost;
    return ScheduleEvent(e);
}

bool SimulationEventTimeline::RecordFluidStroke(
    double triggerTimeSeconds,
    const std::array<float, 3>& centreMeters,
    const std::array<float, 3>& velocityMps,
    float radiusMeters,
    float densityAmount,
    float temperatureAmount) noexcept
{
    TimelineEventEntry e {};
    e.TriggerTimeSeconds = triggerTimeSeconds;
    e.Action             = TimelineActionKind::InjectFluidStroke;
    e.WorldCentreMeters  = centreMeters;
    e.VectorParam        = velocityMps;
    e.RadiusMeters       = radiusMeters;
    e.Magnitude          = densityAmount;
    e.SecondaryParam     = temperatureAmount;
    return ScheduleEvent(e);
}

bool SimulationEventTimeline::RecordForceStrength(
    double triggerTimeSeconds,
    uint32_t forceEntryIndex,
    float newStrength) noexcept
{
    TimelineEventEntry e {};
    e.TriggerTimeSeconds = triggerTimeSeconds;
    e.Action             = TimelineActionKind::ScaleForceStrength;
    e.TargetIndex        = forceEntryIndex;
    e.Magnitude          = newStrength;
    return ScheduleEvent(e);
}

uint32_t SimulationEventTimeline::EvaluateUpToTime(
    double simTimeSeconds,
    SimulationCoordinatorExtension& coordinator) noexcept
{
    uint32_t fired = 0u;
    for (uint32_t i = 0u; i < EventCount_; ++i)
    {
        auto& e = Events_[i];
        if (e.Executed) continue;
        if (e.TriggerTimeSeconds > simTimeSeconds) break; // Chronologically sorted

        switch (e.Action)
        {
        case TimelineActionKind::InjectRadialBlast:
            (void)SimulationInteractionBridge::InjectRadialBlastImpulse(
                coordinator, e.WorldCentreMeters, e.RadiusMeters, e.Magnitude, e.SecondaryParam);
            break;
        case TimelineActionKind::InjectFluidStroke:
            (void)SimulationInteractionBridge::InjectFluidBrushStroke(
                coordinator, e.WorldCentreMeters, e.VectorParam, e.RadiusMeters, e.Magnitude, e.SecondaryParam);
            break;
        case TimelineActionKind::EnableSoftBody:
            if (auto* inst = coordinator.AccessDeformables().AccessWorldRegistry().FindMutable(e.TargetIndex))
            {
                inst->Enabled = true;
            }
            break;
        case TimelineActionKind::DisableSoftBody:
            if (auto* inst = coordinator.AccessDeformables().AccessWorldRegistry().FindMutable(e.TargetIndex))
            {
                inst->Enabled = false;
            }
            break;
        case TimelineActionKind::ScaleForceStrength:
        {
            const auto entries = coordinator.InspectForceFields().QueryEntries();
            if (e.TargetIndex < entries.size())
            {
                if (auto* mutableEntry = coordinator.AccessForceFields().FindForceEntry(entries[e.TargetIndex].Node.NodeId))
                {
                    mutableEntry->Node.Strength = e.Magnitude;
                }
            }
            break;
        }
        case TimelineActionKind::InvokeCallback:
            if (e.Callback != nullptr)
            {
                e.Callback(e, simTimeSeconds, e.UserContext);
            }
            break;
        default:
            break;
        }

        e.Executed = true;
        ++ExecutedCount_;
        ++fired;
    }
    return fired;
}

void SimulationEventTimeline::Rewind(double toTimeSeconds) noexcept
{
    ExecutedCount_ = 0u;
    for (uint32_t i = 0u; i < EventCount_; ++i)
    {
        if (Events_[i].TriggerTimeSeconds > toTimeSeconds)
        {
            Events_[i].Executed = false;
        }
        if (Events_[i].Executed)
        {
            ++ExecutedCount_;
        }
    }
}

uint64_t SimulationEventTimeline::ComputeScheduleHash() const noexcept
{
    uint64_t h = 14695981039346656037ull;
    h = Fnv1aMix(h, &EventCount_, sizeof(EventCount_));
    for (uint32_t i = 0u; i < EventCount_; ++i)
    {
        SerializedTimelineRecord rec {};
        rec.TriggerTimeSeconds = Events_[i].TriggerTimeSeconds;
        rec.ActionCode         = static_cast<uint8_t>(Events_[i].Action);
        rec.TargetIndex        = Events_[i].TargetIndex;
        rec.WorldCentre[0]     = Events_[i].WorldCentreMeters[0];
        rec.WorldCentre[1]     = Events_[i].WorldCentreMeters[1];
        rec.WorldCentre[2]     = Events_[i].WorldCentreMeters[2];
        rec.VectorParam[0]     = Events_[i].VectorParam[0];
        rec.VectorParam[1]     = Events_[i].VectorParam[1];
        rec.VectorParam[2]     = Events_[i].VectorParam[2];
        rec.RadiusMeters       = Events_[i].RadiusMeters;
        rec.Magnitude          = Events_[i].Magnitude;
        rec.SecondaryParam     = Events_[i].SecondaryParam;
        h = Fnv1aMix(h, &rec, sizeof(rec));
    }
    return h;
}

bool SimulationEventTimeline::SaveToFile(const std::filesystem::path& path, std::string* outError) const
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        if (outError) *outError = "Unable to open timeline path for write.";
        return false;
    }

    TimelineFileHeader hdr {};
    hdr.EventCount = EventCount_;
    hdr.Checksum   = ComputeScheduleHash();
    out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    for (uint32_t i = 0u; i < EventCount_; ++i)
    {
        SerializedTimelineRecord rec {};
        rec.TriggerTimeSeconds = Events_[i].TriggerTimeSeconds;
        rec.ActionCode         = static_cast<uint8_t>(Events_[i].Action);
        rec.TargetIndex        = Events_[i].TargetIndex;
        rec.WorldCentre[0]     = Events_[i].WorldCentreMeters[0];
        rec.WorldCentre[1]     = Events_[i].WorldCentreMeters[1];
        rec.WorldCentre[2]     = Events_[i].WorldCentreMeters[2];
        rec.VectorParam[0]     = Events_[i].VectorParam[0];
        rec.VectorParam[1]     = Events_[i].VectorParam[1];
        rec.VectorParam[2]     = Events_[i].VectorParam[2];
        rec.RadiusMeters       = Events_[i].RadiusMeters;
        rec.Magnitude          = Events_[i].Magnitude;
        rec.SecondaryParam     = Events_[i].SecondaryParam;
        out.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
    }

    if (!out.good())
    {
        if (outError) *outError = "I/O write failure saving simulation timeline.";
        return false;
    }
    return true;
}

bool SimulationEventTimeline::LoadFromFile(const std::filesystem::path& path, std::string* outError)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        if (outError) *outError = "Unable to open timeline path for read.";
        return false;
    }

    TimelineFileHeader hdr {};
    in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!in.good() || hdr.Magic != kTimelineMagic || hdr.Version != kTimelineVersion || hdr.EventCount > kMaxTimelineEvents)
    {
        if (outError) *outError = "Invalid or unsupported simulation timeline header.";
        return false;
    }

    SimulationEventTimeline candidate {};
    for (uint32_t i = 0u; i < hdr.EventCount; ++i)
    {
        SerializedTimelineRecord rec {};
        in.read(reinterpret_cast<char*>(&rec), sizeof(rec));
        if (!in.good())
        {
            if (outError) *outError = "Truncated simulation timeline payload.";
            return false;
        }
        TimelineEventEntry e {};
        e.TriggerTimeSeconds = rec.TriggerTimeSeconds;
        e.Action             = static_cast<TimelineActionKind>(rec.ActionCode);
        e.TargetIndex        = rec.TargetIndex;
        e.WorldCentreMeters  = { rec.WorldCentre[0], rec.WorldCentre[1], rec.WorldCentre[2] };
        e.VectorParam        = { rec.VectorParam[0], rec.VectorParam[1], rec.VectorParam[2] };
        e.RadiusMeters       = rec.RadiusMeters;
        e.Magnitude          = rec.Magnitude;
        e.SecondaryParam     = rec.SecondaryParam;
        if (!candidate.ScheduleEvent(e))
        {
            if (outError) *outError = "Failed to restore timeline entry.";
            return false;
        }
    }

    if (candidate.ComputeScheduleHash() != hdr.Checksum)
    {
        if (outError) *outError = "Simulation timeline FNV-1a checksum mismatch (corrupted data).";
        return false;
    }

    *this = candidate;
    return true;
}

} // namespace Frontier::SimulationSpace
