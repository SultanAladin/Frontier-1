/*============================================================================================================================================
                                                    WORLDVIEWPORTRADIOMETRICCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Unified 5-Engine GPU Compute Shader:
// 0=MatCap, 1=OpenPbrDirect, 2=DdgiRaytracing (Chebyshev), 3=SdfGlobalIllumination (Radiosity Cone March), 4=DeterministicPathTrace (MIS/VNDF)
// Enforces Zero SDF Shadows (5 Shadow Lab algorithms over depth32float + BVH4) & Zero SDF Reflections (BVH4 or Sky Environment).

#define FRONTIER_ATMOSPHERE_EMBEDDED 1
#include "../Atmosphere/SkyViewTransmittanceCompute.slang"
#include "../Atmosphere/VolumetricCloudCompute.slang"
#include "../Atmosphere/CelestialDiscStarCompute.slang"

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
    GpuAreaQuadLight AreaLights[4];
    GpuGlassSphere   GlassSpheres[4];
    uint4                       AtmosphereMaskPad; // x = AtmosphereEnabled (0/1), y = SkyViewW, z = SkyViewH, w = Pad
    VolumetricCloudUniformBlock CloudParams;
    CelestialFieldUniformBlock  CelestialParams;
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

struct GpuWorldMaterial64
{
    float4 DiffuseAlbedoRoughness; // xyz = EffectiveDiffuseAlbedo, w = DiffuseRoughness
    float4 SpecParams;             // x = SpecRoughness, y = Metalness, z = DielectricIOR, w = Anisotropy
    float4 EdgeTintCoatWeight;     // xyz = EdgeTintF82, w = CoatWeight
    float4 CoatRoughnessOptics;    // x = CoatRoughness, y = TransmissionWeight, z = AbbeDispersion, w = Pad
};

[[vk::binding(0,  0)]] ConstantBuffer<WorldViewportUniformBlock> WorldUniforms;
[[vk::binding(1,  0)]] StructuredBuffer<GpuWideTreeEntry128>     SceneBvhEntries;
[[vk::binding(2,  0)]] StructuredBuffer<GpuShadedTriangle96>     SceneTriangles;
[[vk::binding(3,  0)]] StructuredBuffer<GpuWorldMaterial64>      MaterialEntries;
[[vk::binding(4,  0)]] StructuredBuffer<float>                   SignedDistanceGrid;
[[vk::binding(5,  0)]] StructuredBuffer<float4>                  IrradianceLatticeTexels;
[[vk::binding(6,  0)]] RWStructuredBuffer<uint>                  OutDisplayRgba8;
[[vk::binding(7,  0)]] StructuredBuffer<float>                   SolarShadowDepthBuffer;
[[vk::binding(8,  0)]] StructuredBuffer<float2>                  IrradianceChebyshevDepth;
[[vk::binding(9,  0)]] StructuredBuffer<float4>                  SdfRadiosityVoxels;
[[vk::binding(10, 0)]] RWStructuredBuffer<float4>                AccumulationHdrBuffer;
[[vk::binding(11, 0)]] StructuredBuffer<float4>                  AtmosphericSkyViewLut;
[[vk::binding(12, 0)]] StructuredBuffer<uint>                    LunarSurfaceAtlasTexels;

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

float3 EvaluateSkyDome(float3 Dir)
{
    if (WorldUniforms.AtmosphereMaskPad.x != 0u)
    {
        return EvaluateSkyViewLutSample(
            AtmosphericSkyViewLut,
            WorldUniforms.AtmosphereMaskPad.y,
            WorldUniforms.AtmosphereMaskPad.z,
            Dir);
    }
    float Up = saturate(Dir.z * 0.5 + 0.5);
    return WorldUniforms.SkyRadianceBvhCount.xyz * (0.65 + 0.70 * Up);
}

float3 EvaluateBackgroundSkyCloudsGpu(float3 RayDir, float2 PixelCoord)
{
    if (WorldUniforms.AtmosphereMaskPad.x == 0u)
    {
        return EvaluateSkyDome(RayDir);
    }
    float3 SkyRad       = EvaluateSkyDome(RayDir);
    float3 CelestialRad = EvaluateCelestialBodiesGpu(WorldUniforms.CelestialParams, LunarSurfaceAtlasTexels, RayDir);
    float  CloudTrans   = 1.0;
    float3 CloudRad     = EvaluateVolumetricCloudsGpu(WorldUniforms.CloudParams, RayDir, PixelCoord, CloudTrans);
    return (SkyRad + CelestialRad) * CloudTrans + CloudRad;
}

float EvaluateSolarCloudTransmittanceGpu(float3 WorldPos, float3 SunDir)
{
    VolumetricCloudUniformBlock C = WorldUniforms.CloudParams;
    if (WorldUniforms.AtmosphereMaskPad.x == 0u || C.SunAxisToggle.w < 0.5 || C.WindVectorOcclude.z < 0.5 || SunDir.z <= 0.01)
    {
        return 1.0;
    }
    float WindX   = (C.SkyColorWind.w > 0.5) ? (C.WindVectorOcclude.x * C.OriginTimeVector.w) : 0.0;
    float WindY   = (C.SkyColorWind.w > 0.5) ? (C.WindVectorOcclude.y * C.OriginTimeVector.w) : 0.0;
    float PlanetR = max(500000.0, C.SolarIrradianceExt.w);
    float FloorM  = max(100.0, C.DeckGeometry.x);
    float ThickM  = max(100.0, C.DeckGeometry.y);
    float MidAlt  = FloorM + ThickM * 0.45;
    float DistM   = MidAlt / max(0.05, SunDir.z);
    float3 P      = float3(WorldPos.x + SunDir.x * DistM, WorldPos.y + SunDir.y * DistM, PlanetR + 100.0 + MidAlt);
    float FScale  = 0.00065 / max(0.08, C.DeckOptics.x);
    float D = SampleCloudDeckDensityGpu(
        P, PlanetR, FloorM, ThickM, clamp(C.DeckGeometry.z, 0.0, 1.0), clamp(C.DeckOptics.y, 0.0, 1.0),
        max(0.0, C.DeckGeometry.w) * 0.0085, FScale, WindX, WindY, C.DeckOptics.w, uint(C.FormationOptics.x + 0.5), 8000.0);
    return 0.32 + 0.68 * exp(-D * ThickM * 0.22);
}

float IntersectTriangleMT(float3 Origin, float3 Dir, float MinT, float MaxT, uint TriIndex, out float2 OutUV)
{
    OutUV = float2(0.0, 0.0);
    float3 Edge01  = SceneTriangles[TriIndex].Edge01Pad.xyz;
    float3 Edge02  = SceneTriangles[TriIndex].Edge02Pad.xyz;
    float3 PVec    = cross(Dir, Edge02);
    float  Det     = dot(Edge01, PVec);
    if (abs(Det) <= 1.0e-8) return -1.0;

    float  InvDet  = 1.0 / Det;
    float3 Corner0 = SceneTriangles[TriIndex].Corner0Pad.xyz;
    float3 TVec    = Origin - Corner0;
    float  U       = dot(TVec, PVec) * InvDet;
    if (U < 0.0 || U > 1.0) return -1.0;

    float3 QVec = cross(TVec, Edge01);
    float  V    = dot(Dir, QVec) * InvDet;
    if (V < 0.0 || (U + V) > 1.0) return -1.0;

    float HitT = dot(Edge02, QVec) * InvDet;
    if (HitT < MinT || HitT > MaxT) return -1.0;

    OutUV = float2(U, V);
    return HitT;
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
            else if (StackTop < 30)
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

            if (Cnt > 0u)
            {
                uint Start = Ref & 0x7FFFFFFFu;
                for (uint P = 0u; P < Cnt; ++P)
                {
                    float2 DummyUV;
                    if (IntersectTriangleMT(Origin, Dir, MinT, MaxT, Start + P, DummyUV) > 0.0)
                    {
                        return true;
                    }
                }
            }
            else if (StackTop < 30)
            {
                Stack[++StackTop] = Ref;
            }
        }
    }
    return false;
}

float SampleSdfGrid(float3 WorldPos)
{
    uint3 Dims = WorldUniforms.SdfDimsLightCount.xyz;
    if (Dims.x < 2u || Dims.y < 2u || Dims.z < 2u) return 4.0;

    float Spacing = max(1.0e-4, WorldUniforms.SdfMinSpacing.w);
    float3 Rel    = (WorldPos - WorldUniforms.SdfMinSpacing.xyz) / Spacing;
    int3   MaxIdx = int3(Dims) - int3(1);
    if (Rel.x < 0.0 || Rel.y < 0.0 || Rel.z < 0.0 ||
        Rel.x > float(MaxIdx.x) || Rel.y > float(MaxIdx.y) || Rel.z > float(MaxIdx.z))
    {
        return 2.5;
    }

    int3   P0  = clamp(int3(floor(Rel)), int3(0), MaxIdx);
    int3   P1  = min(P0 + int3(1), MaxIdx);
    float3 T   = saturate(Rel - float3(P0));
    uint   SX  = Dims.x;
    uint   SXY = Dims.x * Dims.y;

    float C00 = lerp(SignedDistanceGrid[uint(P0.z) * SXY + uint(P0.y) * SX + uint(P0.x)],
                     SignedDistanceGrid[uint(P0.z) * SXY + uint(P0.y) * SX + uint(P1.x)], T.x);
    float C10 = lerp(SignedDistanceGrid[uint(P0.z) * SXY + uint(P1.y) * SX + uint(P0.x)],
                     SignedDistanceGrid[uint(P0.z) * SXY + uint(P1.y) * SX + uint(P1.x)], T.x);
    float C01 = lerp(SignedDistanceGrid[uint(P1.z) * SXY + uint(P0.y) * SX + uint(P0.x)],
                     SignedDistanceGrid[uint(P1.z) * SXY + uint(P0.y) * SX + uint(P1.x)], T.x);
    float C11 = lerp(SignedDistanceGrid[uint(P1.z) * SXY + uint(P1.y) * SX + uint(P0.x)],
                     SignedDistanceGrid[uint(P1.z) * SXY + uint(P1.y) * SX + uint(P1.x)], T.x);
    return lerp(lerp(C00, C10, T.y), lerp(C01, C11, T.y), T.z);
}

float3 SampleSdfRadiosityTrilinear(float3 WorldPos)
{
    uint3 Dims = WorldUniforms.SdfDimsLightCount.xyz;
    if (Dims.x < 2u || Dims.y < 2u || Dims.z < 2u) return float3(0.0);

    float Spacing = max(1.0e-4, WorldUniforms.SdfMinSpacing.w);
    float3 Rel    = (WorldPos - WorldUniforms.SdfMinSpacing.xyz) / Spacing;
    int3   MaxIdx = int3(Dims) - int3(1);
    if (Rel.x < 0.0 || Rel.y < 0.0 || Rel.z < 0.0 ||
        Rel.x > float(MaxIdx.x) || Rel.y > float(MaxIdx.y) || Rel.z > float(MaxIdx.z))
    {
        return float3(0.0);
    }

    int3   P0  = clamp(int3(floor(Rel)), int3(0), MaxIdx);
    int3   P1  = min(P0 + int3(1), MaxIdx);
    float3 T   = saturate(Rel - float3(P0));
    uint   SX  = Dims.x;
    uint   SXY = Dims.x * Dims.y;

    float3 C00 = lerp(SdfRadiosityVoxels[uint(P0.z) * SXY + uint(P0.y) * SX + uint(P0.x)].xyz,
                      SdfRadiosityVoxels[uint(P0.z) * SXY + uint(P0.y) * SX + uint(P1.x)].xyz, T.x);
    float3 C10 = lerp(SdfRadiosityVoxels[uint(P0.z) * SXY + uint(P1.y) * SX + uint(P0.x)].xyz,
                      SdfRadiosityVoxels[uint(P0.z) * SXY + uint(P1.y) * SX + uint(P1.x)].xyz, T.x);
    float3 C01 = lerp(SdfRadiosityVoxels[uint(P1.z) * SXY + uint(P0.y) * SX + uint(P0.x)].xyz,
                      SdfRadiosityVoxels[uint(P1.z) * SXY + uint(P0.y) * SX + uint(P1.x)].xyz, T.x);
    float3 C11 = lerp(SdfRadiosityVoxels[uint(P1.z) * SXY + uint(P1.y) * SX + uint(P0.x)].xyz,
                      SdfRadiosityVoxels[uint(P1.z) * SXY + uint(P1.y) * SX + uint(P1.x)].xyz, T.x);
    return lerp(lerp(C00, C10, T.y), lerp(C01, C11, T.y), T.z);
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

float3 EvaluateSurfaceBrdf(GpuWorldMaterial64 Mat, float3 N, float3 V, float3 L)
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

float3 EvaluateAreaQuadLight(GpuAreaQuadLight Quad, float3 Pos, float3 N, float3 V, GpuWorldMaterial64 Mat)
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

bool ProjectSunCascade(float3 WorldPos, out float2 OutTexelXY, out float OutRecD, out float OutTexelM, out uint OutRes)
{
    OutTexelXY = float2(0.0);
    OutRecD    = 0.0;
    OutTexelM  = 0.015;
    OutRes     = uint(WorldUniforms.SunCascadeRightRes.w + 0.5);
    float HalfSpan = WorldUniforms.SunCascadeCenterSpan.w;
    if (OutRes < 4u || HalfSpan <= 1.0e-4) return false;

    float3 Rel    = WorldPos - WorldUniforms.SunCascadeCenterSpan.xyz;
    float  LocalU = dot(Rel, WorldUniforms.SunCascadeRightRes.xyz);
    float  LocalV = dot(Rel, WorldUniforms.SunCascadeUpDepth.xyz);
    float  LocalZ = dot(Rel, WorldUniforms.SunCascadeForwardPad.xyz);
    float  Margin = HalfSpan * 0.94;
    if (abs(LocalU) > Margin || abs(LocalV) > Margin) return false;

    float ResF     = float(OutRes);
    float InvSpan2 = 1.0 / (2.0 * HalfSpan);
    OutTexelXY.x   = ((LocalU + HalfSpan) * InvSpan2) * ResF;
    OutTexelXY.y   = ((HalfSpan - LocalV) * InvSpan2) * ResF;
    OutRecD        = LocalZ + WorldUniforms.SunCascadeUpDepth.w;
    OutTexelM      = (2.0 * HalfSpan) / ResF;
    return true;
}

float EvaluateSolarShadow(RayHitReading Hit, float3 SunDir, uint2 PixelXY)
{
    uint ShadowAlg = WorldUniforms.EngineShadowReflSample.y;
    float NdotL    = dot(Hit.ShadingNormal, SunDir);
    if (NdotL <= 1.0e-5) return 0.0;

    float TanSun = max(0.004, WorldUniforms.SunDirConeTan.w);
    uint  Seed   = PixelXY.x * 1973u + PixelXY.y * 9277u + 89173u;
    float RotPhi = float(Seed & 0xFFFFu) * (TauConst / 65536.0);

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
        return Vis;
    }

    float2 CenterTXY;
    float  RecD, TexelM;
    uint   Res;
    float3 BiasedPos = Hit.Position + Hit.GeomNormal * 0.008;
    if (!ProjectSunCascade(BiasedPos, CenterTXY, RecD, TexelM, Res))
    {
        float3 Orig = Hit.Position + Hit.GeomNormal * 0.012;
        return TraceSceneAnyHit(Orig, SunDir, 0.028, 90.0) ? 0.0 : 1.0;
    }

    int MaxIdx       = int(Res) - 1;
    float SlopeScale = sqrt(max(0.0, 1.0 - NdotL * NdotL)) / max(0.15, NdotL);
    float Bias       = TexelM * (1.25 + 0.90 * SlopeScale);

    // Algorithm 3: PcfBilinear (3x3 Tent PCF)
    if (ShadowAlg == 3u)
    {
        int BaseX = int(floor(CenterTXY.x));
        int BaseY = int(floor(CenterTXY.y));
        const float KW[3] = { 0.25, 0.50, 0.25 };
        float Vis = 0.0;
        for (int DY = -1; DY <= 1; ++DY)
        {
            int TY = clamp(BaseY + DY, 0, MaxIdx);
            for (int DX = -1; DX <= 1; ++DX)
            {
                int TX = clamp(BaseX + DX, 0, MaxIdx);
                float D = SolarShadowDepthBuffer[uint(TY) * Res + uint(TX)];
                if ((RecD - D) <= Bias) Vis += KW[DY + 1] * KW[DX + 1];
            }
        }
        return Vis;
    }

    // Algorithm 2: ShadowRayMarch (EEVEE-Next Quadratic SMRT)
    if (ShadowAlg == 2u)
    {
        float MaxMarchM  = 2.60;
        float MaxStepPx  = clamp((MaxMarchM * TanSun) / TexelM, 2.0, 36.0);
        float StepZM     = MaxMarchM / 196.0;
        float ThicknessM = max(0.08, TexelM * 26.0);
        float Lit        = 0.0;
        for (uint R = 0u; R < 4u; ++R)
        {
            float Rad   = sqrt((float(R) + 0.5) * 0.25);
            float Ang   = float(R) * 2.39996323 + RotPhi;
            float2 StepDir = float2(cos(Ang), sin(Ang)) * (Rad * MaxStepPx);
            bool Occ = false;
            for (uint S = 1u; S <= 14u; ++S)
            {
                float TNorm = float(S) / 14.0;
                float TQuad = TNorm * TNorm;
                int TX = clamp(int(floor(CenterTXY.x + StepDir.x * TQuad)), 0, MaxIdx);
                int TY = clamp(int(floor(CenterTXY.y + StepDir.y * TQuad)), 0, MaxIdx);
                float RayD     = RecD - StepZM * float(S * S);
                float CascadeD = SolarShadowDepthBuffer[uint(TY) * Res + uint(TX)];
                float Diff     = RayD - CascadeD;
                if (Diff > Bias && Diff < ThicknessM)
                {
                    Occ = true;
                    break;
                }
            }
            if (!Occ) Lit += 0.25;
        }
        return Lit;
    }

    // Algorithms 0 (ContactPcss) & 1 (PcssPlaneAware): Vogel Blocker Search + Continuous Penumbra PCF
    float SearchRadPx = clamp((TanSun * 3.5) / TexelM, 2.0, 18.0);
    float BlockerSum  = 0.0;
    uint  Blockers    = 0u;
    for (uint Tap = 0u; Tap < 12u; ++Tap)
    {
        float R   = sqrt((float(Tap) + 0.5) / 12.0) * SearchRadPx;
        float Ang = float(Tap) * 2.39996323 + RotPhi;
        int TX = clamp(int(floor(CenterTXY.x + R * cos(Ang))), 0, MaxIdx);
        int TY = clamp(int(floor(CenterTXY.y + R * sin(Ang))), 0, MaxIdx);
        float D = SolarShadowDepthBuffer[uint(TY) * Res + uint(TX)];
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
        float FilterPx    = clamp((MeanBlocker * TanSun * 2.15) / TexelM, 1.15, 22.0);
        float LitSum      = 0.0;
        for (uint Tap = 0u; Tap < 16u; ++Tap)
        {
            float R   = sqrt((float(Tap) + 0.5) / 16.0) * FilterPx;
            float Ang = float(Tap) * 2.39996323 + RotPhi + 0.71;
            int TX = clamp(int(floor(CenterTXY.x + R * cos(Ang))), 0, MaxIdx);
            int TY = clamp(int(floor(CenterTXY.y + R * sin(Ang))), 0, MaxIdx);
            float D = SolarShadowDepthBuffer[uint(TY) * Res + uint(TX)];
            if ((RecD - D) <= (Bias + TexelM * 0.16 * R))
            {
                LitSum += 1.0;
            }
        }
        Vis = LitSum / 16.0;
    }

    if (ShadowAlg == 0u)
    {
        // Short 0.24 m BVH4 Contact Gate eliminating cascade detachment at contacts!
        float3 COrig = Hit.Position + Hit.GeomNormal * 0.008;
        if (TraceSceneAnyHit(COrig, SunDir, 0.004, 0.24))
        {
            Vis = min(Vis, 0.08);
        }
    }
    return Vis;
}

//------------------------------------------------------------------------------------------------------------------------
// GLOBAL ILLUMINATION ENGINES: DDGI CHEBYSHEV (2) vs SDF RADIOSITY CONE MARCH (3)
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

                float Backface = max(1.0e-4, (dot(-DirFromC, Normal) + 1.0) * 0.5);
                float VisW     = Backface * Backface + 1.0e-4;

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
                Accum          += IrradianceLatticeTexels[CellId * 64u + TexId].xyz * CombinedW;
                TotalW         += CombinedW;
            }
        }
    }
    return (TotalW > 0.0) ? (Accum / TotalW) : float3(0.0);
}

// Strictly reserved for EngineMode == 3 (SdfGlobalIllumination) GI only!
float3 EvaluateSdfRadiosityConeGi(float3 WorldPos, float3 Normal, uint2 PixelXY)
{
    float3 T, B;
    ConstructOrthonormalBasis(Normal, T, B);

    float IgnPhase  = frac(52.9829189 * frac(0.06711056 * float(PixelXY.x) + 0.00583715 * float(PixelXY.y)));
    float RotOffset = IgnPhase * TauConst;
    float Spacing   = max(0.05, WorldUniforms.SdfMinSpacing.w);
    float3 Origin   = WorldPos + Normal * (Spacing * 0.90);

    float3 Accum = float3(0.0);
    for (uint C = 0u; C < 16u; ++C)
    {
        float U1   = (float(C) + 0.5) / 16.0;
        float Phi  = float(C) * 2.39996323 + RotOffset;
        float SinT = sqrt(U1);
        float CosT = sqrt(max(0.0, 1.0 - U1));
        float3 Dir = T * (SinT * cos(Phi)) + B * (SinT * sin(Phi)) + Normal * CosT;

        float  DistM   = max(0.06, Spacing * 0.65);
        float  SkyVis  = 1.0;
        float3 BounceR = float3(0.0);
        for (uint Step = 0u; Step < 20u && DistM < 7.0; ++Step)
        {
            float3 SPos   = Origin + Dir * DistM;
            float  SdfD   = SampleSdfGrid(SPos);
            float  ConeR  = max(0.04, DistM * 0.42);
            if (SdfD < ConeR)
            {
                float  Occ   = saturate(1.0 - SdfD / ConeR);
                float3 VRad  = SampleSdfRadiosityTrilinear(SPos);
                BounceR     += VRad * (Occ * SkyVis);
                SkyVis      *= (1.0 - Occ * 0.68);
                if (SkyVis < 0.04)
                {
                    SkyVis = 0.0;
                    break;
                }
            }
            DistM += max(Spacing * 0.55, abs(SdfD) * 0.85);
        }
        Accum += (EvaluateSkyDome(Dir) * SkyVis + BounceR) * (1.0 / 16.0);
    }
    return clamp(Accum, float3(0.0), float3(6.5));
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
    GpuWorldMaterial64 Mat = MaterialEntries[min(Hit.MaterialSlot, MatCount - 1u)];
    float3 V      = -RayDir;
    float3 N      = Hit.ShadingNormal;
    float3 SunDir = WorldUniforms.SunDirConeTan.xyz;
    float3 SunIrr = WorldUniforms.SolarIrradianceTier.xyz;

    float3 SafeSecondaryPos = Hit.Position + Hit.GeomNormal * 0.012 + N * 0.006;
    float3 ColorHdr = float3(0.0);
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
    bool   HasDdgi = (WorldUniforms.EngineShadowReflSample.x == 2u && WorldUniforms.SolarIrradianceTier.w > 0.5);
    float3 AmbGi   = HasDdgi
                        ? SampleRtxIrradianceLatticeChebyshev(Hit.Position, N, V)
                        : (WorldUniforms.SkyRadianceBvhCount.xyz * 0.42);
    ColorHdr += Albedo * AmbGi;
    return ColorHdr;
}

float3 EvaluateMatCapSurface(RayHitReading Hit)
{
    uint MatCount = max(1u, WorldUniforms.LatticeDimsMaterialCount.w);
    GpuWorldMaterial64 Mat = MaterialEntries[min(Hit.MaterialSlot, MatCount - 1u)];
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
         + float3(0.26, 0.28, 0.32) * RimGlow;
}

float3 ShadePrimarySurface(RayHitReading Hit, float3 ViewDir, uint2 PixelXY)
{
    uint EngineMode = WorldUniforms.EngineShadowReflSample.x;
    uint MatCount   = max(1u, WorldUniforms.LatticeDimsMaterialCount.w);
    GpuWorldMaterial64 Mat = MaterialEntries[min(Hit.MaterialSlot, MatCount - 1u)];
    float3 N      = Hit.ShadingNormal;
    float3 SunDir = WorldUniforms.SunDirConeTan.xyz;
    float3 SunIrr = WorldUniforms.SolarIrradianceTier.xyz;

    uint DebugMode = uint(WorldUniforms.LatticeMaxDebugMode.w + 0.5);
    if (DebugMode == 1u) return float3(saturate(1.0 - Hit.Distance / 16.0));
    if (DebugMode == 2u) return Mat.DiffuseAlbedoRoughness.xyz;
    if (DebugMode == 3u) return N * 0.5 + 0.5;
    if (DebugMode == 4u) return float3(Mat.SpecParams.x);

    if (EngineMode == 0u)
    {
        return EvaluateMatCapSurface(Hit);
    }

    // 1. Direct Solar Shadow (Shadow Lab modes — ZERO SDF shadows!) + OpenPBR Shading
    float3 DirectHdr      = float3(0.0);
    float3 SafePrimaryPos = Hit.Position + Hit.GeomNormal * 0.012 + N * 0.006;
    float  SunVis         = EvaluateSolarShadow(Hit, SunDir, PixelXY) * EvaluateSolarCloudTransmittanceGpu(Hit.Position, SunDir);
    if (SunVis > 0.001)
    {
        DirectHdr += EvaluateSurfaceBrdf(Mat, N, ViewDir, SunDir) * SunIrr * SunVis;
    }

    // 2. Upper-Hemisphere Clipped Polygonal LTC Quad Area Lights
    uint LightCnt = min(4u, WorldUniforms.SdfDimsLightCount.w);
    for (uint L = 0u; L < LightCnt; ++L)
    {
        DirectHdr += EvaluateAreaQuadLight(WorldUniforms.AreaLights[L], SafePrimaryPos, N, ViewDir, Mat);
    }

    // 3. Fast BVH4 Ambient Occlusion (Zero SDF AO!) + Lagarde Specular AO
    float DiffAo = 1.0;
    {
        float3 T, B;
        ConstructOrthonormalBasis(N, T, B);
        float3 AoOrig = Hit.Position + Hit.GeomNormal * 0.008;
        float  OccSum = 0.0;
        for (uint R = 0u; R < 4u; ++R)
        {
            float U1 = (float(R) + 0.5) * 0.25;
            float Phi = float(R) * 2.39996323 + float(PixelXY.x * 13u + PixelXY.y * 29u);
            float SinT = sqrt(U1);
            float CosT = sqrt(max(0.0, 1.0 - U1));
            float3 AoDir = T * (SinT * cos(Phi)) + B * (SinT * sin(Phi)) + N * CosT;
            if (!TraceSceneAnyHit(AoOrig, AoDir, 0.006, 0.85))
            {
                OccSum += 0.25;
            }
        }
        DiffAo = saturate(0.28 + 0.72 * OccSum);
    }
    float NdotV     = max(1.0e-3, dot(N, ViewDir));
    float SpecRough = clamp(Mat.SpecParams.x, 0.025, 1.0);
    float SpecAo    = saturate(pow(NdotV + DiffAo, exp2(-16.0 * SpecRough - 1.0)) - 1.0 + DiffAo);

    // 4. Engine-Specific Diffuse GI
    float  Metalness = saturate(Mat.SpecParams.y);
    float  TransW    = saturate(Mat.CoatRoughnessOptics.y);
    float3 DiffuseGi = float3(0.0);
    if (EngineMode == 1u)
    {
        // OpenPbrDirect: zero indirect GI, calibrated flat sky dome fill
        DiffuseGi = WorldUniforms.SkyRadianceBvhCount.xyz * 0.20 * Mat.DiffuseAlbedoRoughness.xyz * (1.0 - Metalness) * (1.0 - TransW) * DiffAo;
    }
    else if (EngineMode == 2u)
    {
        float3 IndirectOverPi = SampleRtxIrradianceLatticeChebyshev(Hit.Position, N, ViewDir);
        DiffuseGi = IndirectOverPi * Mat.DiffuseAlbedoRoughness.xyz * (1.0 - Metalness) * (1.0 - TransW) * (0.35 + 0.65 * DiffAo);
    }
    else if (EngineMode == 3u)
    {
        float3 IndirectOverPi = EvaluateSdfRadiosityConeGi(Hit.Position, N, PixelXY);
        DiffuseGi = IndirectOverPi * Mat.DiffuseAlbedoRoughness.xyz * (1.0 - Metalness) * (1.0 - TransW) * (0.35 + 0.65 * DiffAo);
    }

    // 5. Secondary Specular Reflection (BVH4 WideTreeRaytrace or SkyEnvironment — Zero SDF!)
    float3 SpecGi = float3(0.0);
    float  CoatW  = saturate(Mat.EdgeTintCoatWeight.w);
    if (Metalness > 0.20 || CoatW > 0.20 || SpecRough < 0.24)
    {
        float3 ReflDir = reflect(-ViewDir, N);
        float3 EnvRad  = ShadeSecondaryBounce(Hit.Position + Hit.GeomNormal * 3.0e-3, ReflDir);
        float  Ior     = max(1.02, Mat.SpecParams.z);
        float  F0Diel  = pow((Ior - 1.0) / (Ior + 1.0), 2.0);
        float3 F0      = lerp(float3(F0Diel), Mat.DiffuseAlbedoRoughness.xyz, Metalness);
        float  Schlick = pow(1.0 - NdotV, 5.0);
        float3 Fresnel = F0 + (float3(1.0) - F0) * Schlick;
        float  Gloss   = saturate(1.0 - SpecRough * 0.85);
        if (CoatW > 0.15 && Metalness < 0.2)
        {
            Fresnel = max(Fresnel, float3((0.04 + 0.96 * Schlick) * CoatW));
            Gloss   = max(Gloss, 1.0 - Mat.CoatRoughnessOptics.x);
        }
        SpecGi = EnvRad * Fresnel * Gloss * SpecAo;
    }

    return DirectHdr + DiffuseGi + SpecGi;
}

//------------------------------------------------------------------------------------------------------------------------
// DETERMINISTIC PATH TRACER (EngineMode == 4): Progressive MIS + NEE + Heitz GGX VNDF
//------------------------------------------------------------------------------------------------------------------------

float3 SampleGgxVndfLocal(float3 LocalWo, float AlphaX, float AlphaY, float U1, float U2)
{
    float3 Vh  = normalize(float3(AlphaX * LocalWo.x, AlphaY * LocalWo.y, LocalWo.z));
    float  Len = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1  = (Len > 1.0e-7) ? (float3(-Vh.y, Vh.x, 0.0) * rsqrt(Len)) : float3(1.0, 0.0, 0.0);
    float3 T2  = cross(Vh, T1);
    float  R   = sqrt(U1);
    float  Phi = TauConst * U2;
    float  P1  = R * cos(Phi);
    float  P2  = R * sin(Phi);
    float  S   = 0.5 * (1.0 + Vh.z);
    P2 = lerp(sqrt(max(0.0, 1.0 - P1 * P1)), P2, S);
    float3 Nh  = T1 * P1 + T2 * P2 + Vh * sqrt(max(0.0, 1.0 - P1 * P1 - P2 * P2));
    return normalize(float3(AlphaX * Nh.x, AlphaY * Nh.y, max(1.0e-4, Nh.z)));
}

float3 TraceDeterministicPathSample(float3 CamOrigin, float3 CamRayDir, uint2 PixelXY, uint SampleIdx)
{
    uint   Rng        = (PixelXY.x * 1973u + PixelXY.y * 9277u + SampleIdx * 26699u + 911u) | 1u;
    uint   MaxBounces = clamp(uint(WorldUniforms.SunCascadeForwardPad.w + 0.5), 1u, 6u);
    uint   MatCount   = max(1u, WorldUniforms.LatticeDimsMaterialCount.w);
    float3 SunDir     = WorldUniforms.SunDirConeTan.xyz;
    float3 SunIrr     = WorldUniforms.SolarIrradianceTier.xyz;
    float  TanSun     = max(0.004, WorldUniforms.SunDirConeTan.w);
    float3 SunT, SunB;
    ConstructOrthonormalBasis(SunDir, SunT, SunB);

    float3 Radiance   = float3(0.0);
    float3 Throughput = float3(1.0);
    float3 RayO       = CamOrigin;
    float3 RayD       = CamRayDir;

    for (uint B = 0u; B < MaxBounces; ++B)
    {
        RayHitReading Hit = TraceSceneClosest(RayO, RayD, 1.5e-3, 120.0);
        if (!Hit.Hit)
        {
            float3 SkyB = (B == 0u) ? EvaluateBackgroundSkyCloudsGpu(RayD, float2(PixelXY)) : EvaluateSkyDome(RayD);
            Radiance += Throughput * SkyB;
            break;
        }

        GpuWorldMaterial64 Mat = MaterialEntries[min(Hit.MaterialSlot, MatCount - 1u)];
        float3 N = Hit.ShadingNormal;
        float3 V = -RayD;
        float3 T, BVec;
        ConstructOrthonormalBasis(N, T, BVec);

        // NEE: Solar Disc
        {
            float JR = sqrt(PcgFloat01(Rng)) * TanSun;
            float JA = PcgFloat01(Rng) * TauConst;
            float3 L = normalize(SunDir + SunT * (JR * cos(JA)) + SunB * (JR * sin(JA)));
            if (dot(N, L) > 0.0 && !TraceSceneAnyHit(Hit.Position + Hit.GeomNormal * 0.008, L, 0.010, 90.0))
            {
                Radiance += Throughput * EvaluateSurfaceBrdf(Mat, N, V, L) * SunIrr * EvaluateSolarCloudTransmittanceGpu(Hit.Position, L);
            }
        }

        // NEE: Area Quad Lights
        uint LightCnt = min(4u, WorldUniforms.SdfDimsLightCount.w);
        for (uint LIdx = 0u; LIdx < LightCnt; ++LIdx)
        {
            GpuAreaQuadLight Q = WorldUniforms.AreaLights[LIdx];
            float LU = PcgFloat01(Rng) * 2.0 - 1.0;
            float LV = PcgFloat01(Rng) * 2.0 - 1.0;
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
                float  Geom  = (NdotL * LCos * Area) / max(1.0e-4, D2);
                Radiance += Throughput * EvaluateSurfaceBrdf(Mat, N, V, LDir) * Q.RadiancePacked.xyz * Geom;
            }
        }

        // MIS Next-Bounce: Heitz GGX VNDF vs Cosine Hemisphere
        float3 LocalWo = float3(dot(V, T), dot(V, BVec), max(1.0e-4, dot(V, N)));
        float  Rough   = clamp(Mat.SpecParams.x, 0.025, 1.0);
        float  Alpha   = max(0.0015, Rough * Rough);
        float  DiffP   = clamp((1.0 - saturate(Mat.SpecParams.y)) * 0.65, 0.10, 0.85);
        float  SpecP   = 1.0 - DiffP;
        float  U1      = PcgFloat01(Rng);
        float  U2      = PcgFloat01(Rng);

        float3 LocalWi;
        if (PcgFloat01(Rng) < DiffP)
        {
            float R   = sqrt(U1);
            float Phi = U2 * TauConst;
            LocalWi = float3(R * cos(Phi), R * sin(Phi), sqrt(max(0.0, 1.0 - U1)));
        }
        else
        {
            float3 H = SampleGgxVndfLocal(LocalWo, Alpha, Alpha, U1, U2);
            LocalWi = reflect(-LocalWo, H);
        }
        if (LocalWi.z <= 1.0e-4) break;

        float3 WorldWi = normalize(T * LocalWi.x + BVec * LocalWi.y + N * LocalWi.z);
        float3 HalfV   = normalize(LocalWo + LocalWi);
        float  NdotH   = max(1.0e-4, HalfV.z);
        float  A2      = Alpha * Alpha;
        float  Den     = NdotH * NdotH * (A2 - 1.0) + 1.0;
        float  D_GGX   = A2 / max(1.0e-6, PiConst * Den * Den);
        float  Tan2V   = (LocalWo.x * LocalWo.x + LocalWo.y * LocalWo.y) * A2 / max(1.0e-6, LocalWo.z * LocalWo.z);
        float  G1V     = 2.0 / (1.0 + sqrt(1.0 + Tan2V));
        float  PdfSpec = (D_GGX * G1V) / (4.0 * LocalWo.z);
        float  PdfDiff = LocalWi.z * InvPiConst;
        float  MixPdf  = max(1.0e-4, DiffP * PdfDiff + SpecP * PdfSpec);

        Throughput = min(float3(12.0), Throughput * EvaluateSurfaceBrdf(Mat, N, V, WorldWi) / MixPdf);
        RayO       = Hit.Position + Hit.GeomNormal * 0.006;
        RayD       = WorldWi;
    }

    return min(Radiance, float3(18.0));
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

float3 TraceThroughGlassSphere(float3 EntryPos, float3 EntryNrm, float3 IncidentDir, float3 Center, float Radius, float IorChannel)
{
    float3 InsideDir = refract(IncidentDir, EntryNrm, 1.0 / IorChannel);
    if (dot(InsideDir, InsideDir) < 1.0e-5)
    {
        return ShadeSecondaryBounce(EntryPos + EntryNrm * 2.5e-3, reflect(IncidentDir, EntryNrm));
    }
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
        return ShadeSecondaryBounce(ExitPos + OutNrm * 3.0e-3, normalize(ExitDir));
    }
    return ShadeSecondaryBounce(EntryPos + InsideDir * (Radius * 2.1), InsideDir);
}

float3 ApplyAcesFilmicGpu(float3 Hdr)
{
    float3 V = max(float3(0.0), Hdr * 0.85);
    return saturate((V * (2.51 * V + 0.03)) / (V * (2.43 * V + 0.59) + 0.14));
}

uint PackLinearToSrgb8Rgba(float3 LinearColor)
{
    float3 C    = saturate(LinearColor);
    float3 Srgb = select(C <= float3(0.0031308), C * 12.92, 1.055 * pow(max(C, float3(1.0e-6)), float3(1.0 / 2.4)) - 0.055);
    uint3  Byte = uint3(clamp(round(Srgb * 255.0), float3(0.0), float3(255.0)));
    return Byte.x | (Byte.y << 8u) | (Byte.z << 16u) | (255u << 24u);
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
    uint   SampleIdx  = WorldUniforms.EngineShadowReflSample.w;
    uint   DebugMode  = uint(WorldUniforms.LatticeMaxDebugMode.w + 0.5);

    float2 JitterPx = float2(0.5, 0.5);
    if (EngineMode == 4u && DebugMode == 0u)
    {
        uint JSeed = (DispatchThreadID.x * 1973u + DispatchThreadID.y * 9277u + SampleIdx * 7919u + 17u) | 1u;
        JitterPx   = float2(PcgFloat01(JSeed), PcgFloat01(JSeed));
    }

    float2 PixelUv = (float2(DispatchThreadID.xy) + JitterPx) / float2(float(Width), float(Height));
    float  NdcX    = PixelUv.x * 2.0 - 1.0;
    float  NdcY    = 1.0 - PixelUv.y * 2.0;

    float3 CamOrigin = WorldUniforms.CameraOriginTanX.xyz;
    float3 RayDir    = normalize(
        WorldUniforms.CameraForwardHeight.xyz +
        WorldUniforms.CameraRightTanY.xyz * (NdcX * WorldUniforms.CameraOriginTanX.w) +
        WorldUniforms.CameraUpWidth.xyz   * (NdcY * WorldUniforms.CameraRightTanY.w));

    if (EngineMode == 4u && DebugMode == 0u)
    {
        float3 SampleHdr = TraceDeterministicPathSample(CamOrigin, RayDir, DispatchThreadID.xy, SampleIdx);
        float4 PrevAcc   = (SampleIdx == 0u) ? float4(0.0, 0.0, 0.0, 0.0) : AccumulationHdrBuffer[PixelIdx];
        float  NewCount  = PrevAcc.w + 1.0;
        float3 MeanHdr   = lerp(PrevAcc.xyz, SampleHdr, 1.0 / NewCount);
        AccumulationHdrBuffer[PixelIdx] = float4(MeanHdr, NewCount);
        OutDisplayRgba8[PixelIdx] = PackLinearToSrgb8Rgba(ApplyAcesFilmicGpu(MeanHdr));
        return;
    }

    RayHitReading PrimaryHit = TraceSceneClosest(CamOrigin, RayDir, 0.08, 120.0);

    uint  GlassCnt     = min(4u, uint(WorldUniforms.LatticeMinGlassCount.w + 0.5));
    int   BestGlassIdx = -1;
    float BestGlassT   = PrimaryHit.Hit ? PrimaryHit.Distance : 120.0;
    if (EngineMode != 0u && DebugMode == 0u)
    {
        for (uint G = 0u; G < GlassCnt; ++G)
        {
            float TEn = 0.0, TEx = 0.0;
            if (IntersectGlassSphere(
                    CamOrigin,
                    RayDir,
                    WorldUniforms.GlassSpheres[G].CenterRadiusPacked.xyz,
                    WorldUniforms.GlassSpheres[G].CenterRadiusPacked.w,
                    TEn,
                    TEx))
            {
                float HitT = (TEn > 0.08) ? TEn : TEx;
                if (HitT > 0.08 && HitT < BestGlassT)
                {
                    BestGlassT   = HitT;
                    BestGlassIdx = int(G);
                }
            }
        }
    }

    float3 FinalHdr = float3(0.0);
    if (BestGlassIdx >= 0)
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
        float  Fresnel = F0 + (1.0 - F0) * pow(1.0 - CosI, 5.0);

        float  RefrR   = TraceThroughGlassSphere(HitPos, HitNrm, RayDir, Center, Radius, CrownIor - Dispersion).r;
        float  RefrG   = TraceThroughGlassSphere(HitPos, HitNrm, RayDir, Center, Radius, CrownIor).g;
        float  RefrB   = TraceThroughGlassSphere(HitPos, HitNrm, RayDir, Center, Radius, CrownIor + Dispersion * 1.15).b;
        float3 RefrRgb = float3(RefrR, RefrG, RefrB) * float3(0.95, 0.98, 1.00);
        float3 ReflRgb = ShadeSecondaryBounce(HitPos + HitNrm * 2.5e-3, reflect(RayDir, HitNrm));

        float3 SunDir = WorldUniforms.SunDirConeTan.xyz;
        float3 HSun   = normalize(ViewDir + SunDir);
        float  SpecGl = pow(max(0.0, dot(HitNrm, HSun)), 380.0) * 6.5;
        if (TraceSceneAnyHit(HitPos + HitNrm * 3.0e-3, SunDir, 2.0e-3, 60.0))
        {
            SpecGl = 0.0;
        }
        FinalHdr = lerp(RefrRgb, ReflRgb, Fresnel) + WorldUniforms.SolarIrradianceTier.xyz * SpecGl;
    }
    else if (PrimaryHit.Hit)
    {
        FinalHdr = ShadePrimarySurface(PrimaryHit, -RayDir, DispatchThreadID.xy);
    }
    else
    {
        FinalHdr = (DebugMode != 0u) ? float3(0.0) : EvaluateBackgroundSkyCloudsGpu(RayDir, float2(DispatchThreadID.xy));
    }

    float3 DisplayLinear = (DebugMode != 0u) ? FinalHdr : ApplyAcesFilmicGpu(FinalHdr);
    OutDisplayRgba8[PixelIdx] = PackLinearToSrgb8Rgba(DisplayLinear);
}
