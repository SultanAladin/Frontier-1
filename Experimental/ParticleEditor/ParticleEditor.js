/*==========================================================================================================================================
                                                           PARTICLEEDITOR.JS
==========================================================================================================================================*/
// 🧩 Frontier · Particle Studio:
//    Unmodified eos WebGPU compute particles, light fibres, molecular physics, lightning & 3D force fields
//    unified under the Sun Inspector + Fluid Editor UI architecture.

import { PE } from './Engine/pe.js';
import './Engine/fibres.js';
import './Engine/presets.js';
import './Engine/shaders.js';
import './Engine/engine.js';
import './Engine/lightning.js';
import './Engine/forcefields.js';
import './Engine/edits.js';
import './Engine/handoff.js';
import { ConstructLatticeReadback, EvaluateFieldAcceleration } from './ParticleFieldReadback.js';
import { SoftwarePreviewEngine, SoftwareLatticeReadback } from './SoftwarePreviewEngine.js';
import * as C from './StudioControls.js';

const V = window.ForceFieldVisualization;
const SampleCapacity = 40;

const Dot3 = (A, B) => A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
const Subtract3 = (A, B) => [A[0] - B[0], A[1] - B[1], A[2] - B[2]];
const Cross3 = (A, B) => [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]];
const Normalize3 = A => { const L = Math.hypot(A[0], A[1], A[2]) || 1; return [A[0] / L, A[1] / L, A[2] / L]; };
const Clamp = (V, Lo, Hi) => Math.min(Hi, Math.max(Lo, V));
const FormatInteger = N => Math.round(N).toLocaleString('en-US');
const Wrap360 = D => ((D % 360) + 360) % 360;

function MultiplyProjection(A, B)
{
    const Out = new Float32Array(16);
    for (let C = 0; C < 4; C++)
        for (let R = 0; R < 4; R++)
        {
            let Sum = 0;
            for (let I = 0; I < 4; I++) Sum += A[I * 4 + R] * B[C * 4 + I];
            Out[C * 4 + R] = Sum;
        }
    return Out;
}

function ConstructLookAt(Eye, Centre, Up)
{
    const F = Normalize3(Subtract3(Centre, Eye));
    const S = Normalize3(Cross3(F, Up));
    const U = Cross3(S, F);
    return Float32Array.of(S[0], U[0], -F[0], 0, S[1], U[1], -F[1], 0, S[2], U[2], -F[2], 0, -Dot3(S, Eye), -Dot3(U, Eye), Dot3(F, Eye), 1);
}

function ConstructPerspective(Fov, Aspect, Near, Far)
{
    const K = 1 / Math.tan(Fov / 2);
    return Float32Array.of(K / Aspect, 0, 0, 0, 0, K, 0, 0, 0, 0, Far / (Near - Far), -1, 0, 0, (Near * Far) / (Near - Far), 0);
}

const CategoryLabels = { 0: 'Streak', 1: 'Wind-driven', 3: 'Molecular · LJ', 4: 'Molecular · reactive', 5: 'VFX', 6: 'Swarm · flocking', 7: 'Light fibres' };
const GroupGlyphs = {
    Transitions: 'Boxes', VFX: 'Flame', 'Wind-blown': 'Leaf', 'Light fibres': 'Zap',
    Water: 'Drop', Ambient: 'Sparkles', Effects: 'Sparkles', Weather: 'Cloud',
    Fields: 'Magnet', Ground: 'Layers', Physics: 'Atom', Storms: 'Tornado', Aircraft: 'Rays', Space: 'Planet'
};
const ForceGlyphs = {
    prevailing: 'Wind', gust: 'Front', tornado: 'Tornado', outflow: 'Blast',
    attract: 'Attract', repel: 'Repel', lift: 'Lift', magnetic: 'Magnet',
    gravity: 'Gravity', orbit: 'Orbit', drag: 'Damp', current: 'Current'
};
const Contributions = { flow: 'Flow', accelerate: 'Accelerate', damp: 'Damp' };
const ContributionColours = { Flow: '#8fd3c4', Accelerate: '#e8c27a', Damp: '#b9a3e8' };
const ContributionUnits = { Flow: 'm/s', Accelerate: 'm/s²', Damp: '1/s' };

const DisplayColour = Linear =>
    '#' + [0, 1, 2].map(Ch => Math.round(Clamp(C.LinearToDisplay(Clamp(Linear[Ch], 0, 1)), 0, 1) * 255).toString(16).padStart(2, '0')).join('');

const CompassFromField = Field => Wrap360(180 - PE.Forces.Bearing(Field));
const FieldFromCompass = Deg => PE.Forces.Along(Wrap360(180 - Deg));

// Curated scene presets for the slanted Fluid document dock
const SceneTabs = [
    { Id: 'default', Name: 'Default Scene', Presets: null },
    { Id: 'storm', Name: 'Storm & Lightning', Presets: ['strikeSparks', 'tornadoDebris', 'leaves', 'fireflies'] },
    { Id: 'fibres', Name: 'Light Fibres', Presets: ['fibres-silk', 'fibres-ribbon', 'fibres-orbit', 'blackhole'] },
    { Id: 'molecular', Name: 'Molecular Lab', Presets: ['ljAtoms', 'chemReact', 'magneticDipole'] }
];

const Scene = {
    Engine: null,
    IsFallback: false,
    Lightning: null,
    Readback: null,
    Effects: [],
    Forces: PE.Forces.DefaultForces(),
    Wind: Object.assign(PE.defaultWind(), { showArrows: false }),
    Camera: { Yaw: 0.62, Pitch: 0.34, Distance: 12.2, Target: [0, 1.5, 0] },
    Playing: true,
    TimeScale: 1,
    Time: 0,
    Step: 0,
    Rate: 0,
    CpuMilliseconds: 0,
    ProbeEnabled: true,
    Probe: [0, 1, 0],
    Turntable: false,
    Web: { Enabled: false, Interval: 0.6, NextAt: 0, Links: 2 },
    Overlay: {
        Enabled: true, Contribution: 'Flow', Quantity: 'Speed', Heatmap: true, Arrows: true, Tracers: false,
        SliceHeight: 1.2, Opacity: 0.38, ArrowSpacing: 34, Minimap: true, Labels: true
    },
    PackedFields: null,
    AdapterName: '—',
    NextNumber: 1,
    Revisions: 0,
    ActiveTab: 'default'
};

const Domain = { Minimum: PE.WIND.min, Size: PE.WIND.size, Dimensions: PE.WIND.dim };
const ClearColour = [0.035, 0.037, 0.045];
const LiveRefreshers = [];
const PersistentRefreshers = [];
const ProbeSamples = [];
const LogEntries = [];

let Selected = 'fx_1';
let InspectorTab = 'overview';
let OutlinerFilter = 'all';
let OutlinerQuery = '';
let QuickLibraryGroup = 'All';
let QuickLibraryQuery = '';
let ModalGroup = 'All';
let ModalQuery = '';
let LogChannel = 'All';
let LogOpen = false;
let Visualization = null;
let PageScope = null;
let OutlinerScope = null;
let InspectorScroller = null;
let OutlinerScroller = null;
let LastTick = 0, LiveTimer = 0, CpuAccumulated = 0, CpuTicks = 0, Held = false, ReadbackTimer = 0;

//------------------------------------------------------------------------------------------------------------------------
//                                                 EFFECTS & ENGINE HELPERS
//------------------------------------------------------------------------------------------------------------------------

const FlowKinds = ['prevailing', 'gust', 'tornado', 'outflow'];
const FlowFields = () => Scene.Forces.filter(F => (PE.Forces.KindById(F.Kind) || {}).Give === PE.Forces.Contribution.Flow);
const EffectByToken = Token => Scene.Effects.find(E => E.Token === Token) || null;
const ForceByToken = Token => Token && Token.startsWith('force:') ? Scene.Forces[Number(Token.slice(6))] || null : null;

function ApplyWindLink(Preset)
{
    const Link = Preset.windLink;
    if (!Link) return;
    const Kind = FlowKinds[Link.type];
    if (!Kind) return;
    let Field = Scene.Forces.find(F => F.Kind === Kind);
    if (!Field)
    {
        if (FlowFields().length >= PE.WIND.maxComps) return;
        Field = { ...PE.Forces.BaseField(Kind), Name: Link.name, Enabled: false, Centre: [0, 0, 0], Radius: 2, Strength: 4, Rate: 0, Direction: PE.Forces.Along(0) };
        Scene.Forces.push(Field);
    }
    const Ch = Link.set || {};
    if (Ch.radius !== undefined) Field.Radius = Ch.radius;
    if (Ch.strength !== undefined) Field.Strength = Ch.strength;
    if (Ch.bearing !== undefined) Field.Direction = PE.Forces.Along(Ch.bearing);
    if (Ch.freq !== undefined) Field.Rate = Ch.freq;
    if (Link.local) { Field.Centre[0] = Preset.p.origin[0]; Field.Centre[2] = Preset.p.origin[2]; }
    Field.Enabled = true;
}

function ConstructEffect(PresetId, Name)
{
    const Preset = PE.presetById(PresetId);
    ApplyWindLink(Preset);
    const Effect = {
        Token: 'fx_' + Scene.NextNumber,
        Number: Scene.NextNumber++,
        PresetId,
        Name: Name || Preset.name,
        P: JSON.parse(JSON.stringify(Preset.p)),
        Gpu: null,
        Accumulated: 0,
        PendingBurst: 0,
        OverrideOrigin: null,
        NeedFill: true,
        Head: 0,
        BurstTimer: undefined,
        AliveSamples: []
    };
    Effect.Gpu = Scene.Engine.createSystem(Effect.P);
    return Effect;
}

function RebuildGpu(Effect)
{
    if (Effect.Gpu) Scene.Engine.destroySystem(Effect.Gpu);
    Effect.Gpu = Scene.Engine.createSystem(Effect.P);
    Object.assign(Effect, { NeedFill: true, Head: 0, Accumulated: 0, PendingBurst: 0 });
}

function AddEffect(PresetId)
{
    const Preset = PE.presetById(PresetId);
    if (!Preset) return null;
    const Same = Scene.Effects.filter(E => E.PresetId === PresetId).length;
    const Effect = ConstructEffect(PresetId, Same ? `${Preset.name} ${Same + 1}` : Preset.name);
    Scene.Effects.push(Effect);
    SelectNode(Effect.Token);
    AppendLog('PARTICLES', `Added ${Effect.Name}`, `${Preset.group} · ${CategoryLabels[Effect.P.kind] || 'Particles'}`, 'live');
    ConstructQuickLibrary();
    C.Broadcast();
    return Effect;
}

function RemoveEffect(Effect)
{
    Scene.Engine.destroySystem(Effect.Gpu);
    Scene.Effects = Scene.Effects.filter(E => E !== Effect);
    SelectNode(Scene.Effects[0]?.Token || 'Field');
    AppendLog('PARTICLES', `Removed ${Effect.Name}`, null, 'warm');
    ConstructQuickLibrary();
    C.Broadcast();
}

function DuplicateEffect(Effect)
{
    const Copy = {
        Token: 'fx_' + Scene.NextNumber,
        Number: Scene.NextNumber++,
        PresetId: Effect.PresetId,
        Name: `${Effect.Name} copy`,
        P: JSON.parse(JSON.stringify(Effect.P)),
        Gpu: null,
        Accumulated: 0,
        PendingBurst: 0,
        OverrideOrigin: null,
        NeedFill: true,
        Head: 0,
        BurstTimer: undefined,
        AliveSamples: []
    };
    Copy.Gpu = Scene.Engine.createSystem(Copy.P);
    Scene.Effects.push(Copy);
    SelectNode(Copy.Token);
    AppendLog('PARTICLES', `Duplicated ${Effect.Name}`, null, 'live');
    C.Broadcast();
}

function LoadSceneTab(TabDef)
{
    Scene.ActiveTab = TabDef.Id;
    for (const E of Scene.Effects) Scene.Engine.destroySystem(E.Gpu);
    Scene.Effects = [];
    Scene.Forces = PE.Forces.DefaultForces();
    Scene.NextNumber = 1;
    const Query = new URLSearchParams(location.search).get('scene');
    const Ids = TabDef.Presets || (Query !== null && TabDef.Id === 'default'
        ? (Query === 'none' || Query === '' ? [] : Query.split(','))
        : PE.defaultScene);
    for (const Id of Ids) if (PE.presetById(Id)) Scene.Effects.push(ConstructEffect(Id));
    Selected = Scene.Effects[0]?.Token || 'Field';
    InspectorTab = 'overview';
    ConstructSceneTabs();
    ConstructOutliner();
    ConstructQuickLibrary();
    ConstructInspectorPage();
    AppendLog('PARTICLES', `Loaded scene '${TabDef.Name}'`, `${Scene.Effects.length} effects`, 'live');
    C.Broadcast();
}

function ResetAll()
{
    for (const Effect of Scene.Effects) RebuildGpu(Effect);
    Scene.Lightning.bolts = [];
    Scene.Lightning.flash = 0;
    Scene.Time = 0;
    Scene.Step = 0;
    AppendLog('PARTICLES', 'Every effect reset and re-seeded', null, 'warm');
}

function ReceiveStrike(Point)
{
    for (const Effect of Scene.Effects)
        if (Effect.PresetId === 'strikeSparks') { Effect.PendingBurst += 160; Effect.OverrideOrigin = Point; }
}

function StrikeNow()
{
    const Hit = Scene.Lightning.strike(Scene.Time);
    ReceiveStrike(Hit);
    AppendLog('LIGHTNING', 'Lightning strike', `at (${Hit.map(P => P.toFixed(1)).join(', ')}) m`, 'warm');
}

function BurstAll()
{
    for (const Effect of Scene.Effects)
    {
        if (Effect.P.kind === 7) continue;
        Effect.PendingBurst += Effect.P.transition ? Effect.Gpu.cap : Math.max(80, Math.round(Effect.Gpu.cap * 0.45));
    }
    AppendLog('PARTICLES', 'Triggered burst on active effects', null, 'live');
}

