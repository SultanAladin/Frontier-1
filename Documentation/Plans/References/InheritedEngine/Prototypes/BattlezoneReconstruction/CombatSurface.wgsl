/*====================================================================================================================================
                                                      COMBATSURFACE.WGSL
====================================================================================================================================*/
// 🧩 Instanced surface shading — key-light lambert, horizon rim lift, edge glow and depth fog for the combat field

//------------------------------------------------------------------------------------------------------------------------
//                                                    UNIFORM STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ViewUniform
{
    ViewProjection  : mat4x4<f32>,
    SightPosition   : vec4<f32>,        // xyz = eye position, w = elapsed seconds
    SunDirection    : vec4<f32>,        // xyz = normalized key-light direction, w = fog density
    HorizonTone     : vec4<f32>,        // rgb = sky tone the fog resolves toward, a = unused
};

// 📝 One instance record per drawn part. ModelTransform carries scale, so NormalTransform must be its own
//    inverse-transpose — reusing ModelTransform would skew normals on the non-uniform track and barrel parts.
struct SurfaceInstance
{
    ModelTransform  : mat4x4<f32>,
    NormalTransform : mat4x4<f32>,
    SurfaceTone     : vec4<f32>,        // rgb = base tone, a = emissive lift (projectiles and crates ride hot)
};

@group(0) @binding(0) var<uniform> View            : ViewUniform;
@group(0) @binding(1) var<storage, read> Instances : array<SurfaceInstance>;

//------------------------------------------------------------------------------------------------------------------------
//                                                   INPUT/OUTPUT STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct VertexIntake
{
    @location(0) LocalPosition  : vec3<f32>,
    @location(1) LocalNormal    : vec3<f32>,
};

struct SurfaceInterpolant
{
    @builtin(position) ClipPosition   : vec4<f32>,
    @location(0)       WorldPosition  : vec3<f32>,
    @location(1)       WorldNormal    : vec3<f32>,
    @location(2)       SurfaceTone    : vec3<f32>,
    @location(3)       EmissiveLift   : f32,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      VERTEX STAGE
//------------------------------------------------------------------------------------------------------------------------

@vertex
fn VertexSurface(Intake : VertexIntake, @builtin(instance_index) InstanceIndex : u32) -> SurfaceInterpolant
{
    let Instance      = Instances[InstanceIndex];
    let WorldPosition = Instance.ModelTransform * vec4<f32>(Intake.LocalPosition, 1.0);
    let WorldNormal   = (Instance.NormalTransform * vec4<f32>(Intake.LocalNormal, 0.0)).xyz;

    var Interpolant : SurfaceInterpolant;
    Interpolant.ClipPosition  = View.ViewProjection * WorldPosition;
    Interpolant.WorldPosition = WorldPosition.xyz;
    Interpolant.WorldNormal   = WorldNormal;
    Interpolant.SurfaceTone   = Instance.SurfaceTone.rgb;
    Interpolant.EmissiveLift  = Instance.SurfaceTone.a;
    return Interpolant;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     FRAGMENT STAGE
//------------------------------------------------------------------------------------------------------------------------

@fragment
fn FragmentSurface(Interpolant : SurfaceInterpolant) -> @location(0) vec4<f32>
{
    let SurfaceNormal = normalize(Interpolant.WorldNormal);
    let SunAxis       = normalize(View.SunDirection.xyz);
    let SightAxis     = normalize(View.SightPosition.xyz - Interpolant.WorldPosition);

    // Key-light lambert, floored rather than clamped to zero so unlit faces keep their tone instead of going black.
    let KeyIncidence  = max(dot(SurfaceNormal, SunAxis), 0.0);
    let AmbientFloor  = 0.22;
    let DiffuseWeight = AmbientFloor + KeyIncidence * 0.85;

    // 💡 Grazing-angle rim lift is what gives the faceted hulls their vector-display edge glow without a second pass.
    let GrazingTerm   = 1.0 - clamp(dot(SurfaceNormal, SightAxis), 0.0, 1.0);
    let RimWeight     = pow(GrazingTerm, 3.0) * 0.85;

    var ResolvedTone  = Interpolant.SurfaceTone * DiffuseWeight;
    ResolvedTone     += Interpolant.SurfaceTone * RimWeight;
    ResolvedTone     += Interpolant.SurfaceTone * Interpolant.EmissiveLift;

    // Exponential-squared depth fog toward the horizon tone; hides the ground grid's outer boundary.
    let SightDistance = length(View.SightPosition.xyz - Interpolant.WorldPosition);
    let FogDensity    = View.SunDirection.w;
    let FogArgument   = SightDistance * FogDensity;
    let FogWeight     = 1.0 - exp(-FogArgument * FogArgument);
    ResolvedTone      = mix(ResolvedTone, View.HorizonTone.rgb, clamp(FogWeight, 0.0, 1.0));

    // Filmic-ish shoulder keeps the emissive projectiles from clipping to flat white.
    ResolvedTone = ResolvedTone / (ResolvedTone + vec3<f32>(0.55));
    ResolvedTone = pow(ResolvedTone, vec3<f32>(1.0 / 2.2));
    return vec4<f32>(ResolvedTone, 1.0);
}
