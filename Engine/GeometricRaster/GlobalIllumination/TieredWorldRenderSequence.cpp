/*============================================================================================================================================
                                                        TIEREDWORLDRENDERSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Dual-Engine Non-GI World Render Sequence (MatCap & OpenPbrDirect + WideTreeRayQuery Shadow Lab & BVH4 Specular Reflections)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometricRaster/Atmosphere/LensFlareSequence.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::GeometricRaster
{

namespace
{

[[nodiscard]] float EncodeLinearToSrgb(float LinearVal) noexcept
{
    const float C = std::clamp(LinearVal, 0.0f, 1.0f);
    return (C <= 0.0031308f) ? (12.92f * C) : (1.055f * std::pow(C, 1.0f / 2.4f) - 0.055f);
}

[[nodiscard]] float EvaluateTriangularSrgbDither(uint32_t PX, uint32_t PY) noexcept
{
    const auto Frac = [](float X) noexcept -> float { return X - std::floor(X); };
    const float IgnA = Frac(52.9829189f * Frac(static_cast<float>(PX) * 0.06711056f + static_cast<float>(PY) * 0.00583715f));
    const float IgnB = Frac(52.9829189f * Frac(static_cast<float>(PX + 17u) * 0.06711056f + static_cast<float>(PY + 31u) * 0.00583715f));
    return (IgnA + IgnB - 1.0f) / 255.0f;
}

[[nodiscard]] uint8_t EncodeLinearToSrgbByteDithered(float LinearVal, float TpdfDither) noexcept
{
    const float S = std::clamp(EncodeLinearToSrgb(LinearVal) + TpdfDither, 0.0f, 1.0f);
    return static_cast<uint8_t>(std::round(S * 255.0f));
}

[[nodiscard]] Mathematics::LinearRGB ApplyAcesFilmic(Mathematics::LinearRGB Hdr) noexcept
{
    const auto Curve = [](float X) noexcept -> float
    {
        const float V = std::max(0.0f, X * 0.85f);
        return std::clamp((V * (2.51f * V + 0.03f)) / (V * (2.43f * V + 0.59f) + 0.14f), 0.0f, 1.0f);
    };
    return { Curve(Hdr.R), Curve(Hdr.G), Curve(Hdr.B) };
}

[[nodiscard]] Mathematics::LinearRGB EvaluateSkyDome(
    Mathematics::Axis3D            Dir,
    Mathematics::LinearRGB         SkyRadiance,
    const WorldSceneSpecification* OptionalScene,
    const AtmosphericSkySequence*  OptionalSky) noexcept
{
    if (OptionalScene != nullptr && OptionalSky != nullptr && OptionalScene->Atmosphere.Medium.AtmosphereEnabled)
    {
        return OptionalSky->SampleSkyViewDirection(Dir);
    }
    const float Up = std::clamp(Dir.Z * 0.5f + 0.5f, 0.0f, 1.0f);
    return SkyRadiance * (0.65f + 0.70f * Up);
}

[[nodiscard]] float EvaluateCloudIgn(uint32_t PX, uint32_t PY) noexcept
{
    const float DotV = static_cast<float>(PX) * 0.06711056f + static_cast<float>(PY) * 0.00583715f;
    const float F1   = DotV - std::floor(DotV);
    const float F2   = 52.9829189f * F1;
    return F2 - std::floor(F2);
}

struct CpuDownsampledCloudField
{
    uint32_t WidthPx  = 0u;
    uint32_t HeightPx = 0u;
    std::vector<std::array<float, 4u>> RgbaT {};

    void Build(
        uint32_t                       FullW,
        uint32_t                       FullH,
        const OrthonormalCameraBasis&  Basis,
        const WorldSceneSpecification& Scene,
        const AtmosphericSkySequence&  SkySeq)
    {
        const auto& Atmo = Scene.Atmosphere;
        if (!Atmo.Medium.AtmosphereEnabled || Atmo.Medium.OrbitalViewEnabled || !Atmo.Clouds.CloudsEnabled || FullW == 0u || FullH == 0u)
        {
            WidthPx = HeightPx = 0u;
            RgbaT.clear();
            return;
        }
        const float Scale = std::clamp(Atmo.Clouds.CloudRenderScale, 0.25f, 1.0f);
        if (Scale >= 0.99f)
        {
            WidthPx = HeightPx = 0u;
            RgbaT.clear();
            return;
        }
        WidthPx  = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<float>(FullW) * Scale)));
        HeightPx = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<float>(FullH) * Scale)));
        RgbaT.assign(static_cast<std::size_t>(WidthPx) * HeightPx, { 0.0f, 0.0f, 0.0f, 1.0f });

        const auto SolarSolved = CelestialEphemerisSolver::SolveSolarEphemeris(Atmo.Solar);
        WorldAtmosphereConfiguration DeckOnlyAtmo = Atmo;
        DeckOnlyAtmo.LocalCloud.LocalEnabled = false;
        DeckOnlyAtmo.LocalClouds.clear();

        for (uint32_t CY = 0u; CY < HeightPx; ++CY)
        {
            const float NdcY = 1.0f - ((static_cast<float>(CY) + 0.5f) / static_cast<float>(HeightPx)) * 2.0f;
            for (uint32_t CX = 0u; CX < WidthPx; ++CX)
            {
                const float NdcX = ((static_cast<float>(CX) + 0.5f) / static_cast<float>(WidthPx)) * 2.0f - 1.0f;
                const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(
                    Basis.ForwardAxis + Basis.RightAxis * (NdcX * Basis.TanHalfFovX) + Basis.UpAxis * (NdcY * Basis.TanHalfFovY));
                if (RayDir.Z <= 0.006f)
                {
                    continue;
                }
                const auto LowAngleSky = SkySeq.SampleSkyViewDirection(
                    Mathematics::NormalizeAxis({ RayDir.X, RayDir.Y, std::max(0.05f, RayDir.Z) }));
                float CloudTrans = 1.0f;
                const auto CloudRad = VolumetricCloudSequence::EvaluateVolumetricCloudsWithWind(
                    Basis.CameraOrigin,
                    RayDir,
                    SolarSolved.WorldDirectionToSun,
                    Scene.Sun.SolarIrradiance,
                    LowAngleSky,
                    DeckOnlyAtmo,
                    CloudTrans,
                    EvaluateCloudIgn(CX, CY));
                RgbaT[static_cast<std::size_t>(CY) * WidthPx + CX] = { CloudRad.R, CloudRad.G, CloudRad.B, CloudTrans };
            }
        }
    }

    [[nodiscard]] bool SampleBilinear(float U, float V, Mathematics::LinearRGB& OutRad, float& OutTrans) const noexcept
    {
        if (WidthPx == 0u || HeightPx == 0u || RgbaT.empty())
        {
            return false;
        }
        const float FX = std::clamp(U * static_cast<float>(WidthPx) - 0.5f, 0.0f, std::max(0.0f, static_cast<float>(WidthPx) - 1.0f));
        const float FY = std::clamp(V * static_cast<float>(HeightPx) - 0.5f, 0.0f, std::max(0.0f, static_cast<float>(HeightPx) - 1.0f));
        const uint32_t X0 = static_cast<uint32_t>(std::floor(FX));
        const uint32_t Y0 = static_cast<uint32_t>(std::floor(FY));
        const uint32_t X1 = std::min(WidthPx - 1u, X0 + 1u);
        const uint32_t Y1 = std::min(HeightPx - 1u, Y0 + 1u);
        const float TX = FX - static_cast<float>(X0);
        const float TY = FY - static_cast<float>(Y0);
        const auto& C00 = RgbaT[static_cast<std::size_t>(Y0) * WidthPx + X0];
        const auto& C10 = RgbaT[static_cast<std::size_t>(Y0) * WidthPx + X1];
        const auto& C01 = RgbaT[static_cast<std::size_t>(Y1) * WidthPx + X0];
        const auto& C11 = RgbaT[static_cast<std::size_t>(Y1) * WidthPx + X1];
        const auto Blend = [&](uint32_t K) noexcept
        {
            const float Top = C00[K] * (1.0f - TX) + C10[K] * TX;
            const float Bot = C01[K] * (1.0f - TX) + C11[K] * TX;
            return Top * (1.0f - TY) + Bot * TY;
        };
        OutRad.R = Blend(0u);
        OutRad.G = Blend(1u);
        OutRad.B = Blend(2u);
        OutTrans = std::clamp(Blend(3u), 0.0f, 1.0f);
        return true;
    }
};

[[nodiscard]] Mathematics::LinearRGB EvaluateBackgroundSkyRadiance(
    const WorldSceneSpecification&  Scene,
    const AtmosphericSkySequence&   SkySeq,
    Mathematics::Axis3D             RayOrigin,
    Mathematics::Axis3D             RayDir,
    float                           PixelJitter01       = -1.0f,
    const CpuDownsampledCloudField* CloudField          = nullptr,
    float                           ScreenU             = -1.0f,
    float                           ScreenV             = -1.0f,
    float                           PixelAngularSizeRad = 0.0f) noexcept
{
    const auto& Atmo = Scene.Atmosphere;
    const bool IsOrbital = Atmo.Medium.OrbitalViewEnabled ||
                           (Atmo.Medium.ObserverAltitudeMeters > Atmo.Medium.AtmosphereThicknessMeters);
    if (!Atmo.Medium.AtmosphereEnabled && !IsOrbital)
    {
        return EvaluateSkyDome(RayDir, Scene.SkyRadiance, nullptr, nullptr);
    }

    const auto SolarSolved = CelestialEphemerisSolver::SolveSolarEphemeris(Atmo.Solar);
    if (IsOrbital)
    {
        const float PlanetR = std::max(500000.0f, Atmo.Medium.PlanetRadiusMeters);
        const float ObsAlt  = std::max(Atmo.Medium.AtmosphereThicknessMeters + 5000.0f,
                                       Atmo.Medium.ObserverAltitudeMeters);
        const float CamZ    = PlanetR + ObsAlt + RayOrigin.Z;
        const float B = RayOrigin.X * RayDir.X + RayOrigin.Y * RayDir.Y + CamZ * RayDir.Z;
        const float C = RayOrigin.X * RayOrigin.X + RayOrigin.Y * RayOrigin.Y + CamZ * CamZ - PlanetR * PlanetR;
        const float Disc = B * B - C;
        const bool HitsPlanet = (Disc >= 0.0f) && ((-B - std::sqrt(Disc)) > 0.0f);

        SolarCelestialConfiguration SpaceScatteringConfig = Atmo.Solar;
        SpaceScatteringConfig.SolarEnabled = false;
        Mathematics::LinearRGB SpaceTrans { 1.0f, 1.0f, 1.0f };
        const auto OrbitalRad = AtmosphericSkySequence::EvaluateOrbitalSpaceRadiance(
            RayOrigin, RayDir, SolarSolved.WorldDirectionToSun, SpaceScatteringConfig, Atmo.Medium,
            Atmo.Clouds.CloudsEnabled, Atmo.Clouds.CloudFraction, Atmo.SimulationSeconds, &Atmo.Clouds, &SpaceTrans);

        if (HitsPlanet)
        {
            return OrbitalRad;
        }

        Mathematics::LinearRGB MoonOccT { 1.0f, 1.0f, 1.0f };
        const auto MoonStarRad = LunarStarfieldSequence::EvaluateCelestialField(
            RayDir, SolarSolved.WorldDirectionToSun, Atmo.Moons, Atmo.Stars, Atmo.SimulationSeconds, &Atmo.Medium, {}, PixelAngularSizeRad, &MoonOccT);
        float GlareEclipseVis = 1.0f;
        const float SunR = std::max(0.0008f, Atmo.Solar.AngularDiameterDegrees * 0.5f * Constants::DegToRadFloat);
        for (std::size_t MIdx = 0u; MIdx < std::min<std::size_t>(Atmo.Moons.size(), LunarStarfieldSequence::MaxMoonSlots); ++MIdx)
        {
            if (!Atmo.Moons[MIdx].Visible) continue;
            const auto MDir = LunarStarfieldSequence::ResolveMoonWorldDirection(Atmo.Moons[MIdx], SolarSolved.WorldDirectionToSun);
            const float AngR = std::max(0.001f, Atmo.Moons[MIdx].AngularDiameterDegrees * 0.5f * Constants::DegToRadFloat);
            const float Sep  = std::acos(std::clamp(Mathematics::EvaluateDot(SolarSolved.WorldDirectionToSun, MDir), -1.0f, 1.0f));
            if (Sep < AngR + SunR)
            {
                const float AreaR = std::min(1.0f, (AngR * AngR) / std::max(1.0e-8f, SunR * SunR));
                const float T     = std::clamp((Sep - std::max(0.0f, AngR - SunR)) / std::max(1.0e-5f, 2.0f * std::min(AngR, SunR)), 0.0f, 1.0f);
                const float Ovl   = 1.0f - T * T * (3.0f - 2.0f * T);
                GlareEclipseVis  *= std::clamp(1.0f - Ovl * AreaR, 0.0f, 1.0f);
            }
        }
        const auto SunRad = AtmosphericSkySequence::EvaluateSolarCoronaRadiance(
            RayDir, SolarSolved.WorldDirectionToSun, Atmo.Solar, { 1.0f, 1.0f, 1.0f }, SolarSolved.EffectiveSolarTint, true,
            &Atmo.Medium, PixelAngularSizeRad, MoonOccT, GlareEclipseVis);
        return {
            OrbitalRad.R + (SunRad.R + MoonStarRad.R) * SpaceTrans.R,
            OrbitalRad.G + (SunRad.G + MoonStarRad.G) * SpaceTrans.G,
            OrbitalRad.B + (SunRad.B + MoonStarRad.B) * SpaceTrans.B,
        };
    }

    const auto SolarTrans = AtmosphericSkySequence::ComputeSolarTransmittance(
        SolarSolved.WorldDirectionToSun, Atmo.Medium, Atmo.Medium.ObserverAltitudeMeters);
    const auto SkyRad = SkySeq.SampleSkyViewDirection(RayDir);
    Mathematics::LinearRGB MoonOccT { 1.0f, 1.0f, 1.0f };
    const auto MoonStarRad = LunarStarfieldSequence::EvaluateCelestialField(
        RayDir, SolarSolved.WorldDirectionToSun, Atmo.Moons, Atmo.Stars, Atmo.SimulationSeconds, &Atmo.Medium, {}, PixelAngularSizeRad, &MoonOccT);
    float GlareEclipseVis = 1.0f;
    const float SunR = std::max(0.0008f, Atmo.Solar.AngularDiameterDegrees * 0.5f * Constants::DegToRadFloat);
    for (std::size_t MIdx = 0u; MIdx < std::min<std::size_t>(Atmo.Moons.size(), LunarStarfieldSequence::MaxMoonSlots); ++MIdx)
    {
        if (!Atmo.Moons[MIdx].Visible) continue;
        const auto MDir = LunarStarfieldSequence::ResolveMoonWorldDirection(Atmo.Moons[MIdx], SolarSolved.WorldDirectionToSun);
        const float AngR = std::max(0.001f, Atmo.Moons[MIdx].AngularDiameterDegrees * 0.5f * Constants::DegToRadFloat);
        const float Sep  = std::acos(std::clamp(Mathematics::EvaluateDot(SolarSolved.WorldDirectionToSun, MDir), -1.0f, 1.0f));
        if (Sep < AngR + SunR)
        {
            const float AreaR = std::min(1.0f, (AngR * AngR) / std::max(1.0e-8f, SunR * SunR));
            const float T     = std::clamp((Sep - std::max(0.0f, AngR - SunR)) / std::max(1.0e-5f, 2.0f * std::min(AngR, SunR)), 0.0f, 1.0f);
            const float Ovl   = 1.0f - T * T * (3.0f - 2.0f * T);
            GlareEclipseVis  *= std::clamp(1.0f - Ovl * AreaR, 0.0f, 1.0f);
        }
    }
    const auto SunRad = AtmosphericSkySequence::EvaluateSolarCoronaRadiance(
        RayDir, SolarSolved.WorldDirectionToSun, Atmo.Solar, SolarTrans, SolarSolved.EffectiveSolarTint, false,
        &Atmo.Medium, PixelAngularSizeRad, MoonOccT, GlareEclipseVis);

    float CloudTrans = 1.0f;
    Mathematics::LinearRGB CloudRad {};
    if (CloudField != nullptr && ScreenU >= 0.0f && ScreenV >= 0.0f &&
        CloudField->SampleBilinear(ScreenU, ScreenV, CloudRad, CloudTrans))
    {
        return {
            (SkyRad.R + SunRad.R + MoonStarRad.R) * CloudTrans + CloudRad.R,
            (SkyRad.G + SunRad.G + MoonStarRad.G) * CloudTrans + CloudRad.G,
            (SkyRad.B + SunRad.B + MoonStarRad.B) * CloudTrans + CloudRad.B,
        };
    }

    const auto LowAngleSky = SkySeq.SampleSkyViewDirection(
        Mathematics::NormalizeAxis({ RayDir.X, RayDir.Y, std::max(0.05f, RayDir.Z) }));
    WorldAtmosphereConfiguration DeckOnlyAtmo = Atmo;
    DeckOnlyAtmo.LocalCloud.LocalEnabled      = false;
    DeckOnlyAtmo.LocalClouds.clear();
    CloudRad = VolumetricCloudSequence::EvaluateVolumetricCloudsWithWind(
        RayOrigin,
        RayDir,
        SolarSolved.WorldDirectionToSun,
        Scene.Sun.SolarIrradiance,
        LowAngleSky,
        DeckOnlyAtmo,
        CloudTrans,
        PixelJitter01);

    return {
        (SkyRad.R + SunRad.R + MoonStarRad.R) * CloudTrans + CloudRad.R,
        (SkyRad.G + SunRad.G + MoonStarRad.G) * CloudTrans + CloudRad.G,
        (SkyRad.B + SunRad.B + MoonStarRad.B) * CloudTrans + CloudRad.B,
    };
}

[[nodiscard]] Mathematics::LinearRGB EvaluateMatCapShading(
    const OrthonormalCameraBasis&         CameraBasis,
    Mathematics::Axis3D                   WorldNormal,
    const MaterialSpace::SurfaceMaterial& Mat) noexcept
{
    const float Vx = Mathematics::EvaluateDot(WorldNormal, CameraBasis.RightAxis);
    const float Vy = Mathematics::EvaluateDot(WorldNormal, CameraBasis.UpAxis);
    const float Vz = std::clamp(Mathematics::EvaluateDot(WorldNormal, CameraBasis.ForwardAxis * -1.0f), 0.0f, 1.0f);
    const Mathematics::Axis3D Vn = Mathematics::NormalizeAxis({ Vx, Vy, Vz }, { 0.0f, 0.0f, 1.0f });

    const Mathematics::Axis3D KeyDir  = Mathematics::NormalizeAxis({ 0.42f, 0.62f, 0.66f });
    const Mathematics::Axis3D FillDir = Mathematics::NormalizeAxis({ -0.68f, 0.25f, 0.68f });
    const Mathematics::Axis3D RimDir  = Mathematics::NormalizeAxis({ 0.0f, 0.85f, 0.52f });

    const float KeyDiff  = std::max(0.0f, Mathematics::EvaluateDot(Vn, KeyDir));
    const float FillDiff = std::max(0.0f, Mathematics::EvaluateDot(Vn, FillDir));
    const float RimGlow  = std::pow(std::clamp(1.0f - Vn.Z, 0.0f, 1.0f), 2.6f) * (0.35f + 0.65f * std::max(0.0f, Mathematics::EvaluateDot(Vn, RimDir)));

    const Mathematics::Axis3D HalfKey = Mathematics::NormalizeAxis(KeyDir + Mathematics::Axis3D{ 0.0f, 0.0f, 1.0f });
    const float SpecPow  = std::lerp(96.0f, 6.0f, std::clamp(Mat.Specular.Roughness, 0.02f, 1.0f));
    const float KeySpec  = std::pow(std::max(0.0f, Mathematics::EvaluateDot(Vn, HalfKey)), SpecPow);
    const float SpecGain = std::lerp(1.45f, 0.18f, std::clamp(Mat.Specular.Roughness, 0.0f, 1.0f));

    const float Metal   = Mat.Specular.Metalness;
    const float DiffW   = 1.0f - Metal * 0.78f;
    const float StudioL = 0.14f + 0.88f * KeyDiff + 0.34f * FillDiff;

    const Mathematics::LinearRGB Emissive = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);
    return {
        Mat.Diffuse.BaseColor.R * (StudioL * DiffW + KeySpec * SpecGain * Metal) + KeySpec * SpecGain * (1.0f - Metal) + RimGlow * 0.26f + Emissive.R,
        Mat.Diffuse.BaseColor.G * (StudioL * DiffW + KeySpec * SpecGain * Metal) + KeySpec * SpecGain * (1.0f - Metal) + RimGlow * 0.28f + Emissive.G,
        Mat.Diffuse.BaseColor.B * (StudioL * DiffW + KeySpec * SpecGain * Metal) + KeySpec * SpecGain * (1.0f - Metal) + RimGlow * 0.32f + Emissive.B,
    };
}

[[nodiscard]] Mathematics::LinearRGB EvaluateDebugChannelColor(
    uint32_t                              DebugChannel,
    Mathematics::Axis3D                   CameraOrigin,
    const AnalyticResolvedSurfaceSample&  Hit,
    const MaterialSpace::SurfaceMaterial& Mat) noexcept
{
    if (DebugChannel == 1u)
    {
        const float D = std::pow(std::clamp(1.0f - Mathematics::EvaluateLength(Hit.WorldPosition - CameraOrigin) / 11.0f, 0.0f, 1.0f), 1.8f);
        return { D, D, D };
    }
    if (DebugChannel == 2u)
    {
        return Mat.Diffuse.BaseColor;
    }
    if (DebugChannel == 3u || DebugChannel == 7u)
    {
        return {
            Hit.TangentFrame.NormalZ.X * 0.5f + 0.5f,
            Hit.TangentFrame.NormalZ.Y * 0.5f + 0.5f,
            Hit.TangentFrame.NormalZ.Z * 0.5f + 0.5f,
        };
    }
    if (DebugChannel == 5u)
    {
        const float M = std::clamp(Mat.Specular.Metalness, 0.0f, 1.0f);
        return { M, M, M };
    }
    if (DebugChannel == 6u)
    {
        const uint32_t SId = static_cast<uint32_t>(Hit.MaterialSlot) + 1u;
        const auto Frac = [](float V) noexcept { return V - std::floor(V); };
        return {
            0.18f + 0.78f * Frac(static_cast<float>(SId) * 0.3819660113f),
            0.18f + 0.78f * Frac(static_cast<float>(SId) * 0.6180339887f + 0.27f),
            0.18f + 0.78f * Frac(static_cast<float>(SId) * 0.7548776662f + 0.53f),
        };
    }
    const float R = Mat.Specular.Roughness;
    return { R, R, R };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                            TIERED WORLD RENDER SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

void TieredWorldRenderSequence::CompileBounceSummaries(WorldSceneSpecification& InOutScene)
{
    const uint32_t SlotCount = InOutScene.MaterialRegister.QuerySlotCount();
    InOutScene.BounceSummaries.resize(SlotCount);
    for (uint32_t Slot = 0u; Slot < SlotCount; ++Slot)
    {
        InOutScene.BounceSummaries[Slot] = MaterialSpace::CompileBounceSlabEntry(
            InOutScene.MaterialRegister.InspectAuthoredMaterial(static_cast<uint16_t>(Slot)));
    }
}

void TieredWorldRenderSequence::SynchronizeWorldAtmosphere(WorldSceneSpecification& InOutScene)
{
    if (!InOutScene.Atmosphere.Medium.AtmosphereEnabled && !InOutScene.Atmosphere.Medium.OrbitalViewEnabled)
    {
        return;
    }

    const auto SolarSolved = CelestialEphemerisSolver::SolveSolarEphemeris(InOutScene.Atmosphere.Solar);
    const float ObsAlt = InOutScene.Atmosphere.Medium.ObserverAltitudeMeters;
    SkySequence.BuildSkyViewLut(
        SolarSolved.WorldDirectionToSun,
        InOutScene.Atmosphere.Medium,
        InOutScene.Atmosphere.Solar.SolarIntensityMultiplier,
        ObsAlt);

    const auto Trans = AtmosphericSkySequence::ComputeSolarTransmittance(
        SolarSolved.WorldDirectionToSun,
        InOutScene.Atmosphere.Medium,
        ObsAlt);

    const bool IsOrbital = InOutScene.Atmosphere.Medium.OrbitalViewEnabled ||
                           (ObsAlt > InOutScene.Atmosphere.Medium.AtmosphereThicknessMeters);
    const float SunVis = IsOrbital ? 1.0f : std::clamp((SolarSolved.ElevationDegrees + 4.0f) / 10.0f, 0.0f, 1.0f);
    InOutScene.Sun.DirectionToSun = SolarSolved.WorldDirectionToSun;
    InOutScene.Sun.SunAngularRadiusRadians =
        std::clamp(InOutScene.Atmosphere.Solar.AngularDiameterDegrees * 0.5f * Constants::DegToRadFloat, 0.002f, 0.25f);
    const float IrradScale =
        InOutScene.Atmosphere.Solar.SolarIntensityMultiplier *
        InOutScene.Atmosphere.Solar.DirectGainMultiplier * 0.044f * SunVis;
    InOutScene.Sun.SolarIrradiance = {
        SolarSolved.EffectiveSolarTint.R * Trans.R * IrradScale,
        SolarSolved.EffectiveSolarTint.G * Trans.G * IrradScale,
        SolarSolved.EffectiveSolarTint.B * Trans.B * IrradScale,
    };

    const auto OverheadSky = SkySequence.SampleSkyViewDirection({ 0.0f, 0.0f, 1.0f });
    InOutScene.SkyRadiance = {
        std::max(IsOrbital ? 0.001f : 0.015f, OverheadSky.R),
        std::max(IsOrbital ? 0.001f : 0.020f, OverheadSky.G),
        std::max(IsOrbital ? 0.002f : 0.032f, OverheadSky.B),
    };
}

void TieredWorldRenderSequence::PrepareWorldScene(WorldSceneSpecification& InOutScene)
{
    SynchronizeWorldAtmosphere(InOutScene);
    CompileBounceSummaries(InOutScene);

    if (InOutScene.CompressedSceneTree.WideEntries.empty() && !InOutScene.SceneTree.Primitives.empty())
    {
        (void)InOutScene.CompressedSceneTree.BuildFromPrimitives(
            InOutScene.SceneTree.Primitives,
            InOutScene.SceneTree.PrimitiveNormals);
    }

    const Mathematics::Axis3D FocusCenter = (InOutScene.SceneTree.RootBoundsMin + InOutScene.SceneTree.RootBoundsMax) * 0.5f;
    SolarShadowSequence::BuildTwoCascadeContactClips(
        InOutScene.Sun, FocusCenter, InOutScene.SceneTree, FineContactClip, CoarseContactClip);
}

WorldRenderOutput TieredWorldRenderSequence::RenderWorldFrame(
    WorldRenderTier                Tier,
    const PerspectiveCameraBasis&  CurrentCamera,
    const PerspectiveCameraBasis&  PreviousCamera,
    const WorldSceneSpecification& PreparedScene)
{
    WorldRenderConfiguration Config {};
    Config.Engine           = WorldRenderEngine::OpenPbrDirect;
    Config.ShadowAlgorithm  = (Tier == WorldRenderTier::Tier2RtxDeterministic)
        ? SolarShadowAlgorithm::WideTreeRayQuery
        : SolarShadowAlgorithm::ContactPcss;
    Config.ReflectionSource = SpecularReflectionSource::WideTreeRaytrace;
    WorldRenderOutput Out   = RenderWorldFrameWithEngine(Config, CurrentCamera, PreviousCamera, PreparedScene);
    Out.ActiveTier          = Tier;
    return Out;
}

WorldRenderOutput TieredWorldRenderSequence::RenderWorldFrameWithEngine(
    const WorldRenderConfiguration& Config,
    const PerspectiveCameraBasis&   CurrentCamera,
    const PerspectiveCameraBasis&   PreviousCamera,
    const WorldSceneSpecification&  PreparedScene)
{
    WorldRenderOutput Result {};
    Result.WidthPx      = std::max(1u, CurrentCamera.ViewportWidthPx);
    Result.HeightPx     = std::max(1u, CurrentCamera.ViewportHeightPx);
    Result.ActiveEngine = Config.Engine;
    Result.ActiveTier   = (Config.ShadowAlgorithm == SolarShadowAlgorithm::WideTreeRayQuery)
        ? WorldRenderTier::Tier2RtxDeterministic
        : WorldRenderTier::Tier1GtxCalibrated;

    const std::size_t TotalPixels = static_cast<std::size_t>(Result.WidthPx) * Result.HeightPx;
    Result.CompositeLinearHdr.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });
    Result.IndirectGiLinearHdr.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });
    Result.DisplayRgba8.assign(TotalPixels * 4u, 255u);
    Result.SdfAllocatedBytes = 0u;

    //====================================================================================================================
    // PASS 1: SHARED 64-BIT PRIMARY VISIBILITY, TWO-PHASE HI-Z CULL & 2-LAYER GLASS PEEL
    //====================================================================================================================
    const DepthPyramidSequence* PrevHiZPtr =
        (PreviousHiZ.QueryMipCount() > 0u &&
         PreviousHiZ.InspectMipLevel(0u).Width == Result.WidthPx &&
         PreviousHiZ.InspectMipLevel(0u).Height == Result.HeightPx)
            ? &PreviousHiZ
            : nullptr;

    Result.VisibilityCull = ClusterCullEncoder::EncodePhaseOneVisibility(
        CurrentCamera, PreparedScene.ClusterInstances, PrevHiZPtr, &PreviousCamera);

    VisibilityBufferTargets VisTargets {};
    VisTargets.AllocateClearedImage(Result.WidthPx, Result.HeightPx);
    VisibilityClusterRasterization::RasterizeOpaqueClusters(
        CurrentCamera, PreparedScene.ClusterInstances,
        Result.VisibilityCull.HardwareClusterBatches,
        Result.VisibilityCull.MicroSoftwareClusters,
        VisTargets);

    DepthPyramidSequence CurrentHiZ {};
    CurrentHiZ.BuildFromVisibilityBuffer64(Result.WidthPx, Result.HeightPx, VisTargets.OpaquePrimary64);

    if (!Result.VisibilityCull.PhaseOneDeferredQueue.empty())
    {
        ClusterCullEncoder::ResolvePhaseTwoDisocclusions(
            CurrentCamera, PreparedScene.ClusterInstances, CurrentHiZ, Result.VisibilityCull);
        VisTargets.AllocateClearedImage(Result.WidthPx, Result.HeightPx);
        VisibilityClusterRasterization::RasterizeOpaqueClusters(
            CurrentCamera, PreparedScene.ClusterInstances,
            Result.VisibilityCull.HardwareClusterBatches,
            Result.VisibilityCull.MicroSoftwareClusters,
            VisTargets);
        CurrentHiZ.BuildFromVisibilityBuffer64(Result.WidthPx, Result.HeightPx, VisTargets.OpaquePrimary64);
    }

    VisibilityClusterRasterization::RasterizeTwoLayerGlassPeel(
        CurrentCamera, PreparedScene.ClusterInstances, Result.VisibilityCull.TransparentGlassBatches, VisTargets);

    const OrthonormalCameraBasis CurBasis  = ComputeCameraBasis(CurrentCamera);
    const OrthonormalCameraBasis PrevBasis = ComputeCameraBasis(PreviousCamera);

    std::vector<AnalyticResolvedSurfaceSample> PrimarySamples(TotalPixels);
    for (uint32_t PY = 0u; PY < Result.HeightPx; ++PY)
    {
        for (uint32_t PX = 0u; PX < Result.WidthPx; ++PX)
        {
            const std::size_t Off = static_cast<std::size_t>(PY) * Result.WidthPx + PX;
            PrimarySamples[Off] = VisibilityMaterialResolve::ReconstructAnalyticPixelHit(
                PX, PY, VisTargets.OpaquePrimary64[Off], CurBasis, PrevBasis, PreparedScene.ClusterInstances);
        }
    }

    const uint32_t MatCount = PreparedScene.MaterialRegister.QuerySlotCount();
    CpuDownsampledCloudField CpuCloudField {};
    if (Config.DebugChannel == 0u && !PreparedScene.Atmosphere.Medium.OrbitalViewEnabled)
    {
        CpuCloudField.Build(Result.WidthPx, Result.HeightPx, CurBasis, PreparedScene, SkySequence);
    }
    const float AspectRatio = static_cast<float>(Result.WidthPx) / static_cast<float>(std::max(1u, Result.HeightPx));
    const ProjectedLensFlareSlice FlareProj =
        (Config.DebugChannel == 0u && PreparedScene.Atmosphere.Medium.AtmosphereEnabled && PreparedScene.Atmosphere.LensFlare.Enabled)
            ? LensFlareSequence::ResolveSolarFlareProjection(
                  CurBasis.CameraOrigin, CurBasis.ForwardAxis, CurBasis.RightAxis, CurBasis.UpAxis,
                  CurBasis.TanHalfFovX, CurBasis.TanHalfFovY, PreparedScene.Atmosphere, &PreparedScene.SceneTree)
            : ProjectedLensFlareSlice{};

    //====================================================================================================================
    // ENGINE 0: MATCAP (Zero GI, Zero Shadow Maps, Zero Reflections)
    //====================================================================================================================
    if (Config.Engine == WorldRenderEngine::MatCap)
    {
        for (uint32_t PY = 0u; PY < Result.HeightPx; ++PY)
        {
            for (uint32_t PX = 0u; PX < Result.WidthPx; ++PX)
            {
                const std::size_t Off = static_cast<std::size_t>(PY) * Result.WidthPx + PX;
                const auto& Hit = PrimarySamples[Off];
                Mathematics::LinearRGB OutHdr { 0.05f, 0.07f, 0.12f };
                if (Hit.ValidHit && Hit.MaterialSlot < MatCount)
                {
                    const auto& Mat = PreparedScene.MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);
                    OutHdr = (Config.DebugChannel != 0u)
                        ? EvaluateDebugChannelColor(Config.DebugChannel, CurBasis.CameraOrigin, Hit, Mat)
                        : EvaluateMatCapShading(CurBasis, Hit.TangentFrame.NormalZ, Mat);
                }
                else if (PreparedScene.Atmosphere.Medium.AtmosphereEnabled && Config.DebugChannel == 0u)
                {
                    const float FU = (static_cast<float>(PX) + 0.5f) / static_cast<float>(Result.WidthPx);
                    const float FV = (static_cast<float>(PY) + 0.5f) / static_cast<float>(Result.HeightPx);
                    const float NdcX = FU * 2.0f - 1.0f;
                    const float NdcY = 1.0f - FV * 2.0f;
                    const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(
                        CurBasis.ForwardAxis + CurBasis.RightAxis * (NdcX * CurBasis.TanHalfFovX) + CurBasis.UpAxis * (NdcY * CurBasis.TanHalfFovY));
                    const float PixelSizeRad = (2.0f * CurBasis.TanHalfFovY) / static_cast<float>(std::max(1u, Result.HeightPx));
                    OutHdr = EvaluateBackgroundSkyRadiance(
                        PreparedScene, SkySequence, CurBasis.CameraOrigin, RayDir, EvaluateCloudIgn(PX, PY), &CpuCloudField, FU, FV, PixelSizeRad);
                }
                if (FlareProj.EffectiveGain > 1.0e-4f && Config.DebugChannel == 0u)
                {
                    const float FU = (static_cast<float>(PX) + 0.5f) / static_cast<float>(Result.WidthPx);
                    const float FV = (static_cast<float>(PY) + 0.5f) / static_cast<float>(Result.HeightPx);
                    OutHdr = OutHdr + LensFlareSequence::EvaluateBakedLensFlare(
                        PreparedScene.Atmosphere.LensFlare, FlareProj, FU, FV, AspectRatio);
                }
                Result.CompositeLinearHdr[Off] = OutHdr;
                const auto Tone = (Config.DebugChannel != 0u) ? OutHdr : ApplyAcesFilmic(OutHdr);
                const float Dth = (Config.DebugChannel != 0u) ? 0.0f : EvaluateTriangularSrgbDither(PX, PY);
                Result.DisplayRgba8[Off * 4u + 0u] = EncodeLinearToSrgbByteDithered(Tone.R, Dth);
                Result.DisplayRgba8[Off * 4u + 1u] = EncodeLinearToSrgbByteDithered(Tone.G, Dth);
                Result.DisplayRgba8[Off * 4u + 2u] = EncodeLinearToSrgbByteDithered(Tone.B, Dth);
                Result.DisplayRgba8[Off * 4u + 3u] = 255u;
            }
        }
        PreviousHiZ = std::move(CurrentHiZ);
        return Result;
    }

    //====================================================================================================================
    // ENGINE 1/2/3: OPENPBR DIRECT + ORDER 2 TIERED REAL-TIME GI (GtxLowEnd Hybrid SDF vs HighEnd 0-MB-SDF BVH4)
    //====================================================================================================================
    const bool Order2Active =
        (Config.Engine == WorldRenderEngine::RealTimeGiGtxLowEnd ||
         Config.Engine == WorldRenderEngine::RealTimeGiHighEnd);
    const bool HighEndZeroSdf = (Config.Engine == WorldRenderEngine::RealTimeGiHighEnd);

    if (Config.Engine == WorldRenderEngine::RealTimeGiGtxLowEnd &&
        PreparedScene.GtxVehicleVolume.ActiveToggle &&
        PreparedScene.GtxVehicleVolume.VolumeAsset != nullptr)
    {
        Result.SdfAllocatedBytes = PreparedScene.GtxVehicleVolume.VolumeAsset->QueryCompactMemoryBytes();
    }

    const SolarShadowAlgorithm EffectiveShadowAlgo = HighEndZeroSdf
        ? SolarShadowAlgorithm::WideTreeRayQuery
        : Config.ShadowAlgorithm;

    DirectLighting::HybridOccluderEnvironment Order2Occluders {};
    GlobalIllumination::StokesContourIrradianceSequence Order2Stokes {};
    GlobalIllumination::SurfelRadiosityLatticeSequence  Order2Surfels {};
    GlobalIllumination::StokesContourTierSettings       StokesSettings {};
    GlobalIllumination::SurfelLatticeTierSettings       SurfelSettings {};
    std::vector<Mathematics::LinearRGB>                 MatEmissionBySlot {};
    std::vector<float>                                  GtxSolarVisibility {};

    if (Order2Active && !PreparedScene.CompressedSceneTree.Primitives.empty())
    {
        Order2Occluders.SceneBvh            = &PreparedScene.CompressedSceneTree;
        Order2Occluders.ForbidDistanceField = HighEndZeroSdf;
        if (!HighEndZeroSdf)
        {
            Order2Occluders.InteriorBoxes    = PreparedScene.GtxInteriorBoxes;
            Order2Occluders.VehiclePlacement = PreparedScene.GtxVehicleVolume;
            Order2Occluders.CharacterRig     = &PreparedScene.GtxCharacterRig;

            // Precompute GtxLowEnd HybridConeShadow (0.28 m BVH4 + C0 48^3 SDF + Capsule Rig) BEFORE OpenPBR direct shading
            if (std::max({ PreparedScene.Sun.SolarIrradiance.R, PreparedScene.Sun.SolarIrradiance.G, PreparedScene.Sun.SolarIrradiance.B }) > 1.0e-4f)
            {
                GtxSolarVisibility.assign(TotalPixels, 1.0f);
                const Mathematics::Axis3D UnitSun = Mathematics::NormalizeAxis(PreparedScene.Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
                for (std::size_t Off = 0u; Off < TotalPixels; ++Off)
                {
                    const auto& Hit = PrimarySamples[Off];
                    if (Hit.ValidHit && Hit.MaterialSlot < MatCount)
                    {
                        GtxSolarVisibility[Off] = DirectLighting::EvaluateGtxHybridDirectionalShadow(
                            Order2Occluders, Hit.WorldPosition, Hit.TangentFrame.NormalZ, UnitSun);
                    }
                }
            }
        }
    }

    Result.DirectLighting = LinearCosineLightIntegration::EvaluateDirectLightingFrameWithAlgorithm(
        EffectiveShadowAlgo,
        CurrentCamera,
        PreparedScene.ClusterInstances,
        VisTargets,
        PreparedScene.MaterialRegister,
        PreparedScene.SceneTree,
        PreparedScene.Sun,
        PreparedScene.AreaLights,
        &FineContactClip,
        &CoarseContactClip,
        {},
        PreparedScene.PointLights,
        { 0.0f, 0.0f, 0.0f },
        PreparedScene.PhotometricRadiators,
        PreparedScene.PhotometricProfiles,
        GtxSolarVisibility,
        Order2Active);

    if (Order2Active && !PreparedScene.CompressedSceneTree.Primitives.empty())
    {
        std::vector<Mathematics::LinearRGB> MatAlbedoBySlot(MatCount);
        MatEmissionBySlot.resize(MatCount);
        for (uint32_t M = 0u; M < MatCount; ++M)
        {
            const auto& AuthMat = PreparedScene.MaterialRegister.InspectAuthoredMaterial(M);
            const float DiffK   = (1.0f - AuthMat.Specular.Metalness) * AuthMat.Diffuse.BaseWeight;
            MatAlbedoBySlot[M]  = {
                AuthMat.Diffuse.BaseColor.R * DiffK,
                AuthMat.Diffuse.BaseColor.G * DiffK,
                AuthMat.Diffuse.BaseColor.B * DiffK,
            };
            MatEmissionBySlot[M] = {
                AuthMat.PorosityLight.EmissionTint.R * AuthMat.PorosityLight.EmissionLuminanceNits,
                AuthMat.PorosityLight.EmissionTint.G * AuthMat.PorosityLight.EmissionLuminanceNits,
                AuthMat.PorosityLight.EmissionTint.B * AuthMat.PorosityLight.EmissionLuminanceNits,
            };
        }

        StokesSettings.HighEndZeroSdfTier = HighEndZeroSdf;
        SurfelSettings.HighEndZeroSdfTier = HighEndZeroSdf;
        SurfelSettings.SunDirection       = Mathematics::NormalizeAxis(PreparedScene.Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
        SurfelSettings.SunRadiance        = {
            PreparedScene.Sun.SolarIrradiance.R,
            PreparedScene.Sun.SolarIrradiance.G,
            PreparedScene.Sun.SolarIrradiance.B,
        };

        if (Config.GiCandidate == RealTimeGiCandidate::StokesContour)
        {
            Order2Stokes.BuildFromScene(
                PreparedScene.CompressedSceneTree.Primitives, MatAlbedoBySlot, MatEmissionBySlot, Order2Occluders, StokesSettings);
        }
        else
        {
            Order2Surfels.BuildRelaxedLatticeFromScene(
                PreparedScene.CompressedSceneTree.Primitives, MatAlbedoBySlot, MatEmissionBySlot, Order2Occluders, SurfelSettings);
        }
    }

    Result.SpecularGlass = SpecularReflectionSequence::EvaluateSpecularRefractionWithSource(
        Config.ReflectionSource,
        CurrentCamera,
        PreparedScene.ClusterInstances,
        VisTargets,
        PrimarySamples,
        PreparedScene.MaterialRegister,
        PreparedScene.BounceSummaries,
        PreparedScene.SceneTree,
        PreparedScene.Sun,
        PreparedScene.SkyRadiance,
        PreparedScene.AreaLights);

    for (uint32_t PY = 0u; PY < Result.HeightPx; ++PY)
    {
        for (uint32_t PX = 0u; PX < Result.WidthPx; ++PX)
        {
            const std::size_t Off = static_cast<std::size_t>(PY) * Result.WidthPx + PX;
            const auto& Hit       = PrimarySamples[Off];

            if (Config.DebugChannel != 0u && Hit.ValidHit && Hit.MaterialSlot < MatCount)
            {
                const auto& Mat = PreparedScene.MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);
                Mathematics::LinearRGB Dbg {};
                if (Config.DebugChannel == 8u && Off < Result.DirectLighting.SolarShadows.FilteredVisibility.size())
                {
                    const float V = Result.DirectLighting.SolarShadows.FilteredVisibility[Off];
                    Dbg = { V, V, V };
                }
                else if (Config.DebugChannel == 9u && Off < Result.DirectLighting.CombinedDiffuseAo.size())
                {
                    const float A = Result.DirectLighting.CombinedDiffuseAo[Off];
                    Dbg = { A, A, A };
                }
                else if (Config.DebugChannel == 10u)
                {
                    Dbg = ApplyAcesFilmic(Result.DirectLighting.DirectHdrRadiance[Off]);
                }
                else if (Config.DebugChannel == 11u)
                {
                    const float SpecAo = Result.DirectLighting.LagardeSpecularAo[Off];
                    Dbg = ApplyAcesFilmic(Result.SpecularGlass.IndirectSpecularHdr[Off] * SpecAo);
                }
                else
                {
                    Dbg = EvaluateDebugChannelColor(Config.DebugChannel, CurBasis.CameraOrigin, Hit, Mat);
                }
                Result.CompositeLinearHdr[Off] = Dbg;
                Result.DisplayRgba8[Off * 4u + 0u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Dbg.R) * 255.0f));
                Result.DisplayRgba8[Off * 4u + 1u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Dbg.G) * 255.0f));
                Result.DisplayRgba8[Off * 4u + 2u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Dbg.B) * 255.0f));
                Result.DisplayRgba8[Off * 4u + 3u] = 255u;
                continue;
            }

            Mathematics::LinearRGB FinalHdr = Result.DirectLighting.DirectHdrRadiance[Off];
            if (Hit.ValidHit && Hit.MaterialSlot < MatCount)
            {
                if (PreparedScene.Atmosphere.Medium.AtmosphereEnabled &&
                    PreparedScene.Atmosphere.Clouds.CloudsEnabled &&
                    PreparedScene.Atmosphere.Clouds.CloudShadow.ShadowEnabled &&
                    PreparedScene.AreaLights.empty())
                {
                    const float CloudShadowT = VolumetricCloudSequence::EvaluateSolarCloudTransmittance(
                        Hit.WorldPosition,
                        PreparedScene.Sun.DirectionToSun,
                        PreparedScene.Atmosphere.Clouds,
                        PreparedScene.Atmosphere.Medium.PlanetRadiusMeters,
                        PreparedScene.Atmosphere.SimulationSeconds);
                    FinalHdr = FinalHdr * CloudShadowT;
                }
                const auto& Mat = PreparedScene.MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);
                const float DiffAo = Result.DirectLighting.CombinedDiffuseAo[Off];
                const float SpecAo = Result.DirectLighting.LagardeSpecularAo[Off];
                const float DiffFactor = (1.0f - Mat.Specular.Metalness) *
                                         (1.0f - Mat.Transmission.TransmissionWeight) *
                                         Mat.Diffuse.BaseWeight;

                if (Order2Active && !PreparedScene.CompressedSceneTree.Primitives.empty())
                {
                    const Mathematics::LinearRGB TotalGiE =
                        (Config.GiCandidate == RealTimeGiCandidate::StokesContour)
                            ? Order2Stokes.EvaluateTotalIrradiance(
                                  Hit.WorldPosition, Hit.TangentFrame.NormalZ, Order2Occluders, StokesSettings)
                            : Order2Surfels.EvaluateTotalIrradiance(
                                  Hit.WorldPosition, Hit.TangentFrame.NormalZ, Order2Occluders, SurfelSettings);

                    const float AoMod = 0.45f + 0.55f * DiffAo;
                    const Mathematics::LinearRGB IndirectRgb {
                        (Mat.Diffuse.BaseColor.R * DiffFactor / Constants::PiFloat) * TotalGiE.R   * AoMod,
                        (Mat.Diffuse.BaseColor.G * DiffFactor / Constants::PiFloat) * TotalGiE.G * AoMod,
                        (Mat.Diffuse.BaseColor.B * DiffFactor / Constants::PiFloat) * TotalGiE.B  * AoMod,
                    };
                    Result.IndirectGiLinearHdr[Off] = IndirectRgb;
                    FinalHdr                        = FinalHdr + IndirectRgb;
                }
                else
                {
                    // Clean hemispherical ambient sky fill (Order 1: Zero GI)
                    const auto SkyFill = EvaluateSkyDome(Hit.TangentFrame.NormalZ, PreparedScene.SkyRadiance, &PreparedScene, &SkySequence);
                    FinalHdr.R += SkyFill.R * 0.22f * Mat.Diffuse.BaseColor.R * DiffFactor * (0.35f + 0.65f * DiffAo);
                    FinalHdr.G += SkyFill.G * 0.22f * Mat.Diffuse.BaseColor.G * DiffFactor * (0.35f + 0.65f * DiffAo);
                    FinalHdr.B += SkyFill.B * 0.22f * Mat.Diffuse.BaseColor.B * DiffFactor * (0.35f + 0.65f * DiffAo);
                }

                if (Config.ReflectionsEnabled)
                {
                    FinalHdr = FinalHdr + Result.SpecularGlass.IndirectSpecularHdr[Off] * SpecAo;
                }
            }
            else if (VisTargets.GlassPeelFront64[Off] == 0ull)
            {
                const float FU = (static_cast<float>(PX) + 0.5f) / static_cast<float>(Result.WidthPx);
                const float FV = (static_cast<float>(PY) + 0.5f) / static_cast<float>(Result.HeightPx);
                const float NdcX = FU * 2.0f - 1.0f;
                const float NdcY = 1.0f - FV * 2.0f;
                const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(
                    CurBasis.ForwardAxis + CurBasis.RightAxis * (NdcX * CurBasis.TanHalfFovX) + CurBasis.UpAxis * (NdcY * CurBasis.TanHalfFovY));
                if (PreparedScene.Atmosphere.Medium.AtmosphereEnabled)
                {
                    const float PixelSizeRad = (2.0f * CurBasis.TanHalfFovY) / static_cast<float>(std::max(1u, Result.HeightPx));
                    FinalHdr = EvaluateBackgroundSkyRadiance(
                        PreparedScene, SkySequence, CurBasis.CameraOrigin, RayDir, EvaluateCloudIgn(PX, PY), &CpuCloudField, FU, FV, PixelSizeRad);
                }
                else
                {
                    FinalHdr = EvaluateSkyDome(RayDir, PreparedScene.SkyRadiance, nullptr, nullptr);
                }
            }

            if (VisTargets.GlassPeelFront64[Off] != 0ull)
            {
                FinalHdr = Result.SpecularGlass.RefractedGlassHdr[Off];
            }

            {
                const float NdcX = ((static_cast<float>(PX) + 0.5f) / static_cast<float>(Result.WidthPx)) * 2.0f - 1.0f;
                const float NdcY = 1.0f - ((static_cast<float>(PY) + 0.5f) / static_cast<float>(Result.HeightPx)) * 2.0f;
                const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(
                    CurBasis.ForwardAxis + CurBasis.RightAxis * (NdcX * CurBasis.TanHalfFovX) + CurBasis.UpAxis * (NdcY * CurBasis.TanHalfFovY));
                const float HitDist = (Hit.ValidHit && VisTargets.GlassPeelFront64[Off] == 0ull)
                    ? Mathematics::EvaluateLength(Hit.WorldPosition - CurBasis.CameraOrigin)
                    : 3500.0f;
                const float CloudDist = (Hit.ValidHit && VisTargets.GlassPeelFront64[Off] == 0ull) ? HitDist : 60000.0f;
                const float Ign = EvaluateCloudIgn(PX, PY);

                const auto& Atmo = PreparedScene.Atmosphere;
                const Mathematics::LinearRGB SkyForVol = Atmo.Medium.AtmosphereEnabled
                    ? SkySequence.SampleSkyViewDirection(RayDir)
                    : EvaluateSkyDome(RayDir, PreparedScene.SkyRadiance, nullptr, nullptr);

                if (Atmo.LocalCloud.LocalEnabled || !Atmo.LocalClouds.empty())
                {
                    float LocalCloudT = 1.0f;
                    const auto LocalCloudR = VolumetricCloudSequence::EvaluateLocalCloudsOverSegment(
                        CurBasis.CameraOrigin, RayDir, CloudDist, PreparedScene.Sun.DirectionToSun,
                        PreparedScene.Sun.SolarIrradiance, SkyForVol, Atmo, LocalCloudT, Ign);
                    FinalHdr = {
                        FinalHdr.R * LocalCloudT + LocalCloudR.R,
                        FinalHdr.G * LocalCloudT + LocalCloudR.G,
                        FinalHdr.B * LocalCloudT + LocalCloudR.B,
                    };
                }

                if (!Atmo.Medium.OrbitalViewEnabled &&
                    (Atmo.Fog.HeightFog.Enabled || Atmo.Fog.AerialFog.Enabled || !Atmo.Fog.LocalFogs.empty()))
                {
                    const auto FogRes = VolumetricFogSequence::EvaluateVolumetricFog(
                        CurBasis.CameraOrigin, RayDir, HitDist, PreparedScene.Sun.DirectionToSun,
                        PreparedScene.Sun.SolarIrradiance, SkyForVol, Atmo.Fog, Atmo.Wind,
                        Atmo.Clouds.WindSpeedMetersPerSecond, Atmo.Clouds.WindBearingDegrees,
                        Atmo.SimulationSeconds, Ign);
                    FinalHdr = {
                        FinalHdr.R * FogRes.Transmittance + FogRes.InScatterHdr.R,
                        FinalHdr.G * FogRes.Transmittance + FogRes.InScatterHdr.G,
                        FinalHdr.B * FogRes.Transmittance + FogRes.InScatterHdr.B,
                    };
                }

                if (Atmo.Fog.GodRays.Enabled && Atmo.Fog.GodRays.Intensity > 1.0e-4f)
                {
                    const auto SceneVisFn = [&](const Mathematics::Axis3D& P, const Mathematics::Axis3D& SDir) noexcept -> float
                    {
                        SpatialAcceleration::RaySegmentQuery Q {};
                        Q.Origin          = P + SDir * 0.02f;
                        Q.UnitDirection   = SDir;
                        Q.MinimumDistance = 0.02f;
                        Q.MaximumDistance = 140.0f;
                        return PreparedScene.SceneTree.QueryRayOccluded(Q) ? 0.0f : 1.0f;
                    };
                    const auto CloudVisFn = [&](const Mathematics::Axis3D& P, const Mathematics::Axis3D& SDir) noexcept -> float
                    {
                        return VolumetricCloudSequence::EvaluateAllSolarCloudTransmittance(P, SDir, Atmo);
                    };
                    FinalHdr = FinalHdr + VolumetricFogSequence::EvaluateVolumetricGodRays(
                        CurBasis.CameraOrigin, RayDir, HitDist, PreparedScene.Sun.DirectionToSun,
                        PreparedScene.Sun.SolarIrradiance, Atmo.Fog, Atmo.Wind,
                        Atmo.Clouds.WindSpeedMetersPerSecond, Atmo.Clouds.WindBearingDegrees,
                        Atmo.SimulationSeconds,
                        SceneVisFn, CloudVisFn, Ign);
                }
            }

            if (FlareProj.EffectiveGain > 1.0e-4f)
            {
                const float FU = (static_cast<float>(PX) + 0.5f) / static_cast<float>(Result.WidthPx);
                const float FV = (static_cast<float>(PY) + 0.5f) / static_cast<float>(Result.HeightPx);
                FinalHdr = FinalHdr + LensFlareSequence::EvaluateBakedLensFlare(
                    PreparedScene.Atmosphere.LensFlare, FlareProj, FU, FV, AspectRatio);
            }

            Result.CompositeLinearHdr[Off] = FinalHdr;
            const auto Tone = ApplyAcesFilmic(FinalHdr);
            const float Dth = (Config.DebugChannel != 0u) ? 0.0f : EvaluateTriangularSrgbDither(PX, PY);
            Result.DisplayRgba8[Off * 4u + 0u] = EncodeLinearToSrgbByteDithered(Tone.R, Dth);
            Result.DisplayRgba8[Off * 4u + 1u] = EncodeLinearToSrgbByteDithered(Tone.G, Dth);
            Result.DisplayRgba8[Off * 4u + 2u] = EncodeLinearToSrgbByteDithered(Tone.B, Dth);
            Result.DisplayRgba8[Off * 4u + 3u] = 255u;
        }
    }

    PreviousHiZ = std::move(CurrentHiZ);
    return Result;
}

} // namespace Frontier::GeometricRaster
