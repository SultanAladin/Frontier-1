/*============================================================================================================================================
                                                          ARTICULATEDCAPSULERIG.H
============================================================================================================================================*/
// 🧩 Order 2 GtxLowEnd live deforming tapered bone-capsule solver (< 0.01 ms pose update) and articulated humanoid generator

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Frontier::DistanceFieldSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) TaperedLimbCapsule32
{
    Mathematics::Axis3D JointStart  {};     // [m] - Proximal joint origin
    float               RadiusStart = 0.06f;// [m] - Proximal capsule radius
    Mathematics::Axis3D JointEnd    {};     // [m] - Distal joint origin
    float               RadiusEnd   = 0.06f;// [m] - Distal capsule radius

    [[nodiscard]] float EvaluateSignedDistance(Mathematics::Axis3D WorldPos) const noexcept
    {
        const Mathematics::Axis3D Segment = JointEnd - JointStart;
        const Mathematics::Axis3D Rel     = WorldPos - JointStart;
        const float LenSq                 = Mathematics::EvaluateDot(Segment, Segment);
        const float Fraction              = (LenSq > 1.0e-8f)
            ? std::clamp(Mathematics::EvaluateDot(Rel, Segment) / LenSq, 0.0f, 1.0f)
            : 0.0f;
        const float RadiusAtFraction = RadiusStart + (RadiusEnd - RadiusStart) * Fraction;
        return Mathematics::EvaluateLength(Rel - Segment * Fraction) - RadiusAtFraction;
    }
};

static_assert(sizeof(TaperedLimbCapsule32) == 32u, "TaperedLimbCapsule32 must occupy 32 GPU-aligned bytes.");

struct ArticulatedCapsuleRig
{
    std::vector<TaperedLimbCapsule32> LimbCapsules    {};
    Mathematics::Axis3D               RootCenterWorld {};

