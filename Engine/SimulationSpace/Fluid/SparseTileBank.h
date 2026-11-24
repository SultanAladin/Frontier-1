/*============================================================================================================================================
                                                           SPARSETILEBANK.H
============================================================================================================================================*/
// 🧩 Stage D (Slice S14) 1:1 Sparse Infinite fluid tile bank (64x16x64 lookup, occupancy growth/eviction, bounds & Fixed/WorldGrid relocation)
// Reference: Experimental/FluidEditor/Engine/FluidSolver.js L89-L389 & FluidShaders.js L22-L233, L494-L531

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Fluid/FluidRegionTypes.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Frontier::SimulationSpace::Fluid
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    DATA STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SparseTileSlotEntry
{
    std::array<int32_t, 3> TileCoord     { 0, 0, 0 }; // [-] - Unbounded world tile coordinates (tx, ty, tz)
    bool                   Pinned        = false;     // [-] - True while a ReleaseSource overlaps this tile
    uint64_t               AllocatedTick = 0u;        // [-] - Solver step tick when this slot was allocated
    float                  MeanDensity   = 0.0f;      // [-] - Volume-averaged occupancy metric
    float                  PeakDensity   = 0.0f;      // [-] - Maximum occupancy metric in tile
};

struct SparsePinnedSource
{
    std::array<float, 3> PositionMeters { 0.0f, 0.0f, 0.0f };
    float                RadiusMeters   = 0.5f;
};

struct SparseTileRelocationResult
{
    std::array<int32_t, 3> ShiftCells { 0, 0, 0 };
    std::array<float, 3>   NewCentre  { 0.0f, 0.0f, 0.0f };
    bool                   ClearWhole = false;
    bool                   Moved      = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                           COORDINATE & BOUNDARY QUERIES
//------------------------------------------------------------------------------------------------------------------------

/// Matches VolumeChunk / SparseVolumeChunk WorldFromCell (FluidShaders.js L79-L82, L215-L218).
[[nodiscard]] std::array<float, 3> WorldFromFluidCell(
    const FluidVolumeUniformBlock& uniforms,
    const std::array<float, 3>& cellCentre) noexcept;

/// Matches VolumeChunk / SparseVolumeChunk CellFromWorld (FluidShaders.js L84-L87, L220-L223).
[[nodiscard]] std::array<float, 3> FluidCellFromWorld(
    const FluidVolumeUniformBlock& uniforms,
    const std::array<float, 3>& worldPos) noexcept;

/// Matches BoundaryChunk::OpenFace (FluidShaders.js L519-L526).
[[nodiscard]] bool IsOpenFluidFace(
    const FluidVolumeUniformBlock& uniforms,
    const std::array<int32_t, 3>& neighbourCell) noexcept;

/// Matches ColliderChunk::SolidAtWorld (FluidShaders.js L499-L506).
[[nodiscard]] bool IsSolidAtFluidWorld(
    const FluidVolumeUniformBlock& uniforms,
    const std::array<float, 3>& worldPos) noexcept;

/// Matches ColliderChunk::SolidAtCell (FluidShaders.js L508-L511).
[[nodiscard]] bool IsSolidAtFluidCell(
    const FluidVolumeUniformBlock& uniforms,
    const std::array<int32_t, 3>& cell) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                  SPARSETILEBANK
//------------------------------------------------------------------------------------------------------------------------

/// Manages both Fixed/WorldGrid layout metadata and Infinite sparse T^3 tile allocation, lookup packing & eviction.
class SparseTileBank
{
public:
    SparseTileBank() noexcept = default;
    ~SparseTileBank() noexcept = default;

    /// Configures Fixed / WorldGrid dense volume addressing (FluidSolver.js L89-L100).
    void ConfigureFixed(
        const std::array<uint32_t, 3>& resolution,
        float cellSizeMeters,
        const std::array<float, 3>& centreMeters = { 0.0f, 0.0f, 0.0f });

