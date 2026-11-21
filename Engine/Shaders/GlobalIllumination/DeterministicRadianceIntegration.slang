/*============================================================================================================================================
                                                DETERMINISTICRADIANCEINTEGRATION.SLANG
============================================================================================================================================*/
// 🧩 Unified deterministic Sobol' (0,2) path tracer for GameRealtime and SceneFinalRender:
//    zero RNG, 64 B 4-wide TLAS4+BLAS4 tree walk, full-lobe & Fresnel splitting, MIS area softboxes, and adaptive .ies light tree

#include "GlobalIllumination/QuadratureSequence.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

static const float PiConstant                = 3.14159265358979f;
static const uint  InvalidMediumSlot         = 0xFFu;
static const uint  TransportStackCapacity    = 8u;
static const uint  SceneStackCapacity        = 28u;
static const uint  ReturnToWorldMarker       = 0xFFFFFFFFu;
static const uint  EmptyBranchLink           = 0xFFFFFFFFu;
static const uint  InstanceIndexShift        = 20u;
static const uint  TriangleIndexMask         = 0xFFFFFu;
static const uint  ThinSheetBit              = 0x80000000u;
static const uint  MaterialSlotMask          = 0x7FFFFFFFu;
static const uint  InstancePlacementStride   = 2u; // [uint4] per instance: (Cosine, Sine, Tx, Tz) + (root, paintSwap, rimSwap, 0)
static const uint  UnusedMaterialSlot        = 0xFFFFu;
static const float DeltaRoughnessAlpha       = 0.0025f;
static const float RayOffsetScale            = 2.0e-4f;
static const float UnboundedDistance         = 1.0e30f;

static const uint  SurfaceCategoryLayered    = 0u;
static const uint  SurfaceCategoryMetallic  = 1u;
static const uint  SurfaceCategorySolidGlass = 2u;
static const uint  SurfaceCategoryThinSheet  = 3u;

static const uint  HitCategoryAbsent         = 0u;
static const uint  HitCategoryTriangle       = 1u;
static const uint  HitCategorySphere         = 2u;
static const uint  HitCategoryFloor          = 3u;
static const uint  HitCategoryLight          = 4u;

static const uint  PairSlotSubpixel          = 0u;
static const uint  PairSlotLens              = 1u;
static const uint  PairsPerDepth             = 4u;
static const uint  PairSlotLight             = 0u;
static const uint  PairSlotDiffuse           = 1u;
static const uint  PairSlotSpecular          = 2u;
static const uint  PairSlotCoat              = 3u;
static const uint  PairPracticalStart        = 66u;
static const uint  LampClusterLeafBit        = 0x80000000u;
static const uint  ProfileVerticalSamples    = 128u;
static const uint  ProfileHorizontalSamples  = 32u;
static const float SelectionCeiling          = 0.99999994f;
static const uint  LampCutCapacity           = 8u;
static const float LampSplitRatio            = 0.05f;
static const float GoldenFraction            = 0.618034f;

static const uint  LobeSelectorMetal         = 0u;
static const uint  LobeSelectorSpecular          = 1u;
static const uint  LobeSelectorCoat              = 2u;

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct TracingUniformBlock
{
    float4 CameraOrigin;    // xyz [m] origin, w [m] lens radius
    float4 CameraForward;   // xyz unit forward, w [m] focus distance
    float4 CameraRight;     // xyz right * tan(fov/2) * aspect
    float4 CameraUp;        // xyz up * tan(fov/2)
    float4 EnvironmentTint; // rgb tint, w intensity
    uint4  Extent;          // x = width, y = height, z = first sample index, w = samples in this dispatch
    uint4  DepthLimits;     // x = non-delta bounce limit, y = total depth limit, z = area light count, w = sphere count
    uint4  SceneLayout;     // x = floor material slot, y = environment category, z = entry count, w = vehicle active (0/1)
    float4 Tuning;          // x = throughput cutoff, y = floor radius [m], z = softboxes visible to camera (0/1), w = unused
    float4 PreviousOrigin;  // xyz [m] previous camera origin
    float4 PreviousForward;
    float4 PreviousRight;
    float4 PreviousUp;
    uint4  Realtime;        // x = pattern (0 shared, 1 pixel, 2 bluenoise), y = visibility raster (0/1), z = history read [px], w = history write [px]
    float4 HistoryTuning;   // x = history capacity [samples], y = history active (0/1), zw = unused
    uint4  Geometry;        // x = instance tree root, y = first instance placement [uint4], z = instance count, w = trace scale [px]
    float4 Shading;         // x = light cutoff, y = analytic area, z = all lamps (1) or tree (0), w = tree cut limit
    uint4  Practical;       // x = lamp count, y = cluster start [float4], z = lamp start [float4], w = profile start [float4]
    float4 SphereBounds;    // xyz [m] showcase sphere group bounding centre, w [m] radius
};

struct WideEntryVisit
{
    float4 Distances;
    uint4  Counts;
    uint4  Links;
};

struct SortedWideBranches
{
    float4 Distances;
    uint4  Entries;
};

struct PathSurfaceMaterial
{
    float4 AlbedoRoughness; // rgb albedo / tint, w perceptual roughness
    float4 EtaCategory;     // rgb metallic eta, w category
    float4 KappaCoat;       // rgb metallic kappa, w coat weight
    float4 ExitanceCoat;    // rgb exitance radiance, w coat roughness
    float4 AbsorptionIor;   // rgb sigma_a [1/m], w index of refraction
    float4 SpecularLevel;   // x dielectric F0
};

struct StudioAreaLight
{
    float4 Centre;   // xyz [m], w area [m^2]
    float4 EdgeU;    // xyz [m], w normal.x
    float4 EdgeV;    // xyz [m], w normal.y
    float4 Radiance; // rgb radiance, w normal.z
};

struct SurfaceHitResult
{
    float  Distance;
    uint   Category;
    uint   PrimitiveIndex;
    float2 Barycentric;
};

struct ResolvedSurfaceSample
{
    float3 Position;
    float3 GeometricNormal;
    float3 ShadingNormal;
    uint   MaterialSlot;
    bool   FrontFacing;
};

struct TransportPathRay
{
    float3 Origin;
    uint   PackedCounters; // bits 0..7 = Depth, bits 8..15 = NonDeltaDepth, bits 16..23 = Medium (0xFF = air)
    float3 Direction;
    float  LobePdf;
    float3 Throughput;
    float  TravelSinceLobe;
};

struct OrthonormalBasis3
{
    float3 Tangent;
    float3 Bitangent;
    float3 Normal;
};

struct LobeEvaluationResult
{
    float3 Diffuse;
    float  DiffusePdf;
    float3 Specular;
    float  SpecularPdf;
    float3 Coat;
    float  CoatPdf;
};

struct PracticalLampPick
{
    uint  LampIndex;
    float Pdf;
    float Relative;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       RESOURCES
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 0)]]  ConstantBuffer<TracingUniformBlock>     Tracing;
[[vk::binding(1, 0)]]  StructuredBuffer<uint4>                 BoundsEntries;
[[vk::binding(2, 0)]]  StructuredBuffer<float4>                TrianglePositions;
[[vk::binding(3, 0)]]  StructuredBuffer<float4>                TriangleNormals;
[[vk::binding(4, 0)]]  StructuredBuffer<PathSurfaceMaterial>   Materials;
[[vk::binding(5, 0)]]  StructuredBuffer<float4>                LightRegister;
[[vk::binding(6, 0)]]  StructuredBuffer<float4>                Spheres;
[[vk::binding(7, 0)]]  RWStructuredBuffer<float4>              Accumulation;
[[vk::binding(8, 0)]]  Texture2D<uint>                         VisibilityIds;
[[vk::binding(9, 0)]]  Texture2D<uint>                         BlueNoiseRanks;
[[vk::binding(10, 0)]] RWStructuredBuffer<float4>              SurfaceFeatures;

static TransportPathRay gTransportStack[TransportStackCapacity];
static uint             gTransportStackSize;
static uint             gPixelScramble;
static uint2            gBlueNoiseOrigin;
static bool             gBlueNoiseActive;
static float2           gImagePosition;
static uint             gLampCut[LampCutCapacity];

uint ReadLampCut(uint Slot)              { return gLampCut[Slot]; }
void WriteLampCut(uint Slot, uint SlotReading) { gLampCut[Slot] = SlotReading; }

//------------------------------------------------------------------------------------------------------------------------
//                                                    COUNTERS & QUADRATURE
//------------------------------------------------------------------------------------------------------------------------

uint PackRayCounters(uint Depth, uint NonDeltaDepth, uint Medium)
{
    return (Depth & 0xFFu) | ((NonDeltaDepth & 0xFFu) << 8u) | ((Medium & 0xFFu) << 16u);
}

uint RayDepth(TransportPathRay Ray)         { return Ray.PackedCounters & 0xFFu; }
uint RayNonDeltaDepth(TransportPathRay Ray) { return (Ray.PackedCounters >> 8u) & 0xFFu; }
uint RayMedium(TransportPathRay Ray)        { return (Ray.PackedCounters >> 16u) & 0xFFu; }

uint2 ResolveBlueNoiseShift(uint PairIndex)
{
    uint2 Offset = uint2((PairIndex * 3242174890u) >> 26u, (PairIndex * 2447445413u) >> 26u);
    int2  Texel  = int2((gBlueNoiseOrigin + Offset) & uint2(63u, 63u));
    uint  Ranks  = BlueNoiseRanks.Load(int3(Texel, 0));
    return uint2((Ranks & 0xfffu) << 20u, ((Ranks >> 16u) & 0xfffu) << 20u);
}

float2 QuadraturePair(uint SampleIndex, uint PairIndex)
{
    bool  Keyed = (PairIndex != PairSlotSubpixel);
    uint2 Shift = uint2(0u, 0u);
    if (gBlueNoiseActive && Keyed)
    {
        Shift = ResolveBlueNoiseShift(PairIndex);
    }
    return ScrambledQuadraturePair(SampleIndex, PairIndex, select(Keyed, gPixelScramble, 0u), Shift);
}

