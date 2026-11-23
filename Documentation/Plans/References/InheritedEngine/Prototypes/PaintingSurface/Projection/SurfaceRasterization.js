/*====================================================================================================================================
                                                 SURFACERASTERIZATION.JS
====================================================================================================================================*/
// 🧩 Camera-space raster of the painted surface: samples the atlas, shades it, resolves depth

import { SurfaceVertexLayout } from "../Ingest/SurfaceUpload.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

export const DepthFormat = "depth32float";

// 📝 mat4x4f(16) + eye padded to vec4f(4) + surface extent vec4f(4) + atlas extent vec4f(4) = 28 floats.
//    A vec3f in a uniform block occupies 16 bytes, not 12; packing it as 12 slides every later field.
const ProjectionUniformFloatCount = 28;
const ProjectionUniformByteLength = ProjectionUniformFloatCount * 4;

//------------------------------------------------------------------------------------------------------------------------
//                                                      SHADER SOURCE
//------------------------------------------------------------------------------------------------------------------------

const SurfaceShaderSource = `
struct ProjectionUniform
{
    ViewProjection : mat4x4f,
    EyePosition    : vec4f,
    SurfaceExtent  : vec4f,   // xy = pixels, z = display mode, w = unused
    AtlasExtent    : vec4f,   // x = atlas texels per side, y = height-to-normal strength, zw = unused
};

@group(0) @binding(0) var<uniform> Projection : ProjectionUniform;
@group(0) @binding(1) var          AtlasStore : texture_2d<f32>;      // rgb = base colour, a = coverage
@group(0) @binding(2) var          AtlasSampler : sampler;
@group(0) @binding(3) var          MaterialStore : texture_2d<f32>;   // r = metallic, g = roughness, b = height
@group(0) @binding(4) var          EmissiveStore : texture_2d<f32>;   // rgb = emissive

// 🔴 The FOCUSED layer's resolved mask, bound only so the mask-on-mesh inspection mode can show it. It is
//    deliberately NOT read by the shading path: a mask decides where a layer contributes during
//    compositing, and by the time this shader runs the stack is already flattened. Multiplying it in here
//    would apply the focused layer's mask a second time, over the whole combined surface.
@group(0) @binding(5) var          MaskStore : texture_2d<f32>;       // r = mask coverage

struct SurfaceVarying
{
    @builtin(position) ClipPosition   : vec4f,
    @location(0)       WorldPosition  : vec3f,
    @location(1)       Coordinate     : vec2f,
    @location(2)       WorldNormal    : vec3f,
};

@vertex
fn SurfaceVertex(
    @location(0) Position   : vec3f,
    @location(1) Coordinate : vec2f,
    @location(2) Normal     : vec3f) -> SurfaceVarying
{
    var Out : SurfaceVarying;
    Out.ClipPosition  = Projection.ViewProjection * vec4f(Position, 1.0);
    Out.WorldPosition = Position;
    Out.Coordinate    = Coordinate;
    Out.WorldNormal   = Normal;
    return Out;
}

//------------------------------------------------------------------------------------------------------
//                                        MICROFACET TERMS
//------------------------------------------------------------------------------------------------------

// GGX normal distribution. 📝 Roughness is squared before use so the slider feels perceptually even;
//    feeding linear roughness straight in makes the whole bottom half of the slider look identical.
fn DistributionGGX(NormalDotHalf : f32, Roughness : f32) -> f32
{
    let Alpha       = Roughness * Roughness;
    let AlphaSquare = Alpha * Alpha;
    let Denominator = NormalDotHalf * NormalDotHalf * (AlphaSquare - 1.0) + 1.0;
    return AlphaSquare / max(3.14159265 * Denominator * Denominator, 1e-6);
}

fn GeometrySchlick(NormalDotVector : f32, Roughness : f32) -> f32
{
    let Kappa = (Roughness + 1.0) * (Roughness + 1.0) / 8.0;
    return NormalDotVector / max(NormalDotVector * (1.0 - Kappa) + Kappa, 1e-6);
}

fn FresnelSchlick(CosineTheta : f32, Reflectance : vec3f) -> vec3f
{
    return Reflectance + (vec3f(1.0) - Reflectance) * pow(clamp(1.0 - CosineTheta, 0.0, 1.0), 5.0);
}

// 🔴 The normal is DERIVED from the painted height, not painted directly. A brush writing raw RGB into
//    a normal map produces vectors that are neither unit-length nor in tangent space, which shades as
//    coloured noise. Central differences over the height channel give a perturbation that is correct by
//    construction, and it means the height slider and the normal are guaranteed to agree.
//
// 📝 The tangent frame comes from screen-space derivatives of position and UV rather than from a vertex
//    tangent attribute, because the OBJ carries none. This is the standard cotangent-frame trick; it is
//    exact enough for a perturbation and costs no extra vertex data.
fn DerivedNormal(GeometricNormal : vec3f, WorldPosition : vec3f, Coordinate : vec2f, Strength : f32) -> vec3f
{
    if (Strength <= 0.0) { return GeometricNormal; }

    let Step = 1.0 / Projection.AtlasExtent.x;

    let HeightLeft  = textureSampleLevel(MaterialStore, AtlasSampler, Coordinate - vec2f(Step, 0.0), 0.0).b;
    let HeightRight = textureSampleLevel(MaterialStore, AtlasSampler, Coordinate + vec2f(Step, 0.0), 0.0).b;
    let HeightDown  = textureSampleLevel(MaterialStore, AtlasSampler, Coordinate - vec2f(0.0, Step), 0.0).b;
    let HeightUp    = textureSampleLevel(MaterialStore, AtlasSampler, Coordinate + vec2f(0.0, Step), 0.0).b;

    // 0.5 is the undisplaced surface, so the gradient is of the SIGNED offset from it.
    //
    // 🔴 SlopeV is NEGATED. The coordinate handed in is already v-flipped for sampling (v runs down),
    //    so a step in +v walks DOWN the atlas while the tangent frame below is built from derivatives of
    //    that same flipped coordinate. Without this negation the binormal points the wrong way and every
    //    painted bump lights as a dent — which is invisible on a symmetric blob and unmistakable on a
    //    scratch, because the highlight lands on the wrong side of the groove.
    let SlopeU =  (HeightRight - HeightLeft) * Strength;
    let SlopeV = -(HeightUp    - HeightDown) * Strength;

    let PositionDx = dpdx(WorldPosition);
    let PositionDy = dpdy(WorldPosition);
    let CoordDx    = dpdx(Coordinate);
    let CoordDy    = dpdy(Coordinate);

    let Determinant = CoordDx.x * CoordDy.y - CoordDy.x * CoordDx.y;
    // A degenerate UV patch (a collapsed triangle, or a texel the rasterizer gives no derivative for)
    // cannot produce a frame. Falling back to the geometric normal is the only safe answer; inverting a
    // near-zero determinant would fling the normal to infinity along island edges.
    if (abs(Determinant) < 1e-12) { return GeometricNormal; }

    let Inverse  = 1.0 / Determinant;
    let Tangent  = normalize((PositionDx * CoordDy.y - PositionDy * CoordDx.y) * Inverse);
    let Binormal = normalize((PositionDy * CoordDx.x - PositionDx * CoordDy.x) * Inverse);

    return normalize(GeometricNormal - Tangent * SlopeU - Binormal * SlopeV);
}

@fragment
fn SurfaceFragment(In : SurfaceVarying) -> @location(0) vec4f
{
    // 📝 The atlas is authored with v increasing upward (OBJ convention) while texture sampling runs
    //    v downward, so the coordinate is flipped on read. Getting this wrong mirrors every dab
    //    vertically and the error is invisible on a symmetric test pattern — which Suzanne's UV is not.
    let SampleCoordinate = vec2f(In.Coordinate.x, 1.0 - In.Coordinate.y);

    let AtlasColour = textureSampleLevel(AtlasStore,    AtlasSampler, SampleCoordinate, 0.0);
    let Material    = textureSampleLevel(MaterialStore, AtlasSampler, SampleCoordinate, 0.0);
    let Emissive    = textureSampleLevel(EmissiveStore, AtlasSampler, SampleCoordinate, 0.0);

    let BaseColour = AtlasColour.rgb;
    let Metallic   = clamp(Material.r, 0.0, 1.0);
    // 🔴 Roughness is floored well above zero. A perfectly smooth microfacet lobe is a delta function
    //    against an analytic light, which renders as a single blown-out pixel that flickers as the
    //    camera moves — not as a mirror.
    let Roughness  = clamp(Material.g, 0.045, 1.0);

    let Geometric = normalize(In.WorldNormal);
    let ViewRay   = normalize(Projection.EyePosition.xyz - In.WorldPosition);

    // Two-sided shading: the UV atlas has no consistent winding, so a backfacing normal is flipped
    // rather than blackened.
    var FacingNormal = Geometric;
    if (dot(FacingNormal, ViewRay) < 0.0) { FacingNormal = -FacingNormal; }

    let ShadingNormal = DerivedNormal(FacingNormal, In.WorldPosition, SampleCoordinate,
                                      Projection.AtlasExtent.y);

    // 📝 Two analytic lights, matching the key/fill the unlit path used, so the change of shading model
    //    does not also silently change the lighting direction the model has been judged under.
    let KeyDirection  = normalize(vec3f(0.45, 0.72, 0.53));
    let FillDirection = normalize(vec3f(-0.5, 0.15, -0.35));
    let KeyColour     = vec3f(1.00, 0.97, 0.92) * 2.6;
    let FillColour    = vec3f(0.72, 0.80, 1.00) * 0.7;

    // 🔴 Dielectrics reflect ~4% at normal incidence and tint their DIFFUSE; metals reflect their base
    //    colour and have no diffuse at all. Collapsing that distinction is what makes a "metallic"
    //    slider do nothing visible.
    let Reflectance = mix(vec3f(0.04), BaseColour, Metallic);
    let Albedo      = BaseColour * (1.0 - Metallic);

    var Radiance = vec3f(0.0);

    for (var Index = 0; Index < 2; Index += 1)
    {
        var LightDirection = KeyDirection;
        var LightColour    = KeyColour;
        if (Index == 1) { LightDirection = FillDirection; LightColour = FillColour; }

        let HalfVector = normalize(LightDirection + ViewRay);

        let NormalDotLight = max(dot(ShadingNormal, LightDirection), 0.0);
        let NormalDotView  = max(dot(ShadingNormal, ViewRay),        1e-4);
        let NormalDotHalf  = max(dot(ShadingNormal, HalfVector),     0.0);
        let ViewDotHalf    = max(dot(ViewRay,       HalfVector),     0.0);

        if (NormalDotLight <= 0.0) { continue; }

        let Distribution = DistributionGGX(NormalDotHalf, Roughness);
        let Geometry     = GeometrySchlick(NormalDotLight, Roughness)
                         * GeometrySchlick(NormalDotView,  Roughness);
        let Fresnel      = FresnelSchlick(ViewDotHalf, Reflectance);

        let Specular = (Distribution * Geometry * Fresnel)
                     / max(4.0 * NormalDotView * NormalDotLight, 1e-4);

        // Energy that reflected specularly is not available to scatter diffusely.
        let Diffuse = (vec3f(1.0) - Fresnel) * Albedo / 3.14159265;

        Radiance += (Diffuse + Specular) * LightColour * NormalDotLight;
    }

    // A cheap hemispheric ambient so cavities are not pure black. Metals take it tinted by their own
    // colour, dielectrics take it neutral, for the same reason the direct term splits.
    let Sky      = 0.16 + 0.10 * (ShadingNormal.y * 0.5 + 0.5);
    let AmbientTint = mix(vec3f(1.0), BaseColour, Metallic);
    Radiance += Albedo * Sky + AmbientTint * Sky * Metallic * 0.5;

    // Emissive is added AFTER shading and is not lit — that is what makes it emissive.
    Radiance += Emissive.rgb * 3.0;

    // 📝 Reinhard then gamma. The PBR terms are linear and can exceed 1 by a wide margin, so writing
    //    them straight to an 8-bit sRGB target would clip every highlight to flat white.
    let Mapped = Radiance / (Radiance + vec3f(1.0));
    let Display = pow(Mapped, vec3f(1.0 / 2.2));

    // ---- inspection modes ---------------------------------------------------------------------------
    // 🔴 These read the atlases RAW and return before tone mapping. A debug view that passes through the
    //    tonemap is not showing the channel, it is showing a curve of the channel.
    let Mode = Projection.SurfaceExtent.z;

    if (Mode > 0.5 && Mode < 1.5) { return vec4f(BaseColour, 1.0); }
    if (Mode > 1.5 && Mode < 2.5) { return vec4f(vec3f(Metallic), 1.0); }
    if (Mode > 2.5 && Mode < 3.5) { return vec4f(vec3f(Material.g), 1.0); }
    if (Mode > 3.5 && Mode < 4.5) { return vec4f(Emissive.rgb, 1.0); }
    if (Mode > 4.5 && Mode < 5.5) { return vec4f(ShadingNormal * 0.5 + vec3f(0.5), 1.0); }
    if (Mode > 5.5 && Mode < 6.5) { return vec4f(vec3f(Material.b), 1.0); }

    // The focused layer's mask, painted over the shaded surface rather than replacing it.
    //
    // 🔴 A mix rather than a flat grey return, and this is the whole point of the mode. A mask is a
    //    SPATIAL selection, so what the user needs to see is WHERE on the model it falls — and a screen
    //    full of grey values carries no landmarks to place them against. Keeping a dim shaded surface
    //    underneath means the nose and the brow are still recognisable while the mask reads over them.
    // 🔴 Red for the hidden side, not black. Black is what an unlit cavity already looks like, so a
    //    black-masked region and a region merely facing away from the light would be indistinguishable —
    //    exactly the confusion the mode exists to remove. Nothing else in this shader emits pure red.
    if (Mode > 6.5)
    {
        let Coverage = textureSample(MaskStore, AtlasSampler, In.Coordinate).r;
        let Ground   = Display * 0.28;
        let Revealed = mix(vec3f(0.72, 0.10, 0.10), vec3f(1.0), Coverage);
        return vec4f(Ground + Revealed * 0.72, 1.0);
    }

    return vec4f(Display, 1.0);
}`;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

