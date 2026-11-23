//============================================================================================================================================
//                                                            WORKSPACEPANEL.H
//============================================================================================================================================
// 🧩 The workspace surface the artist works inside — its tab strip, its body, and its footer, per `DockWorkspace.html`.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT A WORKSPACE IS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one workspace is for, which decides what it is called and what a host opens by default.
/// note  🔴 The subject is the workspace's own, not the host's. `32` §3 ships one texturing host, and an
///        editor presents every subject at once — so a host that inferred the subject from itself could
///        not carry two kinds of workspace, which is the whole reason the editor exists.
/// tag   guarantee
enum class WorkspaceSubject : std::uint32_t
{
    Vacant       = 0u,   // [-] - opened blank; the editor's default
    Texturing     = 1u,   // [-] - a texture surface; the texturing host's default
    Modelling    = 2u,   // [-] - a sketch or solid workspace
    Parametric   = 3u,   // [-] - a dedicated CAD workspace
    SubjectCount = 4u    // [-] - the closed count, never a subject
};

/// 🧩 The run one workspace of a given subject is titled with, before its ordinal is appended.
/// note  📝 Declared here beside the subject and defined beside the index that owns it. Two hosts opening
///        one subject then title it identically; `DockWorkspace.html` titles a tab by stem and ordinal.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
const char* WorkspaceStem(WorkspaceSubject Subject);

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE PANEL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Presents the workspaces a host has open, as `DockWorkspace.html` draws them.
/// note  🔴 `14` §1: this panel presents state owned elsewhere and stores none of it. What it holds is a
///        borrowed index and the extents it drew last, which is presentation and not content — the
///        workspaces themselves belong to `WorkspaceIndex`.
/// note  ⚠️ The tab strip's TRAPEZOIDAL geometry is the vendor's, patched per `Patches/`. What this records
///        is the strip ground, the body, the footer and the vacant placeholder — the parts of the sheet the
///        vendor's tab bar does not draw.
/// tag   owning
class WorkspacePanel
{
public:

    WorkspacePanel()                                 = default;
    WorkspacePanel(const WorkspacePanel&)            = delete;
    WorkspacePanel& operator=(const WorkspacePanel&) = delete;
    ~WorkspacePanel()                                = default;

    /// 🧩 Borrows the surface every extent is recorded into and the appearance it is measured against.
    /// in    Recording   [-]  borrowed; outlives this panel
    /// in    Appearance  [-]  borrowed; outlives this panel
    /// out   Result     [-]  refuses with ContentUnsupported when a construction already stands
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> ConstructWorkspacePanel(RecordingSurface& Recording, const ThemeProfile& Appearance);

    /// 🧩 Records one workspace panel — strip ground, body, footer, and the vacant run when it carries none.
    /// in    Extent      [px]  the whole panel, strip and footer included
    /// in    Titled      [-]   the active workspace's title, or nullptr when the panel carries none
    /// out   Result     [-]   refuses with CapabilityAbsent before Construct
    /// note  🔴 The body is recorded BEFORE the footer and the footer draws its edge on top. The sheet gives
    ///        `.panelfooter` a `border-top` and `z-index: 2`, so a body that textured over it would lose the
    ///        one line separating the workspace from the strip below it.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Record(const PlaneExtent& Extent, const char* Titled);

    /// 🧩 The body extent the last `Record` left, for a caller drawing content inside the workspace.
    /// note  ⚠️ Valid only until the next `Record`. It is the strip and footer subtracted from the extent
    ///        that was passed, and it is zero before the first one.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    PlaneExtent Body() const;

    /// 🧩 The strip extent the last `Record` left, for the caller applying the vendor's tab bar on it.
    /// note  ⚠️ Valid only until the next `Record`, and zero before the first.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    PlaneExtent Strip() const;

    /// 🧩 Returns the panel to its constructed condition.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Reset();

private:

    RecordingSurface*              Surface     = nullptr;   // [-] - borrowed; never owned
    const ThemeProfile* Appearance  = nullptr;   // [-] - borrowed; never owned
    PlaneExtent                    BodyExtent  = {};        // [px] - what the last Record left
    PlaneExtent                    StripExtent = {};        // [px] - where the vendor's tab bar is applied
};

}   // namespace Slate
