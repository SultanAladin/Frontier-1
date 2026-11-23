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
    float4                      GlobalDistanceFineCenterSpan;     // xyz = FineCenterWorld,   w = FineHalfSpanMeters
    float4                      GlobalDistanceCoarseCenterSpan;   // xyz = CoarseCenterWorld, w = CoarseHalfSpanMeters
    uint4                       GlobalDistanceBakeControl;        // x = BakePhase, y = CascadeIdx, z = GdfReadyMask, w = XeGTAO (bit0 enable, bits 8..15 strength)
    int4                        GlobalDistanceFineToroidalCell;   // xyz = FineMinCellWorld,   w = Reserved
    int4                        GlobalDistanceCoarseToroidalCell; // xyz = CoarseMinCellWorld, w = Reserved
    uint4                       GlobalDistanceSlabBounds;         // xyz = (SlabMin | (SlabMax << 16)), w = SlabActiveFlag
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

struct GpuDynamicLocalSdfInstance144
{
    float4 WorldToLocalRow0;     // xyz = R^T row 0 / scale, w = -dot(row0, T)
    float4 WorldToLocalRow1;     // xyz = R^T row 1 / scale, w = -dot(row1, T)
    float4 WorldToLocalRow2;     // xyz = R^T row 2 / scale, w = -dot(row2, T)
    float4 LocalToWorldNormRow0; // xyz = R row 0,           w = UniformWorldScale
    float4 LocalToWorldNormRow1; // xyz = R row 1,           w = VoxelSpacing.z
    float4 LocalToWorldNormRow2; // xyz = R row 2,           w = Reserved
    float4 LocalMinPad;          // xyz = LocalMin,          w = VoxelSpacing.x
    float4 LocalMaxPad;          // xyz = LocalMax / HalfExt,w = VoxelSpacing.y
    uint4  ResolutionOffset;     // x = ResX|(ResY<<16), y = ResZ|(ShapeCode<<16), z = VoxelFloatOffset, w = MaterialSlot|(Active<<16)
};

[[vk::binding(0,  0)]] ConstantBuffer<WorldViewportUniformBlock>     WorldUniforms;
[[vk::binding(1,  0)]] StructuredBuffer<GpuWideTreeEntry128>         SceneBvhEntries;
[[vk::binding(2,  0)]] StructuredBuffer<GpuShadedTriangle96>         SceneTriangles;
[[vk::binding(3,  0)]] StructuredBuffer<GpuWorldMaterial80>          MaterialEntries;
[[vk::binding(4,  0)]] RWStructuredBuffer<uint>                      OutDisplayRgba8;
[[vk::binding(5,  0)]] RWStructuredBuffer<float>                     SolarShadowDepthBuffer;
[[vk::binding(6,  0)]] RWStructuredBuffer<float4>                    AccumulationHdrBuffer;
[[vk::binding(7,  0)]] StructuredBuffer<float4>                      AtmosphericSkyViewLut;
[[vk::binding(8,  0)]] StructuredBuffer<uint>                        LunarSurfaceAtlasTexels;
[[vk::binding(9,  0)]] StructuredBuffer<uint>                        BakedLensFlareAtlasTexels;
[[vk::binding(10, 0)]] RWStructuredBuffer<float4>                    GBufPosMask;
[[vk::binding(11, 0)]] RWStructuredBuffer<float4>                    GBufNormAo;
[[vk::binding(12, 0)]] RWStructuredBuffer<float4>                    GBufDirectBase;
[[vk::binding(13, 0)]] RWStructuredBuffer<float4>                    GBufSunContrib;
[[vk::binding(14, 0)]] RWStructuredBuffer<float4>                    GBufAlbedo;
[[vk::binding(15, 0)]] RWStructuredBuffer<float4>                    PingShadowAo;
[[vk::binding(16, 0)]] RWStructuredBuffer<float4>                    PongShadowAo;
[[vk::binding(17, 0)]] RWStructuredBuffer<float4>                    CloudRadiometricBuffer;
[[vk::binding(18, 0)]] StructuredBuffer<GpuProjectedStar>            ProjectedCatalogStars;
[[vk::binding(19, 0)]] StructuredBuffer<uint>                        StellarTileBins;
[[vk::binding(22, 0)]] StructuredBuffer<float>                       PhotometricProfileTables;
[[vk::binding(23, 0)]] StructuredBuffer<GpuPhotometricLamp64>        PhotometricLampEntries;
[[vk::binding(24, 0)]] RWStructuredBuffer<float>                     GlobalDistanceFieldBuffer;
[[vk::binding(25, 0)]] RWStructuredBuffer<uint2>                     GlobalRadiosityFieldBuffer;
[[vk::binding(26, 0)]] RWStructuredBuffer<float4>                    PingIndirectIrradiance;
[[vk::binding(27, 0)]] RWStructuredBuffer<float4>                    PongIndirectIrradiance;
[[vk::binding(28, 0)]] RWStructuredBuffer<uint2>                     GlobalSurfaceShellBuffer;
[[vk::binding(29, 0)]] StructuredBuffer<float>                       LocalAssetSdfVoxelBuffer;
[[vk::binding(30, 0)]] StructuredBuffer<GpuDynamicLocalSdfInstance144> DynamicLocalSdfInstanceBuffer;
[[vk::binding(31, 0)]] StructuredBuffer<uint>                        ActiveClusterBitmask;

bool QueryClusterBitActive(uint ClusterOrdinal)
{
    uint WordIdx = ClusterOrdinal >> 5u;
    uint BitMask = 1u << (ClusterOrdinal & 31u);
    return (ActiveClusterBitmask[WordIdx] & BitMask) != 0u;
}

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
    uint   LodLevel;        // Nanite pyramid level of the hit triangle
    uint   MaxLodLevel;     // Coarsest pyramid level of the hit asset
    uint   ClusterOrdinal;  // Global cluster ordinal
    uint   GroupOrdinal;    // Simplification group ordinal
    uint   TriangleOrdinal; // Global triangle ordinal
    uint   SeamEdgeMask;    // 3-bit mask of open/material/UV seam edges
    float  SelfErrorMeters; // World-space simplification error of producing group
    float  EdgeDistMeters;  // Analytical world distance to nearest triangle edge
    float  SeamDistMeters;  // Analytical world distance to nearest seam edge
    float2 SurfaceUv;       // Interpolated parameterized UV coordinate
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

    uint Stack[32];
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

            uint Ref = Refs[Lane];
            uint Cnt = Cnts[Lane];
            if (Ref == 0xFFFFFFFFu) continue;

            uint GatePlusOne = Cnt >> 4u;
            if (GatePlusOne != 0u && !QueryClusterBitActive(GatePlusOne - 1u)) continue;

            uint TriCnt = Cnt & 0xFu;
            if (TriCnt > 0u)
            {
                uint Start = Ref & 0x7FFFFFFFu;
                for (uint P = 0u; P < TriCnt; ++P)
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
                        float3 RawCross = cross(Edge01, Edge02);
                        float  Area2    = length(RawCross);
                        float3 RawGeom  = (Area2 > 1.0e-12) ? (RawCross / Area2) : float3(0.0, 0.0, 1.0);
                        float3 SmoothN  = normalize(
                            SceneTriangles[TriIdx].Normal0Pad.xyz * W0 +
                            SceneTriangles[TriIdx].Normal1Pad.xyz * BaryUV.x +
                            N2Slot.xyz * BaryUV.y);
                        if (dot(RawGeom, Dir) > 0.0)
                        {
                            RawGeom = -RawGeom;
                            SmoothN = -SmoothN;
                        }
                        if (dot(SmoothN, -Dir) < 0.0)
                        {
                            // Grazing silhouette normal guard (GeometryCompiler ViewportRasterization.js parity)
                            SmoothN = normalize(SmoothN - Dir * (dot(SmoothN, -Dir) - 0.02));
                        }

                        uint   MetaWord = asuint(SceneTriangles[TriIdx].Corner0Pad.w) & 0x3FFFFFFFu;
                        uint   UvWordA  = asuint(SceneTriangles[TriIdx].Normal0Pad.w) & 0x3FFFFFFFu;
                        uint   UvWordB  = asuint(SceneTriangles[TriIdx].Normal1Pad.w) & 0x3FFFFFFFu;
                        float2 Uv0      = float2(float(UvWordA & 0x3FFu), float((UvWordA >> 10u) & 0x3FFu)) * (4.0 / 1023.0);
                        float2 Uv1      = float2(float((UvWordA >> 20u) & 0x3FFu), float(UvWordB & 0x3FFu)) * (4.0 / 1023.0);
                        float2 Uv2      = float2(float((UvWordB >> 10u) & 0x3FFu), float((UvWordB >> 20u) & 0x3FFu)) * (4.0 / 1023.0);

                        float  DistE01  = BaryUV.y * Area2 / max(1.0e-6, length(Edge01));
                        float  DistE12  = W0 * Area2 / max(1.0e-6, length(Edge02 - Edge01));
                        float  DistE20  = BaryUV.x * Area2 / max(1.0e-6, length(Edge02));
                        uint   SeamMask = (MetaWord >> 8u) & 0x7u;
                        float  SeamDist = 1.0e9;
                        if ((SeamMask & 1u) != 0u) SeamDist = min(SeamDist, DistE01);
                        if ((SeamMask & 2u) != 0u) SeamDist = min(SeamDist, DistE12);
                        if ((SeamMask & 4u) != 0u) SeamDist = min(SeamDist, DistE20);

                        Res.GeomNormal      = RawGeom;
                        Res.ShadingNormal   = SmoothN;
                        Res.MaterialSlot    = uint(N2Slot.w + 0.5);
                        Res.LodLevel        = MetaWord & 0xFu;
                        Res.MaxLodLevel     = (MetaWord >> 4u) & 0xFu;
                        Res.SeamEdgeMask    = SeamMask;
                        Res.GroupOrdinal    = (MetaWord >> 11u) & 0x7FFFFu;
                        Res.ClusterOrdinal  = uint(SceneTriangles[TriIdx].Edge01Pad.w + 0.5);
                        Res.TriangleOrdinal = TriIdx;
                        Res.SelfErrorMeters = max(0.0, SceneTriangles[TriIdx].Edge02Pad.w);
                        Res.EdgeDistMeters  = min(DistE01, min(DistE12, DistE20));
                        Res.SeamDistMeters  = SeamDist;
                        Res.SurfaceUv       = Uv0 * W0 + Uv1 * BaryUV.x + Uv2 * BaryUV.y;
                    }
                }
            }
            else if (StackTop < 28)
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

    uint Stack[32];
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

            uint GatePlusOne = Cnt >> 4u;
            if (GatePlusOne != 0u && !QueryClusterBitActive(GatePlusOne - 1u)) continue;

            uint TriCnt = Cnt & 0xFu;
            if (TriCnt > 0u)
            {
                uint Start = Ref & 0x7FFFFFFFu;
                for (uint P = 0u; P < TriCnt; ++P)
                {
                    if (IntersectTriangleShadowMT(Origin, Dir, MinT, MaxT, Start + P))
                    {
                        return true;
                    }
                }
            }
            else if (StackTop < 28)
            {
                Stack[++StackTop] = Ref;
            }
        }
    }
    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                  BAKED GLOBAL DISTANCE FIELD (2-CASCADE 64^3) & 2-BOUNCE 3D RADIOSITY PYRAMID
//------------------------------------------------------------------------------------------------------------------------

