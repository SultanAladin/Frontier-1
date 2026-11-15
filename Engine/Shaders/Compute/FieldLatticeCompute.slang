/*============================================================================================================================================
                                                          FIELDLATTICECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Runtime & Editor Vulkan 1.3 Compute shader evaluating cascaded 3D velocity + curl lattice voxels from ForceSourceShaderEntry SSBO

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Shaders/Compute/ForceFieldEvaluationShared.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                       RESOURCES
//------------------------------------------------------------------------------------------------------------------------

struct LatticePushConstants
{
    float4 FocusOriginHalfExtent; // xyz = FocusOrigin [m], w = WorldHalfExtent [m]
    uint   GridResolution;           // [count] - Voxels per axis (N x N x N)
    uint   SourceCount;              // [count] - Active ForceSource count
    float  TimeSeconds;              // [s]     - Elapsed simulation time
    float  ReservedPad;
};

[[vk::push_constant]]
ConstantBuffer<LatticePushConstants> Push;

[[vk::binding(0, 0)]]
StructuredBuffer<ForceSourceShaderEntry> SourceBuffer;

[[vk::binding(1, 0)]]
StructuredBuffer<float4> WaypointBuffer;

[[vk::binding(2, 0)]]
RWStructuredBuffer<float4> OutLatticeVoxels;

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(4, 4, 4)]
void ComputeFieldLatticeVoxel(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Res = Push.GridResolution;
    if (DispatchThreadID.x >= Res || DispatchThreadID.y >= Res || DispatchThreadID.z >= Res)
    {
        return;
    }

    float Div = float(max(1u, Res - 1u));
    float3 Normalized = (float3(DispatchThreadID) / Div) * 2.0 - 1.0;
    float3 WorldPos   = Push.FocusOriginHalfExtent.xyz + Normalized * Push.FocusOriginHalfExtent.w;

    float3 Combined = VfEvaluateCombinedVelocity(SourceBuffer, WaypointBuffer, Push.SourceCount, WorldPos, float3(0.0, 0.0, 0.0));

    float StepM   = 0.25;
    float InvStep = 1.0 / StepM;
    float3 VxP = VfEvaluateCombinedVelocity(SourceBuffer, WaypointBuffer, Push.SourceCount, WorldPos + float3(StepM, 0.0, 0.0), float3(0.0, 0.0, 0.0));
    float3 VyP = VfEvaluateCombinedVelocity(SourceBuffer, WaypointBuffer, Push.SourceCount, WorldPos + float3(0.0, StepM, 0.0), float3(0.0, 0.0, 0.0));
    float3 VzP = VfEvaluateCombinedVelocity(SourceBuffer, WaypointBuffer, Push.SourceCount, WorldPos + float3(0.0, 0.0, StepM), float3(0.0, 0.0, 0.0));

    float CurlX = ((VyP.z - Combined.z) - (VzP.y - Combined.y)) * InvStep;
    float CurlY = ((VzP.x - Combined.x) - (VxP.z - Combined.z)) * InvStep;
    float CurlZ = ((VxP.y - Combined.y) - (VyP.x - Combined.x)) * InvStep;
    float CurlM = sqrt(CurlX * CurlX + CurlY * CurlY + CurlZ * CurlZ);

    uint LinearIdx = (DispatchThreadID.z * Res + DispatchThreadID.y) * Res + DispatchThreadID.x;
    OutLatticeVoxels[LinearIdx] = float4(Combined.x, Combined.y, Combined.z, CurlM);
}
