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
        bool IsTornado = (FluidCfg.EmitterParams.z >= 3.0);
        float HeightAbove = max(0.0, D.z);
        float FunnelH = IsTornado ? min(1.0, HeightAbove / max(0.5, (BoxMax.z - BoxMin.z) * 0.65)) : 0.0;
        float DZ = IsTornado ? ((D.z < 0.0) ? -D.z * 2.0 : HeightAbove * 0.28) : (D.z * 1.45);
        float RawDist = IsTornado ? length(float3(D.x, D.y, DZ * 0.59)) : length(float3(D.x, D.y, DZ));
        float EmitR = max(0.04, FluidCfg.EmitterParams.x) * (IsTornado ? (1.0 + FunnelH * 0.75) : 1.0);
        float Fbm = FluidFbm2Noise3(WorldPos * float3(8.5, 8.5, 7.5) + float3(TimeSec * 1.6, -TimeSec * 1.3, -TimeSec * 4.8));
        float WarpedDist = RawDist * (1.0 + 0.28 * Fbm);
        if (WarpedDist < EmitR * 1.55)
        {
            float HeightFade = IsTornado ? (1.0 - 0.62 * FunnelH) : 1.0;
            float Flicker = max(0.15, 0.85 + 0.52 * Fbm);
            float Falloff = exp(-(WarpedDist * WarpedDist) / (EmitR * EmitR * 0.42)) * FluidCfg.EmitterPosRate.w * Flicker * HeightFade;
            float XYDist = max(1.0e-4, length(D.xy));
            float2 TanXY = float2(-D.y, D.x) / XYDist;
            float2 RadXY = D.xy / XYDist;

            float3 Turb = FluidHash33(WorldPos * 14.0 + float3(TimeSec * 7.1, -TimeSec * 5.3, TimeSec * 11.7));
            float RadialBias = IsTornado ? -0.85 : 0.35;
            float2 TargetXY = TanXY * FluidCfg.EmitterParams.z + RadXY * RadialBias + float2(Fbm * 0.9, -Fbm * 0.8) + Turb.xy * 0.65;
            float  TargetZ  = FluidCfg.EmitterParams.y * (IsTornado ? 1.25 : 1.0) * (0.82 + 0.42 * max(0.0, Fbm)) + Turb.z * 0.45;
            float  Blend    = clamp(Falloff * Dt * 11.0, 0.0, 0.85);

            Vel.xy += (TargetXY - Vel.xy) * Blend;
            Vel.z  += (TargetZ  - Vel.z)  * Blend;
            Th.x = max(Th.x, FluidCfg.EmitterThermo.z * Falloff * (0.6 + 0.4 * max(0.0, Fbm)));
            Th.y = max(Th.y, FluidCfg.EmitterThermo.x * Falloff);
            Th.z = max(Th.z, FluidCfg.EmitterThermo.y * Falloff);
        }
    }

    // Detonation blast injection
    if (FluidCfg.BlastLobesActive.z > 0.5)
    {
        float3 D = WorldPos - FluidCfg.BlastCenterRad.xyz;
        float Dist = length(D);
        float RMax = max(0.06, FluidCfg.BlastCenterRad.w) * 2.45;
        if (Dist < RMax)
        {
            float Theta = atan2(D.y, D.x);
            float Phi = acos(clamp(D.z / max(1.0e-4, Dist), -1.0, 1.0));
            float Lobes = FluidCfg.BlastLobesActive.x;
            float Seed = FluidCfg.BlastLobesActive.y;
            float NoiseWarp = FluidFbm2Noise3(WorldPos * 10.5 + float3(Seed, -Seed, 0.0));
            float Lobe = 1.0 + 0.38 * sin(Theta * Lobes + Seed + NoiseWarp * 1.4) * cos(Phi * Lobes * 0.7) + 0.22 * NoiseWarp;
            float W = exp(-(Dist * Dist) / (FluidCfg.BlastCenterRad.w * FluidCfg.BlastCenterRad.w * 0.72)) * max(0.18, Lobe);
            Vel.xyz += ((D / max(1.0e-4, Dist)) * FluidCfg.BlastStrength.x + float3(NoiseWarp * 2.2, -NoiseWarp * 2.2, 0.0)) * W;
            Th.x = min(14.0, Th.x + FluidCfg.BlastStrength.w * W);
            Th.y = min(14.0, Th.y + FluidCfg.BlastStrength.y * W);
            Th.z = min(14.0, Th.z + FluidCfg.BlastStrength.z * W);
        }
    }

    VelocitySolidTex[Coord] = Vel;
    ThermoTex[Coord]        = Th;
}