bool IsGlobalDistanceFieldReady()
{
    return (WorldUniforms.GlobalDistanceBakeControl.z != 0u) &&
           (WorldUniforms.GlobalDistanceFineCenterSpan.w > 0.05);
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

    float3 LogicalNorm = (WorldPos - CenterSpan.xyz) / (2.0 * HalfSpan) + 0.5;
    float  Margin      = 1.25 / float(GdfGridRes);
    if (any(LogicalNorm < float3(Margin)) || any(LogicalNorm > float3(1.0 - Margin)))
    {
        OutInBounds = false;
        return HalfSpan;
    }
    OutInBounds = true;

    int3 MinCell = (CascadeIdx == 0u)
        ? WorldUniforms.GlobalDistanceFineToroidalCell.xyz
        : WorldUniforms.GlobalDistanceCoarseToroidalCell.xyz;

    // Toroidal frac() wrap-around addressing over physical [0..63]^3 ring storage
    float3 ToroidalNorm = frac(LogicalNorm + float3(MinCell & 63) * (1.0 / float(GdfGridRes)));
    float3 ToroidalGrid = ToroidalNorm * float(GdfGridRes) - 0.5;
    int3   BaseCell     = int3(floor(ToroidalGrid));
    float3 Fr           = saturate(ToroidalGrid - float3(BaseCell));
    uint3  P0           = uint3(BaseCell & 63);
    uint3  P1           = uint3((BaseCell + int3(1)) & 63);

    uint BaseOff = CascadeIdx * GdfVoxCnt;
    uint R       = GdfGridRes;
    uint R2      = GdfGridRes * GdfGridRes;

    float C000 = GlobalDistanceFieldBuffer[BaseOff + P0.z * R2 + P0.y * R + P0.x];
    float C100 = GlobalDistanceFieldBuffer[BaseOff + P0.z * R2 + P0.y * R + P1.x];
    float C010 = GlobalDistanceFieldBuffer[BaseOff + P0.z * R2 + P1.y * R + P0.x];
    float C110 = GlobalDistanceFieldBuffer[BaseOff + P0.z * R2 + P1.y * R + P1.x];
    float C001 = GlobalDistanceFieldBuffer[BaseOff + P1.z * R2 + P0.y * R + P0.x];
    float C101 = GlobalDistanceFieldBuffer[BaseOff + P1.z * R2 + P0.y * R + P1.x];
    float C011 = GlobalDistanceFieldBuffer[BaseOff + P1.z * R2 + P1.y * R + P0.x];
    float C111 = GlobalDistanceFieldBuffer[BaseOff + P1.z * R2 + P1.y * R + P1.x];

    float C00 = lerp(C000, C100, Fr.x);
    float C10 = lerp(C010, C110, Fr.x);
    float C01 = lerp(C001, C101, Fr.x);
    float C11 = lerp(C011, C111, Fr.x);
    return lerp(lerp(C00, C10, Fr.y), lerp(C01, C11, Fr.y), Fr.z);
}

float SampleGlobalDistanceWorld(float3 WorldPos, out float OutLocalVoxelSize)
{
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
    if (!IsGlobalDistanceFieldReady() || MaxDist <= 0.12)
    {
        return 1.0;
    }

    float  Vox0      = QueryFineGdfVoxelSize();
    float  StartLift = Vox0 * 0.68;
    float3 StartP    = Origin + GeomN * StartLift;
    float  NdotD     = max(0.0, dot(GeomN, Dir));
    float  EndMargin = max(0.16, Vox0 * 2.15);
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

        // Receiver tangent-plane clearance: ignore distance readings explained by the receiver surface itself
        float SelfPlaneDist = StartLift + T * NdotD;
        bool  IsReceiverSelf = (Dist >= SelfPlaneDist - LocalVox * 0.42);

        if (PointHard)
        {
            if (!IsReceiverSelf)
            {
                if (Dist < -LocalVox * 0.15) return 0.0;
                float HardBand = saturate((Dist + LocalVox * 0.10) / max(1.0e-3, LocalVox * 0.32));
                MinVis = min(MinVis, HardBand);
            }
            T += max(LocalVox * 0.60, max(0.0, Dist) * 0.88);
        }
        else
        {
            float RemSpan  = max(1.0e-3, SafeMax - T);
            float Aperture = max(LocalVox * 0.24, min(T * max(0.020, ConeTan), RemSpan * 0.55));
            if (!IsReceiverSelf)
            {
                float ConeVis = saturate((Dist + Aperture * 0.15) / (1.15 * Aperture));
                MinVis = min(MinVis, ConeVis);
                if (MinVis <= 0.01) return 0.0;
            }
            T += max(LocalVox * 0.62, max(max(0.0, Dist) * 0.82, Aperture * 0.50));
        }
    }
    return PointHard ? smoothstep(0.25, 0.75, MinVis) : (MinVis * MinVis * (3.0 - 2.0 * MinVis));
}

// Distance-attenuated hemisphere cone AO over the baked Global Distance Field:
// 1 axial cone + 4 cones tilted 48 deg from the surface normal, bounded to a physical contact
// reach (~0.85 m, matching the BVH4 raytraced AO reach) with inverse-distance quadratic falloff
// so occlusion hugs tyre contact patches, wheel wells, undercarriage crevices without leaking
// a 2-meter grey skirt out across open ground planes.
float TraceGlobalDistanceAmbientCone(
    float3 Origin,
    float3 GeomN,
    float3 Dir,
    float  MaxDist,
    float  ConeTan)
{
    float  Vox0    = QueryFineGdfVoxelSize();
    float  BaseVox = Vox0;
    (void)SampleGlobalDistanceWorld(Origin, BaseVox);
    float  StartLift = BaseVox * 0.58;
    float3 StartP    = Origin + GeomN * StartLift;
    float  NdotD     = max(0.0, dot(GeomN, Dir));
    float  T         = BaseVox * 0.58;
    float  MaxOcc    = 0.0;

    [loop]
    for (uint Step = 0u; Step < 8u; ++Step)
    {
        if (T >= MaxDist) break;
        float LocalVox = BaseVox;
        float Dist     = SampleGlobalDistanceWorld(StartP + Dir * T, LocalVox);

        float SelfPlaneDist  = StartLift + T * NdotD;
        bool  ReceiverClear  = (Dist >= SelfPlaneDist - LocalVox * 0.42);
        float Aperture       = max(LocalVox * 0.22, T * ConeTan);
        if (!ReceiverClear)
        {
            float RawOcc   = 1.0 - saturate((Dist + Aperture * 0.14) / (1.08 * Aperture));
            float NormDist = saturate(T / MaxDist);
            float Falloff  = (1.0 - NormDist * NormDist) / (1.0 + 1.85 * T * T);
            MaxOcc         = max(MaxOcc, RawOcc * Falloff);
            if (MaxOcc >= 0.96) break;
        }
        T += max(LocalVox * 0.52, max(max(0.0, Dist) * 0.80, Aperture * 0.55));
    }
    return 1.0 - saturate(MaxOcc);
}

float EvaluateGlobalDistanceAmbientOcclusion(float3 Pos, float3 N, float3 GeomN)
{
    if (!IsGlobalDistanceFieldReady()) return 1.0;
    float4 CoarseSpan    = WorldUniforms.GlobalDistanceCoarseCenterSpan;
    float  CoarseHalf    = max(0.5, CoarseSpan.w);
    float3 CoarseRel     = abs(Pos - CoarseSpan.xyz) / CoarseHalf;
    float  CascadeWeight = smoothstep(0.96, 0.72, max(CoarseRel.x, max(CoarseRel.y, CoarseRel.z)));
    if (CascadeWeight <= 1.0e-3) return 1.0;

    float Vox0   = QueryFineGdfVoxelSize();
    float Radius = clamp(Vox0 * 6.5, 0.45, 0.88);

    float3 TangentAxis = (abs(N.z) < 0.92) ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
    float3 Tangent     = normalize(cross(TangentAxis, N));
    float3 Bitangent   = cross(N, Tangent);

    // Axial cone (weight 1.0) + 4 cones tilted ~48 deg (cos 48 = 0.67, sin 48 = 0.74)
    float VisSum    = TraceGlobalDistanceAmbientCone(Pos, GeomN, N, Radius, 0.32);
    float WeightSum = 1.0;
    for (uint ConeIdx = 0u; ConeIdx < 4u; ++ConeIdx)
    {
        float  Azimuth = (float(ConeIdx) + 0.5) * 1.5707963;
        float3 TiltDir = normalize(N * 0.67 + (Tangent * cos(Azimuth) + Bitangent * sin(Azimuth)) * 0.74);
        VisSum    += 0.67 * TraceGlobalDistanceAmbientCone(Pos, GeomN, TiltDir, Radius, 0.36);
        WeightSum += 0.67;
    }
    float Ao    = VisSum / WeightSum;
    float RawAo = clamp(1.0 - (1.0 - Ao) * 1.32, 0.04, 1.0);
    return lerp(1.0, RawAo, CascadeWeight);
}

