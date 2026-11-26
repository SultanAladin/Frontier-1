/*==========================================================================================================================================
                                                        TERRAINLAYERSTACK.JS
==========================================================================================================================================*/
// Fast Composable SDF Terrain Layer Stack, Boundary Falloff Evaluators, Catmull-Rom Spline Landforms,
// Real-Time SDF Sculpting (2.5D + True 3D Overhang/Cave SDF), Thin-Slab Triangle Mesher, and 3 Demo Open-World Masks.

import { ComputeRegionMaskSdf2D, MakeTerrainUid } from './WorldMask2D.js';
import {
    createDefaultFractureParams,
    buildFracturedFormation,
    bakeFracturedFormationQefMesh
} from './RockCliff/PureSdfFractureEngine.js';

const RockFormationQefCache = new WeakMap();

export const Clamp = (V, Lo, Hi) => (V < Lo ? Lo : V > Hi ? Hi : V);
export const Mix = (A, B, T) => A + (B - A) * T;
export const Smoothstep = (E0, E1, X) =>
{
    const Span = Math.max(1e-5, E1 - E0);
    const T = Clamp((X - E0) / Span, 0.0, 1.0);
    return T * T * (3.0 - 2.0 * T);
};

/*==========================================================================================================================================
                                            DETERMINISTIC SPATIAL NOISE & FRACTALS
==========================================================================================================================================*/
function Hash2(Ix, Iz, Seed)
{
    let H = Math.imul(Ix | 0, 374761393) ^ Math.imul(Iz | 0, 668265263) ^ Math.imul(Seed | 0, 1442695041);
    H = Math.imul(H ^ (H >>> 13), 1274126177);
    return ((H ^ (H >>> 16)) >>> 0) / 4294967295.0;
}

function GradNoise2D(X, Z, Seed = 4029)
{
    const X0 = Math.floor(X), Z0 = Math.floor(Z);
    const Fx = X - X0, Fz = Z - Z0;
    const Ux = Fx * Fx * Fx * (Fx * (Fx * 6.0 - 15.0) + 10.0);
    const Uz = Fz * Fz * Fz * (Fz * (Fz * 6.0 - 15.0) + 10.0);

    const DotGrad = (Gx, Gz, Dx, Dz) =>
    {
        const Angle = Hash2(Gx, Gz, Seed) * 6.28318530718;
        return Math.cos(Angle) * Dx + Math.sin(Angle) * Dz;
    };

    const N00 = DotGrad(X0, Z0, Fx, Fz);
    const N10 = DotGrad(X0 + 1, Z0, Fx - 1.0, Fz);
    const N01 = DotGrad(X0, Z0 + 1, Fx, Fz - 1.0);
    const N11 = DotGrad(X0 + 1, Z0 + 1, Fx - 1.0, Fz - 1.0);

    return Mix(Mix(N00, N10, Ux), Mix(N01, N11, Ux), Uz) * 0.7071 + 0.5;
}

export function Fbm2D(X, Z, Octaves = 5, Gain = 0.5, Lacunarity = 2.02, Seed = 4029)
{
    let Sum = 0.0, Amp = 0.5, Freq = 1.0, Norm = 0.0;
    for (let I = 0; I < Octaves; ++I)
    {
        Sum += Amp * GradNoise2D(X * Freq, Z * Freq, Seed + I * 131);
        Norm += Amp;
        Amp *= Gain;
        Freq *= Lacunarity;
    }
    return Clamp(Sum / Math.max(1e-6, Norm), 0.0, 1.0);
}

/**
 * Musgrave-inspired Hybrid Multifractal: smooth alluvial valleys with crisp craggy high peaks.
 */
export function HybridMultifractal2D(X, Z, Octaves = 6, Gain = 0.52, Offset = 0.25, Seed = 4029)
{
    let Signal = GradNoise2D(X, Z, Seed) + Offset;
    let Result = Signal;
    let Weight = Clamp(Signal * 0.85, 0.0, 1.0);
    let Freq = 2.03;
    let Amp = Gain;
    let Norm = 1.0 + Offset;

    for (let I = 1; I < Octaves; ++I)
    {
        Weight = Clamp(Weight, 0.0, 1.0);
        Signal = (GradNoise2D(X * Freq, Z * Freq, Seed + I * 173) + Offset) * Amp;
        Result += Weight * Signal;
        Weight *= Signal / Math.max(0.12, Amp) * 0.72;
        Norm += Amp * (1.0 + Offset) * 0.65;
        Freq *= 2.03;
        Amp *= Gain;
    }
    return Clamp((Result - Offset * 0.8) / Math.max(1e-4, Norm * 0.78), 0.0, 1.15);
}

/**
 * Ridged Multifractal: sharp tectonic arête ridges & hogback crests.
 */
export function RidgedMultifractal2D(X, Z, Octaves = 6, Gain = 0.55, Sharpness = 0.68, Seed = 4029)
{
    let Sum = 0.0, Weight = 1.0, Freq = 1.0, Amp = 0.5, Norm = 0.0;
    const Exp = Mix(1.1, 2.5, Clamp(Sharpness, 0.0, 1.0));
    for (let I = 0; I < Octaves; ++I)
    {
        const N = GradNoise2D(X * Freq, Z * Freq, Seed + I * 211) * 2.0 - 1.0;
        let Ridge = 1.0 - Math.abs(N);
        Ridge = Math.pow(Math.max(0.0, Ridge), Exp);
        Ridge *= Weight;
        Weight = Clamp(Ridge * 1.65, 0.0, 1.0);
        Sum += Ridge * Amp;
        Norm += Amp;
        Freq *= 2.07;
        Amp *= Gain;
    }
    return Clamp(Sum / Math.max(1e-5, Norm * 0.78), 0.0, 1.15);
}

/**
 * Voronoi / Cellular Landform: supports 0=Plateaus/Blocks, 1=Fracture Ridges (F2-F1), 2=Volcanic Craters, 3=Domes (1-F1).
 */
export function VoronoiCellular2D(X, Z, Variant = 0, Jitter = 0.82, Sharpness = 0.65, Seed = 4029)
{
    const Cx = Math.floor(X), Cz = Math.floor(Z);
    const Fx = X - Cx, Fz = Z - Cz;
    let D1 = 1e9, D2 = 1e9, CellVal = 0.5;

    for (let Dz = -1; Dz <= 1; ++Dz)
    {
        for (let Dx = -1; Dx <= 1; ++Dx)
        {
            const Nx = Cx + Dx, Nz = Cz + Dz;
            const Rx = (Hash2(Nx, Nz, Seed) - 0.5) * Jitter + 0.5;
            const Rz = (Hash2(Nx, Nz, Seed + 59) - 0.5) * Jitter + 0.5;
            const Dist = Math.hypot(Dx + Rx - Fx, Dz + Rz - Fz);
            if (Dist < D1)
            {
                D2 = D1;
                D1 = Dist;
                CellVal = Hash2(Nx, Nz, Seed + 199);
            }
            else if (Dist < D2)
            {
                D2 = Dist;
            }
        }
    }

    const Edge = Clamp(D2 - D1, 0.0, 1.0);
    if (Variant === 0)
    {
        // Stepped Basalt / Sedimentary Mesa Blocks with clean rim bevel
        const Rim = Smoothstep(0.02, Mix(0.28, 0.05, Sharpness), Edge);
        return (0.35 + 0.65 * CellVal) * Rim;
    }
    if (Variant === 1)
    {
        // Tectonic Polygonal Ridges (F2 - F1)
        return Math.pow(Clamp(1.0 - Edge * 1.45, 0.0, 1.0), Mix(1.0, 2.6, Sharpness));
    }
    if (Variant === 2)
    {
        // Caldera / Impact Crater Bowls
        const R = D1 * 1.65;
        const Bowl = -(1.0 - Smoothstep(0.0, 0.72, R)) * (1.0 - R * R * 0.75) * 0.65;
        const Rim = Math.exp(-Math.pow((R - 0.74) * 5.0, 2.0)) * 0.55;
        return Clamp(0.38 + Bowl + Rim, 0.0, 1.0);
    }
    return Math.pow(Clamp(1.0 - D1 * 1.15, 0.0, 1.0), Mix(0.7, 1.8, Sharpness));
}

/**
 * Billow / Folded Hills & Directional Dunes.
 */
export function BillowFolded2D(X, Z, Octaves = 5, DuneMix = 0.0, AzimuthRad = 0.4, Seed = 4029)
{
    let Sum = 0.0, Amp = 0.5, Freq = 1.0, Norm = 0.0;
    for (let I = 0; I < Octaves; ++I)
    {
        const N = Math.abs(GradNoise2D(X * Freq, Z * Freq, Seed + I * 89) * 2.0 - 1.0);
        Sum += N * Amp;
        Norm += Amp;
        Freq *= 2.02;
        Amp *= 0.5;
    }
    const Billow = Sum / Math.max(1e-5, Norm);
    if (DuneMix <= 0.01) return Billow;

    const Warp = GradNoise2D(X * 0.7, Z * 0.7, Seed + 503) - 0.5;
    const Phase = (((X * Math.cos(AzimuthRad) + Z * Math.sin(AzimuthRad)) * 1.8 + Warp * 0.9) % 1.0 + 1.0) % 1.0;
    const Dune = Smoothstep(0.0, 0.72, Phase) * (1.0 - Smoothstep(0.72, 0.97, Phase));
    return Mix(Billow, Dune, Clamp(DuneMix, 0.0, 1.0));
}

/*==========================================================================================================================================
                                      PER-LAYER EDGE -> CENTER BOUNDARY FALLOFF SYSTEM
==========================================================================================================================================*/
export const BOUNDARY_FALLOFF_MODES = [
    ['Soft Edge → Strong Center (Smooth)', 'soft'],
    ['None — Full Cliff at Edge', 'none'],
    ['Continental Power (Wide Plain → Peak)', 'continental'],
    ['Plateau Rim (Steep Escarpment)', 'plateau'],
    ['Coastal Rim Ring (Strong at Shore)', 'rim'],
    ['Bounds Box Feather (Rect Frame)', 'bounds'],
    ['Custom Curve Profile', 'custom']
];

export const COMBINE_MODES = [
    ['Add (Uplift)', 'add'],
    ['Subtract (Carve)', 'sub'],
    ['Max (Union)', 'max'],
    ['Min (Clip)', 'min'],
    ['Blend (Lerp)', 'lerp']
];

export const FALLOFF_CURVE_DEFAULT_POINTS = [
    [0.0, 0.0],
    [0.35, 0.28],
    [0.72, 0.84],
    [1.0, 1.0]
];

/**
 * Evaluates a layer's Boundary Falloff weight in [0..1] at a point given:
 * - `InlandDistM`: exact metric distance inland from the 2D mask coastline (`max(0, -sdf2D)`)
 * - `BoxEdgeDistM`: exact metric distance inward from the Terrain Bounds Box edges
 * - `FalloffSpec`: `{ mode, distance, edgeFloor, points }`
 */
export function EvaluateBoundaryFalloff(InlandDistM, BoxEdgeDistM, FalloffSpec)
{
    if (!FalloffSpec || FalloffSpec.mode === 'none')
    {
        return 1.0; // Full strength right to the cliff/coastline edge!
    }

    const Mode = FalloffSpec.mode || 'soft';
    const DistSpan = Math.max(4.0, Number(FalloffSpec.distance) || 260.0);
    const EdgeFloor = Clamp(Number(FalloffSpec.edgeFloor) || 0.0, 0.0, 1.0);
    const Metric = Mode === 'bounds' ? Math.max(0.0, BoxEdgeDistM) : Math.max(0.0, InlandDistM);
    const T = Clamp(Metric / DistSpan, 0.0, 1.0);

    let Shape = 1.0;
    switch (Mode)
    {
        case 'soft':
        case 'bounds':
            Shape = T * T * (3.0 - 2.0 * T);
            break;
        case 'continental':
            Shape = Math.pow(T, 2.25);
            break;
        case 'plateau':
            Shape = 1.0 - Math.pow(1.0 - T, 3.6);
            break;
        case 'rim':
        {
            // Rises quickly from shore to a coastal ridge crest at ~30% of falloff distance, then relaxes inland
            const Rise = Smoothstep(0.0, 0.28, T);
            const Decay = 1.0 - 0.72 * Smoothstep(0.32, 1.0, T);
            Shape = Rise * Decay;
            return Clamp(Mix(EdgeFloor, 1.0, Shape), 0.0, 1.0);
        }
        case 'custom':
        {
            const Pts = FalloffSpec.points || FALLOFF_CURVE_DEFAULT_POINTS;
            Shape = T;
            for (let I = 0; I < Pts.length - 1; ++I)
            {
                const P0 = Pts[I], P1 = Pts[I + 1];
                if (T <= P1[0] || I === Pts.length - 2)
                {
                    const Local = Clamp((T - P0[0]) / Math.max(1e-4, P1[0] - P0[0]), 0.0, 1.0);
                    const H = Local * Local * (3.0 - 2.0 * Local);
                    Shape = Mix(P0[1], P1[1], H);
                    break;
                }
            }
            return Clamp(Shape, 0.0, 1.0);
        }
        default:
            Shape = T;
            break;
    }

    return Clamp(EdgeFloor + (1.0 - EdgeFloor) * Shape, 0.0, 1.0);
}

/*==========================================================================================================================================
                                        COMPOSABLE LAYER STACK SPECIFICATION & FACTORY
==========================================================================================================================================*/
export const LAYER_TYPE_META = {
    multifractal: { label: 'Hybrid Multifractal', badge: 'FRACTAL', glyph: 'Mountain', accent: '#e8c27a' },
    ridged:       { label: 'Ridged Mountain Noise', badge: 'RIDGE', glyph: 'Activity', accent: '#f0935b' },
    voronoi:      { label: 'Voronoi / Mesa Blocks', badge: 'CELLULAR', glyph: 'Boxes', accent: '#5ac8fa' },
    billow:       { label: 'Billow Hills & Dunes', badge: 'FOLDED', glyph: 'Wind', accent: '#a3d977' },
    terrace:      { label: 'Terrace & Strata', badge: 'STRATA', glyph: 'Layers', accent: '#d6a2e8' },
    spline:         { label: 'Spline Landform', badge: 'SPLINE', glyph: 'Spline', accent: '#3ec46d' },
    sculpt:         { label: 'SDF Sculpt Layer', badge: 'SCULPT', glyph: 'Brush', accent: '#ff7b7b' },
    rock_formation: { label: 'Pure-SDF Fractured Rock', badge: 'SDF ROCK', glyph: 'Boxes', accent: '#eff4cf' }
};

