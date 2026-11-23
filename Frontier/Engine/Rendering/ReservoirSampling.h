/*==========================================================================================================================================
                                                       RESERVOIR SAMPLING
                                     ------------------------------------------------------------------
                 Weighted reservoir sampling + resampled importance sampling + reuse combines —
                      the estimator core of the ReSTIR chain (plan §8.2 steps 2–4).
==========================================================================================================================================*/

// 🧩 Reservoir sampling: the portable (CPU) form of the demo's Reservoir* compute units.
//
// One reservoir holds one winning sample y out of M streamed candidates, chosen with probability
// proportional to the candidates' RIS weights w_i = pHat(x_i)/p_i(x_i). After Finalize, the shading
// estimate is F(y)·W with W = wSum / (M · pHat(y)) — unbiased for ∫F whenever the candidates cover
// F's support (bitterli et al., "Spatiotemporal reservoir resampling", 2020 — the algorithm the demo's
// ReservoirTemporal/ReservoirSpatial implement on GPU).
//
// Reuse mirrors the chain exactly:
//   * temporal — the previous frame's (finalised) reservoir is streamed in as one meta-candidate
//     whose weight restates its sample under *this* pixel's target; its M is clamped first
//     (M-clamping bounds history influence so disocclusions recover in bounded frames);
//   * spatial — a neighbour's reservoir is streamed the same way, scaled by the Jacobian of the
//     change of variables between the two receivers (geometry-term ratio for light-point samples).
//
// Numbers are double precision here so validation can prove unbiasedness against analytic ground
// truth; the SDK-phase Slang bodies carry the same recurrences in fp32 exactly as the demo does.
// All operations are total: non-positive or non-finite weights count as zero-weight candidates
// (M still advances — the candidate was considered), and an empty/zero-target reservoir finalises
// to W = 0 rather than NaN.

#pragma once

#include <cstdint>

namespace Frontier
{

// One candidate: which element of the sampled domain, plus a parametric point on it.
// For direct lighting: Candidate = light index, (U, V) = point on the light's surface.
struct ReservoirSample
{
    uint32_t Candidate = 0;   // [idx]
    double   U         = 0.0; // [-]
    double   V         = 0.0; // [-]
};

class Reservoir
{
  public:
    // Streams one candidate with RIS weight Weight = pHat(x)/p(x). Uniform01 in [0,1) drives the
    // weighted choice (accepted with probability Weight / WeightSum). Returns true when x wins.
    // SampleCount (M) always advances by one. Call Finalize() before reading Weight().
    bool Update(ReservoirSample Sample, double Weight, double Uniform01) noexcept;

    // Streams another *finalised* reservoir's sample as a meta-candidate:
    //   w = TargetHere(Other.Sample) · Jacobian · Other.Weight · Other.SampleCount
    // This is a resampled meta-candidate, not the exact sum of all original candidates at the new
    // receiver. Correct spatial reuse also requires compatible support, visibility and normalization;
    // calling Combine alone does not establish unbiasedness. SampleCount grows by Other.SampleCount.
    bool Combine(const Reservoir& Other, double TargetHere, double Jacobian, double Uniform01) noexcept;

    // Bounds history influence before a temporal Combine: M = min(M, MaxCount). Weight is untouched
    // (the clamp trades a documented, bounded bias for bounded ghosting — as in the source algorithm).
    void ClampSampleCount(uint32_t MaxCount) noexcept;

    // W = WeightSum / (M · pHat(y)). Zero M or non-positive target finalises to W = 0.
    void Finalize(double TargetAtSample) noexcept;
    // Spatial MIS may instead use the selected sample's count of supporting source-domain
    // candidates. The caller must establish support and avoid double-counting source histories.
    void FinalizeWithNormalization(double TargetAtSample, uint64_t SupportingCandidateCount) noexcept;

    // The shading estimate F(y)·W. Meaningful only after Finalize().
    [[nodiscard]] double Estimate(double ContributionAtSample) const noexcept
    {
        return ContributionAtSample * Weight_;
    }

    [[nodiscard]] const ReservoirSample& Sample() const noexcept { return Sample_; }
    [[nodiscard]] double                 WeightSum() const noexcept { return WeightSum_; }
    [[nodiscard]] uint32_t               SampleCount() const noexcept { return SampleCount_; }
    [[nodiscard]] double                 Weight() const noexcept { return Weight_; }
    [[nodiscard]] bool                   HasSample() const noexcept { return SampleCount_ > 0 && WeightSum_ > 0.0; }

    void Reset() noexcept
    {
        Sample_      = {};
        WeightSum_   = 0.0;
        SampleCount_ = 0;
        Weight_      = 0.0;
    }

  private:
    ReservoirSample Sample_{};
    double          WeightSum_   = 0.0;
    uint32_t        SampleCount_ = 0;   // [candidates] - "M"
    double          Weight_      = 0.0; // [-] - "W", set by Finalize
};

} // namespace Frontier
