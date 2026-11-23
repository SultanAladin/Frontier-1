//============================================================================================================================================
//                                                          TOPOLOGYSTRUCTURE.H
//============================================================================================================================================
// 🧩 Polygon topology exactly as it arrived — sealed once, never mutated, and never repaired.

#pragma once

#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"
#include "SlateMath/Numeric/TransformProjection/Api/TransformProjection.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 IMPORTED ATTRIBUTES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One position in a surface's parametric domain.
/// note  Held at 32 bits per `02` §3.2's surface space. The domain is the unit square and a 32-bit real resolves
///        it far below one texel of the largest working extent, so widening would buy nothing.
/// tag   nonallocating, nonthrowing
struct DomainCoordinate
{
    float  CoordinateX  = 0.0f;   // [-] - the domain's first axis
    float  CoordinateY = 0.0f;   // [-] - its second
};

/// 🧩 A unit direction in an owner's own object space.
/// tag   nonallocating, nonthrowing
struct SurfaceDirection
{
    float  DirectionX = 0.0f;   // [-] - unit length, in object space
    float  DirectionY = 0.0f;   // [-]
    float  DirectionZ = 0.0f;   // [-]
};

/// 🧩 One vertex's tangent basis and the handedness that completes it.
/// note  🔴 Handedness is stored **per vertex** and is not derivable per pixel — `38` §4. A domain that mirrors
///        across a seam inverts it on one side, and `18` §1.1 interpolates this rather than recomputing it.
/// tag   nonallocating, nonthrowing
struct TangentBasis
{
    SurfaceDirection  Tangent           = {};      // [-] - along the domain's first axis
    float             HandednessSignum  = 1.0f;    // [-] - +1 or −1; completes the basis with the perpendicular
    bool              BasisDeclared     = false;   // [-] - false where the domain is degenerate — `18` §1.1
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE TOPOLOGY
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Polygon topology — positions, corners, faces, and whichever attributes the source supplied.
/// note  🔴 Sealed once and read thereafter. `38`'s non-mutation guarantee begins here: an index into these
///        arrays means the same thing after conditioning as before it, so nothing may renumber them.
/// note  🔴 Faces are corner runs and may carry any corner count. `50` §2 ① never repairs, so n-gons, repeated
///        indices and degenerate faces all survive intake and are **registered** by `38` §3 rather than removed.
/// note  ⚠️ A format that stores a domain coordinate per corner has already split every vertex where its
///        coordinates differ, which is why coordinates are held per corner and positions per vertex. `38` §2's
///        welding index is what relates the two, and it is derived, not stored here.
/// tag   owning
class TopologyStructure
{
public:

    /// 🧩 Declares the vertex positions, in the source's own ordering.
    /// in    Incoming  [-]  positions in object space at 64-bit
    /// out   Result   [-]  refuses with HostDenied once the topology is sealed
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> DeclarePositions(const std::vector<DocumentPosition>& Incoming);

    /// 🧩 Declares one face as a run of vertex ordinals, in the source's own winding.
    /// in    CornerVertices  [-]  vertex ordinals, in traversal order
    /// out   Result         [-]  refuses with HostDenied once sealed, with ExtentExhausted for a run of fewer
    ///                            than three corners, and with ContentUnsupported for an out-of-range ordinal
    /// note  📝 A run of fewer than three corners is rejected rather than registered as degenerate, because it is
    ///        not a face at all and `38` §3's enrollment is over faces. `50` §3 refuses the intake instead.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> DeclareFace(const std::vector<std::uint32_t>& CornerVertices);

    /// 🧩 Declares one domain coordinate per corner, in corner order.
    /// out   Result  [-]  refuses with ContentUnsupported when the count does not equal the corner count
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> DeclareCoordinates(const std::vector<DomainCoordinate>& Incoming);

    /// 🧩 Declares one perpendicular per vertex, as the source supplied it.
    /// out   Result  [-]  refuses with ContentUnsupported when the count does not equal the vertex count
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> DeclarePerpendiculars(const std::vector<SurfaceDirection>& Incoming);

