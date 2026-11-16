//============================================================================================================================================
//                                                        OBJECTSURFACESAMPLER.JS
//============================================================================================================================================
// 🧩 Any-Object 3D Mesh Library, OBJ/STL/JSON/Facet Importer & Deterministic Area-Weighted Surface/Volume Sampler for MoGraph Particle Grids.
//    Generates both renderable triangle meshes (WebGPU ConstructGeometry + SoftwarePreviewEngine) and GPU/CPU particle anchor buffers.

export const OBJECT_SURFACES = [
    { id: 0,  label: 'XZ Floor Matrix',               category: 'Primitive', procedural: false },
    { id: 1,  label: 'XY Wall Matrix',                category: 'Primitive', procedural: false },
    { id: 2,  label: '3D Obsidian Sphere',            category: 'Primitive', procedural: false },
    { id: 3,  label: '3D Obsidian Hand + Chrome Ball',category: 'Sculpted',  procedural: false },
    { id: 4,  label: '3D Torus Ring',                 category: 'Primitive', procedural: false },
    { id: 5,  label: '3D Cubic Volume',               category: 'Primitive', procedural: false },
    { id: 6,  label: 'Utah Teapot',                   category: 'Object',    procedural: true  },
    { id: 7,  label: 'Sports Car Silhouette',         category: 'Object',    procedural: true  },
    { id: 8,  label: 'Sculpted Head / Bust',          category: 'Sculpted',  procedural: true  },
    { id: 9,  label: 'Torus Knot (3,2)',              category: 'Sculpted',  procedural: true  },
    { id: 10, label: 'Precision Gear / Cog',          category: 'Object',    procedural: true  },
    { id: 11, label: 'Quartz Crystal Cluster',        category: 'Object',    procedural: true  },
    { id: 12, label: 'Architectural Archway',         category: 'Object',    procedural: true  },
    { id: 13, label: 'Bevelled Monolith Cube',        category: 'Primitive', procedural: true  },
    { id: 14, label: 'Faceted Spire / Cone',          category: 'Primitive', procedural: true  },
    { id: 15, label: 'Stellated Star Polyhedron',     category: 'Sculpted',  procedural: true  },
    { id: 16, label: 'Custom 3D Mesh (.obj/.stl/.json)', category: 'Custom', procedural: true  },
];

export const OBJECT_STYLES = [
    ['Obsidian Dark', 0],
    ['Brushed Chrome', 1],
    ['Ghost Wireframe', 2],
    ['Particles Only (Hidden)', 3],
];

export const OBJECT_SAMPLE_MODES = [
    ['Even Surface', 0],
    ['Surface + Interior Volume', 1],
    ['Ridge & Crease Accent', 2],
];

function Sub3(A, B) { return [A[0] - B[0], A[1] - B[1], A[2] - B[2]]; }
function Cross3(A, B) { return [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]]; }
function Norm3(V) {
    const L = Math.hypot(V[0], V[1], V[2]);
    return L > 1e-8 ? [V[0] / L, V[1] / L, V[2] / L] : [0, 1, 0];
}
function Hash01(Idx, Salt) {
    let X = (Math.imul(Idx | 0, 2654435761) ^ Math.imul(Salt | 0, 2246822519) ^ 0x9e3779b9) >>> 0;
    X = Math.imul(X ^ (X >>> 15), 0x85ebca6b) >>> 0;
    X = Math.imul(X ^ (X >>> 13), 0xc2b2ae35) >>> 0;
    return ((X ^ (X >>> 16)) >>> 0) / 4294967296;
}

// Build normalized triangle list [{ a, b, c, n, area, edgeWeight }] with unit bounding radius ~1.0
function FinalizeMesh(RawTris, AutoNormalize = true) {
    if (!RawTris || !RawTris.length) return { tris: [], totalArea: 0, bounds: { min: [-1, -1, -1], max: [1, 1, 1] } };
    let MinX = Infinity, MinY = Infinity, MinZ = Infinity;
    let MaxX = -Infinity, MaxY = -Infinity, MaxZ = -Infinity;
    for (const T of RawTris) {
        for (const P of [T[0], T[1], T[2]]) {
            if (P[0] < MinX) MinX = P[0]; if (P[0] > MaxX) MaxX = P[0];
            if (P[1] < MinY) MinY = P[1]; if (P[1] > MaxY) MaxY = P[1];
            if (P[2] < MinZ) MinZ = P[2]; if (P[2] > MaxZ) MaxZ = P[2];
        }
    }
    const Cx = (MinX + MaxX) * 0.5, Cy = (MinY + MaxY) * 0.5, Cz = (MinZ + MaxZ) * 0.5;
    const MaxSpan = Math.max(1e-5, MaxX - MinX, MaxY - MinY, MaxZ - MinZ);
    const InvScale = AutoNormalize ? (2.0 / MaxSpan) : 1.0;

    const Tris = [];
    let TotalArea = 0;
    for (const Raw of RawTris) {
        const A = [(Raw[0][0] - Cx) * InvScale, (Raw[0][1] - Cy) * InvScale, (Raw[0][2] - Cz) * InvScale];
        const B = [(Raw[1][0] - Cx) * InvScale, (Raw[1][1] - Cy) * InvScale, (Raw[1][2] - Cz) * InvScale];
        const C = [(Raw[2][0] - Cx) * InvScale, (Raw[2][1] - Cy) * InvScale, (Raw[2][2] - Cz) * InvScale];
        const E1 = Sub3(B, A), E2 = Sub3(C, A);
        const Cr = Cross3(E1, E2);
        const Area = 0.5 * Math.hypot(Cr[0], Cr[1], Cr[2]);
        if (Area < 1e-7) continue;
        const N = Norm3(Cr);
        const Perim = Math.hypot(...E1) + Math.hypot(...E2) + Math.hypot(...Sub3(C, B));
        const EdgeWeight = Area * (0.4 + 0.6 * Math.min(3, Perim / Math.max(0.05, Math.sqrt(Area) * 4)));
        TotalArea += Area;
        Tris.push({ a: A, b: B, c: C, n: N, area: Area, edgeWeight: EdgeWeight });
    }
    return {
        tris: Tris,
        totalArea: TotalArea,
        bounds: {
            min: [(MinX - Cx) * InvScale, (MinY - Cy) * InvScale, (MinZ - Cz) * InvScale],
            max: [(MaxX - Cx) * InvScale, (MaxY - Cy) * InvScale, (MaxZ - Cz) * InvScale],
        },
    };
}

