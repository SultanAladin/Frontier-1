//============================================================================================================================================
//                                                       OBJECTMESHCATALOG.JS
//        Procedural 3D Triangle Meshes · Custom .obj/.stl/.json Import Registry · Area-Weighted Surface, Volume & Ridge Anchors
//                                   · RGBA32F Texel Packing for gl_VertexID Object Instancing
//============================================================================================================================================
// 🧩 The visual carrier of a particle is not necessarily a sprite: a particle can carry a triangle mesh, and a spawn can be a mesh
//    surface. This module is the single source of truth for both. It contains no WebGL — ParticleSimulation.js uploads the texels and
//    anchors produced here, and ParticleShaders.js reads them with texelFetch.
//
//    Mesh record:  { Name, Corners: Float32Array (tris·9), Normals: Float32Array (tris·9), Triangles, Bounds, Radius, Creases }
//    Texel record: { Data: Float32Array (tris·6·4), Width, Height, TriangleCount, VertexCount }
//      texel 2n   = position.xyz, normal.x     texel 2n+1 = normal.y, normal.z, 0, 0
//    Anchor record: { Data: Float32Array (count·2·4), Width, Height, Count }
//      texel 2n   = position.xyz, localY       texel 2n+1 = normal.xyz, ridge
//============================================================================================================================================

/** Built-in selectable object meshes. Code = index, shared by the Spawn shape and the Sprite & Object carrier. */
export const ObjectNames = Object.freeze([
    'Cube', 'Sphere', 'Coin', 'CrystalShard', 'HexPrism', 'StarPoly', 'UtahTeapot', 'SportsCar', 'ObsidianHand',
    'SculptedBust', 'TorusKnot', 'PrecisionGear', 'CrystalCluster', 'Archway', 'FacetedSpire',
    'WingedButterfly', 'WingedBird', 'FishBody', 'CustomObject',
]);

export const ObjectLabels = Object.freeze({
    Cube: 'Cube', Sphere: 'Sphere', Coin: 'Coin', CrystalShard: 'Crystal Shard', HexPrism: 'Hex Prism', StarPoly: 'Star Polyhedron',
    UtahTeapot: 'Utah Teapot', SportsCar: 'Sports Car', ObsidianHand: 'Obsidian Hand', SculptedBust: 'Sculpted Bust',
    TorusKnot: 'Torus Knot', PrecisionGear: 'Precision Gear', CrystalCluster: 'Crystal Cluster', Archway: 'Archway',
    FacetedSpire: 'Faceted Spire', WingedButterfly: 'Winged Butterfly', WingedBird: 'Winged Bird', FishBody: 'Fish Body',
    CustomObject: 'Custom Object…',
});

/** How particles are distributed over a mesh. Code = index. */
export const ObjectSampleModes = Object.freeze(['EvenSurface', 'SurfaceVolume', 'RidgeAccent']);

/** Every object that is a mesh (CustomObject resolves through the import registry, falling back to the teapot). */
export const BuiltInObjectNames = Object.freeze(ObjectNames.filter((name) => name !== 'CustomObject'));

export const MaximumMeshTriangles = 120000;
export const MaximumAnchorCount = 16384;
const AnchorTextureWidth = 256;

//------------------------------------------------------------------------------------------------------------------------
//                                                    MESH PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const scale = (a, k) => [a[0] * k, a[1] * k, a[2] * k];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const length3 = (a) => Math.hypot(a[0], a[1], a[2]);
function unit(a)
{
    const len = length3(a);
    return len > 1e-9 ? [a[0] / len, a[1] / len, a[2] / len] : [0, 1, 0];
}

/** Accumulates raw triangles; FinalizeMesh() turns them into the packed mesh record. */
class MeshBuilder
{
    constructor(name)
    {
        this.Name = name;
        this.Raw = [];
    }

    triangle(a, b, c)
    {
        const normal = cross(sub(b, a), sub(c, a));
        if (length3(normal) < 1e-12) return;
        this.Raw.push([a, b, c, unit(normal)]);
    }

    quad(a, b, c, d) { this.triangle(a, b, c); this.triangle(a, c, d); }

    box(cx, cy, cz, hx, hy, hz)
    {
        const x0 = cx - hx, x1 = cx + hx, y0 = cy - hy, y1 = cy + hy, z0 = cz - hz, z1 = cz + hz;
        const corner = (x, y, z) => [x, y, z];
        this.quad(corner(x0, y0, z1), corner(x1, y0, z1), corner(x1, y1, z1), corner(x0, y1, z1));
        this.quad(corner(x1, y0, z0), corner(x0, y0, z0), corner(x0, y1, z0), corner(x1, y1, z0));
        this.quad(corner(x1, y0, z1), corner(x1, y0, z0), corner(x1, y1, z0), corner(x1, y1, z1));
        this.quad(corner(x0, y0, z0), corner(x0, y0, z1), corner(x0, y1, z1), corner(x0, y1, z0));
        this.quad(corner(x0, y1, z1), corner(x1, y1, z1), corner(x1, y1, z0), corner(x0, y1, z0));
        this.quad(corner(x0, y0, z0), corner(x1, y0, z0), corner(x1, y0, z1), corner(x0, y0, z1));
    }

    sphere(centre, radius, rings = 8, sides = 12)
    {
        const grid = [];
        for (let r = 0; r <= rings; r += 1)
        {
            const v = r / rings, y = Math.cos(v * Math.PI), ringRadius = Math.sin(v * Math.PI);
            const row = [];
            for (let s = 0; s < sides; s += 1)
            {
                const a = (s / sides) * Math.PI * 2;
                row.push([centre[0] + Math.cos(a) * ringRadius * radius, centre[1] + y * radius, centre[2] + Math.sin(a) * ringRadius * radius]);
            }
            grid.push(row);
        }
        for (let r = 0; r < rings; r += 1)
            for (let s = 0; s < sides; s += 1)
            {
                const sn = (s + 1) % sides;
                this.quad(grid[r][s], grid[r + 1][s], grid[r + 1][sn], grid[r][sn]);
            }
    }

    /** Body of revolution about +Y from a [radius, y] profile. */
    lathe(profile, segments = 24)
    {
        for (let s = 0; s < segments; s += 1)
        {
            const a0 = (s / segments) * Math.PI * 2, a1 = ((s + 1) / segments) * Math.PI * 2;
            for (let p = 0; p + 1 < profile.length; p += 1)
            {
                const [r0, y0] = profile[p], [r1, y1] = profile[p + 1];
                if (r0 < 1e-6 && r1 < 1e-6) continue;
                const p00 = [Math.cos(a0) * r0, y0, Math.sin(a0) * r0];
                const p01 = [Math.cos(a0) * r1, y1, Math.sin(a0) * r1];
                const p10 = [Math.cos(a1) * r0, y0, Math.sin(a1) * r0];
                const p11 = [Math.cos(a1) * r1, y1, Math.sin(a1) * r1];
                if (r0 < 1e-6) this.triangle(p00, p01, p11);
                else if (r1 < 1e-6) this.triangle(p00, p01, p10);
                else this.quad(p00, p01, p11, p10);
            }
        }
    }

