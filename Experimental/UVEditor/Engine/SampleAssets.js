/*==========================================================================================================================================
                                                           SAMPLEASSETS.JS
==========================================================================================================================================*/
// Procedural multi-island 3D quad/triangle assets with clean UV seams, part metadata, UDIM configurations,
// and realistic importance-packing presets so every UV tool, seam unwrap, and UDIM workflow works immediately.

import { TopologySurface, EdgeKey } from './TopologySurface.js';
import { SmartUvProject } from './UnwrapSolver.js';
import { SolveImportancePack } from './ImportancePackingSolver.js';
import { CreateControlledIntegratorAsset } from './ControlledIntegratorLoader.js';

class SurfaceAssembler
{
    constructor(Name)
    {
        this.name = Name;
        this.positions = [];
        this.uvCoords = [];
        this.faces = [];
        this.seams = new Set();
        this.importanceByFace = new Map();
    }

    AddVertex(X, Y, Z)
    {
        const Idx = this.positions.length;
        this.positions.push([X, Y, Z]);
        return Idx;
    }

    AddUV(U, V)
    {
        const Idx = this.uvCoords.length;
        this.uvCoords.push([U, V]);
        return Idx;
    }

    AddQuad(V0, V1, V2, V3, { partId, materialName = 'Default', uvs = null, group = null } = {})
    {
        let UvIdx;
        if (uvs)
        {
            UvIdx = uvs.map(([U, V]) => this.AddUV(U, V));
        }
        else
        {
            UvIdx = [this.AddUV(0, 0), this.AddUV(1, 0), this.AddUV(1, 1), this.AddUV(0, 1)];
        }
        const Fi = this.faces.length;
        this.faces.push({
            id: Fi,
            name: partId,
            partId,
            materialName,
            corners: [
                { v: V0, uv: UvIdx[0] },
                { v: V1, uv: UvIdx[1] },
                { v: V2, uv: UvIdx[2] },
                { v: V3, uv: UvIdx[3] }
            ]
        });
        if (group) this.importanceByFace.set(Fi, group);
        return Fi;
    }