export function CreateTerrainLayer(Type = 'multifractal', Overrides = {})
{
    const Meta = LAYER_TYPE_META[Type] || LAYER_TYPE_META.multifractal;
    const Base = {
        id: Overrides.id || MakeTerrainUid('lyr'),
        type: Type,
        name: Overrides.name || Meta.label,
        enabled: Overrides.enabled !== false,
        opacity: Overrides.opacity ?? 1.0,
        combine: Overrides.combine || 'add',
        seed: Overrides.seed ?? 4029,
        falloff: {
            mode: Overrides.falloff?.mode ?? 'soft',
            distance: Overrides.falloff?.distance ?? 280.0,
            edgeFloor: Overrides.falloff?.edgeFloor ?? 0.0,
            points: (Overrides.falloff?.points || FALLOFF_CURVE_DEFAULT_POINTS).map(P => P.slice())
        },
        // Optional elevation/slope range filter
        heightMask: {
            enabled: !!Overrides.heightMask?.enabled,
            minElev: Overrides.heightMask?.minElev ?? 0.0,
            maxElev: Overrides.heightMask?.maxElev ?? 300.0,
            minSlope: Overrides.heightMask?.minSlope ?? 0.0,
            maxSlope: Overrides.heightMask?.maxSlope ?? 90.0
        }
    };

    if (Type === 'multifractal')
    {
        return {
            ...Base,
            amplitude: Overrides.amplitude ?? 185.0,
            scale: Overrides.scale ?? 2.2,
            octaves: Overrides.octaves ?? 6,
            gain: Overrides.gain ?? 0.52,
            warp: Overrides.warp ?? 0.32,
            valleyOffset: Overrides.valleyOffset ?? 0.24
        };
    }
    if (Type === 'ridged')
    {
        return {
            ...Base,
            amplitude: Overrides.amplitude ?? 135.0,
            scale: Overrides.scale ?? 2.6,
            octaves: Overrides.octaves ?? 6,
            sharpness: Overrides.sharpness ?? 0.72,
            azimuth: Overrides.azimuth ?? 32.0,
            warp: Overrides.warp ?? 0.38
        };
    }
    if (Type === 'voronoi')
    {
        return {
            ...Base,
            variant: Overrides.variant ?? 0, // 0=Mesa Blocks, 1=Polygonal Ridges, 2=Caldera Craters, 3=Domes
            amplitude: Overrides.amplitude ?? 140.0,
            scale: Overrides.scale ?? 3.1,
            jitter: Overrides.jitter ?? 0.82,
            sharpness: Overrides.sharpness ?? 0.74,
            warp: Overrides.warp ?? 0.22
        };
    }
    if (Type === 'billow')
    {
        return {
            ...Base,
            amplitude: Overrides.amplitude ?? 68.0,
            scale: Overrides.scale ?? 3.4,
            octaves: Overrides.octaves ?? 5,
            duneMix: Overrides.duneMix ?? 0.25,
            azimuth: Overrides.azimuth ?? 45.0
        };
    }
    if (Type === 'terrace')
    {
        return {
            ...Base,
            bedCount: Overrides.bedCount ?? 9,
            strength: Overrides.strength ?? 0.76,
            dipDeg: Overrides.dipDeg ?? 4.0,
            strikeDeg: Overrides.strikeDeg ?? 25.0
        };
    }
    if (Type === 'spline')
    {
        return {
            ...Base,
            splineMode: Overrides.splineMode ?? 'grade', // 'grade' | 'arch3d' | 'ridge' | 'canyon' | 'river' | 'lake'
            width: Overrides.width ?? 105.0,
            amount: Overrides.amount ?? 32.0, // Vertical offset or arch tube thickness (m)
            bankSmooth: Overrides.bankSmooth ?? 0.62,
            meander: Overrides.meander ?? 0.14,
            points: (Overrides.points || []).map(P => P.slice())
        };
    }
    if (Type === 'rock_formation')
    {
        return {
            ...Base,
            worldX: Overrides.worldX ?? 0.0,
            worldZ: Overrides.worldZ ?? 0.0,
            worldScaleM: Overrides.worldScaleM ?? 165.0,
            yawDeg: Overrides.yawDeg ?? 0.0,
            embedDepthM: Overrides.embedDepthM ?? 8.0,
            fractureParams: {
                ...createDefaultFractureParams(Overrides.fractureParams?.presetId || 'cracked-monolith'),
                ...(Overrides.fractureParams || {})
            }
        };
    }
    // 'sculpt'
    return {
        ...Base,
        deltaRes: Overrides.deltaRes || 0,
        deltas: Overrides.deltas || null, // Float32Array(Nx * Nz)
        strokeCount: Overrides.strokeCount || 0
    };
}

/*==========================================================================================================================================
                                        CATMULL-ROM 3D SPLINE DISTANCE & ELEVATION EVALUATOR
==========================================================================================================================================*/
function CatmullRomScalar(V0, V1, V2, V3, T)
{
    const T2 = T * T, T3 = T2 * T;
    return 0.5 * ((2 * V1) + (-V0 + V2) * T + (2 * V0 - 5 * V1 + 4 * V2 - V3) * T2 + (-V0 + 3 * V1 - 3 * V2 + V3) * T3);
}

export function SampleCatmullRomPolyline(ControlPoints, SubdivsPerSeg = 7)
{
    if (!ControlPoints || ControlPoints.length < 2) return ControlPoints || [];
    const N = ControlPoints.length;
    const Out = [];
    for (let I = 0; I < N - 1; ++I)
    {
        const P0 = ControlPoints[Math.max(0, I - 1)];
        const P1 = ControlPoints[I];
        const P2 = ControlPoints[I + 1];
        const P3 = ControlPoints[Math.min(N - 1, I + 2)];
        const HasY = Number.isFinite(P1[2]) && Number.isFinite(P2[2]);
        const Y0 = Number.isFinite(P0[2]) ? P0[2] : (P1[2] ?? 0);
        const Y1 = P1[2] ?? 0;
        const Y2 = P2[2] ?? 0;
        const Y3 = Number.isFinite(P3[2]) ? P3[2] : Y2;

        const Steps = (I === N - 2) ? SubdivsPerSeg : SubdivsPerSeg - 1;
        for (let S = 0; S <= Steps; ++S)
        {
            const T = S / SubdivsPerSeg;
            const X = CatmullRomScalar(P0[0], P1[0], P2[0], P3[0], T);
            const Z = CatmullRomScalar(P0[1], P1[1], P2[1], P3[1], T);
            if (HasY)
            {
                const Y = CatmullRomScalar(Y0, Y1, Y2, Y3, T);
                Out.push([X, Z, Y]);
            }
            else
            {
                Out.push([X, Z]);
            }
        }
    }
    return Out;
}

function PointToPolylineDist(Wx, Wz, Poly)
{
    let BestSq = 1e18;
    let BestAlong = 0.0;
    let BestY = undefined;
    const Segs = Poly.length - 1;
    if (Segs < 1) return { dist: 1e9, along: 0, elevY: undefined };

    for (let I = 0; I < Segs; ++I)
    {
        const Ax = Poly[I][0], Az = Poly[I][1];
        const Bx = Poly[I + 1][0], Bz = Poly[I + 1][1];
        const Ex = Bx - Ax, Ez = Bz - Az;
        const LenSq = Math.max(1e-5, Ex * Ex + Ez * Ez);
        const T = Clamp(((Wx - Ax) * Ex + (Wz - Az) * Ez) / LenSq, 0.0, 1.0);
        const Dx = Wx - (Ax + Ex * T), Dz = Wz - (Az + Ez * T);
        const DSq = Dx * Dx + Dz * Dz;
        if (DSq < BestSq)
        {
            BestSq = DSq;
            BestAlong = (I + T) / Segs;
            if (Poly[I][2] !== undefined && Poly[I + 1][2] !== undefined)
            {
                BestY = Mix(Poly[I][2], Poly[I + 1][2], T);
            }
        }
    }
    return { dist: Math.sqrt(BestSq), along: BestAlong, elevY: BestY };
}

/*==========================================================================================================================================
                                    TERRAIN REGION LAYER-STACK & THIN-SLAB SDF EVALUATOR
==========================================================================================================================================*/
/**
 * Evaluates a Terrain Region's 2D Mask SDF + Thin Base Slab + Composable Layer Stack + Falloff Heatmap!
 * Stores results in `Region.runtime` and marks `Region.dirty = false`.
 */
export function EvaluateTerrainRegion(World, Region, PreviewLayerIdForFalloff = null)
{
    const B = Region.bounds;
    const BaseRes = Clamp(Region.resolution | 0, 48, 256);
    const Aspect = B.width / Math.max(1.0, B.depth);
    const Nx = Aspect >= 1.0 ? BaseRes : Math.max(32, Math.round(BaseRes * Aspect));
    const Nz = Aspect >= 1.0 ? Math.max(32, Math.round(BaseRes / Aspect)) : BaseRes;
    const Count = Nx * Nz;
    const StepX = B.width / Math.max(1, Nx - 1);
    const StepZ = B.depth / Math.max(1, Nz - 1);

    // 1. Exact O(N) 2D Euclidean Signed Distance Field from the 2D Mask
    const { coverage: MaskCov, sdf2D: Sdf2D, hasLand: HasLand } = ComputeRegionMaskSdf2D(World, Region, Nx, Nz);

    const Heights = new Float32Array(Count);
    const FalloffPreview = new Float32Array(Count);
    const WaterChannel = new Float32Array(Count); // River/lake spline water tint mask

    const SeaLevel = World.seaLevel ?? 0.0;
    const SlabBottom = SeaLevel - Math.max(1.5, Number(Region.slabThickness) || 6.5);
    const BaseTop = SeaLevel + (Number(Region.baseElevation) ?? 3.0);
    const ShoreBevel = Math.max(0.0, Number(Region.shorelineBevel) ?? 16.0);

    if (!HasLand)
    {
        Heights.fill(SlabBottom);
        Region.runtime = {
            nx: Nx, nz: Nz, stepX: StepX, stepZ: StepZ,
            slabBottom: SlabBottom, baseTop: BaseTop,
            coverage: MaskCov, sdf2D: Sdf2D, heights: Heights,
            falloffMap: FalloffPreview, waterChannel: WaterChannel,
            hasLand: false, maxHeight: SlabBottom, landAreaKm2: 0
        };
        Region.dirty = false;
        return Region.runtime;
    }

    // 2. Initialize Thin Flat SDF Base Surface inside 2D Mask (zero wasted deep-underground bulk!)
    let LandCells = 0;
    for (let I = 0; I < Count; ++I)
    {
        const D2 = Sdf2D[I];
        if (D2 <= 0.0)
        {
            LandCells++;
            if (ShoreBevel > 0.5)
            {
                const ShoreT = Clamp(-D2 / ShoreBevel, 0.0, 1.0);
                const SmoothShore = ShoreT * ShoreT * (3.0 - 2.0 * ShoreT);
                Heights[I] = Mix(SeaLevel - 0.35, BaseTop, SmoothShore);
            }
            else
            {
                Heights[I] = BaseTop;
            }
        }
        else if (D2 <= Math.max(StepX, StepZ) * 1.6)
        {
            // Narrow coastal skirt tapering cleanly down to the thin slab floor
            const SkirtT = Clamp(D2 / (Math.max(StepX, StepZ) * 1.6), 0.0, 1.0);
            Heights[I] = Mix(SeaLevel - 0.35, SlabBottom, SkirtT);
        }
        else
        {
            Heights[I] = SlabBottom;
        }
    }

    // 3. Evaluate Composable Layer Stack bottom-to-top!
    const RefSpan = Region.worldFrequencySpan > 0
        ? Number(Region.worldFrequencySpan)
        : Math.max(400.0, Math.min(B.width, B.depth));

    // Identify top interactive sculpt layer so we can snapshot `preSculptHeights` for O(brush-rect) live sculpting!
    let TopLiveSculptIdx = -1;
    for (let SIdx = Region.stack.length - 1; SIdx >= 0; --SIdx)
    {
        const Cand = Region.stack[SIdx];
        if (Cand && Cand.enabled !== false && Cand.type === 'sculpt' && Cand.name !== 'Erosion Baked Surface')
        {
            TopLiveSculptIdx = SIdx;
            break;
        }
    }
    let PreSculptHeights = null;

    for (let LIdx = 0; LIdx < Region.stack.length; ++LIdx)
    {
        if (LIdx === TopLiveSculptIdx)
        {
            PreSculptHeights = new Float32Array(Heights);
        }
        const Layer = Region.stack[LIdx];
        const RecordFalloff = (Layer.id === PreviewLayerIdForFalloff) || (!PreviewLayerIdForFalloff && LIdx === Region.stack.length - 1);
        if (!Layer || Layer.enabled === false) continue;

        const Opacity = Clamp(Number(Layer.opacity) ?? 1.0, 0.0, 1.0);
        if (Opacity <= 1e-4 && !RecordFalloff) continue;

        const Type = Layer.type;
        const Seed = (Layer.seed | 0) || 4029;
        const Combine = Layer.combine || 'add';

        // Precompute spline polyline & bounding box when evaluating a Spline Landform layer
        let Poly = null;
        let PolyBounds = null;
        if (Type === 'spline')
        {
            if (!Layer.points || Layer.points.length < 2) continue;
            // Ensure every spline control knot has an explicit 3D elevation Y [wx, wz, wy] so the 3D Gizmo can move it Up/Down!
            for (const Pt of Layer.points)
            {
                if (!Number.isFinite(Pt[2]))
                {
                    const Gx = Clamp(Math.round((Pt[0] - B.x) / StepX), 0, Nx - 1);
                    const Gz = Clamp(Math.round((Pt[1] - B.z) / StepZ), 0, Nz - 1);
                    Pt[2] = Math.round(Heights[Gz * Nx + Gx] * 10) / 10;
                }
            }
            // 'arch3d' & 'tunnel' are extracted as true 3D volumetric geometry (rock bridge & mountain tunnel bore)!
            if (Layer.splineMode === 'arch3d' || Layer.splineMode === 'tunnel') continue;

            Poly = SampleCatmullRomPolyline(Layer.points, 8);
            const MaxReach = (Number(Layer.width) || 90.0) * 2.5 + 45.0;
            let PMinX = 1e9, PMinZ = 1e9, PMaxX = -1e9, PMaxZ = -1e9;
            for (const Pt of Poly)
            {
                if (Pt[0] < PMinX) PMinX = Pt[0];
                if (Pt[0] > PMaxX) PMaxX = Pt[0];
                if (Pt[1] < PMinZ) PMinZ = Pt[1];
                if (Pt[1] > PMaxZ) PMaxZ = Pt[1];
            }
            PolyBounds = [PMinX - MaxReach, PMinZ - MaxReach, PMaxX + MaxReach, PMaxZ + MaxReach];
        }

        // Ensure sculpt layer buffer matches grid resolution (bilinearly resampling if resolution changed)
        if (Type === 'sculpt')
        {
            EnsureSculptBufferMatches(Layer, Nx, Nz);
        }

        const AzRad = ((Number(Layer.azimuth) || 0.0) * Math.PI) / 180.0;
        const CosAz = Math.cos(AzRad), SinAz = Math.sin(AzRad);
        const DipRad = ((Number(Layer.dipDeg) || 0.0) * Math.PI) / 180.0;
        const StrikeRad = ((Number(Layer.strikeDeg) || 0.0) * Math.PI) / 180.0;
        const TanDip = Math.tan(DipRad);
        const DipDirX = -Math.sin(StrikeRad), DipDirZ = Math.cos(StrikeRad);

        for (let Z = 0; Z < Nz; ++Z)
        {
            const Wz = B.z + Z * StepZ;
            const BoxDz = Math.min(Wz - B.z, B.z + B.depth - Wz);
            const Row = Z * Nx;

            for (let X = 0; X < Nx; ++X)
            {
                const Idx = Row + X;
                const D2 = Sdf2D[Idx];
                if (D2 > StepX * 1.1) continue; // Outside 2D mask — zero wasted computation or ocean contamination!

                const Wx = B.x + X * StepX;
                const BoxEdgeDist = Math.min(BoxDz, Wx - B.x, B.x + B.width - Wx);
                const InlandDist = Math.max(0.0, -D2);

                const FallW = EvaluateBoundaryFalloff(InlandDist, BoxEdgeDist, Layer.falloff);
                if (RecordFalloff) FalloffPreview[Idx] = FallW;

                let MaskW = FallW * Opacity;
                if (MaskW <= 1e-4) continue;

                const CurH = Heights[Idx];
                if (Layer.heightMask?.enabled)
                {
                    const Hm = Layer.heightMask;
                    const EWeight = Smoothstep(Hm.minElev - 15.0, Hm.minElev + 5.0, CurH) *
                                    (1.0 - Smoothstep(Hm.maxElev - 5.0, Hm.maxElev + 15.0, CurH));
                    MaskW *= EWeight;
                    if (MaskW <= 1e-4) continue;
                }

                if (Type === 'sculpt')
                {
                    Heights[Idx] = Math.max(SlabBottom + 0.5, CurH + Layer.deltas[Idx] * MaskW);
                    continue;
                }

                if (Type === 'terrace')
                {
                    const Beds = Math.max(2.0, Number(Layer.bedCount) || 8.0);
                    const ReliefRef = Math.max(40.0, Number(Region.maxRelief) || 240.0);
                    const StratM = CurH + (Wx * DipDirX + Wz * DipDirZ) * TanDip;
                    const Ph = (StratM / ReliefRef) * Beds;
                    const Fl = Math.floor(Ph);
                    const Fr = Ph - Fl;
                    const SteppedFr = Mix(Fr, Smoothstep(0.16, 0.84, Fr), Clamp(Number(Layer.strength) || 0.75, 0.0, 0.98));
                    const TargetH = ((Fl + SteppedFr) / Beds) * ReliefRef - (Wx * DipDirX + Wz * DipDirZ) * TanDip;
                    Heights[Idx] = Math.max(SlabBottom + 0.5, Mix(CurH, TargetH, MaskW * Clamp(Number(Layer.strength) || 0.75, 0.0, 1.0)));
                    continue;
                }

                if (Type === 'spline')
                {
                    if (Wx < PolyBounds[0] || Wx > PolyBounds[2] || Wz < PolyBounds[1] || Wz > PolyBounds[3]) continue;
                    let Qx = Wx, Qz = Wz;
                    const Meander = Number(Layer.meander) || 0.0;
                    if (Meander > 0.01)
                    {
                        const Mw = (Number(Layer.width) || 80.0) * 0.45 * Meander;
                        Qx += (GradNoise2D(Wx * 0.004, Wz * 0.004, Seed) - 0.5) * Mw * 2.0;
                        Qz += (GradNoise2D(Wx * 0.004 + 13.7, Wz * 0.004 + 9.2, Seed + 41) - 0.5) * Mw * 2.0;
                    }
                    const { dist: SDist, along: AlongT, elevY: KnotElevY } = PointToPolylineDist(Qx, Qz, Poly);
                    const HalfW = Math.max(6.0, (Number(Layer.width) || 90.0) * 0.5);
                    if (SDist >= HalfW * 2.1) continue;

                    const Bank = Clamp(Number(Layer.bankSmooth) ?? 0.55, 0.05, 1.0);
                    const NormD = SDist / (HalfW * (1.0 + Bank));
                    if (NormD >= 1.0) continue;

                    const Profile = Math.pow(1.0 - Smoothstep(0.0, 1.0, NormD), Mix(2.2, 0.75, Bank));
                    const Amt = Number(Layer.amount) ?? 32.0;
                    const Mode = Layer.splineMode || 'grade';
                    const Has3DElev = Number.isFinite(KnotElevY);

                    if (Mode === 'grade' || Mode === 'road')
                    {
                        // Direct 3D Elevation Cut & Fill: raising/lowering any knot on the Y-axis Gizmo pulls or carves the terrain right to KnotElevY!
                        const TargetY = Has3DElev ? KnotElevY : Mix(BaseTop + Amt * 0.8, BaseTop + 8.0, AlongT);
                        const FlatCore = 1.0 - Smoothstep(0.32, 1.0, NormD);
                        const BlendW = Clamp(Mix(Profile, FlatCore, 0.55) * MaskW, 0.0, 1.0);
                        Heights[Idx] = Math.max(SlabBottom + 0.6, Mix(CurH, TargetY, BlendW));
                    }
                    else if (Mode === 'ridge')
                    {
                        const RidgeTarget = Has3DElev ? (KnotElevY + Amt) : (CurH + Amt);
                        Heights[Idx] = Math.max(SlabBottom + 0.6, Mix(CurH, Math.max(CurH, RidgeTarget), Profile * MaskW));
                    }
                    else if (Mode === 'canyon')
                    {
                        const GorgeProfile = 1.0 - Smoothstep(0.18, 0.95, SDist / HalfW);
                        const FloorY = Has3DElev ? (KnotElevY - Amt) : (CurH - Amt);
                        Heights[Idx] = Math.max(SlabBottom + 0.6, Mix(CurH, FloorY, GorgeProfile * MaskW));
                    }
                    else if (Mode === 'river')
                    {
                        const BedBase = Has3DElev ? (KnotElevY - Amt * 0.35) : Mix(CurH * 0.55, SeaLevel + 0.6, AlongT);
                        const Carve = Math.min(CurH, BedBase) - 2.2 * Profile;
                        Heights[Idx] = Math.max(SlabBottom + 0.6, Mix(CurH, Carve, Profile * MaskW));
                        if (SDist < HalfW * 0.42) WaterChannel[Idx] = Math.max(WaterChannel[Idx], (1.0 - SDist / (HalfW * 0.42)) * MaskW);
                    }
                    else if (Mode === 'lake')
                    {
                        const BasinElev = Has3DElev ? KnotElevY : Math.max(SlabBottom + 1.0, SeaLevel + Amt * 0.25);
                        Heights[Idx] = Math.max(SlabBottom + 0.6, Mix(CurH, BasinElev - 6.0 * Profile, Profile * MaskW));
                        if (Profile > 0.35) WaterChannel[Idx] = Math.max(WaterChannel[Idx], Profile * MaskW);
                    }
                    continue;
                }

                if (Type === 'rock_formation')
                {
                    continue;
                }

                // Procedural Landform Generators (Multifractal, Ridged, Voronoi, Billow)
                const Scale = Math.max(0.2, Number(Layer.scale) || 2.2);
                let NxCoord = (Wx / RefSpan) * Scale;
                let NzCoord = (Wz / RefSpan) * Scale;

                const WarpAmt = Number(Layer.warp) || 0.0;
                if (WarpAmt > 0.005)
                {
                    const WpX = Fbm2D(NxCoord * 1.35 + 7.1, NzCoord * 1.35 + 3.4, 3, 0.5, 2.0, Seed + 701) - 0.5;
                    const WpZ = Fbm2D(NxCoord * 1.35 + 19.8, NzCoord * 1.35 + 11.2, 3, 0.5, 2.0, Seed + 809) - 0.5;
                    NxCoord += WpX * WarpAmt * 0.85;
                    NzCoord += WpZ * WarpAmt * 0.85;
                }

                let Signal = 0.0;
                if (Type === 'multifractal')
                {
                    Signal = HybridMultifractal2D(
                        NxCoord, NzCoord,
                        Clamp(Layer.octaves | 0, 2, 8),
                        Clamp(Number(Layer.gain) || 0.52, 0.2, 0.85),
                        Number(Layer.valleyOffset) ?? 0.24,
                        Seed
                    );
                }
                else if (Type === 'ridged')
                {
                    // Rotate coordinates slightly along strike azimuth for anisotropic mountain chains
                    const Rx = NxCoord * CosAz + NzCoord * SinAz;
                    const Rz = (-NxCoord * SinAz + NzCoord * CosAz) * 1.28;
                    Signal = RidgedMultifractal2D(
                        Rx, Rz,
                        Clamp(Layer.octaves | 0, 2, 8),
                        0.55,
                        Clamp(Number(Layer.sharpness) ?? 0.72, 0.0, 1.0),
                        Seed
                    );
                }
                else if (Type === 'voronoi')
                {
                    Signal = VoronoiCellular2D(
                        NxCoord, NzCoord,
                        Layer.variant | 0,
                        Clamp(Number(Layer.jitter) ?? 0.82, 0.0, 1.0),
                        Clamp(Number(Layer.sharpness) ?? 0.74, 0.0, 1.0),
                        Seed
                    );
                }
                else if (Type === 'billow')
                {
                    Signal = BillowFolded2D(
                        NxCoord, NzCoord,
                        Clamp(Layer.octaves | 0, 2, 7),
                        Clamp(Number(Layer.duneMix) ?? 0.2, 0.0, 1.0),
                        AzRad,
                        Seed
                    );
                }

                const Delta = Signal * (Number(Layer.amplitude) || 120.0);
                let TargetH = CurH + Delta;
                if (Combine === 'sub') TargetH = Math.max(SlabBottom + 0.5, CurH - Math.abs(Delta));
                else if (Combine === 'max') TargetH = Math.max(CurH, BaseTop + Delta);
                else if (Combine === 'min') TargetH = Math.min(CurH, BaseTop + Delta);
                else if (Combine === 'lerp') TargetH = BaseTop + Delta;

                // When Falloff == 'none' (Full Cliff Edge), maintain sharp vertical cliff wall right up to D2 == 0!
                const ShoreProtect = (Layer.falloff?.mode === 'none')
                    ? (D2 <= 0.0 ? 1.0 : 0.0)
                    : (ShoreBevel > 0.5 ? Smoothstep(0.0, Math.min(ShoreBevel, 22.0), InlandDist) : 1.0);

                Heights[Idx] = Math.max(SlabBottom + 0.5, Mix(CurH, TargetH, MaskW * ShoreProtect));
            }
        }
    }

    // If no interactive sculpt layer existed yet, snapshot final stack output as `preSculptHeights`
    if (!PreSculptHeights)
    {
        PreSculptHeights = new Float32Array(Heights);
    }

    // Compute peak height & land area telemetry
    let MaxHeight = SlabBottom;
    for (let I = 0; I < Count; ++I)
    {
        if (Sdf2D[I] <= 0.0 && Heights[I] > MaxHeight) MaxHeight = Heights[I];
    }
    const CellAreaM2 = StepX * StepZ;
    const LandAreaKm2 = (LandCells * CellAreaM2) / 1e6;
    const BaseSig = ComputeRegionBaseSignature(World, Region);

    const PrevRuntime = Region.runtime;
    Region.runtime = {
        nx: Nx, nz: Nz, stepX: StepX, stepZ: StepZ,
        slabBottom: SlabBottom, baseTop: BaseTop,
        coverage: MaskCov, sdf2D: Sdf2D, heights: Heights,
        preSculptHeights: PreSculptHeights,
        topSculptLayerId: TopLiveSculptIdx >= 0 ? Region.stack[TopLiveSculptIdx].id : null,
        baseSignature: BaseSig,
        erosionStale: !!(Region.erosionBaseSig && Region.erosionBaseSig !== BaseSig),
        falloffMap: FalloffPreview, waterChannel: WaterChannel,
        hasLand: true, maxHeight: MaxHeight, landAreaKm2: LandAreaKm2
    };
    // Preserve live erosion state + data-map lens across stack re-evaluations
    // (baked erosion persists via the 'Erosion Baked Surface' sculpt deltas).
    if (PrevRuntime && PrevRuntime.erosion) Region.runtime.erosion = PrevRuntime.erosion;
    if (PrevRuntime && PrevRuntime.mapLensData) Region.runtime.mapLensData = PrevRuntime.mapLensData;
    Region.dirty = false;
    return Region.runtime;
}