// Central-difference gradient of the Global Distance Field: the SDF surface normal at any point.
float3 EvaluateGlobalDistanceGradientNormal(float3 Pos, float3 FallbackNormal)
{
    if (!IsGlobalDistanceFieldReady()) return FallbackNormal;
    float Eps = max(0.01, QueryFineGdfVoxelSize() * 0.75);
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

// 0 = fine cascade, 1 = coarse cascade, 2 = outside both cascades (for the SDF Cascade debug view)
uint QueryGlobalDistanceCascadeIndex(float3 Pos)
{
    bool InFine = false;
    float FineProbe = SampleGlobalDistanceCascade(0u, Pos, InFine);
    (void)FineProbe;
    if (InFine) return 0u;
    bool InCoarse = false;
    float CoarseProbe = SampleGlobalDistanceCascade(1u, Pos, InCoarse);
    (void)CoarseProbe;
    return InCoarse ? 1u : 2u;
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

    uint   Res = 64u, BaseOff = 0u;
    QueryRadiosityMipLayout(MipIdx, Res, BaseOff);

    uint3  P0 = uint3(0u);
    uint3  P1 = uint3(0u);
    float3 Fr = float3(0.0);
    if (MipIdx == 0u)
    {
        float  HalfTexel    = 0.5 / float(GdfGridRes);
        float3 ClampedNorm  = clamp(NormCoord, float3(HalfTexel), float3(1.0 - HalfTexel));
        int3   FineMinCell  = WorldUniforms.GlobalDistanceFineToroidalCell.xyz;
        float3 ToroidalNorm = frac(ClampedNorm + float3(FineMinCell & 63) * (1.0 / float(GdfGridRes)));
        float3 ToroidalGrid = ToroidalNorm * float(GdfGridRes) - 0.5;
        int3   BaseCell     = int3(floor(ToroidalGrid));
        Fr                  = saturate(ToroidalGrid - float3(BaseCell));
        P0                  = uint3(BaseCell & 63);
        P1                  = uint3((BaseCell + int3(1)) & 63);
    }
    else
    {
        float3 GridF = NormCoord * float(Res) - 0.5;
        int3   L0    = clamp(int3(floor(GridF)), int3(0), int3(int(Res) - 1));
        int3   L1    = min(L0 + int3(1), int3(int(Res) - 1));
        Fr           = saturate(GridF - float3(L0));
        P0           = uint3(L0);
        P1           = uint3(L1);
    }
    uint R2 = Res * Res;

    float4 V000 = UnpackHalf4(GlobalRadiosityFieldBuffer[BaseOff + P0.z * R2 + P0.y * Res + P0.x]);
    float4 V100 = UnpackHalf4(GlobalRadiosityFieldBuffer[BaseOff + P0.z * R2 + P0.y * Res + P1.x]);
    float4 V010 = UnpackHalf4(GlobalRadiosityFieldBuffer[BaseOff + P0.z * R2 + P1.y * Res + P0.x]);
    float4 V110 = UnpackHalf4(GlobalRadiosityFieldBuffer[BaseOff + P0.z * R2 + P1.y * Res + P1.x]);
    float4 V001 = UnpackHalf4(GlobalRadiosityFieldBuffer[BaseOff + P1.z * R2 + P0.y * Res + P0.x]);
    float4 V101 = UnpackHalf4(GlobalRadiosityFieldBuffer[BaseOff + P1.z * R2 + P0.y * Res + P1.x]);
    float4 V011 = UnpackHalf4(GlobalRadiosityFieldBuffer[BaseOff + P1.z * R2 + P1.y * Res + P0.x]);
    float4 V111 = UnpackHalf4(GlobalRadiosityFieldBuffer[BaseOff + P1.z * R2 + P1.y * Res + P1.x]);

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

float ComputeRadiosityVolumeFade(float3 WorldPos)
{
    float4 CenterSpan = WorldUniforms.GlobalDistanceFineCenterSpan;
    if (CenterSpan.w <= 0.05)
    {
        return 0.0;
    }
    float3 Rel    = abs(WorldPos - CenterSpan.xyz) / CenterSpan.w;
    float  MaxRel = max(Rel.x, max(Rel.y, Rel.z));
    return smoothstep(0.95, 0.60, MaxRel);
}

float3 TraceSingleRadiosityCone(
    float3 Origin,
    float3 GeomN,
    float3 StartP,
    float3 ConeDir,
    float  TanHalfAngle,
    uint   MaxSteps,
    float  SkyFallbackScale)
{
    float  OriginFade = ComputeRadiosityVolumeFade(Origin);
    if (OriginFade <= 1.0e-3)
    {
        return EvaluateSkyDome(ConeDir) * SkyFallbackScale;
    }

    float  Vox0     = QueryFineGdfVoxelSize();
    float  MaxTrace = max(1.2, WorldUniforms.GlobalDistanceFineCenterSpan.w * 2.15);
    float  T        = Vox0 * 1.10;
    float3 AccumRad = float3(0.0);
    float  AccumOcc = 0.0;

    [loop]
    for (uint S = 0u; S < MaxSteps; ++S)
    {
        if (T >= MaxTrace || AccumOcc >= 0.96) break;
        float3 SampleP    = StartP + ConeDir * T;
        float  SampleFade = ComputeRadiosityVolumeFade(SampleP);
        if (SampleFade <= 1.0e-3)
        {
            break;
        }
        float  Diameter = max(Vox0, 2.0 * TanHalfAngle * T);
        uint   MipIdx   = min(3u, uint(clamp(log2(max(1.0, Diameter / Vox0)) + 0.45, 0.0, 3.0)));
        bool   InBounds = false;
        float4 VoxPrem  = SampleRadiosityMipTrilinear(MipIdx, SampleP, InBounds);
        if (!InBounds)
        {
            break;
        }

        bool  InSdf          = false;
        float SdfD           = SampleGlobalDistanceCascade(0u, SampleP, InSdf);
        float MipVox         = Vox0 * exp2(float(MipIdx));
        float PlaneClearance = dot(SampleP - Origin, GeomN);
        // Reject coplanar receiver self-bleed from 2x2x2 mip dilation on flat ground/walls,
        // while preserving full bounce whenever a non-receiver surface is closer than the plane.
        float HeightClear     = smoothstep(MipVox * 0.78, MipVox * 1.70, PlaneClearance);
        float OtherGeomWeight = InSdf ? smoothstep(Vox0 * 0.35, Vox0 * 1.15, PlaneClearance - SdfD) : 0.0;
        float Weight          = OriginFade * SampleFade * max(HeightClear, OtherGeomWeight);

        float Trans = 1.0 - AccumOcc;
        AccumRad   += Trans * (VoxPrem.rgb * Weight);
        AccumOcc    = saturate(AccumOcc + Trans * saturate(VoxPrem.a * Weight));

        float StepAdv = max(Diameter * 0.72, InSdf ? max(Vox0 * 0.85, SdfD * 0.62) : Diameter);
        T += StepAdv;
    }

    if (AccumOcc < 0.99)
    {
        AccumRad += (1.0 - AccumOcc) * EvaluateSkyDome(ConeDir) * SkyFallbackScale;
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
        GiIrr += ConeWt * TraceSingleRadiosityCone(Pos, GeomN, StartP, ConeDir, 0.577, Steps, 0.34);
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
    return TraceSingleRadiosityCone(Pos, GeomN, StartP, ReflDir, ConeTan, 6u, 1.0);
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
        [loop]
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

// Chiang/Estevez smooth-normal shadow-terminator offset: lifts origins off tessellated curved surfaces
// proportionally to (1 - dot(GeomN, ShadingN)) so smooth-shaded triangles never self-occlude their own chord.
float3 ComputeSmoothSurfaceRayOrigin(float3 WorldPos, float3 GeomN, float3 ShadingN, float NominalBias)
{
    float CosDiff = saturate(dot(GeomN, ShadingN));
    float SinSq   = max(0.0, 1.0 - CosDiff * CosDiff);
    float CurvatureLift = 0.018 * SinSq;
    return WorldPos + GeomN * (NominalBias + CurvatureLift) + ShadingN * (0.35 * NominalBias);
}

float EvaluateSmoothTerminatorFade(float3 GeomN, float3 ShadingN, float3 LightDir)
{
    float NdotL = dot(ShadingN, LightDir);
    if (NdotL <= 0.0) return 0.0;
    float GdotL = dot(GeomN, LightDir);
    float GdotN = max(0.15, dot(GeomN, ShadingN));
    if (GdotL >= GdotN) return 1.0;
    float T = saturate((GdotL + 0.08) / max(0.08, GdotN + 0.08));
    return T * T * (3.0 - 2.0 * T);
}

float SampleCascadedSunShadowFast(float3 WorldPos, float3 GeomN, float3 N, float3 SunDir)
{
    float NdotL = dot(N, SunDir);
    if (NdotL <= 1.0e-5) return 0.0;
    float TermFade = EvaluateSmoothTerminatorFade(GeomN, N, SunDir);
    if (TermFade <= 1.0e-4) return 0.0;

    float2 CenterTXY;
    float  RecD, TexelM;
    uint   Res, BufOffs;
    float3 BiasedPos = ComputeSmoothSurfaceRayOrigin(WorldPos, GeomN, N, 0.008);
    if (ProjectBestSunCascade(BiasedPos, CenterTXY, RecD, TexelM, Res, BufOffs))
    {
        float EffNdotL   = max(0.18, min(NdotL, max(0.0, dot(GeomN, SunDir))));
        float SlopeScale = min(4.0, sqrt(max(0.0, 1.0 - EffNdotL * EffNdotL)) / EffNdotL);
        float Bias       = TexelM * (1.40 + 0.95 * SlopeScale);
        return SampleShadowVisBilinear(CenterTXY, RecD, Bias, Res, BufOffs) * TermFade;
    }
    if (IsGlobalDistanceFieldReady())
    {
        return TraceGlobalDistanceConeVisibility(BiasedPos, GeomN, SunDir, 12.0, max(0.01, WorldUniforms.SunDirConeTan.w), false, 8u) * TermFade;
    }
    return (TraceSceneAnyHit(BiasedPos + GeomN * 0.010, SunDir, 0.02, 80.0) ? 0.0 : 1.0) * TermFade;
}

bool QueryCascadeCardinalOpen(float2 CenterTXY, float RadiusPx, float RecD, float Bias, uint Res, uint BufOffs)
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
    float TermFade  = EvaluateSmoothTerminatorFade(Hit.GeomNormal, Hit.ShadingNormal, SunDir);
    if (TermFade <= 1.0e-4) return 0.0;

    float  TanSun    = max(0.004, WorldUniforms.SunDirConeTan.w);
    float2 R2P       = PixelR2(PixelXY);
    float  RotPhi    = R2P.x * TauConst;
    float3 SmoothPos = ComputeSmoothSurfaceRayOrigin(Hit.Position, Hit.GeomNormal, Hit.ShadingNormal, 0.008);

    // Algorithm 4: WideTreeRayQuery — Path-Tracer 1-Ray R2 Solar-Disc AnyHit NEE + Spatial Penumbra Filter
    if (ShadowAlg == 4u)
    {
        if (EngineMode <= 2u && IsGlobalDistanceFieldReady())
        {
            float GdfVis = TraceGlobalDistanceConeVisibility(SmoothPos, Hit.GeomNormal, SunDir, 12.0, TanSun * 2.0, false, 8u);
            if (GdfVis >= 0.99)
            {
                return TermFade;
            }
        }
        float3 T, B;
        ConstructOrthonormalBasis(SunDir, T, B);
        float  DiscR  = sqrt(saturate(R2P.x)) * TanSun;
        float  DiscPh = R2P.y * TauConst;
        float3 JitSun = normalize(SunDir + T * (DiscR * cos(DiscPh)) + B * (DiscR * sin(DiscPh)));
        bool   Occ    = (dot(Hit.GeomNormal, JitSun) <= 0.0) || TraceSceneAnyHit(SmoothPos, JitSun, 0.008, 90.0);
        OutBlockerDist = Occ ? 0.55 : 0.0;
        return (Occ ? 0.0 : 1.0) * TermFade;
    }

    float2 CenterTXY;
    float  RecD, TexelM;
    uint   Res, BufOffs;
    float3 BiasedPos = SmoothPos;
    if (!ProjectBestSunCascade(BiasedPos, CenterTXY, RecD, TexelM, Res, BufOffs))
    {
        if (EngineMode <= 2u && IsGlobalDistanceFieldReady())
        {
            return TraceGlobalDistanceConeVisibility(SmoothPos, Hit.GeomNormal, SunDir, 14.0, TanSun, false, 8u) * TermFade;
        }
        float3 Orig = SmoothPos + Hit.GeomNormal * 0.008;
        return (TraceSceneAnyHit(Orig, SunDir, 0.020, 90.0) ? 0.0 : 1.0) * TermFade;
    }

    int   MaxIdx     = int(Res) - 1;
    float EffNdotL   = max(0.16, min(NdotL, max(0.0, dot(Hit.GeomNormal, SunDir))));
    float SlopeScale = min(4.2, sqrt(max(0.0, 1.0 - EffNdotL * EffNdotL)) / EffNdotL);
    float Bias       = TexelM * (1.40 + 0.95 * SlopeScale);

    // Algorithm 3: PcfBilinear (8-Tap Smooth Bilinear Vogel PCF with 5-Tap Lit Early-Out)
    if (ShadowAlg == 3u)
    {
        float RadiusPx = 1.85;
        if (QueryCascadeCardinalOpen(CenterTXY, RadiusPx, RecD, Bias, Res, BufOffs))
        {
            return TermFade;
        }
        float Vis = 0.0;
        [loop]
        for (uint Tap = 0u; Tap < 8u; ++Tap)
        {
            float R   = sqrt((float(Tap) + 0.5) * 0.125) * RadiusPx;
            float Ang = float(Tap) * 2.39996323 + RotPhi;
            Vis += SampleShadowVisBilinear(CenterTXY + float2(cos(Ang), sin(Ang)) * R, RecD, Bias + TexelM * 0.12 * R, Res, BufOffs);
        }
        Vis *= 0.125;
        OutBlockerDist = (Vis > 0.01 && Vis < 0.99) ? 0.28 : 0.0;
        return Vis * TermFade;
    }

    // Algorithm 2: ShadowRayMarch (5-Tap Cardinal Early-Out + 4x10 Quadratic SMRT)
    if (ShadowAlg == 2u)
    {
        float MaxMarchM  = 6.50;
        float MaxStepPx  = clamp((MaxMarchM * TanSun) / TexelM, 2.5, 44.0);
        if (QueryCascadeCardinalOpen(CenterTXY, MaxStepPx * 0.85, RecD, Bias, Res, BufOffs))
        {
            return TermFade;
        }
        float ThicknessM = max(0.65, TexelM * 42.0);
        float Lit        = 0.0;
        float BlkDistSum = 0.0;
        uint  BlkHits    = 0u;
        [loop]
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
        return Lit * TermFade;
    }

    // Algorithms 0 (ContactPcss) & 1 (PcssPlaneAware): 8-Tap Vogel Blocker Search + 12-Tap Bilinear Penumbra PCF
    float SearchRadPx  = clamp((TanSun * 4.5) / TexelM, 2.75, 24.0);
    float BlockerSum   = 0.0;
    uint  Blockers     = 0u;
    bool  OpenCorridor = QueryCascadeCardinalOpen(CenterTXY, SearchRadPx, RecD, Bias, Res, BufOffs);
    if (!OpenCorridor)
    {
        [loop]
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
        [loop]
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
            float GdfGate = TraceGlobalDistanceConeVisibility(SmoothPos, Hit.GeomNormal, SunDir, 0.34, TanSun * 1.25, false, 4u);
            Vis = min(Vis, lerp(0.03, 1.0, GdfGate));
        }
        else if (!OpenCorridor && Vis > 0.02 && dot(Hit.GeomNormal, SunDir) > 0.02)
        {
            float3 COrig = SmoothPos + Hit.GeomNormal * 0.004;
            if (TraceSceneAnyHit(COrig, SunDir, 0.006, 0.24))
            {
                Vis = min(Vis, 0.04);
            }
        }
    }
    return Vis * TermFade;
}

//------------------------------------------------------------------------------------------------------------------------
//                           SPECULAR REFLECTIONS & O(1) RADIOSITY-SHADED SECONDARY BOUNCE
//------------------------------------------------------------------------------------------------------------------------

float3 EvaluatePhotometricLampSecondaryFast(
    GpuPhotometricLamp64 Lamp,
    uint                 ProfileCount,
    float3               SafePos,
    float3               N,
    float3               V,
    GpuWorldMaterial80   Mat)
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
    if (dot(N, LDir) <= 1.0e-4)
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
    if (IsGlobalDistanceFieldReady() && Dist > 0.45)
    {
        LampVis = TraceGlobalDistanceConeVisibility(SafePos, N, LDir, max(0.04, Dist - max(Radius, 0.35)), Radius / max(0.1, Dist), false, 6u);
        if (LampVis <= 1.0e-3) return float3(0.0);
    }

    float Ratio  = saturate(Dist / max(0.5, Range));
    float Ratio2 = Ratio * Ratio;
    float Window = max(0.0, 1.0 - Ratio2 * Ratio2);
    float Atten  = (ProfileGain * Window * Window) / max(0.01, Dist2 + Radius * Radius);
    return EvaluateSurfaceBrdf(Mat, N, V, LDir) * Lamp.PeakIntensityPad.xyz * (Atten * LampVis);
}

float3 ShadeSecondaryHitNoTrace(RayHitReading Hit, float3 RayDir)
{
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
    [loop]
    for (uint L = 0u; L < LightCnt; ++L)
    {
        ColorHdr += EvaluateAreaQuadLightGdfFast(WorldUniforms.AreaLights[L], SafeSecondaryPos, Hit.GeomNormal, N, V, Mat);
    }

    uint PackedPhotoCounts = WorldUniforms.DenoiseStepParams.z;
    uint PhotoProfileCount = PackedPhotoCounts >> 16u;
    uint PhotoLampCount    = PackedPhotoCounts & 0xFFFFu;
    if (PhotoProfileCount > 0u && PhotoLampCount > 0u)
    {
        [loop]
        for (uint PL = 0u; PL < PhotoLampCount; ++PL)
        {
            ColorHdr += EvaluatePhotometricLampSecondaryFast(PhotometricLampEntries[PL], PhotoProfileCount, SafeSecondaryPos, N, V, Mat);
        }
    }

    float3 Albedo    = Mat.DiffuseAlbedoRoughness.xyz * (1.0 - saturate(Mat.SpecParams.y)) * (1.0 - saturate(Mat.CoatRoughnessOptics.y));
    float3 SkyBounce = EvaluateSkyDome(N) * 0.22;
    if (IsGlobalDistanceFieldReady())
    {
        float  Vox0      = QueryFineGdfVoxelSize();
        float3 SamplePos = Hit.Position + Hit.GeomNormal * (Vox0 * 1.15);
        float  RadFade   = ComputeRadiosityVolumeFade(SamplePos);
        if (RadFade > 1.0e-3)
        {
            bool   InRad    = false;
            float4 RadFetch = SampleRadiosityPyramidMip(SamplePos, 1.0, InRad);
            if (InRad && RadFetch.a > 0.01)
            {
                float3 VoxelBounce = (RadFetch.rgb / max(0.22, RadFetch.a)) * 0.55;
                float  BlendW      = RadFade * smoothstep(0.01, 0.18, RadFetch.a);
                SkyBounce          = lerp(SkyBounce, VoxelBounce, BlendW);
            }
        }
    }
    ColorHdr += Albedo * SkyBounce;
    return ColorHdr;
}

float3 ShadeSecondaryBounce(float3 RayOrigin, float3 RayDir)
{
    uint ReflSource = WorldUniforms.EngineShadowReflPad.z;
    if (ReflSource == 1u || !IsRaytracingEnabled())
    {
        return EvaluateSkyDome(RayDir);
    }
    RayHitReading Hit = TraceSceneClosest(RayOrigin, RayDir, 2.0e-3, 80.0);
    return ShadeSecondaryHitNoTrace(Hit, RayDir);
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
    float3 SafePrimaryPos = ComputeSmoothSurfaceRayOrigin(Hit.Position, Hit.GeomNormal, N, 0.012);
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
    [loop]
    for (uint L = 0u; L < LightCnt; ++L)
    {
        OutDirectBaseHdr += EvaluateAreaQuadLight(
            WorldUniforms.AreaLights[L], SafePrimaryPos, Hit.GeomNormal, N, ViewDir, Mat, PixelSalt + L * 7919u);
    }

    // 2b. ANSI/IESNA LM-63 Type C Photometric Point Luminaires
    uint PackedPhotoCounts = WorldUniforms.DenoiseStepParams.z;
    uint PhotoProfileCount = PackedPhotoCounts >> 16u;
    uint PhotoLampCount    = PackedPhotoCounts & 0xFFFFu;
    if (PhotoProfileCount > 0u && PhotoLampCount > 0u)
    {
        [loop]
        for (uint PL = 0u; PL < PhotoLampCount; ++PL)
        {
            OutDirectBaseHdr += EvaluatePhotometricLampGpu(PhotometricLampEntries[PL], PhotoProfileCount, SafePrimaryPos, N, ViewDir, Mat, false);
        }
    }

    // 3. Ambient Occlusion: 0-ray Global Distance Field Cone AO on GTX (Engines 1..2); G-buffer GTAO handles non-GDF
    float3 BasisT, BasisB;
    ConstructOrthonormalBasis(N, BasisT, BasisB);
    OutDiffAo = IsGlobalDistanceFieldReady()
        ? EvaluateGlobalDistanceAmbientOcclusion(SafePrimaryPos, N, Hit.GeomNormal)
        : 1.0;

    float NdotV  = max(1.0e-3, dot(N, ViewDir));
    OutSpecRough = clamp(Mat.SpecParams.x, 0.025, 1.0);
    OutSpecAo    = saturate(pow(NdotV + OutDiffAo, exp2(-16.0 * OutSpecRough - 1.0)) - 1.0 + OutDiffAo);

    // 4. Matte Diffuse Albedo
    float Metalness       = saturate(Mat.SpecParams.y);
    float TransW          = saturate(Mat.CoatRoughnessOptics.y);
    OutDiffuseAlbedoMatte = Mat.DiffuseAlbedoRoughness.xyz * (1.0 - Metalness) * (1.0 - TransW);

    // 5. Secondary Specular Reflection (Mirror BVH4 ray only for sharp metals/clearcoats; GDF Glossy Cone for rough GTX surfaces)
    float CoatW = saturate(Mat.EdgeTintCoatWeight.w);
    if (IsReflectionsEnabled() && (Metalness > 0.20 || CoatW > 0.20 || OutSpecRough < 0.24 || EngineMode == 2u))
    {
        float3 ReflDir       = reflect(-ViewDir, N);
        float3 EnvRad        = float3(0.0);
        bool   SharpSpecular = (OutSpecRough < 0.18) || (CoatW > 0.20 && Mat.CoatRoughnessOptics.x < 0.16);

        if (!SharpSpecular && IsGlobalDistanceFieldReady())
        {
            EnvRad = EvaluateGdfGlossyCone(Hit.Position, Hit.GeomNormal, ReflDir, OutSpecRough);
        }
        else if (SharpSpecular)
        {
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
        float  Ior      = max(1.02, Mat.SpecParams.z);
        float  F0Diel   = pow((Ior - 1.0) / (Ior + 1.0), 2.0);
        float3 F0       = lerp(float3(F0Diel), Mat.DiffuseAlbedoRoughness.xyz, Metalness);
        float  Schlick  = pow(1.0 - NdotV, 5.0);
        float3 RoughMax = max(float3(1.0 - OutSpecRough), F0);
        float3 Fresnel  = F0 + (RoughMax - F0) * Schlick;
        float  Gloss    = saturate(1.0 - OutSpecRough * 0.85);
        if (CoatW > 0.15 && Metalness < 0.2)
        {
            Fresnel = max(Fresnel, float3((0.04 + 0.96 * Schlick) * CoatW));
            Gloss   = max(Gloss, 1.0 - Mat.CoatRoughnessOptics.x);
        }
        OutSpecReflHdr = EnvRad * Fresnel * Gloss;
    }

    // 6. Demodulated Indirect Diffuse GI (Engine 2 GTX GDF GI: 5-cone 2-bounce 3D Radiosity Cone Tracing, 0 BVH4 rays)
    bool EnableIndirectGi = (PenumbraMode != 2u) && (dot(OutDiffuseAlbedoMatte, float3(1.0)) > 1.0e-4);
    if (EnableIndirectGi && EngineMode == 2u && IsGlobalDistanceFieldReady())
    {
        OutIndirectIrradiance = EvaluateGdfVoxelConeGi(Hit.Position, N, Hit.GeomNormal, BasisT, BasisB);
        OutFilterIndirect     = false;
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

    float PrimaryMaxT = max(120.0, WorldUniforms.SolarIrradiancePad.w);

    // Primary intersection handled outside bounce loop to avoid duplicating sky/cloud/glass raymarchers
    RayHitReading CurHit = TraceSceneClosest(CamOrigin, PrimaryDir, 0.08, PrimaryMaxT);
    float         TriT0  = CurHit.Hit ? CurHit.Distance : PrimaryMaxT;
    float         GlsT0  = TriT0;
    int           GlsId0 = FindClosestGlassSphere(CamOrigin, PrimaryDir, 0.08, TriT0, GlsT0);
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
            CurHit = TraceSceneClosest(RayO, RayD, 0.003, 120.0);
            if (!CurHit.Hit)
            {
                Radiance += Throughput * EvaluateSkyDome(RayD);
                break;
            }
        }

        GpuWorldMaterial80 Mat = MaterialEntries[min(CurHit.MaterialSlot, MatCount - 1u)];
        float3 N        = CurHit.ShadingNormal;
        float3 V        = -RayD;
        float3 SafePos  = ComputeSmoothSurfaceRayOrigin(CurHit.Position, CurHit.GeomNormal, N, 0.012);
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
            float  TermF  = EvaluateSmoothTerminatorFade(CurHit.GeomNormal, N, JitSun);
            if (TermF > 1.0e-4 && dot(N, JitSun) > 0.0 && !TraceSceneAnyHit(SafePos, JitSun, 0.025, 110.0))
            {
                float CloudTr = (Bounce == 0u) ? EvaluateSolarCloudTransmittanceGpu(CurHit.Position, SunDir) : 1.0;
                Radiance += Throughput * EvaluateSurfaceBrdf(Mat, N, V, JitSun) * SunIrr * CloudTr * TermF;
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

bool QueryNaniteWireframeEnabled()
{
    return (WorldUniforms.AtmosphereMaskPad.w & (1u << 22u)) != 0u;
}

// GeometryCompiler ViewportRasterization.js 5-stop blue -> cyan -> green -> amber -> red ramp (converted to linear RGB)
float3 EvaluateGeometryCompilerRamp(float Ratio)
{
    float T = saturate(Ratio) * 4.0;
    int   Idx = min(3, int(floor(T)));
    float Fr  = T - float(Idx);
    float3 Stops[5] = {
        float3(0.027, 0.147, 0.694), // sRGB (0.18, 0.42, 0.85)
        float3(0.020, 0.477, 0.448), // sRGB (0.15, 0.72, 0.70)
        float3(0.147, 0.571, 0.084), // sRGB (0.42, 0.78, 0.32)
        float3(0.831, 0.420, 0.027), // sRGB (0.92, 0.68, 0.18)
        float3(0.749, 0.047, 0.040), // sRGB (0.88, 0.24, 0.22)
    };
    return lerp(Stops[Idx], Stops[Idx + 1], Fr);
}

// GeometryCompiler ViewportRasterization.js golden-ratio HSL idColor(id) (s=0.58, l=0.56) in linear RGB
float3 EvaluateGeometryCompilerIdColor(uint Ordinal)
{
    float Hue = frac(float(Ordinal) * 0.61803398875);
    float Sat = 0.58;
    float Lit = 0.56;
    float Amp = Sat * min(Lit, 1.0 - Lit);
    float3 Srgb;
    float K0 = fmod(Hue * 12.0, 12.0);
    float K8 = fmod(8.0 + Hue * 12.0, 12.0);
    float K4 = fmod(4.0 + Hue * 12.0, 12.0);
    Srgb.x = Lit - Amp * max(-1.0, min(K0 - 3.0, min(9.0 - K0, 1.0)));
    Srgb.y = Lit - Amp * max(-1.0, min(K8 - 3.0, min(9.0 - K8, 1.0)));
    Srgb.z = Lit - Amp * max(-1.0, min(K4 - 3.0, min(9.0 - K4, 1.0)));
    return pow(saturate(Srgb), 2.2);
}

float EvaluateAnalyticalWireframeAlpha(RayHitReading Hit, float3 RayDir, float EdgeMeters)
{
    float TanHalfY   = max(1.0e-4, WorldUniforms.CameraRightTanY.w);
    float ViewHeight = max(1.0, WorldUniforms.CameraForwardHeight.w);
    float GrazingCos = max(0.28, abs(dot(Hit.GeomNormal, RayDir)));
    float PixelSpan  = Hit.Distance * (2.0 * TanHalfY / ViewHeight) / GrazingCos;
    float EdgeInPx   = EdgeMeters / max(1.0e-6, PixelSpan);
    return 1.0 - smoothstep(0.30, 1.35, EdgeInPx);
}

float3 EvaluateNaniteDebugChannelHdr(uint DebugMode, RayHitReading Hit, float3 RayDir)
{
    float3 KeyLightA = normalize(WorldUniforms.SunDirConeTan.xyz);
    float3 KeyLightB = normalize(float3(-KeyLightA.y, KeyLightA.x, 0.55));
    float3 ShadeNorm = (DebugMode == 20u) ? Hit.GeomNormal : Hit.ShadingNormal;
    float  StudioLit = 0.24 + 0.58 * max(0.0, dot(ShadeNorm, KeyLightA)) + 0.20 * max(0.0, dot(ShadeNorm, KeyLightB));

    float3 DebugTint = float3(0.5);
    if (DebugMode == 17u)
    {
        float Ratio = float(Hit.LodLevel) / float(max(1u, Hit.MaxLodLevel));
        DebugTint   = EvaluateGeometryCompilerRamp(Ratio);
    }
    else if (DebugMode == 18u)
    {
        DebugTint = EvaluateGeometryCompilerIdColor(Hit.ClusterOrdinal + 1u);
    }
    else if (DebugMode == 19u)
    {
        DebugTint = EvaluateGeometryCompilerIdColor(Hit.GroupOrdinal * 7u + 3u);
    }
    else if (DebugMode == 20u)
    {
        DebugTint = EvaluateGeometryCompilerIdColor(Hit.TriangleOrdinal + 1u);
    }
    else if (DebugMode == 21u)
    {
        float PixelsPerRadian = max(1.0, WorldUniforms.CameraForwardHeight.w / max(1.0e-5, 2.0 * WorldUniforms.CameraRightTanY.w));
        float BudgetPx        = max(0.10, WorldUniforms.SunCascadeForwardPad.w);
        float ProjErrPx       = (Hit.SelfErrorMeters * PixelsPerRadian) / max(1.0e-3, Hit.Distance);
        DebugTint             = EvaluateGeometryCompilerRamp(saturate(ProjErrPx / BudgetPx));
    }
    else if (DebugMode == 22u)
    {
        int   CellX   = int(floor(Hit.SurfaceUv.x * 16.0));
        int   CellY   = int(floor(Hit.SurfaceUv.y * 16.0));
        float Checker = (((CellX + CellY) & 1) != 0) ? 0.88 : 0.34;
        float2 TileUv = frac(Hit.SurfaceUv);
        DebugTint     = float3(0.18 + 0.72 * TileUv.x, 0.22 + 0.68 * TileUv.y, 0.58) * Checker;
    }

    float3 Result = DebugTint * StudioLit;
    if (DebugMode == 22u && Hit.SeamDistMeters < 1.0e8)
    {
        float SeamAlpha = EvaluateAnalyticalWireframeAlpha(Hit, RayDir, Hit.SeamDistMeters * 0.65);
        Result          = lerp(Result, float3(1.00, 0.62, 0.04), SeamAlpha * 0.92);
    }
    if (DebugMode == 20u || QueryNaniteWireframeEnabled())
    {
        float WireAlpha = EvaluateAnalyticalWireframeAlpha(Hit, RayDir, Hit.EdgeDistMeters);
        Result          = lerp(Result, Result * 0.14 + float3(0.012), WireAlpha * 0.78);
    }
    return Result;
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

    float PrimaryMaxT = max(120.0, WorldUniforms.SolarIrradiancePad.w);
    RayHitReading PrimaryHit = TraceSceneClosest(CamOrigin, RayDir, 0.08, PrimaryMaxT);

    float TriHitT      = PrimaryHit.Hit ? PrimaryHit.Distance : PrimaryMaxT;
    float BestGlassT   = TriHitT;
    int   BestGlassIdx = -1;
    if (EngineMode != 0u)
    {
        BestGlassIdx = FindClosestGlassSphere(CamOrigin, RayDir, 0.08, TriHitT, BestGlassT);
    }

    float HitDistForClouds = (BestGlassIdx >= 0) ? BestGlassT : (PrimaryHit.Hit ? PrimaryHit.Distance : 60000.0);
    float HitDistForFog    = (BestGlassIdx >= 0) ? BestGlassT : (PrimaryHit.Hit ? PrimaryHit.Distance : 3500.0);

    bool UsesShadingDebug = (DebugMode >= 8u && DebugMode <= 11u) || DebugMode == 15u;
    if (DebugMode != 0u && !UsesShadingDebug)
    {
        float3 DbgHdr = float3(0.0);
        if (BestGlassIdx >= 0)
        {
            GpuGlassSphere Glass = WorldUniforms.GlassSpheres[BestGlassIdx];
            float3 HitPos = CamOrigin + RayDir * BestGlassT;
            float3 HitNrm = normalize(HitPos - Glass.CenterRadiusPacked.xyz);
            if (DebugMode == 1u)      DbgHdr = float3(pow(saturate(1.0 - BestGlassT / 11.0), 1.8));
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
            if (DebugMode == 1u)      DbgHdr = float3(pow(saturate(1.0 - PrimaryHit.Distance / 11.0), 1.8));
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
                DbgHdr = float3(0.18 + 0.78 * H1, 0.18 + 0.78 * H2, 0.18 + 0.78 * H3);
            }
            else if (DebugMode == 7u) DbgHdr = PrimaryHit.GeomNormal * 0.5 + 0.5;
            else if (DebugMode == 12u)
            {
                // SDF Distance: heat ramp of the signed distance at the hit (red inside, green surface, blue far)
                float LocalVox = 0.1;
                float SdfDist  = SampleGlobalDistanceWorld(PrimaryHit.Position, LocalVox);
                DbgHdr = EvaluateSdfDistanceHeatColor(SdfDist, LocalVox);
            }
            else if (DebugMode == 13u)
            {
                // SDF with Materials: material albedo lit by the SDF gradient normal against the sun
                float3 SdfNormal = EvaluateGlobalDistanceGradientNormal(PrimaryHit.Position, PrimaryHit.GeomNormal);
                float  SdfNdotL  = saturate(dot(SdfNormal, WorldUniforms.SunDirConeTan.xyz) * 0.5 + 0.5);
                DbgHdr = Mat.DiffuseAlbedoRoughness.xyz * (0.18 + 0.82 * SdfNdotL);
            }
            else if (DebugMode == 14u)
            {
                // SDF Normal: central-difference gradient of the distance field at the hit
                DbgHdr = EvaluateGlobalDistanceGradientNormal(PrimaryHit.Position, PrimaryHit.GeomNormal) * 0.5 + 0.5;
            }
            else if (DebugMode == 16u)
            {
                // SDF Cascade: fine = green, coarse = orange, outside both = red, tinted by albedo
                uint CascadeIdx = QueryGlobalDistanceCascadeIndex(PrimaryHit.Position);
                float3 CascadeTint = (CascadeIdx == 0u) ? float3(0.20, 0.90, 0.30)
                    : ((CascadeIdx == 1u) ? float3(0.95, 0.60, 0.15) : float3(0.90, 0.20, 0.20));
                DbgHdr = CascadeTint * (0.30 + 0.70 * Mat.DiffuseAlbedoRoughness.xyz);
            }
            else if (DebugMode >= 17u && DebugMode <= 22u)
            {
                DbgHdr = EvaluateNaniteDebugChannelHdr(DebugMode, PrimaryHit, RayDir);
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
        float3 ReflRgb = float3(0.0);
        int    MaxCh   = IsReflectionsEnabled() ? 4 : 3;
        [loop]
        for (int Ch = 0; Ch < MaxCh; ++Ch)
        {
            if (Ch < 3)
            {
                float  ChIor = (Ch == 0) ? (CrownIor - Dispersion) : ((Ch == 1) ? CrownIor : (CrownIor + Dispersion * 1.15));
                float3 ChRad = TraceThroughGlassSphere(HitPos, HitNrm, RayDir, Center, Radius, ChIor);
                if (Ch == 0) RefrRgb.r = ChRad.r;
                else if (Ch == 1) RefrRgb.g = ChRad.g;
                else RefrRgb.b = ChRad.b;
            }
            else
            {
                ReflRgb = ShadeSecondaryBounce(HitPos + HitNrm * 2.5e-3, reflect(RayDir, HitNrm));
            }
        }
        RefrRgb *= float3(0.95, 0.98, 1.00);

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
        if (QueryNaniteWireframeEnabled())
        {
            MatCapHdr *= (1.0 - 0.72 * EvaluateAnalyticalWireframeAlpha(PrimaryHit, RayDir, PrimaryHit.EdgeDistMeters));
        }
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

    if (QueryNaniteWireframeEnabled())
    {
        float WireKeep     = 1.0 - 0.72 * EvaluateAnalyticalWireframeAlpha(PrimaryHit, RayDir, PrimaryHit.EdgeDistMeters);
        DirectBaseHdr      *= WireKeep;
        UnoccludedSunHdr   *= WireKeep;
        DiffuseAlbedoMatte *= WireKeep;
        SpecReflHdr        *= WireKeep;
    }

    AccumulationHdrBuffer[PixelIdx]  = float4(SpecReflHdr, SpecRough);
    GBufPosMask[PixelIdx]            = float4(PrimaryHit.Position, 1.0 + SpecRough);
    GBufNormAo[PixelIdx]             = float4(PrimaryHit.ShadingNormal, DiffAo);
    GBufDirectBase[PixelIdx]         = float4(DirectBaseHdr, HitDistForClouds);
    GBufSunContrib[PixelIdx]         = float4(UnoccludedSunHdr, HitDistForFog);
    GBufAlbedo[PixelIdx]             = float4(DiffuseAlbedoMatte, SpecAo);
    PingShadowAo[PixelIdx]           = float4(SunVis, DiffAo, BlockerDist, 1.0);
    PingIndirectIrradiance[PixelIdx] = float4(IndirectIrradiance, FilterIndirect ? 1.0 : 0.0);
}

// Dedicated Engine 3 (HighEnd BVH4 GI) compute entry using the Path-Tracer single-loop BVH4 ray march
// architecture (1 TraceSceneClosest call site across primary + sharp specular + 1-spp R2 cosine GI/AO,
// plus stochastic NEE AnyHit shadows) so neither ComputeEntry nor HighEndBvhComputeEntry spills VGPRs.
[shader("compute")]
[numthreads(8, 8, 1)]
void HighEndBvhComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Width  = uint(WorldUniforms.CameraUpWidth.w + 0.5);
    uint Height = uint(WorldUniforms.CameraForwardHeight.w + 0.5);
    if (DispatchThreadID.x >= Width || DispatchThreadID.y >= Height)
    {
        return;
    }

    uint   PixelIdx     = DispatchThreadID.y * Width + DispatchThreadID.x;
    uint   DebugMode    = WorldUniforms.LightGlassMaterialDebug.w;
    uint   PenumbraMode = (WorldUniforms.AtmosphereMaskPad.w >> 16u) & 0x7u;
    float3 CamOrigin    = WorldUniforms.CameraOriginTanX.xyz;

    float2 PixelUv = (float2(DispatchThreadID.xy) + float2(0.5, 0.5)) / float2(float(Width), float(Height));
    float  NdcX    = PixelUv.x * 2.0 - 1.0;
    float  NdcY    = 1.0 - PixelUv.y * 2.0;
    float3 RayDir  = normalize(
        WorldUniforms.CameraForwardHeight.xyz +
        WorldUniforms.CameraRightTanY.xyz * (NdcX * WorldUniforms.CameraOriginTanX.w) +
        WorldUniforms.CameraUpWidth.xyz   * (NdcY * WorldUniforms.CameraRightTanY.w));

    float PrimaryMaxT = max(120.0, WorldUniforms.SolarIrradiancePad.w);

    RayHitReading PrimaryHit;
    PrimaryHit.Hit = false;

    float3 DirectSurfaceHdr   = float3(0.0);
    float3 SpecReflHdr        = float3(0.0);
    float3 UnoccludedSunHdr   = float3(0.0);
    float3 DiffuseAlbedoMatte = float3(0.0);
    float3 IndirectIrradiance = float3(0.0);
    bool   FilterIndirect     = false;
    float  SunVis             = 1.0;
    float  BlockerDist        = 0.0;
    float  DiffAo             = 1.0;
    float  SpecRough          = 0.5;
    float  SpecAo             = 1.0;
    float  HitDistForClouds   = 60000.0;
    float  HitDistForFog      = 3500.0;

    bool   NeedSpecRay = false;
    bool   NeedGiRay   = false;
    float3 SpecRayOrig = float3(0.0);
    float3 SpecRayDir  = float3(0.0, 0.0, 1.0);
    float3 SpecScale   = float3(0.0);
    float  SpecMaxClamp= 32.0;
    float3 GiRayOrig   = float3(0.0);
    float3 GiRayDir    = float3(0.0, 0.0, 1.0);

    float3 CurRayO = CamOrigin;
    float3 CurRayD = RayDir;
    float  CurMinT = 0.08;
    float  CurMaxT = PrimaryMaxT;

    // Single [loop] with ONE TraceSceneClosest call site (primary=0, sharp reflection=1, cosine GI+AO=2)
    [loop]
    for (uint Step = 0u; Step < 3u; ++Step)
    {
        if (Step == 1u && !NeedSpecRay)
        {
            continue;
        }
        if (Step == 2u && !NeedGiRay)
        {
            break;
        }
        if (Step == 1u)
        {
            CurRayO = SpecRayOrig;
            CurRayD = SpecRayDir;
            CurMinT = 2.0e-3;
            CurMaxT = 80.0;
        }
        else if (Step == 2u)
        {
            CurRayO = GiRayOrig;
            CurRayD = GiRayDir;
            CurMinT = 8.0e-3;
            CurMaxT = 80.0;
        }

        RayHitReading CurHit = TraceSceneClosest(CurRayO, CurRayD, CurMinT, CurMaxT);

        if (Step == 0u)
        {
            PrimaryHit         = CurHit;
            float TriHitT      = PrimaryHit.Hit ? PrimaryHit.Distance : PrimaryMaxT;
            float BestGlassT   = TriHitT;
            int   BestGlassIdx = FindClosestGlassSphere(CamOrigin, RayDir, 0.08, TriHitT, BestGlassT);
            HitDistForClouds   = (BestGlassIdx >= 0) ? BestGlassT : (PrimaryHit.Hit ? PrimaryHit.Distance : 60000.0);
            HitDistForFog      = (BestGlassIdx >= 0) ? BestGlassT : (PrimaryHit.Hit ? PrimaryHit.Distance : 3500.0);

            bool UsesShadingDebug = (DebugMode >= 8u && DebugMode <= 11u) || DebugMode == 15u;
            if (DebugMode != 0u && !UsesShadingDebug)
            {
                float3 DbgHdr = float3(0.0);
                if (BestGlassIdx >= 0)
                {
                    GpuGlassSphere Glass = WorldUniforms.GlassSpheres[BestGlassIdx];
                    float3 HitPos = CamOrigin + RayDir * BestGlassT;
                    float3 HitNrm = normalize(HitPos - Glass.CenterRadiusPacked.xyz);
                    if (DebugMode == 1u)      DbgHdr = float3(pow(saturate(1.0 - BestGlassT / 11.0), 1.8));
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
                    if (DebugMode == 1u)      DbgHdr = float3(pow(saturate(1.0 - PrimaryHit.Distance / 11.0), 1.8));
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
                        DbgHdr = float3(0.18 + 0.78 * H1, 0.18 + 0.78 * H2, 0.18 + 0.78 * H3);
                    }
                    else if (DebugMode == 7u) DbgHdr = PrimaryHit.GeomNormal * 0.5 + 0.5;
                    else if (DebugMode == 12u)
                    {
                        float LocalVox = 0.1;
                        float SdfDist  = SampleGlobalDistanceWorld(PrimaryHit.Position, LocalVox);
                        DbgHdr = EvaluateSdfDistanceHeatColor(SdfDist, LocalVox);
                    }
                    else if (DebugMode == 13u)
                    {
                        float3 SdfNormal = EvaluateGlobalDistanceGradientNormal(PrimaryHit.Position, PrimaryHit.GeomNormal);
                        float  SdfNdotL  = saturate(dot(SdfNormal, WorldUniforms.SunDirConeTan.xyz) * 0.5 + 0.5);
                        DbgHdr = Mat.DiffuseAlbedoRoughness.xyz * (0.18 + 0.82 * SdfNdotL);
                    }
                    else if (DebugMode == 14u)
                    {
                        DbgHdr = EvaluateGlobalDistanceGradientNormal(PrimaryHit.Position, PrimaryHit.GeomNormal) * 0.5 + 0.5;
                    }
                    else if (DebugMode == 16u)
                    {
                        uint CascadeIdx = QueryGlobalDistanceCascadeIndex(PrimaryHit.Position);
                        float3 CascadeTint = (CascadeIdx == 0u) ? float3(0.20, 0.90, 0.30)
                            : ((CascadeIdx == 1u) ? float3(0.95, 0.60, 0.15) : float3(0.90, 0.20, 0.20));
                        DbgHdr = CascadeTint * (0.30 + 0.70 * Mat.DiffuseAlbedoRoughness.xyz);
                    }
                    else if (DebugMode >= 17u && DebugMode <= 22u)
                    {
                        DbgHdr = EvaluateNaniteDebugChannelHdr(DebugMode, PrimaryHit, RayDir);
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
                float3 Center     = Glass.CenterRadiusPacked.xyz;
                float  Radius     = Glass.CenterRadiusPacked.w;
                float3 HitPos     = CamOrigin + RayDir * BestGlassT;
                float3 HitNrm     = normalize(HitPos - Center);
                float3 ViewDirG   = -RayDir;
                float  CrownIor   = max(1.05, Glass.OpticsSlotPacked.x);
                float  Dispersion = clamp(0.45 / max(10.0, Glass.OpticsSlotPacked.y), 0.004, 0.028);
                float  CosI       = saturate(dot(HitNrm, ViewDirG));
                float  F0G        = pow((CrownIor - 1.0) / (CrownIor + 1.0), 2.0);
                float  FresnelG   = IsReflectionsEnabled() ? (F0G + (1.0 - F0G) * pow(1.0 - CosI, 5.0)) : 0.0;
                float3 RefrRgb    = float3(0.0);
                float3 ReflEnv    = float3(0.0);
                int    MaxChG     = IsReflectionsEnabled() ? 4 : 3;
                [loop]
                for (int Ch = 0; Ch < MaxChG; ++Ch)
                {
                    if (Ch < 3)
                    {
                        float  ChIor = (Ch == 0) ? (CrownIor - Dispersion) : ((Ch == 1) ? CrownIor : (CrownIor + Dispersion * 1.15));
                        float3 ChRad = TraceThroughGlassSphere(HitPos, HitNrm, RayDir, Center, Radius, ChIor);
                        if (Ch == 0) RefrRgb.r = ChRad.r;
                        else if (Ch == 1) RefrRgb.g = ChRad.g;
                        else RefrRgb.b = ChRad.b;
                    }
                    else
                    {
                        ReflEnv = ShadeSecondaryBounce(HitPos + HitNrm * 2.5e-3, reflect(RayDir, HitNrm));
                    }
                }
                RefrRgb          *= float3(0.95, 0.98, 1.00);
                float3 SunDirG    = WorldUniforms.SunDirConeTan.xyz;
                float  SpecGl     = IsReflectionsEnabled() ? (pow(max(0.0, dot(HitNrm, normalize(ViewDirG + SunDirG))), 380.0) * 6.5) : 0.0;
                if (SpecGl > 0.0) SpecGl *= SampleCascadedSunShadowFast(HitPos, HitNrm, HitNrm, SunDirG);
                float3 GlassHdr   = (DebugMode == 11u)
                    ? ApplyAcesFilmicGpu(ReflEnv * FresnelG + WorldUniforms.SolarIrradiancePad.xyz * SpecGl)
                    : (lerp(RefrRgb, ReflEnv, FresnelG) + WorldUniforms.SolarIrradiancePad.xyz * SpecGl);
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
                GBufPosMask[PixelIdx]            = float4(RayDir, -1.0);
                GBufNormAo[PixelIdx]             = float4(0.0, 0.0, 1.0, 1.0);
                GBufDirectBase[PixelIdx]         = float4(0.0, 0.0, 0.0, HitDistForClouds);
                GBufSunContrib[PixelIdx]         = float4(0.0, 0.0, 0.0, HitDistForFog);
                GBufAlbedo[PixelIdx]             = float4(0.0, 0.0, 0.0, 1.0);
                PingShadowAo[PixelIdx]           = float4(1.0, 1.0, 0.0, 1.0);
                PingIndirectIrradiance[PixelIdx] = float4(0.0, 0.0, 0.0, 0.0);
                return;
            }

            uint MatCount = max(1u, WorldUniforms.LightGlassMaterialDebug.z);
            GpuWorldMaterial80 Mat = MaterialEntries[min(PrimaryHit.MaterialSlot, MatCount - 1u)];
            float3 ViewDir        = -RayDir;
            float3 N              = PrimaryHit.ShadingNormal;
            float3 SunDir         = WorldUniforms.SunDirConeTan.xyz;
            float3 SunIrr         = WorldUniforms.SolarIrradiancePad.xyz;
            float3 SafePrimaryPos = ComputeSmoothSurfaceRayOrigin(PrimaryHit.Position, PrimaryHit.GeomNormal, N, 0.012);

            DirectSurfaceHdr = Mat.EmissiveRadiancePad.xyz;
            if (dot(N, SunDir) > 1.0e-5)
            {
                float EnvSunTrans = EvaluateSolarCloudTransmittanceGpu(PrimaryHit.Position, SunDir);
                UnoccludedSunHdr  = EvaluateSurfaceBrdf(Mat, N, ViewDir, SunDir) * SunIrr * EnvSunTrans;
                SunVis            = EvaluateSolarShadow(PrimaryHit, SunDir, DispatchThreadID.xy, BlockerDist);
            }
            else
            {
                SunVis = 0.0;
            }

            float2 R2P = PixelR2(DispatchThreadID.xy);
            uint LightCnt = min(4u, WorldUniforms.LightGlassMaterialDebug.x);
            [loop]
            for (uint L = 0u; L < LightCnt; ++L)
            {
                float2 XiL = frac(R2P + float2(float(L) * 0.3819660113, float(L) * 0.6180339887));
                DirectSurfaceHdr += SampleAreaQuadLightStochasticPt(
                    WorldUniforms.AreaLights[L], SafePrimaryPos, PrimaryHit.GeomNormal, N, ViewDir, Mat, XiL);
            }

            uint PackedPhotoCounts = WorldUniforms.DenoiseStepParams.z;
            uint PhotoProfileCount = PackedPhotoCounts >> 16u;
            uint PhotoLampCount    = PackedPhotoCounts & 0xFFFFu;
            if (PhotoProfileCount > 0u && PhotoLampCount > 0u)
            {
                [loop]
                for (uint PL = 0u; PL < PhotoLampCount; ++PL)
                {
                    DirectSurfaceHdr += EvaluatePhotometricLampGpu(
                        PhotometricLampEntries[PL], PhotoProfileCount, SafePrimaryPos, N, ViewDir, Mat, false);
                }
            }

            float NdotV      = max(1.0e-3, dot(N, ViewDir));
            SpecRough        = clamp(Mat.SpecParams.x, 0.025, 1.0);
            float Metalness  = saturate(Mat.SpecParams.y);
            float TransW     = saturate(Mat.CoatRoughnessOptics.y);
            float CoatW      = saturate(Mat.EdgeTintCoatWeight.w);
            DiffuseAlbedoMatte = Mat.DiffuseAlbedoRoughness.xyz * (1.0 - Metalness) * (1.0 - TransW);

            float3 BasisT, BasisB;
            ConstructOrthonormalBasis(N, BasisT, BasisB);

            if (IsReflectionsEnabled())
            {
                float  Ior      = max(1.02, Mat.SpecParams.z);
                float  F0Diel   = pow((Ior - 1.0) / (Ior + 1.0), 2.0);
                float3 F0       = lerp(float3(F0Diel), Mat.DiffuseAlbedoRoughness.xyz, Metalness);
                float  Schlick  = pow(1.0 - NdotV, 5.0);
                float3 RoughMax = max(float3(1.0 - SpecRough), F0);
                float3 Fresnel  = F0 + (RoughMax - F0) * Schlick;
                float  Gloss    = saturate(1.0 - SpecRough * 0.85);
                if (CoatW > 0.15 && Metalness < 0.2)
                {
                    Fresnel = max(Fresnel, float3((0.04 + 0.96 * Schlick) * CoatW));
                    Gloss   = max(Gloss, 1.0 - Mat.CoatRoughnessOptics.x);
                }
                SpecScale    = Fresnel * Gloss;
                SpecMaxClamp = (SpecRough > 0.12) ? lerp(32.0, 3.5, saturate((SpecRough - 0.12) / 0.68)) : 32.0;

                float3 ReflDir = reflect(-ViewDir, N);
                float  Alpha   = max(1.0e-3, SpecRough * SpecRough);
                float3 DomDir  = normalize(lerp(N, ReflDir, (1.0 - Alpha) * (sqrt(max(0.0, 1.0 - Alpha)) + Alpha)));
                bool   ReflectiveLobe = (Metalness > 0.18) || (CoatW > 0.18) || (SpecRough < 0.28);
                if (ReflectiveLobe && WorldUniforms.EngineShadowReflPad.z != 1u && IsRaytracingEnabled())
                {
                    NeedSpecRay = true;
                    SpecRayOrig = PrimaryHit.Position + PrimaryHit.GeomNormal * 3.0e-3;
                    SpecRayDir  = (SpecRough > 0.06 && dot(DomDir, PrimaryHit.GeomNormal) > 0.02) ? DomDir : ReflDir;
                }
                else
                {
                    SpecReflHdr = min(EvaluateSkyDome(DomDir), float3(SpecMaxClamp)) * SpecScale;
                }
            }

            NeedGiRay = (PenumbraMode != 2u) && (dot(DiffuseAlbedoMatte, float3(1.0)) > 1.0e-4) && IsRaytracingEnabled();
            if (NeedGiRay)
            {
                float SinT = sqrt(saturate(R2P.x));
                float CosT = sqrt(max(0.0, 1.0 - R2P.x));
                float Phi  = TauConst * R2P.y;
                GiRayOrig  = PrimaryHit.Position + PrimaryHit.GeomNormal * 0.014 + N * 0.006;
                GiRayDir   = normalize(BasisT * (SinT * cos(Phi)) + BasisB * (SinT * sin(Phi)) + N * CosT);
                if (dot(GiRayDir, PrimaryHit.GeomNormal) <= 0.01)
                {
                    GiRayDir = N;
                }
            }
        }
        else if (Step == 1u)
        {
            float3 EnvRad = ShadeSecondaryHitNoTrace(CurHit, CurRayD);
            SpecReflHdr   = min(EnvRad, float3(SpecMaxClamp)) * SpecScale;
        }
        else
        {
            if (CurHit.Hit)
            {
                DiffAo             = (CurHit.Distance < 0.85) ? saturate(0.28 + 0.72 * (CurHit.Distance / 0.85)) : 1.0;
                IndirectIrradiance = min(ShadeSecondaryHitNoTrace(CurHit, CurRayD), float3(6.5));
            }
            else
            {
                DiffAo             = 1.0;
                float3 SmoothSkyDir = normalize(lerp(PrimaryHit.ShadingNormal, CurRayD, 0.35));
                IndirectIrradiance = min(EvaluateSkyDome(SmoothSkyDir), float3(6.5));
            }
            FilterIndirect = true;
        }
    }

    float NdotVFinal = max(1.0e-3, dot(PrimaryHit.ShadingNormal, -RayDir));
    SpecAo = saturate(pow(NdotVFinal + DiffAo, exp2(-16.0 * SpecRough - 1.0)) - 1.0 + DiffAo);

    AccumulationHdrBuffer[PixelIdx]  = float4(SpecReflHdr, SpecRough);
    GBufPosMask[PixelIdx]            = float4(PrimaryHit.Position, 1.0 + SpecRough);
    if (QueryNaniteWireframeEnabled())
    {
        float WireKeep     = 1.0 - 0.72 * EvaluateAnalyticalWireframeAlpha(PrimaryHit, RayDir, PrimaryHit.EdgeDistMeters);
        DirectSurfaceHdr   *= WireKeep;
        UnoccludedSunHdr   *= WireKeep;
        DiffuseAlbedoMatte *= WireKeep;
        SpecReflHdr        *= WireKeep;
    }

    GBufNormAo[PixelIdx]             = float4(PrimaryHit.ShadingNormal, DiffAo);
    GBufDirectBase[PixelIdx]         = float4(DirectSurfaceHdr, HitDistForClouds);
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

// XeGTAO-style ground-truth ambient occlusion: multi-slice horizon search over the G-buffer.
// Each slice integrates cosine-weighted visibility between the two horizon angles found by
// marching the neighbouring world positions, exactly the XeGTAO visibility integral. It shades
// direct PBR ambient/indirect terms only (no GI is added). Toggle + strength ride in
// GlobalDistanceBakeControl.w (bit0, bits 8..15 = strength x255/2).
bool IsXeGtaoEnabled()
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
    float3 ViewToward = ToSurface / ViewDist;      // camera -> surface
    float3 ViewFrom   = -ViewToward;               // surface -> camera (V)

    const float SearchRadiusMeters = 0.85;
    float RadiusPx = clamp(SearchRadiusMeters * (float(Height) * 0.5) / (ViewDist * TanHalfY), 3.0, 48.0);

    const uint SliceCount = 4u;
    const uint StepsPerSide = 4u;
    float VisibilitySum = 0.0;

    for (uint SliceIdx = 0u; SliceIdx < SliceCount; ++SliceIdx)
    {
        float  SliceAngle = float(SliceIdx) * 0.7853981; // pi / 4
        float2 SliceScreenDir = float2(cos(SliceAngle), sin(SliceAngle));
        float3 SliceWorldDir  = normalize(CamRight * SliceScreenDir.x + CamUp * SliceScreenDir.y);
        float3 SliceAxis      = SliceWorldDir - ViewFrom * dot(SliceWorldDir, ViewFrom);
        float  SliceAxisLen   = length(SliceAxis);
        if (SliceAxisLen < 1.0e-4)
        {
            VisibilitySum += 1.0;
            continue;
        }
        SliceAxis /= SliceAxisLen;

        // Angle of the surface normal inside this slice, measured from V
        float NormalSlice = dot(WorldNormal, SliceAxis);
        float NormalView  = dot(WorldNormal, ViewFrom);
        float NormalAngle = atan2(NormalSlice, NormalView);

        float HorizonPlus  = NormalAngle;
        float HorizonMinus = NormalAngle;
        for (uint SideIdx = 0u; SideIdx < 2u; ++SideIdx)
        {
            float SideSign = (SideIdx == 0u) ? 1.0 : -1.0;
            for (uint StepIdx = 1u; StepIdx <= StepsPerSide; ++StepIdx)
            {
                float StepFrac = float(StepIdx) / float(StepsPerSide);
                int2 SampleCoord = int2(PixelCoord) + int2(
                    int(round(SliceScreenDir.x * SideSign * RadiusPx * StepFrac)),
                    int(round(-SliceScreenDir.y * SideSign * RadiusPx * StepFrac)));
                SampleCoord = clamp(SampleCoord, int2(0, 0), int2(int(Width) - 1, int(Height) - 1));
                uint   SampleIdx  = uint(SampleCoord.y) * Width + uint(SampleCoord.x);
                float4 SamplePos  = GBufPosMask[SampleIdx];
                if (SamplePos.w <= 0.0) continue;
                float3 SampleVec  = SamplePos.xyz - WorldPos;
                float  SampleDist = length(SampleVec);
                if (SampleDist < 1.0e-4 || SampleDist > SearchRadiusMeters * 1.6) continue;
                float3 SampleDir  = SampleVec / SampleDist;
                float  SampleAngle = atan2(dot(SampleDir, SliceAxis), dot(SampleDir, ViewFrom));
                if (SideSign > 0.0) HorizonPlus  = max(HorizonPlus, SampleAngle);
                else                HorizonMinus = min(HorizonMinus, SampleAngle);
            }
        }

        float UpperAngle = min(HorizonPlus, NormalAngle + 1.5707963);
        float LowerAngle = max(HorizonMinus, NormalAngle - 1.5707963);
        float SliceVisibility = clamp(
            (sin(UpperAngle - NormalAngle) - sin(LowerAngle - NormalAngle)) * 0.5,
            0.0, 1.0);
        VisibilitySum += SliceVisibility;
    }

    float Gtao = clamp(VisibilitySum / float(SliceCount), 0.0, 1.0);
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
        if (IsXeGtaoEnabled())
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
        FinalHdr = float3(OutDiffAo);
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
    uint CascadeIdx = DispatchThreadID.z + WorldUniforms.GlobalDistanceBakeControl.y;
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

float SampleLocalAssetSignedSdf(
    uint   VoxelFloatOffset,
    uint3  Res,
    float3 LocalMin,
    float3 LocalMax,
    float3 LocalP)
{
    float3 Span     = max(float3(1.0e-5), LocalMax - LocalMin);
    float3 ClampedP = clamp(LocalP, LocalMin, LocalMax);
    float3 Outside  = LocalP - ClampedP;
    float  OutDist  = length(Outside);

    float3 MaxIdxF = float3(max(uint3(1u), Res) - 1u);
    float3 GridF   = saturate((ClampedP - LocalMin) / Span) * MaxIdxF;
    uint3  Idx0    = min(uint3(floor(GridF)), max(uint3(1u), Res) - 1u);
    uint3  Idx1    = min(Idx0 + 1u, max(uint3(1u), Res) - 1u);
    float3 Frac    = GridF - float3(Idx0);

    uint StrideY   = Res.x;
    uint StrideZ   = Res.x * Res.y;
    uint Origin000 = VoxelFloatOffset + Idx0.z * StrideZ + Idx0.y * StrideY + Idx0.x;
    uint OffX      = Idx1.x - Idx0.x;
    uint OffY      = (Idx1.y - Idx0.y) * StrideY;
    uint OffZ      = (Idx1.z - Idx0.z) * StrideZ;

    float C000 = LocalAssetSdfVoxelBuffer[Origin000];
    float C100 = LocalAssetSdfVoxelBuffer[Origin000 + OffX];
    float C010 = LocalAssetSdfVoxelBuffer[Origin000 + OffY];
    float C110 = LocalAssetSdfVoxelBuffer[Origin000 + OffX + OffY];
    float C001 = LocalAssetSdfVoxelBuffer[Origin000 + OffZ];
    float C101 = LocalAssetSdfVoxelBuffer[Origin000 + OffX + OffZ];
    float C011 = LocalAssetSdfVoxelBuffer[Origin000 + OffY + OffZ];
    float C111 = LocalAssetSdfVoxelBuffer[Origin000 + OffX + OffY + OffZ];

    float C00 = lerp(C000, C100, Frac.x);
    float C10 = lerp(C010, C110, Frac.x);
    float C01 = lerp(C001, C101, Frac.x);
    float C11 = lerp(C011, C111, Frac.x);
    float C0  = lerp(C00, C10, Frac.y);
    float C1  = lerp(C01, C11, Frac.y);
    float GridDist = lerp(C0, C1, Frac.z);
    return (OutDist > 1.0e-5) ? sqrt(max(0.0, GridDist) * max(0.0, GridDist) + OutDist * OutDist) : GridDist;
}

float EvaluateAnalyticalPrimitiveLocalSdf(uint ShapeCode, float3 HalfExt, float3 P)
{
    if (ShapeCode == 2u) // Sphere
    {
        float R = max(1.0e-4, max(HalfExt.x, max(HalfExt.y, HalfExt.z)));
        return length(P) - R;
    }
    if (ShapeCode == 3u) // Cylinder (Z-axis)
    {
        float Rad = max(1.0e-4, max(HalfExt.x, HalfExt.y));
        float Hz  = max(1.0e-4, HalfExt.z);
        float2 D  = float2(length(P.xy) - Rad, abs(P.z) - Hz);
        return min(max(D.x, D.y), 0.0) + length(max(D, float2(0.0)));
    }
    if (ShapeCode == 4u) // Cone (Z-axis, tip at +Hz, foot radius Rad at -Hz)
    {
        float Rad = max(1.0e-4, max(HalfExt.x, HalfExt.y));
        float Hz  = max(1.0e-4, HalfExt.z);
        float H2  = 2.0 * Hz;
        float2 Q  = float2(length(P.xy), P.z - Hz);
        float2 K1 = float2(0.0, 0.0);
        float2 K2 = float2(-Rad, H2);
        float2 Ca = float2(Q.x - min(Q.x, (Q.y < 0.0) ? 0.0 : Rad), abs(Q.y + Hz) - Hz);
        float2 Cb = Q - K1 + K2 * saturate(dot(K1 - Q, K2) / max(1.0e-8, dot(K2, K2)));
        float  S  = (Cb.x < 0.0 && Ca.y < 0.0) ? -1.0 : 1.0;
        return S * sqrt(max(0.0, min(dot(Ca, Ca), dot(Cb, Cb))));
    }
    if (ShapeCode == 5u) // Torus (XY-plane)
    {
        float MinorR = max(1.0e-4, HalfExt.z);
        float MajorR = max(MinorR, max(HalfExt.x, HalfExt.y) - MinorR);
        float2 Q = float2(length(P.xy) - MajorR, P.z);
        return length(Q) - MinorR;
    }
    // Default ShapeCode == 1u: Box
    float3 Q = abs(P) - HalfExt;
    return length(max(Q, float3(0.0))) + min(max(Q.x, max(Q.y, Q.z)), 0.0);
}

float EvaluateInstanceLocalSdf(GpuDynamicLocalSdfInstance144 Inst, uint ShapeCode, uint3 Res, float3 LocalP)
{
    if (ShapeCode == 0u)
    {
        return SampleLocalAssetSignedSdf(
            Inst.ResolutionOffset.z,
            Res,
            Inst.LocalMinPad.xyz,
            Inst.LocalMaxPad.xyz,
            LocalP);
    }
    return EvaluateAnalyticalPrimitiveLocalSdf(ShapeCode, Inst.LocalMaxPad.xyz, LocalP);
}

void QueryActiveToroidalSlab(out uint3 OutSlabMin, out uint3 OutSlabMax)
{
    if (WorldUniforms.GlobalDistanceSlabBounds.w != 0u)
    {
        uint3 Packed = WorldUniforms.GlobalDistanceSlabBounds.xyz;
        OutSlabMin   = Packed & 0xFFFFu;
        OutSlabMax   = min(uint3(GdfGridRes), Packed >> 16u);
    }
    else
    {
        OutSlabMin = uint3(0u);
        OutSlabMax = uint3(GdfGridRes);
    }
}

[shader("compute")]
[numthreads(4, 4, 4)]
void GlobalDistanceBakeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint3 SlabMin = uint3(0u), SlabMax = uint3(GdfGridRes);
    QueryActiveToroidalSlab(SlabMin, SlabMax);
    uint3 LogicalCoord = SlabMin + DispatchThreadID;
    if (any(LogicalCoord >= SlabMax))
    {
        return;
    }

    uint   CascadeIdx = min(1u, WorldUniforms.GlobalDistanceBakeControl.y);
    float4 CenterSpan = (CascadeIdx == 0u)
        ? WorldUniforms.GlobalDistanceFineCenterSpan
        : WorldUniforms.GlobalDistanceCoarseCenterSpan;
    int3   MinCell    = (CascadeIdx == 0u)
        ? WorldUniforms.GlobalDistanceFineToroidalCell.xyz
        : WorldUniforms.GlobalDistanceCoarseToroidalCell.xyz;
    float  HalfSpan   = max(0.25, CenterSpan.w);
    float  VoxSize    = (2.0 * HalfSpan) / float(GdfGridRes);
    int3   WorldCell  = MinCell + int3(LogicalCoord);
    float3 VoxelPos   = (float3(WorldCell) + 0.5) * VoxSize;
    uint3  PhysCoord  = uint3(WorldCell & 63);

    uint   LinearIdx         = PhysCoord.z * (GdfGridRes * GdfGridRes) + PhysCoord.y * GdfGridRes + PhysCoord.x;
    uint   LocalSdfInstCount = (WorldUniforms.GlobalDistanceBakeControl.w >> 16u) & 0xFFFFu;
    float  MaxDist           = VoxSize * 5.5;
    float  SignedDist        = MaxDist;
    float3 BestNormal        = float3(0.0, 0.0, 1.0);
    uint   BestMat           = 0u;
    bool   FoundSurface      = false;

    if (LocalSdfInstCount > 0u)
    {
        float BestSignedDist  = MaxDist;
        float BestAbsDist     = MaxDist;
        float SurfaceBandDist = VoxSize * 1.45;

        [loop]
        for (uint InstIdx = 0u; InstIdx < LocalSdfInstCount; ++InstIdx)
        {
            GpuDynamicLocalSdfInstance144 Inst = DynamicLocalSdfInstanceBuffer[InstIdx];
            if ((Inst.ResolutionOffset.w >> 16u) == 0u)
            {
                continue;
            }

            float3 LocalP = float3(
                dot(Inst.WorldToLocalRow0.xyz, VoxelPos) + Inst.WorldToLocalRow0.w,
                dot(Inst.WorldToLocalRow1.xyz, VoxelPos) + Inst.WorldToLocalRow1.w,
                dot(Inst.WorldToLocalRow2.xyz, VoxelPos) + Inst.WorldToLocalRow2.w);

            float  MinWorldScale = max(1.0e-4, Inst.LocalToWorldNormRow0.w);
            float3 ClampedLocal  = clamp(LocalP, Inst.LocalMinPad.xyz, Inst.LocalMaxPad.xyz);
            float  BoxLowerWorld = length(LocalP - ClampedLocal) * MinWorldScale;
            if (BoxLowerWorld > BestAbsDist + 1.0e-4)
            {
                continue;
            }

            uint  ShapeCode = (Inst.ResolutionOffset.y >> 16u) & 0xFFFFu;
            uint3 Res       = uint3(
                Inst.ResolutionOffset.x & 0xFFFFu,
                (Inst.ResolutionOffset.x >> 16u) & 0xFFFFu,
                Inst.ResolutionOffset.y & 0xFFFFu);

            float LocalD = EvaluateInstanceLocalSdf(Inst, ShapeCode, Res, LocalP);
            float WorldD = LocalD * MinWorldScale;
            float AbsD   = abs(WorldD);

            // CSG union across opaque scene instances: preserve interior sign while tracking closest surface boundary
            BestSignedDist = FoundSurface ? min(BestSignedDist, WorldD) : WorldD;
            if (!FoundSurface || AbsD < BestAbsDist)
            {
                BestAbsDist  = AbsD;
                BestMat      = Inst.ResolutionOffset.w & 0xFFFFu;
                FoundSurface = true;

                if (CascadeIdx == 0u && AbsD <= SurfaceBandDist)
                {
                    float StepEps = (ShapeCode == 0u)
                        ? max(1.0e-3, max(Inst.LocalMinPad.w, max(Inst.LocalMaxPad.w, Inst.LocalToWorldNormRow1.w)) * 0.85)
                        : 0.008;
                    float Dx = EvaluateInstanceLocalSdf(Inst, ShapeCode, Res, LocalP + float3(StepEps, 0.0, 0.0))
                             - EvaluateInstanceLocalSdf(Inst, ShapeCode, Res, LocalP - float3(StepEps, 0.0, 0.0));
                    float Dy = EvaluateInstanceLocalSdf(Inst, ShapeCode, Res, LocalP + float3(0.0, StepEps, 0.0))
                             - EvaluateInstanceLocalSdf(Inst, ShapeCode, Res, LocalP - float3(0.0, StepEps, 0.0));
                    float Dz = EvaluateInstanceLocalSdf(Inst, ShapeCode, Res, LocalP + float3(0.0, 0.0, StepEps))
                             - EvaluateInstanceLocalSdf(Inst, ShapeCode, Res, LocalP - float3(0.0, 0.0, StepEps));
                    float3 LocalGrad = float3(Dx, Dy, Dz);
                    float3 WorldGrad = float3(
                        dot(Inst.LocalToWorldNormRow0.xyz, LocalGrad),
                        dot(Inst.LocalToWorldNormRow1.xyz, LocalGrad),
                        dot(Inst.LocalToWorldNormRow2.xyz, LocalGrad));
                    float GradLenSq = dot(WorldGrad, WorldGrad);
                    BestNormal = (GradLenSq > 1.0e-12) ? (WorldGrad * rsqrt(GradLenSq)) : float3(0.0, 0.0, 1.0);
                }
            }
        }

        SignedDist = FoundSurface ? clamp(BestSignedDist, -MaxDist, MaxDist) : MaxDist;
    }
    else
    {
        uint  EntryCount = uint(WorldUniforms.SkyRadianceBvhCount.w + 0.5);
        float BestDistSq = MaxDist * MaxDist;
        float BestAbsCos = -1.0;
        float BestSign   = 1.0;

        if (EntryCount > 0u)
        {
            uint Stack[32];
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

                    uint GatePlusOne = Cnt >> 4u;
                    if (GatePlusOne != 0u && !QueryClusterBitActive(GatePlusOne - 1u)) continue;

                    uint TriCnt = Cnt & 0xFu;
                    if (TriCnt > 0u)
                    {
                        uint Start = Ref & 0x7FFFFFFFu;
                        for (uint P = 0u; P < TriCnt; ++P)
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
                                float  CosVal  = dot(Diff, SmoothN) * InvDist;
                                float  AbsCos  = abs(CosVal);
                                bool StrictlyCloser = !FoundSurface || (DistSq < BestDistSq - TieEps);
                                if (StrictlyCloser || AbsCos > BestAbsCos)
                                {
                                    BestAbsCos   = StrictlyCloser ? AbsCos : max(BestAbsCos, AbsCos);
                                    BestDistSq   = min(BestDistSq, DistSq);
                                    BestSign     = (CosVal < -1.0e-4) ? -1.0 : 1.0;
                                    BestNormal   = SmoothN;
                                    BestMat      = uint(N2Slot.w + 0.5);
                                    FoundSurface = true;
                                }
                            }
                        }
                    }
                    else if (StackTop < 28)
                    {
                        Stack[++StackTop] = Ref;
                    }
                }
            }
        }

        SignedDist = FoundSurface ? (BestSign * sqrt(BestDistSq)) : MaxDist;
    }

    GlobalDistanceFieldBuffer[CascadeIdx * GdfVoxCnt + LinearIdx] = SignedDist;

    if (CascadeIdx == 0u)
    {
        float ShellBand = VoxSize * 0.92;
        float Opacity   = FoundSurface ? saturate(1.0 - abs(SignedDist) / ShellBand) : 0.0;
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
                uint3  LogicalSrc = BaseSrc + uint3(DX, DY, DZ);
                uint3  P = (DstMip == 1u)
                    ? uint3((WorldUniforms.GlobalDistanceFineToroidalCell.xyz + int3(LogicalSrc)) & 63)
                    : LogicalSrc;
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

    uint3 SlabMin = uint3(0u), SlabMax = uint3(GdfGridRes);
    QueryActiveToroidalSlab(SlabMin, SlabMax);
    uint3 LogicalCoord = SlabMin + DispatchThreadID;
    if (any(LogicalCoord >= SlabMax))
    {
        return;
    }

    int3  FineMinCell = WorldUniforms.GlobalDistanceFineToroidalCell.xyz;
    int3  WorldCell   = FineMinCell + int3(LogicalCoord);
    uint3 PhysCoord   = uint3(WorldCell & 63);
    uint  LinearIdx   = PhysCoord.z * (GdfGridRes * GdfGridRes) + PhysCoord.y * GdfGridRes + PhysCoord.x;
    uint2 ShellMeta   = GlobalSurfaceShellBuffer[LinearIdx];
    float Opacity     = float((ShellMeta.y >> 16u) & 0xFFFFu) * (1.0 / 65535.0);
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
    float  VoxSize    = (2.0 * HalfSpan) / float(GdfGridRes);
    float3 VoxelPos   = (float3(WorldCell) + 0.5) * VoxSize;
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
            Bounce1Irr += 0.25 * TraceSingleRadiosityCone(SurfPos, SurfN, StartP, ConeDir, 0.72, 4u, 0.34);
        }

        float4 PrevPrem = UnpackHalf4(GlobalRadiosityFieldBuffer[RadMip0Off + LinearIdx]);
        float3 BaseRad  = (PrevPrem.a > 1.0e-4) ? (PrevPrem.rgb / PrevPrem.a) : float3(0.0);
        float3 TotalRad = min(BaseRad + DiffAlbedo * Bounce1Irr, float3(28.0));
        GlobalRadiosityFieldBuffer[RadMip0Off + LinearIdx] = PackHalf4(float4(TotalRad * Opacity, Opacity));
    }
}
