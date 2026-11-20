/*============================================================================================================================================
                                                        VOLUMETRICFOGCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 GPU Atmospheric Height Fog, Spectral Aerial Perspective & Wind-Advected Multi-Shape LocalFogVolumes

static const float FogPiConst = 3.14159265359;

struct GpuLocalFogVolume
{
    float4 CenterShape;          // xyz = CenterWorld, w = Shape (0=Box,1=Sphere,2=Ellipsoid,3=Cylinder,4=Cone,5=Diamond,6=Prism,7=HalfSphere)
    float4 HalfExtentYaw;        // xyz = HalfExtentM, w = YawRad
    float4 DensityCovScaleSides; // x = Density, y = Coverage, z = InvFeatureScale, w = float(PrismSides)
    float4 WindVelCurl;          // xyz = ResolvedWindVelMps, w = CurlScalar
    float4 TintAniso;            // xyz = FogTintLinear, w = AnisotropyG
};

struct VolumetricFogUniformBlock
{
    float4            HeightParams0; // x = Enabled, y = Density, z = FalloffHeightM, w = BaseAltM
    float4            HeightParams1; // xyz = HeightColour, w = SunScatterIntensity
    float4            AerialParams;  // x = Enabled, y = DensityMul, z = StartDistM, w = MieBlend
    float4            FogWindClock;  // x = HeightFollowWind, y = HeightWindVX, z = HeightWindVY, w = SimulationSeconds
    float4            FogMeta;       // x = LocalFogCount (0..8), y = AerialScaleHeightM, z = Softness, w = Reserved
    float4            GodRayParams0; // x = Enabled, y = Intensity, z = AnisotropyG, w = Density
    float4            GodRayParams1; // x = MaxDistanceM, y = SampleSteps, z = InteractWithClouds, w = InteractWithFog
    float4            GodRayTint;    // xyz = ShaftTint, w = Decay
    GpuLocalFogVolume LocalFogs[8];
};

float HashFogInt3Gpu(int IX, int IY, int IZ)
{
    uint N = uint(IX) * 374761393u + uint(IY) * 668265263u + uint(IZ) * 2147483647u;
    N = (N ^ (N >> 13u)) * 1274126177u;
    N = N ^ (N >> 16u);
    return float(N) * (1.0 / 4294967296.0);
}

float LatticeNoiseFog3Gpu(float3 P)
{
    int3 I   = int3(floor(P));
    float3 F = P - float3(I);
    float3 U = F * F * (3.0 - 2.0 * F);

    float N000 = HashFogInt3Gpu(I.x, I.y, I.z),         N100 = HashFogInt3Gpu(I.x + 1, I.y, I.z);
    float N010 = HashFogInt3Gpu(I.x, I.y + 1, I.z),     N110 = HashFogInt3Gpu(I.x + 1, I.y + 1, I.z);
    float N001 = HashFogInt3Gpu(I.x, I.y, I.z + 1),     N101 = HashFogInt3Gpu(I.x + 1, I.y, I.z + 1);
    float N011 = HashFogInt3Gpu(I.x, I.y + 1, I.z + 1), N111 = HashFogInt3Gpu(I.x + 1, I.y + 1, I.z + 1);

    return lerp(
        lerp(lerp(N000, N100, U.x), lerp(N010, N110, U.x), U.y),
        lerp(lerp(N001, N101, U.x), lerp(N011, N111, U.x), U.y),
        U.z);
}

float FogFbmGpu3(float3 P)
{
    float  Sum = 0.0;
    float  Amp = 0.54;
    float3 Q   = P;
    for (uint Oct = 0u; Oct < 4u; ++Oct)
    {
        Sum += Amp * LatticeNoiseFog3Gpu(Q);
        Q = float3(
             0.80 * Q.x - 0.48 * Q.y + 0.36 * Q.z,
             0.48 * Q.x + 0.86 * Q.y + 0.16 * Q.z,
            -0.36 * Q.x + 0.04 * Q.y + 0.93 * Q.z) * 2.03 + float3(7.1, -11.3, 5.7);
        Amp *= 0.46;
    }
    return Sum;
}

float EvaluateFogPhaseHGGpu(float CosTheta, float G)
{
    float CG  = clamp(G, -0.89, 0.89);
    float G2  = CG * CG;
    float Den = max(1.0e-3, 1.0 + G2 - 2.0 * CG * CosTheta);
    return (1.0 - G2) / (4.0 * FogPiConst * pow(Den, 1.5));
}

float EvaluateVolumeShapeMaskGpu(
    float3 P,
    float3 Center,
    float3 HalfExt,
    uint   Shape,
    float  YawRad,
    float  PrismSides,
    float  Softness)
{
    float3 H = max(float3(0.1), HalfExt);
    float3 D = P - Center;
    float  Cs = cos(YawRad);
    float  Sn = sin(YawRad);
    float3 L  = float3(D.x * Cs + D.y * Sn, -D.x * Sn + D.y * Cs, D.z);
    float3 N  = L / H;

    float NormDist = 0.0;
    if (Shape == 0u) // Box
    {
        NormDist = max(abs(N.x), max(abs(N.y), abs(N.z)));
    }
    else if (Shape == 1u) // Sphere
    {
        float MinR = min(H.x, min(H.y, H.z));
        NormDist = length(L) / MinR;
    }
    else if (Shape == 2u) // Ellipsoid
    {
        NormDist = length(N);
    }
    else if (Shape == 3u) // Cylinder
    {
        NormDist = max(length(N.xy), abs(N.z));
    }
    else if (Shape == 4u) // Cone
    {
        float T   = clamp(0.5 * (1.0 - N.z), 0.0, 1.0);
        float Rad = length(N.xy) / max(0.08, T);
        NormDist  = max(Rad, abs(N.z));
    }
    else if (Shape == 5u) // Diamond
    {
        NormDist = abs(N.x) + abs(N.y) + abs(N.z);
    }
    else if (Shape == 6u) // Prism
    {
        float Sides = clamp(PrismSides, 3.0, 16.0);
        float Theta = atan2(N.y, N.x);
        float Sec   = 6.2831853 / Sides;
        float ModA  = (Theta - Sec * floor(Theta / Sec)) - 0.5 * Sec;
        float Rad   = length(N.xy) * cos(ModA) / max(0.25, cos(0.5 * Sec));
        NormDist    = max(Rad, abs(N.z));
    }
    else // HalfSphere (7u)
    {
        float BaseDepth = (L.z < 0.0) ? (-L.z / max(0.05, 0.14 * H.z)) : 0.0;
        float DomeRad   = (L.z >= 0.0) ? length(N) : length(N.xy);
        NormDist        = max(DomeRad, BaseDepth);
    }

    float Soft = clamp(Softness, 0.04, 0.92);
    float Mask = 1.0 - smoothstep(max(0.0, 1.0 - Soft), 1.0, NormDist);
    if (Shape == 7u)
    {
        float RadialDecay = pow(saturate(1.0 - NormDist * 0.82), 0.72);
        Mask *= (0.35 + 0.65 * RadialDecay);
    }
    return Mask;
}

float3 AdvectFogPointBackwardGpu(
    VolumetricFogUniformBlock Fog,
    float3                    P,
    float                     TimeSec,
    float4                    BaseWindVelCurl,
    out float3                OutVel,
    out float                 OutCurl)
{
    OutVel  = BaseWindVelCurl.xyz;
    OutCurl = BaseWindVelCurl.w;
    if (abs(TimeSec) <= 1.0e-4) return P;
    float AdvSec = TimeSec * 0.72;
    return P - OutVel * AdvSec;
}

float EvaluateSingleLocalFogDensityGpu(
    VolumetricFogUniformBlock Fog,
    GpuLocalFogVolume         Vol,
    float3                    P,
    float                     Softness,
    float                     TimeSec,
    out float                 OutCurlHighlight)
{
    OutCurlHighlight = 0.0;
    uint   ShapeIdx  = uint(Vol.CenterShape.w + 0.5);
    float3 Center    = Vol.CenterShape.xyz;
    float3 HalfExt   = Vol.HalfExtentYaw.xyz;
    float  Mask      = EvaluateVolumeShapeMaskGpu(
        P, Center, HalfExt, ShapeIdx, Vol.HalfExtentYaw.w, Vol.DensityCovScaleSides.w, Softness);
    if (Mask <= 1.0e-4) return 0.0;

    float3 Vel;
    float  Curl;
    float3 AdvP = AdvectFogPointBackwardGpu(Fog, P, TimeSec, Vol.WindVelCurl, Vel, Curl);
    float Speed = length(Vel);

    float LScale  = Vol.DensityCovScaleSides.z;
    float Cov     = clamp(Vol.DensityCovScaleSides.y, 0.01, 1.0);
    float Thresh  = 1.0 - Cov;
    float N       = FogFbmGpu3(AdvP * LScale);
    float BaseDen = max(0.0, (N - Thresh) / max(0.15, Cov));

    if (Speed > 0.25)
    {
        float FilamentNoise = LatticeNoiseFog3Gpu(float3(AdvP.x * LScale * 1.85, AdvP.y * LScale * 1.85, AdvP.z * LScale * 1.45));
        float CurlBoost     = 1.0 + clamp(Curl * 0.26, 0.0, 1.15) * (0.55 + 0.90 * FilamentNoise);
        float JetClear      = 1.0 - clamp((Speed - 6.0) * 0.034, 0.0, 0.62) * (0.55 + 0.45 * (1.0 - FilamentNoise));
        BaseDen *= clamp(CurlBoost * JetClear, 0.22, 2.15);
        OutCurlHighlight = clamp(Curl * 0.14, 0.0, 0.40);
    }

    return BaseDen * Mask * Vol.DensityCovScaleSides.x * 0.075;
}

float EvaluateFogMediumDensityAtPointGpu(VolumetricFogUniformBlock Fog, float3 WorldPos)
{
    float Sigma = 0.0;
    if (Fog.HeightParams0.x > 0.5 && Fog.HeightParams0.y > 1.0e-6)
    {
        float Falloff = max(10.0, Fog.HeightParams0.z);
        float RelZ    = max(0.0, (WorldPos.z - Fog.HeightParams0.w) / Falloff);
        Sigma += clamp(Fog.HeightParams0.y, 0.0, 0.25) * exp(-RelZ);
    }
    uint LocalCount = min(8u, uint(Fog.FogMeta.x + 0.5));
    float Softness  = clamp(Fog.FogMeta.z, 0.04, 0.92);
    float TimeSec   = Fog.FogWindClock.w;
    for (uint I = 0u; I < LocalCount; ++I)
    {
        GpuLocalFogVolume Vol = Fog.LocalFogs[I];
        if (Vol.DensityCovScaleSides.x <= 1.0e-4 || Vol.DensityCovScaleSides.y <= 1.0e-4) continue;
        float3 Center  = Vol.CenterShape.xyz;
        float3 HalfExt = Vol.HalfExtentYaw.xyz;
        float  MaxExt  = max(HalfExt.x, max(HalfExt.y, HalfExt.z)) * 1.45;
        uint   ShapeIdx = uint(Vol.CenterShape.w + 0.5);
        float  MinZ    = (ShapeIdx == 7u) ? (Center.z - 0.14 * HalfExt.z) : (Center.z - HalfExt.z);
        if (WorldPos.x < Center.x - MaxExt || WorldPos.x > Center.x + MaxExt ||
            WorldPos.y < Center.y - MaxExt || WorldPos.y > Center.y + MaxExt ||
            WorldPos.z < MinZ || WorldPos.z > Center.z + HalfExt.z)
        {
            continue;
        }
        float DummyCurl = 0.0;
        Sigma += EvaluateSingleLocalFogDensityGpu(Fog, Vol, WorldPos, Softness, TimeSec, DummyCurl);
    }
    return Sigma;
}

float EvaluateHeightFogOpticalDepthGpu(float OriginZ, float DirZ, float SegM, float Density, float FalloffM, float BaseAltM)
{
    if (Density <= 1.0e-6 || SegM <= 1.0e-4) return 0.0;
    float SafeFalloff = max(10.0, FalloffM);
    float StartRel    = max(-4.0, (OriginZ - BaseAltM) / SafeFalloff);
    float StartRho    = clamp(Density, 0.0, 0.25) * exp(-max(0.0, StartRel));
    float K           = (DirZ * SegM) / SafeFalloff;
    if (abs(K) < 1.0e-3)
    {
        return StartRho * SegM * (1.0 - 0.5 * K + (K * K) / 6.0);
    }
    float SafeK = clamp(K, -24.0, 24.0);
    return max(0.0, StartRho * SegM * ((1.0 - exp(-SafeK)) / SafeK));
}

float3 EvaluateVolumetricFogGpu(
    VolumetricFogUniformBlock Fog,
    float3                    RayOrigin,
    float3                    RayDir,
    float                     MaxDistM,
    float3                    SunDir,
    float3                    SolarIrrad,
    float3                    SkyColor,
    float2                    PixelCoord,
    out float                 OutTransmittance)
{
    OutTransmittance = 1.0;
    float3 InScatter = float3(0.0, 0.0, 0.0);
    float  SafeDist  = max(0.0, MaxDistM);
    if (SafeDist <= 1.0e-3) return InScatter;

    float CosTheta = dot(RayDir, SunDir);
    float Ign      = frac(52.9829189 * frac(dot(PixelCoord, float2(0.06711056, 0.00583715))));
    float Jitter   = 0.5 + 0.75 * (Ign - 0.5);
    float TimeSec  = Fog.FogWindClock.w;

    // 1. Exponential Height Fog
    if (Fog.HeightParams0.x > 0.5 && Fog.HeightParams0.y > 1.0e-6)
    {
        float TauH = EvaluateHeightFogOpticalDepthGpu(
            RayOrigin.z, RayDir.z, SafeDist, Fog.HeightParams0.y, Fog.HeightParams0.z, Fog.HeightParams0.w);
        if (Fog.FogWindClock.x > 0.5)
        {
            float3 ProbeP = RayOrigin + RayDir * min(SafeDist * 0.35, 80.0);
            float  Ripple = 0.88 + 0.24 * LatticeNoiseFog3Gpu(float3(
                (ProbeP.x - Fog.FogWindClock.y * TimeSec) * 0.03,
                (ProbeP.y - Fog.FogWindClock.z * TimeSec) * 0.03,
                TimeSec * 0.15));
            TauH *= Ripple;
        }
        float TransH  = exp(-TauH);
        float ScatH   = 1.0 - TransH;
        float MieHg   = EvaluateFogPhaseHGGpu(CosTheta, 0.62) * Fog.HeightParams1.w * 0.45;
        float3 FogLit = Fog.HeightParams1.xyz * (SkyColor * 0.62 + SolarIrrad * (0.045 + MieHg));
        InScatter        += OutTransmittance * FogLit * ScatH;
        OutTransmittance *= TransH;
    }

    // 2. Spectral Aerial Perspective
    if (Fog.AerialParams.x > 0.5 && Fog.AerialParams.y > 1.0e-5)
    {
        float Travel = max(0.0, SafeDist - max(0.0, Fog.AerialParams.z));
        if (Travel > 1.0e-3)
        {
            float AltF    = exp(-max(0.0, RayOrigin.z) / max(100.0, Fog.FogMeta.y));
            float SigmaA  = Fog.AerialParams.y * 0.00022 * AltF;
            float TransA  = exp(-SigmaA * Travel);
            float ScatA   = 1.0 - TransA;
            float MieB    = saturate(Fog.AerialParams.w);
            float MiePh   = EvaluateFogPhaseHGGpu(CosTheta, 0.72);
            float3 AerCol = SkyColor * (1.0 - 0.30 * MieB) + SolarIrrad * MieB * (0.04 + MiePh * 0.42);
            InScatter        += OutTransmittance * AerCol * ScatA;
            OutTransmittance *= TransA;
        }
    }

    // 3. Wind-Driven Multi-Shape LocalFogVolumes (up to 8, 32 steps)
    uint LocalCount = min(8u, uint(Fog.FogMeta.x + 0.5));
    float Softness  = clamp(Fog.FogMeta.z, 0.04, 0.92);
    float3 InvD     = 1.0 / select(abs(RayDir) > float3(1.0e-6), RayDir, float3(1.0e-6));

    for (uint I = 0u; I < LocalCount; ++I)
    {
        GpuLocalFogVolume Vol = Fog.LocalFogs[I];
        if (Vol.DensityCovScaleSides.x <= 1.0e-4 || Vol.DensityCovScaleSides.y <= 1.0e-4) continue;

        float3 Center   = Vol.CenterShape.xyz;
        float3 HalfExt  = Vol.HalfExtentYaw.xyz;
        float  MaxExt   = max(HalfExt.x, max(HalfExt.y, HalfExt.z)) * 1.45;
        uint   ShapeIdx = uint(Vol.CenterShape.w + 0.5);
        float  MinZOff  = (ShapeIdx == 7u) ? (0.14 * HalfExt.z) : HalfExt.z;
        float3 BoxMin   = Center - float3(MaxExt, MaxExt, MinZOff);
        float3 BoxMax   = Center + float3(MaxExt, MaxExt, HalfExt.z);
        float3 T0       = (BoxMin - RayOrigin) * InvD;
        float3 T1       = (BoxMax - RayOrigin) * InvD;
        float3 TN       = min(T0, T1);
        float3 TF       = max(T0, T1);
        float  TEnter   = max(0.05, max(TN.x, max(TN.y, TN.z)));
        float  TExit    = min(SafeDist, min(TF.x, min(TF.y, TF.z)));
        if (TExit <= TEnter) continue;

        float  Dt       = (TExit - TEnter) / 32.0;
        float  Phase    = 0.72 * EvaluateFogPhaseHGGpu(CosTheta, Vol.TintAniso.w) +
                          0.28 * EvaluateFogPhaseHGGpu(CosTheta, -0.18);

        for (uint S = 0u; S < 32u; ++S)
        {
            float  T    = TEnter + (float(S) + Jitter) * Dt;
            float3 P    = RayOrigin + RayDir * T;
            float  CurlHighlight = 0.0;
            float  Den  = EvaluateSingleLocalFogDensityGpu(Fog, Vol, P, Softness, TimeSec, CurlHighlight);
            if (Den > 1.0e-5)
            {
                float SegT    = exp(-Den * Dt);
                float Scatter = 1.0 - SegT;
                float HNorm   = clamp((P.z - BoxMin.z) / max(0.2, BoxMax.z - BoxMin.z), 0.0, 1.0);
                float Amb     = 0.42 + 0.58 * HNorm + CurlHighlight * 0.25;
                InScatter        += OutTransmittance * Vol.TintAniso.xyz * (SolarIrrad * Phase * 1.45 + SkyColor * Amb * 0.75) * Scatter;
                OutTransmittance *= SegT;
                if (OutTransmittance < 0.01) break;
            }
        }
        if (OutTransmittance < 0.01) break;
    }

    return InScatter;
}

#ifndef FRONTIER_ATMOSPHERE_EMBEDDED
[[vk::binding(0, 0)]] ConstantBuffer<VolumetricFogUniformBlock> FogUniforms;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4>               OutFogRgba;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint W = 64u, H = 32u;
    if (DispatchThreadID.x >= W || DispatchThreadID.y >= H) return;
    float2 Uv = (float2(DispatchThreadID.xy) + float2(0.5, 0.5)) / float2(float(W), float(H));
    float  Az = (Uv.x - 0.5) * 6.2831853;
    float  El = (Uv.y - 0.5) * 3.1415926;
    float3 Rd = float3(cos(El) * cos(Az), cos(El) * sin(Az), sin(El));
    float  Tr = 1.0;
    float3 Sc = EvaluateVolumetricFogGpu(
        FogUniforms, float3(0.0, 0.0, 2.0), Rd, 2000.0, float3(0.4, 0.5, 0.75),
        float3(1.0, 0.95, 0.9), float3(0.25, 0.40, 0.68), float2(DispatchThreadID.xy), Tr);
    OutFogRgba[DispatchThreadID.y * W + DispatchThreadID.x] = float4(Sc, Tr);
}
#endif