function AddQuad(Tris, A, B, C, D) {
    Tris.push([A, B, C]);
    Tris.push([A, C, D]);
}

function AddLathe(Tris, Profile, Segs = 24, Offset = [0, 0, 0], Scale = [1, 1, 1]) {
    for (let I = 0; I < Profile.length - 1; I++) {
        const [R0, Y0] = Profile[I], [R1, Y1] = Profile[I + 1];
        for (let S = 0; S < Segs; S++) {
            const A0 = (S / Segs) * Math.PI * 2;
            const A1 = ((S + 1) / Segs) * Math.PI * 2;
            const P00 = [Offset[0] + Math.cos(A0) * R0 * Scale[0], Offset[1] + Y0 * Scale[1], Offset[2] + Math.sin(A0) * R0 * Scale[2]];
            const P10 = [Offset[0] + Math.cos(A1) * R0 * Scale[0], Offset[1] + Y0 * Scale[1], Offset[2] + Math.sin(A1) * R0 * Scale[2]];
            const P11 = [Offset[0] + Math.cos(A1) * R1 * Scale[0], Offset[1] + Y1 * Scale[1], Offset[2] + Math.sin(A1) * R1 * Scale[2]];
            const P01 = [Offset[0] + Math.cos(A0) * R1 * Scale[0], Offset[1] + Y1 * Scale[1], Offset[2] + Math.sin(A0) * R1 * Scale[2]];
            if (R0 > 1e-4 && R1 > 1e-4) AddQuad(Tris, P00, P01, P11, P10);
            else if (R0 <= 1e-4 && R1 > 1e-4) Tris.push([P00, P01, P11]);
            else if (R0 > 1e-4 && R1 <= 1e-4) Tris.push([P00, P01, P10]);
        }
    }
}

function AddTubeAlongCurve(Tris, EvalPt, Steps, Sides, EvalRad, Closed = false) {
    const Rings = [];
    for (let I = 0; I <= Steps; I++) {
        const U = I / Steps;
        const C = EvalPt(U);
        const CNext = EvalPt(Math.min(1, U + 0.004));
        const CPrev = EvalPt(Math.max(0, U - 0.004));
        const Tang = Norm3(Sub3(CNext, CPrev));
        const UpRef = Math.abs(Tang[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0];
        const UAx = Norm3(Cross3(Tang, UpRef));
        const VAx = Norm3(Cross3(UAx, Tang));
        const Rad = typeof EvalRad === 'function' ? EvalRad(U) : EvalRad;
        const Rx = Array.isArray(Rad) ? Rad[0] : Rad;
        const Ry = Array.isArray(Rad) ? Rad[1] : Rad;
        const Ring = [];
        for (let S = 0; S < Sides; S++) {
            const Phi = (S / Sides) * Math.PI * 2;
            const Cp = Math.cos(Phi), Sp = Math.sin(Phi);
            Ring.push([
                C[0] + UAx[0] * (Rx * Cp) + VAx[0] * (Ry * Sp),
                C[1] + UAx[1] * (Rx * Cp) + VAx[1] * (Ry * Sp),
                C[2] + UAx[2] * (Rx * Cp) + VAx[2] * (Ry * Sp),
            ]);
        }
        Rings.push(Ring);
    }
    const Limit = Closed ? Steps : Steps;
    for (let I = 0; I < Limit; I++) {
        const R0 = Rings[I], R1 = Rings[(I + 1) % (Steps + 1)];
        for (let S = 0; S < Sides; S++) {
            const Sn = (S + 1) % Sides;
            AddQuad(Tris, R0[S], R0[Sn], R1[Sn], R1[S]);
        }
    }
}

function AddBox(Tris, Cx, Cy, Cz, Hx, Hy, Hz) {
    const P = (sx, sy, sz) => [Cx + sx * Hx, Cy + sy * Hy, Cz + sz * Hz];
    AddQuad(Tris, P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1));
    AddQuad(Tris, P(1, -1, -1), P(-1, -1, -1), P(-1, 1, -1), P(1, 1, -1));
    AddQuad(Tris, P(1, -1, 1), P(1, -1, -1), P(1, 1, -1), P(1, 1, 1));
    AddQuad(Tris, P(-1, -1, -1), P(-1, -1, 1), P(-1, 1, 1), P(-1, 1, -1));
    AddQuad(Tris, P(-1, 1, 1), P(1, 1, 1), P(1, 1, -1), P(-1, 1, -1));
    AddQuad(Tris, P(-1, -1, -1), P(1, -1, -1), P(1, -1, 1), P(-1, -1, 1));
}

