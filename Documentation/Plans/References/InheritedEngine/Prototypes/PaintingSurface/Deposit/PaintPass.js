/*====================================================================================================================================
                                                        PAINTPASS.JS
====================================================================================================================================*/
// 🧩 Rasterize dabs straight into the atlas by substituting UV for clip position, unprojecting per fragment

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// One dab's uniform block. 📝 vec3f fields occupy 16 bytes in a uniform block, so every one is padded
//    to vec4f explicitly. Packing them as 12 slides every later field and the failure is silent.
//
// 🔴 Five vec4f = 20 floats: Centre, Normal, Ink, Control, ChannelMask. This must match the WGSL struct
//    exactly — it is the minBindingSize on the bind layout, so a value SMALLER than the struct is a
//    validation error at bind time, while a value larger silently over-reserves. It was 24 while the
//    struct held four vec4f, which over-reserved harmlessly; adding the fifth made 20 the true size.
const DabUniformFloatCount = 20;
const DabUniformByteLength = DabUniformFloatCount * 4;

// Dabs are staged into one buffer and drawn with a dynamic offset, so a whole stroke segment costs a
// single write. 📝 The offset must be a multiple of minUniformBufferOffsetAlignment, which is 256 on
//    every current adapter — not the 96 bytes the block actually needs.
export const DabUniformStride = 256;

// How many dabs one staged buffer holds before it is flushed.
export const DabBatchCapacity = 256;

//------------------------------------------------------------------------------------------------------------------------
//                                                      SHADER SOURCE
//------------------------------------------------------------------------------------------------------------------------

// 📝 The load-bearing trick, from the research: the VERTEX stage writes the UV as the clip position,
//    so the rasterizer sweeps the ATLAS rather than the screen, while the object-space position rides
//    along as a varying. One pass therefore paints, bakes and stamps decals — the fragment shader is
//    the only thing that differs between them.
const PaintShaderSource = `
struct DabUniform
{
    Centre       : vec4f,   // xyz = object-space dab centre, w = radius
    Normal       : vec4f,   // xyz = surface normal at the dab,  w = hardness
    Ink          : vec4f,   // rgb = colour, a = strength this dab contributes
    Control      : vec4f,   // x = normal cutoff, y = erase flag, z = unused, w = unused

    // 🔴 Which components of THIS atlas the dab is allowed to touch, 1 or 0 per channel. A layer
    //    painting only roughness must leave metallic and height exactly as they were where it deposits
    //    — they share one RGBA8 texture. Without the mask, a roughness stroke over a layer that already
    //    carries height resets the height to the brush's default wherever the two overlap.
    ChannelMask  : vec4f,
};

@group(0) @binding(0) var<uniform> Dab : DabUniform;

struct PaintVarying
{
    @builtin(position) ClipPosition   : vec4f,
    @location(0)       ObjectPosition : vec3f,
    @location(1)       ObjectNormal   : vec3f,
};

@vertex
fn PaintVertex(
    @location(0) Position   : vec3f,
    @location(1) Coordinate : vec2f,
    @location(2) Normal     : vec3f) -> PaintVarying
{
    var Out : PaintVarying;

    // 📝 The atlas is authored with v increasing UPWARD while the render target's y runs downward,
    //    so v is flipped here. SurfaceRasterization flips it again on read; the two must agree or
    //    every dab lands mirrored vertically — invisible on a symmetric pattern, obvious on a face.
    let Target = vec2f(Coordinate.x, 1.0 - Coordinate.y);

    // UV in [0,1] mapped to clip space [-1,1], with y negated for the downward-running target.
    Out.ClipPosition   = vec4f(Target.x * 2.0 - 1.0, 1.0 - Target.y * 2.0, 0.0, 1.0);
    Out.ObjectPosition = Position;
    Out.ObjectNormal   = Normal;
    return Out;
}

@fragment
fn PaintFragment(In : PaintVarying) -> @location(0) vec4f
{
    let Centre   = Dab.Centre.xyz;
    let Radius   = Dab.Centre.w;
    let Hardness = Dab.Normal.w;

    let Offset   = In.ObjectPosition - Centre;
    let Distance = length(Offset);

    // Outside the dab sphere contributes nothing. 📝 Discard rather than a zero-alpha write: the
    //    blend is a straight src-over and a zero-alpha source is already a no-op, but discarding
    //    keeps the atlas untouched where it matters for the coverage probe.
    if (Distance >= Radius) { discard; }

    // Angular falloff: a dab must not paint through to surfaces facing away from it. This is the
    // cheap two-threshold test rather than a depth-buffer occlusion pass.
    let SurfaceNormal = normalize(In.ObjectNormal);
    let DabNormal     = normalize(Dab.Normal.xyz);
    let Facing        = dot(SurfaceNormal, DabNormal);
    let Cutoff        = Dab.Control.x;

    if (Facing < Cutoff) { discard; }

    // 📝 The angular term ramps across the band between the cutoff and fully-facing rather than
    //    switching hard, otherwise a dab on a curved surface leaves a visible polygonal rim.
    let AngularSpan = max(1.0 - Cutoff, 1e-4);
    let Angular     = clamp((Facing - Cutoff) / AngularSpan, 0.0, 1.0);

    // Radial falloff. Must match EvaluateFalloff in DabFootprint.js — the probe checks one against
    // the other, so a change here without a change there will be caught.
    let Normalized = Distance / Radius;
    let CoreEdge   = clamp(Hardness, 0.0, 0.999);

    var Radial = 1.0;
    if (Normalized > CoreEdge)
    {
        let Shoulder = (Normalized - CoreEdge) / (1.0 - CoreEdge);
        Radial = 1.0 - (Shoulder * Shoulder * (3.0 - 2.0 * Shoulder));
    }

    let Strength = Dab.Ink.a * Radial * Angular;
    if (Strength <= 0.0) { discard; }

    // 🔴 The per-component mask is applied to the STRENGTH, not to the colour, and it is what makes a
    //    single RGBA8 atlas carry three independent channels. Masking the colour instead (writing the
    //    destination's own value into unmasked components) is impossible here: the fragment stage cannot
    //    read its own render target, so the only way to leave a component untouched is to give it zero
    //    blend weight. That needs a per-component alpha, which one src-alpha blend cannot express —
    //    hence the write mask below, set per draw on the pipeline rather than in the shader.
    //
    // 📝 So the mask arrives twice: as a GPUColorWrite mask on the pipeline (which is what actually
    //    protects the untouched components) and here as a guard that discards a dab whose every
    //    component is masked off, rather than paying for a no-op draw.
    let Live = Dab.ChannelMask.x + Dab.ChannelMask.y + Dab.ChannelMask.z;
    if (Live <= 0.0) { discard; }

    // Erase writes the destination toward the ground colour; paint writes toward the ink. Both ride
    // the same src-over blend, so erase is just a differently-coloured source.
    return vec4f(Dab.Ink.rgb, Strength);
}`;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

