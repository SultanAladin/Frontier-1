/*============================================================================================================================================
                                                        FLUIDADVECTMACCORMACK.SLANG
============================================================================================================================================*/
// 🧩 MacCormack (BFECC + 6-neighbor min/max clamp) advection, Arrhenius combustion, cooling & soot generation

#include "FluidSolverLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<FluidSolverUniformBlock> FluidCfg;
[[vk::binding(1, 0)]] Texture3D<float4>                       VelocitySolidRead;
[[vk::binding(2, 0)]] Texture3D<float4>                       ThermoRead;
[[vk::binding(3, 0)]] SamplerState                            LinearClampSampler;
[[vk::binding(4, 0)]] RWTexture3D<float4>                     VelocitySolidWrite;
[[vk::binding(5, 0)]] RWTexture3D<float4>                     ThermoWrite;
[[vk::binding(6, 0)]] RWTexture3D<float4>                     DivergenceTex;

[shader("compute")]
[numthreads(4, 4, 4)]
void FluidAdvectMacCormackEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint Res = uint(FluidCfg.GridDims.x);
    if (DispatchThreadId.x >= Res || DispatchThreadId.y >= Res || DispatchThreadId.z >= Res) return;

    int3 Coord = int3(DispatchThreadId);
    float3 UVW = (float3(Coord) + 0.5) / float(Res);
    float3 Span = max(float3(1.0e-4, 1.0e-4, 1.0e-4), FluidCfg.HeroBoxMaxTime.xyz - FluidCfg.HeroBoxMinDt.xyz);
    float3 InvSpan = 1.0 / Span;
    float Dt = FluidCfg.HeroBoxMinDt.w;

    float4 CurV = VelocitySolidRead.Load(int4(Coord, 0));
    if (CurV.w > 0.5)
    {
        VelocitySolidWrite[Coord] = float4(0.0, 0.0, 0.0, 1.0);
        ThermoWrite[Coord]        = float4(0.0, 0.0, 0.0, 0.0);
        DivergenceTex[Coord]      = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float3 BackUVW = UVW - CurV.xyz * Dt * InvSpan;
    float4 AdvV = VelocitySolidRead.SampleLevel(LinearClampSampler, saturate(BackUVW), 0.0);
    float4 AdvT = ThermoRead.SampleLevel(LinearClampSampler, saturate(BackUVW), 0.0);

    if (FluidCfg.GridDims.w > 0.5)
    {
        float3 FwdUVW = BackUVW + AdvV.xyz * Dt * InvSpan;
        float4 BFT = ThermoRead.SampleLevel(LinearClampSampler, saturate(FwdUVW), 0.0);
        float4 CurT = ThermoRead.Load(int4(Coord, 0));
        float3 Corr = AdvT.xyz + 0.45 * (CurT.xyz - BFT.xyz);

        float3 NL = ThermoRead.Load(int4(max(0, Coord.x - 1), Coord.y, Coord.z, 0)).xyz;
        float3 NR = ThermoRead.Load(int4(min(int(Res) - 1, Coord.x + 1), Coord.y, Coord.z, 0)).xyz;
        float3 ND = ThermoRead.Load(int4(Coord.x, max(0, Coord.y - 1), Coord.z, 0)).xyz;
        float3 NU = ThermoRead.Load(int4(Coord.x, min(int(Res) - 1, Coord.y + 1), Coord.z, 0)).xyz;
        float3 NB = ThermoRead.Load(int4(Coord.x, Coord.y, max(0, Coord.z - 1), 0)).xyz;
        float3 NF = ThermoRead.Load(int4(Coord.x, Coord.y, min(int(Res) - 1, Coord.z + 1), 0)).xyz;

        float3 Lo = min(CurT.xyz, min(min(NL, NR), min(min(ND, NU), min(NB, NF))));
        float3 Hi = max(CurT.xyz, max(max(NL, NR), max(max(ND, NU), max(NB, NF))));
        AdvT.xyz = clamp(Corr, Lo, Hi);
    }

    float Smoke = max(0.0, AdvT.x);
    float Temp  = max(0.0, AdvT.y);
    float Fuel  = max(0.0, AdvT.z);

    float Ign = smoothstep(0.08, 0.65, Temp + 0.5 * Fuel);
    float BurnedFuel = min(Fuel, Fuel * FluidCfg.CombustionB.x * (0.65 + 0.65 * Ign) * Dt);
    Fuel  = max(0.0, Fuel - BurnedFuel);
    Temp += BurnedFuel * FluidCfg.CombustionB.y;
    Smoke += BurnedFuel * FluidCfg.CombustionB.z;

    float RadCool = FluidCfg.CombustionA.w * (0.65 * Temp + 0.24 * Temp * Temp) * Dt;
    Temp  = max(0.0, Temp - RadCool);
    Smoke = max(0.0, Smoke * exp(-FluidCfg.CombustionC.z * Dt));
    Fuel  = max(0.0, Fuel  * exp(-0.15 * Dt));

    if (FluidCfg.GridDims.z < 0.5)
    {
        float EdgeXY = min(min(UVW.x, 1.0 - UVW.x), min(UVW.y, 1.0 - UVW.y));
        float Sponge = smoothstep(0.0, 0.10, EdgeXY) * smoothstep(0.0, 0.11, 1.0 - UVW.z);
        Smoke *= Sponge;
        Temp  *= Sponge;
        Fuel  *= Sponge;
    }

    float Damp = exp(-max(0.0, FluidCfg.CombustionC.w) * Dt);
    VelocitySolidWrite[Coord] = float4(AdvV.xyz * Damp, 0.0);
    ThermoWrite[Coord]        = float4(Smoke, Temp, Fuel, Temp);
    DivergenceTex[Coord]      = float4(0.0, BurnedFuel / max(Dt, 1.0e-4), 0.0, 0.0);
}