/**
 * Deterministic FNV-1a structural hash of a Region's upstream non-erosion layers & mask state
 * so editing base mountains/splines/masks immediately flags baked erosion as stale (`erosionStale: true`).
 */
export function ComputeRegionBaseSignature(World, Region)
{
    let H = 2166136261 >>> 0;
    const MixStr = Str =>
    {
        for (let I = 0; I < Str.length; ++I)
        {
            H ^= Str.charCodeAt(I);
            H = Math.imul(H, 16777619) >>> 0;
        }
    };
    const B = Region.bounds;
    MixStr(`${B.x},${B.z},${B.width},${B.depth}|${Region.resolution}|${Region.baseElevation}|${Region.slabThickness}|${Region.shorelineBevel}|${Region.worldFrequencySpan || 0}|${World.seaLevel ?? 0}`);
    for (const M of World.maskLayers)
    {
        if (!Region.maskLayerId || Region.maskLayerId === 'all' || Region.maskLayerId === M.id)
        {
            MixStr(`|m:${M.id}:${M.visible ? 1 : 0}:${M.revision}`);
        }
    }
    for (const Lyr of Region.stack)
    {
        if (!Lyr || Lyr.name === 'Erosion Baked Surface') continue;
        MixStr(`|l:${Lyr.id}:${Lyr.type}:${Lyr.enabled ? 1 : 0}:${Lyr.opacity ?? 1}:${Lyr.combine || 'add'}:${Lyr.amplitude ?? 0}:${Lyr.scale ?? 0}:${Lyr.seed ?? 0}:${Lyr.octaves ?? 0}:${Lyr.gain ?? 0}:${Lyr.warp ?? 0}:${Lyr.sharpness ?? 0}:${Lyr.azimuth ?? 0}:${Lyr.variant ?? 0}:${Lyr.bedCount ?? 0}:${Lyr.strength ?? 0}:${Lyr.dipDeg ?? 0}:${Lyr.splineMode || ''}:${Lyr.width ?? 0}:${Lyr.amount ?? 0}:${Lyr.strokeCount ?? 0}:${Lyr.falloff?.mode || ''}:${Lyr.falloff?.distance ?? 0}:${Lyr.falloff?.edgeFloor ?? 0}`);
        if (Array.isArray(Lyr.points))
        {
            for (const Pt of Lyr.points) MixStr(`(${Pt[0]},${Pt[1]},${Pt[2] ?? 0})`);
        }
    }
    return H.toString(16).padStart(8, '0');
}

function EnsureSculptBufferMatches(Layer, Nx, Nz)
{
    const TargetLen = Nx * Nz;
    if (Layer.deltas instanceof Float32Array && Layer.deltas.length === TargetLen && Layer.nx === Nx && Layer.nz === Nz)
    {
        return;
    }
    const Next = new Float32Array(TargetLen);
    if (Layer.deltas instanceof Float32Array && Layer.nx > 1 && Layer.nz > 1)
    {
        const OldNx = Layer.nx, OldNz = Layer.nz, Old = Layer.deltas;
        for (let Z = 0; Z < Nz; ++Z)
        {
            const V = (Z / (Nz - 1)) * (OldNz - 1);
            const Z0 = Math.floor(V), Z1 = Math.min(OldNz - 1, Z0 + 1), Tz = V - Z0;
            for (let X = 0; X < Nx; ++X)
            {
                const U = (X / (Nx - 1)) * (OldNx - 1);
                const X0 = Math.floor(U), X1 = Math.min(OldNx - 1, X0 + 1), Tx = U - X0;
                const C00 = Old[Z0 * OldNx + X0], C10 = Old[Z0 * OldNx + X1];
                const C01 = Old[Z1 * OldNx + X0], C11 = Old[Z1 * OldNx + X1];
                Next[Z * Nx + X] = Mix(Mix(C00, C10, Tx), Mix(C01, C11, Tx), Tz);
            }
        }
    }
    Layer.deltas = Next;
    Layer.nx = Nx;
    Layer.nz = Nz;
}

/*==========================================================================================================================================
                                     REAL-TIME TERRAIN SDF SCULPT BRUSH KERNEL
==========================================================================================================================================*/
/**
 * Applies a 3D terrain sculpt dab at world point (Wx, Wy, Wz) on `Region`:
 * - Automatically finds or creates the top `SDF Sculpt Layer` in `Region.stack`
 * - Supports `'raise'`, `'lower'`, `'smooth'`, `'flatten'`, `'pinch'`, plus true 3D SDF `'overhang'` & `'cave'`!
 */
