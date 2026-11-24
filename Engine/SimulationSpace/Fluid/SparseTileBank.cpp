/*============================================================================================================================================
                                                          SPARSETILEBANK.CPP
============================================================================================================================================*/
// 🧩 Stage D (Slice S14) 1:1 Sparse Infinite fluid tile bank (64x16x64 lookup, occupancy growth/eviction, bounds & Fixed/WorldGrid relocation)
// Reference: Experimental/FluidEditor/Engine/FluidSolver.js L89-L389 & FluidShaders.js L22-L233, L494-L531

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Fluid/SparseTileBank.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Frontier::SimulationSpace::Fluid
{

namespace
{

[[nodiscard]] int32_t FloorDivInt(const int32_t value, const int32_t divisor) noexcept
{
    const float q = std::floor(static_cast<float>(value) / static_cast<float>(divisor));
    return static_cast<int32_t>(q);
}

[[nodiscard]] size_t LookupLinearIndex(const int32_t rx, const int32_t ry, const int32_t rz) noexcept
{
    return static_cast<size_t>(rx + (rz + kLookupExtentZ * ry) * kLookupExtentX);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           COORDINATE & BOUNDARY QUERIES
//------------------------------------------------------------------------------------------------------------------------

std::array<float, 3> WorldFromFluidCell(
    const FluidVolumeUniformBlock& uniforms,
    const std::array<float, 3>& cellCentre) noexcept
{
    const float h = uniforms.GridCentreCellSize[3];
    if (uniforms.CellOriginSparse[3] != 0)
    {
        return {
            (cellCentre[0] + static_cast<float>(uniforms.CellOriginSparse[0])) * h,
            (cellCentre[1] + static_cast<float>(uniforms.CellOriginSparse[1])) * h,
            (cellCentre[2] + static_cast<float>(uniforms.CellOriginSparse[2])) * h
        };
    }
    return {
        (cellCentre[0] - static_cast<float>(uniforms.GridBoundary[0]) * 0.5f) * h + uniforms.GridCentreCellSize[0],
        (cellCentre[1] - static_cast<float>(uniforms.GridBoundary[1]) * 0.5f) * h + uniforms.GridCentreCellSize[1],
        (cellCentre[2] - static_cast<float>(uniforms.GridBoundary[2]) * 0.5f) * h + uniforms.GridCentreCellSize[2]
    };
}

std::array<float, 3> FluidCellFromWorld(
    const FluidVolumeUniformBlock& uniforms,
    const std::array<float, 3>& worldPos) noexcept
{
    const float h = std::max(1.0e-6f, uniforms.GridCentreCellSize[3]);
    if (uniforms.CellOriginSparse[3] != 0)
    {
        return {
            worldPos[0] / h - static_cast<float>(uniforms.CellOriginSparse[0]),
            worldPos[1] / h - static_cast<float>(uniforms.CellOriginSparse[1]),
            worldPos[2] / h - static_cast<float>(uniforms.CellOriginSparse[2])
        };
    }
    return {
        (worldPos[0] - uniforms.GridCentreCellSize[0]) / h + static_cast<float>(uniforms.GridBoundary[0]) * 0.5f,
        (worldPos[1] - uniforms.GridCentreCellSize[1]) / h + static_cast<float>(uniforms.GridBoundary[1]) * 0.5f,
        (worldPos[2] - uniforms.GridCentreCellSize[2]) / h + static_cast<float>(uniforms.GridBoundary[2]) * 0.5f
    };
}

bool IsOpenFluidFace(
    const FluidVolumeUniformBlock& uniforms,
    const std::array<int32_t, 3>& neighbourCell) noexcept
{
    const int32_t mode = uniforms.GridBoundary[3];
    if (mode == 3) return (neighbourCell[1] + uniforms.CellOriginSparse[1]) >= 0;
    if (mode == 2) return true;
    if (mode == 1 && neighbourCell[1] >= uniforms.GridBoundary[1]) return true;
    return false;
}

bool IsSolidAtFluidWorld(
    const FluidVolumeUniformBlock& uniforms,
    const std::array<float, 3>& worldPos) noexcept
{
    const int32_t count = std::clamp(uniforms.LookupOriginCount[3], 0, static_cast<int32_t>(kMaximumFluidColliders));
    for (int32_t i = 0; i < count; ++i)
    {
        const auto& c = uniforms.Colliders[static_cast<size_t>(i)];
        const float dx = worldPos[0] - c[0];
        const float dy = worldPos[1] - c[1];
        const float dz = worldPos[2] - c[2];
        if ((dx * dx + dy * dy + dz * dz) < (c[3] * c[3])) return true;
    }
    return false;
}

bool IsSolidAtFluidCell(
    const FluidVolumeUniformBlock& uniforms,
    const std::array<int32_t, 3>& cell) noexcept
{
    return IsSolidAtFluidWorld(uniforms, WorldFromFluidCell(uniforms, {
        static_cast<float>(cell[0]) + 0.5f,
        static_cast<float>(cell[1]) + 0.5f,
        static_cast<float>(cell[2]) + 0.5f
    }));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SPARSETILEBANK
//------------------------------------------------------------------------------------------------------------------------

void SparseTileBank::ConfigureFixed(
    const std::array<uint32_t, 3>& resolution,
    const float cellSizeMeters,
    const std::array<float, 3>& centreMeters)
{
    // Reference: FluidSolver.js L89-L100
    SparseMode       = false;
    CellSizeMeters   = std::max(1.0e-4f, cellSizeMeters);
    GridCentreMeters = centreMeters;
    Layout           = ComputeFluidVolumeLayout(resolution[0], resolution[1], resolution[2]);
    CellOrigin       = {
        -static_cast<int32_t>(Layout.Width / 2u),
        -static_cast<int32_t>(Layout.Height / 2u),
        -static_cast<int32_t>(Layout.Depth / 2u)
    };
    Entries.clear();
    LookupTexels.clear();
    SlotTableTexels.clear();
    TablesDirty = false;
}

void SparseTileBank::ConfigureInfinite(
    const uint32_t tileCells,
    const float cellSizeMeters,
    const uint32_t maxSlots)
{
    // Reference: FluidSolver.js L152-L185
    const uint32_t T     = std::clamp(tileCells, kMinimumSparseTileEdge, kMaximumSparseTileEdge);
    const uint32_t slots = std::clamp(maxSlots, 4u, 256u);
    const uint32_t sCols = std::max(1u, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(T)))));
    const uint32_t sRows = std::max(1u, (T + sCols - 1u) / sCols);
    const uint32_t sX    = std::max(1u, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(slots)))));
    const uint32_t sY    = std::max(1u, (slots + sX - 1u) / sX);
    const uint32_t bW    = T * sCols;
    const uint32_t bH    = T * sRows;

    const bool unchanged = SparseMode && (TileCells == T) && (SlotCount == slots);
    CellSizeMeters = std::max(1.0e-4f, cellSizeMeters);
    if (unchanged)
    {
        TileSizeMeters = static_cast<float>(T) * CellSizeMeters;
        UpdateBounds();
        return;
    }

    SparseMode       = true;
    TileCells        = T;
    TileSizeMeters   = static_cast<float>(T) * CellSizeMeters;
    SlotCount        = slots;
    SlotsX           = sX;
    SlotsY           = sY;
    SliceCols        = sCols;
    SliceRows        = sRows;
    BlockWidth       = bW;
    BlockHeight      = bH;
    LookupOrigin     = { -32, 0, -32 };
    ScheduleInterval = 3u;
    FaceBandCells    = std::max(3u, static_cast<uint32_t>(std::lround(static_cast<double>(T) * 0.25)));
    OccupancyStride  = (T >= 32u) ? 2u : 1u;
    AllocationCount  = 0u;
    FreeCount        = 0u;
    TablesDirty      = true;

    Entries.assign(slots, std::nullopt);
    LookupTexels.assign(kLookupTexelCount, static_cast<int16_t>(-1));
    SlotTableTexels.assign(static_cast<size_t>(slots) * 4u, 0);

    Layout = FluidVolumeLayout {};
    Layout.Width         = T;
    Layout.Height        = T;
    Layout.Depth         = T;
    Layout.TilesX        = sCols;
    Layout.TilesY        = sRows;
    Layout.TextureWidth  = sX * bW;
    Layout.TextureHeight = sY * bH;
    Layout.CellCount     = 0u;
    Layout.BankSlotCount = slots;
    UpdateBounds();
}

