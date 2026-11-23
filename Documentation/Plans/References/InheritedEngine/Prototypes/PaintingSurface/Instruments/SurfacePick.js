/*====================================================================================================================================
                                                      SURFACEPICK.JS
====================================================================================================================================*/
// 🧩 Cast a pointer ray into the surface and report the frontmost hit: triangle, barycentric, coordinate, normal

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Ray-triangle rejection epsilon. Guards the reciprocal of the determinant only — it is a
//    parallelism test, not a distance tolerance, so it stays small regardless of model scale.
const ParallelEpsilon = 1e-12;

// Hits behind the eye, or exactly at it, are not surface hits.
const MinimumTravel = 1e-6;

//------------------------------------------------------------------------------------------------------------------------
//                                                    RAY CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

// Invert a column-major 4x4. Returns null when the matrix is singular.
//
// 📝 Written out rather than looped: the cofactor form is branch-free and this runs once per pointer
//    event, so clarity of the algebra matters more than compactness.
export function InvertMatrix(M)
{
    const A00 = M[0],  A01 = M[1],  A02 = M[2],  A03 = M[3];
    const A10 = M[4],  A11 = M[5],  A12 = M[6],  A13 = M[7];
    const A20 = M[8],  A21 = M[9],  A22 = M[10], A23 = M[11];
    const A30 = M[12], A31 = M[13], A32 = M[14], A33 = M[15];

    const B00 = A00 * A11 - A01 * A10;
    const B01 = A00 * A12 - A02 * A10;
    const B02 = A00 * A13 - A03 * A10;
    const B03 = A01 * A12 - A02 * A11;
    const B04 = A01 * A13 - A03 * A11;
    const B05 = A02 * A13 - A03 * A12;
    const B06 = A20 * A31 - A21 * A30;
    const B07 = A20 * A32 - A22 * A30;
    const B08 = A20 * A33 - A23 * A30;
    const B09 = A21 * A32 - A22 * A31;
    const B10 = A21 * A33 - A23 * A31;
    const B11 = A22 * A33 - A23 * A32;

    const Determinant = B00 * B11 - B01 * B10 + B02 * B09 + B03 * B08 - B04 * B07 + B05 * B06;
    if (Math.abs(Determinant) < 1e-20) { return null; }

    const Reciprocal = 1.0 / Determinant;

    return new Float32Array([
        (A11 * B11 - A12 * B10 + A13 * B09) * Reciprocal,
        (A02 * B10 - A01 * B11 - A03 * B09) * Reciprocal,
        (A31 * B05 - A32 * B04 + A33 * B03) * Reciprocal,
        (A22 * B04 - A21 * B05 - A23 * B03) * Reciprocal,

        (A12 * B08 - A10 * B11 - A13 * B07) * Reciprocal,
        (A00 * B11 - A02 * B08 + A03 * B07) * Reciprocal,
        (A32 * B02 - A30 * B05 - A33 * B01) * Reciprocal,
        (A20 * B05 - A22 * B02 + A23 * B01) * Reciprocal,

        (A10 * B10 - A11 * B08 + A13 * B06) * Reciprocal,
        (A01 * B08 - A00 * B10 - A03 * B06) * Reciprocal,
        (A30 * B04 - A31 * B02 + A33 * B00) * Reciprocal,
        (A21 * B02 - A20 * B04 - A23 * B00) * Reciprocal,

        (A11 * B07 - A10 * B09 - A12 * B06) * Reciprocal,
        (A00 * B09 - A01 * B07 + A02 * B06) * Reciprocal,
        (A31 * B01 - A30 * B03 - A32 * B00) * Reciprocal,
        (A20 * B03 - A21 * B01 + A22 * B00) * Reciprocal
    ]);
}

// Column-major matrix times column vector, matching how WGSL evaluates `M * v`.
function TransformPoint(M, X, Y, Z, W)
{
    return [
        M[0] * X + M[4] * Y + M[8]  * Z + M[12] * W,
        M[1] * X + M[5] * Y + M[9]  * Z + M[13] * W,
        M[2] * X + M[6] * Y + M[10] * Z + M[14] * W,
        M[3] * X + M[7] * Y + M[11] * Z + M[15] * W
    ];
}

