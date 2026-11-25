/**
 * GaeaTexturingAndStamps.js — Gaea-Style Data Maps, Procedural Stamps, SatMap
 * Ramps & PNG/RAW Export for the Open-World SDF Terrain Studio.
 * ===========================================================================
 * After erosion baking, this module derives a WIDE inspector-ready range of
 * simulated & derived data maps (far richer than a height/slope pair) plus
 * procedural geological surface stamps and multi-stop satellite color ramps.
 *
 * Everything is pure JS (node + browser): maps are Float32Array fields on the
 * region grid; PNG export uses a built-in uncompressed-PNG encoder so no DOM
 * is required. In the browser the studio shows any map as a viewport lens and
 * exports it as PNG/RAW.
 */

import { Clamp, Mix, Smoothstep } from './TerrainLayerStack.js';
import { Hash01, StrataHardnessAt } from './ErosionPhysicsEngine.js';

/*==========================================================================================================================================
                                                    DATA MAP CATALOG + COMPUTE
==========================================================================================================================================*/
export const DATA_MAP_CATALOG = [
    { id: 'altitude',        label: 'Altitude',            category: 'Terrain',    unit: 'm',    color: '#e8c27a' },
    { id: 'slope',           label: 'Slope',               category: 'Terrain',    unit: '°',    color: '#f0935b' },
    { id: 'convexity',       label: 'Convexity',           category: 'Terrain',    unit: '',     color: '#c9d66c' },
    { id: 'cavity',          label: 'Cavity / Concavity',  category: 'Terrain',    unit: '',     color: '#8fc7e8' },
    { id: 'coastDist',       label: 'Coast Distance',      category: 'Terrain',    unit: 'm',    color: '#5ac8fa' },
    { id: 'strata',          label: '3D Strata Hardness',  category: 'Geology',    unit: '',     color: '#d6a2e8' },
    { id: 'flow',            label: 'Flow Accumulation',   category: 'Hydrology',  unit: '',     color: '#4fa3ff' },
    { id: 'velocity',        label: 'Flow Velocity',       category: 'Hydrology',  unit: 'm/s',  color: '#43d3c4' },
    { id: 'shear',           label: 'Stream Shear (Q·S)',  category: 'Hydrology',  unit: '',     color: '#2e86ff' },
    { id: 'wetness',         label: 'Wetness',             category: 'Hydrology',  unit: '',     color: '#37c8b0' },
    { id: 'wear',            label: 'Wear / Incision',     category: 'Erosion',    unit: 'm',    color: '#ff7b7b' },
    { id: 'deposition',      label: 'Deposition',          category: 'Erosion',    unit: 'm',    color: '#ffd28a' },
    { id: 'talus',           label: 'Talus / Scree',       category: 'Erosion',    unit: 'm',    color: '#c9a06a' },
    { id: 'soil',            label: 'Soil / Alluvium',     category: 'Erosion',    unit: 'm',    color: '#a3d977' },
    { id: 'sand',            label: 'Aeolian Sand',        category: 'Erosion',    unit: 'm',    color: '#f2d27a' },
    { id: 'pitting',         label: 'Pitting / Cracks',    category: 'Erosion',    unit: '',     color: '#ff9d5c' },
    { id: 'tafoni',          label: 'Tafoni Honeycomb',    category: 'Erosion',    unit: '',     color: '#e8926c' },
    { id: 'snow',            label: 'Snow Cover',          category: 'Climate',    unit: 'm',    color: '#eaf4ff' },
    { id: 'ice',             label: 'Glacier Ice',         category: 'Climate',    unit: 'm',    color: '#9fd8ff' },
    { id: 'moisture',        label: 'Soil Moisture',       category: 'Climate',    unit: '',     color: '#7fc98a' },
    { id: 'insolation',      label: 'Insolation / Aspect', category: 'Climate',    unit: '',     color: '#ffb35c' },
    { id: 'stainFe',         label: 'Iron Rust Stain',     category: 'Chemical',   unit: '',     color: '#c2643c' },
    { id: 'stainCu',         label: 'Copper Patina',       category: 'Chemical',   unit: '',     color: '#57c7a2' },
    { id: 'stainMn',         label: 'Desert Varnish (Mn)', category: 'Chemical',   unit: '',     color: '#6e5a72' },
    { id: 'salt',            label: 'Salt / Evaporite',    category: 'Chemical',   unit: '',     color: '#f0ead2' }
];

