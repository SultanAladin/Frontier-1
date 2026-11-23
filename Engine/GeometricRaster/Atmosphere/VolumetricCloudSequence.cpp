/*============================================================================================================================================
                                                      VOLUMETRICCLOUDSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Single-source Slang PlanetaryClouds CPU execution (generated PlanetaryCloudGenerated.hpp + CloudNoiseVolume.h) + LocalClouds

#include "Engine/GeometricRaster/Atmosphere/VolumetricCloudSequence.h"
#include "Engine/GeometricRaster/Atmosphere/CloudNoiseVolume.h"

#include <algorithm>
#include <array>
#include <cmath>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include "PlanetaryCloudGenerated.hpp"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace Frontier::GeometricRaster
{

namespace
{

constexpr float ViewerAltM = 100.0f;

[[nodiscard]] float Frac(float X) noexcept
{
    return X - std::floor(X);
}

[[nodiscard]] float EvaluateHenyeyGreenstein(float CosTheta, float G) noexcept
{
    const float G2  = G * G;
    const float Den = std::max(1.0e-3f, 1.0f + G2 - 2.0f * G * CosTheta);
    return (1.0f - G2) / (4.0f * Constants::PiFloat * std::pow(Den, 1.5f));
}

[[nodiscard]] GlobalParams_0& AcquireCpuCloudGlobals() noexcept
{
    static GlobalParams_0 Globals = []() -> GlobalParams_0
    {
        const auto& Texels = CondensateNoise::QueryTexels();
        GlobalParams_0 G {};
        G.PlanetaryCloudNoise_0 = { const_cast<uint32_t*>(Texels.data()), Texels.size() };
        return G;
    }();
    return Globals;
}

[[nodiscard]] Vector<float, 3> ToSlangVec3(const Mathematics::Axis3D& V) noexcept
{
    return Vector<float, 3>{ V.X, V.Y, V.Z };
}

[[nodiscard]] Vector<float, 2> PixelFromJitter(float PixelJitter01, const Mathematics::Axis3D& Rd) noexcept
{
    const float J = (PixelJitter01 >= 0.0f)
        ? std::clamp(PixelJitter01, 0.0f, 1.0f)
        : Frac(52.9829189f * Frac(Rd.X * 193.713f + Rd.Y * 371.419f + Rd.Z * 547.823f));
    const uint32_t R = static_cast<uint32_t>(std::clamp(J, 0.0f, 1.0f) * 63.0f);
    return Vector<float, 2>{ static_cast<float>(R), static_cast<float>((R * 17u) & 63u) };
}

[[nodiscard]] CloudSpecification_0 BuildCpuCloudSpec(
    const CloudDeckConfiguration&         Deck,
    const AtmosphericMediumConfiguration& Medium,
    const Mathematics::Axis3D&            SunDir,
    const Mathematics::LinearRGB&         SolarIrradToa,
    const Mathematics::LinearRGB&         SkyAmbient,
    float                                 SimulationSeconds,
    bool                                  ShadowOnly,
    uint32_t                              FormationOverride = 0xFFFFFFFFu) noexcept
{
    const float BearRad = Deck.WindBearingDegrees * Constants::DegToRadFloat;
    const float WindVX  = std::cos(BearRad) * Deck.WindSpeedMetersPerSecond;
    const float WindVY  = -std::sin(BearRad) * Deck.WindSpeedMetersPerSecond;

    CloudSpecification_0 Spec {};
    Spec.RadiusM_0             = std::max(500000.0f, Medium.PlanetRadiusMeters);
    Spec.Coverage_0            = std::clamp(Deck.CloudFraction * (ShadowOnly ? Deck.CloudShadow.ShadowFraction : 1.0f), 0.0f, 1.0f);
    Spec.Extinction_0          = std::max(0.0f, Deck.ExtinctionMultiplier * (ShadowOnly ? Deck.CloudShadow.ShadowDensityMultiplier : 1.0f));
    Spec.FloorM_0              = std::clamp(Deck.CloudFloorMeters, 100.0f, std::max(200.0f, Deck.DeckCeilingMeters - 100.0f));
    Spec.ThicknessM_0          = std::max(100.0f, Deck.LayerThicknessMeters);
    Spec.FeatureScale_0        = std::max(0.08f, Deck.FeatureScaleMultiplier) * (ShadowOnly ? std::max(0.1f, Deck.CloudShadow.ShadowScale) : 1.0f);
    Spec.Anvil_0               = std::clamp(Deck.AnvilTopSpread * (ShadowOnly ? Deck.CloudShadow.ShadowAnvil : 1.0f), 0.0f, 1.0f);
    Spec.PhaseG_0              = std::clamp(Deck.ForwardScatterAnisotropy, -0.5f, 0.92f);
    Spec.Seed_0                = 0.04f;
    Spec.PixelAngle_0          = 0.0020f;
    Spec.UniformExtinction_0   = -1.0f;
    Spec.TimeSeconds_0         = Deck.FollowWindEnabled ? SimulationSeconds : 0.0f;
    Spec.WindVelocityMps_0     = Vector<float, 2>{ WindVX, WindVY };
    Spec.Shear_0               = Deck.WindShearPerKilometer;
    Spec.Veer_0                = Deck.WindVeerDegreesPerKm * Constants::DegToRadFloat;
    Spec.Turbulence_0          = Deck.WindTurbulence;
    Spec.CycloneSpeedMps_0     = 28.0f;
    Spec.CycloneEyeM_0         = 22000.0f;
    Spec.CycloneAxis_0         = normalize_0(Vector<float, 3>{ 0.05f, 0.14f, 0.988f });
    Spec.SunDirection_0        = normalize_0(ToSlangVec3(SunDir));
    Spec.SolarIrradiance_0     = Vector<float, 3>{ SolarIrradToa.R, SolarIrradToa.G, SolarIrradToa.B };
    Spec.SkyAmbient_0          = Vector<float, 3>{ SkyAmbient.R, SkyAmbient.G, SkyAmbient.B };
    Spec.AtmosphereStrengths_0 = Vector<float, 3>{ Medium.RayleighStrength, Medium.MieStrength, Medium.OzoneStrength };
    Spec.AirScaleHeightsM_0    = Vector<float, 2>{ std::max(400.0f, Medium.RayleighScaleHeightMeters), std::max(200.0f, Medium.MieScaleHeightMeters) };
    Spec.Steps_0               = std::clamp(Deck.CloudMarchSteps, 18u, 28u);
    Spec.LightSteps_0          = 4u;
    Spec.Formation_0           = (FormationOverride != 0xFFFFFFFFu) ? FormationOverride : static_cast<uint32_t>(Deck.Category);
    Spec.UseClouds_0           = Deck.CloudsEnabled;
    Spec.UseCloudShadows_0     = Deck.CloudsEnabled && Deck.CloudShadow.ShadowEnabled;
    return Spec;
}

[[nodiscard]] float SampleLocalCloudFieldCpu(const Mathematics::Axis3D& AdvP, float LScale) noexcept
{
    KernelContext_0 Ctx { &AcquireCpuCloudGlobals() };
    const float PeriodM = std::max(12.0f, 2.5f / std::max(1.0e-4f, LScale));
    const Vector<float, 3> Coord { AdvP.X / PeriodM + 0.37f, AdvP.Y / PeriodM + 0.61f, AdvP.Z / PeriodM + 0.19f };
    const Vector<float, 4> S = SampleCloudShape_0(Coord, 1.5f, PeriodM, &Ctx);
    const Vector<float, 4> E = SampleCloudErosion_0(
        Vector<float, 3>{ Coord.x * 1.9f + 0.23f, Coord.y * 1.9f + 0.51f, Coord.z * 1.9f + 0.79f },
        1.5f,
        PeriodM * 0.5f,
        &Ctx);
    return std::clamp(0.62f * S.x + 0.24f * S.y + 0.14f * E.x, 0.0f, 1.0f);
}

[[nodiscard]] Mathematics::LinearRGB EvaluateDeckViaPlanetarySlang(
    const Mathematics::Axis3D&            RayOriginWorld,
    const Mathematics::Axis3D&            RayDirection,
    const Mathematics::Axis3D&            SunDirection,
    const Mathematics::LinearRGB&         SolarIrradianceToa,
    const Mathematics::LinearRGB&         SkyAmbient,
    const CloudDeckConfiguration&         Deck,
    const AtmosphericMediumConfiguration& Medium,
    float                                 SimulationSeconds,
    float&                                OutDeckTransmittance,
    float                                 PixelJitter01) noexcept
{
    OutDeckTransmittance = 1.0f;
    if (!Deck.CloudsEnabled || Deck.CloudFraction <= 0.01f || Deck.ExtinctionMultiplier <= 0.01f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    KernelContext_0      Ctx { &AcquireCpuCloudGlobals() };
    CloudSpecification_0 Spec   = BuildCpuCloudSpec(Deck, Medium, SunDirection, SolarIrradianceToa, SkyAmbient, SimulationSeconds, false);
    const float          ObsAlt = (Medium.ObserverAltitudeMeters > 0.0f) ? std::max(5.0f, Medium.ObserverAltitudeMeters) : ViewerAltM;
    const Vector<float, 3> Origin { RayOriginWorld.X, RayOriginWorld.Y, Spec.RadiusM_0 + ObsAlt + RayOriginWorld.Z };
    const Vector<float, 3> Dir = normalize_0(ToSlangVec3(RayDirection));
    const float            GroundM = PlanetCloudGroundDistance_0(Origin, Dir, Spec.RadiusM_0);
    const Vector<float, 2> Pix = PixelFromJitter(PixelJitter01, RayDirection);

    CloudTransportIntegral_0 Cloud = TracePlanetaryClouds_0(&Spec, Origin, Dir, Pix, GroundM, &Ctx);
    OutDeckTransmittance = std::clamp(Cloud.Transmittance_0, 0.0f, 1.0f);
    if (OutDeckTransmittance >= 0.999999f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const CloudAirSegment_0 Front = IntegrateCloudAir_0(&Spec, Origin, Dir, Cloud.DistanceM_0, 12u, false, &Ctx);
    return {
        Front.Transmittance_1.x * Cloud.Radiance_0.x + (1.0f - Cloud.Transmittance_0) * Front.Radiance_1.x,
        Front.Transmittance_1.y * Cloud.Radiance_0.y + (1.0f - Cloud.Transmittance_0) * Front.Radiance_1.y,
        Front.Transmittance_1.z * Cloud.Radiance_0.z + (1.0f - Cloud.Transmittance_0) * Front.Radiance_1.z,
    };
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

Mathematics::LinearRGB VolumetricCloudSequence::EvaluateLocalCloudsOverSegment(
    const Mathematics::Axis3D&          RayOriginWorld,
    const Mathematics::Axis3D&          RayDirection,
    float                               MaxDistanceMeters,
    const Mathematics::Axis3D&          SunDirection,
    const Mathematics::LinearRGB&       SolarIrradiance,
    const Mathematics::LinearRGB&       SkyRadiance,
    const WorldAtmosphereConfiguration& Atmosphere,
    float&                              InOutTransmittance,
    float                               PixelJitter01) noexcept
{
    Mathematics::LinearRGB TotalRad { 0.0f, 0.0f, 0.0f };
    const float SafeMax = std::max(0.0f, MaxDistanceMeters);
    if (SafeMax <= 0.05f)
    {
        return TotalRad;
    }

    const float Ign = (PixelJitter01 >= 0.0f)
        ? std::clamp(PixelJitter01, 0.0f, 1.0f)
        : Frac(52.9829189f * Frac(RayDirection.X * 193.713f + RayDirection.Y * 371.419f + RayDirection.Z * 547.823f));
    const float Jitter   = Ign;
    const float CosTheta = Mathematics::EvaluateDot(RayDirection, SunDirection);
    const float TimeSec  = Atmosphere.SimulationSeconds;

    auto MarchSingleVolume = [&](const BoundedCloudVolume& Vol)
    {
        if (!Vol.LocalEnabled || Vol.CloudFraction <= 0.01f || Vol.ExtinctionMultiplier <= 0.01f)
        {
            return;
        }
        const float MaxExt = std::max({ Vol.HalfExtentMeters.X, Vol.HalfExtentMeters.Y, Vol.HalfExtentMeters.Z }) * 1.45f;
        const Mathematics::Axis3D BoxMin = Vol.VolumeCenterMeters - Mathematics::Axis3D{ MaxExt, MaxExt, Vol.HalfExtentMeters.Z };
        const Mathematics::Axis3D BoxMax = Vol.VolumeCenterMeters + Mathematics::Axis3D{ MaxExt, MaxExt, Vol.HalfExtentMeters.Z };

        const auto SafeInv = [](float V) noexcept -> float { return (std::abs(V) > 1.0e-7f) ? (1.0f / V) : 1.0e7f; };
        const float InvX = SafeInv(RayDirection.X), InvY = SafeInv(RayDirection.Y), InvZ = SafeInv(RayDirection.Z);
        const float TX0 = (BoxMin.X - RayOriginWorld.X) * InvX, TX1 = (BoxMax.X - RayOriginWorld.X) * InvX;
        const float TY0 = (BoxMin.Y - RayOriginWorld.Y) * InvY, TY1 = (BoxMax.Y - RayOriginWorld.Y) * InvY;
        const float TZ0 = (BoxMin.Z - RayOriginWorld.Z) * InvZ, TZ1 = (BoxMax.Z - RayOriginWorld.Z) * InvZ;
        const float TE  = std::max({ 0.05f, std::min(TX0, TX1), std::min(TY0, TY1), std::min(TZ0, TZ1) });
        const float TX  = std::min({ SafeMax, std::max(TX0, TX1), std::max(TY0, TY1), std::max(TZ0, TZ1) });
        if (TX <= TE)
        {
            return;
        }

        constexpr uint32_t LocalSteps = 24u;
        const float Dt     = (TX - TE) / static_cast<float>(LocalSteps);
        const float LScale = 1.0f / std::max(2.0f, Vol.FeatureScaleMeters);
        const float Phase  = 0.76f * EvaluateHenyeyGreenstein(CosTheta, Vol.ForwardScatterAnisotropy) +
                             0.24f * EvaluateHenyeyGreenstein(CosTheta, -0.22f);
        const float Thresh = 1.0f - std::clamp(Vol.CloudFraction, 0.0f, 1.0f);

        for (uint32_t S = 0u; S < LocalSteps; ++S)
        {
            const float T = TE + (static_cast<float>(S) + Jitter) * Dt;
            const Mathematics::Axis3D P = RayOriginWorld + RayDirection * T;
            const float ShapeMask = AtmosphericWindSolver::EvaluateVolumeShapeMask(
                P, Vol.VolumeCenterMeters, Vol.HalfExtentMeters, Vol.Shape,
                Vol.YawDegrees, Vol.PrismSides, Vol.EdgeSoftness);
            if (ShapeMask <= 1.0e-4f)
            {
                continue;
            }

            const Mathematics::Axis3D AdvP = Vol.FollowWindEnabled
                ? AtmosphericWindSolver::AdvectWorldPointBackward(
                      Atmosphere.Wind,
                      Atmosphere.Clouds.WindSpeedMetersPerSecond,
                      Atmosphere.Clouds.WindBearingDegrees,
                      Vol.WindSource,
                      P,
                      P.Z,
                      TimeSec)
                : P;

            const float N   = SampleLocalCloudFieldCpu(AdvP, LScale);
            const float Den = std::max(0.0f, (N - Thresh) / std::max(0.15f, Vol.CloudFraction)) *
                              ShapeMask * Vol.ExtinctionMultiplier * 0.048f;
            if (Den > 1.0e-5f)
            {
                const float SegT    = std::exp(-Den * Dt);
                const float Scatter = 1.0f - SegT;
                const float Amb     = 0.38f + 0.58f * std::clamp((P.Z - BoxMin.Z) / std::max(1.0f, BoxMax.Z - BoxMin.Z), 0.0f, 1.0f);
                TotalRad.R += InOutTransmittance * (SolarIrradiance.R * Phase * 2.2f + SkyRadiance.R * Amb * 0.68f) * Scatter;
                TotalRad.G += InOutTransmittance * (SolarIrradiance.G * Phase * 2.2f + SkyRadiance.G * Amb * 0.68f) * Scatter;
                TotalRad.B += InOutTransmittance * (SolarIrradiance.B * Phase * 2.2f + SkyRadiance.B * Amb * 0.68f) * Scatter;
                InOutTransmittance *= SegT;
                if (InOutTransmittance < 0.01f)
                {
                    break;
                }
            }
        }
    };

    if (Atmosphere.LocalCloud.LocalEnabled)
    {
        MarchSingleVolume(Atmosphere.LocalCloud);
    }
    for (const auto& Vol : Atmosphere.LocalClouds)
    {
        if (InOutTransmittance < 0.01f)
        {
            break;
        }
        MarchSingleVolume(Vol);
    }

    return TotalRad;
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
    WorldAtmosphereConfiguration Wrap {};
    Wrap.Medium            = Medium;
    Wrap.Clouds            = Deck;
    Wrap.LocalCloud        = LocalVolume;
    Wrap.SimulationSeconds = SimulationSeconds;
    Wrap.Solar.ManualDirectionOverride = true;
    Wrap.Solar.SolarIntensityMultiplier = 22.0f;

    OutTransmittance = 1.0f;
    Mathematics::LinearRGB TotalRad = EvaluateLocalCloudsOverSegment(
        RayOriginWorld, RayDirection, 60000.0f, SunDirection, SolarIrradiance, SkyRadiance,
        Wrap, OutTransmittance, PixelJitter01);

    const float CloudSolarScale = 15.84f * Medium.SkyBrightnessMultiplier;
    const float MaxS = std::max({ SolarIrradiance.R, SolarIrradiance.G, SolarIrradiance.B, 1.0e-4f });
    const Mathematics::LinearRGB ToaIrrad {
        (SolarIrradiance.R / MaxS) * CloudSolarScale,
        (SolarIrradiance.G / MaxS) * CloudSolarScale,
        (SolarIrradiance.B / MaxS) * CloudSolarScale,
    };
    const float AmbGain = CloudSolarScale / 16.0f;
    const Mathematics::LinearRGB SkyAmbient {
        0.37f * AmbGain * Medium.SkyTint.R,
        0.46f * AmbGain * Medium.SkyTint.G,
        0.62f * AmbGain * Medium.SkyTint.B,
    };

    float DeckTrans = 1.0f;
    const auto DeckRad = EvaluateDeckViaPlanetarySlang(
        RayOriginWorld, RayDirection, SunDirection, ToaIrrad, SkyAmbient,
        Deck, Medium, SimulationSeconds, DeckTrans, PixelJitter01);
    TotalRad.R += OutTransmittance * DeckRad.R;
    TotalRad.G += OutTransmittance * DeckRad.G;
    TotalRad.B += OutTransmittance * DeckRad.B;
    OutTransmittance *= DeckTrans;
    return TotalRad;
}

Mathematics::LinearRGB VolumetricCloudSequence::EvaluateVolumetricCloudsWithWind(
    const Mathematics::Axis3D&          RayOriginWorld,
    const Mathematics::Axis3D&          RayDirection,
    const Mathematics::Axis3D&          SunDirection,
    const Mathematics::LinearRGB&       SolarIrradiance,
    const Mathematics::LinearRGB&       SkyRadiance,
    const WorldAtmosphereConfiguration& Atmosphere,
    float&                              OutTransmittance,
    float                               PixelJitter01) noexcept
{
    OutTransmittance = 1.0f;
    Mathematics::LinearRGB TotalRad = EvaluateLocalCloudsOverSegment(
        RayOriginWorld, RayDirection, 60000.0f, SunDirection, SolarIrradiance, SkyRadiance,
        Atmosphere, OutTransmittance, PixelJitter01);

    if (!Atmosphere.Clouds.CloudsEnabled || Atmosphere.Clouds.CloudFraction <= 0.01f || Atmosphere.Clouds.ExtinctionMultiplier <= 0.01f)
    {
        return TotalRad;
    }

    const auto  SolvedSun       = CelestialEphemerisSolver::SolveSolarEphemeris(Atmosphere.Solar);
    const float CloudSolarScale = std::max(0.0f, Atmosphere.Solar.SolarIntensityMultiplier * Atmosphere.Medium.SkyBrightnessMultiplier * 0.72f);
    const Mathematics::LinearRGB ToaIrrad {
        SolvedSun.EffectiveSolarTint.R * CloudSolarScale,
        SolvedSun.EffectiveSolarTint.G * CloudSolarScale,
        SolvedSun.EffectiveSolarTint.B * CloudSolarScale,
    };
    const float AmbGain = CloudSolarScale / 16.0f;
    const Mathematics::LinearRGB SkyAmbient {
        0.37f * AmbGain * Atmosphere.Medium.SkyTint.R,
        0.46f * AmbGain * Atmosphere.Medium.SkyTint.G,
        0.62f * AmbGain * Atmosphere.Medium.SkyTint.B,
    };

    float DeckTrans = 1.0f;
    const auto DeckRad = EvaluateDeckViaPlanetarySlang(
        RayOriginWorld, RayDirection, SunDirection, ToaIrrad, SkyAmbient,
        Atmosphere.Clouds, Atmosphere.Medium, Atmosphere.SimulationSeconds, DeckTrans, PixelJitter01);

    TotalRad.R += OutTransmittance * DeckRad.R;
    TotalRad.G += OutTransmittance * DeckRad.G;
    TotalRad.B += OutTransmittance * DeckRad.B;
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
    AtmosphericMediumConfiguration Med {};
    Med.PlanetRadiusMeters = std::max(500000.0f, PlanetRadiusMeters);
    KernelContext_0 Ctx { &AcquireCpuCloudGlobals() };
    CloudSpecification_0 Spec = BuildCpuCloudSpec(
        Deck, Med, SunDirection, { 15.84f, 15.84f, 15.84f }, { 0.37f, 0.46f, 0.62f }, SimulationSeconds, true);
    const Vector<float, 3> Pos { WorldPos.X, WorldPos.Y, Spec.RadiusM_0 + ViewerAltM + WorldPos.Z };
    const float Tau = CloudSolarOpticalDepth_0(&Spec, Pos, 2u, 24.0f, &Ctx);
    return std::clamp(0.12f + 0.88f * std::exp(-Tau), 0.08f, 1.0f);
}

float VolumetricCloudSequence::EvaluateAllSolarCloudTransmittance(
    const Mathematics::Axis3D&          WorldPos,
    const Mathematics::Axis3D&          SunDirection,
    const WorldAtmosphereConfiguration& Atmosphere) noexcept
{
    if (SunDirection.Z <= 0.01f)
    {
        return 1.0f;
    }
    float Trans = 1.0f;
    const auto& Deck = Atmosphere.Clouds;
    if (Deck.CloudsEnabled && Deck.CloudShadow.ShadowEnabled)
    {
        Trans = EvaluateSolarCloudTransmittance(
            WorldPos, SunDirection, Deck, Atmosphere.Medium.PlanetRadiusMeters, Atmosphere.SimulationSeconds);
    }

    const auto SafeInv = [](float V) noexcept -> float { return (std::abs(V) > 1.0e-7f) ? (1.0f / V) : 1.0e7f; };
    const float InvX = SafeInv(SunDirection.X), InvY = SafeInv(SunDirection.Y), InvZ = SafeInv(SunDirection.Z);
    const float TimeSec = Atmosphere.SimulationSeconds;

    auto OccludeByLocalCloud = [&](const BoundedCloudVolume& Vol)
    {
        if (!Vol.LocalEnabled || Vol.CloudFraction <= 0.01f || Vol.ExtinctionMultiplier <= 0.01f)
        {
            return;
        }
        const float MaxExt = std::max({ Vol.HalfExtentMeters.X, Vol.HalfExtentMeters.Y, Vol.HalfExtentMeters.Z }) * 1.45f;
        const Mathematics::Axis3D BoxMin = Vol.VolumeCenterMeters - Mathematics::Axis3D{ MaxExt, MaxExt, Vol.HalfExtentMeters.Z };
        const Mathematics::Axis3D BoxMax = Vol.VolumeCenterMeters + Mathematics::Axis3D{ MaxExt, MaxExt, Vol.HalfExtentMeters.Z };
        const float TX0 = (BoxMin.X - WorldPos.X) * InvX, TX1 = (BoxMax.X - WorldPos.X) * InvX;
        const float TY0 = (BoxMin.Y - WorldPos.Y) * InvY, TY1 = (BoxMax.Y - WorldPos.Y) * InvY;
        const float TZ0 = (BoxMin.Z - WorldPos.Z) * InvZ, TZ1 = (BoxMax.Z - WorldPos.Z) * InvZ;
        const float TE  = std::max({ 0.02f, std::min(TX0, TX1), std::min(TY0, TY1), std::min(TZ0, TZ1) });
        const float TX  = std::min({ 250.0f, std::max(TX0, TX1), std::max(TY0, TY1), std::max(TZ0, TZ1) });
        if (TX <= TE)
        {
            return;
        }

        constexpr uint32_t ShadowSteps = 6u;
        const float Dt     = (TX - TE) / static_cast<float>(ShadowSteps);
        const float LScale = 1.0f / std::max(2.0f, Vol.FeatureScaleMeters);
        const float Thresh = 1.0f - std::clamp(Vol.CloudFraction, 0.0f, 1.0f);
        float Tau = 0.0f;
        for (uint32_t S = 0u; S < ShadowSteps; ++S)
        {
            const float T = TE + (static_cast<float>(S) + 0.5f) * Dt;
            const Mathematics::Axis3D P = WorldPos + SunDirection * T;
            const float ShapeMask = AtmosphericWindSolver::EvaluateVolumeShapeMask(
                P, Vol.VolumeCenterMeters, Vol.HalfExtentMeters, Vol.Shape,
                Vol.YawDegrees, Vol.PrismSides, Vol.EdgeSoftness);
            if (ShapeMask <= 1.0e-4f)
            {
                continue;
            }
            const Mathematics::Axis3D AdvP = Vol.FollowWindEnabled
                ? AtmosphericWindSolver::AdvectWorldPointBackward(
                      Atmosphere.Wind,
                      Atmosphere.Clouds.WindSpeedMetersPerSecond,
                      Atmosphere.Clouds.WindBearingDegrees,
                      Vol.WindSource,
                      P,
                      P.Z,
                      TimeSec)
                : P;
            const float N   = SampleLocalCloudFieldCpu(AdvP, LScale);
            const float Den = std::max(0.0f, (N - Thresh) / std::max(0.15f, Vol.CloudFraction)) *
                              ShapeMask * Vol.ExtinctionMultiplier * 0.085f;
            Tau += Den * Dt;
        }
        Trans *= (0.14f + 0.86f * std::exp(-Tau));
    };

    if (Atmosphere.LocalCloud.LocalEnabled)
    {
        OccludeByLocalCloud(Atmosphere.LocalCloud);
    }
    for (const auto& Vol : Atmosphere.LocalClouds)
    {
        OccludeByLocalCloud(Vol);
    }
    return std::clamp(Trans, 0.08f, 1.0f);
}

VolumetricCloudSequence::OrbitalCloudEvaluation VolumetricCloudSequence::EvaluateOrbitalPlanetClouds(
    const Mathematics::Axis3D&            CameraPlanetOriginMeters,
    const Mathematics::Axis3D&            RayDirection,
    const Mathematics::Axis3D&            SunDirection,
    const Mathematics::LinearRGB&         EffectiveSolarTint,
    float                                 SolarIntensityMultiplier,
    const AtmosphericMediumConfiguration& Medium,
    const CloudDeckConfiguration&         Deck,
    float                                 SimulationSeconds,
    float                                 PixelJitter01) noexcept
{
    OrbitalCloudEvaluation Out {};
    KernelContext_0 Ctx { &AcquireCpuCloudGlobals() };

    const float CloudSolarScale = std::max(0.0f, SolarIntensityMultiplier * Medium.SkyBrightnessMultiplier * 0.72f);
    const Mathematics::LinearRGB ToaIrrad {
        EffectiveSolarTint.R * CloudSolarScale,
        EffectiveSolarTint.G * CloudSolarScale,
        EffectiveSolarTint.B * CloudSolarScale,
    };
    const float AmbGain = CloudSolarScale / 16.0f;
    const Mathematics::LinearRGB SkyAmbient {
        0.37f * AmbGain * Medium.SkyTint.R,
        0.46f * AmbGain * Medium.SkyTint.G,
        0.62f * AmbGain * Medium.SkyTint.B,
    };

    const uint32_t BaseFmt = static_cast<uint32_t>(Deck.Category);
    CloudSpecification_0 OrbSpec = BuildCpuCloudSpec(
        Deck, Medium, SunDirection, ToaIrrad, SkyAmbient, SimulationSeconds, false, (BaseFmt == 2u) ? 12u : BaseFmt);
    OrbSpec.Steps_0      = std::clamp(OrbSpec.Steps_0, 24u, 36u);
    OrbSpec.LightSteps_0 = 4u;

    const Vector<float, 3> Origin = ToSlangVec3(CameraPlanetOriginMeters);
    const Vector<float, 3> Dir    = normalize_0(ToSlangVec3(RayDirection));
    const float            GroundM = PlanetCloudGroundDistance_0(Origin, Dir, OrbSpec.RadiusM_0);
    Out.GroundDistanceM = GroundM;

    CloudTransportIntegral_0 Cloud = TracePlanetaryClouds_0(&OrbSpec, Origin, Dir, PixelFromJitter(PixelJitter01, RayDirection), GroundM, &Ctx);
    const CloudAirSegment_0  Air   = IntegrateCloudAir_0(&OrbSpec, Origin, Dir, GroundM, 24u, false, &Ctx);
    Out.AirRadianceHdr   = { Air.Radiance_1.x, Air.Radiance_1.y, Air.Radiance_1.z };
    Out.AirTransmittance = { Air.Transmittance_1.x, Air.Transmittance_1.y, Air.Transmittance_1.z };
    Out.Transmittance    = std::clamp(Cloud.Transmittance_0, 0.0f, 1.0f);

    if (Out.Transmittance < 0.999999f)
    {
        const Vector<float, 3> Cr = CloudRadianceThroughAir_0(&OrbSpec, Origin, Dir, &Cloud, 16u, &Ctx);
        Out.RadianceHdr = { Cr.x, Cr.y, Cr.z };
    }

    if (GroundM < 1.0e8f && Deck.CloudsEnabled)
    {
        const Vector<float, 3> HitP = Origin + Dir * Vector<float, 3>(GroundM);
        const float FootprintM = std::max(1.0f, GroundM * OrbSpec.PixelAngle_0);
        const float Tau        = CloudSolarOpticalDepth_0(&OrbSpec, HitP, 8u, FootprintM, &Ctx);
        Out.SurfaceCloudShadow = std::exp(-Tau);
    }

    return Out;
}

} // namespace Frontier::GeometricRaster
