/*==========================================================================================================================================
                                                          SCATTERINGSEQUENCE.H
==========================================================================================================================================*/
// 🧩 Ordered opaque scattering strata and absorbing dielectric coatings, with a bounded shared preview-shader contract.

#pragma once
#include "SurfaceNormalProjection.h"
#include <numbers>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     ORDERED SCATTERING CONTRACT
//------------------------------------------------------------------------------------------------------------------------

// 📝 Opaque strata mix evaluated scattering by coverage. Coatings attenuate the scattering beneath, not its parameters.
enum class ScatteringCategory : uint32_t
{
    Opaque = 0,
    Coating = 1
};

struct alignas(16) ScatteringStratum
{
    std::array<float,4> AlbedoRoughness{.5f,.5f,.5f,.4f};
    std::array<float,4> Optics{0,1.5f,1,0};                 // [-] - metallic, IOR, coverage, reserved
    std::array<float,4> AbsorptionThickness{0,0,0,0};       // [1/m,m] - absorption RGB and coating thickness
    std::array<uint32_t,4> Classification{0,0,0,0};
    // 📝 Charlie-distribution fabric sheen (fuzz rim), zero by default so every non-fabric
    // stratum stays bit-identical. [rgb] is the authored tint at full slider strength, [a] is
    // the live intensity multiplier (0..1); roughness lives alongside with its own reserved pair,
    // matching the Classification convention of one meaningful lane plus validated-zero padding.
    std::array<float,4> Sheen{0,0,0,0};                     // [rgb] tint colour (linear), [a] intensity
    std::array<float,4> SheenFinish{0,0,0,0};                // [x] roughness, [y,z,w] reserved, must be zero
};

struct alignas(16) PreviewScattering
{
    std::array<float,16> ViewFromSurface{1,0,0,0,0,1,0,0,0,0,1,0,0,0,-3,1};
    std::array<float,4> LightDirectionIntensity{-.4f,.6f,.7f,3};
    std::array<float,4> LightColourExposure{1,.95f,.9f,1};
    std::array<float,4> EnvironmentHorizon{.12f,.14f,.18f,1};
    std::array<float,4> EnvironmentZenith{.45f,.55f,.8f,0};
    std::array<float,4> EnvironmentUp{0,1,0,0};
    std::array<uint32_t,4> Configuration{1,0,0,0};           // [-] - stratum count, sRGB planar albedo texture, perspective, reserved
    std::array<ScatteringStratum,8> Strata{};
};

static_assert(sizeof(ScatteringStratum) == 96);
static_assert(offsetof(PreviewScattering,Strata) == 160);
static_assert(sizeof(PreviewScattering) == 928);

