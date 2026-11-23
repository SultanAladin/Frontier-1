/*====================================================================================================================================
                                                     CHANNELPREVIEW.JS
====================================================================================================================================*/
// 🧩 Thumbnail of one painted channel: GPU-downsample its atlas to a small tile, read it back as a PNG

import { CHANNEL_ATLASES, CHANNEL_SLOTS, ChannelAtlasFormat } from "./ChannelSet.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 The preview is rendered at 64² and NOT read back at atlas resolution. A 1024² RGBA8 readback is 4 MiB
//    mapped per channel per refresh; six channels on one layer is 24 MiB and a full pipeline stall every
//    time the panel repaints, which on a stroke is every frame. Downsampling on the GPU first makes the
//    mapped range 16 KiB — 256x less — and the sampler does the box filter for free. The tile is also all
//    a 34px thumbnail can show, so nothing is lost but the cost.
export const PreviewExtent = 64;                // [px]

// 📝 rgba8unorm, matching the atlases, so no swizzle applies on readback.
export const PreviewFormat = ChannelAtlasFormat;

const RowAlignment = 256;                       // [B] copyTextureToBuffer row boundary

//------------------------------------------------------------------------------------------------------------------------
//                                                      SHADER SOURCE
//------------------------------------------------------------------------------------------------------------------------

// 🔴 A channel is NOT an image, and this shader is the whole reason a preview needs a pass rather than a
//    plain texture copy. Metallic, roughness and height are single COMPONENTS packed into one shared RGBA8
//    atlas, so copying that atlas as-is shows a false-colour blend of all three at once — a rough metal
//    reads as olive green. Each scalar channel is therefore splatted to grey from its own component, and
//    only the two genuine colour channels pass RGB through.
//
// 🔴 Coverage is composited against a CHECKERBOARD here rather than written through as alpha. A thumbnail
//    drawn with real alpha over a dark panel makes "unpainted" and "painted black" identical, which is the
//    one distinction the preview exists to show on a lazily-allocated layer.
const PreviewShaderSource = `
struct PreviewUniform
{
    // x = component index (-1 = pass RGB through), y = checker size in preview texels, zw = unused
    Control : vec4f,
};

@group(0) @binding(0) var<uniform> Preview : PreviewUniform;
@group(0) @binding(1) var AtlasStore : texture_2d<f32>;
@group(0) @binding(2) var AtlasSampler : sampler;

struct PreviewVarying
{
    @builtin(position) ClipPosition : vec4f,
    @location(0)       Coordinate   : vec2f,
};

@vertex
fn PreviewVertex(@builtin(vertex_index) Index : u32) -> PreviewVarying
{
    // One oversized triangle covering the target, matching the compositor's idiom.
    var Corners = array<vec2f, 3>(vec2f(-1.0, -5.0), vec2f(-1.0, 1.0), vec2f(5.0, 1.0));
    let Corner  = Corners[Index];

    var Out : PreviewVarying;
    Out.ClipPosition = vec4f(Corner, 0.0, 1.0);
    // Clip y runs up, texture v runs down.
    Out.Coordinate   = vec2f((Corner.x + 1.0) * 0.5, (1.0 - Corner.y) * 0.5);
    return Out;
}

@fragment
fn PreviewFragment(In : PreviewVarying) -> @location(0) vec4f
{
    // 🔴 Sampled from an explicit MIP LEVEL, not from level 0. A bilinear tap on a 1024 -> 64 reduction
    //    reads four texels and calls it a 256-texel footprint, so a sparse generator (rust flecks,
    //    scratches) lands mostly between its features and the thumbnail comes back nearly empty while the
    //    surface is visibly covered. Level log2(1024/64) = 4 is the mip whose single texel IS the average
    //    of the 16x16 footprint — which only holds because EnsureMipped built that pyramid by hand; the
    //    layer atlases themselves are single-level.
    let Level   = log2(f32(textureDimensions(AtlasStore).x) / ${PreviewExtent}.0);
    let Sampled = textureSampleLevel(AtlasStore, AtlasSampler, In.Coordinate, Level);

    let Component = i32(Preview.Control.x);

    // 📝 select() takes (whenFalse, whenTrue, condition). A scalar channel splats one component to grey;
    //    a colour channel passes RGB straight through.
    var Value = Sampled.rgb;
    if (Component == 0) { Value = vec3f(Sampled.r); }
    if (Component == 1) { Value = vec3f(Sampled.g); }
    if (Component == 2) { Value = vec3f(Sampled.b); }

    // The checkerboard the coverage is composited over.
    let Cell   = Preview.Control.y;
    let Square = floor(In.Coordinate * ${PreviewExtent}.0 / Cell);
    let Odd    = (Square.x + Square.y) - 2.0 * floor((Square.x + Square.y) * 0.5);
    let Checker = mix(vec3f(0.16, 0.16, 0.18), vec3f(0.24, 0.24, 0.27), Odd);

    // 🔴 Written OPAQUE, with coverage resolved here rather than carried in alpha. See the note above the
    //    shader: an alpha-carrying thumbnail cannot distinguish unpainted from painted-black.
    return vec4f(mix(Checker, Value, Sampled.a), 1.0);
}`;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

