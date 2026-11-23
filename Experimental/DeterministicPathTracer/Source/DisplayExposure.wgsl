/*====================================================================================================================================
                                                      DISPLAYEXPOSURE.WGSL
====================================================================================================================================*/
// 🧩 Resolves the accumulated radiance average to the canvas: exposure, ACES fitted tone curve, sRGB encoding

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct DisplayUniforms
{
    Extent        : vec4u,   // accumulation width, height, tone curve (0 ACES fitted, 1 linear clamp), history / reconstruction offset [px]
    Exposure      : vec4f,   // x exposure multiplier (2^EV), y history active (0/1), z reconstructed (0/1), w unused
}

@group(0) @binding(0) var<uniform>       Display      : DisplayUniforms;
@group(0) @binding(1) var<storage, read> Accumulation : array<vec4f>;
@group(0) @binding(2) var<storage, read> Reconstructed : array<vec4f>;   // DisplayReconstruction.wgsl output (rgb, 1)


//------------------------------------------------------------------------------------------------------------------------
//                                                    TONE CURVE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Stephen Hill's fit of the ACES RRT + sRGB ODT, applied in the ACEScg-like fitted input space.
fn ToneCurveAces(Linear: vec3f) -> vec3f
{
    let InputMatrix = mat3x3f(vec3f(0.59719, 0.07600, 0.02840), vec3f(0.35458, 0.90834, 0.13383), vec3f(0.04823, 0.01566, 0.83777));
    let OutputMatrix = mat3x3f(vec3f(1.60475, -0.10208, -0.00327), vec3f(-0.53108, 1.10813, -0.07276), vec3f(-0.07367, -0.00605, 1.07602));
    let V = InputMatrix * Linear;
    let A = V * (V + 0.0245786) - 0.000090537;
    let B = V * (0.983729 * V + 0.4329510) + 0.238081;
    return clamp(OutputMatrix * (A / B), vec3f(0.0), vec3f(1.0));
}

fn EncodeSrgb(Linear: vec3f) -> vec3f
{
    let Low  = Linear * 12.92;
    let High = 1.055 * pow(Linear, vec3f(1.0 / 2.4)) - 0.055;
    return select(High, Low, Linear <= vec3f(0.0031308));
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

struct ScreenVarying
{
    @builtin(position) Position : vec4f,
    @location(0)       Texel    : vec2f,
}

@vertex
fn ProjectScreenTriangle(@builtin(vertex_index) VertexIndex: u32) -> ScreenVarying
{
    let Corner = vec2f(f32((VertexIndex << 1u) & 2u), f32(VertexIndex & 2u));
    var Varying : ScreenVarying;
    Varying.Position = vec4f(Corner * 2.0 - 1.0, 0.0, 1.0);
    Varying.Texel    = vec2f(Corner.x, 1.0 - Corner.y) * vec2f(f32(Display.Extent.x), f32(Display.Extent.y));
    return Varying;
}

@fragment
fn ResolveDisplay(Varying: ScreenVarying) -> @location(0) vec4f
{
    let Pixel = min(vec2u(Varying.Texel), vec2u(Display.Extent.x - 1u, Display.Extent.y - 1u));
    let PixelIndex = Pixel.y * Display.Extent.x + Pixel.x;
    var Sum        = Accumulation[PixelIndex];
    if (Display.Exposure.z > 0.5) { Sum = Reconstructed[Display.Extent.w + PixelIndex]; }
    // 📝 Reprojected history (average rgb, weight w) stands in for missing samples and fades out linearly: once the
    //    exact sum holds w samples it contributes nothing, so a converged image is the exact accumulation.
    let History       = select(vec4f(0.0), Accumulation[Display.Extent.w + PixelIndex], Display.Exposure.y > 0.5);
    let HistoryWeight = max(History.w - Sum.w, 0.0);
    let Weight        = Sum.w + HistoryWeight;
    if (Weight <= 0.0) { return vec4f(0.02, 0.02, 0.025, 1.0); }
    let Radiance = (Sum.rgb + History.rgb * HistoryWeight) / Weight * Display.Exposure.x;
    let Mapped   = select(ToneCurveAces(Radiance), clamp(Radiance, vec3f(0.0), vec3f(1.0)), Display.Extent.z == 1u);
    return vec4f(EncodeSrgb(Mapped), 1.0);
}
