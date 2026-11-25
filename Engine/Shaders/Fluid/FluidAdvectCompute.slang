/*============================================================================================================================================
                                                        FLUIDADVECTCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Stage D (Slice S15) 1:1 RK2 forward (+1) / reverse (-1) semi-Lagrangian advection on native 3D RGBA16F volumes
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js L532-L553 (AdvectFragment)

import FluidVolumeAccess;

//------------------------------------------------------------------------------------------------------------------------
//                                                    UNIFORM BLOCK & BINDINGS
//------------------------------------------------------------------------------------------------------------------------

/// Matches C++ FluidAdvectUniformBlock byte-for-byte (176 bytes, 16-byte aligned).
public struct FluidAdvectUniformBlock
{
    public FluidVolumeUniformBlock Volume;
    public float4                  AdvectParams; // x = deltaTime (s), y = dissipation, z = direction (+1 forward, -1 reverse), w = reserved
};

[[vk::binding(0, 0)]] ConstantBuffer<FluidAdvectUniformBlock> uParams;
[[vk::binding(1, 0)]] Texture3D<float4>                       gVelocity;
[[vk::binding(2, 0)]] Texture3D<float4>                       gSource;
[[vk::binding(3, 0)]] [format("rgba16f")] RWTexture3D<float4>                     gOutput;
[[vk::binding(4, 0)]] SamplerState                            gLinearClamp;

//------------------------------------------------------------------------------------------------------------------------
//                                                      COMPUTE ENTRY
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 4)]
void ComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int3 cell = int3(dispatchThreadId);
    if (!CellInsideDense(uParams.Volume, cell)) return;

    float dt          = uParams.AdvectParams.x;
    float dissipation = uParams.AdvectParams.y;
    float direction   = uParams.AdvectParams.z;
    float cellSize    = max(uParams.Volume.GridCentreCellSize.w, 1.0e-5);

    float3 centre      = float3(cell) + 0.5;
    float3 velocity    = FetchCellDense(gVelocity, uParams.Volume, cell).xyz;
    float3 midpoint    = centre - direction * 0.5 * dt * velocity / cellSize;
    float3 midVelocity = SampleVolumeDense(gVelocity, gLinearClamp, uParams.Volume, midpoint).xyz;
    float3 origin      = centre - direction * dt * midVelocity / cellSize;

    gOutput[cell] = SampleVolumeDense(gSource, gLinearClamp, uParams.Volume, origin) * dissipation;
}