export class PaintPass
{
    constructor(Device, AtlasFormat)
    {
        this.Device      = Device;
        this.AtlasFormat = AtlasFormat;

        const ShaderModule = Device.createShaderModule({
            label: "PaintPass",
            code:  PaintShaderSource
        });
        this.ShaderModule = ShaderModule;

        this.DabUniform = Device.createBuffer({
            label: "PaintDabUniform",
            size:  DabUniformStride * DabBatchCapacity,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
        });

        this.BindLayout = Device.createBindGroupLayout({
            label: "PaintBindLayout",
            entries: [{
                binding:    0,
                visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
                buffer:     { type: "uniform", hasDynamicOffset: true, minBindingSize: DabUniformByteLength }
            }]
        });

        this.BindGroup = Device.createBindGroup({
            label:  "PaintBindGroup",
            layout: this.BindLayout,
            entries: [{
                binding:  0,
                resource: { buffer: this.DabUniform, offset: 0, size: DabUniformByteLength }
            }]
        });

        // 🔴 One pipeline per RGB write-mask combination, built up front and cached. The mask cannot
        //    live in the shader — a fragment cannot read its own target, so the only way to leave a
        //    component untouched is to stop the hardware writing it. Alpha is ALWAYS written: it is the
        //    layer's coverage, and a dab must mark a texel as painted no matter which channel it drove.
        //
        // 📝 Eight combinations, seven of them useful (mask 0 never draws). Building them eagerly costs
        //    microseconds at startup and removes a lazy-creation branch from the per-stroke path.
        this.PipelineByMask = [];

        for (let Mask = 0; Mask < 8; Mask += 1)
        {
            let Written = GPUColorWrite.ALPHA;
            if (Mask & 1) { Written |= GPUColorWrite.RED;   }
            if (Mask & 2) { Written |= GPUColorWrite.GREEN; }
            if (Mask & 4) { Written |= GPUColorWrite.BLUE;  }

            this.PipelineByMask[Mask] = Device.createRenderPipeline({
                label:  `PaintPassMask${Mask}`,
                layout: Device.createPipelineLayout({ bindGroupLayouts: [this.BindLayout] }),
                vertex: {
                    module:     ShaderModule,
                    entryPoint: "PaintVertex",
                    buffers:    [PaintVertexLayout()]
                },
                fragment: {
                    module:     ShaderModule,
                    entryPoint: "PaintFragment",
                    targets: [{
                        format:    AtlasFormat,
                        writeMask: Written,
                        // Straight source-over. The dab's computed strength is the source alpha.
                        blend: {
                            color: { srcFactor: "src-alpha", dstFactor: "one-minus-src-alpha", operation: "add" },
                            alpha: { srcFactor: "one",       dstFactor: "one-minus-src-alpha", operation: "add" }
                        }
                    }]
                },
                // 📝 No culling and no depth. The pass sweeps UV space, where winding carries no meaning
                //    and there is nothing to occlude — the angular cutoff in the fragment stage does the
                //    job a depth test would do in screen space.
                primitive: { topology: "triangle-list", cullMode: "none" }
            });
        }

        // The all-channels pipeline, which is what the pre-layer call sites already expect.
        this.Pipeline = this.PipelineByMask[7];
    }

