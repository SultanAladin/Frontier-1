/*==========================================================================================================================================
                                                               LOG RING STORE
                                     ------------------------------------------------------------------
                        Fixed-capacity multi-producer ring of log entries; storage is allocated once and the oldest
                                                          entries are overwritten.
==========================================================================================================================================*/

// 🧩 Fixed-capacity multi-producer ring of log entries; storage is allocated once and the oldest entries are overwritten.

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string_view>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         TYPES
//------------------------------------------------------------------------------------------------------------------------

enum class LogSeverity : uint8_t
{
    Info,
    Warning,
    Error,
    Fatal
};

// 📝 Verbosity refines Info only; Warning and above always carry their own rank (mirrors the approved HTML model).
enum class LogVerbosity : uint8_t
{
    Display,
    Log,
    Verbose
};

enum class LogCeiling : uint8_t
{
    FatalOnly,
    Error,
    Warning,
    Display,
    Log,
    Verbose
};

// Rank order is the filter contract shared with the HTML prototype: Fatal 0 · Error 1 · Warning 2 · Display 3 · Log 4 · Verbose 5.
[[nodiscard]] constexpr uint8_t LogRank(LogSeverity Severity, LogVerbosity Verbosity) noexcept
{
    switch (Severity)
    {
        case LogSeverity::Fatal:   return 0;
        case LogSeverity::Error:   return 1;
        case LogSeverity::Warning: return 2;
        case LogSeverity::Info:    break;
    }
    switch (Verbosity)
    {
        case LogVerbosity::Display: return 3;
        case LogVerbosity::Log:     return 4;
        case LogVerbosity::Verbose: return 5;
    }
    return 4;
}

[[nodiscard]] constexpr uint8_t LogCeilingRank(LogCeiling Ceiling) noexcept
{
    switch (Ceiling)
    {
        case LogCeiling::FatalOnly: return 0;
        case LogCeiling::Error:     return 1;
        case LogCeiling::Warning:   return 2;
        case LogCeiling::Display:   return 3;
        case LogCeiling::Log:       return 4;
        case LogCeiling::Verbose:   return 5;
    }
    return 4;
}

[[nodiscard]] constexpr bool LogPassesCeiling(LogSeverity Severity, LogVerbosity Verbosity, LogCeiling Ceiling) noexcept
{
    return LogRank(Severity, Verbosity) <= LogCeilingRank(Ceiling);
}

[[nodiscard]] constexpr const char* LogSeverityText(LogSeverity Severity) noexcept
{
    switch (Severity)
    {
        case LogSeverity::Info:    return "Info";
        case LogSeverity::Warning: return "Warning";
        case LogSeverity::Error:   return "Error";
        case LogSeverity::Fatal:   return "Fatal";
    }
    return "Info";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Fixed-size slot payload: producers format straight into the ring, so a log write never allocates after start-up.
//    Messages and sources truncate at the buffer edge with a guaranteed NUL terminator.
struct LogEntry
{
    uint64_t     Sequence   = 0;                    // [-]  - Monotonic write order, 1-based; 0 means never written
    uint64_t     WallMicros = 0;                    // [µs] - System-clock microseconds since the epoch at write time
    uint32_t     Category   = 0;                    // [idx] - LogCategoryRegistry token; 0 is the invalid category
    LogSeverity  Severity   = LogSeverity::Info;    // [-]  - Severity classification
    LogVerbosity Verbosity  = LogVerbosity::Log;    // [-]  - Verbosity refinement (Info only)
    char         Message[256] = {};                 // [B]  - Message text, NUL-terminated, truncated to fit
    char         Source[96]   = {};                 // [B]  - Origin text ("File.cpp:line"), NUL-terminated
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      RING STORE
//------------------------------------------------------------------------------------------------------------------------

// Multi-producer / single-consumer ring. Writers claim a sequence atomically and publish with a release store;
// the reader verifies each slot sequence before and after copying, so a slot overwritten mid-copy is skipped
// and accounted as dropped instead of surfacing torn text.
class LogRingStore
{
  public:
    static constexpr uint32_t Capacity = 65536;     // [slots] - Power of two so slot selection is a mask

    LogRingStore();
    ~LogRingStore();

    LogRingStore(const LogRingStore&)            = delete;
    LogRingStore& operator=(const LogRingStore&) = delete;

    // Stamps the entry with the current system-clock time. Returns the assigned sequence.
    uint64_t Write(uint32_t     Category,
                   LogSeverity  Severity,
                   LogVerbosity Verbosity,
                   std::string_view Message,
                   std::string_view Source) noexcept;

    // Same write with an explicit timestamp (validation and replayed sessions).
    uint64_t WriteAt(uint32_t     Category,
                     LogSeverity  Severity,
                     LogVerbosity Verbosity,
                     std::string_view Message,
                     std::string_view Source,
                     uint64_t     WallMicros) noexcept;

    [[nodiscard]] uint64_t QueryWritten() const noexcept;          // [-] - Total entries ever claimed
    [[nodiscard]] uint64_t QueryOldestSequence() const noexcept;   // [-] - Oldest retained sequence; 0 when empty
    [[nodiscard]] uint64_t QueryRetainedCount() const noexcept;    // [entries] - Entries still readable

    // Copies retained entries with Sequence greater than AfterSequence, oldest first, up to OutCapacity.
    // NewCursor receives the last examined sequence (feed it back as AfterSequence on the next call).
    // SkippedSequences receives how many entries in the examined range were overwritten before they could be read.
    uint32_t ReadSince(uint64_t  AfterSequence,
                       LogEntry* Out,
                       uint32_t  OutCapacity,
                       uint64_t& NewCursor,
                       uint64_t& SkippedSequences) const noexcept;

  private:
    struct Slot
    {
        std::atomic<uint64_t> WrittenSequence{0};   // [-] - Published sequence for this slot; 0 means empty
        LogEntry              Entry{};
    };

    // 📝 Adjacent slots share cache lines; at log-stream write rates this is immaterial next to the formatting cost,
    //    so no per-slot padding is spent. Revisit only with measurements.
    std::unique_ptr<Slot[]> Slots;
    std::atomic<uint64_t>   NextSequence{0};        // [-] - Last claimed sequence
};

} // namespace Frontier
