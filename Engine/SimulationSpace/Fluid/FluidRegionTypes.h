/*============================================================================================================================================
                                                          FLUIDREGIONTYPES.H
============================================================================================================================================*/
// 🧩 Stage D (Slice S14) 1:1 3D Eulerian + WorldGrid (1^3..5^3) + Sparse Infinite fluid region types, constants, grid resolution & GPU ABI
// Reference: Experimental/FluidEditor/Engine/ForceGeneratorCatalog.js L16-L24, L238-L244, L348-L459 & GpuContext.js L255-L261

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace Frontier::SimulationSpace::Fluid
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t kMaximumGridTiles      = 5u;        // [-] - WorldGrid max tiles per axis (1^3..5^3)
inline constexpr uint32_t kMaximumTileBankSlots  = 160u;      // [-] - Sparse Infinite hard bank slot cap
inline constexpr uint32_t kMaximumInfiniteCells  = 1600000u;  // [-] - Sparse Infinite total cell memory budget
inline constexpr uint32_t kMinimumFixedCells     = 8u;        // [-] - Minimum cells per axis (Fixed mode)
inline constexpr uint32_t kMaximumFixedCells     = 160u;      // [-] - Maximum cells per axis (Fixed/WorldGrid)
inline constexpr uint32_t kMinimumSparseTileEdge = 8u;        // [-] - Minimum T cells per sparse tile edge
inline constexpr uint32_t kMaximumSparseTileEdge = 48u;       // [-] - Maximum T cells per sparse tile edge
inline constexpr int32_t  kLookupExtentX         = 64;        // [-] - Sparse tile lookup X span (64x16x64)
inline constexpr int32_t  kLookupExtentY         = 16;        // [-] - Sparse tile lookup Y span (64x16x64)
inline constexpr int32_t  kLookupExtentZ         = 64;        // [-] - Sparse tile lookup Z span (64x16x64)
inline constexpr size_t   kLookupTexelCount      = 64u * 16u * 64u; // 65536 texels (64 x 1024)
inline constexpr int32_t  kSparseOutsideCell     = -1000000;  // [-] - Sentinel cell for inactive atlas pixels
inline constexpr uint32_t kMaximumFluidColliders = 4u;        // [-] - Analytic sphere collider slots in ColliderChunk
inline constexpr uint32_t kMaximumReleaseSources = 6u;        // [-] - ReleaseSource slots in InjectFragment (MAX_EMITTERS = 6)

//------------------------------------------------------------------------------------------------------------------------
//                                                     ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class FluidGridTopologyOrdinal : uint32_t
{
    Fixed    = 0u, // Fixed box or sliding WorldGrid (1^3..5^3 tiles)
    Infinite = 1u, // Unbounded sparse T^3 tile lattice backed by SparseTileBank
};

enum class FluidBoundaryOrdinal : uint32_t
{
    ClosedBox     = 0u, // Solid no-penetration on all 6 faces
    OpenTop       = 1u, // Open outflow through +Y face, solid on sides & floor
    OpenAll       = 2u, // Open outflow on all 6 faces
    InfiniteFloor = 3u, // Sparse Infinite mode: open everywhere except y < 0 world floor
};

enum class FluidAdvectionOrdinal : uint32_t
{
    SemiLagrangian = 0u, // First-order RK2 backtrace
    MacCormack     = 1u, // Selle 2008 overshoot-clamped predictor-corrector
};

enum class FluidFollowTargetOrdinal : uint32_t
{
    None          = 0u,
    ReleaseSource = 1u,
    Particles     = 2u,
    Camera        = 3u,
};

enum class ReleaseSourceShapeOrdinal : uint32_t
{
    Sphere = 0u,
    Disc   = 1u,
    Box    = 2u,
};

enum class FluidDebrisKindOrdinal : uint32_t
{
    Rubble = 0u,
    Ember  = 1u,
    Paper  = 2u,
};