function AddExtrudedPolygon(Tris, Poly2D, HalfDepthZ) {
    const N = Poly2D.length;
    if (N < 3) return;
    let Cx = 0, Cy = 0;
    for (const P of Poly2D) { Cx += P[0]; Cy += P[1]; }
    Cx /= N; Cy /= N;
    const Cf = [Cx, Cy, HalfDepthZ], Cb = [Cx, Cy, -HalfDepthZ];
    for (let I = 0; I < N; I++) {
        const A = Poly2D[I], B = Poly2D[(I + 1) % N];
        const Af = [A[0], A[1], HalfDepthZ], Bf = [B[0], B[1], HalfDepthZ];
        const Ab = [A[0], A[1], -HalfDepthZ], Bb = [B[0], B[1], -HalfDepthZ];
        Tris.push([Cf, Af, Bf]);
        Tris.push([Cb, Bb, Ab]);
        AddQuad(Tris, Af, Ab, Bb, Bf);
    }
}

function BuildUtahTeapot() {
    const Tris = [];
    // Classic silhouette body + lid + finial knob
    const Body = [
        [0.0, -0.58], [0.46, -0.58], [0.58, -0.45], [0.76, -0.18], [0.78, 0.08],
        [0.64, 0.34], [0.46, 0.48], [0.50, 0.54], [0.38, 0.55], [0.36, 0.62],
        [0.14, 0.69], [0.06, 0.72], [0.11, 0.80], [0.08, 0.87], [0.0, 0.89],
    ];
    AddLathe(Tris, Body, 26);
    // S-curved pouring spout on +X
    AddTubeAlongCurve(Tris, u => {
        const x = 0.64 + u * 0.58 + Math.sin(u * Math.PI) * 0.08;
        const y = -0.22 + u * 0.78 - Math.sin(u * Math.PI * 1.15) * 0.14;
        return [x, y, 0];
    }, 14, 10, u => 0.16 * (1 - u * 0.56));
    // Swept D-handle on -X
    AddTubeAlongCurve(Tris, u => {
        const a = -Math.PI * 0.52 + u * Math.PI * 1.18;
        const x = -0.64 - Math.cos(a) * 0.44;
        const y = 0.06 + Math.sin(a) * 0.42;
        return [x, y, 0];
    }, 16, 10, u => [0.072, 0.052]);
    return FinalizeMesh(Tris);
}

function BuildSportsCar() {
    const Tris = [];
    // Sculpted wedge supercar lofted along X (-1.05 rear .. +1.05 front)
    const Stations = [
        { x: -1.04, yBot: -0.20, yTop: 0.16, yCab: 0.18, wBot: 0.44, wTop: 0.40, wCab: 0.34 },
        { x: -0.82, yBot: -0.22, yTop: 0.20, yCab: 0.28, wBot: 0.49, wTop: 0.44, wCab: 0.34 },
        { x: -0.45, yBot: -0.22, yTop: 0.21, yCab: 0.44, wBot: 0.48, wTop: 0.44, wCab: 0.32 },
        { x:  0.02, yBot: -0.22, yTop: 0.20, yCab: 0.45, wBot: 0.47, wTop: 0.43, wCab: 0.31 },
        { x:  0.42, yBot: -0.22, yTop: 0.18, yCab: 0.22, wBot: 0.48, wTop: 0.43, wCab: 0.34 },
        { x:  0.80, yBot: -0.21, yTop: 0.12, yCab: 0.13, wBot: 0.47, wTop: 0.41, wCab: 0.34 },
        { x:  1.06, yBot: -0.18, yTop: 0.02, yCab: 0.03, wBot: 0.40, wTop: 0.35, wCab: 0.28 },
    ];
    const RingFor = S => [
        [S.x, S.yBot, -S.wBot], [S.x, S.yTop, -S.wTop], [S.x, S.yCab, -S.wCab],
        [S.x, S.yCab,  S.wCab], [S.x, S.yTop,  S.wTop], [S.x, S.yBot,  S.wBot],
    ];
    const Rings = Stations.map(RingFor);
    for (let I = 0; I < Rings.length - 1; I++) {
        const A = Rings[I], B = Rings[I + 1];
        for (let K = 0; K < A.length; K++) {
            const Kn = (K + 1) % A.length;
            AddQuad(Tris, A[K], A[Kn], B[Kn], B[K]);
        }
    }
    // End caps
    for (const [R, Flip] of [[Rings[0], true], [Rings[Rings.length - 1], false]]) {
        const C = [R[0][0], (R[0][1] + R[2][1]) * 0.5, 0];
        for (let K = 0; K < R.length; K++) {
            const Kn = (K + 1) % R.length;
            Tris.push(Flip ? [C, R[Kn], R[K]] : [C, R[K], R[Kn]]);
        }
    }
    // Rear wing spoiler
    AddBox(Tris, -0.96, 0.30, 0, 0.11, 0.025, 0.44);
    AddBox(Tris, -0.94, 0.22, -0.24, 0.04, 0.07, 0.02);
    AddBox(Tris, -0.94, 0.22,  0.24, 0.04, 0.07, 0.02);
    // Four wheel cylinders
    for (const [Wx, Wz] of [[-0.64, -0.46], [-0.64, 0.46], [0.64, -0.46], [0.64, 0.46]]) {
        const Segs = 14, Wr = 0.20, Wh = 0.08;
        for (let S = 0; S < Segs; S++) {
            const A0 = (S / Segs) * Math.PI * 2, A1 = ((S + 1) / Segs) * Math.PI * 2;
            const P0i = [Wx + Math.cos(A0) * Wr, -0.18 + Math.sin(A0) * Wr, Wz - Wh];
            const P1i = [Wx + Math.cos(A1) * Wr, -0.18 + Math.sin(A1) * Wr, Wz - Wh];
            const P0o = [Wx + Math.cos(A0) * Wr, -0.18 + Math.sin(A0) * Wr, Wz + Wh];
            const P1o = [Wx + Math.cos(A1) * Wr, -0.18 + Math.sin(A1) * Wr, Wz + Wh];
            AddQuad(Tris, P0i, P0o, P1o, P1i);
            Tris.push([[Wx, -0.18, Wz + Wh], P0o, P1o]);
            Tris.push([[Wx, -0.18, Wz - Wh], P1i, P0i]);
        }
    }
    return FinalizeMesh(Tris);
}

