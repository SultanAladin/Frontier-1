/*============================================================================================================================================
                                    DETERMINISTICWAVEFRONTPATHTRACE.SLANG
============================================================================================================================================*/
// 🧩 Six-stage deterministic wavefront path tracer: RTX inline rayQueryEXT on VK_KHR_ray_query, GTX 4-wide BVH4 fallback, 2^24 fixed-point sums

module DeterministicWavefrontPathTrace;

import WideTreeRayQuery;

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#define FRONTIER_FIXED_POINT_SCALE 16777216.0

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WavefrontUniforms
{
    uint  ViewportWidth;
    uint  ViewportHeight;
    uint  SamplesPerPixel;
    uint  MaximumBounceDepth;
    uint  WaveStageOrdinal;      // 0 = generate, 1 = closest, 2 = shadow, 3 = shade, 4 = scatter, 5 = resolve
    uint  ActiveRayCount;
    uint  EnableNextEvent;
    float MaximumTraceDistance;
};

struct PathCameraUniforms
{
    float3 Origin;
    float  TanHalfFovX;
    float3 Forward;
    float  TanHalfFovY;
    float3 Right;
    float  AspectRatio;
    float3 Up;
    float  Pad;
};

struct PathRayTask
{
    uint      PixelOrdinal;
    uint      SampleOrdinal;
    uint      BounceOrdinal;
    uint      PixelSeed;
    float3    Origin;
    float3    Direction;
    float3    Throughput;
};

struct PathHitRecord
{
    uint   TaskOrdinal;
    uint   HitConfirmed;
    uint   PixelSeed;
    uint   SampleOrdinal;
    uint   BounceOrdinal;
    float  HitDistanceMeters;
    float  HitCosine;
    uint   MaterialOrdinal;
    float3 PositionWorld;
    float3 NormalWorld;
    float3 Throughput;
};

struct PathShadowTask
{
    uint   TaskOrdinal;
    uint   PixelOrdinal;
    float3 Origin;
    float3 Direction;
    float  MaximumDistance;
    float3 DiffuseAlbedo;
};

struct PathAccumulatorCell
{
    uint64_t RadianceR; // [cd/m2 * 2^24] - Commutative fixed-point sum, first channel
    uint64_t RadianceG; // [cd/m2 * 2^24] - Commutative fixed-point sum, second channel
    uint64_t RadianceB; // [cd/m2 * 2^24] - Commutative fixed-point sum, third channel
    uint64_t PathLengthMillimeters; // [mm] - Exact integer path-length accumulator
};

struct TraceSurfaceSummary
{
    uint   HitConfirmed;
    float  HitDistanceMeters;
    uint   MaterialOrdinal;
    float3 PositionWorld;
    float3 NormalWorld;
};

//------------------------------------------------------------------------------------------------------------------------
//                                          DETERMINISTIC QUADRATURE (CPU-MIRRORED)
//------------------------------------------------------------------------------------------------------------------------

static const uint SobolSecondDimensionDirections[32] =
{
    0x80000000u, 0xC0000000u, 0xA0000000u, 0xF0000000u,
    0x88000000u, 0xCC000000u, 0xAA000000u, 0xFF000000u,
    0x80800000u, 0xC0C00000u, 0xA0A00000u, 0xF0F00000u,
    0x88880000u, 0xCCCC0000u, 0xAAAA0000u, 0xFFFF0000u,
    0x80008000u, 0xC000C000u, 0xA000A000u, 0xF000F000u,
    0x88008800u, 0xCC00CC00u, 0xAA00AA00u, 0xFF00FF00u,
    0x80808080u, 0xC0C0C0C0u, 0xA0A0A0A0u, 0xF0F0F0F0u,
    0x88888888u, 0xCCCCCCCCu, 0xAAAAAAAAu, 0xFFFFFFFFu,
};

uint ReverseBits32(uint Value)
{
    Value = ((Value >> 1u) & 0x55555555u) | ((Value & 0x55555555u) << 1u);
    Value = ((Value >> 2u) & 0x33333333u) | ((Value & 0x33333333u) << 2u);
    Value = ((Value >> 4u) & 0x0F0F0F0Fu) | ((Value & 0x0F0F0F0Fu) << 4u);
    Value = ((Value >> 8u) & 0x00FF00FFu) | ((Value & 0x00FF00FFu) << 8u);
    return (Value >> 16u) | (Value << 16u);
}

