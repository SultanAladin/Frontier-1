/*============================================================================================================================================
                                                            NONLINEARHISTORY.CPP
============================================================================================================================================*/
// 🧩 Non-kinship directed revision tree implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Revision/NonLinearHistory.h"

#include <algorithm>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  HISTORY OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

NonLinearHistory::NonLinearHistory()
{
    Reset();
}

void NonLinearHistory::Reset()
{
    Entries.clear();
    HistoryStepEntry Anchor{};
    Anchor.LocalOrdinal        = 0u;
    Anchor.GlobalSequenceStamp = 0ull;
    Anchor.DepthLevel          = 0u;
    Anchor.RecordEntryIndex    = 0u;
    Entries.push_back(Anchor);
    RootStepIndex    = 0u;
    CursorStepIndex  = 0u;
    NextLocalOrdinal = 1u;
}

uint32_t NonLinearHistory::AppendStepFromCursor(
    uint32_t RecordEntryIndex,
    uint64_t GlobalSequenceStamp)
{
    const uint32_t NewIndex = static_cast<uint32_t>(Entries.size());
    HistoryStepEntry Step{};
    Step.LocalOrdinal        = NextLocalOrdinal++;
    Step.GlobalSequenceStamp = GlobalSequenceStamp;
    Step.EnclosingIndex      = CursorStepIndex;
    Step.DepthLevel          = Entries[CursorStepIndex].DepthLevel + 1u;
    Step.RecordEntryIndex    = RecordEntryIndex;
    Entries.push_back(Step);

    HistoryStepEntry& Enclosing = Entries[CursorStepIndex];
    if (Enclosing.FirstNestedIndex == HistoryStepEntry::InvalidIndex)
    {
        Enclosing.FirstNestedIndex = NewIndex;
    }
    else
    {
        uint32_t TailIndex = Enclosing.FirstNestedIndex;
        while (Entries[TailIndex].NextPeerIndex != HistoryStepEntry::InvalidIndex)
        {
            TailIndex = Entries[TailIndex].NextPeerIndex;
        }
        Entries[TailIndex].NextPeerIndex = NewIndex;
    }
    Enclosing.ActiveForwardIndex = NewIndex;
    CursorStepIndex              = NewIndex;
    return NewIndex;
}

bool NonLinearHistory::StepBackward() noexcept
{
    if (!QueryUndoAvailable())
    {
        return false;
    }
    const uint32_t Enclosing = Entries[CursorStepIndex].EnclosingIndex;
    if (Enclosing == HistoryStepEntry::InvalidIndex || Entries[Enclosing].RetiredEntry)
    {
        return false;
    }
    Entries[Enclosing].ActiveForwardIndex = CursorStepIndex;
    CursorStepIndex                       = Enclosing;
    return true;
}

bool NonLinearHistory::StepForward(uint32_t PreferredNestedIndex) noexcept
{
    const HistoryStepEntry& Current = Entries[CursorStepIndex];
    uint32_t TargetIndex            = PreferredNestedIndex;
    if (TargetIndex == HistoryStepEntry::InvalidIndex)
    {
        TargetIndex = Current.ActiveForwardIndex != HistoryStepEntry::InvalidIndex
                          ? Current.ActiveForwardIndex
                          : Current.FirstNestedIndex;
    }

    if (TargetIndex == HistoryStepEntry::InvalidIndex ||
        TargetIndex >= Entries.size() ||
        Entries[TargetIndex].RetiredEntry ||
        Entries[TargetIndex].EnclosingIndex != CursorStepIndex)
    {
        return false;
    }

    Entries[CursorStepIndex].ActiveForwardIndex = TargetIndex;
    CursorStepIndex                             = TargetIndex;
    return true;
}

bool NonLinearHistory::JumpToStep(uint32_t TargetStepIndex) noexcept
{
    if (TargetStepIndex >= Entries.size() || Entries[TargetStepIndex].RetiredEntry)
    {
        return false;
    }
    CursorStepIndex = TargetStepIndex;
    return true;
}

void NonLinearHistory::ExtractActiveRootToCursorChain(std::vector<uint32_t>& OutStepIndices) const
{
    OutStepIndices.clear();
    uint32_t WalkIndex = CursorStepIndex;
    while (WalkIndex != HistoryStepEntry::InvalidIndex && WalkIndex < Entries.size())
    {
        if (!Entries[WalkIndex].RetiredEntry)
        {
            OutStepIndices.push_back(WalkIndex);
        }
        if (WalkIndex == RootStepIndex)
        {
            break;
        }
        WalkIndex = Entries[WalkIndex].EnclosingIndex;
    }
    std::reverse(OutStepIndices.begin(), OutStepIndices.end());
}

std::vector<uint32_t> NonLinearHistory::QueryForwardBranchesAtCursor() const
{
    std::vector<uint32_t> Branches;
    uint32_t Nested = Entries[CursorStepIndex].FirstNestedIndex;
    while (Nested != HistoryStepEntry::InvalidIndex && Nested < Entries.size())
    {
        if (!Entries[Nested].RetiredEntry)
        {
            Branches.push_back(Nested);
        }
        Nested = Entries[Nested].NextPeerIndex;
    }
    return Branches;
}

