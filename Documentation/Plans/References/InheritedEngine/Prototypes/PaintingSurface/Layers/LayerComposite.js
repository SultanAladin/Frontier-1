/*====================================================================================================================================
                                                     LAYERCOMPOSITE.JS
====================================================================================================================================*/
// 🧩 Flatten the layer stack into three resolved channel atlases, one blend-mode-aware pass per layer

import { CHANNEL_ATLASES, ChannelAtlasFormat } from "./ChannelSet.js";
import { BlendShaderIndex }                    from "./LayerStack.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// vec4f control + vec4f settings = 8 floats. Padded to the 256-byte dynamic-offset alignment on write.
const CompositeUniformFloatCount = 8;
const CompositeUniformByteLength = CompositeUniformFloatCount * 4;
const CompositeUniformStride     = 256;

// Room for every layer × every atlas in one staged write.
const CompositeSlotCapacity = 64;

//------------------------------------------------------------------------------------------------------------------------
//                                                      SHADER SOURCE
//------------------------------------------------------------------------------------------------------------------------

// 📝 A full-screen triangle over the resolved atlas, one draw per layer per atlas, blending in the
//    fragment stage. The obvious alternative — bind all N layers at once and loop inside the shader —
//    needs a bind group whose size depends on the layer count, which means rebuilding the pipeline
//    whenever a layer is added. Drawing once per layer keeps one static pipeline.
const CompositeShaderSource = `
struct CompositeUniform
{
    Control  : vec4f,   // x = blend mode index, y = layer opacity [0,1], z = atlas kind, w = unused
    Settings : vec4f,   // x = mask applied (0/1), y = mask strength [0,1], z = mask inverted (0/1), w = unused
};

@group(0) @binding(0) var<uniform> Composite : CompositeUniform;
@group(0) @binding(1) var          LayerStore : texture_2d<f32>;
@group(0) @binding(2) var          LayerSampler : sampler;

// 🔴 The already-resolved result BENEATH this layer, bound as a separate texture. It cannot be the
//    render target of this same pass — sampling a texture while writing it is a read-write hazard — so
//    the caller ping-pongs: read Below from one target, write to the other, swap, repeat per layer.
//    An earlier revision sampled LayerStore for both operands, which made Multiply compute Above*Above
//    and turned Darken into a no-op. Both look plausible on a uniform test fill and wrong on real paint.
@group(0) @binding(3) var          BelowStore : texture_2d<f32>;

// 🔴 The layer's resolved mask, or a 1x1 opaque white stand-in when the layer carries none. A bind group
//    has to satisfy its layout in full, so the slot cannot simply be left empty — and white reads as
//    "applies everywhere", which is exactly the unmasked result. The Settings.x gate below means the
//    stand-in is never even sampled in the common case, but binding white keeps the two paths identical.
@group(0) @binding(4) var          MaskStore : texture_2d<f32>;

struct CompositeVarying
{
    @builtin(position) ClipPosition : vec4f,
    @location(0)       Coordinate   : vec2f,
};

@vertex
fn CompositeVertex(@builtin(vertex_index) Index : u32) -> CompositeVarying
{
    // 📝 One oversized triangle, not two triangles. A quad seams along its diagonal under some
    //    rasterizers; a single triangle covering the target cannot.
    //
    // 📝 Vertices at ±5, not the classic ±3. The ±3 triangle is exactly inscribed — its hypotenuse passes
    //    precisely through the clip corner (1,-1) — so that corner falls on a triangle edge and the fill
    //    rule decides it. This was widened while chasing an uncovered corner on the generator pass, which
    //    turned out to be that pass's coverage threshold rather than anything to do with the triangle, so no
    //    dropped corner has ever actually been measured here. Kept because it costs nothing: Coordinate is
    //    derived FROM Corner below, so it scales with the geometry and the coordinate at any given clip
    //    position is unchanged, and the surplus area is clipped away.
    var Corners = array<vec2f, 3>(vec2f(-1.0, -5.0), vec2f(-1.0, 1.0), vec2f(5.0, 1.0));
    let Corner  = Corners[Index];

    var Out : CompositeVarying;
    Out.ClipPosition = vec4f(Corner, 0.0, 1.0);
    // Clip y runs up, texture v runs down.
    Out.Coordinate   = vec2f((Corner.x + 1.0) * 0.5, (1.0 - Corner.y) * 0.5);
    return Out;
}

// 🔴 Blend maths operate on the layer's value against what is already resolved beneath it. Every mode
//    here is the standard formula EXCEPT that they are applied per component and then folded back
//    against coverage below — a Multiply layer must not darken texels it never painted.
fn ApplyBlend(Mode : i32, Below : vec3f, Above : vec3f) -> vec3f
{
    switch (Mode)
    {
        case 1:  { return Below * Above; }                                  // Multiply
        case 2:  { return 1.0 - (1.0 - Below) * (1.0 - Above); }            // Screen
        case 3:  {                                                          // Overlay
            let Low  = 2.0 * Below * Above;
            let High = 1.0 - 2.0 * (1.0 - Below) * (1.0 - Above);
            return select(High, Low, Below < vec3f(0.5));
        }
        case 4:  { return min(Below + Above, vec3f(1.0)); }                 // Add
        case 5:  { return min(Below, Above); }                              // Darken
        case 6:  { return min(Below + Above, vec3f(1.0)); }                 // Linear Dodge
        default: { return Above; }                                          // Normal
    }
}

@fragment
fn CompositeFragment(In : CompositeVarying) -> @location(0) vec4f
{
    let Layer = textureSampleLevel(LayerStore, LayerSampler, In.Coordinate, 0.0);
    let Below = textureSampleLevel(BelowStore, LayerSampler, In.Coordinate, 0.0);

    let Mode    = i32(Composite.Control.x);
    let Opacity = Composite.Control.y;

    // 🔴 Coverage gates EVERYTHING. Layer.a is where this layer was actually painted; multiplying the
    //    blend weight by it is what stops an unpainted region of a Multiply layer from blacking out the
    //    surface below. Reading only the RGB and trusting the blend mode to be a no-op over untouched
    //    texels is wrong for every mode except Normal.
    //
    // 🔴 The mask is a THIRD factor on that same weight, not a separate operation. A mask says "apply
    //    this layer here, by this much", which is precisely a per-texel opacity — so it multiplies in
    //    alongside coverage and the layer's own opacity, and every blend mode gets it for free. Applying
    //    it to the layer's RGB instead would darken the painted colour toward black wherever the mask is
    //    low, which is a completely different picture from revealing what is underneath.
    var MaskWeight = 1.0;
    if (Composite.Settings.x > 0.5)
    {
        // Greyscale, read from red — every mask write stores the same scalar in all three components.
        var Sampled = textureSampleLevel(MaskStore, LayerSampler, In.Coordinate, 0.0).r;
        if (Composite.Settings.z > 0.5) { Sampled = 1.0 - Sampled; }
        // 📝 Mask strength blends the mask toward "fully applied" rather than scaling it toward zero: at
        //    strength 0 the mask must vanish (layer applies everywhere), not black the layer out.
        MaskWeight = mix(1.0, Sampled, clamp(Composite.Settings.y, 0.0, 1.0));
    }

    let Weight = Layer.a * Opacity * MaskWeight;

    // 🔴 The shader owns the WHOLE compositing equation, so the pipeline blend is disabled and this
    //    pass writes the final value. An unpainted texel must therefore pass the value below through
    //    unchanged rather than discard — discarding would leave whatever the ping-pong target happened
    //    to hold from two layers ago, which is stale paint, not the surface beneath.
    let Blended = ApplyBlend(Mode, Below.rgb, Layer.rgb);

    return vec4f(mix(Below.rgb, Blended, Weight), max(Below.a, Weight));
}`;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

