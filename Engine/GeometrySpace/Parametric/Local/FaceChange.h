/*====================================================================================================================================
                                                          FACECHANGE.H
====================================================================================================================================*/
// 🧩 Generic face change (research § K 🥇): new carriers for chosen faces, every edge and vertex re-solved against them

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Audit/ModelAudit.h"
#include "Engine/GeometrySpace/Parametric/Construction/BodyAssembly.h"
#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"
#include "Engine/GeometrySpace/Parametric/Topology/ModelPartition.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 One face and the carrier it takes. OutwardOffset is set when the carrier is the old one offset along the outward
//    normal: two faces offset by the same distance across a smooth (G1) edge keep meeting tangentially, so that edge is
//    offset exactly instead of intersected (tangent surfaces have no transversal intersection to solve).
struct FaceReplacement
{
    FaceToken       Face          = {};                                                      // [-]   - Face of the source body
    SurfaceGeometry Surface       = PlaneSurface{};                                          // [-]   - New carrier (same sense)
    double          OutwardOffset = std::numeric_limits<double>::quiet_NaN();                // [len] - Offset distance (NaN: not an offset)
};

enum class FaceChangeOrigin : uint8_t
{
    Unchanged = 0,                                                                           // Same carrier, same boundary
    Trimmed   = 1,                                                                           // Same carrier, boundary moved
    Replaced  = 2,                                                                           // New carrier
    Generated = 3,                                                                           // New face (extrude side ring, thicken rim)
};

struct FaceChangeHistory
{
    FaceToken        Result = {};                                                            // [-] - Face of the new body
    FaceChangeOrigin Origin = FaceChangeOrigin::Unchanged;                                   // [-] - What happened to it
    FaceToken        Face   = {};                                                            // [-] - Source face (absent for Generated)
    EdgeToken        Edge   = {};                                                            // [-] - Source edge (Generated side faces)
};

enum class EdgeChangeCategory : uint8_t
{
    Kept        = 0,                                                                         // Both faces unchanged: curve kept
    Seam        = 1,                                                                         // Seam of a replaced face: its iso-line
    Offset      = 2,                                                                         // Smooth edge between equal offsets
    Intersected = 3,                                                                         // Exact intersection of the new carriers
};

struct FaceChangeStatistics
{
    int    Edges[4]          = { 0, 0, 0, 0 };                                               // [-]   - Per EdgeChangeCategory
    int    EllipticalEdges   = 0;                                                            // [-]   - Oblique plane ∩ cylinder
    int    TracedEdges       = 0;                                                            // [-]   - Traced meetings of NURBS / parallel carriers
    int    MovedVertices     = 0;                                                            // [-]   - Vertices re-solved
    int    ReplacedFaces     = 0;                                                            // [-]   - Faces with a new carrier
    double MaxVertexResidual = 0.0;                                                          // [len] - Worst vertex off an incident curve
    double MaxEdgeResidual   = 0.0;                                                          // [len] - Worst edge sample off its two carriers
    double Milliseconds      = 0.0;                                                          // [ms]  - Wall time
};

struct FaceChangeOutcome
{
    BodyToken                      Body          = {};                                       // [-] - New body (input untouched)
    std::string                    Refusal       = {};                                       // [-] - Non-empty when refused (atomic)
    FaceChangeStatistics           Statistics    = {};                                       // [-] - Counters and residuals
    std::vector<FaceChangeHistory> FaceHistories = {};                                       // [-] - Result face provenance
    AuditReport                    Audit         = {};                                       // [-] - Audit of the new body
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Topology-preserving face change (Parasolid-style tweak; research § J/K share it):
//    ① edges — Kept when neither face changed; Seam → iso-line of the new carrier; Offset when both faces moved by the
//       same outward offset across a smooth edge; otherwise the exact intersection of the two new carriers nearest the
//       old edge (plane–plane line, plane–cylinder circle / ellipse / generators, plane–cone circle / generators,
//       plane–sphere circle); orientation aligned with the old edge;
//    ② vertices — offset along the common normal (all incident edges Offset), else Gauss–Newton on the two most
//       transversal incident curves, then every other incident curve must pass through the point;
//    ③ intervals re-projected — an edge that collapses or runs backwards is refused (the change exceeds the edge:
//       topology would have to change, 🚧);
//    ④ SP-curves re-fitted with chart anchors from the old fins, AssembleBody, AuditBody, genus unchanged.
//    Label prefixes refusals ("Offset", "Draft", …). A sheet body is accepted: each laminar edge is re-solved against its
//    rim surface (ConstructRimSurface), which a face change leaves in place.
[[nodiscard]] FaceChangeOutcome ChangeFaces(ModelPartition&                     Partition,
                                            BodyToken                           Body,
                                            const std::vector<FaceReplacement>& Replacements,
                                            const std::string&                  Label);

// Steps ①–④ without assembly: the new body as a blueprint whose vertex / edge / face indices are the positions in
// CollectInventory(Body), with per-face provenance (Result unset). Thicken stitches two such skins.
[[nodiscard]] std::string PlanFaceChange(ModelPartition&                     Partition,
                                         BodyToken                           Body,
                                         const std::vector<FaceReplacement>& Replacements,
                                         BodyBlueprint&                      Blueprint,
                                         std::vector<FaceChangeHistory>&     Provenance,
                                         FaceChangeStatistics&               Statistics);

// 📝 Rim of a laminar edge on Face: the surface swept by the face normal along the edge, so it contains every offset
//    of the edge — line on a plane → plane (normal t × n); circle on a plane → coaxial cylinder; circle on a coaxial
//    cylinder → the circle's plane; generator of a cylinder → the plane through the axis. Empty otherwise (🚧).
[[nodiscard]] std::optional<SurfaceGeometry> ConstructRimSurface(const ModelPartition& Partition, EdgeToken Edge, FaceToken Face);

// 📝 Self-intersection probe (research § J ③, detection only): every edge against every face that does not use it —
//    a crossing inside the face or on its boundary (away from the edge's own vertices), or a coincident stretch there,
//    means the body passes through itself. Empty when clean, else the clash.
[[nodiscard]] std::string DetectSelfClash(const ModelPartition& Partition, BodyToken Body);

// Exact intersection of two carriers nearest Near; empty when the pair is parallel, disjoint or not solved exactly.
[[nodiscard]] std::optional<CurveGeometry> IntersectCarriersExactly(const SurfaceGeometry& First, const SurfaceGeometry& Second, Vector3 Near);

// Outward unit normal of a face at the point of its carrier nearest Position.
[[nodiscard]] Vector3 EvaluateOutwardNormal(const ModelPartition& Partition, FaceToken Face, Vector3 Position);

[[nodiscard]] const char* FaceChangeOriginLabel(FaceChangeOrigin Origin) noexcept;
[[nodiscard]] const char* EdgeChangeCategoryLabel(EdgeChangeCategory Category) noexcept;

} // namespace Frontier::GeometrySpace::Parametric