    /// Configures Infinite sparse tile lattice (FluidSolver.js L152-L185).
    void ConfigureInfinite(uint32_t tileCells, float cellSizeMeters, uint32_t maxSlots);

    /// Resets all resident tiles and counters while keeping allocated table capacities.
    void ResetTiles();

    [[nodiscard]] bool IsSparse() const noexcept { return SparseMode; }
    [[nodiscard]] float QueryCellSizeMeters() const noexcept { return CellSizeMeters; }
    [[nodiscard]] const FluidVolumeLayout& QueryLayout() const noexcept { return Layout; }
    [[nodiscard]] const std::array<float, 3>& QueryGridCentre() const noexcept { return GridCentreMeters; }
    [[nodiscard]] const std::array<int32_t, 3>& QueryCellOrigin() const noexcept { return CellOrigin; }
    [[nodiscard]] const std::array<int32_t, 3>& QueryLookupOrigin() const noexcept { return LookupOrigin; }
    [[nodiscard]] uint32_t QueryTileCells() const noexcept { return TileCells; }
    [[nodiscard]] uint32_t QuerySlotCount() const noexcept { return SlotCount; }
    [[nodiscard]] uint32_t QuerySlotsX() const noexcept { return SlotsX; }
    [[nodiscard]] uint32_t QuerySliceCols() const noexcept { return SliceCols; }
    [[nodiscard]] uint32_t QuerySliceRows() const noexcept { return SliceRows; }
    [[nodiscard]] uint32_t QueryScheduleInterval() const noexcept { return ScheduleInterval; }
    [[nodiscard]] uint32_t QueryFaceBandCells() const noexcept { return FaceBandCells; }
    [[nodiscard]] uint32_t QueryOccupancyStride() const noexcept { return OccupancyStride; }
    [[nodiscard]] uint64_t QueryAllocationCount() const noexcept { return AllocationCount; }
    [[nodiscard]] uint64_t QueryFreeCount() const noexcept { return FreeCount; }
    [[nodiscard]] uint64_t QueryRelocationCount() const noexcept { return RelocationCount; }
    [[nodiscard]] uint32_t QueryResidentTileCount() const noexcept;

    [[nodiscard]] std::span<const std::optional<SparseTileSlotEntry>> QuerySlots() const noexcept { return Entries; }
    [[nodiscard]] std::span<const int16_t> QueryLookupTexels() const noexcept { return LookupTexels; }
    [[nodiscard]] std::span<const int32_t> QuerySlotTableTexels() const noexcept { return SlotTableTexels; }

    /// Tile coordinate containing a world-space point (FluidSolver.js L208-L212).
    [[nodiscard]] std::array<int32_t, 3> TileOfWorld(const std::array<float, 3>& worldPos) const noexcept;

    /// Tile coordinate containing a world cell index (SparseVolumeChunk::TileOfWorldCell).
    [[nodiscard]] std::array<int32_t, 3> TileOfWorldCell(const std::array<int32_t, 3>& worldCell) const noexcept;

    /// Returns true if tile lies within the 64x16x64 lookup window (FluidSolver.js L214-L218).
    [[nodiscard]] bool IsTileInLookup(const std::array<int32_t, 3>& tile) const noexcept;

    /// Returns slot index [0..SlotCount-1] for tile, or -1 if non-resident (SparseVolumeChunk::SlotOfTile).
    [[nodiscard]] int32_t FindSlotOfTile(const std::array<int32_t, 3>& tile) const noexcept;

    /// Returns true if bounding-box-relative `cell` lies inside a resident tile (or Fixed grid bounds).
    [[nodiscard]] bool IsCellInside(const std::array<int32_t, 3>& cell) const noexcept;

    /// Computes 2D atlas texel coordinate for bounding-box-relative `cell`, or {-1,-1} if non-resident.
    [[nodiscard]] std::array<int32_t, 2> AtlasPixelFromCell(const std::array<int32_t, 3>& cell) const noexcept;