function NormalizeField(Src, Nx, Nz, Sdf2D, LogScale = false)
{
    const Out = new Float32Array(Nx * Nz);
    let Lo = Infinity, Hi = -Infinity;
    for (let I = 0; I < Nx * Nz; ++I)
    {
        if (Sdf2D && Sdf2D[I] >= 0) continue;
        let V = Src[I];
        if (LogScale) V = Math.log1p(Math.max(0, V));
        if (V < Lo) Lo = V;
        if (V > Hi) Hi = V;
    }
    if (!Number.isFinite(Lo) || !Number.isFinite(Hi) || Hi - Lo < 1e-12) { Lo = 0; Hi = 1; }
    for (let I = 0; I < Nx * Nz; ++I)
    {
        let V = Src[I];
        if (LogScale) V = Math.log1p(Math.max(0, V));
        Out[I] = Clamp((V - Lo) / (Hi - Lo), 0, 1);
    }
    return { data: Out, min: Lo, max: Hi };
}

/**
 * Compute the full data-map suite from a region + its erosion state.
 * Returns { id: { meta, data (normalized 0..1), min, max } }.
 * Works with State == null too (derives purely static terrain maps).
 */
export function ComputeDataMaps(Region, State = null, Options = {})
{
    const Rt = Region.runtime;
    if (!Rt) throw new Error('ComputeDataMaps: evaluate the region first');
    const Nx = Rt.nx, Nz = Rt.nz, N = Nx * Nz;
    const Sdf = Rt.sdf2D;
    const H = State ? State.h : Rt.heights;
    const StepX = Rt.stepX, StepZ = Rt.stepZ;
    const Sea = State ? State.seaLevel : 0;

    // ---- static derivatives ----
    const SlopeF = new Float32Array(N), ConvF = new Float32Array(N), CavityF = new Float32Array(N), InsolF = new Float32Array(N), StrataF = new Float32Array(N);
    const AltF = new Float32Array(N), CoastF = new Float32Array(N);
    for (let Z = 0; Z < Nz; ++Z)
    {
        for (let X = 0; X < Nx; ++X)
        {
            const I = Z * Nx + X;
            const Xm = Math.max(0, X - 1), Xp = Math.min(Nx - 1, X + 1);
            const Zm = Math.max(0, Z - 1), Zp = Math.min(Nz - 1, Z + 1);
            const DhDx = (H[Z * Nx + Xp] - H[Z * Nx + Xm]) / ((Xp - Xm || 1) * StepX);
            const DhDz = (H[Zp * Nx + X] - H[Zm * Nx + X]) / ((Zp - Zm || 1) * StepZ);
            SlopeF[I] = Math.atan(Math.hypot(DhDx, DhDz)) * 57.2957795;
            const Lap = (H[Z * Nx + Xm] + H[Z * Nx + Xp] + H[Zm * Nx + X] + H[Zp * Nx + X]) * 0.25 - H[I];
            ConvF[I] = Clamp(-Lap * 2.2, -1, 1);
            CavityF[I] = Clamp(Lap * 2.2, -1, 1);
            // Aspect-based insolation proxy (S-facing bright in N hemisphere).
            InsolF[I] = Clamp(0.5 + 0.5 * (DhDx * 0.45 + DhDz * 0.89) / Math.max(0.25, Math.hypot(DhDx, DhDz) + 0.35), 0, 1);
            StrataF[I] = State ? Clamp(0.5 + 0.5 * StrataHardnessAt(State, I, X, Z), 0, 1) : 0.5 + 0.5 * Math.sin(H[I] * 0.25);
            AltF[I] = H[I] - Sea;
            CoastF[I] = Sdf[I] < 0 ? Math.min(-Sdf[I], 2000) : 0;
        }
    }

    const Static = {
        altitude: NormalizeField(AltF, Nx, Nz, Sdf),
        slope: NormalizeField(SlopeF, Nx, Nz, Sdf),
        convexity: NormalizeField(ConvF, Nx, Nz, Sdf),
        cavity: NormalizeField(CavityF, Nx, Nz, Sdf),
        coastDist: NormalizeField(CoastF, Nx, Nz, Sdf),
        strata: NormalizeField(StrataF, Nx, Nz, Sdf),
        insolation: NormalizeField(InsolF, Nx, Nz, Sdf)
    };

    const Maps = {};
    for (const Meta of DATA_MAP_CATALOG)
    {
        if (Static[Meta.id]) Maps[Meta.id] = { meta: Meta, ...Static[Meta.id] };
    }

    if (State)
    {
        const ShearF = new Float32Array(N);
        for (let I = 0; I < N; ++I) ShearF[I] = State.discharge[I] * Math.max(0.001, SlopeF[I] / 57.3);
        const Sim = {
            flow: NormalizeField(State.discharge, Nx, Nz, Sdf, true),
            velocity: NormalizeField(State.vel, Nx, Nz, Sdf),
            shear: NormalizeField(ShearF, Nx, Nz, Sdf, true),
            wetness: NormalizeField(State.wetness, Nx, Nz, Sdf),
            wear: NormalizeField(State.wearMap, Nx, Nz, Sdf),
            deposition: NormalizeField(State.depoMap, Nx, Nz, Sdf),
            talus: NormalizeField(State.talusMap, Nx, Nz, Sdf),
            soil: NormalizeField(State.soil, Nx, Nz, Sdf),
            sand: NormalizeField(State.sand, Nx, Nz, Sdf),
            pitting: NormalizeField(State.crack, Nx, Nz, Sdf),
            tafoni: NormalizeField(State.pitTafoni, Nx, Nz, Sdf),
            snow: NormalizeField(State.snow, Nx, Nz, Sdf),
            ice: NormalizeField(State.ice, Nx, Nz, Sdf),
            moisture: NormalizeField(State.moist, Nx, Nz, Sdf),
            stainFe: NormalizeField(State.fe, Nx, Nz, Sdf),
            stainCu: NormalizeField(State.cu, Nx, Nz, Sdf),
            stainMn: NormalizeField(State.mn, Nx, Nz, Sdf),
            salt: NormalizeField(State.salt, Nx, Nz, Sdf)
        };
        for (const Meta of DATA_MAP_CATALOG)
        {
            if (Sim[Meta.id]) Maps[Meta.id] = { meta: Meta, ...Sim[Meta.id] };
        }
    }
    return Maps;
}

