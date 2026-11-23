//============================================================================================================================================
//                                                         IMPRESSIONSEQUENCE.H
//============================================================================================================================================
// 🧩 A stroke as ordered impressions against the parametric domain — resampled by arrival, deferred never dropped, sealed once.

#pragma once

#include "Foundation/Combination.h"
#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "Foundation/NumericTolerance.h"
#include "SlateCompute/Compute/RequestQueue/Api/RequestQueue.h"
#include "SlateCompute/Compute/StrokeSpace/Api/StrokeSpace.h"
#include "SlateCompute/Compute/SurfaceTileSpace/Api/SurfaceTileSpace.h"
#include "SlateDocument/Document/BrushSpecification/Api/BrushSpecification.h"
#include "SlateDocument/Document/RevisionSequence/Api/RevisionSequence.h"
#include "SlateDocument/Document/SurfaceLayerSequence/Api/SurfaceLayerSequence.h"
#include "SlateMath/Platform/InputExchange/Api/InputExchange.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE PAINTING LEVEL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which reduction level a declared working extent textures at.
/// in    WorkingExtent  [px] texels per edge of the surface's authored content
/// out   Result        [-]  refuses with ContentUnsupported when no level has that extent
/// note  🔴 `22` §2: texture is authored content and lands at **one** level — the level whose texel grid is the
///        working extent. Resolving a stroke against a coarser level is not a lower-quality stroke, it is a
///        stroke recorded at a resolution the artist did not choose, and no later promotion recovers it.
/// note  📐 Level L holds `VirtualCellsPerEdge >> L` cells of `CoverageTileTexels` each, so its extent is
///        `MaximumWorkingEdge >> L`. The level is therefore the base-two logarithm of the ratio, and an extent
///        that is not one of the seven is rejected rather than rounded to the nearest.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
Deliver<std::uint32_t> TexturingLevelOf(std::uint32_t WorkingExtent);

// 📝 `ChannelPlacement` is `56`'s, declared beside `TexturedContent` because it describes that content's layout
//    and `70` reads the same declaration from below. It arrives through `SurfaceLayerSequence.h`.

