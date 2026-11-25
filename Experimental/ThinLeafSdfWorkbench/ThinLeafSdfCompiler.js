/*============================================================================================================================================
                                                      THINLEAFSDFCOMPILER.JS
============================================================================================================================================*/
// 🧩 Compiles 5 botanical thin-leaf archetypes (Beech, Linden, Oak, Maple, Willow) into exact 64x64 2D Euclidean Signed Distance Grids,
//    populates instanced curved leaves onto wooden branch capsules, and builds both:
//      ① 32x32x32 3D Voxel SDF volumes (exposing Standard Signed Zero-Crossing Erasure vs. Dilated Two-Sided Blob Bloat), and
//      ② The Two-Level 16x16x16 Canopy Brick + 2.5D Signed-Sheet Extrusion SDF with automatic Cone-Width Beer-Lambert Optical-Depth LOD.

export const LeafArchetypeCount   = 5;
export const LeafSdf2dRes         = 64;     // [px] - 64x64 2D Signed Distance Grid per leaf archetype (20 KB total for all 5!)
export const MacroVoxelRes        = 32;     // [cells] - 32^3 3D Voxel SDF resolution for canopy hull + leaf voxel comparison modes
export const CanopyBrickRes       = 16;     // [bricks] - 16^3 macro brick accelerator for leaf instance lists & 3-axis optical depth
export const LeafHalfThicknessM   = 0.0006; // [m] - 0.6 mm half-thickness (1.2 mm total physical leaf blade thickness!)
export const DefaultBarkNoiseAmp  = 0.65;   // [0..1] - 3D procedural bark furrow & woody burl radial displacement strength
export const DefaultBarkNoiseFreq = 9.5;    // [1/m]  - Spatial frequency of 3D procedural bark ridges & burls
export const DefaultBranchGnarl   = 0.55;   // [0..1] - Organic 3D centerline gnarl curvature along articulated capsule limbs
export const BarkDispMaxFrac      = 0.18;   // Max fraction of local capsule radius displaced by 3D bark FBM (radius stays >= 0.82*R!)

export const SdfEvaluationMode = Object.freeze({
    StandardVoxel3d:    0, // 🔴 Standard 32^3 Signed Voxel SDF -> thin leaves vanish (all 8 voxel corners > 0!)
    DilatedVoxel3d:     1, // 🚩 Two-Sided Dilated 32^3 Voxel SDF (u - 0.55*dx) -> huge dark "meatball" blob shadows!
    OpacityUdf3d:       2, // 🏷️ Non-Instanced 32^3 Unsigned Distance + Sub-Voxel Opacity -> soft canopy, blurred silhouettes
    TwoLevelThinSheet:  3  // 🟢 Two-Level Canopy Brick + 5 x 2D Thin-Sheet Extrusion SDF + Cone-LOD -> Unconditional!
});

export const LeafArchetypeNames = Object.freeze([
    "0 · Ovate Beech",
    "1 · Heart Linden",
    "2 · Lobed Oak",
    "3 · 5-Point Maple",
    "4 · Slender Willow"
]);

function Clamp(X, Lo, Hi)
{
    return X < Lo ? Lo : (X > Hi ? Hi : X);
}

function Normalize3(V)
{
    const L = Math.hypot(V[0], V[1], V[2]) || 1.0;
    return [V[0] / L, V[1] / L, V[2] / L];
}

function Cross3(A, B)
{
    return [
        A[1] * B[2] - A[2] * B[1],
        A[2] * B[0] - A[0] * B[2],
        A[0] * B[1] - A[1] * B[0]
    ];
}

function Dot3(A, B)
{
    return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}

// 📝 IEEE-754 float32 -> float16 bit encoder for WebGPU rgba16float 3D & 2D-Array filterable textures
export function EncodeFloat16Bits(Val)
{
    if (!Number.isFinite(Val)) return Val > 0 ? 0x7c00 : (Val < 0 ? 0xfc00 : 0);
    const Sign = Val < 0 || Object.is(Val, -0) ? 0x8000 : 0;
    const AbsV = Math.abs(Val);
    if (AbsV < 5.96046448e-8) return Sign;
    if (AbsV > 65504.0) return Sign | 0x7bff;

    const Exp = Math.floor(Math.log2(AbsV));
    if (Exp < -14)
    {
        return Sign | Math.round(AbsV / 5.96046448e-8);
    }
    const Mant = Math.round((AbsV / Math.pow(2.0, Exp) - 1.0) * 1024.0);
    if (Mant >= 1024)
    {
        return Sign | (((Exp + 16) & 0x1f) << 10);
    }
    return Sign | (((Exp + 15) & 0x1f) << 10) | (Mant & 0x3ff);
}

//============================================================================================================================================
//                                     1. EXACT 5 BOTANICAL 2D LEAF SILHOUETTE SDF TEMPLATES (64x64)
//============================================================================================================================================

// Evaluates exact boolean occupancy (true = inside leaf blade or petiole stem) on local [-1, +1]^2 coordinates (X = width, Z = length)
export function TestLeafArchetypeInside(ArchetypeId, X, Z)
{
    const AbsX = Math.abs(X);

    // Petiole stem shared at petiole root
    if (ArchetypeId !== 4 && Z >= -0.96 && Z <= -0.56 && AbsX <= 0.034)
    {
        return true;
    }

    if (ArchetypeId === 0)
    {
        // 0 · Ovate Beech: tapered elliptic blade with crisp marginal serrations
        const T = (Z + 0.64) / 1.56;
        if (T < 0.0 || T > 1.0) return false;
        const Env = 0.62 * Math.sin(Math.PI * Math.pow(T, 0.78)) * (1.0 - 0.18 * T);
        const Teeth = 0.042 * Math.sin(T * Math.PI * 13.0) * Math.sin(Math.PI * T);
        return AbsX <= Math.max(0.0, Env + Teeth);
    }

    if (ArchetypeId === 1)
    {
        // 1 · Cordate Heart Linden: basal heart notch + broad rounded shoulders + acuminate drip-tip
        const Dz = Z + 0.22;
        const R = Math.hypot(AbsX * 1.12, Dz);
        if (R < 1e-5) return true;
        const Theta = Math.atan2(AbsX * 1.12, Dz); // 0 at +Z tip, pi at -Z notch
        const HeartRad = 0.72 * (1.0 + 0.26 * Math.cos(Theta) - 0.42 * Math.pow(Math.sin(0.5 * Theta), 3.0))
            + 0.055 * Math.exp(-Math.pow(Theta / 0.24, 2.0));
        const Teeth = 0.024 * Math.sin(Theta * 18.0) * Math.sin(Theta);
        return R <= HeartRad + Teeth && Z <= 0.94 && Z >= -0.78;
    }

    if (ArchetypeId === 2)
    {
        // 2 · Lobed Oak (Quercus): 4 unmistakable deep rounded lobes per side!
        const T = (Z + 0.66) / 1.58;
        if (T < 0.0 || T > 1.0) return false;
        const Taper = Math.sin(Math.PI * Math.pow(T, 0.72));
        const LobeWave = 0.56 + 0.44 * Math.cos((T - 0.14) * Math.PI * 7.2);
        const HalfW = 0.68 * Taper * LobeWave;
        return AbsX <= Math.max(0.03 * (1.0 - T), HalfW);
    }

    if (ArchetypeId === 3)
    {
        // 3 · 5-Point Palmate Maple (Acer): star silhouette with 5 radiating pointed lobes!
        const Ox = AbsX;
        const Oz = Z + 0.42;
        const R = Math.hypot(Ox, Oz);
        if (R > 1.38 || Z < -0.76) return false;
        const Ang = Math.atan2(Ox, Oz); // [0 .. pi]
        const LobeAngles = [0.0, 0.72, 1.46];
        const LobeLengths = [1.28, 1.08, 0.70];
        const LobeSharp = [0.27, 0.25, 0.26];
        let MaxReach = 0.22;
        for (let L = 0; L < 3; ++L)
        {
            const Da = (Ang - LobeAngles[L]) / LobeSharp[L];
            const SubTeeth = 1.0 + 0.07 * Math.cos(Da * 5.2);
            const Reach = LobeLengths[L] * Math.exp(-0.55 * Da * Da) * SubTeeth;
            if (Reach > MaxReach) MaxReach = Reach;
        }
        return R <= MaxReach;
    }

    // 4 · Slender Lanceolate Willow (Salix): graceful narrow curved spear blade
    if (Z >= -0.96 && Z <= -0.74 && Math.abs(X + 0.02) <= 0.025) return true;
    const T = (Z + 0.78) / 1.72;
    if (T < 0.0 || T > 1.0) return false;
    const CenterCurve = 0.07 * Math.sin(T * Math.PI * 1.15);
    const HalfW = 0.22 * Math.sin(Math.PI * Math.pow(T, 0.80));
    return Math.abs(X - CenterCurve) <= HalfW;
}

// Exact O(N^2) Felzenszwalb-Huttenlocher 2D Squared Euclidean Distance Transform
function ComputeExact2dEdt(BinaryGrid, Res, TargetInsideBool)
{
    const Inf = 1e12;
    const F = new Float64Array(Res * Res);
    for (let I = 0; I < Res * Res; ++I)
    {
        F[I] = (Boolean(BinaryGrid[I]) === TargetInsideBool) ? 0.0 : Inf;
    }

    const V = new Int32Array(Res);
    const Z = new Float64Array(Res + 1);
    const Buf = new Float64Array(Res);

    function Transform1d(LineIn, LineOut)
    {
        let K = 0;
        V[0] = 0;
        Z[0] = -Inf;
        Z[1] = +Inf;
        for (let Q = 1; Q < Res; ++Q)
        {
            let S = ((LineIn[Q] + Q * Q) - (LineIn[V[K]] + V[K] * V[K])) / (2.0 * Q - 2.0 * V[K]);
            while (S <= Z[K])
            {
                --K;
                S = ((LineIn[Q] + Q * Q) - (LineIn[V[K]] + V[K] * V[K])) / (2.0 * Q - 2.0 * V[K]);
            }
            ++K;
            V[K] = Q;
            Z[K] = S;
            Z[K + 1] = +Inf;
        }
        K = 0;
        for (let Q = 0; Q < Res; ++Q)
        {
            while (Z[K + 1] < Q) ++K;
            const Diff = Q - V[K];
            LineOut[Q] = Diff * Diff + LineIn[V[K]];
        }
    }

    const Temp = new Float64Array(Res * Res);
    const ColIn = new Float64Array(Res);
    const ColOut = new Float64Array(Res);

    // Rows
    for (let Y = 0; Y < Res; ++Y)
    {
        const RowOff = Y * Res;
        for (let X = 0; X < Res; ++X) Buf[X] = F[RowOff + X];
        Transform1d(Buf, ColOut);
        for (let X = 0; X < Res; ++X) Temp[RowOff + X] = ColOut[X];
    }
    // Columns
    const OutSq = new Float64Array(Res * Res);
    for (let X = 0; X < Res; ++X)
    {
        for (let Y = 0; Y < Res; ++Y) ColIn[Y] = Temp[Y * Res + X];
        Transform1d(ColIn, ColOut);
        for (let Y = 0; Y < Res; ++Y) OutSq[Y * Res + X] = ColOut[Y];
    }
    return OutSq;
}

