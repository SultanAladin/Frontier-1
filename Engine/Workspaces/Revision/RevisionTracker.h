/*============================================================================================================================================
                                                             REVISIONTRACKER.H
============================================================================================================================================*/
// 🧩 Per-document 215 MiB parametric revision tracker coordinating NonLinearHistory, OperationRecorder, and CursorResolution

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/EngineConstants.h"
#include "Engine/Workspaces/Revision/CursorResolution.h"
#include "Engine/Workspaces/Revision/NonLinearHistory.h"
#include "Engine/Workspaces/Revision/OperationRecorder.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class RevisionTracker
{
public:
    explicit RevisionTracker(std::size_t DocumentBudgetBytes = Constants::DefaultDocumentRevisionBudgetBytes);

    void Reset();
    void ConfigureDocumentBudget(std::size_t NewBudgetBytes);

    [[nodiscard]] bool RecordParametricOperation(
        const ParametricOperationParameters& Parameters,
        uint64_t                             GlobalSequenceStamp = 0ull,
        std::span<const std::byte>           AuxiliaryPayload    = {},
        CompactionReport*                    OutCompaction       = nullptr);

    bool Undo() noexcept;
    bool Redo(uint32_t PreferredBranchIndex = HistoryStepEntry::InvalidIndex) noexcept;
    bool JumpToStep(uint32_t StepIndex) noexcept;

    void               MarkSavedAtCurrentCursor() noexcept;
    [[nodiscard]] bool QueryModifiedSinceSave() const noexcept;

    CompactionReport PurgeInactiveRedoBranches();
    CompactionReport FoldOldestPrefixSteps(uint32_t StepCount);
    CompactionReport EnforceTargetFootprint(std::size_t TargetFootprintBytes);

    [[nodiscard]] ReconstructedDocumentProjection ReconstructAtCursor(
        const ReconstructionTargetSpec& TargetSpec = {}) const;

    [[nodiscard]] std::size_t              QueryBytesUsed() const noexcept { return Recorder.QueryTotalFootprintBytes(); }
    [[nodiscard]] std::size_t              QueryDocumentBudgetBytes() const noexcept { return Recorder.QueryBudgetCeilingBytes(); }
    [[nodiscard]] uint32_t                 QueryCursorStepIndex() const noexcept { return History.QueryCursorIndex(); }
    [[nodiscard]] uint32_t                 QuerySavedStepIndex() const noexcept { return SavedStepIndex; }
    [[nodiscard]] uint32_t                 QueryLiveStepCount() const noexcept { return History.QueryLiveStepCount(); }
    [[nodiscard]] bool                     QueryUndoAvailable() const noexcept { return History.QueryUndoAvailable(); }
    [[nodiscard]] bool                     QueryRedoAvailable() const noexcept { return History.QueryRedoAvailable(); }
    [[nodiscard]] std::vector<uint32_t>    QueryForwardBranchesAtCursor() const { return History.QueryForwardBranchesAtCursor(); }
    [[nodiscard]] uint64_t                 QueryLatestSequenceStamp() const noexcept { return LatestGlobalSequenceStamp; }
    [[nodiscard]] const NonLinearHistory&  InspectHistory() const noexcept { return History; }
    [[nodiscard]] const OperationRecorder& InspectRecorder() const noexcept { return Recorder; }

    bool RestoreSerializedRevision(
        const ParametricBaselineSummary&       ImportedBaseline,
        std::span<const ParametricRecordEntry> ImportedRecords,
        std::span<const std::byte>             ImportedRegionBytes,
        std::span<const HistoryStepEntry>      ImportedHistoryEntries,
        uint32_t                               ImportedRootIndex,
        uint32_t                               ImportedCursorIndex,
        uint32_t                               ImportedSavedIndex,
        uint32_t                               NextLocalOrdinal);

private:
    NonLinearHistory  History                   {};  // [-]     - Directed non-kinship revision tree
    OperationRecorder Recorder                  ;    // [-]     - Parametric operation recorder & 215 MiB region
    uint32_t          SavedStepIndex            = 0u;// [idx]   - Step index corresponding to clean saved disk mark
    uint64_t          LatestGlobalSequenceStamp = 0ull; // [count] - Most recent global stamp applied to this tracker
};

} // namespace Frontier::Workspaces
