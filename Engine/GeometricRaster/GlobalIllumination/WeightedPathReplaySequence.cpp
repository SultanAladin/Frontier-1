/*============================================================================================================================================
                                                      RESERVOIRPATHREPLAYSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Hybrid ReSTIR Path Replay GI: Candidate RIS, Temporal/Spatial Reservoir Reuse, DynamicProbe 2nd-Bounce & 5-Pass Joint A-Trous SVGF

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/WeightedPathReplaySequence.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometrySpace/Analysis/CurvatureAnalysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Frontier::GeometricRaster
{

namespace
{

[[nodiscard]] uint32_t AdvancePcgRng(uint32_t& State) noexcept
{
    State = State * 747796405u + 2891336453u;
    const uint32_t Word = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return (Word >> 22u) ^ Word;
}

[[nodiscard]] float NextPcgFloat01(uint32_t& State) noexcept
{
    return static_cast<float>(AdvancePcgRng(State) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

[[nodiscard]] uint32_t ReverseBits32Scalar(uint32_t X) noexcept
{
    X = ((X & 0x55555555u) << 1u) | ((X >> 1u) & 0x55555555u);
    X = ((X & 0x33333333u) << 2u) | ((X >> 2u) & 0x33333333u);
    X = ((X & 0x0F0F0F0Fu) << 4u) | ((X >> 4u) & 0x0F0F0F0Fu);
    X = ((X & 0x00FF00FFu) << 8u) | ((X >> 8u) & 0x00FF00FFu);
    return (X << 16u) | (X >> 16u);
}

[[nodiscard]] uint32_t HashMix32(uint32_t X) noexcept
{
    X ^= X >> 16u;
    X *= 0x7FEB352Du;
    X ^= X >> 15u;
    X *= 0x846CA68Bu;
    X ^= X >> 16u;
    return X;
}

[[nodiscard]] Mathematics::Axis2D EvaluateOwenSobol2D(uint32_t SampleIdx, uint32_t PixelSeed) noexcept
{
    const uint32_t D0 = ReverseBits32Scalar(SampleIdx) ^ HashMix32(PixelSeed ^ 0xA341316Cu);
    uint32_t       C  = SampleIdx;
    uint32_t       V  = 0x80000000u;
    uint32_t       D1 = 0u;
    while (C != 0u)
    {
        if ((C & 1u) != 0u)
        {
            D1 ^= V;
        }
        C >>= 1u;
        V ^= V >> 1u;
    }
    D1 ^= HashMix32(PixelSeed ^ 0xC8013EA4u);
    return {
        static_cast<float>(D0 >> 8u) * (1.0f / 16777216.0f),
        static_cast<float>(D1 >> 8u) * (1.0f / 16777216.0f),
    };
}

[[nodiscard]] float DecodeFp16Val(uint16_t HalfBits) noexcept
{
    const uint32_t Sign = static_cast<uint32_t>(HalfBits & 0x8000u) << 16u;
    const uint32_t Exp  = (HalfBits >> 10u) & 0x1Fu;
    const uint32_t Mant = HalfBits & 0x03FFu;
    uint32_t Bits32     = 0u;
    if (Exp == 0u)
    {
        if (Mant == 0u)
        {
            Bits32 = Sign;
        }
        else
        {
            const float Sub = static_cast<float>(Mant) * (1.0f / 16777216.0f);
            return (Sign != 0u) ? -Sub : Sub;
        }
    }
    else if (Exp == 31u)
    {
        Bits32 = Sign | 0x7F800000u | (Mant << 13u);
    }
    else
    {
        Bits32 = Sign | ((Exp + 112u) << 23u) | (Mant << 13u);
    }
    float Result = 0.0f;
    std::memcpy(&Result, &Bits32, sizeof(float));
    return Result;
}

[[nodiscard]] Mathematics::LinearRGB UnpackReplaySlabDiffuse(const MaterialSpace::BounceSlabEntry& Slab) noexcept
{
    return {
        DecodeFp16Val(static_cast<uint16_t>(Slab.DiffuseAlbedoRgHalf & 0xFFFFu)),
        DecodeFp16Val(static_cast<uint16_t>((Slab.DiffuseAlbedoRgHalf >> 16u) & 0xFFFFu)),
        DecodeFp16Val(static_cast<uint16_t>(Slab.DiffuseAlbedoBAndRoughnessHalf & 0xFFFFu)),
    };
}

[[nodiscard]] Mathematics::LinearRGB UnpackReplaySlabEmissive(const MaterialSpace::BounceSlabEntry& Slab) noexcept
{
    return {
        DecodeFp16Val(static_cast<uint16_t>(Slab.EmissiveExitanceRgHalf & 0xFFFFu)),
        DecodeFp16Val(static_cast<uint16_t>((Slab.EmissiveExitanceRgHalf >> 16u) & 0xFFFFu)),
        DecodeFp16Val(static_cast<uint16_t>(Slab.EmissiveExitanceBAndTransHalf & 0xFFFFu)),
    };
}

[[nodiscard]] float LuminanceOf(const Mathematics::LinearRGB& C) noexcept
{
    return std::max(0.0f, 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B);
}

void BuildTangentBasis(
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

[[nodiscard]] Mathematics::LinearRGB EvaluateAreaLightAtSecondaryHit(
    const Mathematics::Axis3D&                   HitPos,
    const Mathematics::Axis3D&                   HitNrm,
    const Mathematics::LinearRGB&                DiffAlb,
    std::span<const PolygonalAreaQuadLight>      AreaLights,
    const SpatialAcceleration::WideBoundingTree& SceneTree) noexcept
{
    Mathematics::LinearRGB Accum { 0.0f, 0.0f, 0.0f };
    for (const auto& Light : AreaLights)
    {
        const Mathematics::Axis3D Delta  = Light.CenterWorld - HitPos;
        const float               DistSq = std::max(0.04f, Mathematics::EvaluateDot(Delta, Delta));
        const float               Dist   = std::sqrt(DistSq);
        const Mathematics::Axis3D Dir    = Delta * (1.0f / Dist);
        const float               NdotL  = Mathematics::EvaluateDot(HitNrm, Dir);
        if (NdotL <= 1.0e-4f)
        {
            continue;
        }
        const Mathematics::Axis3D LightNormal = Mathematics::NormalizeAxis(
            Mathematics::EvaluateCross(Light.RightTangent, Light.UpBitangent),
            { 0.0f, 0.0f, -1.0f });
        float LdotNeg = Mathematics::EvaluateDot(LightNormal, Dir * -1.0f);
        if (Light.TwoSidedEmission)
        {
            LdotNeg = std::fabs(LdotNeg);
        }
        if (LdotNeg <= 1.0e-4f)
        {
            continue;
        }

        SpatialAcceleration::RaySegmentQuery VisRay {};
        VisRay.Origin          = HitPos + HitNrm * 2.5e-3f;
        VisRay.UnitDirection   = Dir;
        VisRay.MinimumDistance = 1.0e-3f;
        VisRay.MaximumDistance = std::max(0.01f, Dist - 0.04f);
        if (SceneTree.QueryRayOccluded(VisRay))
        {
            continue;
        }

        const float Area   = 4.0f * Light.HalfWidthMeters * Light.HalfHeightMeters;
        const float Factor = (NdotL * LdotNeg * Area) / (Constants::PiFloat * DistSq + Area);
        Accum.R += Light.EmittedRadiance.R * DiffAlb.R * Factor;
        Accum.G += Light.EmittedRadiance.G * DiffAlb.G * Factor;
        Accum.B += Light.EmittedRadiance.B * DiffAlb.B * Factor;
    }
    return Accum;
}

[[nodiscard]] float EvaluateTargetPdfAtSurface(
    const Mathematics::Axis3D&    SurfacePos,
    const Mathematics::Axis3D&    SurfaceNormal,
    const ReservoirSampleState&   Sample) noexcept
{
    if (Sample.ValidFlag == 0u)
    {
        return 0.0f;
    }
    const Mathematics::Axis3D DirToSample = (Sample.SkyMissFlag != 0u)
        ? Mathematics::NormalizeAxis(Sample.SecondaryHitPos, { 0.0f, 0.0f, 1.0f })
        : Mathematics::NormalizeAxis(Sample.SecondaryHitPos - SurfacePos, { 0.0f, 0.0f, 1.0f });
    const float CosTheta = std::max(0.0f, Mathematics::EvaluateDot(SurfaceNormal, DirToSample));
    if (CosTheta <= 1.0e-5f)
    {
        return 0.0f;
    }
    return LuminanceOf(Sample.Radiance) * (CosTheta / Constants::PiFloat);
}

void UpdateReservoirWithCandidate(
    ReservoirSampleState&       InOutRes,
    const ReservoirSampleState& Candidate,
    float                       RisWeight,
    uint32_t                    NewCountM,
    uint32_t&                   InOutRng) noexcept
{
    InOutRes.SampleCountM += NewCountM;
    if (RisWeight <= 0.0f || !std::isfinite(RisWeight))
    {
        return;
    }
    InOutRes.WeightSum += RisWeight;
    const float AcceptProb = RisWeight / std::max(1.0e-12f, InOutRes.WeightSum);
    if (NextPcgFloat01(InOutRng) < AcceptProb)
    {
        InOutRes.SecondaryHitPos = Candidate.SecondaryHitPos;
        InOutRes.Radiance        = Candidate.Radiance;
        InOutRes.TargetPdf       = Candidate.TargetPdf;
        InOutRes.ValidFlag       = Candidate.ValidFlag;
        InOutRes.SkyMissFlag     = Candidate.SkyMissFlag;
    }
}

void FinalizeReservoirWeight(
    ReservoirSampleState& InOutRes,
    float                 CurrentTargetPdf,
    float                 CosTheta) noexcept
{
    if (InOutRes.ValidFlag == 0u || InOutRes.SampleCountM == 0u || CurrentTargetPdf <= 1.0e-9f)
    {
        InOutRes.ContributionW = 0.0f;
        InOutRes.TargetPdf     = 0.0f;
        return;
    }
    InOutRes.TargetPdf     = CurrentTargetPdf;
    const float RawW       = InOutRes.WeightSum / (static_cast<float>(InOutRes.SampleCountM) * CurrentTargetPdf);
    const float MaxRawW    = (20.0f * Constants::PiFloat) / std::max(1.0e-3f, CosTheta);
    InOutRes.ContributionW = std::clamp(RawW, 0.0f, MaxRawW);
}

[[nodiscard]] float EvaluateCosThetaAtSurface(
    const Mathematics::Axis3D&  SurfacePos,
    const Mathematics::Axis3D&  SurfaceNormal,
    const ReservoirSampleState& Sample) noexcept
{
    if (Sample.ValidFlag == 0u)
    {
        return 0.0f;
    }
    const Mathematics::Axis3D Dir = (Sample.SkyMissFlag != 0u)
        ? Mathematics::NormalizeAxis(Sample.SecondaryHitPos, { 0.0f, 0.0f, 1.0f })
        : Mathematics::NormalizeAxis(Sample.SecondaryHitPos - SurfacePos, { 0.0f, 0.0f, 1.0f });
    return std::max(0.0f, Mathematics::EvaluateDot(SurfaceNormal, Dir));
}

void ApplyJointATrousSvgfFilter(
    uint32_t                                       WidthPx,
    uint32_t                                       HeightPx,
    std::span<const AnalyticResolvedSurfaceSample> PrimarySamples,
    const MaterialSpace::CompactMaterialRegister&  MaterialRegister,
    uint32_t                                       PassCount,
    std::vector<Mathematics::LinearRGB>&           InOutGi)
{
    const std::size_t TotalPixels = static_cast<std::size_t>(WidthPx) * HeightPx;
    if (InOutGi.size() != TotalPixels || PrimarySamples.size() != TotalPixels)
    {
        return;
    }

    const std::size_t MatCount = MaterialRegister.QuerySlotCount();
    std::vector<Mathematics::LinearRGB> Scratch(TotalPixels);
    constexpr std::array<int32_t, 5u> Strides {{ 1, 2, 4, 8, 16 }};
    const uint32_t ClampedPasses = std::min<uint32_t>(PassCount, 5u);

    for (uint32_t Pass = 0u; Pass < ClampedPasses; ++Pass)
    {
        const int32_t Stride = Strides[Pass];
        const float   SigmaG = std::max(1.0f, static_cast<float>(Stride) * 1.15f);
        const float   Inv2Sig = 1.0f / (2.0f * SigmaG * SigmaG);

        for (uint32_t PY = 0u; PY < HeightPx; ++PY)
        {
            for (uint32_t PX = 0u; PX < WidthPx; ++PX)
            {
                const std::size_t Off = static_cast<std::size_t>(PY) * WidthPx + PX;
                const auto& Center    = PrimarySamples[Off];
                if (!Center.ValidHit || Center.MaterialSlot >= MatCount)
                {
                    Scratch[Off] = InOutGi[Off];
                    continue;
                }

                const auto&  CMat  = MaterialRegister.InspectAuthoredMaterial(Center.MaterialSlot);
                const float  CLum  = LuminanceOf(InOutGi[Off]);
                Mathematics::LinearRGB Sum { 0.0f, 0.0f, 0.0f };
                float                  SumW = 0.0f;

                for (int32_t KY = -1; KY <= 1; ++KY)
                {
                    const int32_t SY = std::clamp(static_cast<int32_t>(PY) + KY * Stride, 0, static_cast<int32_t>(HeightPx) - 1);
                    for (int32_t KX = -1; KX <= 1; ++KX)
                    {
                        const int32_t SX = std::clamp(static_cast<int32_t>(PX) + KX * Stride, 0, static_cast<int32_t>(WidthPx) - 1);
                        const std::size_t SOff = static_cast<std::size_t>(SY) * WidthPx + static_cast<uint32_t>(SX);
                        const auto& Neigh      = PrimarySamples[SOff];
                        if (!Neigh.ValidHit || Neigh.MaterialSlot >= MatCount)
                        {
                            continue;
                        }

                        const float PlaneDist = std::fabs(Mathematics::EvaluateDot(
                            Center.TangentFrame.NormalZ, Neigh.WorldPosition - Center.WorldPosition));
                        const float WPlane = std::exp(-PlaneDist * 65.0f);
                        const float NDot   = std::clamp(
                            Mathematics::EvaluateDot(Center.TangentFrame.NormalZ, Neigh.TangentFrame.NormalZ), 0.0f, 1.0f);
                        const float WNorm  = std::pow(NDot, 24.0f);

                        const auto& NMat   = MaterialRegister.InspectAuthoredMaterial(Neigh.MaterialSlot);
                        const float DR     = CMat.Diffuse.BaseColor.R - NMat.Diffuse.BaseColor.R;
                        const float DG     = CMat.Diffuse.BaseColor.G - NMat.Diffuse.BaseColor.G;
                        const float DB     = CMat.Diffuse.BaseColor.B - NMat.Diffuse.BaseColor.B;
                        const float WAlb   = std::exp(-(DR * DR + DG * DG + DB * DB) * 16.0f);

                        const float NLum   = LuminanceOf(InOutGi[SOff]);
                        const float DLum   = std::fabs(CLum - NLum);
                        const float WLum   = std::exp(-(DLum * DLum) / std::max(0.12f, 1.8f * CLum + 0.08f));

                        const float DistSq = static_cast<float>((KX * Stride) * (KX * Stride) + (KY * Stride) * (KY * Stride));
                        const float W      = std::exp(-DistSq * Inv2Sig) * WPlane * WNorm * WAlb * WLum;

                        Sum  = Sum + InOutGi[SOff] * W;
                        SumW += W;
                    }
                }
                Scratch[Off] = (SumW > 1.0e-6f) ? (Sum * (1.0f / SumW)) : InOutGi[Off];
            }
        }
        InOutGi.swap(Scratch);
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           RESERVOIR PATH REPLAY SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

WeightedIndirectOutput WeightedPathReplaySequence::EvaluateWeightedIndirectOutput(
    const PerspectiveCameraBasis&                   CurrentCamera,
    const PerspectiveCameraBasis&                   PreviousCamera,
    std::span<const AnalyticResolvedSurfaceSample>  PrimarySamples,
    const MaterialSpace::CompactMaterialRegister&   MaterialRegister,
    std::span<const MaterialSpace::BounceSlabEntry> BounceSummaries,
    const SpatialAcceleration::WideBoundingTree&    SceneTree,
    const DynamicIrradianceCacheSequence&                IrradianceCache,
    const SolarDirectionalSource&                  Sun,
    std::span<const PolygonalAreaQuadLight>         AreaLights,
    Mathematics::LinearRGB                          SkyRadiance,
    const WeightedPathReplaySettings&              Settings,
    uint32_t                                        SequenceIndex,
    std::span<const ReservoirSampleState>           PreviousReservoirs)
{
    const uint32_t    WidthPx     = std::max(1u, CurrentCamera.ViewportWidthPx);
    const uint32_t    HeightPx    = std::max(1u, CurrentCamera.ViewportHeightPx);
    const std::size_t TotalPixels = static_cast<std::size_t>(WidthPx) * HeightPx;
    const std::size_t MatCount    = MaterialRegister.QuerySlotCount();

    WeightedIndirectOutput Output {};
    Output.RawIndirectOverPi.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });
    Output.FilteredIndirectOverPi.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });
    Output.ResolvedReservoirs.assign(TotalPixels, ReservoirSampleState{});

    if (PrimarySamples.size() != TotalPixels)
    {
        return Output;
    }

    const Mathematics::Axis3D SunDir   = Mathematics::NormalizeAxis(Sun.DirectionToSun, { 0.42f, -0.55f, 0.72f });
    const uint32_t            CandCnt  = std::clamp(Settings.InitialCandidates, 1u, 8u);
    const OrthonormalCameraBasis PrevBasis = ComputeCameraBasis(PreviousCamera);

    // Stage A: Initial Multi-Candidate Owen-Sobol RIS + 1-Bounce BVH Path Replay + DynamicProbe 2nd Bounce
    std::vector<ReservoirSampleState> InitialReservoirs(TotalPixels);

    for (uint32_t PY = 0u; PY < HeightPx; ++PY)
    {
        for (uint32_t PX = 0u; PX < WidthPx; ++PX)
        {
            const std::size_t Off = static_cast<std::size_t>(PY) * WidthPx + PX;
            const auto&       Hit = PrimarySamples[Off];
            if (!Hit.ValidHit || Hit.MaterialSlot >= MatCount)
            {
                continue;
            }

            const Mathematics::Axis3D& N = Hit.TangentFrame.NormalZ;
            Mathematics::Axis3D        T, B;
            BuildTangentBasis(N, T, B);

            const uint32_t PixelSeed = HashMix32(PX * 1973u + PY * 9277u + 89173u + SequenceIndex * 104729u);
            uint32_t       RngState  = PixelSeed | 1u;

            ReservoirSampleState Res {};
            for (uint32_t C = 0u; C < CandCnt; ++C)
            {
                const Mathematics::Axis2D U    = EvaluateOwenSobol2D(SequenceIndex * CandCnt + C, PixelSeed);
                const float               Phi  = Constants::TauFloat * U.X;
                const float               SinT = std::sqrt(std::clamp(U.Y, 0.0f, 1.0f));
                const float               CosT = std::sqrt(std::max(0.0f, 1.0f - U.Y));
                const Mathematics::Axis3D Dir  = Mathematics::NormalizeAxis(
                    T * (SinT * std::cos(Phi)) + B * (SinT * std::sin(Phi)) + N * CosT, N);

                SpatialAcceleration::RaySegmentQuery Ray {};
                Ray.Origin          = Hit.WorldPosition + N * 2.5e-3f;
                Ray.UnitDirection   = Dir;
                Ray.MinimumDistance = 1.0e-3f;
                Ray.MaximumDistance = 80.0f;

                ReservoirSampleState Cand {};
                Cand.ValidFlag = 1u;

                const auto Hit2 = SceneTree.TraceClosestRay(Ray, false);
                if (!Hit2.HitConfirmed)
                {
                    Cand.SkyMissFlag     = 1u;
                    Cand.SecondaryHitPos = Dir * 1.0e4f;
                    Cand.Radiance        = SkyRadiance;
                }
                else
                {
                    Cand.SkyMissFlag     = 0u;
                    Cand.SecondaryHitPos = Ray.Origin + Dir * Hit2.HitDistanceMeters;
                    Mathematics::Axis3D N2 = (Mathematics::EvaluateDot(Hit2.GeometricNormal, Dir) < 0.0f)
                        ? Hit2.GeometricNormal
                        : (Hit2.GeometricNormal * -1.0f);

                    const uint16_t Slot2 = (Hit2.MaterialOrdinal < BounceSummaries.size()) ? Hit2.MaterialOrdinal : 0u;
                    const auto&    Slab2 = BounceSummaries[Slot2];
                    const auto     Alb2  = UnpackReplaySlabDiffuse(Slab2);

                    Mathematics::LinearRGB L2 = UnpackReplaySlabEmissive(Slab2);
                    if (Mathematics::EvaluateDot(N2, SunDir) > 0.0f)
                    {
                        SpatialAcceleration::RaySegmentQuery SunRay {};
                        SunRay.Origin          = Cand.SecondaryHitPos + N2 * 2.5e-3f;
                        SunRay.UnitDirection   = SunDir;
                        SunRay.MinimumDistance = 1.0e-3f;
                        SunRay.MaximumDistance = 120.0f;
                        if (!SceneTree.QueryRayOccluded(SunRay))
                        {
                            L2 = L2 + MaterialSpace::EvaluateBounceSlabRadiance(
                                Slab2, N2, Dir * -1.0f, SunDir, Sun.SolarIrradiance);
                        }
                    }
                    if (!AreaLights.empty())
                    {
                        L2 = L2 + EvaluateAreaLightAtSecondaryHit(
                            Cand.SecondaryHitPos, N2, Alb2, AreaLights, SceneTree);
                    }
                    // Multi-bounce diffuse tail via DynamicIrradianceCache
                    const auto ProbeTail = IrradianceCache.SampleIrradianceOverPi(
                        Cand.SecondaryHitPos, N2, Dir * -1.0f);
                    L2.R += ProbeTail.R * Alb2.R;
                    L2.G += ProbeTail.G * Alb2.G;
                    L2.B += ProbeTail.B * Alb2.B;

                    Cand.Radiance = L2;
                }

                // Proposal q(w) = cos(theta)/pi, target p_hat(w) = Luma(L2) * cos(theta)/pi -> weight w_k = Luma(L2)!
                const float Luma2 = std::max(1.0e-5f, LuminanceOf(Cand.Radiance));
                Cand.TargetPdf    = Luma2 * (std::max(1.0e-4f, CosT) / Constants::PiFloat);
                UpdateReservoirWithCandidate(Res, Cand, Luma2, 1u, RngState);
            }

            // Temporal Reprojection Reuse (when PreviousReservoirs provided)
            if (Settings.EnableTemporalReuse && PreviousReservoirs.size() == TotalPixels)
            {
                const Mathematics::Axis3D RelPrev = Hit.WorldPosition - PrevBasis.CameraOrigin;
                const float               DepthP  = Mathematics::EvaluateDot(RelPrev, PrevBasis.ForwardAxis);
                if (DepthP > 0.05f)
                {
                    const float NdcX = Mathematics::EvaluateDot(RelPrev, PrevBasis.RightAxis) / (DepthP * PrevBasis.TanHalfFovX);
                    const float NdcY = Mathematics::EvaluateDot(RelPrev, PrevBasis.UpAxis)    / (DepthP * PrevBasis.TanHalfFovY);
                    const int32_t PrevX = static_cast<int32_t>(std::floor((NdcX * 0.5f + 0.5f) * static_cast<float>(WidthPx)));
                    const int32_t PrevY = static_cast<int32_t>(std::floor((0.5f - NdcY * 0.5f) * static_cast<float>(HeightPx)));
                    if (PrevX >= 0 && PrevX < static_cast<int32_t>(WidthPx) &&
                        PrevY >= 0 && PrevY < static_cast<int32_t>(HeightPx))
                    {
                        const std::size_t PrevOff = static_cast<std::size_t>(PrevY) * WidthPx + static_cast<uint32_t>(PrevX);
                        ReservoirSampleState PrevRes = PreviousReservoirs[PrevOff];
                        if (PrevRes.ValidFlag != 0u && PrevRes.SampleCountM > 0u)
                        {
                            PrevRes.SampleCountM = std::min(PrevRes.SampleCountM, Settings.TemporalHistoryCap);
                            const float TargetCur = EvaluateTargetPdfAtSurface(Hit.WorldPosition, N, PrevRes);
                            if (TargetCur > 1.0e-6f)
                            {
                                const float TempWeight = TargetCur * PrevRes.ContributionW * static_cast<float>(PrevRes.SampleCountM);
                                UpdateReservoirWithCandidate(Res, PrevRes, TempWeight, PrevRes.SampleCountM, RngState);
                            }
                        }
                    }
                }
            }

            FinalizeReservoirWeight(
                Res,
                EvaluateTargetPdfAtSurface(Hit.WorldPosition, N, Res),
                EvaluateCosThetaAtSurface(Hit.WorldPosition, N, Res));
            InitialReservoirs[Off] = Res;
        }
    }

    // Stage B: 4-Tap Visibility-Validated Spatial Reservoir Reuse
    const uint32_t SpatialTaps = Settings.EnableSpatialReuse ? std::clamp(Settings.SpatialNeighborCount, 1u, 8u) : 0u;
    for (uint32_t PY = 0u; PY < HeightPx; ++PY)
    {
        for (uint32_t PX = 0u; PX < WidthPx; ++PX)
        {
            const std::size_t Off = static_cast<std::size_t>(PY) * WidthPx + PX;
            const auto&       Hit = PrimarySamples[Off];
            if (!Hit.ValidHit || Hit.MaterialSlot >= MatCount)
            {
                continue;
            }

            const Mathematics::Axis3D& N = Hit.TangentFrame.NormalZ;
            const auto&                CMat = MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);
            ReservoirSampleState       Combined = InitialReservoirs[Off];

            if (SpatialTaps > 0u)
            {
                const uint32_t PixelSeed = HashMix32(PX * 747796405u + PY * 2891336453u + (SequenceIndex + 1u) * 277803737u);
                uint32_t       RngState  = PixelSeed | 1u;
                Combined.WeightSum       = Combined.TargetPdf * Combined.ContributionW * static_cast<float>(Combined.SampleCountM);

                for (uint32_t Tap = 0u; Tap < SpatialTaps; ++Tap)
                {
                    const float Angle  = Constants::TauFloat * ((static_cast<float>(Tap) + NextPcgFloat01(RngState)) / static_cast<float>(SpatialTaps));
                    const float Radius = 2.0f + 8.0f * std::sqrt(NextPcgFloat01(RngState));
                    const int32_t NX   = std::clamp(static_cast<int32_t>(std::lround(static_cast<float>(PX) + std::cos(Angle) * Radius)),
                                                    0, static_cast<int32_t>(WidthPx) - 1);
                    const int32_t NY   = std::clamp(static_cast<int32_t>(std::lround(static_cast<float>(PY) + std::sin(Angle) * Radius)),
                                                    0, static_cast<int32_t>(HeightPx) - 1);
                    const std::size_t NOff = static_cast<std::size_t>(NY) * WidthPx + static_cast<uint32_t>(NX);
                    const auto& NHit       = PrimarySamples[NOff];
                    if (!NHit.ValidHit || NHit.MaterialSlot >= MatCount)
                    {
                        continue;
                    }

                    const float PlaneDist = std::fabs(Mathematics::EvaluateDot(N, NHit.WorldPosition - Hit.WorldPosition));
                    const float NormDot   = Mathematics::EvaluateDot(N, NHit.TangentFrame.NormalZ);
                    if (PlaneDist > 0.08f || NormDot < 0.90f)
                    {
                        continue;
                    }

                    const auto& NMat = MaterialRegister.InspectAuthoredMaterial(NHit.MaterialSlot);
                    const float AlbDiff =
                        std::fabs(CMat.Diffuse.BaseColor.R - NMat.Diffuse.BaseColor.R) +
                        std::fabs(CMat.Diffuse.BaseColor.G - NMat.Diffuse.BaseColor.G) +
                        std::fabs(CMat.Diffuse.BaseColor.B - NMat.Diffuse.BaseColor.B);
                    if (AlbDiff > 0.35f)
                    {
                        continue;
                    }

                    const auto& NRes = InitialReservoirs[NOff];
                    if (NRes.ValidFlag == 0u || NRes.SampleCountM == 0u)
                    {
                        continue;
                    }

                    float TargetAtCenter = EvaluateTargetPdfAtSurface(Hit.WorldPosition, N, NRes);
                    if (TargetAtCenter <= 1.0e-6f)
                    {
                        continue;
                    }

                    // Exact visibility + bounded distance-ratio Jacobian prevents wall/corner shadow bleeding
                    float Jacobian = 1.0f;
                    if (NRes.SkyMissFlag == 0u)
                    {
                        const Mathematics::Axis3D DeltaC = NRes.SecondaryHitPos - Hit.WorldPosition;
                        const float               DistC  = Mathematics::EvaluateLength(DeltaC);
                        if (DistC <= 0.02f)
                        {
                            continue;
                        }
                        const Mathematics::Axis3D DirC = DeltaC * (1.0f / DistC);
                        SpatialAcceleration::RaySegmentQuery VisRay {};
                        VisRay.Origin          = Hit.WorldPosition + N * 2.5e-3f;
                        VisRay.UnitDirection   = DirC;
                        VisRay.MinimumDistance = 1.0e-3f;
                        VisRay.MaximumDistance = std::max(0.01f, DistC - 0.02f);
                        if (SceneTree.QueryRayOccluded(VisRay))
                        {
                            continue;
                        }
                        const float DistN = std::max(0.05f, Mathematics::EvaluateLength(NRes.SecondaryHitPos - NHit.WorldPosition));
                        Jacobian = std::clamp((DistN * DistN) / (DistC * DistC), 0.0f, 3.0f);
                    }
                    else
                    {
                        const Mathematics::Axis3D SkyDir = Mathematics::NormalizeAxis(NRes.SecondaryHitPos, { 0.0f, 0.0f, 1.0f });
                        SpatialAcceleration::RaySegmentQuery SkyVisRay {};
                        SkyVisRay.Origin          = Hit.WorldPosition + N * 2.5e-3f;
                        SkyVisRay.UnitDirection   = SkyDir;
                        SkyVisRay.MinimumDistance = 1.0e-3f;
                        SkyVisRay.MaximumDistance = 80.0f;
                        if (SceneTree.QueryRayOccluded(SkyVisRay))
                        {
                            continue;
                        }
                    }

                    const uint32_t ClampedM = std::min(NRes.SampleCountM, 16u);
                    const float    NeighW   = TargetAtCenter * Jacobian * NRes.ContributionW * static_cast<float>(ClampedM);
                    UpdateReservoirWithCandidate(Combined, NRes, NeighW, ClampedM, RngState);
                }

                FinalizeReservoirWeight(
                    Combined,
                    EvaluateTargetPdfAtSurface(Hit.WorldPosition, N, Combined),
                    EvaluateCosThetaAtSurface(Hit.WorldPosition, N, Combined));
                Combined.SampleCountM = std::min(Combined.SampleCountM, Settings.SpatialHistoryCap);
            }

            Output.ResolvedReservoirs[Off] = Combined;

            if (Combined.ValidFlag != 0u && Combined.ContributionW > 0.0f)
            {
                const Mathematics::Axis3D DirToY = (Combined.SkyMissFlag != 0u)
                    ? Mathematics::NormalizeAxis(Combined.SecondaryHitPos, { 0.0f, 0.0f, 1.0f })
                    : Mathematics::NormalizeAxis(Combined.SecondaryHitPos - Hit.WorldPosition, { 0.0f, 0.0f, 1.0f });
                const float CosTheta = std::max(0.0f, Mathematics::EvaluateDot(N, DirToY));
                const float Scale    = (CosTheta / Constants::PiFloat) * Combined.ContributionW;
                Output.RawIndirectOverPi[Off] = Combined.Radiance * Scale;
            }
        }
    }

    Output.FilteredIndirectOverPi = Output.RawIndirectOverPi;
    if (Settings.EnableSvfgFilter && Settings.SvfgFilterPasses > 0u)
    {
        ApplyJointATrousSvgfFilter(
            WidthPx,
            HeightPx,
            PrimarySamples,
            MaterialRegister,
            Settings.SvfgFilterPasses,
            Output.FilteredIndirectOverPi);
    }

    return Output;
}

} // namespace Frontier::GeometricRaster