enum class FluidResolutionPresetOrdinal : uint32_t
{
    Draft   = 32u,
    Low     = 48u,
    Medium  = 64u,
    High    = 96u,
    Ultra   = 128u,
    Extreme = 160u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  LAYOUT & REGION TYPES
//------------------------------------------------------------------------------------------------------------------------

struct FluidVolumeLayout
{
    uint32_t Width             = 48u;
    uint32_t Height            = 96u;
    uint32_t Depth             = 48u;
    uint32_t TilesX            = 7u;
    uint32_t TilesY            = 7u;
    uint32_t TextureWidth      = 336u;
    uint32_t TextureHeight     = 672u;
    uint32_t CellCount         = 221184u;
    uint32_t ResidentTileCount = 0u;
    uint32_t BankSlotCount     = 0u;
};

struct FluidColliderSphere
{
    std::array<float, 3> CentreMeters { 0.0f, 0.0f, 0.0f };
    float                RadiusMeters = 0.5f;
};

/// 1:1 native representation of FluidEditor's Gas Domain parameters (ForceGeneratorCatalog.js L405-L441).
struct FluidRegionParameters
{
    std::string              RegionId            = "domain";
    std::string              RegionName          = "Gas Domain";
    FluidGridTopologyOrdinal GridTopology        = FluidGridTopologyOrdinal::Fixed;
    std::array<float, 3>     SizeMeters          { 3.0f, 6.0f, 3.0f };
    uint32_t                 MaxCells            = 96u;
    std::array<uint32_t, 3>  Tiles               { 1u, 1u, 1u };
    std::array<uint32_t, 3>  TileCells           { 48u, 96u, 48u };
    std::array<uint32_t, 3>  Resolution          { 48u, 96u, 48u };
    float                    CellSizeMeters      = 0.0625f;
    std::array<int32_t, 3>   GridOrigin          { 0, 0, 0 };
    FluidFollowTargetOrdinal FollowTarget        = FluidFollowTargetOrdinal::None;
    bool                     FollowHeight        = false;
    float                    TileSizeMeters      = 2.0f;
    uint32_t                 TileResolution      = 24u;
    uint32_t                 TileBankSlots       = 48u;

    // Solver physics parameters
    float                    Buoyancy            = 1.2f;
    float                    AmbientTemperature  = 0.0f;
    float                    Cooling             = 0.6f;
    float                    Vorticity           = 0.35f;
    float                    Dissipation         = 0.996f;
    float                    VelocityDissipation = 0.999f;
    float                    Gravity             = 0.25f;
    uint32_t                 PressureIterations  = 36u;
    FluidAdvectionOrdinal    Advection           = FluidAdvectionOrdinal::MacCormack;
    FluidBoundaryOrdinal     Boundary            = FluidBoundaryOrdinal::OpenTop;
    float                    TimeScale           = 1.0f;
    float                    Combustion          = 1.0f;
    float                    Expansion           = 0.6f;
    float                    ForceResponse       = 1.0f;
    float                    SceneLoopSeconds    = 0.0f;
};

/// Matches FluidVolumeUniformBlock in Engine/Shaders/Fluid/FluidVolumeAccess.slang byte-for-byte (160 bytes, 16-byte aligned).
struct alignas(16) FluidVolumeUniformBlock
{
    std::array<int32_t, 4>              GridBoundary       { 48, 96, 48, 1 };      // xyz = uGrid, w = uBoundaryMode (0..3)
    std::array<float, 4>                GridCentreCellSize { 0.0f, 0.0f, 0.0f, 0.0625f }; // xyz = uGridCentre, w = uCellSize
    std::array<int32_t, 4>              CellOriginSparse   { -24, -48, -24, 0 };   // xyz = uCellOrigin, w = isSparse (0/1)
    std::array<int32_t, 4>              AtlasTilesTexSize  { 7, 7, 336, 672 };     // xy = uTiles, zw = uTexSize
    std::array<int32_t, 4>              SparseLayout       { 24, 7, 5, 5 };        // x = uTileCells, y = uSlotsX, z = uSliceCols, w = uSliceRows
    std::array<int32_t, 4>              LookupOriginCount  { -32, 0, -32, 0 };     // xyz = uLookupOrigin, w = uColliderCount (0..4)
    std::array<std::array<float, 4>, 4> Colliders          {};                     // xyz = world centre, w = radius
};
static_assert(sizeof(FluidVolumeUniformBlock) == 160u, "FluidVolumeUniformBlock must be 160 bytes");

//------------------------------------------------------------------------------------------------------------------------
//                                             GRID RESOLUTION HELPERS
//------------------------------------------------------------------------------------------------------------------------

/// Computes tiled 2D atlas layout for a dense 3D volume (GpuContext.js L255-L261).
[[nodiscard]] inline FluidVolumeLayout ComputeFluidVolumeLayout(
    const uint32_t width,
    const uint32_t height,
    const uint32_t depth) noexcept
{
    const uint32_t w = std::clamp(width,  kMinimumFixedCells, kMaximumFixedCells);
    const uint32_t h = std::clamp(height, kMinimumFixedCells, kMaximumFixedCells);
    const uint32_t d = std::clamp(depth,  kMinimumFixedCells, kMaximumFixedCells);
    const uint32_t tilesX = std::max(1u, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(d)))));
    const uint32_t tilesY = std::max(1u, (d + tilesX - 1u) / tilesX);
    FluidVolumeLayout layout {};
    layout.Width         = w;
    layout.Height        = h;
    layout.Depth         = d;
    layout.TilesX        = tilesX;
    layout.TilesY        = tilesY;
    layout.TextureWidth  = w * tilesX;
    layout.TextureHeight = h * tilesY;
    layout.CellCount     = w * h * d;
    return layout;
}

