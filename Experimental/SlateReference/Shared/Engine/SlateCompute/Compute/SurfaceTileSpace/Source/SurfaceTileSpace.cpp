//============================================================================================================================================
//                                                          SURFACETILESPACE.CPP
//============================================================================================================================================
// 🧩 The coarsening walk that never stalls, the revision comparison, and promotion that evicts only what it may.

#include "SlateCompute/Compute/SurfaceTileSpace/Api/SurfaceTileSpace.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CELL ADDRESSING
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> IndexOf(CellAddress Addressed)
{
    if (Addressed.Level >= ReductionLevelCount)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "no such reduction level" });

    const std::uint32_t Span = CellsPerEdgeAt(Addressed.Level);

    if (Addressed.X >= Span || Addressed.Y >= Span)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "no such cell at that level" });

    return Deliver<std::uint32_t>::Result(LevelBaseIndex(Addressed.Level) + Addressed.Y * Span
                                         + Addressed.X);
}

Deliver<CellAddress> AddressOf(std::uint32_t CellIndex)
{
    if (CellIndex >= CellIndexSpan)
        return Deliver<CellAddress>::Refuse({ RefusalReason::ContentUnsupported, "no such cell" });

    for (std::uint32_t Level = 0u; Level < ReductionLevelCount; ++Level)
    {
        const std::uint32_t Span = CellsPerEdgeAt(Level);
        const std::uint32_t Base = LevelBaseIndex(Level);

        if (CellIndex >= Base + Span * Span)
            continue;

        CellAddress Addressed;
        Addressed.Level  = Level;
        Addressed.X  = (CellIndex - Base) % Span;
        Addressed.Y = (CellIndex - Base) / Span;

        return Deliver<CellAddress>::Result(Addressed);
    }

    return Deliver<CellAddress>::Refuse({ RefusalReason::ContentUnsupported, "no such cell" });
}

Deliver<std::uint32_t> IndexAt(std::uint32_t Level, double PositionX, double PositionY)
{
    if (Level >= ReductionLevelCount)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "no such reduction level" });

    const std::uint32_t Span = CellsPerEdgeAt(Level);
    const double        Edge = static_cast<double>(Span);

    // 📝 Clamped rather than rejected. `68` §5 packs every chart strictly inside the domain with an apron's gap,
    //    so a position outside the unit square is an apron read at the domain edge — and the edge cell is what
    //    it should read from.
    double XCell  = PositionX  * Edge;
    double YCell = PositionY * Edge;

    XCell  = XCell  < 0.0 ? 0.0 : (XCell  > Edge - 1.0 ? Edge - 1.0 : XCell);
    YCell = YCell < 0.0 ? 0.0 : (YCell > Edge - 1.0 ? Edge - 1.0 : YCell);

    CellAddress Addressed;
    Addressed.Level  = Level;
    Addressed.X  = static_cast<std::uint32_t>(XCell);
    Addressed.Y = static_cast<std::uint32_t>(YCell);

    return IndexOf(Addressed);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE CELLS
//------------------------------------------------------------------------------------------------------------------------

void CellSpace::ConstructCells()
{
    CellRecords.assign(CellIndexSpan, CellRecord{});
    ResidentCells    = 0u;
    UncommittedCells = 0u;

    // 🔴 The coarsest `PermanentLevelCount` levels are marked here and never unmarked. `20` §3: every sample has
    //    an answer regardless of what is promoted, and that guarantee is a property of the marking rather than
    //    of any eviction policy being careful.
    for (std::uint32_t Level = ReductionLevelCount - PermanentLevelCount; Level < ReductionLevelCount; ++Level)
    {
        const std::uint32_t Span = CellsPerEdgeAt(Level);
        const std::uint32_t Base = LevelBaseIndex(Level);

        for (std::uint32_t Index = 0u; Index < Span * Span; ++Index)
            CellRecords[Base + Index].Permanent = true;
    }
}