export class LayerComposite
{
    constructor(Device, Extent)
    {
        this.Device = Device;
        this.Extent = Extent;

        const ShaderModule = Device.createShaderModule({
            label: "LayerComposite",
            code:  CompositeShaderSource
        });
        this.ShaderModule = ShaderModule;

        // ---- the resolved output ------------------------------------------------------------------
        // 🔴 Ping-pong pairs, not single targets. A blend mode like Multiply needs to READ what is
        //    already resolved beneath while WRITING the new result, and sampling a texture that is
        //    also the current render attachment is a read-write hazard. Each atlas therefore has two
        //    textures and the pass alternates between them, one layer at a time.
        this.Resolved = {};
        this.Scratch  = {};
        this.View     = {};

        for (const Descriptor of CHANNEL_ATLASES)
        {
            const Make = (Suffix) => Device.createTexture({
                label:  `Resolved${Descriptor.Key}${Suffix}`,
                size:   [Extent, Extent],
                format: ChannelAtlasFormat,
                usage:  GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING |
                        GPUTextureUsage.COPY_SRC          | GPUTextureUsage.COPY_DST
            });

            this.Resolved[Descriptor.Key] = Make("A");
            this.Scratch[Descriptor.Key]  = Make("B");
        }

        this.RefreshViews();

        this.Uniform = Device.createBuffer({
            label: "CompositeUniform",
            size:  CompositeUniformStride * CompositeSlotCapacity,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
        });

        this.Sampler = Device.createSampler({
            label:        "CompositeSampler",
            magFilter:    "nearest",
            minFilter:    "nearest",
            addressModeU: "clamp-to-edge",
            addressModeV: "clamp-to-edge"
        });

        // The stand-in bound at the mask slot for every unmasked layer: 1x1, opaque white, "applies
        // everywhere". Built once and shared by every layer that carries no mask.
        this.MaskStandIn = Device.createTexture({
            label:  "CompositeMaskStandIn",
            size:   [1, 1],
            format: ChannelAtlasFormat,
            usage:  GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING
        });
        const StandInSeed = Device.createCommandEncoder({ label: "CompositeMaskStandInClear" });
        StandInSeed.beginRenderPass({
            colorAttachments: [{ view: this.MaskStandIn.createView(),
                                 clearValue: { r: 1, g: 1, b: 1, a: 1 },
                                 loadOp: "clear", storeOp: "store" }]
        }).end();
        Device.queue.submit([StandInSeed.finish()]);
        this.MaskStandInView = this.MaskStandIn.createView();

        this.BindLayout = Device.createBindGroupLayout({
            label: "CompositeBindLayout",
            entries: [
                { binding: 0, visibility: GPUShaderStage.FRAGMENT, buffer:  { type: "uniform", hasDynamicOffset: true, minBindingSize: CompositeUniformByteLength } },
                { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
                { binding: 2, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "non-filtering" } },
                { binding: 3, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
                { binding: 4, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } }
            ]
        });

        this.Pipeline = Device.createRenderPipeline({
            label:  "LayerComposite",
            layout: Device.createPipelineLayout({ bindGroupLayouts: [this.BindLayout] }),
            vertex:   { module: ShaderModule, entryPoint: "CompositeVertex" },
            fragment: {
                module:     ShaderModule,
                entryPoint: "CompositeFragment",
                // 🔴 NO blend state. The fragment stage reads the destination through BelowStore and
                //    returns the finished value, so hardware blending would apply the coverage weight a
                //    second time — a 50%-opacity layer would land at 25%. The shader's mix() IS the
                //    blend; adding a src-alpha blend on top of it double-counts.
                targets: [{ format: ChannelAtlasFormat }]
            },
            primitive: { topology: "triangle-list", cullMode: "none" }
        });
    }

