//============================================================================================================================================
//                                                        ANNOTATIONSESSION.H
//============================================================================================================================================
// 🧩 The gestures that place dimensions and apply constraints: pick what to measure, drag it off to the
//    side, type an exact figure.
//
// 🔴 A DIMENSION IS PLACED IN ONE DRAG AND EDITED FOREVER AFTER. Picking the geometry declares it at its
//    measured value, so it always starts out true; the drag only decides where it sits. That ordering
//    matters -- a dimension that appeared with a made-up value would move the drawing the instant it was
//    created, which is the opposite of what an artist expects from an annotation.
//
// 🔴 TYPING A VALUE GOES THROUGH THE SOLVER, NEVER STRAIGHT INTO THE GEOMETRY. Writing the parameter
//    directly is simpler and always "succeeds", but it cascades unpredictably -- edit one edge of a
//    triangle and the neighbouring edge silently changes length too -- and it cannot tell you when the
//    sketch has been over-constrained. The solver can refuse. A refusal that leaves the drawing alone is
//    worth far more than a write that quietly produces something nobody asked for.
//
// 🔴 CONSTRAINTS COMMIT ON THE LAST PICK, NOT ON A BUTTON. A constraint has no figure, so it needs no
//    readout and no Apply -- it needs exactly as many picks as its subject demands, and it applies the
//    moment it has them.
//
// 📝 Pure, like every other session here: picks and pointer positions in, intent out. No ImGui, no
//    device, so the whole thing is provable headlessly.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/MeasureDisplay.h"
#include "SlateShape/World/WorldSketchDimensionGeometry/Api/WorldSketchDimensionGeometry.h"
#include "SlateShape/World/WorldSketchPicking/Api/WorldSketchPicking.h"
#include "SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       WHAT IS BEING DONE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How far along placing a dimension the artist is.
enum class AnnotationPhase : std::uint32_t
{
    Idle      = 0u,   // [-] - nothing picked
    Gathering = 1u,   // [-] - some picks taken, more needed
    Placing   = 2u,   // [-] - enough picked; the drag is choosing where it sits
    Editing   = 3u,   // [-] - placed, and the figure is open for typing
    Applied   = 4u    // [-] - committed this frame
};

/// 🧩 Why an annotation could not be made.
/// note  📝 Told apart so the caller can say something useful. "It did not work" is not a message.
enum class AnnotationVerdict : std::uint32_t
{
    Produced          = 0u,
    NeedsMorePicks    = 1u,
    PicksUnsuitable   = 2u,   // [-] - e.g. a radius asked of a straight line
    GeometryAbsent    = 3u,
    ValueNotPositive  = 4u,
    SolverRefused     = 5u,   // [-] - the sketch cannot take that value; nothing was changed
    NothingToApply    = 6u,

    /// 🧩 The value was taken, and constraints were withdrawn to make room for it.
    /// note  🔴 A SUCCESS, NOT A WARNING, and it is separate from `Produced` only so the artist can be
    ///        TOLD. A dimension silently dissolving a Parallel the artist set up an hour ago is the kind
    ///        of helpfulness that destroys trust in a modeller; the same act, reported, is a tool doing
    ///        what it was asked.
    ProducedByRetiringConstraints = 7u
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE SESSION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How many picks a dimension subject needs before it can be placed.
/// note  🔴 THE ONE PLACE THIS IS DECIDED. An aligned dimension takes either two points or one whole
///        curve, so "how many picks" is a question about what has been picked so far, not a constant per
///        subject -- which is why this takes the first pick as well as the subject.
std::uint32_t DimensionPicksNeeded(WorldDimensionSubject Subject, const WorldPick& First);

/// 🧩 How many picks a constraint subject needs.
std::uint32_t ConstraintPicksNeeded(WorldConstraintSubject Subject);

/// 🧩 Everything a dimension or constraint gesture remembers between frames.
struct AnnotationSession
{
    AnnotationPhase Phase = AnnotationPhase::Idle;

    /// 🧩 Whether this gesture is placing a dimension or applying a constraint.
    bool Constraining = false;

    WorldDimensionSubject  Dimension  = WorldDimensionSubject::Aligned;
    WorldConstraintSubject Constraint = WorldConstraintSubject::Coincident;

    WorldPick     First  = {};
    WorldPick     Second = {};
    std::uint32_t Taken  = 0u;    // [-] - how many picks have been gathered

    /// 🧩 The dimension once it has been declared, so the drag can move it.
    WorldDimensionName Placed = {};