function BuildSculptedBust() {
    const Tris = [];
    // Anatomical head + neck + draped shoulders sculpted via modulated spherical/lathe field
    const Rings = 22, Segs = 26;
    const Grid = [];
    for (let R = 0; R <= Rings; R++) {
        const V = R / Rings; // 0 bottom pedestal -> 1 crown
        const Y = -0.95 + V * 1.92;
        const Row = [];
        for (let S = 0; S < Segs; S++) {
            const Phi = (S / Segs) * Math.PI * 2;
            const C = Math.cos(Phi), Sn = Math.sin(Phi); // +Z = face front (Sn > 0)
            let Rx = 0.42, Rz = 0.42;
            if (V < 0.24) {
                // Sculpted shoulders & collar pedestal
                const T = V / 0.24;
                Rx = 0.78 - T * 0.46;
                Rz = 0.38 - T * 0.12;
            } else if (V < 0.38) {
                // Neck cylinder
                const T = (V - 0.24) / 0.14;
                Rx = 0.25 + T * 0.04;
                Rz = 0.24 + T * 0.05;
            } else {
                // Cranium + jawline + facial planes
                const H = (V - 0.38) / 0.62; // 0 chin -> 1 crown
                const Profile = Math.sin(Math.pow(H, 0.85) * Math.PI);
                Rx = 0.46 * Profile * (H < 0.32 ? (0.78 + H * 0.7) : 1.0);
                Rz = 0.54 * Profile;
                // Nose bridge ridge (+Z front center)
                if (Sn > 0.65 && H > 0.26 && H < 0.56) {
                    const NoseW = Math.pow(Math.max(0, (Sn - 0.65) / 0.35), 2.2);
                    const NoseH = Math.sin(((H - 0.26) / 0.30) * Math.PI);
                    Rz += 0.12 * NoseW * NoseH;
                }
                // Brow ridge & eye sockets
                if (Sn > 0.35 && H > 0.46 && H < 0.62) {
                    const Brow = Math.sin(((H - 0.46) / 0.16) * Math.PI);
                    Rz += 0.045 * Brow * Math.max(0, Sn - 0.35);
                }
                // Chin taper
                if (Sn > 0.4 && H < 0.18) {
                    Rz += 0.06 * (1 - H / 0.18) * Sn;
                }
            }
            Row.push([C * Rx, Y, Sn * Rz]);
        }
        Grid.push(Row);
    }
    for (let R = 0; R < Rings; R++) {
        for (let S = 0; S < Segs; S++) {
            const Sn = (S + 1) % Segs;
            AddQuad(Tris, Grid[R][S], Grid[R + 1][S], Grid[R + 1][Sn], Grid[R][Sn]);
        }
    }
    // Base cap
    const BaseC = [0, -0.95, 0];
    for (let S = 0; S < Segs; S++) Tris.push([BaseC, Grid[0][(S + 1) % Segs], Grid[0][S]]);
    return FinalizeMesh(Tris);
}

function BuildTorusKnot() {
    const Tris = [];
    const P = 3, Q = 2;
    AddTubeAlongCurve(Tris, u => {
        const T = u * Math.PI * 2;
        const R = 0.62 + 0.26 * Math.cos(P * T);
        return [R * Math.cos(Q * T), 0.32 * Math.sin(P * T), R * Math.sin(Q * T)];
    }, 64, 12, 0.165, true);
    return FinalizeMesh(Tris);
}

function BuildGear() {
    const Tris = [];
    const Teeth = 12;
    const Poly = [];
    for (let I = 0; I < Teeth; I++) {
        const Base = (I / Teeth) * Math.PI * 2;
        const Step = (Math.PI * 2) / Teeth;
        const Pts = [
            [0.68, Base + Step * 0.06],
            [0.96, Base + Step * 0.18],
            [0.96, Base + Step * 0.42],
            [0.68, Base + Step * 0.54],
        ];
        for (const [R, A] of Pts) Poly.push([Math.cos(A) * R, Math.sin(A) * R]);
    }
    AddExtrudedPolygon(Tris, Poly, 0.20);
    // Center raised boss hub
    const Hub = [];
    for (let I = 0; I < 16; I++) {
        const A = (I / 16) * Math.PI * 2;
        Hub.push([Math.cos(A) * 0.34, Math.sin(A) * 0.34]);
    }
    AddExtrudedPolygon(Tris, Hub, 0.32);
    return FinalizeMesh(Tris);
}

