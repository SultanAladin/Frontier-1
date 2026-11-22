/*============================================================================================================================================
                                                            THOUGHTLOG.H
============================================================================================================================================*/
// 🧩 In-memory timing ledger: instrumented scopes write samples into a RAM ring buffer and flush once on application exit

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>

//------------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------------
namespace Frontier::Diagnostics
{

//------------------------------------------------------------------------------------------------------------------------
// Records nanosecond timing samples for named scopes entirely in RAM and aggregates them on a worker thread.
// Hot paths pay two clock reads + one lock-free ring write; all allocation, aggregation, and file I/O happen
// off the hot path. Usage:
//     FRONTIER_THOUGHT_SCOPE("Editor.ViewportRaster");        // RAII scope timer (one per line)
//     ThoughtLog::RecordSample("Import.Decode", 123456);      // manual sample in nanoseconds
// Call Shutdown() once when the application closes to flush "Logs/FrontierThoughtLog.txt" and close the log.
class ThoughtLog
{
public:
    // Starts the background aggregator thread (idempotent; also auto-started by the first recorded sample)
    static void Initialize();

    // Stops the aggregator thread, flushes the aggregated report to disk, and closes the log (idempotent)
    static void Shutdown();

    // Interns a scope name and returns its stable id (thread-safe; call once per instrumentation site)
    [[nodiscard]] static uint32_t ResolveNameId(const char* Name);

    // Records one timing sample for an interned name id (lock-free ring write; safe from any thread)
    static void RecordScopeSample(uint32_t NameId, uint64_t DurationNs) noexcept;

    // Records one timing sample by name, interning on first use (convenience for ad-hoc call sites)
    static void RecordSample(const char* Name, uint64_t DurationNs);

    // True between Initialize() (or first sample) and Shutdown()
    [[nodiscard]] static bool QueryRunning() noexcept;

    // Writes the aggregated report for everything recorded so far (Shutdown calls this with the default path)
    static void FlushReport(const std::filesystem::path& TargetFile);
};

//------------------------------------------------------------------------------------------------------------------------
// RAII scope timer: records the elapsed steady-clock time of the enclosing scope under an interned name id.
class ScopedThoughtTimer
{
public:
    explicit ScopedThoughtTimer(uint32_t NameId) noexcept
        : NameId(NameId)
        , StartTick(std::chrono::steady_clock::now())
    {
    }

    ~ScopedThoughtTimer()
    {
        const uint64_t ElapsedNs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - StartTick).count());
        ThoughtLog::RecordScopeSample(NameId, ElapsedNs);
    }

    ScopedThoughtTimer(const ScopedThoughtTimer&)            = delete;
    ScopedThoughtTimer& operator=(const ScopedThoughtTimer&) = delete;

private:
    uint32_t                                NameId    = 0u;
    std::chrono::steady_clock::time_point   StartTick {};
};

} // namespace Frontier::Diagnostics

//------------------------------------------------------------------------------------------------------------------------
// One scope timer per line: FRONTIER_THOUGHT_SCOPE("Subsystem.Phase")
#define FRONTIER_THOUGHT_PASTE_INNER(A, B) A##B
#define FRONTIER_THOUGHT_PASTE(A, B) FRONTIER_THOUGHT_PASTE_INNER(A, B)
#define FRONTIER_THOUGHT_SCOPE(NameLiteral)                                                                             \
    static const uint32_t FRONTIER_THOUGHT_PASTE(ThoughtSiteId_, __LINE__) =                                            \
        ::Frontier::Diagnostics::ThoughtLog::ResolveNameId(NameLiteral);                                                \
    const ::Frontier::Diagnostics::ScopedThoughtTimer FRONTIER_THOUGHT_PASTE(ThoughtTimer_, __LINE__)(                  \
        FRONTIER_THOUGHT_PASTE(ThoughtSiteId_, __LINE__))
