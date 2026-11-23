//============================================================================================================================================
//                                                           WORKSPACEINDEX.CPP
//============================================================================================================================================
// 🧩 Registration, withdrawal, the active ordinal, and the title composed exactly once per workspace.

#include "SlateUI/Interface/WorkspacePanel/Api/WorkspaceIndex.h"

#include <cstdio>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE SUBJECTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Defined beside the index and not beside the panel. The stem is a property of the SUBJECT, which
//    this component owns; the panel merely draws whatever title it is handed.
const char* WorkspaceStem(WorkspaceSubject Subject)
{
    switch (Subject)
    {
        case WorkspaceSubject::Texturing:   return "Canvas";
        case WorkspaceSubject::Modelling:  return "Sketch";
        case WorkspaceSubject::Parametric: return "Parametric";
        case WorkspaceSubject::Vacant:     return "Workspace";
        default:                           return "Workspace";
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE ENROLMENT
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> WorkspaceIndex::Register(WorkspaceSubject Subject)
{
    if (OpenOccupancy >= WorkspaceLimit)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ExtentExhausted, "no more workspaces may be opened at once" });
    }

    const std::uint32_t SubjectIndex = static_cast<std::uint32_t>(Subject);

    if (SubjectIndex >= static_cast<std::uint32_t>(WorkspaceSubject::SubjectCount))
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "no such workspace subject" });

    WorkspaceEntry& Registered = Open[OpenOccupancy];

    Registered.Subject = Subject;

    // 🔴 Never reused. Closing the second canvas and opening another yields a third, because two tabs that
    //    had carried one title within a session make an artist's account of what they were doing ambiguous.
    Registered.SubjectIndex = ++RegisteredPerSubject[SubjectIndex];

    // 🔴 Composed HERE and never again. The sheet titles a tab by stem and ordinal, and composing that per
    //    tick would write into storage the recording is still reading, sixty times a second.
    // 📝 The truncation is not checked: the stems are three known literals and the ordinal is bounded by
    //    the ceiling, so the longest run this can compose is well inside the extent.
    std::snprintf(Registered.Titled, sizeof(Registered.Titled), "%s %u",
                  WorkspaceStem(Subject), Registered.SubjectIndex);

    Active = OpenOccupancy;
    ++OpenOccupancy;

    return Deliver<std::uint32_t>::Result(Active);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE WITHDRAWAL
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> WorkspaceIndex::Withdraw(std::uint32_t Index)
{
    if (Index >= OpenOccupancy)
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "that ordinal names no open workspace" });

    // 📝 The order is preserved rather than the last entry being swapped in. The sheet presents its tabs in
    //    the order they were opened, and a swap would move an unrelated tab under the artist's pointer.
    for (std::uint32_t Moving = Index; Moving + 1u < OpenOccupancy; ++Moving)
        Open[Moving] = Open[Moving + 1u];

    --OpenOccupancy;
    Open[OpenOccupancy] = WorkspaceEntry{};

    // ⚠️ The active ordinal follows the withdrawal rather than staying where it was. Left alone it would
    //    name whichever workspace slid into the closed one's place, which is not a choice anybody made.
    if (OpenOccupancy == 0u)
    {
        Active = AbsentWorkspace;
    }
    else if (Active > Index || Active >= OpenOccupancy)
    {
        Active = (Active == 0u) ? 0u : Active - 1u;
    }

    return Deliver<bool>::Result(true);
}

Deliver<bool> WorkspaceIndex::Present(std::uint32_t Index)
{
    if (Index >= OpenOccupancy)
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "that ordinal names no open workspace" });

    Active = Index;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE READINGS
//------------------------------------------------------------------------------------------------------------------------

std::uint32_t WorkspaceIndex::OpenCount() const
{
    return OpenOccupancy;
}

Deliver<WorkspaceEntry> WorkspaceIndex::Current(std::uint32_t Index) const
{
    if (Index >= OpenOccupancy)
    {
        return Deliver<WorkspaceEntry>::Refuse(
            { RefusalReason::IdentityStale, "that ordinal names no open workspace" });
    }

    return Deliver<WorkspaceEntry>::Result(Open[Index]);
}

bool WorkspaceIndex::Applied(std::uint32_t Index) const
{
    return (Index < OpenOccupancy) && Open[Index].DockApplied;
}

void WorkspaceIndex::Apply(std::uint32_t Index)
{
    if (Index < OpenOccupancy)
        Open[Index].DockApplied = true;
}

const char* WorkspaceIndex::Titled(std::uint32_t Index) const
{
    if (Index >= OpenOccupancy)
        return nullptr;

    // 📝 Points into the index's own storage, which outlives the tick. The delivered form cannot: it
    //    copies the entry, and a pointer taken from that copy dies with the temporary.
    return Open[Index].Titled;
}

std::uint32_t WorkspaceIndex::ActiveIndex() const
{
    return Active;
}

const char* WorkspaceIndex::ActiveTitle() const
{
    if (Active >= OpenOccupancy)
        return nullptr;

    return Open[Active].Titled;
}

void WorkspaceIndex::Reset()
{
    for (std::uint32_t Index = 0u; Index < WorkspaceLimit; ++Index)
        Open[Index] = WorkspaceEntry{};

    OpenOccupancy = 0u;
    Active        = AbsentWorkspace;

    for (std::uint32_t Subject = 0u;
         Subject < static_cast<std::uint32_t>(WorkspaceSubject::SubjectCount);
         ++Subject)
    {
        RegisteredPerSubject[Subject] = 0u;
    }
}

}   // namespace Slate