    /// Inverse of AtlasPixelFromCell: maps 2D atlas texel to bounding-box-relative cell (or kSparseOutsideCell).
    [[nodiscard]] std::array<int32_t, 3> CellFromAtlasPixel(int32_t pixelX, int32_t pixelY) const noexcept;

    /// Allocates or pins a tile slot, evicting the weakest unpinned slot if full (FluidSolver.js L220-L246).
    [[nodiscard]] int32_t AllocateTile(
        const std::array<int32_t, 3>& tile,
        bool pinned,
        uint64_t currentTick,
        float requestedMean = 0.0f);

    /// Frees an active slot and marks lookup tables dirty (FluidSolver.js L248-L256).
    void FreeTile(int32_t slot);

    /// Pins active source tiles, recenters 64x16x64 lookup window, evaluates 3xSlot RGBA32F occupancy readback
    /// for 6-face growth (>0.0025) & empty-tile eviction (<0.0015 & peak<0.02 & age>90), and repacks GPU tables.
    void ScheduleTiles(
        std::span<const SparsePinnedSource> activeSources,
        uint64_t currentTick,
        std::span<const float> occupancyReadbackRgba = {});

    /// Repacks LookupTexels (64x1024 int16) & SlotTableTexels (SlotCount x 4 int32) and updates bounding box.
    void RebuildGpuTables();

    /// Recomputes bounding box CellOrigin, Layout, and GridCentreMeters from resident tiles (FluidSolver.js L324-L339).
    void UpdateBounds() noexcept;

    /// Computes WorldGrid integer cell shift and updates GridCentreMeters (FluidSolver.js L346-L363).
    [[nodiscard]] SparseTileRelocationResult RelocateFixedWindow(
        const std::array<int32_t, 3>& shiftCells,
        const std::array<float, 3>& newCentreMeters) noexcept;

    /// Packs FluidVolumeUniformBlock matching FluidVolumeAccess.slang byte-for-byte (160 bytes).
    [[nodiscard]] FluidVolumeUniformBlock BuildVolumeUniforms(
        FluidBoundaryOrdinal boundary,
        std::span<const FluidColliderSphere> colliders = {}) const noexcept;

private:
    bool                                         SparseMode       = false;
    float                                        CellSizeMeters   = 0.0625f;
    FluidVolumeLayout                            Layout           {};
    std::array<float, 3>                         GridCentreMeters { 0.0f, 0.0f, 0.0f };
    std::array<int32_t, 3>                       CellOrigin       { 0, 0, 0 };

    // Sparse Infinite bank state
    uint32_t                                     TileCells        = 24u;
    float                                        TileSizeMeters   = 2.0f;
    uint32_t                                     SlotCount        = 48u;
    uint32_t                                     SlotsX           = 7u;
    uint32_t                                     SlotsY           = 7u;
    uint32_t                                     SliceCols        = 5u;
    uint32_t                                     SliceRows        = 5u;
    uint32_t                                     BlockWidth       = 120u;
    uint32_t                                     BlockHeight      = 120u;
    std::array<int32_t, 3>                       LookupOrigin     { -32, 0, -32 };
    std::vector<std::optional<SparseTileSlotEntry>> Entries       {};
    std::vector<int16_t>                         LookupTexels     {}; // 64 x 1024 (x + (z + 64*y)*64) -> slot or -1
    std::vector<int32_t>                         SlotTableTexels  {}; // SlotCount * 4: (tx, ty, tz, active)
    uint32_t                                     ScheduleInterval = 3u;
    uint32_t                                     FaceBandCells    = 6u;
    uint32_t                                     OccupancyStride  = 1u;
    bool                                         TablesDirty      = true;
    uint64_t                                     AllocationCount  = 0u;
    uint64_t                                     FreeCount        = 0u;
    uint64_t                                     RelocationCount  = 0u;
};

} // namespace Frontier::SimulationSpace::Fluid
