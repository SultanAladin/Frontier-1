//============================================================================================================================================
//                                                              ROWSEQUENCE.H
//============================================================================================================================================
// 🧩 Depth-first linearisation of the enclosure relation, and the counted ordering that scrolls it.

#pragma once

#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"
#include "SlateDocument/Document/SceneStructure/Api/SceneStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       ONE ROW
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One row of the linearisation — who it presents, how deep it sits, and whether it is counted.
/// note  Collapsed and narrowed rows stay in the sequence and leave the count. Removing them would make
///       expanding an enclosure a rebuild instead of a count adjustment.
/// tag   nonallocating, nonthrowing
struct SequencedRow
{
    OwnerIdentity  Owner           = {};      // [-] - what the row presents
    std::uint32_t     EnclosureDepth     = 0u;      // [-] - indentation, in enclosures from the root ordering
    std::uint32_t     EnclosedCount      = 0u;      // [-] - owners enclosed directly; zero accepts no arrow
    bool              ExpansionEnabled   = true;    // [-] - this owner's enclosed rows are counted
    bool              VisibleInCount     = true;    // [-] - counted; false while collapsed above or narrowed out
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   COUNTED ORDERING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Counted ordering over the sequence, answering both scroll questions in logarithmic time.
/// note  📐 A binary-indexed count over the row ordering: each stored word carries the visible count of a
///        power-of-two run ending at its ordinal, so a prefix count and a search by count are both a walk of
///        the set bits of an ordinal rather than of the rows.
/// note  🔴 The two questions are not symmetric conveniences. Scrolling to an arbitrary position needs the
///        first and scrolling to a selection needs the second, and a linear answer to either is felt at a
///        thousand rows.
/// tag   owning
class RankIndex
{
public:

    /// 🧩 Sizes the counted ordering to a row count, every row counted.
    /// in    RowCount  [-]  rows the sequence holds
    /// post  the count of every prefix equals its row ordinal
    /// cost  🚩
    /// tag   api, nonthrowing
    void PopulateRanks(std::uint32_t RowCount);

    /// 🧩 Declares one row counted or uncounted, adjusting every prefix that contains it.
    /// in    RowIndex      [-]  the row
    /// in    CountedEnabled  [-]  whether it participates in the visible count
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Declare(std::uint32_t RowIndex, bool CountedEnabled);

    /// 🧩 How many rows before this ordinal are counted.
    /// in    RowIndex  [-]  exclusive upper bound
    /// out   Counted     [-]  visible rows in the half-open prefix
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t CountedBefore(std::uint32_t RowIndex) const;

    /// 🧩 Which row carries the visible row at a declared position — the first scroll question.
    /// in    VisibleIndex  [-]  position among counted rows, zero-based
    /// out   Result         [-]  refuses with ExtentExhausted past the last counted row
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<std::uint32_t> RowAtVisible(std::uint32_t VisibleIndex) const;

    /// 🧩 What visible position a row sits at — the second scroll question.
    /// in    RowIndex  [-]  the row
    /// out   Result     [-]  refuses with ExtentExhausted for an uncounted or out-of-span row
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<std::uint32_t> VisibleOfRow(std::uint32_t RowIndex) const;

    /// 🧩 How many rows are counted in total.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t CountedTotal() const;

private:

    std::vector<std::uint32_t>  RunCounts;             // [-] - counted rows of the run ending at each ordinal
    std::vector<bool>           RowCounted;            // [-] - per row, as declared
    std::uint32_t               SpannedRows    = 0u;   // [-] - rows the ordering spans
    std::uint32_t               CountedRows    = 0u;   // [-] - counted rows across the whole span
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The rows the enclosure relation linearises to, paired with the ordering that scrolls them.
/// note  🔴 Linearised only after `SceneStructure::RepairLabels` has delivered. `12` §4 puts ④ before ⑤
///        because rows rebuilt against stale labels are briefly wrong and are displayed while they are.
/// tag   owning
class RowSequence
{
public:

    /// 🧩 Linearises the enclosure relation depth-first — tick step ⑤.
    /// in    Relations  [-]  the reconciled relations, labels already repaired
    /// out   Result    [-]  refuses with ExtentExhausted when a relabel is still owed
    /// post  row order is fully determined by the enclosure relation and its ordering — invariant 9
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> Linearize(const SceneStructure& Relations);

    /// 🧩 Declares one owner's enclosed rows counted or uncounted, and re-derives the counts below it.
    /// in    Subject           [-]  the enclosing owner
    /// in    ExpansionEnabled  [-]  whether its enclosed rows are counted
    /// out   Result           [-]  refuses with IdentityStale when the owner holds no row
    /// note  A collapse is a count adjustment over the owner's own span, never a re-linearisation.
    /// note  🔴 Recorded against the slot rather than against the row, so that the next linearisation carries
    ///        it forward. Held on the row alone it would be erased by every rebuild, and a collapsed
    ///        enclosure would reopen on the following tick without the artist touching it.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> DeclareExpansion(OwnerIdentity Subject, bool ExpansionEnabled);

    /// 🧩 Declares which owners a standing narrowing retains, or withdraws the narrowing entirely.
    /// in    Retained           [-]  the owners a narrowing confirmed, read only while one is declared
    /// in    NarrowingDeclared  [-]  false returns every row to the count and ignores Retained
    /// out   Result            [-]  refuses with IdentityStale when a retained owner holds no slot here
    /// note  🔴 `12` §10 rules row narrowing a subset rather than a predicate. A predicate re-derived every
    ///        tick makes the cost of a search proportional to the population for as long as the text stands;
    ///        a declared subset is derived once, where the text changed.
    /// note  A narrowed row leaves the count and stays in the sequence, exactly as a collapsed one does.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> DeclareNarrowing(const std::vector<OwnerIdentity>& Retained, bool NarrowingDeclared);

    /// 🧩 The rows, in linearised order, including the uncounted ones.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<SequencedRow>& Rows() const;

    /// 🧩 The counted ordering over those rows.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const RankIndex& Counted() const;

    /// 🧩 Which row an owner sits at.
    /// in    Subject  [-]  the owner
    /// out   Result  [-]  refuses with IdentityStale when the owner holds no row
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<std::uint32_t> RowOf(OwnerIdentity Subject) const;

    /// 🧩 Whether a narrowing stands over the rows.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool NarrowingCurrent() const;

    /// 🧩 🔍 Whether the counts agree with the visible rows of the sequence — invariant 5.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    bool CountsAgree() const;

private:

    void Recount();

    std::vector<SequencedRow>   SequencedRows;           // [-] - depth-first, collapsed rows retained
    std::vector<std::uint32_t>  RowOfSlot;               // [-] - slot ordinal to row ordinal; AbsentSlot for none
    std::vector<bool>           SlotCollapsed;           // [-] - collapsed per slot, so a rebuild carries it
    std::vector<bool>           SlotRetained;            // [-] - retained by the standing narrowing, per slot
    RankIndex                   VisibleOrdering;         // [-] - the counted ordering over the rows
    bool                        NarrowingHeld = false;   // [-] - a narrowing stands; SlotRetained is read
};

}   // namespace Slate
