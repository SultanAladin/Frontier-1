/*====================================================================================================================================
                                                      MATERIALSWATCH.JS
====================================================================================================================================*/
// 🧩 The preview sphere behind every material tile — an analytic GGX evaluation onto a 2D canvas, cached per size

import { MATERIAL_PRESETS } from "./LayerKinds.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                      SWATCH LIGHTING
//------------------------------------------------------------------------------------------------------------------------

// A three-softbox rig: cool key from upper-left, warm rim from the right, broad blue fill from below-front.
//
// 🔴 These are NOT the viewport's lights and are not meant to be. A swatch has to separate fourteen presets
//    from one another at 64 pixels, which needs a key hard enough to give chrome a hotspot and a rim warm
//    enough to keep a black lacquer off pure black. Matching the viewport's single sun instead would render
//    piano black, rubber and carbon as three identical dark circles, and the browser would stop being a
//    browser. `size` is the box's angular width, and the intensity is scaled by it below so widening a box
//    softens the highlight rather than making the tile brighter.
const SWATCH_LIGHTS = [
    { Direction: [-0.55,  0.68,  0.52], Colour: [5.40, 5.70, 6.30], Size: 0.34 },
    { Direction: [ 0.78,  0.30, -0.62], Colour: [4.10, 2.65, 1.55], Size: 0.17 },
    { Direction: [ 0.62, -0.12,  0.72], Colour: [0.62, 0.70, 0.92], Size: 0.52 }
];

// The dielectric reflectance every non-metal sits at. 0.04 is the standard 4% normal-incidence figure for
// the common IOR-1.5 case, and the raster uses the same constant, so a swatch and the model agree.
const DIELECTRIC_F0 = 0.04;

// 🔴 The roughness floor. The GGX denominator collapses toward a delta as alpha goes to zero, so a literal
//    0 both blows up numerically and makes every mirror preset resolve to one indistinguishable highlight.
//    The raster clamps at the same place; if these two floors drift apart, the tile stops predicting the
//    model at exactly the smooth end where the difference is most visible.
const ROUGHNESS_FLOOR = 0.03;

//------------------------------------------------------------------------------------------------------------------------
//                                                     CHANNEL READING
//------------------------------------------------------------------------------------------------------------------------

// A swatch is painted from a plain channel bag, so it can preview either a preset from the table OR a live
// layer's edited values. Anything absent falls back to the neutral the atlas clears to.
//
// 📝 The five channels this prototype stores are all that is read. The shelf these presets came from carries
//    forty (clearcoat, sheen, flake, weave, tow anisotropy, subsurface…) and its swatch painter had a block
//    for each. None of them reach a shader input here — the raster samples three RGBA8 atlases and derives
//    the normal from height — so previewing them would make the tile promise a look the model cannot render.
//    A coat is folded into the roughness it produces, in the preset table, once.
function ReadTriple(Values, Key, Fallback)
{
    const Value = Values?.[Key];
    if (!Array.isArray(Value)) { return Fallback; }
    return [Number(Value[0]) || 0, Number(Value[1]) || 0, Number(Value[2]) || 0];
}