function SelectNode(Token, ResetTab = true)
{
    Selected = Token;
    if (ResetTab) InspectorTab = 'overview';
    ConstructOutliner();
    ConstructInspectorPage();
    C.Broadcast();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   CAMERA & PACKING
//------------------------------------------------------------------------------------------------------------------------

function ResolveCamera(Aspect)
{
    const Cam = Scene.Camera;
    const Cos = Math.cos(Cam.Pitch), Sin = Math.sin(Cam.Pitch);
    const Eye = [
        Cam.Target[0] + Cam.Distance * Cos * Math.sin(Cam.Yaw),
        Cam.Target[1] + Cam.Distance * Sin,
        Cam.Target[2] + Cam.Distance * Cos * Math.cos(Cam.Yaw)
    ];
    const View = ConstructLookAt(Eye, Cam.Target, [0, 1, 0]);
    const Proj = ConstructPerspective((45 * Math.PI) / 180, Aspect, 0.05, 400);
    const Fwd = Normalize3(Subtract3(Cam.Target, Eye));
    const Right = Normalize3(Cross3(Fwd, [0, 1, 0]));
    return { ViewProjection: MultiplyProjection(Proj, View), Eye, Right, Up: Cross3(Right, Fwd) };
}

function ConstructLines()
{
    const Out = [];
    const Line = (A, B, Col) => Out.push(A[0], A[1], A[2], ...Col, B[0], B[1], B[2], ...Col);
    const Cross = (C, S, Col) =>
    {
        Line([C[0] - S, C[1], C[2]], [C[0] + S, C[1], C[2]], Col);
        Line([C[0], C[1] - S, C[2]], [C[0], C[1] + S, C[2]], Col);
        Line([C[0], C[1], C[2] - S], [C[0], C[1], C[2] + S], Col);
    };
    const Box = (Lo, Hi, Col) =>
    {
        const Pts = [
            [Lo[0], Lo[1], Lo[2]], [Hi[0], Lo[1], Lo[2]], [Hi[0], Lo[1], Hi[2]], [Lo[0], Lo[1], Hi[2]],
            [Lo[0], Hi[1], Lo[2]], [Hi[0], Hi[1], Lo[2]], [Hi[0], Hi[1], Hi[2]], [Lo[0], Hi[1], Hi[2]]
        ];
        for (const [A, B] of [[0, 1], [1, 2], [2, 3], [3, 0], [4, 5], [5, 6], [6, 7], [7, 4], [0, 4], [1, 5], [2, 6], [3, 7]]) Line(Pts[A], Pts[B], Col);
    };
    if (Scene.Wind.showDomain)
        Box(Domain.Minimum, [Domain.Minimum[0] + Domain.Size[0], Domain.Minimum[1] + Domain.Size[1], Domain.Minimum[2] + Domain.Size[2]], [0.45, 0.6, 0.85, 0.55]);
    for (const E of Scene.Effects)
    {
        if (!E.P.visible) continue;
        const O = E.P.origin, Chosen = Selected === E.Token;
        Cross(O, 0.14, Chosen ? [0.95, 0.78, 0.35, 1] : [0.6, 0.65, 0.75, 0.55]);
        if (E.P.kind === 3 || E.P.kind === 4)
        {
            const H = E.P.boxHalf[0];
            Box([O[0] - H, O[1] - H, O[2] - H], [O[0] + H, O[1] + H, O[2] + H], Chosen ? [0.95, 0.78, 0.35, 0.9] : [0.5, 0.88, 1, 0.38]);
        }
        if (E.P.emitShape === 3 && E.P.kind < 3)
        {
            const H = E.P.boxHalf;
            Box([O[0] - H[0], O[1] - H[1], O[2] - H[2]], [O[0] + H[0], O[1] + H[1], O[2] + H[2]], [0.6, 0.7, 0.9, 0.3]);
        }
    }
    if (Scene.ProbeEnabled) Cross(Scene.Probe, 0.18, [0.95, 0.88, 0.35, 1]);
    return new Float32Array(Out);
}

function ResolveProbeVoxel()
{
    const Cell = Domain.Size.map((S, I) => S / Domain.Dimensions[I]);
    return {
        ix: Clamp(Math.floor((Scene.Probe[0] - Domain.Minimum[0]) / Cell[0]), 0, Domain.Dimensions[0] - 1),
        iy: Clamp(Math.floor((Scene.Probe[1] - Domain.Minimum[1]) / Cell[1]), 0, Domain.Dimensions[1] - 1),
        iz: Clamp(Math.floor((Scene.Probe[2] - Domain.Minimum[2]) / Cell[2]), 0, Domain.Dimensions[2] - 1)
    };
}

function PackFields()
{
    const Out = new Float32Array(128);
    let Count = 0;
    const Put = (Type, Pos, F) =>
    {
        if (Count >= 8) return;
        const O = (1 + 3 * Count) * 4;
        Out.set([Pos[0], Pos[1], Pos[2], F.radius], O);
        Out.set([Type, F.strength, F.swirl || 0, F.swallow || 0], O + 4);
        Out.set([F.t0 || 0, F.duration, F.period || 0, 0], O + 8);
        Count++;
    };
    for (const E of Scene.Effects)
    {
        const A = E.P.attractor;
        if (E.P.visible && A && A.enabled) Put(0, E.P.origin, { radius: A.radius, strength: A.strength, swirl: A.swirl, swallow: A.swallow, t0: 0, duration: 1e9, period: 0 });
    }
    for (const E of Scene.Effects)
    {
        const M = E.P.magneticField;
        if (E.P.visible && M) Put(3, E.P.origin, { radius: M.radius, strength: M.strength, swirl: M.guide, swallow: 0, t0: 0, duration: 1e9, period: 0 });
    }
    for (const Entry of PE.Forces.ForGpu(Scene.Forces, Scene.Time).Forces) Put(Entry.type, Entry.pos, Entry);
    Out[0] = Count;
    Out[1] = Scene.Time;
    return Out;
}

function PackLens(Camera, Aspect)
{
    const Out = new Float32Array(32);
    let Holes = 0, Shimmers = 0;
    const VP = Camera.ViewProjection;
    const Scale = 0.5 / Math.tan(Math.PI / 8);
    const Project = Origin =>
    {
        const Clip = [0, 1, 2, 3].map(R => VP[R] * Origin[0] + VP[4 + R] * Origin[1] + VP[8 + R] * Origin[2] + VP[12 + R]);
        if (Clip[3] <= 0.01) return null;
        const Dist = Math.hypot(Origin[0] - Camera.Eye[0], Origin[1] - Camera.Eye[1], Origin[2] - Camera.Eye[2]);
        return { U: (Clip[0] / Clip[3]) * 0.5 + 0.5, V: 0.5 - (Clip[1] / Clip[3]) * 0.5, K: Scale / Math.max(Dist, 0.01) };
    };
    for (const E of Scene.Effects)
    {
        const Hole = E.P.blackHole;
        if (!E.P.visible || !Hole || Holes >= 2) continue;
        const Pt = Project(E.P.origin);
        if (!Pt) continue;
        Out.set([Pt.U, Pt.V, Hole.horizon * (Hole.einstein || 0) * Pt.K, Hole.horizon * Pt.K], Holes * 4);
        Holes++;
    }
    for (const E of Scene.Effects)
    {
        const Sh = E.P.heatShimmer;
        if (!E.P.visible || !Sh || Shimmers >= 2) continue;
        const Pt = Project(E.P.origin);
        if (!Pt) continue;
        Out.set([Pt.U, Pt.V, Sh.radius * Pt.K, Sh.strength], 12 + Shimmers * 8);
        Out.set([Scene.Time * Sh.rate, Sh.freq, 0, 0], 16 + Shimmers * 8);
        Shimmers++;
    }
    Out[8] = Holes; Out[9] = Aspect; Out[10] = 0.35; Out[11] = Shimmers;
    return { Count: Holes, ShimmerCount: Shimmers, Packed: Out };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRAME LOOP
//------------------------------------------------------------------------------------------------------------------------

function Tick(Now)
{
    if (Held) return;
    requestAnimationFrame(Tick);
    if (document.hidden) { LastTick = 0; return; }
    Advance(Now);
}

function Advance(Now)
{
    const Started = performance.now();
    const RealDelta = LastTick ? Math.min(0.05, (Now - LastTick) / 1000) : 0.016;
    LastTick = Now;
    if (RealDelta > 0) Scene.Rate = Scene.Rate * 0.9 + (1 / RealDelta) * 0.1;
    const Delta = Scene.Playing ? RealDelta * Scene.TimeScale : 0;
    Scene.Time += Delta;
    Scene.Step++;
    if (Scene.Turntable && Scene.Playing) Scene.Camera.Yaw += RealDelta * 0.28;

    for (const Hit of Scene.Lightning.update(Delta, Scene.Time)) ReceiveStrike(Hit);
    if (Scene.Web.Enabled && Scene.Time >= Scene.Web.NextAt)
    {
        const Origins = Scene.Effects.filter(E => E.P.visible).map(E => [E.P.origin[0], E.P.origin[1], E.P.origin[2]]);
        if (Origins.length >= 2) Scene.Lightning.web(Scene.Time, Origins, { k: Scene.Web.Links, life: Scene.Web.Interval * 0.9 });
        Scene.Web.NextAt = Scene.Time + Scene.Web.Interval;
    }

    const Engine = Scene.Engine;
    const Canvas = Engine.canvas;
    const Aspect = Canvas.width / Math.max(1, Canvas.height);
    const Camera = ResolveCamera(Aspect);
    const Global = new Float32Array(52);
    const Off = PE.GLOB_OFF;
    Global.set(Camera.ViewProjection, Off.viewProj);
    Global.set([Camera.Right[0], Camera.Right[1], Camera.Right[2], 0], Off.camRight);
    Global.set([Camera.Up[0], Camera.Up[1], Camera.Up[2], 0], Off.camUp);
    Global.set([Camera.Eye[0], Camera.Eye[1], Camera.Eye[2], 1], Off.camPos);
    Global.set([Domain.Minimum[0], Domain.Minimum[1], Domain.Minimum[2], 0], Off.windMin);
    Global.set([Domain.Size[0], Domain.Size[1], Domain.Size[2], 0], Off.windSize);
    const Flow = PE.Forces.ForGpu(Scene.Forces).Lattice;
    Global.set([Domain.Dimensions[0], Domain.Dimensions[1], Domain.Dimensions[2], Flow.length], Off.windDim);
    Global.set([Scene.Time, Delta, Scene.Step, Scene.Wind.windScale], Off.timing);
    Global.set([Scene.Wind.arrowRef, 0.012, Scene.Lightning.flash, Scene.Wind.turbulence], Off.viz);
    Global.set([Scene.Wind.swirl, 0.45, 0.22, Scene.Wind.arrowStride || 3], Off.swirl);

    const Components = new Float32Array(PE.WIND.maxComps * 8);
    Flow.slice(0, PE.WIND.maxComps).forEach((Comp, I) =>
    {
        Components.set([Comp.x, Comp.z, Math.max(0.5, Comp.radius), Comp.type, Comp.strength, (Comp.bearing * Math.PI) / 180, Comp.freq, Comp.enabled ? 1 : 0], I * 8);
    });

    const Jobs = [];
    for (const E of Scene.Effects)
    {
        if (!E.P.visible || !E.Gpu) continue;
        const P = E.P, Gpu = E.Gpu;
        if (P.kind === 7)
        {
            const Fibre = P.fibre;
            const Key = Fibre.pathShape + '|' + Fibre.pathSize;
            let Path = null;
            if (Gpu.pathKey !== Key) { Gpu.pathKey = Key; Path = PE.samplePath(Fibre.pathShape, Fibre.pathSize, PE.PATH_SAMPLES); }
            PE.fillFibre(Gpu.fibreCpu, P, { lf: (Scene.Time / Fibre.loopSeconds) % 1, projScale: (0.5 * Canvas.height) / Math.tan(Math.PI / 8), viewW: Canvas.width, viewH: Canvas.height });
            Jobs.push({ gpu: Gpu, cpu: Gpu.fibreCpu, path: Path, fibre: true, simulate: false, draw: false, strands: Fibre.strands, segments: Fibre.segments, sparks: Fibre.sparks > 0, alpha: false, mol: false, emitN: 0, steps: 0, gd: 1 });
            continue;
        }
        const Molecular = P.kind === 3 || P.kind === 4;
        const Simulate = Scene.Playing && Delta > 0;
        let Release = 0, Steps = 0, StepDelta = Delta, Head = E.Head, Origin = P.origin;
        if (Simulate)
        {
            if (P.burstEvery > 0)
            {
                E.BurstTimer = (E.BurstTimer ?? 0) - Delta;
                if (E.BurstTimer <= 0)
                {
                    E.BurstTimer = P.burstEvery * (0.7 + 0.6 * Math.random());
                    E.PendingBurst += P.burstCount;
                    E.OverrideOrigin = P.burstAtOrigin ? null : [-3 + 6 * Math.random(), 3.5 + 2.5 * Math.random(), -3 + 6 * Math.random()];
                }
            }
            if (E.OverrideOrigin) { Origin = E.OverrideOrigin; E.OverrideOrigin = null; }
            if (Molecular)
            {
                Steps = Clamp(Math.round(Delta / P.simDt), 1, 8);
                StepDelta = Math.min(Delta / Steps, P.simDt * 1.25);
                if (E.NeedFill) { Release = Gpu.cap; E.NeedFill = false; Head = 0; }
            }
            else
            {
                E.Accumulated += P.rate * Delta;
                Release = Math.floor(E.Accumulated);
                E.Accumulated -= Release;
                Release = Math.min(Gpu.cap, Release + E.PendingBurst);
                E.PendingBurst = 0;
                E.Head = (Head + Release) % Gpu.cap;
            }
        }
        const Half = P.boxHalf[0];
        const Cutoff = 2.5 * P.sigma;
        const GridDim = Molecular ? Clamp(Math.floor((2 * Half) / Cutoff), 1, PE.MAX_GD) : 1;
        PE.fillSys(Gpu.cpu, P, {
            origin: Origin, dt: StepDelta, time: Scene.Time, frame: Scene.Step, cap: Gpu.cap, emitN: Release, head: Head,
            seed: (Scene.Step * 7919 + E.Number * 104729) >>> 0, mol: Molecular, gd: GridDim, cellSize: Molecular ? (2 * Half) / GridDim : 1
        });
        Jobs.push({
            gpu: Gpu, cpu: Gpu.cpu, simulate: Simulate, mol: Molecular, emitN: Release, steps: Steps, gd: GridDim, swarm: P.kind === 6,
            readStats: Simulate && Scene.Step % 3 === 0, draw: true, alpha: P.blend === 'alpha', cube: P.shape === 4,
            shatter: !!(P.transition && P.transition.fragment), fly: !!(P.transition && P.transition.after === 'butterfly')
        });
    }

    const Segments = Scene.Lightning.pack(Scene.Time);
    const Lines = ConstructLines();
    const Probe = Scene.ProbeEnabled && !Engine.probePending && Scene.Step % 4 === 0 ? ResolveProbeVoxel() : null;
    const Lens = PackLens(Camera, Aspect);
    Scene.PackedFields = PackFields();

    Engine.render({
        glob: Global, fields: Scene.PackedFields, lensCount: Lens.Count + Lens.ShimmerCount, lens: Lens.Packed, comps: Components,
        clear: ClearColour.map(Ch => Ch + Scene.Lightning.flash * 0.22), floor: Scene.Wind.showFloor, arrows: Scene.Wind.showArrows,
        lines: Lines, lineCount: Lines.length / 7, segCount: Segments, segData: Scene.Lightning.dataBuf, jobs: Jobs, probe: Probe
    });

    if (Now - ReadbackTimer > 150 && Scene.Overlay.Enabled && Scene.Readback) { ReadbackTimer = Now; Scene.Readback.Request(); }
    RenderOverlay(Camera);

    UpdateLiveTransport();
    CpuAccumulated += performance.now() - Started;
    CpuTicks++;
    if (Now - LiveTimer > 250)
    {
        LiveTimer = Now;
        Scene.CpuMilliseconds = CpuTicks ? CpuAccumulated / CpuTicks : 0;
        CpuAccumulated = 0; CpuTicks = 0;
        RefreshLive();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   FIELD OVERLAY
//------------------------------------------------------------------------------------------------------------------------

const InsideDomain = (X, Y, Z) => X >= Domain.Minimum[0] && X <= Domain.Minimum[0] + Domain.Size[0] && Z >= Domain.Minimum[2] && Z <= Domain.Minimum[2] + Domain.Size[2];

function SampleParticleField(Contribution, X, Y, Z, Time, CellSpan, Out)
{
    if (Contribution === 'Flow') return Scene.Readback ? Scene.Readback.Sample(X, Y, Z, Out) : InsideDomain(X, Y, Z);
    if (Contribution === 'Accelerate') { EvaluateFieldAcceleration(Scene.PackedFields, X, Y, Z, Out); return InsideDomain(X, Y, Z); }
    for (const Field of Scene.Forces)
        if (Field.Kind === 'drag' && Field.Enabled) Out[0] += PE.Forces.SampleField(Field, [X, Y, Z], Time)[0];
    return InsideDomain(X, Y, Z);
}

function DescribeForces()
{
    const List = [];
    Scene.Forces.forEach((Field, Index) =>
    {
        const Kind = PE.Forces.KindById(Field.Kind);
        if (!Kind) return;
        const Contribution = Contributions[Kind.Give];
        const Token = 'force:' + Index;
        const Shape = Field.Reaches === 'everywhere' ? 'Everywhere' : Field.Reaches === 'box' ? 'Box' : 'Circle';
        List.push({
            Id: Token, X: Field.Centre[0], Y: Field.Centre[1], Z: Field.Centre[2], Radius: Field.Radius, Shape, HalfX: Field.Radius, HalfZ: Field.Radius,
            Contribution, Label: Field.Name, Active: Field.Enabled && PE.Forces.Awake(Field, Scene.Time) ? 1 : 0, Selected: Selected === Token,
            Dim: Contribution !== Scene.Overlay.Contribution, Bearing: Kind.Give === 'flow' && Field.Kind !== 'outflow' && Field.Kind !== 'tornado' ? CompassFromField(Field) : undefined
        });
    });
    for (const E of Scene.Effects)
    {
        if (!E.P.visible) continue;
        if (E.P.attractor && E.P.attractor.enabled)
            List.push({ Id: E.Token, X: E.P.origin[0], Y: E.P.origin[1], Z: E.P.origin[2], Radius: E.P.attractor.radius, Shape: 'Circle',
                        Contribution: 'Accelerate', Label: `${E.Name} · pull`, Active: 1, Selected: Selected === E.Token, Dim: Scene.Overlay.Contribution !== 'Accelerate' });
        if (E.P.magneticField)
            List.push({ Id: E.Token, X: E.P.origin[0], Y: E.P.origin[1], Z: E.P.origin[2], Radius: E.P.magneticField.radius, Shape: 'Circle',
                        Contribution: 'Accelerate', Label: `${E.Name} · dipole`, Active: 1, Selected: Selected === E.Token, Dim: Scene.Overlay.Contribution !== 'Accelerate' });
    }
    return List;
}

function ConstructOverlay()
{
    if (!V) return;
    const Canvas = document.getElementById('OverlayCanvas');
    Visualization = V.Create(Canvas, {
        Sample: SampleParticleField,
        OnMinimapPick: (X, Z) =>
        {
            Scene.Camera.Target[0] = Clamp(X, -6, 6);
            Scene.Camera.Target[2] = Clamp(Z, -6, 6);
            AppendLog('VIEW', 'Camera focused via minimap', `(${X.toFixed(1)}, ${Z.toFixed(1)}) m`, 'live');
        }
    });
    Visualization.Configure({
        Extent: { MinimumX: Domain.Minimum[0], MinimumZ: Domain.Minimum[2], MaximumX: Domain.Minimum[0] + Domain.Size[0], MaximumZ: Domain.Minimum[2] + Domain.Size[2] },
        ClipToExtent: true, CameraCells: 40, ShowScaleBar: false, ShowGroundGrid: false, Minimap: document.getElementById('MinimapCanvas'), RefreshInterval: 100
    });
    Visualization.View.Mode = 'Camera';
}

function RenderOverlay(Camera)
{
    if (!Visualization) return;
    const Overlay = Scene.Overlay;
    const Canvas = document.getElementById('OverlayCanvas');
    document.getElementById('MinimapInset').classList.toggle('shown', Overlay.Enabled && Overlay.Minimap);
    if (!Overlay.Enabled)
    {
        if (Canvas.dataset.cleared !== 'yes') { Canvas.getContext('2d').clearRect(0, 0, Canvas.width, Canvas.height); Canvas.dataset.cleared = 'yes'; }
        return;
    }
    Canvas.dataset.cleared = '';
    Visualization.View.ViewProjection = Camera.ViewProjection;
    Visualization.Configure({
        Contribution: Overlay.Contribution, Quantity: Overlay.Quantity, Heatmap: Overlay.Heatmap, Arrows: Overlay.Arrows, Tracers: Overlay.Tracers,
        TracerCount: 220, SliceHeight: Overlay.SliceHeight, Opacity: Overlay.Opacity, ArrowSpacing: Overlay.ArrowSpacing, ShowLabels: Overlay.Labels,
        Sources: DescribeForces(), ProbePoint: Scene.ProbeEnabled ? [Scene.Probe[0], Scene.Probe[2]] : null,
        Minimap: Overlay.Minimap ? document.getElementById('MinimapCanvas') : null
    });
    Visualization.Render(Scene.Time);
}

//------------------------------------------------------------------------------------------------------------------------
//                                               LOG STREAM DRAWER
//------------------------------------------------------------------------------------------------------------------------

function AppendLog(Channel, Message, Detail, Tone = 'live')
{
    const D = new Date();
    const Time = [D.getHours(), D.getMinutes(), D.getSeconds()].map(N => String(N).padStart(2, '0')).join(':');
    LogEntries.unshift({ Time, Channel, Message, Detail, Tone });
    if (LogEntries.length > 80) LogEntries.length = 80;
    const Badge = document.getElementById('nav-log-count');
    if (Badge) Badge.textContent = String(LogEntries.length);
    RefreshLogDrawer();
}

function RefreshLogDrawer()
{
    const Head = document.getElementById('LogHead');
    const Content = document.getElementById('LogContent');
    if (!Head || !Content) return;
    Head.textContent = '';
    for (const Ch of ['All', 'PARTICLES', 'FORCES', 'WIND', 'LIGHTNING', 'VIEW'])
    {
        const Btn = C.Construct('button', 'log-pill' + (LogChannel === Ch ? ' active' : ''), Ch === 'All' ? `All (${LogEntries.length})` : Ch);
        Btn.type = 'button';
        Btn.addEventListener('click', () => { LogChannel = Ch; RefreshLogDrawer(); });
        Head.append(Btn);
    }
    Content.textContent = '';
    const Filtered = LogEntries.filter(E => LogChannel === 'All' || E.Channel === LogChannel);
    for (const E of Filtered.slice(0, 40))
    {
        Content.append(C.Construct('div', 'log-row', [
            C.Construct('span', 'log-time tabular', E.Time),
            C.Construct('span', 'log-chan ' + (E.Tone || ''), E.Channel),
            C.Construct('span', null, E.Detail ? `${E.Message} — ${E.Detail}` : E.Message)
        ]));
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              SLANTED SCENE TABS & HEADER
//------------------------------------------------------------------------------------------------------------------------

function ConstructSceneTabs()
{
    const Host = document.getElementById('scene-tabs');
    Host.textContent = '';
    for (const TabDef of SceneTabs)
    {
        const Btn = C.Construct('button', 'document-tab' + (Scene.ActiveTab === TabDef.Id ? ' active' : ''), [
            C.Construct('span', 'document-label', TabDef.Name)
        ]);
        Btn.type = 'button';
        Btn.addEventListener('click', () => LoadSceneTab(TabDef));
        Host.append(Btn);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                         OUTLINER + EMBEDDED PRESET LIBRARY
//------------------------------------------------------------------------------------------------------------------------

function ConstructOutliner()
{
    if (OutlinerScope) C.ReleaseScope(OutlinerScope);
    OutlinerScope = C.OpenScope();
    const Content = document.getElementById('OutlinerContent');
    Content.textContent = '';

    const MakeGroup = (Title, Count) =>
    {
        const Box = C.Construct('div', 'tree-group', [
            C.Construct('div', 'tree-group-title', [C.ConstructGlyph('Chevron'), C.Construct('span', null, Title), C.Construct('span', 'group-count tabular', String(Count))])
        ]);
        return Box;
    };

    const AddRow = (Host, Row) =>
    {
        const El = C.Construct('div', 'tree-row' + (Row.Depth ? ' nested' : ''));
        El.style.setProperty('--row-accent', Row.Colour());
        const GlyphBox = C.Construct('div', 'row-glyph', C.ConstructGlyph(Row.Glyph));
        const NameEl = C.Construct('div', 'row-name', Row.Name());
        const MetaEl = C.Construct('div', 'row-meta', Row.Meta());
        const Btn = C.Construct('button', 'row-select', [GlyphBox, C.Construct('div', 'row-copy', [NameEl, MetaEl])]);
        Btn.type = 'button';
        Btn.addEventListener('click', () => SelectNode(Row.Id));

        const Dot = C.Construct('span', 'row-state-dot');
        const EyeBtn = C.Construct('button', 'row-eye', C.ConstructGlyph('Eye'));
        EyeBtn.type = 'button';
        EyeBtn.title = 'Toggle visibility / active';
        EyeBtn.addEventListener('click', Ev => { Ev.stopPropagation(); Row.Flip(); C.Broadcast(); });

        El.append(Btn, Dot, EyeBtn);
        Host.append(El);

        C.Register(() =>
        {
            const On = Row.Enabled();
            El.classList.toggle('selected', Selected === Row.Id);
            El.classList.toggle('is-off', !On);
            Dot.classList.toggle('live', On);
            NameEl.textContent = Row.Name();
            MetaEl.textContent = Row.Meta();
            EyeBtn.textContent = '';
            EyeBtn.append(C.ConstructGlyph(On ? 'Eye' : 'EyeOff'));
            const Match = !OutlinerQuery || `${Row.Name()} ${Row.Meta()}`.toLowerCase().includes(OutlinerQuery);
            El.hidden = !Match;
        });
    };

    const TotalCount = Scene.Effects.length + Scene.Forces.length + 3;
    document.getElementById('outliner-subtitle').textContent = `Particle scene · ${TotalCount} nodes`;

    if (OutlinerFilter === 'all' || OutlinerFilter === 'forces' || OutlinerFilter === 'lightning')
    {
        const EnvGroup = MakeGroup('Environment', Scene.Forces.length + 3);
        if (OutlinerFilter === 'all' || OutlinerFilter === 'forces')
        {
            AddRow(EnvGroup, {
                Id: 'Field', Name: () => 'Force Field', Glyph: 'Wind', Colour: () => '#8fd3c4', Depth: 0,
                Meta: () => `Master · ${Scene.Forces.length} forces · overlay ${Scene.Overlay.Enabled ? 'on' : 'off'}`,
                Enabled: () => Scene.Overlay.Enabled,
                Flip: () => { Scene.Overlay.Enabled = !Scene.Overlay.Enabled; AppendLog('VIEW', `Field overlay ${Scene.Overlay.Enabled ? 'on' : 'off'}`, null, 'live'); }
            });
            AddRow(EnvGroup, {
                Id: 'Wind', Name: () => 'Wind Lattice', Glyph: 'Lattice', Colour: () => '#9fc7e8', Depth: 1,
                Meta: () => `24×12×24 · scale ×${Scene.Wind.windScale.toFixed(2)}`,
                Enabled: () => Scene.Wind.windScale > 0,
                Flip: () => { Scene.Wind.windScale = Scene.Wind.windScale > 0 ? 0 : 1; AppendLog('WIND', `Wind scale ×${Scene.Wind.windScale}`, null, 'live'); }
            });
            Scene.Forces.forEach((Field, Index) =>
            {
                const Kind = PE.Forces.KindById(Field.Kind);
                if (!Kind) return;
                const Contrib = Contributions[Kind.Give];
                AddRow(EnvGroup, {
                    Id: 'force:' + Index, Name: () => Field.Name, Glyph: ForceGlyphs[Field.Kind] || 'Target',
                    Colour: () => ContributionColours[Contrib], Depth: 1,
                    Meta: () => `${Contrib} · ${Number(Field.Strength.toFixed(1))} ${ContributionUnits[Contrib]}`,
                    Enabled: () => Field.Enabled,
                    Flip: () => { Field.Enabled = !Field.Enabled; AppendLog('FORCES', `${Field.Name} ${Field.Enabled ? 'enabled' : 'disabled'}`, null, 'live'); }
                });
            });
        }
        if (OutlinerFilter === 'all' || OutlinerFilter === 'lightning')
        {
            AddRow(EnvGroup, {
                Id: 'Lightning', Name: () => 'Lightning & Storm', Glyph: 'Zap', Colour: () => '#c9b8f0', Depth: 0,
                Meta: () => `${FormatInteger(Scene.Lightning.strikes)} strikes · ${Scene.Lightning.auto ? 'auto' : 'manual'}`,
                Enabled: () => Scene.Lightning.auto,
                Flip: () => { Scene.Lightning.auto = !Scene.Lightning.auto; AppendLog('LIGHTNING', `Auto strikes ${Scene.Lightning.auto ? 'on' : 'off'}`, null, 'live'); }
            });
        }
        Content.append(EnvGroup);
    }

    if (OutlinerFilter === 'all' || OutlinerFilter === 'effects')
    {
        const FxGroup = MakeGroup('Particle Effects', Scene.Effects.length);
        for (const Effect of Scene.Effects)
        {
            const Preset = PE.presetById(Effect.PresetId);
            AddRow(FxGroup, {
                Id: Effect.Token,
                Name: () => Effect.Name,
                Glyph: GroupGlyphs[Preset?.group] || 'Sparkles',
                Colour: () => DisplayColour(Effect.P.colA),
                Depth: 0,
                Meta: () => Effect.P.kind === 7
                    ? `${FibreSummary(Effect)} · strands`
                    : `${CategoryLabels[Effect.P.kind] || 'Particles'} · ${FormatInteger(Effect.Gpu?.stats_?.alive || 0)} alive`,
                Enabled: () => Effect.P.visible,
                Flip: () => { Effect.P.visible = !Effect.P.visible; AppendLog('PARTICLES', `${Effect.Name} ${Effect.P.visible ? 'shown' : 'hidden'}`, null, 'live'); }
            });
        }
        Content.append(FxGroup);
    }

    C.Register(() =>
    {
        const Vis = Scene.Effects.filter(E => E.P.visible).length + Scene.Forces.filter(F => F.Enabled).length;
        const Tot = Scene.Effects.length + Scene.Forces.length;
        document.getElementById('visible-count').textContent = String(Vis);
        document.getElementById('hidden-count').textContent = String(Tot - Vis);
        document.getElementById('status-effects').textContent = `${Scene.Effects.length} effects · ${Scene.Forces.length} forces`;
    });

    C.CloseScope(OutlinerScope);
}

const FibreSummary = Effect => FormatInteger(Effect.P.fibre?.strands || 0);

//------------------------------------------------------------------------------------------------------------------------
//                                       EMBEDDED + MODAL 52-PRESET LIBRARY
//------------------------------------------------------------------------------------------------------------------------

function ConstructQuickLibrary()
{
    // Preset browsing moved out to the shared Asset Browser.
    const FilterHost = document.getElementById('quick-preset-filters');
    const ListHost = document.getElementById('quick-presets-list');
    FilterHost.style.display = 'none';
    const Search = document.getElementById('quick-preset-search');
    if (Search && Search.closest('label')) Search.closest('label').style.display = 'none';
    document.getElementById('quick-preset-count').textContent = String(PE.Presets.length);
    ListHost.textContent = '';
    const Btn = C.Construct('button', 'preset-card', [
        C.Construct('span', 'preset-swatch', C.ConstructGlyph('Sparkles')),
        C.Construct('span', 'preset-copy', [
            C.Construct('strong', null, 'Browse particle assets\u2026'),
            C.Construct('small', null, `${PE.Presets.length} presets \u00B7 shared Asset Browser`)
        ]),
        C.Construct('span', 'preset-arrow', '\u2197')
    ]);
    Btn.type = 'button';
    Btn.addEventListener('click', () => { location.href = '../AssetBrowser/?ws=Particle%20Lab'; });
    ListHost.append(Btn);
}

function OpenFullLibrary()
{
    // The 52-preset modal moved out to the shared Asset Browser.
    location.href = '../AssetBrowser/?ws=Particle%20Lab';
}

/* Asset Browser deep link: ?asset=particle/<preset-id> adds the effect to the scene. */
function HandleAssetDeepLink()
{
    const Raw = new URLSearchParams(location.search).get('asset') || '';
    const M = Raw.match(/^particle\/([A-Za-z0-9-]+)$/);
    if (!M) return;
    if (PE.presetById(M[1])) AddEffect(M[1]);
    else AppendLog('PARTICLES', `Asset \u201C${Raw}\u201D is not a particle preset`, null, 'warn');
}

const BindEffect = (Effect, Target, Key, Extra = {}) => ({
    Read: () => Target[Key],
    Write: Value =>
    {
        const Label = Extra.Label || Key;
        PE.Edits.Apply(Label, () => { Target[Key] = Value; });
        if (Extra.Rebuild) RebuildGpu(Effect);
        AppendLog('PARTICLES', `${Effect.Name}: ${Label}`, String(typeof Value === 'number' ? Number(Value.toFixed(3)) : Value), 'live');
    },
    ...Extra
});

const BindScene = (Target, Key, Extra = {}, Channel = 'PARTICLES', Owner = 'Scene') => ({
    Read: () => Target[Key],
    Write: Value =>
    {
        Target[Key] = Value;
        AppendLog(Channel, `${Owner}: ${Extra.Label || Key}`, String(typeof Value === 'number' ? Number(Value.toFixed(3)) : Value), 'live');
    },
    ...Extra
});

const Range = (Host, Binding, Min, Max, Step, Digits, Unit, Extra) =>
    C.SliderRow(Host, Object.assign(Binding, { Minimum: Min, Maximum: Max, Step, Digits, Unit }, Extra || {}));

function RenderInspectorTabs(Defs)
{
    const Bar = document.getElementById('inspector-tabs');
    Bar.textContent = '';
    if (!Defs.some(D => D[0] === InspectorTab)) InspectorTab = Defs[0][0];
    for (const [Id, Label] of Defs)
    {
        const Btn = C.Construct('button', InspectorTab === Id ? 'active' : '', Label);
        Btn.type = 'button';
        Btn.addEventListener('click', () => { InspectorTab = Id; ConstructInspectorPage(); C.Broadcast(); });
        Bar.append(Btn);
    }
}

function ConstructEffectInspector(Page, Effect)
{
    const P = Effect.P;
    const Preset = PE.presetById(Effect.PresetId);
    const Molecular = P.kind === 3 || P.kind === 4, Fibre = P.kind === 7;
    PE.Edits.For(Effect);

    const TabDefs = [
        ['overview', 'Overview'],
        ['source', Fibre ? 'Form & Path' : 'Source'],
        ['motion', Fibre ? 'Glow & Colour' : 'Motion & FX'],
        ['readback', Fibre ? 'Sparks & Record' : 'Modules & Record']
    ];
    RenderInspectorTabs(TabDefs);

    if (InspectorTab === 'overview')
    {
        // Card 01: Sun Inspector Hero Card + Glowing Sparkline
        const Mark = C.Construct('div', 'mark live', C.ConstructGlyph('Check'));
        const MarkText = C.Construct('span');
        const Metric = C.Construct('div', 'metric-big');
        const Rate = C.Construct('div', 'rate');
        const Low = C.Construct('div', 'low');
        const Card = C.Construct('div', 'hero-card', [
            C.Construct('div', 'hero-top', [
                C.Construct('div', 'budget-chip', [Mark, MarkText]),
                C.Construct('div', 'estimate-tag', Fibre ? 'ANALYTIC FIBRE' : '01 / LIVE SOLVER')
            ]),
            C.Construct('div', 'hero-title', Fibre ? 'Strands rendered' : 'Particles alive'),
            C.Construct('div', 'hero-metrics', [Metric, C.Construct('div', 'metric-side', [Rate, Low])])
        ]);
        Page.append(Card);

        const Series = C.TimeSeries(Card, {
            Samples: Effect.AliveSamples, Capacity: SampleCapacity,
            Target: () => Fibre ? P.fibre.strands : Effect.Gpu.cap, Height: 96, Unit: '',
            TargetLabel: Fibre ? 'Strands' : 'Capacity', TargetDigits: 0, Headroom: 1.12
        });
        LiveRefreshers.push(Series.Refresh);
        LiveRefreshers.push(() =>
        {
            const Alive = Fibre ? P.fibre.strands : (Effect.Gpu?.stats_?.alive ?? 0);
            Metric.textContent = FormatInteger(Alive);
            Rate.textContent = Fibre ? `${P.fibre.loopSeconds.toFixed(0)} s` : Molecular ? 'fill' : `${FormatInteger(P.rate)}`;
            Rate.append(C.Construct('small', null, Fibre ? 'loop' : Molecular ? 'locked' : '/s'));
            Low.textContent = Fibre ? `${FormatInteger(P.fibre.segments)} segs` : `of ${FormatInteger(Effect.Gpu?.cap || P.capacity)} cap`;
            const Full = !Molecular && !Fibre && (Alive / Math.max(1, Effect.Gpu?.cap || 1)) >= 0.98;
            Mark.className = 'mark ' + (!P.visible ? 'warm' : Full ? 'warm' : 'live');
            Mark.textContent = '';
            Mark.append(C.ConstructGlyph(!P.visible ? 'EyeOff' : Full ? 'Alert' : 'Check'));
            MarkText.textContent = !P.visible ? 'Hidden' : !Scene.Playing ? 'Paused' : Full ? 'At capacity' : Molecular ? 'Conservation locked' : Fibre ? 'Analytic curve' : 'Within capacity';
        });

        // Sun / Atmosphere Signature 3-Card KPI Highlight Row
        C.MetricTrio(Page, [
            { Label: 'Shape', Resolve: () => [['Streak', 'Sphere', 'Leaf', 'Glow', 'Cube', 'Coin', 'Bubble'][P.shape] || 'Strand', P.blend === 'alpha' ? 'alpha blend' : 'additive glow'] },
            { Label: Molecular ? 'Box half' : 'Gravity', Resolve: () => Molecular ? [`${P.boxHalf[0].toFixed(2)} m`, 'container'] : [`${P.gravity.toFixed(2)} g`, `${P.drag.toFixed(2)} drag`] },
            { Label: Fibre ? 'Loop speed' : 'Wind coupling', Highlight: true, Resolve: () => Fibre ? [`${P.fibre.loopSeconds.toFixed(1)} s`, 'analytic phase'] : [`${P.windCoupling.toFixed(2)} /s`, 'lattice coupling'] }
        ]);

        // Sun Quick-Control Feature Tiles
        Page.append(C.Construct('div', 'section-subhead', [C.Construct('span', null, 'Quick controls'), C.Construct('span', null, Preset?.group || 'Particles')]));
        const Grid = C.Construct('div', 'property-switches');
        C.ToggleTile(Grid, BindEffect(Effect, P, 'visible', { Label: 'Visible', Glyph: 'Eye' }));
        C.ToggleTile(Grid, {
            Label: 'Wind-driven', Glyph: 'Wind',
            Read: () => P.windCoupling > 0,
            Write: PE.Edits.Through('Wind coupling', On => { P.windCoupling = On ? (Effect.StoredCoupling || 1) : (Effect.StoredCoupling = P.windCoupling, 0); }),
            Availability: () => (Molecular || Fibre) ? { Enabled: false, Reason: Fibre ? 'Fibres ignore wind' : 'Molecular gas ignores wind' } : { Enabled: true }
        });
        C.ToggleTile(Grid, {
            Label: 'Additive glow', Glyph: 'Sparkles',
            Read: () => P.blend === 'add',
            Write: PE.Edits.Through('Blend', On => { P.blend = On ? 'add' : 'alpha'; }),
            Availability: () => Fibre ? { Enabled: false, Reason: 'Fibres glow analytically' } : { Enabled: true }
        });
        C.ToggleTile(Grid, {
            Label: 'Floor bounce', Glyph: 'Activity',
            Read: () => P.bounce > 0,
            Write: PE.Edits.Through('Bounce', On => { P.bounce = On ? (Effect.StoredBounce || 0.4) : (Effect.StoredBounce = P.bounce, 0); }),
            Availability: () => (Molecular || Fibre) ? { Enabled: false, Reason: 'No floor plane collision' } : { Enabled: true }
        });
        Page.append(Grid);

        // Quick Actions
        C.ActionRow(Page, [
            { Label: 'Burst', Glyph: 'Sparkles', Tone: 'accent', Act: () => { Effect.PendingBurst += P.transition ? Effect.Gpu.cap : Math.max(60, Math.round(Effect.Gpu.cap * 0.4)); }, Availability: () => Fibre ? { Enabled: false, Reason: 'Analytic' } : { Enabled: true } },
            { Label: Molecular ? 'Re-seed' : 'Clear', Glyph: 'Reset', Act: () => RebuildGpu(Effect) },
            { Label: 'Duplicate', Glyph: 'Copy', Act: () => DuplicateEffect(Effect) },
            { Label: 'Delete', Glyph: 'Trash', Tone: 'danger', Act: () => RemoveEffect(Effect) }
        ]);

        // Sun-Style Optical / Emitter Diagram Card
        if (!Fibre)
        {
            const Opt = C.SectionCard(Page, { Title: 'Emitter cone & spectrum', Glyph: 'Aperture', Badge: '02 / OPTICS', Hint: 'Live launch direction, spread cone angle, speed band and colour ramp.' });
            C.EmitterInstrument(Opt.Inner, Effect, DisplayColour);
            const Ribbon = C.Construct('div', 'gradient-ribbon');
            Opt.Inner.append(Ribbon);
            C.Register(() => { Ribbon.style.background = `linear-gradient(90deg, ${DisplayColour(P.colA)}, ${DisplayColour(P.colC || P.colB)}, ${DisplayColour(P.colB)})`; });
        }
        return;
    }

    if (InspectorTab === 'source')
    {
        if (Fibre) { ConstructFibreFormAndPlacement(Page, Effect); return; }
        const Src = C.SectionCard(Page, {
            Title: 'Emission source', Glyph: 'Target', Badge: '01 / SOURCE',
            Hint: Molecular ? 'Molecular systems fill once at initialization and conserve total count inside the box.' : 'Birth rate, emitter volume geometry and spawn origin.'
        }).Inner;
        const Capacities = [...new Set([384, 512, 1024, 1536, 2048, 4096, 8192, P.capacity])].sort((A, B) => A - B);
        C.SelectRow(Src, BindEffect(Effect, P, 'capacity', { Label: 'Pool capacity', Options: Capacities.map(K => [FormatInteger(K), K]), Rebuild: true }));
        if (!Molecular) Range(Src, BindEffect(Effect, P, 'rate', { Label: 'Spawn rate' }), 0, 2000, 1, 0, '/s');
        C.SelectRow(Src, BindEffect(Effect, P, 'emitShape', { Label: 'Volume shape', Options: [['Point', 0], ['Sphere volume', 1], ['Disc', 2], ['Box', 3], ['Box surface grid', 4]] }));
        C.VectorRow(Src, BindEffect(Effect, P, 'origin', { Label: 'Emitter origin', Unit: 'm', Step: 0.05, Digits: 2, Minimum: -20, Maximum: 20 }));
        Range(Src, BindEffect(Effect, P, 'radius', { Label: 'Emitter radius' }), 0, 8, 0.01, 2, 'm');
        if (Molecular)
            Range(Src, Object.assign(BindEffect(Effect, P, 'boxHalf', { Label: 'Box half extent' }), { Read: () => P.boxHalf[0], Write: PE.Edits.Through('Box half', R => { P.boxHalf = [R, R, R]; }) }), 0.2, 4, 0.01, 2, 'm');
        else
            C.VectorRow(Src, BindEffect(Effect, P, 'boxHalf', { Label: 'Box half extents', Unit: 'm', Step: 0.05, Digits: 2, Minimum: 0, Maximum: 10 }));

        if (!Molecular)
        {
            const Vel = C.SectionCard(Page, { Title: 'Launch velocity & lifetime', Glyph: 'Rays', Badge: '02 / KINEMATICS' }).Inner;
            C.VectorRow(Vel, BindEffect(Effect, P, 'dir', { Label: 'Launch direction', Step: 0.01, Digits: 2, Minimum: -1, Maximum: 1 }));
            Range(Vel, BindEffect(Effect, P, 'spread', { Label: 'Cone spread' }), 0, 3.1416, 0.01, 2, 'rad');
            Range(Vel, Object.assign(BindEffect(Effect, P, 'speedMin', { Label: 'Speed minimum' }), { Write: PE.Edits.Through('Speed min', V => { P.speedMin = Math.min(V, P.speedMax); }) }), 0, 20, 0.05, 2, 'm/s');
            Range(Vel, Object.assign(BindEffect(Effect, P, 'speedMax', { Label: 'Speed maximum' }), { Write: PE.Edits.Through('Speed max', V => { P.speedMax = Math.max(V, P.speedMin); }) }), 0, 20, 0.05, 2, 'm/s');
            Range(Vel, Object.assign(BindEffect(Effect, P, 'lifeMin', { Label: 'Life minimum' }), { Write: PE.Edits.Through('Life min', V => { P.lifeMin = Math.min(V, P.lifeMax); }) }), 0.05, 20, 0.05, 2, 's');
            Range(Vel, Object.assign(BindEffect(Effect, P, 'lifeMax', { Label: 'Life maximum' }), { Write: PE.Edits.Through('Life max', V => { P.lifeMax = Math.max(V, P.lifeMin); }) }), 0.05, 20, 0.05, 2, 's');
        }
        return;
    }

    if (InspectorTab === 'motion')
    {
        if (Fibre) { ConstructFibreLookAndColour(Page, Effect); return; }
        const Look = C.SectionCard(Page, { Title: 'Particle appearance', Glyph: 'Palette', Badge: '01 / SHADING' }).Inner;
        const Ribbon = C.Construct('div', 'gradient-ribbon');
        Look.append(Ribbon);
        C.Register(() => { Ribbon.style.background = `linear-gradient(90deg, ${DisplayColour(P.colA)}, ${DisplayColour(P.colC || P.colB)}, ${DisplayColour(P.colB)})`; });

        if (!Molecular) C.SelectRow(Look, BindEffect(Effect, P, 'shape', { Label: 'Billboard / mesh shape', Options: [['Streak', 0], ['Sphere', 1], ['Leaf / paper', 2], ['Soft glow', 3], ['Chip cube', 4], ['Coin', 5], ['Bubble', 6]] }));
        C.SegmentedRow(Look, BindEffect(Effect, P, 'blend', { Label: 'Compositing blend', Options: [['Additive glow', 'add'], ['Alpha · premultiplied', 'alpha']] }));
        if (!Molecular)
        {
            Range(Look, BindEffect(Effect, P, 'sizeStart', { Label: 'Size at birth' }), 0.002, 2, 0.002, 3, 'm');
            Range(Look, BindEffect(Effect, P, 'sizeEnd', { Label: 'Size at death' }), 0.002, 3, 0.002, 3, 'm');
        }
        Range(Look, BindEffect(Effect, P, 'sizeScale', { Label: 'Size multiplier' }), 0.1, 4, 0.02, 2, '×');
        C.ColourRow(Look, BindEffect(Effect, P, 'colA', { Label: Molecular ? 'Species / birth colour A' : 'Birth colour', Alpha: true }));
        C.ColourRow(Look, BindEffect(Effect, P, 'colB', { Label: Molecular ? 'Species / death colour B' : 'Death colour', Alpha: true }));
        if (P.kind === 4) C.ColourRow(Look, BindEffect(Effect, P, 'colC', { Label: 'Reaction product colour C', Alpha: true }));

        if (!Molecular)
        {
            const Mot = C.SectionCard(Page, { Title: 'Forces & motion response', Glyph: 'Wind', Badge: '02 / DYNAMICS' }).Inner;
            Range(Mot, BindEffect(Effect, P, 'windCoupling', { Label: 'Wind lattice coupling' }), 0, 5, 0.02, 2, '1/s');
            Range(Mot, BindEffect(Effect, P, 'drag', { Label: 'Aerodynamic drag' }), 0, 5, 0.02, 2, '1/s');
            Range(Mot, BindEffect(Effect, P, 'gravity', { Label: 'Gravity factor' }), -1, 2, 0.02, 2, 'g');
            Range(Mot, BindEffect(Effect, P, 'buoyancy', { Label: 'Thermal buoyancy' }), -4, 6, 0.05, 2, 'm/s²');
            Range(Mot, BindEffect(Effect, P, 'bounce', { Label: 'Floor restitution' }), 0, 1, 0.01, 2, '');
            Range(Mot, BindEffect(Effect, P, 'flutter', { Label: 'Flutter turbulence' }), 0, 5, 0.05, 2, 'm/s²');
        }
        else
        {
            const Phy = C.SectionCard(Page, { Title: 'Molecular physics', Glyph: 'Atom', Badge: '02 / THERMOSTAT' }).Inner;
            Range(Phy, BindEffect(Effect, P, 'temperature', { Label: 'Temperature kT' }), 0.02, 3, 0.01, 2, 'ε');
            Range(Phy, BindEffect(Effect, P, 'damping', { Label: 'Thermostat γ' }), 0, 10, 0.05, 2, '1/s');
            Range(Phy, BindEffect(Effect, P, 'sigma', { Label: 'Particle diameter σ' }), 0.03, 0.2, 0.002, 3, 'm');
            if (P.kind === 3) Range(Phy, BindEffect(Effect, P, 'epsilon', { Label: 'Well depth ε' }), 0, 1, 0.01, 2, '');
            else
            {
                Range(Phy, BindEffect(Effect, P, 'reactRate', { Label: 'Forward rate A+B→C' }), 0, 30, 0.1, 1, '1/s');
                Range(Phy, BindEffect(Effect, P, 'dissociation', { Label: 'Dissociation C→A+B' }), 0, 5, 0.05, 2, '1/s');
            }
        }
        return;
    }

    // Readback / Special FX / Record tab
    if (Fibre) { ConstructFibreSparksAndRecord(Page, Effect); return; }

    if (P.transition)
    {
        const Tr = P.transition;
        const Sec = C.SectionCard(Page, { Title: 'Derez / assembly wave', Glyph: 'Boxes', Badge: 'TRANSITION' }).Inner;
        C.SelectRow(Sec, BindEffect(Effect, Tr, 'wave', { Label: 'Release wave pattern', Options: [['Corner (derez)', 'corner'], ['Melt (patchy)', 'melt'], ['Glitch (blocky)', 'glitch']] }));
        C.SelectRow(Sec, BindEffect(Effect, Tr, 'assemble', { Label: 'Flight mode', Options: [['Burst or fall', 0], ['Rebuild from outside', 1], ['Stack in column', 2]] }));
        Range(Sec, BindEffect(Effect, Tr, 'holdBase', { Label: 'Hold duration' }), 0, 10, 0.05, 2, 's');
        Range(Sec, BindEffect(Effect, Tr, 'holdSpread', { Label: 'Wave spread' }), 0, 10, 0.05, 2, 's');
    }
    if (P.blackHole && P.attractor)
    {
        const BH = P.blackHole, A = P.attractor;
        const Sec = C.SectionCard(Page, { Title: 'Black hole lensing & well', Glyph: 'Planet', Badge: 'RELATIVITY' }).Inner;
        Range(Sec, BindEffect(Effect, BH, 'horizon', { Label: 'Event horizon radius' }), 0.1, 2, 0.01, 2, 'm');
        Range(Sec, BindEffect(Effect, BH, 'einstein', { Label: 'Einstein ring lensing' }), 0, 6, 0.05, 2, '×');
        Range(Sec, BindEffect(Effect, A, 'strength', { Label: 'Gravity pull strength' }), 0, 30, 0.1, 1, 'm/s²');
        Range(Sec, BindEffect(Effect, A, 'swirl', { Label: 'Accretion swirl' }), 0, 12, 0.05, 2, 'm/s²');
    }
    if (P.heatShimmer)
    {
        const Sh = P.heatShimmer;
        const Sec = C.SectionCard(Page, { Title: 'Heat shimmer distortion', Glyph: 'Flame', Badge: 'OPTICAL' }).Inner;
        Range(Sec, BindEffect(Effect, Sh, 'radius', { Label: 'Refraction radius' }), 0.2, 6, 0.05, 2, 'm');
        Range(Sec, BindEffect(Effect, Sh, 'strength', { Label: 'Distortion amplitude' }), 0, 0.05, 0.001, 3, '');
    }
    if (P.magneticField)
    {
        const M = P.magneticField;
        const Sec = C.SectionCard(Page, { Title: 'Magnetic dipole field', Glyph: 'Magnet', Badge: 'DIPOLE' }).Inner;
        Range(Sec, BindEffect(Effect, M, 'strength', { Label: 'Drive strength' }), 0, 10, 0.05, 2, 'm/s²');
        Range(Sec, BindEffect(Effect, M, 'guide', { Label: 'Field-line guide' }), 0, 12, 0.05, 2, '1/s');
    }

    const Rb = C.SectionCard(Page, { Title: 'Async GPU reduction readback', Glyph: 'Activity', Badge: '32 B MAP' }).Inner;
    const AliveCell = C.Construct('b', null, '0');
    const LatCell = C.Construct('b', null, '—');
    const CountCell = C.Construct('b', null, '0');
    Rb.append(C.Construct('div', 'stat-grid', [
        C.Construct('div', 'stat-cell', [AliveCell, C.Construct('span', null, 'Particles alive')]),
        C.Construct('div', 'stat-cell', [LatCell, C.Construct('span', null, 'Map latency')]),
        C.Construct('div', 'stat-cell', [CountCell, C.Construct('span', null, 'Completed maps')])
    ]));
    const BenchMsg = C.Construct('p', 'property-hint', 'Run an async 80-byte-per-particle full-buffer DMA readback test on the active adapter.');
    Rb.append(BenchMsg);
    C.ActionRow(Rb, [{
        Label: 'Benchmark Full Buffer Readback', Glyph: 'Gauge', Tone: 'accent',
        Act: async () =>
        {
            const M = await Scene.Engine.benchmarkFullReadback(Effect.Gpu);
            BenchMsg.textContent = `${FormatInteger(M.bytes / 1024)} KiB in ${M.ms.toFixed(2)} ms (${M.mbPerSecond.toFixed(0)} MiB/s) · ${FormatInteger(M.alive)} alive.`;
        }
    }]);
    LiveRefreshers.push(() =>
    {
        AliveCell.textContent = FormatInteger(Effect.Gpu?.stats_?.alive ?? 0);
        LatCell.textContent = Effect.Gpu?.readbackMs ? `${Effect.Gpu.readbackMs.toFixed(2)} ms` : '—';
        CountCell.textContent = FormatInteger(Effect.Gpu?.readbackCount ?? 0);
    });

    RenderRecordCard(Page, 'ParticleEffect (.levelspace)', () => EncodeEffect(Effect));
}

function ConstructFibreFormAndPlacement(Page, Effect)
{
    const P = Effect.P, F = P.fibre;
    const Form = C.SectionCard(Page, { Title: 'Fibre curve form', Glyph: 'Zap', Badge: '01 / FORM' }).Inner;
    C.SelectRow(Form, BindEffect(Effect, F, 'shape', { Label: 'Curve topology', Options: [['Streak (light head)', 'streak'], ['Ribbon (sheet)', 'ribbon'], ['Trail (path)', 'trail']] }));
    Range(Form, BindEffect(Effect, F, 'strands', { Label: 'Strand count' }), 1, 1200, 1, 0, '');
    Range(Form, BindEffect(Effect, F, 'segments', { Label: 'Curve segments' }), 4, 160, 1, 0, '');
    Range(Form, BindEffect(Effect, F, 'spread', { Label: 'Bundle spread' }), 0, 10, 0.02, 2, 'm');
    Range(Form, BindEffect(Effect, F, 'amplitude', { Label: 'Wave wobble' }), 0, 4, 0.02, 2, 'm');

    const Place = C.SectionCard(Page, { Title: 'Placement & timing', Glyph: 'Move', Badge: '02 / MOTION' }).Inner;
    C.VectorRow(Place, BindEffect(Effect, P, 'origin', { Label: 'Origin', Unit: 'm', Step: 0.05, Digits: 2, Minimum: -20, Maximum: 20 }));
    Range(Place, BindEffect(Effect, F, 'scale', { Label: 'Geometry scale' }), 0.05, 10, 0.02, 2, '×');
    Range(Place, BindEffect(Effect, F, 'loopSeconds', { Label: 'Loop duration' }), 0.5, 20, 0.1, 1, 's');
}

function ConstructFibreLookAndColour(Page, Effect)
{
    const F = Effect.P.fibre;
    const Look = C.SectionCard(Page, { Title: 'Optical glow & ribbon profile', Glyph: 'Sparkles', Badge: '01 / OPTICS' }).Inner;
    Range(Look, BindEffect(Effect, F, 'thickness', { Label: 'Ribbon thickness' }), 0.5, 8, 0.05, 2, 'px');
    Range(Look, BindEffect(Effect, F, 'intensity', { Label: 'Emissive intensity' }), 0, 12, 0.05, 2, '');
    Range(Look, BindEffect(Effect, F, 'halo', { Label: 'Outer halo' }), 0, 2, 0.02, 2, '');
    Range(Look, BindEffect(Effect, F, 'baseline', { Label: 'Idle brightness' }), 0, 1, 0.01, 2, '');
    const Col = C.SectionCard(Page, { Title: 'Gradient stops', Glyph: 'Palette', Badge: '02 / COLOUR' }).Inner;
    C.SelectRow(Col, BindEffect(Effect, F, 'colourMode', { Label: 'Colouring mode', Options: [['Solid', 'solid'], ['Ramp along fibre', 'ramp'], ['Palette per strand', 'palette']] }));
    F.stops.forEach((Stop, I) => C.ColourRow(Col, BindEffect(Effect, Stop, 'col', { Label: `Gradient stop ${I + 1}` })));
}

function ConstructFibreSparksAndRecord(Page, Effect)
{
    const F = Effect.P.fibre;
    const Sp = C.SectionCard(Page, { Title: 'Leading head sparks', Glyph: 'Sparkles', Badge: 'HEAD FX' }).Inner;
    Range(Sp, BindEffect(Effect, F, 'sparks', { Label: 'Spark emission share' }), 0, 1, 0.01, 2, '');
    Range(Sp, BindEffect(Effect, F, 'sparkSize', { Label: 'Spark radius' }), 0, 0.4, 0.005, 3, 'm');
    Range(Sp, BindEffect(Effect, F, 'sparkBrightness', { Label: 'Spark bloom' }), 0, 12, 0.05, 2, '');
    RenderRecordCard(Page, 'ParticleEffect (.levelspace)', () => EncodeEffect(Effect));
}

function ConstructFieldInspector(Page)
{
    RenderInspectorTabs([
        ['overview', 'Overview'],
        ['forces', 'Forces'],
        ['overlay', 'Overlay & Probe'],
        ['readback', 'Record']
    ]);

    if (InspectorTab === 'overview')
    {
        C.PillTabs(Page, {
            Options: [['Flow · m/s', 'Flow'], ['Accelerate · m/s²', 'Accelerate'], ['Damp · 1/s', 'Damp']],
            Read: () => Scene.Overlay.Contribution,
            Write: Cont => { Scene.Overlay.Contribution = Cont; Scene.Overlay.Enabled = true; AppendLog('VIEW', `Overlay set to ${Cont}`, null, 'live'); }
        });

        const Metric = C.Construct('div', 'metric-big');
        const Rate = C.Construct('div', 'rate');
        const Low = C.Construct('div', 'low');
        const MarkText = C.Construct('span');
        const Card = C.Construct('div', 'hero-card', [
            C.Construct('div', 'hero-top', [
                C.Construct('div', 'budget-chip', [C.Construct('div', 'mark live', C.ConstructGlyph('Check')), MarkText]),
                C.Construct('div', 'estimate-tag', '01 / WIND PROBE')
            ]),
            C.Construct('div', 'hero-title', 'Flow velocity at probe'),
            C.Construct('div', 'hero-metrics', [Metric, C.Construct('div', 'metric-side', [Rate, Low])])
        ]);
        Page.append(Card);

        const Series = C.TimeSeries(Card, {
            Samples: ProbeSamples, Capacity: SampleCapacity,
            Target: () => Scene.Wind.arrowRef, Height: 96, Unit: 'm/s',
            TargetLabel: 'Arrow scale', Headroom: 1.2, AxisDigits: 1
        });
        LiveRefreshers.push(Series.Refresh);
        LiveRefreshers.push(() =>
        {
            const R = Scene.Engine?.probeResult;
            C.ConstructMetric(Metric, R ? R.mag : 0, 2, 'm/s');
            const Bearing = R ? Wrap360(Math.atan2(R.value[0], -R.value[2]) * 180 / Math.PI) : 0;
            Rate.textContent = `${Bearing.toFixed(0)}°`;
            Rate.append(C.Construct('small', null, C.ResolveCompassPoint(Bearing)));
            Low.textContent = R ? `vertical ${R.value[1].toFixed(2)} m/s` : 'polling lattice';
            const St = Scene.Readback?.Status;
            MarkText.textContent = St?.Count ? `Readback ${(St.Bytes / 1024).toFixed(0)} KB · ${St.Milliseconds.toFixed(1)} ms` : 'Lattice active';
        });

        C.MetricTrio(Page, [
            { Label: 'Lattice flow', Resolve: () => [`${FlowFields().length} / ${PE.WIND.maxComps}`, 'baked 24·12·24'] },
            { Label: 'Per-particle', Resolve: () => [`${Scene.PackedFields ? Math.round(Scene.PackedFields[0]) : 0} / 8`, 'GPU fieldAccel'] },
            { Label: 'Active total', Highlight: true, Resolve: () => [String(Scene.Forces.filter(F => F.Enabled).length), `of ${Scene.Forces.length} forces`] }
        ]);

        Page.append(C.Construct('div', 'section-subhead', [C.Construct('span', null, 'Quick controls'), C.Construct('span', null, 'Overlay & Lattice')]));
        const Grid = C.Construct('div', 'property-switches');
        C.ToggleTile(Grid, BindScene(Scene.Overlay, 'Enabled', { Label: 'Field overlay', Glyph: 'Layers' }, 'VIEW', 'Overlay'));
        C.ToggleTile(Grid, BindScene(Scene.Overlay, 'Heatmap', { Label: 'Heatmap', Glyph: 'Palette' }, 'VIEW', 'Overlay'));
        C.ToggleTile(Grid, BindScene(Scene.Overlay, 'Arrows', { Label: 'Slice arrows', Glyph: 'Move' }, 'VIEW', 'Overlay'));
        C.ToggleTile(Grid, BindScene(Scene.Overlay, 'Minimap', { Label: 'Minimap', Glyph: 'Map' }, 'VIEW', 'Overlay'));
        Page.append(Grid);
        return;
    }

    if (InspectorTab === 'forces')
    {
        const Sec = C.SectionCard(Page, { Title: 'Scene force fields', Glyph: 'Target', Badge: `${Scene.Forces.length} ACTIVE` }).Inner;
        const List = C.Construct('div', 'force-list');
        Scene.Forces.forEach((Field, Index) =>
        {
            const Kind = PE.Forces.KindById(Field.Kind);
            if (!Kind) return;
            const Cont = Contributions[Kind.Give];
            const Dot = C.Construct('div', 'row-glyph', C.ConstructGlyph(ForceGlyphs[Field.Kind] || 'Target'));
            Dot.style.color = ContributionColours[Cont];
            const Item = C.Construct('button', 'force-item', [
                Dot,
                C.Construct('div', null, [C.Construct('div', 'row-name', Field.Name), C.Construct('div', 'force-item-meta', `${Kind.Name} · ${Cont}`)]),
                C.Construct('div', 'force-item-val', Field.Enabled ? `${Field.Strength.toFixed(1)} ${ContributionUnits[Cont]}` : 'Off')
            ]);
            Item.type = 'button';
            Item.addEventListener('click', () => SelectNode('force:' + Index));
            List.append(Item);
        });
        Sec.append(List);

        const AddSec = C.SectionCard(Page, { Title: 'Add localized force', Glyph: 'Plus', Badge: 'TAXONOMY' }).Inner;
        const Chips = C.Construct('div', 'chip-adders');
        for (const Kind of PE.Forces.ForceFieldKinds)
        {
            const Btn = C.Construct('button', 'chip-add-btn', `+ ${Kind.Name}`);
            Btn.type = 'button';
            Btn.addEventListener('click', () => AddForceField(Kind.Id));
            Chips.append(Btn);
        }
        AddSec.append(Chips);
        return;
    }

    if (InspectorTab === 'overlay')
    {
        const Ov = C.SectionCard(Page, { Title: 'Field slice visualization', Glyph: 'Layers', Badge: 'OVERLAY' }).Inner;
        C.SegmentedRow(Ov, BindScene(Scene.Overlay, 'Quantity', { Label: 'Mapped scalar', Options: [['Speed', 'Speed'], ['Up', 'Vertical'], ['Vorticity', 'Vorticity'], ['Divergence', 'Divergence']] }, 'VIEW', 'Overlay'));
        Range(Ov, BindScene(Scene.Overlay, 'SliceHeight', { Label: 'Slice elevation' }, 'VIEW', 'Overlay'), 0.1, 7.9, 0.05, 2, 'm');
        Range(Ov, BindScene(Scene.Overlay, 'Opacity', { Label: 'Heatmap opacity' }, 'VIEW', 'Overlay'), 0.1, 1, 0.05, 2, '');
        C.SwitchRow(Ov, BindScene(Scene.Overlay, 'Tracers', { Label: 'Advect slice tracers' }, 'VIEW', 'Overlay'));
        C.SwitchRow(Ov, BindScene(Scene.Overlay, 'Labels', { Label: 'Field badge labels' }, 'VIEW', 'Overlay'));

        const Pr = C.SectionCard(Page, { Title: '3D Lattice probe', Glyph: 'Crosshair', Badge: 'TEXEL MAP' }).Inner;
        C.SwitchRow(Pr, BindScene(Scene, 'ProbeEnabled', { Label: 'Probe enabled' }, 'WIND', 'Probe'));
        C.VectorRow(Pr, BindScene(Scene, 'Probe', { Label: 'Probe world coordinate', Unit: 'm', Step: 0.05, Digits: 2, Minimum: -6, Maximum: 8 }, 'WIND', 'Probe'));
        return;
    }

    RenderRecordCard(Page, 'ForceSource (.levelspace)', EncodeForces);
}

function ConstructWindInspector(Page)
{
    RenderInspectorTabs([
        ['overview', 'Overview'],
        ['lattice', 'Lattice'],
        ['display', 'Display'],
        ['flows', 'Flow List']
    ]);
    const W = Scene.Wind;
    if (InspectorTab === 'overview')
    {
        C.MetricTrio(Page, [
            { Label: 'Lattice scale', Resolve: () => [`×${W.windScale.toFixed(2)}`, 'global gain'] },
            { Label: 'Turbulence', Resolve: () => [`${W.turbulence.toFixed(2)}`, 'm/s curl'] },
            { Label: 'Swirl vortex', Highlight: true, Resolve: () => [`${W.swirl.toFixed(2)}`, 'm/s² curl'] }
        ]);
        const LatticeSec = C.SectionCard(Page, { Title: 'Global wind controls', Glyph: 'Wind', Badge: '24·12·24' }).Inner;
        Range(LatticeSec, BindScene(W, 'windScale', { Label: 'Global wind scale' }, 'WIND', 'Wind'), 0, 3, 0.01, 2, '×');
        Range(LatticeSec, BindScene(W, 'turbulence', { Label: 'Value-noise turbulence' }, 'WIND', 'Wind'), 0, 2, 0.01, 2, 'm/s');
        Range(LatticeSec, BindScene(W, 'swirl', { Label: 'Divergence-free swirl' }, 'WIND', 'Wind'), 0, 4, 0.05, 2, 'm/s');
        return;
    }
    if (InspectorTab === 'lattice')
    {
        const Sec = C.SectionCard(Page, { Title: 'Lattice sampling grid', Glyph: 'Lattice', Badge: '12×8×12 m' }).Inner;
        Range(Sec, BindScene(W, 'windScale', { Label: 'Global wind scale' }, 'WIND', 'Wind'), 0, 3, 0.01, 2, '×');
        Range(Sec, BindScene(W, 'turbulence', { Label: 'Turbulence noise' }, 'WIND', 'Wind'), 0, 2, 0.01, 2, 'm/s');
        Range(Sec, BindScene(W, 'swirl', { Label: 'Curl swirl' }, 'WIND', 'Wind'), 0, 4, 0.05, 2, 'm/s');
        return;
    }
    if (InspectorTab === 'display')
    {
        const Sec = C.SectionCard(Page, { Title: 'Viewport lattice display', Glyph: 'Eye', Badge: 'DEBUG' }).Inner;
        C.SwitchRow(Sec, BindScene(W, 'showArrows', { Label: '3D voxel velocity arrows' }, 'VIEW', 'Lattice'));
        Range(Sec, BindScene(W, 'arrowRef', { Label: 'Arrow speed ceiling' }, 'VIEW', 'Lattice'), 1, 15, 0.1, 1, 'm/s');
        Range(Sec, BindScene(W, 'arrowStride', { Label: 'Arrow voxel stride' }, 'VIEW', 'Lattice'), 1, 6, 1, 0, '');
        C.SwitchRow(Sec, BindScene(W, 'showFloor', { Label: 'Ground reference grid' }, 'VIEW', 'Lattice'));
        C.SwitchRow(Sec, BindScene(W, 'showDomain', { Label: 'Domain wire bounding box' }, 'VIEW', 'Lattice'));
        return;
    }
    const Sec = C.SectionCard(Page, { Title: 'Flow contributors', Glyph: 'Wind', Badge: `${FlowFields().length} FLOWS` }).Inner;
    for (const F of FlowFields())
    {
        const idx = Scene.Forces.indexOf(F);
        const Btn = C.Construct('button', 'force-item', [
            C.Construct('div', 'row-name', F.Name),
            C.Construct('div', 'force-item-val', F.Enabled ? `${F.Strength.toFixed(2)} m/s` : 'Off')
        ]);
        Btn.type = 'button';
        Btn.style.marginBottom = '6px';
        Btn.addEventListener('click', () => SelectNode('force:' + idx));
        Sec.append(Btn);
    }
}

function ConstructForceInspector(Page, Field, Index)
{
    const Kind = PE.Forces.KindById(Field.Kind);
    const Contribution = Contributions[Kind.Give];
    RenderInspectorTabs([
        ['overview', 'Overview'],
        ['field', 'Field Params'],
        ['reach', 'Reach & Time'],
        ['record', 'Record']
    ]);

    if (InspectorTab === 'overview')
    {
        const Metric = C.Construct('div', 'metric-big');
        const Rate = C.Construct('div', 'rate');
        const Low = C.Construct('div', 'low');
        const Card = C.Construct('div', 'hero-card', [
            C.Construct('div', 'hero-top', [
                C.Construct('div', 'budget-chip', [C.Construct('div', 'mark live', C.ConstructGlyph('Check')), C.Construct('span', null, Contribution)]),
                C.Construct('div', 'estimate-tag', Kind.Id.toUpperCase())
            ]),
            C.Construct('div', 'hero-title', 'Force amplitude'),
            C.Construct('div', 'hero-metrics', [Metric, C.Construct('div', 'metric-side', [Rate, Low])])
        ]);
        Page.append(Card);

        C.CurvePlot(Card, {
            Height: 100,
            Resolve: () =>
            {
                const Reach = Field.Reaches === 'everywhere' ? 10 : Field.Radius;
                const Pts = [];
                for (let S = 0; S <= 50; ++S)
                {
                    const X = (S / 50) * Reach * 1.1;
                    const Y = Field.Reaches === 'everywhere' ? Math.abs(Field.Strength) : Math.abs(Field.Strength) * (X < Reach ? PE.Forces.Falls(Field.Fades, X / Reach * Reach, Reach) : 0);
                    Pts.push([X, Y]);
                }
                const Peak = Math.max(1e-5, ...Pts.map(P => P[1]));
                return {
                    XRange: [0, Reach * 1.1], YRange: [0, Peak * 1.18],
                    Curves: [{ Points: Pts, Stroke: ContributionColours[Contribution], Fill: ContributionColours[Contribution] + '24' }],
                    XTicks: [[0, '0'], [Reach, `${Reach.toFixed(1)}m`]],
                    Caption: Field.Reaches === 'everywhere' ? 'Unbounded global field' : `Falloff: ${Field.Fades} · Reach ${Field.Radius.toFixed(1)} m`
                };
            }
        });

        LiveRefreshers.push(() =>
        {
            C.ConstructMetric(Metric, Math.abs(Field.Strength), 2, Kind.Unit);
            Rate.textContent = Field.Reaches === 'everywhere' ? '∞' : `${Field.Radius.toFixed(1)} m`;
            Low.textContent = PE.Forces.Packable(Field) ? 'GPU active' : 'Authored only';
        });

        C.ActionRow(Page, [
            { Label: 'Remove force field', Glyph: 'Trash', Tone: 'danger', Act: () => { Scene.Forces.splice(Index, 1); SelectNode('Field'); } }
        ]);
        return;
    }

    if (InspectorTab === 'field')
    {
        const Sec = C.SectionCard(Page, { Title: 'Force configuration', Glyph: 'Gauge', Badge: Kind.Unit }).Inner;
        Range(Sec, BindScene(Field, 'Strength', { Label: 'Strength amplitude' }, 'FORCES', Field.Name), -20, 20, 0.05, 2, Kind.Unit);
        if (Kind.Give === 'flow' && Field.Kind !== 'tornado' && Field.Kind !== 'outflow')
            C.PolarDial(Sec, {
                Label: 'Compass bearing',
                Read: () => CompassFromField(Field),
                Write: Deg => { Field.Direction = FieldFromCompass(Deg); },
                Resolve: () => ({ Caption: `Travels toward ${CompassFromField(Field).toFixed(0)}°` })
            });
        return;
    }

    if (InspectorTab === 'reach')
    {
        const Reach = C.SectionCard(Page, { Title: 'Spatial reach & falloff', Glyph: 'Target', Badge: Field.Reaches }).Inner;
        if (Field.Reaches !== 'everywhere')
        {
            C.VectorRow(Reach, BindScene(Field, 'Centre', { Label: 'World centre', Unit: 'm', Step: 0.05, Digits: 2, Minimum: -20, Maximum: 20 }, 'FORCES', Field.Name));
            Range(Reach, BindScene(Field, 'Radius', { Label: 'Influence radius' }, 'FORCES', Field.Name), 0.5, 30, 0.05, 2, 'm');
            C.SegmentedRow(Reach, BindScene(Field, 'Fades', { Label: 'Falloff profile', Options: Object.values(PE.Forces.Falloff).map(N => [N === 'inverse-square' ? 'Inv²' : N[0].toUpperCase() + N.slice(1), N]) }, 'FORCES', Field.Name));
        }
        else Reach.append(C.Construct('p', 'property-hint', 'Everywhere fields affect the full unbounded particle space.'));

        const Tm = C.SectionCard(Page, { Title: 'Schedule window', Glyph: 'Clock', Badge: Field.Lasts > 0 ? 'TIMED' : 'ALWAYS' }).Inner;
        Range(Tm, BindScene(Field, 'Begins', { Label: 'Start delay' }, 'FORCES', Field.Name), 0, 60, 0.1, 1, 's');
        Range(Tm, BindScene(Field, 'Lasts', { Label: 'Duration (0 = forever)' }, 'FORCES', Field.Name), 0, 120, 0.1, 1, 's');
        Range(Tm, BindScene(Field, 'Repeats', { Label: 'Repeat interval' }, 'FORCES', Field.Name), 0, 120, 0.1, 1, 's');
        return;
    }

    RenderRecordCard(Page, 'ForceSource (.levelspace)', () => EncodeForces().join('\n').split('\n\n')[Index].split('\n'));
}

function ConstructLightningInspector(Page)
{
    const L = Scene.Lightning;
    RenderInspectorTabs([
        ['overview', 'Overview'],
        ['strikes', 'Strikes'],
        ['bolt', 'Bolt & Web'],
        ['telemetry', 'Audio & Stats']
    ]);

    if (InspectorTab === 'overview')
    {
        C.MetricTrio(Page, [
            { Label: 'Strikes fired', Resolve: () => [FormatInteger(L.strikes), L.auto ? 'auto active' : 'manual'] },
            { Label: 'Bolt detail', Resolve: () => [`${L.levels} lvl`, `${L.amp.toFixed(1)} m rough`] },
            { Label: 'Distance', Highlight: true, Resolve: () => [`${L.distance} m`, `${(L.distance / 343).toFixed(1)} s delay`] }
        ]);
        Page.append(C.Construct('div', 'section-subhead', [C.Construct('span', null, 'Quick controls'), C.Construct('span', null, 'Storm')]));
        const Grid = C.Construct('div', 'property-switches');
        C.ToggleTile(Grid, BindScene(L, 'auto', { Label: 'Auto strikes', Glyph: 'Zap' }, 'LIGHTNING', 'Lightning'));
        C.ToggleTile(Grid, BindScene(Scene.Web, 'Enabled', { Label: 'Lightning web', Glyph: 'Lattice' }, 'LIGHTNING', 'Web'));
        C.ToggleTile(Grid, {
            Label: 'Thunder audio', Glyph: 'Activity',
            Read: () => L.thunder.enabled,
            Write: On => { if (On) L.thunder.enable(); else L.thunder.disable(); }
        });
        C.ToggleTile(Grid, BindScene(Scene.Overlay, 'Enabled', { Label: 'Field overlay', Glyph: 'Layers' }, 'VIEW', 'Overlay'));
        Page.append(Grid);
        C.ActionRow(Page, [{ Label: 'Strike Lightning Bolt (L)', Glyph: 'Zap', Tone: 'accent', Act: StrikeNow }]);
        return;
    }
    if (InspectorTab === 'strikes')
    {
        const Sec = C.SectionCard(Page, { Title: 'Strike frequency', Glyph: 'Zap', Badge: 'INTERVAL' }).Inner;
        C.SwitchRow(Sec, BindScene(L, 'auto', { Label: 'Automatic periodic strikes' }, 'LIGHTNING', 'Lightning'));
        Range(Sec, BindScene(L, 'intervalMin', { Label: 'Minimum interval' }, 'LIGHTNING', 'Lightning'), 0.2, 30, 0.1, 1, 's');
        Range(Sec, BindScene(L, 'intervalMax', { Label: 'Maximum interval' }, 'LIGHTNING', 'Lightning'), 0.2, 60, 0.1, 1, 's');
        Range(Sec, BindScene(L, 'distance', { Label: 'Storm distance (thunder delay)' }, 'LIGHTNING', 'Lightning'), 50, 6000, 20, 0, 'm');
        return;
    }
    if (InspectorTab === 'bolt')
    {
        const Sec = C.SectionCard(Page, { Title: 'Procedural fractal bolt', Glyph: 'Activity', Badge: 'GEOMETRY' }).Inner;
        Range(Sec, BindScene(L, 'amp', { Label: 'Jagged roughness' }, 'LIGHTNING', 'Lightning'), 0.2, 5, 0.05, 2, 'm');
        Range(Sec, Object.assign(BindScene(L, 'levels', { Label: 'Subdivision depth' }, 'LIGHTNING', 'Lightning'), { Write: V => { L.levels = Math.round(V); } }), 3, 7, 1, 0, '');
        Range(Sec, BindScene(L, 'branchChance', { Label: 'Secondary branching probability' }, 'LIGHTNING', 'Lightning'), 0, 1, 0.02, 2, '');
        const WebSec = C.SectionCard(Page, { Title: 'Inter-effect lightning arcs', Glyph: 'Lattice', Badge: 'WEB' }).Inner;
        C.SwitchRow(WebSec, BindScene(Scene.Web, 'Enabled', { Label: 'Link active emitters with arcs' }, 'LIGHTNING', 'Web'));
        Range(WebSec, BindScene(Scene.Web, 'Interval', { Label: 'Re-arc interval' }, 'LIGHTNING', 'Web'), 0.1, 3, 0.05, 2, 's');
        Range(WebSec, Object.assign(BindScene(Scene.Web, 'Links', { Label: 'Arcs per node' }, 'LIGHTNING', 'Web'), { Write: V => { Scene.Web.Links = Math.round(V); } }), 1, 4, 1, 0, '');
        return;
    }
    const Sec = C.SectionCard(Page, { Title: 'Strike telemetry', Glyph: 'Clock', Badge: 'LIVE' }).Inner;
    const Hit = C.Construct('b', null, L.lastStrike ? `(${L.lastStrike.at.map(X => X.toFixed(1)).join(', ')})` : 'None yet');
    Sec.append(C.Construct('div', 'stat-grid', [
        C.Construct('div', 'stat-cell', [C.Construct('b', null, FormatInteger(L.strikes)), C.Construct('span', null, 'Total strikes')]),
        C.Construct('div', 'stat-cell', [Hit, C.Construct('span', null, 'Last impact')]),
        C.Construct('div', 'stat-cell', [C.Construct('b', null, L.thunder.enabled ? 'Active' : 'Muted'), C.Construct('span', null, 'Thunder synth')])
    ]));
}

function RenderRecordCard(Page, Title, LinesFn)
{
    const Sec = C.SectionCard(Page, { Title, Glyph: 'Boxes', Badge: 'TOML RECORD', Open: false }).Inner;
    const Block = C.Construct('pre', 'levelspace');
    C.ActionRow(Sec, [{
        Label: 'Copy TOML Record', Glyph: 'Copy',
        Act: () => { navigator.clipboard?.writeText(LinesFn().join('\n')).then(() => C.Toast('TOML record copied')); }
    }]);
    Sec.append(Block);
    C.Register(() => { Block.textContent = LinesFn().join('\n'); });
}

function AddForceField(Id)
{
    const Fresh = PE.Forces.BaseField(Id);
    const Same = Scene.Forces.filter(F => F.Kind === Id).length;
    if (Same) Fresh.Name = `${Fresh.Name} ${Same + 1}`;
    if (Fresh.Reaches !== PE.Forces.Reach.Everywhere) { Fresh.Centre = [0, 2, 0]; Fresh.Radius = 4; }
    Scene.Forces.push(Fresh);
    AppendLog('FORCES', `Added ${Fresh.Name}`, PE.Forces.Packable(Fresh) ? 'GPU active' : 'Authored', 'live');
    SelectNode('force:' + (Scene.Forces.length - 1));
}

function EncodeEffect(Effect)
{
    const Preset = PE.presetById(Effect.PresetId);
    const Lines = ['[[Records]]', `Identity = "${Effect.Token}"`, 'Type = "ParticleEffect"', `Name = "${Effect.Name}"`, '[Records.Configuration]', `Enabled = ${Effect.P.visible}`, `Preset = "${Effect.PresetId}"`];
    for (const [K, V] of Object.entries(Effect.P))
    {
        if (K === 'visible' || (typeof V === 'object' && !Array.isArray(V))) continue;
        if (JSON.stringify(V) === JSON.stringify(Preset.p[K])) continue;
        const Fmt = Array.isArray(V) ? `[${V.map(N => Number(N.toFixed(3))).join(', ')}]` : typeof V === 'string' ? `"${V}"` : String(Number(V.toFixed(4)));
        const PascalKey = K.charAt(0).toUpperCase() + K.slice(1);
        Lines.push(`${PascalKey} = ${Fmt}`);
    }
    return Lines;
}

function EncodeForces()
{
    const Lines = [];
    Scene.Forces.forEach((F, I) =>
    {
        if (I) Lines.push('');
        Lines.push('[[Records]]', `Identity = "ForceSource${I}"`, 'Type = "ForceSource"', `Name = "${F.Name}"`, '[Records.Configuration]',
                   `Enabled = ${F.Enabled}`, `Source = "${F.Kind}"`, `Strength = ${F.Strength}`, `BearingDegrees = ${CompassFromField(F).toFixed(1)}`);
    });
    return Lines;
}

function ConstructInspectorPage()
{
    if (PageScope) C.ReleaseScope(PageScope);
    PageScope = C.OpenScope();
    LiveRefreshers.length = 0;

    const Content = document.getElementById('InspectorContent');
    Content.textContent = '';
    const Page = C.Construct('div', 'inspector-page');
    Content.append(Page);

    const NameInput = document.getElementById('object-name');
    const TypeLabel = document.getElementById('object-type');
    const SymEl = document.getElementById('object-symbol');
    const EnabledEl = document.getElementById('object-enabled');
    const Crumb = document.getElementById('inspector-crumb');
    const VpObj = document.getElementById('viewport-object');
    const VpSub = document.getElementById('viewport-subtitle');

    const Effect = EffectByToken(Selected);
    const Field = ForceByToken(Selected);

    if (Effect)
    {
        const Preset = PE.presetById(Effect.PresetId);
        Crumb.textContent = `Inspector / Effects / ${Preset?.group || 'Particles'}`;
        NameInput.value = Effect.Name;
        NameInput.disabled = false;
        NameInput.onchange = () => { Effect.Name = NameInput.value.trim() || Effect.Name; ConstructOutliner(); C.Broadcast(); };
        TypeLabel.textContent = `${(CategoryLabels[Effect.P.kind] || 'Particles').toUpperCase()} · ${Effect.P.blend.toUpperCase()}`;
        SymEl.textContent = ''; SymEl.append(C.ConstructGlyph(GroupGlyphs[Preset?.group] || 'Sparkles'));
        SymEl.style.color = DisplayColour(Effect.P.colA);
        EnabledEl.checked = !!Effect.P.visible;
        EnabledEl.onchange = () => { Effect.P.visible = EnabledEl.checked; C.Broadcast(); };
        VpObj.textContent = Effect.Name;
        VpSub.textContent = `${CategoryLabels[Effect.P.kind] || 'Particles'} · eos engine`;
        ConstructEffectInspector(Page, Effect);
    }
    else if (Field)
    {
        const Idx = Number(Selected.slice(6));
        const Kind = PE.Forces.KindById(Field.Kind);
        const Cont = Contributions[Kind.Give];
        Crumb.textContent = `Inspector / Environment / Force`;
        NameInput.value = Field.Name;
        NameInput.disabled = false;
        NameInput.onchange = () => { Field.Name = NameInput.value.trim() || Field.Name; ConstructOutliner(); C.Broadcast(); };
        TypeLabel.textContent = `${Cont.toUpperCase()} · ${Kind.Name.toUpperCase()}`;
        SymEl.textContent = ''; SymEl.append(C.ConstructGlyph(ForceGlyphs[Field.Kind] || 'Target'));
        SymEl.style.color = ContributionColours[Cont];
        EnabledEl.checked = !!Field.Enabled;
        EnabledEl.onchange = () => { Field.Enabled = EnabledEl.checked; C.Broadcast(); };
        VpObj.textContent = Field.Name;
        VpSub.textContent = `${Cont} field · ${Field.Radius.toFixed(1)} m radius`;
        ConstructForceInspector(Page, Field, Idx);
    }
    else if (Selected === 'Wind')
    {
        Crumb.textContent = 'Inspector / Environment / Lattice';
        NameInput.value = 'Wind Lattice'; NameInput.disabled = true;
        TypeLabel.textContent = '24 × 12 × 24 VELOCITY LATTICE';
        SymEl.textContent = ''; SymEl.append(C.ConstructGlyph('Lattice')); SymEl.style.color = '#9fc7e8';
        EnabledEl.checked = Scene.Wind.windScale > 0;
        EnabledEl.onchange = () => { Scene.Wind.windScale = EnabledEl.checked ? 1 : 0; C.Broadcast(); };
        VpObj.textContent = 'Wind Lattice';
        VpSub.textContent = '12 × 8 × 12 m sampling volume';
        ConstructWindInspector(Page);
    }
    else if (Selected === 'Lightning')
    {
        Crumb.textContent = 'Inspector / Environment / Storm';
        NameInput.value = 'Lightning & Storm'; NameInput.disabled = true;
        TypeLabel.textContent = 'FRACTAL RIBBON BOLTS · THUNDER';
        SymEl.textContent = ''; SymEl.append(C.ConstructGlyph('Zap')); SymEl.style.color = '#c9b8f0';
        EnabledEl.checked = !!Scene.Lightning.auto;
        EnabledEl.onchange = () => { Scene.Lightning.auto = EnabledEl.checked; C.Broadcast(); };
        VpObj.textContent = 'Lightning & Storm';
        VpSub.textContent = `${Scene.Lightning.strikes} strikes fired`;
        ConstructLightningInspector(Page);
    }
    else
    {
        Selected = 'Field';
        Crumb.textContent = 'Inspector / Environment';
        NameInput.value = 'Force Field'; NameInput.disabled = true;
        TypeLabel.textContent = 'MASTER FIELD COMPONENT';
        SymEl.textContent = ''; SymEl.append(C.ConstructGlyph('Wind')); SymEl.style.color = '#8fd3c4';
        EnabledEl.checked = !!Scene.Overlay.Enabled;
        EnabledEl.onchange = () => { Scene.Overlay.Enabled = EnabledEl.checked; C.Broadcast(); };
        VpObj.textContent = 'Force Field';
        VpSub.textContent = 'Wind lattice & localized force overlay';
        ConstructFieldInspector(Page);
    }

    C.CloseScope(PageScope);
    document.getElementById('InspectorScroll').scrollTop = 0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                          VIEWPORT, TRANSPORT & LIVE REFRESH
//------------------------------------------------------------------------------------------------------------------------

function UpdateLiveTransport()
{
    const Tc = document.getElementById('timecode');
    const S = Scene.Time;
    const Mins = Math.floor(S / 60), Secs = Math.floor(S % 60), Ms = Math.floor((S * 1000) % 1000);
    Tc.textContent = `${String(Mins).padStart(2, '0')}:${String(Secs).padStart(2, '0')}.${String(Ms).padStart(3, '0')}`;
    document.getElementById('frame-counter').textContent = `FRAME ${String(Scene.Step).padStart(4, '0')}`;
    document.getElementById('playhead').style.left = `${((S % 30) / 30) * 100}%`;
    document.getElementById('live-pill').classList.toggle('paused', !Scene.Playing);
    document.getElementById('live-pill-label').textContent = Scene.Playing ? 'LIVE' : 'PAUSED';
}

function RefreshLive()
{
    for (const E of Scene.Effects)
    {
        E.AliveSamples.push(E.P.kind === 7 ? E.P.fibre.strands : (E.Gpu?.stats_?.alive ?? 0));
        if (E.AliveSamples.length > SampleCapacity) E.AliveSamples.shift();
    }
    ProbeSamples.push(Scene.Engine?.probeResult ? Scene.Engine.probeResult.mag : 0);
    if (ProbeSamples.length > SampleCapacity) ProbeSamples.shift();

    LiveRefreshers.forEach(Fn => Fn());
    PersistentRefreshers.forEach(Fn => Fn());

    const Hud = document.getElementById('ViewportHud');
    const Total = Scene.Effects.filter(E => E.P.visible).reduce((S, E) => S + (E.Gpu?.stats_?.alive ?? 0), 0);
    const Cap = Scene.Effects.filter(E => E.P.visible && E.P.kind !== 7).reduce((S, E) => S + (E.Gpu?.cap || 0), 0);
    Hud.textContent = '';
    Hud.append(
        C.Construct('div', 'hud-pill', ['PARTICLES', C.Construct('b', null, `${FormatInteger(Total)} / ${FormatInteger(Cap)}`)]),
        C.Construct('div', 'hud-pill', ['RATE', C.Construct('b', null, `${Math.round(Scene.Rate)} fps`)]),
        C.Construct('div', 'hud-pill', ['CPU', C.Construct('b', null, `${Scene.CpuMilliseconds.toFixed(2)} ms`)])
    );

    const Readout = document.getElementById('ViewportReadout');
    const Pr = Scene.Engine?.probeResult;
    Readout.classList.toggle('shown', Scene.ProbeEnabled && !!Pr);
    if (Pr)
    {
        Readout.textContent = `PROBE (${Scene.Probe.map(V => V.toFixed(1)).join(', ')}) m\nwind ${Pr.value.map(V => V.toFixed(2)).join('  ')} m/s\n|v| ${Pr.mag.toFixed(2)} m/s`;
    }

    const Rb = Scene.Readback?.Status;
    document.getElementById('status-readback').textContent = Rb?.Count
        ? `Lattice readback ${(Rb.Bytes / 1024).toFixed(0)} KB · ${Rb.Milliseconds.toFixed(1)} ms · ${FormatInteger(Rb.Count)}×`
        : 'Lattice readback active';
}

function ResizeCanvas()
{
    const Canvas = document.getElementById('ViewportCanvas');
    const Bounds = Canvas.parentElement.getBoundingClientRect();
    const Ratio = Math.min(window.devicePixelRatio || 1, 2);
    const W = Math.max(64, Math.floor(Bounds.width * Ratio)), H = Math.max(64, Math.floor(Bounds.height * Ratio));
    if (Canvas.width !== W || Canvas.height !== H)
    {
        Canvas.width = W; Canvas.height = H;
        Scene.Engine?.resize(W, H);
    }
}

function TogglePlay()
{
    Scene.Playing = !Scene.Playing;
    const PlayBtn = document.getElementById('play-button');
    PlayBtn.textContent = '';
    PlayBtn.append(C.ConstructGlyph(Scene.Playing ? 'Pause' : 'Play'));
    AppendLog('VIEW', Scene.Playing ? 'Simulation resumed' : 'Simulation paused', `t=${Scene.Time.toFixed(1)}s`, Scene.Playing ? 'live' : 'warm');
}

function SyncTopNav()
{
    const VeilOpen = document.getElementById('LibraryVeil').classList.contains('shown');
    document.getElementById('nav-workspace').classList.toggle('active', !VeilOpen && !LogOpen);
    document.getElementById('nav-library').classList.toggle('active', VeilOpen);
    document.getElementById('nav-log').classList.toggle('active', !VeilOpen && LogOpen);
}

function WireStaticIconsAndControls()
{
    const SetGlyph = (Id, Name) => { const El = document.getElementById(Id); if (El) { El.textContent = ''; El.append(C.ConstructGlyph(Name)); } };
    SetGlyph('help-button', 'Help');
    SetGlyph('compact-outliner', 'Compact');
    SetGlyph('add-button', 'Plus');
    SetGlyph('stat-visible-icon', 'Check');
    SetGlyph('stat-hidden-icon', 'EyeOff');
    SetGlyph('search-icon-slot', 'Search');
    SetGlyph('preset-search-icon-slot', 'Search');
    SetGlyph('viewport-title-icon', 'Sparkles');
    SetGlyph('toggle-arrows-btn', 'Wind');
    SetGlyph('toggle-minimap-btn', 'Map');
    SetGlyph('toggle-log-btn', 'Terminal');
    SetGlyph('focus-btn', 'Focus');
    SetGlyph('maximize-btn', 'Maximize');
    SetGlyph('reset-button', 'Reset');
    SetGlyph('play-button', 'Pause');
    SetGlyph('step-button', 'Step');
    SetGlyph('close-export', 'Close');
    SetGlyph('close-help', 'Close');

    const VTools = document.querySelectorAll('#ViewportTools [data-vtool]');
    const VToolIcons = { orbit: 'Orbit', probe: 'Crosshair', strike: 'Zap', turntable: 'Reset' };
    VTools.forEach(Btn =>
    {
        Btn.append(C.ConstructGlyph(VToolIcons[Btn.dataset.vtool]));
        Btn.addEventListener('click', () =>
        {
            const T = Btn.dataset.vtool;
            if (T === 'strike') { StrikeNow(); return; }
            if (T === 'probe') { Scene.ProbeEnabled = !Scene.ProbeEnabled; Btn.classList.toggle('active', Scene.ProbeEnabled); C.Broadcast(); return; }
            if (T === 'turntable') { Scene.Turntable = !Scene.Turntable; Btn.classList.toggle('active', Scene.Turntable); return; }
        });
    });

    // Transport & Viewport bar bindings
    document.getElementById('play-button').addEventListener('click', TogglePlay);
    document.getElementById('reset-button').addEventListener('click', ResetAll);
    document.getElementById('step-button').addEventListener('click', () =>
    {
        Scene.Playing = false;
        const PlayBtn = document.getElementById('play-button');
        PlayBtn.textContent = '';
        PlayBtn.append(C.ConstructGlyph('Play'));
        Scene.Playing = true;
        Advance((LastTick || performance.now()) + 16.6);
        Scene.Playing = false;
        UpdateLiveTransport();
    });
    document.getElementById('strike-button').addEventListener('click', StrikeNow);
    document.getElementById('burst-button').addEventListener('click', BurstAll);
    document.getElementById('speed-select').addEventListener('change', Ev => { Scene.TimeScale = Number(Ev.target.value); });

    document.getElementById('camera-view-select').addEventListener('change', Ev =>
    {
        const Mode = Ev.target.value;
        if (Mode === 'top') Object.assign(Scene.Camera, { Pitch: 1.45, Yaw: 0, Distance: 13.5, Target: [0, 0.8, 0] });
        else if (Mode === 'front') Object.assign(Scene.Camera, { Pitch: 0.08, Yaw: 0, Distance: 12.2, Target: [0, 1.5, 0] });
        else if (Mode === 'side') Object.assign(Scene.Camera, { Pitch: 0.1, Yaw: Math.PI * 0.5, Distance: 12.2, Target: [0, 1.5, 0] });
        else Object.assign(Scene.Camera, { Pitch: 0.34, Yaw: 0.62, Distance: 12.2, Target: [0, 1.5, 0] });
    });

    document.getElementById('overlay-channel-select').addEventListener('change', Ev =>
    {
        const V = Ev.target.value;
        if (V === 'off') Scene.Overlay.Enabled = false;
        else { Scene.Overlay.Enabled = true; Scene.Overlay.Contribution = V; }
        document.getElementById('toggle-overlay-btn').classList.toggle('active', Scene.Overlay.Enabled);
        C.Broadcast();
    });

    document.getElementById('toggle-overlay-btn').addEventListener('click', () =>
    {
        Scene.Overlay.Enabled = !Scene.Overlay.Enabled;
        document.getElementById('toggle-overlay-btn').classList.toggle('active', Scene.Overlay.Enabled);
        C.Broadcast();
    });

    document.getElementById('toggle-arrows-btn').addEventListener('click', Ev =>
    {
        Scene.Wind.showArrows = !Scene.Wind.showArrows;
        Ev.currentTarget.classList.toggle('active', Scene.Wind.showArrows);
        C.Broadcast();
    });

    document.getElementById('toggle-minimap-btn').addEventListener('click', Ev =>
    {
        Scene.Overlay.Minimap = !Scene.Overlay.Minimap;
        Ev.currentTarget.classList.toggle('active', Scene.Overlay.Minimap);
        C.Broadcast();
    });

    document.getElementById('toggle-log-btn').addEventListener('click', () =>
    {
        LogOpen = !LogOpen;
        document.getElementById('log-drawer').hidden = !LogOpen;
        document.getElementById('toggle-log-btn').classList.toggle('active', LogOpen);
        SyncTopNav();
        setTimeout(ResizeCanvas, 20);
    });

    document.getElementById('focus-btn').addEventListener('click', () => Object.assign(Scene.Camera, { Yaw: 0.62, Pitch: 0.34, Distance: 12.2, Target: [0, 1.5, 0] }));
    document.getElementById('maximize-btn').addEventListener('click', Ev =>
    {
        const Max = document.getElementById('studio-workspace').classList.toggle('maximized');
        Ev.currentTarget.classList.toggle('active', Max);
        setTimeout(ResizeCanvas, 20);
    });

    // Outliner controls
    document.getElementById('compact-outliner').addEventListener('click', Ev =>
    {
        const Compact = document.getElementById('left-panel').classList.toggle('compact');
        Ev.currentTarget.setAttribute('aria-pressed', String(Compact));
        Ev.currentTarget.classList.toggle('active', Compact);
    });
    document.getElementById('scene-search').addEventListener('input', Ev => { OutlinerQuery = Ev.target.value.trim().toLowerCase(); C.Broadcast(); });
    document.getElementById('quick-preset-search').addEventListener('input', Ev => { QuickLibraryQuery = Ev.target.value.trim().toLowerCase(); ConstructQuickLibrary(); });
    document.querySelectorAll('#scene-filters button').forEach(Btn =>
    {
        Btn.addEventListener('click', () =>
        {
            OutlinerFilter = Btn.dataset.filter;
            document.querySelectorAll('#scene-filters button').forEach(B => B.classList.toggle('active', B === Btn));
            ConstructOutliner();
            C.Broadcast();
        });
    });

    // Add popover
    const AddBtn = document.getElementById('add-button'), AddMenu = document.getElementById('add-menu');
    AddBtn.addEventListener('click', () =>
    {
        const Open = AddMenu.hidden;
        if (Open)
        {
            const R = AddBtn.getBoundingClientRect();
            AddMenu.style.left = `${R.left}px`;
            AddMenu.style.top = `${R.bottom + 6}px`;
        }
        AddMenu.hidden = !Open;
        AddBtn.setAttribute('aria-expanded', String(Open));
    });
    AddMenu.querySelectorAll('[data-add-force]').forEach(B => B.addEventListener('click', () => { AddMenu.hidden = true; AddForceField(B.dataset.addForce); }));
    AddMenu.querySelector('[data-action="open-library"]').addEventListener('click', () => { AddMenu.hidden = true; OpenFullLibrary(); });
    window.addEventListener('pointerdown', Ev => { if (!AddMenu.hidden && !AddMenu.contains(Ev.target) && !AddBtn.contains(Ev.target)) AddMenu.hidden = true; });

    // Header navigation, Library, Export & Help dialogs
    document.getElementById('nav-workspace').addEventListener('click', () =>
    {
        document.getElementById('LibraryVeil').classList.remove('shown');
        LogOpen = false;
        document.getElementById('log-drawer').hidden = true;
        document.getElementById('toggle-log-btn').classList.remove('active');
        SyncTopNav();
        setTimeout(ResizeCanvas, 20);
    });
    document.getElementById('nav-library').addEventListener('click', OpenFullLibrary);
    document.getElementById('open-full-library').addEventListener('click', OpenFullLibrary);
    document.getElementById('nav-log').addEventListener('click', () =>
    {
        document.getElementById('LibraryVeil').classList.remove('shown');
        LogOpen = !LogOpen;
        document.getElementById('log-drawer').hidden = !LogOpen;
        document.getElementById('toggle-log-btn').classList.toggle('active', LogOpen);
        SyncTopNav();
        setTimeout(ResizeCanvas, 20);
    });
    document.getElementById('new-scene-btn').addEventListener('click', () => LoadSceneTab({ Id: 'default', Name: 'New Blank Scene', Presets: ['sparks'] }));
    document.getElementById('reset-selection-btn').addEventListener('click', () =>
    {
        const Eff = EffectByToken(Selected);
        if (Eff) { const Pr = PE.presetById(Eff.PresetId); if (Pr) { Eff.P = JSON.parse(JSON.stringify(Pr.p)); RebuildGpu(Eff); ConstructInspectorPage(); C.Broadcast(); C.Toast(`Reset ${Eff.Name}`); } }
        else ResetAll();
    });

    const ExportDlg = document.getElementById('export-dialog');
    document.getElementById('export-button').addEventListener('click', () =>
    {
        const Toml = [...Scene.Effects.map(E => EncodeEffect(E).join('\n')), ...EncodeForces()].join('\n\n');
        document.getElementById('export-preview').textContent = Toml;
        ExportDlg.showModal();
    });
    document.getElementById('close-export').addEventListener('click', () => ExportDlg.close());
    document.getElementById('copy-export-btn').addEventListener('click', () =>
        navigator.clipboard?.writeText(document.getElementById('export-preview').textContent).then(() => C.Toast('Copied scene TOML')));
    document.getElementById('download-export-btn').addEventListener('click', () =>
    {
        const BlobOut = new Blob([document.getElementById('export-preview').textContent], { type: 'text/plain' });
        const Url = URL.createObjectURL(BlobOut);
        const A = document.createElement('a'); A.href = Url; A.download = 'particle-scene.levelspace'; A.click();
        URL.revokeObjectURL(Url);
    });

    const HelpDlg = document.getElementById('help-dialog');
    document.getElementById('help-button').addEventListener('click', () => HelpDlg.showModal());
    document.getElementById('close-help').addEventListener('click', () => HelpDlg.close());
}

function AttachOrbit()
{
    const Canvas = document.getElementById('ViewportCanvas');
    let Drag = null;
    Canvas.addEventListener('contextmenu', E => E.preventDefault());
    Canvas.addEventListener('pointerdown', E => { Drag = { X: E.clientX, Y: E.clientY, Pan: E.button === 2 || E.shiftKey || E.button === 1 }; Canvas.setPointerCapture(E.pointerId); });
    Canvas.addEventListener('pointermove', E =>
    {
        if (!Drag) return;
        const Dx = E.clientX - Drag.X, Dy = E.clientY - Drag.Y;
        Drag.X = E.clientX; Drag.Y = E.clientY;
        const Cam = Scene.Camera;
        if (Drag.Pan)
        {
            const S = Cam.Distance * 0.0015;
            Cam.Target[0] += -Dx * Math.cos(Cam.Yaw) * S;
            Cam.Target[2] += Dx * Math.sin(Cam.Yaw) * S;
            Cam.Target[1] += Dy * S;
        }
        else
        {
            Cam.Yaw -= Dx * 0.005;
            Cam.Pitch = Clamp(Cam.Pitch + Dy * 0.005, -1.45, 1.45);
        }
    });
    const Release = () => { Drag = null; };
    Canvas.addEventListener('pointerup', Release);
    Canvas.addEventListener('pointercancel', Release);
    Canvas.addEventListener('wheel', E => { E.preventDefault(); Scene.Camera.Distance = Clamp(Scene.Camera.Distance * Math.exp(E.deltaY * 0.001), 2, 60); }, { passive: false });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INITIALIZATION
//------------------------------------------------------------------------------------------------------------------------

async function Boot()
{
    WireStaticIconsAndControls();
    ResizeCanvas();

    const Canvas = document.getElementById('ViewportCanvas');
    try
    {
        const GpuEngine = new PE.Engine(Canvas);
        const Facts = await GpuEngine.init();
        Scene.Engine = GpuEngine;
        Scene.AdapterName = Facts.adapter || 'WebGPU GPU';
        Scene.Readback = ConstructLatticeReadback(Scene.Engine);
        Scene.Engine.onLost = Lost => AppendLog('PARTICLES', 'WebGPU device lost', Lost.message || Lost.reason, 'warm');
        document.getElementById('engine-pill-text').textContent = 'WEBGPU · LIVE';
        document.getElementById('status-ready').innerHTML = '<i></i>WebGPU ready';
    }
    catch (GpuTrouble)
    {
        console.info('WebGPU unavailable, engaging interactive software 3D preview engine:', GpuTrouble?.message);
        Scene.IsFallback = true;
        Scene.Engine = new SoftwarePreviewEngine(Canvas);
        const Facts = await Scene.Engine.init();
        Scene.AdapterName = Facts.adapter;
        Scene.Readback = new SoftwareLatticeReadback(Scene.Forces, Scene.Wind);
        const Pill = document.getElementById('engine-pill');
        Pill.classList.add('software');
        document.getElementById('engine-pill-text').textContent = 'PREVIEW · SOFTWARE 3D';
        document.getElementById('status-ready').innerHTML = '<i></i>Software 3D preview active (open in WebGPU Chrome/Edge for GPU compute)';
    }

    document.getElementById('status-adapter').textContent = Scene.AdapterName;
    Scene.Lightning = new PE.Lightning();

    const Query = new URLSearchParams(location.search).get('scene');
    const InitialIds = Query === null ? PE.defaultScene : (Query === 'none' || Query === '' ? [] : Query.split(','));
    for (const Id of InitialIds) if (PE.presetById(Id)) Scene.Effects.push(ConstructEffect(Id));
    Selected = Scene.Effects[0]?.Token || 'Field';

    ConstructSceneTabs();
    ConstructOverlay();
    ConstructOutliner();
    ConstructQuickLibrary();
    ConstructInspectorPage();

    OutlinerScroller = C.SmoothScroll(document.getElementById('OutlinerScroll'), document.getElementById('OutlinerContent'));
    InspectorScroller = C.SmoothScroll(document.getElementById('InspectorScroll'), document.getElementById('InspectorContent'));

    PE.Edits.Subscribe(() =>
    {
        document.getElementById('inspector-revision').textContent = `Parameters · Revision ${++Scene.Revisions}`;
    });

    const HandoffView = {
        get selection() { const E = EffectByToken(Selected); return E ? { type: 'system', id: E.Number } : { type: 'other' }; },
        get systems() { return Scene.Effects.map(E => ({ id: E.Number, presetId: E.PresetId, name: E.Name, p: E.P })); }
    };
    PE.Handoff.Install(HandoffView, {
        Open: (PresetId, Name) =>
        {
            const Ex = Scene.Effects.find(E => E.PresetId === PresetId);
            const E = Ex || (PE.presetById(PresetId) ? AddEffect(PresetId) : null);
            if (!E) return null;
            if (Name) E.Name = Name;
            Selected = E.Token;
            return { p: E.P, Effect: E };
        },
        Rebuild: Opened => RebuildGpu(Opened.Effect),
        Refresh: () => { ConstructOutliner(); ConstructInspectorPage(); C.Broadcast(); }
    });

    AttachOrbit();
    window.addEventListener('resize', ResizeCanvas);
    new ResizeObserver(ResizeCanvas).observe(Canvas.parentElement);

    window.addEventListener('keydown', Ev =>
    {
        if (Ev.target.matches('input, select, textarea')) return;
        if ((Ev.ctrlKey || Ev.metaKey) && Ev.key.toLowerCase() === 'f') { Ev.preventDefault(); document.getElementById('scene-search').focus(); }
        else if (Ev.code === 'Space') { Ev.preventDefault(); TogglePlay(); }
        else if (Ev.key === 'l' || Ev.key === 'L') StrikeNow();
        else if (Ev.key === 'b' || Ev.key === 'B') BurstAll();
        else if (Ev.key === 'r' || Ev.key === 'R') ResetAll();
        else if (Ev.key === 's' || Ev.key === 'S') { document.getElementById('step-button').click(); }
        else if (Ev.key === 'v' || Ev.key === 'V') { document.getElementById('toggle-overlay-btn').click(); }
        else if (Ev.key === '/') { Ev.preventDefault(); location.href = '../AssetBrowser/?ws=Particle%20Lab'; }
        else if (Ev.key === 'Escape') { document.getElementById('LibraryVeil').classList.remove('shown'); SyncTopNav(); }
    });

    AppendLog('PARTICLES', `Engine ready · ${Scene.AdapterName}`, `${Scene.Effects.length} effects · ${PE.Presets.length} presets`, 'live');
    AppendLog('FORCES', 'Field visualization active', `${Scene.Forces.length} forces loaded`, 'live');
    HandleAssetDeepLink();

    C.Broadcast();
    window.ParticleEditorScene = Scene;
    window.ParticleEditorView = Visualization;
    window.ParticleEditorHold = En => { Held = !!En; if (!Held) { LastTick = 0; requestAnimationFrame(Tick); } else { Advance(performance.now()); RefreshLive(); } return Held; };
    requestAnimationFrame(Tick);
}

if (document.readyState === 'loading') window.addEventListener('DOMContentLoaded', Boot);
else Boot();
