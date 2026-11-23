//============================================================================================================================================
//                                                         REFLECTIONTRAVERSAL.WGSL
//============================================================================================================================================
// 🧩 Glossy reflection rays: a GGX visible-normal sample per resampling pixel, traced across the screen, then the distance field.

// 📝 The specular counterpart of the indirect candidate, traced the same two-part way: across the screen through the
//    nearest-depth pyramid, then through the scene distance field from the last point the screen proved unoccluded.
//    The ray reads the sample exitance where it lands and the sky where it escapes. Surfaces rougher than
//    RoughnessCutoff are left to the resolve, which substitutes the diffuse irradiance there exactly as Lumen reuses
//    its diffuse gather for rough reflections.

//! include "CardSampling.wgsl"
//! include "ScreenTraversal.wgsl"
//! include "SurfaceReflectance.wgsl"
//! include "PackingCodec.wgsl"
//! include "DiscrepancySequence.wgsl"
//! include "TriangleHierarchyTraversal.wgsl"

@group(0) @binding(1) var<storage, read> Materials : array<MaterialRecord>;
@group(0) @binding(2) var SurfaceAttributes        : texture_2d<f32>;
@group(0) @binding(3) var SurfaceDepth             : texture_depth_2d;
@group(0) @binding(4) var ReflectionTarget         : texture_storage_2d<rgba16float, write>;

const RoughnessCutoff : f32 = 0.5;

// 📐 Heitz 2018, "Sampling the GGX Distribution of Visible Normals"; view in tangent space, α = roughness².
// 📝 The triangle-reflection toggle replaces only the scene distance field here: the screen walk and the card exitance stay the same.
//    The triangle walk is exact, so the hit reads its surface at the finest card tolerance, as a screen strike does.
fn ResolveTriangleRadiance(Hit: RayHit, Direction: vec3f) -> vec3f
{
    if (!Hit.Hit)
    {
        return SampleSkyRadiance(Direction);
    }
    let Tolerance = ResolveCardTolerance(ResolveFinestClip(Hit.Position, 1.0));
    let Exitance = ResolveSampleExitance(Hit.Position, Hit.Normal, Hit.Identity, Tolerance);
    return select(vec3f(0.0), Exitance.rgb, Exitance.w > 0.0);
}

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
    var Strike: ScreenHit;
    if (ScreenConsumerEnabled(Frame, FeatureScreenReflection))
    {
        Strike = TraverseScreenDepth(Position + Normal * ResolveScreenBias(ResolveViewDepth(Position)), Direction, 300.0,
                                     ScreenTraversalSteps);
    }
    var Radiance: vec3f;
    if (Strike.Hit)
    {
        Radiance = ResolveStrikeRadiance(Strike);
    }
    else
    {
        let Resume = max(0.5 * Bias, Strike.Travel - Bias);
        if (FeatureEnabled(Frame, FeatureTriangleReflection))
        {
            let Hit = TraceSceneRay(Position + Normal * Bias, Direction, Resume, 300.0);
            Radiance = ResolveTriangleRadiance(Hit, Direction);
        }
        else
        {
            let Hit = TraverseSceneDistance(Position + Normal * Bias, Direction, Resume, 300.0, Roughness * 0.05);
            Radiance = ResolveContinuedRadiance(Hit, Direction);
        }
    }
    textureStore(ReflectionTarget, vec2i(TracePixel), vec4f(min(Radiance, vec3f(200.0)), 1.0));
}