export function ResolvePreviewRowStride()
{
    return Math.ceil(PreviewExtent * 4 / RowAlignment) * RowAlignment;
}

export class ChannelPreview
{
    constructor(Device)
    {
        this.Device = Device;

        const ShaderModule = Device.createShaderModule({
            label: "ChannelPreview",
            code:  PreviewShaderSource
        });

        this.Pipeline = Device.createRenderPipeline({
            label:  "ChannelPreview",
            layout: "auto",
            vertex:   { module: ShaderModule, entryPoint: "PreviewVertex" },
            fragment: { module: ShaderModule, entryPoint: "PreviewFragment",
                        targets: [{ format: PreviewFormat }] },
            primitive: { topology: "triangle-list" }
        });

        // 🔴 A MIPMAP-filtering sampler, because the fragment shader samples an explicit mip level to get
        //    a true box average. Leaving mipmapFilter at its "nearest" default makes textureSampleLevel
        //    snap to one mip instead of blending, which is acceptable here, but a linear min filter is
        //    what makes the level itself averaged rather than point-picked.
        this.Sampler = Device.createSampler({
            label: "ChannelPreviewSampler",
            magFilter: "linear", minFilter: "linear", mipmapFilter: "linear"
        });

        // 🔴 ONE target and ONE uniform buffer, reused across every channel of every layer. The alternative
        //    — a target per channel — is six textures per layer of pure overhead for something drawn one at
        //    a time and immediately read back. Each call fully overwrites both, so there is no state to
        //    carry between them.
        this.Target = Device.createTexture({
            label:  "ChannelPreviewTarget",
            size:   [PreviewExtent, PreviewExtent],
            format: PreviewFormat,
            usage:  GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC
        });
        this.TargetView = this.Target.createView();

        this.Uniform = Device.createBuffer({
            label: "ChannelPreviewUniform",
            size:  16,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
        });

        // Mip pyramids for the layer atlases, keyed by the atlas texture. See EnsureMipped.
        this.Mipped = new WeakMap();
        this.MipShader = null;
    }