//------------------------------------------------------------------------------------------------------------------------
//                                                   ONE IMPRESSION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One resolved brush impression on the surface — `22` §1 ④.
/// note  ⚠️ `Stamp` is banned by `00` §8 and the substitution is not a euphemism. A stamp is applied once at a
///        place; an impression is one term of an ordered accumulation, and the accumulation is what `22` §3 is
///        about. The two words describe different mechanisms and only one of them is Slate's.
/// note  🔴 A domain position and **never** a texel. `20`'s whole claim is that a stroke addresses the domain
///        and the texels backing it are a residency decision made independently — an impression carrying a
///        texel would make a stroke unrecordable at any other working extent.
/// tag   nonallocating, nonthrowing
struct ImpressionSample
{
    double         PositionX     = 0.0;   // [-]  - the domain's first axis
    double         PositionY    = 0.0;   // [-]  - its second
    double         PathDistance      = 0.0;   // [-]  - accumulated along the resampled path
    ResolvedBrush  Resolved          = {};    // [-]  - `58`'s, at this impression's ordinal
    std::uint32_t  ImpressionIndex = 0u;    // [-]  - position within the stroke; `58` §6 seeds from it
    bool           ResolutionOwed    = true;  // [-]  - not yet accumulated; a deferral leaves it standing
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    ONE ARRIVAL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One pointer sample, with the domain position `74` resolved it to.
/// note  🚧 The domain position is **supplied**. `74` §1 resolves the pointer on the host against `40`'s
///        subdivision and is unbuilt; declaring the edge instead would put `40` and `74` in this document's
///        Upstream, and `00` §11 gates that a declared edge is a real read. The same choice `28` made for the
///        sun direction, for the same reason.
/// note  🔴 `SurfaceResolved` is false where the pointer left the surface, and the path **breaks** there rather
///        than interpolating across the gap. A stroke that leaves an object and returns must not texture a line
///        between the two points, and the artist has no way to undo half of one stroke.
/// tag   nonallocating, nonthrowing
struct StrokeArrival
{
    PointerSample  Incoming        = {};      // [-] - `04` §3's sample, arrival-timestamped
    double         PositionX   = 0.0;     // [-] - the domain's first axis, from `74`
    double         PositionY  = 0.0;     // [-] - its second
    bool           SurfaceResolved = false;   // [-] - the pointer met the surface at all
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT A STROKE DECLARES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Everything a stroke needs before its first impression.
/// tag   owning
struct StrokeDeclaration
{
    std::vector<ChannelPlacement>  Placements     = {};      // [-]  - one per brush channel — never derived
    LayerIdentity                  Subject        = {};      // [-]  - the `56` entry the stroke textures into
    std::uint32_t                  SurfaceIndex = 0u;      // [-]  - which residency demands name
    std::uint32_t                  WorkingExtent  = 0u;      // [px] - texels per edge of the textured entry
    std::uint32_t                  ComponentCount = 1u;      // [-]  - components per texel the entry holds
    std::uint32_t                  StrokeSeed     = 1u;      // [-]  - `58` §6; recorded with the transaction
    bool                           Speculative    = false;   // [-]  - `22` §4.1; never commits, never pins a tile
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 WHAT ONE RESOLVE DID
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How one rotation's resolution ended.
/// note  🔴 A deferral is a **count**, not a report. `20` §2.2 rules the same for a deferred promotion and the
///        reasoning is identical one layer up: an impression waiting for a tile is ordinary operation, and a
///        register that appended one per impression per rotation is a register nobody reads.
/// tag   nonallocating, nonthrowing
struct ResolvedRun
{
    std::uint32_t  ResolvedCount = 0u;   // [-] - impressions accumulated this rotation
    std::uint32_t  DeferredCount = 0u;   // [-] - impressions whose cells are not resident at the texturing level
    std::uint32_t  PendingCount  = 0u;   // [-] - impressions still owed resolution after this rotation
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT A SEAL PRODUCES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One committed stroke's inverse, bounded by the extents the stroke touched.
/// note  🔴 `22` §4 and `10` §2.3: the inverse records the prior contents of the **touched extents only**. A
///        surface-wide snapshot per stroke makes a texturing session's revision sequence proportional to the
///        surface times the stroke count, and the artist runs out of memory rather than out of undo.
/// note  📝 Whole tiles are recorded rather than only the texels whose coverage was non-zero. The tile is
///        already the granularity the stroke claimed, and a sparse inverse would have to carry a texel mask
///        that costs more than the texels it excludes at any realistic coverage.
/// tag   owning
struct SealedStroke
{
    std::vector<std::uint32_t>  TouchedCells    = {};   // [-]  - cell ordinals the stroke wrote
    std::vector<float>          PriorTexels     = {};   // [-]  - one whole tile per touched cell, interleaved
    StrokeBrushRecord           Recorded        = {};   // [-]  - `58` §7's parameters, seed included
    LayerIdentity               Subject         = {};   // [-]  - the entry the stroke wrote
    std::uint32_t               TexturingLevel   = 0u;   // [-]  - the level the extents address
    std::uint32_t               ComponentCount  = 1u;   // [-]  - components per texel
    std::uint32_t               ImpressionCount = 0u;   // [-]  - impressions the stroke committed
};

/// 🧩 Replays one sealed stroke's inverse, returning the touched extents to what stood before it.
/// in    Sealed   [-]  as `Seal` produced it
/// in    Content  [-]  the sequence the stroke wrote into
/// out   Result  [-]  refuses with IdentityStale when the entry no longer resolves, and with
///                     ContentUnsupported when its extent no longer matches the recorded one
/// note  🔴 This is the operation `RevisionSequence` replays at a backward scrub. It is not a second forward
///        path with the values negated — a combination like `Multiply` has no negation, and `Erase` has no
///        inverse expressible as an erase.
/// cost  🔴
/// tag   api, nonthrowing
Deliver<bool> Restore(const SealedStroke& Sealed, SurfaceLayerSequence& Content);

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE STROKE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One stroke — its resampled path, its impressions, its accumulation, and the one transaction it seals.
/// note  🔴 `10` §2.4's lifecycle, unamended: Open holds the declaration, Amend resamples and records nothing,
///        Abandon ends with no effect, Seal enters **one** transaction. A stroke recording a transaction per
///        pointer sample fills the revision sequence with positions the artist never intended to stop at, and
///        undo then steps back one pixel at a time.
/// note  🔴 Path resampling reads `04` §3's **arrival** timestamps and never consumption times. A path
///        resampled against consumption has the display rate baked into it, so the same physical gesture
///        produces a different stroke on a machine that presents at a different rate.
/// tag   owning
class ImpressionSequence
{
public:

    // 🚧 `22` §7 carries the impression count as unbounded and it cannot be. The ceiling bounds one stroke's
    //    storage and its resolution cost; reaching it refuses the amendment rather than dropping impressions,
    //    so the caller seals and reopens and nothing the artist drew is lost. Read by this unit alone.
    static constexpr std::uint32_t ImpressionLimit = 65536u;   // [-] - impressions one stroke may carry

    /// 🧩 Opens a stroke against a declared brush.
    /// in    Declaring  [-]  the surface, the entry, the packing and the seed
    /// in    Brushed    [-]  `58`'s declaration; its parameters are recorded, not referenced — `58` §7
    /// out   Result    [-]  refuses with HostDenied when a stroke is already open, with ContentUnsupported for
    ///                       a working extent that is no reduction level, for a channel placement outside the
    ///                       entry's components, and for a shape source that is not yet resolvable
    /// post  nothing is recorded; the accumulation is empty and the path has not begun
    /// note  🚧 An imagery or outline shape refuses here rather than falling back to an analytic profile. The
    ///        fallback would be a stroke that does not have the silhouette the artist selected, and `58` §8
    ///        promises the preview and the committed impression share one shape.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Open(const StrokeDeclaration& Declaring, const BrushSpecification& Brushed);

    /// 🧩 Accepts one arrival, resampling the path and emitting whatever impressions it reached.
    /// in    Incoming  [-]  the pointer sample, its arrival stamp, and `74`'s domain position
    /// out   Result   [-]  refuses with HostDenied before Open, and with ExtentExhausted at the ceiling
    /// post  the path advanced; zero or more impressions were appended, each owed resolution
    /// note  🔴 Resampling is in the **domain** at the brush's own spacing, so a stroke drawn slowly and one
    ///        drawn quickly place the same impressions. `58` §5 makes spacing relative to the extent, so a
    ///        brush resized keeps its character rather than becoming a dotted line.
    /// note  📐 Spacing for the next impression comes from the brush resolved at the **previous** one. It
    ///        cannot come from the next, because the next impression's dynamics are read at a position the
    ///        walk has not reached — and a spacing that depended on where it lands has no fixed point.
    /// note  📐 An arrival within `SpacingArrivalTolerance` of the next impression has reached it. Both the
    ///        walked distance and the spacing are accumulated, so an exact comparison decides the segment's
    ///        last impression on the residue of the additions rather than on the path the artist drew.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Amend(const StrokeArrival& Incoming);

    /// 🧩 Resolves whatever impressions the residency now accepts, demanding what it does not.
    /// in    Residency        [-]  the surface's cells and tiles
    /// in    Requesting       [-]  where a demand for a non-resident cell is recorded
    /// in    RecordingIndex  [-]  the rotation resolving
    /// out   Result          [-]  refuses with HostDenied before Open
    /// post  🔴 a deferred impression stays owed; nothing is dropped and nothing resolves coarse
    /// note  🔴 `22` §2: an impression touching a non-resident cell **demands and defers**. It is not resolved
    ///        against the coarse level, because texture applied at the wrong resolution is authored content that
    ///        is permanently wrong — unlike a display sample, which is merely briefly coarse.
    /// note  ⚠️ That rule binds **textured** impressions only. Placed content and tiling resolve through `70` at
    ///        whatever level is promoted and may refine later, because for them the authored thing is the
    ///        source and the transform rather than the texels. A speculative stroke follows the derived rule
    ///        for the same reason: nothing speculative is authored, so nothing speculative can be wrong.
    /// note  🔴 Deferred impressions do not block the ones after them. `StrokeSpace`'s accumulation is `Over`,
    ///        which is symmetric, so an impression resolving two rotations late lands exactly where it would
    ///        have — and the artist sees most of a stroke immediately instead of none of it.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<ResolvedRun> Resolve(SurfaceTileSpace& Residency,
                                 RequestQueue&     Requesting,
                                 std::uint64_t     RecordingIndex);

    /// 🧩 Ends the stroke with no effect, releasing every tile it pinned.
    /// post  no transaction was recorded; the accumulation is reclaimed
    /// cost  🚩
    /// tag   api, nonthrowing
    void Abandon(SurfaceTileSpace& Residency);