export class SurfaceRasterization
{
    constructor(Device, ColourFormat)
    {
        this.Device       = Device;
        this.ColourFormat = ColourFormat;

        const ShaderModule = Device.createShaderModule({
            label: "SurfaceRasterization",
            code:  SurfaceShaderSource
        });
        this.ShaderModule = ShaderModule;

        this.ProjectionUniform = Device.createBuffer({
            label: "SurfaceProjectionUniform",
            size:  ProjectionUniformByteLength,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
        });

        this.AtlasSampler = Device.createSampler({
            label:     "SurfaceAtlasSampler",
            magFilter: "linear",
            minFilter: "linear",
            addressModeU: "clamp-to-edge",
            addressModeV: "clamp-to-edge"
        });

        // 1×1 stand-ins for the material and emissive atlases, bound until the compositor produces real
        // ones. 📝 The material default is (metallic 0, roughness 0.5, height 0.5) — a plain dielectric
        //    on an undisplaced surface — and the emissive default is black, so an unpainted model shades
        //    as a neutral matte object rather than glowing or turning to chrome.
        const MakeConstant = (Label, Bytes) => {
            const Texture = Device.createTexture({
                label:  Label,
                size:   [1, 1],
                format: "rgba8unorm",
                usage:  GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
            });
            Device.queue.writeTexture(
                { texture: Texture }, new Uint8Array(Bytes), { bytesPerRow: 4 }, [1, 1]);
            return Texture;
        };

        this.DefaultMaterial     = MakeConstant("DefaultMaterial", [0, 128, 128, 255]);
        this.DefaultEmissive     = MakeConstant("DefaultEmissive", [0, 0, 0, 255]);
        this.DefaultMaterialView = this.DefaultMaterial.createView();
        this.DefaultEmissiveView = this.DefaultEmissive.createView();

        // 🔴 The mask stand-in is WHITE, matching what EnsureMaskAtlas clears a white-fill mask to. White
        //    means "applies everywhere", so a layer with no mask allocated reads as fully revealed — which
        //    is the truth. A black default would render the mask-inspection mode as a solid red model on
        //    every unmasked layer, indistinguishable from a mask that hides everything.
        this.DefaultMask     = MakeConstant("DefaultMask", [255, 255, 255, 255]);
        this.DefaultMaskView = this.DefaultMask.createView();

        this.BindLayout = Device.createBindGroupLayout({
            label: "SurfaceBindLayout",
            entries: [
                { binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer:  { type: "uniform" } },
                { binding: 1, visibility: GPUShaderStage.FRAGMENT,                         texture: { sampleType: "float" } },
                { binding: 2, visibility: GPUShaderStage.FRAGMENT,                         sampler: { type: "filtering" } },
                { binding: 3, visibility: GPUShaderStage.FRAGMENT,                         texture: { sampleType: "float" } },
                { binding: 4, visibility: GPUShaderStage.FRAGMENT,                         texture: { sampleType: "float" } },
                { binding: 5, visibility: GPUShaderStage.FRAGMENT,                         texture: { sampleType: "float" } }
            ]
        });

        this.Pipeline = Device.createRenderPipeline({
            label:  "SurfaceRasterization",
            layout: Device.createPipelineLayout({ bindGroupLayouts: [this.BindLayout] }),
            vertex: {
                module:     ShaderModule,
                entryPoint: "SurfaceVertex",
                buffers:    [SurfaceVertexLayout]
            },
            fragment: {
                module:     ShaderModule,
                entryPoint: "SurfaceFragment",
                targets:    [{ format: ColourFormat }]
            },
            // 📝 Suzanne's quads are consistently wound, so backface culling would be valid — but the
            //    surface is not closed (the eyes are separate shells) and culling makes their interiors
            //    vanish. Two-sided shading in the fragment stage covers the cost.
            primitive: { topology: "triangle-list", cullMode: "none" },
            depthStencil: {
                format:            DepthFormat,
                depthWriteEnabled: true,
                depthCompare:      "less"
            }
        });
    }

