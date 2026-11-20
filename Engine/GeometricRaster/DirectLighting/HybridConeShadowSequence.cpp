/*============================================================================================================================================
                                                       HYBRIDCONESHADOWSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Order 2 shadows: GtxLowEnd Hybrid Cone (0.28 m BVH4 contact + SmoothMin C0 SDF + Capsule Rig) vs HighEnd 0-SDF BVH4 Vogel Solar

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/HybridConeShadowSequence.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometricRaster::DirectLighting
{

namespace
{

[[nodiscard]] float EvaluateSmoothMinimum(float DistanceA, float DistanceB, float BlendRadius) noexcept
{
    if (BlendRadius <= 1.0e-5f)
    {
        return std::min(DistanceA, DistanceB);
    }
    const float H = std::clamp(0.5f + 0.5f * (DistanceB - DistanceA) / BlendRadius, 0.0f, 1.0f);
    return DistanceB * (1.0f - H) + DistanceA * H - BlendRadius * H * (1.0f - H);
}

[[nodiscard]] uint32_t HashMix32(uint32_t Seed) noexcept
{
    Seed ^= Seed >> 16u;
    Seed *= 0x7FEB352Du;
    Seed ^= Seed >> 15u;
    Seed *= 0x846CA68Bu;
    Seed ^= Seed >> 16u;
    return Seed;
}

[[nodiscard]] float SmoothHermite(float Edge0, float Edge1, float X) noexcept
{
    const float T = std::clamp((X - Edge0) / std::max(1.0e-6f, Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] float SoftMinFloat(float A, float B, float K) noexcept
{
    const float H = std::max(K - std::fabs(A - B), 0.0f) / K;
    return std::min(A, B) - H * H * K * 0.25f;
}

} // namespace

float EvaluateCombinedOccludersDistance(
    const HybridOccluderEnvironment& Env,
    Mathematics::Axis3D              WorldPos,
    float                            SmoothBlendMeters) noexcept
{
    if (Env.ForbidDistanceField)
    {
        return 20.0f;
    }

    float BestDist = 20.0f;
    for (const auto& Box : Env.InteriorBoxes)
    {
        if (Box.HalfExtent.X > 3.5f || Box.HalfExtent.Y > 3.5f)
        {
            continue;
        }
        BestDist = std::min(BestDist, Box.EvaluateSignedDistance(WorldPos));
    }

    if (Env.VehiclePlacement.ActiveToggle && Env.VehiclePlacement.VolumeAsset != nullptr)
    {
        const float CarD = DistanceFieldSpace::LocalSignedDistanceVolume::SampleSmoothWorldDistance(
            Env.VehiclePlacement, WorldPos);
        BestDist = EvaluateSmoothMinimum(BestDist, CarD, SmoothBlendMeters);
    }

    if (Env.CharacterRig != nullptr && !Env.CharacterRig->LimbCapsules.empty())
    {
        const float RigD = Env.CharacterRig->EvaluateMinimumSignedDistance(WorldPos);
        BestDist = EvaluateSmoothMinimum(BestDist, RigD, SmoothBlendMeters);
    }

    return BestDist;
}

float EvaluateInteriorOccluderDistance(
    const HybridOccluderEnvironment& Env,
    Mathematics::Axis3D              WorldPos) noexcept
{
    if (Env.ForbidDistanceField)
    {
        return 20.0f;
    }

    float BestDist = 20.0f;
    for (const auto& Box : Env.InteriorBoxes)
    {
        if (Box.HalfExtent.X > 3.5f || Box.HalfExtent.Y > 3.5f)
        {
            continue;
        }
        BestDist = std::min(BestDist, Box.EvaluateSignedDistance(WorldPos));
    }

    if (Env.CharacterRig != nullptr && !Env.CharacterRig->LimbCapsules.empty())
    {
        BestDist = std::min(BestDist, Env.CharacterRig->EvaluateMinimumSignedDistance(WorldPos));
    }

    if (Env.VehiclePlacement.ActiveToggle && Env.VehiclePlacement.VolumeAsset != nullptr)
    {
        BestDist = std::min(BestDist, DistanceFieldSpace::LocalSignedDistanceVolume::SampleSmoothWorldDistance(
            Env.VehiclePlacement, WorldPos));
    }

    return BestDist;
}

float EvaluateGtxHybridDirectionalShadow(
    const HybridOccluderEnvironment& Env,
    Mathematics::Axis3D              ShadingPos,
    Mathematics::Axis3D              ShadingNormal,
    Mathematics::Axis3D              DirectionToLight,
    const HybridConeShadowSetup&     Setup) noexcept
{
    const float NdotL = Mathematics::EvaluateDot(ShadingNormal, DirectionToLight);
    if (NdotL <= 1.0e-4f)
    {
        return 0.0f;
    }

    // 1. Exact short-range BVH4 contact ray [0, ContactRangeMeters] resolves fine panel creases & wheel-arch self-shadow
    if (Env.SceneBvh != nullptr && Setup.ContactRangeMeters > 0.0f)
    {
        SpatialAcceleration::RaySegmentQuery ContactRay {};
        ContactRay.Origin          = ShadingPos + ShadingNormal * 3.0e-3f;
        ContactRay.UnitDirection   = DirectionToLight;
        ContactRay.MinimumDistance = 1.5e-3f;
        ContactRay.MaximumDistance = Setup.ContactRangeMeters;
        if (Env.SceneBvh->QueryRayOccluded(ContactRay))
        {
            return 0.0f;
        }
    }

    if (Env.ForbidDistanceField)
    {
        return 1.0f;
    }

    // 2. Candidate 1 conservative cone march: signed analytical SDF (boxes + capsule rig) separated from unsigned vehicle SDF
    float MinRatio = 2.0f;
    float Travel   = 0.04f;
    for (uint32_t Step = 0u; Step < Setup.MaxMarchSteps && Travel < Setup.MaxTraceMeters; ++Step)
    {
        const Mathematics::Axis3D SampleP = ShadingPos + DirectionToLight * Travel;
        const float PlaneFade = SmoothHermite(0.015f, 0.10f, Mathematics::EvaluateDot(SampleP - ShadingPos, ShadingNormal));

        float SignedStatic = 20.0f;
        for (const auto& Box : Env.InteriorBoxes)
        {
            if (Box.HalfExtent.X > 3.5f || Box.HalfExtent.Y > 3.5f)
            {
                continue;
            }
            SignedStatic = std::min(SignedStatic, Box.EvaluateSignedDistance(SampleP));
        }

        const float RigD    = (Env.CharacterRig != nullptr && !Env.CharacterRig->LimbCapsules.empty())
            ? Env.CharacterRig->EvaluateMinimumSignedDistance(SampleP)
            : 20.0f;
        const float SignedD = std::min(SignedStatic, RigD);

        float DynD = 20.0f;
        if (Env.VehiclePlacement.ActiveToggle && Env.VehiclePlacement.VolumeAsset != nullptr)
        {
            DynD = DistanceFieldSpace::LocalSignedDistanceVolume::SampleSmoothWorldDistance(
                Env.VehiclePlacement, SampleP);
        }

        const float MinD = std::min(std::fabs(SignedD), DynD);
        if (PlaneFade > 1.0e-4f)
        {
            const float ConeR    = std::max(0.016f, Travel * Setup.TanSunRadius);
            const float SigRatio = std::clamp(SignedD / ConeR, -1.5f, 1.5f);
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

float EvaluateHighEndBvhDirectionalShadow(
    const SpatialAcceleration::CompressedWideTree& SceneBvh,
    Mathematics::Axis3D                            ShadingPos,
    Mathematics::Axis3D                            ShadingNormal,
    Mathematics::Axis3D                            DirectionToLight,
    uint32_t                                       PixelSeed,
    const HybridConeShadowSetup&                   Setup) noexcept
{
    if (Mathematics::EvaluateDot(ShadingNormal, DirectionToLight) <= 0.0f)
    {
        return 0.0f;
    }

    Mathematics::Axis3D TanU {}, BitV {};
    DistanceFieldSpace::ConstructLimbBasis(DirectionToLight, TanU, BitV);

    const uint32_t SampleCount = std::max(1u, Setup.VogelRaySamples);
    const float    GoldenAngle = 2.39996323f;
    const float    PhaseOffset = static_cast<float>(HashMix32(PixelSeed) & 0xFFFFu) * (Constants::TauFloat / 65536.0f);

    uint32_t Unoccluded = 0u;
    const Mathematics::Axis3D RayOrigin = ShadingPos + ShadingNormal * 2.5e-3f;
    for (uint32_t S = 0u; S < SampleCount; ++S)
    {
        Mathematics::Axis3D SampleDir = DirectionToLight;
        if (SampleCount > 1u && Setup.SunAngularRadius > 1.0e-5f)
        {
            const float Radius = Setup.SunAngularRadius * std::sqrt((static_cast<float>(S) + 0.5f) / static_cast<float>(SampleCount));
            const float Theta  = static_cast<float>(S) * GoldenAngle + PhaseOffset;
            SampleDir = Mathematics::NormalizeAxis(
                DirectionToLight + TanU * (Radius * std::cos(Theta)) + BitV * (Radius * std::sin(Theta)),
                DirectionToLight);
        }

        SpatialAcceleration::RaySegmentQuery ShadowRay {};
        ShadowRay.Origin          = RayOrigin;
        ShadowRay.UnitDirection   = SampleDir;
        ShadowRay.MinimumDistance = 1.5e-3f;
        ShadowRay.MaximumDistance = Setup.MaxTraceMeters * 2.0f;
        if (!SceneBvh.QueryRayOccluded(ShadowRay))
        {
            ++Unoccluded;
        }
    }

    return static_cast<float>(Unoccluded) / static_cast<float>(SampleCount);
}

float EvaluateGtxHybridLuminaireVisibility(
    const HybridOccluderEnvironment& Env,
    Mathematics::Axis3D              ShadingPos,
    Mathematics::Axis3D              ShadingNormal,
    Mathematics::Axis3D              LuminaireTargetPos,
    float                            LuminaireRadiusMeters) noexcept
{
    const Mathematics::Axis3D ToTarget = LuminaireTargetPos - ShadingPos;
    const float SegmentLen = Mathematics::EvaluateLength(ToTarget);
    if (SegmentLen <= 0.06f)
    {
        return 1.0f;
    }
    const Mathematics::Axis3D Dir = ToTarget * (1.0f / SegmentLen);
    if (Mathematics::EvaluateDot(ShadingNormal, Dir) <= 1.0e-4f)
    {
        return 0.0f;
    }

    const float SelfGuardM = 0.20f;
    const float LuminaireMargin = std::max(SelfGuardM, LuminaireRadiusMeters * 0.65f);
    const float StartT = 0.12f;
    const float EndT   = SegmentLen - LuminaireMargin;
    if (EndT <= StartT)
    {
        return 1.0f;
    }

    constexpr uint32_t StepCount = 32u;
    constexpr float    InvSteps  = 1.0f / static_cast<float>(StepCount);
    const float        StepDt    = (EndT - StartT) * InvSteps;
    const float        AngRadius = std::clamp(LuminaireRadiusMeters / std::max(0.5f, SegmentLen), 0.025f, 0.075f);

    float OpticalDepth = 0.0f;
    for (uint32_t Step = 0u; Step < StepCount; ++Step)
    {
        const float U = (static_cast<float>(Step) + 0.5f) * InvSteps;
        const float T = StartT + (EndT - StartT) * U;
        const Mathematics::Axis3D SampleP = ShadingPos + Dir * T;
        const float PlaneFade = SmoothHermite(0.02f, 0.16f, Mathematics::EvaluateDot(SampleP - ShadingPos, ShadingNormal));
        if (PlaneFade <= 1.0e-4f)
        {
            continue;
        }
        const float RampIn  = SmoothHermite(0.10f, 0.26f, T);
        const float RampOut = 1.0f - SmoothHermite(EndT - 0.14f, EndT, T);
        const float Weight  = RampIn * RampOut * PlaneFade;
        if (Weight <= 1.0e-4f)
        {
            continue;
        }

        const float Dist    = EvaluateInteriorOccluderDistance(Env, SampleP);
        const float ConeR   = std::max(0.036f, T * AngRadius);
        const float StepVis = SmoothHermite(-0.025f, ConeR * 0.72f, Dist + 0.015f);
        const float Occ     = (1.0f - StepVis) * Weight;
        OpticalDepth += 2.6f * Occ * (StepDt / std::max(0.08f, ConeR));
        if (OpticalDepth > 6.5f)
        {
            return 0.0f;
        }
    }

    float SoftVis = std::exp(-OpticalDepth);
    if (SoftVis <= 0.004f || Env.SceneBvh == nullptr)
    {
        return SoftVis;
    }

    const float ContactLimit = std::min(0.28f, std::max(0.01f, SegmentLen - SelfGuardM));
    if (ContactLimit > 0.02f)
    {
        SpatialAcceleration::RaySegmentQuery ContactRay {};
        ContactRay.Origin          = ShadingPos + ShadingNormal * 0.004f;
        ContactRay.UnitDirection   = Dir;
        ContactRay.MinimumDistance = 0.002f;
        ContactRay.MaximumDistance = ContactLimit;
        if (Env.SceneBvh->QueryRayOccluded(ContactRay))
        {
            return 0.0f;
        }
    }
    return SoftVis;
}

float EvaluateHighEndBvhLuminaireVisibility(
    const SpatialAcceleration::CompressedWideTree& SceneBvh,
    Mathematics::Axis3D                            ShadingPos,
    Mathematics::Axis3D                            ShadingNormal,
    Mathematics::Axis3D                            LuminaireTargetPos) noexcept
{
    const Mathematics::Axis3D ToTarget = LuminaireTargetPos - ShadingPos;
    const float SegmentLen = Mathematics::EvaluateLength(ToTarget);
    if (SegmentLen <= 0.05f)
    {
        return 1.0f;
    }
    SpatialAcceleration::RaySegmentQuery Ray {};
    Ray.Origin          = ShadingPos + ShadingNormal * 3.0e-3f;
    Ray.UnitDirection   = ToTarget * (1.0f / SegmentLen);
    Ray.MinimumDistance = 2.0e-3f;
    Ray.MaximumDistance = SegmentLen - 0.035f;
    return SceneBvh.QueryRayOccluded(Ray) ? 0.0f : 1.0f;
}

} // namespace Frontier::GeometricRaster::DirectLighting
