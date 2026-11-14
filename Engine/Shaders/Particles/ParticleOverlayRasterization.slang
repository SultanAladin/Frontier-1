/*============================================================================================================================================
                                                   PARTICLEOVERLAYRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 procedural tactical overlay carrier: radar sweep arm, concentric range rings, compass notches & acoustic sonar pulses

import ParticleBuffers;

struct OverlayVertexOut
{
    float4 ClipPosition : SV_Position;
    float2 PlaneCoord   : TEXCOORD0;
    float  TimeSec      : TEXCOORD1;
};

[[vk::binding(0, 1)]] ConstantBuffer<ParticleGlobalUniformGpu> GlobalParams;
[[vk::binding(1, 1)]] ConstantBuffer<ParticleRigUniformGpu>    RigParams;

[shader("vertex")]
OverlayVertexOut ParticleOverlayVertexEntry(uint VertexId : SV_VertexID)
{
    OverlayVertexOut Out;
    static const float2 PlaneQuad[6] = {
        float2(-1.0, -1.0), float2( 1.0, -1.0), float2( 1.0,  1.0),
        float2(-1.0, -1.0), float2( 1.0,  1.0), float2(-1.0,  1.0)
    };
    float2 P = PlaneQuad[VertexId % 6u];
    float Radius = max(2.0, RigParams.ManifoldRadius * 2.0);
    float3 WPos = RigParams.OriginCenter.xyz + float3(P.x * Radius, 0.02, P.y * Radius);

    Out.ClipPosition = mul(GlobalParams.ViewProjectionMatrix, float4(WPos, 1.0));
    Out.PlaneCoord   = P;
    Out.TimeSec      = GlobalParams.ElapsedSeconds;
    return Out;
}

[shader("fragment")]
float4 ParticleOverlayFragmentEntry(OverlayVertexOut Input) : SV_Target
{
    float R = length(Input.PlaneCoord);
    if (R > 1.0) discard;

    float Angle = atan2(Input.PlaneCoord.y, Input.PlaneCoord.x);
    float SweepAngle = frac(Input.TimeSec * 0.35) * 6.2831853 - 3.14159265;
    float DeltaAngle = frac((SweepAngle - Angle) / 6.2831853);
    float SweepGlow  = pow(saturate(1.0 - DeltaAngle * 3.8), 2.6);

    float Rings = smoothstep(0.025, 0.0, abs(frac(R * 4.0) - 0.5) - 0.46);
    float Cross = smoothstep(0.009, 0.0, min(abs(Input.PlaneCoord.x), abs(Input.PlaneCoord.y)));
    float Rim   = smoothstep(0.02, 0.0, abs(R - 0.985));

    float Intensity = saturate(Rings * 0.45 + Cross * 0.32 + Rim * 0.85 + SweepGlow * 0.72);
    float3 Phosphor = lerp(float3(0.08, 0.86, 0.62), float3(0.55, 1.0, 0.88), SweepGlow);
    return float4(Phosphor * Intensity, Intensity * 0.92);
}
