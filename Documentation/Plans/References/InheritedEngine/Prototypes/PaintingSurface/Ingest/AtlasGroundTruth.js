/*====================================================================================================================================
                                                   ATLASGROUNDTRUTH.JS
====================================================================================================================================*/
// 🧩 Diagnostic atlas: a checker field overlaid with the surface's own unwrap, for verifying UV mapping

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

export const AtlasFormat = "rgba8unorm";

// 📝 A plain checker verifies almost nothing on its own: it is symmetric under both mirroring and a
//    v-flip, so the two most likely UV mistakes leave it looking correct. Overlaying the ACTUAL unwrap
//    wireframe makes those mistakes visible — Suzanne's five patches are asymmetric and individually
//    recognisable, so a flipped or mirrored atlas is obvious at a glance.
const CheckerSquarePixels = 64;                 // [px]  - Checker cell edge at 1024
const PatchTintAlpha      = 0.34;               // [-]   - Patch fill over the checker
const WireThickness       = 1.4;                // [px]

// One tint per named patch, so each region of the unwrap reads distinctly.
const PatchTint = {
    PRJ_Head:  [ 92, 148, 232],
    PRJ_Ear_L: [232, 148,  92],
    PRJ_Ear_R: [124, 214, 132],
    PRJ_Eye_L: [226, 106, 178],
    PRJ_Eye_R: [212, 196,  96]
};

const FallbackTint = [150, 150, 158];

//------------------------------------------------------------------------------------------------------------------------
//                                                   INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The atlas is authored with v pointing UP (the OBJ/Blender convention), while a canvas y axis points
//    DOWN. Every UV drawn here therefore maps to y = (1 - v) * height. The surface shader performs the
//    matching flip on sample, so the two conventions meet in exactly one place each.
function CoordinateToCanvas(U, V, Extent)
{
    return [U * Extent, (1.0 - V) * Extent];
}

function PaintCheckerField(Context, Extent)
{
    const SquarePixels = CheckerSquarePixels * (Extent / 1024);
    const CellCount    = Math.ceil(Extent / SquarePixels);

    for (let RowOrdinal = 0; RowOrdinal < CellCount; RowOrdinal += 1)
    {
        for (let ColumnOrdinal = 0; ColumnOrdinal < CellCount; ColumnOrdinal += 1)
        {
            const Light = ((RowOrdinal + ColumnOrdinal) & 1) === 0;
            Context.fillStyle = Light ? "#d8d8dc" : "#a8a8b0";
            Context.fillRect(ColumnOrdinal * SquarePixels, RowOrdinal * SquarePixels, SquarePixels, SquarePixels);
        }
    }
}

// 📝 A directional marker in the atlas corner pins orientation absolutely. The checker cannot do this;
//    without it a 180-degree atlas rotation still looks like a valid checker.
function PaintOrientationMarker(Context, Extent)
{
    const Inset = Extent * 0.012;
    const Size  = Extent * 0.055;

    // UV origin (0,0) sits at the BOTTOM-left of the atlas, so the marker lands at canvas bottom-left.
    Context.fillStyle = "#e2364a";
    Context.beginPath();
    Context.moveTo(Inset,               Extent - Inset);
    Context.lineTo(Inset + Size,        Extent - Inset);
    Context.lineTo(Inset,               Extent - Inset - Size);
    Context.closePath();
    Context.fill();

    Context.fillStyle = "#1b6ce0";
    Context.fillRect(Extent - Inset - Size * 0.5, Inset, Size * 0.5, Size * 0.5);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Draw the diagnostic atlas into an offscreen 2D canvas and return its pixel data.
//
// PatchRoster is the parsed suzanne_uv.json shape: an array of
//   { name, loops: [ [ [u, v], ... ], ... ] }
export function ComposeGroundTruthAtlas(Extent, PatchRoster)
{
    const Canvas  = document.createElement("canvas");
    Canvas.width  = Extent;
    Canvas.height = Extent;

    const Context = Canvas.getContext("2d");

    PaintCheckerField(Context, Extent);

    let LoopTally = 0;

    for (const Patch of PatchRoster ?? [])
    {
        const Tint = PatchTint[Patch.name] ?? FallbackTint;

        Context.fillStyle   = `rgba(${Tint[0]}, ${Tint[1]}, ${Tint[2]}, ${PatchTintAlpha})`;
        Context.strokeStyle = `rgb(${Math.round(Tint[0] * 0.42)}, ${Math.round(Tint[1] * 0.42)}, ${Math.round(Tint[2] * 0.42)})`;
        Context.lineWidth   = WireThickness * (Extent / 1024);

        for (const Loop of Patch.loops ?? [])
        {
            if (!Loop || Loop.length < 3) continue;

            Context.beginPath();
            for (let CornerOrdinal = 0; CornerOrdinal < Loop.length; CornerOrdinal += 1)
            {
                const [X, Y] = CoordinateToCanvas(Loop[CornerOrdinal][0], Loop[CornerOrdinal][1], Extent);
                if (CornerOrdinal === 0) { Context.moveTo(X, Y); } else { Context.lineTo(X, Y); }
            }
            Context.closePath();
            Context.fill();
            Context.stroke();

            LoopTally += 1;
        }
    }

    PaintOrientationMarker(Context, Extent);

    const Image = Context.getImageData(0, 0, Extent, Extent);
    return { Pixels: new Uint8Array(Image.data.buffer.slice(0)), Extent, LoopTally };
}

// Upload a composed atlas into a sampleable GPU texture.
export function UploadAtlas(Device, Composed)
{
    const AtlasStore = Device.createTexture({
        label:  "PaintAtlas",
        size:   [Composed.Extent, Composed.Extent],
        format: AtlasFormat,
        usage:  GPUTextureUsage.TEXTURE_BINDING
             |  GPUTextureUsage.COPY_DST
             |  GPUTextureUsage.COPY_SRC
             |  GPUTextureUsage.RENDER_ATTACHMENT
    });

    Device.queue.writeTexture(
        { texture: AtlasStore },
        Composed.Pixels,
        { bytesPerRow: Composed.Extent * 4, rowsPerImage: Composed.Extent },
        [Composed.Extent, Composed.Extent]);

    return AtlasStore;
}

// Fetch the unwrap description that ComposeGroundTruthAtlas consumes.
export async function RetrievePatchRoster(SourceAddress)
{
    const Response = await fetch(SourceAddress);
    if (!Response.ok)
    {
        throw new Error(`unwrap fetch failed: ${Response.status} ${Response.statusText} for ${SourceAddress}`);
    }
    return await Response.json();
}
