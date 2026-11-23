/*====================================================================================================================================
                                                     SURFACECAPTURE.JS
====================================================================================================================================*/
// 🧩 Compositor-independent readback of a render target: pixel tallies to assert on, plus a PNG to look at

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 copyTextureToBuffer demands each row start on a 256-byte boundary, so the readback stride is almost
//    never width*4. Indexing the mapped bytes as though it were is the standard way to read diagonal
//    garbage out of a perfectly good render.
const RowAlignment = 256;                       // [B]

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

export function ResolveRowByteStride(SurfaceWidth)
{
    return Math.ceil(SurfaceWidth * 4 / RowAlignment) * RowAlignment;
}

// Allocate a colour target that can be both drawn into and read back.
export function CreateCaptureTarget(Device, SurfaceWidth, SurfaceHeight, ColourFormat)
{
    return Device.createTexture({
        label:  "CaptureTarget",
        size:   [SurfaceWidth, SurfaceHeight],
        format: ColourFormat,
        usage:  GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC
    });
}

// Queue a texture-to-buffer copy on an open encoder. Returns the staging buffer to map afterwards.
export function EncodeCapture(Device, Encoder, CaptureTexture, SurfaceWidth, SurfaceHeight)
{
    const RowByteStride = ResolveRowByteStride(SurfaceWidth);

    const ReadbackStore = Device.createBuffer({
        label: "CaptureReadback",
        size:  RowByteStride * SurfaceHeight,
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ
    });

    Encoder.copyTextureToBuffer(
        { texture: CaptureTexture },
        { buffer: ReadbackStore, bytesPerRow: RowByteStride },
        [SurfaceWidth, SurfaceHeight]);

    return ReadbackStore;
}

// Map the staging buffer, tally the distinct colours, and re-encode to a PNG data address.
//
// 📝 The preferred canvas format here is bgra8unorm, so the raw bytes arrive as B,G,R,A. Both the tally
//    and the PNG swizzle to RGBA — otherwise every reported colour has its red and blue exchanged and
//    an assertion written against the intended colour fails for no visible reason.
export async function ResolveCapture(ReadbackStore, SurfaceWidth, SurfaceHeight, ColourFormat)
{
    await ReadbackStore.mapAsync(GPUMapMode.READ);
    const Raw = new Uint8Array(ReadbackStore.getMappedRange()).slice();
    ReadbackStore.unmap();
    ReadbackStore.destroy();

    const RowByteStride    = ResolveRowByteStride(SurfaceWidth);
    const SwizzleRequired  = ColourFormat.startsWith("bgra");

    const Straightened = new Uint8ClampedArray(SurfaceWidth * SurfaceHeight * 4);
    // 🔴 A SECOND array that keeps the real alpha. `Straightened` deliberately forces alpha to 255 so
    //    the PNG below is viewable — a screen capture written with its true alpha renders as an empty
    //    transparent image. But a LAYER atlas stores coverage in alpha, and a caller measuring "was this
    //    texel painted" against the opaque copy sees every texel covered: the whole 1024² atlas reads as
    //    painted, a stroke's mean value comes back diluted toward the clear value, and the failure looks
    //    like a paint bug rather than a readback bug. Keep both; never let the PNG path define coverage.
    const WithAlpha    = new Uint8ClampedArray(SurfaceWidth * SurfaceHeight * 4);
    const Tally        = new Map();

    for (let RowOrdinal = 0; RowOrdinal < SurfaceHeight; RowOrdinal += 1)
    {
        for (let ColumnOrdinal = 0; ColumnOrdinal < SurfaceWidth; ColumnOrdinal += 1)
        {
            const Source = RowOrdinal * RowByteStride + ColumnOrdinal * 4;
            const Target = (RowOrdinal * SurfaceWidth + ColumnOrdinal) * 4;

            const Red   = Raw[Source + (SwizzleRequired ? 2 : 0)];
            const Green = Raw[Source + 1];
            const Blue  = Raw[Source + (SwizzleRequired ? 0 : 2)];
            const Alpha = Raw[Source + 3];

            Straightened[Target + 0] = Red;
            Straightened[Target + 1] = Green;
            Straightened[Target + 2] = Blue;
            Straightened[Target + 3] = 255;

            WithAlpha[Target + 0] = Red;
            WithAlpha[Target + 1] = Green;
            WithAlpha[Target + 2] = Blue;
            WithAlpha[Target + 3] = Alpha;

            const Key = `${Red},${Green},${Blue}`;
            Tally.set(Key, (Tally.get(Key) ?? 0) + 1);
        }
    }

    const Encode  = document.createElement("canvas");
    Encode.width  = SurfaceWidth;
    Encode.height = SurfaceHeight;

    const EncodeContext = Encode.getContext("2d");
    const EncodeImage   = EncodeContext.createImageData(SurfaceWidth, SurfaceHeight);
    EncodeImage.data.set(Straightened);
    EncodeContext.putImageData(EncodeImage, 0, 0);

    const Ranked = [...Tally.entries()].sort((A, B) => B[1] - A[1]);

    return {
        DistinctColourCount: Tally.size,
        DominantColour:      Ranked.slice(0, 6).map(([Colour, Count]) => `${Colour} x${Count}`),
        PixelTotal:          SurfaceWidth * SurfaceHeight,
        PortableNetworkGraphic: Encode.toDataURL("image/png"),
        Straightened,
        WithAlpha
    };
}

// Fraction of pixels that differ from the clear colour — the honest measure of "did anything draw".
export function MeasureCoverage(Capture, ClearRed, ClearGreen, ClearBlue, Tolerance)
{
    const Slack = Tolerance ?? 6;
    let Covered = 0;

    for (let Offset = 0; Offset < Capture.Straightened.length; Offset += 4)
    {
        const DeltaRed   = Math.abs(Capture.Straightened[Offset + 0] - ClearRed);
        const DeltaGreen = Math.abs(Capture.Straightened[Offset + 1] - ClearGreen);
        const DeltaBlue  = Math.abs(Capture.Straightened[Offset + 2] - ClearBlue);

        if (DeltaRed > Slack || DeltaGreen > Slack || DeltaBlue > Slack) { Covered += 1; }
    }

    return Covered / Capture.PixelTotal;
}
