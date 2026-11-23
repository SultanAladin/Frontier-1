/*==========================================================================================================================================
                                                   RESERVOIR SAMPLING DEFINITION
==========================================================================================================================================*/

#include "ReservoirSampling.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

// A weight that cannot participate (non-positive, NaN, infinite) still counts as a considered
// candidate — it simply never wins. This keeps every recurrence total and deterministic.
double ParticipatingWeight(double Weight) noexcept
{
    return (std::isfinite(Weight) && Weight > 0.0) ? Weight : 0.0;
}

double ParticipatingFactor(double Factor) noexcept
{
    return (std::isfinite(Factor) && Factor > 0.0) ? Factor : 0.0;
}

} // namespace

bool Reservoir::Update(ReservoirSample Sample, double Weight, double Uniform01) noexcept
{
    const double W = ParticipatingWeight(Weight);
    WeightSum_ += W;
    SampleCount_ += 1;
    if (W > 0.0 && Uniform01 * WeightSum_ < W)
    {
        Sample_ = Sample;
        return true;
    }
    return false;
}

bool Reservoir::Combine(const Reservoir& Other, double TargetHere, double Jacobian, double Uniform01) noexcept
{
    const double W = ParticipatingFactor(TargetHere) * ParticipatingFactor(Jacobian) *
                     ParticipatingFactor(Other.Weight_) * static_cast<double>(Other.SampleCount_);
    WeightSum_ += W;
    SampleCount_ += Other.SampleCount_;
    if (W > 0.0 && Uniform01 * WeightSum_ < W)
    {
        Sample_ = Other.Sample_;
        return true;
    }
    return false;
}

void Reservoir::ClampSampleCount(uint32_t MaxCount) noexcept
{
    SampleCount_ = std::min(SampleCount_, MaxCount);
}

void Reservoir::Finalize(double TargetAtSample) noexcept
{
    FinalizeWithNormalization(TargetAtSample, SampleCount_);
}

void Reservoir::FinalizeWithNormalization(double TargetAtSample, uint64_t SupportingCandidateCount) noexcept
{
    const double Target = ParticipatingFactor(TargetAtSample);
    if (SupportingCandidateCount == 0 || Target <= 0.0)
    {
        Weight_ = 0.0;
        return;
    }
    Weight_ = WeightSum_ / (Target * static_cast<double>(SupportingCandidateCount));
}

} // namespace Frontier