float2 DepthPair(uint SampleIndex, uint Depth, uint Slot)
{
    return QuadraturePair(SampleIndex, 2u + min(Depth, 15u) * PairsPerDepth + Slot);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    GEOMETRY QUERIES
//------------------------------------------------------------------------------------------------------------------------

[ForceInline]
float3 SafeReciprocal(float3 Direction)
{
    float3 Magnitude = max(abs(Direction), float3(1.0e-12f, 1.0e-12f, 1.0e-12f));
    float3 SignVec   = select(Direction < float3(0.0f, 0.0f, 0.0f), float3(-1.0f, -1.0f, -1.0f), float3(1.0f, 1.0f, 1.0f));
    return SignVec / Magnitude;
}

float4 ExtractQuantizedBytes(uint Word)
{
    return float4(uint4(Word, Word >> 8u, Word >> 16u, Word >> 24u) & uint4(255u, 255u, 255u, 255u));
}

WideEntryVisit IntersectWideEntry(uint EntrySlot, float3 Origin, float3 Reciprocal, float MaximumDistance)
{
    uint   StartWord   = EntrySlot * 4u;
    uint4  Header      = BoundsEntries[StartWord];
    uint4  BoundsXY    = BoundsEntries[StartWord + 1u];
    uint4  BoundsZ     = BoundsEntries[StartWord + 2u];
    uint4  Links       = BoundsEntries[StartWord + 3u];
    float3 EntryOrigin = asfloat(Header.xyz);
    float3 Scale       = asfloat(uint3(Header.w & 255u, (Header.w >> 8u) & 255u, (Header.w >> 16u) & 255u) << uint3(23u, 23u, 23u));
    float3 Offset      = (EntryOrigin - Origin) * Reciprocal;
    float3 ScaledRecip = Scale * Reciprocal;
    bool   NegX = Reciprocal.x < 0.0f;
    bool   NegY = Reciprocal.y < 0.0f;
    bool   NegZ = Reciprocal.z < 0.0f;

    float4 MinX = ExtractQuantizedBytes(select(NegX, BoundsXY.y, BoundsXY.x)) * ScaledRecip.x + Offset.x;
    float4 MaxX = ExtractQuantizedBytes(select(NegX, BoundsXY.x, BoundsXY.y)) * ScaledRecip.x + Offset.x;
    float4 MinY = ExtractQuantizedBytes(select(NegY, BoundsXY.w, BoundsXY.z)) * ScaledRecip.y + Offset.y;
    float4 MaxY = ExtractQuantizedBytes(select(NegY, BoundsXY.z, BoundsXY.w)) * ScaledRecip.y + Offset.y;
    float4 MinZ = ExtractQuantizedBytes(select(NegZ, BoundsZ.y,  BoundsZ.x))  * ScaledRecip.z + Offset.z;
    float4 MaxZ = ExtractQuantizedBytes(select(NegZ, BoundsZ.x,  BoundsZ.y))  * ScaledRecip.z + Offset.z;

    float4 Entered   = max(max(MinX, MinY), max(MinZ, float4(0.0f, 0.0f, 0.0f, 0.0f)));
    float4 Exited    = min(min(MaxX, MaxY), min(MaxZ, float4(MaximumDistance, MaximumDistance, MaximumDistance, MaximumDistance)));
    bool4  HitMask   = (Links != uint4(EmptyBranchLink, EmptyBranchLink, EmptyBranchLink, EmptyBranchLink)) & (Entered <= Exited * 1.000001f);
    float4 Distances = select(HitMask, Entered, float4(UnboundedDistance, UnboundedDistance, UnboundedDistance, UnboundedDistance));
    uint4 Counts = uint4(BoundsZ.z & 0xFFFFu, BoundsZ.z >> 16u, BoundsZ.w & 0xFFFFu, BoundsZ.w >> 16u);

    WideEntryVisit Visit;
    Visit.Distances = Distances;
    Visit.Counts    = Counts;
    Visit.Links     = Links;
    return Visit;
}

float4 FetchInstanceYaw(uint Instance)
{
    return asfloat(BoundsEntries[Tracing.Geometry.y + Instance * InstancePlacementStride]);
}

float3 WorldToInstancePoint(float4 Yaw, float3 Point)
{
    float Dx = Point.x - Yaw.z;
    float Dz = Point.z - Yaw.w;
    return float3(Yaw.x * Dx - Yaw.y * Dz, Point.y, Yaw.y * Dx + Yaw.x * Dz);
}

float3 WorldToInstanceVector(float4 Yaw, float3 Vec)
{
    return float3(Yaw.x * Vec.x - Yaw.y * Vec.z, Vec.y, Yaw.y * Vec.x + Yaw.x * Vec.z);
}

float3 InstanceToWorldVector(float4 Yaw, float3 Vec)
{
    return float3(Yaw.x * Vec.x + Yaw.y * Vec.z, Vec.y, -Yaw.y * Vec.x + Yaw.x * Vec.z);
}

uint ResolveInstanceMaterialSlot(uint Instance, uint TriangleSlot)
{
    uint4 Meta      = BoundsEntries[Tracing.Geometry.y + Instance * InstancePlacementStride + 1u];
    uint  PaintSwap = Meta.y >> 16u;
    uint  Slot      = select((PaintSwap != UnusedMaterialSlot) & (TriangleSlot == (Meta.y & 0xFFFFu)), PaintSwap, TriangleSlot);
    uint  RimSwap   = Meta.z >> 16u;
    if (RimSwap != UnusedMaterialSlot && TriangleSlot == (Meta.z & 0xFFFFu))
    {
        Slot = RimSwap;
    }
    return Slot;
}

float3 IntersectTriangle(float3 Origin, float3 Direction, uint TriangleIndex, float MaxDistance)
{
    float3 Corner      = TrianglePositions[TriangleIndex * 3u].xyz;
    float3 EdgeA       = TrianglePositions[TriangleIndex * 3u + 1u].xyz;
    float3 EdgeB       = TrianglePositions[TriangleIndex * 3u + 2u].xyz;
    float3 PVector     = cross(Direction, EdgeB);
    float  Determinant = dot(EdgeA, PVector);
    float  Sign        = select(Determinant > 0.0f, 1.0f, -1.0f);
    float  AbsDet      = max(Determinant * Sign, 1.0e-20f);
    float3 TVec        = Origin - Corner;
    float  UDet        = dot(TVec, PVector) * Sign;
    float3 QVec        = cross(TVec, EdgeA);
    float  VDet        = dot(Direction, QVec) * Sign;
    float  TDet        = dot(EdgeB, QVec) * Sign;
    bool   Valid       = (abs(Determinant) >= 1.0e-12f) & (UDet >= 0.0f) & (VDet >= 0.0f) & (UDet + VDet <= AbsDet) &
                         (TDet > RayOffsetScale * AbsDet) & (TDet < MaxDistance * AbsDet);
    float  InvAbsDet   = 1.0f / AbsDet;
    return select(bool3(Valid, Valid, Valid), float3(TDet * InvAbsDet, UDet * InvAbsDet, VDet * InvAbsDet), float3(-1.0f, -1.0f, -1.0f));
}

bool IntersectTriangleShadow(float3 Origin, float3 Direction, uint TriangleIndex, float MaxDistance)
{
    float3 Corner      = TrianglePositions[TriangleIndex * 3u].xyz;
    float3 EdgeA       = TrianglePositions[TriangleIndex * 3u + 1u].xyz;
    float3 EdgeB       = TrianglePositions[TriangleIndex * 3u + 2u].xyz;
    float3 PVector     = cross(Direction, EdgeB);
    float  Determinant = dot(EdgeA, PVector);
    float  Sign        = select(Determinant > 0.0f, 1.0f, -1.0f);
    float  AbsDet      = max(Determinant * Sign, 1.0e-20f);
    float3 TVec        = Origin - Corner;
    float  UDet        = dot(TVec, PVector) * Sign;
    float3 QVec        = cross(TVec, EdgeA);
    float  VDet        = dot(Direction, QVec) * Sign;
    float  TDet        = dot(EdgeB, QVec) * Sign;
    return (abs(Determinant) >= 1.0e-12f) & (UDet >= 0.0f) & (VDet >= 0.0f) & (UDet + VDet <= AbsDet) &
           (TDet > RayOffsetScale * AbsDet) & (TDet < MaxDistance * AbsDet);
}

bool IntersectsBoundingSphere(float3 Origin, float3 Direction, float4 Sphere, float MaxDistance)
{
    float3 Offset       = Origin - Sphere.xyz;
    float  HalfB        = dot(Offset, Direction);
    float  C            = dot(Offset, Offset) - Sphere.w * Sphere.w;
    float  Discriminant = HalfB * HalfB - C;
    float  Root         = sqrt(max(Discriminant, 0.0f));
    return (C <= 0.0f) | ((HalfB < 0.0f) & (Discriminant >= 0.0f) & ((-HalfB - Root) < MaxDistance));
}

float IntersectSphere(float3 Origin, float3 Direction, float4 Sphere)
{
    float3 Offset       = Origin - Sphere.xyz;
    float  HalfB        = dot(Offset, Direction);
    float  C            = dot(Offset, Offset) - Sphere.w * Sphere.w;
    float  Discriminant = HalfB * HalfB - C;
    float  Root         = sqrt(max(Discriminant, 0.0f));
    float  Near         = -HalfB - Root;
    float  Far          = -HalfB + Root;
    float  Chosen       = select(Near > RayOffsetScale, Near, select(Far > RayOffsetScale, Far, -1.0f));
    bool   Valid        = !((C > 0.0f) & (HalfB >= 0.0f)) & (Discriminant >= 0.0f);
    return select(Valid, Chosen, -1.0f);
}

StudioAreaLight FetchAreaLight(uint LightIndex)
{
    uint StartSlot = LightIndex * 4u;
    StudioAreaLight Light;
    Light.Centre   = LightRegister[StartSlot];
    Light.EdgeU    = LightRegister[StartSlot + 1u];
    Light.EdgeV    = LightRegister[StartSlot + 2u];
    Light.Radiance = LightRegister[StartSlot + 3u];
    return Light;
}

float3 AreaLightNormal(StudioAreaLight Light)
{
    return float3(Light.EdgeU.w, Light.EdgeV.w, Light.Radiance.w);
}

float IntersectAreaLight(float3 Origin, float3 Direction, StudioAreaLight Light, float MaxDistance)
{
    float3 Normal   = AreaLightNormal(Light);
    float  Approach = dot(Direction, Normal);
    if (abs(Approach) < 1.0e-8f)
    {
        return -1.0f;
    }
    float Distance = dot(Light.Centre.xyz - Origin, Normal) / Approach;
    if (Distance <= RayOffsetScale || Distance >= MaxDistance)
    {
        return -1.0f;
    }
    float3 Local = Origin + Direction * Distance - Light.Centre.xyz;
    float  U     = dot(Local, Light.EdgeU.xyz) / dot(Light.EdgeU.xyz, Light.EdgeU.xyz);
    float  V     = dot(Local, Light.EdgeV.xyz) / dot(Light.EdgeV.xyz, Light.EdgeV.xyz);
    return select((abs(U) <= 0.5f) & (abs(V) <= 0.5f), Distance, -1.0f);
}

SurfaceHitResult ConstructSurfaceHit(float Distance, uint Category, uint PrimitiveIndex, float2 Barycentric)
{
    SurfaceHitResult Hit;
    Hit.Distance       = Distance;
    Hit.Category       = Category;
    Hit.PrimitiveIndex = PrimitiveIndex;
    Hit.Barycentric    = Barycentric;
    return Hit;
}

SurfaceHitResult TraceAnalyticHit(float3 Origin, float3 Direction, bool LightsVisible)
{
    SurfaceHitResult Hit = ConstructSurfaceHit(UnboundedDistance, HitCategoryAbsent, 0u, float2(0.0f, 0.0f));

    if (abs(Direction.y) > 1.0e-8f)
    {
        float FloorDistance = -Origin.y / Direction.y;
        if (FloorDistance > RayOffsetScale)
        {
            float3 FloorPoint = Origin + Direction * FloorDistance;
            if (dot(FloorPoint.xz, FloorPoint.xz) < Tracing.Tuning.y * Tracing.Tuning.y)
            {
                Hit = ConstructSurfaceHit(FloorDistance, HitCategoryFloor, 0u, float2(0.0f, 0.0f));
            }
        }
    }

    if (Tracing.DepthLimits.w > 0u && IntersectsBoundingSphere(Origin, Direction, Tracing.SphereBounds, Hit.Distance))
    {
        for (uint SphereIndex = 0u; SphereIndex < Tracing.DepthLimits.w; ++SphereIndex)
        {
            float Distance = IntersectSphere(Origin, Direction, Spheres[SphereIndex * 2u]);
            if (Distance > 0.0f && Distance < Hit.Distance)
            {
                Hit = ConstructSurfaceHit(Distance, HitCategorySphere, SphereIndex, float2(0.0f, 0.0f));
            }
        }
    }

    if (LightsVisible)
    {
        for (uint LightIndex = 0u; LightIndex < Tracing.DepthLimits.z; ++LightIndex)
        {
            float Distance = IntersectAreaLight(Origin, Direction, FetchAreaLight(LightIndex), Hit.Distance);
            if (Distance > 0.0f)
            {
                Hit = ConstructSurfaceHit(Distance, HitCategoryLight, LightIndex, float2(0.0f, 0.0f));
            }
        }
    }
    return Hit;
}

SortedWideBranches SortWideBranches(WideEntryVisit Visit)
{
    float4 D = Visit.Distances;
    uint4  E = (Visit.Links << uint4(4u, 4u, 4u, 4u)) | Visit.Counts;

    bool4 m1 = bool4(D.y < D.x, D.y < D.x, D.w < D.z, D.w < D.z);
    D = select(m1, D.yxwz, D);
    E = select(m1, E.yxwz, E);

    bool4 m2 = bool4(D.z < D.x, D.w < D.y, D.z < D.x, D.w < D.y);
    D = select(m2, D.zwxy, D);
    E = select(m2, E.zwxy, E);

    bool4 m3 = bool4(false, D.z < D.y, D.z < D.y, false);
    D = select(m3, D.xzyw, D);
    E = select(m3, E.xzyw, E);

    SortedWideBranches Sorted;
    Sorted.Distances = D;
    Sorted.Entries   = E;
    return Sorted;
}

SurfaceHitResult TraceClosestHit(float3 Origin, float3 Direction, bool LightsVisible)
{
    SurfaceHitResult Hit = TraceAnalyticHit(Origin, Direction, LightsVisible);
    if (Tracing.SceneLayout.w == 0u || Tracing.Geometry.z == 0u)
    {
        return Hit;
    }

    float3 WorldReciprocal = SafeReciprocal(Direction);
    bool   SingleIdentity  = (Tracing.Geometry.z == 1u);
    float3 RayOrigin       = Origin;
    float3 RayDirection    = Direction;
    float3 Reciprocal      = WorldReciprocal;
    uint   Instance        = 0u;
    bool   InstanceLevel     = SingleIdentity;
    uint   Stack[SceneStackCapacity];
    Stack[0]               = 0u;
    uint   StackSize       = 0u;
    uint   Entry           = select(SingleIdentity, 0u, Tracing.Geometry.x) << 4u;
    bool   Active          = true;

    [loop] while (Active || StackSize > 0u)
    {
        if (!Active)
        {
            if (StackSize == 0u)
            {
                break;
            }
            StackSize -= 1u;
            Entry = Stack[StackSize];
        }
        Active = false;

        if (Entry == ReturnToWorldMarker)
        {
            InstanceLevel  = false;
            RayOrigin    = Origin;
            RayDirection = Direction;
            Reciprocal   = WorldReciprocal;
            continue;
        }

        uint Count = Entry & 15u;
        uint Link  = Entry >> 4u;
        if (Count > 0u && !InstanceLevel)
        {
            Instance     = Link;
            InstanceLevel  = true;
            float4 Yaw   = FetchInstanceYaw(Instance);
            RayOrigin    = WorldToInstancePoint(Yaw, Origin);
            RayDirection = WorldToInstanceVector(Yaw, Direction);
            Reciprocal   = SafeReciprocal(RayDirection);
            if (StackSize < SceneStackCapacity)
            {
                Stack[StackSize] = ReturnToWorldMarker;
                StackSize += 1u;
            }
            Entry  = BoundsEntries[Tracing.Geometry.y + Instance * InstancePlacementStride + 1u].x << 4u;
            Active = true;
            continue;
        }
        if (Count > 0u)
        {
            [loop] for (uint Offset = 0u; Offset < Count; ++Offset)
            {
                float3 Candidate = IntersectTriangle(RayOrigin, RayDirection, Link + Offset, Hit.Distance);
                if (Candidate.x > 0.0f)
                {
                    Hit = ConstructSurfaceHit(Candidate.x, HitCategoryTriangle, (Instance << InstanceIndexShift) | (Link + Offset), Candidate.yz);
                }
            }
            continue;
        }

        WideEntryVisit Visit = IntersectWideEntry(Link, RayOrigin, Reciprocal, Hit.Distance);
        if (min(min(Visit.Distances.x, Visit.Distances.y), Visit.Distances.z) >= UnboundedDistance && Visit.Distances.w >= UnboundedDistance)
        {
            continue;
        }
        SortedWideBranches Sorted = SortWideBranches(Visit);
        bool Room = (StackSize + 3u <= SceneStackCapacity);
        if (Room && Sorted.Distances.w < Hit.Distance) { Stack[StackSize] = Sorted.Entries.w; StackSize += 1u; }
        if (Room && Sorted.Distances.z < Hit.Distance) { Stack[StackSize] = Sorted.Entries.z; StackSize += 1u; }
        if (Room && Sorted.Distances.y < Hit.Distance) { Stack[StackSize] = Sorted.Entries.y; StackSize += 1u; }
        if (Sorted.Distances.x < Hit.Distance)
        {
            Entry  = Sorted.Entries.x;
            Active = true;
        }
    }
    return Hit;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MICROFACET OPTICS
//------------------------------------------------------------------------------------------------------------------------

float FresnelSchlick(float F0, float Cosine)
{
    float F90        = clamp(50.0f * F0, 0.0f, 1.0f);
    float Complement = 1.0f - clamp(Cosine, 0.0f, 1.0f);
    float Squared    = Complement * Complement;
    return F0 + (F90 - F0) * Squared * Squared * Complement;
}

float FresnelDielectric(float CosineIncidence, float Eta)
{
    float SineTransmitted2 = (1.0f - CosineIncidence * CosineIncidence) / (Eta * Eta);
    if (SineTransmitted2 >= 1.0f)
    {
        return 1.0f;
    }
    float CosineTransmitted = sqrt(1.0f - SineTransmitted2);
    float Perpendicular     = (CosineIncidence - Eta * CosineTransmitted) / (CosineIncidence + Eta * CosineTransmitted);
    float Parallel          = (Eta * CosineIncidence - CosineTransmitted) / (Eta * CosineIncidence + CosineTransmitted);
    return 0.5f * (Perpendicular * Perpendicular + Parallel * Parallel);
}

float3 FresnelMetal(float CosineIncidence, float3 Eta, float3 Kappa)
{
    float  Cosine2      = CosineIncidence * CosineIncidence;
    float  Sine2        = 1.0f - Cosine2;
    float3 Eta2         = Eta * Eta;
    float3 Kappa2       = Kappa * Kappa;
    float3 Shifted      = Eta2 - Kappa2 - float3(Sine2, Sine2, Sine2);
    float3 SumSquare    = sqrt(Shifted * Shifted + 4.0f * Eta2 * Kappa2);
    float3 RealRoot     = sqrt(max(0.5f * (SumSquare + Shifted), float3(0.0f, 0.0f, 0.0f)));
    float3 TermS1       = SumSquare + float3(Cosine2, Cosine2, Cosine2);
    float3 TermS2       = 2.0f * RealRoot * CosineIncidence;
    float3 ReflectanceS = (TermS1 - TermS2) / (TermS1 + TermS2);
    float3 TermP1       = Cosine2 * SumSquare + float3(Sine2 * Sine2, Sine2 * Sine2, Sine2 * Sine2);
    float3 TermP2       = TermS2 * Sine2;
    float3 ReflectanceP = ReflectanceS * (TermP1 - TermP2) / (TermP1 + TermP2);
    return clamp(0.5f * (ReflectanceP + ReflectanceS), float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f));
}

float3 ThinSheetTransmittance(float CosineIncidence, PathSurfaceMaterial Material)
{
    float Reflectance = FresnelDielectric(abs(CosineIncidence), Material.AbsorptionIor.w);
    float Transmitted = (1.0f - Reflectance) * (1.0f - Reflectance) / max(1.0f - Reflectance * Reflectance, 1.0e-6f);
    return Material.AlbedoRoughness.rgb * Transmitted;
}

float3 TraceShadowTransmittance(float3 Origin, float3 Direction, float Distance)
{
    if (Tracing.DepthLimits.w > 0u && IntersectsBoundingSphere(Origin, Direction, Tracing.SphereBounds, Distance))
    {
        for (uint SphereIndex = 0u; SphereIndex < Tracing.DepthLimits.w; ++SphereIndex)
        {
            float SphereDistance = IntersectSphere(Origin, Direction, Spheres[SphereIndex * 2u]);
            if (SphereDistance > 0.0f && SphereDistance < Distance)
            {
                return float3(0.0f, 0.0f, 0.0f);
            }
        }
    }

    float3 Transmittance = float3(1.0f, 1.0f, 1.0f);
    if (Tracing.SceneLayout.w == 0u || Tracing.Geometry.z == 0u)
    {
        return Transmittance;
    }

    float3 WorldReciprocal = SafeReciprocal(Direction);
    bool   SingleIdentity  = (Tracing.Geometry.z == 1u);
    float3 RayOrigin       = Origin;
    float3 RayDirection    = Direction;
    float3 Reciprocal      = WorldReciprocal;
    uint   Instance        = 0u;
    bool   InstanceLevel     = SingleIdentity;
    uint   Stack[SceneStackCapacity];
    Stack[0]               = 0u;
    uint   StackSize       = 0u;
    uint   Entry           = select(SingleIdentity, 0u, Tracing.Geometry.x) << 4u;
    bool   Active          = true;

    [loop] while (Active || StackSize > 0u)
    {
        if (!Active)
        {
            if (StackSize == 0u)
            {
                break;
            }
            StackSize -= 1u;
            Entry = Stack[StackSize];
        }
        Active = false;

        if (Entry == ReturnToWorldMarker)
        {
            InstanceLevel  = false;
            RayOrigin    = Origin;
            RayDirection = Direction;
            Reciprocal   = WorldReciprocal;
            continue;
        }

        uint Count = Entry & 15u;
        uint Link  = Entry >> 4u;
        if (Count > 0u && !InstanceLevel)
        {
            Instance     = Link;
            InstanceLevel  = true;
            float4 Yaw   = FetchInstanceYaw(Instance);
            RayOrigin    = WorldToInstancePoint(Yaw, Origin);
            RayDirection = WorldToInstanceVector(Yaw, Direction);
            Reciprocal   = SafeReciprocal(RayDirection);
            Stack[StackSize] = ReturnToWorldMarker;
            StackSize += 1u;
            Entry  = BoundsEntries[Tracing.Geometry.y + Instance * InstancePlacementStride + 1u].x << 4u;
            Active = true;
            continue;
        }
        if (Count > 0u)
        {
            [loop] for (uint Offset = 0u; Offset < Count; ++Offset)
            {
                uint TriangleIndex = Link + Offset;
                if (IntersectTriangleShadow(RayOrigin, RayDirection, TriangleIndex, Distance))
                {
                    uint PackedSlot = asuint(TrianglePositions[TriangleIndex * 3u].w);
                    if ((PackedSlot & ThinSheetBit) == 0u)
                    {
                        return float3(0.0f, 0.0f, 0.0f);
                    }
                    PathSurfaceMaterial Material = Materials[ResolveInstanceMaterialSlot(Instance, PackedSlot & MaterialSlotMask)];
                    float3 EdgeNormal = normalize(cross(TrianglePositions[TriangleIndex * 3u + 1u].xyz, TrianglePositions[TriangleIndex * 3u + 2u].xyz));
                    Transmittance *= ThinSheetTransmittance(dot(EdgeNormal, RayDirection), Material);
                }
            }
            continue;
        }

        WideEntryVisit Visit   = IntersectWideEntry(Link, RayOrigin, Reciprocal, Distance);
        uint4          Entries = (Visit.Links << uint4(4u, 4u, 4u, 4u)) | Visit.Counts;
        bool           Room    = (StackSize + 3u <= SceneStackCapacity);
        if (Visit.Distances.w < UnboundedDistance) { Entry = Entries.w; Active = true; }
        if (Visit.Distances.z < UnboundedDistance) { if (Active && Room) { Stack[StackSize] = Entry; StackSize += 1u; } Entry = Entries.z; Active = true; }
        if (Visit.Distances.y < UnboundedDistance) { if (Active && Room) { Stack[StackSize] = Entry; StackSize += 1u; } Entry = Entries.y; Active = true; }
        if (Visit.Distances.x < UnboundedDistance) { if (Active && Room) { Stack[StackSize] = Entry; StackSize += 1u; } Entry = Entries.x; Active = true; }
    }
    return Transmittance;
}

ResolvedSurfaceSample ResolveHitSurface(float3 Origin, float3 Direction, SurfaceHitResult Hit)
{
    float3 Position        = Origin + Direction * Hit.Distance;
    float3 GeometricNormal = float3(0.0f, 1.0f, 0.0f);
    float3 ShadingNormal   = GeometricNormal;
    uint   MaterialSlot    = Tracing.SceneLayout.x;

    if (Hit.Category == HitCategoryTriangle)
    {
        uint   Instance          = Hit.PrimitiveIndex >> InstanceIndexShift;
        uint   CornerOffset      = (Hit.PrimitiveIndex & TriangleIndexMask) * 3u;
        float3 LocalGeometric    = cross(TrianglePositions[CornerOffset + 1u].xyz, TrianglePositions[CornerOffset + 2u].xyz);
        float  FirstCornerWeight = 1.0f - Hit.Barycentric.x - Hit.Barycentric.y;
        float3 LocalShading      = TriangleNormals[CornerOffset].xyz * FirstCornerWeight
                                 + TriangleNormals[CornerOffset + 1u].xyz * Hit.Barycentric.x
                                 + TriangleNormals[CornerOffset + 2u].xyz * Hit.Barycentric.y;
        if (Tracing.Geometry.z == 1u)
        {
            GeometricNormal = normalize(LocalGeometric);
            ShadingNormal   = normalize(LocalShading);
        }
        else
        {
            float4 Yaw      = FetchInstanceYaw(Instance);
            GeometricNormal = normalize(InstanceToWorldVector(Yaw, LocalGeometric));
            ShadingNormal   = normalize(InstanceToWorldVector(Yaw, LocalShading));
        }
        MaterialSlot = ResolveInstanceMaterialSlot(Instance, asuint(TrianglePositions[CornerOffset].w) & MaterialSlotMask);
    }
    else if (Hit.Category == HitCategorySphere)
    {
        float4 Sphere   = Spheres[Hit.PrimitiveIndex * 2u];
        GeometricNormal = (Position - Sphere.xyz) / Sphere.w;
        ShadingNormal   = GeometricNormal;
        MaterialSlot    = uint(Spheres[Hit.PrimitiveIndex * 2u + 1u].x);
    }

    bool FrontFacing = dot(GeometricNormal, Direction) < 0.0f;
    if (!FrontFacing)
    {
        GeometricNormal = -GeometricNormal;
    }
    if (dot(ShadingNormal, GeometricNormal) < 0.0f)
    {
        ShadingNormal = -ShadingNormal;
    }
    if (dot(ShadingNormal, -Direction) <= 1.0e-4f)
    {
        ShadingNormal = GeometricNormal;
    }

    ResolvedSurfaceSample Sample;
    Sample.Position        = Position;
    Sample.GeometricNormal = GeometricNormal;
    Sample.ShadingNormal   = ShadingNormal;
    Sample.MaterialSlot    = MaterialSlot;
    Sample.FrontFacing     = FrontFacing;
    return Sample;
}

float3 EvaluateStudioEnvironment(float3 Direction)
{
    float  Elevation = Direction.y;
    float3 Radiance  = float3(0.0f, 0.0f, 0.0f);
    uint   Category  = Tracing.SceneLayout.y;
    if (Category == 0u)
    {
        float Overhead = pow(max(Elevation, 0.0f), 1.5f);
        Radiance = lerp(float3(0.020f, 0.021f, 0.024f), float3(0.090f, 0.095f, 0.105f), Overhead)
                 + float3(0.030f, 0.024f, 0.018f) * exp(-abs(Elevation) * 9.0f);
    }
    else if (Category == 1u)
    {
        float Gradient = (1.0f + 2.0f * max(Elevation, 0.0f)) / 3.0f;
        Radiance = (Elevation > 0.0f) ? (float3(0.80f, 0.86f, 0.95f) * Gradient) : (float3(0.18f, 0.17f, 0.16f) * 0.5f);
    }
    else
    {
        float UpperSkyFactor      = pow(max(Elevation, 0.0f), 0.6f);
        float SkylineGlow = exp(-max(Elevation, 0.0f) * 6.0f) * (0.6f + 0.4f * max(dot(normalize(Direction.xz + float2(1.0e-6f, 1.0e-6f)), float2(-0.6f, -0.8f)), 0.0f));
        Radiance = (Elevation > 0.0f)
                       ? (float3(0.06f, 0.10f, 0.25f) * UpperSkyFactor + float3(1.1f, 0.42f, 0.14f) * SkylineGlow)
                       : float3(0.05f, 0.035f, 0.03f);
    }
    return Radiance * Tracing.EnvironmentTint.rgb * Tracing.EnvironmentTint.w;
}

float DistributionGgx(float CosineHalf, float Alpha)
{
    float Alpha2 = Alpha * Alpha;
    float Denom  = CosineHalf * CosineHalf * (Alpha2 - 1.0f) + 1.0f;
    return Alpha2 / (PiConstant * Denom * Denom);
}

float SmithLambda(float Cosine, float Alpha)
{
    float Cosine2  = max(Cosine * Cosine, 1.0e-8f);
    float Tangent2 = (1.0f - Cosine2) / Cosine2;
    return 0.5f * (-1.0f + sqrt(1.0f + Alpha * Alpha * Tangent2));
}

OrthonormalBasis3 BuildOrthonormalBasis(float3 Normal)
{
    float SignVal = select(Normal.z >= 0.0f, 1.0f, -1.0f);
    float Inverse = -1.0f / (SignVal + Normal.z);
    float Shear   = Normal.x * Normal.y * Inverse;
    OrthonormalBasis3 Basis;
    Basis.Tangent   = float3(1.0f + SignVal * Normal.x * Normal.x * Inverse, SignVal * Shear, -SignVal * Normal.x);
    Basis.Bitangent = float3(Shear, SignVal + Normal.y * Normal.y * Inverse, -Normal.y);
    Basis.Normal    = Normal;
    return Basis;
}

float3 BasisToWorld(OrthonormalBasis3 Basis, float3 Local)
{
    return Basis.Tangent * Local.x + Basis.Bitangent * Local.y + Basis.Normal * Local.z;
}

float3 WorldToBasis(OrthonormalBasis3 Basis, float3 World)
{
    return float3(dot(Basis.Tangent, World), dot(Basis.Bitangent, World), dot(Basis.Normal, World));
}

// Dupuy & Benyoub (2023) Spherical-Cap GGX Visible Normal Distribution Function (VNDF) sampler
float3 SampleVisibleNormal(float3 ViewLocal, float Alpha, float2 Point)
{
    float3 Stretched     = normalize(float3(Alpha * ViewLocal.x, Alpha * ViewLocal.y, max(1.0e-5f, ViewLocal.z)));
    float  Phi           = 2.0f * PiConstant * Point.x;
    float  CapZ          = (1.0f - Point.y) * (1.0f + Stretched.z) - Stretched.z;
    float  SinTheta      = sqrt(max(0.0f, 1.0f - CapZ * CapZ));
    float3 HalfStretched = float3(
        SinTheta * cos(Phi) + Stretched.x,
        SinTheta * sin(Phi) + Stretched.y,
        max(0.0f, CapZ + Stretched.z));
    return normalize(float3(Alpha * HalfStretched.x, Alpha * HalfStretched.y, max(1.0e-6f, HalfStretched.z)));
}

float2 ConcentricDisk(float2 Point)
{
    float2 Offset = 2.0f * Point - float2(1.0f, 1.0f);
    if (Offset.x == 0.0f && Offset.y == 0.0f)
    {
        return float2(0.0f, 0.0f);
    }
    float Radius;
    float Theta;
    if (abs(Offset.x) > abs(Offset.y))
    {
        Radius = Offset.x;
        Theta  = (PiConstant / 4.0f) * (Offset.y / Offset.x);
    }
    else
    {
        Radius = Offset.y;
        Theta  = (PiConstant / 2.0f) - (PiConstant / 4.0f) * (Offset.x / Offset.y);
    }
    return Radius * float2(cos(Theta), sin(Theta));
}

float3 CosineHemisphere(float2 Point)
{
    float2 Disk = ConcentricDisk(Point);
    return float3(Disk.x, Disk.y, sqrt(max(0.0f, 1.0f - dot(Disk, Disk))));
}

float2 EvaluateGgxLobe(float CosineOut, float CosineIn, float CosineHalf, float Alpha)
{
    float D         = DistributionGgx(CosineHalf, Alpha);
    float LambdaOut = SmithLambda(CosineOut, Alpha);
    float LambdaIn  = SmithLambda(CosineIn, Alpha);
    float G2        = 1.0f / (1.0f + LambdaOut + LambdaIn);
    float G1        = 1.0f / (1.0f + LambdaOut);
    return float2(D * G2 / (4.0f * CosineOut), D * G1 / (4.0f * CosineOut));
}

float MaterialAlpha(float PerceptualRoughness)
{
    return PerceptualRoughness * PerceptualRoughness;
}

LobeEvaluationResult EvaluateSurfaceLobes(PathSurfaceMaterial Material, float3 Normal, float3 GeometricNormal, float3 OmegaOut, float3 OmegaIn)
{
    LobeEvaluationResult Lobes;
    Lobes.Diffuse     = float3(0.0f, 0.0f, 0.0f);
    Lobes.DiffusePdf  = 0.0f;
    Lobes.Specular    = float3(0.0f, 0.0f, 0.0f);
    Lobes.SpecularPdf = 0.0f;
    Lobes.Coat        = float3(0.0f, 0.0f, 0.0f);
    Lobes.CoatPdf     = 0.0f;

    float CosineIn  = dot(Normal, OmegaIn);
    float CosineOut = dot(Normal, OmegaOut);
    if (CosineIn <= 0.0f || CosineOut <= 0.0f || dot(GeometricNormal, OmegaIn) <= 0.0f)
    {
        return Lobes;
    }

    float3 HalfVector    = normalize(OmegaOut + OmegaIn);
    float  CosineHalf    = max(dot(Normal, HalfVector), 0.0f);
    float  CosineHalfOut = dot(OmegaOut, HalfVector);
    uint   Category      = uint(Material.EtaCategory.w);
    float  Alpha         = max(MaterialAlpha(Material.AlbedoRoughness.w), 1.0e-4f);

    if (Category == SurfaceCategoryMetallic)
    {
        if (Alpha >= DeltaRoughnessAlpha)
        {
            float2 Ggx = EvaluateGgxLobe(CosineOut, CosineIn, CosineHalf, Alpha);
            Lobes.Specular    = FresnelMetal(CosineHalfOut, Material.EtaCategory.rgb, Material.KappaCoat.rgb) * Ggx.x;
            Lobes.SpecularPdf = Ggx.y;
        }
        return Lobes;
    }

    float CoatWeight      = Material.KappaCoat.w;
    float CoatAlpha       = max(MaterialAlpha(Material.ExitanceCoat.w), 1.0e-4f);
    float F0              = Material.SpecularLevel.x;
    float CoatAttenuation = (CoatWeight > 0.0f)
                                ? ((1.0f - CoatWeight * FresnelSchlick(0.04f, CosineOut)) * (1.0f - CoatWeight * FresnelSchlick(0.04f, CosineIn)))
                                : 1.0f;

    if (CoatWeight > 0.0f && CoatAlpha >= DeltaRoughnessAlpha)
    {
        float2 Ggx    = EvaluateGgxLobe(CosineOut, CosineIn, CosineHalf, CoatAlpha);
        float  CoatV  = CoatWeight * FresnelSchlick(0.04f, CosineHalfOut) * Ggx.x;
        Lobes.Coat    = float3(CoatV, CoatV, CoatV);
        Lobes.CoatPdf = Ggx.y;
    }
    if (F0 > 0.0f && Alpha >= DeltaRoughnessAlpha)
    {
        float2 Ggx        = EvaluateGgxLobe(CosineOut, CosineIn, CosineHalf, Alpha);
        float  SpecV      = CoatAttenuation * FresnelSchlick(F0, CosineHalfOut) * Ggx.x;
        Lobes.Specular    = float3(SpecV, SpecV, SpecV);
        Lobes.SpecularPdf = Ggx.y;
    }
    float SpecTransfer    = (F0 > 0.0f) ? ((1.0f - FresnelSchlick(F0, CosineOut)) * (1.0f - FresnelSchlick(F0, CosineIn))) : 1.0f;
    float DiffuseTransfer = SpecTransfer * CoatAttenuation;
    Lobes.Diffuse         = Material.AlbedoRoughness.rgb * (DiffuseTransfer * CosineIn / PiConstant);
    Lobes.DiffusePdf      = CosineIn / PiConstant;
    return Lobes;
}

float PowerHeuristic(float Primary, float Secondary)
{
    float P2 = Primary * Primary;
    float S2 = Secondary * Secondary;
    return select(P2 + S2 > 0.0f, P2 / max(P2 + S2, 1.0e-20f), 1.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRANSPORT & LIGHT TREE
//------------------------------------------------------------------------------------------------------------------------

void AssignTransportSlot(uint Slot, TransportPathRay Ray)
{
    gTransportStack[Slot] = Ray;
}

TransportPathRay PopTransportSlot()
{
    gTransportStackSize -= 1u;
    return gTransportStack[gTransportStackSize];
}

void PushTransportRay(TransportPathRay Ray)
{
    float Peak = max(max(Ray.Throughput.x, Ray.Throughput.y), Ray.Throughput.z);
    if (Peak < Tracing.Tuning.x || RayDepth(Ray) >= Tracing.DepthLimits.y)
    {
        return;
    }
    if (gTransportStackSize >= TransportStackCapacity)
    {
        return;
    }
    AssignTransportSlot(gTransportStackSize, Ray);
    gTransportStackSize += 1u;
}

float3 OffsetRayOrigin(float3 Position, float3 Normal, float3 Direction)
{
    float Scale = RayOffsetScale * (1.0f + max(max(abs(Position.x), abs(Position.y)), abs(Position.z)) * 0.1f);
    return Position + Normal * select(dot(Normal, Direction) > 0.0f, Scale, -Scale);
}

float2 CosSinSubClamped(float CosA, float SinA, float CosB, float SinB)
{
    if (CosA >= CosB)
    {
        return float2(1.0f, 0.0f);
    }
    return float2(CosA * CosB + SinA * SinB, max(0.0f, SinA * CosB - CosA * SinB));
}

float EvaluatePhotometricProfile(uint ProfileIndex, float3 Toward, float3 Axis, float3 Reference)
{
    float Vertical   = acos(clamp(dot(Axis, Toward), -1.0f, 1.0f)) * (float(ProfileVerticalSamples - 1u) / PiConstant);
    float Horizontal = atan2(dot(Toward, cross(Axis, Reference)), dot(Toward, Reference)) * (float(ProfileHorizontalSamples) / (2.0f * PiConstant));
    if (Horizontal < 0.0f)
    {
        Horizontal += float(ProfileHorizontalSamples);
    }
    uint  V0        = min(uint(Vertical), ProfileVerticalSamples - 2u);
    float Vt        = Vertical - float(V0);
    uint  H0        = min(uint(Horizontal), ProfileHorizontalSamples - 1u);
    float Ht        = clamp(Horizontal - float(H0), 0.0f, 1.0f);
    uint  H1        = (H0 + 1u) % ProfileHorizontalSamples;
    uint  StartSlot = Tracing.Practical.w + ProfileIndex * (ProfileVerticalSamples * ProfileHorizontalSamples / 4u);
    uint  I00       = H0 * ProfileVerticalSamples + V0;
    uint  I10       = H1 * ProfileVerticalSamples + V0;
    float Near      = lerp(LightRegister[StartSlot + (I00 >> 2u)][I00 & 3u], LightRegister[StartSlot + ((I00 + 1u) >> 2u)][(I00 + 1u) & 3u], Vt);
    float Far       = lerp(LightRegister[StartSlot + (I10 >> 2u)][I10 & 3u], LightRegister[StartSlot + ((I10 + 1u) >> 2u)][(I10 + 1u) & 3u], Vt);
    return lerp(Near, Far, Ht);
}

float2 LightClusterImportance(float3 Position, float3 Normal, uint ClusterSlot)
{
    uint   StartSlot = Tracing.Practical.y + ClusterSlot * 4u;
    float4 Lower     = LightRegister[StartSlot];
    float4 Upper     = LightRegister[StartSlot + 1u];
    uint   Link      = asuint(Upper.w);
    if ((Link & LampClusterLeafBit) != 0u)
    {
        uint   LampSlot   = Tracing.Practical.z + (Link & ~LampClusterLeafBit) * 4u;
        float4 Placement  = LightRegister[LampSlot];
        float3 Separation = Placement.xyz - Position;
        float  Distance2  = max(dot(Separation, Separation), 1.0e-12f);
        float3 OmegaIn    = Separation * rsqrt(Distance2);
        float  Incidence  = dot(Normal, OmegaIn);
        if (Incidence <= 0.0f)
        {
            return float2(0.0f, 0.0f);
        }
        float Relative   = EvaluatePhotometricProfile(uint(Placement.w), -OmegaIn, LightRegister[LampSlot + 1u].xyz, LightRegister[LampSlot + 2u].xyz);
        float Importance = Relative * dot(LightRegister[LampSlot + 3u].rgb, float3(0.2126f, 0.7152f, 0.0722f)) * Incidence / Distance2;
        return float2(Importance, Relative);
    }

    float4 Cone      = LightRegister[StartSlot + 2u];
    float4 Angles    = LightRegister[StartSlot + 3u];
    float3 Diagonal  = Upper.xyz - Lower.xyz;
    float  Radius2   = 0.25f * dot(Diagonal, Diagonal);
    float3 Offset    = Position - 0.5f * (Lower.xyz + Upper.xyz);
    float  Distance2 = dot(Offset, Offset);
    float  CosOuter  = 1.0f;
    float  CosInner  = 1.0f;
    if (Distance2 <= Radius2)
    {
        if (Angles.w <= 0.0f)
        {
            return float2(0.0f, -1.0f);
        }
    }
    else
    {
        float  InvDist = rsqrt(Distance2);
        float3 Toward  = Offset * InvDist;
        float  SinB    = sqrt(Radius2 / Distance2);
        float  CosB    = sqrt(max(0.0f, 1.0f - SinB * SinB));
        float  CosN    = clamp(-dot(Normal, Toward), -1.0f, 1.0f);
        float  SinN    = sqrt(max(0.0f, 1.0f - CosN * CosN));
        CosInner       = select(CosN >= CosB, 1.0f, CosN * CosB + SinN * SinB);
        if (CosInner <= 0.0f)
        {
            return float2(0.0f, -1.0f);
        }
        float  CosC  = clamp(dot(Cone.xyz, Toward), -1.0f, 1.0f);
        float  SinC  = sqrt(max(0.0f, 1.0f - CosC * CosC));
        float2 Step1 = CosSinSubClamped(CosC, SinC, Angles.x, Angles.y);
        float2 Step2 = CosSinSubClamped(Step1.x, Step1.y, CosB, SinB);
        CosOuter     = Step2.x;
        if (CosOuter <= Angles.z)
        {
            return float2(0.0f, -1.0f);
        }
    }
    return float2(Lower.w * max(CosOuter, 1.0e-3f) * CosInner / max(Distance2, Radius2), -1.0f);
}

uint ResolveLampCut(float3 Position)
{
    uint Limit  = clamp(uint(Tracing.Shading.w), 1u, LampCutCapacity);
    WriteLampCut(0u, 0u);
    uint Count  = 1u;
    for (uint Round = 1u; Round < Limit; ++Round)
    {
        uint  Chosen = LampCutCapacity;
        float Score  = 0.0f;
        for (uint Entry = 0u; Entry < Count; ++Entry)
        {
            uint   StartSlot = Tracing.Practical.y + ReadLampCut(Entry) * 4u;
            float4 Lower     = LightRegister[StartSlot];
            float4 Upper     = LightRegister[StartSlot + 1u];
            if ((asuint(Upper.w) & LampClusterLeafBit) != 0u)
            {
                continue;
            }
            float3 Diagonal  = Upper.xyz - Lower.xyz;
            float  Radius2   = 0.25f * dot(Diagonal, Diagonal);
            float3 Offset    = Position - 0.5f * (Lower.xyz + Upper.xyz);
            float  Distance2 = max(dot(Offset, Offset), 1.0e-6f);
            float  Spread    = Lower.w * Radius2 / (Distance2 * Distance2);
            if (Radius2 > LampSplitRatio * Distance2 && Spread > Score)
            {
                Score  = Spread;
                Chosen = Entry;
            }
        }
        if (Chosen == LampCutCapacity)
        {
            break;
        }
        uint Link = asuint(LightRegister[Tracing.Practical.y + ReadLampCut(Chosen) * 4u + 1u].w);
        WriteLampCut(Chosen, Link);
        WriteLampCut(Count, Link + 1u);
        Count += 1u;
    }
    return Count;
}

PracticalLampPick SelectPracticalLamp(float3 Position, float3 Normal, uint CutEntry, float Selection)
{
    PracticalLampPick EmptyPick;
    EmptyPick.LampIndex = 0u;
    EmptyPick.Pdf       = 0.0f;
    EmptyPick.Relative  = -1.0f;

    if (Tracing.Shading.z > 0.5f)
    {
        PracticalLampPick AllPick;
        AllPick.LampIndex = CutEntry;
        AllPick.Pdf       = 1.0f;
        AllPick.Relative  = -1.0f;
        return AllPick;
    }

    uint  Cluster  = ReadLampCut(min(CutEntry, LampCutCapacity - 1u));
    float Pdf      = 1.0f;
    float Relative = -1.0f;
    float Steer    = frac(Selection + float(CutEntry) * GoldenFraction);
    [loop] for (uint Level = 0u; Level < 32u; ++Level)
    {
        uint Link = asuint(LightRegister[Tracing.Practical.y + Cluster * 4u + 1u].w);
        if ((Link & LampClusterLeafBit) != 0u)
        {
            PracticalLampPick LeafPick;
            LeafPick.LampIndex = Link & ~LampClusterLeafBit;
            LeafPick.Pdf       = Pdf;
            LeafPick.Relative  = Relative;
            return LeafPick;
        }
        float2 LeftRes  = LightClusterImportance(Position, Normal, Link);
        float2 RightRes = LightClusterImportance(Position, Normal, Link + 1u);
        float  Total    = LeftRes.x + RightRes.x;
        if (!(Total > 0.0f))
        {
            break;
        }
        float LeftShare  = LeftRes.x / Total;
        float RightShare = RightRes.x / Total;
        if (Steer < LeftShare)
        {
            Cluster  = Link;
            Pdf     *= LeftShare;
            Relative = LeftRes.y;
            Steer    = min(Steer / LeftShare, SelectionCeiling);
        }
        else
        {
            Cluster  = Link + 1u;
            Pdf     *= RightShare;
            Relative = RightRes.y;
            Steer    = min((Steer - LeftShare) / RightShare, SelectionCeiling);
        }
    }
    return EmptyPick;
}

float3 IntegrateLightQuadrature(
    ResolvedSurfaceSample Surface,
    PathSurfaceMaterial   Material,
    float3                OmegaOut,
    uint                  SampleIndex,
    uint                  Depth,
    bool                  LobesContinue)
{
    float3 Radiance  = float3(0.0f, 0.0f, 0.0f);
    uint   AreaCount = Tracing.DepthLimits.z;
    float2 Point     = float2(0.0f, 0.0f);
    if (AreaCount > 0u)
    {
        Point = DepthPair(SampleIndex, Depth, PairSlotLight);
    }
    bool  AllLamps  = (Tracing.Shading.z > 0.5f);
    uint  LampPickCount = select(AllLamps, Tracing.Practical.x, min(Tracing.Practical.x, 1u));
    float Selection = 0.0f;
    if (LampPickCount > 0u && !AllLamps)
    {
        Selection = QuadraturePair(SampleIndex, PairPracticalStart + min(Depth, 15u)).x;
        LampPickCount = ResolveLampCut(Surface.Position);
    }

    [loop] for (uint EntryIndex = 0u; EntryIndex < AreaCount + LampPickCount; ++EntryIndex)
    {
        float3 OmegaIn  = float3(0.0f, 0.0f, 0.0f);
        float  Distance = 0.0f;
        float  LightPdf = 0.0f;
        float3 RadiantExitance  = float3(0.0f, 0.0f, 0.0f);
        bool   Delta    = false;

        if (EntryIndex < AreaCount)
        {
            StudioAreaLight Light       = FetchAreaLight(EntryIndex);
            float3          LightPoint  = Light.Centre.xyz + (Point.x - 0.5f) * Light.EdgeU.xyz + (Point.y - 0.5f) * Light.EdgeV.xyz;
            float3          Separation  = LightPoint - Surface.Position;
            float           Distance2   = dot(Separation, Separation);
            Distance                    = sqrt(Distance2);
            OmegaIn                     = Separation / Distance;
            if (dot(Surface.GeometricNormal, OmegaIn) <= 0.0f || dot(Surface.ShadingNormal, OmegaIn) <= 0.0f)
            {
                continue;
            }
            float CosineLight = -dot(OmegaIn, AreaLightNormal(Light));
            if (CosineLight <= 0.0f)
            {
                continue;
            }
            LightPdf = Distance2 / (CosineLight * Light.Centre.w);
            RadiantExitance  = Light.Radiance.rgb;
        }
        else
        {
            PracticalLampPick LampPick = SelectPracticalLamp(Surface.Position, Surface.ShadingNormal, EntryIndex - AreaCount, Selection);
            if (LampPick.Pdf <= 0.0f)
            {
                continue;
            }
            uint   LampSlot   = Tracing.Practical.z + LampPick.LampIndex * 4u;
            float4 Placement  = LightRegister[LampSlot];
            float3 Separation = Placement.xyz - Surface.Position;
            float  Distance2  = dot(Separation, Separation);
            Distance          = sqrt(Distance2);
            OmegaIn           = Separation / Distance;
            if (dot(Surface.GeometricNormal, OmegaIn) <= 0.0f || dot(Surface.ShadingNormal, OmegaIn) <= 0.0f)
            {
                continue;
            }
            float Relative = (LampPick.Relative >= 0.0f)
                                 ? LampPick.Relative
                                 : EvaluatePhotometricProfile(uint(Placement.w), -OmegaIn, LightRegister[LampSlot + 1u].xyz, LightRegister[LampSlot + 2u].xyz);
            if (Relative <= 0.0f)
            {
                continue;
            }
            LightPdf = Distance2 * LampPick.Pdf;
            RadiantExitance  = LightRegister[LampSlot + 3u].rgb * Relative;
            Delta    = true;
        }

        LobeEvaluationResult Lobes        = EvaluateSurfaceLobes(Material, Surface.ShadingNormal, Surface.GeometricNormal, OmegaOut, OmegaIn);
        float3               ContinuedPdf = LobesContinue ? float3(Lobes.DiffusePdf, Lobes.SpecularPdf, Lobes.CoatPdf) : float3(0.0f, 0.0f, 0.0f);
        float3               Combined     = Lobes.Diffuse  * PowerHeuristic(LightPdf, ContinuedPdf.x)
                                          + Lobes.Specular * PowerHeuristic(LightPdf, ContinuedPdf.y)
                                          + Lobes.Coat     * PowerHeuristic(LightPdf, ContinuedPdf.z);
        float3 Weighted = select(bool3(Delta, Delta, Delta), Lobes.Diffuse + Lobes.Specular + Lobes.Coat, Combined);
        if (max(max(Weighted.x, Weighted.y), Weighted.z) <= 0.0f)
        {
            continue;
        }

        float3 ShadowOrigin = OffsetRayOrigin(Surface.Position, Surface.GeometricNormal, OmegaIn);
        float3 Visibility   = TraceShadowTransmittance(ShadowOrigin, OmegaIn, Distance * (1.0f - 1.0e-4f));
        Radiance += Visibility * RadiantExitance * Weighted / LightPdf;
    }
    return Radiance;
}

float3 ReflectionLobeFresnel(PathSurfaceMaterial Material, uint LobeSelector, float CosineOut, float CosineIn, float CosineHalfOut, float CoatOut)
{
    if (LobeSelector == LobeSelectorMetal)
    {
        return FresnelMetal(CosineHalfOut, Material.EtaCategory.rgb, Material.KappaCoat.rgb);
    }
    float CoatWeight = Material.KappaCoat.w;
    if (LobeSelector == LobeSelectorCoat)
    {
        float CoatF = CoatWeight * FresnelSchlick(0.04f, CosineHalfOut);
        return float3(CoatF, CoatF, CoatF);
    }
    float CoatAtten = (CoatWeight > 0.0f) ? (CoatOut * (1.0f - CoatWeight * FresnelSchlick(0.04f, CosineIn))) : 1.0f;
    float SpecF     = CoatAtten * FresnelSchlick(Material.SpecularLevel.x, CosineHalfOut);
    return float3(SpecF, SpecF, SpecF);
}

void PushReflectionLobe(
    ResolvedSurfaceSample Surface,
    TransportPathRay      Ray,
    PathSurfaceMaterial   Material,
    float3                OmegaOut,
    float                 Alpha,
    uint                  SampleIndex,
    uint                  PairSlot,
    uint                  LobeSelector,
    OrthonormalBasis3     Basis,
    float3                ViewLocal,
    float                 CoatOut)
{
    uint Depth         = RayDepth(Ray);
    uint NonDeltaDepth = RayNonDeltaDepth(Ray);
    uint Medium        = RayMedium(Ray);
    if (Depth + 1u >= Tracing.DepthLimits.y)
    {
        return;
    }
    float3 Normal    = Surface.ShadingNormal;
    float  CosineOut = max(dot(Normal, OmegaOut), 1.0e-6f);
    if (Alpha < DeltaRoughnessAlpha)
    {
        float3 OmegaIn = reflect(-OmegaOut, Normal);
        if (dot(OmegaIn, Surface.GeometricNormal) <= 0.0f)
        {
            return;
        }
        float3 Weight = ReflectionLobeFresnel(Material, LobeSelector, CosineOut, CosineOut, CosineOut, CoatOut);
        TransportPathRay NextRay;
        NextRay.Origin          = OffsetRayOrigin(Surface.Position, Surface.GeometricNormal, OmegaIn);
        NextRay.PackedCounters     = PackRayCounters(Depth + 1u, NonDeltaDepth, Medium);
        NextRay.Direction       = OmegaIn;
        NextRay.LobePdf         = 0.0f;
        NextRay.Throughput      = Ray.Throughput * Weight;
        NextRay.TravelSinceLobe = 0.0f;
        PushTransportRay(NextRay);
        return;
    }

    if (NonDeltaDepth >= Tracing.DepthLimits.x)
    {
        return;
    }
    float2 Point      = DepthPair(SampleIndex, Depth, PairSlot);
    float3 HalfVector = BasisToWorld(Basis, SampleVisibleNormal(ViewLocal, Alpha, Point));
    float3 OmegaIn    = reflect(-OmegaOut, HalfVector);
    float  CosineIn   = dot(Normal, OmegaIn);
    if (CosineIn <= 0.0f || dot(OmegaIn, Surface.GeometricNormal) <= 0.0f)
    {
        return;
    }
    float  LambdaOut = SmithLambda(CosineOut, Alpha);
    float  LambdaIn  = SmithLambda(CosineIn, Alpha);
    float  Masking   = (1.0f + LambdaOut) / (1.0f + LambdaOut + LambdaIn);
    float  Pdf       = DistributionGgx(max(dot(Normal, HalfVector), 0.0f), Alpha) / ((1.0f + LambdaOut) * 4.0f * CosineOut);
    float3 Weight    = ReflectionLobeFresnel(Material, LobeSelector, CosineOut, CosineIn, max(dot(OmegaOut, HalfVector), 0.0f), CoatOut) * Masking;

    TransportPathRay NextRay;
    NextRay.Origin          = OffsetRayOrigin(Surface.Position, Surface.GeometricNormal, OmegaIn);
    NextRay.PackedCounters     = PackRayCounters(Depth + 1u, NonDeltaDepth + 1u, Medium);
    NextRay.Direction       = OmegaIn;
    NextRay.LobePdf         = Pdf;
    NextRay.Throughput      = Ray.Throughput * Weight;
    NextRay.TravelSinceLobe = 0.0f;
    PushTransportRay(NextRay);
}

float3 ScatterSurfaceVertex(ResolvedSurfaceSample Surface, TransportPathRay Ray, float HitDistance, uint SampleIndex)
{
    PathSurfaceMaterial Material      = Materials[Surface.MaterialSlot];
    uint                Category      = uint(Material.EtaCategory.w);
    float3              OmegaOut      = -Ray.Direction;
    float3              Normal        = Surface.ShadingNormal;
    float               CosineOut     = max(dot(Normal, OmegaOut), 1.0e-6f);
    uint                Depth         = RayDepth(Ray);
    uint                NonDeltaDepth = RayNonDeltaDepth(Ray);
    uint                Medium        = RayMedium(Ray);
    float3              Radiance      = float3(0.0f, 0.0f, 0.0f);

    if (Surface.FrontFacing)
    {
        Radiance += Ray.Throughput * Material.ExitanceCoat.rgb;
    }

    if (Category == SurfaceCategorySolidGlass)
    {
        if (Depth + 1u >= Tracing.DepthLimits.y)
        {
            return Radiance;
        }
        float  Ior       = Material.AbsorptionIor.w;
        float  Eta       = select(Surface.FrontFacing, Ior, 1.0f / Ior);
        float  Cosine    = clamp(dot(Surface.GeometricNormal, OmegaOut), 0.0f, 1.0f);
        float  Reflected = FresnelDielectric(Cosine, Eta);
        float3 OmegaRefl = reflect(-OmegaOut, Surface.GeometricNormal);

        TransportPathRay ReflRay;
        ReflRay.Origin          = OffsetRayOrigin(Surface.Position, Surface.GeometricNormal, OmegaRefl);
        ReflRay.PackedCounters     = PackRayCounters(Depth + 1u, NonDeltaDepth, Medium);
        ReflRay.Direction       = OmegaRefl;
        ReflRay.LobePdf         = 0.0f;
        ReflRay.Throughput      = Ray.Throughput * Reflected;
        ReflRay.TravelSinceLobe = 0.0f;
        PushTransportRay(ReflRay);

        if (Reflected < 1.0f)
        {
            float3 OmegaTrans  = refract(-OmegaOut, Surface.GeometricNormal, 1.0f / Eta);
            uint   InnerMedium = select(Surface.FrontFacing, Surface.MaterialSlot, InvalidMediumSlot);
            TransportPathRay TransRay;
            TransRay.Origin          = OffsetRayOrigin(Surface.Position, Surface.GeometricNormal, OmegaTrans);
            TransRay.PackedCounters     = PackRayCounters(Depth + 1u, NonDeltaDepth, InnerMedium);
            TransRay.Direction       = OmegaTrans;
            TransRay.LobePdf         = 0.0f;
            TransRay.Throughput      = Ray.Throughput * (1.0f - Reflected);
            TransRay.TravelSinceLobe = 0.0f;
            PushTransportRay(TransRay);
        }
        return Radiance;
    }

    if (Category == SurfaceCategoryThinSheet)
    {
        if (Depth + 1u >= Tracing.DepthLimits.y)
        {
            return Radiance;
        }
        float  Cosine      = clamp(dot(Surface.GeometricNormal, OmegaOut), 0.0f, 1.0f);
        float  Reflectance = FresnelDielectric(Cosine, Material.AbsorptionIor.w);
        float  Sheet       = Reflectance + (1.0f - Reflectance) * (1.0f - Reflectance) * Reflectance / max(1.0f - Reflectance * Reflectance, 1.0e-6f);
        float3 OmegaRefl   = reflect(-OmegaOut, Surface.GeometricNormal);

        TransportPathRay ReflRay;
        ReflRay.Origin          = OffsetRayOrigin(Surface.Position, Surface.GeometricNormal, OmegaRefl);
        ReflRay.PackedCounters     = PackRayCounters(Depth + 1u, NonDeltaDepth, Medium);
        ReflRay.Direction       = OmegaRefl;
        ReflRay.LobePdf         = 0.0f;
        ReflRay.Throughput      = Ray.Throughput * Sheet;
        ReflRay.TravelSinceLobe = 0.0f;
        PushTransportRay(ReflRay);

        TransportPathRay SheetRay;
        SheetRay.Origin          = OffsetRayOrigin(Surface.Position, Surface.GeometricNormal, Ray.Direction);
        SheetRay.PackedCounters     = PackRayCounters(Depth + 1u, NonDeltaDepth, Medium);
        SheetRay.Direction       = Ray.Direction;
        SheetRay.LobePdf         = Ray.LobePdf;
        SheetRay.Throughput      = Ray.Throughput * ThinSheetTransmittance(Cosine, Material);
        SheetRay.TravelSinceLobe = Ray.TravelSinceLobe + HitDistance;
        PushTransportRay(SheetRay);
        return Radiance;
    }

    bool  BouncePermitted   = (Depth + 1u < Tracing.DepthLimits.y);
    bool  LobesContinue = (NonDeltaDepth < Tracing.DepthLimits.x) && BouncePermitted;
    float Alpha         = max(MaterialAlpha(Material.AlbedoRoughness.w), 1.0e-4f);
    if (!(Category == SurfaceCategoryMetallic && Alpha < DeltaRoughnessAlpha))
    {
        Radiance += Ray.Throughput * IntegrateLightQuadrature(Surface, Material, OmegaOut, SampleIndex, Depth, LobesContinue);
    }
    if (!BouncePermitted)
    {
        return Radiance;
    }

    bool NeedBasis = LobesContinue && (Category != SurfaceCategoryMetallic || Alpha >= DeltaRoughnessAlpha);
    OrthonormalBasis3 Basis;
    Basis.Tangent   = float3(1.0f, 0.0f, 0.0f);
    Basis.Bitangent = float3(0.0f, 1.0f, 0.0f);
    Basis.Normal    = float3(0.0f, 0.0f, 1.0f);
    float3 ViewLocal = float3(0.0f, 0.0f, 1.0f);
    if (NeedBasis)
    {
        Basis     = BuildOrthonormalBasis(Normal);
        ViewLocal = WorldToBasis(Basis, OmegaOut);
    }

    if (Category == SurfaceCategoryMetallic)
    {
        PushReflectionLobe(Surface, Ray, Material, OmegaOut, Alpha, SampleIndex, PairSlotSpecular, LobeSelectorMetal, Basis, ViewLocal, 1.0f);
        return Radiance;
    }

    float CoatWeight = Material.KappaCoat.w;
    float CoatOut    = (CoatWeight > 0.0f) ? (1.0f - CoatWeight * FresnelSchlick(0.04f, CosineOut)) : 1.0f;
    float F0         = Material.SpecularLevel.x;
    if (CoatWeight > 0.0f)
    {
        float CoatAlpha = max(MaterialAlpha(Material.ExitanceCoat.w), 1.0e-4f);
        PushReflectionLobe(Surface, Ray, Material, OmegaOut, CoatAlpha, SampleIndex, PairSlotCoat, LobeSelectorCoat, Basis, ViewLocal, CoatOut);
    }
    if (F0 > 0.0f)
    {
        PushReflectionLobe(Surface, Ray, Material, OmegaOut, Alpha, SampleIndex, PairSlotSpecular, LobeSelectorSpecular, Basis, ViewLocal, CoatOut);
    }

    if (LobesContinue)
    {
        float  SpecularOut       = (F0 > 0.0f) ? (1.0f - FresnelSchlick(F0, CosineOut)) : 1.0f;
        float  SubstrateTransfer = SpecularOut * CoatOut;
        float3 MaxDiffuse        = Ray.Throughput * Material.AlbedoRoughness.rgb * SubstrateTransfer;
        if (max(max(MaxDiffuse.x, MaxDiffuse.y), MaxDiffuse.z) >= Tracing.Tuning.x)
        {
            float3 OmegaIn  = BasisToWorld(Basis, CosineHemisphere(DepthPair(SampleIndex, Depth, PairSlotDiffuse)));
            float  CosineIn = dot(Normal, OmegaIn);
            if (CosineIn > 0.0f && dot(OmegaIn, Surface.GeometricNormal) > 0.0f)
            {
                float CoatIn   = (CoatWeight > 0.0f) ? (1.0f - CoatWeight * FresnelSchlick(0.04f, CosineIn)) : 1.0f;
                float SpecIn   = (F0 > 0.0f) ? (1.0f - FresnelSchlick(F0, CosineIn)) : 1.0f;
                float Transfer = SubstrateTransfer * SpecIn * CoatIn;

                TransportPathRay DiffRay;
                DiffRay.Origin          = OffsetRayOrigin(Surface.Position, Surface.GeometricNormal, OmegaIn);
                DiffRay.PackedCounters     = PackRayCounters(Depth + 1u, NonDeltaDepth + 1u, Medium);
                DiffRay.Direction       = OmegaIn;
                DiffRay.LobePdf         = CosineIn / PiConstant;
                DiffRay.Throughput      = Ray.Throughput * Material.AlbedoRoughness.rgb * Transfer;
                DiffRay.TravelSinceLobe = 0.0f;
                PushTransportRay(DiffRay);
            }
        }
    }
    return Radiance;
}

SurfaceHitResult ResolveRasterizedPrimary(float3 Origin, float3 Direction, uint2 PixelCoordinate)
{
    SurfaceHitResult Hit = TraceAnalyticHit(Origin, Direction, Tracing.Tuning.z > 0.5f);
    uint Identifier      = VisibilityIds.Load(int3(int2(PixelCoordinate), 0));
    if (Identifier == 0u)
    {
        return Hit;
    }
    uint   Instance       = (Identifier - 1u) >> InstanceIndexShift;
    float3 LocalOrigin    = Origin;
    float3 LocalDirection = Direction;
    if (Tracing.Geometry.z > 1u)
    {
        float4 Yaw     = FetchInstanceYaw(Instance);
        LocalOrigin    = WorldToInstancePoint(Yaw, Origin);
        LocalDirection = WorldToInstanceVector(Yaw, Direction);
    }
    float3 Candidate = IntersectTriangle(LocalOrigin, LocalDirection, (Identifier - 1u) & TriangleIndexMask, UnboundedDistance);
    if (Candidate.x <= 0.0f)
    {
        return ConstructSurfaceHit(0.0f, HitCategoryAbsent, 0u, float2(0.0f, 0.0f));
    }
    if (Candidate.x < Hit.Distance)
    {
        Hit = ConstructSurfaceHit(Candidate.x, HitCategoryTriangle, Identifier - 1u, Candidate.yz);
    }
    return Hit;
}

uint ResolvePixelScramble(uint2 PixelCoordinate)
{
    if (Tracing.Realtime.x != 1u)
    {
        return 0u;
    }
    return AvalancheKey((PixelCoordinate.x * 0x8da6b343u) ^ (PixelCoordinate.y * 0xd8163841u) ^ 0x2545f491u) | 1u;
}

float3 IntegrateSample(uint2 PixelCoordinate, uint SampleIndex)
{
    float2 Extent   = float2(float(Tracing.Extent.x), float(Tracing.Extent.y));
    uint   ScaleInt = max(Tracing.Geometry.w, 1u);
    float  Scale    = float(ScaleInt);
    float2 Subpixel = QuadraturePair(SampleIndex, PairSlotSubpixel);
    gImagePosition  = float2(PixelCoordinate) * Scale + Subpixel * Scale;
    uint2  KeyPixel = (ScaleInt == 1u) ? PixelCoordinate : min(uint2(gImagePosition), PixelCoordinate * ScaleInt + uint2(ScaleInt - 1u, ScaleInt - 1u));
    gPixelScramble   = ResolvePixelScramble(KeyPixel);
    gBlueNoiseActive = (Tracing.Realtime.x == 2u);
    gBlueNoiseOrigin = KeyPixel;

    float2 Ndc       = (gImagePosition / Extent) * 2.0f - float2(1.0f, 1.0f);
    float3 Origin    = Tracing.CameraOrigin.xyz;
    float3 Direction = normalize(Tracing.CameraForward.xyz + Ndc.x * Tracing.CameraRight.xyz - Ndc.y * Tracing.CameraUp.xyz);

    if (Tracing.CameraOrigin.w > 0.0f)
    {
        float2 Lens       = ConcentricDisk(QuadraturePair(SampleIndex, PairSlotLens)) * Tracing.CameraOrigin.w;
        float3 FocusPoint = Origin + Direction * (Tracing.CameraForward.w / dot(Direction, Tracing.CameraForward.xyz));
        Origin            = Origin + normalize(Tracing.CameraRight.xyz) * Lens.x + normalize(Tracing.CameraUp.xyz) * Lens.y;
        Direction         = normalize(FocusPoint - Origin);
    }

    TransportPathRay CameraRay;
    CameraRay.Origin          = Origin;
    CameraRay.PackedCounters     = PackRayCounters(0u, 0u, InvalidMediumSlot);
    CameraRay.Direction       = Direction;
    CameraRay.LobePdf         = 0.0f;
    CameraRay.Throughput      = float3(1.0f, 1.0f, 1.0f);
    CameraRay.TravelSinceLobe = 0.0f;

    gTransportStack[0]           = CameraRay;
    gTransportStackSize = 1u;
    float3 Radiance     = float3(0.0f, 0.0f, 0.0f);

    SurfaceHitResult PrimaryHit     = ConstructSurfaceHit(0.0f, HitCategoryAbsent, 0u, float2(0.0f, 0.0f));
    bool             PrimaryPending = false;
    if (Tracing.Realtime.y == 1u && Tracing.CameraOrigin.w <= 0.0f)
    {
        PrimaryHit     = ResolveRasterizedPrimary(Origin, Direction, PixelCoordinate);
        PrimaryPending = (PrimaryHit.Distance > 0.0f);
    }

    [loop] while (gTransportStackSize > 0u)
    {
        if (gTransportStackSize == 0u)
        {
            break;
        }
        TransportPathRay Ray = PopTransportSlot();

        SurfaceHitResult Hit = PrimaryHit;
        if (PrimaryPending)
        {
            PrimaryPending = false;
        }
        else
        {
            Hit = TraceClosestHit(Ray.Origin, Ray.Direction, (RayDepth(Ray) > 0u) || (Tracing.Tuning.z > 0.5f));
        }

        uint Medium = RayMedium(Ray);
        if (Medium != InvalidMediumSlot)
        {
            float3 Absorption = Materials[Medium].AbsorptionIor.rgb;
            Ray.Throughput    = Ray.Throughput * exp(-Absorption * min(Hit.Distance, 100.0f));
        }

        if (Hit.Category == HitCategoryAbsent)
        {
            Radiance += Ray.Throughput * EvaluateStudioEnvironment(Ray.Direction);
            continue;
        }

        if (Hit.Category == HitCategoryLight)
        {
            StudioAreaLight Light       = FetchAreaLight(Hit.PrimitiveIndex);
            float           CosineLight = -dot(Ray.Direction, AreaLightNormal(Light));
            if (CosineLight > 0.0f)
            {
                float Travel   = Ray.TravelSinceLobe + Hit.Distance;
                float LightPdf = Travel * Travel / (CosineLight * Light.Centre.w);
                float Weight   = select(Ray.LobePdf > 0.0f, PowerHeuristic(Ray.LobePdf, LightPdf), 1.0f);
                Radiance += Ray.Throughput * Light.Radiance.rgb * Weight;
            }
            continue;
        }

        ResolvedSurfaceSample Surface = ResolveHitSurface(Ray.Origin, Ray.Direction, Hit);
        Radiance += ScatterSurfaceVertex(Surface, Ray, Hit.Distance, SampleIndex);
    }
    return Radiance;
}

float3 FilterFiniteRadiance(float3 Radiance)
{
    bool Finite = all(Radiance == Radiance) && all(abs(Radiance) < float3(1.0e20f, 1.0e20f, 1.0e20f));
    return select(bool3(Finite, Finite, Finite), Radiance, float3(0.0f, 0.0f, 0.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void IntegrateRadiance(uint3 InvocationId : SV_DispatchThreadID)
{
    uint  Scale = max(Tracing.Geometry.w, 1u);
    uint2 Tiles = (Tracing.Extent.xy + uint2(Scale - 1u, Scale - 1u)) / Scale;
    if (InvocationId.x >= Tiles.x || InvocationId.y >= Tiles.y)
    {
        return;
    }

    uint   PixelCount = Tracing.Extent.x * Tracing.Extent.y;
    bool   Direct     = (Scale == 1u);
    uint   TileIndex  = InvocationId.y * Tracing.Extent.x + InvocationId.x;
    float4 Sum        = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 Moments    = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (Direct)
    {
        Sum     = Accumulation[TileIndex];
        Moments = Accumulation[3u * PixelCount + TileIndex];
    }

    [loop] for (uint Offset = 0u; Offset < Tracing.Extent.w; ++Offset)
    {
        float3 Accepted  = FilterFiniteRadiance(IntegrateSample(InvocationId.xy, Tracing.Extent.z + Offset));
        float  Luminance = dot(Accepted, float3(0.2126f, 0.7152f, 0.0722f));
        if (Direct)
        {
            Sum     = Sum + float4(Accepted, 1.0f);
            Moments = Moments + float4(Luminance * Luminance, 0.0f, 0.0f, 0.0f);
            continue;
        }
        uint2 Pixel = min(uint2(gImagePosition), InvocationId.xy * Scale + uint2(Scale - 1u, Scale - 1u));
        if (Pixel.x >= Tracing.Extent.x || Pixel.y >= Tracing.Extent.y)
        {
            continue;
        }
        uint PixelIndex           = Pixel.y * Tracing.Extent.x + Pixel.x;
        uint MomentIndex          = 3u * PixelCount + PixelIndex;
        Accumulation[PixelIndex]  = Accumulation[PixelIndex] + float4(Accepted, 1.0f);
        Accumulation[MomentIndex] = Accumulation[MomentIndex] + float4(Luminance * Luminance, 0.0f, 0.0f, 0.0f);
    }

    if (Direct)
    {
        Accumulation[TileIndex]                  = Sum;
        Accumulation[3u * PixelCount + TileIndex] = Moments;
    }
}

float3 PixelCentreDirection(float2 Pixel, float3 Forward, float3 Right, float3 Up)
{
    float2 Extent = float2(float(Tracing.Extent.x), float(Tracing.Extent.y));
    float2 Ndc    = (Pixel + float2(0.5f, 0.5f)) / Extent * 2.0f - float2(1.0f, 1.0f);
    return normalize(Forward + Ndc.x * Right - Ndc.y * Up);
}

float ResolveSharpestRoughness(uint MaterialSlot)
{
    PathSurfaceMaterial Material  = Materials[MaterialSlot];
    uint                Category  = uint(Material.EtaCategory.w);
    float               Roughness = (Category == SurfaceCategorySolidGlass || Category == SurfaceCategoryThinSheet) ? 0.0f : Material.AlbedoRoughness.w;
    if (Material.KappaCoat.w > 0.0f)
    {
        Roughness = min(Roughness, Material.ExitanceCoat.w);
    }
    return Roughness;
}

float2 ProjectIntoPreviousCamera(float3 Point)
{
    float3 Offset  = Point - Tracing.PreviousOrigin.xyz;
    float  Depth   = dot(Offset, Tracing.PreviousForward.xyz);
    float  Across  = dot(Offset, Tracing.PreviousRight.xyz) / max(dot(Tracing.PreviousRight.xyz, Tracing.PreviousRight.xyz), 1.0e-12f);
    float  Upward  = dot(Offset, Tracing.PreviousUp.xyz) / max(dot(Tracing.PreviousUp.xyz, Tracing.PreviousUp.xyz), 1.0e-12f);
    float2 Extent  = float2(float(Tracing.Extent.x), float(Tracing.Extent.y));
    float2 Landing = (float2(Across / max(Depth, 1.0e-6f), -Upward / max(Depth, 1.0e-6f)) + float2(1.0f, 1.0f)) * 0.5f * Extent;
    if (Depth <= 0.0f || Landing.x < 0.0f || Landing.y < 0.0f || Landing.x >= Extent.x || Landing.y >= Extent.y)
    {
        return float2(-1.0f, -1.0f);
    }
    return Landing;
}

float2 ResolveMirrorIdentity(ResolvedSurfaceSample Surface, float3 Direction)
{
    float3           Reflected = reflect(Direction, Surface.ShadingNormal);
    SurfaceHitResult Hit       = TraceClosestHit(OffsetRayOrigin(Surface.Position, Surface.GeometricNormal, Reflected), Reflected, true);
    if (Hit.Category == HitCategoryAbsent)
    {
        return float2(0.0f, 1.0e4f);
    }
    float Identity = float(Hit.Category) * 4096.0f + float(Hit.PrimitiveIndex % 4096u);
    if (Hit.Category == HitCategoryTriangle || Hit.Category == HitCategorySphere || Hit.Category == HitCategoryFloor)
    {
        Identity = float(Hit.Category) * 4096.0f + float(ResolveHitSurface(Surface.Position, Reflected, Hit).MaterialSlot);
    }
    return float2(Identity, Hit.Distance);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ReprojectHistory(uint3 InvocationId : SV_DispatchThreadID)
{
    if (InvocationId.x >= Tracing.Extent.x || InvocationId.y >= Tracing.Extent.y)
    {
        return;
    }

    uint   PixelCount    = Tracing.Extent.x * Tracing.Extent.y;
    uint   PixelIndex    = InvocationId.y * Tracing.Extent.x + InvocationId.x;
    bool   LightsVisible = (Tracing.Tuning.z > 0.5f);
    float2 Extent        = float2(float(Tracing.Extent.x), float(Tracing.Extent.y));
    float  Footprint     = 2.0f * length(Tracing.CameraUp.xyz) / Extent.y;
    float4 Carried       = float4(0.0f, 0.0f, 0.0f, 0.0f);

    float3           Origin    = Tracing.CameraOrigin.xyz;
    float3           Direction = PixelCentreDirection(float2(InvocationId.xy), Tracing.CameraForward.xyz, Tracing.CameraRight.xyz, Tracing.CameraUp.xyz);
    SurfaceHitResult Hit       = TraceClosestHit(Origin, Direction, LightsVisible);
    float            Travel    = (Hit.Category == HitCategoryAbsent) ? 1.0e4f : Hit.Distance;
    float3           Point     = Origin + Direction * Travel;
    bool             MatValid  = (Hit.Category != HitCategoryAbsent) && (Hit.Category != HitCategoryLight);

    ResolvedSurfaceSample Surface;
    Surface.Position        = Point;
    Surface.GeometricNormal = -Direction;
    Surface.ShadingNormal   = -Direction;
    Surface.MaterialSlot    = 0u;
    Surface.FrontFacing     = true;

    float  Roughness = 0.0f;
    bool   Sharp     = false;
    float2 Mirror    = float2(0.0f, 0.0f);
    if (MatValid)
    {
        Surface   = ResolveHitSurface(Origin, Direction, Hit);
        Roughness = ResolveSharpestRoughness(Surface.MaterialSlot);
        Sharp     = (Roughness <= 0.05f);
        if (Sharp)
        {
            Mirror = ResolveMirrorIdentity(Surface, Direction);
        }
        uint Packed = (Surface.MaterialSlot & 0xffffu) | (uint(clamp(Roughness, 0.0f, 1.0f) * 65535.0f) << 16u);
        SurfaceFeatures[PixelIndex]              = float4(Surface.Position, Hit.Distance);
        SurfaceFeatures[PixelCount + PixelIndex] = float4(Surface.ShadingNormal, asfloat(Packed));
    }
    else if (Hit.Category == HitCategoryLight)
    {
        SurfaceFeatures[PixelIndex]              = float4(Point, Hit.Distance);
        SurfaceFeatures[PixelCount + PixelIndex] = float4(-Direction, asfloat(0xfffeu));
    }
    else
    {
        SurfaceFeatures[PixelIndex]              = float4(Point, 0.0f);
        SurfaceFeatures[PixelCount + PixelIndex] = float4(-Direction, asfloat(0xffffu));
    }

    float3 ReprojectPoint = Sharp ? (Origin + Direction * (Travel + Mirror.y)) : Point;
    float2 Landing        = ProjectIntoPreviousCamera(ReprojectPoint);
    if (Landing.x >= 0.0f)
    {
        uint2            PreviousPixel     = uint2(Landing);
        float3           PreviousOrigin    = Tracing.PreviousOrigin.xyz;
        float3           PreviousDirection = PixelCentreDirection(float2(PreviousPixel), Tracing.PreviousForward.xyz, Tracing.PreviousRight.xyz, Tracing.PreviousUp.xyz);
        SurfaceHitResult PreviousHit       = TraceClosestHit(PreviousOrigin, PreviousDirection, LightsVisible);
        bool             Accepted          = (PreviousHit.Category == Hit.Category);
        float            Capacity          = Tracing.HistoryTuning.x;

        if (Accepted && MatValid)
        {
            ResolvedSurfaceSample PreviousSurface = ResolveHitSurface(PreviousOrigin, PreviousDirection, PreviousHit);
            Accepted = (Surface.MaterialSlot == PreviousSurface.MaterialSlot) && (dot(Surface.GeometricNormal, PreviousSurface.GeometricNormal) > 0.9f);
            if (Sharp)
            {
                float2 PreviousMirror = ResolveMirrorIdentity(PreviousSurface, PreviousDirection);
                Accepted = Accepted && (PreviousMirror.x == Mirror.x) && (abs(PreviousMirror.y - Mirror.y) < 0.05f * Mirror.y + 0.05f);
                Capacity = 0.5f * Tracing.HistoryTuning.x;
            }
            else
            {
                float3 PreviousPoint = PreviousOrigin + PreviousDirection * PreviousHit.Distance;
                Accepted = Accepted && (distance(Point, PreviousPoint) < 2.0f * Footprint * max(Travel, PreviousHit.Distance) + 2.0e-3f);
                Capacity = Tracing.HistoryTuning.x * smoothstep(0.05f, 0.4f, Roughness);
            }
        }
        else if (Accepted && Hit.Category == HitCategoryLight)
        {
            float3 PreviousPoint = PreviousOrigin + PreviousDirection * PreviousHit.Distance;
            Accepted = (distance(Point, PreviousPoint) < 2.0f * Footprint * max(Travel, PreviousHit.Distance) + 2.0e-3f);
        }

        if (Accepted)
        {
            uint   PreviousIndex = PreviousPixel.y * Tracing.Extent.x + PreviousPixel.x;
            float4 Sum           = Accumulation[PreviousIndex];
            float4 History       = (Tracing.HistoryTuning.y > 0.5f) ? Accumulation[Tracing.Realtime.z + PreviousIndex] : float4(0.0f, 0.0f, 0.0f, 0.0f);
            float  HistoryWeight = max(History.w - Sum.w, 0.0f);
            float  Weight        = Sum.w + HistoryWeight;
            if (Weight > 0.0f)
            {
                Carried = float4((Sum.rgb + History.rgb * HistoryWeight) / Weight, min(Weight, Capacity));
            }
        }
    }
    Accumulation[Tracing.Realtime.w + PixelIndex] = Carried;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ResolveSurfaceFeatures(uint3 InvocationId : SV_DispatchThreadID)
{
    if (InvocationId.x >= Tracing.Extent.x || InvocationId.y >= Tracing.Extent.y)
    {
        return;
    }

    uint             PixelCount = Tracing.Extent.x * Tracing.Extent.y;
    uint             PixelIndex = InvocationId.y * Tracing.Extent.x + InvocationId.x;
    float3           Origin     = Tracing.CameraOrigin.xyz;
    float3           Direction  = PixelCentreDirection(float2(InvocationId.xy), Tracing.CameraForward.xyz, Tracing.CameraRight.xyz, Tracing.CameraUp.xyz);
    SurfaceHitResult Hit        = TraceClosestHit(Origin, Direction, Tracing.Tuning.z > 0.5f);
    float3           Position   = Origin + Direction * 1.0e4f;
    float            Travel     = 0.0f;
    float3           Normal     = -Direction;
    uint             Packed     = 0xffffu;

    if (Hit.Category == HitCategoryLight)
    {
        Position = Origin + Direction * Hit.Distance;
        Travel   = Hit.Distance;
        Packed   = 0xfffeu;
    }
    else if (Hit.Category != HitCategoryAbsent)
    {
        ResolvedSurfaceSample Surface = ResolveHitSurface(Origin, Direction, Hit);
        float Roughness               = clamp(ResolveSharpestRoughness(Surface.MaterialSlot), 0.0f, 1.0f);
        Position = Surface.Position;
        Travel   = Hit.Distance;
        Normal   = Surface.ShadingNormal;
        Packed   = (Surface.MaterialSlot & 0xffffu) | (uint(Roughness * 65535.0f) << 16u);
    }
    SurfaceFeatures[PixelIndex]              = float4(Position, Travel);
    SurfaceFeatures[PixelCount + PixelIndex] = float4(Normal, asfloat(Packed));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ExtractQuadrature(uint3 InvocationId : SV_DispatchThreadID)
{
    uint Total = Tracing.Extent.x * Tracing.Extent.y;
    if (InvocationId.x >= Total)
    {
        return;
    }
    gPixelScramble   = 0u;
    gBlueNoiseActive = false;
    gBlueNoiseOrigin = uint2(0u, 0u);
    uint   SampleIndex = InvocationId.x % Tracing.Extent.x;
    uint   PairIndex   = InvocationId.x / Tracing.Extent.x;
    float2 Point       = QuadraturePair(SampleIndex, PairIndex);
    Accumulation[InvocationId.x] = float4(Point, asfloat(SampleIndex), asfloat(PairIndex));
}
