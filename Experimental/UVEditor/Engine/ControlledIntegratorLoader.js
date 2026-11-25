/*==========================================================================================================================================
                                                     CONTROLLEDINTEGRATORLOADER.JS
==========================================================================================================================================*/
// Assembles the real VehicleHMI "Controlled Integrator" 3D vehicle (ControlVehicleBody + 4x ControlVehicleRim + 4x ControlVehicleTyre,
// 25,068 triangles) from FCT1 binary facet archives into a clean, zero-overlap, zero-flip, multi-part TopologySurface ready for
// Frontier UV Studio and cross-studio transfer to Texture Studio.

import { TopologySurface, EdgeKey } from './TopologySurface.js';
import { UnwrapLscm } from './UnwrapSolver.js';
import { SolveImportancePack } from './ImportancePackingSolver.js';

const FACET_NAMES = ['Body', 'Rim', 'Tyre'];

async function LoadFacetBinaryBuffers()
{
    const SampleUrl = new URL('../Assets/ControlVehicleBody.facet', import.meta.url);
    const UseNodeFs = SampleUrl.protocol === 'file:' && typeof process !== 'undefined' && !!process.versions?.node;
    if (!UseNodeFs)
    {
        const Buffers = await Promise.all(
            FACET_NAMES.map(async Name =>
            {
                const Url = new URL(`../Assets/ControlVehicle${Name}.facet`, import.meta.url);
                const Resp = await fetch(Url);
                if (!Resp.ok) throw new Error(`Failed to fetch ${Url}: HTTP ${Resp.status}`);
                return Resp.arrayBuffer();
            })
        );
        return { body: Buffers[0], rim: Buffers[1], tyre: Buffers[2] };
    }

    const Fs = await import('node:fs/promises');
    const { fileURLToPath } = await import('node:url');
    const Buffers = await Promise.all(
        FACET_NAMES.map(async Name =>
        {
            const FilePath = fileURLToPath(new URL(`../Assets/ControlVehicle${Name}.facet`, import.meta.url));
            const Buf = await Fs.readFile(FilePath);
            return Buf.buffer.slice(Buf.byteOffset, Buf.byteOffset + Buf.byteLength);
        })
    );
    return { body: Buffers[0], rim: Buffers[1], tyre: Buffers[2] };
}

function ParseFacetBinary(ArrayBuf)
{
    const Dv = new DataView(ArrayBuf);
    const Magic = String.fromCharCode(
        Dv.getUint8(0), Dv.getUint8(1), Dv.getUint8(2), Dv.getUint8(3)
    );
    if (Magic !== 'FCT1') throw new Error(`Invalid facet magic "${Magic}"`);

    const VertCount = Dv.getUint32(4, true);
    const FaceCount = Dv.getUint32(8, true);
    let Offset = 48;

    const Pos = new Array(VertCount);
    for (let i = 0; i < VertCount; i++, Offset += 12)
    {
        Pos[i] = [
            Dv.getFloat32(Offset, true),
            Dv.getFloat32(Offset + 4, true),
            Dv.getFloat32(Offset + 8, true)
        ];
    }

    const Idx = new Array(FaceCount);
    for (let i = 0; i < FaceCount; i++, Offset += 12)
    {
        Idx[i] = [
            Dv.getUint32(Offset, true),
            Dv.getUint32(Offset + 4, true),
            Dv.getUint32(Offset + 8, true)
        ];
    }

    const Fam = new Uint32Array(FaceCount);
    for (let i = 0; i < FaceCount; i++, Offset += 4)
    {
        Fam[i] = Dv.getUint32(Offset, true);
    }

    return { pos: Pos, idx: Idx, fam: Fam };
}

const RawBuffers = await LoadFacetBinaryBuffers();
const ParsedBody = ParseFacetBinary(RawBuffers.body);
const ParsedRim = ParseFacetBinary(RawBuffers.rim);
const ParsedTyre = ParseFacetBinary(RawBuffers.tyre);

// VehicleHMI coordinate mapping: (x_fwd, y_left, z_up) -> Three.js / UV Studio (x_fwd, z_up_rebased, -y_left) [det = +1]
const VEHICLE_Z_REBASE = 0.11285;
const VEHICLE_GROUND_LIFT = 0.53; // Puts tyre contact radius (~0.51m) resting flush on UVViewport3D ground plane y=0
const RIM_TO_HUB_SCALE = 0.34460 / 0.344818;

