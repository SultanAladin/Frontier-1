/*============================================================================================================================================
                                                       DYNAMICPROBECACHESEQUENCE.CPP
============================================================================================================================================*/
// 🧩 GPU-Parity 8x8 Octahedral 1/pi Irradiance Probe Cache + 16x16 Chebyshev Radial Moments with Backface Validity Gating

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/DynamicProbeCacheSequence.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometrySpace/Analysis/CurvatureAnalysis.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier::GeometricRaster
{

namespace
{

[[nodiscard]] float DecodeHalfFloatBits(uint16_t HalfBits) noexcept
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

[[nodiscard]] Mathematics::LinearRGB UnpackProbeSlabDiffuse(const MaterialSpace::BounceSlabEntry& Slab) noexcept
{
    return {
        DecodeHalfFloatBits(static_cast<uint16_t>(Slab.DiffuseAlbedoRgHalf & 0xFFFFu)),
        DecodeHalfFloatBits(static_cast<uint16_t>((Slab.DiffuseAlbedoRgHalf >> 16u) & 0xFFFFu)),
        DecodeHalfFloatBits(static_cast<uint16_t>(Slab.DiffuseAlbedoBAndRoughnessHalf & 0xFFFFu)),
    };
}

[[nodiscard]] Mathematics::LinearRGB UnpackProbeSlabEmissive(const MaterialSpace::BounceSlabEntry& Slab) noexcept
{
    return {
        DecodeHalfFloatBits(static_cast<uint16_t>(Slab.EmissiveExitanceRgHalf & 0xFFFFu)),
        DecodeHalfFloatBits(static_cast<uint16_t>((Slab.EmissiveExitanceRgHalf >> 16u) & 0xFFFFu)),
        DecodeHalfFloatBits(static_cast<uint16_t>(Slab.EmissiveExitanceBAndTransHalf & 0xFFFFu)),
    };
}

[[nodiscard]] Mathematics::Axis2D EncodeProbeOctahedral(Mathematics::Axis3D Dir) noexcept
{
    const float InvL1 = 1.0f / std::max(1.0e-6f, std::fabs(Dir.X) + std::fabs(Dir.Y) + std::fabs(Dir.Z));
    float OX = Dir.X * InvL1;
    float OY = Dir.Y * InvL1;
    if (Dir.Z < 0.0f)
    {
        const float WrapX = (1.0f - std::fabs(OY)) * ((OX >= 0.0f) ? 1.0f : -1.0f);
        const float WrapY = (1.0f - std::fabs(OX)) * ((OY >= 0.0f) ? 1.0f : -1.0f);
        OX = WrapX;
        OY = WrapY;
    }
    return { OX * 0.5f + 0.5f, OY * 0.5f + 0.5f };
}

[[nodiscard]] Mathematics::Axis3D DecodeProbeOctahedral(float U, float V) noexcept
{
    const float OX = U * 2.0f - 1.0f;
    const float OY = V * 2.0f - 1.0f;
    Mathematics::Axis3D Dir { OX, OY, 1.0f - std::fabs(OX) - std::fabs(OY) };
    if (Dir.Z < 0.0f)
    {
        const float FoldX = (1.0f - std::fabs(Dir.Y)) * ((Dir.X >= 0.0f) ? 1.0f : -1.0f);
        const float FoldY = (1.0f - std::fabs(Dir.X)) * ((Dir.Y >= 0.0f) ? 1.0f : -1.0f);
        Dir.X = FoldX;
        Dir.Y = FoldY;
    }
    return Mathematics::NormalizeAxis(Dir, { 0.0f, 0.0f, 1.0f });
}

template <typename ElemType, std::size_t GridW>
[[nodiscard]] ElemType SampleProbeOctahedralBilinear(
    const std::array<ElemType, GridW * GridW>& Sheet,
    Mathematics::Axis3D                        Direction) noexcept
{
    const Mathematics::Axis2D Uv = EncodeProbeOctahedral(Direction);
    const float FX = std::clamp(Uv.X * static_cast<float>(GridW) - 0.5f, 0.0f, static_cast<float>(GridW - 1u));
    const float FY = std::clamp(Uv.Y * static_cast<float>(GridW) - 0.5f, 0.0f, static_cast<float>(GridW - 1u));
    const auto  X0 = static_cast<uint32_t>(std::floor(FX));
    const auto  Y0 = static_cast<uint32_t>(std::floor(FY));
    const uint32_t X1 = std::min(static_cast<uint32_t>(GridW - 1u), X0 + 1u);
    const uint32_t Y1 = std::min(static_cast<uint32_t>(GridW - 1u), Y0 + 1u);
    const float TX = FX - static_cast<float>(X0);
    const float TY = FY - static_cast<float>(Y0);

    const ElemType& C00 = Sheet[Y0 * GridW + X0];
    const ElemType& C10 = Sheet[Y0 * GridW + X1];
    const ElemType& C01 = Sheet[Y1 * GridW + X0];
    const ElemType& C11 = Sheet[Y1 * GridW + X1];

    return (C00 * (1.0f - TX) + C10 * TX) * (1.0f - TY) +
           (C01 * (1.0f - TX) + C11 * TX) * TY;
}

[[nodiscard]] Mathematics::LinearRGB EvaluateAreaDirectContribution(
    Mathematics::Axis3D                      HitPosition,
    Mathematics::Axis3D                      HitNormal,
    const Mathematics::LinearRGB&            DiffuseAlbedo,
    std::span<const PolygonalAreaQuadLight>  AreaLights,
    const SpatialAcceleration::WideBoundingTree& SceneTree) noexcept
{
    Mathematics::LinearRGB Accum { 0.0f, 0.0f, 0.0f };
    for (const auto& Light : AreaLights)
    {
        const Mathematics::Axis3D Delta = Light.CenterWorld - HitPosition;
        const float DistSq = std::max(0.04f, Mathematics::EvaluateDot(Delta, Delta));
        const float Dist   = std::sqrt(DistSq);
        const Mathematics::Axis3D Dir = Delta * (1.0f / Dist);
        const float NdotL = Mathematics::EvaluateDot(HitNormal, Dir);
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
        VisRay.Origin          = HitPosition + HitNormal * 2.5e-3f;
        VisRay.UnitDirection   = Dir;
        VisRay.MinimumDistance = 1.0e-3f;
        VisRay.MaximumDistance = std::max(0.01f, Dist - 0.04f);
        if (SceneTree.QueryRayOccluded(VisRay))
        {
            continue;
        }

        const float Area   = 4.0f * Light.HalfWidthMeters * Light.HalfHeightMeters;
        const float Factor = (NdotL * LdotNeg * Area) / (Constants::PiFloat * DistSq + Area);
        Accum.R += Light.EmittedRadiance.R * DiffuseAlbedo.R * Factor;
        Accum.G += Light.EmittedRadiance.G * DiffuseAlbedo.G * Factor;
        Accum.B += Light.EmittedRadiance.B * DiffuseAlbedo.B * Factor;
    }
    return Accum;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                             DYNAMIC PROBE CACHE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

void DynamicProbeCacheSequence::AssignExternalCells(
    const DynamicProbeCacheConfiguration& Config,
    std::vector<DynamicProbeCell>&&       ExternalCells) noexcept
{
    ActiveConfig = Config;
    const uint32_t CX = std::max(2u, Config.CountX);
    const uint32_t CY = std::max(2u, Config.CountY);
    const uint32_t CZ = std::max(2u, Config.CountZ);
    const Mathematics::Axis3D Span = Config.GridMaximumWorld - Config.GridMinimumWorld;
    CellSpacing = {
        std::max(0.05f, Span.X / static_cast<float>(CX - 1u)),
        std::max(0.05f, Span.Y / static_cast<float>(CY - 1u)),
        std::max(0.05f, Span.Z / static_cast<float>(CZ - 1u)),
    };
    Cells = std::move(ExternalCells);
}

void DynamicProbeCacheSequence::UpdateProbeCache(
    const DynamicProbeCacheConfiguration&           Config,
    const SpatialAcceleration::WideBoundingTree&    SceneTree,
    std::span<const MaterialSpace::BounceSlabEntry> BounceSummaries,
    const SolarDirectionalEmitter&                  Sun,
    std::span<const PolygonalAreaQuadLight>         AreaLights,
    Mathematics::LinearRGB                          SkyRadiance)
{
    const bool PreserveHistory = (Config.TemporalBlend > 0.01f) &&
                                 (Cells.size() == static_cast<std::size_t>(std::max(2u, Config.CountX)) *
                                                  std::max(2u, Config.CountY) *
                                                  std::max(2u, Config.CountZ));
    std::vector<DynamicProbeCell> PrevCells;
    if (PreserveHistory)
    {
        PrevCells = Cells;
    }

    ActiveConfig = Config;
    ActiveConfig.CountX            = std::max(2u, Config.CountX);
    ActiveConfig.CountY            = std::max(2u, Config.CountY);
    ActiveConfig.CountZ            = std::max(2u, Config.CountZ);
    ActiveConfig.FibonacciRayCount = std::clamp(Config.FibonacciRayCount, 32u, 256u);
    ActiveConfig.BouncePasses      = std::clamp(Config.BouncePasses, 1u, 4u);

    const Mathematics::Axis3D Span = ActiveConfig.GridMaximumWorld - ActiveConfig.GridMinimumWorld;
    CellSpacing = {
        std::max(0.05f, Span.X / static_cast<float>(ActiveConfig.CountX - 1u)),
        std::max(0.05f, Span.Y / static_cast<float>(ActiveConfig.CountY - 1u)),
        std::max(0.05f, Span.Z / static_cast<float>(ActiveConfig.CountZ - 1u)),
    };

    const std::size_t TotalCells = static_cast<std::size_t>(ActiveConfig.CountX) *
                                   ActiveConfig.CountY *
                                   ActiveConfig.CountZ;
    Cells.assign(TotalCells, DynamicProbeCell{});

    struct ProbeRayTraceRecord
    {
        Mathematics::Axis3D Direction       {};
        Mathematics::Axis3D HitPosition     {};
        Mathematics::Axis3D HitNormal       {};
        float               HitDistance     = 100.0f;
        uint16_t            MaterialOrdinal = 0u;
        bool                HitSurface      = false;
        bool                BackfaceHit     = false;
    };

    const uint32_t RayCount = ActiveConfig.FibonacciRayCount;
    const float    MaxDepthClamp = Mathematics::EvaluateLength(CellSpacing) * 2.25f;
    std::vector<std::vector<ProbeRayTraceRecord>> CellRays(TotalCells, std::vector<ProbeRayTraceRecord>(RayCount));

    const float GoldenRatioInv = 0.61803398875f;
    const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis(Sun.DirectionToSun, { 0.42f, -0.55f, 0.72f });

    // Step 1: Trace Spherical Fibonacci probe rays against BVH4 + detect reverse hits + encode 16x16 Chebyshev moments
    for (uint32_t IZ = 0u; IZ < ActiveConfig.CountZ; ++IZ)
    {
        for (uint32_t IY = 0u; IY < ActiveConfig.CountY; ++IY)
        {
            for (uint32_t IX = 0u; IX < ActiveConfig.CountX; ++IX)
            {
                const std::size_t CellIdx = (static_cast<std::size_t>(IZ) * ActiveConfig.CountY + IY) * ActiveConfig.CountX + IX;
                const Mathematics::Axis3D CellPos {
                    ActiveConfig.GridMinimumWorld.X + static_cast<float>(IX) * CellSpacing.X,
                    ActiveConfig.GridMinimumWorld.Y + static_cast<float>(IY) * CellSpacing.Y,
                    ActiveConfig.GridMinimumWorld.Z + static_cast<float>(IZ) * CellSpacing.Z,
                };
                Cells[CellIdx].WorldPosition = CellPos;

                uint32_t BackfaceHits = 0u;
                auto& RayList = CellRays[CellIdx];
                for (uint32_t RIdx = 0u; RIdx < RayCount; ++RIdx)
                {
                    const float Z     = 1.0f - (2.0f * (static_cast<float>(RIdx) + 0.5f)) / static_cast<float>(RayCount);
                    const float RadXY = std::sqrt(std::max(0.0f, 1.0f - Z * Z));
                    const float Phi   = Constants::TauFloat * std::fmod(static_cast<float>(RIdx) * GoldenRatioInv, 1.0f);
                    const Mathematics::Axis3D RayDir { RadXY * std::cos(Phi), RadXY * std::sin(Phi), Z };

                    ProbeRayTraceRecord Rec {};
                    Rec.Direction = RayDir;

                    SpatialAcceleration::RaySegmentQuery Ray {};
                    Ray.Origin          = CellPos;
                    Ray.UnitDirection   = RayDir;
                    Ray.MinimumDistance = 1.0e-3f;
                    Ray.MaximumDistance = 60.0f;

                    const auto Hit = SceneTree.TraceClosestRay(Ray, false);
                    if (Hit.HitConfirmed)
                    {
                        Rec.HitSurface      = true;
                        Rec.HitDistance     = Hit.HitDistanceMeters;
                        Rec.HitPosition     = CellPos + RayDir * Hit.HitDistanceMeters;
                        Rec.MaterialOrdinal = Hit.MaterialOrdinal;
                        const float Side    = Mathematics::EvaluateDot(Hit.GeometricNormal, RayDir);
                        Rec.BackfaceHit     = (Side >= 0.0f);
                        if (Rec.BackfaceHit)
                        {
                            ++BackfaceHits;
                        }
                        Rec.HitNormal       = Rec.BackfaceHit ? (Hit.GeometricNormal * -1.0f) : Hit.GeometricNormal;
                    }
                    else
                    {
                        Rec.HitDistance = MaxDepthClamp;
                    }
                    RayList[RIdx] = Rec;
                }

                Cells[CellIdx].BackfaceRatio = static_cast<float>(BackfaceHits) / static_cast<float>(RayCount);

                // Convolve 16x16 Chebyshev distance moments (MeanR, MeanR^2)
                for (uint32_t OY = 0u; OY < ProbeChebyshevOctaWidth; ++OY)
                {
                    for (uint32_t OX = 0u; OX < ProbeChebyshevOctaWidth; ++OX)
                    {
                        const Mathematics::Axis3D TexDir = DecodeProbeOctahedral(
                            (static_cast<float>(OX) + 0.5f) / static_cast<float>(ProbeChebyshevOctaWidth),
                            (static_cast<float>(OY) + 0.5f) / static_cast<float>(ProbeChebyshevOctaWidth));

                        float SumR  = 0.0f;
                        float SumR2 = 0.0f;
                        float SumW  = 0.0f;
                        for (uint32_t RIdx = 0u; RIdx < RayCount; ++RIdx)
                        {
                            const float CosAlign = Mathematics::EvaluateDot(TexDir, RayList[RIdx].Direction);
                            if (CosAlign > 0.0f)
                            {
                                const float W        = std::pow(CosAlign, 16.0f);
                                const float ClampedD = std::min(MaxDepthClamp, RayList[RIdx].HitDistance);
                                SumR  += ClampedD * W;
                                SumR2 += (ClampedD * ClampedD) * W;
                                SumW  += W;
                            }
                        }
                        Cells[CellIdx].DepthMoments[OY * ProbeChebyshevOctaWidth + OX] = (SumW > 1.0e-6f)
                            ? Mathematics::Axis2D{ SumR / SumW, SumR2 / SumW }
                            : Mathematics::Axis2D{ MaxDepthClamp, MaxDepthClamp * MaxDepthClamp };
                    }
                }
            }
        }
    }

    // Step 2: Multi-Bounce Radiance Propagation (1/pi normalized cosine convolution)
    for (uint32_t Pass = 0u; Pass < ActiveConfig.BouncePasses; ++Pass)
    {
        std::vector<std::array<Mathematics::LinearRGB, ProbeIrradianceOctaWidth * ProbeIrradianceOctaWidth>> NextIrradiance(TotalCells);

        for (std::size_t CellIdx = 0u; CellIdx < TotalCells; ++CellIdx)
        {
            const auto& RayList = CellRays[CellIdx];
            std::vector<Mathematics::LinearRGB> RayRadiances(RayCount, { 0.0f, 0.0f, 0.0f });

            for (uint32_t RIdx = 0u; RIdx < RayCount; ++RIdx)
            {
                const auto& Sample = RayList[RIdx];
                if (!Sample.HitSurface)
                {
                    RayRadiances[RIdx] = SkyRadiance;
                    continue;
                }
                if (Sample.BackfaceHit)
                {
                    RayRadiances[RIdx] = { 0.0f, 0.0f, 0.0f };
                    continue;
                }

                const uint16_t Slot = (Sample.MaterialOrdinal < BounceSummaries.size()) ? Sample.MaterialOrdinal : 0u;
                const auto& Slab    = BounceSummaries[Slot];
                const auto  DiffAlb = UnpackProbeSlabDiffuse(Slab);

                Mathematics::LinearRGB HitExitance = UnpackProbeSlabEmissive(Slab);
                if (Mathematics::EvaluateDot(Sample.HitNormal, SunDir) > 0.0f)
                {
                    SpatialAcceleration::RaySegmentQuery SunRay {};
                    SunRay.Origin          = Sample.HitPosition + Sample.HitNormal * 2.5e-3f;
                    SunRay.UnitDirection   = SunDir;
                    SunRay.MinimumDistance = 1.0e-3f;
                    SunRay.MaximumDistance = 120.0f;
                    if (!SceneTree.QueryRayOccluded(SunRay))
                    {
                        HitExitance = HitExitance + MaterialSpace::EvaluateBounceSlabRadiance(
                            Slab, Sample.HitNormal, Sample.Direction * -1.0f, SunDir, Sun.SolarIrradiance);
                    }
                }
                if (!AreaLights.empty())
                {
                    HitExitance = HitExitance + EvaluateAreaDirectContribution(
                        Sample.HitPosition, Sample.HitNormal, DiffAlb, AreaLights, SceneTree);
                }
                if (Pass > 0u)
                {
                    const auto PrevBounce = SampleIrradianceOverPi(
                        Sample.HitPosition, Sample.HitNormal, Sample.Direction * -1.0f);
                    HitExitance.R += PrevBounce.R * DiffAlb.R;
                    HitExitance.G += PrevBounce.G * DiffAlb.G;
                    HitExitance.B += PrevBounce.B * DiffAlb.B;
                }
                RayRadiances[RIdx] = HitExitance;
            }

            for (uint32_t OY = 0u; OY < ProbeIrradianceOctaWidth; ++OY)
            {
                for (uint32_t OX = 0u; OX < ProbeIrradianceOctaWidth; ++OX)
                {
                    const Mathematics::Axis3D NormalDir = DecodeProbeOctahedral(
                        (static_cast<float>(OX) + 0.5f) / static_cast<float>(ProbeIrradianceOctaWidth),
                        (static_cast<float>(OY) + 0.5f) / static_cast<float>(ProbeIrradianceOctaWidth));

                    Mathematics::LinearRGB SumLw { 0.0f, 0.0f, 0.0f };
                    float                  SumCosWeight = 0.0f;
                    for (uint32_t RIdx = 0u; RIdx < RayCount; ++RIdx)
                    {
                        const float CosTheta = Mathematics::EvaluateDot(NormalDir, RayList[RIdx].Direction);
                        if (CosTheta > 0.0f)
                        {
                            SumLw        = SumLw + RayRadiances[RIdx] * CosTheta;
                            SumCosWeight += CosTheta;
                        }
                    }
                    const float NormScale = (SumCosWeight > 1.0e-5f) ? (1.0f / SumCosWeight) : 0.0f;
                    NextIrradiance[CellIdx][OY * ProbeIrradianceOctaWidth + OX] = SumLw * NormScale;
                }
            }
        }

        for (std::size_t CellIdx = 0u; CellIdx < TotalCells; ++CellIdx)
        {
            if (PreserveHistory && (Pass + 1u == ActiveConfig.BouncePasses))
            {
                const float Alpha = std::clamp(1.0f - ActiveConfig.TemporalBlend, 0.05f, 1.0f);
                for (std::size_t T = 0u; T < Cells[CellIdx].IrradianceOverPi.size(); ++T)
                {
                    const auto& Cur  = NextIrradiance[CellIdx][T];
                    const auto& Prev = PrevCells[CellIdx].IrradianceOverPi[T];
                    Cells[CellIdx].IrradianceOverPi[T] = {
                        Prev.R + (Cur.R - Prev.R) * Alpha,
                        Prev.G + (Cur.G - Prev.G) * Alpha,
                        Prev.B + (Cur.B - Prev.B) * Alpha,
                    };
                }
            }
            else
            {
                Cells[CellIdx].IrradianceOverPi = NextIrradiance[CellIdx];
            }
        }
    }
}

Mathematics::LinearRGB DynamicProbeCacheSequence::SampleIrradianceOverPi(
    Mathematics::Axis3D WorldPosition,
    Mathematics::Axis3D SurfaceNormal,
    Mathematics::Axis3D ViewDirection) const noexcept
{
    if (Cells.empty())
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const float BiasMag = std::min(0.09f, 0.14f * std::min({ CellSpacing.X, CellSpacing.Y, CellSpacing.Z }));
    const Mathematics::Axis3D BiasedPos = WorldPosition + (SurfaceNormal * 0.72f + ViewDirection * 0.28f) * BiasMag;

    const float FX = std::clamp(
        (BiasedPos.X - ActiveConfig.GridMinimumWorld.X) / CellSpacing.X,
        0.0f,
        static_cast<float>(ActiveConfig.CountX - 1u));
    const float FY = std::clamp(
        (BiasedPos.Y - ActiveConfig.GridMinimumWorld.Y) / CellSpacing.Y,
        0.0f,
        static_cast<float>(ActiveConfig.CountY - 1u));
    const float FZ = std::clamp(
        (BiasedPos.Z - ActiveConfig.GridMinimumWorld.Z) / CellSpacing.Z,
        0.0f,
        static_cast<float>(ActiveConfig.CountZ - 1u));

    const auto IX0 = static_cast<uint32_t>(std::floor(FX));
    const auto IY0 = static_cast<uint32_t>(std::floor(FY));
    const auto IZ0 = static_cast<uint32_t>(std::floor(FZ));
    const uint32_t IX1 = std::min(ActiveConfig.CountX - 1u, IX0 + 1u);
    const uint32_t IY1 = std::min(ActiveConfig.CountY - 1u, IY0 + 1u);
    const uint32_t IZ1 = std::min(ActiveConfig.CountZ - 1u, IZ0 + 1u);
    const float TX = FX - static_cast<float>(IX0);
    const float TY = FY - static_cast<float>(IY0);
    const float TZ = FZ - static_cast<float>(IZ0);

    Mathematics::LinearRGB Accum { 0.0f, 0.0f, 0.0f };
    float                  TotalWeight = 0.0f;

    for (uint32_t DZ = 0u; DZ < 2u; ++DZ)
    {
        const uint32_t CZ = (DZ == 0u) ? IZ0 : IZ1;
        const float    WZ = (DZ == 0u) ? (1.0f - TZ) : TZ;
        for (uint32_t DY = 0u; DY < 2u; ++DY)
        {
            const uint32_t CY = (DY == 0u) ? IY0 : IY1;
            const float    WY = (DY == 0u) ? (1.0f - TY) : TY;
            for (uint32_t DX = 0u; DX < 2u; ++DX)
            {
                const uint32_t CX = (DX == 0u) ? IX0 : IX1;
                const float    WX = (DX == 0u) ? (1.0f - TX) : TX;

                const std::size_t CellIdx = (static_cast<std::size_t>(CZ) * ActiveConfig.CountY + CY) * ActiveConfig.CountX + CX;
                const DynamicProbeCell& Cell = Cells[CellIdx];

                const Mathematics::Axis3D CellToSurface = BiasedPos - Cell.WorldPosition;
                const float DistToProbe = Mathematics::EvaluateLength(CellToSurface);
                const Mathematics::Axis3D DirProbeToSurf = (DistToProbe > 1.0e-5f)
                    ? (CellToSurface * (1.0f / DistToProbe))
                    : (SurfaceNormal * -1.0f);
                const Mathematics::Axis3D DirSurfToProbe = DirProbeToSurf * -1.0f;

                // Wrapped cosine-squared smooth falloff + backface probe rejection + cubed Chebyshev visibility
                const float WrappedDot = 0.5f * (Mathematics::EvaluateDot(DirSurfToProbe, SurfaceNormal) + 1.0f);
                const float SmoothBack = std::max(0.02f, WrappedDot * WrappedDot);
                const float ValidGate  = (Cell.BackfaceRatio > 0.25f) ? 0.02f : 1.0f;

                const Mathematics::Axis2D Moments = SampleProbeOctahedralBilinear<Mathematics::Axis2D, ProbeChebyshevOctaWidth>(
                    Cell.DepthMoments, DirProbeToSurf);
                const float MeanR    = Moments.X;
                const float Variance = std::max(1.0e-4f, std::fabs(Moments.Y - MeanR * MeanR));
                float Chebyshev      = 1.0f;
                if (DistToProbe > MeanR)
                {
                    const float Excess = DistToProbe - MeanR;
                    Chebyshev = Variance / (Variance + Excess * Excess);
                    Chebyshev = Chebyshev * Chebyshev * Chebyshev;
                }

                const float CellWeight = WX * WY * WZ * SmoothBack * ValidGate * std::max(1.0e-4f, Chebyshev);
                const auto  CellIrr    = SampleProbeOctahedralBilinear<Mathematics::LinearRGB, ProbeIrradianceOctaWidth>(
                    Cell.IrradianceOverPi, SurfaceNormal);

                Accum       = Accum + CellIrr * CellWeight;
                TotalWeight += CellWeight;
            }
        }
    }

    return (TotalWeight > 1.0e-6f) ? (Accum * (1.0f / TotalWeight)) : Mathematics::LinearRGB{ 0.0f, 0.0f, 0.0f };
}

} // namespace Frontier::GeometricRaster