    // Rebuild the bind group whenever the atlas textures are replaced.
    //
    // `MaterialView` and `EmissiveView` are the resolved material and emissive atlases. Both are
    // optional; when absent the standing 1×1 defaults are bound instead.
    //
    // 🔴 The fallback is a DEFAULT texture, not the colour atlas. Binding the colour atlas into these
    //    slots type-checks and shades catastrophically: its green would read as roughness, its blue as
    //    height, and — worst — its RGB as emissive, which the fragment stage adds at 3× unlit, so an
    //    unpainted model would render as a white silhouette. Two 1×1 textures cost 8 bytes and remove
    //    the whole failure mode.
    // `MaskView` is the FOCUSED layer's resolved mask, for the mask-inspection mode only. Optional in the
    // same way: a layer with no mask allocated falls back to the white stand-in and reads as revealed.
    BindAtlas(AtlasView, MaterialView, EmissiveView, MaskView)
    {
        this.BindGroup = this.Device.createBindGroup({
            label:  "SurfaceBindGroup",
            layout: this.BindLayout,
            entries: [
                { binding: 0, resource: { buffer: this.ProjectionUniform } },
                { binding: 1, resource: AtlasView },
                { binding: 2, resource: this.AtlasSampler },
                { binding: 3, resource: MaterialView ?? this.DefaultMaterialView },
                { binding: 4, resource: EmissiveView ?? this.DefaultEmissiveView },
                { binding: 5, resource: MaskView     ?? this.DefaultMaskView }
            ]
        });
    }

