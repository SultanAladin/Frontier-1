/*============================================================================================================================================
                                                          WORKSPACELIFECYCLE.CPP
============================================================================================================================================*/
// 🧩 Multi-document workspace session loop implementation

#include "Engine/Lifecycle/WorkspaceLifecycle.h"

namespace Frontier::Lifecycle
{

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

WorkspaceLifecycle::~WorkspaceLifecycle() noexcept
{
    Finalize();
}

bool WorkspaceLifecycle::Initialize(bool OpenDefaultEmptySession) noexcept
{
    Finalize();

    if (!SessionSequence.Initialize())
    {
        return false;
    }

    Initialized       = true;
    LatestStepSeconds = 0.0;

    if (OpenDefaultEmptySession)
    {
        WorkspaceDocumentDescriptor DefaultDoc{};
        DefaultDoc.DocumentTitle   = "Scene";
        DefaultDoc.OutlinerVariant = OutlinerTitleVariant::Outliner;
        DefaultDoc.InitialMode     = ViewportExecutionMode::Edit;
        (void)OpenSession(DefaultDoc);
    }

    return true;
}

void WorkspaceLifecycle::Tick(double Δt) noexcept
{
    if (!Initialized)
    {
        return;
    }
    LatestStepSeconds = Δt;
    SessionSequence.Tick(Δt);
}

bool WorkspaceLifecycle::Suspend() noexcept
{
    return Initialized && SessionSequence.Suspend();
}

bool WorkspaceLifecycle::Resume() noexcept
{
    return Initialized && SessionSequence.Resume();
}

void WorkspaceLifecycle::Finalize() noexcept
{
    SessionSequence.Finalize();

    for (SessionSlot& Slot : Slots)
    {
        if (Slot.Occupied)
        {
            Slot.Occupied = false;
            Slot.Panes.reset();
            Slot.Revisions.clear();
            ++Slot.Generation;
        }
    }
    Slots.clear();
    ActiveSessionToken = {};
    LatestStepSeconds  = 0.0;
    Initialized        = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              DOCUMENT SESSION OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

WorkspaceSessionToken WorkspaceLifecycle::OpenSession(const WorkspaceDocumentDescriptor& Descriptor)
{
    if (!Initialized)
    {
        return {};
    }

    uint32_t ChosenIndex = 0u;
    for (uint32_t Index = 0u; Index < static_cast<uint32_t>(Slots.size()); ++Index)
    {
        if (!Slots[Index].Occupied)
        {
            ChosenIndex = Index + 1u;
            break;
        }
    }
    if (ChosenIndex == 0u)
    {
        Slots.push_back(SessionSlot{});
        ChosenIndex = static_cast<uint32_t>(Slots.size());
    }

    SessionSlot& Slot      = Slots[ChosenIndex - 1u];
    Slot.Occupied          = true;
    Slot.Title             = Descriptor.DocumentTitle.empty() ? std::string{ "Untitled" } : std::string{ Descriptor.DocumentTitle };
    Slot.TitleVariant      = Descriptor.OutlinerVariant;
    Slot.Panes             = std::make_unique<WorkspaceSessionPanes>();
    Slot.Revisions.clear();
    Slot.RevisionCursor    = 0u;
    Slot.SavedRevisionMark = 0u;

    // 📝 Cross-wire shared panes while preserving the empty-initial-scene invariant (0 entries).
    Slot.Panes->Outliner.ConfigureTitleVariant(Descriptor.OutlinerVariant);
    Slot.Panes->Inspector.AttachOutliner(&Slot.Panes->Outliner);
    Slot.Panes->Viewport.AttachOutliner(&Slot.Panes->Outliner);
    Slot.Panes->Viewport.ConfigureSceneTitle(Slot.Title);
    Slot.Panes->Viewport.ConfigureExecutionMode(Descriptor.InitialMode, false);

    const WorkspaceSessionToken Issued{ ChosenIndex, Slot.Generation };
    ActiveSessionToken = Issued;
    return Issued;
}

bool WorkspaceLifecycle::CloseSession(WorkspaceSessionToken Token) noexcept
{
    SessionSlot* Slot = LocateMutable(Token);
    if (Slot == nullptr)
    {
        return false;
    }

    Slot->Occupied = false;
    Slot->Panes.reset();
    Slot->Revisions.clear();
    Slot->RevisionCursor    = 0u;
    Slot->SavedRevisionMark = 0u;
    ++Slot->Generation;
    if (Slot->Generation == 0u)
    {
        Slot->Generation = 1u;
    }

    if (ActiveSessionToken == Token)
    {
        ActiveSessionToken = {};
        for (uint32_t Index = 0u; Index < static_cast<uint32_t>(Slots.size()); ++Index)
        {
            if (Slots[Index].Occupied)
            {
                ActiveSessionToken = WorkspaceSessionToken{ Index + 1u, Slots[Index].Generation };
                break;
            }
        }
    }
    return true;
}

bool WorkspaceLifecycle::ActivateSession(WorkspaceSessionToken Token) noexcept
{
    if (LocateConst(Token) == nullptr)
    {
        return false;
    }
    ActiveSessionToken = Token;
    return true;
}

void WorkspaceLifecycle::RenameSession(WorkspaceSessionToken Token, std::string_view NewTitle)
{
    SessionSlot* Slot = LocateMutable(Token);
    if (Slot == nullptr || NewTitle.empty())
    {
        return;
    }
    Slot->Title.assign(NewTitle.data(), NewTitle.size());
    if (Slot->Panes)
    {
        Slot->Panes->Viewport.ConfigureSceneTitle(Slot->Title);
    }
}

uint32_t WorkspaceLifecycle::RecordRevision(WorkspaceSessionToken Token, std::string_view RevisionSummary)
{
    SessionSlot* Slot = LocateMutable(Token);
    if (Slot == nullptr)
    {
        return 0u;
    }

    if (Slot->RevisionCursor < static_cast<uint32_t>(Slot->Revisions.size()))
    {
        Slot->Revisions.resize(Slot->RevisionCursor);
    }

    const uint32_t NextOrdinal = static_cast<uint32_t>(Slot->Revisions.size()) + 1u;
    RevisionEntry  Entry{};
    Entry.RevisionIndex = NextOrdinal;
    Entry.Summary       = std::string{ RevisionSummary };
    Slot->Revisions.push_back(std::move(Entry));
    Slot->RevisionCursor = NextOrdinal;
    return NextOrdinal;
}

bool WorkspaceLifecycle::StepBackwardRevision(WorkspaceSessionToken Token) noexcept
{
    SessionSlot* Slot = LocateMutable(Token);
    if (Slot == nullptr || Slot->RevisionCursor == 0u)
    {
        return false;
    }
    --Slot->RevisionCursor;
    return true;
}

bool WorkspaceLifecycle::StepForwardRevision(WorkspaceSessionToken Token) noexcept
{
    SessionSlot* Slot = LocateMutable(Token);
    if (Slot == nullptr || Slot->RevisionCursor >= static_cast<uint32_t>(Slot->Revisions.size()))
    {
        return false;
    }
    ++Slot->RevisionCursor;
    return true;
}

void WorkspaceLifecycle::MarkSessionSaved(WorkspaceSessionToken Token) noexcept
{
    if (SessionSlot* Slot = LocateMutable(Token))
    {
        Slot->SavedRevisionMark = Slot->RevisionCursor;
    }
}

void WorkspaceLifecycle::PresentActivePanesInImGui(double Δt, float ScaleFactor)
{
    WorkspaceSessionPanes* Panes = AccessActivePanes();
    if (Panes == nullptr)
    {
        return;
    }
    Panes->Outliner.PresentInImGuiPane(Δt, ScaleFactor);
    Panes->Viewport.PresentInImGuiPane(Δt, ScaleFactor);
    Panes->Inspector.PresentInImGuiPane(Δt, ScaleFactor);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 INSPECTION & QUERIES
//------------------------------------------------------------------------------------------------------------------------

uint32_t WorkspaceLifecycle::OpenSessionCount() const noexcept
{
    uint32_t Count = 0u;
    for (const SessionSlot& Slot : Slots)
    {
        if (Slot.Occupied)
        {
            ++Count;
        }
    }
    return Count;
}

std::vector<WorkspaceSessionToken> WorkspaceLifecycle::EnumerateOpenSessions() const
{
    std::vector<WorkspaceSessionToken> Result;
    Result.reserve(Slots.size());
    for (uint32_t Index = 0u; Index < static_cast<uint32_t>(Slots.size()); ++Index)
    {
        if (Slots[Index].Occupied)
        {
            Result.push_back(WorkspaceSessionToken{ Index + 1u, Slots[Index].Generation });
        }
    }
    return Result;
}

WorkspaceSessionReading WorkspaceLifecycle::InspectSession(WorkspaceSessionToken Token) const noexcept
{
    WorkspaceSessionReading Reading{};
    const SessionSlot*      Slot = LocateConst(Token);
    if (Slot == nullptr)
    {
        return Reading;
    }

    Reading.Token            = Token;
    Reading.DocumentTitle    = Slot->Title;
    Reading.OutlinerVariant  = Slot->TitleVariant;
    Reading.ExecutionMode    = Slot->Panes ? Slot->Panes->Viewport.QueryExecutionMode() : ViewportExecutionMode::Edit;
    Reading.RevisionCursor   = Slot->RevisionCursor;
    Reading.RevisionTotal    = static_cast<uint32_t>(Slot->Revisions.size());
    Reading.ConstructedSlots = Slot->Panes ? Slot->Panes->Outliner.QueryTotalCount() : 0u;
    Reading.Modified         = (Slot->RevisionCursor != Slot->SavedRevisionMark);
    Reading.ActiveTab        = (ActiveSessionToken == Token);
    return Reading;
}

WorkspaceSessionPanes* WorkspaceLifecycle::AccessSessionPanes(WorkspaceSessionToken Token) noexcept
{
    SessionSlot* Slot = LocateMutable(Token);
    return (Slot != nullptr && Slot->Panes) ? Slot->Panes.get() : nullptr;
}

const WorkspaceSessionPanes* WorkspaceLifecycle::InspectSessionPanes(WorkspaceSessionToken Token) const noexcept
{
    const SessionSlot* Slot = LocateConst(Token);
    return (Slot != nullptr && Slot->Panes) ? Slot->Panes.get() : nullptr;
}

WorkspaceSessionPanes* WorkspaceLifecycle::AccessActivePanes() noexcept
{
    return AccessSessionPanes(ActiveSessionToken);
}

WorkspaceLifecycle::SessionSlot* WorkspaceLifecycle::LocateMutable(WorkspaceSessionToken Token) noexcept
{
    if (!Token.Valid() || Token.SlotIndex > Slots.size())
    {
        return nullptr;
    }
    SessionSlot& Slot = Slots[Token.SlotIndex - 1u];
    return (Slot.Occupied && Slot.Generation == Token.Generation) ? &Slot : nullptr;
}

const WorkspaceLifecycle::SessionSlot* WorkspaceLifecycle::LocateConst(WorkspaceSessionToken Token) const noexcept
{
    if (!Token.Valid() || Token.SlotIndex > Slots.size())
    {
        return nullptr;
    }
    const SessionSlot& Slot = Slots[Token.SlotIndex - 1u];
    return (Slot.Occupied && Slot.Generation == Token.Generation) ? &Slot : nullptr;
}

} // namespace Frontier::Lifecycle