    /** Circular tube swept along a parametric curve (spout, handle, torus knot). */
    tube(curve, steps, sides, radius, closed = false)
    {
        const rings = [];
        for (let i = 0; i <= steps; i += 1) rings.push(curve(i / steps));
        for (let i = 0; i < rings.length; i += 1)
        {
            const next = rings[Math.min(i + 1, rings.length - 1)], prev = rings[Math.max(i - 1, 0)];
            const tangent = unit(sub(next, prev));
            const helper = Math.abs(tangent[1]) < 0.95 ? [0, 1, 0] : [1, 0, 0];
            const normal = unit(cross(helper, tangent));
            const binormal = cross(tangent, normal);
            const r = typeof radius === 'function' ? radius(i / steps) : radius;
            const row = [];
            for (let s = 0; s < sides; s += 1)
            {
                const a = (s / sides) * Math.PI * 2;
                row.push(add(rings[i], add(scale(normal, Math.cos(a) * r), scale(binormal, Math.sin(a) * r))));
            }
            rings[i] = row;
        }
        const last = closed ? rings.length - 1 : rings.length - 1;
        for (let i = 0; i < last; i += 1)
        {
            const a = rings[i], b = rings[(i + 1) % rings.length];
            for (let s = 0; s < sides; s += 1)
            {
                const sn = (s + 1) % sides;
                this.quad(a[s], a[sn], b[sn], b[s]);
            }
        }
    }

    /** Prism extruded along ±Y from a closed 2D polygon in XZ. */
    extrude(polygon, halfHeight)
    {
        const top = polygon.map(([x, z]) => [x, halfHeight, z]);
        const bottom = polygon.map(([x, z]) => [x, -halfHeight, z]);
        const centreTop = [0, halfHeight, 0], centreBottom = [0, -halfHeight, 0];
        for (let i = 0; i < polygon.length; i += 1)
        {
            const n = (i + 1) % polygon.length;
            this.quad(bottom[i], bottom[n], top[n], top[i]);
            this.triangle(centreTop, top[i], top[n]);
            this.triangle(centreBottom, bottom[n], bottom[i]);
        }
    }

