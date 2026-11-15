/*============================================================================================================================================
                                                    VECTORFIELDGPUVIEWPORT.JS
============================================================================================================================================*/
// 🧩 Single-Kernel 128×128 WebGL2 GPU Vector Field Viewport + Automatic Software Isometric/2D Canvas Fallback:
//    • Only 1 shader (SLICE_BAKE_FRAG on zoom-adaptive 128×128 FBO) runs all 19 ForceModels + compiled Symbolic Formulas
//    • Bidirectional RK4 Streamline Ribbons + Chevrons (1 batched draw call) & Zoomable Field (Wheel / Pinch / Pan / Fit)
//    • Live hot-recompile of SLICE_BAKE_FRAG when editing custom GPU symbolic equations (e.g. a = -S·A(r)·r̂ + ω_swirl·(ŷ×r̂))
//    • Graceful automatic fallback to dark #0b0d10 Software 2D/Iso Renderer if browser disables WebGL

import * as THREE from '../Vendor/three.esm.min.js';
import { OrbitControls } from '../Vendor/orbit-controls.esm.min.js';
import {
    Clamp,
    EvaluateComposite,
    ForceModelById,
    GetActiveSliceAxes,
    IntegrateStreamlineBidirectional,
    PackForcesForGPU,
    ResolveDurationEnvelope,
    SampleDifferential,
    SampleMotionPathPoints,
    MAX_GPU_FORCES
} from './VectorFieldCatalogue.js';
import {
    BuildSliceBakeFrag,
    SLICE_BAKE_VERT,
    SLICE_BAKE_FRAG,
    SLICE_DISPLAY_VERT,
    SLICE_DISPLAY_FRAG,
    GPU_ARROWS_VERT,
    GPU_ARROWS_FRAG,
    STREAMLINE_VERT,
    STREAMLINE_FRAG,
    BOKEH_RIBBON_VERT,
    BOKEH_RIBBON_FRAG,
    BOKEH_BEAD_VERT,
    BOKEH_BEAD_FRAG
} from './VectorFieldShaders.js';

const WORLD_SCALE = 1 / 12;
const INV_SCALE = 12;
const DEFAULT_HALF_M = 460;
const BAKE_RES = 128;
const FBO_QUAD_GEO = new THREE.PlaneGeometry(2, 2);

const AXIS_CODES = { XZ: 0, XY: 1, YZ: 2 };
const AXIS_NAMES = ['XZ', 'XY', 'YZ'];
const QUANTITY_CODES = { Speed: 0, Acceleration: 1, Curl: 2, Divergence: 3, Pressure: 4 };
const COLORMAP_CODES = { Engineering: 0, Turbo: 1, Plasma: 2, Diverging: 3 };

function ColormapRgb01(Name, Share)
{
    const Palettes = {
        Engineering: [[15, 28, 38], [46, 110, 125], [89, 166, 110], [212, 181, 102], [232, 107, 79]],
        Turbo: [[36, 23, 28], [69, 105, 240], [33, 224, 196], [227, 224, 46], [232, 51, 23]],
        Plasma: [[18, 10, 82], [125, 20, 158], [204, 71, 120], [247, 148, 64], [245, 245, 110]],
        Diverging: [[43, 122, 227], [107, 181, 242], [219, 227, 232], [240, 153, 87], [224, 59, 51]]
    };
    const Stops = Palettes[Name] || Palettes.Engineering;
    const T = Clamp(Share, 0, 1) * (Stops.length - 1);
    const I = Math.min(Stops.length - 2, Math.floor(T));
    const F = T - I;
    const A = Stops[I], B = Stops[I + 1];
    return [
        (A[0] + (B[0] - A[0]) * F) / 255,
        (A[1] + (B[1] - A[1]) * F) / 255,
        (A[2] + (B[2] - A[2]) * F) / 255
    ];
}

function ColormapCss(Name, Share, Alpha = 1)
{
    const C = ColormapRgb01(Name, Share);
    return `rgba(${Math.round(C[0] * 255)},${Math.round(C[1] * 255)},${Math.round(C[2] * 255)},${Alpha})`;
}

function StreamlineRgb01(Name, Share)
{
    const Palettes = {
        Engineering: [[38, 152, 255], [32, 224, 198], [94, 232, 124], [250, 194, 62], [255, 84, 72]],
        Turbo: [[62, 134, 255], [33, 228, 206], [134, 240, 82], [248, 212, 48], [244, 64, 42]],
        Plasma: [[98, 66, 240], [178, 52, 216], [234, 78, 136], [252, 156, 68], [250, 245, 118]],
        Diverging: [[56, 138, 244], [98, 202, 248], [218, 234, 246], [246, 158, 86], [236, 66, 58]]
    };
    const Stops = Palettes[Name] || Palettes.Engineering;
    const T = Clamp(Share, 0, 1) * (Stops.length - 1);
    const I = Math.min(Stops.length - 2, Math.floor(T));
    const F = T - I;
    const A = Stops[I], B = Stops[I + 1];
    return [
        (A[0] + (B[0] - A[0]) * F) / 255,
        (A[1] + (B[1] - A[1]) * F) / 255,
        (A[2] + (B[2] - A[2]) * F) / 255
    ];
}

function HexToRgb01(Hex)
{
    const Clean = String(Hex || '#68dcff').replace('#', '');
    const Full = Clean.length === 3
        ? Clean.split('').map((Ch) => Ch + Ch).join('')
        : Clean.padEnd(6, '0').slice(0, 6);
    const Num = parseInt(Full, 16);
    if (!Number.isFinite(Num)) return [0.408, 0.863, 1.0];
    return [((Num >> 16) & 255) / 255, ((Num >> 8) & 255) / 255, (Num & 255) / 255];
}

export function ResolveStreamlinePointColor(Colormap, ColorMode, SingleColor, Share, ElevY = 16)
{
    const Mode = ColorMode || 'FieldPalette';
    if (Mode === 'CyanSingle' || Mode === 'CustomSingle')
    {
        const Base = HexToRgb01(Mode === 'CyanSingle' ? '#68dcff' : (SingleColor || '#68dcff'));
        const SpeedBoost = 0.72 + 0.44 * Math.pow(Clamp(Share, 0, 1), 0.75);
        const ElevBoost = Clamp(0.92 + (ElevY - 16) * 0.0012, 0.86, 1.14);
        const Gain = SpeedBoost * ElevBoost;
        return [
            Clamp(Base[0] * Gain, 0, 1),
            Clamp(Base[1] * Gain, 0, 1),
            Clamp(Base[2] * Gain, 0, 1)
        ];
    }
    if (Mode === 'Elevation')
    {
        const AltT = Clamp((ElevY - 8) / 140, 0, 1);
        const Stops = [[56, 152, 255], [48, 226, 204], [122, 238, 128], [255, 208, 92]];
        const T = AltT * (Stops.length - 1);
        const I = Math.min(Stops.length - 2, Math.floor(T));
        const F = T - I;
        const A = Stops[I], B = Stops[I + 1];
        const SpdLum = 0.80 + 0.34 * Clamp(Share, 0, 1);
        return [
            Clamp(((A[0] + (B[0] - A[0]) * F) / 255) * SpdLum, 0, 1),
            Clamp(((A[1] + (B[1] - A[1]) * F) / 255) * SpdLum, 0, 1),
            Clamp(((A[2] + (B[2] - A[2]) * F) / 255) * SpdLum, 0, 1)
        ];
    }
    const StreamT = Clamp(0.10 + Math.pow(Clamp(Share, 0, 1), 0.72) * 0.90, 0, 1);
    const BaseCol = StreamlineRgb01(Colormap, StreamT);
    const ElevLum = Clamp(0.92 + (ElevY - 16) * 0.0014, 0.86, 1.15);
    return [
        Clamp(BaseCol[0] * ElevLum, 0, 1),
        Clamp(BaseCol[1] * ElevLum, 0, 1),
        Clamp(BaseCol[2] * ElevLum, 0, 1)
    ];
}

function StreamlineCss(Name, Share, Alpha = 1, ColorMode = 'FieldPalette', SingleColor = '#68dcff', ElevY = 16)
{
    const C = ResolveStreamlinePointColor(Name, ColorMode, SingleColor, Share, ElevY);
    return `rgba(${Math.round(C[0] * 255)},${Math.round(C[1] * 255)},${Math.round(C[2] * 255)},${Alpha})`;
}

function CreateVector4Array(Len)
{
    return Array.from({ length: Len }, () => new THREE.Vector4());
}

function BuildArrowTemplateGeometry()
{
    const Shaft = new THREE.CylinderGeometry(0.055, 0.055, 0.72, 6, 1, false);
    Shaft.translate(0, 0.36, 0);
    const Head = new THREE.ConeGeometry(0.16, 0.32, 8, 1, false);
    Head.translate(0, 0.86, 0);

    const GeoA = Shaft.toNonIndexed();
    const GeoB = Head.toNonIndexed();
    const PosA = GeoA.attributes.position.array;
    const PosB = GeoB.attributes.position.array;
    const Merged = new Float32Array(PosA.length + PosB.length);
    Merged.set(PosA, 0);
    Merged.set(PosB, PosA.length);
    Shaft.dispose(); Head.dispose(); GeoA.dispose(); GeoB.dispose();

    const Geo = new THREE.InstancedBufferGeometry();
    Geo.setAttribute('position', new THREE.BufferAttribute(Merged, 3));
    return Geo;
}

export function PlaneToWorld(Axis, U, V, OffsetM)
{
    if (Axis === 'XY') return [U, V, OffsetM];
    if (Axis === 'YZ') return [OffsetM, V, U];
    return [U, OffsetM, V];
}

export function WorldToPlaneUV(Axis, X, Y, Z)
{
    if (Axis === 'XY') return [X, Y, Z];
    if (Axis === 'YZ') return [Z, Y, X];
    return [X, Z, Y];
}

export function CollectMultiAxisStreamlineSeeds(Scene, BoundsByAxis, SliceOffsetM, StepM, Options = {})
{
    const ActiveAxes = Options.ActiveAxes?.length ? Options.ActiveAxes : GetActiveSliceAxes(Scene.Viz);
    const Is3D = Options.Is3D !== false;
    const IsVolumetric3D = Options.Volumetric !== undefined ? Boolean(Options.Volumetric && Is3D) : (Is3D && ActiveAxes.length === 3);
    const HasTarget = Number.isFinite(Options.TargetCount);
    const TargetMax = HasTarget ? Clamp(Math.round(Options.TargetCount), 24, 520) : 320;
    const CountScale = HasTarget ? Clamp(TargetMax / 220, 0.18, 2.5) : 1.0;
    const Seeds = [];
    const EffStep = HasTarget ? Clamp(StepM / Math.sqrt(CountScale), 10, 260) : StepM;
    const Cell = Math.max(10, EffStep * 0.45);
    const Seen = new Set();
    const MarkAndPush = (X, Y, Z, Force = false) => {
        if (HasTarget && Seeds.length >= TargetMax) return false;
        const Key = `${Math.round(X / Cell)},${Math.round(Y / 18)},${Math.round(Z / Cell)}`;
        if (!Force && Seen.has(Key)) return false;
        Seen.add(Key);
        Seeds.push([X, Y, Z]);
        return true;
    };

    const Solo = Scene.Viz?.SoloToken || null;
    const RadialModels = new Set(['Attractor', 'Repulsor', 'RadialBurst', 'Containment']);

    // 1. Slice-bound seeds for EVERY enabled axis (strictly on that axis plane unless all 3 are enabled)
    for (const Axis of ActiveAxes)
    {
        const B = BoundsByAxis[Axis];
        if (!B) continue;
        const { MinU, MaxU, MinV, MaxV } = B;
        const SpanU = Math.max(40, (MaxU - MinU) * 0.5);
        const SpanV = Math.max(40, (MaxV - MinV) * 0.5);

        // Source radial/vortex spoke rings projected cleanly onto this slice plane
        for (const S of Scene.Sources)
        {
            if (!S.Enabled || (Solo && S.Token !== Solo)) continue;
            if (S.Model === 'LinearWind' || S.Model === 'Drag') continue;
            const [Su, Sv, Sperp] = WorldToPlaneUV(Axis, S.Centre[0], S.Centre[1], S.Centre[2]);
            const SrcReach = S.Radius || 180;
            const DeltaPerp = Math.abs(Sperp - SliceOffsetM);
            let SliceR = 0;
            if (S.Reach === 'Column' && Axis === 'XZ') SliceR = SrcReach;
            else if (DeltaPerp < SrcReach * 1.05) SliceR = Math.sqrt(Math.max(0, SrcReach * SrcReach - DeltaPerp * DeltaPerp));
            if (SliceR < 16) continue;
            if (Su + SliceR < MinU || Su - SliceR > MaxU || Sv + SliceR < MinV || Sv - SliceR > MaxV) continue;

            const IsRadial = RadialModels.has(S.Model) || S.UseFormula;
            const RingR = IsRadial
                ? Clamp(SliceR * 0.46, 20, Math.min(SpanU, SpanV) * 0.48)
                : Clamp(S.CoreRadius || SliceR * 0.34, 18, Math.min(SpanU, SpanV) * 0.45);
            const BaseEq = IsRadial ? 12 : 8;
            const EqCount = HasTarget ? Clamp(Math.round(BaseEq * CountScale), 3, 18) : BaseEq;
            for (let K = 0; K < EqCount; ++K)
            {
                const Ang = (K / EqCount) * Math.PI * 2 + (IsRadial ? 0.12 : 0.35);
                const [Wx, Wy, Wz] = PlaneToWorld(Axis, Su + Math.cos(Ang) * RingR, Sv + Math.sin(Ang) * RingR, SliceOffsetM);
                MarkAndPush(Wx, Wy, Wz, true);
            }
        }

        // Evenly staggered hex grid strictly ON this slice plane
        const BasePlaneBudget = IsVolumetric3D ? 42 : (ActiveAxes.length === 1 ? 96 : ActiveAxes.length === 2 ? 48 : 30);
        const PlaneBudget = HasTarget ? Math.max(8, Math.round(BasePlaneBudget * CountScale)) : BasePlaneBudget;
        const PlaneCap = HasTarget ? Math.min(TargetMax, Math.round(TargetMax * (IsVolumetric3D ? 0.55 : 1.0))) : 230;
        const AreaStep = Math.max(EffStep, Math.sqrt(Math.max(1, (MaxU - MinU) * (MaxV - MinV)) / PlaneBudget));
        let Row = 0;
        for (let V = MinV; V <= MaxV && Seeds.length < PlaneCap; V += AreaStep)
        {
            const OffsetU = (Row++ % 2) * AreaStep * 0.5;
            for (let U = MinU + OffsetU; U <= MaxU && Seeds.length < PlaneCap; U += AreaStep)
            {
                const [Wx, Wy, Wz] = PlaneToWorld(Axis, U, V, SliceOffsetM);
                MarkAndPush(Wx, Wy, Wz, false);
            }
        }
    }

    // 2. Full Volumetric 3D spherical hemisphere & multi-altitude shells when Volumetric 3D is active
    if (IsVolumetric3D && BoundsByAxis.XZ)
    {
        const { MinU: MinX, MaxU: MaxX, MinV: MinZ, MaxV: MaxZ } = BoundsByAxis.XZ;
        const SpanX = Math.max(40, (MaxX - MinX) * 0.5);
        const SpanZ = Math.max(40, (MaxZ - MinZ) * 0.5);
        const RingPts = HasTarget ? Clamp(Math.round(6 * CountScale), 2, 10) : 6;

        for (const S of Scene.Sources)
        {
            if (!S.Enabled || (Solo && S.Token !== Solo)) continue;
            if (S.Model === 'LinearWind' || S.Model === 'Drag') continue;
            const Cx = S.Centre[0], Cy = S.Centre[1], Cz = S.Centre[2];
            const SrcReach = S.Radius || 180;
            if (Cx + SrcReach < MinX || Cx - SrcReach > MaxX || Cz + SrcReach < MinZ || Cz - SrcReach > MaxZ) continue;
            const IsRadial = RadialModels.has(S.Model) || S.UseFormula;
            const RingR = Clamp(SrcReach * 0.45, 22, Math.min(SpanX, SpanZ) * 0.48);

            if (IsRadial)
            {
                const UpR = RingR * Math.cos(0.73);
                const UpY = Cy + RingR * Math.sin(0.73);
                for (let K = 0; K < RingPts; ++K)
                {
                    const Ang = (K / RingPts) * Math.PI * 2 + 0.52;
                    MarkAndPush(Cx + Math.cos(Ang) * UpR, UpY, Cz + Math.sin(Ang) * UpR, true);
                }
                if (Cy > 18 || S.Reach === 'Sphere')
                {
                    const DnR = RingR * Math.cos(0.49);
                    const DnY = Math.max(4, Cy - RingR * Math.sin(0.49));
                    for (let K = 0; K < RingPts; ++K)
                    {
                        const Ang = (K / RingPts) * Math.PI * 2;
                        MarkAndPush(Cx + Math.cos(Ang) * DnR, DnY, Cz + Math.sin(Ang) * DnR, true);
                    }
                }
            }
            else
            {
                const HighY = Cy + Clamp((S.Height || 160) * 0.28, 34, 88);
                for (let K = 0; K < RingPts; ++K)
                {
                    const Ang = (K / RingPts) * Math.PI * 2 + 0.2;
                    MarkAndPush(Cx + Math.cos(Ang) * RingR * 0.85, HighY, Cz + Math.sin(Ang) * RingR * 0.85, true);
                }
            }
        }

        const AltLayers = [Math.max(4, SliceOffsetM + 36), Math.max(4, SliceOffsetM + 80), Math.max(4, SliceOffsetM + 126)];
        const LayerMaxTotal = HasTarget ? TargetMax : 320;
        const PerLayerBudget = Math.max(4, Math.floor((LayerMaxTotal - Seeds.length) / AltLayers.length));
        const AreaStep = Math.max(EffStep, Math.sqrt(Math.max(1, (MaxX - MinX) * (MaxZ - MinZ)) / Math.max(6, Math.min(48, PerLayerBudget))));
        for (let Li = 0; Li < AltLayers.length; ++Li)
        {
            const LayerY = AltLayers[Li];
            let Row = 0;
            let Added = 0;
            for (let Z = MinZ + Li * AreaStep * 0.35; Z <= MaxZ && Added < PerLayerBudget && Seeds.length < LayerMaxTotal; Z += AreaStep)
            {
                const OffsetX = ((Row++ % 2) * 0.5 + (Li + 1) * 0.33) * AreaStep;
                for (let X = MinX + OffsetX; X <= MaxX && Added < PerLayerBudget && Seeds.length < LayerMaxTotal; X += AreaStep)
                {
                    if (MarkAndPush(X, LayerY, Z, false)) Added++;
                }
            }
        }
    }
    return Seeds;
}

export function CollectAdaptiveStreamlineSeedsRect(Scene, MinX, MaxX, MinZ, MaxZ, SliceHeight, StepM, Options = {})
{
    const Axis = Options.Axis || 'XZ';
    const ActiveAxes = Options.ActiveAxes || [Axis];
    const BoundsByAxis = {
        XZ: { MinU: MinX, MaxU: MaxX, MinV: MinZ, MaxV: MaxZ },
        XY: { MinU: MinX, MaxU: MaxX, MinV: Math.max(-40, 48 - (MaxZ - MinZ) * 0.45), MaxV: 48 + (MaxZ - MinZ) * 0.45 },
        YZ: { MinU: MinZ, MaxU: MaxZ, MinV: Math.max(-40, 48 - (MaxX - MinX) * 0.45), MaxV: 48 + (MaxX - MinX) * 0.45 }
    };
    return CollectMultiAxisStreamlineSeeds(Scene, BoundsByAxis, SliceHeight, StepM, {
        ActiveAxes,
        Is3D: Options.Is3D !== false
    });
}

export function BuildStreamlineRibbonGeometry(Curves, Colormap, FixedMax, Options = {})
{
    const ColorMode = Options.ColorMode || 'FieldPalette';
    const SingleColor = Options.SingleColor || '#68dcff';
    const Verts = [];
    const Tangents = [];
    const Colors = [];
    const Prog = [];
    const Edges = [];
    const StrandSeeds = [];
    const MagShares = [];

    const PushVert = (Px, Py, Pz, Tan, Col, Pr, Ed, Seed, MagShare) => {
        Verts.push(Px, Py, Pz);
        Tangents.push(Tan[0], Tan[1], Tan[2]);
        Colors.push(Col[0], Col[1], Col[2]);
        Prog.push(Pr);
        Edges.push(Ed);
        StrandSeeds.push(Seed);
        MagShares.push(MagShare);
    };

    const HashSeed = (X, Y, Z) => {
        const H = Math.sin(X * 12.9898 + Y * 78.233 + Z * 37.719) * 43758.5453;
        return H - Math.floor(H);
    };

    for (const Pts of Curves)
    {
        const N = Pts.length;
        if (N < 3) continue;

        const Seed = HashSeed(Pts[0].X, Pts[0].Y, Pts[0].Z);
        const Arc = new Array(N);
        Arc[0] = 0;
        for (let I = 1; I < N; ++I)
        {
            const Dx = Pts[I].X - Pts[I - 1].X;
            const Dy = Pts[I].Y - Pts[I - 1].Y;
            const Dz = Pts[I].Z - Pts[I - 1].Z;
            Arc[I] = Arc[I - 1] + Math.hypot(Dx, Dy, Dz);
        }
        const TotalArc = Math.max(Arc[N - 1], 1e-4);

        const PosArr = [];
        const TanArr = [];
        const ColArr = [];
        const PrArr = [];
        const MagArr = [];

        for (let I = 0; I < N; ++I)
        {
            const P = Pts[I];
            const Prev = Pts[Math.max(0, I - 1)];
            const Next = Pts[Math.min(N - 1, I + 1)];
            let Tx = Next.X - Prev.X, Ty = Next.Y - Prev.Y, Tz = Next.Z - Prev.Z;
            const TLen = Math.hypot(Tx, Ty, Tz) || 1;
            Tx /= TLen; Ty /= TLen; Tz /= TLen;

            const Share = Clamp(P.Mag / FixedMax, 0, 1);
            const Col = ResolveStreamlinePointColor(Colormap, ColorMode, SingleColor, Share, P.Y);

            PosArr.push([P.X * WORLD_SCALE, P.Y * WORLD_SCALE, P.Z * WORLD_SCALE]);
            TanArr.push([Tx, Ty, Tz]);
            ColArr.push(Col);
            PrArr.push(Arc[I] / TotalArc);
            MagArr.push(Share);
        }

        // Single camera-facing centerline ribbon strip: GPU expands lateral width in screen pixels
        for (let I = 0; I < N - 1; ++I)
        {
            const P0 = PosArr[I], P1 = PosArr[I + 1];
            const T0 = TanArr[I], T1 = TanArr[I + 1];
            const C0 = ColArr[I], C1 = ColArr[I + 1];
            const Pr0 = PrArr[I], Pr1 = PrArr[I + 1];
            const M0 = MagArr[I], M1 = MagArr[I + 1];

            PushVert(P0[0], P0[1], P0[2], T0, C0, Pr0, -1, Seed, M0);
            PushVert(P0[0], P0[1], P0[2], T0, C0, Pr0, 1, Seed, M0);
            PushVert(P1[0], P1[1], P1[2], T1, C1, Pr1, -1, Seed, M1);

            PushVert(P0[0], P0[1], P0[2], T0, C0, Pr0, 1, Seed, M0);
            PushVert(P1[0], P1[1], P1[2], T1, C1, Pr1, 1, Seed, M1);
            PushVert(P1[0], P1[1], P1[2], T1, C1, Pr1, -1, Seed, M1);
        }
    }

    const VertCount = Verts.length / 3;
    const Geo = new THREE.BufferGeometry();
    Geo.setAttribute('position', new THREE.Float32BufferAttribute(Verts.length ? Verts : [0, 0, 0, 0, 0, 0, 0, 0, 0], 3));
    Geo.setAttribute('aTangent', new THREE.Float32BufferAttribute(Tangents.length ? Tangents : [1, 0, 0, 1, 0, 0, 1, 0, 0], 3));
    Geo.setAttribute('aColor', new THREE.Float32BufferAttribute(Colors.length ? Colors : [0, 0, 0, 0, 0, 0, 0, 0, 0], 3));
    Geo.setAttribute('aProgress', new THREE.Float32BufferAttribute(Prog.length ? Prog : [0, 0.5, 1], 1));
    Geo.setAttribute('aEdge', new THREE.Float32BufferAttribute(Edges.length ? Edges : [0, 0, 0], 1));
    Geo.setAttribute('aStrandSeed', new THREE.Float32BufferAttribute(StrandSeeds.length ? StrandSeeds : [0, 0, 0], 1));
    Geo.setAttribute('aMagShare', new THREE.Float32BufferAttribute(MagShares.length ? MagShares : [0, 0, 0], 1));
    Geo.setDrawRange(0, VertCount);
    return Geo;
}