    AddBoxPatch({ cx = 0, cy = 0, cz = 0, sx = 1, sy = 1, sz = 1, partId = 'Box', materialName = 'Metal', group = null, separateFaces = false })
    {
        const Hx = sx * 0.5, Hy = sy * 0.5, Hz = sz * 0.5;
        const P = [
            this.AddVertex(cx - Hx, cy - Hy, cz - Hz), // 0: ---
            this.AddVertex(cx + Hx, cy - Hy, cz - Hz), // 1: +--
            this.AddVertex(cx + Hx, cy + Hy, cz - Hz), // 2: ++-
            this.AddVertex(cx - Hx, cy + Hy, cz - Hz), // 3: -+-
            this.AddVertex(cx - Hx, cy - Hy, cz + Hz), // 4: --+
            this.AddVertex(cx + Hx, cy - Hy, cz + Hz), // 5: +-+
            this.AddVertex(cx + Hx, cy + Hy, cz + Hz), // 6: +++
            this.AddVertex(cx - Hx, cy + Hy, cz + Hz)  // 7: -++
        ];

        // Unfolded net (cross pattern) or individual face patches if separateFaces
        if (separateFaces)
        {
            const FacesSpec = [
                [P[4], P[5], P[6], P[7], sx, sy],
                [P[1], P[0], P[3], P[2], sx, sy],
                [P[3], P[2], P[6], P[7], sx, sz],
                [P[0], P[1], P[5], P[4], sx, sz],
                [P[5], P[1], P[2], P[6], sz, sy],
                [P[0], P[4], P[7], P[3], sz, sy]
            ];
            for (const [A, B, C, D, W, H] of FacesSpec)
            {
                this.AddQuad(A, B, C, D, {
                    partId,
                    materialName,
                    group,
                    uvs: [[0, 0], [W * 0.35, 0], [W * 0.35, H * 0.35], [0, H * 0.35]]
                });
            }
            return;
        }

        // Clean connected cross-unfolded UV shell (5 side faces welded to top/front, bottom split by seams)
        const S = 0.16;
        const UvPts = {
            t0: this.AddUV(0, 0),       t1: this.AddUV(sx * S, 0),
            t2: this.AddUV(sx * S, sz * S), t3: this.AddUV(0, sz * S),
            f0: this.AddUV(0, -sy * S), f1: this.AddUV(sx * S, -sy * S),
            b2: this.AddUV(sx * S, sz * S + sy * S), b3: this.AddUV(0, sz * S + sy * S),
            l0: this.AddUV(-sy * S, 0), l3: this.AddUV(-sy * S, sz * S),
            r1: this.AddUV((sx + sy) * S, 0), r2: this.AddUV((sx + sy) * S, sz * S),
            bot0: this.AddUV(0, -(sy + sz) * S), bot1: this.AddUV(sx * S, -(sy + sz) * S)
        };
        const Push = (A, B, C, D, Ua, Ub, Uc, Ud) =>
        {
            const Fi = this.faces.length;
            this.faces.push({
                id: Fi, name: partId, partId, materialName,
                corners: [{ v: A, uv: Ua }, { v: B, uv: Ub }, { v: C, uv: Uc }, { v: D, uv: Ud }]
            });
            if (group) this.importanceByFace.set(Fi, group);
        };
        // Top (+Y): 7,6,2,3 -> t0,t1,t2,t3
        Push(P[7], P[6], P[2], P[3], UvPts.t0, UvPts.t1, UvPts.t2, UvPts.t3);
        // Front (+Z): 4,5,6,7 -> f0,f1,t1,t0
        Push(P[4], P[5], P[6], P[7], UvPts.f0, UvPts.f1, UvPts.t1, UvPts.t0);
        // Back (-Z): 3,2,1,0 -> t3,t2,b2,b3 (outward -Z normal and CCW UV winding)
        Push(P[3], P[2], P[1], P[0], UvPts.t3, UvPts.t2, UvPts.b2, UvPts.b3);
        // Left (-X): 0,4,7,3
        Push(P[0], P[4], P[7], P[3], UvPts.l3, UvPts.l0, UvPts.t0, UvPts.t3);
        // Right (+X): 5,1,2,6
        Push(P[5], P[1], P[2], P[6], UvPts.r1, UvPts.r2, UvPts.t2, UvPts.t1);
        // Bottom (-Y): 0,1,5,4
        Push(P[0], P[1], P[5], P[4], UvPts.bot0, UvPts.bot1, UvPts.f1, UvPts.f0);

        // Mark seams along the 7 cut edges of the unfolded cube
        const CutEdges = [
            [P[4], P[0]], [P[5], P[1]], [P[0], P[3]], [P[1], P[2]],
            [P[0], P[1]], [P[4], P[7]], [P[5], P[6]]
        ];
        for (const [A, B] of CutEdges) this.seams.add(EdgeKey(A, B));
    }

    AddCurvedPanel({ rows, cols, pointFn, partId, materialName = 'Paint', group = null, uScale = 1, vScale = 1 })
    {
        const VGrid = [];
        const UGrid = [];
        for (let r = 0; r <= rows; r++)
        {
            const VRow = [], URow = [];
            const Vt = r / rows;
            for (let c = 0; c <= cols; c++)
            {
                const Ut = c / cols;
                const [X, Y, Z] = pointFn(Ut, Vt);
                VRow.push(this.AddVertex(X, Y, Z));
                URow.push(this.AddUV(Ut * uScale, Vt * vScale));
            }
            VGrid.push(VRow);
            UGrid.push(URow);
        }
        for (let r = 0; r < rows; r++)
        {
            for (let c = 0; c < cols; c++)
            {
                const Fi = this.faces.length;
                this.faces.push({
                    id: Fi, name: partId, partId, materialName,
                    corners: [
                        { v: VGrid[r][c], uv: UGrid[r][c] },
                        { v: VGrid[r][c + 1], uv: UGrid[r][c + 1] },
                        { v: VGrid[r + 1][c + 1], uv: UGrid[r + 1][c + 1] },
                        { v: VGrid[r + 1][c], uv: UGrid[r + 1][c] }
                    ]
                });
                if (group) this.importanceByFace.set(Fi, group);
            }
        }
    }

