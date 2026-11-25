/*==========================================================================================================================================
                                                             SDFMATH.JS
==========================================================================================================================================*/
// Analytical 3D Signed Distance Field primitives, smooth CSG boolean operators, domain modifiers,
// IEEE-754 Float16 binary conversion (matching Frontier::VolumeDistanceArchive), quaternion math,
// and brush falloff curve evaluation. Shared 1:1 between CPU sculpting/raycasting and GLSL ES 3.00.

export const SHAPE_TYPES = [
    ['Sphere', 0],
    ['Rounded Box', 1],
    ['Capsule', 2],
    ['Cylinder', 3],
    ['Torus', 4],
    ['Capped Cone', 5],
    ['Octahedron', 6],
    ['Hex Prism', 7],
    ['Ellipsoid', 8],
    ['Chain Link', 9],
    ['Blade / Vesica', 10],
    ['Gyroid Lattice', 11]
];

export const SHAPE_NAMES = Object.fromEntries(SHAPE_TYPES.map(([Name, Id]) => [Id, Name]));

export const BOOL_OPS = [
    ['Smooth Union', 0],
    ['Smooth Subtract', 1],
    ['Smooth Intersect', 2],
    ['Chamfer Union', 3],
    ['Color Tint', 4]
];

export const BOOL_OP_NAMES = Object.fromEntries(BOOL_OPS.map(([Name, Id]) => [Id, Name]));

const _F32Scratch = new Float32Array(1);
const _U32Scratch = new Uint32Array(_F32Scratch.buffer);

/**
 * Exact IEEE-754 Float32 -> Float16 bit encoder matching Frontier::VolumeDistanceArchive::EncodeFloat16.
 */
export function EncodeFloat16(Value)
{
    _F32Scratch[0] = Value;
    const Bits = _U32Scratch[0];
    const Sign = (Bits >>> 16) & 0x8000;
    const Exp32 = ((Bits >>> 23) & 0xFF) - 127 + 15;
    let Mant = Bits & 0x007FFFFF;

    if (Exp32 <= 0)
    {
        if (Exp32 < -10) return Sign;
        Mant = (Mant | 0x00800000) >>> (1 - Exp32);
        return (Sign | ((Mant + 0x00001000) >>> 13)) & 0xFFFF;
    }
    if (Exp32 >= 31)
    {
        return (Sign | 0x7BFF) & 0xFFFF;
    }
    const Rounded = Mant + 0x00001000;
    if ((Rounded & 0x00800000) !== 0)
    {
        const NextExp = Exp32 + 1;
        return (NextExp >= 31 ? (Sign | 0x7BFF) : (Sign | (NextExp << 10))) & 0xFFFF;
    }
    return (Sign | (Exp32 << 10) | (Rounded >>> 13)) & 0xFFFF;
}

/**
 * Exact IEEE-754 Float16 -> Float32 bit decoder matching Frontier::VolumeDistanceArchive::DecodeFloat16.
 */
export function DecodeFloat16(HalfBits)
{
    const Sign = ((HalfBits & 0x8000) << 16) >>> 0;
    const Exp = (HalfBits >>> 10) & 0x1F;
    const Mant = HalfBits & 0x03FF;

    if (Exp === 0)
    {
        if (Mant === 0)
        {
            _U32Scratch[0] = Sign;
            return _F32Scratch[0];
        }
        let NormMant = Mant;
        let Exp32 = 127 - 15 + 1;
        while ((NormMant & 0x0400) === 0)
        {
            NormMant <<= 1;
            --Exp32;
        }
        NormMant &= 0x03FF;
        _U32Scratch[0] = (Sign | (Exp32 << 23) | (NormMant << 13)) >>> 0;
        return _F32Scratch[0];
    }
    if (Exp === 31)
    {
        _U32Scratch[0] = (Sign | 0x7F800000 | (Mant << 13)) >>> 0;
        return _F32Scratch[0];
    }
    _U32Scratch[0] = (Sign | ((Exp + 127 - 15) << 23) | (Mant << 13)) >>> 0;
    return _F32Scratch[0];
}

export const Clamp = (V, Lo, Hi) => (V < Lo ? Lo : V > Hi ? Hi : V);
export const Mix = (A, B, T) => A + (B - A) * T;

