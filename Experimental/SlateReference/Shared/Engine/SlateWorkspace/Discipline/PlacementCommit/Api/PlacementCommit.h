//============================================================================================================================================
//                                                        PLACEMENTCOMMIT.H
//============================================================================================================================================
// 🧩 Turning a sealed placement into declared geometry, a named record and one revision.
//
// 🔴 A SEALED PLACEMENT IS ANSWERED BY EXACTLY ONE ARM, CHOSEN BY SUBJECT **AND** METHOD TOGETHER.
//
//    The host chose by walking a twenty-arm `if` chain in source order, and most arms tested only the
//    subject. An arm written for `Circle` with no method test therefore caught a CENTRED circle before
//    the chain ever reached the arm meant for it, and the artist got the wrong shape with no refusal and
//    no message. Nine of the twenty-seven accepted combinations reached an arm not written for them:
//
//        Arc Centred, Arc Tangent, Arc ThreePoint   -> the bare Arc arm, built as a 3-point arc
//        Circle Centred                             -> the bare Circle arm, built as centre-and-radius
//        Ellipse Centred, Ellipse Diameter          -> the bare Ellipse arm
//        EllipticalArc Centred, ~ ThreePoint        -> the bare EllipticalArc arm
//        Polygon Centred                            -> the bare Polygon arm
//
//    Worse than a wrong answer, the arm written for a centred arc was UNREACHABLE — 24 lines of dead
//    code that looked like a working feature. `Tangent` had no implementation reachable at all.
//
//    The dispatch is now a table keyed on both axes. An unlisted pair is refused with a sentence naming
//    it rather than falling into whichever arm happens to sit lowest in a chain.
//
// 📝 WHY THIS BELONGS IN A UNIT. Declaring a shape is arithmetic on anchors — no device, no window, no
//    frame. The host called it in one place and it never touched anything the host owns.

#pragma once

#include "SketchToolset/SketchTool/SketchPlacement/Api/SketchPlacement.h"
#include "SlateShape/Record/WorkspaceNameIndex/Api/WorkspaceNameIndex.h"
#include "SlateShape/Record/WorkspaceRecordStructure/Api/WorkspaceRecordStructure.h"
#include "SlateShape/Record/WorkspaceRevisionSequence/Api/WorkspaceRevisionSequence.h"
#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"
#include "SlateWorkspace/Discipline/RecordDeclaration/Api/RecordDeclaration.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/SketchBasis.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"

namespace Slate
{

/// 🧩 Collects everything one placement writes, and seals it as a single revision.
///
/// 🔴 ONE DRAWN THING IS ONE UNDO. A polyline of five anchors declared four coincidence constraints and
///    sealed a revision for each, so the artist pressed undo five times to remove one drawn line — and
///    the first four presses removed constraints while the geometry sat there unchanged, which looks
///    exactly like undo being broken. A circle sealed two: one for the profile, one for the radius
///    dimension it declares for itself.
///
/// 📝 It carries the same `Seal` shape as `WorkspaceRevisionSequence` on purpose, so the declaring arms
///    are written the way they always were and simply hand their work to this instead. What changes is
///    who decides when a revision closes: the placement, not each individual write.
class PlacementJournal
{
public:
    explicit PlacementJournal(WorkspaceRevisionSequence& Sealing) : Revisions(Sealing) {}

    /// 🧩 Records a write. Nothing is sealed yet.
    /// note ⚠️ The FIRST description wins. A rectangle is "Declared Rectangle_1", not the name of the
    ///       last constraint that happened to be written while declaring it.
    WorkspaceRevisionName Seal(const std::string& Description,
                               const std::string& Operation,
                               const std::vector<WorkspaceRecordName>& Affected,
                               std::uint64_t)
    {
        if (Described.empty())
        {
            Described = Description;
            Operated  = Operation;
        }
        for (WorkspaceRecordName Name : Affected)
            Written.push_back(Name);
        return {};
    }

    /// 🧩 What the underlying history already holds, so an arm reading it sees the truth.
    std::uint32_t DeclaredCount() const { return Revisions.DeclaredCount(); }

