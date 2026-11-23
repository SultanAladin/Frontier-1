//============================================================================================================================================
//                                                          ANALYTICPROJECTION.H
//============================================================================================================================================
// 🧩 `70` — resolution-free sources resolved into a tile at promotion, at that tile's own reduction level.

#pragma once

#include "Foundation/Combination.h"
#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateCompute/Compute/SurfaceTileSpace/Api/SurfaceTileSpace.h"
#include "SlateDocument/Document/DecalProjection/Api/DecalProjection.h"
#include "SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h"
#include "SlateDocument/Document/SurfaceLayerSequence/Api/SurfaceLayerSequence.h"
#include "SlateDocument/Document/TilingSpecification/Api/TilingSpecification.h"
#include "SlateDocument/Document/VectorInterchange/Api/VectorInterchange.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHERE THE SOURCES LIVE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every content library one resolution reads, borrowed and never owned.
/// note  🔴 Gathered here because `70` is the one component that needs all five at once. `56` refers to a source
///        by ordinal alone and each library is held elsewhere; a resolution that reached for them individually
///        would acquire five edges where one declaration says the same thing.
/// note  ⚠️ 🚧 `52` declares one `VectorInterchange` and no index of them, so the outline run is supplied. When
///        `48`'s `ReferenceIndex` owns the document's external content it becomes that index's, and this field
///        becomes a read of it rather than a supplied run.
/// note  📝 A null library is not an error at construction. A document holding no tiling resolves every layer
///        without one, and refusing at construction would refuse a document for content it does not contain.
/// tag   nonallocating, nonthrowing
struct AnalyticSources
{
    const std::vector<VectorInterchange>*  Outlines   = nullptr;   // [-] - by source ordinal — 🚧 supplied
    const std::vector<ResolvedText>*       Texts      = nullptr;   // [-] - by source ordinal — 🚧 supplied
    const TypefaceInterchange*             Typeface   = nullptr;   // [-] - the one typeface a text reads
    const TilingIndex*                     Tilings    = nullptr;   // [-] - `54`'s declared patterns
    const PlacementIndex*                  Placements = nullptr;   // [-] - `72`'s declared placements
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT ONE POSITION HOLDS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Components one resolved sample carries. Four rather than a per-channel run, because a channel is a scalar
//    or a colour and `42`'s widest measure is three components with coverage beside it. A resolution wider than
//    this is a channel `42` does not declare.
inline constexpr std::uint32_t ResolvedComponentLimit = 4u;   // [-] - components one sample may carry

/// 🧩 One position's resolved value and the coverage it applies at.
/// note  🔴 Coverage is carried **beside** the value rather than folded into it. `56` §2's combination reads the
///        two separately — `CombineValue` takes the incoming coverage and `CombineCoverage` accumulates it — and
///        a resolution that premultiplied would make `Erase` reduce a value it is declared to leave alone.
/// tag   nonallocating, nonthrowing
struct ResolvedSample
{
    double         Component[ResolvedComponentLimit] = {};    // [-] - the resolved value, by component
    double         Coverage                            = 0.0;   // [-] - how strongly it applies here
    std::uint32_t  ComponentCount                      = 0u;    // [-] - components the source produced
    bool           SampleResolved                      = false; // [-] - the source answered at all
};

/// 🧩 One tile's resolved texels and what the resolution cost.
/// note  🔴 `EvaluationUnits` is charged to `20` §2.2's **second** measure and not to its first. Transfer volume
///        alone does not bound analytic resolution, because an analytically resolved tile transfers nothing and
///        the transfer measure truthfully reports zero while the device is fully occupied.
/// tag   owning
struct ResolvedTile
{
    std::vector<float>  Texels           = {};   // [-] - interleaved, ComponentCount per texel, apron included
    std::uint32_t       ComponentCount   = 1u;   // [-] - components per texel
    std::uint64_t       ResolvedRevision = 0u;   // [-] - the content revision it was resolved from
    std::uint64_t       EvaluationUnits  = 0u;   // [-] - charged to `20` §2.2's evaluation budget
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE LEVEL TOLERANCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The flattening tolerance one reduction level accepts, in domain units.
/// in    Level      [-]  the level being resolved; zero is finest
/// out   Tolerance  [-]  one texel of that level's working extent
/// note  🔴 `52` §4's rule, as a number. Tolerance is **relative to the level being resolved**, because a fixed
///        tolerance is wasteful at coarse levels and visibly polygonal at fine ones — and the coarse waste is the
///        expensive one, since `20` §3 keeps the coarsest levels permanently resident and re-resolves them for
///        every surface in the document.
/// note  📐 One texel rather than a fraction of one. The classification that follows the flattening is a
///        containment test at texel centres, so a chord deviating by less than one texel cannot move an answer;
///        subdividing further buys positions no sample reads.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr double ToleranceAtLevel(std::uint32_t Level)
{
    return 1.0 / static_cast<double>(CellsPerEdgeAt(Level) * PhysicalTileTexels);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Resolves `56`'s layer sequence into texels, at one position or over one whole tile.
/// note  🔴 `70` §1: resolution happens **at promotion, at the promoted level, into the tile**. It is not
///        resolved per rotation, not per pixel, and not into a resolution-independent representation that is then
///        sampled. A finer level is therefore a re-resolution rather than a magnification, which is the whole of
///        what "resolution-free source" means and the reason `20` §4 may evict what this produces.
/// note  🔴 The **whole** sequence is resolved, textured entries included. `20` §2.1 lists three reconstruction
///        sources and a mixed sequence needs all three composed; a resolver that skipped textured entries would
///        hand `20` a partial tile it has no route to complete. `PromotionScheduler::Estimate` already charges
///        both measures for exactly this case.
/// note  🔴 Every source resolves identically here and on the device — `00` §11 gates the agreement at Tier B
///        through `ParityRunner`. Where the two disagree the artist blames the preview, because the preview is
///        the thing that looks provisional, and the defect is then attributed to the wrong subsystem for as long
///        as it takes someone to check.
/// note  ⚠️ 🚧 Imagery is **rejected** rather than sampled. `50` retains a decoded image's original bytes and
///        performs `36` §3's conversion into the working space nowhere, so sampling here would return values in a
///        content-native space nothing declared. Refusing is the honest state of the tree; converting per sample
///        would violate `36` §3's once-at-intake rule at the one site that would make the violation invisible.
/// tag   owning
class AnalyticProjection
{
public:

    /// 🧩 Takes the content libraries every resolution reads.
    /// in    Supplied  [-]  borrowed; each outlives this component
    /// out   Result   [-]  delivers unconditionally; an absent library refuses only where a layer names it
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> ConstructAnalyticProjection(const AnalyticSources& Supplied);

    /// 🧩 The content revision one sequence currently stands at.
    /// in    Content  [-]  the surface's layer sequence
    /// out   Revision [-]  folded from every entry's own revision and every placement's
    /// note  🔴 `70` §2's comparison operand. `SurfaceTileSpace::Promote` tests it against what a resident tile
    ///        recorded, one integer per tile, and the answer is almost always "no work": a camera move advances
    ///        no counter and a moved owner advances none either, because `72` §1 stores a placing transform
    ///        relative to the surface it is attached to.
    /// note  ⚠️ 🚧 Folded over the **whole** sequence rather than over the entries a tile's extent reaches, so a
    ///        stroke in one corner of the domain re-resolves every analytic tile. That is conservative and
    ///        correct; sharpening it needs a per-entry coverage extent, which `56` §10 carries as open and
    ///        `PromotionScheduler::Estimate` records from the other side.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t ContentRevision(const SurfaceLayerSequence& Content) const;

    /// 🧩 Resolves one domain position through the whole layer sequence.
    /// in    Content         [-]  the surface's layer sequence
    /// in    Placements      [-]  where each of `42`'s channels sits among the components
    /// in    PositionX   [-]  the domain's first axis
    /// in    PositionY  [-]  its second
    /// in    Tolerance       [-]  the flattening tolerance, from `ToleranceAtLevel`
    /// in    ComponentCount  [-]  components the caller's texel carries
    /// out   Result         [-]  refuses with ContentUnsupported for a component count above the ceiling, and
    ///                            with whatever a source rejected
    /// note  🔴 This is the form `74` §1 and `82` §5 read on the host, and `ResolveTile` below walks it. One
    ///        routine and not two, because a preview resolved by a second implementation is a preview that
    ///        disagrees with the result in exactly the way `00` §11's gate exists to catch.
    /// note  ⚠️ Flattening happens **inside** this call, so resolving a tile through it directly would flatten
    ///        once per texel. `ResolveTile` flattens once per distinct outline before its walk and is the routine
    ///        a promotion calls; this one is for a single position and says so.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<ResolvedSample> ResolveAt(const SurfaceLayerSequence&           Content,
                                      const std::vector<ChannelPlacement>&  Placements,
                                      double                                PositionX,
                                      double                                PositionY,
                                      double                                Tolerance,
                                      std::uint32_t                         ComponentCount) const;

    /// 🧩 Resolves one whole tile, apron included.
    /// in    Content         [-]  the surface's layer sequence
    /// in    Placements      [-]  where each channel sits among the components
    /// in    Addressed       [-]  the cell the tile backs — level, along, across
    /// in    ComponentCount  [-]  components per texel
    /// out   Result         [-]  refuses with ContentUnsupported outside the level count or above the component
    ///                            ceiling, and with whatever a source rejected
    /// post  the returned run is `StoredTexelsPerEdge` squared texels, interleaved
    /// note  🔴 The **apron** is resolved along with the interior, per `20` §1. A tile whose apron were left
    ///        unwritten would read whatever the slot previously held at every filtered sample near its edge, and
    ///        the artist meets that as a fringe along every cell boundary in the domain.
    /// note  📝 Outlines are flattened once per distinct source before the texel walk, at the level's tolerance.
    ///        Flattening inside the walk would flatten eighteen thousand times per tile for a result that does
    ///        not vary across it.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<ResolvedTile> ResolveTile(const SurfaceLayerSequence&           Content,
                                      const std::vector<ChannelPlacement>&  Placements,
                                      CellAddress                           Addressed,
                                      std::uint32_t                         ComponentCount) const;

