//============================================================================================================================================
//                                                           PARTITIONCLASSIFIER.H
//============================================================================================================================================
// 🧩 The three predicates `16` §2 ① rejects a partition by — the frustum, the orientation cone, and the extent it projects to.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateCompute/Compute/VisibilityIndex/Api/PartitionStructure.h"
#include "SlateDocument/Document/CameraProjection/Api/CameraProjection.h"
#include "SlateMath/Numeric/TransformProjection/Api/TransformProjection.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT A TEST ANSWERS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Why one partition is drawn or is not, at the granularity the two phases distinguish.
/// note  🔴 A rejection is an **answer**, not a refusal. `Result` carries absence with a reason and this
///        carries presence with a reason; a partition outside the frustum is the cull working rather than
///        anything declining, and spelling it as a `Refusal` would put the ordinary case on the error path.
/// note  ⚠️ `DepthOccluded` is the only standing `16` §2 ③ re-tests. Frustum and orientation rejections do not
///        change between the two phases — neither predicate reads depth — so re-testing them would walk the
///        same partitions to the same answer and pay for it twice.
/// tag   guarantee
enum class PartitionVerdict : std::uint32_t
{
    Accepted            = 0u,   // [-] - every predicate passed; the partition is drawn
    FrustumExcluded     = 1u,   // [-] - wholly outside one bounding plane
    OrientationExcluded = 2u,   // [-] - every face of it turns away from the camera
    DepthOccluded       = 3u,   // [-] - nearer surfaces already stand where it projects
    VerdictCount       = 4u    // [-] - the closed count, never a standing
};

/// 🧩 What one classified partition carries into the phase that draws it.
/// note  📝 The projected extent is carried because the occlusion test needs it and re-deriving it inside that
///        test would project the same eight corners a second time. It is in display texels and rounded
///        **outward**, so the level `DepthReduction::LevelOfExtent` selects covers at least what is projected.
/// note  🔴 The **origin** is carried beside the span, and both are needed rather than one. The level is chosen
///        from the span and the four texels are read at the origin; a test carrying the span alone knows how
///        coarse a level to read and not where in it to read, and reading at nought instead compares every
///        partition against the depth recorded in the display's first corner.
/// note  🔴 `NearestDepth` is the partition's nearest reversed-depth coordinate — its **greatest**, since near is
///        one and far is zero. The occlusion comparison is then `NearestDepth > ReducedDepth`, and reading the
///        least coordinate here would reject a partition whose front face is in front of everything recorded.
/// note  🔴 The **triangle run** is carried because the occlusion test compacts triangles and not partitions. An
///        indirect draw reads corners by a running ordinal, so three corners of a partition are three corners of
///        its first triangle and the other hundred-odd are unreachable; the survivor writes its own run out
///        instead, and it cannot do that without knowing where in the fan the run begins and how far it goes.
/// tag   nonallocating, nonthrowing
struct ClassifiedPartition
{
    std::uint32_t      PartitionIndex = 0u;                            // [-]    - within the registration
    PartitionVerdict  Current         = PartitionVerdict::Accepted;   // [-]
    std::uint32_t      FirstTriangle    = 0u;                            // [-]    - into the residency's fanned run
    std::uint32_t      TriangleCount    = 0u;                            // [-]    - triangles of that run it spans
    std::uint32_t      OriginX      = 0u;                            // [px]   - the first texel it covers
    std::uint32_t      OriginY     = 0u;                            // [px]
    std::uint32_t      ProjectedX   = 0u;                            // [px]   - conservative outward
    std::uint32_t      ProjectedY  = 0u;                            // [px]
    float              NearestDepth     = 0.0f;                          // [-]    - reversed; the greatest coordinate
};

// 📐 The width and the member count the device reads, asserted rather than commented. `TestedPartition` in
//    `Shader/OcclusionUniform.slang` is the mirror, and a member added on one side alone is a span the device
//    walks at the wrong stride — every ordinal past the first then belongs to another partition.
static_assert(sizeof(ClassifiedPartition) == 36u, "the device reads nine 32-bit words per classified partition");

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE PROJECTED EXTENT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Projects one object-space extent through a composed transform into the display texels it covers.
/// in    Composed       [-]   the composition `ComposeVisibilityTransform` produced, column-major
/// in    Bounded        [mm]  the partition's extent, in the owner's own object space
/// in    DisplayX   [px]  the extent this rotation is recorded against
/// in    DisplayY  [px]
/// out   Projected      [-]   the covered texels, where they begin, and the nearest coordinate; a wholly-behind
///                            extent projects to nothing and reports a zero extent, read as sub-pixel
/// note  📐 All eight corners, not two. The extremal-corner shortcut `FrustumSpace::Classify` uses holds for a
///         plane, because a plane's normal picks the extreme corner in advance; a projective transform has no
///         such normal and the corner that lands furthest along the display is not the corner furthest along
///         any axis of object space.
/// note  🔴 A corner behind the nearest plane is **skipped** rather than divided through. Its homogeneous
///         coordinate is at or below zero, and dividing by it reflects the corner to the opposite side of the
///         display — which produces an extent that spans the whole display and a cull that rejects nothing, or
///         one that spans nothing and rejects a partition the camera is looking straight at.
/// note  ⚠️ An extent with any corner behind the plane reports the **whole display**. It is conservative and it
///         is deliberate: a partition straddling the nearest plane covers an unbounded region of the display in
///         the limit, and no finite extent derived from the corners in front of the plane bounds it.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
ClassifiedPartition ProjectPartitionExtent(const ProjectedTransform&  Composed,
                                           const ConditionedExtent&   Bounded,
                                           std::uint32_t              DisplayX,
                                           std::uint32_t              DisplayY);