const BODY_FAMILY_PARTS = {
    6:  'Body Coat',
    7:  'Vehicle Glass',
    8:  'Chassis & Trim',
    9:  'Tyre Rubber',
    10: 'Rim Alloy',
    11: 'Chassis & Trim',
    12: 'Chassis & Trim',
    13: 'Optics & Lights',
    14: 'Optics & Lights',
    15: 'Chassis & Trim'
};

const WHEEL_MOUNTS = [
    { name: 'FL', mount: [ 1.7274,  1.0475, 0.0914], mirrorY: false },
    { name: 'FR', mount: [ 1.7274, -1.0475, 0.0914], mirrorY: true  },
    { name: 'RL', mount: [-1.6686,  1.0475, 0.0914], mirrorY: false },
    { name: 'RR', mount: [-1.6686, -1.0475, 0.0914], mirrorY: true  }
];

const ORTHO_AXES = [
    { n: [ 1,  0,  0], t: [ 0, 0, -1], b: [0, 1,  0] },
    { n: [-1,  0,  0], t: [ 0, 0,  1], b: [0, 1,  0] },
    { n: [ 0,  1,  0], t: [ 1, 0,  0], b: [0, 0, -1] },
    { n: [ 0, -1,  0], t: [ 1, 0,  0], b: [0, 0,  1] },
    { n: [ 0,  0,  1], t: [ 1, 0,  0], b: [0, 1,  0] },
    { n: [ 0,  0, -1], t: [-1, 0,  0], b: [0, 1,  0] }
];

function ToStudioCoords(X, YLeft, ZUp)
{
    return [X, ZUp - VEHICLE_Z_REBASE + VEHICLE_GROUND_LIFT, -YLeft];
}

// Deterministic packed template UV coordinates computed once per module session so CreateControlledIntegratorAsset()
// stays instantaneous (< 40ms) on repeated document creation/reset while yielding 0 overlaps, 0 flips, and exact 55/15/30 budgets.
let CachedVehicleTemplate = null;

