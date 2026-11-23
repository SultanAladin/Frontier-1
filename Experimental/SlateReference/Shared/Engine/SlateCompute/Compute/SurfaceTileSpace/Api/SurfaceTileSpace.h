//============================================================================================================================================
//                                                           SURFACETILESPACE.H
//============================================================================================================================================
// 🧩 Resolution-independent texturable surfaces — the cell subdivision, its residency, and the sample that never stalls.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "Foundation/NumericTolerance.h"
#include "SlateCompute/Compute/PromotionScheduler/Api/PromotionScheduler.h"
#include "SlateCompute/Compute/RequestQueue/Api/RequestQueue.h"
#include "SlateCompute/Compute/SurfaceDepot/Api/SurfaceDepot.h"
#include "SlateCompute/Compute/TileSpace/Api/TileSpace.h"
#include "SlateMath/Numeric/ReportSequence/Api/ReportSequence.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE REDUCTION CHAIN
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How many reduction levels the chain carries, from the finest down to a single tile.
/// note  ⚠️ `Mip` is banned. The chain is a reduction chain and levels are reduction levels — `20` §3.
/// cost  ✔️
constexpr std::uint32_t DerivedLevelCount()
{
    std::uint32_t Count = 1u;

    for (std::uint32_t Span = VirtualCellsPerEdge; Span > 1u; Span >>= 1)
        ++Count;

    return Count;
}

inline constexpr std::uint32_t ReductionLevelCount = DerivedLevelCount();   // [-] - seven, at 64 cells per edge

// 🔴 `20` §3 and §5: the coarsest levels are **permanently** resident, so every sample has an answer regardless
//    of what is promoted. Two levels is five tiles per surface, which is the price of the guarantee that
//    sampling never stalls — and a system without that guarantee has converted a memory problem into a
//    frame-time problem, which is `20` §2.1's whole objection.
inline constexpr std::uint32_t PermanentLevelCount = 2u;   // [-] - the coarsest two never evict

/// 🧩 Cells per edge at one reduction level; level zero is finest.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr std::uint32_t CellsPerEdgeAt(std::uint32_t Level)
{
    return Level < ReductionLevelCount ? (VirtualCellsPerEdge >> Level) : 1u;
}

/// 🧩 Where one level's cells begin in the single ordinal span every level shares.
/// note  📐 Levels are numbered into one span rather than addressed by a level-and-position pair, so a demand,
///        a residency record and an eviction candidate all carry one integer. The whole span is 5461 cells at
///        the declared subdivision, which is small enough to hold densely — so residency is an indexed read
///        rather than a search, and `70` §2's per-tile counter comparison costs one comparison.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr std::uint32_t LevelBaseIndex(std::uint32_t Level)
{
    std::uint32_t Base = 0u;

    for (std::uint32_t Passed = 0u; Passed < Level && Passed < ReductionLevelCount; ++Passed)
    {
        const std::uint32_t Span = CellsPerEdgeAt(Passed);
        Base += Span * Span;
    }

    return Base;
}

inline constexpr std::uint32_t CellIndexSpan = LevelBaseIndex(ReductionLevelCount);   // [-] - 5461

//------------------------------------------------------------------------------------------------------------------------
//                                                    ONE CELL ADDRESS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One cell, named by its level and its position within that level.
/// tag   nonallocating, nonthrowing
struct CellAddress
{
    std::uint32_t  Level  = 0u;   // [-] - zero is finest
    std::uint32_t  X  = 0u;   // [-] - the domain's first axis
    std::uint32_t  Y = 0u;   // [-] - its second
};

/// 🧩 The single ordinal one address occupies.
/// out   Result  [-]  refuses with ContentUnsupported outside the level or its cell span
/// cost  ✔️
/// tag   api, nonthrowing
Deliver<std::uint32_t> IndexOf(CellAddress Addressed);

/// 🧩 The address one ordinal names.
/// out   Result  [-]  refuses with ContentUnsupported outside the span
/// cost  ✔️
/// tag   api, nonthrowing
Deliver<CellAddress> AddressOf(std::uint32_t CellIndex);