    /// 🧩 Ends the stroke, applying it once and sealing one transaction.
    /// in    Content    [-]  the layer sequence holding the textured entry
    /// in    Revised    [-]  where the transaction is recorded
    /// in    Residency  [-]  the tiles pinned by the stroke, released here
    /// in    SealedAt   [ns] the arrival stamp the transaction carries
    /// out   Result    [-]  refuses with HostDenied before Open and for a speculative stroke, with
    ///                       IdentityStale for an entry that no longer resolves, and with ContentUnsupported
    ///                       when the entry's extent no longer matches the stroke's
    /// post  🔴 the accumulated coverage was applied **once**; every touched cell is committed again
    /// note  🔴 One stroke is one transaction even where the brush wrote several channels — `22` §5. Undo
    ///        restores base colour and roughness together, which is what the artist performed; two
    ///        transactions would make them undo separately and neither in isolation is a thing they did.
    /// note  ⚠️ A speculative stroke refuses. `22` §4.1: a speculative extent never commits, and a Seal that
    ///        quietly succeeded for one would put a preview in the revision sequence.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<SealedStroke> Seal(SurfaceLayerSequence& Content,
                               RevisionSequence&     Revised,
                               SurfaceTileSpace&     Residency,
                               std::uint64_t         SealedAt);

    /// 🧩 Discards the accumulation without ending the stroke — a speculative extent's per-slot reclaim.
    /// note  🔴 `22` §4.1: a speculative extent is discarded and re-resolved each rotation. Refuses for a
    ///        committed stroke, whose accumulation is the only record of what has been textured so far.
    /// out   Result  [-]  refuses with HostDenied for a committed stroke
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> ReclaimSpeculative();

    const std::vector<ImpressionSample>& Impressions() const;
    const StrokeSpace&                   Accumulation() const;

    std::uint32_t ImpressionCount() const;
    std::uint32_t PendingCount() const;
    std::uint32_t TexturingLevel() const;
    double        PathLength() const;
    bool          StrokeOpen() const;
    bool          SpeculativeDeclared() const;

private:

    void          Emit(double PositionX, double PositionY,
                       double TangentX,  double TangentY,
                       const ResolvedAxes& Axes, double PathDistance);
    ResolvedAxes  ProjectAxes(const PointerSample& Incoming,
                              double TangentX, double TangentY,
                              double Speed, double PathDistance) const;
    Deliver<bool> ResolveOne(ImpressionSample& Impressing,
                             SurfaceTileSpace& Residency,
                             RequestQueue&     Requesting,
                             std::uint64_t     RecordingIndex);

    StrokeDeclaration              Declared            = {};      // [-] - as Open validated it
    BrushSpecification             Brush               = {};      // [-] - held by value; `58` §7
    StrokeBrushRecord              Recorded            = {};      // [-] - what the transaction carries
    std::vector<ImpressionSample>  Sequenced;                     // [-] - in stroke order; never reordered
    StrokeSpace                    Accumulated;                   // [-] - coverage, per touched cell
    CombineSpecification           Combination         = CombineSpecification::Over;
    double                         LastX           = 0.0;     // [-] - the last arrival's domain position
    double                         LastY          = 0.0;     // [-]
    TickPoint                      LastArrival         = {};      // [ns] - its arrival stamp
    double                         TravelledDistance   = 0.0;     // [-] - path length up to the last arrival
    double                         PendingDistance     = 0.0;     // [-] - walked since the last impression
    double                         NextSpacing         = 0.0;     // [-] - to the next impression, from the last
    std::uint32_t                  Level               = 0u;      // [-] - the texturing level
    std::uint32_t                  ResolvedTotal       = 0u;      // [-] - impressions accumulated this stroke
    bool                           OpenDeclared        = false;   // [-] - Open delivered, Seal has not
    bool                           PathBegun           = false;   // [-] - one arrival has been accepted
    bool                           PathBroken          = false;   // [-] - the pointer left the surface
};

// 📐 Impression ordinals, cell ordinals and the variation sequence are Exact; domain positions, coverage and
//    every dynamic progression are Bounded, per `02` §3.2's surface space at 32 bits. The component claims
//    Bounded, because `00` §3's transitivity rule forbids claiming the stronger guarantee over a body that also
//    produces the weaker one.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded, PrecisionGuarantee::Exact);

}   // namespace Slate