export function ComputeFallbackPlaneFrame(Px, Py, Pw, Ph, IsIso, Axis, FallbackZoom, FallbackCenter, FallbackYaw, FallbackPitch, OffsetM = 16, PivotYOverride = null)
{
    const Cx = Px + Pw * 0.5;
    const Cy = Py + Ph * 0.5;
    const Zoom = Math.max(0.25, FallbackZoom || 1.0);
    const Span = DEFAULT_HALF_M / Zoom;
    const ZoomRatio = 1.0 / Zoom;
    const Scale = Math.min(Pw, Ph) / (Span * (IsIso ? 2.15 : 2.0));
    const PanX = FallbackCenter[0] || 0;
    const PanZ = FallbackCenter[1] || 0;
    const PanY = Number.isFinite(FallbackCenter[2]) ? FallbackCenter[2] : 48;
    const PivotY = Number.isFinite(PivotYOverride) ? PivotYOverride : (Axis === 'XZ' ? OffsetM : PanY);
    const CosY = Math.cos(FallbackYaw), SinY = Math.sin(FallbackYaw);
    const CosP = Math.cos(FallbackPitch), SinP = Math.sin(FallbackPitch);

    const MapPt = (X, Y, Z) => {
        if (!IsIso)
        {
            if (Axis === 'XY') return [Cx + (X - PanX) * Scale, Cy - (Y - PanY) * Scale];
            if (Axis === 'YZ') return [Cx + (Z - PanZ) * Scale, Cy - (Y - PanY) * Scale];
            return [Cx + (X - PanX) * Scale, Cy + (Z - PanZ) * Scale];
        }
        const Dx = X - PanX, Dy = Y - PivotY, Dz = Z - PanZ;
        const Rx = Dx * CosY - Dz * SinY;
        const Rz = Dx * SinY + Dz * CosY;
        const Ry = Dy * CosP - Rz * SinP;
        return [Cx + Rx * Scale, Cy - Ry * Scale];
    };

    const MapPlaneUV = (U, V) => {
        const [Wx, Wy, Wz] = PlaneToWorld(Axis, U, V, OffsetM);
        return MapPt(Wx, Wy, Wz);
    };

    const [CenU, CenV] = WorldToPlaneUV(Axis, PanX, PivotY, PanZ);
    const O = MapPlaneUV(0, 0);
    const P1U = MapPlaneUV(1, 0);
    const P1V = MapPlaneUV(0, 1);
    const Ax = P1U[0] - O[0], Ay = P1U[1] - O[1];
    const Bx = P1V[0] - O[0], By = P1V[1] - O[1];
    const Det = Ax * By - Bx * Ay;
    const NormDet = Det / Math.max(1e-8, Scale * Scale);

    const ScreenToPlaneUV = (Sx, Sy) => {
        if (Math.abs(Det) < 1e-8) return [CenU, CenV];
        const Dx = Sx - O[0], Dy = Sy - O[1];
        return [(Dx * By - Bx * Dy) / Det, (Ax * Dy - Dx * Ay) / Det];
    };

    const ScreenToWorldOnAxis = (Sx, Sy) => {
        const [U, V] = ScreenToPlaneUV(Sx, Sy);
        return PlaneToWorld(Axis, U, V, OffsetM);
    };

    let MinU, MaxU, MinV, MaxV;
    if (Math.abs(NormDet) >= 0.08)
    {
        const Corners = [
            ScreenToPlaneUV(Px, Py),
            ScreenToPlaneUV(Px + Pw, Py),
            ScreenToPlaneUV(Px, Py + Ph),
            ScreenToPlaneUV(Px + Pw, Py + Ph)
        ];
        const Pad = Math.max(24, 48 * ZoomRatio);
        MinU = Clamp(Math.min(...Corners.map((C) => C[0])) - Pad, CenU - 6000, CenU + 6000);
        MaxU = Clamp(Math.max(...Corners.map((C) => C[0])) + Pad, CenU - 6000, CenU + 6000);
        MinV = Clamp(Math.min(...Corners.map((C) => C[1])) - Pad, CenV - 6000, CenV + 6000);
        MaxV = Clamp(Math.max(...Corners.map((C) => C[1])) + Pad, CenV - 6000, CenV + 6000);
    }
    else
    {
        // Vertical slice viewed nearly edge-on: clamp smoothly instead of projecting corners to infinity
        MinU = CenU - Span * 1.85;
        MaxU = CenU + Span * 1.85;
        MinV = CenV - Span * 1.35;
        MaxV = CenV + Span * 1.35;
    }

    return {
        Axis, Cx, Cy, PanX, PanY, PanZ, PivotY, Span, Scale, ZoomRatio,
        MinU, MaxU, MinV, MaxV,
        MinX: MinU, MaxX: MaxU, MinZ: MinV, MaxZ: MaxV,
        MapPt, MapPlaneUV, ScreenToPlaneUV, ScreenToWorldOnAxis, ScreenToWorldXZ: ScreenToPlaneUV
    };
}

export function ProjectRayToAxisParam(RayOrigin, RayDir, AxisOrigin, AxisDir)
{
    const W0x = RayOrigin[0] - AxisOrigin[0];
    const W0y = RayOrigin[1] - AxisOrigin[1];
    const W0z = RayOrigin[2] - AxisOrigin[2];
    const B = RayDir[0] * AxisDir[0] + RayDir[1] * AxisDir[1] + RayDir[2] * AxisDir[2];
    const C = RayDir[0] * W0x + RayDir[1] * W0y + RayDir[2] * W0z;
    const F = AxisDir[0] * W0x + AxisDir[1] * W0y + AxisDir[2] * W0z;
    const Denom = 1 - B * B;
    if (Denom < 1e-4) return 0;
    return (F - B * C) / Denom;
}

export function ComputeFallbackSliceFrame(Px, Py, Pw, Ph, IsIso, FallbackZoom, FallbackCenter, FallbackYaw, FallbackPitch, SliceY = 16)
{
    return ComputeFallbackPlaneFrame(Px, Py, Pw, Ph, IsIso, 'XZ', FallbackZoom, FallbackCenter, FallbackYaw, FallbackPitch, SliceY);
}

//------------------------------------------------------------------------------------------------------------------------
//                           BOKEH STREAM RIBBONS DEBUG VISUALIZER (LIVE FIELD ADVECTION + DOF BEADS)
//------------------------------------------------------------------------------------------------------------------------

export const BOKEH_TRAIL_SEGS = 20;
export const BOKEH_MAX_TRACERS = 1200;

const BOKEH_DEFAULT_COL_A = [0x3b / 255, 0x5a / 255, 0x5b / 255]; // Deep mist teal tail (#3b5a5b)
const BOKEH_DEFAULT_COL_C = [0x8e / 255, 0xc7 / 255, 0xc7 / 255]; // Mist teal body (#8ec7c7)
const BOKEH_DEFAULT_COL_B = [0xef / 255, 0xff / 255, 0xff / 255]; // Luminous ice-white head (#efffff)

export function ResolveBokehColors(Viz, Share, YMetres)
{
    const PaletteMode = Viz?.BokehPalette || 'BokehTeal';
    if (PaletteMode === 'FollowStreams')
    {
        const Base = ResolveStreamlinePointColor(
            Viz?.Colormap || 'Engineering',
            Viz?.StreamColorMode || 'FieldPalette',
            Viz?.StreamSingleColor || '#68dcff',
            Share,
            YMetres
        );
        const HeadRgb = [
            Clamp(Base[0] * 0.45 + 0.55, 0, 1),
            Clamp(Base[1] * 0.45 + 0.55, 0, 1),
            Clamp(Base[2] * 0.45 + 0.55, 0, 1)
        ];
        const TailRgb = [Base[0] * 0.44, Base[1] * 0.44, Base[2] * 0.44];
        return { HeadRgb, MidRgb: Base, TailRgb };
    }
    const Lift = 0.08 * Clamp(Share, 0, 1);
    return {
        HeadRgb: [
            Clamp(BOKEH_DEFAULT_COL_B[0] + Lift, 0, 1),
            Clamp(BOKEH_DEFAULT_COL_B[1] + Lift, 0, 1),
            Clamp(BOKEH_DEFAULT_COL_B[2] + Lift, 0, 1)
        ],
        MidRgb: [
            Clamp(BOKEH_DEFAULT_COL_C[0] + Lift * 0.7, 0, 1),
            Clamp(BOKEH_DEFAULT_COL_C[1] + Lift * 0.8, 0, 1),
            Clamp(BOKEH_DEFAULT_COL_C[2] + Lift * 0.8, 0, 1)
        ],
        TailRgb: BOKEH_DEFAULT_COL_A
    };
}

function CreateMulberry32(Seed = 133742)
{
    let S = Seed >>> 0;
    return () => {
        S = (S + 0x6d2b79f5) | 0;
        let T = Math.imul(S ^ (S >>> 15), 1 | S);
        T = (T + Math.imul(T ^ (T >>> 7), 61 | T)) ^ T;
        return ((T ^ (T >>> 14)) >>> 0) / 4294967296;
    };
}

function SmoothStep01(E0, E1, X)
{
    const T = Clamp((X - E0) / Math.max(1e-6, E1 - E0), 0, 1);
    return T * T * (3 - 2 * T);
}

export class BokehStreamRibbonSim
{
    constructor(Seed = 20261111)
    {
        this.Rand = CreateMulberry32(Seed);
        this.Tracers = Array.from({ length: BOKEH_MAX_TRACERS }, (_, Idx) => ({
            Idx,
            X: 0,
            Y: 16,
            Z: 0,
            Phase: (Idx * 0.371) % 1,
            Age: 0,
            Life: 14.0,
            Dof: 0.3,
            SizeSeed: 1.0,
            SpeedShare: 0.25,
            CurveIdx: 0,
            ValidLen: 0,
            Trail: new Float32Array(BOKEH_TRAIL_SEGS * 4)
        }));
        this.Curves = [];
        this.ActiveCount = 0;
        this.SceneKey = '';
        this.LastCurveRefreshMs = 0;
        this.LastSimTime = -999;
        this.LastBounds = null;
    }

    SampleCurveAtFrac(Curve, Frac)
    {
        const Pts = Curve.Pts;
        const Arc = Curve.Arc;
        const N = Pts.length;
        const F = Clamp(Frac, 0, 1);
        if (F <= 0) return [Pts[0].X, Pts[0].Y, Pts[0].Z, Pts[0].Mag];
        if (F >= 1)
        {
            const P = Pts[N - 1];
            return [P.X, P.Y, P.Z, P.Mag];
        }
        // Binary search cumulative normalized arc length Arc[0..N-1]
        let Lo = 0, Hi = N - 1;
        while (Lo + 1 < Hi)
        {
            const Mid = (Lo + Hi) >> 1;
            if (Arc[Mid] <= F) Lo = Mid;
            else Hi = Mid;
        }
        const A0 = Arc[Lo], A1 = Arc[Hi];
        const T = A1 > A0 + 1e-6 ? (F - A0) / (A1 - A0) : 0;
        const P0 = Pts[Lo], P1 = Pts[Hi];
        return [
            P0.X + (P1.X - P0.X) * T,
            P0.Y + (P1.Y - P0.Y) * T,
            P0.Z + (P1.Z - P0.Z) * T,
            P0.Mag + (P1.Mag - P0.Mag) * T
        ];
    }

    BuildStructuredCurves(Scene, Bounds, SimTime, TargetTracers)
    {
        const Solo = Scene?.Viz?.SoloToken || null;
        const FixedMax = Math.max(6, Scene?.Viz?.FixedMax || 34);
        const Is3D = Bounds.Is3D !== false;
        const ActiveAxes = Bounds.ActiveAxes || ['XZ'];
        const HalfM = Math.max(45, Bounds.HalfM || DEFAULT_HALF_M);
        const Center = Bounds.Center || [0, 0, 48];
        const Fx = Center[0] || 0;
        const Fz = Center[1] || 0;
        const Fy = Center[2] ?? 48;

        const TargetCurveCount = Clamp(Math.round(TargetTracers * 0.52), 48, 240);
        const ZoomRatio = HalfM / DEFAULT_HALF_M;
        const SeedStep = Clamp((Scene?.Viz?.StreamlineSpacing || 84) * ZoomRatio, 18, 150);
        const StepM = Clamp(13.0 * ZoomRatio, 2.2, 26);
        const Span = HalfM * 0.90;
        const BoundsByAxis = {
            XZ: { MinU: Fx - Span, MaxU: Fx + Span, MinV: Fz - Span, MaxV: Fz + Span },
            XY: { MinU: Fx - Span, MaxU: Fx + Span, MinV: Fy - Span, MaxV: Fy + Span },
            YZ: { MinU: Fz - Span, MaxU: Fz + Span, MinV: Fy - Span, MaxV: Fy + Span }
        };
        const Seeds = CollectMultiAxisStreamlineSeeds(Scene, BoundsByAxis, Bounds.SliceHeight ?? 16, SeedStep, {
            ActiveAxes,
            Is3D,
            Volumetric: Is3D,
            TargetCount: TargetCurveCount
        });

        const Built = [];
        for (const [Sx, Sy, Sz] of Seeds)
        {
            const Pts = IntegrateStreamlineBidirectional(Scene, Sx, Sy, Sz, SimTime, 18, StepM, { Solo });
            if (Pts.length < 6) continue;
            const N = Pts.length;
            const Arc = new Float32Array(N);
            let SumArc = 0;
            let SumMag = Pts[0].Mag;
            for (let K = 1; K < N; ++K)
            {
                const Dx = Pts[K].X - Pts[K - 1].X;
                const Dy = Pts[K].Y - Pts[K - 1].Y;
                const Dz = Pts[K].Z - Pts[K - 1].Z;
                SumArc += Math.hypot(Dx, Dy, Dz);
                Arc[K] = SumArc;
                SumMag += Pts[K].Mag;
            }
            if (SumArc < 8.0) continue;
            for (let K = 1; K < N; ++K) Arc[K] /= SumArc;
            const MidMagShare = Clamp((SumMag / N) / FixedMax, 0, 1);
            Built.push({ Pts, Arc, TotalArc: SumArc, MidMagShare });
        }
        return Built;
    }

    UpdateTracerFromCurve(Tracer, Scene)
    {
        if (!this.Curves.length)
        {
            Tracer.ValidLen = 0;
            return;
        }
        const Curve = this.Curves[Tracer.CurveIdx % this.Curves.length];
        const FixedMax = Math.max(6, Scene?.Viz?.FixedMax || 34);
        const HeadFrac = Clamp(Tracer.Phase, 0.01, 0.995);
        // Long graceful curved ribbon (42% of full streamline curve) gliding behind bokeh bead
        const TailFrac = Math.max(0.0, HeadFrac - 0.42);
        const SpanFrac = Math.max(0.02, HeadFrac - TailFrac);
        const Tr = Tracer.Trail;

        for (let K = 0; K < BOKEH_TRAIL_SEGS; ++K)
        {
            const U = K / (BOKEH_TRAIL_SEGS - 1);
            const F = HeadFrac - U * SpanFrac;
            const [Px, Py, Pz, Mag] = this.SampleCurveAtFrac(Curve, F);
            const K4 = K * 4;
            Tr[K4] = Px;
            Tr[K4 + 1] = Py;
            Tr[K4 + 2] = Pz;
            Tr[K4 + 3] = Clamp(Mag / FixedMax, 0, 1);
        }

        Tracer.X = Tr[0];
        Tracer.Y = Tr[1];
        Tracer.Z = Tr[2];
        Tracer.SpeedShare = Math.max(Tr[3], Curve.MidMagShare * 0.72);
        Tracer.ValidLen = BOKEH_TRAIL_SEGS;
    }

    ComputeTopologySignature(Scene, Bounds)
    {
        const SrcSig = (Scene?.Sources || [])
            .map((S) => `${S.Token}:${S.Enabled}:${S.Model}:${S.Strength}:${S.Swirl}:${S.Radius}:${S.UseFormula}:${S.FormulaCode}`)
            .join('|');
        const AxSig = (Bounds.ActiveAxes || ['XZ']).join(',');
        return `${SrcSig}::${Bounds.Is3D !== false}::${AxSig}::${Bounds.SliceAxis || 'XZ'}::${Math.round(Bounds.SliceHeight ?? 16)}::${Scene?.Viz?.SoloToken || ''}`;
    }

    ComputeSceneSignature(Scene, Bounds)
    {
        const SrcSig = (Scene?.Sources || [])
            .map((S) => `${S.Token}:${S.Enabled}:${S.Model}:${S.Strength}:${S.Swirl}:${S.Centre.map((V) => Number(V).toFixed(1)).join(',')}:${S.Radius}:${S.UseFormula}:${S.FormulaCode}`)
            .join('|');
        const AxSig = (Bounds.ActiveAxes || ['XZ']).join(',');
        return `${SrcSig}::${Bounds.Is3D !== false}::${AxSig}::${Bounds.SliceAxis || 'XZ'}::${Math.round(Bounds.SliceHeight ?? 16)}::${Scene?.Viz?.SoloToken || ''}`;
    }

    EnsureSynced(Scene, Bounds, SimTime, ForceReseed = false)
    {
        const TargetCount = Clamp(Math.round(Scene?.Viz?.BokehCount ?? 320), 60, BOKEH_MAX_TRACERS);
        const TopoSig = this.ComputeTopologySignature(Scene, Bounds);
        const Sig = this.ComputeSceneSignature(Scene, Bounds);
        this.LastBounds = Bounds;

        if (!ForceReseed && this.SceneKey === Sig && this.ActiveCount === TargetCount && this.Curves.length > 0) return false;

        // When only animated source centres move (e.g. Vortex along spline), throttle curve refreshes smoothly for 60 FPS playback
        const NowMs = typeof performance !== 'undefined' ? performance.now() : 0;
        const OnlyPositionMoved = !ForceReseed && this.TopologyKey === TopoSig && this.ActiveCount === TargetCount && this.Curves.length > 0;
        if (OnlyPositionMoved && NowMs && NowMs - this.LastCurveRefreshMs < 240) return false;

        const RebuiltCurves = this.BuildStructuredCurves(Scene, Bounds, SimTime, TargetCount);
        const KeepExistingOrder = !ForceReseed && this.ActiveCount === TargetCount && this.Curves.length > 0;
        this.Curves = RebuiltCurves;
        this.TopologyKey = TopoSig;
        this.SceneKey = Sig;
        this.ActiveCount = TargetCount;
        this.LastCurveRefreshMs = NowMs;

        if (this.Curves.length === 0)
        {
            for (let I = 0; I < TargetCount; ++I) this.Tracers[I].ValidLen = 0;
            return true;
        }

        const NumCurves = this.Curves.length;
        for (let I = 0; I < TargetCount; ++I)
        {
            const Tracer = this.Tracers[I];
            Tracer.CurveIdx = I % NumCurves;
            if (!KeepExistingOrder)
            {
                const WaveSlot = Math.floor(I / NumCurves);
                const SlotsPerCurve = Math.max(1, Math.ceil(TargetCount / NumCurves));
                const Stagger = (WaveSlot + this.Rand() * 0.35) / SlotsPerCurve;
                Tracer.Phase = (Stagger + 0.12) % 1.0;
                Tracer.Life = 1.0;
                Tracer.Age = Tracer.Phase;
                Tracer.Dof = this.Rand();
                Tracer.SizeSeed = 0.78 + this.Rand() * 0.44;
            }
            this.UpdateTracerFromCurve(Tracer, Scene);
        }
        return true;
    }

    RefreshCurvesKeepTracers(Scene, Bounds, SimTime)
    {
        if (!this.ActiveCount) return;
        const NowMs = typeof performance !== 'undefined' ? performance.now() : 0;
        if (NowMs && NowMs - this.LastCurveRefreshMs < 240) return;
        this.LastCurveRefreshMs = NowMs;

        const TargetCount = Clamp(Math.round(Scene?.Viz?.BokehCount ?? 320), 60, BOKEH_MAX_TRACERS);
        this.Curves = this.BuildStructuredCurves(Scene, Bounds, SimTime, TargetCount);
        this.TopologyKey = this.ComputeTopologySignature(Scene, Bounds);
        this.SceneKey = this.ComputeSceneSignature(Scene, Bounds);
        this.ActiveCount = TargetCount;
        if (this.Curves.length === 0) return;

        for (let I = 0; I < this.ActiveCount; ++I)
        {
            const Tracer = this.Tracers[I];
            Tracer.CurveIdx = I % this.Curves.length;
            this.UpdateTracerFromCurve(Tracer, Scene);
        }
    }

    Step(Scene, Bounds, SimTime, Dt)
    {
        this.EnsureSynced(Scene, Bounds, SimTime, false);
        const ClampedDt = Clamp(Dt || 0, 0, 0.05);
        if (ClampedDt <= 1e-5 || !this.Curves.length) return;

        const SpeedMul = Clamp(Scene?.Viz?.BokehSpeed ?? 0.45, 0.08, 2.5);
        const NumCurves = this.Curves.length;

        for (let I = 0; I < this.ActiveCount; ++I)
        {
            const Tracer = this.Tracers[I];
            const Curve = this.Curves[Tracer.CurveIdx % NumCurves];
            const ArcFactor = 220 / Math.max(85, Curve.TotalArc);
            // Calm, graceful progression along structured RK4 curves (~10-16 px/s at default 0.45x speed)
            const PhaseRate = (0.040 + 0.036 * Curve.MidMagShare) * SpeedMul * Clamp(ArcFactor, 0.55, 1.45);
            Tracer.Phase = (Tracer.Phase + PhaseRate * ClampedDt) % 1.0;
            Tracer.Age = Tracer.Phase;
            Tracer.Life = 1.0;
            this.UpdateTracerFromCurve(Tracer, Scene);
        }
    }

