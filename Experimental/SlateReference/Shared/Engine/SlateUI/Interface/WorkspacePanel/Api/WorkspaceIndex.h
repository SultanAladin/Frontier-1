//============================================================================================================================================
//                                                            WORKSPACEINDEX.H
//============================================================================================================================================
// 🧩 The workspaces one host has open, their subjects, their titles, and which of them is active.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateUI/Interface/WorkspacePanel/Api/WorkspacePanel.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ONE WORKSPACE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One open workspace, as the index holds it.
/// note  🔴 The title is composed ONCE, when the workspace is registered, into storage the entry owns. The
///        sheet titles a tab by subject and ordinal, and composing it per tick would compose the same run
///        sixty times a second into a buffer the recording is still reading.
/// tag   guarantee, nonallocating, nonthrowing
struct WorkspaceEntry
{
    WorkspaceSubject  Subject      = WorkspaceSubject::Vacant;   // [-] - what it is for
    std::uint32_t     SubjectIndex = 1u;                       // [-] - the nth of its subject; 1-based, as titled
    char              Titled[48]   = {};                         // [-] - composed at registration; never per tick
    bool              DockApplied   = false;                      // [-] - applied into the dock space once
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE INDEX
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The workspaces a host has open, and which one it is presenting.
/// note  🔴 This OWNS the workspaces; `WorkspacePanel` presents them and stores none of them, per `14` §1.
///        The two are separate for that reason alone — a panel holding this would be the home of what it
///        displays, which is the defect that section forbids by name.
/// note  ⚠️ Indexs are per subject and never reused. Closing `Canvas 2` and registering another yields
///        `Canvas 3`, because two tabs that had carried the same title within one session would make an
///        artist's description of what they were doing ambiguous.
/// tag   owning
class WorkspaceIndex
{
public:

    static constexpr std::uint32_t WorkspaceLimit = 32u;   // [-] - open at once; never allocated, never grown

    WorkspaceIndex()                                 = default;
    WorkspaceIndex(const WorkspaceIndex&)            = delete;
    WorkspaceIndex& operator=(const WorkspaceIndex&) = delete;
    ~WorkspaceIndex()                                = default;

    /// 🧩 Opens one workspace of the declared subject and makes it the active one.
    /// in    Subject  [-]  what the workspace is for; decides its title stem
    /// out   Result  [-]  the ordinal it was registered at; refuses with ExtentExhausted at the ceiling
    /// post  the registered workspace is active; its title is composed and will not be composed again
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<std::uint32_t> Register(WorkspaceSubject Subject);

    /// 🧩 Closes the workspace at one ordinal, preserving the order of the rest.
    /// out   Result  [-]  refuses with IdentityStale when the ordinal names no workspace
    /// note  ⚠️ The active ordinal moves to the preceding workspace, or to none when the last one closed.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Withdraw(std::uint32_t Index);

    /// 🧩 Makes the workspace at one ordinal the active one.
    /// out   Result  [-]  refuses with IdentityStale when the ordinal names no workspace
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Present(std::uint32_t Index);

    /// 🧩 How many workspaces stand open.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t OpenCount() const;

    /// 🧩 The title of the workspace at one ordinal, as storage the index owns.
    /// out   Titled  [-]  nullptr when the ordinal names no workspace
    /// note  🔴 This exists because `Current` delivers BY VALUE. Writing
    ///        `Current(n).Resolve().Titled` binds a pointer into a temporary that is destroyed at the
    ///        semicolon, so every title handed to the tab bar was a dangling read — which ImGui reported
    ///        as four visible items with conflicting IDs, the labels having decayed to the same garbage.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const char* Titled(std::uint32_t Index) const;

    /// 🧩 Whether the workspace at one ordinal has been applied into the dock space already.
    /// note  🔴 A workspace is docked on its FIRST tick only. Forcing the dock every tick would drag a
    ///        workspace the artist tore off straight back in, one frame after they moved it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Applied(std::uint32_t Index) const;

    /// 🧩 Records that the workspace at one ordinal has been applied.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Apply(std::uint32_t Index);

    /// 🧩 The workspace at one ordinal.
    /// out   Result  [-]  refuses with IdentityStale when the ordinal names no workspace
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<WorkspaceEntry> Current(std::uint32_t Index) const;

    /// 🧩 Which workspace is active, as an ordinal into the open set.
    /// out   Index  [-]  AbsentWorkspace when none is open
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t ActiveIndex() const;

    /// 🧩 The active workspace's title, or nullptr when none is open.
    /// use   Handed straight to `WorkspacePanel::Record`, which draws the vacant run for a null.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const char* ActiveTitle() const;

    static constexpr std::uint32_t AbsentWorkspace = 0xFFFFFFFFu;   // [-] - never an ordinal

    /// 🧩 Returns the index to its constructed condition.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Reset();

private:

    WorkspaceEntry  Open[WorkspaceLimit]                              = {};   // [-] - never allocated
    std::uint32_t   OpenOccupancy                                       = 0u;   // [-]
    std::uint32_t   Active                                              = AbsentWorkspace;   // [-]
    std::uint32_t   RegisteredPerSubject[
                        static_cast<std::uint32_t>(WorkspaceSubject::SubjectCount)] = {};    // [-] - ordinals registered
};

}   // namespace Slate
