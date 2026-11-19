/*============================================================================================================================================
                                                        IRRADIANCELATTICESEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Tier 2 RTX 1/pi-Normalized Octahedral Irradiance Lattice (16x16 Chebyshev Depth) + Tier 1 GTX 6-Cone SignedDistanceVolume Diffuse GI

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/IrradianceLatticeSequence.h"
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

    DynamicProbeCacheConfiguration ProbeCfg {};
    ProbeCfg.GridMinimumWorld  = ActiveConfig.GridMinimumWorld;
    ProbeCfg.GridMaximumWorld  = ActiveConfig.GridMaximumWorld;
    ProbeCfg.CountX            = ActiveConfig.CountX;
    ProbeCfg.CountY            = ActiveConfig.CountY;
    ProbeCfg.CountZ            = ActiveConfig.CountZ;
    ProbeCfg.FibonacciRayCount = std::clamp(ActiveConfig.FibonacciRayCount, 32u, 256u);
    ProbeCfg.BouncePasses      = std::clamp(ActiveConfig.BouncePasses, 1u, 4u);

    DynamicProbeCacheSequence ProbeSolver {};
    ProbeSolver.UpdateProbeCache(
        ProbeCfg,
        SceneTree,
        BounceSummaries,
        Sun,
        std::span<const PolygonalAreaQuadLight>{},
        SkyRadiance);

    CellSpacing = ProbeSolver.InspectCellSpacing();
    const auto ProbeCells = ProbeSolver.InspectCells();
    Cells.assign(ProbeCells.size(), OctahedralIrradianceCell{});
    for (std::size_t Idx = 0u; Idx < ProbeCells.size(); ++Idx)
    {
        Cells[Idx].WorldPosition    = ProbeCells[Idx].WorldPosition;
        Cells[Idx].IrradianceOverPi = ProbeCells[Idx].IrradianceOverPi;
        Cells[Idx].DepthMoments     = ProbeCells[Idx].DepthMoments;
        Cells[Idx].BackfaceRatio    = ProbeCells[Idx].BackfaceRatio;
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

    const float BiasMag = std::min(0.09f, 0.14f * std::min({ CellSpacing.X, CellSpacing.Y, CellSpacing.Z }));
    const Mathematics::Axis3D BiasedPos = WorldPosition + (SurfaceNormal * 0.72f + ViewDirection * 0.28f) * BiasMag;

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

        const Mathematics::Axis3D DirToCell = DirFromCell * -1.0f;
        const float BackfaceFactor = std::max(0.02f, (Mathematics::EvaluateDot(DirToCell, SurfaceNormal) + 1.0f) * 0.5f);
        const float ValidGate      = (Cell.BackfaceRatio > 0.25f) ? 0.02f : 1.0f;
        float VisibilityW          = BackfaceFactor * BackfaceFactor * ValidGate;

        const Mathematics::Axis2D Moments = SampleOctahedralBilinear<Mathematics::Axis2D, ChebyshevDepthOctaW>(
            Cell.DepthMoments, DirFromCell);
        if (DistToCell > Moments.X + 1.0e-3f)
        {
            const float Variance = std::max(1.0e-4f, std::fabs(Moments.Y - Moments.X * Moments.X));
            const float Diff     = DistToCell - Moments.X;
            const float Cheby    = Variance / (Variance + Diff * Diff);
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
    const DistanceFieldSpace::SignedDistanceVolume& /*SdfVolume*/,
    const SpatialAcceleration::WideBoundingTree&    /*SceneTree*/,
    std::span<const MaterialSpace::BounceSlabEntry> /*BounceSummaries*/,
    const SolarDirectionalEmitter&                  /*Sun*/,
    Mathematics::LinearRGB                          /*SkyRadiance*/) const noexcept
{
    return SampleChebyshevIrradianceOverPi(WorldPosition, SurfaceNormal, SurfaceNormal);
}

