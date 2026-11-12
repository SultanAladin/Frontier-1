/*============================================================================================================================================
                                                            REVISIONTRACKER.CPP
============================================================================================================================================*/
// 🧩 Per-document parametric revision tracker implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Revision/RevisionTracker.h"

#include <algorithm>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 TRACKER OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

RevisionTracker::RevisionTracker(std::size_t DocumentBudgetBytes)
    : Recorder(DocumentBudgetBytes)
{
}

void RevisionTracker::Reset()
{
    History.Reset();
    Recorder.Reset();
    SavedStepIndex            = 0u;
    LatestGlobalSequenceStamp = 0ull;
}

void RevisionTracker::ConfigureDocumentBudget(std::size_t NewBudgetBytes)
{
    Recorder.ConfigureBudgetCeiling(NewBudgetBytes);
    if (Recorder.QueryTotalFootprintBytes() > Recorder.QueryBudgetCeilingBytes())
    {
        (void)Recorder.EnforceBudgetCeiling(History, Recorder.QueryBudgetCeilingBytes());
    }
}

bool RevisionTracker::RecordParametricOperation(
    const ParametricOperationParameters& Parameters,
    uint64_t                             GlobalSequenceStamp,
    std::span<const std::byte>           AuxiliaryPayload,
    CompactionReport*                    OutCompaction)
{
    if (!Recorder.RecordOperation(History, Parameters, AuxiliaryPayload, GlobalSequenceStamp, OutCompaction))
    {
        return false;
    }
    LatestGlobalSequenceStamp = std::max(LatestGlobalSequenceStamp, GlobalSequenceStamp);
    return true;
}

bool RevisionTracker::Undo() noexcept
{
    return History.StepBackward();
}

bool RevisionTracker::Redo(uint32_t PreferredBranchIndex) noexcept
{
    return History.StepForward(PreferredBranchIndex);
}

bool RevisionTracker::JumpToStep(uint32_t StepIndex) noexcept
{
    return History.JumpToStep(StepIndex);
}

void RevisionTracker::MarkSavedAtCurrentCursor() noexcept
{
    SavedStepIndex = History.QueryCursorIndex();
}

bool RevisionTracker::QueryModifiedSinceSave() const noexcept
{
    return History.QueryCursorIndex() != SavedStepIndex;
}

CompactionReport RevisionTracker::PurgeInactiveRedoBranches()
{
    return Recorder.PurgeInactiveRedo(History);
}

CompactionReport RevisionTracker::FoldOldestPrefixSteps(uint32_t StepCount)
{
    return Recorder.FoldOldestPrefix(History, StepCount);
}

CompactionReport RevisionTracker::EnforceTargetFootprint(std::size_t TargetFootprintBytes)
{
    return Recorder.EnforceBudgetCeiling(History, TargetFootprintBytes);
}

ReconstructedDocumentProjection RevisionTracker::ReconstructAtCursor(
    const ReconstructionTargetSpec& TargetSpec) const
{
    return CursorResolution::ReconstructAtCursor(History, Recorder, TargetSpec);
}

bool RevisionTracker::RestoreSerializedRevision(
    const ParametricBaselineSummary&       ImportedBaseline,
    std::span<const ParametricRecordEntry> ImportedRecords,
    std::span<const std::byte>             ImportedRegionBytes,
    std::span<const HistoryStepEntry>      ImportedHistoryEntries,
    uint32_t                               ImportedRootIndex,
    uint32_t                               ImportedCursorIndex,
    uint32_t                               ImportedSavedIndex,
    uint32_t                               NextLocalOrdinal)
{
    if (!Recorder.RestoreSerializedRecorder(ImportedBaseline, ImportedRecords, ImportedRegionBytes))
    {
        return false;
    }
    History.RestoreSerializedEntries(
        ImportedHistoryEntries,
        ImportedRootIndex,
        ImportedCursorIndex,
        NextLocalOrdinal);
    SavedStepIndex = ImportedSavedIndex < History.InspectEntries().size() ? ImportedSavedIndex : History.QueryRootIndex();

    LatestGlobalSequenceStamp = 0ull;
    for (const HistoryStepEntry& Entry : History.InspectEntries())
    {
        if (!Entry.RetiredEntry)
        {
            LatestGlobalSequenceStamp = std::max(LatestGlobalSequenceStamp, Entry.GlobalSequenceStamp);
        }
    }
    return true;
}

} // namespace Frontier::Workspaces