function BuildVehicleTemplate()
{
    const Positions = [];
    const Faces = [];
    const WheelMeta = [];

    // 1. Vehicle Body shell & exterior sub-parts
    const BodyBase = Positions.length;
    for (const P of ParsedBody.pos)
    {
        Positions.push(ToStudioCoords(P[0], P[1], P[2]));
    }
    const BodyFaceCount = ParsedBody.idx.length;
    for (let Fi = 0; Fi < BodyFaceCount; Fi++)
    {
        const [A, B, C] = ParsedBody.idx[Fi];
        const PartName = BODY_FAMILY_PARTS[ParsedBody.fam[Fi]] || 'Chassis & Trim';
        Faces.push({
            id: Faces.length,
            name: PartName,
            partId: PartName,
            materialName: PartName,
            corners: [
                { v: BodyBase + A, uv: 0 },
                { v: BodyBase + B, uv: 0 },
                { v: BodyBase + C, uv: 0 }
            ]
        });
        WheelMeta.push(null);
    }

    // 2. Four Wheel Assemblies (Tyre + Rim) mounted at ControlVehicleSockets.AxleMount
    const AppendWheelPart = (Parsed, Spec, Kind, Scale) =>
    {
        const [Mx, My, Mz] = Spec.mount;
        const Base = Positions.length;
        for (const P of Parsed.pos)
        {
            const Ly = Spec.mirrorY ? -P[1] : P[1];
            Positions.push(ToStudioCoords(P[0] * Scale + Mx, Ly * Scale + My, P[2] * Scale + Mz));
        }
        const PartName = `${Kind} ${Spec.name}`;
        const MatName = Kind === 'Tyre' ? 'Tyre Rubber' : 'Rim Alloy';
        const HubY = Mz - VEHICLE_Z_REBASE + VEHICLE_GROUND_LIFT;
        const AxleZ = -My;

        for (let Fi = 0; Fi < Parsed.idx.length; Fi++)
        {
            let [A, B, C] = Parsed.idx[Fi];
            if (Spec.mirrorY) { const Tmp = B; B = C; C = Tmp; }

            const Pa = Positions[Base + A];
            const Pb = Positions[Base + B];
            const Pc = Positions[Base + C];
            const E1x = Pb[0] - Pa[0], E1y = Pb[1] - Pa[1], E1z = Pb[2] - Pa[2];
            const E2x = Pc[0] - Pa[0], E2y = Pc[1] - Pa[1], E2z = Pc[2] - Pa[2];
            const Nx = E1y * E2z - E1z * E2y;
            const Ny = E1z * E2x - E1x * E2z;
            const Nz = E1x * E2y - E1y * E2x;
            const Nl = Math.hypot(Nx, Ny, Nz) || 1;
            const Fnx = Nx / Nl, Fny = Ny / Nl, Fnz = Nz / Nl;

            const Cx = (Pa[0] + Pb[0] + Pc[0]) / 3 - Mx;
            const Cy = (Pa[1] + Pb[1] + Pc[1]) / 3 - HubY;
            const Cz = (Pa[2] + Pb[2] + Pc[2]) / 3 - AxleZ;
            const R = Math.hypot(Cx, Cy) || 1;
            const RadDot = (Fnx * Cx + Fny * Cy) / R;

            let Zone;
            if (Math.abs(Fnz) <= 0.55) Zone = RadDot >= 0 ? 'co' : 'ci';
            else if (Kind === 'Tyre') Zone = Fnz > 0 ? 'dp' : 'dn';
            else Zone = `${Fnz > 0 ? 'dp' : 'dn'}_${Cz > 0 ? 1 : 0}`;

            Faces.push({
                id: Faces.length,
                name: PartName,
                partId: `${PartName}#${Zone}`,
                materialName: MatName,
                corners: [
                    { v: Base + A, uv: 0 },
                    { v: Base + B, uv: 0 },
                    { v: Base + C, uv: 0 }
                ]
            });
            WheelMeta.push({ cx: Mx, cy: HubY, cz: AxleZ, zone: Zone, fnZ: Fnz });
        }
    };

    for (const Spec of WHEEL_MOUNTS)
    {
        AppendWheelPart(ParsedTyre, Spec, 'Tyre', 1.0);
        AppendWheelPart(ParsedRim, Spec, 'Rim', RIM_TO_HUB_SCALE);
    }

    const Surface = new TopologySurface({
        name: 'Controlled Integrator',
        positions: Positions,
        faces: Faces,
        uvCoords: [[0, 0]]
    });
    Surface.RecomputeNormals();

    // Build shared 3D vertex-edge adjacency
    const EdgeFaces = new Map();
    for (let Fi = 0; Fi < Surface.faces.length; Fi++)
    {
        const C = Surface.faces[Fi].corners;
        for (let k = 0; k < 3; k++)
        {
            const Key = EdgeKey(C[k].v, C[(k + 1) % 3].v);
            let List = EdgeFaces.get(Key);
            if (!List) { List = []; EdgeFaces.set(Key, List); }
            List.push(Fi);
        }
    }

    // Tag body faces with dominant 6-axis box projection direction
    const BodyAxis = new Uint8Array(BodyFaceCount);
    for (let Fi = 0; Fi < BodyFaceCount; Fi++)
    {
        const Fn = Surface.faceNormals[Fi];
        let Best = 0, BestDot = -Infinity;
        for (let Ai = 0; Ai < 6; Ai++)
        {
            const AxN = ORTHO_AXES[Ai].n;
            const Dot = Fn[0] * AxN[0] + Fn[1] * AxN[1] + Fn[2] * AxN[2];
            if (Dot > BestDot) { BestDot = Dot; Best = Ai; }
        }
        BodyAxis[Fi] = Best;
    }

    Surface.uvCoords = [];
    const Visited = new Uint8Array(Surface.faces.length);

    // Unwrap Body connected part-axis patches
    for (let Seed = 0; Seed < BodyFaceCount; Seed++)
    {
        if (Visited[Seed]) continue;
        Visited[Seed] = 1;
        const AxisIdx = BodyAxis[Seed];
        const PartId = Surface.faces[Seed].partId;
        const Queue = [Seed];
        const Group = [];
        while (Queue.length)
        {
            const Fi = Queue.pop();
            Group.push(Fi);
            const C = Surface.faces[Fi].corners;
            for (let k = 0; k < 3; k++)
            {
                const Key = EdgeKey(C[k].v, C[(k + 1) % 3].v);
                for (const Adj of EdgeFaces.get(Key))
                {
                    if (Adj < BodyFaceCount && !Visited[Adj] && BodyAxis[Adj] === AxisIdx && Surface.faces[Adj].partId === PartId)
                    {
                        Visited[Adj] = 1;
                        Queue.push(Adj);
                    }
                }
            }
        }
        const MapV = new Map();
        const Ax = ORTHO_AXES[AxisIdx];
        for (const Fi of Group)
        {
            for (const Corner of Surface.faces[Fi].corners)
            {
                let UvIdx = MapV.get(Corner.v);
                if (UvIdx === undefined)
                {
                    const P = Surface.positions[Corner.v];
                    UvIdx = Surface.uvCoords.push([
                        (P[0] * Ax.t[0] + P[1] * Ax.t[1] + P[2] * Ax.t[2]) * 0.22,
                        (P[0] * Ax.b[0] + P[1] * Ax.b[1] + P[2] * Ax.b[2]) * 0.22
                    ]) - 1;
                    MapV.set(Corner.v, UvIdx);
                }
                Corner.uv = UvIdx;
            }
        }
    }

    // Unwrap Wheel assemblies (cylindrical tread/bore + planar discs + LSCM on self-overlapping spoke pockets)
    const CosSharpRim = Math.cos((35 * Math.PI) / 180);
    for (let Seed = BodyFaceCount; Seed < Surface.faces.length; Seed++)
    {
        if (Visited[Seed]) continue;
        Visited[Seed] = 1;
        const Meta = WheelMeta[Seed];
        const PartId = Surface.faces[Seed].partId;
        const Queue = [Seed];
        const Group = [];
        while (Queue.length)
        {
            const Fi = Queue.pop();
            Group.push(Fi);
            const C = Surface.faces[Fi].corners;
            for (let k = 0; k < 3; k++)
            {
                const Key = EdgeKey(C[k].v, C[(k + 1) % 3].v);
                for (const Adj of EdgeFaces.get(Key))
                {
                    if (!Visited[Adj] && Surface.faces[Adj].partId === PartId)
                    {
                        Visited[Adj] = 1;
                        Queue.push(Adj);
                    }
                }
            }
        }

        if (Meta.zone.startsWith('c'))
        {
            const Sgn = Meta.zone === 'co' ? 1 : -1;
            const MapV = new Map();
            for (const Fi of Group)
            {
                const F = Surface.faces[Fi];
                const Thetas = F.corners.map(C => Math.atan2(Surface.positions[C.v][1] - Meta.cy, Surface.positions[C.v][0] - Meta.cx));
                const Wraps = (Math.max(...Thetas) - Math.min(...Thetas)) > Math.PI;
                for (let k = 0; k < 3; k++)
                {
                    const Corner = F.corners[k];
                    let Th = Thetas[k];
                    if (Wraps && Th < 0) Th += Math.PI * 2;
                    const Key = `${Corner.v}:${Wraps && Thetas[k] < 0 ? 1 : 0}`;
                    let UvIdx = MapV.get(Key);
                    if (UvIdx === undefined)
                    {
                        UvIdx = Surface.uvCoords.push([
                            (Th / (Math.PI * 2)) * 0.6,
                            (Surface.positions[Corner.v][2] - Meta.cz) * Sgn * 0.35
                        ]) - 1;
                        MapV.set(Key, UvIdx);
                    }
                    Corner.uv = UvIdx;
                }
            }
        }
        else
        {
            const Sgn = Meta.fnZ >= 0 ? 1 : -1;
            const MapV = new Map();
            for (const Fi of Group)
            {
                for (const Corner of Surface.faces[Fi].corners)
                {
                    let UvIdx = MapV.get(Corner.v);
                    if (UvIdx === undefined)
                    {
                        const P = Surface.positions[Corner.v];
                        UvIdx = Surface.uvCoords.push([
                            (P[0] - Meta.cx) * 0.45 * Sgn,
                            (P[1] - Meta.cy) * 0.45
                        ]) - 1;
                        MapV.set(Corner.v, UvIdx);
                    }
                    Corner.uv = UvIdx;
                }
            }

            // Detect concave spoke overhang on inner rim face and resolve via 35-deg LSCM sub-unwrap
            const Check = Surface.DetectOverlapsAndFlips([{ id: 0, hidden: false, udimTile: 1001, faceIndices: Group }]);
            if (Check.OverlappingFaces.size > 0 || Check.FlippedFaces.size > 0)
            {
                const SubV = new Map();
                const SubPos = [];
                const SubFaces = [];
                for (const Fi of Group)
                {
                    const NewCorners = [];
                    for (const Corner of Surface.faces[Fi].corners)
                    {
                        let Sv = SubV.get(Corner.v);
                        if (Sv === undefined)
                        {
                            Sv = SubPos.length;
                            SubPos.push([...Surface.positions[Corner.v]]);
                            SubV.set(Corner.v, Sv);
                        }
                        NewCorners.push({ v: Sv, uv: 0 });
                    }
                    SubFaces.push({ id: SubFaces.length, name: 'Rim', partId: 'Rim', materialName: 'Rim Alloy', corners: NewCorners });
                }

                const Sub = new TopologySurface({ name: 'Rim', positions: SubPos, faces: SubFaces, uvCoords: [[0, 0]] });
                Sub.RecomputeNormals();
                const SubEdges = new Map();
                for (let i = 0; i < Sub.faces.length; i++)
                {
                    const C = Sub.faces[i].corners;
                    for (let k = 0; k < 3; k++)
                    {
                        const Key = EdgeKey(C[k].v, C[(k + 1) % 3].v);
                        let L = SubEdges.get(Key);
                        if (!L) { L = []; SubEdges.set(Key, L); }
                        L.push(i);
                    }
                }
                for (const [Key, Fl] of SubEdges)
                {
                    if (Fl.length === 2)
                    {
                        const N0 = Sub.faceNormals[Fl[0]], N1 = Sub.faceNormals[Fl[1]];
                        if (N0[0] * N1[0] + N0[1] * N1[1] + N0[2] * N1[2] < CosSharpRim) Sub.seams.add(Key);
                    }
                }
                UnwrapLscm(Sub);
                const UvOffset = Surface.uvCoords.length;
                for (const Uv of Sub.uvCoords) Surface.uvCoords.push([Uv[0], Uv[1]]);
                for (let i = 0; i < Group.length; i++)
                {
                    const Dst = Surface.faces[Group[i]].corners;
                    const Src = Sub.faces[i].corners;
                    for (let k = 0; k < 3; k++) Dst[k].uv = UvOffset + Src[k].uv;
                }
            }
        }
    }

    // Restore clean human-readable partId names after zone-isolated unwrapping and mark UV seams
    for (let Fi = 0; Fi < Surface.faces.length; Fi++)
    {
        const F = Surface.faces[Fi];
        const HashIdx = F.partId.indexOf('#');
        if (HashIdx !== -1) F.partId = F.partId.slice(0, HashIdx);

        if (F.materialName === 'Body Coat') Surface.importanceByFace.set(Fi, 'hero');
        else if (F.materialName === 'Vehicle Glass' || F.materialName === 'Optics & Lights') Surface.importanceByFace.set(Fi, 'optics');
    }

    for (const [Key, Fl] of EdgeFaces)
    {
        if (Fl.length === 2)
        {
            const F0 = Surface.faces[Fl[0]], F1 = Surface.faces[Fl[1]];
            const [VaStr, VbStr] = Key.split(':');
            const Va = Number(VaStr), Vb = Number(VbStr);
            const C0A = F0.corners.find(C => C.v === Va)?.uv;
            const C1A = F1.corners.find(C => C.v === Va)?.uv;
            const C0B = F0.corners.find(C => C.v === Vb)?.uv;
            const C1B = F1.corners.find(C => C.v === Vb)?.uv;
            if (C0A !== C1A || C0B !== C1B) Surface.seams.add(Key);
        }
    }

    Surface.PruneUnusedUVs();

    const ImportanceGroups = [
        { id: 'hero',   name: 'Body Coat Hero', targetPercent: 55, color: '#eff4cf', udimTarget: '1001' },
        { id: 'optics', name: 'Optics & Glass', targetPercent: 15, color: '#9ecfff', udimTarget: '1001' }
    ];
    const UdimTiles = [{ id: 1001, label: 'Vehicle Atlas', resolution: 4096, checkerMode: 'color' }];

    SolveImportancePack(Surface, {
        importanceGroups: ImportanceGroups,
        udimTiles: UdimTiles,
        activeUdim: 1001,
        options: { scope: 'active-udim', marginPx: 8, rotations: '90', budgetMode: 'strict-ratio' }
    });

    CachedVehicleTemplate = {
        positions: Surface.positions,
        uvCoords: Surface.uvCoords,
        faces: Surface.faces,
        seams: Surface.seams,
        importanceByFace: Surface.importanceByFace,
        importanceGroups: ImportanceGroups,
        udimTiles: UdimTiles
    };
}

export function CreateControlledIntegratorAsset()
{
    if (!CachedVehicleTemplate) BuildVehicleTemplate();
    const T = CachedVehicleTemplate;
    const Surface = new TopologySurface({
        name: 'Controlled Integrator',
        positions: T.positions.map(P => [P[0], P[1], P[2]]),
        uvCoords: T.uvCoords.map(U => [U[0], U[1]]),
        faces: T.faces.map(F => ({
            id: F.id,
            name: F.name,
            partId: F.partId,
            materialName: F.materialName,
            corners: F.corners.map(C => ({ v: C.v, uv: C.uv }))
        })),
        seams: new Set(T.seams),
        importanceByFace: new Map(T.importanceByFace)
    });
    return {
        surface: Surface,
        importanceGroups: T.importanceGroups.map(G => ({ ...G })),
        udimTiles: T.udimTiles.map(U => ({ ...U }))
    };
}