std::vector<uint32_t> NonLinearHistory::PurgeInactiveBranches()
{
    std::vector<uint32_t> ActiveChain;
    ExtractActiveRootToCursorChain(ActiveChain);

    std::vector<bool> PreserveMask(Entries.size(), false);
    for (uint32_t ActiveStep : ActiveChain)
    {
        PreserveMask[ActiveStep] = true;
    }

    std::vector<uint32_t> RetiredRecordIndices;
    for (std::size_t Index = 0u; Index < Entries.size(); ++Index)
    {
        HistoryStepEntry& Entry = Entries[Index];
        if (!PreserveMask[Index] && !Entry.RetiredEntry)
        {
            Entry.RetiredEntry       = true;
            Entry.FirstNestedIndex   = HistoryStepEntry::InvalidIndex;
            Entry.NextPeerIndex      = HistoryStepEntry::InvalidIndex;
            Entry.ActiveForwardIndex = HistoryStepEntry::InvalidIndex;
            RetiredRecordIndices.push_back(Entry.RecordEntryIndex);
        }
    }

    // Re-link active chain cleanly so no preserved entry points into a retired forward branch.
    for (std::size_t ChainPos = 0u; ChainPos < ActiveChain.size(); ++ChainPos)
    {
        HistoryStepEntry& Step  = Entries[ActiveChain[ChainPos]];
        Step.NextPeerIndex      = HistoryStepEntry::InvalidIndex;
        if (ChainPos + 1u < ActiveChain.size())
        {
            const uint32_t NextStep = ActiveChain[ChainPos + 1u];
            Step.FirstNestedIndex   = NextStep;
            Step.ActiveForwardIndex = NextStep;
        }
        else
        {
            Step.FirstNestedIndex   = HistoryStepEntry::InvalidIndex;
            Step.ActiveForwardIndex = HistoryStepEntry::InvalidIndex;
        }
    }

    return RetiredRecordIndices;
}

std::vector<uint32_t> NonLinearHistory::FoldOldestActivePrefixSteps(uint32_t MaxStepsToFold)
{
    std::vector<uint32_t> FoldedRecordIndices;
    if (MaxStepsToFold == 0u)
    {
        return FoldedRecordIndices;
    }

    std::vector<uint32_t> ActiveChain;
    ExtractActiveRootToCursorChain(ActiveChain);
    if (ActiveChain.size() <= 1u)
    {
        return FoldedRecordIndices;
    }

    // Never fold past CursorStepIndex: at least the cursor step becomes the new baseline anchor.
    const std::size_t MaxAvailable = ActiveChain.size() - 1u;
    const std::size_t FoldCount    = std::min<std::size_t>(MaxStepsToFold, MaxAvailable);

    for (std::size_t StepOffset = 1u; StepOffset <= FoldCount; ++StepOffset)
    {
        const uint32_t AbsorbedStepIndex = ActiveChain[StepOffset];
        FoldedRecordIndices.push_back(Entries[AbsorbedStepIndex].RecordEntryIndex);

        const uint32_t PreviousRoot = ActiveChain[StepOffset - 1u];
        Entries[PreviousRoot].RetiredEntry       = true;
        Entries[PreviousRoot].FirstNestedIndex   = HistoryStepEntry::InvalidIndex;
        Entries[PreviousRoot].NextPeerIndex      = HistoryStepEntry::InvalidIndex;
        Entries[PreviousRoot].ActiveForwardIndex = HistoryStepEntry::InvalidIndex;
    }

    RootStepIndex                          = ActiveChain[FoldCount];
    Entries[RootStepIndex].EnclosingIndex  = HistoryStepEntry::InvalidIndex;
    Entries[RootStepIndex].NextPeerIndex   = HistoryStepEntry::InvalidIndex;
    Entries[RootStepIndex].DepthLevel      = 0u;

    for (std::size_t StepOffset = FoldCount + 1u; StepOffset < ActiveChain.size(); ++StepOffset)
    {
        Entries[ActiveChain[StepOffset]].DepthLevel = static_cast<uint32_t>(StepOffset - FoldCount);
    }

    return FoldedRecordIndices;
}

uint32_t NonLinearHistory::QueryLiveStepCount() const noexcept
{
    uint32_t Count = 0u;
    for (const HistoryStepEntry& Entry : Entries)
    {
        if (!Entry.RetiredEntry)
        {
            ++Count;
        }
    }
    return Count;
}

bool NonLinearHistory::QueryUndoAvailable() const noexcept
{
    return CursorStepIndex != RootStepIndex &&
           Entries[CursorStepIndex].EnclosingIndex != HistoryStepEntry::InvalidIndex;
}

bool NonLinearHistory::QueryRedoAvailable() const noexcept
{
    const HistoryStepEntry& Current = Entries[CursorStepIndex];
    if (Current.ActiveForwardIndex != HistoryStepEntry::InvalidIndex &&
        !Entries[Current.ActiveForwardIndex].RetiredEntry)
    {
        return true;
    }
    return Current.FirstNestedIndex != HistoryStepEntry::InvalidIndex &&
           !Entries[Current.FirstNestedIndex].RetiredEntry;
}

void NonLinearHistory::RestoreSerializedEntries(
    std::span<const HistoryStepEntry> ImportedEntries,
    uint32_t                          ImportedRootIndex,
    uint32_t                          ImportedCursorIndex,
    uint32_t                          NextOrdinalCounter)
{
    if (ImportedEntries.empty())
    {
        Reset();
        return;
    }
    Entries.assign(ImportedEntries.begin(), ImportedEntries.end());
    RootStepIndex    = ImportedRootIndex < Entries.size() ? ImportedRootIndex : 0u;
    CursorStepIndex  = ImportedCursorIndex < Entries.size() ? ImportedCursorIndex : RootStepIndex;
    NextLocalOrdinal = std::max(1u, NextOrdinalCounter);
}

} // namespace Frontier::Workspaces
