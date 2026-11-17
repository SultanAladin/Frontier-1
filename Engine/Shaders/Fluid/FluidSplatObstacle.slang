/*============================================================================================================================================
                                                         FLUIDSPLATOBSTACLE.SLANG
============================================================================================================================================*/
// 🧩 Continuous +Z emitter, multi-lobe Rayleigh-Taylor blast detonation, particle splats & analytical/GI solid obstacle injection

#include "FluidSolverLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<FluidSolverUniformBlock> FluidCfg;
[[vk::binding(1, 0)]] RWTexture3D<float4>                     VelocitySolidTex;
[[vk::binding(2, 0)]] RWTexture3D<float4>                     ThermoTex;
[[vk::binding(3, 0)]] Texture3D<float>                        SceneSolidMaskTex;

[shader("compute")]
[numthreads(4, 4, 4)]
void FluidSplatObstacleEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint Res = uint(FluidCfg.GridDims.x);
    if (DispatchThreadId.x >= Res || DispatchThreadId.y >= Res || DispatchThreadId.z >= Res) return;

    int3 Coord = int3(DispatchThreadId);
    float3 UVW = (float3(Coord) + 0.5) / float(Res);
    float3 BoxMin = FluidCfg.HeroBoxMinDt.xyz;
    float3 BoxMax = FluidCfg.HeroBoxMaxTime.xyz;
    float3 WorldPos = lerp(BoxMin, BoxMax, UVW);
    float Dt = FluidCfg.HeroBoxMinDt.w;
    float TimeSec = FluidCfg.HeroBoxMaxTime.w;

    float4 Vel = VelocitySolidTex[Coord];
    float4 Th  = ThermoTex[Coord];

    float Solid = SceneSolidMaskTex.Load(int4(Coord, 0));
    int ObsType = int(FluidCfg.ObstacleSpec.w + 0.5);
    if (ObsType > 0)
    {
        float3 D = WorldPos - FluidCfg.ObstacleSpec.xyz;
        float R = max(0.05, FluidCfg.ObstacleExt.x);
        if (ObsType == 1 && length(D) < R) Solid = 1.0;
        else if (ObsType == 2 && max(max(abs(D.x), abs(D.y)), abs(D.z)) < R) Solid = 1.0;
        else if (ObsType == 3 && length(D.xy) < R && abs(D.z) < R * 1.4) Solid = 1.0;
    }

    if (Solid > 0.5)
    {
        VelocitySolidTex[Coord] = float4(0.0, 0.0, 0.0, 1.0);
        ThermoTex[Coord]        = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    Vel.w = 0.0;

    // Continuous +Z emitter jet
    if (FluidCfg.EmitterParams.w > 0.5 && FluidCfg.EmitterPosRate.w > 1.0e-3)
    {
        float3 EmitCenter = float3(FluidCfg.EmitterPosRate.xy, BoxMin.z + FluidCfg.EmitterThermo.w);
        float3 D = WorldPos - EmitCenter;
        float Dist = length(float3(D.x, D.y, D.z * 1.45));
        float EmitR = max(0.04, FluidCfg.EmitterParams.x);
        if (Dist < EmitR * 1.35)
        {
            float Flicker = 0.82 + 0.28 * FluidValueNoise3(
                WorldPos * float3(3.8, 3.8, 3.2) + float3(TimeSec * 1.5, -TimeSec * 1.2, -TimeSec * 3.8));
            float Falloff = exp(-(Dist * Dist) / (EmitR * EmitR * 0.45)) * FluidCfg.EmitterPosRate.w * max(0.1, Flicker);
            float XYDist = max(1.0e-4, length(D.xy));
            float2 TanXY = float2(-D.y, D.x) / XYDist;

            Vel.xy += (TanXY * FluidCfg.EmitterParams.z + (D.xy / XYDist) * 0.45) * Falloff * Dt * 4.5;
            Vel.z  += FluidCfg.EmitterParams.y * Falloff * Dt * 6.0;
            Th.x = min(12.0, Th.x + FluidCfg.EmitterThermo.z * Falloff * Dt * 5.0);
            Th.y = min(12.0, Th.y + FluidCfg.EmitterThermo.x * Falloff * Dt * 5.5);
            Th.z = min(12.0, Th.z + FluidCfg.EmitterThermo.y * Falloff * Dt * 5.5);
        }
    }

    // Detonation blast injection
    if (FluidCfg.BlastLobesActive.z > 0.5)
    {
        float3 D = WorldPos - FluidCfg.BlastCenterRad.xyz;
        float Dist = length(D);
        float RMax = max(0.06, FluidCfg.BlastCenterRad.w) * 2.2;
        if (Dist < RMax)
        {
            float Theta = atan2(D.y, D.x);
            float Phi = acos(clamp(D.z / max(1.0e-4, Dist), -1.0, 1.0));
            float Lobes = FluidCfg.BlastLobesActive.x;
            float Lobe = 1.0 + 0.34 * sin(Theta * Lobes + FluidCfg.BlastLobesActive.y) * cos(Phi * Lobes * 0.7);
            float W = exp(-(Dist * Dist) / (FluidCfg.BlastCenterRad.w * FluidCfg.BlastCenterRad.w * 0.75)) * max(0.25, Lobe);
            Vel.xyz += (D / max(1.0e-4, Dist)) * FluidCfg.BlastStrength.x * W;
            Th.x = min(14.0, Th.x + FluidCfg.BlastStrength.w * W);
            Th.y = min(14.0, Th.y + FluidCfg.BlastStrength.y * W);
            Th.z = min(14.0, Th.z + FluidCfg.BlastStrength.z * W);
        }
    }

    VelocitySolidTex[Coord] = Vel;
    ThermoTex[Coord]        = Th;
}
