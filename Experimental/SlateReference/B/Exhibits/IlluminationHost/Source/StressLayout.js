//============================================================================================================================================
//                                                              STRESSLAYOUT.JS
//============================================================================================================================================
// 🧩 The stress-test pool as data: four shapes, their palette, and a reproducible scatter over the pad that keeps the course clear.

// 📝 Every slot of the pool has a fixed shape, colour and place, derived from its index alone, so "+256 objects" puts
//    the same 256 objects in the same places on every run and on every machine — frame times are comparable between
//    runs. Slots are shaped in runs of sixteen (crate, pillar, drum, ball) so consecutive active slots batch into a
//    handful of instanced draws, and placed through a seeded shuffle so any prefix of the pool spreads over the pad.

import { EmitBoxFacets } from "./CourseStructure.js";
import { LevelClearZones } from "./LevelStructure.js";
import { CourseMaterial } from "./MaterialSpecification.js";

export const StressShapeRun = 16;   // [slot] - consecutive slots sharing one shape

// 📝 DistanceVoxel [m] sizes each shape's distance volume; CardDensity [texel/m] is kept low so a full pool fits the
//    card texel space without lowering the course's own card density (eight-texel faces at the high grade).
export const StressShapes = Object.freeze([
    { Name: "Stress crate",  Emit: () => EmitBoxFacets(1.1, 1.1, 1.1, CourseMaterial.StressIvory), DistanceVoxel: 0.05, CardDensity: 7.0 },
    { Name: "Stress pillar", Emit: () => EmitBoxFacets(0.7, 0.7, 2.4, CourseMaterial.StressIvory), DistanceVoxel: 0.05, CardDensity: 6.0 },
    { Name: "Stress drum",   Emit: () => EmitDrumFacets(0.45, 1.05, 18, CourseMaterial.StressIvory), DistanceVoxel: 0.05, CardDensity: 7.0 },
    { Name: "Stress ball",   Emit: () => EmitBallFacets(0.55, 2, CourseMaterial.StressIvory), DistanceVoxel: 0.05, CardDensity: 7.0 }
]);

const Palette = [CourseMaterial.StressAmber, CourseMaterial.StressTeal, CourseMaterial.StressViolet, CourseMaterial.StressIvory,
                 CourseMaterial.WallCrimson, CourseMaterial.WallCobalt];

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHAPE FACETS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A closed cylinder standing on its base centre, outward counter-clockwise; smooth radial normals on the side.
export function EmitDrumFacets(Radius, Height, Segments, Family)
{
    const Facets = [];
    for (let Segment = 0; Segment < Segments; ++Segment)
    {
        const A0 = (2.0 * Math.PI * Segment) / Segments;
        const A1 = (2.0 * Math.PI * (Segment + 1)) / Segments;
        const N0 = [Math.cos(A0), Math.sin(A0), 0.0], N1 = [Math.cos(A1), Math.sin(A1), 0.0];
        const B0 = [Radius * N0[0], Radius * N0[1], 0.0], B1 = [Radius * N1[0], Radius * N1[1], 0.0];
        const T0 = [B0[0], B0[1], Height], T1 = [B1[0], B1[1], Height];
        Facets.push({ Corners: [B0, B1, T1], Normals: [N0, N1, N1], Family });
        Facets.push({ Corners: [B0, T1, T0], Normals: [N0, N1, N0], Family });
        Facets.push({ Corners: [[0.0, 0.0, Height], T0, T1], Normals: [[0, 0, 1], [0, 0, 1], [0, 0, 1]], Family });
        Facets.push({ Corners: [[0.0, 0.0, 0.0], B1, B0], Normals: [[0, 0, -1], [0, 0, -1], [0, 0, -1]], Family });
    }
    return Facets;
}

