/*============================================================================================================================================
                                                    WORLDVIEWPORTRADIOMETRICCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Unified 5-Engine GPU Compute Shader:
// 0=MatCap, 1=OpenPbrDirect, 2=DdgiRaytracing (DynamicIrradianceCache + Chebyshev), 3=SdfGlobalIllumination (Hybrid ReSTIR Path Replay + 5-Pass SVGF), 4=DeterministicPathTrace (MIS/VNDF)
// Enforces Zero SDF Shadows (5 Shadow Lab algorithms over depth32float + BVH4) & Zero SDF Reflections (BVH4 or Sky Environment).

#define FRONTIER_ATMOSPHERE_EMBEDDED 1
#include "../Atmosphere/SkyViewTransmittanceCompute.slang"
#include "../Atmosphere/VolumetricCloudCompute.slang"
#include "../Atmosphere/VolumetricFogCompute.slang"
#include "../Atmosphere/CelestialDiscStarCompute.slang"
#include "../Atmosphere/LensFlareCompute.slang"
#include "../Fluid/FluidVolumetricRaymarch.slang"

struct GpuAreaQuadLight
{
    float4 CenterHalfWidth; // xyz = CenterWorld, w = HalfWidthMeters
    float4 RightHalfHeight; // xyz = RightTangent, w = HalfHeightMeters
    float4 UpTwoSided;      // xyz = UpBitangent, w = TwoSidedRadiance (1.0 / 0.0)
    float4 RadiancePacked;  // xyz = EmittedRadiance, w = Pad
};

struct GpuGlassSphere
{
    float4 CenterRadiusPacked; // xyz = CenterWorld, w = RadiusMeters
    float4 OpticsSlotPacked;   // x = DielectricIOR, y = AbbeDispersion, z = MaterialSlot, w = Roughness
};

struct WorldViewportUniformBlock
{
    float4 CameraOriginTanX;         // xyz = CameraOrigin, w = TanHalfFovX
    float4 CameraRightTanY;          // xyz = CameraRight,  w = TanHalfFovY
    float4 CameraUpWidth;            // xyz = CameraUp,     w = ViewportWidth
    float4 CameraForwardHeight;      // xyz = CameraForward,w = ViewportHeight
    float4 SunDirConeTan;            // xyz = SunDirection, w = TanHalfSolarCone
    float4 SolarIrradianceTier;      // xyz = SolarIrradiance, w = ActiveDdgiReady
    float4 SkyRadianceBvhCount;      // xyz = SkyRadiance,  w = BvhEntryCount
    float4 SdfMinSpacing;            // xyz = SdfMinCorner, w = SdfVoxelSpacing
    uint4  SdfDimsLightCount;        // xyz = SdfDimensions,w = AreaLightCount
    float4 LatticeMinGlassCount;     // xyz = LatticeMin,   w = GlassSphereCount
    float4 LatticeMaxDebugMode;      // xyz = LatticeMax,   w = DebugChannel
    uint4  LatticeDimsMaterialCount; // xyz = LatticeDims,  w = MaterialCount
    uint4  EngineShadowReflSample;   // x = EngineMode (0..4), y = ShadowAlg (0..4), z = ReflSource (0..1), w = SampleIndex
    float4 SunCascadeCenterSpan;     // xyz = CenterWorld,  w = HalfSpanMeters
    float4 SunCascadeRightRes;       // xyz = LightRight,   w = ResolutionTexels
    float4 SunCascadeUpDepth;        // xyz = LightUp,      w = DepthExtentMeters
    float4 SunCascadeForwardPad;     // xyz = LightForward, w = PathTraceMaxBounces
    float4 FineCascadeCenterSpan;    // xyz = FineCenter,   w = FineHalfSpanMeters
    float4 FineCascadeParams;        // x = FineRes, y = FineDepthExtent, z = CoarseOffsetTexels, w = PathSamplesPerDispatch
    uint4  DenoiseStepParams;        // x = StridePx, y = Flags (bit0=FinalComposite, bit1=SpatialDenoise, bit2=LowSppPt), z = Pad, w = Pad
    GpuAreaQuadLight AreaLights[4];
    GpuGlassSphere   GlassSpheres[4];
    uint4                       AtmosphereMaskPad; // x = AtmosphereEnabled (0/1), y = SkyViewW, z = SkyViewH, w = FlagBits
    VolumetricCloudUniformBlock CloudParams;
    VolumetricFogUniformBlock   FogParams;
    CelestialFieldUniformBlock  CelestialParams;
    LensFlareUniformBlock       LensFlareParams;
    FluidRenderUniformBlock     FluidParams;
    float4                      PrevCameraOriginTanX;
    float4                      PrevCameraRightTanY;
    float4                      PrevCameraUpWidth;
    float4                      PrevCameraForwardHeight;
};

struct GpuWideTreeEntry128
{
    float4 BranchBoundsMinX;
    float4 BranchBoundsMaxX;
    float4 BranchBoundsMinY;
    float4 BranchBoundsMaxY;
    float4 BranchBoundsMinZ;
    float4 BranchBoundsMaxZ;
    uint4  BranchReference;
    uint4  BranchPrimitiveCount;
};

struct GpuShadedTriangle96
{
    float4 Corner0Pad;
    float4 Edge01Pad;
    float4 Edge02Pad;
    float4 Normal0Pad;
    float4 Normal1Pad;
    float4 Normal2Slot; // xyz = SmoothNormal2, w = float(MaterialSlot)
};

struct GpuWorldMaterial80
{
    float4 DiffuseAlbedoRoughness; // xyz = EffectiveDiffuseAlbedo, w = DiffuseRoughness
    float4 SpecParams;             // x = SpecRoughness, y = Metalness, z = DielectricIOR, w = Anisotropy
    float4 EdgeTintCoatWeight;     // xyz = EdgeTintF82, w = CoatWeight
    float4 CoatRoughnessOptics;    // x = CoatRoughness, y = TransmissionWeight, z = AbbeDispersion, w = Pad
    float4 EmissiveRadiancePad;    // xyz = Planckian Blackbody Emissive RGB, w = Pad
};

[[vk::binding(0,  0)]] ConstantBuffer<WorldViewportUniformBlock> WorldUniforms;
[[vk::binding(1,  0)]] StructuredBuffer<GpuWideTreeEntry128>     SceneBvhEntries;
[[vk::binding(2,  0)]] StructuredBuffer<GpuShadedTriangle96>     SceneTriangles;
[[vk::binding(3,  0)]] StructuredBuffer<GpuWorldMaterial80>      MaterialEntries;
[[vk::binding(4,  0)]] StructuredBuffer<float>                   SignedDistanceGrid;
[[vk::binding(5,  0)]] RWStructuredBuffer<float4>                IrradianceLatticeTexels;
[[vk::binding(6,  0)]] RWStructuredBuffer<uint>                  OutDisplayRgba8;
[[vk::binding(7,  0)]] RWStructuredBuffer<float>                 SolarShadowDepthBuffer;
[[vk::binding(8,  0)]] RWStructuredBuffer<float2>                IrradianceChebyshevDepth;
[[vk::binding(9,  0)]] RWStructuredBuffer<float4>                SdfRadiosityVoxels;
[[vk::binding(10, 0)]] RWStructuredBuffer<float4>                AccumulationHdrBuffer;
[[vk::binding(11, 0)]] StructuredBuffer<float4>                  AtmosphericSkyViewLut;
[[vk::binding(12, 0)]] StructuredBuffer<uint>                    LunarSurfaceAtlasTexels;
[[vk::binding(13, 0)]] StructuredBuffer<uint>                    BakedLensFlareAtlasTexels;
[[vk::binding(14, 0)]] StructuredBuffer<float4>                  FluidHeroThermoTexels;
[[vk::binding(15, 0)]] StructuredBuffer<float4>                  FluidWorldThermoTexels;
[[vk::binding(16, 0)]] RWStructuredBuffer<float4>                GBufPosMask;
[[vk::binding(17, 0)]] RWStructuredBuffer<float4>                GBufNormAo;
[[vk::binding(18, 0)]] RWStructuredBuffer<float4>                GBufDirectBase;
[[vk::binding(19, 0)]] RWStructuredBuffer<float4>                GBufSunContrib;
[[vk::binding(20, 0)]] RWStructuredBuffer<float4>                GBufAlbedo;
[[vk::binding(21, 0)]] RWStructuredBuffer<float4>                PingShadowAo;
[[vk::binding(22, 0)]] RWStructuredBuffer<float4>                PingGiHdr;
[[vk::binding(23, 0)]] RWStructuredBuffer<float4>                PongShadowAo;
[[vk::binding(24, 0)]] RWStructuredBuffer<float4>                PongGiHdr;
[[vk::binding(25, 0)]] RWStructuredBuffer<float4>                CloudRadiometricBuffer;
[[vk::binding(26, 0)]] Sampler3D<float4>                         SdfBakedVolumeRead;
[[vk::binding(27, 0)]] RWTexture3D<float4>                       SdfBakedVolumeWrite;
[[vk::binding(28, 0)]] RWStructuredBuffer<float4>                WeightedCurrentBuffer;
[[vk::binding(29, 0)]] RWStructuredBuffer<float4>                ReservoirHistoryBuffer;
[[vk::binding(30, 0)]] StructuredBuffer<GpuProjectedStar>        ProjectedCatalogStars;
[[vk::binding(31, 0)]] StructuredBuffer<uint>                    StellarTileBins;

static const float PiConst    = 3.14159265359;
static const float TauConst   = 6.28318530718;
static const float InvPiConst = 0.31830988618;

struct RayHitReading
{
    bool   Hit;
    float  Distance;
    float3 Position;
    float3 ShadingNormal;
    float3 GeomNormal;
    uint   MaterialSlot;
};

uint PcgNext(inout uint State)
{
    State = State * 747796405u + 2891336453u;
    uint Word = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return (Word >> 22u) ^ Word;
}

float PcgFloat01(inout uint State)
{
    return float(PcgNext(State) & 0x00FFFFFFu) * (1.0 / 16777216.0);
}

float2 PixelR2(uint2 PixelXY)
{
    float IgnA = frac(52.9829189 * frac(float(PixelXY.x) * 0.06711056 + float(PixelXY.y) * 0.00583715));
    uint  Rng  = (PixelXY.x * 747796405u + PixelXY.y * 2891336453u + 277803737u) | 1u;
    float RandB = PcgFloat01(Rng);
    return float2(IgnA, RandB);
}

uint ReverseBits32(uint X)
{
    X = ((X & 0x55555555u) << 1u) | ((X >> 1u) & 0x55555555u);
    X = ((X & 0x33333333u) << 2u) | ((X >> 2u) & 0x33333333u);
    X = ((X & 0x0F0F0F0Fu) << 4u) | ((X >> 4u) & 0x0F0F0F0Fu);
    X = ((X & 0x00FF00FFu) << 8u) | ((X >> 8u) & 0x00FF00FFu);
    return (X << 16u) | (X >> 16u);
}

bool IsGiEnabled()          { return (WorldUniforms.AtmosphereMaskPad.w & 1u) == 0u; }
bool IsReflectionsEnabled() { return (WorldUniforms.AtmosphereMaskPad.w & 2u) == 0u; }
bool IsPtDenoiseEnabled()   { return (WorldUniforms.AtmosphereMaskPad.w & 4u) == 0u; }

uint AvalancheKey32(uint Input)
{
    uint Mixed = Input;
    Mixed ^= Mixed >> 16u;
    Mixed *= 0x7FEB352Du;
    Mixed ^= Mixed >> 15u;
    Mixed *= 0x846CA68Bu;
    Mixed ^= Mixed >> 16u;
    return Mixed;
}

uint LaineKarrasPermutation(uint Input, uint Key)
{
    uint Permuted = Input + Key;
    Permuted ^= Permuted * 0x6C50B47Cu;
    Permuted ^= Permuted * 0xB82F1E52u;
    Permuted ^= Permuted * 0xC7AFE638u;
    Permuted ^= Permuted * 0x8D22F6E6u;
    return Permuted;
}

uint NestedDigitPermutation(uint Input, uint Key)
{
    return ReverseBits32(LaineKarrasPermutation(ReverseBits32(Input), Key));
}

// 5-stage bit-parallel GF(2) Pascal zeta transform (exact Sobol dim 2)
uint SobolDim2(uint Index)
{
    uint Bits = Index;
    Bits ^= (Bits & 0xAAAAAAAAu) >> 1u;
    Bits ^= (Bits & 0xCCCCCCCCu) >> 2u;
    Bits ^= (Bits & 0xF0F0F0F0u) >> 4u;
    Bits ^= (Bits & 0xFF00FF00u) >> 8u;
    Bits ^= (Bits & 0xFFFF0000u) >> 16u;
    return ReverseBits32(Bits);
}

float2 Sobol02Sample(uint SampleIndex, uint PixelHash, uint PairIndex)
{
    uint PairKey       = AvalancheKey32(PairIndex * 0x9E3779B9u + 0x85EBCA6Bu) ^ PixelHash;
    uint ShuffledIndex = NestedDigitPermutation(SampleIndex, PairKey);
    uint FirstDigits   = NestedDigitPermutation(ReverseBits32(ShuffledIndex), AvalancheKey32(PairKey ^ 0xA511E9B3u));
    uint SecondDigits  = NestedDigitPermutation(SobolDim2(ShuffledIndex), AvalancheKey32(PairKey ^ 0x63D83595u));
    return float2(float(FirstDigits >> 8u), float(SecondDigits >> 8u)) * (1.0 / 16777216.0);
}

float3 EvaluateSkyDome(float3 Dir)
{
    if (WorldUniforms.AtmosphereMaskPad.x != 0u)
    {
        float PlanetRadiusM = max(500000.0, WorldUniforms.CelestialParams.SolarOpticsParams.w);
        float ObsAltM       = WorldUniforms.CelestialParams.SolarOpticsParams.z;
        return EvaluateSkyViewLutSample(
            AtmosphericSkyViewLut,
            WorldUniforms.AtmosphereMaskPad.y,
            WorldUniforms.AtmosphereMaskPad.z,
            Dir,
            PlanetRadiusM,
            ObsAltM);
    }
    float Up = saturate(Dir.z * 0.5 + 0.5);
    return WorldUniforms.SkyRadianceBvhCount.xyz * (0.65 + 0.70 * Up);
}

float4 SampleCloudBufferBilinear(float2 PixelUv, out float OutCloudTrans)
{
    uint CW = uint(WorldUniforms.CloudParams.CloudPassControl.x + 0.5);
    uint CH = uint(WorldUniforms.CloudParams.CloudPassControl.y + 0.5);
    if (CW == 0u || CH == 0u)
    {
        OutCloudTrans = 1.0;
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    float FX = clamp(PixelUv.x * float(CW) - 0.5, 0.0, max(0.0, float(CW) - 1.0));
    float FY = clamp(PixelUv.y * float(CH) - 0.5, 0.0, max(0.0, float(CH) - 1.0));
    uint X0 = uint(floor(FX));
    uint Y0 = uint(floor(FY));
    uint X1 = min(CW - 1u, X0 + 1u);
    uint Y1 = min(CH - 1u, Y0 + 1u);
    float TX = FX - float(X0);
    float TY = FY - float(Y0);

    float4 C00 = CloudRadiometricBuffer[Y0 * CW + X0];
    float4 C10 = CloudRadiometricBuffer[Y0 * CW + X1];
    float4 C01 = CloudRadiometricBuffer[Y1 * CW + X0];
    float4 C11 = CloudRadiometricBuffer[Y1 * CW + X1];
    float4 Res = lerp(lerp(C00, C10, TX), lerp(C01, C11, TX), TY);

    // Light 5-tap spatial anti-stipple filter when upscaling half/quarter-res cloud pass
    uint FullW = uint(WorldUniforms.CameraUpWidth.w + 0.5);
    if (CW < FullW && CW >= 4u && CH >= 4u)
    {
        uint XM = (X0 > 0u) ? (X0 - 1u) : X0;
        uint XP = min(CW - 1u, X1 + 1u);
        uint YM = (Y0 > 0u) ? (Y0 - 1u) : Y0;
        uint YP = min(CH - 1u, Y1 + 1u);
        float4 S0 = CloudRadiometricBuffer[Y0 * CW + XM];
        float4 S1 = CloudRadiometricBuffer[Y0 * CW + XP];
        float4 S2 = CloudRadiometricBuffer[YM * CW + X0];
        float4 S3 = CloudRadiometricBuffer[YP * CW + X0];
        float4 AvgNbr = (S0 + S1 + S2 + S3) * 0.25;
        Res = lerp(Res, AvgNbr, 0.52);
    }

    OutCloudTrans = clamp(Res.w, 0.0, 1.0);
    return Res;
}

float3 EvaluateCloudDeckReflectionGpu(float3 RayDir, out float OutCloudTrans)
{
    OutCloudTrans = 1.0;
    VolumetricCloudUniformBlock C = WorldUniforms.CloudParams;
    if (C.SunAxisToggle.w < 0.5 || RayDir.z <= 0.015 || C.DeckGeometry.z <= 0.01 || C.DeckGeometry.w <= 0.01)
    {
        return float3(0.0, 0.0, 0.0);
    }
    float TimeSec = (C.SkyColorWind.w > 0.5) ? C.OriginTimeVector.w : 0.0;
    bool  UseProbes = (C.SkyColorWind.w > 0.5 && C.CloudPassControl.w > 0.5 && abs(TimeSec) > 1.0e-4);
    float WindX   = UseProbes ? 0.0 : (C.WindVectorOcclude.x * TimeSec);
    float WindY   = UseProbes ? 0.0 : (C.WindVectorOcclude.y * TimeSec);
    float PlanetR = max(500000.0, C.SolarIrradianceExt.w);
    float FloorM  = max(100.0, C.DeckGeometry.x);
    float ThickM  = max(100.0, C.DeckGeometry.y);
    float MidAlt  = FloorM + ThickM * 0.45;
    float DistM   = min(38000.0, MidAlt / max(0.03, RayDir.z));
    float3 P      = float3(RayDir.x * DistM, RayDir.y * DistM, PlanetR + 100.0 + MidAlt);
    float3 EvalP  = P;
    if (UseProbes)
    {
        float3 Adv = AdvectCloudPointBackwardGpu(C, float3(P.x, P.y, MidAlt), TimeSec, true);
        EvalP = float3(Adv.x, Adv.y, sqrt(max(1.0, (PlanetR + Adv.z) * (PlanetR + Adv.z) - Adv.x * Adv.x - Adv.y * Adv.y)));
    }
    float FScale  = 0.00065 / max(0.08, C.DeckOptics.x);
    float D = SampleCloudDeckDensityGpu(
        EvalP, PlanetR, FloorM, ThickM, clamp(C.DeckGeometry.z, 0.0, 1.0), clamp(C.DeckOptics.y, 0.0, 1.0),
        max(0.0, C.DeckGeometry.w) * 0.0085, FScale, WindX, WindY, C.DeckOptics.w, uint(C.FormationOptics.x + 0.5), DistM,
        C.FormationOptics.y, C.FormationOptics.z, TimeSec);
    if (D <= 1.0e-5)
    {
        return float3(0.0, 0.0, 0.0);
    }
    float Tau      = D * ThickM * 0.35;
    OutCloudTrans  = exp(-Tau);
    float3 SunDir  = normalize(C.SunAxisToggle.xyz);
    float  CosT    = dot(RayDir, SunDir);
    float  Phase   = lerp(0.55, 1.55, clamp(CosT * 0.5 + 0.5, 0.0, 1.0));
    float  Haze    = clamp(C.SolarIrradianceExt.w > 0.0 ? (1.0 - exp(-DistM * 0.000025)) : 0.0, 0.0, 0.65);
    float3 LitCol  = C.SolarIrradianceExt.xyz * (0.28 * Phase) + C.SkyColorWind.xyz * 0.65;
    return lerp(LitCol, C.SkyColorWind.xyz, Haze) * (1.0 - OutCloudTrans);
}

float3 EvaluateOrbitalSpaceGpu(float3 RayOrigin, float3 RayDir, float3 SunDir, out bool OutHitsPlanet)
{
    OutHitsPlanet = false;
    VolumetricCloudUniformBlock C = WorldUniforms.CloudParams;
    float PlanetR = max(500000.0, C.SolarIrradianceExt.w);
    float AtmoThk = 100000.0;
    float ObsAlt  = max(AtmoThk + 5000.0, C.WindForceMeta.w);
    float3 CamPos = float3(RayOrigin.x, RayOrigin.y, PlanetR + ObsAlt + RayOrigin.z);

    float B = dot(CamPos, RayDir);
    float K = dot(CamPos, CamPos) - PlanetR * PlanetR;
    float Disc = B * B - K;
    float TPlanet = -1.0;
    if (Disc >= 0.0)
    {
        float Root = sqrt(Disc);
        float T0 = -B - Root;
        if (T0 > 0.0) TPlanet = T0;
    }
    OutHitsPlanet = (TPlanet > 0.0);

    float OutR = PlanetR + AtmoThk;
    float KA = dot(CamPos, CamPos) - OutR * OutR;
    float DiscA = B * B - KA;
    float3 Rad = float3(0.0, 0.0, 0.0);
    if (DiscA <= 0.0)
    {
        return Rad;
    }
    float RootA = sqrt(DiscA);
    float TEnter = max(0.0, -B - RootA);
    float TExit  = OutHitsPlanet ? min(TPlanet, -B + RootA) : (-B + RootA);
    if (TExit <= TEnter)
    {
        return Rad;
    }

    float CosTheta = dot(RayDir, SunDir);
    float RayPhase = 0.05968310366 * (1.0 + CosTheta * CosTheta);
    float3 RayBeta = float3(5.8e-6, 13.5e-6, 33.1e-6) * max(0.0, WorldUniforms.CelestialParams.AtmoMediumPack.x);
    float  Steps   = 12.0;
    float  Dt      = (TExit - TEnter) / Steps;
    float3 InScat  = float3(0.0, 0.0, 0.0);
    float3 ViewTau = float3(0.0, 0.0, 0.0);
    for (uint S = 0u; S < 12u; ++S)
    {
        float  T    = TEnter + (float(S) + 0.5) * Dt;
        float3 P    = CamPos + RayDir * T;
        float  Alt  = max(0.0, length(P) - PlanetR);
        float  RhoR = exp(-Alt / 8000.0);
        ViewTau    += RayBeta * (RhoR * Dt);
        float3 Up   = normalize(P);
        float  Mus  = dot(Up, SunDir);
        if (Mus > -0.14)
        {
            float Ch   = 1.0 / max(0.08, max(0.0, Mus) + 0.14);
            float3 ST  = exp(-(RayBeta * (8000.0 * RhoR * Ch)));
            InScat    += exp(-ViewTau) * ST * RayBeta * (RhoR * RayPhase * Dt * 12.0);
        }
    }

    if (OutHitsPlanet)
    {
        float3 HitP = CamPos + RayDir * TPlanet;
        float3 N    = normalize(HitP);
        float  NdotLRaw = dot(N, SunDir);
        float  SunM = max(0.0, NdotLRaw);
        float  Cont = CloudFbmGpu3(N * 3.1 + float3(4.1, -1.8, 0.9), 1.0);
        float  PolarIce = smoothstep(0.83 + 0.06 * Cont, 0.92 + 0.06 * Cont, abs(N.z));
        float  LandMask = smoothstep(0.47, 0.515, Cont);
        float  Shelf    = smoothstep(0.415, 0.48, Cont) * (1.0 - LandMask);
        float  Moist = CloudFbmGpu3(N * 7.5 + float3(-2.3, 5.1, 0.0), 1.0);
        float  Elev  = saturate((Cont - 0.49) / 0.24);
        float3 LandCol = lerp(float3(0.055, 0.145, 0.048), float3(0.350, 0.265, 0.165), saturate((Moist - 0.44) / 0.24)) * (0.85 + 0.35 * Elev);
        float3 OceanCol = float3(0.012 + 0.018 * Shelf, 0.042 + 0.058 * Shelf, 0.135 + 0.055 * Shelf);
        float3 Alb = lerp(OceanCol, LandCol, LandMask);
        Alb = lerp(Alb, float3(0.72, 0.76, 0.82), PolarIce);

        float3 HVec = normalize(SunDir - RayDir);
        float  OceanSpec = (1.0 - LandMask) * (1.0 - PolarIce) * pow(max(0.0, dot(N, HVec)), 42.0) * 0.45 * (0.2 + 0.8 * SunM);
        float  TermWarm  = exp(-abs(NdotLRaw) * 14.0) * saturate((NdotLRaw + 0.08) / 0.16);
        float  SVBase    = 0.35 + 0.65 * saturate((NdotLRaw + 0.04) / 0.25);
        float3 SunVisC   = float3(SVBase * (1.0 + 0.65 * TermWarm), SVBase * (1.0 + 0.22 * TermWarm), SVBase * (1.0 - 0.10 * TermWarm));

        float JetLon = C.OriginTimeVector.w * 0.0022;
        float CosJ = cos(JetLon);
        float SinJ = sin(JetLon);
        float3 RotN = float3(N.x * CosJ + N.y * SinJ, -N.x * SinJ + N.y * CosJ, N.z);
        if (C.SkyColorWind.w > 0.5)
        {
            float3 LocalP = float3(HitP.x * 0.0001, HitP.y * 0.0001, 1500.0);
            float3 AdvL   = AdvectCloudPointBackwardGpu(C, LocalP, C.OriginTimeVector.w, true);
            RotN = normalize(RotN + float3((AdvL.x - LocalP.x), (AdvL.y - LocalP.y), 0.0) * 0.00015);
        }
        float  Lon = atan2(RotN.y, RotN.x);
        float  Lat = asin(clamp(RotN.z, -1.0, 1.0));
        float  SpiralR = length(float2(Lon - 0.35, Lat - 0.38));
        if (SpiralR < 0.45)
        {
            float SwirlA = (1.0 - SpiralR / 0.45) * 1.45;
            float DLon = Lon - 0.35;
            float DLat = Lat - 0.38;
            float CS = cos(SwirlA);
            float SS = sin(SwirlA);
            RotN = normalize(RotN + float3((DLon * CS - DLat * SS - DLon) * 0.45, (DLon * SS + DLat * CS - DLat) * 0.45, 0.0));
        }
        float  Cld  = (C.SunAxisToggle.w > 0.5) ? smoothstep(0.46, 0.72, CloudFbmGpu3(RotN * 5.2, 1.0)) * clamp(C.DeckGeometry.z, 0.0, 1.0) : 0.0;
        float  CldShad = 1.0 - 0.48 * Cld;
        float3 Surf = (Alb * SunM * CldShad + float3(OceanSpec)) * SunVisC * 0.28;

        float  NightMask = saturate((-NdotLRaw - 0.04) / 0.14) * LandMask * (1.0 - PolarIce);
        if (NightMask > 0.01)
        {
            float City = saturate((CloudFbmGpu3(N * 16.0, 1.0) - 0.60) / 0.12) * NightMask * (1.0 - 0.65 * Cld);
            Surf += float3(City * 0.38, City * 0.22, City * 0.06);
        }
        if (Cld > 0.001)
        {
            float CLit = max(0.015, SunM * 1.15 + 0.04) * 0.28 * 0.85;
            Surf = lerp(Surf, SunVisC * CLit, Cld * 0.88);
        }
        float3 Tr = exp(-ViewTau);
        return max(float3(0.0), InScat * 0.28 + Surf * Tr);
    }

    return InScat * 0.28;
}

float3 EvaluateBackgroundSkyCloudsGpu(float3 RayDir, float2 PixelCoord)
{
    float2 SubPix    = PixelCoord + float2(0.5, 0.5);
    float2 VpDims    = max(float2(1.0, 1.0), float2(WorldUniforms.CameraUpWidth.w, WorldUniforms.CameraForwardHeight.w));
    float2 PrimUv    = SubPix / VpDims;
    float3 PrimDir   = normalize(WorldUniforms.CameraForwardHeight.xyz
                               + WorldUniforms.CameraRightTanY.xyz * ((PrimUv.x * 2.0 - 1.0) * WorldUniforms.CameraOriginTanX.w)
                               + WorldUniforms.CameraUpWidth.xyz   * ((1.0 - PrimUv.y * 2.0) * WorldUniforms.CameraRightTanY.w));
    bool   UseTiled  = (dot(normalize(RayDir), PrimDir) > 0.999);

    int AtmoFlags = int(WorldUniforms.CelestialParams.AtmoMediumPack.w + 0.5);
    bool IsOrbital = (AtmoFlags & 2) != 0;
    if (!IsOrbital && WorldUniforms.AtmosphereMaskPad.x == 0u)
    {
        return EvaluateSkyDome(RayDir);
    }

    float3 BaseSky = float3(0.0);
    if (IsOrbital)
    {
        bool HitsPlanet = false;
        BaseSky = EvaluateOrbitalSpaceGpu(WorldUniforms.CameraOriginTanX.xyz, RayDir, normalize(WorldUniforms.SunDirConeTan.xyz), HitsPlanet);
        if (HitsPlanet)
        {
            return BaseSky;
        }
    }
    else
    {
        BaseSky = EvaluateSkyDome(RayDir);
    }

    float3 CelestialRad = EvaluateCelestialBodiesUnifiedGpu(
        WorldUniforms.CelestialParams,
        LunarSurfaceAtlasTexels,
        ProjectedCatalogStars,
        StellarTileBins,
        SubPix,
        RayDir,
        UseTiled);
    if (IsOrbital)
    {
        return BaseSky + CelestialRad;
    }

    float3 SkyRad = BaseSky;
    if (WorldUniforms.CloudParams.SunAxisToggle.w < 0.5)
    {
        return SkyRad + CelestialRad;
    }

    float  CloudTrans = 1.0;
    float3 CloudRad   = float3(0.0, 0.0, 0.0);
    uint   CW         = uint(WorldUniforms.CloudParams.CloudPassControl.x + 0.5);
    uint   CH         = uint(WorldUniforms.CloudParams.CloudPassControl.y + 0.5);
    float  FwdProj    = dot(RayDir, WorldUniforms.CameraForwardHeight.xyz);
    if (CW > 0u && CH > 0u && FwdProj > 0.05)
    {
        float NdcX = dot(RayDir, WorldUniforms.CameraRightTanY.xyz) / (FwdProj * max(1.0e-4, WorldUniforms.CameraOriginTanX.w));
        float NdcY = dot(RayDir, WorldUniforms.CameraUpWidth.xyz)   / (FwdProj * max(1.0e-4, WorldUniforms.CameraRightTanY.w));
        if (abs(NdcX) <= 1.001 && abs(NdcY) <= 1.001)
        {
            float2 Uv = float2(NdcX * 0.5 + 0.5, 0.5 - NdcY * 0.5);
            CloudRad  = SampleCloudBufferBilinear(Uv, CloudTrans).xyz;
            return (SkyRad + CelestialRad) * CloudTrans + CloudRad;
        }
    }

    // Off-frustum reflection/refraction fallback uses single-tap analytical deck lookup
    CloudRad = EvaluateCloudDeckReflectionGpu(RayDir, CloudTrans);
    return (SkyRad + CelestialRad) * CloudTrans + CloudRad;
}

float EvaluateSolarCloudTransmittanceGpu(float3 WorldPos, float3 SunDir)
{
    VolumetricCloudUniformBlock C = WorldUniforms.CloudParams;
    if (WorldUniforms.AtmosphereMaskPad.x == 0u || SunDir.z <= 0.01)
    {
        return 1.0;
    }
    float Trans = 1.0;
    if (C.SunAxisToggle.w >= 0.5 && C.WindVectorOcclude.z >= 0.5)
    {
        float TimeSec   = (C.SkyColorWind.w > 0.5) ? C.OriginTimeVector.w : 0.0;
        bool  UseProbes = (C.SkyColorWind.w > 0.5 && C.CloudPassControl.w > 0.5 && abs(TimeSec) > 1.0e-4);
        float WindX     = UseProbes ? 0.0 : (C.WindVectorOcclude.x * TimeSec);
        float WindY     = UseProbes ? 0.0 : (C.WindVectorOcclude.y * TimeSec);
        float PlanetR   = max(500000.0, C.SolarIrradianceExt.w);
        float FloorM    = max(100.0, C.DeckGeometry.x);
        float ThickM    = max(100.0, C.DeckGeometry.y);
        float MidAlt    = FloorM + ThickM * 0.45;
        float DistM     = MidAlt / max(0.05, SunDir.z);
        float3 P        = float3(WorldPos.x + SunDir.x * DistM, WorldPos.y + SunDir.y * DistM, PlanetR + 100.0 + MidAlt);
        float3 EvalP    = P;
        if (UseProbes)
        {
            float3 Adv = AdvectCloudPointBackwardGpu(C, float3(P.x, P.y, MidAlt), TimeSec, true);
            EvalP = float3(Adv.x, Adv.y, sqrt(max(1.0, (PlanetR + Adv.z) * (PlanetR + Adv.z) - Adv.x * Adv.x - Adv.y * Adv.y)));
        }
        float FScale    = (0.00065 / max(0.08, C.DeckOptics.x)) * max(0.01, C.CloudShadowOptics.z);
        float Cov       = clamp(C.DeckGeometry.z * C.CloudShadowOptics.x, 0.0, 1.0);
        float DensMul   = max(0.0, C.DeckGeometry.w * C.CloudShadowOptics.y) * 0.0085;
        float Anvil     = clamp(C.DeckOptics.y * C.CloudShadowOptics.w, 0.0, 1.0);
        float D = SampleCloudDeckDensityGpu(
            EvalP, PlanetR, FloorM, ThickM, Cov, Anvil,
            DensMul, FScale, WindX, WindY, C.DeckOptics.w, uint(C.FormationOptics.x + 0.5), 8000.0,
            C.FormationOptics.y, C.FormationOptics.z, TimeSec);
        Trans *= (0.12 + 0.88 * exp(-D * ThickM * 0.36));
    }
    Trans *= EvaluateLocalCloudsShadowGpu(C, WorldPos, SunDir);
    return clamp(Trans, 0.08, 1.0);
}

float IntersectTriangleMT(float3 Origin, float3 Dir, float MinT, float MaxT, uint TriIndex, out float2 OutUV)
{
    OutUV = float2(0.0, 0.0);
    float3 Edge01  = SceneTriangles[TriIndex].Edge01Pad.xyz;
    float3 Edge02  = SceneTriangles[TriIndex].Edge02Pad.xyz;
    float3 PVec    = cross(Dir, Edge02);
    float  Det     = dot(Edge01, PVec);
    float  AbsDet  = abs(Det);
    if (AbsDet <= 1.0e-8) return -1.0;

    float  SignDet = (Det >= 0.0) ? 1.0 : -1.0;
    float3 Corner0 = SceneTriangles[TriIndex].Corner0Pad.xyz;
    float3 TVec    = Origin - Corner0;
    float  UDet    = dot(TVec, PVec) * SignDet;
    if (UDet < 0.0 || UDet > AbsDet) return -1.0;

    float3 QVec  = cross(TVec, Edge01);
    float  VDet  = dot(Dir, QVec) * SignDet;
    if (VDet < 0.0 || (UDet + VDet) > AbsDet) return -1.0;

    float  TDet  = dot(Edge02, QVec) * SignDet;
    if (TDet < MinT * AbsDet || TDet > MaxT * AbsDet) return -1.0;

    float  InvAbsDet = 1.0 / AbsDet;
    OutUV = float2(UDet, VDet) * InvAbsDet;
    return TDet * InvAbsDet;
}

bool IntersectTriangleShadowMT(float3 Origin, float3 Dir, float MinT, float MaxT, uint TriIndex)
{
    float3 Edge01  = SceneTriangles[TriIndex].Edge01Pad.xyz;
    float3 Edge02  = SceneTriangles[TriIndex].Edge02Pad.xyz;
    float3 PVec    = cross(Dir, Edge02);
    float  Det     = dot(Edge01, PVec);
    float  AbsDet  = abs(Det);
    if (AbsDet <= 1.0e-8) return false;

    float  SignDet = (Det >= 0.0) ? 1.0 : -1.0;
    float3 Corner0 = SceneTriangles[TriIndex].Corner0Pad.xyz;
    float3 TVec    = Origin - Corner0;
    float  UDet    = dot(TVec, PVec) * SignDet;
    if (UDet < 0.0 || UDet > AbsDet) return false;

    float3 QVec  = cross(TVec, Edge01);
    float  VDet  = dot(Dir, QVec) * SignDet;
    if (VDet < 0.0 || (UDet + VDet) > AbsDet) return false;

    float  TDet  = dot(Edge02, QVec) * SignDet;
    return (TDet >= MinT * AbsDet && TDet <= MaxT * AbsDet);
}

float4 IntersectFourBoxes(float3 Origin, float3 InvDir, float MinT, float MaxT, uint EntryIndex)
{
    float4 TX0 = (SceneBvhEntries[EntryIndex].BranchBoundsMinX - Origin.xxxx) * InvDir.xxxx;
    float4 TX1 = (SceneBvhEntries[EntryIndex].BranchBoundsMaxX - Origin.xxxx) * InvDir.xxxx;
    float4 TY0 = (SceneBvhEntries[EntryIndex].BranchBoundsMinY - Origin.yyyy) * InvDir.yyyy;
    float4 TY1 = (SceneBvhEntries[EntryIndex].BranchBoundsMaxY - Origin.yyyy) * InvDir.yyyy;
    float4 TZ0 = (SceneBvhEntries[EntryIndex].BranchBoundsMinZ - Origin.zzzz) * InvDir.zzzz;
    float4 TZ1 = (SceneBvhEntries[EntryIndex].BranchBoundsMaxZ - Origin.zzzz) * InvDir.zzzz;

    float4 NearT = max(max(min(TX0, TX1), min(TY0, TY1)), max(min(TZ0, TZ1), float4(MinT)));
    float4 FarT  = min(min(max(TX0, TX1), max(TY0, TY1)), min(max(TZ0, TZ1), float4(MaxT)));
    return select(NearT <= FarT, NearT, float4(1.0e30));
}

RayHitReading TraceSceneClosest(float3 Origin, float3 Dir, float MinT, float MaxT)
{
    RayHitReading Res;
    Res.Hit           = false;
    Res.Distance      = MaxT;
    Res.Position      = Origin;
    Res.ShadingNormal = float3(0.0, 0.0, 1.0);
    Res.GeomNormal    = float3(0.0, 0.0, 1.0);
    Res.MaterialSlot  = 0u;

    uint EntryCount = uint(WorldUniforms.SkyRadianceBvhCount.w + 0.5);
    if (EntryCount == 0u) return Res;

    float3 SafeDir = select(abs(Dir) > float3(1.0e-8), Dir, float3(1.0e-8));
    float3 InvDir  = 1.0 / SafeDir;

    uint Stack[20];
    int  StackTop = 0;
    Stack[0]      = 0u;

    while (StackTop >= 0)
    {
        uint CurIdx = Stack[StackTop--];
        if (CurIdx >= EntryCount) continue;

        float4 BoxT = IntersectFourBoxes(Origin, InvDir, MinT, Res.Distance, CurIdx);
        uint4  Refs = SceneBvhEntries[CurIdx].BranchReference;
        uint4  Cnts = SceneBvhEntries[CurIdx].BranchPrimitiveCount;

        uint4 Order = uint4(0u, 1u, 2u, 3u);
        if (BoxT[Order.x] < BoxT[Order.y]) { uint Tmp = Order.x; Order.x = Order.y; Order.y = Tmp; }
        if (BoxT[Order.z] < BoxT[Order.w]) { uint Tmp = Order.z; Order.z = Order.w; Order.w = Tmp; }
        if (BoxT[Order.x] < BoxT[Order.z]) { uint Tmp = Order.x; Order.x = Order.z; Order.z = Tmp; }
        if (BoxT[Order.y] < BoxT[Order.w]) { uint Tmp = Order.y; Order.y = Order.w; Order.w = Tmp; }
        if (BoxT[Order.y] < BoxT[Order.z]) { uint Tmp = Order.y; Order.y = Order.z; Order.z = Tmp; }

        for (int LaneIdx = 0; LaneIdx < 4; ++LaneIdx)
        {
            uint Lane = Order[LaneIdx];
            if (BoxT[Lane] >= Res.Distance) continue;

            uint Ref  = Refs[Lane];
            uint Cnt  = Cnts[Lane];
            if (Ref == 0xFFFFFFFFu) continue;

            if (Cnt > 0u)
            {
                uint Start = Ref & 0x7FFFFFFFu;
                for (uint P = 0u; P < Cnt; ++P)
                {
                    uint   TriIdx = Start + P;
                    float2 BaryUV;
                    float  HitT = IntersectTriangleMT(Origin, Dir, MinT, Res.Distance, TriIdx, BaryUV);
                    if (HitT > 0.0)
                    {
                        Res.Hit        = true;
                        Res.Distance   = HitT;
                        Res.Position   = Origin + Dir * HitT;
                        float  W0      = 1.0 - BaryUV.x - BaryUV.y;
                        float3 Edge01  = SceneTriangles[TriIdx].Edge01Pad.xyz;
                        float3 Edge02  = SceneTriangles[TriIdx].Edge02Pad.xyz;
                        float4 N2Slot  = SceneTriangles[TriIdx].Normal2Slot;
                        float3 RawGeom = normalize(cross(Edge01, Edge02));
                        float3 SmoothN = normalize(
                            SceneTriangles[TriIdx].Normal0Pad.xyz * W0 +
                            SceneTriangles[TriIdx].Normal1Pad.xyz * BaryUV.x +
                            N2Slot.xyz * BaryUV.y);
                        if (dot(RawGeom, Dir) > 0.0)
                        {
                            RawGeom = -RawGeom;
                            SmoothN = -SmoothN;
                        }
                        Res.GeomNormal    = RawGeom;
                        Res.ShadingNormal = SmoothN;
                        Res.MaterialSlot  = uint(N2Slot.w + 0.5);
                    }
                }
            }
            else if (StackTop < 18)
            {
                Stack[++StackTop] = Ref;
            }
        }
    }
    return Res;
}

bool TraceSceneAnyHit(float3 Origin, float3 Dir, float MinT, float MaxT)
{
    uint EntryCount = uint(WorldUniforms.SkyRadianceBvhCount.w + 0.5);
    if (EntryCount == 0u) return false;

    float3 SafeDir = select(abs(Dir) > float3(1.0e-8), Dir, float3(1.0e-8));
    float3 InvDir  = 1.0 / SafeDir;

    uint Stack[20];
    int  StackTop = 0;
    Stack[0]      = 0u;

    while (StackTop >= 0)
    {
        uint CurIdx = Stack[StackTop--];
        if (CurIdx >= EntryCount) continue;

        float4 BoxT = IntersectFourBoxes(Origin, InvDir, MinT, MaxT, CurIdx);
        uint4  Refs = SceneBvhEntries[CurIdx].BranchReference;
        uint4  Cnts = SceneBvhEntries[CurIdx].BranchPrimitiveCount;

        for (uint Lane = 0u; Lane < 4u; ++Lane)
        {
            if (BoxT[Lane] >= MaxT) continue;
            uint Ref = Refs[Lane];
            uint Cnt = Cnts[Lane];
            if (Ref == 0xFFFFFFFFu) continue;

            if (Cnt > 0u)
            {
                uint Start = Ref & 0x7FFFFFFFu;
                for (uint P = 0u; P < Cnt; ++P)
                {
                    if (IntersectTriangleShadowMT(Origin, Dir, MinT, MaxT, Start + P))
                    {
                        return true;
                    }
                }
            }
            else if (StackTop < 18)
            {
                Stack[++StackTop] = Ref;
            }
        }
    }
    return false;
}

float4 SampleSdfBakedVolume(float3 WorldPos)
{
    uint3 Dims = WorldUniforms.SdfDimsLightCount.xyz;
    if (Dims.x < 2u || Dims.y < 2u || Dims.z < 2u) return float4(0.0, 0.0, 0.0, 4.0);

    float Spacing = max(1.0e-4, WorldUniforms.SdfMinSpacing.w);
    float3 Rel    = (WorldPos - WorldUniforms.SdfMinSpacing.xyz) / Spacing;
    float3 MaxRel = float3(Dims) - float3(1.0);
    if (Rel.x < 0.0 || Rel.y < 0.0 || Rel.z < 0.0 ||
        Rel.x > MaxRel.x || Rel.y > MaxRel.y || Rel.z > MaxRel.z)
    {
        return float4(0.0, 0.0, 0.0, 2.5);
    }

    float3 Uvw = (Rel + 0.5) / float3(Dims);
    return SdfBakedVolumeRead.SampleLevel(Uvw, 0.0);
}

float SampleSdfGrid(float3 WorldPos)
{
    return SampleSdfBakedVolume(WorldPos).w;
}

float3 EvaluateSdfNormal(float3 WorldPos)
{
    float H = max(0.03, WorldUniforms.SdfMinSpacing.w * 0.75);
    float DX = SampleSdfGrid(WorldPos + float3(H, 0.0, 0.0)) - SampleSdfGrid(WorldPos - float3(H, 0.0, 0.0));
    float DY = SampleSdfGrid(WorldPos + float3(0.0, H, 0.0)) - SampleSdfGrid(WorldPos - float3(0.0, H, 0.0));
    float DZ = SampleSdfGrid(WorldPos + float3(0.0, 0.0, H)) - SampleSdfGrid(WorldPos - float3(0.0, 0.0, H));
    float3 G = float3(DX, DY, DZ);
    float LenSq = dot(G, G);
    return (LenSq > 1.0e-10) ? (G * rsqrt(LenSq)) : float3(0.0, 0.0, 1.0);
}

float3 SampleSdfRadiosityTrilinear(float3 WorldPos)
{
    return SampleSdfBakedVolume(WorldPos).rgb;
}

void ConstructOrthonormalBasis(float3 N, out float3 T, out float3 B)
{
    float SignZ = (N.z >= 0.0) ? 1.0 : -1.0;
    float A     = -1.0 / (SignZ + N.z);
    float D     = N.x * N.y * A;
    T = float3(1.0 + SignZ * N.x * N.x * A, SignZ * D, -SignZ * N.x);
    B = float3(D, SignZ + N.y * N.y * A, -N.y);
}

float IntegrateSphericalEdgeZ(float3 V1, float3 V2)
{
    float X    = clamp(dot(V1, V2), -0.99999, 0.99999);
    float AbsX = abs(X);
    float A    = 5.42031 + (3.12829 + 0.0902326 * AbsX) * AbsX;
    float B    = 3.45068 + (4.18814 + AbsX) * AbsX;
    float ThetaOverSin = A / B;
    if (X < 0.0)
    {
        float SinTheta = sqrt(max(1.0e-8, 1.0 - X * X));
        ThetaOverSin   = PiConst / SinTheta - ThetaOverSin;
    }
    return (V1.x * V2.y - V1.y * V2.x) * ThetaOverSin;
}

float3 EvaluateSurfaceBrdf(GpuWorldMaterial80 Mat, float3 N, float3 V, float3 L)
{
    float NdotL = max(0.0, dot(N, L));
    float NdotV = max(1.0e-4, dot(N, V));
    if (NdotL <= 1.0e-5) return float3(0.0);

    float3 Albedo    = Mat.DiffuseAlbedoRoughness.xyz;
    float  DiffRough = Mat.DiffuseAlbedoRoughness.w;
    float  SpecRough = clamp(Mat.SpecParams.x, 0.025, 1.0);
    float  Metalness = saturate(Mat.SpecParams.y);
    float  Ior       = max(1.02, Mat.SpecParams.z);
    float3 EdgeF82   = Mat.EdgeTintCoatWeight.xyz;
    float  CoatW     = saturate(Mat.EdgeTintCoatWeight.w);
    float  CoatRough = clamp(Mat.CoatRoughnessOptics.x, 0.025, 1.0);
    float  TransW    = saturate(Mat.CoatRoughnessOptics.y);

    float CoeffA  = 1.0 / (1.0 + DiffRough * 0.2877934);
    float CoeffB  = DiffRough * CoeffA;
    float SVal    = dot(V, L) - NdotV * NdotL;
    float InvMax  = (SVal > 0.0) ? (1.0 / max(1.0e-4, max(NdotV, NdotL))) : 1.0;
    float3 Diff   = Albedo * (InvPiConst * (CoeffA + CoeffB * SVal * InvMax)) * (1.0 - Metalness) * (1.0 - TransW);

    float3 H       = normalize(V + L);
    float  NdotH   = max(0.0, dot(N, H));
    float  VdotH   = saturate(dot(V, H));
    float  Alpha   = SpecRough * SpecRough;
    float  Alpha2  = Alpha * Alpha;
    float  Denom   = NdotH * NdotH * (Alpha2 - 1.0) + 1.0;
    float  D_GGX   = Alpha2 / max(1.0e-6, PiConst * Denom * Denom);
    float  KVis    = 0.5 * Alpha;
    float  Vis     = 0.25 / max(1.0e-4, (NdotV * (1.0 - KVis) + KVis) * (NdotL * (1.0 - KVis) + KVis));
    float  F0Diel  = pow((Ior - 1.0) / (Ior + 1.0), 2.0);
    float3 F0      = lerp(float3(F0Diel), Albedo, Metalness);
    float  Schlick = pow(1.0 - VdotH, 5.0);
    float3 Fresnel = F0 + (float3(1.0) - F0) * Schlick;
    if (Metalness > 0.5)
    {
        float Lazanyi = VdotH * pow(1.0 - VdotH, 6.0);
        Fresnel -= (float3(1.0) - EdgeF82) * F0 * Lazanyi * 4.5;
    }
    float3 Spec = max(float3(0.0), Fresnel * (D_GGX * Vis));

    if (CoatW > 0.01)
    {
        float CA   = CoatRough * CoatRough;
        float CA2  = CA * CA;
        float CDen = NdotH * NdotH * (CA2 - 1.0) + 1.0;
        float CD   = CA2 / max(1.0e-6, PiConst * CDen * CDen);
        float CF   = 0.04 + 0.96 * Schlick;
        Spec      += float3(CoatW * CF * CD * 0.25);
        Diff      *= (1.0 - CoatW * CF);
    }

    return (Diff + Spec) * NdotL;
}

float IntegrateClippedQuadFormFactor(float3 LocalQ[4])
{
    float3 Clipped[8];
    uint   ClipCount = 0u;
    for (uint I = 0u; I < 4u; ++I)
    {
        float3 Cur  = LocalQ[I];
        float3 Next = LocalQ[(I + 1u) & 3u];
        bool   CurA = (Cur.z >= 0.0);
        bool   NxtA = (Next.z >= 0.0);
        if (CurA) Clipped[ClipCount++] = Cur;
        if (CurA != NxtA)
        {
            float Alpha = Cur.z / (Cur.z - Next.z);
            float3 Hit  = Cur + (Next - Cur) * Alpha;
            Hit.z       = 0.0;
            Clipped[ClipCount++] = Hit;
        }
    }
    if (ClipCount < 3u) return 0.0;

    for (uint I = 0u; I < ClipCount; ++I)
    {
        Clipped[I] = normalize(Clipped[I]);
    }
    float ContourSum = 0.0;
    for (uint I = 0u; I < ClipCount; ++I)
    {
        ContourSum += IntegrateSphericalEdgeZ(Clipped[I], Clipped[(I + 1u) % ClipCount]);
    }
    return saturate(abs(ContourSum) / TauConst);
}

float3 EvaluateAreaQuadLight(GpuAreaQuadLight Quad, float3 Pos, float3 N, float3 V, GpuWorldMaterial80 Mat)
{
    float3 RightT = Quad.RightHalfHeight.xyz;
    float3 UpB    = Quad.UpTwoSided.xyz;
    float3 QuadN  = normalize(cross(RightT, UpB));
    float3 Center = Quad.CenterHalfWidth.xyz;
    if (Quad.UpTwoSided.w < 0.5 && dot(Pos - Center, QuadN) <= 1.0e-4)
    {
        return float3(0.0);
    }

    float3 HalfU = RightT * Quad.CenterHalfWidth.w;
    float3 HalfV = UpB * Quad.RightHalfHeight.w;

    const float2 GridSlices[2] = { float2(-0.42, -0.42), float2(0.42, 0.42) };
    float3 RayOrig = Pos + N * 0.010;
    uint   VisCnt  = 0u;
    for (uint S = 0u; S < 2u; ++S)
    {
        float3 Target = Center + HalfU * GridSlices[S].x + HalfV * GridSlices[S].y;
        float3 ToL    = Target - RayOrig;
        float  Dist   = length(ToL);
        if (Dist <= 1.0e-4) continue;
        float3 DirL   = ToL / Dist;
        if (dot(N, DirL) <= 0.0) continue;
        if (!TraceSceneAnyHit(RayOrig, DirL, 0.032, max(0.040, Dist - 0.035)))
        {
            ++VisCnt;
        }
    }
    if (VisCnt == 0u) return float3(0.0);
    float VisFactor = float(VisCnt) * 0.5;

    float  NdotV = clamp(dot(N, V), 1.0e-4, 1.0);
    float3 T1    = V - N * NdotV;
    float  T1Len = dot(T1, T1);
    if (T1Len <= 1.0e-6)
    {
        float3 FallbackB;
        ConstructOrthonormalBasis(N, T1, FallbackB);
    }
    else
    {
        T1 = T1 * rsqrt(T1Len);
    }
    float3 T2 = normalize(cross(N, T1));

    float3 WorldC[4] = {
        Center - HalfU - HalfV,
        Center + HalfU - HalfV,
        Center + HalfU + HalfV,
        Center - HalfU + HalfV
    };
    float3 DiffLocal[4];
    for (uint I = 0u; I < 4u; ++I)
    {
        float3 Rel   = WorldC[I] - Pos;
        DiffLocal[I] = float3(dot(Rel, T1), dot(Rel, T2), dot(Rel, N));
    }
    float DiffFormFactor = IntegrateClippedQuadFormFactor(DiffLocal);

    float Roughness = clamp(Mat.SpecParams.x, 0.025, 1.0);
    float Alpha     = clamp(Roughness * Roughness, 0.004, 1.0);
    float SinV      = sqrt(max(0.0, 1.0 - NdotV * NdotV));
    float InvScaleX = 1.0 / max(0.04, Alpha * (0.85 + 0.15 * NdotV));
    float InvScaleY = 1.0 / max(0.04, Alpha);
    float ShearXZ   = -SinV / max(0.08, NdotV + Alpha);

    float3 SpecLocal[4];
    for (uint I = 0u; I < 4u; ++I)
    {
        float3 D      = DiffLocal[I];
        float MirrorX = -D.x * NdotV + D.z * SinV;
        float MirrorZ =  D.x * SinV  + D.z * NdotV;
        SpecLocal[I]  = float3(
            (MirrorX + ShearXZ * MirrorZ * Alpha) * InvScaleX,
            D.y * InvScaleY,
            MirrorZ);
    }
    float SpecFormFactor = IntegrateClippedQuadFormFactor(SpecLocal);

    float3 Albedo    = Mat.DiffuseAlbedoRoughness.xyz;
    float  Metalness = saturate(Mat.SpecParams.y);
    float  TransW    = saturate(Mat.CoatRoughnessOptics.y);
    float  Ior       = max(1.02, Mat.SpecParams.z);
    float  DielF0    = pow((Ior - 1.0) / (Ior + 1.0), 2.0);
    float3 SpecF0    = lerp(float3(DielF0), Albedo, Metalness);
    float  Schlick   = pow(1.0 - NdotV, 5.0);
    float3 SpecAlb   = SpecF0 + (float3(1.0) - SpecF0) * (Schlick * saturate(1.0 - Roughness * 0.65));

    float3 DiffContrib = Albedo * (1.0 - Metalness) * (1.0 - TransW) * DiffFormFactor;
    float3 SpecContrib = SpecAlb * SpecFormFactor;
    return Quad.RadiancePacked.xyz * (DiffContrib + SpecContrib) * VisFactor;
}

//------------------------------------------------------------------------------------------------------------------------
// SHADOW LAB ALGORITHMS (Zero SDF Shadows! Uses depth32float Sun Cascade + BVH4 Ray Queries)
//------------------------------------------------------------------------------------------------------------------------

bool ProjectCascadeLevel(
    float3     WorldPos,
    float3     CenterWorld,
    float      HalfSpan,
    float3     RightAxis,
    float3     UpAxis,
    float3     FwdAxis,
    float      DepthExtent,
    uint       Res,
    out float2 OutTexelXY,
    out float  OutRecD,
    out float  OutTexelM)
{
    OutTexelXY = float2(0.0);
    OutRecD    = 0.0;
    OutTexelM  = 0.015;
    if (Res < 4u || HalfSpan <= 1.0e-4) return false;

    float3 Rel    = WorldPos - CenterWorld;
    float  LocalU = dot(Rel, RightAxis);
    float  LocalV = dot(Rel, UpAxis);
    float  LocalZ = dot(Rel, FwdAxis);
    float  Margin = HalfSpan * 0.94;
    if (abs(LocalU) > Margin || abs(LocalV) > Margin) return false;

    float ResF     = float(Res);
    float InvSpan2 = 1.0 / (2.0 * HalfSpan);
    OutTexelXY.x   = ((LocalU + HalfSpan) * InvSpan2) * ResF;
    OutTexelXY.y   = ((HalfSpan - LocalV) * InvSpan2) * ResF;
    OutRecD        = LocalZ + DepthExtent;
    OutTexelM      = (2.0 * HalfSpan) / ResF;
    return true;
}

bool ProjectBestSunCascade(
    float3     WorldPos,
    out float2 OutTexelXY,
    out float  OutRecD,
    out float  OutTexelM,
    out uint   OutRes,
    out uint   OutBufferOffset)
{
    uint FineRes    = uint(WorldUniforms.FineCascadeParams.x + 0.5);
    uint CoarseOffs = uint(WorldUniforms.FineCascadeParams.z + 0.5);
    if (FineRes >= 4u && CoarseOffs >= FineRes * FineRes)
    {
        if (ProjectCascadeLevel(
                WorldPos,
                WorldUniforms.FineCascadeCenterSpan.xyz,
                WorldUniforms.FineCascadeCenterSpan.w,
                WorldUniforms.SunCascadeRightRes.xyz,
                WorldUniforms.SunCascadeUpDepth.xyz,
                WorldUniforms.SunCascadeForwardPad.xyz,
                WorldUniforms.FineCascadeParams.y,
                FineRes,
                OutTexelXY,
                OutRecD,
                OutTexelM))
        {
            OutRes          = FineRes;
            OutBufferOffset = 0u;
            return true;
        }
    }

    OutRes          = uint(WorldUniforms.SunCascadeRightRes.w + 0.5);
    OutBufferOffset = CoarseOffs;
    return ProjectCascadeLevel(
        WorldPos,
        WorldUniforms.SunCascadeCenterSpan.xyz,
        WorldUniforms.SunCascadeCenterSpan.w,
        WorldUniforms.SunCascadeRightRes.xyz,
        WorldUniforms.SunCascadeUpDepth.xyz,
        WorldUniforms.SunCascadeForwardPad.xyz,
        WorldUniforms.SunCascadeUpDepth.w,
        OutRes,
        OutTexelXY,
        OutRecD,
        OutTexelM);
}

float SampleShadowVisBilinear(float2 TexelXY, float RecD, float Bias, uint Res, uint BufferOffset)
{
    int   MaxIdx = int(Res) - 1;
    float FX     = TexelXY.x - 0.5;
    float FY     = TexelXY.y - 0.5;
    int   X0     = clamp(int(floor(FX)), 0, MaxIdx);
    int   Y0     = clamp(int(floor(FY)), 0, MaxIdx);
    int   X1     = min(X0 + 1, MaxIdx);
    int   Y1     = min(Y0 + 1, MaxIdx);
    float TX     = saturate(FX - floor(FX));
    float TY     = saturate(FY - floor(FY));

    float V00 = ((RecD - SolarShadowDepthBuffer[BufferOffset + uint(Y0) * Res + uint(X0)]) <= Bias) ? 1.0 : 0.0;
    float V10 = ((RecD - SolarShadowDepthBuffer[BufferOffset + uint(Y0) * Res + uint(X1)]) <= Bias) ? 1.0 : 0.0;
    float V01 = ((RecD - SolarShadowDepthBuffer[BufferOffset + uint(Y1) * Res + uint(X0)]) <= Bias) ? 1.0 : 0.0;
    float V11 = ((RecD - SolarShadowDepthBuffer[BufferOffset + uint(Y1) * Res + uint(X1)]) <= Bias) ? 1.0 : 0.0;
    return lerp(lerp(V00, V10, TX), lerp(V01, V11, TX), TY);
}

float EvaluateSolarShadow(RayHitReading Hit, float3 SunDir, uint2 PixelXY, out float OutBlockerDist)
{
    OutBlockerDist = 0.0;
    uint ShadowAlg = WorldUniforms.EngineShadowReflSample.y;
    float NdotL    = dot(Hit.ShadingNormal, SunDir);
    if (NdotL <= 1.0e-5) return 0.0;

    float  TanSun = max(0.004, WorldUniforms.SunDirConeTan.w);
    float2 R2P    = PixelR2(PixelXY);
    float  RotPhi = R2P.x * TauConst;

    // Algorithms 5, 6, 7: Voxel SDF Penumbra / Voxel SDF Clipmap Penumbra / Hybrid SDF (0.24 m BVH4 Contact + SDF Penumbra)
    if (ShadowAlg == 5u || ShadowAlg == 6u || ShadowAlg == 7u)
    {
        float Spacing = max(0.035, WorldUniforms.SdfMinSpacing.w);
        float NormBias = max(0.028, Spacing * 0.45);
        float DistM    = max(0.028, Spacing * 0.35);
        float Vis      = 1.0;
        float BlkDist  = 0.0;
        float MaxDist  = 24.0;
        for (uint Step = 0u; Step < 56u && DistM < MaxDist; ++Step)
        {
            float3 SPos = Hit.Position + Hit.ShadingNormal * NormBias + SunDir * DistM;
            float  SdfD = SampleSdfBakedVolume(SPos).w;
            float  ConeR = max(0.018, DistM * TanSun);
            float  Pen   = smoothstep(-ConeR * 0.20, ConeR * 0.90, SdfD);
            if (Pen < Vis)
            {
                Vis     = Pen;
                BlkDist = DistM;
            }
            if (Vis < 0.005)
            {
                Vis = 0.0;
                break;
            }
            DistM += clamp(SdfD * 0.72, 0.022, 0.36);
        }
        if (ShadowAlg == 7u)
        {
            float3 COrig = Hit.Position + Hit.GeomNormal * 0.005;
            if (TraceSceneAnyHit(COrig, SunDir, 0.002, 0.26))
            {
                Vis     = 0.0;
                BlkDist = 0.06;
            }
        }
        OutBlockerDist = BlkDist;
        return Vis;
    }

    // Algorithm 4: WideTreeRayQuery (Deterministic Vogel Solar Disc BVH4 Raytrace)
    if (ShadowAlg == 4u)
    {
        float3 T, B;
        ConstructOrthonormalBasis(SunDir, T, B);
        float3 Orig = Hit.Position + Hit.GeomNormal * 0.010 + Hit.ShadingNormal * 0.004;
        float  Vis  = 0.0;
        for (uint R = 0u; R < 4u; ++R)
        {
            float DiscR = sqrt((float(R) + 0.5) * 0.25) * TanSun;
            float Ang   = float(R) * 2.39996323 + RotPhi;
            float3 JDir = normalize(SunDir + T * (DiscR * cos(Ang)) + B * (DiscR * sin(Ang)));
            if (!TraceSceneAnyHit(Orig, JDir, 0.025, 90.0))
            {
                Vis += 0.25;
            }
        }
        OutBlockerDist = (Vis < 0.99 && Vis > 0.01) ? 0.35 : 0.0;
        return Vis;
    }

    float2 CenterTXY;
    float  RecD, TexelM;
    uint   Res, BufOffs;
    float3 BiasedPos = Hit.Position + Hit.GeomNormal * 0.004 + Hit.ShadingNormal * 0.004;
    if (!ProjectBestSunCascade(BiasedPos, CenterTXY, RecD, TexelM, Res, BufOffs))
    {
        float3 Orig = Hit.Position + Hit.GeomNormal * 0.012;
        return TraceSceneAnyHit(Orig, SunDir, 0.028, 90.0) ? 0.0 : 1.0;
    }

    int   MaxIdx     = int(Res) - 1;
    float SlopeScale = sqrt(max(0.0, 1.0 - NdotL * NdotL)) / max(0.18, NdotL);
    float Bias       = TexelM * (1.20 + 0.90 * SlopeScale);

    // Algorithm 3: PcfBilinear (16-Tap Smooth Bilinear Vogel PCF)
    if (ShadowAlg == 3u)
    {
        float Vis = 0.0;
        float RadiusPx = 1.85;
        for (uint Tap = 0u; Tap < 16u; ++Tap)
        {
            float R   = sqrt((float(Tap) + 0.5) / 16.0) * RadiusPx;
            float Ang = float(Tap) * 2.39996323 + RotPhi;
            Vis += SampleShadowVisBilinear(CenterTXY + float2(cos(Ang), sin(Ang)) * R, RecD, Bias + TexelM * 0.12 * R, Res, BufOffs);
        }
        Vis *= (1.0 / 16.0);
        OutBlockerDist = (Vis > 0.01 && Vis < 0.99) ? 0.28 : 0.0;
        return Vis;
    }

    // Algorithm 2: ShadowRayMarch (EEVEE-Next Quadratic SMRT with Segment-Crossing Continuity)
    if (ShadowAlg == 2u)
    {
        float MaxMarchM  = 6.50;
        float MaxStepPx  = clamp((MaxMarchM * TanSun) / TexelM, 2.5, 44.0);
        float ThicknessM = max(0.65, TexelM * 42.0);
        float Lit        = 0.0;
        float BlkDistSum = 0.0;
        uint  BlkHits    = 0u;
        for (uint R = 0u; R < 8u; ++R)
        {
            uint   StepRng = (PixelXY.x * 747796405u + PixelXY.y * 2891336453u + R * 277803737u) | 1u;
            float  StepJit = PcgFloat01(StepRng);
            float  Rad     = sqrt((float(R) + 0.5) * 0.125);
            float  Ang     = float(R) * 2.39996323 + RotPhi;
            float2 StepDir = float2(cos(Ang), sin(Ang)) * (Rad * MaxStepPx);
            bool   Occ     = false;
            float  PrevRayD = RecD;
            for (uint S = 1u; S <= 16u; ++S)
            {
                float TNorm = (float(S) - 0.5 + StepJit) / 16.0;
                float TQuad = TNorm * TNorm;
                float RayD  = RecD - MaxMarchM * TQuad;
                float2 SampleTXY = clamp(CenterTXY + StepDir * TQuad, float2(0.0), float2(float(MaxIdx)));
                int TX0 = int(floor(SampleTXY.x));
                int TY0 = int(floor(SampleTXY.y));
                int TX1 = min(TX0 + 1, MaxIdx);
                int TY1 = min(TY0 + 1, MaxIdx);
                float D00 = SolarShadowDepthBuffer[BufOffs + uint(TY0) * Res + uint(TX0)];
                float D10 = SolarShadowDepthBuffer[BufOffs + uint(TY0) * Res + uint(TX1)];
                float D01 = SolarShadowDepthBuffer[BufOffs + uint(TY1) * Res + uint(TX0)];
                float D11 = SolarShadowDepthBuffer[BufOffs + uint(TY1) * Res + uint(TX1)];
                float CascadeD = min(min(D00, D10), min(D01, D11));
                float DiffCurr = RayD - CascadeD;
                float DiffPrev = PrevRayD - CascadeD;
                if (DiffCurr > Bias && DiffPrev < ThicknessM)
                {
                    Occ = true;
                    BlkDistSum += max(0.04, RecD - CascadeD);
                    ++BlkHits;
                    break;
                }
                PrevRayD = RayD;
            }
            if (!Occ) Lit += 0.125;
        }
        OutBlockerDist = (BlkHits > 0u) ? (BlkDistSum / float(BlkHits)) : 0.0;
        return Lit;
    }

    // Algorithms 0 (ContactPcss) & 1 (PcssPlaneAware): Fine+Coarse Vogel Blocker Search + Smooth Bilinear Penumbra PCF
    float SearchRadPx = clamp((TanSun * 4.5) / TexelM, 2.75, 24.0);
    float BlockerSum  = 0.0;
    uint  Blockers    = 0u;
    for (uint Tap = 0u; Tap < 16u; ++Tap)
    {
        float R   = sqrt((float(Tap) + 0.5) / 16.0) * SearchRadPx;
        float Ang = float(Tap) * 2.39996323 + RotPhi;
        float2 SXY = CenterTXY + float2(cos(Ang), sin(Ang)) * R;
        int TX = clamp(int(floor(SXY.x)), 0, MaxIdx);
        int TY = clamp(int(floor(SXY.y)), 0, MaxIdx);
        float D = SolarShadowDepthBuffer[BufOffs + uint(TY) * Res + uint(TX)];
        float Diff = RecD - D;
        if (Diff > Bias)
        {
            BlockerSum += Diff;
            ++Blockers;
        }
    }

    float Vis = 1.0;
    if (Blockers > 0u)
    {
        float MeanBlocker = BlockerSum / float(Blockers);
        OutBlockerDist    = MeanBlocker;
        float FilterPx    = clamp((MeanBlocker * TanSun * 2.35) / TexelM, 2.25, 24.0);
        float LitSum      = 0.0;
        for (uint Tap = 0u; Tap < 24u; ++Tap)
        {
            float R   = sqrt((float(Tap) + 0.5) / 24.0) * FilterPx;
            float Ang = float(Tap) * 2.39996323 + RotPhi + R2P.y * 1.9;
            float2 SampleCoord = CenterTXY + float2(cos(Ang), sin(Ang)) * R;
            LitSum += SampleShadowVisBilinear(SampleCoord, RecD, Bias + TexelM * 0.14 * R, Res, BufOffs);
        }
        Vis = LitSum * (1.0 / 24.0);
    }

    if (ShadowAlg == 0u)
    {
        // Soft 3-tap Vogel cone 0.24 m BVH4 Contact Gate eliminating cascade detachment without jagged stepping
        float3 CT, CB;
        ConstructOrthonormalBasis(SunDir, CT, CB);
        float3 COrig    = Hit.Position + Hit.GeomNormal * 0.006;
        float  GateVis  = 0.0;
        for (uint G = 0u; G < 3u; ++G)
        {
            float R   = sqrt((float(G) + 0.5) / 3.0) * TanSun;
            float Ang = float(G) * 2.39996323 + RotPhi;
            float3 JDir = normalize(SunDir + CT * (R * cos(Ang)) + CB * (R * sin(Ang)));
            if (!TraceSceneAnyHit(COrig, JDir, 0.003, 0.24))
            {
                GateVis += (1.0 / 3.0);
            }
        }
        Vis = min(Vis, lerp(0.03, 1.0, GateVis));
    }
    return Vis;
}

//------------------------------------------------------------------------------------------------------------------------
// GLOBAL ILLUMINATION ENGINES: DYNAMIC PROBE CACHE CHEBYSHEV (2) vs HYBRID RESTIR PATH REPLAY + SVGF (3)
//------------------------------------------------------------------------------------------------------------------------

float2 EncodeUnitOctahedral(float3 Dir)
{
    float  InvL1 = 1.0 / max(1.0e-6, abs(Dir.x) + abs(Dir.y) + abs(Dir.z));
    float2 Oct   = Dir.xy * InvL1;
    if (Dir.z < 0.0)
    {
        Oct = (1.0 - abs(Oct.yx)) * float2((Oct.x >= 0.0) ? 1.0 : -1.0, (Oct.y >= 0.0) ? 1.0 : -1.0);
    }
    return Oct * 0.5 + 0.5;
}

float3 SampleRtxIrradianceLatticeChebyshev(float3 WorldPos, float3 Normal, float3 ViewDir)
{
    uint3 Dims = WorldUniforms.LatticeDimsMaterialCount.xyz;
    if (Dims.x < 2u || Dims.y < 2u || Dims.z < 2u) return WorldUniforms.SkyRadianceBvhCount.xyz * 0.35;

    float3 LatMin  = WorldUniforms.LatticeMinGlassCount.xyz;
    float3 LatMax  = WorldUniforms.LatticeMaxDebugMode.xyz;
    float3 Span    = max(float3(1.0e-3), LatMax - LatMin);
    float3 Spacing = Span / float3(Dims - uint3(1u));
    float3 BiasPos = WorldPos + Normal * 0.06 + ViewDir * 0.02;
    float3 Rel     = saturate((BiasPos - LatMin) / Span) * float3(Dims - uint3(1u));
    int3   C0      = clamp(int3(floor(Rel)), int3(0), int3(Dims) - int3(1));
    int3   C1      = min(C0 + int3(1), int3(Dims) - int3(1));
    float3 Frac    = saturate(Rel - float3(C0));

    float2 OctIrr  = EncodeUnitOctahedral(Normal);
    uint   OX      = min(7u, uint(OctIrr.x * 8.0));
    uint   OY      = min(7u, uint(OctIrr.y * 8.0));
    uint   TexId   = OY * 8u + OX;

    uint SX  = Dims.x;
    uint SXY = Dims.x * Dims.y;

    float3 Accum  = float3(0.0);
    float  TotalW = 0.0;
    for (uint DZ = 0u; DZ < 2u; ++DZ)
    {
        float WZ = (DZ == 0u) ? (1.0 - Frac.z) : Frac.z;
        uint  IZ = uint((DZ == 0u) ? C0.z : C1.z);
        for (uint DY = 0u; DY < 2u; ++DY)
        {
            float WY = (DY == 0u) ? (1.0 - Frac.y) : Frac.y;
            uint  IY = uint((DY == 0u) ? C0.y : C1.y);
            for (uint DX = 0u; DX < 2u; ++DX)
            {
                float WX     = (DX == 0u) ? (1.0 - Frac.x) : Frac.x;
                uint  IX     = uint((DX == 0u) ? C0.x : C1.x);
                uint  CellId = IZ * SXY + IY * SX + IX;
                float TriW   = WX * WY * WZ;

                float3 CellWorld  = LatMin + float3(float(IX), float(IY), float(IZ)) * Spacing;
                float3 CellToSurf = BiasPos - CellWorld;
                float  DistToCell = length(CellToSurf);
                float3 DirFromC   = (DistToCell > 1.0e-5) ? (CellToSurf / DistToCell) : Normal;

                float Backface = max(0.02, (dot(-DirFromC, Normal) + 1.0) * 0.5);
                float4 CellTexel = IrradianceLatticeTexels[CellId * 64u + TexId];
                float ValidGate  = (CellTexel.w > 0.25) ? 0.02 : 1.0;
                float VisW       = Backface * Backface * ValidGate;

                // 16x16 Chebyshev depth upper-tail visibility weight eliminating wall leaks!
                float2 OctD  = EncodeUnitOctahedral(DirFromC);
                uint   DXId  = min(15u, uint(OctD.x * 16.0));
                uint   DYId  = min(15u, uint(OctD.y * 16.0));
                float2 Mom   = IrradianceChebyshevDepth[CellId * 256u + DYId * 16u + DXId];
                if (DistToCell > Mom.x + 1.0e-3 && Mom.y > 0.0)
                {
                    float Variance = max(1.0e-4, abs(Mom.y - Mom.x * Mom.x));
                    float Diff     = DistToCell - Mom.x;
                    float Cheby    = Variance / (Variance + Diff * Diff);
                    VisW          *= max(0.0, Cheby * Cheby * Cheby);
                }

                float CombinedW = max(1.0e-7, TriW * VisW);
                Accum          += CellTexel.xyz * CombinedW;
                TotalW         += CombinedW;
            }
        }
    }
    return (TotalW > 0.0) ? (Accum / TotalW) : float3(0.0);
}

// Hybrid ReSTIR Path Replay GI for EngineMode == 3 (4-Candidate Owen-Sobol RIS + 1-Bounce BVH Replay + DynamicProbe 2nd Bounce + Temporal Reuse)
float3 EvaluateWeightedPathReplayCandidate(float3 WorldPos, float3 Normal, uint2 PixelXY, uint PixelIdx)
{
    (void)PixelXY;
    float3 T, B;
    ConstructOrthonormalBasis(Normal, T, B);

    uint  SeqIdx    = WorldUniforms.DenoiseStepParams.w;
    uint  PixelHash = AvalancheKey32(PixelXY.x * 1973u + PixelXY.y * 9277u + 89173u + SeqIdx * 104729u);
    uint  Rng       = PixelHash | 1u;
    float3 SunDir   = WorldUniforms.SunDirConeTan.xyz;
    float3 SunIrr   = WorldUniforms.SolarIrradianceTier.xyz;
    uint  MatCount  = max(1u, WorldUniforms.LatticeDimsMaterialCount.w);
    uint  LightCnt  = min(4u, WorldUniforms.SdfDimsLightCount.w);
    bool  HasProbes = (WorldUniforms.LatticeDimsMaterialCount.x >= 2u);

    float3 WinPos  = float3(0.0);
    float3 WinRad  = float3(0.0);
    float  WinPdf  = 0.0;
    uint   WinSky  = 0u;
    float  WeightS = 0.0;

    for (uint C = 0u; C < 4u; ++C)
    {
        float2 U    = Sobol02Sample(SeqIdx * 4u + C, PixelHash, 0u);
        float  Phi  = TauConst * U.x;
        float  SinT = sqrt(saturate(U.y));
        float  CosT = sqrt(max(0.0, 1.0 - U.y));
        float3 Dir  = normalize(T * (SinT * cos(Phi)) + B * (SinT * sin(Phi)) + Normal * CosT);

        RayHitReading Hit2 = TraceSceneClosest(WorldPos + Normal * 2.5e-3, Dir, 1.0e-3, 80.0);

        float3 CandPos = Dir * 1.0e4;
        float3 CandRad = float3(0.0);
        uint   CandSky = 1u;

        if (!Hit2.Hit)
        {
            CandRad = EvaluateSkyDome(Dir);
        }
        else
        {
            CandSky = 0u;
            CandPos = Hit2.Position;
            float3 N2 = Hit2.ShadingNormal;
            if (dot(N2, Dir) > 0.0) N2 = -N2;
            GpuWorldMaterial80 Mat2 = MaterialEntries[min(Hit2.MaterialSlot, MatCount - 1u)];
            float3 SafeHit2 = Hit2.Position + N2 * 4.0e-3;
            float3 DiffAlb2 = Mat2.DiffuseAlbedoRoughness.xyz * (1.0 - saturate(Mat2.SpecParams.y)) * (1.0 - saturate(Mat2.CoatRoughnessOptics.y));

            CandRad += Mat2.EmissiveRadiancePad.xyz;
            if (dot(N2, SunDir) > 0.0 && !TraceSceneAnyHit(SafeHit2, SunDir, 0.02, 80.0))
            {
                CandRad += EvaluateSurfaceBrdf(Mat2, N2, -Dir, SunDir) * SunIrr;
            }
            for (uint L = 0u; L < LightCnt; ++L)
            {
                CandRad += EvaluateAreaQuadLight(WorldUniforms.AreaLights[L], SafeHit2, N2, -Dir, Mat2);
            }
            if (HasProbes)
            {
                CandRad += SampleRtxIrradianceLatticeChebyshev(Hit2.Position, N2, -Dir) * DiffAlb2;
            }
            else
            {
                CandRad += (WorldUniforms.SkyRadianceBvhCount.xyz * 0.35) * DiffAlb2;
            }
        }

        float Luma = max(1.0e-5, dot(CandRad, float3(0.2126, 0.7152, 0.0722)));
        float Pdf  = Luma * (max(1.0e-4, CosT) * InvPiConst);
        WeightS   += Luma;
        if (PcgFloat01(Rng) < (Luma / max(1.0e-12, WeightS)))
        {
            WinPos = CandPos;
            WinRad = CandRad;
            WinPdf = Pdf;
            WinSky = CandSky;
        }
    }

    uint TotalM = 4u;
    if (WorldUniforms.DenoiseStepParams.z != 0u && WorldUniforms.PrevCameraUpWidth.w > 1.0)
    {
        float3 RelP  = WorldPos - WorldUniforms.PrevCameraOriginTanX.xyz;
        float  DepP  = dot(RelP, WorldUniforms.PrevCameraForwardHeight.xyz);
        if (DepP > 0.05)
        {
            float NdcX = dot(RelP, WorldUniforms.PrevCameraRightTanY.xyz) / (DepP * max(1.0e-4, WorldUniforms.PrevCameraOriginTanX.w));
            float NdcY = dot(RelP, WorldUniforms.PrevCameraUpWidth.xyz)   / (DepP * max(1.0e-4, WorldUniforms.PrevCameraRightTanY.w));
            int   PX   = int(floor((NdcX * 0.5 + 0.5) * WorldUniforms.PrevCameraUpWidth.w));
            int   PY   = int(floor((0.5 - NdcY * 0.5) * WorldUniforms.PrevCameraForwardHeight.w));
            int   PW   = int(WorldUniforms.PrevCameraUpWidth.w + 0.5);
            int   PH   = int(WorldUniforms.PrevCameraForwardHeight.w + 0.5);
            if (PX >= 0 && PX < PW && PY >= 0 && PY < PH)
            {
                uint   PrevIdx = uint(PY) * uint(PW) + uint(PX);
                float4 Prev0   = ReservoirHistoryBuffer[PrevIdx * 2u + 0u];
                float4 Prev1   = ReservoirHistoryBuffer[PrevIdx * 2u + 1u];
                uint   PMeta   = asuint(Prev1.w);
                uint   PM      = min(PMeta & 0xFFFFu, 20u);
                if (((PMeta & (1u << 16u)) != 0u) && PM > 0u)
                {
                    bool   PSky  = ((PMeta & (1u << 17u)) != 0u);
                    float3 PDir  = PSky ? normalize(Prev0.xyz) : normalize(Prev0.xyz - WorldPos);
                    float  PPdf  = dot(Prev1.rgb, float3(0.2126, 0.7152, 0.0722)) * max(0.0, dot(Normal, PDir)) * InvPiConst;
                    if (PPdf > 1.0e-6)
                    {
                        float TempW = PPdf * Prev0.w * float(PM);
                        TotalM  += PM;
                        WeightS += TempW;
                        if (PcgFloat01(Rng) < (TempW / max(1.0e-12, WeightS)))
                        {
                            WinPos = Prev0.xyz;
                            WinRad = Prev1.rgb;
                            WinPdf = PPdf;
                            WinSky = PSky ? 1u : 0u;
                        }
                    }
                }
            }
        }
    }

    float3 DirY    = (WinSky != 0u) ? normalize(WinPos) : normalize(WinPos - WorldPos);
    float  CosY    = max(0.0, dot(Normal, DirY));
    float  MaxRawW = (20.0 * PiConst) / max(1.0e-3, CosY);
    float  FinalW  = (WinPdf > 1.0e-9) ? clamp(WeightS / (float(TotalM) * WinPdf), 0.0, MaxRawW) : 0.0;
    float3 GiVal   = WinRad * ((CosY * InvPiConst) * FinalW);

    uint Meta = min(TotalM, 0xFFFFu) | (1u << 16u) | ((WinSky & 1u) << 17u);
    WeightedCurrentBuffer[PixelIdx * 2u + 0u] = float4(WinPos, FinalW);
    WeightedCurrentBuffer[PixelIdx * 2u + 1u] = float4(WinRad, asfloat(Meta));
    return GiVal;
}

//------------------------------------------------------------------------------------------------------------------------
// SPECULAR REFLECTIONS (Zero SDF Reflections! BVH4 WideTreeRaytrace or Analytical Sky Environment)
//------------------------------------------------------------------------------------------------------------------------

float3 ShadeSecondaryBounce(float3 RayOrigin, float3 RayDir)
{
    uint ReflSource = WorldUniforms.EngineShadowReflSample.z;
    if (ReflSource == 1u)
    {
        return EvaluateSkyDome(RayDir);
    }

    RayHitReading Hit = TraceSceneClosest(RayOrigin, RayDir, 2.0e-3, 80.0);
    if (!Hit.Hit)
    {
        return EvaluateSkyDome(RayDir);
    }

    uint MatCount = max(1u, WorldUniforms.LatticeDimsMaterialCount.w);
    GpuWorldMaterial80 Mat = MaterialEntries[min(Hit.MaterialSlot, MatCount - 1u)];
    float3 V      = -RayDir;
    float3 N      = Hit.ShadingNormal;
    float3 SunDir = WorldUniforms.SunDirConeTan.xyz;
    float3 SunIrr = WorldUniforms.SolarIrradianceTier.xyz;

    float3 SafeSecondaryPos = Hit.Position + Hit.GeomNormal * 0.012 + N * 0.006;
    float3 ColorHdr = Mat.EmissiveRadiancePad.xyz;
    if (dot(N, SunDir) > 0.0 && !TraceSceneAnyHit(SafeSecondaryPos, SunDir, 0.032, 80.0))
    {
        float TermGate = saturate(dot(Hit.GeomNormal, SunDir) * 6.0 + 0.35);
        ColorHdr += EvaluateSurfaceBrdf(Mat, N, V, SunDir) * SunIrr * TermGate;
    }

    uint LightCnt = min(4u, WorldUniforms.SdfDimsLightCount.w);
    for (uint L = 0u; L < LightCnt; ++L)
    {
        ColorHdr += EvaluateAreaQuadLight(WorldUniforms.AreaLights[L], SafeSecondaryPos, N, V, Mat);
    }

    float3 Albedo = Mat.DiffuseAlbedoRoughness.xyz * (1.0 - saturate(Mat.SpecParams.y)) * (1.0 - saturate(Mat.CoatRoughnessOptics.y));
    bool   HasDdgi = ((WorldUniforms.EngineShadowReflSample.x == 2u || WorldUniforms.EngineShadowReflSample.x == 3u) &&
                      WorldUniforms.SolarIrradianceTier.w > 0.5);
    float3 AmbGi   = HasDdgi
                        ? SampleRtxIrradianceLatticeChebyshev(Hit.Position, N, V)
                        : (WorldUniforms.SkyRadianceBvhCount.xyz * 0.42);
    ColorHdr += Albedo * AmbGi;
    return ColorHdr;
}

float3 EvaluateMatCapSurface(RayHitReading Hit)
{
    uint MatCount = max(1u, WorldUniforms.LatticeDimsMaterialCount.w);
    GpuWorldMaterial80 Mat = MaterialEntries[min(Hit.MaterialSlot, MatCount - 1u)];
    float3 N      = Hit.ShadingNormal;
    float3 CamR   = WorldUniforms.CameraRightTanY.xyz;
    float3 CamU   = WorldUniforms.CameraUpWidth.xyz;
    float3 CamFwd = WorldUniforms.CameraForwardHeight.xyz;

    float3 Vn = normalize(float3(dot(N, CamR), dot(N, CamU), max(0.0, dot(N, -CamFwd))));
    float3 KeyDir  = normalize(float3(0.42, 0.62, 0.66));
    float3 FillDir = normalize(float3(-0.68, 0.25, 0.68));
    float3 RimDir  = normalize(float3(0.0, 0.85, 0.52));

    float KeyDiff  = max(0.0, dot(Vn, KeyDir));
    float FillDiff = max(0.0, dot(Vn, FillDir));
    float RimGlow  = pow(saturate(1.0 - Vn.z), 2.6) * (0.35 + 0.65 * max(0.0, dot(Vn, RimDir)));
    float SpecPow  = lerp(96.0, 6.0, clamp(Mat.SpecParams.x, 0.02, 1.0));
    float KeySpec  = pow(max(0.0, dot(Vn, normalize(KeyDir + float3(0.0, 0.0, 1.0)))), SpecPow);
    float SpecGain = lerp(1.45, 0.18, saturate(Mat.SpecParams.x));
    float Metal    = saturate(Mat.SpecParams.y);
    float StudioL  = 0.14 + 0.88 * KeyDiff + 0.34 * FillDiff;

    float3 Base = Mat.DiffuseAlbedoRoughness.xyz;
    return Base * (StudioL * (1.0 - Metal * 0.78) + KeySpec * SpecGain * Metal)
         + float3(KeySpec * SpecGain * (1.0 - Metal))
         + float3(0.26, 0.28, 0.32) * RimGlow
         + Mat.EmissiveRadiancePad.xyz;
}

void ShadePrimarySurfaceDemod(
    RayHitReading Hit,
    float3        ViewDir,
    uint2         PixelXY,
    out float3    OutDirectBaseHdr,
    out float3    OutSpecReflHdr,
    out float3    OutUnoccludedSunHdr,
    out float3    OutDemodGiOverPi,
    out float3    OutDiffuseAlbedoMatte,
    out float     OutSunVis,
    out float     OutBlockerDist,
    out float     OutDiffAo,
    out float     OutSpecRough,
    out float     OutSpecAo)
{
    uint EngineMode = WorldUniforms.EngineShadowReflSample.x;
    uint MatCount   = max(1u, WorldUniforms.LatticeDimsMaterialCount.w);
    GpuWorldMaterial80 Mat = MaterialEntries[min(Hit.MaterialSlot, MatCount - 1u)];
    float3 N      = Hit.ShadingNormal;
    float3 SunDir = WorldUniforms.SunDirConeTan.xyz;
    float3 SunIrr = WorldUniforms.SolarIrradianceTier.xyz;

    // 1. Direct Solar Shadow (Two-Cascade Fine/Coarse + BVH4 Contact Gate)
    OutDirectBaseHdr      = Mat.EmissiveRadiancePad.xyz;
    OutSpecReflHdr        = float3(0.0);
    OutUnoccludedSunHdr   = float3(0.0);
    OutSunVis             = 1.0;
    OutBlockerDist        = 0.0;
    float3 SafePrimaryPos = Hit.Position + Hit.GeomNormal * 0.012 + N * 0.006;
    float  NdotL          = dot(N, SunDir);
    if (NdotL > 1.0e-5)
    {
        float EnvSunTrans = EvaluateSolarCloudTransmittanceGpu(Hit.Position, SunDir)
                          * EvaluateFluidShadowTransmittanceBufferGpu(
                                WorldUniforms.FluidParams, FluidHeroThermoTexels, FluidWorldThermoTexels, SafePrimaryPos, SunDir, 32.0);
        OutUnoccludedSunHdr = EvaluateSurfaceBrdf(Mat, N, ViewDir, SunDir) * SunIrr * EnvSunTrans;
        OutSunVis           = EvaluateSolarShadow(Hit, SunDir, PixelXY, OutBlockerDist);
    }
    else
    {
        OutSunVis = 0.0;
    }

    // 2. Upper-Hemisphere Clipped Polygonal LTC Quad Area Lights
    uint LightCnt = min(4u, WorldUniforms.SdfDimsLightCount.w);
    for (uint L = 0u; L < LightCnt; ++L)
    {
        OutDirectBaseHdr += EvaluateAreaQuadLight(WorldUniforms.AreaLights[L], SafePrimaryPos, N, ViewDir, Mat);
    }

    // 2b. Dynamic Fluid Blackbody Fire Proxy Lights
    if (WorldUniforms.FluidParams.HeroBoxMinFade.w > 0.5)
    {
        for (uint FL = 0u; FL < 8u; ++FL)
        {
            FluidEmissiveLightGpu FLgt = WorldUniforms.FluidParams.EmissiveLights[FL];
            if (FLgt.ColorIrradiance.w < 0.5) continue;
            float3 ToL = FLgt.PositionRadius.xyz - SafePrimaryPos;
            float Dist2 = max(0.08, dot(ToL, ToL) + FLgt.PositionRadius.w * FLgt.PositionRadius.w * 0.35);
            float Dist = sqrt(Dist2);
            float3 Wi = ToL / Dist;
            float LCos = dot(N, Wi);
            if (LCos > 1.0e-4)
            {
                float Atten = LCos / (4.0 * PiConst * Dist2);
                OutDirectBaseHdr += EvaluateSurfaceBrdf(Mat, N, ViewDir, Wi) * FLgt.ColorIrradiance.rgb * Atten;
            }
        }
    }

    // 3. Fast BVH4 Ambient Occlusion (IGN-jittered 8-ray, Zero SDF AO!) + Lagarde Specular AO
    float2 R2P = PixelR2(PixelXY);
    {
        float3 T, B;
        ConstructOrthonormalBasis(N, T, B);
        float3 AoOrig = Hit.Position + Hit.GeomNormal * 0.008;
        float  OccSum = 0.0;
        for (uint R = 0u; R < 8u; ++R)
        {
            float U1 = (float(R) + 0.15 + 0.7 * R2P.y) * 0.125;
            float Phi = float(R) * 2.39996323 + R2P.x * TauConst;
            float SinT = sqrt(saturate(U1));
            float CosT = sqrt(max(0.0, 1.0 - U1));
            float3 AoDir = T * (SinT * cos(Phi)) + B * (SinT * sin(Phi)) + N * CosT;
            if (!TraceSceneAnyHit(AoOrig, AoDir, 0.006, 0.85))
            {
                OccSum += 0.125;
            }
        }
        OutDiffAo = saturate(0.28 + 0.72 * OccSum);
    }
    float NdotV  = max(1.0e-3, dot(N, ViewDir));
    OutSpecRough = clamp(Mat.SpecParams.x, 0.025, 1.0);
    OutSpecAo    = saturate(pow(NdotV + OutDiffAo, exp2(-16.0 * OutSpecRough - 1.0)) - 1.0 + OutDiffAo);

    // 4. Engine-Specific Demodulated Diffuse GI (filtered separately in DenoiseCompositeEntry!)
    float Metalness       = saturate(Mat.SpecParams.y);
    float TransW          = saturate(Mat.CoatRoughnessOptics.y);
    OutDiffuseAlbedoMatte = Mat.DiffuseAlbedoRoughness.xyz * (1.0 - Metalness) * (1.0 - TransW);
    OutDemodGiOverPi      = float3(0.0);
    if (IsGiEnabled())
    {
        if (EngineMode == 1u)
        {
            OutDemodGiOverPi = WorldUniforms.SkyRadianceBvhCount.xyz * 0.20;
        }
        else if (EngineMode == 2u)
        {
            OutDemodGiOverPi = SampleRtxIrradianceLatticeChebyshev(Hit.Position, N, ViewDir);
        }
        else if (EngineMode == 3u)
        {
            uint Width = max(1u, uint(WorldUniforms.CameraUpWidth.w + 0.5));
            uint PixelIdx = PixelXY.y * Width + PixelXY.x;
            OutDemodGiOverPi = EvaluateWeightedPathReplayCandidate(Hit.Position, N, PixelXY, PixelIdx);
        }
    }
    if (!IsGiEnabled() || EngineMode != 3u)
    {
        uint Width    = max(1u, uint(WorldUniforms.CameraUpWidth.w + 0.5));
        uint PixelIdx = PixelXY.y * Width + PixelXY.x;
        WeightedCurrentBuffer[PixelIdx * 2u + 0u] = float4(0.0);
        WeightedCurrentBuffer[PixelIdx * 2u + 1u] = float4(0.0);
    }

    // 5. Secondary Specular Reflection (BVH4 WideTreeRaytrace or SkyEnvironment — Zero SDF!)
    //    Stored un-occluded in OutSpecReflHdr and modulated by denoised OutDiffAo in DenoiseCompositeEntry!
    float CoatW = saturate(Mat.EdgeTintCoatWeight.w);
    if (IsReflectionsEnabled() && (Metalness > 0.20 || CoatW > 0.20 || OutSpecRough < 0.24))
    {
        float3 ReflDir = reflect(-ViewDir, N);
        float3 EnvRad  = ShadeSecondaryBounce(Hit.Position + Hit.GeomNormal * 3.0e-3, ReflDir);
        float  Ior     = max(1.02, Mat.SpecParams.z);
        float  F0Diel  = pow((Ior - 1.0) / (Ior + 1.0), 2.0);
        float3 F0      = lerp(float3(F0Diel), Mat.DiffuseAlbedoRoughness.xyz, Metalness);
        float  Schlick = pow(1.0 - NdotV, 5.0);
        float3 Fresnel = F0 + (float3(1.0) - F0) * Schlick;
        float  Gloss   = saturate(1.0 - OutSpecRough * 0.85);
        if (CoatW > 0.15 && Metalness < 0.2)
        {
            Fresnel = max(Fresnel, float3((0.04 + 0.96 * Schlick) * CoatW));
            Gloss   = max(Gloss, 1.0 - Mat.CoatRoughnessOptics.x);
        }
        OutSpecReflHdr = EnvRad * Fresnel * Gloss;
    }
}

//------------------------------------------------------------------------------------------------------------------------
// DETERMINISTIC PATH TRACER (EngineMode == 4): Owen-Scrambled Sobol (0,2) + Dupuy & Benyoub (2023) Spherical-Cap VNDF + Depth-0 Lobe Split
//------------------------------------------------------------------------------------------------------------------------

float3 SampleSphericalCapGgxVndfLocal(float3 LocalWo, float AlphaX, float AlphaY, float U1, float U2)
{
    float3 Vh  = normalize(float3(AlphaX * LocalWo.x, AlphaY * LocalWo.y, max(1.0e-5, LocalWo.z)));
    float  Phi = TauConst * U2;
    float  Z   = (1.0 - U1) * (1.0 + Vh.z) - Vh.z;
    float  SinT = sqrt(max(0.0, 1.0 - Z * Z));
    float3 C   = float3(SinT * cos(Phi), SinT * sin(Phi), Z);
    float3 Nh  = C + Vh;
    float3 Out = float3(AlphaX * Nh.x, AlphaY * Nh.y, max(1.0e-4, Nh.z));
    return normalize(Out);
}

float EvaluateSphericalCapGgxVndfPdf(float3 LocalWo, float3 LocalWi, float AlphaX, float AlphaY)
{
    if (LocalWo.z <= 1.0e-4 || LocalWi.z <= 1.0e-4) return 0.0;
    float3 HalfV = normalize(LocalWo + LocalWi);
    float  NdotH = max(1.0e-4, HalfV.z);
    float  Ax    = max(0.0015, AlphaX);
    float  Ay    = max(0.0015, AlphaY);
    float  TermH = (HalfV.x * HalfV.x) / (Ax * Ax) + (HalfV.y * HalfV.y) / (Ay * Ay) + NdotH * NdotH;
    float  D     = 1.0 / (PiConst * Ax * Ay * TermH * TermH);
    float  Tan2V = ((LocalWo.x * Ax) * (LocalWo.x * Ax) + (LocalWo.y * Ay) * (LocalWo.y * Ay)) / max(1.0e-6, LocalWo.z * LocalWo.z);
    float  G1V   = 2.0 / (1.0 + sqrt(1.0 + Tan2V));
    return (D * G1V) / (4.0 * max(1.0e-4, LocalWo.z));
}

bool IntersectGlassSphere(float3 Origin, float3 Dir, float3 Center, float Radius, out float TEnter, out float TExit)
{
    TEnter = -1.0;
    TExit  = -1.0;
    float3 Oc   = Origin - Center;
    float  B    = dot(Oc, Dir);
    float  C    = dot(Oc, Oc) - Radius * Radius;
    float  Disc = B * B - C;
    if (Disc <= 0.0) return false;
    float SqrtD = sqrt(Disc);
    TEnter      = -B - SqrtD;
    TExit       = -B + SqrtD;
    return TExit > 1.0e-3;
}

int FindClosestGlassSphere(float3 RayO, float3 RayD, float MinT, float MaxT, out float OutT)
{
    OutT = MaxT;
    int  BestIdx  = -1;
    uint GlassCnt = min(4u, uint(WorldUniforms.LatticeMinGlassCount.w + 0.5));
    for (uint G = 0u; G < GlassCnt; ++G)
    {
        float TEn = 0.0, TEx = 0.0;
        if (IntersectGlassSphere(
                RayO,
                RayD,
                WorldUniforms.GlassSpheres[G].CenterRadiusPacked.xyz,
                WorldUniforms.GlassSpheres[G].CenterRadiusPacked.w,
                TEn,
                TEx))
        {
            float HitT = (TEn > MinT) ? TEn : TEx;
            if (HitT > MinT && HitT < OutT)
            {
                OutT    = HitT;
                BestIdx = int(G);
            }
        }
    }
    return BestIdx;
}

bool IntersectAreaLightsClosest(float3 RayO, float3 RayD, float MinT, float MaxT, out float3 OutEmission, out float OutT)
{
    OutEmission = float3(0.0);
    OutT        = MaxT;
    bool AnyHit = false;
    uint LightCnt = min(4u, WorldUniforms.SdfDimsLightCount.w);
    for (uint LIdx = 0u; LIdx < LightCnt; ++LIdx)
    {
        GpuAreaQuadLight Q = WorldUniforms.AreaLights[LIdx];
        if (max(Q.RadiancePacked.x, max(Q.RadiancePacked.y, Q.RadiancePacked.z)) <= 1.0e-4) continue;
        float3 LNorm = normalize(cross(Q.RightHalfHeight.xyz, Q.UpTwoSided.xyz));
        float  Den   = dot(LNorm, RayD);
        bool   Front = (Q.UpTwoSided.w > 0.5) ? (abs(Den) > 1.0e-5) : (Den < -1.0e-5);
        if (!Front) continue;
        float T = dot(Q.CenterHalfWidth.xyz - RayO, LNorm) / Den;
        if (T >= MinT && T < OutT)
        {
            float3 Rel = (RayO + RayD * T) - Q.CenterHalfWidth.xyz;
            float  U   = dot(Rel, Q.RightHalfHeight.xyz);
            float  V   = dot(Rel, Q.UpTwoSided.xyz);
            if (abs(U) <= Q.CenterHalfWidth.w && abs(V) <= Q.RightHalfHeight.w)
            {
                OutT        = T;
                OutEmission = Q.RadiancePacked.xyz;
                AnyHit      = true;
            }
        }
    }
    return AnyHit;
}

float3 TracePathTail(
    float3 RayO,
    float3 RayD,
    float3 InitialThroughput,
    uint   StartBounce,
    uint   MaxBounces,
    uint2  PixelXY,
    uint   PixelHash,
    uint   SampleIdx,
    uint   PairOffset,
    bool   PrevWasSpecular)
{
    uint   MatCount = max(1u, WorldUniforms.LatticeDimsMaterialCount.w);
    float3 SunDir   = WorldUniforms.SunDirConeTan.xyz;
    float3 SunIrr   = WorldUniforms.SolarIrradianceTier.xyz;
    float  TanSun   = max(0.004, WorldUniforms.SunDirConeTan.w);
    float3 SunT, SunB;
    ConstructOrthonormalBasis(SunDir, SunT, SunB);

    float3 Radiance   = float3(0.0);
    float3 Throughput = InitialThroughput;
    bool   SpecBounce = PrevWasSpecular;

    for (uint B = StartBounce; B < MaxBounces; ++B)
    {
        RayHitReading Hit = TraceSceneClosest(RayO, RayD, 1.5e-3, 120.0);
        float ClosestTriT = Hit.Hit ? Hit.Distance : 120.0;

        // Check analytical Glass Spheres on secondary bounces too
        float GlassT   = ClosestTriT;
        int   GlassIdx = FindClosestGlassSphere(RayO, RayD, 1.5e-3, ClosestTriT, GlassT);
        if (GlassIdx >= 0)
        {
            GpuGlassSphere Glass = WorldUniforms.GlassSpheres[GlassIdx];
            float3 Center   = Glass.CenterRadiusPacked.xyz;
            float  Radius   = Glass.CenterRadiusPacked.w;
            float3 GPos     = RayO + RayD * GlassT;
            float3 GNrm     = normalize(GPos - Center);
            float  CrownIor = max(1.05, Glass.OpticsSlotPacked.x);
            float  Disp     = clamp(0.45 / max(10.0, Glass.OpticsSlotPacked.y), 0.004, 0.028);
            float  CosI     = saturate(dot(GNrm, -RayD));
            float  F0       = pow((CrownIor - 1.0) / (CrownIor + 1.0), 2.0);
            float  Fresnel  = F0 + (1.0 - F0) * pow(1.0 - CosI, 5.0);
            uint   GlassPair = 1u + PairOffset + B * 5u + 4u;
            float2 GS       = Sobol02Sample(SampleIdx, PixelHash, GlassPair);

            if (GS.x < Fresnel)
            {
                RayD = reflect(RayD, GNrm);
                RayO = GPos + GNrm * 2.5e-3;
            }
            else
            {
                float IorSample = CrownIor + ((GS.y < 0.333333) ? -Disp : ((GS.y < 0.666667) ? 0.0 : Disp * 1.15));
                float3 InsideD  = refract(RayD, GNrm, 1.0 / IorSample);
                if (dot(InsideD, InsideD) < 1.0e-5)
                {
                    RayD = reflect(RayD, GNrm);
                    RayO = GPos + GNrm * 2.5e-3;
                }
                else
                {
                    float TIn = 0.0, TOut = 0.0;
                    if (IntersectGlassSphere(GPos + InsideD * 2.0e-3, InsideD, Center, Radius, TIn, TOut) && TOut > 0.0)
                    {
                        float3 ExitPos = GPos + InsideD * (2.0e-3 + TOut);
                        float3 OutNrm  = normalize(ExitPos - Center);
                        float3 ExitDir = refract(InsideD, -OutNrm, IorSample);
                        if (dot(ExitDir, ExitDir) < 1.0e-5) ExitDir = reflect(InsideD, -OutNrm);
                        RayO = ExitPos + OutNrm * 3.0e-3;
                        RayD = normalize(ExitDir);
                    }
                    else
                    {
                        RayO = GPos + InsideD * (Radius * 2.1);
                        RayD = InsideD;
                    }
                    Throughput *= float3(0.95, 0.98, 1.00);
                }
            }
            SpecBounce = true;
            continue;
        }

        // Specular/glass bounce hits on analytical Area Quad Lights (visible softbox in chrome/glass!)
        if (SpecBounce)
        {
            float3 LightEmit = float3(0.0);
            float  LightT    = ClosestTriT;
            if (IntersectAreaLightsClosest(RayO, RayD, 1.5e-3, ClosestTriT, LightEmit, LightT))
            {
                Radiance += Throughput * LightEmit;
                break;
            }
        }

        if (!Hit.Hit)
        {
            Radiance += Throughput * EvaluateSkyDome(RayD);
            break;
        }

        GpuWorldMaterial80 Mat = MaterialEntries[min(Hit.MaterialSlot, MatCount - 1u)];
        Radiance += Throughput * Mat.EmissiveRadiancePad.xyz;
        float3 N = Hit.ShadingNormal;
        float3 V = -RayD;
        float3 T, BVec;
        ConstructOrthonormalBasis(N, T, BVec);

        uint BasePair = 1u + PairOffset + B * 5u;

        // NEE: Solar Disc (Sobol Pair BasePair + 0)
        {
            float2 SunS = Sobol02Sample(SampleIdx, PixelHash, BasePair);
            float  JR   = sqrt(SunS.x) * TanSun;
            float  JA   = SunS.y * TauConst;
            float3 L    = normalize(SunDir + SunT * (JR * cos(JA)) + SunB * (JR * sin(JA)));
            if (dot(N, L) > 0.0 && !TraceSceneAnyHit(Hit.Position + Hit.GeomNormal * 0.008, L, 0.010, 90.0))
            {
                Radiance += Throughput * EvaluateSurfaceBrdf(Mat, N, V, L) * SunIrr * EvaluateSolarCloudTransmittanceGpu(Hit.Position, L);
            }
        }

        // NEE: Area Quad Lights (Sobol Pair BasePair + 1)
        uint LightCnt = min(4u, WorldUniforms.SdfDimsLightCount.w);
        if (LightCnt > 0u)
        {
            float2 AreaS = Sobol02Sample(SampleIdx, PixelHash, BasePair + 1u);
            for (uint LIdx = 0u; LIdx < LightCnt; ++LIdx)
            {
                GpuAreaQuadLight Q = WorldUniforms.AreaLights[LIdx];
                if (max(Q.RadiancePacked.x, max(Q.RadiancePacked.y, Q.RadiancePacked.z)) <= 1.0e-4) continue;
                float LU = frac(AreaS.x + float(LIdx) * 0.6180339887) * 2.0 - 1.0;
                float LV = frac(AreaS.y + float(LIdx) * 0.3819660113) * 2.0 - 1.0;
                float3 Target = Q.CenterHalfWidth.xyz
                              + Q.RightHalfHeight.xyz * (Q.CenterHalfWidth.w * LU)
                              + Q.UpTwoSided.xyz      * (Q.RightHalfHeight.w * LV);
                float3 ToL = Target - Hit.Position;
                float  D2  = dot(ToL, ToL);
                float  D   = sqrt(max(1.0e-6, D2));
                float3 LDir = ToL / D;
                float  NdotL = dot(N, LDir);
                if (NdotL > 0.0 && !TraceSceneAnyHit(Hit.Position + Hit.GeomNormal * 0.008, LDir, 0.010, max(0.015, D - 0.015)))
                {
                    float3 LNorm = normalize(cross(Q.RightHalfHeight.xyz, Q.UpTwoSided.xyz));
                    float  LCos  = (Q.UpTwoSided.w > 0.5) ? abs(dot(LNorm, -LDir)) : max(0.0, dot(LNorm, -LDir));
                    float  Area  = 4.0 * Q.CenterHalfWidth.w * Q.RightHalfHeight.w;
                    float  SafeD2 = max(0.04, D2);
                    float  PdfArea = SafeD2 / max(1.0e-5, LCos * Area);
                    float3 LocalWoL = float3(dot(V, T), dot(V, BVec), max(1.0e-4, dot(V, N)));
                    float3 LocalWiL = float3(dot(LDir, T), dot(LDir, BVec), max(1.0e-4, NdotL));
                    float  RoughL   = clamp(Mat.SpecParams.x, 0.025, 1.0);
                    float  AlphaL   = max(0.0015, RoughL * RoughL);
                    float  DiffWtL  = 1.0 - saturate(Mat.SpecParams.y);
                    float  PdfBsdfL = DiffWtL * (NdotL * InvPiConst) + (1.0 - DiffWtL * 0.5) * EvaluateSphericalCapGgxVndfPdf(LocalWoL, LocalWiL, AlphaL, AlphaL);
                    float  MisW     = (B == 0u || SpecBounce) ? 1.0 : ((PdfArea * PdfArea) / max(1.0e-10, PdfArea * PdfArea + PdfBsdfL * PdfBsdfL));
                    float  Geom     = (NdotL * MisW) / max(1.0e-4, PdfArea);
                    Radiance += Throughput * EvaluateSurfaceBrdf(Mat, N, V, LDir) * Q.RadiancePacked.xyz * Geom;
                }
            }
        }

        if (B + 1u >= MaxBounces) break;

        // MIS Next-Bounce: Sobol Pair BasePair + 2 (Lobe & Direction) + Pair BasePair + 3 (Selector)
        float3 LocalWo = float3(dot(V, T), dot(V, BVec), max(1.0e-4, dot(V, N)));
        float  Rough   = clamp(Mat.SpecParams.x, 0.025, 1.0);
        float  Alpha   = max(0.0015, Rough * Rough);
        float  DiffP   = IsReflectionsEnabled() ? clamp((1.0 - saturate(Mat.SpecParams.y)) * 0.65, 0.0, 0.85) : 1.0;
        float  SpecP   = 1.0 - DiffP;
        float2 DirS    = Sobol02Sample(SampleIdx, PixelHash, BasePair + 2u);
        float2 SelS    = Sobol02Sample(SampleIdx, PixelHash, BasePair + 3u);

        float3 LocalWi;
        bool   ChoseSpec = false;
        if (SelS.x < DiffP)
        {
            float R   = sqrt(DirS.x);
            float Phi = DirS.y * TauConst;
            LocalWi = float3(R * cos(Phi), R * sin(Phi), sqrt(max(0.0, 1.0 - DirS.x)));
        }
        else
        {
            float3 H = SampleSphericalCapGgxVndfLocal(LocalWo, Alpha, Alpha, DirS.x, DirS.y);
            LocalWi   = reflect(-LocalWo, H);
            ChoseSpec = (Rough < 0.28);
        }
        if (LocalWi.z <= 1.0e-4) break;

        float3 WorldWi = normalize(T * LocalWi.x + BVec * LocalWi.y + N * LocalWi.z);
        float  PdfSpec = EvaluateSphericalCapGgxVndfPdf(LocalWo, LocalWi, Alpha, Alpha);
        float  PdfDiff = LocalWi.z * InvPiConst;
        float  MixPdf  = max(1.0e-4, DiffP * PdfDiff + SpecP * PdfSpec);

        Throughput = Throughput * EvaluateSurfaceBrdf(Mat, N, V, WorldWi) / MixPdf;
        RayO       = Hit.Position + Hit.GeomNormal * 0.006;
        RayD       = WorldWi;
        SpecBounce = ChoseSpec;
    }

    return max(Radiance, float3(0.0));
}

float3 TraceDeterministicPathSample(
    float3            CamOrigin,
    float3            CamRayDir,
    uint2             PixelXY,
    uint              PixelHash,
    uint              SampleIdx,
    out RayHitReading OutPrimaryHit,
    out int           OutGlassIdx,
    out float         OutGlassT,
    out float3        OutDemodAlbedo,
    out float         OutSpecRough)
{
    uint MaxBounces = IsGiEnabled() ? clamp(uint(WorldUniforms.SunCascadeForwardPad.w + 0.5), 1u, 6u) : 1u;
    uint MatCount   = max(1u, WorldUniforms.LatticeDimsMaterialCount.w);

    OutPrimaryHit  = TraceSceneClosest(CamOrigin, CamRayDir, 0.08, 120.0);
    float TriMaxT  = OutPrimaryHit.Hit ? OutPrimaryHit.Distance : 120.0;
    OutGlassIdx    = FindClosestGlassSphere(CamOrigin, CamRayDir, 0.08, TriMaxT, OutGlassT);
    OutDemodAlbedo = float3(1.0);
    OutSpecRough   = 0.0;

    if (OutGlassIdx < 0 && !OutPrimaryHit.Hit)
    {
        return float3(0.0);
    }

    float3 Radiance   = float3(0.0);
    uint   TaskCount  = 0u;
    uint   TaskMaxB   = MaxBounces;

    float3 GlassCenter = float3(0.0);
    float  GlassRadius = 0.4;
    float3 GlassHitPos = float3(0.0);
    float3 GlassHitNrm = float3(0.0, 0.0, 1.0);
    float  GlassIor    = 1.5;
    float  GlassDisp   = 0.01;
    float  GlassFres   = 0.0;

    GpuWorldMaterial80 Mat = MaterialEntries[0u];
    float3 SurfN = float3(0.0, 0.0, 1.0), SurfV = float3(0.0, 0.0, 1.0);
    float3 SurfT = float3(1.0, 0.0, 0.0), SurfB = float3(0.0, 1.0, 0.0);
    float  SurfRough = 0.5, SurfMetal = 0.0;

    if (OutGlassIdx >= 0)
    {
        GpuGlassSphere Glass = WorldUniforms.GlassSpheres[OutGlassIdx];
        GlassCenter = Glass.CenterRadiusPacked.xyz;
        GlassRadius = Glass.CenterRadiusPacked.w;
        GlassHitPos = CamOrigin + CamRayDir * OutGlassT;
        GlassHitNrm = normalize(GlassHitPos - GlassCenter);
        float3 V    = -CamRayDir;
        GlassIor    = max(1.05, Glass.OpticsSlotPacked.x);
        GlassDisp   = clamp(0.45 / max(10.0, Glass.OpticsSlotPacked.y), 0.004, 0.028);
        float CosI  = saturate(dot(GlassHitNrm, V));
        float F0    = pow((GlassIor - 1.0) / (GlassIor + 1.0), 2.0);
        GlassFres   = IsReflectionsEnabled() ? (F0 + (1.0 - F0) * pow(1.0 - CosI, 5.0)) : 0.0;

        float3 SunDir = WorldUniforms.SunDirConeTan.xyz;
        float3 HSun   = normalize(V + SunDir);
        float  SpecGl = IsReflectionsEnabled() ? (pow(max(0.0, dot(GlassHitNrm, HSun)), 380.0) * 6.5) : 0.0;
        if (SpecGl > 0.0 && TraceSceneAnyHit(GlassHitPos + GlassHitNrm * 3.0e-3, SunDir, 2.0e-3, 60.0))
        {
            SpecGl = 0.0;
        }
        Radiance  = WorldUniforms.SolarIrradianceTier.xyz * SpecGl;
        TaskMaxB  = max(2u, MaxBounces);
        TaskCount = 4u;
    }
    else
    {
        // Depth-0 Deterministic Lobe Splitting (Direct NEE + Explicit Diffuse Bounce + Explicit GGX Specular Bounce)
        Mat          = MaterialEntries[min(OutPrimaryHit.MaterialSlot, MatCount - 1u)];
        Radiance    += Mat.EmissiveRadiancePad.xyz;
        SurfN        = OutPrimaryHit.ShadingNormal;
        SurfV        = -CamRayDir;
        SurfMetal    = saturate(Mat.SpecParams.y);
        SurfRough    = clamp(Mat.SpecParams.x, 0.025, 1.0);
        OutSpecRough = SurfRough;
        OutDemodAlbedo = (SurfRough < 0.14 || SurfMetal > 0.5)
                            ? float3(1.0)
                            : max(float3(0.08), Mat.DiffuseAlbedoRoughness.xyz);

        float3 SunDir = WorldUniforms.SunDirConeTan.xyz;
        float3 SunIrr = WorldUniforms.SolarIrradianceTier.xyz;
        float  TanSun = max(0.004, WorldUniforms.SunDirConeTan.w);
        float3 SunT, SunB;
        ConstructOrthonormalBasis(SunDir, SunT, SunB);
        ConstructOrthonormalBasis(SurfN, SurfT, SurfB);

        // Depth-0 NEE: Solar Disc
        {
            float2 SunS = Sobol02Sample(SampleIdx, PixelHash, 1u);
            float  JR   = sqrt(SunS.x) * TanSun;
            float  JA   = SunS.y * TauConst;
            float3 L    = normalize(SunDir + SunT * (JR * cos(JA)) + SunB * (JR * sin(JA)));
            if (dot(SurfN, L) > 0.0 && !TraceSceneAnyHit(OutPrimaryHit.Position + OutPrimaryHit.GeomNormal * 0.008, L, 0.010, 90.0))
            {
                Radiance += EvaluateSurfaceBrdf(Mat, SurfN, SurfV, L) * SunIrr * EvaluateSolarCloudTransmittanceGpu(OutPrimaryHit.Position, L);
            }
        }

        // Depth-0 NEE: Area Quad Lights
        uint LightCnt = min(4u, WorldUniforms.SdfDimsLightCount.w);
        if (LightCnt > 0u)
        {
            float2 AreaS = Sobol02Sample(SampleIdx, PixelHash, 2u);
            for (uint LIdx = 0u; LIdx < LightCnt; ++LIdx)
            {
                GpuAreaQuadLight Q = WorldUniforms.AreaLights[LIdx];
                if (max(Q.RadiancePacked.x, max(Q.RadiancePacked.y, Q.RadiancePacked.z)) <= 1.0e-4) continue;
                float LU = frac(AreaS.x + float(LIdx) * 0.6180339887) * 2.0 - 1.0;
                float LV = frac(AreaS.y + float(LIdx) * 0.3819660113) * 2.0 - 1.0;
                float3 Target = Q.CenterHalfWidth.xyz
                              + Q.RightHalfHeight.xyz * (Q.CenterHalfWidth.w * LU)
                              + Q.UpTwoSided.xyz      * (Q.RightHalfHeight.w * LV);
                float3 ToL = Target - OutPrimaryHit.Position;
                float  D2  = dot(ToL, ToL);
                float  D   = sqrt(max(1.0e-6, D2));
                float3 LDir = ToL / D;
                float  NdotL = dot(SurfN, LDir);
                if (NdotL > 0.0 && !TraceSceneAnyHit(OutPrimaryHit.Position + OutPrimaryHit.GeomNormal * 0.008, LDir, 0.010, max(0.015, D - 0.015)))
                {
                    float3 LNorm = normalize(cross(Q.RightHalfHeight.xyz, Q.UpTwoSided.xyz));
                    float  LCos  = (Q.UpTwoSided.w > 0.5) ? abs(dot(LNorm, -LDir)) : max(0.0, dot(LNorm, -LDir));
                    float  Area  = 4.0 * Q.CenterHalfWidth.w * Q.RightHalfHeight.w;
                    float  Geom  = (NdotL * LCos * Area) / max(1.0e-4, D2);
                    Radiance += EvaluateSurfaceBrdf(Mat, SurfN, SurfV, LDir) * Q.RadiancePacked.xyz * Geom;
                }
            }
        }

        TaskCount = (MaxBounces > 1u) ? 2u : 0u;
    }

    // Single TracePathTail call site (keeps SwiftShader/GPU stack minimal!)
    for (uint Task = 0u; Task < TaskCount; ++Task)
    {
        bool   ValidTask  = false;
        float3 TailOrig   = float3(0.0);
        float3 TailDir    = float3(0.0, 0.0, 1.0);
        float3 TailThr    = float3(1.0);
        float3 MaskRgb    = float3(1.0);
        uint   PairOff    = 0u;
        bool   TaskSpec   = false;

        if (OutGlassIdx >= 0)
        {
            if (Task == 0u)
            {
                if (IsReflectionsEnabled() && GlassFres > 1.0e-4)
                {
                    TailOrig  = GlassHitPos + GlassHitNrm * 2.5e-3;
                    TailDir   = reflect(CamRayDir, GlassHitNrm);
                    TailThr   = float3(1.0);
                    MaskRgb   = float3(GlassFres);
                    PairOff   = 32u;
                    TaskSpec  = true;
                    ValidTask = true;
                }
            }
            else
            {
                float IorC = GlassIor + ((Task == 1u) ? -GlassDisp : ((Task == 2u) ? 0.0 : GlassDisp * 1.15));
                MaskRgb    = ((Task == 1u) ? float3(1.0, 0.0, 0.0) : ((Task == 2u) ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, 1.0))) * (1.0 - GlassFres);
                PairOff    = 64u;
                TaskSpec   = true;
                ValidTask  = true;
                float3 InsideD = refract(CamRayDir, GlassHitNrm, 1.0 / IorC);
                if (dot(InsideD, InsideD) < 1.0e-5)
                {
                    TailOrig = GlassHitPos + GlassHitNrm * 2.5e-3;
                    TailDir  = reflect(CamRayDir, GlassHitNrm);
                    TailThr  = float3(1.0);
                }
                else
                {
                    float TIn = 0.0, TOut = 0.0;
                    if (IntersectGlassSphere(GlassHitPos + InsideD * 2.0e-3, InsideD, GlassCenter, GlassRadius, TIn, TOut) && TOut > 0.0)
                    {
                        float3 ExitPos = GlassHitPos + InsideD * (2.0e-3 + TOut);
                        float3 OutNrm  = normalize(ExitPos - GlassCenter);
                        float3 ExitDir = refract(InsideD, -OutNrm, IorC);
                        if (dot(ExitDir, ExitDir) < 1.0e-5) ExitDir = reflect(InsideD, -OutNrm);
                        TailOrig = ExitPos + OutNrm * 3.0e-3;
                        TailDir  = normalize(ExitDir);
                    }
                    else
                    {
                        TailOrig = GlassHitPos + InsideD * (GlassRadius * 2.1);
                        TailDir  = InsideD;
                    }
                    TailThr = float3(0.95, 0.98, 1.0);
                }
            }
        }
        else if (Task == 0u)
        {
            float  TransW   = saturate(Mat.CoatRoughnessOptics.y);
            float3 DiffAlb  = Mat.DiffuseAlbedoRoughness.xyz * (1.0 - SurfMetal) * (1.0 - TransW);
            if (max(DiffAlb.x, max(DiffAlb.y, DiffAlb.z)) > 0.01)
            {
                float2 DS  = Sobol02Sample(SampleIdx, PixelHash, 3u);
                float  R   = sqrt(DS.x);
                float  Phi = DS.y * TauConst;
                float3 LWi = float3(R * cos(Phi), R * sin(Phi), sqrt(max(0.0, 1.0 - DS.x)));
                if (LWi.z > 1.0e-4)
                {
                    TailOrig  = OutPrimaryHit.Position + OutPrimaryHit.GeomNormal * 0.006;
                    TailDir   = normalize(SurfT * LWi.x + SurfB * LWi.y + SurfN * LWi.z);
                    TailThr   = min(float3(4.0), DiffAlb);
                    PairOff   = 0u;
                    TaskSpec  = false;
                    ValidTask = true;
                }
            }
        }
        else
        {
            float3 LocalWo = float3(dot(SurfV, SurfT), dot(SurfV, SurfB), max(1.0e-4, dot(SurfV, SurfN)));
            float  Alpha   = max(0.0015, SurfRough * SurfRough);
            float  Ior     = max(1.02, Mat.SpecParams.z);
            float  F0Diel  = pow((Ior - 1.0) / (Ior + 1.0), 2.0);
            float3 F0      = lerp(float3(F0Diel), Mat.DiffuseAlbedoRoughness.xyz, SurfMetal);
            float  CoatW   = saturate(Mat.EdgeTintCoatWeight.w);
            if (IsReflectionsEnabled() && (max(F0.x, max(F0.y, F0.z)) > 0.015 || CoatW > 0.05))
            {
                float2 SS  = Sobol02Sample(SampleIdx, PixelHash, 4u);
                float3 H   = SampleSphericalCapGgxVndfLocal(LocalWo, Alpha, Alpha, SS.x, SS.y);
                float3 LWi = reflect(-LocalWo, H);
                if (LWi.z > 1.0e-4)
                {
                    float  VdotH   = saturate(dot(LocalWo, H));
                    float  Schlick = pow(1.0 - VdotH, 5.0);
                    float3 Fresnel = F0 + (float3(1.0) - F0) * Schlick + float3(CoatW * (0.04 + 0.96 * Schlick));
                    float  A2      = Alpha * Alpha;
                    float  Tan2L   = (LWi.x * LWi.x + LWi.y * LWi.y) * A2 / max(1.0e-6, LWi.z * LWi.z);
                    float  G1L     = 2.0 / (1.0 + sqrt(1.0 + Tan2L));
                    TailOrig  = OutPrimaryHit.Position + OutPrimaryHit.GeomNormal * 0.006;
                    TailDir   = normalize(SurfT * LWi.x + SurfB * LWi.y + SurfN * LWi.z);
                    TailThr   = Fresnel * G1L;
                    PairOff   = 32u;
                    TaskSpec  = (SurfRough < 0.28);
                    ValidTask = true;
                }
            }
        }

        if (ValidTask)
        {
            Radiance += MaskRgb * TracePathTail(TailOrig, TailDir, TailThr, 1u, TaskMaxB, PixelXY, PixelHash, SampleIdx, PairOff, TaskSpec);
        }
    }

    return max(Radiance, float3(0.0));
}

float3 TraceThroughGlassSphere(float3 EntryPos, float3 EntryNrm, float3 IncidentDir, float3 Center, float Radius, float IorChannel)
{
    float3 BounceOrig = EntryPos + EntryNrm * 2.5e-3;
    float3 BounceDir  = reflect(IncidentDir, EntryNrm);
    float3 InsideDir  = refract(IncidentDir, EntryNrm, 1.0 / IorChannel);
    if (dot(InsideDir, InsideDir) >= 1.0e-5)
    {
        float TIn = 0.0, TOut = 0.0;
        if (IntersectGlassSphere(EntryPos + InsideDir * 2.0e-3, InsideDir, Center, Radius, TIn, TOut) && TOut > 0.0)
        {
            float3 ExitPos = EntryPos + InsideDir * (2.0e-3 + TOut);
            float3 OutNrm  = normalize(ExitPos - Center);
            float3 ExitDir = refract(InsideDir, -OutNrm, IorChannel);
            if (dot(ExitDir, ExitDir) < 1.0e-5)
            {
                ExitDir = reflect(InsideDir, -OutNrm);
            }
            BounceOrig = ExitPos + OutNrm * 3.0e-3;
            BounceDir  = normalize(ExitDir);
        }
        else
        {
            BounceOrig = EntryPos + InsideDir * (Radius * 2.1);
            BounceDir  = InsideDir;
        }
    }
    return ShadeSecondaryBounce(BounceOrig, BounceDir);
}

float3 ApplyAcesFilmicGpu(float3 Hdr)
{
    float3 V = max(float3(0.0), Hdr * 0.85);
    return saturate((V * (2.51 * V + 0.03)) / (V * (2.43 * V + 0.59) + 0.14));
}

uint PackLinearToSrgb8Rgba(float3 LinearColor, uint2 PixelXY)
{
    float3 FiniteCol = select(isnan(LinearColor) | isinf(LinearColor), float3(0.0), LinearColor);
    float3 C         = saturate(FiniteCol);
    float3 Srgb      = select(C <= float3(0.0031308), C * 12.92, 1.055 * pow(max(C, float3(1.0e-6)), float3(1.0 / 2.4)) - 0.055);
    float  IgnA = frac(52.9829189 * frac(float(PixelXY.x) * 0.06711056 + float(PixelXY.y) * 0.00583715));
    float  IgnB = frac(52.9829189 * frac(float(PixelXY.x + 17u) * 0.06711056 + float(PixelXY.y + 31u) * 0.00583715));
    float  Tpdf = (IgnA + IgnB - 1.0) / 255.0;
    uint3  Byte = uint3(clamp(round((Srgb + float3(Tpdf)) * 255.0), float3(0.0), float3(255.0)));
    return Byte.x | (Byte.y << 8u) | (Byte.z << 16u) | (255u << 24u);
}

float3 EvaluateVolumetricGodRaysGpu(
    float3 RayOrigin,
    float3 RayDir,
    float  MaxDistM,
    float3 SunDir,
    float3 SolarIrrad,
    float2 PixelCoord)
{
    VolumetricFogUniformBlock Fog = WorldUniforms.FogParams;
    if (Fog.GodRayParams0.x < 0.5 || Fog.GodRayParams0.y <= 1.0e-4 || SunDir.z <= 0.005 || MaxDistM <= 0.25)
    {
        return float3(0.0, 0.0, 0.0);
    }

    float  RangeM  = min(max(0.25, MaxDistM), max(10.0, Fog.GodRayParams1.x));
    uint   Steps   = clamp(uint(Fog.GodRayParams1.y + 0.5), 8u, 48u);
    float  Dt      = (RangeM - 0.15) / float(Steps);
    float  Ign     = frac(52.9829189 * frac(dot(PixelCoord, float2(0.06711056, 0.00583715))));
    float  Jitter  = 0.5 + 0.72 * (Ign - 0.5);

    float  CosTheta   = dot(RayDir, SunDir);
    float  G          = clamp(Fog.GodRayParams0.z, 0.0, 0.94);
    float  G2         = G * G;
    float  Den        = max(1.0e-3, 1.0 + G2 - 2.0 * G * CosTheta);
    float  MiePhase   = (1.0 - G2) / (4.0 * PiConst * pow(Den, 1.5));
    float  Phase      = 0.85 * MiePhase + 0.15 * (1.0 / (4.0 * PiConst));
    float  DecayScale = max(15.0, Fog.GodRayParams1.x * (1.35 - 0.85 * saturate(Fog.GodRayTint.w)));

    float3 Accum    = float3(0.0, 0.0, 0.0);
    float  CamTrans = 1.0;
    bool   DoClouds = (Fog.GodRayParams1.z > 0.5);
    bool   DoFog    = (Fog.GodRayParams1.w > 0.5);
    float  BaseDen  = max(0.0, Fog.GodRayParams0.w);

    for (uint S = 0u; S < Steps; ++S)
    {
        float  T = 0.15 + (float(S) + Jitter) * Dt;
        float3 P = RayOrigin + RayDir * T;

        if (TraceSceneAnyHit(P + SunDir * 0.02, SunDir, 0.02, 140.0))
        {
            continue;
        }
        float SceneVis = 1.0;
        if (WorldUniforms.FluidParams.HeroBoxMinFade.w > 0.5)
        {
            SceneVis *= EvaluateFluidShadowTransmittanceBufferGpu(
                WorldUniforms.FluidParams, FluidHeroThermoTexels, FluidWorldThermoTexels, P, SunDir, 32.0);
        }
        float RawCloudVis = DoClouds ? EvaluateSolarCloudTransmittanceGpu(P, SunDir) : 1.0;
        float CloudVis    = DoClouds ? pow(saturate((RawCloudVis - 0.12) / 0.88), 2.35) : 1.0;

        float MediumSigma = 0.0048 * BaseDen;
        if (DoFog)
        {
            float FogRho = EvaluateFogMediumDensityAtPointGpu(Fog, P);
            MediumSigma += FogRho * 0.68 * max(0.1, BaseDen);
        }

        float DecayW = exp(-T / DecayScale);
        float StepWt = CamTrans * SceneVis * CloudVis * MediumSigma * Phase * Fog.GodRayParams0.y * DecayW * Dt * 0.72;
        Accum       += SolarIrrad * Fog.GodRayTint.xyz * StepWt;
        CamTrans    *= exp(-MediumSigma * Dt * 0.35);
        if (CamTrans < 0.02) break;
    }

    return Accum;
}

//------------------------------------------------------------------------------------------------------------------------
// GLOBAL DISTANCE FIELD (GDF) DEBUG VISUALIZATION HELPERS (Zero BVH4 Triangles for Mode 5/6!)
//------------------------------------------------------------------------------------------------------------------------

bool IntersectSdfVolumeBounds(float3 RayO, float3 RayD, out float OutTMin, out float OutTMax)
{
    uint3 Dims = WorldUniforms.SdfDimsLightCount.xyz;
    float Spacing = max(1.0e-4, WorldUniforms.SdfMinSpacing.w);
    float3 BoxMin = WorldUniforms.SdfMinSpacing.xyz;
    float3 BoxMax = BoxMin + float3(Dims - uint3(1u, 1u, 1u)) * Spacing;
    float3 SafeD  = select(abs(RayD) > float3(1.0e-8), RayD, float3(1.0e-8));
    float3 InvD   = 1.0 / SafeD;
    float3 T0     = (BoxMin - RayO) * InvD;
    float3 T1     = (BoxMax - RayO) * InvD;
    float3 TSm    = min(T0, T1);
    float3 TLg    = max(T0, T1);
    OutTMin = max(0.05, max(TSm.x, max(TSm.y, TSm.z)));
    OutTMax = min(60.0, min(TLg.x, min(TLg.y, TLg.z)));
    return OutTMax > OutTMin;
}

float3 EvaluateGdfIsoSurfaceDebug(float3 RayO, float3 RayD)
{
    float TMin, TMax;
    if (!IntersectSdfVolumeBounds(RayO, RayD, TMin, TMax))
    {
        return float3(0.04, 0.05, 0.08);
    }

    float  Spacing = max(0.03, WorldUniforms.SdfMinSpacing.w);
    float  Eps     = max(0.008, Spacing * 0.14);
    float  T       = TMin + 0.01;
    bool   HitIso  = false;
    float3 HitP    = float3(0.0);
    for (uint Step = 0u; Step < 64u && T < TMax; ++Step)
    {
        float3 P = RayO + RayD * T;
        float  D = SampleSdfGrid(P);
        if (D < Eps)
        {
            HitIso = true;
            HitP   = P;
            break;
        }
        T += max(Eps * 0.65, D * 0.85);
    }
    if (HitIso)
    {
        float3 N      = EvaluateSdfNormal(HitP);
        float  Rim    = 0.85 + 0.15 * saturate(dot(N, -RayD));
        float  Stripe = 0.90 + 0.10 * cos(HitP.z * 24.0);
        return saturate((N * 0.5 + 0.5) * Rim * Stripe);
    }
    return float3(0.04, 0.05, 0.08);
}

float3 ColorMapSignedDistance(float D)
{
    float Isoline = abs(frac(D / 0.16 + 0.5) - 0.5) * 0.16;
    float LineMask = 1.0 - smoothstep(0.008, 0.022, Isoline);
    float ZeroMask = 1.0 - smoothstep(0.014, 0.032, abs(D));
    float3 BaseCol = (D < 0.0)
        ? lerp(float3(0.96, 0.36, 0.10), float3(0.45, 0.06, 0.02), saturate(-D * 2.2))
        : lerp(float3(0.14, 0.68, 0.96), float3(0.03, 0.12, 0.34), saturate(D * 1.1));
    BaseCol *= (1.0 - 0.38 * LineMask);
    return lerp(BaseCol, float3(1.0, 0.98, 0.92), ZeroMask);
}

float3 EvaluateGdfSliceDebug(float3 RayO, float3 RayD, RayHitReading PrimaryHit)
{
    float PlaneZ = -0.15;
    if (abs(RayD.z) > 1.0e-4)
    {
        float TPlane = (PlaneZ - RayO.z) / RayD.z;
        float TMin, TMax;
        if (TPlane > 0.05 && IntersectSdfVolumeBounds(RayO, RayD, TMin, TMax) && TPlane >= TMin && TPlane <= TMax)
        {
            float3 SlicePos = RayO + RayD * TPlane;
            float  D        = SampleSdfGrid(SlicePos);
            return ColorMapSignedDistance(D);
        }
    }
    if (PrimaryHit.Hit)
    {
        float D = SampleSdfGrid(PrimaryHit.Position + PrimaryHit.ShadingNormal * 0.05);
        return ColorMapSignedDistance(D);
    }
    return float3(0.03, 0.04, 0.06);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CloudComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint CW = uint(WorldUniforms.CloudParams.CloudPassControl.x + 0.5);
    uint CH = uint(WorldUniforms.CloudParams.CloudPassControl.y + 0.5);
    if (CW == 0u || CH == 0u || DispatchThreadID.x >= CW || DispatchThreadID.y >= CH)
    {
        return;
    }

    uint PIdx = DispatchThreadID.y * CW + DispatchThreadID.x;
    if (WorldUniforms.AtmosphereMaskPad.x == 0u || WorldUniforms.CloudParams.SunAxisToggle.w < 0.5)
    {
        CloudRadiometricBuffer[PIdx] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float2 PixelUv = (float2(DispatchThreadID.xy) + float2(0.5, 0.5)) / float2(float(CW), float(CH));
    float  NdcX    = PixelUv.x * 2.0 - 1.0;
    float  NdcY    = 1.0 - PixelUv.y * 2.0;
    float3 RayDir  = normalize(
        WorldUniforms.CameraForwardHeight.xyz +
        WorldUniforms.CameraRightTanY.xyz * (NdcX * WorldUniforms.CameraOriginTanX.w) +
        WorldUniforms.CameraUpWidth.xyz   * (NdcY * WorldUniforms.CameraRightTanY.w));

    float  CloudTrans = 1.0;
    float3 CloudRad   = EvaluateVolumetricCloudsGpu(
        WorldUniforms.CloudParams, RayDir, float2(DispatchThreadID.xy), CloudTrans, false);
    CloudRadiometricBuffer[PIdx] = float4(CloudRad, CloudTrans);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputePathTraceEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Width  = uint(WorldUniforms.CameraUpWidth.w + 0.5);
    uint Height = uint(WorldUniforms.CameraForwardHeight.w + 0.5);
    if (DispatchThreadID.x >= Width || DispatchThreadID.y >= Height)
    {
        return;
    }

    uint   PixelIdx     = DispatchThreadID.y * Width + DispatchThreadID.x;
    uint   SampleIdx    = WorldUniforms.EngineShadowReflSample.w;
    float3 CamOrigin    = WorldUniforms.CameraOriginTanX.xyz;
    uint   SamplesToRun = clamp(uint(WorldUniforms.FineCascadeParams.w + 0.5), 1u, 4u);
    uint   PixelHash    = 0u; // Shared Owen-scrambled Sobol (0,2) low-discrepancy net (zero spatial scramble noise)

    float4 PrevAcc = (SampleIdx == 0u) ? float4(0.0, 0.0, 0.0, 0.0) : AccumulationHdrBuffer[PixelIdx];
    float3 AccSum  = PrevAcc.xyz * PrevAcc.w;
    float  Count   = PrevAcc.w;
    uint   BaseIdx = (SampleIdx == 0u) ? 0u : uint(PrevAcc.w + 0.5);
    uint   TargetS = SamplesToRun;

    RayHitReading LastPrimaryHit;
    LastPrimaryHit.Hit = false;
    LastPrimaryHit.Distance = 120.0;
    LastPrimaryHit.Position = float3(0.0);
    LastPrimaryHit.ShadingNormal = float3(0.0, 0.0, 1.0);
    LastPrimaryHit.GeomNormal = float3(0.0, 0.0, 1.0);
    LastPrimaryHit.MaterialSlot = 0u;
    int    LastGlassIdx    = -1;
    float  LastGlassT      = 120.0;
    float3 LastDemodAlbedo = float3(1.0);
    float  LastSpecRough   = 0.0;
    uint   MissCount       = 0u;
    float3 LastMissDir     = float3(0.0, 0.0, 1.0);

    for (uint S = 0u; S < TargetS; ++S)
    {
        uint   CurSample = BaseIdx + S;
        float2 JitterPx  = Sobol02Sample(CurSample, PixelHash, 0u);
        float2 PixelUv   = (float2(DispatchThreadID.xy) + JitterPx) / float2(float(Width), float(Height));
        float  NdcX      = PixelUv.x * 2.0 - 1.0;
        float  NdcY      = 1.0 - PixelUv.y * 2.0;
        float3 RayDir    = normalize(
            WorldUniforms.CameraForwardHeight.xyz +
            WorldUniforms.CameraRightTanY.xyz * (NdcX * WorldUniforms.CameraOriginTanX.w) +
            WorldUniforms.CameraUpWidth.xyz   * (NdcY * WorldUniforms.CameraRightTanY.w));

        RayHitReading PtHit;
        int    PtGlassIdx   = -1;
        float  PtGlassT     = 120.0;
        float3 PtDemodAlb   = float3(1.0);
        float  PtSpecRough  = 0.0;
        float3 SampleHdr = TraceDeterministicPathSample(
            CamOrigin, RayDir, DispatchThreadID.xy, PixelHash, CurSample,
            PtHit, PtGlassIdx, PtGlassT, PtDemodAlb, PtSpecRough);
        SampleHdr = select(isnan(SampleHdr) | isinf(SampleHdr), float3(0.0), max(float3(0.0), SampleHdr));
        if (!PtHit.Hit && PtGlassIdx < 0)
        {
            ++MissCount;
            LastMissDir = RayDir;
        }
        AccSum += SampleHdr;
        Count  += 1.0;
        if (S == 0u)
        {
            LastPrimaryHit  = PtHit;
            LastGlassIdx    = PtGlassIdx;
            LastGlassT      = PtGlassT;
            LastDemodAlbedo = PtDemodAlb;
            LastSpecRough   = PtSpecRough;
            if (PtGlassIdx >= 0 && SamplesToRun >= 2u && SampleIdx == 0u)
            {
                TargetS = min(4u, SamplesToRun * 2u);
            }
        }
    }

    if (MissCount > 0u)
    {
        float3 SkyHdr = EvaluateBackgroundSkyCloudsGpu(LastMissDir, float2(DispatchThreadID.xy));
        SkyHdr = select(isnan(SkyHdr) | isinf(SkyHdr), float3(0.0), max(float3(0.0), SkyHdr));
        AccSum += SkyHdr * float(MissCount);
    }

    float3 MeanHdr = AccSum / max(1.0, Count);
    AccumulationHdrBuffer[PixelIdx] = float4(MeanHdr, Count);

    float PtCloudDist = (LastGlassIdx >= 0) ? LastGlassT : (LastPrimaryHit.Hit ? LastPrimaryHit.Distance : 60000.0);
    float PtFogDist   = (LastGlassIdx >= 0) ? LastGlassT : (LastPrimaryHit.Hit ? LastPrimaryHit.Distance : 3500.0);
    bool  ValidSurf   = LastPrimaryHit.Hit && (LastGlassIdx < 0);
    float3 SafeAlb    = ValidSurf ? max(float3(0.08), LastDemodAlbedo) : float3(1.0);
    float3 DemodMean  = ValidSurf ? (MeanHdr / SafeAlb) : MeanHdr;

    GBufPosMask[PixelIdx]    = float4(ValidSurf ? LastPrimaryHit.Position : float3(0.0), ValidSurf ? (1.0 + LastSpecRough) : 0.0);
    GBufNormAo[PixelIdx]     = float4(ValidSurf ? LastPrimaryHit.ShadingNormal : float3(0.0, 0.0, 1.0), 1.0);
    GBufDirectBase[PixelIdx] = float4(MeanHdr, PtCloudDist);
    GBufSunContrib[PixelIdx] = float4(0.0, 0.0, 0.0, PtFogDist);
    GBufAlbedo[PixelIdx]     = float4(SafeAlb, LastSpecRough);
    PingShadowAo[PixelIdx]   = float4(1.0, 1.0, 0.0, Count);
    PingGiHdr[PixelIdx]      = float4(DemodMean, Count);
    WeightedCurrentBuffer[PixelIdx * 2u + 0u] = float4(0.0);
    WeightedCurrentBuffer[PixelIdx * 2u + 1u] = float4(0.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Width  = uint(WorldUniforms.CameraUpWidth.w + 0.5);
    uint Height = uint(WorldUniforms.CameraForwardHeight.w + 0.5);
    if (DispatchThreadID.x >= Width || DispatchThreadID.y >= Height)
    {
        return;
    }

    uint   PixelIdx   = DispatchThreadID.y * Width + DispatchThreadID.x;
    uint   EngineMode = WorldUniforms.EngineShadowReflSample.x;
    uint   DebugMode  = uint(WorldUniforms.LatticeMaxDebugMode.w + 0.5);
    float3 CamOrigin  = WorldUniforms.CameraOriginTanX.xyz;

    float2 PixelUv = (float2(DispatchThreadID.xy) + float2(0.5, 0.5)) / float2(float(Width), float(Height));
    float  NdcX    = PixelUv.x * 2.0 - 1.0;
    float  NdcY    = 1.0 - PixelUv.y * 2.0;
    float3 RayDir  = normalize(
        WorldUniforms.CameraForwardHeight.xyz +
        WorldUniforms.CameraRightTanY.xyz * (NdcX * WorldUniforms.CameraOriginTanX.w) +
        WorldUniforms.CameraUpWidth.xyz   * (NdcY * WorldUniforms.CameraRightTanY.w));

    RayHitReading PrimaryHit = TraceSceneClosest(CamOrigin, RayDir, 0.08, 120.0);

    float TriHitT      = PrimaryHit.Hit ? PrimaryHit.Distance : 120.0;
    float BestGlassT   = TriHitT;
    int   BestGlassIdx = -1;
    bool  AllowGlass   = (EngineMode != 0u) && (DebugMode != 5u && DebugMode != 6u && DebugMode != 7u && DebugMode != 8u);
    if (AllowGlass)
    {
        BestGlassIdx = FindClosestGlassSphere(CamOrigin, RayDir, 0.08, TriHitT, BestGlassT);
    }

    float HitDistForClouds = (BestGlassIdx >= 0) ? BestGlassT : (PrimaryHit.Hit ? PrimaryHit.Distance : 60000.0);
    float HitDistForFog    = (BestGlassIdx >= 0) ? BestGlassT : (PrimaryHit.Hit ? PrimaryHit.Distance : 3500.0);

    bool UsesShadingDebug = (DebugMode == 8u || DebugMode == 12u || DebugMode == 13u || DebugMode == 14u || DebugMode == 15u);
    if (DebugMode != 0u && !UsesShadingDebug)
    {
        float3 DbgHdr = float3(0.0);
        if (DebugMode == 5u)
        {
            DbgHdr = EvaluateGdfIsoSurfaceDebug(CamOrigin, RayDir);
        }
        else if (DebugMode == 6u)
        {
            DbgHdr = EvaluateGdfSliceDebug(CamOrigin, RayDir, PrimaryHit);
        }
        else if (BestGlassIdx >= 0)
        {
            GpuGlassSphere Glass = WorldUniforms.GlassSpheres[BestGlassIdx];
            float3 HitPos = CamOrigin + RayDir * BestGlassT;
            float3 HitNrm = normalize(HitPos - Glass.CenterRadiusPacked.xyz);
            if (DebugMode == 1u)       DbgHdr = float3(pow(saturate(1.0 - BestGlassT / 11.0), 1.8));
            else if (DebugMode == 2u)  DbgHdr = float3(0.95, 0.98, 1.00);
            else if (DebugMode == 3u)  DbgHdr = HitNrm * 0.5 + 0.5;
            else if (DebugMode == 4u)  DbgHdr = float3(clamp(Glass.OpticsSlotPacked.w, 0.025, 1.0));
            else if (DebugMode == 7u)
            {
                float  Spacing = max(0.04, WorldUniforms.SdfMinSpacing.w);
                float3 VRad    = SampleSdfRadiosityTrilinear(HitPos + HitNrm * (Spacing * 0.65));
                DbgHdr         = ApplyAcesFilmicGpu(VRad * 1.35);
            }
            else if (DebugMode == 9u)  DbgHdr = float3(0.0);
            else if (DebugMode == 10u) DbgHdr = float3(0.28, 0.85, 0.96);
            else if (DebugMode == 11u) DbgHdr = HitNrm * 0.5 + 0.5;
        }
        else if (PrimaryHit.Hit)
        {
            uint MatCount = max(1u, WorldUniforms.LatticeDimsMaterialCount.w);
            GpuWorldMaterial80 Mat = MaterialEntries[min(PrimaryHit.MaterialSlot, MatCount - 1u)];
            if (DebugMode == 1u)       DbgHdr = float3(pow(saturate(1.0 - PrimaryHit.Distance / 11.0), 1.8));
            else if (DebugMode == 2u)  DbgHdr = Mat.DiffuseAlbedoRoughness.xyz;
            else if (DebugMode == 3u)  DbgHdr = PrimaryHit.ShadingNormal * 0.5 + 0.5;
            else if (DebugMode == 4u)  DbgHdr = float3(Mat.SpecParams.x);
            else if (DebugMode == 7u)
            {
                float  Spacing = max(0.04, WorldUniforms.SdfMinSpacing.w);
                float3 VRad    = SampleSdfRadiosityTrilinear(PrimaryHit.Position + PrimaryHit.ShadingNormal * (Spacing * 0.65));
                DbgHdr         = ApplyAcesFilmicGpu(VRad * 1.35);
            }
            else if (DebugMode == 9u)  DbgHdr = float3(saturate(Mat.SpecParams.y));
            else if (DebugMode == 10u)
            {
                uint SId = PrimaryHit.MaterialSlot + 1u;
                float H1 = frac(float(SId) * 0.3819660113);
                float H2 = frac(float(SId) * 0.6180339887 + 0.27);
                float H3 = frac(float(SId) * 0.7548776662 + 0.53);
                DbgHdr = float3(0.18 + 0.78 * H1, 0.18 + 0.78 * H2, 0.18 + 0.78 * H3);
            }
            else if (DebugMode == 11u) DbgHdr = PrimaryHit.GeomNormal * 0.5 + 0.5;
        }
        GBufPosMask[PixelIdx]    = float4(0.0, 0.0, 0.0, 0.0);
        GBufNormAo[PixelIdx]     = float4(0.0, 0.0, 1.0, 1.0);
        GBufDirectBase[PixelIdx] = float4(DbgHdr, HitDistForClouds);
        GBufSunContrib[PixelIdx] = float4(0.0, 0.0, 0.0, HitDistForFog);
        GBufAlbedo[PixelIdx]     = float4(0.0, 0.0, 0.0, 1.0);
        PingShadowAo[PixelIdx]   = float4(1.0, 1.0, 0.0, 1.0);
        PingGiHdr[PixelIdx]      = float4(0.0, 0.0, 0.0, 1.0);
        WeightedCurrentBuffer[PixelIdx * 2u + 0u] = float4(0.0);
        WeightedCurrentBuffer[PixelIdx * 2u + 1u] = float4(0.0);
        return;
    }

    if (BestGlassIdx >= 0 && (DebugMode == 0u || DebugMode == 15u))
    {
        GpuGlassSphere Glass = WorldUniforms.GlassSpheres[BestGlassIdx];
        float3 Center        = Glass.CenterRadiusPacked.xyz;
        float  Radius        = Glass.CenterRadiusPacked.w;
        float3 HitPos        = CamOrigin + RayDir * BestGlassT;
        float3 HitNrm        = normalize(HitPos - Center);
        float3 ViewDir       = -RayDir;
        float  CrownIor      = max(1.05, Glass.OpticsSlotPacked.x);
        float  Dispersion    = clamp(0.45 / max(10.0, Glass.OpticsSlotPacked.y), 0.004, 0.028);

        float  CosI    = saturate(dot(HitNrm, ViewDir));
        float  F0      = pow((CrownIor - 1.0) / (CrownIor + 1.0), 2.0);
        float  Fresnel = IsReflectionsEnabled() ? (F0 + (1.0 - F0) * pow(1.0 - CosI, 5.0)) : 0.0;

        float3 RefrRgb = float3(0.0);
        for (int Ch = 0; Ch < 3; ++Ch)
        {
            float  ChIor = (Ch == 0) ? (CrownIor - Dispersion) : ((Ch == 1) ? CrownIor : (CrownIor + Dispersion * 1.15));
            float3 ChRad = TraceThroughGlassSphere(HitPos, HitNrm, RayDir, Center, Radius, ChIor);
            if (Ch == 0) RefrRgb.r = ChRad.r;
            else if (Ch == 1) RefrRgb.g = ChRad.g;
            else RefrRgb.b = ChRad.b;
        }
        RefrRgb *= float3(0.95, 0.98, 1.00);
        float3 ReflRgb = IsReflectionsEnabled() ? ShadeSecondaryBounce(HitPos + HitNrm * 2.5e-3, reflect(RayDir, HitNrm)) : float3(0.0);

        float3 SunDir = WorldUniforms.SunDirConeTan.xyz;
        float3 HSun   = normalize(ViewDir + SunDir);
        float  SpecGl = IsReflectionsEnabled() ? (pow(max(0.0, dot(HitNrm, HSun)), 380.0) * 6.5) : 0.0;
        if (SpecGl > 0.0 && TraceSceneAnyHit(HitPos + HitNrm * 3.0e-3, SunDir, 2.0e-3, 60.0))
        {
            SpecGl = 0.0;
        }
        float3 GlassHdr = (DebugMode == 15u)
            ? ApplyAcesFilmicGpu(ReflRgb * Fresnel + WorldUniforms.SolarIrradianceTier.xyz * SpecGl)
            : (lerp(RefrRgb, ReflRgb, Fresnel) + WorldUniforms.SolarIrradianceTier.xyz * SpecGl);
        GBufPosMask[PixelIdx]    = float4(0.0, 0.0, 0.0, 0.0);
        GBufNormAo[PixelIdx]     = float4(HitNrm, 1.0);
        GBufDirectBase[PixelIdx] = float4(GlassHdr, HitDistForClouds);
        GBufSunContrib[PixelIdx] = float4(0.0, 0.0, 0.0, HitDistForFog);
        GBufAlbedo[PixelIdx]     = float4(0.0, 0.0, 0.0, 1.0);
        PingShadowAo[PixelIdx]   = float4(1.0, 1.0, 0.0, 1.0);
        PingGiHdr[PixelIdx]      = float4(0.0, 0.0, 0.0, 1.0);
        WeightedCurrentBuffer[PixelIdx * 2u + 0u] = float4(0.0);
        WeightedCurrentBuffer[PixelIdx * 2u + 1u] = float4(0.0);
        return;
    }

    if (!PrimaryHit.Hit)
    {
        float3 SkyHdr = UsesShadingDebug ? float3(0.03, 0.04, 0.07) : EvaluateBackgroundSkyCloudsGpu(RayDir, float2(DispatchThreadID.xy));
        GBufPosMask[PixelIdx]    = float4(0.0, 0.0, 0.0, 0.0);
        GBufNormAo[PixelIdx]     = float4(0.0, 0.0, 1.0, 1.0);
        GBufDirectBase[PixelIdx] = float4(SkyHdr, HitDistForClouds);
        GBufSunContrib[PixelIdx] = float4(0.0, 0.0, 0.0, HitDistForFog);
        GBufAlbedo[PixelIdx]     = float4(0.0, 0.0, 0.0, 1.0);
        PingShadowAo[PixelIdx]   = float4(1.0, 1.0, 0.0, 1.0);
        PingGiHdr[PixelIdx]      = float4(0.0, 0.0, 0.0, 1.0);
        WeightedCurrentBuffer[PixelIdx * 2u + 0u] = float4(0.0);
        WeightedCurrentBuffer[PixelIdx * 2u + 1u] = float4(0.0);
        return;
    }

    if (EngineMode == 0u && DebugMode == 0u)
    {
        float3 MatCapHdr = EvaluateMatCapSurface(PrimaryHit);
        GBufPosMask[PixelIdx]    = float4(0.0, 0.0, 0.0, 0.0);
        GBufNormAo[PixelIdx]     = float4(PrimaryHit.ShadingNormal, 1.0);
        GBufDirectBase[PixelIdx] = float4(MatCapHdr, HitDistForClouds);
        GBufSunContrib[PixelIdx] = float4(0.0, 0.0, 0.0, HitDistForFog);
        GBufAlbedo[PixelIdx]     = float4(0.0, 0.0, 0.0, 1.0);
        PingShadowAo[PixelIdx]   = float4(1.0, 1.0, 0.0, 1.0);
        PingGiHdr[PixelIdx]      = float4(0.0, 0.0, 0.0, 1.0);
        WeightedCurrentBuffer[PixelIdx * 2u + 0u] = float4(0.0);
        WeightedCurrentBuffer[PixelIdx * 2u + 1u] = float4(0.0);
        return;
    }

    float3 DirectBaseHdr = float3(0.0);
    float3 SpecReflHdr   = float3(0.0);
    float3 UnoccludedSunHdr = float3(0.0);
    float3 DemodGiOverPi = float3(0.0);
    float3 DiffuseAlbedoMatte = float3(0.0);
    float  SunVis = 1.0, BlockerDist = 0.0, DiffAo = 1.0, SpecRough = 0.5, SpecAo = 1.0;
    ShadePrimarySurfaceDemod(
        PrimaryHit,
        -RayDir,
        DispatchThreadID.xy,
        DirectBaseHdr,
        SpecReflHdr,
        UnoccludedSunHdr,
        DemodGiOverPi,
        DiffuseAlbedoMatte,
        SunVis,
        BlockerDist,
        DiffAo,
        SpecRough,
        SpecAo);

    if (DebugMode == 8u)
    {
        AccumulationHdrBuffer[PixelIdx] = float4(0.0, 0.0, 0.0, 0.5);
        GBufPosMask[PixelIdx]           = float4(PrimaryHit.Position, 1.5);
        GBufNormAo[PixelIdx]            = float4(PrimaryHit.ShadingNormal, 1.0);
        GBufDirectBase[PixelIdx]        = float4(0.0, 0.0, 0.0, HitDistForClouds);
        GBufSunContrib[PixelIdx]        = float4(0.0, 0.0, 0.0, HitDistForFog);
        GBufAlbedo[PixelIdx]            = float4(1.0, 1.0, 1.0, 1.0);
        PingShadowAo[PixelIdx]          = float4(1.0, 1.0, 0.0, 1.0);
        PingGiHdr[PixelIdx]             = float4(DemodGiOverPi, 1.0);
        return;
    }

    AccumulationHdrBuffer[PixelIdx] = float4(SpecReflHdr, SpecRough);
    GBufPosMask[PixelIdx]           = float4(PrimaryHit.Position, 1.0 + SpecRough);
    GBufNormAo[PixelIdx]      = float4(PrimaryHit.ShadingNormal, DiffAo);
    GBufDirectBase[PixelIdx]  = float4(DirectBaseHdr, HitDistForClouds);
    GBufSunContrib[PixelIdx]  = float4(UnoccludedSunHdr, HitDistForFog);
    GBufAlbedo[PixelIdx]      = float4(DiffuseAlbedoMatte, SpecAo);
    PingShadowAo[PixelIdx]    = float4(SunVis, DiffAo, BlockerDist, 1.0);
    PingGiHdr[PixelIdx]       = float4(DemodGiOverPi, 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void DenoiseCompositeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Width  = uint(WorldUniforms.CameraUpWidth.w + 0.5);
    uint Height = uint(WorldUniforms.CameraForwardHeight.w + 0.5);
    if (DispatchThreadID.x >= Width || DispatchThreadID.y >= Height)
    {
        return;
    }

    uint   PixelIdx         = DispatchThreadID.y * Width + DispatchThreadID.x;
    uint   EngineMode       = WorldUniforms.EngineShadowReflSample.x;
    uint   DebugMode        = uint(WorldUniforms.LatticeMaxDebugMode.w + 0.5);
    int    StridePx         = max(1, int(WorldUniforms.DenoiseStepParams.x));
    bool   IsFinalComposite = (WorldUniforms.DenoiseStepParams.y & 1u) != 0u;
    bool   SpatialDenoise   = (WorldUniforms.DenoiseStepParams.y & 2u) != 0u;

    float4 CenterPos  = GBufPosMask[PixelIdx];
    float4 CenterNorm = GBufNormAo[PixelIdx];
    float4 InShadAo   = PingShadowAo[PixelIdx];
    float4 InGi       = PingGiHdr[PixelIdx];
    float4 CenterAlb  = GBufAlbedo[PixelIdx];

    float  OutSunVis  = InShadAo.x;
    float  OutDiffAo  = InShadAo.y;
    float  OutBlocker = InShadAo.z;
    float3 OutGi      = InGi.xyz;

    float  PtRoughGuard = (EngineMode == 4u) ? smoothstep(0.08, 0.30, CenterAlb.w) : 1.0;
    bool   RunSpatial   = SpatialDenoise && (CenterPos.w > 0.0) &&
                          ((EngineMode != 4u) || (IsPtDenoiseEnabled() && PtRoughGuard > 0.01));

    if (RunSpatial)
    {
        float3 CenterN  = normalize(CenterNorm.xyz);
        float3 CamOrig  = WorldUniforms.CameraOriginTanX.xyz;
        float  CamDist  = max(0.25, length(CenterPos.xyz - CamOrig));
        float  TanSun   = max(0.004, WorldUniforms.SunDirConeTan.w);
        float  EdgeBlock = (OutSunVis > 0.005 && OutSunVis < 0.995) ? max(OutBlocker, 0.30) : OutBlocker;
        float  Penumbra = EdgeBlock * TanSun;
        float  SigmaSh  = clamp(1.10 + (Penumbra / CamDist) * float(Height) * 1.65, 1.10, 9.0);
        float  Inv2SigS = 1.0 / (2.0 * SigmaSh * SigmaSh);
        float  SigmaGi  = max(1.0, float(StridePx) * 1.15);
        float  Inv2SigG = 1.0 / (2.0 * SigmaGi * SigmaGi);

        float  SumVis   = 0.0;
        float  SumWVis  = 0.0;
        float  SumAo    = 0.0;
        float3 SumGi    = float3(0.0);
        float  SumWGi   = 0.0;
        float  MaxBlock = OutBlocker;
        float  CenterLum = dot(InGi.xyz, float3(0.2126, 0.7152, 0.0722));

        for (int KY = -1; KY <= 1; ++KY)
        {
            for (int KX = -1; KX <= 1; ++KX)
            {
                int2 SCoord = clamp(int2(DispatchThreadID.xy) + int2(KX, KY) * StridePx,
                                    int2(0, 0),
                                    int2(int(Width) - 1, int(Height) - 1));
                uint SIdx   = uint(SCoord.y) * Width + uint(SCoord.x);
                float4 NPos = GBufPosMask[SIdx];
                if (NPos.w <= 0.0) continue;

                float3 NNrm   = normalize(GBufNormAo[SIdx].xyz);
                float4 NShAo  = PingShadowAo[SIdx];
                float4 NGiRaw = PingGiHdr[SIdx];
                float3 NGi    = select(isnan(NGiRaw.xyz) | isinf(NGiRaw.xyz), float3(0.0), max(float3(0.0), NGiRaw.xyz));

                float DistSq    = float((KX * StridePx) * (KX * StridePx) + (KY * StridePx) * (KY * StridePx));
                float PlaneDist = abs(dot(CenterN, NPos.xyz - CenterPos.xyz));
                float WPlane    = exp(-PlaneDist * 65.0);
                float WNrm      = pow(saturate(dot(CenterN, NNrm)), 24.0);
                float WGeom     = WPlane * WNrm;

                if (EngineMode == 3u || EngineMode == 4u)
                {
                    float4 NAlb    = GBufAlbedo[SIdx];
                    float3 AlbDiff = NAlb.xyz - CenterAlb.xyz;
                    float  WAlb    = exp(-dot(AlbDiff, AlbDiff) * 20.0);
                    float  WRough  = exp(-abs(NAlb.w - CenterAlb.w) * 12.0);
                    float  NLum    = dot(NGi, float3(0.2126, 0.7152, 0.0722));
                    float  LumDiff = abs(CenterLum - NLum);
                    float  WLum    = exp(-(LumDiff * LumDiff) / max(0.16, 1.5 * CenterLum + 0.12));
                    WGeom *= WAlb * WRough * WLum;
                }

                float WVis = exp(-DistSq * Inv2SigS) * WGeom;
                SumVis  += NShAo.x * WVis;
                SumWVis += WVis;

                float WGi = exp(-DistSq * Inv2SigG) * WGeom;
                SumAo  += NShAo.y * WGi;
                SumGi  += NGi * WGi;
                SumWGi += WGi;

                if (PlaneDist < 0.03 && WNrm > 0.5)
                {
                    MaxBlock = max(MaxBlock, NShAo.z);
                }
            }
        }

        OutSunVis  = (SumWVis > 1.0e-6) ? (SumVis / SumWVis) : InShadAo.x;
        OutDiffAo  = (SumWGi > 1.0e-6)  ? (SumAo / SumWGi)  : InShadAo.y;
        OutGi      = (SumWGi > 1.0e-6)  ? (SumGi / SumWGi)  : InGi.xyz;
        OutBlocker = MaxBlock;
    }

    if (!IsFinalComposite)
    {
        PongShadowAo[PixelIdx] = float4(OutSunVis, OutDiffAo, OutBlocker, InShadAo.w);
        PongGiHdr[PixelIdx]    = float4(OutGi, InGi.w);
        return;
    }

    float4 DirectBase = GBufDirectBase[PixelIdx];
    float4 SunContrib = GBufSunContrib[PixelIdx];
    float4 AlbedoPack = CenterAlb;

    float3 FinalHdr = DirectBase.xyz;
    if (EngineMode == 4u && DebugMode == 0u)
    {
        float3 ReModIn  = (CenterPos.w > 0.0) ? (InGi.xyz * AlbedoPack.xyz) : InGi.xyz;
        float3 ReModOut = (CenterPos.w > 0.0) ? (OutGi    * AlbedoPack.xyz) : OutGi;
        if (RunSpatial)
        {
            float BlendW = saturate(1.0 - InShadAo.w / 12.0) * 0.78 * PtRoughGuard;
            FinalHdr = lerp(ReModIn, ReModOut, BlendW);
        }
        else
        {
            FinalHdr = ReModIn;
        }
    }
    else if (CenterPos.w > 0.0 && DebugMode == 0u)
    {
        float3 CamOrigin = WorldUniforms.CameraOriginTanX.xyz;
        float3 VDir      = normalize(CamOrigin - CenterPos.xyz);
        float  NdotV     = max(1.0e-3, dot(normalize(CenterNorm.xyz), VDir));
        float4 SpecPack  = AccumulationHdrBuffer[PixelIdx];
        float  SpecRough = clamp(SpecPack.w, 0.025, 1.0);
        float  SpecAo    = saturate(pow(NdotV + OutDiffAo, exp2(-16.0 * SpecRough - 1.0)) - 1.0 + OutDiffAo);
        FinalHdr = DirectBase.xyz
                 + SpecPack.xyz * SpecAo
                 + SunContrib.xyz * OutSunVis
                 + AlbedoPack.xyz * OutGi * (0.35 + 0.65 * OutDiffAo);
    }
    else if (CenterPos.w > 0.0 && DebugMode == 8u)
    {
        FinalHdr = ApplyAcesFilmicGpu(OutGi * 1.25);
    }
    else if (CenterPos.w > 0.0 && DebugMode == 12u)
    {
        FinalHdr = float3(OutSunVis);
    }
    else if (CenterPos.w > 0.0 && DebugMode == 13u)
    {
        FinalHdr = float3(OutDiffAo);
    }
    else if (CenterPos.w > 0.0 && DebugMode == 14u)
    {
        FinalHdr = ApplyAcesFilmicGpu(DirectBase.xyz + SunContrib.xyz * OutSunVis);
    }
    else if (CenterPos.w > 0.0 && DebugMode == 15u)
    {
        float3 CamOrigin = WorldUniforms.CameraOriginTanX.xyz;
        float3 VDir      = normalize(CamOrigin - CenterPos.xyz);
        float  NdotV     = max(1.0e-3, dot(normalize(CenterNorm.xyz), VDir));
        float4 SpecPack  = AccumulationHdrBuffer[PixelIdx];
        float  SpecRough = clamp(SpecPack.w, 0.025, 1.0);
        float  SpecAo    = saturate(pow(NdotV + OutDiffAo, exp2(-16.0 * SpecRough - 1.0)) - 1.0 + OutDiffAo);
        FinalHdr = ApplyAcesFilmicGpu(SpecPack.xyz * SpecAo);
    }

    if (DebugMode == 0u)
    {
        float2 PixelUv = (float2(DispatchThreadID.xy) + float2(0.5, 0.5)) / float2(float(Width), float(Height));
        float  NdcX    = PixelUv.x * 2.0 - 1.0;
        float  NdcY    = 1.0 - PixelUv.y * 2.0;
        float3 CamOrigin = WorldUniforms.CameraOriginTanX.xyz;
        float3 RayDir    = normalize(
            WorldUniforms.CameraForwardHeight.xyz +
            WorldUniforms.CameraRightTanY.xyz * (NdcX * WorldUniforms.CameraOriginTanX.w) +
            WorldUniforms.CameraUpWidth.xyz   * (NdcY * WorldUniforms.CameraRightTanY.w));

        float HitDistForClouds = DirectBase.w;
        float HitDistForFog    = SunContrib.w;

        float  LocalCloudTrans = 1.0;
        float3 LocalCloudRad   = EvaluateLocalCloudsSegmentGpu(
            WorldUniforms.CloudParams, CamOrigin, RayDir, HitDistForClouds, float2(DispatchThreadID.xy), LocalCloudTrans);
        FinalHdr = FinalHdr * LocalCloudTrans + LocalCloudRad;

        if (WorldUniforms.FluidParams.HeroBoxMinFade.w > 0.5)
        {
            float3 FluidRad   = float3(0.0);
            float  FluidTrans = 1.0;
            float  IgnJit     = PixelR2(DispatchThreadID.xy).x;
            EvaluateFluidSegmentBufferGpu(
                WorldUniforms.FluidParams,
                FluidHeroThermoTexels,
                FluidWorldThermoTexels,
                CamOrigin,
                RayDir,
                HitDistForFog,
                IgnJit,
                FluidRad,
                FluidTrans);
            FinalHdr = FinalHdr * FluidTrans + FluidRad;
        }

        float  FogTrans = 1.0;
        float3 FogRad   = EvaluateVolumetricFogGpu(
            WorldUniforms.FogParams,
            CamOrigin,
            RayDir,
            HitDistForFog,
            normalize(WorldUniforms.SunDirConeTan.xyz),
            WorldUniforms.SolarIrradianceTier.xyz,
            EvaluateSkyDome(RayDir),
            float2(DispatchThreadID.xy),
            FogTrans);
        FinalHdr = FinalHdr * FogTrans + FogRad;
        FinalHdr += EvaluateVolumetricGodRaysGpu(
            CamOrigin,
            RayDir,
            HitDistForFog,
            normalize(WorldUniforms.SunDirConeTan.xyz),
            WorldUniforms.SolarIrradianceTier.xyz,
            float2(DispatchThreadID.xy));

        if (WorldUniforms.AtmosphereMaskPad.x != 0u && WorldUniforms.LensFlareParams.GhostRotationTint.w > 0.5)
        {
            FinalHdr += EvaluateBakedLensFlareGpu(WorldUniforms.LensFlareParams, BakedLensFlareAtlasTexels, PixelUv);
        }
    }

    float3 DisplayLinear = (DebugMode != 0u) ? FinalHdr : ApplyAcesFilmicGpu(FinalHdr);
    OutDisplayRgba8[PixelIdx] = PackLinearToSrgb8Rgba(DisplayLinear, DispatchThreadID.xy);
}

//------------------------------------------------------------------------------------------------------------------------
// 100% GPU SCENE PRECOMPUTE PASSES (Shadow Cascades, SDF Radiosity Volume, 2-Bounce DDGI Lattice)
//------------------------------------------------------------------------------------------------------------------------

float3 DecodeUnitOctahedralGpu(float2 Uv)
{
    float2 F = Uv * 2.0 - 1.0;
    float3 N = float3(F.x, F.y, 1.0 - abs(F.x) - abs(F.y));
    if (N.z < 0.0)
    {
        N.xy = (1.0 - abs(N.yx)) * float2((N.x >= 0.0) ? 1.0 : -1.0, (N.y >= 0.0) ? 1.0 : -1.0);
    }
    return normalize(N);
}

float3 EvaluateDirectAreaRadiosityGpu(float3 HitPos, float3 HitNrm)
{
    float3 Sum = float3(0.0);
    uint LightCnt = min(4u, WorldUniforms.SdfDimsLightCount.w);
    for (uint LIdx = 0u; LIdx < LightCnt; ++LIdx)
    {
        GpuAreaQuadLight Q = WorldUniforms.AreaLights[LIdx];
        if (max(Q.RadiancePacked.x, max(Q.RadiancePacked.y, Q.RadiancePacked.z)) <= 1.0e-4) continue;
        float3 ToL   = Q.CenterHalfWidth.xyz - HitPos;
        float  Dist2 = max(0.08, dot(ToL, ToL));
        float  Dist  = sqrt(Dist2);
        float3 Wi    = ToL / Dist;
        float  NdotL = dot(HitNrm, Wi);
        if (NdotL <= 0.01) continue;
        float3 LNorm = normalize(cross(Q.RightHalfHeight.xyz, Q.UpTwoSided.xyz));
        float  LCos  = (Q.UpTwoSided.w > 0.5) ? abs(dot(LNorm, -Wi)) : max(0.0, dot(LNorm, -Wi));
        if (LCos <= 0.01) continue;
        if (!TraceSceneAnyHit(HitPos + HitNrm * 4.0e-3, Wi, 0.004, max(0.01, Dist - 0.015)))
        {
            float Area = 4.0 * Q.CenterHalfWidth.w * Q.RightHalfHeight.w;
            float Geom = (NdotL * LCos * Area) / Dist2;
            Sum += Q.RadiancePacked.xyz * min(1.5, Geom * InvPiConst);
        }
    }
    return Sum;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ShadowCascadeComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint CascadeIdx = DispatchThreadID.z;
    if (CascadeIdx > 1u) return;

    uint   Res         = (CascadeIdx == 0u) ? uint(WorldUniforms.FineCascadeParams.x + 0.5) : uint(WorldUniforms.SunCascadeRightRes.w + 0.5);
    float  HalfSpan    = (CascadeIdx == 0u) ? WorldUniforms.FineCascadeCenterSpan.w : WorldUniforms.SunCascadeCenterSpan.w;
    float  DepthExtent = (CascadeIdx == 0u) ? WorldUniforms.FineCascadeParams.y : WorldUniforms.SunCascadeUpDepth.w;
    float3 Center      = (CascadeIdx == 0u) ? WorldUniforms.FineCascadeCenterSpan.xyz : WorldUniforms.SunCascadeCenterSpan.xyz;
    uint   OutOffset   = (CascadeIdx == 0u) ? 0u : uint(WorldUniforms.FineCascadeParams.z + 0.5);

    if (Res < 4u || DispatchThreadID.x >= Res || DispatchThreadID.y >= Res) return;

    float3 LightRight = WorldUniforms.SunCascadeRightRes.xyz;
    float3 LightUp    = WorldUniforms.SunCascadeUpDepth.xyz;
    float3 LightFwd   = WorldUniforms.SunCascadeForwardPad.xyz;

    float LocalU = (((float(DispatchThreadID.x) + 0.5) / float(Res)) * 2.0 - 1.0) * HalfSpan;
    float LocalV = (1.0 - ((float(DispatchThreadID.y) + 0.5) / float(Res)) * 2.0) * HalfSpan;
    float3 RayOrig = Center + LightRight * LocalU + LightUp * LocalV - LightFwd * DepthExtent;

    RayHitReading Hit = TraceSceneClosest(RayOrig, LightFwd, 0.0, DepthExtent * 2.0 + 4.0);
    SolarShadowDepthBuffer[OutOffset + DispatchThreadID.y * Res + DispatchThreadID.x] = Hit.Hit ? Hit.Distance : 1.0e6;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void SdfRadiosityComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint3 Dims = WorldUniforms.SdfDimsLightCount.xyz;
    uint  TotalVoxels = Dims.x * Dims.y * Dims.z;
    uint  Idx = DispatchThreadID.x;
    if (Idx >= TotalVoxels || Dims.x < 2u || Dims.y < 2u || Dims.z < 2u) return;

    uint SXY = Dims.x * Dims.y;
    uint IZ  = Idx / SXY;
    uint Rem = Idx - IZ * SXY;
    uint IY  = Rem / Dims.x;
    uint IX  = Rem - IY * Dims.x;

    float SdfD = SignedDistanceGrid[Idx];
    float Spacing = max(0.04, WorldUniforms.SdfMinSpacing.w);
    float NearBand = Spacing * 1.85;
    if (abs(SdfD) > NearBand)
    {
        SdfRadiosityVoxels[Idx] = float4(0.0, 0.0, 0.0, 0.0);
        SdfBakedVolumeWrite[uint3(IX, IY, IZ)] = float4(0.0, 0.0, 0.0, SdfD);
        return;
    }

    uint XM = (IX > 0u) ? (IX - 1u) : 0u;
    uint XP = min(Dims.x - 1u, IX + 1u);
    uint YM = (IY > 0u) ? (IY - 1u) : 0u;
    uint YP = min(Dims.y - 1u, IY + 1u);
    uint ZM = (IZ > 0u) ? (IZ - 1u) : 0u;
    uint ZP = min(Dims.z - 1u, IZ + 1u);

    float3 GradRaw = float3(
        SignedDistanceGrid[IZ * SXY + IY * Dims.x + XP] - SignedDistanceGrid[IZ * SXY + IY * Dims.x + XM],
        SignedDistanceGrid[IZ * SXY + YP * Dims.x + IX] - SignedDistanceGrid[IZ * SXY + YM * Dims.x + IX],
        SignedDistanceGrid[ZP * SXY + IY * Dims.x + IX] - SignedDistanceGrid[ZM * SXY + IY * Dims.x + IX]);
    float  GLenSq = dot(GradRaw, GradRaw);
    float3 Grad   = (GLenSq > 1.0e-10) ? (GradRaw * rsqrt(GLenSq)) : float3(0.0, 0.0, 1.0);

    float3 VoxelPos = WorldUniforms.SdfMinSpacing.xyz + float3(float(IX), float(IY), float(IZ)) * Spacing;
    RayHitReading Hit = TraceSceneClosest(VoxelPos + Grad * (Spacing * 0.55), -Grad, 1.0e-3, NearBand * 1.75);
    if (!Hit.Hit)
    {
        SdfRadiosityVoxels[Idx] = float4(0.0, 0.0, 0.0, 0.0);
        SdfBakedVolumeWrite[uint3(IX, IY, IZ)] = float4(0.0, 0.0, 0.0, SdfD);
        return;
    }

    uint MatCount = max(1u, WorldUniforms.LatticeDimsMaterialCount.w);
    GpuWorldMaterial80 Mat = MaterialEntries[min(Hit.MaterialSlot, MatCount - 1u)];
    float3 HitNrm = (dot(Hit.GeomNormal, -Grad) < 0.0) ? Hit.GeomNormal : -Hit.GeomNormal;
    float3 DiffAlb = Mat.DiffuseAlbedoRoughness.xyz * (1.0 - saturate(Mat.SpecParams.y)) * (1.0 - saturate(Mat.CoatRoughnessOptics.y));

    float3 DirectIrr = WorldUniforms.SkyRadianceBvhCount.xyz * 0.25;
    float3 SunDir = normalize(WorldUniforms.SunDirConeTan.xyz);
    float  SunNdotL = max(0.0, dot(HitNrm, SunDir));
    if (SunNdotL > 0.0 && !TraceSceneAnyHit(Hit.Position + HitNrm * 3.0e-3, SunDir, 1.0e-3, 100.0))
    {
        DirectIrr += WorldUniforms.SolarIrradianceTier.xyz * (SunNdotL * InvPiConst);
    }
    DirectIrr += EvaluateDirectAreaRadiosityGpu(Hit.Position, HitNrm);

    float3 VoxelRad = Mat.EmissiveRadiancePad.xyz + DiffAlb * DirectIrr;
    SdfRadiosityVoxels[Idx] = float4(VoxelRad, 1.0);
    SdfBakedVolumeWrite[uint3(IX, IY, IZ)] = float4(VoxelRad, SdfD);
}

groupshared float4 SharedDdgiRayDirDist[64];
groupshared float4 SharedDdgiRayRadFront[64];

[shader("compute")]
[numthreads(64, 1, 1)]
void DdgiLatticeComputeEntry(uint3 GroupID : SV_GroupID, uint3 GroupThreadID : SV_GroupThreadID)
{
    uint3 Dims = WorldUniforms.LatticeDimsMaterialCount.xyz;
    uint  TotalCells = Dims.x * Dims.y * Dims.z;
    uint  CellIdx    = GroupID.x;
    uint  ThreadIdx  = GroupThreadID.x;
    if (CellIdx >= TotalCells || Dims.x < 2u || Dims.y < 2u || Dims.z < 2u) return;

    uint SXY = Dims.x * Dims.y;
    uint IZ  = CellIdx / SXY;
    uint Rem = CellIdx - IZ * SXY;
    uint IY  = Rem / Dims.x;
    uint IX  = Rem - IY * Dims.x;

    float3 LatMin  = WorldUniforms.LatticeMinGlassCount.xyz;
    float3 LatMax  = WorldUniforms.LatticeMaxDebugMode.xyz;
    float3 Span    = max(float3(1.0e-3), LatMax - LatMin);
    float3 Spacing = Span / float3(Dims - uint3(1u));
    float3 Origin  = LatMin + float3(float(IX), float(IY), float(IZ)) * Spacing;
    float  MaxDepthClamp = length(Spacing) * 2.25;

    uint PassIdx = WorldUniforms.DenoiseStepParams.x;

    // Trace 1 deterministic Spherical Fibonacci ray per thread (64 rays/cell)
    float Z   = 1.0 - (2.0 * (float(ThreadIdx) + 0.5)) / 64.0;
    float Rad = sqrt(max(0.0, 1.0 - Z * Z));
    float Phi = TauConst * frac(float(ThreadIdx) * 0.61803398875);
    float3 Dir = float3(Rad * cos(Phi), Rad * sin(Phi), Z);

    RayHitReading Hit = TraceSceneClosest(Origin, Dir, 1.0e-3, 60.0);
    float  HitDist   = Hit.Hit ? min(Hit.Distance, MaxDepthClamp) : MaxDepthClamp;
    float3 RayRad    = float3(0.0);
    float  FrontFlag = 1.0;

    if (!Hit.Hit)
    {
        RayRad = EvaluateSkyDome(Dir);
    }
    else
    {
        float FacingDot = dot(Hit.GeomNormal, Dir);
        FrontFlag = (FacingDot < 0.0) ? 1.0 : 0.0;
        if (FrontFlag > 0.5)
        {
            uint MatCount = max(1u, WorldUniforms.LatticeDimsMaterialCount.w);
            GpuWorldMaterial80 Mat = MaterialEntries[min(Hit.MaterialSlot, MatCount - 1u)];
            float3 HitNrm = -Hit.GeomNormal;
            float3 DiffAlb = Mat.DiffuseAlbedoRoughness.xyz * (1.0 - saturate(Mat.SpecParams.y)) * (1.0 - saturate(Mat.CoatRoughnessOptics.y));

            float3 SunDir = normalize(WorldUniforms.SunDirConeTan.xyz);
            float  SunNdotL = max(0.0, dot(HitNrm, SunDir));
            float3 DirectIrr = float3(0.0);
            if (SunNdotL > 0.0 && !TraceSceneAnyHit(Hit.Position + HitNrm * 3.0e-3, SunDir, 1.0e-3, 100.0))
            {
                DirectIrr += WorldUniforms.SolarIrradianceTier.xyz * (SunNdotL * InvPiConst);
            }
            DirectIrr += EvaluateDirectAreaRadiosityGpu(Hit.Position, HitNrm);

            if (PassIdx > 0u)
            {
                float3 BiasPos = Hit.Position + HitNrm * 0.06;
                float3 Rel     = saturate((BiasPos - LatMin) / Span) * float3(Dims - uint3(1u));
                int3   C0      = clamp(int3(floor(Rel)), int3(0), int3(Dims) - int3(1));
                int3   C1      = min(C0 + int3(1), int3(Dims) - int3(1));
                float3 Frac    = saturate(Rel - float3(C0));
                float2 OctIrr  = EncodeUnitOctahedral(HitNrm);
                uint   TexId   = min(7u, uint(OctIrr.y * 8.0)) * 8u + min(7u, uint(OctIrr.x * 8.0));
                float3 PrevIrr = float3(0.0);
                for (uint DZ = 0u; DZ < 2u; ++DZ)
                {
                    float WZ = (DZ == 0u) ? (1.0 - Frac.z) : Frac.z;
                    uint  IZ2 = uint((DZ == 0u) ? C0.z : C1.z);
                    for (uint DY = 0u; DY < 2u; ++DY)
                    {
                        float WY = (DY == 0u) ? (1.0 - Frac.y) : Frac.y;
                        uint  IY2 = uint((DY == 0u) ? C0.y : C1.y);
                        for (uint DX = 0u; DX < 2u; ++DX)
                        {
                            float WX = (DX == 0u) ? (1.0 - Frac.x) : Frac.x;
                            uint  IX2 = uint((DX == 0u) ? C0.x : C1.x);
                            uint  CId = IZ2 * SXY + IY2 * Dims.x + IX2;
                            PrevIrr += PingGiHdr[CId * 64u + TexId].xyz * (WX * WY * WZ);
                        }
                    }
                }
                DirectIrr += PrevIrr * 0.85;
            }
            RayRad = Mat.EmissiveRadiancePad.xyz + DiffAlb * DirectIrr;
        }
    }

    SharedDdgiRayDirDist[ThreadIdx]  = float4(Dir, HitDist);
    SharedDdgiRayRadFront[ThreadIdx] = float4(RayRad, FrontFlag);
    GroupMemoryBarrierWithGroupSync();

    // Convolve 8x8 Octahedral Irradiance/Pi for texel ThreadIdx
    uint TX = ThreadIdx & 7u;
    uint TY = ThreadIdx >> 3u;
    float3 TexelDir = DecodeUnitOctahedralGpu(float2((float(TX) + 0.5) / 8.0, (float(TY) + 0.5) / 8.0));
    float3 IrrSum   = float3(0.0);
    float  WeightS  = 0.0;
    float  BackHits = 0.0;
    for (uint R = 0u; R < 64u; ++R)
    {
        float4 DD = SharedDdgiRayDirDist[R];
        float4 RF = SharedDdgiRayRadFront[R];
        float  CosW = max(0.0, dot(TexelDir, DD.xyz));
        if (CosW > 0.0)
        {
            IrrSum  += RF.xyz * CosW;
            WeightS += CosW;
        }
        if (RF.w < 0.5) BackHits += 1.0;
    }
    float3 OutIrr = (WeightS > 1.0e-5) ? (IrrSum / WeightS) : float3(0.0);
    float  BackRatio = BackHits / 64.0;

    if (PassIdx == 0u)
    {
        PingGiHdr[CellIdx * 64u + ThreadIdx] = float4(OutIrr, BackRatio);
        // Also populate 16x16 Chebyshev radial depth moments (4 texels per thread)
        for (uint Sub = 0u; Sub < 4u; ++Sub)
        {
            uint   DIdx = ThreadIdx * 4u + Sub;
            uint   DX   = DIdx & 15u;
            uint   DY   = DIdx >> 4u;
            float3 DDir = DecodeUnitOctahedralGpu(float2((float(DX) + 0.5) / 16.0, (float(DY) + 0.5) / 16.0));
            float  SumW = 0.0, SumR = 0.0, SumR2 = 0.0;
            for (uint R = 0u; R < 64u; ++R)
            {
                float4 DD = SharedDdgiRayDirDist[R];
                float  DotV = max(0.0, dot(DDir, DD.xyz));
                if (DotV > 0.0)
                {
                    float W = pow(DotV, 16.0);
                    SumW  += W;
                    SumR  += DD.w * W;
                    SumR2 += (DD.w * DD.w) * W;
                }
            }
            float MeanR  = (SumW > 1.0e-5) ? (SumR  / SumW) : MaxDepthClamp;
            float MeanR2 = (SumW > 1.0e-5) ? (SumR2 / SumW) : (MaxDepthClamp * MaxDepthClamp);
            IrradianceChebyshevDepth[CellIdx * 256u + DIdx] = float2(MeanR, MeanR2);
        }
    }
    else
    {
        IrradianceLatticeTexels[CellIdx * 64u + ThreadIdx] = float4(OutIrr, BackRatio);
    }
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ImportanceSpatialReuseEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Width  = uint(WorldUniforms.CameraUpWidth.w + 0.5);
    uint Height = uint(WorldUniforms.CameraForwardHeight.w + 0.5);
    if (DispatchThreadID.x >= Width || DispatchThreadID.y >= Height)
    {
        return;
    }

    uint   PixelIdx   = DispatchThreadID.y * Width + DispatchThreadID.x;
    float4 CenterPos  = GBufPosMask[PixelIdx];
    float4 CenterNorm = GBufNormAo[PixelIdx];

    if (CenterPos.w <= 0.0 || !IsGiEnabled())
    {
        ReservoirHistoryBuffer[PixelIdx * 2u + 0u] = float4(0.0);
        ReservoirHistoryBuffer[PixelIdx * 2u + 1u] = float4(0.0);
        return;
    }

    float3 P1        = CenterPos.xyz;
    float3 N1        = normalize(CenterNorm.xyz);
    float3 CenterAlb = GBufAlbedo[PixelIdx].rgb;

    float4 Cur0  = WeightedCurrentBuffer[PixelIdx * 2u + 0u];
    float4 Cur1  = WeightedCurrentBuffer[PixelIdx * 2u + 1u];
    uint   Meta  = asuint(Cur1.w);
    uint   CM    = Meta & 0xFFFFu;
    bool   CVal  = ((Meta & (1u << 16u)) != 0u);
    bool   CSky  = ((Meta & (1u << 17u)) != 0u);

    float3 CDir  = CSky ? normalize(Cur0.xyz) : normalize(Cur0.xyz - P1);
    float  CPdf  = CVal ? (dot(Cur1.rgb, float3(0.2126, 0.7152, 0.0722)) * max(0.0, dot(N1, CDir)) * InvPiConst) : 0.0;
    float  WSum  = CPdf * Cur0.w * float(CM);
    float3 WPos  = Cur0.xyz;
    float3 WRad  = Cur1.rgb;
    float  WPdf  = CPdf;
    uint   WSky  = CSky ? 1u : 0u;
    uint   WVal  = CVal ? 1u : 0u;

    uint SeqIdx = WorldUniforms.DenoiseStepParams.w;
    uint Rng    = AvalancheKey32(DispatchThreadID.x * 747796405u + DispatchThreadID.y * 2891336453u + (SeqIdx + 1u) * 277803737u) | 1u;

    for (uint Tap = 0u; Tap < 4u; ++Tap)
    {
        float Angle  = TauConst * ((float(Tap) + PcgFloat01(Rng)) * 0.25);
        float Radius = 2.0 + 8.0 * sqrt(PcgFloat01(Rng));
        int   NX     = clamp(int(round(float(DispatchThreadID.x) + cos(Angle) * Radius)), 0, int(Width) - 1);
        int   NY     = clamp(int(round(float(DispatchThreadID.y) + sin(Angle) * Radius)), 0, int(Height) - 1);
        uint  NIdx   = uint(NY) * Width + uint(NX);

        float4 NPos = GBufPosMask[NIdx];
        if (NPos.w <= 0.0) continue;
        float3 NNrm = normalize(GBufNormAo[NIdx].xyz);
        if (abs(dot(N1, NPos.xyz - P1)) > 0.08 || dot(N1, NNrm) < 0.90) continue;

        float3 NAlb = GBufAlbedo[NIdx].rgb;
        if (dot(abs(CenterAlb - NAlb), float3(1.0)) > 0.35) continue;

        float4 N0    = WeightedCurrentBuffer[NIdx * 2u + 0u];
        float4 N1Buf = WeightedCurrentBuffer[NIdx * 2u + 1u];
        uint   NMeta = asuint(N1Buf.w);
        uint   NM    = min(NMeta & 0xFFFFu, 16u);
        if (((NMeta & (1u << 16u)) == 0u) || NM == 0u) continue;
        bool   NSky  = ((NMeta & (1u << 17u)) != 0u);

        float3 DirC  = NSky ? normalize(N0.xyz) : normalize(N0.xyz - P1);
        float  CosC  = max(0.0, dot(N1, DirC));
        float  NPdf  = dot(N1Buf.rgb, float3(0.2126, 0.7152, 0.0722)) * CosC * InvPiConst;
        if (NPdf <= 1.0e-6) continue;

        float Jacobian = 1.0;
        if (!NSky)
        {
            float DistC = length(N0.xyz - P1);
            if (DistC <= 0.02) continue;
            if (TraceSceneAnyHit(P1 + N1 * 2.5e-3, DirC, 1.0e-3, max(0.01, DistC - 0.02))) continue;
            float DistN = max(0.05, length(N0.xyz - NPos.xyz));
            Jacobian = clamp((DistN * DistN) / (DistC * DistC), 0.0, 3.0);
        }
        else if (TraceSceneAnyHit(P1 + N1 * 2.5e-3, DirC, 1.0e-3, 80.0))
        {
            continue;
        }

        float NW = NPdf * Jacobian * N0.w * float(NM);
        CM   += NM;
        WSum += NW;
        if (PcgFloat01(Rng) < (NW / max(1.0e-12, WSum)))
        {
            WPos = N0.xyz;
            WRad = N1Buf.rgb;
            WPdf = NPdf;
            WSky = NSky ? 1u : 0u;
            WVal = 1u;
        }
    }

    float3 DirOut  = (WSky != 0u) ? normalize(WPos) : normalize(WPos - P1);
    float  CosOut  = max(0.0, dot(N1, DirOut));
    float  MaxRawW = (20.0 * PiConst) / max(1.0e-3, CosOut);
    float  FinalW  = (WVal != 0u && WPdf > 1.0e-9 && CM > 0u) ? clamp(WSum / (float(CM) * WPdf), 0.0, MaxRawW) : 0.0;
    CM             = min(CM, 24u);
    float3 GiOut   = (WVal != 0u) ? (WRad * ((CosOut * InvPiConst) * FinalW)) : float3(0.0);

    uint OutMeta = (CM & 0xFFFFu) | ((WVal & 1u) << 16u) | ((WSky & 1u) << 17u);
    ReservoirHistoryBuffer[PixelIdx * 2u + 0u] = float4(WPos, FinalW);
    ReservoirHistoryBuffer[PixelIdx * 2u + 1u] = float4(WRad, asfloat(OutMeta));
    PingGiHdr[PixelIdx] = float4(GiOut, float(CM));
}