    /// 🧩 What the figure currently reads, in MILLIMETRES.
    /// note  🔴 ALWAYS MILLIMETRES HERE. The display unit is a property of the panel showing it, not of
    ///        the measurement, and letting a unit into the session is how a drawing gets silently
    ///        rescaled the first time somebody switches to metres.
    double Figure = 0.0;

    /// 🧩 Whether the figure has been typed rather than merely measured.
    /// note  📝 A dimension that is only measuring is not driving anything, so applying it must not
    ///        disturb geometry that is already correct.
    bool Driving = false;

    /// 🧩 The constraints withdrawn by the last apply, so the artist can be told what was given up.
    /// note  🔴 KEPT SO IT CAN BE REPORTED. Retiring a constraint the artist set up earlier is a real
    ///        loss, and a modeller that does it silently is one nobody can trust with a drawing they
    ///        care about. Cleared at the start of every apply, so it never describes an older edit.
    std::vector<WorldConstraintName> RetiredConstraints = {};

    bool ReadoutStanding() const
    {
        return Phase == AnnotationPhase::Placing || Phase == AnnotationPhase::Editing;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         THE GESTURE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Offers a pick to the gesture, declaring the dimension once enough have been gathered.
/// note  🔴 THE DIMENSION IS DECLARED AT ITS MEASURED VALUE, so it is true the moment it appears and the
///        drawing does not move. Only a typed figure ever drives geometry.
/// cost  🚩🚩
/// tag   api, nonthrowing
AnnotationVerdict OfferAnnotationPick(WorldSketchStructure& Declared,
                                      const WorldPick& Offered,
                                      AnnotationSession& Session);

/// 🧩 Takes hold of a dimension that was placed earlier, so it can be dragged somewhere else.
/// out   Session  [-] re-enters `Placing` on the named dimension
/// note  🔴 DECLARES NOTHING. The dimension already exists; this only points the gesture at it. Once a
///        dimension has been committed there is otherwise no way back to it, and an annotation that can
///        never be moved again is one the artist has to delete and redo to tidy up a drawing.
/// note  📝 Refuses an unassigned or already-deleted name, so a stale hit-test result is harmless.
/// tag   api, nonthrowing
bool GraspDeclaredDimension(const WorldSketchStructure& Declared,
                            WorldDimensionName Subject,
                            AnnotationSession& Session);

/// 🧩 Moves a placed dimension to wherever the pointer is, in one call.
/// note  📝 Writes `Offset` and, for round dimensions, `Angle`. Both are placement; neither touches the
///        geometry being measured, so dragging a dimension can never alter the drawing.
/// tag   api, nonthrowing
void DragAnnotationTo(WorldSketchStructure& Declared,
                      const SpatialPoint& Probe,
                      AnnotationSession& Session);

/// 🧩 Accepts a figure the artist typed, in millimetres, marking the dimension as driving.
/// note  ⚠️ Records the wish; it does NOT solve. `ApplyAnnotation` does that, so a half-typed number
///        cannot move the drawing on every keystroke.
/// tag   api, nonthrowing
AnnotationVerdict DeclareAnnotationFigure(AnnotationSession& Session, double Millimetres);

/// 🧩 Commits the annotation: solves a driving dimension, or applies a constraint.
/// note  🔴 A REFUSAL CHANGES NOTHING. When the solver cannot reach the value the sketch is left exactly
///        as it was and `SolverRefused` comes back, rather than a partly-moved drawing that satisfies
///        nothing. This is the whole reason edits go through the solver instead of straight into the
///        parameters.
/// cost  🚩🚩🚩
/// tag   api, nonthrowing
AnnotationVerdict ApplyAnnotation(WorldSketchStructure& Declared, AnnotationSession& Session);

/// 🧩 Abandons the gesture. A dimension already declared is withdrawn.
void CancelAnnotationSession(WorldSketchStructure& Declared, AnnotationSession& Session);

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE LABEL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Writes a dimension's figure the way it should be read, in the unit asked for.
/// out   Delivered  [-] e.g. "⌀42.00 mm", "R4.2 m", "128.00 mm"
/// note  📝 The prefix belongs to the SUBJECT, not to the caller -- a radius is meaningless without its
///        R, and leaving it to each drawing site is how half of them end up missing it.
/// tag   api, nonthrowing
void ComposeDimensionLabel(const WorldSketchStructure& Declared,
                           WorldDimensionName Subject,
                           MeasureUnit Unit,
                           bool ShowUnit,
                           char* Delivered,
                           std::uint32_t Capacity);

} // namespace Slate