// 📝 Compiles all 5 botanical leaf archetypes into exact 64x64 2D Signed Distance Grids (supersampled at 256x256!)
//    Returns { FloatGrids: Float32Array[5], Rgba16fTexels: Uint16Array, AreaFractions: number[5] }
export function CompileFiveLeafArchetypeSdfs()
{
    const SuperRes = 256;
    const PixelWorldSpan = 2.0 / SuperRes; // Local [-1, +1] span = 2.0 units
    const FloatGrids = [];
    const AreaFractions = [];
    const Rgba16fTexels = new Uint16Array(LeafArchetypeCount * LeafSdf2dRes * LeafSdf2dRes * 4);

    for (let Arch = 0; Arch < LeafArchetypeCount; ++Arch)
    {
        const Mask = new Uint8Array(SuperRes * SuperRes);
        let InsideCount = 0;
        for (let Y = 0; Y < SuperRes; ++Y)
        {
            const LocalZ = -1.0 + ((Y + 0.5) / SuperRes) * 2.0;
            for (let X = 0; X < SuperRes; ++X)
            {
                const LocalX = -1.0 + ((X + 0.5) / SuperRes) * 2.0;
                const Hit = TestLeafArchetypeInside(Arch, LocalX, LocalZ);
                Mask[Y * SuperRes + X] = Hit ? 1 : 0;
                if (Hit) ++InsideCount;
            }
        }
        AreaFractions.push(InsideCount / (SuperRes * SuperRes));

        const DistToInsideSq  = ComputeExact2dEdt(Mask, SuperRes, true);
        const DistToOutsideSq = ComputeExact2dEdt(Mask, SuperRes, false);

        const Grid64 = new Float32Array(LeafSdf2dRes * LeafSdf2dRes);
        const Ratio = SuperRes / LeafSdf2dRes; // 4x4 block average of exact signed distance

        for (let Gy = 0; Gy < LeafSdf2dRes; ++Gy)
        {
            for (let Gx = 0; Gx < LeafSdf2dRes; ++Gx)
            {
                let SumSdf = 0.0;
                for (let Sy = 0; Sy < Ratio; ++Sy)
                {
                    for (let Sx = 0; Sx < Ratio; ++Sx)
                    {
                        const Idx = (Gy * Ratio + Sy) * SuperRes + (Gx * Ratio + Sx);
                        const SignedPx = Mask[Idx]
                            ? -(Math.sqrt(DistToOutsideSq[Idx]) - 0.5)
                            : +(Math.sqrt(DistToInsideSq[Idx]) - 0.5);
                        SumSdf += SignedPx * PixelWorldSpan;
                    }
                }
                const MeanSdf = SumSdf / (Ratio * Ratio);
                Grid64[Gy * LeafSdf2dRes + Gx] = MeanSdf;
            }
        }

        // Compute finite-difference 2D gradient into G, B channels for crisp rim/vein shading too!
        for (let Gy = 0; Gy < LeafSdf2dRes; ++Gy)
        {
            for (let Gx = 0; Gx < LeafSdf2dRes; ++Gx)
            {
                const Sd = Grid64[Gy * LeafSdf2dRes + Gx];
                const XPrev = Grid64[Gy * LeafSdf2dRes + Math.max(0, Gx - 1)];
                const XNext = Grid64[Gy * LeafSdf2dRes + Math.min(LeafSdf2dRes - 1, Gx + 1)];
                const YPrev = Grid64[Math.max(0, Gy - 1) * LeafSdf2dRes + Gx];
                const YNext = Grid64[Math.min(LeafSdf2dRes - 1, Gy + 1) * LeafSdf2dRes + Gx];
                const GxVal = (XNext - XPrev) * 0.5 * (LeafSdf2dRes * 0.5);
                const GzVal = (YNext - YPrev) * 0.5 * (LeafSdf2dRes * 0.5);

                const Dst = ((Arch * LeafSdf2dRes + Gy) * LeafSdf2dRes + Gx) * 4;
                Rgba16fTexels[Dst + 0] = EncodeFloat16Bits(Sd);
                Rgba16fTexels[Dst + 1] = EncodeFloat16Bits(GxVal);
                Rgba16fTexels[Dst + 2] = EncodeFloat16Bits(GzVal);
                Rgba16fTexels[Dst + 3] = EncodeFloat16Bits(1.0);
            }
        }

        FloatGrids.push(Grid64);
    }

    const ArchetypeStats = [];
    for (let A = 0; A < LeafArchetypeCount; ++A)
    {
        ArchetypeStats.push({
            Label: LeafArchetypeNames[A],
            AreaFraction: AreaFractions[A]
        });
    }

    return { FloatGrids, Rgba16fTexels, AreaFractions, ArchetypeStats };
}

// Exact CPU bilinear sample of a 64x64 2D Leaf SDF in local [-1, +1]^2 coordinates
export function SampleLeaf2dSdfBilinear(Grid64, LocalX, LocalZ)
{
    // Outside [-1, +1]^2, extend linearly by exterior box distance so far distances remain exact Eikonal!
    const ClampedX = Clamp(LocalX, -1.0, 1.0);
    const ClampedZ = Clamp(LocalZ, -1.0, 1.0);
    const Excess = Math.hypot(LocalX - ClampedX, LocalZ - ClampedZ);

    const U = ClampedX * 0.5 + 0.5;
    const V = ClampedZ * 0.5 + 0.5;
    const Fx = Clamp(U * LeafSdf2dRes - 0.5, 0.0, LeafSdf2dRes - 1.00001);
    const Fy = Clamp(V * LeafSdf2dRes - 0.5, 0.0, LeafSdf2dRes - 1.00001);
    const X0 = Math.floor(Fx), Y0 = Math.floor(Fy);
    const X1 = Math.min(LeafSdf2dRes - 1, X0 + 1);
    const Y1 = Math.min(LeafSdf2dRes - 1, Y0 + 1);
    const Tx = Fx - X0, Ty = Fy - Y0;

    const C00 = Grid64[Y0 * LeafSdf2dRes + X0];
    const C10 = Grid64[Y0 * LeafSdf2dRes + X1];
    const C01 = Grid64[Y1 * LeafSdf2dRes + X0];
    const C11 = Grid64[Y1 * LeafSdf2dRes + X1];

    const InBox = (C00 * (1.0 - Tx) + C10 * Tx) * (1.0 - Ty) + (C01 * (1.0 - Tx) + C11 * Tx) * Ty;
    return Excess > 0.0 ? Math.max(InBox, 0.0) + Excess : InBox;
}

//============================================================================================================================================
//                     2. 3D PROCEDURAL BARK & GNARL FBM NOISE + ARTICULATED WATERTIGHT CAPSULE ARBOR BUILDER
//============================================================================================================================================

function FractScalar(X)
{
    return X - Math.floor(X);
}

// Sin-free modulo-289 3D lattice hash (matches WGSL & GLSL float32 evaluation bit-for-bit across GPU vendors!)
export function HashLattice3d(Ix, Iy, Iz)
{
    const Wx = Ix - Math.floor(Ix * (1.0 / 289.0)) * 289.0;
    const Wy = Iy - Math.floor(Iy * (1.0 / 289.0)) * 289.0;
    const Wz = Iz - Math.floor(Iz * (1.0 / 289.0)) * 289.0;
    let Px = FractScalar(Wx * 0.1031);
    let Py = FractScalar(Wy * 0.1030);
    let Pz = FractScalar(Wz * 0.0973);
    const D = Px * (Py + 19.19) + Py * (Pz + 19.19) + Pz * (Px + 19.19);
    Px += D;
    Py += D;
    Pz += D;
    return FractScalar((Px + Py) * Pz);
}

export function EvalSmoothNoise3d(Qx, Qy, Qz)
{
    const Ix = Math.floor(Qx), Iy = Math.floor(Qy), Iz = Math.floor(Qz);
    const Fx = Qx - Ix,        Fy = Qy - Iy,        Fz = Qz - Iz;
    const Ux = Fx * Fx * (3.0 - 2.0 * Fx);
    const Uy = Fy * Fy * (3.0 - 2.0 * Fy);
    const Uz = Fz * Fz * (3.0 - 2.0 * Fz);

    const N000 = HashLattice3d(Ix,       Iy,       Iz);
    const N100 = HashLattice3d(Ix + 1.0, Iy,       Iz);
    const N010 = HashLattice3d(Ix,       Iy + 1.0, Iz);
    const N110 = HashLattice3d(Ix + 1.0, Iy + 1.0, Iz);
    const N001 = HashLattice3d(Ix,       Iy,       Iz + 1.0);
    const N101 = HashLattice3d(Ix + 1.0, Iy,       Iz + 1.0);
    const N011 = HashLattice3d(Ix,       Iy + 1.0, Iz + 1.0);
    const N111 = HashLattice3d(Ix + 1.0, Iy + 1.0, Iz + 1.0);

    const Nx00 = N000 + (N100 - N000) * Ux;
    const Nx10 = N010 + (N110 - N010) * Ux;
    const Nx01 = N001 + (N101 - N001) * Ux;
    const Nx11 = N011 + (N111 - N011) * Ux;
    const Nxy0 = Nx00 + (Nx10 - Nx00) * Uy;
    const Nxy1 = Nx01 + (Nx11 - Nx01) * Uy;
    return Nxy0 + (Nxy1 - Nxy0) * Uz;
}

// Anisotropic 2-octave 3D Bark Ridge + Woody Burl FBM in [-1, +1] (elongated along limb grain + finer burl knots)
export function EvalBarkFbm3d(Px, Py, Pz, Freq = DefaultBarkNoiseFreq)
{
    const Oct1 = EvalSmoothNoise3d(Px * Freq, Py * Freq * 0.46, Pz * Freq) * 2.0 - 1.0;
    const F2   = Freq * 2.13;
    const Oct2 = EvalSmoothNoise3d(Pz * F2 + 11.3, Px * 0.62 * F2 + 7.1, Py * F2 + 5.7) * 2.0 - 1.0;
    return Oct1 * 0.68 + Oct2 * 0.32;
}

function MakeSeededPrng(SeedInt)
{
    let S = SeedInt >>> 0;
    return function NextRand()
    {
        S = (S + 0x6d2b79f5) >>> 0;
        let T = Math.imul(S ^ (S >>> 15), 1 | S);
        T = (T + Math.imul(T ^ (T >>> 7), 61 | T)) ^ T;
        return ((T ^ (T >>> 14)) >>> 0) / 4294967296.0;
    };
}

function BuildLeafOrthonormalAxes(ForwardDir, RollAngle)
{
    const ZAxis = Normalize3(ForwardDir);
    const RefUp = Math.abs(ZAxis[1]) > 0.92 ? [1.0, 0.0, 0.0] : [0.0, 1.0, 0.0];
    const Right0 = Normalize3(Cross3(RefUp, ZAxis));
    const Up0    = Normalize3(Cross3(ZAxis, Right0));

    const CosR = Math.cos(RollAngle), SinR = Math.sin(RollAngle);
    const XAxis = [
        Right0[0] * CosR + Up0[0] * SinR,
        Right0[1] * CosR + Up0[1] * SinR,
        Right0[2] * CosR + Up0[2] * SinR
    ];
    const YAxis = [
        -Right0[0] * SinR + Up0[0] * CosR,
        -Right0[1] * SinR + Up0[1] * CosR,
        -Right0[2] * SinR + Up0[2] * CosR
    ];
    return { XAxis, YAxis, ZAxis };
}