function BuildCrystalCluster() {
    const Tris = [];
    const Shards = [
        { ox: 0.0, oz: 0.0, tiltX: 0.04, tiltZ: -0.02, r: 0.30, h: 1.55 },
        { ox: -0.34, oz: 0.16, tiltX: -0.34, tiltZ: 0.16, r: 0.22, h: 1.15 },
        { ox: 0.36, oz: -0.14, tiltX: 0.32, tiltZ: -0.18, r: 0.24, h: 1.22 },
        { ox: 0.12, oz: 0.36, tiltX: 0.12, tiltZ: 0.36, r: 0.19, h: 0.96 },
        { ox: -0.22, oz: -0.32, tiltX: -0.22, tiltZ: -0.32, r: 0.18, h: 0.92 },
    ];
    for (const Sh of Shards) {
        const Sides = 6;
        const BaseY = -0.65;
        const TopY = BaseY + Sh.h * 0.72;
        const TipY = BaseY + Sh.h;
        const Bot = [], Top = [];
        for (let S = 0; S < Sides; S++) {
            const A = (S / Sides) * Math.PI * 2;
            const Lx = Math.cos(A) * Sh.r, Lz = Math.sin(A) * Sh.r;
            Bot.push([Sh.ox + Lx, BaseY, Sh.oz + Lz]);
            Top.push([Sh.ox + Lx * 0.84 + Sh.tiltX * Sh.h * 0.72, TopY, Sh.oz + Lz * 0.84 + Sh.tiltZ * Sh.h * 0.72]);
        }
        const Tip = [Sh.ox + Sh.tiltX * Sh.h, TipY, Sh.oz + Sh.tiltZ * Sh.h];
        const BotC = [Sh.ox, BaseY, Sh.oz];
        for (let S = 0; S < Sides; S++) {
            const Sn = (S + 1) % Sides;
            AddQuad(Tris, Bot[S], Top[S], Top[Sn], Bot[Sn]);
            Tris.push([Top[S], Tip, Top[Sn]]);
            Tris.push([BotC, Bot[Sn], Bot[S]]);
        }
    }
    return FinalizeMesh(Tris);
}

function BuildArchway() {
    const Tris = [];
    // Left and right pillars + semicircular barrel vault arch
    AddBox(Tris, -0.62, -0.35, 0, 0.22, 0.65, 0.28);
    AddBox(Tris,  0.62, -0.35, 0, 0.22, 0.65, 0.28);
    AddBox(Tris, -0.62, -0.94, 0, 0.28, 0.08, 0.34);
    AddBox(Tris,  0.62, -0.94, 0, 0.28, 0.08, 0.34);
    const Segs = 16, Ri = 0.40, Ro = 0.84, Hz = 0.28;
    for (let I = 0; I < Segs; I++) {
        const A0 = (I / Segs) * Math.PI, A1 = ((I + 1) / Segs) * Math.PI;
        const Pi0f = [Math.cos(A0) * Ri, 0.30 + Math.sin(A0) * Ri,  Hz];
        const Pi1f = [Math.cos(A1) * Ri, 0.30 + Math.sin(A1) * Ri,  Hz];
        const Po0f = [Math.cos(A0) * Ro, 0.30 + Math.sin(A0) * Ro,  Hz];
        const Po1f = [Math.cos(A1) * Ro, 0.30 + Math.sin(A1) * Ro,  Hz];
        const Pi0b = [Pi0f[0], Pi0f[1], -Hz], Pi1b = [Pi1f[0], Pi1f[1], -Hz];
        const Po0b = [Po0f[0], Po0f[1], -Hz], Po1b = [Po1f[0], Po1f[1], -Hz];
        AddQuad(Tris, Pi0f, Po0f, Po1f, Pi1f);
        AddQuad(Tris, Po0b, Pi0b, Pi1b, Po1b);
        AddQuad(Tris, Po0f, Po0b, Po1b, Po1f);
        AddQuad(Tris, Pi0b, Pi0f, Pi1f, Pi1b);
    }
    return FinalizeMesh(Tris);
}

function BuildBevelCube() {
    const Tris = [];
    // Subdivided bevelled cube (smoothly chamfered superellipsoid feel)
    const Sub = 6;
    const Faces = [
        { n: [1, 0, 0], u: [0, 1, 0], v: [0, 0, 1] },
        { n: [-1, 0, 0], u: [0, 1, 0], v: [0, 0, -1] },
        { n: [0, 1, 0], u: [1, 0, 0], v: [0, 0, -1] },
        { n: [0, -1, 0], u: [1, 0, 0], v: [0, 0, 1] },
        { n: [0, 0, 1], u: [1, 0, 0], v: [0, 1, 0] },
        { n: [0, 0, -1], u: [-1, 0, 0], v: [0, 1, 0] },
    ];
    const Bevel = (x, y, z) => {
        const P = 4.5;
        const Den = Math.pow(Math.pow(Math.abs(x), P) + Math.pow(Math.abs(y), P) + Math.pow(Math.abs(z), P), 1 / P) || 1;
        return [x / Den, y / Den, z / Den];
    };
    for (const F of Faces) {
        const Grid = [];
        for (let I = 0; I <= Sub; I++) {
            const Row = [];
            const Tu = (I / Sub) * 2 - 1;
            for (let J = 0; J <= Sub; J++) {
                const Tv = (J / Sub) * 2 - 1;
                Row.push(Bevel(F.n[0] + F.u[0] * Tu + F.v[0] * Tv, F.n[1] + F.u[1] * Tu + F.v[1] * Tv, F.n[2] + F.u[2] * Tu + F.v[2] * Tv));
            }
            Grid.push(Row);
        }
        for (let I = 0; I < Sub; I++) {
            for (let J = 0; J < Sub; J++) {
                AddQuad(Tris, Grid[I][J], Grid[I + 1][J], Grid[I + 1][J + 1], Grid[I][J + 1]);
            }
        }
    }
    return FinalizeMesh(Tris);
}