inline bool ValidatePreviewScattering(const PreviewScattering &Scattering)
{
    if (!Scattering.Configuration[0] || Scattering.Configuration[0] > 8 || Scattering.Configuration[1] > 1 ||
        Scattering.Configuration[2] > 1 || Scattering.Configuration[3] != 0) return false;
    for (const auto Coefficient : Scattering.ViewFromSurface)
        if (!std::isfinite(Coefficient)) return false;
    if (Scattering.ViewFromSurface[3] != 0 || Scattering.ViewFromSurface[7] != 0 ||
        Scattering.ViewFromSurface[11] != 0 || Scattering.ViewFromSurface[15] != 1) return false;
    for (const auto *Spectrum : {&Scattering.LightDirectionIntensity,&Scattering.LightColourExposure,
                                 &Scattering.EnvironmentHorizon,&Scattering.EnvironmentZenith,&Scattering.EnvironmentUp})
        for (const auto Channel : *Spectrum)
            if (!std::isfinite(Channel) || std::abs(Channel) > 64) return false;
    for (size_t Channel = 0; Channel < 3; ++Channel)
        if (Scattering.LightColourExposure[Channel] < 0 || Scattering.EnvironmentHorizon[Channel] < 0 ||
            Scattering.EnvironmentZenith[Channel] < 0) return false;
    const auto DirectionLength = [](const auto &Direction)
    {
        return double(Direction[0])*Direction[0]+double(Direction[1])*Direction[1]+double(Direction[2])*Direction[2];
    };
    if (DirectionLength(Scattering.LightDirectionIntensity) < 1e-8 || DirectionLength(Scattering.EnvironmentUp) < 1e-8 ||
        Scattering.LightDirectionIntensity[3] < 0 || Scattering.LightColourExposure[3] < 0 ||
        Scattering.EnvironmentHorizon[3] < 0 || Scattering.EnvironmentZenith[3] != 0 || Scattering.EnvironmentUp[3] != 0) return false;
    for (size_t Index = 0; Index < Scattering.Configuration[0]; ++Index)
    {
        const auto &Stratum = Scattering.Strata[Index];
        for (const auto *Spectrum : {&Stratum.AlbedoRoughness,&Stratum.Optics,&Stratum.AbsorptionThickness,
                                     &Stratum.Sheen,&Stratum.SheenFinish})
            for (const auto Channel : *Spectrum)
                if (!std::isfinite(Channel)) return false;
        for (size_t Channel = 0; Channel < 3; ++Channel)
            if (Stratum.AlbedoRoughness[Channel] < 0 || Stratum.AlbedoRoughness[Channel] > 1 ||
                Stratum.AbsorptionThickness[Channel] < 0 || Stratum.AbsorptionThickness[Channel] > 100 ||
                Stratum.Sheen[Channel] < 0 || Stratum.Sheen[Channel] > 2) return false;
        if (Stratum.AlbedoRoughness[3] < .045f || Stratum.AlbedoRoughness[3] > 1 ||
            Stratum.Optics[0] < 0 || Stratum.Optics[0] > 1 || Stratum.Optics[1] < 1 || Stratum.Optics[1] > 3 ||
            Stratum.Optics[2] < 0 || Stratum.Optics[2] > 1 || Stratum.Optics[3] != 0 ||
            Stratum.AbsorptionThickness[3] < 0 || Stratum.AbsorptionThickness[3] > 1 ||
            Stratum.Sheen[3] < 0 || Stratum.Sheen[3] > 1 ||
            Stratum.SheenFinish[0] < 0 || Stratum.SheenFinish[0] > 1 ||
            Stratum.SheenFinish[1] || Stratum.SheenFinish[2] || Stratum.SheenFinish[3] ||
            Stratum.Classification[0] > 1 || Stratum.Classification[1] || Stratum.Classification[2] || Stratum.Classification[3]) return false;
        if (Index == 0 && (Stratum.Classification[0] != 0 || Stratum.Optics[2] != 1)) return false;
        if (Stratum.Classification[0] == 1 && Stratum.Optics[0] != 0) return false;
        if (Stratum.Classification[0] == 0 && Stratum.AbsorptionThickness != std::array<float,4>{0,0,0,0}) return false;
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SCALAR REFERENCE EVALUATION
//------------------------------------------------------------------------------------------------------------------------

using ScatteringSpectrum = std::array<double,3>;
inline double EvaluateGgxDistribution(double NormalHalf, double Roughness)
{
    const double α = Roughness*Roughness;
    const double Denominator = NormalHalf*NormalHalf*(α*α-1)+1;
    return α*α/(std::numbers::pi*Denominator*Denominator);
}
inline double EvaluateDielectricReflectance(double Ior)
{
    const double Ratio = (Ior-1)/(Ior+1);
    return Ratio*Ratio;
}
// 📝 Charlie sheen lobe (Estevez & Kulla), shared verbatim with the GPU evaluator so a fabric's
// grazing rim matches between the CPU reference and the shader-ball preview.
inline double EvaluateCharlieDistribution(double Roughness, double NormalHalf)
{
    const double InverseAlpha = 1 / std::max(Roughness*Roughness,1e-3);
    const double SineSquared = std::max(1-NormalHalf*NormalHalf,.0078125);
    return (2+InverseAlpha)*std::pow(SineSquared,InverseAlpha*.5)/(2*std::numbers::pi);
}
inline double EvaluateAshikhminVisibility(double NormalView, double NormalLight)
{
    return 1 / std::max(1e-4,4*(NormalLight+NormalView-NormalLight*NormalView));
}
// 📝 For n=1/alpha, Charlie D <= (n+2)/(2*pi)/(1+n*NL^2/8) and V*NL <= 1/4.
// Integrating that bound gives a view-independent upper bound on hemispherical sheen reflectance.
inline double EvaluateSheenEnergyScale(double Roughness)
{
    const double InverseAlpha = 1 / std::max(Roughness*Roughness, 1e-3);
    const double Radius = std::sqrt(InverseAlpha / 8);
    const double Bound = (InverseAlpha + 2) * std::atan(Radius) / (4 * Radius);
    return 1 / std::max(1.0, Bound);
}
inline double EvaluateDiffuseTransmission(double FresnelOrigin, double NormalView, double NormalLight)
{
    const double Entrance = FresnelOrigin + (1-FresnelOrigin)*std::pow(1-NormalLight, 5);
    const double Exit = FresnelOrigin + (1-FresnelOrigin)*std::pow(1-NormalView, 5);
    return (1-Entrance)*(1-Exit);
}
inline ScatteringSpectrum EvaluateScatteringSequence(
    const PreviewScattering &Scattering,
    double                    NormalView,
    double                    NormalLight,
    double                    NormalHalf,
    double                    ViewHalf)
{
    ScatteringSpectrum Accumulated{};
    if (NormalView <= 0 || NormalLight <= 0) return Accumulated;
    for (size_t Index = 0; Index < Scattering.Configuration[0]; ++Index)
    {
        const auto &Stratum = Scattering.Strata[Index];
        const double Roughness = Stratum.AlbedoRoughness[3];
        const double α = Roughness*Roughness;
        const double Visibility = .5 / std::max(1e-12,NormalLight*std::sqrt(NormalView*NormalView*(1-α*α)+α*α) +
                                                     NormalView*std::sqrt(NormalLight*NormalLight*(1-α*α)+α*α));
        const double Distribution = EvaluateGgxDistribution(NormalHalf,Roughness);
        const double Reflectance = EvaluateDielectricReflectance(Stratum.Optics[1]);
        const double Coverage = Stratum.Optics[2];
        for (size_t Channel = 0; Channel < 3; ++Channel)
        {
            const double Albedo = Stratum.AlbedoRoughness[Channel];
            const double Metal = Stratum.Optics[0];
            const double FresnelOrigin = Reflectance*(1-Metal)+Albedo*Metal;
            const double Fresnel = FresnelOrigin+(1-FresnelOrigin)*std::pow(1-ViewHalf,5);
            double Evaluation = Distribution*Visibility*Fresnel;
            if (Stratum.Classification[0] == 0)
            {
                Evaluation += EvaluateDiffuseTransmission(FresnelOrigin,NormalView,NormalLight)*
                    (1-Metal)*Albedo/std::numbers::pi;
                const double FuzzCoverage = std::clamp(double(Stratum.Sheen[Channel]),0.0,1.0)*Stratum.Sheen[3];
                if (FuzzCoverage > 0)
                {
                    const double Sheen = EvaluateSheenEnergyScale(Stratum.SheenFinish[0])*
                        EvaluateCharlieDistribution(Stratum.SheenFinish[0],NormalHalf)*
                        EvaluateAshikhminVisibility(NormalView,NormalLight);
                    Evaluation = Evaluation*(1-FuzzCoverage)+Sheen*FuzzCoverage;
                }
            }
            else
            {
                const double Entrance = Reflectance+(1-Reflectance)*std::pow(1-NormalLight,5);
                const double Exit = Reflectance+(1-Reflectance)*std::pow(1-NormalView,5);
                const double Absorption = std::exp(-Stratum.AbsorptionThickness[Channel]*Stratum.AbsorptionThickness[3]*
                                                   (1/std::max(.001,NormalView)+1/std::max(.001,NormalLight)));
                Evaluation += Accumulated[Channel]*((1-Entrance)*(1-Exit))*Absorption;
            }
            Accumulated[Channel] = Accumulated[Channel]*(1-Coverage)+Evaluation*Coverage;
        }
    }
    return Accumulated;
}
inline double EncodePreviewDisplay(double Radiance, double Exposure)
{
    const double Exposed = std::max(0.0,Radiance*Exposure);
    const double Display = std::clamp(Exposed*(2.51*Exposed+.03)/(Exposed*(2.43*Exposed+.59)+.14),0.0,1.0);
    return Display <= .0031308 ? 12.92*Display : 1.055*std::pow(Display,1/2.4)-.055;
}
} // namespace Frontier
