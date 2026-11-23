//============================================================================================================================================
//                                                            STROKESPACE.CPP
//============================================================================================================================================
// 🧩 Sparse tile claiming over the dense cell index, and the commutative coverage accumulation.

#include "SlateCompute/Compute/StrokeSpace/Api/StrokeSpace.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

void StrokeSpace::ConstructStrokeSpace()
{
    TileOfCell.assign(CellIndexSpan, AbsentTile);

    ReservedCells.clear();
    Reserved.clear();

    TouchedTexels = 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLAIM AND LOCATE
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> StrokeSpace::Reserve(std::uint32_t CellIndex)
{
    if (TileOfCell.empty())
        ConstructStrokeSpace();

    if (CellIndex >= CellIndexSpan)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "no such cell" });

    if (TileOfCell[CellIndex] != AbsentTile)
        return Deliver<std::uint32_t>::Result(TileOfCell[CellIndex]);

    if (Reserved.size() >= CoverageTileLimit)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ExtentExhausted, "the stroke touched more cells than the accumulation holds" });
    }

    const std::uint32_t TileIndex = static_cast<std::uint32_t>(Reserved.size());

    // 📝 Zeroed on claim rather than on reclaim. A stroke that touches four cells and is abandoned pays for four
    //    tiles; zeroing at reclaim would pay for whatever the previous stroke touched as well.
    Reserved.push_back(std::vector<float>(static_cast<std::size_t>(CoverageTileTexels) * CoverageTileTexels, 0.0f));
    ReservedCells.push_back(CellIndex);

    TileOfCell[CellIndex] = TileIndex;

    return Deliver<std::uint32_t>::Result(TileIndex);
}

Deliver<std::uint32_t> StrokeSpace::Located(std::uint32_t CellIndex) const
{
    if (CellIndex >= TileOfCell.size() || TileOfCell[CellIndex] == AbsentTile)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ExtentExhausted, "the stroke has not touched it" });

    return Deliver<std::uint32_t>::Result(TileOfCell[CellIndex]);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ACCUMULATION
//------------------------------------------------------------------------------------------------------------------------

void StrokeSpace::Accumulate(std::uint32_t TileIndex, std::uint32_t X, std::uint32_t Y, double Incoming)
{
    if (TileIndex >= Reserved.size() || X >= CoverageTileTexels || Y >= CoverageTileTexels)
        return;

    if (Incoming <= 0.0)
        return;

    const std::size_t Writing = static_cast<std::size_t>(Y) * CoverageTileTexels + X;

    const double Current = static_cast<double>(Reserved[TileIndex][Writing]);

    if (Current <= 0.0)
        ++TouchedTexels;

    // 🔴 `22` §3's within-stroke rule, and the one line that decides it. `Over` saturates toward unity and is
    //    symmetric in its operands; addition neither saturates nor stays inside the interval the apply reads.
    const double Combined = CombineCoverage(CombineSpecification::Over,
                                            Current,
                                            Incoming > 1.0 ? 1.0 : Incoming);

    Reserved[TileIndex][Writing] = static_cast<float>(Combined > 1.0 ? 1.0 : Combined);
}

double StrokeSpace::Coverage(std::uint32_t TileIndex, std::uint32_t X, std::uint32_t Y) const
{
    if (TileIndex >= Reserved.size() || X >= CoverageTileTexels || Y >= CoverageTileTexels)
        return 0.0;

    return static_cast<double>(Reserved[TileIndex][static_cast<std::size_t>(Y) * CoverageTileTexels + X]);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

const std::vector<std::uint32_t>& StrokeSpace::TouchedCells() const { return ReservedCells; }

std::uint32_t StrokeSpace::ReservedCount() const
{
    return static_cast<std::uint32_t>(Reserved.size());
}

std::uint64_t StrokeSpace::TouchedTexelCount() const { return TouchedTexels; }

void StrokeSpace::Reclaim()
{
    for (const std::uint32_t CellIndex : ReservedCells)
    {
        if (CellIndex < TileOfCell.size())
            TileOfCell[CellIndex] = AbsentTile;
    }

    ReservedCells.clear();
    Reserved.clear();

    TouchedTexels = 0u;
}

}   // namespace Slate
