/*==========================================================================================================================================
                                                              LOG STREAM PANEL
                                     ------------------------------------------------------------------
                         Filtering, repeat collapsing, follow lock and rate history over the shared log ring store.
==========================================================================================================================================*/

// 🧩 Log Stream panel: filtering, repeat collapsing, follow lock and rate history over the shared log ring store.

#pragma once

#include "Engine/Diagnostics/LogCategories.h"
#include "Engine/Diagnostics/LogRingStore.h"

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// One displayed row. Repeat collapsing folds identical (category, message) writes inside six seconds into a
// single row whose timestamp tracks the latest occurrence, exactly like the approved HTML prototype.
struct LogStreamVisibleEntry
{
    LogEntry Entry;                          // [-] - Latest occurrence payload
    uint32_t RepeatCount      = 1;           // [entries] - Occurrences folded into this row (the ×N badge)
    uint64_t FirstWallMicros  = 0;           // [µs] - First occurrence time (detail readout)
};

struct LogScrubBucket
{
    uint32_t Count         = 0;              // [entries] - Entries consumed in that second
    uint8_t  WorstSeverity = 0;              // [-] - 0 info · 1 warning · 2 error/fatal (scrubber tick colour)
};

struct LogClockText
{
    char Main[24]   = {};                    // [-] - "HH:MM:SS" (session hours may exceed 99)
    char Millis[8]  = {};                    // [-] - ".mmm" (drawn dimmed, like the reference dashboards)
};

// Wall mode reads UTC from epoch microseconds; session mode reads elapsed time since the first consumed entry.
[[nodiscard]] LogClockText FormatLogClock(uint64_t WallMicros, uint64_t SessionStartMicros, bool SessionMode) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                      PANEL
//------------------------------------------------------------------------------------------------------------------------

// Renderer-free panel logic. The host calls Synchronize once per frame, then ConstructInterface draws the result
// (presentation lives in LogStreamPanelPresentation.cpp and builds only with the interface SDK present).
// 📝 Pause semantics differ from the HTML prototype on purpose: the prototype paused its generator, the engine
//    pauses the view — producers keep writing into the ring and the panel catches up on resume (losses, if the
//    ring laps, are accounted in QueryDroppedCount).
class LogStreamPanel
{
  public:
    static constexpr size_t   RetainedLimit = 4096;   // [entries] - Raw entries the panel keeps for re-filtering
    static constexpr size_t   VisibleLimit  = 420;    // [rows] - Displayed rows kept (prototype DOM cap)
    static constexpr uint64_t CollapseWindowMicros = 6000000;   // [µs] - Repeat-folding window

    // -- Filter configuration (each marks the visible list for rebuild) --
    void ConfigureSeverity(LogSeverity Severity, bool Enabled);
    void ConfigureAllSeverities() noexcept;
    void ConfigureCategory(uint32_t Token, bool Enabled);
    void ConfigureAllCategories(const LogCategoryRegistry& Categories, bool Enabled);
    void InvertCategories(const LogCategoryRegistry& Categories);
    void ConfigureCeiling(LogCeiling Ceiling) noexcept;
    void ConfigureQuery(std::string Text);

    // -- Stream behaviour --
    void ConfigurePaused(bool Paused) noexcept   { PausedFlag = Paused; }
    void ConfigureFollow(bool Follow) noexcept;
    void ConfigureWrap(bool Wrap) noexcept       { WrapFlag = Wrap; }
    void ConfigureCollapse(bool Collapse);
    void ConfigureSessionClock(bool Session) noexcept { SessionClock = Session; }
    void ClearView();

    void NotifyScrolledUp() noexcept;      // Presentation: user scrolled away from the tail (breaks follow)
    void NotifyScrolledToEnd() noexcept;   // Presentation: user returned to the tail (clears the new-entries pill)
    void JumpToLatest() noexcept;          // Re-engage follow and clear the pill

    void SelectVisible(size_t Index);      // Toggles expansion selection, like clicking a prototype row

    // -- Frame intake --
    void Synchronize(LogRingStore& Store, const LogCategoryRegistry& Categories, double Δτ);

    // -- Presentation (LogStreamPanelPresentation.cpp) --
    void ConstructInterface();

    // -- Queries --
    [[nodiscard]] bool QuerySeverityEnabled(LogSeverity Severity) const noexcept;
    [[nodiscard]] bool QueryCategoryEnabled(uint32_t Token) const noexcept;
    [[nodiscard]] LogCeiling QueryCeiling() const noexcept { return Ceiling; }
    [[nodiscard]] const std::string& QueryQuery() const noexcept { return Query; }
    [[nodiscard]] bool QueryPaused() const noexcept { return PausedFlag; }
    [[nodiscard]] bool QueryFollow() const noexcept { return FollowFlag; }
    [[nodiscard]] bool QueryWrap() const noexcept { return WrapFlag; }
    [[nodiscard]] bool QueryCollapse() const noexcept { return CollapseFlag; }
    [[nodiscard]] bool QuerySessionClock() const noexcept { return SessionClock; }