Deliver<const CellRecord*> CellSpace::Held(std::uint32_t CellIndex) const
{
    if (CellIndex >= CellRecords.size())
        return Deliver<const CellRecord*>::Refuse({ RefusalReason::ContentUnsupported, "no such cell" });

    return Deliver<const CellRecord*>::Result(&CellRecords[CellIndex]);
}

Deliver<CellRecord*> CellSpace::Amend(std::uint32_t CellIndex)
{
    if (CellIndex >= CellRecords.size())
        return Deliver<CellRecord*>::Refuse({ RefusalReason::ContentUnsupported, "no such cell" });

    return Deliver<CellRecord*>::Result(&CellRecords[CellIndex]);
}

const std::vector<CellRecord>& CellSpace::Records() const { return CellRecords; }

std::uint32_t CellSpace::ResidentCount() const     { return ResidentCells;    }
std::uint32_t CellSpace::UncommittedCount() const  { return UncommittedCells; }

void CellSpace::DeclareResident(std::uint32_t CellIndex, std::uint32_t SlotIndex, std::uint64_t RecordingIndex)
{
    CellRecord& Held_ = CellRecords[CellIndex];

    if (!Held_.Resident)
        ++ResidentCells;

    Held_.SlotIndex  = SlotIndex;
    Held_.Resident     = true;
    Held_.PromotedAt   = RecordingIndex;
    Held_.ApronWritten = false;
}

void CellSpace::DeclareAbsent(std::uint32_t CellIndex)
{
    CellRecord& Held_ = CellRecords[CellIndex];

    if (Held_.Resident && ResidentCells != 0u)
        --ResidentCells;

    Held_.SlotIndex      = AbsentTile;
    Held_.Resident         = false;
    Held_.ApronWritten     = false;
    Held_.ResolvedRevision = 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> SurfaceTileSpace::ConstructSurfaceTiles(std::uint32_t SurfaceIndex_,
                                          std::uint32_t BytesPerTexel,
                                          std::uint32_t SlotLimit)
{
    // 📐 The permanent levels' cell count, summed from the marking rule rather than transcribed. Five at the
    //    declared subdivision; transcribing it would be a second representation of `PermanentLevelCount`.
    std::uint32_t PermanentCells = 0u;

    for (std::uint32_t Level = ReductionLevelCount - PermanentLevelCount; Level < ReductionLevelCount; ++Level)
    {
        const std::uint32_t Span = CellsPerEdgeAt(Level);
        PermanentCells += Span * Span;
    }

    if (SlotLimit < PermanentCells)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ExtentExhausted, "the backing cannot hold the permanently resident levels" });
    }

    const Deliver<bool> Interaction = Tiles_.ReserveTileSpace(SlotLimit, BytesPerTexel);

    if (!Interaction.Resolved)
        return Interaction;

    // 🚧 The depot's ceiling is the backing extent again, which is a placeholder for the discretionary claim
    //    `06` §3 will report. It is declared rather than derived so nothing here consults a device.
    const Deliver<bool> Depoted = Depot_.ReserveSurfaceStorage(Tiles_.BackingBytes());

    if (!Depoted.Resolved)
        return Depoted;

    Cells_.ConstructCells();

    Index     = SurfaceIndex_;
    Constructed = true;

    // 🔴 The permanent levels are made resident immediately, before anything can sample. Promoting them lazily
    //    would leave the first rotations of a session with no resident level at all, which is exactly the stall
    //    `20` §3's guarantee exists to remove — and it would appear only on the first frame, where nobody looks.
    for (std::uint32_t CellIndex = 0u; CellIndex < CellIndexSpan; ++CellIndex)
    {
        if (!Cells_.Records()[CellIndex].Permanent)
            continue;

        const Deliver<std::uint32_t> Reserved = Tiles_.Reserve();

        if (!Reserved.Resolved)
            return Deliver<bool>::Refuse(Reserved.Error);

        Cells_.DeclareResident(CellIndex, Reserved.Resolve(), 0u);
    }

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      SAMPLING
//------------------------------------------------------------------------------------------------------------------------

