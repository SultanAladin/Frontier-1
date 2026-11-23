/*====================================================================================================================================
                                                        LAYERMASK.JS
====================================================================================================================================*/
// 🧩 The per-layer mask: a greyscale atlas evaluated from an ordered component stack — fill, paint, generator, levels

import { ChannelAtlasFormat } from "./ChannelSet.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Same format as a channel atlas, so the paint pass can rasterize dabs straight into a mask component
//    with no second pipeline. Only the red component is ever read back out — the mask is greyscale — but
//    the brush writes all three, which keeps the deposited value identical under any component the
//    compositor happens to sample.
export const MaskAtlasFormat = ChannelAtlasFormat;

// 🔴 The category is a CAPABILITY, exactly as a layer kind is. Only `paint` accepts brush strokes, and it
//    is the only one that owns storage of its own; the other three are evaluated from their parameters
//    every time the sequence runs. Treating them uniformly would mean allocating 4 MiB for a Levels
//    component that holds no image at all.
export const MASK_COMPONENT_CATEGORY = {
    paint: {
        Label:     "Paint",
        Paintable: true,
        Summary:   "Hand-painted mask strokes.",
        Defaults:  {}
    },
    fill: {
        Label:     "Fill",
        Paintable: false,
        Summary:   "Uniform fill region.",
        Defaults:  { Value: 1.0 }
    },
    generator: {
        Label:     "Generator",
        Paintable: false,
        Summary:   "Procedural mottling over the UV atlas.",
        Defaults:  { Scale: 0.5, Contrast: 0.5, Amount: 1.0, Seed: 12 }
    },
    levels: {
        Label:     "Levels",
        Paintable: false,
        Summary:   "Remap mask contrast and range.",
        Defaults:  { Low: 0.0, High: 1.0 }
    }
};

export const MASK_COMPONENT_ORDER = ["paint", "fill", "generator", "levels"];

// The four sliders a Levels / Generator component exposes, so the inspector cannot drift from the shader.
export const MASK_COMPONENT_PARAMS = {
    fill:      [{ Key: "Value",    Label: "Value",    Min: 0,    Max: 1,   Step: 0.01 }],
    generator: [{ Key: "Scale",    Label: "Scale",    Min: 0.02, Max: 1,   Step: 0.01 },
                { Key: "Contrast", Label: "Contrast", Min: 0,    Max: 1,   Step: 0.01 },
                { Key: "Amount",   Label: "Amount",   Min: 0,    Max: 1,   Step: 0.01 },
                // 🔴 Integer seed, step 1 — a fractional seed walks a continuum of near-identical
                //    patterns instead of giving the discrete re-rolls the control implies.
                { Key: "Seed",     Label: "Seed",     Min: 0,    Max: 999, Step: 1 }],
    levels:    [{ Key: "Low",      Label: "Low",      Min: 0,    Max: 1,   Step: 0.01 },
                { Key: "High",     Label: "High",     Min: 0,    Max: 1,   Step: 0.01 }],
    paint:     []
};

const CategoryIndex = { fill: 0, paint: 1, generator: 2, levels: 3 };

let ComponentSequence = 0;
const NextComponentToken = () => `M${(ComponentSequence += 1).toString().padStart(3, "0")}`;

//------------------------------------------------------------------------------------------------------------------------
//                                                     MASK MODEL
//------------------------------------------------------------------------------------------------------------------------

// One entry in a mask's component stack.
//
// 📝 `Atlas`/`AtlasView` stay null for every category but `paint`, and even a paint component allocates
//    nothing until its first dab — the same lazy contract the channel atlases keep.
export function CreateMaskComponent(Category, Name)
{
    const Descriptor = MASK_COMPONENT_CATEGORY[Category] ? Category : "fill";
    return {
        Token:     NextComponentToken(),
        Category:  Descriptor,
        Name:      Name ?? MASK_COMPONENT_CATEGORY[Descriptor].Label,
        Opacity:   100,                                        // [%]
        Params:    { ...MASK_COMPONENT_CATEGORY[Descriptor].Defaults },
        Atlas:     null,
        AtlasView: null
    };
}