void SparseTileBank::ResetTiles()
{
    if (!SparseMode) return;
    Entries.assign(SlotCount, std::nullopt);
    LookupOrigin = { -32, 0, -32 };
    TablesDirty  = true;
    RebuildGpuTables();
}

uint32_t SparseTileBank::QueryResidentTileCount() const noexcept
{
    uint32_t count = 0u;
    for (const auto& e : Entries)
    {
        if (e.has_value()) ++count;
    }
    return count;
}

std::array<int32_t, 3> SparseTileBank::TileOfWorld(const std::array<float, 3>& worldPos) const noexcept
{
    const double sz = std::max(1.0e-5, static_cast<double>(TileSizeMeters));
    return {
        static_cast<int32_t>(std::floor(static_cast<double>(worldPos[0]) / sz)),
        static_cast<int32_t>(std::floor(static_cast<double>(worldPos[1]) / sz)),
        static_cast<int32_t>(std::floor(static_cast<double>(worldPos[2]) / sz))
    };
}

std::array<int32_t, 3> SparseTileBank::TileOfWorldCell(const std::array<int32_t, 3>& worldCell) const noexcept
{
    const int32_t T = static_cast<int32_t>(std::max(1u, TileCells));
    return {
        FloorDivInt(worldCell[0], T),
        FloorDivInt(worldCell[1], T),
        FloorDivInt(worldCell[2], T)
    };
}