export const LinearToDisplay = Value => (Value <= 0.0031308 ? 12.92 * Value : 1.055 * Math.pow(Value, 1 / 2.4) - 0.055);
export const DisplayToLinear = Value => (Value <= 0.04045 ? Value / 12.92 : Math.pow((Value + 0.055) / 1.055, 2.4));

export function HexToLinearRgb(Hex)
{
    const Clean = String(Hex || '#b8785c').replace('#', '');
    const R = parseInt(Clean.slice(0, 2), 16) / 255;
    const G = parseInt(Clean.slice(2, 4), 16) / 255;
    const B = parseInt(Clean.slice(4, 6), 16) / 255;
    return [DisplayToLinear(R), DisplayToLinear(G), DisplayToLinear(B)];
}

export function LinearRgbToHex(Lin)
{
    const Byte = C => Math.round(Clamp(LinearToDisplay(Clamp(C, 0, 1)), 0, 1) * 255).toString(16).padStart(2, '0');
    return `#${Byte(Lin?.[0] ?? 0.7)}${Byte(Lin?.[1] ?? 0.5)}${Byte(Lin?.[2] ?? 0.4)}`;
}

export function Vec3Sub(A, B) { return [A[0] - B[0], A[1] - B[1], A[2] - B[2]]; }
export function Vec3Add(A, B) { return [A[0] + B[0], A[1] + B[1], A[2] + B[2]]; }
export function Vec3Scale(A, S) { return [A[0] * S, A[1] * S, A[2] * S]; }
export function Vec3Dot(A, B) { return A[0] * B[0] + A[1] * B[1] + A[2] * B[2]; }
export function Vec3Cross(A, B)
{
    return [
        A[1] * B[2] - A[2] * B[1],
        A[2] * B[0] - A[0] * B[2],
        A[0] * B[1] - A[1] * B[0]
    ];
}
export function Vec3Len(A) { return Math.hypot(A[0], A[1], A[2]); }
export function Vec3Normalize(A)
{
    const L = Math.hypot(A[0], A[1], A[2]);
    return L > 1e-8 ? [A[0] / L, A[1] / L, A[2] / L] : [0, 1, 0];
}

/**
 * Converts ZYX intrinsic / XYZ extrinsic Euler degrees [rx, ry, rz] to unit quaternion [qx, qy, qz, qw].
 */
export function EulerDegreesToQuat(EulerDeg)
{
    const D2R = Math.PI / 180;
    const Hx = ((EulerDeg?.[0] || 0) * D2R) * 0.5;
    const Hy = ((EulerDeg?.[1] || 0) * D2R) * 0.5;
    const Hz = ((EulerDeg?.[2] || 0) * D2R) * 0.5;
    const Cx = Math.cos(Hx), Sx = Math.sin(Hx);
    const Cy = Math.cos(Hy), Sy = Math.sin(Hy);
    const Cz = Math.cos(Hz), Sz = Math.sin(Hz);

    return [
        Sx * Cy * Cz - Cx * Sy * Sz,
        Cx * Sy * Cz + Sx * Cy * Sz,
        Cx * Cy * Sz - Sx * Sy * Cz,
        Cx * Cy * Cz + Sx * Sy * Sz
    ];
}

export function InvertUnitQuat(Q)
{
    return [-Q[0], -Q[1], -Q[2], Q[3]];
}

export function RotateByQuat(V, Q)
{
    const Qx = Q[0], Qy = Q[1], Qz = Q[2], Qw = Q[3];
    const Tx = 2.0 * (Qy * V[2] - Qz * V[1]);
    const Ty = 2.0 * (Qz * V[0] - Qx * V[2]);
    const Tz = 2.0 * (Qx * V[1] - Qy * V[0]);
    return [
        V[0] + Qw * Tx + (Qy * Tz - Qz * Ty),
        V[1] + Qw * Ty + (Qz * Tx - Qx * Tz),
        V[2] + Qw * Tz + (Qx * Ty - Qy * Tx)
    ];
}

/**
 * Analytical 3D SDF primitive evaluation in local space (matching GLSL sdPrimitive 1:1).
 */
