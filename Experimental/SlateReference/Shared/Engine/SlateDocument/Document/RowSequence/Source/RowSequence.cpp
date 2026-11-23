//============================================================================================================================================
//                                                             ROWSEQUENCE.CPP
//============================================================================================================================================
// 🧩 The depth-first walk, and the binary-indexed counts that answer both scroll questions.

#include "SlateDocument/Document/RowSequence/Api/RowSequence.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   COUNTED ORDERING
//------------------------------------------------------------------------------------------------------------------------

void RankIndex::PopulateRanks(std::uint32_t RowCount)
{
    SpannedRows = RowCount;
    CountedRows = RowCount;

    RowCounted.assign(RowCount, true);
    RunCounts.assign(static_cast<std::size_t>(RowCount) + 1u, 0u);

    // 📐 Built in one pass rather than by RowCount insertions: each stored word takes the count of the run
    //    ending at its ordinal, then hands that run's total to the word that contains it.
    for (std::uint32_t Index = 1u; Index <= RowCount; ++Index)
    {
        RunCounts[Index] += 1u;

        const std::uint32_t Containing = Index + (Index & (~Index + 1u));

        if (Containing <= RowCount)
            RunCounts[Containing] += RunCounts[Index];
    }
}

void RankIndex::Declare(std::uint32_t RowIndex, bool CountedEnabled)
{
    if (RowIndex >= SpannedRows)
        return;

    if (RowCounted[RowIndex] == CountedEnabled)
        return;

    RowCounted[RowIndex] = CountedEnabled;

    const std::uint32_t Adjustment = CountedEnabled ? 1u : 0u;

    for (std::uint32_t Index = RowIndex + 1u;
         Index <= SpannedRows;
         Index += Index & (~Index + 1u))
    {
        if (Adjustment == 1u)
            ++RunCounts[Index];
        else
            --RunCounts[Index];
    }

    if (CountedEnabled)
        ++CountedRows;
    else
        --CountedRows;
}

std::uint32_t RankIndex::CountedBefore(std::uint32_t RowIndex) const
{
    std::uint32_t Bounded = RowIndex > SpannedRows ? SpannedRows : RowIndex;
    std::uint32_t Counted = 0u;

    while (Bounded != 0u)
    {
        Counted += RunCounts[Bounded];
        Bounded -= Bounded & (~Bounded + 1u);
    }

    return Counted;
}

Deliver<std::uint32_t> RankIndex::RowAtVisible(std::uint32_t VisibleIndex) const
{
    if (VisibleIndex >= CountedRows)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ExtentExhausted, "the visible position lies past the last counted row" });
    }

    // 📐 Descends the runs from the widest, taking each whose count the target still exceeds. The ordinal
    //    accumulated is the last row whose prefix count is not greater than the target.
    std::uint32_t Stride    = 1u;
    std::uint32_t Remaining = VisibleIndex;
    std::uint32_t Located   = 0u;

    while (Stride * 2u <= SpannedRows)
        Stride *= 2u;

    for (; Stride != 0u; Stride /= 2u)
    {
        const std::uint32_t Candidate = Located + Stride;

        if (Candidate <= SpannedRows && RunCounts[Candidate] <= Remaining)
        {
            Remaining -= RunCounts[Candidate];
            Located    = Candidate;
        }
    }

    return Deliver<std::uint32_t>::Result(Located);
}

Deliver<std::uint32_t> RankIndex::VisibleOfRow(std::uint32_t RowIndex) const
{
    if (RowIndex >= SpannedRows)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ExtentExhausted, "the row lies outside the span" });

    if (!RowCounted[RowIndex])
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ExtentExhausted, "the row is collapsed or narrowed out of the count" });
    }

    return Deliver<std::uint32_t>::Result(CountedBefore(RowIndex));
}

std::uint32_t RankIndex::CountedTotal() const
{
    return CountedRows;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LINEARISATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 Appends one enclosure's ordering to the pending run, last owner first. The reversal is what makes a
//    run taken from the end reproduce the declared order.
void AppendReversed(std::vector<std::uint32_t>& Pending,
                    const SceneStructure&       Relations,
                    std::uint32_t               FirstInOrder)
{
    const std::size_t Appended = Pending.size();

    for (std::uint32_t Walking = FirstInOrder; Walking != AbsentSlot; Walking = Relations.NextInOrder(Walking))
        Pending.push_back(Walking);

    std::size_t Lower = Appended;
    std::size_t Upper = Pending.size();

    while (Lower + 1u < Upper)
    {
        --Upper;

        const std::uint32_t Held = Pending[Lower];
        Pending[Lower]           = Pending[Upper];
        Pending[Upper]           = Held;

        ++Lower;
    }
}

}   // namespace

