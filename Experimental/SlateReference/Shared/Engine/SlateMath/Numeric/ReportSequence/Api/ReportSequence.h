//============================================================================================================================================
//                                                             REPORTSEQUENCE.H
//============================================================================================================================================
// 🧩 The session's reports and its sampled measures — one appended once, one overwritten, and never confused.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateMath/Platform/TickSequence/Api/TickSequence.h"

#include <cstdint>
#include <mutex>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE SEVEN CLASSES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What the engine did, on the artist's behalf, that the artist could not otherwise see.
/// note  ⚠️ `86` §4.1 calls these report classes. `Class` used as a noun is banned alongside `Role`, and for the
///        same reason — it names the category rather than the mechanism. Each member below is instead the past
///        participle of what happened, which is the discriminating fact.
/// note  🔴 The verdict is declared by the reporting mechanism and never inferred from the text. An inferred
///        verdict is a presentation that disagrees with the document that made the promise — `86` §4.1.
/// note  Five of the seven describe normal operation. `86` §5 is the authority on which of them is a problem,
///        and a presenter that treats all seven as failures teaches the artist to ignore it.
/// tag   guarantee
enum class ReportVerdict : std::uint32_t
{
    Measured         = 0u,   // [-] - a sampled quantity with a current value; belongs in MeasureIndex
    Assumed          = 1u,   // [-] - the source declared nothing and something was chosen
    Amended          = 2u,   // [-] - content was changed on the way in, out, or between partitions
    Truncated        = 3u,   // [-] - content was dropped at a declared capacity
    Rejected          = 4u,   // [-] - rejected outright; nothing partial was produced
    Terminated       = 5u,   // [-] - a Convergent solve ended at its ceiling instead of its criterion
    Failed           = 6u,   // [-] - the mechanism did not complete and there is no result
    VerdictCount = 7u    // [-] - the closed count, never a verdict
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      ONE REPORT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One appended report — where it came from, what it applies to, and how many times it has happened.
/// note  🔴 Every text field points at string literal storage only. `86` §3.1 accepts an append from any thread,
///        and a report that owned an allocation would allocate on a worker while the tick presents the register.
/// note  ⚠️ The verdict defaults to Failed rather than to Measured. A report that forgot to declare its
///        verdict then presents as the most serious thing it could be, which is the direction that gets fixed.
/// tag   nonallocating, nonthrowing
struct ReportSpecification
{
    const char*        Origin          = "";                          // [-] - static text naming document and section
    const char*        Subject         = "";                          // [-] - static text naming what it applies to
    const char*        Detail          = "";                          // [-] - static text; the reason, verbatim
    std::uint64_t      SubjectIndex  = 0u;                          // [-] - a position, a slot, a count; zero for none
    ReportVerdict  Verdict     = ReportVerdict::Failed;   // [-] - declared, never inferred
    TickPoint          Arrival         = {};                          // [ns] - stamped where the occurrence happened
    std::uint32_t      OccurrenceCount = 1u;                          // [-] - raised by coalescing, never by the caller
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE REGISTER
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The session's appended reports, bounded, coalesced, and readable from the tick.
/// note  🔴 This lives in `SlateMath` and not in `SlateUI`. `86` §3 gives the reason and it is the link partition:
///        every origin that must write here sits beneath `SlateUI`, so a register held in the interface could not
///        be written by a single one of the mechanisms obliged to write it.
/// note  🔴 `86` §3.1: an append is accepted from any thread, and this is the one structure in the engine that
///        accepts one. A report about a failure has to survive the failure, and `34` §5's failed work produces no
///        result to carry it back on.
/// note  📝 `86` §1 also names a `ReportClassifier`. §4.1 forbids inferring a verdict from the text, which
///        leaves that component nothing to do; the coalescing rule it would have carried is `Coalesces` below.
/// tag   owning
class ReportSequence
{
public:

    // 🚧 `86` §11 leaves the session bound open — by count or by extent held. It is a count here, and the
    //    discard is itself presented, because a register that silently forgot the first report of a run is
    //    worse than one that accepts it is full.
    static constexpr std::uint32_t RetainedLimit = 4096u;   // [-] - reports retained before the oldest leaves

    ReportSequence()                                 = default;
    ReportSequence(const ReportSequence&)            = delete;
    ReportSequence& operator=(const ReportSequence&) = delete;

    /// 🧩 Appends one report, coalescing it into a recurrence of the same origin, verdict and subject.
    /// in    Incoming  [-]  the report as its origin declared it
    /// post  the retained count never exceeds RetainedLimit; the oldest report leaves when it would
    /// note  🔴 Appended exactly once per occurrence, at the moment of the occurrence — `86` §2.2. A report
    ///        reconstructed later from a measure that changed is a report about the wrong instant.
    /// note  📝 Coalescing compares the two integer discriminants before either text, so a full register costs
    ///        integer comparisons per append rather than four thousand string comparisons.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Append(const ReportSpecification& Incoming);