export function ApplyTerrainSculptDab(World, Region, HitPos, Mode = 'raise', Settings = {})
{
    if (!Region || Region.locked) return false;
    if (!Region.runtime) EvaluateTerrainRegion(World, Region);
    const Rt = Region.runtime;
    if (!Rt || !Rt.hasLand) return false;

    const Radius = Clamp(Number(Settings.radius) || 85.0, 8.0, 900.0);
    const Strength = Clamp(Number(Settings.strength) ?? 0.65, 0.04, 1.0);
    const Invert = !!Settings.invert;

    // True 3D Volumetric SDF Overhang / Cave Carve operation (stored as analytical 3D SDF capsules — NO 96-op cap!)
    if (Mode === 'overhang' || Mode === 'cave')
    {
        const IsCave = (Mode === 'cave') !== Invert;
        const PrevPos = Settings.prevPos || HitPos;
        Region.sdf3DOps.push({
            ax: PrevPos[0], ay: PrevPos[1], az: PrevPos[2],
            bx: HitPos[0], by: HitPos[1], bz: HitPos[2],
            radius: Math.max(6.0, Radius * 0.44),
            blend: Math.max(2.5, Radius * 0.18),
            op: IsCave ? 'cave' : 'overhang'
        });
        Region.meshDirty = true;
        return true;
    }

    // Find active sculpt layer or create one at top of stack
    let SculptLayer = Region.stack.find(L => L.id === Settings.targetLayerId && L.type === 'sculpt');
    if (!SculptLayer)
    {
        for (let I = Region.stack.length - 1; I >= 0; --I)
        {
            if (Region.stack[I].type === 'sculpt' && Region.stack[I].enabled !== false && Region.stack[I].name !== 'Erosion Baked Surface')
            {
                SculptLayer = Region.stack[I];
                break;
            }
        }
    }
    let CreatedAtTop = false;
    if (!SculptLayer)
    {
        SculptLayer = CreateTerrainLayer('sculpt', {
            name: 'SDF Sculpt Edits',
            falloff: { mode: 'none', distance: 100 }
        });
        Region.stack.push(SculptLayer);
        CreatedAtTop = true;
    }
    EnsureSculptBufferMatches(SculptLayer, Rt.nx, Rt.nz);

    const B = Region.bounds;
    const Nx = Rt.nx, Nz = Rt.nz;
    const StepX = Rt.stepX, StepZ = Rt.stepZ;
    const X0 = Clamp(Math.floor((HitPos[0] - Radius - B.x) / StepX), 0, Nx - 1);
    const X1 = Clamp(Math.ceil((HitPos[0] + Radius - B.x) / StepX), 0, Nx - 1);
    const Z0 = Clamp(Math.floor((HitPos[2] - Radius - B.z) / StepZ), 0, Nz - 1);
    const Z1 = Clamp(Math.ceil((HitPos[2] + Radius - B.z) / StepZ), 0, Nz - 1);
    if (X1 < X0 || Z1 < Z0) return false;

    const EffectiveMode = (Mode === 'raise' && Invert) ? 'lower' : (Mode === 'lower' && Invert) ? 'raise' : Mode;
    const TargetFlattenH = Settings.flattenHeight ?? HitPos[1];
    const Deltas = SculptLayer.deltas;
    const Heights = Rt.heights;

    // Copy sub-window of current composite heights for smooth/pinch
    let CopyH = null;
    const SubW = X1 - X0 + 1, SubD = Z1 - Z0 + 1;
    if (EffectiveMode === 'smooth' || EffectiveMode === 'pinch')
    {
        CopyH = new Float32Array(SubW * SubD);
        for (let Z = Z0; Z <= Z1; ++Z)
        {
            for (let X = X0; X <= X1; ++X)
            {
                CopyH[(Z - Z0) * SubW + (X - X0)] = Heights[Z * Nx + X];
            }
        }
    }

    for (let Z = Z0; Z <= Z1; ++Z)
    {
        const Wz = B.z + Z * StepZ;
        for (let X = X0; X <= X1; ++X)
        {
            const Idx = Z * Nx + X;
            if (Rt.sdf2D[Idx] > StepX * 0.5) continue;

            const Wx = B.x + X * StepX;
            const Dist = Math.hypot(Wx - HitPos[0], Wz - HitPos[2]);
            if (Dist >= Radius) continue;

            const U = 1.0 - (Dist / Radius) * (Dist / Radius);
            const W = U * U * Strength;

            if (EffectiveMode === 'raise')
            {
                Deltas[Idx] += W * Radius * 0.22;
            }
            else if (EffectiveMode === 'lower')
            {
                Deltas[Idx] -= W * Radius * 0.22;
            }
            else if (EffectiveMode === 'flatten')
            {
                const Diff = TargetFlattenH - Heights[Idx];
                Deltas[Idx] += Diff * W * 0.55;
            }
            else if (EffectiveMode === 'smooth')
            {
                const Lx = X - X0, Lz = Z - Z0;
                const At = (Ax, Az) => CopyH[Clamp(Az, 0, SubD - 1) * SubW + Clamp(Ax, 0, SubW - 1)];
                const Avg = (At(Lx - 1, Lz) + At(Lx + 1, Lz) + At(Lx, Lz - 1) + At(Lx, Lz + 1)) * 0.25;
                Deltas[Idx] += (Avg - CopyH[Lz * SubW + Lx]) * W * 0.72;
            }
            else if (EffectiveMode === 'pinch')
            {
                const Crest = Math.pow(1.0 - Dist / Radius, 2.4) * Radius * 0.16 * Strength;
                Deltas[Idx] += Invert ? -Crest : Crest;
            }
            else if (EffectiveMode === 'restore')
            {
                Deltas[Idx] *= (1.0 - Clamp(W * 0.78, 0.0, 1.0));
            }
        }
    }

    SculptLayer.strokeCount = (SculptLayer.strokeCount || 0) + 1;

    // Fast O(brush-subrect) scissored update when sculpting the top active sculpt layer!
    const IsTopMostActive = Region.stack.slice(Region.stack.indexOf(SculptLayer) + 1).every(L => !L || L.enabled === false);
    if (IsTopMostActive && Rt.preSculptHeights && (CreatedAtTop || Rt.topSculptLayerId === SculptLayer.id))
    {
        if (CreatedAtTop) Rt.topSculptLayerId = SculptLayer.id;
        const Opac = Clamp(Number(SculptLayer.opacity) ?? 1.0, 0.0, 1.0);
        const ShoreBevel = Number(Region.shorelineBevel) ?? 18.0;
        for (let Z = Z0; Z <= Z1; ++Z)
        {
            const Wz = B.z + Z * StepZ;
            const BoxEdgeZ = Math.min(Wz - B.z, B.z + B.depth - Wz);
            for (let X = X0; X <= X1; ++X)
            {
                const Idx = Z * Nx + X;
                const D2 = Rt.sdf2D[Idx];
                if (D2 > StepX * 0.5) continue;
                const Wx = B.x + X * StepX;
                const BoxEdgeDist = Math.min(BoxEdgeZ, Wx - B.x, B.x + B.width - Wx);
                const InlandDist = Math.max(0.0, -D2);
                const FallW = EvaluateBoundaryFalloff(InlandDist, BoxEdgeDist, SculptLayer.falloff);
                const MaskW = FallW * Opac;
                const ShoreProtect = (SculptLayer.falloff?.mode === 'none')
                    ? (D2 <= 0.0 ? 1.0 : 0.0)
                    : (ShoreBevel > 0.5 ? Smoothstep(0.0, Math.min(ShoreBevel, 22.0), InlandDist) : 1.0);
                const BaseH = Rt.preSculptHeights[Idx];
                const TargetH = BaseH + Deltas[Idx];
                Heights[Idx] = Math.max(Rt.slabBottom + 0.5, Mix(BaseH, TargetH, MaskW * ShoreProtect));
                if (D2 <= 0.0 && Heights[Idx] > Rt.maxHeight) Rt.maxHeight = Heights[Idx];
            }
        }
        Rt.baseSignature = ComputeRegionBaseSignature(World, Region);
        Rt.erosionStale = !!(Region.erosionBaseSig && Region.erosionBaseSig !== Rt.baseSignature);
        Region.dirty = false;
        Region.meshDirty = true;
        return true;
    }

    Region.dirty = true;
    Region.meshDirty = true;
    return true;
}

/*==========================================================================================================================================
                     SPACE-EFFICIENT THIN-SLAB SDF -> WATERTIGHT TRIANGLE MESH EXTRACTOR (.OBJ / .STL / GPU)
==========================================================================================================================================*/
/**
 * Converts an evaluated Terrain Region's Thin-Slab SDF + 3D Overhang/Cave SDF into an indexed triangle mesh!
 * - Skips 100% of empty ocean cells outside the 2D mask (`sdf2D > skirtWidth`) -> zero wasted vertices!
 * - Builds both the top geological surface AND the thin flat underside slab (`y = slabBottom`) + watertight perimeter cliff skirt
 *   so toggling Ocean OFF reveals the exact thin flat SDF body requested!
 */
export function ExtractRegionTriangleMesh(World, Region, Options = {})
{
    const Rt = Region.dirty || !Region.runtime ? EvaluateTerrainRegion(World, Region, Options.previewFalloffLayerId) : Region.runtime;
    if (!Rt || !Rt.hasLand)
    {
        return {
            positions: new Float32Array(0),
            normals: new Float32Array(0),
            colors: new Float32Array(0),
            aux: new Float32Array(0), // [falloffWeight, slopeDegNorm]
            indices: new Uint32Array(0),
            vertexCount: 0,
            triangleCount: 0
        };
    }

    const B = Region.bounds;
    const Nx = Rt.nx, Nz = Rt.nz;
    const StepX = Rt.stepX, StepZ = Rt.stepZ;
    const SlabBottom = Rt.slabBottom;
    const SeaLevel = World.seaLevel ?? 0.0;
    const SkirtMaxD = Math.max(StepX, StepZ) * 1.55;
    const IncludeUnderside = Options.includeUnderside !== false;

    // Determine active vertices inside mask + skirt
    const TopVertIdx = new Int32Array(Nx * Nz);
    TopVertIdx.fill(-1);

    const PosList = [];
    const NormList = [];
    const ColList = [];
    const AuxList = [];
    const IdxList = [];

    for (let Z = 0; Z < Nz; ++Z)
    {
        const Wz = B.z + Z * StepZ;
        const Zm = Math.max(0, Z - 1), Zp = Math.min(Nz - 1, Z + 1);
        for (let X = 0; X < Nx; ++X)
        {
            const Cell = Z * Nx + X;
            const D2 = Rt.sdf2D[Cell];
            if (D2 > SkirtMaxD) continue;

            const Wx = B.x + X * StepX;
            const H = Rt.heights[Cell];

            const Xm = Math.max(0, X - 1), Xp = Math.min(Nx - 1, X + 1);
            const DhDx = (Rt.heights[Z * Nx + Xp] - Rt.heights[Z * Nx + Xm]) / ((Xp - Xm || 1) * StepX);
            const DhDz = (Rt.heights[Zp * Nx + X] - Rt.heights[Zm * Nx + X]) / ((Zp - Zm || 1) * StepZ);
            const InvLen = 1.0 / Math.hypot(DhDx, 1.0, DhDz);
            const NxVal = -DhDx * InvLen, NyVal = InvLen, NzVal = -DhDz * InvLen;
            const SlopeDeg = Math.acos(Clamp(NyVal, -1.0, 1.0)) * 57.2957795;

            const Litho = ComputeFlatLithologyColor(H, SeaLevel, SlopeDeg, Rt.waterChannel[Cell], D2, SlabBottom);

            // ---- Erosion overlays (live simulated surface state) ----
            let CR = Litho[0], CG = Litho[1], CB = Litho[2];
            const Er = Rt.erosion;
            if (Er && Cell < Er.nx * Er.nz)
            {
                // Aeolian sand sheet
                const SandT = Clamp(Er.sand[Cell] / 0.6, 0, 1) * 0.85;
                CR = Mix(CR, 0.82, SandT); CG = Mix(CG, 0.68, SandT); CB = Mix(CB, 0.42, SandT);
                // Chemical staining (Fe rust / Cu patina / Mn varnish / salt crust)
                CR = Mix(CR, 0.55, Clamp(Er.fe[Cell], 0, 1) * 0.32); CG = Mix(CG, 0.26, Clamp(Er.fe[Cell], 0, 1) * 0.28); CB = Mix(CB, 0.16, Clamp(Er.fe[Cell], 0, 1) * 0.28);
                CR = Mix(CR, 0.22, Clamp(Er.cu[Cell], 0, 1) * 0.26); CG = Mix(CG, 0.58, Clamp(Er.cu[Cell], 0, 1) * 0.32); CB = Mix(CB, 0.48, Clamp(Er.cu[Cell], 0, 1) * 0.3);
                CR = Mix(CR, 0.24, Clamp(Er.mn[Cell], 0, 1) * 0.3); CG = Mix(CG, 0.2, Clamp(Er.mn[Cell], 0, 1) * 0.28); CB = Mix(CB, 0.26, Clamp(Er.mn[Cell], 0, 1) * 0.3);
                const SaltT = Clamp(Er.salt[Cell], 0, 1) * 0.5;
                CR = Mix(CR, 0.9, SaltT); CG = Mix(CG, 0.88, SaltT); CB = Mix(CB, 0.82, SaltT);
                // Snow / glacier ice
                const SnowT = Clamp(Er.snow[Cell] / 0.5, 0, 1);
                CR = Mix(CR, 0.86, SnowT); CG = Mix(CG, 0.88, SnowT); CB = Mix(CB, 0.94, SnowT);
                const IceT = Clamp(Er.ice[Cell] / 1.2, 0, 1) * 0.85;
                CR = Mix(CR, 0.55, IceT); CG = Mix(CG, 0.76, IceT); CB = Mix(CB, 0.9, IceT);
            }

            // ---- Data-map diagnostic lens: false-color turbo ramp per cell ----
            if (Rt.mapLensData && Cell < Rt.mapLensData.length)
            {
                const T = Clamp(Rt.mapLensData[Cell], 0, 1);
                CR = TurboRampJS(T, 0); CG = TurboRampJS(T, 1); CB = TurboRampJS(T, 2);
            }

            const VId = PosList.length / 3;
            TopVertIdx[Cell] = VId;
            PosList.push(Wx, H, Wz);
            NormList.push(NxVal, NyVal, NzVal);
            ColList.push(CR, CG, CB);
            // aux = [falloffWeight, slopeNorm, waterDepthM, wetness] — vec4!
            AuxList.push(
                Rt.falloffMap[Cell], Clamp(SlopeDeg / 90.0, 0.0, 1.0),
                Er && Cell < Er.w.length ? Clamp(Er.w[Cell], 0, 8) : 0,
                Er && Cell < Er.wetness.length ? Clamp(Er.wetness[Cell], 0, 1) : Rt.waterChannel[Cell] * 0.4
            );
        }
    }

    // Identify snapped 2D grid footprints of true 3D volumetric features (caves, smooth overhangs & 3D mountain tunnel bores)
    // so their overlapping 2.5D top surface quads are replaced cleanly by watertight 3D Dual-Surface-Nets patches!
    const VolBricks = BuildVolumetric3DBricks(Region, Rt);
    const SuppressTopQuad = VolBricks.length > 0 ? new Uint8Array((Nx - 1) * (Nz - 1)) : null;
    if (SuppressTopQuad)
    {
        for (const Brk of VolBricks)
        {
            for (let Z = Brk.gz0; Z < Brk.gz1; ++Z)
            {
                for (let X = Brk.gx0; X < Brk.gx1; ++X)
                {
                    SuppressTopQuad[Z * (Nx - 1) + X] = 1;
                }
            }
        }
    }

    // Emit top terrain triangles for active cells
    const CellActive = new Uint8Array((Nx - 1) * (Nz - 1));
    for (let Z = 0; Z < Nz - 1; ++Z)
    {
        for (let X = 0; X < Nx - 1; ++X)
        {
            const I00 = TopVertIdx[Z * Nx + X];
            const I10 = TopVertIdx[Z * Nx + (X + 1)];
            const I01 = TopVertIdx[(Z + 1) * Nx + X];
            const I11 = TopVertIdx[(Z + 1) * Nx + (X + 1)];
            if (I00 < 0 || I10 < 0 || I01 < 0 || I11 < 0) continue;

            const QIdx = Z * (Nx - 1) + X;
            CellActive[QIdx] = 1;
            if (SuppressTopQuad && SuppressTopQuad[QIdx]) continue;
            // Counter-clockwise upward-facing winding (+Y when X right, Z forward/south -> (00, 01, 11) & (00, 11, 10))
            IdxList.push(I00, I01, I11, I00, I11, I10);
        }
    }

    // Build thin flat underside slab & perimeter sidewall skirt so the SDF body is a clean thin closed wafer!
    if (IncludeUnderside && PosList.length > 0)
    {
        const BotVertIdx = new Int32Array(Nx * Nz);
        BotVertIdx.fill(-1);
        const GetBotVert = GridCell =>
        {
            if (BotVertIdx[GridCell] >= 0) return BotVertIdx[GridCell];
            const TopV = TopVertIdx[GridCell];
            const Wx = PosList[TopV * 3], Wz = PosList[TopV * 3 + 2];
            const BId = PosList.length / 3;
            BotVertIdx[GridCell] = BId;
            PosList.push(Wx, SlabBottom, Wz);
            NormList.push(0, -1, 0);
            ColList.push(0.055, 0.058, 0.064); // Clean dark basalt slab underside
            AuxList.push(0.0, 0.0, 0.0, 0.0);
            return BId;
        };

        const EmitSkirtQuad = (CellA, CellB, OutNx, OutNz) =>
        {
            const Ta = TopVertIdx[CellA], Tb = TopVertIdx[CellB];
            if (Ta < 0 || Tb < 0) return;
            const BaseV = PosList.length / 3;
            const Ax = PosList[Ta * 3], Ay = PosList[Ta * 3 + 1], Az = PosList[Ta * 3 + 2];
            const Bx = PosList[Tb * 3], By = PosList[Tb * 3 + 1], Bz = PosList[Tb * 3 + 2];
            PosList.push(
                Ax, Ay, Az,
                Bx, By, Bz,
                Bx, SlabBottom, Bz,
                Ax, SlabBottom, Az
            );
            for (let K = 0; K < 4; ++K)
            {
                NormList.push(OutNx, 0, OutNz);
                ColList.push(0.105, 0.098, 0.088);
                AuxList.push(0.0, 1.0, 0.0, 0.0);
            }
            IdxList.push(BaseV, BaseV + 3, BaseV + 2, BaseV, BaseV + 2, BaseV + 1);
        };

        for (let Z = 0; Z < Nz - 1; ++Z)
        {
            for (let X = 0; X < Nx - 1; ++X)
            {
                if (!CellActive[Z * (Nx - 1) + X]) continue;
                const C00 = Z * Nx + X;
                const C10 = Z * Nx + (X + 1);
                const C01 = (Z + 1) * Nx + X;
                const C11 = (Z + 1) * Nx + (X + 1);

                // Underside floor triangles (stride 2 interior decimation keeps thin underside featherlight!)
                const B00 = GetBotVert(C00), B10 = GetBotVert(C10);
                const B01 = GetBotVert(C01), B11 = GetBotVert(C11);
                IdxList.push(B00, B11, B01, B00, B10, B11);

                // Perimeter cliff walls wherever an active mask cell borders empty ocean or region edge
                if (Z === 0 || !CellActive[(Z - 1) * (Nx - 1) + X]) EmitSkirtQuad(C10, C00, 0, -1);
                if (Z === Nz - 2 || !CellActive[(Z + 1) * (Nx - 1) + X]) EmitSkirtQuad(C01, C11, 0, 1);
                if (X === 0 || !CellActive[Z * (Nx - 1) + (X - 1)]) EmitSkirtQuad(C00, C01, -1, 0);
                if (X === Nx - 2 || !CellActive[Z * (Nx - 1) + (X + 1)]) EmitSkirtQuad(C11, C10, 1, 0);
            }
        }
    }

    // Extract true 3D Volumetric SDF Caves, Smooth Overhangs & Mountain Tunnel Bores via Dual Surface Nets!
    if (VolBricks.length > 0)
    {
        AppendVolumetric3DBricksMesh(World, Region, Rt, VolBricks, PosList, NormList, ColList, AuxList, IdxList);
    }

    // Extract 3D Volumetric Spline Rock Bridges / Overhang Tubes ('arch3d' spline layers!)
    Append3DSplineTubeMeshes(Region, PosList, NormList, ColList, AuxList, IdxList);

    // Extract Pure-SDF Fractured Rock & Cliff Formations ('rock_formation' layers via QEF Dual Contouring!)
    AppendPureSdfRockFormationLayers(Region, Rt, PosList, NormList, ColList, AuxList, IdxList);

    return {
        positions: new Float32Array(PosList),
        normals: new Float32Array(NormList),
        colors: new Float32Array(ColList),
        aux: new Float32Array(AuxList),
        indices: new Uint32Array(IdxList),
        vertexCount: PosList.length / 3,
        triangleCount: IdxList.length / 3
    };
}

