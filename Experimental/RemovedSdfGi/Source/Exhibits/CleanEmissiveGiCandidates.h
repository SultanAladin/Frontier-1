/*============================================================================================================================================
                                                     CLEANEMISSIVEGICANDIDATES.H
============================================================================================================================================*/
// 🧩 Three Denoiser-Free Clean Emissive Global Illumination Architectures (Applicable to Both DDGI & Normal GI):
//    1. StokesContourEmissiveGi  — Analytic Spherical-Polygon Stokes Contour Integration + Hybrid SDF Soft Penumbra + Smooth 2nd Bounce
//    2. SurfelCascadeEmissiveGi  — Two-Tier Area-Stratified Virtual Disk Radiosity + 2-Bounce Surfel Bleed + Adaptive Lattice
//    3. ContinuousFieldEmissiveGi — Field-Native Gaussian-Splatted Directional L1 Clipmap (64 Cones) & Veach-MIS Hermite DDGI Lattice

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/DistanceFieldSpace/VoxelSdfClipmap.h"
#include "Engine/GeometricRaster/GlobalIllumination/DeterministicIntegrators.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/MaterialSpace/Scattering/BlackbodyEmission.h"
#include "Engine/MaterialSpace/Scattering/OpenPbrSurfaceScattering.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <thread>
#include <vector>

namespace Frontier::Exhibits
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   SHARED MATH & SDF UTILITIES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline float ClampUnit(float X) noexcept
{
    return std::clamp(X, 0.0f, 1.0f);
}

