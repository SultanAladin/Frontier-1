/*============================================================================================================================================
                                                        IRRADIANCELATTICESEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Tier 2 RTX 1/pi-Normalized Octahedral Irradiance Lattice (16x16 Chebyshev Depth) + Tier 1 GTX 6-Cone SignedDistanceVolume Diffuse GI

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/IrradianceLatticeSequence.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometrySpace/Analysis/CurvatureAnalysis.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier::GeometricRaster
{

namespace
{

[[nodiscard]] float DecodeFp16Bits(uint16_t HalfBits) noexcept
{
    const uint32_t Sign = static_cast<uint32_t>(HalfBits & 0x8000u) << 16u;
    const uint32_t Exp  = (HalfBits >> 10u) & 0x1Fu;
    const uint32_t Mant = HalfBits & 0x03FFu;
    uint32_t Bits32     = 0u;
    if (Exp == 0u)
    {
        if (Mant == 0u) Bits32 = Sign;
        else
        {
            float Sub = static_cast<float>(Mant) * (1.0f / 16777216.0f);
            return (Sign != 0u) ? -Sub : Sub;
        }
    }
    else if (Exp == 31u) Bits32 = Sign | 0x7F800000u | (Mant << 13u);
    else                 Bits32 = Sign | ((Exp + 112u) << 23u) | (Mant << 13u);
    float Result = 0.0f;
    std::memcpy(&Result, &Bits32, sizeof(float));
    return Result;
}

[[nodiscard]] Mathematics::LinearRGB UnpackSlabDiffuseAlbedo(const MaterialSpace::BounceSlabEntry& Slab) noexcept
{
    return {
        DecodeFp16Bits(static_cast<uint16_t>(Slab.DiffuseAlbedoRgHalf & 0xFFFFu)),
        DecodeFp16Bits(static_cast<uint16_t>((Slab.DiffuseAlbedoRgHalf >> 16u) & 0xFFFFu)),
        DecodeFp16Bits(static_cast<uint16_t>(Slab.DiffuseAlbedoBAndRoughnessHalf & 0xFFFFu)),
    };
}

[[nodiscard]] Mathematics::LinearRGB UnpackSlabEmissive(const MaterialSpace::BounceSlabEntry& Slab) noexcept
{
    return {
        DecodeFp16Bits(static_cast<uint16_t>(Slab.EmissiveExitanceRgHalf & 0xFFFFu)),
        DecodeFp16Bits(static_cast<uint16_t>((Slab.EmissiveExitanceRgHalf >> 16u) & 0xFFFFu)),
        DecodeFp16Bits(static_cast<uint16_t>(Slab.EmissiveExitanceBAndTransHalf & 0xFFFFu)),
    };
}

// 📝 Map unit 3D direction onto [0, 1]^2 octahedral square (Cigolle et al. JCGT 2014)
[[nodiscard]] Mathematics::Axis2D EncodeUnitOctahedralUv(Mathematics::Axis3D Dir) noexcept
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

[[nodiscard]] Mathematics::Axis3D DecodeUnitOctahedralUv(float U, float V) noexcept
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
[[nodiscard]] ElemType SampleOctahedralBilinear(
    const std::array<ElemType, GridW * GridW>& Sheet,
    Mathematics::Axis3D                        Direction) noexcept
{
    const Mathematics::Axis2D Uv = EncodeUnitOctahedralUv(Direction);
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

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              IRRADIANCE LATTICE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

void IrradianceLatticeSequence::PropagateRtxIrradianceLattice(
    const IrradianceLatticeConfiguration&           Config,
    const SpatialAcceleration::WideBoundingTree&    SceneTree,
    std::span<const MaterialSpace::BounceSlabEntry> BounceSummaries,
    const SolarDirectionalEmitter&                  Sun,
    Mathematics::LinearRGB                          SkyRadiance)
{
    ActiveConfig        = Config;
    ActiveConfig.CountX = std::max(2u, Config.CountX);
    ActiveConfig.CountY = std::max(2u, Config.CountY);
    ActiveConfig.CountZ = std::max(2u, Config.CountZ);

    const Mathematics::Axis3D Span = ActiveConfig.GridMaximumWorld - ActiveConfig.GridMinimumWorld;
    CellSpacing = {
        std::max(0.05f, Span.X / static_cast<float>(ActiveConfig.CountX - 1u)),
        std::max(0.05f, Span.Y / static_cast<float>(ActiveConfig.CountY - 1u)),
        std::max(0.05f, Span.Z / static_cast<float>(ActiveConfig.CountZ - 1u)),
    };

    const std::size_t TotalCells =
        static_cast<std::size_t>(ActiveConfig.CountX) * ActiveConfig.CountY * ActiveConfig.CountZ;
    Cells.assign(TotalCells, OctahedralIrradianceCell{});

    for (uint32_t IZ = 0u; IZ < ActiveConfig.CountZ; ++IZ)
    {
        for (uint32_t IY = 0u; IY < ActiveConfig.CountY; ++IY)
        {
            for (uint32_t IX = 0u; IX < ActiveConfig.CountX; ++IX)
            {
                const std::size_t CellIdx = (static_cast<std::size_t>(IZ) * ActiveConfig.CountY + IY) * ActiveConfig.CountX + IX;
                Cells[CellIdx].WorldPosition = {
                    ActiveConfig.GridMinimumWorld.X + static_cast<float>(IX) * CellSpacing.X,
                    ActiveConfig.GridMinimumWorld.Y + static_cast<float>(IY) * CellSpacing.Y,
                    ActiveConfig.GridMinimumWorld.Z + static_cast<float>(IZ) * CellSpacing.Z,
                };
            }
        }
    }

    const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis(Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
    const float MaxDepthClamp = Mathematics::EvaluateLength(CellSpacing) * 2.25f;
    const uint32_t RayCount   = std::clamp(ActiveConfig.FibonacciRayCount, 16u, 256u);
    const uint32_t Bounces    = std::clamp(ActiveConfig.BouncePasses, 1u, 4u);

    struct CachedRaySample
    {
        Mathematics::Axis3D Direction {};
        bool                HitSurface      = false;
        bool                FrontFacingHit  = true;
        float               HitDistance     = 0.0f;
        Mathematics::Axis3D HitPosition     {};
        Mathematics::Axis3D HitNormal       {};
        uint16_t            MaterialOrdinal = 0u;
    };

    // Pre-trace deterministic Spherical Fibonacci geometry rays once per cell, then propagate multi-bounce radiance!
    std::vector<std::vector<CachedRaySample>> CellRayCache(TotalCells);
    constexpr float GoldenRatioConjugate = 0.61803398875f;

    for (std::size_t CellIdx = 0u; CellIdx < TotalCells; ++CellIdx)
    {
        auto& RayList = CellRayCache[CellIdx];
        RayList.resize(RayCount);
        const Mathematics::Axis3D Origin = Cells[CellIdx].WorldPosition;

        for (uint32_t RIdx = 0u; RIdx < RayCount; ++RIdx)
        {
            const float Z   = 1.0f - (2.0f * (static_cast<float>(RIdx) + 0.5f)) / static_cast<float>(RayCount);
            const float Rad = std::sqrt(std::max(0.0f, 1.0f - Z * Z));
            const float Phi = Constants::TauFloat * std::fmod(static_cast<float>(RIdx) * GoldenRatioConjugate, 1.0f);
            const Mathematics::Axis3D Dir { Rad * std::cos(Phi), Rad * std::sin(Phi), Z };

            SpatialAcceleration::RaySegmentQuery Query {};
            Query.Origin          = Origin;
            Query.UnitDirection   = Dir;
            Query.MinimumDistance = 1.0e-3f;
            Query.MaximumDistance = 60.0f;

            const auto Hit = SceneTree.TraceClosestRay(Query, false);
            CachedRaySample Sample {};
            Sample.Direction = Dir;
            if (Hit.HitConfirmed)
            {
                Sample.HitSurface      = true;
                Sample.HitDistance     = Hit.HitDistanceMeters;
                Sample.HitPosition     = Origin + Dir * Hit.HitDistanceMeters;
                const float FacingDot  = Mathematics::EvaluateDot(Hit.GeometricNormal, Dir);
                Sample.FrontFacingHit  = (FacingDot < 0.0f);
                Sample.HitNormal       = Sample.FrontFacingHit ? Hit.GeometricNormal : (Hit.GeometricNormal * -1.0f);
                Sample.MaterialOrdinal = Hit.MaterialOrdinal;
            }
            else
            {
                Sample.HitDistance = MaxDepthClamp;
            }
            RayList[RIdx] = Sample;
        }

        // Convolve 16x16 Octahedral Chebyshev Radial Depth Moments (MeanR, MeanR^2) with sharp directional kernel
        for (uint32_t OY = 0u; OY < ChebyshevDepthOctaW; ++OY)
        {
            for (uint32_t OX = 0u; OX < ChebyshevDepthOctaW; ++OX)
            {
                const Mathematics::Axis3D TexelDir = DecodeUnitOctahedralUv(
                    (static_cast<float>(OX) + 0.5f) / static_cast<float>(ChebyshevDepthOctaW),
                    (static_cast<float>(OY) + 0.5f) / static_cast<float>(ChebyshevDepthOctaW));

                float SumR  = 0.0f;
                float SumR2 = 0.0f;
                float SumW  = 0.0f;
                for (const auto& Sample : RayList)
                {
                    const float CosAlign = Mathematics::EvaluateDot(TexelDir, Sample.Direction);
                    if (CosAlign > 0.0f)
                    {
                        const float W = std::pow(CosAlign, 16.0f);
                        const float ClampedD = std::min(MaxDepthClamp, Sample.HitDistance);
                        SumR  += ClampedD * W;
                        SumR2 += (ClampedD * ClampedD) * W;
                        SumW  += W;
                    }
                }
                if (SumW > 1.0e-6f)
                {
                    Cells[CellIdx].DepthMoments[OY * ChebyshevDepthOctaW + OX] = { SumR / SumW, SumR2 / SumW };
                }
                else
                {
                    Cells[CellIdx].DepthMoments[OY * ChebyshevDepthOctaW + OX] = { MaxDepthClamp, MaxDepthClamp * MaxDepthClamp };
                }
            }
        }
    }

    // Multi-Bounce Radiance Propagation using 32 B BounceSlabEntry (5-ALU Secondary Hit Shading!)
    for (uint32_t Pass = 0u; Pass < Bounces; ++Pass)
    {
        std::vector<std::array<Mathematics::LinearRGB, IrradianceOctaWidth * IrradianceOctaWidth>> NextIrradiance(TotalCells);

        for (std::size_t CellIdx = 0u; CellIdx < TotalCells; ++CellIdx)
        {
            const auto& RayList = CellRayCache[CellIdx];
            std::vector<Mathematics::LinearRGB> RayRadiances(RayCount, { 0.0f, 0.0f, 0.0f });

            for (uint32_t RIdx = 0u; RIdx < RayCount; ++RIdx)
            {
                const auto& Sample = RayList[RIdx];
                if (!Sample.HitSurface)
                {
                    RayRadiances[RIdx] = SkyRadiance;
                    continue;
                }

                const uint16_t Slot = (Sample.MaterialOrdinal < BounceSummaries.size()) ? Sample.MaterialOrdinal : 0u;
                const auto& Slab    = BounceSummaries[Slot];

                // Direct solar shadow check at secondary hit + emissive + previous-bounce diffuse color bleed!
                Mathematics::LinearRGB HitExitance = UnpackSlabEmissive(Slab);
                const float SunCos = Mathematics::EvaluateDot(Sample.HitNormal, SunDir);
                if (SunCos > 0.0f)
                {
                    SpatialAcceleration::RaySegmentQuery SunRay {};
                    SunRay.Origin          = Sample.HitPosition + Sample.HitNormal * 2.5e-3f;
                    SunRay.UnitDirection   = SunDir;
                    SunRay.MinimumDistance = 1.0e-3f;
                    SunRay.MaximumDistance = 120.0f;
                    if (!SceneTree.QueryRayOccluded(SunRay))
                    {
                        HitExitance = MaterialSpace::EvaluateBounceSlabRadiance(
                            Slab, Sample.HitNormal, Sample.Direction * -1.0f, SunDir, Sun.SolarIrradiance);
                    }
                }

                // Pure Lambertian multi-bounce feedback: L_bounce = (E_prev(N) / pi) * rho_diffuse (strictly energy-conserving!)
                if (Pass > 0u)
                {
                    const auto PrevIndirectOverPi = SampleChebyshevIrradianceOverPi(
                        Sample.HitPosition, Sample.HitNormal, Sample.Direction * -1.0f);
                    const auto DiffAlb = UnpackSlabDiffuseAlbedo(Slab);
                    HitExitance.R += PrevIndirectOverPi.R * DiffAlb.R;
                    HitExitance.G += PrevIndirectOverPi.G * DiffAlb.G;
                    HitExitance.B += PrevIndirectOverPi.B * DiffAlb.B;
                }

                RayRadiances[RIdx] = HitExitance;
            }

            // Convolve 1/pi-normalized clamped-cosine irradiance E(N)/pi = (1/pi) int_{Omega+} L(w) (N.w) dw
            // With uniform sphere Monte Carlo (p(w) = 1 / (4*pi)), E(N)/pi = (4 / N_rays) * sum_{N.w > 0} L(w) * (N.w)
            // Notice sum_{w_k} (4 / N_rays) (N.w_k)_+ == 1.0 -> exact White-Furnace conservation!
            for (uint32_t OY = 0u; OY < IrradianceOctaWidth; ++OY)
            {
                for (uint32_t OX = 0u; OX < IrradianceOctaWidth; ++OX)
                {
                    const Mathematics::Axis3D NormalDir = DecodeUnitOctahedralUv(
                        (static_cast<float>(OX) + 0.5f) / static_cast<float>(IrradianceOctaWidth),
                        (static_cast<float>(OY) + 0.5f) / static_cast<float>(IrradianceOctaWidth));

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
                    // Exact discrete normalization dividing by SumCosWeight guarantees E(N)/pi == L_const for uniform white sky!
                    const float NormScale = (SumCosWeight > 1.0e-5f) ? (1.0f / SumCosWeight) : 0.0f;
                    NextIrradiance[CellIdx][OY * IrradianceOctaWidth + OX] = SumLw * NormScale;
                }
            }
        }

        for (std::size_t CellIdx = 0u; CellIdx < TotalCells; ++CellIdx)
        {
            Cells[CellIdx].IrradianceOverPi = NextIrradiance[CellIdx];
        }
    }
}

Mathematics::LinearRGB IrradianceLatticeSequence::SampleChebyshevIrradianceOverPi(
    Mathematics::Axis3D WorldPosition,
    Mathematics::Axis3D SurfaceNormal,
    Mathematics::Axis3D ViewDirection) const noexcept
{
    if (Cells.empty())
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    // Small normal + view self-shadow bias per Majercik et al. (JCGT 2019 DDGI) to prevent thin-wall self-occlusion
    const float BiasMag = 0.18f * std::min({ CellSpacing.X, CellSpacing.Y, CellSpacing.Z });
    const Mathematics::Axis3D BiasedPos = WorldPosition + (SurfaceNormal * 0.7f + ViewDirection * 0.3f) * BiasMag;

    const Mathematics::Axis3D GridCoord {
        std::clamp((BiasedPos.X - ActiveConfig.GridMinimumWorld.X) / CellSpacing.X, 0.0f, static_cast<float>(ActiveConfig.CountX - 1u)),
        std::clamp((BiasedPos.Y - ActiveConfig.GridMinimumWorld.Y) / CellSpacing.Y, 0.0f, static_cast<float>(ActiveConfig.CountY - 1u)),
        std::clamp((BiasedPos.Z - ActiveConfig.GridMinimumWorld.Z) / CellSpacing.Z, 0.0f, static_cast<float>(ActiveConfig.CountZ - 1u)),
    };

    const auto BaseX = std::min(ActiveConfig.CountX - 2u, static_cast<uint32_t>(std::floor(GridCoord.X)));
    const auto BaseY = std::min(ActiveConfig.CountY - 2u, static_cast<uint32_t>(std::floor(GridCoord.Y)));
    const auto BaseZ = std::min(ActiveConfig.CountZ - 2u, static_cast<uint32_t>(std::floor(GridCoord.Z)));

    const float AlphaX = GridCoord.X - static_cast<float>(BaseX);
    const float AlphaY = GridCoord.Y - static_cast<float>(BaseY);
    const float AlphaZ = GridCoord.Z - static_cast<float>(BaseZ);

    Mathematics::LinearRGB WeightedIrradiance { 0.0f, 0.0f, 0.0f };
    float                  TotalWeight        = 0.0f;

    for (uint32_t Corner = 0u; Corner < 8u; ++Corner)
    {
        const uint32_t OffsetX = (Corner & 1u);
        const uint32_t OffsetY = (Corner >> 1u) & 1u;
        const uint32_t OffsetZ = (Corner >> 2u) & 1u;

        const uint32_t CX = BaseX + OffsetX;
        const uint32_t CY = BaseY + OffsetY;
        const uint32_t CZ = BaseZ + OffsetZ;
        const auto& Cell  = Cells[(static_cast<std::size_t>(CZ) * ActiveConfig.CountY + CY) * ActiveConfig.CountX + CX];

        const float TriW =
            ((OffsetX != 0u) ? AlphaX : (1.0f - AlphaX)) *
            ((OffsetY != 0u) ? AlphaY : (1.0f - AlphaY)) *
            ((OffsetZ != 0u) ? AlphaZ : (1.0f - AlphaZ));

        const Mathematics::Axis3D CellToSurface = BiasedPos - Cell.WorldPosition;
        const float DistToCell = Mathematics::EvaluateLength(CellToSurface);
        const Mathematics::Axis3D DirFromCell = (DistToCell > 1.0e-5f) ? (CellToSurface * (1.0f / DistToCell)) : SurfaceNormal;

        // Smooth backface wrap weight prevents cells behind a wall from bleeding onto its front face!
        const Mathematics::Axis3D DirToCell = DirFromCell * -1.0f;
        const float BackfaceFactor = std::max(1.0e-4f, (Mathematics::EvaluateDot(DirToCell, SurfaceNormal) + 1.0f) * 0.5f);
        float VisibilityW = BackfaceFactor * BackfaceFactor + 1.0e-4f;

        // Chebyshev upper-tail inequality on 16x16 octahedral radial distance moments: P(R >= d) <= sigma^2 / (sigma^2 + (d - mu)^2)
        const Mathematics::Axis2D Moments = SampleOctahedralBilinear<Mathematics::Axis2D, ChebyshevDepthOctaW>(
            Cell.DepthMoments, DirFromCell);
        if (DistToCell > Moments.X + 1.0e-3f)
        {
            const float Variance = std::max(1.0e-4f, std::fabs(Moments.Y - Moments.X * Moments.X));
            const float Diff     = DistToCell - Moments.X;
            const float Cheby    = Variance / (Variance + Diff * Diff);
            // Crush crushed tail^3 so sealed shadow chambers behind thin walls receive ZERO light leak!
            VisibilityW *= std::max(0.0f, Cheby * Cheby * Cheby);
        }

        const float CombinedW = std::max(1.0e-7f, TriW * VisibilityW);
        const Mathematics::LinearRGB CellIrr = SampleOctahedralBilinear<Mathematics::LinearRGB, IrradianceOctaWidth>(
            Cell.IrradianceOverPi, SurfaceNormal);
        WeightedIrradiance = WeightedIrradiance + CellIrr * CombinedW;
        TotalWeight       += CombinedW;
    }

    return (TotalWeight > 0.0f) ? (WeightedIrradiance * (1.0f / TotalWeight)) : Mathematics::LinearRGB{};
}

Mathematics::LinearRGB IrradianceLatticeSequence::EvaluateGtxSignedDistanceConeGi(
    Mathematics::Axis3D                             WorldPosition,
    Mathematics::Axis3D                             SurfaceNormal,
    const DistanceFieldSpace::SignedDistanceVolume& SdfVolume,
    const SpatialAcceleration::WideBoundingTree&    SceneTree,
    std::span<const MaterialSpace::BounceSlabEntry> BounceSummaries,
    const SolarDirectionalEmitter&                  Sun,
    Mathematics::LinearRGB                          SkyRadiance) const noexcept
{
    Mathematics::Axis3D TanU {};
    Mathematics::Axis3D BitV {};
    GeometrySpace::ConstructDuffOrthonormalBasis(SurfaceNormal, TanU, BitV);

    // 6-Cone Cosine Hemisphere Partition (1 zenith cone + 5 ring cones at 60 deg elevation, 60 deg half-aperture -> tan(30 deg) = 0.577)
    struct LocalConeSpec { Mathematics::Axis3D LocalDir; float Weight; };
    constexpr std::array<LocalConeSpec, 6u> Cones {{
        { {  0.000000f,  0.000000f, 1.000f }, 0.25f },
        { {  0.866025f,  0.000000f, 0.500f }, 0.15f },
        { {  0.267617f,  0.823639f, 0.500f }, 0.15f },
        { { -0.700629f,  0.509037f, 0.500f }, 0.15f },
        { { -0.700629f, -0.509037f, 0.500f }, 0.15f },
        { {  0.267617f, -0.823639f, 0.500f }, 0.15f },
    }};

    const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis(Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
    const Mathematics::Axis3D Origin = WorldPosition + SurfaceNormal * (SdfVolume.VoxelSpacingMeters * 0.9f);

    Mathematics::LinearRGB AccumulatedOverPi { 0.0f, 0.0f, 0.0f };
    for (const auto& Cone : Cones)
    {
        const Mathematics::Axis3D WorldConeDir =
            TanU * Cone.LocalDir.X + BitV * Cone.LocalDir.Y + SurfaceNormal * Cone.LocalDir.Z;
        const float SkyVis = SdfVolume.TraceConeVisibility(Origin, WorldConeDir, 8.0f, 0.45f, 24u);

        // Estimate nearby bounce color bleed when cone encounters geometry
        Mathematics::LinearRGB BounceBleed { 0.0f, 0.0f, 0.0f };
        if (SkyVis < 0.92f && !BounceSummaries.empty())
        {
            SpatialAcceleration::RaySegmentQuery ShortQuery {};
            ShortQuery.Origin          = WorldPosition + SurfaceNormal * 3.0e-3f;
            ShortQuery.UnitDirection   = WorldConeDir;
            ShortQuery.MinimumDistance = 1.0e-3f;
            ShortQuery.MaximumDistance = 4.5f;
            const auto NearHit = SceneTree.TraceClosestRay(ShortQuery, false);
            if (NearHit.HitConfirmed)
            {
                const uint16_t Slot = (NearHit.MaterialOrdinal < BounceSummaries.size()) ? NearHit.MaterialOrdinal : 0u;
                const auto& Slab    = BounceSummaries[Slot];
                const Mathematics::Axis3D HitN = (Mathematics::EvaluateDot(NearHit.GeometricNormal, WorldConeDir) < 0.0f)
                    ? NearHit.GeometricNormal
                    : (NearHit.GeometricNormal * -1.0f);
                const float NdotSun = std::max(0.0f, Mathematics::EvaluateDot(HitN, SunDir));
                if (NdotSun > 0.0f)
                {
                    SpatialAcceleration::RaySegmentQuery SunCheck {};
                    SunCheck.Origin          = ShortQuery.Origin + WorldConeDir * NearHit.HitDistanceMeters + HitN * 3.0e-3f;
                    SunCheck.UnitDirection   = SunDir;
                    SunCheck.MinimumDistance = 1.0e-3f;
                    SunCheck.MaximumDistance = 80.0f;
                    if (!SceneTree.QueryRayOccluded(SunCheck))
                    {
                        BounceBleed = MaterialSpace::EvaluateBounceSlabRadiance(
                            Slab, HitN, WorldConeDir * -1.0f, SunDir, Sun.SolarIrradiance);
                    }
                    else
                    {
                        BounceBleed = UnpackSlabEmissive(Slab);
                    }
                }
                else
                {
                    BounceBleed = UnpackSlabEmissive(Slab);
                }
            }
        }

        const Mathematics::LinearRGB ConeRadiance = SkyRadiance * SkyVis + BounceBleed * (1.0f - SkyVis);
        AccumulatedOverPi = AccumulatedOverPi + ConeRadiance * Cone.Weight;
    }

    return AccumulatedOverPi;
}

} // namespace Frontier::GeometricRaster
