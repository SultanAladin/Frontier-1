/*==========================================================================================================================================
                                                 SOLAR DISC PIXEL FILTER
                                     ------------------------------------------------------------------
                  Finite-pixel solar disc with analytical Gaussian footprint for the CPU reference.
==========================================================================================================================================*/
// 🧩 Integrate a limb-darkened solar disc over finite camera pixels.

#pragma once

#include "LensFlareModel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier
{

// 📝 For unresolved/small discs, integrate a Gaussian PSF over the rectangular pixel analytically
//    (separable erf) and integrate the limb-darkened source with a normalized polar quadrature. For
//    resolved discs use the existing locally planar erfc limb, averaged over a small pixel footprint.
//    Coordinates are locally tangent angles [rad]; the small-angle approximation is appropriate to
//    the Sun but is not a measured camera/lens PSF or a full-sphere convolution.
class SolarDiscPixelFilter
{
public:
    explicit SolarDiscPixelFilter(SunDiscProfile Profile) noexcept : DiscProfile_(Profile)
    {
        constexpr double Pi = 3.14159265358979323846;
        ProfileValid_ = std::isfinite(Profile.AngularRadiusRad) && Profile.AngularRadiusRad > 0.0 &&
                  Profile.AngularRadiusRad < 0.5*Pi && std::isfinite(Profile.EdgeBlurRad) &&
                  Profile.EdgeBlurRad >= 0.0 && Profile.EdgeBlurRad < 0.25*Pi &&
                  std::isfinite(Profile.LimbLinear) && std::isfinite(Profile.LimbQuadratic) &&
                  Profile.LimbLinear >= 0.0 && Profile.LimbQuadratic >= 0.0 &&
                  Profile.LimbLinear + Profile.LimbQuadratic <= 1.0;
        if (!ProfileValid_) return;
        double Sum = 0.0;
        for (int Ring = 0; Ring < Rings; ++Ring)
        {
            const double AreaFraction = (Ring + 0.5) / Rings;
            const double Radius = Profile.AngularRadiusRad * std::sqrt(AreaFraction);
            const double Mu = std::sqrt(1.0 - AreaFraction);
            const double Limb = std::max(0.0, 1.0 - Profile.LimbLinear*(1.0-Mu) -
                                                   Profile.LimbQuadratic*(1.0-Mu)*(1.0-Mu));
            for (int Sector = 0; Sector < Sectors; ++Sector)
            {
                // Rotate successive rings to avoid aligned radial spoke patterns.
                const double Angle = 2.0 * Pi * (Sector + 0.5 * (Ring & 1)) / Sectors;
                SolarSourceSample& S = SourceSamples_[Ring*Sectors+Sector];
                S = {Radius*std::cos(Angle), Radius*std::sin(Angle), Limb};
                Sum += Limb;
            }
        }
        if (Sum > 0.0) for (SolarSourceSample& S : SourceSamples_) S.Weight /= Sum;
        // Normalize the approximate resolved limb against its radial integral once per profile.
        // This keeps the artist-friendly erfc edge from losing or gaining disc flux as blur changes.
        const double Sigma = Profile.EdgeBlurRad > 0.0 ? Profile.EdgeBlurRad : 1e-9;
        const double Radius = Profile.AngularRadiusRad;
        if (Radius > 0.0)
        {
            constexpr int Steps = 256;
            double Integral = 0.0;
            for (int Part = 0; Part < 2; ++Part)
            {
                const double Start = Part == 0 ? 0.0 : Radius;
                const double End = Part == 0 ? Radius : Radius + 8.0*Sigma;
                const double Width = (End-Start)/Steps;
                for (int I = 0; I < Steps; ++I)
                {
                    const double Theta = Start + (I+0.5)*Width;
                    const double Radiance = EvaluateSunDiscRadiance(Theta, Profile, {1,1,1}).R;
                    Integral += Radiance * 2.0*Pi*std::sin(Theta)*Width;
                }
            }
            const double Target = SunDiscSolidAngleSr(Radius) * SunDiscMeanLimbFactor(Profile);
            if (Integral > 0.0 && Target > 0.0) ResolvedCorrection_ = Target / Integral;
        }
    }

    // Irradiance is integrated over the solar disc at the observer (after atmospheric attenuation).
    // The returned RGB is average radiance in this pixel; Dx/Dy and pixel sizes are tangent angles.
    [[nodiscard]] SpectrumRgb Evaluate(double Dx, double Dy, double PixelWidth, double PixelHeight,
                                       const SpectrumRgb& Irradiance) const noexcept
    {
        const double Radius = DiscProfile_.AngularRadiusRad;
        if (!ProfileValid_ || !std::isfinite(Dx) || !std::isfinite(Dy) || !std::isfinite(PixelWidth) ||
            !std::isfinite(PixelHeight) || PixelWidth <= 0.0 || PixelHeight <= 0.0 ||
            !std::isfinite(Irradiance.R) || !std::isfinite(Irradiance.G) || !std::isfinite(Irradiance.B) ||
            Irradiance.R < 0.0 || Irradiance.G < 0.0 || Irradiance.B < 0.0) return {};
        const double RadiusPixels = Radius / std::max(PixelWidth, PixelHeight);
        const double Blend = std::clamp((6.0 - RadiusPixels) / 3.0, 0.0, 1.0);
        const double SmallWeight = Blend*Blend*(3.0 - 2.0*Blend);
        const double ResolvedSigma = DiscProfile_.EdgeBlurRad > 0.0 ? DiscProfile_.EdgeBlurRad : 1e-9;
        const double SmallSigma = std::max(ResolvedSigma, 0.20 * std::min(PixelWidth, PixelHeight));
        const double HalfDiagonal = 0.5 * std::hypot(PixelWidth, PixelHeight);
        if (std::hypot(Dx, Dy) > Radius + HalfDiagonal + 6.0*std::max(SmallSigma, ResolvedSigma)) return {};
        const double Mean = SunDiscMeanLimbFactor(DiscProfile_);
        const double SolidAngle = SunDiscSolidAngleSr(Radius);
        if (!(Mean > 0.0 && SolidAngle > 0.0)) return {};
        const double PixelArea = PixelWidth * PixelHeight;
        if (!std::isfinite(PixelArea) || PixelArea < 1e-300) return {};
        double Scalar = 0.0;
        if (SmallWeight > 0.0)
        {
            constexpr double RootTwo = 1.4142135623730951;
            const double Scale = 1.0 / (RootTwo * SmallSigma);
            double Mass = 0.0;
            for (const SolarSourceSample& S : SourceSamples_)
            {
                const double X0 = (Dx - 0.5*PixelWidth - S.X)*Scale, X1 = (Dx + 0.5*PixelWidth - S.X)*Scale;
                const double Y0 = (Dy - 0.5*PixelHeight - S.Y)*Scale, Y1 = (Dy + 0.5*PixelHeight - S.Y)*Scale;
                const double XMass = 0.5 * (std::erf(X1)-std::erf(X0));
                const double YMass = 0.5 * (std::erf(Y1)-std::erf(Y0));
                Mass += S.Weight * XMass * YMass;
            }
            Scalar = SmallWeight * Mass / PixelArea;
        }
        if (SmallWeight < 1.0)
        {
            // Resolved-disc path: a four-by-four midpoint pixel integral of the existing erfc limb.
            SunDiscProfile Profile = DiscProfile_; Profile.EdgeBlurRad = ResolvedSigma;
            constexpr int Grid = 4;
            double Average = 0.0;
            for (int Y = 0; Y < Grid; ++Y)
                for (int X = 0; X < Grid; ++X)
                {
                    const double Xp = Dx + ((X+0.5)/Grid-0.5)*PixelWidth;
                    const double Yp = Dy + ((Y+0.5)/Grid-0.5)*PixelHeight;
                    Average += EvaluateSunDiscRadiance(std::hypot(Xp,Yp), Profile, {1,1,1}).R;
                }
            Scalar += (1.0 - SmallWeight) * Average * ResolvedCorrection_ / (Grid*Grid * SolidAngle * Mean);
        }
        return {Irradiance.R*Scalar, Irradiance.G*Scalar, Irradiance.B*Scalar};
    }

private:
    static constexpr int Rings = 32, Sectors = 48;
    struct SolarSourceSample { double X = 0.0, Y = 0.0, Weight = 0.0; };
    SunDiscProfile DiscProfile_;
    bool ProfileValid_ = false;
    double ResolvedCorrection_ = 1.0;
    std::array<SolarSourceSample, Rings*Sectors> SourceSamples_{};
};

} // namespace Frontier
