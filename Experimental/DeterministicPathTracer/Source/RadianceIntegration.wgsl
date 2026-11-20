/*====================================================================================================================================
                                                    RADIANCEINTEGRATION.WGSL
====================================================================================================================================*/
// 🧩 Deterministic quadrature path tracer: bounds-tree ray casting, lobe and Fresnel splitting, light quadrature with MIS

// 📝 Determinism contract
//    ① No random number generator exists in this file. Every integral (pixel footprint, lens, light area, BSDF lobe)
//       is evaluated with one fixed, shared low-discrepancy point set: base-2 Sobol' (0,2)-sequence pairs, each pair
//       decorrelated by a FIXED nested-uniform digit permutation keyed by the pair index. By default every pixel uses
//       the same points; the optional per-pixel mode adds a fixed key derived from the pixel coordinate. Either way
//       sample k is identical on every run.
//    ② No Russian roulette and no stochastic lobe or Fresnel selection. At each surface vertex every lobe is traced
//       (coat, specular, diffuse; reflection AND refraction for glass), each carrying its exact weight.
//    ③ Paths end only at fixed depth limits or below a fixed throughput cutoff — deterministic, documented bias.
//    ④ Each sample is added to the accumulation in index order, so the sum is bit-identical regardless of how many
//       samples a dispatch carries.

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const π                       : f32 = 3.14159265358979;
const InvalidMedium           : u32 = 0xFFu;       // [-]  - Packed in bits 16..23 of TransportRay.PackedState (material count << 255)
const TransportStackCapacity  : u32 = 16u;
const SceneStackCapacity      : u32 = 40u;         // [-]  - Direct-descent pushes <= 3 per level (max tree depth <= 15)
const ReturnMarker            : u32 = 0xFFFFFFFFu; // [-]  - Stack entry that restores the world-space ray
const EmptyLink               : u32 = 0xFFFFFFFFu;
const InstanceShift           : u32 = 20u;         // [-]  - SurfaceHit.PrimitiveIndex = instance << 20 | triangle
const TriangleMask            : u32 = 0xFFFFFu;
const ThinDielectricBit       : u32 = 0x80000000u; // [-]  - Bit 31 of TrianglePositions[t*3].w flags thin glass for 1-cycle opaque shadow early-out
const MaterialSlotMask        : u32 = 0x7FFFFFFFu;
const InstanceStride          : u32 = 2u;          // [vec4u] per instance record: (Cosine, Sine, Tx, Tz) + (root, paint, rim)
const UnusedMaterialSlot      : u32 = 0xFFFFu;     // [-]  - Sentinel for unchanged material slot in 16-bit packed swap words
const DeltaRoughnessAlpha     : f32 = 0.0025;     // [-]  - α below this traces the exact mirror / refraction direction
const RayOffsetScale          : f32 = 2.0e-4;     // [m]  - Self-intersection offset along the geometric normal
const UnboundedDistance       : f32 = 1.0e30;     // [m]

const SurfaceLayered          : u32 = 0u;
const SurfaceConductor        : u32 = 1u;
const SurfaceSolidDielectric  : u32 = 2u;
const SurfaceThinDielectric   : u32 = 3u;

const HitAbsent               : u32 = 0u;
const HitTriangle             : u32 = 1u;
const HitSphere               : u32 = 2u;
const HitFloor                : u32 = 3u;
const HitLight                : u32 = 4u;

const PairSubpixel            : u32 = 0u;
const PairLens                : u32 = 1u;
const PairsPerDepth           : u32 = 4u;          // light point, diffuse lobe, specular lobe, coat lobe
const PairLight               : u32 = 0u;
const PairDiffuse             : u32 = 1u;
const PairSpecular            : u32 = 2u;
const PairCoat                : u32 = 3u;
const PairPracticalBase       : u32 = 66u;         // [-]  - Lamp selection at depth d uses pair 66 + d (after the 2 + 16 × 4 above)
const LampLeafFlag            : u32 = 0x80000000u; // [-]  - Light-tree link bit: leaf, low bits = lamp index
const ProfileVerticalSamples  : u32 = 128u;        // [-]  - PhotometricProfile.js grid: θ ∈ [0, π] endpoints included …
const ProfileHorizontalSamples : u32 = 32u;        // [-]  - … φ ∈ [0, 2π) wrapping
const SelectionCeiling        : f32 = 0.99999994;  // [-]  - Largest float below 1
const LampCutCapacity         : u32 = 8u;          // [-]  - Most light-tree subtrees sampled at one vertex (adaptive splitting)
const LampSplitRatio          : f32 = 0.05;        // [-]  - Split a node while its radius² exceeds this share of its distance²
const GoldenFraction          : f32 = 0.618034;    // [-]  - Fixed rotation decorrelating the steering value between cut nodes


//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct TracingUniforms
{
    CameraOrigin      : vec4f,   // xyz [m] origin, w [m] lens radius
    CameraForward     : vec4f,   // xyz unit forward, w [m] focus distance
    CameraRight       : vec4f,   // xyz right × tan(fov/2) × aspect
    CameraUp          : vec4f,   // xyz up × tan(fov/2)
    EnvironmentTint   : vec4f,   // rgb tint, w intensity
    Extent            : vec4u,   // width, height, first sample index, samples in this dispatch
    DepthLimits       : vec4u,   // non-delta bounce limit, total depth limit, light count, sphere count
    SceneLayout       : vec4u,   // floor material slot, environment category, entry count, vehicle enabled (0/1)
    Tuning            : vec4f,   // throughput cutoff, floor radius [m], softboxes visible to camera (0/1), unused
    PreviousOrigin    : vec4f,   // xyz [m] camera of the accumulation being reprojected
    PreviousForward   : vec4f,
    PreviousRight     : vec4f,
    PreviousUp        : vec4f,
    Realtime          : vec4u,   // per-pixel decorrelation (0/1), visibility raster (0/1), history read / write offset [px]
    HistoryTuning     : vec4f,   // history capacity [samples], history active (0/1), unused, unused
    Geometry          : vec4u,   // instance tree root node, first instance record [vec4u], instance count, trace scale [px]
    Shading           : vec4f,   // light cutoff (0 = off), analytic area lights (0/1), practical lamps: all (1) or tree (0), tree cut limit
    Practical         : vec4u,   // lamp count (0 = none), tree node base, lamp record base, profile base [vec4f in LightRecords]
    SphereBounds      : vec4f,   // xyz [m] bounding sphere centre of showcase spheres, w [m] radius
}

// 📝 One 4-wide node = 4 vec4u (BoundingVolumeConstruction.js EncodeWideNode): origin + exponents, 8-bit child bounds,
//    16-bit child primitive counts (0 = inner), child links. Decoded child boxes contain the exact ones.
struct WideVisit
{
    Distances         : vec4f,   // entry distance per child, UnboundedDistance = missed or empty
    Counts            : vec4u,   // primitives per leaf child, 0 = inner child
    Links             : vec4u,   // inner: node index; leaf: first primitive
}

// 📝 Closest-hit stack entry: x = link << 4 | primitive count (0 = inner node; leaves hold ≤ 15), y = entry distance bits.
struct SortedChildren
{
    Distances         : vec4f,
    Entries           : vec4u,
}

struct SurfaceMaterial
{
    AlbedoRoughness   : vec4f,   // rgb albedo / tint, w perceptual roughness
    EtaCategory       : vec4f,   // rgb conductor η, w category
    KappaCoat         : vec4f,   // rgb conductor κ, w coat weight
    ExitanceCoat      : vec4f,   // rgb exitance radiance, w coat roughness
    AbsorptionIor     : vec4f,   // rgb σa [1/m], w index of refraction
    SpecularLevel     : vec4f,   // x dielectric F0
}

struct AreaLight
{
    Centre            : vec4f,   // xyz [m], w area [m²]
    EdgeU             : vec4f,
    EdgeV             : vec4f,
    Radiance          : vec4f,
}

struct SurfaceHit
{
    Distance          : f32,
    Category          : u32,
    PrimitiveIndex    : u32,
    Barycentric       : vec2f,
}

struct SurfaceSample
{
    Position          : vec3f,
    GeometricNormal   : vec3f,   // faces the incoming ray
    ShadingNormal     : vec3f,   // faces the incoming ray
    MaterialSlot      : u32,
    FrontFacing       : bool,
}

struct TransportRay
{
    Origin            : vec3f,
    PackedState       : u32,     // bits 0..7 = Depth, bits 8..15 = NonDeltaDepth, bits 16..23 = Medium (0xFF = air)
    Direction         : vec3f,
    LobePdf           : f32,     // solid-angle pdf of the generating lobe; 0 = camera or delta interaction
    Throughput        : vec3f,
    TravelSinceLobe   : f32,     // [m] distance carried through straight thin-sheet transmissions since the lobe vertex
}

fn PackRayState(Depth: u32, NonDeltaDepth: u32, Medium: u32) -> u32
{
    return (Depth & 0xFFu) | ((NonDeltaDepth & 0xFFu) << 8u) | ((Medium & 0xFFu) << 16u);
}

fn RayDepth(Ray: TransportRay) -> u32         { return Ray.PackedState & 0xFFu; }
fn RayNonDeltaDepth(Ray: TransportRay) -> u32 { return (Ray.PackedState >> 8u) & 0xFFu; }
fn RayMedium(Ray: TransportRay) -> u32        { return (Ray.PackedState >> 16u) & 0xFFu; }

