/*============================================================================================================================================
                                                           REVISIONEXTENSION.CPP
============================================================================================================================================*/
// 🧩 Application-wide 750 MiB revision coordinator implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Revision/RevisionExtension.h"

#include <algorithm>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                             REVISION EXTENSION OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

RevisionExtension::RevisionExtension(std::size_t GlobalBudgetBytes) noexcept
    : GlobalBudgetCeilingBytes(ContentRegion::AlignUp64(
          GlobalBudgetBytes == 0u ? Constants::DefaultGlobalRevisionBudgetBytes : GlobalBudgetBytes))
{
}

bool RevisionExtension::Initialize() noexcept
{
    ActivePhase        = true;
    SuspendedPhase     = false;
    AccumulatedSeconds = 0.0;
    return true;
}

void RevisionExtension::Tick(double Δt) noexcept
{
    if (!ActivePhase || SuspendedPhase)
    {
        return;
    }
    AccumulatedSeconds += (Δt > 0.0 ? Δt : 0.0);
    if (QueryGlobalBytesUsed() > GlobalBudgetCeilingBytes)
    {
        (void)EnforceGlobalBudget(UINT32_MAX);
    }
}

void RevisionExtension::Suspend() noexcept
{
    if (ActivePhase)
    {
        SuspendedPhase = true;
    }
}

void RevisionExtension::Resume() noexcept
{
    if (ActivePhase)
    {
        SuspendedPhase = false;
    }
}

void RevisionExtension::Finalize() noexcept
{
    TrackerSlots.clear();
    GlobalTimeline.clear();
    ActivePhase    = false;
    SuspendedPhase = false;
}

void RevisionExtension::ConfigureGlobalBudget(std::size_t NewGlobalBudgetBytes) noexcept
{
    GlobalBudgetCeilingBytes = ContentRegion::AlignUp64(
        NewGlobalBudgetBytes == 0u ? Constants::ContainerBinaryAlignmentBytes : NewGlobalBudgetBytes);
    if (QueryGlobalBytesUsed() > GlobalBudgetCeilingBytes)
    {
        (void)EnforceGlobalBudget(UINT32_MAX);
    }
}

uint32_t RevisionExtension::RegisterDocumentTracker(uint32_t DocumentIdentifier, RevisionTracker* Tracker)
{
    if (Tracker == nullptr)
    {
        return UINT32_MAX;
    }

    for (std::size_t Index = 0u; Index < TrackerSlots.size(); ++Index)
    {
        if (TrackerSlots[Index].DocumentIdentifier == DocumentIdentifier)
        {
            TrackerSlots[Index].Tracker          = Tracker;
            TrackerSlots[Index].LastTouchedStamp = NextGlobalSequenceStamp;
            return static_cast<uint32_t>(Index);
        }
    }

    RegisteredTrackerSlot Slot{};
    Slot.DocumentIdentifier = DocumentIdentifier;
    Slot.Tracker            = Tracker;
    Slot.LastTouchedStamp   = NextGlobalSequenceStamp;
    TrackerSlots.push_back(Slot);
    return static_cast<uint32_t>(TrackerSlots.size() - 1u);
}

void RevisionExtension::UnregisterDocumentTracker(uint32_t DocumentIdentifier) noexcept
{
    TrackerSlots.erase(
        std::remove_if(
            TrackerSlots.begin(),
            TrackerSlots.end(),
            [DocumentIdentifier](const RegisteredTrackerSlot& Candidate) noexcept
            {
                return Candidate.DocumentIdentifier == DocumentIdentifier;
            }),
        TrackerSlots.end());
}

