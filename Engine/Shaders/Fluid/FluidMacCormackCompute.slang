/*============================================================================================================================================
                                                      FLUIDMACCORMACKCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Stage D (Slice S15) 1:1 Selle et al. (2008) overshoot-fallback MacCormack advection with Courant [1..2] stability blend
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js L554-L596 (MacCormackFragment)

import FluidVolumeAccess;

//------------------------------------------------------------------------------------------------------------------------
//                                                    UNIFORM BLOCK & BINDINGS
//------------------------------------------------------------------------------------------------------------------------

/// Matches C++ FluidMacCormackUniformBlock byte-for-byte (176 bytes, 16-byte aligned).
public struct FluidMacCormackUniformBlock
{
    public FluidVolumeUniformBlock Volume;
    public float4                  MacCormackParams; // x = deltaTime (s), y = dissipation, z = reserved, w = reserved
};

[[vk::binding(0, 0)]] ConstantBuffer<FluidMacCormackUniformBlock> uParams;
[[vk::binding(1, 0)]] Texture3D<float4>                           gVelocity;
[[vk::binding(2, 0)]] Texture3D<float4>                           gSource;   // phi_n
[[vk::binding(3, 0)]] Texture3D<float4>                           gForward;  // phi_hat_{n+1}
[[vk::binding(4, 0)]] Texture3D<float4>                           gBackward; // phi_hat_n
[[vk::binding(5, 0)]] [format("rgba16f")] RWTexture3D<float4>                         gOutput;
[[vk::binding(6, 0)]] SamplerState                                gLinearClamp;

//------------------------------------------------------------------------------------------------------------------------
//                                                      COMPUTE ENTRY
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 4)]
void ComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int3 cell = int3(dispatchThreadId);
    if (!CellInsideDense(uParams.Volume, cell)) return;

    float dt          = uParams.MacCormackParams.x;
    float dissipation = uParams.MacCormackParams.y;
    float cellSize    = max(uParams.Volume.GridCentreCellSize.w, 1.0e-5);

    float3 centre   = float3(cell) + 0.5;
    float3 velocity = FetchCellDense(gVelocity, uParams.Volume, cell).xyz;
    float3 midpoint = centre - 0.5 * dt * velocity / cellSize;
    float3 origin   = centre - dt * SampleVolumeDense(gVelocity, gLinearClamp, uParams.Volume, midpoint).xyz / cellSize;

    float4 forward   = FetchCellDense(gForward,  uParams.Volume, cell);
    float4 backward  = FetchCellDense(gBackward, uParams.Volume, cell);
    float4 source    = FetchCellDense(gSource,   uParams.Volume, cell);
    float4 corrected = forward + 0.5 * (source * dissipation - backward);

    float3 gridMax = float3(uParams.Volume.GridBoundary.xyz) - float3(1.0, 1.0, 1.0);
    int3 base = int3(floor(clamp(origin - 0.5, float3(0.0, 0.0, 0.0), gridMax)));
    float4 lo = float4( 1.0e9,  1.0e9,  1.0e9,  1.0e9);
    float4 hi = float4(-1.0e9, -1.0e9, -1.0e9, -1.0e9);

    for (int z = 0; z < 2; ++z)
    {
        for (int y = 0; y < 2; ++y)
        {
            for (int x = 0; x < 2; ++x)
            {
                float4 s = FetchCellDense(gSource, uParams.Volume, base + int3(x, y, z)) * dissipation;
                lo = min(lo, s);
                hi = max(hi, s);
            }
        }
    }

    // Selle et al. (2008): fall back to forward semi-Lagrangian on stencil overshoot; blend out for Courant > 1.
    bool overshoot = any(corrected < lo) || any(corrected > hi);
    float courant  = length(velocity) * dt / cellSize;
    float4 result  = overshoot ? forward : lerp(forward, corrected, 1.0 - smoothstep(1.0, 2.0, courant));
    gOutput[cell]  = clamp(result, lo, hi);
}