    FillWebGLBuffers(Scene, RibbonGeo, BeadGeo)
    {
        const Viz = Scene?.Viz || {};
        const RibPos = RibbonGeo.getAttribute('position').array;
        const RibTan = RibbonGeo.getAttribute('aTangent').array;
        const RibCol = RibbonGeo.getAttribute('aColor').array;
        const RibUv = RibbonGeo.getAttribute('aUv').array;
        const RibAlpha = RibbonGeo.getAttribute('aAlpha').array;
        const MaxRVerts = Math.floor(RibPos.length / 3);

        const BeadPos = BeadGeo.getAttribute('position').array;
        const BeadCorner = BeadGeo.getAttribute('aCorner').array;
        const BeadCol = BeadGeo.getAttribute('aColor').array;
        const BeadDof = BeadGeo.getAttribute('aDof').array;
        const BeadRad = BeadGeo.getAttribute('aRadiusPx').array;
        const BeadAlpha = BeadGeo.getAttribute('aAlpha').array;
        const MaxBVerts = Math.floor(BeadPos.length / 3);

        let RVert = 0;
        let BVert = 0;

        const WriteRibbonSegmentQuad = (P0x, P0y, P0z, P1x, P1y, P1z, T0x, T0y, T0z, T1x, T1y, T1z, C0, C1, U0, U1, AlphaVal) => {
            if (RVert + 6 > MaxRVerts) return;
            const WriteVert = (Px, Py, Pz, Tx, Ty, Tz, C, Sx, U) => {
                const I3 = RVert * 3;
                const I2 = RVert * 2;
                RibPos[I3] = Px; RibPos[I3 + 1] = Py; RibPos[I3 + 2] = Pz;
                RibTan[I3] = Tx; RibTan[I3 + 1] = Ty; RibTan[I3 + 2] = Tz;
                RibCol[I3] = C[0]; RibCol[I3 + 1] = C[1]; RibCol[I3 + 2] = C[2];
                RibUv[I2] = Sx; RibUv[I2 + 1] = U;
                RibAlpha[RVert] = AlphaVal;
                RVert++;
            };
            WriteVert(P0x, P0y, P0z, T0x, T0y, T0z, C0, -1, U0);
            WriteVert(P0x, P0y, P0z, T0x, T0y, T0z, C0, 1, U0);
            WriteVert(P1x, P1y, P1z, T1x, T1y, T1z, C1, -1, U1);

            WriteVert(P0x, P0y, P0z, T0x, T0y, T0z, C0, 1, U0);
            WriteVert(P1x, P1y, P1z, T1x, T1y, T1z, C1, 1, U1);
            WriteVert(P1x, P1y, P1z, T1x, T1y, T1z, C1, -1, U1);
        };

        // 1. Subtle structured streamline guide spines so full vector-field curves read cleanly everywhere
        for (const Curve of this.Curves)
        {
            const Pts = Curve.Pts;
            const N = Pts.length;
            if (N < 4) continue;
            const { MidRgb, TailRgb } = ResolveBokehColors(Viz, Curve.MidMagShare, Pts[Math.floor(N * 0.5)].Y);
            const GuideCol = [
                TailRgb[0] * 0.60 + MidRgb[0] * 0.40,
                TailRgb[1] * 0.60 + MidRgb[1] * 0.40,
                TailRgb[2] * 0.60 + MidRgb[2] * 0.40
            ];
            const GuideAlpha = 0.16 + 0.14 * Curve.MidMagShare;
            for (let S = 0; S < N - 1; ++S)
            {
                if (RVert + 6 > MaxRVerts) break;
                const A = Pts[S], B = Pts[S + 1];
                const P0x = A.X * WORLD_SCALE, P0y = A.Y * WORLD_SCALE, P0z = A.Z * WORLD_SCALE;
                const P1x = B.X * WORLD_SCALE, P1y = B.Y * WORLD_SCALE, P1z = B.Z * WORLD_SCALE;
                let Tx = P1x - P0x, Ty = P1y - P0y, Tz = P1z - P0z;
                const TL = Math.hypot(Tx, Ty, Tz);
                if (TL < 1e-4) continue;
                Tx /= TL; Ty /= TL; Tz /= TL;
                const EndsFade = Math.sin(Math.PI * ((S + 0.5) / (N - 1)));
                WriteRibbonSegmentQuad(P0x, P0y, P0z, P1x, P1y, P1z, Tx, Ty, Tz, Tx, Ty, Tz, GuideCol, GuideCol, 0.58, 0.60, GuideAlpha * Math.max(0.25, EndsFade));
            }
        }

        // 2. Smooth tapered bokeh wake ribbons + optical focal/aperture heads gliding along curves
        for (let I = 0; I < this.ActiveCount; ++I)
        {
            const Tracer = this.Tracers[I];
            const VLen = Tracer.ValidLen;
            if (VLen < 2) continue;

            const ULife = Clamp(Tracer.Phase, 0, 1);
            const LifeEnv = SmoothStep01(0, 0.11, ULife) * (1 - SmoothStep01(0.84, 1, ULife));
            if (LifeEnv <= 0.01) continue;

            const SpdGain = 0.52 + 0.48 * Clamp(Tracer.SpeedShare, 0, 1);
            const TracerAlpha = LifeEnv * SpdGain;
            const Tr = Tracer.Trail;
            const { HeadRgb, MidRgb, TailRgb } = ResolveBokehColors(Viz, Tracer.SpeedShare, Tracer.Y);

            const SegCount = VLen - 1;
            const DenomSegs = Math.max(1, BOKEH_TRAIL_SEGS - 1);
            for (let S = 0; S < SegCount; ++S)
            {
                if (RVert + 6 > MaxRVerts) break;
                const K0 = S * 4;
                const K1 = (S + 1) * 4;
                const P0x = Tr[K0] * WORLD_SCALE, P0y = Tr[K0 + 1] * WORLD_SCALE, P0z = Tr[K0 + 2] * WORLD_SCALE;
                const P1x = Tr[K1] * WORLD_SCALE, P1y = Tr[K1 + 1] * WORLD_SCALE, P1z = Tr[K1 + 2] * WORLD_SCALE;

                const SegDx = P1x - P0x, SegDy = P1y - P0y, SegDz = P1z - P0z;
                const SegLen = Math.hypot(SegDx, SegDy, SegDz);
                if (SegLen < 1e-4 || SegLen > 6.5) continue;

                const KPrev = Math.max(0, S - 1) * 4;
                const KNext = Math.min(VLen - 1, S + 2) * 4;
                const PPrevX = Tr[KPrev] * WORLD_SCALE, PPrevY = Tr[KPrev + 1] * WORLD_SCALE, PPrevZ = Tr[KPrev + 2] * WORLD_SCALE;
                const PNextX = Tr[KNext] * WORLD_SCALE, PNextY = Tr[KNext + 1] * WORLD_SCALE, PNextZ = Tr[KNext + 2] * WORLD_SCALE;

                let T0x = S === 0 ? SegDx : (P1x - PPrevX);
                let T0y = S === 0 ? SegDy : (P1y - PPrevY);
                let T0z = S === 0 ? SegDz : (P1z - PPrevZ);
                const T0L = Math.hypot(T0x, T0y, T0z) || 1;
                T0x /= T0L; T0y /= T0L; T0z /= T0L;

                let T1x = S >= SegCount - 1 ? SegDx : (PNextX - P0x);
                let T1y = S >= SegCount - 1 ? SegDy : (PNextY - P0y);
                let T1z = S >= SegCount - 1 ? SegDz : (PNextZ - P0z);
                const T1L = Math.hypot(T1x, T1y, T1z) || 1;
                T1x /= T1L; T1y /= T1L; T1z /= T1L;

                const U0 = S / DenomSegs;
                const U1 = (S + 1) / DenomSegs;

                const ColorAtU = (U) => {
                    if (U < 0.48)
                    {
                        const T = SmoothStep01(0, 0.48, U);
                        return [
                            HeadRgb[0] + (MidRgb[0] - HeadRgb[0]) * T,
                            HeadRgb[1] + (MidRgb[1] - HeadRgb[1]) * T,
                            HeadRgb[2] + (MidRgb[2] - HeadRgb[2]) * T
                        ];
                    }
                    const T = SmoothStep01(0.45, 1, U);
                    return [
                        MidRgb[0] + (TailRgb[0] - MidRgb[0]) * T,
                        MidRgb[1] + (TailRgb[1] - MidRgb[1]) * T,
                        MidRgb[2] + (TailRgb[2] - MidRgb[2]) * T
                    ];
                };
                WriteRibbonSegmentQuad(P0x, P0y, P0z, P1x, P1y, P1z, T0x, T0y, T0z, T1x, T1y, T1z, ColorAtU(U0), ColorAtU(U1), U0, U1, TracerAlpha);
            }

            if (BVert + 6 > MaxBVerts) continue;
            const Bx = Tr[0] * WORLD_SCALE;
            const By = Tr[1] * WORLD_SCALE;
            const Bz = Tr[2] * WORLD_SCALE;
            const Dof = Tracer.Dof;
            // Refined lens optics sizing: ~82% crisp bright pinpoint stars, ~18% subtle soft bokeh aperture orbs
            const Aperture = Math.pow(Dof, 4.2);
            const RadPx = (1.8 + Aperture * 6.4) * (0.86 + 0.28 * Tracer.SizeSeed);
            const BeadAlphaVal = LifeEnv * (0.64 + 0.36 * Tracer.SpeedShare);
            const QuadCorners = [[-1, -1], [1, -1], [-1, 1], [1, -1], [1, 1], [-1, 1]];
            for (const [Cx, Cy] of QuadCorners)
            {
                const I3 = BVert * 3;
                const I2 = BVert * 2;
                BeadPos[I3] = Bx; BeadPos[I3 + 1] = By; BeadPos[I3 + 2] = Bz;
                BeadCorner[I2] = Cx; BeadCorner[I2 + 1] = Cy;
                BeadCol[I3] = HeadRgb[0]; BeadCol[I3 + 1] = HeadRgb[1]; BeadCol[I3 + 2] = HeadRgb[2];
                BeadDof[BVert] = Dof;
                BeadRad[BVert] = RadPx;
                BeadAlpha[BVert] = BeadAlphaVal;
                BVert++;
            }
        }

        RibbonGeo.getAttribute('position').needsUpdate = true;
        RibbonGeo.getAttribute('aTangent').needsUpdate = true;
        RibbonGeo.getAttribute('aColor').needsUpdate = true;
        RibbonGeo.getAttribute('aUv').needsUpdate = true;
        RibbonGeo.getAttribute('aAlpha').needsUpdate = true;
        RibbonGeo.setDrawRange(0, RVert);

        BeadGeo.getAttribute('position').needsUpdate = true;
        BeadGeo.getAttribute('aCorner').needsUpdate = true;
        BeadGeo.getAttribute('aColor').needsUpdate = true;
        BeadGeo.getAttribute('aDof').needsUpdate = true;
        BeadGeo.getAttribute('aRadiusPx').needsUpdate = true;
        BeadGeo.getAttribute('aAlpha').needsUpdate = true;
        BeadGeo.setDrawRange(0, BVert);

        return { RibbonVerts: RVert, BeadVerts: BVert };
    }
}

export class VectorFieldGPUViewport
{
    constructor(ContainerEl, Callbacks = {})
    {
        this.Container = ContainerEl;
        this.Callbacks = Callbacks;
        this.SceneState = null;
        this.SelectedToken = null;
        this.ViewMode = '3d';
        this.LastSimTime = 0;
        this.LastBakedTime = -999;
        this.LastSyncTime = -999;
        this.UniformsDirty = true;
        this.BakeDirty = true;
        this.LastFrameMs = performance.now();
        this.FpsSmoothed = 60;
        this.StreamDirty = true;
        this.StreamSeedHash = '';
        this.OrthoZoomMetres = DEFAULT_HALF_M;
        this.OrthoCenter = [0, 0, 48];
        this.ActiveBakeHalfM = DEFAULT_HALF_M;
        this.ActiveBakeCenter = [0, 0, 48];
        this.LastCustomKey = '';

        this.FallbackZoom = 1.0;
        this.FallbackCenter = [0, 0, 48];
        this.FallbackYaw = 0.62;
        this.FallbackPitch = 0.64;
        this.FallbackHeatPanes = [];
        this.FallbackPickState = { iso: null, ortho: null };
        this.SourceVisuals = new Map();
        this.GizmoHandles = [];
        this.WaypointHandles = [];
        this.SelectedWaypointIdx = null;
        this.HoveredWaypoint = null;
        this.LastStreamRebuildMs = 0;
        this.HoveredGizmoAxis = null;
        this.HoveredToken = null;

        this.ShadersReady = false;
        this.ContextLost = false;
        this.FallbackMode = false;
        this.FallbackReason = '';
        this.BokehSim = new BokehStreamRibbonSim();
        this.BokehDirty = true;
        this.GpuInfo = { Renderer: 'WebGL2', BakeMode: 'Float16', StreamMode: 'Bokeh Stream Ribbons', Stages: [] };

        this.InitRendererWithFallback();
    }

    TryCreateWebGLRenderer()
    {
        const Attempts = [
            { antialias: true, alpha: false, failIfMajorPerformanceCaveat: false },
            { antialias: false, alpha: false, failIfMajorPerformanceCaveat: false }
        ];
        for (const Opts of Attempts)
        {
            try
            {
                const R = new THREE.WebGLRenderer(Opts);
                const GL = R.getContext();
                if (GL && !GL.isContextLost()) return R;
                R.dispose();
            }
            catch {}
        }
        return null;
    }

