//============================================================================================================================================
//                                                      WORKSPACEDECLARATION.H
//============================================================================================================================================
// 🧩 What a discipline's workspace IS — which panels it seats, how they are arranged, which tools it offers.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SketchToolset/SketchTool/SketchPlacement/Api/SketchPlacement.h"
#include "SlateUI/Interface/PanelStructure/Api/PanelStructure.h"
#include "SlateUI/Interface/WorkspacePanel/Api/WorkspacePanel.h"
#include "TextureToolset/TextureTool/TextureToolDeclaration/Api/TextureToolDeclaration.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE ARRANGEMENT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One step in building a panel arrangement: divide a slot, or seat a panel in one.
/// note  🔴 The arrangement is DATA, not a procedure. It was a procedure — `ConstructParametricLayout` ran
///        eleven statements against `PanelStructure`, each checking a `Deliver` and returning early on
///        refusal, and the only way to learn what the parametric discipline seats was to read all eleven.
///        A workspace stated as a list can be read, compared, validated and presented; a workspace stated
///        as a function can only be run.
/// note  📝 `Divide` names the slot it splits and which side stays vacant for the next step; `Seat` names
///        the slot and what presents there. Slot ordinals are `PanelStructure`'s own, so a declaration
///        addresses the same slots the artist's later edits do.
/// tag   guarantee, nonallocating, nonthrowing
struct ArrangementStep
{
    /// 🧩 What this step does to the partition.
    enum class Action : std::uint32_t
    {
        Seat        = 0u,   // [-] - assign `Subject` to the leaf at `Slot`
        Divide      = 1u,   // [-] - split `Slot` along `Axis`, leaving `Side` vacant
        Proportion  = 2u,   // [-] - set the least-side fraction of the division at `Slot`
        ActionCount = 3u    // [-] - the closed count, never an action
    };

    Action             Applied  = Action::Seat;                  // [-]
    std::uint32_t      Slot     = PanelStructure::RootIndex;     // [-] - a PanelStructure ordinal
    PanelSubject       Subject  = PanelSubject::Viewport;        // [-] - `Seat` only
    PanelDivisionAxis  Axis     = PanelDivisionAxis::X;          // [-] - `Divide` only
    PanelDivisionSide  Side     = PanelDivisionSide::Minimum;    // [-] - `Divide` only
    float              Fraction = 0.5f;                          // [-] - `Proportion` only
};

/// 🧩 Everything one discipline's workspace declares.
/// note  🔴 `StepLimit` is `PanelStructure::RecordLimit`, not a number chosen here. A partition holds eleven
///        records, so an arrangement can never need more steps than that many seats plus the divisions
///        between them; sizing against the structure's own limit means the two cannot drift apart.
/// note  📝 Fixed extent, no allocation. A workspace declaration is read during bring-up and never grows,
///        so a vector would allocate once per product for content known at compile time.
/// tag   guarantee, nonallocating, nonthrowing
struct WorkspaceDeclaration
{
    static constexpr std::uint32_t StepLimit = PanelStructure::RecordLimit * 2u;

    const char*       Naming        = "";                          // [-] - static text; what the artist calls it
    WorkspaceSubject  Subject       = WorkspaceSubject::Vacant;    // [-] - which discipline this is
    PanelSubject      Initial       = PanelSubject::Viewport;      // [-] - the single leaf before any step
    ArrangementStep   Steps[StepLimit] = {};                       // [-] - applied in order
    std::uint32_t     StepCount     = 0u;                          // [-] - how many of `Steps` are declared