function AppendPureSdfRockFormationLayers(Region, Rt, PosList, NormList, ColList, AuxList, IdxList)
{
    if (!Region || !Region.stack || !Rt) return;
    const B = Region.bounds;

    for (const Layer of Region.stack)
    {
        if (!Layer || Layer.enabled === false || Layer.type !== 'rock_formation') continue;

        const Fp = Layer.fractureParams || createDefaultFractureParams('cracked-monolith');
        const CacheKey = JSON.stringify({ ...Fp, cutawayEnabled: false });
        let Cached = RockFormationQefCache.get(Layer);
        if (!Cached || Cached.key !== CacheKey)
        {
            const Formation = buildFracturedFormation({ ...Fp, cutawayEnabled: false });
            const Baked = bakeFracturedFormationQefMesh(Formation, { resolution: Clamp(Fp.meshResolution || 64, 42, 88) });
            Cached = { key: CacheKey, mesh: Baked.mesh, scale: Formation.params.scale };
            RockFormationQefCache.set(Layer, Cached);
        }

        const M = Cached.mesh;
        if (!M || !M.indices || M.indices.length === 0) continue;

        const Wx0 = Number.isFinite(Layer.worldX) ? Layer.worldX : (B.x + B.width * 0.5);
        const Wz0 = Number.isFinite(Layer.worldZ) ? Layer.worldZ : (B.z + B.depth * 0.5);
        const U = Clamp((Wx0 - B.x) / Math.max(1e-4, Rt.stepX), 0, Rt.nx - 1.001);
        const V = Clamp((Wz0 - B.z) / Math.max(1e-4, Rt.stepZ), 0, Rt.nz - 1.001);
        const Ix = Math.floor(U), Iz = Math.floor(V);
        const BaseTerrY = Rt.heights[Iz * Rt.nx + Ix] - (Number(Layer.embedDepthM) ?? 8.0);

        const ScaleMul = Math.max(12.0, Number(Layer.worldScaleM) || 165.0) / Math.max(0.5, Cached.scale);
        const YawRad = ((Number(Layer.yawDeg) || 0.0) * Math.PI) / 180.0;
        const Cy = Math.cos(YawRad), Sy = Math.sin(YawRad);

        const BaseVert = PosList.length / 3;
        const NumV = M.positions.length / 3;

        for (let I = 0; I < NumV; ++I)
        {
            const P3 = I * 3, C4 = I * 4;
            const Lx = M.positions[P3] * ScaleMul;
            const Ly = M.positions[P3 + 1] * ScaleMul;
            const Lz = M.positions[P3 + 2] * ScaleMul;

            const Rx = Lx * Cy + Lz * Sy;
            const Rz = -Lx * Sy + Lz * Cy;
            PosList.push(Wx0 + Rx, BaseTerrY + Ly, Wz0 + Rz);

            const Nx = M.normals[P3], Ny = M.normals[P3 + 1], Nz = M.normals[P3 + 2];
            NormList.push(Nx * Cy + Nz * Sy, Ny, -Nx * Sy + Nz * Cy);

            const Ao = M.colors ? M.colors[C4] : 0.85;
            // Convert sRGB-like rock albedo into TerrainOceanViewport's calibrated linear dark-studio space
            const CR = Math.pow(Clamp(M.albedos[P3], 0.02, 1.0), 1.8) * (0.35 + 0.65 * Ao) * 0.68;
            const CG = Math.pow(Clamp(M.albedos[P3 + 1], 0.02, 1.0), 1.8) * (0.35 + 0.65 * Ao) * 0.68;
            const CB = Math.pow(Clamp(M.albedos[P3 + 2], 0.02, 1.0), 1.8) * (0.35 + 0.65 * Ao) * 0.68;
            ColList.push(CR, CG, CB);

            const SlopeDeg = Math.acos(Clamp(Ny, -1.0, 1.0)) * 57.2957795;
            AuxList.push(1.0, Clamp(SlopeDeg / 90.0, 0.0, 1.0), 0.0, 0.0);
        }

        for (let I = 0; I < M.indices.length; ++I)
        {
            IdxList.push(BaseVert + M.indices[I]);
        }
    }
}

/**
 * Calibrated linear-space dark-studio geological lithology palette (rich, grounded, non-blinding!).
 */
/*==========================================================================================================================================
                                        TURBO FALSE-COLOR RAMP (data-map lens)
==========================================================================================================================================*/
export function TurboRampJS(T, Channel)
{
    const X = Clamp(T, 0, 1);
    // Compact turbo-like ramp: blue → cyan → green → yellow → red.
    const Stops = Channel === 0
        ? [[0, 0.05], [0.35, 0.08], [0.7, 0.58], [1, 0.62]]
        : Channel === 1
            ? [[0, 0.20], [0.35, 0.42], [0.7, 0.42], [1, 0.14]]
            : [[0, 0.42], [0.35, 0.28], [0.7, 0.10], [1, 0.09]];
    for (let S = 0; S < Stops.length - 1; ++S)
    {
        if (X >= Stops[S][0] && X <= Stops[S + 1][0])
        {
            const K = (X - Stops[S][0]) / Math.max(1e-9, Stops[S + 1][0] - Stops[S][0]);
            return Mix(Stops[S][1], Stops[S + 1][1], K);
        }
    }
    return Stops[Stops.length - 1][1];
}

function ComputeFlatLithologyColor(HeightM, SeaLevelM, SlopeDeg, RiverWeight, Sdf2D, SlabBottom)
{
    const RelH = HeightM - SeaLevelM;
    if (HeightM <= SlabBottom + 0.4) return [0.065, 0.070, 0.078];

    // River / Lake bed tint
    if (RiverWeight > 0.12)
    {
        const T = Clamp(RiverWeight, 0.0, 1.0);
        return [Mix(0.11, 0.045, T), Mix(0.14, 0.145, T), Mix(0.12, 0.175, T)];
    }

    // Submerged shallow shelf / coastal sandbar
    if (RelH < 4.5 && SlopeDeg < 25.0)
    {
        const BeachT = Smoothstep(-3.0, 4.5, RelH);
        return [Mix(0.09, 0.24, BeachT), Mix(0.13, 0.21, BeachT), Mix(0.13, 0.15, BeachT)];
    }

    // Grounded linear-space elevation zones: deep pine/moss lowland -> olive/ochre highland -> slate -> crisp granite summit
    let R, G, B;
    if (RelH < 58.0)
    {
        const T = Smoothstep(4.5, 58.0, RelH);
        R = Mix(0.085, 0.125, T);
        G = Mix(0.145, 0.158, T);
        B = Mix(0.078, 0.088, T);
    }
    else if (RelH < 145.0)
    {
        const T = Smoothstep(58.0, 145.0, RelH);
        R = Mix(0.125, 0.195, T);
        G = Mix(0.158, 0.168, T);
        B = Mix(0.088, 0.122, T);
    }
    else if (RelH < 215.0)
    {
        const T = Smoothstep(145.0, 215.0, RelH);
        R = Mix(0.195, 0.245, T);
        G = Mix(0.168, 0.228, T);
        B = Mix(0.122, 0.212, T);
    }
    else
    {
        const T = Smoothstep(215.0, 275.0, RelH);
        R = Mix(0.245, 0.44, T);
        G = Mix(0.228, 0.45, T);
        B = Mix(0.212, 0.47, T);
    }

    // Steep cliff bedrock exposure (> 28 deg transitions cleanly to dark basalt/warm cliff stone)
    const CliffT = Smoothstep(26.0, 45.0, SlopeDeg);
    const RockR = Sdf2D > -18.0 ? 0.135 : 0.168;
    const RockG = Sdf2D > -18.0 ? 0.125 : 0.148;
    const RockB = Sdf2D > -18.0 ? 0.118 : 0.132;
    R = Mix(R, RockR, CliffT);
    G = Mix(G, RockG, CliffT);
    B = Mix(B, RockB, CliffT);

    return [R, G, B];
}

function Append3DSplineTubeMeshes(Region, PosList, NormList, ColList, AuxList, IdxList)
{
    for (const Layer of Region.stack)
    {
        if (!Layer.enabled || Layer.type !== 'spline' || Layer.splineMode !== 'arch3d') continue;
        if (!Layer.points || Layer.points.length < 2) continue;

        const Poly = SampleCatmullRomPolyline(Layer.points, 10);
        if (Poly.length < 2) continue;

        const HalfW = Math.max(6.0, (Number(Layer.width) || 68.0) * 0.5);
        const HalfH = Math.max(4.0, (Number(Layer.amount) || 26.0) * 0.5);
        const Sides = 12;
        const Rings = Poly.length;
        const BaseV = PosList.length / 3;

        for (let I = 0; I < Rings; ++I)
        {
            const Cur = Poly[I];
            const Prev = Poly[Math.max(0, I - 1)];
            const Next = Poly[Math.min(Rings - 1, I + 1)];
            let Tx = Next[0] - Prev[0];
            let Ty = (Next[2] ?? 40) - (Prev[2] ?? 40);
            let Tz = Next[1] - Prev[1];
            const Tl = Math.hypot(Tx, Ty, Tz) || 1.0;
            Tx /= Tl; Ty /= Tl; Tz /= Tl;

            // Horizontal side vector = normalize(cross(WorldUp, Tangent))
            let Sx = Tz, Sy = 0.0, Sz = -Tx;
            const Sl = Math.hypot(Sx, Sz) || 1.0;
            Sx /= Sl; Sz /= Sl;

            // Local binormal up = cross(Tangent, Side)
            const Ux = Ty * Sz - Tz * Sy;
            const Uy = Tz * Sx - Tx * Sz;
            const Uz = Tx * Sy - Ty * Sx;

            const Taper = 0.72 + 0.28 * Math.sin((I / Math.max(1, Rings - 1)) * Math.PI);
            const Rx = HalfW * Taper;
            const Ry = HalfH * Taper;
            const Cy = Cur[2] ?? 45.0;

            for (let S = 0; S <= Sides; ++S)
            {
                const Ang = (S / Sides) * Math.PI * 2.0;
                const Ca = Math.cos(Ang), Sa = Math.sin(Ang);
                // Flat-topped bridge deck feel: slightly flatten top of arch rock
                const DeckSa = Sa > 0 ? Math.pow(Sa, 0.72) : Sa;
                const Px = Cur[0] + Sx * Ca * Rx + Ux * DeckSa * Ry;
                const Py = Cy     + Sy * Ca * Rx + Uy * DeckSa * Ry;
                const Pz = Cur[1] + Sz * Ca * Rx + Uz * DeckSa * Ry;

                let Nx = Sx * (Ca / Rx) + Ux * (Sa / Ry);
                let Ny = Sy * (Ca / Rx) + Uy * (Sa / Ry);
                let Nz = Sz * (Ca / Rx) + Uz * (Sa / Ry);
                const Nl = Math.hypot(Nx, Ny, Nz) || 1.0;
                Nx /= Nl; Ny /= Nl; Nz /= Nl;

                PosList.push(Px, Py, Pz);
                NormList.push(Nx, Ny, Nz);
                const TopTint = Clamp(Ny * 0.5 + 0.5, 0.0, 1.0);
                ColList.push(Mix(0.13, 0.18, TopTint), Mix(0.12, 0.17, TopTint), Mix(0.11, 0.14, TopTint));
                AuxList.push(1.0, Math.acos(Clamp(Ny, -1, 1)) / (Math.PI * 0.5), 0.0, 0.0);
            }
        }

        const Stride = Sides + 1;
        for (let I = 0; I < Rings - 1; ++I)
        {
            for (let S = 0; S < Sides; ++S)
            {
                const A = BaseV + I * Stride + S;
                const B = A + Stride;
                IdxList.push(A, B, B + 1, A, B + 1, A + 1);
            }
        }
    }
}

/*==========================================================================================================================================
             TRUE 3D VOLUMETRIC SDF DUAL-SURFACE-NETS EXTRACTOR (CAVES · SMOOTH OVERHANGS · MOUNTAIN TUNNEL BORES)
==========================================================================================================================================*/
function SdfSmoothMinPoly(A, B, K)
{
    if (K <= 1e-4) return Math.min(A, B);
    const H = Clamp(0.5 + (0.5 * (B - A)) / K, 0.0, 1.0);
    return Mix(B, A, H) - K * H * (1.0 - H);
}

function SdfSmoothMaxPoly(A, B, K)
{
    return -SdfSmoothMinPoly(-A, -B, K);
}

function CapsuleSdf3D(Px, Py, Pz, Ax, Ay, Az, Bx, By, Bz, Radius)
{
    const Bax = Bx - Ax, Bay = By - Ay, Baz = Bz - Az;
    const Pax = Px - Ax, Pay = Py - Ay, Paz = Pz - Az;
    const BaDot = Bax * Bax + Bay * Bay + Baz * Baz;
    const T = BaDot > 1e-6 ? Clamp((Pax * Bax + Pay * Bay + Paz * Baz) / BaDot, 0.0, 1.0) : 0.0;
    const Dx = Pax - Bax * T, Dy = Pay - Bay * T, Dz = Paz - Baz * T;
    return Math.hypot(Dx, Dy, Dz) - Radius;
}

/**
 * Arched flat-floor 3D tunnel bore SDF along segment A -> B:
 * flat highway/rail floor at `y = centerY - archH * 0.45`, arched ceiling up to `centerY + archH`.
 */
function TunnelSegSdf3D(Px, Py, Pz, Ax, Ay, Az, Bx, By, Bz, HalfWidth, ArchHeight)
{
    const Bax = Bx - Ax, Bay = By - Ay, Baz = Bz - Az;
    const Pax = Px - Ax, Pay = Py - Ay, Paz = Pz - Az;
    const BaDot = Bax * Bax + Bay * Bay + Baz * Baz;
    const T = BaDot > 1e-6 ? Clamp((Pax * Bax + Pay * Bay + Paz * Baz) / BaDot, 0.0, 1.0) : 0.0;
    const Cx = Ax + Bax * T, Cy = Ay + Bay * T, Cz = Az + Baz * T;
    const HorizDist = Math.hypot(Px - Cx, Pz - Cz);
    const RelY = Py - Cy;
    // Semi-elliptical arch cross-section combined with flat floor plane `FloorY = -ArchHeight * 0.38`
    const NormX = HorizDist / Math.max(2.0, HalfWidth);
    const NormY = Math.max(0.0, RelY) / Math.max(2.0, ArchHeight);
    const ArchD = (Math.hypot(NormX, NormY) - 1.0) * Math.min(HalfWidth, ArchHeight);
    const SideD = HorizDist - HalfWidth;
    const FloorD = (-ArchHeight * 0.38) - RelY;
    const RoofOrWall = RelY >= 0.0 ? ArchD : SideD;
    return Math.max(RoofOrWall, FloorD);
}

