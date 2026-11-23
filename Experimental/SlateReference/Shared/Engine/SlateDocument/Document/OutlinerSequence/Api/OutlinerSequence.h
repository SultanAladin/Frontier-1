//============================================================================================================================================
//                                                            OUTLINERSEQUENCE.H
//============================================================================================================================================
// 🧩 The fixed tick order over both relations, the linearisation, the subsets and the name search.

#pragma once

#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"
#include "SlateDocument/Document/RegistrationIndex/Api/RegistrationIndex.h"
#include "SlateDocument/Document/PopulationIndex/Api/PopulationIndex.h"
#include "SlateDocument/Document/RevisionSequence/Api/RevisionSequence.h"
#include "SlateDocument/Document/RowSequence/Api/RowSequence.h"
#include "SlateDocument/Document/SceneStructure/Api/SceneStructure.h"
#include "SlateDocument/Document/SelectionSequence/Api/SelectionSequence.h"
#include "SlateDocument/Document/TrigramIndex/Api/TrigramIndex.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   DECLARED INTENT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What the interface asked the outliner to do. Never applied where it arrives; applied at ① of a tick.
/// note  🔴 `12` §7: reordering rows is a transaction against the enclosure relation, committed through
///        `RevisionSequence` like any other edit. Drag-reordering that mutated the relation directly would
///        bypass undo, and its absence from the sequence is discovered by the artist rather than by a test.
/// tag   guarantee
enum class OutlinerIntent : std::uint32_t
{
    Enclose           = 0u,   // [-] - place an owner in an enclosure at a position in its ordering
    Attach            = 1u,   // [-] - make an owner follow another's motion
    Rename            = 2u,   // [-] - declare an owner's name; re-derives its search entries at ⑦
    Select            = 3u,   // [-] - recorded in SelectionSequence, session-scoped
    ExcludeVisibility = 4u,   // [-] - recorded in RevisionSequence, scrubbed by undo
    Isolate           = 5u,   // [-] - recorded in RevisionSequence, scrubbed by undo
    Lock              = 6u,   // [-] - recorded in RevisionSequence, scrubbed by undo
    Expand            = 7u,   // [-] - a count adjustment; presentation only, never a document mutation
    Retire            = 8u,   // [-] - retire an owner, whole cascade included
    Narrow            = 9u    // [-] - narrow the rows to what a name search confirmed; not a document mutation
};

/// 🧩 One declared intent, complete enough to apply without consulting whoever declared it.
/// note  ⚠️ An intent carrying only "the selection" would apply against whatever the selection had become by
///        ①. Every operand is named here so that what is applied is what was meant.
/// note  🔴 `Narrow` is the one intent that addresses no owner. It carries its sought text and nothing
///        else, and `Declare` accepts it with an undeclared subject for exactly that reason.
/// tag   owning
struct DeclaredIntent
{
    OutlinerIntent    Declared             = OutlinerIntent::Select;   // [-] - what was asked
    OwnerIdentity  Subject              = {};                       // [-] - the owner it addresses
    OwnerIdentity  RelatedOwner      = {};                       // [-] - the enclosure or the attachment
    std::string       DeclaredName         = {};                       // [-] - supplied by Rename only
    std::string       SoughtText           = {};                       // [-] - supplied by Narrow only; empty widens
    std::uint32_t     OrderWithinEnclosure = 0u;                       // [-] - position in the enclosure ordering
    bool              CurrentEnabled      = true;                     // [-] - whether the subset holds it after
    bool              SelectionExtended    = false;                    // [-] - Select adds rather than replaces
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   REPORTED REFUSAL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One rejected intent, with both operands named — what `86` presents.
/// note  🔴 `12` §9: a relation change that would create a cycle is rejected at commit, reported as a Refusal
///        naming both owners, and never applied. Both identities are held here rather than formatted into
///        a message, so the reporting stays nonallocating and the presenter chooses the wording.
/// tag   owning
struct RejectedIntent
{
    DeclaredIntent  Rejected    = {};   // [-] - the intent as it was declared, both operands included
    Refusal         Declining  = {};   // [-] - why it was rejected; Detail is static text
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE OUTLINER
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Everything `12` owns, reconciled in the one order `12` §4 fixes.
/// note  🔴 The tick order is ① apply committed intent · ② reconcile the population · ③ compound attachments
///        · ④ repair enclosure labels · ⑤ rebuild rows and adjust counts · ⑥ re-derive changed subsets ·
///        ⑦ re-derive search entries for renamed owners. Every one of those orderings is load-bearing:
///        ③ before ④ because transforms must be final before anything spatial is derived from them, and
///        ④ before ⑤ because rows rebuilt against stale labels are briefly wrong and are displayed.
/// note  ⚠️ No linearisation is observed between ④ and ⑤ — invariant 10. `Rows()` reads the sequence the last
///        completed tick left, and `Reconcile` is the only writer of it.
/// tag   owning
class OutlinerSequence
{
public:

    /// 🧩 Registers one owner into the population and both relations, with a name.
    /// in    DeclaredName  [-]  what the artist called it; may be empty
    /// out   Result       [-]  refuses with ExtentExhausted at the population ceiling
    /// post  the owner sits last in the root ordering, attached to nothing, in no subset
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<OwnerIdentity> Register(const std::string& DeclaredName);

    /// 🧩 Declares one intent, to be applied at the next tick's ①.
    /// in    Incoming  [-]  the intent, every operand named
    /// out   Result   [-]  refuses with IdentityStale when the subject does not resolve now
    /// note  Declaring is not applying. An intent that arrives mid-tick is applied at the next ① rather than
    ///        against a linearisation that is halfway rebuilt.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Declare(const DeclaredIntent& Incoming);

