/*==========================================================================================================================================
                                                       VARIANCE FILTERING
                                     ------------------------------------------------------------------
                 SVGF-style denoise core of the illumination chain (plan §8.2 final steps:
                      VarianceTemporal → VarianceWavelet → VarianceUpsample).
==========================================================================================================================================*/

// 🧩 Variance filtering: temporal moment integration + edge-avoiding à-trous wavelet + joint upsample.
//
// The reservoirs deliver an unbiased but noisy 1-spp estimate; this core removes the noise the way the
// demo's Variance* units do (Schied et al., "Spatiotemporal Variance-Guided Filtering", 2017):
//   1. VarianceTemporal — exponential accumulation of first/second moments per pixel; the variance
//      estimate E[x²]−E[x]² both measures local noise and steers the spatial filter's luminance sigma;
//   2. VarianceWavelet  — à-trous passes (B3-spline kernel, steps 1,2,4,…) with edge-stopping weights
//      from depth (gradient-aware), normals and variance-scaled luminance; variance propagates through
//      the same weights so later, wider passes know how much noise remains;
//   3. VarianceUpsample — joint bilateral 2× upsample against full-resolution guides, for chains that
//      filter at half resolution.
//
// This portable core works on one channel (luminance) in double precision: it is the exact recurrence
// the SDK-phase Slang bodies apply per colour channel with shared weights. Buffers are row-major,
// tightly packed, caller-owned. Null/empty inputs are refused (false), never silently zero-filled.

#pragma once

#include "Engine/Foundation/Math.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

struct VarianceFilterParameters
{
    double SigmaDepth     = 1.0;    // [-] - Depth edge softness (gradient-scaled)
    double SigmaNormal    = 128.0;  // [-] - Normal edge power
    double SigmaLuminance = 4.0;    // [-] - Luminance edge softness, in local standard deviations
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    TEMPORAL MOMENTS
//------------------------------------------------------------------------------------------------------------------------

class VarianceHistory
{
  public:
    void Resize(uint32_t Width, uint32_t Height);

    // Integrates one frame: Mean/2nd moment move toward Current by Alpha (first frame and pixels with
    // a set ResetMask byte restart from Current). Variance is refreshed for query. Sizes must match
    // Resize; a mismatched call is refused (returns false, history untouched).
    bool Integrate(const double* Current, uint32_t Width, uint32_t Height, double Alpha,
                   const uint8_t* ResetMask = nullptr);

    [[nodiscard]] const std::vector<double>& Mean() const noexcept { return Mean_; }
    [[nodiscard]] const std::vector<double>& Variance() const noexcept { return Variance_; }
    [[nodiscard]] uint32_t                   FramesIntegrated() const noexcept { return Frames_; }
    [[nodiscard]] uint32_t                   Width() const noexcept { return Width_; }
    [[nodiscard]] uint32_t                   Height() const noexcept { return Height_; }

  private:
    std::vector<double> Mean_;
    std::vector<double> SecondMoment_;
    std::vector<double> Variance_;
    uint32_t            Width_  = 0;
    uint32_t            Height_ = 0;
    uint32_t            Frames_ = 0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     WAVELET + UPSAMPLE
//------------------------------------------------------------------------------------------------------------------------

// One à-trous step at the given dilation Step (1, 2, 4, …). VarianceOut may be null when the caller
// does not propagate variance (last pass). Input and Output must not alias.
[[nodiscard]] bool VarianceWaveletStep(const double* Input, const double* VarianceIn, const double* Depth,
                                       const Vec3* Normals, uint32_t Width, uint32_t Height, uint32_t Step,
                                       const VarianceFilterParameters& Parameters, double* Output,
                                       double* VarianceOut);

// Joint bilateral upsample from LowW×LowH to exactly (2·LowW)×(2·LowH), guided by full-res
// depth/normals tested against the low-res guides. Falls back to the nearest low texel where no
// neighbour passes the edge tests (defined output everywhere).
[[nodiscard]] bool JointBilateralUpsample2X(const double* Low, const double* LowDepth, const Vec3* LowNormals,
                                            uint32_t LowWidth, uint32_t LowHeight, const double* HighDepth,
                                            const Vec3* HighNormals, const VarianceFilterParameters& Parameters,
                                            double* HighOut);

} // namespace Frontier
