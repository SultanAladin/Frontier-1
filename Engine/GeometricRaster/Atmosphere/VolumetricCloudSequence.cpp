/*============================================================================================================================================
                                                      VOLUMETRICCLOUDSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Raymarched 3D FBM planetary volumetric cloud deck, bounded LocalCloudVolume, Henyey-Greenstein scattering, and solar cloud shadows

#include "Engine/GeometricRaster/Atmosphere/VolumetricCloudSequence.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::GeometricRaster
{

namespace
{

constexpr float ViewerAltM = 100.0f;

[[nodiscard]] float HashInt3(int32_t IX, int32_t IY, int32_t IZ) noexcept
{
    uint32_t N = static_cast<uint32_t>(IX * 374761393 + IY * 668265263 + IZ * 2147483647);
    N = (N ^ (N >> 13u)) * 1274126177u;
    N = N ^ (N >> 16u);
    return static_cast<float>(N) * (1.0f / 4294967296.0f);
}

[[nodiscard]] float SmoothLatticeNoise3(float X, float Y, float Z) noexcept
{
    const int32_t IX = static_cast<int32_t>(std::floor(X));
    const int32_t IY = static_cast<int32_t>(std::floor(Y));
    const int32_t IZ = static_cast<int32_t>(std::floor(Z));
    const float FX   = X - static_cast<float>(IX);
    const float FY   = Y - static_cast<float>(IY);
    const float FZ   = Z - static_cast<float>(IZ);
    const float UX   = FX * FX * (3.0f - 2.0f * FX);
    const float UY   = FY * FY * (3.0f - 2.0f * FY);
    const float UZ   = FZ * FZ * (3.0f - 2.0f * FZ);

    const auto Mix = [](float A, float B, float T) noexcept -> float { return A + (B - A) * T; };
    const float N000 = HashInt3(IX, IY, IZ),     N100 = HashInt3(IX + 1, IY, IZ);
    const float N010 = HashInt3(IX, IY + 1, IZ), N110 = HashInt3(IX + 1, IY + 1, IZ);
    const float N001 = HashInt3(IX, IY, IZ + 1), N101 = HashInt3(IX + 1, IY, IZ + 1);
    const float N011 = HashInt3(IX, IY + 1, IZ + 1), N111 = HashInt3(IX + 1, IY + 1, IZ + 1);

    return Mix(
        Mix(Mix(N000, N100, UX), Mix(N010, N110, UX), UY),
        Mix(Mix(N001, N101, UX), Mix(N011, N111, UX), UY),
        UZ);
}

[[nodiscard]] float CloudFbm3(float X, float Y, float Z) noexcept
{
    float Sum  = 0.0f;
    float Amp  = 0.52f;
    float Freq = 1.0f;
    for (uint32_t Oct = 0u; Oct < 4u; ++Oct)
    {
        Sum  += Amp * SmoothLatticeNoise3(X * Freq, Y * Freq, Z * Freq);
        Freq *= 2.07f;
        Amp  *= 0.48f;
    }
    return Sum;
}

[[nodiscard]] float EvaluateHenyeyGreenstein(float CosTheta, float G) noexcept
{
    const float G2 = G * G;
    const float Den = std::max(1.0e-3f, 1.0f + G2 - 2.0f * G * CosTheta);
    return (1.0f - G2) / (4.0f * Constants::PiFloat * std::pow(Den, 1.5f));
}

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

[[nodiscard]] float SampleDeckDensity(
    float PX, float PY, float PZ,
    float PlanetR, float FloorM, float ThickM,
    float Cov, float Anvil, float DensMul, float FScale,
    float WindX, float WindY, float ShearPerKm) noexcept
{
    const float Alt     = std::sqrt(PX * PX + PY * PY + PZ * PZ) - PlanetR;
    const float H       = std::clamp((Alt - FloorM) / std::max(1.0f, ThickM), 0.0f, 1.0f);
    const float Bot     = std::clamp(H / 0.16f, 0.0f, 1.0f);
    const float Top     = std::clamp((1.0f - H) / std::max(0.05f, 0.28f - 0.16f * Anvil), 0.0f, 1.0f);
    const float Profile = Bot * Top;
    if (Profile <= 0.001f)
    {
        return 0.0f;
    }
    const float ShearShift = ((Alt - FloorM) * 0.001f) * ShearPerKm * 45.0f;
    const float ShapeCov   = std::clamp(Cov + Anvil * H * H * 0.25f, 0.0f, 1.0f);
    const float N          = CloudFbm3((PX + WindX + ShearShift) * FScale, (PY + WindY) * FScale, (Alt - FloorM) * FScale * 1.4f);
    const float NNorm      = std::clamp((N - 0.28f) / 0.44f, 0.0f, 1.0f);
    const float Thresh     = 1.0f - ShapeCov;
    return std::max(0.0f, (NNorm - Thresh) / std::max(0.18f, ShapeCov)) * Profile * DensMul;
}

} // namespace

void VolumetricCloudSequence::ApplyCloudCategoryPreset(
    CloudDeckConfiguration& InOutDeck,
    CloudCategoryType Category) noexcept
{
    InOutDeck.Category = Category;
    switch (Category)
    {
        case CloudCategoryType::Stratus:
            InOutDeck.CloudFraction          = 0.62f;
            InOutDeck.ExtinctionMultiplier   = 1.40f;
            InOutDeck.FeatureScaleMultiplier = 0.55f;
            InOutDeck.CloudFloorMeters       = 900.0f;
            InOutDeck.LayerThicknessMeters   = 450.0f;
            InOutDeck.AnvilTopSpread         = 0.15f;
            break;
        case CloudCategoryType::Stratocumulus:
            InOutDeck.CloudFraction          = 0.50f;
            InOutDeck.ExtinctionMultiplier   = 2.00f;
            InOutDeck.FeatureScaleMultiplier = 0.42f;
            InOutDeck.CloudFloorMeters       = 1200.0f;
            InOutDeck.LayerThicknessMeters   = 800.0f;
            InOutDeck.AnvilTopSpread         = 0.30f;
            break;
        case CloudCategoryType::Cumulus:
            InOutDeck.CloudFraction          = 0.34f;
            InOutDeck.ExtinctionMultiplier   = 2.40f;
            InOutDeck.FeatureScaleMultiplier = 0.35f;
            InOutDeck.CloudFloorMeters       = 1400.0f;
            InOutDeck.LayerThicknessMeters   = 1100.0f;
            InOutDeck.AnvilTopSpread         = 0.50f;
            break;
        case CloudCategoryType::Cumulonimbus:
            InOutDeck.CloudFraction          = 0.58f;
            InOutDeck.ExtinctionMultiplier   = 3.40f;
            InOutDeck.FeatureScaleMultiplier = 0.30f;
            InOutDeck.CloudFloorMeters       = 1100.0f;
            InOutDeck.LayerThicknessMeters   = 4800.0f;
            InOutDeck.AnvilTopSpread         = 0.92f;
            break;
        case CloudCategoryType::Altostratus:
            InOutDeck.CloudFraction          = 0.48f;
            InOutDeck.ExtinctionMultiplier   = 1.30f;
            InOutDeck.FeatureScaleMultiplier = 0.65f;
            InOutDeck.CloudFloorMeters       = 3400.0f;
            InOutDeck.LayerThicknessMeters   = 700.0f;
            InOutDeck.AnvilTopSpread         = 0.20f;
            break;
        case CloudCategoryType::Cirrus:
            InOutDeck.CloudFraction          = 0.28f;
            InOutDeck.ExtinctionMultiplier   = 0.85f;
            InOutDeck.FeatureScaleMultiplier = 0.85f;
            InOutDeck.CloudFloorMeters       = 6800.0f;
            InOutDeck.LayerThicknessMeters   = 420.0f;
            InOutDeck.AnvilTopSpread         = 0.10f;
            break;
    }
}

Mathematics::LinearRGB VolumetricCloudSequence::EvaluateVolumetricClouds(
    const Mathematics::Axis3D&            RayOriginWorld,
    const Mathematics::Axis3D&            RayDirection,
    const Mathematics::Axis3D&            SunDirection,
    const Mathematics::LinearRGB&         SolarIrradiance,
    const Mathematics::LinearRGB&         SkyRadiance,
    const CloudDeckConfiguration&         Deck,
    const BoundedCloudVolume&             LocalVolume,
    const AtmosphericMediumConfiguration& Medium,
    float                                 SimulationSeconds,
    float&                                OutTransmittance) noexcept
{
    OutTransmittance = 1.0f;
    Mathematics::LinearRGB TotalRad { 0.0f, 0.0f, 0.0f };

    const float BearRad = Deck.WindBearingDegrees * Constants::DegToRadFloat;
    const float WindVX  = std::cos(BearRad) * Deck.WindSpeedMetersPerSecond;
    const float WindVY  = -std::sin(BearRad) * Deck.WindSpeedMetersPerSecond;
    const float WindX   = Deck.FollowWindEnabled ? (WindVX * SimulationSeconds) : 0.0f;
    const float WindY   = Deck.FollowWindEnabled ? (WindVY * SimulationSeconds) : 0.0f;

    // 1. Optional bounded LocalCloudVolume AABB raymarch
    if (LocalVolume.LocalEnabled && LocalVolume.CloudFraction > 0.01f && LocalVolume.ExtinctionMultiplier > 0.01f)
    {
        const Mathematics::Axis3D BoxMin = LocalVolume.VolumeCenterMeters - Mathematics::Axis3D{
            LocalVolume.HalfExtentMeters.X, LocalVolume.HalfExtentMeters.Y, LocalVolume.HalfExtentMeters.Z };
        const Mathematics::Axis3D BoxMax = LocalVolume.VolumeCenterMeters + Mathematics::Axis3D{
            LocalVolume.HalfExtentMeters.X, LocalVolume.HalfExtentMeters.Y, LocalVolume.HalfExtentMeters.Z };

        const auto SafeInv = [](float V) noexcept -> float { return (std::abs(V) > 1.0e-7f) ? (1.0f / V) : 1.0e7f; };
        const float InvX = SafeInv(RayDirection.X), InvY = SafeInv(RayDirection.Y), InvZ = SafeInv(RayDirection.Z);
        const float TX0 = (BoxMin.X - RayOriginWorld.X) * InvX, TX1 = (BoxMax.X - RayOriginWorld.X) * InvX;
        const float TY0 = (BoxMin.Y - RayOriginWorld.Y) * InvY, TY1 = (BoxMax.Y - RayOriginWorld.Y) * InvY;
        const float TZ0 = (BoxMin.Z - RayOriginWorld.Z) * InvZ, TZ1 = (BoxMax.Z - RayOriginWorld.Z) * InvZ;
        const float TE  = std::max({ 0.05f, std::min(TX0, TX1), std::min(TY0, TY1), std::min(TZ0, TZ1) });
        const float TX  = std::min({ std::max(TX0, TX1), std::max(TY0, TY1), std::max(TZ0, TZ1) });
        if (TX > TE)
        {
            constexpr uint32_t LocalSteps = 12u;
            const float Dt = (TX - TE) / static_cast<float>(LocalSteps);
            const float CosTheta = Mathematics::EvaluateDot(RayDirection, SunDirection);
            const float Phase = 0.78f * EvaluateHenyeyGreenstein(CosTheta, LocalVolume.ForwardScatterAnisotropy) +
                                0.22f * EvaluateHenyeyGreenstein(CosTheta, -0.25f);
            const float LScale = 1.0f / std::max(4.0f, LocalVolume.FeatureScaleMeters);
            const float LWindX = LocalVolume.FollowWindEnabled ? WindX : 0.0f;
            const float LWindY = LocalVolume.FollowWindEnabled ? WindY : 0.0f;

            for (uint32_t S = 0u; S < LocalSteps; ++S)
            {
                const float T = TE + (static_cast<float>(S) + 0.5f) * Dt;
                const Mathematics::Axis3D P = RayOriginWorld + RayDirection * T;
                const float NX = std::abs(P.X - LocalVolume.VolumeCenterMeters.X) / std::max(0.1f, LocalVolume.HalfExtentMeters.X);
                const float NY = std::abs(P.Y - LocalVolume.VolumeCenterMeters.Y) / std::max(0.1f, LocalVolume.HalfExtentMeters.Y);
                const float NZ = std::abs(P.Z - LocalVolume.VolumeCenterMeters.Z) / std::max(0.1f, LocalVolume.HalfExtentMeters.Z);
                const float EdgeFalloff = std::clamp(1.0f - std::max({ NX, NY, NZ }), 0.0f, 1.0f);
                const float N = CloudFbm3((P.X + LWindX) * LScale, (P.Y + LWindY) * LScale, P.Z * LScale);
                const float Thresh = 1.0f - LocalVolume.CloudFraction;
                const float Den = std::max(0.0f, (N - Thresh) / std::max(0.15f, LocalVolume.CloudFraction)) *
                                  EdgeFalloff * LocalVolume.ExtinctionMultiplier * 0.025f;
                if (Den > 1.0e-5f)
                {
                    const float SegT = std::exp(-Den * Dt);
                    const float Scatter = 1.0f - SegT;
                    const float Amb = 0.35f + 0.45f * std::clamp((P.Z - BoxMin.Z) / std::max(1.0f, BoxMax.Z - BoxMin.Z), 0.0f, 1.0f);
                    TotalRad.R += OutTransmittance * (SolarIrradiance.R * Phase * 2.1f + SkyRadiance.R * Amb * 0.55f) * Scatter;
                    TotalRad.G += OutTransmittance * (SolarIrradiance.G * Phase * 2.1f + SkyRadiance.G * Amb * 0.55f) * Scatter;
                    TotalRad.B += OutTransmittance * (SolarIrradiance.B * Phase * 2.1f + SkyRadiance.B * Amb * 0.55f) * Scatter;
                    OutTransmittance *= SegT;
                }
            }
        }
    }

    // 2. Global Planetary Cloud Deck
    if (!Deck.CloudsEnabled || RayDirection.Z <= 0.008f || Deck.CloudFraction <= 0.01f || Deck.ExtinctionMultiplier <= 0.01f)
    {
        return TotalRad;
    }

    const float PlanetR = std::max(500000.0f, Medium.PlanetRadiusMeters);
    const float FloorM  = std::clamp(Deck.CloudFloorMeters, 100.0f, std::max(200.0f, Deck.DeckCeilingMeters - 100.0f));
    const float ThickM  = std::max(100.0f, Deck.LayerThicknessMeters);
    const float RInner  = PlanetR + FloorM;
    const float ROuter  = PlanetR + FloorM + ThickM;
    const float OrigZ   = PlanetR + ViewerAltM;

    const auto [_1, TEnter] = IntersectOriginSphere(OrigZ, RayDirection.Z, RInner);
    const auto [_2, TExit]  = IntersectOriginSphere(OrigZ, RayDirection.Z, ROuter);
    const float MaxDist = std::min(TExit, 48000.0f);
    if (MaxDist <= TEnter || TEnter < 0.0f)
    {
        return TotalRad;
    }

    constexpr uint32_t Steps = 16u;
    const float Dt       = (MaxDist - TEnter) / static_cast<float>(Steps);
    const float FScale   = 0.00065f / std::max(0.08f, Deck.FeatureScaleMultiplier);
    const float Cov      = std::clamp(Deck.CloudFraction, 0.0f, 1.0f);
    const float DensMul  = std::max(0.0f, Deck.ExtinctionMultiplier) * 0.008f;
    const float Anvil    = std::clamp(Deck.AnvilTopSpread, 0.0f, 1.0f);
    const float G        = std::clamp(Deck.ForwardScatterAnisotropy, -0.5f, 0.9f);
    const float CosTheta = Mathematics::EvaluateDot(RayDirection, SunDirection);
    const float Phase    = 0.78f * EvaluateHenyeyGreenstein(CosTheta, G) + 0.22f * EvaluateHenyeyGreenstein(CosTheta, -0.25f);

    float DeckTrans = 1.0f;
    float DR = 0.0f, DG = 0.0f, DB = 0.0f;
    for (uint32_t S = 0u; S < Steps; ++S)
    {
        const float T  = TEnter + (static_cast<float>(S) + 0.5f) * Dt;
        const float PX = RayDirection.X * T;
        const float PY = RayDirection.Y * T;
        const float PZ = OrigZ + RayDirection.Z * T;
        const float D  = SampleDeckDensity(
            PX, PY, PZ, PlanetR, FloorM, ThickM, Cov, Anvil, DensMul, FScale, WindX, WindY, Deck.WindShearPerKilometer);
        if (D > 1.0e-6f)
        {
            float SunTau = 0.0f;
            const float SStep = (ThickM * 0.55f) / 3.0f;
            for (uint32_t K = 1u; K <= 3u; ++K)
            {
                const float KS = static_cast<float>(K) * SStep;
                SunTau += SampleDeckDensity(
                    PX + SunDirection.X * KS,
                    PY + SunDirection.Y * KS,
                    PZ + SunDirection.Z * KS,
                    PlanetR, FloorM, ThickM, Cov, Anvil, DensMul, FScale, WindX, WindY, Deck.WindShearPerKilometer) * SStep;
            }
            const float SunAtt  = (0.28f + 0.72f * std::exp(-SunTau * 0.65f)) *
                                  (0.45f + 0.55f * (1.0f - std::exp(-SunTau * 2.2f + 0.25f)));
            const float SegT    = std::exp(-D * Dt);
            const float Scatter = 1.0f - SegT;
            const float Alt     = std::sqrt(PX * PX + PY * PY + PZ * PZ) - PlanetR;
            const float H       = std::clamp((Alt - FloorM) / ThickM, 0.0f, 1.0f);
            const float Amb     = 0.45f + 0.55f * H;

            DR += DeckTrans * (SolarIrradiance.R * (SunAtt * Phase * 2.4f + 0.08f) + SkyRadiance.R * Amb * 0.85f) * Scatter;
            DG += DeckTrans * (SolarIrradiance.G * (SunAtt * Phase * 2.4f + 0.08f) + SkyRadiance.G * Amb * 0.85f) * Scatter;
            DB += DeckTrans * (SolarIrradiance.B * (SunAtt * Phase * 2.4f + 0.08f) + SkyRadiance.B * Amb * 0.85f) * Scatter;
            DeckTrans *= SegT;
            if (DeckTrans < 0.015f)
            {
                break;
            }
        }
    }

    const float LowAngleSmooth = std::clamp((RayDirection.Z - 0.008f) / 0.11f, 0.0f, 1.0f);
    const float LowAngleFade   = LowAngleSmooth * LowAngleSmooth * (3.0f - 2.0f * LowAngleSmooth);
    const float Haze = 1.0f - std::exp(-TEnter * 0.000065f);
    DR = (DR * (1.0f - Haze) + SkyRadiance.R * (1.0f - DeckTrans) * Haze) * LowAngleFade;
    DG = (DG * (1.0f - Haze) + SkyRadiance.G * (1.0f - DeckTrans) * Haze) * LowAngleFade;
    DB = (DB * (1.0f - Haze) + SkyRadiance.B * (1.0f - DeckTrans) * Haze) * LowAngleFade;
    DeckTrans = 1.0f - (1.0f - DeckTrans) * LowAngleFade;

    TotalRad.R += OutTransmittance * DR;
    TotalRad.G += OutTransmittance * DG;
    TotalRad.B += OutTransmittance * DB;
    OutTransmittance *= DeckTrans;
    return TotalRad;
}

float VolumetricCloudSequence::EvaluateSolarCloudTransmittance(
    const Mathematics::Axis3D&    WorldPos,
    const Mathematics::Axis3D&    SunDirection,
    const CloudDeckConfiguration& Deck,
    float                         PlanetRadiusMeters,
    float                         SimulationSeconds) noexcept
{
    if (!Deck.CloudsEnabled || !Deck.CloudShadow.ShadowEnabled || SunDirection.Z <= 0.01f)
    {
        return 1.0f;
    }
    const float BearRad = Deck.WindBearingDegrees * Constants::DegToRadFloat;
    const float WindX   = Deck.FollowWindEnabled ? (std::cos(BearRad) * Deck.WindSpeedMetersPerSecond * SimulationSeconds) : 0.0f;
    const float WindY   = Deck.FollowWindEnabled ? (-std::sin(BearRad) * Deck.WindSpeedMetersPerSecond * SimulationSeconds) : 0.0f;

    const float PlanetR = std::max(500000.0f, PlanetRadiusMeters);
    const float FloorM  = std::max(100.0f, Deck.CloudFloorMeters);
    const float ThickM  = std::max(100.0f, Deck.LayerThicknessMeters);
    const float MidAlt  = FloorM + ThickM * 0.45f;
    const float DistM   = MidAlt / std::max(0.05f, SunDirection.Z);
    const float PX      = WorldPos.X + SunDirection.X * DistM;
    const float PY      = WorldPos.Y + SunDirection.Y * DistM;
    const float PZ      = PlanetR + ViewerAltM + MidAlt;

    const float FScale  = (0.00065f / std::max(0.08f, Deck.FeatureScaleMultiplier)) * std::max(0.01f, Deck.CloudShadow.ShadowScale);
    const float Cov     = std::clamp(Deck.CloudFraction * Deck.CloudShadow.ShadowFraction, 0.0f, 1.0f);
    const float DensMul = std::max(0.0f, Deck.ExtinctionMultiplier * Deck.CloudShadow.ShadowDensityMultiplier) * 0.008f;
    const float D = SampleDeckDensity(
        PX, PY, PZ, PlanetR, FloorM, ThickM, Cov, Deck.AnvilTopSpread * Deck.CloudShadow.ShadowAnvil,
        DensMul, FScale, WindX, WindY, Deck.WindShearPerKilometer);

    return std::exp(-D * ThickM * 0.65f);
}

} // namespace Frontier::GeometricRaster