function BuildVolumetric3DBricks(Region, Rt)
{
    const Ops = Region.sdf3DOps || [];
    const TunnelLayers = (Region.stack || []).filter(
        L => L && L.enabled !== false && L.type === 'spline' && L.splineMode === 'tunnel' && L.points && L.points.length >= 2
    );
    if (Ops.length === 0 && TunnelLayers.length === 0) return [];

    const B = Region.bounds;
    const Nx = Rt.nx, Nz = Rt.nz;
    const StepX = Rt.stepX, StepZ = Rt.stepZ;
    const MarginM = Math.max(StepX, StepZ) * 3.0;

    const PrimBoxes = [];
    for (const Op of Ops)
    {
        const R = (Number(Op.radius) || 24.0) + (Number(Op.blend) || 10.0) + MarginM;
        PrimBoxes.push({
            minX: Math.min(Op.ax, Op.bx) - R, maxX: Math.max(Op.ax, Op.bx) + R,
            minY: Math.min(Op.ay, Op.by) - R, maxY: Math.max(Op.ay, Op.by) + R,
            minZ: Math.min(Op.az, Op.bz) - R, maxZ: Math.max(Op.az, Op.bz) + R,
            ops: [Op], tunnelSegs: []
        });
    }

    for (const Lyr of TunnelLayers)
    {
        const Poly = SampleCatmullRomPolyline(Lyr.points, 8);
        const HalfW = Math.max(6.0, (Number(Lyr.width) || 44.0) * 0.5);
        const ArchH = Math.max(6.0, Number(Lyr.amount) || 22.0);
        const Blend = Math.max(3.0, (Number(Lyr.bankSmooth) ?? 0.35) * 16.0);
        const Segs = [];
        let MinX = 1e9, MinY = 1e9, MinZ = 1e9, MaxX = -1e9, MaxY = -1e9, MaxZ = -1e9;
        for (let I = 0; I < Poly.length - 1; ++I)
        {
            const P0 = Poly[I], P1 = Poly[I + 1];
            const Y0 = Number.isFinite(P0[2]) ? P0[2] : 25.0;
            const Y1 = Number.isFinite(P1[2]) ? P1[2] : 25.0;
            Segs.push({ ax: P0[0], ay: Y0, az: P0[1], bx: P1[0], by: Y1, bz: P1[1], halfW: HalfW, archH: ArchH, blend: Blend });
            MinX = Math.min(MinX, P0[0], P1[0]); MaxX = Math.max(MaxX, P0[0], P1[0]);
            MinY = Math.min(MinY, Y0, Y1);       MaxY = Math.max(MaxY, Y0, Y1);
            MinZ = Math.min(MinZ, P0[1], P1[1]); MaxZ = Math.max(MaxZ, P0[1], P1[1]);
        }
        const PadXZ = HalfW + Blend + MarginM;
        const PadY = ArchH + Blend + MarginM;
        PrimBoxes.push({
            minX: MinX - PadXZ, maxX: MaxX + PadXZ,
            minY: MinY - PadY,  maxY: MaxY + PadY,
            minZ: MinZ - PadXZ, maxZ: MaxZ + PadXZ,
            ops: [], tunnelSegs: Segs
        });
    }

    // Merge overlapping AABB clusters so neighboring cave/overhang/tunnel strokes share one watertight Dual-Surface-Nets brick!
    let Merged = true;
    while (Merged && PrimBoxes.length > 1)
    {
        Merged = false;
        for (let I = 0; I < PrimBoxes.length && !Merged; ++I)
        {
            for (let J = I + 1; J < PrimBoxes.length; ++J)
            {
                const A = PrimBoxes[I], C = PrimBoxes[J];
                const Disjoint = A.maxX < C.minX || A.minX > C.maxX || A.maxZ < C.minZ || A.minZ > C.maxZ;
                if (!Disjoint)
                {
                    A.minX = Math.min(A.minX, C.minX); A.maxX = Math.max(A.maxX, C.maxX);
                    A.minY = Math.min(A.minY, C.minY); A.maxY = Math.max(A.maxY, C.maxY);
                    A.minZ = Math.min(A.minZ, C.minZ); A.maxZ = Math.max(A.maxZ, C.maxZ);
                    A.ops.push(...C.ops);
                    A.tunnelSegs.push(...C.tunnelSegs);
                    PrimBoxes.splice(J, 1);
                    Merged = true;
                    break;
                }
            }
        }
    }

    const OutBricks = [];
    for (const Box of PrimBoxes)
    {
        const Gx0 = Clamp(Math.floor((Box.minX - B.x) / StepX), 1, Nx - 3);
        const Gx1 = Clamp(Math.ceil((Box.maxX - B.x) / StepX), Gx0 + 2, Nx - 2);
        const Gz0 = Clamp(Math.floor((Box.minZ - B.z) / StepZ), 1, Nz - 3);
        const Gz1 = Clamp(Math.ceil((Box.maxZ - B.z) / StepZ), Gz0 + 2, Nz - 2);

        // Scan surface heights inside [Gx0..Gx1, Gz0..Gz1] to expand vertical bounds cleanly
        let SurfMinY = 1e9, SurfMaxY = -1e9, HasActiveLand = false;
        for (let Z = Gz0; Z <= Gz1; ++Z)
        {
            for (let X = Gx0; X <= Gx1; ++X)
            {
                const Idx = Z * Nx + X;
                if (Rt.sdf2D[Idx] <= Math.max(StepX, StepZ) * 1.55) HasActiveLand = true;
                const H = Rt.heights[Idx];
                if (H < SurfMinY) SurfMinY = H;
                if (H > SurfMaxY) SurfMaxY = H;
            }
        }
        if (!HasActiveLand) continue;

        OutBricks.push({
            gx0: Gx0, gx1: Gx1, gz0: Gz0, gz1: Gz1,
            wx0: B.x + Gx0 * StepX, wx1: B.x + Gx1 * StepX,
            wz0: B.z + Gz0 * StepZ, wz1: B.z + Gz1 * StepZ,
            wy0: Math.max(Rt.slabBottom - 2.0, Math.min(SurfMinY, Box.minY) - StepX * 2.0),
            wy1: Math.max(SurfMaxY, Box.maxY) + StepX * 2.0,
            ops: Box.ops,
            tunnelSegs: Box.tunnelSegs
        });
    }
    return OutBricks;
}

function AppendVolumetric3DBricksMesh(World, Region, Rt, Bricks, PosList, NormList, ColList, AuxList, IdxList)
{
    const B = Region.bounds;
    const Nx = Rt.nx, Nz = Rt.nz;
    const StepX = Rt.stepX, StepZ = Rt.stepZ;
    const SlabBottom = Rt.slabBottom;
    const SeaLevel = World.seaLevel ?? 0.0;
    const SkirtMaxD = Math.max(StepX, StepZ) * 1.55;

    const SampleBilinear = (Arr, Wx, Wz) =>
    {
        const U = Clamp((Wx - B.x) / StepX, 0.0, Nx - 1.0001);
        const V = Clamp((Wz - B.z) / StepZ, 0.0, Nz - 1.0001);
        const X0 = Math.floor(U), Z0 = Math.floor(V);
        const Tx = U - X0, Tz = V - Z0;
        const I00 = Z0 * Nx + X0;
        return Mix(
            Mix(Arr[I00], Arr[I00 + 1], Tx),
            Mix(Arr[I00 + Nx], Arr[I00 + Nx + 1], Tx),
            Tz
        );
    };

    for (const Brk of Bricks)
    {
        const SpanX = Brk.wx1 - Brk.wx0;
        const SpanY = Math.max(8.0, Brk.wy1 - Brk.wy0);
        const SpanZ = Brk.wz1 - Brk.wz0;

        // Choose crisp 3D Dual-Surface-Nets voxel pitch (~half terrain grid cell, capped at 56^3 per brick for instant live updates!)
        const TargetPitch = Math.max(2.6, Math.min(StepX, StepZ) * 0.58);
        const VxN = Clamp(Math.round(SpanX / TargetPitch), Brk.gx1 - Brk.gx0, 56);
        const VyN = Clamp(Math.round(SpanY / TargetPitch), 12, 52);
        const VzN = Clamp(Math.round(SpanZ / TargetPitch), Brk.gz1 - Brk.gz0, 56);

        const Dx = SpanX / VxN, Dy = SpanY / VyN, Dz = SpanZ / VzN;
        const EdgeCollarM = Math.max(StepX, StepZ) * 1.8;

        const EvalBrickSdf = (Px, Py, Pz) =>
        {
            const H = SampleBilinear(Rt.heights, Px, Pz);
            const D2 = SampleBilinear(Rt.sdf2D, Px, Pz);
            // Base Thin-Slab SDF: negative inside rock (`SlabBottom < Py < H` and `D2 < SkirtMaxD`)
            let D = Math.max(Py - H, (SlabBottom + 0.35) - Py, D2 - SkirtMaxD);

            // Smoothly attenuate 3D edits near the horizontal brick collar so the perimeter matches `Py - H` seamlessly!
            const EdgeDist = Math.min(Px - Brk.wx0, Brk.wx1 - Px, Pz - Brk.wz0, Brk.wz1 - Pz);
            const CollarW = Smoothstep(0.0, EdgeCollarM, EdgeDist);
            if (CollarW <= 1e-4) return D;

            const BaseD = D;
            for (let I = 0; I < Brk.ops.length; ++I)
            {
                const Op = Brk.ops[I];
                const CapD = CapsuleSdf3D(Px, Py, Pz, Op.ax, Op.ay, Op.az, Op.bx, Op.by, Op.bz, Op.radius);
                const K = Math.max(1.5, Op.blend || 8.0);
                if (Op.op === 'cave')
                {
                    D = SdfSmoothMaxPoly(D, -CapD, K);
                }
                else
                {
                    D = SdfSmoothMinPoly(D, CapD, K);
                }
            }
            for (let I = 0; I < Brk.tunnelSegs.length; ++I)
            {
                const Seg = Brk.tunnelSegs[I];
                const TunD = TunnelSegSdf3D(Px, Py, Pz, Seg.ax, Seg.ay, Seg.az, Seg.bx, Seg.by, Seg.bz, Seg.halfW, Seg.archH);
                D = SdfSmoothMaxPoly(D, -TunD, Seg.blend);
            }
            // Keep thin-slab underside floor intact
            D = Math.max(D, (SlabBottom + 0.35) - Py);
            return Mix(BaseD, D, CollarW);
        };

        // Sample SDF on (VxN + 1) x (VyN + 1) x (VzN + 1) lattice corners
        const SX = VxN + 1, SY = VyN + 1, SZ = VzN + 1;
        const SXY = SX * SY;
        const Field = new Float32Array(SX * SY * SZ);
        for (let Z = 0; Z < SZ; ++Z)
        {
            const Pz = Brk.wz0 + Z * Dz;
            for (let Y = 0; Y < SY; ++Y)
            {
                const Py = Brk.wy0 + Y * Dy;
                const Row = Z * SXY + Y * SX;
                for (let X = 0; X < SX; ++X)
                {
                    Field[Row + X] = EvalBrickSdf(Brk.wx0 + X * Dx, Py, Pz);
                }
            }
        }

        // Dual Surface Nets cell vertices (VxN x VyN x VzN)
        const EdgePairs = [
            [0, 1], [2, 3], [4, 5], [6, 7],
            [0, 2], [1, 3], [4, 6], [5, 7],
            [0, 4], [1, 5], [2, 6], [3, 7]
        ];
        const CornerOff = [
            [0, 0, 0], [1, 0, 0], [0, 1, 0], [1, 1, 0],
            [0, 0, 1], [1, 0, 1], [0, 1, 1], [1, 1, 1]
        ];
        const DualVertIdx = new Int32Array(VxN * VyN * VzN);
        DualVertIdx.fill(-1);
        const CornerD = new Float32Array(8);

        for (let Z = 0; Z < VzN; ++Z)
        {
            for (let Y = 0; Y < VyN; ++Y)
            {
                for (let X = 0; X < VxN; ++X)
                {
                    let NegMask = 0;
                    for (let C = 0; C < 8; ++C)
                    {
                        const Off = CornerOff[C];
                        const Val = Field[(Z + Off[2]) * SXY + (Y + Off[1]) * SX + (X + Off[0])];
                        CornerD[C] = Val;
                        if (Val < 0.0) NegMask |= (1 << C);
                    }
                    if (NegMask === 0 || NegMask === 255) continue;

                    let SumX = 0.0, SumY = 0.0, SumZ = 0.0, Crossings = 0;
                    for (let E = 0; E < 12; ++E)
                    {
                        const A = EdgePairs[E][0], BIdx = EdgePairs[E][1];
                        const Da = CornerD[A], Db = CornerD[BIdx];
                        if ((Da < 0.0) === (Db < 0.0)) continue;
                        const T = Da / (Da - Db);
                        const Oa = CornerOff[A], Ob = CornerOff[BIdx];
                        SumX += Oa[0] + (Ob[0] - Oa[0]) * T;
                        SumY += Oa[1] + (Ob[1] - Oa[1]) * T;
                        SumZ += Oa[2] + (Ob[2] - Oa[2]) * T;
                        Crossings++;
                    }
                    if (Crossings === 0) continue;

                    let Wx = Brk.wx0 + (X + SumX / Crossings) * Dx;
                    let Wy = Brk.wy0 + (Y + SumY / Crossings) * Dy;
                    let Wz = Brk.wz0 + (Z + SumZ / Crossings) * Dz;

                    // Snap outer perimeter dual cells onto exact brick border so they seal cleanly against the 2.5D terrain!
                    if (X === 0) Wx = Brk.wx0;
                    else if (X === VxN - 1) Wx = Brk.wx1;
                    if (Z === 0) Wz = Brk.wz0;
                    else if (Z === VzN - 1) Wz = Brk.wz1;
                    if (X === 0 || X === VxN - 1 || Z === 0 || Z === VzN - 1)
                    {
                        Wy = SampleBilinear(Rt.heights, Wx, Wz);
                    }

                    // Skip underside wafer floor cells (already emitted by Thin-Slab underside!)
                    if (Wy <= SlabBottom + 0.65) continue;

                    // Smooth central-difference 3D SDF gradient normal
                    const Eps = Math.max(0.8, TargetPitch * 0.45);
                    const Gx = EvalBrickSdf(Wx + Eps, Wy, Wz) - EvalBrickSdf(Wx - Eps, Wy, Wz);
                    const Gy = EvalBrickSdf(Wx, Wy + Eps, Wz) - EvalBrickSdf(Wx, Wy - Eps, Wz);
                    const Gz = EvalBrickSdf(Wx, Wy, Wz + Eps) - EvalBrickSdf(Wx, Wy, Wz - Eps);
                    const GLen = Math.hypot(Gx, Gy, Gz) || 1.0;
                    const NvX = Gx / GLen, NvY = Gy / GLen, NvZ = Gz / GLen;

                    const SlopeDeg = Math.acos(Clamp(NvY, -1.0, 1.0)) * 57.2957795;
                    const D2 = SampleBilinear(Rt.sdf2D, Wx, Wz);
                    const Wat = SampleBilinear(Rt.waterChannel, Wx, Wz);
                    let [CR, CG, CB] = ComputeFlatLithologyColor(Wy, SeaLevel, Math.min(SlopeDeg, 88.0), Wat, D2, SlabBottom);
                    // Subterranean cave ceiling / overhang underside rock darkening for crisp depth read!
                    if (NvY < 0.15)
                    {
                        const CaveShade = Clamp((0.15 - NvY) * 0.65, 0.0, 0.55);
                        CR = Mix(CR, 0.095, CaveShade);
                        CG = Mix(CG, 0.088, CaveShade);
                        CB = Mix(CB, 0.082, CaveShade);
                    }

                    const VId = PosList.length / 3;
                    DualVertIdx[(Z * VyN + Y) * VxN + X] = VId;
                    PosList.push(Wx, Wy, Wz);
                    NormList.push(NvX, NvY, NvZ);
                    ColList.push(CR, CG, CB);
                    AuxList.push(SampleBilinear(Rt.falloffMap, Wx, Wz), Clamp(SlopeDeg / 90.0, 0.0, 1.0), 0.0, Wat * 0.35);
                }
            }
        }

        const CellAt = (Cx, Cy, Cz) => DualVertIdx[(Cz * VyN + Cy) * VxN + Cx];
        const EmitQuad = (C00, C10, C11, C01, Flip) =>
        {
            if (C00 < 0 || C10 < 0 || C11 < 0 || C01 < 0) return;
            if (!Flip) IdxList.push(C00, C10, C11, C00, C11, C01);
            else       IdxList.push(C00, C11, C10, C00, C01, C11);
        };

        // Emit quads for sign-changing interior lattice edges (+X, +Y, +Z)
        for (let Z = 0; Z <= VzN; ++Z)
        {
            for (let Y = 0; Y <= VyN; ++Y)
            {
                for (let X = 0; X <= VxN; ++X)
                {
                    const D0 = Field[Z * SXY + Y * SX + X];
                    // +Y edge (X in 1..VxN-1, Z in 1..VzN-1, Y < VyN) -> upward (+Y) when D0 < 0 && D1 >= 0
                    if (Y < VyN && X > 0 && X < VxN && Z > 0 && Z < VzN)
                    {
                        const D1 = Field[Z * SXY + (Y + 1) * SX + X];
                        if ((D0 < 0.0) !== (D1 < 0.0))
                        {
                            EmitQuad(
                                CellAt(X - 1, Y, Z - 1), CellAt(X - 1, Y, Z),
                                CellAt(X, Y, Z),         CellAt(X, Y, Z - 1),
                                !(D0 < 0.0)
                            );
                        }
                    }
                    // +X edge -> +X normal when D0 < 0 && D1 >= 0
                    if (X < VxN && Y > 0 && Y < VyN && Z > 0 && Z < VzN)
                    {
                        const D1 = Field[Z * SXY + Y * SX + (X + 1)];
                        if ((D0 < 0.0) !== (D1 < 0.0))
                        {
                            EmitQuad(
                                CellAt(X, Y - 1, Z - 1), CellAt(X, Y, Z - 1),
                                CellAt(X, Y, Z),         CellAt(X, Y - 1, Z),
                                !(D0 < 0.0)
                            );
                        }
                    }
                    // +Z edge -> +Z normal when D0 < 0 && D1 >= 0
                    if (Z < VzN && X > 0 && X < VxN && Y > 0 && Y < VyN)
                    {
                        const D1 = Field[(Z + 1) * SXY + Y * SX + X];
                        if ((D0 < 0.0) !== (D1 < 0.0))
                        {
                            EmitQuad(
                                CellAt(X - 1, Y - 1, Z), CellAt(X, Y - 1, Z),
                                CellAt(X, Y, Z),         CellAt(X - 1, Y, Z),
                                !(D0 < 0.0)
                            );
                        }
                    }
                }
            }
        }
    }
}

