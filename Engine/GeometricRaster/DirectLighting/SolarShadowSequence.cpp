/*============================================================================================================================================
                                                           SOLARSHADOWSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Tiered Direct Solar Shadows (Zero VSM!): RTX Vogel Solar Frustum + Plane-Aware Filter & GTX 3.9 mm 2-Cascade Contact Clip + SDF Cone

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/SolarShadowSequence.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/DistanceFieldSpace/VoxelSdfClipmap.h"
#include "Engine/GeometrySpace/Analysis/CurvatureAnalysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Frontier::GeometricRaster
{

namespace
{

[[nodiscard]] uint32_t HashPixelSeed(uint32_t PX, uint32_t PY) noexcept
{
    uint32_t State = PX * 0x9E3779B1u ^ (PY * 0x85EBCA77u);
    State ^= State >> 16u;
    State *= 0x7FEB352Du;
    State ^= State >> 15u;
    return State;
}

void RasterizeTreeIntoOrthographicCascade(
    const SpatialAcceleration::WideBoundingTree& SceneTree,
    OrthographicShadowCascade&                   InOutCascade)
{
    const uint32_t Res = std::max(16u, InOutCascade.ResolutionTexels);
    InOutCascade.ResolutionTexels = Res;
    InOutCascade.ClosestLightDepth.assign(static_cast<std::size_t>(Res) * Res, std::numeric_limits<float>::infinity());

    const float HalfSpan  = std::max(0.05f, InOutCascade.HalfSpanMeters);
    const float InvSpan2  = 1.0f / (2.0f * HalfSpan);
    const float NearPlane = -InOutCascade.DepthExtentMeters;

    const auto ProjectLightPoint = [&](Mathematics::Axis3D WorldP, float& OutTX, float& OutTY, float& OutDepthM) noexcept
    {
        const Mathematics::Axis3D Rel = WorldP - InOutCascade.CenterWorld;
        const float LocalU = Mathematics::EvaluateDot(Rel, InOutCascade.LightRightAxis);
        const float LocalV = Mathematics::EvaluateDot(Rel, InOutCascade.LightUpAxis);
        const float LocalZ = Mathematics::EvaluateDot(Rel, InOutCascade.LightForwardAxis);
        OutTX     = ((LocalU + HalfSpan) * InvSpan2) * static_cast<float>(Res);
        OutTY     = ((HalfSpan - LocalV) * InvSpan2) * static_cast<float>(Res);
        OutDepthM = LocalZ - NearPlane;
    };

    for (const auto& Prim : SceneTree.Primitives)
    {
        if (Prim.OpaqueMask == 0u)
        {
            continue;
        }
        const Mathematics::Axis3D P0 = Prim.Corner0;
        const Mathematics::Axis3D P1 = Prim.Corner0 + Prim.Edge01;
        const Mathematics::Axis3D P2 = Prim.Corner0 + Prim.Edge02;

        float X0 = 0.0f, Y0 = 0.0f, Z0 = 0.0f;
        float X1 = 0.0f, Y1 = 0.0f, Z1 = 0.0f;
        float X2 = 0.0f, Y2 = 0.0f, Z2 = 0.0f;
        ProjectLightPoint(P0, X0, Y0, Z0);
        ProjectLightPoint(P1, X1, Y1, Z1);
        ProjectLightPoint(P2, X2, Y2, Z2);

        const float Area2 = (X1 - X0) * (Y2 - Y0) - (Y1 - Y0) * (X2 - X0);
        if (std::fabs(Area2) <= 1.0e-7f)
        {
            continue;
        }

        const int MinX = std::max(0, static_cast<int>(std::floor(std::min({ X0, X1, X2 }))));
        const int MaxX = std::min(static_cast<int>(Res) - 1, static_cast<int>(std::ceil(std::max({ X0, X1, X2 }))));
        const int MinY = std::max(0, static_cast<int>(std::floor(std::min({ Y0, Y1, Y2 }))));
        const int MaxY = std::min(static_cast<int>(Res) - 1, static_cast<int>(std::ceil(std::max({ Y0, Y1, Y2 }))));
        if (MinX > MaxX || MinY > MaxY)
        {
            continue;
        }

        const float InvArea2 = 1.0f / Area2;
        const float Sign     = (Area2 < 0.0f) ? -1.0f : 1.0f;

        for (int TY = MinY; TY <= MaxY; ++TY)
        {
            const float SY = static_cast<float>(TY) + 0.5f;
            for (int TX = MinX; TX <= MaxX; ++TX)
            {
                const float SX = static_cast<float>(TX) + 0.5f;
                const float W0 = (X1 - SX) * (Y2 - SY) - (Y1 - SY) * (X2 - SX);
                const float W1 = (X2 - SX) * (Y0 - SY) - (Y2 - SY) * (X0 - SX);
                const float W2 = (X0 - SX) * (Y1 - SY) - (Y0 - SY) * (X1 - SX);
                if (W0 * Sign >= -1.0e-5f && W1 * Sign >= -1.0e-5f && W2 * Sign >= -1.0e-5f)
                {
                    const float B0 = W0 * InvArea2;
                    const float B1 = W1 * InvArea2;
                    const float B2 = 1.0f - B0 - B1;
                    const float D  = B0 * Z0 + B1 * Z1 + B2 * Z2;
                    float& Cell    = InOutCascade.ClosestLightDepth[static_cast<std::size_t>(TY) * Res + static_cast<uint32_t>(TX)];
                    Cell           = std::min(Cell, D);
                }
            }
        }
    }
}

struct CascadeSampleOutcome
{
    bool  InsideCascade     = false;
    float Visibility        = 1.0f;
    float BlockerDistanceM  = 0.0f;
};

[[nodiscard]] bool ProjectIntoCascade(
    const OrthographicShadowCascade& Cascade,
    Mathematics::Axis3D              BiasedWorldPos,
    float&                           OutCenterTX,
    float&                           OutCenterTY,
    float&                           OutReceiverD,
    float&                           OutTexelSizeM) noexcept
{
    if (Cascade.ClosestLightDepth.empty() || Cascade.ResolutionTexels == 0u)
    {
        return false;
    }
    const Mathematics::Axis3D Rel = BiasedWorldPos - Cascade.CenterWorld;
    const float LocalU = Mathematics::EvaluateDot(Rel, Cascade.LightRightAxis);
    const float LocalV = Mathematics::EvaluateDot(Rel, Cascade.LightUpAxis);
    const float LocalZ = Mathematics::EvaluateDot(Rel, Cascade.LightForwardAxis);
    const float Margin = Cascade.HalfSpanMeters * 0.94f;
    if (std::fabs(LocalU) > Margin || std::fabs(LocalV) > Margin)
    {
        return false;
    }

    const float ResF     = static_cast<float>(Cascade.ResolutionTexels);
    const float InvSpan2 = 1.0f / (2.0f * Cascade.HalfSpanMeters);
    OutCenterTX          = ((LocalU + Cascade.HalfSpanMeters) * InvSpan2) * ResF;
    OutCenterTY          = ((Cascade.HalfSpanMeters - LocalV) * InvSpan2) * ResF;
    OutReceiverD         = LocalZ - (-Cascade.DepthExtentMeters);
    OutTexelSizeM        = (2.0f * Cascade.HalfSpanMeters) / ResF;
    return true;
}

[[nodiscard]] CascadeSampleOutcome SampleCascadePcf3x3(
    const OrthographicShadowCascade& Cascade,
    Mathematics::Axis3D              BiasedWorldPos,
    float                            SlopeScale) noexcept
{
    CascadeSampleOutcome Out {};
    float CX = 0.0f, CY = 0.0f, RecD = 0.0f, TexelM = 0.0f;
    if (!ProjectIntoCascade(Cascade, BiasedWorldPos, CX, CY, RecD, TexelM))
    {
        return Out;
    }
    Out.InsideCascade  = true;
    const float Bias   = TexelM * (1.25f + 0.95f * SlopeScale);
    const int BaseX    = static_cast<int>(std::floor(CX));
    const int BaseY    = static_cast<int>(std::floor(CY));
    const int MaxIdx   = static_cast<int>(Cascade.ResolutionTexels) - 1;

    constexpr float KernelW[3] { 0.25f, 0.50f, 0.25f };
    float VisSum = 0.0f;
    float BlkSum = 0.0f;
    float BlkW   = 0.0f;

    for (int DY = -1; DY <= 1; ++DY)
    {
        const int TY = std::clamp(BaseY + DY, 0, MaxIdx);
        for (int DX = -1; DX <= 1; ++DX)
        {
            const int TX = std::clamp(BaseX + DX, 0, MaxIdx);
            const float W = KernelW[DY + 1] * KernelW[DX + 1];
            const float ClosestD = Cascade.ClosestLightDepth[static_cast<std::size_t>(TY) * Cascade.ResolutionTexels + static_cast<uint32_t>(TX)];
            const float Diff     = RecD - ClosestD;
            if (Diff > Bias)
            {
                BlkSum += Diff * W;
                BlkW   += W;
            }
            else
            {
                VisSum += W;
            }
        }
    }
    Out.Visibility       = VisSum;
    Out.BlockerDistanceM = (BlkW > 0.0f) ? (BlkSum / BlkW) : 0.0f;
    return Out;
}

[[nodiscard]] CascadeSampleOutcome SampleCascadePcss(
    const OrthographicShadowCascade& Cascade,
    Mathematics::Axis3D              BiasedWorldPos,
    float                            SlopeScale,
    float                            TanSunRadius,
    uint32_t                         RotationSeed) noexcept
{
    CascadeSampleOutcome Out {};
    float CX = 0.0f, CY = 0.0f, RecD = 0.0f, TexelM = 0.0f;
    if (!ProjectIntoCascade(Cascade, BiasedWorldPos, CX, CY, RecD, TexelM))
    {
        return Out;
    }
    Out.InsideCascade  = true;
    const float Bias   = TexelM * (1.2f + 0.9f * SlopeScale);
    const int MaxIdx   = static_cast<int>(Cascade.ResolutionTexels) - 1;
    const float RotPhi = static_cast<float>(RotationSeed & 0xFFFFu) * (Constants::TauFloat / 65536.0f);
    constexpr float GoldenAngle = 2.39996323f;

    // 1. Vogel Blocker Search over light frustum cone
    constexpr uint32_t SearchTaps = 12u;
    const float SearchRadiusPx    = std::clamp((TanSunRadius * 3.5f) / TexelM, 2.0f, 18.0f);
    float BlockerSum = 0.0f;
    uint32_t Blockers = 0u;
    for (uint32_t Tap = 0u; Tap < SearchTaps; ++Tap)
    {
        const float R   = std::sqrt((static_cast<float>(Tap) + 0.5f) / static_cast<float>(SearchTaps)) * SearchRadiusPx;
        const float Ang = static_cast<float>(Tap) * GoldenAngle + RotPhi;
        const int TX    = std::clamp(static_cast<int>(std::floor(CX + R * std::cos(Ang))), 0, MaxIdx);
        const int TY    = std::clamp(static_cast<int>(std::floor(CY + R * std::sin(Ang))), 0, MaxIdx);
        const float D   = Cascade.ClosestLightDepth[static_cast<std::size_t>(TY) * Cascade.ResolutionTexels + static_cast<uint32_t>(TX)];
        const float Diff = RecD - D;
        if (Diff > Bias)
        {
            BlockerSum += Diff;
            ++Blockers;
        }
    }

    if (Blockers == 0u)
    {
        Out.Visibility       = 1.0f;
        Out.BlockerDistanceM = 0.0f;
        return Out;
    }

    const float MeanBlockerD  = BlockerSum / static_cast<float>(Blockers);
    Out.BlockerDistanceM      = MeanBlockerD;

    // 2. Physical Penumbra Width in Cascade Texels
    const float PenumbraM     = MeanBlockerD * TanSunRadius * 2.15f;
    const float FilterRadiusPx = std::clamp(PenumbraM / TexelM, 1.15f, 22.0f);

    // 3. 16-Tap Vogel PCF with Receiver-Plane Bias Scaling
    constexpr uint32_t PcfTaps = 16u;
    float LitSum = 0.0f;
    for (uint32_t Tap = 0u; Tap < PcfTaps; ++Tap)
    {
        const float R   = std::sqrt((static_cast<float>(Tap) + 0.5f) / static_cast<float>(PcfTaps)) * FilterRadiusPx;
        const float Ang = static_cast<float>(Tap) * GoldenAngle + RotPhi + 0.71f;
        const int TX    = std::clamp(static_cast<int>(std::floor(CX + R * std::cos(Ang))), 0, MaxIdx);
        const int TY    = std::clamp(static_cast<int>(std::floor(CY + R * std::sin(Ang))), 0, MaxIdx);
        const float D   = Cascade.ClosestLightDepth[static_cast<std::size_t>(TY) * Cascade.ResolutionTexels + static_cast<uint32_t>(TX)];
        const float AdaptiveBias = Bias + TexelM * 0.16f * R;
        if ((RecD - D) <= AdaptiveBias)
        {
            LitSum += 1.0f;
        }
    }
    Out.Visibility = LitSum / static_cast<float>(PcfTaps);
    return Out;
}

[[nodiscard]] CascadeSampleOutcome SampleCascadeShadowRayMarch(
    const OrthographicShadowCascade& Cascade,
    Mathematics::Axis3D              BiasedWorldPos,
    float                            SlopeScale,
    float                            TanSunRadius,
    uint32_t                         RotationSeed) noexcept
{
    CascadeSampleOutcome Out {};
    float CX = 0.0f, CY = 0.0f, RecD = 0.0f, TexelM = 0.0f;
    if (!ProjectIntoCascade(Cascade, BiasedWorldPos, CX, CY, RecD, TexelM))
    {
        return Out;
    }
    Out.InsideCascade  = true;
    const int MaxIdx   = static_cast<int>(Cascade.ResolutionTexels) - 1;
    const float Bias   = TexelM * (1.25f + 0.85f * SlopeScale);
    const float RotPhi = static_cast<float>(RotationSeed & 0xFFFFu) * (Constants::TauFloat / 65536.0f);
    constexpr float GoldenAngle = 2.39996323f;

    // EEVEE-Next Quadratic Shadow Map Ray March (SMRT): 8 rays x 16 quadratic steps with segment-crossing continuity
    constexpr uint32_t SmrtRays  = 8u;
    constexpr uint32_t SmrtSteps = 16u;
    const float MaxMarchWorldM   = 6.50f;
    const float MaxStepTexels    = std::clamp((MaxMarchWorldM * TanSunRadius) / TexelM, 2.5f, 44.0f);
    const float ThicknessM       = std::max(0.65f, TexelM * 42.0f);

    float LitSum     = 0.0f;
    float BlkDistSum = 0.0f;
    uint32_t BlkHits = 0u;

    for (uint32_t RIdx = 0u; RIdx < SmrtRays; ++RIdx)
    {
        uint32_t StepRng     = (RotationSeed ^ (RIdx * 747796405u + 2891336453u)) | 1u;
        StepRng              = StepRng * 747796405u + 2891336453u;
        const float StepJit  = static_cast<float>(((StepRng >> ((StepRng >> 28u) + 4u)) ^ StepRng) * 277803737u >> 8u) * (1.0f / 16777216.0f);
        const float Radial   = std::sqrt((static_cast<float>(RIdx) + 0.5f) / static_cast<float>(SmrtRays));
        const float Angle    = static_cast<float>(RIdx) * GoldenAngle + RotPhi;
        const float DirTX    = Radial * std::cos(Angle) * MaxStepTexels;
        const float DirTY    = Radial * std::sin(Angle) * MaxStepTexels;
        bool Occluded        = false;
        float BestHitDist    = 0.0f;
        float PrevRayD       = RecD;

        for (uint32_t Step = 1u; Step <= SmrtSteps; ++Step)
        {
            const float TNorm = (static_cast<float>(Step) - 0.5f + StepJit) / static_cast<float>(SmrtSteps);
            const float TQuad = TNorm * TNorm;
            const int TX      = std::clamp(static_cast<int>(std::floor(CX + DirTX * TQuad)), 0, MaxIdx);
            const int TY      = std::clamp(static_cast<int>(std::floor(CY + DirTY * TQuad)), 0, MaxIdx);
            const float RayD      = RecD - MaxMarchWorldM * TQuad;
            const float CascadeD  = Cascade.ClosestLightDepth[static_cast<std::size_t>(TY) * Cascade.ResolutionTexels + static_cast<uint32_t>(TX)];
            const float DiffCurr  = RayD - CascadeD;
            const float DiffPrev  = PrevRayD - CascadeD;

            if (DiffCurr > Bias && DiffPrev < ThicknessM)
            {
                Occluded    = true;
                BestHitDist = std::max(0.04f, RecD - CascadeD);
                break;
            }
            PrevRayD = RayD;
        }
        if (Occluded)
        {
            BlkDistSum += BestHitDist;
            ++BlkHits;
        }
        else
        {
            LitSum += 1.0f;
        }
    }

    Out.Visibility       = LitSum / static_cast<float>(SmrtRays);
    Out.BlockerDistanceM = (BlkHits > 0u) ? (BlkDistSum / static_cast<float>(BlkHits)) : 0.0f;
    return Out;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                               SOLAR SHADOW SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

void SolarShadowSequence::BuildTwoCascadeContactClips(
    const SolarDirectionalEmitter&               Sun,
    Mathematics::Axis3D                          FocusCenterWorld,
    const SpatialAcceleration::WideBoundingTree& SceneTree,
    OrthographicShadowCascade&                   OutFineCascade,
    OrthographicShadowCascade&                   OutCoarseCascade)
{
    const Mathematics::Axis3D SunDir   = Mathematics::NormalizeAxis(Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
    const Mathematics::Axis3D LightFwd = SunDir * -1.0f;
    Mathematics::Axis3D RightAxis {};
    Mathematics::Axis3D UpAxis    {};
    GeometrySpace::ConstructDuffOrthonormalBasis(LightFwd, RightAxis, UpAxis);

    // Snap cascade centers to texel increments in light space so camera pans never shimmer!
    const auto ConfigureCascade = [&](OrthographicShadowCascade& Cascade, float HalfSpanMeters)
    {
        Cascade.HalfSpanMeters    = HalfSpanMeters;
        Cascade.DepthExtentMeters = 28.0f;
        Cascade.ResolutionTexels  = 1024u;
        Cascade.LightRightAxis    = RightAxis;
        Cascade.LightUpAxis       = UpAxis;
        Cascade.LightForwardAxis  = LightFwd;

        const float TexelSizeM = (2.0f * HalfSpanMeters) / static_cast<float>(Cascade.ResolutionTexels);
        const float ProjU      = std::round(Mathematics::EvaluateDot(FocusCenterWorld, RightAxis) / TexelSizeM) * TexelSizeM;
        const float ProjV      = std::round(Mathematics::EvaluateDot(FocusCenterWorld, UpAxis) / TexelSizeM) * TexelSizeM;
        const float ProjZ      = Mathematics::EvaluateDot(FocusCenterWorld, LightFwd);
        Cascade.CenterWorld    = RightAxis * ProjU + UpAxis * ProjV + LightFwd * ProjZ;
        RasterizeTreeIntoOrthographicCascade(SceneTree, Cascade);
    };

    // Cascade 0: 4.0 m total width / 1024 = 3.90625 mm/texel!
    ConfigureCascade(OutFineCascade, 2.0f);
    // Cascade 1: 16.0 m total width / 1024 = 15.625 mm/texel!
    ConfigureCascade(OutCoarseCascade, 8.0f);
}

SolarShadowFieldResult SolarShadowSequence::EvaluateSolarShadowFieldWithAlgorithm(
    SolarShadowAlgorithm                                    Algorithm,
    const SolarDirectionalEmitter&                          Sun,
    uint32_t                                                ViewportWidth,
    uint32_t                                                ViewportHeight,
    std::span<const AnalyticResolvedSurfaceSample>          PrimarySamples,
    const SpatialAcceleration::WideBoundingTree&            SceneTree,
    const OrthographicShadowCascade*                        OptionalFineClip,
    const OrthographicShadowCascade*                        OptionalCoarseClip,
    const DistanceFieldSpace::VoxelSdfClipmapVolume*        OptionalClipmap,
    std::span<const DistanceFieldSpace::DynamicLocalSdfInstance> DynamicObjects)
{
    SolarShadowFieldResult Result {};
    Result.WidthPx  = ViewportWidth;
    Result.HeightPx = ViewportHeight;
    const std::size_t TotalPixels = static_cast<std::size_t>(ViewportWidth) * ViewportHeight;
    Result.RawVisibility.assign(TotalPixels, 1.0f);
    Result.FilteredVisibility.assign(TotalPixels, 1.0f);
    Result.BlockerDistanceMeters.assign(TotalPixels, 0.0f);
    if (OptionalFineClip != nullptr && OptionalFineClip->ResolutionTexels > 0u)
    {
        Result.CascadeZeroTexelMm = (2.0f * OptionalFineClip->HalfSpanMeters * 1000.0f) /
                                    static_cast<float>(OptionalFineClip->ResolutionTexels);
    }

    const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis(Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
    Mathematics::Axis3D DiscTangentU {};
    Mathematics::Axis3D DiscTangentV {};
    GeometrySpace::ConstructDuffOrthonormalBasis(SunDir, DiscTangentU, DiscTangentV);
    const float TanSunRadius = std::tan(std::clamp(Sun.SunAngularRadiusRadians, 1.0e-4f, 0.25f));

    for (uint32_t PY = 0u; PY < ViewportHeight; ++PY)
    {
        for (uint32_t PX = 0u; PX < ViewportWidth; ++PX)
        {
            const std::size_t Off = static_cast<std::size_t>(PY) * ViewportWidth + PX;
            const auto& Hit       = PrimarySamples[Off];
            if (!Hit.ValidHit)
            {
                continue;
            }

            const float NdotL = Mathematics::EvaluateDot(Hit.TangentFrame.NormalZ, SunDir);
            if (NdotL <= 1.0e-5f)
            {
                Result.RawVisibility[Off]      = 0.0f;
                Result.FilteredVisibility[Off] = 0.0f;
                continue;
            }

            const uint32_t Seed      = HashPixelSeed(PX, PY);
            const float SlopeScale   = std::sqrt(std::max(0.0f, 1.0f - NdotL * NdotL)) / std::max(0.15f, NdotL);
            const Mathematics::Axis3D BiasedOrigin = Hit.WorldPosition + Hit.TangentFrame.NormalZ * 3.5e-3f;

            if (Algorithm == SolarShadowAlgorithm::WideTreeRayQuery)
            {
                // Deterministic Vogel Solar Disc Raytrace against 4-Wide WideBoundingTree!
                const uint32_t RayCount = std::clamp(Sun.RtxSamplesPerPixel, 1u, 32u);
                const float    RotPhi   = static_cast<float>(Seed & 0xFFFFu) * (Constants::TauFloat / 65536.0f);
                constexpr float GoldenAngle = 2.39996323f;

                const Mathematics::Axis3D RayOrigin = Hit.WorldPosition + Hit.TangentFrame.NormalZ * 1.5e-3f;
                float    UnoccludedSum     = 0.0f;
                float    HarmonicInvDistS  = 0.0f;
                uint32_t OccludedHits      = 0u;

                for (uint32_t RIdx = 0u; RIdx < RayCount; ++RIdx)
                {
                    const float DiscR = std::sqrt((static_cast<float>(RIdx) + 0.5f) / static_cast<float>(RayCount)) * TanSunRadius;
                    const float Angle = static_cast<float>(RIdx) * GoldenAngle + RotPhi;
                    const Mathematics::Axis3D JitteredSun = Mathematics::NormalizeAxis(
                        SunDir + DiscTangentU * (DiscR * std::cos(Angle)) + DiscTangentV * (DiscR * std::sin(Angle)),
                        SunDir);

                    SpatialAcceleration::RaySegmentQuery ShadowRay {};
                    ShadowRay.Origin          = RayOrigin;
                    ShadowRay.UnitDirection   = JitteredSun;
                    ShadowRay.MinimumDistance = 1.0e-3f;
                    ShadowRay.MaximumDistance = 150.0f;

                    const auto RayHit = SceneTree.TraceClosestRay(ShadowRay, false);
                    if (RayHit.HitConfirmed)
                    {
                        ++OccludedHits;
                        HarmonicInvDistS += 1.0f / std::max(5.0e-3f, RayHit.HitDistanceMeters);
                    }
                    else
                    {
                        UnoccludedSum += 1.0f;
                    }
                }

                Result.RawVisibility[Off] = UnoccludedSum / static_cast<float>(RayCount);
                if (OccludedHits > 0u)
                {
                    Result.BlockerDistanceMeters[Off] = static_cast<float>(OccludedHits) / HarmonicInvDistS;
                }
            }
            else if (Algorithm == SolarShadowAlgorithm::HybridSdf)
            {
                // Exact arena/f2d37e4b-frontier Hybrid SDF Clipmap + Exact Triangle Refinement over Vogel Solar Disc!
                const uint32_t RayCount = std::clamp(Sun.RtxSamplesPerPixel, 4u, 8u);
                const float    RotPhi   = static_cast<float>(Seed & 0xFFFFu) * (Constants::TauFloat / 65536.0f);
                constexpr float GoldenAngle = 2.39996323f;

                const Mathematics::Axis3D RayOrigin = Hit.WorldPosition + Hit.TangentFrame.NormalZ * 1.5e-3f;
                float    UnoccludedSum     = 0.0f;
                float    HarmonicInvDistS  = 0.0f;
                uint32_t OccludedHits      = 0u;
                const bool UseClipmapHybrid = (OptionalClipmap != nullptr) &&
                    (!OptionalClipmap->StaticAtlas.Surfaces.empty() || !DynamicObjects.empty());

                for (uint32_t RIdx = 0u; RIdx < RayCount; ++RIdx)
                {
                    const float DiscR = std::sqrt((static_cast<float>(RIdx) + 0.5f) / static_cast<float>(RayCount)) * TanSunRadius;
                    const float Angle = static_cast<float>(RIdx) * GoldenAngle + RotPhi;
                    const Mathematics::Axis3D JitteredSun = Mathematics::NormalizeAxis(
                        SunDir + DiscTangentU * (DiscR * std::cos(Angle)) + DiscTangentV * (DiscR * std::sin(Angle)),
                        SunDir);

                    bool HitOccluded = false;
                    float HitDistM   = 1.0f;
                    if (UseClipmapHybrid)
                    {
                        HitOccluded = OptionalClipmap->TraceHybridOccluded(RayOrigin, JitteredSun, 45.0f, DynamicObjects);
                        if (HitOccluded)
                        {
                            const auto Interval = OptionalClipmap->SampleClipmapInterval(RayOrigin + JitteredSun * 0.25f);
                            HitDistM = std::max(0.05f, Interval.Raw);
                        }
                    }
                    else
                    {
                        SpatialAcceleration::RaySegmentQuery Ray {};
                        Ray.Origin          = RayOrigin;
                        Ray.UnitDirection   = JitteredSun;
                        Ray.MinimumDistance = 1.0e-3f;
                        Ray.MaximumDistance = 150.0f;
                        const auto RayHit = SceneTree.TraceClosestRay(Ray, false);
                        HitOccluded = RayHit.HitConfirmed;
                        HitDistM    = RayHit.HitDistanceMeters;
                    }

                    if (HitOccluded)
                    {
                        ++OccludedHits;
                        HarmonicInvDistS += 1.0f / std::max(5.0e-3f, HitDistM);
                    }
                    else
                    {
                        UnoccludedSum += 1.0f;
                    }
                }

                Result.RawVisibility[Off] = UnoccludedSum / static_cast<float>(RayCount);
                if (OccludedHits > 0u)
                {
                    Result.BlockerDistanceMeters[Off] = static_cast<float>(OccludedHits) / HarmonicInvDistS;
                }
            }
            else if (Algorithm == SolarShadowAlgorithm::VoxelSdfPenumbra ||
                     Algorithm == SolarShadowAlgorithm::VoxelSdfClipmapPenumbra)
            {
                float SdfVis = 1.0f;
                float SdfBlk = 0.0f;
                if (OptionalClipmap != nullptr)
                {
                    const bool ForceCoarse = (Algorithm == SolarShadowAlgorithm::VoxelSdfPenumbra);
                    const auto SdfRes = OptionalClipmap->TraceClipmapSdfPenumbra(
                        Hit.WorldPosition, Hit.TangentFrame.NormalZ, SunDir, 24.0f,
                        std::max(0.020f, TanSunRadius), true, DynamicObjects, ForceCoarse);
                    SdfVis = SdfRes.Visibility;
                    SdfBlk = SdfRes.BlockerDistM;
                }
                Result.RawVisibility[Off]         = std::clamp(SdfVis, 0.0f, 1.0f);
                Result.BlockerDistanceMeters[Off] = SdfBlk;
            }
            else if (Algorithm == SolarShadowAlgorithm::PcfBilinear)
            {
                CascadeSampleOutcome ClipSample {};
                if (OptionalFineClip != nullptr)
                {
                    ClipSample = SampleCascadePcf3x3(*OptionalFineClip, BiasedOrigin, SlopeScale);
                }
                if (!ClipSample.InsideCascade && OptionalCoarseClip != nullptr)
                {
                    ClipSample = SampleCascadePcf3x3(*OptionalCoarseClip, BiasedOrigin, SlopeScale);
                }
                Result.RawVisibility[Off]         = ClipSample.InsideCascade ? ClipSample.Visibility : 1.0f;
                Result.BlockerDistanceMeters[Off] = ClipSample.BlockerDistanceM;
            }
            else if (Algorithm == SolarShadowAlgorithm::ShadowRayMarch)
            {
                CascadeSampleOutcome ClipSample {};
                if (OptionalFineClip != nullptr)
                {
                    ClipSample = SampleCascadeShadowRayMarch(*OptionalFineClip, BiasedOrigin, SlopeScale, TanSunRadius, Seed);
                }
                if (!ClipSample.InsideCascade && OptionalCoarseClip != nullptr)
                {
                    ClipSample = SampleCascadeShadowRayMarch(*OptionalCoarseClip, BiasedOrigin, SlopeScale, TanSunRadius, Seed);
                }
                Result.RawVisibility[Off]         = ClipSample.InsideCascade ? ClipSample.Visibility : 1.0f;
                Result.BlockerDistanceMeters[Off] = ClipSample.BlockerDistanceM;
            }
            else
            {
                // PcssPlaneAware OR ContactPcss (Zero SDF Shadows!)
                CascadeSampleOutcome ClipSample {};
                if (OptionalFineClip != nullptr)
                {
                    ClipSample = SampleCascadePcss(*OptionalFineClip, BiasedOrigin, SlopeScale, TanSunRadius, Seed);
                }
                if (!ClipSample.InsideCascade && OptionalCoarseClip != nullptr)
                {
                    ClipSample = SampleCascadePcss(*OptionalCoarseClip, BiasedOrigin, SlopeScale, TanSunRadius, Seed);
                }

                float FinalVis = ClipSample.InsideCascade ? ClipSample.Visibility : 1.0f;
                float BlockerD = ClipSample.BlockerDistanceM;

                if (Algorithm == SolarShadowAlgorithm::ContactPcss)
                {
                    // Short 0.24 m BVH4 Contact Gate eliminates shadow detachment at plinth/tire contacts!
                    SpatialAcceleration::RaySegmentQuery ContactRay {};
                    ContactRay.Origin          = Hit.WorldPosition + Hit.TangentFrame.NormalZ * 1.5e-3f;
                    ContactRay.UnitDirection   = SunDir;
                    ContactRay.MinimumDistance = 1.0e-3f;
                    ContactRay.MaximumDistance = 0.24f;
                    const auto ContactHit      = SceneTree.TraceClosestRay(ContactRay, false);
                    if (ContactHit.HitConfirmed)
                    {
                        const float ContactFade = std::clamp(ContactHit.HitDistanceMeters / 0.24f, 0.0f, 1.0f);
                        const float ContactVis  = ContactFade * 0.25f;
                        FinalVis                = std::min(FinalVis, ContactVis);
                        BlockerD                = (BlockerD > 0.0f) ? std::min(BlockerD, ContactHit.HitDistanceMeters) : ContactHit.HitDistanceMeters;
                    }
                }
                else if (!ClipSample.InsideCascade)
                {
                    // Outside cascade bounds, fall back cleanly to 1-ray BVH4 query (never SDF)
                    SpatialAcceleration::RaySegmentQuery FallbackRay {};
                    FallbackRay.Origin          = Hit.WorldPosition + Hit.TangentFrame.NormalZ * 2.0e-3f;
                    FallbackRay.UnitDirection   = SunDir;
                    FallbackRay.MinimumDistance = 1.0e-3f;
                    FallbackRay.MaximumDistance = 150.0f;
                    FinalVis = SceneTree.QueryRayOccluded(FallbackRay) ? 0.0f : 1.0f;
                }

                Result.RawVisibility[Off]         = std::clamp(FinalVis, 0.0f, 1.0f);
                Result.BlockerDistanceMeters[Off] = BlockerD;
            }
        }
    }

    if (Algorithm == SolarShadowAlgorithm::PcfBilinear)
    {
        Result.FilteredVisibility = Result.RawVisibility;
    }
    else
    {
        ApplyPlaneAwarePenumbraFilter(ViewportWidth, ViewportHeight, TanSunRadius, PrimarySamples, Result);
    }
    return Result;
}

SolarShadowFieldResult SolarShadowSequence::EvaluateSolarShadowField(
    DirectShadowTier                                Tier,
    const SolarDirectionalEmitter&                  Sun,
    uint32_t                                        ViewportWidth,
    uint32_t                                        ViewportHeight,
    std::span<const AnalyticResolvedSurfaceSample>  PrimarySamples,
    const SpatialAcceleration::WideBoundingTree&    SceneTree,
    const DistanceFieldSpace::SignedDistanceVolume* OptionalSdfVolume,
    const OrthographicShadowCascade*                OptionalFineClip,
    const OrthographicShadowCascade*                OptionalCoarseClip)
{
    (void)OptionalSdfVolume;
    const SolarShadowAlgorithm MappedAlg = (Tier == DirectShadowTier::RtxDeterministicSolarRay)
        ? SolarShadowAlgorithm::WideTreeRayQuery
        : SolarShadowAlgorithm::ContactPcss;
    return EvaluateSolarShadowFieldWithAlgorithm(
        MappedAlg,
        Sun,
        ViewportWidth,
        ViewportHeight,
        PrimarySamples,
        SceneTree,
        OptionalFineClip,
        OptionalCoarseClip);
}

void SolarShadowSequence::ApplyPlaneAwarePenumbraFilter(
    uint32_t                                       ViewportWidth,
    uint32_t                                       ViewportHeight,
    float                                          TanSunAngularRadius,
    std::span<const AnalyticResolvedSurfaceSample> PrimarySamples,
    SolarShadowFieldResult&                        InOutField)
{
    const std::size_t TotalPixels = static_cast<std::size_t>(ViewportWidth) * ViewportHeight;
    if (TotalPixels == 0u || InOutField.RawVisibility.size() < TotalPixels)
    {
        return;
    }

    // Pass 1: Coplanar 5x5 Blocker Dilation so sunlit penumbra fringe pixels inherit accurate caster distance!
    std::vector<float> DilatedBlocker = InOutField.BlockerDistanceMeters;
    for (uint32_t PY = 0u; PY < ViewportHeight; ++PY)
    {
        for (uint32_t PX = 0u; PX < ViewportWidth; ++PX)
        {
            const std::size_t Off = static_cast<std::size_t>(PY) * ViewportWidth + PX;
            const auto& CenterHit = PrimarySamples[Off];
            if (!CenterHit.ValidHit) continue;

            float MaxCoplanarBlocker = DilatedBlocker[Off];
            for (int DY = -2; DY <= 2; ++DY)
            {
                const int NY = std::clamp(static_cast<int>(PY) + DY, 0, static_cast<int>(ViewportHeight) - 1);
                for (int DX = -2; DX <= 2; ++DX)
                {
                    const int NX = std::clamp(static_cast<int>(PX) + DX, 0, static_cast<int>(ViewportWidth) - 1);
                    const std::size_t NOff = static_cast<std::size_t>(NY) * ViewportWidth + static_cast<uint32_t>(NX);
                    const auto& NeighHit   = PrimarySamples[NOff];
                    if (!NeighHit.ValidHit) continue;
                    const float PlaneDist = std::fabs(Mathematics::EvaluateDot(
                        CenterHit.TangentFrame.NormalZ, NeighHit.WorldPosition - CenterHit.WorldPosition));
                    if (PlaneDist < 0.03f &&
                        Mathematics::EvaluateDot(CenterHit.TangentFrame.NormalZ, NeighHit.TangentFrame.NormalZ) > 0.90f)
                    {
                        MaxCoplanarBlocker = std::max(MaxCoplanarBlocker, InOutField.BlockerDistanceMeters[NOff]);
                    }
                }
            }
            DilatedBlocker[Off] = MaxCoplanarBlocker;
        }
    }

    // Pass 2: 3-Step Continuous-Sigma Plane-Aware À-Trous Wavelet Filter (Steps 1, 2, 4)
    // Contact shadows (DilatedBlocker -> 0) collapse SigmaPx -> 0.35 px (razor-sharp!), while distant casters widen smoothly!
    std::vector<float> Ping = InOutField.RawVisibility;
    std::vector<float> Pong = Ping;
    constexpr std::array<int, 3u> StepSizes { 1, 2, 4 };

    for (int Step : StepSizes)
    {
        for (uint32_t PY = 0u; PY < ViewportHeight; ++PY)
        {
            for (uint32_t PX = 0u; PX < ViewportWidth; ++PX)
            {
                const std::size_t Off = static_cast<std::size_t>(PY) * ViewportWidth + PX;
                const auto& CenterHit = PrimarySamples[Off];
                if (!CenterHit.ValidHit)
                {
                    Pong[Off] = Ping[Off];
                    continue;
                }

                // Estimate screen-space penumbra radius in pixels from world blocker distance & camera distance
                const float CamDist = std::max(0.25f, Mathematics::EvaluateLength(CenterHit.WorldPosition));
                const float PenumbraWorldM = DilatedBlocker[Off] * TanSunAngularRadius;
                const float SigmaPx = std::clamp(0.35f + (PenumbraWorldM / CamDist) * static_cast<float>(ViewportHeight) * 1.4f, 0.35f, 9.0f);
                const float InvTwoSigmaSq = 1.0f / (2.0f * SigmaPx * SigmaPx);

                float SumVis    = 0.0f;
                float SumWeight = 0.0f;

                for (int KY = -1; KY <= 1; ++KY)
                {
                    const int NY = std::clamp(static_cast<int>(PY) + KY * Step, 0, static_cast<int>(ViewportHeight) - 1);
                    for (int KX = -1; KX <= 1; ++KX)
                    {
                        const int NX = std::clamp(static_cast<int>(PX) + KX * Step, 0, static_cast<int>(ViewportWidth) - 1);
                        const std::size_t NOff = static_cast<std::size_t>(NY) * ViewportWidth + static_cast<uint32_t>(NX);
                        const auto& NeighHit   = PrimarySamples[NOff];
                        if (!NeighHit.ValidHit) continue;

                        const float DistPxSq  = static_cast<float>((KX * Step) * (KX * Step) + (KY * Step) * (KY * Step));
                        const float WSpatial  = std::exp(-DistPxSq * InvTwoSigmaSq);
                        const float PlaneDist = std::fabs(Mathematics::EvaluateDot(
                            CenterHit.TangentFrame.NormalZ, NeighHit.WorldPosition - CenterHit.WorldPosition));
                        const float WPlane    = std::exp(-PlaneDist * 65.0f);
                        const float NrmDot    = std::max(0.0f, Mathematics::EvaluateDot(
                            CenterHit.TangentFrame.NormalZ, NeighHit.TangentFrame.NormalZ));
                        const float WNrm      = std::pow(NrmDot, 24.0f);

                        const float Weight = WSpatial * WPlane * WNrm;
                        SumVis    += Ping[NOff] * Weight;
                        SumWeight += Weight;
                    }
                }

                Pong[Off] = (SumWeight > 1.0e-6f) ? (SumVis / SumWeight) : Ping[Off];
            }
        }
        Ping.swap(Pong);
    }

    InOutField.FilteredVisibility = std::move(Ping);
}

} // namespace Frontier::GeometricRaster