    /// 🧩 Whether every library a sequence names is present.
    /// out   Resolvable  [-]  false where a layer names a library this component was not given
    /// note  📝 Asked before a promotion charges its budget, so a refusal costs nothing rather than costing the
    ///        walk that discovers it.
    /// cost  🚩
    /// tag   api, nonthrowing
    bool SourcesPresent(const SurfaceLayerSequence& Content) const;

private:

    Deliver<ResolvedSample> ResolveEntryAt(const LayerSpecification&  Held,
                                           double                     PositionX,
                                           double                     PositionY,
                                           double                     Tolerance,
                                           std::uint32_t              ComponentCount) const;

    Deliver<ResolvedSample> ResolveOutlineAt(std::uint32_t  SourceIndex,
                                             double         SourceX,
                                             double         SourceY,
                                             double         Tolerance) const;

    Deliver<ResolvedSample> ResolveTextAt(std::uint32_t  SourceIndex,
                                          double         SourceX,
                                          double         SourceY,
                                          double         Tolerance) const;

    Deliver<ResolvedSample> ResolveTilingAt(std::uint32_t  TilingIndex,
                                            double         SourceX,
                                            double         SourceY,
                                            double         Tolerance,
                                            std::uint32_t  NestingDepth) const;

    Deliver<ResolvedSample> ResolvePlacedAt(std::uint32_t  PlacementIndex,
                                            double         PositionX,
                                            double         PositionY,
                                            double         Tolerance) const;