Deliver<bool> RowSequence::Linearize(const SceneStructure& Relations)
{
    // 🔴 `12` §4 puts ④ before ⑤. Linearising against labels a relabel is still owed on produces an order
    //    that is briefly wrong, and briefly wrong here means displayed.
    if (Relations.RelabelOwed())
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ExtentExhausted, "a label repair is owed before the sequence may be taken" });
    }

    SequencedRows.clear();
    RowOfSlot.assign(Relations.SpannedCount(), AbsentSlot);

    // 📝 🔴 Both per-slot declarations are resized rather than cleared. A collapse and a narrowing are the
    //    artist's standing decisions, and ⑤ runs on every tick — clearing them here would reopen every
    //    collapsed enclosure and widen every narrowing on the tick after the one that declared it.
    SlotCollapsed.resize(Relations.SpannedCount(), false);
    SlotRetained.resize(Relations.SpannedCount(), false);

    // 📝 The walk carries its own pending ordering rather than recursing, so enclosure depth is bounded by
    //    the relation's declared ceiling instead of by whatever call stack the walk happens to be given.
    //    Owners are appended in reverse so they come off the end in the order their enclosure declares
    //    them: depth-first order is the row order, and the two never diverge.
    std::vector<std::uint32_t> Pending;
    Pending.reserve(Relations.SpannedCount());

    AppendReversed(Pending, Relations, Relations.RootFirst());

    while (!Pending.empty())
    {
        const std::uint32_t SlotIndex = Pending.back();
        Pending.pop_back();

        SequencedRow Incoming;
        Incoming.Owner         = Relations.OwnerAt(SlotIndex);
        Incoming.EnclosureDepth   = Relations.EnclosureDepth(SlotIndex);
        Incoming.ExpansionEnabled = !SlotCollapsed[SlotIndex];

        std::uint32_t EnclosedCount = 0u;

        for (std::uint32_t Enclosed = Relations.FirstEnclosed(SlotIndex);
             Enclosed != AbsentSlot;
             Enclosed = Relations.NextInOrder(Enclosed))
        {
            ++EnclosedCount;
        }

        Incoming.EnclosedCount = EnclosedCount;

        RowOfSlot[SlotIndex] = static_cast<std::uint32_t>(SequencedRows.size());
        SequencedRows.push_back(Incoming);

        AppendReversed(Pending, Relations, Relations.FirstEnclosed(SlotIndex));
    }

    // 📝 🔴 A slot that holds no row holds no standing decision either. Clearing here is what makes a reused
    //    slot safe: without it a new owner inherits the collapse and the retention of whoever the slot
    //    carried before, and appears collapsed, or retained by a narrowing it was never confirmed against.
    for (std::uint32_t SlotIndex = 0u; SlotIndex < RowOfSlot.size(); ++SlotIndex)
    {
        if (RowOfSlot[SlotIndex] != AbsentSlot)
            continue;

        SlotCollapsed[SlotIndex] = false;
        SlotRetained[SlotIndex]  = false;
    }

    Recount();

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                               EXPANSION AND NARROWING
//------------------------------------------------------------------------------------------------------------------------

// 📝 No enclosure is collapsed above the row being counted. Spelled as a constant rather than repeated,
//    because it is compared against a depth and every enclosure depth is below the relation's ceiling.
namespace
{

constexpr std::uint32_t NothingCollapsed = 0xFFFFFFFFu;   // [-] - no collapsing enclosure stands above

}   // namespace