    AddWheelCylinder({ cx, cy, cz, radius, width, segs = 14, partId = 'Wheel', materialName = 'Rubber', group = null })
    {
        // Tread band (open cylinder with 1 longitudinal seam) + 2 hubcap rings as separate UV islands
        const RingL = [], RingR = [];
        const UvL = [], UvR = [];
        for (let i = 0; i <= segs; i++)
        {
            const T = i / segs;
            const A = T * Math.PI * 2;
            const X = cx + Math.cos(A) * radius;
            const Y = cy + Math.sin(A) * radius;
            if (i < segs)
            {
                RingL.push(this.AddVertex(X, Y, cz - width * 0.5));
                RingR.push(this.AddVertex(X, Y, cz + width * 0.5));
            }
            UvL.push(this.AddUV(T * radius * 2.2, 0));
            UvR.push(this.AddUV(T * radius * 2.2, width * 1.1));
        }

        for (let i = 0; i < segs; i++)
        {
            const Next = (i + 1) % segs;
            const Fi = this.faces.length;
            this.faces.push({
                id: Fi, name: partId, partId, materialName,
                corners: [
                    { v: RingL[i], uv: UvL[i] },
                    { v: RingL[Next], uv: UvL[i + 1] },
                    { v: RingR[Next], uv: UvR[i + 1] },
                    { v: RingR[i], uv: UvR[i] }
                ]
            });
            if (group) this.importanceByFace.set(Fi, group);
        }
        this.seams.add(EdgeKey(RingL[0], RingR[0]));

        // Outer disc rim cap
        const CenterOuter = this.AddVertex(cx, cy, cz + (cz >= 0 ? width * 0.52 : -width * 0.52));
        const CapUVs = Array.from({ length: segs }, (_, i) =>
        {
            const Sign = cz >= 0 ? 1 : -1;
            const A = Sign * (i / segs) * Math.PI * 2;
            return this.AddUV(0.2 + Math.cos(A) * radius * 0.65, 0.2 + Math.sin(A) * radius * 0.65);
        });
        const CenterUV = this.AddUV(0.2, 0.2);
        const OuterRing = cz >= 0 ? RingR : RingL;
        for (let i = 0; i < segs; i++)
        {
            const Next = (i + 1) % segs;
            const Fi = this.faces.length;
            const Corners = cz >= 0
                ? [{ v: CenterOuter, uv: CenterUV }, { v: OuterRing[i], uv: CapUVs[i] }, { v: OuterRing[Next], uv: CapUVs[Next] }]
                : [{ v: CenterOuter, uv: CenterUV }, { v: OuterRing[Next], uv: CapUVs[Next] }, { v: OuterRing[i], uv: CapUVs[i] }];
            this.faces.push({ id: Fi, name: `${partId} Rim`, partId: `${partId} Rim`, materialName: 'Alloy', corners: Corners });
            if (group) this.importanceByFace.set(Fi, group);
        }
    }

    BuildSurface()
    {
        const Surf = new TopologySurface({
            name: this.name,
            positions: this.positions,
            faces: this.faces,
            uvCoords: this.uvCoords,
            seams: [...this.seams]
        });
        Surf.importanceByFace = new Map(this.importanceByFace);
        Surf.PruneUnusedUVs();
        return Surf;
    }
}

