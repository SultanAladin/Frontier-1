/*====================================================================================================================================
                                                     SURFACEUPLOAD.JS
====================================================================================================================================*/
// 🧩 Upload decoded surface arrays into interleaved GPU vertex storage plus an index buffer

import { DecodeWavefront } from "./WavefrontDecode.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One interleaved stream rather than three parallel buffers. Every pass in this prototype — the
//    surface raster, the UV-space dab raster, the unwrap overlay — consumes position, coordinate and
//    normal together, so splitting them would only add bind overhead with no pass benefiting.
const VertexFloatCount = 8;                     // [-]   - position(3) + coordinate(2) + normal(3)
const VertexByteStride = VertexFloatCount * 4;  // [B]   - Bytes per interleaved vertex

const AttributeOffsetPosition   = 0;            // [B]
const AttributeOffsetCoordinate = 12;           // [B]
const AttributeOffsetNormal     = 20;           // [B]

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// The vertex layout the surface pipelines declare. Kept beside the interleave so the two cannot drift.
export const SurfaceVertexLayout = {
    arrayStride: VertexByteStride,
    attributes: [
        { shaderLocation: 0, offset: AttributeOffsetPosition,   format: "float32x3" },
        { shaderLocation: 1, offset: AttributeOffsetCoordinate, format: "float32x2" },
        { shaderLocation: 2, offset: AttributeOffsetNormal,     format: "float32x3" }
    ]
};

// Interleave the three de-indexed arrays into one Float32Array in the layout above.
export function InterleaveSurface(Decoded)
{
    const Interleaved = new Float32Array(Decoded.VertexCount * VertexFloatCount);

    for (let VertexOrdinal = 0; VertexOrdinal < Decoded.VertexCount; VertexOrdinal += 1)
    {
        const Target = VertexOrdinal * VertexFloatCount;

        Interleaved[Target + 0] = Decoded.Position[VertexOrdinal * 3 + 0];
        Interleaved[Target + 1] = Decoded.Position[VertexOrdinal * 3 + 1];
        Interleaved[Target + 2] = Decoded.Position[VertexOrdinal * 3 + 2];

        Interleaved[Target + 3] = Decoded.Coordinate[VertexOrdinal * 2 + 0];
        Interleaved[Target + 4] = Decoded.Coordinate[VertexOrdinal * 2 + 1];

        Interleaved[Target + 5] = Decoded.Normal[VertexOrdinal * 3 + 0];
        Interleaved[Target + 6] = Decoded.Normal[VertexOrdinal * 3 + 1];
        Interleaved[Target + 7] = Decoded.Normal[VertexOrdinal * 3 + 2];
    }

    return Interleaved;
}

// Derive the framing a camera needs to fit the surface: the centre of the bounding box and the radius
// of the sphere enclosing it.
export function MeasureFraming(Decoded)
{
    const Centre = [
        (Decoded.MinimumBoundary[0] + Decoded.MaximumBoundary[0]) * 0.5,
        (Decoded.MinimumBoundary[1] + Decoded.MaximumBoundary[1]) * 0.5,
        (Decoded.MinimumBoundary[2] + Decoded.MaximumBoundary[2]) * 0.5
    ];

    const HalfSpan = [
        (Decoded.MaximumBoundary[0] - Decoded.MinimumBoundary[0]) * 0.5,
        (Decoded.MaximumBoundary[1] - Decoded.MinimumBoundary[1]) * 0.5,
        (Decoded.MaximumBoundary[2] - Decoded.MinimumBoundary[2]) * 0.5
    ];

    const Radius = Math.sqrt(HalfSpan[0] * HalfSpan[0] + HalfSpan[1] * HalfSpan[1] + HalfSpan[2] * HalfSpan[2]);

    return { Centre, HalfSpan, Radius };
}

// Fetch an OBJ, decode it, and place it in GPU buffers.
//
// Returns { VertexStore, IndexStore, VertexCount, TriangleCount, IndexCount, Framing, Decoded }
export async function UploadSurfaceFromAddress(Device, SourceAddress)
{
    const Response = await fetch(SourceAddress);
    if (!Response.ok)
    {
        throw new Error(`surface fetch failed: ${Response.status} ${Response.statusText} for ${SourceAddress}`);
    }

    const Decoded = DecodeWavefront(await Response.text());
    return UploadSurface(Device, Decoded);
}

// Place already-decoded surface arrays into GPU buffers.
export function UploadSurface(Device, Decoded)
{
    const Interleaved = InterleaveSurface(Decoded);

    const VertexStore = Device.createBuffer({
        label: "SurfaceVertex",
        size:  Interleaved.byteLength,
        usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST
    });
    Device.queue.writeBuffer(VertexStore, 0, Interleaved);

    // 📝 writeBuffer demands a size that is a multiple of 4. A Uint32Array index run always satisfies
    //    that, but the buffer allocation is rounded explicitly so a future Uint16 path cannot trip it.
    const IndexByteLength = Math.ceil(Decoded.Index.byteLength / 4) * 4;

    const IndexStore = Device.createBuffer({
        label: "SurfaceIndex",
        size:  IndexByteLength,
        usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST
    });
    Device.queue.writeBuffer(IndexStore, 0, Decoded.Index);

    return {
        VertexStore,
        IndexStore,
        VertexCount:   Decoded.VertexCount,
        TriangleCount: Decoded.TriangleCount,
        IndexCount:    Decoded.Index.length,
        Framing:       MeasureFraming(Decoded),
        Decoded
    };
}
