/*============================================================================================================================================
                                                    SKYVIEWTRANSMITTANCECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Bruneton/Hillaire Rayleigh + Mie + Ozone Atmospheric Transmittance & 96x48 SkyView LUT Compute Shader

static const float AtmoPiConst   = 3.14159265359;
static const float AtmoTauConst  = 6.28318530718;
static const float ViewerAltM  = 100.0;

struct AtmosphericSkyUniformBlock
{
    float4 SunDirectionIntensity; // xyz = UnitSunDir, w = SolarIntensityScale
    float4 ScatteringStrengths;   // x = Rayleigh, y = Mie, z = MieAnisotropy, w = Ozone
    float4 PlanetDimensions;      // x = PlanetRadiusM, y = AtmoThicknessM, z = RayleighScaleH, w = MieScaleH
    float4 SkyTintBrightness;     // xyz = SkyTint, w = SkyBrightnessMultiplier
    float4 GroundAlbedoLimbGlow;  // xyz = GroundAlbedo, w = LimbGlowIntensity
    uint4  LutDimensions;         // x = SkyViewW (96), y = SkyViewH (48), z = Pad, w = Pad
};

float2 IntersectOriginSphereAtmo(float OriginZ, float DirZ, float Radius)
{
    float B    = OriginZ * DirZ;
    float C    = OriginZ * OriginZ - Radius * Radius;
    float Disc = B * B - C;
    if (Disc < 0.0) return float2(-1.0, -1.0);
    float Root = sqrt(Disc);
    return float2(-B - Root, -B + Root);
}

float2 IntersectVecSphereAtmo(float3 Pos, float3 Dir, float Radius)
{
    float B    = dot(Pos, Dir);
    float C    = dot(Pos, Pos) - Radius * Radius;
    float Disc = B * B - C;
    if (Disc < 0.0) return float2(-1.0, -1.0);
    float Root = sqrt(Disc);
    return float2(-B - Root, -B + Root);
}

float3 ComputeAtmoTransmittanceAlongRay(
    float3 StartPos,
    float3 RayDir,
    float  PlanetR,
    float  AtmoR,
    float  RayMul,
    float  MieMul,
    float  OzoneMul,
    float  RayH,
    float  MieH)
{
    float2 GHit = IntersectVecSphereAtmo(StartPos, RayDir, PlanetR);
    if (GHit.x > 0.0) return float3(0.0, 0.0, 0.0);

    float2 AHit = IntersectVecSphereAtmo(StartPos, RayDir, AtmoR);
    if (AHit.y <= 0.0) return float3(1.0, 1.0, 1.0);

    float  MaxT = min(AHit.y, 180000.0);
    float  Step = MaxT / 16.0;
    float3 RayBeta   = float3(5.802e-6, 13.558e-6, 33.1e-6);
    float3 OzoneBeta = float3(0.650e-6, 1.881e-6, 0.085e-6);
    float3 OptDepth  = float3(0.0, 0.0, 0.0);

    for (uint I = 0u; I < 16u; ++I)
    {
        float  T   = (float(I) + 0.5) * Step;
        float3 P   = StartPos + RayDir * T;
        float  Alt = max(0.0, length(P) - PlanetR);
        float  DenR = exp(-Alt / RayH) * RayMul;
        float  DenM = exp(-Alt / MieH) * MieMul;
        float  DenO = max(0.0, 1.0 - abs(Alt - 25000.0) / 15000.0) * OzoneMul;
        OptDepth += (RayBeta * DenR + float3(4.40e-6 * DenM) + OzoneBeta * DenO) * Step;
    }
    return exp(-OptDepth);
}

float3 EvaluateSkyViewLutSample(StructuredBuffer<float4> SkyLut, uint WidthPx, uint HeightPx, float3 Dir)
{
    if (WidthPx < 2u || HeightPx < 2u) return float3(0.16, 0.23, 0.36);

    float Az   = atan2(Dir.y, Dir.x);
    float El   = asin(clamp(Dir.z, -1.0, 1.0));
    float U    = frac(Az / AtmoTauConst + 0.5);
    float SignE = (El >= 0.0) ? 1.0 : -1.0;
    float V    = clamp(0.5 - 0.5 * SignE * sqrt(abs(El) / (AtmoPiConst * 0.5)), 0.0, 1.0);

    float FX = U * float(WidthPx) - 0.5;
    float FY = V * float(HeightPx) - 0.5;
    int   IX0 = ((int(floor(FX)) % int(WidthPx)) + int(WidthPx)) % int(WidthPx);
    int   IX1 = (IX0 + 1) % int(WidthPx);
    int   IY0 = clamp(int(floor(FY)), 0, int(HeightPx) - 1);
    int   IY1 = clamp(IY0 + 1, 0, int(HeightPx) - 1);
    float TX  = FX - floor(FX);
    float TY  = FY - floor(FY);

    float3 C00 = SkyLut[uint(IY0) * WidthPx + uint(IX0)].xyz;
    float3 C10 = SkyLut[uint(IY0) * WidthPx + uint(IX1)].xyz;
    float3 C01 = SkyLut[uint(IY1) * WidthPx + uint(IX0)].xyz;
    float3 C11 = SkyLut[uint(IY1) * WidthPx + uint(IX1)].xyz;
    return lerp(lerp(C00, C10, TX), lerp(C01, C11, TX), TY);
}

float3 ComputePhysicalSkyTexel(AtmosphericSkyUniformBlock Uni, float2 Uv)
{
    float3 SunDir   = normalize(Uni.SunDirectionIntensity.xyz);
    float  SolarScale = max(0.0, Uni.SunDirectionIntensity.w) * 0.075;
    float  RayMul   = max(0.0, Uni.ScatteringStrengths.x);
    float  MieMul   = max(0.0, Uni.ScatteringStrengths.y);
    float  MieG     = clamp(Uni.ScatteringStrengths.z, 0.0, 0.99);
    float  OzoneMul = max(0.0, Uni.ScatteringStrengths.w);
    float  PlanetR  = max(500000.0, Uni.PlanetDimensions.x);
    float  AtmoTop  = max(10000.0, Uni.PlanetDimensions.y);
    float  RayH     = max(500.0, Uni.PlanetDimensions.z);
    float  MieH     = max(200.0, Uni.PlanetDimensions.w);
    float  AtmoR    = PlanetR + AtmoTop;

    float Az = (Uv.x - 0.5) * AtmoTauConst;
    float SV = (0.5 - Uv.y) * 2.0;
    float El = ((SV >= 0.0) ? 1.0 : -1.0) * SV * SV * (AtmoPiConst * 0.5);
    float CosE = cos(El);
    float3 RawDir = float3(CosE * cos(Az), CosE * sin(Az), sin(El));

    bool   GroundRay = (RawDir.z < 0.0);
    float  RdZ       = GroundRay ? 0.002 : RawDir.z;
    float  RdHoriz   = sqrt(max(1.0e-8, RawDir.x * RawDir.x + RawDir.y * RawDir.y));
    float  RdScale   = sqrt(max(0.0, 1.0 - RdZ * RdZ)) / RdHoriz;
    float3 Rd        = GroundRay ? float3(RawDir.x * RdScale, RawDir.y * RdScale, RdZ) : RawDir;

    float  OrigZ     = PlanetR + ViewerAltM;
    float2 AExit     = IntersectOriginSphereAtmo(OrigZ, Rd.z, AtmoR);
    float  MaxDist   = min(AExit.y, 180000.0);
    float  CosTheta  = dot(Rd, SunDir);

    float  PhaseR    = (3.0 / (16.0 * AtmoPiConst)) * (1.0 + CosTheta * CosTheta);
    float  G2        = MieG * MieG;
    float  Denom     = max(1.0e-3, 1.0 + G2 - 2.0 * MieG * CosTheta);
    float  PhaseM    = (3.0 / (8.0 * AtmoPiConst)) * ((1.0 - G2) * (1.0 + CosTheta * CosTheta)) /
                       ((2.0 + G2) * pow(Denom, 1.5));

    float3 RayBeta   = float3(5.802e-6, 13.558e-6, 33.1e-6);
    float3 OzoneBeta = float3(0.650e-6, 1.881e-6, 0.085e-6);
    float3 OverheadT = ComputeAtmoTransmittanceAlongRay(
        float3(0.0, 0.0, OrigZ), normalize(float3(SunDir.x, SunDir.y, max(0.05, SunDir.z))),
        PlanetR, AtmoR, RayMul, MieMul, OzoneMul, RayH, MieH);

    float3 TAccum   = float3(1.0, 1.0, 1.0);
    float3 RadAccum = float3(0.0, 0.0, 0.0);
    float3 MsAccum  = float3(0.0, 0.0, 0.0);
    float  Step     = max(1.0, MaxDist / 18.0);
    float  AvgGnd   = (Uni.GroundAlbedoLimbGlow.x + Uni.GroundAlbedoLimbGlow.y + Uni.GroundAlbedoLimbGlow.z) * 0.333333;
    float  MsBounce = 0.24 + 0.18 * AvgGnd;

    for (uint S = 0u; S < 18u; ++S)
    {
        float  T   = (float(S) + 0.5) * Step;
        float3 P   = float3(0.0, 0.0, OrigZ) + Rd * T;
        float  Alt = max(0.0, length(P) - PlanetR);

        float  DenR = exp(-Alt / RayH) * RayMul;
        float  DenM = exp(-Alt / MieH) * MieMul;
        float  DenO = max(0.0, 1.0 - abs(Alt - 25000.0) / 15000.0) * OzoneMul;

        float3 SRay = RayBeta * DenR;
        float  SMie = 3.996e-6 * DenM;
        float  EMie = 4.40e-6 * DenM;

        float3 Ext  = max(float3(1.0e-7), SRay + float3(EMie) + OzoneBeta * DenO);
        float3 SegT = exp(-Ext * Step);
        float3 IntS = (float3(1.0) - SegT) / Ext;

        float3 SunT = ComputeAtmoTransmittanceAlongRay(P, SunDir, PlanetR, AtmoR, RayMul, MieMul, OzoneMul, RayH, MieH);
        float3 Scat = SRay * PhaseR + float3(SMie * PhaseM);

        RadAccum += TAccum * Scat * SunT * IntS;
        MsAccum  += TAccum * (SRay + float3(SMie)) * (1.0 / (4.0 * AtmoPiConst)) *
                    (SunT * 0.65 + OverheadT * 0.35) * MsBounce * IntS;
        TAccum   *= SegT;
    }

    float LimbFactor = exp(-max(0.0, Rd.z) * 7.5);
    float GlowScale  = 1.0 + (Uni.GroundAlbedoLimbGlow.w - 1.0) * LimbFactor * 0.55;
    float SunElevDeg = asin(clamp(SunDir.z, -1.0, 1.0)) * (180.0 / AtmoPiConst);
    float DayVis     = clamp((SunElevDeg + 6.0) / 14.0, 0.0, 1.0);
    float3 NightAir  = float3(0.0007, 0.0010, 0.0022) + float3(0.0009, 0.0013, 0.0024) * LimbFactor;

    float3 OutRgb = ((RadAccum + MsAccum) * SolarScale * GlowScale + NightAir * (1.0 - DayVis * 0.9)) *
                    Uni.SkyTintBrightness.xyz * Uni.SkyTintBrightness.w;

    if (GroundRay)
    {
        float SunUp = max(0.0, SunDir.z);
        float3 GndCol = Uni.GroundAlbedoLimbGlow.xyz *
                        (OverheadT * SunUp * 0.8 + OutRgb * 0.35 + float3(0.24 * SunUp * 0.08 + 0.0005));
        float Blend = clamp(-RawDir.z * 3.5, 0.0, 1.0);
        OutRgb = lerp(OutRgb, GndCol, Blend);
    }
    return max(float3(0.0), OutRgb);
}

#ifndef FRONTIER_ATMOSPHERE_EMBEDDED
[[vk::binding(0, 0)]] ConstantBuffer<AtmosphericSkyUniformBlock> SkyUniforms;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4>               OutSkyViewLut;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint W = max(1u, SkyUniforms.LutDimensions.x);
    uint H = max(1u, SkyUniforms.LutDimensions.y);
    if (DispatchThreadID.x >= W || DispatchThreadID.y >= H) return;

    float2 Uv = (float2(DispatchThreadID.xy) + float2(0.5, 0.5)) / float2(float(W), float(H));
    float3 SkyCol = ComputePhysicalSkyTexel(SkyUniforms, Uv);
    OutSkyViewLut[DispatchThreadID.y * W + DispatchThreadID.x] = float4(SkyCol, 1.0);
}
#endif
