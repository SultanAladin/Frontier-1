/*============================================================================================================================================
                                                           WORKSPACEEXTENSION.CPP
============================================================================================================================================*/
// 🧩 Top-level asynchronous WorkspaceExtension coordinator implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/UVCodec.h"
#include "Engine/Workspaces/WorkspaceExtension.h"

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                             WORKSPACE EXTENSION OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

WorkspaceExtension::WorkspaceExtension(
    std::size_t GlobalRevisionBudgetBytes,
    uint32_t    ChannelLaneCount)
    : RequestedChannelLanes(ChannelLaneCount == 0u ? 1u : ChannelLaneCount)
    , GlobalRevisions(GlobalRevisionBudgetBytes)
{
}

WorkspaceExtension::~WorkspaceExtension() noexcept
{
    Finalize();
}

bool WorkspaceExtension::Initialize() noexcept
{
    if (ActivePhase)
    {
        return true;
    }

    if (!GlobalRevisions.Initialize())
    {
        return false;
    }
    if (!Cluster.Initialize(RequestedChannelLanes, &Monitor))
    {
        GlobalRevisions.Finalize();
        return false;
    }
    if (!DedicatedThread.Initialize(&Monitor, &Cluster))
    {
        Cluster.Finalize();
        GlobalRevisions.Finalize();
        return false;
    }

    ActivePhase    = true;
    SuspendedPhase = false;
    PublishLiveTelemetry("WorkspaceExtension Initialized");
    return true;
}

void WorkspaceExtension::Tick(double Δt) noexcept
{
    if (!ActivePhase || SuspendedPhase)
    {
        return;
    }
    Threading::ScopedCriticalGuard Guard(DocumentAccessSection);
    GlobalRevisions.Tick(Δt);
}

void WorkspaceExtension::Suspend() noexcept
{
    if (ActivePhase)
    {
        SuspendedPhase = true;
        GlobalRevisions.Suspend();
    }
}

void WorkspaceExtension::Resume() noexcept
{
    if (ActivePhase)
    {
        SuspendedPhase = false;
        GlobalRevisions.Resume();
    }
}

void WorkspaceExtension::Finalize() noexcept
{
    if (!ActivePhase)
    {
        return;
    }

    DedicatedThread.Finalize();
    Cluster.Finalize();
    {
        Threading::ScopedCriticalGuard Guard(DocumentAccessSection);
        Registry.CloseAllDocuments();
        GlobalRevisions.Finalize();
    }
    ActivePhase    = false;
    SuspendedPhase = false;
}

WorkspaceToken WorkspaceExtension::CreateWorkspaceDocument(
    Codecs::SpaceExtensionCategory Category,
    std::string_view               Title,
    const std::filesystem::path&   InitialPath,
    std::size_t                    DocumentRevisionBudgetBytes)
{
    WorkspaceToken CreatedToken{};
    {
        Threading::ScopedCriticalGuard Guard(DocumentAccessSection);
        CreatedToken = Registry.CreateDocument(Category, Title, InitialPath, DocumentRevisionBudgetBytes);
        WorkspaceDocument* Document = Registry.ResolveDocument(CreatedToken);
        if (Document != nullptr)
        {
            (void)GlobalRevisions.RegisterDocumentTracker(
                Document->QueryDocumentIdentifier(),
                &Document->AccessRevisionTracker());
        }
    }
    PublishLiveTelemetry("Created .Space Document");
    return CreatedToken;
}

WorkspaceToken WorkspaceExtension::OpenWorkspaceDocument(
    const std::filesystem::path& SourcePath,
    std::string&                 OutDiagnostic,
    std::size_t                  DocumentRevisionBudgetBytes)
{
    WorkspaceToken OpenedToken{};
    {
        Threading::ScopedCriticalGuard Guard(DocumentAccessSection);
        OpenedToken = Registry.OpenDocumentFromFile(SourcePath, OutDiagnostic, DocumentRevisionBudgetBytes);
        WorkspaceDocument* Document = Registry.ResolveDocument(OpenedToken);
        if (Document != nullptr)
        {
            (void)GlobalRevisions.RegisterDocumentTracker(
                Document->QueryDocumentIdentifier(),
                &Document->AccessRevisionTracker());
        }
    }
    if (OpenedToken.VerifyValid())
    {
        PublishLiveTelemetry("Opened .Space Document");
    }
    return OpenedToken;
}

bool WorkspaceExtension::CloseWorkspaceDocument(WorkspaceToken Token) noexcept
{
    bool Closed = false;
    {
        Threading::ScopedCriticalGuard Guard(DocumentAccessSection);
        WorkspaceDocument* Document = Registry.ResolveDocument(Token);
        if (Document != nullptr)
        {
            GlobalRevisions.UnregisterDocumentTracker(Document->QueryDocumentIdentifier());
        }
        Closed = Registry.CloseDocument(Token);
    }
    if (Closed)
    {
        PublishLiveTelemetry("Closed .Space Document");
    }
    return Closed;
}

