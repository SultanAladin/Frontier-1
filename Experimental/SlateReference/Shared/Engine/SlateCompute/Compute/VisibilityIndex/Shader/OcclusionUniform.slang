//============================================================================================================================================
//                                                          OCCLUSIONUNIFORM.SLANG
//============================================================================================================================================
// 🧩 The device halves of `16` §2's three uploaded records, and the two comparisons both culling dispatches read.

#pragma once

// 📐 🔴 Every declaration below is a **mirror**. Its host counterpart carries a static assertion on its width and
//    on its member count, and a member inserted on one side alone shifts every later member of the other by four
//    bytes — which the vendor accepts and the device then reads a triangle ordinal out of a depth coordinate for.
//
// ⚠️ There is no shared declaration to reach for. `Shared/` compiles under both toolchains and this does not:
//    the records below are read through resource declarations the host toolchain has no spelling for. The two
//    are mirrors held in agreement by assertion rather than one declaration read twice, exactly as
//    `VisibilityUniform.slang` and `VisibilityRaster.h` already are.

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE THREE RECORDS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which level of the chain one reduction dispatch writes.
/// note  🔴 `SourceFromTarget` is declared and never derived from `WrittenLevel` being nought. The derivation
///        holds only while the finest level's extent is the display's, and that is a condition the record cannot
///        see — the chain may have been derived against an extent the display has since been resized past.
struct ReductionUniform
{
    uint  WrittenLevel;       // [-]     - which level the dispatch writes
    uint  WrittenOffset;      // [texel] - where that level begins in the chain span
    uint  WrittenX;       // [texel] - its extent
    uint  WrittenY;      // [texel]
    uint  SourceOffset;       // [texel] - where the level above begins; unread when the target is read
    uint  SourceX;        // [texel] - the source's extent
    uint  SourceY;       // [texel]
    uint  SourceFromTarget;   // [-]     - non-zero for the finest level
};

/// 🧩 What the occlusion dispatch reads of the chain and of the run it is testing.
struct OcclusionUniform
{
    uint  ClassifiedCount;   // [-]  - partitions the dispatch tests
    uint  LevelCount;        // [-]  - levels the chain carries
    uint  DisplayX;      // [px] - the extent the finest level covers
    uint  DisplayY;     // [px]
    uint  TriangleLimit;   // [-]  - entries the surviving run can hold
    uint  PhaseIndex;      // [-]  - nought is ①, one is ③
    uint  Unoccupied1;       // [-]
    uint  Unoccupied2;       // [-]
};

/// 🧩 One classified partition, as the host handed it over.
/// note  🔴 The **triangle run** is carried because the compaction compacts triangles and not partitions. An
///        indirect draw reads corners by a running ordinal, so three corners of a partition are three corners of
///        its first triangle and every other one is unreachable; the survivor writes its own run out instead.
struct TestedPartition
{
    uint   PartitionIndex;   // [-]  - within the registration
    uint   Current;           // [-]  - the host's verdict; see the four ordinals below
    uint   FirstTriangle;      // [-]  - into the residency's fanned run
    uint   TriangleCount;      // [-]  - triangles of that run it spans
    uint   OriginX;        // [px] - the first display texel it covers
    uint   OriginY;       // [px]
    uint   ProjectedX;     // [px] - conservative outward
    uint   ProjectedY;    // [px]
    float  NearestDepth;       // [-]  - reversed; the greatest coordinate of the eight corners
};

// 📝 `PartitionVerdict`'s ordinals, spelled as constants because the host enumeration has no device counterpart.
//    An enumeration declared on both sides is two declarations that must agree; two constants compared by the
//    same integer are one fact written twice at a width the compiler checks.
static const uint PartitionAccepted            = 0u;
static const uint PartitionFrustumExcluded     = 1u;
static const uint PartitionOrientationExcluded = 2u;
static const uint PartitionDepthOccluded       = 3u;

static const uint PhaseAgainstPrevious = 0u;
static const uint PhaseAgainstCurrent  = 1u;

//------------------------------------------------------------------------------------------------------------------------
//                                                    LEVEL SELECTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The coarsest level at which one projected extent is covered by a two-by-two reading.
/// in    SpannedX   [px]  the extent the partition projects to, conservative outward
/// in    SpannedY  [px]
/// in    LevelCount     [-]   levels the chain carries
/// out   Level          [-]   below LevelCount
/// note  📐 The walk mirrors `DepthReduction::LevelOfExtent` step for step, halving with **round up** on both
///        ordinates. Rounding down here selects a level one coarser than the host would, and the four texels
///        read then fail to span the extent along whichever axis was odd — a partition rejected on depth it
///        never actually stood behind.
/// note  ⚠️ An extent of nothing resolves to the finest level rather than refusing. A partition projecting to
///        nothing is sub-pixel and belongs to `16` §3's compute route; it is not an error and rejecting it here
///        would drop geometry no other route then draws.
uint SelectLevel(uint SpannedX, uint SpannedY, uint LevelCount)
{
    uint RemainingX  = SpannedX;
    uint RemainingY = SpannedY;
    uint Selected        = 0u;

    const uint Coarsest = LevelCount - 1u;

    while ((RemainingX > 2u || RemainingY > 2u) && Selected < Coarsest)
    {
        RemainingX  = (RemainingX  + 1u) / 2u;
        RemainingY = (RemainingY + 1u) / 2u;

        Selected += 1u;
    }

    return Selected;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE DEPTH COMPARISON
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Whether one partition stands wholly behind what the reduction recorded.
/// in    NearestDepth  [-]  the partition's nearest coordinate; reversed, so the greatest of its eight corners
/// in    ReducedDepth  [-]  the least coordinate the covered region carries; the furthest point recorded there
/// out   Occluded      [-]  true only where nothing of the partition can reach in front of what stands
/// note  📐 Reversed depth throughout — `NearPlaneDepth` is unity and `FarPlaneDepth` nought, per `Foundation/`.
///        The conservative reduction is therefore the **minimum**, and a partition is occluded when its nearest
///        coordinate is at or below the furthest coordinate recorded across the region it covers.
/// note  🔴 Written the other way round, this rejects on every silhouette and accepts inside every surface — the
///        artist meets it as an object flickering along its own edge rather than as a comparison inverted.
bool DepthOccludes(float NearestDepth, float ReducedDepth)
{
    return NearestDepth <= ReducedDepth;
}