export function CreateAeroCoupeAsset()
{
    const B = new SurfaceAssembler('Aero Coupe GT');

    // --- 5 HERO ISLANDS (Target 70%) ---
    // 1. Sculpted Hood
    B.AddCurvedPanel({
        rows: 4, cols: 5, partId: 'Hood Hero', materialName: 'BodyPaint', group: 'hero',
        uScale: 0.42, vScale: 0.48,
        pointFn: (u, v) => [0.85 + v * 1.15, 0.72 - v * 0.22 - Math.pow(Math.abs(u - 0.5) * 2, 2) * 0.08, (u - 0.5) * 1.48]
    });
    // 2. Roof Panel
    B.AddCurvedPanel({
        rows: 3, cols: 4, partId: 'Roof Shell', materialName: 'BodyPaint', group: 'hero',
        uScale: 0.38, vScale: 0.42,
        pointFn: (u, v) => [-0.75 + v * 1.05, 1.19 - Math.pow(Math.abs(u - 0.5) * 2, 2) * 0.06, (u - 0.5) * 1.18]
    });
    // 3. Left Body Flank & Door
    B.AddCurvedPanel({
        rows: 3, cols: 6, partId: 'Left Flank & Door', materialName: 'BodyPaint', group: 'hero',
        uScale: 0.62, vScale: 0.26,
        pointFn: (u, v) => [-1.75 + u * 3.45, 0.34 + v * 0.48, 0.80 - Math.pow(v, 1.4) * 0.11]
    });
    // 4. Right Body Flank & Door
    B.AddCurvedPanel({
        rows: 3, cols: 6, partId: 'Right Flank & Door', materialName: 'BodyPaint', group: 'hero',
        uScale: 0.62, vScale: 0.26,
        pointFn: (u, v) => [-1.75 + (1 - u) * 3.45, 0.34 + v * 0.48, -0.80 + Math.pow(v, 1.4) * 0.11]
    });
    // 5. Rear Decklid & Wing Foil
    B.AddCurvedPanel({
        rows: 3, cols: 5, partId: 'Rear Decklid', materialName: 'BodyPaint', group: 'hero',
        uScale: 0.44, vScale: 0.30,
        pointFn: (u, v) => [-1.88 + v * 0.92, 0.78 + v * 0.14 - Math.pow(Math.abs(u - 0.5) * 2, 2) * 0.05, (u - 0.5) * 1.46]
    });

    // --- 10 SECONDARY / TRIM ISLANDS (Target 10%) ---
    B.AddCurvedPanel({
        rows: 2, cols: 4, partId: 'Windshield', materialName: 'Glass', group: 'secondary',
        uScale: 0.22, vScale: 0.14,
        pointFn: (u, v) => [0.28 + (1 - v) * 0.62, 0.73 + v * 0.45, (u - 0.5) * (1.36 - v * 0.18)]
    });
    B.AddCurvedPanel({
        rows: 2, cols: 4, partId: 'Rear Glass', materialName: 'Glass', group: 'secondary',
        uScale: 0.22, vScale: 0.13,
        pointFn: (u, v) => [-1.26 + v * 0.52, 0.88 + v * 0.30, (u - 0.5) * (1.32 - v * 0.14)]
    });
    B.AddBoxPatch({ cx: 2.02, cy: 0.38, cz: 0, sx: 0.26, sy: 0.28, sz: 1.48, partId: 'Front Splitter', materialName: 'Carbon', group: 'secondary' });
    B.AddBoxPatch({ cx: -1.96, cy: 0.40, cz: 0, sx: 0.24, sy: 0.32, sz: 1.46, partId: 'Rear Diffuser', materialName: 'Carbon', group: 'secondary' });
    B.AddBoxPatch({ cx: -1.86, cy: 1.06, cz: 0, sx: 0.28, sy: 0.05, sz: 1.44, partId: 'GT Rear Wing', materialName: 'Carbon', group: 'secondary' });
    B.AddBoxPatch({ cx: -1.86, cy: 0.96, cz: 0.72, sx: 0.32, sy: 0.22, sz: 0.03, partId: 'Wing Endplate L', materialName: 'Carbon', group: 'secondary' });
    B.AddBoxPatch({ cx: -1.86, cy: 0.96, cz: -0.72, sx: 0.32, sy: 0.22, sz: 0.03, partId: 'Wing Endplate R', materialName: 'Carbon', group: 'secondary' });
    B.AddBoxPatch({ cx: 0.45, cy: 0.84, cz: 0.86, sx: 0.14, sy: 0.09, sz: 0.18, partId: 'Mirror L', materialName: 'Trim', group: 'secondary' });
    B.AddBoxPatch({ cx: 0.45, cy: 0.84, cz: -0.86, sx: 0.14, sy: 0.09, sz: 0.18, partId: 'Mirror R', materialName: 'Trim', group: 'secondary' });
    B.AddBoxPatch({ cx: 1.98, cy: 0.52, cz: 0, sx: 0.06, sy: 0.14, sz: 0.52, partId: 'Front Grille', materialName: 'Trim', group: 'secondary' });

    // --- REMAINING MECHANICAL / WHEEL ISLANDS (Auto-fill Remaining 20%) ---
    const WheelCoords = [
        [1.24, 0.33, 0.78, 'Front Left'],
        [1.24, 0.33, -0.78, 'Front Right'],
        [-1.22, 0.34, 0.80, 'Rear Left'],
        [-1.22, 0.34, -0.80, 'Rear Right']
    ];
    for (const [Wx, Wy, Wz, Label] of WheelCoords)
    {
        B.AddWheelCylinder({ cx: Wx, cy: Wy, cz: Wz, radius: 0.33, width: 0.26, segs: 12, partId: `${Label} Tyre`, materialName: 'Rubber' });
    }
    // Underbody tray, calipers, exhaust tips, suspension arms (~7 additional islands)
    B.AddBoxPatch({ cx: 0, cy: 0.18, cz: 0, sx: 3.1, sy: 0.06, sz: 1.24, partId: 'Undertray', materialName: 'Chassis' });
    B.AddBoxPatch({ cx: 1.92, cy: 0.54, cz: 0.56, sx: 0.14, sy: 0.11, sz: 0.24, partId: 'Headlight L', materialName: 'Optics' });
    B.AddBoxPatch({ cx: 1.92, cy: 0.54, cz: -0.56, sx: 0.14, sy: 0.11, sz: 0.24, partId: 'Headlight R', materialName: 'Optics' });
    B.AddBoxPatch({ cx: -1.94, cy: 0.62, cz: 0.54, sx: 0.08, sy: 0.10, sz: 0.28, partId: 'Taillight L', materialName: 'Optics' });
    B.AddBoxPatch({ cx: -1.94, cy: 0.62, cz: -0.54, sx: 0.08, sy: 0.10, sz: 0.28, partId: 'Taillight R', materialName: 'Optics' });
    B.AddBoxPatch({ cx: -2.02, cy: 0.26, cz: 0.34, sx: 0.22, sy: 0.09, sz: 0.09, partId: 'Exhaust L', materialName: 'Chrome' });
    B.AddBoxPatch({ cx: -2.02, cy: 0.26, cz: -0.34, sx: 0.22, sy: 0.09, sz: 0.09, partId: 'Exhaust R', materialName: 'Chrome' });

    const Surface = B.BuildSurface();
    const ImportanceGroups = [
        { id: 'hero', name: 'Hero Body Panels', targetPercent: 70, color: '#eff4cf', udimTarget: '1001' },
        { id: 'secondary', name: 'Trim & Glass', targetPercent: 10, color: '#9ecfff', udimTarget: '1001' }
    ];
    const UdimTiles = [
        { id: 1001, label: 'Main Chassis Atlas', resolution: 4096, checkerMode: 'color' },
        { id: 1002, label: 'Aux Wheels & Decals', resolution: 2048, checkerMode: 'uv' }
    ];
    SolveImportancePack(Surface, {
        importanceGroups: ImportanceGroups,
        udimTiles: UdimTiles,
        activeUdim: 1001,
        options: { scope: 'active-udim', marginPx: 14, rotations: '90', budgetMode: 'strict-ratio' }
    });
    return { surface: Surface, importanceGroups: ImportanceGroups, udimTiles: UdimTiles };
}

