/*============================================================================================================================================
                                                             NONLINEARHISTORY.H
============================================================================================================================================*/
// 🧩 Directed branching revision tree using strict non-kinship index links (EnclosingIndex, FirstNestedIndex, NextPeerIndex)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct HistoryStepEntry
{
    static constexpr uint32_t InvalidIndex = UINT32_MAX; // [idx] - Unassigned index marker

    uint32_t LocalOrdinal        = 0u;           // [count] - Monotonic per-document revision ordinal (0 = baseline anchor)
    uint64_t GlobalSequenceStamp = 0ull;         // [count] - Application-wide RevisionExtension stamp
    uint32_t EnclosingIndex      = InvalidIndex; // [idx]   - Preceding step index toward baseline root
    uint32_t FirstNestedIndex    = InvalidIndex; // [idx]   - First forward branch step index
    uint32_t NextPeerIndex       = InvalidIndex; // [idx]   - Alternate forward branch step sharing EnclosingIndex
    uint32_t ActiveForwardIndex  = InvalidIndex; // [idx]   - Selected default redo target from this step
    uint32_t DepthLevel          = 0u;           // [count] - Distance in steps from baseline anchor
    uint32_t RecordEntryIndex    = 0u;           // [idx]   - Associated parametric RecordEntry index
    bool     RetiredEntry        = false;        // [-]     - Marked retired after Redo Purge or Prefix Fold
};

class NonLinearHistory
{
public:
    NonLinearHistory();

    void Reset();

    [[nodiscard]] uint32_t AppendStepFromCursor(
        uint32_t RecordEntryIndex,
        uint64_t GlobalSequenceStamp);

    bool StepBackward() noexcept;
    bool StepForward(uint32_t PreferredNestedIndex = HistoryStepEntry::InvalidIndex) noexcept;
    bool JumpToStep(uint32_t TargetStepIndex) noexcept;

    void ExtractActiveRootToCursorChain(std::vector<uint32_t>& OutStepIndices) const;
    [[nodiscard]] std::vector<uint32_t> QueryForwardBranchesAtCursor() const;

    std::vector<uint32_t> PurgeInactiveBranches();
    std::vector<uint32_t> FoldOldestActivePrefixSteps(uint32_t MaxStepsToFold);

    [[nodiscard]] uint32_t                         QueryCursorIndex() const noexcept { return CursorStepIndex; }
    [[nodiscard]] uint32_t                         QueryRootIndex() const noexcept { return RootStepIndex; }
    [[nodiscard]] uint32_t                         QueryLiveStepCount() const noexcept;
    [[nodiscard]] bool                             QueryUndoAvailable() const noexcept;
    [[nodiscard]] bool                             QueryRedoAvailable() const noexcept;
    [[nodiscard]] std::span<const HistoryStepEntry> InspectEntries() const noexcept { return Entries; }

    void RestoreSerializedEntries(
        std::span<const HistoryStepEntry> ImportedEntries,
        uint32_t                          ImportedRootIndex,
        uint32_t                          ImportedCursorIndex,
        uint32_t                          NextOrdinalCounter);

    [[nodiscard]] uint32_t QueryNextLocalOrdinal() const noexcept { return NextLocalOrdinal; }

private:
    std::vector<HistoryStepEntry> Entries          {}; // [-]     - Indexed array of revision step links
    uint32_t                      RootStepIndex    = 0u; // [idx] - Current baseline anchor step index
    uint32_t                      CursorStepIndex  = 0u; // [idx] - Current active cursor step index
    uint32_t                      NextLocalOrdinal = 1u; // [count] - Next local step ordinal
};

} // namespace Frontier::Workspaces
