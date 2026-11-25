/*============================================================================================================================================
                                                      THINLEAFSDFCOMPILER.JS
============================================================================================================================================*/
// 🧩 Compiles 5 botanical thin-leaf archetypes (Beech, Linden, Oak, Maple, Willow) into exact 64x64 2D Euclidean Signed Distance Grids,
//    populates instanced curved leaves onto wooden branch capsules, and builds both:
//      ① 32x32x32 3D Voxel SDF volumes (exposing Standard Signed Zero-Crossing Erasure vs. Dilated Two-Sided Blob Bloat), and
//      ② The Two-Level 16x16x16 Canopy Brick + 2.5D Signed-Sheet Extrusion SDF with automatic Cone-Width Beer-Lambert Optical-Depth LOD.

export const LeafArchetypeCount = 5;
export const LeafSdf2dRes       = 64;   // [px] - 64x64 2D Signed Distance Grid per leaf archetype (20 KB total for all 5!)
export const MacroVoxelRes      = 32;   // [cells] - 32^3 3D Voxel SDF resolution for trunk + comparison voxel modes
export const CanopyBrickRes     = 16;   // [bricks] - 16^3 macro brick accelerator for leaf instance lists & 3-axis optical depth
export const LeafHalfThicknessM = 0.0006; // [m] - 0.6 mm half-thickness (1.2 mm total physical leaf blade thickness!)

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
//                                  2. PROCEDURAL ARBOR BRANCH CAPSULES & INSTANCED CURVED LEAVES
//============================================================================================================================================

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

