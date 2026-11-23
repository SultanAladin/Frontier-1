/*==========================================================================================================================================
                                                   VARIANCE FILTERING DEFINITION
==========================================================================================================================================*/

#include "VarianceFiltering.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

constexpr double Kernel[5] = {1.0, 4.0, 6.0, 4.0, 1.0};   // B3 spline (SVGF à-trous kernel)

double NormalWeight(Vec3 A, Vec3 B, double SigmaNormal) noexcept
{
    const double Cosine = std::clamp(static_cast<double>(Dot(A, B)), 0.0, 1.0);
    return std::pow(Cosine, SigmaNormal);
}

size_t Index(uint32_t X, uint32_t Y, uint32_t Width) noexcept
{
    return static_cast<size_t>(Y) * Width + X;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    TEMPORAL MOMENTS
//------------------------------------------------------------------------------------------------------------------------

void VarianceHistory::Resize(uint32_t Width, uint32_t Height)
{
    Width_  = Width;
    Height_ = Height;
    Frames_ = 0;
    Mean_.assign(static_cast<size_t>(Width) * Height, 0.0);
    SecondMoment_.assign(static_cast<size_t>(Width) * Height, 0.0);
    Variance_.assign(static_cast<size_t>(Width) * Height, 0.0);
}

bool VarianceHistory::Integrate(const double* Current, uint32_t Width, uint32_t Height, double Alpha,
                                const uint8_t* ResetMask)
{
    if (Current == nullptr || Width != Width_ || Height != Height_ || Width == 0)
    {
        return false;
    }
    const double ClampedAlpha = std::clamp(Alpha, 0.0, 1.0);
    const size_t Count        = static_cast<size_t>(Width) * Height;
    for (size_t Pixel = 0; Pixel < Count; ++Pixel)
    {
        const double Value = Current[Pixel];
        if (Frames_ == 0 || (ResetMask != nullptr && ResetMask[Pixel] != 0))
        {
            Mean_[Pixel]         = Value;
            SecondMoment_[Pixel] = Value * Value;
            Variance_[Pixel]     = 0.0;
            continue;
        }
        Mean_[Pixel] += ClampedAlpha * (Value - Mean_[Pixel]);
        SecondMoment_[Pixel] += ClampedAlpha * (Value * Value - SecondMoment_[Pixel]);
        Variance_[Pixel] = std::max(SecondMoment_[Pixel] - Mean_[Pixel] * Mean_[Pixel], 0.0);
    }
    Frames_ += 1;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WAVELET + UPSAMPLE
//------------------------------------------------------------------------------------------------------------------------

bool VarianceWaveletStep(const double* Input, const double* VarianceIn, const double* Depth,
                         const Vec3* Normals, uint32_t Width, uint32_t Height, uint32_t Step,
                         const VarianceFilterParameters& Parameters, double* Output, double* VarianceOut)
{
    if (Input == nullptr || VarianceIn == nullptr || Depth == nullptr || Normals == nullptr ||
        Output == nullptr || Width == 0 || Height == 0 || Step == 0)
    {
        return false;
    }
    for (uint32_t Y = 0; Y < Height; ++Y)
    {
        for (uint32_t X = 0; X < Width; ++X)
        {
            const size_t P        = Index(X, Y, Width);
            const double Centre   = Input[P];
            const double CentreZ  = Depth[P];
            const Vec3   CentreN  = Normals[P];
            const double SigmaLum = Parameters.SigmaLuminance * std::sqrt(std::max(VarianceIn[P], 0.0)) + 1e-4;

            // Directional depth derivatives at the centre (forward differences, edge-clamped).
            const double GradX = Depth[Index(std::min(X + 1, Width - 1), Y, Width)] - CentreZ;
            const double GradY = Depth[Index(X, std::min(Y + 1, Height - 1), Width)] - CentreZ;

            double Sum         = 0.0;
            double WeightedSum = 0.0;
            double VarianceSum = 0.0;
            for (int TapY = -2; TapY <= 2; ++TapY)
            {
                for (int TapX = -2; TapX <= 2; ++TapX)
                {
                    const int OffsetX = TapX * static_cast<int>(Step);
                    const int OffsetY = TapY * static_cast<int>(Step);
                    const int QX      = static_cast<int>(X) + OffsetX;
                    const int QY      = static_cast<int>(Y) + OffsetY;
                    if (QX < 0 || QY < 0 || QX >= static_cast<int>(Width) || QY >= static_cast<int>(Height))
                    {
                        continue;
                    }
                    const size_t Q           = Index(static_cast<uint32_t>(QX), static_cast<uint32_t>(QY), Width);
                    const double DepthExpect = GradX * OffsetX + GradY * OffsetY;
                    const double WeightZ     = std::exp(-std::abs(CentreZ - Depth[Q]) /
                                                        (Parameters.SigmaDepth * std::abs(DepthExpect) + 1e-4));
                    const double WeightN     = NormalWeight(CentreN, Normals[Q], Parameters.SigmaNormal);
                    const double WeightL     = std::exp(-std::abs(Centre - Input[Q]) / SigmaLum);
                    const double W           = Kernel[TapX + 2] * Kernel[TapY + 2] * WeightZ * WeightN * WeightL;
                    Sum += W;
                    WeightedSum += W * Input[Q];
                    VarianceSum += W * W * std::max(VarianceIn[Q], 0.0);
                }
            }
            if (Sum > 0.0)
            {
                Output[P] = WeightedSum / Sum;
                if (VarianceOut != nullptr)
                {
                    VarianceOut[P] = VarianceSum / (Sum * Sum);
                }
            }
            else
            {
                Output[P] = Centre;   // Fully rejected neighbourhood: keep the centre sample defined
                if (VarianceOut != nullptr)
                {
                    VarianceOut[P] = VarianceIn[P];
                }
            }
        }
    }
    return true;
}

bool JointBilateralUpsample2X(const double* Low, const double* LowDepth, const Vec3* LowNormals,
                              uint32_t LowWidth, uint32_t LowHeight, const double* HighDepth,
                              const Vec3* HighNormals, const VarianceFilterParameters& Parameters,
                              double* HighOut)
{
    if (Low == nullptr || LowDepth == nullptr || LowNormals == nullptr || HighDepth == nullptr ||
        HighNormals == nullptr || HighOut == nullptr || LowWidth == 0 || LowHeight == 0)
    {
        return false;
    }
    const uint32_t HighWidth  = LowWidth * 2;
    const uint32_t HighHeight = LowHeight * 2;
    for (uint32_t Y = 0; Y < HighHeight; ++Y)
    {
        for (uint32_t X = 0; X < HighWidth; ++X)
        {
            const size_t   P       = Index(X, Y, HighWidth);
            const double   CentreZ = HighDepth[P];
            const Vec3     CentreN = HighNormals[P];
            const uint32_t BaseX   = std::min(X / 2, LowWidth - 1);
            const uint32_t BaseY   = std::min(Y / 2, LowHeight - 1);
            const double   DepthTolerance = Parameters.SigmaDepth * 0.01 * std::max(std::abs(CentreZ), 1e-3) + 1e-4;

            double Sum = 0.0, WeightedSum = 0.0;
            for (uint32_t DY = 0; DY < 2; ++DY)
            {
                for (uint32_t DX = 0; DX < 2; ++DX)
                {
                    const uint32_t LX = std::min(BaseX + DX, LowWidth - 1);
                    const uint32_t LY = std::min(BaseY + DY, LowHeight - 1);
                    const size_t   Q  = Index(LX, LY, LowWidth);
                    const double   W  = std::exp(-std::abs(CentreZ - LowDepth[Q]) / DepthTolerance) *
                                        NormalWeight(CentreN, LowNormals[Q], Parameters.SigmaNormal);
                    Sum += W;
                    WeightedSum += W * Low[Q];
                }
            }
            HighOut[P] = Sum > 1e-9 ? WeightedSum / Sum : Low[Index(BaseX, BaseY, LowWidth)];
        }
    }
    return true;
}

} // namespace Frontier