export function EvaluatePrimitiveLocal(ShapeId, Px, Py, Pz, Sx, Sy, Sz)
{
    const Ax = Math.abs(Px), Ay = Math.abs(Py), Az = Math.abs(Pz);
    switch (ShapeId | 0)
    {
        case 0: // Sphere (radius = Sx)
            return Math.hypot(Px, Py, Pz) - Math.max(0.01, Sx);

        case 1: // Box (half-extents Sx, Sy, Sz)
        {
            const Qx = Ax - Math.max(0.005, Sx);
            const Qy = Ay - Math.max(0.005, Sy);
            const Qz = Az - Math.max(0.005, Sz);
            return Math.hypot(Math.max(Qx, 0), Math.max(Qy, 0), Math.max(Qz, 0)) +
                   Math.min(Math.max(Qx, Math.max(Qy, Qz)), 0);
        }

        case 2: // Capsule (radius Sx, half-segment Sy)
        {
            const H = Math.max(0.001, Sy);
            const Cy = Py - Clamp(Py, -H, H);
            return Math.hypot(Px, Cy, Pz) - Math.max(0.005, Sx);
        }

        case 3: // Capped Cylinder (radius Sx, half-height Sy)
        {
            const Dr = Math.hypot(Px, Pz) - Math.max(0.005, Sx);
            const Dy = Ay - Math.max(0.005, Sy);
            return Math.min(Math.max(Dr, Dy), 0) + Math.hypot(Math.max(Dr, 0), Math.max(Dy, 0));
        }

        case 4: // Torus (major radius Sx, minor radius Sy in XZ plane)
        {
            const Qx = Math.hypot(Px, Pz) - Math.max(0.01, Sx);
            return Math.hypot(Qx, Py) - Math.max(0.005, Sy);
        }

        case 5: // Capped Cone (base radius Sx, half-height Sy, top radius Sz)
        {
            const H = Math.max(0.01, Sy);
            const R1 = Math.max(0.005, Sx);
            const R2 = Math.max(0.002, Sz);
            const Qx = Math.hypot(Px, Pz);
            const Qy = Py;
            const K1x = R2, K1y = H;
            const K2x = R2 - R1, K2y = 2.0 * H;
            const Cax = Qx - Math.min(Qx, Qy < 0.0 ? R1 : R2);
            const Cay = Math.abs(Qy) - H;
            const DotK2 = K2x * K2x + K2y * K2y;
            const T = Clamp(((K1x - Qx) * K2x + (K1y - Qy) * K2y) / Math.max(1e-6, DotK2), 0.0, 1.0);
            const Cbx = Qx - K1x + K2x * T;
            const Cby = Qy - K1y + K2y * T;
            const S = (Cbx < 0.0 && Cay < 0.0) ? -1.0 : 1.0;
            return S * Math.sqrt(Math.min(Cax * Cax + Cay * Cay, Cbx * Cbx + Cby * Cby));
        }

        case 6: // Octahedron (size Sx)
        {
            const S = Math.max(0.01, Sx);
            let X = Ax, Y = Ay, Z = Az;
            const M = X + Y + Z - S;
            let Qx, Qy, Qz;
            if (3.0 * X < M) { Qx = X; Qy = Y; Qz = Z; }
            else if (3.0 * Y < M) { Qx = Y; Qy = Z; Qz = X; }
            else if (3.0 * Z < M) { Qx = Z; Qy = X; Qz = Y; }
            else return M * 0.57735027;
            const K = Clamp(0.5 * (Qz - Qy + S), 0.0, S);
            return Math.hypot(Qx, Qy - S + K, Qz - K);
        }

        case 7: // Hexagonal Prism (hex radius Sx, half-height Sy along Y)
        {
            const Kx = -0.8660254, Ky = 0.5, Kz = 0.57735027;
            let X = Ax, Z = Az;
            const Dot = 2.0 * Math.min(Kx * X + Ky * Z, 0.0);
            X -= Dot * Kx;
            Z -= Dot * Ky;
            const Hx = Math.max(0.01, Sx);
            const Hy = Math.max(0.005, Sy);
            const Dx = Math.hypot(X - Clamp(X, -Kz * Hx, Kz * Hx), Z - Hx) * Math.sign(Z - Hx);
            const Dy = Ay - Hy;
            return Math.min(Math.max(Dx, Dy), 0.0) + Math.hypot(Math.max(Dx, 0.0), Math.max(Dy, 0.0));
        }

        case 8: // Ellipsoid (radii Sx, Sy, Sz)
        {
            const Rx = Math.max(0.01, Sx), Ry = Math.max(0.01, Sy), Rz = Math.max(0.01, Sz);
            const K0 = Math.hypot(Px / Rx, Py / Ry, Pz / Rz);
            if (K0 < 1e-6) return -Math.min(Rx, Math.min(Ry, Rz));
            const K1 = Math.hypot(Px / (Rx * Rx), Py / (Ry * Ry), Pz / (Rz * Rz));
            return K0 * (K0 - 1.0) / Math.max(1e-6, K1);
        }

        case 9: // Chain Link (half-length Sy, major radius Sx, tube radius Sz)
        {
            const Le = Math.max(0.0, Sy);
            const R1 = Math.max(0.01, Sx);
            const R2 = Math.max(0.005, Sz);
            const Qy = Math.max(Ay - Le, 0.0);
            return Math.hypot(Math.hypot(Px, Qy) - R1, Pz) - R2;
        }

        case 10: // Blade / Extruded Vesica (radius Sx, half-height Sy, width Sz)
        {
            const R = Math.max(0.02, Sx);
            const W = Clamp(Sz, 0.005, R * 0.95);
            const D = R - W;
            const Vx = Ax, Vz = Az;
            const B = Math.sqrt(Math.max(1e-6, R * R - D * D));
            const Dist2D = ((Vz - B) * D > Vx * B)
                ? Math.hypot(Vx, Vz - B)
                : Math.hypot(Vx + D, Vz) - R;
            const Dy = Ay - Math.max(0.005, Sy);
            return Math.min(Math.max(Dist2D, Dy), 0.0) + Math.hypot(Math.max(Dist2D, 0.0), Math.max(Dy, 0.0));
        }

        case 11: // Gyroid TPMS Lattice Block (box Sx, Sy, Sz, frequency driven by Sz/round)
        default:
        {
            const BoxQx = Ax - Math.max(0.02, Sx);
            const BoxQy = Ay - Math.max(0.02, Sy);
            const BoxQz = Az - Math.max(0.02, Sz);
            const BoxD = Math.hypot(Math.max(BoxQx, 0), Math.max(BoxQy, 0), Math.max(BoxQz, 0)) +
                         Math.min(Math.max(BoxQx, Math.max(BoxQy, BoxQz)), 0);
            const Freq = 9.5;
            const G = Math.sin(Px * Freq) * Math.cos(Py * Freq) +
                      Math.sin(Py * Freq) * Math.cos(Pz * Freq) +
                      Math.sin(Pz * Freq) * Math.cos(Px * Freq);
            const GyroidD = (Math.abs(G) / Freq - 0.042) * 0.75;
            return Math.max(BoxD, GyroidD);
        }
    }
}

