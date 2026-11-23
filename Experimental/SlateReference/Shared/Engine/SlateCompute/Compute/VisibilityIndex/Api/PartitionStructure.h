//============================================================================================================================================
//                                                           PARTITIONSTRUCTURE.H
//============================================================================================================================================
// 🧩 Topology grown across adjacency into partitions of 64 to 128 triangles, each carrying an extent and an orientation cone.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "Foundation/NumericTolerance.h"
#include "SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h"
#include "SlateDocument/Document/TopologyConditioning/Api/TopologyConditioning.h"
#include "SlateDocument/Document/TopologyStructure/Api/TopologyStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 THE ORIENTATION CONE
//------------------------------------------------------------------------------------------------------------------------

// 📝 No partition; never a valid partition ordinal. Declared per unit, matching `20`'s `AbsentTile` and `34`'s
//    `AbsentWork` — nothing reads two of them, and a shared spelling would be a dependency edge no traversal sees.
//    It is also what an unoccupied pixel of the visibility target carries, which is why it is the whole word set.
inline constexpr std::uint32_t AbsentPartition = 0xFFFFFFFFu;   // [-] - no partition ordinal

/// 🧩 The cone every face orientation of one partition falls inside.
/// note  📐 `16` §2 ① rejects a partition whose every face points away from the camera with one dot product,
///        rather than with one per triangle. The axis is the normalised sum of the face orientations and the
///        aperture is the widest departure from it any single face takes.
/// note  🔴 A partition whose orientations span more than a hemisphere has no such cone — no direction exists
///        from which every face is back-facing — and `ConeDerived` is false there. Reporting a cone anyway is
///        how a partition is rejected while the artist is looking straight at part of it.
/// tag   nonallocating, nonthrowing
struct OrientationCone
{
    SurfaceDirection  Axis           = {};      // [-] - unit; the mean of the partition's face orientations
    float             ApertureCosine = -1.0f;   // [-] - cosine of the half-angle; −1 while no cone is claimed
    bool              ConeDerived    = false;   // [-] - false where the orientations exceed a hemisphere
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    ONE PARTITION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One `MicroSurfacePartition` — the unit `16` culls, rasterises and names in every pixel it writes.
/// note  🔴 `FirstFace` addresses `DerivedPartitioning::OrderedFaces` and **never** the imported topology. `50`
///        §2 ① and `38`'s opening rule both forbid renumbering the artist's faces, so the partitioning derives an
///        ordering beside them instead: a partition's faces are contiguous in the ordering the device draws from
///        while every ordinal inside that ordering still means what the imported file meant by it.
/// note  🔴 One material per partition. `42`'s `ResolvedPartition` carries a single material ordinal, so growth
///        stops where the enrollment changes; a partition spanning two would resolve to whichever was recorded
///        first and shade half its own pixels with the wrong reflectance.
/// note  ⚠️ `TriangleCount` sits between `PartitionTriangleFloor` and `PartitionTriangleLimit` except at the
///        end of a connected piece, where the growth front exhausts before the floor is reached. That partition
///        is closed short rather than merged across a boundary — merging is what makes an extent enclose two
///        pieces that are nowhere near each other, and the cull then rejects neither.
/// tag   nonallocating, nonthrowing
struct MicroSurfacePartition
{
    ConditionedExtent  Extent          = {};   // [mm] - object space, conservative outward
    OrientationCone    Orientation     = {};   // [-]  - what `16` §2 ① rejects a back-facing partition by
    std::uint32_t      FirstFace       = 0u;   // [-]  - into OrderedFaces, never into the imported topology
    std::uint32_t      FaceCount       = 0u;   // [-]  - faces of that ordering the partition spans
    std::uint32_t      TriangleCount   = 0u;   // [-]  - fan triangles the spanned faces amount to
    std::uint32_t      MaterialIndex = 0u;   // [-]  - the one enrollment every spanned face carries
};

//------------------------------------------------------------------------------------------------------------------------
//                                               THE DERIVED PARTITIONING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What the derivation reports through `86` — every measure a partitioning can be judged by.
/// tag   nonallocating, nonthrowing
struct PartitioningMetrics
{
    std::uint32_t  PartitionCount        = 0u;   // [-] - partitions derived
    std::uint32_t  ShortPartitionCount   = 0u;   // [-] - closed below the floor; a connected piece ran out
    std::uint32_t  ConelessCount         = 0u;   // [-] - orientations spanning more than a hemisphere
    std::uint32_t  EdgeRefusalCount  = 0u;   // [-] - adjacency refusals the growth front met
    std::uint32_t  ExcludedFaceCount     = 0u;   // [-] - zero-extent faces the growth stepped over
    std::uint32_t  MinimumTriangleCount    = 0u;   // [-] - the smallest partition derived
    std::uint32_t  MaximumTriangleCount = 0u;   // [-] - the largest; above the ceiling only for a lone wide face
};

/// 🧩 A whole derived partitioning, as a value that crosses back to the tick.
/// note  🔴 A value rather than a mutation, for the reason `68` §7 gives `24`: this runs off the tick against a
///        sealed topology and touches no document state at all, and the standing partitioning stands until
///        `PartitionStructure::Adopt` takes this one. Swapping mid-derivation leaves `16` naming partitions in
///        pixels that the resolution no longer resolves, and the artist meets that as a surface shading as some
///        other surface for one rotation.
/// tag   owning
struct DerivedPartitioning
{
    std::vector<MicroSurfacePartition>  Partitions        = {};   // [-] - in the order they were grown
    std::vector<std::uint32_t>          OrderedFaces      = {};   // [-] - imported face ordinals, partition-contiguous
    PartitioningMetrics                 Metrics           = {};   // [-] - what `86` reports
    std::uint64_t                       DescribedRevision = 0u;   // [-] - the sealed topology revision this describes
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE DERIVATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Grows one sealed topology into partitions across its own adjacency.
/// in    Imported     [-]  the sealed topology; immutable for the whole run
/// in    Conditioned  [-]  its conditioning, at the same revision
/// out   Result      [-]  refuses with HostDenied for an unsealed topology, with ContentUnsupported when the
///                         conditioning describes another revision, and with ExtentExhausted where the partition
///                         count would reach `AbsentPartition` and stop being an ordinal
/// note  ⚠️ `42`'s own resolution ceiling is rejected by `42`, at `PartitionStructure::Declare`. It is not
///        re-declared here, because a second spelling of one number is `00` §2's case whichever unit holds it.
/// note  🔴 `16` §1 and `16` §6's first two gates. Growth is across `38`'s adjacency rather than along the
///        imported face ordering, because a contiguous run of face ordinals is spatially coherent only in files
///        that happen to have been authored that way — and one that was not produces extents that each enclose
///        the whole object, at which point the cull rejects nothing and the whole mechanism costs without paying.
/// note  🔴 An adjacency refusal is a **growth-front terminator and not an error**. `38`'s `AdjacentCorner`
///        refuses at a boundary and at a non-manifold edge rather than choosing one of several faces, so the
///        front simply does not cross there and the count is reported through `PartitioningMetrics`.
/// note  📝 Faces registered as zero-extent are stepped over rather than accepted. They contribute no triangle and
///        no orientation, and accepting them would spend a partition's budget on geometry that rasterises to
///        nothing.
/// cost  🔴
/// tag   api, nonthrowing
Deliver<DerivedPartitioning> DerivePartitioning(const TopologyStructure&    Imported,
                                                const TopologyConditioning& Conditioned);

// 📐 The extents and the cone are Bounded; the adjacency traversal, the face counting and the material comparison
//    are Exact. `00` §3's transitivity rule forbids claiming the stronger over a body producing the weaker.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded, PrecisionGuarantee::Exact);

//------------------------------------------------------------------------------------------------------------------------
//                                              THE STANDING PARTITIONING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The partitioning one owner's topology currently stands at, and the identities `42` registered against it.
/// note  🔴 `16` §1: derived once when the topology changes and **never** per rotation. A camera move, an
///        owner move and a texture stroke re-derive nothing here — the extents and the cone are in object space
///        and none of the three touches object space.
/// tag   owning
class PartitionStructure
{
public:

    /// 🧩 Adopts a derived partitioning on the tick, advancing the revision.
    /// in    Incoming  [-]  as DerivePartitioning produced it
    /// out   Result   [-]  refuses with ContentUnsupported for a partitioning carrying no partition
    /// post  the revision advanced; every identity registered against the prior one is discoverably stale
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Adopt(const DerivedPartitioning& Incoming);

    /// 🧩 Declares every standing partition into `42`'s resolution, retaining the identities it issues.
    /// in    Resolutions  [-]  the document's resolution; rebuilt by `42` and written here
    /// in    Owner     [-]  who the standing partitioning belongs to
    /// out   Result      [-]  refuses with whatever the resolution rejected, having declared nothing further
    /// post  Identities carries one identity per standing partition, in partition ordinal order
    /// note  🔴 `16` §4.1 and `00` §10's conflict 15: the owner is supplied here and written into the
    ///        resolution, because a partition identity is not an owner identity and nothing downstream can
    ///        recover one from the other. This declaration is the only place the two are related.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> Declare(PartitionResolutionIndex& Resolutions, OwnerIdentity Owner);

    /// 🧩 The standing partitioning.
    /// pre   PartitioningCurrent holds
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const DerivedPartitioning& Current() const;

    /// 🧩 The identity `42` registered for one standing partition.
    /// out   Result  [-]  refuses with ContentUnsupported outside the standing partition count, and with
    ///                     IdentityStale when nothing has been declared since the last adoption
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<PartitionIdentity> IdentityOf(std::uint32_t PartitionIndex) const;

    /// 🧩 Discards the standing partitioning and every identity taken against it.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Reclaim();

    bool          PartitioningCurrent() const;
    std::uint64_t Revision() const;
    std::uint64_t DescribedRevision() const;
    std::uint32_t PartitionCount() const;

private:

    DerivedPartitioning             CurrentPartitioning = {};    // [-] - as Adopt took it
    std::vector<PartitionIdentity>  Identities           = {};    // [-] - one per partition, as Declare registered them
    std::uint64_t                   AdoptedRevision      = 0u;    // [-] - advanced by Adopt
    bool                            PartitioningAdopted  = false; // [-] - Adopt has delivered
};

}   // namespace Slate