/*==========================================================================================================================================
         SEAMLESS SUB-WORLD BORDER STITCHER + PRIORITY-FLOOD HYDROSTATIC WATER BODIES (LAKES & TRUNK RIVERS)
==========================================================================================================================================*/
/**
 * Smoothly fuses shared world-space borders of adjacent `TerrainRegion` sub-world tiles (`seamLock !== false`)
 * so even after solo 1-by-1 erosion or sculpting on individual sub-worlds, shared seams meet with 0.000 m step!
 */
export function StitchAdjacentRegionBorders(World, TargetRegions = null, BlendWidthM = 68.0)
{
    const Regions = (TargetRegions || World.regions).filter(R => R && R.enabled !== false && R.seamLock !== false);
    for (const R of Regions)
    {
        if (!R.runtime || R.dirty) EvaluateTerrainRegion(World, R);
    }

    const SampleRegionHeightAt = (Reg, Wx, Wz) =>
    {
        const Rt = Reg.runtime;
        const B = Reg.bounds;
        const U = Clamp((Wx - B.x) / Rt.stepX, 0.0, Rt.nx - 1.0001);
        const V = Clamp((Wz - B.z) / Rt.stepZ, 0.0, Rt.nz - 1.0001);
        const X0 = Math.floor(U), Z0 = Math.floor(V);
        const Tx = U - X0, Tz = V - Z0;
        const I00 = Z0 * Rt.nx + X0;
        return Mix(
            Mix(Rt.heights[I00], Rt.heights[I00 + 1], Tx),
            Mix(Rt.heights[I00 + Rt.nx], Rt.heights[I00 + Rt.nx + 1], Tx),
            Tz
        );
    };

    let StitchedPairs = 0;
    const Tol = 16.0;

    for (let I = 0; I < Regions.length; ++I)
    {
        for (let J = I + 1; J < Regions.length; ++J)
        {
            const A = Regions[I], C = Regions[J];
            if (!A.runtime?.hasLand || !C.runtime?.hasLand) continue;
            const Ba = A.bounds, Bc = C.bounds;

            // Check shared East-West vertical seam (`A` West of `C` or `C` West of `A`)
            const OverlapZ0 = Math.max(Ba.z, Bc.z), OverlapZ1 = Math.min(Ba.z + Ba.depth, Bc.z + Bc.depth);
            if (OverlapZ1 - OverlapZ0 > 20.0)
            {
                let Left = null, Right = null, SeamX = 0.0;
                if (Math.abs((Ba.x + Ba.width) - Bc.x) <= Tol) { Left = A; Right = C; SeamX = 0.5 * (Ba.x + Ba.width + Bc.x); }
                else if (Math.abs((Bc.x + Bc.width) - Ba.x) <= Tol) { Left = C; Right = A; SeamX = 0.5 * (Bc.x + Bc.width + Ba.x); }

                if (Left && Right)
                {
                    const Lr = Left.runtime, Rr = Right.runtime;
                    const Samples = Math.max(16, Math.ceil((OverlapZ1 - OverlapZ0) / Math.min(Lr.stepZ, Rr.stepZ)) + 1);
                    const SeamH = new Float32Array(Samples);
                    for (let S = 0; S < Samples; ++S)
                    {
                        const Wz = OverlapZ0 + (S / (Samples - 1)) * (OverlapZ1 - OverlapZ0);
                        SeamH[S] = 0.5 * (
                            SampleRegionHeightAt(Left, Left.bounds.x + Left.bounds.width, Wz) +
                            SampleRegionHeightAt(Right, Right.bounds.x, Wz)
                        );
                    }
                    const SampleSeamZ = Wz =>
                    {
                        const T = Clamp((Wz - OverlapZ0) / Math.max(1.0, OverlapZ1 - OverlapZ0), 0.0, 1.0) * (Samples - 1);
                        const K0 = Math.floor(T), K1 = Math.min(Samples - 1, K0 + 1);
                        return Mix(SeamH[K0], SeamH[K1], T - K0);
                    };

                    const BlendSide = (Reg, EdgeX) =>
                    {
                        const Rt = Reg.runtime, B = Reg.bounds;
                        for (let Z = 0; Z < Rt.nz; ++Z)
                        {
                            const Wz = B.z + Z * Rt.stepZ;
                            if (Wz < OverlapZ0 - Tol || Wz > OverlapZ1 + Tol) continue;
                            const ZFade = Smoothstep(0.0, 24.0, Math.min(Wz - OverlapZ0 + Tol, OverlapZ1 + Tol - Wz));
                            const TargetSeam = SampleSeamZ(Wz);
                            for (let X = 0; X < Rt.nx; ++X)
                            {
                                const Wx = B.x + X * Rt.stepX;
                                const Dist = Math.abs(Wx - EdgeX);
                                if (Dist > BlendWidthM) continue;
                                const Idx = Z * Rt.nx + X;
                                if (Rt.sdf2D[Idx] > Rt.stepX * 1.5) continue;
                                const W = (1.0 - Smoothstep(0.0, BlendWidthM, Dist)) * ZFade;
                                const Cur = Rt.heights[Idx];
                                // Add decayed seam correction `(TargetSeam - EdgeCur) * W` to preserve interior micro-relief!
                                const EdgeCur = SampleRegionHeightAt(Reg, EdgeX, Wz);
                                Rt.heights[Idx] = Cur + (TargetSeam - EdgeCur) * W;
                            }
                        }
                        Reg.meshDirty = true;
                    };
                    BlendSide(Left, Left.bounds.x + Left.bounds.width);
                    BlendSide(Right, Right.bounds.x);
                    StitchedPairs++;
                }
            }

            // Check shared North-South horizontal seam (`A` North of `C` or `C` North of `A`)
            const OverlapX0 = Math.max(Ba.x, Bc.x), OverlapX1 = Math.min(Ba.x + Ba.width, Bc.x + Bc.width);
            if (OverlapX1 - OverlapX0 > 20.0)
            {
                let TopR = null, BotR = null;
                if (Math.abs((Ba.z + Ba.depth) - Bc.z) <= Tol) { TopR = A; BotR = C; }
                else if (Math.abs((Bc.z + Bc.depth) - Ba.z) <= Tol) { TopR = C; BotR = A; }

                if (TopR && BotR)
                {
                    const Tr = TopR.runtime, Br = BotR.runtime;
                    const Samples = Math.max(16, Math.ceil((OverlapX1 - OverlapX0) / Math.min(Tr.stepX, Br.stepX)) + 1);
                    const SeamH = new Float32Array(Samples);
                    for (let S = 0; S < Samples; ++S)
                    {
                        const Wx = OverlapX0 + (S / (Samples - 1)) * (OverlapX1 - OverlapX0);
                        SeamH[S] = 0.5 * (
                            SampleRegionHeightAt(TopR, Wx, TopR.bounds.z + TopR.bounds.depth) +
                            SampleRegionHeightAt(BotR, Wx, BotR.bounds.z)
                        );
                    }
                    const SampleSeamX = Wx =>
                    {
                        const T = Clamp((Wx - OverlapX0) / Math.max(1.0, OverlapX1 - OverlapX0), 0.0, 1.0) * (Samples - 1);
                        const K0 = Math.floor(T), K1 = Math.min(Samples - 1, K0 + 1);
                        return Mix(SeamH[K0], SeamH[K1], T - K0);
                    };

                    const BlendSideZ = (Reg, EdgeZ) =>
                    {
                        const Rt = Reg.runtime, B = Reg.bounds;
                        for (let Z = 0; Z < Rt.nz; ++Z)
                        {
                            const Wz = B.z + Z * Rt.stepZ;
                            const Dist = Math.abs(Wz - EdgeZ);
                            if (Dist > BlendWidthM) continue;
                            const BaseW = 1.0 - Smoothstep(0.0, BlendWidthM, Dist);
                            for (let X = 0; X < Rt.nx; ++X)
                            {
                                const Wx = B.x + X * Rt.stepX;
                                if (Wx < OverlapX0 - Tol || Wx > OverlapX1 + Tol) continue;
                                const Idx = Z * Rt.nx + X;
                                if (Rt.sdf2D[Idx] > Rt.stepX * 1.5) continue;
                                const XFade = Smoothstep(0.0, 24.0, Math.min(Wx - OverlapX0 + Tol, OverlapX1 + Tol - Wx));
                                const TargetSeam = SampleSeamX(Wx);
                                const EdgeCur = SampleRegionHeightAt(Reg, Wx, EdgeZ);
                                Rt.heights[Idx] += (TargetSeam - EdgeCur) * BaseW * XFade;
                            }
                        }
                        Reg.meshDirty = true;
                    };
                    BlendSideZ(TopR, TopR.bounds.z + TopR.bounds.depth);
                    BlendSideZ(BotR, BotR.bounds.z);
                    StitchedPairs++;
                }
            }
        }
    }
    return StitchedPairs;
}

/**
 * Priority-Flood Closed-Basin Lake + Trunk River Water-Body Extractor (harvested & upgraded from `fe92aaa5`):
 * - Solves exact hydrostatic spill elevations for upland closed basins (caldera lakes, glacial tarns, dammed valleys).
 * - Combines closed lakes (`lakeDepth >= 0.45 m`) with simulated river channels (`Er.w >= 0.14 m` or river/lake splines).
 */
export function ComputeHydrostaticWaterBodies(World, Region)
{
    const Rt = Region?.runtime;
    if (!Rt || !Rt.hasLand)
    {
        return {
            positions: new Float32Array(0),
            aux: new Float32Array(0), // [depthM, isRiver]
            indices: new Uint32Array(0),
            vertexCount: 0,
            triangleCount: 0,
            lakeCells: 0,
            riverCells: 0
        };
    }

    const Nx = Rt.nx, Nz = Rt.nz, Count = Nx * Nz;
    const B = Region.bounds;
    const StepX = Rt.stepX, StepZ = Rt.stepZ;
    const SeaLevel = World.seaLevel ?? 0.0;
    const H = Rt.heights;
    const Sdf2D = Rt.sdf2D;

    // 1. O(N log N) Priority-Flood depression fill seeded from ocean / grid perimeter outlets
    const Filled = new Float32Array(Count);
    const Visited = new Uint8Array(Count);
    const HeapIdx = new Int32Array(Count + 4);
    const HeapVal = new Float32Array(Count + 4);
    let HeapSize = 0;

    const HeapPush = (Cell, Elev) =>
    {
        let I = ++HeapSize;
        while (I > 1)
        {
            const P = I >> 1;
            if (HeapVal[P] <= Elev) break;
            HeapIdx[I] = HeapIdx[P];
            HeapVal[I] = HeapVal[P];
            I = P;
        }
        HeapIdx[I] = Cell;
        HeapVal[I] = Elev;
    };

    const HeapPop = () =>
    {
        const Top = HeapIdx[1];
        const LastIdx = HeapIdx[HeapSize];
        const LastVal = HeapVal[HeapSize--];
        let I = 1;
        while ((I << 1) <= HeapSize)
        {
            let C = I << 1;
            if (C + 1 <= HeapSize && HeapVal[C + 1] < HeapVal[C]) C++;
            if (LastVal <= HeapVal[C]) break;
            HeapIdx[I] = HeapIdx[C];
            HeapVal[I] = HeapVal[C];
            I = C;
        }
        if (HeapSize >= 1) { HeapIdx[I] = LastIdx; HeapVal[I] = LastVal; }
        return Top;
    };

    for (let Z = 0; Z < Nz; ++Z)
    {
        for (let X = 0; X < Nx; ++X)
        {
            const I = Z * Nx + X;
            const IsBoundary = X === 0 || X === Nx - 1 || Z === 0 || Z === Nz - 1 || Sdf2D[I] > 0.0;
            if (IsBoundary)
            {
                Visited[I] = 1;
                const SeedH = Math.max(H[I], SeaLevel);
                Filled[I] = SeedH;
                HeapPush(I, SeedH);
            }
        }
    }

    const OffX = [-1, 1, 0, 0, -1, 1, -1, 1];
    const OffZ = [0, 0, -1, 1, -1, -1, 1, 1];
    while (HeapSize > 0)
    {
        const Cur = HeapPop();
        const CurFill = Filled[Cur];
        const Cx = Cur % Nx, Cz = (Cur / Nx) | 0;
        for (let K = 0; K < 8; ++K)
        {
            const Mx = Cx + OffX[K], Mz = Cz + OffZ[K];
            if (Mx < 0 || Mx >= Nx || Mz < 0 || Mz >= Nz) continue;
            const Nb = Mz * Nx + Mx;
            if (Visited[Nb]) continue;
            Visited[Nb] = 1;
            const NextFill = Math.max(H[Nb], CurFill);
            Filled[Nb] = NextFill;
            HeapPush(Nb, NextFill);
        }
    }

    // 2. Classify hydrostatic closed lakes (above sea level) & channeled rivers
    const WaterSurfY = new Float32Array(Count);
    const WaterDepth = new Float32Array(Count);
    const WaterKind = new Uint8Array(Count); // 0 = dry, 1 = lake, 2 = river
    const Er = Rt.erosion;
    let LakeCells = 0, RiverCells = 0;

    for (let I = 0; I < Count; ++I)
    {
        if (Sdf2D[I] > -StepX * 0.5) continue;
        const LakeD = Filled[I] - H[I];
        const SimW = (Er && I < Er.w.length) ? Er.w[I] : 0.0;
        const SplineW = Rt.waterChannel ? Rt.waterChannel[I] : 0.0;

        if (LakeD >= 0.45 && Filled[I] > SeaLevel + 0.6)
        {
            WaterKind[I] = 1;
            WaterSurfY[I] = Filled[I];
            WaterDepth[I] = LakeD;
            LakeCells++;
        }
        else if ((SimW >= 0.14 || SplineW >= 0.45) && H[I] > SeaLevel + 0.35)
        {
            const RivD = Math.max(SimW, SplineW * 1.15);
            WaterKind[I] = 2;
            WaterSurfY[I] = H[I] + Math.min(RivD, 2.8) * 0.35 + 0.16;
            WaterDepth[I] = RivD;
            RiverCells++;
        }
    }

    if (LakeCells + RiverCells < 4)
    {
        return {
            positions: new Float32Array(0),
            aux: new Float32Array(0),
            indices: new Uint32Array(0),
            vertexCount: 0,
            triangleCount: 0,
            lakeCells: LakeCells,
            riverCells: RiverCells
        };
    }

    // 3. Extract smooth inland water-surface mesh (with shoreline skirt pinch so water tucks cleanly into banks!)
    const VertMap = new Int32Array(Count);
    VertMap.fill(-1);
    const Pos = [];
    const Aux = [];
    const Idx = [];

    const GetVert = (X, Z) =>
    {
        const I = Z * Nx + X;
        if (VertMap[I] >= 0) return VertMap[I];
        const Kind = WaterKind[I];
        let Wy = Kind > 0 ? WaterSurfY[I] : H[I] - 0.12;
        if (Kind === 0)
        {
            // Bank vertex: average neighboring water surface slightly below bank height so water seals cleanly into terrain!
            let SumY = 0.0, Cnt = 0;
            for (let K = 0; K < 4; ++K)
            {
                const Mx = Clamp(X + OffX[K], 0, Nx - 1), Mz = Clamp(Z + OffZ[K], 0, Nz - 1);
                const Nb = Mz * Nx + Mx;
                if (WaterKind[Nb] > 0) { SumY += WaterSurfY[Nb]; Cnt++; }
            }
            if (Cnt > 0) Wy = SumY / Cnt - 0.05;
        }
        const VId = Pos.length / 3;
        VertMap[I] = VId;
        Pos.push(B.x + X * StepX, Wy, B.z + Z * StepZ);
        Aux.push(WaterDepth[I], Kind === 2 ? 1.0 : 0.0);
        return VId;
    };

    for (let Z = 0; Z < Nz - 1; ++Z)
    {
        for (let X = 0; X < Nx - 1; ++X)
        {
            const C00 = Z * Nx + X, C10 = C00 + 1, C01 = C00 + Nx, C11 = C01 + 1;
            const WetCount = (WaterKind[C00] > 0 ? 1 : 0) + (WaterKind[C10] > 0 ? 1 : 0) +
                             (WaterKind[C01] > 0 ? 1 : 0) + (WaterKind[C11] > 0 ? 1 : 0);
            if (WetCount < 3) continue;
            const V00 = GetVert(X, Z), V10 = GetVert(X + 1, Z), V01 = GetVert(X, Z + 1), V11 = GetVert(X + 1, Z + 1);
            Idx.push(V00, V01, V11, V00, V11, V10);
        }
    }

    return {
        positions: new Float32Array(Pos),
        aux: new Float32Array(Aux),
        indices: new Uint32Array(Idx),
        vertexCount: Pos.length / 3,
        triangleCount: Idx.length / 3,
        lakeCells: LakeCells,
        riverCells: RiverCells
    };
}

