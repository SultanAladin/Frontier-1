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

[[nodiscard]] float SmoothStepSky(float Edge0, float Edge1, float X) noexcept
{
    const float T = std::clamp((X - Edge0) / std::max(1.0e-5f, Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] float HashUnitSky3(int32_t X, int32_t Y, int32_t Z) noexcept
{
    uint32_t H = static_cast<uint32_t>(X) * 374761393u +
                 static_cast<uint32_t>(Y) * 668265263u +
                 static_cast<uint32_t>(Z) * 1442695041u;
    H = (H ^ (H >> 13u)) * 1274126177u;
    H ^= (H >> 16u);
    return static_cast<float>(H & 0x00FFFFFFu) / 16777216.0f;
}

[[nodiscard]] float NoiseSky3(float X, float Y, float Z) noexcept
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
    const auto Mix = [](float A, float B, float T) noexcept { return A + (B - A) * T; };

    const float C000 = HashUnitSky3(IX, IY, IZ);
    const float C100 = HashUnitSky3(IX + 1, IY, IZ);
    const float C010 = HashUnitSky3(IX, IY + 1, IZ);
    const float C110 = HashUnitSky3(IX + 1, IY + 1, IZ);
    const float C001 = HashUnitSky3(IX, IY, IZ + 1);
    const float C101 = HashUnitSky3(IX + 1, IY, IZ + 1);
    const float C011 = HashUnitSky3(IX, IY + 1, IZ + 1);
    const float C111 = HashUnitSky3(IX + 1, IY + 1, IZ + 1);

    const float X00 = Mix(C000, C100, UX);
    const float X10 = Mix(C010, C110, UX);
    const float X01 = Mix(C001, C101, UX);
    const float X11 = Mix(C011, C111, UX);
    return Mix(Mix(X00, X10, UY), Mix(X01, X11, UY), UZ);
}

[[nodiscard]] float FbmSky3(float X, float Y, float Z, uint32_t Octaves) noexcept
{
    float Sum = 0.0f;
    float Amp = 0.5f;
    float PX = X, PY = Y, PZ = Z;
    for (uint32_t I = 0u; I < Octaves; ++I)
    {
        Sum += Amp * NoiseSky3(PX, PY, PZ);
        const float NX = PX * 1.62f - PY * 1.18f + 13.7f;
        const float NY = PX * 1.18f + PY * 1.62f - 7.3f;
        const float NZ = PZ * 1.98f + 5.1f;
        PX = NX; PY = NY; PZ = NZ;
        Amp *= 0.5f;
    }
    return Sum;
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
    const AtmosphericMediumConfiguration& Medium,
    float                                 ObserverAltitudeOverrideM) noexcept
{
    const float PlanetR   = std::max(500000.0f, Medium.PlanetRadiusMeters);
    const float AtmoTop   = std::max(10000.0f, Medium.AtmosphereThicknessMeters);
    const float AtmoR     = PlanetR + AtmoTop;
    const float RayMul    = std::max(0.0f, Medium.RayleighStrength);
    const float MieMul    = std::max(0.0f, Medium.MieStrength);
    const float OzoneMul  = std::max(0.0f, Medium.OzoneStrength);
    const float RayH      = std::max(500.0f, Medium.RayleighScaleHeightMeters);
    const float MieH      = std::max(200.0f, Medium.MieScaleHeightMeters);

    const float ObsAltM   = (ObserverAltitudeOverrideM >= 0.0f)
                                ? ObserverAltitudeOverrideM
                                : std::max(0.0f, Medium.ObserverAltitudeMeters);
    const float OrigZ     = PlanetR + ObsAltM;
    const auto [GNear, _] = IntersectOriginSphere(OrigZ, SunDirection.Z, PlanetR);
    if (GNear > 0.0f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    const auto [AtmoNear, ExitT] = IntersectOriginSphere(OrigZ, SunDirection.Z, AtmoR);
    if (ExitT <= 0.0f)
    {
        return { 1.0f, 1.0f, 1.0f };
    }
    const float StartT = std::max(0.0f, AtmoNear);
    const float SpanT  = ExitT - StartT;
    if (SpanT <= 1.0f)
    {
        return { 1.0f, 1.0f, 1.0f };
    }

    constexpr uint32_t Samples = 48u;
    const float Step = SpanT / static_cast<float>(Samples);
    float DR = 0.0f, DG = 0.0f, DB = 0.0f;
    for (uint32_t I = 0u; I < Samples; ++I)
    {
        const float T   = StartT + (static_cast<float>(I) + 0.5f) * Step;
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
    const float ObsAltClamped = std::clamp(Medium.ObserverAltitudeMeters, 10.0f, AtmoTop * 0.96f);
    const float OrigZ         = PlanetR + (Medium.OrbitalViewEnabled ? ViewerAltitudeMeters : ObsAltClamped);
    const float SunElevRad    = std::asin(std::clamp(SunDirection.Z, -1.0f, 1.0f));
    const float OverheadSunFade = SmoothStepSky(-0.14f, 0.04f, SunDirection.Z);
    const float NightTwilFade   = SmoothStepSky(-0.28f, -0.14f, SunDirection.Z);
    auto        OverheadSunT    = SampleTransmittanceLut(TransLut, 0.0f, std::max(0.05f, SunDirection.Z), AtmoTop);
    OverheadSunT[0] *= OverheadSunFade;
    OverheadSunT[1] *= OverheadSunFade;
    OverheadSunT[2] *= OverheadSunFade;
    const float SolarScale    = std::max(0.0f, SolarIntensityScale) * 0.24f;

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
                if (EarthHit <= 0.0f && NightTwilFade > 1.0e-5f)
                {
                    const float CosSun = (PX * SunDirection.X + PY * SunDirection.Y + PZ * SunDirection.Z) / std::max(1.0f, RLen);
                    const float AntiCool = SmoothStepSky(0.06f, 0.24f, SunDirection.Z)
                        + (1.0f - SmoothStepSky(0.06f, 0.24f, SunDirection.Z)) * (0.35f + 0.65f * SmoothStepSky(-0.35f, 0.30f, CosTheta));
                    SunT = SampleTransmittanceLut(TransLut, Alt, CosSun, AtmoTop);
                    SunT[0] *= NightTwilFade * AntiCool;
                    SunT[1] *= NightTwilFade * AntiCool;
                    SunT[2] *= NightTwilFade * AntiCool;
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
                const float MsDir     = 0.42f + 0.58f * (CosTheta * 0.5f + 0.5f);
                const float SunMsW    = 0.65f * MsDir;
                const float OvhMsW    = 0.35f + 0.20f * (1.0f - MsDir);
                MsR += TR * (SRayR + SMie) * IsoScatter * (SunT[0] * SunMsW + OverheadSunT[0] * OvhMsW) * MsBounce * IntR;
                MsG += TG * (SRayG + SMie) * IsoScatter * (SunT[1] * SunMsW + OverheadSunT[1] * OvhMsW) * MsBounce * IntG;
                MsB += TB * (SRayB + SMie) * IsoScatter * (SunT[2] * SunMsW + OverheadSunT[2] * OvhMsW) * MsBounce * IntB;

                TR *= SegTR;
                TG *= SegTG;
                TB *= SegTB;
            }

            const float LimbFactor = std::exp(-std::max(0.0f, RdZ) * 7.5f);
            const float GlowScale  = 1.0f + (Medium.LimbGlowIntensity - 1.0f) * LimbFactor * 0.45f;
            const float DayVis     = std::clamp((SunElevRad * Constants::RadToDegFloat + 6.0f) / 14.0f, 0.0f, 1.0f);
            const float NightR     = 0.0005f + 0.0007f * LimbFactor;
            const float NightG     = 0.0008f + 0.0011f * LimbFactor;
            const float NightB     = 0.0020f + 0.0022f * LimbFactor;

            // Pure physical Rayleigh + Mie + Ozone scattering (no artificial 360-deg white DawnMist stripe)
            float OutR = ((RadR + MsR) * SolarScale * GlowScale + NightR * (1.0f - DayVis * 0.9f)) * Medium.SkyTint.R * Medium.SkyBrightnessMultiplier;
            float OutG = ((RadG + MsG) * SolarScale * GlowScale + NightG * (1.0f - DayVis * 0.9f)) * Medium.SkyTint.G * Medium.SkyBrightnessMultiplier;
            float OutB = ((RadB + MsB) * SolarScale * GlowScale + NightB * (1.0f - DayVis * 0.9f)) * Medium.SkyTint.B * Medium.SkyBrightnessMultiplier;

            if (GroundRay)
            {
                // Clean cool planetary lower-hemisphere aerial perspective (zero brown/beige horizon band)
                const float SunUp     = std::max(0.0f, SunDirection.Z);
                const float Depth     = std::clamp(-RawDir.Z, 0.0f, 1.0f);
                const float HazeBlend = SmoothStepSky(0.0f, 0.095f, Depth);
                const float NadirT    = SmoothStepSky(0.05f, 0.42f, Depth);
                const float DayScale  = 0.14f + 0.86f * SunUp;
                const float NightAdd  = 1.0f - DayVis * 0.85f;

                // Cool slate-indigo atmospheric nadir matching Rayleigh sky scattering
                const float CoolMixR = std::min(Medium.GroundAlbedo.R, Medium.GroundAlbedo.B * 0.95f) * 0.03f;
                const float CoolMixG = std::min(Medium.GroundAlbedo.G, Medium.GroundAlbedo.B) * 0.04f;
                const float CoolMixB = Medium.GroundAlbedo.B * 0.06f;

                const float NadirR = (0.022f + CoolMixR) * DayScale * OverheadSunT[0] + NightR * NightAdd * 1.25f;
                const float NadirG = (0.052f + CoolMixG) * DayScale * OverheadSunT[1] + NightG * NightAdd * 1.25f;
                const float NadirB = (0.115f + CoolMixB) * DayScale * OverheadSunT[2] + NightB * NightAdd * 1.25f;

                const float HzR = OutR * (1.0f - 0.82f * HazeBlend);
                const float HzG = OutG * (1.0f - 0.82f * HazeBlend);
                const float HzB = OutB * (1.0f - 0.82f * HazeBlend);
                const float LowR = HzR * (1.0f - HazeBlend) + NadirR * HazeBlend;
                const float LowG = HzG * (1.0f - HazeBlend) + NadirG * HazeBlend;
                const float LowB = HzB * (1.0f - HazeBlend) + NadirB * HazeBlend;
                const float NdMix = NadirT * 0.78f;
                OutR = LowR * (1.0f - NdMix) + (NadirR * 0.62f) * NdMix;
                OutG = LowG * (1.0f - NdMix) + (NadirG * 0.62f) * NdMix;
                OutB = LowB * (1.0f - NdMix) + (NadirB * 0.62f) * NdMix;
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
    const Mathematics::LinearRGB&      EffectiveSolarTint,
    bool                               OrbitalSpaceView) noexcept
{
    if (!SolarConfig.SolarEnabled || (!OrbitalSpaceView && RayDirection.Z < -0.04f))
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    const float CosAngle = std::clamp(Mathematics::EvaluateDot(RayDirection, SunDirection), -1.0f, 1.0f);
    const float HalfRad  = std::max(0.0008f, SolarConfig.AngularDiameterDegrees * 0.5f * Constants::DegToRadFloat);
    const float Angle    = std::acos(CosAngle);

    // Smooth Eddington photospheric limb darkening + continuous multi-scale coronal / Mie aureole falloff
    const float RNorm        = Angle / HalfRad;
    const float DiscMask     = 1.0f - SmoothStepSky(0.92f, 1.14f, RNorm);
    const float MuSun        = std::sqrt(std::max(0.0f, 1.0f - std::min(1.0f, RNorm * RNorm)));
    const float LimbDark     = 0.42f + 0.58f * std::pow(MuSun, 0.45f);
    const float EnvFade      = 1.0f - SmoothStepSky(HalfRad * 18.0f, HalfRad * 45.0f, Angle);
    const float InnerCorona  = std::exp(-std::max(0.0f, Angle - HalfRad * 0.70f) / std::max(0.003f, HalfRad * 2.4f)) * 0.80f;
    const float MidAureole   = std::exp(-Angle / std::max(0.012f, HalfRad * 9.0f)) * 0.20f * EnvFade;
    const float OuterGlare   = std::exp(-Angle / std::max(0.045f, HalfRad * 24.0f)) * 0.025f * EnvFade;
    const float LimbFade     = OrbitalSpaceView ? 1.0f : std::clamp((RayDirection.Z + 0.02f) * 18.0f, 0.0f, 1.0f);
    const float Scale        = std::max(0.0f, SolarConfig.SolarIntensityMultiplier) * 0.35f * LimbFade;

    const float DiscRadiance = DiscMask * LimbDark * 28.0f + InnerCorona + MidAureole + OuterGlare;
    return {
        EffectiveSolarTint.R * SolarTransmittance.R * DiscRadiance * Scale,
        EffectiveSolarTint.G * SolarTransmittance.G * DiscRadiance * Scale,
        EffectiveSolarTint.B * SolarTransmittance.B * DiscRadiance * Scale
    };
}

Mathematics::LinearRGB AtmosphericSkySequence::EvaluateOrbitalSpaceRadiance(
    const Mathematics::Axis3D&            RayDirection,
    const Mathematics::Axis3D&            CameraOriginMeters,
    const Mathematics::Axis3D&            SunDirection,
    const AtmosphericMediumConfiguration& Medium,
    float                                 SolarIntensityScale,
    float                                 SimulationSeconds,
    float                                 CloudCoverage01,
    Mathematics::LinearRGB*               OutSpaceTransmittance) noexcept
{
    const float PlanetR  = std::max(500000.0f, Medium.PlanetRadiusMeters);
    const float AtmoTop  = std::max(60000.0f, Medium.AtmosphereThicknessMeters * 1.65f);
    const float AtmoR    = PlanetR + AtmoTop;
    const float ObsAlt   = std::max(AtmoTop * 1.15f, Medium.ObserverAltitudeMeters);
    const float RayMul   = std::max(0.0f, Medium.RayleighStrength);
    const float MieMul   = std::max(0.0f, Medium.MieStrength);
    const float MieG     = std::clamp(Medium.MieAnisotropy, 0.0f, 0.99f);
    const float OzoneMul = std::max(0.0f, Medium.OzoneStrength);
    const float RayH     = std::max(500.0f, Medium.RayleighScaleHeightMeters);
    const float MieH     = std::max(200.0f, Medium.MieScaleHeightMeters);

    // Place planet directly beneath Z=0 so CameraOriginMeters at altitude ObsAlt sees the curved horizon & limb
    const float OX = CameraOriginMeters.X;
    const float OY = CameraOriginMeters.Y;
    const float OZ = CameraOriginMeters.Z + PlanetR + ObsAlt;

    const float DX = RayDirection.X;
    const float DY = RayDirection.Y;
    const float DZ = RayDirection.Z;

    const auto [AtmoNear, AtmoFar] = IntersectVecSphere(OX, OY, OZ, DX, DY, DZ, AtmoR);
    if (AtmoFar <= 0.0f)
    {
        if (OutSpaceTransmittance != nullptr) *OutSpaceTransmittance = { 1.0f, 1.0f, 1.0f };
        return { 0.0f, 0.0f, 0.0f };
    }

    const auto [PlanetNear, _PlanetFar] = IntersectVecSphere(OX, OY, OZ, DX, DY, DZ, PlanetR);
    const bool  HitPlanet = (PlanetNear > 0.0f && PlanetNear < AtmoFar);
    const float RayStart  = std::max(0.0f, AtmoNear);
    const float RayEnd    = HitPlanet ? PlanetNear : AtmoFar;
    const float PathLen   = std::max(0.0f, RayEnd - RayStart);
    if (PathLen <= 1.0f)
    {
        if (OutSpaceTransmittance != nullptr) *OutSpaceTransmittance = { 1.0f, 1.0f, 1.0f };
        return { 0.0f, 0.0f, 0.0f };
    }

    const float CosTheta = DX * SunDirection.X + DY * SunDirection.Y + DZ * SunDirection.Z;
    const float PhaseR   = (3.0f / (16.0f * Constants::PiFloat)) * (1.0f + CosTheta * CosTheta);
    const float G2       = MieG * MieG;
    const float Denom    = std::max(1.0e-3f, 1.0f + G2 - 2.0f * MieG * CosTheta);
    const float PhaseM   = (3.0f / (8.0f * Constants::PiFloat)) * ((1.0f - G2) * (1.0f + CosTheta * CosTheta)) /
                           ((2.0f + G2) * std::pow(Denom, 1.5f));

    constexpr uint32_t Steps = 32u;
    const float Step = PathLen / static_cast<float>(Steps);
    float TR = 1.0f, TG = 1.0f, TB = 1.0f;
    float RadR = 0.0f, RadG = 0.0f, RadB = 0.0f;

    auto EvalSunTransmittanceAt = [&](float SX, float SY, float SZ) noexcept -> std::array<float, 3>
    {
        const auto [G0, _G1] = IntersectVecSphere(SX, SY, SZ, SunDirection.X, SunDirection.Y, SunDirection.Z, PlanetR);
        if (G0 > 0.0f)
        {
            return { 0.0f, 0.0f, 0.0f };
        }
        const auto [_A0, AExit] = IntersectVecSphere(SX, SY, SZ, SunDirection.X, SunDirection.Y, SunDirection.Z, AtmoR);
        if (AExit <= 0.0f)
        {
            return { 1.0f, 1.0f, 1.0f };
        }
        const float SStep = std::min(AExit, 240000.0f) / 10.0f;
        float ODR = 0.0f, ODG = 0.0f, ODB = 0.0f;
        for (uint32_t J = 0u; J < 10u; ++J)
        {
            const float ST   = (static_cast<float>(J) + 0.5f) * SStep;
            const float QX   = SX + SunDirection.X * ST;
            const float QY   = SY + SunDirection.Y * ST;
            const float QZ   = SZ + SunDirection.Z * ST;
            const float QAlt = std::max(0.0f, std::sqrt(QX * QX + QY * QY + QZ * QZ) - PlanetR);
            const float DR   = std::exp(-QAlt / RayH) * RayMul;
            const float DM   = std::exp(-QAlt / MieH) * MieMul;
            const float DO   = std::max(0.0f, 1.0f - std::abs(QAlt - 25000.0f) / 15000.0f) * OzoneMul;
            const float EM   = SeaLevelMieExtinction * DM;
            ODR += (SeaLevelRayleighScatter[0] * DR + EM + PeakOzoneAbs[0] * DO) * SStep;
            ODG += (SeaLevelRayleighScatter[1] * DR + EM + PeakOzoneAbs[1] * DO) * SStep;
            ODB += (SeaLevelRayleighScatter[2] * DR + EM + PeakOzoneAbs[2] * DO) * SStep;
        }
        return { std::exp(-ODR), std::exp(-ODG), std::exp(-ODB) };
    };

    for (uint32_t S = 0u; S < Steps; ++S)
    {
        const float T   = RayStart + (static_cast<float>(S) + 0.5f) * Step;
        const float PX  = OX + DX * T;
        const float PY  = OY + DY * T;
        const float PZ  = OZ + DZ * T;
        const float Alt = std::max(0.0f, std::sqrt(PX * PX + PY * PY + PZ * PZ) - PlanetR);

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

        const auto SunT = EvalSunTransmittanceAt(PX, PY, PZ);
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

    const float SolarScale = std::max(0.0f, SolarIntensityScale) * 0.30f;
    float OutR = RadR * SolarScale * Medium.SkyTint.R * Medium.SkyBrightnessMultiplier;
    float OutG = RadG * SolarScale * Medium.SkyTint.G * Medium.SkyBrightnessMultiplier;
    float OutB = RadB * SolarScale * Medium.SkyTint.B * Medium.SkyBrightnessMultiplier;

    if (HitPlanet)
    {
        const float HX  = OX + DX * PlanetNear;
        const float HY  = OY + DY * PlanetNear;
        const float HZ  = OZ + DZ * PlanetNear;
        const float HLen = std::max(1.0f, std::sqrt(HX * HX + HY * HY + HZ * HZ));
        const float NX  = HX / HLen;
        const float NY  = HY / HLen;
        const float NZ  = HZ / HLen;

        // Procedural Earth-like continents, oceans, polar ice caps & cyclonic cloud deck
        const float ContN = FbmSky3(NX * 3.1f + 1.2f, NY * 3.1f - 0.8f, NZ * 3.1f + 0.5f, 4u);
        const float Land  = SmoothStepSky(0.49f, 0.54f, ContN);
        const float Moist = FbmSky3(NX * 6.5f - 2.1f, NY * 6.5f + 1.4f, NZ * 6.5f, 3u);
        const float Ice   = SmoothStepSky(0.74f, 0.90f, std::abs(NY) + 0.14f * (ContN - 0.5f));

        const float OceanR = 0.015f, OceanG = 0.055f, OceanB = 0.140f;
        const float LandR  = 0.10f + 0.12f * (1.0f - Moist);
        const float LandG  = 0.14f + 0.08f * Moist;
        const float LandB  = 0.06f + 0.04f * (1.0f - Moist);

        float AlbR = (OceanR * (1.0f - Land) + LandR * Land) * (1.0f - Ice) + 0.78f * Ice;
        float AlbG = (OceanG * (1.0f - Land) + LandG * Land) * (1.0f - Ice) + 0.82f * Ice;
        float AlbB = (OceanB * (1.0f - Land) + LandB * Land) * (1.0f - Ice) + 0.88f * Ice;

        // Zonal swirled cloud patterns moving with SimulationSeconds
        const float Drift  = SimulationSeconds * 0.008f;
        const float Swirl  = FbmSky3(NX * 4.5f + Drift, NY * 4.5f, NZ * 4.5f, 3u);
        const float CloudN = FbmSky3(
            NX * 7.2f + Drift * 1.4f + Swirl * 0.65f,
            NY * 9.0f - Swirl * 0.45f,
            NZ * 7.2f + Drift * 0.7f,
            4u);
        const float CovThresh = 1.0f - std::clamp(CloudCoverage01, 0.05f, 0.95f) * 0.68f;
        const float CloudMask = SmoothStepSky(CovThresh - 0.14f, CovThresh + 0.16f, CloudN);

        const float NdotL   = std::max(0.0f, NX * SunDirection.X + NY * SunDirection.Y + NZ * SunDirection.Z);
        const float TwiWrap = std::clamp((NX * SunDirection.X + NY * SunDirection.Y + NZ * SunDirection.Z + 0.16f) / 0.34f, 0.0f, 1.0f);
        const auto  SurfSunT = EvalSunTransmittanceAt(HX + NX * 120.0f, HY + NY * 120.0f, HZ + NZ * 120.0f);

        // Specular sun glint on ocean
        const float HalfX = SunDirection.X - DX;
        const float HalfY = SunDirection.Y - DY;
        const float HalfZ = SunDirection.Z - DZ;
        const float HNorm = std::max(1.0e-5f, std::sqrt(HalfX * HalfX + HalfY * HalfY + HalfZ * HalfZ));
        const float NdotH = std::max(0.0f, (NX * HalfX + NY * HalfY + NZ * HalfZ) / HNorm);
        const float Spec  = (1.0f - Land) * (1.0f - Ice) * (1.0f - CloudMask) * std::pow(NdotH, 64.0f) * 0.55f;

        const float SurfShade = NdotL * (1.0f - CloudMask * 0.72f);
        const float CloudShade = std::max(0.04f, NdotL * 0.95f + TwiWrap * 0.08f);
        const float LitR = (AlbR * SurfShade + 0.90f * CloudMask * CloudShade + Spec * NdotL) * SurfSunT[0] * SolarScale * 1.45f;
        const float LitG = (AlbG * SurfShade + 0.92f * CloudMask * CloudShade + Spec * NdotL) * SurfSunT[1] * SolarScale * 1.45f;
        const float LitB = (AlbB * SurfShade + 0.96f * CloudMask * CloudShade + Spec * NdotL) * SurfSunT[2] * SolarScale * 1.45f;

        // Subtle warm night-side city lights
        const float NightMask = SmoothStepSky(0.06f, -0.14f, NX * SunDirection.X + NY * SunDirection.Y + NZ * SunDirection.Z) * Land * (1.0f - Ice);
        const float CityNoise = NoiseSky3(NX * 42.0f, NY * 42.0f, NZ * 42.0f);
        const float CityGlow  = SmoothStepSky(0.76f, 0.94f, CityNoise) * NightMask * (1.0f - CloudMask * 0.7f) * 0.045f;

        OutR += TR * (LitR + CityGlow * 1.0f);
        OutG += TG * (LitG + CityGlow * 0.65f);
        OutB += TB * (LitB + CityGlow * 0.25f);

        if (OutSpaceTransmittance != nullptr) *OutSpaceTransmittance = { 0.0f, 0.0f, 0.0f };
    }
    else
    {
        if (OutSpaceTransmittance != nullptr) *OutSpaceTransmittance = { TR, TG, TB };
    }

    return { std::max(0.0f, OutR), std::max(0.0f, OutG), std::max(0.0f, OutB) };
}

} // namespace Frontier::GeometricRaster
