//============================================================================================================================================
//                                                           SELECTIONSEQUENCE.H
//============================================================================================================================================
// 🧩 Selection ordering, revised in its own sequence, session-scoped, restored alongside the transaction it served.

#pragma once

#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ONE SELECTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One committed selection — what was selected, and which document transaction it was standing at.
/// note  🔴 `12` §11: undoing a move must restore both the transform and the selection that transform was
///        applied to. The recorded revision ordinal is what pairs the two, so a scrub of the document
///        sequence can find the selection that served it without either sequence owning the other.
/// tag   owning
struct CommittedSelection
{
    std::vector<OwnerIdentity>  SelectedOwners = {};   // [-] - in the order the artist selected them
    std::uint64_t                  RevisionIndex   = 0u;   // [-] - document revisions committed when sealed
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The session's selections, traversable backwards and forwards on their own.
/// note  🔴 Session-scoped and never written to the document — `48` §2 rules it and `10` §2 agrees. A document
///        reopening with someone else's selection has restored a decision the artist had already finished
///        making, and the first stroke lands on the wrong owner. Recorded as `00` §10 conflict 34.
/// note  ⚠️ "Selection is not revisioned" is the naive reading and it produces a defect the artist meets in a
///        minute: move three owners, undo, and the transforms revert while the selection does not. It is
///        not in the document's sequence, and it is not unrevisioned either.
/// tag   owning
class SelectionSequence
{
public:

    /// 🧩 Commits one selection, paired with the document revision it stands at.
    /// in    Selected         [-]  the owners, in the order the artist selected them
    /// in    RevisionIndex  [-]  document revisions committed at the moment of sealing
    /// post  the traversal position is the end of the sequence
    /// cost  🚩
    /// tag   api, nonthrowing
    void Seal(const std::vector<OwnerIdentity>& Selected, std::uint64_t RevisionIndex);

    /// 🧩 Traverses one selection backwards.
    /// out   Result  [-]  refuses with ExtentExhausted at the beginning of the sequence
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Retreat();

    /// 🧩 Traverses one selection forwards.
    /// out   Result  [-]  refuses with ExtentExhausted at the end of the sequence
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Advance();

    /// 🧩 Restores the selection that stood at a declared document revision.
    /// in    RevisionIndex  [-]  the document revision a scrub has arrived at
    /// out   Result          [-]  refuses with ExtentExhausted when no selection was ever sealed there
    /// note  This is what pairs a document scrub with the selection its transaction applied to. The pairing
    ///        holds within the session where the scrub happens, which is all `12` §11 requires of it.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> RestoreAt(std::uint64_t RevisionIndex);

    /// 🧩 The selection standing now.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<OwnerIdentity>& Current() const;

    /// 🧩 The committed selections, in order.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<CommittedSelection>& Committed() const;

    /// 🧩 Where the traversal position sits; equal to the committed count when nothing is traversed back.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t TraversalPosition() const;

private:

    std::vector<CommittedSelection>  CommittedOrder;             // [-] - the sequence itself
    std::vector<OwnerIdentity>    CurrentSelection;          // [-] - as the traversal position leaves it
    std::uint64_t                    TraversalIndex = 0u;      // [-] - selections currently applied
};

}   // namespace Slate
