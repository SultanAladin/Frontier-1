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

[[nodiscard]] Mathematics::LinearRGB EvaluateBackgroundSkyRadiance(
    const WorldSceneSpecification& Scene,
    const AtmosphericSkySequence&  SkySeq,
    Mathematics::Axis3D            RayOrigin,
    Mathematics::Axis3D            RayDir,
    float                          PixelJitter01 = -1.0f) noexcept
{
    const auto& Atmo = Scene.Atmosphere;
    if (!Atmo.Medium.AtmosphereEnabled)
    {
        return EvaluateSkyDome(RayDir, Scene.SkyRadiance, nullptr, nullptr);
    }

    const auto SolarSolved = CelestialEphemerisSolver::SolveSolarEphemeris(Atmo.Solar);
    const auto SolarTrans  = AtmosphericSkySequence::ComputeSolarTransmittance(
        SolarSolved.WorldDirectionToSun, Atmo.Medium);
    const auto SkyRad      = SkySeq.SampleSkyViewDirection(RayDir);
    const auto SunRad      = AtmosphericSkySequence::EvaluateSolarCoronaRadiance(
        RayDir, SolarSolved.WorldDirectionToSun, Atmo.Solar, SolarTrans, SolarSolved.EffectiveSolarTint);
    const auto MoonStarRad = LunarStarfieldSequence::EvaluateCelestialField(
        RayDir, SolarSolved.WorldDirectionToSun, Atmo.Moons, Atmo.Stars, Atmo.SimulationSeconds);

    float CloudTrans = 1.0f;
    const auto LowAngleSky = SkySeq.SampleSkyViewDirection(
        Mathematics::NormalizeAxis({ RayDir.X, RayDir.Y, std::max(0.05f, RayDir.Z) }));
    WorldAtmosphereConfiguration DeckOnlyAtmo = Atmo;
    DeckOnlyAtmo.LocalCloud.LocalEnabled      = false;
    DeckOnlyAtmo.LocalClouds.clear();
    const auto CloudRad = VolumetricCloudSequence::EvaluateVolumetricCloudsWithWind(
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

[[nodiscard]] Mathematics::LinearRGB TracePathSample(
    Mathematics::Axis3D                            Origin,
    Mathematics::Axis3D                            Direction,
    const WorldSceneSpecification&                 Scene,
    const AtmosphericSkySequence&                  SkySeq,
    uint32_t                                       MaxBounces,
    uint32_t&                                      RngState) noexcept
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

    for (uint32_t Bounce = 0u; Bounce < MaxBounces; ++Bounce)
    {
        SpatialAcceleration::RaySegmentQuery Query {};
        Query.Origin          = RayO;
        Query.UnitDirection   = RayD;
        Query.MinimumDistance = 1.0e-3f;
        Query.MaximumDistance = 160.0f;

        const auto Hit = Scene.SceneTree.TraceClosestRay(Query, false);
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
        const float DiffProb = std::clamp((1.0f - Mat.Specular.Metalness) * 0.65f, 0.10f, 0.85f);
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
        }
        else
        {
            const auto HalfV = MaterialSpace::Scattering::SampleAnisotropicSphericalCapVndf(LocalWo, Alpha, U1, U2);
            LocalWi = HalfV * (2.0f * Mathematics::EvaluateDot(LocalWo, HalfV)) - LocalWo;
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

        // Russian Roulette after bounce 2
        if (Bounce >= 2u)
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

    // Clamp extreme fireflies per sample
    return {
        std::min(Radiance.R, 18.0f),
        std::min(Radiance.G, 18.0f),
        std::min(Radiance.B, 18.0f),
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
    std::vector<Mathematics::LinearRGB>&           InOutRadiance)
{
    const std::size_t Total = static_cast<std::size_t>(WidthPx) * HeightPx;
    if (Total == 0u || InOutRadiance.size() != Total)
    {
        return;
    }

    const uint32_t MatCount = MaterialRegister.QuerySlotCount();
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
                if (!CHit.ValidHit || CHit.MaterialSlot >= MatCount)
                {
                    Scratch[CenterOff] = InOutRadiance[CenterOff];
                    continue;
                }
                const float CDepth = Mathematics::EvaluateLength(CHit.WorldPosition - CameraOrigin);
                const auto& CMat   = MaterialRegister.InspectAuthoredMaterial(CHit.MaterialSlot);
                const auto& CRad   = InOutRadiance[CenterOff];
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
                        if (!NHit.ValidHit || NHit.MaterialSlot >= MatCount)
                        {
                            continue;
                        }
                        const float NDepth = Mathematics::EvaluateLength(NHit.WorldPosition - CameraOrigin);
                        const auto& NMat   = MaterialRegister.InspectAuthoredMaterial(NHit.MaterialSlot);
                        const auto& NRad   = InOutRadiance[NOff];
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
        const float D = std::clamp(1.0f - Mathematics::EvaluateLength(Hit.WorldPosition - CameraOrigin) / 20.0f, 0.0f, 1.0f);
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
                    const float NdcX = ((static_cast<float>(PX) + 0.5f) / static_cast<float>(Result.WidthPx)) * 2.0f - 1.0f;
                    const float NdcY = 1.0f - ((static_cast<float>(PY) + 0.5f) / static_cast<float>(Result.HeightPx)) * 2.0f;
                    const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(
                        CurBasis.ForwardAxis + CurBasis.RightAxis * (NdcX * CurBasis.TanHalfFovX) + CurBasis.UpAxis * (NdcY * CurBasis.TanHalfFovY));
                    OutHdr = EvaluateBackgroundSkyRadiance(
                        PreparedScene, SkySequence, CurBasis.CameraOrigin, RayDir, EvaluateCloudIgn(PX, PY));
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
                Result.DisplayRgba8[Off * 4u + 0u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.R) * 255.0f));
                Result.DisplayRgba8[Off * 4u + 1u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.G) * 255.0f));
                Result.DisplayRgba8[Off * 4u + 2u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.B) * 255.0f));
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
        const uint32_t Spp = std::clamp(Config.PathTraceSamples, 1u, 128u);
        const uint32_t MaxB = std::clamp(Config.PathTraceMaxBounces, 1u, 8u);

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

                uint32_t Rng = (PX * 1973u + PY * 9277u + 26699u) | 1u;
                Mathematics::LinearRGB Sum {};
                for (uint32_t S = 0u; S < Spp; ++S)
                {
                    const float Jx = (static_cast<float>(PX) + PcgFloat01(Rng)) / static_cast<float>(Result.WidthPx);
                    const float Jy = (static_cast<float>(PY) + PcgFloat01(Rng)) / static_cast<float>(Result.HeightPx);
                    const float NdcX = Jx * 2.0f - 1.0f;
                    const float NdcY = 1.0f - Jy * 2.0f;
                    const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(
                        CurBasis.ForwardAxis + CurBasis.RightAxis * (NdcX * CurBasis.TanHalfFovX) + CurBasis.UpAxis * (NdcY * CurBasis.TanHalfFovY));
                    Sum = Sum + TracePathSample(CurBasis.CameraOrigin, RayDir, PreparedScene, SkySequence, MaxB, Rng);
                }
                Result.CompositeLinearHdr[Off] = Sum * (1.0f / static_cast<float>(Spp));
            }
        }

        if (Config.PathTraceDenoise && Config.DebugChannel == 0u)
        {
            ApplyATrousWaveletDenoiser(
                Result.WidthPx, Result.HeightPx, CurBasis.CameraOrigin, PrimarySamples,
                PreparedScene.MaterialRegister, Result.CompositeLinearHdr);
        }

        for (uint32_t PY = 0u; PY < Result.HeightPx; ++PY)
        {
            for (uint32_t PX = 0u; PX < Result.WidthPx; ++PX)
            {
                const std::size_t Off = static_cast<std::size_t>(PY) * Result.WidthPx + PX;
                const auto& Hit = PrimarySamples[Off];
                if (Config.DebugChannel == 0u && PreparedScene.OptionalFluid != nullptr)
                {
                    if (Hit.ValidHit && Hit.MaterialSlot < MatCount)
                    {
                        const auto& Mat = PreparedScene.MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);
                        const auto ViewDir = Mathematics::NormalizeAxis(CurBasis.CameraOrigin - Hit.WorldPosition);
                        Result.CompositeLinearHdr[Off] = Result.CompositeLinearHdr[Off] +
                            EvaluateFluidEmissiveOnSurface(PreparedScene, Hit.WorldPosition, Hit.TangentFrame.NormalZ, ViewDir, Hit.MaterialSlot, Mat);
                    }
                    const float NdcX = ((static_cast<float>(PX) + 0.5f) / static_cast<float>(Result.WidthPx)) * 2.0f - 1.0f;
                    const float NdcY = 1.0f - ((static_cast<float>(PY) + 0.5f) / static_cast<float>(Result.HeightPx)) * 2.0f;
                    const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(
                        CurBasis.ForwardAxis + CurBasis.RightAxis * (NdcX * CurBasis.TanHalfFovX) + CurBasis.UpAxis * (NdcY * CurBasis.TanHalfFovY));
                    const float HitDist = Hit.ValidHit ? Mathematics::EvaluateLength(Hit.WorldPosition - CurBasis.CameraOrigin) : 3500.0f;
                    const auto SkyForVol = PreparedScene.Atmosphere.Medium.AtmosphereEnabled
                        ? SkySequence.SampleSkyViewDirection(RayDir)
                        : PreparedScene.SkyRadiance;
                    const auto FluidRes = PreparedScene.OptionalFluid->EvaluateFluidSegment(
                        CurBasis.CameraOrigin, RayDir, HitDist, PreparedScene.Sun.DirectionToSun,
                        PreparedScene.Sun.SolarIrradiance, SkyForVol, EvaluateCloudIgn(PX, PY));
                    Result.CompositeLinearHdr[Off] = {
                        Result.CompositeLinearHdr[Off].R * FluidRes.Transmittance + FluidRes.RadianceHdr.R,
                        Result.CompositeLinearHdr[Off].G * FluidRes.Transmittance + FluidRes.RadianceHdr.G,
                        Result.CompositeLinearHdr[Off].B * FluidRes.Transmittance + FluidRes.RadianceHdr.B,
                    };
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
                Result.DisplayRgba8[Off * 4u + 0u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.R) * 255.0f));
                Result.DisplayRgba8[Off * 4u + 1u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.G) * 255.0f));
                Result.DisplayRgba8[Off * 4u + 2u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.B) * 255.0f));
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
    if (Config.Engine == WorldRenderEngine::DdgiRaytracing)
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
    else if (Config.Engine == WorldRenderEngine::SdfGlobalIllumination)
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
                const auto Dbg  = EvaluateDebugChannelColor(Config.DebugChannel, CurBasis.CameraOrigin, Hit, Mat);
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

                FinalHdr = FinalHdr + DiffuseGi + Result.SpecularGlass.IndirectSpecularHdr[Off] * SpecAo;
            }
            else if (PreparedScene.Atmosphere.Medium.AtmosphereEnabled && VisTargets.GlassPeelFront64[Off] == 0ull)
            {
                const float NdcX = ((static_cast<float>(PX) + 0.5f) / static_cast<float>(Result.WidthPx)) * 2.0f - 1.0f;
                const float NdcY = 1.0f - ((static_cast<float>(PY) + 0.5f) / static_cast<float>(Result.HeightPx)) * 2.0f;
                const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(
                    CurBasis.ForwardAxis + CurBasis.RightAxis * (NdcX * CurBasis.TanHalfFovX) + CurBasis.UpAxis * (NdcY * CurBasis.TanHalfFovY));
                FinalHdr = EvaluateBackgroundSkyRadiance(
                    PreparedScene, SkySequence, CurBasis.CameraOrigin, RayDir, EvaluateCloudIgn(PX, PY));
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

                if (Atmo.Fog.HeightFog.Enabled || Atmo.Fog.AerialFog.Enabled || !Atmo.Fog.LocalFogs.empty())
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
            Result.DisplayRgba8[Off * 4u + 0u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.R) * 255.0f));
            Result.DisplayRgba8[Off * 4u + 1u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.G) * 255.0f));
            Result.DisplayRgba8[Off * 4u + 2u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.B) * 255.0f));
            Result.DisplayRgba8[Off * 4u + 3u] = 255u;
        }
    }

    PreviousHiZ = std::move(CurrentHiZ);
    return Result;
}

} // namespace Frontier::GeometricRaster
