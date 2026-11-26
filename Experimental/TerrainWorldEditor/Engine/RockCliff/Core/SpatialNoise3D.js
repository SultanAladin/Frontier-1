/*==========================================================================================================================================
                                                       SPATIALNOISE3D.JS
==========================================================================================================================================*/
// Pure-SDF 3D Spatial Noise, Deterministic PRNG, Geological Strike/Dip Frames, CSG Chamfer Operators,
// 3D Voronoi Planar Cleavage, and Hierarchical 90-Degree T-Junction Thermal Crazing Fields.
// Zero external dependencies; designed so CPU JS and WebGL2 GLSL evaluate matching procedural math.

export const DEG2RAD = Math.PI / 180;

export function clamp(v, lo, hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

export function mix(a, b, t)
{
    return a + (b - a) * t;
}

export function smoothstep(e0, e1, x)
{
    const t = clamp((x - e0) / (e1 - e0 || 1e-8), 0, 1);
    return t * t * (3 - 2 * t);
}

export function dot3(ax, ay, az, bx, by, bz)
{
    return ax * bx + ay * by + az * bz;
}

export function vdot(a, b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

export function vsub(a, b)
{
    return [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
}

export function vadd(a, b)
{
    return [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
}

export function vscale(a, s)
{
    return [a[0] * s, a[1] * s, a[2] * s];
}

export function vcross(a, b)
{
    return [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]
    ];
}

export function vlen(a)
{
    return Math.hypot(a[0], a[1], a[2]);
}

export function vnorm(a)
{
    const l = Math.hypot(a[0], a[1], a[2]);
    if (l < 1e-12) return [0, 1, 0];
    const inv = 1 / l;
    return [a[0] * inv, a[1] * inv, a[2] * inv];
}

/**
 * Rotate vector v around unit axis k by angle theta (radians) via Rodrigues formula.
 */
export function rotateAroundAxis(v, k, theta)
{
    const c = Math.cos(theta);
    const s = Math.sin(theta);
    const kv = vdot(k, v);
    const cx = k[1] * v[2] - k[2] * v[1];
    const cy = k[2] * v[0] - k[0] * v[2];
    const cz = k[0] * v[1] - k[1] * v[0];
    return [
        v[0] * c + cx * s + k[0] * kv * (1 - c),
        v[1] * c + cy * s + k[1] * kv * (1 - c),
        v[2] * c + cz * s + k[2] * kv * (1 - c)
    ];
}

/**
 * Build a 3x3 rotation matrix (row-major 9 floats) for Rodrigues rotation around unit axis k by theta.
 */
export function buildAxisAngleMatrix3(axis, theta)
{
    if (Math.abs(theta) <= 1e-7)
    {
        return [1, 0, 0, 0, 1, 0, 0, 0, 1];
    }
    const k = vnorm(axis);
    const x = k[0], y = k[1], z = k[2];
    const c = Math.cos(theta), s = Math.sin(theta), t = 1 - c;
    return [
        t * x * x + c,     t * x * y - s * z, t * x * z + s * y,
        t * x * y + s * z, t * y * y + c,     t * y * z - s * x,
        t * x * z - s * y, t * y * z + s * x, t * z * z + c
    ];
}

export function mulMat3(A, B)
{
    return [
        A[0] * B[0] + A[1] * B[3] + A[2] * B[6],
        A[0] * B[1] + A[1] * B[4] + A[2] * B[7],
        A[0] * B[2] + A[1] * B[5] + A[2] * B[8],

        A[3] * B[0] + A[4] * B[3] + A[5] * B[6],
        A[3] * B[1] + A[4] * B[4] + A[5] * B[7],
        A[3] * B[2] + A[4] * B[5] + A[5] * B[8],

        A[6] * B[0] + A[7] * B[3] + A[8] * B[6],
        A[6] * B[1] + A[7] * B[4] + A[8] * B[7],
        A[6] * B[2] + A[7] * B[5] + A[8] * B[8]
    ];
}

export function transposeMat3(M)
{
    return [
        M[0], M[3], M[6],
        M[1], M[4], M[7],
        M[2], M[5], M[8]
    ];
}

export function applyMat3(M, v)
{
    return [
        M[0] * v[0] + M[1] * v[1] + M[2] * v[2],
        M[3] * v[0] + M[4] * v[1] + M[5] * v[2],
        M[6] * v[0] + M[7] * v[1] + M[8] * v[2]
    ];
}

/**
 * Perturb a unit vector by random angular cone dispersion (radians).
 */
export function perturbNormal(n, rng, maxAngleRad)
{
    if (maxAngleRad <= 1e-5) return [n[0], n[1], n[2]];
    const arb = Math.abs(n[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0];
    const t1 = vnorm(vcross(n, arb));
    const t2 = vcross(n, t1);
    const phi = rng.float() * Math.PI * 2;
    const axis = [
        t1[0] * Math.cos(phi) + t2[0] * Math.sin(phi),
        t1[1] * Math.cos(phi) + t2[1] * Math.sin(phi),
        t1[2] * Math.cos(phi) + t2[2] * Math.sin(phi)
    ];
    const angle = rng.gauss(0, maxAngleRad * 0.55);
    return vnorm(rotateAroundAxis(n, axis, clamp(angle, -maxAngleRad, maxAngleRad)));
}

/**
 * Build right-handed orthonormal geological frame from Strike and Dip (radians).
 */
export function buildStrikeDipFrame(strikeRad, dipRad)
{
    const cs = Math.cos(strikeRad);
    const ss = Math.sin(strikeRad);
    const cd = Math.cos(dipRad);
    const sd = Math.sin(dipRad);

    const strikeVec = [cs, 0, -ss];
    const dipVec = [cd * ss, -sd, cd * cs];
    const normal = [sd * ss, cd, sd * cs];
    return { strikeVec, dipVec, normal };
}

/**
 * Smooth minimum (polynomial C1 union, k = blend radius > 0).
 */
export function smin(a, b, k)
{
    if (k <= 1e-6) return a < b ? a : b;
    const h = Math.max(k - Math.abs(a - b), 0) / k;
    return (a < b ? a : b) - h * h * k * 0.25;
}

/**
 * Smooth maximum (polynomial C1 intersection / rounding, k = radius > 0).
 */
export function smax(a, b, k)
{
    if (k <= 1e-6) return a > b ? a : b;
    const h = Math.max(k - Math.abs(a - b), 0) / k;
    return (a > b ? a : b) + h * h * k * 0.25;
}

/**
 * Chisel-chamfered intersection of two half-spaces (a <= 0 and b <= 0).
 * Produces crisp 45-degree chipped rock arris instead of plastic round fillet.
 */
export function chamferMax(a, b, r)
{
    const m = a > b ? a : b;
    if (r <= 1e-6) return m;
    const ch = (a + b + r) * 0.7071067811865475;
    return ch > m ? ch : m;
}

/**
 * Chisel-chamfered subtraction of cutter SDF `cut` from solid `base` (`max(base, -cut)` with 45-deg rim bevel).
 */
export function chamferSub(base, cut, r)
{
    return chamferMax(base, -cut, r);
}

/*==========================================================================================================================================
                                                 DETERMINISTIC PRNG (SFC32) & 3D HASH
==========================================================================================================================================*/

export function hashSeed(seed)
{
    const str = String(seed ?? 1337);
    let h1 = 1779033703, h2 = 3144134277, h3 = 1013904242, h4 = 2773480762;
    for (let i = 0; i < str.length; i++)
    {
        const k = str.charCodeAt(i);
        h1 = h2 ^ Math.imul(h1 ^ k, 597399067);
        h2 = h3 ^ Math.imul(h2 ^ k, 2869860233);
        h3 = h4 ^ Math.imul(h3 ^ k, 951274213);
        h4 = h1 ^ Math.imul(h4 ^ k, 2716044179);
    }
    h1 = Math.imul(h3 ^ (h1 >>> 18), 597399067);
    h2 = Math.imul(h4 ^ (h2 >>> 22), 2869860233);
    h3 = Math.imul(h1 ^ (h3 >>> 17), 951274213);
    h4 = Math.imul(h2 ^ (h4 >>> 19), 2716044179);
    return [(h1 ^ h2 ^ h3 ^ h4) >>> 0, (h2 ^ h1) >>> 0, (h3 ^ h1) >>> 0, (h4 ^ h1) >>> 0];
}

export function createRng(seed = 1337)
{
    let [a, b, c, d] = hashSeed(seed);

    function nextU32()
    {
        a >>>= 0; b >>>= 0; c >>>= 0; d >>>= 0;
        let t = (a + b) | 0;
        a = b ^ (b >>> 9);
        b = (c + (c << 3)) | 0;
        c = (c << 21) | (c >>> 11);
        d = (d + 1) | 0;
        t = (t + d) | 0;
        c = (c + t) | 0;
        return t >>> 0;
    }

    function float()
    {
        return nextU32() / 4294967296;
    }

    function range(min, max)
    {
        return min + (max - min) * float();
    }

    function signed()
    {
        return float() * 2 - 1;
    }

    function int(min, maxInclusive)
    {
        return min + Math.floor(float() * (maxInclusive - min + 1));
    }

    function chance(prob)
    {
        return float() < prob;
    }

    let spareGauss = null;
    function gauss(mean = 0, std = 1)
    {
        if (spareGauss !== null)
        {
            const v = spareGauss;
            spareGauss = null;
            return mean + std * v;
        }
        let u = 0, v = 0, s = 0;
        do
        {
            u = float() * 2 - 1;
            v = float() * 2 - 1;
            s = u * u + v * v;
        } while (s >= 1 || s === 0);
        const mul = Math.sqrt((-2 * Math.log(s)) / s);
        spareGauss = v * mul;
        return mean + std * (u * mul);
    }

    function fork(label)
    {
        return createRng(`${seed}:${label}:${nextU32()}`);
    }

    return { nextU32, float, range, signed, int, chance, gauss, fork };
}

/**
 * Fast zero-allocation 3D integer hash -> uint32.
 * Note: Uses exact sine-free integer hashing; GLSL mirror uses matching sin/fract or integer hash.
 */
export function hash3u(ix, iy, iz, salt = 0)
{
    let h = (Math.imul(ix | 0, 374761393) + Math.imul(iy | 0, 668265263) + Math.imul(iz | 0, 2147483647) + Math.imul(salt | 0, 1597334677)) | 0;
    h = Math.imul(h ^ (h >>> 13), 1274126177);
    return (h ^ (h >>> 16)) >>> 0;
}

export function u32ToUnit(h)
{
    return (h >>> 0) * 2.3283064365386963e-10;
}

/*==========================================================================================================================================
                                      SHARED TRIG-FREE / GPU-PORTABLE 3D VALUE NOISE & FBM
==========================================================================================================================================*/
// We use a deterministic sin/fract-free integer hash `hash31` that translates 1-to-1 into WebGL2 GLSL (`uint` bitwise ops)
// so that CPU QEF Dual Contouring and GPU 60 FPS Raymarching evaluate the EXACT SAME 3D fracture warps!

export function hash31(ix, iy, iz, salt = 0)
{
    let n = (Math.imul(ix | 0, 1597334677) ^ Math.imul(iy | 0, 3812015801) ^ Math.imul(iz | 0, 2798796415) ^ Math.imul(salt | 0, 198491317)) >>> 0;
    n = Math.imul(n ^ (n >>> 15), 2246822519) >>> 0;
    n = Math.imul(n ^ (n >>> 13), 3266489917) >>> 0;
    n = (n ^ (n >>> 16)) >>> 0;
    return n * 2.3283064365386963e-10; // [0, 1)
}

export function scalarField3D(x, y, z, salt = 0)
{
    const ix = Math.floor(x);
    const iy = Math.floor(y);
    const iz = Math.floor(z);
    const fx = x - ix;
    const fy = y - iy;
    const fz = z - iz;

    const ux = fx * fx * (3 - 2 * fx);
    const uy = fy * fy * (3 - 2 * fy);
    const uz = fz * fz * (3 - 2 * fz);

    const n000 = hash31(ix, iy, iz, salt);
    const n100 = hash31(ix + 1, iy, iz, salt);
    const n010 = hash31(ix, iy + 1, iz, salt);
    const n110 = hash31(ix + 1, iy + 1, iz, salt);
    const n001 = hash31(ix, iy, iz + 1, salt);
    const n101 = hash31(ix + 1, iy, iz + 1, salt);
    const n011 = hash31(ix, iy + 1, iz + 1, salt);
    const n111 = hash31(ix + 1, iy + 1, iz + 1, salt);

    const nx00 = n000 + (n100 - n000) * ux;
    const nx10 = n010 + (n110 - n010) * ux;
    const nx01 = n001 + (n101 - n001) * ux;
    const nx11 = n011 + (n111 - n011) * ux;

    const nxy0 = nx00 + (nx10 - nx00) * uy;
    const nxy1 = nx01 + (nx11 - nx01) * uy;

    return (nxy0 + (nxy1 - nxy0) * uz) * 2.0 - 1.0; // [-1, 1]
}

export function fbm3D(x, y, z, octaves = 3, salt = 0)
{
    let sum = 0;
    let amp = 0.5;
    let freq = 1.0;
    let norm = 0;
    for (let i = 0; i < octaves; i++)
    {
        sum += amp * scalarField3D(x * freq, y * freq, z * freq, salt + i * 101);
        norm += amp;
        freq *= 2.03;
        amp *= 0.5;
    }
    return sum / (norm || 1);
}

export function ridged3D(x, y, z, octaves = 3, salt = 0)
{
    let sum = 0;
    let amp = 0.55;
    let freq = 1.0;
    let weight = 1.0;
    let norm = 0;
    for (let i = 0; i < octaves; i++)
    {
        let n = 1.0 - Math.abs(scalarField3D(x * freq, y * freq, z * freq, salt + i * 137));
        n = n * n * weight;
        weight = clamp(n * 1.6, 0.15, 1.0);
        sum += n * amp;
        norm += amp;
        freq *= 2.11;
        amp *= 0.5;
    }
    return sum / (norm || 1);
}

/*==========================================================================================================================================
                              3D PLANAR VORONOI CLEAVAGE & 90-DEGREE T-JUNCTION THERMAL CRAZING
==========================================================================================================================================*/

export const cleavageResult = {
    wallDist: 1.0,
    cellRand: 0.0,
    cellId: 0,
    wnx: 0.0,
    wny: 1.0,
    wnz: 0.0
};

/**
 * 3D Planar Voronoi Cleavage (exact Euclidean bisector distance between nearest two jittered cell seeds).
 * Produces crisp polygonal rock jointing & columnar cooling prisms!
 */
export function evalPlanarCleavage3D(x, y, z, jitter = 0.72, salt = 0)
{
    const ix = Math.floor(x);
    const iy = Math.floor(y);
    const iz = Math.floor(z);

    let min1 = 1e9;
    let min2 = 1e9;
    let id1 = 0;
    let c1x = 0, c1y = 0, c1z = 0;
    let c2x = 0, c2y = 0, c2z = 0;

    for (let dz = -1; dz <= 1; dz++)
    {
        const cz = iz + dz;
        for (let dy = -1; dy <= 1; dy++)
        {
            const cy = iy + dy;
            for (let dx = -1; dx <= 1; dx++)
            {
                const cx = ix + dx;
                const jx = (hash31(cx, cy, cz, salt + 11) - 0.5) * jitter;
                const jy = (hash31(cx, cy, cz, salt + 29) - 0.5) * jitter;
                const jz = (hash31(cx, cy, cz, salt + 47) - 0.5) * jitter;

                const px = cx + 0.5 + jx;
                const py = cy + 0.5 + jy;
                const pz = cz + 0.5 + jz;

                const rx = px - x;
                const ry = py - y;
                const rz = pz - z;
                const d2 = rx * rx + ry * ry + rz * rz;

                if (d2 < min1)
                {
                    min2 = min1;
                    c2x = c1x; c2y = c1y; c2z = c1z;
                    min1 = d2;
                    c1x = px; c1y = py; c1z = pz;
                    id1 = hash3u(cx, cy, cz, salt);
                }
                else if (d2 < min2)
                {
                    min2 = d2;
                    c2x = px; c2y = py; c2z = pz;
                }
            }
        }
    }

    const bx = c2x - c1x;
    const by = c2y - c1y;
    const bz = c2z - c1z;
    const bLen = Math.hypot(bx, by, bz);

    let wallDist = 1.0;
    if (bLen > 1e-5)
    {
        wallDist = (min2 - min1) / (2.0 * bLen);
        const inv = 1.0 / bLen;
        cleavageResult.wnx = bx * inv;
        cleavageResult.wny = by * inv;
        cleavageResult.wnz = bz * inv;
    }

    cleavageResult.wallDist = wallDist;
    cleavageResult.cellId = id1;
    cleavageResult.cellRand = ((id1 >>> 8) & 0xffff) / 32767.5 - 1.0;
    return cleavageResult;
}

/**
 * Pure-SDF Hierarchical 90-Degree Orthogonal T-Junction Thermal Crazing!
 *
 * Physics:
 * - Tier-1 master tensile joints form parallel sheets along local u-axis (`spacing = L1`), slightly warped in 3D.
 * - Between adjacent Tier-1 joints (slab integer index `k1`), residual tensile stress is parallel to the Tier-1 walls,
 *   so Tier-2 secondary cross-joints nucleate at **90 degrees** (`v`-axis) with an independent random phase `psi(k1)`
 *   inside each slab `k1` — therefore EVERY Tier-2 crack arrests cleanly at a 90-degree T-junction against Tier-1!
 * - Inside a fraction of `(k1, k2)` rectangular tiles, a Tier-3 orthogonal bisector splits the tile again at 90 degrees!
 *
 * Returns `{ dist, tier1Dist, tier2Dist, tileRand }` in metric world units.
 */
export const crazing90Result = {
    dist: 1e6,
    tier1Dist: 1e6,
    tier2Dist: 1e6,
    tileRand: 0.0
};

export function evalHierarchical90Crazing3D(u, v, w, spacing, salt = 0)
{
    const L1 = Math.max(0.08, spacing);
    const L2 = L1 * 0.92;

    // Gentle geological crookedness so cracks are natural brittle rock joints, not ruler lines
    const warpU = L1 * 0.14 * scalarField3D(u / (L1 * 1.7), v / (L1 * 1.7), w / (L1 * 1.7), salt + 3);
    const warpV = L2 * 0.13 * scalarField3D(u / (L2 * 1.6), v / (L2 * 1.6), w / (L2 * 1.6), salt + 19);

    const uCoord = (u + warpU) / L1;
    const k1 = Math.floor(uCoord);
    const fu = uCoord - k1; // [0, 1) inside Tier-1 slab k1
    const tier1Dist = Math.min(fu, 1.0 - fu) * L1;

    // Independent phase per Tier-1 slab k1 -> guarantees 90-degree T-junction arrest at fu = 0 and fu = 1!
    const slabPhase = hash31(k1, 17, 53, salt + 41);
    const slabScale = 0.82 + 0.36 * hash31(k1, 89, 13, salt + 67);
    const effL2 = L2 * slabScale;

    const vCoord = (v + warpV) / effL2 + slabPhase;
    const k2 = Math.floor(vCoord);
    const fv = vCoord - k2; // [0, 1) inside Tier-2 rectangle (k1, k2)
    let tier2Dist = Math.min(fv, 1.0 - fv) * effL2;

    // Optional Tier-3 90-degree sub-tile T-junction bisector inside ~58% of tiles (narrower aperture weight)
    const tileRand = hash31(k1, k2, 7, salt + 97);
    if (tileRand > 0.42)
    {
        const splitPos = 0.36 + 0.28 * hash31(k1, k2, 31, salt + 131);
        // Sub-crack runs parallel to Tier-1 (along v) inside this single (k1, k2) rectangle -> arrests at Tier-2 walls!
        const tier3Dist = Math.abs(fu - splitPos) * L1 * 1.28;
        if (tier3Dist < tier2Dist) tier2Dist = tier3Dist;
    }

    // Weight Tier-2 slightly above Tier-1 so Tier-2 is slightly finer than master Tier-1 cracks
    const combined = Math.min(tier1Dist, tier2Dist * 1.18);

    crazing90Result.dist = combined;
    crazing90Result.tier1Dist = tier1Dist;
    crazing90Result.tier2Dist = tier2Dist;
    crazing90Result.tileRand = tileRand;
    return crazing90Result;
}