/*==========================================================================================================================================
                                            PROCEDURAL GEOLOGICAL SURFACE STAMPS
==========================================================================================================================================*/
export const STAMP_CATALOG = [
    { id: 'crossBeddedSandstone', label: 'Cross-Bedded Sandstone', desc: 'Dipping cross-bed foresets with differential soft-band relief', amplitude: 1.6, frequency: 0.11 },
    { id: 'hexagonalBasalt',      label: 'Hexagonal Basalt Columns', desc: 'Columnar joint hexagon prisms with weathered cores', amplitude: 1.1, frequency: 0.16 },
    { id: 'tafoniPits',           label: 'Tafoni Pit Honeycomb', desc: 'Clustered spherical cavern-weathering pits', amplitude: 0.9, frequency: 0.09 },
    { id: 'braidedAlluvium',      label: 'Braided Alluvium Channels', desc: 'Interwoven gravel braid bars & channels', amplitude: 0.7, frequency: 0.06 },
    { id: 'aeolianRipples',       label: 'Aeolian Sand Ripples', desc: 'Wind-ripple trains with slipface asymmetry', amplitude: 0.22, frequency: 0.55 },
    { id: 'talusScree',           label: 'Talus Scree Grain', desc: 'Angular coarse/fine fragment sorting texture', amplitude: 0.5, frequency: 0.42 },
    { id: 'karstGrikes',          label: 'Karst Grikes & Rillen', desc: 'Dissolution fissure network on limestone', amplitude: 0.8, frequency: 0.13 },
    { id: 'crackedPlaya',         label: 'Cracked Playa Mud', desc: 'Polygonal desiccation crack ridges', amplitude: 0.35, frequency: 0.2 },
    { id: 'glacialStriations',    label: 'Glacial Striations', desc: 'Parallel subglacial chatter marks & striation grooves', amplitude: 0.3, frequency: 0.24 }
];