bool SparseTileBank::IsTileInLookup(const std::array<int32_t, 3>& tile) const noexcept
{
    return tile[0] >= LookupOrigin[0] && tile[0] < LookupOrigin[0] + kLookupExtentX
        && tile[1] >= LookupOrigin[1] && tile[1] < LookupOrigin[1] + kLookupExtentY
        && tile[2] >= LookupOrigin[2] && tile[2] < LookupOrigin[2] + kLookupExtentZ;
}

int32_t SparseTileBank::FindSlotOfTile(const std::array<int32_t, 3>& tile) const noexcept
{
    if (!SparseMode || !IsTileInLookup(tile) || LookupTexels.size() != kLookupTexelCount) return -1;
    if (!TablesDirty)
    {
        const int32_t rx = tile[0] - LookupOrigin[0];
        const int32_t ry = tile[1] - LookupOrigin[1];
        const int32_t rz = tile[2] - LookupOrigin[2];
        return static_cast<int32_t>(LookupTexels[LookupLinearIndex(rx, ry, rz)]);
    }
    for (size_t s = 0u; s < Entries.size(); ++s)
    {
        if (Entries[s].has_value() && Entries[s]->TileCoord == tile)
        {
            return static_cast<int32_t>(s);
        }
    }
    return -1;
}

bool SparseTileBank::IsCellInside(const std::array<int32_t, 3>& cell) const noexcept
{
    if (!SparseMode)
    {
        return cell[0] >= 0 && cell[0] < static_cast<int32_t>(Layout.Width)
            && cell[1] >= 0 && cell[1] < static_cast<int32_t>(Layout.Height)
            && cell[2] >= 0 && cell[2] < static_cast<int32_t>(Layout.Depth);
    }
    if (cell[0] <= kSparseOutsideCell) return false;
    const std::array<int32_t, 3> worldCell {
        cell[0] + CellOrigin[0],
        cell[1] + CellOrigin[1],
        cell[2] + CellOrigin[2]
    };
    return FindSlotOfTile(TileOfWorldCell(worldCell)) >= 0;
}