/**
 * Evaluates a single Parametric CSG Node at world point [Wx, Wy, Wz], matching the GLSL evalCsgNode shader function.
 */
export function EvaluateCsgNode(Node, Wx, Wy, Wz)
{
    let Px = Wx, Py = Wy, Pz = Wz;
    if (Node.mirrorX) Px = Math.abs(Px);
    if (Node.mirrorZ) Pz = Math.abs(Pz);
    if ((Node.radialCount | 0) >= 2)
    {
        const N = Node.radialCount | 0;
        const Sector = (2.0 * Math.PI) / N;
        let Angle = Math.atan2(Pz, Px);
        Angle = ((Angle + Sector * 0.5) % Sector + Sector) % Sector - Sector * 0.5;
        const Rad = Math.hypot(Px, Pz);
        Px = Math.cos(Angle) * Rad;
        Pz = Math.sin(Angle) * Rad;
    }

    const Pos = Node.position || [0, 0, 0];
    let Lx = Px - Pos[0];
    let Ly = Py - Pos[1];
    let Lz = Pz - Pos[2];

    const InvQ = Node._invQuat || InvertUnitQuat(EulerDegreesToQuat(Node.rotation || [0, 0, 0]));
    const Rot = RotateByQuat([Lx, Ly, Lz], InvQ);
    const Scale = Math.max(0.05, Number(Node.scale) || 1.0);
    Lx = Rot[0] / Scale;
    Ly = Rot[1] / Scale;
    Lz = Rot[2] / Scale;

    const Twist = Number(Node.twist) || 0;
    if (Math.abs(Twist) > 1e-4)
    {
        const C = Math.cos(Twist * Ly);
        const S = Math.sin(Twist * Ly);
        const Nx = C * Lx - S * Lz;
        const Nz = S * Lx + C * Lz;
        Lx = Nx;
        Lz = Nz;
    }

    const Sz = Node.size || [0.35, 0.35, 0.35];
    let Dist = EvaluatePrimitiveLocal(Node.shape ?? 0, Lx, Ly, Lz, Sz[0], Sz[1], Sz[2]);
    if (Math.abs(Twist) > 1e-4) Dist *= 0.82; // Lipschitz safety factor under twist

    const RoundR = Math.max(0, Number(Node.round) || 0);
    Dist -= RoundR;

    const Shell = Math.max(0, Number(Node.shell) || 0);
    if (Shell > 1e-4)
    {
        Dist = Math.abs(Dist) - Shell;
    }

    return Dist * Scale;
}