Deliver<SampledCell> SurfaceTileSpace::Sample(std::uint32_t Level,
                                              double        PositionX,
                                              double        PositionY,
                                              std::uint64_t RecordingIndex,
                                              RequestQueue& Requesting)
{
    if (!Constructed)
        return Deliver<SampledCell>::Refuse({ RefusalReason::HostDenied, "the residency is not constructed" });

    if (Level >= ReductionLevelCount)
        return Deliver<SampledCell>::Refuse({ RefusalReason::ContentUnsupported, "no such reduction level" });

    SampledCell Resolved;
    Resolved.RequestedLevel = Level;

    // 🔴 The coarsening walk, and the whole of `20` §5's never-stalls gate. It terminates because the coarsest
    //    levels are permanently resident, so the loop cannot fall off the end of the chain.
    for (std::uint32_t Walking = Level; Walking < ReductionLevelCount; ++Walking)
    {
        const Deliver<std::uint32_t> Located = IndexAt(Walking, PositionX, PositionY);

        if (!Located.Resolved)
            continue;

        const std::uint32_t CellIndex = Located.Resolve();
        CellRecord&         Held_       = *Cells_.Amend(CellIndex).Resolve();

        if (Walking == Level)
        {
            // 📝 The demand names the level that was **wanted**, not the level that answered. Demanding the
            //    level that answered would demand a cell that is already resident, and the surface would never
            //    refine past whatever it happened to have.
            Held_.DemandedAt = RecordingIndex;

            if (!Held_.Resident)
            {
                Requesting.Demand(Index, CellIndex, RecordingIndex);
                Resolved.DemandRecorded = true;
            }
        }

        if (!Held_.Resident)
            continue;

        // 📝 The coarser cell that answered is marked demanded too, so the eviction ordering does not take away
        //    the tile that is currently doing the work of the one that is missing.
        Held_.DemandedAt = RecordingIndex;

        Resolved.CellIndex   = CellIndex;
        Resolved.SlotIndex   = Held_.SlotIndex;
        Resolved.ResolvedLevel = Walking;

        return Deliver<SampledCell>::Result(Resolved);
    }

    // 📝 Unreachable while the permanent levels stand, and stated as a refusal rather than as an assumption so
    //    that a future amendment to `PermanentLevelCount` is caught here rather than by an artist.
    return Deliver<SampledCell>::Refuse(
        { RefusalReason::ExtentExhausted, "no resident level answered; the permanence guarantee is broken" });
}