struct LobeEvaluation
{
    Diffuse           : vec3f,   // f × cosθi
    DiffusePdf        : f32,
    Specular          : vec3f,
    SpecularPdf       : f32,
    Coat              : vec3f,
    CoatPdf           : f32,
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    RESOURCES
//------------------------------------------------------------------------------------------------------------------------

@group(0) @binding(0) var<uniform>             Tracing           : TracingUniforms;
@group(0) @binding(1) var<storage, read>       BoundsEntries     : array<vec4u>;   // vehicle tree nodes, instance tree nodes, instance records
@group(0) @binding(2) var<storage, read>       TrianglePositions : array<vec4f>;
@group(0) @binding(3) var<storage, read>       TriangleNormals   : array<vec4f>;
@group(0) @binding(4) var<storage, read>       Materials         : array<SurfaceMaterial>;
@group(0) @binding(5) var<storage, read>       LightRecords      : array<vec4f>;   // area lights, lamps, light tree, IES tables (PracticalLighting.js)
@group(0) @binding(6) var<storage, read>       Spheres           : array<vec4f>;
@group(0) @binding(7) var<storage, read_write> Accumulation      : array<vec4f>;   // [0,N) sums, [N,3N) two history regions, [3N,4N) luminance moments
@group(0) @binding(8) var                      VisibilityIds     : texture_2d<u32>; // triangle index + 1 per pixel, 0 = none
@group(0) @binding(9) var                      BlueNoiseRanks    : texture_2d<u32>; // 64² r32uint: rank A | rank B << 16
@group(0) @binding(10) var<storage, read_write> SurfaceFeatures  : array<vec4f>;   // ResolveSurfaceFeatures only: [0,N) position + distance, [N,2N) normal + slot/roughness

var<private> TransportStack     : array<TransportRay, TransportStackCapacity>;
var<private> TransportStackSize : u32;
var<private> PixelScramble      : u32;
var<private> BlueNoiseOrigin    : vec2u;   // pixel coordinate when the blue-noise pattern is active
var<private> BlueNoiseActive    : bool;

// 📝 Ray statistics (profiling pipeline only): closest rays, shadow rays, child box tests, triangle tests per sample.
//    A pipeline-overridable constant, so the normal pipeline compiles these counters away.
override RayStatisticsEnabled   : bool = false;
var<private> RayStatistics      : vec4u;
var<private> ImagePosition      : vec2f;   // display-pixel position of the current sample (trace scale)
var<private> LampCut            : array<u32, LampCutCapacity>;   // light-tree cut of the current vertex


//------------------------------------------------------------------------------------------------------------------------
//                                                    QUADRATURE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Blue-noise pattern: each pair reads the two 64² void-and-cluster masks at a fixed per-pair tile offset (integer R₂
//    steps), so neighbouring pixels get shifts far apart in value and different pairs see uncorrelated tiles. The 12
//    rank bits become the top digits of a digital shift — a fixed XOR, so each pixel still owns a full (0,m,2)-net.
fn ResolveBlueNoiseShift(PairIndex: u32) -> vec2u
{
    let Offset = vec2u((PairIndex * 3242174890u) >> 26u, (PairIndex * 2447445413u) >> 26u);
    let Texel  = vec2i(vec2u(BlueNoiseOrigin + Offset) & vec2u(63u));
    let Ranks  = textureLoad(BlueNoiseRanks, Texel, 0).x;
    return vec2u((Ranks & 0xfffu) << 20u, ((Ranks >> 16u) & 0xfffu) << 20u);
}

// 📝 QuadratureSequence.wgsl is prepended to this source. PixelScramble is 0 (shared point set) unless the per-pixel
//    pattern is selected; the subpixel pair always stays shared so the visibility raster can match it.
fn QuadraturePair(SampleIndex: u32, PairIndex: u32) -> vec2f
{
    let Keyed = PairIndex != PairSubpixel;
    var Shift = vec2u(0u);
    if (BlueNoiseActive && Keyed) { Shift = ResolveBlueNoiseShift(PairIndex); }
    return ScrambledQuadraturePair(SampleIndex, PairIndex, select(0u, PixelScramble, Keyed), Shift);
}

fn DepthPair(SampleIndex: u32, Depth: u32, Slot: u32) -> vec2f
{
    return QuadraturePair(SampleIndex, 2u + min(Depth, 15u) * PairsPerDepth + Slot);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    GEOMETRY QUERIES
//------------------------------------------------------------------------------------------------------------------------

fn SafeReciprocal(Direction: vec3f) -> vec3f
{
    let Magnitude = max(abs(Direction), vec3f(1.0e-12));
    return select(vec3f(1.0), vec3f(-1.0), Direction < vec3f(0.0)) / Magnitude;
}

fn ExtractBytes(Word: u32) -> vec4f
{
    return vec4f(vec4u(Word, Word >> 8u, Word >> 16u, Word >> 24u) & vec4u(255u));
}

fn IntersectWideNode(NodeIndex: u32, Origin: vec3f, Reciprocal: vec3f, MaximumDistance: f32) -> WideVisit
{
    let Base       = NodeIndex * 4u;
    let Header     = BoundsEntries[Base];
    let BoundsXY   = BoundsEntries[Base + 1u];
    let BoundsZ    = BoundsEntries[Base + 2u];
    let Links      = BoundsEntries[Base + 3u];
    let NodeOrigin = bitcast<vec3f>(Header.xyz);
    // 📝 Biased exponent byte E → 2^(E − 127) built directly from its float bits (exact power of 2).
    //    Factoring Offset and ScaledRecip turns 24 sub+mul + 12 min/max swaps into 6 FMA slab tests with zero swaps.
    let Scale       = bitcast<vec3f>(vec3u(Header.w & 255u, (Header.w >> 8u) & 255u, (Header.w >> 16u) & 255u) << vec3u(23u));
    let Offset      = (NodeOrigin - Origin) * Reciprocal;
    let ScaledRecip = Scale * Reciprocal;
    let NegX        = Reciprocal.x < 0.0;
    let NegY        = Reciprocal.y < 0.0;
    let NegZ        = Reciprocal.z < 0.0;
    let MinX = ExtractBytes(select(BoundsXY.x, BoundsXY.y, NegX)) * ScaledRecip.x + Offset.x;
    let MaxX = ExtractBytes(select(BoundsXY.y, BoundsXY.x, NegX)) * ScaledRecip.x + Offset.x;
    let MinY = ExtractBytes(select(BoundsXY.z, BoundsXY.w, NegY)) * ScaledRecip.y + Offset.y;
    let MaxY = ExtractBytes(select(BoundsXY.w, BoundsXY.z, NegY)) * ScaledRecip.y + Offset.y;
    let MinZ = ExtractBytes(select(BoundsZ.x,  BoundsZ.y,  NegZ)) * ScaledRecip.z + Offset.z;
    let MaxZ = ExtractBytes(select(BoundsZ.y,  BoundsZ.x,  NegZ)) * ScaledRecip.z + Offset.z;
    let Entered = max(max(MinX, MinY), max(MinZ, vec4f(0.0)));
    let Exited  = min(min(MaxX, MaxY), min(MaxZ, vec4f(MaximumDistance)));
    let Present = Links != vec4u(EmptyLink);
    if (RayStatisticsEnabled) { RayStatistics.z += u32(Present.x) + u32(Present.y) + u32(Present.z) + u32(Present.w); }
    let Counts = vec4u(BoundsZ.z & 0xFFFFu, BoundsZ.z >> 16u, BoundsZ.w & 0xFFFFu, BoundsZ.w >> 16u);
    return WideVisit(select(vec4f(UnboundedDistance), Entered, Present & (Entered <= Exited * 1.000001)), Counts, Links);
}

// 📝 Compact rigid 2D-yaw instance record (2 vec4u = 32 B):
//    row 0 (vec4f): (Cosine, Sine, Tx, Tz) — yaw about +Y then XZ translation (bay 0 of 1 car is exact identity).
//    row 1 (vec4u): x = vehicle tree root, y = BodySlot | (PaintSwap << 16), z = RimSlot | (RimSwap << 16).
fn FetchInstanceYaw(Instance: u32) -> vec4f
{
    return bitcast<vec4f>(BoundsEntries[Tracing.Geometry.y + Instance * InstanceStride]);
}

fn WorldToObjectPoint(Yaw: vec4f, Point: vec3f) -> vec3f
{
    let Dx = Point.x - Yaw.z;
    let Dz = Point.z - Yaw.w;
    return vec3f(Yaw.x * Dx - Yaw.y * Dz, Point.y, Yaw.y * Dx + Yaw.x * Dz);
}

fn WorldToObjectVector(Yaw: vec4f, Vector: vec3f) -> vec3f
{
    return vec3f(Yaw.x * Vector.x - Yaw.y * Vector.z, Vector.y, Yaw.y * Vector.x + Yaw.x * Vector.z);
}

fn ObjectToWorldVector(Yaw: vec4f, Vector: vec3f) -> vec3f
{
    return vec3f(Yaw.x * Vector.x + Yaw.y * Vector.z, Vector.y, -Yaw.y * Vector.x + Yaw.x * Vector.z);
}

fn InstanceMaterialSlot(Instance: u32, TriangleSlot: u32) -> u32
{
    let Meta      = BoundsEntries[Tracing.Geometry.y + Instance * InstanceStride + 1u];
    let PaintSwap = Meta.y >> 16u;
    var Slot      = select(TriangleSlot, PaintSwap, PaintSwap != UnusedMaterialSlot && TriangleSlot == (Meta.y & 0xFFFFu));
    let RimSwap   = Meta.z >> 16u;
    if (RimSwap != UnusedMaterialSlot && TriangleSlot == (Meta.z & 0xFFFFu)) { Slot = RimSwap; }
    return Slot;
}

// 📝 Möller–Trumbore against precomputed edges with deferred reciprocal: misses and out-of-range hits (95%+ of calls)
//    pay zero SFU divisions; the single 1/AbsDet executes only on a genuine closer hit.
fn IntersectTriangle(Origin: vec3f, Direction: vec3f, TriangleIndex: u32, MaxDistance: f32) -> vec3f
{
    if (RayStatisticsEnabled) { RayStatistics.w += 1u; }
    let Corner      = TrianglePositions[TriangleIndex * 3u].xyz;
    let EdgeA       = TrianglePositions[TriangleIndex * 3u + 1u].xyz;
    let EdgeB       = TrianglePositions[TriangleIndex * 3u + 2u].xyz;
    let PVector     = cross(Direction, EdgeB);
    let Determinant = dot(EdgeA, PVector);
    if (abs(Determinant) < 1.0e-12) { return vec3f(-1.0); }
    let Sign        = select(-1.0, 1.0, Determinant > 0.0);
    let AbsDet      = Determinant * Sign;
    let TVector     = Origin - Corner;
    let UDet        = dot(TVector, PVector) * Sign;
    if (UDet < 0.0 || UDet > AbsDet) { return vec3f(-1.0); }
    let QVector     = cross(TVector, EdgeA);
    let VDet        = dot(Direction, QVector) * Sign;
    if (VDet < 0.0 || UDet + VDet > AbsDet) { return vec3f(-1.0); }
    let TDet        = dot(EdgeB, QVector) * Sign;
    if (TDet <= RayOffsetScale * AbsDet || TDet >= MaxDistance * AbsDet) { return vec3f(-1.0); }
    let InvAbsDet   = 1.0 / AbsDet;
    return vec3f(TDet * InvAbsDet, UDet * InvAbsDet, VDet * InvAbsDet);
}

// 📝 Shadow-ray triangle test: checks RayOffsetScale < t < MaxDistance with ZERO divisions even on hits.
fn IntersectTriangleShadow(Origin: vec3f, Direction: vec3f, TriangleIndex: u32, MaxDistance: f32) -> bool
{
    if (RayStatisticsEnabled) { RayStatistics.w += 1u; }
    let Corner      = TrianglePositions[TriangleIndex * 3u].xyz;
    let EdgeA       = TrianglePositions[TriangleIndex * 3u + 1u].xyz;
    let EdgeB       = TrianglePositions[TriangleIndex * 3u + 2u].xyz;
    let PVector     = cross(Direction, EdgeB);
    let Determinant = dot(EdgeA, PVector);
    if (abs(Determinant) < 1.0e-12) { return false; }
    let Sign        = select(-1.0, 1.0, Determinant > 0.0);
    let AbsDet      = Determinant * Sign;
    let TVector     = Origin - Corner;
    let UDet        = dot(TVector, PVector) * Sign;
    if (UDet < 0.0 || UDet > AbsDet) { return false; }
    let QVector     = cross(TVector, EdgeA);
    let VDet        = dot(Direction, QVector) * Sign;
    if (VDet < 0.0 || UDet + VDet > AbsDet) { return false; }
    let TDet        = dot(EdgeB, QVector) * Sign;
    return TDet > RayOffsetScale * AbsDet && TDet < MaxDistance * AbsDet;
}

fn IntersectsBoundingSphere(Origin: vec3f, Direction: vec3f, Sphere: vec4f, MaxDistance: f32) -> bool
{
    let Offset = Origin - Sphere.xyz;
    let HalfB  = dot(Offset, Direction);
    let C      = dot(Offset, Offset) - Sphere.w * Sphere.w;
    if (C <= 0.0) { return true; }
    if (HalfB >= 0.0) { return false; }
    let Discriminant = HalfB * HalfB - C;
    if (Discriminant < 0.0) { return false; }
    return (-HalfB - sqrt(Discriminant)) < MaxDistance;
}

fn IntersectSphere(Origin: vec3f, Direction: vec3f, Sphere: vec4f) -> f32
{
    let Offset = Origin - Sphere.xyz;
    let HalfB  = dot(Offset, Direction);
    let C      = dot(Offset, Offset) - Sphere.w * Sphere.w;
    if (C > 0.0 && HalfB >= 0.0) { return -1.0; }
    let Discriminant = HalfB * HalfB - C;
    if (Discriminant < 0.0) { return -1.0; }
    let Root = sqrt(Discriminant);
    let Near = -HalfB - Root;
    if (Near > RayOffsetScale) { return Near; }
    let Far = -HalfB + Root;
    return select(-1.0, Far, Far > RayOffsetScale);
}

fn FetchAreaLight(LightIndex: u32) -> AreaLight
{
    let Base = LightIndex * 4u;
    return AreaLight(LightRecords[Base], LightRecords[Base + 1u], LightRecords[Base + 2u], LightRecords[Base + 3u]);
}

// 📝 Unit normal precomputed in StudioSceneAssembly.ConstructSoftboxLights and stored in the unused .w channels.
fn LightNormal(Light: AreaLight) -> vec3f
{
    return vec3f(Light.EdgeU.w, Light.EdgeV.w, Light.Radiance.w);
}

fn IntersectLight(Origin: vec3f, Direction: vec3f, Light: AreaLight, MaxDistance: f32) -> f32
{
    let Normal   = LightNormal(Light);
    let Approach = dot(Direction, Normal);
    if (abs(Approach) < 1.0e-8) { return -1.0; }
    let Distance = dot(Light.Centre.xyz - Origin, Normal) / Approach;
    if (Distance <= RayOffsetScale || Distance >= MaxDistance) { return -1.0; }
    let Local = Origin + Direction * Distance - Light.Centre.xyz;
    let U = dot(Local, Light.EdgeU.xyz) / dot(Light.EdgeU.xyz, Light.EdgeU.xyz);
    let V = dot(Local, Light.EdgeV.xyz) / dot(Light.EdgeV.xyz, Light.EdgeV.xyz);
    return select(-1.0, Distance, abs(U) <= 0.5 && abs(V) <= 0.5);
}

// 📝 Closed-form primitives only: floor disc, showcase spheres and (optionally) the softbox rectangles.
fn TraceAnalyticHit(Origin: vec3f, Direction: vec3f, LightsVisible: bool) -> SurfaceHit
{
    var Hit = SurfaceHit(UnboundedDistance, HitAbsent, 0u, vec2f(0.0));

    if (abs(Direction.y) > 1.0e-8)
    {
        let FloorDistance = -Origin.y / Direction.y;
        if (FloorDistance > RayOffsetScale)
        {
            let FloorPoint = Origin + Direction * FloorDistance;
            if (dot(FloorPoint.xz, FloorPoint.xz) < Tracing.Tuning.y * Tracing.Tuning.y)
            {
                Hit = SurfaceHit(FloorDistance, HitFloor, 0u, vec2f(0.0));
            }
        }
    }

    if (Tracing.DepthLimits.w > 0u && IntersectsBoundingSphere(Origin, Direction, Tracing.SphereBounds, Hit.Distance))
    {
        for (var SphereIndex = 0u; SphereIndex < Tracing.DepthLimits.w; SphereIndex++)
        {
            let Distance = IntersectSphere(Origin, Direction, Spheres[SphereIndex * 2u]);
            if (Distance > 0.0 && Distance < Hit.Distance) { Hit = SurfaceHit(Distance, HitSphere, SphereIndex, vec2f(0.0)); }
        }
    }

    if (LightsVisible)
    {
        for (var LightIndex = 0u; LightIndex < Tracing.DepthLimits.z; LightIndex++)
        {
            let Distance = IntersectLight(Origin, Direction, FetchAreaLight(LightIndex), Hit.Distance);
            if (Distance > 0.0) { Hit = SurfaceHit(Distance, HitLight, LightIndex, vec2f(0.0)); }
        }
    }
    return Hit;
}

// 📝 Orders the 4 children by entry distance with a fixed 5-exchange network written on vector lanes (no dynamic
//    indexing). Strict comparisons keep the child order on ties, so the order is a fixed function of the ray.
fn SortChildren(Visit: WideVisit) -> SortedChildren
{
    var D = Visit.Distances;
    var E = (Visit.Links << vec4u(4u)) | Visit.Counts;
    var Swap = vec2<bool>(D.y < D.x, D.w < D.z);                                       // (0,1) (2,3)
    D = vec4f(select(D.x, D.y, Swap.x), select(D.y, D.x, Swap.x), select(D.z, D.w, Swap.y), select(D.w, D.z, Swap.y));
    E = vec4u(select(E.x, E.y, Swap.x), select(E.y, E.x, Swap.x), select(E.z, E.w, Swap.y), select(E.w, E.z, Swap.y));
    Swap = vec2<bool>(D.z < D.x, D.w < D.y);                                           // (0,2) (1,3)
    D = vec4f(select(D.x, D.z, Swap.x), select(D.y, D.w, Swap.y), select(D.z, D.x, Swap.x), select(D.w, D.y, Swap.y));
    E = vec4u(select(E.x, E.z, Swap.x), select(E.y, E.w, Swap.y), select(E.z, E.x, Swap.x), select(E.w, E.y, Swap.y));
    let Middle = D.z < D.y;                                                            // (1,2)
    D = vec4f(D.x, select(D.y, D.z, Middle), select(D.z, D.y, Middle), D.w);
    E = vec4u(E.x, select(E.y, E.z, Middle), select(E.z, E.y, Middle), E.w);
    return SortedChildren(D, E);
}

// 📝 Two-level closest-hit query in ONE loop with direct-descent into the nearest child (no push/pop for the closest
//    child) and a zero-overhead 1-vehicle identity fast path (bay 0 is exact identity, skipping instance tree & transform).
fn TraceClosestHit(Origin: vec3f, Direction: vec3f, LightsVisible: bool) -> SurfaceHit
{
    if (RayStatisticsEnabled) { RayStatistics.x += 1u; }
    var Hit = TraceAnalyticHit(Origin, Direction, LightsVisible);
    if (Tracing.SceneLayout.w == 0u || Tracing.Geometry.z == 0u) { return Hit; }
    let WorldReciprocal = SafeReciprocal(Direction);
    let SingleIdentity  = Tracing.Geometry.z == 1u;
    var RayOrigin    = Origin;
    var RayDirection = Direction;
    var Reciprocal   = WorldReciprocal;
    var Instance     = 0u;
    var ObjectLevel  = SingleIdentity;
    var Stack        : array<vec2u, SceneStackCapacity>;
    var StackSize    = 0u;
    var Entry        = vec2u(select(Tracing.Geometry.x, 0u, SingleIdentity) << 4u, 0u);
    var Active       = true;
    loop
    {
        if (!Active)
        {
            if (StackSize == 0u) { break; }
            StackSize -= 1u;
            Entry = Stack[StackSize];
        }
        Active = false;
        if (Entry.x == ReturnMarker)
        {
            ObjectLevel = false; RayOrigin = Origin; RayDirection = Direction; Reciprocal = WorldReciprocal;
            continue;
        }
        if (bitcast<f32>(Entry.y) >= Hit.Distance) { continue; }
        let Count = Entry.x & 15u;
        let Link  = Entry.x >> 4u;
        if (Count > 0u && !ObjectLevel)
        {
            // 📝 Instance leaf (exactly one instance per leaf, see ConstructInstanceTree).
            Instance     = Link;
            ObjectLevel  = true;
            let Yaw      = FetchInstanceYaw(Instance);
            RayOrigin    = WorldToObjectPoint(Yaw, Origin);
            RayDirection = WorldToObjectVector(Yaw, Direction);
            Reciprocal   = SafeReciprocal(RayDirection);
            Stack[StackSize] = vec2u(ReturnMarker, 0u);
            StackSize += 1u;
            Entry  = vec2u(BoundsEntries[Tracing.Geometry.y + Instance * InstanceStride + 1u].x << 4u, 0u);
            Active = true;
            continue;
        }
        if (Count > 0u)
        {
            for (var Offset = 0u; Offset < Count; Offset++)
            {
                let Candidate = IntersectTriangle(RayOrigin, RayDirection, Link + Offset, Hit.Distance);
                if (Candidate.x > 0.0)
                {
                    Hit = SurfaceHit(Candidate.x, HitTriangle, (Instance << InstanceShift) | (Link + Offset), Candidate.yz);
                }
            }
            continue;
        }
        let Visit = IntersectWideNode(Link, RayOrigin, Reciprocal, Hit.Distance);
        if (min(min(Visit.Distances.x, Visit.Distances.y), min(Visit.Distances.z, Visit.Distances.w)) >= UnboundedDistance) { continue; }
        let Sorted = SortChildren(Visit);
        let Room   = StackSize + 3u <= SceneStackCapacity;
        if (Room && Sorted.Distances.w < UnboundedDistance) { Stack[StackSize] = vec2u(Sorted.Entries.w, bitcast<u32>(Sorted.Distances.w)); StackSize += 1u; }
        if (Room && Sorted.Distances.z < UnboundedDistance) { Stack[StackSize] = vec2u(Sorted.Entries.z, bitcast<u32>(Sorted.Distances.z)); StackSize += 1u; }
        if (Room && Sorted.Distances.y < UnboundedDistance) { Stack[StackSize] = vec2u(Sorted.Entries.y, bitcast<u32>(Sorted.Distances.y)); StackSize += 1u; }
        Entry  = vec2u(Sorted.Entries.x, bitcast<u32>(Sorted.Distances.x));
        Active = true;
    }
    return Hit;
}

// 📝 Two-interface thin-sheet transmittance: T = (1−R)² / (1−R²), tinted. Shared by shadow rays and sheet transmission.
fn ThinSheetTransmittance(CosineIncidence: f32, Material: SurfaceMaterial) -> vec3f
{
    let Reflectance = FresnelDielectric(abs(CosineIncidence), Material.AbsorptionIor.w);
    let Transmitted = (1.0 - Reflectance) * (1.0 - Reflectance) / max(1.0 - Reflectance * Reflectance, 1.0e-6);
    return Material.AlbedoRoughness.rgb * Transmitted;
}

// 📝 Visibility of a light point: direct-descent stackless-first-child two-level loop, 1-vehicle identity fast path,
//    zero-division shadow triangles, and bit-31 opaque early-out. Descending-area child ordering hits large opaque
//    occluders first on slot 0 while preserving fixed deterministic x → y → z → w order for thin-glass products.
fn TraceShadowTransmittance(Origin: vec3f, Direction: vec3f, Distance: f32) -> vec3f
{
    if (RayStatisticsEnabled) { RayStatistics.y += 1u; }
    if (Tracing.DepthLimits.w > 0u && IntersectsBoundingSphere(Origin, Direction, Tracing.SphereBounds, Distance))
    {
        for (var SphereIndex = 0u; SphereIndex < Tracing.DepthLimits.w; SphereIndex++)
        {
            let SphereDistance = IntersectSphere(Origin, Direction, Spheres[SphereIndex * 2u]);
            if (SphereDistance > 0.0 && SphereDistance < Distance) { return vec3f(0.0); }
        }
    }

    var Transmittance = vec3f(1.0);
    if (Tracing.SceneLayout.w == 0u || Tracing.Geometry.z == 0u) { return Transmittance; }
    let WorldReciprocal = SafeReciprocal(Direction);
    let SingleIdentity  = Tracing.Geometry.z == 1u;
    var RayOrigin    = Origin;
    var RayDirection = Direction;
    var Reciprocal   = WorldReciprocal;
    var Instance     = 0u;
    var ObjectLevel  = SingleIdentity;
    var Stack        : array<u32, SceneStackCapacity>;
    var StackSize    = 0u;
    var Entry        = select(Tracing.Geometry.x, 0u, SingleIdentity) << 4u;
    var Active       = true;
    loop
    {
        if (!Active)
        {
            if (StackSize == 0u) { break; }
            StackSize -= 1u;
            Entry = Stack[StackSize];
        }
        Active = false;
        if (Entry == ReturnMarker)
        {
            ObjectLevel = false; RayOrigin = Origin; RayDirection = Direction; Reciprocal = WorldReciprocal;
            continue;
        }
        let Count = Entry & 15u;
        let Link  = Entry >> 4u;
        if (Count > 0u && !ObjectLevel)
        {
            Instance     = Link;
            ObjectLevel  = true;
            let Yaw      = FetchInstanceYaw(Instance);
            RayOrigin    = WorldToObjectPoint(Yaw, Origin);
            RayDirection = WorldToObjectVector(Yaw, Direction);
            Reciprocal   = SafeReciprocal(RayDirection);
            Stack[StackSize] = ReturnMarker;
            StackSize += 1u;
            Entry  = BoundsEntries[Tracing.Geometry.y + Instance * InstanceStride + 1u].x << 4u;
            Active = true;
            continue;
        }
        if (Count > 0u)
        {
            for (var Offset = 0u; Offset < Count; Offset++)
            {
                let TriangleIndex = Link + Offset;
                if (IntersectTriangleShadow(RayOrigin, RayDirection, TriangleIndex, Distance))
                {
                    let PackedSlot = bitcast<u32>(TrianglePositions[TriangleIndex * 3u].w);
                    if ((PackedSlot & ThinDielectricBit) == 0u) { return vec3f(0.0); }
                    let Material = Materials[InstanceMaterialSlot(Instance, PackedSlot & MaterialSlotMask)];
                    // 📝 Rigid transform: the object-space cosine equals the world-space one.
                    let EdgeNormal = normalize(cross(TrianglePositions[TriangleIndex * 3u + 1u].xyz, TrianglePositions[TriangleIndex * 3u + 2u].xyz));
                    Transmittance *= ThinSheetTransmittance(dot(EdgeNormal, RayDirection), Material);
                }
            }
            continue;
        }
        let Visit   = IntersectWideNode(Link, RayOrigin, Reciprocal, Distance);
        let Entries = (Visit.Links << vec4u(4u)) | Visit.Counts;
        let Room    = StackSize + 3u <= SceneStackCapacity;
        if (Visit.Distances.w < UnboundedDistance) { Entry = Entries.w; Active = true; }
        if (Visit.Distances.z < UnboundedDistance) { if (Active && Room) { Stack[StackSize] = Entry; StackSize += 1u; } Entry = Entries.z; Active = true; }
        if (Visit.Distances.y < UnboundedDistance) { if (Active && Room) { Stack[StackSize] = Entry; StackSize += 1u; } Entry = Entries.y; Active = true; }
        if (Visit.Distances.x < UnboundedDistance) { if (Active && Room) { Stack[StackSize] = Entry; StackSize += 1u; } Entry = Entries.x; Active = true; }
    }
    return Transmittance;
}

fn ResolveSurface(Origin: vec3f, Direction: vec3f, Hit: SurfaceHit) -> SurfaceSample
{
    let Position = Origin + Direction * Hit.Distance;
    var GeometricNormal = vec3f(0.0, 1.0, 0.0);
    var ShadingNormal   = GeometricNormal;
    var MaterialSlot    = Tracing.SceneLayout.x;

    if (Hit.Category == HitTriangle)
    {
        let Instance          = Hit.PrimitiveIndex >> InstanceShift;
        let CornerOffset      = (Hit.PrimitiveIndex & TriangleMask) * 3u;
        let LocalGeometric    = cross(TrianglePositions[CornerOffset + 1u].xyz, TrianglePositions[CornerOffset + 2u].xyz);
        let FirstCornerWeight = 1.0 - Hit.Barycentric.x - Hit.Barycentric.y;
        let LocalShading      = TriangleNormals[CornerOffset].xyz * FirstCornerWeight
                              + TriangleNormals[CornerOffset + 1u].xyz * Hit.Barycentric.x
                              + TriangleNormals[CornerOffset + 2u].xyz * Hit.Barycentric.y;
        if (Tracing.Geometry.z == 1u)
        {
            GeometricNormal = normalize(LocalGeometric);
            ShadingNormal   = normalize(LocalShading);
        }
        else
        {
            let Yaw = FetchInstanceYaw(Instance);
            GeometricNormal = normalize(ObjectToWorldVector(Yaw, LocalGeometric));
            ShadingNormal   = normalize(ObjectToWorldVector(Yaw, LocalShading));
        }
        MaterialSlot = InstanceMaterialSlot(Instance, bitcast<u32>(TrianglePositions[CornerOffset].w) & MaterialSlotMask);
    }
    else if (Hit.Category == HitSphere)
    {
        let Sphere = Spheres[Hit.PrimitiveIndex * 2u];
        GeometricNormal = (Position - Sphere.xyz) / Sphere.w;
        ShadingNormal   = GeometricNormal;
        MaterialSlot    = u32(Spheres[Hit.PrimitiveIndex * 2u + 1u].x);
    }

    let FrontFacing = dot(GeometricNormal, Direction) < 0.0;
    if (!FrontFacing) { GeometricNormal = -GeometricNormal; }
    if (dot(ShadingNormal, GeometricNormal) < 0.0) { ShadingNormal = -ShadingNormal; }
    if (dot(ShadingNormal, -Direction) <= 1.0e-4) { ShadingNormal = GeometricNormal; }
    return SurfaceSample(Position, GeometricNormal, ShadingNormal, MaterialSlot, FrontFacing);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    ENVIRONMENT
//------------------------------------------------------------------------------------------------------------------------

fn EvaluateEnvironment(Direction: vec3f) -> vec3f
{
    let Elevation = Direction.y;
    var Radiance  = vec3f(0.0);
    let Category  = Tracing.SceneLayout.y;
    if (Category == 0u)
    {
        // 📝 Dark studio: charcoal walls, a soft overhead bounce and a faint warm band at the horizon.
        let Overhead = pow(max(Elevation, 0.0), 1.5);
        Radiance = mix(vec3f(0.020, 0.021, 0.024), vec3f(0.090, 0.095, 0.105), Overhead)
                 + vec3f(0.030, 0.024, 0.018) * exp(-abs(Elevation) * 9.0);
    }
    else if (Category == 1u)
    {
        // 📝 Overcast sky: CIE overcast luminance gradient (1 + 2 sin θ)/3 over a grey ground.
        let Gradient = (1.0 + 2.0 * max(Elevation, 0.0)) / 3.0;
        Radiance = select(vec3f(0.18, 0.17, 0.16) * 0.5, vec3f(0.80, 0.86, 0.95) * Gradient, Elevation > 0.0);
    }
    else
    {
        // 📝 Warm dusk: orange horizon glow under a deep blue zenith.
        let Zenith  = pow(max(Elevation, 0.0), 0.6);
        let SkylineGlow = exp(-max(Elevation, 0.0) * 6.0) * (0.6 + 0.4 * max(dot(normalize(Direction.xz + vec2f(1.0e-6)), vec2f(-0.6, -0.8)), 0.0));
        Radiance = select(vec3f(0.05, 0.035, 0.03), vec3f(0.06, 0.10, 0.25) * Zenith + vec3f(1.1, 0.42, 0.14) * SkylineGlow, Elevation > 0.0);
    }
    return Radiance * Tracing.EnvironmentTint.rgb * Tracing.EnvironmentTint.w;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    MICROFACET OPTICS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Schlick with grazing reflectance F90 = saturate(50·F0): F0 = 0 (index-matched) reflects nothing at any angle,
//    while every F0 ≥ 0.02 keeps the classic F90 = 1 curve.
fn FresnelSchlick(F0: f32, Cosine: f32) -> f32
{
    let F90        = clamp(50.0 * F0, 0.0, 1.0);
    let Complement = 1.0 - clamp(Cosine, 0.0, 1.0);
    let Squared    = Complement * Complement;
    return F0 + (F90 - F0) * Squared * Squared * Complement;
}

// 📝 Exact unpolarized Fresnel reflectance of a dielectric interface, η = n_transmitted / n_incident. Returns 1 at TIR.
fn FresnelDielectric(CosineIncidence: f32, η: f32) -> f32
{
    let SineTransmitted2 = (1.0 - CosineIncidence * CosineIncidence) / (η * η);
    if (SineTransmitted2 >= 1.0) { return 1.0; }
    let CosineTransmitted = sqrt(1.0 - SineTransmitted2);
    let Perpendicular = (CosineIncidence - η * CosineTransmitted) / (CosineIncidence + η * CosineTransmitted);
    let Parallel      = (η * CosineIncidence - CosineTransmitted) / (η * CosineIncidence + CosineTransmitted);
    return 0.5 * (Perpendicular * Perpendicular + Parallel * Parallel);
}

// 📝 Exact unpolarized Fresnel reflectance of an air–conductor interface with complex index η + iκ, per channel.
fn FresnelConductor(CosineIncidence: f32, η: vec3f, κ: vec3f) -> vec3f
{
    let Cosine2      = CosineIncidence * CosineIncidence;
    let Sine2        = 1.0 - Cosine2;
    let η2           = η * η;
    let κ2           = κ * κ;
    let Shifted      = η2 - κ2 - vec3f(Sine2);
    let SumSquare    = sqrt(Shifted * Shifted + 4.0 * η2 * κ2);
    let RealRoot     = sqrt(max(0.5 * (SumSquare + Shifted), vec3f(0.0)));
    let TermS1       = SumSquare + vec3f(Cosine2);
    let TermS2       = 2.0 * RealRoot * CosineIncidence;
    let ReflectanceS = (TermS1 - TermS2) / (TermS1 + TermS2);
    let TermP1       = Cosine2 * SumSquare + vec3f(Sine2 * Sine2);
    let TermP2       = TermS2 * Sine2;
    let ReflectanceP = ReflectanceS * (TermP1 - TermP2) / (TermP1 + TermP2);
    return clamp(0.5 * (ReflectanceP + ReflectanceS), vec3f(0.0), vec3f(1.0));
}

fn DistributionGgx(CosineHalf: f32, α: f32) -> f32
{
    let α2    = α * α;
    let Denom = CosineHalf * CosineHalf * (α2 - 1.0) + 1.0;
    return α2 / (π * Denom * Denom);
}

fn SmithLambda(Cosine: f32, α: f32) -> f32
{
    let Cosine2  = max(Cosine * Cosine, 1.0e-8);
    let Tangent2 = (1.0 - Cosine2) / Cosine2;
    return 0.5 * (-1.0 + sqrt(1.0 + α * α * Tangent2));
}

// 📝 Orthonormal tangent basis around Normal (Duff et al. 2017, branchless).
fn TangentBasis(Normal: vec3f) -> mat3x3f
{
    let Sign       = select(-1.0, 1.0, Normal.z >= 0.0);
    let Inverse    = -1.0 / (Sign + Normal.z);
    let Shear      = Normal.x * Normal.y * Inverse;
    let Tangent    = vec3f(1.0 + Sign * Normal.x * Normal.x * Inverse, Sign * Shear, -Sign * Normal.x);
    let Bitangent  = vec3f(Shear, Sign + Normal.y * Normal.y * Inverse, -Normal.y);
    return mat3x3f(Tangent, Bitangent, Normal);
}

// 📝 Visible-normal sampling of GGX (Heitz 2018). ViewLocal is in the tangent basis (z = normal).
fn SampleVisibleNormal(ViewLocal: vec3f, α: f32, Point: vec2f) -> vec3f
{
    let Stretched = normalize(vec3f(α * ViewLocal.x, α * ViewLocal.y, ViewLocal.z));
    let LengthSq  = Stretched.x * Stretched.x + Stretched.y * Stretched.y;
    let T1 = select(vec3f(1.0, 0.0, 0.0), vec3f(-Stretched.y, Stretched.x, 0.0) / sqrt(LengthSq), LengthSq > 1.0e-10);
    let T2 = cross(Stretched, T1);
    let Radius = sqrt(Point.x);
    let φ      = 2.0 * π * Point.y;
    let P1 = Radius * cos(φ);
    var P2 = Radius * sin(φ);
    let Blend = 0.5 * (1.0 + Stretched.z);
    P2 = (1.0 - Blend) * sqrt(max(1.0 - P1 * P1, 0.0)) + Blend * P2;
    let Hemisphere = P1 * T1 + P2 * T2 + sqrt(max(0.0, 1.0 - P1 * P1 - P2 * P2)) * Stretched;
    return normalize(vec3f(α * Hemisphere.x, α * Hemisphere.y, max(1.0e-6, Hemisphere.z)));
}

// 📝 Concentric (Shirley–Chiu) disk mapping: area-preserving, continuous, so the lattice structure survives the warp.
fn ConcentricDisk(Point: vec2f) -> vec2f
{
    let Offset = 2.0 * Point - vec2f(1.0);
    if (Offset.x == 0.0 && Offset.y == 0.0) { return vec2f(0.0); }
    var Radius : f32;
    var θ      : f32;
    if (abs(Offset.x) > abs(Offset.y)) { Radius = Offset.x; θ = (π / 4.0) * (Offset.y / Offset.x); }
    else                               { Radius = Offset.y; θ = (π / 2.0) - (π / 4.0) * (Offset.x / Offset.y); }
    return Radius * vec2f(cos(θ), sin(θ));
}

fn CosineHemisphere(Point: vec2f) -> vec3f
{
    let Disk = ConcentricDisk(Point);
    return vec3f(Disk, sqrt(max(0.0, 1.0 - dot(Disk, Disk))));
}

// 📝 GGX reflection lobe value (f × cosθi) and VNDF pdf for precomputed cosines (skips redundant normalize + dots).
fn EvaluateGgxLobe(CosineOut: f32, CosineIn: f32, CosineHalf: f32, α: f32) -> vec2f
{
    let D         = DistributionGgx(CosineHalf, α);
    let LambdaOut = SmithLambda(CosineOut, α);
    let LambdaIn  = SmithLambda(CosineIn, α);
    let G2        = 1.0 / (1.0 + LambdaOut + LambdaIn);
    let G1        = 1.0 / (1.0 + LambdaOut);
    // 📝 Returns (D·G2 / (4 cosθo), pdf). Fresnel is applied by the caller per channel.
    return vec2f(D * G2 / (4.0 * CosineOut), D * G1 / (4.0 * CosineOut));
}

fn MaterialAlpha(PerceptualRoughness: f32) -> f32
{
    return PerceptualRoughness * PerceptualRoughness;
}

// 📝 Evaluates every non-delta lobe of a layered or conductor surface toward ωi. Delta lobes return zero (they are
//    integrated exactly by their single mirror ray, never by light quadrature).
fn EvaluateLobes(Material: SurfaceMaterial, Normal: vec3f, GeometricNormal: vec3f, ωo: vec3f, ωi: vec3f) -> LobeEvaluation
{
    var Lobes = LobeEvaluation(vec3f(0.0), 0.0, vec3f(0.0), 0.0, vec3f(0.0), 0.0);
    let CosineIn  = dot(Normal, ωi);
    let CosineOut = dot(Normal, ωo);
    if (CosineIn <= 0.0 || CosineOut <= 0.0 || dot(GeometricNormal, ωi) <= 0.0) { return Lobes; }
    let HalfVector    = normalize(ωo + ωi);
    let CosineHalf    = max(dot(Normal, HalfVector), 0.0);
    let CosineHalfOut = dot(ωo, HalfVector);
    let Category      = u32(Material.EtaCategory.w);
    let α             = max(MaterialAlpha(Material.AlbedoRoughness.w), 1.0e-4);

    if (Category == SurfaceConductor)
    {
        if (α >= DeltaRoughnessAlpha)
        {
            let Ggx = EvaluateGgxLobe(CosineOut, CosineIn, CosineHalf, α);
            Lobes.Specular    = FresnelConductor(CosineHalfOut, Material.EtaCategory.rgb, Material.KappaCoat.rgb) * Ggx.x;
            Lobes.SpecularPdf = Ggx.y;
        }
        return Lobes;
    }

    let CoatWeight = Material.KappaCoat.w;
    let CoatAlpha  = max(MaterialAlpha(Material.ExitanceCoat.w), 1.0e-4);
    let F0         = Material.SpecularLevel.x;
    let CoatAttenuation = select(1.0, (1.0 - CoatWeight * FresnelSchlick(0.04, CosineOut)) * (1.0 - CoatWeight * FresnelSchlick(0.04, CosineIn)), CoatWeight > 0.0);

    if (CoatWeight > 0.0 && CoatAlpha >= DeltaRoughnessAlpha)
    {
        let Ggx = EvaluateGgxLobe(CosineOut, CosineIn, CosineHalf, CoatAlpha);
        Lobes.Coat    = vec3f(CoatWeight * FresnelSchlick(0.04, CosineHalfOut) * Ggx.x);
        Lobes.CoatPdf = Ggx.y;
    }
    if (F0 > 0.0 && α >= DeltaRoughnessAlpha)
    {
        let Ggx = EvaluateGgxLobe(CosineOut, CosineIn, CosineHalf, α);
        Lobes.Specular    = vec3f(CoatAttenuation * FresnelSchlick(F0, CosineHalfOut) * Ggx.x);
        Lobes.SpecularPdf = Ggx.y;
    }
    let DiffuseTransfer = select(1.0, (1.0 - FresnelSchlick(F0, CosineOut)) * (1.0 - FresnelSchlick(F0, CosineIn)), F0 > 0.0) * CoatAttenuation;
    Lobes.Diffuse    = Material.AlbedoRoughness.rgb * (DiffuseTransfer * CosineIn / π);
    Lobes.DiffusePdf = CosineIn / π;
    return Lobes;
}

// 📝 Power heuristic (β = 2) between two deterministic quadrature rules over the same integrand.
fn PowerHeuristic(Primary: f32, Secondary: f32) -> f32
{
    let P2 = Primary * Primary;
    let S2 = Secondary * Secondary;
    return select(1.0, P2 / (P2 + S2), P2 + S2 > 0.0);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    TRANSPORT
//------------------------------------------------------------------------------------------------------------------------

fn PushTransportRay(Ray: TransportRay)
{
    let Peak = max(max(Ray.Throughput.x, Ray.Throughput.y), Ray.Throughput.z);
    if (Peak < Tracing.Tuning.x || RayDepth(Ray) >= Tracing.DepthLimits.y) { return; }
    if (TransportStackSize >= TransportStackCapacity) { return; }
    TransportStack[TransportStackSize] = Ray;
    TransportStackSize += 1u;
}

fn OffsetOrigin(Position: vec3f, Normal: vec3f, Direction: vec3f) -> vec3f
{
    let Scale = RayOffsetScale * (1.0 + max(max(abs(Position.x), abs(Position.y)), abs(Position.z)) * 0.1);
    return Position + Normal * select(-Scale, Scale, dot(Normal, Direction) > 0.0);
}

// 📝 Exact algebraic cos(max(a − b, 0)) & sin(max(a − b, 0)) for angles a, b ∈ [0, π]: avoids acos/asin/cos SFU calls.
fn CosSinSubClamped(CosA: f32, SinA: f32, CosB: f32, SinB: f32) -> vec2f
{
    if (CosA >= CosB) { return vec2f(1.0, 0.0); }
    return vec2f(CosA * CosB + SinA * SinB, max(0.0, SinA * CosB - CosA * SinB));
}

// 📝 Practical lamps (PracticalLighting.js). Light-tree importance of node Node seen from Position with shading normal
//    Normal (Conty Estevez & Kulla 2018): exact zero-transcendental algebraic formulation using precomputed
//    (cos θo, sin θo, cos θe, θe) in LightRecords[Base + 3u]. Returns vec2f(Importance, LeafRelative) so the caller
//    can reuse the leaf's EvaluateProfile without computing acos+atan2 a second time.
fn LightNodeImportance(Position: vec3f, Normal: vec3f, Node: u32) -> vec2f
{
    let Base      = Tracing.Practical.y + Node * 4u;
    let Lower     = LightRecords[Base];
    let Upper     = LightRecords[Base + 1u];
    // 📝 A leaf is one lamp: its exact unshadowed irradiance I(ω) cosθi / d² (beam shape included) replaces the bound.
    let Link      = bitcast<u32>(Upper.w);
    if ((Link & LampLeafFlag) != 0u)
    {
        let Lamp       = Tracing.Practical.z + (Link & ~LampLeafFlag) * 4u;
        let Placement  = LightRecords[Lamp];
        let Separation = Placement.xyz - Position;
        let Distance2  = max(dot(Separation, Separation), 1.0e-12);
        let ωi         = Separation * inverseSqrt(Distance2);
        let Incidence  = dot(Normal, ωi);
        if (Incidence <= 0.0) { return vec2f(0.0, 0.0); }
        let Relative   = EvaluateProfile(u32(Placement.w), -ωi, LightRecords[Lamp + 1u].xyz, LightRecords[Lamp + 2u].xyz);
        let Importance = Relative * dot(LightRecords[Lamp + 3u].rgb, vec3f(0.2126, 0.7152, 0.0722)) * Incidence / Distance2;
        return vec2f(Importance, Relative);
    }
    let Cone      = LightRecords[Base + 2u];
    let Angles    = LightRecords[Base + 3u]; // (cos θo, sin θo, cos θe, θe)
    let Diagonal  = Upper.xyz - Lower.xyz;
    let Radius2   = 0.25 * dot(Diagonal, Diagonal);
    let Offset    = Position - 0.5 * (Lower.xyz + Upper.xyz);
    let Distance2 = dot(Offset, Offset);
    var CosOuter  = 1.0;
    var CosInner  = 1.0;
    if (Distance2 <= Radius2)
    {
        if (Angles.w <= 0.0) { return vec2f(0.0, -1.0); }
    }
    else
    {
        let InvDist = inverseSqrt(Distance2);
        let Toward  = Offset * InvDist;
        let SinB    = sqrt(Radius2 / Distance2);
        let CosB    = sqrt(max(0.0, 1.0 - SinB * SinB));
        // 📝 Early-out if the entire cluster lies below the surface horizon (CosInner <= 0) before evaluating the cone.
        let CosN    = clamp(-dot(Normal, Toward), -1.0, 1.0);
        let SinN    = sqrt(max(0.0, 1.0 - CosN * CosN));
        CosInner    = select(CosN * CosB + SinN * SinB, 1.0, CosN >= CosB);
        if (CosInner <= 0.0) { return vec2f(0.0, -1.0); }
        let CosC    = clamp(dot(Cone.xyz, Toward), -1.0, 1.0);
        let SinC    = sqrt(max(0.0, 1.0 - CosC * CosC));
        let Step1   = CosSinSubClamped(CosC, SinC, Angles.x, Angles.y);
        let Step2   = CosSinSubClamped(Step1.x, Step1.y, CosB, SinB);
        CosOuter    = Step2.x;
        if (CosOuter <= Angles.z) { return vec2f(0.0, -1.0); }
    }
    return vec2f(Lower.w * max(CosOuter, 1.0e-3) * CosInner / max(Distance2, Radius2), -1.0);
}

struct LampDraw
{
    Lamp              : u32,
    Pdf               : f32,     // probability of having drawn this lamp; 0 = nothing drawn
    Relative          : f32,     // cached leaf EvaluateProfile when >= 0; -1 means caller must evaluate
}

// 📝 Adaptive tree splitting (Conty Estevez & Kulla 2018): starting from the root, repeatedly replace the cut node with
//    the largest power × radius² / distance⁴ (bright, near and spread out — where one draw would be poorest) by its two
//    children, while its radius² exceeds LampSplitRatio × distance², up to Shading.w nodes. The cut partitions the
//    lamps, so sampling one lamp under every cut node and summing stays unbiased. Pure function of position, no RNG.
fn ResolveLampCut(Position: vec3f) -> u32
{
    let Limit = clamp(u32(Tracing.Shading.w), 1u, LampCutCapacity);
    LampCut[0] = 0u;
    var Count = 1u;
    for (var Round = 1u; Round < Limit; Round++)
    {
        var Chosen = LampCutCapacity;
        var Score  = 0.0;
        for (var Entry = 0u; Entry < Count; Entry++)
        {
            let Base  = Tracing.Practical.y + LampCut[Entry] * 4u;
            let Lower = LightRecords[Base];
            let Upper = LightRecords[Base + 1u];
            if ((bitcast<u32>(Upper.w) & LampLeafFlag) != 0u) { continue; }
            let Diagonal  = Upper.xyz - Lower.xyz;
            let Radius2   = 0.25 * dot(Diagonal, Diagonal);
            let Offset    = Position - 0.5 * (Lower.xyz + Upper.xyz);
            let Distance2 = max(dot(Offset, Offset), 1.0e-6);
            let Spread    = Lower.w * Radius2 / (Distance2 * Distance2);
            if (Radius2 > LampSplitRatio * Distance2 && Spread > Score) { Score = Spread; Chosen = Entry; }
        }
        if (Chosen == LampCutCapacity) { break; }
        let Link = bitcast<u32>(LightRecords[Tracing.Practical.y + LampCut[Chosen] * 4u + 1u].w);
        LampCut[Chosen] = Link;
        LampCut[Count]  = Link + 1u;
        Count += 1u;
    }
    return Count;
}

// 📝 One lamp under cut node Entry: descend choosing each child with probability ∝ importance, steered by the fixed
//    quadrature coordinate Selection (rotated per cut node, rescaled at every level, so one number decides the whole
//    path). With the all-lamps rule (Shading.z = 1) entry Entry is simply lamp Entry with probability 1.
fn DrawPracticalLamp(Position: vec3f, Normal: vec3f, Entry: u32, Selection: f32) -> LampDraw
{
    if (Tracing.Shading.z > 0.5) { return LampDraw(Entry, 1.0, -1.0); }
    var Node     = LampCut[min(Entry, LampCutCapacity - 1u)];
    var Pdf      = 1.0;
    var Relative = -1.0;
    var Steer    = fract(Selection + f32(Entry) * GoldenFraction);
    for (var Level = 0u; Level < 32u; Level++)
    {
        let Link = bitcast<u32>(LightRecords[Tracing.Practical.y + Node * 4u + 1u].w);
        if ((Link & LampLeafFlag) != 0u) { return LampDraw(Link & ~LampLeafFlag, Pdf, Relative); }
        let LeftRes  = LightNodeImportance(Position, Normal, Link);
        let RightRes = LightNodeImportance(Position, Normal, Link + 1u);
        let Total    = LeftRes.x + RightRes.x;
        if (!(Total > 0.0)) { break; }
        let LeftShare  = LeftRes.x / Total;
        let RightShare = RightRes.x / Total;
        if (Steer < LeftShare) { Node = Link;      Pdf *= LeftShare;  Relative = LeftRes.y;  Steer = min(Steer / LeftShare, SelectionCeiling); }
        else                   { Node = Link + 1u; Pdf *= RightShare; Relative = RightRes.y; Steer = min((Steer - LeftShare) / RightShare, SelectionCeiling); }
    }
    return LampDraw(0u, 0.0, -1.0);
}

// 📝 Relative intensity (peak = 1) of IES profile Profile toward unit direction Toward (lamp → receiver): θ from the
//    photometric axis, φ from Reference toward Axis × Reference, bilinear on the PhotometricProfile.js grid.
fn EvaluateProfile(Profile: u32, Toward: vec3f, Axis: vec3f, Reference: vec3f) -> f32
{
    let Vertical   = acos(clamp(dot(Axis, Toward), -1.0, 1.0)) * (f32(ProfileVerticalSamples - 1u) / π);
    var Horizontal = atan2(dot(Toward, cross(Axis, Reference)), dot(Toward, Reference)) * (f32(ProfileHorizontalSamples) / (2.0 * π));
    if (Horizontal < 0.0) { Horizontal += f32(ProfileHorizontalSamples); }
    let V0 = min(u32(Vertical), ProfileVerticalSamples - 2u);
    let Vt = Vertical - f32(V0);
    let H0 = min(u32(Horizontal), ProfileHorizontalSamples - 1u);
    let Ht = clamp(Horizontal - f32(H0), 0.0, 1.0);
    let H1 = (H0 + 1u) % ProfileHorizontalSamples;
    let Base = Tracing.Practical.w + Profile * (ProfileVerticalSamples * ProfileHorizontalSamples / 4u);
    let I00 = H0 * ProfileVerticalSamples + V0;
    let I10 = H1 * ProfileVerticalSamples + V0;
    let Near = mix(LightRecords[Base + (I00 >> 2u)][I00 & 3u], LightRecords[Base + ((I00 + 1u) >> 2u)][(I00 + 1u) & 3u], Vt);
    let Far  = mix(LightRecords[Base + (I10 >> 2u)][I10 & 3u], LightRecords[Base + ((I10 + 1u) >> 2u)][(I10 + 1u) & 3u], Vt);
    return mix(Near, Far, Ht);
}

// 📝 Light quadrature at a non-delta vertex: one shared Sobol' point per softbox per sample, combined with the
//    BSDF-lobe rule through the power heuristic per lobe; then the practical lamps — one drawn from the light tree, or
//    every lamp with the all-lamps rule. Lamps are points (delta lights: no lobe ray can hit them, so no MIS weight).
//    One loop and one EvaluateLobes / shadow-ray call site for both kinds (shader size matters on software adapters).
fn IntegrateLightQuadrature(Surface: SurfaceSample, Material: SurfaceMaterial, ωo: vec3f, SampleIndex: u32, Depth: u32, LobesContinue: bool) -> vec3f
{
    var Radiance  = vec3f(0.0);
    let AreaCount = Tracing.DepthLimits.z;
    var Point     = vec2f(0.0);
    if (AreaCount > 0u) { Point = DepthPair(SampleIndex, Depth, PairLight); }
    let AllLamps  = Tracing.Shading.z > 0.5;
    var LampDraws = select(min(Tracing.Practical.x, 1u), Tracing.Practical.x, AllLamps);
    var Selection = 0.0;
    if (LampDraws > 0u && !AllLamps)
    {
        Selection = QuadraturePair(SampleIndex, PairPracticalBase + min(Depth, 15u)).x;
        LampDraws = ResolveLampCut(Surface.Position);
    }
    for (var EntryIndex = 0u; EntryIndex < AreaCount + LampDraws; EntryIndex++)
    {
        var ωi       = vec3f(0.0);
        var Distance = 0.0;
        var LightPdf = 0.0;
        var Emitted  = vec3f(0.0);
        var Delta    = false;
        if (EntryIndex < AreaCount)
        {
            let Light       = FetchAreaLight(EntryIndex);
            let LightPoint  = Light.Centre.xyz + (Point.x - 0.5) * Light.EdgeU.xyz + (Point.y - 0.5) * Light.EdgeV.xyz;
            let Separation  = LightPoint - Surface.Position;
            let Distance2   = dot(Separation, Separation);
            Distance        = sqrt(Distance2);
            ωi              = Separation / Distance;
            if (dot(Surface.GeometricNormal, ωi) <= 0.0 || dot(Surface.ShadingNormal, ωi) <= 0.0) { continue; }
            let CosineLight = -dot(ωi, LightNormal(Light));
            if (CosineLight <= 0.0) { continue; }
            LightPdf = Distance2 / (CosineLight * Light.Centre.w);
            Emitted  = Light.Radiance.rgb;
        }
        else
        {
            let Draw = DrawPracticalLamp(Surface.Position, Surface.ShadingNormal, EntryIndex - AreaCount, Selection);
            if (Draw.Pdf <= 0.0) { continue; }
            let Base       = Tracing.Practical.z + Draw.Lamp * 4u;
            let Placement  = LightRecords[Base];
            let Separation = Placement.xyz - Surface.Position;
            let Distance2  = dot(Separation, Separation);
            Distance       = sqrt(Distance2);
            ωi             = Separation / Distance;
            if (dot(Surface.GeometricNormal, ωi) <= 0.0 || dot(Surface.ShadingNormal, ωi) <= 0.0) { continue; }
            let Relative   = select(EvaluateProfile(u32(Placement.w), -ωi, LightRecords[Base + 1u].xyz, LightRecords[Base + 2u].xyz),
                                    Draw.Relative, Draw.Relative >= 0.0);
            if (Relative <= 0.0) { continue; }
            // 📝 Point lamp: E = I(ω) cosθ / d², divided by the draw probability (LightPdf carries d² × pdf).
            LightPdf = Distance2 * Draw.Pdf;
            Emitted  = LightRecords[Base + 3u].rgb * Relative;
            Delta    = true;
        }

        // 📝 At a terminal vertex no lobe ray continues, so the light rule carries the full weight.
        let Lobes = EvaluateLobes(Material, Surface.ShadingNormal, Surface.GeometricNormal, ωo, ωi);
        let ContinuedPdf = select(vec3f(0.0), vec3f(Lobes.DiffusePdf, Lobes.SpecularPdf, Lobes.CoatPdf), LobesContinue);
        let Combined = Lobes.Diffuse  * PowerHeuristic(LightPdf, ContinuedPdf.x)
                     + Lobes.Specular * PowerHeuristic(LightPdf, ContinuedPdf.y)
                     + Lobes.Coat     * PowerHeuristic(LightPdf, ContinuedPdf.z);
        let Weighted = select(Combined, Lobes.Diffuse + Lobes.Specular + Lobes.Coat, Delta);
        if (max(max(Weighted.x, Weighted.y), Weighted.z) <= 0.0) { continue; }

        let Origin = OffsetOrigin(Surface.Position, Surface.GeometricNormal, ωi);
        let Visibility = TraceShadowTransmittance(Origin, ωi, Distance * (1.0 - 1.0e-4));
        Radiance += Visibility * Emitted * Weighted / LightPdf;
    }
    return Radiance;
}

const LobeConductor : u32 = 0u;
const LobeSpecular  : u32 = 1u;
const LobeCoat      : u32 = 2u;

// 📝 Reflection weight of one lobe toward ωi through microfacet normal HalfVector — the SAME expressions EvaluateLobes uses, so
//    the BSDF-lobe rule and the light rule integrate one identical integrand and the MIS combination stays exact.
fn ReflectionLobeFresnel(Material: SurfaceMaterial, Lobe: u32, CosineOut: f32, CosineIn: f32, CosineHalfOut: f32, CoatOut: f32) -> vec3f
{
    if (Lobe == LobeConductor) { return FresnelConductor(CosineHalfOut, Material.EtaCategory.rgb, Material.KappaCoat.rgb); }
    let CoatWeight = Material.KappaCoat.w;
    if (Lobe == LobeCoat) { return vec3f(CoatWeight * FresnelSchlick(0.04, CosineHalfOut)); }
    let CoatAttenuation = select(1.0, CoatOut * (1.0 - CoatWeight * FresnelSchlick(0.04, CosineIn)), CoatWeight > 0.0);
    return vec3f(CoatAttenuation * FresnelSchlick(Material.SpecularLevel.x, CosineHalfOut));
}

// 📝 Pushes one reflection-lobe continuation. Delta lobes trace the exact mirror direction with pdf 0 (skipping
//    DepthPair, Sobol' and BlueNoiseRanks); glossy lobes evaluate DepthPair lazily AFTER depth/weight early-outs.
fn PushReflectionLobe(Surface: SurfaceSample, Ray: TransportRay, Material: SurfaceMaterial, ωo: vec3f, α: f32,
                      SampleIndex: u32, PairSlot: u32, Lobe: u32, Basis: mat3x3f, ViewLocal: vec3f, CoatOut: f32)
{
    let Depth         = RayDepth(Ray);
    let NonDeltaDepth = RayNonDeltaDepth(Ray);
    let Medium        = RayMedium(Ray);
    if (Depth + 1u >= Tracing.DepthLimits.y) { return; }
    let Normal        = Surface.ShadingNormal;
    let CosineOut     = max(dot(Normal, ωo), 1.0e-6);
    if (α < DeltaRoughnessAlpha)
    {
        let ωi = reflect(-ωo, Normal);
        if (dot(ωi, Surface.GeometricNormal) <= 0.0) { return; }
        let Weight = ReflectionLobeFresnel(Material, Lobe, CosineOut, CosineOut, CosineOut, CoatOut);
        PushTransportRay(TransportRay(OffsetOrigin(Surface.Position, Surface.GeometricNormal, ωi),
                                      PackRayState(Depth + 1u, NonDeltaDepth, Medium), ωi, 0.0,
                                      Ray.Throughput * Weight, 0.0));
        return;
    }
    if (NonDeltaDepth >= Tracing.DepthLimits.x) { return; }
    let Point      = DepthPair(SampleIndex, Depth, PairSlot);
    let HalfVector = Basis * SampleVisibleNormal(ViewLocal, α, Point);
    let ωi         = reflect(-ωo, HalfVector);
    let CosineIn   = dot(Normal, ωi);
    if (CosineIn <= 0.0 || dot(ωi, Surface.GeometricNormal) <= 0.0) { return; }
    let LambdaOut = SmithLambda(CosineOut, α);
    let LambdaIn  = SmithLambda(CosineIn, α);
    let Masking   = (1.0 + LambdaOut) / (1.0 + LambdaOut + LambdaIn);    // G2 / G1
    let Pdf       = DistributionGgx(max(dot(Normal, HalfVector), 0.0), α) / ((1.0 + LambdaOut) * 4.0 * CosineOut);
    let Weight    = ReflectionLobeFresnel(Material, Lobe, CosineOut, CosineIn, max(dot(ωo, HalfVector), 0.0), CoatOut) * Masking;
    PushTransportRay(TransportRay(OffsetOrigin(Surface.Position, Surface.GeometricNormal, ωi),
                                  PackRayState(Depth + 1u, NonDeltaDepth + 1u, Medium), ωi, Pdf,
                                  Ray.Throughput * Weight, 0.0));
}

fn ScatterSurface(Surface: SurfaceSample, Ray: TransportRay, HitDistance: f32, SampleIndex: u32) -> vec3f
{
    let Material      = Materials[Surface.MaterialSlot];
    let Category      = u32(Material.EtaCategory.w);
    let ωo            = -Ray.Direction;
    let Normal        = Surface.ShadingNormal;
    let CosineOut     = max(dot(Normal, ωo), 1.0e-6);
    let Depth         = RayDepth(Ray);
    let NonDeltaDepth = RayNonDeltaDepth(Ray);
    let Medium        = RayMedium(Ray);
    var Radiance      = vec3f(0.0);

    if (Surface.FrontFacing) { Radiance += Ray.Throughput * Material.ExitanceCoat.rgb; }

    if (Category == SurfaceSolidDielectric)
    {
        // 📝 Fresnel splitting: reflection AND refraction both continue with their exact weights (no coin flip).
        if (Depth + 1u >= Tracing.DepthLimits.y) { return Radiance; }
        let Ior       = Material.AbsorptionIor.w;
        let η         = select(1.0 / Ior, Ior, Surface.FrontFacing);
        let Cosine    = clamp(dot(Surface.GeometricNormal, ωo), 0.0, 1.0);
        let Reflected = FresnelDielectric(Cosine, η);
        let ωr        = reflect(-ωo, Surface.GeometricNormal);
        PushTransportRay(TransportRay(OffsetOrigin(Surface.Position, Surface.GeometricNormal, ωr),
                                      PackRayState(Depth + 1u, NonDeltaDepth, Medium), ωr, 0.0,
                                      Ray.Throughput * Reflected, 0.0));
        if (Reflected < 1.0)
        {
            let ωt = refract(-ωo, Surface.GeometricNormal, 1.0 / η);
            let InnerMedium = select(InvalidMedium, Surface.MaterialSlot, Surface.FrontFacing);
            PushTransportRay(TransportRay(OffsetOrigin(Surface.Position, Surface.GeometricNormal, ωt),
                                          PackRayState(Depth + 1u, NonDeltaDepth, InnerMedium), ωt, 0.0,
                                          Ray.Throughput * (1.0 - Reflected), 0.0));
        }
        return Radiance;
    }

    if (Category == SurfaceThinDielectric)
    {
        // 📝 Thin sheet: straight transmission keeps the lobe pdf and accumulates travel, so the light-quadrature MIS
        //    weight on the far side stays consistent with the shadow ray that already passed through this sheet.
        if (Depth + 1u >= Tracing.DepthLimits.y) { return Radiance; }
        let Cosine      = clamp(dot(Surface.GeometricNormal, ωo), 0.0, 1.0);
        let Reflectance = FresnelDielectric(Cosine, Material.AbsorptionIor.w);
        let Sheet       = Reflectance + (1.0 - Reflectance) * (1.0 - Reflectance) * Reflectance / max(1.0 - Reflectance * Reflectance, 1.0e-6);
        let ωr          = reflect(-ωo, Surface.GeometricNormal);
        PushTransportRay(TransportRay(OffsetOrigin(Surface.Position, Surface.GeometricNormal, ωr),
                                      PackRayState(Depth + 1u, NonDeltaDepth, Medium), ωr, 0.0,
                                      Ray.Throughput * Sheet, 0.0));
        PushTransportRay(TransportRay(OffsetOrigin(Surface.Position, Surface.GeometricNormal, Ray.Direction),
                                      PackRayState(Depth + 1u, NonDeltaDepth, Medium), Ray.Direction, Ray.LobePdf,
                                      Ray.Throughput * ThinSheetTransmittance(Cosine, Material), Ray.TravelSinceLobe + HitDistance));
        return Radiance;
    }

    // 📝 Layered / conductor: light quadrature first (exact short-circuit on mirror conductors whose non-delta lobes
    //    are identically zero), then every lobe continues with its exact weight.
    let CanContinue   = Depth + 1u < Tracing.DepthLimits.y;
    let LobesContinue = NonDeltaDepth < Tracing.DepthLimits.x && CanContinue;
    let α             = max(MaterialAlpha(Material.AlbedoRoughness.w), 1.0e-4);
    if (!(Category == SurfaceConductor && α < DeltaRoughnessAlpha))
    {
        Radiance += Ray.Throughput * IntegrateLightQuadrature(Surface, Material, ωo, SampleIndex, Depth, LobesContinue);
    }
    if (!CanContinue) { return Radiance; }

    let NeedBasis = LobesContinue && (Category != SurfaceConductor || α >= DeltaRoughnessAlpha);
    var Basis     = mat3x3f(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 1.0, 0.0), vec3f(0.0, 0.0, 1.0));
    var ViewLocal = vec3f(0.0, 0.0, 1.0);
    if (NeedBasis)
    {
        Basis     = TangentBasis(Normal);
        ViewLocal = transpose(Basis) * ωo;
    }

    if (Category == SurfaceConductor)
    {
        PushReflectionLobe(Surface, Ray, Material, ωo, α, SampleIndex, PairSpecular, LobeConductor, Basis, ViewLocal, 1.0);
        return Radiance;
    }

    let CoatWeight = Material.KappaCoat.w;
    let CoatOut    = select(1.0, 1.0 - CoatWeight * FresnelSchlick(0.04, CosineOut), CoatWeight > 0.0);
    let F0         = Material.SpecularLevel.x;
    if (CoatWeight > 0.0)
    {
        let CoatAlpha = max(MaterialAlpha(Material.ExitanceCoat.w), 1.0e-4);
        PushReflectionLobe(Surface, Ray, Material, ωo, CoatAlpha, SampleIndex, PairCoat, LobeCoat, Basis, ViewLocal, CoatOut);
    }
    if (F0 > 0.0)
    {
        PushReflectionLobe(Surface, Ray, Material, ωo, α, SampleIndex, PairSpecular, LobeSpecular, Basis, ViewLocal, CoatOut);
    }

    if (LobesContinue)
    {
        let SpecularOut   = select(1.0, 1.0 - FresnelSchlick(F0, CosineOut), F0 > 0.0);
        let BaseTransfer  = SpecularOut * CoatOut;
        let MaxDiffuse    = Ray.Throughput * Material.AlbedoRoughness.rgb * BaseTransfer;
        if (max(max(MaxDiffuse.x, MaxDiffuse.y), MaxDiffuse.z) >= Tracing.Tuning.x)
        {
            let ωi       = Basis * CosineHemisphere(DepthPair(SampleIndex, Depth, PairDiffuse));
            let CosineIn = dot(Normal, ωi);
            if (CosineIn > 0.0 && dot(ωi, Surface.GeometricNormal) > 0.0)
            {
                let CoatIn   = select(1.0, 1.0 - CoatWeight * FresnelSchlick(0.04, CosineIn), CoatWeight > 0.0);
                let SpecIn   = select(1.0, 1.0 - FresnelSchlick(F0, CosineIn), F0 > 0.0);
                let Transfer = BaseTransfer * SpecIn * CoatIn;
                PushTransportRay(TransportRay(OffsetOrigin(Surface.Position, Surface.GeometricNormal, ωi),
                                              PackRayState(Depth + 1u, NonDeltaDepth + 1u, Medium), ωi, CosineIn / π,
                                              Ray.Throughput * Material.AlbedoRoughness.rgb * Transfer, 0.0));
            }
        }
    }
    return Radiance;
}

// 📝 Camera hit from the visibility raster: the rasterized triangle is re-intersected exactly with this sample's ray
//    (same subpixel point), then the closed-form primitives are tested. Category HitAbsent with Distance 0 means
//    "not resolved" (the exact ray misses the named triangle — a silhouette edge): the transport loop then runs the full
//    bounds-tree trace. ⚠️ Not bit-identical to traced visibility: the rasterizer snaps vertices to a subpixel grid, so
//    sliver triangles thinner than that grid and exact coplanar ties can resolve differently (≈ 1 sample in 10⁴).
fn ResolveRasterizedPrimary(Origin: vec3f, Direction: vec3f, PixelCoordinate: vec2u) -> SurfaceHit
{
    var Hit = TraceAnalyticHit(Origin, Direction, Tracing.Tuning.z > 0.5);
    let Identifier = textureLoad(VisibilityIds, vec2i(PixelCoordinate), 0).x;
    if (Identifier == 0u) { return Hit; }
    let Instance = (Identifier - 1u) >> InstanceShift;
    var LocalOrigin    = Origin;
    var LocalDirection = Direction;
    if (Tracing.Geometry.z > 1u)
    {
        let Yaw        = FetchInstanceYaw(Instance);
        LocalOrigin    = WorldToObjectPoint(Yaw, Origin);
        LocalDirection = WorldToObjectVector(Yaw, Direction);
    }
    let Candidate = IntersectTriangle(LocalOrigin, LocalDirection, (Identifier - 1u) & TriangleMask, UnboundedDistance);
    if (Candidate.x <= 0.0) { return SurfaceHit(0.0, HitAbsent, 0u, vec2f(0.0)); }
    if (Candidate.x < Hit.Distance) { Hit = SurfaceHit(Candidate.x, HitTriangle, Identifier - 1u, Candidate.yz); }
    return Hit;
}

// 📝 Fixed per-pixel scramble key (0 = shared point set). A pure function of the pixel coordinate.
fn ResolvePixelScramble(PixelCoordinate: vec2u) -> u32
{
    if (Tracing.Realtime.x != 1u) { return 0u; }
    return AvalancheKey((PixelCoordinate.x * 0x8da6b343u) ^ (PixelCoordinate.y * 0xd8163841u) ^ 0x2545f491u) | 1u;
}

// 📝 PixelCoordinate is the traced tile (one per invocation). With trace scale S > 1 a tile covers S × S display pixels:
//    the subpixel point spans the whole tile and the sample lands in the display pixel it falls in (ImagePosition).
//    S = 1 is the original per-pixel integration, bit for bit.
fn IntegrateSample(PixelCoordinate: vec2u, SampleIndex: u32) -> vec3f
{
    // 📝 The subpixel pair is never pixel-keyed, so it can be drawn first; the pixel keys then follow the display pixel
    //    the sample lands in (scale 1: the invocation's own pixel, bit for bit as before).
    let Extent   = vec2f(f32(Tracing.Extent.x), f32(Tracing.Extent.y));
    let ScaleInt = max(Tracing.Geometry.w, 1u);
    let Scale    = f32(ScaleInt);
    let Subpixel = QuadraturePair(SampleIndex, PairSubpixel);
    ImagePosition = vec2f(PixelCoordinate) * Scale + Subpixel * Scale;
    let KeyPixel = select(min(vec2u(ImagePosition), PixelCoordinate * ScaleInt + vec2u(ScaleInt - 1u)), PixelCoordinate, ScaleInt == 1u);
    PixelScramble   = ResolvePixelScramble(KeyPixel);
    BlueNoiseActive = Tracing.Realtime.x == 2u;
    BlueNoiseOrigin = KeyPixel;
    let Ndc      = (ImagePosition / Extent) * 2.0 - vec2f(1.0);
    var Origin   = Tracing.CameraOrigin.xyz;
    var Direction = normalize(Tracing.CameraForward.xyz + Ndc.x * Tracing.CameraRight.xyz - Ndc.y * Tracing.CameraUp.xyz);

    if (Tracing.CameraOrigin.w > 0.0)
    {
        // 📝 Thin lens: the lens point comes from its own quadrature pair; the focus plane is perpendicular to forward.
        let Lens       = ConcentricDisk(QuadraturePair(SampleIndex, PairLens)) * Tracing.CameraOrigin.w;
        let FocusPoint = Origin + Direction * (Tracing.CameraForward.w / dot(Direction, Tracing.CameraForward.xyz));
        Origin    = Origin + normalize(Tracing.CameraRight.xyz) * Lens.x + normalize(Tracing.CameraUp.xyz) * Lens.y;
        Direction = normalize(FocusPoint - Origin);
    }

    TransportStack[0]  = TransportRay(Origin, PackRayState(0u, 0u, InvalidMedium), Direction, 0.0, vec3f(1.0), 0.0);
    TransportStackSize = 1u;
    var Radiance = vec3f(0.0);

    // 📝 Resolved outside the loop: an extra call inside it slowed every ray by ~11 % on SwiftShader even when disabled.
    var PrimaryHit     = SurfaceHit(0.0, HitAbsent, 0u, vec2f(0.0));
    var PrimaryPending = false;
    if (Tracing.Realtime.y == 1u && Tracing.CameraOrigin.w <= 0.0)
    {
        PrimaryHit     = ResolveRasterizedPrimary(Origin, Direction, PixelCoordinate);
        PrimaryPending = PrimaryHit.Distance > 0.0;
    }

    loop
    {
        if (TransportStackSize == 0u) { break; }
        TransportStackSize -= 1u;
        var Ray = TransportStack[TransportStackSize];
        // 📝 Softboxes may be hidden from camera rays (studio practice); reflections and refractions always see them.
        var Hit = PrimaryHit;
        if (PrimaryPending) { PrimaryPending = false; }
        else                { Hit = TraceClosestHit(Ray.Origin, Ray.Direction, RayDepth(Ray) > 0u || Tracing.Tuning.z > 0.5); }

        let Medium = RayMedium(Ray);
        if (Medium != InvalidMedium)
        {
            let Absorption = Materials[Medium].AbsorptionIor.rgb;
            Ray.Throughput = Ray.Throughput * exp(-Absorption * min(Hit.Distance, 100.0));
        }

        if (Hit.Category == HitAbsent)
        {
            Radiance += Ray.Throughput * EvaluateEnvironment(Ray.Direction);
            continue;
        }

        if (Hit.Category == HitLight)
        {
            let Light       = FetchAreaLight(Hit.PrimitiveIndex);
            let CosineLight = -dot(Ray.Direction, LightNormal(Light));
            if (CosineLight > 0.0)
            {
                let Travel   = Ray.TravelSinceLobe + Hit.Distance;
                let LightPdf = Travel * Travel / (CosineLight * Light.Centre.w);
                let Weight   = select(1.0, PowerHeuristic(Ray.LobePdf, LightPdf), Ray.LobePdf > 0.0);
                Radiance += Ray.Throughput * Light.Radiance.rgb * Weight;
            }
            continue;
        }

        let Surface = ResolveSurface(Ray.Origin, Ray.Direction, Hit);
        Radiance += ScatterSurface(Surface, Ray, Hit.Distance, SampleIndex);
    }
    return Radiance;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

fn AcceptSample(Radiance: vec3f) -> vec3f
{
    // 📝 Guard against non-finite samples so one degenerate triangle can never poison the accumulation.
    let Finite = all(Radiance == Radiance) && all(abs(Radiance) < vec3f(1.0e20));
    return select(vec3f(0.0), Radiance, Finite);
}

// 📝 Full resolution (scale 1): one pixel per invocation, sums kept in registers — the reference digests.
//    Reduced trace (scale > 1) or ray statistics: each sample lands in the display pixel its subpixel point falls in;
//    one invocation owns all pixels of its tile, so writes never race and stay in sample order.
//    IntegrateSample has ONE call site: every inlined copy of the transport loop costs compile size and speed.
@compute @workgroup_size(8, 8, 1)
fn IntegrateRadiance(@builtin(global_invocation_id) InvocationId: vec3u)
{
    let Scale      = max(Tracing.Geometry.w, 1u);
    let Tiles      = (Tracing.Extent.xy + vec2u(Scale - 1u)) / Scale;
    if (InvocationId.x >= Tiles.x || InvocationId.y >= Tiles.y) { return; }
    let PixelCount = Tracing.Extent.x * Tracing.Extent.y;
    let Direct     = Scale == 1u && !RayStatisticsEnabled;
    let TileIndex  = InvocationId.y * Tracing.Extent.x + InvocationId.x;
    var Sum        = vec4f(0.0);
    var Moments    = vec4f(0.0);
    if (Direct) { Sum = Accumulation[TileIndex]; Moments = Accumulation[3u * PixelCount + TileIndex]; }
    for (var Offset = 0u; Offset < Tracing.Extent.w; Offset++)
    {
        RayStatistics = vec4u(0u);
        let Accepted  = AcceptSample(IntegrateSample(InvocationId.xy, Tracing.Extent.z + Offset));
        // 📝 Second moment of luminance, for the denoiser's per-pixel variance. Never feeds back into the sums.
        let Luminance = dot(Accepted, vec3f(0.2126, 0.7152, 0.0722));
        if (Direct)
        {
            Sum     = Sum + vec4f(Accepted, 1.0);
            Moments = Moments + vec4f(Luminance * Luminance, 0.0, 0.0, 0.0);
            continue;
        }
        let Pixel = min(vec2u(ImagePosition), InvocationId.xy * Scale + vec2u(Scale - 1u));
        if (Pixel.x >= Tracing.Extent.x || Pixel.y >= Tracing.Extent.y) { continue; }
        let PixelIndex  = Pixel.y * Tracing.Extent.x + Pixel.x;
        let MomentIndex = 3u * PixelCount + PixelIndex;
        if (RayStatisticsEnabled)
        {
            Accumulation[PixelIndex]  = Accumulation[PixelIndex] + vec4f(vec3f(RayStatistics.xyw), 1.0);
            Accumulation[MomentIndex] = Accumulation[MomentIndex] + vec4f(f32(RayStatistics.z), 0.0, 0.0, 0.0);
        }
        else
        {
            Accumulation[PixelIndex]  = Accumulation[PixelIndex] + vec4f(Accepted, 1.0);
            Accumulation[MomentIndex] = Accumulation[MomentIndex] + vec4f(Luminance * Luminance, 0.0, 0.0, 0.0);
        }
    }
    if (Direct) { Accumulation[TileIndex] = Sum; Accumulation[3u * PixelCount + TileIndex] = Moments; }
}

// 📝 Camera-centre ray of a pixel for an arbitrary camera (no subpixel offset, no lens).
fn PixelCentreDirection(Pixel: vec2f, Forward: vec3f, Right: vec3f, Up: vec3f) -> vec3f
{
    let Extent = vec2f(f32(Tracing.Extent.x), f32(Tracing.Extent.y));
    let Ndc    = (Pixel + vec2f(0.5)) / Extent * 2.0 - vec2f(1.0);
    return normalize(Forward + Ndc.x * Right - Ndc.y * Up);
}

// 📝 How many samples of history a surface may keep. Reprojection moves shading with the surface, which is right for
//    diffuse light but wrong for reflections; glossy and glass surfaces therefore keep a shorter history.
// 📝 History is carried by surface position, which is only right for view-independent shading. The sharpest lobe
//    decides (a clear coat over a rough base reflects like a mirror): ≤ 0.05 keeps no history, ≥ 0.4 keeps full capacity.
fn ResolveSharpestRoughness(MaterialSlot: u32) -> f32
{
    let Material  = Materials[MaterialSlot];
    let Category  = u32(Material.EtaCategory.w);
    var Roughness = select(Material.AlbedoRoughness.w, 0.0, Category == SurfaceSolidDielectric || Category == SurfaceThinDielectric);
    if (Material.KappaCoat.w > 0.0) { Roughness = min(Roughness, Material.ExitanceCoat.w); }
    return Roughness;
}

fn ResolveHistoryCapacity(MaterialSlot: u32) -> f32
{
    return Tracing.HistoryTuning.x * smoothstep(0.05, 0.4, ResolveSharpestRoughness(MaterialSlot));
}

// 📝 Pixel of the PREVIOUS camera whose centre ray passes through Point; x < 0 when Point is behind it or off screen.
fn ProjectIntoPreviousCamera(Point: vec3f) -> vec2f
{
    let Offset  = Point - Tracing.PreviousOrigin.xyz;
    let Depth   = dot(Offset, Tracing.PreviousForward.xyz);
    let Across  = dot(Offset, Tracing.PreviousRight.xyz) / dot(Tracing.PreviousRight.xyz, Tracing.PreviousRight.xyz);
    let Upward  = dot(Offset, Tracing.PreviousUp.xyz) / dot(Tracing.PreviousUp.xyz, Tracing.PreviousUp.xyz);
    let Extent  = vec2f(f32(Tracing.Extent.x), f32(Tracing.Extent.y));
    let Landing = (vec2f(Across / Depth, -Upward / Depth) + vec2f(1.0)) * 0.5 * Extent;
    if (Depth <= 0.0 || any(Landing < vec2f(0.0)) || any(Landing >= Extent)) { return vec2f(-1.0); }
    return Landing;
}

// 📝 What a perfect mirror at Surface reflects: category and, for surfaces, the material slot (lights: index + 1000).
fn ResolveMirrorIdentity(Surface: SurfaceSample, Direction: vec3f) -> vec2f
{
    let Reflected = reflect(Direction, Surface.ShadingNormal);
    let Hit       = TraceClosestHit(OffsetOrigin(Surface.Position, Surface.GeometricNormal, Reflected), Reflected, true);
    if (Hit.Category == HitAbsent) { return vec2f(0.0, 1.0e4); }
    var Identity = f32(Hit.Category) * 4096.0 + f32(Hit.PrimitiveIndex % 4096u);
    if (Hit.Category == HitTriangle || Hit.Category == HitSphere || Hit.Category == HitFloor)
    {
        Identity = f32(Hit.Category) * 4096.0 + f32(ResolveSurface(Surface.Position, Reflected, Hit).MaterialSlot);
    }
    return vec2f(Identity, Hit.Distance);
}

// 📝 Camera-motion reprojection, run once when the camera changes and before the sums are cleared. For each pixel of
//    the NEW camera: ① trace its centre ray, ② find the PREVIOUS pixel that saw the same thing, ③ verify by tracing that
//    previous pixel's centre ray, ④ carry its displayed radiance with weight min(samples, capacity).
//    Rough surfaces move with the surface point. Mirror-like surfaces (sharpest lobe ≤ 0.05, e.g. the coated floor)
//    move with the reflected object's virtual image — the point at the same total path length along the camera ray —
//    and are accepted only if the previous pixel shows the same surface reflecting the same thing.
//    Deterministic for a given camera path; the converged image never contains history (DisplayExposure.wgsl).
@compute @workgroup_size(8, 8, 1)
fn ReprojectHistory(@builtin(global_invocation_id) InvocationId: vec3u)
{
    if (InvocationId.x >= Tracing.Extent.x || InvocationId.y >= Tracing.Extent.y) { return; }
    let PixelCount    = Tracing.Extent.x * Tracing.Extent.y;
    let PixelIndex    = InvocationId.y * Tracing.Extent.x + InvocationId.x;
    let LightsVisible = Tracing.Tuning.z > 0.5;
    let Extent        = vec2f(f32(Tracing.Extent.x), f32(Tracing.Extent.y));
    let Footprint     = 2.0 * length(Tracing.CameraUp.xyz) / Extent.y;
    var Carried       = vec4f(0.0);

    // ① Current surface — also populates SurfaceFeatures so camera-motion frames skip the redundant Features pass.
    let Origin    = Tracing.CameraOrigin.xyz;
    let Direction = PixelCentreDirection(vec2f(InvocationId.xy), Tracing.CameraForward.xyz, Tracing.CameraRight.xyz, Tracing.CameraUp.xyz);
    let Hit       = TraceClosestHit(Origin, Direction, LightsVisible);
    let Travel    = select(Hit.Distance, 1.0e4, Hit.Category == HitAbsent);
    let Point     = Origin + Direction * Travel;
    let Material  = Hit.Category != HitAbsent && Hit.Category != HitLight;
    var Surface   : SurfaceSample;
    var Roughness = 0.0;
    var Sharp     = false;
    var Mirror    = vec2f(0.0);
    if (Material)
    {
        Surface   = ResolveSurface(Origin, Direction, Hit);
        Roughness = ResolveSharpestRoughness(Surface.MaterialSlot);
        Sharp     = Roughness <= 0.05;
        if (Sharp) { Mirror = ResolveMirrorIdentity(Surface, Direction); }
        let Packed = (Surface.MaterialSlot & 0xffffu) | (u32(clamp(Roughness, 0.0, 1.0) * 65535.0) << 16u);
        SurfaceFeatures[PixelIndex]              = vec4f(Surface.Position, Hit.Distance);
        SurfaceFeatures[PixelCount + PixelIndex] = vec4f(Surface.ShadingNormal, bitcast<f32>(Packed));
    }
    else if (Hit.Category == HitLight)
    {
        SurfaceFeatures[PixelIndex]              = vec4f(Point, Hit.Distance);
        SurfaceFeatures[PixelCount + PixelIndex] = vec4f(-Direction, bitcast<f32>(0xfffeu));
    }
    else
    {
        SurfaceFeatures[PixelIndex]              = vec4f(Point, 0.0);
        SurfaceFeatures[PixelCount + PixelIndex] = vec4f(-Direction, bitcast<f32>(0xffffu));
    }

    // ② Previous pixel: through the surface point, or through the virtual image for mirror-like surfaces.
    let Landing = ProjectIntoPreviousCamera(select(Point, Origin + Direction * (Travel + Mirror.y), Sharp));
    if (Landing.x >= 0.0)
    {
        // ③ Same surface (and, for mirrors, the same reflected object) seen by the previous pixel?
        let PreviousPixel     = vec2u(Landing);
        let PreviousOrigin    = Tracing.PreviousOrigin.xyz;
        let PreviousDirection = PixelCentreDirection(vec2f(PreviousPixel), Tracing.PreviousForward.xyz, Tracing.PreviousRight.xyz, Tracing.PreviousUp.xyz);
        let PreviousHit       = TraceClosestHit(PreviousOrigin, PreviousDirection, LightsVisible);
        var Accepted          = PreviousHit.Category == Hit.Category;
        var Capacity          = Tracing.HistoryTuning.x;
        if (Accepted && Material)
        {
            let PreviousSurface = ResolveSurface(PreviousOrigin, PreviousDirection, PreviousHit);
            Accepted = Surface.MaterialSlot == PreviousSurface.MaterialSlot && dot(Surface.GeometricNormal, PreviousSurface.GeometricNormal) > 0.9;
            if (Sharp)
            {
                let PreviousMirror = ResolveMirrorIdentity(PreviousSurface, PreviousDirection);
                Accepted = Accepted && PreviousMirror.x == Mirror.x && abs(PreviousMirror.y - Mirror.y) < 0.05 * Mirror.y + 0.05;
                Capacity = 0.5 * Tracing.HistoryTuning.x;
            }
            else
            {
                let PreviousPoint = PreviousOrigin + PreviousDirection * PreviousHit.Distance;
                Accepted = Accepted && distance(Point, PreviousPoint) < 2.0 * Footprint * max(Travel, PreviousHit.Distance) + 2.0e-3;
                Capacity = Tracing.HistoryTuning.x * smoothstep(0.05, 0.4, Roughness);
            }
        }
        else if (Accepted && Hit.Category == HitLight)
        {
            let PreviousPoint = PreviousOrigin + PreviousDirection * PreviousHit.Distance;
            Accepted = distance(Point, PreviousPoint) < 2.0 * Footprint * max(Travel, PreviousHit.Distance) + 2.0e-3;
        }

        // ④ Previous displayed radiance: exact sums plus whatever history was still blended in.
        if (Accepted)
        {
            let PreviousIndex = PreviousPixel.y * Tracing.Extent.x + PreviousPixel.x;
            let Sum           = Accumulation[PreviousIndex];
            let History       = select(vec4f(0.0), Accumulation[Tracing.Realtime.z + PreviousIndex], Tracing.HistoryTuning.y > 0.5);
            let HistoryWeight = max(History.w - Sum.w, 0.0);
            let Weight        = Sum.w + HistoryWeight;
            if (Weight > 0.0)
            {
                Carried = vec4f((Sum.rgb + History.rgb * HistoryWeight) / Weight, min(Weight, Capacity));
            }
        }
    }
    Accumulation[Tracing.Realtime.w + PixelIndex] = Carried;
}

// 📝 Denoiser guide, traced once per camera / scene: centre-ray position and distance, shading normal facing the
//    camera, material slot (0xffff background, 0xfffe softbox) and sharpest roughness (16-bit) packed into one word.
@compute @workgroup_size(8, 8, 1)
fn ResolveSurfaceFeatures(@builtin(global_invocation_id) InvocationId: vec3u)
{
    if (InvocationId.x >= Tracing.Extent.x || InvocationId.y >= Tracing.Extent.y) { return; }
    let PixelCount = Tracing.Extent.x * Tracing.Extent.y;
    let PixelIndex = InvocationId.y * Tracing.Extent.x + InvocationId.x;
    let Origin     = Tracing.CameraOrigin.xyz;
    let Direction  = PixelCentreDirection(vec2f(InvocationId.xy), Tracing.CameraForward.xyz, Tracing.CameraRight.xyz, Tracing.CameraUp.xyz);
    let Hit        = TraceClosestHit(Origin, Direction, Tracing.Tuning.z > 0.5);
    var Position   = Origin + Direction * 1.0e4;
    var Travel     = 0.0;
    var Normal     = -Direction;
    var Packed     = 0xffffu;
    if (Hit.Category == HitLight)
    {
        Position = Origin + Direction * Hit.Distance;
        Travel   = Hit.Distance;
        Packed   = 0xfffeu;
    }
    else if (Hit.Category != HitAbsent)
    {
        let Surface   = ResolveSurface(Origin, Direction, Hit);
        let Roughness = clamp(ResolveSharpestRoughness(Surface.MaterialSlot), 0.0, 1.0);
        Position = Surface.Position;
        Travel   = Hit.Distance;
        Normal   = Surface.ShadingNormal;
        Packed   = (Surface.MaterialSlot & 0xffffu) | (u32(Roughness * 65535.0) << 16u);
    }
    SurfaceFeatures[PixelIndex]              = vec4f(Position, Travel);
    SurfaceFeatures[PixelCount + PixelIndex] = vec4f(Normal, bitcast<f32>(Packed));
}

// 📝 Validation entry: writes QuadraturePair(k, p) for k < Extent.x, p < Extent.y so host code can compare bit-for-bit.
@compute @workgroup_size(64, 1, 1)
fn ExtractQuadrature(@builtin(global_invocation_id) InvocationId: vec3u)
{
    let Total = Tracing.Extent.x * Tracing.Extent.y;
    if (InvocationId.x >= Total) { return; }
    let SampleIndex = InvocationId.x % Tracing.Extent.x;
    let PairIndex   = InvocationId.x / Tracing.Extent.x;
    let Point = QuadraturePair(SampleIndex, PairIndex);
    Accumulation[InvocationId.x] = vec4f(Point, bitcast<f32>(SampleIndex), bitcast<f32>(PairIndex));
}
