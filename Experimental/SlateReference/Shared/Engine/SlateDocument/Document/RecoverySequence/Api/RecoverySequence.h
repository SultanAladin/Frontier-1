//============================================================================================================================================
//                                                            RECOVERYSEQUENCE.H
//============================================================================================================================================
// 🧩 `48` §4 — the per-document journal appended as transactions seal, and the replay that is offered and never applied.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateDocument/Document/RevisionSequence/Api/RevisionSequence.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ONE JOURNAL ENTRY
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One sealed transaction as the journal carries it — the tail of `RevisionSequence` since the last save.
/// note  🔴 `48` §4: the journal is not a copy of the document. It is bounded by what the transactions touched
///        rather than by the population's size, which is what lets it be written at stroke rate on a document
///        too large to save at stroke rate.
/// tag   owning
struct JournalEntry
{
    std::string    Description     = {};   // [-] - what `84` presents; carried verbatim from the transaction
    std::string    OperationName   = {};   // [-] - the mechanism's spelling
    std::uint64_t  RevisionIndex = 0u;   // [-] - which transaction of the document sequence this is
    std::uint64_t  SealedAt        = 0u;   // [ns] - the arrival stamp the transaction sealed at
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT AN OPEN FINDS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The four situations `48` §4 tabulates, decided on read and never inferred later.
/// tag   guarantee
enum class RecoveryCurrent : std::uint32_t
{
    NothingOffered = 0u,   // [-] - the journal is empty; the application exited cleanly
    Offered        = 1u,   // [-] - entries stand past the save and are offered, named
    PartlyOffered  = 2u,   // [-] - readable up to an entry; the rest is reported rather than guessed
    Unreadable     = 3u,   // [-] - nothing of the journal could be read; `86` reports and nothing is offered
    CurrentCount  = 4u    // [-] - the closed count, never a standing
};

/// 🧩 What the artist is told before they decide — `48` §4's offer, stated rather than performed.
/// note  🔴 The offer names both stamps and the separation between them. An offer that said only "recover?"
///        asks the artist to choose between two things they cannot see, and the wrong choice discards an
///        afternoon either way.
/// tag   owning
struct RecoveryOffer
{
    RecoveryCurrent  Current        = RecoveryCurrent::NothingOffered;   // [-] - which of the four rows
    std::uint64_t     SavedAt         = 0u;   // [ns] - the saved document's own stamp
    std::uint64_t     JournalledAt    = 0u;   // [ns] - the last readable entry's stamp
    std::uint32_t     OfferedCount    = 0u;   // [-]  - transactions separating the two
    std::uint32_t     UnreadableFrom  = 0u;   // [-]  - first entry that could not be read; meaningful when PartlyOffered
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE JOURNAL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One document's journal — appended as transactions seal, retired by a save, replayed only when accepted.
/// note  🔴 `48` §4.1: per document, never per application. Two documents open and one crash leaves two journals,
///        each offered against its own file. A single journal covering the session cannot be replayed into one
///        document without replaying it into the other, and the artist would meet that as one document acquiring
///        the other's edits.
/// note  🔴 Recovery is **offered**, never applied. There is deliberately no routine that replays without an
///        acceptance: an artist who abandoned an experiment by closing without saving must not be handed it back
///        as though it were the file.
/// tag   owning
class RecoverySequence
{
public:

    // 📝 The journal's write interval is `48` §10's open row and is tuning only. Entries are appended per sealed
    //    transaction here, which is the bound that loses the least; batching them is a change to when Persist
    //    runs and not to what an entry is.
    static constexpr std::uint32_t EntryLimit = 65536u;   // [-] - entries retained before the oldest is retired

    /// 🧩 Names the document this journal belongs to, and the journal's own storage location.
    /// in    DeclaredDocument  [-]  UTF-8; the document the journal is offered against
    /// in    DeclaredJournal   [-]  UTF-8; where the journal itself is written
    /// out   Result           [-]  refuses with ContentUnsupported when either path is empty
    /// note  🔴 Both paths are held because §4.1's pairing is what makes the offer meaningful. A journal that
    ///        did not know its document could be replayed into whichever document happened to be open.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareDocument(const std::string& DeclaredDocument, const std::string& DeclaredJournal);

