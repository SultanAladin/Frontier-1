//============================================================================================================================================
//                                                         REFLECTIONTRAVERSAL.WGSL
//============================================================================================================================================
// 🧩 Glossy reflection rays: a GGX visible-normal sample per resampling pixel, traced across the screen, then the world tracer.

// 📝 The specular counterpart of the indirect candidate, traced the same two-part way while the screen traces are on:
//    across the screen through the nearest-depth pyramid, then through the world tracer from the last point the screen
//    proved unoccluded. The world half is the scene distance field, or the triangle mesh while the triangle toggle is
//    on. The ray reads the sample exitance where it lands and the sky where it escapes. Surfaces rougher than
//    RoughnessCutoff are left to the resolve, which substitutes the diffuse irradiance there exactly as Lumen reuses
//    its diffuse gather for rough reflections. Transmissive surfaces instead split Schlick-against-Snell: a reflected
//    leg plus one refracted leg resumed past the far side.

//! include "CardSampling.wgsl"
//! include "ScreenTraversal.wgsl"
//! include "SurfaceReflectance.wgsl"
//! include "PackingCodec.wgsl"
//! include "DiscrepancySequence.wgsl"
//! include "RayTraversal.wgsl"

@group(0) @binding(1) var<storage, read> Materials : array<MaterialRecord>;
@group(0) @binding(2) var SurfaceAttributes        : texture_2d<f32>;
@group(0) @binding(3) var SurfaceDepth             : texture_depth_2d;
@group(0) @binding(4) var ReflectionTarget         : texture_storage_2d<rgba16float, write>;

const RoughnessCutoff : f32 = 0.5;

// 📝 How far past the entry point a transmitted segment resumes: the SDF march reports an instant hit from inside, so
//    the ray restarts past the far side instead. Must clear the thickest transmissive object — the garden's glass
//    sphere is 1.2 m across.
const TransmittedSlabOffset : f32 = 1.4;

// 📐 Heitz 2018, "Sampling the GGX Distribution of Visible Normals"; view in tangent space, α = roughness².
fn ProjectVisibleNormal(View: vec3f, α: f32, Pair: vec2f) -> vec3f
{
    let Stretched = normalize(vec3f(α * View.x, α * View.y, View.z));
    let LengthSquared = Stretched.x * Stretched.x + Stretched.y * Stretched.y;
    let T1 = select(vec3f(1.0, 0.0, 0.0), vec3f(-Stretched.y, Stretched.x, 0.0) * inverseSqrt(LengthSquared), LengthSquared > 0.0);
    let T2 = cross(Stretched, T1);
    let Radius = sqrt(Pair.x);
    let φ = 2.0 * Pi * Pair.y;
    let P1 = Radius * cos(φ);
    let Interpolant = 0.5 * (1.0 + Stretched.z);
    let P2 = mix(sqrt(max(0.0, 1.0 - P1 * P1)), Radius * sin(φ), Interpolant);
    let Hemisphere = P1 * T1 + P2 * T2 + sqrt(max(0.0, 1.0 - P1 * P1 - P2 * P2)) * Stretched;
    return normalize(vec3f(α * Hemisphere.x, α * Hemisphere.y, max(0.0, Hemisphere.z)));
}

// 📝 One world-tracer segment and its radiance — SDF or triangle mesh per the toggle, exitance-aware per the screen
//    toggle. The shared tail of the glass legs below; the opaque path beneath keeps its own inline copy untouched.
fn ResolveWorldSegment(Origin: vec3f, Direction: vec3f, MinimumTravel: f32, ConeSlope: f32) -> vec3f
{
    var Hit = TraverseSceneDistance(Origin, Direction, MinimumTravel, 300.0, ConeSlope);
    if (FeatureEnabled(Frame, FeatureTriangleReflection))
    {
        Hit = ResolveTraversalHit(TraceSceneRay(Origin, Direction, MinimumTravel, 300.0));
    }
    if (FeatureEnabled(Frame, FeatureScreenTraversal))
    {
        return ResolveContinuedRadiance(Hit, Direction);
    }
    return ResolveTraversalRadiance(Hit, Direction);
}