function ReadScalar(Values, Key, Fallback)
{
    const Value = Values?.[Key];
    return Number.isFinite(Value) ? Value : Fallback;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE SWATCH PAINTER
//------------------------------------------------------------------------------------------------------------------------

// Paint one preview sphere into `Canvas`, sized from the canvas's own backing store.
//
// The sphere is implicit: for each pixel the unit disc gives the normal directly (Nz from the circle), the
// view is orthographic down +Z, and everything outside the disc is left fully transparent so the tile takes
// its background from the CSS beneath it rather than carrying a baked-in panel colour.
export function PaintMaterialSwatch(Canvas, Values)
{
    const Width   = Canvas.width;
    const Height  = Canvas.height;
    const Context = Canvas.getContext("2d");
    const Image   = Context.createImageData(Width, Height);
    const Data    = Image.data;

    const CentreX = Width / 2;
    const CentreY = Height / 2;
    const Radius  = Math.min(CentreX, CentreY) - 1;

    const Base     = ReadTriple(Values, "baseColour", [0.5, 0.5, 0.5]);
    const Emission = ReadTriple(Values, "emission",   [0, 0, 0]);
    const Metal    = Math.min(1, Math.max(0, ReadScalar(Values, "metallic",  0)));
    const Rough    = Math.min(1, Math.max(0, ReadScalar(Values, "roughness", 0.5)));

    // The metallic split, exactly as the raster does it: a metal's base colour IS its specular reflectance
    // and it has no diffuse lobe; a dielectric keeps its albedo and reflects the flat 4%.
    const F0            = Base.map((C) => DIELECTRIC_F0 + (C - DIELECTRIC_F0) * Metal);
    const DiffuseColour = Base.map((C) => C * (1 - Metal));

    const Alpha        = Math.max(ROUGHNESS_FLOOR, Rough) ** 2;
    const AlphaSquared = Alpha * Alpha;

    for (let Y = 0; Y < Height; Y++)
    {
        for (let X = 0; X < Width; X++)
        {
            const Ux     = (X - CentreX) / Radius;
            const Uy     = (Y - CentreY) / Radius;
            const R2     = Ux * Ux + Uy * Uy;
            const Offset = (Y * Width + X) * 4;

            // Outside the disc: alpha 0. Left as the ImageData's zero-fill, so no colour is written either —
            // a coloured-but-transparent texel would fringe once the browser filters the canvas down.
            if (R2 > 1) { Data[Offset + 3] = 0; continue; }

            // 📝 Ny is NEGATED. Canvas Y runs downward and the light rig is authored in a Y-up frame, so
            //    without the flip the key light arrives from below and every swatch reads as underlit.
            const Nx = Ux;
            const Ny = -Uy;
            const Nz = Math.sqrt(Math.max(0, 1 - R2));

            // Orthographic view down +Z, so V is constant and NdotV is just Nz.
            const NdotV  = Math.max(Nz, 1e-4);
            const Colour = [0, 0, 0];

            for (const Light of SWATCH_LIGHTS)
            {
                const Direction = Light.Direction;
                const Distance  = Math.hypot(Direction[0], Direction[1], Direction[2]) || 1;
                const Lx = Direction[0] / Distance;
                const Ly = Direction[1] / Distance;
                const Lz = Direction[2] / Distance;

                const NdotL = Nx * Lx + Ny * Ly + Nz * Lz;
                if (NdotL <= 0) { continue; }

                // Half vector against V = (0,0,1).
                let Hx = Lx;
                let Hy = Ly;
                let Hz = Lz + 1;
                const HalfLength = Math.hypot(Hx, Hy, Hz) || 1;
                Hx /= HalfLength; Hy /= HalfLength; Hz /= HalfLength;

                const NdotH = Math.max(0, Nx * Hx + Ny * Hy + Nz * Hz);
                const VdotH = Math.max(0, Hz);

                // GGX normal distribution.
                const Denominator = NdotH * NdotH * (AlphaSquared - 1) + 1;
                const D = AlphaSquared / Math.max(Math.PI * Denominator * Denominator, 1e-7);

                // Smith height-correlated visibility — the G/(4·NdotL·NdotV) form, so the 4 is already in it.
                const LambdaV = NdotL * Math.sqrt(NdotV * NdotV * (1 - AlphaSquared) + AlphaSquared);
                const LambdaL = NdotV * Math.sqrt(NdotL * NdotL * (1 - AlphaSquared) + AlphaSquared);
                const Visibility = 0.5 / Math.max(LambdaV + LambdaL, 1e-7);

                const Fresnel   = (1 - VdotH) ** 5;
                const Intensity = Light.Size * 1.7;

                for (let Index = 0; Index < 3; Index++)
                {
                    const F             = F0[Index] + (1 - F0[Index]) * Fresnel;
                    const SpecularTerm  = D * Visibility * F;
                    const DiffuseTerm   = DiffuseColour[Index] / Math.PI * (1 - F);
                    Colour[Index] += (DiffuseTerm + SpecularTerm) * NdotL * Light.Colour[Index] * Intensity;
                }
            }

            // Ambient: a crude irradiance stand-in, tinted up toward the zenith.
            //
            // 🔴 The metal term is not decoration. With three punctual lights and no environment map, a metal
            //    is black everywhere it is not directly reflecting a box — chrome and gold would both come out
            //    as a dark circle with two dots, indistinguishable from each other and from piano black.
            const Sky = 0.5 + 0.5 * Ny;
            for (let Index = 0; Index < 3; Index++)
            {
                const Ambient = [0.030, 0.033, 0.042][Index] * Sky + 0.012;
                Colour[Index] += DiffuseColour[Index] * Ambient * 2.4;
                Colour[Index] += F0[Index] * Ambient * Metal * 3.0 * (1 - Rough * 0.7);
            }

            // Emission is added AFTER the BRDF and takes no NdotL — that is what makes it emission and not a
            // bright albedo. It is the only one of the five channels the donor swatch had no term for.
            Colour[0] += Emission[0];
            Colour[1] += Emission[1];
            Colour[2] += Emission[2];

            // ACES + sRGB, matching the viewport's tonemap so the tile and the model agree.
            for (let Index = 0; Index < 3; Index++)
            {
                let C = Colour[Index] * 1.35;
                C = Math.min(1, Math.max(0, (C * (2.51 * C + 0.03)) / (C * (2.43 * C + 0.59) + 0.14)));
                Data[Offset + Index] = Math.round(255 * Math.pow(C, 1 / 2.2));
            }
            Data[Offset + 3] = 255;
        }
    }

    Context.putImageData(Image, 0, 0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       SWATCH CACHE
//------------------------------------------------------------------------------------------------------------------------

// 🔴 The cache is keyed by size as well as by material, and both halves are required. The same preset is
//    drawn at three sizes at once — the grid tile, the drag ghost and the hero card — and each is a separate
//    per-pixel raster, so a material-only key would hand the hero a 64-pixel canvas and the tile a 132-pixel
//    one, whichever painted last. Painting is O(size²) in JavaScript: without the cache, one rail filter
//    repaints fourteen spheres and the drawer's slide visibly stutters.
const SwatchCache = new Map();

const CacheKey = (Token, Size) => `${Token}@${Size}`;

// Return a painted canvas for a preset key, at `Size` CSS pixels. Cached; safe to call every frame.
export function MaterialSwatchCanvas(Preset, Size)
{
    const Key    = CacheKey(Preset, Size);
    const Cached = SwatchCache.get(Key);
    if (Cached) { return Cached; }

    const Canvas  = document.createElement("canvas");
    Canvas.width  = Size;
    Canvas.height = Size;
    Canvas.style.width  = `${Size}px`;
    Canvas.style.height = `${Size}px`;

    PaintMaterialSwatch(Canvas, MATERIAL_PRESETS[Preset]?.Values ?? {});
    SwatchCache.set(Key, Canvas);
    return Canvas;
}

// Paint an arbitrary channel bag — a LIVE layer's edited values rather than a preset's authored ones.
//
// 📝 Uncached by design. The caller here is a slider drag, so every call carries different values and a cache
//    keyed on anything cheaper than the values themselves would serve a stale sphere; keyed on the values it
//    would grow without bound over a drag. The cost is bounded instead by only ever asking at one size.
export function PaintValuesSwatch(Canvas, Values)
{
    PaintMaterialSwatch(Canvas, Values ?? {});
}

// Drop every cached size for one preset. Called when a preset's authored values change under an edit.
//
// 🔴 Every size, not just the one on screen. The tile, ghost and hero are three separate entries, and
//    dropping only the visible one leaves the hero card showing the pre-edit sphere the moment the drawer
//    reopens — which reads as "the edit was lost" rather than "the preview is stale".
export function InvalidateMaterialSwatch(Preset)
{
    const Prefix = `${Preset}@`;
    for (const Key of [...SwatchCache.keys()])
    {
        if (Key.startsWith(Prefix)) { SwatchCache.delete(Key); }
    }
}

export function InvalidateAllMaterialSwatches()
{
    SwatchCache.clear();
}
