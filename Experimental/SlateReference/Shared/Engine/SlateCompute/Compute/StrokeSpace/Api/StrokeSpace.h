//============================================================================================================================================
//                                                             STROKESPACE.H
//============================================================================================================================================
// 🧩 The bounded extent one stroke accumulates into — coverage per texel, per touched cell, applied once at Seal.

#pragma once

#include "Foundation/Combination.h"
#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/NumericTolerance.h"
#include "SlateCompute/Compute/SurfaceTileSpace/Api/SurfaceTileSpace.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ONE COVERAGE TILE
//------------------------------------------------------------------------------------------------------------------------

// 📐 Texels per edge of one accumulation tile, at **every** reduction level. The working extent at level L is
//    `MaximumWorkingEdge >> L` and the cell count is `VirtualCellsPerEdge >> L`, so their ratio is invariant and
//    equals `PhysicalTileTexels`. Deriving it per level would be deriving a constant.
inline constexpr std::uint32_t CoverageTileTexels = MaximumWorkingEdge / VirtualCellsPerEdge;   // [px] - 128

// 📝 🔴 No apron. `20` §1's apron exists so a **filtered** read near a tile edge finds valid neighbours; this
//    extent is written and read at exact texels and is filtered by nothing. An impression spanning two cells
//    writes into each cell's own tile, and every texel belongs to exactly one cell by construction.

// 💾 One tile is 128² floats — 64 KiB. Tiles are claimed as cells are touched, so an ordinary stroke costs a
//    handful; the ceiling below is every cell of the finest level, which is the most a stroke can touch at all.
inline constexpr std::uint32_t CoverageTileLimit = VirtualCellsPerEdge * VirtualCellsPerEdge;   // [-] - 4096

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE ACCUMULATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The coverage one stroke has accumulated, held sparsely by cell and reclaimed whole.
/// note  🔴 `22` §3: a stroke resolves into an accumulation extent **first** and applies once to the surface.
///        Applying per impression lets overlapping impressions within one stroke double-darken at their
///        intersections, which is visible wherever an artist slows down — and slowing down is what an artist
///        does at exactly the places they care about.
/// note  📐 Accumulation is `Over`, whatever combination the brush declares. `CombineCoverage(Over, a, b)` is
///        `a + b(1 − a)`, which is symmetric in its two operands — so the accumulated coverage does not depend
///        on the order the impressions were resolved in. That is what lets `22` §2's deferred impression resolve
///        two rotations later without disturbing the ones around it.
/// note  🔴 This holds coverage and **no channel value**. `58` §2 declares one shape and a value per channel, so
///        the values are constants of the stroke and only the coverage varies per texel. Holding values here
///        would be holding one number per channel per texel to store the same number everywhere.
/// tag   owning
class StrokeSpace
{
public:

    /// 🧩 Sizes the sparse index; claims nothing.
    /// post  every cell is unclaimed and the touched count is zero
    /// cost  🚩
    /// tag   api, nonthrowing
    void ConstructStrokeSpace();

    /// 🧩 Reservations the coverage tile backing one cell, or resolves the one already claimed.
    /// in    CellIndex  [-]  into `20` §1's single ordinal span
    /// out   Result      [-]  refuses with ContentUnsupported outside the span, and with ExtentExhausted at
    ///                         the declared tile ceiling
    /// note  📝 Exhaustion is structurally unreachable at the finest level, where the ceiling equals the cell
    ///        count. It is a guard against a coarser level being textured at with a miscomputed ordinal, and it
    ///        refuses rather than growing so that a defect there is a refusal instead of an allocation storm.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Reserve(std::uint32_t CellIndex);

    /// 🧩 The tile backing one cell, if one is claimed.
    /// out   Result  [-]  refuses with ExtentExhausted when the cell is untouched
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Located(std::uint32_t CellIndex) const;

    /// 🧩 Accumulates one impression's coverage at one texel of one claimed tile.
    /// in    TileIndex  [-]  as `Reserve` delivered it
    /// in    X        [px] within the tile
    /// in    Y       [px] within the tile
    /// in    Incoming     [-]  the impression's coverage there, in the closed unit interval
    /// note  🔴 Combined by `Over` and never by addition. Additive accumulation exceeds unity wherever two
    ///        impressions overlap, and the excess is invisible in the accumulation and abrupt at the apply.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Accumulate(std::uint32_t TileIndex, std::uint32_t X, std::uint32_t Y, double Incoming);

    /// 🧩 The coverage standing at one texel.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    double Coverage(std::uint32_t TileIndex, std::uint32_t X, std::uint32_t Y) const;

    /// 🧩 The cells this stroke has touched, in claim order.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<std::uint32_t>& TouchedCells() const;

    std::uint32_t ReservedCount() const;
    std::uint64_t TouchedTexelCount() const;

    /// 🧩 Discards every claimed tile, keeping the sparse index sized.
    /// note  Called at Seal, at Abandon, and once per rotation by a speculative extent — `22` §4.1.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Reclaim();

private:

    // 📝 Held densely across `20` §1's whole ordinal span — 5461 entries, one word each. `20` §1 makes the same
    //    argument for the same span: it is small enough to hold densely, so a lookup is an indexed read rather
    //    than a search, and a stroke that touches four hundred cells pays four hundred indexed reads per
    //    impression instead of four hundred scans.
    std::vector<std::uint32_t>        TileOfCell;              // [-] - AbsentTile where untouched
    std::vector<std::uint32_t>        ReservedCells;            // [-] - in claim order, parallel to Reserved
    std::vector<std::vector<float>>   Reserved;                 // [-] - CoverageTileTexels² each
    std::uint64_t                     TouchedTexels    = 0u;   // [-] - texels this stroke has written at all
};

}   // namespace Slate
