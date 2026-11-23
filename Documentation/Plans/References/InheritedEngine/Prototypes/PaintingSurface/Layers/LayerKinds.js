/*====================================================================================================================================
                                                       LAYERKINDS.JS
====================================================================================================================================*/
// 🧩 What a layer IS — paint / fill / material / generator — plus the material presets and generator recipes

import { CHANNEL_ORDER } from "./ChannelSet.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                       LAYER KINDS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 The kind is not a label, it is a CAPABILITY. Only a `paint` layer accepts brush strokes; the other
//    three are generated or flooded wholesale. This has to be enforced in the stroke path rather than
//    merely shown in the UI, because the failure is otherwise silent: a stroke aimed at a material layer
//    would be swallowed with no dab, no error and no clue, which reads as "painting randomly stops
//    working" the moment the user selects the wrong row.
export const LAYER_KINDS = {
    paint: {
        Label:    "Paint",
        Tint:     "#f97316",
        Paintable: true,
        // A paint layer starts EMPTY and waits for a stroke. Flooding it would defeat the point: the
        // whole surface would already be covered and there would be nothing to reveal by painting.
        Flooded:  false,
        Summary:  "Hand-painted. Accepts brush strokes."
    },
    fill: {
        Label:    "Fill",
        Tint:     "#3b82f6",
        // 📝 A fill is the user's own custom flat layer — the thing they create when they want a solid
        //    colour or a uniform roughness across the whole surface. It is not paintable; its content is
        //    entirely its authored channel values.
        Paintable: false,
        Flooded:  true,
        Summary:  "Uniform authored values across the whole surface."
    },
    material: {
        Label:    "Material",
        Tint:     "#8b5cf6",
        Paintable: false,
        Flooded:  true,
        Summary:  "A PBR preset flooded over the surface."
    },
    generator: {
        Label:    "Generator",
        Tint:     "#10b981",
        Paintable: false,
        // 🔴 NOT flooded. A generator's own pass writes every texel it wants, including its own coverage,
        //    so flooding first would lay a flat colour under the pattern and the generator's alpha would
        //    then be indistinguishable from full coverage — the mottling would vanish into a solid fill.
        Flooded:  false,
        Summary:  "Procedural pattern evaluated over the UV atlas."
    }
};

export const LAYER_KIND_ORDER = ["paint", "fill", "material", "generator"];

export const IsPaintable = (Kind) => Boolean(LAYER_KINDS[Kind]?.Paintable);
export const KindTint    = (Kind) => LAYER_KINDS[Kind]?.Tint  ?? "#8a8a8a";
export const KindLabel   = (Kind) => LAYER_KINDS[Kind]?.Label ?? String(Kind);

//------------------------------------------------------------------------------------------------------------------------
//                                                      CHANNEL MODES
//------------------------------------------------------------------------------------------------------------------------

// The per-channel source, mirroring TexturePaintPropertiesR2's Value / Texture / Generator segment.
//
// 📝 "Texture" means PAINTED BY HAND here, not loaded from disk. The engine's storage for a channel IS
//    the painted atlas, so a hand-painted channel and a texture-mapped channel are the same thing from
//    the compositor's point of view; there is no texture library to assign from.
export const CHANNEL_MODES = ["Value", "Texture", "Generator"];

