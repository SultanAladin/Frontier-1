/*============================================================================================================================================
                                                    STOKESCONTOURIRRADIANCESEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Order 2 GI Candidate 1: Exact Sutherland-Hodgman spherical-polygon Stokes contour integral + 2-bounce diffuse patch radiosity

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/StokesContourIrradianceSequence.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::GeometricRaster::GlobalIllumination
{

namespace
{

[[nodiscard]] float EvaluateDiskFallbackFormFactor(
    Mathematics::Axis3D ReceiverPos,
    Mathematics::Axis3D ReceiverNormal,
    Mathematics::Axis3D DiskCenter,
    Mathematics::Axis3D DiskNormal,
    float               DiskArea,
    bool                TwoSided) noexcept
{
    const Mathematics::Axis3D Delta = DiskCenter - ReceiverPos;
    const float DistSq = Mathematics::EvaluateLengthSquared(Delta);
    if (DistSq <= 1.0e-7f || DiskArea <= 0.0f)
    {
        return 0.0f;
    }
    const float InvDist = 1.0f / std::sqrt(DistSq);
    const Mathematics::Axis3D Wi = Delta * InvDist;
    const float CosR = Mathematics::EvaluateDot(ReceiverNormal, Wi);
    if (CosR <= 0.0f)
    {
        return 0.0f;
    }
    const float RawCosE = Mathematics::EvaluateDot(DiskNormal, Wi * -1.0f);
    const float CosE    = TwoSided ? std::fabs(RawCosE) : std::max(0.0f, RawCosE);
    if (CosE <= 0.0f)
    {
        return 0.0f;
    }
    return (DiskArea * CosR * CosE) / (Constants::PiFloat * DistSq + DiskArea);
}

} // namespace

float EvaluateClippedTriangleFormFactor(
    Mathematics::Axis3D ReceiverPos,
    Mathematics::Axis3D ReceiverNormal,
    Mathematics::Axis3D TriV0,
    Mathematics::Axis3D TriV1,
    Mathematics::Axis3D TriV2,
    Mathematics::Axis3D TriNormal,
    bool                TwoSidedRadiator) noexcept
{
    const Mathematics::Axis3D Centroid = (TriV0 + TriV1 + TriV2) * (1.0f / 3.0f);
    const float Facing = Mathematics::EvaluateDot(TriNormal, ReceiverPos - Centroid);
    if (!TwoSidedRadiator && Facing <= 1.0e-5f)
    {
        return 0.0f;
    }

    // Sutherland-Hodgman clip against receiver horizon plane dot(ReceiverNormal, P - ReceiverPos) >= 1e-4
    std::array<Mathematics::Axis3D, 3u> RawVerts {{
        TriV0 - ReceiverPos,
        TriV1 - ReceiverPos,
        TriV2 - ReceiverPos,
    }};
    if (TwoSidedRadiator && Facing < 0.0f)
    {
        std::swap(RawVerts[1], RawVerts[2]);
    }

    std::array<Mathematics::Axis3D, 6u> Clipped {};
    uint32_t ClippedCount = 0u;
    constexpr float PlaneClipEps = 1.0e-4f;

    Mathematics::Axis3D PrevP = RawVerts[2];
    float               PrevH = Mathematics::EvaluateDot(ReceiverNormal, PrevP);
    for (uint32_t I = 0u; I < 3u; ++I)
    {
        const Mathematics::Axis3D CurrP = RawVerts[I];
        const float               CurrH = Mathematics::EvaluateDot(ReceiverNormal, CurrP);
        const bool PrevIn = (PrevH >= PlaneClipEps);
        const bool CurrIn = (CurrH >= PlaneClipEps);

        if (PrevIn != CurrIn)
        {
            const float Alpha = (PlaneClipEps - PrevH) / (CurrH - PrevH);
            Clipped[ClippedCount++] = PrevP + (CurrP - PrevP) * std::clamp(Alpha, 0.0f, 1.0f);
        }
        if (CurrIn)
        {
            Clipped[ClippedCount++] = CurrP;
        }
        PrevP = CurrP;
        PrevH = CurrH;
    }

    if (ClippedCount < 3u)
    {
        return 0.0f;
    }

    // Project onto unit hemisphere around ReceiverPos
    std::array<Mathematics::Axis3D, 6u> UnitDirs {};
    for (uint32_t I = 0u; I < ClippedCount; ++I)
    {
        const float LenSq = Mathematics::EvaluateLengthSquared(Clipped[I]);
        if (LenSq <= 1.0e-10f)
        {
            return 0.0f;
        }
        UnitDirs[I] = Clipped[I] * (1.0f / std::sqrt(LenSq));
    }

    // Exact Lambert (1760) / Arvo Stokes boundary contour sum
    Mathematics::Axis3D VectorIrradiance { 0.0f, 0.0f, 0.0f };
    for (uint32_t I = 0u; I < ClippedCount; ++I)
    {
        const Mathematics::Axis3D& U = UnitDirs[I];
        const Mathematics::Axis3D& V = UnitDirs[(I + 1u) % ClippedCount];
        const Mathematics::Axis3D CrossUV = Mathematics::EvaluateCross(U, V);
        const float SinTheta = Mathematics::EvaluateLength(CrossUV);
        const float CosTheta = std::clamp(Mathematics::EvaluateDot(U, V), -1.0f, 1.0f);
        if (SinTheta > 1.0e-6f)
        {
            const float Theta = std::atan2(SinTheta, CosTheta);
            VectorIrradiance  = VectorIrradiance + CrossUV * (Theta / SinTheta);
        }
    }

    const float FormFactor = std::fabs(Mathematics::EvaluateDot(ReceiverNormal, VectorIrradiance)) * (0.5f / Constants::PiFloat);
    return std::clamp(FormFactor, 0.0f, 1.0f);
}

void StokesContourIrradianceSequence::Reset() noexcept
{
    Radiators.clear();
    Clusters.clear();
    Patches.clear();
}

void StokesContourIrradianceSequence::BuildFromScene(
    std::span<const SpatialAcceleration::RayTrianglePrimitive> Primitives,
    std::span<const Mathematics::LinearRGB>                    MaterialAlbedoBySlot,
    std::span<const Mathematics::LinearRGB>                    MaterialEmissiveBySlot,
    const DirectLighting::HybridOccluderEnvironment&           Occluders,
    const StokesContourTierSettings&                           Settings)
{
    Reset();
    if (Primitives.empty())
    {
        return;
    }

    // 1. Gather emissive triangles and spatially cluster neighboring triangles by material & proximity (~0.9 m)
    for (const auto& Tri : Primitives)
    {
        const uint16_t MatIdx = Tri.MaterialOrdinal;
        if (MatIdx >= MaterialEmissiveBySlot.size())
        {
            continue;
        }
        const auto Emissive = MaterialEmissiveBySlot[MatIdx];
        const float MaxEm   = std::max({ Emissive.R, Emissive.G, Emissive.B });
        if (MaxEm <= 1.0e-3f)
        {
            continue;
        }

        const Mathematics::Axis3D V0 = Tri.Corner0;
        const Mathematics::Axis3D V1 = Tri.Corner0 + Tri.Edge01;
        const Mathematics::Axis3D V2 = Tri.Corner0 + Tri.Edge02;
        const Mathematics::Axis3D CrossE = Mathematics::EvaluateCross(Tri.Edge01, Tri.Edge02);
        const float DoubleArea = Mathematics::EvaluateLength(CrossE);
        if (DoubleArea <= 1.0e-6f)
        {
            continue;
        }

        EmissiveTriangleRadiator Em {};
        Em.Vertex0         = V0;
        Em.Vertex1         = V1;
        Em.Vertex2         = V2;
        Em.GeometricNormal = CrossE * (1.0f / DoubleArea);
        Em.Centroid        = (V0 + V1 + V2) * (1.0f / 3.0f);
        Em.SurfaceArea     = 0.5f * DoubleArea;
        Em.RadianceRgb     = Emissive;
        Em.TwoSidedRadiator = false;

        uint32_t AssignedCluster = static_cast<uint32_t>(Clusters.size());
        for (uint32_t C = 0u; C < Clusters.size(); ++C)
        {
            const float DistSq = Mathematics::EvaluateLengthSquared(Em.Centroid - Clusters[C].RepresentativePos);
            const float NDot   = Mathematics::EvaluateDot(Em.GeometricNormal, Clusters[C].RepresentativeNormal);
            if (DistSq <= 0.25f && NDot > 0.65f && Clusters.size() <= 28u)
            {
                AssignedCluster = C;
                break;
            }
        }

        if (AssignedCluster == Clusters.size())
        {
            EmissiveLuminaireCluster NewC {};
            NewC.RepresentativePos    = Em.Centroid;
            NewC.RepresentativeNormal = Em.GeometricNormal;
            NewC.BoundingRadius       = std::sqrt(Em.SurfaceArea / Constants::PiFloat);
            NewC.TotalArea            = Em.SurfaceArea;
            NewC.MeanRadianceRgb      = Em.RadianceRgb;
            NewC.TwoSidedCluster      = Em.TwoSidedRadiator;
            Clusters.push_back(NewC);
        }
        else
        {
            auto& CurC = Clusters[AssignedCluster];
            const float PrevArea = CurC.TotalArea;
            const float NextArea = PrevArea + Em.SurfaceArea;
            CurC.RepresentativePos = (CurC.RepresentativePos * PrevArea + Em.Centroid * Em.SurfaceArea) * (1.0f / NextArea);
            CurC.MeanRadianceRgb   = {
                (CurC.MeanRadianceRgb.R   * PrevArea + Em.RadianceRgb.R   * Em.SurfaceArea) / NextArea,
                (CurC.MeanRadianceRgb.G * PrevArea + Em.RadianceRgb.G * Em.SurfaceArea) / NextArea,
                (CurC.MeanRadianceRgb.B  * PrevArea + Em.RadianceRgb.B  * Em.SurfaceArea) / NextArea,
            };
            CurC.TotalArea      = NextArea;
            CurC.BoundingRadius = std::max(CurC.BoundingRadius, std::sqrt(NextArea / Constants::PiFloat));
        }

        Em.ClusterId = AssignedCluster;
        Radiators.push_back(Em);
    }

    // 2. Sample ~256 deterministic area-weighted diffuse bounce patches across architectural & asset surfaces
    double TotalDiffuseArea = 0.0;
    for (const auto& Tri : Primitives)
    {
        const uint16_t MatIdx = Tri.MaterialOrdinal;
        const bool EmissiveActive = (MatIdx < MaterialEmissiveBySlot.size()) &&
            (std::max({ MaterialEmissiveBySlot[MatIdx].R, MaterialEmissiveBySlot[MatIdx].G, MaterialEmissiveBySlot[MatIdx].B }) > 1.0e-3f);
        if (EmissiveActive)
        {
            continue;
        }
        TotalDiffuseArea += 0.5 * static_cast<double>(Mathematics::EvaluateLength(Mathematics::EvaluateCross(Tri.Edge01, Tri.Edge02)));
    }

    if (TotalDiffuseArea > 1.0e-4)
    {
        constexpr uint32_t TargetPatches = 224u;
        const double AreaQuantum = TotalDiffuseArea / static_cast<double>(TargetPatches);
        double Accum = 0.0;

        for (const auto& Tri : Primitives)
        {
            const uint16_t MatIdx = Tri.MaterialOrdinal;
            const bool EmissiveActive = (MatIdx < MaterialEmissiveBySlot.size()) &&
                (std::max({ MaterialEmissiveBySlot[MatIdx].R, MaterialEmissiveBySlot[MatIdx].G, MaterialEmissiveBySlot[MatIdx].B }) > 1.0e-3f);
            if (EmissiveActive)
            {
                continue;
            }
            const Mathematics::Axis3D CrossE = Mathematics::EvaluateCross(Tri.Edge01, Tri.Edge02);
            const float DoubleArea = Mathematics::EvaluateLength(CrossE);
            if (DoubleArea <= 1.0e-6f)
            {
                continue;
            }
            const float TriArea = 0.5f * DoubleArea;
            const Mathematics::Axis3D Normal = CrossE * (1.0f / DoubleArea);
            const Mathematics::LinearRGB Albedo = (MatIdx < MaterialAlbedoBySlot.size())
                ? MaterialAlbedoBySlot[MatIdx]
                : Mathematics::LinearRGB { 0.5f, 0.5f, 0.5f };

            Accum += static_cast<double>(TriArea);
            while (Accum >= AreaQuantum && Patches.size() < TargetPatches + 32u)
            {
                Accum -= AreaQuantum;
                const uint32_t Seed = static_cast<uint32_t>(Patches.size() * 747796405u + 2891336453u);
                float U = static_cast<float>((Seed & 0xFFFFu)) / 65536.0f;
                float V = static_cast<float>((Seed >> 16u)) / 65536.0f;
                if (U + V > 1.0f)
                {
                    U = 1.0f - U;
                    V = 1.0f - V;
                }
                DiffuseBouncePatch Patch {};
                Patch.Position   = Tri.Corner0 + Tri.Edge01 * U + Tri.Edge02 * V;
                Patch.Normal     = Normal;
                Patch.AlbedoRgb  = Albedo;
                Patch.AreaMeters = static_cast<float>(AreaQuantum);
                Patch.Radius     = std::sqrt(Patch.AreaMeters / Constants::PiFloat);
                Patches.push_back(Patch);
            }
        }
    }

    // 3. Relax 1st-bounce (direct solar + emissive) -> patch radiance, then 2nd-bounce patch <-> patch diffuse bleed
    StokesContourTierSettings PatchSettings = Settings;
    const bool HasSunBounce = std::max({ Settings.SunRadiance.R, Settings.SunRadiance.G, Settings.SunRadiance.B }) > 1.0e-4f;
    DirectLighting::HybridConeShadowSetup PatchSunSetup {};
    PatchSunSetup.MaxMarchSteps = 64u;

    for (auto& P : Patches)
    {
        Mathematics::LinearRGB DirectTotal {};
        (void)EvaluateTotalIrradiance(P.Position, P.Normal, Occluders, PatchSettings, &DirectTotal, nullptr);

        if (HasSunBounce)
        {
            const float NdotS = Mathematics::EvaluateDot(P.Normal, Settings.SunDirection);
            if (NdotS > 0.0f)
            {
                float SunVis = 1.0f;
                if (Settings.HighEndZeroSdfTier && Occluders.SceneBvh != nullptr)
                {
                    SpatialAcceleration::RaySegmentQuery SRay {};
                    SRay.Origin          = P.Position + P.Normal * 4.0e-3f;
                    SRay.UnitDirection   = Settings.SunDirection;
                    SRay.MinimumDistance = 2.0e-3f;
                    SRay.MaximumDistance = 24.0f;
                    SunVis = Occluders.SceneBvh->QueryRayOccluded(SRay) ? 0.0f : 1.0f;
                }
                else
                {
                    SunVis = DirectLighting::EvaluateGtxHybridDirectionalShadow(
                        Occluders, P.Position, P.Normal, Settings.SunDirection, PatchSunSetup);
                }
                DirectTotal.R += Settings.SunRadiance.R * NdotS * SunVis;
                DirectTotal.G += Settings.SunRadiance.G * NdotS * SunVis;
                DirectTotal.B += Settings.SunRadiance.B * NdotS * SunVis;
            }
        }

        P.Radiance1 = {
            (P.AlbedoRgb.R * DirectTotal.R) / Constants::PiFloat,
            (P.AlbedoRgb.G * DirectTotal.G) / Constants::PiFloat,
            (P.AlbedoRgb.B * DirectTotal.B) / Constants::PiFloat,
        };
        P.Radiance2 = P.Radiance1;
    }

    for (std::size_t I = 0u; I < Patches.size(); ++I)
    {
        Mathematics::LinearRGB BounceE { 0.0f, 0.0f, 0.0f };
        for (std::size_t J = 0u; J < Patches.size(); ++J)
        {
            if (I == J) continue;
            const float FF = EvaluateDiskFallbackFormFactor(
                Patches[I].Position, Patches[I].Normal, Patches[J].Position, Patches[J].Normal, Patches[J].AreaMeters, false);
            if (FF <= 1.0e-5f) continue;
            BounceE.R   += Patches[J].Radiance1.R   * (Constants::PiFloat * FF);
            BounceE.G += Patches[J].Radiance1.G * (Constants::PiFloat * FF);
            BounceE.B  += Patches[J].Radiance1.B  * (Constants::PiFloat * FF);
        }
        Patches[I].Radiance2 = {
            Patches[I].Radiance1.R   + (Patches[I].AlbedoRgb.R   * BounceE.R)   / Constants::PiFloat,
            Patches[I].Radiance1.G + (Patches[I].AlbedoRgb.G * BounceE.G) / Constants::PiFloat,
            Patches[I].Radiance1.B  + (Patches[I].AlbedoRgb.B  * BounceE.B)  / Constants::PiFloat,
        };
    }
}

Mathematics::LinearRGB StokesContourIrradianceSequence::EvaluateTotalIrradiance(
    Mathematics::Axis3D                              ReceiverPos,
    Mathematics::Axis3D                              ReceiverNormal,
    const DirectLighting::HybridOccluderEnvironment& Occluders,
    const StokesContourTierSettings&                 Settings,
    Mathematics::LinearRGB*                          OptionalOutDirectEmissiveE,
    Mathematics::LinearRGB*                          OptionalOutBounceIndirectE) const noexcept
{
    Mathematics::LinearRGB DirectEmissiveE { 0.0f, 0.0f, 0.0f };
    if (!Clusters.empty())
    {
        std::array<float, 64u> ClusterVisibility {};
        std::array<uint8_t, 64u> ClusterEvaluated {};
        const std::size_t MaxClusters = std::min(Clusters.size(), ClusterVisibility.size());

        auto QueryClusterVis = [&](uint32_t CIdx) noexcept -> float
        {
            if (CIdx >= MaxClusters)
            {
                return 1.0f;
            }
            if (ClusterEvaluated[CIdx] != 0u)
            {
                return ClusterVisibility[CIdx];
            }
            ClusterEvaluated[CIdx] = 1u;
            const auto& Cl = Clusters[CIdx];
            const Mathematics::Axis3D TargetPos = Cl.RepresentativePos + Cl.RepresentativeNormal * 0.04f;
            float Vis = 1.0f;
            if (Settings.HighEndZeroSdfTier && Occluders.SceneBvh != nullptr)
            {
                Vis = DirectLighting::EvaluateHighEndBvhLuminaireVisibility(
                    *Occluders.SceneBvh, ReceiverPos, ReceiverNormal, TargetPos);
            }
            else
            {
                Vis = DirectLighting::EvaluateGtxHybridLuminaireVisibility(
                    Occluders, ReceiverPos, ReceiverNormal, TargetPos, Cl.BoundingRadius);
            }
            ClusterVisibility[CIdx] = Vis;
            return Vis;
        };

        const float FadeStart = std::max(1.0f, Settings.ExactContourCutoffM - 0.6f);
        const float FadeEnd   = Settings.ExactContourCutoffM + 0.6f;
        for (const auto& Em : Radiators)
        {
            const float Dist = std::sqrt(Mathematics::EvaluateLengthSquared(Em.Centroid - ReceiverPos));
            float FF = 0.0f;
            if (!Settings.HighEndZeroSdfTier && Dist >= FadeEnd)
            {
                FF = EvaluateDiskFallbackFormFactor(
                    ReceiverPos, ReceiverNormal, Em.Centroid, Em.GeometricNormal, Em.SurfaceArea, Em.TwoSidedRadiator);
            }
            else if (!Settings.HighEndZeroSdfTier && Dist > FadeStart)
            {
                const float T = (Dist - FadeStart) / (FadeEnd - FadeStart);
                const float H = T * T * (3.0f - 2.0f * T);
                const float ExactFF = EvaluateClippedTriangleFormFactor(
                    ReceiverPos, ReceiverNormal, Em.Vertex0, Em.Vertex1, Em.Vertex2, Em.GeometricNormal, Em.TwoSidedRadiator);
                const float DiskFF  = EvaluateDiskFallbackFormFactor(
                    ReceiverPos, ReceiverNormal, Em.Centroid, Em.GeometricNormal, Em.SurfaceArea, Em.TwoSidedRadiator);
                FF = ExactFF * (1.0f - H) + DiskFF * H;
            }
            else
            {
                FF = EvaluateClippedTriangleFormFactor(
                    ReceiverPos, ReceiverNormal, Em.Vertex0, Em.Vertex1, Em.Vertex2, Em.GeometricNormal, Em.TwoSidedRadiator);
            }
            if (FF <= 1.0e-6f)
            {
                continue;
            }
            float Vis = 1.0f;
            if (Settings.HighEndZeroSdfTier && Occluders.SceneBvh != nullptr)
            {
                static constexpr std::array<std::array<float, 3u>, 4u> Bary4 {{
                    {{ 0.33333334f, 0.33333333f, 0.33333333f }},
                    {{ 0.65f, 0.175f, 0.175f }},
                    {{ 0.175f, 0.65f, 0.175f }},
                    {{ 0.175f, 0.175f, 0.65f }},
                }};
                float VisSum = 0.0f;
                for (const auto& B : Bary4)
                {
                    const Mathematics::Axis3D SamplePos =
                        (Em.Vertex0 * B[0] + Em.Vertex1 * B[1] + Em.Vertex2 * B[2]) + Em.GeometricNormal * 8.0e-3f;
                    VisSum += DirectLighting::EvaluateHighEndBvhLuminaireVisibility(
                        *Occluders.SceneBvh, ReceiverPos, ReceiverNormal, SamplePos);
                }
                Vis = 0.25f * VisSum;
            }
            else
            {
                Vis = QueryClusterVis(Em.ClusterId);
            }
            if (Vis <= 1.0e-4f)
            {
                continue;
            }
            const float Weight = Constants::PiFloat * FF * Vis;
            DirectEmissiveE.R   += Em.RadianceRgb.R   * Weight;
            DirectEmissiveE.G += Em.RadianceRgb.G * Weight;
            DirectEmissiveE.B  += Em.RadianceRgb.B  * Weight;
        }
    }

    Mathematics::LinearRGB BounceIndirectE { 0.0f, 0.0f, 0.0f };
    if (OptionalOutBounceIndirectE != nullptr || OptionalOutDirectEmissiveE == nullptr)
    {
        for (const auto& P : Patches)
        {
            if (std::max({ P.Radiance2.R, P.Radiance2.G, P.Radiance2.B }) <= 1.0e-4f)
            {
                continue;
            }
            const float FF = EvaluateDiskFallbackFormFactor(
                ReceiverPos, ReceiverNormal, P.Position, P.Normal, P.AreaMeters, false);
            if (FF <= 1.0e-5f)
            {
                continue;
            }
            float Vis = 1.0f;
            const float DistSq = Mathematics::EvaluateLengthSquared(P.Position - ReceiverPos);
            if (DistSq > 0.0025f && DistSq < 33.64f)
            {
                float RawVis = 1.0f;
                if (Settings.HighEndZeroSdfTier && Occluders.SceneBvh != nullptr)
                {
                    RawVis = DirectLighting::EvaluateHighEndBvhLuminaireVisibility(
                        *Occluders.SceneBvh, ReceiverPos, ReceiverNormal, P.Position);
                }
                else
                {
                    RawVis = DirectLighting::EvaluateGtxHybridLuminaireVisibility(
                        Occluders, ReceiverPos, ReceiverNormal, P.Position, P.Radius);
                }
                const float Dist = std::sqrt(DistSq);
                const float Fade = std::clamp((Dist - 3.6f) / (5.8f - 3.6f), 0.0f, 1.0f);
                const float H    = Fade * Fade * (3.0f - 2.0f * Fade);
                Vis              = RawVis * (1.0f - H) + H;
            }
            const float Weight = Constants::PiFloat * FF * Vis;
            BounceIndirectE.R   += P.Radiance2.R   * Weight;
            BounceIndirectE.G += P.Radiance2.G * Weight;
            BounceIndirectE.B  += P.Radiance2.B  * Weight;
        }
    }

    if (OptionalOutDirectEmissiveE != nullptr)
    {
        *OptionalOutDirectEmissiveE = DirectEmissiveE;
    }
    if (OptionalOutBounceIndirectE != nullptr)
    {
        *OptionalOutBounceIndirectE = BounceIndirectE;
    }
    return {
        DirectEmissiveE.R   + BounceIndirectE.R,
        DirectEmissiveE.G + BounceIndirectE.G,
        DirectEmissiveE.B  + BounceIndirectE.B,
    };
}

} // namespace Frontier::GeometricRaster::GlobalIllumination
