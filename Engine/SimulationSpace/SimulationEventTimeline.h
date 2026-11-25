/*============================================================================================================================================
                                                  SIMULATIONEVENTTIMELINE.H
============================================================================================================================================*/
// 🧩 Phase K01: Zero-allocation deterministic multi-domain event sequencer & binary playback recorder.
//    Schedules and replays RadialBlast, FluidBrushStroke, SoftBody toggles, ForceField strength updates, and C callbacks
//    against SimulationCoordinatorExtension at exact simulation timestamps, with FNV-1a binary persistence and instant rewind.

#pragma once

#include "Engine/SimulationSpace/SimulationCoordinatorExtension.h"
#include "Engine/SimulationSpace/SimulationInteractionBridge.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace Frontier::SimulationSpace
{

inline constexpr uint32_t kMaxTimelineEvents = 128u;

enum class TimelineActionKind : uint8_t
{
    None                   = 0u,
    InjectRadialBlast      = 1u,
    InjectFluidStroke      = 2u,
    EnableSoftBody         = 3u,
    DisableSoftBody        = 4u,
    ScaleForceStrength     = 5u,
    InvokeCallback         = 6u
};

struct TimelineEventEntry;
using TimelineEventCallback = void (*)(const TimelineEventEntry& entry, double simTimeSeconds, void* userContext) noexcept;

struct TimelineEventEntry
{
    double                TriggerTimeSeconds  = 0.0;
    TimelineActionKind    Action              = TimelineActionKind::None;
    uint32_t              TargetIndex         = 0u; // SoftBody InstanceId OR ForceField entry index
    std::array<float, 3>  WorldCentreMeters   { 0.0f, 0.0f, 0.0f };
    std::array<float, 3>  VectorParam         { 0.0f, 2.0f, 0.0f }; // Stroke velocity
    float                 RadiusMeters        = 2.0f;
    float                 Magnitude           = 6.0f; // Blast speed OR Force strength OR Fluid density
    float                 SecondaryParam      = 1.0f; // Fluid temperature OR Blast temp boost
    bool                  Executed            = false;
    TimelineEventCallback Callback            = nullptr;
    void*                 UserContext         = nullptr;
};

class SimulationEventTimeline
{
public:
    SimulationEventTimeline() noexcept = default;

    void Clear() noexcept;

    /// Schedules a deterministic multi-domain action in chronological order (returns false if capacity reached).
    [[nodiscard]] bool ScheduleEvent(const TimelineEventEntry& entry) noexcept;

    /// Convenience recorders for live interactive actions so user sessions can be saved and replayed deterministically.
    [[nodiscard]] bool RecordRadialBlast(
        double triggerTimeSeconds,
        const std::array<float, 3>& centreMeters,
        float radiusMeters,
        float peakSpeedMps,
        float fluidTempBoost = 1.2f) noexcept;

    [[nodiscard]] bool RecordFluidStroke(
        double triggerTimeSeconds,
        const std::array<float, 3>& centreMeters,
        const std::array<float, 3>& velocityMps,
        float radiusMeters,
        float densityAmount = 1.5f,
        float temperatureAmount = 0.5f) noexcept;

    [[nodiscard]] bool RecordForceStrength(
        double triggerTimeSeconds,
        uint32_t forceEntryIndex,
        float newStrength) noexcept;

    /// Evaluates and executes all pending events with TriggerTimeSeconds <= simTimeSeconds (0 heap allocations).
    /// Returns the number of events executed during this call.
    [[nodiscard]] uint32_t EvaluateUpToTime(
        double simTimeSeconds,
        SimulationCoordinatorExtension& coordinator) noexcept;

    /// Resets execution state of all events with TriggerTimeSeconds > toTimeSeconds so rewind replays bit-identically.
    void Rewind(double toTimeSeconds) noexcept;

    [[nodiscard]] uint32_t QueryEventCount() const noexcept { return EventCount_; }
    [[nodiscard]] uint32_t QueryExecutedCount() const noexcept { return ExecutedCount_; }
    [[nodiscard]] const TimelineEventEntry* InspectEvent(uint32_t index) const noexcept
    {
        return (index < EventCount_) ? &Events_[index] : nullptr;
    }

    /// Computes a deterministic 64-bit FNV-1a checksum over all scheduled event parameters (excluding runtime pointers).
    [[nodiscard]] uint64_t ComputeScheduleHash() const noexcept;

    /// Serializes / deserializes the scheduled timeline track to a compact binary file with FNV-1a integrity verification.
    [[nodiscard]] bool SaveToFile(const std::filesystem::path& path, std::string* outError = nullptr) const;
    [[nodiscard]] bool LoadFromFile(const std::filesystem::path& path, std::string* outError = nullptr);

private:
    std::array<TimelineEventEntry, kMaxTimelineEvents> Events_        {};
    uint32_t                                           EventCount_    = 0u;
    uint32_t                                           ExecutedCount_ = 0u;
};

} // namespace Frontier::SimulationSpace