    // 🔴 A layer atlas is created with ONE mip level, because the paint pass renders into it and nothing
    //    else ever needed a pyramid. Sampling level 4 of a single-level texture silently clamps back to
    //    level 0 — so the preview would work, look plausible, and quietly be a 4-texel point sample of a
    //    1024² atlas. Rather than change every atlas allocation (and pay the pyramid cost on layers that
    //    are never previewed), the preview keeps its OWN mipped copy per atlas and refreshes it here.
    EnsureMipped(Source, Extent)
    {
        const Device = this.Device;
        const Levels = Math.floor(Math.log2(Extent)) + 1;

        let Entry = this.Mipped.get(Source);
        if (!Entry)
        {
            const Texture = Device.createTexture({
                label:  "ChannelPreviewMipped",
                size:   [Extent, Extent],
                format: PreviewFormat,
                mipLevelCount: Levels,
                usage:  GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST |
                        GPUTextureUsage.RENDER_ATTACHMENT
            });
            Entry = { Texture, Levels };
            this.Mipped.set(Source, Entry);
        }

        if (!this.MipShader)
        {
            // A plain box reduction, one pass per level. Same triangle as above.
            this.MipShader = Device.createShaderModule({
                label: "ChannelPreviewReduce",
                code: `
@group(0) @binding(0) var SourceStore : texture_2d<f32>;
@group(0) @binding(1) var SourceSampler : sampler;

struct ReduceVarying
{
    @builtin(position) ClipPosition : vec4f,
    @location(0)       Coordinate   : vec2f,
};

@vertex
fn ReduceVertex(@builtin(vertex_index) Index : u32) -> ReduceVarying
{
    var Corners = array<vec2f, 3>(vec2f(-1.0, -5.0), vec2f(-1.0, 1.0), vec2f(5.0, 1.0));
    let Corner  = Corners[Index];
    var Out : ReduceVarying;
    Out.ClipPosition = vec4f(Corner, 0.0, 1.0);
    Out.Coordinate   = vec2f((Corner.x + 1.0) * 0.5, (1.0 - Corner.y) * 0.5);
    return Out;
}

@fragment
fn ReduceFragment(In : ReduceVarying) -> @location(0) vec4f
{
    // 🔴 Reduced in PREMULTIPLIED form, then un-premultiplied on the way out. The atlases store coverage
    //    as STRAIGHT alpha (the compositor does mix(Below, Above, Layer.a), which is only correct for
    //    straight), and a hardware bilinear average of straight RGBA averages colour and coverage
    //    independently. That pulls the clear colour of UNPAINTED texels into the mean: a sparse red fleck
    //    on a mid-grey transparent atlas reduces toward grey-pink instead of staying red at low coverage,
    //    so a rust preview washes out exactly where the generator is sparsest.
    //
    //    Four explicit taps rather than one bilinear tap, because the weighting has to happen before the
    //    average, and a sampler cannot be told to do that.
    let Size = vec2f(textureDimensions(SourceStore, 0));
    let Half = 0.5 / Size;

    var Colour = vec3f(0.0);
    var Cover  = 0.0;

    for (var Y = -1; Y <= 1; Y += 2)
    {
        for (var X = -1; X <= 1; X += 2)
        {
            let At = In.Coordinate + vec2f(f32(X), f32(Y)) * Half;
            let S  = textureSampleLevel(SourceStore, SourceSampler, At, 0.0);
            Colour += S.rgb * S.a;
            Cover  += S.a;
        }
    }

    // 🔴 Guarded against a fully transparent footprint. Dividing by a zero coverage sum yields NaN, which
    //    propagates up every remaining mip level and lands as a black (or driver-dependent) thumbnail for
    //    the whole channel — and the atlas gutters make an all-transparent 2x2 common, not exotic.
    if (Cover <= 0.0) { return vec4f(0.0, 0.0, 0.0, 0.0); }
    return vec4f(Colour / Cover, Cover * 0.25);
}`
            });

            this.MipPipeline = Device.createRenderPipeline({
                label:  "ChannelPreviewReduce",
                layout: "auto",
                vertex:   { module: this.MipShader, entryPoint: "ReduceVertex" },
                fragment: { module: this.MipShader, entryPoint: "ReduceFragment",
                            targets: [{ format: PreviewFormat }] },
                primitive: { topology: "triangle-list" }
            });

            this.MipSampler = Device.createSampler({
                label: "ChannelPreviewReduceSampler",
                magFilter: "linear", minFilter: "linear"
            });
        }

        const Encoder = Device.createCommandEncoder({ label: "ChannelPreviewMipChain" });

        // Level 0 is a straight copy of the live atlas; every level below is a reduction of the one above.
        Encoder.copyTextureToTexture(
            { texture: Source }, { texture: Entry.Texture }, [Extent, Extent]);

        for (let Level = 1; Level < Entry.Levels; Level += 1)
        {
            const Pass = Encoder.beginRenderPass({
                label: `ChannelPreviewReduce${Level}`,
                colorAttachments: [{
                    view: Entry.Texture.createView({ baseMipLevel: Level, mipLevelCount: 1 }),
                    loadOp: "clear", storeOp: "store",
                    clearValue: { r: 0, g: 0, b: 0, a: 0 }
                }]
            });
            Pass.setPipeline(this.MipPipeline);
            Pass.setBindGroup(0, Device.createBindGroup({
                layout: this.MipPipeline.getBindGroupLayout(0),
                entries: [
                    { binding: 0, resource: Entry.Texture.createView(
                        { baseMipLevel: Level - 1, mipLevelCount: 1 }) },
                    { binding: 1, resource: this.MipSampler }
                ]
            }));
            Pass.draw(3);
            Pass.end();
        }

        Device.queue.submit([Encoder.finish()]);
        return Entry.Texture;
    }

