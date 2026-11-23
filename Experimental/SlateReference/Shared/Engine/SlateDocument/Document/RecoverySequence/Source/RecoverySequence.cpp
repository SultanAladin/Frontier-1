//============================================================================================================================================
//                                                           RECOVERYSEQUENCE.CPP
//============================================================================================================================================
// 🧩 `48` §4 — the per-document journal appended as transactions seal, and the replay that is offered and never applied.

#include "SlateDocument/Document/RecoverySequence/Api/RecoverySequence.h"

#include <cstddef>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DECLARATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> RecoverySequence::DeclareDocument(const std::string& DeclaredDocument, const std::string& DeclaredJournal)
{
    if (DeclaredDocument.empty() || DeclaredJournal.empty())
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a journal knows which document it belongs to — `48` §4.1" });
    }

    DocumentPath = DeclaredDocument;
    JournalPath  = DeclaredJournal;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE APPENDING
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> RecoverySequence::Append(const CommittedTransaction& Sealing, std::uint64_t RevisionIndex)
{
    if (DocumentPath.empty())
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "no document is declared for this journal — `48` §4.1" });
    }

    JournalEntry Appending;
    Appending.Description     = Sealing.Description;
    Appending.OperationName   = Sealing.OperationName;
    Appending.RevisionIndex = RevisionIndex;
    Appending.SealedAt        = Sealing.SealedAt;

    Entries.push_back(Appending);

    if (Entries.size() > static_cast<std::size_t>(EntryLimit))
    {
        // ⚠️ The oldest entry leaves and the discard is counted rather than forgotten. A journal that silently
        //    dropped its first entries would offer a replay that begins mid-session, and the artist would read
        //    the result as the recovery having invented a document.
        Entries.erase(Entries.begin());
        ++DiscardedTotal;
    }

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE RETIREMENT
//------------------------------------------------------------------------------------------------------------------------

void RecoverySequence::Retire(std::uint64_t SavedThrough)
{
    std::size_t Retiring = 0u;

    while (Retiring < Entries.size() && Entries[Retiring].RevisionIndex <= SavedThrough)
    {
        ++Retiring;
    }

    if (Retiring == 0u) { return; }

    Entries.erase(Entries.begin(), Entries.begin() + static_cast<std::ptrdiff_t>(Retiring));

    // 📝 An unreadable ordinal addresses a position in the retained entries, so retiring from the front moves
    //    it. Left unmoved it would exclude entries that are perfectly readable and are past the save.
    if (UnreadableIndex != EntryLimit)
    {
        UnreadableIndex = UnreadableIndex > static_cast<std::uint32_t>(Retiring)
                          ? UnreadableIndex - static_cast<std::uint32_t>(Retiring)
                          : 0u;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE OFFER
//------------------------------------------------------------------------------------------------------------------------

RecoveryOffer RecoverySequence::OfferReplay(std::uint64_t SavedAt, std::uint64_t SavedThrough) const
{
    RecoveryOffer Stating;
    Stating.SavedAt        = SavedAt;
    Stating.UnreadableFrom = UnreadableIndex;

    if (UnreadableIndex == 0u)
    {
        // 🔴 Nothing of the journal was readable. `48` §4 reports it and offers nothing, because an offer of
        //    zero transactions presented as a recovery teaches the artist that recovery does nothing.
        Stating.Current = RecoveryCurrent::Unreadable;
        return Stating;
    }

    std::uint32_t  Offering    = 0u;
    std::uint64_t  LastReadable = 0u;

    for (std::size_t Index = 0u; Index < Entries.size(); ++Index)
    {
        if (Index >= static_cast<std::size_t>(UnreadableIndex)) { break; }
        if (Entries[Index].RevisionIndex <= SavedThrough)       { continue; }

        ++Offering;
        LastReadable = Entries[Index].SealedAt;
    }

    Stating.OfferedCount = Offering;
    Stating.JournalledAt = LastReadable;

    if (Offering == 0u)
    {
        Stating.Current = RecoveryCurrent::NothingOffered;
        return Stating;
    }

    // ⚠️ Partial for either reason — an entry that could not be read, or entries the ceiling discarded before
    //    the save. Both leave a gap between the file and the offer, and the artist is told which they have.
    const bool GapCurrent = UnreadableIndex != EntryLimit || DiscardedTotal > 0u;

    Stating.Current = GapCurrent ? RecoveryCurrent::PartlyOffered : RecoveryCurrent::Offered;

    return Stating;
}

void RecoverySequence::DeclareUnreadable(std::uint32_t EntryIndex)
{
    if (EntryIndex < UnreadableIndex)
    {
        UnreadableIndex = EntryIndex;
    }
}

std::vector<JournalEntry> RecoverySequence::Offered(std::uint64_t SavedThrough) const
{
    std::vector<JournalEntry> Offering;

    for (std::size_t Index = 0u; Index < Entries.size(); ++Index)
    {
        if (Index >= static_cast<std::size_t>(UnreadableIndex)) { break; }
        if (Entries[Index].RevisionIndex <= SavedThrough)       { continue; }

        Offering.push_back(Entries[Index]);
    }

    return Offering;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE READINGS
//------------------------------------------------------------------------------------------------------------------------

const std::vector<JournalEntry>& RecoverySequence::Retained() const
{
    return Entries;
}

const std::string& RecoverySequence::DocumentOrigin() const
{
    return DocumentPath;
}

const std::string& RecoverySequence::JournalOrigin() const
{
    return JournalPath;
}

std::uint32_t RecoverySequence::DiscardedCount() const
{
    return DiscardedTotal;
}

void RecoverySequence::Reclaim()
{
    Entries.clear();
    UnreadableIndex = EntryLimit;
    DiscardedTotal    = 0u;
}

}   // namespace Slate