/*==========================================================================================================================================
                                         TRIANGLE MESH EXPORTERS (.OBJ & BINARY .STL)
==========================================================================================================================================*/
export function ExportWorldTerrainsObj(World, TargetRegions)
{
    const Lines = [
        `# Frontier · Open-World Thin-Slab SDF Terrain Export (.obj)`,
        `# Regions: ${TargetRegions.length} | Sea Level: ${(World.seaLevel ?? 0).toFixed(2)} m`
    ];
    let VertOffset = 1;
    let TotalTris = 0;

    for (const Region of TargetRegions)
    {
        if (!Region.enabled) continue;
        const Mesh = ExtractRegionTriangleMesh(World, Region, { includeUnderside: true });
        if (Mesh.triangleCount === 0) continue;

        Lines.push(`o ${Region.name.replace(/\s+/g, '_')}`);
        const P = Mesh.positions, N = Mesh.normals, C = Mesh.colors, Idx = Mesh.indices;
        for (let I = 0; I < Mesh.vertexCount; ++I)
        {
            const K = I * 3;
            Lines.push(`v ${P[K].toFixed(3)} ${P[K + 1].toFixed(3)} ${P[K + 2].toFixed(3)} ${C[K].toFixed(3)} ${C[K + 1].toFixed(3)} ${C[K + 2].toFixed(3)}`);
        }
        for (let I = 0; I < Mesh.vertexCount; ++I)
        {
            const K = I * 3;
            Lines.push(`vn ${N[K].toFixed(4)} ${N[K + 1].toFixed(4)} ${N[K + 2].toFixed(4)}`);
        }
        for (let T = 0; T < Idx.length; T += 3)
        {
            const A = Idx[T] + VertOffset, B = Idx[T + 1] + VertOffset, D = Idx[T + 2] + VertOffset;
            Lines.push(`f ${A}//${A} ${B}//${B} ${D}//${D}`);
        }
        VertOffset += Mesh.vertexCount;
        TotalTris += Mesh.triangleCount;
    }
    return { text: Lines.join('\n') + '\n', triangleCount: TotalTris };
}

export function ExportWorldTerrainsBinaryStl(World, TargetRegions)
{
    const Meshes = [];
    let TotalTris = 0;
    for (const Region of TargetRegions)
    {
        if (!Region.enabled) continue;
        const M = ExtractRegionTriangleMesh(World, Region, { includeUnderside: true });
        if (M.triangleCount > 0) { Meshes.push(M); TotalTris += M.triangleCount; }
    }

    const Buffer = new ArrayBuffer(84 + TotalTris * 50);
    const View = new DataView(Buffer);
    const Header = 'Frontier Open-World SDF Terrain Studio Binary STL';
    for (let I = 0; I < 80; ++I) View.setUint8(I, I < Header.length ? Header.charCodeAt(I) : 0x20);
    View.setUint32(80, TotalTris, true);

    let Off = 84;
    for (const M of Meshes)
    {
        const P = M.positions, Idx = M.indices;
        for (let T = 0; T < Idx.length; T += 3)
        {
            const I0 = Idx[T] * 3, I1 = Idx[T + 1] * 3, I2 = Idx[T + 2] * 3;
            const Ex1 = P[I1] - P[I0], Ey1 = P[I1 + 1] - P[I0 + 1], Ez1 = P[I1 + 2] - P[I0 + 2];
            const Ex2 = P[I2] - P[I0], Ey2 = P[I2 + 1] - P[I0 + 1], Ez2 = P[I2 + 2] - P[I0 + 2];
            const Nx = Ey1 * Ez2 - Ez1 * Ey2, Ny = Ez1 * Ex2 - Ex1 * Ez2, Nz = Ex1 * Ey2 - Ey1 * Ex2;
            const L = Math.hypot(Nx, Ny, Nz) || 1.0;

            View.setFloat32(Off, Nx / L, true);
            View.setFloat32(Off + 4, Ny / L, true);
            View.setFloat32(Off + 8, Nz / L, true);
            View.setFloat32(Off + 12, P[I0], true);
            View.setFloat32(Off + 16, P[I0 + 1], true);
            View.setFloat32(Off + 20, P[I0 + 2], true);
            View.setFloat32(Off + 24, P[I1], true);
            View.setFloat32(Off + 28, P[I1 + 1], true);
            View.setFloat32(Off + 32, P[I1 + 2], true);
            View.setFloat32(Off + 36, P[I2], true);
            View.setFloat32(Off + 40, P[I2 + 1], true);
            View.setFloat32(Off + 44, P[I2 + 2], true);
            View.setUint16(Off + 48, 0, true);
            Off += 50;
        }
    }
    return { bytes: new Uint8Array(Buffer), triangleCount: TotalTris };
}

/*==========================================================================================================================================
                              3 DEMONSTRATION 2D MASKS + INDEPENDENT TERRAIN BOUNDS REGIONS
==========================================================================================================================================*/
/**
 * Populates the studio with 3 contrasting Demonstration Masks & Independent Terrain Bounds Boxes:
 * 1. "Highland Massif" (NW Peninsula) — Soft Edge -> Strong Center Multifractal + Continental Ridges + River Spline
 * 2. "Basalt Sea Cliffs" (East Headland) — NONE (Full Cliff Edge) Falloff Voronoi Mesa + Stepped Terraces (sheer ocean cliffs!)
 * 3. "Caldera Atoll" (South Crescent) — Coastal Rim Ring Falloff + Folded Foothills + Canyon Breach Spline
 */
export function SeedThreeDemoTerrains(World)
{
    World.maskLayers = [];
    World.regions = [];
    World.seaLevel = 0.0;
    World.oceanVisible = true;

    // --- DEMO MASK 1: North-West Highland Peninsula ---
    const Mask1 = World.addMaskLayer({
        id: 'mask-highland',
        name: 'Mask 01 · Highland Peninsula',
        color: '#58b982'
    });
    World.paintPolygonMeters(Mask1, [
        [-1520, -980],
        [-780, -1140],
        [-210, -890],
        [-90, -420],
        [-380, -160],
        [-240, 180],
        [-680, 340],
        [-1320, 190],
        [-1640, -340]
    ], false, 0.34, 4029);
    // Carve an organic fjord bay on the eastern coast
    World.paintCapsuleMeters(Mask1, [-120, -250], [-490, -290], 135, 'erase', 1.0, 0.25, 4091);

    const Bounds1 = World.computeLayerPaintedBounds(Mask1, 95.0);
    const Region1 = World.addTerrainRegion({
        id: 'terrain-highland',
        name: 'Terrain 01 · Highland Massif',
        maskLayerId: Mask1.id,
        color: '#e8c27a',
        bounds: Bounds1,
        resolution: 148,
        baseElevation: 3.0,
        slabThickness: 6.5,
        shorelineBevel: 22.0,
        maxRelief: 280.0,
        stack: [
            CreateTerrainLayer('multifractal', {
                name: 'Continental Multifractal Base',
                amplitude: 195.0,
                scale: 2.1,
                octaves: 6,
                gain: 0.54,
                warp: 0.35,
                seed: 4029,
                falloff: { mode: 'soft', distance: 340.0, edgeFloor: 0.04 }
            }),
            CreateTerrainLayer('ridged', {
                name: 'Alpine Arête Spine',
                amplitude: 145.0,
                scale: 2.45,
                octaves: 6,
                sharpness: 0.76,
                azimuth: 34.0,
                warp: 0.36,
                seed: 5113,
                falloff: { mode: 'continental', distance: 430.0, edgeFloor: 0.0 }
            }),
            CreateTerrainLayer('spline', {
                name: 'Glacial River Valley',
                splineMode: 'river',
                width: 115.0,
                amount: 45.0,
                bankSmooth: 0.65,
                meander: 0.28,
                seed: 6007,
                falloff: { mode: 'none', distance: 120.0, edgeFloor: 1.0 },
                points: [
                    [-1180, -640],
                    [-920, -470],
                    [-680, -350],
                    [-430, -280]
                ]
            })
        ]
    });

    // --- DEMO MASK 2: East Sheer Basalt Sea-Cliff Plateau (Falloff = NONE for sheer vertical coastal cliffs!) ---
    const Mask2 = World.addMaskLayer({
        id: 'mask-cliffs',
        name: 'Mask 02 · Basalt Headland',
        color: '#5ac8fa'
    });
    World.paintPolygonMeters(Mask2, [
        [340, -960],
        [1080, -1020],
        [1360, -560],
        [1240, 40],
        [680, 120],
        [290, -260],
        [420, -620]
    ], false, 0.22, 7331);
    // Add two sea-stack satellite islets off the western cape!
    World.paintCapsuleMeters(Mask2, [175, -450], [205, -420], 58, 'paint', 1.0, 0.25, 7399);
    World.paintCapsuleMeters(Mask2, [215, -120], [235, -95], 45, 'paint', 1.0, 0.20, 7411);

    const Bounds2 = World.computeLayerPaintedBounds(Mask2, 90.0);
    const Region2 = World.addTerrainRegion({
        id: 'terrain-cliffs',
        name: 'Terrain 02 · Basalt Sea Cliffs',
        maskLayerId: Mask2.id,
        color: '#5ac8fa',
        bounds: Bounds2,
        resolution: 140,
        baseElevation: 8.0,
        slabThickness: 6.5,
        shorelineBevel: 0.0, // Zero shore bevel -> sheer cliffs rising straight from ocean!
        maxRelief: 240.0,
        stack: [
            CreateTerrainLayer('voronoi', {
                name: 'Basalt Mesa Blocks (No Falloff)',
                variant: 0,
                amplitude: 148.0,
                scale: 2.9,
                jitter: 0.78,
                sharpness: 0.82,
                warp: 0.20,
                seed: 8191,
                falloff: { mode: 'none', distance: 160.0, edgeFloor: 1.0 }
            }),
            CreateTerrainLayer('multifractal', {
                name: 'Caprock Crown Relief',
                amplitude: 62.0,
                scale: 3.1,
                octaves: 5,
                gain: 0.50,
                warp: 0.25,
                seed: 8223,
                falloff: { mode: 'plateau', distance: 95.0, edgeFloor: 0.45 }
            }),
            CreateTerrainLayer('terrace', {
                name: 'Columnar Stratigraphic Benches',
                bedCount: 8,
                strength: 0.82,
                dipDeg: 3.5,
                strikeDeg: 18.0,
                falloff: { mode: 'none', distance: 100.0, edgeFloor: 1.0 }
            })
        ]
    });

    // --- DEMO MASK 3: South Volcanic Caldera Atoll ---
    const Mask3 = World.addMaskLayer({
        id: 'mask-atoll',
        name: 'Mask 03 · Caldera Crescent',
        color: '#a3d977'
    });
    World.paintCapsuleMeters(Mask3, [-380, 740], [340, 780], 490, 'paint', 1.0, 0.32, 9103);
    // Carve central lagoon opening to the north-east
    World.paintCapsuleMeters(Mask3, [-20, 730], [260, 560], 225, 'erase', 1.0, 0.24, 9209);

    const Bounds3 = World.computeLayerPaintedBounds(Mask3, 90.0);
    const Region3 = World.addTerrainRegion({
        id: 'terrain-atoll',
        name: 'Terrain 03 · Caldera Atoll',
        maskLayerId: Mask3.id,
        color: '#3ec46d',
        bounds: Bounds3,
        resolution: 136,
        baseElevation: 2.5,
        slabThickness: 6.5,
        shorelineBevel: 24.0,
        maxRelief: 220.0,
        stack: [
            CreateTerrainLayer('multifractal', {
                name: 'Volcanic Rim Ring',
                amplitude: 165.0,
                scale: 2.4,
                octaves: 6,
                gain: 0.54,
                warp: 0.30,
                seed: 9413,
                falloff: { mode: 'rim', distance: 290.0, edgeFloor: 0.05 }
            }),
            CreateTerrainLayer('billow', {
                name: 'Coastal Folded Foothills',
                amplitude: 58.0,
                scale: 3.6,
                octaves: 5,
                duneMix: 0.30,
                azimuth: 52.0,
                seed: 9521,
                falloff: { mode: 'soft', distance: 170.0, edgeFloor: 0.0 }
            }),
            CreateTerrainLayer('spline', {
                name: 'Caldera Canyon Breach',
                splineMode: 'canyon',
                width: 88.0,
                amount: 64.0,
                bankSmooth: 0.42,
                meander: 0.18,
                seed: 9619,
                falloff: { mode: 'none', distance: 100.0, edgeFloor: 1.0 },
                points: [
                    [-480, 920],
                    [-290, 820],
                    [-110, 745]
                ]
            })
        ]
    });

    EvaluateTerrainRegion(World, Region1);
    EvaluateTerrainRegion(World, Region2);
    EvaluateTerrainRegion(World, Region3);

    return { masks: [Mask1, Mask2, Mask3], regions: [Region1, Region2, Region3] };
}
