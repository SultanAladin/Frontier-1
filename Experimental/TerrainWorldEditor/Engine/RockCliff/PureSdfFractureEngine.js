/*==========================================================================================================================================
                                                   PURESDFFRACTUREENGINE.JS
==========================================================================================================================================*/
// 100% Pure-SDF Brittle Fracture, Kinematic Jigsaw Cleaving, Thermal Skin-Depth V-Tipped Cracking,
// Hierarchical 90-Degree T-Junction Crazing, & Concentric Onion-Skin Exfoliation Engine.
//
// Architectural Guarantee:
// - ZERO Mesh->SDF round-trips (`convertMeshToSdf` is NEVER used).
// - Every fractured block tracks its exact inverse rigid pullback `p0 = invR * p + invT` into the unbroken
//   parent rock massif, so opposing fracture walls have 1-to-1 interlocking jigsaw undulations, plumose
//   arrest ribs, and continuous sedimentary strata across fault heave and hinge tilt!
// - Evaluates identically on CPU (Sparse 4^3-Brick QEF Dual Contouring) and GPU (60 FPS WebGL2 Raymarcher).

import {
    DEG2RAD,
    clamp,
    mix,
    smoothstep,
    vdot,
    vcross,
    vnorm,
    rotateAroundAxis,
    buildAxisAngleMatrix3,
    mulMat3,
    transposeMat3,
    applyMat3,
    perturbNormal,
    buildStrikeDipFrame,
    smax,
    chamferMax,
    chamferSub,
    createRng,
    scalarField3D,
    fbm3D,
    ridged3D,
    evalPlanarCleavage3D,
    evalHierarchical90Crazing3D
} from './Core/SpatialNoise3D.js';
import { evaluateSparseSdf } from './Core/SparseSdfBrickGrid.js';
import {
    extractDualContourMesh,
    removeFloatingSpeckles,
    taubinSmoothCreasePreserving,
    buildGameReadyAttributes
} from './Core/QefDualContour.js';

const EPS = 1e-5;

/*==========================================================================================================================================
                        1. CONVEX POLYHEDRON CLIPPER & KINEMATIC INVERSE-PULLBACK TRANSFORMER
==========================================================================================================================================*/

function identityPullback()
{
    return {
        invR: [1, 0, 0, 0, 1, 0, 0, 0, 1],
        invT: [0, 0, 0]
    };
}

function clonePullback(pb)
{
    if (!pb) return identityPullback();
    return {
        invR: pb.invR.slice(),
        invT: pb.invT.slice()
    };
}

export function createBoxPoly(minX, minY, minZ, maxX, maxY, maxZ, planeType = 0)
{
    const v000 = [minX, minY, minZ];
    const v100 = [maxX, minY, minZ];
    const v110 = [maxX, maxY, minZ];
    const v010 = [minX, maxY, minZ];
    const v001 = [minX, minY, maxZ];
    const v101 = [maxX, minY, maxZ];
    const v111 = [maxX, maxY, maxZ];
    const v011 = [minX, maxY, maxZ];

    return {
        faces: [
            { normal: [1, 0, 0],  offset: maxX,  verts: [v100, v110, v111, v101], planeType },
            { normal: [-1, 0, 0], offset: -minX, verts: [v001, v011, v010, v000], planeType },
            { normal: [0, 1, 0],  offset: maxY,  verts: [v010, v011, v111, v110], planeType },
            { normal: [0, -1, 0], offset: -minY, verts: [v000, v100, v101, v001], planeType },
            { normal: [0, 0, 1],  offset: maxZ,  verts: [v001, v101, v111, v011], planeType },
            { normal: [0, 0, -1], offset: -minZ, verts: [v100, v000, v010, v110], planeType }
        ],
        pullback: identityPullback()
    };
}

function polygonArea3D(verts)
{
    if (verts.length < 3) return 0;
    const ax = verts[0][0], ay = verts[0][1], az = verts[0][2];
    let cx = 0, cy = 0, cz = 0;
    for (let i = 1; i < verts.length - 1; i++)
    {
        const ux = verts[i][0] - ax, uy = verts[i][1] - ay, uz = verts[i][2] - az;
        const vx = verts[i + 1][0] - ax, vy = verts[i + 1][1] - ay, vz = verts[i + 1][2] - az;
        cx += uy * vz - uz * vy;
        cy += uz * vx - ux * vz;
        cz += ux * vy - uy * vx;
    }
    return 0.5 * Math.hypot(cx, cy, cz);
}

/**
 * Clips convex polyhedron `poly` against half-space `dot(n, p) - offset <= 0`.
 * Preserves `poly.pullback` (`invR`, `invT`) so child pieces remember their unbroken parent coordinates!
 */
