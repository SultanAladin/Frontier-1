//============================================================================================================================================
//                                                           FRACTURESTRUCTURE.JS
//============================================================================================================================================
// 🧩 Material-biased, energy-limited partitioning of a convex solid into closed convex fragments — the Frontier Fracture Editor's.

// 📝 A port of Experimental/FractureEditor/FractureStructure.js (Frontier-, the Fracture Editor) without three.js:
//    fragment area is bought with energy at 2·Gc per square metre. The budget — the impact's share plus the residual
//    elastic energy a tempered pane stores — is spent one plane cut at a time on the piece that is largest and nearest
//    the impact; each cut takes the best of eight candidate planes and refuses slivers, needles, lopsided halves and
//    pieces below the minimum span. Wood cuts run along the grain and pay more across it; a thin pane cracks radially
//    round the hit. Every fragment is closed and convex and their volumes sum to the solid's to 1e-7, the same
//    acceptance the editor's geometry receipt reports.

import { CourseMaterial } from "./MaterialSpecification.js";

// 📝 Gc, density and stored energy are FractureEditor's materials.ts (real-world orders of magnitude). Threshold is the
//    exhibit's: the impact energy a prop must receive before it gives way rather than stopping the car.
export const FractureMaterials = Object.freeze({
    concrete: { Label: "Concrete",       Density: 2350, Gc: 140, StoredEnergy: 0,     Restitution: 0.12, Friction: 0.80, Threshold: 32000,
                Skin: CourseMaterial.Concrete, Interior: CourseMaterial.ConcreteInterior, Colour: "#9d9d97", InteriorColour: "#8d8b84",
                Note: "Bulk-brittle. Chunky blocks; a fast hit pulverises the face it lands on." },
    rock:     { Label: "Stone",          Density: 2700, Gc: 95,  StoredEnergy: 0,     Restitution: 0.10, Friction: 0.90, Threshold: 45000,
                Skin: CourseMaterial.Stone, Interior: CourseMaterial.StoneInterior, Colour: "#7c7b78", InteriorColour: "#93897f",
                Note: "Bedded granite: heavy, few large slabs." },
    wood:     { Label: "Wood",           Density: 520,  Gc: 320, StoredEnergy: 0,     Restitution: 0.20, Friction: 0.55, Threshold: 3500,
                Skin: CourseMaterial.Wood, Interior: CourseMaterial.WoodInterior, Colour: "#b5854a", InteriorColour: "#d8b57d",
                Grain: 0, Note: "Fibrous. Splits along the grain into long splinters; cross-grain cuts cost eleven times more." },
    glass:    { Label: "Glass",          Density: 2500, Gc: 7,   StoredEnergy: 0,     Restitution: 0.12, Friction: 0.28, Threshold: 500,
                Skin: CourseMaterial.Glass, Interior: CourseMaterial.GlassEdge, Colour: "#cfe6ea", InteriorColour: "#bcd8de",
                Note: "Annealed, plate-brittle. Radial shards round the impact, large ones away from it." },
    tempered: { Label: "Tempered glass", Density: 2500, Gc: 7,   StoredEnergy: 46000, Restitution: 0.10, Friction: 0.35, Threshold: 1500,
                Skin: CourseMaterial.Tempered, Interior: CourseMaterial.TemperedEdge, Colour: "#cfe6ea", InteriorColour: "#c8e2e8",
                Note: "Its own stored stress dices the whole pane into small cubes, wherever it is struck." },
    plastic:  { Label: "ABS plastic",    Density: 1050, Gc: 460, StoredEnergy: 0,     Restitution: 0.35, Friction: 0.45, Threshold: 7000,
                Skin: CourseMaterial.Plastic, Interior: CourseMaterial.PlasticInterior, Colour: "#d8552f", InteriorColour: "#f0b9a4",
                Note: "Ductile. Tears into a few large pieces and bounces." }
});

export const FractureMaterialOrder = ["concrete", "rock", "wood", "glass", "tempered", "plastic"];

// 📝 A fragment's rendered faces may not exceed this many triangles on average; the composite facet set reserves room
//    for Capacity × this. A fragment of a box cut by planes rarely passes 30.
export const FragmentTriangleBudget = 44;