    /// 🧩 Scrubs the document one transaction backwards, restoring the selection that transaction applied to.
    /// in    SealedAt  [ns]  accepted for symmetry with the tick; the restoration seals nothing — `84` §3
    /// out   Result   [-]   refuses with ExtentExhausted at the beginning of the revision sequence
    /// post  🔴 the document position and the standing selection moved together — `12` §11
    /// note  🔴 This is the defect `12` §11 warns of, closed: move three owners, undo, and the transforms
    ///        revert while the selection does not, so the next action applies to something other than what
    ///        the undo appeared to restore. Selection is not in the document's sequence and is not
    ///        unrevisioned either, and this is the seam where the two meet.
    /// note  A revision no selection was ever sealed at leaves the standing selection alone rather than
    ///        clearing it. The artist selected nothing there, so there is nothing to restore.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Retreat(std::uint64_t SealedAt);

    /// 🧩 Scrubs the document one transaction forwards, restoring the selection that transaction applied to.
    /// in    SealedAt  [ns]  accepted for symmetry with the tick; the restoration seals nothing — `84` §3
    /// out   Result   [-]   refuses with ExtentExhausted at the end of the revision sequence
    /// post  the document position and the standing selection moved together
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Advance(std::uint64_t SealedAt);

    /// 🧩 Runs one whole tick in the fixed order ①–⑦.
    /// in    SealedAt  [ns]  the arrival stamp transactions sealed this tick carry
    /// out   Result   [-]   refuses when a step refuses; the refusal carries that step's reason
    /// post  🔴 all ten invariants hold; nothing observed the linearisation between ④ and ⑤
    /// note  🔍 Invariants 3 and 4 are checked here on every reconciliation under SLATE_DEBUG; the remainder
    ///        are checked as each transaction seals.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> Reconcile(std::uint64_t SealedAt);

    /// 🧩 The rows the last completed tick linearised.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const RowSequence& Sequenced() const;

    /// 🧩 The named subsets as the last tick left them.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const RegistrationIndex& Registrations() const;

    /// 🧩 The name search over the population.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const TrigramIndex& Names() const;

    /// 🧩 The two relations, for the interval predicate `16` and `26` ask per owner.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const SceneStructure& Relations() const;

    /// 🧩 The document's committed transactions.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const RevisionSequence& Revisions() const;

    /// 🧩 The session's selections.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const SelectionSequence& Selections() const;

    /// 🧩 The text the standing row narrowing was derived from, empty when no narrowing stands.
    /// note  Held so that ⑦ can re-derive the narrowing when a rename changes what it would confirm. A
    ///        narrowing left standing across a rename retains owners whose names no longer match it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::string& Sought() const;

    /// 🧩 Every intent rejected since the last time the refusals were drained — what `86` presents.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<RejectedIntent>& Rejected() const;

    /// 🧩 Drains the refusals, once the presenter has reported them.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void ReclaimRejected();

    /// 🧩 🔍 Whether every invariant `12` §5 states holds right now.
    /// out   Held  [-]  false as soon as one does not; the caller reports which by asking the components
    /// cost  🔴
    /// tag   api, nonallocating, nonthrowing
    bool InvariantsHeld() const;

private:

    Deliver<bool> ApplyIntent(const DeclaredIntent& Applying, std::uint64_t SealedAt);
    Deliver<bool> ApplySubset(const DeclaredIntent& Applying, SubsetSubject Addressed, std::uint64_t SealedAt);
    Deliver<bool> ApplyNarrowing(const DeclaredIntent& Applying);
    Deliver<bool> DeriveNarrowing();
    Deliver<bool> ApplySelection(const std::vector<OwnerIdentity>& Current, std::uint64_t SealedAt);
    Deliver<bool> RegisterSelection(const std::vector<OwnerIdentity>& Current);
    Deliver<bool> RetireCascade(const DeclaredIntent& Applying, std::uint64_t SealedAt);
    void          Reject(const DeclaredIntent& Rejected, const Refusal& Declining);

    PopulationIndex                Population;                   // [-] - `10`'s slot index, reconciled at ②
    SceneStructure                 NestingRelations;             // [-] - both relations, reconciled at ③ and ④
    RowSequence                    Linearisation;                // [-] - rebuilt at ⑤
    RegistrationIndex                Subsets;                      // [-] - re-derived at ⑥
    TrigramIndex                   NameSearch;                   // [-] - re-derived at ⑦
    RevisionSequence               Revised;                      // [-] - where document mutations are recorded
    SelectionSequence              Selected;                     // [-] - where selection is recorded
    std::vector<DeclaredIntent>    PendingDeclarations;          // [-] - declared, awaiting the next ①
    std::vector<RejectedIntent>    RejectedDeclarations;          // [-] - rejected, awaiting `86`
    std::vector<DeclaredIntent>    RenamedDeclarations;          // [-] - ⑦ re-derives exactly these, name included
    std::vector<OwnerIdentity>  RemovedOwners;           // [-] - ② withdraws exactly these
    std::vector<std::uint32_t>     LiveGenerations;              // [-] - per slot, for the invariant 6 check
    std::string                    NarrowingSought;              // [-] - the standing narrowing's text; empty widens
    bool                           NarrowingOwed = false;        // [-] - ⑦ must confirm the sought text again
};

}   // namespace Slate