/**
 * Combines accumulator [d1, r1, g1, b1] with incoming node (d2, color2) using boolean op and smooth radius k.
 * Mutates Out4 = [d, r, g, b] in place to avoid GC pressure inside tight loops.
 */
export function CombineBooleanInto(Out4, D2, Col2, OpId, K)
{
    const D1 = Out4[0];
    const R1 = Out4[1], G1 = Out4[2], B1 = Out4[3];
    const R2 = Col2[0], G2 = Col2[1], B2 = Col2[2];
    const SmoothK = Math.max(1e-4, K);

    switch (OpId | 0)
    {
        case 0: // Smooth Union
        {
            const H = Clamp(0.5 + 0.5 * (D2 - D1) / SmoothK, 0.0, 1.0);
            Out4[0] = Mix(D2, D1, H) - SmoothK * H * (1.0 - H);
            const Cm = 1.0 - H;
            Out4[1] = Mix(R1, R2, Cm);
            Out4[2] = Mix(G1, G2, Cm);
            Out4[3] = Mix(B1, B2, Cm);
            return;
        }
        case 1: // Smooth Subtract (carves D2 from D1)
        {
            const H = Clamp(0.5 - 0.5 * (D1 + D2) / SmoothK, 0.0, 1.0);
            Out4[0] = Mix(D1, -D2, H) + SmoothK * H * (1.0 - H);
            Out4[1] = Mix(R1, R2, H * 0.88);
            Out4[2] = Mix(G1, G2, H * 0.88);
            Out4[3] = Mix(B1, B2, H * 0.88);
            return;
        }
        case 2: // Smooth Intersect
        {
            const H = Clamp(0.5 - 0.5 * (D2 - D1) / SmoothK, 0.0, 1.0);
            Out4[0] = Mix(D2, D1, H) + SmoothK * H * (1.0 - H);
            Out4[1] = Mix(R2, R1, H);
            Out4[2] = Mix(G2, G1, H);
            Out4[3] = Mix(B2, B1, H);
            return;
        }
        case 3: // Chamfer Union
        {
            const Ch = (D1 - SmoothK + D2) * 0.70710678;
            const MinAb = Math.min(D1, D2);
            Out4[0] = Math.min(MinAb, Ch);
            const H = Clamp(0.5 + 0.5 * (D2 - D1) / SmoothK, 0.0, 1.0);
            Out4[1] = Mix(R2, R1, H);
            Out4[2] = Mix(G2, G1, H);
            Out4[3] = Mix(B2, B1, H);
            return;
        }
        case 4: // Surface Color Tint (non-geometric paint volume)
        default:
        {
            const Influence = 1.0 - Clamp((D2 + SmoothK * 0.25) / SmoothK, 0.0, 1.0);
            Out4[1] = Mix(R1, R2, Influence);
            Out4[2] = Mix(G1, G2, Influence);
            Out4[3] = Mix(B1, B2, Influence);
            return;
        }
    }
}

/**
 * Packs up to 24 active CSG nodes into a 120*4 Float32Array (5 vec4s per node) for GLSL ES 3.00 uniform upload.
 */
export const MAX_CSG_NODES = 24;

