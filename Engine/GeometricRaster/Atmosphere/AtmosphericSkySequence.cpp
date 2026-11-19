/*============================================================================================================================================
                                                      ATMOSPHERICSKYSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Physical Bruneton/Hillaire Rayleigh + Mie + Ozone atmospheric transmittance, SkyView LUT, outer-space planetary limb & Eddington solar disc

#include "Engine/GeometricRaster/Atmosphere/AtmosphericSkySequence.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::GeometricRaster
{

namespace
{

constexpr float DefaultViewerAltitudeMeters = 100.0f;
constexpr float SeaLevelRayleighScatter[3] { 5.802e-6f, 13.558e-6f, 33.1e-6f };
constexpr float SeaLevelMieScatter        = 3.996e-6f;
constexpr float SeaLevelMieExtinction     = 4.40e-6f;
constexpr float PeakOzoneAbs[3]       { 0.650e-6f, 1.881e-6f, 0.085e-6f };

[[nodiscard]] std::array<float, 2> IntersectVecSphere(
    float OX, float OY, float OZ,
    float DX, float DY, float DZ,
    float Radius) noexcept
{
    const float B    = OX * DX + OY * DY + OZ * DZ;
    const float C    = OX * OX + OY * OY + OZ * OZ - Radius * Radius;
    const float Disc = B * B - C;
    if (Disc < 0.0f)
    {
        return { -1.0f, -1.0f };
    }
    const float Root = std::sqrt(Disc);
    return { -B - Root, -B + Root };
}

[[nodiscard]] float ClampUnit(float Value) noexcept
{
    return std::clamp(Value, -1.0f, 1.0f);
}

[[nodiscard]] float ComputeHorizonDistanceKm(float BottomRadiusKm, float TopRadiusKm) noexcept
{
    return std::sqrt(std::max(0.0f, (TopRadiusKm - BottomRadiusKm) * (TopRadiusKm + BottomRadiusKm)));
}

[[nodiscard]] bool QuerySphereBlockedKm(float RadiusKm, float CosinePolar, float TargetRadiusKm) noexcept
{
    if (CosinePolar >= 0.0f)
    {
        return false;
    }
    const float Disc = RadiusKm * RadiusKm * CosinePolar * CosinePolar -
                       (RadiusKm - TargetRadiusKm) * (RadiusKm + TargetRadiusKm);
    return Disc >= 0.0f && RadiusKm > TargetRadiusKm + 1.0e-4f;
}

[[nodiscard]] float IntersectUpperSphereDistanceKm(float RadiusKm, float CosinePolar, float TopRadiusKm) noexcept
{
    const float Disc = std::max(
        0.0f,
        RadiusKm * RadiusKm * CosinePolar * CosinePolar +
            (TopRadiusKm - RadiusKm) * (TopRadiusKm + RadiusKm));
    return std::max(0.0f, -RadiusKm * CosinePolar + std::sqrt(Disc));
}

[[nodiscard]] float IntersectLowerSphereDistanceKm(float RadiusKm, float CosinePolar, float BottomRadiusKm, bool& OutHits) noexcept
{
    const float Disc = RadiusKm * RadiusKm * CosinePolar * CosinePolar -
                       (RadiusKm - BottomRadiusKm) * (RadiusKm + BottomRadiusKm);
    if (CosinePolar < 0.0f && Disc >= 0.0f)
    {
        const float Hit = -RadiusKm * CosinePolar - std::sqrt(Disc);
        if (Hit > 0.0f)
        {
            OutHits = true;
            return Hit;
        }
    }
    OutHits = false;
    return 0.0f;
}

[[nodiscard]] float ComputeLimbPolarAngle(float ViewerRadiusKm, float BottomRadiusKm) noexcept
{
    const float SafeR = std::max(BottomRadiusKm + 1.0e-4f, ViewerRadiusKm);
    const float TangentHeight = std::sqrt(std::max(0.0f, (SafeR - BottomRadiusKm) * (SafeR + BottomRadiusKm)));
    return std::acos(ClampUnit(-TangentHeight / SafeR));
}

struct MediumCoefficientsKm
{
    float RayScatterR = 0.0f;
    float RayScatterG = 0.0f;
    float RayScatterB = 0.0f;
    float MieScatter  = 0.0f;
    float ExtinctionR = 0.0f;
    float ExtinctionG = 0.0f;
    float ExtinctionB = 0.0f;
};

[[nodiscard]] MediumCoefficientsKm EvaluateMediumAtAltitudeKm(
    float                                 AltitudeKm,
    const AtmosphericMediumConfiguration& Medium) noexcept
{
    const float AltM     = std::max(0.0f, AltitudeKm * 1000.0f);
    const float RayH     = std::max(500.0f, Medium.RayleighScaleHeightMeters);
    const float MieH     = std::max(200.0f, Medium.MieScaleHeightMeters);
    const float RayMul   = std::max(0.0f, Medium.RayleighStrength);
    const float MieMul   = std::max(0.0f, Medium.MieStrength);
    const float OzoneMul = std::max(0.0f, Medium.OzoneStrength);

    const float DenR = std::exp(-AltM / RayH) * RayMul;
    const float DenM = std::exp(-AltM / MieH) * MieMul;
    const float DenO = std::max(0.0f, 1.0f - std::abs(AltM - 25000.0f) / 15000.0f) * OzoneMul;

    // Convert m^-1 extinction/scattering into km^-1 for spherical km quadrature
    MediumCoefficientsKm C {};
    C.RayScatterR = (SeaLevelRayleighScatter[0] * 1000.0f) * DenR;
    C.RayScatterG = (SeaLevelRayleighScatter[1] * 1000.0f) * DenR;
    C.RayScatterB = (SeaLevelRayleighScatter[2] * 1000.0f) * DenR;
    C.MieScatter  = (SeaLevelMieScatter * 1000.0f) * DenM;
    const float MieExt  = (SeaLevelMieExtinction * 1000.0f) * DenM;
    const float OzoneR  = (PeakOzoneAbs[0] * 1000.0f) * DenO;
    const float OzoneG  = (PeakOzoneAbs[1] * 1000.0f) * DenO;
    const float OzoneB  = (PeakOzoneAbs[2] * 1000.0f) * DenO;
    C.ExtinctionR = C.RayScatterR + MieExt + OzoneR;
    C.ExtinctionG = C.RayScatterG + MieExt + OzoneG;
    C.ExtinctionB = C.RayScatterB + MieExt + OzoneB;
    return C;
}

[[nodiscard]] std::array<float, 3> SampleLut2DBilinearRgb(
    const std::vector<float>& Lut,
    uint32_t                  Width,
    uint32_t                  Height,
    float                     U,
    float                     V) noexcept
{
    const float FX = std::clamp(U, 0.0f, 1.0f) * static_cast<float>(Width - 1u);
    const float FY = std::clamp(V, 0.0f, 1.0f) * static_cast<float>(Height - 1u);
    const uint32_t X0 = std::min(Width - 2u, static_cast<uint32_t>(std::floor(FX)));
    const uint32_t Y0 = std::min(Height - 2u, static_cast<uint32_t>(std::floor(FY)));
    const float TX = FX - static_cast<float>(X0);
    const float TY = FY - static_cast<float>(Y0);

    const std::size_t I00 = (static_cast<std::size_t>(Y0) * Width + X0) * 3u;
    const std::size_t I10 = I00 + 3u;
    const std::size_t I01 = I00 + static_cast<std::size_t>(Width) * 3u;
    const std::size_t I11 = I01 + 3u;
    const auto Mix = [](float A, float B, float T) noexcept -> float { return A + (B - A) * T; };
    return {
        Mix(Mix(Lut[I00 + 0u], Lut[I10 + 0u], TX), Mix(Lut[I01 + 0u], Lut[I11 + 0u], TX), TY),
        Mix(Mix(Lut[I00 + 1u], Lut[I10 + 1u], TX), Mix(Lut[I01 + 1u], Lut[I11 + 1u], TX), TY),
        Mix(Mix(Lut[I00 + 2u], Lut[I10 + 2u], TX), Mix(Lut[I01 + 2u], Lut[I11 + 2u], TX), TY),
    };
}

[[nodiscard]] std::array<float, 3> SampleTransmittanceSphericalLut(
    const std::vector<float>& Lut,
    float                     RadiusKm,
    float                     CosPolar,
    float                     BottomRadiusKm,
    float                     TopRadiusKm) noexcept
{
    if (QuerySphereBlockedKm(RadiusKm, CosPolar, BottomRadiusKm))
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    const float ClampedR  = std::clamp(RadiusKm, BottomRadiusKm, TopRadiusKm);
    const float HorizonKm = ComputeHorizonDistanceKm(BottomRadiusKm, TopRadiusKm);
    const float RhoKm     = std::sqrt(std::max(0.0f, (ClampedR - BottomRadiusKm) * (ClampedR + BottomRadiusKm)));
    const float DistTopKm = IntersectUpperSphereDistanceKm(ClampedR, ClampUnit(CosPolar), TopRadiusKm);
    const float DMinKm    = std::max(0.0f, TopRadiusKm - ClampedR);
    const float DMaxKm    = RhoKm + HorizonKm;
    const float U         = std::clamp((DistTopKm - DMinKm) / std::max(1.0e-5f, DMaxKm - DMinKm), 0.0f, 1.0f);
    const float V         = std::clamp(RhoKm / std::max(1.0e-5f, HorizonKm), 0.0f, 1.0f);
    return SampleLut2DBilinearRgb(
        Lut,
        AtmosphericSkySequence::TransmittanceWidthPx,
        AtmosphericSkySequence::TransmittanceHeightPx,
        U,
        V);
}

[[nodiscard]] std::array<float, 3> SampleMultiScatterGridLut(
    const std::vector<float>& Lut,
    float                     AltitudeKm,
    float                     SolarCosine,
    float                     ThicknessKm) noexcept
{
    const float U = std::clamp(0.5f * (SolarCosine + 1.0f), 0.0f, 1.0f);
    const float V = std::sqrt(std::clamp(AltitudeKm / std::max(1.0e-3f, ThicknessKm), 0.0f, 1.0f));
    return SampleLut2DBilinearRgb(
        Lut,
        AtmosphericSkySequence::MultiScatterWidthPx,
        AtmosphericSkySequence::MultiScatterHeightPx,
        U,
        V);
}

[[nodiscard]] uint32_t HashOrbitU3(int32_t X, int32_t Y, int32_t Z) noexcept
{
    uint32_t H = static_cast<uint32_t>(X) * 73856093u ^
                 static_cast<uint32_t>(Y) * 19349663u ^
                 static_cast<uint32_t>(Z) * 83492791u;
    H = (H ^ (H >> 15u)) * 2246822519u;
    H = (H ^ (H >> 13u)) * 3266489917u;
    return H ^ (H >> 16u);
}

[[nodiscard]] float NoiseOrbit3(float X, float Y, float Z) noexcept
{
    const int32_t IX = static_cast<int32_t>(std::floor(X));
    const int32_t IY = static_cast<int32_t>(std::floor(Y));
    const int32_t IZ = static_cast<int32_t>(std::floor(Z));
    const float FX = X - static_cast<float>(IX);
    const float FY = Y - static_cast<float>(IY);
    const float FZ = Z - static_cast<float>(IZ);
    const float UX = FX * FX * (3.0f - 2.0f * FX);
    const float UY = FY * FY * (3.0f - 2.0f * FY);
    const float UZ = FZ * FZ * (3.0f - 2.0f * FZ);

    auto Corner = [&](int32_t DX, int32_t DY, int32_t DZ) noexcept -> float
    {
        return static_cast<float>(HashOrbitU3(IX + DX, IY + DY, IZ + DZ) & 0xFFFFu) * (1.0f / 65535.0f);
    };
    const auto Mix = [](float A, float B, float T) noexcept -> float { return A + (B - A) * T; };
    const float Z0 = Mix(Mix(Corner(0, 0, 0), Corner(1, 0, 0), UX), Mix(Corner(0, 1, 0), Corner(1, 1, 0), UX), UY);
    const float Z1 = Mix(Mix(Corner(0, 0, 1), Corner(1, 0, 1), UX), Mix(Corner(0, 1, 1), Corner(1, 1, 1), UX), UY);
    return Mix(Z0, Z1, UZ);
}

[[nodiscard]] float FbmOrbit3(float X, float Y, float Z, uint32_t Octaves = 5u) noexcept
{
    float Sum = 0.0f;
    float Amp = 0.5f;
    float PX = X, PY = Y, PZ = Z;
    for (uint32_t I = 0u; I < Octaves; ++I)
    {
        Sum += Amp * NoiseOrbit3(PX, PY, PZ);
        // Domain rotation between octaves removes axis-aligned striping on spheres
        const float RX = 0.80f * PX - 0.48f * PY + 0.36f * PZ;
        const float RY = 0.48f * PX + 0.86f * PY + 0.16f * PZ;
        const float RZ = -0.36f * PX + 0.04f * PY + 0.93f * PZ;
        PX = RX * 2.04f + 13.7f;
        PY = RY * 2.04f - 7.3f;
        PZ = RZ * 2.04f + 4.1f;
        Amp *= 0.5f;
    }
    return Sum;
}

[[nodiscard]] std::array<float, 3> IntegrateRayTransmittance(
    float OX, float OY, float OZ,
    float DX, float DY, float DZ,
    float MaxDist,
    float PlanetR,
    float RayMul,
    float MieMul,
    float OzoneMul,
    float RayH,
    float MieH,
    uint32_t Steps) noexcept
{
    const float Step = MaxDist / static_cast<float>(std::max(1u, Steps));
    float DR = 0.0f, DG = 0.0f, DB = 0.0f;
    for (uint32_t I = 0u; I < Steps; ++I)
    {
        const float T    = (static_cast<float>(I) + 0.5f) * Step;
        const float PX   = OX + DX * T;
        const float PY   = OY + DY * T;
        const float PZ   = OZ + DZ * T;
        const float Alt  = std::max(0.0f, std::sqrt(PX * PX + PY * PY + PZ * PZ) - PlanetR);
        const float DenR = std::exp(-Alt / RayH) * RayMul;
        const float DenM = std::exp(-Alt / MieH) * MieMul;
        const float DenO = std::max(0.0f, 1.0f - std::abs(Alt - 25000.0f) / 15000.0f) * OzoneMul;
        const float MieE = SeaLevelMieExtinction * DenM;
        DR += (SeaLevelRayleighScatter[0] * DenR + MieE + PeakOzoneAbs[0] * DenO) * Step;
        DG += (SeaLevelRayleighScatter[1] * DenR + MieE + PeakOzoneAbs[1] * DenO) * Step;
        DB += (SeaLevelRayleighScatter[2] * DenR + MieE + PeakOzoneAbs[2] * DenO) * Step;
    }
    return { std::exp(-DR), std::exp(-DG), std::exp(-DB) };
}

} // namespace

Mathematics::LinearRGB AtmosphericSkySequence::ComputeSolarTransmittance(
    const Mathematics::Axis3D&            SunDirection,
    const AtmosphericMediumConfiguration& Medium,
    float                                 ObserverAltitudeMeters) noexcept
{
    const float GroundKm    = std::max(500.0f, Medium.PlanetRadiusMeters * 0.001f);
    const float ThicknessKm = std::max(10.0f, Medium.AtmosphereThicknessMeters * 0.001f);
    const float TopKm       = GroundKm + ThicknessKm;
    const float AltKm       = std::max(0.001f, ObserverAltitudeMeters * 0.001f);
    if (AltKm >= ThicknessKm)
    {
        return { 1.0f, 1.0f, 1.0f };
    }

    const float SunMag = std::sqrt(
        SunDirection.X * SunDirection.X + SunDirection.Y * SunDirection.Y + SunDirection.Z * SunDirection.Z);
    const float CosPolar = (SunMag > 1.0e-6f) ? ClampUnit(SunDirection.Z / SunMag) : 1.0f;
    const float RadiusKm = GroundKm + AltKm;

    if (QuerySphereBlockedKm(RadiusKm, CosPolar, GroundKm))
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    const float DistTopKm = IntersectUpperSphereDistanceKm(RadiusKm, CosPolar, TopKm);
    if (DistTopKm <= 1.0e-5f)
    {
        return { 1.0f, 1.0f, 1.0f };
    }

    constexpr uint32_t TransSteps = 64u;
    float OptR = 0.0f, OptG = 0.0f, OptB = 0.0f;
    for (uint32_t S = 0u; S < TransSteps; ++S)
    {
        const float F0   = static_cast<float>(S) / static_cast<float>(TransSteps);
        const float F1   = static_cast<float>(S + 1u) / static_cast<float>(TransSteps);
        const float T0   = DistTopKm * F0 * F0;
        const float T1   = DistTopKm * F1 * F1;
        const float T    = 0.5f * (T0 + T1);
        const float Step = std::max(1.0e-5f, T1 - T0);
        const float RK   = std::sqrt(std::max(GroundKm * GroundKm, RadiusKm * RadiusKm + 2.0f * RadiusKm * CosPolar * T + T * T));
        const auto  C    = EvaluateMediumAtAltitudeKm(RK - GroundKm, Medium);
        OptR += C.ExtinctionR * Step;
        OptG += C.ExtinctionG * Step;
        OptB += C.ExtinctionB * Step;
    }
    return { std::exp(-OptR), std::exp(-OptG), std::exp(-OptB) };
}

void AtmosphericSkySequence::BuildSkyViewLut(
    const Mathematics::Axis3D&            SunDirection,
    const AtmosphericMediumConfiguration& Medium,
    float                                 SolarIntensityScale,
    float                                 ObserverAltitudeMeters)
{
    const float SunMag = std::sqrt(
        SunDirection.X * SunDirection.X + SunDirection.Y * SunDirection.Y + SunDirection.Z * SunDirection.Z);
    const Mathematics::Axis3D UnitSun = (SunMag > 1.0e-6f)
        ? Mathematics::Axis3D{ SunDirection.X / SunMag, SunDirection.Y / SunMag, SunDirection.Z / SunMag }
        : Mathematics::Axis3D{ 0.0f, 0.0f, 1.0f };

    const float GroundKm    = std::max(500.0f, Medium.PlanetRadiusMeters * 0.001f);
    const float ThicknessKm = std::max(10.0f, Medium.AtmosphereThicknessMeters * 0.001f);
    const float TopKm       = GroundKm + ThicknessKm;
    const float RayMul      = std::max(0.0f, Medium.RayleighStrength);
    const float MieMul      = std::max(0.0f, Medium.MieStrength);
    const float MieG        = std::clamp(Medium.MieAnisotropy, 0.0f, 0.99f);
    const float OzoneMul    = std::max(0.0f, Medium.OzoneStrength);
    const float RayH        = std::max(500.0f, Medium.RayleighScaleHeightMeters);
    const float MieH        = std::max(200.0f, Medium.MieScaleHeightMeters);
    const float AlbHash     = Medium.GroundAlbedo.R + 7.1f * Medium.GroundAlbedo.G + 31.3f * Medium.GroundAlbedo.B;

    const float EffectiveAltM = std::clamp(
        (ObserverAltitudeMeters > 0.0f) ? ObserverAltitudeMeters : DefaultViewerAltitudeMeters,
        5.0f,
        std::max(100.0f, (ThicknessKm - 0.05f) * 1000.0f));
    const float StartRadiusKm = GroundKm + EffectiveAltM * 0.001f;

    // 1. Rebuild 256x64 Transmittance LUT + 32x32 Multi-Scatter LUT whenever planetary medium changes
    const bool MediumChanged =
        !QueryFoundationLutsReady() ||
        std::abs(BakedBottomRadiusKm  - GroundKm)    > 1.0e-4f ||
        std::abs(BakedTopRadiusKm     - TopKm)       > 1.0e-4f ||
        std::abs(BakedRayStrength     - RayMul)      > 1.0e-5f ||
        std::abs(BakedMieStrength     - MieMul)      > 1.0e-5f ||
        std::abs(BakedOzoneStrength   - OzoneMul)    > 1.0e-5f ||
        std::abs(BakedRayScaleH       - RayH)        > 1.0e-2f ||
        std::abs(BakedMieScaleH       - MieH)        > 1.0e-2f ||
        std::abs(BakedGroundAlbedoSum - AlbHash)     > 1.0e-5f;

    BakedBottomRadiusKm  = GroundKm;
    BakedTopRadiusKm     = TopKm;
    BakedStartRadiusKm   = StartRadiusKm;
    BakedRayStrength     = RayMul;
    BakedMieStrength     = MieMul;
    BakedOzoneStrength   = OzoneMul;
    BakedRayScaleH       = RayH;
    BakedMieScaleH       = MieH;
    BakedGroundAlbedoSum = AlbHash;

    if (MediumChanged)
    {
        // Pass 1: 256x64 Bruneton/Hillaire spherical-horizon Transmittance LUT (64 quadratic Fraction^2 steps)
        TransmittanceTexelsRgb.assign(TransmittanceTexelCount * 3u, 1.0f);
        const float HorizonKm = ComputeHorizonDistanceKm(GroundKm, TopKm);
        constexpr uint32_t TransSteps = 64u;
        for (uint32_t Y = 0u; Y < TransmittanceHeightPx; ++Y)
        {
            const float V        = static_cast<float>(Y) / static_cast<float>(TransmittanceHeightPx - 1u);
            const float RhoKm    = V * HorizonKm;
            const float RadiusKm = std::sqrt(RhoKm * RhoKm + GroundKm * GroundKm);
            const float DMinKm   = std::max(0.0f, TopKm - RadiusKm);
            const float DMaxKm   = RhoKm + HorizonKm;
            for (uint32_t X = 0u; X < TransmittanceWidthPx; ++X)
            {
                const float U         = static_cast<float>(X) / static_cast<float>(TransmittanceWidthPx - 1u);
                const float DistTopKm = DMinKm + U * (DMaxKm - DMinKm);
                const float CosPolar  = (DistTopKm <= 1.0e-5f)
                    ? 1.0f
                    : ClampUnit((HorizonKm * HorizonKm - RhoKm * RhoKm - DistTopKm * DistTopKm) /
                                (2.0f * RadiusKm * DistTopKm));

                float OptR = 0.0f, OptG = 0.0f, OptB = 0.0f;
                for (uint32_t S = 0u; S < TransSteps; ++S)
                {
                    const float F0   = static_cast<float>(S) / static_cast<float>(TransSteps);
                    const float F1   = static_cast<float>(S + 1u) / static_cast<float>(TransSteps);
                    const float T0   = DistTopKm * F0 * F0;
                    const float T1   = DistTopKm * F1 * F1;
                    const float T    = 0.5f * (T0 + T1);
                    const float Step = std::max(1.0e-5f, T1 - T0);
                    const float RK   = std::sqrt(std::max(GroundKm * GroundKm, RadiusKm * RadiusKm + 2.0f * RadiusKm * CosPolar * T + T * T));
                    const auto  Coeff = EvaluateMediumAtAltitudeKm(RK - GroundKm, Medium);
                    OptR += Coeff.ExtinctionR * Step;
                    OptG += Coeff.ExtinctionG * Step;
                    OptB += Coeff.ExtinctionB * Step;
                }
                const std::size_t Idx = (static_cast<std::size_t>(Y) * TransmittanceWidthPx + X) * 3u;
                TransmittanceTexelsRgb[Idx + 0u] = std::exp(-OptR);
                TransmittanceTexelsRgb[Idx + 1u] = std::exp(-OptG);
                TransmittanceTexelsRgb[Idx + 2u] = std::exp(-OptB);
            }
        }

        // Pass 2: 32x32 isotropic infinite-order Multi-Scatter LUT (Hillaire 2020 Fibonacci 32 dirs x 24 steps -> L_2nd / (1 - f_ms))
        MultiScatterTexelsRgb.assign(MultiScatterTexelCount * 3u, 0.0f);
        constexpr uint32_t MsDirs  = 32u;
        constexpr uint32_t MsSteps = 24u;
        constexpr float GoldenRatio = 1.6180339887498948482f;
        constexpr float InvFourPi   = 1.0f / (4.0f * Constants::PiFloat);
        const float InvDirs = 1.0f / static_cast<float>(MsDirs);

        for (uint32_t Y = 0u; Y < MultiScatterHeightPx; ++Y)
        {
            const float V       = static_cast<float>(Y) / static_cast<float>(MultiScatterHeightPx - 1u);
            const float AltKm   = std::clamp(V * V * ThicknessKm, 0.01f, std::max(0.02f, ThicknessKm - 0.01f));
            const float RadiusKm = GroundKm + AltKm;

            for (uint32_t X = 0u; X < MultiScatterWidthPx; ++X)
            {
                const float U       = static_cast<float>(X) / static_cast<float>(MultiScatterWidthPx - 1u);
                const float SunCos  = ClampUnit(2.0f * U - 1.0f);
                const float SunSin  = std::sqrt(std::max(0.0f, 1.0f - SunCos * SunCos));

                float Lum2R = 0.0f, Lum2G = 0.0f, Lum2B = 0.0f;
                float FmsR  = 0.0f, FmsG  = 0.0f, FmsB  = 0.0f;

                for (uint32_t D = 0u; D < MsDirs; ++D)
                {
                    const float CosPhi = 1.0f - 2.0f * (static_cast<float>(D) + 0.5f) * InvDirs;
                    const float SinPhi = std::sqrt(std::max(0.0f, 1.0f - CosPhi * CosPhi));
                    const float Theta  = Constants::TauFloat * (static_cast<float>(D) + 0.5f) / GoldenRatio;
                    const float DirX   = SinPhi * std::cos(Theta);
                    const float DirZ   = CosPhi;
                    const float RaySunCos = ClampUnit(DirX * SunSin + DirZ * SunCos);

                    bool HitsGround = false;
                    const float GroundD = IntersectLowerSphereDistanceKm(RadiusKm, DirZ, GroundKm, HitsGround);
                    const float MaxD    = HitsGround ? GroundD : IntersectUpperSphereDistanceKm(RadiusKm, DirZ, TopKm);
                    if (MaxD <= 1.0e-5f)
                    {
                        continue;
                    }
                    const float Step = MaxD / static_cast<float>(MsSteps);

                    float TR = 1.0f, TG = 1.0f, TB = 1.0f;
                    float RayL2R = 0.0f, RayL2G = 0.0f, RayL2B = 0.0f;
                    float RayFmsR = 0.0f, RayFmsG = 0.0f, RayFmsB = 0.0f;

                    for (uint32_t S = 0u; S < MsSteps; ++S)
                    {
                        const float T   = (static_cast<float>(S) + 0.5f) * Step;
                        const float RK  = std::sqrt(std::max(GroundKm * GroundKm, RadiusKm * RadiusKm + 2.0f * RadiusKm * DirZ * T + T * T));
                        const auto  C   = EvaluateMediumAtAltitudeKm(RK - GroundKm, Medium);

                        const float SafeER = std::max(1.0e-7f, C.ExtinctionR);
                        const float SafeEG = std::max(1.0e-7f, C.ExtinctionG);
                        const float SafeEB = std::max(1.0e-7f, C.ExtinctionB);
                        const float SegTR  = std::exp(-SafeER * Step);
                        const float SegTG  = std::exp(-SafeEG * Step);
                        const float SegTB  = std::exp(-SafeEB * Step);
                        const float IntR   = (1.0f - SegTR) / SafeER;
                        const float IntG   = (1.0f - SegTG) / SafeEG;
                        const float IntB   = (1.0f - SegTB) / SafeEB;

                        const float ScatR  = C.RayScatterR + C.MieScatter;
                        const float ScatG  = C.RayScatterG + C.MieScatter;
                        const float ScatB  = C.RayScatterB + C.MieScatter;

                        const float SampleSunCos = ClampUnit((RadiusKm * SunCos + T * RaySunCos) / RK);
                        const auto  SunT = SampleTransmittanceSphericalLut(
                            TransmittanceTexelsRgb, RK, SampleSunCos, GroundKm, TopKm);

                        RayL2R += TR * ScatR * InvFourPi * SunT[0] * IntR;
                        RayL2G += TG * ScatG * InvFourPi * SunT[1] * IntG;
                        RayL2B += TB * ScatB * InvFourPi * SunT[2] * IntB;

                        RayFmsR += TR * ScatR * IntR;
                        RayFmsG += TG * ScatG * IntG;
                        RayFmsB += TB * ScatB * IntB;

                        TR *= SegTR;
                        TG *= SegTG;
                        TB *= SegTB;
                    }

                    if (HitsGround)
                    {
                        const float HitSunCos = ClampUnit((RadiusKm * SunCos + MaxD * RaySunCos) / GroundKm);
                        if (HitSunCos > 0.0f)
                        {
                            const auto GroundSunT = SampleTransmittanceSphericalLut(
                                TransmittanceTexelsRgb, GroundKm, HitSunCos, GroundKm, TopKm);
                            const float DiffFactor = HitSunCos / Constants::PiFloat;
                            RayL2R += TR * Medium.GroundAlbedo.R * GroundSunT[0] * DiffFactor;
                            RayL2G += TG * Medium.GroundAlbedo.G * GroundSunT[1] * DiffFactor;
                            RayL2B += TB * Medium.GroundAlbedo.B * GroundSunT[2] * DiffFactor;
                        }
                    }

                    // 4*pi / MsDirs * (1/(4*pi)) == 1 / MsDirs for isotropic multi-scatter spherical integration
                    Lum2R += RayL2R * InvDirs;
                    Lum2G += RayL2G * InvDirs;
                    Lum2B += RayL2B * InvDirs;
                    FmsR  += RayFmsR * InvDirs;
                    FmsG  += RayFmsG * InvDirs;
                    FmsB  += RayFmsB * InvDirs;
                }

                const std::size_t Idx = (static_cast<std::size_t>(Y) * MultiScatterWidthPx + X) * 3u;
                MultiScatterTexelsRgb[Idx + 0u] = Lum2R / std::max(1.0e-3f, 1.0f - std::min(0.95f, FmsR));
                MultiScatterTexelsRgb[Idx + 1u] = Lum2G / std::max(1.0e-3f, 1.0f - std::min(0.95f, FmsG));
                MultiScatterTexelsRgb[Idx + 2u] = Lum2B / std::max(1.0e-3f, 1.0f - std::min(0.95f, FmsB));
            }
        }
    }

    // 2. Build 192x108 geometric-limb-centered SkyView LUT (Rows 0..53 = upper sky, Rows 54..107 = ground/aerial perspective)
    SkyViewTexelsRgba.assign(SkyViewTexelCount * 4u, 0.0f);
    constexpr uint32_t HalfH      = SkyViewHeightPx / 2u; // 54u
    constexpr uint32_t SkySteps   = 40u;
    constexpr uint32_t GndSteps   = 28u;
    const float LimbAngle   = ComputeLimbPolarAngle(StartRadiusKm, GroundKm);
    const float SunElevRad  = std::asin(ClampUnit(UnitSun.Z));
    const float DayVis      = std::clamp((SunElevRad * Constants::RadToDegFloat + 6.0f) / 14.0f, 0.0f, 1.0f);
    const float SolarScale  = std::max(0.0f, SolarIntensityScale) * 0.24f;
    const float G2          = MieG * MieG;

    for (uint32_t Y = 0u; Y < SkyViewHeightPx; ++Y)
    {
        const bool  LowerBank   = (Y >= HalfH);
        const float LocalV      = LowerBank
            ? static_cast<float>(Y - HalfH) / static_cast<float>(HalfH - 1u)
            : static_cast<float>(Y) / static_cast<float>(HalfH - 1u);
        const float PolarAngle  = LowerBank
            ? std::min(Constants::PiFloat - 1.0e-5f, LimbAngle + (Constants::PiFloat - LimbAngle) * LocalV * LocalV + 1.5e-4f)
            : std::max(0.0f, LimbAngle * (1.0f - LocalV * LocalV) - 1.5e-4f);
        const float CosPolar    = std::cos(PolarAngle);
        const float SinPolar    = std::sqrt(std::max(0.0f, 1.0f - CosPolar * CosPolar));
        const float LimbFactor  = std::exp(-std::max(0.0f, CosPolar) * 7.5f);
        const float GlowScale   = 1.0f + (Medium.LimbGlowIntensity - 1.0f) * LimbFactor * 0.55f;

        bool HitsGround = false;
        const float GroundDistKm = LowerBank ? IntersectLowerSphereDistanceKm(StartRadiusKm, CosPolar, GroundKm, HitsGround) : 0.0f;
        const float MaxDistKm    = (LowerBank && HitsGround)
            ? std::max(0.005f, GroundDistKm)
            : IntersectUpperSphereDistanceKm(StartRadiusKm, CosPolar, TopKm);

        for (uint32_t X = 0u; X < SkyViewWidthPx; ++X)
        {
            const float U       = (static_cast<float>(X) + 0.5f) / static_cast<float>(SkyViewWidthPx);
            const float Azimuth = (U - 0.5f) * Constants::TauFloat;
            const float RdX     = SinPolar * std::cos(Azimuth);
            const float RdY     = SinPolar * std::sin(Azimuth);
            const float RdZ     = CosPolar;

            const float CosTheta = RdX * UnitSun.X + RdY * UnitSun.Y + RdZ * UnitSun.Z;
            const float PhaseR   = (3.0f / (16.0f * Constants::PiFloat)) * (1.0f + CosTheta * CosTheta);
            const float Denom    = std::max(1.0e-3f, 1.0f + G2 - 2.0f * MieG * CosTheta);
            const float PhaseM   = (3.0f / (8.0f * Constants::PiFloat)) * ((1.0f - G2) * (1.0f + CosTheta * CosTheta)) /
                                   ((2.0f + G2) * std::pow(Denom, 1.5f));

            float TR = 1.0f, TG = 1.0f, TB = 1.0f;
            float RadR = 0.0f, RadG = 0.0f, RadB = 0.0f;

            const uint32_t Steps = LowerBank ? GndSteps : SkySteps;
            for (uint32_t S = 0u; S < Steps; ++S)
            {
                const float F0   = static_cast<float>(S) / static_cast<float>(Steps);
                const float F1   = static_cast<float>(S + 1u) / static_cast<float>(Steps);
                const float T0   = MaxDistKm * F0 * F0;
                const float T1   = MaxDistKm * F1 * F1;
                const float T    = 0.5f * (T0 + T1);
                const float Step = std::max(1.0e-5f, T1 - T0);

                const float RK    = std::sqrt(std::max(GroundKm * GroundKm, StartRadiusKm * StartRadiusKm + 2.0f * StartRadiusKm * RdZ * T + T * T));
                const float AltKm = std::max(0.0f, RK - GroundKm);
                const auto  C     = EvaluateMediumAtAltitudeKm(AltKm, Medium);

                const float SafeER = std::max(1.0e-7f, C.ExtinctionR);
                const float SafeEG = std::max(1.0e-7f, C.ExtinctionG);
                const float SafeEB = std::max(1.0e-7f, C.ExtinctionB);
                const float SegTR  = std::exp(-SafeER * Step);
                const float SegTG  = std::exp(-SafeEG * Step);
                const float SegTB  = std::exp(-SafeEB * Step);
                const float IntR   = (1.0f - SegTR) / SafeER;
                const float IntG   = (1.0f - SegTG) / SafeEG;
                const float IntB   = (1.0f - SegTB) / SafeEB;

                const float SampleSunCos = ClampUnit((StartRadiusKm * UnitSun.Z + T * CosTheta) / RK);
                const auto  SunT = SampleTransmittanceSphericalLut(
                    TransmittanceTexelsRgb, RK, SampleSunCos, GroundKm, TopKm);
                const auto  Multi = SampleMultiScatterGridLut(
                    MultiScatterTexelsRgb, AltKm, SampleSunCos, ThicknessKm);

                const float ScR = C.RayScatterR * PhaseR + C.MieScatter * PhaseM;
                const float ScG = C.RayScatterG * PhaseR + C.MieScatter * PhaseM;
                const float ScB = C.RayScatterB * PhaseR + C.MieScatter * PhaseM;
                const float MsR = C.RayScatterR + C.MieScatter;
                const float MsG = C.RayScatterG + C.MieScatter;
                const float MsB = C.RayScatterB + C.MieScatter;

                RadR += TR * (ScR * SunT[0] + MsR * Multi[0]) * IntR;
                RadG += TG * (ScG * SunT[1] + MsG * Multi[1]) * IntG;
                RadB += TB * (ScB * SunT[2] + MsB * Multi[2]) * IntB;

                TR *= SegTR;
                TG *= SegTG;
                TB *= SegTB;
            }

            if (LowerBank && HitsGround)
            {
                const float HitSunCos = ClampUnit((StartRadiusKm * UnitSun.Z + MaxDistKm * CosTheta) / GroundKm);
                const auto  GroundSunT = (HitSunCos > 0.0f)
                    ? SampleTransmittanceSphericalLut(TransmittanceTexelsRgb, GroundKm, HitSunCos, GroundKm, TopKm)
                    : std::array<float, 3>{ 0.0f, 0.0f, 0.0f };
                const auto GroundMulti = SampleMultiScatterGridLut(
                    MultiScatterTexelsRgb, 0.0f, HitSunCos, ThicknessKm);
                const float SunTerm = std::max(0.0f, HitSunCos) / Constants::PiFloat;
                const float GndR    = Medium.GroundAlbedo.R * (GroundSunT[0] * SunTerm + GroundMulti[0] * 0.70f);
                const float GndG    = Medium.GroundAlbedo.G * (GroundSunT[1] * SunTerm + GroundMulti[1] * 0.70f);
                const float GndB    = Medium.GroundAlbedo.B * (GroundSunT[2] * SunTerm + GroundMulti[2] * 0.70f);
                RadR += TR * GndR;
                RadG += TG * GndG;
                RadB += TB * GndB;
            }

            const float NightR = 0.0007f + 0.0008f * LimbFactor;
            const float NightG = 0.0010f + 0.0011f * LimbFactor;
            const float NightB = 0.0022f + 0.0020f * LimbFactor;

            float OutR = (RadR * SolarScale * GlowScale + NightR * (1.0f - DayVis * 0.9f)) * Medium.SkyTint.R * Medium.SkyBrightnessMultiplier;
            float OutG = (RadG * SolarScale * GlowScale + NightG * (1.0f - DayVis * 0.9f)) * Medium.SkyTint.G * Medium.SkyBrightnessMultiplier;
            float OutB = (RadB * SolarScale * GlowScale + NightB * (1.0f - DayVis * 0.9f)) * Medium.SkyTint.B * Medium.SkyBrightnessMultiplier;

            if (LowerBank)
            {
                // True grazing Schlick Fresnel reflection near the lower limb blended cleanly against upper-limb row 0
                const std::size_t LimbIdx = static_cast<std::size_t>(X) * 4u;
                const float Grazing = std::pow(1.0f - std::clamp(-CosPolar, 0.0f, 1.0f), 5.0f);
                const float Fresnel = 0.02f + 0.98f * Grazing;
                OutR = OutR * (1.0f - Fresnel) + SkyViewTexelsRgba[LimbIdx + 0u] * Fresnel;
                OutG = OutG * (1.0f - Fresnel) + SkyViewTexelsRgba[LimbIdx + 1u] * Fresnel;
                OutB = OutB * (1.0f - Fresnel) + SkyViewTexelsRgba[LimbIdx + 2u] * Fresnel;
            }

            const std::size_t Idx = (static_cast<std::size_t>(Y) * SkyViewWidthPx + X) * 4u;
            SkyViewTexelsRgba[Idx + 0u] = std::max(0.0f, OutR);
            SkyViewTexelsRgba[Idx + 1u] = std::max(0.0f, OutG);
            SkyViewTexelsRgba[Idx + 2u] = std::max(0.0f, OutB);
            SkyViewTexelsRgba[Idx + 3u] = 1.0f;
        }
    }
}

Mathematics::LinearRGB AtmosphericSkySequence::SampleTransmittanceLutDirection(
    const AtmosphericMediumConfiguration& Medium,
    float                                 AltitudeMeters,
    float                                 PolarCosine) const noexcept
{
    if (!QueryFoundationLutsReady())
    {
        return ComputeSolarTransmittance(
            { std::sqrt(std::max(0.0f, 1.0f - PolarCosine * PolarCosine)), 0.0f, PolarCosine },
            Medium,
            AltitudeMeters);
    }
    const float RadiusKm = BakedBottomRadiusKm + std::max(0.0f, AltitudeMeters * 0.001f);
    const auto  Sample   = SampleTransmittanceSphericalLut(
        TransmittanceTexelsRgb, RadiusKm, PolarCosine, BakedBottomRadiusKm, BakedTopRadiusKm);
    return { Sample[0], Sample[1], Sample[2] };
}

Mathematics::LinearRGB AtmosphericSkySequence::SampleMultiScatterLut(
    const AtmosphericMediumConfiguration& Medium,
    float                                 AltitudeMeters,
    float                                 SolarCosine) const noexcept
{
    if (!QueryFoundationLutsReady())
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    const float ThicknessKm = std::max(10.0f, Medium.AtmosphereThicknessMeters * 0.001f);
    const auto  Sample      = SampleMultiScatterGridLut(
        MultiScatterTexelsRgb, std::max(0.0f, AltitudeMeters * 0.001f), SolarCosine, ThicknessKm);
    return { Sample[0], Sample[1], Sample[2] };
}

Mathematics::LinearRGB AtmosphericSkySequence::SampleSkyViewDirection(
    const Mathematics::Axis3D& WorldDirection) const noexcept
{
    if (!QueryLutReady())
    {
        return { 0.18f, 0.31f, 0.54f };
    }

    constexpr int32_t W     = static_cast<int32_t>(SkyViewWidthPx);
    constexpr int32_t HalfH = static_cast<int32_t>(SkyViewHeightPx / 2u);

    const float Azimuth    = std::atan2(WorldDirection.Y, WorldDirection.X);
    const float PolarAngle = std::acos(ClampUnit(WorldDirection.Z));
    const float LimbAngle  = ComputeLimbPolarAngle(BakedStartRadiusKm, BakedBottomRadiusKm);

    float U = Azimuth / Constants::TauFloat + 0.5f;
    U = U - std::floor(U);

    const bool LowerBank = (PolarAngle > LimbAngle);
    float CoordY = 0.0f;
    if (!LowerBank)
    {
        const float Q = std::sqrt(std::clamp(1.0f - PolarAngle / std::max(1.0e-4f, LimbAngle), 0.0f, 1.0f));
        CoordY = Q * static_cast<float>(HalfH - 1);
    }
    else
    {
        const float Q = std::sqrt(std::clamp((PolarAngle - LimbAngle) / std::max(1.0e-4f, Constants::PiFloat - LimbAngle), 0.0f, 1.0f));
        CoordY = static_cast<float>(HalfH) + Q * static_cast<float>(HalfH - 1);
    }

    const int32_t BankMin = LowerBank ? HalfH : 0;
    const int32_t BankMax = LowerBank ? (HalfH * 2 - 1) : (HalfH - 1);

    const float FX = U * static_cast<float>(W) - 0.5f;
    const int32_t IX0 = ((static_cast<int32_t>(std::floor(FX)) % W) + W) % W;
    const int32_t IX1 = (IX0 + 1) % W;
    const float TX = FX - std::floor(FX);
    const float SX = TX * TX * (3.0f - 2.0f * TX);

    const int32_t IY1 = std::clamp(static_cast<int32_t>(std::floor(CoordY)), BankMin, BankMax);
    const int32_t IY0 = std::clamp(IY1 - 1, BankMin, BankMax);
    const int32_t IY2 = std::clamp(IY1 + 1, BankMin, BankMax);
    const int32_t IY3 = std::clamp(IY1 + 2, BankMin, BankMax);
    const float TY    = std::clamp(CoordY - static_cast<float>(IY1), 0.0f, 1.0f);

    const auto Fetch = [&](int32_t X, int32_t Y) noexcept -> Mathematics::LinearRGB
    {
        const std::size_t Idx = (static_cast<std::size_t>(Y) * SkyViewWidthPx + static_cast<std::size_t>(X)) * 4u;
        return { SkyViewTexelsRgba[Idx + 0u], SkyViewTexelsRgba[Idx + 1u], SkyViewTexelsRgba[Idx + 2u] };
    };
    const auto MixC = [](const Mathematics::LinearRGB& A, const Mathematics::LinearRGB& B, float T) noexcept -> Mathematics::LinearRGB
    {
        return { A.R + (B.R - A.R) * T, A.G + (B.G - A.G) * T, A.B + (B.B - A.B) * T };
    };
    const auto CatmullRom = [](float P0, float P1, float P2, float P3, float T) noexcept -> float
    {
        const float T2 = T * T;
        const float T3 = T2 * T;
        return 0.5f * ((2.0f * P1) +
                       (-P0 + P2) * T +
                       (2.0f * P0 - 5.0f * P1 + 4.0f * P2 - P3) * T2 +
                       (-P0 + 3.0f * P1 - 3.0f * P2 + P3) * T3);
    };
    const auto Row0 = MixC(Fetch(IX0, IY0), Fetch(IX1, IY0), SX);
    const auto Row1 = MixC(Fetch(IX0, IY1), Fetch(IX1, IY1), SX);
    const auto Row2 = MixC(Fetch(IX0, IY2), Fetch(IX1, IY2), SX);
    const auto Row3 = MixC(Fetch(IX0, IY3), Fetch(IX1, IY3), SX);
    return {
        std::max(0.0f, CatmullRom(Row0.R, Row1.R, Row2.R, Row3.R, TY)),
        std::max(0.0f, CatmullRom(Row0.G, Row1.G, Row2.G, Row3.G, TY)),
        std::max(0.0f, CatmullRom(Row0.B, Row1.B, Row2.B, Row3.B, TY)),
    };
}

float AtmosphericSkySequence::EvaluateSolarErf(float Argument) noexcept
{
    const float Variable = 1.0f / (1.0f + 0.3275911f * std::abs(Argument));
    const float Polynomial =
        (((((1.061405429f * Variable - 1.453152027f) * Variable + 1.421413741f) * Variable
            - 0.284496736f) * Variable + 0.254829592f) * Variable);
    const float Result = 1.0f - Polynomial * std::exp(-Argument * Argument);
    return (Argument < 0.0f) ? -Result : Result;
}

Mathematics::LinearRGB AtmosphericSkySequence::EvaluateNeckelLabsLimbDarkening(float MuPhotosphere) noexcept
{
    const float Mu  = std::clamp(MuPhotosphere, 0.0f, 1.0f);
    const float Mu2 = Mu * Mu;
    const float Mu3 = Mu2 * Mu;
    const float Mu4 = Mu2 * Mu2;
    const float Mu5 = Mu4 * Mu;

    // Exact published Neckel & Labs (1994, Solar Physics 153) polynomial coefficients at 650 nm (R), 550 nm (G), 450 nm (B)
    const float LimbRed   = 0.34685f + 1.37539f * Mu - 2.04425f * Mu2 + 2.70493f * Mu3 - 1.94290f * Mu4 + 0.55999f * Mu5;
    const float LimbGreen = 0.23698f + 1.37916f * Mu - 1.83226f * Mu2 + 2.34540f * Mu3 - 1.65319f * Mu4 + 0.52391f * Mu5;
    const float LimbBlue  = 0.11903f + 1.24089f * Mu - 1.19977f * Mu2 + 1.33936f * Mu3 - 0.98690f * Mu4 + 0.48739f * Mu5;

    return {
        std::clamp(LimbRed,   0.0f, 1.05f),
        std::clamp(LimbGreen, 0.0f, 1.05f),
        std::clamp(LimbBlue,  0.0f, 1.05f),
    };
}

float AtmosphericSkySequence::EvaluateSolarGlareSpread(
    float SeparationRadians,
    float DiscRadiusRadians) noexcept
{
    constexpr float SolarGlareReachRadians   = 30.0f * Constants::DegToRadFloat;
    constexpr float SolarGlareBloomWeight    = 1.50f;
    constexpr float SolarGlareAureoleWeight  = 0.25f;
    constexpr float SolarGlareAureoleDegrees = 6.0f;

    if (SeparationRadians >= SolarGlareReachRadians)
    {
        return 0.0f;
    }

    const float SepDegrees   = SeparationRadians * Constants::RadToDegFloat;
    const float DiscDegrees  = DiscRadiusRadians * Constants::RadToDegFloat;
    const float Effective    = std::sqrt(SepDegrees * SepDegrees + DiscDegrees * DiscDegrees);
    const float Window       = 1.0f - SeparationRadians / SolarGlareReachRadians;
    const float Bloom        = SolarGlareBloomWeight / ((Effective + 0.02f) * (Effective + 0.02f));
    const float AureoleRatio = Effective / SolarGlareAureoleDegrees;
    const float Aureole      = SolarGlareAureoleWeight / (1.0f + AureoleRatio * AureoleRatio);
    return (Bloom + Aureole) * Window * Window;
}

Mathematics::LinearRGB AtmosphericSkySequence::EvaluateSolarCoronaRadiance(
    const Mathematics::Axis3D&            RayDirection,
    const Mathematics::Axis3D&            SunDirection,
    const SolarCelestialConfiguration&    SolarConfig,
    const Mathematics::LinearRGB&         SolarTransmittance,
    const Mathematics::LinearRGB&         EffectiveSolarTint,
    bool                                  AllowBelowHorizon,
    const AtmosphericMediumConfiguration* OptionalMedium,
    float                                 PixelAngularSizeRad) noexcept
{
    if (!SolarConfig.SolarEnabled || SolarConfig.SolarIntensityMultiplier <= 0.0f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const Mathematics::Axis3D UnitRay = Mathematics::NormalizeAxis(RayDirection);
    const Mathematics::Axis3D UnitSun = Mathematics::NormalizeAxis(SunDirection);
    const float HalfChord  = 0.5f * Mathematics::EvaluateLength(UnitRay - UnitSun);
    const float Separation = 2.0f * std::asin(std::clamp(HalfChord, 0.0f, 1.0f));
    const float HalfRad    = std::max(0.0008f, SolarConfig.AngularDiameterDegrees * 0.5f * Constants::DegToRadFloat);

    const float SkyBrightness  = (OptionalMedium != nullptr) ? std::max(0.0f, OptionalMedium->SkyBrightnessMultiplier) : 1.0f;
    const float DiscIntensity  = std::max(0.0f, SolarConfig.SolarIntensityMultiplier) * SkyBrightness * 2.1f;
    if (DiscIntensity <= 0.0f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const bool OrbitalView = AllowBelowHorizon ||
        (OptionalMedium != nullptr &&
            (OptionalMedium->OrbitalViewEnabled || OptionalMedium->ObserverAltitudeMeters > OptionalMedium->AtmosphereThicknessMeters));
    const float PlanetR = (OptionalMedium != nullptr) ? std::max(500000.0f, OptionalMedium->PlanetRadiusMeters) : 6360000.0f;
    const float ObsAlt  = (OptionalMedium != nullptr) ? std::max(5.0f, OptionalMedium->ObserverAltitudeMeters) : 100.0f;
    const float ViewerR = PlanetR + ObsAlt;

    auto QueryGroundBlocked = [ViewerR, PlanetR](float DirZ) noexcept -> bool
    {
        if (DirZ >= 0.0f)
        {
            return false;
        }
        const float Disc = ViewerR * ViewerR * (DirZ * DirZ - 1.0f) + PlanetR * PlanetR;
        return Disc >= 0.0f;
    };

    const float PixelSizeRad   = std::max(0.0f, PixelAngularSizeRad);
    const float MaxDiscCoronaR = std::max(HalfRad * 48.0f, PixelSizeRad * 8.0f);

    Mathematics::LinearRGB OutRadiance { 0.0f, 0.0f, 0.0f };
    if (Separation <= MaxDiscCoronaR)
    {
        const bool RayGroundBlocked = (!OrbitalView) && QueryGroundBlocked(UnitRay.Z);
        if (!RayGroundBlocked)
        {
            const Mathematics::LinearRGB LineTransmittance = (!OrbitalView && OptionalMedium != nullptr)
                ? ComputeSolarTransmittance(UnitRay, *OptionalMedium, ObsAlt)
                : SolarTransmittance;

            if (LineTransmittance.R + LineTransmittance.G + LineTransmittance.B > 1.0e-6f)
            {
                const float NormalizedRadius = Separation / HalfRad;

                float SmallWeight = 0.0f;
                if (PixelSizeRad > 1.0e-6f)
                {
                    const float RadiusPixels = HalfRad / PixelSizeRad;
                    const float Blend        = std::clamp((6.0f - RadiusPixels) / 3.0f, 0.0f, 1.0f);
                    SmallWeight              = Blend * Blend * (3.0f - 2.0f * Blend);
                }

                Mathematics::LinearRGB ResolvedPhotosphere { 0.0f, 0.0f, 0.0f };
                if (SmallWeight < 1.0f && NormalizedRadius < 1.32f)
                {
                    const float ClampedR = std::min(1.0f, NormalizedRadius);
                    const float MuSun    = std::sqrt(std::max(0.0f, 1.0f - ClampedR * ClampedR));
                    const auto  LimbDark = EvaluateNeckelLabsLimbDarkening(MuSun);
                    const float EdgeErfc = 0.5f * (1.0f - EvaluateSolarErf((NormalizedRadius - 0.997f) / 0.012f));
                    const float PhotoMag = EdgeErfc * DiscIntensity;
                    ResolvedPhotosphere = {
                        LimbDark.R * PhotoMag,
                        LimbDark.G * PhotoMag,
                        LimbDark.B * PhotoMag,
                    };
                }

                Mathematics::LinearRGB SubPixelPhotosphere { 0.0f, 0.0f, 0.0f };
                if (SmallWeight > 0.0f && Mathematics::EvaluateDot(UnitRay, UnitSun) > 0.0f)
                {
                    const Mathematics::Axis3D RefAxis = (std::abs(UnitSun.Z) < 0.98f)
                        ? Mathematics::Axis3D{ 0.0f, 0.0f, 1.0f }
                        : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
                    const Mathematics::Axis3D TangentT = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(RefAxis, UnitSun));
                    const Mathematics::Axis3D TangentB = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(UnitSun, TangentT));
                    const float DeltaX        = Mathematics::EvaluateDot(UnitRay, TangentT);
                    const float DeltaY        = Mathematics::EvaluateDot(UnitRay, TangentB);
                    const float SmallSigma    = std::sqrt(0.25f * HalfRad * HalfRad + 0.20f * PixelSizeRad * PixelSizeRad);
                    const float InvSqrt2Sigma = 1.0f / (1.4142135623730951f * std::max(1.0e-6f, SmallSigma));
                    const float LowX          = (DeltaX - 0.5f * PixelSizeRad) * InvSqrt2Sigma;
                    const float HighX         = (DeltaX + 0.5f * PixelSizeRad) * InvSqrt2Sigma;
                    const float LowY          = (DeltaY - 0.5f * PixelSizeRad) * InvSqrt2Sigma;
                    const float HighY         = (DeltaY + 0.5f * PixelSizeRad) * InvSqrt2Sigma;
                    const float XMass         = std::max(0.0f, 0.5f * (EvaluateSolarErf(HighX) - EvaluateSolarErf(LowX)));
                    const float YMass         = std::max(0.0f, 0.5f * (EvaluateSolarErf(HighY) - EvaluateSolarErf(LowY)));
                    const float SinHalf       = std::sin(0.5f * HalfRad);
                    const float SolidAngle    = 4.0f * Constants::PiFloat * SinHalf * SinHalf;
                    const float PixelArea     = std::max(1.0e-12f, PixelSizeRad * PixelSizeRad);
                    const float SubGain       = (SolidAngle / PixelArea) * XMass * YMass * DiscIntensity;
                    // Exact disk-integrated Neckel & Labs (1994) * erfc PSF factors: int_0^inf 2*r*Ld(mu(r))*erfc_psf(r) dr = (0.83276, 0.77447, 0.69061)
                    SubPixelPhotosphere = {
                        0.83276f * SubGain,
                        0.77447f * SubGain,
                        0.69061f * SubGain,
                    };
                }

                const Mathematics::LinearRGB Photosphere {
                    ResolvedPhotosphere.R + (SubPixelPhotosphere.R - ResolvedPhotosphere.R) * SmallWeight,
                    ResolvedPhotosphere.G + (SubPixelPhotosphere.G - ResolvedPhotosphere.G) * SmallWeight,
                    ResolvedPhotosphere.B + (SubPixelPhotosphere.B - ResolvedPhotosphere.B) * SmallWeight,
                };

                Mathematics::LinearRGB CoronaColour { 0.0f, 0.0f, 0.0f };
                const float CoronaBlurAngle = 0.30f * PixelSizeRad * SmallWeight;
                const float CoronaNormR     = std::sqrt(Separation * Separation + CoronaBlurAngle * CoronaBlurAngle) / HalfRad;
                if (CoronaNormR < 48.0f)
                {
                    const float R1         = CoronaNormR / 1.18f;
                    const float R2         = CoronaNormR / 3.40f;
                    const float R3         = CoronaNormR / 11.5f;
                    const float InnerTerm  = 1.0f / std::pow(1.0f + R1 * R1, 1.85f);
                    const float MiddleTerm = 1.0f / std::pow(1.0f + R2 * R2, 1.38f);
                    const float OuterTerm  = 1.0f / std::pow(1.0f + R3 * R3, 1.22f);

                    const float ScatterFactor = OrbitalView
                        ? 0.38f
                        : std::clamp(0.55f + 0.85f * (1.0f - LineTransmittance.B), 0.55f, 1.35f);
                    const float CoronaProfile = DiscIntensity * (0.042f * InnerTerm + 0.0085f * ScatterFactor * MiddleTerm + 0.0014f * ScatterFactor * OuterTerm);
                    const float WindowFade    = std::clamp(1.0f - CoronaNormR / 48.0f, 0.0f, 1.0f);
                    const float SmoothWindow  = WindowFade * WindowFade;
                    CoronaColour = {
                        CoronaProfile * SmoothWindow * 1.04f,
                        CoronaProfile * SmoothWindow * 0.99f,
                        CoronaProfile * SmoothWindow * 0.91f,
                    };
                }

                OutRadiance.R += (Photosphere.R + CoronaColour.R) * LineTransmittance.R * EffectiveSolarTint.R;
                OutRadiance.G += (Photosphere.G + CoronaColour.G) * LineTransmittance.G * EffectiveSolarTint.G;
                OutRadiance.B += (Photosphere.B + CoronaColour.B) * LineTransmittance.B * EffectiveSolarTint.B;
            }
        }
    }

    // Optical glare veil (Radiant vs Muted): bounded 30 deg spread tinted by solar-axis transmittance
    if (SolarConfig.AppearanceMode == SolarAppearanceMode::Radiant && SolarConfig.SunGlareStrength > 0.0f)
    {
        const float Spread = EvaluateSolarGlareSpread(Separation, HalfRad);
        if (Spread > 0.0f)
        {
            const bool SunAxisBlocked = (!OrbitalView) && QueryGroundBlocked(UnitSun.Z);
            if (!SunAxisBlocked)
            {
                const float GlareScale = std::max(0.0f, SolarConfig.SunGlareStrength) * Spread * (0.045f * DiscIntensity);
                OutRadiance.R += EffectiveSolarTint.R * SolarTransmittance.R * GlareScale * 1.02f;
                OutRadiance.G += EffectiveSolarTint.G * SolarTransmittance.G * GlareScale * 1.00f;
                OutRadiance.B += EffectiveSolarTint.B * SolarTransmittance.B * GlareScale * 0.96f;
            }
        }
    }

    return OutRadiance;
}

Mathematics::LinearRGB AtmosphericSkySequence::EvaluateOrbitalSpaceRadiance(
    const Mathematics::Axis3D&                               CameraLocalOriginMeters,
    const Mathematics::Axis3D&                               RayDirection,
    const Mathematics::Axis3D&                               SunDirection,
    const SolarCelestialConfiguration&                       SolarConfig,
    const AtmosphericMediumConfiguration&                    Medium,
    bool                                                     IncludeOrbitalClouds,
    float                                                    CloudCoverage01,
    float                                                    SimulationSeconds,
    const SimulationSpace::ForceFields::ForceFieldAccumulator* OptionalForceFields) noexcept
{
    const float PlanetR  = std::max(500000.0f, Medium.PlanetRadiusMeters);
    const float AtmoTop  = std::max(15000.0f, Medium.AtmosphereThicknessMeters);
    const float AtmoR    = PlanetR + AtmoTop;
    const float RayMul   = std::max(0.0f, Medium.RayleighStrength);
    const float MieMul   = std::max(0.0f, Medium.MieStrength);
    const float MieG     = std::clamp(Medium.MieAnisotropy, 0.0f, 0.99f);
    const float OzoneMul = std::max(0.0f, Medium.OzoneStrength);
    const float RayH     = std::max(500.0f, Medium.RayleighScaleHeightMeters);
    const float MieH     = std::max(200.0f, Medium.MieScaleHeightMeters);

    // Resolve planet-centered camera position seamlessly whether passed in orbital space or scene + altitude
    const float CamMag = std::sqrt(CameraLocalOriginMeters.X * CameraLocalOriginMeters.X +
                                   CameraLocalOriginMeters.Y * CameraLocalOriginMeters.Y +
                                   CameraLocalOriginMeters.Z * CameraLocalOriginMeters.Z);
    const bool AlreadyPlanetary = (CamMag > PlanetR * 0.5f);
    const float CamX = AlreadyPlanetary ? CameraLocalOriginMeters.X : CameraLocalOriginMeters.X;
    const float CamY = AlreadyPlanetary ? CameraLocalOriginMeters.Y : CameraLocalOriginMeters.Y;
    const float CamZ = AlreadyPlanetary
        ? CameraLocalOriginMeters.Z
        : (PlanetR + std::max(100.0f, Medium.ObserverAltitudeMeters) + CameraLocalOriginMeters.Z);

    const auto [ANear, AFar] = IntersectVecSphere(CamX, CamY, CamZ, RayDirection.X, RayDirection.Y, RayDirection.Z, AtmoR);
    const auto [PNear, _PF]  = IntersectVecSphere(CamX, CamY, CamZ, RayDirection.X, RayDirection.Y, RayDirection.Z, PlanetR);

    const bool HitsPlanet = (PNear > 0.0f);
    const float TStart    = std::max(0.0f, ANear);
    const float TEnd      = HitsPlanet ? PNear : AFar;

    // 1. Evaluate unoccluded solar disc/corona in space when ray misses planet
    Mathematics::LinearRGB SpaceSun { 0.0f, 0.0f, 0.0f };
    if (!HitsPlanet && SolarConfig.SolarEnabled)
    {
        const auto SpaceTrans = (TEnd > TStart)
            ? IntegrateRayTransmittance(
                  CamX + RayDirection.X * TStart,
                  CamY + RayDirection.Y * TStart,
                  CamZ + RayDirection.Z * TStart,
                  RayDirection.X, RayDirection.Y, RayDirection.Z,
                  TEnd - TStart, PlanetR, RayMul, MieMul, OzoneMul, RayH, MieH, 16u)
            : std::array<float, 3>{ 1.0f, 1.0f, 1.0f };
        const auto SolvedSun = CelestialEphemerisSolver::SolveSolarEphemeris(SolarConfig);
        SpaceSun = EvaluateSolarCoronaRadiance(
            RayDirection,
            SunDirection,
            SolarConfig,
            { SpaceTrans[0], SpaceTrans[1], SpaceTrans[2] },
            SolvedSun.EffectiveSolarTint,
            true,
            &Medium,
            0.0f);
    }

    if (TEnd <= TStart)
    {
        return SpaceSun;
    }

    // 2. Ray-march Rayleigh + Mie + Ozone scattering through curved planetary shell
    const float CosTheta = RayDirection.X * SunDirection.X + RayDirection.Y * SunDirection.Y + RayDirection.Z * SunDirection.Z;
    const float PhaseR   = (3.0f / (16.0f * Constants::PiFloat)) * (1.0f + CosTheta * CosTheta);
    const float G2       = MieG * MieG;
    const float Denom    = std::max(1.0e-3f, 1.0f + G2 - 2.0f * MieG * CosTheta);
    const float PhaseM   = (3.0f / (8.0f * Constants::PiFloat)) * ((1.0f - G2) * (1.0f + CosTheta * CosTheta)) /
                           ((2.0f + G2) * std::pow(Denom, 1.5f));
    const float SolarGain = std::max(0.0f, SolarConfig.SolarIntensityMultiplier) * 0.30f * Medium.SkyBrightnessMultiplier;

    float TR = 1.0f, TG = 1.0f, TB = 1.0f;
    float RadR = 0.0f, RadG = 0.0f, RadB = 0.0f;
    constexpr uint32_t AtmoSteps = 20u;
    const float Step = (TEnd - TStart) / static_cast<float>(AtmoSteps);

    for (uint32_t S = 0u; S < AtmoSteps; ++S)
    {
        const float T   = TStart + (static_cast<float>(S) + 0.5f) * Step;
        const float PX  = CamX + RayDirection.X * T;
        const float PY  = CamY + RayDirection.Y * T;
        const float PZ  = CamZ + RayDirection.Z * T;
        const float R   = std::sqrt(PX * PX + PY * PY + PZ * PZ);
        const float Alt = std::max(0.0f, R - PlanetR);

        const float DenR = std::exp(-Alt / RayH) * RayMul;
        const float DenM = std::exp(-Alt / MieH) * MieMul;
        const float DenO = std::max(0.0f, 1.0f - std::abs(Alt - 25000.0f) / 15000.0f) * OzoneMul;

        const float SRayR = SeaLevelRayleighScatter[0] * DenR;
        const float SRayG = SeaLevelRayleighScatter[1] * DenR;
        const float SRayB = SeaLevelRayleighScatter[2] * DenR;
        const float SMie  = SeaLevelMieScatter * DenM;
        const float EMie  = SeaLevelMieExtinction * DenM;

        const float ExtR = std::max(1.0e-7f, SRayR + EMie + PeakOzoneAbs[0] * DenO);
        const float ExtG = std::max(1.0e-7f, SRayG + EMie + PeakOzoneAbs[1] * DenO);
        const float ExtB = std::max(1.0e-7f, SRayB + EMie + PeakOzoneAbs[2] * DenO);

        const float SegTR = std::exp(-ExtR * Step);
        const float SegTG = std::exp(-ExtG * Step);
        const float SegTB = std::exp(-ExtB * Step);

        const float IntR = (1.0f - SegTR) / ExtR;
        const float IntG = (1.0f - SegTG) / ExtG;
        const float IntB = (1.0f - SegTB) / ExtB;

        const auto [EarthHit, _S] = IntersectVecSphere(PX, PY, PZ, SunDirection.X, SunDirection.Y, SunDirection.Z, PlanetR);
        std::array<float, 3> SunT { 0.0f, 0.0f, 0.0f };
        if (EarthHit <= 0.0f)
        {
            const auto [_N, ExitS] = IntersectVecSphere(PX, PY, PZ, SunDirection.X, SunDirection.Y, SunDirection.Z, AtmoR);
            if (ExitS > 0.0f)
            {
                SunT = IntegrateRayTransmittance(
                    PX, PY, PZ, SunDirection.X, SunDirection.Y, SunDirection.Z,
                    std::min(ExitS, 380000.0f), PlanetR, RayMul, MieMul, OzoneMul, RayH, MieH, 8u);
            }
        }

        const float ScR = SRayR * PhaseR + SMie * PhaseM;
        const float ScG = SRayG * PhaseR + SMie * PhaseM;
        const float ScB = SRayB * PhaseR + SMie * PhaseM;
        RadR += TR * ScR * SunT[0] * IntR;
        RadG += TG * ScG * SunT[1] * IntG;
        RadB += TB * ScB * SunT[2] * IntB;

        TR *= SegTR;
        TG *= SegTG;
        TB *= SegTB;
    }

    float OutR = RadR * SolarGain * Medium.SkyTint.R + SpaceSun.R * TR;
    float OutG = RadG * SolarGain * Medium.SkyTint.G + SpaceSun.G * TG;
    float OutB = RadB * SolarGain * Medium.SkyTint.B + SpaceSun.B * TB;

    if (!HitsPlanet)
    {
        return { std::max(0.0f, OutR), std::max(0.0f, OutG), std::max(0.0f, OutB) };
    }

    // 3. Shade planetary continents, polar caps, Fresnel ocean glint, night city lights & ForceField hurricane cloud shell
    const float HX = CamX + RayDirection.X * PNear;
    const float HY = CamY + RayDirection.Y * PNear;
    const float HZ = CamZ + RayDirection.Z * PNear;
    const float InvR = 1.0f / std::max(1.0f, std::sqrt(HX * HX + HY * HY + HZ * HZ));
    const float NX = HX * InvR;
    const float NY = HY * InvR;
    const float NZ = HZ * InvR;

    const float NdotL_raw = NX * SunDirection.X + NY * SunDirection.Y + NZ * SunDirection.Z;
    const float NdotL     = std::max(0.0f, NdotL_raw);
    const float LatAbs    = std::abs(NZ);

    // Multi-octave continents + coastal shelf + mountain relief
    const float ContNoise = FbmOrbit3(NX * 3.1f + 4.1f, NY * 3.1f - 1.8f, NZ * 3.1f + 0.9f, 6u);
    const float LandMask  = std::clamp((ContNoise - 0.495f) / 0.035f, 0.0f, 1.0f);
    const float Shelf     = std::clamp((ContNoise - 0.435f) / 0.065f, 0.0f, 1.0f) * (1.0f - LandMask);
    const float PolarIce  = std::clamp((LatAbs - (0.83f + 0.06f * ContNoise)) / 0.09f, 0.0f, 1.0f);

    float AlbR = 0.012f + 0.018f * Shelf;
    float AlbG = 0.042f + 0.058f * Shelf;
    float AlbB = 0.135f + 0.055f * Shelf; // Deep ocean + turquoise coastal shelf
    if (LandMask > 0.0f)
    {
        const float Moisture = FbmOrbit3(NX * 7.5f - 2.3f, NY * 7.5f + 5.1f, NZ * 7.5f, 4u);
        const float Elev     = std::clamp((ContNoise - 0.50f) / 0.25f, 0.0f, 1.0f);
        const float ForestR = 0.055f, ForestG = 0.145f, ForestB = 0.048f;
        const float DesertR = 0.350f, DesertG = 0.265f, DesertB = 0.165f;
        const float Arid = std::clamp((Moisture - 0.44f) / 0.24f, 0.0f, 1.0f);
        const float LR = (ForestR + (DesertR - ForestR) * Arid) * (0.85f + 0.35f * Elev);
        const float LG = (ForestG + (DesertG - ForestG) * Arid) * (0.85f + 0.35f * Elev);
        const float LB = (ForestB + (DesertB - ForestB) * Arid) * (0.85f + 0.35f * Elev);
        AlbR = AlbR * (1.0f - LandMask) + LR * LandMask;
        AlbG = AlbG * (1.0f - LandMask) + LG * LandMask;
        AlbB = AlbB * (1.0f - LandMask) + LB * LandMask;
    }
    if (PolarIce > 0.0f)
    {
        AlbR = AlbR * (1.0f - PolarIce) + 0.72f * PolarIce;
        AlbG = AlbG * (1.0f - PolarIce) + 0.76f * PolarIce;
        AlbB = AlbB * (1.0f - PolarIce) + 0.82f * PolarIce;
    }

    // Ocean specular sun glint
    const float VX = -RayDirection.X;
    const float VY = -RayDirection.Y;
    const float VZ = -RayDirection.Z;
    float HVecX = SunDirection.X + VX, HVecY = SunDirection.Y + VY, HVecZ = SunDirection.Z + VZ;
    const float HLen = std::max(1.0e-5f, std::sqrt(HVecX * HVecX + HVecY * HVecY + HVecZ * HVecZ));
    HVecX /= HLen; HVecY /= HLen; HVecZ /= HLen;
    const float NdotH = std::max(0.0f, NX * HVecX + NY * HVecY + NZ * HVecZ);
    const float OceanSpec = (1.0f - LandMask) * (1.0f - PolarIce) * std::pow(NdotH, 42.0f) * 0.45f * (0.2f + 0.8f * NdotL);

    // Planetary terminator warmth & ForceField cyclone cloud shell
    const float TermWarm = std::exp(-std::abs(NdotL_raw) * 14.0f) * std::clamp((NdotL_raw + 0.08f) / 0.16f, 0.0f, 1.0f);
    const float SunVisR = (0.35f + 0.65f * std::clamp((NdotL_raw + 0.04f) / 0.25f, 0.0f, 1.0f)) * (1.0f + 0.65f * TermWarm);
    const float SunVisG = (0.35f + 0.65f * std::clamp((NdotL_raw + 0.04f) / 0.25f, 0.0f, 1.0f)) * (1.0f + 0.22f * TermWarm);
    const float SunVisB = (0.35f + 0.65f * std::clamp((NdotL_raw + 0.04f) / 0.25f, 0.0f, 1.0f)) * (1.0f - 0.10f * TermWarm);

    float CloudMask = 0.0f;
    if (IncludeOrbitalClouds && CloudCoverage01 > 0.01f)
    {
        float CX = NX, CY = NY, CZ = NZ;
        // Drift orbital cloud shell eastward with planetary zonal jet + optional ForceField cyclone curl
        const float JetLon = SimulationSeconds * 0.0022f;
        const float CosJ = std::cos(JetLon), SinJ = std::sin(JetLon);
        CX = NX * CosJ + NY * SinJ;
        CY = -NX * SinJ + NY * CosJ;

        if (OptionalForceFields != nullptr && OptionalForceFields->QueryActiveSourceCount() > 0u)
        {
            const auto WF = OptionalForceFields->QueryWindFieldSample(
                { HX * 0.0001f, HY * 0.0001f, 1500.0f, 0.0f },
                static_cast<double>(SimulationSeconds),
                true);
            CX -= WF.AdvectionVelocity.X * 0.003f;
            CY -= WF.AdvectionVelocity.Y * 0.003f;
        }
        // Embedded mesoscale spiral bands for cyclonic realism
        const float Lon = std::atan2(CY, CX);
        const float Lat = std::asin(std::clamp(CZ, -1.0f, 1.0f));
        const float SpiralTheta = std::sqrt((Lon - 0.35f) * (Lon - 0.35f) + (Lat - 0.38f) * (Lat - 0.38f));
        if (SpiralTheta < 0.45f)
        {
            const float SwirlA = (1.0f - SpiralTheta / 0.45f) * 1.45f;
            const float DLon = Lon - 0.35f, DLat = Lat - 0.38f;
            const float CS = std::cos(SwirlA), SS = std::sin(SwirlA);
            CX += (DLon * CS - DLat * SS - DLon) * 0.45f;
            CY += (DLon * SS + DLat * CS - DLat) * 0.45f;
        }

        const float RawC = FbmOrbit3(CX * 4.2f + 1.3f, CY * 4.2f - 2.1f, CZ * 4.2f + 3.7f, 5u);
        const float Thresh = 0.62f - 0.25f * std::clamp(CloudCoverage01, 0.0f, 1.0f);
        CloudMask = std::clamp((RawC - Thresh) / 0.18f, 0.0f, 1.0f);
    }

    const float CloudShadow = 1.0f - 0.48f * CloudMask;
    const float DiffBoost = 0.28f * std::max(0.0f, SolarConfig.SolarIntensityMultiplier) * Medium.SkyBrightnessMultiplier;
    float SurfR = ((AlbR * NdotL * CloudShadow + OceanSpec) * SunVisR * DiffBoost);
    float SurfG = ((AlbG * NdotL * CloudShadow + OceanSpec) * SunVisG * DiffBoost);
    float SurfB = ((AlbB * NdotL * CloudShadow + OceanSpec) * SunVisB * DiffBoost);

    // Night-side city lights on continents when solar elevation is well below terminator
    const float NightMask = std::clamp((-NdotL_raw - 0.04f) / 0.14f, 0.0f, 1.0f) * LandMask * (1.0f - PolarIce);
    if (NightMask > 0.01f)
    {
        const float Urban = FbmOrbit3(NX * 16.0f, NY * 16.0f, NZ * 16.0f, 3u);
        const float City  = std::clamp((Urban - 0.60f) / 0.12f, 0.0f, 1.0f) * NightMask * (1.0f - 0.65f * CloudMask);
        SurfR += City * 0.38f;
        SurfG += City * 0.22f;
        SurfB += City * 0.06f;
    }

    // Composite orbital cloud deck on top of planet albedo with sunset terminator glow
    if (CloudMask > 0.001f)
    {
        const float CloudLit = std::max(0.015f, NdotL * 1.15f + 0.04f) * DiffBoost * 0.85f;
        const float CR = CloudLit * SunVisR;
        const float CG = CloudLit * SunVisG;
        const float CB = CloudLit * SunVisB;
        SurfR = SurfR * (1.0f - CloudMask * 0.88f) + CR * (CloudMask * 0.88f);
        SurfG = SurfG * (1.0f - CloudMask * 0.88f) + CG * (CloudMask * 0.88f);
        SurfB = SurfB * (1.0f - CloudMask * 0.88f) + CB * (CloudMask * 0.88f);
    }

    return {
        std::max(0.0f, OutR + SurfR * TR),
        std::max(0.0f, OutG + SurfG * TG),
        std::max(0.0f, OutB + SurfB * TB),
    };
}

} // namespace Frontier::GeometricRaster