bool RevisionExtension::RecordGlobalOperation(
    uint32_t                             DocumentIdentifier,
    const ParametricOperationParameters& Parameters,
    std::span<const std::byte>           AuxiliaryPayload,
    GlobalCompactionSummary*             OutGlobalCompaction)
{
    RegisteredTrackerSlot* Slot = LocateSlot(DocumentIdentifier);
    if (Slot == nullptr || Slot->Tracker == nullptr)
    {
        return false;
    }

    const std::size_t EstimatedAddition =
        AuxiliaryPayload.empty() ? ContentRegion::AlignUp64(sizeof(ParametricOperationParameters))
                                 : ContentRegion::AlignUp64(AuxiliaryPayload.size());
    if (EstimatedAddition > GlobalBudgetCeilingBytes)
    {
        return false;
    }

    GlobalCompactionSummary Summary{};
    Summary.GlobalBytesBefore = QueryGlobalBytesUsed();
    if (Summary.GlobalBytesBefore + EstimatedAddition > GlobalBudgetCeilingBytes)
    {
        const std::size_t TargetPreRecordBytes = GlobalBudgetCeilingBytes - EstimatedAddition;
        while (QueryGlobalBytesUsed() > TargetPreRecordBytes)
        {
            const std::size_t BytesBeforeCycle = QueryGlobalBytesUsed();
            const GlobalCompactionSummary StepSummary = EnforceGlobalBudget(DocumentIdentifier);
            Summary.PurgedRedoStepsTotal   += StepSummary.PurgedRedoStepsTotal;
            Summary.FoldedPrefixStepsTotal += StepSummary.FoldedPrefixStepsTotal;
            if (QueryGlobalBytesUsed() >= BytesBeforeCycle)
            {
                break;
            }
        }
    }

    const uint64_t Stamp = NextGlobalSequenceStamp++;
    CompactionReport LocalCompaction{};
    if (!Slot->Tracker->RecordParametricOperation(Parameters, Stamp, AuxiliaryPayload, &LocalCompaction))
    {
        return false;
    }
    Summary.PurgedRedoStepsTotal   += LocalCompaction.PurgedRedoSteps;
    Summary.FoldedPrefixStepsTotal += LocalCompaction.FoldedPrefixSteps;
    Slot->LastTouchedStamp          = Stamp;

    // Recording a new edit truncates any previously undone tail in GlobalTimeline.
    GlobalTimeline.erase(
        std::remove_if(
            GlobalTimeline.begin(),
            GlobalTimeline.end(),
            [](const GlobalTimelineEntry& Entry) noexcept { return Entry.UndoneInTimeline; }),
        GlobalTimeline.end());

    GlobalTimelineEntry TimelineStep{};
    TimelineStep.SequenceStamp      = Stamp;
    TimelineStep.DocumentIdentifier = DocumentIdentifier;
    TimelineStep.UndoneInTimeline   = false;
    GlobalTimeline.push_back(TimelineStep);

    if (QueryGlobalBytesUsed() > GlobalBudgetCeilingBytes)
    {
        const GlobalCompactionSummary PostSummary = EnforceGlobalBudget(DocumentIdentifier);
        Summary.PurgedRedoStepsTotal   += PostSummary.PurgedRedoStepsTotal;
        Summary.FoldedPrefixStepsTotal += PostSummary.FoldedPrefixStepsTotal;
    }

    Summary.GlobalBytesAfter = QueryGlobalBytesUsed();
    if (OutGlobalCompaction != nullptr)
    {
        *OutGlobalCompaction = Summary;
    }
    return true;
}

bool RevisionExtension::UndoDocument(uint32_t DocumentIdentifier) noexcept
{
    RegisteredTrackerSlot* Slot = LocateSlot(DocumentIdentifier);
    if (Slot == nullptr || Slot->Tracker == nullptr)
    {
        return false;
    }
    return Slot->Tracker->Undo();
}

bool RevisionExtension::RedoDocument(uint32_t DocumentIdentifier, uint32_t PreferredBranchIndex) noexcept
{
    RegisteredTrackerSlot* Slot = LocateSlot(DocumentIdentifier);
    if (Slot == nullptr || Slot->Tracker == nullptr)
    {
        return false;
    }
    return Slot->Tracker->Redo(PreferredBranchIndex);
}

bool RevisionExtension::UndoGlobalLatest() noexcept
{
    for (auto Iterator = GlobalTimeline.rbegin(); Iterator != GlobalTimeline.rend(); ++Iterator)
    {
        if (!Iterator->UndoneInTimeline)
        {
            RegisteredTrackerSlot* Slot = LocateSlot(Iterator->DocumentIdentifier);
            if (Slot != nullptr && Slot->Tracker != nullptr && Slot->Tracker->Undo())
            {
                Iterator->UndoneInTimeline = true;
                return true;
            }
        }
    }
    return false;
}