export function CreateTacticalCrateAsset()
{
    const B = new SurfaceAssembler('Tactical Prop Crate');

    // Main crate body faces as crisp separate or unfolded panels
    B.AddBoxPatch({ cx: 0, cy: 0.62, cz: 0, sx: 1.48, sy: 1.05, sz: 1.08, partId: 'Crate Hull', materialName: 'Polymer', group: 'hero', separateFaces: true });
    B.AddBoxPatch({ cx: 0, cy: 1.22, cz: 0, sx: 1.56, sy: 0.18, sz: 1.14, partId: 'Reinforced Lid', materialName: 'Polymer', group: 'hero' });
    B.AddBoxPatch({ cx: 0, cy: 0.72, cz: 0.56, sx: 0.58, sy: 0.36, sz: 0.06, partId: 'Data Nameplate', materialName: 'Decal', group: 'hero' });

    // Corner armor bumpers (8) + latches (2) + side handles (2) -> 12 secondary islands
    const Signs = [[-1, -1], [1, -1], [-1, 1], [1, 1]];
    for (let i = 0; i < Signs.length; i++)
    {
        const [Sx, Sz] = Signs[i];
        B.AddBoxPatch({ cx: Sx * 0.72, cy: 0.14, cz: Sz * 0.52, sx: 0.22, sy: 0.22, sz: 0.22, partId: `Foot Guard ${i + 1}`, materialName: 'Rubber', group: 'secondary' });
        B.AddBoxPatch({ cx: Sx * 0.74, cy: 1.24, cz: Sz * 0.54, sx: 0.18, sy: 0.16, sz: 0.18, partId: `Lid Corner ${i + 1}`, materialName: 'Steel', group: 'secondary' });
    }
    B.AddBoxPatch({ cx: -0.34, cy: 1.08, cz: 0.58, sx: 0.14, sy: 0.22, sz: 0.08, partId: 'Latch L', materialName: 'Steel' });
    B.AddBoxPatch({ cx: 0.34, cy: 1.08, cz: 0.58, sx: 0.14, sy: 0.22, sz: 0.08, partId: 'Latch R', materialName: 'Steel' });
    B.AddBoxPatch({ cx: -0.78, cy: 0.68, cz: 0, sx: 0.10, sy: 0.18, sz: 0.44, partId: 'Grip Handle L', materialName: 'Steel' });
    B.AddBoxPatch({ cx: 0.78, cy: 0.68, cz: 0, sx: 0.10, sy: 0.18, sz: 0.44, partId: 'Grip Handle R', materialName: 'Steel' });

    const Surface = B.BuildSurface();
    const ImportanceGroups = [
        { id: 'hero', name: 'Hull & Lid Panels', targetPercent: 65, color: '#eff4cf', udimTarget: '1001' },
        { id: 'secondary', name: 'Corner Bumpers', targetPercent: 18, color: '#9ecfff', udimTarget: '1001' }
    ];
    const UdimTiles = [{ id: 1001, label: 'Crate Atlas', resolution: 2048, checkerMode: 'color' }];
    SolveImportancePack(Surface, {
        importanceGroups: ImportanceGroups,
        udimTiles: UdimTiles,
        activeUdim: 1001,
        options: { scope: 'active-udim', marginPx: 10, rotations: '90' }
    });
    return { surface: Surface, importanceGroups: ImportanceGroups, udimTiles: UdimTiles };
}

