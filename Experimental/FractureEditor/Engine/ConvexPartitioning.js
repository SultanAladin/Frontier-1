/*==========================================================================================================================================
                                                       CONVEXPARTITIONING.JS
==========================================================================================================================================*/
// 🧩 Exact 3D convex polyhedron half-space partitioning, two-tier fracture solvers (Runtime Flat-BSP + Offline Engineering Continuum),
//    bond adjacency, and Piece SDF evaluation.

import { ExecuteFastRuntimeFracture } from "./FastRuntimeFracture.js";
import { ExecuteEngineeringBakedFracture } from "./EngineeringBakedFracture.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                    VECTOR ALGEBRA
//------------------------------------------------------------------------------------------------------------------------

export const Vec3 = (x = 0, y = 0, z = 0) => [x, y, z];
export const Add3 = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
export const Sub3 = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
export const Mul3 = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
export const ScaleVec3 = (a, b) => [a[0] * b[0], a[1] * b[1], a[2] * b[2]];
export const Lerp3 = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
export const Dot3 = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
export const Cross3 = (a, b) => [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0]
];
export const LenSq3 = (a) => a[0] * a[0] + a[1] * a[1] + a[2] * a[2];
export const Len3 = (a) => Math.hypot(a[0], a[1], a[2]);
export const Dist3 = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
export const Norm3 = (a) =>
{
    const l = Math.hypot(a[0], a[1], a[2]);
    return l > 1e-12 ? [a[0] / l, a[1] / l, a[2] / l] : [0, 1, 0];
};
export const Perp3 = (n) =>
{
    const ref = Math.abs(n[1]) < 0.85 ? [0, 1, 0] : [1, 0, 0];
    return Norm3(Cross3(n, ref));
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 DETERMINISTIC STREAM
//------------------------------------------------------------------------------------------------------------------------

export function ConstructRandomStream(Seed)
{
    let State = (Math.imul(Number(Seed) || 42, 2654435761) >>> 0) || 1;
    return () =>
    {
        State = (State + 0x6D2B79F5) >>> 0;
        let t = State;
        t = Math.imul(t ^ (t >>> 15), t | 1);
        t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  MATERIAL CATALOGUE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Physical SI constants: ρ [kg/m³], Gc [J/m²], E [GPa], ν [-], σT [MPa], m (Weibull), U_res [J/m³].
export const MaterialCatalogue = Object.freeze({
    concrete: {
        Identifier: "concrete",
        Label: "Reinforced Concrete",
        ShortLabel: "Concrete",
        FractureCategory: "bulk-brittle",
        Density: 2350,
        Gc: 140,
        YoungModulusGPa: 30.0,
        PoissonRatio: 0.20,
        TensileStrengthMPa: 3.8,
        WeibullModulus: 8.0,
        StoredEnergy: 0,
        HertzConeRad: 0.95,
        AnisotropyRatio: 1.15,
        GrainAxis: [0, 1, 0],
        BeddingWeakness: 0.12,
        RingBias: 0.24,
        CrackJitter: 0.32,
        Restitution: 0.12,
        Friction: 0.80,
        DustYield: 2.2,
        ExteriorHex: "#8d908b",
        InteriorHex: "#6f716b",
        Roughness: 0.92,
        Metalness: 0.02,
        Transmission: 0.0
    },
    granite: {
        Identifier: "granite",
        Label: "Bedded Granite",
        ShortLabel: "Granite",
        FractureCategory: "bulk-brittle",
        Density: 2700,
        Gc: 95,
        YoungModulusGPa: 55.0,
        PoissonRatio: 0.25,
        TensileStrengthMPa: 12.5,
        WeibullModulus: 9.0,
        StoredEnergy: 0,
        HertzConeRad: 1.02,
        AnisotropyRatio: 1.4,
        GrainAxis: [0, 1, 0],
        BeddingWeakness: 0.55,
        RingBias: 0.18,
        CrackJitter: 0.28,
        Restitution: 0.10,
        Friction: 0.90,
        DustYield: 1.6,
        ExteriorHex: "#6f7378",
        InteriorHex: "#867f78",
        Roughness: 0.88,
        Metalness: 0.05,
        Transmission: 0.0
    },
    wood: {
        Identifier: "wood",
        Label: "Orthotropic Spruce",
        ShortLabel: "Spruce Wood",
        FractureCategory: "fibrous",
        Density: 520,
        Gc: 320,
        YoungModulusGPa: 11.0,
        PoissonRatio: 0.35,
        TensileStrengthMPa: 78.0,
        WeibullModulus: 6.0,
        StoredEnergy: 0,
        HertzConeRad: 0.0,
        AnisotropyRatio: 11.0,
        GrainAxis: [1, 0, 0],
        BeddingWeakness: 0.0,
        RingBias: 0.06,
        CrackJitter: 0.14,
        Restitution: 0.22,
        Friction: 0.58,
        DustYield: 0.25,
        ExteriorHex: "#b3824c",
        InteriorHex: "#d5b17a",
        Roughness: 0.76,
        Metalness: 0.0,
        Transmission: 0.0
    },
    glass: {
        Identifier: "glass",
        Label: "Annealed Float Glass",
        ShortLabel: "Float Glass",
        FractureCategory: "plate-brittle",
        Density: 2500,
        Gc: 7,
        YoungModulusGPa: 70.0,
        PoissonRatio: 0.22,
        TensileStrengthMPa: 45.0,
        WeibullModulus: 5.0,
        StoredEnergy: 0,
        HertzConeRad: 1.15,
        AnisotropyRatio: 1.0,
        GrainAxis: [0, 1, 0],
        BeddingWeakness: 0.0,
        RingBias: 0.44,
        CrackJitter: 0.18,
        Restitution: 0.16,
        Friction: 0.28,
        DustYield: 0.75,
        ExteriorHex: "#b8dce4",
        InteriorHex: "#dff4f8",
        Roughness: 0.06,
        Metalness: 0.05,
        Transmission: 0.82
    },
    tempered: {
        Identifier: "tempered",
        Label: "Tempered Safety Glass",
        ShortLabel: "Tempered",
        FractureCategory: "plate-brittle",
        Density: 2500,
        Gc: 7,
        YoungModulusGPa: 70.0,
        PoissonRatio: 0.22,
        TensileStrengthMPa: 170.0,
        WeibullModulus: 12.0,
        StoredEnergy: 46000,
        HertzConeRad: 0.0,
        AnisotropyRatio: 1.0,
        GrainAxis: [0, 1, 0],
        BeddingWeakness: 0.0,
        RingBias: 0.52,
        CrackJitter: 0.72,
        Restitution: 0.14,
        Friction: 0.35,
        DustYield: 1.45,
        ExteriorHex: "#a8d8e6",
        InteriorHex: "#d6f2fa",
        Roughness: 0.08,
        Metalness: 0.06,
        Transmission: 0.78
    },
    plastic: {
        Identifier: "plastic",
        Label: "Ductile ABS Polymer",
        ShortLabel: "ABS Polymer",
        FractureCategory: "ductile",
        Density: 1050,
        Gc: 460,
        YoungModulusGPa: 2.2,
        PoissonRatio: 0.35,
        TensileStrengthMPa: 42.0,
        WeibullModulus: 20.0,
        StoredEnergy: 0,
        HertzConeRad: 0.0,
        AnisotropyRatio: 1.0,
        GrainAxis: [0, 1, 0],
        BeddingWeakness: 0.0,
        RingBias: 0.12,
        CrackJitter: 0.48,
        Restitution: 0.36,
        Friction: 0.45,
        DustYield: 0.05,
        ExteriorHex: "#d95b38",
        InteriorHex: "#f0b49e",
        Roughness: 0.38,
        Metalness: 0.02,
        Transmission: 0.0
    }
});

// 📝 Evaluate Rayleigh surface wave speed cR ≈ 0.92 * sqrt(μ / ρ) [m/s].
export function EvaluateRayleighWaveSpeed(MaterialSpec)
{
    const ShearModulusPa = (MaterialSpec.YoungModulusGPa * 1e9) / (2.0 * (1.0 + MaterialSpec.PoissonRatio));
    return 0.92 * Math.sqrt(ShearModulusPa / MaterialSpec.Density);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                POLYHEDRON MEASUREMENTS
//------------------------------------------------------------------------------------------------------------------------

export function EvaluateFaceNormal(Complex, Face)
{
    const V = Complex.Vertices;
    let nx = 0, ny = 0, nz = 0;
    const Count = Face.Indices.length;
    for (let i = 0; i < Count; ++i)
    {
        const a = V[Face.Indices[i]];
        const b = V[Face.Indices[(i + 1) % Count]];
        nx += (a[1] - b[1]) * (a[2] + b[2]);
        ny += (a[2] - b[2]) * (a[0] + b[0]);
        nz += (a[0] - b[0]) * (a[1] + b[1]);
    }
    return Norm3([nx, ny, nz]);
}

export function EvaluateFaceArea(Complex, Face)
{
    const V = Complex.Vertices;
    if (Face.Indices.length < 3) return 0;
    const o = V[Face.Indices[0]];
    let Area = 0;
    for (let i = 1; i + 1 < Face.Indices.length; ++i)
    {
        const ab = Sub3(V[Face.Indices[i]], o);
        const ac = Sub3(V[Face.Indices[i + 1]], o);
        Area += 0.5 * Len3(Cross3(ab, ac));
    }
    return Area;
}

export function EvaluatePolyhedronMetrics(Complex)
{
    const V = Complex.Vertices;
    if (!V.length)
    {
        return { Volume: 0, Centroid: [0, 0, 0], BoundsMin: [0, 0, 0], BoundsMax: [0, 0, 0], Span: [0, 0, 0], Radius: 0, CrackArea: 0 };
    }
    const Origin = V[0];
    let SignedVol6 = 0;
    let cx = 0, cy = 0, cz = 0;
    let CrackArea = 0;

    for (const Face of Complex.Faces)
    {
        if (Face.Interior) CrackArea += EvaluateFaceArea(Complex, Face);
        for (let i = 1; i + 1 < Face.Indices.length; ++i)
        {
            const p1 = V[Face.Indices[0]];
            const p2 = V[Face.Indices[i]];
            const p3 = V[Face.Indices[i + 1]];
            const a = Sub3(p1, Origin);
            const b = Sub3(p2, Origin);
            const c = Sub3(p3, Origin);
            const Tet6 = Dot3(a, Cross3(b, c));
            SignedVol6 += Tet6;
            cx += (Origin[0] + p1[0] + p2[0] + p3[0]) * 0.25 * Tet6;
            cy += (Origin[1] + p1[1] + p2[1] + p3[1]) * 0.25 * Tet6;
            cz += (Origin[2] + p1[2] + p2[2] + p3[2]) * 0.25 * Tet6;
        }
    }

    const Volume = Math.abs(SignedVol6) / 6.0;
    const Centroid = Math.abs(SignedVol6) > 1e-12
        ? [cx / SignedVol6, cy / SignedVol6, cz / SignedVol6]
        : [...Origin];

    const BoundsMin = [Infinity, Infinity, Infinity];
    const BoundsMax = [-Infinity, -Infinity, -Infinity];
    let Radius = 0;
    for (const p of V)
    {
        for (let k = 0; k < 3; ++k)
        {
            if (p[k] < BoundsMin[k]) BoundsMin[k] = p[k];
            if (p[k] > BoundsMax[k]) BoundsMax[k] = p[k];
        }
        const d = Dist3(p, Centroid);
        if (d > Radius) Radius = d;
    }
    const Span = Sub3(BoundsMax, BoundsMin);
    return { Volume, Centroid, BoundsMin, BoundsMax, Span, Radius, CrackArea };
}

//------------------------------------------------------------------------------------------------------------------------
//                                               ANALYTICAL BASE CONSTRUCTS
//------------------------------------------------------------------------------------------------------------------------

export function ConstructBoxComplex(ExtentX, ExtentY, ExtentZ)
{
    const hx = 0.5 * ExtentX, hy = 0.5 * ExtentY, hz = 0.5 * ExtentZ;
    const Vertices = [
        [-hx, -hy, -hz], [hx, -hy, -hz], [hx, hy, -hz], [-hx, hy, -hz],
        [-hx, -hy,  hz], [hx, -hy,  hz], [hx, hy,  hz], [-hx, hy,  hz]
    ];
    const Loops = [
        [4, 5, 6, 7], [3, 2, 1, 0],
        [1, 2, 6, 5], [3, 0, 4, 7],
        [2, 3, 7, 6], [0, 1, 5, 4]
    ];
    return {
        Vertices,
        Faces: Loops.map((Indices) => ({ Indices, Interior: false, CutToken: 0 }))
    };
}

export function ConstructPrismComplex(RadiusBottom, RadiusTop, Height, Sides = 20)
{
    const hy = 0.5 * Height;
    const Vertices = [];
    const Faces = [];

    if (RadiusTop <= 1e-5)
    {
        // Cone: bottom ring + apex
        for (let s = 0; s < Sides; ++s)
        {
            const theta = (2.0 * Math.PI * s) / Sides;
            Vertices.push([RadiusBottom * Math.cos(theta), -hy, RadiusBottom * Math.sin(theta)]);
        }
        const ApexSlot = Vertices.length;
        Vertices.push([0, hy, 0]);
        Faces.push({ Indices: Array.from({ length: Sides }, (_, s) => s), Interior: false, CutToken: 0 });
        for (let s = 0; s < Sides; ++s)
        {
            const next = (s + 1) % Sides;
            Faces.push({ Indices: [next, s, ApexSlot], Interior: false, CutToken: 0 });
        }
    }
    else
    {
        for (let s = 0; s < Sides; ++s)
        {
            const theta = (2.0 * Math.PI * s) / Sides;
            Vertices.push([RadiusBottom * Math.cos(theta), -hy, RadiusBottom * Math.sin(theta)]);
        }
        for (let s = 0; s < Sides; ++s)
        {
            const theta = (2.0 * Math.PI * s) / Sides;
            Vertices.push([RadiusTop * Math.cos(theta), hy, RadiusTop * Math.sin(theta)]);
        }
        Faces.push({ Indices: Array.from({ length: Sides }, (_, s) => s), Interior: false, CutToken: 0 });
        Faces.push({ Indices: Array.from({ length: Sides }, (_, s) => 2 * Sides - 1 - s), Interior: false, CutToken: 0 });
        for (let s = 0; s < Sides; ++s)
        {
            const next = (s + 1) % Sides;
            Faces.push({ Indices: [next, s, Sides + s, Sides + next], Interior: false, CutToken: 0 });
        }
    }
    return OrientFacesOutward({ Vertices, Faces });
}

function OrientFacesOutward(Complex)
{
    let cx = 0, cy = 0, cz = 0;
    for (const v of Complex.Vertices) { cx += v[0]; cy += v[1]; cz += v[2]; }
    const Centre = [cx / Complex.Vertices.length, cy / Complex.Vertices.length, cz / Complex.Vertices.length];

    for (const Face of Complex.Faces)
    {
        let fx = 0, fy = 0, fz = 0;
        for (const idx of Face.Indices) { fx += Complex.Vertices[idx][0]; fy += Complex.Vertices[idx][1]; fz += Complex.Vertices[idx][2]; }
        const fc = [fx / Face.Indices.length, fy / Face.Indices.length, fz / Face.Indices.length];
        const n = EvaluateFaceNormal(Complex, Face);
        if (Dot3(n, Sub3(fc, Centre)) < 0) Face.Indices.reverse();
    }
    return Complex;
}

export function ConstructSolidComplex(SolidCategory, Scale = [1, 1, 1], Seed = 17)
{
    let Complex;
    if (SolidCategory === "monolith")
    {
        Complex = ConstructBoxComplex(0.96, 1.92, 0.96);
    }
    else if (SolidCategory === "curtain_pane")
    {
        Complex = ConstructBoxComplex(1.90, 1.38, 0.065);
    }
    else if (SolidCategory === "timber_beam")
    {
        Complex = ConstructBoxComplex(2.15, 0.36, 0.28);
    }
    else if (SolidCategory === "cylinder")
    {
        Complex = ConstructPrismComplex(0.62, 0.62, 1.75, 22);
    }
    else if (SolidCategory === "cone")
    {
        Complex = ConstructPrismComplex(0.76, 0.0, 1.72, 22);
    }
    else if (SolidCategory === "boulder" || SolidCategory === "hollow_core")
    {
        Complex = ConstructBoxComplex(1.70, 1.48, 1.56);
        const Rand = ConstructRandomStream(Seed + 109);
        const Cuts = SolidCategory === "hollow_core" ? 18 : 14;
        for (let i = 0; i < Cuts; ++i)
        {
            const theta = (2.0 * Math.PI * i) / Cuts + (Rand() - 0.5) * 0.35;
            const phi = Math.acos(1.0 - 2.0 * ((i + 0.5) / Cuts)) + (Rand() - 0.5) * 0.25;
            const n = Norm3([
                Math.sin(phi) * Math.cos(theta),
                Math.cos(phi) * 1.08,
                Math.sin(phi) * Math.sin(theta)
            ]);
            const d = 0.64 + Rand() * 0.16;
            const Clipped = ClipConvexPolyhedron(Complex, n, d, 0, false);
            if (Clipped.Back) Complex = Clipped.Back;
        }
    }
    else
    {
        Complex = ConstructBoxComplex(1.32, 1.32, 1.32);
    }

    Complex.Vertices = Complex.Vertices.map((v) => ScaleVec3(v, Scale));
    return OrientFacesOutward(Complex);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              EXACT HALF-SPACE CLIPPING
//------------------------------------------------------------------------------------------------------------------------

const VertexHash = (v) => `${Math.round(v[0] * 1e6)},${Math.round(v[1] * 1e6)},${Math.round(v[2] * 1e6)}`;

// 📝 Splits a closed convex polyhedron by plane n·p = d. Front lies on n·p >= d; Back lies on n·p <= d.
export function ClipConvexPolyhedron(Complex, PlaneNormal, PlaneOffset, CutToken = 1, MarkInterior = true)
{
    const eps = 1e-6;
    const n = Norm3(PlaneNormal);
    const d = PlaneOffset;
    const V = Complex.Vertices;
    const Dist = V.map((p) => Dot3(n, p) - d);

    let PosCount = 0, NegCount = 0;
    for (const s of Dist)
    {
        if (s > eps) PosCount++;
        else if (s < -eps) NegCount++;
    }
    if (NegCount === 0) return { Front: Complex, Back: null, CutArea: 0 };
    if (PosCount === 0) return { Front: null, Back: Complex, CutArea: 0 };

    const FrontVerts = [], BackVerts = [];
    const FrontMap = new Map(), BackMap = new Map();
    const InsertVertex = (Arr, Lookup, Pt) =>
    {
        const Key = VertexHash(Pt);
        const Existing = Lookup.get(Key);
        if (Existing !== undefined) return Existing;
        const Slot = Arr.length;
        Arr.push([Pt[0], Pt[1], Pt[2]]);
        Lookup.set(Key, Slot);
        return Slot;
    };

    const FrontFaces = [], BackFaces = [];
    const CutPoints = [];
    const CutSeen = new Set();

    for (const Face of Complex.Faces)
    {
        const fp = [], bp = [];
        const Count = Face.Indices.length;
        for (let i = 0; i < Count; ++i)
        {
            const ia = Face.Indices[i];
            const ib = Face.Indices[(i + 1) % Count];
            const da = Dist[ia], db = Dist[ib];
            const a = V[ia], b = V[ib];

            if (da >= -eps) fp.push(InsertVertex(FrontVerts, FrontMap, a));
            if (da <=  eps) bp.push(InsertVertex(BackVerts, BackMap, a));
            if (Math.abs(da) <= eps)
            {
                const k = VertexHash(a);
                if (!CutSeen.has(k)) { CutSeen.add(k); CutPoints.push(a); }
            }

            if ((da > eps && db < -eps) || (da < -eps && db > eps))
            {
                const t = da / (da - db);
                const x = Lerp3(a, b, t);
                fp.push(InsertVertex(FrontVerts, FrontMap, x));
                bp.push(InsertVertex(BackVerts, BackMap, x));
                const k = VertexHash(x);
                if (!CutSeen.has(k)) { CutSeen.add(k); CutPoints.push(x); }
            }
        }
        const CleanFp = DeduplicateCycle(fp);
        const CleanBp = DeduplicateCycle(bp);
        if (CleanFp.length >= 3) FrontFaces.push({ ...Face, Indices: CleanFp });
        if (CleanBp.length >= 3) BackFaces.push({ ...Face, Indices: CleanBp });
    }

    let CutArea = 0;
    if (CutPoints.length >= 3)
    {
        let cx = 0, cy = 0, cz = 0;
        for (const p of CutPoints) { cx += p[0]; cy += p[1]; cz += p[2]; }
        const Centre = [cx / CutPoints.length, cy / CutPoints.length, cz / CutPoints.length];
        const AxisX = Norm3(Sub3(CutPoints[0], Centre));
        const AxisY = Norm3(Cross3(n, AxisX));

        const Ordered = CutPoints
            .map((p) =>
            {
                const rel = Sub3(p, Centre);
                return { p, ang: Math.atan2(Dot3(rel, AxisY), Dot3(rel, AxisX)) };
            })
            .sort((a, b) => a.ang - b.ang)
            .map((entry) => entry.p);

        for (let i = 1; i + 1 < Ordered.length; ++i)
        {
            CutArea += 0.5 * Len3(Cross3(Sub3(Ordered[i], Ordered[0]), Sub3(Ordered[i + 1], Ordered[0])));
        }

        // Front lies on +n side, so its cap normal must point -n (reverse winding around +n)
        const FrontCap = DeduplicateCycle(Ordered.map((p) => InsertVertex(FrontVerts, FrontMap, p)).reverse());
        const BackCap = DeduplicateCycle(Ordered.map((p) => InsertVertex(BackVerts, BackMap, p)));
        if (FrontCap.length >= 3) FrontFaces.push({ Indices: FrontCap, Interior: MarkInterior, CutToken, CrackNormal: Mul3(n, -1) });
        if (BackCap.length >= 3) BackFaces.push({ Indices: BackCap, Interior: MarkInterior, CutToken, CrackNormal: n });
    }

    if (FrontFaces.length < 4 || BackFaces.length < 4)
    {
        return { Front: Complex, Back: null, CutArea: 0 };
    }

    return {
        Front: { Vertices: FrontVerts, Faces: FrontFaces },
        Back: { Vertices: BackVerts, Faces: BackFaces },
        CutArea
    };
}

function DeduplicateCycle(Indices)
{
    const Out = [];
    for (let i = 0; i < Indices.length; ++i)
    {
        const curr = Indices[i];
        const prev = Out.length ? Out[Out.length - 1] : Indices[Indices.length - 1];
        if (curr !== prev) Out.push(curr);
    }
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                          DIRECTIONAL TOUGHNESS & STRESS PLANES
//------------------------------------------------------------------------------------------------------------------------

export function EvaluateDirectionalToughness(MaterialSpec, PlaneNormal, GrainAngleRad = 0, Rand = null)
{
    let G = MaterialSpec.Gc;
    const RotGrain = [
        Math.cos(GrainAngleRad) * MaterialSpec.GrainAxis[0] - Math.sin(GrainAngleRad) * MaterialSpec.GrainAxis[2],
        MaterialSpec.GrainAxis[1],
        Math.sin(GrainAngleRad) * MaterialSpec.GrainAxis[0] + Math.cos(GrainAngleRad) * MaterialSpec.GrainAxis[2]
    ];

    if (MaterialSpec.FractureCategory === "fibrous")
    {
        const Across = Math.abs(Dot3(PlaneNormal, Norm3(RotGrain)));
        G *= 1.0 + (MaterialSpec.AnisotropyRatio - 1.0) * Across * Across;
    }
    else if (MaterialSpec.BeddingWeakness > 0)
    {
        const Align = Math.abs(Dot3(PlaneNormal, Norm3(RotGrain)));
        G *= 1.0 - MaterialSpec.BeddingWeakness * Math.pow(Align, 4);
    }

    if (Rand)
    {
        const u = Math.max(1e-4, Math.min(0.9999, Rand()));
        const WeibullFactor = Math.pow(-Math.log(1.0 - u), 1.0 / MaterialSpec.WeibullModulus);
        G *= Math.max(0.35, Math.min(2.2, WeibullFactor));
    }
    return G;
}

function SampleStressCrackNormal(MaterialSpec, Centroid, ImpactPoint, ImpactNormal, ObjectRadius, GrainAngleRad, Rand)
{
    const Rel = Sub3(Centroid, ImpactPoint);
    const Axial = Mul3(ImpactNormal, Dot3(Rel, ImpactNormal));
    let Radial = Sub3(Rel, Axial);
    const r = Len3(Radial);
    Radial = r > 1e-5 ? Mul3(Radial, 1.0 / r) : Perp3(ImpactNormal);
    const Hoop = Norm3(Cross3(ImpactNormal, Radial));

    const RotGrain = Norm3([
        Math.cos(GrainAngleRad) * MaterialSpec.GrainAxis[0] - Math.sin(GrainAngleRad) * MaterialSpec.GrainAxis[2],
        MaterialSpec.GrainAxis[1],
        Math.sin(GrainAngleRad) * MaterialSpec.GrainAxis[0] + Math.cos(GrainAngleRad) * MaterialSpec.GrainAxis[2]
    ]);

    let n;
    if (MaterialSpec.FractureCategory === "plate-brittle")
    {
        if (MaterialSpec.StoredEnergy > 0)
        {
            n = Norm3([Rand() * 2 - 1, Rand() * 2 - 1, Rand() * 2 - 1]);
            n = Norm3(Add3(n, Mul3(ImpactNormal, -Dot3(n, ImpactNormal) * 0.86)));
        }
        else
        {
            const RingProb = Math.min(1, Math.pow(r / Math.max(0.15, ObjectRadius * 0.48), 1.35)) * (0.32 + MaterialSpec.RingBias);
            n = Rand() < RingProb ? [...Radial] : [...Hoop];
            n = Norm3(Add3(n, Mul3(ImpactNormal, -Dot3(n, ImpactNormal) * 0.88)));
        }
    }
    else if (MaterialSpec.FractureCategory === "fibrous")
    {
        const RandomVec = [Rand() * 2 - 1, Rand() * 2 - 1, Rand() * 2 - 1];
        n = Cross3(RotGrain, RandomVec);
        n = LenSq3(n) > 1e-4 ? Norm3(n) : Perp3(RotGrain);
        const NearImpact = Math.max(0, 1.0 - r / Math.max(0.2, ObjectRadius * 0.55));
        if (Rand() < 0.12 + 0.24 * NearImpact)
        {
            n = Norm3(Add3(RotGrain, Mul3(Hoop, 0.28)));
        }
    }
    else if (MaterialSpec.FractureCategory === "ductile")
    {
        n = Norm3(Lerp3(Hoop, Radial, Rand() * 0.65));
    }
    else
    {
        const RingProb = Math.min(1, Math.pow(r / Math.max(0.2, ObjectRadius * 0.62), 1.45)) * MaterialSpec.RingBias;
        n = Rand() < RingProb ? [...Radial] : [...Hoop];
        if (Rand() < 0.30)
        {
            n = Norm3(Lerp3(n, Norm3([Rand() * 2 - 1, Rand() * 2 - 1, Rand() * 2 - 1]), 0.72));
        }
        if (MaterialSpec.BeddingWeakness > 0 && Rand() < 0.32 + MaterialSpec.BeddingWeakness * 0.4)
        {
            n = Norm3(Lerp3(n, RotGrain, 0.82));
        }
    }

    const j = MaterialSpec.CrackJitter;
    n = Norm3([n[0] + (Rand() - 0.5) * j, n[1] + (Rand() - 0.5) * j, n[2] + (Rand() - 0.5) * j]);
    return n;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              UNIFIED FOUR-SOLVER ENGINE
//------------------------------------------------------------------------------------------------------------------------

export function ExecuteFracturePartition(BaseComplex, Specification)
{
    const Solver = Specification.Solver || "griffith";
    if (Solver === "runtime_boussinesq")
    {
        return ExecuteFastRuntimeFracture(BaseComplex, Specification);
    }
    if (Solver === "engineering_fem")
    {
        return ExecuteEngineeringBakedFracture(BaseComplex, Specification);
    }

    const StartClock = performance.now();
    const MaterialSpec = MaterialCatalogue[Specification.Material] || MaterialCatalogue.concrete;
    const Rand = ConstructRandomStream(Specification.Seed || 42);
    const BaseMetrics = EvaluatePolyhedronMetrics(BaseComplex);
    const ImpactPoint = Specification.ImpactPoint || [0, 0.2, BaseMetrics.BoundsMax[2]];
    const ImpactNormal = Norm3(Specification.ImpactNormal || [0, 0, -1]);
    const Ceiling = Math.max(2, Math.min(160, Math.round(Specification.Ceiling || 48)));
    const MinSpan = Math.max(0.015, Number(Specification.MinimumSize || 0.055));
    const GrainAngleRad = ((Specification.GrainAngleDeg || 0) * Math.PI) / 180.0;

    const StoredTotalEnergy = MaterialSpec.StoredEnergy * BaseMetrics.Volume;
    const TotalEnergyBudget = Math.max(40, Number(Specification.Energy || 2500)) + StoredTotalEnergy;

    let CutCounter = 1;
    let RawPieces = [];
    let SpentSurfaceEnergy = 0;
    let WavefrontPaths = [];

    if (Solver === "voronoi")
    {
        RawPieces = SolveClusteredVoronoi(BaseComplex, BaseMetrics, ImpactPoint, Ceiling, MinSpan, Rand, () => CutCounter++);
        for (const p of RawPieces)
        {
            const m = EvaluatePolyhedronMetrics(p);
            SpentSurfaceEnergy += m.CrackArea * MaterialSpec.Gc;
        }
    }
    else if (Solver === "wavefront")
    {
        const Result = SolveLefmWavefront(BaseComplex, BaseMetrics, MaterialSpec, ImpactPoint, ImpactNormal, TotalEnergyBudget, Ceiling, MinSpan, GrainAngleRad, Rand, () => CutCounter++);
        RawPieces = Result.Pieces;
        SpentSurfaceEnergy = Result.SpentEnergy;
        WavefrontPaths = Result.WavefrontPaths;
    }
    else if (Solver === "dendritic")
    {
        const Result = SolveDendriticCoreShell(BaseComplex, BaseMetrics, MaterialSpec, ImpactPoint, ImpactNormal, Ceiling, MinSpan, GrainAngleRad, Rand, () => CutCounter++);
        RawPieces = Result.Pieces;
        SpentSurfaceEnergy = Result.SpentEnergy;
        WavefrontPaths = Result.DendriticPolylines;
    }
    else
    {
        const Result = SolveGriffithStressBudget(BaseComplex, BaseMetrics, MaterialSpec, ImpactPoint, ImpactNormal, TotalEnergyBudget, Ceiling, MinSpan, GrainAngleRad, Rand, () => CutCounter++);
        RawPieces = Result.Pieces;
        SpentSurfaceEnergy = Result.SpentEnergy;
    }

    // Build ConvexFragment records with mass, volume, proximity, and ejection kinematics
    const Fragments = RawPieces.map((Poly, Index) =>
    {
        const Metrics = EvaluatePolyhedronMetrics(Poly);
        const DistImpact = Dist3(Metrics.Centroid, ImpactPoint);
        const Proximity = Math.max(0, 1.0 - DistImpact / Math.max(0.25, BaseMetrics.Radius * 1.65));
        const Mass = Math.max(0.01, Metrics.Volume * MaterialSpec.Density);
        const RadialOut = Norm3(Add3(Sub3(Metrics.Centroid, ImpactPoint), Mul3(ImpactNormal, 0.35)));
        const EjectionBase = Math.sqrt((2.0 * Math.max(15, TotalEnergyBudget * 0.08)) / Math.max(0.8, BaseMetrics.Volume * MaterialSpec.Density));
        const EjectionSpeed = Math.min(18.0, EjectionBase * (0.28 + 1.45 * Proximity * Proximity));

        return {
            Index,
            Token: `FRAG-${String(Index + 1).padStart(3, "0")}`,
            Complex: Poly,
            Volume: Metrics.Volume,
            Mass,
            Centroid: Metrics.Centroid,
            BoundsMin: Metrics.BoundsMin,
            BoundsMax: Metrics.BoundsMax,
            Span: Metrics.Span,
            Radius: Metrics.Radius,
            CrackArea: Metrics.CrackArea,
            Proximity,
            StressRatio: Math.min(1.0, 0.18 + Proximity * 0.92),
            InitialVelocity: Mul3(RadialOut, EjectionSpeed),
            InitialAngular: [
                (Rand() - 0.5) * 9.0 * (0.3 + Proximity),
                (Rand() - 0.5) * 9.0 * (0.3 + Proximity),
                (Rand() - 0.5) * 9.0 * (0.3 + Proximity)
            ],
            Visible: true
        };
    });

    const Bonds = ConstructAdjacencyBonds(Fragments, MaterialSpec, ImpactPoint, BaseMetrics.Radius, Specification.BondStrengthScale ?? 1.0);
    const TotalFragmentVolume = Fragments.reduce((acc, f) => acc + f.Volume, 0);
    const TotalCrackArea = Fragments.reduce((acc, f) => acc + f.CrackArea, 0) * 0.5;
    const VolumeDriftRatio = BaseMetrics.Volume > 1e-9 ? Math.abs(TotalFragmentVolume - BaseMetrics.Volume) / BaseMetrics.Volume : 0;
    const ElapsedMs = performance.now() - StartClock;

    return {
        Solver,
        MaterialSpec,
        BaseMetrics,
        Fragments,
        Bonds,
        WavefrontPaths,
        Receipt: {
            FragmentCount: Fragments.length,
            BondCount: Bonds.length,
            IntactBondCount: Bonds.filter((b) => !b.BrokenOnImpact).length,
            BaseVolume: BaseMetrics.Volume,
            FragmentVolumeSum: TotalFragmentVolume,
            VolumeDriftRatio,
            TotalCrackArea,
            TotalEnergyBudget,
            SpentSurfaceEnergy: Math.min(TotalEnergyBudget, SpentSurfaceEnergy > 0 ? SpentSurfaceEnergy : 2.0 * MaterialSpec.Gc * TotalCrackArea),
            RayleighSpeedMps: EvaluateRayleighWaveSpeed(MaterialSpec),
            ComputeDurationMs: ElapsedMs,
            WatertightManifold: VolumeDriftRatio < 1e-4
        }
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                      SOLVER 1: GRIFFITH STRESS-BUDGETED CLIPPING
//------------------------------------------------------------------------------------------------------------------------

function SolveGriffithStressBudget(BaseComplex, BaseMetrics, MaterialSpec, ImpactPoint, ImpactNormal, TotalEnergyBudget, Ceiling, MinSpan, GrainAngleRad, Rand, NextToken)
{
    let Pool = [{ Complex: BaseComplex, Metrics: BaseMetrics, Energy: TotalEnergyBudget }];
    let SpentEnergy = 0;

    // ① Hertzian impact cone plug nucleation for brittle solids
    if (MaterialSpec.HertzConeRad > 0.1 && Pool.length < Ceiling)
    {
        const ConePlanes = 4;
        for (let k = 0; k < ConePlanes && Pool.length < Ceiling; ++k)
        {
            const Azimuth = (2.0 * Math.PI * k) / ConePlanes + Rand() * 0.35;
            const Tang = Add3(Mul3(Perp3(ImpactNormal), Math.cos(Azimuth)), Mul3(Cross3(ImpactNormal, Perp3(ImpactNormal)), Math.sin(Azimuth)));
            const ConeNormal = Norm3(Add3(Mul3(Tang, Math.sin(MaterialSpec.HertzConeRad)), Mul3(ImpactNormal, -Math.cos(MaterialSpec.HertzConeRad))));
            const Anchor = Add3(ImpactPoint, Mul3(ImpactNormal, 0.14 * BaseMetrics.Radius));
            const Offset = Dot3(ConeNormal, Anchor) + (Rand() - 0.5) * 0.06 * BaseMetrics.Radius;

            Pool.sort((a, b) => Dist3(a.Metrics.Centroid, ImpactPoint) - Dist3(b.Metrics.Centroid, ImpactPoint));
            const Target = Pool[0];
            const Clipped = ClipConvexPolyhedron(Target.Complex, ConeNormal, Offset, NextToken(), true);
            if (Clipped.Front && Clipped.Back)
            {
                const mf = EvaluatePolyhedronMetrics(Clipped.Front);
                const mb = EvaluatePolyhedronMetrics(Clipped.Back);
                if (Math.min(...mf.Span) >= MinSpan * 0.6 && Math.min(...mb.Span) >= MinSpan * 0.6 && mf.Volume > 1e-6 && mb.Volume > 1e-6)
                {
                    const Cost = 2.0 * MaterialSpec.Gc * Clipped.CutArea;
                    SpentEnergy += Cost;
                    const Rem = Math.max(0, Target.Energy - Cost);
                    const Ratio = mf.Volume / (mf.Volume + mb.Volume);
                    Pool.splice(0, 1,
                        { Complex: Clipped.Front, Metrics: mf, Energy: Rem * Ratio },
                        { Complex: Clipped.Back, Metrics: mb, Energy: Rem * (1.0 - Ratio) }
                    );
                }
            }
        }
    }

    // ② Iterative principal-stress energy-budgeted plane cuts
    let Guard = 0;
    while (Pool.length < Ceiling && Guard++ < Ceiling * 10)
    {
        let BestIdx = -1;
        let BestPriority = -1;
        for (let i = 0; i < Pool.length; ++i)
        {
            const Item = Pool[i];
            if (Math.max(...Item.Metrics.Span) < MinSpan * 2.1) continue;
            const d = Dist3(Item.Metrics.Centroid, ImpactPoint);
            const ProximityWeight = MaterialSpec.StoredEnergy > 0
                ? 1.0
                : 1.0 / (0.14 + Math.pow(d / Math.max(0.25, BaseMetrics.Radius), 1.35));
            const Priority = (Item.Energy + 18.0) * Math.pow(Item.Metrics.Volume, 0.55) * ProximityWeight;
            if (Priority > BestPriority)
            {
                BestPriority = Priority;
                BestIdx = i;
            }
        }
        if (BestIdx < 0) break;

        const Target = Pool[BestIdx];
        let Chosen = null;

        for (let Candidate = 0; Candidate < 7; ++Candidate)
        {
            const n = SampleStressCrackNormal(MaterialSpec, Target.Metrics.Centroid, ImpactPoint, ImpactNormal, BaseMetrics.Radius, GrainAngleRad, Rand);
            const Bias = (Rand() - 0.5) * 0.28 * Target.Metrics.Radius;
            const d = Dot3(n, Target.Metrics.Centroid) + Bias;
            const Clipped = ClipConvexPolyhedron(Target.Complex, n, d, NextToken(), true);
            if (!Clipped.Front || !Clipped.Back) continue;

            const mf = EvaluatePolyhedronMetrics(Clipped.Front);
            const mb = EvaluatePolyhedronMetrics(Clipped.Back);
            const MinimumSplitSpan = Math.min(Math.min(...mf.Span), Math.min(...mb.Span));
            const Balance = Math.min(mf.Volume, mb.Volume) / Math.max(mf.Volume, mb.Volume);
            if (MinimumSplitSpan < MinSpan * 0.45 || Balance < 0.12) continue;

            const LocalGc = EvaluateDirectionalToughness(MaterialSpec, n, GrainAngleRad, Rand);
            const Cost = 2.0 * LocalGc * Clipped.CutArea;
            const Score = (Balance * 1.4) / (1.0 + Cost / Math.max(60.0, TotalEnergyBudget * 0.15));
            if (!Chosen || Score > Chosen.Score)
            {
                Chosen = { Clipped, mf, mb, Cost, Score };
            }
        }

        if (!Chosen)
        {
            Pool[BestIdx].Energy *= 0.15;
            if (Pool[BestIdx].Energy < 1.0) continue;
            continue;
        }

        if (SpentEnergy + Chosen.Cost > TotalEnergyBudget * 1.35 && Pool.length >= 4)
        {
            break;
        }

        SpentEnergy += Chosen.Cost;
        const Remaining = Math.max(0, Target.Energy - Chosen.Cost);
        const ShareF = Chosen.mf.Volume / (Chosen.mf.Volume + Chosen.mb.Volume);
        Pool.splice(BestIdx, 1,
            { Complex: Chosen.Clipped.Front, Metrics: Chosen.mf, Energy: Remaining * ShareF },
            { Complex: Chosen.Clipped.Back, Metrics: Chosen.mb, Energy: Remaining * (1.0 - ShareF) }
        );
    }

    return { Pieces: Pool.map((p) => p.Complex), SpentEnergy };
}

//------------------------------------------------------------------------------------------------------------------------
//                                      SOLVER 2: LEFM CRACK-TIP RADIAL / RING WAVEFRONT
//------------------------------------------------------------------------------------------------------------------------

function SolveLefmWavefront(BaseComplex, BaseMetrics, MaterialSpec, ImpactPoint, ImpactNormal, TotalEnergyBudget, Ceiling, MinSpan, GrainAngleRad, Rand, NextToken)
{
    const TangentU = Perp3(ImpactNormal);
    const TangentV = Norm3(Cross3(ImpactNormal, TangentU));
    const RadialSpokes = Math.max(3, Math.min(12, Math.round(3 + Math.sqrt(TotalEnergyBudget / Math.max(12, MaterialSpec.Gc * 8)))));

    let Pieces = [BaseComplex];
    let SpentEnergy = 0;
    const WavefrontPaths = [];

    // ① Primary radial star cracks emanating from impact locus
    const Angles = [];
    for (let s = 0; s < RadialSpokes; ++s)
    {
        Angles.push((2.0 * Math.PI * s) / RadialSpokes + (Rand() - 0.5) * (0.38 / RadialSpokes) * Math.PI);
    }

    for (const Theta of Angles)
    {
        if (Pieces.length >= Ceiling) break;
        const RayDir = Add3(Mul3(TangentU, Math.cos(Theta)), Mul3(TangentV, Math.sin(Theta)));
        const PlaneNorm = Norm3(Cross3(ImpactNormal, RayDir));
        const d = Dot3(PlaneNorm, ImpactPoint);

        WavefrontPaths.push({
            Origin: ImpactPoint,
            End: Add3(ImpactPoint, Mul3(RayDir, BaseMetrics.Radius * 1.4)),
            Classification: "radial"
        });

        const NextPieces = [];
        for (const Piece of Pieces)
        {
            if (Pieces.length + NextPieces.length >= Ceiling) { NextPieces.push(Piece); continue; }
            const pm = EvaluatePolyhedronMetrics(Piece);
            // Half-plane check so radial spoke runs outward on +RayDir side (producing T-junctions!)
            if (Dot3(Sub3(pm.Centroid, ImpactPoint), RayDir) < -0.15 * BaseMetrics.Radius && NextPieces.length + Pieces.length > 2)
            {
                NextPieces.push(Piece);
                continue;
            }
            const Clipped = ClipConvexPolyhedron(Piece, PlaneNorm, d, NextToken(), true);
            if (Clipped.Front && Clipped.Back)
            {
                const mf = EvaluatePolyhedronMetrics(Clipped.Front);
                const mb = EvaluatePolyhedronMetrics(Clipped.Back);
                if (mf.Volume > 1e-5 && mb.Volume > 1e-5)
                {
                    SpentEnergy += 2.0 * MaterialSpec.Gc * Clipped.CutArea;
                    NextPieces.push(Clipped.Front, Clipped.Back);
                    continue;
                }
            }
            NextPieces.push(Piece);
        }
        Pieces = NextPieces;
    }

    // ② Concentric flexural ring chords & secondary branching inside each wedge
    const Rings = [0.24, 0.52, 0.84, 1.18];
    for (const RingFrac of Rings)
    {
        if (Pieces.length >= Ceiling) break;
        const RingRadius = RingFrac * BaseMetrics.Radius;
        const NextPieces = [];
        for (const Piece of Pieces)
        {
            if (Pieces.length + NextPieces.length >= Ceiling) { NextPieces.push(Piece); continue; }
            const pm = EvaluatePolyhedronMetrics(Piece);
            const Rel = Sub3(pm.Centroid, ImpactPoint);
            const InPlane = Sub3(Rel, Mul3(ImpactNormal, Dot3(Rel, ImpactNormal)));
            const r = Len3(InPlane);
            if (Math.abs(r - RingRadius) < pm.Radius * 0.95 && Math.max(...pm.Span) > MinSpan * 2.0)
            {
                const RingNorm = Norm3(Add3(r > 1e-5 ? Mul3(InPlane, 1 / r) : TangentU, Mul3(ImpactNormal, (Rand() - 0.5) * 0.22)));
                const Anchor = Add3(ImpactPoint, Mul3(RingNorm, RingRadius * (0.9 + Rand() * 0.2)));
                const Clipped = ClipConvexPolyhedron(Piece, RingNorm, Dot3(RingNorm, Anchor), NextToken(), true);
                if (Clipped.Front && Clipped.Back)
                {
                    const mf = EvaluatePolyhedronMetrics(Clipped.Front);
                    const mb = EvaluatePolyhedronMetrics(Clipped.Back);
                    if (mf.Volume > 1e-5 && mb.Volume > 1e-5)
                    {
                        SpentEnergy += 2.0 * EvaluateDirectionalToughness(MaterialSpec, RingNorm, GrainAngleRad, Rand) * Clipped.CutArea;
                        NextPieces.push(Clipped.Front, Clipped.Back);
                        continue;
                    }
                }
            }
            NextPieces.push(Piece);
        }
        Pieces = NextPieces;
    }

    return { Pieces, SpentEnergy, WavefrontPaths };
}

//------------------------------------------------------------------------------------------------------------------------
//                                      SOLVER 3: CLUSTERED 3D VORONOI LATTICE
//------------------------------------------------------------------------------------------------------------------------

function SolveClusteredVoronoi(BaseComplex, BaseMetrics, ImpactPoint, Ceiling, MinSpan, Rand, NextToken)
{
    const Seeds = [];
    const Span = BaseMetrics.Span;
    const MaxTries = Ceiling * 18;

    for (let t = 0; t < MaxTries && Seeds.length < Ceiling; ++t)
    {
        let Candidate;
        if (Rand() < 0.62)
        {
            // Impact-clustered radial envelope
            const u = Math.pow(Rand(), 1.65);
            const theta = Rand() * Math.PI * 2.0;
            const phi = Math.acos(2.0 * Rand() - 1.0);
            const r = u * BaseMetrics.Radius * 1.15;
            Candidate = [
                Math.max(BaseMetrics.BoundsMin[0] + 0.02, Math.min(BaseMetrics.BoundsMax[0] - 0.02, ImpactPoint[0] + r * Math.sin(phi) * Math.cos(theta))),
                Math.max(BaseMetrics.BoundsMin[1] + 0.02, Math.min(BaseMetrics.BoundsMax[1] - 0.02, ImpactPoint[1] + r * Math.cos(phi))),
                Math.max(BaseMetrics.BoundsMin[2] + 0.02, Math.min(BaseMetrics.BoundsMax[2] - 0.02, ImpactPoint[2] + r * Math.sin(phi) * Math.sin(theta)))
            ];
        }
        else
        {
            Candidate = [
                BaseMetrics.BoundsMin[0] + (0.06 + 0.88 * Rand()) * Span[0],
                BaseMetrics.BoundsMin[1] + (0.06 + 0.88 * Rand()) * Span[1],
                BaseMetrics.BoundsMin[2] + (0.06 + 0.88 * Rand()) * Span[2]
            ];
        }

        let TooClose = false;
        for (const s of Seeds)
        {
            if (Dist3(s, Candidate) < MinSpan * 0.75) { TooClose = true; break; }
        }
        if (!TooClose) Seeds.push(Candidate);
    }

    if (Seeds.length < 2)
    {
        Seeds.push(BaseMetrics.Centroid, Add3(BaseMetrics.Centroid, [0.1, 0.05, 0]));
    }

    // Shared pair token lookup so neighbor cells recognize shared bisecting planes
    const PairTokens = new Map();
    const GetPairToken = (i, j) =>
    {
        const Key = i < j ? `${i}:${j}` : `${j}:${i}`;
        if (!PairTokens.has(Key)) PairTokens.set(Key, NextToken());
        return PairTokens.get(Key);
    };

    const Cells = [];
    for (let i = 0; i < Seeds.length; ++i)
    {
        let Cell = BaseComplex;
        const si = Seeds[i];
        // Sort neighbors nearest-first so distant seeds get culled fast
        const Neighbors = Seeds
            .map((sj, j) => ({ sj, j, d: Dist3(si, sj) }))
            .filter((n) => n.j !== i)
            .sort((a, b) => a.d - b.d);

        for (const { sj, j } of Neighbors)
        {
            const Mid = Mul3(Add3(si, sj), 0.5);
            const NormalOut = Norm3(Sub3(sj, si)); // Cell i lies on NormalOut · x <= NormalOut · Mid (Back side)
            const Offset = Dot3(NormalOut, Mid);
            const Clipped = ClipConvexPolyhedron(Cell, NormalOut, Offset, GetPairToken(i, j), true);
            Cell = Clipped.Back;
            if (!Cell) break;
        }

        if (Cell)
        {
            const cm = EvaluatePolyhedronMetrics(Cell);
            if (cm.Volume > 1e-6) Cells.push(Cell);
        }
    }

    return Cells.length ? Cells : [BaseComplex];
}

//------------------------------------------------------------------------------------------------------------------------
//                                   SOLVER 4: DENDRITIC LIGHTNING & CORE-SHELL SPALLING
//------------------------------------------------------------------------------------------------------------------------

function SolveDendriticCoreShell(BaseComplex, BaseMetrics, MaterialSpec, ImpactPoint, ImpactNormal, Ceiling, MinSpan, GrainAngleRad, Rand, NextToken)
{
    let Pieces = [BaseComplex];
    let SpentEnergy = 0;
    const DendriticPolylines = [];

    // ① Outer spall shell cuts around impact hemisphere
    const SpallDirs = [
        Norm3(Mul3(ImpactNormal, -1)),
        Norm3(Add3(Mul3(ImpactNormal, -0.6), Perp3(ImpactNormal))),
        Norm3(Sub3(Mul3(ImpactNormal, -0.6), Perp3(ImpactNormal))),
        Norm3([0, 1, 0])
    ];
    for (const Dir of SpallDirs)
    {
        if (Pieces.length >= Math.min(Ceiling, 7)) break;
        const ShellDepth = BaseMetrics.Radius * (0.48 + Rand() * 0.18);
        const Offset = Dot3(Dir, Add3(BaseMetrics.Centroid, Mul3(Dir, ShellDepth)));
        const Next = [];
        for (const p of Pieces)
        {
            const Clipped = ClipConvexPolyhedron(p, Dir, Offset, NextToken(), true);
            if (Clipped.Front && Clipped.Back)
            {
                SpentEnergy += 2.0 * MaterialSpec.Gc * Clipped.CutArea;
                Next.push(Clipped.Front, Clipped.Back);
            }
            else Next.push(p);
        }
        Pieces = Next;
    }

    // ② Dendritic jagged lightning polylines from impact entry through interior core
    const BranchCount = Math.max(3, Math.min(8, Math.round(Ceiling / 7)));
    let Cursor = [...ImpactPoint];
    const ExitTarget = Add3(BaseMetrics.Centroid, Mul3(ImpactNormal, BaseMetrics.Radius * 0.85));
    const Trunk = [Cursor];
    const Steps = 5;
    for (let s = 1; s <= Steps; ++s)
    {
        const t = s / Steps;
        const Jitter = [
            (Rand() - 0.5) * BaseMetrics.Radius * 0.45,
            (Rand() - 0.5) * BaseMetrics.Radius * 0.45,
            (Rand() - 0.5) * BaseMetrics.Radius * 0.45
        ];
        Cursor = Add3(Lerp3(ImpactPoint, ExitTarget, t), s < Steps ? Jitter : [0, 0, 0]);
        Trunk.push(Cursor);
    }
    DendriticPolylines.push({ Points: Trunk, Classification: "trunk" });

    for (let b = 0; b < BranchCount; ++b)
    {
        const Anchor = Trunk[Math.min(Trunk.length - 1, 1 + (b % (Trunk.length - 1)))];
        const BranchEnd = Add3(Anchor, [
            (Rand() - 0.5) * BaseMetrics.Radius * 1.3,
            (Rand() - 0.5) * BaseMetrics.Radius * 1.3,
            (Rand() - 0.5) * BaseMetrics.Radius * 1.3
        ]);
        DendriticPolylines.push({ Points: [Anchor, Lerp3(Anchor, BranchEnd, 0.52), BranchEnd], Classification: "branch" });
    }

    // Slice pieces along dendritic segments until ceiling reached
    let Guard = 0;
    while (Pieces.length < Ceiling && Guard++ < Ceiling * 6)
    {
        Pieces.sort((a, b) => EvaluatePolyhedronMetrics(b).Volume - EvaluatePolyhedronMetrics(a).Volume);
        const Target = Pieces[0];
        const tm = EvaluatePolyhedronMetrics(Target);
        if (Math.max(...tm.Span) < MinSpan * 1.8) break;

        const Polyline = DendriticPolylines[Guard % DendriticPolylines.length];
        const SegA = Polyline.Points[0];
        const SegB = Polyline.Points[Polyline.Points.length - 1];
        const SegDir = Norm3(Sub3(SegB, SegA));
        const RandSide = Norm3([Rand() * 2 - 1, Rand() * 2 - 1, Rand() * 2 - 1]);
        const CutNormal = Norm3(Cross3(SegDir, RandSide));
        const Anchor = Lerp3(tm.Centroid, SegA, 0.35);
        const Clipped = ClipConvexPolyhedron(Target, CutNormal, Dot3(CutNormal, Anchor), NextToken(), true);
        if (Clipped.Front && Clipped.Back)
        {
            const mf = EvaluatePolyhedronMetrics(Clipped.Front);
            const mb = EvaluatePolyhedronMetrics(Clipped.Back);
            if (mf.Volume > 1e-5 && mb.Volume > 1e-5)
            {
                SpentEnergy += 2.0 * EvaluateDirectionalToughness(MaterialSpec, CutNormal, GrainAngleRad, Rand) * Clipped.CutArea;
                Pieces.splice(0, 1, Clipped.Front, Clipped.Back);
            }
        }
    }

    return { Pieces, SpentEnergy, DendriticPolylines };
}

//------------------------------------------------------------------------------------------------------------------------
//                                            ADJACENCY STRAIN-BOND LATTICE
//------------------------------------------------------------------------------------------------------------------------

export function ConstructAdjacencyBonds(Fragments, MaterialSpec, ImpactPoint, ObjectRadius, BondStrengthScale = 1.0)
{
    const Bonds = [];
    const TokenGroups = new Map();

    // Index fragments by shared CutToken for exact interior cut-plane mates
    Fragments.forEach((Frag, FragIdx) =>
    {
        for (const Face of Frag.Complex.Faces)
        {
            if (Face.Interior && Face.CutToken > 0)
            {
                if (!TokenGroups.has(Face.CutToken)) TokenGroups.set(Face.CutToken, []);
                TokenGroups.get(Face.CutToken).push({ FragIdx, Area: EvaluateFaceArea(Frag.Complex, Face) });
            }
        }
    });

    const SeenPairs = new Set();
    const AddBond = (IndexA, IndexB, SharedArea) =>
    {
        if (IndexA === IndexB) return;
        const Key = IndexA < IndexB ? `${IndexA}:${IndexB}` : `${IndexB}:${IndexA}`;
        if (SeenPairs.has(Key)) return;
        SeenPairs.add(Key);

        const A = Fragments[IndexA];
        const B = Fragments[IndexB];
        const Mid = Mul3(Add3(A.Centroid, B.Centroid), 0.5);
        const DistImpact = Dist3(Mid, ImpactPoint);
        const ImpactStress = 1.0 / (0.16 + Math.pow(DistImpact / Math.max(0.25, ObjectRadius), 1.65));
        const Capacity = (0.55 + 0.45 * (MaterialSpec.TensileStrengthMPa / 60.0)) * BondStrengthScale * (0.65 + Math.min(1.4, SharedArea * 4.0));
        const StrainRatio = ImpactStress / Math.max(0.15, Capacity * 2.4);

        Bonds.push({
            IndexA,
            IndexB,
            PointA: A.Centroid,
            PointB: B.Centroid,
            SharedArea,
            CapacityKN: SharedArea * MaterialSpec.TensileStrengthMPa * 1e3 * BondStrengthScale,
            StrainRatio: Math.min(1.6, StrainRatio),
            BrokenOnImpact: MaterialSpec.StoredEnergy > 0 ? true : StrainRatio >= 1.0
        });
    };

    for (const Entries of TokenGroups.values())
    {
        for (let i = 0; i < Entries.length; ++i)
        {
            for (let j = i + 1; j < Entries.length; ++j)
            {
                const fa = Fragments[Entries[i].FragIdx];
                const fb = Fragments[Entries[j].FragIdx];
                if (Dist3(fa.Centroid, fb.Centroid) <= (fa.Radius + fb.Radius) * 1.12)
                {
                    AddBond(Entries[i].FragIdx, Entries[j].FragIdx, Math.min(Entries[i].Area, Entries[j].Area));
                }
            }
        }
    }

    // Supplement geometric proximity neighbors if needed
    for (let i = 0; i < Fragments.length; ++i)
    {
        for (let j = i + 1; j < Fragments.length; ++j)
        {
            const d = Dist3(Fragments[i].Centroid, Fragments[j].Centroid);
            if (d < (Fragments[i].Radius + Fragments[j].Radius) * 0.78)
            {
                AddBond(i, j, 0.04);
            }
        }
    }

    return Bonds;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           3D PIECE SDF VOXEL ATLAS SLICE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Evaluates exact convex half-space signed distance d(p) = max_face n_f · (p - v_f) on a 2D slice across the solid/fragments.
export function EvaluatePieceSdfSlice(Fragments, SliceY = 0.0, GridResolution = 64, BoundsRadius = 1.15)
{
    const Field = new Float32Array(GridResolution * GridResolution);
    const PrecomputedPlanes = Fragments.filter((f) => f.Visible !== false).map((Frag) =>
    {
        const Planes = Frag.Complex.Faces.map((Face) =>
        {
            const n = EvaluateFaceNormal(Frag.Complex, Face);
            const v0 = Frag.Complex.Vertices[Face.Indices[0]];
            return { n, d: Dot3(n, v0) };
        });
        return { Planes, Centroid: Frag.Centroid, Radius: Frag.Radius };
    });

    let InsideSamples = 0;
    for (let row = 0; row < GridResolution; ++row)
    {
        const z = ((row / (GridResolution - 1)) * 2.0 - 1.0) * BoundsRadius;
        for (let col = 0; col < GridResolution; ++col)
        {
            const x = ((col / (GridResolution - 1)) * 2.0 - 1.0) * BoundsRadius;
            const p = [x, SliceY, z];
            let MinSignedDist = Infinity;

            for (const Item of PrecomputedPlanes)
            {
                const QuickDist = Dist3(p, Item.Centroid) - Item.Radius;
                if (QuickDist > MinSignedDist) continue;
                let PieceDist = -Infinity;
                for (const pl of Item.Planes)
                {
                    const sd = Dot3(pl.n, p) - pl.d;
                    if (sd > PieceDist) PieceDist = sd;
                }
                if (PieceDist < MinSignedDist) MinSignedDist = PieceDist;
            }

            Field[row * GridResolution + col] = MinSignedDist;
            if (MinSignedDist <= 0) InsideSamples++;
        }
    }

    return {
        Field,
        Resolution: GridResolution,
        OccupancyRatio: InsideSamples / (GridResolution * GridResolution)
    };
}

export function EvaluateSdfAtlasFootprint(FragmentCount, SdfResolution, Enabled)
{
    if (!Enabled)
    {
        return { Bytes: 0, Megabytes: 0, VoxelCount: 0, OccupiedBricks: 0 };
    }
    const VoxelsPerPiece = SdfResolution * SdfResolution * SdfResolution;
    const TotalVoxels = FragmentCount * VoxelsPerPiece;
    const Bytes = TotalVoxels * 2; // R16F half-float distance bricks
    return {
        Bytes,
        Megabytes: Bytes / (1024 * 1024),
        VoxelCount: TotalVoxels,
        OccupiedBricks: Math.min(96, Math.max(1, FragmentCount))
    };
}