function EvalCatmullPoint(A, B, C, D, T)
{
    const T2 = T * T;
    const T3 = T2 * T;
    const Out = [0.0, 0.0, 0.0];
    for (let K = 0; K < 3; ++K)
    {
        Out[K] = 0.5 * (
            (2.0 * B[K]) +
            (-A[K] + C[K]) * T +
            (2.0 * A[K] - 5.0 * B[K] + 4.0 * C[K] - D[K]) * T2 +
            (-A[K] + 3.0 * B[K] - 3.0 * C[K] + D[K]) * T3
        );
    }
    return Out;
}

// 📝 Constructs one of 3 Scene Presets using continuous ball-jointed Tapered Capsule Chains + 3D Procedural Gnarl & Bark Noise:
//    - "bough-closeup":  Organic cantilevered bough + 5 welded lateral sub-twigs + 36 curved leaves across all 5 archetypes
//    - "full-arbor":     Complete botanical arbor (sculpted trunk + 9 curved limbs + 18 lateral fork twigs + 235 curved leaves)
//    - "avenue-lod":     3 full arbors arrayed from near (z = +1.2 m) to far (z = -9.8 m) demonstrating Cone-Width Optical-Depth LOD
export function ConstructFoliagePreset(PresetId, BarkOpts = {})
{
    const BarkNoiseAmp  = BarkOpts.BarkNoiseAmp  !== undefined ? BarkOpts.BarkNoiseAmp  : DefaultBarkNoiseAmp;
    const BarkNoiseFreq = BarkOpts.BarkNoiseFreq !== undefined ? BarkOpts.BarkNoiseFreq : DefaultBarkNoiseFreq;
    const BranchGnarl   = BarkOpts.BranchGnarl   !== undefined ? BarkOpts.BranchGnarl   : DefaultBranchGnarl;

    const Rand = MakeSeededPrng(PresetId === "bough-closeup" ? 1337 : (PresetId === "full-arbor" ? 4209 : 9001));
    const Capsules = [];      // { P0:[3], R0, P1:[3], R1 }
    const LeafInstances = []; // { Center:[3], XAxis:[3], YAxis:[3], ZAxis:[3], Scale, ArchetypeId, CamberX, CamberZ }

    // Builds an unbroken chain of ball-jointed Tapered Capsules along smooth waypoints + 3D procedural gnarl noise.
    // Pinned exact-on Waypoints[0] at U = 0 so every limb collar stays 100% welded inside its host limb's centerline!
    function AppendOrganicCapsuleChain(Waypoints, Radii, TotalSegments, GnarlDisplacementM)
    {
        const SpanCount = Waypoints.length - 1;
        const SamplesPos = [];
        const SamplesRad = [];

        for (let Step = 0; Step <= TotalSegments; ++Step)
        {
            const U = Step / TotalSegments;
            const Scaled = U * SpanCount;
            const Seg = Math.min(SpanCount - 1, Math.floor(Scaled));
            const LocalT = Scaled - Seg;

            const W0 = Waypoints[Math.max(0, Seg - 1)];
            const W1 = Waypoints[Seg];
            const W2 = Waypoints[Seg + 1];
            const W3 = Waypoints[Math.min(SpanCount, Seg + 2)];

            const SmoothPt = SpanCount === 1
                ? [W1[0] + (W2[0] - W1[0]) * LocalT, W1[1] + (W2[1] - W1[1]) * LocalT, W1[2] + (W2[2] - W1[2]) * LocalT]
                : EvalCatmullPoint(W0, W1, W2, W3, LocalT);

            const Rad = Radii[Seg] + (Radii[Seg + 1] - Radii[Seg]) * LocalT;

            // Zero gnarl at U = 0 so attachment joint never detaches from host limb!
            const Env = Math.sin(Math.PI * Math.min(1.0, U * 0.92)) * BranchGnarl * GnarlDisplacementM;
            let Pos = SmoothPt;
            if (Step > 0 && Env > 1e-5)
            {
                const Tang = Normalize3([W2[0] - W1[0], W2[1] - W1[1], W2[2] - W1[2]]);
                const RefUp = Math.abs(Tang[1]) > 0.90 ? [1.0, 0.0, 0.0] : [0.0, 1.0, 0.0];
                const PerpA = Normalize3(Cross3(RefUp, Tang));
                const PerpB = Normalize3(Cross3(Tang, PerpA));
                const N1 = EvalBarkFbm3d(SmoothPt[0] + 3.1, SmoothPt[1] + 1.7, SmoothPt[2] - 2.3, 1.85);
                const N2 = EvalBarkFbm3d(SmoothPt[2] - 4.7, SmoothPt[0] + 2.9, SmoothPt[1] + 5.1, 1.85);
                Pos = [
                    SmoothPt[0] + (PerpA[0] * N1 + PerpB[0] * N2) * Env,
                    SmoothPt[1] + (PerpA[1] * N1 + PerpB[1] * N2) * Env,
                    SmoothPt[2] + (PerpA[2] * N1 + PerpB[2] * N2) * Env
                ];
            }

            SamplesPos.push(Pos);
            SamplesRad.push(Rad);
        }

        for (let Step = 0; Step < TotalSegments; ++Step)
        {
            Capsules.push({
                P0: SamplesPos[Step],
                R0: SamplesRad[Step],
                P1: SamplesPos[Step + 1],
                R1: SamplesRad[Step + 1]
            });
        }

        return { SamplesPos, SamplesRad };
    }

    function SampleCapsuleChain(Chain, Frac)
    {
        const Last = Chain.SamplesPos.length - 1;
        const F = Clamp(Frac, 0.0, 1.0) * Last;
        const I0 = Math.min(Last - 1, Math.floor(F));
        const T  = F - I0;
        const A  = Chain.SamplesPos[I0];
        const B  = Chain.SamplesPos[I0 + 1];
        return {
            Pos: [A[0] + (B[0] - A[0]) * T, A[1] + (B[1] - A[1]) * T, A[2] + (B[2] - A[2]) * T],
            Rad: Chain.SamplesRad[I0] + (Chain.SamplesRad[I0 + 1] - Chain.SamplesRad[I0]) * T,
            Tangent: Normalize3([B[0] - A[0], B[1] - A[1], B[2] - A[2]])
        };
    }

    function AddLeafSprayAlongChain(TwigChain, FracStart, FracEnd, Count, LeafScaleRange, ArchBias = -1)
    {
        for (let K = 0; K < Count; ++K)
        {
            const Alpha = Count === 1 ? 1.0 : (K / (Count - 1));
            const Frac  = FracStart + (FracEnd - FracStart) * Alpha;
            const SideSign = (K % 2 === 0) ? 1.0 : -1.0;
            const Sample = SampleCapsuleChain(TwigChain, Frac);
            const MountPt = Sample.Pos;
            const TwigDir = Sample.Tangent;

            const SideLat = Normalize3(Cross3([0.0, 1.0, 0.0], TwigDir));
            const Splay = K === Count - 1 ? 0.0 : SideSign * (0.68 + 0.35 * Rand());
            const PitchOut = -0.18 - 0.32 * Rand();
            const OutDir = Normalize3([
                TwigDir[0] + SideLat[0] * Splay,
                TwigDir[1] * 0.45 + PitchOut,
                TwigDir[2] + SideLat[2] * Splay
            ]);

            const Scale = LeafScaleRange[0] + (LeafScaleRange[1] - LeafScaleRange[0]) * Rand();
            // Position leaf blade center so petiole (-0.85 * Scale along ZAxis) meets the twig capsule cleanly!
            const Center = [
                MountPt[0] + OutDir[0] * Scale * 0.82,
                MountPt[1] + OutDir[1] * Scale * 0.82,
                MountPt[2] + OutDir[2] * Scale * 0.82
            ];

            const Roll = (Rand() - 0.5) * 0.62;
            const { XAxis, YAxis, ZAxis } = BuildLeafOrthonormalAxes(OutDir, Roll);
            const ArchetypeId = ArchBias >= 0 ? ArchBias : ((LeafInstances.length) % LeafArchetypeCount);

            LeafInstances.push({
                Center,
                XAxis,
                YAxis,
                ZAxis,
                Scale,
                ArchetypeId,
                CamberX: 0.14 + 0.10 * Rand(),   // Gentle upward V-cup across blade width
                CamberZ: -0.18 - 0.12 * Rand()   // Natural botanical gravity droop along blade length
            });
        }
    }

    if (PresetId === "bough-closeup")
    {
        // Continuous curved cantilevered wooden bough (8 ball-jointed tapered capsules — zero sharp elbow kinks!)
        const MainBough = AppendOrganicCapsuleChain(
            [
                [-1.55, 0.00, -0.45],
                [-1.30, 0.86, -0.32],
                [-0.86, 1.37, -0.15],
                [-0.12, 1.56,  0.05],
                [ 0.92, 1.47,  0.22]
            ],
            [0.155, 0.122, 0.094, 0.064, 0.030],
            8,
            0.065
        );

        const SubTwigSpecs = [
            { HostFrac: 0.48, Tip: [-0.45, 1.52,  0.78], Arch: 3, N: 7 }, // 5-Point Maple spray!
            { HostFrac: 0.60, Tip: [-0.08, 1.66, -0.78], Arch: 2, N: 7 }, // Lobed Oak spray!
            { HostFrac: 0.73, Tip: [ 0.42, 1.62,  0.86], Arch: 1, N: 7 }, // Heart Linden spray!
            { HostFrac: 0.84, Tip: [ 0.78, 1.58, -0.64], Arch: 0, N: 7 }, // Serrated Beech spray!
            { HostFrac: 0.93, Tip: [ 1.32, 1.36,  0.42], Arch: 4, N: 8 }  // Slender Willow tip spray!
        ];
        for (const Tw of SubTwigSpecs)
        {
            const Anchor = SampleCapsuleChain(MainBough, Tw.HostFrac);
            const MidPt = [
                0.5 * (Anchor.Pos[0] + Tw.Tip[0]),
                0.5 * (Anchor.Pos[1] + Tw.Tip[1]) + 0.045,
                0.5 * (Anchor.Pos[2] + Tw.Tip[2])
            ];
            const CollarR = Math.min(0.038, Anchor.Rad * 0.78);
            const TwigChain = AppendOrganicCapsuleChain(
                [Anchor.Pos, MidPt, Tw.Tip],
                [CollarR, 0.024, 0.014],
                2,
                0.040
            );
            AddLeafSprayAlongChain(TwigChain, 0.18, 1.0, Tw.N, [0.145, 0.185], Tw.Arch);
        }

        return {
            PresetId,
            Label: "Isolated 5-Template Bough (36 Thin Curved Leaves · 18 Bark Capsules)",
            BoundsMin: [-1.85, -0.05, -1.55],
            BoundsMax: [ 1.85,  2.35,  1.55],
            BarkNoiseAmp,
            BarkNoiseFreq,
            BranchGnarl,
            Capsules,
            LeafInstances
        };
    }

    function AppendFullArborAt(RootOrigin, HeightScale, MixedArchetypes)
    {
        const [Ox, Oy, Oz] = RootOrigin;
        const S = HeightScale;

        // Continuous organic S-curved trunk all the way from root flare to crown apex (zero radius step-downs or detached limbs!)
        const TrunkChain = AppendOrganicCapsuleChain(
            [
                [Ox,            Oy,            Oz],
                [Ox + 0.05 * S, Oy + 1.15 * S, Oz - 0.04 * S],
                [Ox - 0.03 * S, Oy + 2.22 * S, Oz + 0.03 * S],
                [Ox + 0.01 * S, Oy + 3.35 * S, Oz - 0.01 * S]
            ],
            [0.22 * S, 0.155 * S, 0.102 * S, 0.020 * S],
            7,
            0.075 * S
        );

        const LimbCount = 9;
        for (let L = 0; L < LimbCount; ++L)
        {
            const Az = (L / LimbCount) * Math.PI * 2.0 + 0.35;
            const Tier = L % 3;
            const TrunkFrac = 0.43 + Tier * 0.115 + (L * 0.008);
            const TrunkJoint = SampleCapsuleChain(TrunkChain, TrunkFrac);

            const Reach = (1.34 - Tier * 0.17) * S;
            const Rise  = (0.50 + Tier * 0.23) * S;
            const LStart = TrunkJoint.Pos;
            const LMid = [
                LStart[0] + Math.cos(Az) * Reach * 0.54,
                LStart[1] + Rise * 0.62,
                LStart[2] + Math.sin(Az) * Reach * 0.54
            ];
            const LEnd = [
                LStart[0] + Math.cos(Az) * Reach,
                LStart[1] + Rise,
                LStart[2] + Math.sin(Az) * Reach
            ];

            const LimbR0 = Math.min(0.084 * S, TrunkJoint.Rad * 0.78);
            const LimbChain = AppendOrganicCapsuleChain(
                [LStart, LMid, LEnd],
                [LimbR0, 0.048 * S, 0.020 * S],
                3,
                0.055 * S
            );

            const ArchChoice = MixedArchetypes ? (L % LeafArchetypeCount) : ((L + 2) % LeafArchetypeCount);
            AddLeafSprayAlongChain(LimbChain, 0.50, 1.0, 9, [0.115 * S, 0.155 * S], ArchChoice);

            // Two lateral fork twigs per main limb, welded directly onto LimbChain's curved centerline!
            for (const ForkSign of [-1.0, 1.0])
            {
                const ForkAnchor = SampleCapsuleChain(LimbChain, 0.52);
                const ForkAz = Az + ForkSign * 0.54;
                const FEnd = [
                    ForkAnchor.Pos[0] + Math.cos(ForkAz) * Reach * 0.58,
                    ForkAnchor.Pos[1] + (0.24 + 0.15 * Rand()) * S,
                    ForkAnchor.Pos[2] + Math.sin(ForkAz) * Reach * 0.58
                ];
                const FMid = [
                    0.5 * (ForkAnchor.Pos[0] + FEnd[0]),
                    0.5 * (ForkAnchor.Pos[1] + FEnd[1]) + 0.035 * S,
                    0.5 * (ForkAnchor.Pos[2] + FEnd[2])
                ];
                const ForkCollarR = Math.min(0.036 * S, ForkAnchor.Rad * 0.78);
                const ForkChain = AppendOrganicCapsuleChain(
                    [ForkAnchor.Pos, FMid, FEnd],
                    [ForkCollarR, 0.023 * S, 0.014 * S],
                    2,
                    0.038 * S
                );
                AddLeafSprayAlongChain(
                    ForkChain,
                    0.18,
                    1.0,
                    8,
                    [0.105 * S, 0.145 * S],
                    (ArchChoice + (ForkSign > 0 ? 1 : 2)) % LeafArchetypeCount
                );
            }
        }

        // Crown apex leaf spray along the upper 32% of the trunk leader
        AddLeafSprayAlongChain(TrunkChain, 0.68, 1.0, 10, [0.11 * S, 0.145 * S], 3);
    }

    if (PresetId === "full-arbor")
    {
        AppendFullArborAt([0.0, 0.0, 0.0], 1.0, true);
        return {
            PresetId,
            Label: `Full Botanical Arbor (${LeafInstances.length} Instanced Leaves · ${Capsules.length} Bark Capsules)`,
            BoundsMin: [-2.35, -0.05, -2.35],
            BoundsMax: [ 2.35,  3.85,  2.35],
            BarkNoiseAmp,
            BarkNoiseFreq,
            BranchGnarl,
            Capsules,
            LeafInstances
        };
    }

    // "avenue-lod": 3 Arbors spanning Near -> Mid -> Far along Z
    AppendFullArborAt([ 0.0, 0.0,  1.2], 0.95, true);
    AppendFullArborAt([-2.6, 0.0, -4.6], 0.90, false);
    AppendFullArborAt([ 2.4, 0.0, -9.8], 0.88, true);

    return {
        PresetId,
        Label: `Multi-Arbor Avenue (${LeafInstances.length} Thin Leaves · ${Capsules.length} Bark Capsules)`,
        BoundsMin: [-4.9, -0.05, -12.2],
        BoundsMax: [ 4.7,  3.75,   3.6],
        BarkNoiseAmp,
        BarkNoiseFreq,
        BranchGnarl,
        Capsules,
        LeafInstances
    };
}