Deliver<SampledCell> SurfaceTileSpace::SampleGuaranteed(double PositionX, double PositionY) const
{
    if (!Constructed)
        return Deliver<SampledCell>::Refuse({ RefusalReason::HostDenied, "the residency is not constructed" });

    // 🔴 `16` §3.1 ③: the coarsest **guaranteed-resident** level, and never a demand. This walks only the
    //    permanent levels, so it cannot resolve to a tile that a later eviction could take away — and it cannot
    //    record a demand that would put a visibility recording on the promotion path.
    for (std::uint32_t Walking = ReductionLevelCount - PermanentLevelCount; Walking < ReductionLevelCount; ++Walking)
    {
        const Deliver<std::uint32_t> Located = IndexAt(Walking, PositionX, PositionY);

        if (!Located.Resolved)
            continue;

        const Deliver<const CellRecord*> Held_ = Cells_.Held(Located.Resolve());

        if (!Held_.Resolved || !Held_.Resolve()->Resident)
            continue;

        SampledCell Resolved;
        Resolved.CellIndex    = Located.Resolve();
        Resolved.SlotIndex    = Held_.Resolve()->SlotIndex;
        Resolved.ResolvedLevel  = Walking;
        Resolved.RequestedLevel = Walking;

        return Deliver<SampledCell>::Result(Resolved);
    }

    return Deliver<SampledCell>::Refuse(
        { RefusalReason::ExtentExhausted, "no guaranteed level is resident" });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  UNCOMMITTED PAINT
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> SurfaceTileSpace::DeclareUncommitted(std::uint32_t CellIndex, bool UncommittedDeclared)
{
    const Deliver<CellRecord*> Amending = Cells_.Amend(CellIndex);

    if (!Amending.Resolved)
        return Deliver<bool>::Refuse(Amending.Error);

    CellRecord& Held_ = *Amending.Resolve();

    if (UncommittedDeclared && !Held_.Resident)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::HostDenied, "a cell with no tile cannot hold uncommitted texture" });
    }

    if (Held_.Uncommitted == UncommittedDeclared)
        return Deliver<bool>::Result(true);

    Held_.Uncommitted = UncommittedDeclared;

    if (UncommittedDeclared)
        ++Cells_.UncommittedCells;
    else if (Cells_.UncommittedCells != 0u)
        --Cells_.UncommittedCells;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      PROMOTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> SurfaceTileSpace::ReserveOrEvict(PromotionScheduler& Scheduling, std::uint64_t RecordingIndex)
{
    const Deliver<std::uint32_t> Reserved = Tiles_.Reserve();

    if (Reserved.Resolved)
        return Reserved;

    // 📝 The whole span is walked once. It is 5461 entries at the declared subdivision, which is a scan the
    //    promotion of one tile can afford and which needs no second ordering to be kept current — and a second
    //    ordering is a second representation that drifts from the records the moment one is amended.
    const std::vector<CellRecord>& Records = Cells_.Records();

    bool              Found     = false;
    EvictionCandidate Preferred = {};

    for (std::uint32_t CellIndex = 0u; CellIndex < Records.size(); ++CellIndex)
    {
        const CellRecord& Held_ = Records[CellIndex];

        // 🔴 The three exclusions, in one place. A permanent cell would break `20` §3's guarantee, an
        //    uncommitted cell would discard the artist's unsealed stroke, and a cell promoted this rotation is
        //    one the caller is about to write into.
        if (!Held_.Resident || Held_.Permanent || Held_.Uncommitted)
            continue;

        if (Held_.PromotedAt == RecordingIndex)
            continue;

        const Deliver<CellAddress> Addressed = AddressOf(CellIndex);

        EvictionCandidate Candidate;
        Candidate.CellIndex = CellIndex;
        Candidate.Level       = Addressed.Resolved ? Addressed.Resolve().Level : 0u;
        Candidate.DemandedAt  = Held_.DemandedAt;
        Candidate.PromotedAt  = Held_.PromotedAt;

        if (!Found || PrecedesInEviction(Scheduling.Ordering(), Candidate, Preferred))
        {
            Preferred = Candidate;
            Found     = true;
        }
    }

    if (!Found)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ExtentExhausted, "every resident tile is permanent, uncommitted or newly promoted" });
    }

    const Deliver<bool> Evicted = Evict(Preferred.CellIndex, RecordingIndex);

    if (!Evicted.Resolved)
        return Deliver<std::uint32_t>::Refuse(Evicted.Error);

    // 🔴 The evicted slot is quarantined, not freed, so the claim below still refuses. Reclaiming it here would
    //    be reclaiming inside the recording slot count, which is the one thing `20` §5 forbids — so the promotion
    //    defers this rotation and takes the freed slot on the rotation after the depth elapses.
    return Tiles_.Reserve();
}

