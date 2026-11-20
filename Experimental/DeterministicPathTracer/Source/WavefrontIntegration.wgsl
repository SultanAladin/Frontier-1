/*====================================================================================================================================
                                                    WAVEFRONTINTEGRATION.WGSL
====================================================================================================================================*/
// 🧩 Deterministic lock-free wavefront path-tracing integrator:
//    Splits monolithic per-pixel transport into 6 compact, low-register compute stages connected via SoA queues and
//    indirect dispatches (Laine–Karras–Aila 2013), with carry-propagated 96-bit fixed-point (2^28 scale) atomic
//    accumulation so parallel bounce and shadow contributions sum strictly commutatively and bit-identically across runs.

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const π                        : f32 = 3.14159265358979;
const InvalidMedium            : u32 = 0xFFu;
const SceneStackCapacity       : u32 = 40u;
const ReturnMarker             : u32 = 0xFFFFFFFFu;
const EmptyLink                : u32 = 0xFFFFFFFFu;
const InstanceShift            : u32 = 20u;
const TriangleMask             : u32 = 0xFFFFFu;
const ThinDielectricBit        : u32 = 0x80000000u;
const MaterialSlotMask         : u32 = 0x7FFFFFFFu;
const InstanceStride           : u32 = 2u;
const UnusedMaterialSlot       : u32 = 0xFFFFu;
const DeltaRoughnessAlpha      : f32 = 0.0025;
const RayOffsetScale           : f32 = 2.0e-4;
const UnboundedDistance        : f32 = 1.0e30;

const SurfaceLayered           : u32 = 0u;
const SurfaceConductor         : u32 = 1u;
const SurfaceSolidDielectric   : u32 = 2u;
const SurfaceThinDielectric    : u32 = 3u;

const HitAbsent                : u32 = 0u;
const HitTriangle              : u32 = 1u;
const HitSphere                : u32 = 2u;
const HitFloor                 : u32 = 3u;
const HitLight                 : u32 = 4u;

const PairSubpixel             : u32 = 0u;
const PairLens                 : u32 = 1u;
const PairsPerDepth            : u32 = 4u;
const PairLight                : u32 = 0u;
const PairDiffuse              : u32 = 1u;
const PairSpecular             : u32 = 2u;
const PairCoat                 : u32 = 3u;
const PairPracticalBase        : u32 = 66u;
const LampLeafFlag             : u32 = 0x80000000u;
const ProfileVerticalSamples   : u32 = 128u;
const ProfileHorizontalSamples : u32 = 32u;
const SelectionCeiling         : f32 = 0.99999994;
const LampCutCapacity          : u32 = 8u;
const LampSplitRatio           : f32 = 0.05;
const GoldenFraction           : f32 = 0.618034;

const LobeConductor            : u32 = 0u;
const LobeSpecular             : u32 = 1u;
const LobeCoat                 : u32 = 2u;

// 📝 WaveControl atomic counter word offsets (5 counters — separate from WaveIndirect dispatch buffer so no buffer
//    is ever simultaneously bound as storage(read_write) and indirect in the same compute dispatch):
const CtrlReadRays             : u32 = 0u;
const CtrlWriteRays            : u32 = 1u;
const CtrlHits                 : u32 = 2u;
const CtrlShadows              : u32 = 3u;
const CtrlOverflow             : u32 = 4u;

// 📝 WaveIndirect dispatch argument word offsets (3 × vec3u = 36 B):
const IndirClosestX            : u32 = 0u;
const IndirShadeX              : u32 = 3u;
const IndirShadowX             : u32 = 6u;

const StagePrepareClosest      : u32 = 0u;
const StagePrepareShade        : u32 = 1u;
const StagePrepareShadow       : u32 = 2u;


//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct TracingUniforms
{
    CameraOrigin      : vec4f,
    CameraForward     : vec4f,
    CameraRight       : vec4f,
    CameraUp          : vec4f,
    EnvironmentTint   : vec4f,
    Extent            : vec4u,
    DepthLimits       : vec4u,
    SceneLayout       : vec4u,
    Tuning            : vec4f,
    PreviousOrigin    : vec4f,
    PreviousForward   : vec4f,
    PreviousRight     : vec4f,
    PreviousUp        : vec4f,
    Realtime          : vec4u,
    HistoryTuning     : vec4f,
    Geometry          : vec4u,
    Shading           : vec4f,
    Practical         : vec4u,
    SphereBounds      : vec4f,
}

struct WaveUniforms
{
    Batch             : vec4u,   // x = BatchStartTile, y = BatchTileCount, z = SampleIndex, w = StageMode
    Queues            : vec4u,   // x = QueueCapacity, y = ShadowCapacity, z = ReadSide (0/1), w = WriteSide (0/1)
}

struct WideVisit
{
    Distances         : vec4f,
    Counts            : vec4u,
    Links             : vec4u,
}

struct SortedChildren
{
    Distances         : vec4f,
    Entries           : vec4u,
}

struct SurfaceMaterial
{
    AlbedoRoughness   : vec4f,
    EtaCategory       : vec4f,
    KappaCoat         : vec4f,
    ExitanceCoat      : vec4f,
    AbsorptionIor     : vec4f,
    SpecularLevel     : vec4f,
}

struct AreaLight
{
    Centre            : vec4f,
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
    GeometricNormal   : vec3f,
    ShadingNormal     : vec3f,
    MaterialSlot      : u32,
    FrontFacing       : bool,
}

struct WaveRay
{
    Origin            : vec3f,
    PackedState       : u32,
    Direction         : vec3f,
    LocalTile         : u32,
    Throughput        : vec3f,
    LobePdf           : f32,
    TravelSinceLobe   : f32,
}

struct LobeEvaluation
{
    Diffuse           : vec3f,
    DiffusePdf        : f32,
    Specular          : vec3f,
    SpecularPdf       : f32,
    Coat              : vec3f,
    CoatPdf           : f32,
}

