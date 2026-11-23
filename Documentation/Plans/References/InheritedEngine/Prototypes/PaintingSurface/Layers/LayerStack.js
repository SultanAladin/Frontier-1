/*====================================================================================================================================
                                                       LAYERSTACK.JS
====================================================================================================================================*/
// 🧩 The ordered stack of paint layers: each owns three channel atlases, a blend mode, an opacity and a mask

import { CHANNEL_ATLASES, CHANNEL_SLOTS, CHANNEL_ORDER, ChannelAtlasFormat,
         ResolveAtlasWrite } from "./ChannelSet.js";
import { LAYER_KINDS, IsPaintable, DefaultChannelModes, DefaultChannels,
         MATERIAL_PRESETS, GENERATOR_RECIPES } from "./LayerKinds.js";
import { CreateLayerMask, MaskAtlasFormat, MaskFillValue } from "./LayerMask.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// Ported verbatim from TexturePaintInspector so the UI and the engine agree on the vocabulary.
export const CLASSIFICATION_LABEL = {
    material:  "Material",
    generator: "Generator",
    brushwork: "Brushwork",
    flood:     "Flood"
};

export const CLASSIFICATION_ORDER = ["material", "generator", "brushwork", "flood"];

export const CLASSIFICATION_TINT = {
    material:  "#5b8cff",
    generator: "#c98b3a",
    brushwork: "#4fb286",
    flood:     "#b45fd0"
};

// 🔴 The per-layer IDENTITY palette, distinct from CLASSIFICATION_TINT above. A classification tint answers
//    "what kind of layer is this" and is therefore SHARED by every layer of that kind — which is exactly why
//    it cannot serve as the rail's per-layer marker: a stack of four material layers drew four identical blue
//    tags and the marker carried no information at all.
// 🔴 Hues are ordered so CONSECUTIVE entries contrast: the list alternates around the wheel rather than walking
//    it, because the assignment below hands adjacent layers adjacent entries and two neighbouring tags that
//    differ by 20° of hue read as the same colour in a 4px-wide swatch.
export const LAYER_COLOUR_PALETTE = [
    "#4fb286",   // teal
    "#e0a03c",   // amber
    "#7c6cf0",   // violet
    "#e0685f",   // coral
    "#3fa9d4",   // cyan
    "#c76fb8",   // orchid
    "#9dbd4f",   // olive
    "#d9784a",   // rust
    "#5b8cff",   // blue
    "#57c07a",   // green
    "#b0763a",   // bronze
    "#8d8fa8"    // slate
];

// 🔴 The order here IS the shader's enum. BlendShaderIndex returns the position in this array and the
//    composite shader switches on that integer, so reordering this list silently remaps every layer's
//    blend mode in every existing document.
export const BLEND_MODES = ["Normal", "Multiply", "Screen", "Overlay", "Add", "Darken", "Linear Dodge"];

export const RESOLUTIONS = { "512": 512, "1K": 1024, "2K": 2048, "4K": 4096, "8K": 8192 };

// 📝 Layers are capped because each costs three full atlases. At 1024² that is 12 MiB per layer, so
//    twelve layers is 144 MiB of paint — already past what a modest integrated adapter will hand out
//    without complaint. The cap is a guard rail with a clear message, not a silent allocation failure.
export const LayerCapacity = 12;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

let TokenSequence = 0;
const NextToken = () => `L${(TokenSequence += 1).toString().padStart(3, "0")}`;

export function BlendShaderIndex(Mode)
{
    const Index = BLEND_MODES.indexOf(Mode);
    return Index < 0 ? 0 : Index;
}