//============================================================================================================================================
//                                  3. EXACT ANALYTIC CAPSULE + THIN-LEAF SDF & RAY EVALUATORS
//============================================================================================================================================

export function EvalTaperedCapsuleSdf(P, Cap, BarkNoiseAmp = 0.0, BarkNoiseFreq = DefaultBarkNoiseFreq)
{
    const Bax = Cap.P1[0] - Cap.P0[0], Bay = Cap.P1[1] - Cap.P0[1], Baz = Cap.P1[2] - Cap.P0[2];
    const Pax = P[0] - Cap.P0[0],      Pay = P[1] - Cap.P0[1],      Paz = P[2] - Cap.P0[2];
    const BaDot = Bax * Bax + Bay * Bay + Baz * Baz;
    const H = Clamp((Pax * Bax + Pay * Bay + Paz * Baz) / Math.max(BaDot, 1e-6), 0.0, 1.0);
    const Dx = Pax - Bax * H, Dy = Pay - Bay * H, Dz = Paz - Baz * H;
    const Len = Math.hypot(Dx, Dy, Dz);
    const SmoothRad = Cap.R0 + (Cap.R1 - Cap.R0) * H;
    if (BarkNoiseAmp <= 1e-4)
    {
        return Len - SmoothRad;
    }
    const InvL = 1.0 / Math.max(Len, 1e-6);
    const SkinX = Cap.P0[0] + Bax * H + SmoothRad * Dx * InvL;
    const SkinY = Cap.P0[1] + Bay * H + SmoothRad * Dy * InvL;
    const SkinZ = Cap.P0[2] + Baz * H + SmoothRad * Dz * InvL;
    const BarkDisp = 1.0 + BarkDispMaxFrac * BarkNoiseAmp * EvalBarkFbm3d(SkinX, SkinY, SkinZ, BarkNoiseFreq);
    return Len - SmoothRad * BarkDisp;
}

export function WorldToLeafLocal(P, Leaf)
{
    const Dx = P[0] - Leaf.Center[0];
    const Dy = P[1] - Leaf.Center[1];
    const Dz = P[2] - Leaf.Center[2];
    const InvS = 1.0 / Leaf.Scale;
    return [
        (Dx * Leaf.XAxis[0] + Dy * Leaf.XAxis[1] + Dz * Leaf.XAxis[2]) * InvS,
        (Dx * Leaf.YAxis[0] + Dy * Leaf.YAxis[1] + Dz * Leaf.YAxis[2]) * InvS,
        (Dx * Leaf.ZAxis[0] + Dy * Leaf.ZAxis[1] + Dz * Leaf.ZAxis[2]) * InvS
    ];
}

// Exact 3D Signed Distance to a single curved thin leaf (thickness = 2 * LeafHalfThicknessM = 1.2 mm!)
export function EvalSingleThinLeafExactSdf(P, Leaf, LeafFloatGrids)
{
    const Dx = P[0] - Leaf.Center[0];
    const Dy = P[1] - Leaf.Center[1];
    const Dz = P[2] - Leaf.Center[2];
    const CenterDist = Math.hypot(Dx, Dy, Dz);
    const BoundRad = Leaf.Scale * 1.52;
    if (CenterDist > BoundRad + 0.35)
    {
        return CenterDist - BoundRad;
    }

    const Q = WorldToLeafLocal(P, Leaf);
    const CamberY = Leaf.CamberX * Q[0] * Q[0] + Leaf.CamberZ * Q[2] * Q[2];
    const DPerpWorld = Math.abs(Q[1] - CamberY) * Leaf.Scale - LeafHalfThicknessM;
    const D2dWorld   = SampleLeaf2dSdfBilinear(LeafFloatGrids[Leaf.ArchetypeId], Q[0], Q[2]) * Leaf.Scale;

    const OutX = Math.max(D2dWorld, 0.0);
    const OutY = Math.max(DPerpWorld, 0.0);
    const InMax = Math.min(Math.max(D2dWorld, DPerpWorld), 0.0);
    const GradNorm = Math.sqrt(1.0 + 4.0 * Leaf.CamberX * Leaf.CamberX * Q[0] * Q[0] + 4.0 * Leaf.CamberZ * Leaf.CamberZ * Q[2] * Q[2]);
    return (Math.hypot(OutX, OutY) + InMax) / GradNorm;
}