// 📝 Constructs one of 3 Scene Presets:
//    - "bough-closeup":  Isolated forked wooden bough + 38 large crisp instanced leaves across all 5 archetypes (1.5 m above floor!)
//    - "full-arbor":     Complete botanical arbor (trunk + 15 limbs + 280 instanced leaves across 5 archetypes)
//    - "avenue-lod":     3 arbors arrayed from near (z = +1.5 m) to mid/far (z = -14 m) demonstrating Cone-Width Optical-Depth LOD
export function ConstructFoliagePreset(PresetId)
{
    const Rand = MakeSeededPrng(PresetId === "bough-closeup" ? 1337 : (PresetId === "full-arbor" ? 4209 : 9001));
    const Capsules = [];     // { P0:[3], R0, P1:[3], R1 }
    const LeafInstances = []; // { Center:[3], XAxis:[3], YAxis:[3], ZAxis:[3], Scale, ArchetypeId, CamberX, CamberZ }

    function AddTaperedCapsule(P0, R0, P1, R1)
    {
        Capsules.push({ P0, R0, P1, R1 });
    }

    function AddLeafSprayAlongTwig(TwigStart, TwigEnd, Count, LeafScaleRange, ArchBias = -1)
    {
        const TwigVec = [TwigEnd[0] - TwigStart[0], TwigEnd[1] - TwigStart[1], TwigEnd[2] - TwigStart[2]];
        const TwigDir = Normalize3(TwigVec);

        for (let K = 0; K < Count; ++K)
        {
            const Frac = Count === 1 ? 1.0 : (0.18 + 0.82 * (K / (Count - 1)));
            const SideSign = (K % 2 === 0) ? 1.0 : -1.0;
            const MountPt = [
                TwigStart[0] + TwigVec[0] * Frac,
                TwigStart[1] + TwigVec[1] * Frac,
                TwigStart[2] + TwigVec[2] * Frac
            ];

            const SideLat = Normalize3(Cross3([0.0, 1.0, 0.0], TwigDir));
            const Splay = K === Count - 1 ? 0.0 : SideSign * (0.68 + 0.35 * Rand());
            const PitchOut = -0.18 - 0.32 * Rand();
            const OutDir = Normalize3([
                TwigDir[0] + SideLat[0] * Splay,
                TwigDir[1] * 0.45 + PitchOut,
                TwigDir[2] + SideLat[2] * Splay
            ]);

            const Scale = LeafScaleRange[0] + (LeafScaleRange[1] - LeafScaleRange[0]) * Rand();
            // Position leaf blade center so petiole (-0.85 * Scale along ZAxis) meets the twig!
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
        // Sculpted low cantilevered bough at y = 1.25..1.95 m so individual Oak, Maple, Linden, Beech & Willow shadows
        // project large and razor-sharp onto the sunlit ground & vertical garden wall!
        AddTaperedCapsule([-1.55, 0.0, -0.45], 0.15, [-1.15, 1.28, -0.25], 0.10);
        AddTaperedCapsule([-1.15, 1.28, -0.25], 0.10, [-0.15, 1.56,  0.05], 0.065);
        AddTaperedCapsule([-0.15, 1.56,  0.05], 0.065,[ 0.92, 1.48,  0.22], 0.032);

        const SubTwigs = [
            { A: [-0.82, 1.38, -0.15], B: [-0.45, 1.52,  0.78], Arch: 3, N: 7 }, // 5-Point Maple spray!
            { A: [-0.48, 1.48, -0.05], B: [-0.08, 1.66, -0.78], Arch: 2, N: 7 }, // Lobed Oak spray!
            { A: [-0.05, 1.56,  0.06], B: [ 0.42, 1.62,  0.86], Arch: 1, N: 7 }, // Heart Linden spray!
            { A: [ 0.32, 1.52,  0.12], B: [ 0.78, 1.58, -0.64], Arch: 0, N: 7 }, // Serrated Beech spray!
            { A: [ 0.65, 1.49,  0.18], B: [ 1.32, 1.36,  0.42], Arch: 4, N: 8 }  // Slender Willow tip spray!
        ];
        for (const Tw of SubTwigs)
        {
            AddTaperedCapsule(Tw.A, 0.034, Tw.B, 0.014);
            AddLeafSprayAlongTwig(Tw.A, Tw.B, Tw.N, [0.145, 0.185], Tw.Arch);
        }

        return {
            PresetId,
            Label: "Isolated 5-Template Bough (36 Thin Curved Leaves · 1.2 mm Blade)",
            BoundsMin: [-1.85, -0.05, -1.55],
            BoundsMax: [ 1.85,  2.35,  1.55],
            Capsules,
            LeafInstances
        };
    }

    function AppendFullArborAt(RootOrigin, HeightScale, MixedArchetypes)
    {
        const [Ox, Oy, Oz] = RootOrigin;
        const S = HeightScale;
        const TrunkMid = [Ox + 0.06 * S, Oy + 1.35 * S, Oz - 0.04 * S];
        const TrunkTop = [Ox - 0.03 * S, Oy + 2.25 * S, Oz + 0.03 * S];
        AddTaperedCapsule([Ox, Oy, Oz], 0.21 * S, TrunkMid, 0.15 * S);
        AddTaperedCapsule(TrunkMid, 0.15 * S, TrunkTop, 0.10 * S);

        const LimbCount = 9;
        for (let L = 0; L < LimbCount; ++L)
        {
            const Az = (L / LimbCount) * Math.PI * 2.0 + 0.35;
            const Tier = L % 3;
            const StartY = Oy + (1.48 + Tier * 0.38) * S;
            const Reach  = (1.32 - Tier * 0.18) * S;
            const EndY   = StartY + (0.48 + Tier * 0.24) * S;

            const LStart = [Ox, StartY, Oz];
            const LMid   = [Ox + Math.cos(Az) * Reach * 0.55, StartY + 0.32 * S, Oz + Math.sin(Az) * Reach * 0.55];
            const LEnd   = [Ox + Math.cos(Az) * Reach, EndY, Oz + Math.sin(Az) * Reach];

            AddTaperedCapsule(LStart, 0.082 * S, LMid, 0.048 * S);
            AddTaperedCapsule(LMid,   0.048 * S, LEnd, 0.022 * S);

            const ArchChoice = MixedArchetypes ? (L % LeafArchetypeCount) : ((L + 2) % LeafArchetypeCount);
            AddLeafSprayAlongTwig(LMid, LEnd, 9, [0.115 * S, 0.155 * S], ArchChoice);

            // Two lateral fork twigs per main limb
            for (const ForkSign of [-1.0, 1.0])
            {
                const ForkAz = Az + ForkSign * 0.52;
                const FEnd = [
                    LMid[0] + Math.cos(ForkAz) * Reach * 0.58,
                    LMid[1] + (0.24 + 0.15 * Rand()) * S,
                    LMid[2] + Math.sin(ForkAz) * Reach * 0.58
                ];
                AddTaperedCapsule(LMid, 0.034 * S, FEnd, 0.014 * S);
                AddLeafSprayAlongTwig(LMid, FEnd, 8, [0.105 * S, 0.145 * S], (ArchChoice + (ForkSign > 0 ? 1 : 2)) % LeafArchetypeCount);
            }
        }

        // Crown apex spray
        const ApexTip = [Ox, Oy + 3.35 * S, Oz];
        AddTaperedCapsule(TrunkTop, 0.075 * S, ApexTip, 0.018 * S);
        AddLeafSprayAlongTwig(TrunkTop, ApexTip, 10, [0.11 * S, 0.145 * S], 3);
    }

    if (PresetId === "full-arbor")
    {
        AppendFullArborAt([0.0, 0.0, 0.0], 1.0, true);
        return {
            PresetId,
            Label: `Full Botanical Arbor (${LeafInstances.length} Instanced Leaves · 5 Templates)`,
            BoundsMin: [-2.35, -0.05, -2.35],
            BoundsMax: [ 2.35,  3.85,  2.35],
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
        Label: `Multi-Arbor Avenue (${LeafInstances.length} Thin Leaves · Cone-LOD Near → Far)`,
        BoundsMin: [-4.9, -0.05, -12.2],
        BoundsMax: [ 4.7,  3.75,   3.6],
        Capsules,
        LeafInstances
    };
}

//============================================================================================================================================
//                                  3. EXACT ANALYTIC CAPSULE + THIN-LEAF SDF & RAY EVALUATORS
//============================================================================================================================================

export function EvalTaperedCapsuleSdf(P, Cap)
{
    const Bax = Cap.P1[0] - Cap.P0[0], Bay = Cap.P1[1] - Cap.P0[1], Baz = Cap.P1[2] - Cap.P0[2];
    const Pax = P[0] - Cap.P0[0],      Pay = P[1] - Cap.P0[1],      Paz = P[2] - Cap.P0[2];
    const BaDot = Bax * Bax + Bay * Bay + Baz * Baz;
    const H = Clamp((Pax * Bax + Pay * Bay + Paz * Baz) / Math.max(BaDot, 1e-6), 0.0, 1.0);
    const Dx = Pax - Bax * H, Dy = Pay - Bay * H, Dz = Paz - Baz * H;
    const Radius = Cap.R0 + (Cap.R1 - Cap.R0) * H;
    return Math.hypot(Dx, Dy, Dz) - Radius;
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

                // 1. Exact Wood Capsule SDF
                let DWood = 1e6;
                for (let C = 0; C < Capsules.length; ++C)
                {
                    const Dc = EvalTaperedCapsuleSdf(P, Capsules[C]);
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

                const DStandardSigned = Math.min(DWood, DLeafSigned);
                const DDilated        = Math.min(DWood, Math.abs(DLeafSigned) - 0.54 * VoxelDiag);
                const DUnsigned       = Math.min(Math.max(DWood, -0.05), Math.abs(DLeafSigned));

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

// Exact closed-form visibility of a ray Ro + t*Rd against a tapered wooden branch capsule [P0, R0] -> [P1, R1]
export function EvalRayCapsuleShadow(Ro, Rd, TStart, TEnd, TanCone, Cap)
{
    const Ux = Cap.P1[0] - Cap.P0[0], Uy = Cap.P1[1] - Cap.P0[1], Uz = Cap.P1[2] - Cap.P0[2];
    const Wx = Ro[0] - Cap.P0[0],     Wy = Ro[1] - Cap.P0[1],     Wz = Ro[2] - Cap.P0[2];

    const A = Dot3(Rd, Rd); // 1.0
    const B = Rd[0] * Ux + Rd[1] * Uy + Rd[2] * Uz;
    const C = Ux * Ux + Uy * Uy + Uz * Uz;
    const D = Rd[0] * Wx + Rd[1] * Wy + Rd[2] * Wz;
    const E = Ux * Wx + Uy * Wy + Uz * Wz;
    const Denom = A * C - B * B;

    let S = Denom > 1e-6 ? Clamp((A * E - B * D) / Denom, 0.0, 1.0) : 0.0;
    let T = Clamp(B * S - D, TStart, TEnd);
    S = C > 1e-6 ? Clamp((E + T * B) / C, 0.0, 1.0) : 0.0;
    T = Clamp(B * S - D, TStart, TEnd);

    const Px = Ro[0] + Rd[0] * T - (Cap.P0[0] + Ux * S);
    const Py = Ro[1] + Rd[1] * T - (Cap.P0[1] + Uy * S);
    const Pz = Ro[2] + Rd[2] * T - (Cap.P0[2] + Uz * S);
    const Rad = Cap.R0 + (Cap.R1 - Cap.R0) * S;
    const SignedDist = Math.hypot(Px, Py, Pz) - Rad;
    if (SignedDist <= 0.0) return 0.0;

    const ConeR = Math.max(T * TanCone, 0.0015);
    return Clamp(SignedDist / ConeR, 0.0, 1.0);
}

// Exact Ground-Truth Ray-Traced Shadow (tests every capsule & every thin leaf instance directly without any voxel grid!)
export function EvaluateExactGroundTruthShadow(Ro, Rd, TanCone, CompiledVol, CompiledArchetypes)
{
    const Hit = IntersectRayAabb(Ro, Rd, CompiledVol.BoundsMin, CompiledVol.BoundsMax);
    if (!Hit) return 1.0;
    const TStart = Math.max(0.015, Hit[0]);
    const TEnd = Hit[1];

    let Vis = 1.0;
    const Caps = CompiledVol.SceneSpec.Capsules;
    for (let C = 0; C < Caps.length; ++C)
    {
        Vis = Math.min(Vis, EvalRayCapsuleShadow(Ro, Rd, TStart, TEnd, TanCone, Caps[C]));
        if (Vis <= 0.0) return 0.0;
    }

    const Leaves = CompiledVol.SceneSpec.LeafInstances;
    const Grids2d = CompiledArchetypes.FloatGrids;
    for (let L = 0; L < Leaves.length; ++L)
    {
        Vis = Math.min(Vis, EvalThinLeafRaySegmentShadow(Ro, Rd, TStart, TEnd, TanCone, Leaves[L], Grids2d));
        if (Vis <= 0.0) return 0.0;
    }
    return Vis;
}

// Evaluates any of the 4 SDF Shadow Modes on CPU (matching WGSL shader logic bit-for-bit!)
export function EvaluateSdfShadowMode(Ro, Rd, TanCone, Mode, CompiledVol, CompiledArchetypes)
{
    const BMin = CompiledVol.BoundsMin;
    const BMax = CompiledVol.BoundsMax;
    const Hit = IntersectRayAabb(Ro, Rd, BMin, BMax);
    if (!Hit) return { Visibility: 1.0, Steps: 0 };

    let T = Math.max(0.015, Hit[0]);
    const TEnd = Hit[1];
    const VRes = MacroVoxelRes;

    // Modes 0 & 1: Sphere/Cone-tracing the 32^3 Signed or Dilated Voxel SDF
    if (Mode === SdfEvaluationMode.StandardVoxel3d || Mode === SdfEvaluationMode.DilatedVoxel3d)
    {
        const Chan = Mode === SdfEvaluationMode.StandardVoxel3d ? 0 : 1;
        let Vis = 1.0;
        let Steps = 0;
        for (let Iter = 0; Iter < 56 && T < TEnd; ++Iter)
        {
            ++Steps;
            const P = [Ro[0] + Rd[0] * T, Ro[1] + Rd[1] * T, Ro[2] + Rd[2] * T];
            const D = Sample3dVolumeTrilinear(CompiledVol.ComparisonFloat3d, VRes, BMin, BMax, P, Chan);
            if (D <= 0.0005) return { Visibility: 0.0, Steps };
            const ConeR = Math.max(T * TanCone, 0.002);
            Vis = Math.min(Vis, Clamp(D / ConeR, 0.0, 1.0));
            T += Math.max(D * 0.85, 0.028);
        }
        return { Visibility: Vis, Steps };
    }

    // Mode 2: Non-Instanced 32^3 Unsigned Distance + Sub-Voxel Opacity
    if (Mode === SdfEvaluationMode.OpacityUdf3d)
    {
        let Vis = 1.0;
        let Steps = 0;
        const StepMin = CompiledVol.VoxelDiag * 0.35;
        for (let Iter = 0; Iter < 56 && T < TEnd; ++Iter)
        {
            ++Steps;
            const P = [Ro[0] + Rd[0] * T, Ro[1] + Rd[1] * T, Ro[2] + Rd[2] * T];
            const DWood = Sample3dVolumeTrilinear(CompiledVol.MacroFloat3d, VRes, BMin, BMax, P, 0);
            if (DWood <= 0.0) return { Visibility: 0.0, Steps };
            const ConeR = Math.max(T * TanCone, 0.002);
            Vis = Math.min(Vis, Clamp(DWood / ConeR, 0.0, 1.0));

            const UDist = Sample3dVolumeTrilinear(CompiledVol.ComparisonFloat3d, VRes, BMin, BMax, P, 2);
            const Cov   = Sample3dVolumeTrilinear(CompiledVol.ComparisonFloat3d, VRes, BMin, BMax, P, 3);
            if (UDist < CompiledVol.VoxelDiag * 0.72 && Cov > 0.005)
            {
                Vis *= Math.exp(-Cov * 3.2 * (StepMin / CompiledVol.VoxelDiag));
                if (Vis < 0.01) return { Visibility: 0.0, Steps };
                T += StepMin;
            }
            else
            {
                T += Math.max((UDist - CompiledVol.VoxelDiag * 0.5) * 0.8, StepMin);
            }
        }
        return { Visibility: Vis, Steps };
    }

    // 🟢 Mode 3: Two-Level 16^3 Canopy Brick + 5 x 2D Thin-Sheet Extrusion SDF + Continuous Cone-Width LOD!
    let WoodVis = 1.0;
    let LeafVis = 1.0;
    let Steps = 0;
    let EvaluatedWoodExact = false;
    const Span = [BMax[0] - BMin[0], BMax[1] - BMin[1], BMax[2] - BMin[2]];
    const BrickSz = CompiledVol.BrickSize;
    const Leaves = CompiledVol.SceneSpec.LeafInstances;
    const Caps = CompiledVol.SceneSpec.Capsules;
    const Grids2d = CompiledArchetypes.FloatGrids;

    for (let Iter = 0; Iter < 48 && T < TEnd; ++Iter)
    {
        ++Steps;
        const P = [Ro[0] + Rd[0] * T, Ro[1] + Rd[1] * T, Ro[2] + Rd[2] * T];
        const ConeR = Math.max(T * TanCone, 0.0015);

        const DWoodMacro = Sample3dVolumeTrilinear(CompiledVol.MacroFloat3d, VRes, BMin, BMax, P, 0);
        if (!EvaluatedWoodExact && DWoodMacro < CompiledVol.VoxelDiag * 0.95 + ConeR)
        {
            EvaluatedWoodExact = true;
            for (let C = 0; C < Caps.length; ++C)
            {
                WoodVis = Math.min(WoodVis, EvalRayCapsuleShadow(Ro, Rd, Math.max(0.015, Hit[0]), TEnd, TanCone, Caps[C]));
                if (WoodVis <= 0.0) return { Visibility: 0.0, Steps };
            }
        }

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
            if (WoodVis * LeafVis <= 1e-4) return { Visibility: 0.0, Steps };
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
    const Caps = CompiledVol.SceneSpec.Capsules;

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

            // Identify rays occluded purely by thick wood vs. rays occluded strictly by thin leaves!
            let WoodVisOnly = 1.0;
            for (let C = 0; C < Caps.length; ++C)
            {
                WoodVisOnly = Math.min(WoodVisOnly, EvalRayCapsuleShadow(Ro, SunDir, 0.015, 30.0, TanCone, Caps[C]));
            }
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