/// 🧩 The cell one domain position falls in, at a declared level.
/// in    PositionX   [-]  the domain's first axis, in the unit square
/// in    PositionY  [-]  its second
/// out   Result         [-]  refuses with ContentUnsupported outside the level count
/// note  📝 A position outside the unit square is clamped rather than rejected. `68` §5 packs every chart
///        strictly inside the domain with a gap, so a position outside it is an apron read at the domain edge
///        and the edge cell is the right answer for it.
/// cost  ✔️
/// tag   api, nonthrowing
Deliver<std::uint32_t> IndexAt(std::uint32_t Level, double PositionX, double PositionY);

//------------------------------------------------------------------------------------------------------------------------
//                                                     ONE CELL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One cell's residency record.
/// note  🔴 This holds a slot ordinal and never a texel. `20` §5: no tile is the source of truth for any
///        content; `56` is, and a tile is a projection of it. A record that held content would make the
///        residency system a second place the artist's work lives, and the two would diverge at the first
///        eviction.
/// tag   nonallocating, nonthrowing
struct CellRecord
{
    std::uint32_t  SlotIndex      = AbsentTile;   // [-] - into `TileSpace`; AbsentTile when not resident
    std::uint64_t  ResolvedRevision = 0u;           // [-] - the content revision the tile was resolved from
    std::uint64_t  DemandedAt       = 0u;           // [-] - the last rotation a demand named it
    std::uint64_t  PromotedAt       = 0u;           // [-] - the rotation it became resident
    bool           Resident         = false;        // [-] - a slot is claimed for it
    bool           ApronWritten     = false;        // [-] - `20` §5's gate, per tile
    bool           Uncommitted      = false;        // [-] - holds texture no transaction has sealed — never evicted
    bool           Permanent        = false;        // [-] - a coarsest level; never evicted
};

/// 🧩 One surface's cell subdivision and residency records, dense across every reduction level.
/// note  ⚠️ Reserved in `SKILL-Naming`'s variable register under `BrickSpace`, which `00` §5 declares absent
///        along with every voxel mechanism. The spelling is free and `20` §2 claims it.
/// tag   owning
class CellSpace
{
public:

    /// 🧩 Builds the records and marks the permanently resident levels.
    /// post  every record is absent; the permanent ones are marked and await their promotion
    /// cost  🚩
    /// tag   api, nonthrowing
    void ConstructCells();

    /// 🧩 One record, for reading.
    /// out   Result  [-]  refuses with ContentUnsupported outside the span
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<const CellRecord*> Held(std::uint32_t CellIndex) const;

    /// 🧩 One record, for amending.
    /// out   Result  [-]  refuses with ContentUnsupported outside the span
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<CellRecord*> Amend(std::uint32_t CellIndex);

    /// 🧩 Every record, in ordinal order.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<CellRecord>& Records() const;

    std::uint32_t ResidentCount() const;
    std::uint32_t UncommittedCount() const;

    /// 🧩 Declares one cell resident against a claimed slot.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeclareResident(std::uint32_t CellIndex, std::uint32_t SlotIndex, std::uint64_t RecordingIndex);

    /// 🧩 Declares one cell absent, surrendering its slot ordinal to the caller.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeclareAbsent(std::uint32_t CellIndex);

private:

    std::vector<CellRecord>  CellRecords;              // [-] - dense, CellIndexSpan entries
    std::uint32_t            ResidentCells    = 0u;    // [-]
    std::uint32_t            UncommittedCells = 0u;    // [-]

    friend class SurfaceTileSpace;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   WHAT A SAMPLE GETS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The cell a sample actually resolved to, which may be coarser than the one it asked for.
/// note  🔴 `20` §2.1 and §5: sampling a non-resident cell returns the coarsest resident level and **never
///        stalls**. The level actually resolved is carried back so the caller knows how coarse its answer is —
///        which `82` presents and `16` §3.1 does not care about.
/// tag   nonallocating, nonthrowing
struct SampledCell
{
    std::uint32_t  CellIndex    = 0u;           // [-] - the cell resolved
    std::uint32_t  SlotIndex    = AbsentTile;   // [-] - the tile backing it
    std::uint32_t  ResolvedLevel  = 0u;           // [-] - coarser than or equal to the level asked for
    std::uint32_t  RequestedLevel = 0u;           // [-] - what the caller asked for
    bool           DemandRecorded = false;        // [-] - the finer level was demanded
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE RESIDENCY
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One texturable surface's residency — the reduction chain, its cells, and the tiles backing them.
/// note  🔴 `20`'s whole claim: a stroke is recorded against the surface's parametric domain, not against a
///        texel population, and the texels backing it are a residency decision made independently and revisable
///        without touching the stroke. Every routine here addresses the domain; none addresses a texel.
/// note  🔴 Nothing in this component reconstructs a tile. Promotion claims a slot and charges a budget; the
///        transfer belongs to `06` and the analytic resolution to `70`, and both write the apron before calling
///        `DeclareApronWritten`. A residency system that reconstructed would be one that knew what content is.
/// note  🚧 The byte offsets `TileSpace` yields index a device extent `06`'s `ByteSpace` will claim. That
///        component is unbuilt, so nothing here claims anything — the model is complete and its backing is not.
/// tag   owning
class SurfaceTileSpace
{
public:

    /// 🧩 Constructs one surface's residency.
    /// in    SurfaceIndex  [-]  which surface demands name
    /// in    BytesPerTexel   [B]  the channel set this surface writes, as a width
    /// in    SlotLimit     [-]  tiles the backing extent holds
    /// out   Result         [-]  refuses with ContentUnsupported for a width of zero, and with ExtentExhausted
    ///                            when the ceiling cannot hold the permanently resident levels
    /// post  the permanent levels are resident and their aprons are owed
    /// note  🔴 The ceiling is checked against the permanent levels before anything is claimed. A surface whose
    ///        backing cannot hold its own guarantee is one where sampling stalls, and refusing at construction
    ///        is the only place that failure can still be attributed to its cause.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> ConstructSurfaceTiles(std::uint32_t SurfaceIndex, std::uint32_t BytesPerTexel, std::uint32_t SlotLimit);

    /// 🧩 Resolves a domain position at a declared level, demanding what is not resident.
    /// in    Level            [-]  the level wanted; zero is finest
    /// in    PositionX    [-]  the domain's first axis
    /// in    PositionY   [-]  its second
    /// in    RecordingIndex  [-]  the rotation sampling
    /// in    Requesting       [-]  where the demand is recorded
    /// out   Result          [-]  refuses with ContentUnsupported outside the level count, and with
    ///                             HostDenied before Construct has delivered
    /// post  🔴 a resident cell is always resolved; the permanent levels guarantee it
    /// note  🔴 `20` §5: this never stalls. It walks from the requested level toward coarser until a resident
    ///        record is found, records a demand for the level that was wanted, and returns. A sample that
    ///        waited for a promotion would make every camera cut a hitch.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<SampledCell> Sample(std::uint32_t Level,
                                double        PositionX,
                                double        PositionY,
                                std::uint64_t RecordingIndex,
                                RequestQueue& Requesting);

    /// 🧩 Resolves a domain position from the permanently resident levels alone, demanding nothing.
    /// out   Result  [-]  refuses with HostDenied before Construct has delivered
    /// note  🔴 `16` §3.1 ③'s read, and the reason it is a separate routine. A visibility recording that could
    ///        stall on residency has made the whole image wait for a leaf, so the cutout coverage test reads
    ///        only what is guaranteed and demands nothing at all.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<SampledCell> SampleGuaranteed(double PositionX, double PositionY) const;

    /// 🧩 Declares whether one cell holds texture no transaction has sealed.
    /// out   Result  [-]  refuses with ContentUnsupported outside the span, and with HostDenied when a
    ///                     non-resident cell is declared uncommitted
    /// note  🔴 `20` §5 and `22` §4: no tile holding uncommitted texture is evicted. `22` declares it at the
    ///        stroke's Open and withdraws it at Seal, at which point the texture is in `56` and the tile is a
    ///        projection again.
    /// note  ⚠️ `82`'s speculative extents never call this. `22` §4.1 declares that a speculative extent never
    ///        blocks eviction, and this is the only thing in the engine that blocks one — so the gate holds by
    ///        there being exactly one door and `82` not walking through it.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareUncommitted(std::uint32_t CellIndex, bool UncommittedDeclared);