export function PackCsgUniformArray(Nodes, OutFloat32 = new Float32Array(MAX_CSG_NODES * 20))
{
    OutFloat32.fill(0);
    let Count = 0;
    for (let I = 0; I < Nodes.length && Count < MAX_CSG_NODES; ++I)
    {
        const Node = Nodes[I];
        if (!Node || Node.enabled === false) continue;
        Node._invQuat = InvertUnitQuat(EulerDegreesToQuat(Node.rotation || [0, 0, 0]));

        const Base = Count * 20;
        const Pos = Node.position || [0, 0, 0];
        const Sz = Node.size || [0.35, 0.35, 0.35];
        const Col = Node.color || [0.72, 0.56, 0.44];
        const Q = Node._invQuat;

        // vec4 d0: pos.xyz, shapeId
        OutFloat32[Base + 0] = Pos[0];
        OutFloat32[Base + 1] = Pos[1];
        OutFloat32[Base + 2] = Pos[2];
        OutFloat32[Base + 3] = Node.shape ?? 0;

        // vec4 d1: size.xyz, smoothBlendK
        OutFloat32[Base + 4] = Math.max(0.005, Sz[0]);
        OutFloat32[Base + 5] = Math.max(0.005, Sz[1]);
        OutFloat32[Base + 6] = Math.max(0.005, Sz[2]);
        OutFloat32[Base + 7] = Math.max(0.001, Number(Node.blend) || 0.06);

        // vec4 d2: invQuat.xyzw
        OutFloat32[Base + 8] = Q[0];
        OutFloat32[Base + 9] = Q[1];
        OutFloat32[Base + 10] = Q[2];
        OutFloat32[Base + 11] = Q[3];

        // vec4 d3: linearAlbedo.rgb, opId + selectedHighlightFlag
        OutFloat32[Base + 12] = Col[0];
        OutFloat32[Base + 13] = Col[1];
        OutFloat32[Base + 14] = Col[2];
        OutFloat32[Base + 15] = Node.op ?? 0;

        // vec4 d4: round, shell, twist, packedDomainFlags (scale in integer/frac or separate)
        // Encode mirrorFlags + radialCount cleanly: flags = (mirrorX?1:0) + (mirrorZ?2:0) + radialCount*4
        const Radial = Clamp(Node.radialCount | 0, 1, 16);
        const Flags = (Node.mirrorX ? 1 : 0) + (Node.mirrorZ ? 2 : 0) + Radial * 4;
        const Scale = Clamp(Number(Node.scale) || 1.0, 0.05, 8.0);
        OutFloat32[Base + 16] = Math.max(0, Number(Node.round) || 0);
        OutFloat32[Base + 17] = Math.max(0, Number(Node.shell) || 0);
        OutFloat32[Base + 18] = (Number(Node.twist) || 0) + Math.round(Flags) * 1000.0 * 0.0; // keep twist pure
        // Pack Flags as integer in [Base+19] floor and Scale in fractional, or encode cleanly:
        // Flags is integer 4..67, Scale*0.01 in (0.0005..0.08) -> Flags + Scale * 0.01!
        // Wait: even cleaner and exact to 5 decimals: Scale <= 8.0 -> Flags * 16.0 + Scale!
        // Since Flags is integer >= 4 and Scale in [0.05, 8.0], floor(val / 16.0) == Flags, mod(val, 16.0) == Scale!
        OutFloat32[Base + 18] = Number(Node.twist) || 0;
        OutFloat32[Base + 19] = Flags * 16.0 + Scale;

        Count++;
    }
    return { Buffer: OutFloat32, Count };
}

/**
 * Built-in & Custom Brush Falloff Presets.
 * Maps normalized distance r in [0..1] (0 = brush center, 1 = outer radius) to weight in [0..1].
 */
export const FALLOFF_PRESETS = {
    smooth: {
        Label: 'Smooth Cosine',
        Points: [[0, 1], [0.35, 0.82], [0.7, 0.28], [1, 0]]
    },
    plateau: {
        Label: 'Clay Plateau',
        Points: [[0, 1], [0.58, 0.96], [0.82, 0.42], [1, 0]]
    },
    sharp: {
        Label: 'Sharp Crease',
        Points: [[0, 1], [0.22, 0.46], [0.55, 0.12], [1, 0]]
    },
    linear: {
        Label: 'Linear Cone',
        Points: [[0, 1], [0.33, 0.67], [0.67, 0.33], [1, 0]]
    },
    sphere: {
        Label: 'Spherical Dome',
        Points: [[0, 1], [0.45, 0.89], [0.8, 0.6], [1, 0]]
    }
};