// The mask a layer carries. Disabled until the user adds one, exactly as Studio's makeMask does.
export function CreateLayerMask(Override)
{
    return Object.assign({
        Enabled:    false,
        Fill:       "white",        // the base the component stack is evaluated over
        Invert:     false,
        Opacity:    100,            // [%] how strongly the mask applies, not how bright it is
        Components: [],
        // Which component brush strokes land in. Null means "strokes go to the layer's channels".
        FocusToken: null
    }, Override ?? {});
}

export const MaskFillValue = (Mask) => (Mask.Fill === "black" ? 0.0 : 1.0);

// The paint component a stroke should land in, or null when the stroke belongs to the layer's channels.
//
// 🔴 Resolved from BOTH the mask being enabled and the focused component actually being paintable. A
//    focus token left pointing at a Levels component after the Paint one was dropped would otherwise
//    redirect every stroke into a component that has no storage — dabs would vanish with no refusal.
export function ResolveMaskPaintTarget(Layer)
{
    const Mask = Layer?.Mask;
    if (!Mask || !Mask.Enabled || !Mask.FocusToken) { return null; }

    const Component = Mask.Components.find((C) => C.Token === Mask.FocusToken);
    if (!Component) { return null; }

    return MASK_COMPONENT_CATEGORY[Component.Category]?.Paintable ? Component : null;
}

export const MaskComponentSummary = (Mask) =>
    `${Mask.Fill === "white" ? "White" : "Black"} fill` +
    `${Mask.Invert ? " · inverted" : ""} · ${Mask.Components.length} comp`;

//------------------------------------------------------------------------------------------------------------------------
//                                                      SHADER SOURCE
//------------------------------------------------------------------------------------------------------------------------