void RowSequence::Recount()
{
    VisibleOrdering.PopulateRanks(static_cast<std::uint32_t>(SequencedRows.size()));

    // 📝 A row leaves the count when it is narrowed, or when any row it sits under is collapsed. The
    //    collapsing depth is carried down the sequence so the whole derivation is one pass over the rows.
    std::uint32_t CollapsedAtDepth = NothingCollapsed;

    for (std::size_t Index = 0u; Index < SequencedRows.size(); ++Index)
    {
        SequencedRow& Held = SequencedRows[Index];

        if (Held.EnclosureDepth <= CollapsedAtDepth)
            CollapsedAtDepth = NothingCollapsed;

        // 📝 A narrowing narrows by retention rather than by exclusion, so an owner registered in nothing
        //    leaves the count while one stands. `12` §10 makes it a subset, and the subset is what is kept.
        const bool Retained = !NarrowingHeld
                           || (Held.Owner.SlotIndex < SlotRetained.size()
                            && SlotRetained[Held.Owner.SlotIndex]);

        Held.VisibleInCount = CollapsedAtDepth == NothingCollapsed && Retained;

        VisibleOrdering.Declare(static_cast<std::uint32_t>(Index), Held.VisibleInCount);

        if (!Held.ExpansionEnabled && Held.EnclosureDepth < CollapsedAtDepth)
            CollapsedAtDepth = Held.EnclosureDepth;
    }
}

Deliver<bool> RowSequence::DeclareExpansion(OwnerIdentity Subject, bool ExpansionEnabled)
{
    const Deliver<std::uint32_t> Located = RowOf(Subject);

    if (!Located.Resolved)
        return Deliver<bool>::Refuse(Located.Error);

    // 🔴 Declared against the slot as well as the row. The row is rebuilt at every ⑤ and the slot is not, so
    //    holding it on the row alone would reopen the enclosure on the tick after the artist collapsed it.
    SequencedRows[Located.Resolve()].ExpansionEnabled = ExpansionEnabled;
    SlotCollapsed[Subject.SlotIndex]                = !ExpansionEnabled;

    Recount();

    return Deliver<bool>::Result(true);
}

Deliver<bool> RowSequence::DeclareNarrowing(const std::vector<OwnerIdentity>& Retained, bool NarrowingDeclared)
{
    // 📝 Withdrawing the narrowing ignores what was retained rather than requiring the whole population to be
    //    handed back. Every row returns to the count, which is the one thing an empty search text means.
    if (!NarrowingDeclared)
    {
        NarrowingHeld = false;

        SlotRetained.assign(SlotRetained.size(), false);

        Recount();

        return Deliver<bool>::Result(true);
    }

    // 📝 Confirmed against the rows before anything is written, so a stale identity refuses the whole
    //    narrowing rather than leaving a subset that retains part of what the search found.
    for (const OwnerIdentity& Confirming : Retained)
    {
        if (!RowOf(Confirming).Resolved)
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::IdentityStale, "a retained owner holds no row in this sequence" });
        }
    }

    SlotRetained.assign(SlotRetained.size(), false);

    for (const OwnerIdentity& Retaining : Retained)
        SlotRetained[Retaining.SlotIndex] = true;

    NarrowingHeld = true;

    Recount();

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

const std::vector<SequencedRow>& RowSequence::Rows() const
{
    return SequencedRows;
}

const RankIndex& RowSequence::Counted() const
{
    return VisibleOrdering;
}

Deliver<std::uint32_t> RowSequence::RowOf(OwnerIdentity Subject) const
{
    if (!Subject.IdentityDeclared() || Subject.SlotIndex >= RowOfSlot.size())
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::IdentityStale, "the owner holds no row" });

    const std::uint32_t RowIndex = RowOfSlot[Subject.SlotIndex];

    if (RowIndex == AbsentSlot || SequencedRows[RowIndex].Owner != Subject)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::IdentityStale, "the owner holds no row" });

    return Deliver<std::uint32_t>::Result(RowIndex);
}

bool RowSequence::NarrowingCurrent() const
{
    return NarrowingHeld;
}

bool RowSequence::CountsAgree() const
{
    std::uint32_t Counted = 0u;

    for (const SequencedRow& Held : SequencedRows)
    {
        if (Held.VisibleInCount)
            ++Counted;
    }

    if (Counted != VisibleOrdering.CountedTotal())
        return false;

    std::uint32_t Walking = 0u;

    for (std::size_t Index = 0u; Index < SequencedRows.size(); ++Index)
    {
        if (!SequencedRows[Index].VisibleInCount)
            continue;

        const Deliver<std::uint32_t> Located = VisibleOrdering.RowAtVisible(Walking);

        if (!Located.Resolved || Located.Resolve() != static_cast<std::uint32_t>(Index))
            return false;

        ++Walking;
    }

    return true;
}

}   // namespace Slate
