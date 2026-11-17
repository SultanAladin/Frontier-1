/*============================================================================================================================================
                                                        VOLUMETRICFOGCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 GPU Atmospheric Height Fog, Spectral Aerial Perspective & Wind-Advected Multi-Shape LocalFogVolumes

static const float FogPiConst = 3.14159265359;

struct GpuLocalFogVolume
{
    float4 CenterShape;          // xyz = CenterWorld, w = Shape (0=Box,1=Sphere,2=Ellipsoid,3=Cylinder,4=Cone,5=Diamond,6=Prism)
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
    float4            FogMeta;       // x = LocalFogCount (0..8), y = AerialScaleHeightM, z = Softness, w = Pad
    GpuLocalFogVolume LocalFogs[8];
};

float HashFogInt3Gpu(int IX, int IY, int IZ)
{
    uint N = uint(IX * 374761393 + IY * 668265263 + IZ * 2147483647);
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
    float Sum  = 0.0;
    float Amp  = 0.54;
    float Freq = 1.0;
    for (uint Oct = 0u; Oct < 4u; ++Oct)
    {
        Sum  += Amp * LatticeNoiseFog3Gpu(P * Freq);
        Freq *= 2.03;
        Amp  *= 0.46;
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
    else // Prism
    {
        float Sides = clamp(PrismSides, 3.0, 16.0);
        float Theta = atan2(N.y, N.x);
        float Sec   = 6.2831853 / Sides;
        float ModA  = (Theta - Sec * floor(Theta / Sec)) - 0.5 * Sec;
        float Rad   = length(N.xy) * cos(ModA) / max(0.25, cos(0.5 * Sec));
        NormDist    = max(Rad, abs(N.z));
    }

    float Soft = clamp(Softness, 0.04, 0.85);
    return 1.0 - smoothstep(max(0.0, 1.0 - Soft), 1.0, NormDist);
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

    // 3. Wind-Driven Multi-Shape LocalFogVolumes (up to 8)
    uint LocalCount = min(8u, uint(Fog.FogMeta.x + 0.5));
    float Softness  = clamp(Fog.FogMeta.z, 0.04, 0.85);
    float3 InvD     = 1.0 / select(abs(RayDir) > float3(1.0e-6), RayDir, float3(1.0e-6));

    for (uint I = 0u; I < LocalCount; ++I)
    {
        GpuLocalFogVolume Vol = Fog.LocalFogs[I];
        if (Vol.DensityCovScaleSides.x <= 1.0e-4 || Vol.DensityCovScaleSides.y <= 1.0e-4) continue;

        float3 Center   = Vol.CenterShape.xyz;
        float3 HalfExt  = Vol.HalfExtentYaw.xyz;
        float  MaxExt   = max(HalfExt.x, max(HalfExt.y, HalfExt.z)) * 1.45;
        float3 BoxMin   = Center - float3(MaxExt, MaxExt, HalfExt.z);
        float3 BoxMax   = Center + float3(MaxExt, MaxExt, HalfExt.z);
        float3 T0       = (BoxMin - RayOrigin) * InvD;
        float3 T1       = (BoxMax - RayOrigin) * InvD;
        float3 TN       = min(T0, T1);
        float3 TF       = max(T0, T1);
        float  TEnter   = max(0.05, max(TN.x, max(TN.y, TN.z)));
        float  TExit    = min(SafeDist, min(TF.x, min(TF.y, TF.z)));
        if (TExit <= TEnter) continue;

        float  Dt       = (TExit - TEnter) / 24.0;
        float  LScale   = Vol.DensityCovScaleSides.z;
        float  Cov      = clamp(Vol.DensityCovScaleSides.y, 0.01, 1.0);
        float  Thresh   = 1.0 - Cov;
        float  Phase    = 0.72 * EvaluateFogPhaseHGGpu(CosTheta, Vol.TintAniso.w) +
                          0.28 * EvaluateFogPhaseHGGpu(CosTheta, -0.18);
        uint   ShapeIdx = uint(Vol.CenterShape.w + 0.5);

        for (uint S = 0u; S < 24u; ++S)
        {
            float  T    = TEnter + (float(S) + Jitter) * Dt;
            float3 P    = RayOrigin + RayDir * T;
            float  Mask = EvaluateVolumeShapeMaskGpu(
                P, Center, HalfExt, ShapeIdx, Vol.HalfExtentYaw.w, Vol.DensityCovScaleSides.w, Softness);
            if (Mask <= 1.0e-4) continue;

            float3 AdvP = float3(
                P.x - Vol.WindVelCurl.x * TimeSec,
                P.y - Vol.WindVelCurl.y * TimeSec,
                P.z - Vol.WindVelCurl.z * TimeSec * 0.4);
            if (Vol.WindVelCurl.w > 1.0e-3)
            {
                float2 Dxy = P.xy - Center.xy;
                float  SwirlA = clamp(Vol.WindVelCurl.w * 0.12, -2.5, 2.5) * sin(TimeSec * 0.7 + 0.05 * length(Dxy));
                float  Cs = cos(SwirlA), Sn = sin(SwirlA);
                AdvP.x = Center.x + (Dxy.x * Cs - Dxy.y * Sn) - Vol.WindVelCurl.x * TimeSec * 0.35;
                AdvP.y = Center.y + (Dxy.x * Sn + Dxy.y * Cs) - Vol.WindVelCurl.y * TimeSec * 0.35;
            }

            float N   = FogFbmGpu3(AdvP * LScale);
            float Den = max(0.0, (N - Thresh) / max(0.15, Cov)) * Mask * Vol.DensityCovScaleSides.x * 0.075;
            if (Den > 1.0e-5)
            {
                float SegT    = exp(-Den * Dt);
                float Scatter = 1.0 - SegT;
                float HNorm   = clamp((P.z - (Center.z - HalfExt.z)) / max(0.2, 2.0 * HalfExt.z), 0.0, 1.0);
                float Amb     = 0.42 + 0.58 * HNorm;
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