// Every channel starts at its documented default, whether or not the layer paints it.
function DefaultChannelValues()
{
    const Values = {};
    for (const Key of CHANNEL_ORDER)
    {
        const Slot = CHANNEL_SLOTS[Key];
        if (Slot.Kind === "derived") { continue; }
        Values[Key] = Array.isArray(Slot.Default) ? Slot.Default.slice() : Slot.Default;
    }
    return Values;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       ONE LAYER
//------------------------------------------------------------------------------------------------------------------------

export class PaintLayer
{
    constructor(Device, Extent, Name, Kind, Enabled, Options)
    {
        const Setting = Options ?? {};

        this.Token   = NextToken();
        this.Name    = Name;
        this.Kind    = LAYER_KINDS[Kind] ? Kind : "paint";
        this.Shown   = true;
        this.Blend   = "Normal";
        this.Opacity = 100;              // [%]
        this.Extent  = Extent;
        this.Device  = Device;

        // 📝 Kept as an alias so anything still reading `Classification` keeps working while the UI and
        //    the serializer move over to `Kind`. Same string, one name for it going forward.
        this.Classification = this.Kind;

        // The layer's own identity colour — the rail's tag, independent of Kind.
        //
        // 🔴 Indexed off the TOKEN's ordinal, not off the layer's position in the stack. A position-derived
        //    colour would re-shuffle every tag on a reorder or a delete, so the marker the user just learned to
        //    recognise would jump to a different layer — the one thing an identity colour must never do. The
        //    token ordinal is issued once and never reused, so a layer keeps its colour for life.
        // 📝 Explicitly overridable via Setting.Colour so a caller (or a restored document) can pin one.
        this.Colour = Setting.Colour
            ?? LAYER_COLOUR_PALETTE[(TokenSequence - 1) % LAYER_COLOUR_PALETTE.length];

        // Which material preset or generator recipe this layer is an instance of, if any.
        this.Preset    = Setting.Preset    ?? null;
        this.Generator = Setting.Generator ?? null;

        // Which of the six the brush deposits into on this layer. A Set, because the inspector toggles
        // membership and every consumer asks "is this channel on" rather than iterating a list.
        this.Enabled = new Set(Enabled ?? DefaultChannels(this.Kind, this.Preset, this.Generator));

        // The authored value per channel — what a dab deposits where the brush ink does not apply.
        this.Values = DefaultChannelValues();

        // Per-channel source: Value | Texture | Generator.
        this.Modes = DefaultChannelModes(this.Kind, this.Generator);

        // Generator parameters, seeded from the recipe so the sliders open where the recipe intends.
        this.Params = { ...(GENERATOR_RECIPES[this.Generator]?.Params ?? {}) };

        // A material preset overwrites the channel defaults with its own authored values.
        const Preset = MATERIAL_PRESETS[this.Preset];
        if (Preset) { for (const [Key, Value] of Object.entries(Preset.Values))
                      { this.Values[Key] = Array.isArray(Value) ? Value.slice() : Value; } }

        // ---- GPU storage -----------------------------------------------------------------------------
        // 🔴 Allocation is LAZY — nothing is created here. Each atlas is 4 MiB at 1024², so eagerly
        //    allocating all three per layer costs 12 MiB for a layer that may never be written; twelve
        //    such layers is 144 MiB of paint the user never asked for. A channel's storage appears on its
        //    first actual write (stroke, flood or generator pass), and an atlas that was never written
        //    stays null and is skipped by the compositor — which is also exactly the right composite
        //    result, since an unwritten layer has zero coverage and must not affect anything beneath it.
        this.Atlas     = {};
        this.AtlasView = {};

        // ---- the layer mask --------------------------------------------------------------------------
        // 🔴 A mask is NOT a fourth channel. It gates how strongly this whole layer composites over what
        //    is beneath it, across every channel at once, so it lives beside the channel atlases rather
        //    than inside them. It starts disabled and unallocated: a layer with no mask must cost nothing.
        this.Mask        = CreateLayerMask();
        this.MaskTexture = null;
        this.MaskView    = null;
    }

    // Allocate the layer's resolved mask atlas on demand, cleared to the mask's own fill.
    //
    // 🔴 Cleared to the FILL value, not to zero. A white-fill mask means "this layer applies everywhere",
    //    and a zeroed atlas means the exact opposite — so a mask that was enabled but not yet evaluated
    //    would make the whole layer vanish, which reads as "adding a mask deletes the layer".
    EnsureMaskAtlas()
    {
        if (this.MaskView) { return this.MaskView; }

        this.MaskTexture = this.Device.createTexture({
            label:  `LayerMask${this.Token}`,
            size:   [this.Extent, this.Extent],
            format: MaskAtlasFormat,
            usage:  GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING |
                    GPUTextureUsage.COPY_SRC          | GPUTextureUsage.COPY_DST
        });
        this.MaskView = this.MaskTexture.createView();

        const Base    = MaskFillValue(this.Mask);
        const Encoder = this.Device.createCommandEncoder({ label: `LayerMaskClear${this.Token}` });
        Encoder.beginRenderPass({
            colorAttachments: [{ view: this.MaskView,
                                 clearValue: { r: Base, g: Base, b: Base, a: 1 },
                                 loadOp: "clear", storeOp: "store" }]
        }).end();
        this.Device.queue.submit([Encoder.finish()]);

        return this.MaskView;
    }

    // Allocate one mask COMPONENT's painted atlas on demand, cleared to transparent black.
    //
    // 🔴 Transparent, unlike the resolved mask above. A paint component's alpha is where it was actually
    //    stroked, and the sequence shader mixes by that coverage — clearing it opaque would make every
    //    unpainted texel of the component read as a hard black paint-out over everything beneath it.
    EnsureMaskComponentAtlas(Component)
    {
        if (!Component) { return null; }
        if (Component.AtlasView) { return Component.AtlasView; }

        Component.Atlas = this.Device.createTexture({
            label:  `LayerMask${this.Token}${Component.Token}`,
            size:   [this.Extent, this.Extent],
            format: MaskAtlasFormat,
            usage:  GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING |
                    GPUTextureUsage.COPY_SRC          | GPUTextureUsage.COPY_DST
        });
        Component.AtlasView = Component.Atlas.createView();

        const Encoder = this.Device.createCommandEncoder({ label: `LayerMaskCompClear${Component.Token}` });
        Encoder.beginRenderPass({
            colorAttachments: [{ view: Component.AtlasView,
                                 clearValue: { r: 0, g: 0, b: 0, a: 0 },
                                 loadOp: "clear", storeOp: "store" }]
        }).end();
        this.Device.queue.submit([Encoder.finish()]);

        return Component.AtlasView;
    }

    // Is the mask both enabled and actually resolved into storage the compositor can bind?
    get Masked() { return Boolean(this.Mask?.Enabled && this.MaskView); }

    // Allocate one atlas on demand and clear it to its documented value. Returns the view.
    //
    // 🔴 The clear is NOT optional and "zero-filled" is not good enough. A fresh WebGPU texture is zeroed,
    //    but height lives at 0.5 (the undisplaced surface) — so an unclear material atlas reads as a
    //    maximum dent at every texel and the derived normal explodes along every island edge.
    EnsureAtlas(AtlasKey)
    {
        if (this.AtlasView[AtlasKey]) { return this.AtlasView[AtlasKey]; }

        const Descriptor = CHANNEL_ATLASES.find(A => A.Key === AtlasKey);
        if (!Descriptor) { return null; }

        // 🔴 RENDER_ATTACHMENT is what lets the paint pass draw INTO the layer, TEXTURE_BINDING is what
        //    lets the composite pass read it back out, and COPY_SRC/COPY_DST carry snapshots for undo.
        //    Miss any one and the failure is a validation error at first use, not at creation.
        const Texture = this.Device.createTexture({
            label:  `Layer${this.Token}${AtlasKey}`,
            size:   [this.Extent, this.Extent],
            format: ChannelAtlasFormat,
            usage:  GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING |
                    GPUTextureUsage.COPY_SRC          | GPUTextureUsage.COPY_DST
        });

        this.Atlas[AtlasKey]     = Texture;
        this.AtlasView[AtlasKey] = Texture.createView();

        const [R, G, B, A] = Descriptor.Clear;
        const Encoder = this.Device.createCommandEncoder({ label: `LayerClear${this.Token}${AtlasKey}` });
        Encoder.beginRenderPass({
            colorAttachments: [{ view: this.AtlasView[AtlasKey],
                                 clearValue: { r: R, g: G, b: B, a: A },
                                 loadOp: "clear", storeOp: "store" }]
        }).end();
        this.Device.queue.submit([Encoder.finish()]);

        return this.AtlasView[AtlasKey];
    }

    // Has this layer ever been written? Drives the "allocated on first stroke" note in the UI.
    get Allocated() { return Object.keys(this.Atlas).length > 0; }

    get Paintable() { return IsPaintable(this.Kind); }

    // Wipe every channel atlas back to its documented clear value.
    //
    // 📝 Only ALLOCATED atlases are cleared. An unallocated one is already in the right state by
    //    definition — it has no storage, which the compositor reads as zero coverage, which is what a
    //    cleared layer means. Touching one here would allocate 4 MiB to write the value it already has.
    Clear(Device)
    {
        const Encoder = Device.createCommandEncoder({ label: `LayerClear${this.Token}` });

        for (const Descriptor of CHANNEL_ATLASES)
        {
            if (!this.AtlasView[Descriptor.Key]) { continue; }

            const [R, G, B, A] = Descriptor.Clear;
            Encoder.beginRenderPass({
                label: `LayerClear${this.Token}${Descriptor.Key}`,
                colorAttachments: [{
                    view:       this.AtlasView[Descriptor.Key],
                    clearValue: { r: R, g: G, b: B, a: A },
                    loadOp:     "clear",
                    storeOp:    "store"
                }]
            }).end();
        }

        Device.queue.submit([Encoder.finish()]);
    }

    // Fill every atlas this layer paints with its authored values at full coverage.
    //
    // 📝 A clear-to-value, not a draw. The paint pass rasterizes the mesh in UV space and so only reaches
    //    texels the model actually uses; a base material wants the whole atlas, including the gutters
    //    between UV islands. Filling the gutters is what stops bilinear sampling from dragging the clear
    //    colour in across every island edge.
    //
    // 🔴 Only the ENABLED channels are flooded, and unenabled components keep their clear value — the
    //    same contract the write mask enforces for a dab. A layer that paints roughness but not metallic
    //    must not flood metallic to 0 and call it authored; that is a different statement from "untouched".
    Flood(Device)
    {
        const Encoder = Device.createCommandEncoder({ label: `LayerFlood${this.Token}` });
        let   Any     = false;

        for (const Descriptor of CHANNEL_ATLASES)
        {
            const Write = ResolveAtlasWrite(Descriptor.Key, this.Enabled, this.Values, null);

            // Nothing enabled for this atlas: leave it transparent so the compositor passes the layers
            // beneath it through untouched.
            if (Write === null) { continue; }

            // A flood IS a write, so this is where the storage is earned.
            const View = this.EnsureAtlas(Descriptor.Key);
            if (!View) { continue; }

            const [R, G, B] = Descriptor.Clear;
            const Filled = [
                Write.Mask[0] ? Write.Value[0] : R,
                Write.Mask[1] ? Write.Value[1] : G,
                Write.Mask[2] ? Write.Value[2] : B
            ];

            Encoder.beginRenderPass({
                label: `LayerFlood${this.Token}${Descriptor.Key}`,
                colorAttachments: [{
                    view:       View,
                    clearValue: { r: Filled[0], g: Filled[1], b: Filled[2], a: 1.0 },
                    loadOp:     "clear",
                    storeOp:    "store"
                }]
            }).end();
            Any = true;
        }

        // 🔴 An encoder with no passes still has to be finished and submitted or it leaks, but submitting
        //    an empty command buffer is pointless work every time a non-flooding layer is created.
        if (Any) { Device.queue.submit([Encoder.finish()]); }
        else     { Encoder.finish(); }
    }

    Release()
    {
        // Only allocated atlases exist to destroy — a lazily-skipped one has no texture object.
        for (const Descriptor of CHANNEL_ATLASES) { this.Atlas[Descriptor.Key]?.destroy(); }
        this.Atlas     = {};
        this.AtlasView = {};

        // The mask's own atlas plus every paint component's, each of which is a full-size texture.
        this.MaskTexture?.destroy();
        this.MaskTexture = null;
        this.MaskView    = null;
        for (const Component of this.Mask?.Components ?? [])
        {
            Component.Atlas?.destroy();
            Component.Atlas     = null;
            Component.AtlasView = null;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE STACK
//------------------------------------------------------------------------------------------------------------------------

export class LayerStack
{
    // 🔴 Index 0 is the TOP of the stack, matching how the inspector lists layers and how every paint
    //    tool presents them. The composite pass therefore walks the array BACKWARD. Storing bottom-first
    //    instead would read more naturally in the compositor and would invert the stack in the UI, which
    //    is the more visible wrong.
    constructor(Device, Extent)
    {
        this.Device  = Device;
        this.Extent  = Extent;
        this.Layers  = [];
        this.FocusToken = null;
        this.Revision   = 0;        // Bumped on any change the compositor must react to.
    }

    get Count()        { return this.Layers.length; }
    get EnabledCount() { return this.Layers.filter(L => L.Shown).length; }

    Resolve(Token)     { return this.Layers.find(L => L.Token === Token) ?? null; }
    IndexOf(Token)     { return this.Layers.findIndex(L => L.Token === Token); }

    // The layer a stroke lands on. 🔴 Never falls back to "the top layer" when focus is unset — a
    //    stroke with no target must be refused, not silently redirected onto whatever sits on top.
    get Focus()        { return this.FocusToken ? this.Resolve(this.FocusToken) : null; }

    Touch() { this.Revision += 1; }

    Add(Name, Kind, Enabled, AtIndex, Options)
    {
        if (this.Layers.length >= LayerCapacity)
        {
            throw new Error(`Layer cap of ${LayerCapacity} reached — each layer costs up to three ${this.Extent}² atlases.`);
        }

        const Layer = new PaintLayer(this.Device, this.Extent, Name, Kind, Enabled, Options);
        const Where = AtIndex === undefined ? 0 : Math.max(0, Math.min(this.Layers.length, AtIndex));

        this.Layers.splice(Where, 0, Layer);
        this.FocusToken = Layer.Token;
        this.Touch();
        return Layer;
    }

    Remove(Token)
    {
        const Index = this.IndexOf(Token);
        if (Index < 0) { return false; }

        // 🔴 The bottom layer is the substrate every other layer composites over. Removing it leaves the
        //    stack with nothing opaque underneath and the resolve shows through to the clear colour, so
        //    it is refused rather than allowed to produce a confusing result.
        if (this.Layers.length <= 1) { return false; }

        this.Layers[Index].Release();
        this.Layers.splice(Index, 1);

        if (this.FocusToken === Token)
        {
            // Focus the layer that took its place, or the one above if it was the last.
            const Next = this.Layers[Math.min(Index, this.Layers.length - 1)];
            this.FocusToken = Next ? Next.Token : null;
        }

        this.Touch();
        return true;
    }

    // Move a layer by one position. Direction -1 raises it toward the top (index 0), +1 lowers it.
    Reorder(Token, Direction)
    {
        const Index  = this.IndexOf(Token);
        const Target = Index + Direction;
        if (Index < 0 || Target < 0 || Target >= this.Layers.length) { return false; }

        const [Moved] = this.Layers.splice(Index, 1);
        this.Layers.splice(Target, 0, Moved);
        this.Touch();
        return true;
    }

    Focus_Set(Token)
    {
        if (!this.Resolve(Token)) { return false; }
        this.FocusToken = Token;
        this.Touch();
        return true;
    }

    // Bottom-to-top, which is the order the compositor needs.
    *BottomUp()
    {
        for (let Ordinal = this.Layers.length - 1; Ordinal >= 0; Ordinal -= 1) { yield this.Layers[Ordinal]; }
    }

    Release()
    {
        for (const Layer of this.Layers) { Layer.Release(); }
        this.Layers = [];
        this.FocusToken = null;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SEEDED STACK
//------------------------------------------------------------------------------------------------------------------------

// The stack the prototype opens with: a blank white base, and one empty paint layer above it to receive
// the first stroke.
//
// 🔴 The document starts as a CLEAN WHITE SURFACE, not as a pre-dressed demo scene. A seeded car-paint
//    stack looks better in a screenshot but it is the wrong starting point for a painting tool: every
//    first stroke lands on top of somebody else's material, and the user cannot tell their own paint from
//    the seed. White also makes the brush colour honest — paint over grey and every colour reads dark.
export function SeedLayerStack(Device, Extent)
{
    const Stack = new LayerStack(Device, Extent);

    // 🔴 Added bottom-first because Add() inserts at index 0. Adding these in listed order would stand
    //    the stack on its head and the white base would cover the paint layer completely.
    //
    // 📝 The base is a FILL, not a paint layer: its content is its authored value across the whole
    //    surface, and it is flooded so it covers the UV gutters too. A base that stopped at the island
    //    edges would let the clear colour bleed in under bilinear sampling.
    const Base = Stack.Add("Base — White", "fill",
                           ["baseColour", "metallic", "roughness"]);
    Base.Values.baseColour = [1.0, 1.0, 1.0];
    Base.Values.metallic   = 0.0;
    Base.Values.roughness  = 0.55;      // a plain matte white, not a gloss
    Base.Flood(Device);

    // An empty paint layer on top, so the very first stroke has somewhere legal to land. It allocates no
    // atlases until that stroke actually happens.
    const Paint = Stack.Add("Paint 1", "paint", ["baseColour"]);

    Stack.Focus_Set(Paint.Token);
    return Stack;
}