/**
 * Sample one stamp at world (Wx, Wz). Deterministic.
 * Returns { height, colorVar }: height (m offset), colorVar (0..1 albedo variation).
 */
export function SampleStamp(StampId, Wx, Wz, Params = {})
{
    const Meta = STAMP_CATALOG.find(S => S.id === StampId) || STAMP_CATALOG[0];
    const Freq = (Params.frequency ?? Meta.frequency) * 0.01;   // world-m scale
    const Amp = (Params.amplitude ?? Meta.amplitude);
    const Seed = (Params.seed | 0) || 4029;
    const Sx = Wx * Freq, Sz = Wz * Freq;
    let H = 0, C = 0.5;

    switch (Meta.id)
    {
        case 'crossBeddedSandstone':
        {
            const Dip = ((Params.dipDeg ?? 12) * Math.PI) / 180;
            const Phase = (Sx * Math.sin(Dip) + Sz * Math.cos(Dip)) + Math.sin(Sx * 0.23 + Seed) * 0.6;
            const Foresets = Math.abs(((Phase % 1) + 1) % 1 - 0.5) * 2;
            const Grain = Hash01(Seed, Math.round(Sx * 7), Math.round(Sz * 7));
            H = Amp * (Foresets * 0.7 + Grain * 0.3 - 0.45);
            C = Clamp(Foresets * 0.65 + Grain * 0.35, 0, 1);
            break;
        }
        case 'hexagonalBasalt':
        {
            // Approximate hex tiling via two rotated sine lattices + cell jitter.
            const A = Math.sin(Sx * 2.2) * Math.cos(Sz * 1.27);
            const B = Math.sin(Sx * 1.1 + Sz * 1.9);
            const Cell = Math.abs(A + B) * 0.5;
            const Core = Hash01(Seed, Math.round(Sx * 3), Math.round(Sz * 3));
            H = Amp * (Math.pow(Cell, 1.6) * 1.1 - 0.42 + Core * 0.12);
            C = Clamp(Cell * 0.8 + Core * 0.2, 0, 1);
            break;
        }
        case 'tafoniPits':
        {
            // Clustered spherical pits (3-octave thresholded cellular).
            const N1 = Math.sin(Sx * 3.1 + Seed) * Math.cos(Sz * 2.7 - Seed * 0.5);
            const N2 = Math.sin(Sx * 7.3 - Sz * 5.1) * 0.5;
            const Field = N1 * 0.7 + N2 * 0.3;
            const Pit = Smoothstep(0.18, 0.62, Field);
            H = -Amp * Pit * (0.6 + 0.4 * Hash01(Seed, Math.round(Sx * 5), Math.round(Sz * 5)));
            C = 1 - Pit * 0.8;
            break;
        }
        case 'braidedAlluvium':
        {
            const Braid = Math.sin(Sx * 1.7 + Math.sin(Sz * 0.9) * 1.8) * 0.5 + 0.5;
            const Bar = Smoothstep(0.42, 0.68, Braid);
            const Gravel = Hash01(Seed, Math.round(Sx * 11), Math.round(Sz * 11));
            H = Amp * (Bar * 0.8 + Gravel * 0.2 - 0.45);
            C = Clamp(Bar * 0.6 + Gravel * 0.4, 0, 1);
            break;
        }
        case 'aeolianRipples':
        {
            const Dir = (Params.windRad ?? 0.6);
            const Along = Sx * Math.cos(Dir) + Sz * Math.sin(Dir);
            const Ripple = Math.sin(Along * 6.28 * 2.2) * 0.5 + 0.5;
            const Asym = Math.pow(Ripple, 1.7);  // sharp crest, gentle stoss
            H = Amp * (Asym - 0.45);
            C = Asym;
            break;
        }
        case 'talusScree':
        {
            const Grain = Hash01(Seed, Math.round(Sx * 13), Math.round(Sz * 13));
            const Lump = Math.sin(Sx * 5.3) * Math.cos(Sz * 4.7) * 0.5 + 0.5;
            H = Amp * (Grain * 0.65 + Lump * 0.35 - 0.5);
            C = Clamp(Grain, 0, 1);
            break;
        }
        case 'karstGrikes':
        {
            const F1 = Math.abs(Math.sin(Sx * 2.6 + Math.sin(Sz * 1.1)));
            const F2 = Math.abs(Math.sin(Sz * 2.2 + Math.sin(Sx * 1.4)));
            const Fissure = 1 - Math.min(F1, F2);
            H = -Amp * Math.pow(Fissure, 2.2);
            C = Clamp(1 - Fissure, 0, 1);
            break;
        }
        case 'crackedPlaya':
        {
            const P1 = Math.abs(Math.sin(Sx * 3.1 + 0.7) * Math.sin(Sz * 2.9 + 1.2));
            const P2 = Math.abs(Math.sin(Sx * 5.7 - Sz * 4.3));
            const Crack = 1 - Math.min(P1, P2);
            H = Amp * (0.35 - Crack * 0.9);
            C = Clamp(Crack, 0, 1);
            break;
        }
        case 'glacialStriations':
        {
            const Dir = (Params.strikeRad ?? 0.25);
            const Along = Sx * Math.cos(Dir) + Sz * Math.sin(Dir);
            const Groove = Math.abs(Math.sin(Along * 6.28 * 3.1));
            const Chatter = Hash01(Seed, Math.round(Along * 9), 3);
            H = -Amp * Math.pow(1 - Groove, 2.4) * (0.5 + 0.5 * Chatter);
            C = Clamp(Groove * 0.7 + Chatter * 0.3, 0, 1);
            break;
        }
    }
    return { height: H, colorVar: Clamp(C, 0, 1) };
}

