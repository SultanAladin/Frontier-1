/*============================================================================================================================================
                                                      FLUIDTHERMODYNAMICSCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Stage D (Slice S16) 1:1 3D Fluid outflow sponge, fuel ignition/combustion, and Newtonian + radiative cooling compute kernel
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js ThermodynamicsFragment (L896-L950)

import FluidVolumeAccess;

public struct FluidThermodynamicsUniformBlock
{
    public FluidVolumeUniformBlock Volume;
    public float4                  ThermoControl; // (deltaTime, cooling, combustion, ambientTemperature)
};

[[vk::binding(0, 0)]] ConstantBuffer<FluidThermodynamicsUniformBlock> uParams;
[[vk::binding(1, 0)]] Texture3D<float4>                               gState;
[[vk::binding(2, 0)]] [format("rgba16f")] RWTexture3D<float4>                             gOutputState;

float EvaluateOutflowSponge(FluidVolumeUniformBlock vol, int3 cell)
{
    if (vol.GridBoundary.w == 3)
    {
        return 1.0;
    }

    int3 grid = vol.GridBoundary.xyz;
    float keep = 1.0;
    float band = max(3.0, float(max(grid.x, max(grid.y, grid.z))) * 0.07);

    if (OpenFace(vol, int3(cell.x, grid.y, cell.z))) keep *= smoothstep(0.0, band, float(grid.y - 1 - cell.y));
    if (OpenFace(vol, int3(cell.x, -1,     cell.z))) keep *= smoothstep(0.0, band, float(cell.y));
    if (OpenFace(vol, int3(grid.x, cell.y, cell.z))) keep *= smoothstep(0.0, band, float(grid.x - 1 - cell.x));
    if (OpenFace(vol, int3(-1,     cell.y, cell.z))) keep *= smoothstep(0.0, band, float(cell.x));
    if (OpenFace(vol, int3(cell.x, cell.y, grid.z))) keep *= smoothstep(0.0, band, float(grid.z - 1 - cell.z));
    if (OpenFace(vol, int3(cell.x, cell.y, -1    ))) keep *= smoothstep(0.0, band, float(cell.z));

    return lerp(0.82, 1.0, keep);
}

[shader("compute")]
[numthreads(8, 8, 4)]
void ComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int3 cell = int3(dispatchThreadId);
    if (!CellInsideDense(uParams.Volume, cell)) return;

    float deltaTime   = uParams.ThermoControl.x;
    float cooling     = uParams.ThermoControl.y;
    float combustion  = uParams.ThermoControl.z;
    float ambientTemp = uParams.ThermoControl.w;

    float4 state = FetchCellDense(gState, uParams.Volume, cell);
    float sponge = EvaluateOutflowSponge(uParams.Volume, cell);

    float density     = state.x * sponge;
    float temperature = ambientTemp + (state.y - ambientTemp) * sponge;
    float fuel        = state.z * sponge;

    const float ignition = 0.9;
    float reaction = 0.0;
    if (fuel > 0.0 && temperature > ignition && combustion > 0.0)
    {
        float burn = min(fuel, combustion * 2.5 * deltaTime * fuel * smoothstep(ignition, ignition + 1.0, temperature));
        reaction = burn / max(deltaTime, 1.0e-4);
        fuel -= burn;
        temperature += burn * 12.0;
        density += burn * 0.55;
    }

    float excess = temperature - ambientTemp;
    excess = max(0.0, excess - deltaTime * cooling * (0.35 * excess + 0.02 * excess * excess * excess));
    temperature = ambientTemp + excess;

    gOutputState[cell] = float4(max(density, 0.0), temperature, max(fuel, 0.0), reaction);
}