// Build a world-space ray from a pointer position in canvas pixels.
//
// 📝 The canvas y axis runs downward while normalized device coordinates run upward, so y is flipped
//    here. Getting it wrong mirrors picking about the horizon — and on a face-shaped model that reads
//    as "picking is slightly off" rather than as an obvious flip, so it is easy to miss.
export function BuildPointerRay(Resolved, PointerX, PointerY, SurfaceWidth, SurfaceHeight)
{
    const Inverse = InvertMatrix(Resolved.ViewProjection);
    if (Inverse === null) { return null; }

    const NdcX = (PointerX / SurfaceWidth)  * 2.0 - 1.0;
    const NdcY = 1.0 - (PointerY / SurfaceHeight) * 2.0;

    // WebGPU clip depth runs 0 at the near plane to 1 at the far plane.
    const NearPoint = TransformPoint(Inverse, NdcX, NdcY, 0.0, 1.0);
    const FarPoint  = TransformPoint(Inverse, NdcX, NdcY, 1.0, 1.0);

    if (Math.abs(NearPoint[3]) < 1e-12 || Math.abs(FarPoint[3]) < 1e-12) { return null; }

    const Origin = [NearPoint[0] / NearPoint[3], NearPoint[1] / NearPoint[3], NearPoint[2] / NearPoint[3]];
    const Tail   = [FarPoint[0]  / FarPoint[3],  FarPoint[1]  / FarPoint[3],  FarPoint[2]  / FarPoint[3]];

    const Span   = [Tail[0] - Origin[0], Tail[1] - Origin[1], Tail[2] - Origin[2]];
    const Length = Math.hypot(Span[0], Span[1], Span[2]);
    if (Length < 1e-12) { return null; }

    return {
        Origin,
        Direction: [Span[0] / Length, Span[1] / Length, Span[2] / Length]
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERSECTION
//------------------------------------------------------------------------------------------------------------------------

// Moller-Trumbore, two-sided.
//
// 📝 Two-sided deliberately. Suzanne's eye shells are open surfaces whose winding faces away from
//    most viewpoints; a one-sided test makes them unpaintable from outside while they still draw.
//    Returns the ray parameter and barycentric weights, or null.
export function IntersectTriangle(Origin, Direction, A, B, C)
{
    const EdgeAB = [B[0] - A[0], B[1] - A[1], B[2] - A[2]];
    const EdgeAC = [C[0] - A[0], C[1] - A[1], C[2] - A[2]];

    const Normal = [
        Direction[1] * EdgeAC[2] - Direction[2] * EdgeAC[1],
        Direction[2] * EdgeAC[0] - Direction[0] * EdgeAC[2],
        Direction[0] * EdgeAC[1] - Direction[1] * EdgeAC[0]
    ];

    const Determinant = EdgeAB[0] * Normal[0] + EdgeAB[1] * Normal[1] + EdgeAB[2] * Normal[2];
    if (Math.abs(Determinant) < ParallelEpsilon) { return null; }

    const Reciprocal = 1.0 / Determinant;
    const ToOrigin   = [Origin[0] - A[0], Origin[1] - A[1], Origin[2] - A[2]];

    const WeightB = (ToOrigin[0] * Normal[0] + ToOrigin[1] * Normal[1] + ToOrigin[2] * Normal[2]) * Reciprocal;
    if (WeightB < 0.0 || WeightB > 1.0) { return null; }

    const Cross = [
        ToOrigin[1] * EdgeAB[2] - ToOrigin[2] * EdgeAB[1],
        ToOrigin[2] * EdgeAB[0] - ToOrigin[0] * EdgeAB[2],
        ToOrigin[0] * EdgeAB[1] - ToOrigin[1] * EdgeAB[0]
    ];

    const WeightC = (Direction[0] * Cross[0] + Direction[1] * Cross[1] + Direction[2] * Cross[2]) * Reciprocal;
    if (WeightC < 0.0 || WeightB + WeightC > 1.0) { return null; }

    const Travel = (EdgeAC[0] * Cross[0] + EdgeAC[1] * Cross[1] + EdgeAC[2] * Cross[2]) * Reciprocal;
    if (Travel < MinimumTravel) { return null; }

    return { Travel, WeightA: 1.0 - WeightB - WeightC, WeightB, WeightC };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PUBLIC SURFACE
//------------------------------------------------------------------------------------------------------------------------

// Frontmost intersection of a ray with the decoded surface.
//
// 📝 Linear over every triangle. Suzanne is ~968 triangles, so a brute-force sweep costs microseconds
//    and stays exact; an acceleration structure is deferred until a production-scale mesh needs it.
//    Recorded in the backlog rather than pre-built here.
export function PickSurface(Decoded, Origin, Direction)
{
    const Index       = Decoded.Index;
    const Position    = Decoded.Position;
    const Coordinate  = Decoded.Coordinate;
    const Normal      = Decoded.Normal;
    const TriangleRun = Index.length / 3;

    let NearestTravel   = Infinity;
    let NearestTriangle = -1;
    let NearestWeight   = null;

    const A = [0, 0, 0];
    const B = [0, 0, 0];
    const C = [0, 0, 0];

    for (let Triangle = 0; Triangle < TriangleRun; Triangle += 1)
    {
        const IndexA = Index[Triangle * 3 + 0];
        const IndexB = Index[Triangle * 3 + 1];
        const IndexC = Index[Triangle * 3 + 2];

        A[0] = Position[IndexA * 3 + 0]; A[1] = Position[IndexA * 3 + 1]; A[2] = Position[IndexA * 3 + 2];
        B[0] = Position[IndexB * 3 + 0]; B[1] = Position[IndexB * 3 + 1]; B[2] = Position[IndexB * 3 + 2];
        C[0] = Position[IndexC * 3 + 0]; C[1] = Position[IndexC * 3 + 1]; C[2] = Position[IndexC * 3 + 2];

        const Hit = IntersectTriangle(Origin, Direction, A, B, C);
        if (Hit === null || Hit.Travel >= NearestTravel) { continue; }

        NearestTravel   = Hit.Travel;
        NearestTriangle = Triangle;
        NearestWeight   = Hit;
    }

    if (NearestTriangle < 0) { return null; }

    const IndexA = Index[NearestTriangle * 3 + 0];
    const IndexB = Index[NearestTriangle * 3 + 1];
    const IndexC = Index[NearestTriangle * 3 + 2];

    const { WeightA, WeightB, WeightC } = NearestWeight;

    const HitCoordinate = [
        Coordinate[IndexA * 2 + 0] * WeightA + Coordinate[IndexB * 2 + 0] * WeightB + Coordinate[IndexC * 2 + 0] * WeightC,
        Coordinate[IndexA * 2 + 1] * WeightA + Coordinate[IndexB * 2 + 1] * WeightB + Coordinate[IndexC * 2 + 1] * WeightC
    ];

    const HitNormalRaw = [
        Normal[IndexA * 3 + 0] * WeightA + Normal[IndexB * 3 + 0] * WeightB + Normal[IndexC * 3 + 0] * WeightC,
        Normal[IndexA * 3 + 1] * WeightA + Normal[IndexB * 3 + 1] * WeightB + Normal[IndexC * 3 + 1] * WeightC,
        Normal[IndexA * 3 + 2] * WeightA + Normal[IndexB * 3 + 2] * WeightB + Normal[IndexC * 3 + 2] * WeightC
    ];

    const NormalLength = Math.hypot(HitNormalRaw[0], HitNormalRaw[1], HitNormalRaw[2]);
    const HitNormal    = NormalLength > 1e-12
        ? [HitNormalRaw[0] / NormalLength, HitNormalRaw[1] / NormalLength, HitNormalRaw[2] / NormalLength]
        : [0, 1, 0];

    return {
        Triangle:   NearestTriangle,
        Travel:     NearestTravel,
        // Object-space hit point. 📝 The stroke record stores this, never device pixels — replay at a
        //    different resolution or camera depends on it being a physical metric.
        Position: [
            Origin[0] + Direction[0] * NearestTravel,
            Origin[1] + Direction[1] * NearestTravel,
            Origin[2] + Direction[2] * NearestTravel
        ],
        Coordinate: HitCoordinate,
        Normal:     HitNormal,
        WeightA, WeightB, WeightC
    };
}

// The UV the surface actually carries at an object-space point, found by closest point over triangles.
//
// 🔴 Exists because a UV coordinate cannot be interpolated between two pointer samples. Position and
//    normal are continuous fields, so blending them between samples approximates the surface well. UV
//    is piecewise per island and jumps at every seam, so a blend across one names a texel on NEITHER
//    island: a stroke crossing Suzanne's ear onto her face recorded v marching 0.87 -> 0.76 while the
//    true v there was 0.21. Snapping to the nearer endpoint is no better — consecutive pointer samples
//    span many triangles, so mid-segment dabs are then further from truth than the mix was (measured:
//    117 wrong versus 96). The only correct source for the value is the mesh.
//
// 📝 This does not move any paint. PaintPass rasterizes UV from the mesh and tests purely in object
//    space, so Coordinate is metadata — diagnostics now, atlas-tile culling later. It has to be true
//    because anything trusting it is otherwise reading a coordinate the surface never had.
export function ResolveCoordinate(Decoded, Point)
{
    const Index      = Decoded.Index;
    const Position   = Decoded.Position;
    const Coordinate = Decoded.Coordinate;

    let NearestDistance = Infinity;
    let NearestUv       = null;

    for (let Triangle = 0; Triangle < Index.length / 3; Triangle += 1)
    {
        const IndexA = Index[Triangle * 3 + 0];
        const IndexB = Index[Triangle * 3 + 1];
        const IndexC = Index[Triangle * 3 + 2];

        const Ax = Position[IndexA * 3], Ay = Position[IndexA * 3 + 1], Az = Position[IndexA * 3 + 2];
        const Bx = Position[IndexB * 3], By = Position[IndexB * 3 + 1], Bz = Position[IndexB * 3 + 2];
        const Cx = Position[IndexC * 3], Cy = Position[IndexC * 3 + 1], Cz = Position[IndexC * 3 + 2];

        // Barycentric of the point projected onto the triangle's plane, clamped into the triangle.
        const E0x = Bx - Ax, E0y = By - Ay, E0z = Bz - Az;
        const E1x = Cx - Ax, E1y = Cy - Ay, E1z = Cz - Az;
        const Dx  = Point[0] - Ax, Dy = Point[1] - Ay, Dz = Point[2] - Az;

        const D00 = E0x*E0x + E0y*E0y + E0z*E0z;
        const D01 = E0x*E1x + E0y*E1y + E0z*E1z;
        const D11 = E1x*E1x + E1y*E1y + E1z*E1z;
        const D20 = Dx*E0x  + Dy*E0y  + Dz*E0z;
        const D21 = Dx*E1x  + Dy*E1y  + Dz*E1z;

        const Determinant = D00 * D11 - D01 * D01;
        if (Math.abs(Determinant) < 1e-20) { continue; }

        let WeightB = (D11 * D20 - D01 * D21) / Determinant;
        let WeightC = (D00 * D21 - D01 * D20) / Determinant;

        // Clamp into the triangle so a point slightly off the surface still resolves to a real UV.
        WeightB = Math.min(Math.max(WeightB, 0), 1);
        WeightC = Math.min(Math.max(WeightC, 0), 1);
        if (WeightB + WeightC > 1)
        {
            const Excess = WeightB + WeightC;
            WeightB /= Excess;
            WeightC /= Excess;
        }
        const WeightA = 1 - WeightB - WeightC;

        const Sx = Ax + E0x * WeightB + E1x * WeightC;
        const Sy = Ay + E0y * WeightB + E1y * WeightC;
        const Sz = Az + E0z * WeightB + E1z * WeightC;

        const Distance = (Sx - Point[0]) ** 2 + (Sy - Point[1]) ** 2 + (Sz - Point[2]) ** 2;
        if (Distance >= NearestDistance) { continue; }

        NearestDistance = Distance;
        NearestUv = [
            Coordinate[IndexA*2]     * WeightA + Coordinate[IndexB*2]     * WeightB + Coordinate[IndexC*2]     * WeightC,
            Coordinate[IndexA*2 + 1] * WeightA + Coordinate[IndexB*2 + 1] * WeightB + Coordinate[IndexC*2 + 1] * WeightC
        ];
    }

    return NearestUv;
}

// Convenience: pointer pixels straight to a surface hit.
export function PickFromPointer(Decoded, Resolved, PointerX, PointerY, SurfaceWidth, SurfaceHeight)
{
    const Ray = BuildPointerRay(Resolved, PointerX, PointerY, SurfaceWidth, SurfaceHeight);
    if (Ray === null) { return null; }
    return PickSurface(Decoded, Ray.Origin, Ray.Direction);
}