// 📐 The projection is a product of Bounded transforms followed by a division and an outward rounding. The
//    rounding only ever widens, so it introduces no standing the guarantee does not already admit.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE ORIENTATION CONE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Whether every face of one partition turns away from the camera.
/// in    Turned      [-]   the partition's cone; a cone that was never derived rejects nothing
/// in    Bounded     [mm]  the partition's extent, in document space
/// in    ViewOrigin  [mm]  the camera's own position
/// out   Rejected    [-]   true only when no face of the partition can face the camera
/// note  📐 The cone rejects when `axis · direction > sin θ + sin φ`. `θ` is the cone's half-angle, so `sin θ`
///         is where a normal `θ` from the axis first turns away; `φ` is the half-angle the extent's bounding
///         sphere subtends from the camera, which widens the test by every direction the partition is seen
///         along. Testing the centre direction alone rejects the near edge of a partition the camera is beside.
/// note  🔴 A camera inside the bounding sphere rejects nothing. The directions to the partition then span more
///         than a hemisphere and no cone excludes them, so `sin φ` is undefined rather than large — and the
///         formula, evaluated anyway, would report the partition back-facing from inside it.
/// note  ⚠️ Object space and document space are taken as coincident here, which holds while every owner is
///         placed at the identity. 🚧 The cone is rotated by the placement when `56` supplies one.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
bool OrientationRejected(const OrientationCone&    Turned,
                         const ConditionedExtent&  Bounded,
                         DocumentPosition          ViewOrigin);

// 📐 One normalisation, one dot product and one square root, all Bounded. The predicate is conservative in the
//    direction that accepts — a lapse widens the accepted set and never narrows it.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE FIRST PHASE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Classifies one partition against the frustum, its own cone, and the extent it projects to.
/// in    Partitioned    [-]   the partition, as the derivation grew it
/// in    Viewing        [-]   what `46` derived for this rotation
/// in    Bounding       [-]   the frustum `46` extracted from the same projection
/// in    Composed       [-]   the composition this owner is drawn with
/// in    PartitionIndex [-] the partition's position within the registration
/// in    FirstTriangle  [-]   where this partition's run begins in the residency's fanned triangles
/// in    DisplayX   [px]  the extent this rotation is recorded against
/// in    DisplayY  [px]
/// out   Classified     [-]   the standing and, when accepted, the projected extent the occlusion test reads
/// note  🔴 The **depth** predicate is not here. `16` §2 tests it against a reduction that lives on the device
///         and nowhere else, so a host answer would need a readback the rotation cannot wait for. What this
///         produces is the candidate set that reaches that test, and `DepthOccluded` is written by it rather
///         than by anything in this file.
/// note  🔴 `FirstTriangle` is supplied rather than read off the partition, because a partition carries
///         `FirstFace` and the fan is what the device draws. The two counts differ wherever a face has more than
///         three corners — `10` accepts any corner count — so the run's beginning is the prefix sum of the
///         preceding partitions' triangle counts and the caller walking them in order is what holds it.
/// note  🔴 The ordinal is **supplied and written here**, not left for the caller to fill afterwards. A field
///         the producer leaves blank is one a caller eventually forgets, and every survivor of that run then
///         compacts the triangle ordinals of partition nought — which draws one partition's geometry against
///         every other partition's place in the fan.
/// note  📝 The frustum is asked first because it is the cheapest of the three and rejects the most. The cone
///         is second and the projection last, since the projection is the only one that touches eight corners.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
ClassifiedPartition ClassifyPartition(const MicroSurfacePartition&  Partitioned,
                                      const ViewProjection&         Viewing,
                                      const FrustumSpace&           Bounding,
                                      const ProjectedTransform&     Composed,
                                      std::uint32_t                 PartitionIndex,
                                      std::uint32_t                 FirstTriangle,
                                      std::uint32_t                 DisplayX,
                                      std::uint32_t                 DisplayY);

// 📐 The weakest of the three predicates it folds. Every one of them is Bounded, and the standing it reports is
//    a selection among them rather than an arithmetic of its own.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

}   // namespace Slate