    [[nodiscard]] float EvaluateMinimumSignedDistance(Mathematics::Axis3D WorldPos) const noexcept
    {
        float BestDistance = 1.0e5f;
        for (const auto& Cap : LimbCapsules)
        {
            BestDistance = std::min(BestDistance, Cap.EvaluateSignedDistance(WorldPos));
        }
        return BestDistance;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                              HUMANOID POSE & POLYGON BUILDER
//------------------------------------------------------------------------------------------------------------------------

inline void ConstructLimbBasis(
    Mathematics::Axis3D  AxisUnit,
    Mathematics::Axis3D& OutTangent,
    Mathematics::Axis3D& OutBitangent) noexcept
{
    const float SignZ = (AxisUnit.Z >= 0.0f) ? 1.0f : -1.0f;
    const float CoeffA = -1.0f / (SignZ + AxisUnit.Z);
    const float CoeffB = AxisUnit.X * AxisUnit.Y * CoeffA;
    OutTangent   = { 1.0f + SignZ * AxisUnit.X * AxisUnit.X * CoeffA, SignZ * CoeffB, -SignZ * AxisUnit.X };
    OutBitangent = { CoeffB, SignZ + AxisUnit.Y * AxisUnit.Y * CoeffA, -AxisUnit.Y };
}

inline void AppendTaperedLimbGeometry(
    GeometrySpace::PolygonNetwork& Network,
    const TaperedLimbCapsule32&    Cap,
    uint16_t                       PartOrdinal,
    uint32_t                       RadialSides = 10u)
{
    const Mathematics::Axis3D AxisVec = Cap.JointEnd - Cap.JointStart;
    const float AxisLen = Mathematics::EvaluateLength(AxisVec);
    if (AxisLen <= 1.0e-5f)
    {
        return;
    }
    const Mathematics::Axis3D Dir = AxisVec * (1.0f / AxisLen);
    Mathematics::Axis3D TanU {}, BitV {};
    ConstructLimbBasis(Dir, TanU, BitV);

    constexpr uint32_t Rings = 4u;
    std::vector<uint32_t> RingVertices(static_cast<std::size_t>(Rings) * RadialSides, 0u);
    for (uint32_t R = 0u; R < Rings; ++R)
    {
        const float T      = static_cast<float>(R) / static_cast<float>(Rings - 1u);
        const auto  Center = Cap.JointStart + AxisVec * T;
        const float Radius = Cap.RadiusStart + (Cap.RadiusEnd - Cap.RadiusStart) * T;
        for (uint32_t S = 0u; S < RadialSides; ++S)
        {
            const float Angle = Constants::TauFloat * static_cast<float>(S) / static_cast<float>(RadialSides);
            const auto  Point = Center + (TanU * std::cos(Angle) + BitV * std::sin(Angle)) * Radius;
            RingVertices[static_cast<std::size_t>(R) * RadialSides + S] = Network.AppendVertex(Point);
        }
    }

    const uint32_t CapStart = Network.AppendVertex(Cap.JointStart - Dir * Cap.RadiusStart);
    const uint32_t CapEnd   = Network.AppendVertex(Cap.JointEnd   + Dir * Cap.RadiusEnd);

    for (uint32_t S = 0u; S < RadialSides; ++S)
    {
        const uint32_t NextS = (S + 1u) % RadialSides;
        const std::array<uint32_t, 3u> StartTri {{ CapStart, RingVertices[NextS], RingVertices[S] }};
        (void)Network.AppendFace(StartTri, {}, {}, 0u, PartOrdinal);

        const uint32_t TopOffset = (Rings - 1u) * RadialSides;
        const std::array<uint32_t, 3u> EndTri {{ CapEnd, RingVertices[TopOffset + S], RingVertices[TopOffset + NextS] }};
        (void)Network.AppendFace(EndTri, {}, {}, 0u, PartOrdinal);
    }

    for (uint32_t R = 0u; R + 1u < Rings; ++R)
    {
        for (uint32_t S = 0u; S < RadialSides; ++S)
        {
            const uint32_t NextS = (S + 1u) % RadialSides;
            const uint32_t V00   = RingVertices[static_cast<std::size_t>(R) * RadialSides + S];
            const uint32_t V01   = RingVertices[static_cast<std::size_t>(R) * RadialSides + NextS];
            const uint32_t V10   = RingVertices[static_cast<std::size_t>(R + 1u) * RadialSides + S];
            const uint32_t V11   = RingVertices[static_cast<std::size_t>(R + 1u) * RadialSides + NextS];
            const std::array<uint32_t, 3u> TriA {{ V00, V01, V11 }};
            const std::array<uint32_t, 3u> TriB {{ V00, V11, V10 }};
            (void)Network.AppendFace(TriA, {}, {}, 0u, PartOrdinal);
            (void)Network.AppendFace(TriB, {}, {}, 0u, PartOrdinal);
        }
    }
}

// Builds identical skeleton (fixed vertex & face count across all PoseFraction in [0, 1]) so HighEnd can refit BVH4 in < 0.08 ms
[[nodiscard]] inline ArticulatedCapsuleRig BuildArticulatedHumanoidPose(
    float                          PoseFraction,
    Mathematics::Axis3D            RootWorld,
    GeometrySpace::PolygonNetwork& OutNetwork,
    uint16_t                       SuitMaterialSlot,
    uint16_t                       AccentMaterialSlot)
{
    OutNetwork.Reset();
    ArticulatedCapsuleRig Rig {};
    Rig.RootCenterWorld = RootWorld;

    const float LeanX     = 0.06f + 0.18f * PoseFraction;
    const float StrideL   = -0.24f + 0.58f * PoseFraction;
    const float StrideR   =  0.24f - 0.54f * PoseFraction;
    const float ArmWaveZ  = 1.62f + 0.36f * (1.0f - PoseFraction);
    const float ArmReachX = 0.14f + 0.46f * PoseFraction;
    const float CrouchZ   = -0.05f * PoseFraction;

    const Mathematics::Axis3D Pelvis { RootWorld.X + LeanX * 0.2f, RootWorld.Y, RootWorld.Z + 0.92f + CrouchZ };
    const Mathematics::Axis3D Chest  { RootWorld.X + LeanX * 0.7f, RootWorld.Y, RootWorld.Z + 1.38f + CrouchZ };
    const Mathematics::Axis3D Head   { RootWorld.X + LeanX * 1.1f, RootWorld.Y, RootWorld.Z + 1.68f + CrouchZ };

    const Mathematics::Axis3D LShoulder { Chest.X, Chest.Y - 0.24f, Chest.Z - 0.03f };
    const Mathematics::Axis3D LElbow    { LShoulder.X + ArmReachX * 0.6f, LShoulder.Y - 0.26f, RootWorld.Z + 1.38f + (ArmWaveZ - 1.62f) * 0.5f };
    const Mathematics::Axis3D LHand     { LElbow.X + ArmReachX * 0.7f,    LElbow.Y - 0.16f,    RootWorld.Z + ArmWaveZ };

    const Mathematics::Axis3D RShoulder { Chest.X, Chest.Y + 0.24f, Chest.Z - 0.03f };
    const Mathematics::Axis3D RElbow    { RShoulder.X - 0.16f * PoseFraction, RShoulder.Y + 0.16f, RootWorld.Z + 1.18f - 0.10f * PoseFraction };
    const Mathematics::Axis3D RHand     { RElbow.X + 0.12f * (1.0f - PoseFraction), RElbow.Y + 0.08f, RootWorld.Z + 0.96f + 0.14f * PoseFraction };

    const Mathematics::Axis3D LHip  { Pelvis.X, Pelvis.Y - 0.12f, Pelvis.Z - 0.04f };
    const Mathematics::Axis3D LKnee { LHip.X + StrideL * 0.55f, LHip.Y - 0.02f, RootWorld.Z + 0.48f + 0.08f * PoseFraction };
    const Mathematics::Axis3D LFoot { RootWorld.X + StrideL,     LHip.Y - 0.02f, RootWorld.Z + 0.06f };

    const Mathematics::Axis3D RHip  { Pelvis.X, Pelvis.Y + 0.12f, Pelvis.Z - 0.04f };
    const Mathematics::Axis3D RKnee { RHip.X + StrideR * 0.62f, RHip.Y + 0.02f, RootWorld.Z + 0.48f + 0.14f * PoseFraction };
    const Mathematics::Axis3D RFoot { RootWorld.X + StrideR,     RHip.Y + 0.02f, RootWorld.Z + 0.06f + 0.12f * PoseFraction };

    const auto AppendLimb = [&](Mathematics::Axis3D Start, Mathematics::Axis3D End, float RStart, float REnd, uint16_t MatSlot)
    {
        const TaperedLimbCapsule32 Cap { Start, RStart, End, REnd };
        Rig.LimbCapsules.push_back(Cap);
        AppendTaperedLimbGeometry(OutNetwork, Cap, MatSlot, 10u);
    };

    AppendLimb(Pelvis,    Chest,  0.15f,  0.19f,  SuitMaterialSlot);
    AppendLimb(Chest,     Head,   0.11f,  0.13f,  AccentMaterialSlot);
    AppendLimb(LShoulder, LElbow, 0.075f, 0.065f, SuitMaterialSlot);
    AppendLimb(LElbow,    LHand,  0.062f, 0.052f, AccentMaterialSlot);
    AppendLimb(RShoulder, RElbow, 0.075f, 0.065f, SuitMaterialSlot);
    AppendLimb(RElbow,    RHand,  0.062f, 0.052f, AccentMaterialSlot);
    AppendLimb(LHip,      LKnee,  0.095f, 0.078f, SuitMaterialSlot);
    AppendLimb(LKnee,     LFoot,  0.074f, 0.058f, SuitMaterialSlot);
    AppendLimb(RHip,      RKnee,  0.095f, 0.078f, SuitMaterialSlot);
    AppendLimb(RKnee,     RFoot,  0.074f, 0.058f, SuitMaterialSlot);

    OutNetwork.LinkOppositeEdges();
    OutNetwork.RecomputeNormals();
    return Rig;
}

} // namespace Frontier::DistanceFieldSpace