/** Blend a stamp's height offset into a runtime heightfield (in place). */
export function ApplyStampToHeightfield(Heights, Nx, Nz, StepX, StepZ, OriginX, OriginZ, StampId, Params = {}, Mask2D = null)
{
    let Applied = 0;
    for (let Z = 0; Z < Nz; ++Z)
    {
        for (let X = 0; X < Nx; ++X)
        {
            const I = Z * Nx + X;
            if (Mask2D && Mask2D[I] > 0) continue;
            const S = SampleStamp(StampId, OriginX + X * StepX, OriginZ + Z * StepZ, Params);
            Heights[I] += S.height * (Params.strength ?? 1.0);
            Applied++;
        }
    }
    return Applied;
}

/*==========================================================================================================================================
                                                SATMAP MULTI-STOP COLOR RAMPS
==========================================================================================================================================*/
export const SATMAP_RAMPS = [
    { id: 'satelliteDesert',  label: 'Satellite Desert',      stops: [[0.0, 0.16, 0.12, 0.09], [0.3, 0.52, 0.36, 0.2], [0.6, 0.78, 0.6, 0.34], [1.0, 0.93, 0.86, 0.7]] },
    { id: 'icelandMoss',      label: 'Icelandic Moss',        stops: [[0.0, 0.07, 0.08, 0.09], [0.35, 0.2, 0.26, 0.2], [0.62, 0.38, 0.5, 0.3], [1.0, 0.86, 0.88, 0.84]] },
    { id: 'alpineSnow',       label: 'Alpine Snow',           stops: [[0.0, 0.1, 0.12, 0.15], [0.35, 0.26, 0.29, 0.31], [0.62, 0.55, 0.53, 0.48], [1.0, 0.95, 0.97, 1.0]] },
    { id: 'canyonStrata',     label: 'Canyon Strata',         stops: [[0.0, 0.24, 0.12, 0.08], [0.3, 0.58, 0.28, 0.16], [0.55, 0.82, 0.55, 0.3], [0.8, 0.9, 0.72, 0.5], [1.0, 0.96, 0.9, 0.8]] },
    { id: 'basaltNight',      label: 'Basalt Night',          stops: [[0.0, 0.04, 0.05, 0.07], [0.4, 0.12, 0.14, 0.18], [0.75, 0.28, 0.31, 0.36], [1.0, 0.58, 0.62, 0.68]] },
    { id: 'tropicalHighland', label: 'Tropical Highland',    stops: [[0.0, 0.05, 0.14, 0.1], [0.35, 0.12, 0.36, 0.18], [0.65, 0.42, 0.55, 0.24], [1.0, 0.88, 0.9, 0.78]] },
    { id: 'toxicOchre',       label: 'Toxic Ochre',           stops: [[0.0, 0.16, 0.1, 0.06], [0.35, 0.5, 0.32, 0.1], [0.7, 0.85, 0.62, 0.18], [1.0, 0.98, 0.9, 0.55]] },
    { id: 'moonRegolith',     label: 'Moon Regolith',         stops: [[0.0, 0.03, 0.03, 0.04], [0.45, 0.16, 0.16, 0.18], [0.8, 0.42, 0.42, 0.45], [1.0, 0.78, 0.78, 0.8]] }
];

