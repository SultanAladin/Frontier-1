//============================================================================================================================================
//                                                        TOPOLOGYCONDITIONING.H
//============================================================================================================================================
// 🧩 Derived companions to imported topology — adjacency, welding, orientation, bases and extents. Never a mutation.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateDocument/Document/RegistrationIndex/Api/RegistrationIndex.h"
#include "SlateDocument/Document/TopologyStructure/Api/TopologyStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     DEGENERACY
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The conditions a face or a vertex is registered under rather than removed for.
/// note  🔴 `38` §3: removal is what an editor does. Removing a face renumbers everything after it, and every
///        index the artist's file carried — their selections, their coordinates, their material assignment —
///        would then address the wrong face. Slate does not own that file and must not do this to it.
/// tag   guarantee
enum class DegeneracySubject : std::uint32_t
{
    ZeroExtentFace  = 0u,   // [-] - the face encloses no area; excluded from `40`, `68` and `74`
    RepeatedCorner  = 1u,   // [-] - one vertex appears twice in one face
    Unoriented      = 2u,   // [-] - no consistent orientation against an adjacent face; rendered both-sided
    NonManifoldEdge = 3u,   // [-] - more than two faces meet the edge; `68` cuts a chart boundary at it
    IsolatedVertex  = 4u,   // [-] - no face reaches it; ignored by everything but export
    DegeneracyCount = 5u    // [-] - the closed count, never a condition
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       EXTENTS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One axis-aligned extent in object space, rounded conservatively outward.
/// note  🔴 `38` §6: an extent rounded inward excludes geometry from `16`'s culling and `40`'s traversal, and the
///        symptom is a surface that disappears at one camera angle. Outward costs a nanometre and nothing else.
/// tag   nonallocating, nonthrowing
struct ConditionedExtent
{
    DocumentPosition  Minimum    = {};   // [mm] - the lower corner
    DocumentPosition  Maximum = {};   // [mm] - the upper corner
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE CONDITIONING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Everything imported topology needs before it can be textured on, derived beside it and never into it.
/// note  🔴 `38`'s opening rule: conditioning **derives**; it never mutates. The imported arrays are untouched and
///        an index into them means the same thing after conditioning as before. An importer that repairs its
///        input is an importer whose output the artist cannot reconcile with the file they gave it.
/// note  🔴 `Condition` reads only the supplied topology, which `TopologyStructure::Seal` has made immutable. That
///        is exactly `34` §2's requirement, so this is declarable into `WorkSequence` at `Interactive` priority
///        with nothing captured but the topology — and `38` §5 says it must be, because the artist is waiting.
/// note  🔴 Every derived property is in **object space** — `38` §5's last row. An artist arranging a scene must
///        not pay a re-derivation per move, and `00` §10.1 ② depends on the same property one layer up.
/// tag   owning
class TopologyConditioning
{
public:

    /// 🧩 Derives every companion in `38` §1 from one sealed topology.
    /// in    Imported  [-]  the topology, sealed and therefore immutable for this whole run
    /// out   Result   [-]  refuses with HostDenied for an unsealed topology
    /// post  every read below describes the supplied topology at its sealed revision
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> Condition(const TopologyStructure& Imported);

    /// 🧩 The welded position one imported vertex belongs to.
    /// in    VertexIndex  [-]  an imported vertex
    /// out   Result        [-]  refuses with ContentUnsupported outside the vertex count
    /// note  🔴 `38` §2: two vertices at the same position with different ordinals are one point on the surface
    ///        and two points in the file. `68` unwraps against the welded positions so a chart does not split at
    ///        every coordinate discontinuity, and `18` reads the imported ordinals so authored coordinates live.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> WeldedPosition(std::uint32_t VertexIndex) const;