export function clipPolyByHalfSpace(poly, rawNormal, offset, planeType = 1)
{
    if (!poly || !poly.faces || poly.faces.length < 4) return null;
    const n = vnorm(rawNormal);
    const nx = n[0], ny = n[1], nz = n[2];

    const keptFaces = [];
    const capCandidates = [];
    let anyClipped = false;

    for (let fi = 0; fi < poly.faces.length; fi++)
    {
        const f = poly.faces[fi];
        const verts = f.verts;
        const m = verts.length;
        const outVerts = [];

        for (let i = 0; i < m; i++)
        {
            const a = verts[i];
            const b = verts[(i + 1) % m];
            const da = nx * a[0] + ny * a[1] + nz * a[2] - offset;
            const db = nx * b[0] + ny * b[1] + nz * b[2] - offset;

            if (da > EPS) anyClipped = true;

            if (da <= EPS)
            {
                outVerts.push(a);
                if (da >= -EPS) capCandidates.push(a);
            }

            if ((da < -EPS && db > EPS) || (da > EPS && db < -EPS))
            {
                const t = da / (da - db);
                const pt = [
                    a[0] + (b[0] - a[0]) * t,
                    a[1] + (b[1] - a[1]) * t,
                    a[2] + (b[2] - a[2]) * t
                ];
                outVerts.push(pt);
                capCandidates.push(pt);
            }
        }

        const clean = [];
        for (let i = 0; i < outVerts.length; i++)
        {
            const p = outVerts[i];
            const prev = clean.length > 0 ? clean[clean.length - 1] : outVerts[outVerts.length - 1];
            const d2 = (p[0] - prev[0]) ** 2 + (p[1] - prev[1]) ** 2 + (p[2] - prev[2]) ** 2;
            if (d2 > 1e-8) clean.push(p);
        }

        if (clean.length >= 3 && polygonArea3D(clean) > 1e-6)
        {
            keptFaces.push({
                normal: f.normal,
                offset: f.offset,
                verts: clean,
                planeType: f.planeType ?? 0
            });
        }
    }

    if (!anyClipped)
    {
        return {
            faces: poly.faces,
            pullback: clonePullback(poly.pullback)
        };
    }
    if (keptFaces.length < 3) return null;

    const uniqueCap = [];
    for (let i = 0; i < capCandidates.length; i++)
    {
        const p = capCandidates[i];
        let dup = false;
        for (let j = 0; j < uniqueCap.length; j++)
        {
            const q = uniqueCap[j];
            if ((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (p[2] - q[2]) ** 2 < 1e-6)
            {
                dup = true;
                break;
            }
        }
        if (!dup) uniqueCap.push(p);
    }

    if (uniqueCap.length >= 3)
    {
        let cx = 0, cy = 0, cz = 0;
        for (let i = 0; i < uniqueCap.length; i++)
        {
            cx += uniqueCap[i][0]; cy += uniqueCap[i][1]; cz += uniqueCap[i][2];
        }
        cx /= uniqueCap.length; cy /= uniqueCap.length; cz /= uniqueCap.length;

        const arb = Math.abs(ny) < 0.9 ? [0, 1, 0] : [1, 0, 0];
        const t1 = vnorm(vcross(arb, n));
        const t2 = vcross(n, t1);

        const withAngles = uniqueCap.map(p =>
        {
            const rx = p[0] - cx, ry = p[1] - cy, rz = p[2] - cz;
            const u = rx * t1[0] + ry * t1[1] + rz * t1[2];
            const w = rx * t2[0] + ry * t2[1] + rz * t2[2];
            return { p, ang: Math.atan2(w, u) };
        });
        withAngles.sort((a, b) => a.ang - b.ang);
        const capVerts = withAngles.map(x => x.p);

        if (polygonArea3D(capVerts) > 1e-6)
        {
            keptFaces.push({
                normal: [nx, ny, nz],
                offset,
                verts: capVerts,
                planeType
            });
        }
    }

    if (keptFaces.length < 4) return null;
    return {
        faces: keptFaces,
        pullback: clonePullback(poly.pullback)
    };
}

/**
 * Splits convex polyhedron `poly` across fracture plane `dot(n, p) = offset` with optional gap `gap >= 0`.
 * Marks `negChild` split face with `planeType = +1` and `posChild` split face with `planeType = -1`
 * so opposing fracture walls interlock 1-to-1 like a jigsaw puzzle!
 */
export function splitPolyByPlane(poly, rawNormal, offset, gap = 0)
{
    const n = vnorm(rawNormal);
    const halfGap = gap * 0.5;
    const neg = clipPolyByHalfSpace(poly, n, offset - halfGap, +1.0);
    const pos = clipPolyByHalfSpace(poly, [-n[0], -n[1], -n[2]], -(offset + halfGap), -1.0);
    return [neg, pos];
}

export function measurePoly(poly)
{
    if (!poly || !poly.faces) return null;
    const verts = [];
    for (let fi = 0; fi < poly.faces.length; fi++)
    {
        const fv = poly.faces[fi].verts;
        for (let i = 0; i < fv.length; i++)
        {
            const p = fv[i];
            let dup = false;
            for (let j = 0; j < verts.length; j++)
            {
                const q = verts[j];
                if ((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (p[2] - q[2]) ** 2 < 1e-5)
                {
                    dup = true;
                    break;
                }
            }
            if (!dup) verts.push(p);
        }
    }

    if (verts.length < 4) return null;

    let cx = 0, cy = 0, cz = 0;
    let minX = Infinity, minY = Infinity, minZ = Infinity;
    let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;

    for (let i = 0; i < verts.length; i++)
    {
        const [x, y, z] = verts[i];
        cx += x; cy += y; cz += z;
        if (x < minX) minX = x; if (x > maxX) maxX = x;
        if (y < minY) minY = y; if (y > maxY) maxY = y;
        if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;
    }
    cx /= verts.length; cy /= verts.length; cz /= verts.length;

    let maxR2 = 0;
    for (let i = 0; i < verts.length; i++)
    {
        const dx = verts[i][0] - cx, dy = verts[i][1] - cy, dz = verts[i][2] - cz;
        const r2 = dx * dx + dy * dy + dz * dz;
        if (r2 > maxR2) maxR2 = r2;
    }

    let vol = 0;
    for (let fi = 0; fi < poly.faces.length; fi++)
    {
        const f = poly.faces[fi];
        const area = polygonArea3D(f.verts);
        const h = f.offset - (f.normal[0] * cx + f.normal[1] * cy + f.normal[2] * cz);
        vol += (area * Math.max(0, h)) / 3;
    }

    return {
        verts,
        centroid: [cx, cy, cz],
        radius: Math.sqrt(maxR2),
        minX, minY, minZ,
        maxX, maxY, maxZ,
        spanX: maxX - minX,
        spanY: maxY - minY,
        spanZ: maxZ - minZ,
        minSpan: Math.min(maxX - minX, maxY - minY, maxZ - minZ),
        volume: vol
    };
}

export function isSubstantialBlock(poly, minThickness)
{
    const m = measurePoly(poly);
    if (!m) return false;
    return m.spanX >= minThickness && m.spanY >= minThickness && m.spanZ >= minThickness;
}

/**
 * Rigid-body hinge rotation + translation of `poly`, ALSO composing the exact inverse rigid pullback `T^{-1}`
 * so any displaced world point `p_new` maps straight back to its unbroken parent coordinate `p0`!
 */
export function transformPoly(poly, deltaPos = [0, 0, 0], rotAxis = [0, 1, 0], rotAngle = 0, pivot = null)
{
    if (!poly) return null;
    const m = pivot ? { centroid: pivot } : measurePoly(poly);
    if (!m) return poly;

    const c = m.centroid;
    const [dx, dy, dz] = deltaPos;
    const doRot = Math.abs(rotAngle) > 1e-6;
    const axis = doRot ? vnorm(rotAxis) : [0, 1, 0];

    const R = buildAxisAngleMatrix3(axis, rotAngle);
    const Rt = transposeMat3(R);

    // Forward rigid map: p_new = R*(p - c) + c + d = R*p + s, where s = c + d - R*c
    const Rc = applyMat3(R, c);
    const s = [c[0] + dx - Rc[0], c[1] + dy - Rc[1], c[2] + dz - Rc[2]];

    // Compose with existing pullback p0 = prevInvR * p + prevInvT:
    // p = Rt * (p_new - s) => p0 = (prevInvR * Rt) * p_new + (prevInvT - (prevInvR * Rt) * s)
    const prev = poly.pullback || identityPullback();
    const newInvR = mulMat3(prev.invR, Rt);
    const shift = applyMat3(newInvR, s);
    const newInvT = [
        prev.invT[0] - shift[0],
        prev.invT[1] - shift[1],
        prev.invT[2] - shift[2]
    ];

    const newFaces = poly.faces.map(f =>
    {
        const nn = doRot ? applyMat3(R, f.normal) : [f.normal[0], f.normal[1], f.normal[2]];
        const nv = f.verts.map(v =>
        {
            const rv = applyMat3(R, v);
            return [rv[0] + s[0], rv[1] + s[1], rv[2] + s[2]];
        });
        const newOffset = nn[0] * nv[0][0] + nn[1] * nv[0][1] + nn[2] * nv[0][2];
        return {
            normal: nn,
            offset: newOffset,
            verts: nv,
            planeType: f.planeType ?? 0
        };
    });

    return {
        faces: newFaces,
        pullback: { invR: newInvR, invT: newInvT }
    };
}

/**
 * Sculpts a stress-aligned geological parent polyhedron (exterior faces tagged `planeType = 0`).
 */
export function sculptGeologicalParentPoly(center, radii, frame, rng, options = {})
{
    const [cx, cy, cz] = center;
    const [rx, ry, rz] = radii;
    const dispersion = options.jointDispersion ?? 0.20;
    const cornerBlunt = options.cornerBlunt ?? 0.78;
    const flatBaseY = options.flatBaseY ?? null;

    const bound = Math.max(rx, ry, rz) * 2.25;
    let poly = createBoxPoly(
        cx - bound, flatBaseY !== null ? flatBaseY : cy - bound, cz - bound,
        cx + bound, cy + bound, cz + bound,
        0
    );

    const { strikeVec, dipVec, normal: bedNormal } = frame;
    const conj1 = rotateAroundAxis(dipVec, bedNormal, 0.98 + rng.signed() * 0.16);
    const conj2 = rotateAroundAxis(dipVec, bedNormal, -0.98 + rng.signed() * 0.16);

    const dirSpecs = [
        { dir: bedNormal, dist: ry * rng.range(0.84, 1.10), disp: dispersion * 0.7 },
        { dir: [-bedNormal[0], -bedNormal[1], -bedNormal[2]], dist: ry * rng.range(0.86, 1.12), disp: dispersion * 0.7 },
        { dir: dipVec, dist: rz * rng.range(0.80, 1.14), disp: dispersion },
        { dir: [-dipVec[0], -dipVec[1], -dipVec[2]], dist: rz * rng.range(0.80, 1.14), disp: dispersion },
        { dir: strikeVec, dist: rx * rng.range(0.82, 1.14), disp: dispersion },
        { dir: [-strikeVec[0], -strikeVec[1], -strikeVec[2]], dist: rx * rng.range(0.82, 1.14), disp: dispersion },
        { dir: conj1, dist: Math.hypot(rx, rz) * rng.range(0.60, 0.80), disp: dispersion * 1.1 },
        { dir: [-conj1[0], -conj1[1], -conj1[2]], dist: Math.hypot(rx, rz) * rng.range(0.60, 0.80), disp: dispersion * 1.1 },
        { dir: conj2, dist: Math.hypot(rx, rz) * rng.range(0.60, 0.80), disp: dispersion * 1.1 },
        { dir: [-conj2[0], -conj2[1], -conj2[2]], dist: Math.hypot(rx, rz) * rng.range(0.60, 0.80), disp: dispersion * 1.1 }
    ];

    const extraCuts = options.extraBevelCuts ?? 4;
    for (let i = 0; i < extraCuts; i++)
    {
        const phi = rng.float() * Math.PI * 2;
        const tilt = rng.range(0.25, 1.15);
        const horiz = [
            strikeVec[0] * Math.cos(phi) + dipVec[0] * Math.sin(phi),
            strikeVec[1] * Math.cos(phi) + dipVec[1] * Math.sin(phi),
            strikeVec[2] * Math.cos(phi) + dipVec[2] * Math.sin(phi)
        ];
        const sgn = rng.chance(0.72) ? 1 : -1;
        const d = vnorm([
            horiz[0] * Math.sin(tilt) + bedNormal[0] * sgn * Math.cos(tilt),
            horiz[1] * Math.sin(tilt) + bedNormal[1] * sgn * Math.cos(tilt),
            horiz[2] * Math.sin(tilt) + bedNormal[2] * sgn * Math.cos(tilt)
        ]);
        const effR = Math.hypot(
            rx * vdot(d, strikeVec),
            ry * vdot(d, bedNormal),
            rz * vdot(d, dipVec)
        );
        dirSpecs.push({ dir: d, dist: effR * rng.range(0.84, 1.06), disp: dispersion * 0.8 });
    }

    for (let i = 0; i < dirSpecs.length; i++)
    {
        const sp = dirSpecs[i];
        if (flatBaseY !== null && sp.dir[1] < -0.75) continue;
        const n = perturbNormal(sp.dir, rng, sp.disp);
        const off = n[0] * cx + n[1] * cy + n[2] * cz + sp.dist;
        const next = clipPolyByHalfSpace(poly, n, off, 0);
        if (next) poly = next;
    }

    const info = measurePoly(poly);
    if (info && cornerBlunt > 0)
    {
        const [mcx, mcy, mcz] = info.centroid;
        const maxAllowed = Math.max(rx, ry, rz) * (1.15 + (1 - cornerBlunt) * 0.42);
        for (let i = 0; i < info.verts.length; i++)
        {
            const v = info.verts[i];
            if (flatBaseY !== null && v[1] <= flatBaseY + 0.05) continue;
            const dx = v[0] - mcx, dy = v[1] - mcy, dz = v[2] - mcz;
            const dist = Math.hypot(dx, dy, dz);
            if (dist > maxAllowed)
            {
                const dir = perturbNormal([dx / dist, dy / dist, dz / dist], rng, 0.14);
                const clipD = maxAllowed * rng.range(0.88, 0.97);
                const off = dir[0] * mcx + dir[1] * mcy + dir[2] * mcz + clipD;
                const next = clipPolyByHalfSpace(poly, dir, off, 0);
                if (next) poly = next;
            }
        }
    }

    return poly;
}

export function compileFractureBlock(poly, options = {})
{
    const m = measurePoly(poly);
    if (!m || m.volume < 1e-4 || m.minSpan < 0.03) return null;

    const numPlanes = poly.faces.length;
    const planes = new Float32Array(numPlanes * 4);
    const planeTypes = new Float32Array(numPlanes);
    for (let i = 0; i < numPlanes; i++)
    {
        const f = poly.faces[i];
        planes[i * 4] = f.normal[0];
        planes[i * 4 + 1] = f.normal[1];
        planes[i * 4 + 2] = f.normal[2];
        planes[i * 4 + 3] = f.offset;
        planeTypes[i] = f.planeType ?? 0;
    }

    const pb = poly.pullback || identityPullback();
    return {
        cx: m.centroid[0],
        cy: m.centroid[1],
        cz: m.centroid[2],
        radius: m.radius,
        minX: m.minX, minY: m.minY, minZ: m.minZ,
        maxX: m.maxX, maxY: m.maxY, maxZ: m.maxZ,
        volume: m.volume,
        planes,
        planeTypes,
        numPlanes,
        invR: new Float32Array(pb.invR),
        invT: new Float32Array(pb.invT),
        edgeBevel: options.edgeBevel ?? 0.035,
        isSpall: !!options.isSpall,
        noThermalCracks: !!options.noThermalCracks,
        strataWeight: options.strataWeight ?? 0.25,
        hardness: options.hardness ?? 0.78,
        seedSalt: options.seedSalt ?? 0
    };
}

/*==========================================================================================================================================
                                     2. CURATED PURE-SDF FRACTURE SHOWCASE PRESETS
==========================================================================================================================================*/

export const FRACTURE_PRESETS = [
    {
        id: 'cracked-monolith',
        name: '01 · Cracked Jigsaw Monolith',
        badge: 'JIGSAW + HINGE',
        subtitle: 'Basal-hinged granite spire split with matching jigsaw walls, plumose hackles, chockstones & spalls',
        lithology: 'granite',
        defaults: {
            presetId: 'cracked-monolith',
            seed: 1337,
            scale: 7.2,
            chasmAperture: 0.62,
            hingeTiltDeg: 5.8,
            faultHeave: 0.52,
            jigsawRoughness: 0.65,
            plumoseStrength: 0.68,
            edgeChamfer: 0.34,
            thermalDeltaT: 38,
            sunAzimuthDeg: 48,
            sunElevationDeg: 40,
            directionalBias: 0.55,
            thermalSkinDepth: 0.44,
            crazing90Strength: 0.30,
            crazingSpacing: 0.68,
            voronoiJointStrength: 0.36,
            exfoliationStrength: 0.14,
            spallCalving: 0.68,
            cutawayEnabled: false,
            cutawayAxis: 0,
            cutawayOffset: 0.35,
            meshResolution: 84
        }
    },
    {
        id: 'al-naslaa-split',
        name: '02 · Al-Naslaa Laser-Split Boulder',
        badge: 'STRATA PULLBACK',
        subtitle: 'Balanced twin sandstone megaliths cleaved by a razor tensile joint with continuous cross-beds across the gap',
        lithology: 'sandstone',
        defaults: {
            presetId: 'al-naslaa-split',
            seed: 4021,
            scale: 7.0,
            chasmAperture: 0.50,
            hingeTiltDeg: 2.1,
            faultHeave: 0.28,
            jigsawRoughness: 0.14,
            plumoseStrength: 0.78,
            edgeChamfer: 0.22,
            thermalDeltaT: 46,
            sunAzimuthDeg: 44,
            sunElevationDeg: 42,
            directionalBias: 0.68,
            thermalSkinDepth: 0.36,
            crazing90Strength: 0.44,
            crazingSpacing: 0.62,
            voronoiJointStrength: 0.16,
            exfoliationStrength: 0.08,
            spallCalving: 0.45,
            cutawayEnabled: false,
            cutawayAxis: 2,
            cutawayOffset: 0.40,
            meshResolution: 84
        }
    },
    {
        id: 'thermal-90-fin',
        name: '03 · 90° T-Junction Sun-Crazed Fin',
        badge: '90° T-JUNCTIONS',
        subtitle: 'Directional solar ΔT thermal shock carving hierarchical 90° orthogonal T-junction V-tipped skin cracks',
        lithology: 'redSandstone',
        defaults: {
            presetId: 'thermal-90-fin',
            seed: 7719,
            scale: 7.0,
            chasmAperture: 0.36,
            hingeTiltDeg: 2.8,
            faultHeave: 0.22,
            jigsawRoughness: 0.42,
            plumoseStrength: 0.52,
            edgeChamfer: 0.28,
            thermalDeltaT: 64,
            sunAzimuthDeg: 52,
            sunElevationDeg: 36,
            directionalBias: 0.82,
            thermalSkinDepth: 0.58,
            crazing90Strength: 0.92,
            crazingSpacing: 0.54,
            voronoiJointStrength: 0.14,
            exfoliationStrength: 0.10,
            spallCalving: 0.40,
            cutawayEnabled: false,
            cutawayAxis: 0,
            cutawayOffset: 0.25,
            meshResolution: 88
        }
    },
    {
        id: 'exfoliation-dome',
        name: '04 · Granite Exfoliation Dome',
        badge: 'ONION-SKIN SDF',
        subtitle: 'Unloading concentric phi_0 surface-parallel granite sheets peeling off an intact batholith core',
        lithology: 'sierraGranite',
        defaults: {
            presetId: 'exfoliation-dome',
            seed: 9104,
            scale: 7.4,
            chasmAperture: 0.32,
            hingeTiltDeg: 1.9,
            faultHeave: 0.16,
            jigsawRoughness: 0.36,
            plumoseStrength: 0.35,
            edgeChamfer: 0.42,
            thermalDeltaT: 48,
            sunAzimuthDeg: 46,
            sunElevationDeg: 45,
            directionalBias: 0.45,
            thermalSkinDepth: 0.52,
            crazing90Strength: 0.24,
            crazingSpacing: 0.82,
            voronoiJointStrength: 0.28,
            exfoliationStrength: 0.94,
            spallCalving: 0.65,
            cutawayEnabled: false,
            cutawayAxis: 0,
            cutawayOffset: 0.15,
            meshResolution: 84
        }
    },
    {
        id: 'columnar-basalt',
        name: '05 · Columnar Basalt Cooling Cliff',
        badge: 'HEX COOLING',
        subtitle: 'Thermal contraction polygonal basalt columns with stepped chisel cross-joints & toppled drums',
        lithology: 'basalt',
        defaults: {
            presetId: 'columnar-basalt',
            seed: 5512,
            scale: 7.2,
            chasmAperture: 0.48,
            hingeTiltDeg: 3.4,
            faultHeave: 0.40,
            jigsawRoughness: 0.28,
            plumoseStrength: 0.58,
            edgeChamfer: 0.24,
            thermalDeltaT: 54,
            sunAzimuthDeg: 50,
            sunElevationDeg: 38,
            directionalBias: 0.25,
            thermalSkinDepth: 0.48,
            crazing90Strength: 0.32,
            crazingSpacing: 0.58,
            voronoiJointStrength: 0.82,
            exfoliationStrength: 0.06,
            spallCalving: 0.75,
            cutawayEnabled: false,
            cutawayAxis: 0,
            cutawayOffset: 0.30,
            meshResolution: 86
        }
    },
    {
        id: 'frost-wedged-arete',
        name: '06 · Frost-Wedged Conjugate Arête',
        badge: 'CONJUGATE 60°',
        subtitle: 'Mohr-Coulomb 60°/120° conjugate alpine shear planes dilated by ice-lens frost wedging & topple',
        lithology: 'alpineGneiss',
        defaults: {
            presetId: 'frost-wedged-arete',
            seed: 3188,
            scale: 7.4,
            chasmAperture: 0.66,
            hingeTiltDeg: 6.4,
            faultHeave: 0.62,
            jigsawRoughness: 0.58,
            plumoseStrength: 0.62,
            edgeChamfer: 0.26,
            thermalDeltaT: 44,
            sunAzimuthDeg: 54,
            sunElevationDeg: 36,
            directionalBias: 0.50,
            thermalSkinDepth: 0.50,
            crazing90Strength: 0.38,
            crazingSpacing: 0.64,
            voronoiJointStrength: 0.54,
            exfoliationStrength: 0.20,
            spallCalving: 0.80,
            cutawayEnabled: false,
            cutawayAxis: 0,
            cutawayOffset: 0.20,
            meshResolution: 84
        }
    }
];

export const LITHOLOGY_THEMES = {
    granite: {
        rind: [0.13, 0.12, 0.11],      // Aged dark grey-brown granite patina (linear RGB)
        fresh: [0.46, 0.42, 0.37],     // Bright pale quartz-feldspar cleaved interior!
        iron: [0.34, 0.17, 0.08],
        lichen: [0.15, 0.20, 0.10]
    },
    sandstone: {
        rind: [0.16, 0.10, 0.062],     // Dark manganese/iron desert-varnish crust
        fresh: [0.54, 0.39, 0.23],     // Warm golden-buff unweathered sandstone split!
        iron: [0.40, 0.18, 0.075],
        lichen: [0.22, 0.18, 0.11]
    },
    redSandstone: {
        rind: [0.14, 0.062, 0.045],    // Deep dark desert varnish crust
        fresh: [0.50, 0.22, 0.115],    // Vivid Navajo vermilion-terracotta fresh split!
        iron: [0.36, 0.12, 0.055],
        lichen: [0.20, 0.15, 0.09]
    },
    sierraGranite: {
        rind: [0.17, 0.16, 0.15],      // Weathered Yosemite granodiorite rind
        fresh: [0.52, 0.50, 0.46],     // Crisp pale exfoliated granodiorite scar!
        iron: [0.32, 0.19, 0.10],
        lichen: [0.16, 0.21, 0.11]
    },
    basalt: {
        rind: [0.045, 0.047, 0.053],   // Authentic dark charcoal volcanic basalt!
        fresh: [0.145, 0.155, 0.175],  // Blue-grey freshly snapped basalt joint!
        iron: [0.30, 0.14, 0.06],      // Oxidized iron-rich basalt joint rust
        lichen: [0.14, 0.19, 0.09]
    },
    alpineGneiss: {
        rind: [0.095, 0.105, 0.12],    // Dark weathered high-alpine gneiss crust
        fresh: [0.39, 0.41, 0.45],     // Bright pale frost-shattered gneiss cleavage!
        iron: [0.29, 0.16, 0.08],
        lichen: [0.17, 0.23, 0.10]
    }
};

export function getFracturePreset(presetId)
{
    return FRACTURE_PRESETS.find(p => p.id === presetId) || FRACTURE_PRESETS[0];
}

export function createDefaultFractureParams(presetId = 'cracked-monolith')
{
    const p = getFracturePreset(presetId);
    return { ...p.defaults, lithology: p.lithology };
}

/*==========================================================================================================================================
                              3. STRUCTURAL POLYHEDRAL FRACTURE BUILDERS FOR ALL 6 PRESETS
==========================================================================================================================================*/

function addPedestalSpalls(blocks, toWorld, baseFrame, rng, S, bevel, spallCalving, strataWeight, crackSalt)
{
    if (spallCalving <= 0.05) return;
    const count = Math.max(2, Math.round(6 * spallCalving));
    const placements = [
        [0.01, 0.20, 0.14], [-0.14, 0.22, 0.11], [0.16, 0.19, 0.12],
        [0.02, -0.20, 0.13], [-0.26, 0.15, 0.11], [0.27, 0.13, 0.11],
        [-0.18, -0.17, 0.10], [0.20, -0.16, 0.10]
    ];
    for (let i = 0; i < Math.min(count, placements.length); i++)
    {
        const [ac, al, sc] = placements[i];
        const r = S * sc * rng.range(0.45, 0.72) * (0.65 + 0.45 * spallCalving);
        const h = r * rng.range(0.36, 0.62);
        const pos = toWorld(ac + rng.signed() * 0.02, 0, al + rng.signed() * 0.02);
        pos[1] = h * 0.46;
        const sf = buildStrikeDipFrame(rng.float() * Math.PI * 2, rng.range(0.12, 0.55));
        let poly = sculptGeologicalParentPoly(
            pos,
            [r * rng.range(0.85, 1.25), h, r * rng.range(0.80, 1.18)],
            sf,
            rng,
            { jointDispersion: 0.26, cornerBlunt: 0.74, flatBaseY: 0, extraBevelCuts: 3 }
        );
        // Tag top face of calved spall as fresh split (`+1`) so fallen slabs show their bright cleaved scar!
        if (poly && poly.faces.length > 0)
        {
            let bestFi = -1, bestNy = 0.35;
            for (let fi = 0; fi < poly.faces.length; fi++)
            {
                if (poly.faces[fi].normal[1] > bestNy)
                {
                    bestNy = poly.faces[fi].normal[1];
                    bestFi = fi;
                }
            }
            if (bestFi >= 0) poly.faces[bestFi].planeType = 1.0;
        }
        const blk = compileFractureBlock(poly, {
            edgeBevel: bevel * 0.85,
            isSpall: true,
            noThermalCracks: true,
            strataWeight,
            seedSalt: crackSalt + 700 + i * 23
        });
        if (blk) blocks.push(blk);
    }
}

function buildCrackedMonolithBlocks(params, rng, crackSalt)
{
    const S = params.scale;
    const blocks = [];
    const aperture = params.chasmAperture ?? 0.62;
    const tiltRad = (params.hingeTiltDeg ?? 5.8) * DEG2RAD;
    const faultSlip = params.faultHeave ?? 0.52;
    const bevel = S * (0.0020 + 0.0085 * (params.edgeChamfer ?? 0.34));
    const minThick = S * 0.062;

    const chasmAz = 0.42;
    const masterT = [Math.sin(chasmAz), 0, Math.cos(chasmAz)];
    const masterN = [Math.cos(chasmAz), 0, -Math.sin(chasmAz)];

    const toWorld = (across, yFrac, along) => [
        masterN[0] * (across * S) + masterT[0] * (along * S),
        yFrac * S,
        masterN[2] * (across * S) + masterT[2] * (along * S)
    ];

    const baseSep = (0.010 + 0.036 * aperture) * S;
    const leftTilt = tiltRad * 0.48;
    const rightTilt = -tiltRad * 1.05;
    const rightDropY = -(0.018 + 0.052 * faultSlip) * S;
    const rightSlipAlong = (0.008 + 0.026 * faultSlip) * S;
    const leftHinge = toWorld(-0.08, 0.02, 0.0);
    const rightHinge = toWorld(0.10, 0.02, 0.0);

    const buttresses = [
        { across:  0.00, y: 0.47, along: -0.01, rx: 0.27, ry: 0.52, rz: 0.24, kink:  0.05, fDip:  0.05, crownFork: true },
        { across: -0.01, y: 0.30, along:  0.15, rx: 0.24, ry: 0.33, rz: 0.19, kink: -0.07, fDip: -0.06, crownFork: false },
        { across:  0.01, y: 0.35, along: -0.15, rx: 0.24, ry: 0.39, rz: 0.19, kink:  0.08, fDip:  0.04, crownFork: true },
        { across: -0.17, y: 0.42, along: -0.02, rx: 0.18, ry: 0.45, rz: 0.20, kink:  0.00, fDip:  0.00, crownFork: true },
        { across:  0.16, y: 0.39, along:  0.02, rx: 0.18, ry: 0.42, rz: 0.20, kink:  0.00, fDip:  0.00, crownFork: true }
    ];

    for (let i = 0; i < buttresses.length; i++)
    {
        const sb = buttresses[i];
        const center = toWorld(sb.across, sb.y, sb.along);
        const bf = buildStrikeDipFrame(0.31 + rng.signed() * 0.08, 0.56 + rng.signed() * 0.06);

        const parentPoly = sculptGeologicalParentPoly(
            center,
            [S * sb.rx, S * sb.ry, S * sb.rz],
            bf,
            rng,
            { jointDispersion: 0.17, cornerBlunt: 0.72, flatBaseY: 0, extraBevelCuts: 5 }
        );
        if (!parentPoly) continue;

        let pieces = [];
        if (Math.abs(sb.across) < 0.08)
        {
            const fNorm = vnorm([
                masterN[0] + masterT[0] * sb.kink,
                sb.fDip,
                masterN[2] + masterT[2] * sb.kink
            ]);
            const fOff = vdot(fNorm, toWorld(sb.kink * 0.08, 0.32, sb.along));
            const [leftHalf, rightHalf] = splitPolyByPlane(parentPoly, fNorm, fOff, baseSep);

            if (leftHalf && isSubstantialBlock(leftHalf, minThick))
            {
                const movedL = transformPoly(
                    leftHalf,
                    [-masterN[0] * baseSep * 0.35, 0, -masterN[2] * baseSep * 0.35],
                    masterT,
                    leftTilt,
                    leftHinge
                );
                pieces.push({ poly: movedL, side: -1 });
            }
            if (rightHalf && isSubstantialBlock(rightHalf, minThick))
            {
                const movedR = transformPoly(
                    rightHalf,
                    [
                        masterN[0] * baseSep * 0.65 + masterT[0] * rightSlipAlong,
                        rightDropY,
                        masterN[2] * baseSep * 0.65 + masterT[2] * rightSlipAlong
                    ],
                    masterT,
                    rightTilt,
                    rightHinge
                );
                pieces.push({ poly: movedR, side: 1 });
            }
        }
        else
        {
            const side = sb.across < 0 ? -1 : 1;
            const moved = side < 0
                ? transformPoly(parentPoly, [-masterN[0] * baseSep * 0.35, 0, -masterN[2] * baseSep * 0.35], masterT, leftTilt, leftHinge)
                : transformPoly(
                    parentPoly,
                    [
                        masterN[0] * baseSep * 0.65 + masterT[0] * rightSlipAlong,
                        rightDropY,
                        masterN[2] * baseSep * 0.65 + masterT[2] * rightSlipAlong
                    ],
                    masterT,
                    rightTilt,
                    rightHinge
                );
            pieces.push({ poly: moved, side });
        }

        // Secondary Y-fork summit splits & dipping sheet step
        const finalPolys = [];
        for (let p = 0; p < pieces.length; p++)
        {
            const { poly, side } = pieces[p];
            const m = measurePoly(poly);
            if (!m) continue;

            if (sb.crownFork && m.maxY > S * 0.50 && m.spanX > minThick * 2.1)
            {
                const forkTilt = side < 0 ? -0.26 : 0.28;
                const yNorm = vnorm([
                    masterN[0] * Math.cos(forkTilt) + masterT[0] * 0.22,
                    side < 0 ? 0.24 : -0.22,
                    masterN[2] * Math.cos(forkTilt) + masterT[2] * 0.22
                ]);
                const forkPivot = [m.centroid[0], m.minY + m.spanY * 0.36, m.centroid[2]];
                const forkGap = (0.004 + 0.014 * aperture) * S;
                const [innerHorn, outerHorn] = splitPolyByPlane(poly, yNorm, vdot(yNorm, forkPivot), forkGap);

                if (innerHorn && outerHorn && isSubstantialBlock(innerHorn, minThick) && isSubstantialBlock(outerHorn, minThick))
                {
                    const outTilt = (side < 0 ? 0.38 : -0.42) * tiltRad;
                    const tiltedOuter = transformPoly(
                        outerHorn,
                        [side * masterN[0] * S * 0.008, -S * 0.010 * faultSlip, side * masterN[2] * S * 0.008],
                        masterT,
                        outTilt,
                        [forkPivot[0], m.minY + m.spanY * 0.18, forkPivot[2]]
                    );
                    finalPolys.push(innerHorn, tiltedOuter);
                    continue;
                }
            }
            finalPolys.push(poly);
        }

        // Sub-horizontal dipping release joint across tall blocks
        for (let fp = 0; fp < finalPolys.length; fp++)
        {
            const poly = finalPolys[fp];
            const m = measurePoly(poly);
            if (!m || !isSubstantialBlock(poly, minThick)) continue;

            if (m.spanY > S * 0.44 && i < 3)
            {
                const shNorm = vnorm([masterN[0] * 0.22, 0.95, masterT[2] * 0.16]);
                const splitH = m.minY + m.spanY * 0.48;
                const shOff = vdot(shNorm, [m.centroid[0], splitH, m.centroid[2]]);
                const shGap = (0.003 + 0.008 * aperture) * S;
                const [lowerB, upperB] = splitPolyByPlane(poly, shNorm, shOff, shGap);
                if (lowerB && upperB && isSubstantialBlock(lowerB, minThick) && isSubstantialBlock(upperB, minThick))
                {
                    const slippedUpper = transformPoly(
                        upperB,
                        [masterN[0] * S * 0.010 * faultSlip, 0, masterT[2] * S * 0.008 * faultSlip],
                        [0, 1, 0],
                        0.03 * faultSlip
                    );
                    const b0 = compileFractureBlock(lowerB, { edgeBevel: bevel, strataWeight: 0.22, seedSalt: crackSalt + i * 37 + fp * 7 });
                    const b1 = compileFractureBlock(slippedUpper, { edgeBevel: bevel, strataWeight: 0.22, seedSalt: crackSalt + i * 37 + fp * 7 + 3 });
                    if (b0) blocks.push(b0);
                    if (b1) blocks.push(b1);
                    continue;
                }
            }

            const compiled = compileFractureBlock(poly, { edgeBevel: bevel, strataWeight: 0.22, seedSalt: crackSalt + i * 31 + fp * 11 });
            if (compiled) blocks.push(compiled);
        }
    }

    // Wedged angular chockstones caught inside the opened central chasm!
    if (aperture > 0.18)
    {
        const chockSpecs = [
            { yFrac: 0.50, along: 0.03, rFrac: 0.068 },
            { yFrac: 0.29, along: -0.05, rFrac: 0.058 }
        ];
        for (let c = 0; c < chockSpecs.length; c++)
        {
            const cs = chockSpecs[c];
            const cPos = toWorld(0.01 + aperture * 0.02, cs.yFrac, cs.along);
            const cr = S * cs.rFrac * (0.75 + 0.45 * aperture);
            const cf = buildStrikeDipFrame(rng.float() * Math.PI, rng.range(0.35, 0.95));
            let cPoly = sculptGeologicalParentPoly(cPos, [cr * 1.15, cr * 0.92, cr * 1.05], cf, rng, {
                jointDispersion: 0.26, cornerBlunt: 0.65, extraBevelCuts: 3
            });
            if (cPoly)
            {
                for (let fi = 0; fi < cPoly.faces.length; fi++)
                {
                    if (fi % 2 === 0) cPoly.faces[fi].planeType = 1.0;
                }
                const cBlk = compileFractureBlock(cPoly, {
                    edgeBevel: bevel * 0.75,
                    isSpall: true,
                    noThermalCracks: true,
                    strataWeight: 0.15,
                    seedSalt: crackSalt + 500 + c * 19
                });
                if (cBlk) blocks.push(cBlk);
            }
        }
    }

    addPedestalSpalls(blocks, toWorld, null, rng, S, bevel, params.spallCalving ?? 0.68, 0.18, crackSalt);
    return blocks;
}

function buildAlNaslaaSplitBlocks(params, rng, crackSalt)
{
    const S = params.scale;
    const blocks = [];
    const aperture = params.chasmAperture ?? 0.50;
    const tiltRad = (params.hingeTiltDeg ?? 2.1) * DEG2RAD;
    const faultSlip = params.faultHeave ?? 0.28;
    const bevel = S * (0.0018 + 0.0075 * (params.edgeChamfer ?? 0.22));

    const chasmAz = 0.28;
    const masterT = [Math.sin(chasmAz), 0, Math.cos(chasmAz)];
    const masterN = [Math.cos(chasmAz), 0, -Math.sin(chasmAz)];
    const toWorld = (across, yFrac, along) => [
        masterN[0] * (across * S) + masterT[0] * (along * S),
        yFrac * S,
        masterN[2] * (across * S) + masterT[2] * (along * S)
    ];

    const bf = buildStrikeDipFrame(0.15, 0.08); // Nearly horizontal sandstone bedding!

    // Main balanced megalith hovering above two narrow weathered pedestals!
    const boulderCenter = toWorld(0.0, 0.46, 0.0);
    const parentBoulder = sculptGeologicalParentPoly(
        boulderCenter,
        [S * 0.46, S * 0.36, S * 0.28],
        bf,
        rng,
        { jointDispersion: 0.13, cornerBlunt: 0.82, flatBaseY: S * 0.11, extraBevelCuts: 6 }
    );

    const gap = (0.016 + 0.036 * aperture) * S;
    const laserNormal = vnorm([masterN[0], 0.015, masterN[2]]);
    const [leftB, rightB] = splitPolyByPlane(parentBoulder, laserNormal, 0.0, gap);

    const leftPedPos = toWorld(-0.20, 0.07, 0.0);
    const rightPedPos = toWorld(0.21, 0.07, 0.01);

    if (leftB)
    {
        const movedL = transformPoly(
            leftB,
            [-masterN[0] * gap * 0.25, 0, -masterN[2] * gap * 0.25],
            masterT,
            tiltRad * 0.45,
            leftPedPos
        );
        const bL = compileFractureBlock(movedL, { edgeBevel: bevel, strataWeight: 0.92, seedSalt: crackSalt + 11 });
        if (bL) blocks.push(bL);
    }

    if (rightB)
    {
        const movedR = transformPoly(
            rightB,
            [masterN[0] * gap * 0.45, -S * 0.032 * faultSlip, masterN[2] * gap * 0.45],
            masterT,
            -tiltRad * 0.85,
            rightPedPos
        );
        const bR = compileFractureBlock(movedR, { edgeBevel: bevel, strataWeight: 0.92, seedSalt: crackSalt + 23 });
        if (bR) blocks.push(bR);
    }

    // Two narrow undercut sandstone pedestals supporting left & right halves
    const pedL = sculptGeologicalParentPoly(leftPedPos, [S * 0.14, S * 0.10, S * 0.15], bf, rng, {
        jointDispersion: 0.18, cornerBlunt: 0.80, flatBaseY: 0, extraBevelCuts: 3
    });
    const pedR = sculptGeologicalParentPoly(rightPedPos, [S * 0.15, S * 0.095, S * 0.15], bf, rng, {
        jointDispersion: 0.18, cornerBlunt: 0.80, flatBaseY: 0, extraBevelCuts: 3
    });
    const cpL = compileFractureBlock(pedL, { edgeBevel: bevel * 1.2, strataWeight: 0.85, seedSalt: crackSalt + 41 });
    const cpR = compileFractureBlock(pedR, { edgeBevel: bevel * 1.2, strataWeight: 0.85, seedSalt: crackSalt + 53 });
    if (cpL) blocks.push(cpL);
    if (cpR) blocks.push(cpR);

    addPedestalSpalls(blocks, toWorld, bf, rng, S, bevel, params.spallCalving ?? 0.45, 0.85, crackSalt);
    return blocks;
}

function buildThermal90FinBlocks(params, rng, crackSalt)
{
    const S = params.scale;
    const blocks = [];
    const aperture = params.chasmAperture ?? 0.36;
    const tiltRad = (params.hingeTiltDeg ?? 2.8) * DEG2RAD;
    const faultSlip = params.faultHeave ?? 0.22;
    const bevel = S * (0.0020 + 0.0075 * (params.edgeChamfer ?? 0.28));

    const az = 0.32;
    const masterT = [Math.sin(az), 0, Math.cos(az)];
    const masterN = [Math.cos(az), 0, -Math.sin(az)];
    const toWorld = (across, yFrac, along) => [
        masterN[0] * (across * S) + masterT[0] * (along * S),
        yFrac * S,
        masterN[2] * (across * S) + masterT[2] * (along * S)
    ];

    const bf = buildStrikeDipFrame(0.24, 0.16);
    // Broad sandstone fin presenting a wide sun-drenched SW face for 90-degree T-junction crazing!
    const finPoly = sculptGeologicalParentPoly(
        toWorld(0, 0.42, 0),
        [S * 0.48, S * 0.46, S * 0.20],
        bf,
        rng,
        { jointDispersion: 0.12, cornerBlunt: 0.82, flatBaseY: 0, extraBevelCuts: 4 }
    );

    // Split fin across two master vertical thermal joints at right angles to the fin crest
    const gap1 = (0.008 + 0.024 * aperture) * S;
    const [leftSlab, midRight] = splitPolyByPlane(finPoly, masterN, -S * 0.11, gap1);
    if (leftSlab)
    {
        const mL = transformPoly(leftSlab, [-masterN[0] * gap1 * 0.3, 0, -masterN[2] * gap1 * 0.3], masterT, tiltRad * 0.45, toWorld(-0.22, 0.02, 0));
        const bL = compileFractureBlock(mL, { edgeBevel: bevel, strataWeight: 0.65, seedSalt: crackSalt + 17 });
        if (bL) blocks.push(bL);
    }
    if (midRight)
    {
        const [centerTower, rightWing] = splitPolyByPlane(midRight, masterN, S * 0.16, gap1 * 0.85);
        if (centerTower)
        {
            const bC = compileFractureBlock(centerTower, { edgeBevel: bevel, strataWeight: 0.65, seedSalt: crackSalt + 29 });
            if (bC) blocks.push(bC);
        }
        if (rightWing)
        {
            const mR = transformPoly(
                rightWing,
                [masterN[0] * gap1 * 0.5, -S * 0.026 * faultSlip, masterN[2] * gap1 * 0.5],
                masterT,
                -tiltRad * 0.75,
                toWorld(0.24, 0.02, 0)
            );
            const bR = compileFractureBlock(mR, { edgeBevel: bevel, strataWeight: 0.65, seedSalt: crackSalt + 43 });
            if (bR) blocks.push(bR);
        }
    }

    addPedestalSpalls(blocks, toWorld, bf, rng, S, bevel, params.spallCalving ?? 0.40, 0.65, crackSalt);
    return blocks;
}

function buildExfoliationDomeBlocks(params, rng, crackSalt)
{
    const S = params.scale;
    const blocks = [];
    const aperture = params.chasmAperture ?? 0.32;
    const tiltRad = (params.hingeTiltDeg ?? 1.9) * DEG2RAD;
    const bevel = S * (0.0040 + 0.0110 * (params.edgeChamfer ?? 0.42));

    const bf = buildStrikeDipFrame(0.2, 0.10);
    const toWorld = (x, y, z) => [x * S, y * S, z * S];

    const domePoly = sculptGeologicalParentPoly(
        [0, S * 0.36, 0],
        [S * 0.46, S * 0.42, S * 0.42],
        bf,
        rng,
        { jointDispersion: 0.14, cornerBlunt: 0.90, flatBaseY: 0, extraBevelCuts: 8 }
    );

    const gap = (0.006 + 0.022 * aperture) * S;
    const jNorm = vnorm([0.88, 0.08, -0.46]);
    const [dA, dB] = splitPolyByPlane(domePoly, jNorm, S * 0.06, gap);
    if (dA)
    {
        const bA = compileFractureBlock(dA, { edgeBevel: bevel, strataWeight: 0.08, seedSalt: crackSalt + 13 });
        if (bA) blocks.push(bA);
    }
    if (dB)
    {
        const mB = transformPoly(dB, [jNorm[0] * gap * 0.4, -S * 0.015 * (params.faultHeave ?? 0.16), jNorm[2] * gap * 0.4], [0.46, 0, 0.88], -tiltRad * 0.6, [S * 0.18, 0, -S * 0.08]);
        const bB = compileFractureBlock(mB, { edgeBevel: bevel, strataWeight: 0.08, seedSalt: crackSalt + 27 });
        if (bB) blocks.push(bB);
    }

    addPedestalSpalls(blocks, toWorld, bf, rng, S, bevel * 0.7, params.spallCalving ?? 0.65, 0.08, crackSalt);
    return blocks;
}

function buildColumnarBasaltBlocks(params, rng, crackSalt)
{
    const S = params.scale;
    const blocks = [];
    const aperture = params.chasmAperture ?? 0.48;
    const tiltRad = (params.hingeTiltDeg ?? 3.4) * DEG2RAD;
    const faultSlip = params.faultHeave ?? 0.40;
    const bevel = S * (0.0016 + 0.0055 * (params.edgeChamfer ?? 0.24));

    const colRadius = S * 0.105;
    const spacingX = colRadius * 1.82;
    const spacingZ = colRadius * 1.58;
    const gap = (0.003 + 0.011 * aperture) * S;

    const ringSpecs = [
        // Central tall colonnade
        [0, 0, 0.72], [-1, 0, 0.66], [1, 0, 0.58],
        [-0.5, -1, 0.76], [0.5, -1, 0.69], [-1.5, -1, 0.54], [1.5, -1, 0.50],
        // Stepped front colonnade tiers
        [-0.5, 1, 0.44], [0.5, 1, 0.36], [-1.5, 1, 0.39], [1.5, 1, 0.28],
        [0, 2, 0.22], [-1, 2, 0.18]
    ];

    for (let i = 0; i < ringSpecs.length; i++)
    {
        const [gx, gz, hFrac] = ringSpecs[i];
        const cx = gx * spacingX + rng.signed() * S * 0.008;
        const cz = gz * spacingZ + rng.signed() * S * 0.008;
        const colH = S * hFrac * (1.0 - (gx > 0.2 ? 0.10 * faultSlip : 0.0));
        const r = colRadius - gap * 0.5;

        // Build crisp 6-sided hexagonal basalt prism with slight geological face variation + top chisel cap!
        let poly = createBoxPoly(cx - r * 1.3, 0, cz - r * 1.3, cx + r * 1.3, colH + r, cz + r * 1.3, 0);
        const sides = i % 4 === 0 ? 5 : 6;
        const rot0 = 0.26 + rng.signed() * 0.09;
        for (let s = 0; s < sides; s++)
        {
            const ang = rot0 + (s * Math.PI * 2) / sides + rng.signed() * 0.06;
            const n = vnorm([Math.cos(ang), rng.signed() * 0.02, Math.sin(ang)]);
            const off = n[0] * cx + n[2] * cz + r * rng.range(0.94, 1.05);
            poly = clipPolyByHalfSpace(poly, n, off, s % 2 === 0 ? 1.0 : -1.0);
        }
        // Cup-and-ball / chisel cross-joint top cap
        const capN = vnorm([rng.signed() * 0.16, 1.0, rng.signed() * 0.16]);
        poly = clipPolyByHalfSpace(poly, capN, capN[0] * cx + capN[1] * colH + capN[2] * cz, 1.0);

        if (!poly) continue;

        // Outer front columns lean/cant slightly from frost/cooling topple!
        if (gz >= 1 && Math.abs(gx) >= 0.4)
        {
            const outDir = vnorm([cx, 0, cz]);
            const axis = [-outDir[2], 0, outDir[0]];
            poly = transformPoly(poly, [outDir[0] * gap * 0.8, 0, outDir[2] * gap * 0.8], axis, -tiltRad * (gz === 2 ? 1.15 : 0.55), [cx, 0, cz]);
        }

        // Split taller columns across horizontal entablature cross-joint!
        if (colH > S * 0.52)
        {
            const midY = colH * rng.range(0.46, 0.58);
            const cjN = vnorm([rng.signed() * 0.10, 1.0, rng.signed() * 0.10]);
            const [lowDrum, highDrum] = splitPolyByPlane(poly, cjN, cjN[0] * cx + cjN[1] * midY + cjN[2] * cz, gap * 0.55);
            if (lowDrum && highDrum)
            {
                const shiftedTop = transformPoly(highDrum, [rng.signed() * S * 0.008 * faultSlip, 0, S * 0.010 * faultSlip], [0, 1, 0], 0.04 * faultSlip);
                const b0 = compileFractureBlock(lowDrum, { edgeBevel: bevel, strataWeight: 0.12, seedSalt: crackSalt + i * 19 });
                const b1 = compileFractureBlock(shiftedTop, { edgeBevel: bevel, strataWeight: 0.12, seedSalt: crackSalt + i * 19 + 5 });
                if (b0) blocks.push(b0);
                if (b1) blocks.push(b1);
                continue;
            }
        }

        const blk = compileFractureBlock(poly, { edgeBevel: bevel, strataWeight: 0.12, seedSalt: crackSalt + i * 19 });
        if (blk) blocks.push(blk);
    }

    const toWorld = (x, y, z) => [x * S, y * S, z * S];
    addPedestalSpalls(blocks, toWorld, null, rng, S, bevel, params.spallCalving ?? 0.75, 0.10, crackSalt);
    return blocks;
}

function buildFrostWedgedAreteBlocks(params, rng, crackSalt)
{
    const S = params.scale;
    const blocks = [];
    const aperture = params.chasmAperture ?? 0.66;
    const tiltRad = (params.hingeTiltDeg ?? 6.4) * DEG2RAD;
    const faultSlip = params.faultHeave ?? 0.62;
    const bevel = S * (0.0018 + 0.0068 * (params.edgeChamfer ?? 0.26));
    const minThick = S * 0.06;

    const bf = buildStrikeDipFrame(0.38, 1.05); // Steeply dipping alpine gneiss foliation (~60 deg)!
    const toWorld = (x, y, z) => [x * S, y * S, z * S];

    const spire = sculptGeologicalParentPoly(
        [0, S * 0.48, 0],
        [S * 0.38, S * 0.54, S * 0.28],
        bf,
        rng,
        { jointDispersion: 0.16, cornerBlunt: 0.68, flatBaseY: 0, extraBevelCuts: 5 }
    );

    // Mohr-Coulomb 60° / 120° conjugate shear joint pair!
    const conjA = vnorm([Math.sin(1.06), Math.cos(1.06), 0.14]);
    const conjB = vnorm([-Math.sin(1.04), Math.cos(1.04), -0.12]);
    const gap = (0.010 + 0.030 * aperture) * S;

    const [footwall, hanging] = splitPolyByPlane(spire, conjA, S * 0.18, gap);
    const candidates = [];
    if (footwall) candidates.push({ poly: footwall, tag: -1 });
    if (hanging)
    {
        const heaved = transformPoly(
            hanging,
            [S * 0.032 * faultSlip, -S * 0.042 * faultSlip, S * 0.012 * faultSlip],
            [0, 0, 1],
            -tiltRad * 0.95,
            [S * 0.14, S * 0.06, 0]
        );
        candidates.push({ poly: heaved, tag: 1 });
    }

    for (let i = 0; i < candidates.length; i++)
    {
        const { poly, tag } = candidates[i];
        if (!isSubstantialBlock(poly, minThick)) continue;
        const [sub1, sub2] = splitPolyByPlane(poly, conjB, S * (0.16 + 0.06 * i), gap * 0.78);
        if (sub1 && sub2 && isSubstantialBlock(sub1, minThick) && isSubstantialBlock(sub2, minThick))
        {
            const wedged2 = transformPoly(
                sub2,
                [-S * 0.022 * aperture, S * 0.010 * aperture, 0],
                [0, 0, 1],
                tiltRad * 0.65 * (tag < 0 ? 1 : -0.6),
                [-S * 0.12, S * 0.15, 0]
            );
            const b1 = compileFractureBlock(sub1, { edgeBevel: bevel, strataWeight: 0.42, seedSalt: crackSalt + i * 29 });
            const b2 = compileFractureBlock(wedged2, { edgeBevel: bevel, strataWeight: 0.42, seedSalt: crackSalt + i * 29 + 7 });
            if (b1) blocks.push(b1);
            if (b2) blocks.push(b2);
        }
        else
        {
            const b = compileFractureBlock(poly, { edgeBevel: bevel, strataWeight: 0.42, seedSalt: crackSalt + i * 29 });
            if (b) blocks.push(b);
        }
    }

    addPedestalSpalls(blocks, toWorld, bf, rng, S, bevel, params.spallCalving ?? 0.80, 0.35, crackSalt);
    return blocks;
}

/*==========================================================================================================================================
                               4. UNIFIED PURE-SDF FORMATION COMPILER & EVALUATOR
==========================================================================================================================================*/

export function buildFracturedFormation(rawParams = {})
{
    const preset = getFracturePreset(rawParams.presetId || 'cracked-monolith');
    const params = { ...preset.defaults, lithology: preset.lithology, ...rawParams };
    const rng = createRng(params.seed ?? 1337);
    const crackSalt = rng.int(100, 99999);

    let blocks;
    switch (params.presetId)
    {
        case 'al-naslaa-split':   blocks = buildAlNaslaaSplitBlocks(params, rng, crackSalt); break;
        case 'thermal-90-fin':    blocks = buildThermal90FinBlocks(params, rng, crackSalt); break;
        case 'exfoliation-dome':  blocks = buildExfoliationDomeBlocks(params, rng, crackSalt); break;
        case 'columnar-basalt':   blocks = buildColumnarBasaltBlocks(params, rng, crackSalt); break;
        case 'frost-wedged-arete':blocks = buildFrostWedgedAreteBlocks(params, rng, crackSalt); break;
        case 'cracked-monolith':
        default:                  blocks = buildCrackedMonolithBlocks(params, rng, crackSalt); break;
    }

    // Compute exact bounding box across all compiled fracture blocks
    let minX = Infinity, minY = 0, minZ = Infinity;
    let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
    let totalPlanes = 0;
    for (let i = 0; i < blocks.length; i++)
    {
        const b = blocks[i];
        if (b.minX < minX) minX = b.minX;
        if (b.maxX > maxX) maxX = b.maxX;
        if (b.minY < minY) minY = b.minY;
        if (b.maxY > maxY) maxY = b.maxY;
        if (b.minZ < minZ) minZ = b.minZ;
        if (b.maxZ > maxZ) maxZ = b.maxZ;
        totalPlanes += b.numPlanes;
    }
    if (!Number.isFinite(minX))
    {
        minX = -2; maxX = 2; minY = 0; maxY = 4; minZ = -2; maxZ = 2;
    }

    const margin = params.scale * 0.06;
    const bounds = {
        minX: minX - margin,
        minY: 0.0,
        minZ: minZ - margin,
        maxX: maxX + margin,
        maxY: maxY + margin,
        maxZ: maxZ + margin,
        flatBaseY: 0.0
    };

    const sunAz = (params.sunAzimuthDeg ?? 225) * DEG2RAD;
    const sunEl = (params.sunElevationDeg ?? 38) * DEG2RAD;
    const sunDir = [
        Math.cos(sunEl) * Math.sin(sunAz),
        Math.sin(sunEl),
        Math.cos(sunEl) * Math.cos(sunAz)
    ];

    return {
        params,
        blocks,
        totalPlanes,
        crackSalt,
        sunDir,
        bounds,
        theme: LITHOLOGY_THEMES[params.lithology] || LITHOLOGY_THEMES.granite
    };
}

/**
 * Evaluates a single compiled fracture block's macro SDF (convex half-spaces + 45° chamfer + parent-space
 * interlocking jigsaw wall relief & sedimentary strata ledges!).
 * Also writes `outMeta.freshMask` (0 = exterior weathered rind, 1 = fresh internal fracture face) and `outMeta.p0`.
 */
export function evalSingleFractureBlock(block, wx, wy, wz, formation, includeWallRelief = true, outMeta = null)
{
    const { params, crackSalt } = formation;
    const S = params.scale;

    // Exact inverse rigid pullback p0 = invR * p + invT into unbroken parent massif!
    const invR = block.invR;
    const invT = block.invT;
    const p0x = invR[0] * wx + invR[1] * wy + invR[2] * wz + invT[0];
    const p0y = invR[3] * wx + invR[4] * wy + invR[5] * wz + invT[1];
    const p0z = invR[6] * wx + invR[7] * wy + invR[8] * wz + invT[2];

    // Pre-evaluate parent-space jigsaw undulation + plumose hackle + sedimentary strata ONLY when near block!
    let jigsawDisp = 0.0;
    let extRelief = 0.0;
    if (includeWallRelief)
    {
        const jigRough = params.jigsawRoughness ?? 0.55;
        const plumAmp = S * 0.0080 * (params.plumoseStrength ?? 0.60);
        if (jigRough > 1e-4 || plumAmp > 1e-5)
        {
            // Multi-scale crooked fault trajectory: bold S-bend stepovers + interlocking jigsaw teeth + angular grain kinks!
            const f0 = 0.58 / S;
            const f1 = 1.65 / S;
            const f2 = 4.40 / S;
            const macroBend = scalarField3D(p0x * f0, p0y * f0 * 0.75, p0z * f0, crackSalt + 2);
            const w1 = scalarField3D(p0x * f1, p0y * f1 * 0.9, p0z * f1, crackSalt + 5) * 0.67 +
                       scalarField3D(p0x * f1 * 2.05, p0y * f1 * 1.85, p0z * f1 * 2.05, crackSalt + 106) * 0.33;
            const jagTooth = Math.abs(scalarField3D(p0x * f2, p0y * f2, p0z * f2, crackSalt + 47)) * 2.0 - 1.0;

            const rNucleation = Math.hypot(p0x, p0y - S * 0.08, p0z);
            const plumWave = Math.sin(rNucleation * (22.0 / S) + w1 * 4.2) * 0.45 +
                             scalarField3D(p0x * (8.5 / S), p0y * (4.8 / S), p0z * (8.5 / S), crackSalt + 71) * 0.55;
            jigsawDisp = S * jigRough * (0.022 * macroBend + 0.017 * w1 + 0.007 * jagTooth) + plumAmp * plumWave;
        }

        // Three-scale organic weathered rock-mass sculpting (turns flat polyhedron facets into natural craggy stone!)
        const lobeF = 0.85 / S;
        const cragF = 2.15 / S;
        const detailF = 5.10 / S;
        const macroLobe = S * 0.024 * scalarField3D(p0x * lobeF, p0y * lobeF * 0.85, p0z * lobeF, crackSalt + 109);
        const angularCrag = S * 0.015 * (Math.abs(scalarField3D(p0x * cragF, p0y * cragF, p0z * cragF, crackSalt + 113)) * 2.0 - 1.0);
        const fineGrain = S * 0.0050 * scalarField3D(p0x * detailF, p0y * detailF, p0z * detailF, crackSalt + 127);

        const strataAmp = S * 0.0060 * block.strataWeight;
        const bedY = p0y * (5.4 / S) + 0.52 * scalarField3D(p0x * (1.6 / S), p0y * (2.4 / S), p0z * (1.6 / S), crackSalt + 149);
        const strataLedge = strataAmp * (Math.sin(bedY * Math.PI * 2.0) * 0.72 + 0.28 * Math.sin(bedY * Math.PI * 4.0 + 0.8));
        extRelief = macroLobe + angularCrag + fineGrain + strataLedge;
    }

    // Precompute 3D exterior boulder warp vector ONCE per block (zero noise calls inside plane loop -> 4x faster!)
    let extWarpX = 0.0, extWarpY = 0.0, extWarpZ = 0.0;
    if (includeWallRelief)
    {
        const wf = 1.25 / S;
        const wa = S * 0.024;
        extWarpX = wa * scalarField3D(p0x * wf, p0y * wf * 0.9, p0z * wf, crackSalt + 131);
        extWarpY = wa * 0.75 * scalarField3D(p0x * wf + 5.7, p0y * wf * 0.9 - 3.1, p0z * wf, crackSalt + 137);
        extWarpZ = wa * scalarField3D(p0x * wf - 4.3, p0y * wf * 0.9 + 6.2, p0z * wf, crackSalt + 139);
    }

    const planes = block.planes;
    const types = block.planeTypes;
    const r = block.edgeBevel;
    const kExt = r * (includeWallRelief
        ? (1.75 + 1.45 * Math.abs(scalarField3D(p0x * (1.6 / S), p0y * (1.6 / S), p0z * (1.6 / S), crackSalt + 163)))
        : 2.0);

    let dFloor = -1e9;
    let dExt = -1e9;
    let dFrac = -1e9;
    let bestTopPd = -1e9;
    let sumNx = 0.0, sumNy = 0.0, sumNz = 0.0;
    const invNormSoft = 1.0 / Math.max(0.035, kExt * 0.55);

    for (let i = 0, p = 0; i < block.numPlanes; i++, p += 4)
    {
        const pnx = planes[p], pny = planes[p + 1], pnz = planes[p + 2];
        let pd = pnx * wx + pny * wy + pnz * wz - planes[p + 3];
        const pType = types[i]; // 0 = exterior rind, +1 / -1 = interlocking opposite fracture walls!

        if (includeWallRelief)
        {
            if (Math.abs(pType) > 0.5)
            {
                // pType = +1 vs -1 guarantees convex tooth on wall A matches concave socket on wall B!
                pd += pType * jigsawDisp;
            }
            else if (pny > -0.85)
            {
                // 3D domain-warp exterior boulder hull so ridges & faces curve organically!
                pd += extRelief + (pnx * extWarpX + pny * extWarpY + pnz * extWarpZ);
            }
        }

        if (Math.abs(pType) > 0.5)
        {
            dFrac = dFrac < -1e8 ? pd : chamferMax(dFrac, pd, r * 0.65);
        }
        else if (pny <= -0.85)
        {
            if (pd > dFloor) dFloor = pd;
        }
        else
        {
            // Smooth C1 intersection across ALL exterior boulder faces -> zero 3-plane vertex slivers!
            dExt = dExt < -1e8 ? pd : smax(dExt, pd, kExt);
        }

        if (pd > bestTopPd)
        {
            const rescale = bestTopPd > -1e8 ? Math.exp(Math.max(-20.0, (bestTopPd - pd) * invNormSoft)) : 0.0;
            sumNx = sumNx * rescale + pnx;
            sumNy = sumNy * rescale + pny;
            sumNz = sumNz * rescale + pnz;
            bestTopPd = pd;
        }
        else
        {
            const w = Math.exp(Math.max(-20.0, (pd - bestTopPd) * invNormSoft));
            sumNx += pnx * w;
            sumNy += pny * w;
            sumNz += pnz * w;
        }
    }

    const nlOut = Math.hypot(sumNx, sumNy, sumNz) || 1.0;
    const outNx = sumNx / nlOut, outNy = sumNy / nlOut, outNz = sumNz / nlOut;

    let dMaxExt = dExt;
    if (dFloor > -1e8) dMaxExt = dMaxExt < -1e8 ? dFloor : Math.max(dMaxExt, dFloor);

    let dBlock = dMaxExt;
    if (dFrac > -1e8)
    {
        dBlock = dBlock < -1e8 ? dFrac : chamferMax(dBlock, dFrac, r);
    }

    if (outMeta)
    {
        outMeta.p0x = p0x;
        outMeta.p0y = p0y;
        outMeta.p0z = p0z;
        outMeta.nx = outNx;
        outMeta.ny = outNy;
        outMeta.nz = outNz;
        // Analytical fresh-fracture mask: 1.0 on internal cleaved faces, 0.0 on aged weathered rind!
        const transition = Math.max(0.015, r * 1.35);
        outMeta.freshMask = smoothstep(-transition, transition, dFrac - dMaxExt);
        outMeta.noThermalCracks = block.noThermalCracks;
        outMeta.strataWeight = block.strataWeight;
    }

    return dBlock;
}

const _tmpMeta = { p0x: 0, p0y: 0, p0z: 0, nx: 0, ny: 1, nz: 0, freshMask: 0, noThermalCracks: false, strataWeight: 0 };
const _bestMeta = { p0x: 0, p0y: 0, p0z: 0, nx: 0, ny: 1, nz: 0, freshMask: 0, noThermalCracks: false, strataWeight: 0 };

/**
 * Evaluates the FULL Pure-SDF Fractured Formation at world point `(wx, wy, wz)`:
 * 1. Union of kinematically cleaved polyhedral jigsaw blocks (with parent-space interlocking wall warps & 45° chamfers)
 * 2. Thermal skin-depth V-tipped surface cracks (`a(p) = a_surf * exp(phi_0 / delta_T)`) gated by solar ΔT insolation:
 *    - Hierarchical 90° T-Junction Thermal Crazing (`evalHierarchical90Crazing3D`)
 *    - 3D Voronoi Polygonal Stress Cleavage (`evalPlanarCleavage3D`)
 * 3. Concentric Onion-Skin Exfoliation Sheeting (`abs((-phi_0) mod s - s/2)`) with spalled outer shell windows
 * 4. Optional Cross-Section Cutaway Slice (`max(phi, p_axis - sliceOffset)`)
 */
export function evalFracturedFormationSdf(formation, wx, wy, wz, candidateBlocks = null, outInfo = null)
{
    const { params, blocks, crackSalt, sunDir } = formation;
    const list = candidateBlocks || blocks;

    let dMacro = 1e9;
    _bestMeta.freshMask = 0;
    _bestMeta.p0x = wx; _bestMeta.p0y = wy; _bestMeta.p0z = wz;
    _bestMeta.noThermalCracks = false;
    _bestMeta.strataWeight = 0.25;

    for (let i = 0; i < list.length; i++)
    {
        const b = list[i];
        const dx = wx - b.cx, dy = wy - b.cy, dz = wz - b.cz;
        const sphereLowerBound = Math.hypot(dx, dy, dz) - b.radius - params.scale * 0.04;
        if (sphereLowerBound > dMacro) continue;

        const dB = evalSingleFractureBlock(b, wx, wy, wz, formation, true, _tmpMeta);
        if (dB < dMacro)
        {
            dMacro = dB;
            _bestMeta.p0x = _tmpMeta.p0x;
            _bestMeta.p0y = _tmpMeta.p0y;
            _bestMeta.p0z = _tmpMeta.p0z;
            _bestMeta.nx = _tmpMeta.nx;
            _bestMeta.ny = _tmpMeta.ny;
            _bestMeta.nz = _tmpMeta.nz;
            _bestMeta.freshMask = _tmpMeta.freshMask;
            _bestMeta.noThermalCracks = _tmpMeta.noThermalCracks;
            _bestMeta.strataWeight = _tmpMeta.strataWeight;
        }
    }

    let d = dMacro;
    const S = params.scale;
    const skinDepth = Math.max(0.06, params.thermalSkinDepth ?? 0.44);
    const thermalNorm = clamp((params.thermalDeltaT ?? 38) / 65.0, 0.0, 1.5);
    const crazing90 = params.crazing90Strength ?? 0.30;
    const vorStrength = params.voronoiJointStrength ?? 0.36;
    const exfol = params.exfoliationStrength ?? 0.14;

    let crackFreshBoost = 0.0;
    let thermalStressAtPt = thermalNorm;

    // Evaluate fine thermal V-tipped cracks & exfoliation ONLY inside the active surface/skin-depth band!
    const maxFineDepth = Math.max(skinDepth * 2.2, exfol > 0.02 ? S * 0.14 : 0.12);
    if (!_bestMeta.noThermalCracks && dMacro < S * 0.04 && dMacro > -maxFineDepth &&
        (crazing90 > 0.01 || vorStrength > 0.01 || exfol > 0.01))
    {
        // Exact analytical macro block normal (zero finite-difference overhead!)
        const nx = _bestMeta.nx, ny = _bestMeta.ny, nz = _bestMeta.nz;

        // Directional Solar Insolation ΔT Stress Factor!
        const ndots = Math.max(0.0, nx * sunDir[0] + ny * sunDir[1] + nz * sunDir[2]);
        const sunGate = clamp(Math.pow(ndots, 1.25) + 0.10, 0.0, 1.0);
        const insolFactor = mix(1.0, sunGate, params.directionalBias ?? 0.55) * thermalNorm;
        thermalStressAtPt = insolFactor;

        const p0x = _bestMeta.p0x, p0y = _bestMeta.p0y, p0z = _bestMeta.p0z;

        // (A) Pure-SDF Concentric Onion-Skin Exfoliation Sheeting (100% C1 terraced spall scars + cross-section unloading seams!)
        if (exfol > 0.04)
        {
            const sheetSpacing = S * 0.036;
            const plateThick = S * 0.015; // Realistic ~11 cm granite exfoliation plate thickness!
            const upGate = smoothstep(-0.20, 0.32, ny);

            if (exfol > 0.22 && upGate > 0.005)
            {
                const sf = 2.45 / S;
                const spallN1 = scalarField3D(p0x * sf, p0y * sf * 0.8, p0z * sf, crackSalt + 499) +
                                0.22 * (Math.abs(scalarField3D(p0x * sf * 2.7, p0y * sf * 2.7, p0z * sf * 2.7, crackSalt + 503)) * 2.0 - 1.0);
                const thresh1 = 0.56 - 0.52 * exfol;
                // Steep C1 45-degree beveled terrace step (`thresh1 .. thresh1 + 0.14`) -> zero discontinuous cell jumps!
                const scar1 = smoothstep(thresh1, thresh1 + 0.14, spallN1) * upGate;

                // Optional inner 2nd-tier overlapping onion-skin terrace inside large scars!
                const spallN2 = scalarField3D(p0x * sf * 1.45 + 5.3, p0y * sf * 1.2, p0z * sf * 1.45 - 3.7, crackSalt + 509);
                const scar2 = scar1 * smoothstep(thresh1 + 0.24, thresh1 + 0.38, spallN2);

                const peelOffset = plateThick * (scar1 * 0.95 + scar2 * 0.75);
                if (peelOffset > 1e-4)
                {
                    d += peelOffset;
                    dMacro += peelOffset;
                    crackFreshBoost = Math.max(crackFreshBoost, clamp(scar1 * 0.42 + scar2 * 0.28, 0.0, 0.68));
                }
            }

            // Concentric subsurface unloading seams (carved when cross-section cutaway is active!)
            const interiorDepth = -dMacro;
            if (params.cutawayEnabled && upGate > 0.05 && interiorDepth > sheetSpacing * 0.45 && interiorDepth < sheetSpacing * 3.1)
            {
                const warpS = sheetSpacing * 0.14 * scalarField3D(p0x * 0.9, p0y * 0.9, p0z * 0.9, crackSalt + 409);
                const sCoord = (interiorDepth + warpS) / sheetSpacing;
                const fracS = sCoord - Math.floor(sCoord);
                const seamDist = Math.min(fracS, 1.0 - fracS) * sheetSpacing;
                const seamHalfAp = sheetSpacing * 0.09 * exfol * upGate;
                if (seamHalfAp > 0.0015)
                {
                    d = Math.max(d, -(seamDist - seamHalfAp));
                }
            }
        }

        // Irwin Finite-Depth V-Tipped Crack Wedge Profile:
        // Cracks penetrate to exact depth `tipDepth = skinDepth * (0.30 + 0.70 * insolFactor)` and pinch linearly to a sharp V-tip!
        const tipDepth = skinDepth * clamp(0.30 + 0.70 * insolFactor, 0.15, 1.35);
        const vTaper = Math.max(0.0, 1.0 + Math.min(0.0, dMacro) / tipDepth);

        // (B) Organic Curvilinear & Pinch-and-Swell 90° T-Junction Thermal Crazing!
        if (crazing90 > 0.01 && insolFactor > 0.02 && dMacro > -(tipDepth + S * 0.08))
        {
            const ax = Math.abs(nx), ay = Math.abs(ny), az = Math.abs(nz);
            let u, v, w;
            if (ay >= ax && ay >= az)      { u = p0x; v = p0z; w = p0y; }
            else if (ax >= az)             { u = p0z; v = p0y; w = p0x; }
            else                           { u = p0x; v = p0y; w = p0z; }

            const spacing = Math.max(0.18, params.crazingSpacing ?? 0.62);
            const c90 = evalHierarchical90Crazing3D(u, v, w, spacing, crackSalt + 211);
            const surfHalfAp90 = spacing * 0.068 * crazing90 * insolFactor;
            if (surfHalfAp90 > 0.0015)
            {
                const ap1 = surfHalfAp90 * c90.mod1 * vTaper;
                const ap2 = surfHalfAp90 * c90.mod2 * vTaper;
                const cut1 = c90.tier1Dist - ap1;
                const cut2 = c90.tier2Dist * 1.12 - ap2;
                const cutOrganic90 = Math.min(cut1, cut2);
                const effAp = Math.max(ap1, ap2);
                const cut90 = Math.max(cutOrganic90, (-dMacro) - tipDepth);
                d = chamferSub(d, cut90, effAp * 0.35);
            }
        }

        // (C) Organic Domain-Warped 3D Voronoi Fissures + Pinch-and-Swell Rock Bridges + Dendritic Splinters!
        if (vorStrength > 0.01 && dMacro > -(tipDepth + S * 0.08))
        {
            const vorScale = (1.35 / S) * 4.2;
            let vx = p0x * vorScale;
            let vy = p0y * vorScale * 0.86;
            let vz = p0z * vorScale;

            // Multi-scale 3D organic domain warp bends straight polygon walls into sinuous natural rock fissures!
            const wx0 = 0.38 * scalarField3D(vx * 0.55, vy * 0.55, vz * 0.55, crackSalt + 311) +
                        0.12 * (Math.abs(scalarField3D(vx * 2.15, vy * 2.15, vz * 2.15, crackSalt + 317)) * 2.0 - 1.0);
            const wy0 = 0.36 * scalarField3D(vx * 0.55 + 5.2, vy * 0.55 - 2.8, vz * 0.55, crackSalt + 331) +
                        0.12 * (Math.abs(scalarField3D(vx * 2.15 - 3.4, vy * 2.15, vz * 2.15, crackSalt + 337)) * 2.0 - 1.0);
            const wz0 = 0.38 * scalarField3D(vx * 0.55 - 4.1, vy * 0.55 + 6.3, vz * 0.55, crackSalt + 347) +
                        0.12 * (Math.abs(scalarField3D(vx * 2.15, vy * 2.15 + 4.1, vz * 2.15, crackSalt + 353)) * 2.0 - 1.0);

            const clv = evalPlanarCleavage3D(vx + wx0, vy + wy0, vz + wz0, 0.78, crackSalt + 307);
            const worldWallDist = clv.wallDist / vorScale;

            // Power-tapered C1 pinch-and-swell vitality: wide master fissures tapering smoothly into delicate hairline tips!
            const vorVit = scalarField3D(vx * 0.48 - 7.1, vy * 0.48 + 3.9, vz * 0.48, crackSalt + 367);
            const openVor = smoothstep(-0.12, 0.46, vorVit);
            const vorBridge = openVor * Math.sqrt(openVor) *
                              (0.82 + 0.46 * Math.abs(scalarField3D(vx * 2.7, vy * 2.7, vz * 2.7, crackSalt + 379)));

            const surfHalfApVor = (0.010 + 0.030 * vorStrength) * (0.45 + 0.55 * insolFactor) * (S / 7.0) * vorBridge;
            if (surfHalfApVor > 0.0012)
            {
                const halfApVor = surfHalfApVor * vTaper;
                const cutVor = Math.max(worldWallDist - halfApVor, (-dMacro) - tipDepth);
                d = chamferSub(d, cutVor, halfApVor * 0.36);
            }

            // Secondary dendritic hairline splinters branching ONLY near primary open fissures!
            const nearPrimary = smoothstep(0.42, 0.02, clv.wallDist) * smoothstep(0.12, 0.60, vorBridge);
            if (nearPrimary > 0.04 && vTaper > 0.0)
            {
                const subScale = 2.45;
                const subClv = evalPlanarCleavage3D((vx + wx0 * 1.3) * subScale, (vy + wy0 * 1.3) * subScale, (vz + wz0 * 1.3) * subScale, 0.78, crackSalt + 389);
                const subWallWorld = subClv.wallDist / (vorScale * subScale);
                const subHalfAp = (0.0045 + 0.012 * vorStrength) * nearPrimary * vTaper * (S / 7.0);
                if (subHalfAp > 0.0010)
                {
                    const cutSub = Math.max(subWallWorld - subHalfAp, (-dMacro) - tipDepth * 0.55);
                    d = chamferSub(d, cutSub, subHalfAp * 0.32);
                }
            }
        }
    }

    // 4. Optional Scientific Cross-Section Cutaway Slice (`smax(phi, p_axis - sliceOffset)`)
    let isCutaway = false;
    if (params.cutawayEnabled)
    {
        const axisCoord = (params.cutawayAxis ?? 0) === 0 ? wx : wz;
        const slicePlane = axisCoord - (params.cutawayOffset ?? 0.25);
        if (slicePlane > d)
        {
            d = slicePlane;
            isCutaway = true;
        }
    }

    if (outInfo)
    {
        outInfo.dMacro = dMacro;
        outInfo.freshMask = clamp(Math.max(_bestMeta.freshMask, crackFreshBoost), 0.0, 1.0);
        outInfo.thermalStress = clamp(thermalStressAtPt, 0.0, 1.0);
        outInfo.isCutaway = isCutaway;
        outInfo.p0x = _bestMeta.p0x;
        outInfo.p0y = _bestMeta.p0y;
        outInfo.p0z = _bestMeta.p0z;
        outInfo.strataWeight = _bestMeta.strataWeight;
    }

    return d;
}

function evalMacroOnly(formation, wx, wy, wz, list)
{
    let dMacro = 1e9;
    const { params } = formation;
    for (let i = 0; i < list.length; i++)
    {
        const b = list[i];
        const dx = wx - b.cx, dy = wy - b.cy, dz = wz - b.cz;
        if (Math.hypot(dx, dy, dz) - b.radius - params.scale * 0.04 > dMacro) continue;
        const dB = evalSingleFractureBlock(b, wx, wy, wz, formation, false, null);
        if (dB < dMacro) dMacro = dB;
    }
    if (params.cutawayEnabled)
    {
        const axisCoord = (params.cutawayAxis ?? 0) === 0 ? wx : wz;
        const slicePlane = axisCoord - (params.cutawayOffset ?? 0.25);
        if (slicePlane > dMacro) dMacro = slicePlane;
    }
    return dMacro;
}

/*==========================================================================================================================================
                     5. PURE-SDF WEATHERED RIND vs FRESH FRACTURE INTERIOR MATERIAL & CUTAWAY SHADING
==========================================================================================================================================*/

const _matInfo = { dMacro: 0, freshMask: 0, thermalStress: 0, isCutaway: false, p0x: 0, p0y: 0, p0z: 0, strataWeight: 0 };

export function evalFracturedFormationMaterial(formation, px, py, pz, nx, ny, nz, ao, cav, edg, outMat)
{
    evalFracturedFormationSdf(formation, px, py, pz, null, _matInfo);
    const { theme, params, crackSalt } = formation;
    const S = params.scale;
    const p0x = _matInfo.p0x, p0y = _matInfo.p0y, p0z = _matInfo.p0z;

    // Cutaway cross-section face shows internal SDF phi_0 contour rings & thermal skin-depth boundary!
    if (_matInfo.isCutaway || (params.cutawayEnabled && (((params.cutawayAxis ?? 0) === 0 ? Math.abs(nx) : Math.abs(nz)) > 0.92 &&
        Math.abs(((params.cutawayAxis ?? 0) === 0 ? px : pz) - (params.cutawayOffset ?? 0.25)) < 0.09)))
    {
        const depth = Math.max(0.0, -_matInfo.dMacro);
        const skinD = Math.max(0.06, params.thermalSkinDepth ?? 0.44);
        const skinZone = Math.exp(-depth / skinD);
        const ring = 0.5 + 0.5 * Math.cos((depth / 0.18) * Math.PI * 2.0);
        const isoLine = smoothstep(0.92, 0.99, ring);

        let r = mix(theme.fresh[0] * 0.82, theme.iron[0] * 1.12, skinZone * 0.55);
        let g = mix(theme.fresh[1] * 0.82, theme.iron[1] * 0.95, skinZone * 0.55);
        let b = mix(theme.fresh[2] * 0.82, theme.iron[2] * 0.85, skinZone * 0.55);

        // Crisp amber SDF distance isolines inside the cut rock volume!
        r = mix(r, 0.95, isoLine * 0.42);
        g = mix(g, 0.86, isoLine * 0.42);
        b = mix(b, 0.62, isoLine * 0.42);

        outMat.r = clamp(r, 0.04, 1.0);
        outMat.g = clamp(g, 0.04, 1.0);
        outMat.b = clamp(b, 0.04, 1.0);
        outMat.fresh = 0.95;
        return;
    }

    // Grain & parent-space sedimentary bedding color variation (tracks continuously across fault splits!)
    const grain = scalarField3D(p0x * 6.5, p0y * 6.5, p0z * 6.5, crackSalt + 601);
    const bedBand = Math.sin((p0y * (5.4 / S) + 0.44 * scalarField3D(p0x * 0.6, p0y * 0.6, p0z * 0.6, crackSalt + 619)) * Math.PI * 2.0);
    const strataMod = 1.0 + bedBand * 0.14 * _matInfo.strataWeight + grain * 0.07;

    const fresh = _matInfo.freshMask;
    let r = mix(theme.rind[0], theme.fresh[0], fresh) * strataMod;
    let g = mix(theme.rind[1], theme.fresh[1], fresh) * strataMod;
    let b = mix(theme.rind[2], theme.fresh[2], fresh) * strataMod;

    // Iron-oxide / limonite mineral staining along fracture cavities & water-seep streaks
    const ironSeep = clamp(cav * 0.72 + fresh * 0.28 * (0.5 + 0.5 * scalarField3D(p0x * 2.1, p0y * 0.8, p0z * 2.1, crackSalt + 643)), 0.0, 1.0);
    r = mix(r, theme.iron[0], ironSeep * 0.34);
    g = mix(g, theme.iron[1], ironSeep * 0.34);
    b = mix(b, theme.iron[2], ironSeep * 0.34);

    // Subtle upward-facing alpine lichen / weathered cap tint on unfractured exterior tops
    const upLichen = (1.0 - fresh) * smoothstep(0.55, 0.92, ny) * clamp(0.5 + 0.5 * grain, 0.0, 1.0);
    r = mix(r, theme.lichen[0], upLichen * 0.30);
    g = mix(g, theme.lichen[1], upLichen * 0.30);
    b = mix(b, theme.lichen[2], upLichen * 0.30);

    // Brighten chipped convex rock arrises (`edg`), darken deep fracture recesses (`cav`)
    const bevelHighlight = 1.0 + edg * 0.18 - cav * 0.20;
    outMat.r = clamp(r * bevelHighlight, 0.03, 1.0);
    outMat.g = clamp(g * bevelHighlight, 0.03, 1.0);
    outMat.b = clamp(b * bevelHighlight, 0.03, 1.0);
    outMat.fresh = fresh;
}

/*==========================================================================================================================================
                     6. SPARSE-BRICK QEF DUAL-CONTOUR MESH BAKER & GPU TEXTURE BUFFER PACKER
==========================================================================================================================================*/

export function bakeFracturedFormationQefMesh(formation, options = {})
{
    const t0 = typeof performance !== 'undefined' ? performance.now() : Date.now();
    const { bounds, blocks, params } = formation;

    const spanMax = Math.max(bounds.maxX - bounds.minX, bounds.maxY - bounds.minY, bounds.maxZ - bounds.minZ);
    const targetRes = clamp(options.resolution ?? params.meshResolution ?? 84, 36, 148);
    const voxelSize = spanMax / targetRes;

    const posDispBound = Math.max(voxelSize * 2.0, params.scale * 0.032);
    const negDispBound = Math.max(
        voxelSize * 2.5,
        (params.thermalSkinDepth ?? 0.44) * 1.15,
        (params.exfoliationStrength ?? 0) > 0.02 ? params.scale * 0.12 : 0.08
    );

    const sdf = evaluateSparseSdf(bounds, voxelSize, {
        positiveDisplacementBound: posDispBound,
        negativeDisplacementBound: negDispBound,
        fineDisplacementBound: Math.max(posDispBound, negDispBound),
        buildBrickContext(cx, cy, cz, cullRadius)
        {
            const nearby = [];
            const margin = cullRadius + params.scale * 0.05;
            for (let i = 0; i < blocks.length; i++)
            {
                const b = blocks[i];
                const d = Math.hypot(cx - b.cx, cy - b.cy, cz - b.cz) - b.radius;
                if (d <= margin) nearby.push(b);
            }
            return nearby.length > 0 ? nearby : null;
        },
        evalMacro(wx, wy, wz, nearby)
        {
            if (!nearby) return 1e6;
            return evalMacroOnly(formation, wx, wy, wz, nearby);
        },
        evalFine(wx, wy, wz, _dMacro, nearby)
        {
            return evalFracturedFormationSdf(formation, wx, wy, wz, nearby, null);
        }
    });

    const rawDc = extractDualContourMesh(sdf, { qefRegularization: 0.045 });
    const cleaned = removeFloatingSpeckles(rawDc.positions, rawDc.indices, rawDc.crease, 12);
    const smoothedPos = taubinSmoothCreasePreserving(cleaned.positions, cleaned.indices, cleaned.crease, options.taubinPasses ?? 1);

    const mesh = buildGameReadyAttributes(smoothedPos, cleaned.indices, sdf, {
        creaseAngleDeg: 44,
        uvScale: 0.35,
        evalMaterial: (px, py, pz, nx, ny, nz, ao, cav, edg, outMat) =>
            evalFracturedFormationMaterial(formation, px, py, pz, nx, ny, nz, ao, cav, edg, outMat)
    });

    const t1 = typeof performance !== 'undefined' ? performance.now() : Date.now();
    return {
        mesh,
        sdfStats: {
            voxelSize: Number(voxelSize.toFixed(4)),
            gridDims: [sdf.nx, sdf.ny, sdf.nz],
            activeBricks: sdf.activeBrickCount,
            totalBricks: sdf.totalBricks,
            brickOccupancyPct: Number(((sdf.activeBrickCount / Math.max(1, sdf.totalBricks)) * 100).toFixed(1)),
            evaluatedSamples: sdf.evaluatedSamples,
            vertices: mesh.positions.length / 3,
            triangles: mesh.indices.length / 3,
            blockCount: blocks.length,
            meshToSdfCount: 0, // Strictly ZERO Mesh->SDF round-trips!
            bakeMs: Math.round(t1 - t0)
        }
    };
}

/**
 * Packs `formation.blocks` into two tiny `Float32Array` RGBA32F 2D texture buffers (`blockTexData`, `planeTexData`)
 * for 60 FPS WebGL2 GPU Pure-SDF Raymarching!
 * - `blockTexData`: width = 6, height = numBlocks (4 floats per texel -> 24 floats per block)
 * - `planeTexData`: width = 2, height = totalPlanes (4 floats per texel -> 8 floats per plane)
 */
export function packFracturedFormationGpuBuffers(formation)
{
    const { blocks } = formation;
    const numBlocks = Math.max(1, blocks.length);
    const totalPlanes = Math.max(1, formation.totalPlanes);

    const blockTexData = new Float32Array(numBlocks * 6 * 4);
    const planeTexData = new Float32Array(totalPlanes * 2 * 4);

    let planeCursor = 0;
    for (let i = 0; i < blocks.length; i++)
    {
        const b = blocks[i];
        const bOff = i * 24;

        // Col 0: (cx, cy, cz, radius)
        blockTexData[bOff + 0] = b.cx;
        blockTexData[bOff + 1] = b.cy;
        blockTexData[bOff + 2] = b.cz;
        blockTexData[bOff + 3] = b.radius;

        // Col 1: (planeStart, numPlanes, edgeBevel, flags)
        blockTexData[bOff + 4] = planeCursor;
        blockTexData[bOff + 5] = b.numPlanes;
        blockTexData[bOff + 6] = b.edgeBevel;
        blockTexData[bOff + 7] = b.noThermalCracks ? 1.0 : 0.0;

        // Col 2: (invR[0..2], invT[0])
        blockTexData[bOff + 8]  = b.invR[0];
        blockTexData[bOff + 9]  = b.invR[1];
        blockTexData[bOff + 10] = b.invR[2];
        blockTexData[bOff + 11] = b.invT[0];

        // Col 3: (invR[3..5], invT[1])
        blockTexData[bOff + 12] = b.invR[3];
        blockTexData[bOff + 13] = b.invR[4];
        blockTexData[bOff + 14] = b.invR[5];
        blockTexData[bOff + 15] = b.invT[1];

        // Col 4: (invR[6..8], invT[2])
        blockTexData[bOff + 16] = b.invR[6];
        blockTexData[bOff + 17] = b.invR[7];
        blockTexData[bOff + 18] = b.invR[8];
        blockTexData[bOff + 19] = b.invT[2];

        // Col 5: (seedSalt, strataWeight, hardness, 0)
        blockTexData[bOff + 20] = b.seedSalt;
        blockTexData[bOff + 21] = b.strataWeight;
        blockTexData[bOff + 22] = b.hardness;
        blockTexData[bOff + 23] = 0.0;

        for (let p = 0; p < b.numPlanes; p++)
        {
            const pOff = (planeCursor + p) * 8;
            planeTexData[pOff + 0] = b.planes[p * 4];
            planeTexData[pOff + 1] = b.planes[p * 4 + 1];
            planeTexData[pOff + 2] = b.planes[p * 4 + 2];
            planeTexData[pOff + 3] = b.planes[p * 4 + 3];

            planeTexData[pOff + 4] = b.planeTypes[p];
            planeTexData[pOff + 5] = 0.0;
            planeTexData[pOff + 6] = 0.0;
            planeTexData[pOff + 7] = 0.0;
        }
        planeCursor += b.numPlanes;
    }

    return {
        numBlocks: blocks.length,
        totalPlanes,
        blockTexWidth: 6,
        blockTexHeight: numBlocks,
        blockTexData,
        planeTexWidth: 2,
        planeTexHeight: totalPlanes,
        planeTexData
    };
}
