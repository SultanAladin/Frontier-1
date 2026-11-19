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

float3 FluidHash33(float3 P)
{
    // Exact 1:1 port of Experimental/FluidEditor/src/shaders-wgsl.js :: hash33
    float3 P3 = frac(P * float3(0.1031, 0.1030, 0.0973));
    P3 += dot(P3, P3.yxz + 33.33);
    return frac((P3.xxy + P3.yxx) * P3.zyx) * 2.0 - 1.0;
}

float FluidGradientNoise3(float3 P)
{
    // Exact 1:1 port of Experimental/FluidEditor/src/shaders-wgsl.js :: gradientNoise3
    float3 I = floor(P);
    float3 F = P - I;
    float3 U = F * F * (3.0 - 2.0 * F);

    float N000 = dot(FluidHash33(I + float3(0.0, 0.0, 0.0)), F - float3(0.0, 0.0, 0.0));
    float N100 = dot(FluidHash33(I + float3(1.0, 0.0, 0.0)), F - float3(1.0, 0.0, 0.0));
    float N010 = dot(FluidHash33(I + float3(0.0, 1.0, 0.0)), F - float3(0.0, 1.0, 0.0));
    float N110 = dot(FluidHash33(I + float3(1.0, 1.0, 0.0)), F - float3(1.0, 1.0, 0.0));
    float N001 = dot(FluidHash33(I + float3(0.0, 0.0, 1.0)), F - float3(0.0, 0.0, 1.0));
    float N101 = dot(FluidHash33(I + float3(1.0, 0.0, 1.0)), F - float3(1.0, 0.0, 1.0));
    float N011 = dot(FluidHash33(I + float3(0.0, 1.0, 1.0)), F - float3(0.0, 1.0, 1.0));
    float N111 = dot(FluidHash33(I + float3(1.0, 1.0, 1.0)), F - float3(1.0, 1.0, 1.0));

    return lerp(
        lerp(lerp(N000, N100, U.x), lerp(N010, N110, U.x), U.y),
        lerp(lerp(N001, N101, U.x), lerp(N011, N111, U.x), U.y),
        U.z);
}

float FluidFbm2Noise3(float3 P)
{
    return 0.65 * FluidGradientNoise3(P) + 0.35 * FluidGradientNoise3(P * 2.13 + float3(7.3, -4.1, 5.7));
}

float3 FluidCurlNoise3(float3 P)
{
    // True divergence-free 3D curl of 3 decorrelated potential fields (no (1,1,1) axis bias)
    float Eps = 0.15;
    float Inv2Eps = 1.0 / (2.0 * Eps);

    float DP0DY = (FluidGradientNoise3(P + float3(0.0, Eps, 0.0)) - FluidGradientNoise3(P - float3(0.0, Eps, 0.0))) * Inv2Eps;
    float DP0DZ = (FluidGradientNoise3(P + float3(0.0, 0.0, Eps)) - FluidGradientNoise3(P - float3(0.0, 0.0, Eps))) * Inv2Eps;

    float3 P1 = P + float3(31.4, 17.2, 43.1);
    float DP1DX = (FluidGradientNoise3(P1 + float3(Eps, 0.0, 0.0)) - FluidGradientNoise3(P1 - float3(Eps, 0.0, 0.0))) * Inv2Eps;
    float DP1DZ = (FluidGradientNoise3(P1 + float3(0.0, 0.0, Eps)) - FluidGradientNoise3(P1 - float3(0.0, 0.0, Eps))) * Inv2Eps;

    float3 P2 = P + float3(-19.7, 53.8, -11.3);
    float DP2DX = (FluidGradientNoise3(P2 + float3(Eps, 0.0, 0.0)) - FluidGradientNoise3(P2 - float3(Eps, 0.0, 0.0))) * Inv2Eps;
    float DP2DY = (FluidGradientNoise3(P2 + float3(0.0, Eps, 0.0)) - FluidGradientNoise3(P2 - float3(0.0, Eps, 0.0))) * Inv2Eps;

    return float3(DP2DY - DP1DZ, DP0DZ - DP2DX, DP1DX - DP0DY);
}