    /// 🧩 The corner across the edge one corner opens, where exactly one face is adjacent there.
    /// out   Result  [-]  refuses with ContentUnsupported at a boundary or non-manifold edge
    /// note  Refuses rather than reporting a chosen one of several. An adjacency that picks arbitrarily makes a
    ///        traversal's result depend on face declaration order, and `34` §6 forbids exactly that shape.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> AdjacentCorner(std::uint32_t CornerIndex) const;

    /// 🧩 Whether one face is registered under a degeneracy condition.
    /// note  Answered by interval comparison, per `38` §3, so an excluded population costs nothing to skip.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool FaceRegistered(std::uint32_t FaceIndex, DegeneracySubject Condition) const;

    /// 🧩 Whether one vertex is registered as isolated.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool VertexIsolated(std::uint32_t VertexIndex) const;

    /// 🧩 The runs of one registered condition, for whoever excludes a whole span at once.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<RegisteredInterval>& Registered(DegeneracySubject Condition) const;

    const std::vector<SurfaceDirection>&   Perpendiculars() const;
    const std::vector<TangentBasis>&       TangentBases() const;
    const std::vector<ConditionedExtent>&  FaceExtents() const;

    /// 🧩 The extent of the whole topology, conservative outward.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    ConditionedExtent TopologyExtent() const;

    /// 🧩 How many distinct positions the welding index found.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t WeldedCount() const;

    /// 🧩 The topology revision this conditioning describes.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t ConditionedRevision() const;

    /// 🧩 Whether the tangent bases were supplied by the source rather than derived here.
    /// note  🔴 `38` §4 and `38` §7: a supplied basis is used as supplied and never overridden. This is how a
    ///        consumer tells which of the two it is reading.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool TangentBasesRetained() const;

    /// 🧩 How many faces were reported as having no consistent orientation — `86` §4's `38` §3 row.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t UnorientedCount() const;

private:

    static constexpr std::size_t DegeneracySpan = static_cast<std::size_t>(DegeneracySubject::DegeneracyCount);

    void DeriveWelding(const TopologyStructure& Imported);
    void DeriveAdjacency(const TopologyStructure& Imported);
    void DeriveOrientation(const TopologyStructure& Imported);
    void DerivePerpendiculars(const TopologyStructure& Imported);
    void DeriveTangentBases(const TopologyStructure& Imported);
    void DeriveExtents(const TopologyStructure& Imported);

    std::vector<std::uint32_t>     WeldedPositionOfVertex;                    // [-]  - imported vertex to position
    std::vector<std::uint32_t>     AdjacentCornerOfCorner;                    // [-]  - AbsentCorner at a boundary
    std::vector<std::uint32_t>     FirstCornerOfPosition;                     // [-]  - one incident corner per position
    std::vector<std::uint32_t>     NextCornerOfPosition;                      // [-]  - the incidence run
    std::vector<SurfaceDirection>  DerivedPerpendiculars;                     // [-]  - per imported vertex
    std::vector<TangentBasis>      DerivedTangentBases;                       // [-]  - per imported vertex
    std::vector<ConditionedExtent> DerivedFaceExtents;                        // [mm] - per face, outward
    std::vector<RegisteredInterval>  RegisteredConditions[DegeneracySpan] = {};   // [-]  - by ordinal, interval-compressed
    ConditionedExtent              WholeExtent            = {};               // [mm] - outward
    std::uint64_t                  DescribedRevision      = 0u;               // [-]  - the topology's sealed revision
    std::uint32_t                  DistinctPositionCount  = 0u;               // [-]  - welded positions found
    std::uint32_t                  UnorientedFaceCount    = 0u;               // [-]  - reported through `86`
    bool                           BasesRetained          = false;            // [-]  - the source supplied them
};

// 📐 Coincidence, orientation and degeneracy are all Exact; the perpendiculars, bases and extents are Bounded.
//    The whole component therefore claims Bounded, because `00` §3's transitivity rule forbids claiming the
//    stronger guarantee over a body that also produces the weaker one.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded, PrecisionGuarantee::Exact);

}   // namespace Slate