bool WorkspaceExtension::RecordDocumentOperation(
    WorkspaceToken                       Token,
    const ParametricOperationParameters& Parameters,
    std::span<const std::byte>           AuxiliaryPayload,
    GlobalCompactionSummary*             OutGlobalCompaction)
{
    bool Recorded = false;
    {
        Threading::ScopedCriticalGuard Guard(DocumentAccessSection);
        WorkspaceDocument* Document = Registry.ResolveDocument(Token);
        if (Document == nullptr)
        {
            return false;
        }
        Recorded = GlobalRevisions.RecordGlobalOperation(
            Document->QueryDocumentIdentifier(),
            Parameters,
            AuxiliaryPayload,
            OutGlobalCompaction);

        if (Recorded && Parameters.Category == ParametricOperationCategory::UVTileTransform)
        {
            const uint32_t UdimCode = Codecs::UVEncoder::ComputeUDIMNumber(Parameters.TileU, Parameters.TileV);
            (void)Exchange.PublishUpdate(Token, UdimCode, Parameters.Scalars[4], Parameters.OperationTag);
        }
    }

    if (Recorded)
    {
        PublishLiveTelemetry(Parameters.OperationTag);
    }
    return Recorded;
}

bool WorkspaceExtension::EnqueueAsyncDocumentOperation(
    WorkspaceToken                Token,
    ParametricOperationParameters Parameters,
    Threading::CompletionCounter* CompletionTarget)
{
    return DedicatedThread.EnqueueAuthoringTask(
        [this, Token, Parameters]()
        {
            (void)RecordDocumentOperation(Token, Parameters);
        },
        100u + Token.SlotIndex,
        CompletionTarget);
}

bool WorkspaceExtension::EnqueueAsyncDocumentSave(
    WorkspaceToken                Token,
    Threading::CompletionCounter* CompletionTarget)
{
    return DedicatedThread.EnqueueAuthoringTask(
        [this, Token]()
        {
            bool Saved = false;
            {
                Threading::ScopedCriticalGuard Guard(DocumentAccessSection);
                WorkspaceDocument* Document = Registry.ResolveDocument(Token);
                if (Document != nullptr)
                {
                    std::string Diagnostic;
                    Saved = Document->SaveToDisk(Diagnostic, true);
                }
            }
            if (Saved)
            {
                (void)DedicatedThread.EnqueueImmediateTask(
                    [this]() { PublishLiveTelemetry("Saved .Space Container"); },
                    10u);
            }
        },
        200u + Token.SlotIndex,
        CompletionTarget);
}

bool WorkspaceExtension::UndoInDocument(WorkspaceToken Token) noexcept
{
    bool Changed = false;
    {
        Threading::ScopedCriticalGuard Guard(DocumentAccessSection);
        WorkspaceDocument* Document = Registry.ResolveDocument(Token);
        if (Document != nullptr)
        {
            Changed = GlobalRevisions.UndoDocument(Document->QueryDocumentIdentifier());
        }
    }
    if (Changed)
    {
        PublishLiveTelemetry("Document Undo");
    }
    return Changed;
}

bool WorkspaceExtension::RedoInDocument(WorkspaceToken Token, uint32_t PreferredBranchIndex) noexcept
{
    bool Changed = false;
    {
        Threading::ScopedCriticalGuard Guard(DocumentAccessSection);
        WorkspaceDocument* Document = Registry.ResolveDocument(Token);
        if (Document != nullptr)
        {
            Changed = GlobalRevisions.RedoDocument(Document->QueryDocumentIdentifier(), PreferredBranchIndex);
        }
    }
    if (Changed)
    {
        PublishLiveTelemetry("Document Redo");
    }
    return Changed;
}

bool WorkspaceExtension::UndoGlobalLatest() noexcept
{
    bool Changed = false;
    {
        Threading::ScopedCriticalGuard Guard(DocumentAccessSection);
        Changed = GlobalRevisions.UndoGlobalLatest();
    }
    if (Changed)
    {
        PublishLiveTelemetry("Global Sequence Undo");
    }
    return Changed;
}

bool WorkspaceExtension::RedoGlobalNext() noexcept
{
    bool Changed = false;
    {
        Threading::ScopedCriticalGuard Guard(DocumentAccessSection);
        Changed = GlobalRevisions.RedoGlobalNext();
    }
    if (Changed)
    {
        PublishLiveTelemetry("Global Sequence Redo");
    }
    return Changed;
}

WorkspaceDocument* WorkspaceExtension::ResolveDocument(WorkspaceToken Token) noexcept
{
    Threading::ScopedCriticalGuard Guard(DocumentAccessSection);
    return Registry.ResolveDocument(Token);
}

const WorkspaceDocument* WorkspaceExtension::InspectDocument(WorkspaceToken Token) const noexcept
{
    return Registry.InspectDocument(Token);
}

void WorkspaceExtension::PublishLiveTelemetry(std::string_view OperationLabel) noexcept
{
    uint32_t    OpenCount   = 0u;
    std::size_t UsedBytes   = 0u;
    std::size_t BudgetBytes = Constants::DefaultGlobalRevisionBudgetBytes;
    {
        Threading::ScopedCriticalGuard Guard(DocumentAccessSection);
        OpenCount   = Registry.QueryOpenDocumentCount();
        UsedBytes   = GlobalRevisions.QueryGlobalBytesUsed();
        BudgetBytes = GlobalRevisions.QueryGlobalBudgetBytes();
    }
    DedicatedThread.PublishSnapshot(OpenCount, UsedBytes, BudgetBytes, OperationLabel);
}

} // namespace Frontier::Workspaces