export function CreateCharacterBustAsset()
{
    const B = new SurfaceAssembler('Armored Bust & Straps');

    // Hero Organic Face / Head dome with back-of-head vertical seam so LSCM unwraps cleanly!
    const Rows = 10, Cols = 14;
    const GridV = [];
    const GridUV = [];
    for (let r = 0; r <= Rows; r++)
    {
        const Row = [];
        const URow = [];
        const Phi = (r / Rows) * Math.PI;
        const Y = 1.48 - Math.cos(Phi) * 0.42;
        const Rad = 0.04 + Math.sin(Phi) * 0.32;
        for (let c = 0; c <= Cols; c++)
        {
            const Theta = (c / Cols) * Math.PI * 2 - Math.PI; // seam at back (-Z -> Theta = +/-PI)
            const BrowBump = Math.exp(-Math.pow((Phi - 1.4) * 3, 2)) * Math.max(0, Math.cos(Theta)) * 0.04;
            const Nose = Math.exp(-Math.pow((Phi - 1.65) * 4.5, 2) - Math.pow(Theta * 4.5, 2)) * 0.08;
            const X = Math.sin(Theta) * (Rad + BrowBump);
            const Z = Math.cos(Theta) * (Rad + BrowBump + Nose);
            if (c < Cols) Row.push(B.AddVertex(X, Y, Z));
            else Row.push(Row[0]);
            URow.push(B.AddUV((c / Cols) * 0.7, (r / Rows) * 0.7));
        }
        GridV.push(Row);
        GridUV.push(URow);
    }
    for (let r = 0; r < Rows; r++)
    {
        for (let c = 0; c < Cols; c++)
        {
            const Fi = B.faces.length;
            B.faces.push({
                id: Fi, name: 'Head & Face Hero', partId: 'Head & Face Hero', materialName: 'Skin',
                corners: [
                    { v: GridV[r][c], uv: GridUV[r][c] },
                    { v: GridV[r][c + 1], uv: GridUV[r][c + 1] },
                    { v: GridV[r + 1][c + 1], uv: GridUV[r + 1][c + 1] },
                    { v: GridV[r + 1][c], uv: GridUV[r + 1][c] }
                ]
            });
            B.importanceByFace.set(Fi, 'hero');
        }
        B.seams.add(EdgeKey(GridV[r][0], GridV[r + 1][0]));
    }

    // Neck & Chestplate Hero Panels
    B.AddCurvedPanel({
        rows: 3, cols: 5, partId: 'Chestplate Front', materialName: 'Armor', group: 'hero',
        uScale: 0.44, vScale: 0.34,
        pointFn: (u, v) => [(u - 0.5) * 0.92, 0.35 + v * 0.56, 0.24 + Math.sin(u * Math.PI) * 0.14]
    });
    B.AddCurvedPanel({
        rows: 3, cols: 5, partId: 'Backplate', materialName: 'Armor', group: 'hero',
        uScale: 0.42, vScale: 0.32,
        pointFn: (u, v) => [(0.5 - u) * 0.88, 0.35 + v * 0.56, -0.22 - Math.sin(u * Math.PI) * 0.12]
    });
    B.AddCurvedPanel({
        rows: 3, cols: 4, partId: 'Pauldron L', materialName: 'Armor', group: 'hero',
        uScale: 0.32, vScale: 0.26,
        pointFn: (u, v) => [0.48 + v * 0.32, 0.92 - Math.pow(v, 1.5) * 0.24, (u - 0.5) * 0.48]
    });
    B.AddCurvedPanel({
        rows: 3, cols: 4, partId: 'Pauldron R', materialName: 'Armor', group: 'hero',
        uScale: 0.32, vScale: 0.26,
        pointFn: (u, v) => [-0.48 - v * 0.32, 0.92 - Math.pow(v, 1.5) * 0.24, (0.5 - u) * 0.48]
    });

    // Secondary Straps & Buckles (8 small islands)
    for (let s = 0; s < 4; s++)
    {
        const Side = s % 2 === 0 ? 1 : -1;
        const ZOff = s < 2 ? 0.28 : -0.26;
        B.AddBoxPatch({ cx: Side * 0.28, cy: 0.86, cz: ZOff, sx: 0.12, sy: 0.26, sz: 0.05, partId: `Strap ${s + 1}`, materialName: 'Leather', group: 'secondary' });
        B.AddBoxPatch({ cx: Side * 0.28, cy: 0.68, cz: ZOff + (ZOff > 0 ? 0.03 : -0.03), sx: 0.14, sy: 0.09, sz: 0.04, partId: `Buckle ${s + 1}`, materialName: 'Brass', group: 'secondary' });
    }
    B.AddBoxPatch({ cx: 0, cy: 0.14, cz: 0, sx: 0.72, sy: 0.24, sz: 0.56, partId: 'Plinth Pedestal', materialName: 'Slate' });
    B.AddBoxPatch({ cx: 0, cy: 1.02, cz: 0.02, sx: 0.32, sy: 0.16, sz: 0.32, partId: 'Neck Collar', materialName: 'Leather' });

    const Surface = B.BuildSurface();
    const ImportanceGroups = [
        { id: 'hero', name: 'Head & Armor Hero', targetPercent: 72, color: '#eff4cf', udimTarget: '1001' },
        { id: 'secondary', name: 'Straps & Buckles', targetPercent: 12, color: '#9ecfff', udimTarget: '1001' }
    ];
    const UdimTiles = [{ id: 1001, label: 'Character Head & Torso', resolution: 4096, checkerMode: 'color' }];
    SolveImportancePack(Surface, {
        importanceGroups: ImportanceGroups,
        udimTiles: UdimTiles,
        activeUdim: 1001,
        options: { scope: 'active-udim', marginPx: 12, rotations: '90' }
    });
    return { surface: Surface, importanceGroups: ImportanceGroups, udimTiles: UdimTiles };
}

