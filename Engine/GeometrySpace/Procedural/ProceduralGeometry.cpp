/*============================================================================================================================================
                                                          PROCEDURALGEOMETRY.CPP
============================================================================================================================================*/
// 🧩 Implementation of Frontier's extensible ProceduralGeometry framework and built-in generators:
//    ParametricSweep, TerrainField, BooleanSolid, SplineSurface, VoronoiPartition, ProceduralTree, ProceduralBuilding, ProceduralTyre.

#include "Engine/GeometrySpace/Procedural/ProceduralGeometry.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace Frontier::GeometrySpace
{

namespace
{

constexpr float PiF    = 3.14159265358979323846f;
constexpr float TwoPiF = 6.28318530717958647692f;

[[nodiscard]] uint64_t HashMix64(uint64_t H, uint64_t V) noexcept
{
    H ^= V;
    H *= 0x100000001B3ull;
    return H;
}

[[nodiscard]] uint64_t HashBytes(uint64_t H, const void* Data, std::size_t Size) noexcept
{
    const auto* Bytes = static_cast<const uint8_t*>(Data);
    for (std::size_t I = 0u; I < Size; ++I)
    {
        H = HashMix64(H, Bytes[I]);
    }
    return H;
}

[[nodiscard]] uint32_t SplitMix32(uint32_t& State) noexcept
{
    State += 0x9E3779B9u;
    uint32_t Z = State;
    Z = (Z ^ (Z >> 16u)) * 0x85EBCA6Bu;
    Z = (Z ^ (Z >> 13u)) * 0xC2B2AE35u;
    return Z ^ (Z >> 16u);
}

[[nodiscard]] float NextUnitFloat(uint32_t& State) noexcept
{
    return static_cast<float>(SplitMix32(State) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

[[nodiscard]] float DeterministicValueNoise2D(float X, float Y, uint32_t Seed) noexcept
{
    const auto Hash2 = [&](int32_t IX, int32_t IY) -> float {
        uint32_t S = Seed ^ static_cast<uint32_t>(IX * 374761393) ^ static_cast<uint32_t>(IY * 668265263);
        return NextUnitFloat(S);
    };
    const int32_t IX = static_cast<int32_t>(std::floor(X));
    const int32_t IY = static_cast<int32_t>(std::floor(Y));
    const float   FX = X - static_cast<float>(IX);
    const float   FY = Y - static_cast<float>(IY);
    const float   UX = FX * FX * (3.0f - 2.0f * FX);
    const float   UY = FY * FY * (3.0f - 2.0f * FY);

    const float A = Hash2(IX,     IY);
    const float B = Hash2(IX + 1, IY);
    const float C = Hash2(IX,     IY + 1);
    const float D = Hash2(IX + 1, IY + 1);
    return (A + (B - A) * UX) + ((C - A) + (A - B - C + D) * UX) * UY;
}

[[nodiscard]] float FbmNoise2D(float X, float Y, uint32_t Seed, uint32_t Octaves = 4u) noexcept
{
    float Sum = 0.0f;
    float Amp = 0.5f;
    float Freq = 1.0f;
    float Norm = 0.0f;
    for (uint32_t O = 0u; O < Octaves; ++O)
    {
        Sum  += Amp * DeterministicValueNoise2D(X * Freq, Y * Freq, Seed + O * 1013u);
        Norm += Amp;
        Freq *= 2.03f;
        Amp  *= 0.5f;
    }
    return (Norm > 0.0f) ? (Sum / Norm) : 0.0f;
}

void FinalizeProceduralOutput(ProceduralGeometryOutput& OutResult, uint64_t Hash)
{
    OutResult.Network.LinkOppositeEdges();
    OutResult.Network.RecomputeNormals();

    Mathematics::Axis3D BMin {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    };
    Mathematics::Axis3D BMax {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()
    };

    const auto& Verts = OutResult.Network.Vertices;
    for (const Vertex& V : Verts)
    {
        BMin.X = std::min(BMin.X, V.Axes.X);
        BMin.Y = std::min(BMin.Y, V.Axes.Y);
        BMin.Z = std::min(BMin.Z, V.Axes.Z);
        BMax.X = std::max(BMax.X, V.Axes.X);
        BMax.Y = std::max(BMax.Y, V.Axes.Y);
        BMax.Z = std::max(BMax.Z, V.Axes.Z);
    }

    if (Verts.empty())
    {
        BMin = { -0.5f, -0.5f, -0.5f };
        BMax = {  0.5f,  0.5f,  0.5f };
    }

    uint32_t TriCount = 0u;
    for (const Face& F : OutResult.Network.Faces)
    {
        if (F.CornerCount >= 3u)
        {
            TriCount += static_cast<uint32_t>(F.CornerCount - 2u);
        }
    }

    OutResult.BoundsMin     = BMin;
    OutResult.BoundsMax     = BMax;
    OutResult.VertexCount   = static_cast<uint32_t>(Verts.size());
    OutResult.TriangleCount = TriCount;
    OutResult.ContentHash   = Hash;
}

// Helper to append a grid mesh (Cols x Rows) with given vertex evaluator
template <typename PosFn>
void AppendSurfaceGrid(
    PolygonNetwork&              Network,
    uint32_t                     Cols,
    uint32_t                     Rows,
    PosFn&&                      EvalPos,
    const FaceShadingAssignment& Shading,
    uint16_t                     PartGroup = 0u)
{
    Cols = std::max(2u, Cols);
    Rows = std::max(2u, Rows);
    std::vector<uint32_t> VIdx(static_cast<std::size_t>(Cols + 1u) * (Rows + 1u), InvalidNetworkIndex);
    for (uint32_t R = 0u; R <= Rows; ++R)
    {
        const float V = static_cast<float>(R) / static_cast<float>(Rows);
        for (uint32_t C = 0u; C <= Cols; ++C)
        {
            const float U = static_cast<float>(C) / static_cast<float>(Cols);
            VIdx[static_cast<std::size_t>(R) * (Cols + 1u) + C] = Network.AppendVertex(EvalPos(U, V));
        }
    }
    for (uint32_t R = 0u; R < Rows; ++R)
    {
        const float V0 = static_cast<float>(R) / static_cast<float>(Rows);
        const float V1 = static_cast<float>(R + 1u) / static_cast<float>(Rows);
        for (uint32_t C = 0u; C < Cols; ++C)
        {
            const float U0 = static_cast<float>(C) / static_cast<float>(Cols);
            const float U1 = static_cast<float>(C + 1u) / static_cast<float>(Cols);
            const uint32_t I00 = VIdx[static_cast<std::size_t>(R) * (Cols + 1u) + C];
            const uint32_t I10 = VIdx[static_cast<std::size_t>(R) * (Cols + 1u) + C + 1u];
            const uint32_t I11 = VIdx[static_cast<std::size_t>(R + 1u) * (Cols + 1u) + C + 1u];
            const uint32_t I01 = VIdx[static_cast<std::size_t>(R + 1u) * (Cols + 1u) + C];
            const std::array<uint32_t, 4u> Quad { I00, I10, I11, I01 };
            const std::array<Mathematics::UVAxes, 4u> UVs {{
                { U0, V0 }, { U1, V0 }, { U1, V1 }, { U0, V1 }
            }};
            (void)Network.AppendFace(Quad, UVs, Shading, 0u, PartGroup);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           BUILT-IN GENERATORS (0..7)
//------------------------------------------------------------------------------------------------------------------------

// 0. Parametric Sweep Generator
class ParametricSweepGenerator final : public IProceduralGeometryGenerator
{
public:
    std::string_view QueryTypeId() const noexcept override      { return "procedural.parametric_sweep"; }
    std::string_view QueryDisplayName() const noexcept override { return "Parametric Sweep"; }
    ProceduralGeometryDomain QueryDomain() const noexcept override { return ProceduralGeometryDomain::ParametricSurface; }

    std::span<const ProceduralParameterDescriptor> QueryParameterSchema() const noexcept override
    {
        static const std::array<ProceduralParameterDescriptor, 4u> Schema = {{
            { "seed",        "Random Seed",    "",  "Generator", ProceduralParameterType::Seed,  1.0f, 9999.0f, 42.0f, 42, false },
            { "subdivision", "Subdivisions",   "",  "Tessellation", ProceduralParameterType::Int, 1.0f, 8.0f, 4.0f, 4, false },
            { "scale_m",     "Sweep Width",    "m", "Dimensions", ProceduralParameterType::Float, 0.25f, 10.0f, 2.40f, 0, false },
            { "amplitude_m", "Sweep Height",   "m", "Dimensions", ProceduralParameterType::Float, 0.15f, 8.0f,  0.85f, 0, false },
        }};
        return Schema;
    }

    ProceduralParameterSet BuildDefaultParameters() const override
    {
        ProceduralParameterSet P {};
        P.SetSeed("seed", 42u);
        P.SetInt("subdivision", 4);
        P.SetFloat("scale_m", 2.40f);
        P.SetFloat("amplitude_m", 0.85f);
        return P;
    }

    bool Generate(const ProceduralParameterSet& Params, ProceduralGeometryOutput& OutResult) const override
    {
        OutResult = {};
        const uint32_t Subdiv = static_cast<uint32_t>(std::clamp(Params.GetInt("subdivision", 4), 1, 8));
        const float    Scale  = std::clamp(Params.GetFloat("scale_m", 2.40f), 0.25f, 10.0f);
        const float    Height = std::clamp(Params.GetFloat("amplitude_m", 0.85f), 0.15f, 8.0f);
        const uint32_t Rings  = 6u + Subdiv * 3u;
        const uint32_t Sides  = 8u + Subdiv * 2u;

        FaceShadingAssignment Shading {};
        Shading.AlbedoLinearRGB = { 0.62f, 0.66f, 0.72f };
        Shading.Roughness       = 0.34f;
        Shading.Metallic        = 0.25f;

        const float MajorR = Scale * 0.28f;
        const float MinorR = std::max(0.05f, Scale * 0.10f);
        for (uint32_t R = 0u; R < Rings; ++R)
        {
            const float T0 = static_cast<float>(R) / static_cast<float>(Rings);
            const float T1 = static_cast<float>(R + 1u) / static_cast<float>(Rings);
            const float A0 = T0 * TwoPiF * 0.75f;
            const float A1 = T1 * TwoPiF * 0.75f;
            const Mathematics::Axis3D C0 { std::cos(A0) * MajorR, std::sin(A0) * MajorR, (T0 - 0.5f) * Height };
            const Mathematics::Axis3D C1 { std::cos(A1) * MajorR, std::sin(A1) * MajorR, (T1 - 0.5f) * Height };
            const float R0 = MinorR * (0.70f + 0.30f * std::sin(T0 * PiF));
            const float R1 = MinorR * (0.70f + 0.30f * std::sin(T1 * PiF));
            AppendCylinderFrustumPrimitive(OutResult.Network, C0, C1, R0, R1, Sides, {}, Shading, 0u, 0u);
        }

        OutResult.SubParts.push_back({ "SweepBody", "ProceduralSweepSurface", 0u, 1001u });
        FinalizeProceduralOutput(OutResult, Params.ComputeHash());
        return OutResult.TriangleCount > 0u;
    }
};

// 1. Terrain Field Generator
class TerrainFieldGenerator final : public IProceduralGeometryGenerator
{
public:
    std::string_view QueryTypeId() const noexcept override      { return "procedural.terrain_field"; }
    std::string_view QueryDisplayName() const noexcept override { return "Terrain Field"; }
    ProceduralGeometryDomain QueryDomain() const noexcept override { return ProceduralGeometryDomain::ParametricSurface; }

    std::span<const ProceduralParameterDescriptor> QueryParameterSchema() const noexcept override
    {
        static const std::array<ProceduralParameterDescriptor, 4u> Schema = {{
            { "seed",        "Terrain Seed",   "",  "Generator",    ProceduralParameterType::Seed,  1.0f, 9999.0f, 42.0f, 42, false },
            { "subdivision", "Grid Density",   "",  "Tessellation", ProceduralParameterType::Int,   1.0f, 8.0f,    4.0f,  4,  false },
            { "scale_m",     "Tile Footprint", "m", "Dimensions",   ProceduralParameterType::Float, 0.5f, 40.0f,   2.40f, 0,  false },
            { "amplitude_m", "Relief Height",  "m", "Dimensions",   ProceduralParameterType::Float, 0.05f, 12.0f,  0.85f, 0,  false },
        }};
        return Schema;
    }

    ProceduralParameterSet BuildDefaultParameters() const override
    {
        ProceduralParameterSet P {};
        P.SetSeed("seed", 42u);
        P.SetInt("subdivision", 4);
        P.SetFloat("scale_m", 2.40f);
        P.SetFloat("amplitude_m", 0.85f);
        return P;
    }

    bool Generate(const ProceduralParameterSet& Params, ProceduralGeometryOutput& OutResult) const override
    {
        OutResult = {};
        const uint32_t Seed   = Params.GetSeed("seed", 42u);
        const uint32_t Subdiv = static_cast<uint32_t>(std::clamp(Params.GetInt("subdivision", 4), 1, 8));
        const float    Scale  = std::clamp(Params.GetFloat("scale_m", 2.40f), 0.5f, 40.0f);
        const float    Amp    = std::clamp(Params.GetFloat("amplitude_m", 0.85f), 0.05f, 12.0f);
        const uint32_t GridN  = 4u + Subdiv * 4u;

        FaceShadingAssignment Shading {};
        Shading.AlbedoLinearRGB = { 0.42f, 0.45f, 0.36f };
        Shading.Roughness       = 0.82f;
        Shading.Metallic        = 0.0f;

        AppendSurfaceGrid(OutResult.Network, GridN, GridN, [&](float U, float V) -> Mathematics::Axis3D {
            const float X = (U - 0.5f) * Scale;
            const float Y = (V - 0.5f) * Scale;
            const float N = FbmNoise2D(U * 3.5f, V * 3.5f, Seed, 4u);
            const float Z = (N - 0.45f) * Amp;
            return { X, Y, Z };
        }, Shading, 0u);

        OutResult.SubParts.push_back({ "TerrainSurface", "TerrainGroundSurface", 0u, 1001u });
        FinalizeProceduralOutput(OutResult, Params.ComputeHash());
        return OutResult.TriangleCount > 0u;
    }
};

// 2. Boolean / Compound Mechanical Solid Generator
class BooleanSolidGenerator final : public IProceduralGeometryGenerator
{
public:
    std::string_view QueryTypeId() const noexcept override      { return "procedural.boolean_solid"; }
    std::string_view QueryDisplayName() const noexcept override { return "Boolean Solid"; }
    ProceduralGeometryDomain QueryDomain() const noexcept override { return ProceduralGeometryDomain::ParametricSurface; }

    std::span<const ProceduralParameterDescriptor> QueryParameterSchema() const noexcept override
    {
        static const std::array<ProceduralParameterDescriptor, 4u> Schema = {{
            { "seed",        "Variant Seed",   "",  "Generator",    ProceduralParameterType::Seed,  1.0f, 9999.0f, 42.0f, 42, false },
            { "subdivision", "Radial Detail",  "",  "Tessellation", ProceduralParameterType::Int,   1.0f, 8.0f,    4.0f,  4,  false },
            { "scale_m",     "Base Footprint", "m", "Dimensions",   ProceduralParameterType::Float, 0.25f, 10.0f,  1.60f, 0,  false },
            { "amplitude_m", "Tower Height",   "m", "Dimensions",   ProceduralParameterType::Float, 0.20f, 8.0f,   0.85f, 0,  false },
        }};
        return Schema;
    }

    ProceduralParameterSet BuildDefaultParameters() const override
    {
        ProceduralParameterSet P {};
        P.SetSeed("seed", 42u);
        P.SetInt("subdivision", 4);
        P.SetFloat("scale_m", 1.60f);
        P.SetFloat("amplitude_m", 0.85f);
        return P;
    }

    bool Generate(const ProceduralParameterSet& Params, ProceduralGeometryOutput& OutResult) const override
    {
        OutResult = {};
        const uint32_t Subdiv = static_cast<uint32_t>(std::clamp(Params.GetInt("subdivision", 4), 1, 8));
        const float    Scale  = std::clamp(Params.GetFloat("scale_m", 1.60f), 0.25f, 10.0f);
        const float    Height = std::clamp(Params.GetFloat("amplitude_m", 0.85f), 0.20f, 8.0f);
        const uint32_t Segs   = 12u + Subdiv * 4u;

        FaceShadingAssignment BaseShade {};
        BaseShade.AlbedoLinearRGB = { 0.56f, 0.58f, 0.62f };
        BaseShade.Roughness       = 0.36f;
        BaseShade.Metallic        = 0.72f;

        FaceShadingAssignment BossShade {};
        BossShade.AlbedoLinearRGB = { 0.74f, 0.68f, 0.56f };
        BossShade.Roughness       = 0.28f;
        BossShade.Metallic        = 0.85f;

        const float Half = Scale * 0.5f;
        AppendBoxPrimitive(OutResult.Network, { 0.0f, 0.0f, -Height * 0.25f }, { Half, Half, Height * 0.25f }, {}, BaseShade, 0u, 0u);
        AppendCylinderFrustumPrimitive(OutResult.Network, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, Height * 0.50f }, Half * 0.64f, Half * 0.52f, Segs, {}, BossShade, 0u, 1u);
        AppendTorusRingPrimitive(OutResult.Network, { 0.0f, 0.0f, Height * 0.22f }, Half * 0.66f, Half * 0.09f, Segs, std::max(8u, Segs / 2u), {}, BossShade, 0u, 1u);

        OutResult.SubParts.push_back({ "BaseBlock", "MachinedSteel", 0u, 1001u });
        OutResult.SubParts.push_back({ "CollarBoss", "AnodizedBrass", 1u, 1001u });
        FinalizeProceduralOutput(OutResult, Params.ComputeHash());
        return OutResult.TriangleCount > 0u;
    }
};

// 3. Spline Surface Generator
class SplineSurfaceGenerator final : public IProceduralGeometryGenerator
{
public:
    std::string_view QueryTypeId() const noexcept override      { return "procedural.spline_surface"; }
    std::string_view QueryDisplayName() const noexcept override { return "Spline Surface"; }
    ProceduralGeometryDomain QueryDomain() const noexcept override { return ProceduralGeometryDomain::ParametricSurface; }

    std::span<const ProceduralParameterDescriptor> QueryParameterSchema() const noexcept override
    {
        static const std::array<ProceduralParameterDescriptor, 4u> Schema = {{
            { "seed",        "Curve Seed",     "",  "Generator",    ProceduralParameterType::Seed,  1.0f, 9999.0f, 42.0f, 42, false },
            { "subdivision", "Patch Density",  "",  "Tessellation", ProceduralParameterType::Int,   1.0f, 8.0f,    4.0f,  4,  false },
            { "scale_m",     "Span Width",     "m", "Dimensions",   ProceduralParameterType::Float, 0.4f, 16.0f,   2.40f, 0,  false },
            { "amplitude_m", "Arch Camber",    "m", "Dimensions",   ProceduralParameterType::Float, 0.1f, 6.0f,    0.85f, 0,  false },
        }};
        return Schema;
    }

    ProceduralParameterSet BuildDefaultParameters() const override
    {
        ProceduralParameterSet P {};
        P.SetSeed("seed", 42u);
        P.SetInt("subdivision", 4);
        P.SetFloat("scale_m", 2.40f);
        P.SetFloat("amplitude_m", 0.85f);
        return P;
    }

    bool Generate(const ProceduralParameterSet& Params, ProceduralGeometryOutput& OutResult) const override
    {
        OutResult = {};
        const uint32_t Seed   = Params.GetSeed("seed", 42u);
        const uint32_t Subdiv = static_cast<uint32_t>(std::clamp(Params.GetInt("subdivision", 4), 1, 8));
        const float    Scale  = std::clamp(Params.GetFloat("scale_m", 2.40f), 0.4f, 16.0f);
        const float    Amp    = std::clamp(Params.GetFloat("amplitude_m", 0.85f), 0.1f, 6.0f);
        const uint32_t GridN  = 4u + Subdiv * 3u;
        const float    Phase  = static_cast<float>(Seed % 360u) * (PiF / 180.0f);

        FaceShadingAssignment Shading {};
        Shading.AlbedoLinearRGB = { 0.78f, 0.76f, 0.72f };
        Shading.Roughness       = 0.28f;

        AppendSurfaceGrid(OutResult.Network, GridN, GridN, [&](float U, float V) -> Mathematics::Axis3D {
            const float X = (U - 0.5f) * Scale;
            const float Y = (V - 0.5f) * Scale;
            const float Z = std::sin(U * PiF) * std::sin(V * PiF) * Amp
                          + 0.18f * Amp * std::cos((U - V) * TwoPiF + Phase);
            return { X, Y, Z };
        }, Shading, 0u);

        OutResult.SubParts.push_back({ "SplineShell", "ArchitecturalShell", 0u, 1001u });
        FinalizeProceduralOutput(OutResult, Params.ComputeHash());
        return OutResult.TriangleCount > 0u;
    }
};

// 4. Voronoi Partition Generator
class VoronoiPartitionGenerator final : public IProceduralGeometryGenerator
{
public:
    std::string_view QueryTypeId() const noexcept override      { return "procedural.voronoi_partition"; }
    std::string_view QueryDisplayName() const noexcept override { return "Voronoi Partition"; }
    ProceduralGeometryDomain QueryDomain() const noexcept override { return ProceduralGeometryDomain::ParametricSurface; }

    std::span<const ProceduralParameterDescriptor> QueryParameterSchema() const noexcept override
    {
        static const std::array<ProceduralParameterDescriptor, 4u> Schema = {{
            { "seed",        "Cell Seed",      "",  "Generator",    ProceduralParameterType::Seed,  1.0f, 9999.0f, 42.0f, 42, false },
            { "subdivision", "Cell Count",     "",  "Tessellation", ProceduralParameterType::Int,   1.0f, 8.0f,    4.0f,  4,  false },
            { "scale_m",     "Cluster Radius", "m", "Dimensions",   ProceduralParameterType::Float, 0.4f, 12.0f,   2.00f, 0,  false },
            { "amplitude_m", "Column Relief",  "m", "Dimensions",   ProceduralParameterType::Float, 0.1f, 6.0f,    0.85f, 0,  false },
        }};
        return Schema;
    }

    ProceduralParameterSet BuildDefaultParameters() const override
    {
        ProceduralParameterSet P {};
        P.SetSeed("seed", 42u);
        P.SetInt("subdivision", 4);
        P.SetFloat("scale_m", 2.00f);
        P.SetFloat("amplitude_m", 0.85f);
        return P;
    }

    bool Generate(const ProceduralParameterSet& Params, ProceduralGeometryOutput& OutResult) const override
    {
        OutResult = {};
        uint32_t       Rng    = Params.GetSeed("seed", 42u);
        const uint32_t Subdiv = static_cast<uint32_t>(std::clamp(Params.GetInt("subdivision", 4), 1, 8));
        const float    Scale  = std::clamp(Params.GetFloat("scale_m", 2.00f), 0.4f, 12.0f);
        const float    Amp    = std::clamp(Params.GetFloat("amplitude_m", 0.85f), 0.1f, 6.0f);
        const uint32_t Count  = 5u + Subdiv * 3u;

        FaceShadingAssignment Shading {};
        Shading.AlbedoLinearRGB = { 0.44f, 0.45f, 0.48f };
        Shading.Roughness       = 0.68f;

        for (uint32_t I = 0u; I < Count; ++I)
        {
            const float Angle = TwoPiF * (static_cast<float>(I) / static_cast<float>(Count)) + (NextUnitFloat(Rng) - 0.5f) * 0.35f;
            const float Dist  = std::sqrt(NextUnitFloat(Rng)) * Scale * 0.44f;
            const float Rad   = Scale * (0.09f + 0.06f * NextUnitFloat(Rng));
            const float ColH  = Amp * (0.35f + 0.65f * NextUnitFloat(Rng));
            const float CX    = std::cos(Angle) * Dist;
            const float CY    = std::sin(Angle) * Dist;
            AppendCylinderFrustumPrimitive(OutResult.Network, { CX, CY, 0.0f }, { CX, CY, ColH }, Rad, Rad * 0.94f, 6u, {}, Shading, 0u, 0u);
        }

        OutResult.SubParts.push_back({ "VoronoiCells", "BasaltStone", 0u, 1001u });
        FinalizeProceduralOutput(OutResult, Params.ComputeHash());
        return OutResult.TriangleCount > 0u;
    }
};

// 5. Botanical Starter Generator: Procedural Tree (Trunk + Branches + Canopy Crown)
class ProceduralTreeGenerator final : public IProceduralGeometryGenerator
{
public:
    std::string_view QueryTypeId() const noexcept override      { return "procedural.tree"; }
    std::string_view QueryDisplayName() const noexcept override { return "Procedural Tree"; }
    ProceduralGeometryDomain QueryDomain() const noexcept override { return ProceduralGeometryDomain::Botanical; }

    std::span<const ProceduralParameterDescriptor> QueryParameterSchema() const noexcept override
    {
        static const std::array<ProceduralParameterDescriptor, 4u> Schema = {{
            { "seed",        "Species Seed",   "",  "Botanical",    ProceduralParameterType::Seed,  1.0f, 9999.0f, 42.0f, 42, false },
            { "subdivision", "Branch Density", "",  "Botanical",    ProceduralParameterType::Int,   1.0f, 8.0f,    4.0f,  4,  false },
            { "scale_m",     "Canopy Spread",  "m", "Dimensions",   ProceduralParameterType::Float, 0.6f, 14.0f,   2.40f, 0,  false },
            { "amplitude_m", "Tree Height",    "m", "Dimensions",   ProceduralParameterType::Float, 1.0f, 24.0f,   4.20f, 0,  false },
        }};
        return Schema;
    }

    ProceduralParameterSet BuildDefaultParameters() const override
    {
        ProceduralParameterSet P {};
        P.SetSeed("seed", 42u);
        P.SetInt("subdivision", 4);
        P.SetFloat("scale_m", 2.40f);
        P.SetFloat("amplitude_m", 4.20f);
        return P;
    }

    bool Generate(const ProceduralParameterSet& Params, ProceduralGeometryOutput& OutResult) const override
    {
        OutResult = {};
        uint32_t       Rng      = Params.GetSeed("seed", 42u);
        const uint32_t Subdiv   = static_cast<uint32_t>(std::clamp(Params.GetInt("subdivision", 4), 1, 8));
        const float    Spread   = std::clamp(Params.GetFloat("scale_m", 2.40f), 0.6f, 14.0f);
        const float    Height   = std::clamp(Params.GetFloat("amplitude_m", 4.20f), 1.0f, 24.0f);
        const uint32_t TrunkSeg = 8u + Subdiv;

        FaceShadingAssignment BarkShade {};
        BarkShade.AlbedoLinearRGB = { 0.24f, 0.17f, 0.12f };
        BarkShade.Roughness       = 0.88f;

        FaceShadingAssignment CanopyShade {};
        CanopyShade.AlbedoLinearRGB = { 0.18f, 0.34f, 0.14f };
        CanopyShade.Roughness       = 0.66f;

        const float TrunkH   = Height * 0.56f;
        const float BaseRad  = std::max(0.08f, Spread * 0.085f);
        const float TopRad   = BaseRad * 0.42f;
        AppendCylinderFrustumPrimitive(OutResult.Network, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, TrunkH }, BaseRad, TopRad, TrunkSeg, {}, BarkShade, 0u, 0u);

        const uint32_t BranchCount = 3u + Subdiv;
        for (uint32_t B = 0u; B < BranchCount; ++B)
        {
            const float Azim  = TwoPiF * (static_cast<float>(B) / static_cast<float>(BranchCount)) + NextUnitFloat(Rng) * 0.5f;
            const float StartZ = TrunkH * (0.58f + 0.36f * NextUnitFloat(Rng));
            const float Len   = Spread * (0.34f + 0.18f * NextUnitFloat(Rng));
            const float EndZ  = std::min(Height * 0.92f, StartZ + Height * (0.16f + 0.14f * NextUnitFloat(Rng)));
            const Mathematics::Axis3D Tip { std::cos(Azim) * Len, std::sin(Azim) * Len, EndZ };
            AppendCylinderFrustumPrimitive(OutResult.Network, { 0.0f, 0.0f, StartZ }, Tip, TopRad * 0.72f, TopRad * 0.22f, std::max(6u, TrunkSeg / 2u), {}, BarkShade, 0u, 0u);

            const float ClusterR = Spread * (0.24f + 0.10f * NextUnitFloat(Rng));
            AppendSpherePrimitive(OutResult.Network, Tip, ClusterR, 10u + Subdiv, 7u + Subdiv / 2u, {}, CanopyShade, 0u, 1u);
        }
        // Central crown apex cluster
        AppendSpherePrimitive(OutResult.Network, { 0.0f, 0.0f, Height * 0.80f }, Spread * 0.34f, 12u + Subdiv, 8u + Subdiv / 2u, {}, CanopyShade, 0u, 1u);

        OutResult.SubParts.push_back({ "TrunkAndLimbs", "TreeBarkOpenPbr", 0u, 1001u });
        OutResult.SubParts.push_back({ "FoliageCrown",  "TreeFoliageOpenPbr", 1u, 1002u });
        FinalizeProceduralOutput(OutResult, Params.ComputeHash());
        return OutResult.TriangleCount > 0u;
    }
};

// 6. Architectural Starter Generator: Procedural Building (Podium + Stacked Floors + Roof Cap)
class ProceduralBuildingGenerator final : public IProceduralGeometryGenerator
{
public:
    std::string_view QueryTypeId() const noexcept override      { return "procedural.building"; }
    std::string_view QueryDisplayName() const noexcept override { return "Procedural Building"; }
    ProceduralGeometryDomain QueryDomain() const noexcept override { return ProceduralGeometryDomain::Architectural; }

    std::span<const ProceduralParameterDescriptor> QueryParameterSchema() const noexcept override
    {
        static const std::array<ProceduralParameterDescriptor, 4u> Schema = {{
            { "seed",        "Lot Seed",       "",  "Architecture", ProceduralParameterType::Seed,  1.0f, 9999.0f, 42.0f, 42, false },
            { "subdivision", "Floor Tiers",    "",  "Architecture", ProceduralParameterType::Int,   1.0f, 8.0f,    4.0f,  4,  false },
            { "scale_m",     "Footprint Span", "m", "Dimensions",   ProceduralParameterType::Float, 1.0f, 40.0f,   6.00f, 0,  false },
            { "amplitude_m", "Building Height","m", "Dimensions",   ProceduralParameterType::Float, 2.0f, 80.0f,   12.0f, 0,  false },
        }};
        return Schema;
    }

    ProceduralParameterSet BuildDefaultParameters() const override
    {
        ProceduralParameterSet P {};
        P.SetSeed("seed", 42u);
        P.SetInt("subdivision", 4);
        P.SetFloat("scale_m", 6.00f);
        P.SetFloat("amplitude_m", 12.0f);
        return P;
    }

    bool Generate(const ProceduralParameterSet& Params, ProceduralGeometryOutput& OutResult) const override
    {
        OutResult = {};
        uint32_t       Rng    = Params.GetSeed("seed", 42u);
        const uint32_t Floors = static_cast<uint32_t>(std::clamp(Params.GetInt("subdivision", 4), 1, 8)) + 1u;
        const float    Span   = std::clamp(Params.GetFloat("scale_m", 6.00f), 1.0f, 40.0f);
        const float    Height = std::clamp(Params.GetFloat("amplitude_m", 12.0f), 2.0f, 80.0f);

        FaceShadingAssignment PodiumShade {};
        PodiumShade.AlbedoLinearRGB = { 0.36f, 0.35f, 0.34f };
        PodiumShade.Roughness       = 0.72f;

        FaceShadingAssignment FacadeShade {};
        FacadeShade.AlbedoLinearRGB = { 0.64f, 0.62f, 0.58f };
        FacadeShade.Roughness       = 0.52f;

        FaceShadingAssignment RoofShade {};
        RoofShade.AlbedoLinearRGB = { 0.24f, 0.26f, 0.29f };
        RoofShade.Roughness       = 0.45f;
        RoofShade.Metallic        = 0.35f;

        const float FloorH = Height / static_cast<float>(Floors);
        const float Aspect = 0.72f + 0.52f * NextUnitFloat(Rng);
        const float HalfX  = Span * 0.5f;
        const float HalfY  = HalfX * Aspect;

        for (uint32_t F = 0u; F < Floors; ++F)
        {
            const float Setback = (F >= Floors - 1u && Floors >= 4u) ? 0.82f : 1.0f;
            const float ZCenter = (static_cast<float>(F) + 0.5f) * FloorH;
            const auto& Shade   = (F == 0u) ? PodiumShade : FacadeShade;
            const uint16_t Part = (F == 0u) ? 0u : 1u;
            AppendBoxPrimitive(OutResult.Network, { 0.0f, 0.0f, ZCenter }, { HalfX * Setback, HalfY * Setback, FloorH * 0.46f }, {}, Shade, 0u, Part);
            // Cornice slab band between floors
            AppendBoxPrimitive(OutResult.Network, { 0.0f, 0.0f, (static_cast<float>(F) + 0.96f) * FloorH }, { HalfX * Setback * 1.04f, HalfY * Setback * 1.04f, FloorH * 0.05f }, {}, RoofShade, 0u, 2u);
        }
        // Rooftop mechanical penthouse
        AppendBoxPrimitive(OutResult.Network, { 0.0f, 0.0f, Height + FloorH * 0.24f }, { HalfX * 0.36f, HalfY * 0.36f, FloorH * 0.24f }, {}, RoofShade, 0u, 2u);

        OutResult.SubParts.push_back({ "GroundPodium", "ConcretePodiumOpenPbr", 0u, 1001u });
        OutResult.SubParts.push_back({ "TowerFacade",  "FacadeCladdingOpenPbr", 1u, 1002u });
        OutResult.SubParts.push_back({ "CorniceRoof",  "RoofMetalOpenPbr",      2u, 1003u });
        FinalizeProceduralOutput(OutResult, Params.ComputeHash());
        return OutResult.TriangleCount > 0u;
    }
};

// 7. Automotive Starter Generator: Procedural Tyre (Carcass + Tread Lugs + Rim Barrel)
class ProceduralTyreGenerator final : public IProceduralGeometryGenerator
{
public:
    std::string_view QueryTypeId() const noexcept override      { return "procedural.tyre"; }
    std::string_view QueryDisplayName() const noexcept override { return "Procedural Tyre"; }
    ProceduralGeometryDomain QueryDomain() const noexcept override { return ProceduralGeometryDomain::Automotive; }

    std::span<const ProceduralParameterDescriptor> QueryParameterSchema() const noexcept override
    {
        static const std::array<ProceduralParameterDescriptor, 4u> Schema = {{
            { "seed",        "Tread Seed",     "",  "Automotive", ProceduralParameterType::Seed,  1.0f, 9999.0f, 42.0f, 42, false },
            { "subdivision", "Tread Segments", "",  "Automotive", ProceduralParameterType::Int,   1.0f, 8.0f,    4.0f,  4,  false },
            { "scale_m",     "Outer Diameter", "m", "Dimensions", ProceduralParameterType::Float, 0.30f, 1.40f,  0.68f, 0,  false },
            { "amplitude_m", "Section Width",  "m", "Dimensions", ProceduralParameterType::Float, 0.12f, 0.50f,  0.26f, 0,  false },
        }};
        return Schema;
    }

    ProceduralParameterSet BuildDefaultParameters() const override
    {
        ProceduralParameterSet P {};
        P.SetSeed("seed", 42u);
        P.SetInt("subdivision", 4);
        P.SetFloat("scale_m", 0.68f);
        P.SetFloat("amplitude_m", 0.26f);
        return P;
    }

    bool Generate(const ProceduralParameterSet& Params, ProceduralGeometryOutput& OutResult) const override
    {
        OutResult = {};
        const uint32_t Subdiv   = static_cast<uint32_t>(std::clamp(Params.GetInt("subdivision", 4), 1, 8));
        const float    OuterDia = std::clamp(Params.GetFloat("scale_m", 0.68f), 0.30f, 1.40f);
        const float    SecWidth = std::clamp(Params.GetFloat("amplitude_m", 0.26f), 0.12f, 0.50f);
        const uint32_t MajSegs  = 18u + Subdiv * 4u;
        const uint32_t MinSegs  = 10u + Subdiv * 2u;

        FaceShadingAssignment RubberShade {};
        RubberShade.AlbedoLinearRGB = { 0.045f, 0.045f, 0.048f };
        RubberShade.Roughness       = 0.74f;
        RubberShade.Metallic        = 0.0f;

        FaceShadingAssignment RimShade {};
        RimShade.AlbedoLinearRGB = { 0.74f, 0.76f, 0.80f };
        RimShade.Roughness       = 0.22f;
        RimShade.Metallic        = 0.92f;

        const float OuterR = OuterDia * 0.5f;
        const float MinorR = std::clamp(SecWidth * 0.42f, 0.04f, OuterR * 0.42f);
        const float MajorR = std::max(MinorR + 0.04f, OuterR - MinorR);
        // Tyre sidewall/tread carcass (torus ring around Z axle)
        AppendTorusRingPrimitive(OutResult.Network, { 0.0f, 0.0f, 0.0f }, MajorR, MinorR, MajSegs, MinSegs, {}, RubberShade, 0u, 0u);
        // Alloy wheel barrel + center hub
        const float RimR = std::max(0.06f, MajorR - MinorR * 0.85f);
        AppendCylinderFrustumPrimitive(OutResult.Network, { 0.0f, 0.0f, -SecWidth * 0.36f }, { 0.0f, 0.0f, SecWidth * 0.36f }, RimR, RimR, MajSegs, {}, RimShade, 0u, 1u);
        // Radial tread lug blocks around outer circumference
        const uint32_t LugCount = 12u + Subdiv * 3u;
        for (uint32_t L = 0u; L < LugCount; ++L)
        {
            const float A = TwoPiF * (static_cast<float>(L) / static_cast<float>(LugCount));
            const float R = OuterR - 0.004f;
            AppendBoxPrimitive(OutResult.Network, { std::cos(A) * R, std::sin(A) * R, 0.0f }, { 0.014f, 0.014f, SecWidth * 0.34f }, {}, RubberShade, 0u, 2u);
        }

        OutResult.SubParts.push_back({ "TyreCarcass", "VulcanizedRubberOpenPbr", 0u, 1001u });
        OutResult.SubParts.push_back({ "AlloyRim",    "ForgedAlloyOpenPbr",      1u, 1002u });
        OutResult.SubParts.push_back({ "TreadLugs",   "TreadCompoundOpenPbr",    2u, 1001u });
        FinalizeProceduralOutput(OutResult, Params.ComputeHash());
        return OutResult.TriangleCount > 0u;
    }

    std::optional<float> EvaluateAnalyticSdf(Mathematics::Axis3D P, const ProceduralParameterSet& Params) const noexcept override
    {
        const float OuterDia = std::clamp(Params.GetFloat("scale_m", 0.68f), 0.30f, 1.40f);
        const float SecWidth = std::clamp(Params.GetFloat("amplitude_m", 0.26f), 0.12f, 0.50f);
        const float OuterR   = OuterDia * 0.5f;
        const float MinorR   = std::clamp(SecWidth * 0.42f, 0.04f, OuterR * 0.42f);
        const float MajorR   = std::max(MinorR + 0.04f, OuterR - MinorR);
        const float Qx       = std::hypot(P.X, P.Y) - MajorR;
        return std::hypot(Qx, P.Z) - MinorR;
    }
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           ProceduralParameterSet
//------------------------------------------------------------------------------------------------------------------------

void ProceduralParameterSet::SetFloat(std::string_view Key, float Value)
{
    ProceduralParameterValue V {};
    V.Type       = ProceduralParameterType::Float;
    V.FloatValue = Value;
    Values[std::string(Key)] = V;
}

void ProceduralParameterSet::SetInt(std::string_view Key, int32_t Value)
{
    ProceduralParameterValue V {};
    V.Type     = ProceduralParameterType::Int;
    V.IntValue = Value;
    Values[std::string(Key)] = V;
}

void ProceduralParameterSet::SetSeed(std::string_view Key, uint32_t Seed)
{
    ProceduralParameterValue V {};
    V.Type     = ProceduralParameterType::Seed;
    V.IntValue = static_cast<int32_t>(Seed);
    Values[std::string(Key)] = V;
}

void ProceduralParameterSet::SetBool(std::string_view Key, bool Value)
{
    ProceduralParameterValue V {};
    V.Type      = ProceduralParameterType::Bool;
    V.BoolValue = Value;
    Values[std::string(Key)] = V;
}

void ProceduralParameterSet::SetEnum(std::string_view Key, uint32_t Index)
{
    ProceduralParameterValue V {};
    V.Type     = ProceduralParameterType::Enum;
    V.IntValue = static_cast<int32_t>(Index);
    Values[std::string(Key)] = V;
}

void ProceduralParameterSet::SetVec3(std::string_view Key, Mathematics::Scalar3D Value)
{
    ProceduralParameterValue V {};
    V.Type      = ProceduralParameterType::Vec3;
    V.Vec3Value = Value;
    Values[std::string(Key)] = V;
}

float ProceduralParameterSet::GetFloat(std::string_view Key, float Fallback) const noexcept
{
    const auto It = Values.find(std::string(Key));
    return (It != Values.end()) ? It->second.FloatValue : Fallback;
}

int32_t ProceduralParameterSet::GetInt(std::string_view Key, int32_t Fallback) const noexcept
{
    const auto It = Values.find(std::string(Key));
    return (It != Values.end()) ? It->second.IntValue : Fallback;
}

uint32_t ProceduralParameterSet::GetSeed(std::string_view Key, uint32_t Fallback) const noexcept
{
    const auto It = Values.find(std::string(Key));
    return (It != Values.end()) ? static_cast<uint32_t>(It->second.IntValue) : Fallback;
}

bool ProceduralParameterSet::GetBool(std::string_view Key, bool Fallback) const noexcept
{
    const auto It = Values.find(std::string(Key));
    return (It != Values.end()) ? It->second.BoolValue : Fallback;
}

uint32_t ProceduralParameterSet::GetEnum(std::string_view Key, uint32_t Fallback) const noexcept
{
    const auto It = Values.find(std::string(Key));
    return (It != Values.end()) ? static_cast<uint32_t>(std::max(0, It->second.IntValue)) : Fallback;
}

Mathematics::Scalar3D ProceduralParameterSet::GetVec3(std::string_view Key, Mathematics::Scalar3D Fallback) const noexcept
{
    const auto It = Values.find(std::string(Key));
    return (It != Values.end()) ? It->second.Vec3Value : Fallback;
}

bool ProceduralParameterSet::Contains(std::string_view Key) const noexcept
{
    return Values.find(std::string(Key)) != Values.end();
}

uint64_t ProceduralParameterSet::ComputeHash() const noexcept
{
    // Sort keys so hash is completely deterministic regardless of unordered_map bucket layout
    std::vector<std::string_view> Keys;
    Keys.reserve(Values.size());
    for (const auto& [K, _] : Values) Keys.emplace_back(K);
    std::sort(Keys.begin(), Keys.end());

    uint64_t H = 0xCBF29CE484222325ull;
    for (std::string_view K : Keys)
    {
        H = HashBytes(H, K.data(), K.size());
        const ProceduralParameterValue& V = Values.at(std::string(K));
        H = HashMix64(H, static_cast<uint64_t>(V.Type));
        uint32_t FB = 0u;
        std::memcpy(&FB, &V.FloatValue, sizeof(FB));
        H = HashMix64(H, FB);
        H = HashMix64(H, static_cast<uint64_t>(static_cast<uint32_t>(V.IntValue)));
        H = HashMix64(H, V.BoolValue ? 1ull : 0ull);
        uint32_t VX = 0u, VY = 0u, VZ = 0u;
        std::memcpy(&VX, &V.Vec3Value.X, sizeof(VX));
        std::memcpy(&VY, &V.Vec3Value.Y, sizeof(VY));
        std::memcpy(&VZ, &V.Vec3Value.Z, sizeof(VZ));
        H = HashMix64(H, VX);
        H = HashMix64(H, VY);
        H = HashMix64(H, VZ);
    }
    return H;
}

//------------------------------------------------------------------------------------------------------------------------
//                                         ProceduralGeometryRegistry
//------------------------------------------------------------------------------------------------------------------------

ProceduralGeometryRegistry::ProceduralGeometryRegistry()
{
    RegisterBuiltinGenerators();
}

ProceduralGeometryRegistry& ProceduralGeometryRegistry::Global()
{
    static ProceduralGeometryRegistry Instance {};
    return Instance;
}

void ProceduralGeometryRegistry::RegisterBuiltinGenerators()
{
    (void)RegisterGenerator(std::make_unique<ParametricSweepGenerator>());
    (void)RegisterGenerator(std::make_unique<TerrainFieldGenerator>());
    (void)RegisterGenerator(std::make_unique<BooleanSolidGenerator>());
    (void)RegisterGenerator(std::make_unique<SplineSurfaceGenerator>());
    (void)RegisterGenerator(std::make_unique<VoronoiPartitionGenerator>());
    (void)RegisterGenerator(std::make_unique<ProceduralTreeGenerator>());
    (void)RegisterGenerator(std::make_unique<ProceduralBuildingGenerator>());
    (void)RegisterGenerator(std::make_unique<ProceduralTyreGenerator>());
}

bool ProceduralGeometryRegistry::RegisterGenerator(std::unique_ptr<IProceduralGeometryGenerator> Generator)
{
    if (!Generator) return false;
    const std::string Key(Generator->QueryTypeId());
    if (Key.empty() || ByTypeId.find(Key) != ByTypeId.end()) return false;
    const IProceduralGeometryGenerator* Raw = Generator.get();
    OwnedGenerators.push_back(std::move(Generator));
    OrderedGenerators.push_back(Raw);
    ByTypeId.emplace(Key, Raw);
    return true;
}

const IProceduralGeometryGenerator* ProceduralGeometryRegistry::FindGenerator(std::string_view TypeId) const noexcept
{
    const auto It = ByTypeId.find(std::string(TypeId));
    return (It != ByTypeId.end()) ? It->second : nullptr;
}

const IProceduralGeometryGenerator* ProceduralGeometryRegistry::FindByIndex(uint32_t Index) const noexcept
{
    return (Index < OrderedGenerators.size()) ? OrderedGenerators[Index] : nullptr;
}

std::vector<const IProceduralGeometryGenerator*> ProceduralGeometryRegistry::ListByDomain(ProceduralGeometryDomain Domain) const
{
    std::vector<const IProceduralGeometryGenerator*> Out;
    for (const IProceduralGeometryGenerator* G : OrderedGenerators)
    {
        if (G && G->QueryDomain() == Domain) Out.push_back(G);
    }
    return Out;
}

std::string_view ProceduralGeometryRegistry::CanonicalTypeIdForIndex(uint32_t GeneratorIndex) noexcept
{
    static constexpr std::array<std::string_view, 8u> Ids = {{
        "procedural.parametric_sweep",
        "procedural.terrain_field",
        "procedural.boolean_solid",
        "procedural.spline_surface",
        "procedural.voronoi_partition",
        "procedural.tree",
        "procedural.building",
        "procedural.tyre"
    }};
    return Ids[std::min<uint32_t>(GeneratorIndex, static_cast<uint32_t>(Ids.size() - 1u))];
}

const char* ProceduralGeometryRegistry::DomainDisplayName(ProceduralGeometryDomain Domain) noexcept
{
    switch (Domain)
    {
        case ProceduralGeometryDomain::Analytic:          return "Analytic";
        case ProceduralGeometryDomain::ParametricSurface: return "Parametric Surface";
        case ProceduralGeometryDomain::Botanical:         return "Botanical (Trees)";
        case ProceduralGeometryDomain::Architectural:     return "Architectural (Buildings)";
        case ProceduralGeometryDomain::Automotive:        return "Automotive (Tyres)";
        default:                                          return "Custom";
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           ProceduralGeometryAsset
//------------------------------------------------------------------------------------------------------------------------

ProceduralGeometryAsset::ProceduralGeometryAsset(std::string TypeId)
{
    SetGeneratorTypeId(std::move(TypeId));
}

void ProceduralGeometryAsset::SetGeneratorTypeId(std::string TypeId, const ProceduralGeometryRegistry& Registry)
{
    if (GeneratorTypeId == TypeId && !Parameters.Entries().empty()) return;
    GeneratorTypeId = std::move(TypeId);
    if (const IProceduralGeometryGenerator* Gen = Registry.FindGenerator(GeneratorTypeId))
    {
        Parameters = Gen->BuildDefaultParameters();
    }
    CachedBuildHash     = 0ull;
    HasValidOutput      = false;
    ClusterPyramidBuilt = false;
}

void ProceduralGeometryAsset::SyncCommonInspectorParameters(
    uint32_t GeneratorIndex,
    uint32_t RandomSeed,
    uint32_t SubdivisionLevel,
    float    FeatureScaleMeters,
    float    AmplitudeMeters,
    const ProceduralGeometryRegistry& Registry)
{
    const std::string TargetId(ProceduralGeometryRegistry::CanonicalTypeIdForIndex(GeneratorIndex));
    if (GeneratorTypeId != TargetId)
    {
        SetGeneratorTypeId(TargetId, Registry);
    }
    Parameters.SetSeed("seed", RandomSeed);
    Parameters.SetInt("subdivision", static_cast<int32_t>(std::clamp(SubdivisionLevel, 1u, 8u)));
    Parameters.SetFloat("scale_m", FeatureScaleMeters);
    Parameters.SetFloat("amplitude_m", AmplitudeMeters);
}

bool ProceduralGeometryAsset::EnsureUpToDate(bool BuildClusterPyramid, const ProceduralGeometryRegistry& Registry)
{
    const IProceduralGeometryGenerator* Gen = Registry.FindGenerator(GeneratorTypeId);
    if (Gen == nullptr) return false;

    uint64_t NextHash = Parameters.ComputeHash();
    NextHash = HashBytes(NextHash, GeneratorTypeId.data(), GeneratorTypeId.size());
    NextHash = HashMix64(NextHash, BuildClusterPyramid ? 1ull : 0ull);

    if (HasValidOutput && NextHash == CachedBuildHash && (!BuildClusterPyramid || ClusterPyramidBuilt))
    {
        return false; // Cache hit: zero CPU work
    }

    ProceduralGeometryOutput Fresh {};
    if (!Gen->Generate(Parameters, Fresh))
    {
        return false;
    }

    Output              = std::move(Fresh);
    ClusterPyramidBuilt = false;
    if (BuildClusterPyramid)
    {
        ClusterPyramidBuilt = ClusterPyramidAsset.BuildFromPolygonNetwork(Output.Network, 3u);
    }

    CachedBuildHash = NextHash;
    HasValidOutput  = true;
    ++BuildRevision;
    return true;
}

} // namespace Frontier::GeometrySpace