    // Stage a run of dabs into the uniform buffer. Returns how many were written.
    //
    // `Write` is the optional per-atlas channel write from ResolveAtlasWrite: { Value, Mask }. Omitting
    // it paints base colour with the brush ink into all three components, which is the pre-layer
    // behaviour every existing caller and probe depends on.
    StageDabs(Dabs, Brush, Write)
    {
        const Count   = Math.min(Dabs.length, DabBatchCapacity);
        const Staging = new Float32Array((DabUniformStride / 4) * Count);

        for (let Ordinal = 0; Ordinal < Count; Ordinal += 1)
        {
            const Dab  = Dabs[Ordinal];
            const Base = Ordinal * (DabUniformStride / 4);

            Staging[Base + 0] = Dab.Position[0];
            Staging[Base + 1] = Dab.Position[1];
            Staging[Base + 2] = Dab.Position[2];
            Staging[Base + 3] = Brush.Radius;

            Staging[Base + 4] = Dab.Normal[0];
            Staging[Base + 5] = Dab.Normal[1];
            Staging[Base + 6] = Dab.Normal[2];
            Staging[Base + 7] = Brush.Hardness;

            // 📝 Pressure scales the per-dab strength, never the recorded stroke. The record keeps raw
            //    pressure so a different response curve can be applied on replay.
            const Pressure = Dab.Pressure ?? 1.0;

            // The deposited value is the channel write when painting a layer, the brush ink otherwise.
            const Value = Write ? Write.Value : Brush.Ink;
            const Mask  = Write ? Write.Mask  : [1, 1, 1];

            Staging[Base +  8] = Value[0];
            Staging[Base +  9] = Value[1];
            Staging[Base + 10] = Value[2];
            Staging[Base + 11] = Brush.Flow * Pressure;

            Staging[Base + 12] = Brush.NormalCutoff;
            Staging[Base + 13] = Brush.Erase ? 1.0 : 0.0;
            Staging[Base + 14] = 0.0;
            Staging[Base + 15] = 0.0;

            Staging[Base + 16] = Mask[0];
            Staging[Base + 17] = Mask[1];
            Staging[Base + 18] = Mask[2];
            Staging[Base + 19] = 0.0;
        }

        this.Device.queue.writeBuffer(this.DabUniform, 0, Staging);
        return Count;
    }

    // Encode one pass that lays `Count` staged dabs into the atlas.
    //
    // 📝 loadOp is "load", never "clear" — the atlas already holds every earlier stroke.
    //
    // `Write` is the same channel write handed to StageDabs. Its Mask selects which of the eight
    // write-masked pipelines runs, so components this layer does not paint are left untouched.
    Encode(Encoder, AtlasView, Surface, Count, Write)
    {
        if (Count <= 0) { return; }

        // 🔴 The mask must match the one staged into the uniform. They are derived from the same Write
        //    object for exactly that reason — computing the pipeline mask from anything else lets the
        //    hardware write a component the shader believes it masked off, or vice versa.
        const Mask     = Write ? Write.Mask : [1, 1, 1];
        const MaskIndex = (Mask[0] ? 1 : 0) | (Mask[1] ? 2 : 0) | (Mask[2] ? 4 : 0);
        if (MaskIndex === 0) { return; }

        const Pass = Encoder.beginRenderPass({
            label: "PaintPass",
            colorAttachments: [{
                view:    AtlasView,
                loadOp:  "load",
                storeOp: "store"
            }]
        });

        Pass.setPipeline(this.PipelineByMask[MaskIndex]);
        Pass.setVertexBuffer(0, Surface.VertexStore);
        Pass.setIndexBuffer(Surface.IndexStore, "uint32");

        // 📝 One draw per dab. Dabs within a stroke overlap and the blend is order-dependent, so they
        //    cannot be collapsed into one instanced draw without changing the result.
        for (let Ordinal = 0; Ordinal < Count; Ordinal += 1)
        {
            Pass.setBindGroup(0, this.BindGroup, [Ordinal * DabUniformStride]);
            Pass.drawIndexed(Surface.IndexCount);
        }

        Pass.end();
    }
}

// The paint pass consumes the same interleaved stream as the surface raster.
function PaintVertexLayout()
{
    return {
        arrayStride: 32,
        attributes: [
            { shaderLocation: 0, offset: 0,  format: "float32x3" },
            { shaderLocation: 1, offset: 12, format: "float32x2" },
            { shaderLocation: 2, offset: 20, format: "float32x3" }
        ]
    };
}