void SignedDistanceRadiosityVolume::BuildRadiosityLevels(
    const DistanceFieldSpace::SignedDistanceVolume& SdfVolume,
    const SpatialAcceleration::WideBoundingTree&    SceneTree,
    std::span<const MaterialSpace::BounceSlabEntry> BounceSummaries,
    const SolarDirectionalEmitter&                  Sun,
    Mathematics::LinearRGB                          SkyRadiance)
{
    BoundsMin = SdfVolume.VolumeBoundsMin;
    BoundsMax = SdfVolume.VolumeBoundsMax;
    const Mathematics::Axis3D Span = BoundsMax - BoundsMin;
    if (Span.X <= 1.0e-4f || Span.Y <= 1.0e-4f || Span.Z <= 1.0e-4f || BounceSummaries.empty())
    {
        Level0DimX = Level0DimY = Level0DimZ = 0u;
        RadiosityLevel0.clear();
        RadiosityLevel1.clear();
        RadiosityLevel2.clear();
        return;
    }

    VoxelSpacingM = std::max({ SdfVolume.VoxelSpacingMeters, Span.X / 42.0f, Span.Y / 36.0f, Span.Z / 24.0f, 0.10f });
    Level0DimX    = std::max(2u, static_cast<uint32_t>(std::ceil(Span.X / VoxelSpacingM)) + 1u);
    Level0DimY    = std::max(2u, static_cast<uint32_t>(std::ceil(Span.Y / VoxelSpacingM)) + 1u);
    Level0DimZ    = std::max(2u, static_cast<uint32_t>(std::ceil(Span.Z / VoxelSpacingM)) + 1u);

    const std::size_t Total0 = static_cast<std::size_t>(Level0DimX) * Level0DimY * Level0DimZ;
    RadiosityLevel0.assign(Total0, { 0.0f, 0.0f, 0.0f });

    const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis(Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
    const float NarrowThreshold      = VoxelSpacingM * 1.95f;

    for (uint32_t Z = 0u; Z < Level0DimZ; ++Z)
    {
        for (uint32_t Y = 0u; Y < Level0DimY; ++Y)
        {
            for (uint32_t X = 0u; X < Level0DimX; ++X)
            {
                const Mathematics::Axis3D VoxelCenter {
                    BoundsMin.X + (static_cast<float>(X) + 0.5f) * VoxelSpacingM,
                    BoundsMin.Y + (static_cast<float>(Y) + 0.5f) * VoxelSpacingM,
                    BoundsMin.Z + (static_cast<float>(Z) + 0.5f) * VoxelSpacingM,
                };
                const float SdfVal = SdfVolume.SampleSignedDistance(VoxelCenter);
                if (std::fabs(SdfVal) > NarrowThreshold)
                {
                    continue;
                }

                const Mathematics::Axis3D GradN = SdfVolume.EvaluateSurfaceGradient(VoxelCenter);
                SpatialAcceleration::RaySegmentQuery SurfQuery {};
                SurfQuery.Origin          = VoxelCenter + GradN * (VoxelSpacingM * 0.5f);
                SurfQuery.UnitDirection   = GradN * -1.0f;
                SurfQuery.MinimumDistance = 1.0e-3f;
                SurfQuery.MaximumDistance = NarrowThreshold * 1.6f;

                const auto SurfHit = SceneTree.TraceClosestRay(SurfQuery, false);
                if (!SurfHit.HitConfirmed)
                {
                    continue;
                }

                const uint16_t Slot = (SurfHit.MaterialOrdinal < BounceSummaries.size()) ? SurfHit.MaterialOrdinal : 0u;
                const auto& Slab    = BounceSummaries[Slot];
                const Mathematics::Axis3D HitPos = SurfQuery.Origin + SurfQuery.UnitDirection * SurfHit.HitDistanceMeters;
                const Mathematics::Axis3D HitNrm = (Mathematics::EvaluateDot(SurfHit.GeometricNormal, SurfQuery.UnitDirection) < 0.0f)
                    ? SurfHit.GeometricNormal
                    : (SurfHit.GeometricNormal * -1.0f);

                Mathematics::LinearRGB VoxelRad = UnpackSlabEmissive(Slab);
                const float NdotL = std::max(0.0f, Mathematics::EvaluateDot(HitNrm, SunDir));
                if (NdotL > 0.0f)
                {
                    SpatialAcceleration::RaySegmentQuery SunRay {};
                    SunRay.Origin          = HitPos + HitNrm * 3.0e-3f;
                    SunRay.UnitDirection   = SunDir;
                    SunRay.MinimumDistance = 1.0e-3f;
                    SunRay.MaximumDistance = 120.0f;
                    if (!SceneTree.QueryRayOccluded(SunRay))
                    {
                        const auto DirectSun = MaterialSpace::EvaluateBounceSlabRadiance(
                            Slab, HitNrm, GradN, SunDir, Sun.SolarIrradiance);
                        VoxelRad = VoxelRad + DirectSun;
                    }
                }

                // Add subtle sky fill on sunlit voxels so shadow bounce stays physically luminous
                const auto DiffAlb = UnpackSlabDiffuseAlbedo(Slab);
                VoxelRad.R += SkyRadiance.R * 0.22f * DiffAlb.R;
                VoxelRad.G += SkyRadiance.G * 0.22f * DiffAlb.G;
                VoxelRad.B += SkyRadiance.B * 0.22f * DiffAlb.B;

                const std::size_t Idx = (static_cast<std::size_t>(Z) * Level0DimY + Y) * Level0DimX + X;
                RadiosityLevel0[Idx]  = VoxelRad;
            }
        }
    }

    const auto Downsample2x = [](
        std::span<const Mathematics::LinearRGB> Src,
        uint32_t SrcX, uint32_t SrcY, uint32_t SrcZ,
        std::vector<Mathematics::LinearRGB>& Dst,
        uint32_t& OutX, uint32_t& OutY, uint32_t& OutZ)
    {
        OutX = std::max(1u, (SrcX + 1u) / 2u);
        OutY = std::max(1u, (SrcY + 1u) / 2u);
        OutZ = std::max(1u, (SrcZ + 1u) / 2u);
        Dst.assign(static_cast<std::size_t>(OutX) * OutY * OutZ, { 0.0f, 0.0f, 0.0f });

        for (uint32_t Z = 0u; Z < OutZ; ++Z)
        {
            for (uint32_t Y = 0u; Y < OutY; ++Y)
            {
                for (uint32_t X = 0u; X < OutX; ++X)
                {
                    Mathematics::LinearRGB Sum {};
                    float ActiveCount = 0.0f;
                    for (uint32_t DZ = 0u; DZ < 2u; ++DZ)
                    {
                        const uint32_t SZ = std::min(SrcZ - 1u, Z * 2u + DZ);
                        for (uint32_t DY = 0u; DY < 2u; ++DY)
                        {
                            const uint32_t SY = std::min(SrcY - 1u, Y * 2u + DY);
                            for (uint32_t DX = 0u; DX < 2u; ++DX)
                            {
                                const uint32_t SX = std::min(SrcX - 1u, X * 2u + DX);
                                const auto V      = Src[(static_cast<std::size_t>(SZ) * SrcY + SY) * SrcX + SX];
                                if ((V.R + V.G + V.B) > 1.0e-5f)
                                {
                                    Sum = Sum + V;
                                    ActiveCount += 1.0f;
                                }
                            }
                        }
                    }
                    if (ActiveCount > 0.0f)
                    {
                        Dst[(static_cast<std::size_t>(Z) * OutY + Y) * OutX + X] = Sum * (1.0f / ActiveCount);
                    }
                }
            }
        }
    };

    Downsample2x(RadiosityLevel0, Level0DimX, Level0DimY, Level0DimZ, RadiosityLevel1, Level1DimX, Level1DimY, Level1DimZ);
    Downsample2x(RadiosityLevel1, Level1DimX, Level1DimY, Level1DimZ, RadiosityLevel2, Level2DimX, Level2DimY, Level2DimZ);
}

Mathematics::LinearRGB SignedDistanceRadiosityVolume::SampleRadiosityTrilinear(
    Mathematics::Axis3D WorldPosition,
    float               ConeRadiusMeters) const noexcept
{
    if (Level0DimX < 2u || RadiosityLevel0.empty())
    {
        return {};
    }

    const auto SampleGrid = [](
        std::span<const Mathematics::LinearRGB> Grid,
        uint32_t GX, uint32_t GY, uint32_t GZ,
        Mathematics::Axis3D RelNorm) noexcept -> Mathematics::LinearRGB
    {
        if (GX == 0u || GY == 0u || GZ == 0u || Grid.empty())
        {
            return {};
        }
        const float FX = std::clamp(RelNorm.X * static_cast<float>(GX) - 0.5f, 0.0f, static_cast<float>(GX - 1u));
        const float FY = std::clamp(RelNorm.Y * static_cast<float>(GY) - 0.5f, 0.0f, static_cast<float>(GY - 1u));
        const float FZ = std::clamp(RelNorm.Z * static_cast<float>(GZ) - 0.5f, 0.0f, static_cast<float>(GZ - 1u));

        const uint32_t X0 = static_cast<uint32_t>(std::floor(FX));
        const uint32_t Y0 = static_cast<uint32_t>(std::floor(FY));
        const uint32_t Z0 = static_cast<uint32_t>(std::floor(FZ));
        const uint32_t X1 = std::min(GX - 1u, X0 + 1u);
        const uint32_t Y1 = std::min(GY - 1u, Y0 + 1u);
        const uint32_t Z1 = std::min(GZ - 1u, Z0 + 1u);
        const float TX = FX - static_cast<float>(X0);
        const float TY = FY - static_cast<float>(Y0);
        const float TZ = FZ - static_cast<float>(Z0);

        Mathematics::LinearRGB Out {};
        for (uint32_t C = 0u; C < 8u; ++C)
        {
            const uint32_t IX = (C & 1u) ? X1 : X0;
            const uint32_t IY = (C & 2u) ? Y1 : Y0;
            const uint32_t IZ = (C & 4u) ? Z1 : Z0;
            const float W = ((C & 1u) ? TX : (1.0f - TX)) *
                            ((C & 2u) ? TY : (1.0f - TY)) *
                            ((C & 4u) ? TZ : (1.0f - TZ));
            Out = Out + Grid[(static_cast<std::size_t>(IZ) * GY + IY) * GX + IX] * W;
        }
        return Out;
    };

    const Mathematics::Axis3D Span = BoundsMax - BoundsMin;
    const Mathematics::Axis3D RelNorm {
        (WorldPosition.X - BoundsMin.X) / std::max(1.0e-4f, Span.X),
        (WorldPosition.Y - BoundsMin.Y) / std::max(1.0e-4f, Span.Y),
        (WorldPosition.Z - BoundsMin.Z) / std::max(1.0e-4f, Span.Z),
    };
    if (RelNorm.X < 0.0f || RelNorm.X > 1.0f ||
        RelNorm.Y < 0.0f || RelNorm.Y > 1.0f ||
        RelNorm.Z < 0.0f || RelNorm.Z > 1.0f)
    {
        return {};
    }

    const float MipLevel = std::clamp(std::log2(std::max(1.0f, ConeRadiusMeters / std::max(0.05f, VoxelSpacingM))), 0.0f, 2.0f);
    if (MipLevel < 1.0f)
    {
        const auto L0 = SampleGrid(RadiosityLevel0, Level0DimX, Level0DimY, Level0DimZ, RelNorm);
        const auto L1 = SampleGrid(RadiosityLevel1, Level1DimX, Level1DimY, Level1DimZ, RelNorm);
        return L0 * (1.0f - MipLevel) + L1 * MipLevel;
    }
    const float F12 = MipLevel - 1.0f;
    const auto L1   = SampleGrid(RadiosityLevel1, Level1DimX, Level1DimY, Level1DimZ, RelNorm);
    const auto L2   = SampleGrid(RadiosityLevel2, Level2DimX, Level2DimY, Level2DimZ, RelNorm);
    return L1 * (1.0f - F12) + L2 * F12;
}

Mathematics::LinearRGB IrradianceLatticeSequence::EvaluateSdfRadiosityConeGi(
    Mathematics::Axis3D                             WorldPosition,
    Mathematics::Axis3D                             SurfaceNormal,
    uint32_t                                        PixelX,
    uint32_t                                        PixelY,
    const DistanceFieldSpace::SignedDistanceVolume& SdfVolume,
    const SignedDistanceRadiosityVolume&            RadiosityVolume,
    Mathematics::LinearRGB                          SkyRadiance) const noexcept
{
    Mathematics::Axis3D TanU {};
    Mathematics::Axis3D BitV {};
    GeometrySpace::ConstructDuffOrthonormalBasis(SurfaceNormal, TanU, BitV);

    // Interleaved Gradient Noise (IGN) phase rotation + 16 Fibonacci Cosine Hemisphere Cones
    const float IgnPhase = std::fmod(52.9829189f * std::fmod(0.06711056f * static_cast<float>(PixelX) + 0.00583715f * static_cast<float>(PixelY), 1.0f), 1.0f);
    const float RotOffset = IgnPhase * Constants::TauFloat;
    constexpr uint32_t ConeCount    = 16u;
    constexpr float    GoldenAngle  = 2.39996323f;
    constexpr float    ConeAperture = 0.42f;
    const float        MaxTraceM    = 7.5f;
    const Mathematics::Axis3D Origin = WorldPosition + SurfaceNormal * (SdfVolume.VoxelSpacingMeters * 0.90f);

    Mathematics::LinearRGB AccumOverPi { 0.0f, 0.0f, 0.0f };
    for (uint32_t CIdx = 0u; CIdx < ConeCount; ++CIdx)
    {
        const float U1   = (static_cast<float>(CIdx) + 0.5f) / static_cast<float>(ConeCount);
        const float Phi  = static_cast<float>(CIdx) * GoldenAngle + RotOffset;
        const float SinT = std::sqrt(U1);
        const float CosT = std::sqrt(std::max(0.0f, 1.0f - U1));
        const Mathematics::Axis3D ConeDir =
            TanU * (SinT * std::cos(Phi)) + BitV * (SinT * std::sin(Phi)) + SurfaceNormal * CosT;

        float DistM     = std::max(0.06f, SdfVolume.VoxelSpacingMeters * 0.65f);
        float SkyVis    = 1.0f;
        Mathematics::LinearRGB BounceRad {};

        for (uint32_t Step = 0u; Step < 22u && DistM < MaxTraceM; ++Step)
        {
            const Mathematics::Axis3D SamplePos = Origin + ConeDir * DistM;
            const float SdfD       = SdfVolume.SampleSignedDistance(SamplePos);
            const float ConeRadius = std::max(0.04f, DistM * ConeAperture);

            if (SdfD < ConeRadius)
            {
                const float Occ = std::clamp(1.0f - SdfD / ConeRadius, 0.0f, 1.0f);
                const auto VoxelRad = RadiosityVolume.SampleRadiosityTrilinear(SamplePos, ConeRadius);
                const float W = Occ * SkyVis;
                BounceRad     = BounceRad + VoxelRad * W;
                SkyVis       *= (1.0f - Occ * 0.68f);
                if (SkyVis < 0.04f)
                {
                    SkyVis = 0.0f;
                    break;
                }
            }
            DistM += std::max(SdfVolume.VoxelSpacingMeters * 0.55f, std::fabs(SdfD) * 0.85f);
        }

        const float SkyUp = std::clamp(ConeDir.Z * 0.5f + 0.5f, 0.0f, 1.0f);
        const auto SkyDir = SkyRadiance * (0.65f + 0.65f * SkyUp);
        const auto ConeC  = SkyDir * SkyVis + BounceRad;
        AccumOverPi       = AccumOverPi + ConeC * (1.0f / static_cast<float>(ConeCount));
    }

    return {
        std::clamp(AccumOverPi.R, 0.0f, 6.5f),
        std::clamp(AccumOverPi.G, 0.0f, 6.5f),
        std::clamp(AccumOverPi.B, 0.0f, 6.5f),
    };
}

} // namespace Frontier::GeometricRaster
