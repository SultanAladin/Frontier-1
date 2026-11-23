//============================================================================================================================================
//                                                           OUTLINERSEQUENCE.CPP
//============================================================================================================================================
// 🧩 The seven steps in order, every mutation a transaction, and the retirement cascade as one of them.

#include "SlateDocument/Document/OutlinerSequence/Api/OutlinerSequence.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENROLMENT
//------------------------------------------------------------------------------------------------------------------------

Deliver<OwnerIdentity> OutlinerSequence::Register(const std::string& DeclaredName)
{
    const Deliver<OwnerIdentity> Registered = Population.Register();

    if (!Registered.Resolved)
        return Registered;

    const OwnerIdentity Incoming = Registered.Resolve();

    const Deliver<bool> Accepted = NestingRelations.Accept(Incoming);

    if (!Accepted.Resolved)
    {
        // 📝 The slot is withdrawn again rather than left registered in a population the relations do not hold.
        //    A slot present in one and absent from the other is invariant 6 broken at the moment of arrival.
        Discard(Population.Withdraw(Incoming));
        return Deliver<OwnerIdentity>::Refuse(Accepted.Error);
    }

    if (!DeclaredName.empty())
        Discard(NameSearch.Declare(Incoming, DeclaredName));

    const std::size_t Required = static_cast<std::size_t>(Incoming.SlotIndex) + 1u;

    if (Required > LiveGenerations.size())
        LiveGenerations.resize(Required, 0u);

    LiveGenerations[Incoming.SlotIndex] = Incoming.SlotGeneration;

    // 📝 An owner incoming while a narrowing stands is retained by nothing, so the narrowing is owed again
    //    at ⑦. Without it the arrival is hidden by a search its own name may well confirm.
    NarrowingOwed = true;

    return Deliver<OwnerIdentity>::Result(Incoming);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   DECLARED INTENT
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> OutlinerSequence::Declare(const DeclaredIntent& Incoming)
{
    // 📝 Narrowing addresses the whole sequence rather than one owner, so it is the one intent accepted
    //    with an undeclared subject. Every other intent names what it applies to and is gated on it here.
    if (Incoming.Declared != OutlinerIntent::Narrow && !Population.Resolve(Incoming.Subject))
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "the intent addresses no registered owner" });

    PendingDeclarations.push_back(Incoming);

    return Deliver<bool>::Result(true);
}

