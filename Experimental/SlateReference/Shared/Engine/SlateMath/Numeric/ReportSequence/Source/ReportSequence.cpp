//============================================================================================================================================
//                                                            REPORTSEQUENCE.CPP
//============================================================================================================================================
// 🧩 Coalescing, the bounded cyclic retention, and the overwriting measure index beside it.

#include "SlateMath/Numeric/ReportSequence/Api/ReportSequence.h"

#include <cstring>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     COALESCING
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 Static text compared by content rather than by pointer. Two origins spelled identically in two translation
//    units are two pointers and one fact, and comparing pointers would present the same report twice.
bool TextAgrees(const char* Left, const char* Right)
{
    if (Left == Right)
        return true;

    if (Left == nullptr || Right == nullptr)
        return false;

    return std::strcmp(Left, Right) == 0;
}

// 🔴 `86` §6: coalescing is by origin, verdict and subject together, never by origin alone. Coalescing by
//    origin would present twelve distinct rejected constructs from one intake as one entry with a count of
//    twelve, and `52` §2 promises the artist the construct and its position — which the count destroys.
bool Coalesces(const ReportSpecification& Held, const ReportSpecification& Incoming)
{
    if (Held.Verdict != Incoming.Verdict)
        return false;

    if (Held.SubjectIndex != Incoming.SubjectIndex)
        return false;

    return TextAgrees(Held.Origin, Incoming.Origin) && TextAgrees(Held.Subject, Incoming.Subject);
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      APPENDING
//------------------------------------------------------------------------------------------------------------------------

void ReportSequence::Append(const ReportSpecification& Incoming)
{
    std::lock_guard<std::mutex> Holding(ReportGuard);

    if (RetainedOrder.empty())
        RetainedOrder.resize(RetainedLimit);

    ++AppendedReports;

    // 📝 Searched newest first. A recurrence is overwhelmingly the report that just happened, so the scan
    //    terminates on its first comparison for the case that recurs.
    for (std::uint32_t Passed = OccupiedCount; Passed-- > 0u;)
    {
        const std::uint32_t Index = (OldestIndex + Passed) % RetainedLimit;

        if (!Coalesces(RetainedOrder[Index], Incoming))
            continue;

        ++RetainedOrder[Index].OccurrenceCount;
        RetainedOrder[Index].Arrival = Incoming.Arrival;
        RetainedOrder[Index].Detail  = Incoming.Detail;

        return;
    }

    const std::uint32_t WriteIndex = (OldestIndex + OccupiedCount) % RetainedLimit;

    RetainedOrder[WriteIndex]                 = Incoming;
    RetainedOrder[WriteIndex].OccurrenceCount = 1u;

    if (OccupiedCount == RetainedLimit)
    {
        // 📝 The write above overwrote the oldest retained report. Advancing the oldest ordinal is what makes
        //    that a discard rather than a corruption of the retention order, exactly as `04`'s arrivals do.
        OldestIndex = (OldestIndex + 1u) % RetainedLimit;
        ++DiscardedReports;
    }
    else
    {
        ++OccupiedCount;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

std::vector<ReportSpecification> ReportSequence::Retained() const
{
    std::lock_guard<std::mutex> Holding(ReportGuard);

    std::vector<ReportSpecification> Current;
    Current.reserve(OccupiedCount);

    for (std::uint32_t Passed = 0u; Passed < OccupiedCount; ++Passed)
        Current.push_back(RetainedOrder[(OldestIndex + Passed) % RetainedLimit]);

    return Current;
}

std::uint32_t ReportSequence::RetainedCount() const
{
    std::lock_guard<std::mutex> Holding(ReportGuard);
    return OccupiedCount;
}

std::uint64_t ReportSequence::AppendedCount() const
{
    std::lock_guard<std::mutex> Holding(ReportGuard);
    return AppendedReports;
}

std::uint64_t ReportSequence::DiscardedCount() const
{
    std::lock_guard<std::mutex> Holding(ReportGuard);
    return DiscardedReports;
}

void ReportSequence::Reclaim()
{
    std::lock_guard<std::mutex> Holding(ReportGuard);

    RetainedOrder.clear();
    OldestIndex    = 0u;
    OccupiedCount    = 0u;
    AppendedReports  = 0u;
    DiscardedReports = 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE MEASURES
//------------------------------------------------------------------------------------------------------------------------

std::size_t MeasureIndex::Located(const char* Origin, const char* Measured) const
{
    for (std::size_t Index = 0u; Index < SampledMeasures.size(); ++Index)
    {
        if (TextAgrees(SampledMeasures[Index].Origin, Origin)
         && TextAgrees(SampledMeasures[Index].Measured, Measured))
        {
            return Index;
        }
    }

    return SampledMeasures.size();
}

void MeasureIndex::DeclareCount(const char*   Origin,
                                const char*   Measured,
                                std::uint64_t Counted,
                                TickPoint     Sampled)
{
    const std::size_t Located_ = Located(Origin, Measured);

    SampledMeasure Declaring;
    Declaring.Origin       = Origin;
    Declaring.Measured     = Measured;
    Declaring.Counted      = Counted;
    Declaring.RealDeclared = false;
    Declaring.Sampled      = Sampled;

    if (Located_ == SampledMeasures.size())
        SampledMeasures.push_back(Declaring);
    else
        SampledMeasures[Located_] = Declaring;
}

void MeasureIndex::DeclareMagnitude(const char* Origin,
                                    const char* Measured,
                                    double      Magnitude,
                                    TickPoint   Sampled)
{
    const std::size_t Located_ = Located(Origin, Measured);

    SampledMeasure Declaring;
    Declaring.Origin       = Origin;
    Declaring.Measured     = Measured;
    Declaring.Magnitude    = Magnitude;
    Declaring.RealDeclared = true;
    Declaring.Sampled      = Sampled;

    if (Located_ == SampledMeasures.size())
        SampledMeasures.push_back(Declaring);
    else
        SampledMeasures[Located_] = Declaring;
}

const std::vector<SampledMeasure>& MeasureIndex::Measures() const
{
    return SampledMeasures;
}

Deliver<SampledMeasure> MeasureIndex::Resolve(const char* Origin, const char* Measured) const
{
    const std::size_t Located_ = Located(Origin, Measured);

    if (Located_ == SampledMeasures.size())
    {
        return Deliver<SampledMeasure>::Refuse(
            { RefusalReason::ExtentExhausted, "nothing has declared that measure this session" });
    }

    return Deliver<SampledMeasure>::Result(SampledMeasures[Located_]);
}

void MeasureIndex::Reclaim()
{
    SampledMeasures.clear();
}

}   // namespace Slate