    /// 🧩 One outline flattened at one tolerance, held for the duration of one walk.
    /// note  🔴 Keyed by ordinal **and** tolerance, because a coarser level flattens the same outline more
    ///        loosely and the two runs are different runs. Keyed by ordinal alone, a tile at level three would
    ///        read the run level nought left behind and resolve a polyline eight times finer than it asked for.
    /// note  ⚠️ Held `mutable` because `ResolveAt` and `ResolveTile` are both const and neither amends content.
    ///        What is cached is a derivation of the supplied libraries, not a state of this component.
    /// tag   owning
    struct FlattenedOutline
    {
        std::vector<std::vector<PlanarPosition>>  Flattened     = {};    // [-] - one run per path
        std::uint32_t                             SourceIndex = 0u;    // [-] - by source ordinal
        double                                    Tolerance     = 0.0;   // [-] - the level it was flattened at
    };

    const std::vector<std::vector<PlanarPosition>>* Flattening(std::uint32_t  SourceIndex,
                                                               double         Tolerance) const;

    AnalyticSources  Supplied = {};   // [-] - borrowed; never owned

    mutable std::vector<FlattenedOutline>  Flattenings = {};   // [-] - cleared at each ResolveTile boundary
};

// 📐 Cell ordinals, lattice classification and the revision comparison are Exact; the flattening, the containment
//    coverage and every domain position are Bounded. `00` §3's transitivity rule folds them to the weaker.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded, PrecisionGuarantee::Exact);

}   // namespace Slate