bool RevisionExtension::RedoGlobalNext() noexcept
{
    for (GlobalTimelineEntry& Entry : GlobalTimeline)
    {
        if (Entry.UndoneInTimeline)
        {
            RegisteredTrackerSlot* Slot = LocateSlot(Entry.DocumentIdentifier);
            if (Slot != nullptr && Slot->Tracker != nullptr && Slot->Tracker->Redo())
            {
                Entry.UndoneInTimeline = false;
                return true;
            }
        }
    }
    return false;
}

GlobalCompactionSummary RevisionExtension::EnforceGlobalBudget(uint32_t ProtectedForegroundDocumentId)
{
    GlobalCompactionSummary Summary{};
    Summary.GlobalBytesBefore = QueryGlobalBytesUsed();
    if (Summary.GlobalBytesBefore <= GlobalBudgetCeilingBytes)
    {
        Summary.GlobalBytesAfter = Summary.GlobalBytesBefore;
        return Summary;
    }

    // Order trackers oldest-touched first, placing ProtectedForegroundDocumentId last.
    std::vector<RegisteredTrackerSlot*> Ordered;
    Ordered.reserve(TrackerSlots.size());
    for (RegisteredTrackerSlot& Slot : TrackerSlots)
    {
        if (Slot.Tracker != nullptr)
        {
            Ordered.push_back(&Slot);
        }
    }
    std::sort(
        Ordered.begin(),
        Ordered.end(),
        [ProtectedForegroundDocumentId](const RegisteredTrackerSlot* Left, const RegisteredTrackerSlot* Right) noexcept
        {
            const bool LeftProtected  = (Left->DocumentIdentifier == ProtectedForegroundDocumentId);
            const bool RightProtected = (Right->DocumentIdentifier == ProtectedForegroundDocumentId);
            if (LeftProtected != RightProtected)
            {
                return !LeftProtected;
            }
            return Left->LastTouchedStamp < Right->LastTouchedStamp;
        });

    // Step 1: Purge inactive Redo branches across LRU background trackers first, then foreground if needed.
    for (RegisteredTrackerSlot* Candidate : Ordered)
    {
        if (QueryGlobalBytesUsed() <= GlobalBudgetCeilingBytes)
        {
            break;
        }
        const CompactionReport PurgeReport = Candidate->Tracker->PurgeInactiveRedoBranches();
        Summary.PurgedRedoStepsTotal      += PurgeReport.PurgedRedoSteps;
    }

    // Step 2: Fold oldest prefix steps into parametric baselines across LRU trackers until <= GlobalBudgetCeilingBytes.
    while (QueryGlobalBytesUsed() > GlobalBudgetCeilingBytes)
    {
        bool MadeProgress = false;
        for (RegisteredTrackerSlot* Candidate : Ordered)
        {
            if (QueryGlobalBytesUsed() <= GlobalBudgetCeilingBytes)
            {
                break;
            }
            if (Candidate->Tracker->QueryUndoAvailable())
            {
                const CompactionReport FoldReport = Candidate->Tracker->FoldOldestPrefixSteps(1u);
                if (FoldReport.FoldedPrefixSteps > 0u)
                {
                    Summary.FoldedPrefixStepsTotal += FoldReport.FoldedPrefixSteps;
                    MadeProgress                    = true;
                }
            }
        }
        if (!MadeProgress)
        {
            break;
        }
    }

    Summary.GlobalBytesAfter = QueryGlobalBytesUsed();
    return Summary;
}

std::size_t RevisionExtension::QueryGlobalBytesUsed() const noexcept
{
    std::size_t TotalBytes = 0u;
    for (const RegisteredTrackerSlot& Slot : TrackerSlots)
    {
        if (Slot.Tracker != nullptr)
        {
            TotalBytes += Slot.Tracker->QueryBytesUsed();
        }
    }
    return TotalBytes;
}

uint32_t RevisionExtension::QueryRegisteredTrackerCount() const noexcept
{
    return static_cast<uint32_t>(TrackerSlots.size());
}

RevisionExtension::RegisteredTrackerSlot* RevisionExtension::LocateSlot(uint32_t DocumentIdentifier) noexcept
{
    for (RegisteredTrackerSlot& Slot : TrackerSlots)
    {
        if (Slot.DocumentIdentifier == DocumentIdentifier)
        {
            return &Slot;
        }
    }
    return nullptr;
}

} // namespace Frontier::Workspaces