//------------------------------------------------------------------------------------------------------------------------
//                                                   VECTOR HELPERS
//------------------------------------------------------------------------------------------------------------------------

const Subtract = (A, B) => [A[0] - B[0], A[1] - B[1], A[2] - B[2]];
const Add      = (A, B) => [A[0] + B[0], A[1] + B[1], A[2] + B[2]];
const Scale    = (A, S) => [A[0] * S, A[1] * S, A[2] * S];
const Dot      = (A, B) => A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
const Cross    = (A, B) => [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]];
const Length   = (A) => Math.hypot(A[0], A[1], A[2]);
const Normalise = (A) => { const L = Length(A) || 1.0; return [A[0] / L, A[1] / L, A[2] / L]; };

// 📝 The Fracture Editor's linear congruential stream, so a seed reproduces a pattern exactly.
export function ConstructRandom(Seed)
{
    let Accumulator = Seed >>> 0;
    return () =>
    {
        Accumulator = (Math.imul(1664525, Accumulator) + 1013904223) >>> 0;
        return Accumulator / 4294967296;
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SOLIDS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A part: { Vertices: [[x, y, z]…], Faces: [{ Indices: […], Interior }] }, convex, closed, faces wound
//    counter-clockwise seen from outside. Solids are centred on the origin.
export function ConstructBox(ExtentX, ExtentY, ExtentZ)
{
    const X = 0.5 * ExtentX, Y = 0.5 * ExtentY, Z = 0.5 * ExtentZ;
    const Vertices = [[-X, -Y, -Z], [X, -Y, -Z], [X, Y, -Z], [-X, Y, -Z], [-X, -Y, Z], [X, -Y, Z], [X, Y, Z], [-X, Y, Z]];
    const Faces = [[4, 5, 6, 7], [3, 2, 1, 0], [1, 2, 6, 5], [3, 0, 4, 7], [2, 3, 7, 6], [0, 1, 5, 4]]
        .map((Indices) => ({ Indices, Interior: false }));
    return { Vertices, Faces };
}

// 📝 A vertical prism of Sides facets (pillars, drums), axis along z.
export function ConstructPrism(Radius, Height, Sides)
{
    const Vertices = [];
    for (let Ring = 0; Ring < 2; ++Ring)
        for (let Side = 0; Side < Sides; ++Side)
        {
            const Angle = 2.0 * Math.PI * (Side + 0.5) / Sides;
            Vertices.push([Radius * Math.cos(Angle), Radius * Math.sin(Angle), (Ring - 0.5) * Height]);
        }
    const Faces = [];
    Faces.push({ Indices: Array.from({ length: Sides }, (_, Side) => Sides + Side), Interior: false });
    Faces.push({ Indices: Array.from({ length: Sides }, (_, Side) => Sides - 1 - Side), Interior: false });
    for (let Side = 0; Side < Sides; ++Side)
    {
        const Next = (Side + 1) % Sides;
        Faces.push({ Indices: [Side, Next, Sides + Next, Sides + Side], Interior: false });
    }
    return { Vertices, Faces };
}

export function ConstructSolid(Shape)
{
    return Shape.Kind === "prism" ? ConstructPrism(Shape.Radius, Shape.Height, Shape.Sides || 12)
                                  : ConstructBox(Shape.Extent[0], Shape.Extent[1], Shape.Extent[2]);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   MEASURES
//------------------------------------------------------------------------------------------------------------------------

export function MeasureBounds(Part)
{
    const Minimum = [Infinity, Infinity, Infinity], Maximum = [-Infinity, -Infinity, -Infinity];
    for (const Point of Part.Vertices)
        for (let Axis = 0; Axis < 3; ++Axis)
        {
            Minimum[Axis] = Math.min(Minimum[Axis], Point[Axis]);
            Maximum[Axis] = Math.max(Maximum[Axis], Point[Axis]);
        }
    return { Minimum, Maximum, Size: Subtract(Maximum, Minimum), Centre: Scale(Add(Minimum, Maximum), 0.5) };
}

// 📐 Newell's method: robust for any planar polygon.
export function MeasureFaceNormal(Part, Face)
{
    const Normal = [0.0, 0.0, 0.0];
    const Count = Face.Indices.length;
    for (let Slot = 0; Slot < Count; ++Slot)
    {
        const A = Part.Vertices[Face.Indices[Slot]], B = Part.Vertices[Face.Indices[(Slot + 1) % Count]];
        Normal[0] += (A[1] - B[1]) * (A[2] + B[2]);
        Normal[1] += (A[2] - B[2]) * (A[0] + B[0]);
        Normal[2] += (A[0] - B[0]) * (A[1] + B[1]);
    }
    return Normalise(Normal);
}

// 📐 Volume and centroid together: signed tetrahedra from the first vertex, accumulated with their centroids.
export function MeasureMass(Part)
{
    const Origin = Part.Vertices[0];
    let Volume = 0.0;
    const Moment = [0.0, 0.0, 0.0];
    for (const Face of Part.Faces)
        for (let Slot = 1; Slot + 1 < Face.Indices.length; ++Slot)
        {
            const A = Subtract(Part.Vertices[Face.Indices[0]], Origin);
            const B = Subtract(Part.Vertices[Face.Indices[Slot]], Origin);
            const C = Subtract(Part.Vertices[Face.Indices[Slot + 1]], Origin);
            const Tetra = Dot(A, Cross(B, C)) / 6.0;
            Volume += Tetra;
            const Centre = Scale(Add(Add(A, B), C), 0.25);
            Moment[0] += Centre[0] * Tetra;
            Moment[1] += Centre[1] * Tetra;
            Moment[2] += Centre[2] * Tetra;
        }
    const Centroid = Volume > 0.0 ? Add(Origin, Scale(Moment, 1.0 / Volume)) : MeasureBounds(Part).Centre;
    return { Volume, Centroid };
}

// 📐 Triangle quality 4√3·A / Σℓ²: 1 for an equilateral triangle, 0 for a degenerate one (scalar arithmetic: this is
//    the innermost loop of every fracture).
function MeasureTriangleQuality(A, B, C)
{
    const Ux = B[0] - A[0], Uy = B[1] - A[1], Uz = B[2] - A[2];
    const Vx = C[0] - A[0], Vy = C[1] - A[1], Vz = C[2] - A[2];
    const Wx = C[0] - B[0], Wy = C[1] - B[1], Wz = C[2] - B[2];
    const Nx = Uy * Vz - Uz * Vy, Ny = Uz * Vx - Ux * Vz, Nz = Ux * Vy - Uy * Vx;
    const Sum = Ux * Ux + Uy * Uy + Uz * Uz + Vx * Vx + Vy * Vy + Vz * Vz + Wx * Wx + Wy * Wy + Wz * Wz;
    return Sum > 0.0 ? 3.4641016151377544 * Math.sqrt(Nx * Nx + Ny * Ny + Nz * Nz) / Sum : 0.0;
}

const TriangulationScore = new Float64Array(64 * 64);   // [-] scratch, row-major Begin · 64 + End
const TriangulationPivot = new Int32Array(64 * 64);

// 📝 Maximise the weakest triangle rather than fanning from one corner (the editor's quality-aware triangulation):
//    dynamic programming over the convex polygon's diagonals, O(n³) for the handful of corners a fragment face has;
//    triangles and quads, most of them, are answered directly.
export function TriangulateFace(Part, Face)
{
    const Indices = Face.Indices;
    const Count = Indices.length;
    const V = Part.Vertices;
    if (Count === 3)
        return { Triangles: [[0, 1, 2]], Quality: MeasureTriangleQuality(V[Indices[0]], V[Indices[1]], V[Indices[2]]) };
    if (Count === 4)
    {
        const P0 = V[Indices[0]], P1 = V[Indices[1]], P2 = V[Indices[2]], P3 = V[Indices[3]];
        const First = Math.min(MeasureTriangleQuality(P0, P1, P2), MeasureTriangleQuality(P0, P2, P3));
        const Second = Math.min(MeasureTriangleQuality(P0, P1, P3), MeasureTriangleQuality(P1, P2, P3));
        return First >= Second ? { Triangles: [[0, 1, 2], [0, 2, 3]], Quality: First }
                               : { Triangles: [[0, 1, 3], [1, 2, 3]], Quality: Second };
    }
    const Size = Math.min(Count, 64);
    const Score = TriangulationScore, Pivot = TriangulationPivot;
    for (let Begin = 0; Begin < Size; ++Begin)
        for (let End = 0; End < Size; ++End)
            Score[Begin * 64 + End] = -1.0;
    for (let Begin = 0; Begin + 1 < Size; ++Begin)
        Score[Begin * 64 + Begin + 1] = 2.0;
    for (let Span = 2; Span < Size; ++Span)
        for (let Begin = 0; Begin + Span < Size; ++Begin)
        {
            const End = Begin + Span;
            let Best = -1.0, BestMiddle = Begin + 1;
            for (let Middle = Begin + 1; Middle < End; ++Middle)
            {
                const Weakest = Math.min(Score[Begin * 64 + Middle], Score[Middle * 64 + End],
                                         MeasureTriangleQuality(V[Indices[Begin]], V[Indices[Middle]], V[Indices[End]]));
                if (Weakest > Best)
                {
                    Best = Weakest;
                    BestMiddle = Middle;
                }
            }
            Score[Begin * 64 + End] = Best;
            Pivot[Begin * 64 + End] = BestMiddle;
        }
    const Triangles = [];
    const Stack = [[0, Size - 1]];
    while (Stack.length > 0)
    {
        const [Begin, End] = Stack.pop();
        if (End - Begin < 2)
            continue;
        const Middle = Pivot[Begin * 64 + End];
        Triangles.push([Begin, Middle, End]);
        Stack.push([Begin, Middle], [Middle, End]);
    }
    return { Triangles, Quality: Math.min(1.0, Score[Size - 1]) };
}

// 📝 Only the weakest triangle of every face: what a candidate half is judged on, without building triangle lists.
function MeasurePartQuality(Part)
{
    let Quality = 1.0, Triangles = 0;
    for (const Face of Part.Faces)
    {
        const Count = Face.Indices.length;
        Triangles += Count - 2;
        if (Count <= 4)
            Quality = Math.min(Quality, TriangulateFace(Part, Face).Quality);
        else
            Quality = Math.min(Quality, TriangulateFace(Part, Face).Quality);
    }
    return { Quality, Triangles };
}

// 📝 The slenderness the editor refuses past 6 (10 for wood): bounding diagonal over the narrowest face-normal width,
//    measured in the solid's own proportions so a long plank is not penalised for being a plank. For a thin pane the
//    thin axis is left out (ThinAxis ≥ 0): measured in its proportions a 4 cm pane made every shard look like a needle.
function MeasureAspect(Part, SourceSize, ThinAxis = -1)
{
    const Sx = ThinAxis === 0 ? 0.0 : 1.0 / SourceSize[0], Sy = ThinAxis === 1 ? 0.0 : 1.0 / SourceSize[1], Sz = ThinAxis === 2 ? 0.0 : 1.0 / SourceSize[2];
    let MinX = Infinity, MinY = Infinity, MinZ = Infinity, MaxX = -Infinity, MaxY = -Infinity, MaxZ = -Infinity;
    for (const Point of Part.Vertices)
    {
        const X = Point[0] * Sx, Y = Point[1] * Sy, Z = Point[2] * Sz;
        MinX = Math.min(MinX, X); MaxX = Math.max(MaxX, X);
        MinY = Math.min(MinY, Y); MaxY = Math.max(MaxY, Y);
        MinZ = Math.min(MinZ, Z); MaxZ = Math.max(MaxZ, Z);
    }
    let Width = Infinity;
    for (const Face of Part.Faces)
    {
        const Normal = MeasureFaceNormal(Part, Face);
        if (ThinAxis >= 0 && Math.abs(Normal[ThinAxis]) > 0.5)
            continue;
        // 📐 A plane's normal transforms by the inverse scale: n' ∝ n · SourceSize (zero on the thin axis).
        let Dx = Sx > 0.0 ? Normal[0] / Sx : 0.0, Dy = Sy > 0.0 ? Normal[1] / Sy : 0.0, Dz = Sz > 0.0 ? Normal[2] / Sz : 0.0;
        const L = Math.hypot(Dx, Dy, Dz) || 1.0;
        Dx /= L; Dy /= L; Dz /= L;
        let Low = Infinity, High = -Infinity;
        for (const Point of Part.Vertices)
        {
            const Projection = Dx * Point[0] * Sx + Dy * Point[1] * Sy + Dz * Point[2] * Sz;
            Low = Math.min(Low, Projection);
            High = Math.max(High, Projection);
        }
        Width = Math.min(Width, High - Low);
    }
    return Math.hypot(MaxX - MinX, MaxY - MinY, MaxZ - MinZ) / Math.max(Width, 1.0e-9);
}

// 📝 A light acceptance test per candidate half: finite, positive volume, and its weakest face triangle.
function ValidatePart(Part)
{
    for (const Point of Part.Vertices)
        if (!Number.isFinite(Point[0]) || !Number.isFinite(Point[1]) || !Number.isFinite(Point[2]))
            return null;
    const { Quality, Triangles } = MeasurePartQuality(Part);
    const Mass = MeasureMass(Part);
    const Size = Length(MeasureBounds(Part).Size);
    if (!(Mass.Volume > Size * Size * Size * 1.0e-13))
        return null;
    return { Quality, Triangles, Volume: Mass.Volume };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SPLIT
//------------------------------------------------------------------------------------------------------------------------

// 📝 Cuts a part by the plane Direction · p = Distance into [front (≥), back (≤)], each closed by a cap marked
//    Interior; null when the plane misses. Vertices within tolerance of the plane are snapped onto it.
export function SplitPart(Part, Direction, Distance)
{
    const Tolerance = Length(MeasureBounds(Part).Size) * 1.0e-9;
    const Points = Part.Vertices.map((Point) => Point.slice());
    const Distances = Points.map((Point) => Dot(Direction, Point) - Distance);
    if (!Distances.some((Offset) => Offset > Tolerance) || !Distances.some((Offset) => Offset < -Tolerance))
        return null;
    const Crossings = new Map();
    const Rim = new Set();
    const Sides = [[], []];
    Distances.forEach((Offset, Slot) =>
    {
        if (Math.abs(Offset) <= Tolerance)
        {
            Points[Slot] = Subtract(Points[Slot], Scale(Direction, Offset));
            Distances[Slot] = 0.0;
            Rim.add(Slot);
        }
    });
    for (const Face of Part.Faces)
    {
        const Polygons = [[], []];
        const Count = Face.Indices.length;
        for (let Slot = 0; Slot < Count; ++Slot)
        {
            const Start = Face.Indices[Slot], End = Face.Indices[(Slot + 1) % Count];
            const StartDistance = Distances[Start], EndDistance = Distances[End];
            if (StartDistance >= 0.0)
                Polygons[0].push(Start);
            if (StartDistance <= 0.0)
                Polygons[1].push(Start);
            if (StartDistance * EndDistance < 0.0)
            {
                const Key = Math.min(Start, End) + ":" + Math.max(Start, End);
                let Index = Crossings.get(Key);
                if (Index === undefined)
                {
                    Index = Points.length;
                    const T = StartDistance / (StartDistance - EndDistance);
                    let Point = Add(Points[Start], Scale(Subtract(Points[End], Points[Start]), T));
                    Point = Add(Point, Scale(Direction, Distance - Dot(Direction, Point)));
                    Points.push(Point);
                    Crossings.set(Key, Index);
                    Rim.add(Index);
                }
                Polygons[0].push(Index);
                Polygons[1].push(Index);
            }
        }
        Polygons.forEach((Indices, Side) =>
        {
            const Clean = Indices.filter((Candidate, Slot) => Candidate !== Indices[(Slot + Indices.length - 1) % Indices.length]);
            if (Clean.length >= 3)
                Sides[Side].push({ Indices: Clean, Interior: Face.Interior });
        });
    }
    if (Rim.size < 3)
        return null;
    const RimList = [...Rim];
    const Centre = Scale(RimList.reduce((Sum, Slot) => Add(Sum, Points[Slot]), [0.0, 0.0, 0.0]), 1.0 / RimList.length);
    const Tangent = Normalise(Cross(Math.abs(Direction[0]) < 0.8 ? [1.0, 0.0, 0.0] : [0.0, 1.0, 0.0], Direction));
    const Bitangent = Cross(Direction, Tangent);
    const Angle = (Slot) => { const Offset = Subtract(Points[Slot], Centre); return Math.atan2(Dot(Offset, Bitangent), Dot(Offset, Tangent)); };
    const Ordered = RimList.sort((First, Second) => Angle(First) - Angle(Second));
    Sides[0].push({ Indices: Ordered.slice().reverse(), Interior: true });
    Sides[1].push({ Indices: Ordered, Interior: true });
    return Sides.map((Faces) =>
    {
        const Used = [...new Set(Faces.flatMap((Face) => Face.Indices))];
        const Remap = new Map(Used.map((Index, Slot) => [Index, Slot]));
        return { Vertices: Used.map((Slot) => Points[Slot]), Faces: Faces.map((Face) => ({ Indices: Face.Indices.map((Slot) => Remap.get(Slot)), Interior: Face.Interior })) };
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   FRACTURE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Recipe: { Material, Energy [J] (the budget), Seed, Ceiling [pieces], MinimumSize [m], Impact: [x, y, z] (local) }.
//    Returns { Parts, Milliseconds, VolumeError, Quality, Triangles, Rejected, Spent, Budget }.
export function FractureSolid(Root, Recipe)
{
    const Started = performance.now();
    const Draw = ConstructRandom(Recipe.Seed);
    const Material = FractureMaterials[Recipe.Material] || FractureMaterials.concrete;
    const Original = MeasureMass(Root).Volume;
    const RootCheck = ValidatePart(Root);
    const QualityFloor = Math.min(0.018, (RootCheck ? RootCheck.Quality : 1.0) * 0.4);
    const Size = MeasureBounds(Root).Size;
    const Radius = Length(Size);
    const Hit = Recipe.Impact || [0.0, 0.0, 0.0];
    // 📝 A pane is thin along its smallest axis; cuts then stay perpendicular to it.
    const ThinAxis = Size.indexOf(Math.min(...Size));
    const Thin = Size[ThinAxis] < 0.1 * Math.min(...Size.filter((_, Axis) => Axis !== ThinAxis));
    const Grain = Material.Grain !== undefined ? Material.Grain : -1;
    const Budget = Recipe.Energy + Material.StoredEnergy * Original;
    let Energy = Budget, Rejected = 0, Spent = 0.0;
    let Parts = [Root];
    const Stopped = new Set();
    const Ceiling = Math.max(1, Math.round(Recipe.Ceiling));
    const Measured = new Map([[Root, MeasureMass(Root).Volume]]);
    const Centres = new Map([[Root, MeasureBounds(Root).Centre]]);
    while (Parts.length < Ceiling && Energy > 0.0)
    {
        let Best = -1, Priority = -1.0;
        Parts.forEach((Part, Slot) =>
        {
            if (Stopped.has(Part))
                return;
            // 📝 Stored stress (tempered glass) releases everywhere at once: the dicing ignores where the hit was.
            const Proximity = Material.StoredEnergy > 0.0 ? 1.0 : Math.exp(-Length(Subtract(Centres.get(Part), Hit)) / (Radius * 0.65));
            const Weighted = Measured.get(Part) * Proximity;
            if (Weighted > Priority)
            {
                Priority = Weighted;
                Best = Slot;
            }
        });
        if (Best < 0)
            break;
        const Part = Parts[Best];
        const Extent = MeasureBounds(Part);
        const Uncut = Measured.get(Part);
        let Choice = null;
        for (let Trial = 0; Trial < 8; ++Trial)
        {
            let Direction = [Draw() * 2.0 - 1.0, Draw() * 2.0 - 1.0, Draw() * 2.0 - 1.0];
            if (Thin)
                Direction[ThinAxis] = 0.0;
            if (Grain >= 0)
                Direction[Grain] *= 0.14;
            if (Trial === 7)
            {
                // 📝 The last trial cuts across the piece's longest relative axis.
                const Axes = [0, 1, 2].filter((Axis) => !(Thin && Axis === ThinAxis));
                const Weight = (Axis) => (Extent.Size[Axis] / Size[Axis]) * (Grain === Axis ? 0.15 : 1.0);
                Axes.sort((First, Second) => Weight(Second) - Weight(First));
                Direction = [0.0, 0.0, 0.0];
                Direction[Axes[0]] = 1.0;
            }
            if (Thin && (Recipe.Material === "glass"))
            {
                // 📝 Annealed glass: mostly radial cracks round the impact.
                const Radial = Subtract(Extent.Centre, Hit);
                Radial[ThinAxis] = 0.0;
                const Around = Normalise(Cross(Radial, ThinAxis === 0 ? [1, 0, 0] : ThinAxis === 1 ? [0, 1, 0] : [0, 0, 1]));
                Direction = Add(Scale(Around, 0.65), Scale(Normalise(Direction), 0.35));
                Direction[ThinAxis] = 0.0;
            }
            if (Dot(Direction, Direction) < 0.25 && Trial !== 7)
                continue;
            Direction = Normalise(Direction);
            let Low = Infinity, High = -Infinity;
            for (const Point of Part.Vertices)
            {
                const Projection = Dot(Direction, Point);
                Low = Math.min(Low, Projection);
                High = Math.max(High, Projection);
            }
            const Offset = Dot(Direction, Extent.Centre) + (Draw() - 0.5) * (High - Low) * 0.18;
            const Halves = SplitPart(Part, Direction, Offset);
            if (!Halves)
                continue;
            // 📝 Cheapest refusals first — volume split, minimum span, cost, slenderness — and the triangulation that
            //    measures face quality only for a candidate that survived them all.
            const Volumes = Halves.map((Half) => MeasureMass(Half).Volume);
            const Ratio = Math.min(...Volumes) / Uncut;
            if (!(Ratio >= 0.18) || Math.abs(Volumes[0] + Volumes[1] - Uncut) > Uncut * 1.0e-7)
            {
                Rejected++;
                continue;
            }
            const Spans = Halves.map((Half) => MeasureBounds(Half));
            const TooSmall = Spans.some((Span) => [0, 1, 2].some((Axis) => Span.Size[Axis] < Math.min(Recipe.MinimumSize, Size[Axis] * 0.3)));
            if (TooSmall)
            {
                Rejected++;
                continue;
            }
            // 📐 Cost = 2·Gc·(cap area), the cap being the last face of the front half.
            const Cap = Halves[0].Faces[Halves[0].Faces.length - 1];
            const CapOrigin = Halves[0].Vertices[Cap.Indices[0]];
            let Area = 0.0;
            for (let Slot = 1; Slot + 1 < Cap.Indices.length; ++Slot)
                Area += 0.5 * Length(Cross(Subtract(Halves[0].Vertices[Cap.Indices[Slot]], CapOrigin), Subtract(Halves[0].Vertices[Cap.Indices[Slot + 1]], CapOrigin)));
            const Cost = 2.0 * Material.Gc * Area * (Grain >= 0 ? 1.0 + Math.abs(Direction[Grain]) * 10.0 : 1.0);
            if (Cost > Energy)
                continue;
            const Slenderness = Math.max(...Halves.map((Half) => MeasureAspect(Half, Size, Thin ? ThinAxis : -1)));
            if (Slenderness > (Grain >= 0 ? 10.0 : 6.0))
            {
                Rejected++;
                continue;
            }
            const Checks = Halves.map(ValidatePart);
            if (Checks.some((Check) => !Check || Check.Quality < QualityFloor))
            {
                Rejected++;
                continue;
            }
            const Score = Math.sqrt(Math.min(...Checks.map((Check) => Check.Quality)) / Slenderness) * Math.min(...Volumes) / Math.max(...Volumes);
            if (!Choice || Score > Choice.Score)
                Choice = { Halves, Volumes, Spans, Cost, Score };
        }
        if (!Choice)
        {
            Stopped.add(Part);
            continue;
        }
        Energy -= Choice.Cost;
        Spent += Choice.Cost;
        Measured.set(Choice.Halves[0], Choice.Volumes[0]);
        Measured.set(Choice.Halves[1], Choice.Volumes[1]);
        Centres.set(Choice.Halves[0], Choice.Spans[0].Centre);
        Centres.set(Choice.Halves[1], Choice.Spans[1].Centre);
        Parts.splice(Best, 1, ...Choice.Halves);
    }
    const Checks = Parts.map(ValidatePart);
    const Total = Checks.reduce((Sum, Check) => Sum + (Check ? Check.Volume : 0.0), 0.0);
    return {
        Parts,
        Milliseconds: performance.now() - Started,
        VolumeError: Math.abs(Total - Original) / Original,
        Quality: Math.min(...Checks.map((Check) => (Check ? Check.Quality : 0.0))),
        Triangles: Checks.reduce((Sum, Check) => Sum + (Check ? Check.Triangles : 0), 0),
        Rejected,
        Spent,
        Budget
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 FRAGMENT RECORDS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Each part becomes what the runtime needs: its centroid in the solid's frame, its triangles relative to that
//    centroid (skin or interior family), hull points for contact, mass, a box inertia and a bounding radius.
export function ResolveFragments(Result, MaterialKey, DensityScale = 1.0)
{
    const Material = FractureMaterials[MaterialKey] || FractureMaterials.concrete;
    return Result.Parts.map((Part) =>
    {
        const Mass = MeasureMass(Part);
        const Centroid = Mass.Centroid;
        const Points = Part.Vertices.map((Point) => Subtract(Point, Centroid));
        const Triangles = [];
        for (const Face of Part.Faces)
        {
            const Triangulated = TriangulateFace(Part, Face);
            const Family = Face.Interior ? Material.Interior : Material.Skin;
            for (const [A, B, C] of Triangulated.Triangles)
                Triangles.push({ Corners: [Points[Face.Indices[A]], Points[Face.Indices[B]], Points[Face.Indices[C]]], Family });
        }
        const Minimum = [Infinity, Infinity, Infinity], Maximum = [-Infinity, -Infinity, -Infinity];
        let Radius = 0.0;
        for (const Point of Points)
        {
            Radius = Math.max(Radius, Length(Point));
            for (let Axis = 0; Axis < 3; ++Axis)
            {
                Minimum[Axis] = Math.min(Minimum[Axis], Point[Axis]);
                Maximum[Axis] = Math.max(Maximum[Axis], Point[Axis]);
            }
        }
        const Size = Subtract(Maximum, Minimum);
        const Kilograms = Math.max(0.05, Mass.Volume * Material.Density * DensityScale);
        // 📝 Face planes [n, d] about the centroid, n the Newell normal of each (outward, counter-clockwise) face: the
        //    hull the rigid-body world collides against and the fragment distance volume is computed from.
        const Planes = [];
        for (const Face of Part.Faces)
        {
            let NX = 0.0, NY = 0.0, NZ = 0.0;
            const Indices = Face.Indices;
            for (let Index = 0; Index < Indices.length; ++Index)
            {
                const Current = Points[Indices[Index]], Next = Points[Indices[(Index + 1) % Indices.length]];
                NX += (Current[1] - Next[1]) * (Current[2] + Next[2]);
                NY += (Current[2] - Next[2]) * (Current[0] + Next[0]);
                NZ += (Current[0] - Next[0]) * (Current[1] + Next[1]);
            }
            const Magnitude = Math.hypot(NX, NY, NZ);
            if (Magnitude < 1.0e-12)
                continue;
            const Normal = [NX / Magnitude, NY / Magnitude, NZ / Magnitude];
            const First = Points[Indices[0]];
            Planes.push([Normal[0], Normal[1], Normal[2], Normal[0] * First[0] + Normal[1] * First[1] + Normal[2] * First[2]]);
        }
        // 📐 Box inertia of the fragment's bounds, a fair stand-in for a convex chunk: I = m/12 (b² + c²) per axis.
        const Inertia = [Kilograms / 12.0 * (Size[1] * Size[1] + Size[2] * Size[2]),
                         Kilograms / 12.0 * (Size[0] * Size[0] + Size[2] * Size[2]),
                         Kilograms / 12.0 * (Size[0] * Size[0] + Size[1] * Size[1])];
        return { Centroid, Triangles, Points, Planes, Volume: Mass.Volume, Mass: Kilograms, Inertia, Radius, Size, Minimum, Maximum };
    });
}
