/*====================================================================================================================================
                                                      GENERATORPASS.JS
====================================================================================================================================*/
// 🧩 Procedural patterns evaluated over a layer's UV atlas: rust, noise and scratches in WGSL

import { CHANNEL_ATLASES, CHANNEL_SLOTS } from "./ChannelSet.js";
import { GENERATOR_RECIPES } from "./LayerKinds.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                      SHADER SOURCE
//------------------------------------------------------------------------------------------------------------------------

// 🔴 A full-atlas pass, NOT a mesh raster. The generator has to cover every texel of the atlas including
//    the gutters between UV islands: bilinear sampling at shade time reaches across island edges, so a
//    pattern that stopped exactly at the island boundary would show a seam of the clear colour around
//    every island. Drawing a full-screen triangle over the whole atlas costs nothing extra and removes
//    the whole class of seam artefact.
const GeneratorShaderSource = `
struct GeneratorUniform
{
    // x = scale, y = contrast, z = amount, w = seed
    Params      : vec4f,
    // rgb = low colour, a = which generator (0 rust, 1 noise, 2 scratches)
    Low         : vec4f,
    // rgb = high colour, a = unused
    High        : vec4f,
    // x = roughness low, y = roughness high, z = atlas key (0 Colour, 1 Material, 2 Emissive), w = unused
    Range       : vec4f,
    // xyz = write mask for the three components, w = unused
    WriteMask   : vec4f,
};

@group(0) @binding(0) var<uniform> Generator : GeneratorUniform;

@vertex
fn GeneratorVertex(@builtin(vertex_index) Index : u32) -> @builtin(position) vec4f
{
    // A single oversized triangle covering the whole target. Cheaper than a quad and needs no index
    // buffer or vertex storage at all.
    //
    // 📝 Vertices at ±5 rather than the classic ±3. The classic triangle is exactly inscribed — its
    //    hypotenuse passes precisely through the clip corner (1,-1) — so that corner lands on a triangle
    //    edge and the fill rule decides it. Whether any rasterizer here actually drops it was NOT the cause
    //    of the uncovered-corner symptom this pass once showed (that was the noise coverage threshold, see
    //    the Which guard below), so treat this as cheap insurance and not as a fix for a measured hole. The
    //    surplus area is clipped for free, so there is no reason to tighten it back.
    var Corner = array<vec2f, 3>(vec2f(-1.0, -5.0), vec2f(-1.0, 1.0), vec2f(5.0, 1.0));
    return vec4f(Corner[Index], 0.0, 1.0);
}

//------------------------------------------------------------------------------------------------------
//                                        VALUE NOISE + fBm
//------------------------------------------------------------------------------------------------------

// 📝 A hash-based value noise rather than a texture lookup, so the pattern is resolution-independent and
//    needs no upload. The constants are the usual large primes; they only have to be irrational-looking.
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

    // Quintic smoothstep. 🔴 A linear interpolation here would leave visible grid creases along every
    //    cell boundary, because the DERIVATIVE is discontinuous even though the value is not — and the
    //    derived normal differentiates this, so the creases would light up as hard ridges.
    let Weight = Fraction * Fraction * Fraction * (Fraction * (Fraction * 6.0 - 15.0) + 10.0);

    let A = Hash(Cell + vec2f(0.0, 0.0));
    let B = Hash(Cell + vec2f(1.0, 0.0));
    let C = Hash(Cell + vec2f(0.0, 1.0));
    let D = Hash(Cell + vec2f(1.0, 1.0));

    return mix(mix(A, B, Weight.x), mix(C, D, Weight.x), Weight.y);
}

// Fractal sum. Five octaves is enough for a surface pattern; more just adds sub-texel detail that
// aliases under minification.
fn Fbm(Point : vec2f) -> f32
{
    var Sum       = 0.0;
    var Amplitude = 0.5;
    var Frequency = Point;

    for (var Octave = 0; Octave < 5; Octave += 1)
    {
        Sum       += Amplitude * ValueNoise(Frequency);
        Frequency *= 2.02;      // 📝 Not exactly 2: an exact doubling lines the octaves' grids up and
        Amplitude *= 0.5;       //    the sum shows a faint but regular lattice.
    }
    return Sum;
}

// Anisotropic streaks for the scratch generator: the domain is squashed hard along one axis so the noise
// stretches into thin lines, then rotated so they run diagonally rather than along the UV axes.
fn Streaks(Point : vec2f, Seed : f32) -> f32
{
    let Angle    = 0.7 + Hash(vec2f(Seed, Seed * 1.7)) * 2.0;
    let Cosine   = cos(Angle);
    let Sine     = sin(Angle);
    let Rotated  = vec2f(Point.x * Cosine - Point.y * Sine, Point.x * Sine + Point.y * Cosine);

    // 🔴 The 40:1 squash is what makes these read as scratches rather than as blotches. Feeding an
    //    isotropic fBm here produces mottling that is indistinguishable from the rust generator.
    let Stretched = vec2f(Rotated.x * 40.0, Rotated.y * 0.6);

    let Lines = Fbm(Stretched);

    // 🔴 The fBm is REMAPPED onto [0,1] before the tail is taken, and that remap is load-bearing. Fbm sums
    //    amplitudes 0.5+0.25+...+0.03125, so its output centres near 0.48 and only reaches ~0.89 at the
    //    extreme — measured, not assumed. Applying pow(Lines, 6.0) directly to that field left every texel
    //    near 0.01, the caller's contrast pivot about 0.5 then drove it to zero, and the caller's 0.35
    //    threshold rejected what remained: exactly 1 texel in 262144 survived, so the scratch generator
    //    produced an entirely empty layer. Stretching the observed range to [0,1] first is what puts the
    //    tail where the threshold can actually see it.
    let Spanned = clamp((Lines - 0.30) / 0.55, 0.0, 1.0);

    // Only the tail becomes a scratch, so the surface stays mostly clean and the grooves read as discrete
    // cuts instead of a texture. 🔴 Squared, not to the sixth: the remap above already concentrates the
    // tail, and re-crushing it by six re-creates the empty-layer bug in a subtler form.
    return Spanned * Spanned;
}

@fragment
fn GeneratorFragment(@builtin(position) Fragment : vec4f) -> @location(0) vec4f
{
    let Scale    = Generator.Params.x;
    let Contrast = Generator.Params.y;
    let Amount   = Generator.Params.z;
    let Seed     = Generator.Params.w;
    let Which    = Generator.Low.a;
    let AtlasKey = Generator.Range.z;

    // Atlas texel -> [0..1] UV. The pass covers the whole target, so the fragment position IS the texel.
    let Coordinate = Fragment.xy / max(Generator.Range.w, 1.0);

    // 🔴 The seed OFFSETS the domain, it does not scale it. Multiplying by the seed would change the
    //    pattern's frequency as well as its phase, so raising the seed would zoom the pattern rather than
    //    re-rolling it — and at high seeds it would alias into noise.
    let Offset = vec2f(Hash(vec2f(Seed, 11.0)), Hash(vec2f(Seed, 23.0))) * 64.0;

    // Scale is inverted so the slider reads as "feature size": dragging right makes features BIGGER,
    // which is what "scale" implies to a user. Frequency is the reciprocal.
    let Frequency = mix(48.0, 3.0, clamp(Scale, 0.0, 1.0));
    let Domain    = Coordinate * Frequency + Offset;

    var Pattern = 0.0;
    if (Which < 0.5)       { Pattern = Fbm(Domain); }                    // rust
    else if (Which < 1.5)  { Pattern = Fbm(Domain); }                    // noise
    else                   { Pattern = Streaks(Domain, Seed); }          // scratches

    // Contrast pivots about the midpoint so raising it does not also brighten the whole field.
    let Steepness = mix(1.0, 12.0, clamp(Contrast, 0.0, 1.0));
    Pattern = clamp((Pattern - 0.5) * Steepness + 0.5, 0.0, 1.0);

    // 🔴 Amount drives COVERAGE (alpha), not the pattern's strength. Fading the colour toward the clear
    //    value instead would leave the layer fully opaque and hide everything beneath it — a rust layer
    //    at Amount 0.1 would still completely cover the metal, just in a paler brown.
    var Coverage = Pattern * Amount;

    // Rust and scratches are patchy by nature: below the threshold there is simply no deposit, which is
    // what leaves clean metal showing between the patches. Plain noise covers everything uniformly.
    //
    // 🔴 Rust is 0 and scratches is 2, so the two patchy generators sit either side of noise at 1 and CANNOT
    //    be selected by a single comparison. A plain "Which > 0.5" was the obvious-looking guard and it
    //    silently included noise: measured, noise came back binarised — alpha exactly 1.0 or 0.0 with nothing
    //    (NOTE: no backticks anywhere in this string — the whole shader is a JS template literal, so one
    //    stray backtick in a comment ends it and the WGSL after it is parsed as JavaScript.)
    //    between, because the contrast pivot pushes survivors to saturation and the threshold zeroes the
    //    rest. That read as a rasterization hole at the atlas edge (one corner sampling as alpha 0) and sent
    //    an earlier investigation after the vertex triangle, which was never the cause. Excluding the middle
    //    index by DISTANCE from it is what makes the guard say what the comment above always claimed.
    if (abs(Which - 1.0) > 0.5) { Coverage = select(0.0, Coverage, Pattern > 0.35); }

    var Component = vec3f(0.0);

    if (AtlasKey < 0.5)
    {
        // Colour atlas: interpolate the recipe's two-stop palette by the pattern.
        Component = mix(Generator.Low.rgb, Generator.High.rgb, Pattern);
    }
    else if (AtlasKey < 1.5)
    {
        // Material atlas: r = metallic, g = roughness, b = height.
        //
        // 🔴 Rust DESTROYS metallic — corroded iron is an oxide, a dielectric. Leaving metallic at the
        //    value underneath would keep the rust patches shading as polished metal, which is the single
        //    most obvious way to make rust look like brown paint.
        let Roughness = mix(Generator.Range.x, Generator.Range.y, Pattern);

        // Scratches cut IN (below 0.5), rust builds UP. Height is signed about 0.5.
        var Height = 0.5;
        if (Which > 1.5) { Height = 0.5 - Pattern * 0.35; }
        else             { Height = 0.5 + Pattern * 0.12; }

        Component = vec3f(0.0, Roughness, Height);
    }
    else
    {
        // Emissive: no generator drives it, so nothing is deposited.
        Component = vec3f(0.0);
        Coverage  = 0.0;
    }

    // Components this generator does not drive keep coverage but contribute nothing, so the compositor's
    // per-component blend leaves them at whatever the layers beneath hold.
    let Masked = Component * Generator.WriteMask.xyz;

    return vec4f(Masked, Coverage);
}`;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

