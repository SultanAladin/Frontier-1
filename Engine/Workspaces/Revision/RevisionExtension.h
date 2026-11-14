/*============================================================================================================================================
                                                            REVISIONEXTENSION.H
============================================================================================================================================*/
// 🧩 Application-wide 750 MiB revision coordinator providing global sequence Undo/Redo and cross-tab LRU compaction

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/EngineConstants.h"
#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/Workspaces/Revision/RevisionTracker.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct GlobalCompactionSummary
{
    uint32_t    PurgedRedoStepsTotal   = 0u; // [count] - Inactive redo steps evicted across background trackers
    uint32_t    FoldedPrefixStepsTotal = 0u; // [count] - Oldest prefix steps folded across trackers
    std::size_t GlobalBytesBefore      = 0u; // [B]     - Aggregate memory usage prior to enforcement
    std::size_t GlobalBytesAfter       = 0u; // [B]     - Aggregate memory usage after enforcement
};

class RevisionExtension
{
public:
    explicit RevisionExtension(
        std::size_t GlobalBudgetBytes = Constants::DefaultGlobalRevisionBudgetBytes) noexcept;

    [[nodiscard]] bool Initialize() noexcept;
    void               Tick(double Δt) noexcept;
    void               Suspend() noexcept;
    void               Resume() noexcept;
    void               Finalize() noexcept;

    void ConfigureGlobalBudget(std::size_t NewGlobalBudgetBytes) noexcept;

    [[nodiscard]] uint32_t RegisterDocumentTracker(uint32_t DocumentIdentifier, RevisionTracker* Tracker);
    void                   UnregisterDocumentTracker(uint32_t DocumentIdentifier) noexcept;

    [[nodiscard]] bool RecordGlobalOperation(
        uint32_t                             DocumentIdentifier,
        const ParametricOperationParameters& Parameters,
        std::span<const std::byte>           AuxiliaryPayload    = {},
        GlobalCompactionSummary*             OutGlobalCompaction = nullptr);

    bool UndoDocument(uint32_t DocumentIdentifier) noexcept;
    bool RedoDocument(uint32_t DocumentIdentifier, uint32_t PreferredBranchIndex = HistoryStepEntry::InvalidIndex) noexcept;

    bool UndoGlobalLatest() noexcept;
    bool RedoGlobalNext() noexcept;

    GlobalCompactionSummary EnforceGlobalBudget(uint32_t ProtectedForegroundDocumentId = UINT32_MAX);

    [[nodiscard]] std::size_t QueryGlobalBytesUsed() const noexcept;
    [[nodiscard]] std::size_t QueryGlobalBudgetBytes() const noexcept { return GlobalBudgetCeilingBytes; }
    [[nodiscard]] uint32_t    QueryRegisteredTrackerCount() const noexcept;
    [[nodiscard]] uint64_t    QueryCurrentGlobalSequence() const noexcept { return NextGlobalSequenceStamp - 1ull; }
    [[nodiscard]] bool        QueryActive() const noexcept { return ActivePhase; }

private:
    struct RegisteredTrackerSlot
    {
        uint32_t         DocumentIdentifier = 0u;      // [idx]   - Associated WorkspaceDocument identifier
        RevisionTracker* Tracker            = nullptr; // [-]     - Non-owning pointer to document RevisionTracker
        uint64_t         LastTouchedStamp   = 0ull;    // [count] - Monotonic recency stamp for LRU background compaction
    };

    struct GlobalTimelineEntry
    {
        uint64_t SequenceStamp      = 0ull;  // [count] - Monotonic global edit sequence
        uint32_t DocumentIdentifier = 0u;    // [idx]   - Target document edited at this sequence
        bool     UndoneInTimeline   = false; // [-]     - True while undone via UndoGlobalLatest
    };

    [[nodiscard]] RegisteredTrackerSlot* LocateSlot(uint32_t DocumentIdentifier) noexcept;

    std::size_t                        GlobalBudgetCeilingBytes = Constants::DefaultGlobalRevisionBudgetBytes; // [B] - 750 MiB cap
    uint64_t                           NextGlobalSequenceStamp  = 1ull;  // [count] - Next monotonic sequence stamp
    bool                               ActivePhase              = false; // [-]     - Lifecycle active marker
    bool                               SuspendedPhase           = false; // [-]     - Lifecycle suspended marker
    double                             AccumulatedSeconds       = 0.0;   // [s]     - Cumulative active runtime
    std::vector<RegisteredTrackerSlot> TrackerSlots             {};      // [-]     - Registered open document trackers
    std::vector<GlobalTimelineEntry>   GlobalTimeline           {};      // [-]     - Cross-document undo/redo sequence log
};

static_assert(Lifecycle::SuspendableParticipant<RevisionExtension>);

} // namespace Frontier::Workspaces
