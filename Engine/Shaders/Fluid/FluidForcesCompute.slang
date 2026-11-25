/*============================================================================================================================================
                                                        FLUIDFORCESCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Stage D (Slice S15) 1:1 buoyancy, gravity, collider solid masking & velocity-saturated Phase-1 ForceModelEvaluation on 3D fluid volume
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js L684-L736 (ForcesFragment)

import ForceFieldNoise;
import ForceModelEvaluation;
import FluidVolumeAccess;

//------------------------------------------------------------------------------------------------------------------------
//                                                    UNIFORM BLOCK & BINDINGS
//------------------------------------------------------------------------------------------------------------------------

/// Matches C++ FluidForcesUniformBlock byte-for-byte (192 bytes, 16-byte aligned).
public struct FluidForcesUniformBlock
{
    public FluidVolumeUniformBlock Volume;
    public float4                  ForceKinematics; // x = deltaTime (s), y = buoyancy, z = gravity, w = ambientTemperature
    public float4                  ForceControl;    // x = forceResponse (s), y = simulationTime (s), z = forceRecordCount, w = reserved
};

[[vk::binding(0, 0)]] ConstantBuffer<FluidForcesUniformBlock> uParams;
[[vk::binding(1, 0)]] Texture3D<float4>                       gVelocity;
[[vk::binding(2, 0)]] Texture3D<float4>                       gState;
[[vk::binding(3, 0)]] StructuredBuffer<ForceModelGpuRecord>   gForceRecords;
[[vk::binding(4, 0)]] [format("rgba16f")] RWTexture3D<float4>                     gOutput;

//------------------------------------------------------------------------------------------------------------------------
//                                                      COMPUTE ENTRY
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 4)]
void ComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int3 cell = int3(dispatchThreadId);
    if (!CellInsideDense(uParams.Volume, cell)) return;

    float3 world = WorldFromCell(uParams.Volume, float3(cell) + 0.5);
    if (SolidAtWorld(uParams.Volume, world))
    {
        gOutput[cell] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float3 velocity = FetchCellDense(gVelocity, uParams.Volume, cell).xyz;
    float4 state    = FetchCellDense(gState,    uParams.Volume, cell);

    float dt            = uParams.ForceKinematics.x;
    float buoyancy      = uParams.ForceKinematics.y;
    float gravity       = uParams.ForceKinematics.z;
    float ambientTemp   = uParams.ForceKinematics.w;
    float forceResponse = uParams.ForceControl.x;
    float simTime       = uParams.ForceControl.y;
    uint  forceCount    = uint(max(0, int(uParams.ForceControl.z + 0.5)));

    float temperature   = state.y - ambientTemp;
    float3 acceleration = float3(0.0, buoyancy * temperature - gravity * state.x, 0.0);
    if (forceCount > 0u)
    {
        acceleration += EvaluateSaturatedGasForceField(
            gForceRecords, forceCount, world, velocity, simTime, forceResponse);
    }

    gOutput[cell] = float4(velocity + acceleration * dt, 0.0);
}