const GeneratorUniformFloatCount = 20;      // 5 vec4f
const GeneratorIndex = { rust: 0, noise: 1, scratches: 2 };

export class GeneratorPass
{
    constructor(Device)
    {
        this.Device = Device;

        const ShaderModule = Device.createShaderModule({
            label: "GeneratorPass",
            code:  GeneratorShaderSource
        });

        this.Uniform = Device.createBuffer({
            label: "GeneratorUniform",
            size:  GeneratorUniformFloatCount * 4,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
        });

        this.BindLayout = Device.createBindGroupLayout({
            label: "GeneratorBindLayout",
            entries: [{ binding: 0, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } }]
        });

        this.BindGroup = Device.createBindGroup({
            label:  "GeneratorBindGroup",
            layout: this.BindLayout,
            entries: [{ binding: 0, resource: { buffer: this.Uniform } }]
        });

        this.Pipeline = Device.createRenderPipeline({
            label:  "GeneratorPass",
            layout: Device.createPipelineLayout({ bindGroupLayouts: [this.BindLayout] }),
            vertex:   { module: ShaderModule, entryPoint: "GeneratorVertex" },
            fragment: { module: ShaderModule, entryPoint: "GeneratorFragment",
                        targets: [{ format: "rgba8unorm" }] },
            primitive: { topology: "triangle-list" }
        });
    }

    // Evaluate a generator into every atlas of one layer.
    //
    // 🔴 Each atlas is a SEPARATE pass with its own uniform write, because the shader branches on which
    //    atlas it is filling and the write mask differs per atlas. Encoding all three against one uniform
    //    buffer would make every pass see whichever value was written last — the queue does not snapshot
    //    a uniform per pass, so the Colour atlas would end up filled with Material data.
    Run(Layer, Generator, Params, Extent)
    {
        const Recipe = GENERATOR_RECIPES[Generator];
        if (!Recipe) { return false; }

        const Which = GeneratorIndex[Generator] ?? 0;

        for (const Descriptor of CHANNEL_ATLASES)
        {
            // Which of this atlas's components the generator actually drives.
            const Mask = [0, 0, 0];
            let   Any  = false;

            for (const Key of Descriptor.Channels)
            {
                if (!Recipe.Drives.includes(Key) || !Layer.Enabled.has(Key)) { continue; }
                const Slot = CHANNEL_SLOTS[Key];
                if (Slot.Kind === "colour") { Mask[0] = 1; Mask[1] = 1; Mask[2] = 1; }
                else                        { Mask[Slot.Component] = 1; }
                Any = true;
            }

            if (!Any) { continue; }

            const View = Layer.EnsureAtlas
                ? Layer.EnsureAtlas(Descriptor.Key)
                : Layer.AtlasView[Descriptor.Key];
            if (!View) { continue; }

            const AtlasKey = Descriptor.Key === "Colour" ? 0 : (Descriptor.Key === "Material" ? 1 : 2);

            const Staging = new Float32Array(GeneratorUniformFloatCount);
            Staging[0]  = Params.Scale    ?? Recipe.Params.Scale;
            Staging[1]  = Params.Contrast ?? Recipe.Params.Contrast;
            Staging[2]  = Params.Amount   ?? Recipe.Params.Amount;
            Staging[3]  = Params.Seed     ?? Recipe.Params.Seed;

            Staging[4]  = Recipe.Palette.Low[0];
            Staging[5]  = Recipe.Palette.Low[1];
            Staging[6]  = Recipe.Palette.Low[2];
            Staging[7]  = Which;

            Staging[8]  = Recipe.Palette.High[0];
            Staging[9]  = Recipe.Palette.High[1];
            Staging[10] = Recipe.Palette.High[2];
            Staging[11] = 0;

            Staging[12] = Recipe.Palette.RoughLow;
            Staging[13] = Recipe.Palette.RoughHigh;
            Staging[14] = AtlasKey;
            Staging[15] = Extent;

            Staging[16] = Mask[0];
            Staging[17] = Mask[1];
            Staging[18] = Mask[2];
            Staging[19] = 0;

            this.Device.queue.writeBuffer(this.Uniform, 0, Staging);

            // 🔴 One encoder + submit PER atlas, for the same reason the uniform is rewritten per atlas:
            //    writeBuffer is ordered against submits, not against pass encoding. Batching the three
            //    passes into a single encoder would run them all against the final uniform contents.
            const Encoder = this.Device.createCommandEncoder({ label: `Generator${Descriptor.Key}` });
            const Pass = Encoder.beginRenderPass({
                label: `Generator${Descriptor.Key}Pass`,
                colorAttachments: [{ view: View, loadOp: "clear", storeOp: "store",
                                     clearValue: { r: 0, g: 0, b: 0, a: 0 } }]
            });
            Pass.setPipeline(this.Pipeline);
            Pass.setBindGroup(0, this.BindGroup);
            Pass.draw(3);
            Pass.end();
            this.Device.queue.submit([Encoder.finish()]);
        }

        return true;
    }
}