// 📝 Compiles BOTH the 32^3 Voxel Comparison Volume AND the 16^3 Two-Level Canopy Brick Accelerator + Sorted Instance Buffer
export function CompileFoliageSdfVolumes(SceneSpec, CompiledArchetypes)
{
    const BMin = SceneSpec.BoundsMin;
    const BMax = SceneSpec.BoundsMax;
    const Span = [BMax[0] - BMin[0], BMax[1] - BMin[1], BMax[2] - BMin[2]];
    const Leaves = SceneSpec.LeafInstances;
    const Capsules = SceneSpec.Capsules;
    const Grids2d = CompiledArchetypes.FloatGrids;
    const ArchAreas = CompiledArchetypes.AreaFractions;

    // ① Build 16x16x16 Canopy Brick Accelerator first (fast spatial binning for both runtime GPU & 32^3 voxel compiler!)
    const BrickCount = CanopyBrickRes * CanopyBrickRes * CanopyBrickRes;
    const BrickLists = Array.from({ length: BrickCount }, () => []);
    const BrickSigma = new Float32Array(BrickCount * 3);
    const BrickOccupied = new Uint8Array(BrickCount);
    const BrickSize = [Span[0] / CanopyBrickRes, Span[1] / CanopyBrickRes, Span[2] / CanopyBrickRes];
    const BrickVol = Math.max(BrickSize[0] * BrickSize[1] * BrickSize[2], 1e-5);

    for (let LIdx = 0; LIdx < Leaves.length; ++LIdx)
    {
        const Leaf = Leaves[LIdx];
        // Conservative world AABB of this curved leaf blade (local [-1.06, 1.06] x [-0.36, 0.36] x [-1.06, 1.06])
        const Ex = Leaf.Scale * (1.06 * Math.abs(Leaf.XAxis[0]) + 0.38 * Math.abs(Leaf.YAxis[0]) + 1.06 * Math.abs(Leaf.ZAxis[0]));
        const Ey = Leaf.Scale * (1.06 * Math.abs(Leaf.XAxis[1]) + 0.38 * Math.abs(Leaf.YAxis[1]) + 1.06 * Math.abs(Leaf.ZAxis[1]));
        const Ez = Leaf.Scale * (1.06 * Math.abs(Leaf.XAxis[2]) + 0.38 * Math.abs(Leaf.YAxis[2]) + 1.06 * Math.abs(Leaf.ZAxis[2]));

        const Ix0 = Clamp(Math.floor(((Leaf.Center[0] - Ex - BMin[0]) / Span[0]) * CanopyBrickRes), 0, CanopyBrickRes - 1);
        const Ix1 = Clamp(Math.floor(((Leaf.Center[0] + Ex - BMin[0]) / Span[0]) * CanopyBrickRes), 0, CanopyBrickRes - 1);
        const Iy0 = Clamp(Math.floor(((Leaf.Center[1] - Ey - BMin[1]) / Span[1]) * CanopyBrickRes), 0, CanopyBrickRes - 1);
        const Iy1 = Clamp(Math.floor(((Leaf.Center[1] + Ey - BMin[1]) / Span[1]) * CanopyBrickRes), 0, CanopyBrickRes - 1);
        const Iz0 = Clamp(Math.floor(((Leaf.Center[2] - Ez - BMin[2]) / Span[2]) / 1.0 * CanopyBrickRes), 0, CanopyBrickRes - 1);
        const Iz1 = Clamp(Math.floor(((Leaf.Center[2] + Ez - BMin[2]) / Span[2]) * CanopyBrickRes), 0, CanopyBrickRes - 1);

        const SharedBricks = Math.max(1, (Ix1 - Ix0 + 1) * (Iy1 - Iy0 + 1) * (Iz1 - Iz0 + 1));
        const BladeArea = (4.0 * Leaf.Scale * Leaf.Scale) * ArchAreas[Leaf.ArchetypeId];
        const DensityContrib = (BladeArea / SharedBricks) / BrickVol;

        for (let Bz = Iz0; Bz <= Iz1; ++Bz)
        {
            for (let By = Iy0; By <= Iy1; ++By)
            {
                for (let Bx = Ix0; Bx <= Ix1; ++Bx)
                {
                    const Flat = (Bz * CanopyBrickRes + By) * CanopyBrickRes + Bx;
                    if (BrickLists[Flat].length < 24)
                    {
                        BrickLists[Flat].push(LIdx);
                    }
                    BrickOccupied[Flat] = 1;
                    // Project leaf normal YAxis onto world X, Y, Z axes for exact directional Beer-Lambert extinction!
                    BrickSigma[Flat * 3 + 0] += DensityContrib * Math.max(0.18, Math.abs(Leaf.YAxis[0]));
                    BrickSigma[Flat * 3 + 1] += DensityContrib * Math.max(0.18, Math.abs(Leaf.YAxis[1]));
                    BrickSigma[Flat * 3 + 2] += DensityContrib * Math.max(0.18, Math.abs(Leaf.YAxis[2]));
                }
            }
        }
    }

    // Collect occupied brick centers for fast conservative canopy hull distance
    const OccupiedBrickBounds = [];
    for (let Bz = 0; Bz < CanopyBrickRes; ++Bz)
    {
        for (let By = 0; By < CanopyBrickRes; ++By)
        {
            for (let Bx = 0; Bx < CanopyBrickRes; ++Bx)
            {
                const Flat = (Bz * CanopyBrickRes + By) * CanopyBrickRes + Bx;
                if (!BrickOccupied[Flat]) continue;
                OccupiedBrickBounds.push({
                    Flat,
                    Bx, By, Bz,
                    Density: (BrickSigma[Flat * 3 + 0] + BrickSigma[Flat * 3 + 1] + BrickSigma[Flat * 3 + 2]) / 3.0,
                    Min: [BMin[0] + Bx * BrickSize[0], BMin[1] + By * BrickSize[1], BMin[2] + Bz * BrickSize[2]],
                    Max: [BMin[0] + (Bx + 1) * BrickSize[0], BMin[1] + (By + 1) * BrickSize[1], BMin[2] + (Bz + 1) * BrickSize[2]]
                });
            }
        }
    }

    // Pack flat BrickIndexBuffer & BrickHeaderBuffer (each brick = 2 x vec4: [Offset, Count, 0, 0] + [SigmaX, SigmaY, SigmaZ, Occupied])
    const BrickOffsetsU32 = new Uint32Array(BrickCount * 4);
    const BrickOpticalF32 = new Float32Array(BrickCount * 4);
    const FlatLeafIndices = [];
    for (let B = 0; B < BrickCount; ++B)
    {
        const List = BrickLists[B];
        BrickOffsetsU32[B * 4 + 0] = FlatLeafIndices.length;
        BrickOffsetsU32[B * 4 + 1] = List.length;
        BrickOffsetsU32[B * 4 + 2] = 0;
        BrickOffsetsU32[B * 4 + 3] = 0;
        for (let K = 0; K < List.length; ++K) FlatLeafIndices.push(List[K]);

        BrickOpticalF32[B * 4 + 0] = BrickSigma[B * 3 + 0];
        BrickOpticalF32[B * 4 + 1] = BrickSigma[B * 3 + 1];
        BrickOpticalF32[B * 4 + 2] = BrickSigma[B * 3 + 2];
        BrickOpticalF32[B * 4 + 3] = BrickOccupied[B] ? 1.0 : 0.0;
    }
    const BrickLeafRefsU32 = new Uint32Array(Math.max(4, FlatLeafIndices.length));
    for (let I = 0; I < FlatLeafIndices.length; ++I) BrickLeafRefsU32[I] = FlatLeafIndices[I];

    // Pack GPU Leaf Instance Storage Buffer: 5 x vec4<f32> (80 bytes) per leaf instance (100% 16-byte aligned!)
    //   v0: Center.xyz, InvScale
    //   v1: XAxis.xyz,  Scale
    //   v2: YAxis.xyz,  CamberX
    //   v3: ZAxis.xyz,  CamberZ
    //   v4: ArchetypeId, 0, 0, 0
    const LeafGpuFloats = new Float32Array(Math.max(1, Leaves.length) * 20);
    for (let I = 0; I < Leaves.length; ++I)
    {
        const L = Leaves[I];
        const Off = I * 20;
        LeafGpuFloats[Off + 0] = L.Center[0];
        LeafGpuFloats[Off + 1] = L.Center[1];
        LeafGpuFloats[Off + 2] = L.Center[2];
        LeafGpuFloats[Off + 3] = 1.0 / L.Scale;

        LeafGpuFloats[Off + 4] = L.XAxis[0];
        LeafGpuFloats[Off + 5] = L.XAxis[1];
        LeafGpuFloats[Off + 6] = L.XAxis[2];
        LeafGpuFloats[Off + 7] = L.Scale;

        LeafGpuFloats[Off + 8]  = L.YAxis[0];
        LeafGpuFloats[Off + 9]  = L.YAxis[1];
        LeafGpuFloats[Off + 10] = L.YAxis[2];
        LeafGpuFloats[Off + 11] = L.CamberX;

        LeafGpuFloats[Off + 12] = L.ZAxis[0];
        LeafGpuFloats[Off + 13] = L.ZAxis[1];
        LeafGpuFloats[Off + 14] = L.ZAxis[2];
        LeafGpuFloats[Off + 15] = L.CamberZ;

        LeafGpuFloats[Off + 16] = L.ArchetypeId;
    }

    // Pack GPU Wooden Capsules Storage Buffer: 2 x vec4<f32> per capsule ([P0.xyz, R0], [P1.xyz, R1])
    const CapsuleGpuFloats = new Float32Array(Math.max(1, Capsules.length) * 8);
    for (let I = 0; I < Capsules.length; ++I)
    {
        const C = Capsules[I];
        CapsuleGpuFloats[I * 8 + 0] = C.P0[0];
        CapsuleGpuFloats[I * 8 + 1] = C.P0[1];
        CapsuleGpuFloats[I * 8 + 2] = C.P0[2];
        CapsuleGpuFloats[I * 8 + 3] = C.R0;
        CapsuleGpuFloats[I * 8 + 4] = C.P1[0];
        CapsuleGpuFloats[I * 8 + 5] = C.P1[1];
        CapsuleGpuFloats[I * 8 + 6] = C.P1[2];
        CapsuleGpuFloats[I * 8 + 7] = C.R1;
    }

    // ② Build TWO 32x32x32 3D Textures (both float32 for CPU validation AND rgba16float for WebGPU hardware trilinear sampling!):
    //    Texture A (MacroSdf3d):
    //      .r = Exact Wood Trunk/Branch Capsule Signed Distance
    //      .g = Exact Distance to Nearest Occupied Canopy Brick AABB (for empty-space sphere-tracing leaps!)
    //      .b = min(.r, .g) combined empty-space leap distance
    //      .a = 1.0
    //    Texture B (ComparisonVoxel3d):
    //      .r = Mode 0: Standard 3D Signed Voxel SDF = min(dWood, dLeavesSigned) -> thin leaves have dLeavesSigned > 0 at voxel centers!
    //      .g = Mode 1: Dilated Two-Sided 3D Voxel SDF = min(dWood, |dLeaves| - 0.55 * VoxelDiag) -> meatball blob!
    //      .b = Mode 2: Unsigned Distance to nearest wood/leaf surface
    //      .a = Mode 2: Local sub-voxel leaf opacity [0..1]
    const VRes = MacroVoxelRes;
    const TotalVox = VRes * VRes * VRes;
    const VoxelSize = [Span[0] / VRes, Span[1] / VRes, Span[2] / VRes];
    const VoxelDiag = Math.hypot(VoxelSize[0], VoxelSize[1], VoxelSize[2]);

    const MacroFloat3d      = new Float32Array(TotalVox * 4);
    const MacroRgba16f      = new Uint16Array(TotalVox * 4);
    const ComparisonFloat3d = new Float32Array(TotalVox * 4);
    const ComparisonRgba16f = new Uint16Array(TotalVox * 4);

    for (let Vz = 0; Vz < VRes; ++Vz)
    {
        const Pz = BMin[2] + (Vz + 0.5) * VoxelSize[2];
        const BzNear = Math.min(CanopyBrickRes - 1, Math.floor((Vz / VRes) * CanopyBrickRes));
        for (let Vy = 0; Vy < VRes; ++Vy)
        {
            const Py = BMin[1] + (Vy + 0.5) * VoxelSize[1];
            const ByNear = Math.min(CanopyBrickRes - 1, Math.floor((Vy / VRes) * CanopyBrickRes));
            for (let Vx = 0; Vx < VRes; ++Vx)
            {
                const Px = BMin[0] + (Vx + 0.5) * VoxelSize[0];
                const BxNear = Math.min(CanopyBrickRes - 1, Math.floor((Vx / VRes) * CanopyBrickRes));
                const P = [Px, Py, Pz];

                // 1. Exact Wood Capsule SDF (with 3D procedural bark ridge & burl FBM displacement)
                let DWood = 1e6;
                for (let C = 0; C < Capsules.length; ++C)
                {
                    const Dc = EvalTaperedCapsuleSdf(P, Capsules[C], SceneSpec.BarkNoiseAmp, SceneSpec.BarkNoiseFreq);
                    if (Dc < DWood) DWood = Dc;
                }

                // 2. Distance to nearest Occupied Canopy Brick AABB
                let DBrickHull = 1e6;
                for (let B = 0; B < OccupiedBrickBounds.length; ++B)
                {
                    const Ob = OccupiedBrickBounds[B];
                    const Dx = Math.max(Ob.Min[0] - Px, 0.0, Px - Ob.Max[0]);
                    const Dy = Math.max(Ob.Min[1] - Py, 0.0, Py - Ob.Max[1]);
                    const Dz = Math.max(Ob.Min[2] - Pz, 0.0, Pz - Ob.Max[2]);
                    const Db = Math.hypot(Dx, Dy, Dz);
                    if (Db < DBrickHull) DBrickHull = Db;
                }

                // 3. Exact distance to nearby thin leaves (check 3x3x3 neighborhood of CanopyBricks for speed + exactness!)
                let DLeafSigned = 1e6;
                for (let Dz = -1; Dz <= 1; ++Dz)
                {
                    const Nz = BzNear + Dz;
                    if (Nz < 0 || Nz >= CanopyBrickRes) continue;
                    for (let Dy = -1; Dy <= 1; ++Dy)
                    {
                        const Ny = ByNear + Dy;
                        if (Ny < 0 || Ny >= CanopyBrickRes) continue;
                        for (let Dx = -1; Dx <= 1; ++Dx)
                        {
                            const Nx = BxNear + Dx;
                            if (Nx < 0 || Nx >= CanopyBrickRes) continue;
                            const List = BrickLists[(Nz * CanopyBrickRes + Ny) * CanopyBrickRes + Nx];
                            for (let K = 0; K < List.length; ++K)
                            {
                                const Dl = EvalSingleThinLeafExactSdf(P, Leaves[List[K]], Grids2d);
                                if (Dl < DLeafSigned) DLeafSigned = Dl;
                            }
                        }
                    }
                }
                if (DLeafSigned > 9e5) DLeafSigned = DBrickHull + 0.25;

                // Leaf-only 32^3 comparison channels (wooden limbs are evaluated analytically via Tapered Capsules so branches NEVER break into pieces!)
                const DStandardSigned = DLeafSigned;
                const DDilated        = Math.abs(DLeafSigned) - 0.54 * VoxelDiag;
                const DUnsigned       = Math.abs(DLeafSigned);

                const HostBrick = (BzNear * CanopyBrickRes + ByNear) * CanopyBrickRes + BxNear;
                const MeanSigma = (BrickSigma[HostBrick * 3] + BrickSigma[HostBrick * 3 + 1] + BrickSigma[HostBrick * 3 + 2]) / 3.0;
                const VoxelOpacity = Clamp(MeanSigma * VoxelDiag * 0.85, 0.0, 0.96);

                const OutIdx = ((Vz * VRes + Vy) * VRes + Vx) * 4;
                // Subtract 0.55 * VoxelDiag from DBrickHull so trilinear interpolation is strictly <= 0 everywhere inside every occupied brick!
                const ConservativeHull = Math.max(0.0, DBrickHull - 0.55 * VoxelDiag);
                const CombinedMacro = Math.min(Math.max(DWood, 0.0), ConservativeHull);

                MacroFloat3d[OutIdx + 0] = DWood;
                MacroFloat3d[OutIdx + 1] = ConservativeHull;
                MacroFloat3d[OutIdx + 2] = CombinedMacro;
                MacroFloat3d[OutIdx + 3] = 1.0;

                MacroRgba16f[OutIdx + 0] = EncodeFloat16Bits(DWood);
                MacroRgba16f[OutIdx + 1] = EncodeFloat16Bits(ConservativeHull);
                MacroRgba16f[OutIdx + 2] = EncodeFloat16Bits(CombinedMacro);
                MacroRgba16f[OutIdx + 3] = EncodeFloat16Bits(1.0);

                ComparisonFloat3d[OutIdx + 0] = DStandardSigned;
                ComparisonFloat3d[OutIdx + 1] = DDilated;
                ComparisonFloat3d[OutIdx + 2] = DUnsigned;
                ComparisonFloat3d[OutIdx + 3] = VoxelOpacity;

                ComparisonRgba16f[OutIdx + 0] = EncodeFloat16Bits(DStandardSigned);
                ComparisonRgba16f[OutIdx + 1] = EncodeFloat16Bits(DDilated);
                ComparisonRgba16f[OutIdx + 2] = EncodeFloat16Bits(DUnsigned);
                ComparisonRgba16f[OutIdx + 3] = EncodeFloat16Bits(VoxelOpacity);
            }
        }
    }

    return {
        SceneSpec,
        BoundsMin: BMin,
        BoundsMax: BMax,
        VoxelSize,
        VoxelDiag,
        BrickSize,
        OccupiedBrickBounds,
        BrickOffsetsU32,
        BrickOpticalF32,
        BrickLeafRefsU32,
        LeafGpuFloats,
        CapsuleGpuFloats,
        MacroFloat3d,
        MacroRgba16f,
        ComparisonFloat3d,
        ComparisonRgba16f
    };
}

