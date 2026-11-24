/*============================================================================================================================================
                                                      FLUIDGRADIENTSHIFTCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Stage D (Slice S16) 1:1 3D Fluid pressure-gradient projection, WorldGrid whole-cell shift relocation, and Infinite sparse occupancy scan
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js GradientFragment (L860-L895), ShiftFragment (L1000-L1018), OccupancyFragment (L1019-L1076)

import FluidVolumeAccess;

public struct FluidGradientShiftUniformBlock
{
    public FluidVolumeUniformBlock Volume;
    public int4                    ShiftDelta;       // (dx, dy, dz, 0)
    public int4                    OccupancyControl; // (stride, band, slotCount, 0)
};

[[vk::binding(0, 0)]] ConstantBuffer<FluidGradientShiftUniformBlock> uParams;
[[vk::binding(1, 0)]] Texture3D<float4>                              gPressureOrSource;
[[vk::binding(2, 0)]] Texture3D<float4>                              gVelocity;
[[vk::binding(3, 0)]] Texture2D<float4>                              gSparseStateAtlas;
[[vk::binding(4, 0)]] Texture2D<float4>                              gSparseVelocityAtlas;
[[vk::binding(5, 0)]] Texture2D<int4>                                gSparseSlotTable;
[[vk::binding(6, 0)]] [format("rgba16f")] RWTexture3D<float4>                            gOutput;
[[vk::binding(7, 0)]] RWStructuredBuffer<float4>                     gOccupancyReadback; // 3 * slotCount float4 entries

float SampleGradientPressureNeighbour(FluidVolumeUniformBlock vol, int3 neighbour, float centre)
{
    if (!CellInsideDense(vol, neighbour))
    {
        return OpenFace(vol, neighbour) ? 0.0 : centre;
    }
    if (SolidAtCell(vol, neighbour))
    {
        return centre;
    }
    return FetchCellDense(gPressureOrSource, vol, neighbour).x;
}

[shader("compute")]
[numthreads(8, 8, 4)]
void GradientComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int3 cell = int3(dispatchThreadId);
    FluidVolumeUniformBlock vol = uParams.Volume;
    if (!CellInsideDense(vol, cell)) return;
    if (SolidAtCell(vol, cell))
    {
        gOutput[cell] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float centre = FetchCellDense(gPressureOrSource, vol, cell).x;
    float invTwoH = 0.5 / max(vol.GridCentreCellSize.w, 1.0e-5);
    float3 gradient = float3(
        SampleGradientPressureNeighbour(vol, cell + int3(1, 0, 0), centre) - SampleGradientPressureNeighbour(vol, cell - int3(1, 0, 0), centre),
        SampleGradientPressureNeighbour(vol, cell + int3(0, 1, 0), centre) - SampleGradientPressureNeighbour(vol, cell - int3(0, 1, 0), centre),
        SampleGradientPressureNeighbour(vol, cell + int3(0, 0, 1), centre) - SampleGradientPressureNeighbour(vol, cell - int3(0, 0, 1), centre)) * invTwoH;

    float3 velocity = FetchCellDense(gVelocity, vol, cell).xyz - gradient;

    if (!CellInsideDense(vol, cell - int3(1, 0, 0)) && !OpenFace(vol, cell - int3(1, 0, 0))) velocity.x = max(velocity.x, 0.0);
    if (!CellInsideDense(vol, cell + int3(1, 0, 0)) && !OpenFace(vol, cell + int3(1, 0, 0))) velocity.x = min(velocity.x, 0.0);
    if (!CellInsideDense(vol, cell - int3(0, 1, 0)) && !OpenFace(vol, cell - int3(0, 1, 0))) velocity.y = max(velocity.y, 0.0);
    if (!CellInsideDense(vol, cell + int3(0, 1, 0)) && !OpenFace(vol, cell + int3(0, 1, 0))) velocity.y = min(velocity.y, 0.0);
    if (!CellInsideDense(vol, cell - int3(0, 0, 1)) && !OpenFace(vol, cell - int3(0, 0, 1))) velocity.z = max(velocity.z, 0.0);
    if (!CellInsideDense(vol, cell + int3(0, 0, 1)) && !OpenFace(vol, cell + int3(0, 0, 1))) velocity.z = min(velocity.z, 0.0);

    gOutput[cell] = float4(velocity, 0.0);
}

[shader("compute")]
[numthreads(8, 8, 4)]
void ShiftComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int3 cell = int3(dispatchThreadId);
    FluidVolumeUniformBlock vol = uParams.Volume;
    if (!CellInsideDense(vol, cell)) return;

    int3 source = cell + uParams.ShiftDelta.xyz;
    gOutput[cell] = CellInsideDense(vol, source)
        ? FetchCellDense(gPressureOrSource, vol, source)
        : float4(0.0, 0.0, 0.0, 0.0);
}

[shader("compute")]
[numthreads(3, 8, 1)]
void OccupancyComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int column    = int(dispatchThreadId.x);
    int slot      = int(dispatchThreadId.y);
    int slotCount = uParams.OccupancyControl.z;
    if (column < 0 || column >= 3 || slot < 0 || slot >= slotCount)
    {
        return;
    }

    int4 entry = gSparseSlotTable.Load(int3(slot, 0, 0));
    int outIndex = slot * 3 + column;
    if (entry.w == 0)
    {
        gOccupancyReadback[outIndex] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    int tileCells = uParams.Volume.SparseLayout.x;
    int sliceCols = max(1, uParams.Volume.SparseLayout.z);
    int stride    = max(1, uParams.OccupancyControl.x);
    int band      = max(1, uParams.OccupancyControl.y);
    int2 block    = SlotBlock(uParams.Volume, slot);
    float4 result = float4(0.0, 0.0, 0.0, 0.0);

    for (int z = 0; z < 64; ++z)
    {
        int lz = z * stride;
        if (lz >= tileCells) break;
        int2 sliceOrigin = block + int2(lz - (lz / sliceCols) * sliceCols, lz / sliceCols) * tileCells;
        for (int y = 0; y < 64; ++y)
        {
            int ly = y * stride;
            if (ly >= tileCells) break;
            for (int x = 0; x < 64; ++x)
            {
                int lx = x * stride;
                if (lx >= tileCells) break;
                int2 pixel = sliceOrigin + int2(lx, ly);
                float density = gSparseStateAtlas.Load(int3(pixel, 0)).x;
                if (column == 0)
                {
                    result.x += density;
                    result.y = max(result.y, density);
                    result.z = max(result.z, length(gSparseVelocityAtlas.Load(int3(pixel, 0)).xyz));
                }
                else if (column == 1)
                {
                    if (lx < band) result.x += density;
                    if (lx >= tileCells - band) result.y += density;
                    if (ly < band) result.z += density;
                    if (ly >= tileCells - band) result.w += density;
                }
                else
                {
                    if (lz < band) result.x += density;
                    if (lz >= tileCells - band) result.y += density;
                }
            }
        }
    }

    if (column == 0)
    {
        result.w = 1.0;
    }
    gOccupancyReadback[outIndex] = result;
}