// 🔴 One step of the component stack per pass, ping-ponging between the layer's mask atlas and a shared
//    scratch. A single pass looping over the stack inside the shader would need a bind group whose size
//    depends on how many paint components exist, which means rebuilding the pipeline every time one is
//    added — and Levels is defined as a remap OF THE ACCUMULATED RESULT, which a single pass cannot see.
const MaskStepShaderSource = `
struct MaskStepUniform
{
    // x = category index (0 fill, 1 paint, 2 generator, 3 levels), y = component opacity [0,1],
    // z = fill value / generator amount, w = generator seed
    Control : vec4f,
    // x = generator scale, y = generator contrast, z = levels low, w = levels high
    Shape   : vec4f,
};

@group(0) @binding(0) var<uniform> Step : MaskStepUniform;
@group(0) @binding(1) var          PriorStore : texture_2d<f32>;
@group(0) @binding(2) var          MaskSampler : sampler;

// 🔴 A paint component's own atlas, or a 1x1 transparent stand-in for every other category. The binding
//    cannot simply be omitted — a bind group must satisfy its layout in full — and a stand-in reading as
//    transparent contributes nothing, which is exactly right for a step that is not a paint step.
@group(0) @binding(3) var          PaintStore : texture_2d<f32>;

struct MaskVarying
{
    @builtin(position) ClipPosition : vec4f,
    @location(0)       Coordinate   : vec2f,
};

@vertex
fn MaskStepVertex(@builtin(vertex_index) Index : u32) -> MaskVarying
{
    // One oversized triangle over the whole atlas, matching the compositor's idiom.
    var Corners = array<vec2f, 3>(vec2f(-1.0, -5.0), vec2f(-1.0, 1.0), vec2f(5.0, 1.0));
    let Corner  = Corners[Index];

    var Out : MaskVarying;
    Out.ClipPosition = vec4f(Corner, 0.0, 1.0);
    // Clip y runs up, texture v runs down.
    Out.Coordinate   = vec2f((Corner.x + 1.0) * 0.5, (1.0 - Corner.y) * 0.5);
    return Out;
}

fn Hash(Point : vec2f) -> f32
{
    let Wrapped = fract(Point * vec2f(0.3183099, 0.3678794) + vec2f(0.1, 0.1));
    let Scaled  = Wrapped * 50.0;
    return fract(Scaled.x * Scaled.y * (Scaled.x + Scaled.y));
}

fn ValueNoise(Point : vec2f) -> f32
{
    let Cell     = floor(Point);
    let Fraction = fract(Point);

    // Quintic smoothstep — a linear blend leaves grid creases whose DERIVATIVE is discontinuous, and a
    // mask feeds the compositor's weight, so those creases show as hard edges in the blend.
    let Weight = Fraction * Fraction * Fraction * (Fraction * (Fraction * 6.0 - 15.0) + 10.0);

    let A = Hash(Cell + vec2f(0.0, 0.0));
    let B = Hash(Cell + vec2f(1.0, 0.0));
    let C = Hash(Cell + vec2f(0.0, 1.0));
    let D = Hash(Cell + vec2f(1.0, 1.0));

    return mix(mix(A, B, Weight.x), mix(C, D, Weight.x), Weight.y);
}

fn Fbm(Point : vec2f) -> f32
{
    var Sum       = 0.0;
    var Amplitude = 0.5;
    var Frequency = Point;

    for (var Octave = 0; Octave < 5; Octave += 1)
    {
        Sum       += Amplitude * ValueNoise(Frequency);
        Frequency *= 2.02;
        Amplitude *= 0.5;
    }
    return Sum;
}

@fragment
fn MaskStepFragment(In : MaskVarying) -> @location(0) vec4f
{
    // 🔴 The accumulated mask is read from RED, not from luminance. Every write below stores the same
    //    scalar in all three components so any of them would do, but fixing on one keeps the value exact
    //    under an 8-bit round trip — averaging three quantised copies of the same number drifts.
    let Prior = textureSampleLevel(PriorStore, MaskSampler, In.Coordinate, 0.0).r;

    let Which   = Step.Control.x;
    let Opacity = Step.Control.y;

    var Next = Prior;

    if (Which < 0.5)
    {
        // Fill — a flat region across the whole atlas.
        Next = clamp(Step.Control.z, 0.0, 1.0);
    }
    else if (Which < 1.5)
    {
        // Paint — the component's own painted atlas, weighted by where it was actually painted. Coverage
        // is what makes an unpainted texel leave the accumulated mask alone instead of clearing it.
        let Painted = textureSampleLevel(PaintStore, MaskSampler, In.Coordinate, 0.0);
        Next = mix(Prior, Painted.r, Painted.a);
    }
    else if (Which < 2.5)
    {
        // Generator — the same value-noise field the layer generators use, remapped to a mask scalar.
        let Seed      = Step.Control.w;
        let Amount    = clamp(Step.Control.z, 0.0, 1.0);
        let Offset    = vec2f(Hash(vec2f(Seed, 11.0)), Hash(vec2f(Seed, 23.0))) * 64.0;
        // Scale reads as FEATURE SIZE, so frequency is its reciprocal: dragging right enlarges features.
        let Frequency = mix(48.0, 3.0, clamp(Step.Shape.x, 0.0, 1.0));
        var Pattern   = Fbm(In.Coordinate * Frequency + Offset);

        // Contrast pivots about the midpoint so raising it does not also brighten the field.
        let Steepness = mix(1.0, 12.0, clamp(Step.Shape.y, 0.0, 1.0));
        Pattern = clamp((Pattern - 0.5) * Steepness + 0.5, 0.0, 1.0);

        Next = mix(Prior, Pattern, Amount);
    }
    else
    {
        // Levels — remap the accumulated result, which is why this has to be its own pass.
        //
        // 🔴 The span is guarded rather than trusted. Low and High are independent sliders, so a user can
        //    drag High below Low; an unguarded divide then yields a negative or infinite scale and the
        //    whole mask flips to a hard black-or-white step with no way to read what went wrong.
        let Low  = Step.Shape.z;
        let High = Step.Shape.w;
        let Span = max(High - Low, 1e-4);
        Next = clamp((Prior - Low) / Span, 0.0, 1.0);
    }

    let Resolved = mix(Prior, Next, clamp(Opacity, 0.0, 1.0));

    // 🔴 Written OPAQUE and greyscale. The mask carries no coverage of its own — every texel of it has a
    //    defined value — so alpha here is a constant 1 and must not be confused with the layer coverage
    //    the compositor reads from the channel atlases.
    return vec4f(vec3f(Resolved), 1.0);
}`;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Evaluates a layer's mask component stack into the layer's mask atlas.
//
// 💡 The stack is a chain, not a set: Levels remaps whatever the components beneath it produced, so the
//    result of step N is the input of step N+1. That is the whole reason this is a sequence of passes
//    with a ping-pong rather than one shader reading every component at once.
export class MaskSequence
{
    constructor(Device, Extent)
    {
        this.Device = Device;
        this.Extent = Extent;

        const ShaderModule = Device.createShaderModule({
            label: "MaskSequence",
            code:  MaskStepShaderSource
        });
        this.ShaderModule = ShaderModule;

        this.Uniform = Device.createBuffer({
            label: "MaskStepUniform",
            size:  32,                                  // 2 vec4f
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
        });

        this.Sampler = Device.createSampler({
            label:        "MaskSampler",
            magFilter:    "nearest",
            minFilter:    "nearest",
            addressModeU: "clamp-to-edge",
            addressModeV: "clamp-to-edge"
        });

        // 🔴 ONE scratch shared by every layer, not one per layer. Evaluation is serial and each run
        //    overwrites the scratch from its first pass onward, so there is no state to carry between
        //    layers — and a per-layer scratch would double the mask cost of the whole stack.
        this.Scratch = Device.createTexture({
            label:  "MaskSequenceScratch",
            size:   [Extent, Extent],
            format: MaskAtlasFormat,
            usage:  GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING
        });

        // The stand-in bound at the paint slot for every non-paint step. Transparent, so the shader's
        // coverage-weighted mix leaves the accumulated mask untouched even if it were sampled.
        this.Placeholder = Device.createTexture({
            label:  "MaskPaintPlaceholder",
            size:   [1, 1],
            format: MaskAtlasFormat,
            usage:  GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING
        });
        const Seed = Device.createCommandEncoder({ label: "MaskPlaceholderClear" });
        Seed.beginRenderPass({
            colorAttachments: [{ view: this.Placeholder.createView(),
                                 clearValue: { r: 0, g: 0, b: 0, a: 0 },
                                 loadOp: "clear", storeOp: "store" }]
        }).end();
        Device.queue.submit([Seed.finish()]);
        this.PlaceholderView = this.Placeholder.createView();

        this.BindLayout = Device.createBindGroupLayout({
            label: "MaskStepBindLayout",
            entries: [
                { binding: 0, visibility: GPUShaderStage.FRAGMENT, buffer:  { type: "uniform" } },
                { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
                { binding: 2, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "non-filtering" } },
                { binding: 3, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } }
            ]
        });