    /// 🧩 Appends one sealed transaction to the journal.
    /// in    Sealing  [-]  the transaction as `RevisionSequence` sealed it
    /// in    RevisionIndex  [-]  where it sits in the document's committed order
    /// out   Result  [-]  refuses with ContentUnsupported when no document is declared
    /// post  the retained count never exceeds EntryLimit; the oldest entry leaves when it would
    /// note  ⚠️ An entry leaving at the ceiling makes the journal a **suffix** rather than a tail, and a suffix
    ///        cannot be replayed from the saved file. OfferReplay reports that as PartlyOffered, so the artist
    ///        is told the recovery is partial rather than handed a document assembled from a gap.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Append(const CommittedTransaction& Sealing, std::uint64_t RevisionIndex);

    /// 🧩 Retires every entry a completed save subsumes — `48` §3 ④.
    /// in    SavedThrough  [-]  the revision ordinal the saved document carries
    /// post  replay of what remains is redundant rather than wrong; a retired entry is one already in the file
    /// note  📝 Retired after the replacement, never before it. Retiring first means a save that fails at ③ has
    ///        discarded the journal that would have recovered the very transactions it failed to write.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Retire(std::uint64_t SavedThrough);

    /// 🧩 Reads what stands past the save and states the offer — never applies it.
    /// in    SavedAt       [ns] the saved document's own stamp
    /// in    SavedThrough  [-]  the revision ordinal the saved document carries
    /// out   RecoveryOffer [-]  NothingOffered when the journal holds nothing past the save
    /// note  🔴 This decides nothing on the artist's behalf. `48` §4's whole rule is that the offer states what
    ///        it is — both stamps and the count between them — and the artist answers it.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    RecoveryOffer OfferReplay(std::uint64_t SavedAt, std::uint64_t SavedThrough) const;

    /// 🧩 Declares one entry unreadable, so the offer stops at it rather than past it.
    /// in    EntryIndex  [-]  the first entry that could not be read
    /// post  every entry from this ordinal on is excluded from the offer — `48` §4
    /// note  🔴 Entries up to the failure are offered and the rest is reported. Offering past a gap would
    ///        replay transactions against a document that is missing the ones before them, and the result is a
    ///        document that never existed at any moment of the artist's session.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeclareUnreadable(std::uint32_t EntryIndex);

    /// 🧩 The entries an accepted offer would replay, in sealing order.
    /// in    SavedThrough  [-]  the revision ordinal the saved document carries
    /// out   Entries       [-]  those past the save and before any unreadable entry
    /// note  📝 The caller replays these; this hands them over and applies nothing. Applying here would put the
    ///        replay inside a component that cannot seal a transaction, and every replayed edit would sit
    ///        outside the sequence that scrubs it.
    /// cost  🚩
    /// tag   api, nonthrowing
    std::vector<JournalEntry> Offered(std::uint64_t SavedThrough) const;

    /// 🧩 Every entry the journal retains, in sealing order.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<JournalEntry>& Retained() const;

    /// 🧩 The document this journal is offered against.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::string& DocumentOrigin() const;

    /// 🧩 Where the journal itself is written.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::string& JournalOrigin() const;

    /// 🧩 How many entries the ceiling has retired — itself a fact the offer has to carry.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t DiscardedCount() const;

    /// 🧩 Empties the journal. Called when the document it belongs to closes cleanly.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Reclaim();

private:

    std::vector<JournalEntry>  Entries;                          // [-] - in sealing order
    std::string                DocumentPath      = {};           // [-] - UTF-8; the document offered against
    std::string                JournalPath       = {};           // [-] - UTF-8; where the journal is written
    std::uint32_t              UnreadableIndex = EntryLimit; // [-] - first unreadable entry; the ceiling for none
    std::uint32_t              DiscardedTotal    = 0u;           // [-] - entries the ceiling retired
};

}   // namespace Slate
