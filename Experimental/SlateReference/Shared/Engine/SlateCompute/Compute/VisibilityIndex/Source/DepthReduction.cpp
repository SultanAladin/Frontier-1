//============================================================================================================================================
//                                                            DEPTHREDUCTION.CPP
//============================================================================================================================================
// 🧩 Halving a display extent into a level chain, and the integer logarithm that picks the level one partition is tested at.

#include "SlateCompute/Compute/VisibilityIndex/Api/DepthReduction.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> DepthReduction::ConstructDepthReduction(std::uint32_t DisplayX, std::uint32_t DisplayY)
{
    if (DisplayX == 0u || DisplayY == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a display extent of zero reduces nothing" });

    if (DisplayX > DisplayExtentLimit || DisplayY > DisplayExtentLimit)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the display extent is above the declared ceiling" });

    // 📝 Reclaimed first rather than appended to, so a second Construct against a different extent cannot leave the
    //    previous chain's levels behind the new ones. A chain that grew instead of being replaced reports a level
    //    count the display extent never implied, and the selection below then returns a level nothing reduced into.
    Reclaim();

    std::uint32_t LevelX  = DisplayX;
    std::uint32_t LevelY = DisplayY;

    for (;;)
    {
        if (Levels.size() >= static_cast<std::size_t>(ReductionLevelLimit))
        {
            Reclaim();

            return Deliver<bool>::Refuse(
                { RefusalReason::ExtentExhausted, "the chain would carry more levels than the declared ceiling" });
        }

        ReductionLevel Derived;
        Derived.Width  = LevelX;
        Derived.Height = LevelY;
        Levels.push_back(Derived);

        if (LevelX == 1u && LevelY == 1u)
            break;

        // 📐 Rounding up on both ordinates. Nine texels halve to five and not to four: the odd column has depth
        //    recorded in it, and a level that dropped it is one a partition projecting onto the display's last
        //    column is tested against without its own depth ever having reached it.
        LevelX  = LevelX  > 1u ? (LevelX  + 1u) / 2u : 1u;
        LevelY = LevelY > 1u ? (LevelY + 1u) / 2u : 1u;
    }

    DerivedX  = DisplayX;
    DerivedY = DisplayY;
    ChainCurrent = true;

    return Deliver<bool>::Result(true);
}

void DepthReduction::Reclaim()
{
    Levels.clear();

    DerivedX  = 0u;
    DerivedY = 0u;
    ChainCurrent = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   LEVEL SELECTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<ReductionLevel> DepthReduction::Level(std::uint32_t LevelIndex) const
{
    if (LevelIndex >= static_cast<std::uint32_t>(Levels.size()))
        return Deliver<ReductionLevel>::Refuse({ RefusalReason::ContentUnsupported, "no such level" });

    return Deliver<ReductionLevel>::Result(Levels[LevelIndex]);
}

Deliver<std::uint32_t> DepthReduction::LevelOfExtent(std::uint32_t ProjectedX, std::uint32_t ProjectedY) const
{
    if (!ChainCurrent)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "no chain is derived" });

    // 📐 The same halving Construct performed, run over the projected extent instead of the display extent. Both
    //    round up, so the count arrived at here is exactly how many texels of that level the extent spans — which
    //    is what makes two by two the right terminator rather than an approximation of one.
    std::uint32_t RemainingX  = ProjectedX;
    std::uint32_t RemainingY = ProjectedY;
    std::uint32_t Selected        = 0u;

    const std::uint32_t Coarsest = static_cast<std::uint32_t>(Levels.size()) - 1u;

    while ((RemainingX > 2u || RemainingY > 2u) && Selected < Coarsest)
    {
        RemainingX  = (RemainingX  + 1u) / 2u;
        RemainingY = (RemainingY + 1u) / 2u;
        ++Selected;
    }

    return Deliver<std::uint32_t>::Result(Selected);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE READS
//------------------------------------------------------------------------------------------------------------------------

std::uint64_t DepthReduction::ChainTexels() const
{
    std::uint64_t Spanned = 0u;

    for (const ReductionLevel& Counted : Levels)
    {
        Spanned += static_cast<std::uint64_t>(Counted.Width)
                 * static_cast<std::uint64_t>(Counted.Height);
    }

    return Spanned;
}

std::uint32_t DepthReduction::LevelCount() const
{
    return static_cast<std::uint32_t>(Levels.size());
}

std::uint32_t DepthReduction::DisplayX() const
{
    return DerivedX;
}

std::uint32_t DepthReduction::DisplayY() const
{
    return DerivedY;
}

bool DepthReduction::ChainDerived() const
{
    return ChainCurrent;
}

}   // namespace Slate