    InitRendererWithFallback()
    {
        this.Renderer = this.TryCreateWebGLRenderer();
        if (!this.Renderer)
        {
            this.ActivateFallbackRenderer('WebGL disabled by browser — Software 2D/Iso Renderer active');
            return;
        }

        this.Renderer.debug.checkShaderErrors = true;
        this.Renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
        this.Renderer.setClearColor(0x0b0d10, 1);
        this.Renderer.autoClear = false;
        this.Renderer.domElement.style.background = '#0b0d10';
        this.Renderer.domElement.style.display = 'block';
        this.Container.appendChild(this.Renderer.domElement);

        this.Renderer.domElement.addEventListener('webglcontextlost', (E) => {
            E.preventDefault();
            this.ContextLost = true;
            this.ShadersReady = false;
            this.ActivateFallbackRenderer('WebGL context lost — switched to Software 2D/Iso Renderer');
        });

        this.ThreeScene = new THREE.Scene();
        this.ThreeScene.background = new THREE.Color(0x0b0d10);
        this.ThreeScene.fog = new THREE.FogExp2(0x0b0d10, 0.0018);

        this.PerspCamera = new THREE.PerspectiveCamera(44, 1.6, 0.3, 600);
        this.PerspCamera.position.set(42, 34, 52);

        this.OrthoCamera = new THREE.OrthographicCamera(-40, 40, 40, -40, 0.1, 600);
        this.OrthoCamera.position.set(0, 90, 0);
        this.OrthoCamera.up.set(0, 0, -1);
        this.OrthoCamera.lookAt(0, 0, 0);

        this.Controls = new OrbitControls(this.PerspCamera, this.Renderer.domElement);
        this.Controls.enableDamping = true;
        this.Controls.dampingFactor = 0.08;
        this.Controls.enableZoom = true;
        this.Controls.zoomSpeed = 1.15;
        this.Controls.minDistance = 2.5;
        this.Controls.maxDistance = 280;
        this.Controls.maxPolarAngle = Math.PI * 0.495;
        this.Controls.target.set(0, 1.2, 0);
        this.Controls.addEventListener('change', () => this.NotifyZoomMayHaveChanged());

        this.SharedUniforms = {
            uForceCount: { value: 0 },
            uTime: { value: 0 },
            uWorldScale: { value: WORLD_SCALE },
            uForceA: { value: CreateVector4Array(MAX_GPU_FORCES) },
            uForceB: { value: CreateVector4Array(MAX_GPU_FORCES) },
            uForceC: { value: CreateVector4Array(MAX_GPU_FORCES) },
            uForceD: { value: CreateVector4Array(MAX_GPU_FORCES) },
            uForceE: { value: CreateVector4Array(MAX_GPU_FORCES) },
            uColormap: { value: 0 },
            uQuantity: { value: 0 },
            uSliceAxis: { value: 0 },
            uSliceHeight: { value: 16 },
            uSliceHalfM: { value: DEFAULT_HALF_M },
            uBakeCenter: { value: new THREE.Vector2(0, 0) },
            uBakeMode: { value: 0 },
            uFixedMax: { value: 34 },
            uOpacity: { value: 0.58 },
            uQuietCutoff: { value: 0.25 },
            uFieldBakeTex: { value: null },
            uViewportHeightPx: { value: 600 },
            uStreamThickness: { value: 0.62 },
            uStreamEmissive: { value: 1.0 },
            uStreamOpacity: { value: 0.92 },
            uBokehThickness: { value: 1.0 },
            uBokehEmissive: { value: 1.0 }
        };

        this.ProbeGpuCapabilities();
        this.BuildLightingAndGround();
        this.BuildGpuSliceBakeAndDisplay();
        this.BuildGpuArrows();
        this.BuildGpuStreamlines();
        this.BuildGpuBokehRibbons();
        this.BuildBeaconsAndProbe();
        this.BindPointerEvents(this.Renderer.domElement, false);
        this.Resize();
        this.Renderer.clear();
        this.StartShaderCompilation();
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                         FIELD ZOOMING API (3D, 2D, SPLIT & FALLBACK)
    //------------------------------------------------------------------------------------------------------------------------

    GetZoomPercent()
    {
        if (this.FallbackMode) return Math.round(this.FallbackZoom * 100);
        if (this.ViewMode === '2d') return Math.round((DEFAULT_HALF_M / Math.max(20, this.OrthoZoomMetres)) * 100);
        const DefaultDist = 74.6; // length of (42, 34 - 1.2, 52)
        const Dist = this.PerspCamera ? this.PerspCamera.position.distanceTo(this.Controls.target) : DefaultDist;
        return Math.round((DefaultDist / Math.max(2, Dist)) * 100);
    }

    NotifyZoomMayHaveChanged()
    {
        this.Callbacks.OnZoomChange?.(this.GetZoomPercent());
    }

    ZoomBy(ScaleFactor)
    {
        if (this.FallbackMode)
        {
            this.FallbackZoom = Clamp(this.FallbackZoom * ScaleFactor, 0.3, 8.5);
            this.FallbackHeatDirty = true;
            this.NotifyZoomMayHaveChanged();
            return;
        }

        if (this.ViewMode === '2d' || this.ViewMode === 'split')
        {
            this.OrthoZoomMetres = Clamp(this.OrthoZoomMetres / ScaleFactor, 38, 1250);
        }
        if (this.ViewMode === '3d' || this.ViewMode === 'split')
        {
            const Offset = this.PerspCamera.position.clone().sub(this.Controls.target);
            const NextLen = Clamp(Offset.length() / ScaleFactor, 2.5, 280);
            Offset.setLength(NextLen);
            this.PerspCamera.position.copy(this.Controls.target).add(Offset);
            this.Controls.update();
        }
        this.UpdateBakeWindowFromCamera(true);
        this.NotifyZoomMayHaveChanged();
    }

    ZoomIn() { this.ZoomBy(1.25); }
    ZoomOut() { this.ZoomBy(0.8); }

    FitScene()
    {
        this.ResetCamera();
        this.NotifyZoomMayHaveChanged();
    }

    UpdateBakeWindowFromCamera(Force = false)
    {
        if (this.FallbackMode || !this.SceneState) return;
        let TargetHalf = DEFAULT_HALF_M;
        let Fx = 0, Fy = 48, Fz = 0;

        const PaneW = this.ViewMode === 'split' ? (this.Width || 800) * 0.5 : (this.Width || 800);
        const PaneH = Math.max(1, this.Height || 500);
        const Aspect = Math.max(1.0, PaneW / PaneH);

        if (this.ViewMode === '2d')
        {
            TargetHalf = Clamp(this.OrthoZoomMetres * Aspect * 1.15, 65, 980);
            Fx = this.OrthoCenter[0] || 0;
            Fz = this.OrthoCenter[1] || 0;
            Fy = this.OrthoCenter[2] ?? 48;
        }
        else
        {
            const Offset = this.PerspCamera.position.clone().sub(this.Controls.target);
            const Dist = Offset.length();
            const SinElev = Clamp(Math.abs(Offset.y) / Math.max(0.1, Dist), 0.28, 1.0);
            const VertHalfM = Dist * Math.tan(THREE.MathUtils.degToRad((this.PerspCamera.fov || 44) * 0.5)) * INV_SCALE;
            const HorizHalfM = VertHalfM * Aspect;
            TargetHalf = Clamp(Math.max(HorizHalfM, VertHalfM / SinElev) * 1.55, 85, 980);

            Fx = Clamp(this.Controls.target.x * INV_SCALE, -650, 650);
            Fz = Clamp(this.Controls.target.z * INV_SCALE, -650, 650);
            Fy = Clamp(this.Controls.target.y * INV_SCALE, 20, 140);
        }

        const SizeChanged = Math.abs(TargetHalf - this.ActiveBakeHalfM) / Math.max(40, this.ActiveBakeHalfM) > 0.14;
        const CenterMoved = Math.hypot(
            Fx - this.ActiveBakeCenter[0],
            Fz - this.ActiveBakeCenter[1],
            Fy - (this.ActiveBakeCenter[2] ?? 48)
        ) > TargetHalf * 0.12;
        if (!Force && !SizeChanged && !CenterMoved) return;

        this.ActiveBakeHalfM = TargetHalf;
        this.ActiveBakeCenter = [Fx, Fz, Fy];
        this.SharedUniforms.uSliceHalfM.value = TargetHalf;
        if (this.BakeCenters)
        {
            this.BakeCenters[0].set(Fx, Fz); // XZ: u=X, v=Z
            this.BakeCenters[1].set(Fx, Fy); // XY: u=X, v=Y
            this.BakeCenters[2].set(Fz, Fy); // YZ: u=Z, v=Y
            const PrimId = AXIS_CODES[this.SceneState.Viz?.SliceAxis || 'XZ'] ?? 0;
            this.SharedUniforms.uBakeCenter.value.copy(this.BakeCenters[PrimId]);
        }
        else
        {
            this.SharedUniforms.uBakeCenter.value.set(Fx, Fz);
        }
        this.UpdateProbeAndHeatmapGeometry();
        this.BakeDirty = true;
        this.StreamDirty = true;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                        SOFTWARE 2D / ISOMETRIC CANVAS FALLBACK (IF WEBGL IS DISABLED)
    //------------------------------------------------------------------------------------------------------------------------

    ActivateFallbackRenderer(Reason)
    {
        if (this.FallbackMode) return;
        this.FallbackMode = true;
        this.FallbackReason = Reason;
        this.ShadersReady = true;
        if (this.Renderer?.domElement?.parentNode) this.Renderer.domElement.remove();

        this.FallbackCanvas = document.createElement('canvas');
        this.FallbackCanvas.style.cssText = 'width:100%;height:100%;display:block;background:#0b0d10;';
        this.Container.appendChild(this.FallbackCanvas);
        this.FallbackCtx = this.FallbackCanvas.getContext('2d');

        // 6 offscreen heatmap slots: [0..2]=Iso (XZ,XY,YZ), [3..5]=2D Ortho (XZ,XY,YZ)
        this.FallbackHeatPanes = [0, 1, 2, 3, 4, 5].map(() => {
            const C = document.createElement('canvas');
            C.width = 88;
            C.height = 88;
            return { Canvas: C, Key: '', Time: -999 };
        });
        this.FallbackStreamPanes = [0, 1].map(() => ({ Key: '', Curves: [] }));
        this.FallbackHeatDirty = true;

        this.BindPointerEvents(this.FallbackCanvas, true);
        this.Resize();
        this.NotifyZoomMayHaveChanged();

        this.Callbacks.OnCompileProgress?.({
            Stage: 4,
            Total: 4,
            Label: Reason,
            Percent: 100,
            Done: true,
            Fallback: true,
            Stages: [{ Name: Reason, Ms: 0, Ok: true }],
            GpuInfo: { BakeMode: 'Software Canvas 2D/Iso', StreamMode: 'Optical Ribbon Trails' }
        });
    }

    RenderFallbackPane(Ctx, Px, Py, Pw, Ph, IsIso, SimTime)
    {
        if (!this.SceneState) return;
        Ctx.save();
        Ctx.beginPath();
        Ctx.rect(Px, Py, Pw, Ph);
        Ctx.clip();
        Ctx.fillStyle = '#0b0d10';
        Ctx.fillRect(Px, Py, Pw, Ph);

        const Viz = this.SceneState.Viz;
        const Solo = Viz.SoloToken || null;
        const OffsetM = Viz.SliceHeight ?? 16;
        const ActiveAxes = GetActiveSliceAxes(Viz);
        const PaneAxes = IsIso ? ActiveAxes : [Viz.SliceAxis || ActiveAxes[0]];
        const PivotY = IsIso && PaneAxes.length === 1 && PaneAxes[0] === 'XZ' ? OffsetM : (this.FallbackCenter[2] ?? 48);

        const Frames = {};
        for (const Axis of PaneAxes)
        {
            Frames[Axis] = ComputeFallbackPlaneFrame(
                Px, Py, Pw, Ph, IsIso, Axis,
                this.FallbackZoom, this.FallbackCenter,
                this.FallbackYaw, this.FallbackPitch, OffsetM, PivotY
            );
        }
        const BaseFrame = Frames[PaneAxes[0]];
        const { Span, ZoomRatio, MapPt } = BaseFrame;
        const MultiSlice = PaneAxes.length > 1;
        const IsVolumetric3D = IsIso;

        // 1. Per-Axis Grid Lines & Full-Viewport 88×88 Heatmap Bake
        for (const Axis of PaneAxes)
        {
            const Frame = Frames[Axis];
            const { MinU, MaxU, MinV, MaxV, MapPlaneUV } = Frame;
            const GridStep = Span < 110 ? 20 : Span < 220 ? 25 : Span < 380 ? 50 : 100;
            const MinGU = Math.floor(MinU / GridStep) * GridStep;
            const MaxGU = Math.ceil(MaxU / GridStep) * GridStep;
            const MinGV = Math.floor(MinV / GridStep) * GridStep;
            const MaxGV = Math.ceil(MaxV / GridStep) * GridStep;

            Ctx.strokeStyle = MultiSlice ? 'rgba(255,255,255,0.045)' : 'rgba(255,255,255,0.065)';
            Ctx.lineWidth = 1;
            for (let G = MinGU; G <= MaxGU; G += GridStep)
            {
                const A = MapPlaneUV(G, MinGV), B = MapPlaneUV(G, MaxGV);
                Ctx.beginPath(); Ctx.moveTo(A[0], A[1]); Ctx.lineTo(B[0], B[1]); Ctx.stroke();
            }
            for (let G = MinGV; G <= MaxGV; G += GridStep)
            {
                const C = MapPlaneUV(MinGU, G), D = MapPlaneUV(MaxGU, G);
                Ctx.beginPath(); Ctx.moveTo(C[0], C[1]); Ctx.lineTo(D[0], D[1]); Ctx.stroke();
            }

            if (Viz.Heatmap)
            {
                const SlotIdx = (IsIso ? 0 : 3) + (AXIS_CODES[Axis] ?? 0);
                const PaneSlot = this.FallbackHeatPanes[SlotIdx];
                const Res = 88;
                const OpacityScale = MultiSlice ? (Viz.Opacity * 0.72) : Viz.Opacity;
                const BoundsKey = `${Axis}:${Math.round(MinU)}:${Math.round(MaxU)}:${Math.round(MinV)}:${Math.round(MaxV)}:${Viz.Quantity}:${Viz.Colormap}:${Viz.FixedMax}:${OpacityScale.toFixed(2)}:${OffsetM}:${Solo || ''}`;
                if (this.FallbackHeatDirty || PaneSlot.Key !== BoundsKey || Math.abs(SimTime - PaneSlot.Time) > 0.14)
                {
                    PaneSlot.Key = BoundsKey;
                    PaneSlot.Time = SimTime;
                    const HCtx = PaneSlot.Canvas.getContext('2d');
                    const Img = HCtx.createImageData(Res, Res);
                    const FixedMax = Math.max(4, Viz.FixedMax || 34);
                    const DiffStep = Math.max(1.5, Math.min(MaxU - MinU, MaxV - MinV) / 110);

                    for (let Row = 0; Row < Res; ++Row)
                    {
                        const Tv = Row / (Res - 1);
                        const Wv = MinV + Tv * (MaxV - MinV);
                        for (let Col = 0; Col < Res; ++Col)
                        {
                            const Tu = Col / (Res - 1);
                            const Wu = MinU + Tu * (MaxU - MinU);
                            const [Wx, Wy, Wz] = PlaneToWorld(Axis, Wu, Wv, OffsetM);
                            let Val = 0;
                            if (Viz.Quantity === 'Curl' || Viz.Quantity === 'Divergence')
                            {
                                const Diff = SampleDifferential(this.SceneState, Wx, Wy, Wz, SimTime, DiffStep, { Solo });
                                Val = Viz.Quantity === 'Divergence' ? Diff.Divergence * 22 : Diff.CurlMag * 22;
                            }
                            else
                            {
                                const R = EvaluateComposite(this.SceneState, Wx, Wy, Wz, SimTime, { Solo });
                                Val = Viz.Quantity === 'Acceleration' ? R.AccelMag : Viz.Quantity === 'Pressure' ? R.DynamicPressurePa / 20 : R.TotalMag;
                            }
                            const Signed = Viz.Quantity === 'Divergence';
                            const Share = Signed ? Clamp(0.5 + Val / FixedMax, 0, 1) : Clamp(Val / FixedMax, 0, 1);
                            const RGB = ColormapRgb01(Signed ? 'Diverging' : Viz.Colormap, Share);
                            const Alpha = Signed ? OpacityScale : Share < 0.02 ? (MultiSlice ? 0.01 : 0.03) : Clamp(Share * 1.4, MultiSlice ? 0.04 : 0.1, OpacityScale);
                            const K = (Row * Res + Col) * 4;
                            Img.data[K] = Math.round(RGB[0] * 255);
                            Img.data[K + 1] = Math.round(RGB[1] * 255);
                            Img.data[K + 2] = Math.round(RGB[2] * 255);
                            Img.data[K + 3] = Math.round(Alpha * 255);
                        }
                    }
                    HCtx.putImageData(Img, 0, 0);
                }

                const P0 = MapPlaneUV(MinU, MinV);
                const P1 = MapPlaneUV(MaxU, MinV);
                const P2 = MapPlaneUV(MinU, MaxV);
                const Cross = Math.abs((P1[0] - P0[0]) * (P2[1] - P0[1]) - (P2[0] - P0[0]) * (P1[1] - P0[1]));
                if (Cross > 1.0)
                {
                    Ctx.save();
                    Ctx.transform((P1[0] - P0[0]) / Res, (P1[1] - P0[1]) / Res, (P2[0] - P0[0]) / Res, (P2[1] - P0[1]) / Res, P0[0], P0[1]);
                    Ctx.drawImage(PaneSlot.Canvas, 0, 0);
                    Ctx.restore();
                }
            }
        }
        this.FallbackHeatDirty = false;

        // Subtle intersection lines between active orthogonal slices
        if (IsIso && MultiSlice)
        {
            Ctx.save();
            Ctx.strokeStyle = 'rgba(155, 224, 198, 0.25)';
            Ctx.lineWidth = 1;
            Ctx.setLineDash([4, 4]);
            if (Frames.XZ && Frames.XY)
            {
                const A = MapPt(Frames.XZ.MinU, OffsetM, OffsetM), B = MapPt(Frames.XZ.MaxU, OffsetM, OffsetM);
                Ctx.beginPath(); Ctx.moveTo(A[0], A[1]); Ctx.lineTo(B[0], B[1]); Ctx.stroke();
            }
            if (Frames.XZ && Frames.YZ)
            {
                const A = MapPt(OffsetM, OffsetM, Frames.XZ.MinV), B = MapPt(OffsetM, OffsetM, Frames.XZ.MaxV);
                Ctx.beginPath(); Ctx.moveTo(A[0], A[1]); Ctx.lineTo(B[0], B[1]); Ctx.stroke();
            }
            if (Frames.XY && Frames.YZ)
            {
                const A = MapPt(OffsetM, Frames.XY.MinV, OffsetM), B = MapPt(OffsetM, Frames.XY.MaxV, OffsetM);
                Ctx.beginPath(); Ctx.moveTo(A[0], A[1]); Ctx.lineTo(B[0], B[1]); Ctx.stroke();
            }
            Ctx.restore();
        }

        // 2. Semi-Transparent 3D Volume Source Bounds & Beacons (no wireframes)
        if (Viz.Bounds)
        {
            const ShowXZ = PaneAxes.includes('XZ');
            for (const S of this.SceneState.Sources)
            {
                const Model = ForceModelById[S.Model];
                const Col = S.UseFormula ? '#ffb454' : (Model?.Colour || '#78d2c6');
                const IsSel = S.Token === this.SelectedToken;
                const IsLargeEnv = (S.Radius || 180) > 260;
                const FillOp = S.Enabled
                    ? (IsSel ? (IsLargeEnv ? 0.14 : 0.22) : (IsLargeEnv ? 0.05 : 0.11))
                    : 0.03;

                Ctx.fillStyle = Col;
                Ctx.globalAlpha = FillOp;
                if (S.Reach === 'Box')
                {
                    const Hx = S.HalfX || 120, Hz = S.HalfZ || 120;
                    const Hy = IsIso ? Math.min(180, S.Height || 140) : 0;
                    const Ang = ((S.Bearing || 0) * Math.PI) / 180;
                    const Ca = Math.cos(Ang), Sa = Math.sin(Ang);
                    const Rot = (Lx, Lz) => [S.Centre[0] + Lx * Ca - Lz * Sa, S.Centre[2] + Lx * Sa + Lz * Ca];
                    const C00 = Rot(-Hx, -Hz), C10 = Rot(Hx, -Hz), C11 = Rot(Hx, Hz), C01 = Rot(-Hx, Hz);
                    const DrawPoly = (Pts) => {
                        Ctx.beginPath();
                        Pts.forEach((Pt, Idx) => { if (Idx === 0) Ctx.moveTo(Pt[0], Pt[1]); else Ctx.lineTo(Pt[0], Pt[1]); });
                        Ctx.closePath();
                        Ctx.fill();
                    };
                    DrawPoly([MapPt(C00[0], S.Centre[1], C00[1]), MapPt(C10[0], S.Centre[1], C10[1]), MapPt(C11[0], S.Centre[1], C11[1]), MapPt(C01[0], S.Centre[1], C01[1])]);
                    if (IsIso && Hy > 1)
                    {
                        const TopY = S.Centre[1] + Hy;
                        DrawPoly([MapPt(C00[0], TopY, C00[1]), MapPt(C10[0], TopY, C10[1]), MapPt(C11[0], TopY, C11[1]), MapPt(C01[0], TopY, C01[1])]);
                        DrawPoly([MapPt(C00[0], S.Centre[1], C00[1]), MapPt(C10[0], S.Centre[1], C10[1]), MapPt(C10[0], TopY, C10[1]), MapPt(C00[0], TopY, C00[1])]);
                        DrawPoly([MapPt(C10[0], S.Centre[1], C10[1]), MapPt(C11[0], S.Centre[1], C11[1]), MapPt(C11[0], TopY, C11[1]), MapPt(C10[0], TopY, C10[1])]);
                    }
                }
                else
                {
                    Ctx.beginPath();
                    for (let I = 0; I <= 44; ++I)
                    {
                        const A = (I / 44) * Math.PI * 2;
                        const Pt = MapPt(S.Centre[0] + Math.cos(A) * S.Radius, S.Centre[1], S.Centre[2] + Math.sin(A) * S.Radius);
                        if (I === 0) Ctx.moveTo(Pt[0], Pt[1]);
                        else Ctx.lineTo(Pt[0], Pt[1]);
                    }
                    Ctx.closePath();
                    Ctx.fill();
                    if (IsIso && S.Reach === 'Column')
                    {
                        const TopY = S.Centre[1] + Math.min(200, S.Height || 180);
                        Ctx.beginPath();
                        for (let I = 0; I <= 44; ++I)
                        {
                            const A = (I / 44) * Math.PI * 2;
                            const Pt = MapPt(S.Centre[0] + Math.cos(A) * S.Radius, TopY, S.Centre[2] + Math.sin(A) * S.Radius);
                            if (I === 0) Ctx.moveTo(Pt[0], Pt[1]);
                            else Ctx.lineTo(Pt[0], Pt[1]);
                        }
                        Ctx.closePath();
                        Ctx.fill();
                    }
                }

                Ctx.fillStyle = Col;
            }
            Ctx.globalAlpha = 1;
        }

        // 3. Slice-Aware & Volumetric 3D Bidirectional RK4 Streamlines
        if (Viz.Streamlines)
        {
            const SeedStep = Clamp((Viz.StreamlineSpacing || 84) * ZoomRatio, 12, 160);
            const ResSteps = Clamp(Math.round(Viz.StreamResolution || 28), 8, 72);
            const TargetCount = Clamp(Math.round(Viz.StreamLineCount || 220), 24, 520);
            const ThickScale = Clamp(Viz.StreamThickness ?? 0.62, 0.2, 2.5);
            const EmissiveScale = Clamp(Viz.StreamEmissive ?? 1.0, 0.15, 3.0);
            const OpacityScale = Clamp(Viz.StreamOpacity ?? 0.92, 0.05, 1.0);
            const ColorMode = Viz.StreamColorMode || 'FieldPalette';
            const SingleColor = Viz.StreamSingleColor || '#68dcff';
            const StepM = Clamp((10 * ZoomRatio) * (24 / ResSteps), 1.2, 20);
            const FixedMax = Math.max(4, Viz.FixedMax || 34);
            const StreamSlot = this.FallbackStreamPanes[IsIso ? 0 : 1];
            const SrcSig = this.SceneState.Sources.map((s) => `${s.Token}:${s.Enabled}:${s.Strength}:${s.Swirl}:${s.Centre.join(',')}:${s.Radius}:${s.UseFormula}:${s.FormulaCode}`).join('|');
            const BoundsSig = PaneAxes.map((Ax) => `${Ax}:${Math.round(Frames[Ax].MinU)},${Math.round(Frames[Ax].MaxU)},${Math.round(Frames[Ax].MinV)},${Math.round(Frames[Ax].MaxV)}`).join('|');
            const TimeBucket = Math.round(SimTime * 6);
            const StreamKey = `${SrcSig}:${BoundsSig}:${OffsetM}:${IsIso}:${Viz.StreamlineSpacing}:${Solo || ''}:${TimeBucket}:${ResSteps}:${TargetCount}:${ColorMode}:${SingleColor}`;

            if (!StreamSlot || StreamSlot.Key !== StreamKey)
            {
                const Seeds = CollectMultiAxisStreamlineSeeds(this.SceneState, Frames, OffsetM, SeedStep, {
                    ActiveAxes: PaneAxes,
                    Is3D: IsIso,
                    Volumetric: IsIso,
                    TargetCount
                });
                const Built = [];
                for (const [Sx, Sy, Sz] of Seeds)
                {
                    const Pts = IntegrateStreamlineBidirectional(this.SceneState, Sx, Sy, Sz, SimTime, ResSteps, StepM, { Solo });
                    if (Pts.length < 4) continue;
                    let SumX = 0, SumY = 0, SumZ = 0;
                    for (const Pt of Pts) { SumX += Pt.X; SumY += Pt.Y; SumZ += Pt.Z; }
                    const N = Pts.length;
                    Built.push({
                        Pts,
                        Sx,
                        Sz,
                        AvgX: SumX / N,
                        AvgY: SumY / N,
                        AvgZ: SumZ / N,
                        MidMag: Pts[Math.floor(N * 0.5)].Mag
                    });
                }
                if (StreamSlot) { StreamSlot.Key = StreamKey; StreamSlot.Curves = Built; }
            }

            const Curves = StreamSlot?.Curves || [];
            const CosY = Math.cos(this.FallbackYaw), SinY = Math.sin(this.FallbackYaw);
            const CosP = Math.cos(this.FallbackPitch), SinP = Math.sin(this.FallbackPitch);

            if (IsVolumetric3D)
            {
                Ctx.save();
                Ctx.strokeStyle = 'rgba(130, 168, 200, 0.14)';
                Ctx.lineWidth = 1.0;
                for (const C of Curves)
                {
                    if (Math.abs(C.AvgY - OffsetM) <= 10) continue;
                    const Shd = C.Pts.map((Pt) => MapPt(Pt.X, OffsetM, Pt.Z));
                    Ctx.beginPath();
                    Ctx.moveTo(Shd[0][0], Shd[0][1]);
                    for (let I = 1; I < Shd.length; ++I) Ctx.lineTo(Shd[I][0], Shd[I][1]);
                    Ctx.stroke();
                }
                Ctx.restore();
            }

            const Ordered = IsIso
                ? [...Curves].sort((A, B) => {
                    const Da = (A.AvgY - PivotY) * SinP + ((A.AvgX - BaseFrame.PanX) * SinY + (A.AvgZ - BaseFrame.PanZ) * CosY) * CosP;
                    const Db = (B.AvgY - PivotY) * SinP + ((B.AvgX - BaseFrame.PanX) * SinY + (B.AvgZ - BaseFrame.PanZ) * CosY) * CosP;
                    return Da - Db;
                })
                : Curves;

            Ctx.save();
            Ctx.globalCompositeOperation = 'source-over';
            Ctx.lineCap = 'round';
            Ctx.lineJoin = 'round';

            const TraceCurvePath = (ScrPts, FromIdx, ToIdx) => {
                if (ToIdx <= FromIdx) return;
                Ctx.beginPath();
                Ctx.moveTo(ScrPts[FromIdx][0], ScrPts[FromIdx][1]);
                for (let I = FromIdx + 1; I < ToIdx; ++I)
                {
                    const Mx = (ScrPts[I][0] + ScrPts[I + 1][0]) * 0.5;
                    const My = (ScrPts[I][1] + ScrPts[I + 1][1]) * 0.5;
                    Ctx.quadraticCurveTo(ScrPts[I][0], ScrPts[I][1], Mx, My);
                }
                Ctx.lineTo(ScrPts[ToIdx][0], ScrPts[ToIdx][1]);
                Ctx.stroke();
            };

            for (const C of Ordered)
            {
                const { Pts, Sx, Sz, MidMag, AvgY } = C;
                const Share = Clamp(MidMag / FixedMax, 0, 1);
                const N = Pts.length;
                const ScrPts = Pts.map((Pt) => MapPt(Pt.X, Pt.Y, Pt.Z));

                // 1. Soft colored optical aura + thinner saturated luminous ribbon spine
                const HaloAlpha = Clamp((0.20 + Share * 0.12) * EmissiveScale * OpacityScale, 0.02, 0.85);
                const CoreAlpha = Clamp((0.66 + Share * 0.24) * EmissiveScale * OpacityScale, 0.06, 0.98);
                Ctx.strokeStyle = StreamlineCss(Viz.Colormap, Share, HaloAlpha, ColorMode, SingleColor, AvgY);
                Ctx.lineWidth = Math.max(0.9, 4.0 * ThickScale);
                TraceCurvePath(ScrPts, 0, N - 1);

                Ctx.strokeStyle = StreamlineCss(Viz.Colormap, Share, CoreAlpha, ColorMode, SingleColor, AvgY);
                Ctx.lineWidth = Math.max(0.55, 1.55 * ThickScale);
                TraceCurvePath(ScrPts, 0, N - 1);

                // 2. Per-strand travelling directional comet tail + glowing colored head spark
                const HRaw = Math.sin(Sx * 12.9898 + Sz * 78.233) * 43758.5453;
                const StrandSeed = HRaw - Math.floor(HRaw);
                const Speed = 0.14 + 0.12 * ((StrandSeed * 7.13) % 1) + 0.08 * Share;
                const Cyc = ((SimTime * Speed + StrandSeed) % 1 + 1) % 1;
                const HeadF = Cyc * 1.28;
                const TailF = Math.max(0, HeadF - 0.28);
                const I0 = Math.max(0, Math.floor(TailF * (N - 1)));
                const I1 = Math.min(N - 1, Math.ceil(Math.min(1, HeadF) * (N - 1)));

                if (I1 > I0)
                {
                    Ctx.strokeStyle = StreamlineCss(Viz.Colormap, Math.min(1, Share + 0.18), Clamp(0.38 * EmissiveScale * OpacityScale, 0.04, 0.9), ColorMode, SingleColor, AvgY);
                    Ctx.lineWidth = Math.max(1.2, 4.6 * ThickScale);
                    TraceCurvePath(ScrPts, I0, I1);

                    Ctx.strokeStyle = StreamlineCss(Viz.Colormap, Math.min(1, Share + 0.26), Clamp(0.92 * OpacityScale, 0.08, 0.98), ColorMode, SingleColor, AvgY);
                    Ctx.lineWidth = Math.max(0.7, 1.9 * ThickScale);
                    TraceCurvePath(ScrPts, I0, I1);
                }

                if (HeadF <= 1.02)
                {
                    const TipIdx = Math.min(N - 1, Math.max(0, Math.round(Math.min(1, HeadF) * (N - 1))));
                    const PP = ScrPts[TipIdx];
                    Ctx.fillStyle = StreamlineCss(Viz.Colormap, Math.min(1, Share + 0.24), Clamp(0.40 * EmissiveScale * OpacityScale, 0.05, 0.85), ColorMode, SingleColor, AvgY);
                    Ctx.beginPath();
                    Ctx.arc(PP[0], PP[1], Math.max(1.8, 3.8 * ThickScale), 0, Math.PI * 2);
                    Ctx.fill();

                    Ctx.fillStyle = '#f8fdff';
                    Ctx.beginPath();
                    Ctx.arc(PP[0], PP[1], Math.max(0.8, 1.4 * ThickScale), 0, Math.PI * 2);
                    Ctx.fill();
                }
            }
            Ctx.restore();
        }

        // 3b. Bokeh Stream Ribbons Debug Visualizer (Software 2D/Iso Additive Canvas Mirror)
        if (Viz.BokehRibbons && this.BokehSim?.ActiveCount > 0)
        {
            const ThickScale = Clamp(Viz.BokehThickness ?? 1.0, 0.25, 2.5);
            const EmissiveScale = Clamp(Viz.BokehEmissive ?? 1.0, 0.25, 2.5);
            const CosY = Math.cos(this.FallbackYaw), SinY = Math.sin(this.FallbackYaw);
            const CosP = Math.cos(this.FallbackPitch), SinP = Math.sin(this.FallbackPitch);
            const ToCss = (Rgb, A) => `rgba(${Math.round(Rgb[0] * 255)},${Math.round(Rgb[1] * 255)},${Math.round(Rgb[2] * 255)},${Clamp(A, 0, 1).toFixed(3)})`;

            Ctx.save();
            Ctx.globalCompositeOperation = 'lighter';
            Ctx.lineCap = 'round';
            Ctx.lineJoin = 'round';

            // Subtle base streamline guide curves so vector-field architecture stays crisp everywhere
            for (const Curve of this.BokehSim.Curves || [])
            {
                const Pts = Curve.Pts;
                if (!Pts || Pts.length < 4) continue;
                const { MidRgb, TailRgb } = ResolveBokehColors(Viz, Curve.MidMagShare, Pts[Math.floor(Pts.length * 0.5)].Y);
                const GuideRgb = [
                    TailRgb[0] * 0.6 + MidRgb[0] * 0.4,
                    TailRgb[1] * 0.6 + MidRgb[1] * 0.4,
                    TailRgb[2] * 0.6 + MidRgb[2] * 0.4
                ];
                Ctx.strokeStyle = ToCss(GuideRgb, Clamp((0.14 + 0.12 * Curve.MidMagShare) * EmissiveScale, 0.04, 0.36));
                Ctx.lineWidth = Math.max(0.75, 1.35 * ThickScale);
                Ctx.beginPath();
                const S0 = MapPt(Pts[0].X, Pts[0].Y, Pts[0].Z);
                Ctx.moveTo(S0[0], S0[1]);
                for (let K = 1; K < Pts.length; ++K)
                {
                    const Sk = MapPt(Pts[K].X, Pts[K].Y, Pts[K].Z);
                    Ctx.lineTo(Sk[0], Sk[1]);
                }
                Ctx.stroke();
            }

            const ActiveTracers = [];
            for (let I = 0; I < this.BokehSim.ActiveCount; ++I)
            {
                const TrObj = this.BokehSim.Tracers[I];
                if (TrObj.ValidLen < 2) continue;
                const Depth = IsIso
                    ? (TrObj.Y - PivotY) * SinP + ((TrObj.X - BaseFrame.PanX) * SinY + (TrObj.Z - BaseFrame.PanZ) * CosY) * CosP
                    : 0;
                ActiveTracers.push({ TrObj, Depth });
            }
            if (IsIso) ActiveTracers.sort((A, B) => A.Depth - B.Depth);

            for (const { TrObj } of ActiveTracers)
            {
                const ULife = Clamp(TrObj.Phase ?? (TrObj.Age / Math.max(0.5, TrObj.Life)), 0, 1);
                const LifeEnv = SmoothStep01(0, 0.11, ULife) * (1 - SmoothStep01(0.84, 1, ULife));
                if (LifeEnv <= 0.015) continue;

                const Alpha = Clamp(LifeEnv * (0.52 + 0.48 * TrObj.SpeedShare) * 0.68 * EmissiveScale, 0, 0.94);
                const { HeadRgb, MidRgb, TailRgb } = ResolveBokehColors(Viz, TrObj.SpeedShare, TrObj.Y);
                const ScrPts = [];
                for (let K = 0; K < TrObj.ValidLen; ++K)
                {
                    const B4 = K * 4;
                    ScrPts.push(MapPt(TrObj.Trail[B4], TrObj.Trail[B4 + 1], TrObj.Trail[B4 + 2]));
                }

                // Draw smooth curved tapered ribbon from head (idx 0) -> tail (idx ValidLen-1)
                if (ScrPts.length >= 2)
                {
                    const TailIdx = ScrPts.length - 1;
                    const LeadEnd = Math.min(TailIdx, Math.max(1, Math.floor(ScrPts.length * 0.48)));
                    const BaseW = Math.max(1.0, 3.6 * ThickScale);

                    // Outer mist teal curve
                    Ctx.strokeStyle = ToCss(MidRgb, Alpha * 0.66);
                    Ctx.lineWidth = BaseW;
                    Ctx.beginPath();
                    Ctx.moveTo(ScrPts[0][0], ScrPts[0][1]);
                    for (let K = 1; K < TailIdx; ++K)
                    {
                        const Mx = 0.5 * (ScrPts[K][0] + ScrPts[K + 1][0]);
                        const My = 0.5 * (ScrPts[K][1] + ScrPts[K + 1][1]);
                        Ctx.quadraticCurveTo(ScrPts[K][0], ScrPts[K][1], Mx, My);
                    }
                    Ctx.lineTo(ScrPts[TailIdx][0], ScrPts[TailIdx][1]);
                    Ctx.stroke();

                    // Tail tint fade overlay
                    const MidStart = Math.max(1, Math.floor(ScrPts.length * 0.52));
                    if (TailIdx > MidStart)
                    {
                        Ctx.strokeStyle = ToCss(TailRgb, Alpha * 0.36);
                        Ctx.lineWidth = Math.max(0.65, BaseW * 0.42);
                        Ctx.beginPath();
                        Ctx.moveTo(ScrPts[MidStart][0], ScrPts[MidStart][1]);
                        for (let K = MidStart + 1; K <= TailIdx; ++K) Ctx.lineTo(ScrPts[K][0], ScrPts[K][1]);
                        Ctx.stroke();
                    }

                    // Bright leading ribbon spine behind bokeh bead
                    Ctx.strokeStyle = ToCss(HeadRgb, Alpha * 0.80);
                    Ctx.lineWidth = Math.max(0.6, BaseW * 0.48);
                    Ctx.beginPath();
                    Ctx.moveTo(ScrPts[0][0], ScrPts[0][1]);
                    for (let K = 1; K <= LeadEnd; ++K) Ctx.lineTo(ScrPts[K][0], ScrPts[K][1]);
                    Ctx.stroke();
                }

                // Luminous bokeh glow bead at head (sharp focal pinpoint OR delicate aperture disc)
                const H = ScrPts[0];
                const Dof = TrObj.Dof;
                const Aperture = Math.pow(Dof, 4.2);
                const Rad = (1.8 + Aperture * 6.4) * (0.86 + 0.28 * TrObj.SizeSeed) * ThickScale;
                const BeadA = Clamp(LifeEnv * (0.64 + 0.36 * TrObj.SpeedShare) * EmissiveScale, 0, 1);

                if (Aperture > 0.32 && Ctx.createRadialGradient)
                {
                    const Grad = Ctx.createRadialGradient(H[0], H[1], 0, H[0], H[1], Rad);
                    Grad.addColorStop(0.0, ToCss(HeadRgb, BeadA * 0.52));
                    Grad.addColorStop(0.72, ToCss(HeadRgb, BeadA * 0.24));
                    Grad.addColorStop(0.90, ToCss(HeadRgb, BeadA * 0.30));
                    Grad.addColorStop(1.0, ToCss(HeadRgb, 0));
                    Ctx.fillStyle = Grad;
                    Ctx.beginPath();
                    Ctx.arc(H[0], H[1], Rad, 0, Math.PI * 2);
                    Ctx.fill();
                }
                else
                {
                    if (Rad > 1.6 && Ctx.createRadialGradient)
                    {
                        const Halo = Ctx.createRadialGradient(H[0], H[1], 0, H[0], H[1], Rad * 2.2);
                        Halo.addColorStop(0.0, ToCss(HeadRgb, BeadA * 0.62));
                        Halo.addColorStop(1.0, ToCss(HeadRgb, 0));
                        Ctx.fillStyle = Halo;
                        Ctx.beginPath();
                        Ctx.arc(H[0], H[1], Rad * 2.2, 0, Math.PI * 2);
                        Ctx.fill();
                    }
                    Ctx.fillStyle = `rgba(255,255,255,${Math.min(1, BeadA * 0.96).toFixed(3)})`;
                    Ctx.beginPath();
                    Ctx.arc(H[0], H[1], Math.max(0.9, Rad * 0.56), 0, Math.PI * 2);
                    Ctx.fill();
                }
            }
            Ctx.restore();
        }

        // 4. Full-Viewport Zoom-Adaptive Vector Arrows on EVERY active slice plane
        if (Viz.Vectors)
        {
            const BaseStep = Clamp((Viz.ArrowSpacing || 46) * ZoomRatio, 10, 120);
            const MaxPerAxis = Math.floor(640 / PaneAxes.length);
            const FixedMax = Math.max(4, Viz.FixedMax || 34);

            for (const Axis of PaneAxes)
            {
                const Frame = Frames[Axis];
                const { MinU, MaxU, MinV, MaxV } = Frame;
                const AreaM2 = Math.max(1, (MaxU - MinU) * (MaxV - MinV));
                const Step = Math.max(BaseStep, Math.sqrt(AreaM2 / MaxPerAxis));
                const StartU = Math.floor(MinU / Step) * Step;
                const StartV = Math.floor(MinV / Step) * Step;
                for (let U = StartU; U <= MaxU; U += Step)
                {
                    for (let V = StartV; V <= MaxV; V += Step)
                    {
                        const [Wx, Wy, Wz] = PlaneToWorld(Axis, U, V, OffsetM);
                        const Res = EvaluateComposite(this.SceneState, Wx, Wy, Wz, SimTime, { Solo });
                        if (Res.TotalMag < (Viz.QuietCutoff ?? 0.25)) continue;
                        const Share = Clamp(Res.TotalMag / FixedMax, 0, 1);
                        const Len = ((10 + Share * 20) * ZoomRatio) / Res.TotalMag;
                        const A = MapPt(Wx, Wy, Wz);
                        const B = MapPt(Wx + Res.TotalVec[0] * Len, Wy + Res.TotalVec[1] * Len, Wz + Res.TotalVec[2] * Len);
                        Ctx.strokeStyle = ColormapCss(Viz.Colormap, Share, 0.88);
                        Ctx.lineWidth = 1.45;
                        Ctx.beginPath();
                        Ctx.moveTo(A[0], A[1]);
                        Ctx.lineTo(B[0], B[1]);
                        Ctx.stroke();

                        const Ang = Math.atan2(B[1] - A[1], B[0] - A[0]);
                        Ctx.beginPath();
                        Ctx.moveTo(B[0], B[1]);
                        Ctx.lineTo(B[0] - Math.cos(Ang - 0.5) * 4.5, B[1] - Math.sin(Ang - 0.5) * 4.5);
                        Ctx.moveTo(B[0], B[1]);
                        Ctx.lineTo(B[0] - Math.cos(Ang + 0.5) * 4.5, B[1] - Math.sin(Ang + 0.5) * 4.5);
                        Ctx.stroke();
                    }
                }
            }
        }

        // 5. Curve / Line Motion Paths, Clickable Waypoints, Source Circle Entities & Transform Gizmo
        const FallbackHandles = [];
        const FallbackCircles = [];
        const FallbackWaypoints = [];
        for (const S of this.SceneState.Sources)
        {
            const Model = ForceModelById[S.Model];
            const Col = S.UseFormula ? '#ffb454' : (Model?.Colour || '#78d2c6');
            const IsSel = S.Token === this.SelectedToken;
            const IsHov = S.Token === this.HoveredToken;

            if (S.Motion?.Mode && S.Motion.Mode !== 'None' && S.Motion.Waypoints?.length)
            {
                const CurvePts = SampleMotionPathPoints(S, 44);
                if (CurvePts.length >= 2)
                {
                    Ctx.save();
                    Ctx.strokeStyle = IsSel ? '#ffb454' : Col;
                    Ctx.globalAlpha = IsSel ? 0.92 : 0.45;
                    Ctx.lineWidth = IsSel ? 2.2 : 1.3;
                    Ctx.setLineDash([6, 4]);
                    Ctx.beginPath();
                    CurvePts.forEach((P, Idx) => {
                        const Sp = MapPt(P[0], P[1], P[2]);
                        if (Idx === 0) Ctx.moveTo(Sp[0], Sp[1]);
                        else Ctx.lineTo(Sp[0], Sp[1]);
                    });
                    Ctx.stroke();
                    Ctx.restore();
                }
                const Wps = S.Motion.Waypoints;
                Wps.forEach((Wp, WIdx) => {
                    const Sp = MapPt(Wp[0], Wp[1], Wp[2]);
                    const IsWpSel = IsSel && this.SelectedWaypointIdx === WIdx;
                    const WCol = WIdx === 0 ? '#589979' : (WIdx === Wps.length - 1 ? '#e86b4f' : '#64b5f6');
                    const Label = WIdx === 0 ? 'A' : (WIdx === Wps.length - 1 ? 'B' : `P${WIdx}`);
                    FallbackWaypoints.push({ Token: S.Token, WaypointIdx: WIdx, X: Sp[0], Y: Sp[1] });
                    Ctx.save();
                    Ctx.fillStyle = WCol;
                    Ctx.strokeStyle = IsWpSel ? '#ffffff' : '#101418';
                    Ctx.lineWidth = IsWpSel ? 2.4 : 1.5;
                    Ctx.beginPath();
                    Ctx.arc(Sp[0], Sp[1], IsWpSel ? 7 : 5.5, 0, Math.PI * 2);
                    Ctx.fill();
                    Ctx.stroke();
                    Ctx.font = '700 9px system-ui, sans-serif';
                    Ctx.fillStyle = '#e8edf2';
                    Ctx.fillText(Label, Sp[0] + 7, Sp[1] - 6);
                    Ctx.restore();
                });
            }

            const Bc = MapPt(S.Centre[0], S.Centre[1], S.Centre[2]);
            FallbackCircles.push({ Token: S.Token, X: Bc[0], Y: Bc[1] });

            if (IsIso && Math.abs(S.Centre[1] - OffsetM) > 3)
            {
                const Gnd = MapPt(S.Centre[0], OffsetM, S.Centre[2]);
                Ctx.save();
                Ctx.strokeStyle = IsSel ? '#ffb454' : Col;
                Ctx.globalAlpha = IsSel ? 0.55 : 0.25;
                Ctx.setLineDash([3, 3]);
                Ctx.beginPath();
                Ctx.moveTo(Gnd[0], Gnd[1]);
                Ctx.lineTo(Bc[0], Bc[1]);
                Ctx.stroke();
                Ctx.restore();
            }

            const RingR = IsSel ? 13 : (IsHov ? 12 : 10);
            Ctx.save();
            Ctx.globalAlpha = S.Enabled ? 0.92 : 0.35;
            Ctx.fillStyle = Col;
            Ctx.globalAlpha = S.Enabled ? (IsSel ? 0.28 : 0.16) : 0.08;
            Ctx.beginPath();
            Ctx.arc(Bc[0], Bc[1], RingR, 0, Math.PI * 2);
            Ctx.fill();
            Ctx.globalAlpha = S.Enabled ? 0.96 : 0.4;
            Ctx.strokeStyle = IsSel ? '#ffb454' : Col;
            Ctx.lineWidth = IsSel ? 2.4 : (IsHov ? 2.0 : 1.5);
            Ctx.beginPath();
            Ctx.arc(Bc[0], Bc[1], RingR, 0, Math.PI * 2);
            Ctx.stroke();
            Ctx.fillStyle = Col;
            Ctx.beginPath();
            Ctx.arc(Bc[0], Bc[1], IsSel ? 4.5 : 3.5, 0, Math.PI * 2);
            Ctx.fill();
            Ctx.restore();
        }

        const SelSrc = this.SceneState.Sources.find((S) => S.Token === this.SelectedToken);
        if (SelSrc)
        {
            const ActiveWp = (this.SelectedWaypointIdx != null && SelSrc.Motion?.Waypoints?.[this.SelectedWaypointIdx])
                ? SelSrc.Motion.Waypoints[this.SelectedWaypointIdx]
                : null;
            const C = ActiveWp || SelSrc.Centre;
            const O = MapPt(C[0], C[1], C[2]);
            const StepM = Math.max(24, (DEFAULT_HALF_M / Math.max(0.25, this.FallbackZoom)) * 0.16);
            const AxesCfg = [
                { Axis: 'X', Dir: [1, 0, 0], Col: '#ff5e5b', Label: 'X' },
                { Axis: 'Y', Dir: [0, 1, 0], Col: '#56e39f', Label: 'Y' },
                { Axis: 'Z', Dir: [0, 0, 1], Col: '#5aa9ff', Label: 'Z' }
            ];

            Ctx.save();
            const PlaneHover = this.HoveredGizmoAxis === 'XZ';
            Ctx.strokeStyle = PlaneHover ? '#ffe082' : '#ffc857';
            Ctx.lineWidth = PlaneHover ? 2.8 : 2.0;
            Ctx.beginPath();
            Ctx.arc(O[0], O[1], 18, 0, Math.PI * 2);
            Ctx.stroke();
            FallbackHandles.push({ Axis: 'XZ', X: O[0], Y: O[1], DirScr: [1, 0] });

            for (const Item of AxesCfg)
            {
                const Tip = MapPt(C[0] + Item.Dir[0] * StepM, C[1] + Item.Dir[1] * StepM, C[2] + Item.Dir[2] * StepM);
                const Dx = Tip[0] - O[0], Dy = Tip[1] - O[1];
                const Len = Math.hypot(Dx, Dy);
                if (Len < 8) continue;
                const NormLen = Math.min(44, Math.max(28, Len));
                const Ux = Dx / Len, Uy = Dy / Len;
                const Ex = O[0] + Ux * NormLen, Ey = O[1] + Uy * NormLen;
                const IsHov = this.HoveredGizmoAxis === Item.Axis;

                Ctx.strokeStyle = IsHov ? '#ffffff' : Item.Col;
                Ctx.fillStyle = IsHov ? '#ffffff' : Item.Col;
                Ctx.lineWidth = IsHov ? 3.2 : 2.3;
                Ctx.beginPath();
                Ctx.moveTo(O[0] + Ux * 10, O[1] + Uy * 10);
                Ctx.lineTo(Ex, Ey);
                Ctx.stroke();

                const Ang = Math.atan2(Uy, Ux);
                Ctx.beginPath();
                Ctx.moveTo(Ex + Math.cos(Ang) * 7, Ey + Math.sin(Ang) * 7);
                Ctx.lineTo(Ex - Math.cos(Ang - 0.55) * 6, Ey - Math.sin(Ang - 0.55) * 6);
                Ctx.lineTo(Ex - Math.cos(Ang + 0.55) * 6, Ey - Math.sin(Ang + 0.55) * 6);
                Ctx.closePath();
                Ctx.fill();

                Ctx.font = '600 10px system-ui, sans-serif';
                Ctx.fillText(Item.Label, Ex + Ux * 10 - 3, Ey + Uy * 10 + 3);
                FallbackHandles.push({ Axis: Item.Axis, X: Ex, Y: Ey, Ox: O[0], Oy: O[1], DirScr: [Ux, Uy], PxPerM: NormLen / StepM });
            }
            Ctx.restore();
        }
        this.FallbackPickState[IsIso ? 'iso' : 'ortho'] = {
            Handles: FallbackHandles,
            Circles: FallbackCircles,
            Waypoints: FallbackWaypoints
        };

        // Probe Crosshair
        const Pr = this.SceneState.Probe;
        const PS = MapPt(Pr[0], Pr[1], Pr[2]);
        Ctx.strokeStyle = '#ffb454';
        Ctx.lineWidth = 2;
        Ctx.beginPath();
        Ctx.arc(PS[0], PS[1], 7, 0, Math.PI * 2);
        Ctx.stroke();
        Ctx.fillStyle = '#ffffff';
        Ctx.beginPath();
        Ctx.arc(PS[0], PS[1], 2.5, 0, Math.PI * 2);
        Ctx.fill();

        Ctx.restore();
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                    WEBGL2 PIPELINE SETUP & STAGED SHADER COMPILATION
    //------------------------------------------------------------------------------------------------------------------------

    TestRenderableFBO(TexType)
    {
        const GL = this.Renderer.getContext();
        if (!GL) return false;
        const TestRT = new THREE.WebGLRenderTarget(4, 4, {
            type: TexType,
            format: THREE.RGBAFormat,
            minFilter: THREE.NearestFilter,
            magFilter: THREE.NearestFilter,
            depthBuffer: false,
            stencilBuffer: false
        });
        let Ok = false;
        try
        {
            const Prev = this.Renderer.getRenderTarget();
            this.Renderer.setRenderTarget(TestRT);
            Ok = GL.checkFramebufferStatus(GL.FRAMEBUFFER) === GL.FRAMEBUFFER_COMPLETE;
            this.Renderer.setRenderTarget(Prev);
        }
        catch { Ok = false; }
        TestRT.dispose();
        return Ok;
    }

    ProbeGpuCapabilities()
    {
        const GL = this.Renderer.getContext();
        const IsWebGL2 = typeof WebGL2RenderingContext !== 'undefined' && GL instanceof WebGL2RenderingContext;
        const HasFloatExt = Boolean(GL && (GL.getExtension('EXT_color_buffer_float') || GL.getExtension('EXT_color_buffer_half_float')));
        const HasFloatLinear = Boolean(GL && GL.getExtension('OES_texture_float_linear'));
        const MaxVertTex = GL ? GL.getParameter(GL.MAX_VERTEX_TEXTURE_IMAGE_UNITS) : 0;

        let BakeType = THREE.UnsignedByteType;
        let BakeMode = 1;
        if (IsWebGL2 && HasFloatExt)
        {
            if (this.TestRenderableFBO(THREE.HalfFloatType))
            {
                BakeType = THREE.HalfFloatType;
                BakeMode = 0;
            }
            else if (HasFloatLinear && this.TestRenderableFBO(THREE.FloatType))
            {
                BakeType = THREE.FloatType;
                BakeMode = 0;
            }
        }

        this.BakeTexType = BakeType;
        this.SharedUniforms.uBakeMode.value = BakeMode;
        this.GpuInfo = {
            IsWebGL2,
            Renderer: 'WebGL2 GPU',
            BakeMode: BakeMode === 0 ? 'Float16 FBO (128×128)' : 'RGBA8 Normalized (128×128)',
            StreamMode: 'Optical Ribbon Trails',
            MaxVertTex,
            Stages: []
        };
    }

    BuildLightingAndGround()
    {
        this.ThreeScene.add(new THREE.AmbientLight(0x8fa3b3, 0.72));
        const Key = new THREE.DirectionalLight(0xfff4e0, 1.15);
        Key.position.set(42, 70, 32);
        this.ThreeScene.add(Key);

        const Grid = new THREE.GridHelper(160, 40, 0x2f3b43, 0x1a2024);
        Grid.position.y = -0.04;
        this.ThreeScene.add(Grid);

        const NorthMat = new THREE.LineBasicMaterial({ color: 0x589979, transparent: true, opacity: 0.55 });
        const NorthGeo = new THREE.BufferGeometry().setFromPoints([
            new THREE.Vector3(0, 0.02, 0),
            new THREE.Vector3(0, 0.02, -55)
        ]);
        this.ThreeScene.add(new THREE.Line(NorthGeo, NorthMat));
    }

    BuildGpuSliceBakeAndDisplay()
    {
        this.BakeCenters = [
            new THREE.Vector2(0, 0),
            new THREE.Vector2(0, 48),
            new THREE.Vector2(0, 48)
        ];
        this.FieldBakeRTs = [0, 1, 2].map(() => new THREE.WebGLRenderTarget(BAKE_RES, BAKE_RES, {
            type: this.BakeTexType,
            format: THREE.RGBAFormat,
            minFilter: THREE.LinearFilter,
            magFilter: THREE.LinearFilter,
            depthBuffer: false,
            stencilBuffer: false
        }));
        this.FieldBakeRT = this.FieldBakeRTs[0];
        this.SharedUniforms.uFieldBakeTex.value = this.FieldBakeRTs[0].texture;

        this.BakeScene = new THREE.Scene();
        this.BakeCamera = new THREE.OrthographicCamera(-1, 1, 1, -1, 0, 1);
        this.BakeMat = new THREE.ShaderMaterial({
            glslVersion: THREE.GLSL3,
            vertexShader: SLICE_BAKE_VERT,
            fragmentShader: SLICE_BAKE_FRAG,
            uniforms: this.SharedUniforms,
            depthTest: false,
            depthWrite: false
        });
        this.BakeScene.add(new THREE.Mesh(FBO_QUAD_GEO, this.BakeMat));

        const PlaneGeo = new THREE.PlaneGeometry(2, 2, 1, 1);
        this.HeatmapMats = [0, 1, 2].map((AxisId) => new THREE.ShaderMaterial({
            glslVersion: THREE.GLSL3,
            vertexShader: SLICE_DISPLAY_VERT,
            fragmentShader: SLICE_DISPLAY_FRAG,
            uniforms: {
                ...this.SharedUniforms,
                uFieldBakeTex: { value: this.FieldBakeRTs[AxisId].texture },
                uBakeCenter: { value: this.BakeCenters[AxisId] },
                uSliceAxis: { value: AxisId }
            },
            transparent: true,
            depthWrite: false,
            side: THREE.DoubleSide
        }));
        this.HeatmapMat = this.HeatmapMats[0];
        this.HeatmapMeshes = this.HeatmapMats.map((Mat) => {
            const M = new THREE.Mesh(PlaneGeo, Mat);
            M.rotation.x = -Math.PI / 2;
            M.scale.set(DEFAULT_HALF_M * WORLD_SCALE, DEFAULT_HALF_M * WORLD_SCALE, 1);
            M.position.y = 16 * WORLD_SCALE;
            M.visible = false;
            this.ThreeScene.add(M);
            return M;
        });
        this.HeatmapMesh = this.HeatmapMeshes[0];
        this.HeatmapMeshes[0].visible = true;
    }

    BuildGpuArrows()
    {
        this.ArrowSlots = [0, 1, 2].map((AxisId) => {
            const Geo = BuildArrowTemplateGeometry();
            const Seeds = new THREE.InstancedBufferAttribute(new Float32Array(2500 * 3), 3);
            Geo.setAttribute('aSeedMetres', Seeds);
            Geo.instanceCount = 0;

            const Mat = new THREE.ShaderMaterial({
                glslVersion: THREE.GLSL3,
                vertexShader: GPU_ARROWS_VERT,
                fragmentShader: GPU_ARROWS_FRAG,
                uniforms: {
                    ...this.SharedUniforms,
                    uFieldBakeTex: { value: this.FieldBakeRTs[AxisId].texture },
                    uBakeCenter: { value: this.BakeCenters[AxisId] },
                    uSliceAxis: { value: AxisId }
                },
                transparent: true
            });
            const Mesh = new THREE.Mesh(Geo, Mat);
            Mesh.frustumCulled = false;
            Mesh.visible = false;
            this.ThreeScene.add(Mesh);
            return { AxisId, Geo, Seeds, Mat, Mesh };
        });
        this.ArrowGeo = this.ArrowSlots[0].Geo;
        this.ArrowSeeds = this.ArrowSlots[0].Seeds;
        this.ArrowMat = this.ArrowSlots[0].Mat;
        this.ArrowMesh = this.ArrowSlots[0].Mesh;
    }

    BuildGpuStreamlines()
    {
        this.StreamMat = new THREE.ShaderMaterial({
            glslVersion: THREE.GLSL3,
            vertexShader: STREAMLINE_VERT,
            fragmentShader: STREAMLINE_FRAG,
            uniforms: {
                uTime: this.SharedUniforms.uTime,
                uViewportHeightPx: this.SharedUniforms.uViewportHeightPx,
                uStreamThickness: this.SharedUniforms.uStreamThickness,
                uStreamEmissive: this.SharedUniforms.uStreamEmissive,
                uStreamOpacity: this.SharedUniforms.uStreamOpacity
            },
            transparent: true,
            depthWrite: false,
            blending: THREE.NormalBlending,
            side: THREE.DoubleSide
        });
        const InitGeo = BuildStreamlineRibbonGeometry([], 'Engineering', 34);
        this.StreamMesh = new THREE.Mesh(InitGeo, this.StreamMat);
        this.StreamMesh.frustumCulled = false;
        this.StreamMesh.renderOrder = 4;
        this.ThreeScene.add(this.StreamMesh);
    }

    BuildGpuBokehRibbons()
    {
        const MaxRibVerts = BOKEH_MAX_TRACERS * (BOKEH_TRAIL_SEGS - 1) * 6 + 64000;
        const RibGeo = new THREE.BufferGeometry();
        RibGeo.setAttribute('position', new THREE.BufferAttribute(new Float32Array(MaxRibVerts * 3), 3));
        RibGeo.setAttribute('aTangent', new THREE.BufferAttribute(new Float32Array(MaxRibVerts * 3), 3));
        RibGeo.setAttribute('aColor', new THREE.BufferAttribute(new Float32Array(MaxRibVerts * 3), 3));
        RibGeo.setAttribute('aUv', new THREE.BufferAttribute(new Float32Array(MaxRibVerts * 2), 2));
        RibGeo.setAttribute('aAlpha', new THREE.BufferAttribute(new Float32Array(MaxRibVerts), 1));
        RibGeo.setDrawRange(0, 0);

        this.BokehRibbonMat = new THREE.ShaderMaterial({
            glslVersion: THREE.GLSL3,
            vertexShader: BOKEH_RIBBON_VERT,
            fragmentShader: BOKEH_RIBBON_FRAG,
            uniforms: {
                uViewportHeightPx: this.SharedUniforms.uViewportHeightPx,
                uBokehThickness: this.SharedUniforms.uBokehThickness,
                uBokehEmissive: this.SharedUniforms.uBokehEmissive
            },
            transparent: true,
            depthWrite: false,
            blending: THREE.AdditiveBlending,
            side: THREE.DoubleSide
        });
        this.BokehRibbonGeo = RibGeo;
        this.BokehRibbonMesh = new THREE.Mesh(RibGeo, this.BokehRibbonMat);
        this.BokehRibbonMesh.frustumCulled = false;
        this.BokehRibbonMesh.renderOrder = 5;
        this.ThreeScene.add(this.BokehRibbonMesh);

        const MaxBeadVerts = BOKEH_MAX_TRACERS * 6;
        const BeadGeo = new THREE.BufferGeometry();
        BeadGeo.setAttribute('position', new THREE.BufferAttribute(new Float32Array(MaxBeadVerts * 3), 3));
        BeadGeo.setAttribute('aCorner', new THREE.BufferAttribute(new Float32Array(MaxBeadVerts * 2), 2));
        BeadGeo.setAttribute('aColor', new THREE.BufferAttribute(new Float32Array(MaxBeadVerts * 3), 3));
        BeadGeo.setAttribute('aDof', new THREE.BufferAttribute(new Float32Array(MaxBeadVerts), 1));
        BeadGeo.setAttribute('aRadiusPx', new THREE.BufferAttribute(new Float32Array(MaxBeadVerts), 1));
        BeadGeo.setAttribute('aAlpha', new THREE.BufferAttribute(new Float32Array(MaxBeadVerts), 1));
        BeadGeo.setDrawRange(0, 0);

        this.BokehBeadMat = new THREE.ShaderMaterial({
            glslVersion: THREE.GLSL3,
            vertexShader: BOKEH_BEAD_VERT,
            fragmentShader: BOKEH_BEAD_FRAG,
            uniforms: {
                uViewportHeightPx: this.SharedUniforms.uViewportHeightPx,
                uBokehThickness: this.SharedUniforms.uBokehThickness,
                uBokehEmissive: this.SharedUniforms.uBokehEmissive
            },
            transparent: true,
            depthWrite: false,
            blending: THREE.AdditiveBlending,
            side: THREE.DoubleSide
        });
        this.BokehBeadGeo = BeadGeo;
        this.BokehBeadMesh = new THREE.Mesh(BeadGeo, this.BokehBeadMat);
        this.BokehBeadMesh.frustumCulled = false;
        this.BokehBeadMesh.renderOrder = 6;
        this.ThreeScene.add(this.BokehBeadMesh);
    }

    BuildBeaconsAndProbe()
    {
        this.BoundsGroup = new THREE.Group();
        this.BeaconGroup = new THREE.Group();
        this.MotionPathGroup = new THREE.Group();
        this.ThreeScene.add(this.BoundsGroup);
        this.ThreeScene.add(this.MotionPathGroup);
        this.ThreeScene.add(this.BeaconGroup);
        this.BeaconHandles = [];
        this.WaypointHandles = [];
        this.BuildGizmoGroup();

        this.ProbeGroup = new THREE.Group();
        const Ring = new THREE.Mesh(
            new THREE.RingGeometry(0.55, 0.72, 32),
            new THREE.MeshBasicMaterial({ color: 0xffb454, side: THREE.DoubleSide })
        );
        Ring.rotation.x = -Math.PI / 2;
        const Pillar = new THREE.Mesh(
            new THREE.CylinderGeometry(0.06, 0.06, 3.2, 10),
            new THREE.MeshBasicMaterial({ color: 0xffb454, transparent: true, opacity: 0.85 })
        );
        const Core = new THREE.Mesh(
            new THREE.OctahedronGeometry(0.28, 0),
            new THREE.MeshBasicMaterial({ color: 0xffffff })
        );
        this.ProbeGroup.add(Ring, Pillar, Core);
        this.ThreeScene.add(this.ProbeGroup);
    }

    BuildGizmoGroup()
    {
        this.GizmoGroup = new THREE.Group();
        this.GizmoGroup.visible = false;
        this.GizmoAxesGroup = new THREE.Group();
        this.GizmoGroup.add(this.GizmoAxesGroup);
        this.GizmoHandles = [];
        this.GizmoVisualsByAxis = {};

        const AddAxis = (AxisName, DirVec, BaseHex) => {
            const AxisGrp = new THREE.Group();
            const ShaftMat = new THREE.MeshBasicMaterial({
                color: BaseHex, transparent: true, opacity: 0.92, depthTest: false, depthWrite: false
            });
            const HeadMat = new THREE.MeshBasicMaterial({
                color: BaseHex, transparent: true, opacity: 0.98, depthTest: false, depthWrite: false
            });
            const Shaft = new THREE.Mesh(new THREE.CylinderGeometry(0.065, 0.065, 3.2, 12), ShaftMat);
            Shaft.position.y = 1.65;
            Shaft.renderOrder = 14;
            const Cone = new THREE.Mesh(new THREE.ConeGeometry(0.24, 0.72, 16), HeadMat);
            Cone.position.y = 3.55;
            Cone.renderOrder = 14;

            const HitMat = new THREE.MeshBasicMaterial({
                transparent: true, opacity: 0.0, depthTest: false, depthWrite: false
            });
            const HitCollar = new THREE.Mesh(new THREE.CylinderGeometry(0.42, 0.42, 4.2, 10), HitMat);
            HitCollar.position.y = 2.15;
            HitCollar.userData = { GizmoAxis: AxisName };
            Shaft.userData = { GizmoAxis: AxisName };
            Cone.userData = { GizmoAxis: AxisName };

            AxisGrp.add(Shaft, Cone, HitCollar);
            if (AxisName === 'X') AxisGrp.rotation.z = -Math.PI / 2;
            else if (AxisName === 'Z') AxisGrp.rotation.x = Math.PI / 2;

            this.GizmoAxesGroup.add(AxisGrp);
            this.GizmoHandles.push(HitCollar, Cone, Shaft);
            this.GizmoVisualsByAxis[AxisName] = { BaseHex, Mats: [ShaftMat, HeadMat], BaseOpacity: 0.94 };
        };

        AddAxis('X', [1, 0, 0], 0xff5e5b);
        AddAxis('Y', [0, 1, 0], 0x56e39f);
        AddAxis('Z', [0, 0, 1], 0x5aa9ff);

        // Central XZ Plane Move Ring + Soft Interactive Disc
        const PlaneRingMat = new THREE.MeshBasicMaterial({
            color: 0xffc857, transparent: true, opacity: 0.95, depthTest: false, depthWrite: false
        });
        const PlaneRing = new THREE.Mesh(new THREE.TorusGeometry(1.75, 0.075, 12, 48), PlaneRingMat);
        PlaneRing.rotation.x = Math.PI / 2;
        PlaneRing.renderOrder = 14;
        PlaneRing.userData = { GizmoAxis: 'XZ' };

        const PlaneDiscMat = new THREE.MeshBasicMaterial({
            color: 0xffc857, transparent: true, opacity: 0.16, side: THREE.DoubleSide, depthTest: false, depthWrite: false
        });
        const PlaneDisc = new THREE.Mesh(new THREE.CircleGeometry(1.68, 36), PlaneDiscMat);
        PlaneDisc.rotation.x = -Math.PI / 2;
        PlaneDisc.renderOrder = 13;
        PlaneDisc.userData = { GizmoAxis: 'XZ' };

        this.GizmoAxesGroup.add(PlaneRing, PlaneDisc);
        this.GizmoHandles.push(PlaneRing, PlaneDisc);
        this.GizmoVisualsByAxis.XZ = { BaseHex: 0xffc857, Mats: [PlaneRingMat, PlaneDiscMat], BaseOpacity: 0.95, DiscMat: PlaneDiscMat };

        // Vertical drop stem down to active horizontal slice height
        const StemGeo = new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, -1, 0)]);
        const StemMat = new THREE.LineBasicMaterial({ color: 0xffb454, transparent: true, opacity: 0.55 });
        this.GizmoStemLine = new THREE.Line(StemGeo, StemMat);
        this.GizmoStemLine.renderOrder = 11;
        this.GizmoGroup.add(this.GizmoStemLine);

        this.ThreeScene.add(this.GizmoGroup);
    }

