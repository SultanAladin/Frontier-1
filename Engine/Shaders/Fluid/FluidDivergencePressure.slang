/*============================================================================================================================================
                                                      FLUIDDIVERGENCEPRESSURE.SLANG
============================================================================================================================================*/
// 🧩 Velocity divergence + gas expansion, Neumann Jacobi pressure + fire irradiance diffusion, and pressure gradient subtraction

#include "FluidSolverLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<FluidSolverUniformBlock> FluidCfg;
[[vk::binding(1, 0)]] RWTexture3D<float4>                     VelocitySolidTex;
[[vk::binding(2, 0)]] Texture3D<float4>                       ThermoTex;
[[vk::binding(3, 0)]] RWTexture3D<float4>                     DivergenceTex;
[[vk::binding(4, 0)]] Texture3D<float4>                       PressureRead;
[[vk::binding(5, 0)]] RWTexture3D<float4>                     PressureWrite;
[[vk::binding(6, 0)]] RWTexture3D<float4>                     ThermoIrradianceWrite;

[shader("compute")]
[numthreads(4, 4, 4)]
void FluidDivergenceEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint Res = uint(FluidCfg.GridDims.x);
    if (DispatchThreadId.x >= Res || DispatchThreadId.y >= Res || DispatchThreadId.z >= Res) return;

    int3 C = int3(DispatchThreadId);
    int M = int(Res) - 1;
    bool Enclosed = FluidCfg.GridDims.z > 0.5;
    if (VelocitySolidTex[C].w > 0.5)
    {
        DivergenceTex[C] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float4 VL = VelocitySolidTex[int3(max(0, C.x - 1), C.y, C.z)];
    float4 VR = VelocitySolidTex[int3(min(M, C.x + 1), C.y, C.z)];
    float4 VD = VelocitySolidTex[int3(C.x, max(0, C.y - 1), C.z)];
    float4 VU = VelocitySolidTex[int3(C.x, min(M, C.y + 1), C.z)];
    float4 VB = VelocitySolidTex[int3(C.x, C.y, max(0, C.z - 1))];
    float4 VF = VelocitySolidTex[int3(C.x, C.y, min(M, C.z + 1))];

    float UX0 = (VL.w > 0.5 || (Enclosed && C.x == 0)) ? 0.0 : VL.x;
    float UX1 = (VR.w > 0.5 || (Enclosed && C.x == M)) ? 0.0 : VR.x;
    float UY0 = (VD.w > 0.5 || (Enclosed && C.y == 0)) ? 0.0 : VD.y;
    float UY1 = (VU.w > 0.5 || (Enclosed && C.y == M)) ? 0.0 : VU.y;
    float UZ0 = (VB.w > 0.5 || C.z == 0)               ? 0.0 : VB.z;
    float UZ1 = (VF.w > 0.5 || (Enclosed && C.z == M)) ? 0.0 : VF.z;

    float4 PrevDiv = DivergenceTex[C];
    float Div = 0.5 * ((UX1 - UX0) + (UY1 - UY0) + (UZ1 - UZ0)) - PrevDiv.y * FluidCfg.CombustionB.w * 0.055;
    float4 Th = ThermoTex.Load(int4(C, 0));
    float FireSrc = clamp(Th.y * 0.45 + Th.z * 0.35, 0.0, 6.0);
    DivergenceTex[C] = float4(Div, PrevDiv.y, FireSrc, 0.0);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void FluidJacobiPressureEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint Res = uint(FluidCfg.GridDims.x);
    if (DispatchThreadId.x >= Res || DispatchThreadId.y >= Res || DispatchThreadId.z >= Res) return;

    int3 C = int3(DispatchThreadId);
    int M = int(Res) - 1;
    bool Enclosed = FluidCfg.GridDims.z > 0.5;
    float4 CP = PressureRead.Load(int4(C, 0));
    float4 DivData = DivergenceTex[C];

    float4 PL = (!Enclosed && C.x == 0) ? float4(0,0,0,1) : ((VelocitySolidTex[int3(max(0, C.x - 1), C.y, C.z)].w > 0.5 || (Enclosed && C.x == 0)) ? CP : PressureRead.Load(int4(max(0, C.x - 1), C.y, C.z, 0)));
    float4 PR = (!Enclosed && C.x == M) ? float4(0,0,0,1) : ((VelocitySolidTex[int3(min(M, C.x + 1), C.y, C.z)].w > 0.5 || (Enclosed && C.x == M)) ? CP : PressureRead.Load(int4(min(M, C.x + 1), C.y, C.z, 0)));
    float4 PD = (!Enclosed && C.y == 0) ? float4(0,0,0,1) : ((VelocitySolidTex[int3(C.x, max(0, C.y - 1), C.z)].w > 0.5 || (Enclosed && C.y == 0)) ? CP : PressureRead.Load(int4(C.x, max(0, C.y - 1), C.z, 0)));
    float4 PU = (!Enclosed && C.y == M) ? float4(0,0,0,1) : ((VelocitySolidTex[int3(C.x, min(M, C.y + 1), C.z)].w > 0.5 || (Enclosed && C.y == M)) ? CP : PressureRead.Load(int4(C.x, min(M, C.y + 1), C.z, 0)));
    float4 PB = (VelocitySolidTex[int3(C.x, C.y, max(0, C.z - 1))].w > 0.5 || C.z == 0) ? CP : PressureRead.Load(int4(C.x, C.y, max(0, C.z - 1), 0));
    float4 PF = (!Enclosed && C.z == M) ? float4(0,0,0,1) : ((VelocitySolidTex[int3(C.x, C.y, min(M, C.z + 1))].w > 0.5 || (Enclosed && C.z == M)) ? CP : PressureRead.Load(int4(C.x, C.y, min(M, C.z + 1), 0)));

    float PNew   = (PL.x + PR.x + PD.x + PU.x + PB.x + PF.x - DivData.x) / 6.0;
    float IrrNew = (PL.y + PR.y + PD.y + PU.y + PB.y + PF.y + DivData.z * 0.55) / 6.28;
    PressureWrite[C] = float4(PNew, IrrNew, 0.0, 1.0);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void FluidGradientSubtractEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint Res = uint(FluidCfg.GridDims.x);
    if (DispatchThreadId.x >= Res || DispatchThreadId.y >= Res || DispatchThreadId.z >= Res) return;

    int3 C = int3(DispatchThreadId);
    int M = int(Res) - 1;
    bool Enclosed = FluidCfg.GridDims.z > 0.5;
    float4 Vel = VelocitySolidTex[C];
    if (Vel.w > 0.5)
    {
        VelocitySolidTex[C] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float PC = PressureRead.Load(int4(C, 0)).x;
    float PL = (!Enclosed && C.x == 0) ? 0.0 : ((VelocitySolidTex[int3(max(0, C.x - 1), C.y, C.z)].w > 0.5 || (Enclosed && C.x == 0)) ? PC : PressureRead.Load(int4(max(0, C.x - 1), C.y, C.z, 0)).x);
    float PR = (!Enclosed && C.x == M) ? 0.0 : ((VelocitySolidTex[int3(min(M, C.x + 1), C.y, C.z)].w > 0.5 || (Enclosed && C.x == M)) ? PC : PressureRead.Load(int4(min(M, C.x + 1), C.y, C.z, 0)).x);
    float PD = (!Enclosed && C.y == 0) ? 0.0 : ((VelocitySolidTex[int3(C.x, max(0, C.y - 1), C.z)].w > 0.5 || (Enclosed && C.y == 0)) ? PC : PressureRead.Load(int4(C.x, max(0, C.y - 1), C.z, 0)).x);
    float PU = (!Enclosed && C.y == M) ? 0.0 : ((VelocitySolidTex[int3(C.x, min(M, C.y + 1), C.z)].w > 0.5 || (Enclosed && C.y == M)) ? PC : PressureRead.Load(int4(C.x, min(M, C.y + 1), C.z, 0)).x);
    float PB = (VelocitySolidTex[int3(C.x, C.y, max(0, C.z - 1))].w > 0.5 || C.z == 0) ? PC : PressureRead.Load(int4(C.x, C.y, max(0, C.z - 1), 0)).x;
    float PF = (!Enclosed && C.z == M) ? 0.0 : ((VelocitySolidTex[int3(C.x, C.y, min(M, C.z + 1))].w > 0.5 || (Enclosed && C.z == M)) ? PC : PressureRead.Load(int4(C.x, C.y, min(M, C.z + 1), 0)).x);

    Vel.xyz -= 0.5 * float3(PR - PL, PU - PD, PF - PB);
    VelocitySolidTex[C] = Vel;

    float4 Th = ThermoTex.Load(int4(C, 0));
    Th.w = PressureRead.Load(int4(C, 0)).y;
    ThermoIrradianceWrite[C] = Th;
}
