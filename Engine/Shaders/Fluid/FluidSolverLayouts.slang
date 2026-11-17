/*============================================================================================================================================
                                                         FLUIDSOLVERLAYOUTS.SLANG
============================================================================================================================================*/
// 🧩 Shared +Z-Up Fluid GPU uniform blocks, 6-palette Blackbody evaluator, 3D curl noise, Dual-HG phase & ray-box intersection

#pragma once

static const float FluidPiConst = 3.14159265359;

//------------------------------------------------------------------------------------------------------------------------
//                                                    GPU UNIFORM LAYOUTS
//------------------------------------------------------------------------------------------------------------------------

struct FluidEmissiveLightGpu
{
    float4 PositionRadius;    // xyz = WorldPosM, w = RadiusM
    float4 ColorIrradiance;   // rgb = Irradiance, w = Active (0/1)
};

struct FluidSolverUniformBlock
{
    float4 HeroBoxMinDt;      // xyz = HeroBoxMinWorld, w = DtSeconds
    float4 HeroBoxMaxTime;    // xyz = HeroBoxMaxWorld, w = ElapsedSeconds
    float4 WorldCenterSpan;   // xyz = WorldGridCenter, w = WorldGridSpanMeters
    float4 GridDims;          // x = HeroRes, y = WorldRes, z = EnclosedBox (0/1), w = MacCormack (0/1)
    float4 EmitterPosRate;    // xyz = EmitterOriginWorld, w = EmitterRate
    float4 EmitterParams;     // x = RadiusM, y = UpwardVelMps, z = Swirl, w = Enabled (0/1)
    float4 EmitterThermo;     // x = Temp, y = Fuel, z = Smoke, w = HeightM
    float4 CombustionA;       // x = Buoyancy, y = SmokeWeight, z = Vorticity, w = CoolingRate
    float4 CombustionB;       // x = BurnRate, y = BurnHeat, z = SootGen, w = CombustionExpansion
    float4 CombustionC;       // x = TurbStrength, y = TurbScale, z = SmokeDiss, w = VelDamping
    float4 WindAndForce;      // x = WindVX, y = WindVY, z = CoupleForceFields (0/1), w = ActiveForceCount
    float4 ObstacleSpec;      // xyz = ObstacleCenterWorld, w = ObstacleType (0..4)
    float4 ObstacleExt;       // x = ObstacleRadiusM, y = TimeScale, z = WorldGridEnabled (0/1), w = WorldSmokeCarry
    float4 BlastCenterRad;    // xyz = BlastCenterWorld, w = BlastRadiusM
    float4 BlastStrength;     // x = BlastStrengthMps, y = BlastTemp, z = BlastFuel, w = BlastSmoke
    float4 BlastLobesActive;  // x = Lobes, y = Seed, z = BlastActive (0/1), w = SplatCount
};