    /// 🧩 Whether this discipline offers sketch tools, and whether it offers texturing tools.
    /// note  🔴 The two are independent, not exclusive. A combined workspace offers both, which is what
    ///        makes the third product one line of data rather than a third arrangement procedure.
    bool              SketchTools   = false;                       // [-]
    bool              TextureTools  = false;                       // [-]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE DISCIPLINES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The texturing workspace: one viewport, one layer stack beside it.
/// note  📝 Transcribed from the texturing host, which seated a bare viewport and left the artist to open
///        the layer stack. Seating it up front is the one behavioural change, and it is the arrangement the
///        host's own default configuration already described.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline WorkspaceDeclaration DeclaredTextureWorkspace()
{
    WorkspaceDeclaration Declared = {};
    Declared.Naming       = "Texturing";
    Declared.Subject      = WorkspaceSubject::Texturing;
    Declared.Initial      = PanelSubject::Viewport;
    Declared.TextureTools = true;

    // 🔴 `Divide` leaves the vacant side at `Minimum`, so the viewport that was the root moves to
    //    `Maximum`. Seating the layer stack at `Minimum` and re-seating the viewport at `Maximum` is what
    //    the host's procedure did, and getting the two the wrong way round is why this is declared rather
    //    than repeated.
    Declared.Steps[0] = { ArrangementStep::Action::Divide, PanelStructure::RootIndex,
                          PanelSubject::Viewport, PanelDivisionAxis::X, PanelDivisionSide::Maximum, 0.5f };
    Declared.Steps[1] = { ArrangementStep::Action::Seat, 1u,
                          PanelSubject::Viewport, PanelDivisionAxis::X, PanelDivisionSide::Minimum, 0.5f };
    Declared.Steps[2] = { ArrangementStep::Action::Seat, 2u,
                          PanelSubject::Texturing, PanelDivisionAxis::X, PanelDivisionSide::Minimum, 0.5f };
    Declared.Steps[3] = { ArrangementStep::Action::Proportion, PanelStructure::RootIndex,
                          PanelSubject::Viewport, PanelDivisionAxis::X, PanelDivisionSide::Minimum, 0.78f };
    Declared.StepCount = 4u;
    return Declared;
}

/// 🧩 The sketching workspace: a directory, a tool catalogue, and a viewport.
/// note  🔴 Transcribed step for step from `ConstructParametricLayout`, including its 0.27 and 0.33
///        proportions. Those two numbers were the only statement of the parametric discipline's shape that
///        existed, and they lived in a host that step 11 deletes.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline WorkspaceDeclaration DeclaredSketchWorkspace()
{
    WorkspaceDeclaration Declared = {};
    Declared.Naming      = "Sketching";
    Declared.Subject     = WorkspaceSubject::Parametric;
    Declared.Initial     = PanelSubject::Viewport;
    Declared.SketchTools = true;

    Declared.Steps[0] = { ArrangementStep::Action::Divide, PanelStructure::RootIndex,
                          PanelSubject::Viewport, PanelDivisionAxis::X, PanelDivisionSide::Minimum, 0.5f };
    Declared.Steps[1] = { ArrangementStep::Action::Seat, 1u,
                          PanelSubject::SketchDirectory, PanelDivisionAxis::X, PanelDivisionSide::Minimum, 0.5f };
    Declared.Steps[2] = { ArrangementStep::Action::Divide, 2u,
                          PanelSubject::Viewport, PanelDivisionAxis::X, PanelDivisionSide::Minimum, 0.5f };
    Declared.Steps[3] = { ArrangementStep::Action::Seat, 3u,
                          PanelSubject::ParametricTools, PanelDivisionAxis::X, PanelDivisionSide::Minimum, 0.5f };
    Declared.Steps[4] = { ArrangementStep::Action::Seat, 4u,
                          PanelSubject::Viewport, PanelDivisionAxis::X, PanelDivisionSide::Minimum, 0.5f };
    Declared.Steps[5] = { ArrangementStep::Action::Proportion, PanelStructure::RootIndex,
                          PanelSubject::Viewport, PanelDivisionAxis::X, PanelDivisionSide::Minimum, 0.27f };
    Declared.Steps[6] = { ArrangementStep::Action::Proportion, 2u,
                          PanelSubject::Viewport, PanelDivisionAxis::X, PanelDivisionSide::Minimum, 0.33f };
    Declared.StepCount = 7u;
    return Declared;
}

/// 🧩 The blank workspace: one viewport and nothing else.
/// note  📝 This is what makes "blank editor" free, as §2.1 of the plan states. A product that offers no
///        discipline offers this, and it costs one declaration rather than a code path.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline WorkspaceDeclaration DeclaredVacantWorkspace()
{
    WorkspaceDeclaration Declared = {};
    Declared.Naming  = "Workspace";
    Declared.Subject = WorkspaceSubject::Vacant;
    Declared.Initial = PanelSubject::Viewport;
    return Declared;
}

/// 🧩 The combined workspace: the sketching arrangement, offering both toolsets.
/// note  🔴 The third product at zero additional arrangement code — the plan's §3 claim, realised. It is
///        the sketching arrangement with `TextureTools` also set, because a combined build's difference
///        from a sketching build is which tools it offers, not where its panels sit.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline WorkspaceDeclaration DeclaredCombinedWorkspace()
{
    WorkspaceDeclaration Declared = DeclaredSketchWorkspace();
    Declared.Naming       = "Authoring";
    Declared.TextureTools = true;
    return Declared;
}

/// 🧩 The workspace a discipline declares.
/// in    Subject   [-]  the discipline a workspace was registered under
/// out   Declared  [-]  that discipline's arrangement and toolsets
/// note  🔴 Registering a workspace under a subject and then building its panels are two statements that
///        must agree, and every host previously made them separately — `Register(Parametric)` on one line
///        and a hand-built partition on another, with nothing tying the two together. A host that
///        registered one discipline and seated another's panels would compile and run. This is the join:
///        the subject alone now decides the arrangement.
/// note  ⚠️ `Texturing` is the retired spelling still carried by `WorkspaceSubject`; it is swept with the
///        other 527 uses in its own step. The declaration it selects is already named `Texturing`.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline WorkspaceDeclaration DeclaredWorkspaceFor(WorkspaceSubject Subject)
{
    switch (Subject)
    {
        case WorkspaceSubject::Texturing:   return DeclaredTextureWorkspace();
        case WorkspaceSubject::Parametric: return DeclaredSketchWorkspace();

        // 📝 The modelling discipline has no arrangement of its own yet, so it opens on a bare viewport
        //    rather than borrowing another discipline's panels.
        case WorkspaceSubject::Modelling:
        case WorkspaceSubject::Vacant:
        case WorkspaceSubject::SubjectCount:
            break;
    }
    return DeclaredVacantWorkspace();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     APPLYING ONE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Builds the declared arrangement into a partition.
/// in    Declared   [-]  the discipline's workspace
/// out   Partition  [-]  returned to `Initial`, then each step applied in order
/// out   Applied    [-]  refuses on the first step the partition rejects, naming which
/// note  🔴 This is the ONLY function in the unit that touches a partition, and it is twenty lines. Every
///        host previously carried its own copy of this loop unrolled into straight-line code with a
///        `Deliver` check between each pair of statements.
/// note  ⚠️ A refusal leaves the partition part-built rather than restored. That is deliberate: the caller
///        sees exactly how far the arrangement got, which is what makes a bad declaration diagnosable
///        rather than silently collapsing to a bare viewport.
/// cost  🚩
/// tag   api, nonallocating, nonthrowing
Deliver<bool> ApplyWorkspace(const WorkspaceDeclaration& Declared, PanelStructure& Partition);

}   // namespace Slate