struct LampDraw
{
    Lamp              : u32,
    Pdf               : f32,
    Relative          : f32,
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    RESOURCES
//------------------------------------------------------------------------------------------------------------------------

@group(0) @binding(0) var<uniform>             Tracing           : TracingUniforms;
@group(0) @binding(1) var<storage, read>       BoundsEntries     : array<vec4u>;
@group(0) @binding(2) var<storage, read>       TrianglePositions : array<vec4f>;
@group(0) @binding(3) var<storage, read>       TriangleNormals   : array<vec4f>;
@group(0) @binding(4) var<storage, read>       Materials         : array<SurfaceMaterial>;
@group(0) @binding(5) var<storage, read>       LightRecords      : array<vec4f>;
@group(0) @binding(6) var<storage, read>       Spheres           : array<vec4f>;
@group(0) @binding(7) var                      VisibilityIds     : texture_2d<u32>;
@group(0) @binding(8) var                      BlueNoiseRanks    : texture_2d<u32>;

@group(1) @binding(0) var<uniform>             Wave              : WaveUniforms;
@group(1) @binding(1) var<storage, read_write> WaveControl       : array<atomic<u32>>;
@group(1) @binding(2) var<storage, read_write> WaveRays          : array<vec4f>;
@group(1) @binding(3) var<storage, read_write> WaveWork          : array<vec4f>;
@group(1) @binding(4) var<storage, read_write> SampleAccum       : array<atomic<u32>>;
@group(1) @binding(5) var<storage, read_write> Accumulation      : array<vec4f>;
@group(1) @binding(6) var<storage, read_write> WaveIndirect      : array<u32>;

var<private> PixelScramble   : u32;
var<private> BlueNoiseOrigin : vec2u;
var<private> BlueNoiseActive : bool;
var<private> LampCut         : array<u32, LampCutCapacity>;


//------------------------------------------------------------------------------------------------------------------------
//                                                    STATE & ACCUMULATOR HELPERS
//------------------------------------------------------------------------------------------------------------------------

fn PackRayState(Depth: u32, NonDeltaDepth: u32, Medium: u32) -> u32
{
    return (Depth & 0xFFu) | ((NonDeltaDepth & 0xFFu) << 8u) | ((Medium & 0xFFu) << 16u);
}

fn RayDepth(PackedState: u32) -> u32         { return PackedState & 0xFFu; }
fn RayNonDeltaDepth(PackedState: u32) -> u32 { return (PackedState >> 8u) & 0xFFu; }
fn RayMedium(PackedState: u32) -> u32        { return (PackedState >> 16u) & 0xFFu; }

fn ResolvePixelScramble(PixelCoordinate: vec2u) -> u32
{
    if (Tracing.Realtime.x != 1u) { return 0u; }
    return AvalancheKey((PixelCoordinate.x * 0x8da6b343u) ^ (PixelCoordinate.y * 0xd8163841u) ^ 0x2545f491u) | 1u;
}

fn ResolveBlueNoiseShift(PairIndex: u32) -> vec2u
{
    let Offset = vec2u((PairIndex * 3242174890u) >> 26u, (PairIndex * 2447445413u) >> 26u);
    let Texel  = vec2i(vec2u(BlueNoiseOrigin + Offset) & vec2u(63u));
    let Ranks  = textureLoad(BlueNoiseRanks, Texel, 0).x;
    return vec2u((Ranks & 0xfffu) << 20u, ((Ranks >> 16u) & 0xfffu) << 20u);
}

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

// 📝 Lock-free 96-bit unsigned fixed-point atomic add (scale = 2^28, resolution 3.725e-9, max range 2.95e20).
//    Exact associativity and commutativity guarantee bit-identical sums regardless of GPU thread scheduling order.
fn AtomicAddChannel96(Base: u32, Value: f32)
{
    if (Value != Value || Value >= 1.0e20 || Value <= -1.0e20)
    {
        atomicOr(&SampleAccum[Base + 2u], 0x80000000u);
        return;
    }
    if (!(Value > 0.0)) { return; }
    let HiF    = floor(Value * 1.4551915228366852e-11);
    let Rem1   = Value - HiF * 68719476736.0;
    let MidF   = floor(Rem1 * 0.0625);
    let Rem2   = Rem1 - MidF * 16.0;
    let LoF    = floor(Rem2 * 268435456.0);
    let AddLo  = u32(LoF);
    var AddMid = u32(MidF);
    var AddHi  = u32(HiF);
    if (AddLo != 0u)
    {
        let OldLo = atomicAdd(&SampleAccum[Base], AddLo);
        if (OldLo > 0xFFFFFFFFu - AddLo)
        {
            AddMid += 1u;
            if (AddMid == 0u) { AddHi += 1u; }
        }
    }
    if (AddMid != 0u)
    {
        let OldMid = atomicAdd(&SampleAccum[Base + 1u], AddMid);
        if (OldMid > 0xFFFFFFFFu - AddMid) { AddHi += 1u; }
    }
    if (AddHi != 0u)
    {
        atomicAdd(&SampleAccum[Base + 2u], AddHi);
    }
}

fn AccumulateRadiance96(LocalTile: u32, Radiance: vec3f)
{
    let Base = LocalTile * 9u;
    AtomicAddChannel96(Base,      Radiance.x);
    AtomicAddChannel96(Base + 3u, Radiance.y);
    AtomicAddChannel96(Base + 6u, Radiance.z);
}

// 📝 Queue layout in WaveRays (SoA per ping-pong side: geometry block [0..2*Cap), shading block [2*Cap..4*Cap)):
fn WriteWaveRay(Side: u32, Slot: u32, Ray: WaveRay)
{
    let Cap     = Wave.Queues.x;
    let GeomOff = Side * Cap * 4u + Slot * 2u;
    let AuxOff  = Side * Cap * 4u + Cap * 2u + Slot * 2u;
    WaveRays[GeomOff]      = vec4f(Ray.Origin, bitcast<f32>(Ray.PackedState));
    WaveRays[GeomOff + 1u] = vec4f(Ray.Direction, bitcast<f32>(Ray.LocalTile));
    WaveRays[AuxOff]       = vec4f(Ray.Throughput, Ray.LobePdf);
    WaveRays[AuxOff + 1u]  = vec4f(Ray.TravelSinceLobe, 0.0, 0.0, 0.0);
}

fn ReadWaveRay(Side: u32, Slot: u32) -> WaveRay
{
    let Cap     = Wave.Queues.x;
    let GeomOff = Side * Cap * 4u + Slot * 2u;
    let AuxOff  = Side * Cap * 4u + Cap * 2u + Slot * 2u;
    let G0 = WaveRays[GeomOff];
    let G1 = WaveRays[GeomOff + 1u];
    let A0 = WaveRays[AuxOff];
    let A1 = WaveRays[AuxOff + 1u];
    return WaveRay(G0.xyz, bitcast<u32>(G0.w), G1.xyz, bitcast<u32>(G1.w), A0.xyz, A0.w, A1.x);
}

fn PushNextWaveRay(Ray: WaveRay)
{
    let Peak = max(max(Ray.Throughput.x, Ray.Throughput.y), Ray.Throughput.z);
    if (Peak < Tracing.Tuning.x || RayDepth(Ray.PackedState) >= Tracing.DepthLimits.y) { return; }
    let Slot = atomicAdd(&WaveControl[CtrlWriteRays], 1u);
    if (Slot >= Wave.Queues.x) { atomicAdd(&WaveControl[CtrlOverflow], 1u); return; }
    WriteWaveRay(Wave.Queues.w, Slot, Ray);
}

// 📝 WaveWork layout:
//    [0 .. 3*QueueCapacity)                                : ShadingHit queue (3 vec4f per hit)
//    [3*QueueCapacity .. 3*QueueCapacity + 2*ShadowCap)    : ShadowGeom queue (2 vec4f per shadow ray)
//    [3*QueueCapacity + 2*ShadowCap .. + 3*ShadowCap)      : ShadowContrib queue (1 vec4f per shadow ray)
fn WriteShadingHit(Slot: u32, RayIndex: u32, Hit: SurfaceHit, Surface: SurfaceSample)
{
    let Base    = Slot * 3u;
    let Payload = select(Surface.MaterialSlot & 0xFFFFu, Hit.PrimitiveIndex & 0xFFFFu, Hit.Category == HitLight);
    let Packed  = Payload | (select(0u, 1u, Surface.FrontFacing) << 16u) | ((Hit.Category & 0xFFu) << 24u);
    WaveWork[Base]      = vec4f(Surface.Position, Hit.Distance);
    WaveWork[Base + 1u] = vec4f(Surface.GeometricNormal, bitcast<f32>(Packed));
    WaveWork[Base + 2u] = vec4f(Surface.ShadingNormal, bitcast<f32>(RayIndex));
}

fn PushShadingHit(RayIndex: u32, Hit: SurfaceHit, Surface: SurfaceSample)
{
    let Slot = atomicAdd(&WaveControl[CtrlHits], 1u);
    if (Slot >= Wave.Queues.x) { atomicAdd(&WaveControl[CtrlOverflow], 1u); return; }
    WriteShadingHit(Slot, RayIndex, Hit, Surface);
}

fn PushShadowRay(Origin: vec3f, Direction: vec3f, MaxDistance: f32, LocalTile: u32, Contribution: vec3f)
{
    let Slot = atomicAdd(&WaveControl[CtrlShadows], 1u);
    if (Slot >= Wave.Queues.y) { atomicAdd(&WaveControl[CtrlOverflow], 1u); return; }
    let GeomBase    = Wave.Queues.x * 3u + Slot * 2u;
    let ContribBase = Wave.Queues.x * 3u + Wave.Queues.y * 2u + Slot;
    WaveWork[GeomBase]      = vec4f(Origin, MaxDistance);
    WaveWork[GeomBase + 1u] = vec4f(Direction, bitcast<f32>(LocalTile));
    WaveWork[ContribBase]   = vec4f(Contribution, 0.0);
}

// 📝 Bijective 8×8 strip-tile mapping: every 64 consecutive tile indices form an exact 8×8 2D block (with zero padding
//    on non-multiples of 8), giving primary rays full 2D BVH4 L1 cache locality inside 64-thread 1D workgroups.
fn ResolveTileCoord(TileIndex: u32, TilesX: u32, TilesY: u32) -> vec2u
{
    let StripTiles = TilesX * 8u;
    let Strip      = TileIndex / StripTiles;
    let BaseY      = Strip * 8u;
    let StripH     = min(8u, TilesY - BaseY);
    let Local      = TileIndex - Strip * StripTiles;
    return vec2u(Local / StripH, BaseY + (Local % StripH));
}

fn ResolveTileKeyPixel(LocalTile: u32, SampleIndex: u32) -> vec2u
{
    let ScaleInt  = max(Tracing.Geometry.w, 1u);
    let TilesX    = (Tracing.Extent.x + ScaleInt - 1u) / ScaleInt;
    let TilesY    = (Tracing.Extent.y + ScaleInt - 1u) / ScaleInt;
    let TileCoord = ResolveTileCoord(Wave.Batch.x + LocalTile, TilesX, TilesY);
    if (ScaleInt == 1u) { return TileCoord; }
    let Subpixel = ScrambledQuadraturePair(SampleIndex, PairSubpixel, 0u, vec2u(0u));
    let Scale    = f32(ScaleInt);
    let ImagePos = vec2f(TileCoord) * Scale + Subpixel * Scale;
    return min(vec2u(ImagePos), TileCoord * ScaleInt + vec2u(ScaleInt - 1u));
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
    let Base        = NodeIndex * 4u;
    let Header      = BoundsEntries[Base];
    let BoundsXY    = BoundsEntries[Base + 1u];
    let BoundsZ     = BoundsEntries[Base + 2u];
    let Links       = BoundsEntries[Base + 3u];
    let NodeOrigin  = bitcast<vec3f>(Header.xyz);
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
    let Counts  = vec4u(BoundsZ.z & 0xFFFFu, BoundsZ.z >> 16u, BoundsZ.w & 0xFFFFu, BoundsZ.w >> 16u);
    return WideVisit(select(vec4f(UnboundedDistance), Entered, Present & (Entered <= Exited * 1.000001)), Counts, Links);
}

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

fn IntersectTriangle(Origin: vec3f, Direction: vec3f, TriangleIndex: u32, MaxDistance: f32) -> vec3f
{
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

fn IntersectTriangleShadow(Origin: vec3f, Direction: vec3f, TriangleIndex: u32, MaxDistance: f32) -> bool
{
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

fn SortChildren(Visit: WideVisit) -> SortedChildren
{
    var D = Visit.Distances;
    var E = (Visit.Links << vec4u(4u)) | Visit.Counts;
    var Swap = vec2<bool>(D.y < D.x, D.w < D.z);
    D = vec4f(select(D.x, D.y, Swap.x), select(D.y, D.x, Swap.x), select(D.z, D.w, Swap.y), select(D.w, D.z, Swap.y));
    E = vec4u(select(E.x, E.y, Swap.x), select(E.y, E.x, Swap.x), select(E.z, E.w, Swap.y), select(E.w, E.z, Swap.y));
    Swap = vec2<bool>(D.z < D.x, D.w < D.y);
    D = vec4f(select(D.x, D.z, Swap.x), select(D.y, D.w, Swap.y), select(D.z, D.x, Swap.x), select(D.w, D.y, Swap.y));
    E = vec4u(select(E.x, E.z, Swap.x), select(E.y, E.w, Swap.y), select(E.z, E.x, Swap.x), select(E.w, E.y, Swap.y));
    let Middle = D.z < D.y;
    D = vec4f(D.x, select(D.y, D.z, Middle), select(D.z, D.y, Middle), D.w);
    E = vec4u(E.x, select(E.y, E.z, Middle), select(E.z, E.y, Middle), E.w);
    return SortedChildren(D, E);
}

fn TraceClosestHit(Origin: vec3f, Direction: vec3f, LightsVisible: bool) -> SurfaceHit
{
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


//------------------------------------------------------------------------------------------------------------------------
//                                                    OPTICS & SHADING
//------------------------------------------------------------------------------------------------------------------------

fn EvaluateEnvironment(Direction: vec3f) -> vec3f
{
    let Elevation = Direction.y;
    var Radiance  = vec3f(0.0);
    let Category  = Tracing.SceneLayout.y;
    if (Category == 0u)
    {
        let Overhead = pow(max(Elevation, 0.0), 1.5);
        Radiance = mix(vec3f(0.020, 0.021, 0.024), vec3f(0.090, 0.095, 0.105), Overhead)
                 + vec3f(0.030, 0.024, 0.018) * exp(-abs(Elevation) * 9.0);
    }
    else if (Category == 1u)
    {
        let Gradient = (1.0 + 2.0 * max(Elevation, 0.0)) / 3.0;
        Radiance = select(vec3f(0.18, 0.17, 0.16) * 0.5, vec3f(0.80, 0.86, 0.95) * Gradient, Elevation > 0.0);
    }
    else
    {
        let Zenith  = pow(max(Elevation, 0.0), 0.6);
        let SkylineGlow = exp(-max(Elevation, 0.0) * 6.0) * (0.6 + 0.4 * max(dot(normalize(Direction.xz + vec2f(1.0e-6)), vec2f(-0.6, -0.8)), 0.0));
        Radiance = select(vec3f(0.05, 0.035, 0.03), vec3f(0.06, 0.10, 0.25) * Zenith + vec3f(1.1, 0.42, 0.14) * SkylineGlow, Elevation > 0.0);
    }
    return Radiance * Tracing.EnvironmentTint.rgb * Tracing.EnvironmentTint.w;
}

fn FresnelSchlick(F0: f32, Cosine: f32) -> f32
{
    let F90        = clamp(50.0 * F0, 0.0, 1.0);
    let Complement = 1.0 - clamp(Cosine, 0.0, 1.0);
    let Squared    = Complement * Complement;
    return F0 + (F90 - F0) * Squared * Squared * Complement;
}

fn FresnelDielectric(CosineIncidence: f32, η: f32) -> f32
{
    let SineTransmitted2 = (1.0 - CosineIncidence * CosineIncidence) / (η * η);
    if (SineTransmitted2 >= 1.0) { return 1.0; }
    let CosineTransmitted = sqrt(1.0 - SineTransmitted2);
    let Perpendicular = (CosineIncidence - η * CosineTransmitted) / (CosineIncidence + η * CosineTransmitted);
    let Parallel      = (η * CosineIncidence - CosineTransmitted) / (η * CosineIncidence + CosineTransmitted);
    return 0.5 * (Perpendicular * Perpendicular + Parallel * Parallel);
}

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

fn ThinSheetTransmittance(CosineIncidence: f32, Material: SurfaceMaterial) -> vec3f
{
    let Reflectance = FresnelDielectric(abs(CosineIncidence), Material.AbsorptionIor.w);
    let Transmitted = (1.0 - Reflectance) * (1.0 - Reflectance) / max(1.0 - Reflectance * Reflectance, 1.0e-6);
    return Material.AlbedoRoughness.rgb * Transmitted;
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

fn TangentBasis(Normal: vec3f) -> mat3x3f
{
    let Sign       = select(-1.0, 1.0, Normal.z >= 0.0);
    let Inverse    = -1.0 / (Sign + Normal.z);
    let Shear      = Normal.x * Normal.y * Inverse;
    let Tangent    = vec3f(1.0 + Sign * Normal.x * Normal.x * Inverse, Sign * Shear, -Sign * Normal.x);
    let Bitangent  = vec3f(Shear, Sign + Normal.y * Normal.y * Inverse, -Normal.y);
    return mat3x3f(Tangent, Bitangent, Normal);
}

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

fn EvaluateGgxLobe(CosineOut: f32, CosineIn: f32, CosineHalf: f32, α: f32) -> vec2f
{
    let D         = DistributionGgx(CosineHalf, α);
    let LambdaOut = SmithLambda(CosineOut, α);
    let LambdaIn  = SmithLambda(CosineIn, α);
    let G2        = 1.0 / (1.0 + LambdaOut + LambdaIn);
    let G1        = 1.0 / (1.0 + LambdaOut);
    return vec2f(D * G2 / (4.0 * CosineOut), D * G1 / (4.0 * CosineOut));
}

fn MaterialAlpha(PerceptualRoughness: f32) -> f32
{
    return PerceptualRoughness * PerceptualRoughness;
}

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

fn PowerHeuristic(Primary: f32, Secondary: f32) -> f32
{
    let P2 = Primary * Primary;
    let S2 = Secondary * Secondary;
    return select(1.0, P2 / (P2 + S2), P2 + S2 > 0.0);
}

fn OffsetOrigin(Position: vec3f, Normal: vec3f, Direction: vec3f) -> vec3f
{
    let Scale = RayOffsetScale * (1.0 + max(max(abs(Position.x), abs(Position.y)), abs(Position.z)) * 0.1);
    return Position + Normal * select(-Scale, Scale, dot(Normal, Direction) > 0.0);
}

fn CosSinSubClamped(CosA: f32, SinA: f32, CosB: f32, SinB: f32) -> vec2f
{
    if (CosA >= CosB) { return vec2f(1.0, 0.0); }
    return vec2f(CosA * CosB + SinA * SinB, max(0.0, SinA * CosB - CosA * SinB));
}

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

fn LightNodeImportance(Position: vec3f, Normal: vec3f, Node: u32) -> vec2f
{
    let Base  = Tracing.Practical.y + Node * 4u;
    let Lower = LightRecords[Base];
    let Upper = LightRecords[Base + 1u];
    let Link  = bitcast<u32>(Upper.w);
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
    let Angles    = LightRecords[Base + 3u];
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

fn EmitLightQuadratureShadows(Surface: SurfaceSample, Material: SurfaceMaterial, Throughput: vec3f, LocalTile: u32,
                              ωo: vec3f, SampleIndex: u32, Depth: u32, LobesContinue: bool)
{
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
            LightPdf = Distance2 * Draw.Pdf;
            Emitted  = LightRecords[Base + 3u].rgb * Relative;
            Delta    = true;
        }

        let Lobes = EvaluateLobes(Material, Surface.ShadingNormal, Surface.GeometricNormal, ωo, ωi);
        let ContinuedPdf = select(vec3f(0.0), vec3f(Lobes.DiffusePdf, Lobes.SpecularPdf, Lobes.CoatPdf), LobesContinue);
        let Combined = Lobes.Diffuse  * PowerHeuristic(LightPdf, ContinuedPdf.x)
                     + Lobes.Specular * PowerHeuristic(LightPdf, ContinuedPdf.y)
                     + Lobes.Coat     * PowerHeuristic(LightPdf, ContinuedPdf.z);
        let Weighted = select(Combined, Lobes.Diffuse + Lobes.Specular + Lobes.Coat, Delta);
        if (max(max(Weighted.x, Weighted.y), Weighted.z) <= 0.0) { continue; }

        let Contribution = Throughput * (Emitted * Weighted / LightPdf);
        if (max(max(Contribution.x, Contribution.y), Contribution.z) <= 0.0) { continue; }
        let Origin = OffsetOrigin(Surface.Position, Surface.GeometricNormal, ωi);
        PushShadowRay(Origin, ωi, Distance * (1.0 - 1.0e-4), LocalTile, Contribution);
    }
}

fn ReflectionLobeFresnel(Material: SurfaceMaterial, Lobe: u32, CosineOut: f32, CosineIn: f32, CosineHalfOut: f32, CoatOut: f32) -> vec3f
{
    if (Lobe == LobeConductor) { return FresnelConductor(CosineHalfOut, Material.EtaCategory.rgb, Material.KappaCoat.rgb); }
    let CoatWeight = Material.KappaCoat.w;
    if (Lobe == LobeCoat) { return vec3f(CoatWeight * FresnelSchlick(0.04, CosineHalfOut)); }
    let CoatAttenuation = select(1.0, CoatOut * (1.0 - CoatWeight * FresnelSchlick(0.04, CosineIn)), CoatWeight > 0.0);
    return vec3f(CoatAttenuation * FresnelSchlick(Material.SpecularLevel.x, CosineHalfOut));
}

fn EmitReflectionLobe(Surface: SurfaceSample, Ray: WaveRay, Material: SurfaceMaterial, ωo: vec3f, α: f32,
                      SampleIndex: u32, PairSlot: u32, Lobe: u32, Basis: mat3x3f, ViewLocal: vec3f, CoatOut: f32)
{
    let Depth         = RayDepth(Ray.PackedState);
    let NonDeltaDepth = RayNonDeltaDepth(Ray.PackedState);
    let Medium        = RayMedium(Ray.PackedState);
    if (Depth + 1u >= Tracing.DepthLimits.y) { return; }
    let Normal        = Surface.ShadingNormal;
    let CosineOut     = max(dot(Normal, ωo), 1.0e-6);
    if (α < DeltaRoughnessAlpha)
    {
        let ωi = reflect(-ωo, Normal);
        if (dot(ωi, Surface.GeometricNormal) <= 0.0) { return; }
        let Weight = ReflectionLobeFresnel(Material, Lobe, CosineOut, CosineOut, CosineOut, CoatOut);
        PushNextWaveRay(WaveRay(OffsetOrigin(Surface.Position, Surface.GeometricNormal, ωi),
                                PackRayState(Depth + 1u, NonDeltaDepth, Medium), ωi, Ray.LocalTile,
                                Ray.Throughput * Weight, 0.0, 0.0));
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
    let Masking   = (1.0 + LambdaOut) / (1.0 + LambdaOut + LambdaIn);
    let Pdf       = DistributionGgx(max(dot(Normal, HalfVector), 0.0), α) / ((1.0 + LambdaOut) * 4.0 * CosineOut);
    let Weight    = ReflectionLobeFresnel(Material, Lobe, CosineOut, CosineIn, max(dot(ωo, HalfVector), 0.0), CoatOut) * Masking;
    PushNextWaveRay(WaveRay(OffsetOrigin(Surface.Position, Surface.GeometricNormal, ωi),
                            PackRayState(Depth + 1u, NonDeltaDepth + 1u, Medium), ωi, Ray.LocalTile,
                            Ray.Throughput * Weight, Pdf, 0.0));
}

fn TraceShadowTransmittance(Origin: vec3f, Direction: vec3f, Distance: f32) -> vec3f
{
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
                    let Material   = Materials[InstanceMaterialSlot(Instance, PackedSlot & MaterialSlotMask)];
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

fn AcceptSample(Radiance: vec3f) -> vec3f
{
    let Finite = all(Radiance == Radiance) && all(abs(Radiance) < vec3f(1.0e20));
    return select(vec3f(0.0), Radiance, Finite);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    WAVEFRONT ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

// ① Fused Depth-0 primary generation + closest-hit trace in 8×8 spatially coherent blocks: eliminates the Depth-0
//    ray-queue round-trip and indirect barrier, emitting ShadingHits directly for Depth-0 shading.
@compute @workgroup_size(64, 1, 1)
fn WaveGeneratePrimary(@builtin(global_invocation_id) InvocationId: vec3u)
{
    let LocalTile = InvocationId.x;
    if (LocalTile >= Wave.Batch.y) { return; }
    if (LocalTile == 0u)
    {
        atomicStore(&WaveControl[CtrlHits], Wave.Batch.y);
    }

    let SampleIndex = Wave.Batch.z;
    let ScaleInt    = max(Tracing.Geometry.w, 1u);
    let Scale       = f32(ScaleInt);
    let TilesX      = (Tracing.Extent.x + ScaleInt - 1u) / ScaleInt;
    let TilesY      = (Tracing.Extent.y + ScaleInt - 1u) / ScaleInt;
    let TileCoord   = ResolveTileCoord(Wave.Batch.x + LocalTile, TilesX, TilesY);
    let Extent      = vec2f(f32(Tracing.Extent.x), f32(Tracing.Extent.y));
    let Subpixel    = ScrambledQuadraturePair(SampleIndex, PairSubpixel, 0u, vec2u(0u));
    let ImagePos    = vec2f(TileCoord) * Scale + Subpixel * Scale;
    let KeyPixel    = select(min(vec2u(ImagePos), TileCoord * ScaleInt + vec2u(ScaleInt - 1u)), TileCoord, ScaleInt == 1u);

    let Ndc       = (ImagePos / Extent) * 2.0 - vec2f(1.0);
    var Origin    = Tracing.CameraOrigin.xyz;
    var Direction = normalize(Tracing.CameraForward.xyz + Ndc.x * Tracing.CameraRight.xyz - Ndc.y * Tracing.CameraUp.xyz);

    if (Tracing.CameraOrigin.w > 0.0)
    {
        PixelScramble   = ResolvePixelScramble(KeyPixel);
        BlueNoiseActive = Tracing.Realtime.x == 2u;
        BlueNoiseOrigin = KeyPixel;
        let Lens        = ConcentricDisk(QuadraturePair(SampleIndex, PairLens)) * Tracing.CameraOrigin.w;
        let FocusPoint  = Origin + Direction * (Tracing.CameraForward.w / dot(Direction, Tracing.CameraForward.xyz));
        Origin    = Origin + normalize(Tracing.CameraRight.xyz) * Lens.x + normalize(Tracing.CameraUp.xyz) * Lens.y;
        Direction = normalize(FocusPoint - Origin);
    }

    var Hit = SurfaceHit(0.0, HitAbsent, 0u, vec2f(0.0));
    if (Tracing.Realtime.y == 1u && Tracing.CameraOrigin.w <= 0.0)
    {
        Hit = ResolveRasterizedPrimary(Origin, Direction, TileCoord);
    }
    if (Hit.Distance <= 0.0)
    {
        Hit = TraceClosestHit(Origin, Direction, Tracing.Tuning.z > 0.5);
    }

    let PackedState = PackRayState(0u, 0u, InvalidMedium);
    WriteWaveRay(Wave.Queues.z, LocalTile, WaveRay(Origin, PackedState, Direction, LocalTile, vec3f(1.0), 0.0, 0.0));
    var Surface = SurfaceSample(Origin, vec3f(0.0, 1.0, 0.0), vec3f(0.0, 1.0, 0.0), 0u, true);
    if (Hit.Category != HitAbsent && Hit.Category != HitLight)
    {
        Surface = ResolveSurface(Origin, Direction, Hit);
    }
    WriteShadingHit(LocalTile, LocalTile, Hit, Surface);
}

// ② 1-thread stage barrier translating queue counts into indirect dispatch workgroup counts.
@compute @workgroup_size(1, 1, 1)
fn WavePrepareStage()
{
    let Mode = Wave.Batch.w;
    if (Mode == StagePrepareClosest)
    {
        let Count = min(atomicLoad(&WaveControl[CtrlReadRays]), Wave.Queues.x);
        WaveIndirect[IndirClosestX]      = (Count + 63u) / 64u;
        WaveIndirect[IndirClosestX + 1u] = 1u;
        WaveIndirect[IndirClosestX + 2u] = 1u;
        atomicStore(&WaveControl[CtrlWriteRays], 0u);
        atomicStore(&WaveControl[CtrlShadows], 0u);
    }
    else if (Mode == StagePrepareShade)
    {
        let Count = min(atomicLoad(&WaveControl[CtrlHits]), Wave.Queues.x);
        WaveIndirect[IndirShadeX]      = (Count + 63u) / 64u;
        WaveIndirect[IndirShadeX + 1u] = 1u;
        WaveIndirect[IndirShadeX + 2u] = 1u;
        atomicStore(&WaveControl[CtrlReadRays], 0u);
    }
    else
    {
        let Shadows = min(atomicLoad(&WaveControl[CtrlShadows]), Wave.Queues.y);
        WaveIndirect[IndirShadowX]      = (Shadows + 63u) / 64u;
        WaveIndirect[IndirShadowX + 1u] = 1u;
        WaveIndirect[IndirShadowX + 2u] = 1u;
        let NextRays = min(atomicLoad(&WaveControl[CtrlWriteRays]), Wave.Queues.x);
        atomicStore(&WaveControl[CtrlReadRays], NextRays);
        atomicStore(&WaveControl[CtrlHits], 0u);
    }
}

// ③ Closest-hit traversal stage (Depth >= 1): reads only 32-byte SoA ray geometry; resolves surface normals while triangle data is L1-hot.
@compute @workgroup_size(64, 1, 1)
fn WaveTraceClosest(@builtin(global_invocation_id) InvocationId: vec3u)
{
    let RayIndex = InvocationId.x;
    let RayTotal = min(atomicLoad(&WaveControl[CtrlReadRays]), Wave.Queues.x);
    if (RayIndex >= RayTotal) { return; }

    let GeomOff       = Wave.Queues.z * Wave.Queues.x * 4u + RayIndex * 2u;
    let G0            = WaveRays[GeomOff];
    let G1            = WaveRays[GeomOff + 1u];
    let Origin        = G0.xyz;
    let Direction     = G1.xyz;
    let Hit           = TraceClosestHit(Origin, Direction, true);

    var Surface = SurfaceSample(Origin, vec3f(0.0, 1.0, 0.0), vec3f(0.0, 1.0, 0.0), 0u, true);
    if (Hit.Category != HitAbsent && Hit.Category != HitLight)
    {
        Surface = ResolveSurface(Origin, Direction, Hit);
    }
    PushShadingHit(RayIndex, Hit, Surface);
}

// ④ Material shading, direct light quadrature shadow emission, and deterministic lobe/Fresnel bounce emission.
@compute @workgroup_size(64, 1, 1)
fn WaveShadeScatter(@builtin(global_invocation_id) InvocationId: vec3u)
{
    let HitIndex = InvocationId.x;
    let HitTotal = min(atomicLoad(&WaveControl[CtrlHits]), Wave.Queues.x);
    if (HitIndex >= HitTotal) { return; }

    let Base     = HitIndex * 3u;
    let W0       = WaveWork[Base];
    let W1       = WaveWork[Base + 1u];
    let W2       = WaveWork[Base + 2u];
    let Packed   = bitcast<u32>(W1.w);
    let Category = (Packed >> 24u) & 0xFFu;
    let RayIndex = bitcast<u32>(W2.w);

    var Ray = ReadWaveRay(Wave.Queues.z, RayIndex);
    let Medium = RayMedium(Ray.PackedState);
    if (Medium != InvalidMedium)
    {
        let Absorption = Materials[Medium].AbsorptionIor.rgb;
        Ray.Throughput = Ray.Throughput * exp(-Absorption * min(W0.w, 100.0));
    }

    if (Category == HitAbsent)
    {
        AccumulateRadiance96(Ray.LocalTile, Ray.Throughput * EvaluateEnvironment(Ray.Direction));
        return;
    }

    if (Category == HitLight)
    {
        let Light       = FetchAreaLight(Packed & 0xFFFFu);
        let CosineLight = -dot(Ray.Direction, LightNormal(Light));
        if (CosineLight > 0.0)
        {
            let Travel   = Ray.TravelSinceLobe + W0.w;
            let LightPdf = Travel * Travel / (CosineLight * Light.Centre.w);
            let Weight   = select(1.0, PowerHeuristic(Ray.LobePdf, LightPdf), Ray.LobePdf > 0.0);
            AccumulateRadiance96(Ray.LocalTile, Ray.Throughput * Light.Radiance.rgb * Weight);
        }
        return;
    }

    let Surface = SurfaceSample(W0.xyz, W1.xyz, W2.xyz, Packed & 0xFFFFu, ((Packed >> 16u) & 1u) != 0u);
    let Material      = Materials[Surface.MaterialSlot];
    let MatCategory   = u32(Material.EtaCategory.w);
    let ωo            = -Ray.Direction;
    let Normal        = Surface.ShadingNormal;
    let CosineOut     = max(dot(Normal, ωo), 1.0e-6);
    let Depth         = RayDepth(Ray.PackedState);
    let NonDeltaDepth = RayNonDeltaDepth(Ray.PackedState);
    let SampleIndex   = Wave.Batch.z;

    if (Surface.FrontFacing)
    {
        let Exitance = Ray.Throughput * Material.ExitanceCoat.rgb;
        if (max(max(Exitance.x, Exitance.y), Exitance.z) > 0.0)
        {
            AccumulateRadiance96(Ray.LocalTile, Exitance);
        }
    }

    if (MatCategory == SurfaceSolidDielectric)
    {
        if (Depth + 1u >= Tracing.DepthLimits.y) { return; }
        let Ior       = Material.AbsorptionIor.w;
        let η         = select(1.0 / Ior, Ior, Surface.FrontFacing);
        let Cosine    = clamp(dot(Surface.GeometricNormal, ωo), 0.0, 1.0);
        let Reflected = FresnelDielectric(Cosine, η);
        let ωr        = reflect(-ωo, Surface.GeometricNormal);
        PushNextWaveRay(WaveRay(OffsetOrigin(Surface.Position, Surface.GeometricNormal, ωr),
                                PackRayState(Depth + 1u, NonDeltaDepth, Medium), ωr, Ray.LocalTile,
                                Ray.Throughput * Reflected, 0.0, 0.0));
        if (Reflected < 1.0)
        {
            let ωt          = refract(-ωo, Surface.GeometricNormal, 1.0 / η);
            let InnerMedium = select(InvalidMedium, Surface.MaterialSlot, Surface.FrontFacing);
            PushNextWaveRay(WaveRay(OffsetOrigin(Surface.Position, Surface.GeometricNormal, ωt),
                                    PackRayState(Depth + 1u, NonDeltaDepth, InnerMedium), ωt, Ray.LocalTile,
                                    Ray.Throughput * (1.0 - Reflected), 0.0, 0.0));
        }
        return;
    }

    if (MatCategory == SurfaceThinDielectric)
    {
        if (Depth + 1u >= Tracing.DepthLimits.y) { return; }
        let Cosine      = clamp(dot(Surface.GeometricNormal, ωo), 0.0, 1.0);
        let Reflectance = FresnelDielectric(Cosine, Material.AbsorptionIor.w);
        let Sheet       = Reflectance + (1.0 - Reflectance) * (1.0 - Reflectance) * Reflectance / max(1.0 - Reflectance * Reflectance, 1.0e-6);
        let ωr          = reflect(-ωo, Surface.GeometricNormal);
        PushNextWaveRay(WaveRay(OffsetOrigin(Surface.Position, Surface.GeometricNormal, ωr),
                                PackRayState(Depth + 1u, NonDeltaDepth, Medium), ωr, Ray.LocalTile,
                                Ray.Throughput * Sheet, 0.0, 0.0));
        PushNextWaveRay(WaveRay(OffsetOrigin(Surface.Position, Surface.GeometricNormal, Ray.Direction),
                                PackRayState(Depth + 1u, NonDeltaDepth, Medium), Ray.Direction, Ray.LocalTile,
                                Ray.Throughput * ThinSheetTransmittance(Cosine, Material), Ray.LobePdf, Ray.TravelSinceLobe + W0.w));
        return;
    }

    // 📝 Set pixel decorrelation keys only when layered/conductor shading draws quadrature pairs.
    let KeyPixel    = ResolveTileKeyPixel(Ray.LocalTile, SampleIndex);
    PixelScramble   = ResolvePixelScramble(KeyPixel);
    BlueNoiseActive = Tracing.Realtime.x == 2u;
    BlueNoiseOrigin = KeyPixel;

    let CanContinue   = Depth + 1u < Tracing.DepthLimits.y;
    let LobesContinue = NonDeltaDepth < Tracing.DepthLimits.x && CanContinue;
    let α             = max(MaterialAlpha(Material.AlbedoRoughness.w), 1.0e-4);

    if (!(MatCategory == SurfaceConductor && α < DeltaRoughnessAlpha))
    {
        EmitLightQuadratureShadows(Surface, Material, Ray.Throughput, Ray.LocalTile, ωo, SampleIndex, Depth, LobesContinue);
    }
    if (!CanContinue) { return; }

    let NeedBasis = LobesContinue && (MatCategory != SurfaceConductor || α >= DeltaRoughnessAlpha);
    var Basis     = mat3x3f(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 1.0, 0.0), vec3f(0.0, 0.0, 1.0));
    var ViewLocal = vec3f(0.0, 0.0, 1.0);
    if (NeedBasis)
    {
        Basis     = TangentBasis(Normal);
        ViewLocal = transpose(Basis) * ωo;
    }

    if (MatCategory == SurfaceConductor)
    {
        EmitReflectionLobe(Surface, Ray, Material, ωo, α, SampleIndex, PairSpecular, LobeConductor, Basis, ViewLocal, 1.0);
        return;
    }

    let CoatWeight = Material.KappaCoat.w;
    let CoatOut    = select(1.0, 1.0 - CoatWeight * FresnelSchlick(0.04, CosineOut), CoatWeight > 0.0);
    let F0         = Material.SpecularLevel.x;
    if (CoatWeight > 0.0)
    {
        let CoatAlpha = max(MaterialAlpha(Material.ExitanceCoat.w), 1.0e-4);
        EmitReflectionLobe(Surface, Ray, Material, ωo, CoatAlpha, SampleIndex, PairCoat, LobeCoat, Basis, ViewLocal, CoatOut);
    }
    if (F0 > 0.0)
    {
        EmitReflectionLobe(Surface, Ray, Material, ωo, α, SampleIndex, PairSpecular, LobeSpecular, Basis, ViewLocal, CoatOut);
    }

    if (LobesContinue)
    {
        let SpecularOut  = select(1.0, 1.0 - FresnelSchlick(F0, CosineOut), F0 > 0.0);
        let BaseTransfer = SpecularOut * CoatOut;
        let MaxDiffuse   = Ray.Throughput * Material.AlbedoRoughness.rgb * BaseTransfer;
        if (max(max(MaxDiffuse.x, MaxDiffuse.y), MaxDiffuse.z) >= Tracing.Tuning.x)
        {
            let ωi       = Basis * CosineHemisphere(DepthPair(SampleIndex, Depth, PairDiffuse));
            let CosineIn = dot(Normal, ωi);
            if (CosineIn > 0.0 && dot(ωi, Surface.GeometricNormal) > 0.0)
            {
                let CoatIn   = select(1.0, 1.0 - CoatWeight * FresnelSchlick(0.04, CosineIn), CoatWeight > 0.0);
                let SpecIn   = select(1.0, 1.0 - FresnelSchlick(F0, CosineIn), F0 > 0.0);
                let Transfer = BaseTransfer * SpecIn * CoatIn;
                PushNextWaveRay(WaveRay(OffsetOrigin(Surface.Position, Surface.GeometricNormal, ωi),
                                        PackRayState(Depth + 1u, NonDeltaDepth + 1u, Medium), ωi, Ray.LocalTile,
                                        Ray.Throughput * Material.AlbedoRoughness.rgb * Transfer, CosineIn / π, 0.0));
            }
        }
    }
}

// ⑤ Dedicated shadow-ray traversal stage: reads only 32-byte shadow geometry; skips contribution load and atomics when occluded.
@compute @workgroup_size(64, 1, 1)
fn WaveTraceShadow(@builtin(global_invocation_id) InvocationId: vec3u)
{
    let ShadowIndex = InvocationId.x;
    let ShadowTotal = min(atomicLoad(&WaveControl[CtrlShadows]), Wave.Queues.y);
    if (ShadowIndex >= ShadowTotal) { return; }

    let GeomBase      = Wave.Queues.x * 3u + ShadowIndex * 2u;
    let S0            = WaveWork[GeomBase];
    let S1            = WaveWork[GeomBase + 1u];
    let Transmittance = TraceShadowTransmittance(S0.xyz, S1.xyz, S0.w);
    if (Transmittance.x <= 0.0 && Transmittance.y <= 0.0 && Transmittance.z <= 0.0) { return; }

    let ContribBase  = Wave.Queues.x * 3u + Wave.Queues.y * 2u + ShadowIndex;
    let Contribution = WaveWork[ContribBase].rgb;
    AccumulateRadiance96(bitcast<u32>(S1.w), Contribution * Transmittance);
}

// ⑥ Finalizes one sample's 96-bit fixed-point accumulator into the exact IEEE-754 sums and luminance moments.
@compute @workgroup_size(64, 1, 1)
fn WaveFinalizeSample(@builtin(global_invocation_id) InvocationId: vec3u)
{
    let LocalTile = InvocationId.x;
    if (LocalTile >= Wave.Batch.y) { return; }

    let KeyPixel = ResolveTileKeyPixel(LocalTile, Wave.Batch.z);
    if (KeyPixel.x >= Tracing.Extent.x || KeyPixel.y >= Tracing.Extent.y) { return; }

    let Base  = LocalTile * 9u;
    let RLo   = atomicLoad(&SampleAccum[Base]);
    let RMid  = atomicLoad(&SampleAccum[Base + 1u]);
    let RHi   = atomicLoad(&SampleAccum[Base + 2u]);
    let GLo   = atomicLoad(&SampleAccum[Base + 3u]);
    let GMid  = atomicLoad(&SampleAccum[Base + 4u]);
    let GHi   = atomicLoad(&SampleAccum[Base + 5u]);
    let BLo   = atomicLoad(&SampleAccum[Base + 6u]);
    let BMid  = atomicLoad(&SampleAccum[Base + 7u]);
    let BHi   = atomicLoad(&SampleAccum[Base + 8u]);

    let Poisoned = ((RHi | GHi | BHi) & 0x80000000u) != 0u;
    let Raw      = vec3f(
        f32(RHi) * 68719476736.0 + f32(RMid) * 16.0 + f32(RLo) * 3.725290298461914e-9,
        f32(GHi) * 68719476736.0 + f32(GMid) * 16.0 + f32(GLo) * 3.725290298461914e-9,
        f32(BHi) * 68719476736.0 + f32(BMid) * 16.0 + f32(BLo) * 3.725290298461914e-9
    );
    let Accepted  = select(AcceptSample(Raw), vec3f(0.0), Poisoned);
    let Luminance = dot(Accepted, vec3f(0.2126, 0.7152, 0.0722));

    let PixelCount  = Tracing.Extent.x * Tracing.Extent.y;
    let PixelIndex  = KeyPixel.y * Tracing.Extent.x + KeyPixel.x;
    let MomentIndex = 3u * PixelCount + PixelIndex;
    Accumulation[PixelIndex]  = Accumulation[PixelIndex] + vec4f(Accepted, 1.0);
    Accumulation[MomentIndex] = Accumulation[MomentIndex] + vec4f(Luminance * Luminance, 0.0, 0.0, 0.0);
}
