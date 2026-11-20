/*============================================================================================================================================
                                                       HYBRIDCONESHADOWSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Order 2 shadows: GtxLowEnd Hybrid Cone (0.28 m BVH4 contact + SoftMin C0 SDF + Capsule Rig) vs HighEnd 0-SDF BVH4 Vogel Solar

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

[[nodiscard]] float SmoothHermiteStep(float Edge0, float Edge1, float Value) noexcept
{
    const float T = std::clamp((Value - Edge0) / std::max(1.0e-6f, Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] float EvaluateSoftMinRatio(float RatioA, float RatioB, float BlendWidth) noexcept
{
    if (BlendWidth <= 1.0e-5f)
    {
        return std::min(RatioA, RatioB);
    }
    const float H = std::clamp(0.5f + 0.5f * (RatioB - RatioA) / BlendWidth, 0.0f, 1.0f);
    return RatioB * (1.0f - H) + RatioA * H - BlendWidth * H * (1.0f - H);
}

[[nodiscard]] bool IsActiveOccluderBox(const DistanceFieldSpace::ClosedOccluderBox& Box) noexcept
{
    // Guard against ground-plane / sub-floor receiver slabs (TopZ <= 5 cm) while keeping columns and studio walls
    return (Box.CenterWorld.Z + Box.HalfExtent.Z) > 0.05f;
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

float EvaluateSignedAnalyticOccluders(
    const HybridOccluderEnvironment& Env,
    Mathematics::Axis3D              WorldPos) noexcept
{
    if (Env.ForbidDistanceField)
    {
        return 20.0f;
    }

    float SignedD = 20.0f;
    for (const auto& Box : Env.InteriorBoxes)
    {
        if (IsActiveOccluderBox(Box))
        {
            SignedD = std::min(SignedD, Box.EvaluateSignedDistance(WorldPos));
        }
    }

    if (Env.CharacterRig != nullptr && !Env.CharacterRig->LimbCapsules.empty())
    {
        SignedD = std::min(SignedD, Env.CharacterRig->EvaluateMinimumSignedDistance(WorldPos));
    }

    return SignedD;
}

float EvaluateCombinedOccludersDistance(
    const HybridOccluderEnvironment& Env,
    Mathematics::Axis3D              WorldPos,
    float                            SmoothBlendMeters) noexcept
{
    (void)SmoothBlendMeters;
    if (Env.ForbidDistanceField)
    {
        return 20.0f;
    }

    float BestDist = EvaluateSignedAnalyticOccluders(Env, WorldPos);
    if (Env.VehiclePlacement.ActiveToggle && Env.VehiclePlacement.VolumeAsset != nullptr)
    {
        const float HalfShell = Env.VehiclePlacement.VolumeAsset->QueryHalfVoxelShellMeters();
        const float CarD      = DistanceFieldSpace::LocalSignedDistanceVolume::SampleSmoothWorldDistance(
            Env.VehiclePlacement, WorldPos) - HalfShell;
        BestDist = std::min(BestDist, CarD);
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

    // 1. Exact short-range BVH4 contact ray [2 mm, ContactRangeMeters] resolves tyre/shoe contact & fine panel creases
    if (Env.SceneBvh != nullptr && Setup.ContactRangeMeters > 0.0f)
    {
        SpatialAcceleration::RaySegmentQuery ContactRay {};
        ContactRay.Origin          = ShadingPos + ShadingNormal * 3.0e-3f;
        ContactRay.UnitDirection   = DirectionToLight;
        ContactRay.MinimumDistance = 2.0e-3f;
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

    // 2. Candidate 1 Hybrid Cone March: separate signed capsules/columns from shell-aware unsigned C0 vehicle SDF,
    //    modulated by tangent-plane clearance PlaneFade and accumulated via quadratic SoftMin in normalized cone-ratio space.
    const bool  HasVehicle     = Env.VehiclePlacement.ActiveToggle && (Env.VehiclePlacement.VolumeAsset != nullptr);
    const float HalfVoxelShell = HasVehicle ? Env.VehiclePlacement.VolumeAsset->QueryHalfVoxelShellMeters() : 0.056f;
    const float TanSunRadius   = std::max(0.014f, Setup.SunAngularRadius);
    const float SoftBlend      = std::max(0.04f, Setup.SmoothBlendMeters);
    const auto  ShiftedOrigin  = ShadingPos + ShadingNormal * 6.0e-3f;

    float Travel   = 0.04f;
    float MinRatio = 1.5f;

    for (uint32_t Step = 0u; Step < Setup.MaxMarchSteps && Travel < Setup.MaxTraceMeters; ++Step)
    {
        const Mathematics::Axis3D SampleP = ShiftedOrigin + DirectionToLight * Travel;
        const float ConeR     = std::max(0.014f, Travel * TanSunRadius);
        const float PlaneFade = SmoothHermiteStep(
            0.015f, 0.10f, Mathematics::EvaluateDot(SampleP - ShadingPos, ShadingNormal));

        const float SignedD  = EvaluateSignedAnalyticOccluders(Env, SampleP);
        const float SigRatio = std::clamp(SignedD / ConeR, -1.5f, 1.5f);

        float StepRatio = SigRatio;
        float DynD      = 20.0f;
        if (HasVehicle)
        {
            DynD = DistanceFieldSpace::LocalSignedDistanceVolume::SampleSmoothWorldDistance(
                Env.VehiclePlacement, SampleP);
            const float DynMid   = std::max(HalfVoxelShell, ConeR * 0.65f);
            const float DynHalf  = std::max(0.024f, ConeR * 0.36f);
            const float DynRatio = std::clamp((DynD - DynMid) / DynHalf, -1.5f, 1.5f);
            StepRatio            = EvaluateSoftMinRatio(SigRatio, DynRatio, SoftBlend);
        }

        const float ModStep = 1.5f + (StepRatio - 1.5f) * PlaneFade;
        MinRatio            = std::min(MinRatio, ModStep);

        if (MinRatio <= -1.0f)
        {
            return 0.0f;
        }

        const float ClosestSafe = std::min(std::max(0.0f, SignedD), DynD);
        Travel += std::clamp(ClosestSafe * 0.34f, 0.014f, 0.14f);
    }

    return SmoothHermiteStep(-0.95f, 0.95f, MinRatio);
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
    const float FullDist = Mathematics::EvaluateLength(ToTarget);
    if (FullDist <= 0.08f)
    {
        return 1.0f;
    }
    const Mathematics::Axis3D Dir = ToTarget * (1.0f / FullDist);
    if (Mathematics::EvaluateDot(ShadingNormal, Dir) <= 1.0e-4f)
    {
        return 0.0f;
    }

    const float LuminaireMargin = std::max(0.22f, LuminaireRadiusMeters * 0.65f);

    // 1. Exact short-range BVH4 contact ray [2 mm, min(0.28 m, FullDist - LuminaireMargin)]
    if (Env.SceneBvh != nullptr)
    {
        const float ContactLimit = std::min(0.28f, FullDist - std::min(0.18f, LuminaireMargin));
        if (ContactLimit > 0.02f)
        {
            SpatialAcceleration::RaySegmentQuery ContactRay {};
            ContactRay.Origin          = ShadingPos + ShadingNormal * 4.0e-3f;
            ContactRay.UnitDirection   = Dir;
            ContactRay.MinimumDistance = 2.0e-3f;
            ContactRay.MaximumDistance = ContactLimit;
            if (Env.SceneBvh->QueryRayOccluded(ContactRay))
            {
                return 0.0f;
            }
        }
    }

    if (Env.ForbidDistanceField)
    {
        return 1.0f;
    }

    // 2. Smooth 32-step ring-free optical-depth cone stopping outside the luminaire's own SDF shell (zero self-occlusion arc)
    const float TStart = 0.12f;
    const float TEnd   = FullDist - LuminaireMargin;
    if (TEnd <= TStart)
    {
        return 1.0f;
    }

    constexpr uint32_t StepCount = 32u;
    constexpr float    InvSteps  = 1.0f / static_cast<float>(StepCount);
    const float        StepDt    = (TEnd - TStart) * InvSteps;
    const float        AngRadius = std::clamp(LuminaireRadiusMeters / std::max(0.5f, FullDist), 0.025f, 0.075f);
    const bool         HasCar    = Env.VehiclePlacement.ActiveToggle && (Env.VehiclePlacement.VolumeAsset != nullptr);

    float OpticalDepth = 0.0f;
    for (uint32_t Step = 0u; Step < StepCount; ++Step)
    {
        const float U         = (static_cast<float>(Step) + 0.5f) * InvSteps;
        const float T         = TStart + (TEnd - TStart) * U;
        const auto  SampleP   = ShadingPos + Dir * T;
        const float PlaneFade = SmoothHermiteStep(0.02f, 0.16f, Mathematics::EvaluateDot(SampleP - ShadingPos, ShadingNormal));
        if (PlaneFade <= 1.0e-4f)
        {
            continue;
        }
        const float RampIn  = SmoothHermiteStep(0.10f, 0.26f, T);
        const float RampOut = 1.0f - SmoothHermiteStep(TEnd - 0.14f, TEnd, T);
        const float Weight  = RampIn * RampOut * PlaneFade;
        if (Weight <= 1.0e-4f)
        {
            continue;
        }

        float Dist = EvaluateSignedAnalyticOccluders(Env, SampleP);
        if (HasCar)
        {
            const float VehicleDistance = DistanceFieldSpace::LocalSignedDistanceVolume::SampleSmoothWorldDistance(
                Env.VehiclePlacement, SampleP);
            Dist = std::min(Dist, VehicleDistance);
        }

        const float ConeR   = std::max(0.036f, T * AngRadius);
        const float StepVis = SmoothHermiteStep(-0.025f, ConeR * 0.72f, Dist + 0.015f);
        const float Occ     = (1.0f - StepVis) * Weight;
        OpticalDepth       += 2.6f * Occ * (StepDt / std::max(0.08f, ConeR));
        if (OpticalDepth > 6.5f)
        {
            return 0.0f;
        }
    }

    return std::exp(-OpticalDepth);
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