    UpdateGizmoPlacement()
    {
        if (this.FallbackMode || !this.GizmoGroup || !this.SceneState) return;
        const Src = this.SceneState.Sources.find((S) => S.Token === this.SelectedToken);
        if (!Src)
        {
            this.GizmoGroup.visible = false;
            return;
        }
        const Wp = (this.SelectedWaypointIdx != null && Src.Motion?.Waypoints?.[this.SelectedWaypointIdx])
            ? Src.Motion.Waypoints[this.SelectedWaypointIdx]
            : null;
        const Target = Wp || Src.Centre;
        const Cx = Target[0] * WORLD_SCALE;
        const Cy = Target[1] * WORLD_SCALE;
        const Cz = Target[2] * WORLD_SCALE;
        this.GizmoGroup.visible = true;
        this.GizmoGroup.position.set(Cx, Cy, Cz);

        const SliceY = (this.SceneState.Viz?.SliceHeight ?? 16) * WORLD_SCALE;
        const DeltaY = SliceY - Cy;
        const PosAttr = this.GizmoStemLine.geometry.getAttribute('position');
        PosAttr.setXYZ(0, 0, 0, 0);
        PosAttr.setXYZ(1, 0, DeltaY, 0);
        PosAttr.needsUpdate = true;
        this.GizmoStemLine.visible = Math.abs(DeltaY) > 0.08;
    }