//============================================================================================================================================
//                                  4. CPU REFERENCE SHADOW SOLVERS & ERROR BENCHMARKING
//============================================================================================================================================

function IntersectRayAabb(Ro, Rd, BMin, BMax)
{
    let TMin = 0.0, TMax = 1e6;
    for (let A = 0; A < 3; ++A)
    {
        const InvD = 1.0 / (Math.abs(Rd[A]) < 1e-8 ? (Rd[A] >= 0 ? 1e-8 : -1e-8) : Rd[A]);
        let T0 = (BMin[A] - Ro[A]) * InvD;
        let T1 = (BMax[A] - Ro[A]) * InvD;
        if (T0 > T1) { const Tmp = T0; T0 = T1; T1 = Tmp; }
        if (T0 > TMin) TMin = T0;
        if (T1 < TMax) TMax = T1;
        if (TMax < TMin) return null;
    }
    return [TMin, TMax];
}

export function Sample3dVolumeTrilinear(FloatVol, Res, BMin, BMax, P, Channel)
{
    const U = Clamp((P[0] - BMin[0]) / (BMax[0] - BMin[0]), 0.0, 1.0);
    const V = Clamp((P[1] - BMin[1]) / (BMax[1] - BMin[1]), 0.0, 1.0);
    const W = Clamp((P[2] - BMin[2]) / (BMax[2] - BMin[2]), 0.0, 1.0);

    const Fx = Clamp(U * Res - 0.5, 0.0, Res - 1.00001);
    const Fy = Clamp(V * Res - 0.5, 0.0, Res - 1.00001);
    const Fz = Clamp(W * Res - 0.5, 0.0, Res - 1.00001);

    const X0 = Math.floor(Fx), Y0 = Math.floor(Fy), Z0 = Math.floor(Fz);
    const X1 = Math.min(Res - 1, X0 + 1), Y1 = Math.min(Res - 1, Y0 + 1), Z1 = Math.min(Res - 1, Z0 + 1);
    const Tx = Fx - X0, Ty = Fy - Y0, Tz = Fz - Z0;

    const At = (X, Y, Z) => FloatVol[((Z * Res + Y) * Res + X) * 4 + Channel];
    const C00 = At(X0, Y0, Z0) * (1 - Tx) + At(X1, Y0, Z0) * Tx;
    const C10 = At(X0, Y1, Z0) * (1 - Tx) + At(X1, Y1, Z0) * Tx;
    const C01 = At(X0, Y0, Z1) * (1 - Tx) + At(X1, Y0, Z1) * Tx;
    const C11 = At(X0, Y1, Z1) * (1 - Tx) + At(X1, Y1, Z1) * Tx;

    const Cy0 = C00 * (1 - Ty) + C10 * Ty;
    const Cy1 = C01 * (1 - Ty) + C11 * Ty;
    return Cy0 * (1 - Tz) + Cy1 * Tz;
}

// Evaluates 1 thin curved leaf instance across ray segment [T0, T1] using 2.5D Signed-Sheet Crossing + 64x64 2D Leaf SDF!
export function EvalThinLeafRaySegmentShadow(Ro, Rd, T0, T1, TanCone, Leaf, Grids2d)
{
    const Q0 = WorldToLeafLocal([Ro[0] + Rd[0] * T0, Ro[1] + Rd[1] * T0, Ro[2] + Rd[2] * T0], Leaf);
    const Q1 = WorldToLeafLocal([Ro[0] + Rd[0] * T1, Ro[1] + Rd[1] * T1, Ro[2] + Rd[2] * T1], Leaf);

    // Sub-clip to local leaf slab [-1.12, 1.12] x [-0.45, 0.45] x [-1.12, 1.12] so camber parabola stays inside local range!
    const Dq = [Q1[0] - Q0[0], Q1[1] - Q0[1], Q1[2] - Q0[2]];
    const Bounds = [1.14, 0.48, 1.14];
    let UMin = 0.0, UMax = 1.0;
    for (let A = 0; A < 3; ++A)
    {
        if (Math.abs(Dq[A]) < 1e-7)
        {
            if (Math.abs(Q0[A]) > Bounds[A]) return 1.0;
        }
        else
        {
            let Ua = (-Bounds[A] - Q0[A]) / Dq[A];
            let Ub = ( Bounds[A] - Q0[A]) / Dq[A];
            if (Ua > Ub) { const Tmp = Ua; Ua = Ub; Ub = Tmp; }
            if (Ua > UMin) UMin = Ua;
            if (Ub < UMax) UMax = Ub;
            if (UMax < UMin) return 1.0;
        }
    }

    const Qa = [Q0[0] + Dq[0] * UMin, Q0[1] + Dq[1] * UMin, Q0[2] + Dq[2] * UMin];
    const Qb = [Q0[0] + Dq[0] * UMax, Q0[1] + Dq[1] * UMax, Q0[2] + Dq[2] * UMax];
    const Qm = [0.5 * (Qa[0] + Qb[0]), 0.5 * (Qa[1] + Qb[1]), 0.5 * (Qa[2] + Qb[2])];

    const Sa = Qa[1] - (Leaf.CamberX * Qa[0] * Qa[0] + Leaf.CamberZ * Qa[2] * Qa[2]);
    const Sm = Qm[1] - (Leaf.CamberX * Qm[0] * Qm[0] + Leaf.CamberZ * Qm[2] * Qm[2]);
    const Sb = Qb[1] - (Leaf.CamberX * Qb[0] * Qb[0] + Leaf.CamberZ * Qb[2] * Qb[2]);

    let UStar = UMin;
    let CrossedSheet = false;
    if (Sa * Sm <= 0.0)
    {
        CrossedSheet = true;
        const Frac = Math.abs(Sa) / Math.max(Math.abs(Sa - Sm), 1e-6);
        UStar = UMin + 0.5 * (UMax - UMin) * Frac;
    }
    else if (Sm * Sb <= 0.0)
    {
        CrossedSheet = true;
        const Frac = Math.abs(Sm) / Math.max(Math.abs(Sm - Sb), 1e-6);
        UStar = UMin + (UMax - UMin) * (0.5 + 0.5 * Frac);
    }
    else
    {
        const AbsA = Math.abs(Sa), AbsM = Math.abs(Sm), AbsB = Math.abs(Sb);
        if (AbsM <= AbsA && AbsM <= AbsB) UStar = 0.5 * (UMin + UMax);
        else if (AbsB < AbsA) UStar = UMax;
    }

    const QStar = [Q0[0] + Dq[0] * UStar, Q0[1] + Dq[1] * UStar, Q0[2] + Dq[2] * UStar];
    const SStar = CrossedSheet ? 0.0 : Math.abs(QStar[1] - (Leaf.CamberX * QStar[0] * QStar[0] + Leaf.CamberZ * QStar[2] * QStar[2]));
    const DPerp = Math.max(0.0, SStar * Leaf.Scale - LeafHalfThicknessM);
    const D2d   = SampleLeaf2dSdfBilinear(Grids2d[Leaf.ArchetypeId], QStar[0], QStar[2]) * Leaf.Scale;

    if (D2d <= 0.0 && DPerp <= 0.0)
    {
        return 0.0; // Unconditional exact thin-leaf shadow hit!
    }

    const MissDist = Math.hypot(Math.max(D2d, 0.0), DPerp);
    const THit = T0 + (T1 - T0) * UStar;
    const ConeRad = Math.max(THit * TanCone, 0.0015);
    return Clamp(MissDist / ConeRad, 0.0, 1.0);
}