    // Push the resolved camera state into the uniform block.
    //
    // `AtlasExtent` is the atlas side in texels and drives the finite-difference step for the derived
    // normal; `NormalStrength` scales the height gradient. 🔴 The extent is NOT optional in practice —
    //    defaulting it to a wrong value makes the derived normal sample the wrong neighbours, which
    //    reads as a soft blur rather than an error.
    WriteProjection(Resolved, SurfaceWidth, SurfaceHeight, DisplayMode, AtlasExtent, NormalStrength)
    {
        const Staging = new Float32Array(ProjectionUniformFloatCount);

        Staging.set(Resolved.ViewProjection, 0);

        Staging[16] = Resolved.Eye[0];
        Staging[17] = Resolved.Eye[1];
        Staging[18] = Resolved.Eye[2];
        Staging[19] = 0.0;

        Staging[20] = SurfaceWidth;
        Staging[21] = SurfaceHeight;
        Staging[22] = DisplayMode ?? 0.0;
        Staging[23] = 0.0;

        Staging[24] = AtlasExtent    ?? 1024;
        // 0 disables the derivation entirely and the geometric normal is used, which is what the
        // pre-layer path wants — there is no height channel to differentiate.
        Staging[25] = NormalStrength ?? 0.0;
        Staging[26] = 0.0;
        Staging[27] = 0.0;

        this.Device.queue.writeBuffer(this.ProjectionUniform, 0, Staging);
    }

