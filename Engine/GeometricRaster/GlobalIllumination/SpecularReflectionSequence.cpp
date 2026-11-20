/*============================================================================================================================================
                                                        SPECULARREFLECTIONSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Specular & Abbe Glass Refraction: BVH4 WideBoundingTree Raytrace or Analytical Sky Environment

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/SpecularReflectionSequence.h"
#include "Engine/GeometrySpace/Analysis/CurvatureAnalysis.h"
#include "Engine/MaterialSpace/EnergyCompensation/DirectionalAlbedoIntegral.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier::GeometricRaster
{

namespace
{

[[nodiscard]] float DecodeFp16Val(uint16_t HalfBits) noexcept
{
    const uint32_t Sign = static_cast<uint32_t>(HalfBits & 0x8000u) << 16u;
    const uint32_t Exp  = (HalfBits >> 10u) & 0x1Fu;
    const uint32_t Mant = HalfBits & 0x03FFu;
    uint32_t Bits32     = 0u;
    if (Exp == 0u)
    {
        if (Mant == 0u) Bits32 = Sign;
        else
        {
            const float Sub = static_cast<float>(Mant) * (1.0f / 16777216.0f);
            return (Sign != 0u) ? -Sub : Sub;
        }
    }
    else if (Exp == 31u) Bits32 = Sign | 0x7F800000u | (Mant << 13u);
    else                 Bits32 = Sign | ((Exp + 112u) << 23u) | (Mant << 13u);
    float Result = 0.0f;
    std::memcpy(&Result, &Bits32, sizeof(float));
    return Result;
}

[[nodiscard]] Mathematics::LinearRGB UnpackBounceDiffuse(const MaterialSpace::BounceSlabEntry& Slab) noexcept
{
    return {
        DecodeFp16Val(static_cast<uint16_t>(Slab.DiffuseAlbedoRgHalf & 0xFFFFu)),
        DecodeFp16Val(static_cast<uint16_t>((Slab.DiffuseAlbedoRgHalf >> 16u) & 0xFFFFu)),
        DecodeFp16Val(static_cast<uint16_t>(Slab.DiffuseAlbedoBAndRoughnessHalf & 0xFFFFu)),
    };
}

[[nodiscard]] Mathematics::LinearRGB UnpackBounceEmissive(const MaterialSpace::BounceSlabEntry& Slab) noexcept
{
    return {
        DecodeFp16Val(static_cast<uint16_t>(Slab.EmissiveExitanceRgHalf & 0xFFFFu)),
        DecodeFp16Val(static_cast<uint16_t>((Slab.EmissiveExitanceRgHalf >> 16u) & 0xFFFFu)),
        DecodeFp16Val(static_cast<uint16_t>(Slab.EmissiveExitanceBAndTransHalf & 0xFFFFu)),
    };
}

[[nodiscard]] Mathematics::LinearRGB ShadeSecondaryRayHit(
    const SpatialAcceleration::WideBoundingTree&    SceneTree,
    const MaterialSpace::CompactMaterialRegister&   MaterialRegister,
    std::span<const MaterialSpace::BounceSlabEntry> BounceSummaries,
    const SolarDirectionalSource&                   Sun,
    std::span<const PolygonalAreaQuadLight>         AreaLights,
    Mathematics::Axis3D                             RayOrigin,
    Mathematics::Axis3D                             RayDirection,
    Mathematics::LinearRGB                          SkyRadiance,
    bool&                                           OutHitGeometry) noexcept
{
    SpatialAcceleration::RaySegmentQuery Query {};
    Query.Origin          = RayOrigin;
    Query.UnitDirection   = RayDirection;
    Query.MinimumDistance = 1.5e-3f;
    Query.MaximumDistance = 90.0f;

    const auto Hit = SceneTree.TraceClosestRay(Query, false);
    if (!Hit.HitConfirmed)
    {
        OutHitGeometry = false;
        const float Up = std::clamp(RayDirection.Z * 0.5f + 0.5f, 0.0f, 1.0f);
        return SkyRadiance * (0.45f + 0.55f * Up);
    }

    OutHitGeometry = true;
    const Mathematics::Axis3D HitPos = RayOrigin + RayDirection * Hit.HitDistanceMeters;
    const Mathematics::Axis3D HitNrm = (Mathematics::EvaluateDot(Hit.GeometricNormal, RayDirection) < 0.0f)
        ? Hit.GeometricNormal
        : (Hit.GeometricNormal * -1.0f);
    const uint16_t Slot = (Hit.MaterialOrdinal < BounceSummaries.size()) ? Hit.MaterialOrdinal : 0u;
    const auto& Slab    = BounceSummaries[Slot];

    Mathematics::LinearRGB OutRad {};
    const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis(Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
    bool LitBySun = false;
    if (Mathematics::EvaluateDot(HitNrm, SunDir) > 0.0f)
    {
        SpatialAcceleration::RaySegmentQuery SunShadow {};
        SunShadow.Origin          = HitPos + HitNrm * 2.5e-3f;
        SunShadow.UnitDirection   = SunDir;
        SunShadow.MinimumDistance = 1.0e-3f;
        SunShadow.MaximumDistance = 120.0f;
        if (!SceneTree.QueryRayOccluded(SunShadow))
        {
            OutRad = MaterialSpace::EvaluateBounceSlabRadiance(
                Slab, HitNrm, RayDirection * -1.0f, SunDir, Sun.SolarIrradiance);
            LitBySun = true;
        }
    }
    if (!LitBySun)
    {
        OutRad = UnpackBounceEmissive(Slab);
    }

    if (!AreaLights.empty() && Slot < MaterialRegister.QuerySlotCount())
    {
        const auto& Mat         = MaterialRegister.InspectAuthoredMaterial(Slot);
        const uint32_t LobeMask = MaterialRegister.InspectCompactEntry(Slot).ActiveLobeMask;
        AnalyticResolvedSurfaceSample SecSample {};
        SecSample.ValidHit           = true;
        SecSample.MaterialSlot       = Slot;
        SecSample.WorldPosition      = HitPos;
        SecSample.WorldViewDirection = RayDirection * -1.0f;
        SecSample.TangentFrame.NormalZ = HitNrm;
        GeometrySpace::ConstructDuffOrthonormalBasis(
            HitNrm, SecSample.TangentFrame.TangentX, SecSample.TangentFrame.BitangentY);

        for (const auto& Light : AreaLights)
        {
            const auto AreaRad = LinearCosineLightIntegration::EvaluateVisibilityGatedQuadLight(
                Light, SecSample, Mat, LobeMask, SceneTree);
            OutRad = OutRad + AreaRad;
        }
    }

    const auto DiffAlb = UnpackBounceDiffuse(Slab);
    const float SkyUp  = std::clamp(HitNrm.Z * 0.5f + 0.5f, 0.0f, 1.0f);
    const Mathematics::LinearRGB SkyFill = SkyRadiance * (0.45f + 0.55f * SkyUp) * 0.22f;
    OutRad.R += SkyFill.R * DiffAlb.R;
    OutRad.G += SkyFill.G * DiffAlb.G;
    OutRad.B += SkyFill.B * DiffAlb.B;
    return OutRad;
}

[[nodiscard]] Mathematics::LinearRGB EvaluateSpecularDirectionalWeight(
    const MaterialSpace::SurfaceMaterial& Mat,
    float                                 NdotV) noexcept
{
    const auto& AlbedoSheet = MaterialSpace::InspectGlobalDirectionalAlbedoSheet();
    const auto  AlbO        = MaterialSpace::SampleDirectionalAlbedo(AlbedoSheet, NdotV, Mat.Specular.Roughness);
    const float DielF0      = ((Mat.Specular.DielectricIOR - 1.0f) / (Mat.Specular.DielectricIOR + 1.0f)) *
                              ((Mat.Specular.DielectricIOR - 1.0f) / (Mat.Specular.DielectricIOR + 1.0f)) *
                              Mat.Specular.SpecularWeight;
    const auto  MetalF      = MaterialSpace::Scattering::EvaluateConductorEdgeTintFresnel(
        NdotV, Mat.Diffuse.BaseColor, Mat.Specular.EdgeTintF82);
    const float DielF       = DielF0 + (Mat.Specular.SpecularWeight - DielF0) * std::pow(1.0f - std::clamp(NdotV, 0.0f, 1.0f), 5.0f);
    const Mathematics::LinearRGB F {
        DielF * (1.0f - Mat.Specular.Metalness) + MetalF.R * Mat.Specular.Metalness,
        DielF * (1.0f - Mat.Specular.Metalness) + MetalF.G * Mat.Specular.Metalness,
        DielF * (1.0f - Mat.Specular.Metalness) + MetalF.B * Mat.Specular.Metalness,
    };
    // Include Turquin multi-scatter compensation from 4 KB DirectionalAlbedoSheet!
    const float MsBoost = (1.0f - AlbO.DirectionalAlbedoE) / std::max(0.05f, AlbO.AverageAlbedoEavg);
    return {
        std::clamp(F.R * (AlbO.DirectionalAlbedoE + F.R * MsBoost * 0.35f), 0.0f, 1.0f),
        std::clamp(F.G * (AlbO.DirectionalAlbedoE + F.G * MsBoost * 0.35f), 0.0f, 1.0f),
        std::clamp(F.B * (AlbO.DirectionalAlbedoE + F.B * MsBoost * 0.35f), 0.0f, 1.0f),
    };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                            SPECULAR REFLECTION SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

SpecularReflectionBuffer SpecularReflectionSequence::EvaluateSpecularRefractionWithSource(
    SpecularReflectionSource                        Source,
    const PerspectiveCameraBasis&                   Camera,
    std::span<const RigidClusterInstance>           Instances,
    const VisibilityBufferTargets&                  VisibilityTargets,
    std::span<const AnalyticResolvedSurfaceSample>  PrimarySamples,
    const MaterialSpace::CompactMaterialRegister&   MaterialRegister,
    std::span<const MaterialSpace::BounceSlabEntry> BounceSummaries,
    const SpatialAcceleration::WideBoundingTree&    SceneTree,
    const SolarDirectionalSource&                   Sun,
    Mathematics::LinearRGB                          SkyRadiance,
    std::span<const PolygonalAreaQuadLight>         AreaLights)
{
    if (Source == SpecularReflectionSource::WideTreeRaytrace)
    {
        return EvaluateRtxSpecularRefraction(
            Camera,
            Instances,
            VisibilityTargets,
            PrimarySamples,
            MaterialRegister,
            BounceSummaries,
            SceneTree,
            Sun,
            SkyRadiance,
            AreaLights);
    }

    // SkyEnvironment: Analytical Sky Dome gradient + Fresnel directional albedo weight
    SpecularReflectionBuffer Out {};
    Out.WidthPx  = VisibilityTargets.WidthPx;
    Out.HeightPx = VisibilityTargets.HeightPx;
    const std::size_t TotalPixels = static_cast<std::size_t>(Out.WidthPx) * Out.HeightPx;
    Out.IndirectSpecularHdr.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });
    Out.RefractedGlassHdr.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });

    const OrthonormalCameraBasis Basis = ComputeCameraBasis(Camera);
    const uint32_t MatCount            = MaterialRegister.QuerySlotCount();
    const auto EvaluateSkyEnv = [&](Mathematics::Axis3D Dir) noexcept -> Mathematics::LinearRGB
    {
        const float Up = std::clamp(Dir.Z * 0.5f + 0.5f, 0.0f, 1.0f);
        return SkyRadiance * (0.65f + 0.70f * Up);
    };

    for (uint32_t PY = 0u; PY < Out.HeightPx; ++PY)
    {
        for (uint32_t PX = 0u; PX < Out.WidthPx; ++PX)
        {
            const std::size_t Off = static_cast<std::size_t>(PY) * Out.WidthPx + PX;
            const auto& Hit       = PrimarySamples[Off];

            if (Hit.ValidHit && Hit.MaterialSlot < MatCount)
            {
                const auto& Mat  = MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);
                const float NdotV = std::clamp(Mathematics::EvaluateDot(Hit.TangentFrame.NormalZ, Hit.WorldViewDirection), 0.01f, 1.0f);
                const Mathematics::Axis3D ReflDir = Hit.TangentFrame.NormalZ * (2.0f * NdotV) - Hit.WorldViewDirection;
                const Mathematics::LinearRGB EnvRad = EvaluateSkyEnv(ReflDir);
                const auto SpecW = EvaluateSpecularDirectionalWeight(Mat, NdotV);
                Out.IndirectSpecularHdr[Off] = {
                    EnvRad.R * SpecW.R,
                    EnvRad.G * SpecW.G,
                    EnvRad.B * SpecW.B,
                };
            }

            const uint64_t PeelFront = VisibilityTargets.GlassPeelFront64[Off];
            if (PeelFront != 0ull)
            {
                const auto GlassHit = VisibilityMaterialResolve::ReconstructAnalyticPixelHit(
                    PX, PY, PeelFront, Basis, Basis, Instances);
                if (GlassHit.ValidHit && GlassHit.MaterialSlot < MatCount)
                {
                    const auto& GlassMat = MaterialRegister.InspectAuthoredMaterial(GlassHit.MaterialSlot);
                    const float CosI     = std::clamp(Mathematics::EvaluateDot(GlassHit.TangentFrame.NormalZ, GlassHit.WorldViewDirection), 0.0f, 1.0f);
                    const float FresR    = MaterialSpace::Scattering::EvaluateExactDielectricFresnel(CosI, GlassMat.Specular.DielectricIOR);
                    const Mathematics::Axis3D Inc = GlassHit.WorldViewDirection * -1.0f;
                    const Mathematics::Axis3D ReflDir = Inc + GlassHit.TangentFrame.NormalZ * (2.0f * CosI);
                    const auto EnvRefl = EvaluateSkyEnv(ReflDir);
                    Out.RefractedGlassHdr[Off] = {
                        SkyRadiance.R * (1.0f - FresR) * GlassMat.Transmission.AttenuationColor.R + EnvRefl.R * FresR,
                        SkyRadiance.G * (1.0f - FresR) * GlassMat.Transmission.AttenuationColor.G + EnvRefl.G * FresR,
                        SkyRadiance.B * (1.0f - FresR) * GlassMat.Transmission.AttenuationColor.B + EnvRefl.B * FresR,
                    };
                }
            }
        }
    }
    return Out;
}

SpecularReflectionBuffer SpecularReflectionSequence::EvaluateRtxSpecularRefraction(
    const PerspectiveCameraBasis&                   Camera,
    std::span<const RigidClusterInstance>           Instances,
    const VisibilityBufferTargets&                  VisibilityTargets,
    std::span<const AnalyticResolvedSurfaceSample>  PrimarySamples,
    const MaterialSpace::CompactMaterialRegister&   MaterialRegister,
    std::span<const MaterialSpace::BounceSlabEntry> BounceSummaries,
    const SpatialAcceleration::WideBoundingTree&    SceneTree,
    const SolarDirectionalSource&                   Sun,
    Mathematics::LinearRGB                          SkyRadiance,
    std::span<const PolygonalAreaQuadLight>         AreaLights)
{
    SpecularReflectionBuffer Out {};
    Out.WidthPx  = VisibilityTargets.WidthPx;
    Out.HeightPx = VisibilityTargets.HeightPx;
    const std::size_t TotalPixels = static_cast<std::size_t>(Out.WidthPx) * Out.HeightPx;
    Out.IndirectSpecularHdr.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });
    Out.RefractedGlassHdr.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });

    const OrthonormalCameraBasis Basis = ComputeCameraBasis(Camera);
    const uint32_t MatCount = MaterialRegister.QuerySlotCount();

    for (uint32_t PY = 0u; PY < Out.HeightPx; ++PY)
    {
        for (uint32_t PX = 0u; PX < Out.WidthPx; ++PX)
        {
            const std::size_t Off = static_cast<std::size_t>(PY) * Out.WidthPx + PX;
            const auto& Hit       = PrimarySamples[Off];

            if (Hit.ValidHit && Hit.MaterialSlot < MatCount)
            {
                const auto& Mat = MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);
                const auto LocalWo = Hit.TangentFrame.WorldToLocal(Hit.WorldViewDirection);
                if (LocalWo.Z > 1.0e-4f)
                {
                    const float EffRough = (Mat.Clearcoat.CoatWeight > 0.5f)
                        ? std::min(Mat.Clearcoat.CoatRoughness, Mat.Specular.Roughness)
                        : Mat.Specular.Roughness;

                    // Glossy/Mirror surfaces (Roughness < 0.38) trace crisp BVH4 VNDF rays; rough walls sample analytical sky dome
                    Mathematics::LinearRGB IncomingRad {};
                    if (EffRough < 0.38f)
                    {
                        const uint32_t SampleCount = (EffRough < 0.06f) ? 1u : 6u;
                        const auto Alpha = MaterialSpace::Scattering::ComputeAnisotropicAlpha(EffRough * 0.45f, Mat.Specular.Anisotropy);
                        uint32_t Rng = ((PX * 747796405u) ^ (PY * 2891336453u) ^ 0x9E3779B9u) | 1u;
                        uint32_t ValidSamples = 0u;
                        for (uint32_t SIdx = 0u; SIdx < SampleCount; ++SIdx)
                        {
                            Rng = Rng * 747796405u + 2891336453u;
                            uint32_t W = ((Rng >> ((Rng >> 28u) + 4u)) ^ Rng) * 277803737u;
                            W ^= (W >> 22u);
                            const float JitterA = static_cast<float>(W & 0xFFFFu) / 65536.0f;
                            const float JitterB = static_cast<float>((W >> 16u) & 0xFFFFu) / 65536.0f;
                            const float U1 = (SampleCount == 1u)
                                ? JitterA
                                : std::clamp((static_cast<float>(SIdx) + JitterA) / static_cast<float>(SampleCount), 1.0e-4f, 0.9999f);
                            const float U2 = JitterB;
                            const auto HalfV = MaterialSpace::Scattering::SampleAnisotropicSphericalCapVndf(LocalWo, Alpha, U1, U2);
                            const auto LocalWi = HalfV * (2.0f * Mathematics::EvaluateDot(LocalWo, HalfV)) - LocalWo;

                            if (LocalWi.Z > 1.0e-4f)
                            {
                                const Mathematics::Axis3D WorldReflDir = Hit.TangentFrame.LocalToWorld(LocalWi);
                                bool HitGeom = false;
                                const auto Rad = ShadeSecondaryRayHit(
                                    SceneTree, MaterialRegister, BounceSummaries, Sun, AreaLights,
                                    Hit.WorldPosition + Hit.TangentFrame.NormalZ * 2.5e-3f,
                                    WorldReflDir, SkyRadiance, HitGeom);
                                IncomingRad = IncomingRad + Rad;
                                ++ValidSamples;
                                if (HitGeom && SIdx == 0u) ++Out.RaytracedHitPixelCount;
                            }
                        }
                        if (ValidSamples > 1u)
                        {
                            IncomingRad = IncomingRad * (1.0f / static_cast<float>(ValidSamples));
                        }
                    }
                    else
                    {
                        const Mathematics::Axis3D MirrorDir =
                            Hit.TangentFrame.NormalZ * (2.0f * LocalWo.Z) - Hit.WorldViewDirection;
                        const float Up = std::clamp(MirrorDir.Z * 0.5f + 0.5f, 0.0f, 1.0f);
                        IncomingRad = SkyRadiance * (0.65f + 0.70f * Up);
                    }

                    const auto SpecW = EvaluateSpecularDirectionalWeight(Mat, LocalWo.Z);
                    Out.IndirectSpecularHdr[Off] = {
                        IncomingRad.R * SpecW.R,
                        IncomingRad.G * SpecW.G,
                        IncomingRad.B * SpecW.B,
                    };
                }
            }

            // Solid / 2-Layer Abbe-Dispersed Dielectric Glass Refraction Raytrace!
            const uint64_t PeelFront = VisibilityTargets.GlassPeelFront64[Off];
            if (PeelFront != 0ull)
            {
                const auto GlassHit = VisibilityMaterialResolve::ReconstructAnalyticPixelHit(
                    PX, PY, PeelFront, Basis, Basis, Instances);
                if (GlassHit.ValidHit && GlassHit.MaterialSlot < MatCount)
                {
                    const auto& GlassMat = MaterialRegister.InspectAuthoredMaterial(GlassHit.MaterialSlot);
                    const Mathematics::Axis3D Nrm = GlassHit.TangentFrame.NormalZ;
                    const Mathematics::Axis3D Inc = GlassHit.WorldViewDirection * -1.0f;
                    const float CosI   = std::clamp(-Mathematics::EvaluateDot(Inc, Nrm), 0.0f, 1.0f);
                    const float FresR  = MaterialSpace::Scattering::EvaluateExactDielectricFresnel(CosI, GlassMat.Specular.DielectricIOR);

                    // Refract Red & Blue dispersed rays via Abbe V_d number + reflect Fresnel coat!
                    const auto DispIor = MaterialSpace::Scattering::ComputeAbbeDispersedIor(
                        GlassMat.Specular.DielectricIOR, GlassMat.Transmission.AbbeDispersionNumber);

                    const auto TraceDispersedBand = [&](float BandIor) noexcept -> Mathematics::LinearRGB
                    {
                        const float EtaIn  = 1.0f / std::max(1.01f, BandIor);
                        const float Sin2In = EtaIn * EtaIn * std::max(0.0f, 1.0f - CosI * CosI);
                        Mathematics::Axis3D ExitOrig = GlassHit.WorldPosition - Nrm * 3.0e-3f;
                        Mathematics::Axis3D ExitDir  = Inc + Nrm * (2.0f * CosI);
                        if (Sin2In < 1.0f)
                        {
                            const Mathematics::Axis3D InsideDir = Mathematics::NormalizeAxis(
                                Inc * EtaIn + Nrm * (EtaIn * CosI - std::sqrt(1.0f - Sin2In)));
                            ExitDir = InsideDir;

                            if (GlassHit.InstanceOrdinal < Instances.size())
                            {
                                const Mathematics::Axis3D Center = Instances[GlassHit.InstanceOrdinal].WorldTranslation;
                                const float Radius = std::max(0.02f, Mathematics::EvaluateLength(GlassHit.WorldPosition - Center));
                                const Mathematics::Axis3D StartP = GlassHit.WorldPosition + InsideDir * 2.0e-3f;
                                const Mathematics::Axis3D Oc     = StartP - Center;
                                const float B                    = Mathematics::EvaluateDot(Oc, InsideDir);
                                const float C                    = Mathematics::EvaluateDot(Oc, Oc) - Radius * Radius;
                                const float Disc                 = B * B - C;
                                if (Disc > 0.0f)
                                {
                                    const float TOut = -B + std::sqrt(Disc);
                                    if (TOut > 0.0f)
                                    {
                                        const Mathematics::Axis3D ExitPos = StartP + InsideDir * TOut;
                                        const Mathematics::Axis3D OutNrm  = Mathematics::NormalizeAxis(ExitPos - Center, Nrm);
                                        const float CosOut  = std::clamp(Mathematics::EvaluateDot(InsideDir, OutNrm), 0.0f, 1.0f);
                                        const float EtaOut  = std::max(1.01f, BandIor);
                                        const float Sin2Out = EtaOut * EtaOut * std::max(0.0f, 1.0f - CosOut * CosOut);
                                        if (Sin2Out < 1.0f)
                                        {
                                            ExitDir = Mathematics::NormalizeAxis(
                                                InsideDir * EtaOut + OutNrm * (std::sqrt(1.0f - Sin2Out) - EtaOut * CosOut));
                                        }
                                        else
                                        {
                                            ExitDir = InsideDir - OutNrm * (2.0f * Mathematics::EvaluateDot(InsideDir, OutNrm));
                                        }
                                        ExitOrig = ExitPos + OutNrm * 3.0e-3f;
                                    }
                                }
                            }
                        }
                        bool Dummy = false;
                        return ShadeSecondaryRayHit(
                            SceneTree, MaterialRegister, BounceSummaries, Sun, AreaLights,
                            ExitOrig, ExitDir, SkyRadiance, Dummy);
                    };

                    const auto RadR = TraceDispersedBand(DispIor.X);
                    const auto RadG = TraceDispersedBand(DispIor.Y);
                    const auto RadB = TraceDispersedBand(DispIor.Z);

                    bool DummyRefl = false;
                    const Mathematics::Axis3D ReflDir = Inc + Nrm * (2.0f * CosI);
                    const auto ReflRad = ShadeSecondaryRayHit(
                        SceneTree, MaterialRegister, BounceSummaries, Sun, AreaLights,
                        GlassHit.WorldPosition + Nrm * 3.0e-3f, ReflDir, SkyRadiance, DummyRefl);

                    Out.RefractedGlassHdr[Off] = {
                        RadR.R * (1.0f - FresR) * GlassMat.Transmission.AttenuationColor.R + ReflRad.R * FresR,
                        RadG.G * (1.0f - FresR) * GlassMat.Transmission.AttenuationColor.G + ReflRad.G * FresR,
                        RadB.B * (1.0f - FresR) * GlassMat.Transmission.AttenuationColor.B + ReflRad.B * FresR,
                    };
                }
            }
        }
    }

    return Out;
}

SpecularReflectionBuffer SpecularReflectionSequence::EvaluateGtxHiZSpecularRefraction(
    const PerspectiveCameraBasis&                  Camera,
    std::span<const RigidClusterInstance>          Instances,
    const VisibilityBufferTargets&                 VisibilityTargets,
    const DepthPyramidSequence&                    DepthPyramid,
    std::span<const AnalyticResolvedSurfaceSample> PrimarySamples,
    std::span<const Mathematics::LinearRGB>        DirectLitHdrBuffer,
    const MaterialSpace::CompactMaterialRegister&  MaterialRegister,
    Mathematics::LinearRGB                         SkyRadiance)
{
    SpecularReflectionBuffer Out {};
    Out.WidthPx  = VisibilityTargets.WidthPx;
    Out.HeightPx = VisibilityTargets.HeightPx;
    const std::size_t TotalPixels = static_cast<std::size_t>(Out.WidthPx) * Out.HeightPx;
    Out.IndirectSpecularHdr.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });
    Out.RefractedGlassHdr.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });

    const OrthonormalCameraBasis Basis = ComputeCameraBasis(Camera);
    const uint32_t MatCount = MaterialRegister.QuerySlotCount();
    const auto& Mip0 = DepthPyramid.InspectMipLevel(0u);

    const auto ProjectScreenReverseZ = [&](Mathematics::Axis3D WorldPos, float& OutSX, float& OutSY, float& OutRevZ) noexcept -> bool
    {
        const Mathematics::Axis3D Rel = WorldPos - Basis.CameraOrigin;
        const float ViewZ = Mathematics::EvaluateDot(Rel, Basis.ForwardAxis);
        if (ViewZ <= Basis.NearClip) return false;
        const float ViewX = Mathematics::EvaluateDot(Rel, Basis.RightAxis);
        const float ViewY = Mathematics::EvaluateDot(Rel, Basis.UpAxis);
        const float NdcX  = (ViewX / ViewZ) / Basis.TanHalfFovX;
        const float NdcY  = (ViewY / ViewZ) / Basis.TanHalfFovY;
        OutSX   = (NdcX * 0.5f + 0.5f) * static_cast<float>(Out.WidthPx);
        OutSY   = (0.5f - NdcY * 0.5f) * static_cast<float>(Out.HeightPx);
        OutRevZ = EncodeReversedLinearDepth(ViewZ, Basis.NearClip, Basis.FarClip);
        return (OutSX >= 0.0f && OutSX < static_cast<float>(Out.WidthPx) &&
                OutSY >= 0.0f && OutSY < static_cast<float>(Out.HeightPx));
    };

    for (uint32_t PY = 0u; PY < Out.HeightPx; ++PY)
    {
        for (uint32_t PX = 0u; PX < Out.WidthPx; ++PX)
        {
            const std::size_t Off = static_cast<std::size_t>(PY) * Out.WidthPx + PX;
            const auto& Hit       = PrimarySamples[Off];

            if (Hit.ValidHit && Hit.MaterialSlot < MatCount)
            {
                const auto& Mat  = MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);
                const float NdotV= std::clamp(Mathematics::EvaluateDot(Hit.TangentFrame.NormalZ, Hit.WorldViewDirection), 0.01f, 1.0f);
                const Mathematics::Axis3D ReflDir =
                    Hit.TangentFrame.NormalZ * (2.0f * NdotV) - Hit.WorldViewDirection;

                Mathematics::LinearRGB IncomingRad = SkyRadiance * (0.65f + 0.70f * std::clamp(ReflDir.Z * 0.5f + 0.5f, 0.0f, 1.0f));

                // 16-step screen-space depth-buffer march for smooth/glossy surfaces on GTX Tier 1!
                if (Mat.Specular.Roughness < 0.55f && !Mip0.Texels.empty())
                {
                    for (uint32_t Step = 1u; Step <= 16u; ++Step)
                    {
                        const float MarchDist = 0.06f * static_cast<float>(Step) * (1.0f + 0.12f * static_cast<float>(Step));
                        float SX = 0.0f, SY = 0.0f, RayRevZ = 0.0f;
                        if (!ProjectScreenReverseZ(Hit.WorldPosition + ReflDir * MarchDist, SX, SY, RayRevZ))
                        {
                            break;
                        }
                        const uint32_t IX = static_cast<uint32_t>(SX);
                        const uint32_t IY = static_cast<uint32_t>(SY);
                        const float SceneRevZ = Mip0.Texels[static_cast<std::size_t>(IY) * Out.WidthPx + IX];
                        // Ray crossed behind foreground depth within thickness tolerance
                        if (SceneRevZ > RayRevZ && (SceneRevZ - RayRevZ) < 0.0045f)
                        {
                            IncomingRad = DirectLitHdrBuffer[static_cast<std::size_t>(IY) * Out.WidthPx + IX];
                            ++Out.ScreenSpaceHitCount;
                            break;
                        }
                    }
                }

                const auto SpecW = EvaluateSpecularDirectionalWeight(Mat, NdotV);
                const float RoughFade = std::clamp(1.0f - Mat.Specular.Roughness * 0.65f, 0.25f, 1.0f);
                Out.IndirectSpecularHdr[Off] = IncomingRad * RoughFade;
                Out.IndirectSpecularHdr[Off].R *= SpecW.R;
                Out.IndirectSpecularHdr[Off].G *= SpecW.G;
                Out.IndirectSpecularHdr[Off].B *= SpecW.B;
            }

            // GTX 2-Layer Screen-Space Refracted Glass Peel with Abbe Chromatic Screen Offset!
            const uint64_t PeelFront = VisibilityTargets.GlassPeelFront64[Off];
            if (PeelFront != 0ull)
            {
                const auto GlassHit = VisibilityMaterialResolve::ReconstructAnalyticPixelHit(
                    PX, PY, PeelFront, Basis, Basis, Instances);
                if (GlassHit.ValidHit && GlassHit.MaterialSlot < MatCount)
                {
                    const auto& GlassMat = MaterialRegister.InspectAuthoredMaterial(GlassHit.MaterialSlot);
                    const float CosI  = std::clamp(Mathematics::EvaluateDot(GlassHit.TangentFrame.NormalZ, GlassHit.WorldViewDirection), 0.0f, 1.0f);
                    const float FresR = MaterialSpace::Scattering::EvaluateExactDielectricFresnel(CosI, GlassMat.Specular.DielectricIOR);

                    const float OffsetScale = (1.0f - CosI) * 9.0f;
                    const auto SampleRefrCh = [&](float ScaleMult) noexcept -> Mathematics::LinearRGB
                    {
                        const int RX = std::clamp(static_cast<int>(std::round(static_cast<float>(PX) - GlassHit.TangentFrame.NormalZ.X * OffsetScale * ScaleMult)),
                                                  0, static_cast<int>(Out.WidthPx) - 1);
                        const int RY = std::clamp(static_cast<int>(std::round(static_cast<float>(PY) + GlassHit.TangentFrame.NormalZ.Z * OffsetScale * ScaleMult)),
                                                  0, static_cast<int>(Out.HeightPx) - 1);
                        return DirectLitHdrBuffer[static_cast<std::size_t>(RY) * Out.WidthPx + static_cast<uint32_t>(RX)];
                    };
                    const auto RCh = SampleRefrCh(0.88f);
                    const auto GCh = SampleRefrCh(1.00f);
                    const auto BCh = SampleRefrCh(1.14f);

                    Out.RefractedGlassHdr[Off] = {
                        RCh.R * (1.0f - FresR) * GlassMat.Transmission.AttenuationColor.R + SkyRadiance.R * FresR * 1.6f,
                        GCh.G * (1.0f - FresR) * GlassMat.Transmission.AttenuationColor.G + SkyRadiance.G * FresR * 1.6f,
                        BCh.B * (1.0f - FresR) * GlassMat.Transmission.AttenuationColor.B + SkyRadiance.B * FresR * 1.6f,
                    };
                }
            }
        }
    }

    return Out;
}

} // namespace Frontier::GeometricRaster
