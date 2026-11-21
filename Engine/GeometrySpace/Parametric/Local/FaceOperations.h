/*====================================================================================================================================
                                                        FACEOPERATIONS.H
====================================================================================================================================*/
// 🧩 Face verbs (research § J + K): each one only chooses new carriers; ChangeFaces re-solves the body

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Construction/BodyTransform.h"
#include "Engine/GeometrySpace/Parametric/Local/FaceChange.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Corpus section E verbs. "Faces" is the selection; the carrier each verb gives is listed per entry.
enum class FaceOperationCategory : uint8_t
{
    Offset     = 0,                                                                          // Faces offset by Distance along the outward normal
    Push       = 1,                                                                          // Faces translated by Distance along their outward normal
    Tweak      = 2,                                                                          // Faces translated by Vector
    Scale      = 3,                                                                          // Faces scaled by Factor about their boundary centre
    Rotate     = 4,                                                                          // Faces rotated by Angle about Axis through the face centre
    Draft      = 5,                                                                          // Faces hinged at the neutral plane by Angle (plane → plane, cylinder → cone)
    Extend     = 6,                                                                          // Neighbours offset by +Distance (face grows)
    Trim       = 7,                                                                          // Neighbours offset by −Distance (face shrinks)
    OffsetBody = 8,                                                                          // Every face offset by Distance
    Hollow     = 9,                                                                          // Hollowed to wall Distance, selected faces open (corpus "shell")
    Extrude    = 10,                                                                         // Planar faces moved by Distance with a new ring of side faces
    Delete     = 11,                                                                         // Faces removed; hole loops healed, otherwise an open sheet
    Heal       = 12,                                                                         // Faces on one carrier merged, edges on one carrier merged
    Thicken    = 13,                                                                         // Sheet body grown into a solid of thickness Distance
    Inset      = 14,                                                                         // Planar faces: border ring Distance wide + inner face raised by Depth
    Reshape    = 15,                                                                         // Vertices carried by Placement, their faces re-fitted (direct edit)
};

struct FaceOperationSettings
{
    FaceOperationCategory      Kind          = FaceOperationCategory::Offset;                        // [-]   - Verb
    std::vector<FaceToken> Faces         = {};                                               // [-]   - Selection (empty: verb default)
    double                 Distance      = 0.0;                                              // [len] - Offset, push, wall, extrusion
    double                 Depth         = 0.0;                                              // [len] - Inset: inner face shift along the outward normal (− pocket)
    double                 Angle         = 0.0;                                              // [rad] - Rotate, draft
    double                 Factor        = 1.0;                                              // [-]   - Scale
    Vector3                Vector        = {};                                               // [len] - Tweak translation
    Vector3                Axis          = { 1.0, 0.0, 0.0 };                                // [-]   - Rotation axis direction
    Vector3                PullDirection = { 0.0, 0.0, 1.0 };                                // [-]   - Draft pull direction
    std::vector<VertexToken> Vertices    = {};                                               // [-]   - Reshape: vertices carried (edges give both ends) · Heal: only these join
    std::vector<EdgeToken>   Edges       = {};                                               // [-]   - Heal: only these dissolve (empty with no vertices: every seam)
    AffineMapping              Placement     = {};                                               // [-]   - Reshape: x ↦ M·x + Shift for those vertices
};

struct FaceOperationOutcome
{
    BodyToken                      Body          = {};                                       // [-]  - New body (input untouched)
    std::string                    Refusal       = {};                                       // [-]  - Non-empty when refused (atomic)
    BodyCategory                   Category      = BodyCategory::Solid;                      // [-]  - Solid, or Sheet after Delete
    FaceChangeStatistics           Statistics    = {};                                       // [-]  - Face-change counters
    std::vector<FaceChangeHistory> FaceHistories = {};                                       // [-]  - Result face provenance
    AuditReport                    Audit         = {};                                       // [-]  - Audit of the new body
    std::string                    Method        = {};                                       // [-]  - Which construction ran
    double                         Milliseconds  = 0.0;                                      // [ms] - Wall time
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 One entry point; every verb is "new carriers + re-solve" (ChangeFaces) except Hollow (cavity by ChangeFaces, then
//    General Fuse difference), Extrude (direct rebuild with a side ring), Inset (border ring + inner face, optional side ring), Delete / Heal (topology edits by rebuild) and
//    Thicken (sheet offset + rim faces). Refusals are prefixed with the verb label and leave the input untouched.
[[nodiscard]] FaceOperationOutcome ApplyFaceOperation(ModelPartition& Partition, BodyToken Body, const FaceOperationSettings& Settings);

[[nodiscard]] const char* FaceOperationLabel(FaceOperationCategory Kind) noexcept;

// Centre of the body's bounding box (vertices and edge samples) and the length-weighted centre of a face's boundary.
[[nodiscard]] Vector3 MeasureBodyBoxCentre(const ModelPartition& Partition, BodyToken Body);
[[nodiscard]] Vector3 MeasureFaceBoundaryCentre(const ModelPartition& Partition, FaceToken Face);

} // namespace Frontier::GeometrySpace::Parametric