    /// 🧩 Declares one tangent basis per vertex, as the source supplied it.
    /// note  🔴 A supplied basis is retained and `38` §4 does not override it with a derived one. An imported
    ///        basis that disagrees with the imported perpendicular is the author's decision, and reproducing
    ///        their appearance requires reproducing it.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> DeclareTangentBases(const std::vector<TangentBasis>& Incoming);

    /// 🧩 Declares one material enrollment per face.
    /// out   Result  [-]  refuses with ContentUnsupported when the count does not equal the face count
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> DeclareMaterialRegistration(const std::vector<std::uint32_t>& Incoming);

    /// 🧩 Seals the topology, issuing its revision. Nothing may be declared afterwards.
    /// out   Result  [-]  refuses with ExtentExhausted when no face was declared
    /// post  🔴 every read below is stable, and `38` may condition against it off the tick
    /// post  🔴 the revision is distinct from that of every other topology sealed in this process
    /// note  📝 Idempotent — a second call delivers and does **not** issue a second revision. The seal is the
    ///        point the content stopped moving, and re-announcing it must not invalidate a conditioning that
    ///        already describes the same content.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Seal();

    /// 🧩 Whether the topology is sealed and therefore readable.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Sealed() const;

    /// 🧩 The revision the seal registered — what `24` §3 keys a transferred result on.
    /// out   Revision  [-]  zero until sealed; thereafter distinct across every topology in the process
    /// note  🔴 Registered from one process-wide sequence, not counted per topology. Two topologies never share a
    ///        revision, which is what lets `38`, `40`, `68`, `16` and `24` refuse a description derived from a
    ///        different one. A per-topology count would make all five comparisons read one against one.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t Revision() const;

    std::uint32_t VertexCount() const;
    std::uint32_t FaceCount() const;
    std::uint32_t CornerCount() const;

    /// 🧩 The first corner ordinal of one face, and how many corners it carries.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t FaceFirstCorner(std::uint32_t FaceIndex) const;
    std::uint32_t FaceCornerCount(std::uint32_t FaceIndex) const;

    /// 🧩 The vertex one corner addresses.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t CornerVertex(std::uint32_t CornerIndex) const;

    /// 🧩 Which face one corner belongs to.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t CornerFace(std::uint32_t CornerIndex) const;

    const std::vector<DocumentPosition>&  Positions() const;
    const std::vector<DomainCoordinate>&  Coordinates() const;
    const std::vector<SurfaceDirection>&  Perpendiculars() const;
    const std::vector<TangentBasis>&      TangentBases() const;
    const std::vector<std::uint32_t>&     MaterialRegistration() const;

    bool CoordinatesSupplied() const;
    bool PerpendicularsSupplied() const;
    bool TangentBasesSupplied() const;

private:

    std::vector<DocumentPosition>  VertexPositions;                  // [mm] - as supplied, at 64-bit
    std::vector<std::uint32_t>     CornerVertexIndexs;             // [-]  - vertex per corner
    std::vector<std::uint32_t>     CornerFaceIndexs;               // [-]  - face per corner
    std::vector<std::uint32_t>     FaceFirstCorners;                 // [-]  - first corner per face
    std::vector<std::uint32_t>     FaceCornerCounts;                 // [-]  - corners per face
    std::vector<DomainCoordinate>  CornerCoordinates;                // [-]  - per corner where supplied
    std::vector<SurfaceDirection>  VertexPerpendiculars;             // [-]  - per vertex where supplied
    std::vector<TangentBasis>      VertexTangentBases;               // [-]  - per vertex where supplied
    std::vector<std::uint32_t>     FaceMaterialIndexs;             // [-]  - material enrollment per face
    std::uint64_t                  SealedRevision      = 0u;         // [-]  - registered at Seal, process-wide
    bool                           SealDeclared        = false;      // [-]  - no further declaration is accepted
};

}   // namespace Slate
