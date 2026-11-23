//============================================================================================================================================
//                                                            DOMAINSPACE.CPP
//============================================================================================================================================
// 🧩 Scale-invariant shelf ordering, bisection to the common scale, and the occupancy it reports.

#include "SlateCompute/Compute/DomainSpace/Api/DomainSpace.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE FEASIBILITY
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 Bisection steps. Forty-eight halvings of a double-precision interval reach the last representable place, so
//    the settled scale is the same number on every machine rather than the same number to a tolerance.
constexpr std::uint32_t BisectionLimit = 48u;   // [-] - halvings taken

}   // namespace

bool DomainSpace::Feasible(const std::vector<std::uint32_t>&  Ordering,
                           const std::vector<ChartExtent>&    Extents,
                           double                             Scale,
                           std::vector<ChartPlacement>*       Recording) const
{
    const double Gap = DeclaredGap();

    double ShelfY = Gap;
    double ShelfHeight = 0.0;
    double WalkingX = Gap;

    for (const std::uint32_t Index : Ordering)
    {
        const ChartExtent& Held = Extents[Index];

        const double Width  = Held.Width  * Scale;
        const double Height = Held.Height * Scale;

        // 📝 A chart wider or taller than the whole domain, gap included, is infeasible at this scale whatever
        //    the packing does. Testing it first stops the shelf walk from opening a row it can never fill.
        if (Width + 2.0 * Gap > 1.0 || Height + 2.0 * Gap > 1.0)
            return false;

        if (WalkingX + Width + Gap > 1.0)
        {
            ShelfY  += ShelfHeight + Gap;
            WalkingX  = Gap;
            ShelfHeight   = 0.0;
        }

        if (ShelfY + Height + Gap > 1.0)
            return false;

        if (Recording != nullptr)
        {
            (*Recording)[Index].MinimumX   = WalkingX;
            (*Recording)[Index].MinimumY  = ShelfY;
            (*Recording)[Index].Scale        = Scale;
            (*Recording)[Index].ChartIndex = Held.ChartIndex;
            (*Recording)[Index].Placed       = true;
        }

        WalkingX += Width + Gap;

        if (Height > ShelfHeight)
            ShelfHeight = Height;
    }

    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE ARRANGEMENT
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> DomainSpace::Arrange(const std::vector<ChartExtent>& Extents, bool CommonScale)
{
    Placed.assign(Extents.size(), ChartPlacement{});
    Covered = 0.0;
    Settled = 0.0;

    if (Extents.empty())
        return Deliver<bool>::Result(true);

    // 🚧 `68` §10 carries the per-chart scale as an open row and `68` §5's default stands regardless. Rejected
    //    rather than approximated, because a packing that silently chose its own per-chart scales would answer
    //    the open question by accident and nobody would know which answer shipped.
    if (!CommonScale)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a per-chart scale is `68` §10's open row and is not decided" });
    }

    double AccumulatedArea = 0.0;

    for (const ChartExtent& Held : Extents)
    {
        if (Held.Width <= 0.0 || Held.Height <= 0.0)
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a chart of no extent" });

        AccumulatedArea += Held.Width * Held.Height;
    }

    if (AccumulatedArea <= 0.0)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the charts cover no area" });

    // 📝 🔴 Ordered by unscaled height and then by ordinal. Both keys are scale-invariant, so the packing order
    //    is fixed before the bisection begins — a shelf order that changed with the scale would make each
    //    bisection step evaluate a different packing and the search would converge to nothing reproducible.
    std::vector<std::uint32_t> Ordering(Extents.size());

    for (std::uint32_t Index = 0u; Index < Extents.size(); ++Index)
        Ordering[Index] = Index;

    for (std::size_t Passed = 1u; Passed < Ordering.size(); ++Passed)
    {
        const std::uint32_t Held = Ordering[Passed];
        std::size_t         Slot = Passed;

        while (Slot != 0u)
        {
            const ChartExtent& Earlier = Extents[Ordering[Slot - 1u]];
            const ChartExtent& Later   = Extents[Held];

            const bool Precedes = Earlier.Height > Later.Height
                              || (Earlier.Height == Later.Height && Ordering[Slot - 1u] < Held);

            if (Precedes)
                break;

            Ordering[Slot] = Ordering[Slot - 1u];
            --Slot;
        }

        Ordering[Slot] = Held;
    }

    // 📐 No packing exceeds unit area, so the area-filling scale bounds every feasible scale from above. The
    //    bisection therefore begins at a scale that is provably not too small.
    double UpperScale = std::sqrt(1.0 / AccumulatedArea);
    double LowerScale = 0.0;

    if (!Feasible(Ordering, Extents, UpperScale, nullptr))
    {
        for (std::uint32_t Passed = 0u; Passed < BisectionLimit; ++Passed)
        {
            const double Middle = (LowerScale + UpperScale) * 0.5;

            if (Feasible(Ordering, Extents, Middle, nullptr))
                LowerScale = Middle;
            else
                UpperScale = Middle;
        }
    }
    else
    {
        LowerScale = UpperScale;
    }

    if (LowerScale <= 0.0 || !Feasible(Ordering, Extents, LowerScale, &Placed))
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "no scale accepts every chart" });

    Settled = LowerScale;
    Covered = AccumulatedArea * LowerScale * LowerScale;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

const std::vector<ChartPlacement>& DomainSpace::Placements() const { return Placed;  }
double                             DomainSpace::Occupancy() const  { return Covered; }
double                             DomainSpace::SettledScale() const { return Settled; }

}   // namespace Slate