[[nodiscard]] inline float SmoothHermite(float Edge0, float Edge1, float X) noexcept
{
    const float T = std::clamp((X - Edge0) / std::max(1.0e-6f, Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] inline float SmoothCubicScalar(float U) noexcept
{
    const float T = std::clamp(U, 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] inline float ComputeLinearLuminance(const Mathematics::LinearRGB& C) noexcept
{
    return std::max(0.0f, 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B);
}

inline void ConstructTangentPair(
    const Mathematics::Axis3D& Normal,
    Mathematics::Axis3D&       OutTangent,
    Mathematics::Axis3D&       OutBitangent) noexcept
{
    const float SignZ = (Normal.Z >= 0.0f) ? 1.0f : -1.0f;
    const float A     = -1.0f / (SignZ + Normal.Z);
    const float B     = Normal.X * Normal.Y * A;
    OutTangent   = { 1.0f + SignZ * Normal.X * Normal.X * A, SignZ * B, -SignZ * Normal.X };
    OutBitangent = { B, SignZ + Normal.Y * Normal.Y * A, -Normal.Y };
}

template <typename CallableFn>
inline void RunParallelScanlines(uint32_t RowCount, CallableFn&& Fn)
{
    const uint32_t Workers = std::min(RowCount, std::max(1u, std::thread::hardware_concurrency()));
    if (Workers <= 1u || RowCount <= 2u)
    {
        Fn(0u, RowCount);
        return;
    }
    std::vector<std::thread> Threads;
    Threads.reserve(Workers);
    const uint32_t Chunk = (RowCount + Workers - 1u) / Workers;
    for (uint32_t W = 0u; W < Workers; ++W)
    {
        const uint32_t Begin = W * Chunk;
        const uint32_t End   = std::min(RowCount, Begin + Chunk);
        if (Begin < End)
        {
            Threads.emplace_back([&, Begin, End]() { Fn(Begin, End); });
        }
    }
    for (auto& ThreadHandle : Threads)
    {
        ThreadHandle.join();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                         SCENE SDF & EMISSIVE SOURCE COLLECTION
//------------------------------------------------------------------------------------------------------------------------

struct EmissiveTriangleSource
{
    Mathematics::Axis3D    VertexA       {};     // [m]     - World vertex A
    Mathematics::Axis3D    VertexB       {};     // [m]     - World vertex B
    Mathematics::Axis3D    VertexC       {};     // [m]     - World vertex C
    Mathematics::Axis3D    Centroid      {};     // [m]     - Triangle centroid
    Mathematics::Axis3D    UnitNormal    {};     // [-]     - Outward unit normal
        Mathematics::LinearRGB Radiance      {};     // [cd/m2] - Physical linear radiated light
    float                  AreaM2        = 0.0f; // [m2]    - Triangle area
    float                  CdfUpper      = 0.0f; // [0..1]  - Cumulative flux probability
    uint32_t               ClusterIndex  = 0u;   // [-]     - Spatial cluster grouping index
};

struct EmissiveClusterSummary
{
    Mathematics::Axis3D    CenterWorld   {};     // [m]     - Flux-weighted centroid
    Mathematics::Axis3D    MeanNormal    {};     // [-]     - Normalized flux-weighted normal
        Mathematics::LinearRGB MeanRadiance  {};     // [cd/m2] - Mean radiated light
    float                  BoundRadiusM  = 0.1f; // [m]     - Bounding radius
    float                  TotalAreaM2   = 0.0f; // [m2]    - Total emissive area
    uint32_t               FirstTriIdx   = 0u;   // [-]     - Start offset in sorted array
    uint32_t               TriCount      = 0u;   // [-]     - Triangle count in cluster
};

struct SceneGeometryBridge
{
    std::vector<DistanceFieldSpace::ClosedBox>                 StaticBoxes      {};
    std::span<const DistanceFieldSpace::DynamicLocalSdfInstance> DynamicInstances {};
    const SpatialAcceleration::WideBoundingTree*               SceneTree        = nullptr;
    const MaterialSpace::CompactMaterialRegister*              Materials        = nullptr;
    std::vector<EmissiveTriangleSource>                        Emissives        {};
    std::vector<EmissiveClusterSummary>                        Clusters         {};
    double                                                     TotalFlux        = 0.0;

    [[nodiscard]] float EvaluateSignedBoxDistance(const Mathematics::Axis3D& P, const DistanceFieldSpace::ClosedBox& B) const noexcept
    {
        const float DX = std::fabs(P.X - B.Centre.X) - B.HalfExtent.X;
        const float DY = std::fabs(P.Y - B.Centre.Y) - B.HalfExtent.Y;
        const float DZ = std::fabs(P.Z - B.Centre.Z) - B.HalfExtent.Z;
        const float OX = std::max(DX, 0.0f);
        const float OY = std::max(DY, 0.0f);
        const float OZ = std::max(DZ, 0.0f);
        const float OutD = std::sqrt(OX * OX + OY * OY + OZ * OZ);
        const float InD  = std::min(std::max({ DX, DY, DZ }), 0.0f);
        return OutD + InD;
    }

    // C0-continuous distance to dynamic mesh SDF outside [LocalMin, LocalMax] via Euclidean clamp + boundary SDF
    [[nodiscard]] static float SampleDynamicAssetSmoothDistance(
        const DistanceFieldSpace::DynamicLocalSdfInstance& Dyn,
        const Mathematics::Axis3D&                         P) noexcept
    {
        const auto LocalP = Dyn.Transform.TransformPoint(Dyn.Transform.WorldToLocal, P);
        const auto* Asset = Dyn.Asset;
        const float MX = std::max(0.0f, std::max(Asset->LocalMin.X - LocalP.X, LocalP.X - Asset->LocalMax.X));
        const float MY = std::max(0.0f, std::max(Asset->LocalMin.Y - LocalP.Y, LocalP.Y - Asset->LocalMax.Y));
        const float MZ = std::max(0.0f, std::max(Asset->LocalMin.Z - LocalP.Z, LocalP.Z - Asset->LocalMax.Z));
        const float BoxD = std::sqrt(MX * MX + MY * MY + MZ * MZ);
        const Mathematics::Axis3D ClampedLocal {
            std::clamp(LocalP.X, Asset->LocalMin.X, Asset->LocalMax.X),
            std::clamp(LocalP.Y, Asset->LocalMin.Y, Asset->LocalMax.Y),
            std::clamp(LocalP.Z, Asset->LocalMin.Z, Asset->LocalMax.Z),
        };
        const float BoundaryD = Asset->SampleConservativeLocalDistance(ClampedLocal);
        return (BoxD > 0.0f) ? std::hypot(BoundaryD, BoxD) : BoundaryD;
    }

    // Smooth unsigned/signed minimum scene distance for soft penumbra sphere-tracing
    [[nodiscard]] float SampleSceneDistance(const Mathematics::Axis3D& P) const noexcept
    {
        float MinD = 1.0e5f;
        for (const auto& Box : StaticBoxes)
        {
            const float SD = EvaluateSignedBoxDistance(P, Box);
            MinD = std::min(MinD, std::fabs(SD));
        }
        for (const auto& Dyn : DynamicInstances)
        {
            if (!Dyn.Enabled || Dyn.Asset == nullptr)
            {
                continue;
            }
            MinD = std::min(MinD, SampleDynamicAssetSmoothDistance(Dyn, P));
        }
        return MinD;
    }

    // Interior occluder SDF (excludes studio boundary floor/outer walls > 7 m wide so floor & walls never self-shadow)
    [[nodiscard]] float SampleInteriorOccluderDistance(const Mathematics::Axis3D& P) const noexcept
    {
        float MinD = 20.0f;
        for (const auto& B : StaticBoxes)
        {
            if (B.HalfExtent.X > 3.5f || B.HalfExtent.Y > 3.5f)
            {
                continue;
            }
            MinD = std::min(MinD, EvaluateSignedBoxDistance(P, B));
        }
        for (const auto& Dyn : DynamicInstances)
        {
            if (!Dyn.Enabled || Dyn.Asset == nullptr)
            {
                continue;
            }
            MinD = std::min(MinD, SampleDynamicAssetSmoothDistance(Dyn, P));
        }
        return MinD;
    }

    // Centered tetrahedron SDF gradient for lattice cell relocation out of geometry interiors
    [[nodiscard]] Mathematics::Axis3D EvaluateDistanceGradient(const Mathematics::Axis3D& P, float StepH = 0.05f) const noexcept
    {
        const float D0 = SampleSceneDistance({ P.X + StepH, P.Y - StepH, P.Z - StepH });
        const float D1 = SampleSceneDistance({ P.X - StepH, P.Y - StepH, P.Z + StepH });
        const float D2 = SampleSceneDistance({ P.X - StepH, P.Y + StepH, P.Z - StepH });
        const float D3 = SampleSceneDistance({ P.X + StepH, P.Y + StepH, P.Z + StepH });
        return Mathematics::NormalizeAxis({
             D0 - D1 - D2 + D3,
            -D0 - D1 + D2 + D3,
            -D0 + D1 - D2 + D3,
        }, { 0.0f, 0.0f, 1.0f });
    }

    // Smooth ring-free SDF cone visibility from ReceiverP toward TargetP:
    // uniform parametric steps + interior occluder SDF eliminate floor/wall self-shadowing & concentric step bands
    [[nodiscard]] float EvaluateSoftConeVisibility(
        const Mathematics::Axis3D& ReceiverP,
        const Mathematics::Axis3D& ReceiverN,
        const Mathematics::Axis3D& TargetP,
        float                      TargetRadiusM,
        float                      SelfGuardM = 0.20f) const noexcept
    {
        const Mathematics::Axis3D Delta = TargetP - ReceiverP;
        const float FullDist = Mathematics::EvaluateLength(Delta);
        if (FullDist <= 0.10f)
        {
            return 1.0f;
        }
        const Mathematics::Axis3D Dir = Delta * (1.0f / FullDist);
        if (Mathematics::EvaluateDot(ReceiverN, Dir) <= 1.0e-4f)
        {
            return 0.0f;
        }

        const float LuminaireMargin = std::max(SelfGuardM, TargetRadiusM * 0.65f);
        const float StartT          = 0.12f;
        const float EndT            = FullDist - LuminaireMargin;
        if (EndT <= StartT)
        {
            return 1.0f;
        }

        constexpr uint32_t StepCount = 32u;
        constexpr float    InvSteps  = 1.0f / static_cast<float>(StepCount);
        const float        StepDt    = (EndT - StartT) * InvSteps;
        const float        AngRadius = std::clamp(TargetRadiusM / std::max(0.5f, FullDist), 0.025f, 0.075f);

        // Smooth integral transmittance over uniform steps eliminates discrete step-exchange floor moire completely
        float OpticalDepth = 0.0f;
        for (uint32_t Step = 0u; Step < StepCount; ++Step)
        {
            const float U = (static_cast<float>(Step) + 0.5f) * InvSteps;
            const float T = StartT + (EndT - StartT) * U;
            const Mathematics::Axis3D SampleP = ReceiverP + Dir * T;
            const float PlaneFade = SmoothHermite(0.02f, 0.16f, Mathematics::EvaluateDot(SampleP - ReceiverP, ReceiverN));
            if (PlaneFade <= 1.0e-4f)
            {
                continue;
            }
            const float RampIn   = SmoothHermite(0.10f, 0.26f, T);
            const float RampOut  = 1.0f - SmoothHermite(EndT - 0.14f, EndT, T);
            const float W        = RampIn * RampOut * PlaneFade;
            if (W <= 1.0e-4f)
            {
                continue;
            }

            const float Dist    = SampleInteriorOccluderDistance(SampleP);
            const float ConeR   = std::max(0.036f, T * AngRadius);
            const float StepVis = SmoothHermite(-0.025f, ConeR * 0.72f, Dist + 0.015f);
            const float Occ     = (1.0f - StepVis) * W;
            OpticalDepth += 2.6f * Occ * (StepDt / std::max(0.08f, ConeR));
            if (OpticalDepth > 6.5f)
            {
                return 0.0f;
            }
        }
        return std::exp(-OpticalDepth);
    }

    // Hybrid short-range exact BVH + soft uniform-step far-field SDF visibility for crisp tyre/pillar contact shadows
    [[nodiscard]] float EvaluateHybridConeVisibility(
        const Mathematics::Axis3D& ReceiverP,
        const Mathematics::Axis3D& ReceiverN,
        const Mathematics::Axis3D& TargetP,
        float                      TargetRadiusM,
        float                      SelfGuardM = 0.20f) const noexcept
    {
        const Mathematics::Axis3D Delta = TargetP - ReceiverP;
        const float FullDist = Mathematics::EvaluateLength(Delta);
        if (FullDist <= 0.06f)
        {
            return 1.0f;
        }
        const Mathematics::Axis3D Dir = Delta * (1.0f / FullDist);
        if (Mathematics::EvaluateDot(ReceiverN, Dir) <= 1.0e-4f)
        {
            return 0.0f;
        }

        float SoftVis = EvaluateSoftConeVisibility(ReceiverP, ReceiverN, TargetP, TargetRadiusM, SelfGuardM);
        if (SoftVis <= 0.004f || SceneTree == nullptr)
        {
            return SoftVis;
        }

        const float ContactLimit = std::min(0.28f, std::max(0.01f, FullDist - SelfGuardM));
        if (ContactLimit > 0.02f)
        {
            SpatialAcceleration::RaySegmentQuery ContactRay {};
            ContactRay.Origin          = ReceiverP + ReceiverN * 0.004f;
            ContactRay.UnitDirection   = Dir;
            ContactRay.MinimumDistance = 0.002f;
            ContactRay.MaximumDistance = ContactLimit;
            if (SceneTree->QueryRayOccluded(ContactRay))
            {
                return 0.0f;
            }
        }
        return SoftVis;
    }
};

[[nodiscard]] inline SceneGeometryBridge ConstructSceneGeometryBridge(
    const GeometricRaster::WorldSceneSpecification&             Scene,
    std::span<const DistanceFieldSpace::ClosedBox>              StaticBoxes)
{
    SceneGeometryBridge Bridge {};
    Bridge.StaticBoxes.assign(StaticBoxes.begin(), StaticBoxes.end());
    Bridge.DynamicInstances = Scene.DynamicSdfObjects;
    Bridge.SceneTree        = &Scene.SceneTree;
    Bridge.Materials      = &Scene.MaterialRegister;

    const uint32_t MatCount = Scene.MaterialRegister.QuerySlotCount();
    const auto& Prims = Scene.SceneTree.Primitives;
    for (const auto& Tri : Prims)
    {
        if (Tri.MaterialOrdinal >= MatCount)
        {
            continue;
        }
        const auto& Mat = Scene.MaterialRegister.InspectAuthoredMaterial(Tri.MaterialOrdinal);
        const auto Em   = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);
        const float EmLum = ComputeLinearLuminance(Em);
        if (EmLum <= 1.0e-4f)
        {
            continue;
        }

        const Mathematics::Axis3D CrossN = Mathematics::EvaluateCross(Tri.Edge01, Tri.Edge02);
        const float TwiceArea = Mathematics::EvaluateLength(CrossN);
        if (TwiceArea <= 1.0e-7f)
        {
            continue;
        }
        EmissiveTriangleSource Src {};
        Src.VertexA    = Tri.Corner0;
        Src.VertexB    = Tri.Corner0 + Tri.Edge01;
        Src.VertexC    = Tri.Corner0 + Tri.Edge02;
        Src.Centroid   = (Src.VertexA + Src.VertexB + Src.VertexC) * (1.0f / 3.0f);
        Src.UnitNormal = CrossN * (1.0f / TwiceArea);
        Src.Radiance   = Em;
        Src.AreaM2     = 0.5f * TwiceArea;
        Bridge.TotalFlux += static_cast<double>(EmLum) * static_cast<double>(Src.AreaM2);
        Bridge.Emissives.push_back(Src);
    }

    // Partition emissive triangles into spatial+normal clusters (Front Left/Center/Right, Rear Left/Center/Right)
    // Sorting cleanly by luminaire sign + lateral Y keeps clusters ordered deterministically
    std::sort(Bridge.Emissives.begin(), Bridge.Emissives.end(),
        [](const EmissiveTriangleSource& A, const EmissiveTriangleSource& B)
        {
            const bool FrontA = (A.UnitNormal.X >= 0.0f);
            const bool FrontB = (B.UnitNormal.X >= 0.0f);
            if (FrontA != FrontB) return FrontA;
            return A.Centroid.Y < B.Centroid.Y;
        });

    double Running = 0.0;
    const double InvFlux = (Bridge.TotalFlux > 1.0e-12) ? (1.0 / Bridge.TotalFlux) : 1.0;
    for (auto& E : Bridge.Emissives)
    {
        Running += static_cast<double>(ComputeLinearLuminance(E.Radiance)) * static_cast<double>(E.AreaM2);
        E.CdfUpper = static_cast<float>(std::clamp(Running * InvFlux, 0.0, 1.0));
    }

    // Group into up to 6 coherent spatial-normal clusters (front 3 lateral bands + rear 3 lateral bands)
    if (!Bridge.Emissives.empty())
    {
        std::array<std::vector<uint32_t>, 6u> Buckets {};
        Mathematics::Axis3D MinP { 1e9f, 1e9f, 1e9f }, MaxP { -1e9f, -1e9f, -1e9f };
        for (const auto& E : Bridge.Emissives)
        {
            MinP.Y = std::min(MinP.Y, E.Centroid.Y);
            MaxP.Y = std::max(MaxP.Y, E.Centroid.Y);
        }
        const float SpanY = std::max(0.05f, MaxP.Y - MinP.Y);
        for (uint32_t Idx = 0u; Idx < static_cast<uint32_t>(Bridge.Emissives.size()); ++Idx)
        {
            const auto& E = Bridge.Emissives[Idx];
            const uint32_t SideIdx = (E.UnitNormal.X >= 0.0f) ? 0u : 3u;
            const float NormY = ClampUnit((E.Centroid.Y - MinP.Y) / SpanY);
            const uint32_t BandIdx = std::min(2u, static_cast<uint32_t>(NormY * 3.0f));
            const uint32_t CID = SideIdx + BandIdx;
            Buckets[CID].push_back(Idx);
        }

        std::vector<EmissiveTriangleSource> Reordered {};
        Reordered.reserve(Bridge.Emissives.size());
        for (uint32_t CID = 0u; CID < 6u; ++CID)
        {
            if (Buckets[CID].empty())
            {
                continue;
            }
            EmissiveClusterSummary Sum {};
            Sum.FirstTriIdx = static_cast<uint32_t>(Reordered.size());
            Sum.TriCount    = static_cast<uint32_t>(Buckets[CID].size());
            Mathematics::Axis3D WeightedP {};
            Mathematics::Axis3D WeightedN {};
            Mathematics::LinearRGB WeightedR {};
            float TotalW = 0.0f;

            for (uint32_t TriIdx : Buckets[CID])
            {
                EmissiveTriangleSource T = Bridge.Emissives[TriIdx];
                T.ClusterIndex = static_cast<uint32_t>(Bridge.Clusters.size());
                Reordered.push_back(T);
                const float W = T.AreaM2 * ComputeLinearLuminance(T.Radiance);
                WeightedP = WeightedP + T.Centroid * W;
                WeightedN = WeightedN + T.UnitNormal * W;
                WeightedR = WeightedR + T.Radiance * T.AreaM2;
                Sum.TotalAreaM2 += T.AreaM2;
                TotalW += W;
            }
            if (TotalW > 1.0e-7f)
            {
                Sum.CenterWorld = WeightedP * (1.0f / TotalW);
                Sum.MeanNormal  = Mathematics::NormalizeAxis(WeightedN, { 1.0f, 0.0f, 0.0f });
                Sum.MeanRadiance = WeightedR * (1.0f / std::max(1.0e-7f, Sum.TotalAreaM2));
            }
            float MaxR2 = 0.01f;
            for (uint32_t K = Sum.FirstTriIdx; K < Sum.FirstTriIdx + Sum.TriCount; ++K)
            {
                for (const auto& V : { Reordered[K].VertexA, Reordered[K].VertexB, Reordered[K].VertexC })
                {
                    MaxR2 = std::max(MaxR2, Mathematics::EvaluateLengthSquared(V - Sum.CenterWorld));
                }
            }
            Sum.BoundRadiusM = std::sqrt(MaxR2);
            Bridge.Clusters.push_back(Sum);
        }
        Bridge.Emissives.swap(Reordered);

        // Rebuild monotone CDF in reordered order
        double Acc = 0.0;
        for (auto& E : Bridge.Emissives)
        {
            Acc += static_cast<double>(ComputeLinearLuminance(E.Radiance)) * static_cast<double>(E.AreaM2);
            E.CdfUpper = static_cast<float>(std::clamp(Acc * InvFlux, 0.0, 1.0));
        }
    }

    return Bridge;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  SHARED LATTICE STRUCTURE (ADAPTIVE DUAL-CASCADE DDGI)
//------------------------------------------------------------------------------------------------------------------------

struct CleanOctahedralCell
{
    static constexpr uint32_t IrrWidth   = 16u;
    static constexpr uint32_t DepthWidth = 16u;

    Mathematics::Axis3D WorldPosition {};
    std::array<Mathematics::LinearRGB, IrrWidth * IrrWidth>     IrradianceOverPi {};
    std::array<Mathematics::Axis2D, DepthWidth * DepthWidth>    DepthMoments     {};
    float ActiveValidity = 1.0f;
};

struct CleanLatticeCascade
{
    Mathematics::Axis3D GridMin     {};
    Mathematics::Axis3D GridMax     {};
    Mathematics::Axis3D Spacing     {};
    uint32_t            CountX      = 2u;
    uint32_t            CountY      = 2u;
    uint32_t            CountZ      = 2u;
    std::vector<CleanOctahedralCell> Cells {};

    [[nodiscard]] static Mathematics::Axis2D EncodeOctahedral(Mathematics::Axis3D D) noexcept
    {
        const float InvL1 = 1.0f / std::max(1.0e-6f, std::fabs(D.X) + std::fabs(D.Y) + std::fabs(D.Z));
        float OX = D.X * InvL1;
        float OY = D.Y * InvL1;
        if (D.Z < 0.0f)
        {
            const float WX = (1.0f - std::fabs(OY)) * ((OX >= 0.0f) ? 1.0f : -1.0f);
            const float WY = (1.0f - std::fabs(OX)) * ((OY >= 0.0f) ? 1.0f : -1.0f);
            OX = WX;
            OY = WY;
        }
        return { OX * 0.5f + 0.5f, OY * 0.5f + 0.5f };
    }

    [[nodiscard]] static Mathematics::Axis3D DecodeOctahedral(float U, float V) noexcept
    {
        const float OX = U * 2.0f - 1.0f;
        const float OY = V * 2.0f - 1.0f;
        Mathematics::Axis3D D { OX, OY, 1.0f - std::fabs(OX) - std::fabs(OY) };
        if (D.Z < 0.0f)
        {
            const float FX = (1.0f - std::fabs(D.Y)) * ((D.X >= 0.0f) ? 1.0f : -1.0f);
            const float FY = (1.0f - std::fabs(D.X)) * ((D.Y >= 0.0f) ? 1.0f : -1.0f);
            D.X = FX;
            D.Y = FY;
        }
        return Mathematics::NormalizeAxis(D, { 0.0f, 0.0f, 1.0f });
    }

    template <typename ElemT, uint32_t Width>
    [[nodiscard]] static ElemT SampleOctahedral(const std::array<ElemT, Width * Width>& Sheet, Mathematics::Axis3D Dir) noexcept
    {
        const auto Uv = EncodeOctahedral(Dir);
        const float FX = std::clamp(Uv.X * static_cast<float>(Width) - 0.5f, 0.0f, static_cast<float>(Width - 1u));
        const float FY = std::clamp(Uv.Y * static_cast<float>(Width) - 0.5f, 0.0f, static_cast<float>(Width - 1u));
        const auto X0  = static_cast<uint32_t>(std::floor(FX));
        const auto Y0  = static_cast<uint32_t>(std::floor(FY));
        const uint32_t X1 = std::min(Width - 1u, X0 + 1u);
        const uint32_t Y1 = std::min(Width - 1u, Y0 + 1u);
        const float TX = FX - static_cast<float>(X0);
        const float TY = FY - static_cast<float>(Y0);
        const ElemT& C00 = Sheet[Y0 * Width + X0];
        const ElemT& C10 = Sheet[Y0 * Width + X1];
        const ElemT& C01 = Sheet[Y1 * Width + X0];
        const ElemT& C11 = Sheet[Y1 * Width + X1];
        return (C00 * (1.0f - TX) + C10 * TX) * (1.0f - TY) + (C01 * (1.0f - TX) + C11 * TX) * TY;
    }

    void InitializeGrid(
        const Mathematics::Axis3D& MinWorld,
        const Mathematics::Axis3D& MaxWorld,
        uint32_t                   CX,
        uint32_t                   CY,
        uint32_t                   CZ,
        const SceneGeometryBridge& Bridge,
        bool                       RelocateOutOfGeometry)
    {
        GridMin = MinWorld;
        GridMax = MaxWorld;
        CountX  = std::max(2u, CX);
        CountY  = std::max(2u, CY);
        CountZ  = std::max(2u, CZ);
        Spacing = {
            std::max(0.05f, (GridMax.X - GridMin.X) / static_cast<float>(CountX - 1u)),
            std::max(0.05f, (GridMax.Y - GridMin.Y) / static_cast<float>(CountY - 1u)),
            std::max(0.05f, (GridMax.Z - GridMin.Z) / static_cast<float>(CountZ - 1u)),
        };
        Cells.assign(static_cast<std::size_t>(CountX) * CountY * CountZ, CleanOctahedralCell{});

        for (uint32_t IZ = 0u; IZ < CountZ; ++IZ)
        {
            for (uint32_t IY = 0u; IY < CountY; ++IY)
            {
                for (uint32_t IX = 0u; IX < CountX; ++IX)
                {
                    const std::size_t Idx = (static_cast<std::size_t>(IZ) * CountY + IY) * CountX + IX;
                    Mathematics::Axis3D P {
                        GridMin.X + static_cast<float>(IX) * Spacing.X,
                        GridMin.Y + static_cast<float>(IY) * Spacing.Y,
                        GridMin.Z + static_cast<float>(IZ) * Spacing.Z,
                    };
                    if (RelocateOutOfGeometry)
                    {
                        for (uint32_t Iter = 0u; Iter < 4u; ++Iter)
                        {
                            const float D = Bridge.SampleSceneDistance(P);
                            if (D >= 0.28f)
                            {
                                break;
                            }
                            const auto Grad = Bridge.EvaluateDistanceGradient(P, 0.05f);
                            P = P + Grad * std::max(0.06f, 0.32f - D);
                        }
                    }
                    Cells[Idx].WorldPosition  = P;
                    Cells[Idx].ActiveValidity = 1.0f;
                }
            }
        }
    }

    [[nodiscard]] bool ContainsInside(const Mathematics::Axis3D& P) const noexcept
    {
        return P.X >= GridMin.X && P.X <= GridMax.X &&
               P.Y >= GridMin.Y && P.Y <= GridMax.Y &&
               P.Z >= GridMin.Z && P.Z <= GridMax.Z;
    }

    // C1 Hermite-smoothed trilinear lookup + gentle visibility weighting (eliminates grid-line diamonds & blocks)
    [[nodiscard]] Mathematics::LinearRGB SampleIrradianceOverPi(
        const Mathematics::Axis3D& WorldPos,
        const Mathematics::Axis3D& SurfaceNormal,
        const Mathematics::Axis3D& ViewDir,
        bool                       UseHermiteWeights,
        float                      ChebyshevExponent) const noexcept
    {
        if (Cells.empty())
        {
            return {};
        }
        const float BiasMag = std::min(0.08f, 0.12f * std::min({ Spacing.X, Spacing.Y, Spacing.Z }));
        const Mathematics::Axis3D BiasedP = WorldPos + (SurfaceNormal * 0.75f + ViewDir * 0.25f) * BiasMag;

        const float FX = std::clamp((BiasedP.X - GridMin.X) / Spacing.X, 0.0f, static_cast<float>(CountX - 1u));
        const float FY = std::clamp((BiasedP.Y - GridMin.Y) / Spacing.Y, 0.0f, static_cast<float>(CountY - 1u));
        const float FZ = std::clamp((BiasedP.Z - GridMin.Z) / Spacing.Z, 0.0f, static_cast<float>(CountZ - 1u));

        const auto IX0 = std::min(CountX - 2u, static_cast<uint32_t>(std::floor(FX)));
        const auto IY0 = std::min(CountY - 2u, static_cast<uint32_t>(std::floor(FY)));
        const auto IZ0 = std::min(CountZ - 2u, static_cast<uint32_t>(std::floor(FZ)));
        const uint32_t IX1 = IX0 + 1u;
        const uint32_t IY1 = IY0 + 1u;
        const uint32_t IZ1 = IZ0 + 1u;

        float TX = FX - static_cast<float>(IX0);
        float TY = FY - static_cast<float>(IY0);
        float TZ = FZ - static_cast<float>(IZ0);
        if (UseHermiteWeights)
        {
            TX = SmoothCubicScalar(TX);
            TY = SmoothCubicScalar(TY);
            TZ = SmoothCubicScalar(TZ);
        }

        Mathematics::LinearRGB Accum {};
        float TotalW = 0.0f;

        for (uint32_t DZ = 0u; DZ < 2u; ++DZ)
        {
            const uint32_t CZ = (DZ == 0u) ? IZ0 : IZ1;
            const float WZ    = (DZ == 0u) ? (1.0f - TZ) : TZ;
            for (uint32_t DY = 0u; DY < 2u; ++DY)
            {
                const uint32_t CY = (DY == 0u) ? IY0 : IY1;
                const float WY    = (DY == 0u) ? (1.0f - TY) : TY;
                for (uint32_t DX = 0u; DX < 2u; ++DX)
                {
                    const uint32_t CX = (DX == 0u) ? IX0 : IX1;
                    const float WX    = (DX == 0u) ? (1.0f - TX) : TX;

                    const std::size_t Idx = (static_cast<std::size_t>(CZ) * CountY + CY) * CountX + CX;
                    const auto& Cell = Cells[Idx];
                    if (Cell.ActiveValidity <= 1.0e-4f)
                    {
                        continue;
                    }

                    const Mathematics::Axis3D Delta = BiasedP - Cell.WorldPosition;
                    const float Dist = Mathematics::EvaluateLength(Delta);
                    const Mathematics::Axis3D DirCellToSurf = (Dist > 1.0e-5f) ? (Delta * (1.0f / Dist)) : (SurfaceNormal * -1.0f);
                    const Mathematics::Axis3D DirSurfToCell = DirCellToSurf * -1.0f;

                    const float WrapCos = 0.5f * (Mathematics::EvaluateDot(DirSurfToCell, SurfaceNormal) + 1.0f);
                    const float SmoothBack = std::max(0.02f, WrapCos * WrapCos);

                    const auto Moments = SampleOctahedral<Mathematics::Axis2D, CleanOctahedralCell::DepthWidth>(
                        Cell.DepthMoments, DirCellToSurf);
                    const float MeanR    = Moments.X;
                    const float Variance = std::max(0.06f, std::fabs(Moments.Y - MeanR * MeanR));
                    float Chebyshev = 1.0f;
                    if (Dist > MeanR)
                    {
                        const float Excess = Dist - MeanR;
                        Chebyshev = Variance / (Variance + Excess * Excess);
                        Chebyshev = std::pow(ClampUnit(Chebyshev), ChebyshevExponent);
                    }

                    const float W = WX * WY * WZ * SmoothBack * Cell.ActiveValidity * std::max(1.0e-3f, Chebyshev);
                    const auto Irr = SampleOctahedral<Mathematics::LinearRGB, CleanOctahedralCell::IrrWidth>(
                        Cell.IrradianceOverPi, SurfaceNormal);

                    Accum = Accum + Irr * W;
                    TotalW += W;
                }
            }
        }
        return (TotalW > 1.0e-6f) ? (Accum * (1.0f / TotalW)) : Mathematics::LinearRGB{};
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                      DIFFUSE ENVIRONMENT BOUNCE DISKS (SHARED CLEAN 2ND-BOUNCE ENVIRONMENT CLOUD)
//------------------------------------------------------------------------------------------------------------------------

struct DiffuseSurfacePatch
{
    Mathematics::Axis3D    Center       {};
    Mathematics::Axis3D    UnitNormal   {};
    Mathematics::LinearRGB AlbedoRgb    {};
    float                  AreaM2       = 0.25f;
    Mathematics::LinearRGB ExitantRad   {}; // Radiosity / pi ([cd/m2 / sr] outgoing diffuse)
};

[[nodiscard]] inline std::vector<DiffuseSurfacePatch> BuildStudioDiffusePatchCloud(
    float VehicleCenterX,
    float VehicleCenterY)
{
    std::vector<DiffuseSurfacePatch> Cloud {};
    Cloud.reserve(320u);

    const Mathematics::LinearRGB FloorAlb   { 0.62f, 0.61f, 0.59f };
    const Mathematics::LinearRGB CrimsonAlb { 0.85f, 0.08f, 0.07f };
    const Mathematics::LinearRGB EmeraldAlb { 0.09f, 0.78f, 0.18f };
    const Mathematics::LinearRGB BackAlb    { 0.62f, 0.61f, 0.59f };
    const Mathematics::LinearRGB CobaltAlb  { 0.16f, 0.34f, 0.86f };
    const Mathematics::LinearRGB HoodAlb    { 0.08f, 0.32f, 0.84f };

    // Studio floor grid (12 x 10 patches over [-5.2..5.2, -4.4..4.1])
    {
        constexpr int NX = 12, NY = 10;
        const float W = 10.4f / NX, H = 8.5f / NY;
        for (int IY = 0; IY < NY; ++IY)
        {
            for (int IX = 0; IX < NX; ++IX)
            {
                const float X = -5.2f + (static_cast<float>(IX) + 0.5f) * W;
                const float Y = -4.4f + (static_cast<float>(IY) + 0.5f) * H;
                Cloud.push_back({ { X, Y, 0.005f }, { 0.0f, 0.0f, 1.0f }, FloorAlb, W * H, {} });
            }
        }
    }
    // Left Crimson wall (-5.06 m)
    {
        constexpr int NY = 8, NZ = 4;
        const float H = 8.5f / NY, V = 3.2f / NZ;
        for (int IZ = 0; IZ < NZ; ++IZ)
            for (int IY = 0; IY < NY; ++IY)
                Cloud.push_back({ { -5.05f, -4.4f + (IY + 0.5f) * H, 0.1f + (IZ + 0.5f) * V }, { 1.0f, 0.0f, 0.0f }, CrimsonAlb, H * V, {} });
    }
    // Right Emerald wall (+5.06 m)
    {
        constexpr int NY = 8, NZ = 4;
        const float H = 8.5f / NY, V = 3.2f / NZ;
        for (int IZ = 0; IZ < NZ; ++IZ)
            for (int IY = 0; IY < NY; ++IY)
                Cloud.push_back({ { 5.05f, -4.4f + (IY + 0.5f) * H, 0.1f + (IZ + 0.5f) * V }, { -1.0f, 0.0f, 0.0f }, EmeraldAlb, H * V, {} });
    }
    // Back Plaster wall (+3.95 m)
    {
        constexpr int NX = 10, NZ = 4;
        const float W = 10.1f / NX, V = 3.2f / NZ;
        for (int IZ = 0; IZ < NZ; ++IZ)
            for (int IX = 0; IX < NX; ++IX)
                Cloud.push_back({ { -5.05f + (IX + 0.5f) * W, 3.95f, 0.1f + (IZ + 0.5f) * V }, { 0.0f, -1.0f, 0.0f }, BackAlb, W * V, {} });
    }
    // Cobalt pillar & Plaster pillar front/side faces
    for (int S = 0; S < 4; ++S)
    {
        const Mathematics::Axis3D Nrm = (S == 0) ? Mathematics::Axis3D{ -1, 0, 0 }
            : (S == 1) ? Mathematics::Axis3D{ 1, 0, 0 }
            : (S == 2) ? Mathematics::Axis3D{ 0, -1, 0 }
                       : Mathematics::Axis3D{ 0, 1, 0 };
        for (int IZ = 0; IZ < 3; ++IZ)
        {
            const float Z = 0.38f + static_cast<float>(IZ) * 0.76f;
            Cloud.push_back({ Mathematics::Axis3D{ 3.45f, 1.05f, Z } + Nrm * 0.245f, Nrm, CobaltAlb, 0.48f * 0.76f, {} });
            Cloud.push_back({ Mathematics::Axis3D{ -2.85f, 1.95f, Z } + Nrm * 0.285f, Nrm, FloorAlb, 0.56f * 0.76f, {} });
        }
    }
    // Coarse blue vehicle hood/roof bounce patches
    Cloud.push_back({ { VehicleCenterX + 1.9f, VehicleCenterY, 1.05f }, { 0.15f, 0.0f, 0.99f }, HoodAlb, 1.6f, {} });
    Cloud.push_back({ { VehicleCenterX - 1.9f, VehicleCenterY, 1.15f }, { -0.15f, 0.0f, 0.99f }, HoodAlb, 1.4f, {} });

    return Cloud;
}

//------------------------------------------------------------------------------------------------------------------------
//     CANDIDATE 1: ANALYTIC STOKES SPHERICAL-POLYGON CONTOUR + SOFT SDF PENUMBRA + SMOOTH MULTI-BOUNCE
//------------------------------------------------------------------------------------------------------------------------

class StokesContourEmissiveGi
{
public:
    SceneGeometryBridge              Bridge {};
    std::vector<DiffuseSurfacePatch> BounceCloud {};
    CleanLatticeCascade              DdgiBounceLattice {};

    // Exact closed-form clipped spherical-polygon form factor F = (1/pi) * integral_P (n . w) dw
    [[nodiscard]] static float EvaluateClippedTriangleFormFactor(
        const Mathematics::Axis3D& ReceiverP,
        const Mathematics::Axis3D& ReceiverN,
        const Mathematics::Axis3D& TriA,
        const Mathematics::Axis3D& TriB,
        const Mathematics::Axis3D& TriC) noexcept
    {
        // Sutherland-Hodgman clip against receiver tangent cutoff plane (X - P) . N >= 0
        std::array<Mathematics::Axis3D, 4u> Poly {};
        uint32_t PolyCount = 0u;

        const std::array<Mathematics::Axis3D, 3u> InVerts {{ TriA, TriB, TriC }};
        std::array<float, 3u> Dist {};
        for (uint32_t I = 0u; I < 3u; ++I)
        {
            Dist[I] = Mathematics::EvaluateDot(InVerts[I] - ReceiverP, ReceiverN);
        }
        if (Dist[0] <= 0.0f && Dist[1] <= 0.0f && Dist[2] <= 0.0f)
        {
            return 0.0f;
        }

        for (uint32_t I = 0u; I < 3u; ++I)
        {
            const uint32_t J = (I + 1u) % 3u;
            const float DA = Dist[I];
            const float DB = Dist[J];
            const bool InA = (DA >= 0.0f);
            const bool InB = (DB >= 0.0f);
            if (InA)
            {
                Poly[PolyCount++] = InVerts[I];
                if (!InB && PolyCount < 4u)
                {
                    const float T = DA / std::max(1.0e-7f, DA - DB);
                    Poly[PolyCount++] = InVerts[I] + (InVerts[J] - InVerts[I]) * T;
                }
            }
            else if (InB && PolyCount < 4u)
            {
                const float T = DA / std::max(1.0e-7f, DA - DB);
                Poly[PolyCount++] = InVerts[I] + (InVerts[J] - InVerts[I]) * T;
            }
        }
        if (PolyCount < 3u)
        {
            return 0.0f;
        }

        std::array<Mathematics::Axis3D, 4u> Sphere {};
        for (uint32_t I = 0u; I < PolyCount; ++I)
        {
            Sphere[I] = Mathematics::NormalizeAxis(Poly[I] - ReceiverP, ReceiverN);
        }

        float ContourSum = 0.0f;
        for (uint32_t I = 0u; I < PolyCount; ++I)
        {
            const uint32_t J = (I + 1u) % PolyCount;
            const float CosTheta = std::clamp(Mathematics::EvaluateDot(Sphere[I], Sphere[J]), -0.999999f, 0.999999f);
            const Mathematics::Axis3D EdgeCross = Mathematics::EvaluateCross(Sphere[I], Sphere[J]);
            const float CrossLen = Mathematics::EvaluateLength(EdgeCross);
            if (CrossLen > 1.0e-7f)
            {
                const float Angle = std::acos(CosTheta);
                ContourSum += Angle * (Mathematics::EvaluateDot(EdgeCross, ReceiverN) / CrossLen);
            }
        }
        // Winding order may flip sign depending on camera vs polygon normal orientation; solid-angle flux >= 0
        return std::clamp(std::fabs(ContourSum) * (0.5f / Constants::PiFloat), 0.0f, 1.0f);
    }

    // Exact 1st-bounce Emissive Irradiance / pi at arbitrary world point (evaluated analytically per triangle + cluster SDF shadow)
    [[nodiscard]] Mathematics::LinearRGB EvaluateDirectEmissiveOverPi(
        const Mathematics::Axis3D& ReceiverP,
        const Mathematics::Axis3D& ReceiverN) const noexcept
    {
        if (Bridge.Emissives.empty())
        {
            return {};
        }

        std::array<float, 6u> ClusterVis {};
        std::array<uint8_t, 6u> ClusterReady {};
        Mathematics::LinearRGB TotalOverPi {};

        for (const auto& Clust : Bridge.Clusters)
        {
            const Mathematics::Axis3D ToClust = Clust.CenterWorld - ReceiverP;
            const float ClustDist = Mathematics::EvaluateLength(ToClust);
            // Quick luminaire-plane backface bound check with cluster radius tolerance
            if (ClustDist > Clust.BoundRadiusM + 0.05f)
            {
                const Mathematics::Axis3D DirToRec = (ReceiverP - Clust.CenterWorld) * (1.0f / ClustDist);
                if (Mathematics::EvaluateDot(Clust.MeanNormal, DirToRec) < -0.42f)
                {
                    continue;
                }
            }

            float Vis = 1.0f;
            const uint32_t CID = Bridge.Emissives[Clust.FirstTriIdx].ClusterIndex;
            if (!ClusterReady[CID])
            {
                Vis = Bridge.EvaluateHybridConeVisibility(
                    ReceiverP, ReceiverN, Clust.CenterWorld, std::clamp(Clust.BoundRadiusM * 0.45f, 0.05f, 0.22f), 0.20f);
                ClusterVis[CID]   = Vis;
                ClusterReady[CID] = 1u;
            }
            else
            {
                Vis = ClusterVis[CID];
            }
            if (Vis <= 1.0e-4f)
            {
                continue;
            }

            for (uint32_t K = Clust.FirstTriIdx; K < Clust.FirstTriIdx + Clust.TriCount; ++K)
            {
                const auto& Tri = Bridge.Emissives[K];
                const Mathematics::Axis3D TriToRec = ReceiverP - Tri.Centroid;
                // Strictly single-sided luminaire face check
                if (Mathematics::EvaluateDot(Tri.UnitNormal, TriToRec) <= 1.0e-4f)
                {
                    continue;
                }
                const float F = EvaluateClippedTriangleFormFactor(ReceiverP, ReceiverN, Tri.VertexA, Tri.VertexB, Tri.VertexC);
                if (F > 1.0e-6f)
                {
                    const float W = F * Vis;
                    TotalOverPi.R += Tri.Radiance.R * W;
                    TotalOverPi.G += Tri.Radiance.G * W;
                    TotalOverPi.B += Tri.Radiance.B * W;
                }
            }
        }
        return TotalOverPi;
    }

    // Smooth non-emissive 2nd-bounce bleed evaluated over studio diffuse patches
    [[nodiscard]] Mathematics::LinearRGB EvaluateSecondBounceOverPi(
        const Mathematics::Axis3D& ReceiverP,
        const Mathematics::Axis3D& ReceiverN) const noexcept
    {
        Mathematics::LinearRGB Bounce {};
        for (const auto& Patch : BounceCloud)
        {
            const Mathematics::Axis3D Delta = Patch.Center - ReceiverP;
            const float DistSq = Mathematics::EvaluateLengthSquared(Delta);
            if (DistSq < 0.04f)
            {
                continue;
            }
            const float Dist = std::sqrt(DistSq);
            const Mathematics::Axis3D Dir = Delta * (1.0f / Dist);
            const float CosR = Mathematics::EvaluateDot(ReceiverN, Dir);
            const float CosE = Mathematics::EvaluateDot(Patch.UnitNormal, Dir * -1.0f);
            if (CosR <= 1.0e-3f || CosE <= 1.0e-3f)
            {
                continue;
            }
            const float Form = (Patch.AreaM2 * CosR * CosE) / (Constants::PiFloat * DistSq + Patch.AreaM2);
            Bounce.R += Patch.ExitantRad.R * Form;
            Bounce.G += Patch.ExitantRad.G * Form;
            Bounce.B += Patch.ExitantRad.B * Form;
        }
        return Bounce;
    }

    void Prepare(
        const GeometricRaster::WorldSceneSpecification&             Scene,
        std::span<const DistanceFieldSpace::ClosedBox>              StaticBoxes,
        const Mathematics::Axis3D&                                  VehicleCenter)
    {
        Bridge      = ConstructSceneGeometryBridge(Scene, StaticBoxes);
        BounceCloud = BuildStudioDiffusePatchCloud(VehicleCenter.X, VehicleCenter.Y);

        // Compute clean 1st-bounce illumination on studio patches -> diffuse bounce exitance for 2nd-bounce bleed
        for (auto& Patch : BounceCloud)
        {
            const auto DirectIrr = EvaluateDirectEmissiveOverPi(Patch.Center, Patch.UnitNormal);
            Patch.ExitantRad = {
                DirectIrr.R * Patch.AlbedoRgb.R,
                DirectIrr.G * Patch.AlbedoRgb.G,
                DirectIrr.B * Patch.AlbedoRgb.B,
            };
        }
        // One relaxation step for 3rd-bounce corner softness
        std::vector<Mathematics::LinearRGB> NextExitant(BounceCloud.size());
        for (std::size_t I = 0u; I < BounceCloud.size(); ++I)
        {
            const auto& P = BounceCloud[I];
            const auto Sec = EvaluateSecondBounceOverPi(P.Center, P.UnitNormal);
            NextExitant[I] = {
                P.ExitantRad.R + Sec.R * P.AlbedoRgb.R,
                P.ExitantRad.G + Sec.G * P.AlbedoRgb.G,
                P.ExitantRad.B + Sec.B * P.AlbedoRgb.B,
            };
        }
        for (std::size_t I = 0u; I < BounceCloud.size(); ++I)
        {
            BounceCloud[I].ExitantRad = NextExitant[I];
        }

        // Bake smooth DDGI Lattice holding ONLY non-emissive multi-bounce (0 ray-miss variance!)
        DdgiBounceLattice.InitializeGrid({ -4.90f, -3.95f, 0.12f }, { 4.90f, 3.65f, 2.95f }, 19u, 15u, 6u, Bridge, true);
        constexpr uint32_t RayCount = 96u;
        constexpr float GoldenInv   = 0.61803398875f;
        const float MaxD            = Mathematics::EvaluateLength(DdgiBounceLattice.Spacing) * 2.4f;

        RunParallelScanlines(DdgiBounceLattice.CountZ, [&](uint32_t ZStart, uint32_t ZEnd)
        {
            for (uint32_t IZ = ZStart; IZ < ZEnd; ++IZ)
            {
                for (uint32_t IY = 0u; IY < DdgiBounceLattice.CountY; ++IY)
                {
                    for (uint32_t IX = 0u; IX < DdgiBounceLattice.CountX; ++IX)
                    {
                        auto& Cell = DdgiBounceLattice.Cells[(static_cast<std::size_t>(IZ) * DdgiBounceLattice.CountY + IY) * DdgiBounceLattice.CountX + IX];
                        struct CellRaySample
                        {
                            Mathematics::Axis3D Dir  {};
                            float               Dist = 10.0f;
                        };
                        std::vector<CellRaySample> Samples(RayCount);
                        uint32_t BackHits = 0u;

                        for (uint32_t R = 0u; R < RayCount; ++R)
                        {
                            const float Z   = 1.0f - (2.0f * (static_cast<float>(R) + 0.5f)) / static_cast<float>(RayCount);
                            const float Rad = std::sqrt(std::max(0.0f, 1.0f - Z * Z));
                            const float Phi = Constants::TauFloat * std::fmod(static_cast<float>(R) * GoldenInv, 1.0f);
                            const Mathematics::Axis3D Dir { Rad * std::cos(Phi), Rad * std::sin(Phi), Z };
                            Samples[R].Dir  = Dir;
                            Samples[R].Dist = MaxD;
                            if (Bridge.SceneTree != nullptr)
                            {
                                SpatialAcceleration::RaySegmentQuery Q {};
                                Q.Origin          = Cell.WorldPosition;
                                Q.UnitDirection   = Dir;
                                Q.MinimumDistance = 1.0e-3f;
                                Q.MaximumDistance = 50.0f;
                                const auto Hit = Bridge.SceneTree->TraceClosestRay(Q, false);
                                if (Hit.HitConfirmed)
                                {
                                    Samples[R].Dist = Hit.HitDistanceMeters;
                                    if (Mathematics::EvaluateDot(Hit.GeometricNormal, Dir) >= 0.0f)
                                    {
                                        ++BackHits;
                                    }
                                }
                            }
                        }
                        Cell.ActiveValidity = (static_cast<float>(BackHits) / static_cast<float>(RayCount) > 0.25f) ? 0.02f : 1.0f;

                        for (uint32_t OY = 0u; OY < CleanOctahedralCell::DepthWidth; ++OY)
                        {
                            for (uint32_t OX = 0u; OX < CleanOctahedralCell::DepthWidth; ++OX)
                            {
                                const auto TexD = CleanLatticeCascade::DecodeOctahedral(
                                    (static_cast<float>(OX) + 0.5f) / static_cast<float>(CleanOctahedralCell::DepthWidth),
                                    (static_cast<float>(OY) + 0.5f) / static_cast<float>(CleanOctahedralCell::DepthWidth));
                                float SR = 0.0f, SR2 = 0.0f, SW = 0.0f;
                                for (const auto& S : Samples)
                                {
                                    const float C = Mathematics::EvaluateDot(TexD, S.Dir);
                                    if (C > 0.0f)
                                    {
                                        const float W = std::pow(C, 12.0f);
                                        const float D = std::min(MaxD, S.Dist);
                                        SR += D * W;
                                        SR2 += D * D * W;
                                        SW += W;
                                    }
                                }
                                Cell.DepthMoments[OY * CleanOctahedralCell::DepthWidth + OX] = (SW > 1.0e-6f)
                                    ? Mathematics::Axis2D{ SR / SW, SR2 / SW }
                                    : Mathematics::Axis2D{ MaxD, MaxD * MaxD };
                            }
                        }

                        for (uint32_t OY = 0u; OY < CleanOctahedralCell::IrrWidth; ++OY)
                        {
                            for (uint32_t OX = 0u; OX < CleanOctahedralCell::IrrWidth; ++OX)
                            {
                                const auto NrmD = CleanLatticeCascade::DecodeOctahedral(
                                    (static_cast<float>(OX) + 0.5f) / static_cast<float>(CleanOctahedralCell::IrrWidth),
                                    (static_cast<float>(OY) + 0.5f) / static_cast<float>(CleanOctahedralCell::IrrWidth));
                                Cell.IrradianceOverPi[OY * CleanOctahedralCell::IrrWidth + OX] =
                                    EvaluateSecondBounceOverPi(Cell.WorldPosition, NrmD);
                            }
                        }
                    }
                }
            }
        });
    }

    // Normal GI evaluation: Exact Stokes 1st-bounce + Smooth Surface Cloud 2nd-bounce
    [[nodiscard]] Mathematics::LinearRGB EvaluateNormalGiOverPi(
        const Mathematics::Axis3D& WorldPos,
        const Mathematics::Axis3D& Normal) const noexcept
    {
        return EvaluateDirectEmissiveOverPi(WorldPos, Normal) + EvaluateSecondBounceOverPi(WorldPos, Normal);
    }

    // DDGI evaluation: Analytic Emissive Split (1st-bounce) + Clean Stokes-Baked Multi-Bounce DDGI Lattice
    [[nodiscard]] Mathematics::LinearRGB EvaluateDdgiOverPi(
        const Mathematics::Axis3D& WorldPos,
        const Mathematics::Axis3D& Normal,
        const Mathematics::Axis3D& ViewDir) const noexcept
    {
        const auto DirectEmis = EvaluateDirectEmissiveOverPi(WorldPos, Normal);
        const auto MultiBounce = DdgiBounceLattice.SampleIrradianceOverPi(WorldPos, Normal, ViewDir, true, 1.45f);
        return DirectEmis + MultiBounce;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//     CANDIDATE 2: TWO-TIER VIRTUAL SURFEL RADIOSITY FIELD (BUNNELL DISK CASCADE + DUAL-CASCADE SURFEL DDGI)
//------------------------------------------------------------------------------------------------------------------------

struct RadiantSurfelDisk
{
    Mathematics::Axis3D    Position    {};
    Mathematics::Axis3D    UnitNormal  {};
    Mathematics::LinearRGB Radiance    {}; // Exitant radiance (L_e for emissives, E*albedo/pi for bounce disks)
    float                  AreaM2      = 0.01f;
    float                  RadiusM     = 0.05f;
};

struct SurfelClusterGroup
{
    Mathematics::Axis3D    Center      {};
    Mathematics::Axis3D    MeanNormal  {};
    Mathematics::LinearRGB MeanRad     {};
    float                  TotalAreaM2 = 0.0f;
    float                  BoundRadius = 0.15f;
    uint32_t               FirstDisk   = 0u;
    uint32_t               DiskCount   = 0u;
};

class SurfelCascadeEmissiveGi
{
public:
    SceneGeometryBridge              Bridge        {};
    std::vector<RadiantSurfelDisk>   EmissiveDisks {};
    std::vector<SurfelClusterGroup>  EmissiveGroups{};
    std::vector<DiffuseSurfacePatch> BounceSurfels {};
    CleanLatticeCascade              InnerLattice  {}; // Dense vehicle-local adaptive DDGI cascade
    CleanLatticeCascade              OuterLattice  {}; // Studio room outer DDGI cascade

    // Singularity-free Bunnell/Wallace finite-disk irradiance form factor over pi
    [[nodiscard]] static float EvaluateBunnellDiskFormFactor(
        const Mathematics::Axis3D& ReceiverP,
        const Mathematics::Axis3D& ReceiverN,
        const Mathematics::Axis3D& DiskP,
        const Mathematics::Axis3D& DiskN,
        float                      DiskAreaM2,
        float                      DiskRadiusM) noexcept
    {
        const Mathematics::Axis3D Delta = DiskP - ReceiverP;
        const float DistSq = Mathematics::EvaluateLengthSquared(Delta);
        const float Dist   = std::sqrt(std::max(1.0e-7f, DistSq));
        const Mathematics::Axis3D Dir = Delta * (1.0f / Dist);

        const float CosR = Mathematics::EvaluateDot(ReceiverN, Dir);
        if (CosR <= 1.0e-4f)
        {
            return 0.0f;
        }
        // Smooth finite-disk clip angle prevents sharp planar snapping near grazing lights
        const float RawCosE      = Mathematics::EvaluateDot(DiskN, Dir * -1.0f);
        const float SinClipAngle = DiskRadiusM / std::sqrt(DistSq + DiskRadiusM * DiskRadiusM);
        if (RawCosE <= -SinClipAngle)
        {
            return 0.0f;
        }
        const float SmoothCosE = ClampUnit((RawCosE + 0.30f * SinClipAngle) / (1.0f + 0.30f * SinClipAngle));
        if (SmoothCosE <= 1.0e-4f)
        {
            return 0.0f;
        }
        return (DiskAreaM2 * CosR * SmoothCosE) / (Constants::PiFloat * DistSq + DiskAreaM2 * std::max(0.35f, SmoothCosE));
    }

    // Evaluates exact 2-tier Emissive Disk Cascade + SDF Cluster Occlusion
    [[nodiscard]] Mathematics::LinearRGB EvaluateEmissiveSurfelTreeOverPi(
        const Mathematics::Axis3D& P,
        const Mathematics::Axis3D& N,
        bool                       EnableVisibility) const noexcept
    {
        Mathematics::LinearRGB Sum {};
        for (const auto& Grp : EmissiveGroups)
        {
            const Mathematics::Axis3D Delta = Grp.Center - P;
            const float Dist = Mathematics::EvaluateLength(Delta);
            if (Dist > Grp.BoundRadius + 0.08f)
            {
                const Mathematics::Axis3D ToRec = (P - Grp.Center) * (1.0f / Dist);
                if (Mathematics::EvaluateDot(Grp.MeanNormal, ToRec) < -0.45f)
                {
                    continue;
                }
            }

            float Vis = 1.0f;
            if (EnableVisibility)
            {
                Vis = Bridge.EvaluateHybridConeVisibility(
                    P, N, Grp.Center, std::clamp(Grp.BoundRadius * 0.45f, 0.05f, 0.22f), 0.20f);
                if (Vis <= 1.0e-4f)
                {
                    continue;
                }
            }

            // Cluster solid-angle cutoff: far clusters (> 4.5 m) collapse cleanly to single aggregate disk
            const float SolidAngleApprox = Grp.TotalAreaM2 / std::max(0.1f, Dist * Dist);
            if (Dist > 4.8f && SolidAngleApprox < 0.025f)
            {
                const float R = std::sqrt(Grp.TotalAreaM2 / Constants::PiFloat);
                const float F = EvaluateBunnellDiskFormFactor(P, N, Grp.Center, Grp.MeanNormal, Grp.TotalAreaM2, R);
                Sum.R += Grp.MeanRad.R * F * Vis;
                Sum.G += Grp.MeanRad.G * F * Vis;
                Sum.B += Grp.MeanRad.B * F * Vis;
            }
            else
            {
                for (uint32_t K = Grp.FirstDisk; K < Grp.FirstDisk + Grp.DiskCount; ++K)
                {
                    const auto& Dsk = EmissiveDisks[K];
                    const float F = EvaluateBunnellDiskFormFactor(P, N, Dsk.Position, Dsk.UnitNormal, Dsk.AreaM2, Dsk.RadiusM);
                    if (F > 1.0e-6f)
                    {
                        const float W = F * Vis;
                        Sum.R += Dsk.Radiance.R * W;
                        Sum.G += Dsk.Radiance.G * W;
                        Sum.B += Dsk.Radiance.B * W;
                    }
                }
            }
        }
        return Sum;
    }

    [[nodiscard]] Mathematics::LinearRGB EvaluateBounceSurfelsOverPi(
        const Mathematics::Axis3D& P,
        const Mathematics::Axis3D& N) const noexcept
    {
        Mathematics::LinearRGB Sum {};
        for (const auto& Srf : BounceSurfels)
        {
            const float R = std::sqrt(Srf.AreaM2 / Constants::PiFloat);
            const float F = EvaluateBunnellDiskFormFactor(P, N, Srf.Center, Srf.UnitNormal, Srf.AreaM2, R);
            if (F > 1.0e-6f)
            {
                Sum.R += Srf.ExitantRad.R * F;
                Sum.G += Srf.ExitantRad.G * F;
                Sum.B += Srf.ExitantRad.B * F;
            }
        }
        return Sum;
    }

    // Analytic directional cell integration from the Surfel Radiosity Field (zero stochastic rays!)
    void BakeLatticeAnalyticallyFromSurfels(CleanLatticeCascade& Lattice) const
    {
        constexpr uint32_t RaySamples = 96u;
        constexpr float GoldenInv     = 0.61803398875f;
        const float MaxD              = Mathematics::EvaluateLength(Lattice.Spacing) * 2.4f;

        RunParallelScanlines(Lattice.CountZ, [&](uint32_t ZStart, uint32_t ZEnd)
        {
            for (uint32_t IZ = ZStart; IZ < ZEnd; ++IZ)
            {
                for (uint32_t IY = 0u; IY < Lattice.CountY; ++IY)
                {
                    for (uint32_t IX = 0u; IX < Lattice.CountX; ++IX)
                    {
                        const std::size_t CIdx = (static_cast<std::size_t>(IZ) * Lattice.CountY + IY) * Lattice.CountX + IX;
                        auto& Cell = Lattice.Cells[CIdx];

                        std::array<Mathematics::Axis3D, RaySamples> RayDirs {};
                        std::array<float, RaySamples> RayDists {};
                        uint32_t BackHits = 0u;

                        // Chebyshev visibility field via BVH4
                        for (uint32_t R = 0u; R < RaySamples; ++R)
                        {
                            const float Z   = 1.0f - (2.0f * (static_cast<float>(R) + 0.5f)) / static_cast<float>(RaySamples);
                            const float Rad = std::sqrt(std::max(0.0f, 1.0f - Z * Z));
                            const float Phi = Constants::TauFloat * std::fmod(static_cast<float>(R) * GoldenInv, 1.0f);
                            const Mathematics::Axis3D Dir { Rad * std::cos(Phi), Rad * std::sin(Phi), Z };
                            RayDirs[R]  = Dir;
                            RayDists[R] = MaxD;
                            if (Bridge.SceneTree != nullptr)
                            {
                                SpatialAcceleration::RaySegmentQuery Q {};
                                Q.Origin          = Cell.WorldPosition;
                                Q.UnitDirection   = Dir;
                                Q.MinimumDistance = 1.0e-3f;
                                Q.MaximumDistance = 40.0f;
                                const auto Hit = Bridge.SceneTree->TraceClosestRay(Q, false);
                                if (Hit.HitConfirmed)
                                {
                                    RayDists[R] = Hit.HitDistanceMeters;
                                    if (Mathematics::EvaluateDot(Hit.GeometricNormal, Dir) >= 0.0f)
                                    {
                                        ++BackHits;
                                    }
                                }
                            }
                        }
                        Cell.ActiveValidity = (static_cast<float>(BackHits) / static_cast<float>(RaySamples) > 0.30f) ? 0.04f : 1.0f;

                        for (uint32_t OY = 0u; OY < CleanOctahedralCell::DepthWidth; ++OY)
                        {
                            for (uint32_t OX = 0u; OX < CleanOctahedralCell::DepthWidth; ++OX)
                            {
                                const auto TexD = CleanLatticeCascade::DecodeOctahedral(
                                    (static_cast<float>(OX) + 0.5f) / static_cast<float>(CleanOctahedralCell::DepthWidth),
                                    (static_cast<float>(OY) + 0.5f) / static_cast<float>(CleanOctahedralCell::DepthWidth));
                                float SR = 0.0f, SR2 = 0.0f, SW = 0.0f;
                                for (uint32_t R = 0u; R < RaySamples; ++R)
                                {
                                    const float C = Mathematics::EvaluateDot(TexD, RayDirs[R]);
                                    if (C > 0.0f)
                                    {
                                        const float W = std::pow(C, 10.0f);
                                        const float D = std::min(MaxD, RayDists[R]);
                                        SR += D * W;
                                        SR2 += D * D * W;
                                        SW += W;
                                    }
                                }
                                Cell.DepthMoments[OY * CleanOctahedralCell::DepthWidth + OX] = (SW > 1.0e-6f)
                                    ? Mathematics::Axis2D{ SR / SW, SR2 / SW }
                                    : Mathematics::Axis2D{ MaxD, MaxD * MaxD };
                            }
                        }

                        // Zero-variance analytic integration of ALL Emissive + Bounce Surfels per octahedral normal texel!
                        for (uint32_t OY = 0u; OY < CleanOctahedralCell::IrrWidth; ++OY)
                        {
                            for (uint32_t OX = 0u; OX < CleanOctahedralCell::IrrWidth; ++OX)
                            {
                                const auto TexN = CleanLatticeCascade::DecodeOctahedral(
                                    (static_cast<float>(OX) + 0.5f) / static_cast<float>(CleanOctahedralCell::IrrWidth),
                                    (static_cast<float>(OY) + 0.5f) / static_cast<float>(CleanOctahedralCell::IrrWidth));
                                const auto EmIrr  = EvaluateEmissiveSurfelTreeOverPi(Cell.WorldPosition, TexN, true);
                                const auto BncIrr = EvaluateBounceSurfelsOverPi(Cell.WorldPosition, TexN);
                                Cell.IrradianceOverPi[OY * CleanOctahedralCell::IrrWidth + OX] = EmIrr + BncIrr;
                            }
                        }
                    }
                }
            }
        });
    }

    void Prepare(
        const GeometricRaster::WorldSceneSpecification&             Scene,
        std::span<const DistanceFieldSpace::ClosedBox>              StaticBoxes,
        const Mathematics::Axis3D&                                  VehicleCenter)
    {
        Bridge = ConstructSceneGeometryBridge(Scene, StaticBoxes);

        // Stratified area-proportional decomposition of emissive triangles into uniform micro-disks
        constexpr uint32_t TargetEmissiveDisks = 120u;
        EmissiveDisks.clear();
        EmissiveGroups.clear();

        double TotalArea = 0.0;
        for (const auto& E : Bridge.Emissives) TotalArea += static_cast<double>(E.AreaM2);
        const float InvTotalArea = (TotalArea > 1.0e-9) ? static_cast<float>(1.0 / TotalArea) : 1.0f;

        uint32_t SampleIdx = 0u;
        for (const auto& Clust : Bridge.Clusters)
        {
            SurfelClusterGroup Grp {};
            Grp.FirstDisk = static_cast<uint32_t>(EmissiveDisks.size());
            Grp.Center     = Clust.CenterWorld;
            Grp.MeanNormal = Clust.MeanNormal;
            Grp.MeanRad    = Clust.MeanRadiance;
            Grp.TotalAreaM2 = Clust.TotalAreaM2;
            Grp.BoundRadius = Clust.BoundRadiusM;

            for (uint32_t K = Clust.FirstTriIdx; K < Clust.FirstTriIdx + Clust.TriCount; ++K)
            {
                const auto& Tri = Bridge.Emissives[K];
                const uint32_t SubCount = std::max(1u, static_cast<uint32_t>(
                    std::round(static_cast<float>(TargetEmissiveDisks) * Tri.AreaM2 * InvTotalArea)));
                const float SubArea = Tri.AreaM2 / static_cast<float>(SubCount);
                const float SubRad  = std::sqrt(SubArea / Constants::PiFloat);

                for (uint32_t S = 0u; S < SubCount; ++S)
                {
                    const auto U = GeometricRaster::Sobol02Sample(SampleIdx++, 0u, 1u);
                    const float SqrtU = std::sqrt(ClampUnit(U.X));
                    const float B0 = 1.0f - SqrtU;
                    const float B1 = SqrtU * (1.0f - U.Y);
                    const float B2 = SqrtU * U.Y;
                    const Mathematics::Axis3D Pos = Tri.VertexA * B0 + Tri.VertexB * B1 + Tri.VertexC * B2;
                    EmissiveDisks.push_back({ Pos, Tri.UnitNormal, Tri.Radiance, SubArea, SubRad });
                }
            }
            Grp.DiskCount = static_cast<uint32_t>(EmissiveDisks.size()) - Grp.FirstDisk;
            EmissiveGroups.push_back(Grp);
        }

        // Compute deterministic 2-bounce diffuse studio surfels
        BounceSurfels = BuildStudioDiffusePatchCloud(VehicleCenter.X, VehicleCenter.Y);
        for (auto& Patch : BounceSurfels)
        {
            const auto Direct = EvaluateEmissiveSurfelTreeOverPi(Patch.Center, Patch.UnitNormal, true);
            Patch.ExitantRad = {
                Direct.R * Patch.AlbedoRgb.R,
                Direct.G * Patch.AlbedoRgb.G,
                Direct.B * Patch.AlbedoRgb.B,
            };
        }
        std::vector<Mathematics::LinearRGB> NextExitant(BounceSurfels.size());
        for (std::size_t I = 0u; I < BounceSurfels.size(); ++I)
        {
            const auto& P = BounceSurfels[I];
            const auto B2 = EvaluateBounceSurfelsOverPi(P.Center, P.UnitNormal);
            NextExitant[I] = {
                P.ExitantRad.R + B2.R * P.AlbedoRgb.R,
                P.ExitantRad.G + B2.G * P.AlbedoRgb.G,
                P.ExitantRad.B + B2.B * P.AlbedoRgb.B,
            };
        }
        for (std::size_t I = 0u; I < BounceSurfels.size(); ++I)
        {
            BounceSurfels[I].ExitantRad = NextExitant[I];
        }

        // Bake adaptive Dual-Cascade DDGI directly from Surfel Radiosity Field (full interior studio span = zero seam)
        (void)VehicleCenter;
        InnerLattice.InitializeGrid(
            { -4.90f, -3.95f, 0.09f },
            {  4.90f,  3.65f, 2.95f },
            19u, 15u, 6u, Bridge, true);
        OuterLattice.InitializeGrid(
            { -6.50f, -5.20f, 0.15f },
            {  6.50f,  4.80f, 3.20f },
            9u, 7u, 4u, Bridge, true);

        BakeLatticeAnalyticallyFromSurfels(InnerLattice);
        BakeLatticeAnalyticallyFromSurfels(OuterLattice);
    }

    [[nodiscard]] Mathematics::LinearRGB EvaluateNormalGiOverPi(
        const Mathematics::Axis3D& WorldPos,
        const Mathematics::Axis3D& Normal) const noexcept
    {
        return EvaluateEmissiveSurfelTreeOverPi(WorldPos, Normal, true) +
               EvaluateBounceSurfelsOverPi(WorldPos, Normal);
    }

    // DDGI queries pure Dual-Cascade Surfel-Baked Lattice (with smooth near-field disk continuity inside 0.75 m of vehicle)
    [[nodiscard]] Mathematics::LinearRGB EvaluateDdgiOverPi(
        const Mathematics::Axis3D& WorldPos,
        const Mathematics::Axis3D& Normal,
        const Mathematics::Axis3D& ViewDir) const noexcept
    {
        const auto OuterIrr = OuterLattice.SampleIrradianceOverPi(WorldPos, Normal, ViewDir, true, 1.35f);
        if (!InnerLattice.ContainsInside(WorldPos))
        {
            return OuterIrr;
        }
        const auto InnerIrr = InnerLattice.SampleIrradianceOverPi(WorldPos, Normal, ViewDir, true, 1.35f);
        // Smooth blend band near InnerLattice edge
        const float EdgeX = std::min(WorldPos.X - InnerLattice.GridMin.X, InnerLattice.GridMax.X - WorldPos.X);
        const float EdgeY = std::min(WorldPos.Y - InnerLattice.GridMin.Y, InnerLattice.GridMax.Y - WorldPos.Y);
        const float EdgeZ = std::min(WorldPos.Z - InnerLattice.GridMin.Z, InnerLattice.GridMax.Z - WorldPos.Z);
        const float Blend = SmoothHermite(0.0f, 0.65f, std::min({ EdgeX, EdgeY, EdgeZ }));
        return OuterIrr * (1.0f - Blend) + InnerIrr * Blend;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//   CANDIDATE 3: FIELD-NATIVE CONTINUOUS PREFILTERED DIRECTIONAL L1 CLIPMAP + VEACH-MIS HERMITE DDGI
//------------------------------------------------------------------------------------------------------------------------
// Zero per-pixel emissive list iteration at shade time in BOTH Normal GI and DDGI!

struct DirectionalShVoxel
{
    Mathematics::LinearRGB C0      {}; // Total directional mean radiance (band 0)
    Mathematics::LinearRGB Cx      {}; // X cosine lobe moment (band 1)
    Mathematics::LinearRGB Cy      {}; // Y cosine lobe moment (band 1)
    Mathematics::LinearRGB Cz      {}; // Z cosine lobe moment (band 1)
    float                  Density = 0.0f; // Anti-aliased surface / luminaire opacity density [0..1]
    float                  SdfDist = 1.0f; // Cached scene signed distance [m] for O(1) cone marching

    [[nodiscard]] DirectionalShVoxel operator+(const DirectionalShVoxel& O) const noexcept
    {
        return { C0 + O.C0, Cx + O.Cx, Cy + O.Cy, Cz + O.Cz, Density + O.Density, SdfDist + O.SdfDist };
    }
    [[nodiscard]] DirectionalShVoxel operator*(float S) const noexcept
    {
        return { C0 * S, Cx * S, Cy * S, Cz * S, Density * S, SdfDist * S };
    }
    // Evaluates radiance radiating from voxel toward IncomingDir (= -RayDir)
    // Uses SH directional coherence so unidirectional luminaires radiate strictly into their front hemisphere (zero hood leak!)
    [[nodiscard]] Mathematics::LinearRGB EvaluateToward(const Mathematics::Axis3D& OutDir) const noexcept
    {
        const auto EvalBand = [&](float B0, float BX, float BY, float BZ) noexcept -> float
        {
            if (B0 <= 1.0e-7f)
            {
                return 0.0f;
            }
            const float B1Len = std::sqrt(BX * BX + BY * BY + BZ * BZ);
            const float Coh   = std::min(1.0f, B1Len / B0);
            const float Dot   = BX * OutDir.X + BY * OutDir.Y + BZ * OutDir.Z;
            return std::max(0.0f, B0 * (1.0f - Coh) + 2.0f * std::max(0.0f, Dot));
        };
        return {
            EvalBand(C0.R, Cx.R, Cy.R, Cz.R),
            EvalBand(C0.G, Cx.G, Cy.G, Cz.G),
            EvalBand(C0.B, Cx.B, Cy.B, Cz.B),
        };
    }
};

struct DirectionalClipmapLevel
{
    Mathematics::Axis3D              BoundsMin {};
    Mathematics::Axis3D              BoundsMax {};
    float                            SpacingM  = 0.10f;
    uint32_t                         DimX      = 0u;
    uint32_t                         DimY      = 0u;
    uint32_t                         DimZ      = 0u;
    std::vector<DirectionalShVoxel>  Voxels    {};

    [[nodiscard]] std::size_t IndexOf(uint32_t X, uint32_t Y, uint32_t Z) const noexcept
    {
        return (static_cast<std::size_t>(Z) * DimY + Y) * DimX + X;
    }

    // C1 Hermite-smoothed trilinear sampling eliminates diamond/grid creases along voxel boundaries
    [[nodiscard]] DirectionalShVoxel SampleHermite(const Mathematics::Axis3D& P) const noexcept
    {
        if (DimX < 2u || DimY < 2u || DimZ < 2u || Voxels.empty())
        {
            return {};
        }
        const float InvS = 1.0f / SpacingM;
        const float FX = std::clamp((P.X - BoundsMin.X) * InvS, 0.0f, static_cast<float>(DimX - 1u));
        const float FY = std::clamp((P.Y - BoundsMin.Y) * InvS, 0.0f, static_cast<float>(DimY - 1u));
        const float FZ = std::clamp((P.Z - BoundsMin.Z) * InvS, 0.0f, static_cast<float>(DimZ - 1u));
        const auto X0 = std::min(DimX - 2u, static_cast<uint32_t>(std::floor(FX)));
        const auto Y0 = std::min(DimY - 2u, static_cast<uint32_t>(std::floor(FY)));
        const auto Z0 = std::min(DimZ - 2u, static_cast<uint32_t>(std::floor(FZ)));
        const uint32_t X1 = X0 + 1u;
        const uint32_t Y1 = Y0 + 1u;
        const uint32_t Z1 = Z0 + 1u;

        const float TX = SmoothCubicScalar(FX - static_cast<float>(X0));
        const float TY = SmoothCubicScalar(FY - static_cast<float>(Y0));
        const float TZ = SmoothCubicScalar(FZ - static_cast<float>(Z0));

        const DirectionalShVoxel C00 = Voxels[IndexOf(X0, Y0, Z0)] * (1.0f - TX) + Voxels[IndexOf(X1, Y0, Z0)] * TX;
        const DirectionalShVoxel C10 = Voxels[IndexOf(X0, Y1, Z0)] * (1.0f - TX) + Voxels[IndexOf(X1, Y1, Z0)] * TX;
        const DirectionalShVoxel C01 = Voxels[IndexOf(X0, Y0, Z1)] * (1.0f - TX) + Voxels[IndexOf(X1, Y0, Z1)] * TX;
        const DirectionalShVoxel C11 = Voxels[IndexOf(X0, Y1, Z1)] * (1.0f - TX) + Voxels[IndexOf(X1, Y1, Z1)] * TX;
        const DirectionalShVoxel C0  = C00 * (1.0f - TY) + C10 * TY;
        const DirectionalShVoxel C1  = C01 * (1.0f - TY) + C11 * TY;
        return C0 * (1.0f - TZ) + C1 * TZ;
    }

    void BuildDownsampledFrom(const DirectionalClipmapLevel& Fine)
    {
        BoundsMin = Fine.BoundsMin;
        BoundsMax = Fine.BoundsMax;
        SpacingM  = Fine.SpacingM * 2.0f;
        DimX      = std::max(2u, (Fine.DimX + 1u) / 2u);
        DimY      = std::max(2u, (Fine.DimY + 1u) / 2u);
        DimZ      = std::max(2u, (Fine.DimZ + 1u) / 2u);
        Voxels.assign(static_cast<std::size_t>(DimX) * DimY * DimZ, DirectionalShVoxel{});

        // Surface-area-conserving 2x2x2 pyramid (Crassin 2011): sum/4 for SH radiance/opacity, box-mean for cached SDF
        for (uint32_t Z = 0u; Z < DimZ; ++Z)
        {
            for (uint32_t Y = 0u; Y < DimY; ++Y)
            {
                for (uint32_t X = 0u; X < DimX; ++X)
                {
                    DirectionalShVoxel Sum {};
                    float DistBoxMean = 0.0f;
                    const uint32_t FX0 = std::min(Fine.DimX - 1u, X * 2u);
                    const uint32_t FY0 = std::min(Fine.DimY - 1u, Y * 2u);
                    const uint32_t FZ0 = std::min(Fine.DimZ - 1u, Z * 2u);
                    for (uint32_t DZ = 0u; DZ < 2u; ++DZ)
                    {
                        const uint32_t SZ = std::min(Fine.DimZ - 1u, FZ0 + DZ);
                        for (uint32_t DY = 0u; DY < 2u; ++DY)
                        {
                            const uint32_t SY = std::min(Fine.DimY - 1u, FY0 + DY);
                            for (uint32_t DX = 0u; DX < 2u; ++DX)
                            {
                                const uint32_t SX = std::min(Fine.DimX - 1u, FX0 + DX);
                                const auto& FV = Fine.Voxels[Fine.IndexOf(SX, SY, SZ)];
                                Sum = Sum + FV * 0.25f;
                                DistBoxMean += FV.SdfDist * 0.125f;
                            }
                        }
                    }
                    // Clamp diagonal double-counted surfaces so total cell opacity never exceeds 1.0
                    if (Sum.Density > 1.0f)
                    {
                        const float Scale = 1.0f / Sum.Density;
                        Sum = Sum * Scale;
                    }
                    Sum.SdfDist = DistBoxMean;
                    Voxels[IndexOf(X, Y, Z)] = Sum;
                }
            }
        }
    }
};

class ContinuousFieldEmissiveGi
{
public:
    SceneGeometryBridge                 Bridge {};
    std::array<DirectionalClipmapLevel, 4u> Mips {};
    CleanLatticeCascade                 MisInnerLattice {};
    CleanLatticeCascade                 MisOuterLattice {};

    // Continuous Mip-Interpolated Directional L1 Clipmap lookup (ZERO per-pixel triangle iteration!)
    [[nodiscard]] DirectionalShVoxel SampleMipCascade(const Mathematics::Axis3D& P, float ConeRadiusM) const noexcept
    {
        const float FineSpacing = Mips[0].SpacingM;
        const float LevelF      = std::clamp(std::log2(std::max(1.0f, ConeRadiusM / FineSpacing)), 0.0f, 3.0f);
        const auto L0 = std::min(2u, static_cast<uint32_t>(std::floor(LevelF)));
        const uint32_t L1 = L0 + 1u;
        const float Alpha = LevelF - static_cast<float>(L0);
        const auto V0 = Mips[L0].SampleHermite(P);
        const auto V1 = Mips[L1].SampleHermite(P);
        return V0 * (1.0f - Alpha) + V1 * Alpha;
    }

    // Normal GI: 36 Overlapping Normalized-Weight Fibonacci Cones marching Mip-Filtered Directional L1 Volume + Cached SDF
    [[nodiscard]] Mathematics::LinearRGB EvaluateNormalGiOverPi(
        const Mathematics::Axis3D& WorldPos,
        const Mathematics::Axis3D& SurfaceNormal) const noexcept
    {
        constexpr uint32_t ConeCount = 36u;
        constexpr float InvCones     = 1.0f / static_cast<float>(ConeCount);
        const Mathematics::Axis3D N  = Mathematics::NormalizeAxis(SurfaceNormal, { 0.0f, 0.0f, 1.0f });
        Mathematics::Axis3D TanU {}, BitV {};
        ConstructTangentPair(N, TanU, BitV);

        const float Voxel0 = Mips[0].SpacingM;
        const Mathematics::Axis3D Origin = WorldPos + N * (Voxel0 * 1.45f);
        Mathematics::LinearRGB IrrAccum {};

        for (uint32_t K = 0u; K < ConeCount; ++K)
        {
            const float U = (static_cast<float>(K) + 0.5f) * InvCones;
            const float R = std::sqrt(std::clamp(U, 0.01f, 0.96f));
            const float Ang = static_cast<float>(K) * 2.39996323f + 0.25f;
            const Mathematics::Axis3D CDir = Mathematics::NormalizeAxis(
                TanU * (R * std::cos(Ang)) + BitV * (R * std::sin(Ang)) + N * std::sqrt(std::max(0.0f, 1.0f - R * R)));

            // Wider aperture (~20.8 deg) ensures seamless cone overlap across 36 cones without lobe gaps
            constexpr float TanHalfAperture = 0.38f;
            float CT     = Voxel0 * 0.60f;
            float Trans  = 1.0f;
            Mathematics::LinearRGB ConeRad {};

            for (uint32_t Step = 0u; Step < 20u && Trans > 0.02f && CT < 12.0f; ++Step)
            {
                const Mathematics::Axis3D SP = Origin + CDir * CT;
                if (SP.X < Mips[0].BoundsMin.X || SP.X > Mips[0].BoundsMax.X ||
                    SP.Y < Mips[0].BoundsMin.Y || SP.Y > Mips[0].BoundsMax.Y ||
                    SP.Z < Mips[0].BoundsMin.Z || SP.Z > Mips[0].BoundsMax.Z)
                {
                    break;
                }

                const float ConeR   = std::max(Voxel0 * 0.85f, CT * TanHalfAperture);
                const float VoxelH  = std::max(Voxel0, ConeR);
                const auto ShSample = SampleMipCascade(SP, ConeR);
                const auto PreMultToward = ShSample.EvaluateToward(CDir * -1.0f);

                const float StepDt  = std::min(0.45f, std::max(Voxel0 * 0.80f, ConeR * 0.72f));
                const float DtScale = StepDt / VoxelH;

                float StepOcc = ClampUnit(ShSample.Density * DtScale);
                const float PlaneFade = SmoothHermite(0.03f, 0.16f, Mathematics::EvaluateDot(SP - WorldPos, N));
                if (CT > Voxel0 * 1.4f && ShSample.SdfDist < ConeR * 0.50f && PlaneFade > 1.0e-4f)
                {
                    const float SolidOcc = 1.0f - SmoothHermite(-0.02f, ConeR * 0.50f, ShSample.SdfDist);
                    StepOcc = std::max(StepOcc, SolidOcc * 0.65f * PlaneFade);
                }

                ConeRad.R += PreMultToward.R * Trans * DtScale;
                ConeRad.G += PreMultToward.G * Trans * DtScale;
                ConeRad.B += PreMultToward.B * Trans * DtScale;
                Trans *= (1.0f - StepOcc);

                CT += StepDt;
            }

            IrrAccum.R += ConeRad.R * InvCones;
            IrrAccum.G += ConeRad.G * InvCones;
            IrrAccum.B += ConeRad.B * InvCones;
        }
        return IrrAccum;
    }

    // Veach Power-Heuristic Emissive MIS lattice cell bake (128 uniform Fibonacci + 64 Emissive CDF rays -> ZERO spike blotches)
    void BakeVeachMisLattice(CleanLatticeCascade& Lattice) const
    {
        constexpr uint32_t UniRays  = 128u;
        constexpr uint32_t EmisRays = 64u;
        constexpr uint32_t TotalRays = UniRays + EmisRays;
        constexpr float GoldenInv   = 0.61803398875f;
        const float MaxD            = Mathematics::EvaluateLength(Lattice.Spacing) * 2.4f;

        const auto EvalEmisPdfSolidAngle = [&](const Mathematics::Axis3D& Origin, const Mathematics::Axis3D& Dir) noexcept -> float
        {
            if (Bridge.Emissives.empty() || Bridge.TotalFlux <= 1.0e-12)
            {
                return 0.0f;
            }
            // Ray hit check against emissive triangles or analytic projection
            float SumPdf = 0.0f;
            for (const auto& Tri : Bridge.Emissives)
            {
                const Mathematics::Axis3D Edge1 = Tri.VertexB - Tri.VertexA;
                const Mathematics::Axis3D Edge2 = Tri.VertexC - Tri.VertexA;
                const Mathematics::Axis3D PVec  = Mathematics::EvaluateCross(Dir, Edge2);
                const float Det = Mathematics::EvaluateDot(Edge1, PVec);
                if (std::fabs(Det) < 1.0e-7f) continue;
                const float InvDet = 1.0f / Det;
                const Mathematics::Axis3D TVec = Origin - Tri.VertexA;
                const float U = Mathematics::EvaluateDot(TVec, PVec) * InvDet;
                if (U < 0.0f || U > 1.0f) continue;
                const Mathematics::Axis3D QVec = Mathematics::EvaluateCross(TVec, Edge1);
                const float V = Mathematics::EvaluateDot(Dir, QVec) * InvDet;
                if (V < 0.0f || U + V > 1.0f) continue;
                const float T = Mathematics::EvaluateDot(Edge2, QVec) * InvDet;
                if (T <= 1.0e-3f) continue;
                const float CosE = Mathematics::EvaluateDot(Tri.UnitNormal, Dir * -1.0f);
                if (CosE <= 1.0e-4f) continue;
                const float FluxProb = (ComputeLinearLuminance(Tri.Radiance) * Tri.AreaM2) / static_cast<float>(Bridge.TotalFlux);
                SumPdf += FluxProb * (T * T) / (CosE * Tri.AreaM2);
            }
            return SumPdf;
        };

        RunParallelScanlines(Lattice.CountZ, [&](uint32_t ZStart, uint32_t ZEnd)
        {
            for (uint32_t IZ = ZStart; IZ < ZEnd; ++IZ)
            {
                for (uint32_t IY = 0u; IY < Lattice.CountY; ++IY)
                {
                    for (uint32_t IX = 0u; IX < Lattice.CountX; ++IX)
                    {
                        const std::size_t CIdx = (static_cast<std::size_t>(IZ) * Lattice.CountY + IY) * Lattice.CountX + IX;
                        auto& Cell = Lattice.Cells[CIdx];

                        struct MisRaySample
                        {
                            Mathematics::Axis3D    Dir   {};
                            Mathematics::LinearRGB WeightRad {}; // (L_i * MIS) / (pdf * N)
                            float                  Dist  = 10.0f;
                            bool                   FromUni = true;
                        };
                        std::vector<MisRaySample> Samples(TotalRays);
                        uint32_t BackHits = 0u;

                        // Stream A: 128 Uniform Spherical Fibonacci rays
                        for (uint32_t R = 0u; R < UniRays; ++R)
                        {
                            const float Z   = 1.0f - (2.0f * (static_cast<float>(R) + 0.5f)) / static_cast<float>(UniRays);
                            const float Rad = std::sqrt(std::max(0.0f, 1.0f - Z * Z));
                            const float Phi = Constants::TauFloat * std::fmod(static_cast<float>(R) * GoldenInv, 1.0f);
                            const Mathematics::Axis3D Dir { Rad * std::cos(Phi), Rad * std::sin(Phi), Z };
                            Samples[R].Dir     = Dir;
                            Samples[R].FromUni = true;
                            Samples[R].Dist    = MaxD;

                            if (Bridge.SceneTree != nullptr)
                            {
                                SpatialAcceleration::RaySegmentQuery Q {};
                                Q.Origin          = Cell.WorldPosition;
                                Q.UnitDirection   = Dir;
                                Q.MinimumDistance = 1.0e-3f;
                                Q.MaximumDistance = 45.0f;
                                const auto Hit = Bridge.SceneTree->TraceClosestRay(Q, false);
                                if (Hit.HitConfirmed)
                                {
                                    Samples[R].Dist = Hit.HitDistanceMeters;
                                    if (Mathematics::EvaluateDot(Hit.GeometricNormal, Dir) >= 0.0f)
                                    {
                                        ++BackHits;
                                    }
                                    else if (Hit.MaterialOrdinal < Bridge.Materials->QuerySlotCount())
                                    {
                                        const auto& Mat = Bridge.Materials->InspectAuthoredMaterial(Hit.MaterialOrdinal);
                                        const auto Em = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);
                                        const float EmL = ComputeLinearLuminance(Em);
                                        const float HitPosPdfUni = 1.0f / (4.0f * Constants::PiFloat);
                                        if (EmL > 1.0e-4f)
                                        {
                                            const float PdfEmis = EvalEmisPdfSolidAngle(Cell.WorldPosition, Dir);
                                            const float W0 = static_cast<float>(UniRays) * HitPosPdfUni;
                                            const float W1 = static_cast<float>(EmisRays) * PdfEmis;
                                            const float Mis = (W0 * W0) / std::max(1.0e-12f, W0 * W0 + W1 * W1);
                                            Samples[R].WeightRad = Em * (Mis / (HitPosPdfUni * static_cast<float>(UniRays)));
                                        }
                                        else
                                        {
                                            // Non-emissive wall/floor: sample prefiltered directional bounce field for 2nd bounce!
                                            const auto HitP = Cell.WorldPosition + Dir * Hit.HitDistanceMeters;
                                            const float DiffW = (1.0f - Mat.Specular.Metalness) * (1.0f - Mat.Transmission.TransmissionWeight);
                                            const auto Bnc = EvaluateNormalGiOverPi(HitP, Hit.GeometricNormal);
                                            Samples[R].WeightRad = {
                                                (Bnc.R * Mat.Diffuse.BaseColor.R * DiffW) / (HitPosPdfUni * static_cast<float>(UniRays)),
                                                (Bnc.G * Mat.Diffuse.BaseColor.G * DiffW) / (HitPosPdfUni * static_cast<float>(UniRays)),
                                                (Bnc.B * Mat.Diffuse.BaseColor.B * DiffW) / (HitPosPdfUni * static_cast<float>(UniRays)),
                                            };
                                        }
                                    }
                                }
                            }
                        }

                        // Stream B: 64 Deterministic Sobol Emissive-Triangle Importance Rays
                        for (uint32_t E = 0u; E < EmisRays; ++E)
                        {
                            auto& Dst = Samples[UniRays + E];
                            Dst.FromUni = false;
                            Dst.Dist    = MaxD;
                            if (Bridge.Emissives.empty() || Bridge.SceneTree == nullptr) continue;

                            const float Sel = (static_cast<float>(E) + 0.5f) / static_cast<float>(EmisRays);
                            auto It = std::lower_bound(Bridge.Emissives.begin(), Bridge.Emissives.end(), Sel,
                                [](const EmissiveTriangleSource& S, float V) { return S.CdfUpper < V; });
                            if (It == Bridge.Emissives.end()) --It;

                            const auto U = GeometricRaster::Sobol02Sample(E, static_cast<uint32_t>(CIdx * 97u + 13u), 2u);
                            const float SqrtU = std::sqrt(ClampUnit(U.X));
                            const Mathematics::Axis3D Target = It->VertexA * (1.0f - SqrtU)
                                                             + It->VertexB * (SqrtU * (1.0f - U.Y))
                                                             + It->VertexC * (SqrtU * U.Y);
                            const Mathematics::Axis3D Delta = Target - Cell.WorldPosition;
                            const float Dist2 = Mathematics::EvaluateLengthSquared(Delta);
                            if (Dist2 < 1.0e-5f) continue;
                            const float Dist = std::sqrt(Dist2);
                            const Mathematics::Axis3D Dir = Delta * (1.0f / Dist);
                            Dst.Dir = Dir;

                            const float CosE = Mathematics::EvaluateDot(It->UnitNormal, Dir * -1.0f);
                            if (CosE <= 1.0e-4f) continue;

                            SpatialAcceleration::RaySegmentQuery VisQ {};
                            VisQ.Origin          = Cell.WorldPosition;
                            VisQ.UnitDirection   = Dir;
                            VisQ.MinimumDistance = 1.0e-3f;
                            VisQ.MaximumDistance = std::max(0.01f, Dist - 0.02f);
                            if (!Bridge.SceneTree->QueryRayOccluded(VisQ))
                            {
                                const float PdfEmis = EvalEmisPdfSolidAngle(Cell.WorldPosition, Dir);
                                const float PdfUni  = 1.0f / (4.0f * Constants::PiFloat);
                                if (PdfEmis > 1.0e-7f)
                                {
                                    const float W0 = static_cast<float>(UniRays) * PdfUni;
                                    const float W1 = static_cast<float>(EmisRays) * PdfEmis;
                                    const float Mis = (W1 * W1) / std::max(1.0e-12f, W0 * W0 + W1 * W1);
                                    Dst.WeightRad = It->Radiance * (Mis / (PdfEmis * static_cast<float>(EmisRays)));
                                }
                            }
                        }

                        Cell.ActiveValidity = (static_cast<float>(BackHits) / static_cast<float>(UniRays) > 0.30f) ? 0.04f : 1.0f;

                        // Chebyshev radial moments from uniform ray stream
                        for (uint32_t OY = 0u; OY < CleanOctahedralCell::DepthWidth; ++OY)
                        {
                            for (uint32_t OX = 0u; OX < CleanOctahedralCell::DepthWidth; ++OX)
                            {
                                const auto TexD = CleanLatticeCascade::DecodeOctahedral(
                                    (static_cast<float>(OX) + 0.5f) / static_cast<float>(CleanOctahedralCell::DepthWidth),
                                    (static_cast<float>(OY) + 0.5f) / static_cast<float>(CleanOctahedralCell::DepthWidth));
                                float SR = 0.0f, SR2 = 0.0f, SW = 0.0f;
                                for (uint32_t R = 0u; R < UniRays; ++R)
                                {
                                    const float C = Mathematics::EvaluateDot(TexD, Samples[R].Dir);
                                    if (C > 0.0f)
                                    {
                                        const float W = std::pow(C, 10.0f);
                                        const float D = std::min(MaxD, Samples[R].Dist);
                                        SR += D * W;
                                        SR2 += D * D * W;
                                        SW += W;
                                    }
                                }
                                Cell.DepthMoments[OY * CleanOctahedralCell::DepthWidth + OX] = (SW > 1.0e-6f)
                                    ? Mathematics::Axis2D{ SR / SW, SR2 / SW }
                                    : Mathematics::Axis2D{ MaxD, MaxD * MaxD };
                            }
                        }

                        // Project MIS stream (unit integral over hemisphere -> E/pi = (1/pi) * Sum max(0, N.dir) * WeightRad)
                        for (uint32_t OY = 0u; OY < CleanOctahedralCell::IrrWidth; ++OY)
                        {
                            for (uint32_t OX = 0u; OX < CleanOctahedralCell::IrrWidth; ++OX)
                            {
                                const auto NrmD = CleanLatticeCascade::DecodeOctahedral(
                                    (static_cast<float>(OX) + 0.5f) / static_cast<float>(CleanOctahedralCell::IrrWidth),
                                    (static_cast<float>(OY) + 0.5f) / static_cast<float>(CleanOctahedralCell::IrrWidth));
                                Mathematics::LinearRGB Sum {};
                                for (const auto& S : Samples)
                                {
                                    const float CosT = Mathematics::EvaluateDot(NrmD, S.Dir);
                                    if (CosT > 0.0f)
                                    {
                                        const float Scale = CosT * (1.0f / Constants::PiFloat);
                                        Sum.R += S.WeightRad.R * Scale;
                                        Sum.G += S.WeightRad.G * Scale;
                                        Sum.B += S.WeightRad.B * Scale;
                                    }
                                }
                                Cell.IrradianceOverPi[OY * CleanOctahedralCell::IrrWidth + OX] = Sum;
                            }
                        }
                    }
                }
            }
        });
    }

    void Prepare(
        const GeometricRaster::WorldSceneSpecification&             Scene,
        std::span<const DistanceFieldSpace::ClosedBox>              StaticBoxes,
        const Mathematics::Axis3D&                                  VehicleCenter)
    {
        (void)VehicleCenter;
        Bridge = ConstructSceneGeometryBridge(Scene, StaticBoxes);

        // Allocate Mip0 covering entire studio at ~9.0 cm resolution (118 x 96 x 40)
        Mips[0].BoundsMin = { -5.30f, -4.55f, -0.15f };
        Mips[0].BoundsMax = {  5.30f,  4.10f,  3.45f };
        Mips[0].SpacingM  = 0.090f;
        Mips[0].DimX      = 118u;
        Mips[0].DimY      = 96u;
        Mips[0].DimZ      = 40u;
        Mips[0].Voxels.assign(static_cast<std::size_t>(Mips[0].DimX) * Mips[0].DimY * Mips[0].DimZ, DirectionalShVoxel{});

        // Step 0: Bake continuous interior occluder SDF into Mip0 so cone marching never queries the BVH per ray step
        RunParallelScanlines(Mips[0].DimZ, [&](uint32_t ZStart, uint32_t ZEnd)
        {
            for (uint32_t Z = ZStart; Z < ZEnd; ++Z)
            {
                const float PZ = Mips[0].BoundsMin.Z + static_cast<float>(Z) * Mips[0].SpacingM;
                for (uint32_t Y = 0u; Y < Mips[0].DimY; ++Y)
                {
                    const float PY = Mips[0].BoundsMin.Y + static_cast<float>(Y) * Mips[0].SpacingM;
                    for (uint32_t X = 0u; X < Mips[0].DimX; ++X)
                    {
                        const float PX = Mips[0].BoundsMin.X + static_cast<float>(X) * Mips[0].SpacingM;
                        Mips[0].Voxels[Mips[0].IndexOf(X, Y, Z)].SdfDist = Bridge.SampleInteriorOccluderDistance({ PX, PY, PZ });
                    }
                }
            }
        });

        // Step 1: Anti-aliased Front-Side Gaussian Splatting of Emissive Triangles into Directional L1 SH Mip0
        // (Zero hard voxel snapping, zero 2.5f clamping, zero backward leak onto hood!)
        const float InvSp = 1.0f / Mips[0].SpacingM;
        const float CrossArea = Mips[0].SpacingM * Mips[0].SpacingM;
        const float Sigma = Mips[0].SpacingM * 1.30f;
        const float Inv2Sig2 = 1.0f / (2.0f * Sigma * Sigma);

        for (const auto& Tri : Bridge.Emissives)
        {
            const uint32_t Sub = std::max(4u, static_cast<uint32_t>(std::ceil(Tri.AreaM2 / (CrossArea * 0.25f))));
            const float SubArea = Tri.AreaM2 / static_cast<float>(Sub);
            const float SubOpacity = ClampUnit(SubArea / CrossArea);

            for (uint32_t S = 0u; S < Sub; ++S)
            {
                const auto U = GeometricRaster::Sobol02Sample(S, 31u, 0u);
                const float Sq = std::sqrt(ClampUnit(U.X));
                const Mathematics::Axis3D P = Tri.VertexA * (1.0f - Sq)
                                            + Tri.VertexB * (Sq * (1.0f - U.Y))
                                            + Tri.VertexC * (Sq * U.Y);

                const int32_t CX = static_cast<int32_t>(std::round((P.X - Mips[0].BoundsMin.X) * InvSp));
                const int32_t CY = static_cast<int32_t>(std::round((P.Y - Mips[0].BoundsMin.Y) * InvSp));
                const int32_t CZ = static_cast<int32_t>(std::round((P.Z - Mips[0].BoundsMin.Z) * InvSp));

                float SumW = 0.0f;
                for (int32_t DZ = -1; DZ <= 1; ++DZ)
                for (int32_t DY = -1; DY <= 1; ++DY)
                for (int32_t DX = -1; DX <= 1; ++DX)
                {
                    const int32_t IX = CX + DX, IY = CY + DY, IZ = CZ + DZ;
                    if (IX < 0 || IY < 0 || IZ < 0 ||
                        IX >= static_cast<int32_t>(Mips[0].DimX) ||
                        IY >= static_cast<int32_t>(Mips[0].DimY) ||
                        IZ >= static_cast<int32_t>(Mips[0].DimZ)) continue;
                    const Mathematics::Axis3D VP {
                        Mips[0].BoundsMin.X + IX * Mips[0].SpacingM,
                        Mips[0].BoundsMin.Y + IY * Mips[0].SpacingM,
                        Mips[0].BoundsMin.Z + IZ * Mips[0].SpacingM,
                    };
                    const float FrontMask = SmoothHermite(-0.025f, 0.035f, Mathematics::EvaluateDot(VP - P, Tri.UnitNormal));
                    SumW += std::exp(-Mathematics::EvaluateLengthSquared(VP - P) * Inv2Sig2) * FrontMask;
                }
                if (SumW <= 1.0e-7f) continue;

                for (int32_t DZ = -1; DZ <= 1; ++DZ)
                for (int32_t DY = -1; DY <= 1; ++DY)
                for (int32_t DX = -1; DX <= 1; ++DX)
                {
                    const int32_t IX = CX + DX, IY = CY + DY, IZ = CZ + DZ;
                    if (IX < 0 || IY < 0 || IZ < 0 ||
                        IX >= static_cast<int32_t>(Mips[0].DimX) ||
                        IY >= static_cast<int32_t>(Mips[0].DimY) ||
                        IZ >= static_cast<int32_t>(Mips[0].DimZ)) continue;
                    const Mathematics::Axis3D VP {
                        Mips[0].BoundsMin.X + IX * Mips[0].SpacingM,
                        Mips[0].BoundsMin.Y + IY * Mips[0].SpacingM,
                        Mips[0].BoundsMin.Z + IZ * Mips[0].SpacingM,
                    };
                    const float FrontMask = SmoothHermite(-0.025f, 0.035f, Mathematics::EvaluateDot(VP - P, Tri.UnitNormal));
                    if (FrontMask <= 1.0e-5f) continue;
                    const float W = (std::exp(-Mathematics::EvaluateLengthSquared(VP - P) * Inv2Sig2) * FrontMask) / SumW;
                    auto& Vox = Mips[0].Voxels[Mips[0].IndexOf(static_cast<uint32_t>(IX), static_cast<uint32_t>(IY), static_cast<uint32_t>(IZ))];

                    const float OpacityW = W * SubOpacity;
                    Vox.Density = std::min(1.0f, Vox.Density + OpacityW);
                    Vox.C0 = Vox.C0 + Tri.Radiance * (0.5f * OpacityW);
                    Vox.Cx = Vox.Cx + Tri.Radiance * (0.5f * Tri.UnitNormal.X * OpacityW);
                    Vox.Cy = Vox.Cy + Tri.Radiance * (0.5f * Tri.UnitNormal.Y * OpacityW);
                    Vox.Cz = Vox.Cz + Tri.Radiance * (0.5f * Tri.UnitNormal.Z * OpacityW);
                }
            }
        }

        // Step 2: Build 4-level Gaussian Mip Pyramid from emissive injection first
        for (uint32_t L = 1u; L < 4u; ++L)
        {
            Mips[L].BuildDownsampledFrom(Mips[L - 1u]);
        }

        // Step 3: Inject 1st-bounce studio surface radiosity into separate BounceScratch (zero write contention, zero fog!)
        std::vector<uint8_t> EmissiveMask(Mips[0].Voxels.size(), 0u);
        for (std::size_t I = 0u; I < Mips[0].Voxels.size(); ++I)
        {
            if (Mips[0].Voxels[I].Density > 0.04f && ComputeLinearLuminance(Mips[0].Voxels[I].C0) > 0.25f)
            {
                EmissiveMask[I] = 1u;
                Mips[0].Voxels[I].Density = std::max(Mips[0].Voxels[I].Density, 0.85f);
            }
        }

        std::vector<DirectionalShVoxel> BounceScratch(Mips[0].Voxels.size());
        std::vector<uint8_t> SurfaceMask(Mips[0].Voxels.size(), 0u);
        const auto InjectSurfaceBounce = [&](uint32_t ZStart, uint32_t ZEnd)
        {
            for (uint32_t Z = ZStart; Z < ZEnd; ++Z)
            {
                const float PZ = Mips[0].BoundsMin.Z + static_cast<float>(Z) * Mips[0].SpacingM;
                for (uint32_t Y = 0u; Y < Mips[0].DimY; ++Y)
                {
                    const float PY = Mips[0].BoundsMin.Y + static_cast<float>(Y) * Mips[0].SpacingM;
                    for (uint32_t X = 0u; X < Mips[0].DimX; ++X)
                    {
                        const std::size_t Idx = Mips[0].IndexOf(X, Y, Z);
                        if (EmissiveMask[Idx] != 0u)
                        {
                            continue;
                        }
                        const float PX = Mips[0].BoundsMin.X + static_cast<float>(X) * Mips[0].SpacingM;
                        const Mathematics::Axis3D WP { PX, PY, PZ };
                        // Never wrap emissive clusters in non-emissive bounce paint
                        bool NearEmis = false;
                        for (const auto& Clust : Bridge.Clusters)
                        {
                            if (Mathematics::EvaluateLength(WP - Clust.CenterWorld) < std::max(0.42f, Clust.BoundRadiusM * 0.90f))
                            {
                                NearEmis = true;
                                break;
                            }
                        }
                        if (NearEmis)
                        {
                            continue;
                        }
                        const float D = Bridge.SampleSceneDistance(WP);
                        if (D > Mips[0].SpacingM * 0.65f)
                        {
                            continue;
                        }

                        Mathematics::Axis3D Nrm = Bridge.EvaluateDistanceGradient(WP, 0.05f);
                        Mathematics::LinearRGB Alb { 0.45f, 0.45f, 0.45f };
                        if (WP.X < -4.85f)      { Alb = { 0.85f, 0.08f, 0.07f }; Nrm = {  1.0f, 0.0f, 0.0f }; }
                        else if (WP.X > 4.85f)  { Alb = { 0.09f, 0.78f, 0.18f }; Nrm = { -1.0f, 0.0f, 0.0f }; }
                        else if (WP.Y > 3.75f)  { Alb = { 0.62f, 0.61f, 0.59f }; Nrm = {  0.0f, -1.0f, 0.0f }; }
                        else if (WP.Z < 0.14f)  { Alb = { 0.62f, 0.61f, 0.59f }; Nrm = {  0.0f, 0.0f, 1.0f }; }
                        else if (std::fabs(WP.X - 3.45f) < 0.45f && std::fabs(WP.Y - 1.05f) < 0.45f)
                        {
                            Alb = { 0.16f, 0.34f, 0.86f };
                        }
                        else
                        {
                            // Keep dark ControlVehicle clearcoat hood from creating a self-illuminating voxel skin
                            continue;
                        }

                        const auto Irr = EvaluateNormalGiOverPi(WP, Nrm);
                        const float SurfDensity = 1.0f - SmoothHermite(0.0f, Mips[0].SpacingM * 0.65f, D);
                        const Mathematics::LinearRGB Exit {
                            Irr.R * Alb.R * SurfDensity,
                            Irr.G * Alb.G * SurfDensity,
                            Irr.B * Alb.B * SurfDensity,
                        };
                        SurfaceMask[Idx] = 1u;
                        auto& Dst = BounceScratch[Idx];
                        Dst.Density = SurfDensity;
                        Dst.C0 = Exit * 0.5f;
                        Dst.Cx = Exit * (0.5f * Nrm.X);
                        Dst.Cy = Exit * (0.5f * Nrm.Y);
                        Dst.Cz = Exit * (0.5f * Nrm.Z);
                    }
                }
            }
        };
        RunParallelScanlines(Mips[0].DimZ, InjectSurfaceBounce);

        // Smooth ONLY inside SurfaceMask (never blurring surface paint out into open air!)
        RunParallelScanlines(Mips[0].DimZ, [&](uint32_t ZStart, uint32_t ZEnd)
        {
            for (uint32_t Z = ZStart; Z < ZEnd; ++Z)
            {
                for (uint32_t Y = 0u; Y < Mips[0].DimY; ++Y)
                {
                    for (uint32_t X = 0u; X < Mips[0].DimX; ++X)
                    {
                        const std::size_t Idx = Mips[0].IndexOf(X, Y, Z);
                        if (SurfaceMask[Idx] == 0u || EmissiveMask[Idx] != 0u) continue;
                        DirectionalShVoxel Avg {};
                        float WeightSum = 0.0f;
                        for (int32_t DZ = -1; DZ <= 1; ++DZ)
                        for (int32_t DY = -1; DY <= 1; ++DY)
                        for (int32_t DX = -1; DX <= 1; ++DX)
                        {
                            const int32_t NX = static_cast<int32_t>(X) + DX;
                            const int32_t NY = static_cast<int32_t>(Y) + DY;
                            const int32_t NZ = static_cast<int32_t>(Z) + DZ;
                            if (NX < 0 || NY < 0 || NZ < 0 ||
                                NX >= static_cast<int32_t>(Mips[0].DimX) ||
                                NY >= static_cast<int32_t>(Mips[0].DimY) ||
                                NZ >= static_cast<int32_t>(Mips[0].DimZ)) continue;
                            const std::size_t NIdx = Mips[0].IndexOf(static_cast<uint32_t>(NX), static_cast<uint32_t>(NY), static_cast<uint32_t>(NZ));
                            if (SurfaceMask[NIdx] == 0u) continue;
                            const float KW = (DX == 0 && DY == 0 && DZ == 0) ? 4.0f : (std::abs(DX) + std::abs(DY) + std::abs(DZ) == 1 ? 2.0f : 1.0f);
                            Avg = Avg + BounceScratch[NIdx] * KW;
                            WeightSum += KW;
                        }
                        if (WeightSum > 1.0e-5f)
                        {
                            Avg = Avg * (1.0f / WeightSum);
                            auto& Dst = Mips[0].Voxels[Idx];
                            Dst.Density = std::max(Dst.Density, Avg.Density);
                            Dst.C0 = Dst.C0 + Avg.C0;
                            Dst.Cx = Dst.Cx + Avg.Cx;
                            Dst.Cy = Dst.Cy + Avg.Cy;
                            Dst.Cz = Dst.Cz + Avg.Cz;
                        }
                    }
                }
            }
        });

        for (uint32_t L = 1u; L < 4u; ++L)
        {
            Mips[L].BuildDownsampledFrom(Mips[L - 1u]);
        }

        // Step 4: Bake Veach Emissive-MIS Dual-Cascade DDGI Lattice (full interior studio span = zero seam)
        MisInnerLattice.InitializeGrid(
            { -4.90f, -3.95f, 0.09f },
            {  4.90f,  3.65f, 2.95f },
            19u, 15u, 6u, Bridge, true);
        MisOuterLattice.InitializeGrid(
            { -6.50f, -5.20f, 0.15f },
            {  6.50f,  4.80f, 3.20f },
            9u, 7u, 4u, Bridge, true);

        BakeVeachMisLattice(MisInnerLattice);
        BakeVeachMisLattice(MisOuterLattice);
    }

    [[nodiscard]] Mathematics::LinearRGB EvaluateDdgiOverPi(
        const Mathematics::Axis3D& WorldPos,
        const Mathematics::Axis3D& Normal,
        const Mathematics::Axis3D& ViewDir) const noexcept
    {
        const auto OuterIrr = MisOuterLattice.SampleIrradianceOverPi(WorldPos, Normal, ViewDir, true, 1.35f);
        if (!MisInnerLattice.ContainsInside(WorldPos))
        {
            return OuterIrr;
        }
        const auto InnerIrr = MisInnerLattice.SampleIrradianceOverPi(WorldPos, Normal, ViewDir, true, 1.35f);
        const float EdgeX = std::min(WorldPos.X - MisInnerLattice.GridMin.X, MisInnerLattice.GridMax.X - WorldPos.X);
        const float EdgeY = std::min(WorldPos.Y - MisInnerLattice.GridMin.Y, MisInnerLattice.GridMax.Y - WorldPos.Y);
        const float EdgeZ = std::min(WorldPos.Z - MisInnerLattice.GridMin.Z, MisInnerLattice.GridMax.Z - WorldPos.Z);
        const float Blend = SmoothHermite(0.0f, 0.65f, std::min({ EdgeX, EdgeY, EdgeZ }));
        return OuterIrr * (1.0f - Blend) + InnerIrr * Blend;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                      GROUND-TRUTH REFERENCE: DETERMINISTIC 128-SPP EMISSIVE-NEE PATH TRACER
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline Mathematics::LinearRGB TraceEmissiveNeeReferenceSample(
    const Mathematics::Axis3D&                         Origin,
    const Mathematics::Axis3D&                         Direction,
    const GeometricRaster::WorldSceneSpecification&    Scene,
    const SceneGeometryBridge&                         Bridge,
    uint32_t                                           SampleIdx,
    uint32_t                                           PixelSeed,
    uint32_t                                           MaxBounces) noexcept
{
    const uint32_t MatCount = Scene.MaterialRegister.QuerySlotCount();
    Mathematics::Axis3D RayO = Origin;
    Mathematics::Axis3D RayD = Direction;
    Mathematics::LinearRGB Throughput { 1.0f, 1.0f, 1.0f };
    Mathematics::LinearRGB Radiance {};
    bool PrevWasSpecular = false;
    float PrevBouncePdf  = 1.0f;

    const auto EvalEmisPdfSolid = [&](const Mathematics::Axis3D& P, const Mathematics::Axis3D& Dir) noexcept -> float
    {
        if (Bridge.Emissives.empty() || Bridge.TotalFlux <= 1.0e-12) return 0.0f;
        float SumPdf = 0.0f;
        for (const auto& Tri : Bridge.Emissives)
        {
            const Mathematics::Axis3D E1 = Tri.VertexB - Tri.VertexA;
            const Mathematics::Axis3D E2 = Tri.VertexC - Tri.VertexA;
            const Mathematics::Axis3D PV = Mathematics::EvaluateCross(Dir, E2);
            const float Det = Mathematics::EvaluateDot(E1, PV);
            if (std::fabs(Det) < 1.0e-7f) continue;
            const float InvDet = 1.0f / Det;
            const Mathematics::Axis3D TV = P - Tri.VertexA;
            const float U = Mathematics::EvaluateDot(TV, PV) * InvDet;
            if (U < 0.0f || U > 1.0f) continue;
            const Mathematics::Axis3D QV = Mathematics::EvaluateCross(TV, E1);
            const float V = Mathematics::EvaluateDot(Dir, QV) * InvDet;
            if (V < 0.0f || U + V > 1.0f) continue;
            const float T = Mathematics::EvaluateDot(E2, QV) * InvDet;
            if (T <= 1.0e-3f) continue;
            const float CosE = Mathematics::EvaluateDot(Tri.UnitNormal, Dir * -1.0f);
            if (CosE <= 1.0e-4f) continue;
            const float FluxP = (ComputeLinearLuminance(Tri.Radiance) * Tri.AreaM2) / static_cast<float>(Bridge.TotalFlux);
            SumPdf += FluxP * (T * T) / (CosE * Tri.AreaM2);
        }
        return SumPdf;
    };

    for (uint32_t Bounce = 0u; Bounce < MaxBounces; ++Bounce)
    {
        SpatialAcceleration::RaySegmentQuery Ray {};
        Ray.Origin          = RayO;
        Ray.UnitDirection   = RayD;
        Ray.MinimumDistance = 1.0e-3f;
        Ray.MaximumDistance = 80.0f;
        const auto Hit = Scene.SceneTree.TraceClosestRay(Ray, false);
        if (!Hit.HitConfirmed)
        {
            break;
        }

        const uint16_t MatSlot = (Hit.MaterialOrdinal < MatCount) ? Hit.MaterialOrdinal : 0u;
        const auto& Mat        = Scene.MaterialRegister.InspectAuthoredMaterial(MatSlot);
        const uint32_t Lobes   = Scene.MaterialRegister.InspectCompactEntry(MatSlot).ActiveLobeMask;

        const Mathematics::Axis3D HitPos = RayO + RayD * Hit.HitDistanceMeters;
        const bool Front = (Mathematics::EvaluateDot(Hit.GeometricNormal, RayD) < 0.0f);
        const Mathematics::Axis3D Nrm = Front ? Hit.GeometricNormal : (Hit.GeometricNormal * -1.0f);
        const Mathematics::Axis3D Wo  = RayD * -1.0f;

        Mathematics::Axis3D TanU {}, BitV {};
        ConstructTangentPair(Nrm, TanU, BitV);
        const auto WorldToLocal = [&](const Mathematics::Axis3D& V) noexcept -> Mathematics::Axis3D
        {
            return { Mathematics::EvaluateDot(V, TanU), Mathematics::EvaluateDot(V, BitV), Mathematics::EvaluateDot(V, Nrm) };
        };
        const auto LocalToWorld = [&](const Mathematics::Axis3D& L) noexcept -> Mathematics::Axis3D
        {
            return TanU * L.X + BitV * L.Y + Nrm * L.Z;
        };

        // Direct emissive hit (weighted by MIS on indirect bounces, weight 1.0 on camera/delta glass bounce)
        const auto Em = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);
        if (ComputeLinearLuminance(Em) > 1.0e-4f)
        {
            if (Bounce == 0u || PrevWasSpecular)
            {
                Radiance.R += Throughput.R * Em.R;
                Radiance.G += Throughput.G * Em.G;
                Radiance.B += Throughput.B * Em.B;
            }
            else if (Front)
            {
                const float PdfEmis = EvalEmisPdfSolid(RayO, RayD);
                const float Mis = GeometricRaster::PowerHeuristicMis(PrevBouncePdf, PdfEmis);
                Radiance.R += Throughput.R * Em.R * Mis;
                Radiance.G += Throughput.G * Em.G * Mis;
                Radiance.B += Throughput.B * Em.B * Mis;
            }
            break;
        }

        const auto LocalWo = WorldToLocal(Wo);
        if (LocalWo.Z <= 1.0e-4f)
        {
            break;
        }

        const uint32_t DimAnchor = Bounce * 4u;
        const auto SobEmis = GeometricRaster::Sobol02Sample(SampleIdx, PixelSeed, DimAnchor + 0u);
        const auto SobDiff = GeometricRaster::Sobol02Sample(SampleIdx, PixelSeed, DimAnchor + 1u);
        const auto SobSpec = GeometricRaster::Sobol02Sample(SampleIdx, PixelSeed, DimAnchor + 2u);
        const auto SobLobe = GeometricRaster::Sobol02Sample(SampleIdx, PixelSeed, DimAnchor + 3u);

        // Glass refraction/reflection
        if (Mat.Transmission.TransmissionWeight > 0.35f)
        {
            const float Ior   = std::max(1.01f, Mat.Specular.DielectricIOR);
            const float Eta   = Front ? (1.0f / Ior) : Ior;
            const float CosI  = ClampUnit(Mathematics::EvaluateDot(Wo, Nrm));
            const float Fres  = MaterialSpace::Scattering::EvaluateExactDielectricFresnel(CosI, Ior);
            const float Sin2T = Eta * Eta * std::max(0.0f, 1.0f - CosI * CosI);
            if (Sin2T >= 1.0f || SobSpec.X < Fres)
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
            PrevWasSpecular = true;
            PrevBouncePdf   = 1.0f;
            continue;
        }

        // Emissive Triangle CDF NEE with MIS
        if (!Bridge.Emissives.empty() && Bridge.TotalFlux > 1.0e-12)
        {
            const auto& Sel = SobEmis.X;
            auto It = std::lower_bound(Bridge.Emissives.begin(), Bridge.Emissives.end(), Sel,
                [](const EmissiveTriangleSource& S, float V) { return S.CdfUpper < V; });
            if (It == Bridge.Emissives.end()) --It;

            const auto TriUv = GeometricRaster::Sobol02Sample(SampleIdx, PixelSeed, DimAnchor + 3u);
            const float Sq = std::sqrt(ClampUnit(TriUv.X));
            const Mathematics::Axis3D Target = It->VertexA * (1.0f - Sq)
                                             + It->VertexB * (Sq * (1.0f - TriUv.Y))
                                             + It->VertexC * (Sq * TriUv.Y);
            const Mathematics::Axis3D ToE = Target - HitPos;
            const float Dist2 = Mathematics::EvaluateLengthSquared(ToE);
            const float Dist  = std::sqrt(std::max(1.0e-6f, Dist2));
            const Mathematics::Axis3D Dir = ToE * (1.0f / Dist);
            const float NdotL = Mathematics::EvaluateDot(Nrm, Dir);
            const float CosE  = Mathematics::EvaluateDot(It->UnitNormal, Dir * -1.0f);
            if (NdotL > 1.0e-4f && CosE > 1.0e-4f)
            {
                SpatialAcceleration::RaySegmentQuery VisQ {};
                VisQ.Origin          = HitPos + Nrm * 2.0e-3f;
                VisQ.UnitDirection   = Dir;
                VisQ.MinimumDistance = 1.0e-3f;
                VisQ.MaximumDistance = std::max(2.0e-3f, Dist - 4.0e-3f);
                if (!Scene.SceneTree.QueryRayOccluded(VisQ))
                {
                    const float PdfEmis = EvalEmisPdfSolid(HitPos, Dir);
                    const auto LocalWi  = WorldToLocal(Dir);
                    const float RoughS  = std::clamp(Mat.Specular.Roughness * Mat.Specular.Roughness, 0.0025f, 1.0f);
                    const float DiffW   = (1.0f - Mat.Specular.Metalness) * (1.0f - Mat.Transmission.TransmissionWeight);
                    const float PdfDiff = std::max(0.0f, LocalWi.Z) / Constants::PiFloat;
                    const float PdfSpec = GeometricRaster::EvaluateSphericalCapGgxVndfPdf(LocalWo, LocalWi, RoughS, RoughS);
                    const float DiffP   = std::clamp(DiffW * 0.65f, 0.0f, 0.85f);
                    const float PdfBsdf = DiffP * PdfDiff + (1.0f - DiffP) * PdfSpec;
                    const float Mis     = GeometricRaster::PowerHeuristicMis(PdfEmis, PdfBsdf);
                    const auto Bsdf     = MaterialSpace::Scattering::EvaluateOpenPbrLocalBsdf(
                        Mat, Lobes, LocalWo, LocalWi);
                    if (PdfEmis > 1.0e-6f)
                    {
                        const float G = (NdotL * Mis) / PdfEmis;
                        Radiance.R += Throughput.R * Bsdf.R * It->Radiance.R * G;
                        Radiance.G += Throughput.G * Bsdf.G * It->Radiance.G * G;
                        Radiance.B += Throughput.B * Bsdf.B * It->Radiance.B * G;
                    }
                }
            }
        }

        if (Bounce + 1u >= MaxBounces)
        {
            break;
        }

        const float RoughS   = std::clamp(Mat.Specular.Roughness * Mat.Specular.Roughness, 0.0025f, 1.0f);
        const float DiffW    = (1.0f - Mat.Specular.Metalness) * (1.0f - Mat.Transmission.TransmissionWeight);
        const float DiffProb = std::clamp(DiffW * 0.65f, 0.0f, 0.85f);
        const bool PickDiff  = (SobLobe.Y < DiffProb);

        Mathematics::Axis3D LocalWi {};
        if (PickDiff)
        {
            const float R = std::sqrt(ClampUnit(SobDiff.X));
            const float Phi = SobDiff.Y * Constants::TauFloat;
            LocalWi = { R * std::cos(Phi), R * std::sin(Phi), std::sqrt(std::max(0.0f, 1.0f - SobDiff.X)) };
        }
        else
        {
            LocalWi = GeometricRaster::SampleSphericalCapGgxVndfLocal(LocalWo, RoughS, RoughS, SobSpec.X, SobSpec.Y);
        }
        PrevWasSpecular = false;
        if (LocalWi.Z <= 1.0e-4f)
        {
            break;
        }

        const float PdfSpec = GeometricRaster::EvaluateSphericalCapGgxVndfPdf(LocalWo, LocalWi, RoughS, RoughS);
        const float PdfDiff = LocalWi.Z / Constants::PiFloat;
        const float MixPdf  = std::max(1.0e-4f, DiffProb * PdfDiff + (1.0f - DiffProb) * PdfSpec);
        PrevBouncePdf       = MixPdf;
        const auto Bsdf = MaterialSpace::Scattering::EvaluateOpenPbrLocalBsdf(
            Mat, Lobes, LocalWo, LocalWi);
        const float W = LocalWi.Z / MixPdf;
        Throughput.R *= Bsdf.R * W;
        Throughput.G *= Bsdf.G * W;
        Throughput.B *= Bsdf.B * W;
        if (std::max({ Throughput.R, Throughput.G, Throughput.B }) < 1.0e-4f)
        {
            break;
        }

        RayO = HitPos + Nrm * 2.0e-3f;
        RayD = Mathematics::NormalizeAxis(LocalToWorld(LocalWi));
    }

    return {
        std::isfinite(Radiance.R) ? std::max(0.0f, Radiance.R) : 0.0f,
        std::isfinite(Radiance.G) ? std::max(0.0f, Radiance.G) : 0.0f,
        std::isfinite(Radiance.B) ? std::max(0.0f, Radiance.B) : 0.0f,
    };
}

} // namespace Frontier::Exhibits