std::array<int32_t, 2> SparseTileBank::AtlasPixelFromCell(const std::array<int32_t, 3>& cell) const noexcept
{
    if (!SparseMode)
    {
        const int32_t tilesX = static_cast<int32_t>(std::max(1u, Layout.TilesX));
        const int32_t tileX  = cell[2] - (cell[2] / tilesX) * tilesX;
        const int32_t tileY  = cell[2] / tilesX;
        return {
            tileX * static_cast<int32_t>(Layout.Width)  + cell[0],
            tileY * static_cast<int32_t>(Layout.Height) + cell[1]
        };
    }

    const std::array<int32_t, 3> worldCell {
        cell[0] + CellOrigin[0],
        cell[1] + CellOrigin[1],
        cell[2] + CellOrigin[2]
    };
    const auto tile = TileOfWorldCell(worldCell);
    const int32_t slot = FindSlotOfTile(tile);
    if (slot < 0) return { -1, -1 };

    const int32_t T = static_cast<int32_t>(TileCells);
    const int32_t lx = worldCell[0] - tile[0] * T;
    const int32_t ly = worldCell[1] - tile[1] * T;
    const int32_t lz = worldCell[2] - tile[2] * T;
    const int32_t sCols = static_cast<int32_t>(SliceCols);
    const int32_t sx = lz - (lz / sCols) * sCols;
    const int32_t sy = lz / sCols;
    const int32_t slotX = slot - (slot / static_cast<int32_t>(SlotsX)) * static_cast<int32_t>(SlotsX);
    const int32_t slotY = slot / static_cast<int32_t>(SlotsX);
    return {
        slotX * static_cast<int32_t>(BlockWidth)  + sx * T + lx,
        slotY * static_cast<int32_t>(BlockHeight) + sy * T + ly
    };
}

std::array<int32_t, 3> SparseTileBank::CellFromAtlasPixel(const int32_t pixelX, const int32_t pixelY) const noexcept
{
    if (!SparseMode)
    {
        const int32_t w = static_cast<int32_t>(Layout.Width);
        const int32_t h = static_cast<int32_t>(Layout.Height);
        const int32_t tileX = pixelX / w;
        const int32_t tileY = pixelY / h;
        return {
            pixelX - tileX * w,
            pixelY - tileY * h,
            tileY * static_cast<int32_t>(Layout.TilesX) + tileX
        };
    }

    const int32_t bW = static_cast<int32_t>(BlockWidth);
    const int32_t bH = static_cast<int32_t>(BlockHeight);
    if (pixelX < 0 || pixelY < 0 || bW <= 0 || bH <= 0) return { kSparseOutsideCell, kSparseOutsideCell, kSparseOutsideCell };
    const int32_t slot = (pixelX / bW) + (pixelY / bH) * static_cast<int32_t>(SlotsX);
    if (slot < 0 || static_cast<size_t>(slot) >= Entries.size() || !Entries[static_cast<size_t>(slot)].has_value())
    {
        return { kSparseOutsideCell, kSparseOutsideCell, kSparseOutsideCell };
    }
    const int32_t T = static_cast<int32_t>(TileCells);
    const int32_t bx = pixelX - (pixelX / bW) * bW;
    const int32_t by = pixelY - (pixelY / bH) * bH;
    const int32_t slice = (by / T) * static_cast<int32_t>(SliceCols) + (bx / T);
    if (slice >= T) return { kSparseOutsideCell, kSparseOutsideCell, kSparseOutsideCell };
    const auto& tc = Entries[static_cast<size_t>(slot)]->TileCoord;
    return {
        tc[0] * T + (bx - (bx / T) * T) - CellOrigin[0],
        tc[1] * T + (by - (by / T) * T) - CellOrigin[1],
        tc[2] * T + slice - CellOrigin[2]
    };
}

int32_t SparseTileBank::AllocateTile(
    const std::array<int32_t, 3>& tile,
    const bool pinned,
    const uint64_t currentTick,
    const float requestedMean)
{
    // Reference: FluidSolver.js L220-L246
    if (!SparseMode || tile[1] < 0 || !IsTileInLookup(tile)) return -1;

    const int32_t existing = FindSlotOfTile(tile);
    if (existing >= 0)
    {
        auto& entry = Entries[static_cast<size_t>(existing)];
        if (entry.has_value()) entry->Pinned = entry->Pinned || pinned;
        return existing;
    }

    int32_t slot = -1;
    for (size_t i = 0u; i < Entries.size(); ++i)
    {
        if (!Entries[i].has_value())
        {
            slot = static_cast<int32_t>(i);
            break;
        }
    }

    if (slot < 0)
    {
        // Bank full: evict the thinnest unpinned slot if it carries less gas than the requester.
        int32_t weakest = -1;
        float weakestMean = std::max(requestedMean, 0.01f);
        for (size_t i = 0u; i < Entries.size(); ++i)
        {
            if (Entries[i].has_value() && !Entries[i]->Pinned && Entries[i]->MeanDensity < weakestMean)
            {
                weakest     = static_cast<int32_t>(i);
                weakestMean = Entries[i]->MeanDensity;
            }
        }
        if (weakest < 0) return -1;
        FreeTile(weakest);
        slot = weakest;
    }

    Entries[static_cast<size_t>(slot)] = SparseTileSlotEntry { tile, pinned, currentTick, 0.0f, 0.0f };
    ++AllocationCount;
    TablesDirty = true;
    return slot;
}

