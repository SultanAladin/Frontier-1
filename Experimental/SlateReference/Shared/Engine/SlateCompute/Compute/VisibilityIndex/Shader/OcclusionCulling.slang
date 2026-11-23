//============================================================================================================================================
//                                                         OCCLUSIONCULLING.SLANG
//============================================================================================================================================
// 🧩 `16` §2 ① and ③ — one invocation per partition, four texels of the chain, and the compaction the draw is registered from.

#include "SlateCompute/Compute/VisibilityIndex/Shader/OcclusionUniform.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                      WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 0)]] ConstantBuffer<OcclusionUniform>      Occluding;
[[vk::binding(1, 0)]] StructuredBuffer<float>               Chain;
[[vk::binding(2, 0)]] StructuredBuffer<uint>                LevelExtents;
[[vk::binding(3, 0)]] StructuredBuffer<TestedPartition>     Classified;
[[vk::binding(4, 0)]] RWStructuredBuffer<uint>              SurvivingRun;
[[vk::binding(5, 0)]] RWStructuredBuffer<uint>              IndirectRecord;
[[vk::binding(6, 0)]] RWStructuredBuffer<uint>              Verdicts;

// 📝 Three ordinals per level in `LevelExtents` — where the level begins in the chain, and how far it runs on
//    each axis. Held apart from the uniform because the level is selected per partition, and a uniform carrying
//    every level's extent would be a block sized for the deepest chain the display extent could ever imply.
static const uint LevelExtentStride = 3u;

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE FOUR TEXELS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The furthest coordinate recorded across the region one partition covers.
/// in    Tested  [-]  the classified partition, its origin and span in display texels
/// out   Depth   [-]  the least coordinate of the four texels read; reversed, so the furthest point
/// note  📐 Four texels and no more, whatever the partition's projected extent. `SelectLevel` chooses the level
///        so that a two-by-two reading spans the whole extent, which is what makes the test one constant-cost
///        comparison per partition rather than a walk proportional to what it covers.
/// note  🔴 The **origin** is read as well as the span, and both are needed. The level is chosen from the span
///        and the four texels are read at the origin; reading at nought instead compares every partition against
///        the depth recorded in the display's first corner, which accepts nothing and rejects everything.
float FurthestOver(TestedPartition Tested)
{
    const uint Level = SelectLevel(Tested.ProjectedX, Tested.ProjectedY, Occluding.LevelCount);

    const uint Offset = LevelExtents[Level * LevelExtentStride];
    const uint X  = LevelExtents[Level * LevelExtentStride + 1u];
    const uint Y = LevelExtents[Level * LevelExtentStride + 2u];

    // 📐 The inverse of a round-up halving, for a **position**, is a floor division — so a display texel maps to
    //    its level texel by a shift. Rounding the origin up instead would read one texel past the region the
    //    partition covers and reject it against depth recorded beside it.
    const uint OriginX  = min(Tested.OriginX  >> Level, X  - 1u);
    const uint OriginY = min(Tested.OriginY >> Level, Y - 1u);

    const uint NextX  = min(OriginX  + 1u, X  - 1u);
    const uint NextY = min(OriginY + 1u, Y - 1u);

    const float LowerLeft  = Chain[Offset + OriginY * X + OriginX];
    const float LowerRight = Chain[Offset + OriginY * X + NextX];
    const float UpperLeft  = Chain[Offset + NextY   * X + OriginX];
    const float UpperRight = Chain[Offset + NextY   * X + NextX];

    return min(min(LowerLeft, LowerRight), min(UpperLeft, UpperRight));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE COMPACTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Appends one partition's triangle run to the surviving ordinals and advances the record's corner count.
/// note  🔴 One atomic and not two. The corner count is three times the triangle count, so advancing the count
///        and dividing the prior value by three gives the run's own base — a second counter for the base would
///        be a second ordering the two atomics have no guarantee of agreeing on.
/// note  ⚠️ A run that would overrun the surviving span is dropped whole rather than written short. A partial
///        run leaves the fan's triangle ordinals interleaved with another partition's, and the draw then issues
///        one partition's corners against another's positions.
void Compact(TestedPartition Tested)
{
    uint Prior = 0u;

    InterlockedAdd(IndirectRecord[0], Tested.TriangleCount * 3u, Prior);

    const uint FirstSlot = Prior / 3u;

    if (FirstSlot + Tested.TriangleCount > Occluding.TriangleLimit)
    {
        return;
    }

    for (uint Passed = 0u; Passed < Tested.TriangleCount; ++Passed)
    {
        SurvivingRun[FirstSlot + Passed] = Tested.FirstTriangle + Passed;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE TWO PHASES
//------------------------------------------------------------------------------------------------------------------------

// 🔴 The two phases differ in **which reduction they read** and in what they are permitted to admit, and in
//    nothing else. ① reads the chain the previous rotation left standing and tests every partition the host
//    accepted; ③ reads the chain this rotation reduced and tests only what ① rejected on depth. Without the
//    second restriction ③ re-accepts every one of ①'s survivors and the draw issues each of them twice.

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 Dispatched : SV_DispatchThreadID)
{
    const uint PartitionIndex = Dispatched.x;

    if (PartitionIndex >= Occluding.ClassifiedCount)
    {
        return;
    }

    const TestedPartition Tested = Classified[PartitionIndex];

    // 📝 A partition the host rejected is dispatched and skipped rather than compacted away before the dispatch.
    //    Compacting on the host would make the classified run shorter than the partition count and the verdict
    //    run would then be indexed by something other than the partition's own ordinal.
    if (Tested.Current != PartitionAccepted)
    {
        if (Occluding.PhaseIndex == PhaseAgainstPrevious)
        {
            Verdicts[PartitionIndex] = Tested.Current;
        }

        return;
    }

    if (Occluding.PhaseIndex == PhaseAgainstCurrent && Verdicts[PartitionIndex] != PartitionDepthOccluded)
    {
        // 📝 Already drawn by ①, or already rejected by a predicate depth cannot overturn. Either way it is not
        //    ③'s to admit, and accepting it is what draws a survivor twice.
        return;
    }

    const float Furthest = FurthestOver(Tested);

    if (DepthOccludes(Tested.NearestDepth, Furthest))
    {
        if (Occluding.PhaseIndex == PhaseAgainstPrevious)
        {
            Verdicts[PartitionIndex] = PartitionDepthOccluded;
        }

        return;
    }

    if (Occluding.PhaseIndex == PhaseAgainstPrevious)
    {
        Verdicts[PartitionIndex] = PartitionAccepted;
    }

    Compact(Tested);
}