struct FluidRenderUniformBlock
{
    float4 HeroBoxMinFade;    // xyz = HeroBoxMinWorld, w = FluidEnabled (0/1)
    float4 HeroBoxMaxRes;     // xyz = HeroBoxMaxWorld, w = float(HeroRes)
    float4 WorldCenterSpan;   // xyz = WorldGridCenter, w = WorldGridSpanMeters
    float4 SunDirRaySteps;    // xyz = UnitSunDir, w = float(RaymarchSteps)
    float4 SolarIrradiance;   // rgb = SolarIrradiance, w = float(ShadowSteps)
    float4 SkyRadiance;       // rgb = SkyRadiance, w = TimeSeconds
    float4 ShadingCore;       // x = DensityExtinction, y = SmokeAlbedo, z = FireIntensity, w = TempScale
    float4 ShadingScatter;    // x = AmbientIntensity, y = PhaseAnisotropy, y=g, z = ShadowDensity, w = InternalScattering
    float4 ShadingMode;       // x = PaletteIndex (0..5), y = ChannelIndex (0..8), z = WorldGridEnabled (0/1), w = GiShadowStrength
    float4 CrossSectionCfg;   // x = Axis (0..2), y = SliceOffset01, z = FloorGlow, w = ShockwaveAge
    float4 ShockwaveCenter;   // xyz = ShockwaveWorldPos, w = EmissiveLightCount
    FluidEmissiveLightGpu EmissiveLights[8];
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    NOISE & PHASE HELPERS
//------------------------------------------------------------------------------------------------------------------------

float FluidHashInt3(int IX, int IY, int IZ)
{
    uint N = uint(IX * 374761393 + IY * 668265263 + IZ * 2147483647);
    N = (N ^ (N >> 13u)) * 1274126177u;
    N = N ^ (N >> 16u);
    return float(N) * (1.0 / 4294967296.0) * 2.0 - 1.0;
}

float FluidValueNoise3(float3 P)
{
    int3 I = int3(floor(P));
    float3 F = P - float3(I);
    float3 U = F * F * (3.0 - 2.0 * F);

    float N000 = FluidHashInt3(I.x, I.y, I.z);
    float N100 = FluidHashInt3(I.x + 1, I.y, I.z);
    float N010 = FluidHashInt3(I.x, I.y + 1, I.z);
    float N110 = FluidHashInt3(I.x + 1, I.y + 1, I.z);
    float N001 = FluidHashInt3(I.x, I.y, I.z + 1);
    float N101 = FluidHashInt3(I.x + 1, I.y, I.z + 1);
    float N011 = FluidHashInt3(I.x, I.y + 1, I.z + 1);
    float N111 = FluidHashInt3(I.x + 1, I.y + 1, I.z + 1);

    return lerp(
        lerp(lerp(N000, N100, U.x), lerp(N010, N110, U.x), U.y),
        lerp(lerp(N001, N101, U.x), lerp(N011, N111, U.x), U.y),
        U.z);
}

float3 FluidCurlNoise3(float3 P)
{
    float Eps = 0.25;
    float N1 = FluidValueNoise3(P + float3(0.0, Eps, 0.0)) - FluidValueNoise3(P - float3(0.0, Eps, 0.0));
    float N2 = FluidValueNoise3(P + float3(0.0, 0.0, Eps)) - FluidValueNoise3(P - float3(0.0, 0.0, Eps));
    float N3 = FluidValueNoise3(P + float3(Eps, 0.0, 0.0)) - FluidValueNoise3(P - float3(Eps, 0.0, 0.0));
    return float3(N1 - N2, N2 - N3, N3 - N1) / (2.0 * Eps);
}

float FluidHgPhase(float CosTheta, float G)
{
    float G2 = G * G;
    float Denom = max(1.0e-4, pow(1.0 + G2 - 2.0 * G * CosTheta, 1.5));
    return (1.0 - G2) / (4.0 * FluidPiConst * Denom);
}

float FluidDualHgPhase(float CosTheta, float G)
{
    return 0.28 * FluidHgPhase(CosTheta, -0.25) + 0.72 * FluidHgPhase(CosTheta, G);
}

bool FluidIntersectBox(float3 Ro, float3 Rd, float3 BMin, float3 BMax, out float TNear, out float TFar)
{
    float3 SafeRd = float3(
        abs(Rd.x) > 1.0e-7 ? Rd.x : (Rd.x >= 0.0 ? 1.0e-7 : -1.0e-7),
        abs(Rd.y) > 1.0e-7 ? Rd.y : (Rd.y >= 0.0 ? 1.0e-7 : -1.0e-7),
        abs(Rd.z) > 1.0e-7 ? Rd.z : (Rd.z >= 0.0 ? 1.0e-7 : -1.0e-7));
    float3 InvRd = 1.0 / SafeRd;
    float3 T0 = (BMin - Ro) * InvRd;
    float3 T1 = (BMax - Ro) * InvRd;
    float3 TSmall = min(T0, T1);
    float3 TBig   = max(T0, T1);
    TNear = max(max(TSmall.x, TSmall.y), TSmall.z);
    TFar  = min(min(TBig.x, TBig.y), TBig.z);
    return TFar > max(0.0, TNear);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 6-PALETTE BLACKBODY LUT
//------------------------------------------------------------------------------------------------------------------------

float3 EvaluateFluidBlackbodyPaletteGpu(float Temperature, float TemperatureScale, int PaletteIndex)
{
    float T = clamp(Temperature * TemperatureScale * 0.16, 0.0, 1.35);
    if (T <= 0.01) return float3(0.0, 0.0, 0.0);

    float3 Col = float3(0.0, 0.0, 0.0);
    if (PaletteIndex == 1) // CinemaNapalm
    {
        Col = lerp(float3(0.0, 0.0, 0.0), float3(0.72, 0.04, 0.004), smoothstep(0.0, 0.24, T));
        Col = lerp(Col, float3(1.00, 0.18, 0.01), smoothstep(0.20, 0.56, T));
        Col = lerp(Col, float3(1.00, 0.56, 0.08), smoothstep(0.50, 0.94, T));
        Col = lerp(Col, float3(1.00, 0.88, 0.45), smoothstep(0.88, 1.35, T));
    }
    else if (PaletteIndex == 2) // ThermobaricWhiteHot
    {
        Col = lerp(float3(0.0, 0.0, 0.0), float3(0.75, 0.16, 0.02), smoothstep(0.0, 0.18, T));
        Col = lerp(Col, float3(1.00, 0.54, 0.14), smoothstep(0.16, 0.44, T));
        Col = lerp(Col, float3(1.00, 0.92, 0.68), smoothstep(0.40, 0.75, T));
        Col = lerp(Col, float3(1.15, 1.15, 1.25), smoothstep(0.72, 1.25, T));
    }
    else if (PaletteIndex == 3) // WildfireCopperEmerald
    {
        Col = lerp(float3(0.0, 0.0, 0.0), float3(0.04, 0.35, 0.14), smoothstep(0.0, 0.24, T));
        Col = lerp(Col, float3(0.12, 0.92, 0.36), smoothstep(0.20, 0.55, T));
        Col = lerp(Col, float3(0.62, 1.00, 0.54), smoothstep(0.50, 0.92, T));
        Col = lerp(Col, float3(0.95, 1.00, 0.92), smoothstep(0.88, 1.35, T));
    }
    else if (PaletteIndex == 4) // CherenkovBluePlasma
    {
        Col = lerp(float3(0.0, 0.0, 0.0), float3(0.05, 0.12, 0.62), smoothstep(0.0, 0.22, T));
        Col = lerp(Col, float3(0.14, 0.56, 1.00), smoothstep(0.18, 0.52, T));
        Col = lerp(Col, float3(0.58, 0.88, 1.00), smoothstep(0.48, 0.90, T));
        Col = lerp(Col, float3(0.94, 0.98, 1.05), smoothstep(0.85, 1.35, T));
    }
    else if (PaletteIndex == 5) // ArcaneVoidMagenta
    {
        Col = lerp(float3(0.0, 0.0, 0.0), float3(0.38, 0.03, 0.48), smoothstep(0.0, 0.22, T));
        Col = lerp(Col, float3(0.88, 0.16, 0.96), smoothstep(0.18, 0.54, T));
        Col = lerp(Col, float3(1.00, 0.54, 0.98), smoothstep(0.50, 0.92, T));
        Col = lerp(Col, float3(1.00, 0.92, 1.00), smoothstep(0.86, 1.35, T));
    }
    else // PlanckianBlackbody (0)
    {
        Col = lerp(float3(0.0, 0.0, 0.0), float3(0.55, 0.03, 0.005), smoothstep(0.0, 0.22, T));
        Col = lerp(Col, float3(1.00, 0.24, 0.01), smoothstep(0.18, 0.52, T));
        Col = lerp(Col, float3(1.00, 0.68, 0.10), smoothstep(0.48, 0.88, T));
        Col = lerp(Col, float3(1.00, 0.96, 0.78), smoothstep(0.82, 1.35, T));
    }

    float Gain = pow(T, 1.35) * 0.85;
    return Col * Gain;
}
