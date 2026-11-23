/*==========================================================================================================================================
                                              PANEL INTAKE MIRRORS THE APPROVED HTML PROTOTYPE
                                     ------------------------------------------------------------------
                                         Same filter predicate, collapse window, caps and counters.
==========================================================================================================================================*/

// 🧩 Panel intake mirrors the approved HTML prototype: same filter predicate, collapse window, caps and counters.

#include "LogStreamPanel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

std::string FoldCase(std::string_view Text)
{
    std::string Folded(Text);
    std::transform(Folded.begin(), Folded.end(), Folded.begin(),
                   [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
    return Folded;
}

bool ContainsFolded(std::string_view Haystack, const std::string& NeedleLower)
{
    if (NeedleLower.empty())
    {
        return true;
    }
    return FoldCase(Haystack).find(NeedleLower) != std::string::npos;
}

uint8_t ScrubSeverityCode(LogSeverity Severity) noexcept
{
    switch (Severity)
    {
        case LogSeverity::Warning: return 1;
        case LogSeverity::Error:   return 2;
        case LogSeverity::Fatal:   return 2;
        case LogSeverity::Info:    break;
    }
    return 0;
}

std::string CollapseKey(const LogEntry& Entry)
{
    std::string Key = std::to_string(Entry.Category);
    Key.push_back('|');
    Key += Entry.Message;
    return Key;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLOCK FORMATTING
//------------------------------------------------------------------------------------------------------------------------

LogClockText FormatLogClock(uint64_t WallMicros, uint64_t SessionStartMicros, bool SessionMode) noexcept
{
    uint64_t Micros = WallMicros;
    if (SessionMode)
    {
        Micros = WallMicros >= SessionStartMicros ? WallMicros - SessionStartMicros : 0;
    }

    const uint64_t Millis  = (Micros / 1000) % 1000;
    const uint64_t Seconds = (Micros / 1000000) % 60;
    const uint64_t Minutes = (Micros / 60000000) % 60;
    const uint64_t Hours   = SessionMode ? Micros / 3600000000 : (Micros / 3600000000) % 24;

    LogClockText Text;
    std::snprintf(Text.Main, sizeof(Text.Main), "%02llu:%02llu:%02llu",
                  static_cast<unsigned long long>(Hours),
                  static_cast<unsigned long long>(Minutes),
                  static_cast<unsigned long long>(Seconds));
    std::snprintf(Text.Millis, sizeof(Text.Millis), ".%03llu",
                  static_cast<unsigned long long>(Millis));
    return Text;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 FILTER CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

void LogStreamPanel::ConfigureSeverity(LogSeverity Severity, bool Enabled)
{
    SeverityEnabled[static_cast<size_t>(Severity)] = Enabled;
    FiltersDirty                                   = true;
}

void LogStreamPanel::ConfigureAllSeverities() noexcept
{
    SeverityEnabled[0] = SeverityEnabled[1] = SeverityEnabled[2] = SeverityEnabled[3] = true;
    FiltersDirty = true;
}

void LogStreamPanel::ConfigureCategory(uint32_t Token, bool Enabled)
{
    if (Enabled)
    {
        DisabledCategories.erase(Token);
    }
    else
    {
        DisabledCategories.insert(Token);
    }
    FiltersDirty = true;
}

void LogStreamPanel::ConfigureAllCategories(const LogCategoryRegistry& Categories, bool Enabled)
{
    DisabledCategories.clear();
    if (!Enabled)
    {
        std::vector<const LogCategory*> All;
        Categories.QueryAll(All);
        for (const LogCategory* Category : All)
        {
            DisabledCategories.insert(Category->Token);
        }
    }
    FiltersDirty = true;
}

void LogStreamPanel::InvertCategories(const LogCategoryRegistry& Categories)
{
    std::vector<const LogCategory*> All;
    Categories.QueryAll(All);
    for (const LogCategory* Category : All)
    {
        if (DisabledCategories.count(Category->Token) > 0)
        {
            DisabledCategories.erase(Category->Token);
        }
        else
        {
            DisabledCategories.insert(Category->Token);
        }
    }
    FiltersDirty = true;
}

void LogStreamPanel::ConfigureCeiling(LogCeiling NewCeiling) noexcept
{
    Ceiling      = NewCeiling;
    FiltersDirty = true;
}

void LogStreamPanel::ConfigureQuery(std::string Text)
{
    Query        = std::move(Text);
    QueryLower   = FoldCase(Query);
    FiltersDirty = true;
}

void LogStreamPanel::ConfigureFollow(bool Follow) noexcept
{
    FollowFlag = Follow;
    if (FollowFlag)
    {
        NewBelow = 0;
    }
}

void LogStreamPanel::ConfigureCollapse(bool Collapse)
{
    CollapseFlag = Collapse;
    FiltersDirty = true;   // [rows] - Toggling re-folds (or unfolds) the retained window
}

void LogStreamPanel::ClearView()
{
    Retained.clear();
    Visible.clear();
    CollapseKeys.clear();
    CategoryCounts.clear();
    SeverityCounts   = {};
    RecentWarnings.clear();
    RecentErrors.clear();
    TotalCount       = 0;
    DroppedCount     = 0;
    NewBelow         = 0;
    SelectedSequence = 0;
    FiltersDirty     = false;
}

void LogStreamPanel::NotifyScrolledUp() noexcept
{
    FollowFlag = false;
}

void LogStreamPanel::NotifyScrolledToEnd() noexcept
{
    NewBelow = 0;
}

void LogStreamPanel::JumpToLatest() noexcept
{
    FollowFlag = true;
    NewBelow   = 0;
}

void LogStreamPanel::SelectVisible(size_t Index)
{
    if (Index >= Visible.size())
    {
        return;
    }
    const uint64_t Sequence = Visible[Index].Entry.Sequence;
    SelectedSequence        = SelectedSequence == Sequence ? 0 : Sequence;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        QUERIES
//------------------------------------------------------------------------------------------------------------------------

bool LogStreamPanel::QuerySeverityEnabled(LogSeverity Severity) const noexcept
{
    return SeverityEnabled[static_cast<size_t>(Severity)];
}

bool LogStreamPanel::QueryCategoryEnabled(uint32_t Token) const noexcept
{
    return DisabledCategories.count(Token) == 0;
}

const LogStreamVisibleEntry* LogStreamPanel::QuerySelected() const noexcept
{
    const int64_t Index = QuerySelectedIndex();
    return Index < 0 ? nullptr : &Visible[static_cast<size_t>(Index)];
}

int64_t LogStreamPanel::QuerySelectedIndex() const noexcept
{
    if (SelectedSequence == 0)
    {
        return -1;
    }
    for (size_t Index = 0; Index < Visible.size(); ++Index)
    {
        if (Visible[Index].Entry.Sequence == SelectedSequence)
        {
            return static_cast<int64_t>(Index);
        }
    }
    return -1;
}

uint64_t LogStreamPanel::QuerySeverityCount(LogSeverity Severity) const noexcept
{
    return SeverityCounts[static_cast<size_t>(Severity)];
}

uint64_t LogStreamPanel::QueryCategoryCount(uint32_t Token) const noexcept
{
    const auto Found = CategoryCounts.find(Token);
    return Found == CategoryCounts.end() ? 0 : Found->second;
}

uint32_t LogStreamPanel::QueryRateNow() const noexcept
{
    return RateBuckets[RateBuckets.size() - 1];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTAKE
//------------------------------------------------------------------------------------------------------------------------

bool LogStreamPanel::Passes(const LogEntry& Entry, const LogCategoryRegistry& Categories) const
{
    if (!SeverityEnabled[static_cast<size_t>(Entry.Severity)])
    {
        return false;
    }
    if (DisabledCategories.count(Entry.Category) > 0)
    {
        return false;
    }
    if (!LogPassesCeiling(Entry.Severity, Entry.Verbosity, Ceiling))
    {
        return false;
    }
    if (QueryLower.empty())
    {
        return true;
    }
    if (ContainsFolded(Entry.Message, QueryLower) || ContainsFolded(Entry.Source, QueryLower))
    {
        return true;
    }
    const LogCategory* Category = Categories.Query(Entry.Category);
    return Category != nullptr && ContainsFolded(Category->Name, QueryLower);
}

void LogStreamPanel::AppendVisible(const LogEntry& Entry)
{
    if (CollapseFlag)
    {
        const std::string Key   = CollapseKey(Entry);
        const auto        Found = CollapseKeys.find(Key);
        if (Found != CollapseKeys.end())
        {
            for (LogStreamVisibleEntry& Row : Visible)
            {
                if (Row.Entry.Sequence != Found->second)
                {
                    continue;
                }
                if (Entry.WallMicros >= Row.Entry.WallMicros &&
                    Entry.WallMicros - Row.Entry.WallMicros < CollapseWindowMicros)
                {
                    Row.RepeatCount      += 1;
                    Row.Entry.WallMicros  = Entry.WallMicros;
                    Row.Entry.Sequence    = Entry.Sequence;
                    CollapseKeys[Key]     = Entry.Sequence;
                    if (SelectedSequence == Found->second)
                    {
                        SelectedSequence = Entry.Sequence;
                    }
                    return;   // [rows] - Folded: no new row, and the new-entries pill does not advance
                }
                break;
            }
        }
        CollapseKeys[Key] = Entry.Sequence;
    }

    LogStreamVisibleEntry Row;
    Row.Entry           = Entry;
    Row.RepeatCount     = 1;
    Row.FirstWallMicros = Entry.WallMicros;
    Visible.push_back(Row);
    TrimVisible();

    if (!FollowFlag)
    {
        NewBelow += 1;
    }
}

void LogStreamPanel::RebuildVisible(const LogCategoryRegistry& Categories)
{
    Visible.clear();
    CollapseKeys.clear();
    NewBelow = 0;
    for (const LogEntry& Entry : Retained)
    {
        if (Passes(Entry, Categories))
        {
            const bool WasFollowing = FollowFlag;
            FollowFlag              = true;   // [rows] - A rebuild is not new-below traffic
            AppendVisible(Entry);
            FollowFlag = WasFollowing;
        }
    }
    TrimVisible();
}

void LogStreamPanel::TrimVisible() noexcept
{
    while (Visible.size() > VisibleLimit)
    {
        Visible.pop_front();
    }
}

void LogStreamPanel::AdvanceBuckets(double Δτ)
{
    if (Δτ <= 0)
    {
        return;
    }
    SecondAccumulator += Δτ;
    while (SecondAccumulator >= 1.0)
    {
        SecondAccumulator -= 1.0;

        // 📝 Shift the 72-second window left by one completed second.
        for (size_t Index = 1; Index < RateBuckets.size(); ++Index)
        {
            RateBuckets[Index - 1] = RateBuckets[Index];
        }
        RateBuckets[RateBuckets.size() - 1] = CurrentSecondCount;
        RatePeak                            = std::max(RatePeak, CurrentSecondCount);

        ScrubBuckets.push_back(LogScrubBucket{CurrentSecondCount, CurrentSecondWorst});
        if (ScrubBuckets.size() > 300)
        {
            ScrubBuckets.erase(ScrubBuckets.begin());
        }

        CurrentSecondCount = 0;
        CurrentSecondWorst = 0;
    }
}

void LogStreamPanel::Synchronize(LogRingStore& Store, const LogCategoryRegistry& Categories, double Δτ)
{
    AdvanceBuckets(Δτ);
    if (PausedFlag)
    {
        return;   // [frames] - View frozen; the ring keeps accepting writes and losses surface on resume
    }

    std::array<LogEntry, 512> Chunk;
    for (;;)
    {
        uint64_t NewCursor = Cursor;
        uint64_t Skipped   = 0;
        const uint32_t Count = Store.ReadSince(Cursor, Chunk.data(), static_cast<uint32_t>(Chunk.size()),
                                               NewCursor, Skipped);
        Cursor        = NewCursor;
        DroppedCount += Skipped;
        if (Count == 0)
        {
            break;
        }

        for (uint32_t Index = 0; Index < Count; ++Index)
        {
            const LogEntry& Entry = Chunk[Index];

            if (SessionStartMicros == 0)
            {
                SessionStartMicros = Entry.WallMicros;
            }
            TotalCount          += 1;
            SeverityCounts[static_cast<size_t>(Entry.Severity)] += 1;
            LastWallMicros = Entry.WallMicros > LastWallMicros ? Entry.WallMicros : LastWallMicros;
            if (Entry.Severity == LogSeverity::Warning)
            {
                RecentWarnings.push_back(Entry.WallMicros);
            }
            else if (Entry.Severity == LogSeverity::Error || Entry.Severity == LogSeverity::Fatal)
            {
                RecentErrors.push_back(Entry.WallMicros);
            }
            CategoryCounts[Entry.Category]                      += 1;
            CurrentSecondCount                                  += 1;
            CurrentSecondWorst = std::max(CurrentSecondWorst, ScrubSeverityCode(Entry.Severity));

            Retained.push_back(Entry);
            while (Retained.size() > RetainedLimit)
            {
                Retained.pop_front();
                DroppedCount += 1;
            }

            if (!FiltersDirty && Passes(Entry, Categories))
            {
                AppendVisible(Entry);
            }
        }
    }

    // Trailing-minute windows for the summary delta badges (pruned against the newest entry clock).
    if (LastWallMicros > 60000000ull)
    {
        const uint64_t Cutoff = LastWallMicros - 60000000ull;
        while (!RecentWarnings.empty() && RecentWarnings.front() <= Cutoff)
        {
            RecentWarnings.pop_front();
        }
        while (!RecentErrors.empty() && RecentErrors.front() <= Cutoff)
        {
            RecentErrors.pop_front();
        }
    }

    if (FiltersDirty)
    {
        RebuildVisible(Categories);
        FiltersDirty = false;
    }
}

} // namespace Frontier