void SparseTileBank::FreeTile(const int32_t slot)
{
    // Reference: FluidSolver.js L248-L256
    if (!SparseMode || slot < 0 || static_cast<size_t>(slot) >= Entries.size()) return;
    if (!Entries[static_cast<size_t>(slot)].has_value()) return;
    Entries[static_cast<size_t>(slot)].reset();
    ++FreeCount;
    TablesDirty = true;
}

void SparseTileBank::ScheduleTiles(
    const std::span<const SparsePinnedSource> activeSources,
    const uint64_t currentTick,
    const std::span<const float> occupancyReadbackRgba)
{
    // Reference: FluidSolver.js L259-L307
    if (!SparseMode) return;

    for (auto& entry : Entries)
    {
        if (entry.has_value()) entry->Pinned = false;
    }

    std::vector<std::array<int32_t, 3>> required;
    for (const SparsePinnedSource& src : activeSources)
    {
        const float reach = src.RadiusMeters + TileSizeMeters * 0.15f;
        const auto lo = TileOfWorld({ src.PositionMeters[0] - reach, src.PositionMeters[1] - reach, src.PositionMeters[2] - reach });
        const auto hi = TileOfWorld({ src.PositionMeters[0] + reach, src.PositionMeters[1] + reach, src.PositionMeters[2] + reach });
        for (int32_t x = lo[0]; x <= hi[0]; ++x)
        {
            for (int32_t y = std::max(0, lo[1]); y <= hi[1]; ++y)
            {
                for (int32_t z = lo[2]; z <= hi[2]; ++z)
                {
                    required.push_back({ x, y, z });
                }
            }
        }
    }

    // Keep the 64x16x64 lookup window centred on active sources (or resident gas).
    std::vector<std::array<int32_t, 3>> focusSource = required;
    if (focusSource.empty())
    {
        for (const auto& entry : Entries)
        {
            if (entry.has_value()) focusSource.push_back(entry->TileCoord);
        }
    }
    if (!focusSource.empty())
    {
        std::array<int32_t, 3> focus { 0, 0, 0 };
        for (size_t axis = 0u; axis < 3u; ++axis)
        {
            double sum = 0.0;
            for (const auto& t : focusSource) sum += static_cast<double>(t[axis]);
            focus[axis] = static_cast<int32_t>(std::lround(sum / static_cast<double>(focusSource.size())));
        }
        if (std::abs(focus[0] - (LookupOrigin[0] + 32)) > 16
            || std::abs(focus[2] - (LookupOrigin[2] + 32)) > 16
            || (focus[1] - LookupOrigin[1]) > 12)
        {
            LookupOrigin = { focus[0] - 32, std::max(0, focus[1] - 4), focus[2] - 32 };
            for (size_t s = 0u; s < Entries.size(); ++s)
            {
                if (Entries[s].has_value() && !IsTileInLookup(Entries[s]->TileCoord))
                {
                    FreeTile(static_cast<int32_t>(s));
                }
            }
            TablesDirty = true;
        }
    }

    for (const auto& tile : required)
    {
        (void)AllocateTile(tile, true, currentTick, 0.0f);
    }

    const size_t expectedReadbackFloats = static_cast<size_t>(SlotCount) * 12u; // 3 texels x 4 floats per slot
    if ((currentTick % ScheduleInterval == 0u) && occupancyReadbackRgba.size() >= expectedReadbackFloats && QueryResidentTileCount() > 0u)
    {
        const uint32_t perAxis = std::max(1u, (TileCells + OccupancyStride - 1u) / OccupancyStride);
        const float samples = static_cast<float>(perAxis * perAxis * perAxis);
        const uint32_t bandAxis = std::max(1u, (FaceBandCells + OccupancyStride - 1u) / OccupancyStride);
        const float bandSamples = static_cast<float>(bandAxis * perAxis * perAxis);

        struct GrowRequest
        {
            std::array<int32_t, 3> Tile;
            float                  Mean;
        };
        std::vector<GrowRequest> growList;
        static constexpr std::array<std::array<int32_t, 3>, 6> kFaceDirs {{
            { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }
        }};

        for (size_t s = 0u; s < Entries.size(); ++s)
        {
            if (!Entries[s].has_value()) continue;
            auto& entry = *Entries[s];
            const size_t base = s * 12u;
            entry.MeanDensity = occupancyReadbackRgba[base + 0u] / samples;
            entry.PeakDensity = occupancyReadbackRgba[base + 1u];

            for (size_t f = 0u; f < 6u; ++f)
            {
                const float faceMean = occupancyReadbackRgba[base + 4u + f] / bandSamples;
                if (faceMean > 0.0025f)
                {
                    growList.push_back(GrowRequest {
                        { entry.TileCoord[0] + kFaceDirs[f][0],
                          entry.TileCoord[1] + kFaceDirs[f][1],
                          entry.TileCoord[2] + kFaceDirs[f][2] },
                        faceMean
                    });
                }
            }

            if (!entry.Pinned && entry.MeanDensity < 0.0015f && entry.PeakDensity < 0.02f
                && currentTick > entry.AllocatedTick + 90u)
            {
                FreeTile(static_cast<int32_t>(s));
            }
        }

        std::sort(growList.begin(), growList.end(), [](const GrowRequest& a, const GrowRequest& b) {
            return a.Mean > b.Mean;
        });
        for (const GrowRequest& req : growList)
        {
            if (FindSlotOfTile(req.Tile) < 0)
            {
                (void)AllocateTile(req.Tile, false, currentTick, req.Mean);
            }
        }
    }

    if (TablesDirty)
    {
        RebuildGpuTables();
    }
}

