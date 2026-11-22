/*============================================================================================================================================
                                                        SPECULARREFLECTIONSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Specular & Abbe Glass Refraction: Full-Roughness Sobol VNDF Multi-Bounce BVH4 Raytrace, Hi-Z SSR, & 3D Beer-Lambert Refraction

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/SpecularReflectionSequence.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometrySpace/Analysis/CurvatureAnalysis.h"
#include "Engine/MaterialSpace/EnergyCompensation/DirectionalAlbedoIntegral.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Frontier::GeometricRaster
{

namespace
{

[[nodiscard]] float DecodeFp16Val(uint16_t HalfWord) noexcept
{
    const uint32_t Sign = static_cast<uint32_t>(HalfWord & 0x8000u) << 16u;
    const uint32_t Exp  = (HalfWord >> 10u) & 0x1Fu;
    const uint32_t Mant = HalfWord & 0x03FFu;
    uint32_t Bits = 0u;
    if (Exp == 0u)
    {
        if (Mant == 0u) Bits = Sign;
        else
        {
            uint32_t M = Mant;
            int32_t  E = -14;
            while ((M & 0x0400u) == 0u) { M <<= 1u; --E; }
            M &= 0x03FFu;
            Bits = Sign | (static_cast<uint32_t>(E + 127) << 23u) | (M << 13u);
        }
    }
    else if (Exp == 31u)
    {
        Bits = Sign | 0x7F800000u | (Mant << 13u);
    }
    else
    {
        Bits = Sign | ((Exp + 112u) << 23u) | (Mant << 13u);
    }
    float Result = 0.0f;
    std::memcpy(&Result, &Bits, sizeof(float));
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

[[nodiscard]] uint32_t ReverseBits32Spec(uint32_t Bits) noexcept
{
    Bits = (Bits << 16u) | (Bits >> 16u);
    Bits = ((Bits & 0x00FF00FFu) << 8u) | ((Bits & 0xFF00FF00u) >> 8u);
    Bits = ((Bits & 0x0F0F0F0Fu) << 4u) | ((Bits & 0xF0F0F0F0u) >> 4u);
    Bits = ((Bits & 0x33333333u) << 2u) | ((Bits & 0xCCCCCCCCu) >> 2u);
    Bits = ((Bits & 0x55555555u) << 1u) | ((Bits & 0xAAAAAAAAu) >> 1u);
    return Bits;
}

[[nodiscard]] uint32_t ScrambleOwenSpec(uint32_t X, uint32_t Seed) noexcept
{
    X += Seed;
    X ^= X * 0x6C50B47Cu;
    X ^= X * 0xB82F1E52u;
    X ^= X * 0xC7AFE638u;
    X ^= X * 0x8D22F6E6u;
    return X;
}

void SampleOwenSobol02Pixel(uint32_t PX, uint32_t PY, uint32_t SampleIdx, float& OutU1, float& OutU2) noexcept
{
    const uint32_t Seed = PX * 1973u + PY * 9277u + 0x68BC21EBu;
    uint32_t SobolX     = ReverseBits32Spec(SampleIdx);
    uint32_t Gen        = 0x80000000u;
    uint32_t SobolY     = 0u;
    for (uint32_t N = SampleIdx; N != 0u; N >>= 1u)
    {
        if ((N & 1u) != 0u) SobolY ^= Gen;
        Gen >>= 1u;
    }
    SobolX = ReverseBits32Spec(ScrambleOwenSpec(ReverseBits32Spec(SobolX), Seed ^ 0x02E5BE93u));
    SobolY = ReverseBits32Spec(ScrambleOwenSpec(ReverseBits32Spec(SobolY), Seed ^ 0x967A889Bu));
    OutU1  = std::clamp(static_cast<float>(SobolX >> 8u) * (1.0f / 16777216.0f), 1.0e-5f, 0.99999f);
    OutU2  = std::clamp(static_cast<float>(SobolY >> 8u) * (1.0f / 16777216.0f), 1.0e-5f, 0.99999f);
}

[[nodiscard]] Mathematics::LinearRGB EvaluateSpecularDirectionalWeight(
    const MaterialSpace::SurfaceMaterial& Mat,
    float                                 NdotV) noexcept
{
    const float EffRough = (Mat.Clearcoat.CoatWeight > 0.25f)
        ? (Mat.Clearcoat.CoatRoughness * Mat.Clearcoat.CoatWeight + Mat.Specular.Roughness * (1.0f - Mat.Clearcoat.CoatWeight) * 0.35f)
        : Mat.Specular.Roughness;
    const auto& AlbedoSheet = MaterialSpace::InspectGlobalDirectionalAlbedoSheet();
    const auto  AlbO        = MaterialSpace::SampleDirectionalAlbedo(AlbedoSheet, NdotV, EffRough);
    const float DielF0      = ((Mat.Specular.DielectricIOR - 1.0f) / (Mat.Specular.DielectricIOR + 1.0f)) *
                              ((Mat.Specular.DielectricIOR - 1.0f) / (Mat.Specular.DielectricIOR + 1.0f)) *
                              Mat.Specular.SpecularWeight;
    const auto  MetalF      = MaterialSpace::Scattering::EvaluateConductorEdgeTintFresnel(
        NdotV, Mat.Diffuse.BaseColor, Mat.Specular.EdgeTintF82);
    const float DielF       = DielF0 + (Mat.Specular.SpecularWeight - DielF0) * std::pow(1.0f - std::clamp(NdotV, 0.0f, 1.0f), 5.0f);
    Mathematics::LinearRGB F {
        DielF * (1.0f - Mat.Specular.Metalness) + MetalF.R * Mat.Specular.Metalness,
        DielF * (1.0f - Mat.Specular.Metalness) + MetalF.G * Mat.Specular.Metalness,
        DielF * (1.0f - Mat.Specular.Metalness) + MetalF.B * Mat.Specular.Metalness,
    };
    if (Mat.Clearcoat.CoatWeight > 0.01f)
    {
        const float CoatF = Mat.Clearcoat.CoatWeight * MaterialSpace::Scattering::EvaluateExactDielectricFresnel(NdotV, 1.5f);
        F.R = CoatF + (1.0f - CoatF) * F.R;
        F.G = CoatF + (1.0f - CoatF) * F.G;
        F.B = CoatF + (1.0f - CoatF) * F.B;
    }

    // Include Turquin multi-scatter compensation from 4 KB DirectionalAlbedoSheet!
    const float MsBoost = (1.0f - AlbO.DirectionalAlbedoE) / std::max(0.05f, AlbO.AverageAlbedoEavg);
    return {
        std::clamp(F.R * (AlbO.DirectionalAlbedoE + F.R * MsBoost * 0.45f), 0.0f, 1.0f),
        std::clamp(F.G * (AlbO.DirectionalAlbedoE + F.G * MsBoost * 0.45f), 0.0f, 1.0f),
        std::clamp(F.B * (AlbO.DirectionalAlbedoE + F.B * MsBoost * 0.45f), 0.0f, 1.0f),
    };
}

[[nodiscard]] Mathematics::LinearRGB ShadeSecondaryRayHit(
    const SpatialAcceleration::WideBoundingTree&    SceneTree,
    const MaterialSpace::CompactMaterialRegister&   MaterialRegister,
    std::span<const MaterialSpace::BounceSlabEntry> BounceSummaries,
    const SolarDirectionalSource&                   Sun,
    std::span<const PolygonalAreaQuadLight>         AreaLights,
    const SpecularSecondaryGiContext*               SecondaryGi,
    Mathematics::Axis3D                             RayOrigin,
    Mathematics::Axis3D                             RayDirection,
    Mathematics::LinearRGB                          SkyRadiance,
    float                                           PrimaryRoughness,
    uint32_t                                        RemainingBounces,
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

    // Roughness-governed emissive radiance clamp prevents 76,000-nit pinhole luminaire fireflies on low-SPP rough VNDF lobes
    // while keeping full razor-sharp specular luminaire reflections on mirrors, glass, chrome, and clearcoats!
    Mathematics::LinearRGB Emis = UnpackBounceEmissive(Slab);
    if (PrimaryRoughness > 0.08f)
    {
        const float T      = std::clamp((PrimaryRoughness - 0.08f) / 0.28f, 0.0f, 1.0f);
        const float Smooth = T * T * (3.0f - 2.0f * T);
        const float MaxEm  = 85.0f * (1.0f - Smooth) + 0.85f * Smooth;
        const float PeakEm = std::max({ Emis.R, Emis.G, Emis.B });
        if (PeakEm > MaxEm)
        {
            Emis = Emis * (MaxEm / PeakEm);
        }
    }
    Mathematics::LinearRGB OutRad = Emis;
    const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis(Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
    if (Mathematics::EvaluateDot(HitNrm, SunDir) > 0.0f)
    {
        SpatialAcceleration::RaySegmentQuery SunShadow {};
        SunShadow.Origin          = HitPos + HitNrm * 2.5e-3f;
        SunShadow.UnitDirection   = SunDir;
        SunShadow.MinimumDistance = 1.0e-3f;
        SunShadow.MaximumDistance = 5000.0f;
        if (!SceneTree.QueryRayOccluded(SunShadow))
        {
            OutRad = OutRad + MaterialSpace::EvaluateBounceSlabRadiance(
                Slab, HitNrm, RayDirection * -1.0f, SunDir, Sun.SolarIrradiance);
        }
    }

    if (!AreaLights.empty() && Slot < MaterialRegister.QuerySlotCount())
    {
        const auto& Mat         = MaterialRegister.InspectAuthoredMaterial(Slot);
        const uint32_t LobeMask = MaterialRegister.InspectCompactEntry(Slot).ActiveLobeMask;
        AnalyticResolvedSurfaceSample SecSample {};
        SecSample.ValidHit             = true;
        SecSample.MaterialSlot         = Slot;
        SecSample.WorldPosition        = HitPos;
        SecSample.WorldViewDirection   = RayDirection * -1.0f;
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
    if (SecondaryGi != nullptr && SecondaryGi->OccluderEnv != nullptr)
    {
        Mathematics::LinearRGB SecGiE {};
        if (SecondaryGi->StokesIrradiance != nullptr)
        {
            SecGiE = SecondaryGi->StokesIrradiance->EvaluateTotalIrradiance(
                HitPos, HitNrm, *SecondaryGi->OccluderEnv, SecondaryGi->StokesSettings);
        }
        else if (SecondaryGi->SurfelLattice != nullptr)
        {
            SecGiE = SecondaryGi->SurfelLattice->EvaluateTotalIrradiance(
                HitPos, HitNrm, *SecondaryGi->OccluderEnv, SecondaryGi->SurfelSettings);
        }
        OutRad.R += (DiffAlb.R / Constants::PiFloat) * SecGiE.R;
        OutRad.G += (DiffAlb.G / Constants::PiFloat) * SecGiE.G;
        OutRad.B += (DiffAlb.B / Constants::PiFloat) * SecGiE.B;
    }
    else
    {
        const float SkyUp  = std::clamp(HitNrm.Z * 0.5f + 0.5f, 0.0f, 1.0f);
        const Mathematics::LinearRGB SkyFill = SkyRadiance * (0.45f + 0.55f * SkyUp) * 0.22f;
        OutRad.R += SkyFill.R * DiffAlb.R;
        OutRad.G += SkyFill.G * DiffAlb.G;
        OutRad.B += SkyFill.B * DiffAlb.B;
    }

    // 2nd specular bounce for conductors, clearcoats, and smooth glossy surfaces!
    if (RemainingBounces > 0u && Slot < MaterialRegister.QuerySlotCount())
    {
        const auto& HitMat = MaterialRegister.InspectAuthoredMaterial(Slot);
        const float HitEffRough = (HitMat.Clearcoat.CoatWeight > 0.25f)
            ? std::min(HitMat.Clearcoat.CoatRoughness, HitMat.Specular.Roughness)
            : HitMat.Specular.Roughness;
        if (HitMat.Specular.Metalness > 0.2f || HitMat.Clearcoat.CoatWeight > 0.25f || HitEffRough < 0.28f)
        {
            const Mathematics::Axis3D ReflDir2 = Mathematics::NormalizeAxis(
                RayDirection - HitNrm * (2.0f * Mathematics::EvaluateDot(RayDirection, HitNrm)));
            if (Mathematics::EvaluateDot(HitNrm, ReflDir2) > 1.0e-3f)
            {
                bool DummyHit2 = false;
                const auto Bounce2Rad = ShadeSecondaryRayHit(
                    SceneTree, MaterialRegister, BounceSummaries, Sun, AreaLights, SecondaryGi,
                    HitPos + HitNrm * 2.5e-3f, ReflDir2, SkyRadiance, HitEffRough, RemainingBounces - 1u, DummyHit2);
                const float Cos2  = std::clamp(-Mathematics::EvaluateDot(HitNrm, RayDirection), 0.01f, 1.0f);
                const auto SpecW2 = EvaluateSpecularDirectionalWeight(HitMat, Cos2);
                OutRad.R += Bounce2Rad.R * SpecW2.R;
                OutRad.G += Bounce2Rad.G * SpecW2.G;
                OutRad.B += Bounce2Rad.B * SpecW2.B;
            }
        }
    }

    return OutRad;
}

// Intersect Level-0 clusters of any arbitrary glass instance (sphere, curved windshield, prism, slab) for exact 3D exit & chord thickness
struct GlassBackExitResult
{
    bool                ValidExit         = false;
    float               ThicknessMeters   = 0.008f;
    Mathematics::Axis3D ExitWorldPosition {};
    Mathematics::Axis3D ExitOutwardNormal {};
};

[[nodiscard]] GlassBackExitResult TraceGlassInstanceExit(
    const RigidClusterInstance& Inst,
    Mathematics::Axis3D         FrontWorldPos,
    Mathematics::Axis3D         FrontOutwardNormal,
    Mathematics::Axis3D         InternalRayDir) noexcept
{
    GlassBackExitResult Out {};
    if (Inst.PyramidAsset == nullptr || Inst.PyramidAsset->Clusters.empty())
    {
        return Out;
    }

    const auto& Pyramid    = *Inst.PyramidAsset;
    const float Scale      = std::max(1.0e-4f, Inst.UniformScale);
    const uint32_t FineCnt = std::min<uint32_t>(Pyramid.FinestClusterCount, static_cast<uint32_t>(Pyramid.Clusters.size()));
    const uint32_t TestCnt = (FineCnt > 0u) ? FineCnt : static_cast<uint32_t>(Pyramid.Clusters.size());
    const auto RayOrigin   = FrontWorldPos + InternalRayDir * 1.5e-3f;

    float BestT = 1.0e30f;
    for (uint32_t CIdx = 0u; CIdx < TestCnt; ++CIdx)
    {
        const auto& Cluster     = Pyramid.Clusters[CIdx];
        const auto  WorldCenter = Inst.WorldTranslation + Cluster.SphereCenter * Scale;
        const float WorldRadius = Cluster.SphereRadiusMeters * Scale + 2.0e-3f;

        const Mathematics::Axis3D OC = RayOrigin - WorldCenter;
        const float ProjB            = Mathematics::EvaluateDot(OC, InternalRayDir);
        const float CVal             = Mathematics::EvaluateLengthSquared(OC) - WorldRadius * WorldRadius;
        if (CVal > 0.0f && ProjB > 0.0f) continue;
        if (ProjB * ProjB - CVal < 0.0f) continue;

        for (uint32_t LTri = 0u; LTri < Cluster.TriangleCount; ++LTri)
        {
            const auto& Tri = Pyramid.Triangles[Cluster.TriangleStartOffset + LTri];
            const Mathematics::Axis3D P0 = Inst.WorldTranslation + Pyramid.Positions[Tri.VertexIndex0] * Scale;
            const Mathematics::Axis3D P1 = Inst.WorldTranslation + Pyramid.Positions[Tri.VertexIndex1] * Scale;
            const Mathematics::Axis3D P2 = Inst.WorldTranslation + Pyramid.Positions[Tri.VertexIndex2] * Scale;
            const Mathematics::Axis3D E1 = P1 - P0;
            const Mathematics::Axis3D E2 = P2 - P0;

            const Mathematics::Axis3D PVec = Mathematics::EvaluateCross(InternalRayDir, E2);
            const float Det                = Mathematics::EvaluateDot(E1, PVec);
            if (std::fabs(Det) <= 1.0e-8f) continue;
            const float InvDet             = 1.0f / Det;
            const Mathematics::Axis3D TVec = RayOrigin - P0;
            const float U                  = Mathematics::EvaluateDot(TVec, PVec) * InvDet;
            if (U < -1.0e-4f || U > 1.0001f) continue;
            const Mathematics::Axis3D QVec = Mathematics::EvaluateCross(TVec, E1);
            const float V                  = Mathematics::EvaluateDot(InternalRayDir, QVec) * InvDet;
            if (V < -1.0e-4f || U + V > 1.0001f) continue;
            const float T = Mathematics::EvaluateDot(E2, QVec) * InvDet;
            if (T <= 3.0e-3f || T >= BestT) continue;

            const float W0 = std::max(0.0f, 1.0f - U - V);
            const float W1 = std::max(0.0f, U);
            const float W2 = std::max(0.0f, V);
            const Mathematics::Axis3D N0 = GeometrySpace::UnpackOctahedralDirection32(Tri.PackedNormal0);
            const Mathematics::Axis3D N1 = GeometrySpace::UnpackOctahedralDirection32(Tri.PackedNormal1);
            const Mathematics::Axis3D N2 = GeometrySpace::UnpackOctahedralDirection32(Tri.PackedNormal2);
            const Mathematics::Axis3D FaceN = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(E1, E2), FrontOutwardNormal);
            const Mathematics::Axis3D SmoothN = Mathematics::NormalizeAxis(N0 * W0 + N1 * W1 + N2 * W2, FaceN);

            if (Mathematics::EvaluateDot(SmoothN, InternalRayDir) <= 1.0e-3f) continue;

            BestT                 = T;
            Out.ValidExit         = true;
            Out.ThicknessMeters   = T + 1.5e-3f;
            Out.ExitWorldPosition = RayOrigin + InternalRayDir * T;
            Out.ExitOutwardNormal = SmoothN;
        }
    }
    return Out;
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
    std::span<const PolygonalAreaQuadLight>         AreaLights,
    const SpecularSecondaryGiContext*               SecondaryGi)
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
            AreaLights,
            SecondaryGi);
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
                const auto& Mat   = MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);
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
    std::span<const PolygonalAreaQuadLight>         AreaLights,
    const SpecularSecondaryGiContext*               SecondaryGi)
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

            // 1. Full-roughness Owen-scrambled Sobol (0,2) Spherical-Cap GGX VNDF multi-bounce specular reflection!
            if (Hit.ValidHit && Hit.MaterialSlot < MatCount)
            {
                const auto& Mat    = MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);
                const auto LocalWo = Hit.TangentFrame.WorldToLocal(Hit.WorldViewDirection);
                if (LocalWo.Z > 1.0e-4f && Mat.Transmission.TransmissionWeight < 0.9f)
                {
                    const float EffRough = (Mat.Clearcoat.CoatWeight > 0.25f)
                        ? (Mat.Clearcoat.CoatRoughness * Mat.Clearcoat.CoatWeight + Mat.Specular.Roughness * (1.0f - Mat.Clearcoat.CoatWeight) * 0.35f)
                        : Mat.Specular.Roughness;

                    Mathematics::LinearRGB IncomingRad {};
                    if (EffRough <= 0.86f)
                    {
                        const uint32_t SampleCount = (EffRough < 0.045f) ? 1u : ((EffRough < 0.24f) ? 3u : 4u);
                        const auto Alpha           = MaterialSpace::Scattering::ComputeAnisotropicAlpha(EffRough, Mat.Specular.Anisotropy);
                        uint32_t ValidSamples      = 0u;
                        for (uint32_t SIdx = 0u; SIdx < SampleCount; ++SIdx)
                        {
                            float U1 = 0.5f, U2 = 0.5f;
                            SampleOwenSobol02Pixel(PX, PY, SIdx, U1, U2);
                            const auto HalfV   = MaterialSpace::Scattering::SampleAnisotropicSphericalCapVndf(LocalWo, Alpha, U1, U2);
                            const auto LocalWi = HalfV * (2.0f * Mathematics::EvaluateDot(LocalWo, HalfV)) - LocalWo;

                            if (LocalWi.Z > 1.0e-4f)
                            {
                                const Mathematics::Axis3D WorldReflDir = Hit.TangentFrame.LocalToWorld(LocalWi);
                                bool HitGeom = false;
                                const uint32_t ExtraBounce = (SIdx == 0u && EffRough < 0.30f) ? 1u : 0u;
                                const auto Rad = ShadeSecondaryRayHit(
                                    SceneTree, MaterialRegister, BounceSummaries, Sun, AreaLights, SecondaryGi,
                                    Hit.WorldPosition + Hit.TangentFrame.NormalZ * 2.5e-3f,
                                    WorldReflDir, SkyRadiance, EffRough, ExtraBounce, HitGeom);
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

            // 2. Arbitrary-Geometry 2-Layer Abbe-Dispersed + Beer-Lambert Dielectric Glass Refraction Raytrace!
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

                    const auto DispIor = MaterialSpace::Scattering::ComputeAbbeDispersedIor(
                        GlassMat.Specular.DielectricIOR, GlassMat.Transmission.AbbeDispersionNumber);
                    const Mathematics::Axis3D SigmaA {
                        -std::log(std::clamp(GlassMat.Transmission.AttenuationColor.R, 0.05f, 1.0f)) / 0.42f,
                        -std::log(std::clamp(GlassMat.Transmission.AttenuationColor.G, 0.05f, 1.0f)) / 0.42f,
                        -std::log(std::clamp(GlassMat.Transmission.AttenuationColor.B, 0.05f, 1.0f)) / 0.42f,
                    };

                    const auto TraceDispersedBand = [&](float BandIor, float SigmaBand) noexcept -> Mathematics::LinearRGB
                    {
                        const float EtaIn  = 1.0f / std::max(1.01f, BandIor);
                        const float Sin2In = EtaIn * EtaIn * std::max(0.0f, 1.0f - CosI * CosI);
                        Mathematics::Axis3D ExitOrig = GlassHit.WorldPosition - Nrm * 3.0e-3f;
                        Mathematics::Axis3D ExitDir  = Inc + Nrm * (2.0f * CosI);
                        float OpticalPath            = 0.008f;

                        if (Sin2In < 1.0f)
                        {
                            const Mathematics::Axis3D InsideDir = Mathematics::NormalizeAxis(
                                Inc * EtaIn + Nrm * (EtaIn * CosI - std::sqrt(1.0f - Sin2In)));

                            GlassBackExitResult BackExit {};
                            if (GlassHit.InstanceOrdinal < Instances.size())
                            {
                                BackExit = TraceGlassInstanceExit(
                                    Instances[GlassHit.InstanceOrdinal], GlassHit.WorldPosition, Nrm, InsideDir);
                            }
                            if (!BackExit.ValidExit && Off < VisibilityTargets.GlassPeelSecond64.size() && VisibilityTargets.GlassPeelSecond64[Off] != 0ull)
                            {
                                const auto PeelBackHit = VisibilityMaterialResolve::ReconstructAnalyticPixelHit(
                                    PX, PY, VisibilityTargets.GlassPeelSecond64[Off], Basis, Basis, Instances);
                                if (PeelBackHit.ValidHit)
                                {
                                    BackExit.ValidExit         = true;
                                    BackExit.ThicknessMeters   = std::max(0.008f, Mathematics::EvaluateLength(PeelBackHit.WorldPosition - GlassHit.WorldPosition));
                                    BackExit.ExitWorldPosition = GlassHit.WorldPosition + InsideDir * BackExit.ThicknessMeters;
                                    BackExit.ExitOutwardNormal = (Mathematics::EvaluateDot(PeelBackHit.TangentFrame.NormalZ, InsideDir) > 0.0f)
                                        ? PeelBackHit.TangentFrame.NormalZ
                                        : (PeelBackHit.TangentFrame.NormalZ * -1.0f);
                                }
                            }
                            if (!BackExit.ValidExit)
                            {
                                // Single-walled automotive / architectural pane: 8 mm laminated glass sheet
                                BackExit.ThicknessMeters   = 0.008f;
                                BackExit.ExitWorldPosition = GlassHit.WorldPosition + InsideDir * BackExit.ThicknessMeters;
                                BackExit.ExitOutwardNormal = Nrm;
                            }

                            OpticalPath         = BackExit.ThicknessMeters;
                            const auto  OutNrm  = BackExit.ExitOutwardNormal;
                            const float CosOut  = std::clamp(Mathematics::EvaluateDot(InsideDir, OutNrm), 0.0f, 1.0f);
                            const float EtaOut  = std::max(1.01f, BandIor);
                            const float Sin2Out = EtaOut * EtaOut * std::max(0.0f, 1.0f - CosOut * CosOut);
                            if (Sin2Out < 1.0f)
                            {
                                ExitDir  = Mathematics::NormalizeAxis(
                                    InsideDir * EtaOut + OutNrm * (std::sqrt(1.0f - Sin2Out) - EtaOut * CosOut));
                                ExitOrig = BackExit.ExitWorldPosition + OutNrm * 3.0e-3f;
                            }
                            else
                            {
                                // Total Internal Reflection inside solid glass shell: bounce once inside before exiting!
                                const auto TirBounceDir = Mathematics::NormalizeAxis(
                                    InsideDir - OutNrm * (2.0f * Mathematics::EvaluateDot(InsideDir, OutNrm)));
                                ExitDir  = TirBounceDir;
                                ExitOrig = BackExit.ExitWorldPosition + OutNrm * 3.0e-3f;
                                if (GlassHit.InstanceOrdinal < Instances.size())
                                {
                                    const auto BounceExit = TraceGlassInstanceExit(
                                        Instances[GlassHit.InstanceOrdinal], BackExit.ExitWorldPosition, OutNrm, TirBounceDir);
                                    if (BounceExit.ValidExit)
                                    {
                                        OpticalPath += BounceExit.ThicknessMeters;
                                        const float Cos2  = std::clamp(Mathematics::EvaluateDot(TirBounceDir, BounceExit.ExitOutwardNormal), 0.0f, 1.0f);
                                        const float Sin22 = EtaOut * EtaOut * std::max(0.0f, 1.0f - Cos2 * Cos2);
                                        if (Sin22 < 1.0f)
                                        {
                                            ExitDir = Mathematics::NormalizeAxis(
                                                TirBounceDir * EtaOut + BounceExit.ExitOutwardNormal * (std::sqrt(1.0f - Sin22) - EtaOut * Cos2));
                                        }
                                        ExitOrig = BounceExit.ExitWorldPosition + BounceExit.ExitOutwardNormal * 3.0e-3f;
                                    }
                                }
                            }
                        }
                        bool Dummy = false;
                        const auto BandRad = ShadeSecondaryRayHit(
                            SceneTree, MaterialRegister, BounceSummaries, Sun, AreaLights, SecondaryGi,
                            ExitOrig, ExitDir, SkyRadiance, GlassMat.Specular.Roughness, 0u, Dummy);
                        return BandRad * std::exp(-SigmaBand * OpticalPath);
                    };

                    const auto RadR = TraceDispersedBand(DispIor.X, SigmaA.X);
                    const auto RadG = TraceDispersedBand(DispIor.Y, SigmaA.Y);
                    const auto RadB = TraceDispersedBand(DispIor.Z, SigmaA.Z);

                    bool DummyRefl = false;
                    const Mathematics::Axis3D ReflDir = Inc + Nrm * (2.0f * CosI);
                    const auto ReflRad = ShadeSecondaryRayHit(
                        SceneTree, MaterialRegister, BounceSummaries, Sun, AreaLights, SecondaryGi,
                        GlassHit.WorldPosition + Nrm * 3.0e-3f, ReflDir, SkyRadiance, GlassMat.Specular.Roughness, 1u, DummyRefl);

                    const float TransW = (1.0f - FresR) * GlassMat.Transmission.TransmissionWeight;
                    Out.RefractedGlassHdr[Off] = {
                        RadR.R * TransW + ReflRad.R * FresR,
                        RadG.G * TransW + ReflRad.G * FresR,
                        RadB.B * TransW + ReflRad.B * FresR,
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
                const auto& Mat   = MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);
                const auto LocalV = Hit.TangentFrame.WorldToLocal(Hit.WorldViewDirection);
                const float NdotV = std::clamp(LocalV.Z, 0.01f, 1.0f);
                const float EffR  = (Mat.Clearcoat.CoatWeight > 0.25f)
                    ? std::min(Mat.Clearcoat.CoatRoughness, Mat.Specular.Roughness)
                    : Mat.Specular.Roughness;

                float U1 = 0.5f, U2 = 0.5f;
                SampleOwenSobol02Pixel(PX, PY, 0u, U1, U2);
                const auto Alpha   = MaterialSpace::Scattering::ComputeAnisotropicAlpha(EffR, Mat.Specular.Anisotropy);
                const auto HalfV   = MaterialSpace::Scattering::SampleAnisotropicSphericalCapVndf(LocalV, Alpha, U1, U2);
                const auto LocalWi = HalfV * (2.0f * Mathematics::EvaluateDot(LocalV, HalfV)) - LocalV;
                const Mathematics::Axis3D ReflDir = (LocalWi.Z > 1.0e-3f)
                    ? Hit.TangentFrame.LocalToWorld(LocalWi)
                    : (Hit.TangentFrame.NormalZ * (2.0f * NdotV) - Hit.WorldViewDirection);

                Mathematics::LinearRGB IncomingRad = SkyRadiance * (0.65f + 0.70f * std::clamp(ReflDir.Z * 0.5f + 0.5f, 0.0f, 1.0f));

                if (EffR <= 0.80f && !Mip0.Texels.empty())
                {
                    for (uint32_t Step = 1u; Step <= 24u; ++Step)
                    {
                        const float MarchDist = 0.05f * static_cast<float>(Step) * (1.0f + 0.12f * static_cast<float>(Step));
                        float SX = 0.0f, SY = 0.0f, RayRevZ = 0.0f;
                        if (!ProjectScreenReverseZ(Hit.WorldPosition + ReflDir * MarchDist, SX, SY, RayRevZ))
                        {
                            break;
                        }
                        const uint32_t IX    = static_cast<uint32_t>(SX);
                        const uint32_t IY    = static_cast<uint32_t>(SY);
                        const std::size_t MO = static_cast<std::size_t>(IY) * Mip0.Width + IX;
                        const float SceneRev = Mip0.Texels[MO];
                        if (SceneRev > 1.0e-5f && RayRevZ <= SceneRev && (SceneRev - RayRevZ) < 0.04f)
                        {
                            const std::size_t LitOff = static_cast<std::size_t>(IY) * Out.WidthPx + IX;
                            if (LitOff < DirectLitHdrBuffer.size())
                            {
                                IncomingRad = DirectLitHdrBuffer[LitOff];
                                ++Out.ScreenSpaceHitCount;
                            }
                            break;
                        }
                    }
                }

                const auto SpecW       = EvaluateSpecularDirectionalWeight(Mat, NdotV);
                const float RoughDamp  = 1.0f / (1.0f + 1.5f * EffR * EffR);
                Out.IndirectSpecularHdr[Off] = {
                    IncomingRad.R * SpecW.R * RoughDamp,
                    IncomingRad.G * SpecW.G * RoughDamp,
                    IncomingRad.B * SpecW.B * RoughDamp,
                };
            }

            // 3D-Projected 2-Layer PeelFront64 + PeelSecond64 Abbe Refraction + Beer-Lambert Volumetric Absorption on GTX!
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
                    const float CosI  = std::clamp(-Mathematics::EvaluateDot(Inc, Nrm), 0.0f, 1.0f);
                    const float FresR = MaterialSpace::Scattering::EvaluateExactDielectricFresnel(CosI, GlassMat.Specular.DielectricIOR);

                    float SlabThickness = 0.012f;
                    Mathematics::Axis3D ExitOutwardN = Nrm;
                    if (Off < VisibilityTargets.GlassPeelSecond64.size() && VisibilityTargets.GlassPeelSecond64[Off] != 0ull)
                    {
                        const auto GlassBack = VisibilityMaterialResolve::ReconstructAnalyticPixelHit(
                            PX, PY, VisibilityTargets.GlassPeelSecond64[Off], Basis, Basis, Instances);
                        if (GlassBack.ValidHit)
                        {
                            SlabThickness = std::max(0.008f, Mathematics::EvaluateLength(GlassBack.WorldPosition - GlassHit.WorldPosition));
                            ExitOutwardN  = (Mathematics::EvaluateDot(GlassBack.TangentFrame.NormalZ, Inc) > 0.0f)
                                ? GlassBack.TangentFrame.NormalZ
                                : (GlassBack.TangentFrame.NormalZ * -1.0f);
                        }
                    }

                    const auto DispIor = MaterialSpace::Scattering::ComputeAbbeDispersedIor(
                        GlassMat.Specular.DielectricIOR, GlassMat.Transmission.AbbeDispersionNumber);
                    const Mathematics::Axis3D SigmaA {
                        -std::log(std::clamp(GlassMat.Transmission.AttenuationColor.R, 0.05f, 1.0f)) / 0.42f,
                        -std::log(std::clamp(GlassMat.Transmission.AttenuationColor.G, 0.05f, 1.0f)) / 0.42f,
                        -std::log(std::clamp(GlassMat.Transmission.AttenuationColor.B, 0.05f, 1.0f)) / 0.42f,
                    };

                    const auto SampleGtxBand = [&](float BandIor, float SigmaBand) noexcept -> Mathematics::LinearRGB
                    {
                        const float EtaIn   = 1.0f / std::max(1.01f, BandIor);
                        const float Sin2In  = EtaIn * EtaIn * std::max(0.0f, 1.0f - CosI * CosI);
                        const auto InsideD  = (Sin2In < 1.0f)
                            ? Mathematics::NormalizeAxis(Inc * EtaIn + Nrm * (EtaIn * CosI - std::sqrt(1.0f - Sin2In)))
                            : Mathematics::NormalizeAxis(Inc + Nrm * (2.0f * CosI));
                        const auto ExitPos  = GlassHit.WorldPosition + InsideD * SlabThickness;
                        const float CosOut  = std::clamp(Mathematics::EvaluateDot(InsideD, ExitOutwardN), 0.0f, 1.0f);
                        const float EtaOut  = std::max(1.01f, BandIor);
                        const float Sin2Out = EtaOut * EtaOut * std::max(0.0f, 1.0f - CosOut * CosOut);
                        const auto ExitDir  = (Sin2Out < 1.0f)
                            ? Mathematics::NormalizeAxis(InsideD * EtaOut + ExitOutwardN * (std::sqrt(1.0f - Sin2Out) - EtaOut * CosOut))
                            : Mathematics::NormalizeAxis(InsideD - ExitOutwardN * (2.0f * Mathematics::EvaluateDot(InsideD, ExitOutwardN)));

                        float SX = static_cast<float>(PX), SY = static_cast<float>(PY), RevZ = 0.0f;
                        Mathematics::LinearRGB Bg = SkyRadiance;
                        if (ProjectScreenReverseZ(ExitPos + ExitDir * 1.35f, SX, SY, RevZ))
                        {
                            const std::size_t BOff = static_cast<std::size_t>(static_cast<uint32_t>(SY)) * Out.WidthPx + static_cast<uint32_t>(SX);
                            if (BOff < DirectLitHdrBuffer.size())
                            {
                                Bg = DirectLitHdrBuffer[BOff];
                            }
                        }
                        return Bg * std::exp(-SigmaBand * SlabThickness);
                    };

                    const auto BandR  = SampleGtxBand(DispIor.X, SigmaA.X);
                    const auto BandG  = SampleGtxBand(DispIor.Y, SigmaA.Y);
                    const auto BandB  = SampleGtxBand(DispIor.Z, SigmaA.Z);
                    const auto ReflD  = Inc + Nrm * (2.0f * CosI);
                    const auto EnvRef = SkyRadiance * (0.65f + 0.70f * std::clamp(ReflD.Z * 0.5f + 0.5f, 0.0f, 1.0f));
                    const float Trans = (1.0f - FresR) * GlassMat.Transmission.TransmissionWeight;
                    Out.RefractedGlassHdr[Off] = {
                        BandR.R * Trans + EnvRef.R * FresR,
                        BandG.G * Trans + EnvRef.G * FresR,
                        BandB.B * Trans + EnvRef.B * FresR,
                    };
                }
            }
        }
    }

    return Out;
}

} // namespace Frontier::GeometricRaster
