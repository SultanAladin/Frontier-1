/*============================================================================================================================================
                                                        FLUIDCURLVORTICITYCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Stage D (Slice S16) 1:1 3D Fluid curl calculation & vorticity confinement compute kernels
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js CurlFragment (L737-L754) & VorticityFragment (L755-L779)

import FluidVolumeAccess;

public struct FluidVorticityUniformBlock
{
    public FluidVolumeUniformBlock Volume;
    public float4                  VorticityControl; // (deltaTime, confinement, 0, 0)
};

[[vk::binding(0, 0)]] ConstantBuffer<FluidVorticityUniformBlock> uParams;
[[vk::binding(1, 0)]] Texture3D<float4>                          gVelocity;
[[vk::binding(2, 0)]] Texture3D<float4>                          gCurl;
[[vk::binding(3, 0)]] [format("rgba16f")] RWTexture3D<float4>                        gOutput;

[shader("compute")]
[numthreads(8, 8, 4)]
void CurlComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int3 cell = int3(dispatchThreadId);
    if (!CellInsideDense(uParams.Volume, cell)) return;

    float3 xp = FetchCellDense(gVelocity, uParams.Volume, cell + int3(1, 0, 0)).xyz;
    float3 xm = FetchCellDense(gVelocity, uParams.Volume, cell - int3(1, 0, 0)).xyz;
    float3 yp = FetchCellDense(gVelocity, uParams.Volume, cell + int3(0, 1, 0)).xyz;
    float3 ym = FetchCellDense(gVelocity, uParams.Volume, cell - int3(0, 1, 0)).xyz;
    float3 zp = FetchCellDense(gVelocity, uParams.Volume, cell + int3(0, 0, 1)).xyz;
    float3 zm = FetchCellDense(gVelocity, uParams.Volume, cell - int3(0, 0, 1)).xyz;

    float invTwoH = 0.5 / max(uParams.Volume.GridCentreCellSize.w, 1.0e-5);
    float3 curl = float3(
        (yp.z - ym.z) - (zp.y - zm.y),
        (zp.x - zm.x) - (xp.z - xm.z),
        (xp.y - xm.y) - (yp.x - ym.x)) * invTwoH;

    gOutput[cell] = float4(curl, length(curl));
}

[shader("compute")]
[numthreads(8, 8, 4)]
void VorticityComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int3 cell = int3(dispatchThreadId);
    if (!CellInsideDense(uParams.Volume, cell)) return;

    float cellSize    = max(uParams.Volume.GridCentreCellSize.w, 1.0e-5);
    float deltaTime   = uParams.VorticityControl.x;
    float confinement = uParams.VorticityControl.y;

    float3 velocity = FetchCellDense(gVelocity, uParams.Volume, cell).xyz;
    float4 centre   = FetchCellDense(gCurl, uParams.Volume, cell);

    float3 eta = float3(
        FetchCellDense(gCurl, uParams.Volume, cell + int3(1, 0, 0)).w -
        FetchCellDense(gCurl, uParams.Volume, cell - int3(1, 0, 0)).w,
        FetchCellDense(gCurl, uParams.Volume, cell + int3(0, 1, 0)).w -
        FetchCellDense(gCurl, uParams.Volume, cell - int3(0, 1, 0)).w,
        FetchCellDense(gCurl, uParams.Volume, cell + int3(0, 0, 1)).w -
        FetchCellDense(gCurl, uParams.Volume, cell - int3(0, 0, 1)).w) * (0.5 / cellSize);

    float magnitude = length(eta);
    float3 normal = (magnitude > 1.0e-5) ? (eta / magnitude) : float3(0.0, 0.0, 0.0);
    float3 force = confinement * cellSize * cross(normal, centre.xyz);

    gOutput[cell] = float4(velocity + force * deltaTime, 0.0);
}
