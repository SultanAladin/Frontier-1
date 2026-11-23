//============================================================================================================================================
//                                                             REFERENCEINDEX.H
//============================================================================================================================================
// 🧩 `48` §5 — what one document depends on outside itself, each declared embedded or referenced, absence registered.

#pragma once

#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"
#include "SlateMath/Numeric/ReportSequence/Api/ReportSequence.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 WHAT A DOCUMENT DEPENDS ON
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The five contents `48` §5 tabulates, each carrying its own retention default.
/// note  🔴 The subject decides the default and the document decides the retention. Nothing infers one from the
///        other at read time, because a document written under one answer must open correctly under both — and a
///        reader that inferred it would open the same file two ways on two builds.
/// tag   guarantee
enum class ReferenceSubject : std::uint32_t
{
    TexturedContent  = 0u,   // [-] - authored here, per `56`; nothing else holds it
    ImportedImagery = 1u,   // [-] - often large, often shared across documents — `50`
    ImportedTopology = 2u,  // [-] - Slate does not own it; `38`'s non-mutation rule
    VectorContent   = 3u,   // [-] - small, and pasted source has no file to refer to — `52`
    TypefaceOutline = 4u,   // [-] - `00` §12 carries the licensing question; see DeclareTypefaceRetention
    SubjectCount    = 5u    // [-] - the closed count, never a subject
};

/// 🧩 Whether the content travels inside the document or is read from where it lives.
/// tag   guarantee
enum class ReferenceRetention : std::uint32_t
{
    Embedded   = 0u,   // [-] - written into the document; the document alone is enough to open it
    Referenced = 1u    // [-] - named by path; the document is not enough on its own
};

/// 🧩 Whether a declared reference was found where the document said it would be.
/// note  🔴 `48` §5: Absent is a standing a reference **holds**, not a refusal that discards it. The owner
///        stays registered, reports what it was looking for, and presents as missing. A document that substituted
///        a default for a missing texture is one the artist saves over with the defaults baked in, and the
///        original path is gone from the file at that point.
/// tag   guarantee
enum class ReferenceCondition : std::uint32_t
{
    Unresolved    = 0u,   // [-] - declared, not yet looked for
    Resolved      = 1u,   // [-] - found where the document named it
    Absent        = 2u,   // [-] - looked for and not there; registered, never substituted
    CurrentCount = 3u    // [-] - the closed count, never a standing
};