function BuildConeSpire() {
    const Tris = [];
    const Segs = 20, Rings = 6;
    const Tip = [0, 1.0, 0], BaseC = [0, -0.85, 0];
    const Grid = [];
    for (let R = 0; R <= Rings; R++) {
        const T = R / Rings;
        const Rad = (1 - T) * 0.85;
        const Y = -0.85 + T * 1.85;
        const Row = [];
        for (let S = 0; S < Segs; S++) {
            const A = (S / Segs) * Math.PI * 2;
            Row.push([Math.cos(A) * Rad, Y, Math.sin(A) * Rad]);
        }
        Grid.push(Row);
    }
    for (let R = 0; R < Rings - 1; R++) {
        for (let S = 0; S < Segs; S++) {
            const Sn = (S + 1) % Segs;
            AddQuad(Tris, Grid[R][S], Grid[R + 1][S], Grid[R + 1][Sn], Grid[R][Sn]);
        }
    }
    for (let S = 0; S < Segs; S++) {
        const Sn = (S + 1) % Segs;
        Tris.push([Grid[Rings - 1][S], Tip, Grid[Rings - 1][Sn]]);
        Tris.push([BaseC, Grid[0][Sn], Grid[0][S]]);
    }
    return FinalizeMesh(Tris);
}

function BuildStarPolyhedron() {
    const Tris = [];
    // Stellated octahedron (Stella Octangula) — 8 crisp 3D pyramidal spikes on an octahedral core
    const Core = [
        [1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1],
    ].map(V => V.map(C => C * 0.52));
    const OctFaces = [
        [0, 2, 4], [4, 2, 1], [1, 2, 5], [5, 2, 0],
        [4, 3, 0], [1, 3, 4], [5, 3, 1], [0, 3, 5],
    ];
    for (const [I0, I1, I2] of OctFaces) {
        const A = Core[I0], B = Core[I1], C = Core[I2];
        const Mid = [(A[0] + B[0] + C[0]) / 3, (A[1] + B[1] + C[1]) / 3, (A[2] + B[2] + C[2]) / 3];
        const N = Norm3(Cross3(Sub3(B, A), Sub3(C, A)));
        const Apex = [Mid[0] + N[0] * 0.85, Mid[1] + N[1] * 0.85, Mid[2] + N[2] * 0.85];
        Tris.push([A, B, Apex]);
        Tris.push([B, C, Apex]);
        Tris.push([C, A, Apex]);
    }
    return FinalizeMesh(Tris);
}

const BuiltInCache = new Map();

export function GetProceduralObjectMesh(SurfId) {
    if (BuiltInCache.has(SurfId)) return BuiltInCache.get(SurfId);
    let Mesh = null;
    if (SurfId === 6) Mesh = BuildUtahTeapot();
    else if (SurfId === 7) Mesh = BuildSportsCar();
    else if (SurfId === 8) Mesh = BuildSculptedBust();
    else if (SurfId === 9) Mesh = BuildTorusKnot();
    else if (SurfId === 10) Mesh = BuildGear();
    else if (SurfId === 11) Mesh = BuildCrystalCluster();
    else if (SurfId === 12) Mesh = BuildArchway();
    else if (SurfId === 13) Mesh = BuildBevelCube();
    else if (SurfId === 14) Mesh = BuildConeSpire();
    else if (SurfId === 15) Mesh = BuildStarPolyhedron();
    else Mesh = BuildUtahTeapot();
    BuiltInCache.set(SurfId, Mesh);
    return Mesh;
}

// Parse Wavefront .OBJ text -> FinalizeMesh
export function ParseOBJ(Text) {
    const Verts = [];
    const RawTris = [];
    const Lines = String(Text || '').split(/\r?\n/);
    for (const Line of Lines) {
        const S = Line.trim();
        if (!S || S[0] === '#') continue;
        if (S.startsWith('v ')) {
            const P = S.split(/\s+/);
            if (P.length >= 4) {
                const X = parseFloat(P[1]), Y = parseFloat(P[2]), Z = parseFloat(P[3]);
                if (Number.isFinite(X) && Number.isFinite(Y) && Number.isFinite(Z)) Verts.push([X, Y, Z]);
            }
        } else if (S.startsWith('f ')) {
            const Tokens = S.split(/\s+/).slice(1);
            const Idxs = [];
            for (const Tok of Tokens) {
                const RawIdx = parseInt(Tok.split('/')[0], 10);
                if (!Number.isFinite(RawIdx)) continue;
                const Idx = RawIdx < 0 ? Verts.length + RawIdx : RawIdx - 1;
                if (Idx >= 0 && Idx < Verts.length) Idxs.push(Idx);
            }
            for (let K = 1; K + 1 < Idxs.length; K++) {
                RawTris.push([Verts[Idxs[0]], Verts[Idxs[K]], Verts[Idxs[K + 1]]]);
            }
        }
    }
    return FinalizeMesh(RawTris);
}

