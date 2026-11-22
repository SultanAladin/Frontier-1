/*============================================================================================================================================
                                                    WORLDVIEWPORTRADIOMETRICCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 6-Engine Unified GPU Compute Shader:
// 0=MatCap, 1=OpenPbrDirect, 2=RealTimeGiGtxLowEnd, 3=RealTimeGiHighEnd, 4=PathTracerGameRealtime, 5=PathTracerSceneFinalRender

#define FRONTIER_ATMOSPHERE_EMBEDDED 1
[[vk::binding(20, 0)]] StructuredBuffer<uint> PlanetaryCloudNoise;
#include "../Atmosphere/SkyViewTransmittanceCompute.slang"
#include "../Atmosphere/VolumetricCloudCompute.slang"
#include "../Atmosphere/VolumetricFogCompute.slang"
#include "../Atmosphere/CelestialDiscStarCompute.slang"
#include "../Atmosphere/LensFlareCompute.slang"
[[vk::binding(21, 0)]] StructuredBuffer<GpuRawCatalogStar> RawCatalogStars;

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
    float4 SolarIrradiancePad;       // xyz = SolarIrradiance, w = Reserved
    float4 SkyRadianceBvhCount;      // xyz = SkyRadiance,  w = BvhEntryCount
    uint4  LightGlassMaterialDebug;  // x = AreaLightCount, y = GlassSphereCount, z = MaterialCount, w = DebugChannel
    uint4  EngineShadowReflPad;      // x = EngineMode (0..1), y = ShadowAlg (0..4), z = ReflSource (0..1), w = FrameIndex
    float4 SunCascadeCenterSpan;     // xyz = CenterWorld,  w = HalfSpanMeters
    float4 SunCascadeRightRes;       // xyz = LightRight,   w = ResolutionTexels
    float4 SunCascadeUpDepth;        // xyz = LightUp,      w = DepthExtentMeters
    float4 SunCascadeForwardPad;     // xyz = LightForward, w = Reserved
    float4 FineCascadeCenterSpan;    // xyz = FineCenter,   w = FineHalfSpanMeters
    float4 FineCascadeParams;        // x = FineRes, y = FineDepthExtent, z = CoarseOffsetTexels, w = Reserved
    uint4  DenoiseStepParams;        // x = StridePx, y = Flags (bit0=FinalComposite, bit1=SpatialDenoise), z = Reserved, w = FrameSequence
    GpuAreaQuadLight AreaLights[4];
    GpuGlassSphere   GlassSpheres[4];
    uint4                       AtmosphereMaskPad; // x = AtmosphereEnabled (0/1), y = SkyViewW, z = SkyViewH, w = FlagBits
    VolumetricCloudUniformBlock CloudParams;
    VolumetricFogUniformBlock   FogParams;
    CelestialFieldUniformBlock  CelestialParams;
    LensFlareUniformBlock       LensFlareParams;
    float4                      GlobalDistanceFineCenterSpan;   // xyz = FineCenterWorld,   w = FineHalfSpanMeters
    float4                      GlobalDistanceCoarseCenterSpan; // xyz = CoarseCenterWorld, w = CoarseHalfSpanMeters
    uint4                       GlobalDistanceBakeControl;      // x = BakePhase, y = CascadeIdx, z = GdfReadyMask, w = XeGTAO (bit0 enable, bits 8..15 strength)
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

struct GpuPhotometricLamp64
{
    float4 PositionRange;      // xyz = PositionWorld, w = RangeMeters
    float4 BeamAxisRadius;     // xyz = BeamAxis,      w = RadiusMeters
    float4 ZeroAzimuthProfile; // xyz = ZeroAzimuth,   w = float(ProfileOrdinal)
    float4 PeakIntensityPad;   // xyz = PeakIntensity, w = Reserved
};

struct GpuObjectDistanceInstance128
{
    float4 WorldToLocalRow0; // xyz = R^T row 0,       w = WorldTranslation.X
    float4 WorldToLocalRow1; // xyz = R^T row 1,       w = WorldTranslation.Y
    float4 WorldToLocalRow2; // xyz = R^T row 2,       w = WorldTranslation.Z
    float4 LocalMinScale;    // xyz = LocalMin,        w = UniformScale
    float4 LocalMaxInvScale; // xyz = LocalMax,        w = InvUniformScale
    float4 InvSpacingPad;    // xyz = 1/VoxelSpacing,  w = MaxVoxelSpacing
    uint4  BrickDims;        // x = BrickStartTexel, y = ResX, z = ResY, w = ResZ
    uint4  MaterialMeta;     // x = MaterialOverride (0xFFFF = per-voxel), y = ActiveFlag
};

[[vk::binding(0,  0)]] ConstantBuffer<WorldViewportUniformBlock> WorldUniforms;
[[vk::binding(1,  0)]] StructuredBuffer<GpuWideTreeEntry128>     SceneBvhEntries;
[[vk::binding(2,  0)]] StructuredBuffer<GpuShadedTriangle96>     SceneTriangles;
[[vk::binding(3,  0)]] StructuredBuffer<GpuWorldMaterial80>      MaterialEntries;
[[vk::binding(4,  0)]] RWStructuredBuffer<uint>                  OutDisplayRgba8;
[[vk::binding(5,  0)]] RWStructuredBuffer<float>                 SolarShadowDepthBuffer;
[[vk::binding(6,  0)]] RWStructuredBuffer<float4>                AccumulationHdrBuffer;
[[vk::binding(7,  0)]] StructuredBuffer<float4>                  AtmosphericSkyViewLut;
[[vk::binding(8,  0)]] StructuredBuffer<uint>                    LunarSurfaceAtlasTexels;
[[vk::binding(9,  0)]] StructuredBuffer<uint>                    BakedLensFlareAtlasTexels;
[[vk::binding(10, 0)]] RWStructuredBuffer<float4>                GBufPosMask;
[[vk::binding(11, 0)]] RWStructuredBuffer<float4>                GBufNormAo;
[[vk::binding(12, 0)]] RWStructuredBuffer<float4>                GBufDirectBase;
[[vk::binding(13, 0)]] RWStructuredBuffer<float4>                GBufSunContrib;
[[vk::binding(14, 0)]] RWStructuredBuffer<float4>                GBufAlbedo;
[[vk::binding(15, 0)]] RWStructuredBuffer<float4>                PingShadowAo;
[[vk::binding(16, 0)]] RWStructuredBuffer<float4>                PongShadowAo;
[[vk::binding(17, 0)]] RWStructuredBuffer<float4>                CloudRadiometricBuffer;
[[vk::binding(18, 0)]] StructuredBuffer<GpuProjectedStar>        ProjectedCatalogStars;
[[vk::binding(19, 0)]] StructuredBuffer<uint>                    StellarTileBins;
[[vk::binding(22, 0)]] StructuredBuffer<float>                   PhotometricProfileTables;
[[vk::binding(23, 0)]] StructuredBuffer<GpuPhotometricLamp64>    PhotometricLampEntries;
[[vk::binding(24, 0)]] RWStructuredBuffer<float>                 GlobalDistanceFieldBuffer;
[[vk::binding(25, 0)]] RWStructuredBuffer<uint2>                 GlobalRadiosityFieldBuffer;
[[vk::binding(26, 0)]] RWStructuredBuffer<float4>                PingIndirectIrradiance;
[[vk::binding(27, 0)]] RWStructuredBuffer<float4>                PongIndirectIrradiance;
[[vk::binding(28, 0)]] RWStructuredBuffer<uint2>                 GlobalSurfaceShellBuffer;
[[vk::binding(29, 0)]] StructuredBuffer<uint2>                   LocalDistanceBricks;
[[vk::binding(30, 0)]] StructuredBuffer<GpuObjectDistanceInstance128> ObjectDistanceInstances;

static const float PiConst    = 3.14159265359;
static const float TauConst   = 6.28318530718;
static const float InvPiConst = 0.31830988618;
static const uint  GdfGridRes = 64u;
static const uint  GdfVoxCnt  = 262144u; // 64^3
static const uint  RadMip0Off = 0u;
static const uint  RadMip1Off = 262144u; // + 64^3
static const uint  RadMip2Off = 294912u; // + 32^3
static const uint  RadMip3Off = 299008u; // + 16^3 (+ 8^3 = 299520 total)

struct RayHitReading
{
    bool   Hit;
    float  Distance;
    float3 Position;
    float3 ShadingNormal;
    float3 GeomNormal;
    uint   MaterialSlot;
    uint   LodLevel;       // Cluster LOD debug: hierarchy level of the hit triangle (packed in Corner0Pad.w)
    uint   ClusterOrdinal; // Cluster LOD debug: global cluster ordinal of the hit triangle (packed in Edge01Pad.w)
};

uint PcgNext(inout uint SeedVal)
{
    SeedVal = SeedVal * 747796405u + 2891336453u;
    uint Word = ((SeedVal >> ((SeedVal >> 28u) + 4u)) ^ SeedVal) * 277803737u;
    return (Word >> 22u) ^ Word;
}

float PcgFloat01(inout uint SeedVal)
{
    return float(PcgNext(SeedVal) & 0x00FFFFFFu) * (1.0 / 16777216.0);
}

float2 PixelR2(uint2 PixelXY)
{
    uint   Tick = WorldUniforms.DenoiseStepParams.w & 63u;
    uint   Rng  = (PixelXY.x * 1973u + PixelXY.y * 9277u + Tick * 26699u + 911u) | 1u;
    float  U1   = PcgFloat01(Rng);
    float  U2   = PcgFloat01(Rng);
    return float2(U1, U2);
}

uint2 PackHalf4(float4 V)
{
    float4 Scaled = V * float4(2047.96875, 2047.96875, 2047.96875, 65535.0) + 0.5;
    uint4  Q      = uint4(clamp(Scaled, float4(0.0), float4(65535.0)));
    return uint2(Q.x | (Q.y << 16u), Q.z | (Q.w << 16u));
}

float4 UnpackHalf4(uint2 P)
{
    uint4 Q = uint4(P.x & 0xFFFFu, P.x >> 16u, P.y & 0xFFFFu, P.y >> 16u);
    return float4(Q) * float4(1.0 / 2047.96875, 1.0 / 2047.96875, 1.0 / 2047.96875, 1.0 / 65535.0);
}

uint PackOctNormal32(float3 N)
{
    float  InvL1 = 1.0 / max(1.0e-6, abs(N.x) + abs(N.y) + abs(N.z));
    float2 Oct   = N.xy * InvL1;
    if (N.z < 0.0)
    {
        float2 SignXY = select(Oct >= float2(0.0), float2(1.0), float2(-1.0));
        Oct = (float2(1.0) - abs(Oct.yx)) * SignXY;
    }
    int2 Quant = int2(clamp(round(Oct * 32767.0), float2(-32767.0), float2(32767.0)));
    return (uint(Quant.x) & 0xFFFFu) | ((uint(Quant.y) & 0xFFFFu) << 16u);
}

float3 UnpackOctNormal32(uint Packed)
{
    int  QX = int(Packed << 16u) >> 16;
    int  QY = int(Packed) >> 16;
    float2 F = clamp(float2(float(QX), float(QY)) / 32767.0, float2(-1.0), float2(1.0));
    float3 N = float3(F.x, F.y, 1.0 - abs(F.x) - abs(F.y));
    float  T = max(-N.z, 0.0);
    N.x += (N.x >= 0.0) ? -T : T;
    N.y += (N.y >= 0.0) ? -T : T;
    return normalize(N);
}

bool IsReflectionsEnabled() { return (WorldUniforms.AtmosphereMaskPad.w & 2u) == 0u; }
bool IsRaytracingEnabled()  { return (WorldUniforms.AtmosphereMaskPad.w & 8u) == 0u; }

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
    int   IX = int(floor(FX));
    int   IY = int(floor(FY));
    float TX = FX - float(IX);
    float TY = FY - float(IY);

    uint FullW = uint(WorldUniforms.CameraUpWidth.w + 0.5);
    if (CW >= FullW || CW < 4u || CH < 4u)
    {
        uint X0 = uint(clamp(IX, 0, int(CW) - 1));
        uint Y0 = uint(clamp(IY, 0, int(CH) - 1));
        uint X1 = min(CW - 1u, X0 + 1u);
        uint Y1 = min(CH - 1u, Y0 + 1u);
        float4 C00 = CloudRadiometricBuffer[Y0 * CW + X0];
        float4 C10 = CloudRadiometricBuffer[Y0 * CW + X1];
        float4 C01 = CloudRadiometricBuffer[Y1 * CW + X0];
        float4 C11 = CloudRadiometricBuffer[Y1 * CW + X1];
        float4 Res = lerp(lerp(C00, C10, TX), lerp(C01, C11, TX), TY);
        OutCloudTrans = clamp(Res.w, 0.0, 1.0);
        return Res;
    }

    float OmX = 1.0 - TX;
    float TX2 = TX * TX;
    float TX3 = TX2 * TX;
    float WX[4] = {
        (OmX * OmX * OmX) * (1.0 / 6.0),
        (3.0 * TX3 - 6.0 * TX2 + 4.0) * (1.0 / 6.0),
        (-3.0 * TX3 + 3.0 * TX2 + 3.0 * TX + 1.0) * (1.0 / 6.0),
        TX3 * (1.0 / 6.0)
    };
    float OmY = 1.0 - TY;
    float TY2 = TY * TY;
    float TY3 = TY2 * TY;
    float WY[4] = {
        (OmY * OmY * OmY) * (1.0 / 6.0),
        (3.0 * TY3 - 6.0 * TY2 + 4.0) * (1.0 / 6.0),
        (-3.0 * TY3 + 3.0 * TY2 + 3.0 * TY + 1.0) * (1.0 / 6.0),
        TY3 * (1.0 / 6.0)
    };

    float4 Res = float4(0.0, 0.0, 0.0, 0.0);
    int MaxX = int(CW) - 1;
    int MaxY = int(CH) - 1;
    [unroll]
    for (int DY = 0; DY < 4; ++DY)
    {
        uint SY = uint(clamp(IY + DY - 1, 0, MaxY));
        float4 Row = float4(0.0, 0.0, 0.0, 0.0);
        [unroll]
        for (int DX = 0; DX < 4; ++DX)
        {
            uint SX = uint(clamp(IX + DX - 1, 0, MaxX));
            Row += CloudRadiometricBuffer[SY * CW + SX] * WX[DX];
        }
        Res += Row * WY[DY];
    }

    OutCloudTrans = clamp(Res.w, 0.0, 1.0);
    return max(float4(0.0, 0.0, 0.0, 0.0), Res);
}

float3 EvaluateCloudDeckReflectionGpu(float3 RayDir, out float OutCloudTrans)
{
    OutCloudTrans = 1.0;
    VolumetricCloudUniformBlock C = WorldUniforms.CloudParams;
    if (C.SunAxisToggle.w < 0.5 || RayDir.z <= 0.015 || C.DeckGeometry.z <= 0.01 || C.DeckGeometry.w <= 0.01)
    {
        return float3(0.0, 0.0, 0.0);
    }
    CloudSpecification Spec = BuildCloudSpecificationFromUniformGpu(C, false);
    float3 Origin           = ResolvePlanetaryViewOriginGpu(C, C.OriginTimeVector.xyz);
    CloudDeck Layer         = PlanetCloudLayer(Spec, 0u);
    float MidAlt            = (Layer.BottomM + Layer.TopM) * 0.5;
    float DistM             = min(38000.0, MidAlt / max(0.03, RayDir.z));
    float3 SamplePos        = Origin + RayDir * DistM;
    float3 Adv              = CloudWindCoordinates(Spec, SamplePos, Layer.Category);
    CloudWeatherSample Wx   = CloudWeatherFromAdvectedNormal(Spec, Adv / length(Adv));
    float Sigma             = SamplePlanetCloudShadowDensity(Spec, Layer, SamplePos, Adv, Wx, max(12.0, DistM * Spec.PixelAngle));
    if (Sigma <= 1.0e-6)
    {
        return float3(0.0, 0.0, 0.0);
    }
    float Tau = Sigma * (Layer.TopM - Layer.BottomM) * 0.45 / max(0.15, RayDir.z);
    OutCloudTrans = exp(-Tau);
    float SunT = exp(-CloudSolarOpticalDepthWithWeather(Spec, SamplePos, Wx, 3u, max(16.0, DistM * Spec.PixelAngle)));
    return (Spec.SolarIrradiance * (0.28 * SunT) + Spec.SkyAmbient * 0.55) * (1.0 - OutCloudTrans);
}

float OrbitalGlobeFbmGpu3(float3 P)
{
    float  Sum = 0.0;
    float  Amp = 0.52;
    float3 Q   = P;
    for (uint Oct = 0u; Oct < 5u; ++Oct)
    {
        Sum += Amp * CloudLatticeNoise(Q);
        Q = float3(
             0.80 * Q.x - 0.48 * Q.y + 0.36 * Q.z,
             0.48 * Q.x + 0.86 * Q.y + 0.16 * Q.z,
            -0.36 * Q.x + 0.04 * Q.y + 0.93 * Q.z) * 2.05 + float3(13.7, -7.3, 4.1);
        Amp *= 0.48;
    }
    return Sum;
}

float3 EvaluateOrbitalSpaceGpu(
    float3     RayOrigin,
    float3     RayDir,
    float3     SunDir,
    float2     PixelCoord,
    out bool   OutHitsPlanet,
    out float3 OutSpaceTrans)
{
    VolumetricCloudUniformBlock C = WorldUniforms.CloudParams;
    float PlanetR = max(500000.0, C.SolarIrradianceExt.w);
    float AtmoThk = max(20000.0, C.CloudAtmoStrengths.w > 100.0 ? C.CloudAtmoStrengths.w : 100000.0);
    float ObsAlt  = max(AtmoThk + 5000.0, C.WindForceMeta.w);
    float3 CamPos = float3(RayOrigin.x, RayOrigin.y, PlanetR + ObsAlt + RayOrigin.z);

    float GroundM = PlanetCloudGroundDistance(CamPos, RayDir, PlanetR);
    OutHitsPlanet = (GroundM < 1.0e8);

    uint BaseFmt = uint(C.FormationOptics.x + 0.5);
    CloudSpecification OrbSpec = BuildCloudSpecificationFromUniformGpu(C, false, (BaseFmt == 2u) ? 12u : BaseFmt);
    OrbSpec.Steps      = clamp(OrbSpec.Steps, 24u, 64u);
    OrbSpec.LightSteps = clamp(OrbSpec.LightSteps, 3u, 6u);

    CloudTransportIntegral Cloud = TracePlanetaryClouds(OrbSpec, CamPos, RayDir, PixelCoord, GroundM);
    CloudAirSegment Air = IntegrateCloudAir(OrbSpec, CamPos, RayDir, GroundM, 24u, false);

    float3 Behind = Air.Radiance;
    if (OutHitsPlanet)
    {
        float3 HitP = CamPos + RayDir * GroundM;
        float3 N    = normalize(HitP);
        float  NdotLRaw = dot(N, SunDir);
        float  SunM     = max(0.0, NdotLRaw);
        float  Cont     = OrbitalGlobeFbmGpu3(N * 3.1 + float3(4.1, -1.8, 0.9));
        float  PolarIce = smoothstep(0.83 + 0.06 * Cont, 0.92 + 0.06 * Cont, abs(N.z));
        float  LandMask = smoothstep(0.47, 0.515, Cont);
        float  Shelf    = smoothstep(0.415, 0.48, Cont) * (1.0 - LandMask);
        float  Moist    = OrbitalGlobeFbmGpu3(N * 7.5 + float3(-2.3, 5.1, 0.0));
        float  Elev     = saturate((Cont - 0.49) / 0.24);
        float3 LandCol  = lerp(float3(0.055, 0.145, 0.048), float3(0.350, 0.265, 0.165), saturate((Moist - 0.44) / 0.24)) * (0.85 + 0.35 * Elev);
        float3 OceanCol = float3(0.012 + 0.018 * Shelf, 0.042 + 0.058 * Shelf, 0.135 + 0.055 * Shelf);
        float3 Alb      = lerp(OceanCol, LandCol, LandMask);
        Alb             = lerp(Alb, float3(0.72, 0.76, 0.82), PolarIce);

        float3 HVec      = normalize(SunDir - RayDir);
        float  OceanSpec = (1.0 - LandMask) * (1.0 - PolarIce) * pow(max(0.0, dot(N, HVec)), 42.0) * 0.45 * (0.2 + 0.8 * SunM);
        float  TermWarm  = exp(-abs(NdotLRaw) * 14.0) * saturate((NdotLRaw + 0.08) / 0.16);
        float  SVBase    = 0.35 + 0.65 * saturate((NdotLRaw + 0.04) / 0.25);
        float3 SunVisC   = float3(SVBase * (1.0 + 0.65 * TermWarm), SVBase * (1.0 + 0.22 * TermWarm), SVBase * (1.0 - 0.10 * TermWarm));

        float FootprintM = max(1.0, GroundM * OrbSpec.PixelAngle);
        float CldShad    = (C.SunAxisToggle.w > 0.5)
            ? exp(-CloudSolarOpticalDepth(OrbSpec, HitP, 8u, FootprintM))
            : 1.0;
        float3 Surf = (Alb * SunM * CldShad + float3(OceanSpec)) * SunVisC * 0.28;

        float NightMask = saturate((-NdotLRaw - 0.04) / 0.14) * LandMask * (1.0 - PolarIce);
        if (NightMask > 0.01)
        {
            float City = saturate((OrbitalGlobeFbmGpu3(N * 16.0) - 0.60) / 0.12) * NightMask * (0.35 + 0.65 * CldShad);
            Surf += float3(City * 0.38, City * 0.22, City * 0.06);
        }
        Behind        = Air.Radiance + Air.Transmittance * Surf;
        OutSpaceTrans = float3(0.0, 0.0, 0.0);
    }
    else
    {
        OutSpaceTrans = Air.Transmittance * Cloud.Transmittance;
    }

    float3 Radiance = Behind * Cloud.Transmittance + CloudRadianceThroughAir(OrbSpec, CamPos, RayDir, Cloud, 16u);
    return max(float3(0.0, 0.0, 0.0), Radiance);
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

    int AtmoFlags = int(round(max(0.0, WorldUniforms.CelestialParams.AtmoMediumPack.w)));
    bool IsOrbital = (AtmoFlags & 2) != 0;
    if (!IsOrbital && WorldUniforms.AtmosphereMaskPad.x == 0u)
    {
        return EvaluateSkyDome(RayDir);
    }

    float3 BaseSky    = float3(0.0, 0.0, 0.0);
    float3 SpaceTrans = float3(1.0, 1.0, 1.0);
    if (IsOrbital)
    {
        bool HitsPlanet = false;
        BaseSky = EvaluateOrbitalSpaceGpu(
            WorldUniforms.CameraOriginTanX.xyz, RayDir, normalize(WorldUniforms.SunDirConeTan.xyz), SubPix, HitsPlanet, SpaceTrans);
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
        AtmosphericSkyViewLut,
        WorldUniforms.CloudParams.CloudAtmoStrengths.w,
        LunarSurfaceAtlasTexels,
        ProjectedCatalogStars,
        StellarTileBins,
        RawCatalogStars,
        SubPix,
        RayDir,
        UseTiled);
    if (IsOrbital)
    {
        return BaseSky + CelestialRad * SpaceTrans;
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

    CloudRad = EvaluateCloudDeckReflectionGpu(RayDir, CloudTrans);
    return (SkyRad + CelestialRad) * CloudTrans + CloudRad;
}

float EvaluateSolarCloudTransmittanceGpu(float3 WorldPos, float3 SunDir)
{
    if (WorldUniforms.AtmosphereMaskPad.x == 0u || SunDir.z <= 0.01)
    {
        return 1.0;
    }
    float Trans = 1.0;
    if (WorldUniforms.CloudParams.SunAxisToggle.w >= 0.5 && WorldUniforms.CloudParams.WindVectorOcclude.z >= 0.5)
    {
        float DeckBaseM = max(200.0, WorldUniforms.CloudParams.DeckGeometry.x);
        float Coverage  = clamp(WorldUniforms.CloudParams.DeckGeometry.z, 0.0, 1.0);
        float Density   = max(0.0, WorldUniforms.CloudParams.DeckGeometry.w);
        if (Coverage > 0.01 && Density > 0.01)
        {
            float  TCloud = max(0.0, (DeckBaseM - WorldPos.z) / max(0.06, SunDir.z));
            float  TimeS  = WorldUniforms.CloudParams.OriginTimeVector.w;
            float2 WindV  = WorldUniforms.CloudParams.WindVectorOcclude.xy * TimeS;
            float3 CloudP = WorldPos + SunDir * TCloud - float3(WindV.x, WindV.y, 0.0);
            float  NVal   = SampleLocalCloudFieldGpu(CloudP * 0.0012, 1.0);
            float  CloudD = saturate((NVal - (1.0 - Coverage)) / max(0.15, Coverage)) * Density;
            Trans *= (0.18 + 0.82 * exp(-CloudD * 1.35));
        }
    }
    uint VolCnt = min(8u, uint(WorldUniforms.CloudParams.FormationOptics.w + 0.5));
    if (VolCnt > 0u)
    {
        float3 InvSun = 1.0 / select(abs(SunDir) > float3(1.0e-6), SunDir, float3(1.0e-6));
        for (uint VIdx = 0u; VIdx < VolCnt; ++VIdx)
        {
            float4 DenCov  = WorldUniforms.CloudParams.LocalClouds[VIdx].DensityCovScaleSides;
            if (DenCov.x <= 0.01 || DenCov.y <= 0.01) continue;
            float3 Center  = WorldUniforms.CloudParams.LocalClouds[VIdx].CenterShape.xyz;
            float3 HalfExt = WorldUniforms.CloudParams.LocalClouds[VIdx].HalfExtentYaw.xyz;
            float3 T0      = (Center - HalfExt - WorldPos) * InvSun;
            float3 T1      = (Center + HalfExt - WorldPos) * InvSun;
            float3 TN      = min(T0, T1);
            float3 TF      = max(T0, T1);
            float  TEn     = max(0.0, max(TN.x, max(TN.y, TN.z)));
            float  TEx     = min(TF.x, min(TF.y, TF.z));
            if (TEx > TEn)
            {
                float SegLen = TEx - TEn;
                float MidN   = SampleLocalCloudFieldGpu((WorldPos + SunDir * (0.5 * (TEn + TEx))) * max(0.01, DenCov.z), 1.0);
                float Den    = saturate((MidN - (1.0 - DenCov.y)) / max(0.15, DenCov.y)) * DenCov.x * 0.08;
                Trans *= (0.16 + 0.84 * exp(-Den * SegLen));
            }
        }
    }
    return clamp(Trans, 0.08, 1.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           WIDE TREE (BVH4) RAY INTERSECTION
//------------------------------------------------------------------------------------------------------------------------

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
    Res.LodLevel      = 0u;
    Res.ClusterOrdinal = 0u;

    uint EntryCount = uint(WorldUniforms.SkyRadianceBvhCount.w + 0.5);
    if (EntryCount == 0u) return Res;

    float3 SafeDir = select(abs(Dir) > float3(1.0e-8), Dir, float3(1.0e-8));
    float3 InvDir  = 1.0 / SafeDir;

    uint Stack[24];
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
                        Res.LodLevel      = uint(SceneTriangles[TriIdx].Corner0Pad.w + 0.5);
                        Res.ClusterOrdinal = uint(SceneTriangles[TriIdx].Edge01Pad.w + 0.5);
                    }
                }
            }
            else if (StackTop < 22)
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

    uint Stack[24];
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
            else if (StackTop < 22)
            {
                Stack[++StackTop] = Ref;
            }
        }
    }
    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//        PER-OBJECT LOCAL DISTANCE VOLUMES + 2-CASCADE GLOBAL DISTANCE CLIPMAP & 2-BOUNCE RADIOSITY PYRAMID
//------------------------------------------------------------------------------------------------------------------------

bool IsGlobalDistanceFieldReady()
{
    return ((WorldUniforms.GlobalDistanceBakeControl.z & 1u) != 0u) &&
           (WorldUniforms.GlobalDistanceFineCenterSpan.w > 0.05);
}

uint QueryObjectDistanceInstanceCount()
{
    return WorldUniforms.GlobalDistanceBakeControl.z >> 1u;
}

float3 UnpackOctNormal10x2(uint Packed)
{
    float2 F = float2(float(Packed & 0x3FFu), float((Packed >> 10u) & 0x3FFu)) * (2.0 / 1023.0) - 1.0;
    float3 N = float3(F.x, F.y, 1.0 - abs(F.x) - abs(F.y));
    if (N.z < 0.0)
    {
        float2 SignXY = select(N.xy >= float2(0.0), float2(1.0), float2(-1.0));
        N.xy = (float2(1.0) - abs(N.yx)) * SignXY;
    }
    return normalize(N);
}

float SampleObjectLocalVolume(
    uint       InstIdx,
    float3     WorldPos,
    out float3 OutWorldNormal,
    out uint   OutMaterialSlot,
    out float  OutVoxelSize,
    out bool   OutInsideBrick)
{
    GpuObjectDistanceInstance128 Inst = ObjectDistanceInstances[InstIdx];
    float3 Trans    = float3(Inst.WorldToLocalRow0.w, Inst.WorldToLocalRow1.w, Inst.WorldToLocalRow2.w);
    float  Scale    = max(1.0e-4, Inst.LocalMinScale.w);
    float  InvScale = Inst.LocalMaxInvScale.w;
    float3 Rel      = WorldPos - Trans;
    float3 LocalP   = float3(
        dot(Inst.WorldToLocalRow0.xyz, Rel),
        dot(Inst.WorldToLocalRow1.xyz, Rel),
        dot(Inst.WorldToLocalRow2.xyz, Rel)) * InvScale;

    float3 LMin     = Inst.LocalMinScale.xyz;
    float3 LMax     = Inst.LocalMaxInvScale.xyz;
    float3 ClampedP = clamp(LocalP, LMin, LMax);
    float3 OutDelta = LocalP - ClampedP;
    float  OutDist  = length(OutDelta);
    OutInsideBrick  = (OutDist <= 1.0e-4);

    uint   BaseTex  = Inst.BrickDims.x;
    uint   RX       = max(1u, Inst.BrickDims.y);
    uint   RY       = max(1u, Inst.BrickDims.z);
    uint   RZ       = max(1u, Inst.BrickDims.w);
    uint   RXY      = RX * RY;

    float3 GridF = (ClampedP - LMin) * Inst.InvSpacingPad.xyz - 0.5;
    int3   P0    = clamp(int3(floor(GridF)), int3(0), int3(int(RX) - 1, int(RY) - 1, int(RZ) - 1));
    int3   P1    = min(P0 + int3(1), int3(int(RX) - 1, int(RY) - 1, int(RZ) - 1));
    float3 Fr    = saturate(GridF - float3(P0));

    float D000 = asfloat(LocalDistanceBricks[BaseTex + uint(P0.z) * RXY + uint(P0.y) * RX + uint(P0.x)].x);
    float D100 = asfloat(LocalDistanceBricks[BaseTex + uint(P0.z) * RXY + uint(P0.y) * RX + uint(P1.x)].x);
    float D010 = asfloat(LocalDistanceBricks[BaseTex + uint(P0.z) * RXY + uint(P1.y) * RX + uint(P0.x)].x);
    float D110 = asfloat(LocalDistanceBricks[BaseTex + uint(P0.z) * RXY + uint(P1.y) * RX + uint(P1.x)].x);
    float D001 = asfloat(LocalDistanceBricks[BaseTex + uint(P1.z) * RXY + uint(P0.y) * RX + uint(P0.x)].x);
    float D101 = asfloat(LocalDistanceBricks[BaseTex + uint(P1.z) * RXY + uint(P0.y) * RX + uint(P1.x)].x);
    float D011 = asfloat(LocalDistanceBricks[BaseTex + uint(P1.z) * RXY + uint(P1.y) * RX + uint(P0.x)].x);
    float D111 = asfloat(LocalDistanceBricks[BaseTex + uint(P1.z) * RXY + uint(P1.y) * RX + uint(P1.x)].x);

    float D00 = lerp(D000, D100, Fr.x);
    float D10 = lerp(D010, D110, Fr.x);
    float D01 = lerp(D001, D101, Fr.x);
    float D11 = lerp(D011, D111, Fr.x);
    float LocalDist = lerp(lerp(D00, D10, Fr.y), lerp(D01, D11, Fr.y), Fr.z) + OutDist;

    int3 NearIdx = clamp(int3(round(GridF)), int3(0), int3(int(RX) - 1, int(RY) - 1, int(RZ) - 1));
    uint PackedNM = LocalDistanceBricks[BaseTex + uint(NearIdx.z) * RXY + uint(NearIdx.y) * RX + uint(NearIdx.x)].y;
    float3 LocalN = (OutDist > 0.05) ? normalize(OutDelta) : UnpackOctNormal10x2(PackedNM);
    OutWorldNormal = normalize(float3(
        Inst.WorldToLocalRow0.x * LocalN.x + Inst.WorldToLocalRow1.x * LocalN.y + Inst.WorldToLocalRow2.x * LocalN.z,
        Inst.WorldToLocalRow0.y * LocalN.x + Inst.WorldToLocalRow1.y * LocalN.y + Inst.WorldToLocalRow2.y * LocalN.z,
        Inst.WorldToLocalRow0.z * LocalN.x + Inst.WorldToLocalRow1.z * LocalN.y + Inst.WorldToLocalRow2.z * LocalN.z));
    uint VoxelMat   = (PackedNM >> 20u) & 0xFFFu;
    OutMaterialSlot = (Inst.MaterialMeta.x != 0xFFFFu) ? Inst.MaterialMeta.x : VoxelMat;
    OutVoxelSize    = Inst.InvSpacingPad.w * Scale;
    return LocalDist * Scale;
}

float EvaluateObjectDistanceInstances(
    float3     WorldPos,
    out float3 OutWorldNormal,
    out uint   OutMaterialSlot,
    out float  OutVoxelSize,
    out bool   OutInsideAnyBrick)
{
    uint Count        = QueryObjectDistanceInstanceCount();
    float BestDist    = 1.0e6;
    OutWorldNormal    = float3(0.0, 0.0, 1.0);
    OutMaterialSlot   = 0u;
    OutVoxelSize      = 0.10;
    OutInsideAnyBrick = false;
    for (uint I = 0u; I < Count; ++I)
    {
        float3 Nrm;
        uint   Mat;
        float  Vox;
        bool   InBrick;
        float  D = SampleObjectLocalVolume(I, WorldPos, Nrm, Mat, Vox, InBrick);
        if (InBrick) OutInsideAnyBrick = true;
        if (D < BestDist)
        {
            BestDist        = D;
            OutWorldNormal  = Nrm;
            OutMaterialSlot = Mat;
            OutVoxelSize    = Vox;
        }
    }
    return BestDist;
}

float QueryFineGdfVoxelSize()
{
    return (2.0 * max(0.25, WorldUniforms.GlobalDistanceFineCenterSpan.w)) / float(GdfGridRes);
}

float SampleGlobalDistanceCascade(uint CascadeIdx, float3 WorldPos, out bool OutInBounds)
{
    float4 CenterSpan = (CascadeIdx == 0u)
        ? WorldUniforms.GlobalDistanceFineCenterSpan
        : WorldUniforms.GlobalDistanceCoarseCenterSpan;
    float HalfSpan = CenterSpan.w;
    if (HalfSpan <= 0.01)
    {
        OutInBounds = false;
        return 10.0;
    }

    float3 NormCoord = (WorldPos - CenterSpan.xyz) / (2.0 * HalfSpan) + 0.5;
    float  Margin    = 1.25 / float(GdfGridRes);
    if (any(NormCoord < float3(Margin)) || any(NormCoord > float3(1.0 - Margin)))
    {
        OutInBounds = false;
        return HalfSpan;
    }
    OutInBounds = true;

    float3 GridF = NormCoord * float(GdfGridRes) - 0.5;
    int3   P0    = clamp(int3(floor(GridF)), int3(0), int3(int(GdfGridRes) - 1));
    int3   P1    = min(P0 + int3(1), int3(int(GdfGridRes) - 1));
    float3 Fr    = saturate(GridF - float3(P0));

    uint BaseOff = CascadeIdx * GdfVoxCnt;
    uint R       = GdfGridRes;
    uint R2      = GdfGridRes * GdfGridRes;

    float C000 = GlobalDistanceFieldBuffer[BaseOff + uint(P0.z) * R2 + uint(P0.y) * R + uint(P0.x)];
    float C100 = GlobalDistanceFieldBuffer[BaseOff + uint(P0.z) * R2 + uint(P0.y) * R + uint(P1.x)];
    float C010 = GlobalDistanceFieldBuffer[BaseOff + uint(P0.z) * R2 + uint(P1.y) * R + uint(P0.x)];
    float C110 = GlobalDistanceFieldBuffer[BaseOff + uint(P0.z) * R2 + uint(P1.y) * R + uint(P1.x)];
    float C001 = GlobalDistanceFieldBuffer[BaseOff + uint(P1.z) * R2 + uint(P0.y) * R + uint(P0.x)];
    float C101 = GlobalDistanceFieldBuffer[BaseOff + uint(P1.z) * R2 + uint(P0.y) * R + uint(P1.x)];
    float C011 = GlobalDistanceFieldBuffer[BaseOff + uint(P1.z) * R2 + uint(P1.y) * R + uint(P0.x)];
    float C111 = GlobalDistanceFieldBuffer[BaseOff + uint(P1.z) * R2 + uint(P1.y) * R + uint(P1.x)];

    float C00 = lerp(C000, C100, Fr.x);
    float C10 = lerp(C010, C110, Fr.x);
    float C01 = lerp(C001, C101, Fr.x);
    float C11 = lerp(C011, C111, Fr.x);
    return lerp(lerp(C00, C10, Fr.y), lerp(C01, C11, Fr.y), Fr.z);
}

float EvaluateObjectDistanceScalarFast(float3 WorldPos, out float OutVoxelSize)
{
    uint  Count    = QueryObjectDistanceInstanceCount();
    float BestDist = 1.0e6;
    OutVoxelSize   = 0.10;
    for (uint I = 0u; I < Count; ++I)
    {
        GpuObjectDistanceInstance128 Inst = ObjectDistanceInstances[I];
        float3 Trans    = float3(Inst.WorldToLocalRow0.w, Inst.WorldToLocalRow1.w, Inst.WorldToLocalRow2.w);
        float  Scale    = max(1.0e-4, Inst.LocalMinScale.w);
        float  InvScale = Inst.LocalMaxInvScale.w;
        float3 Rel      = WorldPos - Trans;
        float3 LocalP   = float3(
            dot(Inst.WorldToLocalRow0.xyz, Rel),
            dot(Inst.WorldToLocalRow1.xyz, Rel),
            dot(Inst.WorldToLocalRow2.xyz, Rel)) * InvScale;

        float3 LMin     = Inst.LocalMinScale.xyz;
        float3 LMax     = Inst.LocalMaxInvScale.xyz;
        float3 ClampedP = clamp(LocalP, LMin, LMax);
        float  OutDist  = length(LocalP - ClampedP);
        if (OutDist * Scale >= BestDist)
        {
            continue;
        }

        uint   BaseTex  = Inst.BrickDims.x;
        uint   RX       = max(1u, Inst.BrickDims.y);
        uint   RY       = max(1u, Inst.BrickDims.z);
        uint   RZ       = max(1u, Inst.BrickDims.w);
        uint   RXY      = RX * RY;

        float3 GridF = (ClampedP - LMin) * Inst.InvSpacingPad.xyz - 0.5;
        int3   P0    = clamp(int3(floor(GridF)), int3(0), int3(int(RX) - 1, int(RY) - 1, int(RZ) - 1));
        int3   P1    = min(P0 + int3(1), int3(int(RX) - 1, int(RY) - 1, int(RZ) - 1));
        float3 Fr    = saturate(GridF - float3(P0));

        float D000 = asfloat(LocalDistanceBricks[BaseTex + uint(P0.z) * RXY + uint(P0.y) * RX + uint(P0.x)].x);
        float D100 = asfloat(LocalDistanceBricks[BaseTex + uint(P0.z) * RXY + uint(P0.y) * RX + uint(P1.x)].x);
        float D010 = asfloat(LocalDistanceBricks[BaseTex + uint(P0.z) * RXY + uint(P1.y) * RX + uint(P0.x)].x);
        float D110 = asfloat(LocalDistanceBricks[BaseTex + uint(P0.z) * RXY + uint(P1.y) * RX + uint(P1.x)].x);
        float D001 = asfloat(LocalDistanceBricks[BaseTex + uint(P1.z) * RXY + uint(P0.y) * RX + uint(P0.x)].x);
        float D101 = asfloat(LocalDistanceBricks[BaseTex + uint(P1.z) * RXY + uint(P0.y) * RX + uint(P1.x)].x);
        float D011 = asfloat(LocalDistanceBricks[BaseTex + uint(P1.z) * RXY + uint(P1.y) * RX + uint(P0.x)].x);
        float D111 = asfloat(LocalDistanceBricks[BaseTex + uint(P1.z) * RXY + uint(P1.y) * RX + uint(P1.x)].x);

        float D00 = lerp(D000, D100, Fr.x);
        float D10 = lerp(D010, D110, Fr.x);
        float D01 = lerp(D001, D101, Fr.x);
        float D11 = lerp(D011, D111, Fr.x);
        float D   = (lerp(lerp(D00, D10, Fr.y), lerp(D01, D11, Fr.y), Fr.z) + OutDist) * Scale;
        if (D < BestDist)
        {
            BestDist     = D;
            OutVoxelSize = Inst.InvSpacingPad.w * Scale;
        }
    }
    return BestDist;
}

float SampleGlobalDistanceWorld(float3 WorldPos, out float OutLocalVoxelSize)
{
    uint InstCount = QueryObjectDistanceInstanceCount();
    if (InstCount > 0u)
    {
        float ObjVox  = 0.10;
        float ObjDist = EvaluateObjectDistanceScalarFast(WorldPos, ObjVox);
        OutLocalVoxelSize = clamp(ObjVox, 0.025, 0.35);
        return ObjDist;
    }

    bool InFine = false;
    float FineD = SampleGlobalDistanceCascade(0u, WorldPos, InFine);
    if (InFine)
    {
        float3 Rel      = abs(WorldPos - WorldUniforms.GlobalDistanceFineCenterSpan.xyz) /
                          max(0.1, WorldUniforms.GlobalDistanceFineCenterSpan.w);
        float  EdgeFade = smoothstep(0.78, 0.96, max(Rel.x, max(Rel.y, Rel.z)));
        OutLocalVoxelSize = QueryFineGdfVoxelSize();
        if (EdgeFade > 0.001)
        {
            bool  InCoarse = false;
            float CoarseD  = SampleGlobalDistanceCascade(1u, WorldPos, InCoarse);
            if (InCoarse)
            {
                float CoarseVox = (2.0 * max(0.5, WorldUniforms.GlobalDistanceCoarseCenterSpan.w)) / float(GdfGridRes);
                OutLocalVoxelSize = lerp(OutLocalVoxelSize, CoarseVox, EdgeFade);
                return lerp(FineD, CoarseD, EdgeFade);
            }
        }
        return FineD;
    }

    bool  InCoarse = false;
    float CoarseD  = SampleGlobalDistanceCascade(1u, WorldPos, InCoarse);
    OutLocalVoxelSize = (2.0 * max(0.5, WorldUniforms.GlobalDistanceCoarseCenterSpan.w)) / float(GdfGridRes);
    return InCoarse ? CoarseD : OutLocalVoxelSize * 8.0;
}

float TraceGlobalDistanceConeVisibility(
    float3 Origin,
    float3 GeomN,
    float3 Dir,
    float  MaxDist,
    float  ConeTan,
    bool   PointHard,
    uint   MaxSteps)
{
    if ((!IsGlobalDistanceFieldReady() && QueryObjectDistanceInstanceCount() == 0u) || MaxDist <= 0.12)
    {
        return 1.0;
    }

    float ProbeVox = 0.08;
    (void)SampleGlobalDistanceWorld(Origin, ProbeVox);
    float  Vox0      = clamp(ProbeVox, 0.03, 0.22);
    float  StartLift = Vox0 * 0.72;
    float3 StartP    = Origin + GeomN * StartLift;
    float  NdotD     = max(0.0, dot(GeomN, Dir));
    float  EndMargin = max(0.10, Vox0 * 1.85);
    float  SafeMax   = MaxDist - EndMargin;
    float  T         = Vox0 * 0.65;
    if (SafeMax <= T + 0.02)
    {
        return 1.0;
    }

    float MinVis = 1.0;
    [loop]
    for (uint Step = 0u; Step < MaxSteps; ++Step)
    {
        if (T >= SafeMax) break;
        float LocalVox = Vox0;
        float Dist     = SampleGlobalDistanceWorld(StartP + Dir * T, LocalVox);

        float SelfPlaneDist  = StartLift + T * NdotD;
        bool  IsReceiverSelf = (Dist >= SelfPlaneDist - LocalVox * 0.45);

        if (PointHard)
        {
            if (!IsReceiverSelf)
            {
                if (Dist < LocalVox * 0.12) return 0.0;
                float HardBand = saturate((Dist - LocalVox * 0.10) / max(1.0e-3, LocalVox * 0.40));
                MinVis = min(MinVis, HardBand);
            }
            T += max(LocalVox * 0.55, max(0.0, Dist) * 0.88);
        }
        else
        {
            float RemSpan  = max(1.0e-3, SafeMax - T);
            float Aperture = max(LocalVox * 0.30, min(T * max(0.025, ConeTan), RemSpan * 0.55));
            if (!IsReceiverSelf)
            {
                float ConeVis = saturate(Dist / max(1.0e-3, Aperture));
                MinVis = min(MinVis, ConeVis);
                if (MinVis <= 0.01) return 0.0;
            }
            T += max(LocalVox * 0.58, max(max(0.0, Dist) * 0.82, Aperture * 0.48));
        }
    }
    return PointHard ? smoothstep(0.25, 0.75, MinVis) : (MinVis * MinVis * (3.0 - 2.0 * MinVis));
}

// Clean 9-cone Vogel-spiral hemisphere ambient occlusion over unsigned Local/Global SDF bricks:
// 1 zenith cone + 8 golden-angle cones across two elevation rings, completely free of interior-sign blotches.
float EvaluateGlobalDistanceAmbientOcclusion(float3 Pos, float3 N, float3 GeomN)
{
    if (!IsGlobalDistanceFieldReady() && QueryObjectDistanceInstanceCount() == 0u) return 1.0;
    float ProbeVox = 0.08;
    (void)SampleGlobalDistanceWorld(Pos, ProbeVox);
    float Vox0   = clamp(ProbeVox, 0.035, 0.18);
    float Radius = clamp(Vox0 * 18.0, 0.55, 2.20);

    float3 TangentAxis = (abs(N.z) < 0.92) ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
    float3 Tangent     = normalize(cross(TangentAxis, N));
    float3 Bitangent   = cross(N, Tangent);

    float VisSum    = TraceGlobalDistanceConeVisibility(Pos, GeomN, N, Radius, 0.45, false, 8u);
    float WeightSum = 1.0;
    for (uint ConeIdx = 0u; ConeIdx < 8u; ++ConeIdx)
    {
        float  Frac    = (float(ConeIdx) + 0.5) / 8.0;
        float  CosElev = sqrt(1.0 - Frac * 0.82);
        float  SinElev = sqrt(max(0.0, 1.0 - CosElev * CosElev));
        float  Phi     = float(ConeIdx) * 2.39996323;
        float3 ConeDir = normalize(N * CosElev + (Tangent * cos(Phi) + Bitangent * sin(Phi)) * SinElev);
        if (dot(ConeDir, GeomN) <= 0.08) continue;
        VisSum    += CosElev * TraceGlobalDistanceConeVisibility(Pos, GeomN, ConeDir, Radius, 0.52, false, 8u);
        WeightSum += CosElev;
    }
    float Ao = VisSum / max(1.0e-4, WeightSum);
    return clamp(pow(saturate(Ao), 0.92), 0.08, 1.0);
}

// Central-difference gradient of the combined distance field: smooth SDF surface normal at any world point.
float3 EvaluateGlobalDistanceGradientNormal(float3 Pos, float3 FallbackNormal)
{
    if (!IsGlobalDistanceFieldReady() && QueryObjectDistanceInstanceCount() == 0u) return FallbackNormal;
    float ProbeVox = 0.06;
    (void)SampleGlobalDistanceWorld(Pos, ProbeVox);
    float Eps = clamp(ProbeVox * 0.65, 0.015, 0.14);
    float LocalVox = Eps;
    float Dx = SampleGlobalDistanceWorld(Pos + float3(Eps, 0.0, 0.0), LocalVox) -
               SampleGlobalDistanceWorld(Pos - float3(Eps, 0.0, 0.0), LocalVox);
    float Dy = SampleGlobalDistanceWorld(Pos + float3(0.0, Eps, 0.0), LocalVox) -
               SampleGlobalDistanceWorld(Pos - float3(0.0, Eps, 0.0), LocalVox);
    float Dz = SampleGlobalDistanceWorld(Pos + float3(0.0, 0.0, Eps), LocalVox) -
               SampleGlobalDistanceWorld(Pos - float3(0.0, 0.0, Eps), LocalVox);
    float3 Gradient = float3(Dx, Dy, Dz);
    float  GradLen  = length(Gradient);
    return (GradLen > 1.0e-6) ? (Gradient / GradLen) : FallbackNormal;
}

// 0 = fine per-object brick, 1 = fine global cascade, 2 = coarse global cascade, 3 = outer field
uint QueryGlobalDistanceCascadeIndex(float3 Pos)
{
    if (QueryObjectDistanceInstanceCount() > 0u)
    {
        float3 ObjN;
        uint   ObjMat;
        float  ObjVox;
        bool   InBrick = false;
        (void)EvaluateObjectDistanceInstances(Pos, ObjN, ObjMat, ObjVox, InBrick);
        if (InBrick) return 0u;
    }
    bool InFine = false;
    (void)SampleGlobalDistanceCascade(0u, Pos, InFine);
    if (InFine) return 1u;
    bool InCoarse = false;
    (void)SampleGlobalDistanceCascade(1u, Pos, InCoarse);
    return InCoarse ? 2u : 3u;
}

bool SphereTraceDistanceField(
    float3     RayOrigin,
    float3     RayDir,
    float      MaxDist,
    out float  OutHitDist,
    out float3 OutHitPos,
    out float3 OutHitNormal,
    out uint   OutMaterialSlot)
{
    OutHitDist      = MaxDist;
    OutHitPos       = RayOrigin + RayDir * MaxDist;
    OutHitNormal    = -RayDir;
    OutMaterialSlot = 0u;

    if (!IsGlobalDistanceFieldReady() && QueryObjectDistanceInstanceCount() == 0u)
    {
        return false;
    }

    float T = 0.02;
    [loop]
    for (uint Step = 0u; Step < 96u; ++Step)
    {
        if (T >= MaxDist) break;
        float3 P       = RayOrigin + RayDir * T;
        float  VoxSize = 0.08;
        float  Dist    = SampleGlobalDistanceWorld(P, VoxSize);
        float  HitEps  = clamp(VoxSize * 0.36, 0.008, 0.055);
        if (Dist <= HitEps)
        {
            OutHitDist = T;
            OutHitPos  = P;
            if (QueryObjectDistanceInstanceCount() > 0u)
            {
                float3 BrickN;
                uint   BrickMat;
                float  BrickVox;
                bool   InBrick;
                (void)EvaluateObjectDistanceInstances(P, BrickN, BrickMat, BrickVox, InBrick);
                OutMaterialSlot = BrickMat;
                OutHitNormal    = EvaluateGlobalDistanceGradientNormal(P, BrickN);
            }
            else
            {
                OutHitNormal = EvaluateGlobalDistanceGradientNormal(P, -RayDir);
            }
            return true;
        }
        T += max(0.012, Dist * 0.90);
    }
    return false;
}

float3 EvaluateSdfDistanceHeatColor(float SignedDistanceMeters, float VoxelMeters)
{
    float T = clamp(SignedDistanceMeters / max(1.0e-3, VoxelMeters * 10.0), -1.0, 1.0);
    float3 InsideColor  = float3(0.85, 0.15, 0.10);
    float3 SurfaceColor = float3(0.15, 0.95, 0.35);
    float3 OutsideColor = float3(0.10, 0.30, 0.95);
    return (T < 0.0) ? lerp(SurfaceColor, InsideColor, -T) : lerp(SurfaceColor, OutsideColor, T);
}

void QueryRadiosityMipLayout(uint MipIdx, out uint OutRes, out uint OutOffset)
{
    if (MipIdx == 0u)      { OutRes = 64u; OutOffset = RadMip0Off; }
    else if (MipIdx == 1u) { OutRes = 32u; OutOffset = RadMip1Off; }
    else if (MipIdx == 2u) { OutRes = 16u; OutOffset = RadMip2Off; }
    else                   { OutRes = 8u;  OutOffset = RadMip3Off; }
}

float4 SampleRadiosityMipTrilinear(uint MipIdx, float3 WorldPos, out bool OutInBounds)
{
    float4 CenterSpan = WorldUniforms.GlobalDistanceFineCenterSpan;
    float  HalfSpan   = CenterSpan.w;
    if (HalfSpan <= 0.01)
    {
        OutInBounds = false;
        return float4(0.0);
    }

    float3 NormCoord = (WorldPos - CenterSpan.xyz) / (2.0 * HalfSpan) + 0.5;
    if (any(NormCoord <= float3(0.005)) || any(NormCoord >= float3(0.995)))
    {
        OutInBounds = false;
        return float4(0.0);
    }
    OutInBounds = true;

    uint Res = 64u, BaseOff = 0u;
    QueryRadiosityMipLayout(MipIdx, Res, BaseOff);

    float3 GridF = NormCoord * float(Res) - 0.5;
    int3   P0    = clamp(int3(floor(GridF)), int3(0), int3(int(Res) - 1));
    int3   P1    = min(P0 + int3(1), int3(int(Res) - 1));
    float3 Fr    = saturate(GridF - float3(P0));
    uint   R2    = Res * Res;

    float4 V000 = UnpackHalf4(GlobalRadiosityFieldBuffer[BaseOff + uint(P0.z) * R2 + uint(P0.y) * Res + uint(P0.x)]);
    float4 V100 = UnpackHalf4(GlobalRadiosityFieldBuffer[BaseOff + uint(P0.z) * R2 + uint(P0.y) * Res + uint(P1.x)]);
    float4 V010 = UnpackHalf4(GlobalRadiosityFieldBuffer[BaseOff + uint(P0.z) * R2 + uint(P1.y) * Res + uint(P0.x)]);
    float4 V110 = UnpackHalf4(GlobalRadiosityFieldBuffer[BaseOff + uint(P0.z) * R2 + uint(P1.y) * Res + uint(P1.x)]);
    float4 V001 = UnpackHalf4(GlobalRadiosityFieldBuffer[BaseOff + uint(P1.z) * R2 + uint(P0.y) * Res + uint(P0.x)]);
    float4 V101 = UnpackHalf4(GlobalRadiosityFieldBuffer[BaseOff + uint(P1.z) * R2 + uint(P0.y) * Res + uint(P1.x)]);
    float4 V011 = UnpackHalf4(GlobalRadiosityFieldBuffer[BaseOff + uint(P1.z) * R2 + uint(P1.y) * Res + uint(P0.x)]);
    float4 V111 = UnpackHalf4(GlobalRadiosityFieldBuffer[BaseOff + uint(P1.z) * R2 + uint(P1.y) * Res + uint(P1.x)]);

    float4 V00 = lerp(V000, V100, Fr.x);
    float4 V10 = lerp(V010, V110, Fr.x);
    float4 V01 = lerp(V001, V101, Fr.x);
    float4 V11 = lerp(V011, V111, Fr.x);
    return lerp(lerp(V00, V10, Fr.y), lerp(V01, V11, Fr.y), Fr.z);
}

float4 SampleRadiosityPyramidMip(float3 WorldPos, float MipLevel, out bool OutInBounds)
{
    float ClampedMip = clamp(MipLevel, 0.0, 3.0);
    uint  Mip0       = uint(floor(ClampedMip));
    uint  Mip1       = min(3u, Mip0 + 1u);
    float FracMip    = ClampedMip - float(Mip0);

    bool   In0 = false;
    float4 V0  = SampleRadiosityMipTrilinear(Mip0, WorldPos, In0);
    if (!In0 || Mip0 == Mip1 || FracMip <= 1.0e-3)
    {
        OutInBounds = In0;
        return V0;
    }
    bool   In1 = false;
    float4 V1  = SampleRadiosityMipTrilinear(Mip1, WorldPos, In1);
    OutInBounds = In0 && In1;
    return In1 ? lerp(V0, V1, FracMip) : V0;
}

float3 TraceSingleRadiosityCone(float3 StartP, float3 ConeDir, float TanHalfAngle, uint MaxSteps)
{
    float  Vox0     = QueryFineGdfVoxelSize();
    float  MaxTrace = max(1.2, WorldUniforms.GlobalDistanceFineCenterSpan.w * 2.15);
    float  T        = Vox0 * 1.10;
    float3 AccumRad = float3(0.0);
    float  AccumOcc = 0.0;

    [loop]
    for (uint S = 0u; S < MaxSteps; ++S)
    {
        if (T >= MaxTrace || AccumOcc >= 0.96) break;
        float3 SampleP  = StartP + ConeDir * T;
        float  Diameter = max(Vox0, 2.0 * TanHalfAngle * T);
        uint   MipIdx   = min(3u, uint(clamp(log2(max(1.0, Diameter / Vox0)) + 0.45, 0.0, 3.0)));
        bool   InBounds = false;
        float4 VoxPrem  = SampleRadiosityMipTrilinear(MipIdx, SampleP, InBounds);
        if (!InBounds)
        {
            break;
        }
        float Trans = 1.0 - AccumOcc;
        AccumRad   += Trans * VoxPrem.rgb;
        AccumOcc    = saturate(AccumOcc + Trans * saturate(VoxPrem.a));

        float SdfD    = SampleGlobalDistanceCascade(0u, SampleP, InBounds);
        float StepAdv = max(Diameter * 0.72, InBounds ? max(Vox0 * 0.85, SdfD * 0.62) : Diameter);
        T += StepAdv;
    }

    if (AccumOcc < 0.99)
    {
        AccumRad += (1.0 - AccumOcc) * EvaluateSkyDome(ConeDir) * 0.34;
    }
    return AccumRad;
}

float3 EvaluateGdfVoxelConeGi(float3 Pos, float3 N, float3 GeomN, float3 TAxis, float3 BAxis)
{
    if (!IsGlobalDistanceFieldReady())
    {
        return EvaluateSkyDome(N) * 0.25;
    }

    float  Vox0   = QueryFineGdfVoxelSize();
    float3 StartP = Pos + GeomN * (Vox0 * 0.88) + N * (Vox0 * 0.35);

    const float CosTilt = 0.6427876; // cos(50 deg)
    const float SinTilt = 0.7660444; // sin(50 deg)
    float3 GiIrr = float3(0.0);

    [loop]
    for (uint ConeIdx = 0u; ConeIdx < 5u; ++ConeIdx)
    {
        float3 ConeDir = N;
        float  ConeWt  = 0.28;
        uint   Steps   = 6u;
        if (ConeIdx == 1u)      { ConeDir = normalize(N * CosTilt + TAxis * SinTilt); ConeWt = 0.18; Steps = 5u; }
        else if (ConeIdx == 2u) { ConeDir = normalize(N * CosTilt - TAxis * SinTilt); ConeWt = 0.18; Steps = 5u; }
        else if (ConeIdx == 3u) { ConeDir = normalize(N * CosTilt + BAxis * SinTilt); ConeWt = 0.18; Steps = 5u; }
        else if (ConeIdx == 4u) { ConeDir = normalize(N * CosTilt - BAxis * SinTilt); ConeWt = 0.18; Steps = 5u; }
        GiIrr += ConeWt * TraceSingleRadiosityCone(StartP, ConeDir, 0.577, Steps);
    }
    return max(float3(0.0), GiIrr);
}

float3 EvaluateGdfGlossyCone(float3 Pos, float3 GeomN, float3 ReflDir, float Roughness)
{
    if (!IsGlobalDistanceFieldReady())
    {
        return EvaluateSkyDome(ReflDir);
    }
    float  Vox0    = QueryFineGdfVoxelSize();
    float3 StartP  = Pos + GeomN * (Vox0 * 0.90);
    float  ConeTan = clamp(Roughness * Roughness * 0.85 + 0.045, 0.045, 0.65);
    return TraceSingleRadiosityCone(StartP, ReflDir, ConeTan, 6u);
}

//------------------------------------------------------------------------------------------------------------------------
//                                      OPENPBR BRDF & LINEAR COSINE AREA LIGHTS
//------------------------------------------------------------------------------------------------------------------------

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

uint HashPositionSeed(float3 Pos, uint Salt)
{
    int3 Quant = int3(floor(Pos * 2048.0));
    uint Seed  = (uint(Quant.x) * 73856093u) ^ (uint(Quant.y) * 19349663u) ^ (uint(Quant.z) * 83492791u) ^ (Salt * 2654435761u);
    return Seed | 1u;
}

float3 EvaluateAreaQuadBrdfAnalytic(GpuAreaQuadLight Quad, float3 Pos, float3 N, float3 V, GpuWorldMaterial80 Mat)
{
    float3 RightT = Quad.RightHalfHeight.xyz;
    float3 UpB    = Quad.UpTwoSided.xyz;
    float3 Center = Quad.CenterHalfWidth.xyz;
    float3 HalfU  = RightT * Quad.CenterHalfWidth.w;
    float3 HalfV  = UpB * Quad.RightHalfHeight.w;

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
    return Quad.RadiancePacked.xyz * (DiffContrib + SpecContrib);
}

float3 EvaluateAreaQuadLightGdfFast(GpuAreaQuadLight Quad, float3 Pos, float3 GeomN, float3 N, float3 V, GpuWorldMaterial80 Mat)
{
    float3 RightT = Quad.RightHalfHeight.xyz;
    float3 UpB    = Quad.UpTwoSided.xyz;
    float3 QuadN  = normalize(cross(RightT, UpB));
    float3 Center = Quad.CenterHalfWidth.xyz;
    if (Quad.UpTwoSided.w < 0.5 && dot(Pos - Center, QuadN) <= 1.0e-4)
    {
        return float3(0.0);
    }

    float3 ToL  = Center - Pos;
    float  Dist = length(ToL);
    if (Dist <= 1.0e-4) return float3(0.0);
    float3 DirL = ToL / Dist;
    if (dot(N, DirL) <= -0.15) return float3(0.0);

    float VisFactor = 1.0;
    if (IsGlobalDistanceFieldReady())
    {
        float Radius = max(0.04, 0.65 * (Quad.CenterHalfWidth.w + Quad.RightHalfHeight.w));
        VisFactor    = TraceGlobalDistanceConeVisibility(Pos, GeomN, DirL, Dist, Radius / Dist, false, 6u);
    }
    else if (TraceSceneAnyHit(Pos + GeomN * 0.015, DirL, 0.03, max(0.04, Dist - 0.04)))
    {
        VisFactor = 0.0;
    }

    if (VisFactor <= 1.0e-4) return float3(0.0);
    return EvaluateAreaQuadBrdfAnalytic(Quad, Pos, N, V, Mat) * VisFactor;
}

float3 EvaluateAreaQuadLight(GpuAreaQuadLight Quad, float3 Pos, float3 GeomN, float3 N, float3 V, GpuWorldMaterial80 Mat, uint RngSalt)
{
    float3 RightT = Quad.RightHalfHeight.xyz;
    float3 UpB    = Quad.UpTwoSided.xyz;
    float3 QuadN  = normalize(cross(RightT, UpB));
    float3 Center = Quad.CenterHalfWidth.xyz;
    if (Quad.UpTwoSided.w < 0.5 && dot(Pos - Center, QuadN) <= 1.0e-4)
    {
        return float3(0.0);
    }

    float3 HalfU        = RightT * Quad.CenterHalfWidth.w;
    float3 HalfV        = UpB * Quad.RightHalfHeight.w;
    uint   EngineMode   = WorldUniforms.EngineShadowReflPad.x;
    uint   PenumbraMode = (WorldUniforms.AtmosphereMaskPad.w >> 16u) & 0x7u;
    bool   UseGdf       = (EngineMode <= 2u) && IsGlobalDistanceFieldReady();
    float  VisFactor    = 0.0;

    float3 ToCenter   = Center - Pos;
    float  CenterDist = length(ToCenter);
    if (CenterDist <= 1.0e-4) return float3(0.0);
    float3 CenterDir  = ToCenter / CenterDist;

    if (UseGdf && PenumbraMode <= 3u)
    {
        float EffRadius = max(0.035, 0.72 * (Quad.CenterHalfWidth.w + Quad.RightHalfHeight.w));
        float ConeTan   = clamp(EffRadius / CenterDist, 0.025, 0.65);
        if (PenumbraMode == 1u) ConeTan *= 0.85;
        VisFactor = TraceGlobalDistanceConeVisibility(Pos, GeomN, CenterDir, CenterDist, ConeTan, PenumbraMode == 0u, 10u);
    }
    else if (PenumbraMode == 0u)
    {
        float3 RayOrig = Pos + GeomN * 0.012;
        if (dot(N, CenterDir) > 0.0 && !TraceSceneAnyHit(RayOrig, CenterDir, 0.028, max(0.040, CenterDist - 0.035)))
        {
            VisFactor = 1.0;
        }
    }
    else
    {
        // Hybrid GDF pre-classification on Mode 4 skips BVH4 rays on fully lit & deep umbra pixels!
        if (UseGdf && PenumbraMode == 4u)
        {
            float EffRadius = max(0.035, 0.78 * (Quad.CenterHalfWidth.w + Quad.RightHalfHeight.w));
            float GdfVis    = TraceGlobalDistanceConeVisibility(Pos, GeomN, CenterDir, CenterDist, EffRadius / CenterDist, false, 7u);
            if (GdfVis >= 0.985)
            {
                return EvaluateAreaQuadBrdfAnalytic(Quad, Pos, N, V, Mat);
            }
            if (GdfVis <= 0.015)
            {
                return float3(0.0);
            }
        }

        uint   TapCount = (PenumbraMode >= 5u) ? 6u : 4u;
        uint   Rng      = HashPositionSeed(Pos, RngSalt + (WorldUniforms.DenoiseStepParams.w & 63u) * 199u);
        float  RotAng   = PcgFloat01(Rng) * TauConst;
        float  RadJit   = PcgFloat01(Rng);
        float3 RayOrig  = Pos + GeomN * 0.012;
        uint   VisCnt   = 0u;
        for (uint S = 0u; S < TapCount; ++S)
        {
            float  Rad    = sqrt((float(S) + 0.15 + 0.70 * RadJit) / float(TapCount)) * 0.84;
            float  Ang    = float(S) * 2.39996323 + RotAng;
            float3 Target = Center + HalfU * (Rad * cos(Ang)) + HalfV * (Rad * sin(Ang));
            float3 ToL    = Target - RayOrig;
            float  Dist   = length(ToL);
            if (Dist <= 1.0e-4) continue;
            float3 DirL   = ToL / Dist;
            if (dot(N, DirL) <= 0.0) continue;
            if (!TraceSceneAnyHit(RayOrig, DirL, 0.028, max(0.040, Dist - 0.035)))
            {
                ++VisCnt;
            }
        }
        VisFactor = float(VisCnt) / float(TapCount);
    }

    if (VisFactor <= 1.0e-4) return float3(0.0);
    return EvaluateAreaQuadBrdfAnalytic(Quad, Pos, N, V, Mat) * VisFactor;
}

float EvaluatePhotometricProfile128x32(
    uint   ProfileOrdinal,
    float3 TowardSurface,
    float3 BeamAxis,
    float3 ZeroAzimuth)
{
    float  CosTheta    = clamp(dot(BeamAxis, TowardSurface), -1.0, 1.0);
    float  VerticalPos = acos(CosTheta) * (127.0 / PiConst);
    float3 OrthoBit    = cross(BeamAxis, ZeroAzimuth);
    float  HorizPos    = atan2(dot(TowardSurface, OrthoBit), dot(TowardSurface, ZeroAzimuth)) * (32.0 / TauConst);
    if (HorizPos < 0.0)
    {
        HorizPos += 32.0;
    }

    uint  V0   = min(uint(VerticalPos), 126u);
    float Vt   = saturate(VerticalPos - float(V0));
    uint  H0   = min(uint(HorizPos), 31u);
    float Ht   = saturate(HorizPos - float(H0));
    uint  H1   = (H0 + 1u) & 31u;
    uint  Base = ProfileOrdinal * 4096u;
    uint  I00  = Base + H0 * 128u + V0;
    uint  I10  = Base + H1 * 128u + V0;

    float NearRow = lerp(PhotometricProfileTables[I00], PhotometricProfileTables[I00 + 1u], Vt);
    float FarRow  = lerp(PhotometricProfileTables[I10], PhotometricProfileTables[I10 + 1u], Vt);
    return lerp(NearRow, FarRow, Ht);
}

float3 EvaluatePhotometricLampGpu(
    GpuPhotometricLamp64 Lamp,
    uint                 ProfileCount,
    float3               SafePos,
    float3               N,
    float3               V,
    GpuWorldMaterial80   Mat,
    bool                 UseFastGdfVis)
{
    uint ProfileOrd = uint(Lamp.ZeroAzimuthProfile.w + 0.5);
    if (ProfileOrd >= ProfileCount)
    {
        return float3(0.0);
    }

    float3 Delta = Lamp.PositionRange.xyz - SafePos;
    float  Dist2 = dot(Delta, Delta);
    float  Dist  = sqrt(max(1.0e-5, Dist2));
    float  Range = Lamp.PositionRange.w;
    if (Dist >= Range)
    {
        return float3(0.0);
    }

    float3 LDir  = Delta / Dist;
    float  NdotL = dot(N, LDir);
    if (NdotL <= 1.0e-4)
    {
        return float3(0.0);
    }

    float ProfileGain = EvaluatePhotometricProfile128x32(
        ProfileOrd,
        -LDir,
        normalize(Lamp.BeamAxisRadius.xyz),
        normalize(Lamp.ZeroAzimuthProfile.xyz));
    if (ProfileGain <= 1.0e-5)
    {
        return float3(0.0);
    }

    float Radius  = max(0.025, Lamp.BeamAxisRadius.w);
    float LampVis = 1.0;
    if (UseFastGdfVis && IsGlobalDistanceFieldReady())
    {
        LampVis = TraceGlobalDistanceConeVisibility(SafePos, N, LDir, max(0.04, Dist - Radius), Radius / max(0.1, Dist), false, 8u);
        if (LampVis <= 1.0e-3) return float3(0.0);
    }
    else if (TraceSceneAnyHit(SafePos, LDir, 2.5e-3, max(0.02, Dist - Radius)))
    {
        return float3(0.0);
    }

    float Ratio  = saturate(Dist / max(0.5, Range));
    float Ratio2 = Ratio * Ratio;
    float Window = max(0.0, 1.0 - Ratio2 * Ratio2);
    float Atten  = (ProfileGain * Window * Window) / max(0.01, Dist2 + Radius * Radius);
    return EvaluateSurfaceBrdf(Mat, N, V, LDir) * Lamp.PeakIntensityPad.xyz * (Atten * LampVis);
}

//------------------------------------------------------------------------------------------------------------------------
//                      SHADOW LAB ALGORITHMS (depth32float Sun Cascades + BVH4 Ray Queries)
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

float SampleCascadedSunShadowFast(float3 WorldPos, float3 GeomN, float3 N, float3 SunDir)
{
    float NdotL = dot(N, SunDir);
    if (NdotL <= 1.0e-5) return 0.0;

    float2 CenterTXY;
    float  RecD, TexelM;
    uint   Res, BufOffs;
    float3 BiasedPos = WorldPos + GeomN * 0.006 + N * 0.004;
    if (ProjectBestSunCascade(BiasedPos, CenterTXY, RecD, TexelM, Res, BufOffs))
    {
        float SlopeScale = sqrt(max(0.0, 1.0 - NdotL * NdotL)) / max(0.18, NdotL);
        float Bias       = TexelM * (1.35 + 0.90 * SlopeScale);
        return SampleShadowVisBilinear(CenterTXY, RecD, Bias, Res, BufOffs);
    }
    if (IsGlobalDistanceFieldReady())
    {
        return TraceGlobalDistanceConeVisibility(WorldPos, GeomN, SunDir, 12.0, max(0.01, WorldUniforms.SunDirConeTan.w), false, 8u);
    }
    return TraceSceneAnyHit(WorldPos + GeomN * 0.015, SunDir, 0.03, 80.0) ? 0.0 : 1.0;
}

bool IsCascadeCardinalOpen(float2 CenterTXY, float RadiusPx, float RecD, float Bias, uint Res, uint BufOffs)
{
    int MaxIdx = int(Res) - 1;
    int CX     = clamp(int(floor(CenterTXY.x)), 0, MaxIdx);
    int CY     = clamp(int(floor(CenterTXY.y)), 0, MaxIdx);
    if ((RecD - SolarShadowDepthBuffer[BufOffs + uint(CY) * Res + uint(CX)]) > Bias) return false;

    int RInt = max(1, int(ceil(RadiusPx)));
    int XMin = max(0, CX - RInt);
    int XMax = min(MaxIdx, CX + RInt);
    int YMin = max(0, CY - RInt);
    int YMax = min(MaxIdx, CY + RInt);
    if ((RecD - SolarShadowDepthBuffer[BufOffs + uint(CY)   * Res + uint(XMin)]) > Bias) return false;
    if ((RecD - SolarShadowDepthBuffer[BufOffs + uint(CY)   * Res + uint(XMax)]) > Bias) return false;
    if ((RecD - SolarShadowDepthBuffer[BufOffs + uint(YMin) * Res + uint(CX)])   > Bias) return false;
    if ((RecD - SolarShadowDepthBuffer[BufOffs + uint(YMax) * Res + uint(CX)])   > Bias) return false;
    return true;
}

float EvaluateSolarShadow(RayHitReading Hit, float3 SunDir, uint2 PixelXY, out float OutBlockerDist)
{
    OutBlockerDist  = 0.0;
    uint EngineMode = WorldUniforms.EngineShadowReflPad.x;
    uint ShadowAlg  = WorldUniforms.EngineShadowReflPad.y;
    float NdotL     = dot(Hit.ShadingNormal, SunDir);
    if (NdotL <= 1.0e-5) return 0.0;

    float  TanSun = max(0.004, WorldUniforms.SunDirConeTan.w);
    float2 R2P    = PixelR2(PixelXY);
    float  RotPhi = R2P.x * TauConst;

    // Algorithm 4: WideTreeRayQuery (GDF-accelerated solar corridor cull + 3 rotated BVH4 solar disc rays)
    if (ShadowAlg == 4u)
    {
        if (EngineMode <= 2u && IsGlobalDistanceFieldReady())
        {
            float GdfVis = TraceGlobalDistanceConeVisibility(Hit.Position, Hit.GeomNormal, SunDir, 12.0, TanSun * 2.0, false, 8u);
            if (GdfVis >= 0.99)
            {
                return 1.0;
            }
        }
        float3 T, B;
        ConstructOrthonormalBasis(SunDir, T, B);
        float3 Orig      = Hit.Position + Hit.GeomNormal * 0.004;
        float  Vis       = 0.0;
        float  InvBlkSum = 0.0;
        uint   BlkCnt    = 0u;
        const uint RayCount = (EngineMode <= 2u) ? 3u : 4u;
        for (uint R = 0u; R < RayCount; ++R)
        {
            float DiscR = sqrt((float(R) + 0.5) / float(RayCount)) * TanSun;
            float Ang   = float(R) * 2.39996323 + RotPhi;
            float3 JDir = normalize(SunDir + T * (DiscR * cos(Ang)) + B * (DiscR * sin(Ang)));
            RayHitReading Blk = TraceSceneClosest(Orig, JDir, 0.004, 90.0);
            if (!Blk.Hit)
            {
                Vis += 1.0 / float(RayCount);
            }
            else
            {
                InvBlkSum += 1.0 / max(0.02, Blk.Distance);
                ++BlkCnt;
            }
        }
        OutBlockerDist = (BlkCnt > 0u) ? (float(BlkCnt) / max(1.0e-5, InvBlkSum)) : 0.0;
        return Vis;
    }

    float2 CenterTXY;
    float  RecD, TexelM;
    uint   Res, BufOffs;
    float3 BiasedPos = Hit.Position + Hit.GeomNormal * 0.004 + Hit.ShadingNormal * 0.004;
    if (!ProjectBestSunCascade(BiasedPos, CenterTXY, RecD, TexelM, Res, BufOffs))
    {
        if (EngineMode <= 2u && IsGlobalDistanceFieldReady())
        {
            return TraceGlobalDistanceConeVisibility(Hit.Position, Hit.GeomNormal, SunDir, 14.0, TanSun, false, 8u);
        }
        float3 Orig = Hit.Position + Hit.GeomNormal * 0.012;
        return TraceSceneAnyHit(Orig, SunDir, 0.028, 90.0) ? 0.0 : 1.0;
    }

    int   MaxIdx     = int(Res) - 1;
    float EffNdotL   = min(NdotL, abs(dot(Hit.GeomNormal, SunDir)));
    float SlopeScale = min(4.2, sqrt(max(0.0, 1.0 - EffNdotL * EffNdotL)) / max(0.16, EffNdotL));
    float Bias       = TexelM * (1.35 + 0.95 * SlopeScale);

    // Algorithm 3: PcfBilinear (8-Tap Smooth Bilinear Vogel PCF with 5-Tap Lit Early-Out)
    if (ShadowAlg == 3u)
    {
        float RadiusPx = 1.85;
        if (IsCascadeCardinalOpen(CenterTXY, RadiusPx, RecD, Bias, Res, BufOffs))
        {
            return 1.0;
        }
        float Vis = 0.0;
        for (uint Tap = 0u; Tap < 8u; ++Tap)
        {
            float R   = sqrt((float(Tap) + 0.5) * 0.125) * RadiusPx;
            float Ang = float(Tap) * 2.39996323 + RotPhi;
            Vis += SampleShadowVisBilinear(CenterTXY + float2(cos(Ang), sin(Ang)) * R, RecD, Bias + TexelM * 0.12 * R, Res, BufOffs);
        }
        Vis *= 0.125;
        OutBlockerDist = (Vis > 0.01 && Vis < 0.99) ? 0.28 : 0.0;
        return Vis;
    }

    // Algorithm 2: ShadowRayMarch (5-Tap Cardinal Early-Out + 4x10 Quadratic SMRT)
    if (ShadowAlg == 2u)
    {
        float MaxMarchM  = 6.50;
        float MaxStepPx  = clamp((MaxMarchM * TanSun) / TexelM, 2.5, 44.0);
        if (IsCascadeCardinalOpen(CenterTXY, MaxStepPx * 0.85, RecD, Bias, Res, BufOffs))
        {
            return 1.0;
        }
        float ThicknessM = max(0.65, TexelM * 42.0);
        float Lit        = 0.0;
        float BlkDistSum = 0.0;
        uint  BlkHits    = 0u;
        for (uint R = 0u; R < 4u; ++R)
        {
            uint   StepRng = (PixelXY.x * 747796405u + PixelXY.y * 2891336453u + R * 277803737u) | 1u;
            float  StepJit = PcgFloat01(StepRng);
            float  Rad     = sqrt((float(R) + 0.5) * 0.25);
            float  Ang     = float(R) * 2.39996323 + RotPhi;
            float2 StepDir = float2(cos(Ang), sin(Ang)) * (Rad * MaxStepPx);
            bool   Occ     = false;
            float  PrevRayD = RecD;
            for (uint S = 1u; S <= 10u; ++S)
            {
                float TNorm = (float(S) - 0.5 + StepJit) * 0.10;
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
            if (!Occ) Lit += 0.25;
        }
        OutBlockerDist = (BlkHits > 0u) ? (BlkDistSum / float(BlkHits)) : 0.0;
        return Lit;
    }

    // Algorithms 0 (ContactPcss) & 1 (PcssPlaneAware): 8-Tap Vogel Blocker Search + 12-Tap Bilinear Penumbra PCF
    float SearchRadPx = clamp((TanSun * 4.5) / TexelM, 2.75, 24.0);
    float BlockerSum  = 0.0;
    uint  Blockers    = 0u;
    if (!IsCascadeCardinalOpen(CenterTXY, SearchRadPx, RecD, Bias, Res, BufOffs))
    {
        for (uint Tap = 0u; Tap < 8u; ++Tap)
        {
            float R   = sqrt((float(Tap) + 0.5) * 0.125) * SearchRadPx;
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
    }

    float Vis = 1.0;
    if (Blockers > 0u)
    {
        float MeanBlocker = BlockerSum / float(Blockers);
        OutBlockerDist    = MeanBlocker;
        float FilterPx    = clamp((MeanBlocker * TanSun * 2.35) / TexelM, 2.25, 24.0);
        float LitSum      = 0.0;
        for (uint Tap = 0u; Tap < 12u; ++Tap)
        {
            float R   = sqrt((float(Tap) + 0.5) * (1.0 / 12.0)) * FilterPx;
            float Ang = float(Tap) * 2.39996323 + RotPhi + R2P.y * 1.9;
            float2 SampleCoord = CenterTXY + float2(cos(Ang), sin(Ang)) * R;
            LitSum += SampleShadowVisBilinear(SampleCoord, RecD, Bias + TexelM * 0.14 * R, Res, BufOffs);
        }
        Vis = LitSum * (1.0 / 12.0);
    }

    if (ShadowAlg == 0u)
    {
        if (EngineMode <= 2u && IsGlobalDistanceFieldReady())
        {
            float GdfGate = TraceGlobalDistanceConeVisibility(Hit.Position, Hit.GeomNormal, SunDir, 0.34, TanSun * 1.25, false, 4u);
            Vis = min(Vis, lerp(0.03, 1.0, GdfGate));
        }
        else if (Vis > 0.02)
        {
            float3 COrig = Hit.Position + Hit.GeomNormal * 0.006;
            if (TraceSceneAnyHit(COrig, SunDir, 0.004, 0.24))
            {
                Vis = min(Vis, 0.04);
            }
        }
    }
    return Vis;
}

//------------------------------------------------------------------------------------------------------------------------
//                           SPECULAR REFLECTIONS & O(1) RADIOSITY-SHADED SECONDARY BOUNCE
//------------------------------------------------------------------------------------------------------------------------

float3 ShadeSecondaryBounce(float3 RayOrigin, float3 RayDir)
{
    uint ReflSource = WorldUniforms.EngineShadowReflPad.z;
    if (ReflSource == 1u || !IsRaytracingEnabled())
    {
        return EvaluateSkyDome(RayDir);
    }

    RayHitReading Hit = TraceSceneClosest(RayOrigin, RayDir, 2.0e-3, 80.0);
    if (!Hit.Hit)
    {
        return EvaluateSkyDome(RayDir);
    }

    uint MatCount = max(1u, WorldUniforms.LightGlassMaterialDebug.z);
    GpuWorldMaterial80 Mat = MaterialEntries[min(Hit.MaterialSlot, MatCount - 1u)];
    float3 V      = -RayDir;
    float3 N      = Hit.ShadingNormal;
    float3 SunDir = WorldUniforms.SunDirConeTan.xyz;
    float3 SunIrr = WorldUniforms.SolarIrradiancePad.xyz;

    float3 SafeSecondaryPos = Hit.Position + Hit.GeomNormal * 0.012 + N * 0.006;
    float3 ColorHdr         = Mat.EmissiveRadiancePad.xyz;
    float  SunShadow        = SampleCascadedSunShadowFast(Hit.Position, Hit.GeomNormal, N, SunDir);
    if (SunShadow > 1.0e-3)
    {
        float TermGate = saturate(dot(Hit.GeomNormal, SunDir) * 6.0 + 0.35);
        ColorHdr += EvaluateSurfaceBrdf(Mat, N, V, SunDir) * SunIrr * (SunShadow * TermGate);
    }

    uint LightCnt = min(4u, WorldUniforms.LightGlassMaterialDebug.x);
    for (uint L = 0u; L < LightCnt; ++L)
    {
        ColorHdr += EvaluateAreaQuadLightGdfFast(WorldUniforms.AreaLights[L], SafeSecondaryPos, Hit.GeomNormal, N, V, Mat);
    }

    uint PackedPhotoCounts = WorldUniforms.DenoiseStepParams.z;
    uint PhotoProfileCount = PackedPhotoCounts >> 16u;
    uint PhotoLampCount    = PackedPhotoCounts & 0xFFFFu;
    if (PhotoProfileCount > 0u && PhotoLampCount > 0u)
    {
        for (uint PL = 0u; PL < PhotoLampCount; ++PL)
        {
            ColorHdr += EvaluatePhotometricLampGpu(PhotometricLampEntries[PL], PhotoProfileCount, SafeSecondaryPos, N, V, Mat, false);
        }
    }

    float3 Albedo = Mat.DiffuseAlbedoRoughness.xyz * (1.0 - saturate(Mat.SpecParams.y)) * (1.0 - saturate(Mat.CoatRoughnessOptics.y));
    if (IsGlobalDistanceFieldReady())
    {
        float  Vox0     = QueryFineGdfVoxelSize();
        bool   InRad    = false;
        float4 RadFetch = SampleRadiosityPyramidMip(Hit.Position + Hit.GeomNormal * (Vox0 * 1.15), 1.0, InRad);
        if (InRad && RadFetch.a > 0.01)
        {
            ColorHdr += Albedo * (RadFetch.rgb / max(0.22, RadFetch.a)) * 0.55;
        }
        else
        {
            ColorHdr += Albedo * EvaluateSkyDome(N) * 0.22;
        }
    }
    else
    {
        ColorHdr += Albedo * EvaluateSkyDome(N) * 0.22;
    }
    return ColorHdr;
}

// Ultra-fast single-tap 2x2 bilinear shadow-map lookup for MatCap / Phong (Engine 0):
// Zero SDF volume traversal, zero PCSS blocker search, zero Vogel filter loop, zero BVH4 shadow ray.
float SampleMatCapCheapShadow(float3 WorldPos, float3 GeomN, float3 ShadingN)
{
    if (max(WorldUniforms.SolarIrradiancePad.x, max(WorldUniforms.SolarIrradiancePad.y, WorldUniforms.SolarIrradiancePad.z)) <= 1.0e-4)
    {
        return 1.0;
    }
    float3 SunDir = normalize(WorldUniforms.SunDirConeTan.xyz);
    if (dot(ShadingN, SunDir) <= -0.04)
    {
        return 0.0;
    }
    float2 TexelXY   = float2(0.0);
    float  RecD      = 0.0;
    float  TexelM    = 0.0;
    uint   Res       = 0u;
    uint   BufOffset = 0u;
    float3 BiasedPos = WorldPos + GeomN * 0.008;
    if (!ProjectBestSunCascade(BiasedPos, TexelXY, RecD, TexelM, Res, BufOffset))
    {
        return 1.0;
    }
    float CosSlope = max(0.25, abs(dot(GeomN, SunDir)));
    float SlopeTan = min(2.5, sqrt(max(0.0, 1.0 - saturate(CosSlope * CosSlope))) / CosSlope);
    float Bias     = TexelM * (0.90 + 0.55 * SlopeTan);
    return SampleShadowVisBilinear(TexelXY, RecD, Bias, Res, BufOffset);
}

float3 EvaluateMatCapSurface(RayHitReading Hit)
{
    uint MatCount = max(1u, WorldUniforms.LightGlassMaterialDebug.z);
    GpuWorldMaterial80 Mat = MaterialEntries[min(Hit.MaterialSlot, MatCount - 1u)];
    float3 N      = Hit.ShadingNormal;
    float3 CamR   = WorldUniforms.CameraRightTanY.xyz;
    float3 CamU   = WorldUniforms.CameraUpWidth.xyz;
    float3 CamFwd = WorldUniforms.CameraForwardHeight.xyz;

    float3 Vn = normalize(float3(dot(N, CamR), dot(N, CamU), max(0.0, dot(N, -CamFwd))));
    float3 KeyDir  = normalize(float3(0.42, 0.62, 0.66));
    float3 FillDir = normalize(float3(-0.68, 0.25, 0.68));
    float3 RimDir  = normalize(float3(0.0, 0.85, 0.52));

    float SunVis   = SampleMatCapCheapShadow(Hit.Position, Hit.GeomNormal, N);
    float KeyDiff  = max(0.0, dot(Vn, KeyDir)) * (0.28 + 0.72 * SunVis);
    float FillDiff = max(0.0, dot(Vn, FillDir));
    float RimGlow  = pow(saturate(1.0 - Vn.z), 2.6) * (0.35 + 0.65 * max(0.0, dot(Vn, RimDir)));
    float SpecPow  = lerp(96.0, 6.0, clamp(Mat.SpecParams.x, 0.02, 1.0));
    float KeySpec  = pow(max(0.0, dot(Vn, normalize(KeyDir + float3(0.0, 0.0, 1.0)))), SpecPow) * (0.22 + 0.78 * SunVis);
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
    out float3    OutDiffuseAlbedoMatte,
    out float3    OutIndirectIrradiance,
    out bool      OutFilterIndirect,
    out float     OutSunVis,
    out float     OutBlockerDist,
    out float     OutDiffAo,
    out float     OutSpecRough,
    out float     OutSpecAo)
{
    uint   MatCount     = max(1u, WorldUniforms.LightGlassMaterialDebug.z);
    uint   EngineMode   = WorldUniforms.EngineShadowReflPad.x;
    uint   PenumbraMode = (WorldUniforms.AtmosphereMaskPad.w >> 16u) & 0x7u;
    GpuWorldMaterial80 Mat = MaterialEntries[min(Hit.MaterialSlot, MatCount - 1u)];
    float3 N      = Hit.ShadingNormal;
    float3 SunDir = WorldUniforms.SunDirConeTan.xyz;
    float3 SunIrr = WorldUniforms.SolarIrradiancePad.xyz;

    // 1. Direct Solar Shadow
    OutDirectBaseHdr      = Mat.EmissiveRadiancePad.xyz;
    OutSpecReflHdr        = float3(0.0);
    OutUnoccludedSunHdr   = float3(0.0);
    OutIndirectIrradiance = float3(0.0);
    OutFilterIndirect     = false;
    OutSunVis             = 1.0;
    OutBlockerDist        = 0.0;
    float3 SafePrimaryPos = Hit.Position + Hit.GeomNormal * 0.012 + N * 0.006;
    float  NdotL          = dot(N, SunDir);
    if (NdotL > 1.0e-5)
    {
        float EnvSunTrans = EvaluateSolarCloudTransmittanceGpu(Hit.Position, SunDir);
        OutUnoccludedSunHdr = EvaluateSurfaceBrdf(Mat, N, ViewDir, SunDir) * SunIrr * EnvSunTrans;
        OutSunVis           = EvaluateSolarShadow(Hit, SunDir, PixelXY, OutBlockerDist);
    }
    else
    {
        OutSunVis = 0.0;
    }

    // 2. Upper-Hemisphere Clipped Polygonal LTC Quad Area Lights (GDF Soft Penumbra in Engines 1..2)
    uint LightCnt  = min(4u, WorldUniforms.LightGlassMaterialDebug.x);
    uint PixelSalt = PixelXY.x * 1973u + PixelXY.y * 9277u + 1u;
    for (uint L = 0u; L < LightCnt; ++L)
    {
        OutDirectBaseHdr += EvaluateAreaQuadLight(
            WorldUniforms.AreaLights[L], SafePrimaryPos, Hit.GeomNormal, N, ViewDir, Mat, PixelSalt + L * 7919u);
    }

    // 2b. ANSI/IESNA LM-63 Type C Photometric Point Emitters
    uint PackedPhotoCounts = WorldUniforms.DenoiseStepParams.z;
    uint PhotoProfileCount = PackedPhotoCounts >> 16u;
    uint PhotoLampCount    = PackedPhotoCounts & 0xFFFFu;
    if (PhotoProfileCount > 0u && PhotoLampCount > 0u)
    {
        for (uint PL = 0u; PL < PhotoLampCount; ++PL)
        {
            OutDirectBaseHdr += EvaluatePhotometricLampGpu(PhotometricLampEntries[PL], PhotoProfileCount, SafePrimaryPos, N, ViewDir, Mat, false);
        }
    }

    // 3. Ambient Occlusion: 0-ray Global Distance Field Cone AO on GTX (Engines 1..2), 3-ray BVH4 on HighEnd (Engine 3)
    float2 R2P = PixelR2(PixelXY);
    float3 BasisT, BasisB;
    ConstructOrthonormalBasis(N, BasisT, BasisB);
    if (EngineMode <= 2u && IsGlobalDistanceFieldReady())
    {
        OutDiffAo = EvaluateGlobalDistanceAmbientOcclusion(Hit.Position, N, Hit.GeomNormal);
    }
    else
    {
        float3 AoOrig = Hit.Position + Hit.GeomNormal * 0.008;
        float  OccSum = 0.0;
        for (uint R = 0u; R < 3u; ++R)
        {
            float U1   = (float(R) + 0.15 + 0.70 * R2P.y) * (1.0 / 3.0);
            float Phi  = float(R) * 2.39996323 + R2P.x * TauConst;
            float SinT = sqrt(saturate(U1));
            float CosT = sqrt(max(0.0, 1.0 - U1));
            float3 AoDir = BasisT * (SinT * cos(Phi)) + BasisB * (SinT * sin(Phi)) + N * CosT;
            if (!TraceSceneAnyHit(AoOrig, AoDir, 0.006, 0.85))
            {
                OccSum += (1.0 / 3.0);
            }
        }
        OutDiffAo = saturate(0.28 + 0.72 * OccSum);
    }
    float NdotV  = max(1.0e-3, dot(N, ViewDir));
    OutSpecRough = clamp(Mat.SpecParams.x, 0.025, 1.0);
    OutSpecAo    = saturate(pow(NdotV + OutDiffAo, exp2(-16.0 * OutSpecRough - 1.0)) - 1.0 + OutDiffAo);

    // 4. Matte Diffuse Albedo
    float Metalness       = saturate(Mat.SpecParams.y);
    float TransW          = saturate(Mat.CoatRoughnessOptics.y);
    OutDiffuseAlbedoMatte = Mat.DiffuseAlbedoRoughness.xyz * (1.0 - Metalness) * (1.0 - TransW);

    // 5. Secondary Specular Reflection (Mirror BVH4 ray for sharp metals/clearcoats; GDF Glossy Cone for rough GTX surfaces)
    float CoatW = saturate(Mat.EdgeTintCoatWeight.w);
    if (IsReflectionsEnabled() && (Metalness > 0.20 || CoatW > 0.20 || OutSpecRough < 0.24 || EngineMode == 2u || EngineMode == 3u))
    {
        float3 ReflDir = reflect(-ViewDir, N);
        float3 EnvRad  = float3(0.0);
        bool   SharpSpecular = (OutSpecRough < 0.18) || (CoatW > 0.20 && Mat.CoatRoughnessOptics.x < 0.16);

        if ((EngineMode == 1u || EngineMode == 2u || (EngineMode == 3u && OutSpecRough >= 0.18)) && !SharpSpecular && IsGlobalDistanceFieldReady())
        {
            EnvRad = EvaluateGdfGlossyCone(Hit.Position, Hit.GeomNormal, ReflDir, OutSpecRough);
        }
        else if (SharpSpecular || EngineMode == 3u)
        {
            if (EngineMode == 3u && OutSpecRough > 0.04)
            {
                float3 ViewLoc   = float3(dot(ViewDir, BasisT), dot(ViewDir, BasisB), max(1.0e-4, dot(ViewDir, N)));
                float  Alpha     = max(1.0e-3, OutSpecRough * OutSpecRough);
                float3 Stretched = normalize(float3(Alpha * ViewLoc.x, Alpha * ViewLoc.y, ViewLoc.z));
                float  LenSq     = Stretched.x * Stretched.x + Stretched.y * Stretched.y;
                float3 DiskT1    = (LenSq > 1.0e-10) ? (float3(-Stretched.y, Stretched.x, 0.0) * rsqrt(LenSq)) : float3(1.0, 0.0, 0.0);
                float3 DiskT2    = cross(Stretched, DiskT1);
                float  Rad       = sqrt(saturate(R2P.x));
                float  Phi       = TauConst * R2P.y;
                float  P1        = Rad * cos(Phi);
                float  P2        = Rad * sin(Phi);
                float  Blend     = 0.5 * (1.0 + Stretched.z);
                P2               = (1.0 - Blend) * sqrt(max(0.0, 1.0 - P1 * P1)) + Blend * P2;
                float3 Hemi      = P1 * DiskT1 + P2 * DiskT2 + sqrt(max(0.0, 1.0 - P1 * P1 - P2 * P2)) * Stretched;
                float3 HalfLoc   = normalize(float3(Alpha * Hemi.x, Alpha * Hemi.y, max(1.0e-5, Hemi.z)));
                float3 HalfWorld = normalize(BasisT * HalfLoc.x + BasisB * HalfLoc.y + N * HalfLoc.z);
                float3 CandDir   = reflect(-ViewDir, HalfWorld);
                if (dot(CandDir, Hit.GeomNormal) > 0.01)
                {
                    ReflDir = CandDir;
                }
            }
            EnvRad = ShadeSecondaryBounce(Hit.Position + Hit.GeomNormal * 3.0e-3, ReflDir);
        }
        else
        {
            EnvRad = EvaluateSkyDome(ReflDir);
        }

        if (OutSpecRough > 0.12)
        {
            float MaxSecondary = lerp(32.0, 3.5, saturate((OutSpecRough - 0.12) / 0.68));
            EnvRad = min(EnvRad, float3(MaxSecondary));
        }
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

    // 6. Demodulated Indirect Diffuse GI (PenumbraMode == 2 isolates direct clipmap penumbra without cone GI)
    bool EnableIndirectGi = (PenumbraMode != 2u) && (dot(OutDiffuseAlbedoMatte, float3(1.0)) > 1.0e-4);
    if (EnableIndirectGi)
    {
        if (EngineMode == 2u && IsGlobalDistanceFieldReady())
        {
            // Engine 2 (GTX GDF GI): 5-cone 2-bounce 3D Radiosity Cone Tracing (0 BVH4 rays, 100% noise-free)
            OutIndirectIrradiance = EvaluateGdfVoxelConeGi(Hit.Position, N, Hit.GeomNormal, BasisT, BasisB);
            OutFilterIndirect     = false;
        }
        else if (EngineMode == 2u || EngineMode == 3u)
        {
            // Engine 3 (HighEnd 0-SDF BVH4 GI): 2 PCG-animated hemisphere rays + 3-pass A-Trous irradiance filter
            float3 BounceOrig  = Hit.Position + Hit.GeomNormal * 0.014 + N * 0.006;
            float3 IndirectSum = float3(0.0);
            for (uint B = 0u; B < 2u; ++B)
            {
                float  U1   = frac((float(B) + 0.5) * 0.5 + R2P.x);
                float  U2   = frac(float(B) * 0.6180339887 + R2P.y);
                float  SinT = sqrt(saturate(U1));
                float  CosT = sqrt(max(0.0, 1.0 - U1));
                float  Phi  = TauConst * U2;
                float3 Dir  = normalize(BasisT * (SinT * cos(Phi)) + BasisB * (SinT * sin(Phi)) + N * CosT);
                float3 Rad  = min(ShadeSecondaryBounce(BounceOrig, Dir), float3(6.5));
                IndirectSum += Rad;
            }
            OutIndirectIrradiance = IndirectSum * 0.5;
            OutFilterIndirect     = true;
        }
    }
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
    uint GlassCnt = min(4u, WorldUniforms.LightGlassMaterialDebug.y);
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
    if (WorldUniforms.FogParams.GodRayParams0.x < 0.5 ||
        WorldUniforms.FogParams.GodRayParams0.y <= 1.0e-4 ||
        SunDir.z <= 0.005 ||
        MaxDistM <= 0.25)
    {
        return float3(0.0, 0.0, 0.0);
    }
    VolumetricFogUniformBlock Fog = WorldUniforms.FogParams;

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
//                                           COMPUTE ENTRYPOINTS
//------------------------------------------------------------------------------------------------------------------------

float3 SampleAreaQuadLightStochasticPt(GpuAreaQuadLight Quad, float3 Pos, float3 GeomN, float3 N, float3 V, GpuWorldMaterial80 Mat, float2 XiL)
{
    float3 RightT = Quad.RightHalfHeight.xyz;
    float3 UpB    = Quad.UpTwoSided.xyz;
    float3 QuadN  = normalize(cross(RightT, UpB));
    float3 Center = Quad.CenterHalfWidth.xyz;
    if (Quad.UpTwoSided.w < 0.5 && dot(Pos - Center, QuadN) <= 1.0e-4)
    {
        return float3(0.0);
    }

    float3 HalfU   = RightT * Quad.CenterHalfWidth.w;
    float3 HalfV   = UpB * Quad.RightHalfHeight.w;
    float3 SampleP = Center + HalfU * (XiL.x * 2.0 - 1.0) + HalfV * (XiL.y * 2.0 - 1.0);
    float3 ToL     = SampleP - Pos;
    float  Dist    = length(ToL);
    if (Dist <= 1.0e-4) return float3(0.0);
    float3 DirL    = ToL / Dist;
    if (dot(N, DirL) <= 0.0) return float3(0.0);

    if (TraceSceneAnyHit(Pos + GeomN * 0.012, DirL, 0.025, max(0.035, Dist - 0.030)))
    {
        return float3(0.0);
    }
    return EvaluateAreaQuadBrdfAnalytic(Quad, Pos, N, V, Mat);
}

float3 EvaluateProgressiveViewportPath(float3 CamOrigin, float3 PrimaryDir, uint2 PixelXY, uint SampleIndex, uint EngineMode)
{
    uint   MaxBounces = (EngineMode == 5u) ? 4u : 3u;
    uint   MatCount   = max(1u, WorldUniforms.LightGlassMaterialDebug.z);
    float3 SunDir     = WorldUniforms.SunDirConeTan.xyz;
    float3 SunIrr     = WorldUniforms.SolarIrradiancePad.xyz;
    float  TanSun     = max(0.004, WorldUniforms.SunDirConeTan.w);
    uint   LightCnt   = min(4u, WorldUniforms.LightGlassMaterialDebug.x);
    uint   PackedPhot = WorldUniforms.DenoiseStepParams.z;
    uint   PhotProfs  = PackedPhot >> 16u;
    uint   PhotLamps  = PackedPhot & 0xFFFFu;

    float2 BaseShift = (EngineMode == 4u)
        ? PixelR2(PixelXY)
        : float2(0.0, 0.0);

    // Primary intersection handled outside bounce loop to avoid duplicating sky/cloud/glass raymarchers
    RayHitReading CurHit = TraceSceneClosest(CamOrigin, PrimaryDir, 0.015, 50000.0);
    float         TriT0  = CurHit.Hit ? CurHit.Distance : 50000.0;
    float         GlsT0  = TriT0;
    int           GlsId0 = FindClosestGlassSphere(CamOrigin, PrimaryDir, 0.015, TriT0, GlsT0);
    if (GlsId0 >= 0)
    {
        GpuGlassSphere Glass  = WorldUniforms.GlassSpheres[GlsId0];
        float3         EntryP = CamOrigin + PrimaryDir * GlsT0;
        float3         EntryN = normalize(EntryP - Glass.CenterRadiusPacked.xyz);
        float          Ior    = max(1.03, Glass.OpticsSlotPacked.x);
        return min(TraceThroughGlassSphere(EntryP, EntryN, PrimaryDir, Glass.CenterRadiusPacked.xyz, Glass.CenterRadiusPacked.w, Ior), float3(32.0));
    }
    if (!CurHit.Hit)
    {
        return min(EvaluateBackgroundSkyCloudsGpu(PrimaryDir, float2(PixelXY)), float3(32.0));
    }

    float3 RayO       = CamOrigin;
    float3 RayD       = PrimaryDir;
    float3 Throughput = float3(1.0, 1.0, 1.0);
    float3 Radiance   = float3(0.0, 0.0, 0.0);

    [loop]
    for (uint Bounce = 0u; Bounce < MaxBounces; ++Bounce)
    {
        if (Bounce > 0u)
        {
            CurHit = TraceSceneClosest(RayO, RayD, 0.003, 50000.0);
            if (!CurHit.Hit)
            {
                Radiance += Throughput * EvaluateSkyDome(RayD);
                break;
            }
        }

        GpuWorldMaterial80 Mat = MaterialEntries[min(CurHit.MaterialSlot, MatCount - 1u)];
        float3 N        = CurHit.ShadingNormal;
        float3 V        = -RayD;
        float3 SafePos  = CurHit.Position + CurHit.GeomNormal * 0.012 + N * 0.006;
        float3 Emissive = Mat.EmissiveRadiancePad.xyz;
        if (Bounce > 0u)
        {
            Emissive = min(Emissive, float3(18.0));
        }
        Radiance += Throughput * Emissive;

        float2 Xi = frac(BaseShift + float2(
            frac(float(SampleIndex * 7u + Bounce * 3u + 1u) * 0.7548776662),
            frac(float(SampleIndex * 7u + Bounce * 3u + 1u) * 0.5698402910)));

        // Direct solar NEE with soft solar-disc jitter
        if (dot(N, SunDir) > 1.0e-5)
        {
            float3 SunT, SunB;
            ConstructOrthonormalBasis(SunDir, SunT, SunB);
            float  DiscR  = sqrt(Xi.x) * TanSun;
            float  DiscPh = TauConst * Xi.y;
            float3 JitSun = normalize(SunDir + SunT * (DiscR * cos(DiscPh)) + SunB * (DiscR * sin(DiscPh)));
            if (dot(N, JitSun) > 0.0 && !TraceSceneAnyHit(SafePos, JitSun, 0.025, 110.0))
            {
                float CloudTr = (Bounce == 0u) ? EvaluateSolarCloudTransmittanceGpu(CurHit.Position, SunDir) : 1.0;
                Radiance += Throughput * EvaluateSurfaceBrdf(Mat, N, V, JitSun) * SunIrr * CloudTr;
            }
        }

        // Direct area & photometric luminaires (1 stratified Monte Carlo shadow ray per bounce)
        [loop]
        for (uint L = 0u; L < LightCnt; ++L)
        {
            float2 XiL = frac(Xi + float2(float(L) * 0.3819660113, float(L) * 0.6180339887));
            Radiance += Throughput * SampleAreaQuadLightStochasticPt(
                WorldUniforms.AreaLights[L], SafePos, CurHit.GeomNormal, N, V, Mat, XiL);
        }
        if (PhotProfs > 0u && PhotLamps > 0u)
        {
            [loop]
            for (uint PL = 0u; PL < PhotLamps; ++PL)
            {
                Radiance += Throughput * EvaluatePhotometricLampGpu(PhotometricLampEntries[PL], PhotProfs, SafePos, N, V, Mat, false);
            }
        }

        // BSDF stochastic bounce selection (specular vs diffuse)
        float  Rough    = clamp(Mat.SpecParams.x, 0.025, 1.0);
        float  Metal    = saturate(Mat.SpecParams.y);
        float  TransW   = saturate(Mat.CoatRoughnessOptics.y);
        float  CoatW    = saturate(Mat.EdgeTintCoatWeight.w);
        float  NdotV    = max(1.0e-4, dot(N, V));
        float  Ior      = max(1.02, Mat.SpecParams.z);
        float  F0Scalar = pow((Ior - 1.0) / (Ior + 1.0), 2.0);
        float3 F0       = lerp(float3(F0Scalar), Mat.DiffuseAlbedoRoughness.xyz, Metal);
        float3 Fresnel  = F0 + (float3(1.0) - F0) * pow(1.0 - NdotV, 5.0);
        if (CoatW > 0.15 && Metal < 0.2)
        {
            Fresnel = max(Fresnel, float3((0.04 + 0.96 * pow(1.0 - NdotV, 5.0)) * CoatW));
        }
        float3 DiffAlb  = Mat.DiffuseAlbedoRoughness.xyz * (1.0 - Metal) * (1.0 - TransW);
        float  SpecLuma = dot(Fresnel, float3(0.2126, 0.7152, 0.0722));
        float  DiffLuma = dot(DiffAlb, float3(0.2126, 0.7152, 0.0722));
        float  SpecProb = clamp(SpecLuma / max(1.0e-4, SpecLuma + DiffLuma), 0.10, 0.92);

        float2 BounceXi = frac(BaseShift + float2(
            frac(float(SampleIndex * 11u + Bounce * 5u + 2u) * 0.6180339887),
            frac(float(SampleIndex * 11u + Bounce * 5u + 2u) * 0.3247179572)));
        float  LobeSel  = frac(BaseShift.x * 0.73 + BaseShift.y * 0.37 + float(SampleIndex * 3u + Bounce) * 0.41421356);

        float3 BasisT, BasisB;
        ConstructOrthonormalBasis(N, BasisT, BasisB);
        if (LobeSel < SpecProb)
        {
            float3 ViewLoc   = float3(dot(V, BasisT), dot(V, BasisB), max(1.0e-4, dot(V, N)));
            float  Alpha     = max(1.0e-3, Rough * Rough);
            float3 Stretched = normalize(float3(Alpha * ViewLoc.x, Alpha * ViewLoc.y, ViewLoc.z));
            float  LenSq     = Stretched.x * Stretched.x + Stretched.y * Stretched.y;
            float3 DiskT1    = (LenSq > 1.0e-10) ? (float3(-Stretched.y, Stretched.x, 0.0) * rsqrt(LenSq)) : float3(1.0, 0.0, 0.0);
            float3 DiskT2    = cross(Stretched, DiskT1);
            float  Rad       = sqrt(saturate(BounceXi.x));
            float  Phi       = TauConst * BounceXi.y;
            float  P1        = Rad * cos(Phi);
            float  P2        = Rad * sin(Phi);
            float  Blend     = 0.5 * (1.0 + Stretched.z);
            P2               = (1.0 - Blend) * sqrt(max(0.0, 1.0 - P1 * P1)) + Blend * P2;
            float3 Hemi      = P1 * DiskT1 + P2 * DiskT2 + sqrt(max(0.0, 1.0 - P1 * P1 - P2 * P2)) * Stretched;
            float3 HalfLoc   = normalize(float3(Alpha * Hemi.x, Alpha * Hemi.y, max(1.0e-5, Hemi.z)));
            float3 HalfWorld = normalize(BasisT * HalfLoc.x + BasisB * HalfLoc.y + N * HalfLoc.z);
            RayD             = reflect(-V, HalfWorld);
            if (dot(RayD, CurHit.GeomNormal) <= 0.0)
            {
                break;
            }
            Throughput *= Fresnel / SpecProb;
        }
        else
        {
            float  SinT = sqrt(saturate(BounceXi.x));
            float  CosT = sqrt(max(0.0, 1.0 - BounceXi.x));
            float  Phi  = TauConst * BounceXi.y;
            RayD        = normalize(BasisT * (SinT * cos(Phi)) + BasisB * (SinT * sin(Phi)) + N * CosT);
            if (dot(RayD, CurHit.GeomNormal) <= 0.0)
            {
                break;
            }
            Throughput *= DiffAlb / (1.0 - SpecProb);
        }
        RayO = CurHit.Position + CurHit.GeomNormal * 0.006;
    }
    return min(Radiance, float3(32.0));
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
    float3 CloudRad   = EvaluatePlanetaryCloudsOnlyGpu(
        WorldUniforms.CloudParams, RayDir, float2(DispatchThreadID.xy), CloudTrans);
    CloudRadiometricBuffer[PIdx] = float4(CloudRad, CloudTrans);
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
    uint   EngineMode = WorldUniforms.EngineShadowReflPad.x;
    uint   DebugMode  = WorldUniforms.LightGlassMaterialDebug.w;
    float3 CamOrigin  = WorldUniforms.CameraOriginTanX.xyz;

    float2 PixelUv = (float2(DispatchThreadID.xy) + float2(0.5, 0.5)) / float2(float(Width), float(Height));
    float  NdcX    = PixelUv.x * 2.0 - 1.0;
    float  NdcY    = 1.0 - PixelUv.y * 2.0;
    float3 RayDir  = normalize(
        WorldUniforms.CameraForwardHeight.xyz +
        WorldUniforms.CameraRightTanY.xyz * (NdcX * WorldUniforms.CameraOriginTanX.w) +
        WorldUniforms.CameraUpWidth.xyz   * (NdcY * WorldUniforms.CameraRightTanY.w));

    RayHitReading PrimaryHit = TraceSceneClosest(CamOrigin, RayDir, 0.015, 50000.0);

    float TriHitT      = PrimaryHit.Hit ? PrimaryHit.Distance : 50000.0;
    float BestGlassT   = TriHitT;
    int   BestGlassIdx = -1;
    if (EngineMode != 0u)
    {
        BestGlassIdx = FindClosestGlassSphere(CamOrigin, RayDir, 0.015, TriHitT, BestGlassT);
    }

    float HitDistForClouds = (BestGlassIdx >= 0) ? BestGlassT : (PrimaryHit.Hit ? PrimaryHit.Distance : 60000.0);
    float HitDistForFog    = (BestGlassIdx >= 0) ? BestGlassT : (PrimaryHit.Hit ? PrimaryHit.Distance : 3500.0);

    bool UsesShadingDebug = (DebugMode >= 8u && DebugMode <= 11u) || DebugMode == 15u;
    if (DebugMode != 0u && !UsesShadingDebug)
    {
        float3 DbgHdr = float3(0.025, 0.032, 0.048);
        bool   IsSdfSphereTraceDebug = (DebugMode == 13u || DebugMode == 14u);
        if (IsSdfSphereTraceDebug)
        {
            float  SdfHitDist = 0.0;
            float3 SdfHitPos  = float3(0.0);
            float3 SdfHitNrm  = float3(0.0, 0.0, 1.0);
            uint   SdfMatSlot = 0u;
            if (SphereTraceDistanceField(CamOrigin, RayDir, 500.0, SdfHitDist, SdfHitPos, SdfHitNrm, SdfMatSlot))
            {
                if (DebugMode == 14u)
                {
                    DbgHdr = SdfHitNrm * 0.5 + 0.5;
                }
                else
                {
                    uint MatCount = max(1u, WorldUniforms.LightGlassMaterialDebug.z);
                    GpuWorldMaterial80 SdfMat = MaterialEntries[min(SdfMatSlot, MatCount - 1u)];
                    float  SunNdotL = saturate(dot(SdfHitNrm, WorldUniforms.SunDirConeTan.xyz));
                    float  SunVis   = TraceGlobalDistanceConeVisibility(
                        SdfHitPos, SdfHitNrm, WorldUniforms.SunDirConeTan.xyz, 40.0, 0.04, false, 12u);
                    float  SdfAo    = EvaluateGlobalDistanceAmbientOcclusion(SdfHitPos, SdfHitNrm, SdfHitNrm);
                    float3 SkyAmb   = EvaluateSkyDome(SdfHitNrm) * 0.25 * SdfAo;
                    float3 LitSdf   = SdfMat.DiffuseAlbedoRoughness.xyz *
                        (SkyAmb + WorldUniforms.SolarIrradiancePad.xyz * (0.28 * SunNdotL * SunVis + 0.08));
                    DbgHdr = ApplyAcesFilmicGpu(LitSdf + SdfMat.EmissiveRadiancePad.xyz);
                }
            }
            else
            {
                DbgHdr = ApplyAcesFilmicGpu(EvaluateSkyDome(RayDir) * 0.45);
            }
        }
        else if (BestGlassIdx >= 0)
        {
            GpuGlassSphere Glass = WorldUniforms.GlassSpheres[BestGlassIdx];
            float3 HitPos = CamOrigin + RayDir * BestGlassT;
            float3 HitNrm = normalize(HitPos - Glass.CenterRadiusPacked.xyz);
            if (DebugMode == 1u)      DbgHdr = float3(1.0 / (1.0 + BestGlassT * 0.085));
            else if (DebugMode == 2u) DbgHdr = float3(0.95, 0.98, 1.00);
            else if (DebugMode == 3u) DbgHdr = HitNrm * 0.5 + 0.5;
            else if (DebugMode == 4u) DbgHdr = float3(clamp(Glass.OpticsSlotPacked.w, 0.025, 1.0));
            else if (DebugMode == 5u) DbgHdr = float3(0.0);
            else if (DebugMode == 6u) DbgHdr = float3(0.28, 0.85, 0.96);
            else if (DebugMode == 7u) DbgHdr = HitNrm * 0.5 + 0.5;
        }
        else if (PrimaryHit.Hit)
        {
            uint MatCount = max(1u, WorldUniforms.LightGlassMaterialDebug.z);
            GpuWorldMaterial80 Mat = MaterialEntries[min(PrimaryHit.MaterialSlot, MatCount - 1u)];
            if (DebugMode == 1u)      DbgHdr = float3(1.0 / (1.0 + PrimaryHit.Distance * 0.085));
            else if (DebugMode == 2u) DbgHdr = Mat.DiffuseAlbedoRoughness.xyz;
            else if (DebugMode == 3u) DbgHdr = PrimaryHit.ShadingNormal * 0.5 + 0.5;
            else if (DebugMode == 4u) DbgHdr = float3(Mat.SpecParams.x);
            else if (DebugMode == 5u) DbgHdr = float3(saturate(Mat.SpecParams.y));
            else if (DebugMode == 6u)
            {
                uint SId = PrimaryHit.MaterialSlot + 1u;
                float H1 = frac(float(SId) * 0.3819660113);
                float H2 = frac(float(SId) * 0.6180339887 + 0.27);
                float H3 = frac(float(SId) * 0.7548776662 + 0.53);
                float FormShade = 0.42 + 0.58 * saturate(dot(PrimaryHit.ShadingNormal, WorldUniforms.SunDirConeTan.xyz) * 0.5 + 0.5);
                DbgHdr = float3(0.18 + 0.80 * H1, 0.18 + 0.80 * H2, 0.18 + 0.80 * H3) * FormShade;
            }
            else if (DebugMode == 7u) DbgHdr = PrimaryHit.GeomNormal * 0.5 + 0.5;
            else if (DebugMode == 12u)
            {
                // SDF Distance: evaluate distance along the normal offset + crisp isoline rings across ground/surfaces
                float LocalVox  = 0.08;
                float ProbeDist = SampleGlobalDistanceWorld(PrimaryHit.Position + PrimaryHit.GeomNormal * 0.18, LocalVox);
                float3 BaseHeat = EvaluateSdfDistanceHeatColor(ProbeDist - 0.18, LocalVox);
                float  IsoRing  = smoothstep(0.86, 0.98, abs(sin(ProbeDist * PiConst * 4.0)));
                DbgHdr = lerp(BaseHeat, float3(0.98, 0.96, 0.82), IsoRing * 0.65);
            }
            else if (DebugMode == 16u)
            {
                // SDF Cascade: 0 = Fine Local Brick (cyan-green), 1 = Fine Global (green), 2 = Coarse Global (orange), 3 = Outer (violet)
                uint CascadeIdx = QueryGlobalDistanceCascadeIndex(PrimaryHit.Position);
                float3 CascadeTint = (CascadeIdx == 0u) ? float3(0.12, 0.92, 0.72)
                    : ((CascadeIdx == 1u) ? float3(0.24, 0.88, 0.28)
                    : ((CascadeIdx == 2u) ? float3(0.95, 0.58, 0.14) : float3(0.85, 0.22, 0.65)));
                float FormShade = 0.38 + 0.62 * saturate(dot(PrimaryHit.GeomNormal, WorldUniforms.SunDirConeTan.xyz) * 0.5 + 0.5);
                DbgHdr = CascadeTint * FormShade;
            }
            else if (DebugMode == 17u)
            {
                // Cluster LOD Level: green L0 finest, yellow L1, orange L2, red L3, purple L4+
                float3 LevelColor = float3(0.10, 0.85, 0.35);
                if (PrimaryHit.LodLevel == 1u)      LevelColor = float3(0.95, 0.85, 0.10);
                else if (PrimaryHit.LodLevel == 2u) LevelColor = float3(0.98, 0.45, 0.08);
                else if (PrimaryHit.LodLevel == 3u) LevelColor = float3(0.92, 0.10, 0.15);
                else if (PrimaryHit.LodLevel >= 4u) LevelColor = float3(0.75, 0.20, 0.95);
                float FormShade = 0.32 + 0.68 * saturate(dot(PrimaryHit.ShadingNormal, WorldUniforms.SunDirConeTan.xyz) * 0.5 + 0.5);
                DbgHdr = LevelColor * FormShade;
            }
            else if (DebugMode == 18u)
            {
                // Cluster ID: one vivid colour per cluster so cluster granularity is unmistakable
                uint CId = PrimaryHit.ClusterOrdinal + 1u;
                float H1 = frac(float(CId) * 0.3819660113);
                float H2 = frac(float(CId) * 0.6180339887 + 0.27);
                float H3 = frac(float(CId) * 0.7548776662 + 0.53);
                float3 ClusterColor = float3(H1, H2, H3);
                ClusterColor /= max(0.05, max(ClusterColor.r, max(ClusterColor.g, ClusterColor.b)));
                float FormShade = 0.35 + 0.65 * saturate(dot(PrimaryHit.ShadingNormal, WorldUniforms.SunDirConeTan.xyz) * 0.5 + 0.5);
                DbgHdr = ClusterColor * FormShade;
            }
        }
        GBufPosMask[PixelIdx]            = float4(0.0, 0.0, 0.0, 0.0);
        GBufNormAo[PixelIdx]             = float4(0.0, 0.0, 1.0, 1.0);
        GBufDirectBase[PixelIdx]         = float4(DbgHdr, HitDistForClouds);
        GBufSunContrib[PixelIdx]         = float4(0.0, 0.0, 0.0, HitDistForFog);
        GBufAlbedo[PixelIdx]             = float4(0.0, 0.0, 0.0, 1.0);
        PingShadowAo[PixelIdx]           = float4(1.0, 1.0, 0.0, 1.0);
        PingIndirectIrradiance[PixelIdx] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    if (BestGlassIdx >= 0 && (DebugMode == 0u || DebugMode == 11u))
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
        if (SpecGl > 0.0)
        {
            SpecGl *= SampleCascadedSunShadowFast(HitPos, HitNrm, HitNrm, SunDir);
        }
        float3 GlassHdr = (DebugMode == 11u)
            ? ApplyAcesFilmicGpu(ReflRgb * Fresnel + WorldUniforms.SolarIrradiancePad.xyz * SpecGl)
            : (lerp(RefrRgb, ReflRgb, Fresnel) + WorldUniforms.SolarIrradiancePad.xyz * SpecGl);
        GBufPosMask[PixelIdx]            = float4(0.0, 0.0, 0.0, 0.0);
        GBufNormAo[PixelIdx]             = float4(HitNrm, 1.0);
        GBufDirectBase[PixelIdx]         = float4(GlassHdr, HitDistForClouds);
        GBufSunContrib[PixelIdx]         = float4(0.0, 0.0, 0.0, HitDistForFog);
        GBufAlbedo[PixelIdx]             = float4(0.0, 0.0, 0.0, 1.0);
        PingShadowAo[PixelIdx]           = float4(1.0, 1.0, 0.0, 1.0);
        PingIndirectIrradiance[PixelIdx] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    if (!PrimaryHit.Hit)
    {
        // Defer sky/cloud/celestial background shading to DenoiseCompositeEntry so ComputeEntry stays VGPR-lean!
        GBufPosMask[PixelIdx]            = float4(RayDir, -1.0);
        GBufNormAo[PixelIdx]             = float4(0.0, 0.0, 1.0, 1.0);
        GBufDirectBase[PixelIdx]         = float4(0.0, 0.0, 0.0, HitDistForClouds);
        GBufSunContrib[PixelIdx]         = float4(0.0, 0.0, 0.0, HitDistForFog);
        GBufAlbedo[PixelIdx]             = float4(0.0, 0.0, 0.0, 1.0);
        PingShadowAo[PixelIdx]           = float4(1.0, 1.0, 0.0, 1.0);
        PingIndirectIrradiance[PixelIdx] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    if (EngineMode == 0u && DebugMode == 0u)
    {
        float3 MatCapHdr = EvaluateMatCapSurface(PrimaryHit);
        GBufPosMask[PixelIdx]            = float4(0.0, 0.0, 0.0, 0.0);
        GBufNormAo[PixelIdx]             = float4(PrimaryHit.ShadingNormal, 1.0);
        GBufDirectBase[PixelIdx]         = float4(MatCapHdr, HitDistForClouds);
        GBufSunContrib[PixelIdx]         = float4(0.0, 0.0, 0.0, HitDistForFog);
        GBufAlbedo[PixelIdx]             = float4(0.0, 0.0, 0.0, 1.0);
        PingShadowAo[PixelIdx]           = float4(1.0, 1.0, 0.0, 1.0);
        PingIndirectIrradiance[PixelIdx] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float3 DirectBaseHdr      = float3(0.0);
    float3 SpecReflHdr        = float3(0.0);
    float3 UnoccludedSunHdr   = float3(0.0);
    float3 DiffuseAlbedoMatte = float3(0.0);
    float3 IndirectIrradiance = float3(0.0);
    bool   FilterIndirect     = false;
    float  SunVis = 1.0, BlockerDist = 0.0, DiffAo = 1.0, SpecRough = 0.5, SpecAo = 1.0;
    ShadePrimarySurfaceDemod(
        PrimaryHit,
        -RayDir,
        DispatchThreadID.xy,
        DirectBaseHdr,
        SpecReflHdr,
        UnoccludedSunHdr,
        DiffuseAlbedoMatte,
        IndirectIrradiance,
        FilterIndirect,
        SunVis,
        BlockerDist,
        DiffAo,
        SpecRough,
        SpecAo);

    AccumulationHdrBuffer[PixelIdx]  = float4(SpecReflHdr, SpecRough);
    GBufPosMask[PixelIdx]            = float4(PrimaryHit.Position, 1.0 + SpecRough);
    GBufNormAo[PixelIdx]             = float4(PrimaryHit.ShadingNormal, DiffAo);
    GBufDirectBase[PixelIdx]         = float4(DirectBaseHdr, HitDistForClouds);
    GBufSunContrib[PixelIdx]         = float4(UnoccludedSunHdr, HitDistForFog);
    GBufAlbedo[PixelIdx]             = float4(DiffuseAlbedoMatte, SpecAo);
    PingShadowAo[PixelIdx]           = float4(SunVis, DiffAo, BlockerDist, 1.0);
    PingIndirectIrradiance[PixelIdx] = float4(IndirectIrradiance, FilterIndirect ? 1.0 : 0.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ViewportPathTraceEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Width  = uint(WorldUniforms.CameraUpWidth.w + 0.5);
    uint Height = uint(WorldUniforms.CameraForwardHeight.w + 0.5);
    if (DispatchThreadID.x >= Width || DispatchThreadID.y >= Height)
    {
        return;
    }

    uint   PixelIdx   = DispatchThreadID.y * Width + DispatchThreadID.x;
    uint   EngineMode = WorldUniforms.EngineShadowReflPad.x;
    uint   SampleIdx  = WorldUniforms.EngineShadowReflPad.w;
    float3 CamOrigin  = WorldUniforms.CameraOriginTanX.xyz;

    float2 PixelUv = (float2(DispatchThreadID.xy) + float2(0.5, 0.5)) / float2(float(Width), float(Height));
    float  NdcX    = PixelUv.x * 2.0 - 1.0;
    float  NdcY    = 1.0 - PixelUv.y * 2.0;
    float3 RayDir  = normalize(
        WorldUniforms.CameraForwardHeight.xyz +
        WorldUniforms.CameraRightTanY.xyz * (NdcX * WorldUniforms.CameraOriginTanX.w) +
        WorldUniforms.CameraUpWidth.xyz   * (NdcY * WorldUniforms.CameraRightTanY.w));

    float3 SampleHdr = EvaluateProgressiveViewportPath(CamOrigin, RayDir, DispatchThreadID.xy, SampleIdx, EngineMode);
    float3 NextAccum = (SampleIdx == 0u)
        ? SampleHdr
        : lerp(AccumulationHdrBuffer[PixelIdx].xyz, SampleHdr, 1.0 / float(SampleIdx + 1u));
    AccumulationHdrBuffer[PixelIdx] = float4(NextAccum, float(SampleIdx + 1u));

    float3 FinalHdr = NextAccum;
    if (WorldUniforms.AtmosphereMaskPad.x != 0u && WorldUniforms.LensFlareParams.GhostRotationTint.w > 0.5)
    {
        FinalHdr += EvaluateBakedLensFlareGpu(WorldUniforms.LensFlareParams, BakedLensFlareAtlasTexels, PixelUv);
    }
    OutDisplayRgba8[PixelIdx] = PackLinearToSrgb8Rgba(ApplyAcesFilmicGpu(FinalHdr), DispatchThreadID.xy);
}

// XeGTAO ground-truth ambient occlusion (Jimenez et al.): multi-slice horizon search over G-buffer
// world positions with IGN slice/radial jitter, thin-occluder range falloff, and analytic cosine-weighted
// hemisphere slice integration (exact 1.0 on unoccluded surfaces). Direct PBR only (adds no GI).
bool QueryXeGtaoEnabled()
{
    return (WorldUniforms.GlobalDistanceBakeControl.w & 1u) != 0u;
}

float QueryXeGtaoStrength()
{
    return float((WorldUniforms.GlobalDistanceBakeControl.w >> 8u) & 0xFFu) * (2.0 / 255.0);
}

float EvaluateGroundTruthAmbientOcclusion(uint2 PixelCoord, float3 WorldPos, float3 WorldNormal)
{
    uint Width  = uint(WorldUniforms.CameraUpWidth.w + 0.5);
    uint Height = uint(WorldUniforms.CameraForwardHeight.w + 0.5);
    float3 CamOrigin = WorldUniforms.CameraOriginTanX.xyz;
    float3 CamRight  = WorldUniforms.CameraRightTanY.xyz;
    float3 CamUp     = WorldUniforms.CameraUpWidth.xyz;
    float  TanHalfY  = max(0.05, WorldUniforms.CameraRightTanY.w);

    float3 ToSurface  = WorldPos - CamOrigin;
    float  ViewDist   = max(0.05, length(ToSurface));
    float3 ViewToward = ToSurface / ViewDist;
    float3 ViewFrom   = -ViewToward;

    const float EffectRadiusMeters = 0.65;
    const float FalloffStartMeters = EffectRadiusMeters * 0.55;
    const float FalloffEndMeters   = EffectRadiusMeters * 1.45;
    float RadiusPx = clamp(EffectRadiusMeters * (float(Height) * 0.5) / (ViewDist * TanHalfY), 2.5, 42.0);

    float IgnAngle  = frac(52.9829189 * frac(dot(float2(PixelCoord), float2(0.06711056, 0.00583715))));
    float IgnRadial = frac(52.9829189 * frac(dot(float2(PixelCoord) + float2(17.0, 31.0), float2(0.06711056, 0.00583715))));

    const uint SliceCount   = 4u;
    const uint StepsPerSide = 4u;
    float VisibilitySum = 0.0;

    for (uint SliceIdx = 0u; SliceIdx < SliceCount; ++SliceIdx)
    {
        float  SliceAngle     = (float(SliceIdx) + IgnAngle) * (PiConst / float(SliceCount));
        float2 SliceScreenDir = float2(cos(SliceAngle), sin(SliceAngle));
        float3 SliceWorldDir  = normalize(CamRight * SliceScreenDir.x + CamUp * SliceScreenDir.y);

        float3 OrthoSliceNormal = cross(SliceWorldDir, ViewFrom);
        float  OrthoLen         = length(OrthoSliceNormal);
        if (OrthoLen < 1.0e-4)
        {
            VisibilitySum += 1.0;
            continue;
        }
        OrthoSliceNormal /= OrthoLen;
        float3 SliceTangent = cross(ViewFrom, OrthoSliceNormal);

        float3 ProjNormal    = WorldNormal - OrthoSliceNormal * dot(WorldNormal, OrthoSliceNormal);
        float  ProjNormalLen = length(ProjNormal);
        if (ProjNormalLen < 1.0e-4)
        {
            VisibilitySum += 1.0;
            continue;
        }
        float3 ProjNormalDir = ProjNormal / ProjNormalLen;
        float  CosN          = clamp(dot(ProjNormalDir, ViewFrom), -1.0, 1.0);
        float  SignN         = (dot(ProjNormalDir, SliceTangent) >= 0.0) ? -1.0 : 1.0;
        float  NormalAngle   = SignN * acos(CosN);
        float  SinN          = sin(NormalAngle);

        float CosHorizonPlus  = -1.0;
        float CosHorizonMinus = -1.0;
        for (uint StepIdx = 0u; StepIdx < StepsPerSide; ++StepIdx)
        {
            float StepFrac = pow((float(StepIdx) + 0.25 + 0.70 * IgnRadial) / float(StepsPerSide), 1.35);
            float StepPx   = max(1.0, StepFrac * RadiusPx);
            int2  DeltaPx  = int2(
                int(round(SliceScreenDir.x * StepPx)),
                int(round(-SliceScreenDir.y * StepPx)));

            int2 CoordPlus = clamp(int2(PixelCoord) + DeltaPx, int2(0, 0), int2(int(Width) - 1, int(Height) - 1));
            float4 PosPlus = GBufPosMask[uint(CoordPlus.y) * Width + uint(CoordPlus.x)];
            if (PosPlus.w > 0.0)
            {
                float3 VecP  = PosPlus.xyz - WorldPos;
                float  DistP = length(VecP);
                if (DistP > 1.0e-3 && DistP < FalloffEndMeters)
                {
                    float SampleCos = dot(VecP / DistP, ViewFrom);
                    float Weight    = saturate((FalloffEndMeters - DistP) / (FalloffEndMeters - FalloffStartMeters));
                    CosHorizonPlus  = max(CosHorizonPlus, lerp(-1.0, SampleCos, Weight));
                }
            }

            int2 CoordMinus = clamp(int2(PixelCoord) - DeltaPx, int2(0, 0), int2(int(Width) - 1, int(Height) - 1));
            float4 PosMinus = GBufPosMask[uint(CoordMinus.y) * Width + uint(CoordMinus.x)];
            if (PosMinus.w > 0.0)
            {
                float3 VecM  = PosMinus.xyz - WorldPos;
                float  DistM = length(VecM);
                if (DistM > 1.0e-3 && DistM < FalloffEndMeters)
                {
                    float SampleCos = dot(VecM / DistM, ViewFrom);
                    float Weight    = saturate((FalloffEndMeters - DistM) / (FalloffEndMeters - FalloffStartMeters));
                    CosHorizonMinus = max(CosHorizonMinus, lerp(-1.0, SampleCos, Weight));
                }
            }
        }

        float H1 = NormalAngle + max(-acos(clamp(CosHorizonMinus, -1.0, 1.0)) - NormalAngle, -1.5707963);
        float H2 = NormalAngle + min( acos(clamp(CosHorizonPlus,  -1.0, 1.0)) - NormalAngle,  1.5707963);
        float Side1 = -cos(2.0 * H1 - NormalAngle) + CosN + 2.0 * H1 * SinN;
        float Side2 = -cos(2.0 * H2 - NormalAngle) + CosN + 2.0 * H2 * SinN;
        float SliceVisibility = saturate(ProjNormalLen * 0.25 * (Side1 + Side2));
        VisibilitySum += lerp(1.0, SliceVisibility, ProjNormalLen);
    }

    float Gtao = saturate(VisibilitySum / float(SliceCount));
    return pow(Gtao, clamp(QueryXeGtaoStrength(), 0.0, 2.0));
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
    uint   DebugMode        = WorldUniforms.LightGlassMaterialDebug.w;
    int    StridePx         = max(1, int(WorldUniforms.DenoiseStepParams.x));
    bool   IsFinalComposite = (WorldUniforms.DenoiseStepParams.y & 1u) != 0u;
    bool   SpatialDenoise   = (WorldUniforms.DenoiseStepParams.y & 2u) != 0u;

    float4 CenterPos  = GBufPosMask[PixelIdx];
    float4 CenterNorm = GBufNormAo[PixelIdx];
    float4 InShadAo   = PingShadowAo[PixelIdx];
    float4 InIndir    = (CenterPos.w > 0.0) ? PingIndirectIrradiance[PixelIdx] : float4(0.0);
    float4 CenterAlb  = GBufAlbedo[PixelIdx];

    float  OutSunVis     = InShadAo.x;
    float  OutDiffAo     = InShadAo.y;
    float  OutBlocker    = InShadAo.z;
    float3 OutIndirIrrad = InIndir.xyz;

    if (SpatialDenoise && CenterPos.w > 0.0)
    {
        float3 CenterN    = normalize(CenterNorm.xyz);
        float3 CamOrig    = WorldUniforms.CameraOriginTanX.xyz;
        float  CamDist    = max(0.25, length(CenterPos.xyz - CamOrig));
        float  TanSun     = max(0.004, WorldUniforms.SunDirConeTan.w);
        float  EdgeBlock  = (OutSunVis > 0.005 && OutSunVis < 0.995) ? max(OutBlocker, 0.30) : OutBlocker;
        float  Penumbra   = EdgeBlock * TanSun;
        float  SigmaSh    = clamp(1.10 + (Penumbra / CamDist) * float(Height) * 1.65, 1.10, 9.0);
        float  Inv2SigS   = 1.0 / (2.0 * SigmaSh * SigmaSh);
        float  SigmaAo    = max(1.0, float(StridePx) * 1.15);
        float  Inv2SigA   = 1.0 / (2.0 * SigmaAo * SigmaAo);
        bool   FilterGi   = (InIndir.w > 0.5);
        float  SigmaGi    = max(1.4, float(StridePx) * 1.65);
        float  Inv2SigGi  = 1.0 / (2.0 * SigmaGi * SigmaGi);

        float  SumVis     = 0.0;
        float  SumWVis    = 0.0;
        float  SumAo      = 0.0;
        float  SumWAo     = 0.0;
        float3 SumGi      = float3(0.0);
        float  SumWGi     = 0.0;
        float  MaxBlock   = OutBlocker;

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

                float DistSq    = float((KX * StridePx) * (KX * StridePx) + (KY * StridePx) * (KY * StridePx));
                float PlaneDist = abs(dot(CenterN, NPos.xyz - CenterPos.xyz));
                float WPlane    = exp(-PlaneDist * 65.0);
                float WNrm      = pow(saturate(dot(CenterN, NNrm)), 24.0);
                float WGeom     = WPlane * WNrm;

                float WVis = exp(-DistSq * Inv2SigS) * WGeom;
                SumVis  += NShAo.x * WVis;
                SumWVis += WVis;

                float WAo = exp(-DistSq * Inv2SigA) * WGeom;
                SumAo  += NShAo.y * WAo;
                SumWAo += WAo;

                if (FilterGi)
                {
                    float4 NGi  = PingIndirectIrradiance[SIdx];
                    float  WGi  = exp(-DistSq * Inv2SigGi) * exp(-PlaneDist * 38.0) * pow(saturate(dot(CenterN, NNrm)), 12.0);
                    SumGi  += NGi.xyz * WGi;
                    SumWGi += WGi;
                }

                if (PlaneDist < 0.03 && WNrm > 0.5)
                {
                    MaxBlock = max(MaxBlock, NShAo.z);
                }
            }
        }

        OutSunVis  = (SumWVis > 1.0e-6) ? (SumVis / SumWVis) : InShadAo.x;
        OutDiffAo  = (SumWAo  > 1.0e-6) ? (SumAo  / SumWAo)  : InShadAo.y;
        OutBlocker = MaxBlock;
        if (FilterGi && SumWGi > 1.0e-6)
        {
            OutIndirIrrad = SumGi / SumWGi;
        }
    }

    if (!IsFinalComposite)
    {
        PongShadowAo[PixelIdx] = float4(OutSunVis, OutDiffAo, OutBlocker, InShadAo.w);
        if (CenterPos.w > 0.0 && InIndir.w > 0.5)
        {
            PongIndirectIrradiance[PixelIdx] = float4(OutIndirIrrad, InIndir.w);
        }
        return;
    }

    float4 DirectBase = GBufDirectBase[PixelIdx];
    float4 SunContrib = GBufSunContrib[PixelIdx];
    float4 AlbedoPack = CenterAlb;
    bool   UsesShadingDebug = (DebugMode >= 8u && DebugMode <= 11u) || DebugMode == 15u;

    float3 FinalHdr = DirectBase.xyz;
    if (CenterPos.w < -0.5)
    {
        FinalHdr = UsesShadingDebug
            ? float3(0.03, 0.04, 0.07)
            : EvaluateBackgroundSkyCloudsGpu(normalize(CenterPos.xyz), float2(DispatchThreadID.xy));
    }
    else if (CenterPos.w > 0.0 && DebugMode == 0u)
    {
        float3 CamOrigin  = WorldUniforms.CameraOriginTanX.xyz;
        float3 VDir       = normalize(CamOrigin - CenterPos.xyz);
        float3 SurfN      = normalize(CenterNorm.xyz);
        float  NdotV      = max(1.0e-3, dot(SurfN, VDir));
        float4 SpecPack   = AccumulationHdrBuffer[PixelIdx];
        float  SpecRough  = clamp(SpecPack.w, 0.025, 1.0);
        float  SpecAo     = saturate(pow(NdotV + OutDiffAo, exp2(-16.0 * SpecRough - 1.0)) - 1.0 + OutDiffAo);
        float3 SkyFill    = EvaluateSkyDome(SurfN);
        uint   EngineMode = WorldUniforms.EngineShadowReflPad.x;
        float  SkyFillGain = (EngineMode >= 2u && IsGlobalDistanceFieldReady()) ? 0.08 : 0.22;
        // XeGTAO (toggleable, direct PBR only): tightens ambient/indirect occlusion, adds no GI
        float  EffectiveAo = OutDiffAo;
        if (QueryXeGtaoEnabled())
        {
            float GtaoAo  = EvaluateGroundTruthAmbientOcclusion(DispatchThreadID.xy, CenterPos.xyz, SurfN);
            EffectiveAo   = min(EffectiveAo, GtaoAo);
            SpecAo        = saturate(pow(NdotV + EffectiveAo, exp2(-16.0 * SpecRough - 1.0)) - 1.0 + EffectiveAo);
        }
        FinalHdr = DirectBase.xyz
                 + AlbedoPack.xyz * OutIndirIrrad * (0.45 + 0.55 * EffectiveAo)
                 + SpecPack.xyz * SpecAo
                 + SunContrib.xyz * OutSunVis
                 + AlbedoPack.xyz * SkyFill * SkyFillGain * (0.35 + 0.65 * EffectiveAo);
    }
    else if (CenterPos.w > 0.0 && DebugMode == 8u)
    {
        FinalHdr = float3(OutSunVis);
    }
    else if (CenterPos.w > 0.0 && DebugMode == 9u)
    {
        float EffectiveDbgAo = OutDiffAo;
        if (QueryXeGtaoEnabled())
        {
            float GtaoAo = EvaluateGroundTruthAmbientOcclusion(DispatchThreadID.xy, CenterPos.xyz, normalize(CenterNorm.xyz));
            EffectiveDbgAo = min(EffectiveDbgAo, GtaoAo);
        }
        FinalHdr = float3(EffectiveDbgAo);
    }
    else if (CenterPos.w > 0.0 && DebugMode == 15u)
    {
        // SDF AO Raw: per-pixel cone AO before any spatial filtering (compare against Debug 9)
        FinalHdr = float3(InShadAo.y);
    }
    else if (CenterPos.w > 0.0 && DebugMode == 10u)
    {
        FinalHdr = ApplyAcesFilmicGpu(DirectBase.xyz + AlbedoPack.xyz * OutIndirIrrad + SunContrib.xyz * OutSunVis);
    }
    else if (CenterPos.w > 0.0 && DebugMode == 11u)
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
        float2 PixelUv   = (float2(DispatchThreadID.xy) + float2(0.5, 0.5)) / float2(float(Width), float(Height));
        float  NdcX      = PixelUv.x * 2.0 - 1.0;
        float  NdcY      = 1.0 - PixelUv.y * 2.0;
        float3 CamOrigin = WorldUniforms.CameraOriginTanX.xyz;
        float3 RayDir    = normalize(
            WorldUniforms.CameraForwardHeight.xyz +
            WorldUniforms.CameraRightTanY.xyz * (NdcX * WorldUniforms.CameraOriginTanX.w) +
            WorldUniforms.CameraUpWidth.xyz   * (NdcY * WorldUniforms.CameraRightTanY.w));

        float HitDistForClouds = DirectBase.w;
        float HitDistForFog    = SunContrib.w;

        if (WorldUniforms.CloudParams.FormationOptics.w > 0.5 || WorldUniforms.CloudParams.WindVectorOcclude.w > 0.5)
        {
            float  LocalCloudTrans = 1.0;
            float3 LocalCloudRad   = EvaluateLocalCloudsSegmentGpu(
                WorldUniforms.CloudParams, CamOrigin, RayDir, HitDistForClouds, float2(DispatchThreadID.xy), LocalCloudTrans);
            FinalHdr = FinalHdr * LocalCloudTrans + LocalCloudRad;
        }

        if (WorldUniforms.FogParams.HeightParams0.x > 0.5 ||
            WorldUniforms.FogParams.AerialParams.x > 0.5 ||
            WorldUniforms.FogParams.FogMeta.x > 0.5)
        {
            float  FogTrans = 1.0;
            float3 FogRad   = EvaluateVolumetricFogGpu(
                WorldUniforms.FogParams,
                CamOrigin,
                RayDir,
                HitDistForFog,
                normalize(WorldUniforms.SunDirConeTan.xyz),
                WorldUniforms.SolarIrradiancePad.xyz,
                EvaluateSkyDome(RayDir),
                float2(DispatchThreadID.xy),
                FogTrans);
            FinalHdr = FinalHdr * FogTrans + FogRad;
        }

        FinalHdr += EvaluateVolumetricGodRaysGpu(
            CamOrigin,
            RayDir,
            HitDistForFog,
            normalize(WorldUniforms.SunDirConeTan.xyz),
            WorldUniforms.SolarIrradiancePad.xyz,
            float2(DispatchThreadID.xy));

        if (WorldUniforms.AtmosphereMaskPad.x != 0u && WorldUniforms.LensFlareParams.GhostRotationTint.w > 0.5)
        {
            FinalHdr += EvaluateBakedLensFlareGpu(WorldUniforms.LensFlareParams, BakedLensFlareAtlasTexels, PixelUv);
        }
    }

    float3 DisplayLinear = (DebugMode != 0u) ? FinalHdr : ApplyAcesFilmicGpu(FinalHdr);
    OutDisplayRgba8[PixelIdx] = PackLinearToSrgb8Rgba(DisplayLinear, DispatchThreadID.xy);
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

//------------------------------------------------------------------------------------------------------------------------
//                  EVENT-DRIVEN GLOBAL DISTANCE FIELD BAKE & 2-BOUNCE RADIOSITY PYRAMID KERNELS
//------------------------------------------------------------------------------------------------------------------------

float4 PointToFourBoxesDistSq(float3 P, uint EntryIndex)
{
    float4 DX = max(max(SceneBvhEntries[EntryIndex].BranchBoundsMinX - P.xxxx, P.xxxx - SceneBvhEntries[EntryIndex].BranchBoundsMaxX), float4(0.0));
    float4 DY = max(max(SceneBvhEntries[EntryIndex].BranchBoundsMinY - P.yyyy, P.yyyy - SceneBvhEntries[EntryIndex].BranchBoundsMaxY), float4(0.0));
    float4 DZ = max(max(SceneBvhEntries[EntryIndex].BranchBoundsMinZ - P.zzzz, P.zzzz - SceneBvhEntries[EntryIndex].BranchBoundsMaxZ), float4(0.0));
    return DX * DX + DY * DY + DZ * DZ;
}

float3 ClosestPointOnTriangleBary(float3 P, float3 A, float3 AB, float3 AC, out float3 OutBary)
{
    float3 AP = P - A;
    float  D1 = dot(AB, AP);
    float  D2 = dot(AC, AP);
    if (D1 <= 0.0 && D2 <= 0.0) { OutBary = float3(1.0, 0.0, 0.0); return A; }

    float3 B  = A + AB;
    float3 BP = P - B;
    float  D3 = dot(AB, BP);
    float  D4 = dot(AC, BP);
    if (D3 >= 0.0 && D4 <= D3) { OutBary = float3(0.0, 1.0, 0.0); return B; }

    float VC = D1 * D4 - D3 * D2;
    if (VC <= 0.0 && D1 >= 0.0 && D3 <= 0.0)
    {
        float V = D1 / max(1.0e-10, D1 - D3);
        OutBary = float3(1.0 - V, V, 0.0);
        return A + AB * V;
    }

    float3 C  = A + AC;
    float3 CP = P - C;
    float  D5 = dot(AB, CP);
    float  D6 = dot(AC, CP);
    if (D6 >= 0.0 && D5 <= D6) { OutBary = float3(0.0, 0.0, 1.0); return C; }

    float VB = D5 * D2 - D1 * D6;
    if (VB <= 0.0 && D2 >= 0.0 && D6 <= 0.0)
    {
        float W = D2 / max(1.0e-10, D2 - D6);
        OutBary = float3(1.0 - W, 0.0, W);
        return A + AC * W;
    }

    float VA = D3 * D6 - D5 * D4;
    if (VA <= 0.0 && (D4 - D3) >= 0.0 && (D5 - D6) >= 0.0)
    {
        float W  = (D4 - D3) / max(1.0e-10, (D4 - D3) + (D5 - D6));
        OutBary  = float3(0.0, 1.0 - W, W);
        return B + (C - B) * W;
    }

    float Denom = 1.0 / max(1.0e-10, VA + VB + VC);
    float V     = VB * Denom;
    float W     = VC * Denom;
    OutBary     = float3(1.0 - V - W, V, W);
    return A + AB * V + AC * W;
}

[shader("compute")]
[numthreads(4, 4, 4)]
void GlobalDistanceBakeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    if (any(DispatchThreadID >= uint3(GdfGridRes)))
    {
        return;
    }

    uint   CascadeIdx = min(1u, WorldUniforms.GlobalDistanceBakeControl.y);
    float4 CenterSpan = (CascadeIdx == 0u)
        ? WorldUniforms.GlobalDistanceFineCenterSpan
        : WorldUniforms.GlobalDistanceCoarseCenterSpan;
    float  HalfSpan   = max(0.25, CenterSpan.w);
    float  VoxSize    = (2.0 * HalfSpan) / float(GdfGridRes);
    float3 NormUVW    = (float3(DispatchThreadID) + 0.5) / float(GdfGridRes);
    float3 VoxelPos   = CenterSpan.xyz + (NormUVW * 2.0 - 1.0) * HalfSpan;

    uint   LinearIdx  = DispatchThreadID.z * (GdfGridRes * GdfGridRes) + DispatchThreadID.y * GdfGridRes + DispatchThreadID.x;
    uint   ObjCount   = QueryObjectDistanceInstanceCount();
    float  MaxDist    = VoxSize * 5.5;
    float  SignedDist = MaxDist;
    float3 BestNormal = float3(0.0, 0.0, 1.0);
    uint   BestMat    = 0u;
    bool   FoundSurf  = false;

    if (ObjCount > 0u)
    {
        // Fast O(Instances) GPU Global SDF composition from per-asset LocalSignedDistanceVolume bricks:
        // zero mesh-BVH traversal, zero rebaking on object move/rotate/scale, exact per-voxel material!
        float ObjVox  = VoxSize;
        bool  InBrick = false;
        float ObjDist = EvaluateObjectDistanceInstances(VoxelPos, BestNormal, BestMat, ObjVox, InBrick);
        SignedDist    = min(MaxDist, ObjDist);
        FoundSurf     = (ObjDist < MaxDist);
    }
    else
    {
        uint  EntryCount = uint(WorldUniforms.SkyRadianceBvhCount.w + 0.5);
        float BestDistSq = MaxDist * MaxDist;
        float BestAbsCos = -1.0;
        if (EntryCount > 0u)
        {
            uint Stack[24];
            int  StackTop = 0;
            Stack[0]      = 0u;

            while (StackTop >= 0)
            {
                uint CurIdx = Stack[StackTop--];
                if (CurIdx >= EntryCount) continue;

                float4 BoxDSq = PointToFourBoxesDistSq(VoxelPos, CurIdx);
                uint4  Refs   = SceneBvhEntries[CurIdx].BranchReference;
                uint4  Cnts   = SceneBvhEntries[CurIdx].BranchPrimitiveCount;

                uint4 Order = uint4(0u, 1u, 2u, 3u);
                if (BoxDSq[Order.x] < BoxDSq[Order.y]) { uint Tmp = Order.x; Order.x = Order.y; Order.y = Tmp; }
                if (BoxDSq[Order.z] < BoxDSq[Order.w]) { uint Tmp = Order.z; Order.z = Order.w; Order.w = Tmp; }
                if (BoxDSq[Order.x] < BoxDSq[Order.z]) { uint Tmp = Order.x; Order.x = Order.z; Order.z = Tmp; }
                if (BoxDSq[Order.y] < BoxDSq[Order.w]) { uint Tmp = Order.y; Order.y = Order.w; Order.w = Tmp; }
                if (BoxDSq[Order.y] < BoxDSq[Order.z]) { uint Tmp = Order.y; Order.y = Order.z; Order.z = Tmp; }

                for (int LaneIdx = 0; LaneIdx < 4; ++LaneIdx)
                {
                    uint Lane = Order[LaneIdx];
                    if (BoxDSq[Lane] > BestDistSq + 1.0e-5) continue;

                    uint Ref = Refs[Lane];
                    uint Cnt = Cnts[Lane];
                    if (Ref == 0xFFFFFFFFu) continue;

                    if (Cnt > 0u)
                    {
                        uint Start = Ref & 0x7FFFFFFFu;
                        for (uint P = 0u; P < Cnt; ++P)
                        {
                            uint   TriIdx = Start + P;
                            float3 Bary   = float3(1.0, 0.0, 0.0);
                            float3 Corner = SceneTriangles[TriIdx].Corner0Pad.xyz;
                            float3 Edge01 = SceneTriangles[TriIdx].Edge01Pad.xyz;
                            float3 Edge02 = SceneTriangles[TriIdx].Edge02Pad.xyz;
                            float3 CloseP = ClosestPointOnTriangleBary(VoxelPos, Corner, Edge01, Edge02, Bary);
                            float3 Diff   = VoxelPos - CloseP;
                            float  DistSq = dot(Diff, Diff);
                            float  TieEps = max(1.0e-6, BestDistSq * 2.5e-4);
                            if (DistSq <= BestDistSq + TieEps)
                            {
                                float4 N2Slot  = SceneTriangles[TriIdx].Normal2Slot;
                                float3 SmoothN = normalize(
                                    SceneTriangles[TriIdx].Normal0Pad.xyz * Bary.x +
                                    SceneTriangles[TriIdx].Normal1Pad.xyz * Bary.y +
                                    N2Slot.xyz * Bary.z);
                                float  InvDist = rsqrt(max(1.0e-12, DistSq));
                                float  AbsCos  = abs(dot(Diff, SmoothN) * InvDist);
                                bool StrictlyCloser = !FoundSurf || (DistSq < BestDistSq - TieEps);
                                if (StrictlyCloser || AbsCos > BestAbsCos)
                                {
                                    BestAbsCos = StrictlyCloser ? AbsCos : max(BestAbsCos, AbsCos);
                                    BestDistSq = min(BestDistSq, DistSq);
                                    BestNormal = SmoothN;
                                    BestMat    = uint(N2Slot.w + 0.5);
                                    FoundSurf  = true;
                                }
                            }
                        }
                    }
                    else if (StackTop < 22)
                    {
                        Stack[++StackTop] = Ref;
                    }
                }
            }
        }
        SignedDist = FoundSurf ? sqrt(BestDistSq) : MaxDist;
    }

    GlobalDistanceFieldBuffer[CascadeIdx * GdfVoxCnt + LinearIdx] = SignedDist;

    if (CascadeIdx == 0u)
    {
        float ShellBand = VoxSize * 0.92;
        float Opacity   = FoundSurf ? saturate(1.0 - abs(SignedDist) / ShellBand) : 0.0;
        if (Opacity > 0.01)
        {
            uint PackedNrm = PackOctNormal32(BestNormal);
            uint OpacU16   = uint(clamp(Opacity * 65535.0 + 0.5, 0.0, 65535.0));
            uint PackedMat = (BestMat & 0xFFFFu) | (OpacU16 << 16u);
            GlobalSurfaceShellBuffer[LinearIdx] = uint2(PackedNrm, PackedMat);
        }
        else
        {
            GlobalSurfaceShellBuffer[LinearIdx] = uint2(0u, 0u);
        }
    }
}

void DownsampleRadiosityMipStep(uint3 Coord, uint DstMip)
{
    uint SrcRes = 64u, SrcOff = 0u, DstRes = 32u, DstOff = 0u;
    QueryRadiosityMipLayout(DstMip - 1u, SrcRes, SrcOff);
    QueryRadiosityMipLayout(DstMip, DstRes, DstOff);
    if (any(Coord >= uint3(DstRes))) return;

    uint3  BaseSrc   = Coord * 2u;
    uint   SrcR2     = SrcRes * SrcRes;
    float4 SumPrem   = float4(0.0);
    float  ActiveCnt = 0.0;
    for (uint DZ = 0u; DZ < 2u; ++DZ)
    {
        for (uint DY = 0u; DY < 2u; ++DY)
        {
            for (uint DX = 0u; DX < 2u; ++DX)
            {
                uint3  P = BaseSrc + uint3(DX, DY, DZ);
                float4 V = UnpackHalf4(GlobalRadiosityFieldBuffer[SrcOff + P.z * SrcR2 + P.y * SrcRes + P.x]);
                SumPrem += V;
                if (V.a > 0.02) ActiveCnt += 1.0;
            }
        }
    }

    // Planar wall shells occupy ~4 of 8 child octants when 2x2x2 downsampled: divide by planar footprint (4.0)
    // so thin walls preserve full radiosity density & opacity across coarser cone-trace mips!
    float4 OutPrem = float4(0.0);
    if (ActiveCnt > 0.0 && SumPrem.a > 1.0e-4)
    {
        float3 MeanColor = SumPrem.rgb / SumPrem.a;
        float  PlanarOcc = saturate(SumPrem.a * 0.25);
        OutPrem = float4(MeanColor * PlanarOcc, PlanarOcc);
    }

    uint DstIdx = DstOff + Coord.z * (DstRes * DstRes) + Coord.y * DstRes + Coord.x;
    GlobalRadiosityFieldBuffer[DstIdx] = PackHalf4(OutPrem);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void GlobalRadiosityBakeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint BakePhase = WorldUniforms.GlobalDistanceBakeControl.x;

    // Phases 1..3 & 5..7: 2x2x2 Premultiplied Radiosity Pyramid Downsample
    if (BakePhase == 1u || BakePhase == 5u) { DownsampleRadiosityMipStep(DispatchThreadID, 1u); return; }
    if (BakePhase == 2u || BakePhase == 6u) { DownsampleRadiosityMipStep(DispatchThreadID, 2u); return; }
    if (BakePhase == 3u || BakePhase == 7u) { DownsampleRadiosityMipStep(DispatchThreadID, 3u); return; }

    if (any(DispatchThreadID >= uint3(GdfGridRes)))
    {
        return;
    }

    uint  LinearIdx = DispatchThreadID.z * (GdfGridRes * GdfGridRes) + DispatchThreadID.y * GdfGridRes + DispatchThreadID.x;
    uint2 ShellMeta = GlobalSurfaceShellBuffer[LinearIdx];
    float Opacity   = float((ShellMeta.y >> 16u) & 0xFFFFu) * (1.0 / 65535.0);
    if (Opacity <= 0.01)
    {
        if (BakePhase == 0u)
        {
            GlobalRadiosityFieldBuffer[RadMip0Off + LinearIdx] = uint2(0u, 0u);
        }
        return;
    }

    float4 CenterSpan = WorldUniforms.GlobalDistanceFineCenterSpan;
    float  HalfSpan   = max(0.25, CenterSpan.w);
    float3 NormUVW    = (float3(DispatchThreadID) + 0.5) / float(GdfGridRes);
    float3 VoxelPos   = CenterSpan.xyz + (NormUVW * 2.0 - 1.0) * HalfSpan;
    float  SignedDist = GlobalDistanceFieldBuffer[LinearIdx];
    float3 SurfN      = UnpackOctNormal32(ShellMeta.x);
    float3 SurfPos    = VoxelPos - SurfN * clamp(SignedDist, -HalfSpan, HalfSpan);

    uint MatCount = max(1u, WorldUniforms.LightGlassMaterialDebug.z);
    uint MatSlot  = min(ShellMeta.y & 0xFFFFu, MatCount - 1u);
    GpuWorldMaterial80 Mat = MaterialEntries[MatSlot];

    float3 DiffAlbedo = Mat.DiffuseAlbedoRoughness.xyz *
                        (1.0 - saturate(Mat.SpecParams.y)) *
                        (1.0 - saturate(Mat.CoatRoughnessOptics.y));

    if (BakePhase == 0u)
    {
        // Phase 0: Direct Sun + Area Quad LTC + .ies Photometric + Material Blackbody Emission -> Mip 0
        float3 OutgoingRad = Mat.EmissiveRadiancePad.xyz;
        float3 SunDir      = WorldUniforms.SunDirConeTan.xyz;
        float3 SunIrr      = WorldUniforms.SolarIrradiancePad.xyz;
        float  SunVis      = SampleCascadedSunShadowFast(SurfPos, SurfN, SurfN, SunDir);
        if (SunVis > 1.0e-3)
        {
            float CloudTr = EvaluateSolarCloudTransmittanceGpu(SurfPos, SunDir);
            OutgoingRad += DiffAlbedo * (InvPiConst * max(0.0, dot(SurfN, SunDir)) * SunVis * CloudTr) * SunIrr;
        }

        uint   LightCnt = min(4u, WorldUniforms.LightGlassMaterialDebug.x);
        float3 SafePos  = SurfPos + SurfN * 0.015;
        for (uint L = 0u; L < LightCnt; ++L)
        {
            OutgoingRad += EvaluateAreaQuadLightGdfFast(WorldUniforms.AreaLights[L], SafePos, SurfN, SurfN, SurfN, Mat);
        }

        uint PackedPhotoCounts = WorldUniforms.DenoiseStepParams.z;
        uint PhotoProfileCount = PackedPhotoCounts >> 16u;
        uint PhotoLampCount    = PackedPhotoCounts & 0xFFFFu;
        if (PhotoProfileCount > 0u && PhotoLampCount > 0u)
        {
            for (uint PL = 0u; PL < PhotoLampCount; ++PL)
            {
                OutgoingRad += EvaluatePhotometricLampGpu(PhotometricLampEntries[PL], PhotoProfileCount, SafePos, SurfN, SurfN, Mat, true);
            }
        }

        OutgoingRad = min(max(float3(0.0), OutgoingRad), float3(24.0));
        GlobalRadiosityFieldBuffer[RadMip0Off + LinearIdx] = PackHalf4(float4(OutgoingRad * Opacity, Opacity));
        return;
    }

    if (BakePhase == 4u && dot(DiffAlbedo, float3(1.0)) > 1.0e-4)
    {
        // Phase 4: 2nd-Bounce Radiosity Gather (4 wide cosine cones sampling Mips 1..3)
        float3 TAxis, BAxis;
        ConstructOrthonormalBasis(SurfN, TAxis, BAxis);
        float  Vox0   = (2.0 * HalfSpan) / float(GdfGridRes);
        float3 StartP = SurfPos + SurfN * (Vox0 * 1.45);
        float3 Bounce1Irr = float3(0.0);
        [loop]
        for (uint ConeIdx = 0u; ConeIdx < 4u; ++ConeIdx)
        {
            float3 TangentDir = (ConeIdx == 0u) ? TAxis : ((ConeIdx == 1u) ? -TAxis : ((ConeIdx == 2u) ? BAxis : -BAxis));
            float3 ConeDir    = normalize(SurfN * 0.7071 + TangentDir * 0.7071);
            Bounce1Irr += 0.25 * TraceSingleRadiosityCone(StartP, ConeDir, 0.72, 4u);
        }

        float4 PrevPrem = UnpackHalf4(GlobalRadiosityFieldBuffer[RadMip0Off + LinearIdx]);
        float3 BaseRad  = (PrevPrem.a > 1.0e-4) ? (PrevPrem.rgb / PrevPrem.a) : float3(0.0);
        float3 TotalRad = min(BaseRad + DiffAlbedo * Bounce1Irr, float3(28.0));
        GlobalRadiosityFieldBuffer[RadMip0Off + LinearIdx] = PackHalf4(float4(TotalRad * Opacity, Opacity));
    }
}