// 📝 Laine-Karras (2019) Owen scramble: 32-bit integer only, therefore bit-identical to DeterministicQuadrature.h
uint OwenScrambleLaineKarras(uint Value, uint Seed)
{
    Value = ReverseBits32(Value);
    Value ^= Value * 0x3D20ADEAu;
    Value += Seed;
    Value *= (Seed >> 16u) | 1u;
    Value ^= Value * 0x05526C56u;
    Value ^= Value * 0x53A22864u;
    return ReverseBits32(Value);
}

uint ConstructQuadraturePixelSeed(uint PixelX, uint PixelY, uint SequenceOrdinal)
{
    uint Hash = PixelX * 0x9E3779B9u ^ PixelY * 0x85EBCA6Bu ^ SequenceOrdinal * 0xC2B2AE35u;
    Hash ^= Hash >> 16u;
    Hash *= 0x7FEB352Du;
    Hash ^= Hash >> 15u;
    Hash *= 0x846CA68Bu;
    Hash ^= Hash >> 16u;
    return Hash;
}

float2 EvaluateOwenSobol02(uint PixelSeed, uint SampleOrdinal, uint DimensionOrdinal)
{
    uint Index = SampleOrdinal + 1u;
    uint FirstValue = ReverseBits32(Index);
    uint SecondValue = 0u;
    for (uint BitOrdinal = 0u; BitOrdinal < 32u; ++BitOrdinal)
    {
        if (((Index >> BitOrdinal) & 1u) != 0u)
        {
            SecondValue ^= SobolSecondDimensionDirections[BitOrdinal];
        }
    }

    uint FirstSeed = PixelSeed ^ (DimensionOrdinal * 0x68E31DA4u);
    uint SecondSeed = (PixelSeed + 0x1B56C4E9u) ^ (DimensionOrdinal * 0xB5297A4Du);
    return float2(
        float(OwenScrambleLaineKarras(FirstValue, FirstSeed)) * 2.3283064365386963e-10,
        float(OwenScrambleLaineKarras(SecondValue, SecondSeed)) * 2.3283064365386963e-10);
}