    /// 🧩 Closes the placement as exactly one revision naming every record it touched.
    /// out   Named  [-]  unassigned when the placement wrote nothing
    WorkspaceRevisionName Close()
    {
        if (Written.empty())
            return {};
        return Revisions.Seal(Described, Operated, Written, Revisions.DeclaredCount() + 1u);
    }

private:
    WorkspaceRevisionSequence&       Revisions;
    std::string                      Described = {};
    std::string                      Operated  = {};
    std::vector<WorkspaceRecordName> Written   = {};
};

/// 🧩 Declares the geometry a completed placement describes.
///
/// out   Named  [-]  the record the artist should now see selected, or a refusal saying why not
///
/// note 🔴 Chosen by SUBJECT AND METHOD TOGETHER. A pair the catalogue accepts but this unit has no arm
///       for is refused by name — silence would mean drawing something the artist did not ask for.
///
/// note ⚠️ Seals ONE revision per placement, even when the shape declares many curves. A rectangle is a
///       single thing the artist drew and a single press of undo must remove all four of its sides; four
///       revisions would make them undo it four times.
///
/// note 📝 Auto-constraints follow the geometry within the same revision. A line drawn from an existing
///       endpoint is coincident with it, and that relationship is part of drawing the line rather than a
///       separate act to be undone separately.
Deliver<WorkspaceRecordName> CommitPlacement(WorkspaceNameIndex& Naming,
                                             SketchStructure& Sketch,
                                             WorkspaceRecordStructure& Records,
                                             WorkspaceRevisionSequence& Revisions,
                                             const SealedPlacement& Placed);

/// 🧩 World-native callers supply the active authoring plane explicitly. This keeps the compatibility
/// sketch's remembered plane out of the placement decision while retaining the legacy overload above for
/// callers that are intentionally operating on the sketch document.
Deliver<WorkspaceRecordName> CommitPlacement(WorkspaceNameIndex& Naming,
                                             SketchStructure& Sketch,
                                             const SketchPlane& ActivePlane,
                                             WorkspaceRecordStructure& Records,
                                             WorkspaceRevisionSequence& Revisions,
                                             const SealedPlacement& Placed);

/// 🧩 Whether this unit can declare a given pair, without declaring anything.
/// note 📝 Lets the toolset refuse a combination at the point the artist picks it, rather than after they
///       have placed every anchor. `AcceptedBy` says the catalogue allows the pair; this says the
///       geometry for it exists.
bool CommitSupported(SketchSubject Subject, PlacementMethod Method);

/// 🧩 Whether three anchors can describe an arc at all.
///
/// note 📝 Three points that are coincident or collinear have no circumcentre, so an arc through them is
///       not a curve. Callers ask before drawing a preview as well as before committing.
bool ArcReady(const SpatialPoint& StartPoint,
              const SpatialPoint& ThroughPoint,
              const SpatialPoint& EndPoint);

/// 🧩 Declares a constraint, records it, and notes it in the revision being assembled.
///
/// note ⚠️ Writes through a PlacementJournal so the constraint joins the revision of whatever is being
///       drawn. A constraint applied on its own opens a journal of its own and closes it immediately.
void SealConstraintRecord(WorkspaceNameIndex& Naming,
                          WorkspaceRecordStructure& Records,
                          PlacementJournal& Revisions,
                          SketchStructure& Sketch,
                          const ConstraintSpecification& Constraint,
                          std::vector<WorkspaceRecordName>& Written);

/// 🧩 Apply one relationship the artist asked for, as one thing they can undo.
/// in    Constraint  [-]  the relationship, already checked against the selection it came from
/// out   -           [-]  the record written, or a refusal when the constraint wrote nothing
/// note  📝 A constraint applied on its own opens a journal and closes it immediately, so it lands in the
///        history as one entry and one press of undo — the same guarantee a drawn shape gets.
/// note  ⚠️ Whether the SELECTION supports the relationship is `ConstraintAuthoring`'s question, answered
///        before this is called. This one commits what has already been declared.
/// cost  🚩
/// tag   api, allocating
Deliver<WorkspaceRecordName> CommitConstraint(WorkspaceNameIndex& Naming,
                                              SketchStructure& Sketch,
                                              WorkspaceRecordStructure& Records,
                                              WorkspaceRevisionSequence& Revisions,
                                              const ConstraintSpecification& Constraint);

}   // namespace Slate