Deliver<PromotionVerdict> SurfaceTileSpace::Promote(std::uint32_t        CellIndex,
                                                        const PromotionCost& Costing,
                                                        std::uint64_t        ContentRevision,
                                                        PromotionScheduler&  Scheduling,
                                                        std::uint64_t        RecordingIndex)
{
    if (!Constructed)
    {
        return Deliver<PromotionVerdict>::Refuse(
            { RefusalReason::HostDenied, "the residency is not constructed" });
    }

    const Deliver<CellRecord*> Amending = Cells_.Amend(CellIndex);

    if (!Amending.Resolved)
        return Deliver<PromotionVerdict>::Refuse(Amending.Error);

    CellRecord& Held_ = *Amending.Resolve();

    // 🔴 `70` §2's comparison, and the reason the whole mechanism is affordable. One integer test at Exact, per
    //    tile, whose answer is almost always "no work": a camera move advances no counter, and an owner that
    //    moved advances none either, because a placement's transform is stored relative to its surface.
    if (Held_.Resident && Held_.ResolvedRevision == ContentRevision)
        return Deliver<PromotionVerdict>::Result(PromotionVerdict::AlreadyResident);

    if (!Scheduling.Accepts(Costing))
    {
        Scheduling.DeferOne();
        return Deliver<PromotionVerdict>::Result(PromotionVerdict::Deferred);
    }

    // 📝 A resident cell at a stale revision keeps its own slot and is resolved into again. Releasing and
    //    re-claiming would quarantine the slot for the recording slot count and leave the cell absent meanwhile —
    //    so an edited layer would make its own surface go coarse for two rotations at every stroke.
    if (Held_.Resident)
    {
        const Deliver<bool> Charged = Scheduling.Charge(Costing);

        if (!Charged.Resolved)
        {
            Scheduling.DeferOne();
            return Deliver<PromotionVerdict>::Result(PromotionVerdict::Deferred);
        }

        Held_.ResolvedRevision = ContentRevision;
        Held_.PromotedAt       = RecordingIndex;
        Held_.ApronWritten     = false;

        Scheduling.PromoteOne();

        return Deliver<PromotionVerdict>::Result(PromotionVerdict::ReResolved);
    }

    const Deliver<std::uint32_t> Reserved = ReserveOrEvict(Scheduling, RecordingIndex);

    if (!Reserved.Resolved)
    {
        Scheduling.DeferOne();
        return Deliver<PromotionVerdict>::Result(PromotionVerdict::Deferred);
    }

    const Deliver<bool> Charged = Scheduling.Charge(Costing);

    if (!Charged.Resolved)
    {
        // 📝 The slot is handed straight back rather than held for a promotion that did not happen. It goes into
        //    quarantine like any release, which is correct: nothing was written into it, and the depth costs one
        //    slot for two rotations rather than a rule that has to distinguish an unwritten slot from a written
        //    one.
        Discard(Tiles_.Release(Reserved.Resolve(), RecordingIndex));

        Scheduling.DeferOne();
        return Deliver<PromotionVerdict>::Result(PromotionVerdict::Deferred);
    }

    Cells_.DeclareResident(CellIndex, Reserved.Resolve(), RecordingIndex);
    Held_.ResolvedRevision = ContentRevision;

    Scheduling.PromoteOne();

    return Deliver<PromotionVerdict>::Result(PromotionVerdict::Promoted);
}

Deliver<bool> SurfaceTileSpace::DeclareApronWritten(std::uint32_t CellIndex)
{
    const Deliver<CellRecord*> Amending = Cells_.Amend(CellIndex);

    if (!Amending.Resolved)
        return Deliver<bool>::Refuse(Amending.Error);

    if (!Amending.Resolve()->Resident)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the cell holds no tile to apron" });

    Amending.Resolve()->ApronWritten = true;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  EVICTION AND RECLAIM
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> SurfaceTileSpace::Evict(std::uint32_t CellIndex, std::uint64_t RecordingIndex)
{
    const Deliver<CellRecord*> Amending = Cells_.Amend(CellIndex);

    if (!Amending.Resolved)
        return Deliver<bool>::Refuse(Amending.Error);

    CellRecord& Held_ = *Amending.Resolve();

    if (!Held_.Resident)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the cell holds no tile" });

    if (Held_.Permanent)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a permanently resident level is never evicted" });
    }

    if (Held_.Uncommitted)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the tile holds texture no transaction has sealed" });
    }

    const std::uint32_t SlotIndex = Held_.SlotIndex;

    Cells_.DeclareAbsent(CellIndex);

    return Tiles_.Release(SlotIndex, RecordingIndex);
}

