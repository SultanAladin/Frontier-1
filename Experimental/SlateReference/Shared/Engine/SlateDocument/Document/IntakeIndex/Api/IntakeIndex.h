//============================================================================================================================================
//                                                              INTAKEINDEX.H
//============================================================================================================================================
// 🧩 What arrived, from where, and what was assumed about it — never an assumption made silently.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateMath/Numeric/ReportSequence/Api/ReportSequence.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   WHAT WAS ASSUMED
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The two things intake assumes when the source is silent, and nothing else.
/// note  🔴 `50` §3: these are exactly the two rows that produce a result which looks plausible and is wrong. A
///        model at a hundredth of its intended size still renders, and an image decoded as though it were linear
///        still looks like an image. Every other silent row is rejected rather than assumed.
/// tag   guarantee
enum class AssumedSubject : std::uint32_t
{
    UnitScale       = 0u,   // [-] - the file declared no unit convention
    ContentSpace    = 1u,   // [-] - the imagery declared no colour space — `36` §3
    AssumedCount    = 2u    // [-] - the closed count, never an assumption
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    ONE INTAKE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One thing that arrived, and whatever was assumed about it.
/// tag   owning
struct IntakeRecord
{
    std::string     OriginPath        = {};                        // [-] - where it was read from
    std::string     Subject           = {};                        // [-] - what arrived
    AssumedSubject  Assumed           = AssumedSubject::AssumedCount;
    double          AssumedMagnitude  = 0.0;                       // [-] - the unit scale that was chosen
    std::uint32_t   AssumedIndex    = 0u;                        // [-] - the colour space that was chosen
    bool            AssumptionMade    = false;                     // [-] - the two fields above are meaningful
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE INTAKES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every intake of the session, and the assumptions among them.
/// note  🔴 `50` §3 and §8: an assumption is **recorded here and reported through `86`**, never made silently.
///        The record is what lets an artist ask, six months later, why one model in a scene is a hundred times
///        the size of the others — and get an answer instead of a shrug.
/// note  ⚠️ Recorded per intake rather than per document, so re-importing the same file twice under two
///        different assumptions produces two records. Coalescing them would hide the fact that they differ.
/// tag   owning
class IntakeIndex
{
public:

    /// 🧩 Records one intake.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Record(const IntakeRecord& Incoming);

    /// 🧩 Appends every unreported assumption to the register.
    /// in    Reporting  [-]  where `86` §4's `50` §3 row lands
    /// in    Sampled    [ns] the tick's own reading
    /// post  each assumption is appended once; a second call appends nothing further
    /// note  📝 Reported on the tick rather than at intake, because intake runs through `34` at Interactive and
    ///        a worker appending here would be correct but would report before the requester had applied the
    ///        result — so the artist would read about a model that is not yet in their document.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Report(ReportSequence& Reporting, TickPoint Sampled);

    /// 🧩 Every recorded intake, in arrival order.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<IntakeRecord>& Records() const;

    /// 🧩 The most recent intake of one origin.
    /// out   Result  [-]  refuses with ExtentExhausted when nothing arrived from there
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<IntakeRecord> Resolve(const std::string& OriginPath) const;

    /// 🧩 How many intakes carried an assumption.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t AssumptionCount() const;

    std::uint32_t RecordedCount() const;

private:

    std::vector<IntakeRecord>  Recorded;             // [-] - in arrival order
    std::vector<bool>          AssumptionReported;   // [-] - parallel; Report appends each once
    std::uint32_t              AssumedTotal = 0u;    // [-] - intakes carrying an assumption
};

}   // namespace Slate
