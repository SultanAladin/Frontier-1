/*============================================================================================================================================
                                                    SKYVIEWTRANSMITTANCECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Stage 3 Physical Atmosphere: 256x64 Spherical-Horizon Transmittance + 32x32 Multi-Scatter + 192x108 Geometric-Limb SkyView LUTs

static const float AtmoPiConst      = 3.14159265359;
static const float AtmoTauConst     = 6.28318530718;
static const float AtmoInvFourPi    = 0.07957747155;
static const float ViewerAltM       = 100.0;

static const uint AtmoTransW        = 256u;
static const uint AtmoTransH        = 64u;
static const uint AtmoMultiW        = 32u;
static const uint AtmoMultiH        = 32u;
static const uint AtmoSkyDefW       = 192u;
static const uint AtmoSkyDefH       = 108u;

static const uint AtmoSkyOffset     = 0u;
static const uint AtmoTransOffset   = 20736u; // 192 * 108
static const uint AtmoMultiOffset   = 37120u; // 20736 + 256 * 64

struct AtmosphericSkyUniformBlock
{
    float4 SunDirectionIntensity; // xyz = UnitSunDir, w = SolarIntensityScale
    float4 ScatteringStrengths;   // x = Rayleigh, y = Mie, z = MieAnisotropy, w = Ozone
    float4 PlanetDimensions;      // x = PlanetRadiusM, y = AtmoThicknessM, z = RayleighScaleH, w = MieScaleH
    float4 SkyTintBrightness;     // xyz = SkyTint, w = SkyBrightnessMultiplier
    float4 GroundAlbedoLimbGlow;  // xyz = GroundAlbedo, w = LimbGlowIntensity
    uint4  LutDimensions;         // x = SkyViewW (192), y = SkyViewH (108), z = ObserverAltitudeMeters, w = OrbitalFlag
};

struct AtmoMediumKm
{
    float3 RayScatter;
    float  MieScatter;
    float3 Extinction;
};

float AtmoComputeHorizonKm(float GroundKm, float TopKm)
{
    return sqrt(max(0.0, (TopKm - GroundKm) * (TopKm + GroundKm)));
}

bool AtmoQuerySphereBlockedKm(float RadiusKm, float CosinePolar, float TargetRadiusKm)
{
    if (CosinePolar >= 0.0) return false;
    float Disc = RadiusKm * RadiusKm * CosinePolar * CosinePolar -
                 (RadiusKm - TargetRadiusKm) * (RadiusKm + TargetRadiusKm);
    return (Disc >= 0.0) && (RadiusKm > TargetRadiusKm + 1.0e-4);
}

float AtmoIntersectUpperKm(float RadiusKm, float CosinePolar, float TopKm)
{
    float Disc = max(
        0.0,
        RadiusKm * RadiusKm * CosinePolar * CosinePolar +
            (TopKm - RadiusKm) * (TopKm + RadiusKm));
    return max(0.0, -RadiusKm * CosinePolar + sqrt(Disc));
}

float AtmoIntersectLowerKm(float RadiusKm, float CosinePolar, float GroundKm, out bool OutHits)
{
    float Disc = RadiusKm * RadiusKm * CosinePolar * CosinePolar -
                 (RadiusKm - GroundKm) * (RadiusKm + GroundKm);
    if (CosinePolar < 0.0 && Disc >= 0.0)
    {
        float Hit = -RadiusKm * CosinePolar - sqrt(Disc);
        if (Hit > 0.0)
        {
            OutHits = true;
            return Hit;
        }
    }
    OutHits = false;
    return 0.0;
}

float AtmoComputeLimbPolarAngle(float ViewerRadiusKm, float GroundKm)
{
    float SafeR         = max(GroundKm + 1.0e-4, ViewerRadiusKm);
    float TangentHeight = sqrt(max(0.0, (SafeR - GroundKm) * (SafeR + GroundKm)));
    return acos(clamp(-TangentHeight / SafeR, -1.0, 1.0));
}

AtmoMediumKm AtmoEvaluateMediumKm(float AltitudeKm, AtmosphericSkyUniformBlock Uni)
{
    float AltM     = max(0.0, AltitudeKm * 1000.0);
    float RayMul   = max(0.0, Uni.ScatteringStrengths.x);
    float MieMul   = max(0.0, Uni.ScatteringStrengths.y);
    float OzoneMul = max(0.0, Uni.ScatteringStrengths.w);
    float RayH     = max(500.0, Uni.PlanetDimensions.z);
    float MieH     = max(200.0, Uni.PlanetDimensions.w);

    float DenR = exp(-AltM / RayH) * RayMul;
    float DenM = exp(-AltM / MieH) * MieMul;
    float DenO = max(0.0, 1.0 - abs(AltM - 25000.0) / 15000.0) * OzoneMul;

    AtmoMediumKm C;
    C.RayScatter = float3(5.802e-3, 13.558e-3, 33.1e-3) * DenR;
    C.MieScatter = 3.996e-3 * DenM;
    float MieExt = 4.40e-3 * DenM;
    float3 Ozone = float3(0.650e-3, 1.881e-3, 0.085e-3) * DenO;
    C.Extinction = C.RayScatter + float3(MieExt) + Ozone;
    return C;
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
    float GroundKm  = max(500.0, PlanetR * 0.001);
    float TopKm     = max(GroundKm + 10.0, AtmoR * 0.001);
    float3 PosKm    = StartPos * 0.001;
    float  RadiusKm = max(GroundKm, length(PosKm));
    float  CosPolar = clamp(dot(PosKm / RadiusKm, normalize(RayDir)), -1.0, 1.0);

    if (AtmoQuerySphereBlockedKm(RadiusKm, CosPolar, GroundKm)) return float3(0.0, 0.0, 0.0);
    float DistTopKm = AtmoIntersectUpperKm(RadiusKm, CosPolar, TopKm);
    if (DistTopKm <= 1.0e-5) return float3(1.0, 1.0, 1.0);

    float3 RayBetaKm   = float3(5.802e-3, 13.558e-3, 33.1e-3);
    float3 OzoneBetaKm = float3(0.650e-3, 1.881e-3, 0.085e-3);
    float3 OptDepth    = float3(0.0, 0.0, 0.0);

    for (uint S = 0u; S < 64u; ++S)
    {
        float F0   = float(S) / 64.0;
        float F1   = float(S + 1u) / 64.0;
        float T0   = DistTopKm * F0 * F0;
        float T1   = DistTopKm * F1 * F1;
        float T    = 0.5 * (T0 + T1);
        float Step = max(1.0e-5, T1 - T0);
        float RK   = sqrt(max(GroundKm * GroundKm, RadiusKm * RadiusKm + 2.0 * RadiusKm * CosPolar * T + T * T));
        float AltM = max(0.0, (RK - GroundKm) * 1000.0);
        float DenR = exp(-AltM / RayH) * RayMul;
        float DenM = exp(-AltM / MieH) * MieMul;
        float DenO = max(0.0, 1.0 - abs(AltM - 25000.0) / 15000.0) * OzoneMul;
        OptDepth += (RayBetaKm * DenR + float3(4.40e-3 * DenM) + OzoneBetaKm * DenO) * Step;
    }
    return exp(-OptDepth);
}

float AtmoCatmullRom(float P0, float P1, float P2, float P3, float T)
{
    float T2 = T * T;
    float T3 = T2 * T;
    return 0.5 * ((2.0 * P1) +
                  (-P0 + P2) * T +
                  (2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3) * T2 +
                  (-P0 + 3.0 * P1 - 3.0 * P2 + P3) * T3);
}

float3 EvaluateSkyViewLutSample(
    StructuredBuffer<float4> SkyLut,
    uint                     WidthPx,
    uint                     HeightPx,
    float3                   Dir,
    float                    PlanetRadiusM,
    float                    ObserverAltM)
{
    if (WidthPx < 2u || HeightPx < 4u) return float3(0.16, 0.23, 0.36);

    int   W        = int(WidthPx);
    int   HalfH    = int(HeightPx / 2u);
    float GroundKm = max(500.0, PlanetRadiusM * 0.001);
    float SafeAltM = (ObserverAltM > 0.0) ? max(5.0, ObserverAltM) : ViewerAltM;
    float ViewerKm = GroundKm + SafeAltM * 0.001;
    float LimbAngle = AtmoComputeLimbPolarAngle(ViewerKm, GroundKm);

    float Azimuth    = atan2(Dir.y, Dir.x);
    float PolarAngle = acos(clamp(Dir.z, -1.0, 1.0));
    float U          = frac(Azimuth / AtmoTauConst + 0.5);

    bool  LowerBank = (PolarAngle > LimbAngle);
    float CoordY    = 0.0;
    if (!LowerBank)
    {
        float Q = sqrt(clamp(1.0 - PolarAngle / max(1.0e-4, LimbAngle), 0.0, 1.0));
        CoordY  = Q * float(HalfH - 1);
    }
    else
    {
        float Q = sqrt(clamp((PolarAngle - LimbAngle) / max(1.0e-4, AtmoPiConst - LimbAngle), 0.0, 1.0));
        CoordY  = float(HalfH) + Q * float(HalfH - 1);
    }

    int BankMin = LowerBank ? HalfH : 0;
    int BankMax = LowerBank ? (HalfH * 2 - 1) : (HalfH - 1);

    float FX  = U * float(W) - 0.5;
    int   IX0 = ((int(floor(FX)) % W) + W) % W;
    int   IX1 = (IX0 + 1) % W;
    float TX  = FX - floor(FX);
    float SX  = TX * TX * (3.0 - 2.0 * TX);

    int   IY1 = clamp(int(floor(CoordY)), BankMin, BankMax);
    int   IY0 = clamp(IY1 - 1, BankMin, BankMax);
    int   IY2 = clamp(IY1 + 1, BankMin, BankMax);
    int   IY3 = clamp(IY1 + 2, BankMin, BankMax);
    float TY  = clamp(CoordY - float(IY1), 0.0, 1.0);

    float3 R0 = lerp(SkyLut[uint(IY0) * WidthPx + uint(IX0)].xyz, SkyLut[uint(IY0) * WidthPx + uint(IX1)].xyz, SX);
    float3 R1 = lerp(SkyLut[uint(IY1) * WidthPx + uint(IX0)].xyz, SkyLut[uint(IY1) * WidthPx + uint(IX1)].xyz, SX);
    float3 R2 = lerp(SkyLut[uint(IY2) * WidthPx + uint(IX0)].xyz, SkyLut[uint(IY2) * WidthPx + uint(IX1)].xyz, SX);
    float3 R3 = lerp(SkyLut[uint(IY3) * WidthPx + uint(IX0)].xyz, SkyLut[uint(IY3) * WidthPx + uint(IX1)].xyz, SX);

    return max(
        float3(0.0, 0.0, 0.0),
        float3(
            AtmoCatmullRom(R0.x, R1.x, R2.x, R3.x, TY),
            AtmoCatmullRom(R0.y, R1.y, R2.y, R3.y, TY),
            AtmoCatmullRom(R0.z, R1.z, R2.z, R3.z, TY)));
}

float3 EvaluateSkyViewLutSample(StructuredBuffer<float4> SkyLut, uint WidthPx, uint HeightPx, float3 Dir)
{
    return EvaluateSkyViewLutSample(SkyLut, WidthPx, HeightPx, Dir, 6360000.0, ViewerAltM);
}

float3 EvaluateTransmittanceLutSample(
    StructuredBuffer<float4> SkyLut,
    float                    RadiusKm,
    float                    CosPolar,
    float                    GroundKm,
    float                    TopKm)
{
    if (AtmoQuerySphereBlockedKm(RadiusKm, CosPolar, GroundKm)) return float3(0.0, 0.0, 0.0);
    float ClampedR  = clamp(RadiusKm, GroundKm, TopKm);
    float HorizonKm = AtmoComputeHorizonKm(GroundKm, TopKm);
    float RhoKm     = sqrt(max(0.0, (ClampedR - GroundKm) * (ClampedR + GroundKm)));
    float DistTopKm = AtmoIntersectUpperKm(ClampedR, clamp(CosPolar, -1.0, 1.0), TopKm);
    float DMinKm    = max(0.0, TopKm - ClampedR);
    float DMaxKm    = RhoKm + HorizonKm;
    float U         = clamp((DistTopKm - DMinKm) / max(1.0e-5, DMaxKm - DMinKm), 0.0, 1.0);
    float V         = clamp(RhoKm / max(1.0e-5, HorizonKm), 0.0, 1.0);

    float FX = clamp(U, 0.0, 1.0) * float(AtmoTransW - 1u);
    float FY = clamp(V, 0.0, 1.0) * float(AtmoTransH - 1u);
    uint  X0 = min(AtmoTransW - 2u, uint(floor(FX)));
    uint  Y0 = min(AtmoTransH - 2u, uint(floor(FY)));
    float TX = FX - float(X0);
    float TY = FY - float(Y0);

    uint I00 = AtmoTransOffset + Y0 * AtmoTransW + X0;
    uint I10 = I00 + 1u;
    uint I01 = I00 + AtmoTransW;
    uint I11 = I01 + 1u;

    float3 C00 = SkyLut[I00].xyz;
    float3 C10 = SkyLut[I10].xyz;
    float3 C01 = SkyLut[I01].xyz;
    float3 C11 = SkyLut[I11].xyz;
    return lerp(lerp(C00, C10, TX), lerp(C01, C11, TX), TY);
}

#ifndef FRONTIER_ATMOSPHERE_EMBEDDED
[[vk::binding(0, 0)]] ConstantBuffer<AtmosphericSkyUniformBlock> SkyUniforms;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4>               OutSkyViewLut;

float3 SamplePackedLutBilinear(
    uint  BaseOffset,
    uint  Width,
    uint  Height,
    float U,
    float V)
{
    float FX = clamp(U, 0.0, 1.0) * float(Width - 1u);
    float FY = clamp(V, 0.0, 1.0) * float(Height - 1u);
    uint  X0 = min(Width - 2u, uint(floor(FX)));
    uint  Y0 = min(Height - 2u, uint(floor(FY)));
    float TX = FX - float(X0);
    float TY = FY - float(Y0);

    uint I00 = BaseOffset + Y0 * Width + X0;
    uint I10 = I00 + 1u;
    uint I01 = I00 + Width;
    uint I11 = I01 + 1u;

    float3 C00 = OutSkyViewLut[I00].xyz;
    float3 C10 = OutSkyViewLut[I10].xyz;
    float3 C01 = OutSkyViewLut[I01].xyz;
    float3 C11 = OutSkyViewLut[I11].xyz;
    return lerp(lerp(C00, C10, TX), lerp(C01, C11, TX), TY);
}

float3 SampleTransmittancePacked(
    float RadiusKm,
    float CosPolar,
    float GroundKm,
    float TopKm)
{
    if (AtmoQuerySphereBlockedKm(RadiusKm, CosPolar, GroundKm)) return float3(0.0, 0.0, 0.0);
    float ClampedR  = clamp(RadiusKm, GroundKm, TopKm);
    float HorizonKm = AtmoComputeHorizonKm(GroundKm, TopKm);
    float RhoKm     = sqrt(max(0.0, (ClampedR - GroundKm) * (ClampedR + GroundKm)));
    float DistTopKm = AtmoIntersectUpperKm(ClampedR, clamp(CosPolar, -1.0, 1.0), TopKm);
    float DMinKm    = max(0.0, TopKm - ClampedR);
    float DMaxKm    = RhoKm + HorizonKm;
    float U         = clamp((DistTopKm - DMinKm) / max(1.0e-5, DMaxKm - DMinKm), 0.0, 1.0);
    float V         = clamp(RhoKm / max(1.0e-5, HorizonKm), 0.0, 1.0);
    return SamplePackedLutBilinear(AtmoTransOffset, AtmoTransW, AtmoTransH, U, V);
}

float3 SampleMultiScatterPacked(
    float AltitudeKm,
    float SolarCosine,
    float ThicknessKm)
{
    float U = clamp(0.5 * (SolarCosine + 1.0), 0.0, 1.0);
    float V = sqrt(clamp(AltitudeKm / max(1.0e-3, ThicknessKm), 0.0, 1.0));
    return SamplePackedLutBilinear(AtmoMultiOffset, AtmoMultiW, AtmoMultiH, U, V);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void TransmittanceLutComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    if (DispatchThreadID.x >= AtmoTransW || DispatchThreadID.y >= AtmoTransH) return;

    float GroundKm    = max(500.0, SkyUniforms.PlanetDimensions.x * 0.001);
    float ThicknessKm = max(10.0, SkyUniforms.PlanetDimensions.y * 0.001);
    float TopKm       = GroundKm + ThicknessKm;
    float HorizonKm   = AtmoComputeHorizonKm(GroundKm, TopKm);

    float V         = float(DispatchThreadID.y) / float(AtmoTransH - 1u);
    float U         = float(DispatchThreadID.x) / float(AtmoTransW - 1u);
    float RhoKm     = V * HorizonKm;
    float RadiusKm  = sqrt(RhoKm * RhoKm + GroundKm * GroundKm);
    float DMinKm    = max(0.0, TopKm - RadiusKm);
    float DMaxKm    = RhoKm + HorizonKm;
    float DistTopKm = DMinKm + U * (DMaxKm - DMinKm);
    float CosPolar  = (DistTopKm <= 1.0e-5)
        ? 1.0
        : clamp((HorizonKm * HorizonKm - RhoKm * RhoKm - DistTopKm * DistTopKm) / (2.0 * RadiusKm * DistTopKm), -1.0, 1.0);

    float3 OptDepth = float3(0.0, 0.0, 0.0);
    for (uint S = 0u; S < 64u; ++S)
    {
        float F0   = float(S) / 64.0;
        float F1   = float(S + 1u) / 64.0;
        float T0   = DistTopKm * F0 * F0;
        float T1   = DistTopKm * F1 * F1;
        float T    = 0.5 * (T0 + T1);
        float Step = max(1.0e-5, T1 - T0);
        float RK   = sqrt(max(GroundKm * GroundKm, RadiusKm * RadiusKm + 2.0 * RadiusKm * CosPolar * T + T * T));
        AtmoMediumKm C = AtmoEvaluateMediumKm(RK - GroundKm, SkyUniforms);
        OptDepth += C.Extinction * Step;
    }

    uint Idx = AtmoTransOffset + DispatchThreadID.y * AtmoTransW + DispatchThreadID.x;
    OutSkyViewLut[Idx] = float4(exp(-OptDepth), 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void MultiScatterLutComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    if (DispatchThreadID.x >= AtmoMultiW || DispatchThreadID.y >= AtmoMultiH) return;

    float GroundKm    = max(500.0, SkyUniforms.PlanetDimensions.x * 0.001);
    float ThicknessKm = max(10.0, SkyUniforms.PlanetDimensions.y * 0.001);
    float TopKm       = GroundKm + ThicknessKm;

    float V        = float(DispatchThreadID.y) / float(AtmoMultiH - 1u);
    float U        = float(DispatchThreadID.x) / float(AtmoMultiW - 1u);
    float AltKm    = clamp(V * V * ThicknessKm, 0.01, max(0.02, ThicknessKm - 0.01));
    float RadiusKm = GroundKm + AltKm;
    float SunCos   = clamp(2.0 * U - 1.0, -1.0, 1.0);
    float SunSin   = sqrt(max(0.0, 1.0 - SunCos * SunCos));

    float GoldenRatio = 1.61803398875;
    float InvDirs     = 1.0 / 32.0;
    float3 Lum2       = float3(0.0, 0.0, 0.0);
    float3 Fms        = float3(0.0, 0.0, 0.0);

    for (uint D = 0u; D < 32u; ++D)
    {
        float CosPhi    = 1.0 - 2.0 * (float(D) + 0.5) * InvDirs;
        float SinPhi    = sqrt(max(0.0, 1.0 - CosPhi * CosPhi));
        float Theta     = AtmoTauConst * (float(D) + 0.5) / GoldenRatio;
        float DirX      = SinPhi * cos(Theta);
        float DirZ      = CosPhi;
        float RaySunCos = clamp(DirX * SunSin + DirZ * SunCos, -1.0, 1.0);

        bool  HitsGround = false;
        float GroundD    = AtmoIntersectLowerKm(RadiusKm, DirZ, GroundKm, HitsGround);
        float MaxD       = HitsGround ? GroundD : AtmoIntersectUpperKm(RadiusKm, DirZ, TopKm);
        if (MaxD <= 1.0e-5) continue;

        float  Step   = MaxD / 24.0;
        float3 TAccum = float3(1.0, 1.0, 1.0);
        float3 RayL2  = float3(0.0, 0.0, 0.0);
        float3 RayFms = float3(0.0, 0.0, 0.0);

        for (uint S = 0u; S < 24u; ++S)
        {
            float T    = (float(S) + 0.5) * Step;
            float RK   = sqrt(max(GroundKm * GroundKm, RadiusKm * RadiusKm + 2.0 * RadiusKm * DirZ * T + T * T));
            AtmoMediumKm C = AtmoEvaluateMediumKm(RK - GroundKm, SkyUniforms);

            float3 SafeE = max(float3(1.0e-7), C.Extinction);
            float3 SegT  = exp(-SafeE * Step);
            float3 IntS  = (float3(1.0) - SegT) / SafeE;
            float3 Scat  = C.RayScatter + float3(C.MieScatter);

            float SampleSunCos = clamp((RadiusKm * SunCos + T * RaySunCos) / RK, -1.0, 1.0);
            float3 SunT        = SampleTransmittancePacked(RK, SampleSunCos, GroundKm, TopKm);

            RayL2  += TAccum * Scat * AtmoInvFourPi * SunT * IntS;
            RayFms += TAccum * Scat * IntS;
            TAccum *= SegT;
        }

        if (HitsGround)
        {
            float HitSunCos = clamp((RadiusKm * SunCos + MaxD * RaySunCos) / GroundKm, -1.0, 1.0);
            if (HitSunCos > 0.0)
            {
                float3 GroundSunT = SampleTransmittancePacked(GroundKm, HitSunCos, GroundKm, TopKm);
                RayL2 += TAccum * SkyUniforms.GroundAlbedoLimbGlow.xyz * GroundSunT * (HitSunCos / AtmoPiConst);
            }
        }

        Lum2 += RayL2 * InvDirs;
        Fms  += RayFms * InvDirs;
    }

    float3 Result = Lum2 / max(float3(1.0e-3), float3(1.0) - min(float3(0.95), Fms));
    uint   Idx    = AtmoMultiOffset + DispatchThreadID.y * AtmoMultiW + DispatchThreadID.x;
    OutSkyViewLut[Idx] = float4(max(float3(0.0), Result), 1.0);
}

float3 IntegrateUpperSkyRadiance(
    float                      PolarAngle,
    float                      Azimuth,
    float                      StartR,
    float                      GroundKm,
    float                      TopKm,
    float                      ThicknessKm,
    float3                     UnitSun,
    float                      SolarScale,
    float                      MieG,
    float                      DayVis,
    AtmosphericSkyUniformBlock Uni)
{
    float  CosPolar   = cos(PolarAngle);
    float  SinPolar   = sqrt(max(0.0, 1.0 - CosPolar * CosPolar));
    float3 Rd         = float3(SinPolar * cos(Azimuth), SinPolar * sin(Azimuth), CosPolar);
    float  CosTheta   = dot(Rd, UnitSun);
    float  PhaseR     = (3.0 / (16.0 * AtmoPiConst)) * (1.0 + CosTheta * CosTheta);
    float  G2         = MieG * MieG;
    float  Denom      = max(1.0e-3, 1.0 + G2 - 2.0 * MieG * CosTheta);
    float  PhaseM     = (3.0 / (8.0 * AtmoPiConst)) * ((1.0 - G2) * (1.0 + CosTheta * CosTheta)) /
                        ((2.0 + G2) * pow(Denom, 1.5));
    float  MaxDistKm  = AtmoIntersectUpperKm(StartR, CosPolar, TopKm);

    float  AntiCool   = lerp(lerp(0.38, 1.0, smoothstep(-0.35, 0.30, CosTheta)), 1.0, smoothstep(0.06, 0.24, UnitSun.z));
    float3 TAccum   = float3(1.0, 1.0, 1.0);
    float3 RadAccum = float3(0.0, 0.0, 0.0);
    for (uint S = 0u; S < 40u; ++S)
    {
        float F0   = float(S) / 40.0;
        float F1   = float(S + 1u) / 40.0;
        float T0   = MaxDistKm * F0 * F0;
        float T1   = MaxDistKm * F1 * F1;
        float T    = 0.5 * (T0 + T1);
        float Step = max(1.0e-5, T1 - T0);

        float RK    = sqrt(max(GroundKm * GroundKm, StartR * StartR + 2.0 * StartR * CosPolar * T + T * T));
        float AltKm = max(0.0, RK - GroundKm);
        AtmoMediumKm C = AtmoEvaluateMediumKm(AltKm, Uni);

        float3 SafeE = max(float3(1.0e-7), C.Extinction);
        float3 SegT  = exp(-SafeE * Step);
        float3 IntS  = (float3(1.0) - SegT) / SafeE;

        float  SampleSunCos = clamp((StartR * UnitSun.z + T * CosTheta) / RK, -1.0, 1.0);
        float3 SunT         = SampleTransmittancePacked(RK, SampleSunCos, GroundKm, TopKm);
        float3 Multi        = SampleMultiScatterPacked(AltKm, SampleSunCos, ThicknessKm) * AntiCool;

        float3 Scat = C.RayScatter * PhaseR + float3(C.MieScatter * PhaseM);
        float3 MsW  = C.RayScatter + float3(C.MieScatter);
        RadAccum += TAccum * (Scat * SunT * AntiCool + MsW * Multi) * IntS;
        TAccum   *= SegT;
    }

    float  LimbFactor = exp(-max(0.0, CosPolar) * 7.5);
    float  GlowScale  = 1.0 + (Uni.GroundAlbedoLimbGlow.w - 1.0) * LimbFactor * 0.55;
    float3 NightAir   = float3(0.00018, 0.00028, 0.00062) + float3(0.00022, 0.00032, 0.00058) * LimbFactor;
    return (RadAccum * SolarScale * GlowScale + NightAir * (1.0 - DayVis * 0.9)) *
           Uni.SkyTintBrightness.xyz * Uni.SkyTintBrightness.w;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint W = max(2u, SkyUniforms.LutDimensions.x);
    uint H = max(4u, SkyUniforms.LutDimensions.y);
    if (DispatchThreadID.x >= W || DispatchThreadID.y >= H) return;

    uint   HalfH       = H / 2u;
    bool   LowerBank   = (DispatchThreadID.y >= HalfH);
    float  GroundKm    = max(500.0, SkyUniforms.PlanetDimensions.x * 0.001);
    float  ThicknessKm = max(10.0, SkyUniforms.PlanetDimensions.y * 0.001);
    float  TopKm       = GroundKm + ThicknessKm;
    float  AltM        = (SkyUniforms.LutDimensions.z > 0u)
        ? clamp(float(SkyUniforms.LutDimensions.z), 5.0, max(100.0, (ThicknessKm - 0.05) * 1000.0))
        : ViewerAltM;
    float  StartR      = GroundKm + AltM * 0.001;
    float  LimbAngle   = AtmoComputeLimbPolarAngle(StartR, GroundKm);

    float  U           = (float(DispatchThreadID.x) + 0.5) / float(W);
    float  Azimuth     = (U - 0.5) * AtmoTauConst;
    float3 UnitSun     = normalize(SkyUniforms.SunDirectionIntensity.xyz);
    float  SolarScale  = max(0.0, SkyUniforms.SunDirectionIntensity.w) * 0.24;
    float  MieG        = clamp(SkyUniforms.ScatteringStrengths.z, 0.0, 0.99);
    float  SunElevDeg  = asin(clamp(UnitSun.z, -1.0, 1.0)) * (180.0 / AtmoPiConst);
    float  DayVis      = clamp((SunElevDeg + 6.0) / 14.0, 0.0, 1.0);

    if (!LowerBank)
    {
        float LocalV     = float(DispatchThreadID.y) / float(HalfH - 1u);
        float PolarAngle = max(0.0, LimbAngle * (1.0 - LocalV * LocalV) - 1.5e-4);
        float3 OutRgb    = IntegrateUpperSkyRadiance(
            PolarAngle, Azimuth, StartR, GroundKm, TopKm, ThicknessKm,
            UnitSun, SolarScale, MieG, DayVis, SkyUniforms);
        OutSkyViewLut[DispatchThreadID.y * W + DispatchThreadID.x] = float4(max(float3(0.0), OutRgb), 1.0);
        return;
    }

    float  LocalV     = float(DispatchThreadID.y - HalfH) / float(HalfH - 1u);
    float  PolarAngle = min(AtmoPiConst - 1.0e-5, LimbAngle + (AtmoPiConst - LimbAngle) * LocalV * LocalV + 1.5e-4);
    float  CosPolar   = cos(PolarAngle);
    float  SinPolar   = sqrt(max(0.0, 1.0 - CosPolar * CosPolar));
    float3 Rd         = float3(SinPolar * cos(Azimuth), SinPolar * sin(Azimuth), CosPolar);

    float  CosTheta   = dot(Rd, UnitSun);
    float  PhaseR     = (3.0 / (16.0 * AtmoPiConst)) * (1.0 + CosTheta * CosTheta);
    float  G2         = MieG * MieG;
    float  Denom      = max(1.0e-3, 1.0 + G2 - 2.0 * MieG * CosTheta);
    float  PhaseM     = (3.0 / (8.0 * AtmoPiConst)) * ((1.0 - G2) * (1.0 + CosTheta * CosTheta)) /
                        ((2.0 + G2) * pow(Denom, 1.5));

    float  AntiCool     = lerp(lerp(0.38, 1.0, smoothstep(-0.35, 0.30, CosTheta)), 1.0, smoothstep(0.06, 0.24, UnitSun.z));
    bool   HitsGround   = false;
    float  GroundDistKm = AtmoIntersectLowerKm(StartR, CosPolar, GroundKm, HitsGround);
    float  MaxDistKm    = HitsGround ? max(0.005, GroundDistKm) : AtmoIntersectUpperKm(StartR, CosPolar, TopKm);

    float3 TAccum   = float3(1.0, 1.0, 1.0);
    float3 RadAccum = float3(0.0, 0.0, 0.0);

    for (uint S = 0u; S < 28u; ++S)
    {
        float F0   = float(S) / 28.0;
        float F1   = float(S + 1u) / 28.0;
        float T0   = MaxDistKm * F0 * F0;
        float T1   = MaxDistKm * F1 * F1;
        float T    = 0.5 * (T0 + T1);
        float Step = max(1.0e-5, T1 - T0);

        float RK    = sqrt(max(GroundKm * GroundKm, StartR * StartR + 2.0 * StartR * CosPolar * T + T * T));
        float AltKm = max(0.0, RK - GroundKm);
        AtmoMediumKm C = AtmoEvaluateMediumKm(AltKm, SkyUniforms);

        float3 SafeE = max(float3(1.0e-7), C.Extinction);
        float3 SegT  = exp(-SafeE * Step);
        float3 IntS  = (float3(1.0) - SegT) / SafeE;

        float  SampleSunCos = clamp((StartR * UnitSun.z + T * CosTheta) / RK, -1.0, 1.0);
        float3 SunT         = SampleTransmittancePacked(RK, SampleSunCos, GroundKm, TopKm);
        float3 Multi        = SampleMultiScatterPacked(AltKm, SampleSunCos, ThicknessKm) * AntiCool;

        float3 Scat = C.RayScatter * PhaseR + float3(C.MieScatter * PhaseM);
        float3 MsW  = C.RayScatter + float3(C.MieScatter);
        RadAccum += TAccum * (Scat * SunT * AntiCool + MsW * Multi) * IntS;
        TAccum   *= SegT;
    }

    if (HitsGround)
    {
        float HitSunCos = clamp((StartR * UnitSun.z + MaxDistKm * CosTheta) / GroundKm, -1.0, 1.0);
        float3 GroundSunT = (HitSunCos > 0.0)
            ? SampleTransmittancePacked(GroundKm, HitSunCos, GroundKm, TopKm)
            : float3(0.0, 0.0, 0.0);
        float3 GroundMulti = SampleMultiScatterPacked(0.0, HitSunCos, ThicknessKm) * AntiCool;
        float  SunTerm     = max(0.0, HitSunCos) / AtmoPiConst;
        RadAccum += TAccum * SkyUniforms.GroundAlbedoLimbGlow.xyz * (GroundSunT * SunTerm + GroundMulti * 0.35);
    }

    float LimbFactor = exp(-max(0.0, CosPolar) * 7.5);
    float GlowScale  = 1.0 + (SkyUniforms.GroundAlbedoLimbGlow.w - 1.0) * LimbFactor * 0.55;
    float3 NightAir  = float3(0.00018, 0.00028, 0.00062) + float3(0.00022, 0.00032, 0.00058) * LimbFactor;
    float3 OutRgb    = (RadAccum * SolarScale * GlowScale + NightAir * (1.0 - DayVis * 0.9)) *
                       SkyUniforms.SkyTintBrightness.xyz * SkyUniforms.SkyTintBrightness.w;

    float LimbRowPolar = max(0.0, LimbAngle - 1.5e-4);
    float3 LimbSky     = IntegrateUpperSkyRadiance(
        LimbRowPolar, Azimuth, StartR, GroundKm, TopKm, ThicknessKm,
        UnitSun, SolarScale, MieG, DayVis, SkyUniforms);
    float Grazing = pow(1.0 - clamp(-CosPolar, 0.0, 1.0), 5.0);
    float Fresnel = 0.02 + 0.98 * Grazing;
    OutRgb        = lerp(OutRgb, LimbSky, Fresnel);

    OutSkyViewLut[DispatchThreadID.y * W + DispatchThreadID.x] = float4(max(float3(0.0), OutRgb), 1.0);
}
#endif