void OutlinerSequence::Reject(const DeclaredIntent& Rejected, const Refusal& Declining)
{
    RejectedIntent Reported;
    Reported.Rejected   = Rejected;
    Reported.Declining = Declining;

    RejectedDeclarations.push_back(Reported);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SUBSET INTENT
//------------------------------------------------------------------------------------------------------------------------

// 📝 The subset half of a selection, taken on its own so that a selection incoming as intent and one restored
//    by a scrub reach the same registration without the scrub sealing a selection the artist never made.
Deliver<bool> OutlinerSequence::RegisterSelection(const std::vector<OwnerIdentity>& Current)
{
    Subsets.Reclaim(SubsetSubject::Selection);

    for (const OwnerIdentity& Registering : Current)
    {
        const Deliver<bool> Held = Subsets.Register(Registering, SubsetSubject::Selection);

        if (!Held.Resolved)
            return Held;
    }

    return Deliver<bool>::Result(true);
}

Deliver<bool> OutlinerSequence::ApplySelection(const std::vector<OwnerIdentity>& Current,
                                               std::uint64_t                        SealedAt)
{
    const Deliver<bool> Registered = RegisterSelection(Current);

    if (!Registered.Resolved)
        return Registered;

    Selected.Seal(Current, Revised.Committed().size());

    // 📝 The stamp is accepted and unused: selection is sealed against the document revision it stands at
    //    rather than against an arrival, which is what `12` §11 pairs a scrub with. Taking it keeps every
    //    applier's signature the same, so nothing has to know which of them consults the clock.
    static_cast<void>(SealedAt);

    return Deliver<bool>::Result(true);
}

Deliver<bool> OutlinerSequence::ApplySubset(const DeclaredIntent& Applying,
                                            SubsetSubject         Addressed,
                                            std::uint64_t         SealedAt)
{
    // 🔴 `12` §11: every subset mutation is a transaction without exception. What differs between the subsets
    //    is where the transaction is recorded, and selection is the only one recorded outside the document.
    if (Addressed == SubsetSubject::Selection)
    {
        std::vector<OwnerIdentity> Current = Applying.SelectionExtended
                                               ? Selected.Current()
                                               : std::vector<OwnerIdentity>{};

        if (Applying.CurrentEnabled)
        {
            bool Held = false;

            for (const OwnerIdentity& Registered : Current)
            {
                if (Registered == Applying.Subject)
                {
                    Held = true;
                    break;
                }
            }

            if (!Held)
                Current.push_back(Applying.Subject);
        }
        else
        {
            for (std::size_t Index = 0u; Index < Current.size(); ++Index)
            {
                if (Current[Index] == Applying.Subject)
                {
                    Current.erase(Current.begin() + static_cast<std::ptrdiff_t>(Index));
                    break;
                }
            }
        }

        return ApplySelection(Current, SealedAt);
    }

    const Deliver<bool> Opened = Revised.Open("", Applying.CurrentEnabled ? "RegisterSubset" : "UnenrolSubset");

    if (!Opened.Resolved)
        return Opened;

    const Deliver<bool> Registered = Applying.CurrentEnabled
                                 ? Subsets.Register(Applying.Subject, Addressed)
                                 : Subsets.Unenrol(Applying.Subject, Addressed);

    if (!Registered.Resolved)
    {
        // 📝 Abandoned rather than sealed. A rejected registration that sealed anyway would put a transaction in
        //    the sequence whose forward operation does nothing and whose inverse undoes nothing.
        Revised.Abandon();
        return Registered;
    }

    return Revised.Seal(SealedAt, false);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  NARROWING INTENT
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> OutlinerSequence::ApplyNarrowing(const DeclaredIntent& Applying)
{
    // 📝 `14` §4.1 places the sought text beside the document, so narrowing is not a transaction and nothing
    //    here opens one. The text is held rather than the confirmed set, because the set has to be derived
    //    again whenever a rename or a retirement changes what the same text confirms.
    NarrowingSought = Applying.SoughtText;
    NarrowingOwed   = true;

    // 🔴 The rows are not narrowed here. ① runs before ⑤ rebuilds them, so a set confirmed now would be
    //    retained against row ordinals the rebuild is about to reassign. The narrowing is derived at ⑦, where
    //    both the rows and the search entries are final.
    return Deliver<bool>::Result(true);
}

Deliver<bool> OutlinerSequence::DeriveNarrowing()
{
    if (NarrowingSought.empty())
        return Linearisation.DeclareNarrowing({}, false);

    // 🔴 `12` §3: approximate index, exact confirmation. Narrow confirms each candidate against the whole
    //    name, so what the rows retain is what genuinely contains the text rather than what shares a trigram.
    return Linearisation.DeclareNarrowing(NameSearch.Narrow(NarrowingSought), true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      SCRUBBING
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> OutlinerSequence::Retreat(std::uint64_t SealedAt)
{
    const Deliver<bool> Scrubbed = Revised.Retreat();

    if (!Scrubbed.Resolved)
        return Scrubbed;

    // 🔴 The selection is restored to what the arrived-at position was selected against, and the restoration
    //    seals nothing. `84` §3: scrubbing to position twelve and back is not an edit, and a sequence that
    //    recorded its own navigation is one no artist can reason about.
    if (Selected.RestoreAt(Revised.ScrubPosition()).Resolved)
    {
        const Deliver<bool> Registered = RegisterSelection(Selected.Current());

        if (!Registered.Resolved)
            return Registered;
    }

    static_cast<void>(SealedAt);

    return Deliver<bool>::Result(true);
}

Deliver<bool> OutlinerSequence::Advance(std::uint64_t SealedAt)
{
    const Deliver<bool> Scrubbed = Revised.Advance();

    if (!Scrubbed.Resolved)
        return Scrubbed;

    if (Selected.RestoreAt(Revised.ScrubPosition()).Resolved)
    {
        const Deliver<bool> Registered = RegisterSelection(Selected.Current());

        if (!Registered.Resolved)
            return Registered;
    }

    static_cast<void>(SealedAt);

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                THE RETIREMENT CASCADE
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> OutlinerSequence::RetireCascade(const DeclaredIntent& Applying, std::uint64_t SealedAt)
{
    // 🔴 `12` §12: retirement is one transaction including its whole cascade. A cascade committed as several
    //    transactions is undone in pieces, and the intermediate pieces are states the document was never in.
    const Deliver<bool> Opened = Revised.Open("", "RetireOwner");

    if (!Opened.Resolved)
        return Opened;

    // 📝 The relations re-enclose what the owner enclosed and reattach what followed it. `12` §12 makes
    //    that a declared policy rather than a default: deleting a group deletes the group, not the work.
    const Deliver<bool> Retired = NestingRelations.Retire(Applying.Subject);

    if (!Retired.Resolved)
    {
        Revised.Abandon();
        return Retired;
    }

    Subsets.UnenrolEverywhere(Applying.Subject);
    NameSearch.Withdraw(Applying.Subject);

    // 📝 The population slot is withdrawn at ② rather than here, so that everything applied in this ① still
    //    resolves against the generation it was declared with.
    RemovedOwners.push_back(Applying.Subject);

    return Revised.Seal(SealedAt, false);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   APPLYING INTENT
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> OutlinerSequence::ApplyIntent(const DeclaredIntent& Applying, std::uint64_t SealedAt)
{
    switch (Applying.Declared)
    {
        case OutlinerIntent::Enclose:
        {
            // 🔴 Reordering a row is a transaction against the enclosure relation, committed like any other
            //    edit. `12` §7: a drag that mutated the relation directly would bypass undo entirely.
            const Deliver<bool> Opened = Revised.Open("", "EncloseOwner");

            if (!Opened.Resolved)
                return Opened;

            const Deliver<bool> Enclosed = NestingRelations.Enclose(Applying.Subject,
                                                                   Applying.RelatedOwner,
                                                                   Applying.OrderWithinEnclosure);

            if (!Enclosed.Resolved)
            {
                Revised.Abandon();
                return Enclosed;
            }

            return Revised.Seal(SealedAt, false);
        }

        case OutlinerIntent::Attach:
        {
            const Deliver<bool> Opened = Revised.Open("", "AttachOwner");

            if (!Opened.Resolved)
                return Opened;

            const Deliver<bool> Attached = NestingRelations.Attach(Applying.Subject, Applying.RelatedOwner);

            if (!Attached.Resolved)
            {
                Revised.Abandon();
                return Attached;
            }

            return Revised.Seal(SealedAt, false);
        }

        case OutlinerIntent::Rename:
        {
            const Deliver<bool> Opened = Revised.Open("", "RenameOwner");

            if (!Opened.Resolved)
                return Opened;

            // 📝 The whole declaration is held, not just the subject, because ① clears the pending run before
            //    ⑦ reads it. `12` §4 makes ⑦ not optional: search answering with a name the artist already
            //    changed is worse than search that finds nothing.
            RenamedDeclarations.push_back(Applying);

            return Revised.Seal(SealedAt, false);
        }

        case OutlinerIntent::Select:
            return ApplySubset(Applying, SubsetSubject::Selection, SealedAt);

        case OutlinerIntent::ExcludeVisibility:
            return ApplySubset(Applying, SubsetSubject::VisibilityExclusion, SealedAt);

        case OutlinerIntent::Isolate:
            return ApplySubset(Applying, SubsetSubject::Isolation, SealedAt);

        case OutlinerIntent::Lock:
            return ApplySubset(Applying, SubsetSubject::Lock, SealedAt);

        case OutlinerIntent::Expand:
            // 📝 Expansion is a count adjustment and mutates nothing in the document, so it is not a
            //    transaction. `14` §4.1 rules the same way for every non-document state: undo must not step
            //    back through the artist collapsing a row.
            return Linearisation.DeclareExpansion(Applying.Subject, Applying.CurrentEnabled);

        case OutlinerIntent::Retire:
            return RetireCascade(Applying, SealedAt);

        case OutlinerIntent::Narrow:
            return ApplyNarrowing(Applying);
    }

    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the declared intent has no applier" });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE TICK ORDER
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> OutlinerSequence::Reconcile(std::uint64_t SealedAt)
{
    // ① Apply committed intent. Every refusal is reported and the intent is never partly applied.
    for (const DeclaredIntent& Applying : PendingDeclarations)
    {
        if (!Population.Resolve(Applying.Subject))
        {
            Reject(Applying, { RefusalReason::IdentityStale, "the owner was retired before the intent applied" });
            continue;
        }

        const Deliver<bool> Applied = ApplyIntent(Applying, SealedAt);

        if (!Applied.Resolved)
            Reject(Applying, Applied.Error);
    }

    PendingDeclarations.clear();

    // ② Reconcile the population — retire the slots whose generation this tick advanced.
    for (const OwnerIdentity& Departing : RemovedOwners)
    {
        Discard(Population.Withdraw(Departing));

        if (Departing.SlotIndex < LiveGenerations.size())
            LiveGenerations[Departing.SlotIndex] = 0u;
    }

    RemovedOwners.clear();

    // ③ Reconcile the attachment relation, compounding transforms downward from each attachment root. Before
    //    ④ deliberately: transforms must be final before anything spatial is derived from them.
    const Deliver<bool> Compounded = NestingRelations.CompoundAttachments();

    if (!Compounded.Resolved)
        return Compounded;

    // ④ Reconcile the enclosure relation, repairing interval labels where gaps were exhausted.
    const Deliver<bool> Repaired = NestingRelations.RepairLabels();

    if (!Repaired.Resolved)
        return Repaired;

#if defined(SLATE_DEBUG)
    // 🔍 Invariants 3 and 4 are checked on every reconciliation — `12` §5. The remainder are checked as each
    //    transaction seals, which is why they are not repeated here.
    if (!NestingRelations.RelationsAcyclic() || !NestingRelations.LabelsNested())
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::RelationCyclic, "a relation held a cycle or a label was not strictly nested" });
    }
#endif

    // ⑤ Rebuild the rows and adjust the counts. After ④ deliberately: rows rebuilt against stale labels
    //    produce an order that is briefly wrong and is displayed while it is.
    const Deliver<bool> Linearized = Linearisation.Linearize(NestingRelations);

    if (!Linearized.Resolved)
        return Linearized;

    // ⑥ Re-derive the subsets whose registration changed. Retired slots left every subset at ①, so what
    //    remains is confirming that no registration outlived its owner.
    if (!Subsets.RegistrationsOccupied(LiveGenerations))
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::IdentityStale, "a subset held a slot that is no longer occupied" });
    }

    // ⑦ Re-derive the search entries for owners whose name changed, within this same tick. Each declaration
    //    carries its own name, so nothing here consults a run ① has already cleared.
    for (const DeclaredIntent& Naming : RenamedDeclarations)
    {
        Discard(NameSearch.Declare(Naming.Subject, Naming.DeclaredName));
        NarrowingOwed = true;
    }

    RenamedDeclarations.clear();

    // 🔴 The narrowing is derived here rather than at ①. ⑤ reassigns every row ordinal and the entries above
    //    are only final now, so a set confirmed earlier would be retained against rows the rebuild discarded.
    if (NarrowingOwed)
    {
        const Deliver<bool> Narrowed = DeriveNarrowing();

        if (!Narrowed.Resolved)
            return Narrowed;

        NarrowingOwed = false;
    }

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

const RowSequence& OutlinerSequence::Sequenced() const
{
    return Linearisation;
}

const RegistrationIndex& OutlinerSequence::Registrations() const
{
    return Subsets;
}

const TrigramIndex& OutlinerSequence::Names() const
{
    return NameSearch;
}

const SceneStructure& OutlinerSequence::Relations() const
{
    return NestingRelations;
}

const RevisionSequence& OutlinerSequence::Revisions() const
{
    return Revised;
}

const SelectionSequence& OutlinerSequence::Selections() const
{
    return Selected;
}

const std::string& OutlinerSequence::Sought() const
{
    return NarrowingSought;
}

const std::vector<RejectedIntent>& OutlinerSequence::Rejected() const
{
    return RejectedDeclarations;
}

void OutlinerSequence::ReclaimRejected()
{
    RejectedDeclarations.clear();
}

bool OutlinerSequence::InvariantsHeld() const
{
    // 📝 Invariants 1, 2, 8 and 9 are structural: the relations hold one enclosure and one attachment per
    //    owner by storage rather than by check, retirement withdraws from both and from every subset in one
    //    routine, and row order has no input but the enclosure relation. What remains measurable is checked.
    return NestingRelations.RelationsAcyclic()
        && NestingRelations.LabelsNested()
        && Linearisation.CountsAgree()
        && Subsets.RegistrationsOccupied(LiveGenerations);
}

}   // namespace Slate