    UpdateGizmoScreenScale(Cam)
    {
        if (!this.GizmoGroup?.visible || !this.GizmoAxesGroup || !Cam) return;
        let Scale = 1.0;
        if (Cam.isOrthographicCamera)
        {
            Scale = Clamp(this.OrthoZoomMetres * WORLD_SCALE * 0.085, 0.72, 4.2);
        }
        else
        {
            const Dist = this.GizmoGroup.position.distanceTo(Cam.position);
            Scale = Clamp(Dist * 0.068, 0.72, 4.2);
        }
        this.GizmoAxesGroup.scale.setScalar(Scale);
    }

    SetHoverHighlight(GizmoAxis, HoverToken)
    {
        if (this.HoveredGizmoAxis === GizmoAxis && this.HoveredToken === HoverToken) return;
        this.HoveredGizmoAxis = GizmoAxis;
        this.HoveredToken = HoverToken;

        if (!this.FallbackMode && this.GizmoVisualsByAxis)
        {
            for (const [Axis, Info] of Object.entries(this.GizmoVisualsByAxis))
            {
                const Active = Axis === GizmoAxis;
                Info.Mats[0].color.setHex(Active ? 0xffffff : Info.BaseHex);
                Info.Mats[0].opacity = Active ? 1.0 : Info.BaseOpacity;
                if (Info.Mats[1])
                {
                    if (Info.DiscMat) Info.DiscMat.opacity = Active ? 0.34 : 0.16;
                    else
                    {
                        Info.Mats[1].color.setHex(Active ? 0xffffff : Info.BaseHex);
                        Info.Mats[1].opacity = Active ? 1.0 : Info.BaseOpacity;
                    }
                }
            }
            for (const [Tok, Vis] of this.SourceVisuals.entries())
            {
                const IsSel = Tok === this.SelectedToken;
                const IsHov = Tok === HoverToken;
                if (Vis.CircleRing) Vis.CircleRing.material.opacity = IsSel ? 1.0 : (IsHov ? 0.95 : (Vis.Source.Enabled ? 0.78 : 0.32));
                if (Vis.CircleDisc) Vis.CircleDisc.material.opacity = IsSel ? 0.32 : (IsHov ? 0.28 : (Vis.Source.Enabled ? 0.14 : 0.05));
            }
        }
        else
        {
            this.FallbackHeatDirty = true;
        }
    }

    SetSelectedToken(Token, WaypointIdx = null)
    {
        this.SelectedToken = Token;
        this.SelectedWaypointIdx = WaypointIdx;
        this.FallbackHeatDirty = true;
        if (!this.FallbackMode)
        {
            this.RebuildSourceBeacons();
            this.UpdateGizmoPlacement();
        }
    }

    SetSelectedWaypoint(Token, WaypointIdx)
    {
        this.SelectedToken = Token;
        this.SelectedWaypointIdx = WaypointIdx;
        this.FallbackHeatDirty = true;
        if (!this.FallbackMode)
        {
            this.RebuildMotionPaths();
            this.UpdateGizmoPlacement();
        }
    }

    SyncSourceMoved(Token, WaypointChanged = false)
    {
        this.UniformsDirty = true;
        this.BakeDirty = true;
        this.FallbackHeatDirty = true;
        this.MarkStreamlinesDirty(false);

        if (!this.FallbackMode)
        {
            const Vis = this.SourceVisuals.get(Token);
            if (Vis)
            {
                const S = Vis.Source;
                const Cx = S.Centre[0] * WORLD_SCALE;
                const Cy = S.Centre[1] * WORLD_SCALE;
                const Cz = S.Centre[2] * WORLD_SCALE;
                const BeaconY = Math.abs(Cy) < 0.15 ? 0.15 : Cy;

                if (Vis.Beacon) Vis.Beacon.position.set(Cx, BeaconY, Cz);
                if (Vis.CircleRing) Vis.CircleRing.position.set(Cx, BeaconY, Cz);
                if (Vis.CircleDisc) Vis.CircleDisc.position.set(Cx, BeaconY, Cz);
                if (Vis.PickTarget) Vis.PickTarget.position.set(Cx, BeaconY, Cz);
                if (Vis.BoundMesh) Vis.BoundMesh.position.set(Cx, Cy + Vis.BoundYOffset, Cz);
                if (Vis.WaveSphere) Vis.WaveSphere.position.set(Cx, BeaconY, Cz);
            }
            if (WaypointChanged) this.RebuildMotionPaths();
            this.UpdateGizmoPlacement();
            this.SyncUniforms(this.LastSimTime, true);
        }
    }

    SyncAnimatedSourcesFrame(ChangedTokens = [])
    {
        if (!ChangedTokens.length) return;
        this.UniformsDirty = true;
        this.BakeDirty = true;
        this.FallbackHeatDirty = true;
        this.MarkStreamlinesDirty(false);

        if (!this.FallbackMode)
        {
            for (const Token of ChangedTokens)
            {
                const Vis = this.SourceVisuals.get(Token);
                if (!Vis) continue;
                const S = Vis.Source;
                const Cx = S.Centre[0] * WORLD_SCALE;
                const Cy = S.Centre[1] * WORLD_SCALE;
                const Cz = S.Centre[2] * WORLD_SCALE;
                const BeaconY = Math.abs(Cy) < 0.15 ? 0.15 : Cy;

                if (Vis.Beacon) Vis.Beacon.position.set(Cx, BeaconY, Cz);
                if (Vis.CircleRing) Vis.CircleRing.position.set(Cx, BeaconY, Cz);
                if (Vis.CircleDisc) Vis.CircleDisc.position.set(Cx, BeaconY, Cz);
                if (Vis.PickTarget) Vis.PickTarget.position.set(Cx, BeaconY, Cz);
                if (Vis.BoundMesh) Vis.BoundMesh.position.set(Cx, Cy + Vis.BoundYOffset, Cz);
                if (Vis.WaveSphere) Vis.WaveSphere.position.set(Cx, BeaconY, Cz);
            }
            this.UpdateGizmoPlacement();
            this.SyncUniforms(this.LastSimTime, true);
        }
    }

    RebuildMotionPaths()
    {
        if (this.FallbackMode || !this.MotionPathGroup || !this.SceneState) return;
        while (this.MotionPathGroup.children.length)
        {
            const C = this.MotionPathGroup.children.pop();
            C.geometry?.dispose();
            C.material?.dispose();
        }
        this.WaypointHandles = [];
        const SliceY = (this.SceneState.Viz?.SliceHeight ?? 16) * WORLD_SCALE;

        for (const Source of this.SceneState.Sources)
        {
            if (!Source.Motion?.Mode || Source.Motion.Mode === 'None' || !Source.Motion.Waypoints?.length) continue;
            const Model = ForceModelById[Source.Model];
            const IsSel = Source.Token === this.SelectedToken;
            const BaseHex = Source.UseFormula ? 0xffb454 : (new THREE.Color(Model?.Colour || '#78d2c6')).getHex();

            const CurvePts = SampleMotionPathPoints(Source, 48).map(
                (P) => new THREE.Vector3(P[0] * WORLD_SCALE, P[1] * WORLD_SCALE, P[2] * WORLD_SCALE)
            );
            if (CurvePts.length >= 2)
            {
                const LineGeo = new THREE.BufferGeometry().setFromPoints(CurvePts);
                const LineMat = new THREE.LineBasicMaterial({
                    color: IsSel ? 0xffb454 : BaseHex,
                    transparent: true,
                    opacity: IsSel ? 0.94 : 0.45,
                    depthWrite: false
                });
                const CurveLine = new THREE.Line(LineGeo, LineMat);
                CurveLine.renderOrder = 8;
                this.MotionPathGroup.add(CurveLine);
            }

            const Wps = Source.Motion.Waypoints;
            Wps.forEach((Wp, WIdx) => {
                const Wx = Wp[0] * WORLD_SCALE, Wy = Wp[1] * WORLD_SCALE, Wz = Wp[2] * WORLD_SCALE;
                const IsWpSel = IsSel && this.SelectedWaypointIdx === WIdx;
                const WCol = WIdx === 0 ? 0x589979 : (WIdx === Wps.length - 1 ? 0xe86b4f : 0x64b5f6);

                const Node = new THREE.Mesh(
                    new THREE.SphereGeometry(IsWpSel ? 0.56 : 0.40, 16, 12),
                    new THREE.MeshBasicMaterial({ color: IsWpSel ? 0xffc857 : WCol, transparent: true, opacity: IsSel ? 0.98 : 0.72 })
                );
                Node.position.set(Wx, Wy, Wz);
                Node.renderOrder = 10;
                Node.userData = { Token: Source.Token, WaypointIdx: WIdx };
                this.MotionPathGroup.add(Node);

                const Collar = new THREE.Mesh(
                    new THREE.RingGeometry(0.55, 0.72, 24),
                    new THREE.MeshBasicMaterial({
                        color: IsWpSel ? 0xffffff : WCol,
                        transparent: true,
                        opacity: IsWpSel ? 0.95 : (IsSel ? 0.65 : 0.3),
                        side: THREE.DoubleSide,
                        depthWrite: false
                    })
                );
                Collar.rotation.x = -Math.PI / 2;
                Collar.position.set(Wx, Wy, Wz);
                Collar.renderOrder = 9;
                Collar.userData = { Token: Source.Token, WaypointIdx: WIdx };
                this.MotionPathGroup.add(Collar);

                const Hit = new THREE.Mesh(
                    new THREE.SphereGeometry(1.15, 10, 8),
                    new THREE.MeshBasicMaterial({ transparent: true, opacity: 0.0, depthWrite: false })
                );
                Hit.position.set(Wx, Wy, Wz);
                Hit.userData = { Token: Source.Token, WaypointIdx: WIdx };
                this.MotionPathGroup.add(Hit);

                if (Math.abs(Wy - SliceY) > 0.12)
                {
                    const StemGeo = new THREE.BufferGeometry().setFromPoints([
                        new THREE.Vector3(Wx, Wy, Wz),
                        new THREE.Vector3(Wx, SliceY, Wz)
                    ]);
                    const Stem = new THREE.Line(StemGeo, new THREE.LineBasicMaterial({ color: WCol, transparent: true, opacity: 0.35 }));
                    Stem.renderOrder = 7;
                    this.MotionPathGroup.add(Stem);
                }

                this.WaypointHandles.push(Hit, Node, Collar);
            });
        }
    }

    RecompileBakeShaderIfNeeded(Pack)
    {
        if (this.FallbackMode || !this.ShadersReady) return;
        const Key = Pack.CustomKey || '';
        if (Key === this.LastCustomKey) return;

        const T0 = performance.now();
        try
        {
            const FragSrc = BuildSliceBakeFrag(Pack.CustomSlots || []);
            this.BakeMat.fragmentShader = FragSrc;
            this.BakeMat.needsUpdate = true;
            this.Renderer.compile(this.BakeScene, this.BakeCamera);
            this.LastCustomKey = Key;
            this.BakeDirty = true;
            this.Callbacks.OnFormulaGpuCompiled?.({
                Ok: true,
                Slots: Pack.CustomSlots?.length || 0,
                Ms: performance.now() - T0
            });
        }
        catch (Err)
        {
            this.Callbacks.OnFormulaGpuCompiled?.({
                Ok: false,
                Slots: Pack.CustomSlots?.length || 0,
                Ms: performance.now() - T0,
                Error: String(Err?.message || Err)
            });
        }
    }

    StartShaderCompilation()
    {
        this.ShadersReady = false;
        this.GpuInfo.Stages = [];

        const CompileSoloMat = (MeshOrMat, Kind = 'quad') => {
            const TempScene = new THREE.Scene();
            if (Kind === 'instanced')
            {
                const Slot = this.ArrowSlots[0];
                const PrevInst = Slot.Geo.instanceCount;
                Slot.Geo.instanceCount = Math.max(1, PrevInst);
                TempScene.add(new THREE.Mesh(Slot.Geo, Slot.Mat));
                this.Renderer.compile(TempScene, this.PerspCamera);
                Slot.Geo.instanceCount = PrevInst;
            }
            else if (Kind === 'stream')
            {
                TempScene.add(new THREE.Mesh(this.StreamMesh.geometry, this.StreamMat));
                TempScene.add(new THREE.Mesh(this.BokehRibbonMesh.geometry, this.BokehRibbonMat));
                TempScene.add(new THREE.Mesh(this.BokehBeadMesh.geometry, this.BokehBeadMat));
                this.Renderer.compile(TempScene, this.PerspCamera);
            }
            else if (MeshOrMat.isScene)
            {
                this.Renderer.compile(MeshOrMat, this.BakeCamera);
            }
            else
            {
                TempScene.add(new THREE.Mesh(FBO_QUAD_GEO, MeshOrMat));
                this.Renderer.compile(TempScene, this.BakeCamera);
            }
        };

        const Steps = [
            { Label: 'WebGL2 Capabilities & Render Targets', Fn: () => { if (!this.Renderer.getContext()) throw new Error('WebGL2 unavailable'); } },
            { Label: '128×128 Slice Field Bake + Formula Shader', Fn: () => CompileSoloMat(this.BakeScene) },
            { Label: 'GPU Heatmap LUT & Isoline Display Shader', Fn: () => CompileSoloMat(this.HeatmapMats[0]) },
            { Label: 'GPU Vector Arrows & Ribbon Streamline Shaders', Fn: () => { CompileSoloMat(this.ArrowSlots[0].Mat, 'instanced'); CompileSoloMat(this.StreamMat, 'stream'); } }
        ];

        let Idx = 0;
        const RunNext = () => {
            if (this.ContextLost || this.FallbackMode) return;
            if (Idx >= Steps.length)
            {
                this.ShadersReady = true;
                this.BakeDirty = true;
                if (this.SceneState) this.SyncUniforms(this.LastSimTime, true);
                this.Callbacks.OnCompileProgress?.({
                    Stage: Steps.length, Total: Steps.length,
                    Label: 'GPU WebGL2 Pipeline Ready',
                    Percent: 100, Done: true,
                    Stages: this.GpuInfo.Stages, GpuInfo: this.GpuInfo
                });
                return;
            }

            const Step = Steps[Idx];
            const T0 = performance.now();
            this.Callbacks.OnCompileProgress?.({
                Stage: Idx + 1, Total: Steps.length,
                Label: Step.Label,
                Percent: Math.round(((Idx + 0.35) / Steps.length) * 100),
                Done: false, Stages: this.GpuInfo.Stages, GpuInfo: this.GpuInfo
            });

            setTimeout(() => {
                if (this.ContextLost || this.FallbackMode) return;
                try
                {
                    Step.Fn();
                    const Ms = performance.now() - T0;
                    this.GpuInfo.Stages.push({ Name: Step.Label, Ms, Ok: true });
                    Idx++;
                    this.Callbacks.OnCompileProgress?.({
                        Stage: Idx, Total: Steps.length,
                        Label: Step.Label,
                        Percent: Math.round((Idx / Steps.length) * 100),
                        Done: false, Stages: this.GpuInfo.Stages, GpuInfo: this.GpuInfo
                    });
                    setTimeout(RunNext, 20);
                }
                catch (Err)
                {
                    this.ActivateFallbackRenderer(`Shader error at stage ${Idx + 1}: ${String(Err?.message || Err)}`);
                }
            }, 20);
        };
        setTimeout(RunNext, 16);
    }

    SetScene(Scene, SelectedToken)
    {
        this.SceneState = Scene;
        this.SelectedToken = SelectedToken;
        this.UniformsDirty = true;
        this.BakeDirty = true;
        this.FallbackHeatDirty = true;
        this.MarkStreamlinesDirty();
        if (!this.FallbackMode)
        {
            this.SyncUniforms(this.LastSimTime, true);
            this.RebuildSourceBeacons();
            this.UpdateProbeAndHeatmapGeometry();
            this.ApplySliceVisibilityForPane(this.ViewMode !== '2d');
        }
    }

    SetViewMode(Mode)
    {
        this.ViewMode = Mode;
        this.StreamDirty = true;
        this.Resize();
        this.UpdateBakeWindowFromCamera(true);
        if (!this.FallbackMode && this.SceneState) this.ApplySliceVisibilityForPane(Mode !== '2d');
        this.NotifyZoomMayHaveChanged();
    }

    FocusOn(CentreMetres)
    {
        this.OrthoCenter = [CentreMetres[0], CentreMetres[2], CentreMetres[1]];
        this.FallbackCenter = [CentreMetres[0], CentreMetres[2], CentreMetres[1]];
        this.FallbackHeatDirty = true;
        if (!this.FallbackMode)
        {
            this.Controls.target.set(CentreMetres[0] * WORLD_SCALE, Math.max(0.5, CentreMetres[1] * WORLD_SCALE), CentreMetres[2] * WORLD_SCALE);
            this.Controls.update();
            this.UpdateBakeWindowFromCamera(true);
        }
    }

    ResetCamera()
    {
        this.OrthoZoomMetres = DEFAULT_HALF_M;
        this.OrthoCenter = [0, 0, 48];
        this.FallbackZoom = 1.0;
        this.FallbackCenter = [0, 0, 48];
        this.FallbackYaw = 0.62;
        this.FallbackPitch = 0.64;
        this.FallbackHeatDirty = true;
        if (!this.FallbackMode)
        {
            this.PerspCamera.position.set(42, 34, 52);
            this.Controls.target.set(0, 1.2, 0);
            this.Controls.update();
            this.UpdateBakeWindowFromCamera(true);
        }
        this.NotifyZoomMayHaveChanged();
    }

    MarkStreamlinesDirty(ForceBokehReseed = true)
    {
        this.StreamDirty = true;
        if (ForceBokehReseed) this.BokehDirty = true;
        else this.BokehCurveRefreshNeeded = true;
        if (this.FallbackStreamPanes)
        {
            this.FallbackStreamPanes[0].Key = '';
            this.FallbackStreamPanes[1].Key = '';
        }
    }

    ApplySliceVisibilityForPane(Is3DPane)
    {
        if (this.FallbackMode || !this.SceneState || !this.HeatmapMeshes) return;
        const Viz = this.SceneState.Viz;
        const ActiveAxes = GetActiveSliceAxes(Viz);
        const VisibleAxes = Is3DPane ? ActiveAxes : [Viz.SliceAxis || ActiveAxes[0]];
        const OpScale = VisibleAxes.length > 1 ? 0.76 : 1.0;
        this.SharedUniforms.uOpacity.value = Clamp((Viz.Opacity ?? 0.58) * OpScale, 0.08, 0.96);

        for (let Idx = 0; Idx < 3; ++Idx)
        {
            const AxisName = AXIS_NAMES[Idx];
            const Vis = VisibleAxes.includes(AxisName);
            this.HeatmapMeshes[Idx].visible = Boolean(Viz.Heatmap) && Vis;
            const Slot = this.ArrowSlots?.[Idx];
            if (Slot) Slot.Mesh.visible = Boolean(Viz.Vectors) && Vis && Slot.Geo.instanceCount > 0;
        }
    }

    SyncUniforms(SimTime, Force = true)
    {
        if (this.FallbackMode || !this.SceneState) return;
        if (!Force && !this.UniformsDirty && Math.abs(SimTime - this.LastSyncTime) < 1e-4) return;
        this.LastSyncTime = SimTime;
        if (Force || this.UniformsDirty) this.BokehColorsDirty = true;
        this.UniformsDirty = false;

        const Pack = PackForcesForGPU(this.SceneState);
        this.RecompileBakeShaderIfNeeded(Pack);

        this.SharedUniforms.uForceCount.value = Pack.Count;
        this.SharedUniforms.uTime.value = SimTime;
        for (let I = 0; I < MAX_GPU_FORCES; ++I)
        {
            const I4 = I * 4;
            this.SharedUniforms.uForceA.value[I].set(Pack.A[I4], Pack.A[I4 + 1], Pack.A[I4 + 2], Pack.A[I4 + 3]);
            this.SharedUniforms.uForceB.value[I].set(Pack.B[I4], Pack.B[I4 + 1], Pack.B[I4 + 2], Pack.B[I4 + 3]);
            this.SharedUniforms.uForceC.value[I].set(Pack.C[I4], Pack.C[I4 + 1], Pack.C[I4 + 2], Pack.C[I4 + 3]);
            this.SharedUniforms.uForceD.value[I].set(Pack.D[I4], Pack.D[I4 + 1], Pack.D[I4 + 2], Pack.D[I4 + 3]);
            this.SharedUniforms.uForceE.value[I].set(Pack.E[I4], Pack.E[I4 + 1], Pack.E[I4 + 2], Pack.E[I4 + 3]);
        }

        const Viz = this.SceneState.Viz;
        this.SharedUniforms.uColormap.value = COLORMAP_CODES[Viz.Colormap] ?? 0;
        this.SharedUniforms.uQuantity.value = QUANTITY_CODES[Viz.Quantity] ?? 0;
        this.SharedUniforms.uSliceAxis.value = AXIS_CODES[Viz.SliceAxis || 'XZ'] ?? 0;
        this.SharedUniforms.uSliceHeight.value = Viz.SliceHeight ?? 16;
        this.SharedUniforms.uFixedMax.value = Math.max(4, Viz.FixedMax || 34);
        this.SharedUniforms.uOpacity.value = Clamp(Viz.Opacity ?? 0.58, 0.08, 0.96);
        this.SharedUniforms.uQuietCutoff.value = Viz.QuietCutoff ?? 0.25;
        this.SharedUniforms.uStreamThickness.value = Clamp(Viz.StreamThickness ?? 0.62, 0.2, 2.5);
        this.SharedUniforms.uStreamEmissive.value = Clamp(Viz.StreamEmissive ?? 1.0, 0.15, 3.0);
        this.SharedUniforms.uStreamOpacity.value = Clamp(Viz.StreamOpacity ?? 0.92, 0.05, 1.0);
        this.SharedUniforms.uBokehThickness.value = Clamp(Viz.BokehThickness ?? 1.0, 0.25, 2.5);
        this.SharedUniforms.uBokehEmissive.value = Clamp(Viz.BokehEmissive ?? 1.0, 0.25, 2.5);

        this.ApplySliceVisibilityForPane(this.ViewMode !== '2d');
        if (this.StreamMesh) this.StreamMesh.visible = Boolean(Viz.Streamlines);
        if (this.BokehRibbonMesh) this.BokehRibbonMesh.visible = Boolean(Viz.BokehRibbons);
        if (this.BokehBeadMesh) this.BokehBeadMesh.visible = Boolean(Viz.BokehRibbons);
        this.BoundsGroup.visible = Boolean(Viz.Bounds);
        this.BakeDirty = true;
    }

