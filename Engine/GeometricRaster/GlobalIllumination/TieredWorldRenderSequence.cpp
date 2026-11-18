/*============================================================================================================================================
                                                        TIEREDWORLDRENDERSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Unified 5-Engine World Render Sequence (MatCap, OpenPbrDirect, DdgiRaytracing, SdfGlobalIllumination, DeterministicPathTrace)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometricRaster/Atmosphere/LensFlareSequence.h"
#include "Engine/GeometrySpace/Analysis/CurvatureAnalysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

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

[[nodiscard]] uint32_t PcgNext(uint32_t& State) noexcept
{
    State = State * 747796405u + 2891336453u;
    const uint32_t Word = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return (Word >> 22u) ^ Word;
}

[[nodiscard]] float PcgFloat01(uint32_t& State) noexcept
{
    return static_cast<float>(PcgNext(State) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

[[nodiscard]] Mathematics::LinearRGB EvaluateSkyDome(
    Mathematics::Axis3D           Dir,
    Mathematics::LinearRGB        SkyRadiance,
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
    const WorldSceneSpecification&    Scene,
    const AtmosphericSkySequence&     SkySeq,
    Mathematics::Axis3D               RayOrigin,
    Mathematics::Axis3D               RayDir,
    float                             PixelJitter01 = -1.0f,
    const CpuDownsampledCloudField*   CloudField    = nullptr,
    float                             ScreenU       = -1.0f,
    float                             ScreenV       = -1.0f) noexcept
{
    const auto& Atmo = Scene.Atmosphere;
    if (!Atmo.Medium.AtmosphereEnabled)
    {
        return EvaluateSkyDome(RayDir, Scene.SkyRadiance, nullptr, nullptr);
    }

    const auto SolarSolved = CelestialEphemerisSolver::SolveSolarEphemeris(Atmo.Solar);
    if (Atmo.Medium.OrbitalViewEnabled)
    {
        Mathematics::LinearRGB SpaceTrans { 1.0f, 1.0f, 1.0f };
        const auto PlanetLimb = AtmosphericSkySequence::EvaluateOrbitalSpaceRadiance(
            RayDir,
            RayOrigin,
            SolarSolved.WorldDirectionToSun,
            Atmo.Medium,
            Atmo.Solar.SolarIntensityMultiplier,
            Atmo.SimulationSeconds,
            Atmo.Clouds.CloudFraction,
            &SpaceTrans);
        const auto SunRad = AtmosphericSkySequence::EvaluateSolarCoronaRadiance(
            RayDir,
            SolarSolved.WorldDirectionToSun,
            Atmo.Solar,
            { 1.0f, 1.0f, 1.0f },
            SolarSolved.EffectiveSolarTint,
            true);
        const auto MoonStarRad = LunarStarfieldSequence::EvaluateCelestialField(
            RayDir, SolarSolved.WorldDirectionToSun, Atmo.Moons, Atmo.Stars, Atmo.SimulationSeconds, &Atmo.Medium);
        return {
            PlanetLimb.R + (SunRad.R + MoonStarRad.R) * SpaceTrans.R,
            PlanetLimb.G + (SunRad.G + MoonStarRad.G) * SpaceTrans.G,
            PlanetLimb.B + (SunRad.B + MoonStarRad.B) * SpaceTrans.B,
        };
    }

    const auto SolarTrans  = AtmosphericSkySequence::ComputeSolarTransmittance(
        SolarSolved.WorldDirectionToSun, Atmo.Medium);
    const auto SkyRad      = SkySeq.SampleSkyViewDirection(RayDir);
    const auto SunRad      = AtmosphericSkySequence::EvaluateSolarCoronaRadiance(
        RayDir, SolarSolved.WorldDirectionToSun, Atmo.Solar, SolarTrans, SolarSolved.EffectiveSolarTint, false);
    const auto MoonStarRad = LunarStarfieldSequence::EvaluateCelestialField(
        RayDir, SolarSolved.WorldDirectionToSun, Atmo.Moons, Atmo.Stars, Atmo.SimulationSeconds, &Atmo.Medium);

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
    // View-space normal (X=right, Y=up, Z=toward camera) matching MatCapEvaluation.slang
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

void ApplyDiffuseGiBilateralFilter(
    uint32_t                                       WidthPx,
    uint32_t                                       HeightPx,
    Mathematics::Axis3D                            CameraOrigin,
    std::span<const AnalyticResolvedSurfaceSample> PrimarySamples,
    std::span<const Mathematics::LinearRGB>        RawGi,
    std::vector<Mathematics::LinearRGB>&           OutFiltered)
{
    OutFiltered.assign(RawGi.begin(), RawGi.end());
    if (WidthPx == 0u || HeightPx == 0u)
    {
        return;
    }

    constexpr int Radius = 3; // 7x7 joint bilateral spatial filter
    for (uint32_t PY = 0u; PY < HeightPx; ++PY)
    {
        for (uint32_t PX = 0u; PX < WidthPx; ++PX)
        {
            const std::size_t CenterOff = static_cast<std::size_t>(PY) * WidthPx + PX;
            const auto& CenterHit       = PrimarySamples[CenterOff];
            if (!CenterHit.ValidHit)
            {
                continue;
            }
            const float CenterDepth = Mathematics::EvaluateLength(CenterHit.WorldPosition - CameraOrigin);

            Mathematics::LinearRGB Accum {};
            float WeightSum = 0.0f;
            for (int DY = -Radius; DY <= Radius; ++DY)
            {
                const int NY = std::clamp(static_cast<int>(PY) + DY, 0, static_cast<int>(HeightPx) - 1);
                for (int DX = -Radius; DX <= Radius; ++DX)
                {
                    const int NX = std::clamp(static_cast<int>(PX) + DX, 0, static_cast<int>(WidthPx) - 1);
                    const std::size_t NOff = static_cast<std::size_t>(NY) * WidthPx + static_cast<uint32_t>(NX);
                    const auto& NHit       = PrimarySamples[NOff];
                    if (!NHit.ValidHit)
                    {
                        continue;
                    }
                    const float NDepth = Mathematics::EvaluateLength(NHit.WorldPosition - CameraOrigin);

                    const float SpaceDist2 = static_cast<float>(DX * DX + DY * DY);
                    const float WSpace     = std::exp(-SpaceDist2 / (2.0f * 2.1f * 2.1f));
                    const float NrmDot     = std::max(0.0f, Mathematics::EvaluateDot(CenterHit.TangentFrame.NormalZ, NHit.TangentFrame.NormalZ));
                    const float WNrm       = std::pow(NrmDot, 24.0f);
                    const float DepthDiff  = std::fabs(CenterDepth - NDepth);
                    const float WDepth     = std::exp(-(DepthDiff * DepthDiff) / std::max(1.0e-4f, 0.08f * CenterDepth));
                    const float W          = WSpace * WNrm * WDepth;
                    Accum      = Accum + RawGi[NOff] * W;
                    WeightSum += W;
                }
            }
            if (WeightSum > 1.0e-6f)
            {
                OutFiltered[CenterOff] = Accum * (1.0f / WeightSum);
            }
        }
    }
}

[[nodiscard]] bool IntersectCpuGlassSphere(
    Mathematics::Axis3D Origin,
    Mathematics::Axis3D Dir,
    Mathematics::Axis3D Center,
    float               Radius,
    float&              OutTEnter,
    float&              OutTExit) noexcept
{
    OutTEnter = -1.0f;
    OutTExit  = -1.0f;
    const Mathematics::Axis3D Oc = Origin - Center;
    const float B    = Mathematics::EvaluateDot(Oc, Dir);
    const float C    = Mathematics::EvaluateDot(Oc, Oc) - Radius * Radius;
    const float Disc = B * B - C;
    if (Disc <= 0.0f) return false;
    const float SqrtD = std::sqrt(Disc);
    OutTEnter         = -B - SqrtD;
    OutTExit          = -B + SqrtD;
    return OutTExit > 1.0e-3f;
}

[[nodiscard]] int32_t FindClosestCpuGlassSphere(
    Mathematics::Axis3D            Origin,
    Mathematics::Axis3D            Dir,
    const WorldSceneSpecification& Scene,
    float                          MinT,
    float                          MaxT,
    Mathematics::Axis3D&           OutCenter,
    float&                         OutRadius,
    uint16_t&                      OutMatSlot,
    float&                         OutHitT) noexcept
{
    OutHitT        = MaxT;
    int32_t Best   = -1;
    for (uint32_t I = 0u; I < static_cast<uint32_t>(Scene.ClusterInstances.size()); ++I)
    {
        const auto& Inst = Scene.ClusterInstances[I];
        if (!Inst.TransparentGlass || Inst.UniformScale <= 1.0e-4f || Inst.PyramidAsset == nullptr) continue;
        float LocalRad = 0.40f;
        if (!Inst.PyramidAsset->Positions.empty())
        {
            LocalRad = 0.05f;
            for (const Mathematics::Axis3D& Pos : Inst.PyramidAsset->Positions)
            {
                LocalRad = std::max(LocalRad, std::sqrt(Pos.X * Pos.X + Pos.Y * Pos.Y + Pos.Z * Pos.Z));
            }
        }
        const float Rad = std::max(0.04f, LocalRad * Inst.UniformScale);
        float TEn = 0.0f, TEx = 0.0f;
        if (IntersectCpuGlassSphere(Origin, Dir, Inst.WorldTranslation, Rad, TEn, TEx))
        {
            const float HitT = (TEn > MinT) ? TEn : TEx;
            if (HitT > MinT && HitT < OutHitT)
            {
                OutHitT    = HitT;
                OutCenter  = Inst.WorldTranslation;
                OutRadius  = Rad;
                OutMatSlot = Inst.MaterialSlot;
                Best       = static_cast<int32_t>(I);
            }
        }
    }
    return Best;
}

[[nodiscard]] bool IntersectCpuAreaLightsClosest(
    Mathematics::Axis3D            Origin,
    Mathematics::Axis3D            Dir,
    const WorldSceneSpecification& Scene,
    float                          MinT,
    float                          MaxT,
    Mathematics::LinearRGB&        OutEmit,
    float&                         OutT) noexcept
{
    OutT = MaxT;
    bool AnyHit = false;
    for (const auto& Q : Scene.AreaLights)
    {
        if (std::max({ Q.EmittedRadiance.R, Q.EmittedRadiance.G, Q.EmittedRadiance.B }) <= 1.0e-4f) continue;
        const Mathematics::Axis3D LNorm = Mathematics::NormalizeAxis(
            Mathematics::EvaluateCross(Q.RightTangent, Q.UpBitangent), { 0.0f, 0.0f, -1.0f });
        const float Den   = Mathematics::EvaluateDot(LNorm, Dir);
        const bool  Front = Q.TwoSidedEmission ? (std::fabs(Den) > 1.0e-5f) : (Den < -1.0e-5f);
        if (!Front) continue;
        const float T = Mathematics::EvaluateDot(Q.CenterWorld - Origin, LNorm) / Den;
        if (T >= MinT && T < OutT)
        {
            const Mathematics::Axis3D Rel = (Origin + Dir * T) - Q.CenterWorld;
            const float U = Mathematics::EvaluateDot(Rel, Q.RightTangent);
            const float V = Mathematics::EvaluateDot(Rel, Q.UpBitangent);
            if (std::fabs(U) <= Q.HalfWidthMeters && std::fabs(V) <= Q.HalfHeightMeters)
            {
                OutT    = T;
                OutEmit = Q.EmittedRadiance;
                AnyHit  = true;
            }
        }
    }
    return AnyHit;
}

[[nodiscard]] Mathematics::LinearRGB TracePathSample(
    Mathematics::Axis3D                            Origin,
    Mathematics::Axis3D                            Direction,
    const WorldSceneSpecification&                 Scene,
    const AtmosphericSkySequence&                  SkySeq,
    uint32_t                                       MaxBounces,
    bool                                           ReflectionsEnabled,
    uint32_t&                                      RngState,
    bool                                           AllowDepth0GlassSplit = true) noexcept
{
    const uint32_t MatCount = Scene.MaterialRegister.QuerySlotCount();
    const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis(Scene.Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
    Mathematics::Axis3D SunTanU {}, SunTanV {};
    GeometrySpace::ConstructDuffOrthonormalBasis(SunDir, SunTanU, SunTanV);
    const float TanSun = std::tan(std::clamp(Scene.Sun.SunAngularRadiusRadians, 1.0e-4f, 0.25f));

    Mathematics::LinearRGB Radiance   { 0.0f, 0.0f, 0.0f };
    Mathematics::LinearRGB Throughput { 1.0f, 1.0f, 1.0f };
    Mathematics::Axis3D    RayO = Origin;
    Mathematics::Axis3D    RayD = Direction;
    bool                   PrevWasSpec = false;

    for (uint32_t Bounce = 0u; Bounce < MaxBounces; ++Bounce)
    {
        SpatialAcceleration::RaySegmentQuery Query {};
        Query.Origin          = RayO;
        Query.UnitDirection   = RayD;
        Query.MinimumDistance = 1.0e-3f;
        Query.MaximumDistance = 160.0f;

        const auto Hit = Scene.SceneTree.TraceClosestRay(Query, false);
        const float ClosestTriT = Hit.HitConfirmed ? Hit.HitDistanceMeters : 160.0f;

        Mathematics::Axis3D GCenter {};
        float GRadius = 0.0f, GT = ClosestTriT;
        uint16_t GMatSlot = 0u;
        const int32_t GlassIdx = FindClosestCpuGlassSphere(RayO, RayD, Scene, 1.0e-3f, ClosestTriT, GCenter, GRadius, GMatSlot, GT);
        if (GlassIdx >= 0)
        {
            const auto& GMat = Scene.MaterialRegister.InspectAuthoredMaterial((GMatSlot < MatCount) ? GMatSlot : 0u);
            const Mathematics::Axis3D GPos = RayO + RayD * GT;
            const Mathematics::Axis3D GNrm = Mathematics::NormalizeAxis(GPos - GCenter, { 0.0f, 0.0f, 1.0f });
            const float CrownIor = std::max(1.05f, GMat.Specular.DielectricIOR);
            const float Disp     = std::clamp(0.45f / std::max(10.0f, GMat.Transmission.AbbeDispersionNumber), 0.004f, 0.028f);
            const float CosI     = std::clamp(Mathematics::EvaluateDot(GNrm, RayD * -1.0f), 0.0f, 1.0f);
            const float Fres     = ReflectionsEnabled ? MaterialSpace::Scattering::EvaluateExactDielectricFresnel(CosI, CrownIor) : 0.0f;

            bool TraceRefract = true;
            if (AllowDepth0GlassSplit && Bounce == 0u)
            {
                if (Fres > 1.0e-4f)
                {
                    const Mathematics::Axis3D ReflO = GPos + GNrm * 2.5e-3f;
                    const Mathematics::Axis3D ReflD = Mathematics::NormalizeAxis(RayD + GNrm * (2.0f * CosI));
                    const auto ReflRad = TracePathSample(ReflO, ReflD, Scene, SkySeq, std::min(3u, MaxBounces), ReflectionsEnabled, RngState, false);
                    Radiance.R += Throughput.R * Fres * ReflRad.R;
                    Radiance.G += Throughput.G * Fres * ReflRad.G;
                    Radiance.B += Throughput.B * Fres * ReflRad.B;
                }
                Throughput.R *= (1.0f - Fres);
                Throughput.G *= (1.0f - Fres);
                Throughput.B *= (1.0f - Fres);
            }
            else if (PcgFloat01(RngState) < Fres)
            {
                RayO = GPos + GNrm * 2.5e-3f;
                RayD = Mathematics::NormalizeAxis(RayD + GNrm * (2.0f * CosI));
                TraceRefract = false;
            }

            if (TraceRefract)
            {
                const float WSel = PcgFloat01(RngState);
                const float IorC = CrownIor + ((WSel < 0.3333f) ? -Disp : ((WSel < 0.6667f) ? 0.0f : Disp * 1.15f));
                const float Eta  = 1.0f / IorC;
                const float Sin2T = Eta * Eta * std::max(0.0f, 1.0f - CosI * CosI);
                if (Sin2T >= 1.0f)
                {
                    RayO = GPos + GNrm * 2.5e-3f;
                    RayD = Mathematics::NormalizeAxis(RayD + GNrm * (2.0f * CosI));
                }
                else
                {
                    const Mathematics::Axis3D InsideD = Mathematics::NormalizeAxis(
                        RayD * Eta + GNrm * (Eta * CosI - std::sqrt(1.0f - Sin2T)));
                    float TIn = 0.0f, TOut = 0.0f;
                    if (IntersectCpuGlassSphere(GPos + InsideD * 2.0e-3f, InsideD, GCenter, GRadius, TIn, TOut) && TOut > 0.0f)
                    {
                        const Mathematics::Axis3D ExitPos = GPos + InsideD * (2.0e-3f + TOut);
                        const Mathematics::Axis3D OutNrm  = Mathematics::NormalizeAxis(ExitPos - GCenter, GNrm);
                        const float CosE  = std::clamp(Mathematics::EvaluateDot(InsideD * -1.0f, OutNrm * -1.0f), 0.0f, 1.0f);
                        const float Sin2E = IorC * IorC * std::max(0.0f, 1.0f - CosE * CosE);
                        if (Sin2E >= 1.0f)
                        {
                            RayD = Mathematics::NormalizeAxis(InsideD - OutNrm * (2.0f * Mathematics::EvaluateDot(InsideD, OutNrm)));
                            RayO = ExitPos - OutNrm * 2.0e-3f;
                        }
                        else
                        {
                            RayD = Mathematics::NormalizeAxis(
                                InsideD * IorC - OutNrm * (IorC * CosE - std::sqrt(1.0f - Sin2E)));
                            RayO = ExitPos + OutNrm * 3.0e-3f;
                        }
                    }
                    else
                    {
                        RayO = GPos + InsideD * (GRadius * 2.1f);
                        RayD = InsideD;
                    }
                    Throughput.R *= 0.95f;
                    Throughput.G *= 0.98f;
                    Throughput.B *= 1.00f;
                }
            }
            PrevWasSpec = true;
            if (Bounce + 1u >= MaxBounces) MaxBounces = std::min(8u, MaxBounces + 2u);
            continue;
        }

        if (PrevWasSpec)
        {
            Mathematics::LinearRGB LEmit {};
            float LT = ClosestTriT;
            if (IntersectCpuAreaLightsClosest(RayO, RayD, Scene, 1.0e-3f, ClosestTriT, LEmit, LT))
            {
                Radiance.R += Throughput.R * LEmit.R;
                Radiance.G += Throughput.G * LEmit.G;
                Radiance.B += Throughput.B * LEmit.B;
                break;
            }
        }

        if (!Hit.HitConfirmed)
        {
            const auto Sky = (Bounce == 0u && Scene.Atmosphere.Medium.AtmosphereEnabled)
                ? EvaluateBackgroundSkyRadiance(Scene, SkySeq, RayO, RayD)
                : EvaluateSkyDome(RayD, Scene.SkyRadiance, &Scene, &SkySeq);
            Radiance.R += Throughput.R * Sky.R;
            Radiance.G += Throughput.G * Sky.G;
            Radiance.B += Throughput.B * Sky.B;
            break;
        }

        const uint16_t MatSlot = (Hit.MaterialOrdinal < MatCount) ? Hit.MaterialOrdinal : 0u;
        const auto& Mat        = Scene.MaterialRegister.InspectAuthoredMaterial(MatSlot);
        const uint32_t Lobes   = Scene.MaterialRegister.InspectCompactEntry(MatSlot).ActiveLobeMask;

        const Mathematics::Axis3D HitPos = RayO + RayD * Hit.HitDistanceMeters;
        const bool FrontFace   = (Mathematics::EvaluateDot(Hit.GeometricNormal, RayD) < 0.0f);
        const Mathematics::Axis3D Nrm = FrontFace ? Hit.GeometricNormal : (Hit.GeometricNormal * -1.0f);
        const Mathematics::Axis3D Wo  = RayD * -1.0f;

        Mathematics::Axis3D TanU {}, BitV {};
        GeometrySpace::ConstructDuffOrthonormalBasis(Nrm, TanU, BitV);
        const auto WorldToLocal = [&](Mathematics::Axis3D V) noexcept -> Mathematics::Axis3D
        {
            return {
                Mathematics::EvaluateDot(V, TanU),
                Mathematics::EvaluateDot(V, BitV),
                Mathematics::EvaluateDot(V, Nrm),
            };
        };
        const auto LocalToWorld = [&](Mathematics::Axis3D L) noexcept -> Mathematics::Axis3D
        {
            return TanU * L.X + BitV * L.Y + Nrm * L.Z;
        };

        const auto Emissive = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);
        Radiance.R += Throughput.R * Emissive.R;
        Radiance.G += Throughput.G * Emissive.G;
        Radiance.B += Throughput.B * Emissive.B;

        const auto LocalWo = WorldToLocal(Wo);
        if (LocalWo.Z <= 1.0e-4f)
        {
            break;
        }

        // Dielectric Glass Refraction/Reflection Branch
        if (Mat.Transmission.TransmissionWeight > 0.35f)
        {
            const float Ior       = std::max(1.01f, Mat.Specular.DielectricIOR);
            const float Eta       = FrontFace ? (1.0f / Ior) : Ior;
            const float CosI      = std::clamp(Mathematics::EvaluateDot(Wo, Nrm), 0.0f, 1.0f);
            const float Fres      = MaterialSpace::Scattering::EvaluateExactDielectricFresnel(CosI, Ior);
            const float Sin2T     = Eta * Eta * std::max(0.0f, 1.0f - CosI * CosI);

            if (Sin2T >= 1.0f || PcgFloat01(RngState) < Fres)
            {
                RayO = HitPos + Nrm * 2.0e-3f;
                RayD = Mathematics::NormalizeAxis(RayD + Nrm * (2.0f * CosI));
            }
            else
            {
                RayO = HitPos - Nrm * 2.0e-3f;
                RayD = Mathematics::NormalizeAxis(RayD * Eta + Nrm * (Eta * CosI - std::sqrt(1.0f - Sin2T)));
                Throughput.R *= Mat.Transmission.AttenuationColor.R;
                Throughput.G *= Mat.Transmission.AttenuationColor.G;
                Throughput.B *= Mat.Transmission.AttenuationColor.B;
            }
            continue;
        }

        // 1. NEE: Stratified Solar Disc Visibility + BSDF
        {
            const float JitterR = std::sqrt(PcgFloat01(RngState)) * TanSun;
            const float JitterA = PcgFloat01(RngState) * Constants::TauFloat;
            const Mathematics::Axis3D JitterSun = Mathematics::NormalizeAxis(
                SunDir + SunTanU * (JitterR * std::cos(JitterA)) + SunTanV * (JitterR * std::sin(JitterA)), SunDir);
            const float NdotL = Mathematics::EvaluateDot(Nrm, JitterSun);
            if (NdotL > 0.0f)
            {
                SpatialAcceleration::RaySegmentQuery SunQuery {};
                SunQuery.Origin          = HitPos + Nrm * 2.0e-3f;
                SunQuery.UnitDirection   = JitterSun;
                SunQuery.MinimumDistance = 1.0e-3f;
                SunQuery.MaximumDistance = 120.0f;
                if (!Scene.SceneTree.QueryRayOccluded(SunQuery))
                {
                    const float CloudShadowT = Scene.Atmosphere.Medium.AtmosphereEnabled
                        ? VolumetricCloudSequence::EvaluateSolarCloudTransmittance(
                              HitPos, JitterSun, Scene.Atmosphere.Clouds,
                              Scene.Atmosphere.Medium.PlanetRadiusMeters, Scene.Atmosphere.SimulationSeconds)
                        : 1.0f;
                    const auto LocalWi = WorldToLocal(JitterSun);
                    const auto Bsdf    = MaterialSpace::Scattering::EvaluateOpenPbrLocalBsdf(Mat, Lobes, LocalWo, LocalWi);
                    Radiance.R += Throughput.R * Bsdf.R * Scene.Sun.SolarIrradiance.R * NdotL * CloudShadowT;
                    Radiance.G += Throughput.G * Bsdf.G * Scene.Sun.SolarIrradiance.G * NdotL * CloudShadowT;
                    Radiance.B += Throughput.B * Bsdf.B * Scene.Sun.SolarIrradiance.B * NdotL * CloudShadowT;
                }
            }
        }

        // 2. NEE: Random Sample on Polygonal Area Quad Lights
        for (const auto& Light : Scene.AreaLights)
        {
            const float LU = PcgFloat01(RngState) * 2.0f - 1.0f;
            const float LV = PcgFloat01(RngState) * 2.0f - 1.0f;
            const Mathematics::Axis3D Target = Light.CenterWorld +
                Light.RightTangent * (Light.HalfWidthMeters * LU) +
                Light.UpBitangent  * (Light.HalfHeightMeters * LV);
            const Mathematics::Axis3D ToL = Target - HitPos;
            const float Dist2 = Mathematics::EvaluateLengthSquared(ToL);
            const float Dist  = std::sqrt(std::max(1.0e-6f, Dist2));
            const Mathematics::Axis3D LDir = ToL * (1.0f / Dist);
            const float NdotL = Mathematics::EvaluateDot(Nrm, LDir);
            if (NdotL <= 0.0f) continue;

            const Mathematics::Axis3D LNorm = Mathematics::NormalizeAxis(
                Mathematics::EvaluateCross(Light.RightTangent, Light.UpBitangent), { 0.0f, 0.0f, -1.0f });
            const float LightCos = Light.TwoSidedEmission
                ? std::fabs(Mathematics::EvaluateDot(LNorm, LDir * -1.0f))
                : std::max(0.0f, Mathematics::EvaluateDot(LNorm, LDir * -1.0f));
            if (LightCos <= 1.0e-4f) continue;

            SpatialAcceleration::RaySegmentQuery VisQ {};
            VisQ.Origin          = HitPos + Nrm * 2.0e-3f;
            VisQ.UnitDirection   = LDir;
            VisQ.MinimumDistance = 1.0e-3f;
            VisQ.MaximumDistance = std::max(2.0e-3f, Dist - 4.0e-3f);
            if (!Scene.SceneTree.QueryRayOccluded(VisQ))
            {
                const float Area   = 4.0f * Light.HalfWidthMeters * Light.HalfHeightMeters;
                const float Geom   = (NdotL * LightCos * Area) / std::max(1.0e-4f, Dist2);
                const auto LocalWi = WorldToLocal(LDir);
                const auto Bsdf    = MaterialSpace::Scattering::EvaluateOpenPbrLocalBsdf(Mat, Lobes, LocalWo, LocalWi);
                Radiance.R += Throughput.R * Bsdf.R * Light.EmittedRadiance.R * Geom;
                Radiance.G += Throughput.G * Bsdf.G * Light.EmittedRadiance.G * Geom;
                Radiance.B += Throughput.B * Bsdf.B * Light.EmittedRadiance.B * Geom;
            }
        }

        // 3. MIS Next-Bounce Sampling: Heitz (2018) GGX VNDF Specular vs Cosine Diffuse
        const float DiffProb = ReflectionsEnabled ? std::clamp((1.0f - Mat.Specular.Metalness) * 0.65f, 0.0f, 0.85f) : 1.0f;
        const float SpecProb = 1.0f - DiffProb;
        const bool PickDiff  = (PcgFloat01(RngState) < DiffProb);

        Mathematics::Axis3D LocalWi {};
        const float U1 = PcgFloat01(RngState);
        const float U2 = PcgFloat01(RngState);
        const auto Alpha = MaterialSpace::Scattering::ComputeAnisotropicAlpha(
            std::max(0.0015f, Mat.Specular.Roughness * Mat.Specular.Roughness), Mat.Specular.Anisotropy);

        if (PickDiff)
        {
            const float R   = std::sqrt(U1);
            const float Phi = U2 * Constants::TauFloat;
            LocalWi = { R * std::cos(Phi), R * std::sin(Phi), std::sqrt(std::max(0.0f, 1.0f - U1)) };
            PrevWasSpec = false;
        }
        else
        {
            const auto HalfV = MaterialSpace::Scattering::SampleAnisotropicSphericalCapVndf(LocalWo, Alpha, U1, U2);
            LocalWi = HalfV * (2.0f * Mathematics::EvaluateDot(LocalWo, HalfV)) - LocalWo;
            PrevWasSpec = (Mat.Specular.Roughness < 0.28f);
        }

        if (LocalWi.Z <= 1.0e-4f)
        {
            break;
        }

        // Evaluate 1-sample MIS PDF combining Cosine Hemisphere + GGX VNDF
        const auto HalfVec  = Mathematics::NormalizeAxis(LocalWo + LocalWi);
        const float NdotH   = std::max(1.0e-4f, HalfVec.Z);
        const float Ax      = std::max(0.0015f, Alpha.AlphaX);
        const float Ay      = std::max(0.0015f, Alpha.AlphaY);
        const float TermH   = (HalfVec.X * HalfVec.X) / (Ax * Ax) + (HalfVec.Y * HalfVec.Y) / (Ay * Ay) + NdotH * NdotH;
        const float D       = 1.0f / (Constants::PiFloat * Ax * Ay * TermH * TermH);
        const float Tan2V   = ((LocalWo.X * Ax) * (LocalWo.X * Ax) + (LocalWo.Y * Ay) * (LocalWo.Y * Ay)) / std::max(1.0e-6f, LocalWo.Z * LocalWo.Z);
        const float G1V     = 2.0f / (1.0f + std::sqrt(1.0f + Tan2V));
        const float PdfSpec = (D * G1V) / (4.0f * std::max(1.0e-4f, LocalWo.Z));
        const float PdfDiff = LocalWi.Z / Constants::PiFloat;
        const float MixPdf  = std::max(1.0e-4f, DiffProb * PdfDiff + SpecProb * PdfSpec);

        const auto Bsdf = MaterialSpace::Scattering::EvaluateOpenPbrLocalBsdf(Mat, Lobes, LocalWo, LocalWi);
        const float Weight = LocalWi.Z / MixPdf;
        Throughput.R = std::min(12.0f, Throughput.R * Bsdf.R * Weight);
        Throughput.G = std::min(12.0f, Throughput.G * Bsdf.G * Weight);
        Throughput.B = std::min(12.0f, Throughput.B * Bsdf.B * Weight);

        // Russian Roulette after bounce 3
        if (Bounce >= 3u)
        {
            const float MaxT = std::clamp(std::max({ Throughput.R, Throughput.G, Throughput.B }), 0.05f, 0.95f);
            if (PcgFloat01(RngState) > MaxT)
            {
                break;
            }
            Throughput = Throughput * (1.0f / MaxT);
        }

        RayO = HitPos + Nrm * 2.0e-3f;
        RayD = Mathematics::NormalizeAxis(LocalToWorld(LocalWi));
    }

    // Clamp extreme fireflies & sanitize NaNs/Infs per sample
    const auto SanitizeF = [](float V) noexcept -> float
    {
        return std::isfinite(V) ? std::clamp(V, 0.0f, 18.0f) : 0.0f;
    };
    return {
        SanitizeF(Radiance.R),
        SanitizeF(Radiance.G),
        SanitizeF(Radiance.B),
    };
}

Mathematics::LinearRGB EvaluateFluidEmissiveOnSurface(
    const WorldSceneSpecification&        Scene,
    const Mathematics::Axis3D&            HitPos,
    const Mathematics::Axis3D&            Normal,
    const Mathematics::Axis3D&            ViewDir,
    uint16_t                              MaterialSlot,
    const MaterialSpace::SurfaceMaterial& Mat) noexcept
{
    Mathematics::LinearRGB Sum {};
    if (Scene.OptionalFluid == nullptr) return Sum;
    const auto& Lights = Scene.OptionalFluid->InspectEmissiveLights();
    if (Lights.empty()) return Sum;

    const uint32_t Lobes = Scene.MaterialRegister.InspectCompactEntry(MaterialSlot).ActiveLobeMask;
    const auto WorldToLocal = [&](const Mathematics::Axis3D& W) noexcept -> Mathematics::Axis3D
    {
        Mathematics::Axis3D T {}, B {};
        if (std::abs(Normal.Z) < 0.999f)
        {
            T = Mathematics::NormalizeAxis(Mathematics::EvaluateCross({ 0.0f, 0.0f, 1.0f }, Normal));
        }
        else
        {
            T = { 1.0f, 0.0f, 0.0f };
        }
        B = Mathematics::EvaluateCross(Normal, T);
        return { Mathematics::EvaluateDot(W, T), Mathematics::EvaluateDot(W, B), Mathematics::EvaluateDot(W, Normal) };
    };
    const auto LocalWo = WorldToLocal(ViewDir);

    for (const auto& L : Lights)
    {
        const Mathematics::Axis3D ToL = L.WorldPosition - HitPos;
        const float Dist2 = std::max(0.08f, Mathematics::EvaluateDot(ToL, ToL) + L.RadiusMeters * L.RadiusMeters * 0.35f);
        const float Dist  = std::sqrt(Dist2);
        const Mathematics::Axis3D Wi = ToL * (1.0f / Dist);
        const float NdotL = Mathematics::EvaluateDot(Normal, Wi);
        if (NdotL <= 1.0e-4f) continue;

        const auto LocalWi = WorldToLocal(Wi);
        const auto Bsdf = MaterialSpace::Scattering::EvaluateOpenPbrLocalBsdf(Mat, Lobes, LocalWo, LocalWi);
        const float Atten = NdotL / (4.0f * Constants::PiFloat * Dist2);
        Sum.R += Bsdf.R * L.ColorIrradiance.R * Atten;
        Sum.G += Bsdf.G * L.ColorIrradiance.G * Atten;
        Sum.B += Bsdf.B * L.ColorIrradiance.B * Atten;
    }
    return Sum;
}

void ApplyATrousWaveletDenoiser(
    uint32_t                                       WidthPx,
    uint32_t                                       HeightPx,
    Mathematics::Axis3D                            CameraOrigin,
    std::span<const AnalyticResolvedSurfaceSample> PrimarySamples,
    const MaterialSpace::CompactMaterialRegister&  MaterialRegister,
    std::vector<Mathematics::LinearRGB>&           InOutRadiance,
    std::span<const uint64_t>                      GlassPeelFront64 = {})
{
    const std::size_t Total = static_cast<std::size_t>(WidthPx) * HeightPx;
    if (Total == 0u || InOutRadiance.size() != Total)
    {
        return;
    }

    const uint32_t MatCount = MaterialRegister.QuerySlotCount();
    const bool     HasGlass = (GlassPeelFront64.size() == Total);
    std::vector<Mathematics::LinearRGB> Scratch(Total);
    constexpr float Kernel1D[3] { 1.0f, 2.0f / 3.0f, 1.0f / 6.0f };

    for (uint32_t FilterStepIdx = 0u; FilterStepIdx < 3u; ++FilterStepIdx)
    {
        const int Step = 1 << FilterStepIdx;
        for (uint32_t PY = 0u; PY < HeightPx; ++PY)
        {
            for (uint32_t PX = 0u; PX < WidthPx; ++PX)
            {
                const std::size_t CenterOff = static_cast<std::size_t>(PY) * WidthPx + PX;
                const auto& CHit            = PrimarySamples[CenterOff];
                if ((HasGlass && GlassPeelFront64[CenterOff] != 0ull) ||
                    !CHit.ValidHit || CHit.MaterialSlot >= MatCount)
                {
                    Scratch[CenterOff] = InOutRadiance[CenterOff];
                    continue;
                }
                const float CDepth = Mathematics::EvaluateLength(CHit.WorldPosition - CameraOrigin);
                const auto& CMat   = MaterialRegister.InspectAuthoredMaterial(CHit.MaterialSlot);
                const auto& CRad   = InOutRadiance[CenterOff];
                if (CMat.Specular.Roughness < 0.12f)
                {
                    Scratch[CenterOff] = CRad;
                    continue;
                }
                const float CLum   = 0.2126f * CRad.R + 0.7152f * CRad.G + 0.0722f * CRad.B;

                Mathematics::LinearRGB Accum {};
                float WeightSum = 0.0f;

                for (int DY = -2; DY <= 2; ++DY)
                {
                    const int NY = std::clamp(static_cast<int>(PY) + DY * Step, 0, static_cast<int>(HeightPx) - 1);
                    for (int DX = -2; DX <= 2; ++DX)
                    {
                        const int NX = std::clamp(static_cast<int>(PX) + DX * Step, 0, static_cast<int>(WidthPx) - 1);
                        const std::size_t NOff = static_cast<std::size_t>(NY) * WidthPx + static_cast<uint32_t>(NX);
                        const auto& NHit       = PrimarySamples[NOff];
                        if ((HasGlass && GlassPeelFront64[NOff] != 0ull) ||
                            !NHit.ValidHit || NHit.MaterialSlot >= MatCount)
                        {
                            continue;
                        }
                        const float NDepth = Mathematics::EvaluateLength(NHit.WorldPosition - CameraOrigin);
                        const auto& NMat   = MaterialRegister.InspectAuthoredMaterial(NHit.MaterialSlot);
                        const auto& NRad   = InOutRadiance[NOff];
                        if (!std::isfinite(NRad.R) || !std::isfinite(NRad.G) || !std::isfinite(NRad.B))
                        {
                            continue;
                        }
                        const float NLum   = 0.2126f * NRad.R + 0.7152f * NRad.G + 0.0722f * NRad.B;

                        const float KW      = Kernel1D[std::abs(DX)] * Kernel1D[std::abs(DY)];
                        const float NrmDot  = std::max(0.0f, Mathematics::EvaluateDot(CHit.TangentFrame.NormalZ, NHit.TangentFrame.NormalZ));
                        const float WNrm    = std::pow(NrmDot, 64.0f);
                        const float DDiff   = std::fabs(CDepth - NDepth);
                        const float WDepth  = std::exp(-(DDiff * DDiff) / std::max(1.0e-4f, 0.05f * CDepth));
                        const float AlbDiff = std::fabs(CMat.Diffuse.BaseColor.R - NMat.Diffuse.BaseColor.R) +
                                              std::fabs(CMat.Diffuse.BaseColor.G - NMat.Diffuse.BaseColor.G) +
                                              std::fabs(CMat.Diffuse.BaseColor.B - NMat.Diffuse.BaseColor.B);
                        const float WAlbedo = std::exp(-AlbDiff * 18.0f);
                        const float LumDiff = std::fabs(CLum - NLum);
                        const float WLum    = std::exp(-(LumDiff * LumDiff) / std::max(0.08f, 1.4f * CLum + 0.15f));

                        const float W = KW * WNrm * WDepth * WAlbedo * WLum;
                        Accum      = Accum + NRad * W;
                        WeightSum += W;
                    }
                }
                Scratch[CenterOff] = (WeightSum > 1.0e-6f) ? (Accum * (1.0f / WeightSum)) : CRad;
            }
        }
        InOutRadiance.swap(Scratch);
    }
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
    if (DebugChannel == 3u)
    {
        return {
            Hit.TangentFrame.NormalZ.X * 0.5f + 0.5f,
            Hit.TangentFrame.NormalZ.Y * 0.5f + 0.5f,
            Hit.TangentFrame.NormalZ.Z * 0.5f + 0.5f,
        };
    }
    if (DebugChannel == 9u)
    {
        const float M = std::clamp(Mat.Specular.Metalness, 0.0f, 1.0f);
        return { M, M, M };
    }
    if (DebugChannel == 10u)
    {
        const uint32_t SId = static_cast<uint32_t>(Hit.MaterialSlot) + 1u;
        const auto Frac = [](float V) noexcept { return V - std::floor(V); };
        return {
            0.18f + 0.78f * Frac(static_cast<float>(SId) * 0.3819660113f),
            0.18f + 0.78f * Frac(static_cast<float>(SId) * 0.6180339887f + 0.27f),
            0.18f + 0.78f * Frac(static_cast<float>(SId) * 0.7548776662f + 0.53f),
        };
    }
    if (DebugChannel == 11u)
    {
        return {
            Hit.TangentFrame.NormalZ.X * 0.5f + 0.5f,
            Hit.TangentFrame.NormalZ.Y * 0.5f + 0.5f,
            Hit.TangentFrame.NormalZ.Z * 0.5f + 0.5f,
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
    if (!InOutScene.Atmosphere.Medium.AtmosphereEnabled)
    {
        return;
    }

    const auto SolarSolved = CelestialEphemerisSolver::SolveSolarEphemeris(InOutScene.Atmosphere.Solar);
    SkySequence.BuildSkyViewLut(
        SolarSolved.WorldDirectionToSun,
        InOutScene.Atmosphere.Medium,
        InOutScene.Atmosphere.Solar.SolarIntensityMultiplier);

    const auto Trans = AtmosphericSkySequence::ComputeSolarTransmittance(
        SolarSolved.WorldDirectionToSun,
        InOutScene.Atmosphere.Medium);

    const float SunVis = std::clamp((SolarSolved.ElevationDegrees + 4.0f) / 10.0f, 0.0f, 1.0f);
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
        std::max(0.015f, OverheadSky.R),
        std::max(0.020f, OverheadSky.G),
        std::max(0.032f, OverheadSky.B),
    };
}

void TieredWorldRenderSequence::PrepareWorldScene(WorldSceneSpecification& InOutScene)
{
    // 0. Synchronize physical Atmosphere, Sun Ephemeris, and SkyView LUT
    SynchronizeWorldAtmosphere(InOutScene);

    // 1. Compile 32 B BounceSlabEntries for every registered material slot!
    CompileBounceSummaries(InOutScene);

    // 2. Build 2-Cascade 3.9 mm Orthographic Contact Shadow Clips
    const Mathematics::Axis3D FocusCenter = (InOutScene.SceneTree.RootBoundsMin + InOutScene.SceneTree.RootBoundsMax) * 0.5f;
    SolarShadowSequence::BuildTwoCascadeContactClips(
        InOutScene.Sun, FocusCenter, InOutScene.SceneTree, FineContactClip, CoarseContactClip);

    // 3. Build & Propagate Multi-Bounce DDGI 1/pi Octahedral Irradiance Lattice (with 16x16 Chebyshev Depth!)
    RtxLattice.PropagateRtxIrradianceLattice(
        InOutScene.LatticeConfig,
        InOutScene.SceneTree,
        InOutScene.BounceSummaries,
        InOutScene.Sun,
        InOutScene.SkyRadiance);

    // 4. Build 3D Radiosity SDF Mip-Volume (Strictly for SdfGlobalIllumination GI Only!)
    RadiosityVolume.BuildRadiosityLevels(
        InOutScene.SceneSdf,
        InOutScene.SceneTree,
        InOutScene.BounceSummaries,
        InOutScene.Sun,
        InOutScene.SkyRadiance);
}

void TieredWorldRenderSequence::PrepareRtxIrradianceLatticeOnly(WorldSceneSpecification& InOutScene)
{
    SynchronizeWorldAtmosphere(InOutScene);
    CompileBounceSummaries(InOutScene);
    RtxLattice.PropagateRtxIrradianceLattice(
        InOutScene.LatticeConfig,
        InOutScene.SceneTree,
        InOutScene.BounceSummaries,
        InOutScene.Sun,
        InOutScene.SkyRadiance);
}

WorldRenderFrameResult TieredWorldRenderSequence::RenderWorldFrame(
    WorldRenderTier                Tier,
    const PerspectiveCameraFrame&  CurrentCamera,
    const PerspectiveCameraFrame&  PreviousCamera,
    const WorldSceneSpecification& PreparedScene)
{
    WorldRenderConfiguration Config {};
    if (Tier == WorldRenderTier::Tier2RtxDeterministic)
    {
        Config.Engine           = WorldRenderEngine::DdgiRaytracing;
        Config.ShadowAlgorithm  = SolarShadowAlgorithm::WideTreeRayQuery;
        Config.ReflectionSource = SpecularReflectionSource::WideTreeRaytrace;
    }
    else
    {
        Config.Engine           = WorldRenderEngine::SdfGlobalIllumination;
        Config.ShadowAlgorithm  = SolarShadowAlgorithm::ContactPcss;
        Config.ReflectionSource = SpecularReflectionSource::WideTreeRaytrace;
    }
    WorldRenderFrameResult Out = RenderWorldFrameWithEngine(Config, CurrentCamera, PreviousCamera, PreparedScene);
    Out.ActiveTier = Tier;
    return Out;
}

WorldRenderFrameResult TieredWorldRenderSequence::RenderWorldFrameWithEngine(
    const WorldRenderConfiguration& Config,
    const PerspectiveCameraFrame&   CurrentCamera,
    const PerspectiveCameraFrame&   PreviousCamera,
    const WorldSceneSpecification&  PreparedScene)
{
    WorldRenderFrameResult Result {};
    Result.WidthPx      = std::max(1u, CurrentCamera.ViewportWidthPx);
    Result.HeightPx     = std::max(1u, CurrentCamera.ViewportHeightPx);
    Result.ActiveEngine = Config.Engine;
    Result.ActiveTier   = (Config.Engine == WorldRenderEngine::DdgiRaytracing ||
                           Config.Engine == WorldRenderEngine::DeterministicPathTrace)
        ? WorldRenderTier::Tier2RtxDeterministic
        : WorldRenderTier::Tier1GtxCalibrated;

    const std::size_t TotalPixels = static_cast<std::size_t>(Result.WidthPx) * Result.HeightPx;
    Result.CompositeLinearHdr.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });
    Result.IndirectDiffuseHdr.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });
    Result.DisplayRgba8.assign(TotalPixels * 4u, 255u);

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
    const ProjectedLensFlareFrame FlareProj =
        (Config.DebugChannel == 0u && PreparedScene.Atmosphere.Medium.AtmosphereEnabled && PreparedScene.Atmosphere.LensFlare.Enabled)
            ? LensFlareSequence::ResolveSolarFlareProjection(
                  CurBasis.CameraOrigin, CurBasis.ForwardAxis, CurBasis.RightAxis, CurBasis.UpAxis,
                  CurBasis.TanHalfFovX, CurBasis.TanHalfFovY, PreparedScene.Atmosphere, &PreparedScene.SceneTree)
            : ProjectedLensFlareFrame{};

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
                    OutHdr = EvaluateBackgroundSkyRadiance(
                        PreparedScene, SkySequence, CurBasis.CameraOrigin, RayDir, EvaluateCloudIgn(PX, PY), &CpuCloudField, FU, FV);
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
    // ENGINE 4: DETERMINISTIC PATH TRACER (MIS + NEE + Heitz GGX VNDF + A-Trous Denoiser)
    //====================================================================================================================
    if (Config.Engine == WorldRenderEngine::DeterministicPathTrace)
    {
        const uint32_t Spp  = std::clamp(Config.PathTraceSamples, 1u, 128u);
        const uint32_t MaxB = Config.GlobalIlluminationEnabled ? std::clamp(Config.PathTraceMaxBounces, 1u, 8u) : 1u;

        for (uint32_t PY = 0u; PY < Result.HeightPx; ++PY)
        {
            for (uint32_t PX = 0u; PX < Result.WidthPx; ++PX)
            {
                const std::size_t Off = static_cast<std::size_t>(PY) * Result.WidthPx + PX;
                const auto& Hit = PrimarySamples[Off];
                if (Config.DebugChannel != 0u && Hit.ValidHit && Hit.MaterialSlot < MatCount)
                {
                    const auto& Mat = PreparedScene.MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);
                    Result.CompositeLinearHdr[Off] = EvaluateDebugChannelColor(Config.DebugChannel, CurBasis.CameraOrigin, Hit, Mat);
                    continue;
                }

                uint32_t Rng = (PX * 747796405u + PY * 2891336453u + 277803737u) | 1u;
                (void)PcgFloat01(Rng);
                (void)PcgFloat01(Rng);
                const bool HitGlassPx = (VisTargets.GlassPeelFront64.size() == TotalPixels && VisTargets.GlassPeelFront64[Off] != 0ull);
                const uint32_t PixelSpp = (HitGlassPx && Spp >= 8u) ? std::min(64u, Spp * 3u) : Spp;
                Mathematics::LinearRGB Sum {};
                for (uint32_t S = 0u; S < PixelSpp; ++S)
                {
                    const float Jx = (static_cast<float>(PX) + PcgFloat01(Rng)) / static_cast<float>(Result.WidthPx);
                    const float Jy = (static_cast<float>(PY) + PcgFloat01(Rng)) / static_cast<float>(Result.HeightPx);
                    const float NdcX = Jx * 2.0f - 1.0f;
                    const float NdcY = 1.0f - Jy * 2.0f;
                    const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(
                        CurBasis.ForwardAxis + CurBasis.RightAxis * (NdcX * CurBasis.TanHalfFovX) + CurBasis.UpAxis * (NdcY * CurBasis.TanHalfFovY));
                    Sum = Sum + TracePathSample(CurBasis.CameraOrigin, RayDir, PreparedScene, SkySequence, MaxB, Config.ReflectionsEnabled, Rng);
                }
                Result.CompositeLinearHdr[Off] = Sum * (1.0f / static_cast<float>(PixelSpp));
            }
        }

        if (Config.PathTraceDenoise && Config.DebugChannel == 0u)
        {
            ApplyATrousWaveletDenoiser(
                Result.WidthPx, Result.HeightPx, CurBasis.CameraOrigin, PrimarySamples,
                PreparedScene.MaterialRegister, Result.CompositeLinearHdr, VisTargets.GlassPeelFront64);
        }

        for (uint32_t PY = 0u; PY < Result.HeightPx; ++PY)
        {
            for (uint32_t PX = 0u; PX < Result.WidthPx; ++PX)
            {
                const std::size_t Off = static_cast<std::size_t>(PY) * Result.WidthPx + PX;
                const auto& Hit = PrimarySamples[Off];
                if (Config.DebugChannel == 0u)
                {
                    const float NdcX = ((static_cast<float>(PX) + 0.5f) / static_cast<float>(Result.WidthPx)) * 2.0f - 1.0f;
                    const float NdcY = 1.0f - ((static_cast<float>(PY) + 0.5f) / static_cast<float>(Result.HeightPx)) * 2.0f;
                    const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(
                        CurBasis.ForwardAxis + CurBasis.RightAxis * (NdcX * CurBasis.TanHalfFovX) + CurBasis.UpAxis * (NdcY * CurBasis.TanHalfFovY));
                    const float HitDist = Hit.ValidHit ? Mathematics::EvaluateLength(Hit.WorldPosition - CurBasis.CameraOrigin) : 3500.0f;
                    const float Ign = EvaluateCloudIgn(PX, PY);
                    const auto& Atmo = PreparedScene.Atmosphere;
                    const auto SkyForVol = Atmo.Medium.AtmosphereEnabled
                        ? SkySequence.SampleSkyViewDirection(RayDir)
                        : PreparedScene.SkyRadiance;

                    if (PreparedScene.OptionalFluid != nullptr)
                    {
                        if (Hit.ValidHit && Hit.MaterialSlot < MatCount)
                        {
                            const auto& Mat = PreparedScene.MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);
                            const auto ViewDir = Mathematics::NormalizeAxis(CurBasis.CameraOrigin - Hit.WorldPosition);
                            Result.CompositeLinearHdr[Off] = Result.CompositeLinearHdr[Off] +
                                EvaluateFluidEmissiveOnSurface(PreparedScene, Hit.WorldPosition, Hit.TangentFrame.NormalZ, ViewDir, Hit.MaterialSlot, Mat);
                        }
                        const auto FluidRes = PreparedScene.OptionalFluid->EvaluateFluidSegment(
                            CurBasis.CameraOrigin, RayDir, HitDist, PreparedScene.Sun.DirectionToSun,
                            PreparedScene.Sun.SolarIrradiance, SkyForVol, Ign);
                        Result.CompositeLinearHdr[Off] = {
                            Result.CompositeLinearHdr[Off].R * FluidRes.Transmittance + FluidRes.RadianceHdr.R,
                            Result.CompositeLinearHdr[Off].G * FluidRes.Transmittance + FluidRes.RadianceHdr.G,
                            Result.CompositeLinearHdr[Off].B * FluidRes.Transmittance + FluidRes.RadianceHdr.B,
                        };
                    }

                    if (!Atmo.Medium.OrbitalViewEnabled &&
                        (Atmo.Fog.HeightFog.Enabled || Atmo.Fog.AerialFog.Enabled || !Atmo.Fog.LocalFogs.empty()))
                    {
                        const auto FogRes = VolumetricFogSequence::EvaluateVolumetricFog(
                            CurBasis.CameraOrigin, RayDir, HitDist, PreparedScene.Sun.DirectionToSun,
                            PreparedScene.Sun.SolarIrradiance, SkyForVol, Atmo.Fog, Atmo.Wind,
                            Atmo.Clouds.WindSpeedMetersPerSecond, Atmo.Clouds.WindBearingDegrees,
                            Atmo.SimulationSeconds, Atmo.ForceFieldSource, Atmo.WindLatticeCascade, Ign);
                        Result.CompositeLinearHdr[Off] = {
                            Result.CompositeLinearHdr[Off].R * FogRes.Transmittance + FogRes.InScatterHdr.R,
                            Result.CompositeLinearHdr[Off].G * FogRes.Transmittance + FogRes.InScatterHdr.G,
                            Result.CompositeLinearHdr[Off].B * FogRes.Transmittance + FogRes.InScatterHdr.B,
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
                            if (PreparedScene.SceneTree.QueryRayOccluded(Q)) return 0.0f;
                            return (PreparedScene.OptionalFluid != nullptr)
                                ? PreparedScene.OptionalFluid->EvaluateFluidShadowTransmittance(P, SDir, 32.0f)
                                : 1.0f;
                        };
                        const auto CloudVisFn = [&](const Mathematics::Axis3D& P, const Mathematics::Axis3D& SDir) noexcept -> float
                        {
                            return VolumetricCloudSequence::EvaluateAllSolarCloudTransmittance(P, SDir, Atmo);
                        };
                        const auto ShaftHdr = VolumetricFogSequence::EvaluateVolumetricGodRays(
                            CurBasis.CameraOrigin, RayDir, HitDist, PreparedScene.Sun.DirectionToSun,
                            PreparedScene.Sun.SolarIrradiance, Atmo.Fog, Atmo.Wind,
                            Atmo.Clouds.WindSpeedMetersPerSecond, Atmo.Clouds.WindBearingDegrees,
                            Atmo.SimulationSeconds, Atmo.ForceFieldSource, Atmo.WindLatticeCascade,
                            SceneVisFn, CloudVisFn, Ign);
                        Result.CompositeLinearHdr[Off] = Result.CompositeLinearHdr[Off] + ShaftHdr;
                    }
                }
                if (FlareProj.EffectiveGain > 1.0e-4f && Config.DebugChannel == 0u)
                {
                    const float FU = (static_cast<float>(PX) + 0.5f) / static_cast<float>(Result.WidthPx);
                    const float FV = (static_cast<float>(PY) + 0.5f) / static_cast<float>(Result.HeightPx);
                    Result.CompositeLinearHdr[Off] = Result.CompositeLinearHdr[Off] +
                        LensFlareSequence::EvaluateBakedLensFlare(
                            PreparedScene.Atmosphere.LensFlare, FlareProj, FU, FV, AspectRatio);
                }
                const auto Tone = (Config.DebugChannel != 0u)
                    ? Result.CompositeLinearHdr[Off]
                    : ApplyAcesFilmic(Result.CompositeLinearHdr[Off]);
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
    // ENGINES 1, 2, 3: SHARED DIRECT LIGHTING (SHADOW LAB) + BVH4/SKY SPECULAR & GLASS + ENGINE-SPECIFIC GI
    //====================================================================================================================
    Result.DirectLighting = LinearCosineLightIntegration::EvaluateDirectLightingFrameWithAlgorithm(
        Config.ShadowAlgorithm,
        CurrentCamera,
        PreparedScene.ClusterInstances,
        VisTargets,
        PreparedScene.MaterialRegister,
        PreparedScene.SceneTree,
        PreparedScene.Sun,
        PreparedScene.AreaLights,
        &FineContactClip,
        &CoarseContactClip);

    Result.SpecularGlass = SpecularReflectionSequence::EvaluateSpecularRefractionWithSource(
        Config.ReflectionSource,
        CurrentCamera,
        PreparedScene.ClusterInstances,
        VisTargets,
        PrimarySamples,
        PreparedScene.MaterialRegister,
        PreparedScene.BounceSummaries,
        PreparedScene.SceneTree,
        RtxLattice,
        PreparedScene.Sun,
        PreparedScene.SkyRadiance);

    std::vector<Mathematics::LinearRGB> RawIndirectOverPi(TotalPixels, { 0.0f, 0.0f, 0.0f });
    if (Config.GlobalIlluminationEnabled && Config.Engine == WorldRenderEngine::DdgiRaytracing)
    {
        for (std::size_t Off = 0u; Off < TotalPixels; ++Off)
        {
            const auto& Hit = PrimarySamples[Off];
            if (Hit.ValidHit && Hit.MaterialSlot < MatCount)
            {
                RawIndirectOverPi[Off] = RtxLattice.SampleChebyshevIrradianceOverPi(
                    Hit.WorldPosition, Hit.TangentFrame.NormalZ, Hit.WorldViewDirection);
            }
        }
    }
    else if (Config.GlobalIlluminationEnabled && Config.Engine == WorldRenderEngine::SdfGlobalIllumination)
    {
        // Lazily build radiosity mip levels if PrepareWorldScene wasn't called with SdfVolume ready
        SignedDistanceRadiosityVolume LocalRadiosity {};
        const SignedDistanceRadiosityVolume* RadPtr = &RadiosityVolume;
        if (RadiosityVolume.Level0DimX < 2u && PreparedScene.SceneSdf.VoxelSpacingMeters > 0.0f)
        {
            LocalRadiosity.BuildRadiosityLevels(
                PreparedScene.SceneSdf, PreparedScene.SceneTree,
                PreparedScene.BounceSummaries, PreparedScene.Sun, PreparedScene.SkyRadiance);
            RadPtr = &LocalRadiosity;
        }

        for (uint32_t PY = 0u; PY < Result.HeightPx; ++PY)
        {
            for (uint32_t PX = 0u; PX < Result.WidthPx; ++PX)
            {
                const std::size_t Off = static_cast<std::size_t>(PY) * Result.WidthPx + PX;
                const auto& Hit       = PrimarySamples[Off];
                if (Hit.ValidHit && Hit.MaterialSlot < MatCount)
                {
                    RawIndirectOverPi[Off] = RtxLattice.EvaluateSdfRadiosityConeGi(
                        Hit.WorldPosition, Hit.TangentFrame.NormalZ, PX, PY,
                        PreparedScene.SceneSdf, *RadPtr, PreparedScene.SkyRadiance);
                }
            }
        }

        if (Config.SdfGiBilateral)
        {
            std::vector<Mathematics::LinearRGB> FilteredGi;
            ApplyDiffuseGiBilateralFilter(
                Result.WidthPx, Result.HeightPx, CurBasis.CameraOrigin,
                PrimarySamples, RawIndirectOverPi, FilteredGi);
            RawIndirectOverPi.swap(FilteredGi);
        }
    }

    for (uint32_t PY = 0u; PY < Result.HeightPx; ++PY)
    {
        for (uint32_t PX = 0u; PX < Result.WidthPx; ++PX)
        {
            const std::size_t Off = static_cast<std::size_t>(PY) * Result.WidthPx + PX;
            const auto& Hit = PrimarySamples[Off];
            if (Config.DebugChannel != 0u && Hit.ValidHit && Hit.MaterialSlot < MatCount)
            {
                const auto& Mat = PreparedScene.MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);
                Mathematics::LinearRGB Dbg {};
                if (Config.DebugChannel == 8u)
                {
                    Dbg = ApplyAcesFilmic(RawIndirectOverPi[Off] * 1.25f);
                }
                else if (Config.DebugChannel == 12u && Off < Result.DirectLighting.SolarShadows.FilteredVisibility.size())
                {
                    const float V = Result.DirectLighting.SolarShadows.FilteredVisibility[Off];
                    Dbg = { V, V, V };
                }
                else if (Config.DebugChannel == 13u && Off < Result.DirectLighting.CombinedDiffuseAo.size())
                {
                    const float A = Result.DirectLighting.CombinedDiffuseAo[Off];
                    Dbg = { A, A, A };
                }
                else if (Config.DebugChannel == 14u)
                {
                    Dbg = ApplyAcesFilmic(Result.DirectLighting.DirectHdrRadiance[Off]);
                }
                else if (Config.DebugChannel == 15u)
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
                const auto& Mat        = PreparedScene.MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);
                if (PreparedScene.OptionalFluid != nullptr && Config.DebugChannel == 0u)
                {
                    const float FluidShadowT = PreparedScene.OptionalFluid->EvaluateFluidShadowTransmittance(
                        Hit.WorldPosition + Hit.TangentFrame.NormalZ * 0.015f,
                        PreparedScene.Sun.DirectionToSun,
                        32.0f);
                    FinalHdr = FinalHdr * FluidShadowT;
                    const auto ViewDir = Mathematics::NormalizeAxis(CurBasis.CameraOrigin - Hit.WorldPosition);
                    FinalHdr = FinalHdr + EvaluateFluidEmissiveOnSurface(
                        PreparedScene, Hit.WorldPosition, Hit.TangentFrame.NormalZ, ViewDir, Hit.MaterialSlot, Mat);
                }
                const float DiffAo     = Result.DirectLighting.CombinedDiffuseAo[Off];
                const float SpecAo     = Result.DirectLighting.LagardeSpecularAo[Off];
                const float DiffFactor = (1.0f - Mat.Specular.Metalness) *
                                         (1.0f - Mat.Transmission.TransmissionWeight) *
                                         Mat.Diffuse.BaseWeight;

                const auto& IndirectOverPi = RawIndirectOverPi[Off];
                const Mathematics::LinearRGB DiffuseGi {
                    IndirectOverPi.R * Mat.Diffuse.BaseColor.R * DiffFactor * (0.35f + 0.65f * DiffAo),
                    IndirectOverPi.G * Mat.Diffuse.BaseColor.G * DiffFactor * (0.35f + 0.65f * DiffAo),
                    IndirectOverPi.B * Mat.Diffuse.BaseColor.B * DiffFactor * (0.35f + 0.65f * DiffAo),
                };
                Result.IndirectDiffuseHdr[Off] = DiffuseGi;

                if (Config.GlobalIlluminationEnabled)
                {
                    FinalHdr = FinalHdr + DiffuseGi;
                }
                if (Config.ReflectionsEnabled)
                {
                    FinalHdr = FinalHdr + Result.SpecularGlass.IndirectSpecularHdr[Off] * SpecAo;
                }
            }
            else if (PreparedScene.Atmosphere.Medium.AtmosphereEnabled && VisTargets.GlassPeelFront64[Off] == 0ull)
            {
                const float FU = (static_cast<float>(PX) + 0.5f) / static_cast<float>(Result.WidthPx);
                const float FV = (static_cast<float>(PY) + 0.5f) / static_cast<float>(Result.HeightPx);
                const float NdcX = FU * 2.0f - 1.0f;
                const float NdcY = 1.0f - FV * 2.0f;
                const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(
                    CurBasis.ForwardAxis + CurBasis.RightAxis * (NdcX * CurBasis.TanHalfFovX) + CurBasis.UpAxis * (NdcY * CurBasis.TanHalfFovY));
                FinalHdr = EvaluateBackgroundSkyRadiance(
                    PreparedScene, SkySequence, CurBasis.CameraOrigin, RayDir, EvaluateCloudIgn(PX, PY), &CpuCloudField, FU, FV);
            }

            if (VisTargets.GlassPeelFront64[Off] != 0ull)
            {
                FinalHdr = Result.SpecularGlass.RefractedGlassHdr[Off];
            }

            // Depth-clipped LocalClouds + Atmospheric/Local Fog (CPU parity with GPU compute)
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

                if (PreparedScene.OptionalFluid != nullptr)
                {
                    const auto FluidRes = PreparedScene.OptionalFluid->EvaluateFluidSegment(
                        CurBasis.CameraOrigin, RayDir, HitDist, PreparedScene.Sun.DirectionToSun,
                        PreparedScene.Sun.SolarIrradiance, SkyForVol, Ign);
                    FinalHdr = {
                        FinalHdr.R * FluidRes.Transmittance + FluidRes.RadianceHdr.R,
                        FinalHdr.G * FluidRes.Transmittance + FluidRes.RadianceHdr.G,
                        FinalHdr.B * FluidRes.Transmittance + FluidRes.RadianceHdr.B,
                    };
                }

                if (!Atmo.Medium.OrbitalViewEnabled &&
                    (Atmo.Fog.HeightFog.Enabled || Atmo.Fog.AerialFog.Enabled || !Atmo.Fog.LocalFogs.empty()))
                {
                    const auto FogRes = VolumetricFogSequence::EvaluateVolumetricFog(
                        CurBasis.CameraOrigin, RayDir, HitDist, PreparedScene.Sun.DirectionToSun,
                        PreparedScene.Sun.SolarIrradiance, SkyForVol, Atmo.Fog, Atmo.Wind,
                        Atmo.Clouds.WindSpeedMetersPerSecond, Atmo.Clouds.WindBearingDegrees,
                        Atmo.SimulationSeconds, Atmo.ForceFieldSource, Atmo.WindLatticeCascade, Ign);
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
                        if (PreparedScene.SceneTree.QueryRayOccluded(Q)) return 0.0f;
                        return (PreparedScene.OptionalFluid != nullptr)
                            ? PreparedScene.OptionalFluid->EvaluateFluidShadowTransmittance(P, SDir, 32.0f)
                            : 1.0f;
                    };
                    const auto CloudVisFn = [&](const Mathematics::Axis3D& P, const Mathematics::Axis3D& SDir) noexcept -> float
                    {
                        return VolumetricCloudSequence::EvaluateAllSolarCloudTransmittance(P, SDir, Atmo);
                    };
                    FinalHdr = FinalHdr + VolumetricFogSequence::EvaluateVolumetricGodRays(
                        CurBasis.CameraOrigin, RayDir, HitDist, PreparedScene.Sun.DirectionToSun,
                        PreparedScene.Sun.SolarIrradiance, Atmo.Fog, Atmo.Wind,
                        Atmo.Clouds.WindSpeedMetersPerSecond, Atmo.Clouds.WindBearingDegrees,
                        Atmo.SimulationSeconds, Atmo.ForceFieldSource, Atmo.WindLatticeCascade,
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
