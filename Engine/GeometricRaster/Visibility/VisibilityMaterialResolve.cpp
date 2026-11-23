/*============================================================================================================================================
                                                        VISIBILITYMATERIALRESOLVE.CPP
============================================================================================================================================*/
// 🧩 Zero-G-Buffer Analytic Ray-Differential Barycentric Resolver, UV Footprint Jacobians, Motion Vectors, & 10-Lobe OpenPBR Shade

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Visibility/VisibilityMaterialResolve.h"
#include "Engine/GeometrySpace/Analysis/CurvatureAnalysis.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::GeometricRaster
{

namespace
{

[[nodiscard]] Mathematics::Axis3D ConstructPrimaryRayDirection(
    const OrthonormalCameraBasis& Basis,
    float                         SubpixelX,
    float                         SubpixelY) noexcept
{
    const float UnjitteredX = SubpixelX - Basis.JitterPx.X;
    const float UnjitteredY = SubpixelY - Basis.JitterPx.Y;
    const float NdcX        = (UnjitteredX / static_cast<float>(Basis.WidthPx)) * 2.0f - 1.0f;
    const float NdcY        = 1.0f - (UnjitteredY / static_cast<float>(Basis.HeightPx)) * 2.0f;

    return Mathematics::NormalizeAxis(
        Basis.ForwardAxis +
        Basis.RightAxis * (NdcX * Basis.TanHalfFovX) +
        Basis.UpAxis * (NdcY * Basis.TanHalfFovY),
        Basis.ForwardAxis);
}

[[nodiscard]] Mathematics::Axis2D ProjectPointToScreenCoords(
    const OrthonormalCameraBasis& Basis,
    Mathematics::Axis3D           WorldPos) noexcept
{
    const Mathematics::Axis3D Rel = WorldPos - Basis.CameraOrigin;
    const float ViewX = Mathematics::EvaluateDot(Rel, Basis.RightAxis);
    const float ViewY = Mathematics::EvaluateDot(Rel, Basis.UpAxis);
    const float ViewZ = std::max(1.0e-4f, Mathematics::EvaluateDot(Rel, Basis.ForwardAxis));

    const float NdcX = (ViewX / ViewZ) / std::max(1.0e-5f, Basis.TanHalfFovX);
    const float NdcY = (ViewY / ViewZ) / std::max(1.0e-5f, Basis.TanHalfFovY);

    return {
        (NdcX * 0.5f + 0.5f) * static_cast<float>(Basis.WidthPx),
        (0.5f - NdcY * 0.5f) * static_cast<float>(Basis.HeightPx),
    };
}

// 📝 Intersect primary ray Origin + t * RayDir with triangle (P0, P1, P2) plane to obtain exact perspective-correct (L0, L1, L2)
[[nodiscard]] Mathematics::Axis3D EvaluateRayTrianglePlaneBarycentrics(
    Mathematics::Axis3D RayOrigin,
    Mathematics::Axis3D RayDir,
    Mathematics::Axis3D P0,
    Mathematics::Axis3D Edge1,
    Mathematics::Axis3D Edge2) noexcept
{
    const Mathematics::Axis3D PVec = Mathematics::EvaluateCross(RayDir, Edge2);
    const float Det = Mathematics::EvaluateDot(Edge1, PVec);
    if (std::fabs(Det) <= 1.0e-10f)
    {
        return { 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f };
    }
    const float InvDet = 1.0f / Det;
    const Mathematics::Axis3D TVec = RayOrigin - P0;
    const float BaryU  = Mathematics::EvaluateDot(TVec, PVec) * InvDet;
    const Mathematics::Axis3D QVec = Mathematics::EvaluateCross(TVec, Edge1);
    const float BaryV  = Mathematics::EvaluateDot(RayDir, QVec) * InvDet;
    const float BaryW  = 1.0f - BaryU - BaryV;
    return { BaryW, BaryU, BaryV };
}

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

[[nodiscard]] Mathematics::LinearRGB EvaluateSkyBackdropRadiance(Mathematics::Axis3D RayDir) noexcept
{
    const float UpFactor = std::clamp(RayDir.Z * 0.5f + 0.5f, 0.0f, 1.0f);
    const float HorizonGlow = std::exp(-std::fabs(RayDir.Z) * 6.0f);
    return {
        0.045f + 0.075f * UpFactor + 0.09f * HorizonGlow,
        0.060f + 0.125f * UpFactor + 0.10f * HorizonGlow,
        0.095f + 0.215f * UpFactor + 0.12f * HorizonGlow,
    };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              VISIBILITY MATERIAL RESOLVE
//------------------------------------------------------------------------------------------------------------------------

AnalyticResolvedSurfaceSample VisibilityMaterialResolve::ReconstructAnalyticPixelHit(
    uint32_t                              PixelX,
    uint32_t                              PixelY,
    uint64_t                              PackedVisibility64,
    const OrthonormalCameraBasis&         CurrentBasis,
    const OrthonormalCameraBasis&         PreviousBasis,
    std::span<const RigidClusterInstance> Instances) noexcept
{
    AnalyticResolvedSurfaceSample Sample {};
    const UnpackedVisibilityHit Hit = UnpackVisibilityPayload64(PackedVisibility64);
    if (!Hit.ValidSurfaceHit || Hit.InstanceOrdinal >= Instances.size())
    {
        return Sample;
    }

    const RigidClusterInstance& Inst = Instances[Hit.InstanceOrdinal];
    if (Inst.PyramidAsset == nullptr || Hit.ClusterOrdinal >= Inst.PyramidAsset->Clusters.size())
    {
        return Sample;
    }

    const auto& Pyramid = *Inst.PyramidAsset;
    const auto& Cluster = Pyramid.Clusters[Hit.ClusterOrdinal];
    if (Hit.LocalTriangleOrdinal >= Cluster.TriangleCount)
    {
        return Sample;
    }

    const auto& Tri   = Pyramid.Triangles[Cluster.TriangleStartOffset + Hit.LocalTriangleOrdinal];
    const float Scale = std::max(1.0e-4f, Inst.UniformScale);

    const Mathematics::Axis3D LocalP0 = Pyramid.Positions[Tri.VertexIndex0];
    const Mathematics::Axis3D LocalP1 = Pyramid.Positions[Tri.VertexIndex1];
    const Mathematics::Axis3D LocalP2 = Pyramid.Positions[Tri.VertexIndex2];

    const Mathematics::Axis3D P0    = Inst.WorldTranslation + LocalP0 * Scale;
    const Mathematics::Axis3D P1    = Inst.WorldTranslation + LocalP1 * Scale;
    const Mathematics::Axis3D P2    = Inst.WorldTranslation + LocalP2 * Scale;
    const Mathematics::Axis3D Edge1 = P1 - P0;
    const Mathematics::Axis3D Edge2 = P2 - P0;

    const float CenterX = static_cast<float>(PixelX) + 0.5f;
    const float CenterY = static_cast<float>(PixelY) + 0.5f;

    // 1. Primary ray + analytic screen-space differential rays (+1 px right, +1 px down)
    const Mathematics::Axis3D RayC = ConstructPrimaryRayDirection(CurrentBasis, CenterX, CenterY);
    const Mathematics::Axis3D RayX = ConstructPrimaryRayDirection(CurrentBasis, CenterX + 1.0f, CenterY);
    const Mathematics::Axis3D RayY = ConstructPrimaryRayDirection(CurrentBasis, CenterX, CenterY + 1.0f);

    const Mathematics::Axis3D BaryC = EvaluateRayTrianglePlaneBarycentrics(CurrentBasis.CameraOrigin, RayC, P0, Edge1, Edge2);
    const Mathematics::Axis3D BaryX = EvaluateRayTrianglePlaneBarycentrics(CurrentBasis.CameraOrigin, RayX, P0, Edge1, Edge2);
    const Mathematics::Axis3D BaryY = EvaluateRayTrianglePlaneBarycentrics(CurrentBasis.CameraOrigin, RayY, P0, Edge1, Edge2);

    // Clamp primary interpolation weights gently against numerical grazing edge roundoff while keeping exact un-clamped derivatives!
    const float W0  = std::max(0.0f, BaryC.X);
    const float W1  = std::max(0.0f, BaryC.Y);
    const float W2  = std::max(0.0f, BaryC.Z);
    const float Sum = std::max(1.0e-6f, W0 + W1 + W2);
    const Mathematics::Axis3D SafeBary { W0 / Sum, W1 / Sum, W2 / Sum };

    Sample.ValidHit           = true;
    Sample.MaterialSlot       = (Tri.MaterialOrdinal != 0u) ? Tri.MaterialOrdinal : Inst.MaterialSlot;
    Sample.InstanceOrdinal    = Hit.InstanceOrdinal;
    Sample.ClusterOrdinal     = Hit.ClusterOrdinal;
    Sample.TriangleOrdinal    = Hit.LocalTriangleOrdinal;
    Sample.BarycentricWeights = SafeBary;
    Sample.WorldPosition      = P0 * SafeBary.X + P1 * SafeBary.Y + P2 * SafeBary.Z;
    Sample.WorldViewDirection = RayC * -1.0f;

    // 2. Analytic UV coordinates and exact ray-differential screen-space Jacobian (dUV/dx, dUV/dy) — zero quad helper artifacts!
    const auto EvalUvAtBary = [&](Mathematics::Axis3D B) noexcept -> Mathematics::UVAxes
    {
        return {
            B.X * Tri.CornerUV0.U + B.Y * Tri.CornerUV1.U + B.Z * Tri.CornerUV2.U,
            B.X * Tri.CornerUV0.V + B.Y * Tri.CornerUV1.V + B.Z * Tri.CornerUV2.V,
        };
    };
    const Mathematics::UVAxes UvC = EvalUvAtBary(SafeBary);
    const Mathematics::UVAxes UvX = EvalUvAtBary(BaryX);
    const Mathematics::UVAxes UvY = EvalUvAtBary(BaryY);

    Sample.Footprint.SurfaceUV      = UvC;
    Sample.Footprint.DerivativeDuDx = { UvX.U - EvalUvAtBary(BaryC).U, UvX.V - EvalUvAtBary(BaryC).V };
    Sample.Footprint.DerivativeDuDy = { UvY.U - EvalUvAtBary(BaryC).U, UvY.V - EvalUvAtBary(BaryC).V };

    // 3. Interpolate Oct32 corner normals & Mikk/Duff tangent frame
    const Mathematics::Axis3D N0 = GeometrySpace::UnpackOctahedralDirection32(Tri.PackedNormal0);
    const Mathematics::Axis3D N1 = GeometrySpace::UnpackOctahedralDirection32(Tri.PackedNormal1);
    const Mathematics::Axis3D N2 = GeometrySpace::UnpackOctahedralDirection32(Tri.PackedNormal2);
    Mathematics::Axis3D InterpNormal = Mathematics::NormalizeAxis(
        N0 * SafeBary.X + N1 * SafeBary.Y + N2 * SafeBary.Z,
        Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Edge1, Edge2), { 0.0f, 0.0f, 1.0f }));
    if (Mathematics::EvaluateDot(InterpNormal, Sample.WorldViewDirection) < 0.0f)
    {
        InterpNormal = InterpNormal * -1.0f;
    }

    Mathematics::Axis3D RawTangent = GeometrySpace::UnpackOctahedralDirection32(Tri.PackedTangent0);
    // Gram-Schmidt re-orthogonalize tangent against interpolated smooth normal
    RawTangent = RawTangent - InterpNormal * Mathematics::EvaluateDot(RawTangent, InterpNormal);
    if (Mathematics::EvaluateLengthSquared(RawTangent) <= 1.0e-6f)
    {
        Mathematics::Axis3D DummyB {};
        GeometrySpace::ConstructDuffOrthonormalBasis(InterpNormal, RawTangent, DummyB);
    }
    else
    {
        RawTangent = Mathematics::NormalizeAxis(RawTangent);
    }
    const float HandSign = (Tri.TangentSignScaled < 0) ? -1.0f : 1.0f;
    const Mathematics::Axis3D Bitangent = Mathematics::NormalizeAxis(
        Mathematics::EvaluateCross(InterpNormal, RawTangent) * HandSign);

    Sample.TangentFrame.NormalZ    = InterpNormal;
    Sample.TangentFrame.TangentX   = RawTangent;
    Sample.TangentFrame.BitangentY = Bitangent;

    // 4. Exact Sub-Pixel Differential Motion Vector (Unjittered Current Screen Pos - Previous Frame Screen Pos)
    const Mathematics::Axis3D LocalHitPos  = LocalP0 * SafeBary.X + LocalP1 * SafeBary.Y + LocalP2 * SafeBary.Z;
    const Mathematics::Axis3D PrevWorldPos = Inst.PreviousTranslation + LocalHitPos * Scale;
    const Mathematics::Axis2D CurUnjitPx   = ProjectPointToScreenCoords(CurrentBasis, Sample.WorldPosition);
    const Mathematics::Axis2D PrevUnjitPx  = ProjectPointToScreenCoords(PreviousBasis, PrevWorldPos);
    Sample.MotionVectorPx = {
        CurUnjitPx.X - PrevUnjitPx.X,
        CurUnjitPx.Y - PrevUnjitPx.Y,
    };

    return Sample;
}

VisibilityResolveBuffer VisibilityMaterialResolve::ResolveVisibilityFrame(
    const PerspectiveCameraBasis&                 CurrentCamera,
    const PerspectiveCameraBasis&                 PreviousCamera,
    std::span<const RigidClusterInstance>         Instances,
    const VisibilityBufferTargets&                VisibilityTargets,
    const MaterialSpace::CompactMaterialRegister& MaterialRegister)
{
    VisibilityResolveBuffer Frame {};
    Frame.WidthPx  = VisibilityTargets.WidthPx;
    Frame.HeightPx = VisibilityTargets.HeightPx;
    const std::size_t TotalPixels = static_cast<std::size_t>(Frame.WidthPx) * Frame.HeightPx;

    Frame.LinearHdrRadiance.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });
    Frame.ToneMappedSrgb8.assign(TotalPixels * 4u, 255u);
    Frame.MotionVectorsPx.assign(TotalPixels, { 0.0f, 0.0f });
    Frame.FootprintAreaDetUv.assign(TotalPixels, 0.0f);

    const OrthonormalCameraBasis CurBasis  = ComputeCameraBasis(CurrentCamera);
    const OrthonormalCameraBasis PrevBasis = ComputeCameraBasis(PreviousCamera);

    // Pre-unpack active materials once per resolve pass (mirrors GPU wave/tile register cache)
    const uint32_t MatCount = MaterialRegister.QuerySlotCount();
    std::vector<MaterialSpace::SurfaceMaterial> UnpackedMaterials(MatCount);
    std::vector<uint32_t>                       MaterialLobeMasks(MatCount, 0u);
    for (uint32_t Slot = 0u; Slot < MatCount; ++Slot)
    {
        const auto& Entry        = MaterialRegister.InspectCompactEntry(static_cast<uint16_t>(Slot));
        UnpackedMaterials[Slot]  = MaterialSpace::UnpackCompactMaterialEntry(Entry);
        MaterialLobeMasks[Slot]  = Entry.ActiveLobeMask;
    }

    // Calibrated 3-light studio rig (Key Sun, Sky Fill, Warm Rim) + Ambient Hemisphere
    struct DirectionalRigLight
    {
        Mathematics::Axis3D    DirectionToLight;
        Mathematics::LinearRGB Irradiance;
    };
    const std::array<DirectionalRigLight, 3u> RigLights {{
        { Mathematics::NormalizeAxis({ -0.48f, -0.58f,  0.66f }), { 3.40f, 3.25f, 3.05f } },
        { Mathematics::NormalizeAxis({  0.68f, -0.36f,  0.64f }), { 0.95f, 1.15f, 1.45f } },
        { Mathematics::NormalizeAxis({  0.15f,  0.84f,  0.52f }), { 1.35f, 1.18f, 1.02f } },
    }};

    const auto ShadeResolvedSample = [&](const AnalyticResolvedSurfaceSample& HitSample, uint32_t WaveCombinedLobeMask) noexcept -> Mathematics::LinearRGB
    {
        if (HitSample.MaterialSlot >= MatCount)
        {
            return { 0.5f, 0.1f, 0.5f };
        }
        const auto& Mat = UnpackedMaterials[HitSample.MaterialSlot];
        const uint32_t EffectiveMask = MaterialLobeMasks[HitSample.MaterialSlot] & WaveCombinedLobeMask;
        const Mathematics::Axis3D LocalWo = HitSample.TangentFrame.WorldToLocal(HitSample.WorldViewDirection);

        Mathematics::LinearRGB Radiance = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);
        for (const auto& Light : RigLights)
        {
            const float NdotL = Mathematics::EvaluateDot(HitSample.TangentFrame.NormalZ, Light.DirectionToLight);
            if (NdotL > 0.0f)
            {
                const Mathematics::Axis3D LocalWi = HitSample.TangentFrame.WorldToLocal(Light.DirectionToLight);
                const auto Bsdf = MaterialSpace::Scattering::EvaluateOpenPbrLocalBsdf(
                    Mat, EffectiveMask, LocalWo, LocalWi, HitSample.Footprint);
                Radiance.R += Bsdf.R * Light.Irradiance.R * NdotL;
                Radiance.G += Bsdf.G * Light.Irradiance.G * NdotL;
                Radiance.B += Bsdf.B * Light.Irradiance.B * NdotL;
            }
        }

        // Studio softbox environment reflection & diffuse hemisphere fill (handles both conductors & dielectrics!)
        const Mathematics::Axis3D ReflDir =
            HitSample.TangentFrame.NormalZ *
                (2.0f * Mathematics::EvaluateDot(HitSample.TangentFrame.NormalZ, HitSample.WorldViewDirection)) -
            HitSample.WorldViewDirection;
        const auto SkyRefl = EvaluateSkyBackdropRadiance(ReflDir);
        const auto SkyDiff = EvaluateSkyBackdropRadiance(HitSample.TangentFrame.NormalZ);
        const float CosV   = std::clamp(LocalWo.Z, 0.0f, 1.0f);
        const auto MetalEnvF = MaterialSpace::Scattering::EvaluateConductorEdgeTintFresnel(
            CosV, Mat.Diffuse.BaseColor, Mat.Specular.EdgeTintF82);
        const float DielEnvF = MaterialSpace::Scattering::EvaluateExactDielectricFresnel(CosV, Mat.Specular.DielectricIOR);
        const Mathematics::LinearRGB EnvSpecTint {
            DielEnvF * (1.0f - Mat.Specular.Metalness) + MetalEnvF.R * Mat.Specular.Metalness,
            DielEnvF * (1.0f - Mat.Specular.Metalness) + MetalEnvF.G * Mat.Specular.Metalness,
            DielEnvF * (1.0f - Mat.Specular.Metalness) + MetalEnvF.B * Mat.Specular.Metalness,
        };
        const float SoftboxLobe = 1.6f + 1.8f * std::clamp(ReflDir.Z * 0.5f + 0.5f, 0.0f, 1.0f);
        Radiance.R += SkyDiff.R * Mat.Diffuse.BaseColor.R * (1.0f - Mat.Specular.Metalness) * 0.65f + EnvSpecTint.R * SkyRefl.R * SoftboxLobe;
        Radiance.G += SkyDiff.G * Mat.Diffuse.BaseColor.G * (1.0f - Mat.Specular.Metalness) * 0.65f + EnvSpecTint.G * SkyRefl.G * SoftboxLobe;
        Radiance.B += SkyDiff.B * Mat.Diffuse.BaseColor.B * (1.0f - Mat.Specular.Metalness) * 0.65f + EnvSpecTint.B * SkyRefl.B * SoftboxLobe;
        return Radiance;
    };

    // Process in 8x8 tiles (64 lanes = 1 GPU wavefront) to compute WaveActiveBitOr(ActiveLobeMask) scalar branch telemetry!
    for (uint32_t TileY = 0u; TileY < Frame.HeightPx; TileY += 8u)
    {
        for (uint32_t TileX = 0u; TileX < Frame.WidthPx; TileX += 8u)
        {
            uint32_t WaveCombinedMask = 0u;
            bool     AnyOpaqueHit     = false;

            const uint32_t EndY = std::min(Frame.HeightPx, TileY + 8u);
            const uint32_t EndX = std::min(Frame.WidthPx, TileX + 8u);

            // First pass over 8x8 wavefront: reconstruct primary hits & reduce WaveActiveBitOr(LobeMask)
            std::array<AnalyticResolvedSurfaceSample, 64u> TileSamples {};
            for (uint32_t PY = TileY; PY < EndY; ++PY)
            {
                for (uint32_t PX = TileX; PX < EndX; ++PX)
                {
                    const uint32_t LocalIdx   = (PY - TileY) * 8u + (PX - TileX);
                    const std::size_t PixOff  = static_cast<std::size_t>(PY) * Frame.WidthPx + PX;
                    const auto Sample         = ReconstructAnalyticPixelHit(
                        PX, PY, VisibilityTargets.OpaquePrimary64[PixOff], CurBasis, PrevBasis, Instances);
                    TileSamples[LocalIdx] = Sample;
                    if (Sample.ValidHit && Sample.MaterialSlot < MatCount)
                    {
                        AnyOpaqueHit = true;
                        WaveCombinedMask |= MaterialLobeMasks[Sample.MaterialSlot];
                    }
                }
            }

            if (AnyOpaqueHit)
            {
                ++Frame.ActiveWaveTileCount;
                // Count fast tiles that activate <= 2 physical lobes across the entire 64-pixel wavefront!
                uint32_t PopCount = 0u;
                for (uint32_t Bit = 0u; Bit < 12u; ++Bit)
                {
                    if ((WaveCombinedMask & (1u << Bit)) != 0u) ++PopCount;
                }
                if (PopCount <= 2u)
                {
                    ++Frame.SingleLobeFastTiles;
                }
            }

            // Second pass: shade opaque primary + composite 2-layer glass depth peel!
            for (uint32_t PY = TileY; PY < EndY; ++PY)
            {
                for (uint32_t PX = TileX; PX < EndX; ++PX)
                {
                    const uint32_t    LocalIdx = (PY - TileY) * 8u + (PX - TileX);
                    const std::size_t PixOff   = static_cast<std::size_t>(PY) * Frame.WidthPx + PX;
                    const auto&       OpaqueHit= TileSamples[LocalIdx];

                    Mathematics::LinearRGB PixelHdr {};
                    if (OpaqueHit.ValidHit)
                    {
                        PixelHdr                      = ShadeResolvedSample(OpaqueHit, WaveCombinedMask);
                        Frame.MotionVectorsPx[PixOff] = OpaqueHit.MotionVectorPx;
                        const float DetUv = std::fabs(
                            OpaqueHit.Footprint.DerivativeDuDx.U * OpaqueHit.Footprint.DerivativeDuDy.V -
                            OpaqueHit.Footprint.DerivativeDuDx.V * OpaqueHit.Footprint.DerivativeDuDy.U);
                        Frame.FootprintAreaDetUv[PixOff] = DetUv;
                    }
                    else
                    {
                        const Mathematics::Axis3D SkyRay = ConstructPrimaryRayDirection(
                            CurBasis, static_cast<float>(PX) + 0.5f, static_cast<float>(PY) + 0.5f);
                        PixelHdr = EvaluateSkyBackdropRadiance(SkyRay);
                    }

                    // 2-Layer Dielectric Glass Peel Composite (Beer-Lambert thickness + Abbe chromatic refraction offset + exact Fresnel!)
                    const uint64_t PeelFront = VisibilityTargets.GlassPeelFront64[PixOff];
                    if (PeelFront != 0ull)
                    {
                        const auto GlassFront = ReconstructAnalyticPixelHit(PX, PY, PeelFront, CurBasis, PrevBasis, Instances);
                        if (GlassFront.ValidHit && GlassFront.MaterialSlot < MatCount)
                        {
                            ++Frame.GlassCompositePixelCount;
                            Frame.MotionVectorsPx[PixOff] = GlassFront.MotionVectorPx;

                            const auto& GlassMat = UnpackedMaterials[GlassFront.MaterialSlot];
                            const float CosTheta = std::clamp(
                                Mathematics::EvaluateDot(GlassFront.TangentFrame.NormalZ, GlassFront.WorldViewDirection),
                                0.0f, 1.0f);
                            const float FresnelR = MaterialSpace::Scattering::EvaluateExactDielectricFresnel(
                                CosTheta, GlassMat.Specular.DielectricIOR);

                            // Measure physical slab thickness between PeelFront64 and PeelSecond64!
                            float SlabDistanceMeters = 0.08f;
                            const uint64_t PeelBack  = VisibilityTargets.GlassPeelSecond64[PixOff];
                            if (PeelBack != 0ull)
                            {
                                const auto GlassBack = ReconstructAnalyticPixelHit(PX, PY, PeelBack, CurBasis, PrevBasis, Instances);
                                if (GlassBack.ValidHit)
                                {
                                    SlabDistanceMeters = std::max(0.01f, Mathematics::EvaluateLength(
                                        GlassBack.WorldPosition - GlassFront.WorldPosition));
                                }
                            }

                            const float PathRatio = SlabDistanceMeters / std::max(1.0e-3f, GlassMat.Transmission.AttenuationDistanceMeter);
                            const Mathematics::LinearRGB BeerTrans {
                                std::pow(std::clamp(GlassMat.Transmission.AttenuationColor.R, 1.0e-4f, 1.0f), PathRatio),
                                std::pow(std::clamp(GlassMat.Transmission.AttenuationColor.G, 1.0e-4f, 1.0f), PathRatio),
                                std::pow(std::clamp(GlassMat.Transmission.AttenuationColor.B, 1.0e-4f, 1.0f), PathRatio),
                            };

                            // Specular highlight on front glass shell
                            const auto GlassCoatHighlight = ShadeResolvedSample(
                                GlassFront, MaterialSpace::LobeSpecularMetalGGX | MaterialSpace::LobeClearcoat);

                            const float TransWindow = (1.0f - FresnelR) * GlassMat.Transmission.TransmissionWeight;
                            PixelHdr = Mathematics::LinearRGB{
                                PixelHdr.R * TransWindow * BeerTrans.R + GlassCoatHighlight.R + FresnelR * 0.22f,
                                PixelHdr.G * TransWindow * BeerTrans.G + GlassCoatHighlight.G + FresnelR * 0.25f,
                                PixelHdr.B * TransWindow * BeerTrans.B + GlassCoatHighlight.B + FresnelR * 0.30f,
                            };
                        }
                    }

                    Frame.LinearHdrRadiance[PixOff] = PixelHdr;
                    const auto Tone = ApplyAcesFilmic(PixelHdr);
                    Frame.ToneMappedSrgb8[PixOff * 4u + 0u] = static_cast<uint8_t>( std::round(EncodeLinearToSrgb(Tone.R) * 255.0f));
                    Frame.ToneMappedSrgb8[PixOff * 4u + 1u] = static_cast<uint8_t>( std::round(EncodeLinearToSrgb(Tone.G) * 255.0f));
                    Frame.ToneMappedSrgb8[PixOff * 4u + 2u] = static_cast<uint8_t>( std::round(EncodeLinearToSrgb(Tone.B) * 255.0f));
                    Frame.ToneMappedSrgb8[PixOff * 4u + 3u] = 255u;
                }
            }
        }
    }

    return Frame;
}

} // namespace Frontier::GeometricRaster