float3 FluidCurlFbm2(float3 P)
{
    return FluidCurlNoise3(P) + 0.55 * FluidCurlNoise3(P * 2.25 + float3(11.4, 3.2, -6.8));
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
    // Exact 1:1 port of Experimental/FluidEditor/src/shaders-wgsl.js :: evaluateBlackbodyPalette
    float T = clamp(Temperature * TemperatureScale * 0.28, 0.0, 1.60);
    if (T <= 0.01) return float3(0.0, 0.0, 0.0);

    float3 Col = float3(0.0, 0.0, 0.0);
    float Gain = T * T * 1.65;
    if (PaletteIndex == 1) // CinemaNapalm
    {
        Col = lerp(float3(0.0, 0.0, 0.0), float3(0.65, 0.02, 0.002), smoothstep(0.0, 0.25, T));
        Col = lerp(Col, float3(1.00, 0.16, 0.005), smoothstep(0.20, 0.62, T));
        Col = lerp(Col, float3(1.00, 0.56, 0.06), smoothstep(0.58, 1.02, T));
        Col = lerp(Col, float3(1.00, 0.90, 0.48), smoothstep(0.95, 1.48, T));
    }
    else if (PaletteIndex == 2) // ThermobaricWhiteHot
    {
        Col = lerp(float3(0.0, 0.0, 0.0), float3(0.72, 0.12, 0.01), smoothstep(0.0, 0.18, T));
        Col = lerp(Col, float3(1.00, 0.45, 0.08), smoothstep(0.15, 0.42, T));
        Col = lerp(Col, float3(1.00, 0.90, 0.55), smoothstep(0.38, 0.75, T));
        Col = lerp(Col, float3(1.25, 1.22, 1.35), smoothstep(0.70, 1.22, T));
        Gain = T * T * 2.10;
    }
    else if (PaletteIndex == 3) // WildfireCopperEmerald
    {
        Col = lerp(float3(0.0, 0.0, 0.0), float3(0.03, 0.35, 0.14), smoothstep(0.0, 0.22, T));
        Col = lerp(Col, float3(0.10, 0.92, 0.38), smoothstep(0.18, 0.54, T));
        Col = lerp(Col, float3(0.62, 1.00, 0.52), smoothstep(0.50, 0.90, T));
        Col = lerp(Col, float3(0.95, 1.00, 0.92), smoothstep(0.84, 1.36, T));
        Gain = T * T * 1.70;
    }
    else if (PaletteIndex == 4) // CherenkovBluePlasma
    {
        Col = lerp(float3(0.0, 0.0, 0.0), float3(0.04, 0.10, 0.62), smoothstep(0.0, 0.20, T));
        Col = lerp(Col, float3(0.12, 0.52, 1.00), smoothstep(0.16, 0.50, T));
        Col = lerp(Col, float3(0.58, 0.88, 1.00), smoothstep(0.46, 0.88, T));
        Col = lerp(Col, float3(0.94, 0.98, 1.08), smoothstep(0.82, 1.35, T));
        Gain = T * T * 1.85;
    }
    else if (PaletteIndex == 5) // ArcaneVoidMagenta
    {
        Col = lerp(float3(0.0, 0.0, 0.0), float3(0.38, 0.02, 0.48), smoothstep(0.0, 0.20, T));
        Col = lerp(Col, float3(0.88, 0.14, 0.98), smoothstep(0.16, 0.52, T));
        Col = lerp(Col, float3(1.00, 0.52, 0.98), smoothstep(0.48, 0.90, T));
        Col = lerp(Col, float3(1.00, 0.92, 1.00), smoothstep(0.84, 1.36, T));
        Gain = T * T * 1.75;
    }
    else // PlanckianBlackbody (0)
    {
        Col = lerp(float3(0.0, 0.0, 0.0), float3(0.55, 0.03, 0.005), smoothstep(0.0, 0.22, T));
        Col = lerp(Col, float3(1.00, 0.24, 0.01), smoothstep(0.18, 0.52, T));
        Col = lerp(Col, float3(1.00, 0.68, 0.10), smoothstep(0.48, 0.88, T));
        Col = lerp(Col, float3(1.00, 0.96, 0.78), smoothstep(0.82, 1.35, T));
    }

    return Col * Gain;
}