    RefreshViews()
    {
        for (const Descriptor of CHANNEL_ATLASES)
        {
            this.View[Descriptor.Key] = this.Resolved[Descriptor.Key].createView();
        }
    }

    // Flatten the whole stack. Called whenever the stack changes or a stroke lands.
    Resolve(Stack)
    {
        const Device  = this.Device;
        const Encoder = Device.createCommandEncoder({ label: "LayerCompositeResolve" });

        // Stage every (layer, atlas) pair's control block up front, so the whole resolve costs one
        // buffer write regardless of how many layers there are.
        const Staging = new Float32Array((CompositeUniformStride / 4) * CompositeSlotCapacity);
        const Visible = [...Stack.BottomUp()].filter(L => L.Shown && L.Opacity > 0);

        let Slot = 0;
        const SlotOf = new Map();

        for (const Layer of Visible)
        {
            for (const Descriptor of CHANNEL_ATLASES)
            {
                if (Slot >= CompositeSlotCapacity) { break; }

                // 🔴 Skip an atlas this layer has never written. Storage is allocated lazily, so an
                //    unpainted channel has no texture to bind at all — and binding a placeholder instead
                //    would be worse than skipping: the compositor weights by the layer's own alpha, and a
                //    stand-in reading as covered would let an untouched layer overwrite everything below.
                //    No slot means no pass, which is exactly "this layer contributes nothing here".
                if (!Layer.AtlasView?.[Descriptor.Key]) { continue; }

                const Base = Slot * (CompositeUniformStride / 4);

                Staging[Base + 0] = BlendShaderIndex(Layer.Blend);
                Staging[Base + 1] = Layer.Opacity / 100;
                Staging[Base + 2] = 0.0;
                Staging[Base + 3] = 0.0;

                // 🔴 The gate is `Masked`, which demands BOTH an enabled mask and resolved storage. An
                //    enabled-but-unevaluated mask has no texture to bind, and gating on Mask.Enabled alone
                //    would bind the white stand-in while telling the shader to apply it — harmless today,
                //    but it makes "mask enabled" and "mask readable" two different truths in one frame.
                const Masked = Layer.Masked === true;
                Staging[Base + 4] = Masked ? 1.0 : 0.0;
                Staging[Base + 5] = Masked ? (Layer.Mask.Opacity ?? 100) / 100 : 0.0;
                Staging[Base + 6] = (Masked && Layer.Mask.Invert) ? 1.0 : 0.0;
                Staging[Base + 7] = 0.0;

                SlotOf.set(`${Layer.Token}:${Descriptor.Key}`, Slot);
                Slot += 1;
            }
        }

        Device.queue.writeBuffer(this.Uniform, 0, Staging);

        for (const Descriptor of CHANNEL_ATLASES)
        {
            const Key = Descriptor.Key;

            // 🔴 The resolved atlas is cleared to the SAME value a layer clears to, not to zero. The
            //    surface shader reads the resolved atlas wherever the model has UV, including regions no
            //    layer has painted, and a zeroed height there would make the derived normal blow up.
            const [R, G, B, A] = Descriptor.Clear;
            const ClearValue   = { r: R, g: G, b: B, a: A };

            // Ping-pong pair. `Read` is what the shader samples as BelowStore, `Write` is the attachment.
            // They swap after every layer, so the value each layer composites over is the finished
            // result of all the layers beneath it.
            let Read  = this.Scratch[Key];
            let Write = this.Resolved[Key];

            // 🔴 The FIRST read must be the clear value, not the scratch texture's previous contents.
            //    Skipping this seed leaves the bottom layer compositing over last frame's resolve, which
            //    on a Normal-blend bottom layer is invisible (Weight 1 replaces it) and on a Multiply
            //    bottom layer compounds frame over frame into black.
            Encoder.beginRenderPass({
                label: `CompositeSeed${Key}`,
                colorAttachments: [{ view: Read.createView(), clearValue: ClearValue, loadOp: "clear", storeOp: "store" }]
            }).end();

            let Composited = 0;

            for (const Layer of Visible)
            {
                const Index = SlotOf.get(`${Layer.Token}:${Key}`);
                if (Index === undefined) { continue; }

                const Pass = Encoder.beginRenderPass({
                    label: `Composite${Key}${Layer.Token}`,
                    colorAttachments: [{
                        // 📝 loadOp "clear" every time, and that is not wasteful: the shader writes
                        //    every texel unconditionally (unpainted ones pass Below through), so there
                        //    is nothing in the target worth loading. "load" would only add a dependency.
                        view:       Write.createView(),
                        clearValue: ClearValue,
                        loadOp:     "clear",
                        storeOp:    "store"
                    }]
                });

                const Group = Device.createBindGroup({
                    label:  `CompositeGroup${Key}${Layer.Token}`,
                    layout: this.BindLayout,
                    entries: [
                        { binding: 0, resource: { buffer: this.Uniform, offset: 0, size: CompositeUniformByteLength } },
                        { binding: 1, resource: Layer.AtlasView[Key] },
                        { binding: 2, resource: this.Sampler },
                        { binding: 3, resource: Read.createView() },
                        { binding: 4, resource: Layer.Masked ? Layer.MaskView : this.MaskStandInView }
                    ]
                });

                Pass.setPipeline(this.Pipeline);
                Pass.setBindGroup(0, Group, [Index * CompositeUniformStride]);
                Pass.draw(3);
                Pass.end();

                const Swap = Read; Read = Write; Write = Swap;
                Composited += 1;
            }

            // 🔴 After the swap, the finished result lives in `Read`, whichever texture that now is. If
            //    an odd number of layers composited, that is Scratch — so the pair is re-labelled rather
            //    than copied. Assuming the answer is always in Resolved shows the wrong atlas whenever
            //    the visible layer count is odd, which flips as layers are hidden.
            this.Resolved[Key] = Read;
            this.Scratch[Key]  = Write;

            // Nothing visible at all still needs a defined resolved atlas, or the surface samples stale
            // contents from the previous frame. The seed pass above already cleared it, so this is only
            // a note that the empty case is handled, not a second clear.
            void Composited;
        }

        Device.queue.submit([Encoder.finish()]);
        this.RefreshViews();
    }

    Release()
    {
        for (const Descriptor of CHANNEL_ATLASES)
        {
            this.Resolved[Descriptor.Key].destroy();
            this.Scratch[Descriptor.Key].destroy();
        }

        this.MaskStandIn?.destroy();
        this.MaskStandIn     = null;
        this.MaskStandInView = null;
    }
}