    /** Triangular prism spanning two points with a rectangular cross-section. */
    strut(a, b, halfThickness)
    {
        this.tube((u) => add(a, scale(sub(b, a), u)), 1, 4, halfThickness);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    BUILT-IN MESHES
//------------------------------------------------------------------------------------------------------------------------

function BuildCube()
{
    const builder = new MeshBuilder('Cube');
    builder.box(0, 0, 0, 0.72, 0.72, 0.72);
    return builder;
}

function BuildSphere()
{
    const builder = new MeshBuilder('Sphere');
    const rings = 18, sides = 26;
    const grid = [];
    for (let r = 0; r <= rings; r += 1)
    {
        const v = r / rings, y = Math.cos(v * Math.PI), radius = Math.sin(v * Math.PI);
        const row = [];
        for (let s = 0; s < sides; s += 1)
        {
            const a = (s / sides) * Math.PI * 2;
            row.push([Math.cos(a) * radius, y, Math.sin(a) * radius]);
        }
        grid.push(row);
    }
    for (let r = 0; r < rings; r += 1)
        for (let s = 0; s < sides; s += 1)
        {
            const sn = (s + 1) % sides;
            builder.quad(grid[r][s], grid[r + 1][s], grid[r + 1][sn], grid[r][sn]);
        }
    return builder;
}

function BuildCoin()
{
    // Minted coin: bevelled rim, recessed face dish and a raised centre boss.
    const builder = new MeshBuilder('Coin');
    const segments = 40, thickness = 0.10;
    const rim = [];
    for (let s = 0; s < segments; s += 1)
    {
        const a = (s / segments) * Math.PI * 2;
        rim.push([Math.cos(a), Math.sin(a)]);
    }
    for (let s = 0; s < segments; s += 1)
    {
        const n = (s + 1) % segments;
        const [x0, z0] = rim[s], [x1, z1] = rim[n];
        builder.quad([x0, -thickness, z0], [x1, -thickness, z1], [x1, thickness, z1], [x0, thickness, z0]);
        // Face dish and rim bevel on both sides.
        for (const side of [1, -1])
        {
            const y = side * thickness;
            builder.quad([x0 * 0.86, y, z0 * 0.86], [x1 * 0.86, y, z1 * 0.86], [x1, y, z1], [x0, y, z0]);
            builder.triangle([0, y, 0], [x0 * 0.86, y, z0 * 0.86], [x1 * 0.86, y, z1 * 0.86]);
        }
    }
    // Raised boss on both faces (emblem).
    const bossSegments = 18;
    for (const side of [1, -1])
    {
        const y = side * (thickness + 0.03);
        for (let s = 0; s < bossSegments; s += 1)
        {
            const a0 = (s / bossSegments) * Math.PI * 2, a1 = ((s + 1) / bossSegments) * Math.PI * 2;
            const r = 0.34;
            builder.quad([0, y, 0], [Math.cos(a0) * r, y, Math.sin(a0) * r], [Math.cos(a1) * r, y, Math.sin(a1) * r], [0, y, 0]);
            builder.quad([Math.cos(a0) * r, y, Math.sin(a0) * r], [Math.cos(a0) * r, side * thickness, Math.sin(a0) * r], [Math.cos(a1) * r, side * thickness, Math.sin(a1) * r], [Math.cos(a1) * r, y, Math.sin(a1) * r]);
        }
    }
    return builder;
}

function BuildCrystalShard()
{
    // Elongated hexagonal bipyramid: sharp tip, faceted waist, chipped base.
    const builder = new MeshBuilder('CrystalShard');
    const sides = 6, waist = 0.34, hipY = 0.05;
    const hip = [], tip = [0, 1.0, 0], base = [0, -1.0, 0];
    for (let s = 0; s < sides; s += 1)
    {
        const a = (s / sides) * Math.PI * 2;
        hip.push([Math.cos(a) * waist, hipY, Math.sin(a) * waist]);
    }
    for (let s = 0; s < sides; s += 1)
    {
        const n = (s + 1) % sides;
        builder.triangle(hip[s], hip[n], tip);
        builder.triangle(hip[n], hip[s], base);
    }
    return builder;
}

function BuildHexPrism()
{
    const builder = new MeshBuilder('HexPrism');
    const polygon = [];
    for (let s = 0; s < 6; s += 1)
    {
        const a = (s / 6) * Math.PI * 2 + Math.PI / 6;
        polygon.push([Math.cos(a) * 0.78, Math.sin(a) * 0.78]);
    }
    builder.extrude(polygon, 0.62);
    return builder;
}

function BuildStarPoly()
{
    const builder = new MeshBuilder('StarPoly');
    const core = [[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1]].map((v) => scale(v, 0.52));
    const faces = [[0, 2, 4], [4, 2, 1], [1, 2, 5], [5, 2, 0], [4, 3, 0], [1, 3, 4], [5, 3, 1], [0, 3, 5]];
    for (const [i0, i1, i2] of faces)
    {
        const a = core[i0], b = core[i1], c = core[i2];
        const mid = scale(add(add(a, b), c), 1 / 3);
        const normal = unit(cross(sub(b, a), sub(c, a)));
        const apex = add(mid, scale(normal, 0.88));
        builder.triangle(a, b, apex);
        builder.triangle(b, c, apex);
        builder.triangle(c, a, apex);
    }
    return builder;
}

function BuildUtahTeapot()
{
    const builder = new MeshBuilder('UtahTeapot');
    const body = [
        [0.0, -0.58], [0.46, -0.58], [0.58, -0.45], [0.76, -0.18], [0.78, 0.08],
        [0.64, 0.34], [0.46, 0.48], [0.50, 0.54], [0.38, 0.55], [0.36, 0.62],
        [0.14, 0.69], [0.06, 0.72], [0.11, 0.80], [0.08, 0.87], [0.0, 0.89],
    ];
    builder.lathe(body, 26);
    builder.tube((u) => [
        0.64 + u * 0.58 + Math.sin(u * Math.PI) * 0.08,
        -0.22 + u * 0.78 - Math.sin(u * Math.PI * 1.15) * 0.14,
        0,
    ], 14, 10, (u) => 0.16 * (1 - u * 0.56));
    builder.tube((u) =>
    {
        const a = -Math.PI * 0.52 + u * Math.PI * 1.18;
        return [-0.64 - Math.cos(a) * 0.44, 0.06 + Math.sin(a) * 0.42, 0];
    }, 16, 10, (u) => 0.072 - u * 0.02);
    return builder;
}

function BuildSportsCar()
{
    const builder = new MeshBuilder('SportsCar');
    const stations = [
        { x: -1.04, yBot: -0.20, yTop: 0.16, yCab: 0.18, wBot: 0.44, wTop: 0.40, wCab: 0.34 },
        { x: -0.82, yBot: -0.22, yTop: 0.20, yCab: 0.28, wBot: 0.49, wTop: 0.44, wCab: 0.34 },
        { x: -0.45, yBot: -0.22, yTop: 0.21, yCab: 0.44, wBot: 0.48, wTop: 0.44, wCab: 0.32 },
        { x: 0.02, yBot: -0.22, yTop: 0.20, yCab: 0.45, wBot: 0.47, wTop: 0.43, wCab: 0.31 },
        { x: 0.42, yBot: -0.22, yTop: 0.18, yCab: 0.22, wBot: 0.48, wTop: 0.43, wCab: 0.34 },
        { x: 0.80, yBot: -0.21, yTop: 0.12, yCab: 0.13, wBot: 0.47, wTop: 0.41, wCab: 0.34 },
        { x: 1.06, yBot: -0.18, yTop: 0.02, yCab: 0.03, wBot: 0.40, wTop: 0.35, wCab: 0.28 },
    ];
    const rings = stations.map((station) => [
        [station.x, station.yBot, -station.wBot], [station.x, station.yTop, -station.wTop], [station.x, station.yCab, -station.wCab],
        [station.x, station.yCab, station.wCab], [station.x, station.yTop, station.wTop], [station.x, station.yBot, station.wBot],
    ]);
    for (let i = 0; i < rings.length - 1; i += 1)
    {
        const a = rings[i], b = rings[i + 1];
        for (let k = 0; k < a.length; k += 1)
        {
            const kn = (k + 1) % a.length;
            builder.quad(a[k], a[kn], b[kn], b[k]);
        }
    }
    for (const [ring, flip] of [[rings[0], true], [rings[rings.length - 1], false]])
    {
        const centre = [ring[0][0], (ring[0][1] + ring[2][1]) * 0.5, 0];
        for (let k = 0; k < ring.length; k += 1)
        {
            const kn = (k + 1) % ring.length;
            if (flip) builder.triangle(centre, ring[kn], ring[k]);
            else builder.triangle(centre, ring[k], ring[kn]);
        }
    }
    builder.box(-0.96, 0.30, 0, 0.11, 0.025, 0.44);
    builder.box(-0.94, 0.22, -0.24, 0.04, 0.07, 0.02);
    builder.box(-0.94, 0.22, 0.24, 0.04, 0.07, 0.02);
    for (const [wx, wz] of [[-0.64, -0.46], [-0.64, 0.46], [0.64, -0.46], [0.64, 0.46]])
    {
        const segments = 14, wheelRadius = 0.20, halfWidth = 0.08;
        for (let s = 0; s < segments; s += 1)
        {
            const a0 = (s / segments) * Math.PI * 2, a1 = ((s + 1) / segments) * Math.PI * 2;
            const p0i = [wx + Math.cos(a0) * wheelRadius, -0.18 + Math.sin(a0) * wheelRadius, wz - halfWidth];
            const p1i = [wx + Math.cos(a1) * wheelRadius, -0.18 + Math.sin(a1) * wheelRadius, wz - halfWidth];
            const p0o = [wx + Math.cos(a0) * wheelRadius, -0.18 + Math.sin(a0) * wheelRadius, wz + halfWidth];
            const p1o = [wx + Math.cos(a1) * wheelRadius, -0.18 + Math.sin(a1) * wheelRadius, wz + halfWidth];
            builder.quad(p0i, p0o, p1o, p1i);
            builder.triangle([wx, -0.18, wz + halfWidth], p0o, p1o);
            builder.triangle([wx, -0.18, wz - halfWidth], p1i, p0i);
        }
    }
    return builder;
}

function BuildObsidianHand()
{
    // Open sculpted hand: wrist block, palm volume and five articulated fingers.
    const builder = new MeshBuilder('ObsidianHand');
    builder.box(0, -0.72, 0, 0.24, 0.26, 0.18);
    builder.box(0, -0.32, 0, 0.36, 0.20, 0.16);
    const fingers = [
        { base: [-0.30, -0.16, 0.02], dir: [-0.14, 0.34, 0.02], length: 0.42, radius: 0.075 },
        { base: [-0.10, -0.12, 0.02], dir: [-0.04, 0.40, 0.02], length: 0.50, radius: 0.078 },
        { base: [0.10, -0.12, 0.02], dir: [0.05, 0.42, 0.02], length: 0.52, radius: 0.078 },
        { base: [0.29, -0.15, 0.02], dir: [0.16, 0.36, 0.02], length: 0.42, radius: 0.068 },
        { base: [0.34, -0.36, 0.10], dir: [0.34, 0.10, 0.10], length: 0.36, radius: 0.078 },
    ];
    for (const finger of fingers)
    {
        let point = finger.base;
        let direction = unit(finger.dir);
        for (let joint = 0; joint < 3; joint += 1)
        {
            const segment = scale(direction, finger.length / 3);
            const next = add(point, segment);
            const radius = finger.radius * (1 - joint * 0.16);
            builder.tube((u) => add(point, scale(segment, u)), 3, 8, (u) => radius * (1 - u * 0.14));
            builder.sphere(next, radius * 0.94, 5, 8);
            point = next;
            direction = unit([direction[0], direction[1] - 0.16, direction[2]]);
        }
    }
    return builder;
}

function BuildSculptedBust()
{
    const builder = new MeshBuilder('SculptedBust');
    const rings = 22, segments = 26;
    const grid = [];
    for (let r = 0; r <= rings; r += 1)
    {
        const v = r / rings, y = -0.95 + v * 1.92;
        const row = [];
        for (let s = 0; s < segments; s += 1)
        {
            const phi = (s / segments) * Math.PI * 2;
            const cosPhi = Math.cos(phi), sinPhi = Math.sin(phi);
            let rx = 0.42, rz = 0.42;
            if (v < 0.24) { const t = v / 0.24; rx = 0.78 - t * 0.46; rz = 0.38 - t * 0.12; }
            else if (v < 0.38) { const t = (v - 0.24) / 0.14; rx = 0.25 + t * 0.04; rz = 0.24 + t * 0.05; }
            else
            {
                const h = (v - 0.38) / 0.62;
                const profile = Math.sin(Math.pow(h, 0.85) * Math.PI);
                rx = 0.46 * profile * (h < 0.32 ? 0.78 + h * 0.7 : 1.0);
                rz = 0.54 * profile;
                if (sinPhi > 0.65 && h > 0.26 && h < 0.56)
                    rz += 0.12 * Math.pow(Math.max(0, (sinPhi - 0.65) / 0.35), 2.2) * Math.sin(((h - 0.26) / 0.30) * Math.PI);
                if (sinPhi > 0.35 && h > 0.46 && h < 0.62)
                    rz += 0.045 * Math.sin(((h - 0.46) / 0.16) * Math.PI) * Math.max(0, sinPhi - 0.35);
                if (sinPhi > 0.4 && h < 0.18) rz += 0.06 * (1 - h / 0.18) * sinPhi;
            }
            row.push([cosPhi * rx, y, sinPhi * rz]);
        }
        grid.push(row);
    }
    for (let r = 0; r < rings; r += 1)
        for (let s = 0; s < segments; s += 1)
        {
            const sn = (s + 1) % segments;
            builder.quad(grid[r][s], grid[r + 1][s], grid[r + 1][sn], grid[r][sn]);
        }
    const baseCentre = [0, -0.95, 0];
    for (let s = 0; s < segments; s += 1) builder.triangle(baseCentre, grid[0][(s + 1) % segments], grid[0][s]);
    return builder;
}

function BuildTorusKnot()
{
    const builder = new MeshBuilder('TorusKnot');
    builder.tube((u) =>
    {
        const theta = u * Math.PI * 2;
        const radius = 0.62 + 0.26 * Math.cos(3 * theta);
        return [radius * Math.cos(2 * theta), 0.32 * Math.sin(3 * theta), radius * Math.sin(2 * theta)];
    }, 64, 12, 0.165);
    return builder;
}

function BuildPrecisionGear()
{
    const builder = new MeshBuilder('PrecisionGear');
    const teeth = 12, polygon = [];
    for (let i = 0; i < teeth; i += 1)
    {
        const base = (i / teeth) * Math.PI * 2, step = (Math.PI * 2) / teeth;
        for (const [radius, angle] of [[0.68, base + step * 0.06], [0.96, base + step * 0.18], [0.96, base + step * 0.42], [0.68, base + step * 0.54]])
            polygon.push([Math.cos(angle) * radius, Math.sin(angle) * radius]);
    }
    builder.extrude(polygon, 0.20);
    const hub = [];
    for (let i = 0; i < 16; i += 1)
    {
        const a = (i / 16) * Math.PI * 2;
        hub.push([Math.cos(a) * 0.34, Math.sin(a) * 0.34]);
    }
    builder.extrude(hub, 0.32);
    return builder;
}

function BuildCrystalCluster()
{
    const builder = new MeshBuilder('CrystalCluster');
    const shards = [
        { ox: 0, oz: 0, tiltX: 0.04, tiltZ: -0.02, r: 0.30, h: 1.55 },
        { ox: -0.34, oz: 0.16, tiltX: -0.34, tiltZ: 0.16, r: 0.22, h: 1.15 },
        { ox: 0.36, oz: -0.14, tiltX: 0.32, tiltZ: -0.18, r: 0.24, h: 1.22 },
        { ox: 0.12, oz: 0.36, tiltX: 0.12, tiltZ: 0.36, r: 0.19, h: 0.96 },
        { ox: -0.22, oz: -0.32, tiltX: -0.22, tiltZ: -0.32, r: 0.18, h: 0.92 },
    ];
    for (const shard of shards)
    {
        const sides = 6, baseY = -0.65, topY = baseY + shard.h * 0.72, tipY = baseY + shard.h;
        const bottom = [], top = [];
        for (let s = 0; s < sides; s += 1)
        {
            const a = (s / sides) * Math.PI * 2;
            const lx = Math.cos(a) * shard.r, lz = Math.sin(a) * shard.r;
            bottom.push([shard.ox + lx, baseY, shard.oz + lz]);
            top.push([shard.ox + lx * 0.84 + shard.tiltX * shard.h * 0.72, topY, shard.oz + lz * 0.84 + shard.tiltZ * shard.h * 0.72]);
        }
        const tip = [shard.ox + shard.tiltX * shard.h, tipY, shard.oz + shard.tiltZ * shard.h];
        const baseCentre = [shard.ox, baseY, shard.oz];
        for (let s = 0; s < sides; s += 1)
        {
            const sn = (s + 1) % sides;
            builder.quad(bottom[s], top[s], top[sn], bottom[sn]);
            builder.triangle(top[s], tip, top[sn]);
            builder.triangle(baseCentre, bottom[sn], bottom[s]);
        }
    }
    return builder;
}

function BuildArchway()
{
    const builder = new MeshBuilder('Archway');
    builder.box(-0.62, -0.35, 0, 0.22, 0.65, 0.28);
    builder.box(0.62, -0.35, 0, 0.22, 0.65, 0.28);
    builder.box(-0.62, -0.94, 0, 0.28, 0.08, 0.34);
    builder.box(0.62, -0.94, 0, 0.28, 0.08, 0.34);
    const segments = 16, inner = 0.40, outer = 0.84, halfZ = 0.28;
    for (let i = 0; i < segments; i += 1)
    {
        const a0 = (i / segments) * Math.PI, a1 = ((i + 1) / segments) * Math.PI;
        const pi0f = [Math.cos(a0) * inner, 0.30 + Math.sin(a0) * inner, halfZ];
        const pi1f = [Math.cos(a1) * inner, 0.30 + Math.sin(a1) * inner, halfZ];
        const po0f = [Math.cos(a0) * outer, 0.30 + Math.sin(a0) * outer, halfZ];
        const po1f = [Math.cos(a1) * outer, 0.30 + Math.sin(a1) * outer, halfZ];
        const pi0b = [pi0f[0], pi0f[1], -halfZ], pi1b = [pi1f[0], pi1f[1], -halfZ];
        const po0b = [po0f[0], po0f[1], -halfZ], po1b = [po1f[0], po1f[1], -halfZ];
        builder.quad(pi0f, po0f, po1f, pi1f);
        builder.quad(po0b, pi0b, pi1b, po1b);
        builder.quad(po0f, po0b, po1b, po1f);
        builder.quad(pi0b, pi0f, pi1f, pi1b);
    }
    return builder;
}

function BuildFacetedSpire()
{
    const builder = new MeshBuilder('FacetedSpire');
    const segments = 20, rings = 6, grid = [];
    for (let r = 0; r <= rings; r += 1)
    {
        const t = r / rings, radius = (1 - t) * 0.85, y = -0.85 + t * 1.85;
        const row = [];
        for (let s = 0; s < segments; s += 1)
        {
            const a = (s / segments) * Math.PI * 2;
            row.push([Math.cos(a) * radius, y, Math.sin(a) * radius]);
        }
        grid.push(row);
    }
    for (let r = 0; r < rings - 1; r += 1)
        for (let s = 0; s < segments; s += 1)
        {
            const sn = (s + 1) % segments;
            builder.quad(grid[r][s], grid[r + 1][s], grid[r + 1][sn], grid[r][sn]);
        }
    const tip = [0, 1.0, 0], baseCentre = [0, -0.85, 0];
    for (let s = 0; s < segments; s += 1)
    {
        const sn = (s + 1) % segments;
        builder.triangle(grid[rings - 1][s], tip, grid[rings - 1][sn]);
        builder.triangle(baseCentre, grid[0][sn], grid[0][s]);
    }
    return builder;
}

function BuildWingedButterfly()
{
    // Four thin lofted wings with scalloped trailing edges around a tapered body.
    const builder = new MeshBuilder('WingedButterfly');
    builder.lathe([[0, -0.42], [0.07, -0.30], [0.10, 0.0], [0.08, 0.26], [0.04, 0.44], [0, 0.50]], 10);
    for (const side of [1, -1])
    {
        for (const [spanX, spanY, tipX, tipY, cc] of [[0.62, 0.30, 0.86, 0.10, 0.22], [0.50, -0.20, 0.66, -0.46, -0.16]])
        {
            const rootA = [side * 0.04, spanY + 0.12, 0.06];
            const rootB = [side * 0.04, spanY - 0.12, -0.06];
            const midA = [side * spanX * 0.72, spanY + cc * 0.5 + 0.10, 0.04];
            const midB = [side * spanX * 0.72, spanY + cc * 0.5 - 0.10, -0.02];
            const tip = [side * tipX, tipY, 0];
            builder.quad(rootA, midA, midB, rootB);
            builder.triangle(midA, tip, midB);
        }
    }
    return builder;
}

function BuildWingedBird()
{
    // Streamlined body, swept gull wings with a wrist joint, fan tail and beak.
    const builder = new MeshBuilder('WingedBird');
    builder.lathe([[0, -0.46], [0.05, -0.34], [0.16, -0.05], [0.19, 0.12], [0.13, 0.30], [0.07, 0.40], [0, 0.46]], 14);
    builder.triangle([0, 0.46, 0], [0.05, 0.58, 0], [-0.05, 0.58, 0]);
    for (const side of [1, -1])
    {
        const shoulder = [side * 0.12, 0.20, 0.02];
        const wrist = [side * 0.52, 0.28, -0.02];
        const trailing = [side * 0.34, 0.06, -0.06];
        const tip = [side * 0.94, 0.10, -0.14];
        builder.quad(shoulder, wrist, trailing, [side * 0.16, 0.02, 0.0]);
        builder.triangle(wrist, tip, trailing);
        builder.quad(trailing, tip, [side * 0.42, -0.10, -0.16], [side * 0.14, -0.14, -0.04]);
    }
    for (const offset of [-0.05, 0, 0.05]) builder.quad([0, -0.40, 0], [offset, -0.76, 0], [offset * 1.6, -0.80, 0], [offset * 0.6, -0.40, 0]);
    return builder;
}

function BuildFishBody()
{
    // Laterally compressed body with dorsal, pectoral and forked caudal fins.
    const builder = new MeshBuilder('FishBody');
    const rings = 12, sides = 16, grid = [];
    for (let r = 0; r <= rings; r += 1)
    {
        const v = r / rings;
        const x = -0.72 + v * 1.44;
        const profile = Math.sin(Math.pow(v, 0.82) * Math.PI);
        const row = [];
        for (let s = 0; s < sides; s += 1)
        {
            const a = (s / sides) * Math.PI * 2;
            row.push([x, Math.cos(a) * profile * 0.30, Math.sin(a) * profile * 0.13]);
        }
        grid.push(row);
    }
    for (let r = 0; r < rings; r += 1)
        for (let s = 0; s < sides; s += 1)
        {
            const sn = (s + 1) % sides;
            builder.quad(grid[r][s], grid[r + 1][s], grid[r + 1][sn], grid[r][sn]);
        }
    // Forked tail.
    builder.triangle([-0.70, 0, 0], [-1.02, 0.34, 0], [-0.92, 0, 0]);
    builder.triangle([-0.70, 0, 0], [-0.92, 0, 0], [-1.02, -0.34, 0]);
    // Dorsal and anal fins.
    builder.triangle([0.10, 0.28, 0], [-0.18, 0.62, 0], [-0.34, 0.20, 0]);
    builder.triangle([0.02, -0.26, 0], [-0.24, -0.52, 0], [-0.34, -0.16, 0]);
    for (const side of [1, -1]) builder.triangle([0.26, 0.02, side * 0.10], [-0.06, 0.16, side * 0.34], [-0.02, -0.10, side * 0.30]);
    return builder;
}

const MeshBuilders = Object.freeze({
    Cube: BuildCube,
    Sphere: BuildSphere,
    Coin: BuildCoin,
    CrystalShard: BuildCrystalShard,
    HexPrism: BuildHexPrism,
    StarPoly: BuildStarPoly,
    UtahTeapot: BuildUtahTeapot,
    SportsCar: BuildSportsCar,
    ObsidianHand: BuildObsidianHand,
    SculptedBust: BuildSculptedBust,
    TorusKnot: BuildTorusKnot,
    PrecisionGear: BuildPrecisionGear,
    CrystalCluster: BuildCrystalCluster,
    Archway: BuildArchway,
    FacetedSpire: BuildFacetedSpire,
    WingedButterfly: BuildWingedButterfly,
    WingedBird: BuildWingedBird,
    FishBody: BuildFishBody,
});

//------------------------------------------------------------------------------------------------------------------------
//                                                    FINALISE / PACK
//------------------------------------------------------------------------------------------------------------------------

const vertexKey = (point) => `${point[0].toFixed(4)}|${point[1].toFixed(4)}|${point[2].toFixed(4)}`;

/**
 * Centre the mesh, normalise it to unit bounding radius, then record per-triangle area, crease (ridge) weight and the crease
 * segments themselves — RidgeAccent sampling places particles along those segments.
 */
export function FinalizeMesh(builder)
{
    const raw = builder.Raw.slice(0, MaximumMeshTriangles);
    if (!raw.length) return null;
    let minX = Infinity, minY = Infinity, minZ = Infinity, maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
    for (const [a, b, c] of raw)
        for (const point of [a, b, c])
        {
            minX = Math.min(minX, point[0]); maxX = Math.max(maxX, point[0]);
            minY = Math.min(minY, point[1]); maxY = Math.max(maxY, point[1]);
            minZ = Math.min(minZ, point[2]); maxZ = Math.max(maxZ, point[2]);
        }
    const centre = [(minX + maxX) / 2, (minY + maxY) / 2, (minZ + maxZ) / 2];
    const extent = Math.max(maxX - minX, maxY - minY, maxZ - minZ) / 2;
    const scaleFactor = extent > 1e-6 ? 1 / extent : 1;

    const corners = new Float32Array(raw.length * 9);
    const normals = new Float32Array(raw.length * 9);
    const areas = new Float32Array(raw.length);
    const faceNormals = [];
    let totalArea = 0;
    raw.forEach(([a, b, c], index) =>
    {
        const pa = scale(sub(a, centre), scaleFactor), pb = scale(sub(b, centre), scaleFactor), pc = scale(sub(c, centre), scaleFactor);
        corners.set([pa[0], pa[1], pa[2], pb[0], pb[1], pb[2], pc[0], pc[1], pc[2]], index * 9);
        const normal = unit(cross(sub(pb, pa), sub(pc, pa)));
        faceNormals.push(normal);
        normals.set([normal[0], normal[1], normal[2], normal[0], normal[1], normal[2], normal[0], normal[1], normal[2]], index * 9);
        const area = length3(cross(sub(pb, pa), sub(pc, pa))) / 2;
        areas[index] = area;
        totalArea += area;
    });

    // Crease edges: shared vertex pairs whose face normals diverge by more than ~32°.
    const edgeFaces = new Map();
    const keys = [];
    const pointsByKey = new Map();
    for (let index = 0; index < raw.length; index += 1)
    {
        const ring = [0, 1, 2].map((corner) => vertexKey([corners[index * 9 + corner * 3], corners[index * 9 + corner * 3 + 1], corners[index * 9 + corner * 3 + 2]]));
        keys.push(ring);
        for (let corner = 0; corner < 3; corner += 1)
        {
            if (!pointsByKey.has(ring[corner]))
                pointsByKey.set(ring[corner], [corners[index * 9 + corner * 3], corners[index * 9 + corner * 3 + 1], corners[index * 9 + corner * 3 + 2]]);
            const a = ring[corner], b = ring[(corner + 1) % 3];
            const key = a < b ? `${a}~${b}` : `${b}~${a}`;
            const bucket = edgeFaces.get(key);
            if (bucket) bucket.push(index);
            else edgeFaces.set(key, [index]);
        }
    }
    const creases = [];
    const creaseCount = new Float32Array(raw.length);
    for (const [key, faces] of edgeFaces)
    {
        if (faces.length !== 2) continue;
        const [first, second] = faces;
        if (Math.abs(faceNormals[first][0] * faceNormals[second][0] + faceNormals[first][1] * faceNormals[second][1] + faceNormals[first][2] * faceNormals[second][2]) > 0.84) continue;
        creaseCount[first] += 1;
        creaseCount[second] += 1;
        const [aKey, bKey] = key.split('~');
        creases.push({ A: pointsByKey.get(aKey) || [0, 0, 0], B: pointsByKey.get(bKey) || [0, 0, 0] });
        if (creases.length > 4096) break;
    }
    if (!creases.length)
    {
        // Smooth mesh (sphere, teapot): treat the upper silhouette rings as ridges so RidgeAccent still reads as a stylised contour.
        for (let index = 0; index < raw.length; index += 7)
        {
            if (corners[index * 9 + 1] < 0.05) continue;
            creases.push({
                A: [corners[index * 9], corners[index * 9 + 1], corners[index * 9 + 2]],
                B: [corners[index * 9 + 3], corners[index * 9 + 4], corners[index * 9 + 5]],
            });
            if (creases.length > 512) break;
        }
    }
    const ridgeWeights = new Float32Array(raw.length);
    for (let index = 0; index < raw.length; index += 1) ridgeWeights[index] = areas[index] * (1 + creaseCount[index] * 1.6);

    const bounds = { Minimum: [minX, minY, minZ], Maximum: [maxX, maxY, maxZ], Centre: centre, Radius: extent };
    return {
        Name: builder.Name,
        Corners: corners,
        Normals: normals,
        Areas: areas,
        RidgeWeights: ridgeWeights,
        Creases: creases,
        TotalArea: totalArea,
        Bounds: bounds,
        Radius: 1,
        Triangles: raw.length,
        VertexCount: raw.length * 3,
    };
}

const BuiltInCache = new Map();

/** Resolves a built-in object name to its mesh record (cached). CustomObject is resolved by ResolveObjectMesh. */
export function ObjectMesh(name)
{
    const key = BuiltInObjectNames.includes(name) ? name : 'UtahTeapot';
    if (BuiltInCache.has(key)) return BuiltInCache.get(key);
    const builder = MeshBuilders[key] ? MeshBuilders[key]() : BuildUtahTeapot();
    const mesh = FinalizeMesh(builder);
    BuiltInCache.set(key, mesh);
    return mesh;
}

/** Object mesh name a node asks for, honouring the CustomObject registry with a teapot fallback. */
export function ResolveObjectMesh(name, customId = '')
{
    if (name === 'CustomObject')
    {
        const entry = CustomObjectRegistry.get(customId);
        if (entry && entry.Mesh) return entry.Mesh;
        return ObjectMesh('UtahTeapot');
    }
    return ObjectMesh(name);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CUSTOM MESH IMPORT
//------------------------------------------------------------------------------------------------------------------------

/** Runtime registry of imported meshes. Kept out of scene JSON (the document stores only the id). */
export const CustomObjectRegistry = new Map();
let CustomObjectSequence = 0;

export function RegisterCustomObject(name, mesh)
{
    if (!mesh || !mesh.Triangles) return null;
    CustomObjectSequence += 1;
    const id = `mesh-${CustomObjectSequence}`;
    CustomObjectRegistry.set(id, { Id: id, Name: name || `Custom Object ${CustomObjectSequence}`, Mesh: mesh, TriangleCount: mesh.Triangles });
    return id;
}

export function CustomObjectEntry(id) { return (id && CustomObjectRegistry.get(id)) || null; }
export function CustomObjectIds() { return [...CustomObjectRegistry.keys()]; }

/** Wavefront .obj text → mesh record. Supports negative indices, polygons and `v/vt/vn` triples. */
export function ParseObjMesh(text, name = 'Imported OBJ')
{
    const builder = new MeshBuilder(name);
    const vertices = [];
    for (const line of String(text || '').split(/\r?\n/))
    {
        const trimmed = line.trim();
        if (!trimmed || trimmed.startsWith('#')) continue;
        if (trimmed.startsWith('v '))
        {
            const parts = trimmed.split(/\s+/);
            const point = [parseFloat(parts[1]), parseFloat(parts[2]), parseFloat(parts[3])];
            if (point.every(Number.isFinite)) vertices.push(point);
        }
        else if (trimmed.startsWith('f '))
        {
            const indices = [];
            for (const token of trimmed.split(/\s+/).slice(1))
            {
                const raw = parseInt(token.split('/')[0], 10);
                if (!Number.isFinite(raw)) continue;
                const index = raw < 0 ? vertices.length + raw : raw - 1;
                if (index >= 0 && index < vertices.length) indices.push(index);
            }
            for (let k = 1; k + 1 < indices.length; k += 1) builder.triangle(vertices[indices[0]], vertices[indices[k]], vertices[indices[k + 1]]);
        }
    }
    if (!builder.Raw.length) return null;
    return FinalizeMesh(builder);
}

/** ASCII or binary STL → mesh record. */
export function ParseStlMesh(bufferOrText, name = 'Imported STL')
{
    const builder = new MeshBuilder(name);
    if (bufferOrText instanceof ArrayBuffer)
    {
        const bytes = new Uint8Array(bufferOrText);
        if (bytes.byteLength >= 84)
        {
            const view = new DataView(bufferOrText);
            const count = view.getUint32(80, true);
            if (count > 0 && count < MaximumMeshTriangles && 84 + count * 50 === bytes.byteLength)
            {
                let offset = 84;
                for (let i = 0; i < count; i += 1)
                {
                    offset += 12;
                    const a = [view.getFloat32(offset, true), view.getFloat32(offset + 4, true), view.getFloat32(offset + 8, true)]; offset += 12;
                    const b = [view.getFloat32(offset, true), view.getFloat32(offset + 4, true), view.getFloat32(offset + 8, true)]; offset += 12;
                    const c = [view.getFloat32(offset, true), view.getFloat32(offset + 4, true), view.getFloat32(offset + 8, true)]; offset += 14;
                    builder.triangle(a, b, c);
                }
                return builder.Raw.length ? FinalizeMesh(builder) : null;
            }
        }
        bufferOrText = new TextDecoder().decode(bytes);
    }
    const matches = [...String(bufferOrText || '').matchAll(/vertex\s+([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s+([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s+([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)/g)];
    for (let i = 0; i + 2 < matches.length; i += 3)
    {
        const point = (match) => [parseFloat(match[1]), parseFloat(match[2]), parseFloat(match[3])];
        builder.triangle(point(matches[i]), point(matches[i + 1]), point(matches[i + 2]));
    }
    return builder.Raw.length ? FinalizeMesh(builder) : null;
}

/** JSON mesh: `{ vertices: [x,y,z,…] | [[x,y,z],…], indices | faces: […] }`, flat triangles, or a Frontier facet document. */
export function ParseJsonMesh(text, name = 'Imported JSON')
{
    let data;
    try { data = JSON.parse(String(text || '')); } catch { return null; }
    const rawVertices = data.vertices || data.positions || data.Vertices || data.Positions;
    const rawIndices = data.indices || data.faces || data.triangles || data.Indices || data.Faces;
    if (!Array.isArray(rawVertices)) return null;
    const vertices = Array.isArray(rawVertices[0])
        ? rawVertices.map((v) => [Number(v[0]) || 0, Number(v[1]) || 0, Number(v[2]) || 0])
        : Array.from({ length: Math.floor(rawVertices.length / 3) }, (_, i) => [Number(rawVertices[i * 3]) || 0, Number(rawVertices[i * 3 + 1]) || 0, Number(rawVertices[i * 3 + 2]) || 0]);
    const builder = new MeshBuilder(name);
    if (Array.isArray(rawIndices) && rawIndices.length)
    {
        if (Array.isArray(rawIndices[0]))
        {
            for (const face of rawIndices)
                for (let k = 1; k + 1 < face.length; k += 1)
                    if (vertices[face[0]] && vertices[face[k]] && vertices[face[k + 1]]) builder.triangle(vertices[face[0]], vertices[face[k]], vertices[face[k + 1]]);
        }
        else
        {
            for (let i = 0; i + 2 < rawIndices.length; i += 3)
                if (vertices[rawIndices[i]] && vertices[rawIndices[i + 1]] && vertices[rawIndices[i + 2]])
                    builder.triangle(vertices[rawIndices[i]], vertices[rawIndices[i + 1]], vertices[rawIndices[i + 2]]);
        }
    }
    else for (let i = 0; i + 2 < vertices.length; i += 3) builder.triangle(vertices[i], vertices[i + 1], vertices[i + 2]);
    return builder.Raw.length ? FinalizeMesh(builder) : null;
}

/** File-name driven import used by the Inspector button and viewport drag-and-drop. Returns { Id, Name, TriangleCount } or null. */
export function ImportCustomObject(fileName, bufferOrText)
{
    const lower = String(fileName || '').toLowerCase();
    const plain = lower.replace(/\.[^.]+$/, '') || 'Custom Object';
    let mesh = null;
    if (lower.endsWith('.stl')) mesh = ParseStlMesh(bufferOrText, plain);
    else if (lower.endsWith('.obj')) mesh = ParseObjMesh(String(bufferOrText || ''), plain);
    else if (lower.endsWith('.json') || lower.endsWith('.facet')) mesh = ParseJsonMesh(String(bufferOrText || ''), plain);
    else if (bufferOrText instanceof ArrayBuffer) mesh = ParseStlMesh(bufferOrText, plain) || ParseObjMesh(new TextDecoder().decode(new Uint8Array(bufferOrText)), plain);
    else mesh = ParseObjMesh(String(bufferOrText || ''), plain);
    if (!mesh) return null;
    const id = RegisterCustomObject(plain, mesh);
    return { Id: id, Name: plain, TriangleCount: mesh.Triangles };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TEXEL PACKING (GL_VERTEXID INSTANCING)
//------------------------------------------------------------------------------------------------------------------------

/** Packs the mesh so a vertex shader can rebuild it from gl_VertexID: two RGBA32F texels per corner. */
export function PackObjectTexels(mesh)
{
    const triangles = mesh ? mesh.Triangles : 0;
    const texelCount = Math.max(1, triangles * 6);
    const width = Math.min(1024, Math.max(1, texelCount));
    const height = Math.ceil(texelCount / width);
    const data = new Float32Array(width * height * 4);
    if (mesh)
    {
        for (let triangle = 0; triangle < triangles; triangle += 1)
            for (let corner = 0; corner < 3; corner += 1)
            {
                const source = triangle * 9 + corner * 3;
                const first = (triangle * 6 + corner * 2) * 4;
                data[first] = mesh.Corners[source];
                data[first + 1] = mesh.Corners[source + 1];
                data[first + 2] = mesh.Corners[source + 2];
                data[first + 3] = mesh.Normals[source];
                data[first + 4] = mesh.Normals[source + 1];
                data[first + 5] = mesh.Normals[source + 2];
                data[first + 6] = 0;
                data[first + 7] = 0;
            }
    }
    return { Data: data, Width: width, Height: height, TriangleCount: triangles, VertexCount: triangles * 3 };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ANCHOR SAMPLING
//------------------------------------------------------------------------------------------------------------------------

function hash01(index, salt)
{
    let x = Math.imul(index + 1, 0x9e3779b1) ^ Math.imul(salt + 7, 0x85ebca6b);
    x ^= x >>> 15; x = Math.imul(x, 0x2545f491); x ^= x >>> 13;
    return (x >>> 0) / 4294967296;
}

function rotateY(point, radians)
{
    const c = Math.cos(radians), s = Math.sin(radians);
    return [point[0] * c + point[2] * s, point[1], -point[0] * s + point[2] * c];
}

function cumulative(weights)
{
    const cdf = new Float64Array(weights.length);
    let total = 0;
    for (let i = 0; i < weights.length; i += 1) { total += weights[i]; cdf[i] = total; }
    const inv = total > 1e-9 ? 1 / total : 1;
    for (let i = 0; i < cdf.length; i += 1) cdf[i] *= inv;
    return cdf;
}

function pickIndex(cdf, u)
{
    let lo = 0, hi = cdf.length - 1;
    while (lo < hi)
    {
        const mid = (lo + hi) >> 1;
        if (cdf[mid] < u) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

/**
 * Distributes `count` anchors over a mesh. Even surface samples are stratified across the area CDF, SurfaceVolume pulls a third
 * of them toward the centroid, RidgeAccent walks the crease segments and then tops up on the surface.
 * Returns Float32Arrays ready for an RGBA32F upload, plus a plain list for tests.
 */
export function SampleObjectAnchors(mesh, count, mode = 'EvenSurface', yawDegrees = 0)
{
    const total = Math.max(1, Math.min(MaximumAnchorCount, Math.round(count)));
    const positions = new Float32Array(total * 3);
    const normals = new Float32Array(total * 3);
    const ridges = new Float32Array(total);
    const anchors = [];
    if (!mesh || !mesh.Triangles)
    {
        for (let i = 0; i < total; i += 1) anchors.push({ Position: [0, 0, 0], Normal: [0, 1, 0], Ridge: 0 });
        return { Positions: positions, Normals: normals, Ridges: ridges, Anchors: anchors, Count: total };
    }
    const modeIndex = Math.max(0, ObjectSampleModes.indexOf(mode));
    const cdf = cumulative(Array.from(mesh.Areas));
    const ridgeCdf = cumulative(Array.from(mesh.RidgeWeights));
    const creases = mesh.Creases || [];
    const creaseLengths = creases.map((crease) => length3(sub(crease.B, crease.A)));
    const creaseCdf = cumulative(creaseLengths);
    const totalCreaseLength = creaseLengths.reduce((sum, length) => sum + length, 0);
    const yaw = (Number(yawDegrees) || 0) * Math.PI / 180;
    const ridgeFraction = modeIndex === 2 ? (creases.length && totalCreaseLength > 1e-6 ? 0.72 : 0) : 0;

    for (let i = 0; i < total; i += 1)
    {
        let position, normal, ridge = 0;
        if (ridgeFraction > 0 && hash01(i, 13) < ridgeFraction)
        {
            const crease = creases[pickIndex(creaseCdf, hash01(i, 17))];
            const t = hash01(i, 19);
            position = [crease.A[0] + (crease.B[0] - crease.A[0]) * t, crease.A[1] + (crease.B[1] - crease.A[1]) * t, crease.A[2] + (crease.B[2] - crease.A[2]) * t];
            normal = unit(position);
            ridge = 1;
        }
        else
        {
            const u = (i + hash01(i, 11) * 0.92) / total;
            const triangle = pickIndex(ridgeFraction > 0 && hash01(i, 23) < 0.5 ? ridgeCdf : cdf, Math.min(0.999999, u));
            const source = triangle * 9;
            const r1 = Math.sqrt(hash01(i, 29));
            const r2 = hash01(i, 31);
            const wa = 1 - r1, wb = r1 * (1 - r2), wc = r1 * r2;
            position = [0, 0, 0];
            normal = [0, 0, 0];
            for (let axis = 0; axis < 3; axis += 1)
            {
                position[axis] = mesh.Corners[source + axis] * wa + mesh.Corners[source + 3 + axis] * wb + mesh.Corners[source + 6 + axis] * wc;
                normal[axis] = mesh.Normals[source + axis];
            }
            if (modeIndex === 1 && i % 3 === 0)
            {
                const depth = 0.25 + 0.68 * Math.cbrt(hash01(i, 37));
                position = position.map((component) => component * depth);
            }
            normal = unit(normal);
        }
        position = rotateY(position, yaw);
        normal = rotateY(normal, yaw);
        positions.set(position, i * 3);
        normals.set(normal, i * 3);
        ridges[i] = ridge;
        anchors.push({ Position: position, Normal: normal, Ridge: ridge });
    }
    return { Positions: positions, Normals: normals, Ridges: ridges, Anchors: anchors, Count: total };
}

/** Packs anchors into the two-texel-per-anchor RGBA32F layout the spawn shader fetches. */
export function PackAnchorTexels(sampled)
{
    const count = sampled.Count;
    const texelCount = Math.max(2, count * 2);
    const height = Math.ceil(texelCount / AnchorTextureWidth);
    const data = new Float32Array(AnchorTextureWidth * height * 4);
    for (let i = 0; i < count; i += 1)
    {
        const first = i * 8, second = first + 4;
        data[first] = sampled.Positions[i * 3];
        data[first + 1] = sampled.Positions[i * 3 + 1];
        data[first + 2] = sampled.Positions[i * 3 + 2];
        data[first + 3] = sampled.Positions[i * 3 + 1];
        data[second] = sampled.Normals[i * 3];
        data[second + 1] = sampled.Normals[i * 3 + 1];
        data[second + 2] = sampled.Normals[i * 3 + 2];
        data[second + 3] = sampled.Ridges[i];
    }
    return { Data: data, Width: AnchorTextureWidth, Height: height, Count: count };
}

/** Cache key: only the geometry and sample settings that change the anchors. */
export function ObjectAnchorSignature(node)
{
    return [node.SpawnObject, node.CustomObjectId || '', node.SpawnSampleMode, Math.round(node.ObjectYaw || 0), node.Count, node.Shape].join('|');
}
