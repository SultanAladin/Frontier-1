//============================================================================================================================================
//                                                        RECORDDECLARATION.H
//============================================================================================================================================
// 🧩 Writing a sketched thing into the workspace directory: a curve, a closed profile, a dimension, a
//    constraint or a point, each filed under the folder its category belongs to and named by the index.
//
// 🔴 These five were separate functions in the host that differed only in three places — which subject the
//    record carries, which category folder it is filed under, and which payload member is set. Everything
//    else, including the folder lookup and the naming, was repeated verbatim five times.
//
// 📝 Kept as five named entry points rather than collapsed into one call taking a subject. The payload
//    member differs per subject and a single entry point would need a variant or five optional arguments,
//    which is a worse statement of the same thing: a caller declaring a dimension should not be able to
//    hand over a curve. The repetition that actually mattered — the folder lookup — is now written once.

#pragma once

#include "SlateShape/Record/WorkspaceNameIndex/Api/WorkspaceNameIndex.h"
#include "SlateShape/Record/WorkspaceRecordStructure/Api/WorkspaceRecordStructure.h"
#include "SlateShape/Record/WorkspaceRevisionSequence/Api/WorkspaceRevisionSequence.h"
#include "SlateShape/Sketch/ProfileArea/Api/ProfileArea.h"
#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"

namespace Slate
{

/// 🧩 The top-level folder a category files into.
/// out   Named  [-]  unassigned when the workspace has no folder for that category
/// note ⚠️ Only a folder with no parent counts. A nested folder that happens to carry the same category
///       must not capture records intended for the top-level one.
WorkspaceRecordName ResolveCategoryFolder(const WorkspaceRecordStructure& Records,
                                          WorkspaceCategory Category);

/// 🧩 Writes a sketched curve into the directory.
/// note 📝 A construction curve is named with a leading word rather than filed elsewhere, because it is
///       still part of the sketch — it just does not bound a face.
WorkspaceRecordName DeclareWorkspaceCurve(WorkspaceNameIndex& Naming,
                                          WorkspaceRecordStructure& Records,
                                          SketchCurveName Curve,
                                          bool Construction = false,
                                          WorkspaceShapeFamily Family = WorkspaceShapeFamily::Unknown);

/// 🧩 Writes a closed profile into the directory.
/// note 📝 A profile is closed and cappable by construction; both semantics are set here rather than left
///       to the caller, because a profile that is not closed is not a profile.
WorkspaceRecordName DeclareWorkspaceProfile(WorkspaceNameIndex& Naming,
                                            WorkspaceRecordStructure& Records,
                                            ProfileNameInFeature Profile,
                                            WorkspaceShapeFamily Family = WorkspaceShapeFamily::Profile);

/// 🧩 Writes a dimension into the directory, under the annotation folder.
WorkspaceRecordName DeclareWorkspaceDimension(WorkspaceNameIndex& Naming,
                                              WorkspaceRecordStructure& Records,
                                              DimensionName Dimension);

/// 🧩 Writes a constraint into the directory, under the annotation folder.
WorkspaceRecordName DeclareWorkspaceConstraint(WorkspaceNameIndex& Naming,
                                               WorkspaceRecordStructure& Records,
                                               ConstraintName Constraint);

/// 🧩 Writes a sketch point into the directory.
WorkspaceRecordName DeclareWorkspacePoint(WorkspaceNameIndex& Naming,
                                          WorkspaceRecordStructure& Records,
                                          SketchPointName Point);

/// 🧩 Finds every closed area the sketch now encloses and writes each as a profile.
/// out   Named  [-]  the first profile written, or unassigned when the sketch encloses nothing
/// note 🔴 Seals ONE revision covering all of them, not one per profile. Closing a rectangle declares a
///       single area and the artist expects a single undo; four separate revisions would take four presses
///       to walk back one action.
/// note ⚠️ The 0.05 is a CLOSURE tolerance, not an area threshold: the largest gap, in plane units, that
///       two curve ends may leave and still be treated as joined. Closure here is topological — curves
///       chain end to end — so a loop enclosing no area at all still counts as closed. Raising this makes
///       sloppier sketches close; lowering it makes near-misses stay open.
WorkspaceRecordName AutoDeclareWorkspaceProfilesFromChains(WorkspaceNameIndex& Naming,
                                                           SketchStructure& Sketch,
                                                           WorkspaceRecordStructure& Records,
                                                           WorkspaceRevisionSequence& Revisions);

}   // namespace Slate
