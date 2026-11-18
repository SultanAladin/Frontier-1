/*============================================================================================================================================
                                                      ATMOSPHERICSKYSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Physical Bruneton/Hillaire Rayleigh + Mie + Ozone atmospheric transmittance, SkyView LUT, and solar disc/corona radiance

#include "Engine/GeometricRaster/Atmosphere/AtmosphericSkySequence.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::GeometricRaster
{

namespace
{

constexpr float ViewerAltitudeMeters = 100.0f;
constexpr float SeaLevelRayleighScatter[3] { 5.802e-6f, 13.558e-6f, 33.1e-6f };
constexpr float SeaLevelMieScatter        = 3.996e-6f;
constexpr float SeaLevelMieExtinction     = 4.40e-6f;
constexpr float PeakOzoneAbs[3]       { 0.650e-6f, 1.881e-6f, 0.085e-6f };

[[nodiscard]] std::array<float, 2> IntersectOriginSphere(float OriginZ, float DirZ, float Radius) noexcept
{
    const float B    = OriginZ * DirZ;
    const float C    = OriginZ * OriginZ - Radius * Radius;
    const float Disc = B * B - C;
    if (Disc < 0.0f)
    {
        return { -1.0f, -1.0f };
    }
    const float Root = std::sqrt(Disc);
    return { -B - Root, -B + Root };
}

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

[[nodiscard]] Mathematics::Axis3D ReconstructSkyViewDir(float U, float V) noexcept
{
    const float Azimuth   = (U - 0.5f) * Constants::TauFloat;
    const float SignedV   = (0.5f - V) * 2.0f;
    const float SignVal   = (SignedV >= 0.0f) ? 1.0f : -1.0f;
    const float Elevation = SignVal * SignedV * SignedV * (Constants::PiFloat * 0.5f);
    const float CosE      = std::cos(Elevation);
    return { CosE * std::cos(Azimuth), CosE * std::sin(Azimuth), std::sin(Elevation) };
}

[[nodiscard]] std::array<float, 3> SampleTransmittanceLut(
    const std::vector<float>& Lut,
    float                     AltitudeM,
    float                     CosZ,
    float                     AtmoTopM) noexcept
{
    const float SignZ = (CosZ >= 0.0f) ? 1.0f : -1.0f;
    const float U     = std::clamp(0.5f + 0.5f * SignZ * std::pow(std::abs(CosZ), 0.35f), 0.0f, 1.0f);
    const float V     = std::clamp(AltitudeM / std::max(1.0f, AtmoTopM), 0.0f, 1.0f);
    const float FX    = U * static_cast<float>(AtmosphericSkySequence::TransmittanceWidthPx - 1u);
    const float FY    = V * static_cast<float>(AtmosphericSkySequence::TransmittanceHeightPx - 1u);
    const uint32_t X0 = std::min(AtmosphericSkySequence::TransmittanceWidthPx - 2u, static_cast<uint32_t>(std::floor(FX)));
    const uint32_t Y0 = std::min(AtmosphericSkySequence::TransmittanceHeightPx - 2u, static_cast<uint32_t>(std::floor(FY)));
    const float TX    = FX - static_cast<float>(X0);
    const float TY    = FY - static_cast<float>(Y0);

    const std::size_t I00 = (static_cast<std::size_t>(Y0) * AtmosphericSkySequence::TransmittanceWidthPx + X0) * 3u;
    const std::size_t I10 = I00 + 3u;
    const std::size_t I01 = I00 + AtmosphericSkySequence::TransmittanceWidthPx * 3u;
    const std::size_t I11 = I01 + 3u;

    const auto Mix = [](float A, float B, float T) noexcept -> float { return A + (B - A) * T; };
    return {
        Mix(Mix(Lut[I00 + 0u], Lut[I10 + 0u], TX), Mix(Lut[I01 + 0u], Lut[I11 + 0u], TX), TY),
        Mix(Mix(Lut[I00 + 1u], Lut[I10 + 1u], TX), Mix(Lut[I01 + 1u], Lut[I11 + 1u], TX), TY),
        Mix(Mix(Lut[I00 + 2u], Lut[I10 + 2u], TX), Mix(Lut[I01 + 2u], Lut[I11 + 2u], TX), TY),
    };
}

} // namespace

Mathematics::LinearRGB AtmosphericSkySequence::ComputeSolarTransmittance(
    const Mathematics::Axis3D&            SunDirection,
    const AtmosphericMediumConfiguration& Medium) noexcept
{
    const float PlanetR   = std::max(500000.0f, Medium.PlanetRadiusMeters);
    const float AtmoTop   = std::max(10000.0f, Medium.AtmosphereThicknessMeters);
    const float AtmoR     = PlanetR + AtmoTop;
    const float RayMul    = std::max(0.0f, Medium.RayleighStrength);
    const float MieMul    = std::max(0.0f, Medium.MieStrength);
    const float OzoneMul  = std::max(0.0f, Medium.OzoneStrength);
    const float RayH      = std::max(500.0f, Medium.RayleighScaleHeightMeters);
    const float MieH      = std::max(200.0f, Medium.MieScaleHeightMeters);

    const float OrigZ     = PlanetR + ViewerAltitudeMeters;
    const auto [GNear, _] = IntersectOriginSphere(OrigZ, SunDirection.Z, PlanetR);
    if (GNear > 0.0f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    const auto [__, ExitT] = IntersectOriginSphere(OrigZ, SunDirection.Z, AtmoR);
    if (ExitT <= 0.0f)
    {
        return { 1.0f, 1.0f, 1.0f };
    }

    constexpr uint32_t Samples = 48u;
    const float Step = ExitT / static_cast<float>(Samples);
    float DR = 0.0f, DG = 0.0f, DB = 0.0f;
    for (uint32_t I = 0u; I < Samples; ++I)
    {
        const float T   = (static_cast<float>(I) + 0.5f) * Step;
        const float PX  = SunDirection.X * T;
        const float PY  = SunDirection.Y * T;
        const float PZ  = OrigZ + SunDirection.Z * T;
        const float Alt = std::max(0.0f, std::sqrt(PX * PX + PY * PY + PZ * PZ) - PlanetR);
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

void AtmosphericSkySequence::BuildSkyViewLut(
    const Mathematics::Axis3D&            SunDirection,
    const AtmosphericMediumConfiguration& Medium,
    float                                 SolarIntensityScale)
{
    const float PlanetR  = std::max(500000.0f, Medium.PlanetRadiusMeters);
    const float AtmoTop  = std::max(10000.0f, Medium.AtmosphereThicknessMeters);
    const float AtmoR    = PlanetR + AtmoTop;
    const float RayMul   = std::max(0.0f, Medium.RayleighStrength);
    const float MieMul   = std::max(0.0f, Medium.MieStrength);
    const float MieG     = std::clamp(Medium.MieAnisotropy, 0.0f, 0.99f);
    const float OzoneMul = std::max(0.0f, Medium.OzoneStrength);
    const float RayH     = std::max(500.0f, Medium.RayleighScaleHeightMeters);
    const float MieH     = std::max(200.0f, Medium.MieScaleHeightMeters);

    // 1. Build 128x32 Optical Transmittance LUT
    std::vector<float> TransLut(TransmittanceWidthPx * TransmittanceHeightPx * 3u, 1.0f);
    for (uint32_t Y = 0u; Y < TransmittanceHeightPx; ++Y)
    {
        const float V   = static_cast<float>(Y) / static_cast<float>(TransmittanceHeightPx - 1u);
        const float Alt = V * AtmoTop;
        const float R   = PlanetR + Alt;
        for (uint32_t X = 0u; X < TransmittanceWidthPx; ++X)
        {
            const float U    = static_cast<float>(X) / static_cast<float>(TransmittanceWidthPx - 1u);
            const float S    = (U - 0.5f) * 2.0f;
            const float CosZ = std::clamp(((S >= 0.0f) ? 1.0f : -1.0f) * std::pow(std::abs(S), 2.857f), -1.0f, 1.0f);
            const float SinZ = std::sqrt(std::max(0.0f, 1.0f - CosZ * CosZ));

            const auto [_, ExitT] = IntersectOriginSphere(R, CosZ, AtmoR);
            const std::size_t Idx = (static_cast<std::size_t>(Y) * TransmittanceWidthPx + X) * 3u;
            if (ExitT <= 0.0f)
            {
                TransLut[Idx + 0u] = TransLut[Idx + 1u] = TransLut[Idx + 2u] = 1.0f;
                continue;
            }
            const float Step = ExitT / 16.0f;
            float DR = 0.0f, DG = 0.0f, DB = 0.0f;
            for (uint32_t I = 0u; I < 16u; ++I)
            {
                const float T    = (static_cast<float>(I) + 0.5f) * Step;
                const float PX   = SinZ * T;
                const float PZ   = R + CosZ * T;
                const float H    = std::max(0.0f, std::sqrt(PX * PX + PZ * PZ) - PlanetR);
                const float DenR = std::exp(-H / RayH) * RayMul;
                const float DenM = std::exp(-H / MieH) * MieMul;
                const float DenO = std::max(0.0f, 1.0f - std::abs(H - 25000.0f) / 15000.0f) * OzoneMul;
                const float MieE = SeaLevelMieExtinction * DenM;
                DR += (SeaLevelRayleighScatter[0] * DenR + MieE + PeakOzoneAbs[0] * DenO) * Step;
                DG += (SeaLevelRayleighScatter[1] * DenR + MieE + PeakOzoneAbs[1] * DenO) * Step;
                DB += (SeaLevelRayleighScatter[2] * DenR + MieE + PeakOzoneAbs[2] * DenO) * Step;
            }
            TransLut[Idx + 0u] = std::exp(-DR);
            TransLut[Idx + 1u] = std::exp(-DG);
            TransLut[Idx + 2u] = std::exp(-DB);
        }
    }

    // 2. Build 256x128 SkyView LUT
    SkyViewTexelsRgba.assign(SkyViewWidthPx * SkyViewHeightPx * 4u, 0.0f);
    const float OrigZ        = PlanetR + ViewerAltitudeMeters;
    const float SunElevRad   = std::asin(std::clamp(SunDirection.Z, -1.0f, 1.0f));
    const auto  OverheadSunT = SampleTransmittanceLut(TransLut, 0.0f, std::max(0.05f, SunDirection.Z), AtmoTop);
    const float SolarScale   = std::max(0.0f, SolarIntensityScale) * 0.075f;

    for (uint32_t Y = 0u; Y < SkyViewHeightPx; ++Y)
    {
        const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(SkyViewHeightPx);
        for (uint32_t X = 0u; X < SkyViewWidthPx; ++X)
        {
            const float U        = (static_cast<float>(X) + 0.5f) / static_cast<float>(SkyViewWidthPx);
            const auto RawDir    = ReconstructSkyViewDir(U, V);
            const bool GroundRay = (RawDir.Z < 0.0f);
            const float RdZ      = GroundRay ? 0.002f : RawDir.Z;
            const float RdHoriz  = std::sqrt(std::max(1.0e-8f, RawDir.X * RawDir.X + RawDir.Y * RawDir.Y));
            const float RdScale  = std::sqrt(std::max(0.0f, 1.0f - RdZ * RdZ)) / RdHoriz;
            const float RdX      = GroundRay ? (RawDir.X * RdScale) : RawDir.X;
            const float RdY      = GroundRay ? (RawDir.Y * RdScale) : RawDir.Y;

            const auto [_, AtmoExit] = IntersectOriginSphere(OrigZ, RdZ, AtmoR);
            const float MaxDist = std::min(AtmoExit, 420000.0f);
            const float CosTheta = RdX * SunDirection.X + RdY * SunDirection.Y + RdZ * SunDirection.Z;

            const float PhaseR = (3.0f / (16.0f * Constants::PiFloat)) * (1.0f + CosTheta * CosTheta);
            const float G2     = MieG * MieG;
            const float Denom  = std::max(1.0e-3f, 1.0f + G2 - 2.0f * MieG * CosTheta);
            const float PhaseM = (3.0f / (8.0f * Constants::PiFloat)) * ((1.0f - G2) * (1.0f + CosTheta * CosTheta)) /
                                 ((2.0f + G2) * std::pow(Denom, 1.5f));

            float TR = 1.0f, TG = 1.0f, TB = 1.0f;
            float RadR = 0.0f, RadG = 0.0f, RadB = 0.0f;
            float MsR = 0.0f, MsG = 0.0f, MsB = 0.0f;

            constexpr uint32_t Steps = 28u;
            const float Step = std::max(1.0f, MaxDist / static_cast<float>(Steps));
            for (uint32_t S = 0u; S < Steps; ++S)
            {
                const float T   = (static_cast<float>(S) + 0.5f) * Step;
                const float PX  = RdX * T;
                const float PY  = RdY * T;
                const float PZ  = OrigZ + RdZ * T;
                const float RLen = std::sqrt(PX * PX + PY * PY + PZ * PZ);
                const float Alt  = std::max(0.0f, RLen - PlanetR);

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

                const auto [EarthHit, _2] = IntersectVecSphere(PX, PY, PZ, SunDirection.X, SunDirection.Y, SunDirection.Z, PlanetR);
                std::array<float, 3> SunT { 0.0f, 0.0f, 0.0f };
                if (EarthHit <= 0.0f)
                {
                    const float CosSun = (PX * SunDirection.X + PY * SunDirection.Y + PZ * SunDirection.Z) / std::max(1.0f, RLen);
                    SunT = SampleTransmittanceLut(TransLut, Alt, CosSun, AtmoTop);
                }

                const float ScR = SRayR * PhaseR + SMie * PhaseM;
                const float ScG = SRayG * PhaseR + SMie * PhaseM;
                const float ScB = SRayB * PhaseR + SMie * PhaseM;

                RadR += TR * ScR * SunT[0] * IntR;
                RadG += TG * ScG * SunT[1] * IntG;
                RadB += TB * ScB * SunT[2] * IntB;

                constexpr float IsoScatter = 1.0f / (4.0f * Constants::PiFloat);
                const float AvgGround = (Medium.GroundAlbedo.R + Medium.GroundAlbedo.G + Medium.GroundAlbedo.B) * 0.333333f;
                const float MsBounce  = 0.24f + 0.18f * AvgGround;
                MsR += TR * (SRayR + SMie) * IsoScatter * (SunT[0] * 0.65f + OverheadSunT[0] * 0.35f) * MsBounce * IntR;
                MsG += TG * (SRayG + SMie) * IsoScatter * (SunT[1] * 0.65f + OverheadSunT[1] * 0.35f) * MsBounce * IntG;
                MsB += TB * (SRayB + SMie) * IsoScatter * (SunT[2] * 0.65f + OverheadSunT[2] * 0.35f) * MsBounce * IntB;

                TR *= SegTR;
                TG *= SegTG;
                TB *= SegTB;
            }

            const float LimbFactor = std::exp(-std::max(0.0f, RdZ) * 7.5f);
            const float GlowScale  = 1.0f + (Medium.LimbGlowIntensity - 1.0f) * LimbFactor * 0.55f;
            const float DayVis     = std::clamp((SunElevRad * Constants::RadToDegFloat + 6.0f) / 14.0f, 0.0f, 1.0f);
            const float NightR     = 0.0007f + 0.0009f * LimbFactor;
            const float NightG     = 0.0010f + 0.0013f * LimbFactor;
            const float NightB     = 0.0022f + 0.0024f * LimbFactor;

            // Dawn / civil twilight luminous whitish-silver horizon line (Rayleigh + forward Mie aerosol multi-scatter)
            const float SunClamped = std::clamp(SunDirection.Z, -0.2f, 1.0f);
            const float DawnRise   = std::clamp((SunClamped + 0.10f) / 0.13f, 0.0f, 1.0f);
            const float DawnFall   = std::clamp((SunClamped - 0.14f) / 0.30f, 0.0f, 1.0f);
            const float DawnWindow = (DawnRise * DawnRise * (3.0f - 2.0f * DawnRise)) *
                                     (1.0f - DawnFall * DawnFall * (3.0f - 2.0f * DawnFall));
            const float HorizonLine = std::exp(-std::abs(RawDir.Z) * 26.0f);
            const float HorizonGlow = std::exp(-std::abs(RawDir.Z) * 9.0f);
            const float RdLen2D     = std::sqrt(std::max(1.0e-10f, RdX * RdX + RdY * RdY));
            const float SunLen2D    = std::sqrt(std::max(1.0e-10f, SunDirection.X * SunDirection.X + SunDirection.Y * SunDirection.Y));
            const float ForwardAz   = 0.5f + 0.5f * std::clamp((RdX * SunDirection.X + RdY * SunDirection.Y) / (RdLen2D * SunLen2D), -1.0f, 1.0f);
            const float DawnGain    = (HorizonLine * (0.32f + 0.28f * ForwardAz) + HorizonGlow * 0.10f) * DawnWindow * SolarScale * 0.34f;

            float OutR = ((RadR + MsR) * SolarScale * GlowScale + 0.95f * DawnGain + NightR * (1.0f - DayVis * 0.9f)) * Medium.SkyTint.R * Medium.SkyBrightnessMultiplier;
            float OutG = ((RadG + MsG) * SolarScale * GlowScale + 0.96f * DawnGain + NightG * (1.0f - DayVis * 0.9f)) * Medium.SkyTint.G * Medium.SkyBrightnessMultiplier;
            float OutB = ((RadB + MsB) * SolarScale * GlowScale + 1.00f * DawnGain + NightB * (1.0f - DayVis * 0.9f)) * Medium.SkyTint.B * Medium.SkyBrightnessMultiplier;

            if (GroundRay)
            {
                const float SunUp     = std::max(0.0f, SunDirection.Z);
                const float Depth     = std::clamp(-RawDir.Z, 0.0f, 1.0f);
                const float HazeBlend = std::clamp(Depth / 0.28f, 0.0f, 1.0f);
                const float SmoothHz  = HazeBlend * HazeBlend * (3.0f - 2.0f * HazeBlend);
                const float NadirT    = std::clamp((Depth - 0.18f) / 0.74f, 0.0f, 1.0f);
                const float SmoothNd  = NadirT * NadirT * (3.0f - 2.0f * NadirT);
                const float DayScale  = 0.18f + 0.82f * SunUp;
                const float NightAdd  = 1.0f - DayVis * 0.85f;

                const float NadirR = 0.032f * DayScale * OverheadSunT[0] + NightR * NightAdd * 1.4f;
                const float NadirG = 0.068f * DayScale * OverheadSunT[1] + NightG * NightAdd * 1.4f;
                const float NadirB = 0.135f * DayScale * OverheadSunT[2] + NightB * NightAdd * 1.4f;
                const float SubGndR = Medium.GroundAlbedo.R * (OverheadSunT[0] * SunUp * 0.12f + 0.0005f);
                const float SubGndG = Medium.GroundAlbedo.G * (OverheadSunT[1] * SunUp * 0.12f + 0.0005f);
                const float SubGndB = Medium.GroundAlbedo.B * (OverheadSunT[2] * SunUp * 0.12f + 0.0005f);

                const float HzR = OutR * (1.0f - 0.28f * SmoothHz);
                const float HzG = OutG * (1.0f - 0.28f * SmoothHz);
                const float HzB = OutB * (1.0f - 0.28f * SmoothHz);
                const float LowR = HzR * (1.0f - SmoothHz) + (NadirR + SubGndR) * SmoothHz;
                const float LowG = HzG * (1.0f - SmoothHz) + (NadirG + SubGndG) * SmoothHz;
                const float LowB = HzB * (1.0f - SmoothHz) + (NadirB + SubGndB) * SmoothHz;
                const float NdMix = SmoothNd * 0.65f;
                OutR = LowR * (1.0f - NdMix) + (NadirR * 0.72f + SubGndR * 0.65f) * NdMix;
                OutG = LowG * (1.0f - NdMix) + (NadirG * 0.72f + SubGndG * 0.65f) * NdMix;
                OutB = LowB * (1.0f - NdMix) + (NadirB * 0.72f + SubGndB * 0.65f) * NdMix;
            }

            const std::size_t Idx = (static_cast<std::size_t>(Y) * SkyViewWidthPx + X) * 4u;
            SkyViewTexelsRgba[Idx + 0u] = std::max(0.0f, OutR);
            SkyViewTexelsRgba[Idx + 1u] = std::max(0.0f, OutG);
            SkyViewTexelsRgba[Idx + 2u] = std::max(0.0f, OutB);
            SkyViewTexelsRgba[Idx + 3u] = 1.0f;
        }
    }
}

Mathematics::LinearRGB AtmosphericSkySequence::SampleSkyViewDirection(
    const Mathematics::Axis3D& WorldDirection) const noexcept
{
    if (!QueryLutReady())
    {
        return { 0.16f, 0.23f, 0.36f };
    }
    const float Azimuth   = std::atan2(WorldDirection.Y, WorldDirection.X);
    const float Elevation = std::asin(std::clamp(WorldDirection.Z, -1.0f, 1.0f));
    float U = Azimuth / Constants::TauFloat + 0.5f;
    U = U - std::floor(U);
    const float SignE = (Elevation >= 0.0f) ? 1.0f : -1.0f;
    const float V     = std::clamp(0.5f - 0.5f * SignE * std::sqrt(std::abs(Elevation) / (Constants::PiFloat * 0.5f)), 0.0f, 1.0f);

    const float FX = U * static_cast<float>(SkyViewWidthPx) - 0.5f;
    const float FY = V * static_cast<float>(SkyViewHeightPx) - 0.5f;
    const int32_t W = static_cast<int32_t>(SkyViewWidthPx);
    const int32_t H = static_cast<int32_t>(SkyViewHeightPx);
    const int32_t IX0 = ((static_cast<int32_t>(std::floor(FX)) % W) + W) % W;
    const int32_t IX1 = (IX0 + 1) % W;
    const int32_t IY1 = std::clamp(static_cast<int32_t>(std::floor(FY)), 0, H - 1);
    const int32_t IY0 = std::clamp(IY1 - 1, 0, H - 1);
    const int32_t IY2 = std::clamp(IY1 + 1, 0, H - 1);
    const int32_t IY3 = std::clamp(IY1 + 2, 0, H - 1);
    const float TX = FX - std::floor(FX);
    const float SX = TX * TX * (3.0f - 2.0f * TX);
    const float TY = std::clamp(FY - std::floor(FY), 0.0f, 1.0f);

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

Mathematics::LinearRGB AtmosphericSkySequence::EvaluateSolarCoronaRadiance(
    const Mathematics::Axis3D&         RayDirection,
    const Mathematics::Axis3D&         SunDirection,
    const SolarCelestialConfiguration& SolarConfig,
    const Mathematics::LinearRGB&      SolarTransmittance,
    const Mathematics::LinearRGB&      EffectiveSolarTint) noexcept
{
    if (!SolarConfig.SolarEnabled || RayDirection.Z < -0.04f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    const float CosAngle = std::clamp(Mathematics::EvaluateDot(RayDirection, SunDirection), -1.0f, 1.0f);
    const float HalfRad  = std::max(0.0008f, SolarConfig.AngularDiameterDegrees * 0.5f * Constants::DegToRadFloat);
    const float Angle    = std::acos(CosAngle);
    const float DiscEdge = std::clamp(1.0f - (Angle - HalfRad * 0.92f) / std::max(0.0005f, HalfRad * 0.25f), 0.0f, 1.0f);
    const float Corona   = std::exp(-Angle / std::max(0.008f, HalfRad * 3.8f)) * 0.55f +
                           std::pow(std::max(0.0f, CosAngle), 240.0f) * 0.28f;
    const float LimbFade = std::clamp((RayDirection.Z + 0.02f) * 18.0f, 0.0f, 1.0f);
    const float Scale    = std::max(0.0f, SolarConfig.SolarIntensityMultiplier) * 0.35f * LimbFade;

    const float DiscRadiance = DiscEdge * 26.0f + Corona;
    return {
        EffectiveSolarTint.R * SolarTransmittance.R * DiscRadiance * Scale,
        EffectiveSolarTint.G * SolarTransmittance.G * DiscRadiance * Scale,
        EffectiveSolarTint.B * SolarTransmittance.B * DiscRadiance * Scale
    };
}

} // namespace Frontier::GeometricRaster
