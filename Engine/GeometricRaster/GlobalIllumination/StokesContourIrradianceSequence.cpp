/*============================================================================================================================================
                                                   STOKESCONTOURIRRADIANCESEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Order 2 GI Candidate 1: Exact Stokes Contour Emissive Irradiance + Stratified Occluded 2-Bounce Diffuse Patches

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

[[nodiscard]] float EvaluatePeakChannel(const Mathematics::LinearRGB& Color) noexcept
{
    return std::max({ Color.R, Color.G, Color.B });
}

[[nodiscard]] uint32_t ReverseBits32(uint32_t Bits) noexcept
{
    Bits = (Bits << 16u) | (Bits >> 16u);
    Bits = ((Bits & 0x00FF00FFu) << 8u) | ((Bits & 0xFF00FF00u) >> 8u);
    Bits = ((Bits & 0x0F0F0F0Fu) << 4u) | ((Bits & 0xF0F0F0F0u) >> 4u);
    Bits = ((Bits & 0x33333333u) << 2u) | ((Bits & 0xCCCCCCCCu) >> 2u);
    Bits = ((Bits & 0x55555555u) << 1u) | ((Bits & 0xAAAAAAAAu) >> 1u);
    return Bits;
}

[[nodiscard]] uint32_t LaineKarrasScramble(uint32_t X, uint32_t Seed) noexcept
{
    X += Seed;
    X ^= X * 0x6C50B47Cu;
    X ^= X * 0xB82F1E52u;
    X ^= X * 0xC7AFE638u;
    X ^= X * 0x8D22F6E6u;
    return X;
}

// Deterministic Owen-scrambled Sobol (0,2) sequence in [0, 1)^2 — zero clumping, zero RNG state
void SampleOwenSobol02(uint32_t SampleIdx, uint32_t Seed, float& OutU, float& OutV) noexcept
{
    uint32_t SobolX = ReverseBits32(SampleIdx);
    uint32_t V      = 0x80000000u;
    uint32_t SobolY = 0u;
    for (uint32_t N = SampleIdx; N != 0u; N >>= 1u)
    {
        if ((N & 1u) != 0u)
        {
            SobolY ^= V;
        }
        V ^= (V >> 1u);
    }
    SobolX = ReverseBits32(LaineKarrasScramble(ReverseBits32(SobolX), Seed ^ 0x9E3779B9u));
    SobolY = ReverseBits32(LaineKarrasScramble(ReverseBits32(SobolY), Seed ^ 0xBB67AE85u));
    OutU   = static_cast<float>(SobolX >> 8u) * (1.0f / 16777216.0f);
    OutV   = static_cast<float>(SobolY >> 8u) * (1.0f / 16777216.0f);
}

[[nodiscard]] float EvaluateSegmentVisibility(
    Mathematics::Axis3D                              ShadingPos,
    Mathematics::Axis3D                              ShadingNormal,
    Mathematics::Axis3D                              TargetPos,
    float                                            SourceRadius,
    const DirectLighting::HybridOccluderEnvironment& Occluders,
    const StokesContourTierSettings&                 Settings) noexcept
{
    if (Settings.HighEndZeroSdfTier)
    {
        if (Occluders.SceneBvh == nullptr)
        {
            return 1.0f;
        }
        return DirectLighting::EvaluateHighEndBvhLuminaireVisibility(
            *Occluders.SceneBvh, ShadingPos, ShadingNormal, TargetPos);
    }
    return DirectLighting::EvaluateGtxHybridLuminaireVisibility(
        Occluders, ShadingPos, ShadingNormal, TargetPos, SourceRadius);
}

[[nodiscard]] float EvaluateClusterVisibility(
    const EmissiveLuminaireCluster&                  Cluster,
    Mathematics::Axis3D                              ShadingPos,
    Mathematics::Axis3D                              ShadingNormal,
    const DirectLighting::HybridOccluderEnvironment& Occluders,
    const StokesContourTierSettings&                 Settings) noexcept
{
    if (Settings.HighEndZeroSdfTier)
    {
        if (Occluders.SceneBvh == nullptr)
        {
            return 1.0f;
        }
        // Multi-sample 3-point visibility across luminaire extent eliminates binary hard step on wide softboxes
        const float VisA = DirectLighting::EvaluateHighEndBvhLuminaireVisibility(
            *Occluders.SceneBvh, ShadingPos, ShadingNormal, Cluster.SamplePosA);
        if (Cluster.BoundingRadius <= 0.14f)
        {
            return VisA;
        }
        const float VisB = DirectLighting::EvaluateHighEndBvhLuminaireVisibility(
            *Occluders.SceneBvh, ShadingPos, ShadingNormal, Cluster.SamplePosB);
        const float VisC = DirectLighting::EvaluateHighEndBvhLuminaireVisibility(
            *Occluders.SceneBvh, ShadingPos, ShadingNormal, Cluster.SamplePosC);
        return (VisA + VisB + VisC) * (1.0f / 3.0f);
    }
    return DirectLighting::EvaluateGtxHybridLuminaireVisibility(
        Occluders, ShadingPos, ShadingNormal, Cluster.RepresentativePos, Cluster.BoundingRadius);
}

[[nodiscard]] Mathematics::LinearRGB EvaluateAreaLightPatchIrradiance(
    Mathematics::Axis3D                     PatchPos,
    Mathematics::Axis3D                     PatchNormal,
    std::span<const PolygonalAreaQuadLight> AreaLights,
    const DirectLighting::HybridOccluderEnvironment& Occluders,
    const StokesContourTierSettings&        Settings) noexcept
{
    Mathematics::LinearRGB Total { 0.0f, 0.0f, 0.0f };
    for (const auto& Quad : AreaLights)
    {
        const Mathematics::Axis3D HalfU = Quad.RightTangent * Quad.HalfWidthMeters;
        const Mathematics::Axis3D HalfV = Quad.UpBitangent  * Quad.HalfHeightMeters;
        const Mathematics::Axis3D EmitN = Mathematics::NormalizeAxis(
            Mathematics::EvaluateCross(Quad.RightTangent, Quad.UpBitangent), { 0.0f, 0.0f, -1.0f });
        const Mathematics::Axis3D C0    = Quad.CenterWorld - HalfU - HalfV;
        const Mathematics::Axis3D C1    = Quad.CenterWorld + HalfU - HalfV;
        const Mathematics::Axis3D C2    = Quad.CenterWorld + HalfU + HalfV;
        const Mathematics::Axis3D C3    = Quad.CenterWorld - HalfU + HalfV;
        const float Radius = std::hypot(Quad.HalfWidthMeters, Quad.HalfHeightMeters);
        const float Vis    = EvaluateSegmentVisibility(
            PatchPos, PatchNormal, Quad.CenterWorld, Radius, Occluders, Settings);
        if (Vis <= 1.0e-4f)
        {
            continue;
        }
        const float Form = EvaluateClippedStokesTriangleFormFactor(PatchPos, PatchNormal, C0, C1, C2, EmitN)
                         + EvaluateClippedStokesTriangleFormFactor(PatchPos, PatchNormal, C0, C2, C3, EmitN);
        if (Form > 0.0f)
        {
            Total = Total + Quad.EmittedRadiance * (Constants::PiFloat * Form * Vis);
        }
    }
    return Total;
}

} // namespace

float EvaluateClippedStokesTriangleFormFactor(
    Mathematics::Axis3D ShadingPos,
    Mathematics::Axis3D ShadingNormal,
    Mathematics::Axis3D CornerA,
    Mathematics::Axis3D CornerB,
    Mathematics::Axis3D CornerC,
    Mathematics::Axis3D EmitterNormal) noexcept
{
    const Mathematics::Axis3D Centroid = (CornerA + CornerB + CornerC) * (1.0f / 3.0f);
    if (Mathematics::EvaluateDot(ShadingPos - Centroid, EmitterNormal) <= 1.0e-4f)
    {
        return 0.0f;
    }

    std::array<Mathematics::Axis3D, 3u> Rel {{
        CornerA - ShadingPos,
        CornerB - ShadingPos,
        CornerC - ShadingPos,
    }};
    std::array<float, 3u> Heights {{
        Mathematics::EvaluateDot(Rel[0u], ShadingNormal),
        Mathematics::EvaluateDot(Rel[1u], ShadingNormal),
        Mathematics::EvaluateDot(Rel[2u], ShadingNormal),
    }};

    constexpr float HorizonEps = 1.0e-5f;
    std::array<Mathematics::Axis3D, 6u> Clipped {};
    uint32_t ClippedCount = 0u;
    for (uint32_t I = 0u; I < 3u; ++I)
    {
        const uint32_t Next = (I + 1u) % 3u;
        const bool InCurr   = Heights[I] >= HorizonEps;
        const bool InNext   = Heights[Next] >= HorizonEps;
        if (InCurr)
        {
            Clipped[ClippedCount++] = Rel[I];
        }
        if (InCurr != InNext)
        {
            const float T = (HorizonEps - Heights[I]) / (Heights[Next] - Heights[I]);
            Clipped[ClippedCount++] = Rel[I] + (Rel[Next] - Rel[I]) * T;
        }
    }
    if (ClippedCount < 3u)
    {
        return 0.0f;
    }

    std::array<Mathematics::Axis3D, 6u> UnitDirs {};
    for (uint32_t I = 0u; I < ClippedCount; ++I)
    {
        const float Len = Mathematics::EvaluateLength(Clipped[I]);
        if (Len <= 1.0e-6f)
        {
            return 0.0f;
        }
        UnitDirs[I] = Clipped[I] * (1.0f / Len);
    }

    Mathematics::Axis3D VectorIrrad { 0.0f, 0.0f, 0.0f };
    for (uint32_t I = 0u; I < ClippedCount; ++I)
    {
        const Mathematics::Axis3D& U = UnitDirs[I];
        const Mathematics::Axis3D& V = UnitDirs[(I + 1u) % ClippedCount];
        const Mathematics::Axis3D  CrossUV = Mathematics::EvaluateCross(U, V);
        const float SinAngle = Mathematics::EvaluateLength(CrossUV);
        const float CosAngle = std::clamp(Mathematics::EvaluateDot(U, V), -1.0f, 1.0f);
        const float Theta    = std::atan2(SinAngle, CosAngle);
        const float Scale    = (SinAngle > 1.0e-6f) ? (Theta / SinAngle) : 1.0f;
        VectorIrrad = VectorIrrad + CrossUV * Scale;
    }

    const float FormFactor = std::fabs(Mathematics::EvaluateDot(VectorIrrad, ShadingNormal)) * (1.0f / Constants::TauFloat);
    return std::clamp(FormFactor, 0.0f, 1.0f);
}

void StokesContourIrradianceSequence::BuildFromScene(
    std::span<const SpatialAcceleration::RayTrianglePrimitive> Primitives,
    std::span<const Mathematics::LinearRGB>                    MaterialDiffuseAlbedos,
    std::span<const Mathematics::LinearRGB>                    MaterialEmissiveRadiance,
    const DirectLighting::HybridOccluderEnvironment&           Occluders,
    const StokesContourTierSettings&                           Settings)
{
    EmissiveTriangles.clear();
    Clusters.clear();
    BouncePatches.clear();

    double TotalDiffuseArea = 0.0;
    struct CandidateTri
    {
        uint32_t               PrimIndex = 0u;
        float                  Area      = 0.0f;
        Mathematics::Axis3D    Normal    {};
        Mathematics::LinearRGB Albedo    {};
    };
    std::vector<CandidateTri> DiffuseCandidates;
    DiffuseCandidates.reserve(Primitives.size());

    for (uint32_t Idx = 0u; Idx < static_cast<uint32_t>(Primitives.size()); ++Idx)
    {
        const auto& Prim = Primitives[Idx];
        const Mathematics::Axis3D CrossVec = Mathematics::EvaluateCross(Prim.Edge01, Prim.Edge02);
        const float DoubleArea = Mathematics::EvaluateLength(CrossVec);
        if (DoubleArea <= 1.0e-6f)
        {
            continue;
        }
        const float Area = 0.5f * DoubleArea;
        const Mathematics::Axis3D TriN = CrossVec * (1.0f / DoubleArea);

        const Mathematics::LinearRGB Emissive = (Prim.MaterialOrdinal < MaterialEmissiveRadiance.size())
            ? MaterialEmissiveRadiance[Prim.MaterialOrdinal]
            : Mathematics::LinearRGB {};
        const Mathematics::LinearRGB Albedo = (Prim.MaterialOrdinal < MaterialDiffuseAlbedos.size())
            ? MaterialDiffuseAlbedos[Prim.MaterialOrdinal]
            : Mathematics::LinearRGB { 0.5f, 0.5f, 0.5f };

        if (EvaluatePeakChannel(Emissive) > 0.05f)
        {
            const Mathematics::Axis3D B = Prim.Corner0 + Prim.Edge01;
            const Mathematics::Axis3D C = Prim.Corner0 + Prim.Edge02;
            const Mathematics::Axis3D Centroid = (Prim.Corner0 + B + C) * (1.0f / 3.0f);

            uint32_t AssignedCluster = static_cast<uint32_t>(Clusters.size());
            for (uint32_t CIdx = 0u; CIdx < static_cast<uint32_t>(Clusters.size()); ++CIdx)
            {
                const float Dist  = Mathematics::EvaluateLength(Centroid - Clusters[CIdx].RepresentativePos);
                const float Align = Mathematics::EvaluateDot(TriN, Clusters[CIdx].RepresentativeNormal);
                if (Dist < 0.42f && Align > 0.85f)
                {
                    AssignedCluster = CIdx;
                    break;
                }
            }
            if (AssignedCluster == Clusters.size())
            {
                EmissiveLuminaireCluster NewCl {};
                NewCl.RepresentativePos    = Centroid;
                NewCl.SamplePosA           = Centroid;
                NewCl.SamplePosB           = Centroid;
                NewCl.SamplePosC           = Centroid;
                NewCl.RepresentativeNormal = TriN;
                NewCl.TotalArea            = Area;
                NewCl.BoundingRadius       = std::sqrt(Area / Constants::PiFloat);
                Clusters.push_back(NewCl);
            }
            else
            {
                auto& Cl        = Clusters[AssignedCluster];
                const float Sum = Cl.TotalArea + Area;
                Cl.RepresentativePos    = (Cl.RepresentativePos * Cl.TotalArea + Centroid * Area) * (1.0f / Sum);
                Cl.RepresentativeNormal = Mathematics::NormalizeAxis(
                    Cl.RepresentativeNormal * Cl.TotalArea + TriN * Area, TriN);
                Cl.TotalArea            = Sum;
                Cl.BoundingRadius       = std::max(Cl.BoundingRadius, std::sqrt(Sum / Constants::PiFloat));
            }

            EmissiveTriangleEmitter Emit {};
            Emit.CornerA       = Prim.Corner0;
            Emit.CornerB       = B;
            Emit.CornerC       = C;
            Emit.Centroid      = Centroid;
            Emit.OutwardNormal = TriN;
            Emit.Radiance      = Emissive;
            Emit.SurfaceArea   = Area;
            Emit.ClusterId     = AssignedCluster;
            EmissiveTriangles.push_back(Emit);
        }
        else if (EvaluatePeakChannel(Albedo) > 0.02f && Area > 0.025f)
        {
            TotalDiffuseArea += static_cast<double>(Area);
            DiffuseCandidates.push_back({ Idx, Area, TriN, Albedo });
        }
    }

    // Build 3 deterministic spread sample points per luminaire cluster for soft multi-point HighEnd visibility
    for (uint32_t CIdx = 0u; CIdx < static_cast<uint32_t>(Clusters.size()); ++CIdx)
    {
        auto& Cl = Clusters[CIdx];
        Mathematics::Axis3D TanU {}, BitV {};
        DistanceFieldSpace::ConstructLimbBasis(Cl.RepresentativeNormal, TanU, BitV);
        float MinU = 0.0f, MaxU = 0.0f, MinV = 0.0f, MaxV = 0.0f;
        for (const auto& Emit : EmissiveTriangles)
        {
            if (Emit.ClusterId != CIdx) continue;
            for (const auto& Vtx : { Emit.CornerA, Emit.CornerB, Emit.CornerC })
            {
                const Mathematics::Axis3D Rel = Vtx - Cl.RepresentativePos;
                const float PU = Mathematics::EvaluateDot(Rel, TanU);
                const float PV = Mathematics::EvaluateDot(Rel, BitV);
                MinU = std::min(MinU, PU);
                MaxU = std::max(MaxU, PU);
                MinV = std::min(MinV, PV);
                MaxV = std::max(MaxV, PV);
            }
        }
        const float HalfU = 0.52f * (MaxU - MinU) * 0.5f;
        const float HalfV = 0.52f * (MaxV - MinV) * 0.5f;
        Cl.BoundingRadius = std::max(Cl.BoundingRadius, std::hypot(HalfU, HalfV));
        Cl.SamplePosA     = Cl.RepresentativePos;
        Cl.SamplePosB     = Cl.RepresentativePos - TanU * HalfU - BitV * (HalfV * 0.65f);
        Cl.SamplePosC     = Cl.RepresentativePos + TanU * HalfU + BitV * (HalfV * 0.65f);
    }

    if (DiffuseCandidates.empty() || TotalDiffuseArea <= 1.0e-5)
    {
        return;
    }

    // Stratified area-proportional diffuse patch generation via Owen-scrambled Sobol (0,2)
    const uint32_t Budget = std::max(24u, Settings.TargetPatchBudget);
    BouncePatches.reserve(Budget + 16u);
    const double AreaPerPatch = TotalDiffuseArea / static_cast<double>(Budget);
    double Accumulator        = AreaPerPatch * 0.5;
    uint32_t PatchSeed        = 1u;

    for (const auto& Cand : DiffuseCandidates)
    {
        const auto& Prim = Primitives[Cand.PrimIndex];
        Accumulator += static_cast<double>(Cand.Area);
        uint32_t SpawnCount = 0u;
        while (Accumulator >= AreaPerPatch)
        {
            Accumulator -= AreaPerPatch;
            ++SpawnCount;
        }
        if (SpawnCount == 0u)
        {
            continue;
        }

        const float SubArea = Cand.Area / static_cast<float>(SpawnCount);
        for (uint32_t S = 0u; S < SpawnCount; ++S)
        {
            float U = 0.0f, V = 0.0f;
            SampleOwenSobol02(S, Cand.PrimIndex * 747796405u + PatchSeed * 2891336453u, U, V);
            ++PatchSeed;
            // Uniform triangle fold preserving (0,2) stratification
            const float RootU = std::sqrt(std::clamp(U, 0.0f, 1.0f));
            const float Bary1 = 1.0f - RootU;
            const float Bary2 = V * RootU;
            DiffuseBouncePatch Patch {};
            Patch.Position = Prim.Corner0 + Prim.Edge01 * Bary1 + Prim.Edge02 * Bary2;
            Patch.Normal   = Cand.Normal;
            Patch.Albedo   = Cand.Albedo;
            Patch.Area     = SubArea;
            BouncePatches.push_back(Patch);
        }
    }

    // Bounce 1: Illuminate every diffuse patch via closed-form Stokes luminaire contour + AreaLights + solar shadow
    DirectLighting::HybridConeShadowSetup SunSetup {};
    for (auto& Patch : BouncePatches)
    {
        Mathematics::LinearRGB DirectIrrad = EvaluateDirectEmissiveIrradiance(
            Patch.Position, Patch.Normal, Occluders, Settings);

        if (!Settings.AreaLights.empty())
        {
            DirectIrrad = DirectIrrad + EvaluateAreaLightPatchIrradiance(
                Patch.Position, Patch.Normal, Settings.AreaLights, Occluders, Settings);
        }

        if (EvaluatePeakChannel(Settings.SunRadiance) > 1.0e-3f)
        {
            const float NdotL = std::max(0.0f, Mathematics::EvaluateDot(Patch.Normal, Settings.SunDirection));
            if (NdotL > 0.0f)
            {
                const float SunVis = Settings.HighEndZeroSdfTier
                    ? ((Occluders.SceneBvh != nullptr)
                        ? DirectLighting::EvaluateHighEndBvhDirectionalShadow(
                            *Occluders.SceneBvh, Patch.Position, Patch.Normal, Settings.SunDirection, 17u, SunSetup)
                        : 1.0f)
                    : DirectLighting::EvaluateGtxHybridDirectionalShadow(
                        Occluders, Patch.Position, Patch.Normal, Settings.SunDirection, SunSetup);
                DirectIrrad = DirectIrrad + Settings.SunRadiance * (NdotL * SunVis);
            }
        }

        Patch.ExitantFlux = {
            Patch.Albedo.R * DirectIrrad.R,
            Patch.Albedo.G * DirectIrrad.G,
            Patch.Albedo.B * DirectIrrad.B,
        };
    }

    // Bounce 2: Single visibility-gated Jacobi radiosity relaxation across virtual patches so secondary color bleed respects walls & columns
    std::vector<Mathematics::LinearRGB> SecondBounceFlux(BouncePatches.size(), { 0.0f, 0.0f, 0.0f });
    for (std::size_t I = 0u; I < BouncePatches.size(); ++I)
    {
        Mathematics::LinearRGB GatherIrrad { 0.0f, 0.0f, 0.0f };
        const auto& Dst = BouncePatches[I];
        for (std::size_t J = 0u; J < BouncePatches.size(); ++J)
        {
            if (I == J) continue;
            const auto& Src = BouncePatches[J];
            if (EvaluatePeakChannel(Src.ExitantFlux) <= 1.0e-4f) continue;

            const Mathematics::Axis3D Delta = Src.Position - Dst.Position;
            const float DistSq = Mathematics::EvaluateLengthSquared(Delta);
            if (DistSq <= 0.09f) continue;
            const float InvDist = 1.0f / std::sqrt(DistSq);
            const Mathematics::Axis3D Dir = Delta * InvDist;
            const float CosDst = Mathematics::EvaluateDot(Dst.Normal, Dir);
            const float CosSrc = Mathematics::EvaluateDot(Src.Normal, Dir * -1.0f);
            if (CosDst <= 0.02f || CosSrc <= 0.02f) continue;

            const float PatchRad = std::sqrt(Src.Area * (1.0f / Constants::PiFloat));
            const float Vis      = EvaluateSegmentVisibility(
                Dst.Position, Dst.Normal, Src.Position, PatchRad, Occluders, Settings);
            if (Vis <= 1.0e-4f) continue;

            const float Denom      = Constants::PiFloat * DistSq + std::max(Src.Area * 2.2f, 0.38f);
            const float FormFactor = std::min(0.14f, (CosDst * CosSrc * Src.Area) / Denom) * Vis;
            GatherIrrad = GatherIrrad + Src.ExitantFlux * FormFactor;
        }
        SecondBounceFlux[I] = {
            Dst.ExitantFlux.R + Dst.Albedo.R * GatherIrrad.R,
            Dst.ExitantFlux.G + Dst.Albedo.G * GatherIrrad.G,
            Dst.ExitantFlux.B + Dst.Albedo.B * GatherIrrad.B,
        };
    }
    for (std::size_t I = 0u; I < BouncePatches.size(); ++I)
    {
        BouncePatches[I].ExitantFlux = SecondBounceFlux[I];
    }
}

Mathematics::LinearRGB StokesContourIrradianceSequence::EvaluateDirectEmissiveIrradiance(
    Mathematics::Axis3D                              ShadingPos,
    Mathematics::Axis3D                              ShadingNormal,
    const DirectLighting::HybridOccluderEnvironment& Occluders,
    const StokesContourTierSettings&                 Settings) const noexcept
{
    if (EmissiveTriangles.empty())
    {
        return {};
    }

    std::array<float, 32u> ClusterVis {};
    std::array<bool,  32u> ClusterEvaluated {};

    Mathematics::LinearRGB Irrad { 0.0f, 0.0f, 0.0f };
    for (const auto& Emit : EmissiveTriangles)
    {
        const float Form = EvaluateClippedStokesTriangleFormFactor(
            ShadingPos, ShadingNormal, Emit.CornerA, Emit.CornerB, Emit.CornerC, Emit.OutwardNormal);
        if (Form <= 1.0e-6f)
        {
            continue;
        }

        float Vis = 1.0f;
        if (Settings.HighEndZeroSdfTier && Occluders.SceneBvh != nullptr && EmissiveTriangles.size() <= 160u)
        {
            const Mathematics::Axis3D TargetPt = Emit.Centroid + Emit.OutwardNormal * 0.015f;
            Vis = DirectLighting::EvaluateHighEndBvhLuminaireVisibility(
                *Occluders.SceneBvh, ShadingPos, ShadingNormal, TargetPt);
        }
        else if (Emit.ClusterId < 32u && Emit.ClusterId < Clusters.size())
        {
            if (!ClusterEvaluated[Emit.ClusterId])
            {
                const auto& Cl = Clusters[Emit.ClusterId];
                ClusterVis[Emit.ClusterId] = EvaluateClusterVisibility(
                    Cl, ShadingPos, ShadingNormal, Occluders, Settings);
                ClusterEvaluated[Emit.ClusterId] = true;
            }
            Vis = ClusterVis[Emit.ClusterId];
        }
        if (Vis <= 1.0e-4f)
        {
            continue;
        }

        // Exact Lambertian diffuse irradiance from uniform emitter radiance L is E = Pi * L * FormFactor
        Irrad = Irrad + Emit.Radiance * (Constants::PiFloat * Form * Vis);
    }

    return Irrad;
}

Mathematics::LinearRGB StokesContourIrradianceSequence::EvaluateIndirectBounceIrradiance(
    Mathematics::Axis3D                              ShadingPos,
    Mathematics::Axis3D                              ShadingNormal,
    const DirectLighting::HybridOccluderEnvironment& Occluders,
    const StokesContourTierSettings&                 Settings) const noexcept
{
    Mathematics::LinearRGB BounceIrrad { 0.0f, 0.0f, 0.0f };
    for (const auto& Patch : BouncePatches)
    {
        if (EvaluatePeakChannel(Patch.ExitantFlux) <= 1.0e-4f)
        {
            continue;
        }
        const Mathematics::Axis3D Delta = Patch.Position - ShadingPos;
        const float DistSq = Mathematics::EvaluateLengthSquared(Delta);
        if (DistSq <= 0.04f)
        {
            continue;
        }
        const float Dist    = std::sqrt(DistSq);
        const auto  Dir     = Delta * (1.0f / Dist);
        const float CosRecv = Mathematics::EvaluateDot(ShadingNormal, Dir);
        const float CosSend = Mathematics::EvaluateDot(Patch.Normal, Dir * -1.0f);
        if (CosRecv <= 1.0e-3f || CosSend <= 1.0e-3f)
        {
            continue;
        }

        const float PatchRadius = std::sqrt(Patch.Area * (1.0f / Constants::PiFloat));
        const float Vis         = EvaluateSegmentVisibility(
            ShadingPos, ShadingNormal, Patch.Position, PatchRadius, Occluders, Settings);
        if (Vis <= 1.0e-4f)
        {
            continue;
        }

        const float Denom = Constants::PiFloat * DistSq + std::max(Patch.Area * 2.2f, 0.38f);
        const float Form  = std::min(0.18f, (CosRecv * CosSend * Patch.Area) / Denom);
        BounceIrrad = BounceIrrad + Patch.ExitantFlux * (Form * Vis);
    }
    return BounceIrrad;
}

Mathematics::LinearRGB StokesContourIrradianceSequence::EvaluateTotalIrradiance(
    Mathematics::Axis3D                              ShadingPos,
    Mathematics::Axis3D                              ShadingNormal,
    const DirectLighting::HybridOccluderEnvironment& Occluders,
    const StokesContourTierSettings&                 Settings) const noexcept
{
    // DirectLighting skips PorosityLight meshes when Order2Active == true so Order 2 evaluates exact Stokes luminaire contour + multi-bounce!
    return EvaluateDirectEmissiveIrradiance(ShadingPos, ShadingNormal, Occluders, Settings)
         + EvaluateIndirectBounceIrradiance(ShadingPos, ShadingNormal, Occluders, Settings);
}

} // namespace Frontier::GeometricRaster::GlobalIllumination
