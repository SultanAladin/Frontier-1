//============================================================================================================================================
//                                                         PROMOTIONSCHEDULER.CPP
//============================================================================================================================================
// 🧩 The two measures charged apart, the total eviction order, and the cost read from `56`'s entries.

#include "SlateCompute/Compute/PromotionScheduler/Api/PromotionScheduler.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  EVICTION ORDERING
//------------------------------------------------------------------------------------------------------------------------

bool PrecedesInEviction(EvictionOrdering         Declared,
                        const EvictionCandidate& Earlier,
                        const EvictionCandidate& Later)
{
    if (Declared == EvictionOrdering::FinestLevelFirst && Earlier.Level != Later.Level)
    {
        // 📝 A coarse tile answers every sample a finer one would have answered, so evicting coarse first
        //    removes the only thing standing between a demand and a stall. Finest first is the direction that
        //    keeps the guarantee — `20` §3.
        return Earlier.Level < Later.Level;
    }

    if (Earlier.DemandedAt != Later.DemandedAt)
        return Earlier.DemandedAt < Later.DemandedAt;

    if (Earlier.PromotedAt != Later.PromotedAt)
        return Earlier.PromotedAt < Later.PromotedAt;

    // 📝 The cell ordinal breaks the last tie, so the ordering is total and the same two candidates resolve the
    //    same way on every rotation and every machine. Leaving them incomparable would let the walk order
    //    decide, and the walk order is storage rather than policy.
    return Earlier.CellIndex < Later.CellIndex;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    COST ESTIMATION
//------------------------------------------------------------------------------------------------------------------------

PromotionCost Estimate(const SurfaceLayerSequence& Sequence, std::uint64_t TileBytes, bool DepotArtefactValid)
{
    PromotionCost Costing;

    if (DepotArtefactValid)
    {
        // 🔴 `20` §2.1's second source. The artefact already holds what the whole sequence resolves to, so the
        //    cost is one tile transferred and no evaluation at all.
        Costing.TransferBytes = TileBytes;
        return Costing;
    }

    bool TexturedHeld = false;

    for (const LayerSpecification& Held : Sequence.Entries())
    {
        if (!Held.PresenceEnabled)
            continue;

        if (Held.Source == LayerContentSource::TexturedImpressions)
        {
            TexturedHeld = true;
            continue;
        }

        // 📝 A nested sequence is charged as one entry rather than walked. §4.1 combines its content internally
        //    and reads as a single entry, so charging its interior separately would charge twice for content
        //    that resolves once.
        Costing.EvaluationUnits += EvaluationUnitsPerEntry;
    }

    // 📝 The textured layers of one tile transfer one tile's worth however many of them there are, because they
    //    accumulate into the same texels. Charging per textured entry would make a surface with thirty textured
    //    layers unpromotable, which is the case `18` §8's resolved-channel rule exists to keep affordable.
    if (TexturedHeld)
        Costing.TransferBytes = TileBytes;

    return Costing;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DECLARATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> PromotionScheduler::DeclareBudget(const PromotionBudget& Declaring)
{
    if (Declaring.TransferBytes == 0u && Declaring.EvaluationUnits == 0u)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a budget of nothing promotes nothing, ever" });
    }

    DeclaredBudget  = Declaring;
    RemainingBudget = Declaring;

    return Deliver<bool>::Result(true);
}

void PromotionScheduler::DeclareOrdering(EvictionOrdering Declaring)
{
    if (Declaring != EvictionOrdering::OrderingCount)
        DeclaredOrder = Declaring;
}

Deliver<bool> PromotionScheduler::OpenRecording(std::uint64_t RecordingIndex)
{
    if (RecordingCurrent && RecordingIndex <= RecordingOpened)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::HostDenied, "the rotation is not later than the one already open" });
    }

    RemainingBudget  = DeclaredBudget;
    RecordingOpened   = RecordingIndex;
    RecordingCurrent = true;
    PromotedThis     = 0u;
    DeferredThis     = 0u;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE BUDGET
//------------------------------------------------------------------------------------------------------------------------

bool PromotionScheduler::Accepts(const PromotionCost& Costing) const
{
    // 🔴 Both, independently. A cost that fits the transfer budget and exceeds the evaluation budget does not
    //    fit, and the whole reason `20` §2.2 declares two measures is that one of them is silent about the
    //    other's work.
    return Costing.TransferBytes   <= RemainingBudget.TransferBytes
        && Costing.EvaluationUnits <= RemainingBudget.EvaluationUnits;
}

Deliver<bool> PromotionScheduler::Charge(const PromotionCost& Costing)
{
    if (!Accepts(Costing))
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the rotation's budget cannot admit it" });

    RemainingBudget.TransferBytes   -= Costing.TransferBytes;
    RemainingBudget.EvaluationUnits -= Costing.EvaluationUnits;

    return Deliver<bool>::Result(true);
}

void PromotionScheduler::DeferOne()
{
    ++DeferredThis;
    ++DeferredSession;
}

void PromotionScheduler::PromoteOne()
{
    ++PromotedThis;
    ++PromotedSession;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

EvictionOrdering       PromotionScheduler::Ordering() const       { return DeclaredOrder;   }
const PromotionBudget& PromotionScheduler::Declared() const       { return DeclaredBudget;  }
const PromotionBudget& PromotionScheduler::Remaining() const      { return RemainingBudget; }
std::uint64_t          PromotionScheduler::OpenedRecording() const { return RecordingOpened;  }
std::uint32_t          PromotionScheduler::PromotedCount() const  { return PromotedThis;    }
std::uint32_t          PromotionScheduler::DeferredCount() const  { return DeferredThis;    }
std::uint64_t          PromotionScheduler::PromotedTotal() const  { return PromotedSession; }
std::uint64_t          PromotionScheduler::DeferredTotal() const  { return DeferredSession; }

}   // namespace Slate
