/*============================================================================================================================================
                                                      VOLUMETRICCLOUDSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Anti-banded 64-step IGN multi-scattering planetary volumetric cloud deck (12 formations), BoundedCloudVolume, and solar cloud shadows

#include "Engine/GeometricRaster/Atmosphere/VolumetricCloudSequence.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::GeometricRaster
{

namespace
{

constexpr float ViewerAltM = 100.0f;

[[nodiscard]] float SmoothStep(float Edge0, float Edge1, float X) noexcept
{
    const float T = std::clamp((X - Edge0) / std::max(1.0e-5f, Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] float Frac(float X) noexcept
{
    return X - std::floor(X);
}

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

[[nodiscard]] float CloudFbm3(float X, float Y, float Z, float HighOctaveFade = 1.0f) noexcept
{
    float Sum  = 0.0f;
    float Amp  = 0.52f;
    float Freq = 1.0f;
    for (uint32_t Oct = 0u; Oct < 5u; ++Oct)
    {
        const float W = (Oct >= 3u) ? HighOctaveFade : 1.0f;
        Sum  += Amp * (W * SmoothLatticeNoise3(X * Freq, Y * Freq, Z * Freq) + (1.0f - W) * 0.5f);
        Freq *= 2.05f;
        Amp  *= 0.48f;
    }
    return Sum;
}

[[nodiscard]] float EvaluateHenyeyGreenstein(float CosTheta, float G) noexcept
{
    const float G2  = G * G;
    const float Den = std::max(1.0e-3f, 1.0f + G2 - 2.0f * G * CosTheta);
    return (1.0f - G2) / (4.0f * Constants::PiFloat * std::pow(Den, 1.5f));
}

[[nodiscard]] std::array<float, 2> IntersectOriginSphere(float OriginZ, float DirZ, float Radius) noexcept
{
    const float B    = OriginZ * DirZ;
    // Factored difference avoids catastrophic float32 cancellation on planetary radii
    const float C    = (OriginZ - Radius) * (OriginZ + Radius);
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
    float WindX, float WindY, float ShearPerKm,
    uint32_t FormationIndex,
    float    DistanceMeters) noexcept
{
    const float Alt     = std::sqrt(PX * PX + PY * PY + PZ * PZ) - PlanetR;
    const float H       = std::clamp((Alt - FloorM) / std::max(1.0f, ThickM), 0.0f, 1.0f);
    const float Bot     = SmoothStep(0.0f, 0.22f, H);
    const float Top     = 1.0f - SmoothStep(std::max(0.20f, 0.72f - 0.24f * Anvil), 1.0f, H);
    float       Profile = Bot * Top;
    if (Profile <= 0.001f)
    {
        return 0.0f;
    }

    const float HighOctFade = 1.0f - SmoothStep(6500.0f, 22000.0f, DistanceMeters);
    const float ShearShift  = ((Alt - FloorM) * 0.001f) * ShearPerKm * 45.0f;
    const float WX = PX + WindX + ShearShift;
    const float WY = PY + WindY;
    const float WZ = Alt - FloorM;

    float ShapeCov = std::clamp(Cov + Anvil * H * H * 0.22f, 0.0f, 1.0f);
    float NNorm    = 0.0f;

    if (FormationIndex == 6u) // Lenticular: large organic wind-aligned stacked laminar mountain-wave lenses
    {
        // Anisotropic macro envelope stretched along wind axis + smooth vertical laminar pancakes
        const float MacroLobes = SmoothLatticeNoise3(WX * FScale * 0.22f, WY * FScale * 0.55f, 1.7f);
        const float SubtleWarp = SmoothLatticeNoise3(WX * FScale * 0.65f, WY * FScale * 0.65f, WZ * FScale * 0.25f);
        const float LaminarSlices = std::pow(0.5f + 0.5f * std::sin(H * Constants::PiFloat * 5.0f - 0.6f), 1.35f);
        const float LensMask = SmoothStep(0.46f, 0.76f, MacroLobes + (SubtleWarp - 0.5f) * 0.22f);
        NNorm   = std::clamp(LensMask * (0.45f + 0.55f * LaminarSlices), 0.0f, 1.0f);
        Profile = SmoothStep(0.04f, 0.22f, H) * (1.0f - SmoothStep(0.75f, 0.96f, H));
    }
    else if (FormationIndex == 7u) // Mammatus: dramatic cellular hanging pouches under storm deck
    {
        const float BaseFbm    = CloudFbm3(WX * FScale * 0.95f, WY * FScale * 0.95f, WZ * FScale * 1.15f, HighOctFade);
        const float CellPhaseX = std::sin(WX * FScale * 3.8f + BaseFbm * 2.6f);
        const float CellPhaseY = std::sin(WY * FScale * 3.8f - BaseFbm * 2.6f);
        const float PouchLobe  = std::pow(std::abs(CellPhaseX * CellPhaseY), 0.65f);
        const float LowerMask  = 1.0f - SmoothStep(0.18f, 0.65f, H);
        NNorm    = std::clamp(((BaseFbm - 0.25f) / 0.44f) * 0.55f + PouchLobe * LowerMask * 0.62f, 0.0f, 1.0f);
        ShapeCov = std::clamp(Cov + LowerMask * 0.14f + Anvil * H * 0.16f, 0.0f, 1.0f);
    }
    else if (FormationIndex == 8u) // KelvinHelmholtz: localized breaking wave-crest curls
    {
        const float MacroBand = SmoothStep(0.40f, 0.72f, SmoothLatticeNoise3(WX * FScale * 0.28f, WY * FScale * 0.28f, 3.1f));
        const float CurlSkew  = 280.0f * std::sin(H * Constants::PiFloat) + 190.0f * H * H;
        const float CurlX     = WX + CurlSkew;
        const float WavePhase = CurlX * FScale * 4.4f + SmoothLatticeNoise3(WX * FScale * 0.9f, WY * FScale * 0.9f, 0.4f) * 2.4f;
        const float Crest     = 0.5f + 0.5f * std::sin(WavePhase + H * 3.6f);
        const float BreakCurl = SmoothStep(0.32f, 0.86f, Crest) * SmoothStep(0.12f, 0.65f, H + 0.24f * Crest);
        const float Billow    = CloudFbm3(CurlX * FScale * 1.05f, WY * FScale * 0.85f, WZ * FScale * 1.4f, HighOctFade);
        NNorm = std::clamp(MacroBand * (BreakCurl * 0.68f + ((Billow - 0.26f) / 0.46f) * 0.42f), 0.0f, 1.0f);
    }
    else if (FormationIndex == 9u) // Asperitas: undulating gravity-wave undersides
    {
        const float Warp   = CloudFbm3(WX * FScale * 0.75f, WY * FScale * 0.75f, WZ * FScale, HighOctFade);
        const float W1     = std::sin(WX * FScale * 4.2f + WY * FScale * 1.6f + Warp * 3.2f);
        const float W2     = std::cos(WY * FScale * 4.6f - WX * FScale * 1.8f - Warp * 2.8f);
        const float Swell  = 0.5f + 0.25f * (W1 + W2);
        const float Detail = CloudFbm3(WX * FScale * 1.15f, WY * FScale * 1.15f, WZ * FScale * 1.3f, HighOctFade);
        NNorm = std::clamp(((Detail - 0.24f) / 0.46f) * 0.62f + Swell * 0.46f, 0.0f, 1.0f);
    }
    else if (FormationIndex == 10u) // Nacreous: smooth stratospheric mother-of-pearl ribbons
    {
        const float MacroEnv = SmoothStep(0.36f, 0.72f, SmoothLatticeNoise3(WX * FScale * 0.35f, WY * FScale * 0.35f, 2.3f));
        const float Ribbon   = 0.5f + 0.5f * std::sin((WX * 0.42f + WY * 0.88f) * FScale * 3.2f +
                                                      SmoothLatticeNoise3(WX * FScale * 0.8f, WY * FScale * 0.8f, 0.5f) * 3.5f);
        const float Silk     = SmoothLatticeNoise3(WX * FScale * 1.2f, WY * FScale * 1.2f, WZ * FScale);
        NNorm = std::clamp(MacroEnv * (Ribbon * 0.68f + Silk * 0.38f), 0.0f, 1.0f);
    }
    else if (FormationIndex == 11u) // Pyrocumulus: isolated towering cauliflower thermal plumes
    {
        const float PlumeColumn = SmoothLatticeNoise3(WX * FScale * 0.36f, WY * FScale * 0.36f, 4.7f);
        const float PlumeMask   = SmoothStep(0.46f, 0.74f, PlumeColumn + H * 0.10f);
        const float Base        = CloudFbm3(WX * FScale * 0.90f, WY * FScale * 0.90f, WZ * FScale * 1.15f, HighOctFade);
        const float BillowRidge = 1.0f - std::abs(SmoothLatticeNoise3(WX * FScale * 2.1f, WY * FScale * 2.1f, WZ * FScale * 2.3f) * 2.0f - 1.0f);
        NNorm    = std::clamp(PlumeMask * (((Base - 0.20f) / 0.44f) * 0.68f + BillowRidge * 0.38f), 0.0f, 1.0f);
        ShapeCov = std::clamp(Cov + 0.14f * H, 0.0f, 1.0f);
    }
    else // Classic 0..5 formations (Stratus, Stratocumulus, Cumulus, Cumulonimbus, Altostratus, Cirrus)
    {
        const float N = CloudFbm3(WX * FScale, WY * FScale, WZ * FScale * 1.4f, HighOctFade);
        NNorm = std::clamp((N - 0.26f) / 0.44f, 0.0f, 1.0f);
    }

    const float Thresh = 1.0f - ShapeCov;
    return std::max(0.0f, (NNorm - Thresh) / std::max(0.18f, ShapeCov)) * Profile * DensMul;
}

} // namespace

void VolumetricCloudSequence::ApplyCloudCategoryPreset(
    CloudDeckConfiguration& InOutDeck,
    CloudCategoryType       Category) noexcept
{
    InOutDeck.Category = Category;
    switch (Category)
    {
        case CloudCategoryType::Stratus:
            InOutDeck.CloudFraction            = 0.62f;
            InOutDeck.ExtinctionMultiplier     = 1.40f;
            InOutDeck.FeatureScaleMultiplier   = 0.55f;
            InOutDeck.CloudFloorMeters         = 900.0f;
            InOutDeck.LayerThicknessMeters     = 450.0f;
            InOutDeck.AnvilTopSpread           = 0.15f;
            InOutDeck.ForwardScatterAnisotropy = 0.35f;
            break;
        case CloudCategoryType::Stratocumulus:
            InOutDeck.CloudFraction            = 0.50f;
            InOutDeck.ExtinctionMultiplier     = 2.00f;
            InOutDeck.FeatureScaleMultiplier   = 0.42f;
            InOutDeck.CloudFloorMeters         = 1200.0f;
            InOutDeck.LayerThicknessMeters     = 850.0f;
            InOutDeck.AnvilTopSpread           = 0.30f;
            InOutDeck.ForwardScatterAnisotropy = 0.42f;
            break;
        case CloudCategoryType::Cumulus:
            InOutDeck.CloudFraction            = 0.38f;
            InOutDeck.ExtinctionMultiplier     = 2.45f;
            InOutDeck.FeatureScaleMultiplier   = 0.35f;
            InOutDeck.CloudFloorMeters         = 1400.0f;
            InOutDeck.LayerThicknessMeters     = 1200.0f;
            InOutDeck.AnvilTopSpread           = 0.50f;
            InOutDeck.ForwardScatterAnisotropy = 0.48f;
            break;
        case CloudCategoryType::Cumulonimbus:
            InOutDeck.CloudFraction            = 0.56f;
            InOutDeck.ExtinctionMultiplier     = 3.20f;
            InOutDeck.FeatureScaleMultiplier   = 0.30f;
            InOutDeck.CloudFloorMeters         = 1100.0f;
            InOutDeck.LayerThicknessMeters     = 4800.0f;
            InOutDeck.AnvilTopSpread           = 0.92f;
            InOutDeck.ForwardScatterAnisotropy = 0.52f;
            break;
        case CloudCategoryType::Altostratus:
            InOutDeck.CloudFraction            = 0.48f;
            InOutDeck.ExtinctionMultiplier     = 1.30f;
            InOutDeck.FeatureScaleMultiplier   = 0.65f;
            InOutDeck.CloudFloorMeters         = 3400.0f;
            InOutDeck.LayerThicknessMeters     = 750.0f;
            InOutDeck.AnvilTopSpread           = 0.20f;
            InOutDeck.ForwardScatterAnisotropy = 0.40f;
            break;
        case CloudCategoryType::Cirrus:
            InOutDeck.CloudFraction            = 0.30f;
            InOutDeck.ExtinctionMultiplier     = 0.85f;
            InOutDeck.FeatureScaleMultiplier   = 0.85f;
            InOutDeck.CloudFloorMeters         = 6800.0f;
            InOutDeck.LayerThicknessMeters     = 420.0f;
            InOutDeck.AnvilTopSpread           = 0.10f;
            InOutDeck.ForwardScatterAnisotropy = 0.55f;
            break;
        case CloudCategoryType::Lenticular:
            InOutDeck.CloudFraction            = 0.62f;
            InOutDeck.ExtinctionMultiplier     = 2.65f;
            InOutDeck.FeatureScaleMultiplier   = 0.38f;
            InOutDeck.CloudFloorMeters         = 2600.0f;
            InOutDeck.LayerThicknessMeters     = 1500.0f;
            InOutDeck.AnvilTopSpread           = 0.10f;
            InOutDeck.ForwardScatterAnisotropy = 0.58f;
            break;
        case CloudCategoryType::Mammatus:
            InOutDeck.CloudFraction            = 0.44f;
            InOutDeck.ExtinctionMultiplier     = 3.00f;
            InOutDeck.FeatureScaleMultiplier   = 0.34f;
            InOutDeck.CloudFloorMeters         = 2300.0f;
            InOutDeck.LayerThicknessMeters     = 1900.0f;
            InOutDeck.AnvilTopSpread           = 0.72f;
            InOutDeck.ForwardScatterAnisotropy = 0.42f;
            break;
        case CloudCategoryType::KelvinHelmholtz:
            InOutDeck.CloudFraction            = 0.54f;
            InOutDeck.ExtinctionMultiplier     = 2.40f;
            InOutDeck.FeatureScaleMultiplier   = 0.44f;
            InOutDeck.CloudFloorMeters         = 2400.0f;
            InOutDeck.LayerThicknessMeters     = 1150.0f;
            InOutDeck.AnvilTopSpread           = 0.36f;
            InOutDeck.ForwardScatterAnisotropy = 0.54f;
            break;
        case CloudCategoryType::Asperitas:
            InOutDeck.CloudFraction            = 0.54f;
            InOutDeck.ExtinctionMultiplier     = 2.85f;
            InOutDeck.FeatureScaleMultiplier   = 0.38f;
            InOutDeck.CloudFloorMeters         = 1700.0f;
            InOutDeck.LayerThicknessMeters     = 1300.0f;
            InOutDeck.AnvilTopSpread           = 0.42f;
            InOutDeck.ForwardScatterAnisotropy = 0.38f;
            break;
        case CloudCategoryType::Nacreous:
            InOutDeck.CloudFraction            = 0.56f;
            InOutDeck.ExtinctionMultiplier     = 1.65f;
            InOutDeck.FeatureScaleMultiplier   = 0.58f;
            InOutDeck.CloudFloorMeters         = 11200.0f;
            InOutDeck.LayerThicknessMeters     = 900.0f;
            InOutDeck.AnvilTopSpread           = 0.18f;
            InOutDeck.ForwardScatterAnisotropy = 0.68f;
            break;
        case CloudCategoryType::Pyrocumulus:
            InOutDeck.CloudFraction            = 0.50f;
            InOutDeck.ExtinctionMultiplier     = 3.50f;
            InOutDeck.FeatureScaleMultiplier   = 0.28f;
            InOutDeck.CloudFloorMeters         = 1050.0f;
            InOutDeck.LayerThicknessMeters     = 5000.0f;
            InOutDeck.AnvilTopSpread           = 0.85f;
            InOutDeck.ForwardScatterAnisotropy = 0.46f;
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
    float&                                OutTransmittance,
    float                                 PixelJitter01) noexcept
{
    OutTransmittance = 1.0f;
    Mathematics::LinearRGB TotalRad { 0.0f, 0.0f, 0.0f };

    const float Ign = (PixelJitter01 >= 0.0f)
        ? std::clamp(PixelJitter01, 0.0f, 1.0f)
        : Frac(52.9829189f * Frac(RayDirection.X * 193.713f + RayDirection.Y * 371.419f + RayDirection.Z * 547.823f));
    // Soft centered jitter eliminates contour slice banding without creating 1px salt-and-pepper stipple
    const float Jitter = 0.5f + 0.72f * (Ign - 0.5f);

    const float BearRad = Deck.WindBearingDegrees * Constants::DegToRadFloat;
    const float WindVX  = std::cos(BearRad) * Deck.WindSpeedMetersPerSecond;
    const float WindVY  = -std::sin(BearRad) * Deck.WindSpeedMetersPerSecond;
    const float WindX   = Deck.FollowWindEnabled ? (WindVX * SimulationSeconds) : 0.0f;
    const float WindY   = Deck.FollowWindEnabled ? (WindVY * SimulationSeconds) : 0.0f;

    // 1. Optional bounded LocalCloudVolume AABB raymarch (24 jittered steps)
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
            constexpr uint32_t LocalSteps = 24u;
            const float Dt = (TX - TE) / static_cast<float>(LocalSteps);
            const float CosTheta = Mathematics::EvaluateDot(RayDirection, SunDirection);
            const float Phase = 0.76f * EvaluateHenyeyGreenstein(CosTheta, LocalVolume.ForwardScatterAnisotropy) +
                                0.24f * EvaluateHenyeyGreenstein(CosTheta, -0.22f);
            const float LScale = 1.0f / std::max(4.0f, LocalVolume.FeatureScaleMeters);
            const float LWindX = LocalVolume.FollowWindEnabled ? WindX : 0.0f;
            const float LWindY = LocalVolume.FollowWindEnabled ? WindY : 0.0f;

            for (uint32_t S = 0u; S < LocalSteps; ++S)
            {
                const float T = TE + (static_cast<float>(S) + Jitter) * Dt;
                const Mathematics::Axis3D P = RayOriginWorld + RayDirection * T;
                const float NX = std::abs(P.X - LocalVolume.VolumeCenterMeters.X) / std::max(0.1f, LocalVolume.HalfExtentMeters.X);
                const float NY = std::abs(P.Y - LocalVolume.VolumeCenterMeters.Y) / std::max(0.1f, LocalVolume.HalfExtentMeters.Y);
                const float NZ = std::abs(P.Z - LocalVolume.VolumeCenterMeters.Z) / std::max(0.1f, LocalVolume.HalfExtentMeters.Z);
                const float EdgeFalloff = 1.0f - SmoothStep(0.65f, 1.0f, std::max({ NX, NY, NZ }));
                const float N = CloudFbm3((P.X + LWindX) * LScale, (P.Y + LWindY) * LScale, P.Z * LScale);
                const float Thresh = 1.0f - LocalVolume.CloudFraction;
                const float Den = std::max(0.0f, (N - Thresh) / std::max(0.15f, LocalVolume.CloudFraction)) *
                                  EdgeFalloff * LocalVolume.ExtinctionMultiplier * 0.028f;
                if (Den > 1.0e-5f)
                {
                    const float SegT = std::exp(-Den * Dt);
                    const float Scatter = 1.0f - SegT;
                    const float Amb = 0.35f + 0.55f * std::clamp((P.Z - BoxMin.Z) / std::max(1.0f, BoxMax.Z - BoxMin.Z), 0.0f, 1.0f);
                    TotalRad.R += OutTransmittance * (SolarIrradiance.R * Phase * 2.2f + SkyRadiance.R * Amb * 0.65f) * Scatter;
                    TotalRad.G += OutTransmittance * (SolarIrradiance.G * Phase * 2.2f + SkyRadiance.G * Amb * 0.65f) * Scatter;
                    TotalRad.B += OutTransmittance * (SolarIrradiance.B * Phase * 2.2f + SkyRadiance.B * Amb * 0.65f) * Scatter;
                    OutTransmittance *= SegT;
                    if (OutTransmittance < 0.01f) break;
                }
            }
        }
    }

    // 2. Global Planetary Cloud Deck (64 jittered steps + LOD FBM + 3-octave multi-scattering + Beer-Powder)
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
    const float MaxDist = std::min(TExit, TEnter + 28000.0f);
    if (MaxDist <= TEnter || TEnter < 0.0f || TEnter > 65000.0f)
    {
        return TotalRad;
    }

    constexpr uint32_t Steps = 64u;
    const float Dt       = (MaxDist - TEnter) / static_cast<float>(Steps);
    const float FScale   = 0.00065f / std::max(0.08f, Deck.FeatureScaleMultiplier);
    const float Cov      = std::clamp(Deck.CloudFraction, 0.0f, 1.0f);
    const float DensMul  = std::max(0.0f, Deck.ExtinctionMultiplier) * 0.0085f;
    const float Anvil    = std::clamp(Deck.AnvilTopSpread, 0.0f, 1.0f);
    const float G        = std::clamp(Deck.ForwardScatterAnisotropy, -0.5f, 0.9f);
    const float CosTheta = Mathematics::EvaluateDot(RayDirection, SunDirection);
    const uint32_t FmtIdx = static_cast<uint32_t>(Deck.Category);

    static constexpr std::array<float, 5u> SunOffsets {{ 0.08f, 0.22f, 0.44f, 0.78f, 1.35f }};
    static constexpr std::array<float, 5u> SunWeights {{ 0.14f, 0.22f, 0.28f, 0.36f, 0.50f }};

    float DeckTrans = 1.0f;
    float DR = 0.0f, DG = 0.0f, DB = 0.0f;
    for (uint32_t S = 0u; S < Steps; ++S)
    {
        const float T  = TEnter + (static_cast<float>(S) + Jitter) * Dt;
        const float PX = RayDirection.X * T;
        const float PY = RayDirection.Y * T;
        const float PZ = OrigZ + RayDirection.Z * T;
        const float D  = SampleDeckDensity(
            PX, PY, PZ, PlanetR, FloorM, ThickM, Cov, Anvil, DensMul, FScale, WindX, WindY, Deck.WindShearPerKilometer, FmtIdx, T);
        if (D > 1.0e-6f)
        {
            float SunTau = 0.0f;
            if (Deck.CloudShadow.ShadowEnabled)
            {
                const float ConeReach = ThickM * 0.42f;
                for (uint32_t K = 0u; K < 5u; ++K)
                {
                    const float KS = SunOffsets[K] * ConeReach;
                    SunTau += SampleDeckDensity(
                        PX + SunDirection.X * KS,
                        PY + SunDirection.Y * KS,
                        PZ + SunDirection.Z * KS,
                        PlanetR, FloorM, ThickM, Cov, Anvil, DensMul, FScale, WindX, WindY, Deck.WindShearPerKilometer, FmtIdx, T) *
                        (SunWeights[K] * ConeReach);
                }
            }

            float MultiScatter = 0.0f;
            float OctAlbedo = 1.0f;
            float OctExt    = 1.0f;
            float OctG      = 1.0f;
            for (uint32_t Oct = 0u; Oct < 3u; ++Oct)
            {
                const float Ph = 0.76f * EvaluateHenyeyGreenstein(CosTheta, G * OctG) +
                                 0.24f * EvaluateHenyeyGreenstein(CosTheta, -0.22f * OctG);
                const float Att = std::exp(-SunTau * 0.85f * OctExt);
                const float Powder = 1.0f - 0.52f * std::exp(-D * 320.0f * OctExt - SunTau * 1.4f);
                MultiScatter += OctAlbedo * Att * Powder * Ph;
                OctAlbedo *= 0.54f;
                OctExt    *= 0.42f;
                OctG      *= 0.68f;
            }

            const float SegT    = std::exp(-D * Dt);
            const float Scatter = (1.0f - SegT) * 0.96f;
            const float Alt     = std::sqrt(PX * PX + PY * PY + PZ * PZ) - PlanetR;
            const float H       = std::clamp((Alt - FloorM) / ThickM, 0.0f, 1.0f);
            const float SkyAmb  = 0.38f + 0.62f * H;
            const float GroundBounce = 0.14f * (1.0f - H) * std::clamp(SunDirection.Z * 2.5f + 0.30f, 0.05f, 1.0f);

            float IrR = 1.0f, IrG = 1.0f, IrB = 1.0f;
            float EmberR = 0.0f, EmberG = 0.0f, EmberB = 0.0f;
            if (FmtIdx == 10u) // Nacreous polar stratospheric pastel mother-of-pearl iridescence
            {
                const float SpecPhase = CosTheta * 9.0f + H * 5.0f + (PX + PY) * 0.00020f;
                IrR = 0.98f + 0.28f * std::sin(SpecPhase);
                IrG = 0.95f + 0.28f * std::sin(SpecPhase + 2.094f);
                IrB = 1.04f + 0.30f * std::sin(SpecPhase + 4.188f);
            }
            else if (FmtIdx == 11u) // Pyrocumulus warm ember glow localized to dense core base
            {
                const float CoreDense   = SmoothStep(DensMul * 0.25f, DensMul * 0.90f, D);
                const float EmberFactor = std::pow(1.0f - H, 2.8f) * CoreDense * 1.25f;
                EmberR = 1.55f * EmberFactor;
                EmberG = 0.42f * EmberFactor;
                EmberB = 0.06f * EmberFactor;
            }

            const float DirectGain = MultiScatter * 2.35f + 0.06f;
            DR += DeckTrans * (SolarIrradiance.R * DirectGain * IrR + SkyRadiance.R * (SkyAmb * 0.85f + GroundBounce) + EmberR) * Scatter;
            DG += DeckTrans * (SolarIrradiance.G * DirectGain * IrG + SkyRadiance.G * (SkyAmb * 0.85f + GroundBounce) + EmberG) * Scatter;
            DB += DeckTrans * (SolarIrradiance.B * DirectGain * IrB + SkyRadiance.B * (SkyAmb * 0.85f + GroundBounce) + EmberB) * Scatter;
            DeckTrans *= SegT;
            if (DeckTrans < 0.01f)
            {
                break;
            }
        }
    }

    const float LowAngleFade = SmoothStep(0.015f, 0.145f, RayDirection.Z);
    const float Haze = 1.0f - std::exp(-TEnter * 0.000042f);
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
    const float DensMul = std::max(0.0f, Deck.ExtinctionMultiplier * Deck.CloudShadow.ShadowDensityMultiplier) * 0.0085f;
    const float D = SampleDeckDensity(
        PX, PY, PZ, PlanetR, FloorM, ThickM, Cov, Deck.AnvilTopSpread * Deck.CloudShadow.ShadowAnvil,
        DensMul, FScale, WindX, WindY, Deck.WindShearPerKilometer, static_cast<uint32_t>(Deck.Category), 8000.0f);

    return 0.32f + 0.68f * std::exp(-D * ThickM * 0.22f);
}

} // namespace Frontier::GeometricRaster
