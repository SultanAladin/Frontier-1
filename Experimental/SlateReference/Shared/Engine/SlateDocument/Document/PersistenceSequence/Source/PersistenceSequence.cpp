//============================================================================================================================================
//                                                         PERSISTENCESEQUENCE.CPP
//============================================================================================================================================
// 🧩 `48` §3 — write, verify, replace: the existing file is never touched until a complete replacement has been read back.

#include "SlateDocument/Document/PersistenceSequence/Api/PersistenceSequence.h"

#include <cstddef>
#include <cstring>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE VERIFICATION
//------------------------------------------------------------------------------------------------------------------------

bool PersistenceSequence::VerifyIdentical(const std::vector<std::uint8_t>& Written,
                                          const std::vector<std::uint8_t>& ReadBack)
{
    if (Written.size() != ReadBack.size()) { return false; }
    if (Written.empty())                 { return true;  }

    return std::memcmp(Written.data(), ReadBack.data(), Written.size()) == 0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

Deliver<PersistenceConclusion> PersistenceSequence::Persist(const SealedContent& Sealed)
{
    Reached = PersistenceStep::Unbegun;

    if (Sealed.TargetPath.empty())
    {
        return Deliver<PersistenceConclusion>::Refuse(
            { RefusalReason::ContentUnsupported, "a save names where it is to land — `48` §3" });
    }

    if (Sealed.Content.empty())
    {
        // 🔴 An empty stream is rejected rather than written. Writing it would replace a good document with a
        //    file carrying no heading at all, and `48` §3's whole sequence exists to stop a save from being
        //    the thing that destroys the artist's previous save.
        return Deliver<PersistenceConclusion>::Refuse(
            { RefusalReason::ContentUnsupported, "a document stream of no bytes is not a document — `48` §3" });
    }

    // 🔴 ①②③ are one call at this seam and not three. `FileInterchange::WriteStream` writes beside the target,
    //    reads it back, compares it, and only then renames — and the rename is atomic within one file system.
    //    Splitting them here would put the staged stream's path in two places, and a host that died between
    //    two of the calls would leave the artist a directory with two documents and no way to tell which.
    const Deliver<bool> ReadBack = FileInterchange::WriteStream(Sealed.TargetPath, Sealed.Content);

    if (!ReadBack.Resolved)
    {
        // 📝 Which step the surface rejected at is carried in its own reason: ExtentExhausted is ② failing the
        //    comparison, and HostDenied is ① or ③. Either way the existing file stands, which is the fact the
        //    artist needs and the reason nothing is amended here before returning.
        Reached = ReadBack.Error.DeclaredReason == RefusalReason::ExtentExhausted
                ? PersistenceStep::Written
                : PersistenceStep::Unbegun;

        return Deliver<PersistenceConclusion>::Refuse(ReadBack.Error);
    }

    Reached = PersistenceStep::Replaced;
    ++ReplacedTotal;

    PersistenceConclusion Completed;
    Completed.Reached      = PersistenceStep::Replaced;
    Completed.SavedThrough = Sealed.SavedThrough;
    Completed.SavedAt      = Sealed.SealedAt;
    Completed.SpannedBytes = static_cast<std::uint64_t>(Sealed.Content.size());

    // 📝 ④ — retiring the journal entries this save subsumes — is the caller's, on the tick. The journal belongs
    //    to the session and this runs off it, and `48` §3 ④ makes an unretired entry merely redundant on replay
    //    rather than wrong. Retiring it from here would be the one step of the four that could lose work.
    return Deliver<PersistenceConclusion>::Result(Completed);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE READINGS
//------------------------------------------------------------------------------------------------------------------------

PersistenceStep PersistenceSequence::LastReached() const
{
    return Reached;
}

std::uint32_t PersistenceSequence::ReplacedCount() const
{
    return ReplacedTotal;
}

}   // namespace Slate