/** Evaluate a multi-stop ramp at t ∈ [0,1] → [r, g, b] in 0..1. */
export function EvaluateRamp(Ramp, T)
{
    const Stops = Ramp.stops;
    const X = Clamp(T, 0, 1);
    for (let S = 0; S < Stops.length - 1; ++S)
    {
        const A = Stops[S], B = Stops[S + 1];
        if (X >= A[0] && X <= B[0])
        {
            const K = (X - A[0]) / Math.max(1e-9, B[0] - A[0]);
            return [Mix(A[1], B[1], K), Mix(A[2], B[2], K), Mix(A[3], B[3], K)];
        }
    }
    const L = Stops[Stops.length - 1];
    return [L[1], L[2], L[3]];
}

/**
 * Compose a shaded surface color per cell: altitude ramp × slope shading ×
 * optional data-map tinting + stamp color variation + water/snow/sand overlays.
 */
export function ComposeSurfaceColors(Region, State = null, Options = {})
{
    const Rt = Region.runtime;
    const Nx = Rt.nx, Nz = Rt.nz, N = Nx * Nz;
    const Sdf = Rt.sdf2D;
    const H = State ? State.h : Rt.heights;
    const Ramp = SATMAP_RAMPS.find(R => R.id === (Options.rampId || 'satelliteDesert')) || SATMAP_RAMPS[0];
    const Sea = State ? State.seaLevel : 0;
    const Colors = new Float32Array(N * 3);
    const StampId = Options.stampId || null;
    const StampParams = Options.stampParams || {};
    const Mask2D = Options.maskField || null;

    for (let Z = 0; Z < Nz; ++Z)
    {
        for (let X = 0; X < Nx; ++X)
        {
            const I = Z * Nx + X;
            const Xm = Math.max(0, X - 1), Xp = Math.min(Nx - 1, X + 1);
            const Zm = Math.max(0, Z - 1), Zp = Math.min(Nz - 1, Z + 1);
            const DhDx = (H[Z * Nx + Xp] - H[Z * Nx + Xm]) / ((Xp - Xm || 1) * Rt.stepX);
            const DhDz = (H[Zp * Nx + X] - H[Zm * Nx + X]) / ((Zp - Zm || 1) * Rt.stepZ);
            const SlopeNorm = Clamp(Math.hypot(DhDx, DhDz) / 1.35, 0, 1);
            const AltT = Clamp((H[I] - Sea + 12) / 280, 0, 1);
            let [R, G, B] = EvaluateRamp(Ramp, AltT);
            // Slope darkening (rock bands on steep faces).
            const SlopeDark = 1 - SlopeNorm * 0.42;
            R *= SlopeDark; G *= SlopeDark; B *= SlopeDark;

            if (StampId)
            {
                const S = SampleStamp(StampId, Rt.bounds ? Rt.bounds.x + X * Rt.stepX : X * Rt.stepX, Rt.bounds ? Rt.bounds.z + Z * Rt.stepZ : Z * Rt.stepZ, StampParams);
                const V = 0.72 + 0.56 * S.colorVar;
                R *= V; G *= V; B *= V;
            }

            if (State)
            {
                // Chemical staining tints.
                R = Mix(R, 0.72, State.fe[I] * 0.38);
                G = Mix(G, 0.38, State.fe[I] * 0.30);
                B = Mix(B, 0.22, State.fe[I] * 0.30);
                R = Mix(R, 0.25, State.cu[I] * 0.32); G = Mix(G, 0.72, State.cu[I] * 0.38); B = Mix(B, 0.6, State.cu[I] * 0.34);
                R = Mix(R, 0.28, State.mn[I] * 0.36); G = Mix(G, 0.22, State.mn[I] * 0.32); B = Mix(B, 0.3, State.mn[I] * 0.34);
                // Aeolian sand sheet.
                const SandT = Clamp(State.sand[I] / 0.6, 0, 1) * 0.85;
                R = Mix(R, 0.86, SandT); G = Mix(G, 0.72, SandT); B = Mix(B, 0.45, SandT);
                // Wet darkening (thin film & saturated ground).
                const Wet = Clamp(State.wetness[I], 0, 1);
                R *= 1 - Wet * 0.42; G *= 1 - Wet * 0.38; B *= 1 - Wet * 0.3;
                // Snow / ice overlay.
                const SnowT = Clamp(State.snow[I] / 0.5, 0, 1);
                R = Mix(R, 0.94, SnowT); G = Mix(G, 0.96, SnowT); B = Mix(B, 1.0, SnowT);
                const IceT = Clamp(State.ice[I] / 1.2, 0, 1) * 0.9;
                R = Mix(R, 0.62, IceT); G = Mix(G, 0.82, IceT); B = Mix(B, 0.95, IceT);
                // Salt crust brightens.
                const SaltT = Clamp(State.salt[I], 0, 1) * 0.55;
                R = Mix(R, 0.95, SaltT); G = Mix(G, 0.93, SaltT); B = Mix(B, 0.86, SaltT);
            }

            if (Mask2D && Mask2D[I] > 0)
            {
                R = Mix(R, 0.07, 0.75); G = Mix(G, 0.09, 0.75); B = Mix(B, 0.12, 0.75);
            }
            Colors[I * 3] = Clamp(R, 0, 1);
            Colors[I * 3 + 1] = Clamp(G, 0, 1);
            Colors[I * 3 + 2] = Clamp(B, 0, 1);
        }
    }
    return Colors;
}

