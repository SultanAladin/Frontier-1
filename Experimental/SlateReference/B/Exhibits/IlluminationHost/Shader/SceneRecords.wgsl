//============================================================================================================================================
//                                                            SCENERECORDS.WGSL
//============================================================================================================================================
// 🧩 Record layouts shared by every program — frame, occupant, surface card and material — matching the host encoders.

//------------------------------------------------------------------------------------------------------------------------
//                                                     FRAME RECORD
//------------------------------------------------------------------------------------------------------------------------



struct FrameRecord
{
    ViewProjection                   : mat4x4f,   // [-]    jittered, this frame
    InverseViewProjection            : mat4x4f,   // [-]    inverse of the jittered projection
    UnjitteredViewProjection         : mat4x4f,   // [-]    this frame, no sub-pixel offset
    PreviousUnjitteredViewProjection : mat4x4f,   // [-]    previous frame, no sub-pixel offset
    View                             : mat4x4f,   // [-]    world → view
    CameraPosition                   : vec3f,     // [m]
    FrameIndex                       : u32,       // [-]    monotonic since bring-up
    SunDirection                     : vec3f,     // [-]    unit, toward the light
    ElapsedSeconds                   : f32,       // [s]
    SunIlluminance                   : vec3f,     // [lx·k] after atmospheric transmittance
    SkyScale                         : f32,       // [-]    multiplies the sky-view radiance
    ScreenExtent                     : vec2f,     // [px]
    InverseScreenExtent              : vec2f,     // [1/px]
    TraceExtent                      : vec2f,     // [px]   resampling resolution
    InverseTraceExtent               : vec2f,     // [1/px]
    Jitter                           : vec2f,     // [ndc]  sub-pixel offset folded into ViewProjection
    TraceScale                       : u32,       // [-]    screen pixels per resampling pixel along one axis
    Features                         : u32,       // [-]    FeatureBit mask
    InspectionMode                   : u32,       // [-]    display inspection selector
    TemporalTallyLimit               : f32,       // [-]    M clamp for temporal reuse
    SpatialRadius                    : f32,       // [px]   resampling-space radius of spatial reuse
    SpatialCount                     : u32,       // [-]    spatial candidates per pixel
    NightSkyRadiance                 : vec3f,     // [-]    floor radiance of the night sky
    EmissionScale                    : f32,       // [-]    multiplies every authored emission
    SliceHeight                      : f32,       // [m]    inspection slice through the distance field
    ExposureCompensation             : f32,       // [EV]
    IndirectScale                    : f32,       // [-]    multiplies the resolved indirect term
    DistanceInspection               : u32,       // [-]    distance inspection selector
    FocusPosition                    : vec3f,     // [m]    centre of the scene distance field cascades
    InsetEnabled                     : u32,       // [-]    draw the distance slice inset
    FireLightPosition                : vec3f,     // [m]    power-weighted centre of the burning tyres
    FireLightIntensity               : f32,       // [-]    point intensity; 0 when nothing burns
    FireLightColour                  : vec3f,     // [-]    chromaticity of the flame light
    FireLightReach                   : f32,       // [m]    influence radius
    CameraNear                       : f32,       // [m]    near plane of the reversed infinite projection; device depth = CameraNear / view depth
}

// 📝 FeatureBit — the toggles the control panel exposes, read by the programs that honour them.
const FeatureIndirect          : u32 = 1u;
const FeatureTemporalReuse     : u32 = 2u;
const FeatureSpatialReuse      : u32 = 4u;
const FeatureVisibilityReuse   : u32 = 8u;
const FeatureVarianceKernel    : u32 = 16u;
const FeatureReflection        : u32 = 32u;
const FeatureCardRadiosity     : u32 = 64u;
const FeatureTemporalAntialias : u32 = 128u;
const FeatureDirectShadow      : u32 = 256u;
const FeatureAutoExposure      : u32 = 512u;
const FeatureRadianceRefresh   : u32 = 1024u;
const FeatureTemporalRestart   : u32 = 2048u;   // [-] set for one frame: every temporal tally restarts
const FeatureSmokeComposite    : u32 = 4096u;   // [-] smoke and plume composited over the recombined radiance
const FeatureScreenTraversal   : u32 = 8192u;   // [-] master gate: screen traces ahead of the scene distance field (default off)
const FeatureScreenIndirect    : u32 = 16384u;  // [-] screen traces for ReSTIR GI candidates (needs the master gate)
const FeatureScreenReflection  : u32 = 32768u;  // [-] screen traces for reflection rays (needs the master gate)
const FeatureTriangleReflection: u32 = 65536u;  // [-] reflection rays trace the triangle hierarchy after the screen (default off)

