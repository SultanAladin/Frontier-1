/*============================================================================================================================================
                                                  PARTICLESURFACEBACKINGRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 glossy obsidian / brushed metallic 3D substrate shader for MoGraph Effector Halftone & Ferrofluid liquid basins

import ParticleBuffers;

struct BackingVertexIn
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
};

struct BackingVertexOut
{
    float4 ClipPosition : SV_Position;
    float3 WorldPos     : TEXCOORD0;
    float3 WorldNormal  : NORMAL;
};

[[vk::binding(0, 1)]] ConstantBuffer<ParticleGlobalUniformGpu> GlobalParams;
[[vk::binding(1, 1)]] ConstantBuffer<ParticleRigUniformGpu>    RigParams;

[shader("vertex")]
BackingVertexOut ParticleSurfaceBackingVertexEntry(BackingVertexIn Input)
{
    BackingVertexOut Out;
    float3 WPos = Input.Position + RigParams.OriginCenter.xyz;
    Out.ClipPosition = mul(GlobalParams.ViewProjectionMatrix, float4(WPos, 1.0));
    Out.WorldPos     = WPos;
    Out.WorldNormal  = normalize(Input.Normal);
    return Out;
}

[shader("fragment")]
float4 ParticleSurfaceBackingFragmentEntry(BackingVertexOut Input) : SV_Target
{
    float3 N = normalize(Input.WorldNormal);
    float3 V = normalize(GlobalParams.CameraPositionMeters.xyz - Input.WorldPos);
    float3 R = reflect(-V, N);

    // Studio softbox overhead reflection strips matching ferrofluid & obsidian halftone look
    float SoftboxA = smoothstep(0.55, 0.88, R.y) * smoothstep(0.65, 0.15, abs(R.x));
    float SoftboxB = smoothstep(0.25, 0.72, dot(R, normalize(float3(-0.65, 0.55, 0.52)))) * 0.45;
    float Fresnel  = pow(1.0 - saturate(dot(N, V)), 4.5);

    float3 Obsidian = float3(0.022, 0.026, 0.036);
    float3 SpecCol  = float3(0.76, 0.85, 0.96) * (SoftboxA * 0.85 + SoftboxB + Fresnel * 0.42);
    return float4(Obsidian + SpecCol, 1.0);
}
