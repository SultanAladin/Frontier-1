//============================================================================================================================================
//                                                             DEPTHREDUCTION.H
//============================================================================================================================================
// 🧩 The hierarchical minimum `16` §2 ② reduces one rotation's depth into, and which level a projected extent is tested at.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "Foundation/NumericTolerance.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHY A MINIMUM
//------------------------------------------------------------------------------------------------------------------------

// 📐 🔴 Depth is reversed — `NearPlaneDepth` is one and `FarPlaneDepth` is zero. The conservative reduction over a
//    reversed target is therefore the **minimum**: the least coordinate across a region is its furthest point, and a
//    partition whose nearest coordinate is below it cannot reach past anything already recorded there.
// ⚠️ Reducing by maximum instead is the one defect this arrangement invites. It produces a reduction that is
//    correct wherever the surface is flat and rejects visible geometry at every silhouette, which the artist meets
//    as an object flickering along its own edge rather than as a comparison written the wrong way round.

// 📝 Levels a reduction may carry. The finest level is the display extent and each further level halves both
//    ordinates, so the ceiling is reached only above `DisplayExtentLimit` — 2¹⁴ needs fifteen levels including
//    the finest, and one more is headroom rather than a budget anything is expected to spend.
inline constexpr std::uint32_t ReductionLevelLimit = 16u;   // [-] - levels including the finest

//------------------------------------------------------------------------------------------------------------------------
//                                                      ONE LEVEL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One reduction level's extent, in texels.
/// note  📝 Halving rounds **up**. A level rounded down drops the last odd column, and the partition projecting
///        onto it is then tested against a level that never saw the depth recorded there.
/// tag   nonallocating, nonthrowing
struct ReductionLevel
{
    std::uint32_t  Width  = 0u;   // [texel] - across the display's first coordinate
    std::uint32_t  Height = 0u;   // [texel] - across its second
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE LEVEL CHAIN
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The level chain one display extent reduces through, and the level a projected extent is tested at.
/// note  🔴 This holds **no depth**. `16` §2 ② reduces into a device extent `06` claims; what is held here is how
///        many levels that extent carries, how wide each one is, and which of them a given projected extent
///        resolves to. The ordinates live on the device and nowhere else.
/// note  🔴 The reduction tested in phase ① is the **previous** rotation's, and the one tested in phase ③ is this
///        rotation's, reduced from what phase ② rasterised. `16` §2 requires both, and a single-phase test
///        against last rotation's reduction alone rejects anything that became visible this rotation — which is
///        every silhouette the camera just moved past.
/// tag   owning
class DepthReduction
{
public:

    /// 🧩 Derives the level chain for one display extent.
    /// in    DisplayX   [px]  the display extent this reduction covers
    /// in    DisplayY  [px]
    /// out   Result        [-]   refuses with ContentUnsupported for an extent of zero and with ExtentExhausted
    ///                            above `DisplayExtentLimit` or beyond `ReductionLevelLimit` levels
    /// post  the chain runs from the display extent down to a single texel
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> ConstructDepthReduction(std::uint32_t DisplayX, std::uint32_t DisplayY);

    /// 🧩 One level's extent.
    /// out   Result  [-]  refuses with ContentUnsupported outside the derived level count
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<ReductionLevel> Level(std::uint32_t LevelIndex) const;

    /// 🧩 The coarsest level at which one projected extent is covered by a two-by-two reading.
    /// in    ProjectedX   [px]  the extent the partition projects to, conservative outward
    /// in    ProjectedY  [px]
    /// out   Result          [-]   refuses with ContentUnsupported while no chain is derived
    /// note  📐 The occlusion test reads four texels and no more, whatever the partition's projected extent. The
    ///        level is chosen so that a two-by-two reading spans the whole extent, which is what makes the test
    ///        one constant-cost comparison per partition rather than a walk proportional to what it covers.
    /// note  ⚠️ A projected extent of zero resolves to the finest level rather than refusing. A partition that
    ///        projects to nothing is sub-pixel and `16` §3 routes it to the compute path; it is not an error.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> LevelOfExtent(std::uint32_t ProjectedX, std::uint32_t ProjectedY) const;

    /// 🧩 Texels the whole chain spans — what `06` claims for it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t ChainTexels() const;

    /// 🧩 Discards the chain and the extent it was derived against.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Reclaim();

    std::uint32_t LevelCount() const;
    std::uint32_t DisplayX() const;
    std::uint32_t DisplayY() const;
    bool          ChainDerived() const;

private:

    std::vector<ReductionLevel>  Levels        = {};      // [-]  - finest first; the last is one texel
    std::uint32_t                DerivedX  = 0u;      // [px] - the display extent derived against
    std::uint32_t                DerivedY = 0u;      // [px]
    bool                         ChainCurrent = false;   // [-]  - Construct has delivered
};

// 📐 The chain arithmetic is integer halving throughout and the level selection is an integer logarithm, so both
//    are Exact. The reduction itself is a minimum over recorded ordinates, which introduces no arithmetic at all.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Exact, PrecisionGuarantee::Exact);

}   // namespace Slate