// 📝 An icosphere resting on the ground (centre at z = Radius), outward counter-clockwise, smooth normals.
export function EmitBallFacets(Radius, Subdivisions, Family)
{
    const Golden = (1.0 + Math.sqrt(5.0)) / 2.0;
    let Vertices = [[-1, Golden, 0], [1, Golden, 0], [-1, -Golden, 0], [1, -Golden, 0], [0, -1, Golden], [0, 1, Golden],
                    [0, -1, -Golden], [0, 1, -Golden], [Golden, 0, -1], [Golden, 0, 1], [-Golden, 0, -1], [-Golden, 0, 1]]
                   .map(Normalise);
    let Faces = [[0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11], [1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6], [7, 1, 8],
                 [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9], [4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1]];
    for (let Level = 0; Level < Subdivisions; ++Level)
    {
        const Midpoints = new Map();
        const Midpoint = (A, B) =>
        {
            const Key = A < B ? `${A},${B}` : `${B},${A}`;
            if (!Midpoints.has(Key))
            {
                Vertices.push(Normalise([(Vertices[A][0] + Vertices[B][0]) * 0.5, (Vertices[A][1] + Vertices[B][1]) * 0.5,
                                         (Vertices[A][2] + Vertices[B][2]) * 0.5]));
                Midpoints.set(Key, Vertices.length - 1);
            }
            return Midpoints.get(Key);
        };
        const Refined = [];
        for (const [A, B, C] of Faces)
        {
            const AB = Midpoint(A, B), BC = Midpoint(B, C), CA = Midpoint(C, A);
            Refined.push([A, AB, CA], [B, BC, AB], [C, CA, BC], [AB, BC, CA]);
        }
        Faces = Refined;
    }
    const Facets = [];
    for (let [A, B, C] of Faces)
    {
        // 📝 Guard the winding: the facet normal must point away from the centre, or the distance transfer's
        //    back-face sign heuristic would read the ball as inside-out.
        const Normal = Cross(Subtract(Vertices[B], Vertices[A]), Subtract(Vertices[C], Vertices[A]));
        const Centroid = [Vertices[A][0] + Vertices[B][0] + Vertices[C][0], Vertices[A][1] + Vertices[B][1] + Vertices[C][1],
                          Vertices[A][2] + Vertices[B][2] + Vertices[C][2]];
        if (Normal[0] * Centroid[0] + Normal[1] * Centroid[1] + Normal[2] * Centroid[2] < 0.0)
            [B, C] = [C, B];
        const Place = (Index) => [Vertices[Index][0] * Radius, Vertices[Index][1] * Radius, Vertices[Index][2] * Radius + Radius];
        Facets.push({ Corners: [Place(A), Place(B), Place(C)], Normals: [Vertices[A], Vertices[B], Vertices[C]], Family });
    }
    return Facets;
}

function Normalise(Vector)
{
    const Length = Math.hypot(Vector[0], Vector[1], Vector[2]);
    return [Vector[0] / Length, Vector[1] / Length, Vector[2] / Length];
}

function Subtract(A, B)
{
    return [A[0] - B[0], A[1] - B[1], A[2] - B[2]];
}

function Cross(A, B)
{
    return [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PLACEMENT
//------------------------------------------------------------------------------------------------------------------------

// 📝 Rectangles [MinimumX, MinimumY, MaximumX, MaximumY] the scatter must not touch: the driven lane (bumps, kerb, ramp,
//    slalom), the coloured walls and the corridor between them, the tunnel mouth and the spawn.
const ClearZones = LevelClearZones;

const PadReach    = 78.0;   // [m] - half extent of the scatter: the free pad between the level's features and the grass beyond
const CellSpacing = 2.45;   // [m] - jittered lattice pitch

// 📐 mulberry32: a small, fast, well-mixed 32-bit generator, seeded so the layout is identical everywhere.
function ConstructGenerator(Seed)
{
    let State = Seed >>> 0;
    return () =>
    {
        State = (State + 0x6D2B79F5) >>> 0;
        let Mixed = State;
        Mixed = Math.imul(Mixed ^ (Mixed >>> 15), Mixed | 1);
        Mixed ^= Mixed + Math.imul(Mixed ^ (Mixed >>> 7), Mixed | 61);
        return ((Mixed ^ (Mixed >>> 14)) >>> 0) / 4294967296;
    };
}

// 📝 Returns Capacity placements { Position [x, y], Yaw [rad], Shape, Family, Phase, Lamp }. Should the clear lattice
//    run out (a pool larger than the pad holds), the remaining slots stack onto the crates already placed.
export function ResolveStressPlacements(Capacity)
{
    const Random = ConstructGenerator(0x5EED1234);
    const Cells = [];
    const Steps = Math.floor((2.0 * PadReach) / CellSpacing);
    for (let Row = 0; Row <= Steps; ++Row)
    {
        for (let Column = 0; Column <= Steps; ++Column)
        {
            const X = -PadReach + Row * CellSpacing + (Random() - 0.5) * 0.7;
            const Y = -PadReach + Column * CellSpacing + (Random() - 0.5) * 0.7;
            const Blocked = ClearZones.some(([MinimumX, MinimumY, MaximumX, MaximumY]) =>
                X > MinimumX - 1.4 && X < MaximumX + 1.4 && Y > MinimumY - 1.4 && Y < MaximumY + 1.4);
            if (!Blocked)
                Cells.push([X, Y]);
        }
    }
    for (let Index = Cells.length - 1; Index > 0; --Index)
    {
        const Other = Math.floor(Random() * (Index + 1));
        [Cells[Index], Cells[Other]] = [Cells[Other], Cells[Index]];
    }
    const Placements = [];
    for (let Slot = 0; Slot < Capacity; ++Slot)
    {
        const Shape = Math.floor(Slot / StressShapeRun) % StressShapes.length;
        const Cell = Cells[Slot % Cells.length];
        const Layer = Math.floor(Slot / Cells.length);
        const Lamp = (Slot % 11) === 5;
        Placements.push({
            Position: [Cell[0], Cell[1], Layer * 1.1],
            Yaw:      Random() * 2.0 * Math.PI,
            Shape,
            Family:   Lamp ? CourseMaterial.StressLamp : Palette[Math.floor(Random() * Palette.length)],
            Phase:    Random() * 2.0 * Math.PI,
            Lamp
        });
    }
    return Placements;
}