void SparseTileBank::RebuildGpuTables()
{
    // Reference: FluidSolver.js L309-L321
    if (!SparseMode) return;
    LookupTexels.assign(kLookupTexelCount, static_cast<int16_t>(-1));
    SlotTableTexels.assign(static_cast<size_t>(SlotCount) * 4u, 0);

    for (size_t s = 0u; s < Entries.size(); ++s)
    {
        if (!Entries[s].has_value()) continue;
        const auto& tc = Entries[s]->TileCoord;
        if (!IsTileInLookup(tc)) continue;
        const int32_t rx = tc[0] - LookupOrigin[0];
        const int32_t ry = tc[1] - LookupOrigin[1];
        const int32_t rz = tc[2] - LookupOrigin[2];
        LookupTexels[LookupLinearIndex(rx, ry, rz)] = static_cast<int16_t>(s);
        SlotTableTexels[s * 4u + 0u] = tc[0];
        SlotTableTexels[s * 4u + 1u] = tc[1];
        SlotTableTexels[s * 4u + 2u] = tc[2];
        SlotTableTexels[s * 4u + 3u] = 1;
    }

    TablesDirty = false;
    UpdateBounds();
}

void SparseTileBank::UpdateBounds() noexcept
{
    // Reference: FluidSolver.js L324-L339
    if (!SparseMode) return;
    const int32_t T = static_cast<int32_t>(TileCells);
    std::array<int32_t, 3> lo { 0, 0, 0 };
    std::array<int32_t, 3> hi { 0, 0, 0 };
    uint32_t resident = 0u;
    for (const auto& entry : Entries)
    {
        if (!entry.has_value()) continue;
        if (resident == 0u)
        {
            lo = entry->TileCoord;
            hi = entry->TileCoord;
        }
        else
        {
            for (size_t a = 0u; a < 3u; ++a)
            {
                lo[a] = std::min(lo[a], entry->TileCoord[a]);
                hi[a] = std::max(hi[a], entry->TileCoord[a]);
            }
        }
        ++resident;
    }

    CellOrigin = { lo[0] * T, lo[1] * T, lo[2] * T };
    const uint32_t extX = static_cast<uint32_t>((hi[0] - lo[0] + 1) * T);
    const uint32_t extY = static_cast<uint32_t>((hi[1] - lo[1] + 1) * T);
    const uint32_t extZ = static_cast<uint32_t>((hi[2] - lo[2] + 1) * T);

    Layout.Width             = extX;
    Layout.Height            = extY;
    Layout.Depth             = extZ;
    Layout.CellCount         = resident * TileCells * TileCells * TileCells;
    Layout.ResidentTileCount = resident;
    Layout.BankSlotCount     = SlotCount;

    GridCentreMeters = {
        (static_cast<float>(CellOrigin[0]) + static_cast<float>(extX) * 0.5f) * CellSizeMeters,
        (static_cast<float>(CellOrigin[1]) + static_cast<float>(extY) * 0.5f) * CellSizeMeters,
        (static_cast<float>(CellOrigin[2]) + static_cast<float>(extZ) * 0.5f) * CellSizeMeters
    };
}