std::uint32_t SurfaceTileSpace::Reconcile(std::uint64_t RecordingIndex)
{
    return Tiles_.Reclaim(RecordingIndex);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE MEASURES
//------------------------------------------------------------------------------------------------------------------------

void SurfaceTileSpace::Report(MeasureIndex&             Measured,
                              const PromotionScheduler& Scheduling,
                              TickPoint                 Sampled) const
{
    // 🔴 Every one of these **overwrites** — `86` §2. A residency total appended once per rotation buries the
    //    one refusal that mattered under thousands of readings nobody asked for, and `20` appends nothing at all:
    //    it is absent from `86` §4's register precisely because it makes no promise to report.
    Measured.DeclareCount("20 §2 SurfaceTileSpace", "ResidentTiles", Cells_.ResidentCount(), Sampled);
    Measured.DeclareCount("20 §2 SurfaceTileSpace", "QuarantinedTiles", Tiles_.QuarantinedCount(), Sampled);
    Measured.DeclareCount("20 §2 SurfaceTileSpace", "FreeTiles", Tiles_.FreeCount(), Sampled);
    Measured.DeclareCount("20 §2.2 PromotionScheduler", "Promoted", Scheduling.PromotedCount(), Sampled);
    Measured.DeclareCount("20 §2.2 PromotionScheduler", "Deferred", Scheduling.DeferredCount(), Sampled);
    Measured.DeclareCount("20 §2.2 PromotionScheduler", "TransferRemaining",
                          Scheduling.Remaining().TransferBytes, Sampled);
    Measured.DeclareCount("20 §2.2 PromotionScheduler", "EvaluationRemaining",
                          Scheduling.Remaining().EvaluationUnits, Sampled);
    Measured.DeclareCount("20 §4 SurfaceDepot", "OccupiedBytes", Depot_.OccupiedBytes(), Sampled);
    Measured.DeclareCount("20 §4 SurfaceDepot", "HeldArtefacts", Depot_.HeldCount(), Sampled);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

const CellSpace&    SurfaceTileSpace::Cells() const { return Cells_; }
const TileSpace&    SurfaceTileSpace::Tiles() const { return Tiles_; }
SurfaceDepot&       SurfaceTileSpace::Depot()       { return Depot_; }
const SurfaceDepot& SurfaceTileSpace::Depot() const { return Depot_; }

std::uint32_t SurfaceTileSpace::SurfaceIndex() const     { return Index;                    }
std::uint64_t SurfaceTileSpace::StoredBytesPerTile() const { return Tiles_.StoredBytesPerTile(); }

bool SurfaceTileSpace::ResidencyValid(std::uint64_t RecordingIndex) const
{
    if (!Constructed)
        return false;

    if (!Tiles_.InteractionConsistent() || !Depot_.DepotConsistent())
        return false;

    const std::vector<CellRecord>& Records = Cells_.Records();

    std::vector<bool> SlotHeld(Tiles_.SlotLimit(), false);

    std::uint32_t Resident = 0u;

    for (const CellRecord& Held_ : Records)
    {
        if (Held_.Permanent && !Held_.Resident)
            return false;

        if (Held_.Uncommitted && !Held_.Resident)
            return false;

        if (!Held_.Resident)
        {
            if (Held_.SlotIndex != AbsentTile)
                return false;

            continue;
        }

        ++Resident;

        if (Held_.SlotIndex >= SlotHeld.size() || SlotHeld[Held_.SlotIndex])
            return false;

        SlotHeld[Held_.SlotIndex] = true;

        // 🔴 `20` §5: every resident tile carries a written apron. Checked only for tiles promoted before this
        //    rotation, because a tile promoted this rotation is one the caller has not yet written — and
        //    demanding it here would make the invariant unsatisfiable at exactly the moment it is most useful.
        if (Held_.PromotedAt < RecordingIndex && !Held_.ApronWritten)
            return false;
    }

    return Resident == Cells_.ResidentCount() && Resident == Tiles_.HeldCount();
}

}   // namespace Slate
