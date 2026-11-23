//============================================================================================================================================
//                                                             DOMAINSPACE.H
//============================================================================================================================================
// 🧩 Charts arranged into the unit domain at a common scale, separated by at least one apron.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "Foundation/NumericTolerance.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE GAP
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The least gap between two adjacent charts, in domain units.
/// note  🔴 `68` §5: at least `20` §1's `PhysicalTileApron`, measured at the maximum working extent. A gap
///        narrower than the apron means a tile's duplicated border reads texels belonging to a different chart,
///        and every chart edge in the textured result carries a fringe of a neighbouring chart's content.
/// note  🔴 Both constants are read from `Foundation/` as numbers. Nothing here consults `20`'s subdivision, its
///        residency or its promotion — that is the whole content of `00` §10 conflict 30's resolution, and it is
///        what makes this document precede `20` rather than depend on it.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr double DeclaredGap()
{
    return static_cast<double>(PhysicalTileApron) / static_cast<double>(MaximumWorkingEdge);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT IS ARRANGED
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One chart's planar extent, before any scale is applied.
/// tag   nonallocating, nonthrowing
struct ChartExtent
{
    double         Width        = 0.0;   // [-] - in the chart's own flattened units
    double         Height       = 0.0;   // [-] - in the chart's own flattened units
    std::uint32_t  ChartIndex = 0u;    // [-] - what the caller resolves the placement back to
};

/// 🧩 Where one chart sits in the unit domain, and at what scale.
/// tag   nonallocating, nonthrowing
struct ChartPlacement
{
    double         MinimumX   = 0.0;   // [-] - the domain's first axis
    double         MinimumY  = 0.0;   // [-] - its second
    double         Scale        = 1.0;   // [-] - applied to the chart's own flattened units
    std::uint32_t  ChartIndex = 0u;    // [-] - as supplied
    bool           Placed       = false; // [-] - the arrangement accepted it
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE ARRANGEMENT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The unit domain subdivided into disjoint chart areas.
/// note  ⚠️ `ChartArrangement` is the retired spelling. `Arrangement` is not in `SKILL-Naming`'s closed suffix
///        list, and what the mechanism does is subdivide the parametric domain — which is `Space`.
/// note  🔴 Charts are packed at a **common scale** by default: one texel of domain covers the same topology area
///        on every chart. A per-chart scale packs more tightly and makes one surface's texture finer than
///        another's on the same object, which the artist reads as an inconsistent brush.
/// tag   owning
class DomainSpace
{
public:

    /// 🧩 Arranges every chart into the unit domain, disjoint and gapped.
    /// in    Extents      [-]  one entry per chart, in its own flattened units
    /// in    CommonScale  [-]  true packs at one scale; false is `68` §10's open row and is rejected
    /// out   Result      [-]  refuses with ContentUnsupported for a per-chart scale, and with ExtentExhausted
    ///                         when no scale accepts every chart
    /// note  📐 The common scale is solved by bisection over a deterministic shelf packing: the ordering is by
    ///        unscaled height and then by ordinal, both scale-invariant, so the same charts arrange identically
    ///        on every machine and every run. A packing whose order depended on the scale would arrange
    ///        differently at each bisection step and the search would not converge to anything reproducible.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> Arrange(const std::vector<ChartExtent>& Extents, bool CommonScale);

    /// 🧩 The placements, in the order the extents were supplied.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<ChartPlacement>& Placements() const;

    /// 🧩 The fraction of the domain the charts cover — `86`'s `68` §5 measure.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    double Occupancy() const;

    /// 🧩 The scale the arrangement settled on.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    double SettledScale() const;

private:

    bool Feasible(const std::vector<std::uint32_t>&  Ordering,
                  const std::vector<ChartExtent>&    Extents,
                  double                             Scale,
                  std::vector<ChartPlacement>*       Recording) const;

    std::vector<ChartPlacement>  Placed;             // [-] - parallel to the supplied extents
    double                       Covered  = 0.0;     // [-] - fraction of the unit domain
    double                       Settled  = 0.0;     // [-] - the scale bisection settled on
};

// 📐 Every comparison here is over continuous extents, so the arrangement is Bounded. The chart ordinals it
//    carries are integers and are never derived from a measurement.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

}   // namespace Slate