// Exact closed-form visibility of a ray Ro + t*Rd against a noisy tapered wooden branch capsule [P0, R0] -> [P1, R1]
// Handles interior & endpoint sphere joints (including 0-deg parallel rays!) + 3-tap 3D procedural bark FBM refinement!
export function EvalRayCapsuleShadow(
    Ro,
    Rd,
    TStart,
    TEnd,
    TanCone,
    Cap,
    BarkNoiseAmp = 0.0,
    BarkNoiseFreq = DefaultBarkNoiseFreq
)
{
    const Ux = Cap.P1[0] - Cap.P0[0], Uy = Cap.P1[1] - Cap.P0[1], Uz = Cap.P1[2] - Cap.P0[2];
    const Wx = Ro[0] - Cap.P0[0],     Wy = Ro[1] - Cap.P0[1],     Wz = Ro[2] - Cap.P0[2];

    const B = Rd[0] * Ux + Rd[1] * Uy + Rd[2] * Uz;
    const C = Ux * Ux + Uy * Uy + Uz * Uz;
    const D = Rd[0] * Wx + Rd[1] * Wy + Rd[2] * Wz;
    const E = Ux * Wx + Uy * Wy + Uz * Wz;
    const Denom = Math.max(0.0, C - B * B);

    let S = Denom > 1e-7 ? Clamp((E - B * D) / Denom, 0.0, 1.0) : 0.0;
    let T = Clamp(B * S - D, TStart, TEnd);
    S = C > 1e-7 ? Clamp((E + T * B) / C, 0.0, 1.0) : 0.0;
    T = Clamp(B * S - D, TStart, TEnd);

    // Also verify both spherical capsule caps (S=0 and S=1) so steep/parallel rays never miss ball-joints!
    let BestS = S, BestT = T;
    let Dx = Wx + Rd[0] * T - Ux * S;
    let Dy = Wy + Rd[1] * T - Uy * S;
    let Dz = Wz + Rd[2] * T - Uz * S;
    let BestSmooth = Math.hypot(Dx, Dy, Dz) - (Cap.R0 + (Cap.R1 - Cap.R0) * S);

    const TCap0 = Clamp(-D, TStart, TEnd);
    const D0 = Math.hypot(Wx + Rd[0] * TCap0, Wy + Rd[1] * TCap0, Wz + Rd[2] * TCap0) - Cap.R0;
    if (D0 < BestSmooth) { BestSmooth = D0; BestS = 0.0; BestT = TCap0; }

    const TCap1 = Clamp(B - D, TStart, TEnd);
    const D1 = Math.hypot(Wx + Rd[0] * TCap1 - Ux, Wy + Rd[1] * TCap1 - Uy, Wz + Rd[2] * TCap1 - Uz) - Cap.R1;
    if (D1 < BestSmooth) { BestSmooth = D1; BestS = 1.0; BestT = TCap1; }

    const SmoothRad = Cap.R0 + (Cap.R1 - Cap.R0) * BestS;
    const ConeR = Math.max(BestT * TanCone, 0.0015);
    const MaxBarkBulge = SmoothRad * BarkDispMaxFrac * BarkNoiseAmp;

    // Fast conservative early-out outside the capsule's outer 3D bark envelope!
    if (BestSmooth > MaxBarkBulge + ConeR)
    {
        return 1.0;
    }

    if (BarkNoiseAmp <= 1e-4)
    {
        return BestSmooth <= 0.0 ? 0.0 : Clamp(BestSmooth / ConeR, 0.0, 1.0);
    }

    // Deep inside solid wood interior (< R*(1 - BarkDispMaxFrac)) -> guaranteed 0.0 occlusion without evaluating 3D FBM!
    if (BestSmooth < -MaxBarkBulge)
    {
        return 0.0;
    }

    // Refine 3D procedural bark ridges & burls across 3 micro-samples [-dt, 0, +dt] around BestT!
    const StepT = SmoothRad * 0.42;
    let MinVis = 1.0;
    for (let Tap = -1; Tap <= 1; ++Tap)
    {
        const TSample = Clamp(BestT + Tap * StepT, TStart, TEnd);
        const SSample = C > 1e-7 ? Clamp((E + TSample * B) / C, 0.0, 1.0) : 0.0;
        const Rx = Wx + Rd[0] * TSample - Ux * SSample;
        const Ry = Wy + Rd[1] * TSample - Uy * SSample;
        const Rz = Wz + Rd[2] * TSample - Uz * SSample;
        const DistAxis = Math.hypot(Rx, Ry, Rz);
        const LocRad   = Cap.R0 + (Cap.R1 - Cap.R0) * SSample;
        const InvDist  = 1.0 / Math.max(DistAxis, 1e-6);
        const SkinX = Cap.P0[0] + Ux * SSample + LocRad * Rx * InvDist;
        const SkinY = Cap.P0[1] + Uy * SSample + LocRad * Ry * InvDist;
        const SkinZ = Cap.P0[2] + Uz * SSample + LocRad * Rz * InvDist;
        const NoisyRad  = LocRad * (1.0 + BarkDispMaxFrac * BarkNoiseAmp * EvalBarkFbm3d(SkinX, SkinY, SkinZ, BarkNoiseFreq));
        const NoisyDist = DistAxis - NoisyRad;
        if (NoisyDist <= 0.0) return 0.0;
        const TapCone = Math.max(TSample * TanCone, 0.0015);
        const TapVis  = Clamp(NoisyDist / TapCone, 0.0, 1.0);
        if (TapVis < MinVis) MinVis = TapVis;
    }
    return MinVis;
}

function EvalAllWoodCapsulesVisibility(Ro, Rd, TStart, TEnd, TanCone, SceneSpec)
{
    const Caps = SceneSpec.Capsules;
    const BarkAmp  = SceneSpec.BarkNoiseAmp  !== undefined ? SceneSpec.BarkNoiseAmp  : DefaultBarkNoiseAmp;
    const BarkFreq = SceneSpec.BarkNoiseFreq !== undefined ? SceneSpec.BarkNoiseFreq : DefaultBarkNoiseFreq;
    let WoodVis = 1.0;
    for (let C = 0; C < Caps.length; ++C)
    {
        WoodVis = Math.min(WoodVis, EvalRayCapsuleShadow(Ro, Rd, TStart, TEnd, TanCone, Caps[C], BarkAmp, BarkFreq));
        if (WoodVis <= 0.0) return 0.0;
    }
    return WoodVis;
}

// Exact Ground-Truth Ray-Traced Shadow (tests every noisy bark capsule & every thin leaf instance directly without any voxel grid!)
export function EvaluateExactGroundTruthShadow(Ro, Rd, TanCone, CompiledVol, CompiledArchetypes)
{
    const Hit = IntersectRayAabb(Ro, Rd, CompiledVol.BoundsMin, CompiledVol.BoundsMax);
    if (!Hit) return 1.0;
    const TStart = Math.max(0.015, Hit[0]);
    const TEnd = Hit[1];

    let Vis = EvalAllWoodCapsulesVisibility(Ro, Rd, TStart, TEnd, TanCone, CompiledVol.SceneSpec);
    if (Vis <= 0.0) return 0.0;

    const Leaves = CompiledVol.SceneSpec.LeafInstances;
    const Grids2d = CompiledArchetypes.FloatGrids;
    for (let L = 0; L < Leaves.length; ++L)
    {
        Vis = Math.min(Vis, EvalThinLeafRaySegmentShadow(Ro, Rd, TStart, TEnd, TanCone, Leaves[L], Grids2d));
        if (Vis <= 0.0) return 0.0;
    }
    return Vis;
}