// 📝 Glass: a Schlick split at the entry interface. The reflected leg walks the same screen-first path as every
//    reflection; the transmitted leg refracts by Snell's law and resumes past the far side. One refraction only: the
//    exit bend is skipped, a trapped ray is absorbed, and the transmitted leg never walks the screen.
fn ResolveGlassRadiance(Position: vec3f, Normal: vec3f, View: vec3f, Direction: vec3f, Bias: f32, Roughness: f32,
                        F0: f32) -> vec3f
{
    let Complement = 1.0 - clamp(dot(View, Normal), 0.0, 1.0);
    var Share = F0 + (1.0 - F0) * Complement * Complement * Complement * Complement * Complement;
    var Transmitted = vec3f(0.0);
    let Bent = refract(-View, Normal, 1.0 / ResolveDielectricIor(F0));
    if (dot(Bent, Bent) > 0.0)
    {
        Transmitted = ResolveWorldSegment(Position, Bent, TransmittedSlabOffset, 0.0);
    }
    else
    {
        Share = 1.0;
    }
    var Reflected: vec3f;
    if (FeatureEnabled(Frame, FeatureScreenTraversal))
    {
        let Origin = Position + Normal * ResolveScreenBias(ResolveViewDepth(Position));
        let Strike = TraverseScreenDepth(Origin, Direction, 300.0, ScreenTraversalSteps);
        if (Strike.Hit)
        {
            Reflected = ResolveStrikeRadiance(Strike);
        }
        else
        {
            Reflected = ResolveWorldSegment(Position + Normal * Bias, Direction, max(0.5 * Bias, Strike.Travel - Bias),
                                            Roughness * 0.05);
        }
    }
    else
    {
        Reflected = ResolveWorldSegment(Position + Normal * Bias, Direction, 0.5 * Bias, Roughness * 0.05);
    }
    return Share * Reflected + (1.0 - Share) * Transmitted;
}

@compute @workgroup_size(8, 8)
fn TraverseReflection(@builtin(global_invocation_id) Invocation: vec3u)
{
    let TracePixel = Invocation.xy;
    if (any(vec2f(TracePixel) >= Frame.TraceExtent))
    {
        return;
    }
    let ScreenPixel = min(vec2i(TracePixel * Frame.TraceScale), vec2i(Frame.ScreenExtent) - vec2i(1));
    let Depth = textureLoad(SurfaceDepth, ScreenPixel, 0);
    if (Depth <= 0.0)
    {
        textureStore(ReflectionTarget, vec2i(TracePixel), vec4f(0.0));
        return;
    }
    let Attributes = textureLoad(SurfaceAttributes, ScreenPixel, 0);
    let Surface = ProjectSurfaceSample(Materials[u32(Attributes.z + 0.5)]);
    let Roughness = select(Surface.Roughness, Surface.CoatRoughness, Surface.CoatWeight > 0.0);
    if (Roughness > RoughnessCutoff)
    {
        textureStore(ReflectionTarget, vec2i(TracePixel), vec4f(0.0));
        return;
    }
    let Position = ReconstructWorldPosition(vec2f(ScreenPixel) + 0.5, Depth);
    let Normal = DecodeOctahedral(Attributes.xy);
    let View = normalize(Frame.CameraPosition - Position);
    let Frame3 = ConstructTangentFrame(Normal);
    let TangentView = transpose(Frame3) * View;
    let Pair = ProjectTemporalPair(TracePixel, Frame.FrameIndex, 29u);
    let Half = Frame3 * ProjectVisibleNormal(TangentView, Roughness * Roughness, Pair);
    var Direction = reflect(-View, Half);
    if (dot(Direction, Normal) <= 0.0)
    {
        Direction = reflect(-View, Normal);
    }
    let Bias = ResolveSurfaceBias(Position);
    // 📝 The ray's world half, whichever tracer the triangle toggle selects: the distance field the exhibit has always
    //    taken, or the two-level mesh hierarchy. An if, not a select — the untaken path must not execute.
    var Radiance: vec3f;
    if (Surface.Transmission > 0.5)
    {
        Radiance = ResolveGlassRadiance(Position, Normal, View, Direction, Bias, Roughness, Surface.SpecularLevel);
    }
    else if (FeatureEnabled(Frame, FeatureScreenTraversal))
    {
        let Origin = Position + Normal * ResolveScreenBias(ResolveViewDepth(Position));
        let Strike = TraverseScreenDepth(Origin, Direction, 300.0, ScreenTraversalSteps);
        if (Strike.Hit)
        {
            Radiance = ResolveStrikeRadiance(Strike);
        }
        else
        {
            let Resume = max(0.5 * Bias, Strike.Travel - Bias);
            var Hit = TraverseSceneDistance(Position + Normal * Bias, Direction, Resume, 300.0, Roughness * 0.05);
            if (FeatureEnabled(Frame, FeatureTriangleReflection))
            {
                Hit = ResolveTraversalHit(TraceSceneRay(Position + Normal * Bias, Direction, Resume, 300.0));
            }
            Radiance = ResolveContinuedRadiance(Hit, Direction);
        }
    }
    else
    {
        var Hit = TraverseSceneDistance(Position + Normal * Bias, Direction, 0.5 * Bias, 300.0, Roughness * 0.05);
        if (FeatureEnabled(Frame, FeatureTriangleReflection))
        {
            Hit = ResolveTraversalHit(TraceSceneRay(Position + Normal * Bias, Direction, 0.5 * Bias, 300.0));
        }
        Radiance = ResolveTraversalRadiance(Hit, Direction);
    }
    textureStore(ReflectionTarget, vec2i(TracePixel), vec4f(min(Radiance, vec3f(200.0)), 1.0));
}