// 📝 A screen trace runs only when the master gate and its consumer's bit are both set.
fn ScreenConsumerEnabled(Frame: FrameRecord, Consumer: u32) -> bool
{
    return FeatureEnabled(Frame, FeatureScreenTraversal) && FeatureEnabled(Frame, Consumer);
}

fn FeatureEnabled(Frame: FrameRecord, Bit: u32) -> bool
{
    return (Frame.Features & Bit) != 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   OCCUPANT RECORD
//------------------------------------------------------------------------------------------------------------------------

struct OccupantRecord
{
    LocalToWorld         : mat4x4f,   // [-]
    WorldToLocal         : mat4x4f,   // [-]
    PreviousLocalToWorld : mat4x4f,   // [-]
    PreviousWorldToLocal : mat4x4f,   // [-]
    WorldMinimum         : vec3f,     // [m]  current world bounds
    Roles                : u32,       // [-]  OccupantRole mask
    WorldMaximum         : vec3f,     // [m]
    MaterialOverride     : u32,       // [-]  0xFFFFFFFF keeps the per-facet family
    VolumeMinimum        : vec3f,     // [m]  local corner of distance texel (0,0,0)
    VoxelExtent          : f32,       // [m]  distance volume voxel edge
    VolumeTexelOrigin    : vec3f,     // [texel] placement inside the volume space
    FirstCard            : u32,       // [-]
    VolumeTexelExtent    : vec3f,     // [texel] resolution
    CardCount            : u32,       // [-]
}

const RoleRaster   : u32 = 1u;
const RoleDistance : u32 = 2u;
const RoleCards    : u32 = 4u;
const RoleDynamic  : u32 = 8u;
const RoleFallback : u32 = 16u;   // card-less: FirstCard carries the packed albedo
const RoleShredded : u32 = 32u;   // fractured tyre piece: its triangles are traced as shredded rubber
const NoMaterialOverride : u32 = 0xFFFFFFFFu;

//------------------------------------------------------------------------------------------------------------------------
//                                                     CARD RECORD
//------------------------------------------------------------------------------------------------------------------------

// 📝 One oriented card of an occupant: an axis-aligned face of the occupant's local card bounds, looking inward.
//    Axis 0 +X, 1 −X, 2 +Y, 3 −Y, 4 +Z, 5 −Z names the outward normal of the face the card lies on.
struct CardRecord
{
    LocalMinimum : vec3f,   // [m]
    Axis         : u32,     // [-]
    LocalMaximum : vec3f,   // [m]
    Occupant     : u32,     // [-]
    TexelOrigin  : vec2u,   // [texel] tile corner in the card texel space
    TexelExtent  : vec2u,   // [texel] tile size
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   MATERIAL RECORD
//------------------------------------------------------------------------------------------------------------------------

struct MaterialRecord
{
    Albedo        : vec3f,   // [-]  linear Rec.709 reflectance
    Roughness     : f32,     // [-]  perceptual
    Emission      : vec3f,   // [-]  colour × luminance
    Metalness     : f32,     // [-]
    CoatWeight    : f32,     // [-]
    CoatRoughness : f32,     // [-]
    SpecularLevel : f32,     // [-]  dielectric F0
    Transmission  : f32,     // [-]
    // 📝 Index of refraction; 0 keeps the surface opaque. The record is 64 bytes, its size rounded to 16 by WGSL: the
    //    host writes 16 floats per material (MaterialRecordBytes in MaterialSpecification.js).
    IndexOfRefraction : f32, // [-]  > 1 bends transmitted light by Snell's law (DisplayRefraction.wgsl)
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SHARED CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const Pi           : f32 = 3.14159265358979;
const InversePi    : f32 = 0.31830988618379;
const LuminanceRec709 : vec3f = vec3f(0.2126, 0.7152, 0.0722);

fn ResolveLuminance(Radiance: vec3f) -> f32
{
    return dot(Radiance, LuminanceRec709);
}

// 📐 Builds an orthonormal tangent frame around a unit normal (Duff et al. 2017, branchless).
fn ConstructTangentFrame(Normal: vec3f) -> mat3x3f
{
    let Sign = select(-1.0, 1.0, Normal.z >= 0.0);
    let a = -1.0 / (Sign + Normal.z);
    let b = Normal.x * Normal.y * a;
    let Tangent   = vec3f(1.0 + Sign * Normal.x * Normal.x * a, Sign * b, -Sign * Normal.x);
    let Bitangent = vec3f(b, Sign + Normal.y * Normal.y * a, -Normal.y);
    return mat3x3f(Tangent, Bitangent, Normal);
}
