/*====================================================================================================================================
                                                          EULEREDITS.H
====================================================================================================================================*/
// 🧩 Euler edits — the only topology mutations; each preserves V − E + 2F − L = 2(S − G)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Topology/ModelPartition.h"

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Either the edit's result, or a refusal reason; a refused edit leaves the partition untouched.
template <typename Result>
struct EditOutcome
{
    Result      Value   = {};                                                                // [-] - Edit result when it succeeded
    const char* Refusal = nullptr;                                                           // [-] - Reason when refused

    [[nodiscard]] bool SucceededCondition() const noexcept { return Refusal == nullptr; }
};

// 💡 A corner is a position in a loop: the fin that LEAVES the vertex there. A fin-less loop (isolated vertex) is
//    addressed with an absent fin. Corners remove the ambiguity of "vertex v in loop L" when v occurs twice in L (seams).
struct LoopCorner
{
    LoopToken Loop = {};                                                                     // [-] - Loop addressed
    FinToken  Fin  = {};                                                                     // [-] - Fin leaving the vertex (absent: isolated loop)
};

struct AcornOutcome
{
    BodyToken       Body           = {};                                                     // [-] - New body
    RegionToken     ExteriorRegion = {};                                                     // [-] - Infinite void region
    RegionToken     SolidRegion    = {};                                                     // [-] - Region the shell bounds
    SolidShellToken Shell          = {};                                                     // [-] - The single shell
    FaceToken       Face           = {};                                                     // [-] - The single face
    LoopToken       Loop           = {};                                                     // [-] - Its isolated-vertex loop
    VertexToken     Vertex         = {};                                                     // [-] - The single vertex
};

struct EdgeVertexOutcome
{
    EdgeToken   Edge        = {};                                                            // [-] - New edge (corner vertex → new vertex)
    VertexToken Vertex      = {};                                                            // [-] - New vertex
    FinToken    OutboundFin = {};                                                            // [-] - Forward fin, corner → new vertex
    FinToken    ReturnFin   = {};                                                            // [-] - Reversed fin, new vertex → corner
};

struct EdgeFaceOutcome
{
    EdgeToken Edge           = {};                                                           // [-] - New edge (From vertex → To vertex)
    FaceToken Face           = {};                                                           // [-] - New face
    LoopToken Loop           = {};                                                           // [-] - New face's loop
    FinToken  RetainedFin    = {};                                                           // [-] - Forward fin, stays in the old loop
    FinToken  TransferredFin = {};                                                           // [-] - Reversed fin, bounds the new face
};

struct EdgeRingOutcome
{
    EdgeToken Edge        = {};                                                              // [-] - New edge joining the two loops
    FinToken  OutboundFin = {};                                                              // [-] - Forward fin
    FinToken  ReturnFin   = {};                                                              // [-] - Reversed fin
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTRUCTIVE EDITS
//------------------------------------------------------------------------------------------------------------------------

// MVFS — a body with one vertex, one face, one shell (bounding a solid region) and an exterior void region.
[[nodiscard]] EditOutcome<AcornOutcome> ConstructBodyFaceVertex(ModelPartition& Partition, Vector3 Position);

// MEV — a new edge from the corner's vertex to a new vertex; both fins enter the corner's loop.
[[nodiscard]] EditOutcome<EdgeVertexOutcome> ConstructEdgeVertex(ModelPartition& Partition, LoopCorner Corner, Vector3 Position);

// MEF — a new edge between two corners of ONE loop, splitting it (the new face inherits a copy of the carrier surface). The old loop keeps the forward fin and the fins from
//       To onwards; the new face receives the reversed fin and the fins from From onwards. Equal corners give a closed edge
//       whose forward fin forms a loop on its own.
[[nodiscard]] EditOutcome<EdgeFaceOutcome> ConstructEdgeFace(ModelPartition& Partition, LoopCorner From, LoopCorner To);

// MEKR — a new edge between corners of TWO loops of one face, merging them (removes a ring).
[[nodiscard]] EditOutcome<EdgeRingOutcome> ConstructEdgeCollapseRing(ModelPartition& Partition, LoopCorner From, LoopCorner To);

// MFKRH — a new face taking over a ring loop of an existing face (inherits a copy of the carrier surface).
[[nodiscard]] EditOutcome<FaceToken> ConstructFaceCollapseRing(ModelPartition& Partition, LoopToken Ring);

//------------------------------------------------------------------------------------------------------------------------
//                                                    COLLAPSING EDITS
//------------------------------------------------------------------------------------------------------------------------

// KEMR — remove an edge whose two fins lie in one loop, splitting that loop into two (creates a ring).
[[nodiscard]] EditOutcome<LoopToken> CollapseEdgeConstructRing(ModelPartition& Partition, EdgeToken Edge);

// KFMRH — remove a single-loop face; its loop becomes a ring of the absorbing face (creates genus or a through hole).
[[nodiscard]] EditOutcome<bool> CollapseFaceConstructRing(ModelPartition& Partition, FaceToken Absorbing, FaceToken Collapsing);

// KEV — remove an edge and its end vertex of degree one.
[[nodiscard]] EditOutcome<bool> CollapseEdgeVertex(ModelPartition& Partition, EdgeToken Edge, VertexToken Vertex);

// KEF — remove an edge between two different faces, merging the faces.
[[nodiscard]] EditOutcome<bool> CollapseEdgeFace(ModelPartition& Partition, EdgeToken Edge);

// KVE — remove a degree-two vertex between Kept and Absorbed: Kept now ends at Absorbed's far vertex, Absorbed and the
// vertex are reclaimed. Topology only; the caller extends Kept's interval and re-fits its SP-curves (same carrier).
[[nodiscard]] EditOutcome<bool> CollapseVertexJoinEdges(ModelPartition& Partition, VertexToken Vertex, EdgeToken Kept, EdgeToken Absorbed);

// KVFS — remove an acorn body (one vertex, one face, no edges).
[[nodiscard]] EditOutcome<bool> CollapseBodyFaceVertex(ModelPartition& Partition, BodyToken Body);

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHELL EDITS
//------------------------------------------------------------------------------------------------------------------------

// Invert a shell: every face sense flips and every loop runs the other way (used for cavities and boolean tool bodies).
void ReverseShellOrientation(ModelPartition& Partition, SolidShellToken Shell);

// Move a shell into another region; a body left without shells is reclaimed with its regions.
[[nodiscard]] EditOutcome<bool> TransferShell(ModelPartition& Partition, SolidShellToken Shell, RegionToken Destination);

// Move a shell into a new body of its own (exterior void + one solid region); the inverse of absorbing a cavity.
[[nodiscard]] EditOutcome<BodyToken> DetachShell(ModelPartition& Partition, SolidShellToken Shell);

} // namespace Frontier::GeometrySpace::Parametric
