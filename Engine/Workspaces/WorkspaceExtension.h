/*============================================================================================================================================
                                                            WORKSPACEEXTENSION.H
============================================================================================================================================*/
// 🧩 Top-level workspace lifecycle coordinator unifying WorkspaceRegistry, 750 MiB RevisionExtension, WorkspaceExchange, & WorkspaceThread

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/EngineConstants.h"
#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/Threading/Sync/CompletionCounter.h"
#include "Engine/Threading/Sync/CriticalSection.h"
#include "Engine/Threading/Sync/ThreadProgressionTracker.h"
#include "Engine/Threading/Tasks/ExecutionChannels.h"
#include "Engine/Threading/Threads/Workspace/WorkspaceThread.h"
#include "Engine/Workspaces/Revision/RevisionExtension.h"
#include "Engine/Workspaces/WorkspaceExchange.h"
#include "Engine/Workspaces/WorkspaceRegistry.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class WorkspaceExtension
{
public:
    explicit WorkspaceExtension(
        std::size_t GlobalRevisionBudgetBytes = Constants::DefaultGlobalRevisionBudgetBytes,
        uint32_t    ChannelLaneCount          = 2u);

    ~WorkspaceExtension() noexcept;

    WorkspaceExtension(const WorkspaceExtension&)            = delete;
    WorkspaceExtension& operator=(const WorkspaceExtension&) = delete;

    [[nodiscard]] bool Initialize() noexcept;
    void               Tick(double Δt) noexcept;
    void               Suspend() noexcept;
    void               Resume() noexcept;
    void               Finalize() noexcept;

    [[nodiscard]] WorkspaceToken CreateWorkspaceDocument(
        Codecs::SpaceExtensionCategory Category,
        std::string_view               Title,
        const std::filesystem::path&   InitialPath                 = {},
        std::size_t                    DocumentRevisionBudgetBytes = Constants::DefaultDocumentRevisionBudgetBytes);

    [[nodiscard]] WorkspaceToken OpenWorkspaceDocument(
        const std::filesystem::path& SourcePath,
        std::string&                 OutDiagnostic,
        std::size_t                  DocumentRevisionBudgetBytes = Constants::DefaultDocumentRevisionBudgetBytes);

    bool CloseWorkspaceDocument(WorkspaceToken Token) noexcept;

    [[nodiscard]] bool RecordDocumentOperation(
        WorkspaceToken                       Token,
        const ParametricOperationParameters& Parameters,
        std::span<const std::byte>           AuxiliaryPayload    = {},
        GlobalCompactionSummary*             OutGlobalCompaction = nullptr);

    [[nodiscard]] bool EnqueueAsyncDocumentOperation(
        WorkspaceToken                Token,
        ParametricOperationParameters Parameters,
        Threading::CompletionCounter* CompletionTarget = nullptr);

    [[nodiscard]] bool EnqueueAsyncDocumentSave(
        WorkspaceToken                Token,
        Threading::CompletionCounter* CompletionTarget = nullptr);

    bool UndoInDocument(WorkspaceToken Token) noexcept;
    bool RedoInDocument(WorkspaceToken Token, uint32_t PreferredBranchIndex = HistoryStepEntry::InvalidIndex) noexcept;
    bool UndoGlobalLatest() noexcept;
    bool RedoGlobalNext() noexcept;

    [[nodiscard]] WorkspaceDocument*                            ResolveDocument(WorkspaceToken Token) noexcept;
    [[nodiscard]] const WorkspaceDocument*                      InspectDocument(WorkspaceToken Token) const noexcept;
    [[nodiscard]] RevisionExtension&                            AccessRevisionExtension() noexcept { return GlobalRevisions; }
    [[nodiscard]] const RevisionExtension&                      InspectRevisionExtension() const noexcept { return GlobalRevisions; }
    [[nodiscard]] WorkspaceExchange&                            AccessExchange() noexcept { return Exchange; }
    [[nodiscard]] const WorkspaceExchange&                      InspectExchange() const noexcept { return Exchange; }
    [[nodiscard]] Threading::WorkspaceThread&                   AccessWorkspaceThread() noexcept { return DedicatedThread; }
    [[nodiscard]] const Threading::WorkspaceThread&             InspectWorkspaceThread() const noexcept { return DedicatedThread; }
    [[nodiscard]] Threading::ExecutionChannels&                  AccessExecutionChannels() noexcept { return Cluster; }
    [[nodiscard]] const Threading::ThreadProgressionTracker&            InspectThreadProgressionTracker() const noexcept { return Monitor; }
    [[nodiscard]] Threading::PublishedWorkspaceSnapshot         InspectPublishedSnapshot() const noexcept
    {
        return DedicatedThread.InspectPublishedSnapshot();
    }

private:
    void PublishLiveTelemetry(std::string_view OperationLabel) noexcept;

    uint32_t                        RequestedChannelLanes = 2u;    // [count] - Parallel ExecutionChannels lanes
    bool                            ActivePhase           = false; // [-]     - Lifecycle active marker
    bool                            SuspendedPhase        = false; // [-]     - Lifecycle suspended marker
    Threading::CriticalSection      DocumentAccessSection {};      // [-]     - Serializes synchronous & async WorkspaceThread edits
    Threading::ThreadProgressionTracker     Monitor               {};      // [-]     - Lock-free thread liveness monitor
    Threading::ExecutionChannels     Cluster               {};      // [-]     - Cooperative parallel execution cluster
    Threading::WorkspaceThread      DedicatedThread       {};      // [-]     - Asynchronous WorkspaceThread domain loop
    WorkspaceRegistry               Registry              {};      // [-]     - Open .Space document slot registry
    RevisionExtension               GlobalRevisions       ;        // [-]     - Application-wide 750 MiB revision coordinator
    WorkspaceExchange               Exchange              {};      // [-]     - Cross-workspace live publication bus
};

static_assert(Lifecycle::SuspendableParticipant<WorkspaceExtension>);

} // namespace Frontier::Workspaces