/*==========================================================================================================================================
                                    PURE-JS PNG ENCODER (stored-deflate) + EXPORT
==========================================================================================================================================*/
function Crc32(Bytes)
{
    let C = 0xffffffff;
    for (let I = 0; I < Bytes.length; ++I)
    {
        C ^= Bytes[I];
        for (let K = 0; K < 8; ++K) C = (C >>> 1) ^ (0xedb88320 & -(C & 1));
    }
    return (C ^ 0xffffffff) >>> 0;
}

function Adler32(Bytes)
{
    let A = 1, B = 0;
    for (let I = 0; I < Bytes.length; ++I)
    {
        A = (A + Bytes[I]) % 65521;
        B = (B + A) % 65521;
    }
    return ((B << 16) | A) >>> 0;
}

function ZlibStore(Raw)
{
    // zlib header (stored/deflate) + stored blocks + adler32.
    const MaxBlock = 65535;
    const Chunks = [];
    Chunks.push(0x78, 0x01);
    let Offset = 0;
    do
    {
        const Len = Math.min(MaxBlock, Raw.length - Offset);
        const Final = Offset + Len >= Raw.length ? 1 : 0;
        Chunks.push(Final, Len & 0xff, (Len >> 8) & 0xff, ~Len & 0xff, (~Len >> 8) & 0xff);
        for (let I = 0; I < Len; ++I) Chunks.push(Raw[Offset + I]);
        Offset += Len;
    } while (Offset < Raw.length);
    const Ad = Adler32(Raw);
    Chunks.push((Ad >>> 24) & 0xff, (Ad >>> 16) & 0xff, (Ad >>> 8) & 0xff, Ad & 0xff);
    return new Uint8Array(Chunks);
}

