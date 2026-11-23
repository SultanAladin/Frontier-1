//============================================================================================================================================
//                                                             INTAKEINDEX.CPP
//============================================================================================================================================
// 🧩 Arrival-ordered records, and the once-only report of every assumption among them.

#include "SlateDocument/Document/IntakeIndex/Api/IntakeIndex.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      RECORDING
//------------------------------------------------------------------------------------------------------------------------

void IntakeIndex::Record(const IntakeRecord& Incoming)
{
    Recorded.push_back(Incoming);
    AssumptionReported.push_back(false);

    if (Incoming.AssumptionMade)
        ++AssumedTotal;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE REPORT
//------------------------------------------------------------------------------------------------------------------------

void IntakeIndex::Report(ReportSequence& Reporting, TickPoint Sampled)
{
    for (std::size_t Index = 0u; Index < Recorded.size(); ++Index)
    {
        if (!Recorded[Index].AssumptionMade || AssumptionReported[Index])
            continue;

        ReportSpecification Assumed;
        Assumed.Origin         = "50 §3 AssetInterchange";
        Assumed.Verdict    = ReportVerdict::Assumed;
        Assumed.SubjectIndex = static_cast<std::uint64_t>(Index);
        Assumed.Arrival        = Sampled;

        // 📝 Static text only — `86` §3.1 accepts an append from any thread and a report that owned an allocation
        //    would allocate while the tick presents the register. The origin path lives in the record beside the
        //    report and the presenter reads it from there.
        if (Recorded[Index].Assumed == AssumedSubject::UnitScale)
        {
            Assumed.Subject = "UnitScale";
            Assumed.Detail  = "the source declared no unit convention; one was chosen and applied at intake";
        }
        else
        {
            Assumed.Subject = "ContentSpace";
            Assumed.Detail  = "the imagery declared no colour space; one was chosen — `36` §3";
        }

        Reporting.Append(Assumed);

        AssumptionReported[Index] = true;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

const std::vector<IntakeRecord>& IntakeIndex::Records() const { return Recorded; }

Deliver<IntakeRecord> IntakeIndex::Resolve(const std::string& OriginPath) const
{
    for (std::size_t Index = Recorded.size(); Index-- > 0u;)
    {
        if (Recorded[Index].OriginPath == OriginPath)
            return Deliver<IntakeRecord>::Result(Recorded[Index]);
    }

    return Deliver<IntakeRecord>::Refuse({ RefusalReason::ExtentExhausted, "nothing arrived from that origin" });
}

std::uint32_t IntakeIndex::AssumptionCount() const { return AssumedTotal; }
std::uint32_t IntakeIndex::RecordedCount() const   { return static_cast<std::uint32_t>(Recorded.size()); }

}   // namespace Slate
