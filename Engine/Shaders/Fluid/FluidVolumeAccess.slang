/*============================================================================================================================================
                                                        FLUIDVOLUMEACCESS.SLANG
============================================================================================================================================*/
// 🧩 Stage D (Slice S14) 1:1 shared Slang module for Fixed/WorldGrid dense 3D volumes, Sparse Infinite 64x16x64 tiles, Colliders & Boundaries
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js L22-L233 (VolumeChunk & SparseVolumeChunk), L494-L531 (ColliderChunk & BoundaryChunk)

module FluidVolumeAccess;

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS & UNIFORM BLOCK
//------------------------------------------------------------------------------------------------------------------------

public static const int3 kLookupSize        = int3(64, 16, 64);
public static const int3 kSparseOutsideCell = int3(-1000000, -1000000, -1000000);
public static const int  kMaxFluidColliders = 4;

/// Matches C++ FluidVolumeUniformBlock byte-for-byte (160 bytes, 16-byte aligned).
public struct FluidVolumeUniformBlock
{
    public int4   GridBoundary;       // xyz = grid extent in cells, w = boundaryMode (0=ClosedBox, 1=OpenTop, 2=OpenAll, 3=InfiniteFloor)
    public float4 GridCentreCellSize; // xyz = world centre (m), w = cellSize (m)
    public int4   CellOriginSparse;   // xyz = world cell index of (0,0,0), w = isSparse (0/1)
    public int4   AtlasTilesTexSize;  // xy = slice tile counts, zw = atlas texture dimensions
    public int4   SparseLayout;       // x = tileCells (T), y = slotsX, z = sliceCols, w = sliceRows
    public int4   LookupOriginCount;  // xyz = tile coord of lookup (0,0,0), w = colliderCount (0..4)
    public float4 Colliders[4];       // xyz = sphere centre (m), w = radius (m)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 WORLD <-> CELL TRANSFORMS
//------------------------------------------------------------------------------------------------------------------------

public float3 GridHalf(FluidVolumeUniformBlock vol)
{
    return float3(vol.GridBoundary.xyz) * vol.GridCentreCellSize.w * 0.5;
}

public float3 WorldFromCell(FluidVolumeUniformBlock vol, float3 cellCentre)
{
    if (vol.CellOriginSparse.w != 0)
    {
        return (cellCentre + float3(vol.CellOriginSparse.xyz)) * vol.GridCentreCellSize.w;
    }
    return (cellCentre - float3(vol.GridBoundary.xyz) * 0.5) * vol.GridCentreCellSize.w + vol.GridCentreCellSize.xyz;
}

public float3 CellFromWorld(FluidVolumeUniformBlock vol, float3 world)
{
    if (vol.CellOriginSparse.w != 0)
    {
        return world / vol.GridCentreCellSize.w - float3(vol.CellOriginSparse.xyz);
    }
    return (world - vol.GridCentreCellSize.xyz) / vol.GridCentreCellSize.w + float3(vol.GridBoundary.xyz) * 0.5;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             DENSE 3D VOLUME ACCESS (FIXED / WORLDGRID)
//------------------------------------------------------------------------------------------------------------------------

public bool CellInsideDense(FluidVolumeUniformBlock vol, int3 cell)
{
    return all(cell >= int3(0, 0, 0)) && all(cell < vol.GridBoundary.xyz);
}

public float4 FetchCellDense(Texture3D<float4> field, FluidVolumeUniformBlock vol, int3 cell)
{
    int3 clamped = clamp(cell, int3(0, 0, 0), vol.GridBoundary.xyz - int3(1, 1, 1));
    return field.Load(int4(clamped, 0));
}

/// Hardware trilinear sample on native VK_IMAGE_TYPE_3D RGBA16F volume; coordinate in cell units (cell i centre at i + 0.5).
public float4 SampleVolumeDense(Texture3D<float4> field, SamplerState linearClamp, FluidVolumeUniformBlock vol, float3 coordinate)
{
    float3 uvw = clamp(coordinate, float3(0.5, 0.5, 0.5), float3(vol.GridBoundary.xyz) - float3(0.5, 0.5, 0.5))
               / max(float3(vol.GridBoundary.xyz), float3(1.0, 1.0, 1.0));
    return field.SampleLevel(linearClamp, uvw, 0.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                             SPARSE INFINITE TILE ADDRESSING (L98-L233)
//------------------------------------------------------------------------------------------------------------------------

public int3 TileOfWorldCell(FluidVolumeUniformBlock vol, int3 worldCell)
{
    return int3(floor(float3(worldCell) / float(vol.SparseLayout.x)));
}

public int SlotOfTile(Texture2D<int> tileLookup, FluidVolumeUniformBlock vol, int3 tile)
{
    int3 r = tile - vol.LookupOriginCount.xyz;
    if (any(r < int3(0, 0, 0)) || any(r >= kLookupSize)) return -1;
    return tileLookup.Load(int3(r.x, r.z + kLookupSize.z * r.y, 0));
}

public int2 SlotBlock(FluidVolumeUniformBlock vol, int slot)
{
    int slotsX = max(1, vol.SparseLayout.y);
    int T      = vol.SparseLayout.x;
    return int2(slot - (slot / slotsX) * slotsX, slot / slotsX) * int2(T * vol.SparseLayout.z, T * vol.SparseLayout.w);
}

public int2 PixelFromWorldCell(FluidVolumeUniformBlock vol, int3 worldCell, int3 tile, int slot)
{
    int T     = vol.SparseLayout.x;
    int sCols = max(1, vol.SparseLayout.z);
    int3 local = worldCell - tile * T;
    int sx = local.z - (local.z / sCols) * sCols;
    int sy = local.z / sCols;
    return SlotBlock(vol, slot) + int2(sx * T + local.x, sy * T + local.y);
}

public int3 CellFromSparseAtlasPixel(Texture2D<int4> slotTable, FluidVolumeUniformBlock vol, int2 pixel)
{
    int T      = vol.SparseLayout.x;
    int sCols  = max(1, vol.SparseLayout.z);
    int blockW = T * sCols;
    int blockH = T * vol.SparseLayout.w;
    int slot   = (pixel.x / blockW) + (pixel.y / blockH) * vol.SparseLayout.y;
    int4 entry = slotTable.Load(int3(slot, 0, 0));
    if (entry.w == 0) return kSparseOutsideCell;
    int bx = pixel.x - (pixel.x / blockW) * blockW;
    int by = pixel.y - (pixel.y / blockH) * blockH;
    int slice = (by / T) * sCols + (bx / T);
    if (slice >= T) return kSparseOutsideCell;
    int3 local = int3(bx - (bx / T) * T, by - (by / T) * T, slice);
    return entry.xyz * T + local - vol.CellOriginSparse.xyz;
}

public bool CellInsideSparse(Texture2D<int> tileLookup, FluidVolumeUniformBlock vol, int3 cell)
{
    if (cell.x <= kSparseOutsideCell.x) return false;
    return SlotOfTile(tileLookup, vol, TileOfWorldCell(vol, cell + vol.CellOriginSparse.xyz)) >= 0;
}

public float4 FetchCellSparse(Texture2D<float4> atlasField, Texture2D<int> tileLookup, FluidVolumeUniformBlock vol, int3 cell)
{
    int3 worldCell = cell + vol.CellOriginSparse.xyz;
    int3 tile = TileOfWorldCell(vol, worldCell);
    int slot = SlotOfTile(tileLookup, vol, tile);
    if (slot < 0) return float4(0.0, 0.0, 0.0, 0.0);
    return atlasField.Load(int3(PixelFromWorldCell(vol, worldCell, tile, slot), 0));
}

/// Trilinear sample in cell units; fast path uses 2 hardware-bilinear slice taps inside 1 tile, fallback fetches 8 corners across tile seams.
public float4 SampleVolumeSparse(
    Texture2D<float4> atlasField,
    SamplerState linearClamp,
    Texture2D<int> tileLookup,
    FluidVolumeUniformBlock vol,
    float3 coordinate)
{
    float3 p    = coordinate - 0.5;
    float3 base = floor(p);
    float3 f    = p - base;
    int3 c0     = int3(base) + vol.CellOriginSparse.xyz;
    int3 t0     = TileOfWorldCell(vol, c0);
    int3 t1     = TileOfWorldCell(vol, c0 + int3(1, 1, 1));
    if (all(t0 == t1))
    {
        int slot = SlotOfTile(tileLookup, vol, t0);
        if (slot < 0) return float4(0.0, 0.0, 0.0, 0.0);
        int T     = vol.SparseLayout.x;
        int sCols = max(1, vol.SparseLayout.z);
        int3 local = c0 - t0 * T;
        int2 block = SlotBlock(vol, slot);
        int zA = local.z;
        int zB = local.z + 1;
        float2 inner = float2(local.xy) + f.xy + 0.5;
        int2 sliceA = int2(zA - (zA / sCols) * sCols, zA / sCols);
        int2 sliceB = int2(zB - (zB / sCols) * sCols, zB / sCols);
        float2 texSize = max(float2(vol.AtlasTilesTexSize.zw), float2(1.0, 1.0));
        float2 uvA = (float2(block + sliceA * T) + inner) / texSize;
        float2 uvB = (float2(block + sliceB * T) + inner) / texSize;
        return lerp(atlasField.SampleLevel(linearClamp, uvA, 0.0), atlasField.SampleLevel(linearClamp, uvB, 0.0), f.z);
    }

    int3 b = int3(base);
    float4 x00 = lerp(FetchCellSparse(atlasField, tileLookup, vol, b),                     FetchCellSparse(atlasField, tileLookup, vol, b + int3(1, 0, 0)), f.x);
    float4 x10 = lerp(FetchCellSparse(atlasField, tileLookup, vol, b + int3(0, 1, 0)),     FetchCellSparse(atlasField, tileLookup, vol, b + int3(1, 1, 0)), f.x);
    float4 x01 = lerp(FetchCellSparse(atlasField, tileLookup, vol, b + int3(0, 0, 1)),     FetchCellSparse(atlasField, tileLookup, vol, b + int3(1, 0, 1)), f.x);
    float4 x11 = lerp(FetchCellSparse(atlasField, tileLookup, vol, b + int3(0, 1, 1)),     FetchCellSparse(atlasField, tileLookup, vol, b + int3(1, 1, 1)), f.x);
    return lerp(lerp(x00, x10, f.y), lerp(x01, x11, f.y), f.z);
}

//------------------------------------------------------------------------------------------------------------------------
//                                        COLLIDER & BOUNDARY HELPERS (L494-L531)
//------------------------------------------------------------------------------------------------------------------------

public bool SolidAtWorld(FluidVolumeUniformBlock vol, float3 world)
{
    int count = min(vol.LookupOriginCount.w, kMaxFluidColliders);
    for (int i = 0; i < kMaxFluidColliders; ++i)
    {
        if (i >= count) break;
        if (distance(world, vol.Colliders[i].xyz) < vol.Colliders[i].w) return true;
    }
    return false;
}

public bool SolidAtCell(FluidVolumeUniformBlock vol, int3 cell)
{
    return SolidAtWorld(vol, WorldFromCell(vol, float3(cell) + 0.5));
}

public bool OpenFace(FluidVolumeUniformBlock vol, int3 neighbour)
{
    int mode = vol.GridBoundary.w;
    if (mode == 3) return (neighbour.y + vol.CellOriginSparse.y) >= 0;
    if (mode == 2) return true;
    if (mode == 1 && neighbour.y >= vol.GridBoundary.y) return true;
    return false;
}