// Parse ASCII or Binary .STL -> FinalizeMesh
export function ParseSTL(BufferOrText) {
    const RawTris = [];
    if (BufferOrText instanceof ArrayBuffer) {
        const Bytes = new Uint8Array(BufferOrText);
        if (Bytes.byteLength >= 84) {
            const View = new DataView(BufferOrText);
            const TriCount = View.getUint32(80, true);
            if (84 + TriCount * 50 === Bytes.byteLength && TriCount > 0 && TriCount < 500000) {
                let Off = 84;
                for (let I = 0; I < TriCount; I++) {
                    Off += 12; // skip stored normal
                    const A = [View.getFloat32(Off, true), View.getFloat32(Off + 4, true), View.getFloat32(Off + 8, true)]; Off += 12;
                    const B = [View.getFloat32(Off, true), View.getFloat32(Off + 4, true), View.getFloat32(Off + 8, true)]; Off += 12;
                    const C = [View.getFloat32(Off, true), View.getFloat32(Off + 4, true), View.getFloat32(Off + 8, true)]; Off += 12;
                    Off += 2; // attr bytes
                    RawTris.push([A, B, C]);
                }
                return FinalizeMesh(RawTris);
            }
        }
        BufferOrText = new TextDecoder().decode(Bytes);
    }
    const Text = String(BufferOrText || '');
    const Matches = [...Text.matchAll(/vertex\s+([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s+([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s+([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)/gi)];
    for (let I = 0; I + 2 < Matches.length; I += 3) {
        const P = M => [parseFloat(M[1]), parseFloat(M[2]), parseFloat(M[3])];
        RawTris.push([P(Matches[I]), P(Matches[I + 1]), P(Matches[I + 2])]);
    }
    return FinalizeMesh(RawTris);
}

// Parse JSON ({ vertices: [x,y,z,...] or [[x,y,z],...], indices: [...] }) or Frontier .facet JSON/text
export function ParseCustomMeshFile(FileName, BufferOrText) {
    const Lower = String(FileName || '').toLowerCase();
    if (Lower.endsWith('.stl')) return ParseSTL(BufferOrText);
    const Text = BufferOrText instanceof ArrayBuffer ? new TextDecoder().decode(new Uint8Array(BufferOrText)) : String(BufferOrText || '');
    if (Lower.endsWith('.obj')) return ParseOBJ(Text);
    if (Lower.endsWith('.json') || Lower.endsWith('.facet')) {
        try {
            const Data = JSON.parse(Text);
            const RawTris = [];
            const VRaw = Data.vertices || Data.positions || Data.Vertices || Data.Positions;
            const IRaw = Data.indices || Data.faces || Data.triangles || Data.Indices || Data.Faces;
            if (Array.isArray(VRaw)) {
                const Verts = Array.isArray(VRaw[0])
                    ? VRaw.map(V => [Number(V[0]) || 0, Number(V[1]) || 0, Number(V[2]) || 0])
                    : Array.from({ length: Math.floor(VRaw.length / 3) }, (_, I) => [Number(VRaw[I * 3]) || 0, Number(VRaw[I * 3 + 1]) || 0, Number(VRaw[I * 3 + 2]) || 0]);
                if (Array.isArray(IRaw) && IRaw.length) {
                    if (Array.isArray(IRaw[0])) {
                        for (const F of IRaw) {
                            for (let K = 1; K + 1 < F.length; K++) {
                                if (Verts[F[0]] && Verts[F[K]] && Verts[F[K + 1]]) RawTris.push([Verts[F[0]], Verts[F[K]], Verts[F[K + 1]]]);
                            }
                        }
                    } else {
                        for (let I = 0; I + 2 < IRaw.length; I += 3) {
                            if (Verts[IRaw[I]] && Verts[IRaw[I + 1]] && Verts[IRaw[I + 2]]) RawTris.push([Verts[IRaw[I]], Verts[IRaw[I + 1]], Verts[IRaw[I + 2]]]);
                        }
                    }
                } else {
                    for (let I = 0; I + 2 < Verts.length; I += 3) RawTris.push([Verts[I], Verts[I + 1], Verts[I + 2]]);
                }
                if (RawTris.length) return FinalizeMesh(RawTris);
            }
        } catch {
            // Fall through to OBJ parser for text-based geometry
        }
    }
    return ParseOBJ(Text);
}

// Custom uploaded mesh registry keyed by id (kept out of JSON serialization bloat)
const CustomMeshRegistry = new Map();
let CustomMeshSeq = 1;

export function RegisterCustomMesh(Mesh, Name = 'Custom Mesh') {
    if (!Mesh || !Mesh.tris || !Mesh.tris.length) return null;
    const Id = 'mesh_' + (CustomMeshSeq++);
    CustomMeshRegistry.set(Id, { mesh: Mesh, name: Name, triCount: Mesh.tris.length });
    return Id;
}

export function GetCustomMeshEntry(MeshId) {
    return MeshId ? (CustomMeshRegistry.get(MeshId) || null) : null;
}

export function ResolveObjectMesh(P) {
    const Surf = Math.max(0, Math.min(16, Math.round(P?.gridSurface ?? 0)));
    if (Surf < 6) return null;
    if (Surf === 16) {
        const Entry = GetCustomMeshEntry(P?.customMeshId);
        if (Entry?.mesh?.tris?.length) return Entry.mesh;
        return GetProceduralObjectMesh(6); // clean fallback to Utah Teapot until user uploads a file
    }
    return GetProceduralObjectMesh(Surf);
}

// Apply Euler XYZ rotation (degrees) to a 3D point/normal
export function RotateEulerXYZ(V, Deg) {
    const Rx = ((Deg?.[0] || 0) * Math.PI) / 180;
    const Ry = ((Deg?.[1] || 0) * Math.PI) / 180;
    const Rz = ((Deg?.[2] || 0) * Math.PI) / 180;
    let [x, y, z] = V;
    if (Math.abs(Rx) > 1e-6) {
        const c = Math.cos(Rx), s = Math.sin(Rx);
        const ny = y * c - z * s, nz = y * s + z * c;
        y = ny; z = nz;
    }
    if (Math.abs(Ry) > 1e-6) {
        const c = Math.cos(Ry), s = Math.sin(Ry);
        const nx = x * c + z * s, nz = -x * s + z * c;
        x = nx; z = nz;
    }
    if (Math.abs(Rz) > 1e-6) {
        const c = Math.cos(Rz), s = Math.sin(Rz);
        const nx = x * c - y * s, ny = x * s + y * c;
        x = nx; y = ny;
    }
    return [x, y, z];
}

const SampleCache = new Map();

export function GetSampleCacheKey(P, Cap) {
    const Surf = Math.max(0, Math.min(16, Math.round(P?.gridSurface ?? 0)));
    const Rot = P?.objRot || [0, 0, 0];
    const Mode = Math.round(P?.objSampleMode ?? 0);
    return `${Surf}|${P?.customMeshId || ''}|${Cap}|${Mode}|${Math.round(Rot[0] * 2)}|${Math.round(Rot[1] * 2)}|${Math.round(Rot[2] * 2)}`;
}

// Returns { key, anchors: [{ pos, norm, localY, pitch }], snapBuffer: Float32Array(cap * 20), mesh }
export function SampleObjectAnchors(P, Cap) {
    const Mesh = ResolveObjectMesh(P);
    if (!Mesh || !Mesh.tris.length) return null;
    const Key = GetSampleCacheKey(P, Cap);
    if (SampleCache.has(Key)) return SampleCache.get(Key);

    const Mode = Math.round(P?.objSampleMode ?? 0);
    const Rot = P?.objRot || [0, 0, 0];
    const Tris = Mesh.tris;
    const Cdf = new Float64Array(Tris.length);
    let Sum = 0;
    for (let I = 0; I < Tris.length; I++) {
        const W = Mode === 2 ? Tris[I].edgeWeight : Tris[I].area;
        Sum += W;
        Cdf[I] = Sum;
    }
    const InvSum = Sum > 1e-9 ? 1 / Sum : 1;
    for (let I = 0; I < Tris.length; I++) Cdf[I] *= InvSum;

    const Pitch = Math.max(0.03, Math.sqrt(Math.max(0.2, Mesh.totalArea) / Math.max(16, Cap)) * 1.15);
    const Anchors = new Array(Cap);
    const Snap = new Float32Array(Cap * 20); // 80 bytes per Part slot matching WGSL struct Part

    for (let I = 0; I < Cap; I++) {
        // Stratified quasirandom selection across CDF for uniform surface coverage
        const UArea = (I + Hash01(I, 11) * 0.92) / Math.max(1, Cap);
        let Lo = 0, Hi = Tris.length - 1;
        while (Lo < Hi) {
            const Mid = (Lo + Hi) >> 1;
            if (Cdf[Mid] < UArea) Lo = Mid + 1;
            else Hi = Mid;
        }
        const Tri = Tris[Lo];
        const R1 = Math.sqrt(Hash01(I, 23));
        const R2 = Hash01(I, 37);
        const Wa = 1 - R1, Wb = R1 * (1 - R2), Wc = R1 * R2;
        let Lx = Tri.a[0] * Wa + Tri.b[0] * Wb + Tri.c[0] * Wc;
        let Ly = Tri.a[1] * Wa + Tri.b[1] * Wb + Tri.c[1] * Wc;
        let Lz = Tri.a[2] * Wa + Tri.b[2] * Wb + Tri.c[2] * Wc;
        let Norm = Tri.n;
        // Mode 1: Surface + Interior Volume (pull ~35% of particles smoothly inward toward centroid)
        if (Mode === 1 && (I % 3 === 0)) {
            const Depth = 0.25 + 0.68 * Math.cbrt(Hash01(I, 53));
            Lx *= Depth; Ly *= Depth; Lz *= Depth;
        }
        const Rp = RotateEulerXYZ([Lx, Ly, Lz], Rot);
        const Rn = Norm3(RotateEulerXYZ(Norm, Rot));
        const LocalY = Rp[1];
        Anchors[I] = { pos: Rp, norm: Rn, localY: LocalY, pitch: Pitch };

        // Pack into WGSL struct Part layout (p: vec4f, v: vec4f, c: vec4f, m: vec4f, e: vec4f)
        const O = I * 20;
        Snap[O + 0] = Rp[0]; Snap[O + 1] = Rp[1]; Snap[O + 2] = Rp[2]; Snap[O + 3] = LocalY;
        Snap[O + 16] = Rn[0]; Snap[O + 17] = Rn[1]; Snap[O + 18] = Rn[2]; Snap[O + 19] = Pitch;
    }

    if (SampleCache.size > 24) {
        const FirstKey = SampleCache.keys().next().value;
        SampleCache.delete(FirstKey);
    }
    const Result = { key: Key, anchors: Anchors, snapBuffer: Snap, mesh: Mesh };
    SampleCache.set(Key, Result);
    return Result;
}