SparseTileRelocationResult SparseTileBank::RelocateFixedWindow(
    const std::array<int32_t, 3>& shiftCells,
    const std::array<float, 3>& newCentreMeters) noexcept
{
    // Reference: FluidSolver.js L346-L363
    SparseTileRelocationResult res {};
    if (SparseMode) return res;
    GridCentreMeters = newCentreMeters;
    res.ShiftCells   = shiftCells;
    res.NewCentre    = newCentreMeters;
    if (shiftCells[0] == 0 && shiftCells[1] == 0 && shiftCells[2] == 0) return res;

    res.Moved = true;
    res.ClearWhole = (std::abs(shiftCells[0]) >= static_cast<int32_t>(Layout.Width))
                  || (std::abs(shiftCells[1]) >= static_cast<int32_t>(Layout.Height))
                  || (std::abs(shiftCells[2]) >= static_cast<int32_t>(Layout.Depth));
    ++RelocationCount;
    return res;
}

FluidVolumeUniformBlock SparseTileBank::BuildVolumeUniforms(
    const FluidBoundaryOrdinal boundary,
    const std::span<const FluidColliderSphere> colliders) const noexcept
{
    FluidVolumeUniformBlock u {};
    const FluidBoundaryOrdinal effectiveBoundary = SparseMode ? FluidBoundaryOrdinal::InfiniteFloor : boundary;
    u.GridBoundary = {
        static_cast<int32_t>(Layout.Width),
        static_cast<int32_t>(Layout.Height),
        static_cast<int32_t>(Layout.Depth),
        static_cast<int32_t>(effectiveBoundary)
    };
    u.GridCentreCellSize = {
        GridCentreMeters[0],
        GridCentreMeters[1],
        GridCentreMeters[2],
        CellSizeMeters
    };
    u.CellOriginSparse = {
        CellOrigin[0],
        CellOrigin[1],
        CellOrigin[2],
        SparseMode ? 1 : 0
    };
    u.AtlasTilesTexSize = {
        static_cast<int32_t>(Layout.TilesX),
        static_cast<int32_t>(Layout.TilesY),
        static_cast<int32_t>(Layout.TextureWidth),
        static_cast<int32_t>(Layout.TextureHeight)
    };
    u.SparseLayout = {
        static_cast<int32_t>(TileCells),
        static_cast<int32_t>(SlotsX),
        static_cast<int32_t>(SliceCols),
        static_cast<int32_t>(SliceRows)
    };
    const uint32_t count = static_cast<uint32_t>(std::min<size_t>(colliders.size(), kMaximumFluidColliders));
    u.LookupOriginCount = {
        LookupOrigin[0],
        LookupOrigin[1],
        LookupOrigin[2],
        static_cast<int32_t>(count)
    };
    for (uint32_t i = 0u; i < count; ++i)
    {
        u.Colliders[i] = {
            colliders[i].CentreMeters[0],
            colliders[i].CentreMeters[1],
            colliders[i].CentreMeters[2],
            colliders[i].RadiusMeters
        };
    }
    return u;
}

} // namespace Frontier::SimulationSpace::Fluid