/**
 * Evaluates brush falloff curve at normalized radius r in [0..1], supporting analytic modes and custom Hermite points.
 */
export function EvaluateFalloff(NormalizedR, CurveState)
{
    if (NormalizedR >= 1.0) return 0.0;
    if (NormalizedR <= 0.0) return CurveState?.Points?.[0]?.[1] ?? 1.0;

    const Mode = CurveState?.Mode || 'smooth';
    if (Mode === 'smooth' && !CurveState?.Custom)
    {
        const U = 1.0 - NormalizedR * NormalizedR;
        return U * U;
    }
    if (Mode === 'plateau' && !CurveState?.Custom)
    {
        if (NormalizedR < 0.52) return 1.0;
        const T = (NormalizedR - 0.52) / 0.48;
        const S = 1.0 - T * T * (3.0 - 2.0 * T);
        return Clamp(S, 0.0, 1.0);
    }
    if (Mode === 'sharp' && !CurveState?.Custom)
    {
        const U = 1.0 - NormalizedR;
        return U * U * U;
    }
    if (Mode === 'linear' && !CurveState?.Custom)
    {
        return 1.0 - NormalizedR;
    }
    if (Mode === 'sphere' && !CurveState?.Custom)
    {
        return Math.sqrt(Math.max(0.0, 1.0 - NormalizedR * NormalizedR));
    }

    const Pts = CurveState?.Points || FALLOFF_PRESETS.smooth.Points;
    if (!Pts || Pts.length < 2) return 1.0 - NormalizedR;

    for (let I = 0; I < Pts.length - 1; ++I)
    {
        const P0 = Pts[I], P1 = Pts[I + 1];
        if (NormalizedR <= P1[0] || I === Pts.length - 2)
        {
            const Span = Math.max(1e-5, P1[0] - P0[0]);
            const T = Clamp((NormalizedR - P0[0]) / Span, 0.0, 1.0);
            const SmoothT = (CurveState?.Interpolation === 'linear') ? T : T * T * (3.0 - 2.0 * T);
            return Clamp(Mix(P0[1], P1[1], SmoothT), 0.0, 1.0);
        }
    }
    return 0.0;
}

/**
 * Generates symmetry-mirrored/rotated stroke transforms { Pos, Dir, Normal } for active symmetry settings.
 */
export function ExpandSymmetryTargets(Pos, Dir, Normal, Symmetry)
{
    let Targets = [{ Pos: Pos.slice(), Dir: Dir ? Dir.slice() : [0, 0, 0], Normal: Normal ? Normal.slice() : [0, 1, 0] }];
    if (!Symmetry) return Targets;

    const MirrorAxis = (List, AxisIdx) =>
    {
        const Out = [];
        for (const Item of List)
        {
            Out.push(Item);
            if (Math.abs(Item.Pos[AxisIdx]) > 1e-4)
            {
                const P2 = Item.Pos.slice(); P2[AxisIdx] = -P2[AxisIdx];
                const D2 = Item.Dir.slice(); D2[AxisIdx] = -D2[AxisIdx];
                const N2 = Item.Normal.slice(); N2[AxisIdx] = -N2[AxisIdx];
                Out.push({ Pos: P2, Dir: D2, Normal: N2 });
            }
        }
        return Out;
    };

    if (Symmetry.x) Targets = MirrorAxis(Targets, 0);
    if (Symmetry.y) Targets = MirrorAxis(Targets, 1);
    if (Symmetry.z) Targets = MirrorAxis(Targets, 2);

    const Radial = Symmetry.radial | 0;
    if (Radial >= 2)
    {
        const Expanded = [];
        for (const Item of Targets)
        {
            Expanded.push(Item);
            for (let K = 1; K < Radial; ++K)
            {
                const Angle = (2.0 * Math.PI * K) / Radial;
                const C = Math.cos(Angle), S = Math.sin(Angle);
                const RotY = V => [C * V[0] - S * V[2], V[1], S * V[0] + C * V[2]];
                Expanded.push({
                    Pos: RotY(Item.Pos),
                    Dir: RotY(Item.Dir),
                    Normal: RotY(Item.Normal)
                });
            }
        }
        Targets = Expanded;
    }
    return Targets;
}