    UpdateProbeAndHeatmapGeometry()
    {
        this.FallbackHeatDirty = true;
        if (this.FallbackMode || !this.SceneState) return;
        const { Probe, Viz } = this.SceneState;
        this.ProbeGroup.position.set(Probe[0] * WORLD_SCALE, Probe[1] * WORLD_SCALE, Probe[2] * WORLD_SCALE);

        const SliceM = Viz.SliceHeight ?? 16;
        const HalfM = this.ActiveBakeHalfM;
        const Fx = this.ActiveBakeCenter[0] || 0;
        const Fz = this.ActiveBakeCenter[1] || 0;
        const Fy = this.ActiveBakeCenter[2] ?? 48;

        for (const M of this.HeatmapMeshes) M.scale.set(HalfM * WORLD_SCALE, HalfM * WORLD_SCALE, 1);

        // Slot 0: XZ plane (Y = SliceM)
        this.HeatmapMeshes[0].rotation.set(-Math.PI / 2, 0, 0);
        this.HeatmapMeshes[0].position.set(Fx * WORLD_SCALE, SliceM * WORLD_SCALE, Fz * WORLD_SCALE);

        // Slot 1: XY plane (Z = SliceM)
        this.HeatmapMeshes[1].rotation.set(0, 0, 0);
        this.HeatmapMeshes[1].position.set(Fx * WORLD_SCALE, Fy * WORLD_SCALE, SliceM * WORLD_SCALE);

        // Slot 2: YZ plane (X = SliceM)
        this.HeatmapMeshes[2].rotation.set(0, -Math.PI / 2, 0);
        this.HeatmapMeshes[2].position.set(SliceM * WORLD_SCALE, Fy * WORLD_SCALE, Fz * WORLD_SCALE);

        this.RebuildArrowSeeds();
        this.UpdateGizmoPlacement();
        this.BakeDirty = true;
    }

    RebuildArrowSeeds()
    {
        if (this.FallbackMode || !this.SceneState || !this.ArrowSlots) return;
        const Viz = this.SceneState.Viz;
        const ActiveAxes = GetActiveSliceAxes(Viz);
        const NeededAxes = Array.from(new Set([...ActiveAxes, Viz.SliceAxis || ActiveAxes[0]]));
        const ZoomRatio = this.ActiveBakeHalfM / DEFAULT_HALF_M;
        const Layers = (this.ViewMode !== '2d' && ActiveAxes.length === 3)
            ? Math.max(1, Math.min(3, Viz.VolumeLayers || 1))
            : Math.max(1, Math.min(2, Viz.VolumeLayers || 1));
        const SliceM = Viz.SliceHeight ?? 16;
        const Span = this.ActiveBakeHalfM * 0.94;
        const BaseStep = Clamp((Viz.ArrowSpacing || 46) * ZoomRatio, 10, 120);
        const MultiDiv = Math.sqrt(Math.max(1, NeededAxes.length));
        const Step = Math.max(BaseStep, (Span * 2 * Math.sqrt(Layers) * MultiDiv) / 52);

        for (const Slot of this.ArrowSlots) Slot.Geo.instanceCount = 0;

        for (const AxisName of NeededAxes)
        {
            const SlotIdx = AXIS_CODES[AxisName] ?? 0;
            const Slot = this.ArrowSlots[SlotIdx];
            const Bc = this.BakeCenters[SlotIdx];
            const Cu = Bc.x, Cv = Bc.y;
            const Arr = Slot.Seeds.array;
            let N = 0;

            for (let L = 0; L < Layers; ++L)
            {
                const Offset = Layers === 1 ? 0 : (L - (Layers - 1) * 0.5) * 44 * ZoomRatio;
                for (let U = Cu - Span; U <= Cu + Span && N < 2490; U += Step)
                {
                    for (let V = Cv - Span; V <= Cv + Span && N < 2490; V += Step)
                    {
                        const [Wx, Wy, Wz] = PlaneToWorld(AxisName, U, V, SliceM + Offset);
                        Arr[N * 3] = Wx;
                        Arr[N * 3 + 1] = Wy;
                        Arr[N * 3 + 2] = Wz;
                        ++N;
                    }
                }
            }
            Slot.Seeds.needsUpdate = true;
            Slot.Geo.instanceCount = N;
        }
        this.ApplySliceVisibilityForPane(this.ViewMode !== '2d');
    }

    RebuildSourceBeacons()
    {
        if (this.FallbackMode || !this.SceneState) return;
        while (this.BoundsGroup.children.length)
        {
            const C = this.BoundsGroup.children.pop();
            C.geometry?.dispose();
            C.material?.dispose();
        }
        while (this.BeaconGroup.children.length)
        {
            const C = this.BeaconGroup.children.pop();
            C.geometry?.dispose();
            C.material?.dispose();
        }
        this.BeaconHandles = [];
        this.WaveRings = [];
        this.SourceVisuals.clear();

        for (const Source of this.SceneState.Sources)
        {
            const Model = ForceModelById[Source.Model];
            const Col = new THREE.Color(Source.UseFormula ? '#ffb454' : (Model?.Colour || '#78d2c6'));
            const IsSel = Source.Token === this.SelectedToken;
            const IsLargeEnv = (Source.Radius || 180) > 260;
            const VolumeOp = Source.Enabled
                ? (IsSel ? (IsLargeEnv ? 0.14 : 0.22) : (IsLargeEnv ? 0.05 : 0.11))
                : 0.03;
            const Cx = Source.Centre[0] * WORLD_SCALE, Cy = Source.Centre[1] * WORLD_SCALE, Cz = Source.Centre[2] * WORLD_SCALE;
            const BeaconY = Math.abs(Cy) < 0.15 ? 0.15 : Cy;

            const Beacon = new THREE.Mesh(
                new THREE.OctahedronGeometry(IsSel ? 0.85 : 0.56, 0),
                new THREE.MeshStandardMaterial({
                    color: Col, emissive: Col, emissiveIntensity: IsSel ? 0.9 : 0.35,
                    roughness: 0.35, metalness: 0.2, transparent: true, opacity: Source.Enabled ? 0.96 : 0.38
                })
            );
            Beacon.position.set(Cx, BeaconY, Cz);
            Beacon.renderOrder = 9;
            Beacon.userData = { Token: Source.Token };
            this.BeaconGroup.add(Beacon);

            // Visual selectable Circle Entity around each force center
            const CircleRing = new THREE.Mesh(
                new THREE.TorusGeometry(1.18, IsSel ? 0.072 : 0.052, 8, 36),
                new THREE.MeshBasicMaterial({
                    color: IsSel ? 0xffc857 : Col,
                    transparent: true,
                    opacity: IsSel ? 1.0 : (Source.Enabled ? 0.78 : 0.32),
                    depthWrite: false
                })
            );
            CircleRing.rotation.x = Math.PI / 2;
            CircleRing.position.set(Cx, BeaconY, Cz);
            CircleRing.renderOrder = 9;
            CircleRing.userData = { Token: Source.Token };
            this.BeaconGroup.add(CircleRing);

            const CircleDisc = new THREE.Mesh(
                new THREE.CircleGeometry(1.12, 32),
                new THREE.MeshBasicMaterial({
                    color: IsSel ? 0xffc857 : Col,
                    transparent: true,
                    opacity: IsSel ? 0.32 : (Source.Enabled ? 0.14 : 0.05),
                    side: THREE.DoubleSide,
                    depthWrite: false
                })
            );
            CircleDisc.rotation.x = -Math.PI / 2;
            CircleDisc.position.set(Cx, BeaconY, Cz);
            CircleDisc.renderOrder = 8;
            CircleDisc.userData = { Token: Source.Token };
            this.BeaconGroup.add(CircleDisc);

            const PickTarget = new THREE.Mesh(
                new THREE.SphereGeometry(2.35, 12, 8),
                new THREE.MeshBasicMaterial({ transparent: true, opacity: 0.0, depthWrite: false })
            );
            PickTarget.position.set(Cx, BeaconY, Cz);
            PickTarget.userData = { Token: Source.Token };
            this.BeaconGroup.add(PickTarget);

            this.BeaconHandles.push(CircleRing, CircleDisc, Beacon, PickTarget);

            const VolumeMat = new THREE.MeshPhongMaterial({
                color: Col,
                emissive: Col,
                emissiveIntensity: IsSel ? 0.36 : 0.16,
                shininess: 48,
                transparent: true,
                opacity: VolumeOp,
                depthWrite: false,
                side: THREE.FrontSide
            });

            let BoundMesh = null;
            let BoundYOffset = 0;
            if (Source.Reach === 'Sphere')
            {
                const R = Source.Radius * WORLD_SCALE;
                BoundMesh = new THREE.Mesh(new THREE.SphereGeometry(R, 32, 20), VolumeMat);
                BoundMesh.position.set(Cx, Cy, Cz);
            }
            else if (Source.Reach === 'Column')
            {
                const R = Source.Radius * WORLD_SCALE;
                const H = Math.min(200, Source.Height || 180) * WORLD_SCALE;
                BoundYOffset = H * 0.5;
                BoundMesh = new THREE.Mesh(new THREE.CylinderGeometry(R, R, H, 36, 1, false), VolumeMat);
                BoundMesh.position.set(Cx, Cy + BoundYOffset, Cz);
            }
            else
            {
                const Hx = (Source.HalfX || 120) * WORLD_SCALE;
                const Hz = (Source.HalfZ || 120) * WORLD_SCALE;
                const Hy = Math.min(180, Source.Height || 140) * WORLD_SCALE;
                BoundYOffset = Hy * 0.5;
                BoundMesh = new THREE.Mesh(new THREE.BoxGeometry(Hx * 2, Hy, Hz * 2), VolumeMat);
                BoundMesh.position.set(Cx, Cy + BoundYOffset, Cz);
                BoundMesh.rotation.y = -((Source.Bearing || 0) * Math.PI) / 180;
            }
            BoundMesh.renderOrder = 6;
            this.BoundsGroup.add(BoundMesh);

            let WaveSphere = null;
            if (Source.Model === 'RadialBurst')
            {
                WaveSphere = new THREE.Mesh(
                    new THREE.SphereGeometry(1, 28, 16),
                    new THREE.MeshBasicMaterial({
                        color: 0xf48466,
                        transparent: true,
                        opacity: 0.24,
                        depthWrite: false,
                        side: THREE.FrontSide
                    })
                );
                WaveSphere.position.set(Cx, BeaconY, Cz);
                WaveSphere.renderOrder = 7;
                this.BoundsGroup.add(WaveSphere);
                this.WaveRings.push({ Mesh: WaveSphere, Source });
            }

            this.SourceVisuals.set(Source.Token, {
                Source, Beacon, CircleRing, CircleDisc, PickTarget, BoundMesh, BoundYOffset, WaveSphere
            });
        }
        this.RebuildMotionPaths();
        this.UpdateGizmoPlacement();
    }

    RebuildStreamlinesIfDirty(SimTime)
    {
        if (this.FallbackMode || !this.SceneState || !this.SceneState.Viz.Streamlines) return;
        const Viz = this.SceneState.Viz;
        const Is3D = this.ViewMode !== '2d';
        const ActiveAxes = Is3D ? GetActiveSliceAxes(Viz) : [Viz.SliceAxis || GetActiveSliceAxes(Viz)[0]];
        const HalfM = Math.round(this.ActiveBakeHalfM);
        const Fx = Math.round(this.ActiveBakeCenter[0] || 0);
        const Fz = Math.round(this.ActiveBakeCenter[1] || 0);
        const Fy = Math.round(this.ActiveBakeCenter[2] ?? 48);
        const ResSteps = Clamp(Math.round(Viz.StreamResolution || 28), 8, 72);
        const TargetCount = Clamp(Math.round(Viz.StreamLineCount || 220), 24, 520);
        const ColorMode = Viz.StreamColorMode || 'FieldPalette';
        const SingleColor = Viz.StreamSingleColor || '#68dcff';
        const Hash = `${this.SceneState.Sources.map((s) => `${s.Token}:${s.Enabled}:${s.Strength}:${s.Swirl}:${s.Centre.map((v) => v.toFixed(1)).join(',')}:${s.Radius}:${s.UseFormula}:${s.FormulaCode}`).join('|')}:${ActiveAxes.join(',')}:${Viz.StreamlineSpacing}:${Viz.SliceHeight}:${Viz.Colormap}:${Viz.FixedMax}:${Viz.SoloToken || ''}:${this.ViewMode}:${HalfM}:${Fx}:${Fy}:${Fz}:${ResSteps}:${TargetCount}:${ColorMode}:${SingleColor}`;
        if (!this.StreamDirty && Hash === this.StreamSeedHash) return;

        const NowMs = performance.now();
        if (this.StreamSeedHash && NowMs - this.LastStreamRebuildMs < 48) return;
        this.LastStreamRebuildMs = NowMs;
        this.StreamDirty = false;
        this.StreamSeedHash = Hash;

        const ZoomRatio = this.ActiveBakeHalfM / DEFAULT_HALF_M;
        const SeedStep = Clamp((Viz.StreamlineSpacing || 84) * ZoomRatio, 14, 150);
        const StepM = Clamp((10 * ZoomRatio) * (24 / ResSteps), 1.4, 20);
        const Span = this.ActiveBakeHalfM * 0.9;
        const BoundsByAxis = {
            XZ: { MinU: Fx - Span, MaxU: Fx + Span, MinV: Fz - Span, MaxV: Fz + Span },
            XY: { MinU: Fx - Span, MaxU: Fx + Span, MinV: Fy - Span, MaxV: Fy + Span },
            YZ: { MinU: Fz - Span, MaxU: Fz + Span, MinV: Fy - Span, MaxV: Fy + Span }
        };
        const Seeds = CollectMultiAxisStreamlineSeeds(this.SceneState, BoundsByAxis, Viz.SliceHeight ?? 16, SeedStep, {
            ActiveAxes,
            Is3D,
            Volumetric: Is3D,
            TargetCount
        });
        const FixedMax = Math.max(6, Viz.FixedMax || 34);
        const Solo = Viz.SoloToken || null;

        const Curves = [];
        for (const [Sx, Sy, Sz] of Seeds)
        {
            const Pts = IntegrateStreamlineBidirectional(this.SceneState, Sx, Sy, Sz, SimTime, ResSteps, StepM, { Solo });
            if (Pts.length >= 4) Curves.push(Pts);
        }

        const NextGeo = BuildStreamlineRibbonGeometry(Curves, Viz.Colormap, FixedMax, { ColorMode, SingleColor });
        this.StreamMesh.geometry.dispose();
        this.StreamMesh.geometry = NextGeo;
    }

    UpdateBokehRibbons(SimTime, Dt, StepPhysics = true)
    {
        if (!this.SceneState?.Viz?.BokehRibbons) return;
        const Viz = this.SceneState.Viz;
        const Is3D = this.ViewMode !== '2d';
        const ActiveAxes = Is3D ? GetActiveSliceAxes(Viz) : [Viz.SliceAxis || GetActiveSliceAxes(Viz)[0]];
        const Bounds = {
            Is3D,
            ActiveAxes,
            SliceAxis: Viz.SliceAxis || ActiveAxes[0] || 'XZ',
            SliceHeight: Viz.SliceHeight ?? 16,
            HalfM: this.FallbackMode ? Math.max(85, DEFAULT_HALF_M / Math.max(0.25, this.FallbackZoom || 1)) : this.ActiveBakeHalfM,
            Center: this.FallbackMode ? (this.FallbackCenter || [0, 0, 48]) : (this.ActiveBakeCenter || [0, 0, 48])
        };

        const ForceReseed = Boolean(this.BokehDirty);
        this.BokehDirty = false;
        const Reseeded = this.BokehSim.EnsureSynced(this.SceneState, Bounds, SimTime, ForceReseed);
        let CurvesRefreshed = false;
        if (this.BokehCurveRefreshNeeded && !Reseeded)
        {
            this.BokehCurveRefreshNeeded = false;
            CurvesRefreshed = true;
            this.BokehSim.RefreshCurvesKeepTracers(this.SceneState, Bounds, SimTime);
        }
        if (StepPhysics) this.BokehSim.Step(this.SceneState, Bounds, SimTime, Dt);

        if (!this.FallbackMode && this.BokehRibbonGeo && this.BokehBeadGeo && (StepPhysics || Reseeded || ForceReseed || CurvesRefreshed || this.BokehColorsDirty || this.BokehRibbonGeo.drawRange.count === 0))
        {
            this.BokehColorsDirty = false;
            this.BokehSim.FillWebGLBuffers(this.SceneState, this.BokehRibbonGeo, this.BokehBeadGeo);
        }
    }

    UpdateWaveRings(SimTime)
    {
        if (!this.WaveRings) return;
        for (const Item of this.WaveRings)
        {
            const Env = ResolveDurationEnvelope(Item.Source, SimTime);
            const FrontR = (Item.Source.InitialRadius || 8) + (Item.Source.WaveSpeed || 90) * Env.Age;
            const Visible = Env.Gate > 0.02 && FrontR < Item.Source.Radius * 1.1;
            Item.Mesh.visible = Visible;
            if (Visible)
            {
                const S = FrontR * WORLD_SCALE;
                Item.Mesh.scale.set(S, S, S);
                Item.Mesh.material.opacity = 0.26 * Env.Gate;
            }
        }
    }

    BakeSliceFieldGPU(SimTime)
    {
        if (!this.BakeDirty && Math.abs(SimTime - this.LastBakedTime) < 0.032) return;
        if (!this.FieldBakeRTs || !this.SceneState) return;
        this.BakeDirty = false;
        this.LastBakedTime = SimTime;

        const Viz = this.SceneState.Viz;
        const ActiveAxes = GetActiveSliceAxes(Viz);
        const NeededAxes = this.ViewMode === '3d'
            ? ActiveAxes
            : Array.from(new Set([...ActiveAxes, Viz.SliceAxis || ActiveAxes[0]]));

        const PrevTarget = this.Renderer.getRenderTarget();
        this.Renderer.setScissorTest(false);
        for (const AxisName of NeededAxes)
        {
            const SlotIdx = AXIS_CODES[AxisName] ?? 0;
            this.SharedUniforms.uSliceAxis.value = SlotIdx;
            this.SharedUniforms.uBakeCenter.value.copy(this.BakeCenters[SlotIdx]);
            this.Renderer.setRenderTarget(this.FieldBakeRTs[SlotIdx]);
            this.Renderer.clear();
            this.Renderer.render(this.BakeScene, this.BakeCamera);
        }
        this.Renderer.setRenderTarget(PrevTarget);
    }

    UpdateOrthoCamera(PaneW, PaneH)
    {
        const Aspect = Math.max(0.2, PaneW / Math.max(1, PaneH));
        const HalfH = this.OrthoZoomMetres * WORLD_SCALE;
        const HalfW = HalfH * Aspect;
        this.OrthoCamera.left = -HalfW;
        this.OrthoCamera.right = HalfW;
        this.OrthoCamera.top = HalfH;
        this.OrthoCamera.bottom = -HalfH;

        const Axis = this.SceneState?.Viz?.SliceAxis || 'XZ';
        const Cx = (this.OrthoCenter[0] || 0) * WORLD_SCALE;
        const Cz = (this.OrthoCenter[1] || 0) * WORLD_SCALE;
        const Cy = (this.OrthoCenter[2] ?? 48) * WORLD_SCALE;
        if (Axis === 'XZ')
        {
            this.OrthoCamera.position.set(Cx, 120, Cz);
            this.OrthoCamera.up.set(0, 0, -1);
            this.OrthoCamera.lookAt(Cx, 0, Cz);
        }
        else if (Axis === 'XY')
        {
            this.OrthoCamera.position.set(Cx, Cy, 120);
            this.OrthoCamera.up.set(0, 1, 0);
            this.OrthoCamera.lookAt(Cx, Cy, 0);
        }
        else
        {
            this.OrthoCamera.position.set(-120, Cy, Cz);
            this.OrthoCamera.up.set(0, 1, 0);
            this.OrthoCamera.lookAt(0, Cy, Cz);
        }
        this.OrthoCamera.updateProjectionMatrix();
    }

    RenderFrame(SimTime, Dt, StepPhysics = true)
    {
        this.LastSimTime = SimTime;
        const Now = performance.now();
        const FrameDt = Math.max(1, Now - this.LastFrameMs);
        this.LastFrameMs = Now;
        this.FpsSmoothed = this.FpsSmoothed * 0.9 + (1000 / FrameDt) * 0.1;

        if (this.FallbackMode)
        {
            this.UpdateBokehRibbons(SimTime, Dt, StepPhysics);
            const Ctx = this.FallbackCtx;
            const W = this.Width || 800, H = this.Height || 500;
            const DPR = Math.min(window.devicePixelRatio || 1, 2);
            Ctx.save();
            Ctx.setTransform(DPR, 0, 0, DPR, 0, 0);
            if (this.ViewMode === 'split')
            {
                const Half = Math.floor(W * 0.5);
                this.RenderFallbackPane(Ctx, 0, 0, Half, H, true, SimTime);
                this.RenderFallbackPane(Ctx, Half, 0, W - Half, H, false, SimTime);
            }
            else
            {
                this.RenderFallbackPane(Ctx, 0, 0, W, H, this.ViewMode === '3d', SimTime);
            }
            Ctx.restore();
            return;
        }

        if (this.ContextLost || !this.ShadersReady) return;

        this.UpdateBakeWindowFromCamera(false);
        this.SyncUniforms(SimTime, false);
        this.BakeSliceFieldGPU(SimTime);
        this.RebuildStreamlinesIfDirty(SimTime);
        this.UpdateBokehRibbons(SimTime, Dt, StepPhysics);
        this.UpdateWaveRings(SimTime);
        this.Controls.update();

        const W = this.Width || this.Container.clientWidth || 800;
        const H = this.Height || this.Container.clientHeight || 500;
        if (this.SharedUniforms?.uViewportHeightPx) this.SharedUniforms.uViewportHeightPx.value = H;

        this.Renderer.setRenderTarget(null);
        this.Renderer.clear();

        if (this.ViewMode === '3d')
        {
            this.ApplySliceVisibilityForPane(true);
            this.Renderer.setScissorTest(false);
            this.Renderer.setViewport(0, 0, W, H);
            this.PerspCamera.aspect = W / Math.max(1, H);
            this.PerspCamera.updateProjectionMatrix();
            this.UpdateGizmoScreenScale(this.PerspCamera);
            this.Renderer.render(this.ThreeScene, this.PerspCamera);
        }
        else if (this.ViewMode === '2d')
        {
            this.ApplySliceVisibilityForPane(false);
            this.Renderer.setScissorTest(false);
            this.Renderer.setViewport(0, 0, W, H);
            this.UpdateOrthoCamera(W, H);
            this.UpdateGizmoScreenScale(this.OrthoCamera);
            this.Renderer.render(this.ThreeScene, this.OrthoCamera);
        }
        else
        {
            const Half = Math.floor(W * 0.5);
            this.Renderer.setScissorTest(true);

            this.ApplySliceVisibilityForPane(true);
            this.Renderer.setViewport(0, 0, Half, H);
            this.Renderer.setScissor(0, 0, Half, H);
            this.PerspCamera.aspect = Half / Math.max(1, H);
            this.PerspCamera.updateProjectionMatrix();
            this.UpdateGizmoScreenScale(this.PerspCamera);
            this.Renderer.render(this.ThreeScene, this.PerspCamera);

            this.ApplySliceVisibilityForPane(false);
            const RightW = W - Half;
            this.Renderer.setViewport(Half, 0, RightW, H);
            this.Renderer.setScissor(Half, 0, RightW, H);
            this.UpdateOrthoCamera(RightW, H);
            this.UpdateGizmoScreenScale(this.OrthoCamera);
            this.Renderer.render(this.ThreeScene, this.OrthoCamera);
            this.Renderer.setScissorTest(false);
        }
    }

    Resize()
    {
        const W = Math.max(64, this.Container.clientWidth);
        const H = Math.max(64, this.Container.clientHeight);
        this.Width = W;
        this.Height = H;
        if (this.SharedUniforms?.uViewportHeightPx) this.SharedUniforms.uViewportHeightPx.value = H;
        if (this.FallbackMode && this.FallbackCanvas)
        {
            const DPR = Math.min(window.devicePixelRatio || 1, 2);
            this.FallbackCanvas.width = Math.round(W * DPR);
            this.FallbackCanvas.height = Math.round(H * DPR);
        }
        else if (this.Renderer)
        {
            this.Renderer.setSize(W, H, false);
            this.Renderer.domElement.style.width = '100%';
            this.Renderer.domElement.style.height = '100%';
        }
    }

