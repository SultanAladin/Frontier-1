/*============================================================================================================================================
                                                    FLUIDDIVERGENCEPRESSURECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Stage D (Slice S16) 1:1 3D Fluid velocity divergence + flame light source and warm-started Jacobi pressure + screened fire-light diffusion
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js DivergenceFragment (L780-L819) & PressureFragment (L820-L859)

import FluidVolumeAccess;

public struct FluidDivergenceUniformBlock
{
    public FluidVolumeUniformBlock Volume;
    public float4                  DivergenceControl; // (expansion, fireThreshold, fireSoftness, 0)
};

public struct FluidPressureUniformBlock
{
    public FluidVolumeUniformBlock Volume;
    public float4                  PressureControl; // (retain, 0, 0, 0)
};

[[vk::binding(0, 0)]] ConstantBuffer<FluidDivergenceUniformBlock> uDivergence;
[[vk::binding(1, 0)]] ConstantBuffer<FluidPressureUniformBlock>   uPressure;
[[vk::binding(2, 0)]] Texture3D<float4>                           gVelocity;
[[vk::binding(3, 0)]] Texture3D<float4>                           gState;
[[vk::binding(4, 0)]] Texture3D<float4>                           gPressure;
[[vk::binding(5, 0)]] Texture3D<float4>                           gDivergence;
[[vk::binding(6, 0)]] [format("rgba16f")] RWTexture3D<float4>                         gOutput;

float SampleVelocityAxisComponent(
    FluidVolumeUniformBlock vol,
    int3 neighbour,
    int axis,
    float centreComponent)
{
    if (!CellInsideDense(vol, neighbour))
    {
        return OpenFace(vol, neighbour) ? centreComponent : 0.0;
    }
    if (SolidAtCell(vol, neighbour))
    {
        return 0.0;
    }
    float3 v = FetchCellDense(gVelocity, vol, neighbour).xyz;
    return (axis == 0) ? v.x : ((axis == 1) ? v.y : v.z);
}

[shader("compute")]
[numthreads(8, 8, 4)]
void DivergenceComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int3 cell = int3(dispatchThreadId);
    FluidVolumeUniformBlock vol = uDivergence.Volume;
    if (!CellInsideDense(vol, cell)) return;

    float3 c = FetchCellDense(gVelocity, vol, cell).xyz;
    float xp = SampleVelocityAxisComponent(vol, cell + int3(1, 0, 0), 0, c.x);
    float xm = SampleVelocityAxisComponent(vol, cell - int3(1, 0, 0), 0, c.x);
    float yp = SampleVelocityAxisComponent(vol, cell + int3(0, 1, 0), 1, c.y);
    float ym = SampleVelocityAxisComponent(vol, cell - int3(0, 1, 0), 1, c.y);
    float zp = SampleVelocityAxisComponent(vol, cell + int3(0, 0, 1), 2, c.z);
    float zm = SampleVelocityAxisComponent(vol, cell - int3(0, 0, 1), 2, c.z);

    float cellSize      = max(vol.GridCentreCellSize.w, 1.0e-5);
    float expansion     = uDivergence.DivergenceControl.x;
    float fireThreshold = uDivergence.DivergenceControl.y;
    float fireSoftness  = uDivergence.DivergenceControl.z;

    float divergence = 0.5 * ((xp - xm) + (yp - ym) + (zp - zm)) / cellSize;
    float4 state = FetchCellDense(gState, vol, cell);
    divergence -= expansion * 0.08 * state.w;

    float flame    = saturate((state.y - fireThreshold) / max(fireSoftness, 0.05));
    float presence = smoothstep(fireThreshold * 0.7, fireThreshold * 1.15, state.y);
    float lightSrc = min(8.0, presence * (0.2 + 4.0 * flame * flame));

    gOutput[cell] = float4(divergence, lightSrc, 0.0, 0.0);
}

float SamplePressureNeighbour(FluidVolumeUniformBlock vol, int3 neighbour, float centre)
{
    if (!CellInsideDense(vol, neighbour))
    {
        return OpenFace(vol, neighbour) ? 0.0 : centre;
    }
    if (SolidAtCell(vol, neighbour))
    {
        return centre;
    }
    return FetchCellDense(gPressure, vol, neighbour).x;
}

float SampleLightNeighbour(FluidVolumeUniformBlock vol, int3 neighbour, float centre)
{
    if (!CellInsideDense(vol, neighbour))
    {
        return 0.0;
    }
    if (SolidAtCell(vol, neighbour))
    {
        return centre;
    }
    return FetchCellDense(gPressure, vol, neighbour).y;
}

[shader("compute")]
[numthreads(8, 8, 4)]
void PressureComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int3 cell = int3(dispatchThreadId);
    FluidVolumeUniformBlock vol = uPressure.Volume;
    if (!CellInsideDense(vol, cell)) return;

    float2 centre = FetchCellDense(gPressure, vol, cell).xy;
    float2 source = FetchCellDense(gDivergence, vol, cell).xy;

    float sum = SamplePressureNeighbour(vol, cell + int3(1, 0, 0), centre.x)
              + SamplePressureNeighbour(vol, cell - int3(1, 0, 0), centre.x)
              + SamplePressureNeighbour(vol, cell + int3(0, 1, 0), centre.x)
              + SamplePressureNeighbour(vol, cell - int3(0, 1, 0), centre.x)
              + SamplePressureNeighbour(vol, cell + int3(0, 0, 1), centre.x)
              + SamplePressureNeighbour(vol, cell - int3(0, 0, 1), centre.x);

    float light = SampleLightNeighbour(vol, cell + int3(1, 0, 0), centre.y)
                + SampleLightNeighbour(vol, cell - int3(1, 0, 0), centre.y)
                + SampleLightNeighbour(vol, cell + int3(0, 1, 0), centre.y)
                + SampleLightNeighbour(vol, cell - int3(0, 1, 0), centre.y)
                + SampleLightNeighbour(vol, cell + int3(0, 0, 1), centre.y)
                + SampleLightNeighbour(vol, cell - int3(0, 0, 1), centre.y);

    float h2     = vol.GridCentreCellSize.w * vol.GridCentreCellSize.w;
    float retain = uPressure.PressureControl.x;

    gOutput[cell] = float4(
        (sum * retain - source.x * h2) / 6.0,
        (light + source.y * 0.55) / 6.28,
        0.0,
        0.0);
}