export function CreateMultiUdimArchAsset()
{
    const B = new SurfaceAssembler('Multi-UDIM Architecture Kit');

    // Tile 1001: Stone Archway & Keystone
    B.AddCurvedPanel({
        rows: 4, cols: 8, partId: 'Arch Voussoirs', materialName: 'Stone', group: 'arch',
        uScale: 0.80, vScale: 0.34,
        pointFn: (u, v) =>
        {
            const Ang = u * Math.PI;
            const R = 1.05 + v * 0.36;
            return [Math.cos(Ang) * R, 1.45 + Math.sin(Ang) * R, 0.22];
        }
    });
    B.AddBoxPatch({ cx: 0, cy: 2.66, cz: 0.24, sx: 0.34, sy: 0.44, sz: 0.52, partId: 'Carved Keystone', materialName: 'Stone', group: 'arch' });
    B.AddBoxPatch({ cx: -1.24, cy: 0.14, cz: 0, sx: 0.62, sy: 0.28, sz: 0.62, partId: 'Plinth Left', materialName: 'Stone', group: 'arch' });
    B.AddBoxPatch({ cx: 1.24, cy: 0.14, cz: 0, sx: 0.62, sy: 0.28, sz: 0.62, partId: 'Plinth Right', materialName: 'Stone', group: 'arch' });

    // Tile 1002: Fluted Columns & Capitals
    B.AddWheelCylinder({ cx: -1.24, cy: 0.86, cz: 0, radius: 0.22, width: 1.15, segs: 12, partId: 'Column Left', materialName: 'Marble', group: 'columns' });
    B.AddWheelCylinder({ cx: 1.24, cy: 0.86, cz: 0, radius: 0.22, width: 1.15, segs: 12, partId: 'Column Right', materialName: 'Marble', group: 'columns' });
    B.AddBoxPatch({ cx: -1.24, cy: 1.50, cz: 0, sx: 0.54, sy: 0.20, sz: 0.54, partId: 'Capital Left', materialName: 'Marble', group: 'columns' });
    B.AddBoxPatch({ cx: 1.24, cy: 1.50, cz: 0, sx: 0.54, sy: 0.20, sz: 0.54, partId: 'Capital Right', materialName: 'Marble', group: 'columns' });

    // Tile 1003: Heavy Timber Door Planks & Crossbeams
    for (let p = 0; p < 4; p++)
    {
        const X = -0.66 + p * 0.44;
        B.AddBoxPatch({ cx: X, cy: 0.92, cz: 0, sx: 0.41, sy: 1.72, sz: 0.14, partId: `Door Plank ${p + 1}`, materialName: 'Oak', group: 'timber' });
    }
    B.AddBoxPatch({ cx: 0, cy: 0.46, cz: 0.10, sx: 1.66, sy: 0.18, sz: 0.08, partId: 'Crossbeam Low', materialName: 'Oak', group: 'timber' });
    B.AddBoxPatch({ cx: 0, cy: 1.38, cz: 0.10, sx: 1.66, sy: 0.18, sz: 0.08, partId: 'Crossbeam High', materialName: 'Oak', group: 'timber' });

    // Tile 1004: Forged Iron Hardware
    B.AddBoxPatch({ cx: -0.62, cy: 0.46, cz: 0.15, sx: 0.54, sy: 0.11, sz: 0.05, partId: 'Iron Strap Low', materialName: 'Iron', group: 'hardware' });
    B.AddBoxPatch({ cx: -0.62, cy: 1.38, cz: 0.15, sx: 0.54, sy: 0.11, sz: 0.05, partId: 'Iron Strap High', materialName: 'Iron', group: 'hardware' });
    B.AddBoxPatch({ cx: 0.58, cy: 0.92, cz: 0.16, sx: 0.14, sy: 0.22, sz: 0.08, partId: 'Lock Escutcheon', materialName: 'Iron', group: 'hardware' });
    B.AddBoxPatch({ cx: 0.58, cy: 0.84, cz: 0.22, sx: 0.12, sy: 0.14, sz: 0.05, partId: 'Ring Pull', materialName: 'Iron', group: 'hardware' });

    const Surface = B.BuildSurface();
    const ImportanceGroups = [
        { id: 'arch', name: '1001 · Stone Arch', targetPercent: 100, color: '#eff4cf', udimTarget: '1001' },
        { id: 'columns', name: '1002 · Marble Columns', targetPercent: 100, color: '#9ecfff', udimTarget: '1002' },
        { id: 'timber', name: '1003 · Timber Door', targetPercent: 100, color: '#e8c27a', udimTarget: '1003' },
        { id: 'hardware', name: '1004 · Iron Hardware', targetPercent: 100, color: '#9bdac5', udimTarget: '1004' }
    ];
    const UdimTiles = [
        { id: 1001, label: 'Stone Archway', resolution: 4096, checkerMode: 'color' },
        { id: 1002, label: 'Marble Columns', resolution: 2048, checkerMode: 'uv' },
        { id: 1003, label: 'Timber Door', resolution: 2048, checkerMode: 'checker' },
        { id: 1004, label: 'Iron Hardware', resolution: 1024, checkerMode: 'color' }
    ];
    SolveImportancePack(Surface, {
        importanceGroups: ImportanceGroups,
        udimTiles: UdimTiles,
        activeUdim: 1001,
        options: { scope: 'per-udim', marginPx: 10, rotations: '90' }
    });
    return { surface: Surface, importanceGroups: ImportanceGroups, udimTiles: UdimTiles };
}

export const SampleAssetCatalog = [
    { id: 'controlled_integrator', label: 'Controlled Integrator', subtitle: '25,068 tris · 152 islands · 55/15/30', create: CreateControlledIntegratorAsset },
    { id: 'coupe', label: 'Aero Coupe GT', subtitle: '30 islands · 70/10/Rest demo', create: CreateAeroCoupeAsset },
    { id: 'crate', label: 'Tactical Crate', subtitle: '22 islands · Hard-surface quads', create: CreateTacticalCrateAsset },
    { id: 'bust', label: 'Armored Bust', subtitle: '16 islands · Organic LSCM + trim', create: CreateCharacterBustAsset },
    { id: 'udim_kit', label: 'UDIM Arch Kit', subtitle: '4 UDIMs (1001–1004) · Multi-res', create: CreateMultiUdimArchAsset }
];
