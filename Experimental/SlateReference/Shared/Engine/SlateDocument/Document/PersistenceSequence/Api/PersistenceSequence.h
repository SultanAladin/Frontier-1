//============================================================================================================================================
//                                                          PERSISTENCESEQUENCE.H
//============================================================================================================================================
// 🧩 `48` §3 — write, verify, replace: the existing file is never touched until a complete replacement has been read back.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateDocument/Format/FormatCodec/Api/FormatCodec.h"
#include "SlateMath/Platform/FileInterchange/Api/FileInterchange.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE FOUR STEPS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Where a save stands, so a refusal names which of `48` §3's four steps rejected.
/// note  🔴 The step matters to the artist, not just to the reporter. A refusal at ① or ② means their previous
///        save is untouched and nothing is lost; a refusal at ③ means the replacement exists beside the target
///        and can be recovered by hand. Reporting all three as "the save failed" throws that away.
/// tag   guarantee
enum class PersistenceStep : std::uint32_t
{
    Unbegun     = 0u,   // [-] - ① has not run
    Written     = 1u,   // [-] - ① complete; a full replacement stands beside the target
    Verified    = 2u,   // [-] - ② complete; what landed matches what was written, byte for byte
    Replaced    = 3u,   // [-] - ③ complete; the target holds the new content
    Retired     = 4u,   // [-] - ④ complete; the journal entries the save subsumes are gone
    StepCount   = 5u    // [-] - the closed count, never a step
};

/// 🧩 One completed save, and what it leaves true of the document that requested it.
/// tag   nonallocating, nonthrowing
struct PersistenceConclusion
{
    PersistenceStep  Reached        = PersistenceStep::Unbegun;   // [-]  - how far the sequence got
    std::uint64_t    SavedThrough   = 0u;                         // [-]  - the revision ordinal the file carries
    std::uint64_t    SavedAt        = 0u;                         // [ns] - when the replacement landed
    std::uint64_t    SpannedBytes   = 0u;                         // [B]  - what was written
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE SEALED STATE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Everything a save reads, captured at declaration so the resolution reads nothing that can change.
/// note  🔴 `48` §3: saving reads a **sealed** state. `10` §2.4's open transactions are not written, because a
///        half-finished drag is not a state the artist asked to keep — and a document that reopened mid-drag
///        would present a stroke the artist never let go of.
/// note  🔴 `34` §2: the whole point of this being a captured copy is that the save runs off the tick and the
///        artist keeps texturing while it does. A save that read the live document would read it mid-transaction
///        on whichever machine happened to be slow enough, and `48` §3's non-blocking rule would be the cause.
/// tag   owning
struct SealedContent
{
    std::vector<std::uint8_t>  Content       = {};                    // [-]  - the encoded document, sealed transactions only
    std::string                TargetPath    = {};                    // [-]  - UTF-8; what the artist wants to end up holding it
    std::uint64_t              SavedThrough  = 0u;                    // [-]  - the revision ordinal this content carries
    std::uint64_t              SealedAt      = 0u;                    // [ns] - the tick's reading where the state was captured
    std::uint32_t              StreamVersion = CurrentStreamVersion;  // [-]  - the layout it was written at
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The write-verify-replace sequence, run over one sealed state through one file surface.
/// note  🔴 `48` §3's first rule: the existing file is not touched until a complete replacement has been written
///        and read back. Writing in place is the mechanism by which a full disk, a lost share or a power loss
///        converts a good file into no file at all — and the artist's previous save is their fallback.
/// note  ⚠️ `FileInterchange::WriteStream` is one routine because the three steps must not be separable at that
///        seam. This sequence is what states them apart for `86`, and it does that by recording which step it
///        reached rather than by performing them through three separate calls.
/// tag   owning
class PersistenceSequence
{
public:

    /// 🧩 Runs `48` §3's four steps over one sealed state.
    /// in    Sealed  [-]  the captured content and where it is to land
    /// out   Result [-]  refuses with ContentUnsupported for an empty target path or empty content, and carries
    ///                    the file surface's own refusal otherwise
    /// post  🔴 on any refusal the existing file is exactly as it was — `48` §3
    /// note  🔴 This is what `34` resolves at Interactive. Nothing in it reads the document, so it satisfies
    ///        `34` §2's immutability rule by construction rather than by discipline.
    /// note  📝 The journal retirement at ④ is the caller's, on the tick, because the journal belongs to the
    ///        session and this routine runs off it. `Reached` is what tells the caller ③ actually landed.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<PersistenceConclusion> Persist(const SealedContent& Sealed);

    /// 🧩 Whether two streams are identical, byte for byte — `48` §3 ② and `48` §8's exact tier.
    /// in    Written  [-]  what the save handed to the file surface
    /// in    ReadBack   [-]  what was read back from it
    /// out   Identical [-] false at the first difference, and for two different extents
    /// note  🔴 Exact, never tolerant. A verification that accepted a near match would accept exactly the
    ///        corruptions a failing disk produces, which are the ones this step exists to catch.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    static bool VerifyIdentical(const std::vector<std::uint8_t>& Written, const std::vector<std::uint8_t>& ReadBack);

    /// 🧩 How far the last save got, whether it delivered or rejected.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    PersistenceStep LastReached() const;

    /// 🧩 How many saves this sequence has completed through ③.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t ReplacedCount() const;

private:

    PersistenceStep  Reached       = PersistenceStep::Unbegun;   // [-] - the last save's furthest step
    std::uint32_t    ReplacedTotal = 0u;                         // [-] - saves that reached ③
};

SLATE_DECLARES_PRECISION(PrecisionGuarantee::Exact,
                         PrecisionGuarantee::Exact);

}   // namespace Slate