    [[nodiscard]] const std::deque<LogStreamVisibleEntry>& QueryVisible() const noexcept { return Visible; }
    [[nodiscard]] const LogStreamVisibleEntry*             QuerySelected() const noexcept;
    [[nodiscard]] int64_t                                  QuerySelectedIndex() const noexcept;

    [[nodiscard]] uint64_t QueryTotalCount() const noexcept { return TotalCount; }              // [entries] - Since last ClearView
    [[nodiscard]] uint64_t QuerySeverityCount(LogSeverity Severity) const noexcept;
    [[nodiscard]] uint64_t QueryWarningsLastMinute() const noexcept { return RecentWarnings.size(); } // [entries] - Delta badge
    [[nodiscard]] uint64_t QueryErrorsLastMinute() const noexcept { return RecentErrors.size(); }     // [entries] - Error + Fatal
    [[nodiscard]] uint64_t QueryCategoryCount(uint32_t Token) const noexcept;
    [[nodiscard]] uint64_t QueryDroppedCount() const noexcept { return DroppedCount; }          // [entries] - No longer retained
    [[nodiscard]] uint32_t QueryNewBelow() const noexcept { return NewBelow; }                  // [rows] - Arrived while scrolled up
    [[nodiscard]] uint64_t QuerySessionStartMicros() const noexcept { return SessionStartMicros; }

    [[nodiscard]] const std::array<uint32_t, 72>& QueryRateBuckets() const noexcept { return RateBuckets; }  // [entries/s] - Oldest first
    [[nodiscard]] uint32_t QueryRateNow() const noexcept;                                       // [entries/s] - Last completed second
    [[nodiscard]] uint32_t QueryRatePeak() const noexcept { return RatePeak; }
    [[nodiscard]] const std::vector<LogScrubBucket>& QueryScrubBuckets() const noexcept { return ScrubBuckets; }

  private:
    [[nodiscard]] bool Passes(const LogEntry& Entry, const LogCategoryRegistry& Categories) const;
    void AppendVisible(const LogEntry& Entry);
    void RebuildVisible(const LogCategoryRegistry& Categories);
    void AdvanceBuckets(double Δτ);
    void TrimVisible() noexcept;

    // Filter state
    bool                             SeverityEnabled[4] = {true, true, true, true};
    std::unordered_set<uint32_t>     DisabledCategories;      // [idx] - Absence means enabled (new categories default on)
    LogCeiling                       Ceiling = LogCeiling::Log;
    std::string                      Query;
    std::string                      QueryLower;
    bool                             FiltersDirty = false;

    // Behaviour state
    bool PausedFlag   = false;
    bool FollowFlag   = true;
    bool WrapFlag     = false;
    bool CollapseFlag = true;
    bool SessionClock = false;

    // Retained + visible data
    std::deque<LogEntry>              Retained;
    std::deque<LogStreamVisibleEntry> Visible;
    std::unordered_map<std::string, uint64_t> CollapseKeys;   // [-] - "token|message" -> representative sequence
    uint64_t                          Cursor = 0;             // [-] - Last store sequence consumed
    uint64_t                          SelectedSequence = 0;   // [-] - Expanded row (0 = none)
    uint64_t                          SessionStartMicros = 0; // [µs] - First consumed entry time

    // Counters (since ClearView)
    uint64_t                          TotalCount = 0;
    std::array<uint64_t, 4>           SeverityCounts{};
    std::unordered_map<uint32_t, uint64_t> CategoryCounts;
    uint64_t                          DroppedCount = 0;
    uint32_t                          NewBelow = 0;
    std::deque<uint64_t>              RecentWarnings;        // [µs] - Warning wall times inside the trailing minute
    std::deque<uint64_t>              RecentErrors;          // [µs] - Error/Fatal wall times inside the trailing minute
    uint64_t                          LastWallMicros = 0;    // [µs] - Newest consumed entry (the prune clock)

    // Rate history
    std::array<uint32_t, 72>          RateBuckets{};          // [entries/s] - Last 72 completed seconds
    std::vector<LogScrubBucket>       ScrubBuckets;           // [s] - Up to 300 one-second ticks
    uint32_t                          CurrentSecondCount = 0; // [entries] - Consumed in the open second
    uint8_t                           CurrentSecondWorst = 0; // [-] - Worst severity code in the open second
    uint32_t                          RatePeak = 0;           // [entries/s]
    double                            SecondAccumulator = 0;  // [s] - Fractional second carried between frames
};

} // namespace Frontier