// Evaluates any of the 4 SDF Shadow Modes on CPU (matching WGSL & GLSL shader logic bit-for-bit!)
export function EvaluateSdfShadowMode(Ro, Rd, TanCone, Mode, CompiledVol, CompiledArchetypes)
{
    const BMin = CompiledVol.BoundsMin;
    const BMax = CompiledVol.BoundsMax;
    const Hit = IntersectRayAabb(Ro, Rd, BMin, BMax);
    if (!Hit) return { Visibility: 1.0, Steps: 0 };

    const TStart = Math.max(0.015, Hit[0]);
    let T = TStart;
    const TEnd = Hit[1];
    const VRes = MacroVoxelRes;

    // Evaluate continuous Tapered-Capsule + 3D Bark-Noise Skeleton upfront so wooden limbs NEVER break into missing pieces!
    const WoodVis = EvalAllWoodCapsulesVisibility(Ro, Rd, TStart, TEnd, TanCone, CompiledVol.SceneSpec);
    if (WoodVis <= 0.0)
    {
        return { Visibility: 0.0, Steps: 1 };
    }

    // Modes 0 & 1: Sphere/Cone-tracing the 32^3 Signed or Dilated Leaf Voxel SDF (+ exact Capsule Branches)
    if (Mode === SdfEvaluationMode.StandardVoxel3d || Mode === SdfEvaluationMode.DilatedVoxel3d)
    {
        const Chan = Mode === SdfEvaluationMode.StandardVoxel3d ? 0 : 1;
        let LeafVis = 1.0;
        let Steps = 0;
        for (let Iter = 0; Iter < 56 && T < TEnd; ++Iter)
        {
            ++Steps;
            const P = [Ro[0] + Rd[0] * T, Ro[1] + Rd[1] * T, Ro[2] + Rd[2] * T];
            const D = Sample3dVolumeTrilinear(CompiledVol.ComparisonFloat3d, VRes, BMin, BMax, P, Chan);
            if (D <= 0.0005) return { Visibility: 0.0, Steps };
            const ConeR = Math.max(T * TanCone, 0.002);
            LeafVis = Math.min(LeafVis, Clamp(D / ConeR, 0.0, 1.0));
            T += Math.max(D * 0.85, 0.028);
        }
        return { Visibility: Math.min(WoodVis, LeafVis), Steps };
    }

    // Mode 2: Non-Instanced 32^3 Unsigned Leaf Distance + Sub-Voxel Opacity (+ exact Capsule Branches)
    if (Mode === SdfEvaluationMode.OpacityUdf3d)
    {
        let LeafVis = 1.0;
        let Steps = 0;
        const StepMin = CompiledVol.VoxelDiag * 0.35;
        for (let Iter = 0; Iter < 56 && T < TEnd; ++Iter)
        {
            ++Steps;
            const P = [Ro[0] + Rd[0] * T, Ro[1] + Rd[1] * T, Ro[2] + Rd[2] * T];
            const UDist = Sample3dVolumeTrilinear(CompiledVol.ComparisonFloat3d, VRes, BMin, BMax, P, 2);
            const Cov   = Sample3dVolumeTrilinear(CompiledVol.ComparisonFloat3d, VRes, BMin, BMax, P, 3);
            if (UDist < CompiledVol.VoxelDiag * 0.72 && Cov > 0.005)
            {
                LeafVis *= Math.exp(-Cov * 3.2 * (StepMin / CompiledVol.VoxelDiag));
                if (WoodVis * LeafVis < 0.01) return { Visibility: 0.0, Steps };
                T += StepMin;
            }
            else
            {
                T += Math.max((UDist - CompiledVol.VoxelDiag * 0.5) * 0.8, StepMin);
            }
        }
        return { Visibility: Math.min(WoodVis, LeafVis), Steps };
    }

    // 🟢 Mode 3: Two-Level 16^3 Canopy Brick + 5 x 2D Thin-Sheet Extrusion SDF + Continuous Cone-Width LOD (+ Noisy Capsule Limbs)!
    let LeafVis = 1.0;
    let Steps = 0;
    const Span = [BMax[0] - BMin[0], BMax[1] - BMin[1], BMax[2] - BMin[2]];
    const BrickSz = CompiledVol.BrickSize;
    const Leaves = CompiledVol.SceneSpec.LeafInstances;
    const Grids2d = CompiledArchetypes.FloatGrids;

    for (let Iter = 0; Iter < 48 && T < TEnd; ++Iter)
    {
        ++Steps;
        const P = [Ro[0] + Rd[0] * T, Ro[1] + Rd[1] * T, Ro[2] + Rd[2] * T];
        const ConeR = Math.max(T * TanCone, 0.0015);

        const DHull = Sample3dVolumeTrilinear(CompiledVol.MacroFloat3d, VRes, BMin, BMax, P, 1);
        if (DHull > ConeR + 0.01)
        {
            // Empty-space sphere-tracing leap!
            T += Math.max((DHull - ConeR) * 0.88, 0.025);
            continue;
        }

        // Inside or grazing Canopy Brick (Bx, By, Bz): compute exact slab exit TBrickOut so we visit each brick ONCE!
        const Bx = Clamp(Math.floor(((P[0] - BMin[0]) / Span[0]) * CanopyBrickRes), 0, CanopyBrickRes - 1);
        const By = Clamp(Math.floor(((P[1] - BMin[1]) / Span[1]) * CanopyBrickRes), 0, CanopyBrickRes - 1);
        const Bz = Clamp(Math.floor(((P[2] - BMin[2]) / Span[2]) * CanopyBrickRes), 0, CanopyBrickRes - 1);

        let TBrickOut = TEnd;
        const CellBoundX = BMin[0] + (Rd[0] >= 0 ? Bx + 1 : Bx) * BrickSz[0];
        const CellBoundY = BMin[1] + (Rd[1] >= 0 ? By + 1 : By) * BrickSz[1];
        const CellBoundZ = BMin[2] + (Rd[2] >= 0 ? Bz + 1 : Bz) * BrickSz[2];
        if (Math.abs(Rd[0]) > 1e-6) TBrickOut = Math.min(TBrickOut, (CellBoundX - Ro[0]) / Rd[0]);
        if (Math.abs(Rd[1]) > 1e-6) TBrickOut = Math.min(TBrickOut, (CellBoundY - Ro[1]) / Rd[1]);
        if (Math.abs(Rd[2]) > 1e-6) TBrickOut = Math.min(TBrickOut, (CellBoundZ - Ro[2]) / Rd[2]);
        TBrickOut = Math.max(T + 0.010, Math.min(TEnd, TBrickOut));

        const Flat = (Bz * CanopyBrickRes + By) * CanopyBrickRes + Bx;
        const Count = CompiledVol.BrickOffsetsU32[Flat * 4 + 1];

        if (Count > 0)
        {
            const AvgScale = 0.135;
            const LodBlend = Clamp((ConeR - AvgScale * 0.55) / (AvgScale * 0.85), 0.0, 1.0);

            let FineBrickVis = LeafVis;
            if (LodBlend < 0.999)
            {
                const StartOff = CompiledVol.BrickOffsetsU32[Flat * 4 + 0];
                const TPad0 = Math.max(0.015, T - 0.04);
                const TPad1 = Math.min(TEnd, TBrickOut + 0.04);
                for (let K = 0; K < Count; ++K)
                {
                    const LeafIdx = CompiledVol.BrickLeafRefsU32[StartOff + K];
                    FineBrickVis = Math.min(FineBrickVis, EvalThinLeafRaySegmentShadow(Ro, Rd, TPad0, TPad1, TanCone, Leaves[LeafIdx], Grids2d));
                    if (FineBrickVis <= 0.0) break;
                }
            }

            let FarBeerVis = LeafVis;
            if (LodBlend > 0.001)
            {
                const SigX = CompiledVol.BrickOpticalF32[Flat * 4 + 0];
                const SigY = CompiledVol.BrickOpticalF32[Flat * 4 + 1];
                const SigZ = CompiledVol.BrickOpticalF32[Flat * 4 + 2];
                const Extinction = Math.abs(Rd[0]) * SigX + Math.abs(Rd[1]) * SigY + Math.abs(Rd[2]) * SigZ;
                FarBeerVis = LeafVis * Math.exp(-Extinction * (TBrickOut - T));
            }

            LeafVis = FineBrickVis * (1.0 - LodBlend) + FarBeerVis * LodBlend;
            if (Math.min(WoodVis, LeafVis) <= 1e-4) return { Visibility: 0.0, Steps };
        }

        T = TBrickOut + 0.0015;
    }

    return { Visibility: Math.min(WoodVis, LeafVis), Steps };
}

// 📝 Benchmarks all 4 SDF Shadow Modes against Exact Ray-Traced Ground Truth on a stratified ground shadow grid
export function BenchmarkFoliageShadowAccuracy(CompiledVol, CompiledArchetypes, SunDir, TanCone = 0.008)
{
    const BMin = CompiledVol.BoundsMin;
    const BMax = CompiledVol.BoundsMax;
    const GridN = 28;

    // Project canopy center along -SunDir onto floor y = 0 to center the shadow test patch right on the cast shadow!
    const MidY = 0.55 * (BMin[1] + BMax[1]);
    const ShiftX = -SunDir[0] * (MidY / Math.max(SunDir[1], 0.25));
    const ShiftZ = -SunDir[2] * (MidY / Math.max(SunDir[1], 0.25));
    const SpanX = (BMax[0] - BMin[0]) * 0.72;
    const SpanZ = (BMax[2] - BMin[2]) * 0.72;
    const Cx = 0.5 * (BMin[0] + BMax[0]) + ShiftX;
    const Cz = 0.5 * (BMin[2] + BMax[2]) + ShiftZ;

    const ExactSamples = new Float32Array(GridN * GridN);
    const WoodOnlyMask = new Uint8Array(GridN * GridN);

    for (let Z = 0; Z < GridN; ++Z)
    {
        for (let X = 0; X < GridN; ++X)
        {
            const Ro = [
                Cx - SpanX + ((X + 0.5) / GridN) * 2.0 * SpanX,
                0.01,
                Cz - SpanZ + ((Z + 0.5) / GridN) * 2.0 * SpanZ
            ];
            const Idx = Z * GridN + X;
            ExactSamples[Idx] = EvaluateExactGroundTruthShadow(Ro, SunDir, TanCone, CompiledVol, CompiledArchetypes);

            // Identify rays occluded purely by wooden capsules vs. rays occluded strictly by thin leaves!
            const WoodVisOnly = EvalAllWoodCapsulesVisibility(Ro, SunDir, 0.015, 30.0, TanCone, CompiledVol.SceneSpec);
            WoodOnlyMask[Idx] = WoodVisOnly < 0.5 ? 1 : 0;
        }
    }

    function EvalModeStats(Mode)
    {
        let SqErr = 0.0;
        let GtLeafShadowCount = 0;
        let CapturedLeafCount = 0;
        let FalseBlobCount = 0;
        let TotalSteps = 0;

        for (let Z = 0; Z < GridN; ++Z)
        {
            for (let X = 0; X < GridN; ++X)
            {
                const Ro = [
                    Cx - SpanX + ((X + 0.5) / GridN) * 2.0 * SpanX,
                    0.01,
                    Cz - SpanZ + ((Z + 0.5) / GridN) * 2.0 * SpanZ
                ];
                const Idx = Z * GridN + X;
                const Gt = ExactSamples[Idx];
                const Res = EvaluateSdfShadowMode(Ro, SunDir, TanCone, Mode, CompiledVol, CompiledArchetypes);
                const Diff = Gt - Res.Visibility;
                SqErr += Diff * Diff;
                TotalSteps += Res.Steps;

                const GtShadowedByLeafOnly = (Gt < 0.5) && (WoodOnlyMask[Idx] === 0);
                if (GtShadowedByLeafOnly)
                {
                    ++GtLeafShadowCount;
                    if (Res.Visibility < 0.5) ++CapturedLeafCount;
                }
                if (Gt > 0.75 && Res.Visibility < 0.45)
                {
                    ++FalseBlobCount;
                }
            }
        }

        const N = GridN * GridN;
        const RmsePercent = Math.sqrt(SqErr / N) * 100.0;
        const LeafSurvivalPercent = GtLeafShadowCount > 0 ? (CapturedLeafCount / GtLeafShadowCount) * 100.0 : 100.0;
        const BlobBloatPercent = GtLeafShadowCount > 0 ? (FalseBlobCount / GtLeafShadowCount) * 100.0 : 0.0;

        return {
            RmsePercent,
            LeafSurvivalPercent,
            BlobBloatPercent,
            AvgSteps: TotalSteps / N
        };
    }

    return {
        StandardVoxel:   EvalModeStats(SdfEvaluationMode.StandardVoxel3d),
        DilatedVoxel:    EvalModeStats(SdfEvaluationMode.DilatedVoxel3d),
        OpacityUdf:      EvalModeStats(SdfEvaluationMode.OpacityUdf3d),
        TwoLevelSheet:   EvalModeStats(SdfEvaluationMode.TwoLevelThinSheet)
    };
}