        this.Pipeline = Device.createRenderPipeline({
            label:  "MaskSequence",
            layout: Device.createPipelineLayout({ bindGroupLayouts: [this.BindLayout] }),
            vertex:   { module: ShaderModule, entryPoint: "MaskStepVertex" },
            fragment: { module: ShaderModule, entryPoint: "MaskStepFragment",
                        // No blend state — the shader returns the finished value for the whole texel.
                        targets: [{ format: MaskAtlasFormat }] },
            primitive: { topology: "triangle-list", cullMode: "none" }
        });
    }

    // Re-evaluate one layer's mask. Returns false when the layer carries no enabled mask.
    //
    // 🔴 Every step is its own encoder and its own submit, because the uniform is rewritten per step and
    //    writeBuffer is ordered against SUBMITS, not against pass encoding. Batching the steps into one
    //    encoder would run all of them against whichever uniform contents were written last, so a four
    //    component stack would evaluate as four copies of its top component.
    Evaluate(Layer)
    {
        const Mask = Layer?.Mask;
        if (!Mask || !Mask.Enabled) { return false; }

        const Device = this.Device;
        const Target = Layer.EnsureMaskAtlas();
        if (!Target) { return false; }

        const Steps = Mask.Components;

        // 🔴 The parity decides where the chain STARTS, so that the last write lands on the layer's own
        //    atlas. Always starting on the layer's texture leaves the answer in the scratch whenever the
        //    step count is odd — and the scratch is shared, so the next layer's evaluation overwrites it.
        //    That reads as "the mask works on some layers and not others", flipping as components are added.
        const StartOnLayer = (Steps.length % 2) === 0;
        let   Write = StartOnLayer ? Layer.MaskTexture : this.Scratch;
        let   Read  = StartOnLayer ? this.Scratch      : Layer.MaskTexture;

        // ---- base: the fill the component stack is evaluated over ------------------------------------
        const Base    = MaskFillValue(Mask);
        const Opening = Device.createCommandEncoder({ label: `MaskBase${Layer.Token}` });
        Opening.beginRenderPass({
            label: `MaskBase${Layer.Token}`,
            colorAttachments: [{
                view:       Write.createView(),
                clearValue: { r: Base, g: Base, b: Base, a: 1 },
                loadOp:     "clear",
                storeOp:    "store"
            }]
        }).end();
        Device.queue.submit([Opening.finish()]);

        let Swap = Read; Read = Write; Write = Swap;

        // ---- one pass per component ------------------------------------------------------------------
        for (const Component of Steps)
        {
            const Category = MASK_COMPONENT_CATEGORY[Component.Category] ? Component.Category : "fill";
            const Params   = Component.Params ?? {};

            const Staging = new Float32Array(8);
            Staging[0] = CategoryIndex[Category];
            Staging[1] = (Component.Opacity ?? 100) / 100;
            Staging[2] = Category === "generator" ? (Params.Amount ?? 1.0) : (Params.Value ?? 1.0);
            Staging[3] = Params.Seed  ?? 0;

            Staging[4] = Params.Scale    ?? 0.5;
            Staging[5] = Params.Contrast ?? 0.5;
            Staging[6] = Params.Low      ?? 0.0;
            Staging[7] = Params.High     ?? 1.0;

            Device.queue.writeBuffer(this.Uniform, 0, Staging);

            const Painted = (Category === "paint" && Component.AtlasView)
                ? Component.AtlasView
                : this.PlaceholderView;

            const Encoder = Device.createCommandEncoder({ label: `MaskStep${Component.Token}` });
            const Pass = Encoder.beginRenderPass({
                label: `MaskStep${Component.Token}`,
                colorAttachments: [{
                    // 📝 loadOp "clear" costs nothing here: the shader writes every texel of the target
                    //    unconditionally, so there is nothing in it worth loading.
                    view:       Write.createView(),
                    clearValue: { r: 0, g: 0, b: 0, a: 1 },
                    loadOp:     "clear",
                    storeOp:    "store"
                }]
            });

            Pass.setPipeline(this.Pipeline);
            Pass.setBindGroup(0, Device.createBindGroup({
                label:  `MaskStepGroup${Component.Token}`,
                layout: this.BindLayout,
                entries: [
                    { binding: 0, resource: { buffer: this.Uniform } },
                    { binding: 1, resource: Read.createView() },
                    { binding: 2, resource: this.Sampler },
                    { binding: 3, resource: Painted }
                ]
            }));
            Pass.draw(3);
            Pass.end();
            Device.queue.submit([Encoder.finish()]);

            Swap = Read; Read = Write; Write = Swap;
        }

        // The finished result is in `Read` after the final swap, and the parity above guaranteed that is
        // the layer's own texture. Refresh the view the compositor binds.
        Layer.MaskView = Layer.MaskTexture.createView();
        return true;
    }

    Release()
    {
        this.Scratch?.destroy();
        this.Placeholder?.destroy();
        this.Uniform?.destroy();
    }
}
