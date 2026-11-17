/*============================================================================================================================================
                                                      FLUIDVORTICITYCOMBUSTION.SLANG
============================================================================================================================================*/
// 🧩 3D Curl vorticity confinement, +Z thermal buoyancy, baroclinic torque, curl turbulence & ForceFields advection

#include "FluidSolverLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<FluidSolverUniformBlock> FluidCfg;
[[vk::binding(1, 0)]] RWTexture3D<float4>                     VelocitySolidTex;
[[vk::binding(2, 0)]] Texture3D<float4>                       ThermoTex;
[[vk::binding(3, 0)]] RWTexture3D<float4>                     CurlTex;

[shader("compute")]
[numthreads(4, 4, 4)]
void FluidCurlComputeEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint Res = uint(FluidCfg.GridDims.x);
    if (DispatchThreadId.x >= Res || DispatchThreadId.y >= Res || DispatchThreadId.z >= Res) return;

    int3 C = int3(DispatchThreadId);
    int M = int(Res) - 1;
    float3 VL = VelocitySolidTex[int3(max(0, C.x - 1), C.y, C.z)].xyz;
    float3 VR = VelocitySolidTex[int3(min(M, C.x + 1), C.y, C.z)].xyz;
    float3 VD = VelocitySolidTex[int3(C.x, max(0, C.y - 1), C.z)].xyz;
    float3 VU = VelocitySolidTex[int3(C.x, min(M, C.y + 1), C.z)].xyz;
    float3 VB = VelocitySolidTex[int3(C.x, C.y, max(0, C.z - 1))].xyz;
    float3 VF = VelocitySolidTex[int3(C.x, C.y, min(M, C.z + 1))].xyz;

    float3 Curl = float3(
        0.5 * ((VU.z - VD.z) - (VF.y - VB.y)),
        0.5 * ((VF.x - VB.x) - (VR.z - VL.z)),
        0.5 * ((VR.y - VL.y) - (VU.x - VD.x)));
    CurlTex[C] = float4(Curl, length(Curl));
}

[shader("compute")]
[numthreads(4, 4, 4)]
void FluidVorticityForcesEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint Res = uint(FluidCfg.GridDims.x);
    if (DispatchThreadId.x >= Res || DispatchThreadId.y >= Res || DispatchThreadId.z >= Res) return;

    int3 C = int3(DispatchThreadId);
    int M = int(Res) - 1;
    float4 Vel = VelocitySolidTex[C];
    if (Vel.w > 0.5)
    {
        VelocitySolidTex[C] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float Dt = FluidCfg.HeroBoxMinDt.w;
    float TimeSec = FluidCfg.HeroBoxMaxTime.w;
    float3 UVW = (float3(C) + 0.5) / float(Res);
    float3 WorldPos = lerp(FluidCfg.HeroBoxMinDt.xyz, FluidCfg.HeroBoxMaxTime.xyz, UVW);
    float4 Th = ThermoTex.Load(int4(C, 0));

    float CL = CurlTex[int3(max(0, C.x - 1), C.y, C.z)].w;
    float CR = CurlTex[int3(min(M, C.x + 1), C.y, C.z)].w;
    float CD = CurlTex[int3(C.x, max(0, C.y - 1), C.z)].w;
    float CU = CurlTex[int3(C.x, min(M, C.y + 1), C.z)].w;
    float CB = CurlTex[int3(C.x, C.y, max(0, C.z - 1))].w;
    float CF = CurlTex[int3(C.x, C.y, min(M, C.z + 1))].w;

    float3 Grad = 0.5 * float3(CR - CL, CU - CD, CF - CB);
    float GL = length(Grad);
    float3 Vort = float3(0.0, 0.0, 0.0);
    if (GL > 1.0e-5)
    {
        float3 N = Grad / GL;
        Vort = clamp(cross(N, CurlTex[C].xyz) * FluidCfg.CombustionA.z, float3(-48.0, -48.0, -48.0), float3(48.0, 48.0, 48.0));
    }

    float BuoyZ = FluidCfg.CombustionA.x * Th.y - FluidCfg.CombustionA.y * Th.x * 0.45;
    float TL = ThermoTex.Load(int4(max(0, C.x - 1), C.y, C.z, 0)).y;
    float TR = ThermoTex.Load(int4(min(M, C.x + 1), C.y, C.z, 0)).y;
    float TB = ThermoTex.Load(int4(C.x, max(0, C.y - 1), C.z, 0)).y;
    float TF = ThermoTex.Load(int4(C.x, min(M, C.y + 1), C.z, 0)).y;
    float2 BaroXY = float2(TR - TL, TF - TB) * (0.35 * FluidCfg.CombustionA.x);

    float Activity = clamp(Th.x * 0.65 + Th.y * 0.85 + Th.z * 0.55, 0.0, 1.5);
    float3 Turb = FluidCurlFbm2(WorldPos * (FluidCfg.CombustionC.y * 0.95) + float3(TimeSec * 0.75, TimeSec * 0.55, -TimeSec * 1.65));
    float WindGain = Activity * smoothstep(0.02, 0.30, UVW.z);

    float3 Funnel = float3(0.0, 0.0, 0.0);
    if (FluidCfg.EmitterParams.z >= 3.0 && Activity > 1.0e-3)
    {
        float2 RD = WorldPos.xy - FluidCfg.EmitterPosRate.xy;
        float RXY = length(RD);
        float CoreR = 0.14 + UVW.z * 0.28;
        if (RXY > 1.0e-4 && RXY < CoreR * 3.8)
        {
            float2 TanXY = float2(-RD.y, RD.x) / RXY;
            float2 RadXY = RD / RXY;
            float Profile = (RXY * CoreR) / (RXY * RXY + CoreR * CoreR);
            float SwirlGain = FluidCfg.EmitterParams.z * 3.4 * Profile * (1.15 - 0.35 * UVW.z);
            float PullGain  = (RXY > CoreR * 0.65) ? (-2.35 * FluidCfg.EmitterParams.z * 0.28 * Profile) : 0.35;
            Funnel.xy = TanXY * SwirlGain + RadXY * PullGain;
            Funnel.z  = 14.0 * Profile * (1.10 - 0.35 * UVW.z);
        }
    }

    Vel.xy += (Vort.xy + BaroXY + Funnel.xy + Turb.xy * FluidCfg.CombustionC.x * Activity + FluidCfg.WindAndForce.xy * WindGain) * Dt;
    Vel.z  += (Vort.z  + BuoyZ  + Funnel.z  + Turb.z  * FluidCfg.CombustionC.x * Activity) * Dt;

    float Spd = length(Vel.xyz);
    if (Spd > 24.0) Vel.xyz *= (24.0 / Spd);
    VelocitySolidTex[C] = Vel;
}
