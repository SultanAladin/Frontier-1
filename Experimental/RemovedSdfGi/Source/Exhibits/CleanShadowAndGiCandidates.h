/*============================================================================================================================================
                                                    CLEANSHADOWANDGICANDIDATES.H
============================================================================================================================================*/
// 🧩 Zero-Denoiser Soft Solar Shadow + Deforming Bone Capsule Architectures Paired with Clean Emissive GI:
//    - Before Baseline : Engine VoxelSdfClipmapVolume::TraceClipmapSdfPenumbra (shows AABB discontinuity & missing bone deformation)
//    - Candidate 1     : Hybrid Cone Shadow (0.28 m Contact BVH + SmoothMin Quadratic/Adaptive SDF + C0 Vehicle SDF + Bone Capsules)
//    - Candidate 2     : Virtual Surfel Occlusion Field (Stratified Blocker Disks + Bone Capsules + Contact BVH)
//    - Candidate 3     : World Voxel Clipmap Cone Shadow (3D Static/Dynamic Grid Lookup + Bone Capsules)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Exhibits/CleanEmissiveGiCandidates.h"

#include "Engine/Constants/MathConstants.h"
#include "Engine/DistanceFieldSpace/VoxelSdfClipmap.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::Exhibits
{

//------------------------------------------------------------------------------------------------------------------------
//                                               DEFORMING BONE CAPSULE RIG
//------------------------------------------------------------------------------------------------------------------------

struct DeformingBoneCapsule
{
    Mathematics::Axis3D JointStart  {};
    Mathematics::Axis3D JointEnd    {};
    float               RadiusStart = 0.06f;
    float               RadiusEnd   = 0.06f;

    [[nodiscard]] float EvaluateSignedDistance(const Mathematics::Axis3D& WorldPos) const noexcept
    {
        const Mathematics::Axis3D Seg  = JointEnd - JointStart;
        const Mathematics::Axis3D Rel  = WorldPos - JointStart;
        const float LenSq              = Mathematics::EvaluateDot(Seg, Seg);
        const float Frac               = (LenSq > 1.0e-8f)
            ? std::clamp(Mathematics::EvaluateDot(Rel, Seg) / LenSq, 0.0f, 1.0f)
            : 0.0f;
        const float RadiusAtFrac = RadiusStart + (RadiusEnd - RadiusStart) * Frac;
        return Mathematics::EvaluateLength(Rel - Seg * Frac) - RadiusAtFrac;
    }
};

struct ArticulatedFigureRig
{
    std::vector<DeformingBoneCapsule> Capsules {};
    Mathematics::Axis3D               RootCenterWorld {};

    [[nodiscard]] float EvaluateMinimumSignedDistance(const Mathematics::Axis3D& WorldPos) const noexcept
    {
        float BestD = 1.0e5f;
        for (const auto& Cap : Capsules)
        {
            BestD = std::min(BestD, Cap.EvaluateSignedDistance(WorldPos));
        }
        return BestD;
    }
};

inline void AppendTaperedLimbMesh(
    GeometrySpace::PolygonNetwork& Network,
    const DeformingBoneCapsule&    Cap,
    uint16_t                       MatSlot,
    uint32_t                       Sides = 10u)
{
    const Mathematics::Axis3D AxisVec = Cap.JointEnd - Cap.JointStart;
    const float AxisLen = Mathematics::EvaluateLength(AxisVec);
    if (AxisLen <= 1.0e-5f)
    {
        return;
    }
    const Mathematics::Axis3D Dir = AxisVec * (1.0f / AxisLen);
    Mathematics::Axis3D TanU {}, BitV {};
    ConstructTangentPair(Dir, TanU, BitV);

    constexpr uint32_t Rings = 4u;
    std::vector<uint32_t> RingVerts(static_cast<std::size_t>(Rings) * Sides, 0u);
    for (uint32_t R = 0u; R < Rings; ++R)
    {
        const float T   = static_cast<float>(R) / static_cast<float>(Rings - 1u);
        const auto  C   = Cap.JointStart + AxisVec * T;
        const float Rad = Cap.RadiusStart + (Cap.RadiusEnd - Cap.RadiusStart) * T;
        for (uint32_t S = 0u; S < Sides; ++S)
        {
            const float Ang = Constants::TauFloat * static_cast<float>(S) / static_cast<float>(Sides);
            const auto P = C + (TanU * std::cos(Ang) + BitV * std::sin(Ang)) * Rad;
            RingVerts[static_cast<std::size_t>(R) * Sides + S] = Network.AppendVertex(P);
        }
    }

    const uint32_t CapA = Network.AppendVertex(Cap.JointStart - Dir * Cap.RadiusStart);
    const uint32_t CapB = Network.AppendVertex(Cap.JointEnd   + Dir * Cap.RadiusEnd);

    for (uint32_t S = 0u; S < Sides; ++S)
    {
        const uint32_t SNext = (S + 1u) % Sides;
        const std::array<uint32_t, 3u> EndTriA { CapA, RingVerts[SNext], RingVerts[S] };
        (void)Network.AppendFace(EndTriA, {}, {}, 0u, MatSlot);

        const uint32_t TopOff = (Rings - 1u) * Sides;
        const std::array<uint32_t, 3u> EndTriB { CapB, RingVerts[TopOff + S], RingVerts[TopOff + SNext] };
        (void)Network.AppendFace(EndTriB, {}, {}, 0u, MatSlot);
    }

    for (uint32_t R = 0u; R + 1u < Rings; ++R)
    {
        for (uint32_t S = 0u; S < Sides; ++S)
        {
            const uint32_t SNext = (S + 1u) % Sides;
            const uint32_t V00 = RingVerts[static_cast<std::size_t>(R) * Sides + S];
            const uint32_t V01 = RingVerts[static_cast<std::size_t>(R) * Sides + SNext];
            const uint32_t V10 = RingVerts[static_cast<std::size_t>(R + 1u) * Sides + S];
            const uint32_t V11 = RingVerts[static_cast<std::size_t>(R + 1u) * Sides + SNext];
            const std::array<uint32_t, 3u> Tri0 { V00, V01, V11 };
            const std::array<uint32_t, 3u> Tri1 { V00, V11, V10 };
            (void)Network.AppendFace(Tri0, {}, {}, 0u, MatSlot);
            (void)Network.AppendFace(Tri1, {}, {}, 0u, MatSlot);
        }
    }
}

// Builds a live deforming articulated humanoid rig + polygonal geometry for PosePhase in [0, 1]
// Pose 0.0: standing stride beside ControlVehicle with raised greeting arm; Pose 1.0: forward athletic sprint/reach
[[nodiscard]] inline ArticulatedFigureRig BuildArticulatedFigurePose(
    float                           PosePhase,
    const Mathematics::Axis3D&      RootWorld,
    GeometrySpace::PolygonNetwork&  OutNetwork,
    uint16_t                        SuitMatSlot,
    uint16_t                        AccentMatSlot)
{
    ArticulatedFigureRig Rig {};
    Rig.RootCenterWorld = RootWorld;

    const float LeanX      = 0.06f + 0.16f * PosePhase;
    const float StrideL    = -0.22f + 0.56f * PosePhase;
    const float StrideR    =  0.22f - 0.52f * PosePhase;
    const float ArmWaveZ   = 1.62f + 0.34f * (1.0f - PosePhase);
    const float ArmReachX  = 0.14f + 0.44f * PosePhase;
    const float CrouchZ    = -0.04f * PosePhase;

    const Mathematics::Axis3D Pelvis { RootWorld.X + LeanX * 0.2f, RootWorld.Y, RootWorld.Z + 0.92f + CrouchZ };
    const Mathematics::Axis3D Chest  { RootWorld.X + LeanX * 0.7f, RootWorld.Y, RootWorld.Z + 1.38f + CrouchZ };
    const Mathematics::Axis3D Head   { RootWorld.X + LeanX * 1.1f, RootWorld.Y, RootWorld.Z + 1.68f + CrouchZ };

    const Mathematics::Axis3D LShoulder { Chest.X, Chest.Y - 0.24f, Chest.Z - 0.03f };
    const Mathematics::Axis3D LElbow    { LShoulder.X + ArmReachX * 0.6f, LShoulder.Y - 0.26f, 1.38f + (ArmWaveZ - 1.62f) * 0.5f };
    const Mathematics::Axis3D LHand     { LElbow.X + ArmReachX * 0.7f,    LElbow.Y - 0.16f,    ArmWaveZ };

    const Mathematics::Axis3D RShoulder { Chest.X, Chest.Y + 0.24f, Chest.Z - 0.03f };
    const Mathematics::Axis3D RElbow    { RShoulder.X - 0.16f * PosePhase, RShoulder.Y + 0.16f, 1.18f - 0.10f * PosePhase };
    const Mathematics::Axis3D RHand     { RElbow.X + 0.12f * (1.0f - PosePhase), RElbow.Y + 0.08f, 0.96f + 0.14f * PosePhase };

    const Mathematics::Axis3D LHip   { Pelvis.X, Pelvis.Y - 0.12f, Pelvis.Z - 0.04f };
    const Mathematics::Axis3D LKnee  { LHip.X + StrideL * 0.55f, LHip.Y - 0.02f, 0.48f + 0.08f * PosePhase };
    const Mathematics::Axis3D LFoot  { RootWorld.X + StrideL,     LHip.Y - 0.02f, 0.06f };

    const Mathematics::Axis3D RHip   { Pelvis.X, Pelvis.Y + 0.12f, Pelvis.Z - 0.04f };
    const Mathematics::Axis3D RKnee  { RHip.X + StrideR * 0.62f, RHip.Y + 0.02f, 0.48f + 0.14f * PosePhase };
    const Mathematics::Axis3D RFoot  { RootWorld.X + StrideR,     RHip.Y + 0.02f, 0.06f + 0.12f * PosePhase };

    const auto AddLimb = [&](Mathematics::Axis3D A, Mathematics::Axis3D B, float RA, float RB, uint16_t Mat)
    {
        const DeformingBoneCapsule Cap { A, B, RA, RB };
        Rig.Capsules.push_back(Cap);
        AppendTaperedLimbMesh(OutNetwork, Cap, Mat, 10u);
    };

    // Torso & head
    AddLimb(Pelvis, Chest, 0.15f, 0.19f, SuitMatSlot);
    AddLimb(Chest,  Head,  0.11f, 0.13f, AccentMatSlot);
    // Raised / reaching left arm + counter-balancing right arm
    AddLimb(LShoulder, LElbow, 0.075f, 0.065f, SuitMatSlot);
    AddLimb(LElbow,    LHand,  0.062f, 0.052f, AccentMatSlot);
    AddLimb(RShoulder, RElbow, 0.075f, 0.065f, SuitMatSlot);
    AddLimb(RElbow,    RHand,  0.062f, 0.052f, AccentMatSlot);
    // Striding / sprinting legs
    AddLimb(LHip,  LKnee, 0.095f, 0.078f, SuitMatSlot);
    AddLimb(LKnee, LFoot, 0.074f, 0.058f, SuitMatSlot);
    AddLimb(RHip,  RKnee, 0.095f, 0.078f, SuitMatSlot);
    AddLimb(RKnee, RFoot, 0.074f, 0.058f, SuitMatSlot);

    return Rig;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        SOLAR & OCCLUDER SHADOW CANDIDATE PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

struct DirectionalSolarSpec
{
    Mathematics::Axis3D    DirectionToSun { -0.46f, 0.56f, 0.68f };
    Mathematics::LinearRGB SolarIrradiance { 3.20f, 3.05f, 2.85f };
    float                  TanSunRadius   = 0.048f; // Soft ~2.75 deg solar disc to reveal distance-dependent penumbra
};

struct OccluderSurfelDisk
{
    Mathematics::Axis3D Position {};
    Mathematics::Axis3D Normal   {};
    float               RadiusM  = 0.18f;
};

struct SurfelOccluderField
{
    std::vector<OccluderSurfelDisk> Disks {};

    void BuildFromScene(
        const SpatialAcceleration::WideBoundingTree&    SceneTree,
        const Mathematics::Axis3D&                      CharacterRoot,
        float                                           ExcludeCharRadius = 1.45f)
    {
        Disks.clear();
        struct IndexedArea
        {
            uint32_t Idx;
            float    Area;
            Mathematics::Axis3D Center;
            Mathematics::Axis3D Normal;
        };
        std::vector<IndexedArea> Eligible {};
        double TotalArea = 0.0;
        for (uint32_t I = 0u; I < static_cast<uint32_t>(SceneTree.Primitives.size()); ++I)
        {
            const auto& Tri = SceneTree.Primitives[I];
            const auto V0 = Tri.Corner0;
            const auto V1 = Tri.Corner0 + Tri.Edge01;
            const auto V2 = Tri.Corner0 + Tri.Edge02;
            const auto C = (V0 + V1 + V2) * (1.0f / 3.0f);
            // Exclude outer studio walls/floor > 3.5 m wide and deforming character mesh (handled via live bone capsules)
            if (std::fabs(C.X) > 4.6f || std::fabs(C.Y) > 4.6f || C.Z < 0.03f || C.Z > 3.4f)
            {
                continue;
            }
            if (Mathematics::EvaluateLength(C - CharacterRoot) < ExcludeCharRadius)
            {
                continue;
            }
            const auto Cr = Mathematics::EvaluateCross(Tri.Edge01, Tri.Edge02);
            const float L = Mathematics::EvaluateLength(Cr);
            const float A = 0.5f * L;
            if (A <= 1.0e-5f)
            {
                continue;
            }
            Eligible.push_back(IndexedArea { I, A, C, Cr * (1.0f / L) });
            TotalArea += static_cast<double>(A);
        }
        if (Eligible.empty() || TotalArea <= 1.0e-6)
        {
            return;
        }

        // Spatial grid clustering into fine stratified blocker disks covering pillars & ControlVehicle
        struct CellAccum
        {
            Mathematics::Axis3D PosSum {};
            Mathematics::Axis3D NrmSum {};
            float               Area   = 0.0f;
            uint32_t            Count  = 0u;
        };
        constexpr float CellSize = 0.16f;
        constexpr int   NX = 70, NY = 62, NZ = 24;
        std::vector<CellAccum> Grid(static_cast<std::size_t>(NX) * NY * NZ);
        for (const auto& E : Eligible)
        {
            const int IX = std::clamp(static_cast<int>(std::floor((E.Center.X + 5.6f) / CellSize)), 0, NX - 1);
            const int IY = std::clamp(static_cast<int>(std::floor((E.Center.Y + 5.0f) / CellSize)), 0, NY - 1);
            const int IZ = std::clamp(static_cast<int>(std::floor((E.Center.Z + 0.1f) / CellSize)), 0, NZ - 1);
            auto& G = Grid[(static_cast<std::size_t>(IZ) * NY + IY) * NX + IX];
            G.PosSum = G.PosSum + E.Center * E.Area;
            G.NrmSum = G.NrmSum + E.Normal * E.Area;
            G.Area  += E.Area;
            G.Count += 1u;
        }

        for (const auto& G : Grid)
        {
            if (G.Count == 0u || G.Area <= 1.0e-5f)
            {
                continue;
            }
            OccluderSurfelDisk D {};
            D.Position = G.PosSum * (1.0f / G.Area);
            D.Normal   = Mathematics::NormalizeAxis(G.NrmSum, { 0.0f, 0.0f, 1.0f });
            D.RadiusM  = std::clamp(std::sqrt(G.Area / Constants::PiFloat) * 1.30f, 0.085f, 0.24f);
            Disks.push_back(D);
        }
    }

    // Smooth multiplicative quartic-kernel surfel transmittance (eliminates scalloped disk boundaries)
    [[nodiscard]] float EvaluateSoftDiskShadow(
        const Mathematics::Axis3D&                ReceiverP,
        const Mathematics::Axis3D&                ReceiverN,
        const Mathematics::Axis3D&                SunDir,
        float                                     TanSunRadius,
        const ArticulatedFigureRig&               CharacterRig,
        const SpatialAcceleration::WideBoundingTree* SceneTree) const noexcept
    {
        const float NdotL = Mathematics::EvaluateDot(ReceiverN, SunDir);
        if (NdotL <= 1.0e-4f)
        {
            return 0.0f;
        }
        if (SceneTree != nullptr)
        {
            SpatialAcceleration::RaySegmentQuery ContactRay {};
            ContactRay.Origin          = ReceiverP + ReceiverN * 0.003f;
            ContactRay.UnitDirection   = SunDir;
            ContactRay.MinimumDistance = 0.002f;
            ContactRay.MaximumDistance = 0.28f;
            if (SceneTree->QueryRayOccluded(ContactRay))
            {
                return 0.0f;
            }
        }

        const auto SoftMinFloat = [](float A, float B, float K) noexcept
        {
            const float H = std::max(K - std::fabs(A - B), 0.0f) / K;
            return std::min(A, B) - H * H * K * 0.25f;
        };

        float MinRatio = 2.0f;

        // 1. Deforming character bone capsules along the solar ray
        {
            float Travel = 0.05f;
            for (uint32_t S = 0u; S < 40u && Travel < 6.0f; ++S)
            {
                const auto SP = ReceiverP + SunDir * Travel;
                const float PlaneFade = SmoothHermite(0.015f, 0.10f, Mathematics::EvaluateDot(SP - ReceiverP, ReceiverN));
                const float CapD      = CharacterRig.EvaluateMinimumSignedDistance(SP);
                if (PlaneFade > 1.0e-4f)
                {
                    const float ConeR = std::max(0.016f, Travel * TanSunRadius);
                    const float Ratio = std::clamp(CapD / ConeR, -1.5f, 1.5f) * PlaneFade + 1.5f * (1.0f - PlaneFade);
                    MinRatio = SoftMinFloat(MinRatio, Ratio, 0.12f);
                }
                Travel += std::clamp(std::fabs(CapD) * 0.55f, 0.025f, 0.24f);
            }
        }
        const float CapsuleVis = SmoothHermite(-0.95f, 0.95f, MinRatio);
        if (CapsuleVis <= 0.002f)
        {
            return 0.0f;
        }

        // 2. Smooth C1 quartic-kernel multiplicative transmittance across stratified blocker surfel disks
        float DiskTrans = 1.0f;
        for (const auto& D : Disks)
        {
            const Mathematics::Axis3D Rel = D.Position - ReceiverP;
            const float T = Mathematics::EvaluateDot(Rel, SunDir);
            if (T <= 0.14f || T > 9.5f)
            {
                continue;
            }
            const float PlaneDist = Mathematics::EvaluateDot(Rel, ReceiverN);
            const float PlaneFade = SmoothHermite(0.02f, 0.14f, PlaneDist);
            if (PlaneFade <= 1.0e-4f)
            {
                continue;
            }
            const Mathematics::Axis3D Perp = Rel - SunDir * T;
            const float DistSq = Mathematics::EvaluateDot(Perp, Perp);
            const float ConeR  = std::max(0.038f, T * TanSunRadius);
            const float EffR   = D.RadiusM * 1.15f + ConeR * 1.25f;
            const float R2     = DistSq / (EffR * EffR);
            if (R2 < 1.0f)
            {
                const float W      = 1.0f - R2;
                const float Kernel = W * W;
                const float Proj   = std::max(0.45f, std::fabs(Mathematics::EvaluateDot(D.Normal, SunDir)));
                const float AreaR  = std::min(1.0f, (D.RadiusM * D.RadiusM * Proj) / (EffR * EffR));
                DiskTrans *= std::max(0.0f, 1.0f - Kernel * AreaR * PlaneFade * 1.65f);
                if (DiskTrans <= 0.002f)
                {
                    return 0.0f;
                }
            }
        }

        return std::min(CapsuleVis, DiskTrans);
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                  BEFORE / REFERENCE / CANDIDATE 1 & 3 SHADOW EVALUATORS
//------------------------------------------------------------------------------------------------------------------------

// Before baseline: calls Frontier's current Engine VoxelSdfClipmapVolume::TraceClipmapSdfPenumbra
[[nodiscard]] inline float EvaluateBeforeEngineSdfShadow(
    const DistanceFieldSpace::VoxelSdfClipmapVolume&                EngineClipmap,
    std::span<const DistanceFieldSpace::DynamicLocalSdfInstance>    DynamicInstances,
    const Mathematics::Axis3D&                                      WorldPos,
    const Mathematics::Axis3D&                                      SurfaceNormal,
    const DirectionalSolarSpec&                                     Sun) noexcept
{
    const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis(Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
    const auto Res = EngineClipmap.TraceClipmapSdfPenumbra(
        WorldPos, SurfaceNormal, SunDir, 18.0f, Sun.TanSunRadius, true, DynamicInstances, true);
    return std::clamp(Res.Visibility, 0.0f, 1.0f);
}

// 128-SPP Deterministic Vogel-Fibonacci BVH4 Solar Shadow Reference (zero denoiser)
[[nodiscard]] inline float EvaluateExactSolarShadowReference(
    const SpatialAcceleration::WideBoundingTree& SceneTree,
    const Mathematics::Axis3D&                   WorldPos,
    const Mathematics::Axis3D&                   SurfaceNormal,
    const DirectionalSolarSpec&                  Sun,
    uint32_t                                     Samples = 128u) noexcept
{
    const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis(Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
    if (Mathematics::EvaluateDot(SurfaceNormal, SunDir) <= 1.0e-4f)
    {
        return 0.0f;
    }
    Mathematics::Axis3D TanU {}, BitV {};
    ConstructTangentPair(SunDir, TanU, BitV);

    constexpr float GoldenAngle = 2.39996323f;
    const Mathematics::Axis3D RayOrigin = WorldPos + SurfaceNormal * 2.5e-3f;
    uint32_t Unoccluded = 0u;
    for (uint32_t S = 0u; S < Samples; ++S)
    {
        const float FracR = std::sqrt((static_cast<float>(S) + 0.5f) / static_cast<float>(Samples)) * Sun.TanSunRadius;
        const float Angle = static_cast<float>(S) * GoldenAngle;
        const auto RayDir = Mathematics::NormalizeAxis(
            SunDir + TanU * (FracR * std::cos(Angle)) + BitV * (FracR * std::sin(Angle)), SunDir);
        SpatialAcceleration::RaySegmentQuery Q {};
        Q.Origin          = RayOrigin;
        Q.UnitDirection   = RayDir;
        Q.MinimumDistance = 1.5e-3f;
        Q.MaximumDistance = 24.0f;
        if (!SceneTree.QueryRayOccluded(Q))
        {
            ++Unoccluded;
        }
    }
    return static_cast<float>(Unoccluded) / static_cast<float>(Samples);
}

// Candidate 1 Shadow: 0.28 m Contact BVH + SoftMin Directional Cone over C0 Smooth Vehicle SDF + Interior Pillars + Bone Capsules
[[nodiscard]] inline float EvaluateCandidate1HybridConeShadow(
    const SceneGeometryBridge&    Bridge,
    const ArticulatedFigureRig&   CharacterRig,
    const Mathematics::Axis3D&    WorldPos,
    const Mathematics::Axis3D&    SurfaceNormal,
    const DirectionalSolarSpec&   Sun) noexcept
{
    const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis(Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
    if (Mathematics::EvaluateDot(SurfaceNormal, SunDir) <= 1.0e-4f)
    {
        return 0.0f;
    }

    if (Bridge.SceneTree != nullptr)
    {
        SpatialAcceleration::RaySegmentQuery ContactRay {};
        ContactRay.Origin          = WorldPos + SurfaceNormal * 3.0e-3f;
        ContactRay.UnitDirection   = SunDir;
        ContactRay.MinimumDistance = 1.5e-3f;
        ContactRay.MaximumDistance = 0.28f;
        if (Bridge.SceneTree->QueryRayOccluded(ContactRay))
        {
            return 0.0f;
        }
    }

    const auto SoftMinFloat = [](float A, float B, float K) noexcept
    {
        const float H = std::max(K - std::fabs(A - B), 0.0f) / K;
        return std::min(A, B) - H * H * K * 0.25f;
    };

    float MinRatio = 2.0f;
    float Travel   = 0.04f;
    for (uint32_t S = 0u; S < 112u && Travel < 8.5f; ++S)
    {
        const Mathematics::Axis3D SP = WorldPos + SunDir * Travel;
        const float PlaneFade = SmoothHermite(0.015f, 0.10f, Mathematics::EvaluateDot(SP - WorldPos, SurfaceNormal));

        // 1. Signed interior static pillars
        float SignedStatic = 20.0f;
        for (const auto& Box : Bridge.StaticBoxes)
        {
            if (Box.HalfExtent.X > 3.5f || Box.HalfExtent.Y > 3.5f)
            {
                continue;
            }
            SignedStatic = std::min(SignedStatic, Bridge.EvaluateSignedBoxDistance(SP, Box));
        }

        // 2. Signed live deforming character bone capsules
        const float CapD    = CharacterRig.EvaluateMinimumSignedDistance(SP);
        const float SignedD = std::min(SignedStatic, CapD);

        // 3. C0-continuous unsigned dynamic vehicle SDF
        float DynD = 20.0f;
        for (const auto& Dyn : Bridge.DynamicInstances)
        {
            if (!Dyn.Enabled || Dyn.Asset == nullptr)
            {
                continue;
            }
            DynD = std::min(DynD, SceneGeometryBridge::SampleDynamicAssetSmoothDistance(Dyn, SP));
        }

        const float MinD = std::min(std::fabs(SignedD), DynD);
        if (PlaneFade > 1.0e-4f)
        {
            const float ConeR    = std::max(0.016f, Travel * Sun.TanSunRadius);
            const float SigRatio = std::clamp(SignedD / ConeR, -1.5f, 1.5f);
            // Floor dynamic SDF mid-threshold above half the 8.25 cm LocalSdf voxel size to eliminate grille voxel pinholes
            const float DynMid   = std::max(0.056f, ConeR * 0.65f);
            const float DynHalf  = std::max(0.024f, ConeR * 0.36f);
            const float DynRatio = std::clamp((DynD - DynMid) / DynHalf, -1.5f, 1.5f);
            const float RawRatio = std::min(SigRatio, DynRatio);
            const float Ratio    = RawRatio * PlaneFade + 1.5f * (1.0f - PlaneFade);
            MinRatio = SoftMinFloat(MinRatio, Ratio, 0.12f);
            if (MinRatio <= -1.0f)
            {
                return 0.0f;
            }
        }
        Travel += std::clamp(MinD * 0.34f, 0.014f, 0.14f);
    }

    return SmoothHermite(-0.95f, 0.95f, MinRatio);
}

// Candidate 3 Shadow: World-Space 3D Clipmap Voxel Grid Cone March + Live Deforming Bone Capsules
[[nodiscard]] inline float EvaluateCandidate3ClipmapConeShadow(
    const ContinuousFieldEmissiveGi& FieldGi,
    const ArticulatedFigureRig&      CharacterRig,
    const Mathematics::Axis3D&       WorldPos,
    const Mathematics::Axis3D&       SurfaceNormal,
    const DirectionalSolarSpec&      Sun) noexcept
{
    const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis(Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
    if (Mathematics::EvaluateDot(SurfaceNormal, SunDir) <= 1.0e-4f)
    {
        return 0.0f;
    }

    const auto SoftMinFloat = [](float A, float B, float K) noexcept
    {
        const float H = std::max(K - std::fabs(A - B), 0.0f) / K;
        return std::min(A, B) - H * H * K * 0.25f;
    };

    float MinRatio = 2.0f;
    float Travel   = 0.05f;
    for (uint32_t S = 0u; S < 64u && Travel < 8.5f; ++S)
    {
        const Mathematics::Axis3D SP = WorldPos + SunDir * Travel;
        const float PlaneFade = SmoothHermite(0.02f, 0.14f, Mathematics::EvaluateDot(SP - WorldPos, SurfaceNormal));

        // Sample world-space clipmap distance (excluding outer boundary floor/walls via Z > 0.16 m height guard)
        float ClipDist = 10.0f;
        if (SP.Z > 0.16f && std::fabs(SP.X) < 5.0f && std::fabs(SP.Y) < 4.5f)
        {
            const auto Sample0 = FieldGi.Mips[0].SampleHermite(SP);
            ClipDist = Sample0.SdfDist;
        }
        const float CapD = CharacterRig.EvaluateMinimumSignedDistance(SP);
        const float MinD = std::min(ClipDist, std::fabs(CapD));

        if (PlaneFade > 1.0e-4f)
        {
            const float ConeR    = std::max(0.025f, Travel * Sun.TanSunRadius);
            const float CapRatio = std::clamp(CapD / ConeR, -1.5f, 1.5f);
            const float VoxMid   = std::max(0.075f, ConeR * 0.72f);
            const float VoxHalf  = std::max(0.038f, ConeR * 0.45f);
            const float VoxRatio = std::clamp((ClipDist - VoxMid) / VoxHalf, -1.5f, 1.5f);
            const float RawRatio = std::min(CapRatio, VoxRatio);
            const float Ratio    = RawRatio * PlaneFade + 1.5f * (1.0f - PlaneFade);
            MinRatio = SoftMinFloat(MinRatio, Ratio, 0.14f);
            if (MinRatio <= -1.0f)
            {
                return 0.0f;
            }
        }
        Travel += std::clamp(MinD * 0.48f, 0.032f, 0.22f);
    }

    return SmoothHermite(-0.95f, 0.95f, MinRatio);
}

} // namespace Frontier::Exhibits