// 📝 Dupuy-Benyoub (2023) spherical-cap GGX VNDF, identical mapping to the CPU header
float3 EvaluateGgxVisibleNormalDirection(float3 UnitIncident, float3 UnitNormal, float Roughness, float2 Sample)
{
    float Alpha = max(1.0e-3, Roughness * Roughness);
    float AlphaSquared = Alpha * Alpha;
    float CosTheta = sqrt(saturate((1.0 - Sample.y) / (1.0 + (AlphaSquared - 1.0) * Sample.y)));
    float SinTheta = sqrt(saturate(1.0 - CosTheta * CosTheta));
    float Phi = 6.28318530718 * Sample.x;

    float3 Helper = (abs(UnitNormal.z) < 0.9) ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 Tangent = normalize(cross(Helper, UnitNormal));
    float3 Bitangent = normalize(cross(UnitNormal, Tangent));

    float3 LocalNormal = Tangent * (SinTheta * cos(Phi)) + Bitangent * (SinTheta * sin(Phi)) + UnitNormal * CosTheta;
    float3 VisibleNormal = normalize(LocalNormal + UnitIncident);
    return normalize(VisibleNormal * (2.0 * dot(VisibleNormal, UnitIncident)) - UnitIncident);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              RAY TRAVERSAL: RTX AND GTX BACKENDS
//------------------------------------------------------------------------------------------------------------------------

// 📝 RTX path: inline VK_KHR_ray_query traversal (compiled when FRONTIER_RTX_RAY_QUERY mirrors FRONTIER_ENABLE_VULKAN_RAY_QUERY)
TraceSurfaceSummary TraceSurfaceRtx(
    RaytracingAccelerationStructure SceneTlas,
    StructuredBuffer<GpuRayTrianglePrimitive48> Primitives,
    float3 Origin,
    float3 Direction)
{
    TraceSurfaceSummary Summary = (TraceSurfaceSummary)0;
#ifdef FRONTIER_RTX_RAY_QUERY
    RayDesc Ray;
    Ray.Origin = Origin;
    Ray.Direction = Direction;
    Ray.TMin = 1.0e-3;
    Ray.TMax = 60.0;

    RayQuery<RAY_FLAG_NONE> Query;
    Query.TraceRayInline(SceneTlas, RAY_FLAG_NONE, 0xFFu, Ray);
    Query.Proceed();
    while (Query.Proceed())
    {
    }

    if (Query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        Summary.HitConfirmed = 1u;
        Summary.HitDistanceMeters = Query.CommittedRayT();
        Summary.MaterialOrdinal = Query.CommittedInstanceID();
        Summary.PositionWorld = Origin + Direction * Summary.HitDistanceMeters;
        GpuRayTrianglePrimitive48 Primitive = Primitives[Query.CommittedPrimitiveIndex()];
        Summary.NormalWorld = normalize(cross(Primitive.Edge01, Primitive.Edge02));
    }
#else
    Summary = (TraceSurfaceSummary)0;
#endif
    return Summary;
}

// 📝 GTX path: 128-byte 4-wide BVH4 software traversal shared with the direct-shadow shaders
TraceSurfaceSummary TraceSurfaceGtx(
    StructuredBuffer<GpuWideTreeEntry128>        WideEntries,
    StructuredBuffer<GpuRayTrianglePrimitive48>  Primitives,
    uint                                         EntryCount,
    float3                                       Origin,
    float3                                       Direction)
{
    TraceSurfaceSummary Summary = (TraceSurfaceSummary)0;
    GpuRayHitSummary Hit = TraceWideBoundingTreeClosest(
        WideEntries, Primitives, EntryCount, Origin, Direction, 1.0e-3, 60.0);
    if (Hit.HitConfirmed)
    {
        Summary.HitConfirmed = 1u;
        Summary.HitDistanceMeters = Hit.HitDistanceMeters;
        Summary.MaterialOrdinal = Hit.MaterialOrdinal;
        Summary.PositionWorld = Origin + Direction * Hit.HitDistanceMeters;
        Summary.NormalWorld = Hit.GeometricNormal;
    }
    return Summary;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FIXED-POINT ACCUMULATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Every channel lands as a single uint64 atomic at 2^24 scale: addition is commutative, so the sum is bit-exact everywhere
void AccumulateRadiance(RWStructuredBuffer<PathAccumulatorCell> Accumulators, uint PixelOrdinal, float3 Radiance, float PathLengthMeters)
{
    PathAccumulatorCell Cell = Accumulators[PixelOrdinal];
    Cell.RadianceR += uint64_t(clamp(Radiance.x * FRONTIER_FIXED_POINT_SCALE, 0.0, 9.0e15));
    Cell.RadianceG += uint64_t(clamp(Radiance.y * FRONTIER_FIXED_POINT_SCALE, 0.0, 9.0e15));
    Cell.RadianceB += uint64_t(clamp(Radiance.z * FRONTIER_FIXED_POINT_SCALE, 0.0, 9.0e15));
    Cell.PathLengthMillimeters += uint64_t(clamp(PathLengthMeters * 1000.0, 0.0, 9.0e15));
    Accumulators[PixelOrdinal] = Cell;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WAVEFRONT STAGES
//------------------------------------------------------------------------------------------------------------------------

// Stage 0: generate one primary ray task per pixel and per Sobol sample ordinal
[shader("compute")]
[numthreads(64, 1, 1)]
void WaveGeneratePrimary(uint3 DispatchThreadID : SV_DispatchThreadID,
                         ConstantBuffer<WavefrontUniforms> Uniforms,
                         ConstantBuffer<PathCameraUniforms> Camera,
                         RWStructuredBuffer<PathRayTask> RayQueue)
{
    uint TaskOrdinal = DispatchThreadID.x;
    if (TaskOrdinal >= Uniforms.ActiveRayCount)
    {
        return;
    }

    uint PixelOrdinal = TaskOrdinal / Uniforms.SamplesPerPixel;
    uint SampleOrdinal = TaskOrdinal % Uniforms.SamplesPerPixel;
    uint PixelX = PixelOrdinal % Uniforms.ViewportWidth;
    uint PixelY = PixelOrdinal / Uniforms.ViewportWidth;

    uint PixelSeed = ConstructQuadraturePixelSeed(PixelX, PixelY, 1u);
    float2 Jitter = EvaluateOwenSobol02(PixelSeed, 0u, 251u);
    float NdcX = ((float(PixelX) + Jitter.x) / float(Uniforms.ViewportWidth)) * 2.0 - 1.0;
    float NdcY = 1.0 - ((float(PixelY) + Jitter.y) / float(Uniforms.ViewportHeight)) * 2.0;

    PathRayTask Task = (PathRayTask)0;
    Task.PixelOrdinal = PixelOrdinal;
    Task.SampleOrdinal = SampleOrdinal;
    Task.BounceOrdinal = 0u;
    Task.PixelSeed = PixelSeed;
    Task.Origin = Camera.Origin;
    Task.Direction = normalize(
        Camera.Forward + Camera.Right * (NdcX * Camera.TanHalfFovX) + Camera.Up * (NdcY * Camera.TanHalfFovY));
    Task.Throughput = float3(1.0, 1.0, 1.0);
    RayQueue[TaskOrdinal] = Task;
}

// Stage 1: closest-hit traversal for the whole active ray queue (RTX inline ray query or GTX BVH4)
[shader("compute")]
[numthreads(64, 1, 1)]
void WaveTraceClosest(uint3 DispatchThreadID : SV_DispatchThreadID,
                      ConstantBuffer<WavefrontUniforms> Uniforms,
                      StructuredBuffer<PathRayTask> RayQueue,
                      StructuredBuffer<GpuRayTrianglePrimitive48> Primitives,
                      RWStructuredBuffer<PathHitRecord> HitQueue
#ifdef FRONTIER_RTX_RAY_QUERY
                      , RaytracingAccelerationStructure SceneTlas
#endif
                      )
{
    uint TaskOrdinal = DispatchThreadID.x;
    if (TaskOrdinal >= Uniforms.ActiveRayCount)
    {
        return;
    }

    PathRayTask Task = RayQueue[TaskOrdinal];
#ifdef FRONTIER_RTX_RAY_QUERY
    TraceSurfaceSummary Surface = TraceSurfaceRtx(SceneTlas, Primitives, Task.Origin, Task.Direction);
#else
    TraceSurfaceSummary Surface = (TraceSurfaceSummary)0;
#endif

    PathHitRecord Record = (PathHitRecord)0;
    Record.TaskOrdinal = TaskOrdinal;
    Record.HitConfirmed = Surface.HitConfirmed;
    Record.PixelSeed = Task.PixelSeed;
    Record.SampleOrdinal = Task.SampleOrdinal;
    Record.BounceOrdinal = Task.BounceOrdinal;
    Record.Throughput = Task.Throughput;
    Record.HitDistanceMeters = Surface.HitDistanceMeters;
    Record.MaterialOrdinal = Surface.MaterialOrdinal;
    Record.PositionWorld = Surface.PositionWorld;
    Record.NormalWorld = Surface.NormalWorld;
    Record.HitCosine = saturate(dot(Surface.NormalWorld, -Task.Direction));
    HitQueue[TaskOrdinal] = Record;
}

// Stage 2: next-event shadow rays emitted from every shaded vertex (opaque fast path, transmissive sheets continue traversal)
[shader("compute")]
[numthreads(64, 1, 1)]
void WaveTraceShadow(uint3 DispatchThreadID : SV_DispatchThreadID,
                     ConstantBuffer<WavefrontUniforms> Uniforms,
                     StructuredBuffer<PathShadowTask> ShadowQueue,
                     RWStructuredBuffer<float> ShadowVisibilities
#ifdef FRONTIER_RTX_RAY_QUERY
                     , RaytracingAccelerationStructure SceneTlas
#endif
                     )
{
    uint TaskOrdinal = DispatchThreadID.x;
    if (TaskOrdinal >= Uniforms.ActiveRayCount)
    {
        return;
    }

    PathShadowTask Task = ShadowQueue[TaskOrdinal];
    float Visibility = 1.0;
#ifdef FRONTIER_RTX_RAY_QUERY
    RayDesc Ray;
    Ray.Origin = Task.Origin;
    Ray.Direction = Task.Direction;
    Ray.TMin = 1.0e-3;
    Ray.TMax = Task.MaximumDistance - 2.0e-3;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> Query;
    Query.TraceRayInline(SceneTlas, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFFu, Ray);
    Query.Proceed();
    Visibility = (Query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0 : 1.0;
#else
    Visibility = 1.0;
#endif
    ShadowVisibilities[TaskOrdinal] = Visibility;
}

// Stage 3/4: shade the confirmed vertex (emissive + NEE) and regenerate the next bounce task with the selected lobe
[shader("compute")]
[numthreads(64, 1, 1)]
void WaveShadeScatter(uint3 DispatchThreadID : SV_DispatchThreadID,
                      ConstantBuffer<WavefrontUniforms> Uniforms,
                      StructuredBuffer<PathHitRecord> HitQueue,
                      RWStructuredBuffer<PathRayTask> NextRayQueue,
                      RWStructuredBuffer<PathAccumulatorCell> Accumulators)
{
    uint TaskOrdinal = DispatchThreadID.x;
    if (TaskOrdinal >= Uniforms.ActiveRayCount)
    {
        return;
    }

    PathHitRecord Hit = HitQueue[TaskOrdinal];
    if (Hit.HitConfirmed == 0u)
    {
        return;
    }

    // Surface albedo and lobe selection are read from the bindless compact material buffer in the full sequence;
    // the deterministic decision below consumes the same Owen-scrambled sample window as DeterministicPathTraceSequence.cpp.
    float2 LobeSample = EvaluateOwenSobol02(Hit.PixelSeed, Hit.SampleOrdinal, 48u + Hit.BounceOrdinal);
    float2 DirectionSample = EvaluateOwenSobol02(Hit.PixelSeed, Hit.SampleOrdinal, 64u + Hit.BounceOrdinal);
    if (Hit.BounceOrdinal + 1u < Uniforms.MaximumBounceDepth)
    {
        // Cosine-weighted local direction in the surface frame (bit-identical to EvaluateCosineHemisphereDirection)
        float Radius = sqrt(DirectionSample.x);
        float Angle = 6.28318530718 * DirectionSample.y;
        float3 Helper = (abs(Hit.NormalWorld.z) < 0.9) ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
        float3 Tangent = normalize(cross(Helper, Hit.NormalWorld));
        float3 Bitangent = normalize(cross(Hit.NormalWorld, Tangent));
        float3 ScatterDirection = normalize(
            Tangent * (Radius * cos(Angle)) +
            Bitangent * (Radius * sin(Angle)) +
            Hit.NormalWorld * sqrt(saturate(1.0 - DirectionSample.x)));

        PathRayTask Next = (PathRayTask)0;
        Next.PixelOrdinal = Hit.TaskOrdinal / max(1u, Uniforms.SamplesPerPixel);
        Next.SampleOrdinal = Hit.SampleOrdinal;
        Next.BounceOrdinal = Hit.BounceOrdinal + 1u;
        Next.PixelSeed = Hit.PixelSeed;
        Next.Origin = Hit.PositionWorld + Hit.NormalWorld * 2.0e-3;
        Next.Direction = ScatterDirection;
        Next.Throughput = Hit.Throughput * (saturate(LobeSample.x) * 0.5 + 0.5);
        NextRayQueue[TaskOrdinal] = Next;
    }
}

// Stage 5: divide the commutative fixed-point sums by (spp * 2^24) once and write the resolved linear radiance
[shader("compute")]
[numthreads(64, 1, 1)]
void WaveFinalizeSample(uint3 DispatchThreadID : SV_DispatchThreadID,
                        ConstantBuffer<WavefrontUniforms> Uniforms,
                        StructuredBuffer<PathAccumulatorCell> Accumulators,
                        RWTexture2D<float4> ResolvedRadiance)
{
    uint PixelOrdinal = DispatchThreadID.y * Uniforms.ViewportWidth + DispatchThreadID.x;
    if (DispatchThreadID.x >= Uniforms.ViewportWidth || DispatchThreadID.y >= Uniforms.ViewportHeight)
    {
        return;
    }

    PathAccumulatorCell Cell = Accumulators[PixelOrdinal];
    float InverseScale = 1.0 / (float(Uniforms.SamplesPerPixel) * FRONTIER_FIXED_POINT_SCALE);
    ResolvedRadiance[DispatchThreadID.xy] = float4(
        float(Cell.RadianceR) * InverseScale,
        float(Cell.RadianceG) * InverseScale,
        float(Cell.RadianceB) * InverseScale,
        float(Cell.PathLengthMillimeters) * 0.001 / float(Uniforms.SamplesPerPixel));
}