    /// 🧩 The retained reports, oldest first, as a copy taken under the register's own guard.
    /// out   Retained  [-]  at most RetainedLimit entries, each carrying its occurrence count
    /// note  Returned by value deliberately. Appends arrive from any thread, so handing back a reference would
    ///        hand back storage a worker may be writing while the presenter walks it.
    /// cost  🚩
    /// tag   api, nonthrowing
    std::vector<ReportSpecification> Retained() const;

    /// 🧩 How many reports are retained now.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t RetainedCount() const;

    /// 🧩 How many occurrences have been appended across the whole session, coalesced ones included.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t AppendedCount() const;

    /// 🧩 How many retained reports the ceiling has discarded — itself a fact worth presenting.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t DiscardedCount() const;

    /// 🧩 Empties the register. Called at process teardown and by nothing else.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Reclaim();

private:

    mutable std::mutex                ReportGuard;              // [-] - held for every append and every read
    std::vector<ReportSpecification>  RetainedOrder;            // [-] - cyclic, sized once to the ceiling
    std::uint32_t                     OldestIndex   = 0u;     // [-] - where the oldest retained report sits
    std::uint32_t                     OccupiedCount   = 0u;     // [-] - how many are retained
    std::uint64_t                     AppendedReports = 0u;     // [-] - occurrences across the session
    std::uint64_t                     DiscardedReports = 0u;    // [-] - retained reports the ceiling dropped
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    ONE MEASURE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One sampled quantity with a current value — overwritten every time it is sampled.
/// note  🔴 A measure is not a report and the distinction is what the whole register rests on. `06` §3 samples
///        its totals every rotation; appended, that is thousands of entries a minute inside which the one
///        refusal that mattered is unfindable — `86` §2.
/// tag   nonallocating, nonthrowing
struct SampledMeasure
{
    const char*    Origin       = "";     // [-]  - static text naming document and section
    const char*    Measured     = "";     // [-]  - static text naming the quantity
    std::uint64_t  Counted      = 0u;     // [-]  - the integer reading; meaningful while RealDeclared is false
    double         Magnitude    = 0.0;    // [-]  - the real reading; meaningful while RealDeclared holds
    bool           RealDeclared = false;  // [-]  - which of the two above the producer declared
    TickPoint      Sampled      = {};     // [ns] - when the tick last sampled it
};

/// 🧩 The current reading of every sampled measure, keyed by origin and quantity.
/// note  🔴 Written on the tick only and therefore unguarded. `86` §2.1: measures are sampled by the tick and
///        never pushed — a producer that pushed its own measure would write from inside a recording, contending
///        with the tick for the state the tick is presenting.
/// tag   owning
class MeasureIndex
{
public:

    /// 🧩 Declares one integer measure, replacing whatever the same origin and quantity last read.
    /// in    Origin    [-]   static text naming document and section
    /// in    Measured  [-]   static text naming the quantity
    /// in    Counted   [-]   the reading
    /// in    Sampled   [ns]  when the tick took it
    /// cost  🚩
    /// tag   api, nonthrowing
    void DeclareCount(const char* Origin, const char* Measured, std::uint64_t Counted, TickPoint Sampled);

    /// 🧩 Declares one real measure, replacing whatever the same origin and quantity last read.
    /// note  Named apart from the integer form rather than overloaded, so that an integer literal cannot select
    ///        the real declaration and quietly change a count into a magnitude.
    /// cost  🚩
    /// tag   api, nonthrowing
    void DeclareMagnitude(const char* Origin, const char* Measured, double Magnitude, TickPoint Sampled);

    /// 🧩 Every measure currently held, in declaration order.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<SampledMeasure>& Measures() const;

    /// 🧩 One measure's current reading.
    /// in    Origin    [-]  static text naming document and section
    /// in    Measured  [-]  static text naming the quantity
    /// out   Result   [-]  refuses with ExtentExhausted when nothing has declared it
    /// note  An undeclared measure refuses rather than reading zero. `08` §5 rules the same for an unmeasurable
    ///        capability: a metric that reports zero when it could not be measured is confidently wrong.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<SampledMeasure> Resolve(const char* Origin, const char* Measured) const;

    /// 🧩 Discards every held measure.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Reclaim();

private:

    std::size_t Located(const char* Origin, const char* Measured) const;

    std::vector<SampledMeasure>  SampledMeasures;   // [-] - one entry per origin and quantity
};

}   // namespace Slate
