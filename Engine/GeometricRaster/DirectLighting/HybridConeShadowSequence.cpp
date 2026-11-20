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

float EvaluateGtxHybridDirectionalShadow(
    const HybridOccluderEnvironment& Env,
    Mathematics::Axis3D              ShadingPos,
    Mathematics::Axis3D              ShadingNormal,
    Mathematics::Axis3D              DirectionToLight,
    const HybridConeShadowSetup&     Setup) noexcept
{
    const float NdotL = Mathematics::EvaluateDot(ShadingNormal, DirectionToLight);
    if (NdotL <= 0.0f)
    {
        return 0.0f;
    }

    // 1. Exact short-range BVH4 contact ray [0, ContactRangeMeters] resolves fine panel creases & wheel-arch self-shadow
    if (Env.SceneBvh != nullptr && Setup.ContactRangeMeters > 0.0f)
    {
        SpatialAcceleration::RaySegmentQuery ContactRay {};
        ContactRay.Origin          = ShadingPos + ShadingNormal * 2.5e-3f;
        ContactRay.UnitDirection   = DirectionToLight;
        ContactRay.MinimumDistance = 1.0e-3f;
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

    // 2. C1-smooth SDF + Capsule Rig directional soft cone beyond ContactRangeMeters
    const Mathematics::Axis3D MarchOrigin = ShadingPos + ShadingNormal * 0.015f;
    float RayT       = std::max(0.08f, Setup.ContactRangeMeters * 0.85f);
    float Visibility = 1.0f;

    for (uint32_t Step = 0u; Step < Setup.MaxMarchSteps && RayT < Setup.MaxTraceMeters; ++Step)
    {
        const Mathematics::Axis3D SampleP = MarchOrigin + DirectionToLight * RayT;
        const float Dist = EvaluateCombinedOccludersDistance(Env, SampleP, Setup.SmoothBlendMeters);
        if (Dist < -0.025f)
        {
            return 0.0f;
        }

        const float Ramp = std::clamp((RayT - Setup.ContactRangeMeters * 0.65f) / 0.22f, 0.0f, 1.0f);
        const float Soft = std::clamp(Setup.ConeSoftness * std::max(Dist, 0.0f) / RayT, 0.0f, 1.0f);
        Visibility       = std::min(Visibility, 1.0f - Ramp * (1.0f - Soft));
        if (Visibility < 0.008f)
        {
            return 0.0f;
        }
        RayT += std::clamp(Dist * 0.80f, 0.032f, 0.42f);
    }

    return Visibility * Visibility * (3.0f - 2.0f * Visibility);
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
    if (SegmentLen <= 1.0e-4f)
    {
        return 1.0f;
    }
    const Mathematics::Axis3D Dir = ToTarget * (1.0f / SegmentLen);

    // 1. Short-range exact BVH4 contact check [0, 0.28 m]
    if (Env.SceneBvh != nullptr)
    {
        SpatialAcceleration::RaySegmentQuery ContactRay {};
        ContactRay.Origin          = ShadingPos + ShadingNormal * 3.0e-3f;
        ContactRay.UnitDirection   = Dir;
        ContactRay.MinimumDistance = 1.5e-3f;
        ContactRay.MaximumDistance = std::min(0.28f, SegmentLen - 0.04f);
        if (ContactRay.MaximumDistance > ContactRay.MinimumDistance && Env.SceneBvh->QueryRayOccluded(ContactRay))
        {
            return 0.0f;
        }
    }

    if (Env.ForbidDistanceField)
    {
        return 1.0f;
    }

    // 2. Uniform 32-step optical-depth SDF cone integration (0.38 m start avoids self-capsule grazing; contact BVH4 covers [0, 0.28 m])
    const float TStart = 0.38f;
    const float TEnd   = SegmentLen - 0.10f;
    if (TEnd <= TStart)
    {
        return 1.0f;
    }

    constexpr uint32_t FixedSteps = 32u;
    const float StepLen   = (TEnd - TStart) / static_cast<float>(FixedSteps);
    const float SourceSpreadR     = std::max(0.05f, LuminaireRadiusMeters * 0.42f);
    const auto  StartPos  = ShadingPos + ShadingNormal * 0.02f;
    float OpticalDepth    = 0.0f;

    for (uint32_t K = 0u; K < FixedSteps; ++K)
    {
        const float T         = TStart + (static_cast<float>(K) + 0.5f) * StepLen;
        const float Frac      = T / SegmentLen;
        const float ConeR     = 0.03f + SourceSpreadR * Frac;
        const float Dist      = EvaluateCombinedOccludersDistance(Env, StartPos + Dir * T, 0.08f);
        const float Overlap   = std::clamp((ConeR - Dist) / (2.0f * ConeR), 0.0f, 1.0f);
        const float SmoothOvl = Overlap * Overlap * (3.0f - 2.0f * Overlap);
        OpticalDepth         += SmoothOvl * (StepLen / std::max(0.09f, ConeR * 0.90f));
        if (OpticalDepth > 6.5f)
        {
            return 0.0f;
        }
    }

    return std::exp(-OpticalDepth * 1.45f);
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