export const CHANNEL_MODE_NOTE = {
    Value:     "One authored value across the whole layer.",
    Texture:   "Painted by hand. Storage is allocated on the first stroke.",
    Generator: "Driven by this layer's procedural pass."
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   MATERIAL PRESETS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 Values are LINEAR [0..1], not sRGB hex. The shader works in linear space and tone-maps at the end,
//    so pasting an 8-bit hex triple straight in here would wash every preset out by roughly a 2.2 power.
//
// 📝 Metallic is deliberately 0 or 1 and never in between. A partially metallic surface is not a real
//    material — the channel selects between two different BRDF interpretations, and mid values only make
//    sense as a blend across a boundary within one texel.
// 🔴 Every preset is authored in the FIVE channels this prototype actually stores — baseColour,
//    metallic, roughness, height, emission. The MaterialShelfDrawer library these are drawn from carries
//    forty (clearcoat, sheen, flake, weave, tow anisotropy, subsurface, grain, grunge…), and none of
//    those reach a shader input here: the raster reads three RGBA8 atlases and derives the normal from
//    height. Carrying the extra channels across as authored numbers would put a full wall of sliders in
//    the layer properties, every one of them inert — the same fault the mask pane's note calls out. So a
//    clearcoat is folded into the roughness it produces, and a weave into the height it displaces.
//
// 🔴 `Family` groups the shelf's category rail and `Note` is the hero card's one-line read. Both live on
//    the preset rather than in the shelf, because the shelf is a VIEW of this table: a preset added here
//    must appear in the browser without a second edit, and the earlier hardcoded family list made every
//    new category silently unreachable (the material rendered under "All" but no rail pill filtered to it).
export const MATERIAL_PRESETS = {
    plastic: {
        Label:  "Plastic — Red",
        Family: "Plastic",
        Note:   "Injection-moulded ABS, glossy.",
        Tint:   "#c0392b",
        Values: { baseColour: [0.52, 0.06, 0.04], metallic: 0.0, roughness: 0.34,
                  emission: [0, 0, 0], height: 0.5 },
        Channels: ["baseColour", "metallic", "roughness"]
    },
    polymerGrey: {
        Label:  "Plastic — Grey Satin",
        Family: "Plastic",
        Note:   "Neutral grey polymer, semi-gloss.",
        Tint:   "#4a4c50",
        Values: { baseColour: [0.26, 0.27, 0.29], metallic: 0.0, roughness: 0.44,
                  emission: [0, 0, 0], height: 0.5 },
        Channels: ["baseColour", "metallic", "roughness"]
    },
    rubber: {
        Label:  "Rubber — EPDM Black",
        Family: "Plastic",
        // 📝 A flat rubber is the useful bottom end of the roughness range: near-no highlight at all, so
        //    it reads as an unlit silhouette next to the glazes and is the honest test that the roughness
        //    channel reaches the shader.
        Note:   "Flat EPDM rubber, no highlight.",
        Tint:   "#16161a",
        Values: { baseColour: [0.03, 0.03, 0.035], metallic: 0.0, roughness: 0.93,
                  emission: [0, 0, 0], height: 0.5 },
        Channels: ["baseColour", "metallic", "roughness"]
    },

    metal: {
        Label:  "Metal — Brushed Steel",
        Family: "Metal",
        Note:   "Directional brushed steel, satin.",
        Tint:   "#95a5a6",
        // Brushed steel is a bright dielectric-looking grey in linear terms; a proper metal takes its
        // base colour as its reflectance, so this triple IS the specular colour.
        Values: { baseColour: [0.56, 0.57, 0.58], metallic: 1.0, roughness: 0.28,
                  emission: [0, 0, 0], height: 0.5 },
        Channels: ["baseColour", "metallic", "roughness"]
    },
    chrome: {
        Label:  "Metal — Chrome",
        Family: "Metal",
        Note:   "Mirror chrome, hard hotspot.",
        Tint:   "#d8dde2",
        // 🔴 0.04, not 0. The raster clamps roughness at a small floor before the GGX denominator, so a
        //    literal zero lands ON the clamp and every mirror preset resolves to the same highlight —
        //    which reads as "chrome and polished copper look identical".
        Values: { baseColour: [0.85, 0.87, 0.90], metallic: 1.0, roughness: 0.04,
                  emission: [0, 0, 0], height: 0.5 },
        Channels: ["baseColour", "metallic", "roughness"]
    },
    copper: {
        Label:  "Metal — Polished Copper",
        Family: "Metal",
        Note:   "Polished copper, warm reflection.",
        Tint:   "#d98a6a",
        Values: { baseColour: [0.95, 0.64, 0.54], metallic: 1.0, roughness: 0.16,
                  emission: [0, 0, 0], height: 0.5 },
        Channels: ["baseColour", "metallic", "roughness"]
    },
    gold: {
        Label:  "Metal — Gold",
        Family: "Metal",
        Note:   "Soft-polished gold.",
        Tint:   "#e0b23c",
        Values: { baseColour: [1.0, 0.77, 0.34], metallic: 1.0, roughness: 0.20,
                  emission: [0, 0, 0], height: 0.5 },
        Channels: ["baseColour", "metallic", "roughness"]
    },

    ceramic: {
        Label:  "Ceramic — Glazed White",
        Family: "Ceramic",
        Note:   "Glazed porcelain, wet highlight.",
        Tint:   "#ecf0f1",
        // A glaze is very smooth but NOT a mirror; 0.09 keeps a tight highlight without hitting the
        // roughness floor the shader clamps at.
        Values: { baseColour: [0.86, 0.85, 0.82], metallic: 0.0, roughness: 0.09,
                  emission: [0, 0, 0], height: 0.5 },
        Channels: ["baseColour", "metallic", "roughness"]
    },
    terracotta: {
        Label:  "Ceramic — Terracotta",
        Family: "Ceramic",
        Note:   "Unglazed earthenware, chalky.",
        Tint:   "#7a3320",
        Values: { baseColour: [0.48, 0.20, 0.12], metallic: 0.0, roughness: 0.78,
                  emission: [0, 0, 0], height: 0.5 },
        Channels: ["baseColour", "metallic", "roughness"]
    },

    carPaint: {
        Label:  "Coated — Car Paint Red",
        Family: "Coated",
        // 📝 The shelf's own Car Paint Red is a rough basecoat under a mirror lacquer. With no coat lobe
        //    to render, the LOOK of the pair is a single smooth layer, so the authored roughness is the
        //    coat's (0.06) rather than the basecoat's (0.42) — folding the coat in as the value it
        //    actually produces instead of exposing a slider that reaches nothing.
        Note:   "Basecoat red under lacquer.",
        Tint:   "#8c1410",
        Values: { baseColour: [0.55, 0.03, 0.03], metallic: 0.0, roughness: 0.06,
                  emission: [0, 0, 0], height: 0.5 },
        Channels: ["baseColour", "metallic", "roughness"]
    },
    pianoBlack: {
        Label:  "Coated — Piano Black",
        Family: "Coated",
        Note:   "Lacquered black, mirror coat.",
        Tint:   "#0b0b0d",
        Values: { baseColour: [0.02, 0.02, 0.02], metallic: 0.0, roughness: 0.03,
                  emission: [0, 0, 0], height: 0.5 },
        Channels: ["baseColour", "metallic", "roughness"]
    },
    carbon: {
        Label:  "Coated — Carbon Fibre",
        Family: "Coated",
        // 🔴 Metallic 0, not the shelf's 0.25. A part-metallic value is not a real material — the channel
        //    selects between two BRDF interpretations — and the resin over a carbon twill is a dielectric.
        //    The shelf's fraction stood in for a flake term this raster has no input for.
        Note:   "Twill weave under gloss resin.",
        Tint:   "#1b1c1f",
        Values: { baseColour: [0.03, 0.033, 0.038], metallic: 0.0, roughness: 0.22,
                  emission: [0, 0, 0], height: 0.5 },
        Channels: ["baseColour", "metallic", "roughness"]
    },

    clay: {
        Label:  "Neutral — Calibration Clay",
        Family: "Neutral",
        // 📝 The matte neutral every other preset is judged against. Kept in the shelf on purpose: a flat
        //    grey is what makes a lighting or normal-strength fault legible, because nothing in it can be
        //    mistaken for authored colour.
        Note:   "Matte neutral grey — calibration.",
        Tint:   "#575757",
        Values: { baseColour: [0.34, 0.34, 0.34], metallic: 0.0, roughness: 0.85,
                  emission: [0, 0, 0], height: 0.5 },
        Channels: ["baseColour", "metallic", "roughness"]
    },
    emissivePanel: {
        Label:  "Neutral — Emissive Panel",
        Family: "Neutral",
        // 🔴 The one preset that enables the emissive channel, so the shelf covers all five stored
        //    channels rather than three. Its base colour stays dark: an emissive surface that also
        //    reflects brightly reads as a lit white panel, and the emission is then unfalsifiable.
        Note:   "Self-lit signal panel, cool white.",
        Tint:   "#7fd2ff",
        Values: { baseColour: [0.05, 0.06, 0.07], metallic: 0.0, roughness: 0.55,
                  emission: [0.42, 0.68, 0.95], height: 0.5 },
        Channels: ["baseColour", "metallic", "roughness", "emission"]
    }
};

export const MATERIAL_ORDER = [
    "plastic", "polymerGrey", "rubber",
    "metal", "chrome", "copper", "gold",
    "ceramic", "terracotta",
    "carPaint", "pianoBlack", "carbon",
    "clay", "emissivePanel"
];

// 🔴 Derived from the preset table in first-appearance order, never written out by hand. A hardcoded
//    family list is what made every newly added category unreachable in the source shelf: the material
//    showed under "All" but no rail pill filtered to it and the per-family counts under-reported.
export const MATERIAL_FAMILIES = [
    ...new Set(MATERIAL_ORDER.map((Key) => MATERIAL_PRESETS[Key]?.Family ?? "Other"))
];

// Which channels a material preset may be edited through, in the order the properties pane lists them.
//
// 📝 Read off the preset's own `Channels` rather than CHANNEL_ORDER, so a plastic offers no emissive row
//    and the emissive panel does. `normal` never appears: it is derived from height at shade time, and a
//    row for it would imply a value the layer does not carry.
export function MaterialChannelRows(Preset)
{
    return [...(MATERIAL_PRESETS[Preset]?.Channels ?? [])];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  GENERATOR RECIPES
//------------------------------------------------------------------------------------------------------------------------

// Each generator declares which channels it drives and the defaults for the four shared parameters.
//
// 🔴 `Drives` is load-bearing: a generator writes ONLY these channels, and the atlases it does not drive
//    stay transparent so the layers beneath show through. A rust generator that also wrote height and
//    emissive would silently flatten a height pass underneath it and make the model glow.
export const GENERATOR_RECIPES = {
    rust: {
        Label:  "Rust",
        Tint:   "#b7410e",
        Drives: ["baseColour", "roughness", "height"],
        // Rust is patchy, high-contrast and rough. It eats the metal underneath, so it drives colour and
        // roughness together — rust that stayed shiny would read as painted-on brown, not corrosion.
        Params: { Scale: 0.70, Contrast: 0.82, Amount: 0.55, Seed: 24 },
        Palette: { Low: [0.30, 0.12, 0.04], High: [0.62, 0.28, 0.09],
                   RoughLow: 0.55, RoughHigh: 0.95 }
    },
    noise: {
        Label:  "Noise",
        Tint:   "#7f8c8d",
        Drives: ["baseColour", "roughness"],
        Params: { Scale: 0.45, Contrast: 0.50, Amount: 1.00, Seed: 7 },
        Palette: { Low: [0.14, 0.14, 0.16], High: [0.78, 0.78, 0.80],
                   RoughLow: 0.25, RoughHigh: 0.85 }
    },
    scratches: {
        Label:  "Scratches",
        Tint:   "#bdc3c7",
        // Scratches cut the surface, so they drive HEIGHT as well — that is what makes the derived normal
        // catch the light along each groove instead of just tinting it.
        Drives: ["baseColour", "roughness", "height"],
        Params: { Scale: 0.85, Contrast: 0.90, Amount: 0.40, Seed: 91 },
        Palette: { Low: [0.72, 0.73, 0.75], High: [0.92, 0.93, 0.95],
                   RoughLow: 0.12, RoughHigh: 0.40 }
    }
};

export const GENERATOR_ORDER = ["rust", "noise", "scratches"];

export const GENERATOR_PARAMS = [
    { Key: "Scale",    Label: "Scale",    Min: 0.02, Max: 1,   Step: 0.01 },
    { Key: "Contrast", Label: "Contrast", Min: 0,    Max: 1,   Step: 0.01 },
    { Key: "Amount",   Label: "Amount",   Min: 0,    Max: 1,   Step: 0.01 },
    // 🔴 Seed is an INTEGER and its step must stay 1. A fractional seed still hashes to something, but
    //    dragging the slider would then walk through a continuum of near-identical patterns instead of
    //    giving the discrete re-rolls the control implies.
    { Key: "Seed",     Label: "Seed",     Min: 0,    Max: 999, Step: 1 }
];

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// The default per-channel mode map for a new layer of a given kind.
//
// 📝 A paint layer's channels default to Texture because that is the only mode a stroke can reach; a
//    fill or material defaults to Value; a generator defaults its driven channels to Generator and
//    leaves the rest on Value.
export function DefaultChannelModes(Kind, Generator)
{
    const Modes  = {};
    const Recipe = Generator ? GENERATOR_RECIPES[Generator] : null;

    for (const Key of CHANNEL_ORDER)
    {
        if (Kind === "generator") { Modes[Key] = Recipe?.Drives.includes(Key) ? "Generator" : "Value"; }
        else if (Kind === "paint") { Modes[Key] = "Texture"; }
        else                       { Modes[Key] = "Value"; }
    }
    return Modes;
}

// Which channels a newly created layer of this kind should have enabled.
export function DefaultChannels(Kind, Preset, Generator)
{
    if (Kind === "material" && MATERIAL_PRESETS[Preset]) { return [...MATERIAL_PRESETS[Preset].Channels]; }
    if (Kind === "generator" && GENERATOR_RECIPES[Generator]) { return [...GENERATOR_RECIPES[Generator].Drives]; }
    if (Kind === "fill") { return ["baseColour", "metallic", "roughness"]; }
    return ["baseColour"];
}