/// Resolves Infinite sparse-tile grid dimensions & bank slot budget (ForceGeneratorCatalog.js L376-L390).
inline void ResolveInfiniteFluidRegion(FluidRegionParameters& region) noexcept
{
    region.GridTopology   = FluidGridTopologyOrdinal::Infinite;
    region.TileSizeMeters = std::clamp(region.TileSizeMeters > 0.0f ? region.TileSizeMeters : 2.0f, 0.5f, 8.0f);
    region.TileResolution = std::clamp(region.TileResolution > 0u ? region.TileResolution : 24u, kMinimumSparseTileEdge, kMaximumSparseTileEdge);
    const uint64_t perTile = static_cast<uint64_t>(region.TileResolution) * region.TileResolution * region.TileResolution;
    const uint32_t maxByCells = static_cast<uint32_t>(kMaximumInfiniteCells / std::max<uint64_t>(1u, perTile));
    const uint32_t requested  = (region.TileBankSlots > 0u) ? region.TileBankSlots : 48u;
    const uint32_t upperCap   = std::min(kMaximumTileBankSlots, maxByCells);
    region.TileBankSlots  = std::max(4u, std::min(upperCap, requested));
    region.CellSizeMeters = region.TileSizeMeters / static_cast<float>(region.TileResolution);
    region.Resolution     = { region.TileResolution, region.TileResolution, region.TileResolution };
    region.Tiles          = { 1u, 1u, 1u };
    region.TileCells      = region.Resolution;
}

/// Resolves Fixed / WorldGrid (1^3..5^3) cell size and resolution (ForceGeneratorCatalog.js L348-L368).
inline void ResolveFixedFluidRegion(FluidRegionParameters& region) noexcept
{
    region.GridTopology = FluidGridTopologyOrdinal::Fixed;
    for (size_t axis = 0u; axis < 3u; ++axis)
    {
        region.Tiles[axis] = std::clamp(region.Tiles[axis] > 0u ? region.Tiles[axis] : 1u, 1u, kMaximumGridTiles);
    }
    const float longest = std::max({ region.SizeMeters[0], region.SizeMeters[1], region.SizeMeters[2], 0.1f });
    const uint32_t maxCells = std::clamp(region.MaxCells > 0u ? region.MaxCells : 96u, kMinimumFixedCells, kMaximumFixedCells);
    float cellSize = longest / static_cast<float>(maxCells);
    for (size_t axis = 0u; axis < 3u; ++axis)
    {
        cellSize = std::max(cellSize, region.SizeMeters[axis] * static_cast<float>(region.Tiles[axis]) / 160.0f);
    }
    const bool multi = (region.Tiles[0] * region.Tiles[1] * region.Tiles[2]) > 1u;
    const uint32_t minTileCells = multi ? 4u : 8u;
    for (size_t axis = 0u; axis < 3u; ++axis)
    {
        const uint32_t tc = std::max(minTileCells, static_cast<uint32_t>(std::lround(static_cast<double>(region.SizeMeters[axis]) / static_cast<double>(cellSize))));
        region.TileCells[axis]  = tc;
        region.Resolution[axis] = std::clamp(tc * region.Tiles[axis], kMinimumFixedCells, kMaximumFixedCells);
    }
    region.CellSizeMeters = cellSize;
}

inline void ResolveFluidRegionGrid(FluidRegionParameters& region) noexcept
{
    if (region.GridTopology == FluidGridTopologyOrdinal::Infinite) ResolveInfiniteFluidRegion(region);
    else ResolveFixedFluidRegion(region);
}

/// World-space centre of the Fixed/WorldGrid simulated window (ForceGeneratorCatalog.js L393-L397).
[[nodiscard]] inline std::array<float, 3> ComputeGridWindowCentre(const FluidRegionParameters& region) noexcept
{
    return {
        static_cast<float>(region.GridOrigin[0]) * static_cast<float>(region.TileCells[0]) * region.CellSizeMeters,
        static_cast<float>(region.GridOrigin[1]) * static_cast<float>(region.TileCells[1]) * region.CellSizeMeters,
        static_cast<float>(region.GridOrigin[2]) * static_cast<float>(region.TileCells[2]) * region.CellSizeMeters
    };
}

/// WorldGrid tile index containing a world position (ForceGeneratorCatalog.js L400-L403).
[[nodiscard]] inline std::array<int32_t, 3> ComputeWorldGridTileOf(
    const FluidRegionParameters& region,
    const std::array<float, 3>& worldPosition) noexcept
{
    std::array<int32_t, 3> tile {};
    for (size_t axis = 0u; axis < 3u; ++axis)
    {
        const double pitch = std::max(1.0e-5, static_cast<double>(region.TileCells[axis]) * static_cast<double>(region.CellSizeMeters));
        tile[axis] = static_cast<int32_t>(std::floor(static_cast<double>(worldPosition[axis]) / pitch + 0.5));
    }
    return tile;
}

} // namespace Frontier::SimulationSpace::Fluid