    BindPointerEvents(Dom, IsFallback)
    {
        if (IsFallback)
        {
            let Mode = null, LastX = 0, LastY = 0, ActivePickAxis = 'XZ';
            let GizmoDrag = null;
            const FallbackSliceAt = (ClientX, ClientY) => {
                const Rect = Dom.getBoundingClientRect();
                const Rx = ClientX - Rect.left, Ry = ClientY - Rect.top;
                const IsSplitRight = this.ViewMode === 'split' && Rx >= Rect.width * 0.5;
                const Px = IsSplitRight ? Rect.width * 0.5 : 0;
                const Pw = this.ViewMode === 'split' ? Rect.width * 0.5 : Rect.width;
                const IsIso = this.ViewMode === '3d' || (this.ViewMode === 'split' && !IsSplitRight);
                const Viz = this.SceneState?.Viz;
                const OffsetM = Viz?.SliceHeight ?? 16;
                const ActiveAxes = GetActiveSliceAxes(Viz);
                const Axis = !IsIso
                    ? (Viz?.SliceAxis || ActiveAxes[0])
                    : (ActiveAxes.includes(Viz?.SliceAxis) ? Viz.SliceAxis : ActiveAxes[0]);
                const PivotY = IsIso && ActiveAxes.length === 1 && ActiveAxes[0] === 'XZ'
                    ? OffsetM
                    : (this.FallbackCenter[2] ?? 48);
                const Frame = ComputeFallbackPlaneFrame(
                    Px, 0, Pw, Rect.height, IsIso, Axis,
                    this.FallbackZoom, this.FallbackCenter,
                    this.FallbackYaw, this.FallbackPitch, OffsetM, PivotY
                );
                return { Rx, Ry, Rect, Frame, OffsetM, Axis, IsIso };
            };

            const HitTestFallback = (Rx, Ry, IsIso) => {
                const Pick = this.FallbackPickState[IsIso ? 'iso' : 'ortho'];
                if (!Pick) return null;
                for (const H of Pick.Handles)
                {
                    if (H.Axis === 'XZ')
                    {
                        if (Math.hypot(Rx - H.X, Ry - H.Y) <= 19 && Math.hypot(Rx - H.X, Ry - H.Y) >= 9) return { Kind: 'handle', Handle: H };
                    }
                    else if (Math.hypot(Rx - H.X, Ry - H.Y) <= 13)
                    {
                        return { Kind: 'handle', Handle: H };
                    }
                }
                if (Pick.Waypoints)
                {
                    for (let I = Pick.Waypoints.length - 1; I >= 0; --I)
                    {
                        const Wp = Pick.Waypoints[I];
                        if (Math.hypot(Rx - Wp.X, Ry - Wp.Y) <= 11) return { Kind: 'waypoint', Token: Wp.Token, WaypointIdx: Wp.WaypointIdx };
                    }
                }
                for (let I = Pick.Circles.length - 1; I >= 0; --I)
                {
                    const C = Pick.Circles[I];
                    if (Math.hypot(Rx - C.X, Ry - C.Y) <= 15) return { Kind: 'circle', Token: C.Token };
                }
                return null;
            };

            Dom.addEventListener('contextmenu', (E) => E.preventDefault());
            Dom.addEventListener('pointerdown', (E) => {
                const { Rx, Ry, Rect, Frame, Axis, IsIso } = FallbackSliceAt(E.clientX, E.clientY);
                ActivePickAxis = Axis;
                if (E.button === 2 || E.button === 1)
                {
                    Mode = 'pan'; LastX = E.clientX; LastY = E.clientY;
                    return;
                }
                if (E.button === 0 && !E.shiftKey)
                {
                    const Hit = HitTestFallback(Rx, Ry, IsIso);
                    if (Hit?.Kind === 'handle')
                    {
                        const Src = this.SceneState?.Sources.find((S) => S.Token === this.SelectedToken);
                        if (Src)
                        {
                            const Wp = (this.SelectedWaypointIdx != null && Src.Motion?.Waypoints?.[this.SelectedWaypointIdx])
                                ? Src.Motion.Waypoints[this.SelectedWaypointIdx]
                                : null;
                            Mode = 'gizmo';
                            GizmoDrag = {
                                Token: Src.Token,
                                WaypointIdx: Wp ? this.SelectedWaypointIdx : null,
                                Axis: Hit.Handle.Axis,
                                Handle: Hit.Handle,
                                StartX: Rx,
                                StartY: Ry,
                                StartCentre: [...(Wp || Src.Centre)],
                                StartPlanePos: Frame.ScreenToWorldOnAxis(Rx, Ry)
                            };
                            Dom.style.cursor = 'grabbing';
                            return;
                        }
                    }
                    if (Hit?.Kind === 'waypoint')
                    {
                        this.SelectedWaypointIdx = Hit.WaypointIdx;
                        this.Callbacks.OnSelectWaypoint?.(Hit.Token, Hit.WaypointIdx);
                        const Src = this.SceneState?.Sources.find((S) => S.Token === Hit.Token);
                        const Wp = Src?.Motion?.Waypoints?.[Hit.WaypointIdx];
                        if (Src && Wp)
                        {
                            Mode = 'gizmo';
                            GizmoDrag = {
                                Token: Src.Token,
                                WaypointIdx: Hit.WaypointIdx,
                                Axis: 'XZ',
                                Handle: { Axis: 'XZ' },
                                StartX: Rx,
                                StartY: Ry,
                                StartCentre: [...Wp],
                                StartPlanePos: Frame.ScreenToWorldOnAxis(Rx, Ry)
                            };
                            Dom.style.cursor = 'grabbing';
                            return;
                        }
                    }
                    if (Hit?.Kind === 'circle')
                    {
                        this.SelectedWaypointIdx = null;
                        this.Callbacks.OnSelectSource?.(Hit.Token);
                        const Src = this.SceneState?.Sources.find((S) => S.Token === Hit.Token);
                        if (Src)
                        {
                            Mode = 'gizmo';
                            GizmoDrag = {
                                Token: Src.Token,
                                WaypointIdx: null,
                                Axis: 'XZ',
                                Handle: { Axis: 'XZ' },
                                StartX: Rx,
                                StartY: Ry,
                                StartCentre: [...Src.Centre],
                                StartPlanePos: Frame.ScreenToWorldOnAxis(Rx, Ry)
                            };
                            Dom.style.cursor = 'grabbing';
                            return;
                        }
                    }
                }
                if (E.shiftKey || this.ViewMode === '2d' || (this.ViewMode === 'split' && Rx > Rect.width * 0.5))
                {
                    const PosM = Frame.ScreenToWorldOnAxis(Rx, Ry);
                    this.Callbacks.OnMoveProbe?.(PosM);
                    Mode = 'probe';
                }
                else
                {
                    Mode = 'rot'; LastX = E.clientX; LastY = E.clientY;
                }
            });
            window.addEventListener('pointermove', (E) => {
                if (!Mode)
                {
                    const { Rx, Ry, IsIso } = FallbackSliceAt(E.clientX, E.clientY);
                    const Hit = HitTestFallback(Rx, Ry, IsIso);
                    if (Hit?.Kind === 'handle')
                    {
                        this.SetHoverHighlight(Hit.Handle.Axis, null);
                        Dom.style.cursor = 'grab';
                    }
                    else if (Hit?.Kind === 'circle')
                    {
                        this.SetHoverHighlight(null, Hit.Token);
                        Dom.style.cursor = 'pointer';
                    }
                    else
                    {
                        this.SetHoverHighlight(null, null);
                        Dom.style.cursor = '';
                    }
                    return;
                }
                if (Mode === 'gizmo' && GizmoDrag)
                {
                    const { Rx, Ry, Frame } = FallbackSliceAt(E.clientX, E.clientY);
                    const Next = [...GizmoDrag.StartCentre];
                    if (GizmoDrag.Axis === 'XZ')
                    {
                        const NowPos = Frame.ScreenToWorldOnAxis(Rx, Ry);
                        Next[0] += NowPos[0] - GizmoDrag.StartPlanePos[0];
                        Next[1] += NowPos[1] - GizmoDrag.StartPlanePos[1];
                        Next[2] += NowPos[2] - GizmoDrag.StartPlanePos[2];
                    }
                    else
                    {
                        const H = GizmoDrag.Handle;
                        const DeltaScr = (Rx - GizmoDrag.StartX) * H.DirScr[0] + (Ry - GizmoDrag.StartY) * H.DirScr[1];
                        const DeltaM = DeltaScr / Math.max(0.05, H.PxPerM || 0.5);
                        const Idx = GizmoDrag.Axis === 'X' ? 0 : GizmoDrag.Axis === 'Y' ? 1 : 2;
                        Next[Idx] += DeltaM;
                    }
                    if (GizmoDrag.WaypointIdx != null) this.Callbacks.OnMoveWaypoint?.(GizmoDrag.Token, GizmoDrag.WaypointIdx, Next);
                    else this.Callbacks.OnMoveSource?.(GizmoDrag.Token, Next);
                }
                else if (Mode === 'rot')
                {
                    this.FallbackYaw += (E.clientX - LastX) * 0.008;
                    this.FallbackPitch = Clamp(this.FallbackPitch + (E.clientY - LastY) * 0.008, 0.15, 1.4);
                    this.FallbackHeatDirty = true;
                    LastX = E.clientX; LastY = E.clientY;
                }
                else if (Mode === 'probe')
                {
                    const { Rx, Ry, Frame } = FallbackSliceAt(E.clientX, E.clientY);
                    const PosM = Frame.ScreenToWorldOnAxis(Rx, Ry);
                    this.Callbacks.OnMoveProbe?.(PosM);
                }
                else if (Mode === 'pan')
                {
                    const Rect = Dom.getBoundingClientRect();
                    const Span = DEFAULT_HALF_M / Math.max(0.25, this.FallbackZoom);
                    const Scale = Math.min(Rect.width, Rect.height) / (Span * 2.0);
                    const Dx = (E.clientX - LastX) / Scale;
                    const Dy = (E.clientY - LastY) / Scale;
                    if (ActivePickAxis === 'XY')
                    {
                        this.FallbackCenter[0] -= Dx;
                        this.FallbackCenter[2] += Dy;
                    }
                    else if (ActivePickAxis === 'YZ')
                    {
                        this.FallbackCenter[1] -= Dx;
                        this.FallbackCenter[2] += Dy;
                    }
                    else
                    {
                        this.FallbackCenter[0] -= Dx;
                        this.FallbackCenter[1] -= Dy;
                    }
                    this.FallbackHeatDirty = true;
                    LastX = E.clientX; LastY = E.clientY;
                }
            });
            window.addEventListener('pointerup', () => {
                Mode = null;
                GizmoDrag = null;
                Dom.style.cursor = '';
            });
            Dom.addEventListener('wheel', (E) => {
                E.preventDefault();
                const Factor = E.deltaY < 0 ? 1.14 : 0.88;
                this.ZoomBy(Factor);
            }, { passive: false });
            return;
        }

        const Raycaster = new THREE.Raycaster();
        const Ndc = new THREE.Vector2();

        const ResolvePaneAndCamera = (ClientX, ClientY) => {
            const Rect = Dom.getBoundingClientRect();
            const Rx = ClientX - Rect.left;
            const Ry = ClientY - Rect.top;
            if (this.ViewMode === '2d')
            {
                Ndc.set((Rx / Rect.width) * 2 - 1, -(Ry / Rect.height) * 2 + 1);
                return { Cam: this.OrthoCamera, Is2D: true };
            }
            if (this.ViewMode === 'split' && Rx >= Rect.width * 0.5)
            {
                const LocalX = Rx - Rect.width * 0.5;
                const PaneW = Rect.width * 0.5;
                Ndc.set((LocalX / PaneW) * 2 - 1, -(Ry / Rect.height) * 2 + 1);
                return { Cam: this.OrthoCamera, Is2D: true };
            }
            const PaneW = this.ViewMode === 'split' ? Rect.width * 0.5 : Rect.width;
            Ndc.set((Rx / PaneW) * 2 - 1, -(Ry / Rect.height) * 2 + 1);
            return { Cam: this.PerspCamera, Is2D: false };
        };

        const IntersectSliceMetres = (Cam, Is2D) => {
            Raycaster.setFromCamera(Ndc, Cam);
            const Viz = this.SceneState?.Viz;
            const SliceM = Viz?.SliceHeight ?? 16;
            const ActiveAxes = GetActiveSliceAxes(Viz);
            const Axis = Is2D
                ? (Viz?.SliceAxis || ActiveAxes[0])
                : (ActiveAxes.includes(Viz?.SliceAxis) ? Viz.SliceAxis : ActiveAxes[0]);
            const Plane = Axis === 'XZ'
                ? new THREE.Plane(new THREE.Vector3(0, 1, 0), -SliceM * WORLD_SCALE)
                : Axis === 'XY'
                    ? new THREE.Plane(new THREE.Vector3(0, 0, 1), -SliceM * WORLD_SCALE)
                    : new THREE.Plane(new THREE.Vector3(1, 0, 0), -SliceM * WORLD_SCALE);
            const Hit = new THREE.Vector3();
            if (!Raycaster.ray.intersectPlane(Plane, Hit)) return null;
            return [Hit.x * INV_SCALE, Hit.y * INV_SCALE, Hit.z * INV_SCALE];
        };

        const IntersectDragPlaneWorld = (Cam, PlaneNormal, AnchorWorld) => {
            Raycaster.setFromCamera(Ndc, Cam);
            const Plane = new THREE.Plane().setFromNormalAndCoplanarPoint(PlaneNormal, AnchorWorld);
            const Hit = new THREE.Vector3();
            return Raycaster.ray.intersectPlane(Plane, Hit) ? Hit : null;
        };

        const AxisDirs = {
            X: [1, 0, 0],
            Y: [0, 1, 0],
            Z: [0, 0, 1]
        };

        const PlaneNormalForDrag = (Is2D) => {
            if (!Is2D) return new THREE.Vector3(0, 1, 0);
            const Ax = this.SceneState?.Viz?.SliceAxis || 'XZ';
            if (Ax === 'XY') return new THREE.Vector3(0, 0, 1);
            if (Ax === 'YZ') return new THREE.Vector3(1, 0, 0);
            return new THREE.Vector3(0, 1, 0);
        };

        let DragMode = null;
        let LastPanX = 0, LastPanY = 0;
        Dom.addEventListener('contextmenu', (E) => E.preventDefault());

        // Capture phase ensures gizmo/circle grabs run BEFORE OrbitControls starts rotating!
        Dom.addEventListener('pointerdown', (E) => {
            const { Cam, Is2D } = ResolvePaneAndCamera(E.clientX, E.clientY);
            this.Controls.enabled = !Is2D;
            this.UpdateGizmoScreenScale(Cam);
            this.GizmoGroup?.updateMatrixWorld(true);

            if (Is2D && (E.button === 2 || E.button === 1))
            {
                DragMode = { Kind: 'pan2d' };
                LastPanX = E.clientX; LastPanY = E.clientY;
                return;
            }

            if (E.button === 0 && !E.shiftKey)
            {
                Raycaster.setFromCamera(Ndc, Cam);
                if (this.GizmoGroup?.visible)
                {
                    const GizmoHits = Raycaster.intersectObjects(this.GizmoHandles, false);
                    if (GizmoHits.length > 0)
                    {
                        const Axis = GizmoHits[0].object.userData.GizmoAxis;
                        const Src = this.SceneState?.Sources.find((S) => S.Token === this.SelectedToken);
                        if (Src)
                        {
                            const ActiveWp = (this.SelectedWaypointIdx != null && Src.Motion?.Waypoints?.[this.SelectedWaypointIdx])
                                ? Src.Motion.Waypoints[this.SelectedWaypointIdx]
                                : null;
                            const ActivePt = ActiveWp || Src.Centre;
                            const WaypointIdx = ActiveWp ? this.SelectedWaypointIdx : null;
                            const StartWorld = new THREE.Vector3(
                                ActivePt[0] * WORLD_SCALE,
                                ActivePt[1] * WORLD_SCALE,
                                ActivePt[2] * WORLD_SCALE
                            );
                            const StartCentre = [...ActivePt];
                            if (Axis === 'XZ')
                            {
                                const PlaneNorm = PlaneNormalForDrag(Is2D);
                                const StartHit = IntersectDragPlaneWorld(Cam, PlaneNorm, StartWorld);
                                if (StartHit)
                                {
                                    DragMode = { Kind: 'gizmo-plane', Token: Src.Token, WaypointIdx, StartCentre, StartHit, PlaneNorm, Is2D };
                                    this.Controls.enabled = false;
                                    Dom.style.cursor = 'grabbing';
                                    E.stopPropagation();
                                    return;
                                }
                            }
                            else if (AxisDirs[Axis])
                            {
                                const Dir = AxisDirs[Axis];
                                const Ro = [Raycaster.ray.origin.x, Raycaster.ray.origin.y, Raycaster.ray.origin.z];
                                const Rd = [Raycaster.ray.direction.x, Raycaster.ray.direction.y, Raycaster.ray.direction.z];
                                const StartT = ProjectRayToAxisParam(Ro, Rd, [StartWorld.x, StartWorld.y, StartWorld.z], Dir);
                                DragMode = { Kind: 'gizmo-axis', Token: Src.Token, WaypointIdx, Axis, Dir, StartCentre, StartWorld, StartT };
                                this.Controls.enabled = false;
                                Dom.style.cursor = 'grabbing';
                                E.stopPropagation();
                                return;
                            }
                        }
                    }
                }

                const WaypointHits = Raycaster.intersectObjects(this.WaypointHandles, false);
                if (WaypointHits.length > 0)
                {
                    const { Token, WaypointIdx } = WaypointHits[0].object.userData;
                    this.SelectedWaypointIdx = WaypointIdx;
                    this.Callbacks.OnSelectWaypoint?.(Token, WaypointIdx);
                    const Src = this.SceneState?.Sources.find((S) => S.Token === Token);
                    const Wp = Src?.Motion?.Waypoints?.[WaypointIdx];
                    if (Src && Wp)
                    {
                        const StartWorld = new THREE.Vector3(
                            Wp[0] * WORLD_SCALE,
                            Wp[1] * WORLD_SCALE,
                            Wp[2] * WORLD_SCALE
                        );
                        const PlaneNorm = PlaneNormalForDrag(Is2D);
                        const StartHit = IntersectDragPlaneWorld(Cam, PlaneNorm, StartWorld) || StartWorld.clone();
                        DragMode = { Kind: 'gizmo-plane', Token, WaypointIdx, StartCentre: [...Wp], StartHit, PlaneNorm, Is2D };
                        this.Controls.enabled = false;
                        Dom.style.cursor = 'grabbing';
                        E.stopPropagation();
                        return;
                    }
                }

                const BeaconHits = Raycaster.intersectObjects(this.BeaconHandles, false);
                if (BeaconHits.length > 0)
                {
                    const Token = BeaconHits[0].object.userData.Token;
                    this.SelectedWaypointIdx = null;
                    this.Callbacks.OnSelectSource?.(Token);
                    const Src = this.SceneState?.Sources.find((S) => S.Token === Token);
                    if (Src)
                    {
                        const StartWorld = new THREE.Vector3(
                            Src.Centre[0] * WORLD_SCALE,
                            Src.Centre[1] * WORLD_SCALE,
                            Src.Centre[2] * WORLD_SCALE
                        );
                        const PlaneNorm = PlaneNormalForDrag(Is2D);
                        const StartHit = IntersectDragPlaneWorld(Cam, PlaneNorm, StartWorld) || StartWorld.clone();
                        DragMode = { Kind: 'gizmo-plane', Token, WaypointIdx: null, StartCentre: [...Src.Centre], StartHit, PlaneNorm, Is2D };
                        this.Controls.enabled = false;
                        Dom.style.cursor = 'grabbing';
                        E.stopPropagation();
                        return;
                    }
                }
            }

            if (E.shiftKey || (E.button === 0 && Is2D))
            {
                const PosM = IntersectSliceMetres(Cam, Is2D);
                if (PosM)
                {
                    DragMode = { Kind: 'probe' };
                    this.Callbacks.OnMoveProbe?.(PosM);
                    this.Controls.enabled = false;
                    E.stopPropagation();
                }
            }
        }, { capture: true });

        window.addEventListener('pointermove', (E) => {
            if (!DragMode)
            {
                if (E.target !== Dom)
                {
                    this.SetHoverHighlight(null, null);
                    Dom.style.cursor = '';
                    return;
                }
                const { Cam } = ResolvePaneAndCamera(E.clientX, E.clientY);
                this.UpdateGizmoScreenScale(Cam);
                this.GizmoGroup?.updateMatrixWorld(true);
                Raycaster.setFromCamera(Ndc, Cam);
                if (this.GizmoGroup?.visible)
                {
                    const GizmoHits = Raycaster.intersectObjects(this.GizmoHandles, false);
                    if (GizmoHits.length > 0)
                    {
                        this.SetHoverHighlight(GizmoHits[0].object.userData.GizmoAxis, null);
                        Dom.style.cursor = 'grab';
                        return;
                    }
                }
                const BeaconHits = Raycaster.intersectObjects(this.BeaconHandles, false);
                if (BeaconHits.length > 0)
                {
                    this.SetHoverHighlight(null, BeaconHits[0].object.userData.Token);
                    Dom.style.cursor = 'pointer';
                    return;
                }
                this.SetHoverHighlight(null, null);
                Dom.style.cursor = '';
                return;
            }

            if (DragMode.Kind === 'pan2d')
            {
                const Rect = Dom.getBoundingClientRect();
                const Scale = (Rect.height * 0.5) / Math.max(20, this.OrthoZoomMetres);
                const Dx = (E.clientX - LastPanX) / Scale;
                const Dy = (E.clientY - LastPanY) / Scale;
                const Axis2D = this.SceneState?.Viz?.SliceAxis || 'XZ';
                if (Axis2D === 'XY')
                {
                    this.OrthoCenter[0] -= Dx;
                    this.OrthoCenter[2] += Dy;
                }
                else if (Axis2D === 'YZ')
                {
                    this.OrthoCenter[1] -= Dx;
                    this.OrthoCenter[2] += Dy;
                }
                else
                {
                    this.OrthoCenter[0] -= Dx;
                    this.OrthoCenter[1] -= Dy;
                }
                LastPanX = E.clientX; LastPanY = E.clientY;
                this.UpdateBakeWindowFromCamera(true);
                return;
            }

            const { Cam, Is2D } = ResolvePaneAndCamera(E.clientX, E.clientY);
            if (DragMode.Kind === 'gizmo-axis')
            {
                Raycaster.setFromCamera(Ndc, Cam);
                const Ro = [Raycaster.ray.origin.x, Raycaster.ray.origin.y, Raycaster.ray.origin.z];
                const Rd = [Raycaster.ray.direction.x, Raycaster.ray.direction.y, Raycaster.ray.direction.z];
                const Sw = DragMode.StartWorld;
                const NowT = ProjectRayToAxisParam(Ro, Rd, [Sw.x, Sw.y, Sw.z], DragMode.Dir);
                const DeltaM = (NowT - DragMode.StartT) * INV_SCALE;
                const Next = [
                    DragMode.StartCentre[0] + DragMode.Dir[0] * DeltaM,
                    DragMode.StartCentre[1] + DragMode.Dir[1] * DeltaM,
                    DragMode.StartCentre[2] + DragMode.Dir[2] * DeltaM
                ];
                if (DragMode.WaypointIdx != null) this.Callbacks.OnMoveWaypoint?.(DragMode.Token, DragMode.WaypointIdx, Next);
                else this.Callbacks.OnMoveSource?.(DragMode.Token, Next);
                return;
            }
            if (DragMode.Kind === 'gizmo-plane')
            {
                const Hit = IntersectDragPlaneWorld(Cam, DragMode.PlaneNorm, new THREE.Vector3(
                    DragMode.StartCentre[0] * WORLD_SCALE,
                    DragMode.StartCentre[1] * WORLD_SCALE,
                    DragMode.StartCentre[2] * WORLD_SCALE
                ));
                if (!Hit) return;
                const Next = [
                    DragMode.StartCentre[0] + (Hit.x - DragMode.StartHit.x) * INV_SCALE,
                    DragMode.StartCentre[1] + (Hit.y - DragMode.StartHit.y) * INV_SCALE,
                    DragMode.StartCentre[2] + (Hit.z - DragMode.StartHit.z) * INV_SCALE
                ];
                if (DragMode.WaypointIdx != null) this.Callbacks.OnMoveWaypoint?.(DragMode.Token, DragMode.WaypointIdx, Next);
                else this.Callbacks.OnMoveSource?.(DragMode.Token, Next);
                return;
            }

            const PosM = IntersectSliceMetres(Cam, Is2D);
            if (!PosM) return;
            if (DragMode.Kind === 'probe') this.Callbacks.OnMoveProbe?.(PosM);
        });

        window.addEventListener('pointerup', () => {
            DragMode = null;
            Dom.style.cursor = '';
            this.Controls.enabled = this.ViewMode !== '2d';
        });

        Dom.addEventListener('wheel', (E) => {
            const { Is2D } = ResolvePaneAndCamera(E.clientX, E.clientY);
            if (Is2D)
            {
                E.preventDefault();
                this.OrthoZoomMetres = Clamp(this.OrthoZoomMetres * (1 + Math.sign(E.deltaY) * 0.12), 38, 1250);
                this.UpdateBakeWindowFromCamera(true);
                this.NotifyZoomMayHaveChanged();
            }
            else
            {
                setTimeout(() => {
                    this.UpdateBakeWindowFromCamera(false);
                    this.NotifyZoomMayHaveChanged();
                }, 0);
            }
        }, { passive: false });
    }
}
