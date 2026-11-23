/*====================================================================================================================================
                                                    WAVEFRONTDECODE.JS
====================================================================================================================================*/
// 🧩 Wavefront OBJ decode into de-indexed triangle arrays with per-corner texture coordinates

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const PositionStride   = 3;                     // [-]   - Floats per position (x, y, z)
const CoordinateStride = 2;                     // [-]   - Floats per texture coordinate (u, v)
const NormalStride     = 3;                     // [-]   - Floats per normal (x, y, z)

//------------------------------------------------------------------------------------------------------------------------
//                                                   INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 OBJ indices are 1-based and may be negative (relative to the end of the list already parsed).
//    Resolve both forms to a 0-based index. A missing or empty field yields -1 (absent).
function ResolveIndex(Token, ParsedCount)
{
    if (Token === undefined || Token === "") return -1;

    const Signed = parseInt(Token, 10);
    if (Number.isNaN(Signed)) return -1;
    if (Signed > 0) return Signed - 1;
    return ParsedCount + Signed;
}

// 📝 A face corner is identified by its full (position/coordinate/normal) triple, NOT by its position
//    index alone. Suzanne carries 507 positions against 555 coordinates: the extra 48 are UV seam
//    splits where one position appears under two coordinates. Keying on position alone would weld
//    those splits back together and tear the atlas along every seam.
function CornerKey(PositionIndex, CoordinateIndex, NormalIndex)
{
    return `${PositionIndex}/${CoordinateIndex}/${NormalIndex}`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Decode Wavefront OBJ source text into de-indexed, triangulated surface arrays ready for GPU upload.
//
// Returns:
//   Position          Float32Array   3 floats per unique corner
//   Coordinate        Float32Array   2 floats per unique corner
//   Normal            Float32Array   3 floats per unique corner
//   Index             Uint32Array    3 indices per triangle
//   VertexCount       number         Unique corners emitted
//   TriangleCount     number         Triangles emitted
//   MinimumBoundary   [x, y, z]      Axis-aligned lower bound of the positions
//   MaximumBoundary   [x, y, z]      Axis-aligned upper bound of the positions
//   CoordinateBounds  {MinimumU, MaximumU, MinimumV, MaximumV}
//   NormalPresence    boolean        Whether the source supplied any normals
export function DecodeWavefront(SourceText)
{
    const SourcePosition   = [];                // Flat xyz triples, source order
    const SourceCoordinate = [];                // Flat uv pairs, source order
    const SourceNormal     = [];                // Flat xyz triples, source order

    const CornerPosition   = [];
    const CornerCoordinate = [];
    const CornerNormal     = [];
    const TriangleIndex    = [];

    const CornerRegistry   = new Map();         // CornerKey -> emitted corner index
    let   NormalPresence   = false;

    const Lines = SourceText.split("\n");

    for (let LineOrdinal = 0; LineOrdinal < Lines.length; LineOrdinal += 1)
    {
        const Line = Lines[LineOrdinal].trim();
        if (Line.length === 0 || Line.charCodeAt(0) === 35) continue;   // 35 = '#'

        const Field = Line.split(/\s+/);
        const Directive = Field[0];

        if (Directive === "v")
        {
            SourcePosition.push(parseFloat(Field[1]), parseFloat(Field[2]), parseFloat(Field[3]));
        }
        else if (Directive === "vt")
        {
            SourceCoordinate.push(parseFloat(Field[1]), parseFloat(Field[2]));
        }
        else if (Directive === "vn")
        {
            SourceNormal.push(parseFloat(Field[1]), parseFloat(Field[2]), parseFloat(Field[3]));
            NormalPresence = true;
        }
        else if (Directive === "f")
        {
            // 📝 Resolve every corner of the face first, then fan-triangulate. Suzanne is 468 quads
            //    plus 32 triangles; the fan handles both and any n-gon that might appear later.
            const FaceCorner = [];

            for (let FieldOrdinal = 1; FieldOrdinal < Field.length; FieldOrdinal += 1)
            {
                const Token = Field[FieldOrdinal].split("/");

                const PositionIndex   = ResolveIndex(Token[0], SourcePosition.length   / PositionStride);
                const CoordinateIndex = ResolveIndex(Token[1], SourceCoordinate.length / CoordinateStride);
                const NormalIndex     = ResolveIndex(Token[2], SourceNormal.length     / NormalStride);

                if (PositionIndex < 0) continue;

                const Key = CornerKey(PositionIndex, CoordinateIndex, NormalIndex);
                let   EmittedIndex = CornerRegistry.get(Key);

                if (EmittedIndex === undefined)
                {
                    EmittedIndex = CornerPosition.length / PositionStride;
                    CornerRegistry.set(Key, EmittedIndex);

                    CornerPosition.push(
                        SourcePosition[PositionIndex * PositionStride + 0],
                        SourcePosition[PositionIndex * PositionStride + 1],
                        SourcePosition[PositionIndex * PositionStride + 2]);

                    if (CoordinateIndex >= 0)
                    {
                        CornerCoordinate.push(
                            SourceCoordinate[CoordinateIndex * CoordinateStride + 0],
                            SourceCoordinate[CoordinateIndex * CoordinateStride + 1]);
                    }
                    else
                    {
                        CornerCoordinate.push(0.0, 0.0);
                    }

                    if (NormalIndex >= 0)
                    {
                        CornerNormal.push(
                            SourceNormal[NormalIndex * NormalStride + 0],
                            SourceNormal[NormalIndex * NormalStride + 1],
                            SourceNormal[NormalIndex * NormalStride + 2]);
                    }
                    else
                    {
                        CornerNormal.push(0.0, 0.0, 0.0);
                    }
                }

                FaceCorner.push(EmittedIndex);
            }

            for (let FanOrdinal = 1; FanOrdinal + 1 < FaceCorner.length; FanOrdinal += 1)
            {
                TriangleIndex.push(FaceCorner[0], FaceCorner[FanOrdinal], FaceCorner[FanOrdinal + 1]);
            }
        }
    }

    // 📝 Any corner that arrived without a source normal gets an area-weighted face normal accumulated
    //    here, so the facing-falloff term in the dab shader always has a usable direction.
    if (!NormalPresence)
    {
        AccumulateCornerNormals(CornerPosition, CornerNormal, TriangleIndex);
    }

    const MinimumBoundary = [ Infinity,  Infinity,  Infinity];
    const MaximumBoundary = [-Infinity, -Infinity, -Infinity];

    for (let CornerOrdinal = 0; CornerOrdinal < CornerPosition.length; CornerOrdinal += PositionStride)
    {
        for (let AxisOrdinal = 0; AxisOrdinal < PositionStride; AxisOrdinal += 1)
        {
            const Component = CornerPosition[CornerOrdinal + AxisOrdinal];
            if (Component < MinimumBoundary[AxisOrdinal]) MinimumBoundary[AxisOrdinal] = Component;
            if (Component > MaximumBoundary[AxisOrdinal]) MaximumBoundary[AxisOrdinal] = Component;
        }
    }

    let MinimumU =  Infinity, MaximumU = -Infinity;
    let MinimumV =  Infinity, MaximumV = -Infinity;

    for (let CornerOrdinal = 0; CornerOrdinal < CornerCoordinate.length; CornerOrdinal += CoordinateStride)
    {
        const U = CornerCoordinate[CornerOrdinal + 0];
        const V = CornerCoordinate[CornerOrdinal + 1];
        if (U < MinimumU) MinimumU = U;
        if (U > MaximumU) MaximumU = U;
        if (V < MinimumV) MinimumV = V;
        if (V > MaximumV) MaximumV = V;
    }

    return {
        Position:         new Float32Array(CornerPosition),
        Coordinate:       new Float32Array(CornerCoordinate),
        Normal:           new Float32Array(CornerNormal),
        Index:            new Uint32Array(TriangleIndex),
        VertexCount:      CornerPosition.length / PositionStride,
        TriangleCount:    TriangleIndex.length / 3,
        MinimumBoundary,
        MaximumBoundary,
        CoordinateBounds: { MinimumU, MaximumU, MinimumV, MaximumV },
        NormalPresence
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Cross-product face normals summed onto each corner, then normalized. The un-normalized cross
//    product is proportional to twice the triangle area, so summing it area-weights automatically.
function AccumulateCornerNormals(CornerPosition, CornerNormal, TriangleIndex)
{
    CornerNormal.fill(0.0);

    for (let TriangleOrdinal = 0; TriangleOrdinal < TriangleIndex.length; TriangleOrdinal += 3)
    {
        const IndexA = TriangleIndex[TriangleOrdinal + 0] * PositionStride;
        const IndexB = TriangleIndex[TriangleOrdinal + 1] * PositionStride;
        const IndexC = TriangleOrdinal + 2 < TriangleIndex.length
                     ? TriangleIndex[TriangleOrdinal + 2] * PositionStride
                     : IndexA;

        const EdgeOneX = CornerPosition[IndexB + 0] - CornerPosition[IndexA + 0];
        const EdgeOneY = CornerPosition[IndexB + 1] - CornerPosition[IndexA + 1];
        const EdgeOneZ = CornerPosition[IndexB + 2] - CornerPosition[IndexA + 2];

        const EdgeTwoX = CornerPosition[IndexC + 0] - CornerPosition[IndexA + 0];
        const EdgeTwoY = CornerPosition[IndexC + 1] - CornerPosition[IndexA + 1];
        const EdgeTwoZ = CornerPosition[IndexC + 2] - CornerPosition[IndexA + 2];

        const FaceX = EdgeOneY * EdgeTwoZ - EdgeOneZ * EdgeTwoY;
        const FaceY = EdgeOneZ * EdgeTwoX - EdgeOneX * EdgeTwoZ;
        const FaceZ = EdgeOneX * EdgeTwoY - EdgeOneY * EdgeTwoX;

        for (const Corner of [IndexA, IndexB, IndexC])
        {
            CornerNormal[Corner + 0] += FaceX;
            CornerNormal[Corner + 1] += FaceY;
            CornerNormal[Corner + 2] += FaceZ;
        }
    }

    for (let CornerOrdinal = 0; CornerOrdinal < CornerNormal.length; CornerOrdinal += NormalStride)
    {
        const X = CornerNormal[CornerOrdinal + 0];
        const Y = CornerNormal[CornerOrdinal + 1];
        const Z = CornerNormal[CornerOrdinal + 2];

        const Magnitude = Math.sqrt(X * X + Y * Y + Z * Z);
        if (Magnitude > 1e-8)
        {
            CornerNormal[CornerOrdinal + 0] = X / Magnitude;
            CornerNormal[CornerOrdinal + 1] = Y / Magnitude;
            CornerNormal[CornerOrdinal + 2] = Z / Magnitude;
        }
    }
}