    /// 🧩 Considers one demanded cell for promotion, against the rotation's budget.
    /// in    CellIndex      [-]  the cell
    /// in    Costing          [-]  what promoting it would cost, from `Estimate`
    /// in    ContentRevision  [-]  the revision the tile would be resolved from
    /// in    Scheduling       [-]  the rotation's budget and eviction ordering
    /// in    RecordingIndex  [-]  the rotation promoting
    /// out   Result          [-]  refuses with ContentUnsupported outside the span, and with HostDenied
    ///                             before Construct has delivered
    /// post  a promoted or re-resolved cell owes its apron; the caller writes it and declares it
    /// note  🔴 `70` §2's comparison, discharged here: a resident cell whose recorded revision equals the
    ///        incoming one is `AlreadyResident` and nothing is charged. The comparison is one integer test at
    ///        Exact, per tile, and its answer is almost always "no work" — a camera move advances no counter and
    ///        a moved owner advances no counter, because a placement's transform is stored relative to its
    ///        surface.
    /// note  🔴 A budget refusal and an eviction refusal both resolve to `Deferred`, never to a failure. `20`
    ///        §2.2: deferral is normal operation, and `86` §5 keeps it a measure rather than a report.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<PromotionVerdict> Promote(std::uint32_t       CellIndex,
                                          const PromotionCost& Costing,
                                          std::uint64_t        ContentRevision,
                                          PromotionScheduler&  Scheduling,
                                          std::uint64_t        RecordingIndex);

    /// 🧩 Declares one promoted cell's apron written.
    /// out   Result  [-]  refuses with ContentUnsupported outside the span, and with HostDenied for a
    ///                     non-resident cell
    /// note  🔴 `20` §5: every resident tile carries a written apron. Declared rather than assumed, so a
    ///        promotion path that forgot to write one is caught by `ResidencyValid` rather than by an artist
    ///        finding a seam in a textured result.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareApronWritten(std::uint32_t CellIndex);

    /// 🧩 Evicts one resident cell, releasing its slot into quarantine.
    /// out   Result  [-]  refuses with ContentUnsupported for a permanent, uncommitted or absent cell
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Evict(std::uint32_t CellIndex, std::uint64_t RecordingIndex);

    /// 🧩 Reclaims quarantined slots whose release is older than the recording slot count.
    /// out   Reclaimed  [-]  how many slots became free
    /// note  Called once per rotation, on the tick, before anything is promoted. Reclaiming after promotion
    ///        would make the rotation's first promotions evict against a index that is about to free itself.
    /// cost  🚩
    /// tag   api, nonthrowing
    std::uint32_t Reconcile(std::uint64_t RecordingIndex);

    /// 🧩 Declares this surface's residency measures — never a report.
    /// note  🔴 `86` §5: promotion deferred against budget is a **Measure** and discretionary exhaustion is not
    ///        reported at all. A coarse tile resolving under the cursor is ordinary operation and reporting it
    ///        would mean the register is never quiet, which teaches the artist to ignore it.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Report(MeasureIndex& Measured, const PromotionScheduler& Scheduling, TickPoint Sampled) const;

    const CellSpace&    Cells() const;
    const TileSpace&    Tiles() const;
    SurfaceDepot&       Depot();
    const SurfaceDepot& Depot() const;

    std::uint32_t SurfaceIndex() const;
    std::uint64_t StoredBytesPerTile() const;

    /// 🧩 🔍 Whether every invariant `20` §5 states holds right now.
    /// note  Checks that every permanent cell is resident, that no two resident cells hold one slot, that no
    ///        uncommitted cell is absent, and that a tile promoted before this rotation carries its apron.
    /// cost  🔴
    /// tag   api, nonallocating, nonthrowing
    bool ResidencyValid(std::uint64_t RecordingIndex) const;

private:

    Deliver<std::uint32_t> ReserveOrEvict(PromotionScheduler& Scheduling, std::uint64_t RecordingIndex);

    CellSpace      Cells_;                       // [-] - one record per cell of every level
    TileSpace      Tiles_;                       // [-] - the slot index behind them
    SurfaceDepot   Depot_;                       // [-] - the evictable derived artefacts
    std::uint32_t  Index        = 0u;          // [-] - which surface demands name
    bool           Constructed    = false;       // [-] - Construct has delivered
};

// 📐 Cell identity, slot identity and the revision comparison are Exact; the domain position that resolves to a
//    cell is Bounded, per `02` §3.2's surface space at 32 bits. The component claims Bounded, because `00` §3's
//    transitivity rule forbids claiming the stronger guarantee over a body that also produces the weaker one.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded, PrecisionGuarantee::Exact);

}   // namespace Slate