/// 🧩 The retention `48` §5's table declares for one subject when the document declares none.
/// in    Subject    [-]  which of the five contents
/// out   Retention  [-]  Embedded for authored and pasted content, Referenced for imported and typeface content
/// cost  ✔️
/// tag   api, constexpr, nonallocating, nonthrowing
constexpr ReferenceRetention ResolveRetention(ReferenceSubject Subject)
{
    return Subject == ReferenceSubject::TexturedContent || Subject == ReferenceSubject::VectorContent
         ? ReferenceRetention::Embedded
         : ReferenceRetention::Referenced;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ONE DECLARED REFERENCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One thing the document depends on, the owner that depends on it, and whether it is there.
/// note  🔴 The owner is held so that an absence is presentable **in the outliner**, which is where the artist
///        is looking. An absence recorded without its owner is a line in a register nobody reads until the
///        render is already wrong.
/// tag   owning
struct DeclaredReference
{
    OwnerIdentity    Registered      = {};                                // [-] - the owner that depends on it
    std::string         OriginPath    = {};                                // [-] - UTF-8; empty for embedded content
    ReferenceSubject    Subject       = ReferenceSubject::TexturedContent;  // [-] - which of `48` §5's five rows
    ReferenceRetention  Retention     = ReferenceRetention::Embedded;      // [-] - declared per document, per `48` §5
    ReferenceCondition   Condition      = ReferenceCondition::Unresolved;     // [-] - whether it was found
    std::uint64_t       SpannedBytes  = 0u;                                // [B] - as last resolved; zero when absent
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE REFERENCES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every external dependency of one document, by identity, with the typeface answer the document was made under.
/// note  🔴 `48` §5: a missing reference is an registered absence and never a substitution. Nothing here produces
///        stand-in content, and there is deliberately no routine that would.
/// note  ⚠️ Held per document rather than per application. Two documents referring to the same imagery declare it
///        twice, because each has to open on its own — a shared declaration would make the second document's
///        reference depend on whether the first was open.
/// tag   owning
class ReferenceIndex
{
public:

    /// 🧩 Declares one external dependency and issues the ordinal that addresses it.
    /// in    Incoming  [-]  the reference; Retention is honoured as given
    /// out   Result   [-]  refuses with ContentUnsupported for a Referenced entry naming no path
    /// note  📝 A Referenced entry with no path cannot be looked for, so it could only ever stand Unresolved.
    ///        Accepting it would put a permanent unknown into the document with nothing able to settle it.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Declare(const DeclaredReference& Incoming);

    /// 🧩 Declares one reference embedded or referenced, per the document's own answer.
    /// out   Result  [-]  refuses with ExtentExhausted outside the declared count
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareRetention(std::uint32_t ReferenceIndex, ReferenceRetention Declaring);

    /// 🧩 Declares one reference found, with the extent it spans.
    /// out   Result  [-]  refuses with ExtentExhausted outside the declared count
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareResolved(std::uint32_t ReferenceIndex, std::uint64_t SpannedBytes);

    /// 🧩 Declares one reference missing — registered, reported, and never replaced.
    /// out   Result  [-]  refuses with ExtentExhausted outside the declared count
    /// post  the owner stays registered and the origin path stays exactly as the document wrote it
    /// note  🔴 The path is retained rather than cleared. It is the only thing that tells the artist which file
    ///        to go and find, and clearing it turns a recoverable absence into a permanent one.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareAbsent(std::uint32_t ReferenceIndex);

    /// 🧩 Appends every unreported absence to the register — `48` §5 and `86` §4.
    /// in    Reporting  [-]  where the absence rows land
    /// in    Sampled    [ns] the tick's own reading
    /// post  each absence is appended once; a second call appends nothing further
    /// note  📝 Reported on the tick rather than where the absence was discovered, for `IntakeIndex::Report`'s
    ///        reason: resolution runs through `34`, and a worker reporting a missing texture would report it
    ///        before the requester had applied the open it belongs to.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Report(ReportSequence& Reporting, TickPoint Sampled);

    /// 🧩 Every declared reference, in declaration order.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<DeclaredReference>& Declared() const;

    /// 🧩 The most recently declared reference naming one origin path.
    /// out   Result  [-]  refuses with ExtentExhausted when nothing declares that path
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<DeclaredReference> Resolve(const std::string& OriginPath) const;

    /// 🧩 Declares whether this document embeds its typeface outlines or refers to them.
    /// note  ⚠️ `00` §12 leaves the licensing question open and `48` §5 does not close it. What is fixed here is
    ///        that the answer is **recorded in the document**, so a file made under one answer opens correctly
    ///        under the other. A build-wide constant would make the same file mean two things on two builds.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeclareTypefaceRetention(ReferenceRetention Declaring);

    /// 🧩 The typeface answer this document was made under.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    ReferenceRetention TypefaceRetention() const;

    /// 🧩 How many references stand absent — what `86` presents beside the outliner.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t AbsentCount() const;

    /// 🧩 How many references are declared in total.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t DeclaredCount() const;

    /// 🧩 Empties the index. Called when the session it belongs to closes and by nothing else.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Reclaim();

private:

    std::vector<DeclaredReference>  Declarations;                                       // [-] - in declaration order
    std::vector<bool>               AbsenceReported;                                    // [-] - parallel; Report appends each once
    ReferenceRetention              TypefaceDeclared = ReferenceRetention::Referenced;  // [-] - this document's `00` §12 answer
    std::uint32_t                   AbsentTotal      = 0u;                              // [-] - references standing Absent
};

}   // namespace Slate
