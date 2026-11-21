/*============================================================================================================================================
                                                   SURFELRADIOSITYLATTICESEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Order 2 GI Candidate 2: Relocated Surfel Radiosity + Dual-Cascade Octahedral Irradiance Lattice with Dual-Tier Chebyshev Moments

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/SurfelRadiosityLatticeSequence.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::GeometricRaster::GlobalIllumination
{

namespace
{

[[nodiscard]] float PeakChannel(const Mathematics::LinearRGB& C) noexcept
{
    return std::max({ C.R, C.G, C.B });
}

[[nodiscard]] uint32_t ReverseBits32Surfel(uint32_t Bits) noexcept
{
    Bits = (Bits << 16u) | (Bits >> 16u);
    Bits = ((Bits & 0x00FF00FFu) << 8u) | ((Bits & 0xFF00FF00u) >> 8u);
    Bits = ((Bits & 0x0F0F0F0Fu) << 4u) | ((Bits & 0xF0F0F0F0u) >> 4u);
    Bits = ((Bits & 0x33333333u) << 2u) | ((Bits & 0xCCCCCCCCu) >> 2u);
    Bits = ((Bits & 0x55555555u) << 1u) | ((Bits & 0xAAAAAAAAu) >> 1u);
    return Bits;
}

[[nodiscard]] uint32_t ScrambleOwenSurfel(uint32_t X, uint32_t Seed) noexcept
{
    X += Seed;
    X ^= X * 0x6C50B47Cu;
    X ^= X * 0xB82F1E52u;
    X ^= X * 0xC7AFE638u;
    X ^= X * 0x8D22F6E6u;
    return X;
}

void SampleOwenSobol02Surfel(uint32_t SampleIdx, uint32_t Seed, float& OutU, float& OutV) noexcept
{
    uint32_t SobolX = ReverseBits32Surfel(SampleIdx);
    uint32_t Gen    = 0x80000000u;
    uint32_t SobolY = 0u;
    for (uint32_t N = SampleIdx; N != 0u; N >>= 1u)
    {
        if ((N & 1u) != 0u)
        {
            SobolY ^= Gen;
        }
        Gen ^= (Gen >> 1u);
    }
    SobolX = ReverseBits32Surfel(ScrambleOwenSurfel(ReverseBits32Surfel(SobolX), Seed ^ 0xA511E9B3u));
    SobolY = ReverseBits32Surfel(ScrambleOwenSurfel(ReverseBits32Surfel(SobolY), Seed ^ 0x63D83595u));
    OutU   = static_cast<float>(SobolX >> 8u) * (1.0f / 16777216.0f);
    OutV   = static_cast<float>(SobolY >> 8u) * (1.0f / 16777216.0f);
}

[[nodiscard]] float EvaluateBakeSegmentVisibility(
    Mathematics::Axis3D                              OriginPos,
    Mathematics::Axis3D                              OriginNormal,
    Mathematics::Axis3D                              TargetPos,
    float                                            SourceRadius,
    const DirectLighting::HybridOccluderEnvironment& Occluders,
    const SurfelLatticeTierSettings&                 Settings) noexcept
{
    if (Settings.HighEndZeroSdfTier)
    {
        if (Occluders.SceneBvh == nullptr)
        {
            return 1.0f;
        }
        return DirectLighting::EvaluateHighEndBvhLuminaireVisibility(
            *Occluders.SceneBvh, OriginPos, OriginNormal, TargetPos);
    }
    return DirectLighting::EvaluateGtxHybridLuminaireVisibility(
        Occluders, OriginPos, OriginNormal, TargetPos, SourceRadius);
}

// Relocate lattice cell gather position out of walls, columns, vehicle, and character capsules (>= 0.26 m clearance)
[[nodiscard]] Mathematics::Axis3D RelocateLatticeGatherOrigin(
    Mathematics::Axis3D                              NominalPos,
    const DirectLighting::HybridOccluderEnvironment& Occluders,
    const SurfelLatticeTierSettings&                 Settings) noexcept
{
    constexpr float MinClearance = 0.26f;
    Mathematics::Axis3D Pos = NominalPos;

    if (!Settings.HighEndZeroSdfTier)
    {
        for (uint32_t Iter = 0u; Iter < 3u; ++Iter)
        {
            const float Dist = DirectLighting::EvaluateInteriorOccluderDistance(Occluders, Pos);
            if (Dist >= MinClearance)
            {
                break;
            }
            constexpr float Eps = 0.04f;
            const float GX = DirectLighting::EvaluateInteriorOccluderDistance(Occluders, { Pos.X + Eps, Pos.Y, Pos.Z })
                           - DirectLighting::EvaluateInteriorOccluderDistance(Occluders, { Pos.X - Eps, Pos.Y, Pos.Z });
            const float GY = DirectLighting::EvaluateInteriorOccluderDistance(Occluders, { Pos.X, Pos.Y + Eps, Pos.Z })
                           - DirectLighting::EvaluateInteriorOccluderDistance(Occluders, { Pos.X, Pos.Y - Eps, Pos.Z });
            const float GZ = DirectLighting::EvaluateInteriorOccluderDistance(Occluders, { Pos.X, Pos.Y, Pos.Z + Eps })
                           - DirectLighting::EvaluateInteriorOccluderDistance(Occluders, { Pos.X, Pos.Y, Pos.Z - Eps });
            const Mathematics::Axis3D Grad = Mathematics::NormalizeAxis({ GX, GY, GZ }, { 0.0f, 0.0f, 1.0f });
            Pos = Pos + Grad * std::min(0.42f, (MinClearance - Dist) + 0.02f);
        }
        return Pos;
    }

    // HighEnd 0-SDF tier: 6 short cardinal BVH4 clearance rays (zero SDF memory!)
    if (Occluders.SceneBvh != nullptr)
    {
        constexpr std::array<Mathematics::Axis3D, 6u> CardinalDirs {{
            {  1.0f,  0.0f,  0.0f },
            { -1.0f,  0.0f,  0.0f },
            {  0.0f,  1.0f,  0.0f },
            {  0.0f, -1.0f,  0.0f },
            {  0.0f,  0.0f,  1.0f },
            {  0.0f,  0.0f, -1.0f },
        }};
        Mathematics::Axis3D Push { 0.0f, 0.0f, 0.0f };
        for (const auto& Dir : CardinalDirs)
        {
            SpatialAcceleration::RaySegmentQuery Ray {};
            Ray.Origin          = Pos;
            Ray.UnitDirection   = Dir;
            Ray.MinimumDistance = 1.0e-3f;
            Ray.MaximumDistance = MinClearance;
            const auto Hit = Occluders.SceneBvh->TraceClosestRay(Ray, false);
            if (Hit.HitConfirmed && Hit.HitDistanceMeters < MinClearance)
            {
                const float Deficit = MinClearance - Hit.HitDistanceMeters;
                // If hitting the inside/backface of a closed box or wall, push along Hit.GeometricNormal; otherwise push opposite Dir
                if (Mathematics::EvaluateDot(Hit.GeometricNormal, Dir) > 0.0f)
                {
                    Push = Push + Hit.GeometricNormal * (Hit.HitDistanceMeters + MinClearance);
                }
                else
                {
                    Push = Push - Dir * Deficit;
                }
            }
        }
        const float PushLen = Mathematics::EvaluateLength(Push);
        if (PushLen > 0.48f)
        {
            Push = Push * (0.48f / PushLen);
        }
        Pos = Pos + Push;
    }
    return Pos;
}

[[nodiscard]] float TraceGtxSdfRadialDistance(
    const DirectLighting::HybridOccluderEnvironment& Occluders,
    Mathematics::Axis3D                              Origin,
    Mathematics::Axis3D                              UnitDir,
    float                                            MaxDist) noexcept
{
    float Travel = 0.05f;
    for (uint32_t Step = 0u; Step < 22u && Travel < MaxDist; ++Step)
    {
        const Mathematics::Axis3D SampleP = Origin + UnitDir * Travel;
        // Also terminate on floor/ceiling Z planes if present in scene bounds
        float Dist = DirectLighting::EvaluateInteriorOccluderDistance(Occluders, SampleP);
        if (UnitDir.Z < -1.0e-3f && SampleP.Z < 0.0f)
        {
            return std::max(0.05f, Travel);
        }
        if (Dist <= 0.025f)
        {
            return Travel;
        }
        Travel += std::max(0.06f, Dist * 0.78f);
    }
    return MaxDist;
}

void SetupCascadeGrid(
    IrradianceCascadeGrid& OutGrid,
    Mathematics::Axis3D    WorldMin,
    Mathematics::Axis3D    WorldMax,
    uint32_t               DimX,
    uint32_t               DimY,
    uint32_t               DimZ,
    uint32_t               SheetRes)
{
    OutGrid.WorldMin     = WorldMin;
    OutGrid.WorldMax     = WorldMax;
    OutGrid.GridDimX     = std::max(2u, DimX);
    OutGrid.GridDimY     = std::max(2u, DimY);
    OutGrid.GridDimZ     = std::max(2u, DimZ);
    OutGrid.OctaSheetRes = std::max(4u, SheetRes);
    OutGrid.CellSpacing  = {
        (WorldMax.X - WorldMin.X) / static_cast<float>(OutGrid.GridDimX),
        (WorldMax.Y - WorldMin.Y) / static_cast<float>(OutGrid.GridDimY),
        (WorldMax.Z - WorldMin.Z) / static_cast<float>(OutGrid.GridDimZ),
    };
    const std::size_t EntryCount = static_cast<std::size_t>(OutGrid.GridDimX) * OutGrid.GridDimY * OutGrid.GridDimZ;
    const std::size_t TexelCount = EntryCount * OutGrid.OctaSheetRes * OutGrid.OctaSheetRes;
    OutGrid.IrradianceSheets.assign(TexelCount, { 0.0f, 0.0f, 0.0f });
    OutGrid.ChebyshevMoments.clear();
}

[[nodiscard]] Mathematics::LinearRGB SampleOctahedralSheetBilinear(
    const IrradianceCascadeGrid& Cascade,
    std::size_t                  EntryIndex,
    Mathematics::Axis3D          Direction) noexcept
{
    const Mathematics::Axis2D Uv = EncodeEqualAreaOctahedralUv(Direction);
    const float ResF             = static_cast<float>(Cascade.OctaSheetRes);
    const float GridX            = std::clamp(Uv.X * ResF - 0.5f, 0.0f, ResF - 1.0001f);
    const float GridY            = std::clamp(Uv.Y * ResF - 0.5f, 0.0f, ResF - 1.0001f);
    const uint32_t X0            = static_cast<uint32_t>(GridX);
    const uint32_t Y0            = static_cast<uint32_t>(GridY);
    const uint32_t X1            = std::min(X0 + 1u, Cascade.OctaSheetRes - 1u);
    const uint32_t Y1            = std::min(Y0 + 1u, Cascade.OctaSheetRes - 1u);
    const float    FX            = GridX - static_cast<float>(X0);
    const float    FY            = GridY - static_cast<float>(Y0);

    const std::size_t Base = EntryIndex * Cascade.OctaSheetRes * Cascade.OctaSheetRes;
    const auto& C00 = Cascade.IrradianceSheets[Base + Y0 * Cascade.OctaSheetRes + X0];
    const auto& C10 = Cascade.IrradianceSheets[Base + Y0 * Cascade.OctaSheetRes + X1];
    const auto& C01 = Cascade.IrradianceSheets[Base + Y1 * Cascade.OctaSheetRes + X0];
    const auto& C11 = Cascade.IrradianceSheets[Base + Y1 * Cascade.OctaSheetRes + X1];

    const Mathematics::LinearRGB Top    = C00 * (1.0f - FX) + C10 * FX;
    const Mathematics::LinearRGB Bottom = C01 * (1.0f - FX) + C11 * FX;
    return Top * (1.0f - FY) + Bottom * FY;
}

[[nodiscard]] float EvaluateChebyshevVisibilityWeight(
    const IrradianceCascadeGrid& Cascade,
    std::size_t                  EntryIndex,
    Mathematics::Axis3D          EntryPos,
    Mathematics::Axis3D          ShadingPos) noexcept
{
    if (Cascade.ChebyshevMoments.empty())
    {
        return 1.0f;
    }
    const Mathematics::Axis3D ToReceiver = ShadingPos - EntryPos;
    const float Dist = Mathematics::EvaluateLength(ToReceiver);
    if (Dist <= 0.04f)
    {
        return 1.0f;
    }
    const Mathematics::Axis2D Uv = EncodeEqualAreaOctahedralUv(ToReceiver * (1.0f / Dist));
    const float ResF             = static_cast<float>(Cascade.OctaSheetRes);
    const float GridX            = std::clamp(Uv.X * ResF - 0.5f, 0.0f, ResF - 1.0001f);
    const float GridY            = std::clamp(Uv.Y * ResF - 0.5f, 0.0f, ResF - 1.0001f);
    const uint32_t X0            = static_cast<uint32_t>(GridX);
    const uint32_t Y0            = static_cast<uint32_t>(GridY);
    const uint32_t X1            = std::min(X0 + 1u, Cascade.OctaSheetRes - 1u);
    const uint32_t Y1            = std::min(Y0 + 1u, Cascade.OctaSheetRes - 1u);
    const float    FX            = GridX - static_cast<float>(X0);
    const float    FY            = GridY - static_cast<float>(Y0);

    const std::size_t SheetBase = EntryIndex * Cascade.OctaSheetRes * Cascade.OctaSheetRes;
    const auto LoadMom = [&](uint32_t X, uint32_t Y, uint32_t Comp) noexcept -> float
    {
        return Cascade.ChebyshevMoments[(SheetBase + Y * Cascade.OctaSheetRes + X) * 2u + Comp];
    };
    const float Mean = (LoadMom(X0, Y0, 0u) * (1.0f - FX) + LoadMom(X1, Y0, 0u) * FX) * (1.0f - FY)
                     + (LoadMom(X0, Y1, 0u) * (1.0f - FX) + LoadMom(X1, Y1, 0u) * FX) * FY;
    const float MeanSq = (LoadMom(X0, Y0, 1u) * (1.0f - FX) + LoadMom(X1, Y0, 1u) * FX) * (1.0f - FY)
                       + (LoadMom(X0, Y1, 1u) * (1.0f - FX) + LoadMom(X1, Y1, 1u) * FX) * FY;
    if (Dist <= Mean)
    {
        return 1.0f;
    }
    const float Variance = std::max(0.045f, MeanSq - Mean * Mean);
    const float Delta    = Dist - Mean;
    const float Prob     = Variance / (Variance + Delta * Delta);
    return std::max(0.08f, Prob * Prob);
}

[[nodiscard]] Mathematics::LinearRGB SampleCascadeIrradiance(
    const IrradianceCascadeGrid& Cascade,
    Mathematics::Axis3D          ShadingPos,
    Mathematics::Axis3D          ShadingNormal) noexcept
{
    const Mathematics::Axis3D BiasedPos = ShadingPos + ShadingNormal * 0.14f;
    const float CoordX = (BiasedPos.X - Cascade.WorldMin.X) / std::max(1.0e-5f, Cascade.CellSpacing.X) - 0.5f;
    const float CoordY = (BiasedPos.Y - Cascade.WorldMin.Y) / std::max(1.0e-5f, Cascade.CellSpacing.Y) - 0.5f;
    const float CoordZ = (BiasedPos.Z - Cascade.WorldMin.Z) / std::max(1.0e-5f, Cascade.CellSpacing.Z) - 0.5f;

    const int32_t BaseX = std::clamp(static_cast<int32_t>(std::floor(CoordX)), 0, static_cast<int32_t>(Cascade.GridDimX) - 1);
    const int32_t BaseY = std::clamp(static_cast<int32_t>(std::floor(CoordY)), 0, static_cast<int32_t>(Cascade.GridDimY) - 1);
    const int32_t BaseZ = std::clamp(static_cast<int32_t>(std::floor(CoordZ)), 0, static_cast<int32_t>(Cascade.GridDimZ) - 1);
    const float   LX    = std::clamp(CoordX - static_cast<float>(BaseX), 0.0f, 1.0f);
    const float   LY    = std::clamp(CoordY - static_cast<float>(BaseY), 0.0f, 1.0f);
    const float   LZ    = std::clamp(CoordZ - static_cast<float>(BaseZ), 0.0f, 1.0f);
    const float   FracX = LX * LX * (3.0f - 2.0f * LX);
    const float   FracY = LY * LY * (3.0f - 2.0f * LY);
    const float   FracZ = LZ * LZ * (3.0f - 2.0f * LZ);

    Mathematics::LinearRGB WeightedSum { 0.0f, 0.0f, 0.0f };
    float WeightTotal = 0.0f;

    for (uint32_t Corner = 0u; Corner < 8u; ++Corner)
    {
        const uint32_t OX = (Corner & 1u) != 0u ? 1u : 0u;
        const uint32_t OY = (Corner & 2u) != 0u ? 1u : 0u;
        const uint32_t OZ = (Corner & 4u) != 0u ? 1u : 0u;
        const uint32_t IX = std::min(static_cast<uint32_t>(BaseX) + OX, Cascade.GridDimX - 1u);
        const uint32_t IY = std::min(static_cast<uint32_t>(BaseY) + OY, Cascade.GridDimY - 1u);
        const uint32_t IZ = std::min(static_cast<uint32_t>(BaseZ) + OZ, Cascade.GridDimZ - 1u);

        const float TriW = (OX ? FracX : (1.0f - FracX)) *
                           (OY ? FracY : (1.0f - FracY)) *
                           (OZ ? FracZ : (1.0f - FracZ));
        if (TriW <= 1.0e-5f)
        {
            continue;
        }

        const Mathematics::Axis3D EntryPos {
            Cascade.WorldMin.X + (static_cast<float>(IX) + 0.5f) * Cascade.CellSpacing.X,
            Cascade.WorldMin.Y + (static_cast<float>(IY) + 0.5f) * Cascade.CellSpacing.Y,
            Cascade.WorldMin.Z + (static_cast<float>(IZ) + 0.5f) * Cascade.CellSpacing.Z,
        };
        const Mathematics::Axis3D DirToEntry = Mathematics::NormalizeAxis(EntryPos - ShadingPos, ShadingNormal);
        const float WrapCos    = std::max(0.06f, 0.5f + 0.5f * Mathematics::EvaluateDot(DirToEntry, ShadingNormal));
        const float BackfaceW  = WrapCos;
        const std::size_t EIdx = (static_cast<std::size_t>(IZ) * Cascade.GridDimY + IY) * Cascade.GridDimX + IX;
        const float ChebW      = EvaluateChebyshevVisibilityWeight(Cascade, EIdx, EntryPos, BiasedPos);
        const float CombinedW  = std::max(1.0e-5f, TriW * BackfaceW * ChebW);

        const Mathematics::LinearRGB SampleE = SampleOctahedralSheetBilinear(Cascade, EIdx, ShadingNormal);
        WeightedSum = WeightedSum + SampleE * CombinedW;
        WeightTotal += CombinedW;
    }

    if (WeightTotal <= 1.0e-6f)
    {
        return {};
    }
    return WeightedSum * (1.0f / WeightTotal);
}

} // namespace

Mathematics::Axis2D EncodeEqualAreaOctahedralUv(Mathematics::Axis3D UnitDir) noexcept
{
    const float InvL1 = 1.0f / std::max(1.0e-6f, std::fabs(UnitDir.X) + std::fabs(UnitDir.Y) + std::fabs(UnitDir.Z));
    float OX = UnitDir.X * InvL1;
    float OY = UnitDir.Y * InvL1;
    if (UnitDir.Z < 0.0f)
    {
        const float FoldX = (1.0f - std::fabs(OY)) * (OX >= 0.0f ? 1.0f : -1.0f);
        const float FoldY = (1.0f - std::fabs(OX)) * (OY >= 0.0f ? 1.0f : -1.0f);
        OX = FoldX;
        OY = FoldY;
    }
    return { std::clamp(OX * 0.5f + 0.5f, 0.0f, 1.0f), std::clamp(OY * 0.5f + 0.5f, 0.0f, 1.0f) };
}

Mathematics::Axis3D DecodeEqualAreaOctahedralUv(Mathematics::Axis2D UvZeroToOne) noexcept
{
    const float OX = UvZeroToOne.X * 2.0f - 1.0f;
    const float OY = UvZeroToOne.Y * 2.0f - 1.0f;
    Mathematics::Axis3D Dir { OX, OY, 1.0f - std::fabs(OX) - std::fabs(OY) };
    const float Fold = std::max(-Dir.Z, 0.0f);
    Dir.X += (Dir.X >= 0.0f) ? -Fold : Fold;
    Dir.Y += (Dir.Y >= 0.0f) ? -Fold : Fold;
    return Mathematics::NormalizeAxis(Dir, { 0.0f, 0.0f, 1.0f });
}

void SurfelRadiosityLatticeSequence::BuildRelaxedLatticeFromScene(
    std::span<const SpatialAcceleration::RayTrianglePrimitive> Primitives,
    std::span<const Mathematics::LinearRGB>                    MaterialDiffuseAlbedos,
    std::span<const Mathematics::LinearRGB>                    MaterialEmissiveRadiance,
    const DirectLighting::HybridOccluderEnvironment&           Occluders,
    const SurfelLatticeTierSettings&                           Settings)
{
    Surfels.clear();
    BuiltValid = false;
    if (Primitives.empty())
    {
        return;
    }

    // Build exact Stokes luminaire cluster representation for analytical near-field ceiling/emissive lighting
    StokesContourTierSettings StokesTier {};
    StokesTier.HighEndZeroSdfTier = Settings.HighEndZeroSdfTier;
    StokesTier.TargetPatchBudget  = 32u;
    StokesTier.SunDirection       = Settings.SunDirection;
    StokesTier.SunRadiance        = Settings.SunRadiance;
    StokesTier.AreaLights         = Settings.AreaLights;
    NearFieldStokes.BuildFromScene(Primitives, MaterialDiffuseAlbedos, MaterialEmissiveRadiance, Occluders, StokesTier);

    Mathematics::Axis3D SceneMin {  1.0e9f,  1.0e9f,  1.0e9f };
    Mathematics::Axis3D SceneMax { -1.0e9f, -1.0e9f, -1.0e9f };

    struct DiffuseTriInfo
    {
        uint32_t               PrimIdx = 0u;
        float                  Area    = 0.0f;
        Mathematics::Axis3D    Normal  {};
        Mathematics::LinearRGB Albedo  {};
    };
    std::vector<DiffuseTriInfo> DiffuseList;
    DiffuseList.reserve(Primitives.size());
    double TotalDiffuseArea = 0.0;

    for (uint32_t Idx = 0u; Idx < static_cast<uint32_t>(Primitives.size()); ++Idx)
    {
        const auto& Prim = Primitives[Idx];
        const Mathematics::Axis3D B = Prim.Corner0 + Prim.Edge01;
        const Mathematics::Axis3D C = Prim.Corner0 + Prim.Edge02;
        for (const auto& P : { Prim.Corner0, B, C })
        {
            SceneMin.X = std::min(SceneMin.X, P.X);
            SceneMin.Y = std::min(SceneMin.Y, P.Y);
            SceneMin.Z = std::min(SceneMin.Z, P.Z);
            SceneMax.X = std::max(SceneMax.X, P.X);
            SceneMax.Y = std::max(SceneMax.Y, P.Y);
            SceneMax.Z = std::max(SceneMax.Z, P.Z);
        }

        const Mathematics::Axis3D Cross = Mathematics::EvaluateCross(Prim.Edge01, Prim.Edge02);
        const float DoubleArea = Mathematics::EvaluateLength(Cross);
        if (DoubleArea <= 1.0e-6f) continue;
        const float Area = 0.5f * DoubleArea;
        const Mathematics::Axis3D TriN = Cross * (1.0f / DoubleArea);

        const Mathematics::LinearRGB Emissive = (Prim.MaterialOrdinal < MaterialEmissiveRadiance.size())
            ? MaterialEmissiveRadiance[Prim.MaterialOrdinal]
            : Mathematics::LinearRGB {};
        const Mathematics::LinearRGB Albedo = (Prim.MaterialOrdinal < MaterialDiffuseAlbedos.size())
            ? MaterialDiffuseAlbedos[Prim.MaterialOrdinal]
            : Mathematics::LinearRGB { 0.5f, 0.5f, 0.5f };

        if (PeakChannel(Emissive) > 0.05f)
        {
            RadiositySurfaceElement EmitSurfel {};
            EmitSurfel.Position        = (Prim.Corner0 + B + C) * (1.0f / 3.0f);
            EmitSurfel.Normal          = TriN;
            EmitSurfel.DiffuseAlbedo   = Albedo;
            EmitSurfel.EmittedRadiance = Emissive;
            EmitSurfel.ExitantRadiance = Emissive;
            EmitSurfel.Area            = Area;
            EmitSurfel.EmissiveSurface = true;
            Surfels.push_back(EmitSurfel);
        }
        else if (PeakChannel(Albedo) > 0.02f && Area > 0.02f)
        {
            TotalDiffuseArea += static_cast<double>(Area);
            DiffuseList.push_back({ Idx, Area, TriN, Albedo });
        }
    }

    // Spawn 2x2 stratified emitter surfels per PolygonalAreaQuadLight so scenes using AreaLights (e.g. ShowcaseGallery) drive full surfel GI!
    for (const auto& Quad : Settings.AreaLights)
    {
        if (PeakChannel(Quad.EmittedRadiance) <= 0.05f) continue;
        const Mathematics::Axis3D HalfU = Quad.RightTangent * Quad.HalfWidthMeters;
        const Mathematics::Axis3D HalfV = Quad.UpBitangent  * Quad.HalfHeightMeters;
        const Mathematics::Axis3D EmitN = Mathematics::NormalizeAxis(
            Mathematics::EvaluateCross(Quad.RightTangent, Quad.UpBitangent), { 0.0f, 0.0f, -1.0f });
        const float SubArea = (4.0f * Quad.HalfWidthMeters * Quad.HalfHeightMeters) * 0.25f;
        for (int SU = -1; SU <= 1; SU += 2)
        {
            for (int SV = -1; SV <= 1; SV += 2)
            {
                RadiositySurfaceElement QuadSurfel {};
                QuadSurfel.Position        = Quad.CenterWorld + HalfU * (0.5f * static_cast<float>(SU))
                                                              + HalfV * (0.5f * static_cast<float>(SV));
                QuadSurfel.Normal          = EmitN;
                QuadSurfel.DiffuseAlbedo   = { 0.0f, 0.0f, 0.0f };
                QuadSurfel.EmittedRadiance = Quad.EmittedRadiance;
                QuadSurfel.ExitantRadiance = Quad.EmittedRadiance;
                QuadSurfel.Area            = SubArea;
                QuadSurfel.EmissiveSurface = true;
                Surfels.push_back(QuadSurfel);
            }
        }
    }

    const uint32_t SurfelBudget = Settings.HighEndZeroSdfTier
        ? std::max(220u, Settings.TargetSurfelBudget + 60u)
        : std::max(140u, Settings.TargetSurfelBudget);
    if (TotalDiffuseArea > 1.0e-5)
    {
        const double AreaStep = TotalDiffuseArea / static_cast<double>(SurfelBudget);
        double Accum          = AreaStep * 0.5;
        uint32_t SurfelSeed   = 1u;
        for (const auto& Item : DiffuseList)
        {
            const auto& Prim = Primitives[Item.PrimIdx];
            Accum += static_cast<double>(Item.Area);
            uint32_t Spawn = 0u;
            while (Accum >= AreaStep)
            {
                Accum -= AreaStep;
                ++Spawn;
            }
            if (Spawn == 0u) continue;
            const float SubArea = Item.Area / static_cast<float>(Spawn);
            for (uint32_t S = 0u; S < Spawn; ++S)
            {
                float U = 0.0f, V = 0.0f;
                SampleOwenSobol02Surfel(S, Item.PrimIdx * 747796405u + SurfelSeed * 2891336453u, U, V);
                ++SurfelSeed;
                const float RootU = std::sqrt(std::clamp(U, 0.0f, 1.0f));
                const float Bary1 = 1.0f - RootU;
                const float Bary2 = V * RootU;
                RadiositySurfaceElement Surfel {};
                Surfel.Position        = Prim.Corner0 + Prim.Edge01 * Bary1 + Prim.Edge02 * Bary2;
                Surfel.Normal          = Item.Normal;
                Surfel.DiffuseAlbedo   = Item.Albedo;
                Surfel.Area            = SubArea;
                Surfel.EmissiveSurface = false;
                Surfels.push_back(Surfel);
            }
        }
    }

    // Seed direct solar irradiance onto diffuse surfels before multi-bounce relaxation
    DirectLighting::HybridConeShadowSetup SunSetup {};
    std::vector<Mathematics::LinearRGB> DirectSunIrrad(Surfels.size(), { 0.0f, 0.0f, 0.0f });
    if (PeakChannel(Settings.SunRadiance) > 1.0e-3f)
    {
        for (std::size_t I = 0u; I < Surfels.size(); ++I)
        {
            if (Surfels[I].EmissiveSurface) continue;
            const float NdotL = std::max(0.0f, Mathematics::EvaluateDot(Surfels[I].Normal, Settings.SunDirection));
            if (NdotL <= 0.0f) continue;
            const float Vis = Settings.HighEndZeroSdfTier
                ? ((Occluders.SceneBvh != nullptr)
                    ? DirectLighting::EvaluateHighEndBvhDirectionalShadow(
                        *Occluders.SceneBvh, Surfels[I].Position, Surfels[I].Normal, Settings.SunDirection, static_cast<uint32_t>(I + 1u), SunSetup)
                    : 1.0f)
                : DirectLighting::EvaluateGtxHybridDirectionalShadow(
                    Occluders, Surfels[I].Position, Surfels[I].Normal, Settings.SunDirection, SunSetup);
            DirectSunIrrad[I] = Settings.SunRadiance * (NdotL * Vis);
            Surfels[I].ExitantRadiance = {
                (Surfels[I].DiffuseAlbedo.R * DirectSunIrrad[I].R) * (1.0f / Constants::PiFloat),
                (Surfels[I].DiffuseAlbedo.G * DirectSunIrrad[I].G) * (1.0f / Constants::PiFloat),
                (Surfels[I].DiffuseAlbedo.B * DirectSunIrrad[I].B) * (1.0f / Constants::PiFloat),
            };
        }
    }

    // Visibility-gated multi-bounce Jacobi radiosity transfer across surfels (both Bounce 0 AND Bounce 1 respect occluders!)
    const uint32_t Bounces = std::clamp(Settings.RadiosityBounceCount, 1u, 3u);
    std::vector<Mathematics::LinearRGB> NextExitance(Surfels.size());
    for (uint32_t Bounce = 0u; Bounce < Bounces; ++Bounce)
    {
        for (std::size_t I = 0u; I < Surfels.size(); ++I)
        {
            const auto& Dst = Surfels[I];
            if (Dst.EmissiveSurface)
            {
                NextExitance[I] = Dst.EmittedRadiance;
                continue;
            }

            Mathematics::LinearRGB IncomingE = DirectSunIrrad[I];
            for (std::size_t J = 0u; J < Surfels.size(); ++J)
            {
                if (I == J) continue;
                const auto& Src = Surfels[J];
                if (PeakChannel(Src.ExitantRadiance) <= 1.0e-4f) continue;

                const Mathematics::Axis3D Delta = Src.Position - Dst.Position;
                const float DistSq = Mathematics::EvaluateLengthSquared(Delta);
                if (DistSq <= 0.04f) continue;
                const float Dist   = std::sqrt(DistSq);
                const auto  Dir    = Delta * (1.0f / Dist);
                const float CosDst = Mathematics::EvaluateDot(Dst.Normal, Dir);
                const float CosSrc = Mathematics::EvaluateDot(Src.Normal, Dir * -1.0f);
                if (CosDst <= 0.01f || CosSrc <= 0.01f) continue;

                const float SrcRad = std::sqrt(Src.Area * (1.0f / Constants::PiFloat));
                const float Vis    = EvaluateBakeSegmentVisibility(
                    Dst.Position, Dst.Normal, Src.Position, SrcRad, Occluders, Settings);
                if (Vis <= 1.0e-4f) continue;

                const float SolidAngle = std::min(0.65f, (Src.Area * CosSrc) / (DistSq + std::max(Src.Area * 0.72f, 0.14f)));
                IncomingE = IncomingE + Src.ExitantRadiance * (CosDst * SolidAngle * Vis);
            }

            NextExitance[I] = {
                Dst.EmittedRadiance.R + (Dst.DiffuseAlbedo.R * IncomingE.R) * (1.0f / Constants::PiFloat),
                Dst.EmittedRadiance.G + (Dst.DiffuseAlbedo.G * IncomingE.G) * (1.0f / Constants::PiFloat),
                Dst.EmittedRadiance.B + (Dst.DiffuseAlbedo.B * IncomingE.B) * (1.0f / Constants::PiFloat),
            };
        }
        for (std::size_t I = 0u; I < Surfels.size(); ++I)
        {
            Surfels[I].ExitantRadiance = NextExitance[I];
        }
    }

    // Configure Dual-Cascade Lattice:
    // GtxLowEnd: Inner 12x10x4 + Outer 8x6x3 (624 entries + SDF Chebyshev moments)
    // HighEnd  : Inner 19x15x6 + Outer 9x7x4 (1,962 entries + 96 BVH4 Chebyshev rays per entry, 0 MB SDF)
    const Mathematics::Axis3D PadOuter { 0.35f, 0.35f, 0.25f };
    const Mathematics::Axis3D OuterMin = SceneMin - PadOuter;
    const Mathematics::Axis3D OuterMax = SceneMax + PadOuter;
    const Mathematics::Axis3D Center   = (SceneMin + SceneMax) * 0.5f;
    const Mathematics::Axis3D HalfSpan = (SceneMax - SceneMin) * 0.36f;
    const Mathematics::Axis3D InnerMin { Center.X - HalfSpan.X, Center.Y - HalfSpan.Y, SceneMin.Z + 0.05f };
    const Mathematics::Axis3D InnerMax { Center.X + HalfSpan.X, Center.Y + HalfSpan.Y, std::min(SceneMax.Z - 0.05f, SceneMin.Z + 2.85f) };

    if (Settings.HighEndZeroSdfTier)
    {
        SetupCascadeGrid(InnerCascade, InnerMin, InnerMax, 19u, 15u, 6u, 6u);
        SetupCascadeGrid(OuterCascade, OuterMin, OuterMax,  9u,  7u, 4u, 6u);
    }
    else
    {
        SetupCascadeGrid(InnerCascade, InnerMin, InnerMax, 12u, 10u, 4u, 6u);
        SetupCascadeGrid(OuterCascade, OuterMin, OuterMax,  8u,  6u, 3u, 6u);
    }

    const auto PopulateCascade = [&](IrradianceCascadeGrid& Cascade)
    {
        const uint32_t SheetRes = Cascade.OctaSheetRes;
        std::vector<Mathematics::Axis3D> TexelDirs(SheetRes * SheetRes);
        for (uint32_t TY = 0u; TY < SheetRes; ++TY)
        {
            for (uint32_t TX = 0u; TX < SheetRes; ++TX)
            {
                TexelDirs[TY * SheetRes + TX] = DecodeEqualAreaOctahedralUv({
                    (static_cast<float>(TX) + 0.5f) / static_cast<float>(SheetRes),
                    (static_cast<float>(TY) + 0.5f) / static_cast<float>(SheetRes),
                });
            }
        }

        // Dual-tier Chebyshev depth moments: 96 BVH4 rays on HighEnd (0 SDF), 48 SDF sphere-trace rays on GtxLowEnd!
        const bool BuildChebyshev = Settings.HighEndZeroSdfTier
            ? (Occluders.SceneBvh != nullptr)
            : (!Occluders.ForbidDistanceField);
        if (BuildChebyshev)
        {
            const std::size_t Texels = static_cast<std::size_t>(Cascade.GridDimX) * Cascade.GridDimY * Cascade.GridDimZ * SheetRes * SheetRes;
            Cascade.ChebyshevMoments.assign(Texels * 2u, 10.0f);
        }

        const uint32_t RayCount = Settings.HighEndZeroSdfTier ? 96u : 48u;
        std::vector<Mathematics::Axis3D> FibDirs(RayCount);
        for (uint32_t R = 0u; R < RayCount; ++R)
        {
            const float Z     = 1.0f - 2.0f * (static_cast<float>(R) + 0.5f) / static_cast<float>(RayCount);
            const float RadXY = std::sqrt(std::max(0.0f, 1.0f - Z * Z));
            const float Phi   = static_cast<float>(R) * 2.39996323f;
            FibDirs[R]        = { RadXY * std::cos(Phi), RadXY * std::sin(Phi), Z };
        }

        for (uint32_t IZ = 0u; IZ < Cascade.GridDimZ; ++IZ)
        {
            for (uint32_t IY = 0u; IY < Cascade.GridDimY; ++IY)
            {
                for (uint32_t IX = 0u; IX < Cascade.GridDimX; ++IX)
                {
                    const std::size_t EntryIdx = (static_cast<std::size_t>(IZ) * Cascade.GridDimY + IY) * Cascade.GridDimX + IX;
                    const Mathematics::Axis3D NominalPos {
                        Cascade.WorldMin.X + (static_cast<float>(IX) + 0.5f) * Cascade.CellSpacing.X,
                        Cascade.WorldMin.Y + (static_cast<float>(IY) + 0.5f) * Cascade.CellSpacing.Y,
                        Cascade.WorldMin.Z + (static_cast<float>(IZ) + 0.5f) * Cascade.CellSpacing.Z,
                    };
                    // Relocate gather point out of walls/columns/vehicle so boundary cells never bake dark interior values
                    const Mathematics::Axis3D GatherPos = RelocateLatticeGatherOrigin(NominalPos, Occluders, Settings);

                    std::array<Mathematics::LinearRGB, 64u> DirIrrad {};
                    const uint32_t TotalTexels = SheetRes * SheetRes;

                    for (const auto& Src : Surfels)
                    {
                        // Direct luminaires are evaluated analytically per pixel via DirectLighting; lattice stores pure diffuse indirect bounce!
                        if (Src.EmissiveSurface) continue;
                        if (PeakChannel(Src.ExitantRadiance) <= 1.0e-4f) continue;
                        const Mathematics::Axis3D Delta = Src.Position - GatherPos;
                        const float DistSq = Mathematics::EvaluateLengthSquared(Delta);
                        if (DistSq <= 0.03f) continue;
                        const float Dist = std::sqrt(DistSq);
                        const Mathematics::Axis3D Wi = Delta * (1.0f / Dist);
                        const float CosEmit = Mathematics::EvaluateDot(Src.Normal, Wi * -1.0f);
                        if (CosEmit <= 0.01f) continue;

                        const float SrcRad = std::sqrt(Src.Area * (1.0f / Constants::PiFloat));
                        const float Vis    = EvaluateBakeSegmentVisibility(
                            GatherPos, Wi, Src.Position, SrcRad, Occluders, Settings);
                        if (Vis <= 1.0e-4f) continue;

                        const float dOmega = std::min(0.65f, (Src.Area * CosEmit) / (DistSq + std::max(Src.Area * 0.72f, 0.14f))) * Vis;
                        const Mathematics::LinearRGB Contrib = Src.ExitantRadiance * dOmega;

                        for (uint32_t TIdx = 0u; TIdx < TotalTexels; ++TIdx)
                        {
                            const float CosN = std::max(0.0f, Mathematics::EvaluateDot(TexelDirs[TIdx], Wi));
                            if (CosN > 0.0f)
                            {
                                DirIrrad[TIdx] = DirIrrad[TIdx] + Contrib * CosN;
                            }
                        }
                    }

                    const std::size_t SheetStart = EntryIdx * TotalTexels;
                    for (uint32_t TIdx = 0u; TIdx < TotalTexels; ++TIdx)
                    {
                        Cascade.IrradianceSheets[SheetStart + TIdx] = DirIrrad[TIdx];
                    }

                    if (BuildChebyshev)
                    {
                        std::array<float, 48u> RayDistsHigh {};
                        std::vector<float>     RayDists(RayCount, 12.0f);
                        (void)RayDistsHigh;
                        for (uint32_t R = 0u; R < RayCount; ++R)
                        {
                            if (Settings.HighEndZeroSdfTier)
                            {
                                SpatialAcceleration::RaySegmentQuery Ray {};
                                Ray.Origin          = GatherPos;
                                Ray.UnitDirection   = FibDirs[R];
                                Ray.MinimumDistance = 0.02f;
                                Ray.MaximumDistance = 14.0f;
                                const auto Hit      = Occluders.SceneBvh->TraceClosestRay(Ray, false);
                                RayDists[R]         = Hit.HitConfirmed ? Hit.HitDistanceMeters : 14.0f;
                            }
                            else
                            {
                                RayDists[R] = TraceGtxSdfRadialDistance(Occluders, GatherPos, FibDirs[R], 12.0f);
                            }
                        }
                        for (uint32_t TIdx = 0u; TIdx < TotalTexels; ++TIdx)
                        {
                            float SumW = 0.0f, SumD = 0.0f, SumD2 = 0.0f;
                            for (uint32_t R = 0u; R < RayCount; ++R)
                            {
                                const float W = std::pow(std::max(0.0f, Mathematics::EvaluateDot(TexelDirs[TIdx], FibDirs[R])), 8.0f);
                                if (W <= 1.0e-4f) continue;
                                SumW  += W;
                                SumD  += W * RayDists[R];
                                SumD2 += W * RayDists[R] * RayDists[R];
                            }
                            const float MeanD   = (SumW > 1.0e-5f) ? (SumD / SumW) : 10.0f;
                            const float MeanSqD = (SumW > 1.0e-5f) ? (SumD2 / SumW) : 100.0f;
                            Cascade.ChebyshevMoments[(SheetStart + TIdx) * 2u + 0u] = MeanD;
                            Cascade.ChebyshevMoments[(SheetStart + TIdx) * 2u + 1u] = MeanSqD;
                        }
                    }
                }
            }
        }
    };

    PopulateCascade(InnerCascade);
    PopulateCascade(OuterCascade);
    BuiltValid = true;
}

Mathematics::LinearRGB SurfelRadiosityLatticeSequence::EvaluateIndirectIrradiance(
    Mathematics::Axis3D ShadingPos,
    Mathematics::Axis3D ShadingNormal) const noexcept
{
    if (!BuiltValid)
    {
        return {};
    }

    const Mathematics::Axis3D Margin {
        (InnerCascade.WorldMax.X - InnerCascade.WorldMin.X) * 0.14f,
        (InnerCascade.WorldMax.Y - InnerCascade.WorldMin.Y) * 0.14f,
        (InnerCascade.WorldMax.Z - InnerCascade.WorldMin.Z) * 0.14f,
    };
    const float AlphaX = std::min(
        (ShadingPos.X - InnerCascade.WorldMin.X) / std::max(0.05f, Margin.X),
        (InnerCascade.WorldMax.X - ShadingPos.X) / std::max(0.05f, Margin.X));
    const float AlphaY = std::min(
        (ShadingPos.Y - InnerCascade.WorldMin.Y) / std::max(0.05f, Margin.Y),
        (InnerCascade.WorldMax.Y - ShadingPos.Y) / std::max(0.05f, Margin.Y));
    const float AlphaZ = std::min(
        (ShadingPos.Z - InnerCascade.WorldMin.Z) / std::max(0.05f, Margin.Z),
        (InnerCascade.WorldMax.Z - ShadingPos.Z) / std::max(0.05f, Margin.Z));
    const float RawBlend   = std::clamp(std::min({ AlphaX, AlphaY, AlphaZ }), 0.0f, 1.0f);
    const float InnerBlend = RawBlend * RawBlend * (3.0f - 2.0f * RawBlend);

    if (InnerBlend >= 0.999f)
    {
        return SampleCascadeIrradiance(InnerCascade, ShadingPos, ShadingNormal);
    }
    if (InnerBlend <= 0.001f)
    {
        return SampleCascadeIrradiance(OuterCascade, ShadingPos, ShadingNormal);
    }

    const Mathematics::LinearRGB InnerE = SampleCascadeIrradiance(InnerCascade, ShadingPos, ShadingNormal);
    const Mathematics::LinearRGB OuterE = SampleCascadeIrradiance(OuterCascade, ShadingPos, ShadingNormal);
    return InnerE * InnerBlend + OuterE * (1.0f - InnerBlend);
}

Mathematics::LinearRGB SurfelRadiosityLatticeSequence::EvaluateTotalIrradiance(
    Mathematics::Axis3D                              ShadingPos,
    Mathematics::Axis3D                              ShadingNormal,
    const DirectLighting::HybridOccluderEnvironment& Occluders,
    const SurfelLatticeTierSettings&                 Settings) const noexcept
{
    StokesContourTierSettings StokesTier {};
    StokesTier.HighEndZeroSdfTier = Settings.HighEndZeroSdfTier;
    StokesTier.SunDirection       = Settings.SunDirection;
    StokesTier.SunRadiance        = Settings.SunRadiance;
    StokesTier.AreaLights         = Settings.AreaLights;
    return NearFieldStokes.EvaluateDirectEmissiveIrradiance(ShadingPos, ShadingNormal, Occluders, StokesTier)
         + EvaluateIndirectIrradiance(ShadingPos, ShadingNormal);
}

} // namespace Frontier::GeometricRaster::GlobalIllumination
