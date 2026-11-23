//============================================================================================================================================
//                                                      CONSTRAINTAUTHORING.H
//============================================================================================================================================
// 🧩 What a constraint is to the viewport: which tool asks for which relationship, what the artist must
//    have selected before it can be applied, where its badge hangs, and what it reads as.
//
// 🔴 THE SELECTION A CONSTRAINT NEEDS IS PART OF WHAT THE CONSTRAINT IS. The host decided it in an
//    `if`-chain that tested the subject and then reached for `.Curve` or `.Point` accordingly — the same
//    shape as the placement defect, where a decision with more than one axis was made on one of them. A
//    constraint needs a specific NUMBER of a specific KIND of thing, and that is a property of the
//    relationship, not of the branch it happened to fall into.
//
// 📝 Stated as a table for the same reason `PlacementCommit`'s is: a subject with no row is refused by
//    name, and adding a relationship means adding a row rather than finding the right `else if` to sit
//    below. `ConstraintAuthoringProof` walks every subject the enumeration declares, so a new one cannot
//    be added without either giving it a row or being told it has none.
//
// ⚠️ This unit declares and reads. It records nothing to a surface and reads no pointer; where a badge
//    hangs is a position it answers, not a thing it draws.

#pragma once

#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"
#include "SlateShape/Sketch/ConstraintSpecification/Api/ConstraintSpecification.h"
#include "SlateShape/Reference/ReferenceSpecification/Api/ReferenceSpecification.h"
#include "SlateShape/Sketch/ConstraintSolver/Api/ConstraintSolver.h"

#include "Foundation/DeliveryGuarantee.h"

#include <cstdint>

namespace Slate
{

/// 🧩 What the artist must have selected for a relationship to be declarable.
enum class ConstraintDemand : std::uint32_t
{
    OneCurve   = 0u,    // horizontal, vertical, fixed — a single curve stands on its own
    TwoCurves  = 1u,    // parallel, perpendicular, tangent, equal — a relationship between two
    TwoPoints  = 2u,    // coincident — two points meeting
    DemandCount = 3u
};

/// 🧩 What a constraint subject needs and how it reads.
struct ConstraintDeclaration
{
    ConstraintSubject Subject = ConstraintSubject::Coincident;
    ConstraintDemand  Demand  = ConstraintDemand::OneCurve;
    const char*       Glyph   = "?";     // the badge drawn beside the geometry
    const char*       Naming  = "";      // what the artist reads in the directory
};

/// 🧩 What a relationship needs, or nothing if this unit does not know the subject.
/// note 🔴 Returns a refusal rather than a default. A subject with no row must be reported, because
///       guessing produces a constraint the artist did not ask for.
Deliver<ConstraintDeclaration> DeclaredConstraint(ConstraintSubject Subject);

/// 🧩 Whether this unit can author a given relationship at all.
bool ConstraintSupported(ConstraintSubject Subject);

/// 🧩 The badge drawn beside a constrained curve or point.
/// note 📝 Always answers something. A glyph is decoration, and refusing to draw one would leave the
///       artist with a constraint they cannot see rather than one they cannot name.
const char* ConstraintGlyph(ConstraintSubject Subject);

/// 🧩 What a solver's verdict on a constraint reads as.
const char* ConstraintDispositionNaming(ConstraintDisposition Disposition);

/// 🧩 Where a constraint's badge should hang: the point on the geometry it refers to.
///
/// out   Anchor  [-]  written only when the reference names something the sketch still holds
///
/// note 📝 A curve is badged at its MIDDLE rather than an end, so two constraints on curves that share an
///       endpoint do not stack their badges on top of each other.
/// note ⚠️ A reference to geometry that has since been removed answers false rather than the origin —
///       badges must vanish with what they described, not collect at the world centre.
bool ResolveConstraintAnchor(const SketchStructure& Sketch,
                             const ReferenceSpecification& Reference,
                             SpatialPoint& Anchor);

/// 🧩 Whether a selection satisfies what a relationship demands.
///
/// in    PrimaryCurve    [-]  the curve the artist has active
/// in    SecondaryCurve  [-]  the curve the artist has hovered
/// in    PrimaryPoint    [-]  the point the artist has active
/// in    SecondaryPoint  [-]  the point the artist has hovered
///
/// note 🔴 The single place that answers "can this constraint be applied right now". The host asked it
///       three different ways in three branches, so a relationship that wanted two curves could be
///       reached with one.
bool ConstraintDemandMet(ConstraintDemand Demand,
                         bool PrimaryCurve,
                         bool SecondaryCurve,
                         bool PrimaryPoint,
                         bool SecondaryPoint);

/// 🧩 Builds the relationship a tool asks for from what the artist has selected.
///
/// out   Declared  [-]  a constraint ready to be recorded, or a refusal naming what is missing
///
/// note 🔴 Refuses rather than half-building. A constraint with only its primary reference set is
///       `Declared()` for some subjects, so returning one that was never finished would record a
///       relationship between a curve and nothing.
Deliver<ConstraintSpecification> DeclareConstraintFrom(ConstraintSubject Subject,
                                                       SketchCurveName PrimaryCurve,
                                                       SketchCurveName SecondaryCurve,
                                                       SketchPointName PrimaryPoint,
                                                       SketchPointName SecondaryPoint);

}   // namespace Slate