    // Encode the surface pass into an open command encoder.
    Encode(Encoder, ColourView, DepthView, Surface, ClearColour)
    {
        const Pass = Encoder.beginRenderPass({
            label: "SurfacePass",
            colorAttachments: [{
                view:       ColourView,
                clearValue: ClearColour ?? { r: 0.043, g: 0.043, b: 0.047, a: 1.0 },
                loadOp:     "clear",
                storeOp:    "store"
            }],
            depthStencilAttachment: {
                view:            DepthView,
                depthClearValue: 1.0,
                depthLoadOp:     "clear",
                depthStoreOp:    "store"
            }
        });

        Pass.setPipeline(this.Pipeline);
        Pass.setBindGroup(0, this.BindGroup);
        Pass.setVertexBuffer(0, Surface.VertexStore);
        Pass.setIndexBuffer(Surface.IndexStore, "uint32");
        Pass.drawIndexed(Surface.IndexCount);
        Pass.end();
    }
}

// Allocate (or reallocate) the depth target for a given surface size.
export function CreateDepthTarget(Device, SurfaceWidth, SurfaceHeight)
{
    return Device.createTexture({
        label:  "SurfaceDepth",
        size:   [SurfaceWidth, SurfaceHeight],
        format: DepthFormat,
        usage:  GPUTextureUsage.RENDER_ATTACHMENT
    });
}