function PngChunk(Type, Data)
{
    const Out = new Uint8Array(12 + Data.length);
    const View = new DataView(Out.buffer);
    View.setUint32(0, Data.length);
    for (let I = 0; I < 4; ++I) Out[4 + I] = Type.charCodeAt(I);
    Out.set(Data, 8);
    View.setUint32(8 + Data.length, Crc32(Out.subarray(4, 8 + Data.length)));
    return Out;
}

/** Encode 8-bit grayscale or RGBA pixel data as a PNG byte array. */
export function EncodePng(Width, Height, PixelsRGBA, HasAlpha = false)
{
    const Ch = HasAlpha ? 4 : 3;
    const Raw = new Uint8Array((Width * Ch + 1) * Height);
    let P = 0;
    for (let Y = 0; Y < Height; ++Y)
    {
        Raw[P++] = 0;   // filter: none
        for (let X = 0; X < Width; ++X)
        {
            for (let C = 0; C < Ch; ++C) Raw[P++] = PixelsRGBA[(Y * Width + X) * (HasAlpha ? 4 : 3) + C];
        }
    }
    const Sig = new Uint8Array([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
    const IHDR = new Uint8Array(13);
    const View = new DataView(IHDR.buffer);
    View.setUint32(0, Width);
    View.setUint32(4, Height);
    IHDR[8] = 8;                       // bit depth
    IHDR[9] = HasAlpha ? 6 : 2;        // color type RGBA / RGB
    const IDAT = ZlibStore(Raw);
    const Parts = [Sig, PngChunk('IHDR', IHDR), PngChunk('IDAT', IDAT), PngChunk('IEND', new Uint8Array(0))];
    let Total = 0;
    for (const P2 of Parts) Total += P2.length;
    const Out = new Uint8Array(Total);
    let O = 0;
    for (const P2 of Parts) { Out.set(P2, O); O += P2.length; }
    return Out;
}

/** Export a normalized data map (Float32Array 0..1) as a grayscale PNG. */
export function ExportMapPngBytes(MapField, Nx, Nz, InvertSea = null)
{
    const Px = new Uint8Array(Nx * Nz * 3);
    for (let I = 0; I < Nx * Nz; ++I)
    {
        let V = Clamp(MapField[I], 0, 1);
        if (InvertSea && InvertSea[I] >= 0) V = 0.02;
        const G = Math.round(V * 255);
        Px[I * 3] = G; Px[I * 3 + 1] = G; Px[I * 3 + 2] = G;
    }
    return EncodePng(Nx, Nz, Px, false);
}

/** Export composed surface colors (Float32Array RGB 0..1) as an RGB PNG. */
export function ExportSurfacePngBytes(Colors, Nx, Nz)
{
    const Px = new Uint8Array(Nx * Nz * 3);
    for (let I = 0; I < Nx * Nz * 3; ++I) Px[I] = Math.round(Clamp(Colors[I], 0, 1) * 255);
    return EncodePng(Nx, Nz, Px, false);
}

/** Export a data map as raw Float32 bytes (for external tooling / Gaea interchange). */
export function ExportMapRawBytes(MapField)
{
    return new Uint8Array(MapField.buffer.slice(0));
}
