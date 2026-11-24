/*============================================================================================================================================
                                                             PHYSICSDEBUGLOG.H
============================================================================================================================================*/
// 🧩 Fixed-capacity thread-safe physics debug log ring for vehicle, collision, trigger, constraint & surface events (F3 telemetry hook)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace Frontier::SimulationSpace::PhysicsSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t PhysicsDebugLogRingCapacity = 256u; // [cnt] - Fixed circular log buffer capacity
inline constexpr uint32_t PhysicsDebugLogMessageBytes = 160u; // [B]   - Inline zero-allocation message length

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class PhysicsLogDomain : uint8_t
{
    Solver     = 0u, // [-] - Jolt solver lifecycle & step diagnostics
    Vehicle    = 1u, // [-] - Powertrain, gear shifts, induction, fuel & aero
    Collision  = 2u, // [-] - High-impulse contacts & hull baking
    Trigger    = 3u, // [-] - Sensor volume enter / exit transitions
    Constraint = 4u, // [-] - Dock latch, tether taut/rupture, joint events
    Surface    = 5u, // [-] - Tire PhysicsMaterial surface changes
};

enum class PhysicsLogSeverity : uint8_t
{
    Trace   = 0u, // [-] - Verbose instrumentation
    Event   = 1u, // [-] - Normal physical transition
    Warning = 2u, // [-] - Starvation, rupture, over-temp or numerical clamp
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PhysicsDebugLogEntry
{
    uint64_t           Sequence       = 0ull;                      // [id] - Monotonic log entry sequence
    double             SimulatedSec   = 0.0;                       // [s]  - Physics simulation clock at log emit
    PhysicsLogDomain   Domain         = PhysicsLogDomain::Solver;  // [-]  - Source domain tag
    PhysicsLogSeverity Severity       = PhysicsLogSeverity::Event; // [-]  - Severity level
    char               Message[PhysicsDebugLogMessageBytes] {};    // [-]  - Null-terminated ASCII message
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class PhysicsDebugLog
{
public:
    [[nodiscard]] static const char* QueryDomainLabel(PhysicsLogDomain Domain) noexcept
    {
        switch (Domain)
        {
            case PhysicsLogDomain::Solver:     return "SOLVER";
            case PhysicsLogDomain::Vehicle:    return "VEHICLE";
            case PhysicsLogDomain::Collision:  return "COLLISION";
            case PhysicsLogDomain::Trigger:    return "TRIGGER";
            case PhysicsLogDomain::Constraint: return "CONSTRAINT";
            case PhysicsLogDomain::Surface:    return "SURFACE";
        }
        return "PHYSICS";
    }

    void Clear() noexcept
    {
        std::lock_guard<std::mutex> Lock(RingMutex);
        WriteCursor = 0u;
        StoredCount = 0u;
        NextSeq     = 1ull;
        LatestSequence.store(0ull, std::memory_order_release);
    }

    void ConfigureConsoleEcho(bool EnableEcho) noexcept
    {
        ConsoleEcho.store(EnableEcho, std::memory_order_release);
    }

    void AppendFormatted(
        double             SimulatedSeconds,
        PhysicsLogDomain   Domain,
        PhysicsLogSeverity Severity,
        const char*        Format,
        ...) noexcept
    {
        if (Format == nullptr)
        {
            return;
        }

        char Formatted[PhysicsDebugLogMessageBytes] {};
        va_list Args;
        va_start(Args, Format);
        std::vsnprintf(Formatted, sizeof(Formatted), Format, Args);
        va_end(Args);

        PhysicsDebugLogEntry Entry {};
        Entry.SimulatedSec = SimulatedSeconds;
        Entry.Domain       = Domain;
        Entry.Severity     = Severity;
        std::memcpy(Entry.Message, Formatted, sizeof(Entry.Message));
        Entry.Message[PhysicsDebugLogMessageBytes - 1u] = '\0';

        {
            std::lock_guard<std::mutex> Lock(RingMutex);
            Entry.Sequence     = NextSeq++;
            Ring[WriteCursor]  = Entry;
            WriteCursor        = (WriteCursor + 1u) % PhysicsDebugLogRingCapacity;
            if (StoredCount < PhysicsDebugLogRingCapacity)
            {
                ++StoredCount;
            }
            LatestSequence.store(Entry.Sequence, std::memory_order_release);
        }

        if (ConsoleEcho.load(std::memory_order_acquire))
        {
            std::printf("[PHYS:%s t=%.2fs] %s\n", QueryDomainLabel(Domain), SimulatedSeconds, Entry.Message);
        }
    }

    [[nodiscard]] std::vector<PhysicsDebugLogEntry> CopyRecentEntries(uint32_t MaxCount = 64u) const
    {
        std::lock_guard<std::mutex> Lock(RingMutex);
        const uint32_t Count = std::min(MaxCount, StoredCount);
        std::vector<PhysicsDebugLogEntry> Out {};
        Out.reserve(Count);
        const uint32_t Start = (WriteCursor + PhysicsDebugLogRingCapacity - Count) % PhysicsDebugLogRingCapacity;
        for (uint32_t I = 0u; I < Count; ++I)
        {
            Out.push_back(Ring[(Start + I) % PhysicsDebugLogRingCapacity]);
        }
        return Out;
    }

    [[nodiscard]] bool CopyLatestMessage(char* OutBuffer, std::size_t BufferBytes) const noexcept
    {
        if (OutBuffer == nullptr || BufferBytes == 0u)
        {
            return false;
        }
        std::lock_guard<std::mutex> Lock(RingMutex);
        if (StoredCount == 0u)
        {
            OutBuffer[0] = '\0';
            return false;
        }
        const uint32_t LastIdx = (WriteCursor + PhysicsDebugLogRingCapacity - 1u) % PhysicsDebugLogRingCapacity;
        const auto& E          = Ring[LastIdx];
        std::snprintf(OutBuffer, BufferBytes, "[%s] %s", QueryDomainLabel(E.Domain), E.Message);
        return true;
    }

    [[nodiscard]] uint64_t QueryTotalEmittedCount() const noexcept
    {
        return LatestSequence.load(std::memory_order_acquire);
    }

    [[nodiscard]] static std::string FormatEntryLine(const PhysicsDebugLogEntry& Entry)
    {
        char Line[PhysicsDebugLogMessageBytes + 32u] {};
        std::snprintf(
            Line,
            sizeof(Line),
            "[%s %.2fs] %s",
            QueryDomainLabel(Entry.Domain),
            Entry.SimulatedSec,
            Entry.Message);
        return std::string(Line);
    }

    [[nodiscard]] static PhysicsDebugLog& AccessSharedJournal() noexcept
    {
        static PhysicsDebugLog Shared {};
        return Shared;
    }

private:
    mutable std::mutex                                             RingMutex      {};
    std::array<PhysicsDebugLogEntry, PhysicsDebugLogRingCapacity>  Ring           {};
    uint32_t                                                       WriteCursor    = 0u;
    uint32_t                                                       StoredCount    = 0u;
    uint64_t                                                       NextSeq        = 1ull;
    std::atomic<uint64_t>                                          LatestSequence { 0ull };
    std::atomic<bool>                                              ConsoleEcho    { false };
};

} // namespace Frontier::SimulationSpace::PhysicsSpace