    // Render one channel of one layer to the preview target and read it back as a PNG data address.
    //
    // 🔴 Returns null — never a blank tile — when the channel has no storage. On a lazily-allocated layer
    //    "not painted yet" is the normal state, and a checkerboard tile would claim the atlas exists and
    //    is empty. The caller decides how to show absence.
    async Capture(Layer, ChannelKey, Extent)
    {
        const Slot = CHANNEL_SLOTS[ChannelKey];
        if (!Slot || !Slot.Atlas) { return null; }

        const Source = Layer.Atlas?.[Slot.Atlas];
        if (!Source) { return null; }

        const Device = this.Device;
        const Mipped = this.EnsureMipped(Source, Extent);

        // Component -1 means "pass RGB through"; 0/1/2 splat that component to grey.
        Device.queue.writeBuffer(this.Uniform, 0, new Float32Array(
            [Slot.Component ?? -1, 8.0, 0.0, 0.0]));

        const Encoder = Device.createCommandEncoder({ label: "ChannelPreview" });
        const Pass = Encoder.beginRenderPass({
            label: "ChannelPreview",
            colorAttachments: [{
                view: this.TargetView, loadOp: "clear", storeOp: "store",
                clearValue: { r: 0, g: 0, b: 0, a: 1 }
            }]
        });
        Pass.setPipeline(this.Pipeline);
        Pass.setBindGroup(0, Device.createBindGroup({
            layout: this.Pipeline.getBindGroupLayout(0),
            entries: [
                { binding: 0, resource: { buffer: this.Uniform } },
                { binding: 1, resource: Mipped.createView() },
                { binding: 2, resource: this.Sampler }
            ]
        }));
        Pass.draw(3);
        Pass.end();

        const RowByteStride = ResolvePreviewRowStride();
        const ReadbackStore = Device.createBuffer({
            label: "ChannelPreviewReadback",
            size:  RowByteStride * PreviewExtent,
            usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ
        });
        Encoder.copyTextureToBuffer(
            { texture: this.Target },
            { buffer: ReadbackStore, bytesPerRow: RowByteStride },
            [PreviewExtent, PreviewExtent]);

        Device.queue.submit([Encoder.finish()]);

        await ReadbackStore.mapAsync(GPUMapMode.READ);
        const Raw = new Uint8Array(ReadbackStore.getMappedRange()).slice();
        ReadbackStore.unmap();
        ReadbackStore.destroy();

        // 🔴 The readback stride is 256 B while a preview row is 64*4 = 256 B — equal here, but that is a
        //    coincidence of this extent and NOT something to rely on. Re-packing row by row keeps the
        //    preview correct if PreviewExtent ever changes to a value whose row is not 256-aligned.
        const Packed = new Uint8ClampedArray(PreviewExtent * PreviewExtent * 4);
        let   Ink    = 0;

        for (let Row = 0; Row < PreviewExtent; Row += 1)
        {
            for (let Column = 0; Column < PreviewExtent; Column += 1)
            {
                const From = Row * RowByteStride + Column * 4;
                const To   = (Row * PreviewExtent + Column) * 4;
                Packed[To + 0] = Raw[From + 0];
                Packed[To + 1] = Raw[From + 1];
                Packed[To + 2] = Raw[From + 2];
                Packed[To + 3] = 255;
                Ink += Raw[From + 0] + Raw[From + 1] + Raw[From + 2];
            }
        }

        const Canvas = document.createElement("canvas");
        Canvas.width  = PreviewExtent;
        Canvas.height = PreviewExtent;
        const Context = Canvas.getContext("2d");
        const Image   = Context.createImageData(PreviewExtent, PreviewExtent);
        Image.data.set(Packed);
        Context.putImageData(Image, 0, 0);

        return {
            Channel: ChannelKey,
            Atlas:   Slot.Atlas,
            Extent:  PreviewExtent,
            // A probe assertion needs something numeric: the mean ink over the tile. Two previews of
            // different content cannot both be flat at the same value unless the pass did nothing.
            MeanInk: Ink / (PreviewExtent * PreviewExtent * 3) / 255,
            Image:   Canvas.toDataURL("image/png")
        };
    }

    Release()
    {
        this.Target?.destroy();
        this.Uniform?.destroy();
    }
}
