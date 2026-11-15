/*==========================================================================================================================================
                                                           PARTICLEEDITOR.JS
==========================================================================================================================================*/
// 🧩 Frontier · Particle & Force Studio:
//    eos WebGPU compute particles seamlessly driven by the unified
//    17-type ForceFieldCatalogue & shared ForceFieldVisualization library (3D Camera overlay + Inspector Map debug canvas).

import { PE } from './Engine/pe.js';
import './Engine/presets.js';
import './Engine/shaders.js';
import './Engine/engine.js';
import './Engine/forcefields.js';
import './Engine/edits.js';
import './Engine/handoff.js';
import { ConstructLatticeReadback } from './ParticleFieldReadback.js';
import { SoftwarePreviewEngine, SoftwareLatticeReadback, EvalCarPoseCPU, EvalFerroMagnetCPU } from './SoftwarePreviewEngine.js';
import { OBJECT_SURFACES, OBJECT_STYLES, OBJECT_SAMPLE_MODES, ResolveObjectMesh, RotateEulerXYZ, SampleObjectAnchors, ParseCustomMeshFile, RegisterCustomMesh, GetCustomMeshEntry } from './Engine/ObjectSurfaceSampler.js';
import { FIREWORK_SHAPES, BuildFireworkSnapBuffer } from './Engine/FireworkPatterns.js';
import * as C from './StudioControls.js';

const SampleCapacity = 40;
const FV = () => window.ForceFieldVisualization;
const FC = () => PE.Forces;

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
    for (let CIdx = 0; CIdx < 4; CIdx++)
        for (let R = 0; R < 4; R++)
        {
            let Sum = 0;
            for (let I = 0; I < 4; I++) Sum += A[I * 4 + R] * B[CIdx * 4 + I];
            Out[CIdx * 4 + R] = Sum;
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

const CategoryLabels = { 0: 'Streak', 1: 'Wind-driven', 5: 'VFX' };
const GroupGlyphs = {
    'Cloth & Rigging': 'Cloth', 'Foliage & Flora': 'Leaf', 'Motion & Grids': 'Boxes',
    'Ferrofluid & Fields': 'Magnet', 'Fracture & Cracks': 'Zap',
    'Silk & Curl': 'Sparkles', 'Ballistics & Sparks': 'Flame', 'Pyro & Water': 'Drop',
    'Wind-Blown': 'Leaf', 'Storms & Weather': 'CloudRain',
    'Transitions': 'Boxes', 'Space & Distortion': 'Singularity',
    'Shields & Tech': 'Orbit', 'Atmosphere & Sky': 'Sparkles',
    'Energy & Tech': 'Orbit', 'Impact & Burst': 'Blast'
};
const PRECIP_IDS = ['snow', 'rain', 'hail'];
const IsArcMode = M => M === 0 || M === 1 || M === 2 || M === 3;
const GlyphForPreset = Preset => Preset?.id === 'snow' ? 'Snowflake'
    : Preset?.id === 'rain' ? 'CloudRain'
    : Preset?.id === 'hail' ? 'CloudHail'
    : Preset?.id === 'car-puddle-splash' ? 'Car'
    : (Preset?.p?.beadsMode || 0) > 0 ? 'Cloth'
    : (Preset?.p?.seedHeadMode || 0) > 0 ? 'Leaf'
    : (Preset?.p?.jellyMode || 0) > 0 ? 'Drop'
    : (Preset?.p?.mudSplashMode || 0) > 0 ? 'Drop'
    : (Preset?.p?.ferroMode || 0) > 0 ? 'Magnet'
    : (Preset?.p?.crackMode || 0) > 0 ? 'Zap'
    : (Preset?.p?.arcMode >= 2) ? 'Zap'
    : Preset?.id?.startsWith('spiderweb-') ? 'Lattice'
    : (Preset?.p?.gridMode || 0) > 0 ? 'Boxes'
    : (Preset?.p?.dynMode >= 1 && Preset?.p?.dynMode <= 3) ? 'Cloth'
    : (Preset?.p?.dynMode >= 4) ? 'Leaf'
    : Preset?.id === 'heat-shimmer' ? 'Heat'
    : Preset?.id === 'black-hole' ? 'Singularity'
    : (GroupGlyphs[Preset?.group] || GroupGlyphs[PE.vfxCategoryOf?.(Preset)] || 'Sparkles');
const ContributionColours = { Flow: '#8fd3c4', Accelerate: '#e8c27a', Damp: '#b9a3e8' };
const ContributionUnits = { Flow: 'm/s', Accelerate: 'm/s²', Damp: '1/s' };
const StatusText = { Active: 'active', Waiting: 'waiting', Armed: 'armed', Spent: 'spent', Off: 'off' };
const ParameterBounds = (P, FieldScene) =>
{
    const IsChamber = (FieldScene?.Field?.Extent ?? 12) <= 40;
    return {
        Min: IsChamber && P.ChamberMin !== undefined ? P.ChamberMin : P.Min,
        Max: IsChamber && P.ChamberMax !== undefined ? P.ChamberMax : P.Max
    };
};

const DisplayColour = Linear =>
    '#' + [0, 1, 2].map(Ch => Math.round(Clamp(C.LinearToDisplay(Clamp(Linear[Ch], 0, 1)), 0, 1) * 255).toString(16).padStart(2, '0')).join('');
const DisplayColourA = Linear =>
{
    const A = Linear && Linear.length > 3 ? Clamp(Number(Linear[3]), 0, 1) : 1;
    return `rgba(${[0, 1, 2].map(Ch => Math.round(Clamp(C.LinearToDisplay(Clamp(Linear[Ch], 0, 1)), 0, 1) * 255)).join(', ')}, ${A.toFixed(3)})`;
};
const RampStopsOf = P => (PE.getColorStops ? PE.getColorStops(P) : [{ color: [...(P.colA || [1, 1, 1, 1])], pos: 0 }, { color: [...(P.colB || [1, 1, 1, 0])], pos: 1 }]);
const RampCss = P =>
{
    const Stops = RampStopsOf(P);
    if (!Stops.length) return DisplayColour(P.colA || [1, 1, 1, 1]);
    if (Stops.length === 1) return DisplayColourA(Stops[0].color);
    const Stepped = P.colorInterp === 'stepped';
    const Parts = [];
    for (let I = 0; I < Stops.length; I++)
    {
        const C = DisplayColourA(Stops[I].color);
        const Pos = (Stops[I].pos * 100).toFixed(2) + '%';
        if (Stepped && I > 0)
        {
            Parts.push(`${DisplayColourA(Stops[I - 1].color)} ${Pos}`);
            Parts.push(`${C} ${Pos}`);
        }
        else Parts.push(`${C} ${Pos}`);
    }
    return `linear-gradient(90deg, ${Parts.join(', ')})`;
};

// Curated scene presets for the slanted Fluid document dock
const SceneTabs = [
    { Id: 'default', Name: 'Studio Default', Presets: null, Scenario: 'lab' },
    { Id: 'object_placer', Name: '3D Object Placer · MoGraph', Presets: ['effector-object-placer'], Scenario: 'lab' },
    { Id: 'effector_grids', Name: 'Effector Grids & Halftones', Presets: ['effector-grid-cubes', 'effector-sphere-dots', 'effector-hand-halftone', 'effector-torus-hex'], Scenario: 'lab' },
    { Id: 'rigging_cloth', Name: 'Cloth, Sails, Cables & Vines', Presets: ['flag-banner', 'ship-sail', 'cables-chains', 'vines-ropes'], Scenario: 'lab' },
    { Id: 'beads_chimes', Name: 'Bead Curtains, Chimes & Drapes', Presets: ['beaded-door-curtain', 'bamboo-wind-chimes', 'velvet-theater-drapes'], Scenario: 'lab' },
    { Id: 'foliage', Name: 'Grass, Reeds, Ferns & Tree', Presets: ['grass-wheat-field', 'tall-reeds', 'interactive-fern', 'interactive-tree'], Scenario: 'lab' },
    { Id: 'dandelions', Name: 'Dandelions & Wish Seeds', Presets: ['dandelion-puff'], Scenario: 'lab' },
    { Id: 'underwater_reef', Name: 'Underwater Kelp & Anemones', Presets: ['kelp-forest', 'sea-anemone', 'boids-fish-school', 'bubbles'], Scenario: 'lab' },
    { Id: 'ferrofluid', Name: 'Magnetic Ferrofluid', Presets: ['ferrofluid-spikes', 'ferrofluid-orb'], Scenario: 'lab' },
    { Id: 'fracture_cracks', Name: 'Cracked Ice & Ground Fracture', Presets: ['cracked-ice-sheet', 'ground-crack-quake'], Scenario: 'blank' },
    { Id: 'spiderwebs', Name: 'Spiderwebs · Orb & Cobwebs', Presets: ['spiderweb-orb', 'spiderweb-cobweb'], Scenario: 'lab' },
    { Id: 'tesla', Name: 'Lightning Webs, Plasma Cage & Arcs', Presets: ['plasma-cage-arcs', 'lightning-web-mesh', 'tesla-sphere-lightning', 'static-sphere-shock', 'strikeSparks'], Scenario: 'blank' },
    { Id: 'curl', Name: 'Object Pulse & Bokeh Ribbons', Presets: ['bokeh-object-pulse', 'bokeh-stream-ribbons', 'curl-fur-field', 'curl-sphere-plasma'], Scenario: 'blank' },
    { Id: 'transitions', Name: 'Mesh Derez & Transitions', Presets: ['derez-cube', 'cube-coins', 'dissolve-butterflies', 'reverse-derez'], Scenario: 'blank' },
    { Id: 'distortion', Name: 'Black Hole & Heat Shimmer', Presets: ['black-hole', 'heat-shimmer'], Scenario: 'blank' },
    { Id: 'shields_aurora', Name: 'Energy Shields & Aurora Sky', Presets: ['energy-shield-hex', 'energy-shield-prism', 'aurora-borealis', 'radar-sweep', 'sonar-ping'], Scenario: 'blank' },
    { Id: 'pyro', Name: 'Pyro & Sparks', Presets: ['sparks', 'fire', 'smoke', 'embers'], Scenario: 'lab' },
    { Id: 'weather', Name: 'Weather · Snow / Rain / Hail', Presets: ['rain', 'snow', 'hail'], Scenario: 'storm' },
    { Id: 'wind', Name: 'Wind-Blown & Murmuration', Presets: ['starling-murmuration', 'leaves', 'petals', 'pollen', 'seeds'], Scenario: 'lab' },
    { Id: 'storms', Name: 'Storms & Vortex', Presets: ['tornado', 'sandstorm', 'watermist'], Scenario: 'storm' },
    { Id: 'blank', Name: 'Blank Canvas', Presets: [], Scenario: 'blank' }
];

const Scene = {
    Engine: null,
    IsFallback: false,
    Readback: null,
    Effects: [],
    FieldScene: FC().ConstructChamberScenario('lab'),
    get Forces() { return this.FieldScene.Sources; },
    Wind: Object.assign(PE.defaultWind(), { showArrows: false, showDomain: false, arrowRef: 12, arrowStride: 2, arrowStyle: 2 }),
    Camera: { Yaw: 0.62, Pitch: 0.34, Distance: 12.2, Target: [0, 1.5, 0], Focal: 29, SensorWidth: 36, Aperture: 2.8, SubjectDistance: 12.2 },
    Playing: true,
    TimeScale: 1,
    Time: 0,
    Step: 0,
    Rate: 0,
    CpuMilliseconds: 0,
    ProbeEnabled: false,
    Probe: [0, 1.5, 0],
    AnemometerPlacing: false,
    AnemometerDragging: false,
    AnemometerHover: false,
    BallPlacing: false,
    Ball: {
        Active: false,
        Pos: [-4.5, 2.2, 2.4],
        Vel: [0, 0, 0],
        Radius: 0.42,
        Impulse: 1.4,
        ThrowSpeed: 10.5,
        Gravity: 9.81,
        Bounce: 0.62
    },
    ArcHover: null,
    ArcDragging: null,
    ShowHandles: false,
    PointHover: null,
    PointDragging: null,
    ActiveInfographic: null,
    LastCamera: null,
    Turntable: false,
    Overlay: {
        Enabled: false,
        Contribution: 'Flow',
        Heatmap: false,
        HeatmapStyle: 'Smooth',
        Opacity: 0.30,
        Minimap: false,
        Bounds: false,
        Streamlines3D: false
    },
    PreparedField: null,
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
const AnemHistory = [];
const LogEntries = [];

let Selected = 'fx_1';
let InspectorTab = 'overview';
let OutlinerFilter = 'all';
let OutlinerQuery = '';
let AddMenuAssetQuery = '';
let ModalGroup = 'All';
let ModalQuery = '';
let LogChannel = 'All';
let LogOpen = false;
let Visualization = null;
let InspectorViz = null;
let InspectorDebugCanvas = null;
let PageScope = null;
let OutlinerScope = null;
let InspectorScroller = null;
let OutlinerScroller = null;
let LastTick = 0, LiveTimer = 0, CpuAccumulated = 0, CpuTicks = 0, Held = false, ReadbackTimer = 0;

//------------------------------------------------------------------------------------------------------------------------
//                                                 EFFECTS & FIELD HELPERS
//------------------------------------------------------------------------------------------------------------------------

const EffectByToken = Token => Scene.Effects.find(E => E.Token === Token) || null;
const SourceByToken = Token => Token && Token.startsWith('src:') ? Scene.FieldScene.Sources.find(S => S.Token === Token.slice(4)) || null : null;

function ApplyWindLink(Preset)
{
    const Link = Preset.windLink;
    if (!Link) return;
    const MapType = ['WindGenerator', 'GustFront', 'Tornado', 'Blast'];
    const Type = MapType[Link.type];
    if (!Type) return;
    const Ch = Link.set || {};
    if (Link.type === 0 && !Link.local)
    {
        Scene.FieldScene.Wind.Enabled = true;
        if (Ch.strength !== undefined) Scene.FieldScene.Wind.Speed = Math.max(0.5, Ch.strength);
        if (Ch.bearing !== undefined) Scene.FieldScene.Wind.Bearing = Wrap360(Ch.bearing);
        return;
    }
    let Source = Scene.FieldScene.Sources.find(S => S.Type === Type && S.Name === (Link.name || Type));
    if (!Source)
    {
        Source = FC().ConstructSource(Type, {
            Name: Link.name || FC().TypeById[Type].Name,
            Centre: Link.local ? [Preset.p.origin[0], Math.max(0.0, Preset.p.origin[1]), Preset.p.origin[2]] : [0, 0.4, 0],
            Radius: Ch.radius !== undefined ? Clamp(Ch.radius, 0.8, 6.0) : 3.2
        });
        Scene.FieldScene.Sources.push(Source);
    }
    if (Ch.radius !== undefined) Source.Radius = Clamp(Ch.radius, 0.8, 6.0);
    if (Ch.strength !== undefined) Source.Strength = Ch.strength * (Type === 'Tornado' ? 2.2 : 1.5);
    if (Ch.bearing !== undefined && 'Bearing' in Source) Source.Bearing = Wrap360(Ch.bearing);
    if (Link.local) { Source.Centre[0] = Preset.p.origin[0]; Source.Centre[2] = Preset.p.origin[2]; }
    Source.Enabled = true;
}

function TriggerArcStrike(Effect, AtTime = Scene.Time)
{
    if (!Effect || !IsArcMode(Effect.P.arcMode)) return;
    Effect.ArcStrikeAt = AtTime;
    Effect.ArcSeed = Math.floor(Math.random() * 0x7fffffff) + 1;
    const BurstCount = Effect.P.arcMode >= 2 ? 36 : (Effect.P.arcMode === 1 ? 24 : 52);
    Effect.PendingBurst = Math.min(Effect.Gpu?.cap || 600, (Effect.PendingBurst || 0) + BurstCount);
    const StrikePoint = Effect.P.arcMode >= 2 ? Effect.P.origin : (Effect.P.targetPos || Effect.P.origin);
    for (const SparkFx of Scene.Effects)
    {
        if (SparkFx !== Effect && SparkFx.P.visible && (SparkFx.PresetId === 'strikeSparks' || SparkFx.P.strikeCoupled))
        {
            SparkFx.OverrideOrigin = [...StrikePoint];
            SparkFx.PendingBurst = Math.min(SparkFx.Gpu?.cap || 500, (SparkFx.PendingBurst || 0) + 50);
        }
    }
    const Interval = Math.max(0.14, Effect.P.arcInterval ?? (Effect.P.arcMode >= 2 ? 0.32 : (Effect.P.arcMode === 1 ? 0.55 : 1.40)));
    Effect.ArcNextLoopAt = AtTime + Interval * (0.82 + 0.36 * Math.random());
}

function TriggerAllArcs()
{
    let Fired = 0;
    for (const Effect of Scene.Effects)
    {
        if (!Effect.P.visible || !IsArcMode(Effect.P.arcMode)) continue;
        TriggerArcStrike(Effect, Scene.Time);
        Fired++;
    }
    if (Fired) AppendLog('PARTICLES', `Triggered instantaneous spark arc (${Fired})`, `t=${Scene.Time.toFixed(2)}s`, 'live');
    return Fired;
}

function SeedBurstCountFor(PresetId, Params)
{
    if (!Params) return 0;
    if (IsArcMode(Params.arcMode)) return Params.arcMode >= 2 ? 36 : (Params.arcMode === 1 ? 24 : 52);
    if (Params.transition) return Math.max(1, Params.burstCount || Params.capacity || 384);
    if (Params.shape === 15 || Params.shape === 16 || Params.shape === 17) return Math.max(1, Params.burstCount || (Params.shape === 17 ? 6 : 1));
    if (Params.kind === 6) return Math.min(Params.capacity || 64, 36);
    if ((Params.fireworkMode || 0) > 0) return Math.max(120, Params.capacity || 1200);
    if ((Params.flareMode || 0) > 0) return Math.max(120, Params.capacity || 480);
    if ((Params.crackMode || 0) > 0) return Math.max(48, Params.capacity || 180);
    if ((Params.ferroMode || 0) > 0) return Math.max(128, Params.capacity || 640);
    if ((Params.beadsMode || 0) > 0) return Math.max(48, Params.capacity || 220);
    if ((Params.seedHeadMode || 0) > 0) return Math.max(120, Params.capacity || 460);
    if ((Params.jellyMode || 0) > 0) return Math.max(120, Params.capacity || 440);
    if ((Params.mudSplashMode || 0) > 0) return Math.min(Params.capacity || 220, 72);
    if ((Params.gridMode || 0) > 0) return Math.max(64, Params.capacity || 576);
    if ((Params.webMode || 0) > 0) return Math.max(64, Params.capacity || 320);
    if ((Params.dynMode || 0) > 0) return Math.max(64, Params.capacity || 64);
    if (Params.blackHole) return Math.min(Params.capacity || 2000, Math.round((Params.capacity || 2000) * 0.45));
    if ((Params.curlMode || 0) > 0) return Math.min(Params.capacity || 4000, Math.round((Params.capacity || 4000) * 0.45));
    if (PRECIP_IDS.includes(PresetId)) return Math.min(240, Math.round((Params.capacity || 1000) * 0.14));
    if ((Params.rate || 0) <= 0 && (Params.burstEvery || 0) <= 0 && (Params.burstCount || 0) > 0) return Math.min(Params.capacity || 200, Params.burstCount);
    return 0;
}

function ConstructEffect(PresetId, Name)
{
    const Preset = PE.presetById(PresetId);
    ApplyWindLink(Preset);
    const P = Preset.p;
    const IsArc = IsArcMode(P?.arcMode);
    const IsPreseed = PRECIP_IDS.includes(PresetId) || (P?.curlMode || 0) > 0 || (P?.webMode || 0) > 0 || (P?.dynMode || 0) > 0 || (P?.gridMode || 0) > 0 || (P?.ferroMode || 0) > 0 || (P?.crackMode || 0) > 0 || (P?.mudSplashMode || 0) > 0 || (P?.beadsMode || 0) > 0 || (P?.seedHeadMode || 0) > 0 || (P?.jellyMode || 0) > 0 || (P?.flareMode || 0) > 0 || (P?.fireworkMode || 0) > 0 || !!P?.blackHole;
    const SeedBurst = SeedBurstCountFor(PresetId, P);
    const HasImmediateBurst = !!(P?.transition || (P?.shape >= 15 && P?.burstEvery > 0));
    const Interval = Math.max(0.14, P?.arcInterval ?? (P?.arcMode >= 2 ? 0.32 : (P?.arcMode === 1 ? 0.55 : 1.40)));
    const Effect = {
        Token: 'fx_' + Scene.NextNumber,
        Number: Scene.NextNumber++,
        PresetId,
        Name: Name || Preset.name,
        P: JSON.parse(JSON.stringify(P)),
        Gpu: null,
        Accumulated: 0,
        PendingBurst: SeedBurst,
        PreseedCurtain: IsPreseed,
        ArcStrikeAt: IsArc ? Scene.Time : -99,
        ArcSeed: Math.floor(Math.random() * 0x7fffffff) + 1,
        ArcNextLoopAt: Scene.Time + Interval,
        CrackHit: null,
        OverrideOrigin: null,
        NeedFill: true,
        Head: 0,
        BurstTimer: HasImmediateBurst ? (P.burstEvery || undefined) : undefined,
        AliveSamples: []
    };
    Effect.Gpu = Scene.Engine.createSystem(Effect.P);
    return Effect;
}

function RebuildGpu(Effect)
{
    if (Effect.Gpu) Scene.Engine.destroySystem(Effect.Gpu);
    Effect.Gpu = Scene.Engine.createSystem(Effect.P);
    const P = Effect.P;
    const IsArc = IsArcMode(P?.arcMode);
    const IsPreseed = PRECIP_IDS.includes(Effect.PresetId) || (P?.curlMode || 0) > 0 || (P?.webMode || 0) > 0 || (P?.dynMode || 0) > 0 || (P?.gridMode || 0) > 0 || (P?.ferroMode || 0) > 0 || (P?.crackMode || 0) > 0 || (P?.mudSplashMode || 0) > 0 || (P?.beadsMode || 0) > 0 || (P?.seedHeadMode || 0) > 0 || (P?.jellyMode || 0) > 0 || (P?.flareMode || 0) > 0 || (P?.fireworkMode || 0) > 0 || !!P?.blackHole;
    const SeedBurst = SeedBurstCountFor(Effect.PresetId, P);
    const HasImmediateBurst = !!(P?.transition || (P?.shape >= 15 && P?.burstEvery > 0));
    Object.assign(Effect, {
        NeedFill: true,
        Head: 0,
        Accumulated: 0,
        PendingBurst: SeedBurst,
        PreseedCurtain: IsPreseed,
        CrackHit: null,
        BurstTimer: HasImmediateBurst ? (P.burstEvery || undefined) : undefined,
        ArcStrikeAt: IsArc ? Scene.Time : -99,
        ArcSeed: Math.floor(Math.random() * 0x7fffffff) + 1,
        ArcNextLoopAt: Scene.Time + Math.max(0.14, P?.arcInterval ?? 1.4)
    });
}

function AddEffect(PresetId)
{
    const Preset = PE.presetById(PresetId);
    if (!Preset) return null;
    const Same = Scene.Effects.filter(E => E.PresetId === PresetId).length;
    const Effect = ConstructEffect(PresetId, Same ? `${Preset.name} ${Same + 1}` : Preset.name);
    Scene.Effects.push(Effect);
    SelectNode(Effect.Token);
    const VfxCat = PE.vfxCategoryOf ? PE.vfxCategoryOf(Preset) : Preset.group;
    AppendLog('PARTICLES', `Added ${Effect.Name}`, `${VfxCat} · ${PE.vfxSublabelOf ? PE.vfxSublabelOf(Preset) : (CategoryLabels[Effect.P.kind] || 'Particles')}`, 'live');
    ConstructAddMenuAssets();
    C.Broadcast();
    return Effect;
}

function RemoveEffect(Effect)
{
    Scene.Engine.destroySystem(Effect.Gpu);
    Scene.Effects = Scene.Effects.filter(E => E !== Effect);
    SelectNode(Scene.Effects[0]?.Token || 'Field');
    AppendLog('PARTICLES', `Removed ${Effect.Name}`, null, 'warm');
    ConstructAddMenuAssets();
    C.Broadcast();
}

function DuplicateEffect(Effect)
{
    const IsArc = IsArcMode(Effect.P?.arcMode);
    const Copy = {
        Token: 'fx_' + Scene.NextNumber,
        Number: Scene.NextNumber++,
        PresetId: Effect.PresetId,
        Name: `${Effect.Name} copy`,
        P: JSON.parse(JSON.stringify(Effect.P)),
        Gpu: null,
        Accumulated: 0,
        PendingBurst: IsArc ? (Effect.P.arcMode >= 2 ? 36 : (Effect.P.arcMode === 1 ? 24 : 52)) : 0,
        ArcStrikeAt: IsArc ? Scene.Time : -99,
        ArcSeed: Math.floor(Math.random() * 0x7fffffff) + 1,
        ArcNextLoopAt: Scene.Time + Math.max(0.14, Effect.P?.arcInterval ?? 1.4),
        CrackHit: null,
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

function AddForceSource(Type, CentreX = 0, CentreZ = 0)
{
    const IsFormula = Type === '__Formula__';
    const BaseType = IsFormula ? 'Attractor' : Type;
    const Meta = FC().TypeById[BaseType];
    if (!Meta) return null;
    const Same = Scene.FieldScene.Sources.filter(S => IsFormula ? S.UseFormula : (S.Type === BaseType && !S.UseFormula)).length;
    const Fresh = FC().ConstructSource(BaseType, {
        Name: IsFormula ? (Same ? `Custom Formula ${Same + 1}` : 'Custom Formula Field') : (Same ? `${Meta.Name} ${Same + 1}` : Meta.Name),
        Centre: [CentreX, Meta.ChamberDefaults?.CentreY ?? (Meta.Reach === 'Sphere' ? 1.6 : 0), CentreZ],
        Activation: BaseType === 'Blast' ? 'Trigger' : 'Always',
        TriggeredAt: BaseType === 'Blast' ? Scene.Time : null,
        Radius: IsFormula ? 3.4 : undefined,
        Strength: IsFormula ? 6.5 : undefined,
        Swirl: IsFormula ? 2.5 : undefined,
        UseFormula: IsFormula,
        FormulaCode: FC().DEFAULT_FORMULA_CODE
    });
    Scene.FieldScene.Sources.push(Fresh);
    AppendLog('FORCES', `Added ${Fresh.Name}`, IsFormula ? 'Symbolic fx(GPU/CPU)' : `${Meta.Contribution} · ${Fresh.Reach}`, 'live');
    InspectorTab = IsFormula ? 'formula' : 'field';
    SelectNode('src:' + Fresh.Token, false);
    C.Broadcast();
    return Fresh;
}

function DuplicateForceSource(Source)
{
    const Raw = JSON.parse(JSON.stringify(Source));
    delete Raw.Token;
    const Copy = FC().ConstructSource(Source.Type, {
        ...Raw,
        Name: `${Source.Name} copy`,
        Centre: [Source.Centre[0] + 1.2, Source.Centre[1], Source.Centre[2] + 0.8]
    });
    Scene.FieldScene.Sources.push(Copy);
    AppendLog('FORCES', `Duplicated ${Source.Name}`, null, 'live');
    SelectNode('src:' + Copy.Token);
    C.Broadcast();
}

function RemoveForceSource(Source)
{
    Scene.FieldScene.Sources = Scene.FieldScene.Sources.filter(S => S !== Source);
    if (Scene.FieldScene.Visualization.Solo === Source.Token) Scene.FieldScene.Visualization.Solo = null;
    AppendLog('FORCES', `Removed ${Source.Name}`, null, 'warm');
    SelectNode('Field');
    C.Broadcast();
}

function LoadSceneTab(TabDef)
{
    Scene.ActiveTab = TabDef.Id;
    Scene.Ball.Active = false;
    for (const E of Scene.Effects) Scene.Engine.destroySystem(E.Gpu);
    Scene.Effects = [];
    Scene.FieldScene = FC().ConstructChamberScenario(TabDef.Scenario || 'lab');
    Scene.NextNumber = 1;
    const Query = new URLSearchParams(location.search).get('scene');
    const Ids = TabDef.Presets || (Query !== null && TabDef.Id === 'default'
        ? (Query === 'none' || Query === '' ? [] : Query.split(','))
        : PE.defaultScene);
    for (const Id of Ids) if (PE.presetById(Id)) Scene.Effects.push(ConstructEffect(Id));
    Selected = Scene.Effects[0]?.Token || (Scene.FieldScene.Sources.length ? 'Field' : 'Wind');
    InspectorTab = IsForceNode(Selected) ? 'field' : 'overview';
    ConstructSceneTabs();
    ConstructOutliner();
    ConstructAddMenuAssets();
    ConstructInspectorPage();
    AppendLog('PARTICLES', `Loaded scene '${TabDef.Name}'`, `${Scene.Effects.length} effects · ${Scene.FieldScene.Sources.length} force sources`, 'live');
    C.Broadcast();
}

function ResetAll()
{
    Scene.Time = 0;
    Scene.Step = 0;
    Scene.Ball.Active = false;
    for (const Effect of Scene.Effects) RebuildGpu(Effect);
    for (const Src of Scene.FieldScene.Sources)
        if (Src.Activation === 'Trigger') Src.TriggeredAt = Src.Type === 'Blast' ? 0 : null;
    AppendLog('PARTICLES', 'Every effect & force timing reset', null, 'warm');
}

function BurstAll()
{
    let Triggered = 0;
    for (const Effect of Scene.Effects)
    {
        const Cls = ClassifyEffect(Effect.P);
        if (Cls.IsArc) { TriggerArcStrike(Effect, Scene.Time); Triggered++; }
        else if (Cls.IsCrack) { Effect.CrackHit = { x: Effect.P.origin[0], z: Effect.P.origin[2], t: Scene.Time }; Triggered++; }
        else if (Cls.IsTransition || Cls.IsDyn || Cls.IsWeb || Cls.IsGrid || Cls.IsFerro || Cls.IsBeads || Cls.IsSeedHead || Cls.IsJelly) { RebuildGpu(Effect); Triggered++; }
        else if (Cls.IsSteadyOverlay) continue;
        else if (Cls.IsExpandingWave) { Effect.PendingBurst += Math.max(1, Effect.P.burstCount || 1); Triggered++; }
        else { Effect.PendingBurst += Math.max(80, Math.round(Effect.Gpu.cap * 0.45)); Triggered++; }
    }
    if (Triggered) AppendLog('PARTICLES', 'Triggered burst, cycle, or spark strike', `${Triggered} active effect(s)`, 'live');
    else AppendLog('PARTICLES', 'Burst skipped (steady-state HUD / stream effects active)', null, 'warm');
}

const FORCE_TABS = new Set(['field', 'formula', 'layers', 'cache']);
const IsForceNode = Token => Token === 'Field' || Token === 'Wind' || String(Token || '').startsWith('src:');

function SelectNode(Token, ResetTab = true)
{
    Selected = Token;
    if (IsForceNode(Token)) { if (!FORCE_TABS.has(InspectorTab)) InspectorTab = 'field'; }
    else if (ResetTab || FORCE_TABS.has(InspectorTab)) { InspectorTab = 'overview'; }
    if (ResetTab)
    {
        const SelFx = EffectByToken(Token);
        if (SelFx && IsArcMode(SelFx.P.arcMode) && !SelFx.P.arcLoop && (Scene.Time - (SelFx.ArcStrikeAt ?? -99)) > (SelFx.P.arcHold ?? 0.30))
            TriggerArcStrike(SelFx, Scene.Time);
    }
    ConstructOutliner();
    ConstructInspectorPage();
    C.Broadcast();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   CAMERA & 3D LINES
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
    const SensorH = Math.max(8, (Cam.SensorWidth || 36) / 1.5);
    const VFov = 2 * Math.atan(SensorH / (2 * Math.max(8, Cam.Focal || 29)));
    const Proj = ConstructPerspective(VFov, Aspect, 0.05, 400);
    const Fwd = Normalize3(Subtract3(Cam.Target, Eye));
    const Right = Normalize3(Cross3(Fwd, [0, 1, 0]));
    return { ViewProjection: MultiplyProjection(Proj, View), Eye, Target: Cam.Target, Fwd, Right, Up: Cross3(Right, Fwd), VFov };
}

let Stream3DCache = { Key: '', Mesh: [] };

function ConstructGeometry()
{
    const Out = [];
    const Mesh = [];
    const ArcSegs = [];
    const ArcFlashes = [];
    const Line = (A, B, Col) => Out.push(A[0], A[1], A[2], ...Col, B[0], B[1], B[2], ...Col);
    const Tri = (P0, P1, P2, N, Col) =>
    {
        Mesh.push(P0[0], P0[1], P0[2], N[0], N[1], N[2], ...Col);
        Mesh.push(P1[0], P1[1], P1[2], N[0], N[1], N[2], ...Col);
        Mesh.push(P2[0], P2[1], P2[2], N[0], N[1], N[2], ...Col);
    };
    const Face = (A, B, C, Col) =>
    {
        const N = Normalize3(Cross3(Subtract3(B, A), Subtract3(C, A)));
        Tri(A, B, C, N, Col);
    };
    const QuadMesh = (A, B, C, D, N, Col) =>
    {
        Tri(A, B, C, N, Col);
        Tri(A, C, D, N, Col);
    };
    const OctahedronBeacon = (C, R, Col) =>
    {
        const Top = [C[0], C[1] + R * 1.15, C[2]], Bot = [C[0], C[1] - R * 1.15, C[2]];
        const Px = [C[0] + R, C[1], C[2]], Nx = [C[0] - R, C[1], C[2]];
        const Pz = [C[0], C[1], C[2] + R], Nz = [C[0], C[1], C[2] - R];
        Face(Top, Px, Nz, Col); Face(Top, Nz, Nx, Col); Face(Top, Nx, Pz, Col); Face(Top, Pz, Px, Col);
        Face(Bot, Nz, Px, Col); Face(Bot, Nx, Nz, Col); Face(Bot, Pz, Nx, Col); Face(Bot, Px, Pz, Col);
    };
    const Orthonormal = Axis =>
    {
        const F = Normalize3(Axis);
        const UpRef = Math.abs(F[1]) > 0.9 ? [0, 0, 1] : [0, 1, 0];
        const U = Normalize3(Cross3(UpRef, F));
        const V = Normalize3(Cross3(F, U));
        return [U, V, F];
    };
    const Cross = (C, S, Col) =>
    {
        Line([C[0] - S, C[1], C[2]], [C[0] + S, C[1], C[2]], Col);
        Line([C[0], C[1] - S, C[2]], [C[0], C[1] + S, C[2]], Col);
        Line([C[0], C[1], C[2] - S], [C[0], C[1], C[2] + S], Col);
    };
    const RingXZ = (Cx, Cy, Cz, R, Segs, Col) =>
    {
        for (let I = 0; I < Segs; I++)
        {
            const A0 = (I / Segs) * Math.PI * 2, A1 = ((I + 1) / Segs) * Math.PI * 2;
            Line([Cx + Math.cos(A0) * R, Cy, Cz + Math.sin(A0) * R], [Cx + Math.cos(A1) * R, Cy, Cz + Math.sin(A1) * R], Col);
        }
    };
    const Box = (Lo, Hi, Col) =>
    {
        const Pts = [
            [Lo[0], Lo[1], Lo[2]], [Hi[0], Lo[1], Lo[2]], [Hi[0], Lo[1], Hi[2]], [Lo[0], Lo[1], Hi[2]],
            [Lo[0], Hi[1], Lo[2]], [Hi[0], Hi[1], Lo[2]], [Hi[0], Hi[1], Hi[2]], [Lo[0], Hi[1], Hi[2]]
        ];
        for (const [A, B] of [[0, 1], [1, 2], [2, 3], [3, 0], [4, 5], [5, 6], [6, 7], [7, 4], [0, 4], [1, 5], [2, 6], [3, 7]]) Line(Pts[A], Pts[B], Col);
    };
    const HexToRgb01 = Hex =>
    {
        const Clean = (Hex || '#78d2c6').replace('#', '');
        const Num = parseInt(Clean.length === 6 ? Clean : '78d2c6', 16);
        return [((Num >> 16) & 255) / 255, ((Num >> 8) & 255) / 255, (Num & 255) / 255];
    };

    if (Scene.Wind.showDomain)
        Box(Domain.Minimum, [Domain.Minimum[0] + Domain.Size[0], Domain.Minimum[1] + Domain.Size[1], Domain.Minimum[2] + Domain.Size[2]], [0.68, 0.31, 0.19, 0.72]);

    // 1. Minimal Emitter Origin Markers (no bulky cone meshes)
    for (const E of Scene.Effects)
    {
        if (!E.P.visible) continue;
        const P = E.P, O = P.origin, Chosen = Selected === E.Token;
        Cross(O, Chosen ? 0.14 : 0.08, Chosen ? [0.95, 0.78, 0.35, 0.92] : [0.65, 0.70, 0.78, 0.45]);
        if (P.curlMode === 3 && (P.pulseMode || 0) > 0 && P.showObject !== false)
        {
            const Ex = Math.max(0.45, P.boxHalf?.[0] || 1.65), Ey = Math.max(0.45, P.boxHalf?.[1] || 1.65), Ez = Math.max(0.45, P.boxHalf?.[2] || 1.65);
            const ObjSh = P.objectShape ?? 0;
            const EdgeCol = Chosen ? [0.42, 0.88, 0.94, 0.42] : [0.22, 0.52, 0.58, 0.24];
            const CoreCol = [0.06, 0.09, 0.12, 0.72];
            if (ObjSh === 1)
            {
                const RCore = Math.max(0.35, Ex) * 0.96;
                const Lats = 8, Lons = 14;
                for (let Ia = 0; Ia < Lats; Ia++)
                {
                    const P0 = -Math.PI * 0.5 + (Ia / Lats) * Math.PI, P1 = -Math.PI * 0.5 + ((Ia + 1) / Lats) * Math.PI;
                    const Y0 = Math.sin(P0) * RCore, R0 = Math.cos(P0) * RCore;
                    const Y1 = Math.sin(P1) * RCore, R1 = Math.cos(P1) * RCore;
                    const Shade = Ia % 2 === 0 ? CoreCol : [CoreCol[0] * 0.85, CoreCol[1] * 0.85, CoreCol[2] * 0.9, CoreCol[3]];
                    for (let Io = 0; Io < Lons; Io++)
                    {
                        const T0 = (Io / Lons) * Math.PI * 2, T1 = ((Io + 1) / Lons) * Math.PI * 2;
                        const A00 = [O[0] + Math.cos(T0) * R0, O[1] + Y0, O[2] + Math.sin(T0) * R0];
                        const A10 = [O[0] + Math.cos(T1) * R0, O[1] + Y0, O[2] + Math.sin(T1) * R0];
                        const A01 = [O[0] + Math.cos(T0) * R1, O[1] + Y1, O[2] + Math.sin(T0) * R1];
                        const A11 = [O[0] + Math.cos(T1) * R1, O[1] + Y1, O[2] + Math.sin(T1) * R1];
                        if (Ia > 0) Face(A00, A10, A11, Shade);
                        if (Ia < Lats - 1) Face(A00, A11, A01, Shade);
                    }
                }
                RingXZ(O[0], O[1], O[2], RCore * 1.01, 28, EdgeCol);
                const CrownY = (P.pulseAxis ?? 1) === 1 ? (O[1] + RCore * 0.92) : O[1];
                RingXZ(O[0], CrownY, O[2], RCore * 0.42, 20, [0.65, 0.96, 1.0, 0.72]);
            }
            else if (ObjSh === 2)
            {
                const Rc = Math.max(0.35, Ex) * 0.96;
                RingXZ(O[0], O[1] - Ey, O[2], Rc, 28, EdgeCol);
                RingXZ(O[0], O[1] + Ey, O[2], Rc, 28, [0.65, 0.96, 1.0, 0.72]);
                for (let I = 0; I < 10; I++)
                {
                    const A = (I / 10) * Math.PI * 2;
                    Line([O[0] + Math.cos(A) * Rc, O[1] - Ey, O[2] + Math.sin(A) * Rc], [O[0] + Math.cos(A) * Rc, O[1] + Ey, O[2] + Math.sin(A) * Rc], EdgeCol);
                }
            }
            else
            {
                const Lo = [O[0] - Ex * 0.96, O[1] - Ey * 0.96, O[2] - Ez * 0.96], Hi = [O[0] + Ex * 0.96, O[1] + Ey * 0.96, O[2] + Ez * 0.96];
                Box(Lo, Hi, EdgeCol);
                const C000 = [Lo[0], Lo[1], Lo[2]], C100 = [Hi[0], Lo[1], Lo[2]], C110 = [Hi[0], Hi[1], Lo[2]], C010 = [Lo[0], Hi[1], Lo[2]];
                const C001 = [Lo[0], Lo[1], Hi[2]], C101 = [Hi[0], Lo[1], Hi[2]], C111 = [Hi[0], Hi[1], Hi[2]], C011 = [Lo[0], Hi[1], Hi[2]];
                Face(C000, C100, C110, CoreCol); Face(C000, C110, C010, CoreCol);
                Face(C001, C111, C101, CoreCol); Face(C001, C011, C111, CoreCol);
                Face(C010, C110, C111, CoreCol); Face(C010, C111, C011, CoreCol);
            }
        }
        if (!Chosen) continue;
        {
            RingXZ(O[0], O[1], O[2], 0.22, 24, [0.95, 0.78, 0.35, 0.75]);
            if (ClassifyEffect(P).UsesDirVector)
            {
                const RawDir = P.dir || [0, 1, 0];
                const DirMag = Math.hypot(RawDir[0], RawDir[1], RawDir[2]);
                if (DirMag > 1e-4)
                {
                    const Dir = [RawDir[0] / DirMag, RawDir[1] / DirMag, RawDir[2] / DirMag];
                    Line(O, [O[0] + Dir[0] * 0.45, O[1] + Dir[1] * 0.45, O[2] + Dir[2] * 0.45], [0.95, 0.78, 0.35, 0.85]);
                }
            }
        }
    }

    // 1b. Dual-Sphere Dielectric Spark Arc (Instantaneous Plasma Breakdown + Optional Loop + Metallic Spheres)
    const DrawElectrodeSphere = (Ctr, Rad, BaseCol, RingCol, Lit) =>
    {
        const Lats = 16, Lons = 28;
        for (let Ia = 0; Ia < Lats; Ia++)
        {
            const P0 = -Math.PI * 0.5 + (Ia / Lats) * Math.PI, P1 = -Math.PI * 0.5 + ((Ia + 1) / Lats) * Math.PI;
            const Y0 = Math.sin(P0) * Rad, R0 = Math.cos(P0) * Rad;
            const Y1 = Math.sin(P1) * Rad, R1 = Math.cos(P1) * Rad;
            const Spec = Math.max(0, Math.sin((Ia / Lats) * Math.PI) * 0.35 + (Ia > Lats * 0.62 ? 0.25 : 0));
            const Shade = [
                Math.min(1, BaseCol[0] + Spec * 0.24),
                Math.min(1, BaseCol[1] + Spec * 0.28),
                Math.min(1, BaseCol[2] + Spec * 0.36),
                BaseCol[3]
            ];
            for (let Io = 0; Io < Lons; Io++)
            {
                const T0 = (Io / Lons) * Math.PI * 2, T1 = ((Io + 1) / Lons) * Math.PI * 2;
                const A00 = [Ctr[0] + Math.cos(T0) * R0, Ctr[1] + Y0, Ctr[2] + Math.sin(T0) * R0];
                const A10 = [Ctr[0] + Math.cos(T1) * R0, Ctr[1] + Y0, Ctr[2] + Math.sin(T1) * R0];
                const A01 = [Ctr[0] + Math.cos(T0) * R1, Ctr[1] + Y1, Ctr[2] + Math.sin(T0) * R1];
                const A11 = [Ctr[0] + Math.cos(T1) * R1, Ctr[1] + Y1, Ctr[2] + Math.sin(T1) * R1];
                if (Ia > 0) Face(A00, A10, A11, Shade);
                if (Ia < Lats - 1) Face(A00, A11, A01, Shade);
            }
        }
        const RingSegs = 32;
        const RingGlow = Lit ? [0.98, 0.84, 0.38, 0.92] : [RingCol[0], RingCol[1], RingCol[2], RingCol[3] * 0.62];
        RingXZ(Ctr[0], Ctr[1], Ctr[2], Rad * 1.015, RingSegs, RingGlow);
        RingXZ(Ctr[0], 0.02, Ctr[2], Math.max(0.14, Rad * 0.62), 20, [RingGlow[0], RingGlow[1], RingGlow[2], Lit ? 0.45 : 0.16]);
    };
    const ArcHash = (N, S) =>
    {
        const X = Math.sin(N * 127.1 + S * 311.7) * 43758.5453123;
        return X - Math.floor(X);
    };
    const PushArcSeg = (A, B, Wa, Wb, Col, Inten) =>
    {
        ArcSegs.push(A[0], A[1], A[2], Wa, B[0], B[1], B[2], Wb, Col[0], Col[1], Col[2], Inten);
    };
    const PushArcFlash = (Pos, Rad, Col, Inten) =>
    {
        ArcFlashes.push(Pos[0], Pos[1], Pos[2], Rad, Col[0], Col[1], Col[2], Inten);
    };
    const SubdivideSpark = (A, B, UDir, VDir, Amp, BaseW, Depth, SeedBase, CoolCol, Inten, ThermalUp, ForkQuota, MainChain) =>
    {
        const Steps = 1 << Depth;
        const Pts = new Array(Steps + 1);
        Pts[0] = [A[0], A[1], A[2]];
        Pts[Steps] = [B[0], B[1], B[2]];
        let Stride = Steps;
        let Scale = Amp;
        let Rng = 1;
        for (let D = 0; D < Depth; D++)
        {
            const Half = Stride >> 1;
            for (let I = 0; I < Steps; I += Stride)
            {
                const P0 = Pts[I], P1 = Pts[I + Stride];
                const Mx = (P0[0] + P1[0]) * 0.5, My = (P0[1] + P1[1]) * 0.5, Mz = (P0[2] + P1[2]) * 0.5;
                // Skewed sharp dielectric step offsets (avoid smooth uniform sine curves)
                const R1 = ArcHash(SeedBase + Rng * 1.73, 11.3) - 0.5;
                const R2 = ArcHash(SeedBase + Rng * 2.41, 23.7) - 0.5;
                const Kink = 0.65 + ArcHash(SeedBase + Rng * 3.19, 37.1) * 0.85;
                const Du = R1 * Scale * Kink, Dv = R2 * Scale * Kink;
                const MidT = (I + Half) / Steps;
                const Lift = Math.sin(Math.PI * MidT) * (ThermalUp / Depth);
                Pts[I + Half] = [
                    Mx + UDir[0] * Du + VDir[0] * Dv,
                    My + UDir[1] * Du + VDir[1] * Dv + Lift,
                    Mz + UDir[2] * Du + VDir[2] * Dv
                ];
                Rng++;
            }
            Stride = Half;
            Scale *= 0.54;
        }
        for (let I = 0; I < Steps; I++)
        {
            const T0 = I / Steps, T1 = (I + 1) / Steps;
            const Taper0 = MainChain ? (0.85 + 0.30 * Math.sin(Math.PI * T0)) : Math.max(0.18, 1.0 - T0 * 0.82);
            const Taper1 = MainChain ? (0.85 + 0.30 * Math.sin(Math.PI * T1)) : Math.max(0.16, 1.0 - T1 * 0.84);
            const SegFade = MainChain ? 1.0 : Math.max(0.14, 1.0 - T0 * 0.78);
            PushArcSeg(Pts[I], Pts[I + 1], BaseW * Taper0, BaseW * Taper1, CoolCol, Inten * SegFade);
        }
        // Spawn sharp acute-angle dielectric leader branches off interior knees
        if (ForkQuota > 0 && Steps >= 8)
        {
            const GapVec = Normalize3([B[0] - A[0], B[1] - A[1], B[2] - A[2]]);
            const SpanTotal = Math.hypot(B[0] - A[0], B[1] - A[1], B[2] - A[2]);
            for (let F = 0; F < ForkQuota; F++)
            {
                const Ratio = 0.18 + ((F + 0.45 * ArcHash(SeedBase, 71 + F)) / ForkQuota) * 0.64;
                const Idx = Math.max(2, Math.min(Steps - 2, Math.floor(Ratio * Steps)));
                const Root = Pts[Idx];
                const SideSign = (ArcHash(SeedBase, 91 + F) > 0.5 ? 1 : -1) * (0.48 + ArcHash(SeedBase, 95 + F) * 0.62);
                const VertSign = (ArcHash(SeedBase, 103 + F) - 0.44) * 0.85;
                const FDir = Normalize3([
                    GapVec[0] * 0.78 + UDir[0] * SideSign + VDir[0] * VertSign,
                    GapVec[1] * 0.78 + UDir[1] * SideSign + VDir[1] * VertSign,
                    GapVec[2] * 0.78 + UDir[2] * SideSign + VDir[2] * VertSign
                ]);
                const FLen = Math.min(1.6, SpanTotal * (0.20 + ArcHash(SeedBase, 113 + F) * 0.24));
                const Tip = [Root[0] + FDir[0] * FLen, Root[1] + FDir[1] * FLen, Root[2] + FDir[2] * FLen];
                const [FU, FV] = Orthonormal(FDir);
                SubdivideSpark(Root, Tip, FU, FV, FLen * 0.24, BaseW * 0.52, Math.max(2, Depth - 2), SeedBase + 409 + F * 67, CoolCol, Inten * 0.68, ThermalUp * 0.25, F % 2 === 0 && Depth >= 5 ? 1 : 0, false);
            }
        }
        return Pts;
    };
    for (const E of Scene.Effects)
    {
        if (!E.P.visible) continue;
        const P = E.P;
        if (!IsArcMode(P.arcMode)) continue;
        if (P.arcMode >= 2)
        {
            // Dynamic Plasma Cage Web (2) & Volumetric Lightning Storm Web (3) with Ball Capture
            const Org = P.origin;
            const CageR = Math.max(0.5, P.radius ?? 2.2);
            const StrikeAge = Scene.Time - (E.ArcStrikeAt ?? -99);
            const HoldDur = Math.max(0.10, P.arcHold ?? 0.22);
            const Envelope = (StrikeAge >= 0 && StrikeAge <= HoldDur)
                ? Math.max(0.45, Math.pow(1.0 - StrikeAge / HoldDur, 0.85))
                : 0.42;
            const BaseSeed = (E.ArcSeed ?? (E.Number * 97)) + Math.floor(Scene.Time * 11.0) * 13.7;
            const ColCage = P.arcMode === 2 ? [0.52, 0.68, 1.0] : [0.46, 0.82, 1.0];
            const Nodes = [];
            if (P.arcMode === 2)
            {
                const YB = Org[1] + Math.max(0.45, CageR * 0.45);
                const RingsN = 8;
                const Rot = Scene.Time * 0.16;
                Nodes.push([Org[0], Org[1] + 0.1, Org[2]]);
                for (let N = 0; N < RingsN; N++)
                {
                    const Ang = (N / RingsN) * Math.PI * 2 + Rot;
                    const PNode = [Org[0] + Math.cos(Ang) * CageR, YB + Math.sin(Ang * 2.0 + Scene.Time * 1.4) * 0.22, Org[2] + Math.sin(Ang) * CageR];
                    Nodes.push(PNode);
                    DrawElectrodeSphere(PNode, 0.11, [0.16, 0.20, 0.28, 0.96], [0.48, 0.84, 1.0, 0.92], false);
                    Line([PNode[0], 0.01, PNode[2]], PNode, [0.22, 0.26, 0.34, 0.72]);
                    if (N > 0) Line(Nodes[N], PNode, [0.28, 0.32, 0.40, 0.75]);
                }
                Line(Nodes[Nodes.length - 1], Nodes[1], [0.28, 0.32, 0.40, 0.75]);
                DrawElectrodeSphere(Nodes[0], 0.26, [0.35, 0.62, 1.0, 0.96], [0.75, 0.92, 1.0, 0.98], Selected === E.Token);
                PushArcFlash(Nodes[0], 0.42, [0.58, 0.88, 1.0], 1.15 * Envelope);
            }
            else
            {
                const Ex = Math.max(0.8, P.boxHalf?.[0] || 2.8);
                const Ey = Math.max(0.5, P.boxHalf?.[1] || 1.6);
                const Ez = Math.max(0.8, P.boxHalf?.[2] || 2.8);
                for (let N = 0; N < 9; N++)
                {
                    const Ph = Scene.Time * 0.55 + N * 1.73;
                    const PNode = [
                        Org[0] + Math.sin(Ph * 0.9 + N) * Ex * 0.85,
                        Math.max(0.32, Org[1] + Math.cos(Ph * 1.15 + N * 2.1) * Ey * 0.78),
                        Org[2] + Math.cos(Ph * 0.75 - N * 1.3) * Ez * 0.85
                    ];
                    Nodes.push(PNode);
                    DrawElectrodeSphere(PNode, 0.085, [0.22, 0.28, 0.36, 0.96], [0.52, 0.90, 1.0, 0.95], false);
                }
            }
            const Jitter = P.arcJitter ?? 0.64;
            const LinkCount = P.arcMode === 2 ? 9 : 11;
            for (let L = 0; L < LinkCount; L++)
            {
                const Ia = P.arcMode === 2 ? (L % 2 === 0 ? 0 : 1 + (L % (Nodes.length - 1))) : (L % Nodes.length);
                const Ib = P.arcMode === 2 ? (1 + ((L * 3 + Math.floor(ArcHash(BaseSeed, L) * 3)) % (Nodes.length - 1))) : ((L + 2 + Math.floor(ArcHash(BaseSeed, L * 3) * 3)) % Nodes.length);
                if (Ia === Ib) continue;
                const A = Nodes[Ia], B = Nodes[Ib];
                const DVec = Subtract3(B, A);
                const Span = Math.hypot(DVec[0], DVec[1], DVec[2]) || 0.2;
                const DirN = [DVec[0] / Span, DVec[1] / Span, DVec[2] / Span];
                const [SU, SV] = Orthonormal(DirN);
                SubdivideSpark(A, B, SU, SV, Math.min(0.75, Span * 0.22) * Jitter, 0.024, 4, BaseSeed + L * 31.7, ColCage, 1.45 * Envelope, 0.08, L % 2 === 0 ? 2 : 1, true);
                PushArcFlash(A, 0.16, ColCage, 0.95 * Envelope);
                PushArcFlash(B, 0.18, ColCage, 1.05 * Envelope);
            }
            if (Scene.Ball.Active)
            {
                const B = Scene.Ball;
                const DistToCage = Math.hypot(B.Pos[0] - Org[0], B.Pos[1] - Org[1], B.Pos[2] - Org[2]);
                if (DistToCage < CageR + 2.8)
                {
                    const CapN = Math.min(3, Nodes.length);
                    for (let C = 0; C < CapN; C++)
                    {
                        const SrcN = Nodes[(C * 3) % Nodes.length];
                        const DV = Subtract3(B.Pos, SrcN);
                        const Sp = Math.hypot(DV[0], DV[1], DV[2]) || 0.2;
                        const DN = [DV[0] / Sp, DV[1] / Sp, DV[2] / Sp];
                        const [SU, SV] = Orthonormal(DN);
                        const HitPt = [B.Pos[0] - DN[0] * B.Radius, B.Pos[1] - DN[1] * B.Radius, B.Pos[2] - DN[2] * B.Radius];
                        SubdivideSpark(SrcN, HitPt, SU, SV, Math.min(0.65, Sp * 0.24) * Jitter, 0.032, 4, BaseSeed + 901 + C * 47, [0.78, 0.72, 1.0], 2.1, 0.12, 3, true);
                        PushArcFlash(HitPt, 0.28, [0.82, 0.85, 1.0], 1.8);
                    }
                }
            }
            continue;
        }
        const IsStatic = P.arcMode === 1;
        const PosA = P.origin;
        const PosB = P.targetPos || [PosA[0] + 4, PosA[1], PosA[2]];
        const RadA = Math.max(0.08, P.radius ?? 0.45);
        const RadB = Math.max(0.08, P.targetRadius ?? 0.45);
        const ChosenFx = Selected === E.Token;
        const HoverA = Scene.ArcHover?.EffectToken === E.Token && Scene.ArcHover?.Which === 'origin';
        const HoverB = Scene.ArcHover?.EffectToken === E.Token && Scene.ArcHover?.Which === 'targetPos';
        const DragA = Scene.ArcDragging?.EffectToken === E.Token && Scene.ArcDragging?.Which === 'origin';
        const DragB = Scene.ArcDragging?.EffectToken === E.Token && Scene.ArcDragging?.Which === 'targetPos';

        if (P.showElectrodes !== false)
        {
            const ColA_Sphere = [0.16, 0.20, 0.26, 0.94];
            const ColB_Sphere = [0.20, 0.19, 0.28, 0.94];
            DrawElectrodeSphere(PosA, RadA, ColA_Sphere, IsStatic ? [0.42, 0.88, 1.0, 0.9] : [0.46, 0.80, 1.0, 0.9], ChosenFx || HoverA || DragA);
            DrawElectrodeSphere(PosB, RadB, ColB_Sphere, IsStatic ? [0.76, 0.56, 1.0, 0.9] : [0.60, 0.72, 1.0, 0.9], ChosenFx || HoverB || DragB);
        }

        const Delta = [PosB[0] - PosA[0], PosB[1] - PosA[1], PosB[2] - PosA[2]];
        const CenterDist = Math.hypot(Delta[0], Delta[1], Delta[2]) || 0.001;
        const Dir = [Delta[0] / CenterDist, Delta[1] / CenterDist, Delta[2] / CenterDist];
        const [SideU, UpPerp] = Orthonormal(Dir);

        // Compute realistic instantaneous discharge window [0 .. arcHold]
        const StrikeAge = Scene.Time - (E.ArcStrikeAt ?? -99);
        const HoldDur = Math.max(0.08, P.arcHold ?? (IsStatic ? 0.24 : 0.30));
        const ActiveStrike = StrikeAge >= 0 && StrikeAge <= HoldDur;

        // Subtle electrode rim corona strictly ON Sphere A/B when idle in static mode
        if (!ActiveStrike)
        {
            if (IsStatic)
            {
                const IdleFlicker = 0.22 + 0.16 * Math.sin(Scene.Time * 31.0 + E.Number);
                PushArcFlash([PosA[0] + Dir[0] * RadA, PosA[1] + Dir[1] * RadA, PosA[2] + Dir[2] * RadA], 0.16, [0.45, 0.82, 1.0], IdleFlicker * 0.75);
                PushArcFlash([PosB[0] - Dir[0] * RadB, PosB[1] - Dir[1] * RadB, PosB[2] - Dir[2] * RadB], 0.16, [0.68, 0.60, 1.0], IdleFlicker * 0.75);
            }
            continue;
        }

        const UAge = StrikeAge / HoldDur;
        const Snap = Math.min(1, StrikeAge / 0.012);
        const Decay = Math.pow(Math.max(0, 1.0 - UAge), IsStatic ? 1.25 : 1.45);
        const Restrike1 = Math.exp(-Math.pow((UAge - 0.38) / 0.055, 2)) * 0.62;
        const Restrike2 = Math.exp(-Math.pow((UAge - 0.70) / 0.045, 2)) * 0.34;
        const Envelope = Snap * Math.min(1.25, Decay * 1.15 + Restrike1 + Restrike2);
        if (Envelope <= 0.02) continue;

        // Temperature-mapped air-plasma cooling: blinding blue-white -> sapphire cyan -> violet afterglow
        const CoolCol = IsStatic
            ? [0.42 + 0.45 * Decay, 0.56 + 0.40 * Decay, 1.0]
            : [0.36 + 0.58 * Decay, 0.58 + 0.40 * Decay, 1.0];

        const ArchBow = P.arcBow ?? (IsStatic ? 0.32 : 0.14);
        const ArcCount = Math.max(1, Math.min(6, Math.round(P.arcCount ?? (IsStatic ? 3 : 1))));
        const Jitter = P.arcJitter ?? (IsStatic ? 0.42 : 0.62);
        const BaseSeed = (E.ArcSeed ?? (E.Number * 97)) + Math.floor(UAge * 2.6) * 7.13;

        for (let K = 0; K < ArcCount; K++)
        {
            const KSeed = BaseSeed + K * 43.17;
            const Lx = (ArcHash(KSeed, 1.1) - 0.5) * 0.54;
            const Ly = (ArcHash(KSeed, 2.2) - 0.5) * 0.44 + (ArchBow > 0 ? 0.10 : -0.10);
            const Rx = (ArcHash(KSeed, 3.3) - 0.5) * 0.52;
            const Ry = (ArcHash(KSeed, 4.4) - 0.5) * 0.42 + (ArchBow > 0 ? 0.10 : -0.10);
            const CapA = Normalize3([Dir[0] + SideU[0] * Lx + UpPerp[0] * Ly, Dir[1] + SideU[1] * Lx + UpPerp[1] * Ly, Dir[2] + SideU[2] * Lx + UpPerp[2] * Ly]);
            const CapB = Normalize3([-Dir[0] + SideU[0] * Rx + UpPerp[0] * Ry, -Dir[1] + SideU[1] * Rx + UpPerp[1] * Ry, -Dir[2] + SideU[2] * Rx + UpPerp[2] * Ry]);
            const StartPt = [PosA[0] + CapA[0] * RadA, PosA[1] + CapA[1] * RadA, PosA[2] + CapA[2] * RadA];
            const EndPt = [PosB[0] + CapB[0] * RadB, PosB[1] + CapB[1] * RadB, PosB[2] + CapB[2] * RadB];
            const SpanGap = Math.max(0.25, Math.hypot(EndPt[0] - StartPt[0], EndPt[1] - StartPt[1], EndPt[2] - StartPt[2]));
            const Amp = Math.min(1.15, SpanGap * 0.21) * Jitter * (K === 0 ? 1.0 : 0.85);
            const BaseW = (K === 0 ? 0.036 : 0.022) * (0.65 + 0.55 * Envelope);
            const BoltInten = (K === 0 ? 1.85 : 1.05) * Envelope;
            const ThermalLift = ArchBow + StrikeAge * 0.55;
            const Depth = IsStatic ? 4 : 5;
            const Forks = K === 0 ? (IsStatic ? 2 : 4) : (IsStatic ? 1 : 2);

            SubdivideSpark(StartPt, EndPt, SideU, UpPerp, Amp, BaseW, Depth, KSeed, CoolCol, BoltInten, ThermalLift, Forks, true);

            // Bright cathode spot & electrode impact corona strictly at Sphere A and Sphere B surface contacts
            PushArcFlash(StartPt, (K === 0 ? 0.24 : 0.14) * (0.55 + 0.65 * Envelope), CoolCol, (K === 0 ? 1.55 : 0.85) * Envelope);
            PushArcFlash(EndPt, (K === 0 ? 0.30 : 0.17) * (0.55 + 0.70 * Envelope), CoolCol, (K === 0 ? 1.85 : 0.95) * Envelope);
        }
    }

    // 1c. Continuous Coherent Laser & Plasma Beams (placeable Muzzle + Target, axial plasma wave & impact flares)
    for (const E of Scene.Effects)
    {
        if (!E.P.visible) continue;
        const P = E.P;
        if ((P.beamMode || 0) <= 0) continue;
        const PosA = P.origin;
        const PosB = P.targetPos || [PosA[0] + 4.8, PosA[1] - 0.8, PosA[2]];
        const Delta = [PosB[0] - PosA[0], PosB[1] - PosA[1], PosB[2] - PosA[2]];
        const Span = Math.hypot(Delta[0], Delta[1], Delta[2]) || 0.001;
        const Dir = [Delta[0] / Span, Delta[1] / Span, Delta[2] / Span];
        const [SideU, UpPerp] = Orthonormal(Dir);
        const BeamW = Math.max(0.012, P.beamWidth ?? 0.048);
        const PulseSpd = P.beamPulse ?? 6.0;
        const ColCore = (PE.evalColorRampAt && PE.getColorStops) ? PE.evalColorRampAt(PE.getColorStops(P), 0.5, P) : (P.colC || [0.35, 0.92, 1.0, 1.0]);
        const ChosenFx = Selected === E.Token;

        if (P.showElectrodes !== false)
        {
            const RingColA = ChosenFx ? [1.0, 0.85, 0.36, 0.96] : [0.38, 0.92, 1.0, 0.86];
            const RingColB = ChosenFx ? [1.0, 0.85, 0.36, 0.96] : [0.95, 0.56, 0.32, 0.88];
            RingXZ(PosA[0], PosA[1], PosA[2], Math.max(0.14, P.radius ?? 0.22), 20, RingColA);
            RingXZ(PosB[0], PosB[1], PosB[2], Math.max(0.14, P.targetRadius ?? 0.26), 20, RingColB);
            Line(PosA, [PosA[0], 0.02, PosA[2]], [RingColA[0], RingColA[1], RingColA[2], 0.28]);
            Line(PosB, [PosB[0], 0.02, PosB[2]], [RingColB[0], RingColB[1], RingColB[2], 0.28]);
        }

        const Steps = 24;
        for (let S = 0; S < Steps; S++)
        {
            const U0 = S / Steps, U1 = (S + 1) / Steps;
            const Wave0 = 0.84 + 0.32 * Math.sin(U0 * 14.0 - Scene.Time * PulseSpd * 3.0);
            const Wave1 = 0.84 + 0.32 * Math.sin(U1 * 14.0 - Scene.Time * PulseSpd * 3.0);
            const A0 = [PosA[0] + Delta[0] * U0, PosA[1] + Delta[1] * U0, PosA[2] + Delta[2] * U0];
            const A1 = [PosA[0] + Delta[0] * U1, PosA[1] + Delta[1] * U1, PosA[2] + Delta[2] * U1];
            PushArcSeg(A0, A1, BeamW * Wave0, BeamW * Wave1, ColCore, 1.85);

            // Helical plasma containment rib spiralling tightly around beam core
            const Ph0 = U0 * 12.0 - Scene.Time * PulseSpd * 4.2;
            const Ph1 = U1 * 12.0 - Scene.Time * PulseSpd * 4.2;
            const Rw = BeamW * 1.65;
            const H0 = [A0[0] + (SideU[0] * Math.cos(Ph0) + UpPerp[0] * Math.sin(Ph0)) * Rw, A0[1] + (SideU[1] * Math.cos(Ph0) + UpPerp[1] * Math.sin(Ph0)) * Rw, A0[2] + (SideU[2] * Math.cos(Ph0) + UpPerp[2] * Math.sin(Ph0)) * Rw];
            const H1 = [A1[0] + (SideU[0] * Math.cos(Ph1) + UpPerp[0] * Math.sin(Ph1)) * Rw, A1[1] + (SideU[1] * Math.cos(Ph1) + UpPerp[1] * Math.sin(Ph1)) * Rw, A1[2] + (SideU[2] * Math.cos(Ph1) + UpPerp[2] * Math.sin(Ph1)) * Rw];
            PushArcSeg(H0, H1, BeamW * 0.42, BeamW * 0.42, ColCore, 0.92);
        }

        const Flick = 0.92 + 0.18 * Math.sin(Scene.Time * 37.0 + E.Number);
        PushArcFlash(PosA, Math.max(0.24, BeamW * 6.5), ColCore, 1.45 * Flick);
        PushArcFlash(PosB, Math.max(0.32, BeamW * 8.5), [1.0, 0.78, 0.48], 1.75 * Flick);
    }
    const PoleTypes = new Set(['Attractor', 'Repulsor', 'Gravity', 'Blast', 'Magnetic']);
    for (const Src of Scene.FieldScene.Sources)
    {
        const Meta = FC().TypeById[Src.Type];
        if (!Meta || !Src.Enabled) continue;
        const SelectedSrc = Selected === ('src:' + Src.Token);
        const Rgb = Src.UseFormula ? [1.0, 0.71, 0.33] : HexToRgb01(Meta.Colour);
        const P = Src.Centre, R = Math.max(0.2, Src.Radius || 3);

        if (Src.Type === 'WindGenerator')
        {
            const Turn = ((Src.Bearing || 0) * Math.PI) / 180;
            const Fwd = [Math.sin(Turn), 0, -Math.cos(Turn)];
            const Right = [Math.cos(Turn), 0, Math.sin(Turn)];
            const Up = [0, 1, 0];
            const HubY = P[1] < 0.35 ? 1.02 : P[1];
            const Hub = [P[0], HubY, P[2]];
            const LocalPt = (Lx, Ly, Lz) => [
                Hub[0] + Right[0] * Lx + Up[0] * Ly + Fwd[0] * Lz,
                Hub[1] + Right[1] * Lx + Up[1] * Ly + Fwd[1] * Lz,
                Hub[2] + Right[2] * Lx + Up[2] * Ly + Fwd[2] * Lz
            ];

            // Sleek 32-segment chamfered airfoil shroud ring (no blocky cylinder or plume cone)
            const Segs = 32;
            const ShroudProfile = [
                [0.37, -0.09], [0.40, -0.05], [0.405, 0.04], [0.38, 0.11],
                [0.355, 0.11], [0.35, 0.0], [0.355, -0.09], [0.37, -0.09]
            ];
            const ShroudCol = SelectedSrc ? [0.30, 0.40, 0.48, 0.94] : [0.22, 0.27, 0.33, 0.92];
            const RimCol = SelectedSrc ? [0.96, 0.78, 0.36, 0.95] : [0.46, 0.80, 0.78, 0.88];
            for (let S = 0; S < Segs; S++)
            {
                const A0 = (S / Segs) * Math.PI * 2, A1 = ((S + 1) / Segs) * Math.PI * 2;
                const C0 = Math.cos(A0), S0 = Math.sin(A0), C1 = Math.cos(A1), S1 = Math.sin(A1);
                for (let K = 0; K < ShroudProfile.length - 1; K++)
                {
                    const [R0, Z0] = ShroudProfile[K], [R1, Z1] = ShroudProfile[K + 1];
                    const Q0 = LocalPt(C0 * R0, S0 * R0, Z0), Q1 = LocalPt(C1 * R0, S1 * R0, Z0);
                    const Q2 = LocalPt(C1 * R1, S1 * R1, Z1), Q3 = LocalPt(C0 * R1, S0 * R1, Z1);
                    const Col = (K === 2 || K === 3) ? RimCol : ShroudCol;
                    Face(Q0, Q1, Q2, Col); Face(Q0, Q2, Q3, Col);
                }
            }

            // Smooth parabolic nose spinner + 7 curved swept scimitar rotor blades
            const SpinSegs = 16;
            const SpinProf = [[0.088, -0.04], [0.082, 0.03], [0.052, 0.095], [0.008, 0.135]];
            const HubCol = [0.56, 0.62, 0.68, 0.96];
            for (let S = 0; S < SpinSegs; S++)
            {
                const A0 = (S / SpinSegs) * Math.PI * 2, A1 = ((S + 1) / SpinSegs) * Math.PI * 2;
                const C0 = Math.cos(A0), S0 = Math.sin(A0), C1 = Math.cos(A1), S1 = Math.sin(A1);
                for (let K = 0; K < SpinProf.length - 1; K++)
                {
                    const [R0, Z0] = SpinProf[K], [R1, Z1] = SpinProf[K + 1];
                    const Q0 = LocalPt(C0 * R0, S0 * R0, Z0), Q1 = LocalPt(C1 * R0, S1 * R0, Z0);
                    const Q2 = LocalPt(C1 * R1, S1 * R1, Z1), Q3 = LocalPt(C0 * R1, S0 * R1, Z1);
                    Face(Q0, Q1, Q2, HubCol); Face(Q0, Q2, Q3, HubCol);
                }
            }
            const BladeSpin = Scene.Time * (2.6 + Math.abs(Src.Strength || 6) * 0.72);
            const BladeCol = [0.76, 0.82, 0.86, 0.95];
            const Stations = [
                { r: 0.085, wLead: -0.022, wTrail: 0.028, zLead: 0.028, zTrail: -0.018, sweep: 0.0 },
                { r: 0.185, wLead: -0.036, wTrail: 0.042, zLead: 0.024, zTrail: -0.022, sweep: 0.14 },
                { r: 0.275, wLead: -0.030, wTrail: 0.034, zLead: 0.018, zTrail: -0.018, sweep: 0.28 },
                { r: 0.340, wLead: -0.014, wTrail: 0.016, zLead: 0.010, zTrail: -0.010, sweep: 0.38 }
            ];
            for (let B = 0; B < 7; B++)
            {
                const BaseAng = BladeSpin + (B * Math.PI * 2) / 7;
                const RingPts = Stations.map(St =>
                {
                    const A = BaseAng + St.sweep;
                    const Ca = Math.cos(A), Sa = Math.sin(A);
                    const Tx = -Sa, Ty = Ca;
                    const Cx = Ca * St.r, Cy = Sa * St.r;
                    return [
                        LocalPt(Cx + Tx * St.wLead, Cy + Ty * St.wLead, St.zLead),
                        LocalPt(Cx + Tx * St.wTrail, Cy + Ty * St.wTrail, St.zTrail)
                    ];
                });
                for (let K = 0; K < RingPts.length - 1; K++)
                {
                    const [L0, T0] = RingPts[K], [L1, T1] = RingPts[K + 1];
                    Face(L0, L1, T1, BladeCol); Face(L0, T1, T0, BladeCol);
                }
            }

            // Slim architectural hairline floor strut + base ring
            Line([P[0], 0.02, P[2]], [P[0], HubY - 0.38, P[2]], [0.46, 0.52, 0.60, SelectedSrc ? 0.85 : 0.45]);
            RingXZ(P[0], 0.03, P[2], 0.24, 24, SelectedSrc ? [0.96, 0.78, 0.36, 0.85] : [0.46, 0.52, 0.60, 0.38]);
        }
        else if (PoleTypes.has(Src.Type))
        {
            // Pole sources use crisp 2D circle-with-(+/−) badges rendered in RenderSourceDebugMarkers(); keep 3D clean
            if (SelectedSrc)
            {
                const Y0 = Math.max(0.04, P[1]);
                RingXZ(P[0], Y0, P[2], 0.32, 28, [1.0, 0.74, 0.32, 0.88]);
                if (P[1] > 0.2) Line([P[0], 0.03, P[2]], P, [...Rgb, 0.48]);
            }
        }
        else
        {
            const BeaconPos = [P[0], Math.max(0.18, P[1]), P[2]];
            OctahedronBeacon(BeaconPos, SelectedSrc ? 0.16 : 0.11, [...Rgb, 0.90]);
            if (SelectedSrc)
            {
                RingXZ(BeaconPos[0], Math.max(0.04, P[1]), BeaconPos[2], 0.30, 28, [1.0, 0.71, 0.33, 0.88]);
                if (Meta.Bearings?.length)
                {
                    const Key = Meta.Bearings[0][0];
                    const Rad = ((Src[Key] || 0) * Math.PI) / 180;
                    const Tip = [P[0] + Math.sin(Rad) * Math.min(R * 0.5, 1.4), BeaconPos[1], P[2] - Math.cos(Rad) * Math.min(R * 0.5, 1.4)];
                    Line(BeaconPos, Tip, [1.0, 0.71, 0.33, 0.85]);
                }
            }
        }

        if (Scene.Overlay.Bounds)
        {
            const BoundCol = [...Rgb, SelectedSrc ? 0.65 : 0.22];
            if (Src.Reach === 'Sphere' || Src.Reach === 'Column')
            {
                RingXZ(P[0], Math.max(0.04, P[1]), P[2], R, 40, BoundCol);
            }
            else
            {
                const Ext = FC().ResolveBoxExtents(Src);
                const Rad = ((Src.Bearing || 0) * Math.PI) / 180;
                const Rot = (Lx, Lz) => [P[0] + Lx * Math.cos(Rad) - (Lz - Ext.OffsetZ) * Math.sin(Rad), P[2] + Lx * Math.sin(Rad) + (Lz - Ext.OffsetZ) * Math.cos(Rad)];
                const C0 = Rot(-Ext.HalfX, -Ext.HalfZ), C1 = Rot(Ext.HalfX, -Ext.HalfZ), C2 = Rot(Ext.HalfX, Ext.HalfZ), C3 = Rot(-Ext.HalfX, Ext.HalfZ);
                const Y0 = Math.max(0.04, P[1]);
                for (const [A, B] of [[C0, C1], [C1, C2], [C2, C3], [C3, C0]]) Line([A[0], Y0, A[1]], [B[0], Y0, B[1]], BoundCol);
            }
        }
    }

    // 3. Volumetric 3D Bidirectional RK4 Streamlines (VectorFieldVisualizer Dual-Crossed 3D Ribbons + Arrowheads, Engineering palette, zero glow)
    if (Scene.Overlay.Enabled && Scene.Overlay.Streamlines3D && Scene.PreparedField)
    {
        const Chan = Scene.Overlay.Contribution === 'Damp' ? 'Flow' : Scene.Overlay.Contribution;
        const SliceY = Scene.FieldScene.Visualization.SliceHeight ?? 1.5;
        const Solo = Scene.FieldScene.Visualization.Solo || '';
        const HasDynamic = Scene.FieldScene.Sources.some(S => S.Enabled && S.Activation !== 'Always');
        const TimeBucket = HasDynamic ? Math.round(Scene.Time * 6) : 0;
        const SrcSig = Scene.FieldScene.Sources.map(S => `${S.Token}:${S.Enabled}:${S.Type}:${S.Strength}:${S.Swirl}:${S.Bearing}:${S.Centre.join(',')}:${S.Radius}:${S.UseFormula}:${S.FormulaCode}`).join('|');
        const StreamKey = `${Chan}:${SliceY.toFixed(2)}:${Solo}:${Scene.Wind.windScale.toFixed(2)}:${TimeBucket}:${SrcSig}`;

        if (Stream3DCache.Key !== StreamKey)
        {
            const VBuf = [0, 0, 0];
            const StepSize = 0.24, HalfSteps = 13;
            const EngStops = [[15, 28, 38], [46, 110, 125], [89, 166, 110], [212, 181, 102], [232, 107, 79]];
            const EngRgb = Mag =>
            {
                const T = Math.max(0.22, Clamp(Mag / 14, 0, 1)) * (EngStops.length - 1);
                const I = Math.min(EngStops.length - 2, Math.floor(T)), F = T - I;
                const A = EngStops[I], B = EngStops[I + 1];
                return [(A[0] + (B[0] - A[0]) * F) / 255, (A[1] + (B[1] - A[1]) * F) / 255, (A[2] + (B[2] - A[2]) * F) / 255];
            };
            const SampleUnit = (Px, Py, Pz) =>
            {
                VBuf[0] = VBuf[1] = VBuf[2] = 0;
                FC().EvaluatePrepared(Scene.FieldScene, Scene.PreparedField, Chan, Px, Py, Pz, 0.5, VBuf, Scene.FieldScene.Visualization.Solo, 'Particles');
                const Gain = Chan === 'Flow' ? (Scene.Wind?.windScale ?? 1) : 1;
                VBuf[0] *= Gain; VBuf[1] *= Gain; VBuf[2] *= Gain;
                const M = Math.hypot(VBuf[0], VBuf[1], VBuf[2]);
                return M >= 0.12 ? [VBuf[0] / M, VBuf[1] / M, VBuf[2] / M, M] : null;
            };
            const TraceDir = (Sx, Sy, Sz, Sign) =>
            {
                const Pts = [];
                let X = Sx, Y = Sy, Z = Sz, PrevDx = 0, PrevDy = 0, PrevDz = 0;
                for (let S = 0; S < HalfSteps; S++)
                {
                    const K1 = SampleUnit(X, Y, Z); if (!K1) break;
                    const Ds = Sign * StepSize, Half = Ds * 0.5;
                    const K2 = SampleUnit(X + K1[0] * Half, Y + K1[1] * Half, Z + K1[2] * Half); if (!K2) break;
                    const K3 = SampleUnit(X + K2[0] * Half, Y + K2[1] * Half, Z + K2[2] * Half); if (!K3) break;
                    const K4 = SampleUnit(X + K3[0] * Ds, Y + K3[1] * Ds, Z + K3[2] * Ds); if (!K4) break;
                    const Dx = (K1[0] + 2 * K2[0] + 2 * K3[0] + K4[0]) / 6;
                    const Dy = (K1[1] + 2 * K2[1] + 2 * K3[1] + K4[1]) / 6;
                    const Dz = (K1[2] + 2 * K2[2] + 2 * K3[2] + K4[2]) / 6;
                    if (Math.hypot(Dx, Dy, Dz) < 0.25) break;
                    if (S > 2 && (Dx * PrevDx + Dy * PrevDy + Dz * PrevDz) < -0.35) break;
                    PrevDx = Dx; PrevDy = Dy; PrevDz = Dz;
                    const Nx = X + Dx * Ds, Ny = Clamp(Y + Dy * Ds, 0.08, 7.6), Nz = Z + Dz * Ds;
                    if (Math.abs(Nx) > 5.8 || Math.abs(Nz) > 5.8) break;
                    X = Nx; Y = Ny; Z = Nz;
                    Pts.push([X, Y, Z, K2[3]]);
                }
                return Pts;
            };

            const Seeds = [];
            const Seen = new Set();
            const PushSeed = (X, Y, Z, Force = false) =>
            {
                if (Math.abs(X) > 5.6 || Math.abs(Z) > 5.6 || Y < 0.25 || Y > 7.4) return;
                const Key = `${Math.round(X / 1.15)},${Math.round(Y / 1.15)},${Math.round(Z / 1.15)}`;
                if (!Force && Seen.has(Key)) return;
                Seen.add(Key);
                Seeds.push([X, Y, Z]);
            };

            for (const Src of Scene.FieldScene.Sources)
            {
                if (!Src.Enabled || (Solo && Src.Token !== Solo)) continue;
                const RingR = Clamp((Src.Radius || 2.5) * 0.44, 0.55, 2.2);
                const Cy = Clamp(Src.Centre[1] < 0.35 ? SliceY : Src.Centre[1], 0.45, 5.8);
                for (let K = 0; K < 6; K++)
                {
                    const A = (K / 6) * Math.PI * 2 + 0.2;
                    PushSeed(Src.Centre[0] + Math.cos(A) * RingR, Cy, Src.Centre[2] + Math.sin(A) * RingR, true);
                }
                const UpY = Clamp(Cy + RingR * Math.sin(0.68), 0.6, 6.8), UpR = RingR * Math.cos(0.68);
                for (let K = 0; K < 4; K++)
                {
                    const A = (K / 4) * Math.PI * 2 + 0.5;
                    PushSeed(Src.Centre[0] + Math.cos(A) * UpR, UpY, Src.Centre[2] + Math.sin(A) * UpR, true);
                }
            }
            const StrataY = [SliceY, Clamp(SliceY + 1.6, 0.5, 5.8), Clamp(SliceY + 3.2, 0.8, 6.8)];
            const GridCoords = [-4.4, -2.2, 0.0, 2.2, 4.4];
            for (let Li = 0; Li < StrataY.length && Seeds.length < 76; Li++)
            {
                const Sy = StrataY[Li];
                for (let RIdx = 0; RIdx < GridCoords.length && Seeds.length < 76; RIdx++)
                {
                    const Stag = (RIdx % 2) * 0.9 + Li * 0.45;
                    for (const Sx of GridCoords)
                    {
                        if (Seeds.length >= 76) break;
                        PushSeed(Sx + (Stag - 0.9) * 0.45, Sy, GridCoords[RIdx]);
                    }
                }
            }

            const Built = [];
            const PushTri = (P0, P1, P2, N, Col) =>
            {
                Built.push(P0[0], P0[1], P0[2], N[0], N[1], N[2], ...Col);
                Built.push(P1[0], P1[1], P1[2], N[0], N[1], N[2], ...Col);
                Built.push(P2[0], P2[1], P2[2], N[0], N[1], N[2], ...Col);
            };
            const PushQuad = (A0, B0, A1, B1, N, Col) =>
            {
                PushTri(A0, B0, A1, N, Col);
                PushTri(B0, B1, A1, N, Col);
            };

            const HalfWidth = 0.015;
            for (const [Sx, Sy, Sz] of Seeds)
            {
                const Seed = SampleUnit(Sx, Sy, Sz);
                if (!Seed) continue;
                const Back = TraceDir(Sx, Sy, Sz, -1).reverse();
                const Fwd = TraceDir(Sx, Sy, Sz, 1);
                const Curve = [...Back, [Sx, Sy, Sz, Seed[3]], ...Fwd];
                if (Curve.length < 4) continue;

                const L1 = [], R1 = [], L2 = [], R2 = [], Cols = [], Frames = [];
                for (let I = 0; I < Curve.length; I++)
                {
                    const P = Curve[I];
                    const Prev = Curve[Math.max(0, I - 1)];
                    const Next = Curve[Math.min(Curve.length - 1, I + 1)];
                    const Tan = Normalize3([Next[0] - Prev[0], Next[1] - Prev[1], Next[2] - Prev[2]]);
                    const [Side, Norm] = Orthonormal(Tan);
                    const Pr = I / (Curve.length - 1);
                    const W = HalfWidth * (0.58 + 0.42 * Math.sin(Pr * Math.PI));
                    L1.push([P[0] - Side[0] * W, P[1] - Side[1] * W, P[2] - Side[2] * W]);
                    R1.push([P[0] + Side[0] * W, P[1] + Side[1] * W, P[2] + Side[2] * W]);
                    L2.push([P[0] - Norm[0] * W, P[1] - Norm[1] * W, P[2] - Norm[2] * W]);
                    R2.push([P[0] + Norm[0] * W, P[1] + Norm[1] * W, P[2] + Norm[2] * W]);
                    Cols.push([...EngRgb(P[3]), 0.86]);
                    Frames.push([Tan, Side, Norm]);
                }

                for (let I = 0; I < Curve.length - 1; I++)
                {
                    const [Tan, Side, Norm] = Frames[I];
                    const Col = Cols[I];
                    PushQuad(L1[I], R1[I], L1[I + 1], R1[I + 1], Norm, Col);
                    PushQuad(L2[I], R2[I], L2[I + 1], R2[I + 1], Side, Col);

                    if (I > 2 && I < Curve.length - 3 && I % 9 === 4)
                    {
                        const P = Curve[I];
                        const TipW = HalfWidth * 3.1, TipL = HalfWidth * 5.6;
                        const Tip = [P[0] + Tan[0] * TipL * 0.78, P[1] + Tan[1] * TipL * 0.78, P[2] + Tan[2] * TipL * 0.78];
                        const Bx = P[0] - Tan[0] * TipL * 0.35, By = P[1] - Tan[1] * TipL * 0.35, Bz = P[2] - Tan[2] * TipL * 0.35;
                        const ACol = [Math.min(1, Col[0] + 0.14), Math.min(1, Col[1] + 0.14), Math.min(1, Col[2] + 0.14), 0.94];
                        PushTri([Bx - Side[0] * TipW, By - Side[1] * TipW, Bz - Side[2] * TipW], [Bx + Side[0] * TipW, By + Side[1] * TipW, Bz + Side[2] * TipW], Tip, Norm, ACol);
                        PushTri([Bx - Norm[0] * TipW, By - Norm[1] * TipW, Bz - Norm[2] * TipW], [Bx + Norm[0] * TipW, By + Norm[1] * TipW, Bz + Norm[2] * TipW], Tip, Side, ACol);
                    }
                }
            }
            Stream3DCache = { Key: StreamKey, Mesh: Built };
        }
        for (let I = 0; I < Stream3DCache.Mesh.length; I++) Mesh.push(Stream3DCache.Mesh[I]);
    }

    // 4. Placed 3D Meteorological Cup Anemometer & Wind Vane Station
    if (Scene.ProbeEnabled)
    {
        const [Px, Py, Pz] = Scene.Probe;
        const Prep = Scene.PreparedField || FC().Prepare(Scene.FieldScene, Scene.Time);
        const Vz = Scene.FieldScene.Visualization;
        const ProbeAll = FC().EvaluateAllPrepared(Scene.FieldScene, Prep, Px, Py, Pz, 0.5, Vz.Solo, 'Particles');
        const ActiveProbe = Scene.AnemometerDragging || Scene.AnemometerHover || Scene.AnemometerPlacing;
        const MastCol = ActiveProbe ? [0.64, 0.94, 0.84, 0.98] : [0.40, 0.46, 0.54, 0.96];
        const HousingCol = [0.22, 0.27, 0.33, 0.96];
        const AccentCol = ActiveProbe ? [0.98, 0.80, 0.38, 0.98] : [0.46, 0.84, 0.74, 0.95];
        const CupCol = [0.84, 0.89, 0.93, 0.97];

        const CylinderY = (Cx, Y0, Y1, Cz, R0, R1, Segs, Col) =>
        {
            for (let I = 0; I < Segs; I++)
            {
                const A0 = (I / Segs) * Math.PI * 2, A1 = ((I + 1) / Segs) * Math.PI * 2;
                const C0 = Math.cos(A0), S0 = Math.sin(A0), C1 = Math.cos(A1), S1 = Math.sin(A1);
                const P00 = [Cx + C0 * R0, Y0, Cz + S0 * R0], P10 = [Cx + C1 * R0, Y0, Cz + S1 * R0];
                const P01 = [Cx + C0 * R1, Y1, Cz + S0 * R1], P11 = [Cx + C1 * R1, Y1, Cz + S1 * R1];
                Face(P00, P10, P11, Col); Face(P00, P11, P01, Col);
            }
        };

        // Tripod ground feet & slim tapered mast
        const FootR = 0.16;
        RingXZ(Px, 0.025, Pz, FootR, 24, [0.48, 0.84, 0.74, ActiveProbe ? 0.85 : 0.42]);
        for (let T = 0; T < 3; T++)
        {
            const A = (T * Math.PI * 2) / 3 + 0.35;
            Line([Px + Math.cos(A) * FootR, 0.025, Pz + Math.sin(A) * FootR], [Px, Math.min(Py * 0.28, 0.24), Pz], [0.44, 0.50, 0.58, 0.88]);
        }
        if (Py > 0.12) CylinderY(Px, 0.025, Py - 0.04, Pz, 0.020, 0.013, 8, MastCol);

        // Anemometer sensor housing hub & accent collar
        CylinderY(Px, Py - 0.045, Py + 0.045, Pz, 0.036, 0.034, 12, HousingCol);
        CylinderY(Px, Py + 0.028, Py + 0.048, Pz, 0.039, 0.039, 12, AccentCol);
        CylinderY(Px, Py + 0.048, Py + 0.095, Pz, 0.014, 0.010, 8, CupCol);

        // 3-Cup Meteorological Rotor with 3D conical cups
        const RotorTheta = Scene.Time * (1.2 + ProbeAll.FlowMag * 1.45);
        const ArmR = 0.165, RotorY = Py + 0.082;
        for (let K = 0; K < 3; K++)
        {
            const Ang = RotorTheta + (K * Math.PI * 2) / 3;
            const Ca = Math.cos(Ang), Sa = Math.sin(Ang);
            const Tx = Px + Ca * ArmR, Tz = Pz + Sa * ArmR;
            Line([Px, RotorY, Pz], [Tx, RotorY, Tz], CupCol);
            // Tangential cup axis
            const Ax = -Sa, Az = Ca;
            const Up = [0, 1, 0];
            const Rad = [Ca, 0, Sa];
            const CupR = 0.034, CupD = 0.046;
            const RimC = [Tx + Ax * 0.012, RotorY, Tz + Az * 0.012];
            const Apex = [Tx - Ax * CupD, RotorY, Tz - Az * CupD];
            for (let S = 0; S < 6; S++)
            {
                const B0 = (S / 6) * Math.PI * 2, B1 = ((S + 1) / 6) * Math.PI * 2;
                const R0 = [
                    RimC[0] + (Rad[0] * Math.cos(B0) + Up[0] * Math.sin(B0)) * CupR,
                    RimC[1] + (Rad[1] * Math.cos(B0) + Up[1] * Math.sin(B0)) * CupR,
                    RimC[2] + (Rad[2] * Math.cos(B0) + Up[2] * Math.sin(B0)) * CupR
                ];
                const R1 = [
                    RimC[0] + (Rad[0] * Math.cos(B1) + Up[0] * Math.sin(B1)) * CupR,
                    RimC[1] + (Rad[1] * Math.cos(B1) + Up[1] * Math.sin(B1)) * CupR,
                    RimC[2] + (Rad[2] * Math.cos(B1) + Up[2] * Math.sin(B1)) * CupR
                ];
                Face(R0, R1, Apex, S % 2 ? AccentCol : CupCol);
            }
        }

        // Aerodynamic Wind Vane Boom & Swept Delta Tail Fin
        const Horiz = Math.hypot(ProbeAll.Velocity[0], ProbeAll.Velocity[2]);
        const Dx = Horiz > 0.05 ? ProbeAll.Velocity[0] / Horiz : 1;
        const Dz = Horiz > 0.05 ? ProbeAll.Velocity[2] / Horiz : 0;
        const Rx = -Dz, Rz = Dx;
        const VaneY = Py - 0.025;
        const Nose = [Px + Dx * 0.16, VaneY, Pz + Dz * 0.16];
        const TailEnd = [Px - Dx * 0.21, VaneY, Pz - Dz * 0.21];
        Line(TailEnd, Nose, [0.76, 0.82, 0.88, 0.96]);
        // Forward counterweight pointer cone
        const NBase = [Px + Dx * 0.10, VaneY, Pz + Dz * 0.10];
        const N0 = [NBase[0] + Rx * 0.018, VaneY, NBase[2] + Rz * 0.018];
        const N1 = [NBase[0] - Rx * 0.018, VaneY, NBase[2] - Rz * 0.018];
        const N2 = [NBase[0], VaneY + 0.018, NBase[2]];
        const N3 = [NBase[0], VaneY - 0.018, NBase[2]];
        Face(N0, N2, Nose, [0.95, 0.78, 0.36, 0.98]);
        Face(N2, N1, Nose, [0.95, 0.78, 0.36, 0.98]);
        Face(N1, N3, Nose, [0.95, 0.78, 0.36, 0.98]);
        Face(N3, N0, Nose, [0.95, 0.78, 0.36, 0.98]);
        // Swept tail fin
        const F0 = [Px - Dx * 0.08, VaneY, Pz - Dz * 0.08];
        const F1 = [Px - Dx * 0.20, VaneY, Pz - Dz * 0.20];
        const F2 = [Px - Dx * 0.23, VaneY + 0.085, Pz - Dz * 0.23];
        const F3 = [Px - Dx * 0.13, VaneY + 0.068, Pz - Dz * 0.13];
        Face(F0, F1, F2, AccentCol);
        Face(F0, F2, F3, AccentCol);
    }

    // 5. Physical Rigging Supports (Flagpole, Ship Mast & Spars, Cable Stanchions) + Interactive 3D Projectile Ball
    const DrawCylinderPillar = (Bx, By, Bz, Tx, Ty, Tz, Rad, Col) =>
    {
        const Sides = 10;
        for (let K = 0; K < Sides; K++)
        {
            const A0 = (K / Sides) * Math.PI * 2, A1 = ((K + 1) / Sides) * Math.PI * 2;
            const B0 = [Bx + Math.cos(A0) * Rad, By, Bz + Math.sin(A0) * Rad];
            const B1 = [Bx + Math.cos(A1) * Rad, By, Bz + Math.sin(A1) * Rad];
            const T0 = [Tx + Math.cos(A0) * Rad * 0.78, Ty, Tz + Math.sin(A0) * Rad * 0.78];
            const T1 = [Tx + Math.cos(A1) * Rad * 0.78, Ty, Tz + Math.sin(A1) * Rad * 0.78];
            const Lit = 0.78 + 0.22 * Math.cos(A0 - 0.8);
            const C = [Col[0] * Lit, Col[1] * Lit, Col[2] * Lit, Col[3]];
            Face(B0, B1, T1, C); Face(B0, T1, T0, C);
        }
    };
    for (const E of Scene.Effects)
    {
        if (!E.P.visible) continue;
        const P = E.P;
        const Dyn = Math.max(0, Math.min(9, P.dynMode | 0));
        if (Dyn === 9)
        {
            const O = P.origin;
            const ReefCol = [0.16, 0.12, 0.18, 0.96];
            for (let C = 0; C < 3; C++)
            {
                const Cx = O[0] + (C - 1) * 0.88, Cz = O[2] + (C === 1 ? 0.35 : -0.25);
                const Rad = C === 1 ? 0.56 : 0.42;
                DrawElectrodeSphere([Cx, 0.04, Cz], Rad, ReefCol, [0.35, 0.25, 0.42, 0.35], false);
            }
        }
        if ((P.mudSplashMode || 0) > 0 && P.showObject !== false)
        {
            const O = P.origin;
            const MM = Math.max(1, Math.min(3, P.mudSplashMode | 0));
            const PoolR = MM === 2 ? Math.max(1.8, (P.carTrackRadius || 3.2) * 1.15) : Math.max(1.0, P.radius || 2.8);
            const PuddleCol = MM === 1 ? [0.05, 0.08, 0.12, 0.82] : [0.075, 0.052, 0.032, 0.86];
            const RimCol = MM === 1 ? [0.16, 0.24, 0.30, 0.28] : [0.22, 0.16, 0.10, 0.30];
            const Segs = 32;
            for (let S = 0; S < Segs; S++)
            {
                const A0 = (S / Segs) * Math.PI * 2, A1 = ((S + 1) / Segs) * Math.PI * 2;
                const P0 = [O[0] + Math.cos(A0) * PoolR, 0.011, O[2] + Math.sin(A0) * PoolR * 0.78];
                const P1 = [O[0] + Math.cos(A1) * PoolR, 0.011, O[2] + Math.sin(A1) * PoolR * 0.78];
                Face([O[0], 0.011, O[2]], P0, P1, PuddleCol);
                Line([P0[0], 0.013, P0[2]], [P1[0], 0.013, P1[2]], RimCol);
            }
            if (MM === 2)
            {
                const CP = EvalCarPoseCPU(Scene.Time, O, P.carTrackRadius || 3.2, P.carSpeed || 2.8);
                const Fw = CP.fwd, Rt = CP.right, P0 = CP.pos;
                const CarW = 0.66, CarL = 1.12;
                const BoxPt = (Lx, Ly, Lz) => [
                    P0[0] + Fw[0] * Lx + Rt[0] * Lz,
                    Ly,
                    P0[2] + Fw[2] * Lx + Rt[2] * Lz
                ];
                const DrawBox = (X0, X1, Y0, Y1, Z0, Z1, Col) =>
                {
                    const B00 = BoxPt(X0, Y0, Z0), B10 = BoxPt(X1, Y0, Z0), B11 = BoxPt(X1, Y0, Z1), B01 = BoxPt(X0, Y0, Z1);
                    const T00 = BoxPt(X0, Y1, Z0), T10 = BoxPt(X1, Y1, Z0), T11 = BoxPt(X1, Y1, Z1), T01 = BoxPt(X0, Y1, Z1);
                    Face(T00, T10, T11, Col); Face(T00, T11, T01, Col);
                    Face(B00, B10, T10, Col); Face(B00, T10, T00, Col);
                    Face(B01, B11, T11, Col); Face(B01, T11, T01, Col);
                    Face(B10, B11, T11, Col); Face(B10, T11, T10, Col);
                    Face(B00, B01, T01, Col); Face(B00, T01, T00, Col);
                };
                // Sleek matte slate-blue rally car body + dark cabin + glowing head/tail lights + 4 wheels
                DrawBox(-CarL, CarL, 0.16, 0.48, -CarW, CarW, [0.16, 0.24, 0.34, 0.98]);
                DrawBox(-CarL * 0.52, CarL * 0.32, 0.48, 0.78, -CarW * 0.78, CarW * 0.78, [0.07, 0.10, 0.15, 0.98]);
                for (const W of CP.wheels)
                {
                    DrawElectrodeSphere([W[0], 0.19, W[2]], 0.19, [0.06, 0.06, 0.07, 0.99], [0.46, 0.50, 0.56, 0.55], false);
                }
                for (const Sz of [-0.42, 0.42])
                {
                    const HL = BoxPt(CarL + 0.02, 0.33, Sz);
                    const TL = BoxPt(-CarL - 0.02, 0.35, Sz);
                    DrawElectrodeSphere(HL, 0.08, [1.0, 0.94, 0.72, 0.98], [1.0, 0.98, 0.85, 0.95], false);
                    DrawElectrodeSphere(TL, 0.07, [0.98, 0.22, 0.16, 0.98], [1.0, 0.38, 0.24, 0.92], false);
                    PushArcFlash(HL, 0.18, [1.0, 0.92, 0.65], 0.95);
                }
            }
        }
        if ((P.ferroMode || 0) > 0 && P.showObject !== false)
        {
            const O = P.origin;
            const FM = Math.max(1, Math.min(2, P.ferroMode | 0));
            const DishR = Math.max(0.8, P.radius || 2.05);
            if (FM === 1)
            {
                // Brushed machined-steel dish lip + glossy liquid-chrome ferrofluid pool base
                const Segs = 36;
                const RimOuter = DishR * 1.07;
                for (let S = 0; S < Segs; S++)
                {
                    const A0 = (S / Segs) * Math.PI * 2, A1 = ((S + 1) / Segs) * Math.PI * 2;
                    const C0 = Math.cos(A0), S0 = Math.sin(A0), C1 = Math.cos(A1), S1 = Math.sin(A1);
                    const Sheen = 0.5 + 0.5 * Math.cos(A0 - 0.85);
                    const PoolCol = [0.014 + 0.024 * Sheen, 0.018 + 0.032 * Sheen, 0.028 + 0.048 * Sheen, 0.99];
                    Face([O[0], O[1] + 0.018, O[2]], [O[0] + C0 * DishR, O[1] + 0.014, O[2] + S0 * DishR], [O[0] + C1 * DishR, O[1] + 0.014, O[2] + S1 * DishR], PoolCol);
                    // Metallic raised brushed-steel laboratory petri dish bezel wall
                    const RimCol = [0.24 + 0.22 * Sheen, 0.27 + 0.24 * Sheen, 0.32 + 0.26 * Sheen, 0.98];
                    Face([O[0] + C0 * DishR, O[1] + 0.014, O[2] + S0 * DishR], [O[0] + C0 * RimOuter, O[1] + 0.056, O[2] + S0 * RimOuter], [O[0] + C1 * RimOuter, O[1] + 0.056, O[2] + S1 * RimOuter], RimCol);
                    Face([O[0] + C0 * DishR, O[1] + 0.014, O[2] + S0 * DishR], [O[0] + C1 * RimOuter, O[1] + 0.056, O[2] + S1 * RimOuter], [O[0] + C1 * DishR, O[1] + 0.014, O[2] + S1 * DishR], RimCol);
                }
                RingXZ(O[0], O[1] + 0.058, O[2], RimOuter, 36, [0.76, 0.84, 0.94, 0.72]);
            }
            else
            {
                // Levitated deep obsidian ferrofluid core sphere (clean glossy silhouette, zero wireframe lines!)
                DrawElectrodeSphere(O, DishR * 0.62, [0.015, 0.020, 0.030, 0.99], [0.0, 0.0, 0.0, 0.0], false);
            }
            const Mag = EvalFerroMagnetCPU(Scene.Time, O, DishR, P.ferroMagnetSpeed ?? 1.35, FM, P);
            DrawElectrodeSphere(Mag, 0.17, [0.72, 0.78, 0.86, 0.98], P.ferroMagnetFree ? [0.95, 0.36, 0.32, 0.85] : [0.55, 0.72, 0.94, 0.55], Selected === E.Token);
        }
        if ((P.crackMode || 0) > 0 && P.showObject !== false)
        {
            const O = P.origin;
            const CM = Math.max(1, Math.min(2, P.crackMode | 0));
            const Ex = Math.max(1.2, P.boxHalf?.[0] || 2.35);
            const Ey = Math.max(0.08, P.boxHalf?.[1] || 0.14);
            const Ez = Math.max(1.2, P.boxHalf?.[2] || 2.35);
            const TopY = O[1] + Ey;
            const BotY = Math.max(0.004, O[1]);
            const HasHit = E.CrackHit && (Scene.Time - E.CrackHit.t) >= 0 && (Scene.Time - E.CrackHit.t) < 6.0;
            const CycleLen = 4.8;
            const TSince = HasHit ? Math.max(0, Scene.Time - E.CrackHit.t) : (Scene.Time % CycleLen);
            const HitX = HasHit ? E.CrackHit.x : O[0];
            const HitZ = HasHit ? E.CrackHit.z : O[2];
            const CSpd = Math.max(0.25, P.crackSpeed ?? 2.8);
            const Freq = Math.max(0.3, P.curlFreq ?? 1.15);
            const MaxR = Math.hypot(Ex, Ez) * 0.94;
            const WaveR = Math.min(MaxR, TSince * CSpd);

            // 1) Continuous Solid 3D Glacial Ice Sheet (Mode 1) or Dark Volcanic Basalt Slab (Mode 2)
            const SurfCol = CM === 1 ? [0.46, 0.78, 0.92, 0.88] : [0.16, 0.14, 0.13, 0.98];
            const SubCol = CM === 1 ? [0.10, 0.30, 0.48, 0.96] : [0.09, 0.08, 0.075, 0.99];
            const WallCol = CM === 1 ? [0.28, 0.60, 0.80, 0.94] : [0.12, 0.10, 0.095, 0.99];
            const X0 = O[0] - Ex, X1 = O[0] + Ex, Z0 = O[2] - Ez, Z1 = O[2] + Ez;
            // Deep under-ice / sub-crust backing + continuous top slab surface
            Face([X0, BotY, Z0], [X1, BotY, Z0], [X1, BotY, Z1], SubCol);
            Face([X0, BotY, Z0], [X1, BotY, Z1], [X0, BotY, Z1], SubCol);
            Face([X0, TopY, Z0], [X1, TopY, Z0], [X1, TopY, Z1], SurfCol);
            Face([X0, TopY, Z0], [X1, TopY, Z1], [X0, TopY, Z1], SurfCol);
            // 4 beveled 3D thickness side walls of the ice / basalt sheet
            Face([X0, BotY, Z1], [X1, BotY, Z1], [X1, TopY, Z1], WallCol);
            Face([X0, BotY, Z1], [X1, TopY, Z1], [X0, TopY, Z1], WallCol);
            Face([X1, BotY, Z0], [X0, BotY, Z0], [X0, TopY, Z0], WallCol);
            Face([X1, BotY, Z0], [X0, TopY, Z0], [X1, TopY, Z0], WallCol);
            Face([X1, BotY, Z1], [X1, BotY, Z0], [X1, TopY, Z0], WallCol);
            Face([X1, BotY, Z1], [X1, TopY, Z0], [X1, TopY, Z1], WallCol);
            Face([X0, BotY, Z0], [X0, BotY, Z1], [X0, TopY, Z1], WallCol);
            Face([X0, BotY, Z0], [X0, TopY, Z1], [X0, TopY, Z0], WallCol);
            // Crisp frozen rim highlight around sheet edge
            const RimEdge = CM === 1 ? [0.84, 0.97, 1.0, 0.72] : [0.34, 0.28, 0.24, 0.55];
            Line([X0, TopY + 0.004, Z0], [X1, TopY + 0.004, Z0], RimEdge);
            Line([X1, TopY + 0.004, Z0], [X1, TopY + 0.004, Z1], RimEdge);
            Line([X1, TopY + 0.004, Z1], [X0, TopY + 0.004, Z1], RimEdge);
            Line([X0, TopY + 0.004, Z1], [X0, TopY + 0.004, Z0], RimEdge);

            // 2) Deterministic Real-Time Propagating 3D Branching Fracture Network (identical math to WGSL & CPU shards!)
            const CrackHash = (S, K) => {
                const V = Math.sin(S * 12.9898 + K * 78.233) * 43758.5453;
                return V - Math.floor(V);
            };
            const ClampToBox = (Px, Pz) => [Math.max(X0 + 0.02, Math.min(X1 - 0.02, Px)), Math.max(Z0 + 0.02, Math.min(Z1 - 0.02, Pz))];
            const EvalBranchPt = (Arm, IsFork, Dist) => {
                const BaseAng = (Arm / 8) * Math.PI * 2 + (CrackHash(Arm * 19 + 3, 1) - 0.5) * 0.42;
                const U = Math.max(0, Math.min(1, Dist / Math.max(0.35, MaxR)));
                const Seg = Math.floor(U * 5);
                const Zig = (CrackHash(Arm * 31 + Seg * 7, 2) - 0.5) * 0.34 * Math.sin(U * Math.PI) + Math.sin(U * 11.5 * Freq + Arm * 2.1) * 0.08;
                if (!IsFork)
                {
                    const Ang = BaseAng + Zig;
                    return [HitX + Math.cos(Ang) * Dist, HitZ + Math.sin(Ang) * Dist];
                }
                const ForkStart = MaxR * (0.24 + 0.38 * CrackHash(Arm * 29 + 5, 3));
                const FSign = (Arm % 2 === 0) ? 1 : -1;
                const ForkAng = BaseAng + FSign * (0.52 + 0.38 * CrackHash(Arm * 41 + 7, 4));
                const TrunkR = Math.min(Dist, ForkStart);
                const TrunkZig = (CrackHash(Arm * 31 + Math.floor((TrunkR / Math.max(0.35, MaxR)) * 5) * 7, 2) - 0.5) * 0.30;
                const Jx = HitX + Math.cos(BaseAng + TrunkZig) * TrunkR;
                const Jz = HitZ + Math.sin(BaseAng + TrunkZig) * TrunkR;
                const BDist = Math.max(0, Dist - ForkStart);
                return [Jx + Math.cos(ForkAng) * BDist, Jz + Math.sin(ForkAng) * BDist];
            };
            const DrawCrackSegment = (P0, P1, W0, W1, DepthFrac, IsPrimary) => {
                const Dx = P1[0] - P0[0], Dz = P1[1] - P0[1];
                const Len = Math.hypot(Dx, Dz);
                if (Len < 1e-4) return;
                const Nx = -Dz / Len, Nz = Dx / Len;
                const YSurf = TopY + 0.008;
                const YDeep = TopY + 0.004;
                // Wide dark-sapphire internal ice cleavage plane (Mode 1) or incandescent glowing lava fissure (Mode 2)
                const CoreCol = CM === 1
                    ? [0.03, 0.14, 0.28, 0.98]
                    : [1.0, 0.44 + 0.28 * DepthFrac, 0.08 + 0.18 * DepthFrac, 0.99];
                const LipCol = CM === 1
                    ? [0.88, 0.98, 1.0, 0.95]
                    : [0.98, 0.62, 0.18, 0.92];
                const ShelfCol = CM === 1
                    ? [0.68, 0.92, 1.0, 0.52]
                    : [0.55, 0.16, 0.04, 0.72];
                // Outer internal subsurface cleavage bevel (gives real 3D cracked ice thickness refraction!)
                const Sw0 = W0 * 2.4, Sw1 = W1 * 2.4;
                Face([P0[0] - Nx * Sw0, YDeep, P0[1] - Nz * Sw0], [P0[0] + Nx * Sw0, YDeep, P0[1] + Nz * Sw0], [P1[0] + Nx * Sw1, YDeep, P1[1] + Nz * Sw1], ShelfCol);
                Face([P0[0] - Nx * Sw0, YDeep, P0[1] - Nz * Sw0], [P1[0] + Nx * Sw1, YDeep, P1[1] + Nz * Sw1], [P1[0] - Nx * Sw1, YDeep, P1[1] - Nz * Sw1], ShelfCol);
                // Core fissure ribbon
                Face([P0[0] - Nx * W0, YSurf, P0[1] - Nz * W0], [P0[0] + Nx * W0, YSurf, P0[1] + Nz * W0], [P1[0] + Nx * W1, YSurf, P1[1] + Nz * W1], CoreCol);
                Face([P0[0] - Nx * W0, YSurf, P0[1] - Nz * W0], [P1[0] + Nx * W1, YSurf, P1[1] + Nz * W1], [P1[0] - Nx * W1, YSurf, P1[1] - Nz * W1], CoreCol);
                // Crisp fractured crystal / scorched edge lines on both sides of the crack
                Line([P0[0] - Nx * W0, YSurf + 0.005, P0[1] - Nz * W0], [P1[0] - Nx * W1, YSurf + 0.005, P1[1] - Nz * W1], LipCol);
                Line([P0[0] + Nx * W0, YSurf + 0.005, P0[1] + Nz * W0], [P1[0] + Nx * W1, YSurf + 0.005, P1[1] + Nz * W1], LipCol);
                if (IsPrimary && CM === 1)
                {
                    // Sub-surface oblique ice cleavage gleam offset slightly beneath the top crack line
                    Line([P0[0] + Nx * (W0 * 1.9), YSurf + 0.002, P0[1] + Nz * (W0 * 1.9)], [P1[0] + Nx * (W1 * 1.9), YSurf + 0.002, P1[1] + Nz * (W1 * 1.9)], [0.95, 0.99, 1.0, 0.65]);
                }
            };

            if (WaveR > 0.04)
            {
                const Steps = 10;
                const BaseW = (CM === 1 ? 0.048 : 0.072) * (0.65 + 0.65 * Math.min(1, P.crackDepth ?? 0.55));
                for (let Arm = 0; Arm < 8; Arm++)
                {
                    let Prev = ClampToBox(HitX, HitZ);
                    for (let S = 1; S <= Steps; S++)
                    {
                        const D1 = (S / Steps) * WaveR;
                        const RawPt = EvalBranchPt(Arm, false, D1);
                        const Cur = ClampToBox(RawPt[0], RawPt[1]);
                        const F0 = 1 - ((S - 1) / Steps) * 0.86;
                        const F1 = 1 - (S / Steps) * 0.86;
                        DrawCrackSegment(Prev, Cur, BaseW * F0, BaseW * F1, F0, true);
                        Prev = Cur;
                        if (Math.abs(RawPt[0] - O[0]) > Ex || Math.abs(RawPt[1] - O[2]) > Ez) break;
                    }
                    // Advancing crack tip flash!
                    if (WaveR < MaxR * 0.96 && WaveR > 0.12)
                    {
                        PushArcFlash([Prev[0], TopY + 0.04, Prev[1]], CM === 1 ? 0.14 : 0.22, CM === 1 ? [0.85, 0.98, 1.0] : [1.0, 0.62, 0.18], 0.85);
                    }
                    // Secondary crystalline angled fork branch
                    const ForkStart = MaxR * (0.24 + 0.38 * CrackHash(Arm * 29 + 5, 3));
                    if (WaveR > ForkStart + 0.05)
                    {
                        const FSteps = 5;
                        const FMax = Math.min(MaxR - ForkStart, WaveR - ForkStart);
                        let FPrev = ClampToBox(...EvalBranchPt(Arm, true, ForkStart));
                        for (let K = 1; K <= FSteps; K++)
                        {
                            const FD = ForkStart + (K / FSteps) * FMax;
                            const RawF = EvalBranchPt(Arm, true, FD);
                            const FCur = ClampToBox(RawF[0], RawF[1]);
                            const FF0 = (1 - (K - 1) / FSteps) * 0.62;
                            const FF1 = (1 - K / FSteps) * 0.62;
                            DrawCrackSegment(FPrev, FCur, BaseW * FF0, BaseW * FF1, FF0, false);
                            FPrev = FCur;
                            if (Math.abs(RawF[0] - O[0]) > Ex || Math.abs(RawF[1] - O[2]) > Ez) break;
                        }
                    }
                    // Tertiary fine spiderweb stress hairline linking adjacent arms near epicenter
                    if (WaveR > 0.42)
                    {
                        const RingR = Math.min(WaveR * 0.52, MaxR * (0.28 + 0.24 * CrackHash(Arm * 13 + 9, 5)));
                        const A0Pt = ClampToBox(...EvalBranchPt(Arm, false, RingR));
                        const A1Pt = ClampToBox(...EvalBranchPt((Arm + 1) % 8, false, RingR * (0.86 + 0.28 * CrackHash(Arm * 17, 6))));
                        DrawCrackSegment(A0Pt, A1Pt, BaseW * 0.28, BaseW * 0.24, 0.35, false);
                    }
                }
                // Epicenter impact shatter core flash
                PushArcFlash([HitX, TopY + 0.06, HitZ], CM === 1 ? 0.32 : 0.48, CM === 1 ? [0.78, 0.96, 1.0] : [1.0, 0.48, 0.12], Math.max(0.25, 0.95 - TSince * 0.22));
            }
        }
        if ((P.flareMode || 0) > 0 && P.showObject !== false)
        {
            // Military Aircraft Countermeasure Twin-Pod Dispenser Silhouette (C-130 / Jet Belly & Swept Wings)
            const O = P.origin;
            const FusCol = [0.22, 0.26, 0.31, 0.96];
            const PodCol = [0.36, 0.40, 0.46, 0.98];
            const TrimCol = [0.72, 0.78, 0.86, 0.88];
            // Fuselage centerline pod + swept wings
            DrawCylinderPillar(O[0] - 0.95, O[1] + 0.06, O[2], O[0] + 1.05, O[1] + 0.06, O[2], 0.14, FusCol);
            DrawCylinderPillar(O[0] - 0.15, O[1] + 0.05, O[2] - 1.45, O[0] - 0.15, O[1] + 0.05, O[2] + 1.45, 0.065, FusCol);
            // Left & right IR flare dispenser pods
            DrawCylinderPillar(O[0] - 0.32, O[1] - 0.05, O[2] - 0.52, O[0] + 0.12, O[1] - 0.05, O[2] - 0.52, 0.075, PodCol);
            DrawCylinderPillar(O[0] - 0.32, O[1] - 0.05, O[2] + 0.52, O[0] + 0.12, O[1] - 0.05, O[2] + 0.52, 0.075, PodCol);
            Line([O[0] + 1.05, O[1] + 0.06, O[2]], [O[0] - 0.35, O[1] + 0.05, O[2] - 1.45], TrimCol);
            Line([O[0] + 1.05, O[1] + 0.06, O[2]], [O[0] - 0.35, O[1] + 0.05, O[2] + 1.45], TrimCol);
            // Rhythmic ejector flash on alternating pods
            const SalvoP = Math.max(2.2, P.burstEvery || 2.2);
            const FlashGate = (Scene.Time % (SalvoP + 2.8)) < SalvoP;
            if (FlashGate)
            {
                const Side = (Math.floor(Scene.Time * 6.5) % 2 === 0) ? 1 : -1;
                PushArcFlash([O[0] - 0.12, O[1] - 0.06, O[2] + Side * 0.54], 0.24, [1.0, 0.88, 0.55], 0.72);
            }
        }
        if ((P.fireworkMode || 0) > 0 && P.showObject !== false)
        {
            // Professional 5-Tube Pyrotechnic Mortar Launch Rack on the ground
            const O = P.origin;
            const BaseY = Math.max(0.02, O[1]);
            const WoodCol = [0.34, 0.23, 0.14, 0.96];
            const TubeCol = [0.20, 0.24, 0.30, 0.98];
            const RimCol = [0.76, 0.80, 0.86, 0.85];
            DrawCylinderPillar(O[0] - 1.02, BaseY + 0.05, O[2] - 0.14, O[0] + 1.02, BaseY + 0.05, O[2] - 0.14, 0.045, WoodCol);
            DrawCylinderPillar(O[0] - 1.02, BaseY + 0.05, O[2] + 0.14, O[0] + 1.02, BaseY + 0.05, O[2] + 0.14, 0.045, WoodCol);
            DrawCylinderPillar(O[0] - 1.02, BaseY + 0.26, O[2] - 0.12, O[0] + 1.02, BaseY + 0.26, O[2] - 0.12, 0.030, WoodCol);
            DrawCylinderPillar(O[0] - 1.02, BaseY + 0.26, O[2] + 0.12, O[0] + 1.02, BaseY + 0.26, O[2] + 0.12, 0.030, WoodCol);
            const Cycle = Math.max(2.2, P.burstEvery || 3.1);
            const ShellId = Math.floor(Scene.Time / Cycle);
            const LocT = Scene.Time - ShellId * Cycle;
            const ActiveTube = ShellId % 5;
            for (let T = 0; T < 5; T++)
            {
                const Tx = O[0] + (T - 2) * 0.42;
                const Fan = (T - 2) * 0.05;
                DrawCylinderPillar(Tx, BaseY + 0.02, O[2], Tx + Fan, BaseY + 0.44, O[2], 0.068, TubeCol);
                RingXZ(Tx + Fan, BaseY + 0.44, O[2], 0.072, 14, RimCol);
                if (T === ActiveTube && LocT < 0.16)
                {
                    PushArcFlash([Tx + Fan, BaseY + 0.48, O[2]], 0.34 * (1 - LocT / 0.16), [1.0, 0.88, 0.46], 0.92);
                }
            }
        }
        if (P.shape === 19 && P.showObject !== false)
        {
            // High-pressure misting waterfall/aerosol spray nozzle manifold bar
            const O = P.origin;
            const R = Math.max(0.4, P.radius || 0.95);
            DrawCylinderPillar(O[0] + 0.06, O[1], O[2] - R, O[0] + 0.06, O[1], O[2] + R, 0.042, [0.46, 0.56, 0.64, 0.95]);
            DrawCylinderPillar(O[0] + 0.08, 0.0, O[2] - R * 0.82, O[0] + 0.08, O[1], O[2] - R * 0.82, 0.028, [0.30, 0.38, 0.44, 0.88]);
            DrawCylinderPillar(O[0] + 0.08, 0.0, O[2] + R * 0.82, O[0] + 0.08, O[1], O[2] + R * 0.82, 0.028, [0.30, 0.38, 0.44, 0.88]);
            for (let N = -2; N <= 2; N++)
            {
                const Nz = O[2] + (N / 2) * R * 0.78;
                RingXZ(O[0] - 0.02, O[1], Nz, 0.042, 10, [0.76, 0.94, 1.0, 0.82]);
            }
        }
        if ((P.beadsMode || 0) > 0 && P.showObject !== false)
        {
            const O = P.origin;
            const BM = Math.max(1, Math.min(3, P.beadsMode | 0));
            const DropH = Math.max(0.85, P.dynHeight || 3.0);
            const YTop = O[1] + DropH;
            if (BM === 2)
            {
                const CrownR = Math.max(0.32, P.radius || 0.56);
                RingXZ(O[0], YTop, O[2], CrownR, 28, [0.85, 0.68, 0.34, 0.92]);
                RingXZ(O[0], YTop + 0.16, O[2], CrownR * 0.22, 16, [0.92, 0.78, 0.44, 0.85]);
                Line([O[0], YTop + 0.32, O[2]], [O[0], YTop, O[2]], [0.88, 0.74, 0.44, 0.9]);
            }
            else
            {
                const Ex = Math.max(0.45, P.boxHalf?.[0] || 2.0);
                DrawCylinderPillar(O[0] - Ex * 1.08, YTop, O[2], O[0] + Ex * 1.08, YTop, O[2], 0.048, [0.36, 0.24, 0.16, 0.95]);
                DrawCylinderPillar(O[0] - Ex * 1.06, 0.0, O[2], O[0] - Ex * 1.06, YTop, O[2], 0.036, [0.28, 0.19, 0.13, 0.82]);
                DrawCylinderPillar(O[0] + Ex * 1.06, 0.0, O[2], O[0] + Ex * 1.06, YTop, O[2], 0.036, [0.28, 0.19, 0.13, 0.82]);
            }
        }
        if (Dyn === 3)
        {
            const Ex = Math.max(0.5, P.boxHalf?.[0] || 1.8);
            const Ey = Math.max(0.5, P.boxHalf?.[1] || 1.2);
            const DynH = Math.max(0.5, P.dynHeight ?? 3.6);
            const YTop = P.origin[1] + DynH;
            if ((P.dynSub || 0) > 0.5)
            {
                // Square-rigged Ship Mast + Yardarm + Boom matching WGSL evalDynStrand
                const SH = Math.max(0.8, Ey * 1.85);
                const YBot = Math.max(0.45, YTop - SH);
                const Mx = P.origin[0], Mz = P.origin[2] - 0.14;
                const TopY = YTop + 0.25;
                DrawCylinderPillar(Mx, 0.0, Mz, Mx, TopY, Mz, 0.068, [0.46, 0.32, 0.20, 0.96]);
                RingXZ(Mx, 0.02, Mz, 0.22, 20, [0.56, 0.42, 0.28, 0.65]);
                Line([P.origin[0] - Ex * 0.96, YTop, P.origin[2]], [P.origin[0] + Ex * 0.96, YTop, P.origin[2]], [0.56, 0.40, 0.26, 0.95]);
                Line([P.origin[0] - Ex * 1.12, YBot, P.origin[2]], [P.origin[0] + Ex * 1.12, YBot, P.origin[2]], [0.56, 0.40, 0.26, 0.88]);
                Line([Mx, TopY - 0.05, Mz], [P.origin[0] - Ex * 0.90, YTop, P.origin[2]], [0.70, 0.64, 0.52, 0.72]);
                Line([Mx, TopY - 0.05, Mz], [P.origin[0] + Ex * 0.90, YTop, P.origin[2]], [0.70, 0.64, 0.52, 0.72]);
            }
            else
            {
                // Flagpole at x = origin.x - ext.x matching WGSL evalDynStrand
                const Mx = P.origin[0] - Ex, Mz = P.origin[2];
                const TopY = YTop + 0.16;
                DrawCylinderPillar(Mx, 0.0, Mz, Mx, TopY, Mz, 0.048, [0.58, 0.62, 0.68, 0.96]);
                DrawElectrodeSphere([Mx, TopY + 0.05, Mz], 0.075, [0.92, 0.74, 0.28, 0.98], [0.98, 0.86, 0.45, 0.9], false);
                RingXZ(Mx, 0.02, Mz, 0.16, 20, [0.65, 0.70, 0.76, 0.65]);
            }
        }
        else if (Dyn === 1)
        {
            const Ex = Math.max(0.5, P.boxHalf?.[0] || 2.4);
            const Ez = Math.max(0.5, P.boxHalf?.[2] || 1.4);
            const DynH = Math.max(0.8, P.dynHeight ?? 3.1);
            const Py = P.origin[1] + DynH * 1.02;
            for (const Sx of [-1, 1])
            {
                for (const Sz of [-1, 1])
                {
                    const Px = P.origin[0] + Sx * Ex * 1.04;
                    const Pz = P.origin[2] + Sz * Ez;
                    DrawCylinderPillar(Px, 0.0, Pz, Px, Py, Pz, 0.05, [0.34, 0.37, 0.42, 0.94]);
                }
                Line([P.origin[0] + Sx * Ex * 1.04, Py * 0.96, P.origin[2] - Ez], [P.origin[0] + Sx * Ex * 1.04, Py * 0.96, P.origin[2] + Ez], [0.46, 0.52, 0.58, 0.86]);
            }
        }
        if ((P.gridMode || 0) > 0 && P.showObject !== false)
        {
            const Surf = Math.max(0, Math.min(16, Math.round(P.gridSurface ?? 0)));
            const O = P.origin;
            const Ex = Math.max(0.35, P.boxHalf?.[0] || 1.7), Ey = Math.max(0.35, P.boxHalf?.[1] || 1.7), Ez = Math.max(0.35, P.boxHalf?.[2] || 1.7);
            const Sc = Math.max(0.35, P.radius || Ex);
            if (Surf >= 6)
            {
                const Style = Math.round(P.objStyle ?? 0);
                if (Style !== 3)
                {
                    const MeshObj = ResolveObjectMesh(P);
                    if (MeshObj?.tris?.length)
                    {
                        const Rot = P.objRot || [0, 0, 0];
                        const InnerSc = Sc * 0.972;
                        const Xform = V => {
                            const R = RotateEulerXYZ(V, Rot);
                            return [O[0] + R[0] * InnerSc, O[1] + R[1] * InnerSc, O[2] + R[2] * InnerSc];
                        };
                        const MaxFaces = Style === 2 ? 420 : 1800;
                        const Step = Math.max(1, Math.ceil(MeshObj.tris.length / MaxFaces));
                        const BaseCol = Style === 1 ? [0.68, 0.72, 0.78, 0.96] : [0.04, 0.046, 0.06, 0.98];
                        const WireCol = Selected === E.Token ? [0.46, 0.86, 0.62, 0.42] : [0.45, 0.56, 0.68, 0.28];
                        for (let Ti = 0; Ti < MeshObj.tris.length; Ti += Step)
                        {
                            const T = MeshObj.tris[Ti];
                            const A = Xform(T.a), B = Xform(T.b), C3 = Xform(T.c);
                            if (Style === 2)
                            {
                                Line(A, B, WireCol); Line(B, C3, WireCol); Line(C3, A, WireCol);
                            }
                            else
                            {
                                Face(A, B, C3, BaseCol);
                            }
                        }
                    }
                }
            }
            else if (Surf === 2)
            {
                DrawElectrodeSphere(O, Sc * 0.985, [0.04, 0.045, 0.055, 0.99], [0.22, 0.24, 0.28, 0.35], false);
            }
            else if (Surf === 3)
            {
                // Sculpted obsidian forearm + palm + 5 curved fingers matching shaders.js evalHandTube (0.975x inner skin) + glossy chrome sphere
                const HandSegs = [
                    { p0: [0, 1.15, -0.14], p1: [0, 0.72, -0.06], p2: [0, 0.28, -0.01], rx0: 0.273, rx1: 0.234, rz0: 0.205, rz1: 0.136, palm: false, cap: false },
                    { p0: [0, 0.30, -0.01], p1: [-0.01, 0, 0.03], p2: [-0.01, -0.34, 0.05], rx0: 0.263, rx1: 0.332, rz0: 0.136, rz1: 0.112, palm: true, cap: false },
                    { p0: [-0.22, 0.04, 0.04], p1: [-0.44, -0.36, 0.14], p2: [-0.33, -0.82, 0.25], rx0: 0.112, rx1: 0.063, rz0: 0.102, rz1: 0.058, palm: false, cap: true },
                    { p0: [-0.21, -0.30, 0.04], p1: [-0.26, -0.66, 0.17], p2: [-0.18, -0.94, 0.26], rx0: 0.078, rx1: 0.052, rz0: 0.073, rz1: 0.049, palm: false, cap: true },
                    { p0: [-0.05, -0.33, 0.04], p1: [-0.06, -0.72, 0.16], p2: [-0.02, -0.95, 0.23], rx0: 0.080, rx1: 0.054, rz0: 0.075, rz1: 0.051, palm: false, cap: true },
                    { p0: [0.11, -0.32, 0.03], p1: [0.12, -0.68, 0.13], p2: [0.12, -0.87, 0.17], rx0: 0.074, rx1: 0.050, rz0: 0.069, rz1: 0.047, palm: false, cap: true },
                    { p0: [0.25, -0.28, 0.01], p1: [0.28, -0.56, 0.08], p2: [0.25, -0.74, 0.12], rx0: 0.064, rx1: 0.044, rz0: 0.060, rz1: 0.041, palm: false, cap: true }
                ];
                const SkinCol = [0.042, 0.045, 0.054, 0.99];
                for (const Hs of HandSegs)
                {
                    const Rings = 8, Sides = 14;
                    const RingPts = [];
                    for (let R = 0; R <= Rings; R++)
                    {
                        const T = R / Rings, IT = 1 - T;
                        const C = [0, 1, 2].map(K => Hs.p0[K] * IT * IT + Hs.p1[K] * 2 * IT * T + Hs.p2[K] * T * T);
                        const Dc = [0, 1, 2].map(K => (Hs.p1[K] - Hs.p0[K]) * 2 * IT + (Hs.p2[K] - Hs.p1[K]) * 2 * T);
                        const Tang = Normalize3([Dc[0], Dc[1] - 0.001, Dc[2]]);
                        const UAx = Normalize3(Cross3(Tang, [0, 0, 1]));
                        const WAx = Normalize3(Cross3(UAx, Tang));
                        let Rx = Hs.palm ? (0.254 + (0.342 - 0.254) * Math.sin(Math.min(Math.PI, T * 2.2))) : (Hs.rx0 + (Hs.rx1 - Hs.rx0) * T);
                        let Rz = Hs.rz0 + (Hs.rz1 - Hs.rz0) * T;
                        let CapLift = 0;
                        if (Hs.cap && T > 0.88)
                        {
                            const AngC = ((T - 0.88) / 0.12) * 1.38;
                            Rx *= Math.cos(AngC); Rz *= Math.cos(AngC); CapLift = Math.sin(AngC) * Hs.rx1;
                        }
                        const Row = [];
                        for (let S = 0; S < Sides; S++)
                        {
                            const Phi = (S / Sides) * Math.PI * 2;
                            const Cp = Math.cos(Phi), Sp = Math.sin(Phi);
                            Row.push([
                                O[0] + (C[0] + Tang[0] * CapLift + UAx[0] * (Rx * Cp) + WAx[0] * (Rz * Sp)) * Sc,
                                O[1] + (C[1] + Tang[1] * CapLift + UAx[1] * (Rx * Cp) + WAx[1] * (Rz * Sp)) * Sc,
                                O[2] + (C[2] + Tang[2] * CapLift + UAx[2] * (Rx * Cp) + WAx[2] * (Rz * Sp)) * Sc
                            ]);
                        }
                        RingPts.push(Row);
                    }
                    for (let R = 0; R < Rings; R++)
                    {
                        for (let S = 0; S < Sides; S++)
                        {
                            const Sn = (S + 1) % Sides;
                            Face(RingPts[R][S], RingPts[R][Sn], RingPts[R + 1][Sn], SkinCol);
                            Face(RingPts[R][S], RingPts[R + 1][Sn], RingPts[R + 1][S], SkinCol);
                        }
                    }
                }
                // Glossy chrome sphere cradled by the downward-cupped obsidian fingers
                DrawElectrodeSphere([O[0] - 0.04 * Sc, O[1] - 0.64 * Sc, O[2] - 0.02 * Sc], 0.31 * Sc, [0.36, 0.41, 0.48, 0.99], [0.88, 0.94, 1.0, 0.72], true);
            }
            else if (Surf === 4)
            {
                const Rmaj = Sc, rmin = Rmaj * 0.365, SegU = 28, SegV = 14;
                const TCol = [0.045, 0.05, 0.06, 0.98];
                for (let Iu = 0; Iu < SegU; Iu++)
                {
                    const P0 = (Iu / SegU) * Math.PI * 2, P1 = ((Iu + 1) / SegU) * Math.PI * 2;
                    for (let Iv = 0; Iv < SegV; Iv++)
                    {
                        const T0 = (Iv / SegV) * Math.PI * 2, T1 = ((Iv + 1) / SegV) * Math.PI * 2;
                        const Pt = (Ph, Th) => {
                            const Rad = Rmaj + rmin * Math.cos(Th);
                            return [O[0] + Math.cos(Ph) * Rad, O[1] + Math.sin(Th) * rmin, O[2] + Math.sin(Ph) * Rad];
                        };
                        const A = Pt(P0, T0), B = Pt(P1, T0), C = Pt(P1, T1), D = Pt(P0, T1);
                        Face(A, B, C, TCol); Face(A, C, D, TCol);
                    }
                }
            }
            else if (Surf === 0 || Surf === 1)
            {
                const Slate = [0.038, 0.042, 0.050, 0.88];
                if (Surf === 0)
                {
                    const Y0 = O[1] - 0.012;
                    const A = [O[0] - Ex, Y0, O[2] - Ez], B = [O[0] + Ex, Y0, O[2] - Ez], C = [O[0] + Ex, Y0, O[2] + Ez], D = [O[0] - Ex, Y0, O[2] + Ez];
                    Face(A, B, C, Slate); Face(A, C, D, Slate);
                }
                else
                {
                    const Z0 = O[2] - 0.015;
                    const A = [O[0] - Ex, O[1] - Ey, Z0], B = [O[0] + Ex, O[1] - Ey, Z0], C = [O[0] + Ex, O[1] + Ey, Z0], D = [O[0] - Ex, O[1] + Ey, Z0];
                    Face(A, B, C, Slate); Face(A, C, D, Slate);
                }
            }
        }
    }
    if (Scene.Ball.Active)
    {
        const B = Scene.Ball;
        DrawElectrodeSphere(B.Pos, B.Radius, [0.88, 0.38, 0.12, 0.98], [1.0, 0.84, 0.42, 0.98], true);
        if (B.Pos[1] > 0.02 && B.Pos[1] < 5.5)
        {
            const ShR = Math.max(0.16, B.Radius * (0.85 + B.Pos[1] * 0.08));
            RingXZ(B.Pos[0], 0.02, B.Pos[2], ShR, 24, [0.95, 0.62, 0.26, Math.max(0.18, 0.65 - B.Pos[1] * 0.1)]);
        }
    }

    return { Lines: new Float32Array(Out), Meshes: new Float32Array(Mesh), ArcSegs, ArcFlashes };
}

function InvertMatrix4(M)
{
    const Inv = new Float32Array(16);
    Inv[0] = M[5] * M[10] * M[15] - M[5] * M[11] * M[14] - M[9] * M[6] * M[15] + M[9] * M[7] * M[14] + M[13] * M[6] * M[11] - M[13] * M[7] * M[10];
    Inv[4] = -M[4] * M[10] * M[15] + M[4] * M[11] * M[14] + M[8] * M[6] * M[15] - M[8] * M[7] * M[14] - M[12] * M[6] * M[11] + M[12] * M[7] * M[10];
    Inv[8] = M[4] * M[9] * M[15] - M[4] * M[11] * M[13] - M[8] * M[5] * M[15] + M[8] * M[7] * M[13] + M[12] * M[5] * M[11] - M[12] * M[7] * M[9];
    Inv[12] = -M[4] * M[9] * M[14] + M[4] * M[10] * M[13] + M[8] * M[5] * M[14] - M[8] * M[6] * M[13] - M[12] * M[5] * M[10] + M[12] * M[6] * M[9];
    Inv[1] = -M[1] * M[10] * M[15] + M[1] * M[11] * M[14] + M[9] * M[2] * M[15] - M[9] * M[3] * M[14] - M[13] * M[2] * M[11] + M[13] * M[3] * M[10];
    Inv[5] = M[0] * M[10] * M[15] - M[0] * M[11] * M[14] - M[8] * M[2] * M[15] + M[8] * M[3] * M[14] + M[12] * M[2] * M[11] - M[12] * M[3] * M[10];
    Inv[9] = -M[0] * M[9] * M[15] + M[0] * M[11] * M[13] + M[8] * M[1] * M[15] - M[8] * M[3] * M[13] - M[12] * M[1] * M[11] + M[12] * M[3] * M[9];
    Inv[13] = M[0] * M[9] * M[14] - M[0] * M[10] * M[13] - M[8] * M[1] * M[14] + M[8] * M[2] * M[13] + M[12] * M[1] * M[10] - M[12] * M[2] * M[9];
    Inv[2] = M[1] * M[6] * M[15] - M[1] * M[7] * M[14] - M[5] * M[2] * M[15] + M[5] * M[3] * M[14] + M[13] * M[2] * M[7] - M[13] * M[3] * M[6];
    Inv[6] = -M[0] * M[6] * M[15] + M[0] * M[7] * M[14] + M[4] * M[2] * M[15] - M[4] * M[3] * M[14] - M[12] * M[2] * M[7] + M[12] * M[3] * M[6];
    Inv[10] = M[0] * M[5] * M[15] - M[0] * M[7] * M[13] - M[4] * M[1] * M[15] + M[4] * M[3] * M[13] + M[12] * M[1] * M[7] - M[12] * M[3] * M[5];
    Inv[14] = -M[0] * M[5] * M[14] + M[0] * M[6] * M[13] + M[4] * M[1] * M[14] - M[4] * M[2] * M[13] - M[12] * M[1] * M[6] + M[12] * M[2] * M[5];
    Inv[3] = -M[1] * M[6] * M[11] + M[1] * M[7] * M[10] + M[5] * M[2] * M[11] - M[5] * M[3] * M[10] - M[9] * M[2] * M[7] + M[9] * M[3] * M[6];
    Inv[7] = M[0] * M[6] * M[11] - M[0] * M[7] * M[10] - M[4] * M[2] * M[11] + M[4] * M[3] * M[10] + M[8] * M[2] * M[7] - M[8] * M[3] * M[6];
    Inv[11] = -M[0] * M[5] * M[11] + M[0] * M[7] * M[9] + M[4] * M[1] * M[11] - M[4] * M[3] * M[9] - M[8] * M[1] * M[7] + M[8] * M[3] * M[5];
    Inv[15] = M[0] * M[5] * M[10] - M[0] * M[6] * M[9] - M[4] * M[1] * M[10] + M[4] * M[2] * M[9] + M[8] * M[1] * M[6] - M[8] * M[2] * M[5];
    const Det = M[0] * Inv[0] + M[1] * Inv[4] + M[2] * Inv[8] + M[3] * Inv[12];
    if (Math.abs(Det) < 1e-8) return null;
    for (let I = 0; I < 16; I++) Inv[I] /= Det;
    return Inv;
}

function ProjectWorldToCss(VP, X, Y, Z, W, H)
{
    const Cx = VP[0] * X + VP[4] * Y + VP[8] * Z + VP[12];
    const Cy = VP[1] * X + VP[5] * Y + VP[9] * Z + VP[13];
    const Cw = VP[3] * X + VP[7] * Y + VP[11] * Z + VP[15];
    if (Cw <= 0.05) return null;
    return { X: ((Cx / Cw) * 0.5 + 0.5) * W, Y: (0.5 - (Cy / Cw) * 0.5) * H };
}

function ScreenToRay(Cam, ClientX, ClientY, Canvas)
{
    if (!Cam || !Canvas) return null;
    const Inv = InvertMatrix4(Cam.ViewProjection);
    if (!Inv) return null;
    const Rect = Canvas.getBoundingClientRect();
    if (Rect.width < 4 || Rect.height < 4) return null;
    const NdcX = ((ClientX - Rect.left) / Rect.width) * 2 - 1;
    const NdcY = 1 - ((ClientY - Rect.top) / Rect.height) * 2;
    const Unproj = NdcZ =>
    {
        const Ox = Inv[0] * NdcX + Inv[4] * NdcY + Inv[8] * NdcZ + Inv[12];
        const Oy = Inv[1] * NdcX + Inv[5] * NdcY + Inv[9] * NdcZ + Inv[13];
        const Oz = Inv[2] * NdcX + Inv[6] * NdcY + Inv[10] * NdcZ + Inv[14];
        const Ow = Inv[3] * NdcX + Inv[7] * NdcY + Inv[11] * NdcZ + Inv[15] || 1;
        return [Ox / Ow, Oy / Ow, Oz / Ow];
    };
    const Near = Unproj(-1), Far = Unproj(1);
    const Dir = Normalize3([Far[0] - Near[0], Far[1] - Near[1], Far[2] - Near[2]]);
    return { Origin: Near, Dir };
}

function ScreenToWorldPlane(Cam, ClientX, ClientY, Canvas, PlaneY)
{
    const Ray = ScreenToRay(Cam, ClientX, ClientY, Canvas);
    if (!Ray || Math.abs(Ray.Dir[1]) < 1e-4) return null;
    const T = (PlaneY - Ray.Origin[1]) / Ray.Dir[1];
    if (T <= 0 || T > 400) return null;
    return [Ray.Origin[0] + Ray.Dir[0] * T, PlaneY, Ray.Origin[2] + Ray.Dir[2] * T];
}

function ThrowBallAtTarget(TargetPos, Speed = null, Loft = 1.6)
{
    const B = Scene.Ball;
    const Cam = Scene.LastCamera || ResolveCamera(1.6);
    const Aim = TargetPos || [0, 1.8, 0];
    const Eye = Cam?.Eye || [4.5, 4.0, 9.5];
    const ToTgt = Subtract3(Aim, Eye);
    const Dist = Math.hypot(ToTgt[0], ToTgt[1], ToTgt[2]) || 1;
    const Dir = [ToTgt[0] / Dist, ToTgt[1] / Dist, ToTgt[2] / Dist];
    const Spd = Speed ?? B.ThrowSpeed ?? 10.5;
    B.Pos = [Eye[0] + Dir[0] * 1.4, Math.max(B.Radius + 0.25, Eye[1] + Dir[1] * 1.4), Eye[2] + Dir[2] * 1.4];
    const FlightT = Math.max(0.12, (Dist - 1.4) / Math.max(2.0, Spd));
    B.Vel = [
        Dir[0] * Spd,
        Dir[1] * Spd + 0.5 * B.Gravity * FlightT * 0.48 + Loft * 0.25,
        Dir[2] * Spd
    ];
    B.Active = true;
    AppendLog('PARTICLES', 'Thrown interactive 3D ball', `v=${Spd.toFixed(1)} m/s -> (${Aim.map(v => v.toFixed(1)).join(', ')})`, 'live');
}

function ThrowBallFromScreen(ClientX, ClientY, Canvas, SpeedScale = 1.0)
{
    const Cam = Scene.LastCamera || ResolveCamera(Canvas.width / Math.max(1, Canvas.height));
    const Ray = ScreenToRay(Cam, ClientX, ClientY, Canvas);
    if (!Ray) return;
    const B = Scene.Ball;
    const Spd = (B.ThrowSpeed ?? 10.5) * SpeedScale;
    const Eye = Cam.Eye || Ray.Origin;
    B.Pos = [Eye[0] + Ray.Dir[0] * 1.35, Math.max(B.Radius + 0.2, Eye[1] + Ray.Dir[1] * 1.35), Eye[2] + Ray.Dir[2] * 1.35];
    B.Vel = [Ray.Dir[0] * Spd, Ray.Dir[1] * Spd + 1.25, Ray.Dir[2] * Spd];
    B.Active = true;
    AppendLog('PARTICLES', 'Thrown interactive 3D ball from cursor', `v=${Spd.toFixed(1)} m/s`, 'live');
}

function StepBallPhysics(Dt)
{
    const B = Scene.Ball;
    if (!B.Active || Dt <= 0) return;
    const Keep = Math.exp(-0.14 * Dt);
    B.Vel[0] *= Keep;
    B.Vel[1] = B.Vel[1] * Keep - B.Gravity * Dt;
    B.Vel[2] *= Keep;
    B.Pos[0] += B.Vel[0] * Dt;
    B.Pos[1] += B.Vel[1] * Dt;
    B.Pos[2] += B.Vel[2] * Dt;
    if (B.Pos[1] < B.Radius)
    {
        B.Pos[1] = B.Radius;
        if (Math.abs(B.Vel[1]) > 0.35) B.Vel[1] = Math.abs(B.Vel[1]) * B.Bounce;
        else B.Vel[1] = 0;
        B.Vel[0] *= 0.92;
        B.Vel[2] *= 0.92;
    }
    const ArenaBound = 7.4;
    if (Math.abs(B.Pos[0]) > ArenaBound) { B.Pos[0] = Clamp(B.Pos[0], -ArenaBound, ArenaBound); B.Vel[0] = -B.Vel[0] * 0.72; }
    if (Math.abs(B.Pos[2]) > ArenaBound) { B.Pos[2] = Clamp(B.Pos[2], -ArenaBound, ArenaBound); B.Vel[2] = -B.Vel[2] * 0.72; }
    for (const E of Scene.Effects)
    {
        if (!E.P.visible) continue;
        if ((E.P.crackMode || 0) > 0)
        {
            const O = E.P.origin;
            const Ex = Math.max(1.2, E.P.boxHalf?.[0] || 3.4);
            const Ez = Math.max(1.2, E.P.boxHalf?.[2] || 3.4);
            if (B.Pos[1] <= B.Radius + 0.16 && Math.abs(B.Pos[0] - O[0]) <= Ex * 1.05 && Math.abs(B.Pos[2] - O[2]) <= Ez * 1.05 && B.Vel[1] <= 0.15)
            {
                const JustBounced = !E.CrackHit || (Scene.Time - E.CrackHit.t) > 0.24 || Math.hypot(B.Pos[0] - E.CrackHit.x, B.Pos[2] - E.CrackHit.z) > 0.45;
                if (JustBounced)
                {
                    E.CrackHit = { x: B.Pos[0], z: B.Pos[2], t: Scene.Time };
                    B.Vel[1] = Math.max(B.Vel[1], 3.2);
                    AppendLog('PARTICLES', `${E.Name}: ground fracture impact`, `(${B.Pos[0].toFixed(1)}, ${B.Pos[2].toFixed(1)}) m`, 'live');
                }
            }
        }
        if ((E.P.shape ?? 3) !== 16) continue;
        const P = E.P, O = P.origin;
        const SForm = Math.round(P.shieldForm ?? 0);
        const ShR = (P.sizeStart || 3.1) * (P.sizeScale || 1) * 0.5;
        const Dx = B.Pos[0] - O[0], Dy = B.Pos[1] - O[1], Dz = B.Pos[2] - O[2];
        if (SForm === 0)
        {
            const Dist = Math.hypot(Dx, Dy, Dz) || 1e-4;
            const MinD = ShR + B.Radius;
            if (Dist < MinD)
            {
                const Nx = Dx / Dist, Ny = Dy / Dist, Nz = Dz / Dist;
                B.Pos[0] = O[0] + Nx * MinD; B.Pos[1] = O[1] + Ny * MinD; B.Pos[2] = O[2] + Nz * MinD;
                const Vn = B.Vel[0] * Nx + B.Vel[1] * Ny + B.Vel[2] * Nz;
                if (Vn < 0)
                {
                    const Rest = 1 + Math.max(0.45, B.Bounce);
                    B.Vel[0] -= Rest * Vn * Nx; B.Vel[1] -= Rest * Vn * Ny; B.Vel[2] -= Rest * Vn * Nz;
                }
            }
        }
        else
        {
            const [Nx, Ny, Nz] = Normalize3(P.dir || [0, 0, 1]);
            const Dn = Dx * Nx + Dy * Ny + Dz * Nz;
            const Tx = Dx - Dn * Nx, Ty = Dy - Dn * Ny, Tz = Dz - Dn * Nz;
            if (Math.abs(Dn) < B.Radius + 0.10 && Math.hypot(Tx, Ty, Tz) <= ShR * 1.02)
            {
                const Sign = Dn >= 0 ? 1 : -1;
                const Push = (B.Radius + 0.10) - Math.abs(Dn);
                B.Pos[0] += Nx * Sign * Push; B.Pos[1] += Ny * Sign * Push; B.Pos[2] += Nz * Sign * Push;
                const Vn = B.Vel[0] * Nx + B.Vel[1] * Ny + B.Vel[2] * Nz;
                if (Vn * Sign < 0)
                {
                    const Rest = 1 + Math.max(0.45, B.Bounce);
                    B.Vel[0] -= Rest * Vn * Nx; B.Vel[1] -= Rest * Vn * Ny; B.Vel[2] -= Rest * Vn * Nz;
                }
            }
        }
    }
}

function SyncBallToolState()
{
    const BallTool = document.querySelector('#ViewportTools [data-vtool="ball"]');
    if (BallTool) BallTool.classList.toggle('active', !!Scene.BallPlacing);
    const TopBtn = document.getElementById('throw-ball-btn');
    if (TopBtn) TopBtn.classList.toggle('active', !!Scene.BallPlacing);
    const Surface = document.getElementById('ViewportSurface');
    if (Surface) Surface.classList.toggle('is-throwing-ball', !!Scene.BallPlacing);
    const OrbitTool = document.querySelector('#ViewportTools [data-vtool="orbit"]');
    if (OrbitTool) OrbitTool.classList.toggle('active', !Scene.AnemometerPlacing && !Scene.BallPlacing);
    const Banner = document.getElementById('ViewportPlaceBanner');
    const BannerText = document.getElementById('ViewportPlaceText');
    if (Scene.BallPlacing)
    {
        if (Banner) Banner.classList.add('shown');
        if (BannerText) BannerText.textContent = 'THROW BALL TOOL · Click anywhere in viewport to launch 3D sphere into Cloth, Webs, Ferns & Trees · Esc cancel';
    }
    else if (!Scene.AnemometerPlacing && !Scene.AnemometerDragging)
    {
        if (Banner) Banner.classList.remove('shown');
    }
}

function HitTestArcSphere(Cam, ClientX, ClientY, Canvas)
{
    if (!Cam || !Canvas) return null;
    const Rect = Canvas.getBoundingClientRect();
    const Mx = ClientX - Rect.left, My = ClientY - Rect.top;
    let Best = null, BestD = Infinity;
    for (const E of Scene.Effects)
    {
        const IsArc = E.P.arcMode === 0 || E.P.arcMode === 1;
        const IsBeam = (E.P.beamMode || 0) > 0;
        if (!E.P.visible || (!IsArc && !IsBeam)) continue;
        const P = E.P;
        const Pairs = [
            { Which: 'origin', Label: IsBeam ? 'Beam Muzzle' : 'Sphere A (Emitter)', Pos: P.origin, Rad: Math.max(0.12, P.radius ?? 0.45) },
            { Which: 'targetPos', Label: IsBeam ? 'Beam Impact Target' : 'Sphere B (Landing)', Pos: P.targetPos || [P.origin[0] + 4, P.origin[1], P.origin[2]], Rad: Math.max(0.12, P.targetRadius ?? 0.45) }
        ];
        for (const Item of Pairs)
        {
            const C2 = ProjectWorldToCss(Cam.ViewProjection, Item.Pos[0], Item.Pos[1], Item.Pos[2], Rect.width, Rect.height);
            if (!C2) continue;
            const E2 = ProjectWorldToCss(Cam.ViewProjection, Item.Pos[0] + Item.Rad, Item.Pos[1], Item.Pos[2], Rect.width, Rect.height);
            const HitR = Math.max(22, (E2 ? Math.hypot(E2.X - C2.X, E2.Y - C2.Y) : 22) + 8);
            const Dist = Math.hypot(Mx - C2.X, My - C2.Y);
            if (Dist <= HitR && Dist < BestD)
            {
                BestD = Dist;
                Best = { EffectToken: E.Token, Effect: E, Which: Item.Which, Label: Item.Label };
            }
        }
    }
    return Best;
}

function HitTestEffector(Cam, ClientX, ClientY, Canvas)
{
    if (!Cam || !Canvas) return null;
    const Rect = Canvas.getBoundingClientRect();
    const Mx = ClientX - Rect.left, My = ClientY - Rect.top;
    let Best = null, BestD = Infinity;
    for (const E of Scene.Effects)
    {
        const P = E.P;
        if (!P.visible || (P.gridMode || 0) <= 0) continue;
        if ((P.gridEffector || 0) !== 5 && !P.effBlend) continue;
        const Pairs = [
            { Which: 'effPos', Label: 'Effector A', Pos: P.effPos, Size: Number(P.effSize ?? 0) },
            { Which: 'effPos2', Label: 'Effector B', Pos: P.effPos2, Size: Number(P.effSize2 ?? 0) }
        ];
        for (const Item of Pairs)
        {
            if (!Item.Pos || Item.Size <= 0.01) continue;
            const C2 = ProjectWorldToCss(Cam.ViewProjection, Item.Pos[0], Item.Pos[1], Item.Pos[2], Rect.width, Rect.height);
            if (!C2) continue;
            const E2 = ProjectWorldToCss(Cam.ViewProjection, Item.Pos[0] + Item.Size, Item.Pos[1], Item.Pos[2], Rect.width, Rect.height);
            const HitR = Math.max(22, Math.min(72, (E2 ? Math.hypot(E2.X - C2.X, E2.Y - C2.Y) : 22) + 8));
            const Dist = Math.hypot(Mx - C2.X, My - C2.Y);
            if (Dist <= HitR && Dist < BestD)
            {
                BestD = Dist;
                Best = { EffectToken: E.Token, Effect: E, Which: Item.Which, Label: Item.Label };
            }
        }
    }
    return Best;
}

function HitTestControlPoint(Cam, ClientX, ClientY, Canvas)
{
    if (!Scene.ShowHandles || !Cam || !Canvas) return null;
    const Rect = Canvas.getBoundingClientRect();
    const Mx = ClientX - Rect.left, My = ClientY - Rect.top;
    let Best = null, BestD = Infinity;

    const Check = (Pos, HitR, Payload) => {
        if (!Pos) return;
        const C2 = ProjectWorldToCss(Cam.ViewProjection, Pos[0], Pos[1], Pos[2], Rect.width, Rect.height);
        if (!C2) return;
        const D = Math.hypot(Mx - C2.X, My - C2.Y);
        if (D <= HitR && D < BestD) { BestD = D; Best = Payload; }
    };

    // 1. Ferrofluid magnetic pole handle (any visible ferrofluid effect)
    for (const E of Scene.Effects)
    {
        const P = E.P;
        if (!P.visible || (P.ferroMode || 0) <= 0) continue;
        const FM = Math.max(1, Math.min(2, P.ferroMode | 0));
        const DishR = Math.max(0.8, P.radius || 2.1);
        const Mag = EvalFerroMagnetCPU(Scene.Time, P.origin, DishR, P.ferroMagnetSpeed ?? 1.35, FM, P);
        Check(Mag, 24, { Kind: 'ferroMagnet', Id: E.Token + ':mag', Effect: E, Label: `${E.Name} · Magnet Pole` });
    }

    // 2. Fracture epicenter handle (visible crack effect with active hit or selected)
    for (const E of Scene.Effects)
    {
        const P = E.P;
        if (!P.visible || (P.crackMode || 0) <= 0) continue;
        const Hx = E.CrackHit ? E.CrackHit.x : P.origin[0];
        const Hz = E.CrackHit ? E.CrackHit.z : P.origin[2];
        Check([Hx, 0.14, Hz], 22, { Kind: 'crackHit', Id: E.Token + ':crack', Effect: E, Label: `${E.Name} · Fracture Epicenter` });
    }

    // 3. Selected Particle Effect Origin / Placed 3D Object Anchor
    const SelEff = EffectByToken(Selected);
    if (SelEff && SelEff.P.visible)
    {
        const P = SelEff.P;
        const IsArc = P.arcMode === 0 || P.arcMode === 1;
        const IsBeam = (P.beamMode || 0) > 0;
        if (!IsArc && !IsBeam)
        {
            const IsObj = (P.gridMode || 0) > 0 && (P.gridSurface ?? 0) >= 2;
            Check(P.origin, 22, { Kind: 'origin', Id: SelEff.Token + ':origin', Effect: SelEff, Label: IsObj ? `${SelEff.Name} · 3D Object` : `${SelEff.Name} · Origin` });
        }
    }

    // 4. All Enabled Force Field Sources (draggable 3D field centres!)
    const Solo = Scene.FieldScene.Visualization.Solo;
    for (const Src of Scene.FieldScene.Sources)
    {
        if (!Src.Enabled || (Solo && Src.Token !== Solo)) continue;
        Check(Src.Centre, 20, { Kind: 'source', Id: 'src:' + Src.Token, Source: Src, Label: `${Src.Name} (${Src.Type})` });
    }

    return Best;
}

function HitTestAnemometer(Cam, ClientX, ClientY, Canvas)
{
    if (!Scene.ProbeEnabled || !Cam || !Canvas) return false;
    const Rect = Canvas.getBoundingClientRect();
    const Mx = ClientX - Rect.left, My = ClientY - Rect.top;
    const Head = ProjectWorldToCss(Cam.ViewProjection, Scene.Probe[0], Scene.Probe[1], Scene.Probe[2], Rect.width, Rect.height);
    const Base = ProjectWorldToCss(Cam.ViewProjection, Scene.Probe[0], 0.02, Scene.Probe[2], Rect.width, Rect.height);
    if (Head && Math.hypot(Mx - Head.X, My - Head.Y) <= 24) return true;
    if (Base && Math.hypot(Mx - Base.X, My - Base.Y) <= 18) return true;
    return false;
}

function SyncAnemometerToolState()
{
    const Surface = document.getElementById('ViewportSurface');
    if (Surface)
    {
        Surface.classList.toggle('is-placing-probe', !!Scene.AnemometerPlacing);
        Surface.classList.toggle('is-drag-probe', !!Scene.AnemometerDragging);
        Surface.classList.toggle('is-hover-probe', !Scene.AnemometerPlacing && !Scene.AnemometerDragging && !!Scene.AnemometerHover);
    }
    const Banner = document.getElementById('ViewportPlaceBanner');
    if (Banner) Banner.classList.toggle('shown', !!Scene.AnemometerPlacing || !!Scene.AnemometerDragging || !!Scene.BallPlacing);
    const BannerText = document.getElementById('ViewportPlaceText');
    if (BannerText)
    {
        if (Scene.AnemometerDragging)
            BannerText.textContent = `Moving Anemometer (${Scene.Probe.map(V => V.toFixed(1)).join(', ')}) m · Hold Shift to adjust altitude Y`;
        else if (Scene.AnemometerPlacing)
            BannerText.textContent = 'Click anywhere in the viewport to place the Anemometer · Drag pin anytime · Esc cancel';
        else if (Scene.BallPlacing)
            BannerText.textContent = 'THROW BALL TOOL · Click anywhere in viewport to launch 3D sphere into Cloth, Webs, Ferns & Trees · Esc cancel';
    }
    const ProbeTool = document.querySelector('#ViewportTools [data-vtool="probe"]');
    if (ProbeTool) ProbeTool.classList.toggle('active', !!Scene.AnemometerPlacing || !!Scene.ProbeEnabled);
    const OrbitTool = document.querySelector('#ViewportTools [data-vtool="orbit"]');
    if (OrbitTool) OrbitTool.classList.toggle('active', !Scene.AnemometerPlacing && !Scene.BallPlacing);
    const PlaceBtn = document.getElementById('btn-place-anemometer');
    if (PlaceBtn)
    {
        PlaceBtn.classList.toggle('active', !!Scene.AnemometerPlacing);
        PlaceBtn.textContent = Scene.AnemometerPlacing ? 'Click Viewport…' : 'Place in Viewport';
    }
    const ToggleBtn = document.getElementById('btn-toggle-anemometer');
    if (ToggleBtn)
    {
        ToggleBtn.classList.toggle('active', !!Scene.ProbeEnabled);
        ToggleBtn.textContent = Scene.ProbeEnabled ? 'In Viewport' : 'Hidden';
    }
}

function DrawAnemometerTrace(Canvas, History)
{
    if (!Canvas || !History || History.length < 2) return;
    const W = Canvas.clientWidth || 310;
    const H = Canvas.clientHeight || 78;
    const DPR = Math.min(window.devicePixelRatio || 1, 2);
    if (Canvas.width !== Math.round(W * DPR) || Canvas.height !== Math.round(H * DPR))
    {
        Canvas.width = Math.round(W * DPR);
        Canvas.height = Math.round(H * DPR);
    }
    const Ctx = Canvas.getContext('2d');
    Ctx.save();
    Ctx.setTransform(DPR, 0, 0, DPR, 0, 0);
    Ctx.clearRect(0, 0, W, H);

    const MaxV = Math.max(20, Math.ceil(Math.max(...History, 18) / 10) * 10);
    const PadTop = 6, PadBot = 6;
    const PlotH = H - PadTop - PadBot;

    Ctx.strokeStyle = 'rgba(255,255,255,0.06)';
    Ctx.lineWidth = 1;
    Ctx.fillStyle = 'rgba(255,255,255,0.32)';
    Ctx.font = '10px "DM Sans", sans-serif';
    for (let Step = 10; Step < MaxV; Step += 10)
    {
        const Y = PadTop + PlotH * (1 - Step / MaxV);
        Ctx.beginPath();
        Ctx.moveTo(0, Math.round(Y) + 0.5);
        Ctx.lineTo(W, Math.round(Y) + 0.5);
        Ctx.stroke();
        Ctx.fillText(String(Step), 4, Y - 2);
    }

    const MapX = I => (I / Math.max(1, History.length - 1)) * (W - 6) + 2;
    const MapY = V => PadTop + PlotH * (1 - Clamp(V / MaxV, 0, 1));

    const Grad = Ctx.createLinearGradient(0, PadTop, 0, H);
    Grad.addColorStop(0, 'rgba(88, 153, 121, 0.30)');
    Grad.addColorStop(1, 'rgba(88, 153, 121, 0.01)');
    Ctx.beginPath();
    Ctx.moveTo(MapX(0), H);
    for (let I = 0; I < History.length; I++) Ctx.lineTo(MapX(I), MapY(History[I]));
    Ctx.lineTo(MapX(History.length - 1), H);
    Ctx.closePath();
    Ctx.fillStyle = Grad;
    Ctx.fill();

    Ctx.beginPath();
    for (let I = 0; I < History.length; I++)
    {
        const X = MapX(I), Y = MapY(History[I]);
        if (I === 0) Ctx.moveTo(X, Y);
        else Ctx.lineTo(X, Y);
    }
    Ctx.strokeStyle = '#92d8c2';
    Ctx.lineWidth = 1.65;
    Ctx.lineJoin = 'round';
    Ctx.stroke();

    const LastX = MapX(History.length - 1);
    const LastY = MapY(History[History.length - 1]);
    Ctx.beginPath();
    Ctx.arc(LastX, LastY, 3.2, 0, Math.PI * 2);
    Ctx.fillStyle = '#ffffff';
    Ctx.fill();
    Ctx.restore();
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

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRAME LOOP
//------------------------------------------------------------------------------------------------------------------------

let LastTickErrorAt = 0;

function Tick(Now)
{
    if (Held) return;
    requestAnimationFrame(Tick);
    if (document.hidden) { LastTick = 0; return; }
    try
    {
        Advance(Now);
    }
    catch (Err)
    {
        if (Now - LastTickErrorAt > 2000)
        {
            LastTickErrorAt = Now;
            console.error('ParticleWorkspace frame error:', Err);
            AppendLog('PARTICLES', 'Frame render error', Err?.message || String(Err), 'warm');
        }
    }
}

function PackLens(Camera, Aspect)
{
    const Out = new Float32Array(32);
    let Holes = 0, Shimmers = 0;
    const VP = Camera.ViewProjection;
    const Scale = 0.5 / Math.tan((Camera.VFov || (Math.PI / 4)) / 2);
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
        Out.set([Scene.Time * (Sh.rate ?? 1.5), Sh.freq ?? 14, 0, 0], 16 + Shimmers * 8);
        Shimmers++;
    }
    Out[8] = Holes; Out[9] = Aspect; Out[10] = 0.35; Out[11] = Shimmers;
    return { Count: Holes, ShimmerCount: Shimmers, Packed: Out };
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
    if (Scene.Playing && Delta > 0) StepBallPhysics(Delta);


    const Engine = Scene.Engine;
    const Canvas = Engine.canvas;
    const Aspect = Canvas.width / Math.max(1, Canvas.height);
    const Camera = ResolveCamera(Aspect);
    const Solo = Scene.FieldScene.Visualization.Solo;

    Scene.PreparedField = FC().Prepare(Scene.FieldScene, Scene.Time);
    Scene.PackedFields = FC().PackForceEntries(Scene.FieldScene, Scene.PreparedField, Scene.Effects, null, Solo);

    const WindTexData = Scene.IsFallback
        ? null
        : FC().SampleWindTexData(Scene.FieldScene, Scene.PreparedField, PE.WIND, Scene.Wind.windScale, Solo);

    const Global = new Float32Array(PE.GLOB_OFF.sizeFloats || 60);
    const Off = PE.GLOB_OFF;
    Global.set(Camera.ViewProjection, Off.viewProj);
    Global.set([Camera.Right[0], Camera.Right[1], Camera.Right[2], 0], Off.camRight);
    Global.set([Camera.Up[0], Camera.Up[1], Camera.Up[2], 0], Off.camUp);
    Global.set([Camera.Eye[0], Camera.Eye[1], Camera.Eye[2], 1], Off.camPos);
    Global.set([Domain.Minimum[0], Domain.Minimum[1], Domain.Minimum[2], 0], Off.windMin);
    Global.set([Domain.Size[0], Domain.Size[1], Domain.Size[2], 0], Off.windSize);
    Global.set([Domain.Dimensions[0], Domain.Dimensions[1], Domain.Dimensions[2], Scene.FieldScene.Sources.length], Off.windDim);
    Global.set([Scene.Time, Delta, Scene.Step, Scene.Wind.windScale], Off.timing);
    Global.set([Scene.Wind.arrowRef, Scene.Wind.arrowStyle ?? 0, 0, Scene.Wind.turbulence], Off.viz);
    Global.set([Scene.Wind.swirl, 0.45, 0.22, Scene.Wind.arrowStride ?? 2], Off.swirl);
    const B = Scene.Ball;
    Global.set(B.Active ? [B.Pos[0], B.Pos[1], B.Pos[2], B.Radius] : [0, 0, 0, -1], Off.ballPos);
    Global.set(B.Active ? [B.Vel[0], B.Vel[1], B.Vel[2], B.Impulse] : [0, 0, 0, 0], Off.ballVel);

    const Components = new Float32Array(PE.WIND.maxComps * 8);

    const Jobs = [];
    for (const E of Scene.Effects)
    {
        if (!E.P.visible || !E.Gpu) continue;
        const P = E.P, Gpu = E.Gpu;

        const SeedNow = E.PendingBurst > 0 && ((P.dynMode || 0) > 0 || (P.webMode || 0) > 0 || (P.gridMode || 0) > 0 || (P.ferroMode || 0) > 0 || (P.crackMode || 0) > 0 || (P.beadsMode || 0) > 0 || (P.seedHeadMode || 0) > 0 || (P.jellyMode || 0) > 0 || (P.flareMode || 0) > 0 || (P.fireworkMode || 0) > 0);
        const Simulate = (Scene.Playing && Delta > 0) || SeedNow;
        let Release = 0, Steps = 0, StepDelta = Delta > 0 ? Delta : (SeedNow ? 1 / 60 : 0), Head = E.Head, Origin = P.origin;
        if (Simulate)
        {
            if (IsArcMode(P.arcMode) && P.arcLoop)
            {
                if (E.ArcNextLoopAt === undefined || Scene.Time >= E.ArcNextLoopAt)
                    TriggerArcStrike(E, Scene.Time);
            }
            if (P.burstEvery > 0 && !(P.flareMode > 0) && !(P.fireworkMode > 0))
            {
                E.BurstTimer = (E.BurstTimer ?? 0) - StepDelta;
                if (E.BurstTimer <= 0)
                {
                    E.BurstTimer = P.burstEvery * (0.7 + 0.6 * Math.random());
                    E.PendingBurst += P.burstCount;
                    E.OverrideOrigin = P.burstAtOrigin ? null : [-3 + 6 * Math.random(), 3.5 + 2.5 * Math.random(), -3 + 6 * Math.random()];
                }
            }
            if (E.OverrideOrigin) { Origin = E.OverrideOrigin; E.OverrideOrigin = null; }
            E.Accumulated += P.rate * StepDelta;
            Release = Math.floor(E.Accumulated);
            E.Accumulated -= Release;
            Release = Math.min(Gpu.cap, Release + E.PendingBurst);
            E.PendingBurst = 0;
            E.Head = (Head + Release) % Gpu.cap;
        }
        const PreseedCurtain = !!E.PreseedCurtain;
        E.PreseedCurtain = false;
        let FireworkShellIdx = 0, FireworkBurstX = Origin[0], FireworkBurstZ = Origin[2];
        let FireworkSnap = null;
        if ((P.fireworkMode || 0) > 0)
        {
            const Cycle = Math.max(2.2, P.burstEvery || 3.1);
            FireworkShellIdx = Math.floor(Scene.Time / Cycle);
            const Hx = Math.sin(FireworkShellIdx * 19.37 + 1.7) * 43758.5453;
            const Hz = Math.sin(FireworkShellIdx * 31.13 + 4.3) * 43758.5453;
            const Rx = (Hx - Math.floor(Hx)) - 0.5;
            const Rz = (Hz - Math.floor(Hz)) - 0.5;
            // Text / Spectacle shells stay nicely centered in view; Grand Bouquet fans across the sky!
            const SpreadX = (P.fireworkMode === 2 || (P.fireworkShape || 0) === 1 || (P.fireworkShape || 0) === 9) ? 1.8 : 4.2;
            const SpreadZ = (P.fireworkMode === 2 || (P.fireworkShape || 0) === 1 || (P.fireworkShape || 0) === 9) ? 1.2 : 2.8;
            FireworkBurstX = Origin[0] + Rx * SpreadX;
            FireworkBurstZ = Origin[2] + Rz * SpreadZ;
            const FwKey = `fw:${P.fireworkShape ?? 0}:${String(P.fireworkText || '2026').trim().toUpperCase().slice(0, 28)}:${Gpu.cap}:${FireworkShellIdx}`;
            if (!E._fwSnapCache || E._fwSnapCache.key !== FwKey)
                E._fwSnapCache = BuildFireworkSnapBuffer(P.fireworkShape ?? 0, P.fireworkText || '2026', Gpu.cap, FireworkShellIdx);
            FireworkSnap = E._fwSnapCache;
        }
        PE.fillSys(Gpu.cpu, P, {
            origin: Origin, dt: StepDelta, time: Scene.Time, frame: Scene.Step, cap: Gpu.cap, emitN: Release, head: Head,
            seed: (Scene.Step * 7919 + E.Number * 104729) >>> 0, preseedCurtain: PreseedCurtain, crackHit: E.CrackHit || null,
            fireworkShellIdx: FireworkShellIdx, fireworkBurstX: FireworkBurstX, fireworkBurstZ: FireworkBurstZ
        });
        const GShape = (P.gridMode || 0) > 0 ? Math.round(P.gridShape ?? 0) : -1;
        const GSurf = (P.gridMode || 0) > 0 ? Math.round(P.gridSurface ?? 0) : -1;
        const SampledAnchors = FireworkSnap || (GSurf >= 6 ? SampleObjectAnchors(P, Gpu.cap) : null);
        const Ramp = PE.packColorRamp ? PE.packColorRamp(P) : null;
        Jobs.push({
            gpu: Gpu, cpu: Gpu.cpu, origin: Origin, simulate: Simulate, emitN: Release, preseedCurtain: PreseedCurtain,
            kind: P.kind, readStats: Simulate && (SeedNow || Scene.Step % 3 === 0), draw: true, alpha: P.blend === 'alpha',
            cube: P.shape === 4 || P.shape === 18 || GShape === 1 || GShape === 2 || (P.crackMode || 0) > 0 || (P.ferroMode || 0) > 0,
            dualPrim: GShape === 6 || (P.crackMode || 0) > 0 || (P.ferroMode || 0) > 0 || (P.flareMode || 0) > 0 || (P.fireworkMode || 0) > 0,
            curveSegs: (P.dynMode || 0) > 0 ? Math.max(2, Math.min(32, Math.round(P.dynSegs ?? 16))) : 19,
            shatter: !!(P.transition && P.transition.fragment), fly: !!(P.transition && P.transition.after === 'butterfly'),
            rampData: Ramp ? Ramp.floats : null, rampCount: Ramp ? Ramp.count : 0,
            anchorSnapData: SampledAnchors ? SampledAnchors.snapBuffer : null,
            anchorSnapKey: SampledAnchors ? SampledAnchors.key : null
        });
    }

    const Geo = ConstructGeometry();
    const Lens = PackLens(Camera, Aspect);
    const Probe = Scene.ProbeEnabled && !Engine.probePending && Scene.Step % 4 === 0 ? ResolveProbeVoxel() : null;

    Engine.render({
        glob: Global, fields: Scene.PackedFields, lensCount: Lens.Count + Lens.ShimmerCount, lens: Lens.Packed, comps: Components, windTexData: WindTexData,
        fieldScene: Scene.FieldScene, preparedField: Scene.PreparedField,
        clear: ClearColour, floor: Scene.Wind.showFloor, arrows: Scene.Wind.showArrows,
        lines: Geo.Lines, lineCount: Geo.Lines.length / 7, meshes: Geo.Meshes, meshCount: Geo.Meshes.length / 10,
        arcSegs: Geo.ArcSegs, arcFlashes: Geo.ArcFlashes,
        jobs: Jobs, probe: Probe
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
//                                       FORCEFIELDVISUALIZATION OVERLAY & INSPECTOR MAP
//------------------------------------------------------------------------------------------------------------------------

const InsideDomain = (X, Y, Z) => X >= Domain.Minimum[0] && X <= Domain.Minimum[0] + Domain.Size[0] && Z >= Domain.Minimum[2] && Z <= Domain.Minimum[2] + Domain.Size[2];

function SampleParticleField(Contribution, X, Y, Z, Time, CellSpan, Out)
{
    Out[0] = 0; Out[1] = 0; Out[2] = 0;
    const T = Number.isFinite(Time) ? Time : Scene.Time;
    const Prepared = T === Scene.Time && Scene.PreparedField ? Scene.PreparedField : FC().Prepare(Scene.FieldScene, T);
    FC().EvaluatePrepared(Scene.FieldScene, Prepared, Contribution, X, Y, Z, CellSpan, Out, Scene.FieldScene.Visualization.Solo, 'Particles');
    const Gain = Contribution === 'Flow' ? (Scene.Wind?.windScale ?? 1) : 1;
    Out[0] *= Gain; Out[1] *= Gain; Out[2] *= Gain;
    return InsideDomain(X, Y, Z);
}

function DescribeSources()
{
    const Sources = [];
    for (const S of Scene.FieldScene.Sources)
    {
        const Info = FC().TypeById[S.Type];
        if (!Info) continue;
        const Token = 'src:' + S.Token;
        const ActiveFlag = FC().ResolveActivation(S, Scene.Time).Status === 'Active' ? 1 : 0;
        const Ext = S.Reach === 'Box' ? FC().ResolveBoxExtents(S) : null;
        const Turn = ((S.Bearing || 0) * Math.PI) / 180;
        const OutlineX = Ext && Ext.OffsetZ ? S.Centre[0] + Ext.OffsetZ * Math.sin(Turn) : undefined;
        const OutlineZ = Ext && Ext.OffsetZ ? S.Centre[2] - Ext.OffsetZ * Math.cos(Turn) : undefined;
        Sources.push({
            Id: Token, X: S.Centre[0], Y: S.Centre[1], Z: S.Centre[2],
            Radius: S.Radius, Height: S.Height, HalfX: Ext ? Ext.HalfX : S.HalfX, HalfZ: Ext ? Ext.HalfZ : S.HalfZ,
            OutlineX, OutlineZ, Rotation: S.Bearing || 0,
            Shape: S.Reach === 'Box' ? 'Box' : 'Circle',
            Point3D: S.Reach === 'Sphere',
            Contribution: Info.Contribution,
            Colour: Info.Colour,
            Label: S.Name,
            Active: ActiveFlag,
            Selected: Selected === Token,
            Dim: Scene.Overlay.Contribution && Info.Contribution !== Scene.Overlay.Contribution,
            Bearing: Info.Bearings?.length ? S[Info.Bearings[0][0]] : undefined
        });
    }
    for (const E of Scene.Effects)
    {
        if (!E.P.visible) continue;
        if (E.P.attractor && E.P.attractor.enabled)
            Sources.push({ Id: E.Token, X: E.P.origin[0], Y: E.P.origin[1], Z: E.P.origin[2], Radius: E.P.attractor.radius, Shape: 'Circle',
                           Contribution: 'Accelerate', Label: `${E.Name} · pull`, Active: 1, Selected: Selected === E.Token,
                           Dim: Scene.Overlay.Contribution !== 'Accelerate' });
        if (E.P.magneticField)
            Sources.push({ Id: E.Token, X: E.P.origin[0], Y: E.P.origin[1], Z: E.P.origin[2], Radius: E.P.magneticField.radius, Shape: 'Circle',
                           Contribution: 'Accelerate', Label: `${E.Name} · dipole`, Active: 1, Selected: Selected === E.Token,
                           Dim: Scene.Overlay.Contribution !== 'Accelerate' });
    }
    return Sources;
}

function ConstructOverlay()
{
    const Lib = FV();
    if (!Lib) return;
    const Canvas = document.getElementById('OverlayCanvas');
    Visualization = Lib.Create(Canvas, {
        Sample: SampleParticleField,
        MinimumSpan: 2,
        MaximumSpan: 80,
        OnMinimapPick: (X, Z) =>
        {
            Scene.Camera.Target[0] = Clamp(X, -6, 6);
            Scene.Camera.Target[2] = Clamp(Z, -6, 6);
            AppendLog('VIEW', 'Camera focused via minimap', `(${X.toFixed(1)}, ${Z.toFixed(1)}) m`, 'live');
        }
    });
    Visualization.Configure({
        Extent: { MinimumX: Domain.Minimum[0], MinimumZ: Domain.Minimum[2], MaximumX: Domain.Minimum[0] + Domain.Size[0], MaximumZ: Domain.Minimum[2] + Domain.Size[2], MinimumY: 0, MaximumY: 7.5 },
        ClipToExtent: true, CameraCells: 48, ShowScaleBar: false, ShowGroundGrid: false,
        Minimap: document.getElementById('MinimapCanvas'), RefreshInterval: 50
    });
    Visualization.View.Mode = 'Camera';
}

function SyncOverlayOptions()
{
    if (!Visualization) return;
    const Vz = Scene.FieldScene.Visualization;
    Visualization.Configure({
        Contribution: Scene.Overlay.Contribution,
        Quantity: Vz.Quantity || 'Speed',
        Heatmap: !!Vz.Heatmap,
        HeatmapStyle: Scene.Overlay.HeatmapStyle || 'Smooth',
        Opacity: Scene.Overlay.Opacity,
        FlowLines: !!Vz.FlowLines,
        FlowLineSpacing: Vz.FlowLineSpacing || 32,
        FlowLineLength: Vz.FlowLineLength || 28,
        FlowLinePulse: false,
        QuietCutoff: Vz.QuietCutoff ?? 0.02,
        Arrows: !!Vz.Arrows,
        ArrowSpacing: Vz.ArrowSpacing || 42,
        ArrowWeight: 1,
        Tracers: false,
        TracerCount: 0,
        ShowSources: !!Vz.ShowInfluence,
        ShowLabels: !!Vz.Labels,
        ShowLegend: !!Vz.ShowLegend !== false,
        ShowScaleBar: false,
        SliceHeight: Vz.SliceHeight ?? 1.5,
        Sources: DescribeSources(),
        ProbePoint: Scene.ProbeEnabled ? [Scene.Probe[0], Scene.Probe[2]] : null,
        Minimap: Scene.Overlay.Minimap ? document.getElementById('MinimapCanvas') : null
    });
}

function RenderAnemometerPin(Camera, Canvas)
{
    if (!Scene.ProbeEnabled || !Camera || !Canvas) return;
    const CssW = Canvas.clientWidth || Canvas.width, CssH = Canvas.clientHeight || Canvas.height;
    if (CssW < 8 || CssH < 8) return;
    const Head = ProjectWorldToCss(Camera.ViewProjection, Scene.Probe[0], Scene.Probe[1] + 0.04, Scene.Probe[2], CssW, CssH);
    if (!Head) return;

    const RatioX = Canvas.width / CssW, RatioY = Canvas.height / CssH;
    const Ctx = Canvas.getContext('2d');
    const Prep = Scene.PreparedField || FC().Prepare(Scene.FieldScene, Scene.Time);
    const All = FC().EvaluateAllPrepared(Scene.FieldScene, Prep, Scene.Probe[0], Scene.Probe[1], Scene.Probe[2], 0.5, Scene.FieldScene.Visualization.Solo, 'Particles');
    const Active = Scene.AnemometerDragging || Scene.AnemometerHover || Scene.AnemometerPlacing;

    Ctx.save();
    Ctx.setTransform(RatioX, 0, 0, RatioY, 0, 0);
    Ctx.shadowBlur = 0;

    if (Active)
    {
        Ctx.beginPath();
        Ctx.arc(Head.X, Head.Y, 16, 0, Math.PI * 2);
        Ctx.strokeStyle = 'rgba(180, 245, 218, 0.78)';
        Ctx.lineWidth = 1.4;
        Ctx.stroke();
    }

    // Precision callout badge offset cleanly from the 3D cup-anemometer head
    const Bearing = Wrap360(Math.atan2(All.Velocity[0], -All.Velocity[2]) * 180 / Math.PI);
    const Label = `${All.FlowMag.toFixed(1)} m/s · ${C.ResolveCompassPoint(Bearing)} · ${Scene.Probe[1].toFixed(1)}m`;
    Ctx.font = '500 10.5px "DM Sans", sans-serif';
    const Tw = Ctx.measureText(Label).width;
    const Px = Math.min(CssW - Tw - 20, Math.max(8, Head.X + 20));
    const Py = Math.max(12, Math.min(CssH - 28, Head.Y - 26));

    Ctx.strokeStyle = Active ? 'rgba(180, 245, 218, 0.7)' : 'rgba(146, 216, 194, 0.42)';
    Ctx.lineWidth = 1.0;
    Ctx.beginPath();
    Ctx.moveTo(Head.X + 4, Head.Y - 4);
    Ctx.lineTo(Px, Py + 10.5);
    Ctx.stroke();

    Ctx.fillStyle = 'rgba(13, 17, 22, 0.84)';
    Ctx.strokeStyle = Active ? 'rgba(150, 222, 192, 0.68)' : 'rgba(146, 216, 194, 0.32)';
    Ctx.lineWidth = 1;
    Ctx.beginPath();
    if (Ctx.roundRect) Ctx.roundRect(Px, Py, Tw + 14, 21, 999);
    else Ctx.rect(Px, Py, Tw + 14, 21);
    Ctx.fill();
    Ctx.stroke();
    Ctx.fillStyle = '#e8f5ef';
    Ctx.fillText(Label, Px + 7, Py + 14);
    Ctx.restore();
}

function DrawGizmoPill(Ctx, Text, X, Y, Accent = '#e8f5ef', Bg = 'rgba(12, 16, 20, 0.86)')
{
    Ctx.save();
    Ctx.font = '600 9.5px "DM Sans", system-ui, sans-serif';
    const Tw = Ctx.measureText(Text).width;
    const Pw = Tw + 12, Ph = 17;
    const Px = X - Pw * 0.5, Py = Y - Ph - 8;
    Ctx.fillStyle = Bg;
    Ctx.strokeStyle = Accent;
    Ctx.lineWidth = 1;
    Ctx.beginPath();
    if (Ctx.roundRect) Ctx.roundRect(Px, Py, Pw, Ph, 6);
    else Ctx.rect(Px, Py, Pw, Ph);
    Ctx.fill();
    Ctx.stroke();
    Ctx.fillStyle = Accent;
    Ctx.textAlign = 'center';
    Ctx.textBaseline = 'middle';
    Ctx.fillText(Text, X, Py + Ph * 0.5 + 0.5);
    Ctx.restore();
}

function DrawProjectedLoop3D(Ctx, VP, Pts3D, CssW, CssH, Closed = true)
{
    let Started = false;
    Ctx.beginPath();
    for (let I = 0; I < Pts3D.length; I++)
    {
        const P = ProjectWorldToCss(VP, Pts3D[I][0], Pts3D[I][1], Pts3D[I][2], CssW, CssH);
        if (!P) { Started = false; continue; }
        if (!Started) { Ctx.moveTo(P.X, P.Y); Started = true; }
        else Ctx.lineTo(P.X, P.Y);
    }
    if (Closed && Started && Pts3D.length > 2)
    {
        const P0 = ProjectWorldToCss(VP, Pts3D[0][0], Pts3D[0][1], Pts3D[0][2], CssW, CssH);
        if (P0) Ctx.lineTo(P0.X, P0.Y);
    }
    Ctx.stroke();
}

function RenderEffectorGizmos(Camera, Canvas)
{
    if (!Camera || !Canvas) return;
    if (!Scene.ShowHandles && !Scene.Overlay.Enabled && !Scene.EffDragging) return;
    const CssW = Canvas.clientWidth || Canvas.width, CssH = Canvas.clientHeight || Canvas.height;
    if (CssW < 8 || CssH < 8) return;
    const RatioX = Canvas.width / CssW, RatioY = Canvas.height / CssH;
    const Ctx = Canvas.getContext('2d');
    const VP = Camera.ViewProjection;
    Ctx.save();
    Ctx.setTransform(RatioX, 0, 0, RatioY, 0, 0);
    Ctx.shadowBlur = 0;
    Ctx.lineCap = 'round';
    Ctx.lineJoin = 'round';

    const HoverTok = Scene.EffHover ? Scene.EffHover.EffectToken : null;
    const HoverWhich = Scene.EffHover ? Scene.EffHover.Which : null;

    for (const E of Scene.Effects)
    {
        const P = E.P;
        if (!P.visible || (P.gridMode || 0) <= 0) continue;
        if ((P.gridEffector || 0) !== 5 && !P.effBlend) continue;
        const Effs = [
            { Which: 'effPos', Tag: 'EFF A', BaseCol: '#3ec46d', Pos: P.effPos, Size: Number(P.effSize ?? 0), Shape: P.effShape || 0 },
            { Which: 'effPos2', Tag: 'EFF B', BaseCol: '#f5b041', Pos: P.effPos2, Size: Number(P.effSize2 ?? 0), Shape: P.effShape2 || 0 }
        ];
        for (const Item of Effs)
        {
            if (!Item.Pos || Item.Size <= 0.01) continue;
            const [Ex, Ey, Ez] = Item.Pos;
            const S = Item.Size;
            const IsSel = Scene.EffDragging && Scene.EffDragEffect === E && Scene.EffDragWhich === Item.Which;
            const IsHover = HoverTok === E.Token && HoverWhich === Item.Which;
            const Pt = ProjectWorldToCss(VP, Ex, Ey, Ez, CssW, CssH);
            if (!Pt) continue;
            const Col = IsSel ? '#ffd166' : (IsHover ? '#ffffff' : Item.BaseCol);
            Ctx.strokeStyle = Col;
            Ctx.fillStyle = Col;
            Ctx.lineWidth = IsSel ? 2.4 : (IsHover ? 1.9 : 1.35);

            // Ground drop-line so 3D depth is always readable
            const GroundPt = ProjectWorldToCss(VP, Ex, 0.02, Ez, CssW, CssH);
            if (GroundPt && Ey > 0.08)
            {
                Ctx.save();
                Ctx.setLineDash([3, 4]);
                Ctx.globalAlpha = IsSel || IsHover ? 0.65 : 0.30;
                Ctx.beginPath(); Ctx.moveTo(Pt.X, Pt.Y); Ctx.lineTo(GroundPt.X, GroundPt.Y); Ctx.stroke();
                Ctx.setLineDash([]);
                Ctx.beginPath(); Ctx.arc(GroundPt.X, GroundPt.Y, 2.5, 0, Math.PI * 2); Ctx.fill();
                Ctx.restore();
            }

            const Shape = Item.Shape | 0;
            const Segs = 28;
            if (Shape === 1)
            {
                // True 3D projected wireframe Box
                const C = [
                    [Ex - S, Ey - S, Ez - S], [Ex + S, Ey - S, Ez - S], [Ex + S, Ey - S, Ez + S], [Ex - S, Ey - S, Ez + S],
                    [Ex - S, Ey + S, Ez - S], [Ex + S, Ey + S, Ez - S], [Ex + S, Ey + S, Ez + S], [Ex - S, Ey + S, Ez + S]
                ];
                DrawProjectedLoop3D(Ctx, VP, C.slice(0, 4), CssW, CssH, true);
                DrawProjectedLoop3D(Ctx, VP, C.slice(4, 8), CssW, CssH, true);
                for (let K = 0; K < 4; K++) DrawProjectedLoop3D(Ctx, VP, [C[K], C[K + 4]], CssW, CssH, false);
            }
            else if (Shape === 2)
            {
                // True 3D projected Torus outer & inner horizontal rings + 4 cross-sections
                const Outer = [], Inner = [];
                for (let K = 0; K < Segs; K++)
                {
                    const A = (K / Segs) * Math.PI * 2;
                    Outer.push([Ex + Math.cos(A) * (S * 1.32), Ey, Ez + Math.sin(A) * (S * 1.32)]);
                    Inner.push([Ex + Math.cos(A) * (S * 0.68), Ey, Ez + Math.sin(A) * (S * 0.68)]);
                }
                DrawProjectedLoop3D(Ctx, VP, Outer, CssW, CssH, true);
                DrawProjectedLoop3D(Ctx, VP, Inner, CssW, CssH, true);
            }
            else if (Shape === 3)
            {
                // True 3D Disc ring + crosshairs
                const Ring = [];
                for (let K = 0; K < Segs; K++)
                {
                    const A = (K / Segs) * Math.PI * 2;
                    Ring.push([Ex + Math.cos(A) * S, Ey, Ez + Math.sin(A) * S]);
                }
                DrawProjectedLoop3D(Ctx, VP, Ring, CssW, CssH, true);
                DrawProjectedLoop3D(Ctx, VP, [[Ex - S, Ey, Ez], [Ex + S, Ey, Ez]], CssW, CssH, false);
                DrawProjectedLoop3D(Ctx, VP, [[Ex, Ey, Ez - S], [Ex, Ey, Ez + S]], CssW, CssH, false);
            }
            else if (Shape === 4)
            {
                // True 3D Cone base ring + 4 ribs to apex
                const Base = [];
                const Apex = [Ex, Ey + S, Ez];
                for (let K = 0; K < Segs; K++)
                {
                    const A = (K / Segs) * Math.PI * 2;
                    Base.push([Ex + Math.cos(A) * S, Ey - S, Ez + Math.sin(A) * S]);
                }
                DrawProjectedLoop3D(Ctx, VP, Base, CssW, CssH, true);
                for (let K = 0; K < 4; K++) DrawProjectedLoop3D(Ctx, VP, [Base[K * 7], Apex], CssW, CssH, false);
            }
            else
            {
                // True 3D Sphere: XZ equator + XY & YZ meridians
                const Rxz = [], Rxy = [], Ryz = [];
                for (let K = 0; K < Segs; K++)
                {
                    const A = (K / Segs) * Math.PI * 2;
                    Rxz.push([Ex + Math.cos(A) * S, Ey, Ez + Math.sin(A) * S]);
                    Rxy.push([Ex + Math.cos(A) * S, Ey + Math.sin(A) * S, Ez]);
                    Ryz.push([Ex, Ey + Math.sin(A) * S, Ez + Math.cos(A) * S]);
                }
                DrawProjectedLoop3D(Ctx, VP, Rxz, CssW, CssH, true);
                Ctx.save(); Ctx.globalAlpha = IsSel || IsHover ? 0.72 : 0.42;
                DrawProjectedLoop3D(Ctx, VP, Rxy, CssW, CssH, true);
                DrawProjectedLoop3D(Ctx, VP, Ryz, CssW, CssH, true);
                Ctx.restore();
            }

            // Center draggable knob + label pill
            Ctx.fillStyle = '#111519';
            Ctx.beginPath(); Ctx.arc(Pt.X, Pt.Y, 6, 0, Math.PI * 2); Ctx.fill();
            Ctx.strokeStyle = Col; Ctx.lineWidth = 2; Ctx.stroke();
            Ctx.fillStyle = Col;
            Ctx.beginPath(); Ctx.arc(Pt.X, Pt.Y, 2.4, 0, Math.PI * 2); Ctx.fill();
            DrawGizmoPill(Ctx, IsSel ? `${Item.Tag} · DRAG` : Item.Tag, Pt.X, Pt.Y - 2, Col);
        }
    }
    Ctx.restore();
}

function RenderControlPointGizmos(Camera, Canvas)
{
    if (!Scene.ShowHandles || !Camera || !Canvas) return;
    const CssW = Canvas.clientWidth || Canvas.width, CssH = Canvas.clientHeight || Canvas.height;
    if (CssW < 8 || CssH < 8) return;
    const RatioX = Canvas.width / CssW, RatioY = Canvas.height / CssH;
    const Ctx = Canvas.getContext('2d');
    const VP = Camera.ViewProjection;
    Ctx.save();
    Ctx.setTransform(RatioX, 0, 0, RatioY, 0, 0);
    Ctx.shadowBlur = 0;
    Ctx.lineCap = 'round';
    Ctx.lineJoin = 'round';

    const HoverId = Scene.PointHover?.Id || null;
    const DragId = Scene.PointDragging?.Id || null;

    // 1. Selected Particle Effect Origin / Placed 3D Object Transform Handle
    const SelEff = EffectByToken(Selected);
    if (SelEff && SelEff.P.visible)
    {
        const P = SelEff.P;
        const IsArc = P.arcMode === 0 || P.arcMode === 1;
        const IsBeam = (P.beamMode || 0) > 0;
        if (!IsArc && !IsBeam)
        {
            const Id = SelEff.Token + ':origin';
            const Active = DragId === Id, Hover = HoverId === Id;
            const [Ox, Oy, Oz] = P.origin;
            const Pt = ProjectWorldToCss(VP, Ox, Oy, Oz, CssW, CssH);
            if (Pt)
            {
                const Col = Active ? '#ffd166' : (Hover ? '#ffffff' : '#83d6a1');
                const Gnd = ProjectWorldToCss(VP, Ox, 0.02, Oz, CssW, CssH);
                if (Gnd && Oy > 0.1)
                {
                    Ctx.save();
                    Ctx.strokeStyle = Col; Ctx.globalAlpha = 0.35; Ctx.setLineDash([3, 3]); Ctx.lineWidth = 1;
                    Ctx.beginPath(); Ctx.moveTo(Pt.X, Pt.Y); Ctx.lineTo(Gnd.X, Gnd.Y); Ctx.stroke();
                    Ctx.restore();
                }
                // Mini XYZ axis triad around origin handle
                const AxisLen = 0.42;
                for (const [Dv, ACol] of [[[AxisLen, 0, 0], '#ff6b6b'], [[0, AxisLen, 0], '#3ec46d'], [[0, 0, AxisLen], '#5aa9ff']])
                {
                    const Tip = ProjectWorldToCss(VP, Ox + Dv[0], Oy + Dv[1], Oz + Dv[2], CssW, CssH);
                    if (!Tip) continue;
                    Ctx.strokeStyle = ACol; Ctx.lineWidth = Active || Hover ? 2 : 1.35;
                    Ctx.beginPath(); Ctx.moveTo(Pt.X, Pt.Y); Ctx.lineTo(Tip.X, Tip.Y); Ctx.stroke();
                }
                // Diamond grab handle
                const R = Active ? 8 : (Hover ? 7.5 : 6);
                Ctx.fillStyle = 'rgba(14,18,22,0.88)';
                Ctx.strokeStyle = Col; Ctx.lineWidth = Active || Hover ? 2 : 1.4;
                Ctx.beginPath();
                Ctx.moveTo(Pt.X, Pt.Y - R); Ctx.lineTo(Pt.X + R, Pt.Y);
                Ctx.lineTo(Pt.X, Pt.Y + R); Ctx.lineTo(Pt.X - R, Pt.Y);
                Ctx.closePath(); Ctx.fill(); Ctx.stroke();
                if (Active || Hover || ((P.gridMode || 0) > 0 && (P.gridSurface ?? 0) >= 6))
                {
                    const IsObj = (P.gridMode || 0) > 0 && (P.gridSurface ?? 0) >= 2;
                    DrawGizmoPill(Ctx, Active ? 'SHIFT: ALTITUDE Y' : (IsObj ? 'MOVE OBJECT' : 'MOVE ORIGIN'), Pt.X, Pt.Y - 4, Col);
                }
            }
        }
    }

    // 2. Ferrofluid Magnetic Pole Handles
    for (const E of Scene.Effects)
    {
        const P = E.P;
        if (!P.visible || (P.ferroMode || 0) <= 0) continue;
        const FM = Math.max(1, Math.min(2, P.ferroMode | 0));
        const DishR = Math.max(0.8, P.radius || 2.1);
        const Mag = EvalFerroMagnetCPU(Scene.Time, P.origin, DishR, P.ferroMagnetSpeed ?? 1.35, FM, P);
        const Pt = ProjectWorldToCss(VP, Mag[0], Mag[1], Mag[2], CssW, CssH);
        if (!Pt) continue;
        const Id = E.Token + ':mag';
        const Active = DragId === Id, Hover = HoverId === Id;
        const Col = Active ? '#ffd166' : (Hover ? '#ffffff' : '#ff6b6b');
        Ctx.strokeStyle = Col; Ctx.lineWidth = Active || Hover ? 2.2 : 1.5;
        Ctx.beginPath(); Ctx.arc(Pt.X, Pt.Y, Active ? 13 : 10, 0, Math.PI * 2); Ctx.stroke();
        if (Active || Hover || P.ferroMagnetFree || Selected === E.Token)
            DrawGizmoPill(Ctx, P.ferroMagnetFree ? 'MAGNET · MANUAL' : 'DRAG MAGNET', Pt.X, Pt.Y - 6, Col);
    }

    // 3. Fracture Epicenter Handles
    for (const E of Scene.Effects)
    {
        const P = E.P;
        if (!P.visible || (P.crackMode || 0) <= 0) continue;
        const Hx = E.CrackHit ? E.CrackHit.x : P.origin[0];
        const Hz = E.CrackHit ? E.CrackHit.z : P.origin[2];
        const Pt = ProjectWorldToCss(VP, Hx, 0.14, Hz, CssW, CssH);
        if (!Pt) continue;
        const Id = E.Token + ':crack';
        const Active = DragId === Id, Hover = HoverId === Id;
        const Col = Active ? '#ffd166' : (Hover ? '#ffffff' : '#f5b041');
        Ctx.strokeStyle = Col; Ctx.lineWidth = 1.6;
        Ctx.beginPath();
        Ctx.arc(Pt.X, Pt.Y, 9, 0, Math.PI * 2);
        Ctx.moveTo(Pt.X - 13, Pt.Y); Ctx.lineTo(Pt.X + 13, Pt.Y);
        Ctx.moveTo(Pt.X, Pt.Y - 13); Ctx.lineTo(Pt.X, Pt.Y + 13);
        Ctx.stroke();
        if (Active || Hover || Selected === E.Token)
            DrawGizmoPill(Ctx, 'EPICENTER · DRAG', Pt.X, Pt.Y - 6, Col);
    }

    // 4. All Enabled Force Field Sources (draggable 3D nodes + dashed reach envelope on hover/select)
    const Solo = Scene.FieldScene.Visualization.Solo;
    for (const Src of Scene.FieldScene.Sources)
    {
        if (!Src.Enabled || (Solo && Src.Token !== Solo)) continue;
        const Info = FC().TypeById[Src.Type];
        const [Sx, Sy, Sz] = Src.Centre;
        const Pt = ProjectWorldToCss(VP, Sx, Sy, Sz, CssW, CssH);
        if (!Pt || Pt.X < -24 || Pt.X > CssW + 24 || Pt.Y < -24 || Pt.Y > CssH + 24) continue;
        const Id = 'src:' + Src.Token;
        const Active = DragId === Id, Hover = HoverId === Id, IsSel = Selected === Id;
        const Col = Active ? '#ffd166' : (Hover ? '#ffffff' : (Src.UseFormula ? '#ffb454' : (Info?.Colour || ContributionColours[Info?.Contribution || 'Flow'] || '#52d1a0')));

        if (Active || Hover || IsSel)
        {
            const R = Math.max(0.5, Src.Radius || 3);
            const Ring = [];
            for (let K = 0; K < 28; K++)
            {
                const A = (K / 28) * Math.PI * 2;
                Ring.push([Sx + Math.cos(A) * R, Sy, Sz + Math.sin(A) * R]);
            }
            Ctx.save();
            Ctx.strokeStyle = Col; Ctx.globalAlpha = Active ? 0.72 : 0.45; Ctx.lineWidth = 1.3; Ctx.setLineDash([4, 4]);
            DrawProjectedLoop3D(Ctx, VP, Ring, CssW, CssH, true);
            Ctx.restore();
            const Gnd = ProjectWorldToCss(VP, Sx, 0.02, Sz, CssW, CssH);
            if (Gnd && Sy > 0.1)
            {
                Ctx.save();
                Ctx.strokeStyle = Col; Ctx.globalAlpha = 0.32; Ctx.setLineDash([3, 3]); Ctx.lineWidth = 1;
                Ctx.beginPath(); Ctx.moveTo(Pt.X, Pt.Y); Ctx.lineTo(Gnd.X, Gnd.Y); Ctx.stroke();
                Ctx.restore();
            }
        }
        const RNode = Active || IsSel ? 11 : (Hover ? 10 : 8.5);
        Ctx.beginPath(); Ctx.arc(Pt.X, Pt.Y, RNode, 0, Math.PI * 2);
        Ctx.fillStyle = IsSel || Active ? 'rgba(18, 24, 30, 0.94)' : 'rgba(14, 19, 24, 0.80)';
        Ctx.fill();
        Ctx.lineWidth = Active || IsSel ? 2.0 : 1.4;
        Ctx.strokeStyle = Col;
        Ctx.stroke();

        // Compact symbol inside force node (+ repel/blast, - attract/gravity, swirl/dot others)
        const Bar = RNode * 0.44;
        Ctx.strokeStyle = '#f3f7fa'; Ctx.lineWidth = 1.7;
        if (Src.Type === 'Repulsor' || Src.Type === 'Blast')
        {
            Ctx.beginPath(); Ctx.moveTo(Pt.X - Bar, Pt.Y); Ctx.lineTo(Pt.X + Bar, Pt.Y);
            Ctx.moveTo(Pt.X, Pt.Y - Bar); Ctx.lineTo(Pt.X, Pt.Y + Bar); Ctx.stroke();
        }
        else if (Src.Type === 'Attractor' || Src.Type === 'Gravity')
        {
            Ctx.beginPath(); Ctx.moveTo(Pt.X - Bar, Pt.Y); Ctx.lineTo(Pt.X + Bar, Pt.Y); Ctx.stroke();
        }
        else
        {
            Ctx.fillStyle = Col;
            Ctx.beginPath(); Ctx.arc(Pt.X, Pt.Y, 2.8, 0, Math.PI * 2); Ctx.fill();
        }
        if (Active || Hover || IsSel)
            DrawGizmoPill(Ctx, Active ? `${Src.Name} · DRAG` : Src.Name, Pt.X, Pt.Y - 6, Col);
    }

    Ctx.restore();
}

function RenderSourceDebugMarkers(Camera, Canvas)
{
    // Only draw pole source badges when Field Overlay is explicitly enabled (and not superseded by Move Handles)
    if (Scene.ShowHandles || !Scene.Overlay.Enabled) return;
    if (!Camera || !Canvas) return;
    const CssW = Canvas.clientWidth || Canvas.width, CssH = Canvas.clientHeight || Canvas.height;
    if (CssW < 8 || CssH < 8) return;
    const Solo = Scene.FieldScene.Visualization.Solo;
    const PoleSources = Scene.FieldScene.Sources.filter(S => S.Enabled && (!Solo || S.Token === Solo) && (S.Type === 'Attractor' || S.Type === 'Repulsor' || S.Type === 'Gravity' || S.Type === 'Blast'));
    if (!PoleSources.length) return;

    const RatioX = Canvas.width / CssW, RatioY = Canvas.height / CssH;
    const Ctx = Canvas.getContext('2d');
    Ctx.save();
    Ctx.setTransform(RatioX, 0, 0, RatioY, 0, 0);
    Ctx.shadowBlur = 0;

    for (const Src of PoleSources)
    {
        const P = Src.Centre;
        const Pt = ProjectWorldToCss(Camera.ViewProjection, P[0], P[1], P[2], CssW, CssH);
        if (!Pt || Pt.X < -24 || Pt.X > CssW + 24 || Pt.Y < -24 || Pt.Y > CssH + 24) continue;
        const IsPlus = Src.Type === 'Repulsor' || Src.Type === 'Blast';
        const SelectedSrc = Selected === ('src:' + Src.Token);
        const R = SelectedSrc ? 13 : 11;

        Ctx.beginPath();
        Ctx.arc(Pt.X, Pt.Y, R, 0, Math.PI * 2);
        Ctx.fillStyle = SelectedSrc ? 'rgba(18, 24, 30, 0.92)' : 'rgba(14, 19, 24, 0.84)';
        Ctx.fill();

        Ctx.lineWidth = SelectedSrc ? 2.0 : 1.5;
        Ctx.strokeStyle = SelectedSrc
            ? '#f5c45a'
            : (IsPlus ? 'rgba(242, 134, 96, 0.92)' : 'rgba(120, 210, 198, 0.92)');
        Ctx.stroke();

        const Bar = R * 0.48;
        Ctx.strokeStyle = '#f3f7fa';
        Ctx.lineWidth = 2.0;
        Ctx.lineCap = 'round';
        Ctx.beginPath();
        Ctx.moveTo(Pt.X - Bar, Pt.Y);
        Ctx.lineTo(Pt.X + Bar, Pt.Y);
        if (IsPlus)
        {
            Ctx.moveTo(Pt.X, Pt.Y - Bar);
            Ctx.lineTo(Pt.X, Pt.Y + Bar);
        }
        Ctx.stroke();
    }
    Ctx.restore();
}

function RenderCurveDebugBanner(Canvas)
{
    if (!Canvas) return;
    const DebugFx = Scene.Effects.filter(E => E.P.visible && (E.P.dynMode || 0) > 0 && E.P.dynCurveDebug);
    if (!DebugFx.length) return;
    const CssW = Canvas.clientWidth || Canvas.width, CssH = Canvas.clientHeight || Canvas.height;
    if (CssW < 8 || CssH < 8) return;
    const RatioX = Canvas.width / CssW, RatioY = Canvas.height / CssH;
    const Ctx = Canvas.getContext('2d');
    Ctx.save();
    Ctx.setTransform(RatioX, 0, 0, RatioY, 0, 0);
    Ctx.font = '600 10.5px ui-monospace, SFMono-Regular, Menlo, monospace';
    let Y = 64;
    for (const E of DebugFx)
    {
        const Segs = Math.max(2, Math.min(32, Math.round(E.P.dynSegs ?? 16)));
        const Curves = Math.max(1, Math.round(E.P.dynRows ?? 20));
        const Label = `CURVE DEBUG · ${E.Name} · ${Segs} segs/curve (${Segs + 1} verts) · ${Curves} curves · ${Curves * (Segs + 1)} total verts`;
        const Tw = Ctx.measureText(Label).width;
        const Pw = Tw + 20, Ph = 22, X = 14;
        Ctx.fillStyle = 'rgba(8, 14, 20, 0.86)';
        Ctx.strokeStyle = 'rgba(36, 240, 255, 0.72)';
        Ctx.lineWidth = 1.1;
        Ctx.beginPath();
        if (Ctx.roundRect) Ctx.roundRect(X, Y, Pw, Ph, 6);
        else Ctx.rect(X, Y, Pw, Ph);
        Ctx.fill();
        Ctx.stroke();
        Ctx.fillStyle = '#ffe135';
        Ctx.beginPath();
        Ctx.arc(X + 9, Y + Ph * 0.5, 3, 0, Math.PI * 2);
        Ctx.fill();
        Ctx.fillStyle = '#d9faff';
        Ctx.textBaseline = 'middle';
        Ctx.fillText(Label, X + 16, Y + Ph * 0.5 + 0.5);
        Y += 28;
    }
    Ctx.restore();
}

function RenderOverlay(Camera)
{
    Scene.LastCamera = Camera;
    const Overlay = Scene.Overlay;
    const Canvas = document.getElementById('OverlayCanvas');
    document.getElementById('MinimapInset').classList.toggle('shown', Overlay.Enabled && Overlay.Minimap);
    const HasEffectorGizmos = (Scene.ShowHandles || Overlay.Enabled || Scene.EffDragging) && Scene.Effects.some(E => E.P.visible && (E.P.gridMode || 0) > 0 && ((E.P.gridEffector || 0) === 5 || E.P.effBlend));
    const HasControlHandles = Scene.ShowHandles && (Scene.Effects.some(E => E.P.visible) || Scene.FieldScene.Sources.some(S => S.Enabled));
    const HasCurveDebug = Scene.Effects.some(E => E.P.visible && (E.P.dynMode || 0) > 0 && E.P.dynCurveDebug);
    const HasMarkers = Scene.ProbeEnabled || HasEffectorGizmos || HasControlHandles || HasCurveDebug;
    if (!Overlay.Enabled || !Visualization)
    {
        if (HasMarkers || Canvas.dataset.cleared !== 'yes')
        {
            const Ctx = Canvas.getContext('2d');
            Ctx.setTransform(1, 0, 0, 1, 0, 0);
            Ctx.clearRect(0, 0, Canvas.width, Canvas.height);
            Canvas.dataset.cleared = HasMarkers ? '' : 'yes';
        }
    }
    else
    {
        Canvas.dataset.cleared = '';
        Visualization.View.ViewProjection = Camera.ViewProjection;
        SyncOverlayOptions();
        Visualization.Render(Scene.Time);
    }
    RenderSourceDebugMarkers(Camera, Canvas);
    RenderControlPointGizmos(Camera, Canvas);
    RenderAnemometerPin(Camera, Canvas);
    RenderEffectorGizmos(Camera, Canvas);
    RenderCurveDebugBanner(Canvas);

    if (Scene.ActiveInfographic && Scene.Step % 3 === 0)
    {
        const SelEff = EffectByToken(Selected);
        if (SelEff && Scene.ActiveInfographic.Token === SelEff.Token)
        {
            Scene.ActiveInfographic.Refresh({
                alive: SelEff.Gpu?.stats_?.alive ?? 0,
                cap: SelEff.Gpu?.cap || SelEff.P.capacity || 1,
                time: Scene.Time
            });
        }
    }

    if (InspectorViz && InspectorDebugCanvas && document.body.contains(InspectorDebugCanvas))
        RenderInspectorViz();
}

function RenderInspectorViz()
{
    if (!InspectorViz) return;
    const Vz = Scene.FieldScene.Visualization;
    const Src = SourceByToken(Selected);
    const Meta = Src ? FC().TypeById[Src.Type] : null;
    const Cont = Meta ? Meta.Contribution : Scene.Overlay.Contribution;
    InspectorViz.Configure({
        Contribution: Cont,
        Quantity: Vz.Quantity || 'Speed',
        Heatmap: !!Vz.Heatmap,
        HeatmapStyle: 'Smooth',
        Opacity: 0.85,
        FlowLines: true,
        FlowLineSpacing: 28,
        FlowLineLength: 18,
        FlowLinePulse: false,
        QuietCutoff: 0.02,
        Arrows: true,
        ArrowSpacing: 28,
        ArrowWeight: 1,
        Tracers: !!Vz.Tracers,
        TracerCount: 90,
        ShowSources: true,
        ShowLabels: false,
        ShowLegend: false,
        ShowScaleBar: true,
        ShowGroundGrid: true,
        SliceHeight: Src ? Math.max(0.1, Src.Centre[1] + (Src.Reach === 'Sphere' ? 0 : Math.min(1.0, (Src.Height || 2) * 0.45))) : (Vz.SliceHeight ?? 1.5),
        Extent: { MinimumX: Domain.Minimum[0], MinimumZ: Domain.Minimum[2], MaximumX: Domain.Minimum[0] + Domain.Size[0], MaximumZ: Domain.Minimum[2] + Domain.Size[2] },
        Sources: DescribeSources(),
        ProbePoint: Scene.ProbeEnabled ? [Scene.Probe[0], Scene.Probe[2]] : null
    });
    InspectorViz.Invalidate();
    InspectorViz.Render(Scene.Time);
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
    for (const Ch of ['All', 'PARTICLES', 'FORCES', 'WIND', 'VIEW'])
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

function SourceMetaSummary(Src)
{
    const Info = FC().TypeById[Src.Type];
    const Unit = ContributionUnits[Info.Contribution];
    const Reach = Src.Reach === 'Box'
        ? `${Src.HalfX.toFixed(1)}×${Src.HalfZ.toFixed(1)}m`
        : `${Src.Radius.toFixed(1)} m`;
    const Mode = Src.UseFormula ? 'fx(Formula)' : `${Math.abs(Src.Strength).toFixed(1)} ${Unit}`;
    return `${Info.Name} · ${Mode} · ${Reach}`;
}

function ConstructOutliner()
{
    if (OutlinerScope) C.ReleaseScope(OutlinerScope);
    OutlinerScope = C.OpenScope();
    const Content = document.getElementById('OutlinerContent');
    Content.textContent = '';

    const MakeGroup = (Title, Count) => C.Construct('div', 'tree-group', [
        C.Construct('div', 'tree-group-title', [C.ConstructGlyph('Chevron'), C.Construct('span', null, Title), C.Construct('span', 'group-count tabular', String(Count))])
    ]);

    const AddRow = (Host, Row) =>
    {
        const El = C.Construct('div', 'tree-row' + (Row.Depth ? ' nested' : '') + (Row.ExtraClass ? ` ${Row.ExtraClass}` : ''));
        El.style.setProperty('--row-accent', Row.Colour());
        const GlyphBox = C.Construct('div', 'row-glyph', C.ConstructGlyph(Row.Glyph));
        const NameEl = C.Construct('div', 'row-name', Row.Name());
        const MetaEl = C.Construct('div', 'row-meta', Row.Meta());
        const Btn = C.Construct('button', 'row-select', [GlyphBox, C.Construct('div', 'row-copy', [NameEl, MetaEl])]);
        Btn.type = 'button';
        Btn.addEventListener('click', () => SelectNode(Row.Id));

        const StatusEl = Row.Status ? C.Construct('span', 'row-status') : null;
        const Dot = C.Construct('span', 'row-state-dot');
        const EyeBtn = C.Construct('button', 'row-eye' + (Row.Locked ? ' locked' : ''), C.ConstructGlyph('Eye'));
        EyeBtn.type = 'button';
        EyeBtn.title = Row.Locked ? 'Permanent scene camera (cannot be hidden or deleted)' : 'Toggle visibility / active';
        EyeBtn.addEventListener('click', Ev => { Ev.stopPropagation(); Row.Flip(); C.Broadcast(); });

        El.append(Btn);
        if (StatusEl) El.append(StatusEl);
        El.append(Dot, EyeBtn);
        Host.append(El);

        C.Register(() =>
        {
            const On = Row.Enabled();
            El.classList.toggle('selected', Selected === Row.Id);
            El.classList.toggle('is-off', !On);
            Dot.classList.toggle('live', On);
            NameEl.textContent = Row.Name();
            MetaEl.textContent = Row.Meta();
            if (StatusEl && Row.Status)
            {
                const St = Row.Status();
                StatusEl.textContent = StatusText[St] || 'off';
                StatusEl.className = 'row-status ' + (StatusText[St] || 'off');
            }
            EyeBtn.textContent = '';
            if (Row.Locked) EyeBtn.append(C.ConstructGlyph('Eye'), C.ConstructGlyph('Lock'));
            else EyeBtn.append(C.ConstructGlyph(On ? 'Eye' : 'EyeOff'));
            const Match = !OutlinerQuery || `${Row.Name()} ${Row.Meta()}`.toLowerCase().includes(OutlinerQuery);
            El.hidden = !Match;
        });
    };

    const Sources = Scene.FieldScene.Sources;
    const FlowSources = Sources.filter(S => FC().TypeById[S.Type]?.Contribution === 'Flow');
    const ZoneSources = Sources.filter(S => FC().TypeById[S.Type]?.Contribution !== 'Flow');
    const TotalCount = Scene.Effects.length + Sources.length + 2;
    document.getElementById('outliner-subtitle').textContent = `Particle & force scene · ${TotalCount} nodes`;

    if (OutlinerFilter === 'all' || OutlinerFilter === 'forces')
    {
        const EnvGroup = MakeGroup('Environment', Sources.length + 2);
        if (OutlinerFilter === 'all' || OutlinerFilter === 'forces')
        {
            AddRow(EnvGroup, {
                Id: 'Field', Name: () => 'Force Field', Glyph: 'Field', Colour: () => '#8fd3c4', Depth: 0,
                Meta: () => `${Sources.length} bounded forces · ${Scene.Overlay.Enabled ? Scene.Overlay.Contribution : 'overlay off'}`,
                Enabled: () => Scene.Overlay.Enabled,
                Flip: () => { Scene.Overlay.Enabled = !Scene.Overlay.Enabled; AppendLog('VIEW', `Field overlay ${Scene.Overlay.Enabled ? 'on' : 'off'}`, null, 'live'); }
            });
            AddRow(EnvGroup, {
                Id: 'Wind', Name: () => 'Ambient Wind', Glyph: 'Wind', Colour: () => '#9fc7e8', Depth: 1,
                Meta: () =>
                {
                    const W = Scene.FieldScene.Wind;
                    return W.Enabled ? `${W.Speed.toFixed(1)} m/s at ${Math.round(W.Bearing)}° (${FC().ResolveBeaufort(W.Speed).Name})` : 'Ambient wind off';
                },
                Enabled: () => Scene.FieldScene.Wind.Enabled,
                Flip: () => { Scene.FieldScene.Wind.Enabled = !Scene.FieldScene.Wind.Enabled; AppendLog('WIND', `Ambient wind ${Scene.FieldScene.Wind.Enabled ? 'on' : 'off'}`, null, 'live'); }
            });

            if (FlowSources.length)
            {
                EnvGroup.append(C.Construct('div', 'outliner-subgroup', `Wind & Flow Generators (${FlowSources.length})`));
                for (const S of FlowSources)
                {
                    const Info = FC().TypeById[S.Type];
                    AddRow(EnvGroup, {
                        Id: 'src:' + S.Token, Name: () => S.Name, Glyph: Info?.Glyph || 'Wind',
                        Colour: () => Info?.Colour || ContributionColours.Flow, Depth: 1,
                        Meta: () => SourceMetaSummary(S),
                        Status: () => FC().ResolveActivation(S, Scene.Time).Status,
                        Enabled: () => S.Enabled,
                        Flip: () => { S.Enabled = !S.Enabled; AppendLog('FORCES', `${S.Name} ${S.Enabled ? 'enabled' : 'disabled'}`, null, 'live'); }
                    });
                }
            }
            if (ZoneSources.length)
            {
                EnvGroup.append(C.Construct('div', 'outliner-subgroup', `Accelerate & Damp Zones (${ZoneSources.length})`));
                for (const S of ZoneSources)
                {
                    const Info = FC().TypeById[S.Type];
                    AddRow(EnvGroup, {
                        Id: 'src:' + S.Token, Name: () => S.Name, Glyph: Info?.Glyph || 'Target',
                        Colour: () => Info?.Colour || ContributionColours[Info.Contribution], Depth: 1,
                        Meta: () => SourceMetaSummary(S),
                        Status: () => FC().ResolveActivation(S, Scene.Time).Status,
                        Enabled: () => S.Enabled,
                        Flip: () => { S.Enabled = !S.Enabled; AppendLog('FORCES', `${S.Name} ${S.Enabled ? 'enabled' : 'disabled'}`, null, 'live'); }
                    });
                }
            }
        }
        Content.append(EnvGroup);
    }

    const VfxCatEntries = (PE.VFX_CATEGORIES || [
        { id: 'Silk & Curl', label: 'Silk & Curl' },
        { id: 'Ballistics & Sparks', label: 'Ballistics & Sparks' },
        { id: 'Pyro & Water', label: 'Pyro & Water' },
        { id: 'Wind-Blown', label: 'Wind-Blown' },
        { id: 'Storms & Weather', label: 'Storms & Weather' }
    ]).map(V => typeof V === 'string' ? { id: V, label: V } : V);

    const FilterVfxCat = OutlinerFilter.startsWith('vfx:') ? OutlinerFilter.slice(4) : null;
    if (OutlinerFilter === 'all' || OutlinerFilter === 'effects' || FilterVfxCat)
    {
        const CategoryOf = E => PE.vfxCategoryOf ? PE.vfxCategoryOf(PE.presetById(E.PresetId) || { p: E.P }) : 'Particles';
        const SublabelOf = E => PE.vfxSublabelOf ? PE.vfxSublabelOf(PE.presetById(E.PresetId) || { p: E.P }) : (CategoryLabels[E.P.kind] || 'Particles');
        const FilteredEffects = FilterVfxCat ? Scene.Effects.filter(E => CategoryOf(E) === FilterVfxCat) : Scene.Effects;
        const FxTitle = FilterVfxCat ? `VFX · ${(VfxCatEntries.find(C => C.id === FilterVfxCat)?.label || FilterVfxCat)}` : 'Particle Effects';
        const FxGroup = MakeGroup(FxTitle, FilteredEffects.length);

        for (const Cat of VfxCatEntries)
        {
            const CatEffects = FilteredEffects.filter(E => CategoryOf(E) === Cat.id);
            if (!CatEffects.length) continue;
            FxGroup.append(C.Construct('div', 'outliner-subgroup', `${Cat.label} (${CatEffects.length})`));
            for (const Effect of CatEffects)
            {
                const Preset = PE.presetById(Effect.PresetId);
                AddRow(FxGroup, {
                    Id: Effect.Token,
                    Name: () => Effect.Name,
                    Glyph: GlyphForPreset(Preset),
                    Colour: () => DisplayColour(Effect.P.colA),
                    Depth: 1,
                    Meta: () => `${SublabelOf(Effect)} · ${FormatInteger(Effect.Gpu?.stats_?.alive || 0)} alive`,
                    Enabled: () => Effect.P.visible,
                    Flip: () => { Effect.P.visible = !Effect.P.visible; AppendLog('PARTICLES', `${Effect.Name} ${Effect.P.visible ? 'shown' : 'hidden'}`, null, 'live'); }
                });
            }
        }
        Content.append(FxGroup);
    }

    C.Register(() =>
    {
        const Vis = Scene.Effects.filter(E => E.P.visible).length + Scene.FieldScene.Sources.filter(S => S.Enabled).length;
        const Tot = Scene.Effects.length + Scene.FieldScene.Sources.length;
        document.getElementById('visible-count').textContent = String(Vis);
        document.getElementById('hidden-count').textContent = String(Tot - Vis);
        document.getElementById('status-effects').textContent = `${Scene.Effects.length} effects · ${Scene.FieldScene.Sources.length} force sources`;
    });

    C.CloseScope(OutlinerScope);
}

//------------------------------------------------------------------------------------------------------------------------
//                                       + MENU & MODAL 53-ASSET LIBRARY
//------------------------------------------------------------------------------------------------------------------------

function ConstructAddMenuAssets()
{
    const ListHost = document.getElementById('add-menu-assets');
    const CatOf = P => PE.vfxCategoryOf ? PE.vfxCategoryOf(P) : 'Particles';
    const Badge = document.getElementById('nav-preset-total');
    if (Badge) Badge.textContent = String(PE.Presets.filter(P => CatOf(P) !== 'Quarantine').length);
    if (!ListHost) return;

    const Q = AddMenuAssetQuery.trim().toLowerCase();
    const SubOf = P => PE.vfxSublabelOf ? PE.vfxSublabelOf(P) : (CategoryLabels[P.p.kind] || 'Particles');
    const ShowQuarantine = Q.includes('quarantine') || Q.includes('blossom') || Q.includes('slime') || Q.includes('jelly') || Q.includes('puddle') || Q.includes('mud') || Q.includes('splash') || Q.includes('dust');
    const Matches = PE.Presets.filter(P =>
        (ShowQuarantine || CatOf(P) !== 'Quarantine') &&
        (!Q || `${P.name} ${P.group} ${CatOf(P)} ${SubOf(P)} ${P.blurb || ''}`.toLowerCase().includes(Q))
    );
    ListHost.textContent = '';

    const Cats = (PE.VFX_CATEGORIES || ['Silk & Curl', 'Ballistics & Sparks', 'Pyro & Water', 'Wind-Blown', 'Storms & Weather'])
        .map(V => typeof V === 'string' ? { id: V, label: V } : V);
    for (const Cat of Cats)
    {
        if (Cat.id === 'Quarantine' && !ShowQuarantine) continue;
        const Bucket = Matches.filter(P => CatOf(P) === Cat.id);
        if (!Bucket.length) continue;
        ListHost.append(C.Construct('div', 'add-menu-group', [
            C.Construct('span', null, Cat.label),
            C.Construct('small', null, String(Bucket.length))
        ]));
        for (const Preset of Bucket)
        {
            const InScene = Scene.Effects.some(E => E.PresetId === Preset.id);
            const Swatch = C.Construct('span', 'add-menu-asset-swatch');
            Swatch.style.background = `linear-gradient(135deg, ${DisplayColour(Preset.p.colA)}, ${DisplayColour(Preset.p.colB || Preset.p.colA)})`;
            const Btn = C.Construct('button', 'add-menu-asset-btn' + (InScene ? ' in-scene' : ''), [
                Swatch,
                C.Construct('span', 'add-menu-asset-copy', [
                    C.Construct('span', null, Preset.name),
                    C.Construct('small', null, `${SubOf(Preset)} · ${Preset.group}`)
                ]),
                C.Construct('span', 'add-menu-asset-state', InScene ? '✓' : '+')
            ]);
            Btn.type = 'button';
            Btn.title = Preset.blurb || Preset.name;
            Btn.addEventListener('click', () =>
            {
                const Menu = document.getElementById('add-menu');
                if (Menu) Menu.hidden = true;
                document.getElementById('add-button')?.setAttribute('aria-expanded', 'false');
                AddEffect(Preset.id);
            });
            ListHost.append(Btn);
        }
    }
    if (!Matches.length)
        ListHost.append(C.Construct('div', 'add-menu-empty', 'No matching particle assets.'));
}

function OpenFullLibrary()
{
    const Veil = document.getElementById('LibraryVeil');
    const Sheet = document.getElementById('LibrarySheet');
    if (!Veil || !Sheet) return;

    const CatOf = P => PE.vfxCategoryOf ? PE.vfxCategoryOf(P) : 'Particles';
    const SubOf = P => PE.vfxSublabelOf ? PE.vfxSublabelOf(P) : (CategoryLabels[P.p.kind] || 'Particles');
    const CatEntries = (PE.VFX_CATEGORIES || ['Silk & Curl', 'Ballistics & Sparks', 'Pyro & Water', 'Wind-Blown', 'Storms & Weather'])
        .map(V => typeof V === 'string' ? { id: V, label: V } : V);
    const Groups = [{ id: 'All', label: 'All' }, ...CatEntries];
    if (!Groups.some(G => G.id === ModalGroup)) ModalGroup = 'All';

    const RenderGrid = (GridHost) =>
    {
        const Q = ModalQuery.trim().toLowerCase();
        const Matches = PE.Presets.filter(P =>
            (ModalGroup === 'All' ? CatOf(P) !== 'Quarantine' : CatOf(P) === ModalGroup) &&
            (!Q || `${P.name} ${P.group} ${CatOf(P)} ${SubOf(P)} ${P.blurb || ''}`.toLowerCase().includes(Q))
        );
        GridHost.textContent = '';
        for (const Preset of Matches)
        {
            const InScene = Scene.Effects.some(E => E.PresetId === Preset.id);
            const ColA = DisplayColour(Preset.p.colA);
            const ColB = DisplayColour(Preset.p.colB || Preset.p.colA);
            const GlyphEl = C.Construct('div', 'card-glyph', C.ConstructGlyph(GlyphForPreset(Preset)));
            GlyphEl.style.background = `${ColA}22`;
            GlyphEl.style.color = ColA;
            const Swatch = C.Construct('div', 'card-swatch');
            Swatch.style.background = `linear-gradient(90deg, ${ColA}, ${ColB})`;
            const Card = C.Construct('button', 'library-card', [
                C.Construct('div', 'card-top', [
                    GlyphEl,
                    C.Construct('div', null, [
                        C.Construct('div', 'card-name', Preset.name),
                        C.Construct('div', 'card-native', `${CatOf(Preset).toUpperCase()} · ${SubOf(Preset).toUpperCase()} · ${Preset.group.toUpperCase()}`)
                    ])
                ]),
                C.Construct('div', 'card-blurb', Preset.blurb || ''),
                Swatch,
                InScene ? C.Construct('span', 'card-badge', 'In Scene') : null
            ].filter(Boolean));
            Card.type = 'button';
            Card.addEventListener('click', () => { AddEffect(Preset.id); RenderGrid(GridHost); });
            GridHost.append(Card);
        }
    };

    const ActiveCount = PE.Presets.filter(P => CatOf(P) !== 'Quarantine').length;
    const QuarantineCount = PE.Presets.length - ActiveCount;
    Sheet.textContent = '';
    const CloseBtn = C.Construct('button', 'icon-button', C.ConstructGlyph('Close'));
    CloseBtn.type = 'button';
    CloseBtn.addEventListener('click', () => { Veil.classList.remove('shown'); SyncTopNav(); });

    const SearchInput = document.createElement('input');
    SearchInput.type = 'search';
    SearchInput.placeholder = `Search ${ActiveCount} active presets (${QuarantineCount} in Quarantine)…`;
    SearchInput.value = ModalQuery;
    const SearchBox = C.Construct('label', 'search library-search', [C.ConstructGlyph('Search'), SearchInput]);

    const Head = C.Construct('div', 'library-head', [
        C.Construct('h2', null, `Particle & VFX Asset Library · ${ActiveCount} Active`),
        SearchBox,
        CloseBtn
    ]);

    const Tabs = C.Construct('div', 'library-tabs');
    const Grid = C.Construct('div', 'library-grid');

    const RebuildTabs = () =>
    {
        Tabs.textContent = '';
        for (const G of Groups)
        {
            const Count = G.id === 'All' ? ActiveCount : PE.Presets.filter(P => CatOf(P) === G.id).length;
            const Tab = C.Construct('button', 'library-tab' + (ModalGroup === G.id ? ' active' : ''), [
                G.label, C.Construct('small', null, String(Count))
            ]);
            Tab.type = 'button';
            Tab.addEventListener('click', () => { ModalGroup = G.id; RebuildTabs(); RenderGrid(Grid); });
            Tabs.append(Tab);
        }
    };

    SearchInput.addEventListener('input', () => { ModalQuery = SearchInput.value; RenderGrid(Grid); });
    RebuildTabs();
    RenderGrid(Grid);
    Sheet.append(Head, Tabs, Grid);

    if (!Veil._BoundBackdrop)
    {
        Veil._BoundBackdrop = true;
        Veil.addEventListener('click', Ev => { if (Ev.target === Veil) { Veil.classList.remove('shown'); SyncTopNav(); } });
    }
    Veil.classList.add('shown');
    SyncTopNav();
    SearchInput.focus();
}

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
        Target[Key] = Value;
        PE.Edits.Announce(Label, Value);
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
        Visualization?.Invalidate();
        InspectorViz?.Invalidate();
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
    if (!Defs || !Defs.length)
    {
        Bar.hidden = true;
        return;
    }
    Bar.hidden = false;
    if (!Defs.some(D => D[0] === InspectorTab)) InspectorTab = Defs[0][0];
    for (const [Id, Label] of Defs)
    {
        const Btn = C.Construct('button', InspectorTab === Id ? 'active' : '', Label);
        Btn.type = 'button';
        Btn.addEventListener('click', () => { InspectorTab = Id; ConstructInspectorPage(); C.Broadcast(); });
        Bar.append(Btn);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                          EMBEDDED INTERACTIVE FORCEFIELDVISUALIZATION INSPECTOR WIDGET
//------------------------------------------------------------------------------------------------------------------------

function MountInspectorVectorPreview(Parent, FocusSource = null)
{
    const Lib = FV();
    if (!Lib) return;
    const Vz = Scene.FieldScene.Visualization;
    const Card = C.Construct('div', 'inspector-debug-viz');
    const Canvas = document.createElement('canvas');
    Canvas.className = 'inspector-debug-canvas';
    const Badge = C.Construct('div', 'inspector-debug-badge', FocusSource
        ? `ForceFieldVisualization · ${FocusSource.Name}`
        : 'ForceFieldVisualization · Chamber Map');

    const Bar = C.Construct('div', 'inspector-debug-bar');
    const MakeChip = (Label, Read, Toggle, ClassName = 'debug-chip') =>
    {
        const Btn = C.Construct('button', ClassName, Label);
        Btn.type = 'button';
        Btn.addEventListener('click', () => { Toggle(); C.Broadcast(); RenderInspectorViz(); });
        Bar.append(Btn);
        C.Register(() => Btn.classList.toggle('active', !!Read()));
        return Btn;
    };

    MakeChip('Flow lines', () => Vz.FlowLines, () => { Vz.FlowLines = !Vz.FlowLines; });
    MakeChip('Arrows', () => Vz.Arrows, () => { Vz.Arrows = !Vz.Arrows; });
    MakeChip('Heatmap', () => Vz.Heatmap, () => { Vz.Heatmap = !Vz.Heatmap; });
    if (FocusSource)
    {
        MakeChip('Solo this source', () => Vz.Solo === FocusSource.Token, () =>
        {
            Vz.Solo = Vz.Solo === FocusSource.Token ? null : FocusSource.Token;
        }, 'debug-chip solo');
    }

    Card.append(Canvas, Badge, Bar);
    Parent.append(Card);

    InspectorDebugCanvas = Canvas;
    InspectorViz = Lib.Create(Canvas, {
        Sample: (Contribution, X, Y, Z, Time, CellSpan, Out) =>
        {
            Out[0] = 0; Out[1] = 0; Out[2] = 0;
            if (FocusSource && Vz.Solo === FocusSource.Token)
            {
                const Prep = FC().Prepare(Scene.FieldScene, Time);
                FC().EvaluatePrepared(Scene.FieldScene, Prep, Contribution, X, Y, Z, CellSpan, Out, FocusSource.Token, 'Particles');
                return true;
            }
            return SampleParticleField(Contribution, X, Y, Z, Time, CellSpan, Out);
        },
        MinimumSpan: 2,
        MaximumSpan: 48,
        OnSourcePick: Token =>
        {
            if (Token && Token.startsWith('src:')) SelectNode(Token, false);
        },
        OnSourceMove: (Token, X, Z) =>
        {
            if (!Token || !Token.startsWith('src:')) return;
            const S = Scene.FieldScene.Sources.find(Item => Item.Token === Token.slice(4));
            if (S) { S.Centre[0] = Clamp(X, -8, 8); S.Centre[2] = Clamp(Z, -8, 8); C.Broadcast(); }
        }
    });
    InspectorViz.View.Mode = 'Map';
    InspectorViz.View.CentreX = FocusSource ? FocusSource.Centre[0] : 0;
    InspectorViz.View.CentreZ = FocusSource ? FocusSource.Centre[2] : 0;
    InspectorViz.View.Span = FocusSource ? Clamp((FocusSource.Radius || 3) * 3.2, 4, 20) : 13;

    // Allow dragging the Anemometer probe dot directly (or Shift+click/drag) on the 2D Inspector Map
    let DragMapAnem = false;
    Canvas.addEventListener('pointerdown', Ev =>
    {
        if (Ev.button !== 0 || !InspectorViz.ScreenToPlane) return;
        const Rect = Canvas.getBoundingClientRect();
        const Pt = InspectorViz.ScreenToPlane(Ev.clientX - Rect.left, Ev.clientY - Rect.top);
        if (!Pt) return;
        const NearProbe = Scene.ProbeEnabled && Math.hypot(Pt[0] - Scene.Probe[0], Pt[1] - Scene.Probe[2]) <= Math.max(0.45, InspectorViz.View.Span * 0.065);
        if (NearProbe || Ev.shiftKey || Scene.AnemometerPlacing)
        {
            Ev.stopImmediatePropagation();
            Ev.preventDefault();
            DragMapAnem = true;
            Scene.ProbeEnabled = true;
            Scene.Probe[0] = Clamp(Pt[0], -6, 6);
            Scene.Probe[2] = Clamp(Pt[1], -6, 6);
            Scene.AnemometerPlacing = false;
            Canvas.setPointerCapture(Ev.pointerId);
            SyncAnemometerToolState();
            RefreshLive();
            RenderInspectorViz();
            C.Broadcast();
        }
    }, true);
    Canvas.addEventListener('pointermove', Ev =>
    {
        if (!DragMapAnem || !InspectorViz.ScreenToPlane) return;
        Ev.stopImmediatePropagation();
        const Rect = Canvas.getBoundingClientRect();
        const Pt = InspectorViz.ScreenToPlane(Ev.clientX - Rect.left, Ev.clientY - Rect.top);
        if (!Pt) return;
        Scene.Probe[0] = Clamp(Pt[0], -6, 6);
        Scene.Probe[2] = Clamp(Pt[1], -6, 6);
        RefreshLive();
        RenderInspectorViz();
        C.Broadcast();
    }, true);
    const EndMapAnem = Ev =>
    {
        if (!DragMapAnem) return;
        Ev.stopImmediatePropagation();
        DragMapAnem = false;
        AppendLog('WIND', 'Anemometer moved via Chamber Map', `(${Scene.Probe.map(V => V.toFixed(1)).join(', ')}) m`, 'live');
    };
    Canvas.addEventListener('pointerup', EndMapAnem, true);
    Canvas.addEventListener('pointercancel', EndMapAnem, true);

    InspectorViz.AttachNavigation?.();
    RenderInspectorViz();
}

//------------------------------------------------------------------------------------------------------------------------
//                                              EFFECT INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

function ClassifyEffect(P)
{
    const IsArc = IsArcMode(P.arcMode);
    const IsBeam = (P.beamMode || 0) > 0;
    const IsTransition = !!P.transition;
    const AsmMode = IsTransition ? (P.transition.assemble | 0) : 0;
    const IsAssembly = IsTransition && AsmMode > 0;
    const CurlMode = IsTransition ? 0 : (P.curlMode || 0);
    const IsCurl = CurlMode > 0;
    const IsBoids = P.kind === 6;
    const FireworkMode = !IsTransition && !IsArc && !IsBeam && !IsBoids ? Math.max(0, Math.min(2, P.fireworkMode | 0)) : 0;
    const IsFirework = FireworkMode > 0;
    const FlareMode = !IsTransition && !IsArc && !IsBeam && !IsBoids && !IsFirework ? Math.max(0, Math.min(2, P.flareMode | 0)) : 0;
    const IsFlare = FlareMode > 0;
    const CrackMode = !IsTransition && !IsArc && !IsBeam && !IsBoids && !IsFirework && !IsFlare ? Math.max(0, Math.min(2, P.crackMode | 0)) : 0;
    const IsCrack = CrackMode > 0;
    const FerroMode = !IsTransition && !IsArc && !IsBeam && !IsBoids && !IsFirework && !IsFlare && !IsCrack ? Math.max(0, Math.min(2, P.ferroMode | 0)) : 0;
    const IsFerro = FerroMode > 0;
    const MudMode = !IsTransition && !IsArc && !IsBeam && !IsBoids && !IsFirework && !IsFlare && !IsCrack && !IsFerro ? Math.max(0, Math.min(3, P.mudSplashMode | 0)) : 0;
    const IsMud = MudMode > 0;
    const GridMode = !IsTransition && !IsArc && !IsBeam && !IsBoids && !IsFirework && !IsFlare && !IsCrack && !IsFerro && !IsMud ? Math.max(0, Math.min(2, P.gridMode | 0)) : 0;
    const IsGrid = GridMode > 0;
    const BeadsMode = !IsTransition && !IsArc && !IsBeam && !IsBoids && !IsFirework && !IsFlare && !IsCrack && !IsFerro && !IsMud && !IsGrid ? Math.max(0, Math.min(3, P.beadsMode | 0)) : 0;
    const IsBeads = BeadsMode > 0;
    const SeedHeadMode = !IsTransition && !IsArc && !IsBeam && !IsBoids && !IsFirework && !IsFlare && !IsCrack && !IsFerro && !IsMud && !IsGrid && !IsBeads ? Math.max(0, Math.min(2, P.seedHeadMode | 0)) : 0;
    const IsSeedHead = SeedHeadMode > 0;
    const JellyMode = !IsTransition && !IsArc && !IsBeam && !IsBoids && !IsFirework && !IsFlare && !IsCrack && !IsFerro && !IsMud && !IsGrid && !IsBeads && !IsSeedHead ? Math.max(0, Math.min(2, P.jellyMode | 0)) : 0;
    const IsJelly = JellyMode > 0;
    const DynMode = !IsTransition && !IsArc && !IsBeam && !IsBoids && !IsFirework && !IsFlare && !IsCrack && !IsFerro && !IsMud && !IsGrid && !IsBeads && !IsSeedHead && !IsJelly ? Math.max(0, Math.min(9, P.dynMode | 0)) : 0;
    const IsDyn = DynMode > 0;
    const WebMode = !IsTransition && !IsArc && !IsBeam && !IsBoids && !IsFirework && !IsFlare && !IsCrack && !IsFerro && !IsMud && !IsGrid && !IsBeads && !IsSeedHead && !IsJelly && !IsDyn ? Math.max(0, Math.min(2, P.webMode | 0)) : 0;
    const IsWeb = WebMode > 0;
    const IsObjectPour = CurlMode === 3 && (P.pulseMode || 0) > 0;
    const Shape = P.shape ?? 3;
    const IsDiscPlane = Shape === 10 || Shape === 15;
    const IsSteadyOverlay = IsCurl || IsWeb || IsDyn || IsGrid || IsFerro || IsCrack || IsBeads || IsSeedHead || IsJelly || IsFirework || IsFlare || Shape === 16 || (Shape === 15 && (P.leafMode || 0) === 0);
    const IsExpandingWave = Shape === 10 || Shape === 11 || (Shape === 15 && (P.leafMode || 0) >= 1);
    const UsesEmitVolume = !IsArc && !IsBeam && !IsWeb && !IsDyn && !IsGrid && !IsFerro && !IsCrack && !IsMud && !IsBeads && !IsSeedHead && !IsJelly && !IsFirework && !IsFlare && CurlMode !== 2 && !IsObjectPour && !IsAssembly;
    const EmitSh = P.emitShape ?? 0;
    const UsesRate = !IsTransition && !IsWeb && !IsDyn && !IsGrid && !IsFerro && !IsCrack && !IsBeads && !IsSeedHead && !IsJelly && !IsFirework && !IsFlare;
    const UsesRadius = IsArc || (IsBeam && P.showElectrodes !== false) || WebMode === 1 || IsFerro || IsMud || BeadsMode === 2 || IsSeedHead || IsJelly || (IsGrid && (P.gridSurface ?? 0) >= 2 && (P.gridSurface ?? 0) !== 5) || (UsesEmitVolume && (EmitSh === 1 || EmitSh === 2));
    const UsesBoxHalf = IsObjectPour || CurlMode === 1 || WebMode === 2 || IsDyn || IsCrack || BeadsMode === 1 || BeadsMode === 3 || IsSeedHead || P.arcMode === 3 || (IsGrid && ((P.gridSurface ?? 0) <= 1 || (P.gridSurface ?? 0) === 5)) || (IsTransition && AsmMode === 1) || (UsesEmitVolume && (EmitSh === 3 || EmitSh === 4));
    const HasCustomGeometry = IsGrid || IsFerro || IsCrack || IsBeads || IsSeedHead || IsJelly || IsDyn || IsWeb || IsFirework || IsFlare;
    const UsesTimedBurst = !IsSteadyOverlay && !IsTransition && !IsArc && !IsBeam && !IsBoids && !P.blackHole && !P.heatShimmer;
    const UsesDirVector = !IsTransition && !IsCurl && !IsArc && !IsBeam && !IsBoids && !IsDyn && !IsGrid && !IsFerro && !IsCrack && !IsMud && !IsBeads && !IsSeedHead && !IsJelly && !IsFirework && !IsFlare && Shape !== 11 && (Shape !== 16 || (P.shieldForm ?? 0) >= 1);
    const UsesSpread = UsesDirVector && !IsDiscPlane && !IsWeb && !IsDyn && Shape !== 16;
    const UsesSpeed = !IsCurl && !IsWeb && !IsDyn && !IsGrid && !IsFerro && !IsCrack && !IsBeads && !IsSeedHead && !IsJelly && !IsFirework && !IsFlare && !IsAssembly && !IsBoids && (!IsSteadyOverlay || IsMud);
    const UsesLife = !IsAssembly && !IsWeb && !IsDyn && !IsGrid && !IsFerro && !IsCrack && !IsBeads && !IsSeedHead && !IsJelly && !IsFirework && !IsFlare;
    const UsesSizeEnd = !IsAssembly && !IsWeb && !IsDyn && !IsGrid && !IsFerro && !IsCrack && !IsBeads && !IsSeedHead && !IsJelly;
    const UsesColB = !IsAssembly;
    const UsesColC = IsCurl || IsWeb || IsDyn || IsGrid || IsFerro || IsCrack || IsMud || IsBeads || IsSeedHead || IsJelly || IsFirework || IsFlare || (P.trailMode || 0) > 0 || IsBoids || IsBeam || Shape === 0 || Shape >= 10;
    const UsesColorRamp = !IsAssembly;
    const UsesTrailMode = !IsCurl && !IsWeb && !IsDyn && !IsGrid && !IsFerro && !IsCrack && !IsMud && !IsBeads && !IsSeedHead && !IsJelly && !IsFirework && !IsFlare && !IsTransition && !IsBoids && !IsArc && !IsBeam && (Shape < 10 || Shape === 14);
    const UsesWind = !IsAssembly && !IsGrid && !IsFerro && !IsCrack && !(Shape === 16 || (Shape === 15 && (P.leafMode || 0) === 0));
    const UsesDynamics = !IsAssembly && !IsGrid && !IsFerro && !IsCrack && !(Shape === 16 || (Shape === 15 && (P.leafMode || 0) === 0));
    const UsesLaunchCone = !IsTransition && !IsCurl && !IsWeb && !IsDyn && !IsGrid && !IsFerro && !IsCrack && !IsMud && !IsBeads && !IsSeedHead && !IsJelly && !IsFirework && !IsFlare && !IsArc && !IsBeam && !IsBoids && Shape !== 10 && Shape !== 11 && Shape !== 15 && Shape !== 16 && Shape !== 17;

    let BounceBlockReason = null;
    if (IsAssembly) BounceBlockReason = 'Analytic assembly ignores floor collision';
    else if (IsFirework) BounceBlockReason = 'Firework stars pop into a brief impact flare and vanish instantly on floor or object touch';
    else if (IsTransition && P.transition.fragment) BounceBlockReason = 'Shatter cubes break into 8 pieces on floor contact';
    else if (IsGrid) BounceBlockReason = 'Effector lattice is anchored to its surface geometry';
    else if (IsFerro) BounceBlockReason = 'Ferrofluid spikes align dynamically with magnetic flux';
    else if (IsCrack) BounceBlockReason = 'Fractured ground slabs hinge & tilt in response to impacts';
    else if (IsBeads) BounceBlockReason = 'Hanging beads, chimes & doorway drapes swing from top rails';
    else if (IsSeedHead) BounceBlockReason = 'Dandelion puffs & blossoms settle gently after gust detachment';
    else if (IsDyn) BounceBlockReason = 'Dynamic rigging & flora respond directly to wind & 3D ball collisions';
    else if (IsWeb) BounceBlockReason = 'Anchored spiderweb strands flex in wind instead of floor bouncing';
    else if (IsCurl) BounceBlockReason = 'Curl flow field does not bounce on floor';
    else if (P.kind === 1) BounceBlockReason = 'Wind-driven particles settle cleanly on floor';
    else if (IsBoids) BounceBlockReason = 'Boids steer smoothly above the floor';
    else if (Shape === 15 || Shape === 16) BounceBlockReason = 'Holographic HUD disc / shield ignores floor bounce';

    return {
        IsArc, IsBeam, IsTransition, AsmMode, IsAssembly,
        CurlMode, IsCurl, FireworkMode, IsFirework, FlareMode, IsFlare, CrackMode, IsCrack, FerroMode, IsFerro, MudMode, IsMud, BeadsMode, IsBeads, SeedHeadMode, IsSeedHead, JellyMode, IsJelly, GridMode, IsGrid, DynMode, IsDyn, WebMode, IsWeb, IsObjectPour, IsBoids, Shape,
        IsDiscPlane, IsSteadyOverlay, IsExpandingWave,
        UsesRate, UsesEmitVolume, UsesRadius, UsesBoxHalf,
        UsesDirVector, UsesSpread, UsesSpeed, UsesLife,
        UsesSizeEnd, UsesColB, UsesColC, UsesColorRamp, UsesTrailMode,
        UsesWind, UsesDynamics, UsesLaunchCone, HasCustomGeometry, UsesTimedBurst, BounceBlockReason
    };
}

function SpectrumGradient(P, Cls)
{
    const C = Cls || ClassifyEffect(P);
    if (!C.UsesColB) return DisplayColour(P.colA);
    return RampCss(P);
}

function AddColorRampEditor(Host, Effect, P, Cls)
{
    const MAX_STOPS = 16;
    let SelectedIdx = 0;
    const CommitStops = Stops =>
    {
        PE.syncColorStops(P, Stops);
        SelectedIdx = Clamp(SelectedIdx, 0, Math.max(0, Stops.length - 1));
        PE.Edits.Announce('Colour ramp', `${Stops.length} stop${Stops.length === 1 ? '' : 's'}`);
        AppendLog('PARTICLES', `${Effect.Name}: Colour ramp`, `${Stops.length} stops`, 'live');
        C.Broadcast();
    };
    const ClampPos = (Stops, Idx, Pos) =>
    {
        const Lo = Idx > 0 ? Stops[Idx - 1].pos + 0.001 : 0;
        const Hi = Idx < Stops.length - 1 ? Stops[Idx + 1].pos - 0.001 : 1;
        return Clamp(Pos, Math.min(Lo, Hi), Math.max(Lo, Hi));
    };

    const Wrap = C.Construct('div', 'ramp-editor');
    Host.append(Wrap);

    const Track = C.Construct('div', 'ramp-track');
    const Fill = C.Construct('div', 'ramp-track-fill');
    Track.append(Fill);
    Wrap.append(Track);

    const Handles = [];
    let Dragging = false;
    const RenderHandles = () =>
    {
        if (Dragging) return;
        for (const H of Handles) H.remove();
        Handles.length = 0;
        const Stops = RampStopsOf(P);
        SelectedIdx = Clamp(SelectedIdx, 0, Math.max(0, Stops.length - 1));
        Stops.forEach((Stop, Idx) =>
        {
            const Handle = C.Construct('button', 'ramp-stop' + (Idx === SelectedIdx ? ' selected' : ''));
            Handle.type = 'button';
            Handle.style.left = (Stop.pos * 100).toFixed(2) + '%';
            Handle.style.background = DisplayColourA(Stop.color);
            Handle.title = `Stop ${Idx + 1} · ${(Stop.pos * 100).toFixed(0)}% — drag to move, click to select`;
            Handle.setAttribute('aria-label', `Colour stop ${Idx + 1} at ${(Stop.pos * 100).toFixed(0)} percent`);
            Handle.addEventListener('pointerdown', Event =>
            {
                Event.preventDefault();
                Event.stopPropagation();
                SelectedIdx = Idx;
                for (const H of Handles) H.classList.remove('selected');
                Handle.classList.add('selected');
                Dragging = true;
                Handle.setPointerCapture(Event.pointerId);
                const Rect = Track.getBoundingClientRect();
                const Move = MoveEvent =>
                {
                    const Fresh = RampStopsOf(P);
                    const Pos = Clamp((MoveEvent.clientX - Rect.left) / Math.max(1, Rect.width), 0, 1);
                    Fresh[Idx].pos = ClampPos(Fresh, Idx, Pos);
                    PE.syncColorStops(P, Fresh);
                    Handle.style.left = (Fresh[Idx].pos * 100).toFixed(2) + '%';
                    Fill.style.background = RampCss(P);
                    C.Broadcast();
                };
                const Up = () =>
                {
                    Handle.removeEventListener('pointermove', Move);
                    Handle.removeEventListener('pointerup', Up);
                    Handle.removeEventListener('pointercancel', Up);
                    Dragging = false;
                    RenderHandles();
                    C.Broadcast();
                };
                Handle.addEventListener('pointermove', Move);
                Handle.addEventListener('pointerup', Up);
                Handle.addEventListener('pointercancel', Up);
            });
            Track.append(Handle);
            Handles.push(Handle);
        });
        Fill.style.background = RampCss(P);
    };
    Track.addEventListener('pointerdown', Event =>
    {
        if (Event.target !== Track && Event.target !== Fill) return;
        const Stops = RampStopsOf(P);
        if (Stops.length >= MAX_STOPS) return;
        const Rect = Track.getBoundingClientRect();
        const Pos = Clamp((Event.clientX - Rect.left) / Math.max(1, Rect.width), 0, 1);
        const Sample = PE.evalColorRampAt ? PE.evalColorRampAt(Stops, Pos, P) : [...Stops[0].color];
        Stops.push({ color: [...Sample], pos: Pos });
        Stops.sort((A, B) => A.pos - B.pos);
        SelectedIdx = Stops.findIndex(S => Math.abs(S.pos - Pos) < 1e-6);
        CommitStops(Stops);
        RenderHandles();
    });
    C.Register(() => RenderHandles());

    const Meta = C.Construct('p', 'property-hint ramp-meta');
    Wrap.append(Meta);
    C.Register(() =>
    {
        const Stops = RampStopsOf(P);
        const Mode = (PE.COLOR_BY_MODES || []).find(M => M.id === (P.colorBy || 'modeDefault'));
        Meta.textContent = `${Stops.length} stop${Stops.length === 1 ? '' : 's'} · ${Mode ? Mode.label : 'Mode default'} · click the track to add a stop, drag handles to move them.`;
    });

    const StopTools = C.Construct('div', 'ramp-tools');
    Wrap.append(StopTools);
    const AddBtn = C.Construct('button', 'chip-add-btn', '+ Add stop');
    AddBtn.type = 'button';
    AddBtn.addEventListener('click', () =>
    {
        const Stops = RampStopsOf(P);
        if (Stops.length >= MAX_STOPS) return;
        const At = Stops.length > 1 ? 0.5 : 0.5;
        const Sample = PE.evalColorRampAt ? PE.evalColorRampAt(Stops, At, P) : [...Stops[0].color];
        Stops.push({ color: [...Sample], pos: At });
        Stops.sort((A, B) => A.pos - B.pos);
        SelectedIdx = Stops.findIndex(S => Math.abs(S.pos - At) < 1e-6);
        CommitStops(Stops);
        RenderHandles();
    });
    const RemoveBtn = C.Construct('button', 'chip-add-btn', '− Remove stop');
    RemoveBtn.type = 'button';
    RemoveBtn.addEventListener('click', () =>
    {
        const Stops = RampStopsOf(P);
        if (Stops.length <= 2) return;
        Stops.splice(SelectedIdx, 1);
        SelectedIdx = Clamp(SelectedIdx, 0, Stops.length - 1);
        CommitStops(Stops);
        RenderHandles();
    });
    const ResetBtn = C.Construct('button', 'chip-add-btn', 'Preset colours');
    ResetBtn.type = 'button';
    ResetBtn.title = 'Discard custom stops and restore the preset base / mid / crest colours';
    ResetBtn.addEventListener('click', () =>
    {
        const Preset = PE.presetById(Effect.PresetId);
        if (Preset && Preset.p)
        {
            P.colA = [...Preset.p.colA];
            P.colB = [...(Preset.p.colB || Preset.p.colA)];
            P.colC = [...(Preset.p.colC || Preset.p.colB || Preset.p.colA)];
        }
        P.colorStops = null;
        SelectedIdx = 0;
        PE.Edits.Announce('Colour ramp', 'preset colours');
        AppendLog('PARTICLES', `${Effect.Name}: Colour ramp`, 'preset colours', 'live');
        C.Broadcast();
        RenderHandles();
    });
    StopTools.append(AddBtn, RemoveBtn, ResetBtn);
    C.Register(() =>
    {
        const N = RampStopsOf(P).length;
        AddBtn.disabled = N >= MAX_STOPS;
        RemoveBtn.disabled = N <= 2;
    });

    const SelLabel = C.Construct('span', 'property-label ramp-sel-label');
    Wrap.append(SelLabel);
    C.Register(() =>
    {
        const Stops = RampStopsOf(P);
        const S = Stops[SelectedIdx];
        SelLabel.textContent = S ? `Stop ${SelectedIdx + 1} of ${Stops.length} · ${(S.pos * 100).toFixed(0)}%` : '';
    });
    C.ColourRow(Wrap, {
        Label: 'Selected stop colour',
        Alpha: true,
        Read: () => { const S = RampStopsOf(P)[SelectedIdx]; return S ? [...S.color] : [1, 1, 1, 1]; },
        Write: Col =>
        {
            const Stops = RampStopsOf(P);
            if (!Stops[SelectedIdx]) return;
            Stops[SelectedIdx].color = [...Col];
            CommitStops(Stops);
            RenderHandles();
        }
    });
    Range(Wrap, {
        Label: 'Selected stop position',
        Read: () => { const S = RampStopsOf(P)[SelectedIdx]; return S ? S.pos : 0; },
        Write: V =>
        {
            const Stops = RampStopsOf(P);
            if (!Stops[SelectedIdx]) return;
            Stops[SelectedIdx].pos = ClampPos(Stops, SelectedIdx, Number(V));
            CommitStops(Stops);
            RenderHandles();
        }
    }, 0, 1, 0.01, 2, '');

    const PalLabel = C.Construct('span', 'property-label ramp-pal-label', 'Palette presets');
    Wrap.append(PalLabel);
    const PalRow = C.Construct('div', 'ramp-palettes');
    Wrap.append(PalRow);
    for (const Pal of (PE.COLOR_PALETTES || []))
    {
        const Btn = C.Construct('button', 'ramp-palette-btn');
        Btn.type = 'button';
        Btn.title = Pal.label;
        Btn.setAttribute('aria-label', `Apply palette: ${Pal.label}`);
        Btn.style.background = `linear-gradient(90deg, ${Pal.colors.join(', ')})`;
        Btn.addEventListener('click', () =>
        {
            const N = Pal.colors.length;
            const Stops = Pal.colors.map((Hex, I) => ({ color: PE.hexLinear(Hex, 1), pos: N > 1 ? I / (N - 1) : 0 }));
            SelectedIdx = 0;
            CommitStops(Stops);
            RenderHandles();
        });
        PalRow.append(Btn);
    }

    const Mode = (PE.COLOR_BY_MODES || []).find(M => M.id === (P.colorBy || 'modeDefault')) || (PE.COLOR_BY_MODES || [])[0];
    const ByBinding = {
        Label: 'Colour driven by',
        Options: (PE.COLOR_BY_MODES || []).map(M => [M.label, M.id]),
        Read: () => P.colorBy || 'modeDefault',
        Write: V =>
        {
            P.colorBy = V;
            const Next = (PE.COLOR_BY_MODES || []).find(M => M.id === V);
            if (Next) { P.colorMin = Next.defaultMin; P.colorMax = Next.defaultMax; }
            PE.Edits.Announce('Colour driven by', V);
            AppendLog('PARTICLES', `${Effect.Name}: Colour driven by`, String(V), 'live');
            ConstructInspectorPage();
        }
    };
    C.SelectRow(Wrap, ByBinding);
    const Span = Math.max(1e-3, (Mode ? Mode.defaultMax : 1) - (Mode ? Mode.defaultMin : 0));
    const Lo = (Mode ? Mode.defaultMin : 0) - Span;
    const Hi = (Mode ? Mode.defaultMax : 1) + Span;
    const Step = Span / 100;
    Range(Wrap, BindEffect(Effect, P, 'colorMin', { Label: `Ramp input minimum${Mode && Mode.unit ? ` (${Mode.unit})` : ''}` }), Lo, Hi, Step, 2, Mode && Mode.unit ? Mode.unit : '');
    Range(Wrap, BindEffect(Effect, P, 'colorMax', { Label: `Ramp input maximum${Mode && Mode.unit ? ` (${Mode.unit})` : ''}` }), Lo, Hi, Step, 2, Mode && Mode.unit ? Mode.unit : '');
    Range(Wrap, BindEffect(Effect, P, 'colorRepeat', { Label: 'Ramp repeat count' }), 0.05, 8, 0.05, 2, '×');
    C.SelectRow(Wrap, BindEffect(Effect, P, 'colorWrap', { Label: 'Ramp wrap mode', Options: PE.COLOR_WRAP_MODES || [['Clamp', 'clamp'], ['Repeat', 'repeat'], ['Mirror', 'mirror']] }));
    Range(Wrap, BindEffect(Effect, P, 'colorPhase', { Label: 'Ramp phase offset' }), -1, 1, 0.01, 2, '');
    C.SegmentedRow(Wrap, BindEffect(Effect, P, 'colorInterp', { Label: 'Ramp interpolation', Options: PE.COLOR_INTERP_MODES || [['Linear', 'linear'], ['Smooth', 'smooth'], ['Stepped', 'stepped']] }));
}

function ConstructEffectInspector(Page, Effect)
{
    const P = Effect.P;
    const Preset = PE.presetById(Effect.PresetId);
    const Cls = ClassifyEffect(P);
    PE.Edits.For(Effect);

    const TabDefs = [
        ['overview', 'Overview'],
        ['source', 'Source'],
        ['motion', 'Motion & FX'],
        ['readback', 'Modules & Record']
    ];
    RenderInspectorTabs(TabDefs);

    const DynModeLabels = ['Off', 'Cables & Chains', 'Hanging Vines & Ivy', 'Cloth Flag / Sail', 'Grass & Wheat', 'Reeds & Cattails', 'Broad Ferns', 'Canopy Tree', 'Giant Kelp Forest', 'Sea Anemone Tentacles'];
    const AddDynSection = (HostPage, Badge = '02 / RIGGING & FLORA') =>
    {
        const Sec = C.SectionCard(HostPage, { Title: 'Dynamic rigging & flora', Glyph: Cls.DynMode <= 3 ? 'Cloth' : 'Leaf', Badge }).Inner;
        const ModeB = BindEffect(Effect, P, 'dynMode', {
            Label: 'Dynamic architecture', Rebuild: true,
            Options: [[DynModeLabels[1], 1], [DynModeLabels[2], 2], [DynModeLabels[3], 3], [DynModeLabels[4], 4], [DynModeLabels[5], 5], [DynModeLabels[6], 6], [DynModeLabels[7], 7], [DynModeLabels[8], 8], [DynModeLabels[9], 9]]
        });
        const OrigDM = ModeB.Write;
        ModeB.Write = V => { OrigDM(Number(V)); ConstructInspectorPage(); };
        C.SelectRow(Sec, ModeB);
        if (Cls.DynMode === 1 || Cls.DynMode === 3 || Cls.DynMode === 4)
        {
            const SubLabels = Cls.DynMode === 1
                ? [['Braided Steel Cables', 0], ['Metallic Iron Chains', 1]]
                : Cls.DynMode === 3
                    ? [['Striped Banner Flag', 0], ['Pinned Ship Sail', 1]]
                    : [['Emerald Grass Meadow', 0], ['Golden Plumed Wheat', 1]];
            C.SelectRow(Sec, BindEffect(Effect, P, 'dynSub', { Label: 'Style variant', Options: SubLabels }));
        }
        Range(Sec, BindEffect(Effect, P, 'dynRows', { Label: Cls.DynMode === 3 ? 'Cloth vertical strips' : (Cls.DynMode === 7 ? 'Woody canopy limbs' : (Cls.DynMode === 8 ? 'Kelp stipes (curve count)' : (Cls.DynMode === 9 ? 'Anemone tentacles (curve count)' : 'Strands / cables count'))) }), 4, 48, 1, 0, '');
        if (Cls.DynMode !== 3)
        {
            Range(Sec, BindEffect(Effect, P, 'dynSegs', { Label: 'Curve resolution (segments per curve)' }), 2, 32, 1, 0, 'segs');
            C.SwitchRow(Sec, BindEffect(Effect, P, 'dynCurveDebug', { Label: 'Show curve debug (nodes & segments)' }));
        }
        if (Cls.DynMode === 9)
        {
            Range(Sec, BindEffect(Effect, P, 'dynAnemThick', { Label: 'Anemone tentacle thickness' }), 0.2, 3.0, 0.05, 2, '×');
        }
        Range(Sec, BindEffect(Effect, P, 'dynHeight', { Label: Cls.DynMode <= 3 ? 'Cloth height / drop span' : 'Foliage / frond height' }), 0.4, 5.5, 0.05, 2, 'm');
        Range(Sec, BindEffect(Effect, P, 'dynSag', { Label: Cls.DynMode === 6 ? 'Frond tip arch sag' : 'Gravity catenary sag' }), 0.0, 1.4, 0.02, 2, 'm');
        Range(Sec, BindEffect(Effect, P, 'dynStiff', { Label: 'Elastic stiffness' }), 0.2, 3.0, 0.05, 2, '×');
        Range(Sec, BindEffect(Effect, P, 'dynWaveSpeed', { Label: Cls.DynMode >= 8 ? 'Underwater current speed' : 'Wind wave / flutter speed' }), 0.2, 6.0, 0.05, 2, 'Hz');
        Range(Sec, BindEffect(Effect, P, 'dynTearThresh', { Label: 'Impact tear speed (0 = unbreakable)' }), 0.0, 18.0, 0.2, 1, 'm/s');
    };

    const MudModeLabels = ['Off', 'Rain Puddle Ripples', 'Rally Car Mud Splashes', 'Heavy Mud Splatter'];
    const AddMudSection = (HostPage, Badge = '02 / PUDDLE & SPLASH') =>
    {
        const Sec = C.SectionCard(HostPage, {
            Title: 'Fluid-coupled mud rooster-tails, crowns & puddle waves', Glyph: Cls.MudMode === 2 ? 'Car' : 'Drop', Badge,
            Hint: 'Shallow-water puddle waves + compact fine fluid ligaments, ragged mud micro-clods & mist spray (zero oversized cartoon drops).'
        }).Inner;
        const MMB = BindEffect(Effect, P, 'mudSplashMode', {
            Label: 'Splash system mode', Rebuild: true,
            Options: [[MudModeLabels[1], 1], [MudModeLabels[2], 2], [MudModeLabels[3], 3]]
        });
        const OrigMM = MMB.Write;
        MMB.Write = V => { OrigMM(Number(V)); ConstructInspectorPage(); };
        C.SelectRow(Sec, MMB);
        if (Cls.MudMode === 2)
        {
            Range(Sec, BindEffect(Effect, P, 'carSpeed', { Label: 'Rally car drive speed' }), 0.4, 7.5, 0.1, 2, 'rad/s');
            Range(Sec, BindEffect(Effect, P, 'carTrackRadius', { Label: 'Figure-8 track radius' }), 1.2, 5.5, 0.1, 2, 'm');
        }
        Range(Sec, BindEffect(Effect, P, 'radius', { Label: 'Puddle pool radius' }), 0.6, 5.0, 0.05, 2, 'm');
        const Sw = C.Construct('div', 'property-switches');
        Sec.append(Sw);
        C.ToggleTile(Sw, BindEffect(Effect, P, 'showObject', { Label: Cls.MudMode === 2 ? 'Show 3D car & puddle' : 'Show water puddle disc', Glyph: 'Layers' }));
    };

    const FerroModeLabels = ['Off', 'Petri Dish Spikes', 'Floating Magnetic Orb'];
    const AddFerroSection = (HostPage, Badge = '02 / FERROFLUID FLUX') =>
    {
        const Sec = C.SectionCard(HostPage, {
            Title: 'Magnetic Rosensweig ferrofluid spikes', Glyph: 'Magnet', Badge,
            Hint: 'Liquid-chrome Rosensweig cone spikes track the orbiting Neodymium magnet and thrown 3D ball in real time.'
        }).Inner;
        const FMB = BindEffect(Effect, P, 'ferroMode', {
            Label: 'Ferrofluid vessel', Rebuild: true,
            Options: [[FerroModeLabels[1], 1], [FerroModeLabels[2], 2]]
        });
        const OrigFM = FMB.Write;
        FMB.Write = V => { OrigFM(Number(V)); ConstructInspectorPage(); };
        C.SelectRow(Sec, FMB);
        Range(Sec, BindEffect(Effect, P, 'ferroSpikes', { Label: 'Magnetic spike count' }), 32, Math.max(560, P.capacity || 640), 8, 0, '');
        Range(Sec, BindEffect(Effect, P, 'ferroMagnetSpeed', { Label: 'Magnet orbit speed' }), 0.2, 5.0, 0.05, 2, 'rad/s');
        Range(Sec, BindEffect(Effect, P, 'radius', { Label: Cls.FerroMode === 1 ? 'Dish diameter radius' : 'Ferro orb radius' }), 0.8, 4.0, 0.05, 2, 'm');
        Range(Sec, {
            Label: 'Max spike height (Y)',
            Read: () => Array.isArray(P.boxHalf) ? (P.boxHalf[1] ?? 1.05) : (Number(P.boxHalf) || 1.05),
            Write: PE.Edits.Through('Max spike height', V => {
                if (!Array.isArray(P.boxHalf)) P.boxHalf = [2.0, Number(V), 2.0];
                else P.boxHalf[1] = Number(V);
                AppendLog('PARTICLES', `${Effect.Name}: Max spike height`, `${Number(V).toFixed(2)} m`, 'live');
            })
        }, 0.2, 1.8, 0.02, 2, 'm');
        const FreeMagB = BindEffect(Effect, P, 'ferroMagnetFree', { Label: 'Manual magnet position (drag pole in 3D)' });
        const OrigFreeMagW = FreeMagB.Write;
        FreeMagB.Write = V => {
            OrigFreeMagW(!!V);
            if (V && !Array.isArray(P.effPos)) P.effPos = [P.origin[0] + 0.8, P.origin[1] + 0.65, P.origin[2]];
            ConstructInspectorPage();
        };
        C.SwitchRow(Sec, FreeMagB);
        if (P.ferroMagnetFree)
        {
            if (!Array.isArray(P.effPos)) P.effPos = [P.origin[0] + 0.8, P.origin[1] + 0.65, P.origin[2]];
            C.VectorRow(Sec, BindEffect(Effect, P, 'effPos', { Label: 'Magnet pole XYZ (drag in 3D)', Unit: 'm', Step: 0.05, Digits: 2, Minimum: -10, Maximum: 10 }));
        }
        const Sw = C.Construct('div', 'property-switches');
        Sec.append(Sw);
        C.ToggleTile(Sw, BindEffect(Effect, P, 'showObject', { Label: 'Show dish & magnet', Glyph: 'Layers' }));
    };

    const AddFireworkSection = (HostPage, Badge = '02 / PYROTECHNIC SPECTACLE') =>
    {
        const Sec = C.SectionCard(HostPage, {
            Title: 'Fireworks shells, sky text & New Year\'s shapes', Glyph: 'Spark', Badge,
            Hint: 'Choose classic Hanabi shells, 3D objects (Heart, Saturn, Star, Smiley, Crown, Butterfly), or type custom Sky Text. Any star touching the floor or an object pops into a quick flare and vanishes instantly.'
        }).Inner;
        const ShapeBind = BindEffect(Effect, P, 'fireworkShape', {
            Label: 'Firework pattern / spectacle',
            Options: FIREWORK_SHAPES.map(S => [S.label, S.id])
        });
        const OrigSw = ShapeBind.Write;
        ShapeBind.Write = V => {
            OrigSw(Number(V));
            Effect._fwSnapCache = null;
            ConstructInspectorPage();
        };
        C.SelectRow(Sec, ShapeBind);

        // Live editable custom Sky Text input + quick New Year's preset chips!
        const TextRow = C.Construct('div', 'property-row');
        TextRow.style.cssText = 'display:flex;flex-direction:column;gap:6px;margin:8px 0 6px;';
        const LabelEl = C.Construct('label', 'property-label');
        LabelEl.textContent = 'Sky-writing firework text (used in Sky Text & Grand Finale)';
        LabelEl.style.cssText = 'font-size:11px;color:var(--text-muted,#a8b8d0);';
        const InputEl = document.createElement('input');
        InputEl.type = 'text';
        InputEl.maxLength = 28;
        InputEl.value = String(P.fireworkText || '2026');
        InputEl.placeholder = 'e.g. 2026, HAPPY NEW YEAR, BOOM...';
        InputEl.style.cssText = 'width:100%;box-sizing:border-box;padding:7px 10px;border-radius:7px;border:1px solid rgba(130,170,220,0.28);background:rgba(12,18,28,0.86);color:#f2f7ff;font-size:12px;font-weight:600;letter-spacing:0.06em;';
        InputEl.addEventListener('input', () => {
            P.fireworkText = InputEl.value || '2026';
            Effect._fwSnapCache = null;
            if ((P.fireworkShape | 0) !== 1 && (P.fireworkShape | 0) !== 9)
                P.fireworkShape = 1;
        });
        const ChipBar = C.Construct('div');
        ChipBar.style.cssText = 'display:flex;flex-wrap:wrap;gap:5px;';
        for (const Word of ['2026', 'HAPPY NEW YEAR', 'BOOM', 'LOVE', 'ARENA'])
        {
            const Btn = document.createElement('button');
            Btn.type = 'button';
            Btn.textContent = Word;
            Btn.style.cssText = 'padding:3px 8px;border-radius:999px;border:1px solid rgba(255,210,110,0.34);background:rgba(255,195,70,0.12);color:#ffe49c;font-size:10px;font-weight:600;cursor:pointer;';
            Btn.addEventListener('click', () => {
                P.fireworkText = Word;
                InputEl.value = Word;
                if ((P.fireworkShape | 0) !== 1 && (P.fireworkShape | 0) !== 9) P.fireworkShape = 1;
                Effect._fwSnapCache = null;
                C.Toast(`Firework text set to "${Word}"`);
            });
            ChipBar.append(Btn);
        }
        TextRow.append(LabelEl, InputEl, ChipBar);
        Sec.append(TextRow);

        Range(Sec, BindEffect(Effect, P, 'fireworkScale', { Label: 'Burst pattern scale' }), 1.2, 4.6, 0.05, 2, 'm');
        Range(Sec, BindEffect(Effect, P, 'burstEvery', { Label: 'Shell launch interval' }), 2.2, 6.0, 0.1, 1, 's');
        Range(Sec, BindEffect(Effect, P, 'gravity', { Label: 'Willow star droop gravity' }), 0.10, 1.2, 0.02, 2, 'g');
        const Sw = C.Construct('div', 'property-switches');
        Sec.append(Sw);
        C.ToggleTile(Sw, BindEffect(Effect, P, 'showObject', { Label: 'Show ground 5-tube mortar rack', Glyph: 'Layers' }));
    };

    const AddFlareSection = (HostPage, Badge = '02 / IR COUNTERMEASURE SALVO') =>
    {
        const Sec = C.SectionCard(HostPage, {
            Title: 'Military IR decoy flare V-salvo & smoke plumes', Glyph: 'Flame', Badge,
            Hint: 'Twin-pod aircraft dispenser fires alternating left/right magnesium flares that decelerate steeply, curl thick billowing smoke plumes in the wind, and shower molten slag sparks.'
        }).Inner;
        Range(Sec, BindEffect(Effect, P, 'burstEvery', { Label: 'Salvo burst duration' }), 1.2, 5.0, 0.1, 1, 's');
        Range(Sec, BindEffect(Effect, P, 'gravity', { Label: 'Flare drop gravity' }), 0.2, 1.4, 0.02, 2, 'g');
        Range(Sec, BindEffect(Effect, P, 'drag', { Label: 'Aerodynamic braking drag' }), 0.4, 3.0, 0.05, 2, '/s');
        Range(Sec, BindEffect(Effect, P, 'buoyancy', { Label: 'Smoke plume thermal rise' }), 0.0, 1.2, 0.05, 2, 'm/s');
        const Sw = C.Construct('div', 'property-switches');
        Sec.append(Sw);
        C.ToggleTile(Sw, BindEffect(Effect, P, 'showObject', { Label: 'Show aircraft twin-pod mount', Glyph: 'Layers' }));
    };

    const CrackModeLabels = ['Off', 'Glacial Ice Sheet', 'Tectonic Magma Crust'];
    const AddCrackSection = (HostPage, Badge = '02 / GROUND FRACTURE') =>
    {
        const Sec = C.SectionCard(HostPage, {
            Title: 'Voronoi ground fracture & seismic slabs', Glyph: 'Zap', Badge,
            Hint: 'Click "Fracture at Center" or throw the 3D ball anywhere onto the crust to radiate cracks, tilt beveled slabs, and blast splinters.'
        }).Inner;
        const CMB = BindEffect(Effect, P, 'crackMode', {
            Label: 'Crust material', Rebuild: true,
            Options: [[CrackModeLabels[1], 1], [CrackModeLabels[2], 2]]
        });
        const OrigCM = CMB.Write;
        CMB.Write = V => { OrigCM(Number(V)); ConstructInspectorPage(); };
        C.SelectRow(Sec, CMB);
        C.ActionRow(Sec, [{
            Label: 'Fracture at Center', Glyph: 'Zap', Tone: 'accent',
            Act: () => { Effect.CrackHit = { x: P.origin[0], z: P.origin[2], t: Scene.Time }; C.Toast('Ground fractured!'); }
        }]);
        Range(Sec, BindEffect(Effect, P, 'crackSpeed', { Label: 'Crack propagation speed' }), 0.3, 4.0, 0.05, 2, '×');
        Range(Sec, BindEffect(Effect, P, 'crackDepth', { Label: 'Slab heave & tilt angle' }), 0.05, 1.2, 0.02, 2, 'm');
        const Sw = C.Construct('div', 'property-switches');
        Sec.append(Sw);
        C.ToggleTile(Sw, BindEffect(Effect, P, 'showObject', { Label: 'Show abyssal glow underlayer', Glyph: 'Layers' }));
    };

    const BeadsModeLabels = ['Off', 'Crystal Bead Curtain', 'Bamboo & Brass Wind Chimes', 'Velvet Doorway Drapes'];
    const AddBeadsSection = (HostPage, Badge = '02 / HANGING CURTAINS & CHIMES') =>
    {
        const Sec = C.SectionCard(HostPage, {
            Title: 'Bead curtains, wind chimes & doorway drapes', Glyph: 'Cloth', Badge,
            Hint: 'Elastic hanging strands & tuned tubes swing in indoor drafts and part cleanly with impact sparkles when struck by thrown balls.'
        }).Inner;
        const BMB = BindEffect(Effect, P, 'beadsMode', {
            Label: 'Hanging structure style', Rebuild: true,
            Options: [[BeadsModeLabels[1], 1], [BeadsModeLabels[2], 2], [BeadsModeLabels[3], 3]]
        });
        const OrigBM = BMB.Write;
        BMB.Write = V => { OrigBM(Number(V)); ConstructInspectorPage(); };
        C.SelectRow(Sec, BMB);
        Range(Sec, BindEffect(Effect, P, 'dynRows', { Label: Cls.BeadsMode === 2 ? 'Tuned chime tubes' : 'Hanging strands / drape strips' }), 6, 42, 1, 0, '');
        Range(Sec, BindEffect(Effect, P, 'dynHeight', { Label: 'Doorway / hanger drop span' }), 0.85, 5.2, 0.05, 2, 'm');
        Range(Sec, BindEffect(Effect, P, 'dynStiff', { Label: 'Restoring stiffness' }), 0.25, 3.0, 0.05, 2, '×');
        Range(Sec, BindEffect(Effect, P, 'dynWaveSpeed', { Label: 'Sway resonance speed' }), 0.35, 5.5, 0.05, 2, 'Hz');
        const Sw = C.Construct('div', 'property-switches');
        Sec.append(Sw);
        C.ToggleTile(Sw, BindEffect(Effect, P, 'showObject', { Label: Cls.BeadsMode === 2 ? 'Show crown striker ring' : 'Show doorway lintel frame', Glyph: 'Layers' }));
    };

    const SeedHeadModeLabels = ['Off', 'Dandelion Puff Wish Seeds', 'Spring Blossom Boughs'];
    const AddSeedHeadSection = (HostPage, Badge = '02 / SEED HEADS & BLOSSOMS') =>
    {
        const Sec = C.SectionCard(HostPage, {
            Title: 'Dandelion seed puffs & blossom detachment', Glyph: 'Leaf', Badge,
            Hint: 'Radial pappus seeds & cherry blossoms cling to swaying meadow stems / boughs until gust thresholds or passing 3D balls shake them loose.'
        }).Inner;
        const SMB = BindEffect(Effect, P, 'seedHeadMode', {
            Label: 'Botanical seed assembly', Rebuild: true,
            Options: [[SeedHeadModeLabels[1], 1], [SeedHeadModeLabels[2], 2]]
        });
        const OrigSM = SMB.Write;
        SMB.Write = V => { OrigSM(Number(V)); ConstructInspectorPage(); };
        C.SelectRow(Sec, SMB);
        Range(Sec, BindEffect(Effect, P, 'dynRows', { Label: Cls.SeedHeadMode === 1 ? 'Meadow stalks' : 'Orchard boughs' }), 2, 18, 1, 0, '');
        Range(Sec, BindEffect(Effect, P, 'dynHeight', { Label: 'Stalk / canopy height' }), 0.85, 4.2, 0.05, 2, 'm');
        Range(Sec, BindEffect(Effect, P, 'radius', { Label: Cls.SeedHeadMode === 1 ? 'Seed puff head radius' : 'Bough cluster spread' }), 0.22, 1.4, 0.02, 2, 'm');
        Range(Sec, BindEffect(Effect, P, 'dynTearThresh', { Label: 'Detachment wind threshold' }), 0.4, 12.0, 0.1, 1, 'm/s');
        Range(Sec, BindEffect(Effect, P, 'dynWaveSpeed', { Label: 'Stalk sway frequency' }), 0.3, 4.5, 0.05, 2, 'Hz');
    };

    const JellyModeLabels = ['Off', 'Soft-Body Jelly Cube', 'Elastic Slime Cluster'];
    const AddJellySection = (HostPage, Badge = '02 / SOFT-BODY JELLY') =>
    {
        const Sec = C.SectionCard(HostPage, {
            Title: 'Volume-preserving jelly & sticky slime tendrils', Glyph: 'Drop', Badge,
            Hint: 'Bouncy viscoelastic jelly preserves volume during floor squash-and-stretch while sticky tendrils reach onto passing 3D balls.'
        }).Inner;
        const JMB = BindEffect(Effect, P, 'jellyMode', {
            Label: 'Soft-body morphology', Rebuild: true,
            Options: [[JellyModeLabels[1], 1], [JellyModeLabels[2], 2]]
        });
        const OrigJM = JMB.Write;
        JMB.Write = V => { OrigJM(Number(V)); ConstructInspectorPage(); };
        C.SelectRow(Sec, JMB);
        Range(Sec, BindEffect(Effect, P, 'radius', { Label: 'Body core radius' }), 0.4, 2.6, 0.05, 2, 'm');
        Range(Sec, BindEffect(Effect, P, 'bounce', { Label: 'Bounce & squash height' }), 0.2, 0.95, 0.02, 2, '×');
        Range(Sec, BindEffect(Effect, P, 'dynWaveSpeed', { Label: 'Gelatin wobble frequency' }), 1.0, 9.5, 0.1, 1, 'Hz');
        Range(Sec, BindEffect(Effect, P, 'dynTearThresh', { Label: 'Sticky tendril pull reach' }), 1.0, 10.0, 0.1, 1, 'm');
    };

    const AddBallInteractionCard = (HostPage, Badge = '03 / INTERACTIVE BALL', ShowTear = true) =>
    {
        const BallSec = C.SectionCard(HostPage, { Title: 'Interactive 3D ball & impacts', Glyph: 'Ball', Badge, Hint: 'Press T (or Alt+Click in viewport) to throw spheres through cloth, spiderwebs, shields, effector grids, or trees.' }).Inner;
        C.ActionRow(BallSec, [
            {
                Label: 'Throw Ball at Effect', Glyph: 'Ball', Tone: 'accent',
                Act: () => { ThrowBallAtTarget(P.origin, Scene.Ball.ThrowSpeed); C.Toast('Ball thrown!'); }
            },
            {
                Label: 'Drop From Above', Glyph: 'Gravity',
                Act: () =>
                {
                    Scene.Ball.Pos = [P.origin[0], P.origin[1] + 3.2, P.origin[2]];
                    Scene.Ball.Vel = [0, -5.5, 0];
                    Scene.Ball.Active = true;
                    C.Toast('Dropped ball onto effect');
                }
            },
            {
                Label: 'Clear Ball', Glyph: 'Close',
                Act: () => { Scene.Ball.Active = false; }
            }
        ]);
        Range(BallSec, { Label: 'Ball radius', Read: () => Scene.Ball.Radius, Write: V => { Scene.Ball.Radius = Number(V); } }, 0.15, 1.2, 0.02, 2, 'm');
        Range(BallSec, { Label: 'Throw velocity', Read: () => Scene.Ball.ThrowSpeed, Write: V => { Scene.Ball.ThrowSpeed = Number(V); } }, 3.0, 22.0, 0.25, 1, 'm/s');
        if (ShowTear && (Cls.IsWeb || (Cls.IsDyn && InspectorTab === 'overview')))
            Range(BallSec, BindEffect(Effect, P, 'dynTearThresh', { Label: 'Strand / cloth tear speed (0 = tough)' }), 0.0, 18.0, 0.2, 1, 'm/s');
    };

    const GridSurfLabels = OBJECT_SURFACES.map(S => S.label);
    const GridShapeLabels = ['Halftone Circles', '3D Uniform Boxes', '3D Height Cubes (Up/Down)', 'Flat Square Tiles', 'Triangles', 'Hexagons', 'Auto Mixed (Dots + Cubes)'];
    const TriggerCustomMeshImport = (TargetEffect) =>
    {
        const Input = document.createElement('input');
        Input.type = 'file';
        Input.accept = '.obj,.stl,.json,.facet';
        Input.addEventListener('change', async () =>
        {
            const File = Input.files?.[0];
            if (!File) return;
            try
            {
                const Buf = await File.arrayBuffer();
                const Mesh = ParseCustomMeshFile(File.name, Buf);
                if (!Mesh || !Mesh.tris.length)
                {
                    C.Toast('No valid 3D triangles found in ' + File.name);
                    return;
                }
                const MeshId = RegisterCustomMesh(Mesh, File.name);
                TargetEffect.P.customMeshId = MeshId;
                TargetEffect.P.customMeshName = File.name;
                TargetEffect.P.gridSurface = 16;
                if ((TargetEffect.P.radius || 0) < 0.8) TargetEffect.P.radius = 1.5;
                AppendLog('PARTICLES', `${TargetEffect.Name}: imported 3D mesh ${File.name}`, `${Mesh.tris.length.toLocaleString()} triangles`, 'live');
                C.Toast(`Placed ${File.name} (${Mesh.tris.length.toLocaleString()} tris)`);
                ConstructInspectorPage();
                C.Broadcast();
            }
            catch (Err)
            {
                C.Toast('Mesh import error: ' + (Err?.message || Err));
            }
        });
        Input.click();
    };

    const AddGridSection = (HostPage, Badge = '02 / OBJECT & EFFECTOR GRID') =>
    {
        const CurSurf = Math.max(0, Math.min(16, Math.round(P.gridSurface ?? 0)));
        const Sec = C.SectionCard(HostPage, {
            Title: '3D Object Placer & Effector Grid', Glyph: 'Boxes', Badge,
            Hint: 'Place any built-in 3D object or import an OBJ/STL/JSON mesh, drag the object & effectors directly in 3D (hold Shift for Y).'
        }).Inner;
        const SurfB = BindEffect(Effect, P, 'gridSurface', {
            Label: 'Placed 3D object / surface',
            Options: OBJECT_SURFACES.map(S => [S.label, S.id])
        });
        const OrigSW = SurfB.Write;
        SurfB.Write = V => { OrigSW(Number(V)); ConstructInspectorPage(); };
        C.SelectRow(Sec, SurfB);

        const CustomEntry = GetCustomMeshEntry(P.customMeshId);
        C.ActionRow(Sec, [
            {
                Label: CustomEntry ? `Replace Mesh (${CustomEntry.name})` : 'Import 3D Mesh (.obj / .stl)', Glyph: 'Layers', Tone: 'accent',
                Act: () => TriggerCustomMeshImport(Effect)
            },
            {
                Label: (P.gridEffector || 0) === 5 ? 'Effectors Active' : 'Use Placeable Effectors', Glyph: 'Target',
                Act: () => {
                    P.gridEffector = 5;
                    if (!Array.isArray(P.effPos)) P.effPos = [P.origin[0] + 0.8, P.origin[1] + 0.3, P.origin[2] + 0.4];
                    if ((P.effSize ?? 0) < 0.25) P.effSize = 1.15;
                    ConstructInspectorPage();
                    C.Broadcast();
                    C.Toast('Placeable Effectors active — drag EFF A / EFF B in viewport');
                }
            }
        ]);

        if (CurSurf >= 6)
        {
            if (CurSurf === 16)
            {
                const InfoText = CustomEntry
                    ? `Loaded custom mesh: ${CustomEntry.name} (${CustomEntry.triCount.toLocaleString()} triangles · auto-normalized)`
                    : 'No custom file uploaded yet (showing Utah Teapot fallback). Click "Import 3D Mesh" or drop an .obj/.stl file onto the viewport.';
                Sec.append(C.Construct('p', 'property-hint', InfoText));
            }
            if (!Array.isArray(P.objRot)) P.objRot = [0, 0, 0];
            Range(Sec, BindEffect(Effect, P, 'radius', { Label: 'Object scale radius' }), 0.4, 5.0, 0.05, 2, 'm');
            C.VectorRow(Sec, BindEffect(Effect, P, 'objRot', { Label: 'Object rotation (X, Y, Z deg)', Unit: '°', Step: 2, Digits: 0, Minimum: -180, Maximum: 180 }));
            C.SelectRow(Sec, BindEffect(Effect, P, 'objStyle', { Label: 'Backing object material', Options: OBJECT_STYLES }));
            C.SelectRow(Sec, BindEffect(Effect, P, 'objSampleMode', { Label: 'Particle anchor distribution', Options: OBJECT_SAMPLE_MODES }));
        }

        const ShapeB = BindEffect(Effect, P, 'gridShape', {
            Label: 'Particle shape',
            Options: GridShapeLabels.map((L, K) => [L, K])
        });
        const OrigGSW = ShapeB.Write;
        ShapeB.Write = V => { OrigGSW(Number(V)); ConstructInspectorPage(); };
        C.SelectRow(Sec, ShapeB);

        const EffDriverB = BindEffect(Effect, P, 'gridEffector', {
            Label: 'Effector driver',
            Options: [['Fluid Simplex Wave', 0], ['Orbiting Dual Effectors', 1], ['Animated Curl Islands', 2], ['Expanding Pulse Ring', 3], ['Ball & ForceFields Only', 4], ['Placeable Effectors (drag in 3D)', 5]]
        });
        const OrigEDW = EffDriverB.Write;
        EffDriverB.Write = V => {
            OrigEDW(Number(V));
            if (Number(V) === 5 && (P.effSize ?? 0) < 0.2) P.effSize = 1.15;
            ConstructInspectorPage();
        };
        C.SelectRow(Sec, EffDriverB);

        if (CurSurf <= 1 || CurSurf === 4)
        {
            Range(Sec, BindEffect(Effect, P, 'gridCols', { Label: 'Grid columns (planar/torus)' }), 4, 64, 1, 0, '');
            if (CurSurf <= 1) Range(Sec, BindEffect(Effect, P, 'gridRows', { Label: 'Grid rows (planar)' }), 4, 64, 1, 0, '');
        }
        Range(Sec, BindEffect(Effect, P, 'curlFreq', { Label: 'Effector wave frequency' }), 0.2, 3.5, 0.05, 2, '1/m');
        Range(Sec, BindEffect(Effect, P, 'curlAmp', { Label: 'Effector motion speed' }), 0.0, 2.5, 0.05, 2, '×');
        Range(Sec, BindEffect(Effect, P, 'gridMinScale', { Label: 'Inactive scale / base height' }), 0.0, 0.90, 0.02, 2, '×');
        Range(Sec, BindEffect(Effect, P, 'gridMaxHeight', { Label: 'Active height extrusion (cubes)' }), 0.05, 2.8, 0.02, 2, 'm');
        Range(Sec, BindEffect(Effect, P, 'gridContrast', { Label: 'Mask contrast sharpness' }), 0.5, 3.5, 0.05, 2, '');
        const GridSw = C.Construct('div', 'property-switches');
        Sec.append(GridSw);
        C.ToggleTile(GridSw, BindEffect(Effect, P, 'showObject', { Label: 'Show backing object', Glyph: 'Layers' }));
        C.ToggleTile(GridSw, {
            Label: 'Billboard to camera', Glyph: 'Target',
            Read: () => (P.gridBillboard || 0) > 0,
            Write: PE.Edits.Through('Billboard dots', On => { P.gridBillboard = On ? 1 : 0; })
        });
        const BlendEffB = {
            Label: 'Stack Effectors A/B', Glyph: 'Orbit',
            Read: () => (P.gridEffector || 0) === 5 || !!P.effBlend,
            Write: PE.Edits.Through('Stack placeable effectors', On => {
                if ((P.gridEffector || 0) === 5 && !On) P.gridEffector = 0;
                P.effBlend = !!On;
                if (On && (P.effSize ?? 0) < 0.2) P.effSize = 1.15;
                ConstructInspectorPage();
            })
        };
        C.ToggleTile(GridSw, BlendEffB);

        if ((P.gridEffector || 0) === 5 || P.effBlend)
        {
            const EffLabels = [['Sphere', 0], ['Box', 1], ['Torus', 2], ['Disc', 3], ['Cone', 4]];
            if (!Array.isArray(P.effPos)) P.effPos = [P.origin[0] + 0.8, P.origin[1] + 0.3, P.origin[2] + 0.4];
            if (!Array.isArray(P.effPos2)) P.effPos2 = [P.origin[0] - 0.9, P.origin[1] - 0.2, P.origin[2] + 0.4];
            C.ActionRow(Sec, [
                {
                    Label: (P.effSize2 || 0) > 0.01 ? 'Hide Effector B' : '+ Enable Effector B', Glyph: 'Plus',
                    Act: () => {
                        P.effSize2 = (P.effSize2 || 0) > 0.01 ? 0 : 0.95;
                        if (P.effSize2 > 0) P.effPos2 = [P.origin[0] - 0.95, P.origin[1], P.origin[2] + 0.45];
                        ConstructInspectorPage();
                        C.Broadcast();
                    }
                },
                {
                    Label: 'Center Effectors', Glyph: 'Reset',
                    Act: () => {
                        P.effPos = [P.origin[0] + 0.85, P.origin[1] + 0.35, P.origin[2] + 0.5];
                        if ((P.effSize2 || 0) > 0.01) P.effPos2 = [P.origin[0] - 0.95, P.origin[1] - 0.2, P.origin[2] + 0.45];
                        C.Broadcast();
                        C.Toast('Effectors centred on object');
                    }
                }
            ]);
            C.VectorRow(Sec, BindEffect(Effect, P, 'effPos', { Label: 'Effector A position (drag in 3D)', Unit: 'm', Step: 0.05, Digits: 2, Minimum: -12, Maximum: 12 }));
            C.SelectRow(Sec, BindEffect(Effect, P, 'effShape', { Label: 'Effector A shape', Options: EffLabels }));
            Range(Sec, BindEffect(Effect, P, 'effSize', { Label: 'Effector A radius' }), 0.0, 4.0, 0.05, 2, 'm');
            if ((P.effSize2 || 0) > 0.01)
            {
                C.VectorRow(Sec, BindEffect(Effect, P, 'effPos2', { Label: 'Effector B position (drag in 3D)', Unit: 'm', Step: 0.05, Digits: 2, Minimum: -12, Maximum: 12 }));
                C.SelectRow(Sec, BindEffect(Effect, P, 'effShape2', { Label: 'Effector B shape', Options: EffLabels }));
                Range(Sec, BindEffect(Effect, P, 'effSize2', { Label: 'Effector B radius (0 = off)' }), 0.0, 4.0, 0.05, 2, 'm');
            }
        }
    };

    const ShieldFormLabels = ['Sphere Dome', 'Circle Disc', 'Triangle Aegis', 'Square Pane', 'Hexagon Wall', 'Diamond Barrier'];
    const AddShieldSection = (HostPage, Badge = '02 / SHIELD BARRIER') =>
    {
        const Sec = C.SectionCard(HostPage, {
            Title: 'Shield silhouette & pattern layers', Glyph: 'Orbit', Badge,
            Hint: 'Pick the outer barrier form and toggle any combination of geometric lattice shapes inside the shield.'
        }).Inner;
        const FormB = BindEffect(Effect, P, 'shieldForm', {
            Label: 'Barrier silhouette',
            Options: ShieldFormLabels.map((L, K) => [L, K])
        });
        const OrigFW = FormB.Write;
        FormB.Write = V => { OrigFW(Number(V)); ConstructInspectorPage(); };
        C.SelectRow(Sec, FormB);
        if ((P.shieldForm ?? 0) >= 1)
            C.VectorRow(Sec, BindEffect(Effect, P, 'dir', { Label: 'Barrier facing normal', Step: 0.01, Digits: 2, Minimum: -1, Maximum: 1 }));
        Sec.append(C.Construct('div', 'section-subhead', [C.Construct('span', null, 'Layered pattern shapes'), C.Construct('span', null, 'Stackable')]));
        const MaskGrid = C.Construct('div', 'property-switches');
        Sec.append(MaskGrid);
        for (const [Label, Bit, Glyph] of [
            ['Hexagons', 1, 'Lattice'],
            ['Circles & Dots', 2, 'Orbit'],
            ['Triangles', 4, 'Rays'],
            ['Squares', 8, 'Boxes'],
            ['Diamonds', 16, 'Sparkles'],
            ['Voronoi Cracks', 32, 'Zap'],
            ['Stars', 64, 'Target']
        ])
        {
            C.ToggleTile(MaskGrid, {
                Label, Glyph,
                Read: () => (((P.shieldMask ?? 1) & Bit) !== 0),
                Write: PE.Edits.Through(`Shield ${Label}`, On =>
                {
                    let Next = P.shieldMask ?? 1;
                    Next = On ? (Next | Bit) : (Next & ~Bit);
                    P.shieldMask = Next === 0 ? Bit : Next;
                })
            });
        }
        Range(Sec, BindEffect(Effect, P, 'curlFreq', { Label: 'Pattern lattice density' }), 0.5, 5.0, 0.05, 2, '×');
        Range(Sec, BindEffect(Effect, P, 'shieldSpin', { Label: 'Pattern rotation speed' }), -2.0, 2.0, 0.05, 2, 'rad/s');
        Range(Sec, BindEffect(Effect, P, 'curlRate', { Label: 'Impact ripple cadence' }), 0.05, 2.0, 0.05, 2, 'Hz');
    };

    if (InspectorTab === 'overview')
    {
        const Mark = C.Construct('div', 'mark live', C.ConstructGlyph('Check'));
        const MarkText = C.Construct('span');
        const Metric = C.Construct('div', 'metric-big');
        const Rate = C.Construct('div', 'rate');
        const Low = C.Construct('div', 'low');
        const Card = C.Construct('div', 'hero-card', [
            C.Construct('div', 'hero-top', [
                C.Construct('div', 'budget-chip', [Mark, MarkText]),
                C.Construct('div', 'estimate-tag', '01 / LIVE SOLVER')
            ]),
            C.Construct('div', 'hero-title', 'Particles alive'),
            C.Construct('div', 'hero-metrics', [Metric, C.Construct('div', 'metric-side', [Rate, Low])])
        ]);
        Page.append(Card);

        if (Scene.ActiveInfographic?.Destroy) Scene.ActiveInfographic.Destroy();
        const Infographic = C.ParticleInfographic(Card, Effect, Cls, {
            Height: 152,
            OnChange: () => { C.Broadcast(); },
            OnBurst: () => {
                if (Cls.IsArc) TriggerArcStrike(Effect, Scene.Time);
                else Effect.PendingBurst += Math.max(48, Math.round((Effect.Gpu?.cap || 120) * 0.35));
            },
            OnReset: () => { RebuildGpu(Effect); C.Toast(`Replayed ${Effect.Name}`); },
            OnCrackStrike: (X, Z) => {
                Effect.CrackHit = { x: X, z: Z, t: Scene.Time };
                C.Toast('Fracture shockwave triggered!');
            }
        });
        Scene.ActiveInfographic = { Token: Effect.Token, Refresh: Infographic.Refresh, Destroy: Infographic.Destroy };
        LiveRefreshers.push(() => Infographic.Refresh({
            alive: Effect.Gpu?.stats_?.alive ?? 0,
            cap: Effect.Gpu?.cap || P.capacity || 1,
            time: Scene.Time
        }));
        LiveRefreshers.push(() =>
        {
            const Alive = Effect.Gpu?.stats_?.alive ?? 0;
            Metric.textContent = FormatInteger(Alive);
            if (Cls.IsTransition || Cls.IsWeb || Cls.IsDyn || Cls.IsGrid || Cls.IsFerro || Cls.IsCrack || Cls.IsBeads || Cls.IsSeedHead || Cls.IsJelly || Cls.IsFirework || Cls.IsFlare)
            {
                Rate.textContent = Cls.IsFirework ? 'Pyrotechnic show'
                    : Cls.IsFlare ? 'IR V-Salvo'
                        : Cls.IsGrid ? GridShapeLabels[Math.round(P.gridShape ?? 0)]
                    : Cls.IsFerro ? FerroModeLabels[Cls.FerroMode]
                        : Cls.IsCrack ? CrackModeLabels[Cls.CrackMode]
                            : Cls.IsBeads ? BeadsModeLabels[Cls.BeadsMode]
                                : Cls.IsSeedHead ? SeedHeadModeLabels[Cls.SeedHeadMode]
                                    : Cls.IsJelly ? JellyModeLabels[Cls.JellyMode]
                                        : (Cls.IsDyn ? DynModeLabels[Cls.DynMode] : (Cls.IsWeb ? 'Silk strands' : 'Batch cycle'));
            }
            else
            {
                Rate.textContent = FormatInteger(P.rate);
                Rate.append(C.Construct('small', null, '/s'));
            }
            Low.textContent = `of ${FormatInteger(Effect.Gpu?.cap || P.capacity)} cap`;
            const Full = (Alive / Math.max(1, Effect.Gpu?.cap || 1)) >= 0.98;
            Mark.className = 'mark ' + (!P.visible ? 'warm' : Full ? 'warm' : 'live');
            Mark.textContent = '';
            Mark.append(C.ConstructGlyph(!P.visible ? 'EyeOff' : Full ? 'Alert' : 'Check'));
            MarkText.textContent = !P.visible ? 'Hidden' : !Scene.Playing ? 'Paused' : Full ? 'At capacity' : 'Within capacity';
        });

        const SecondTrio = Cls.IsAssembly
            ? { Label: 'Assembly', Resolve: () => [Cls.AsmMode === 1 ? 'Shell rebuild' : 'Column stack', `${(P.transition.holdSpread ?? 0).toFixed(1)} s wave`] }
            : Cls.IsFirework
                ? { Label: 'Firework Show', Resolve: () => [(FIREWORK_SHAPES.find(S => S.id === (P.fireworkShape | 0))?.label || 'Grand Bouquet').split(' (')[0], `${(P.burstEvery ?? 3.1).toFixed(1)} s · "${String(P.fireworkText || '2026').slice(0, 12)}"`] }
                : Cls.IsFlare
                    ? { Label: 'IR Salvo', Resolve: () => ['Twin-Pod Decoys', `${(P.burstEvery ?? 2.6).toFixed(1)} s salvo · curls smoke`] }
                    : Cls.IsGrid
                        ? { Label: 'Placed Object', Resolve: () => [GridSurfLabels[Math.round(P.gridSurface ?? 0)] || 'Grid', `extrude ${(P.gridMaxHeight ?? 0.42).toFixed(2)} m`] }
                        : Cls.IsFerro
                            ? { Label: 'Ferrofluid', Resolve: () => [FerroModeLabels[Cls.FerroMode] || 'Spikes', `${Math.round(P.ferroSpikes ?? 520)} spikes · ${P.ferroMagnetFree ? 'manual pole' : (P.ferroMagnetSpeed ?? 1.35).toFixed(1) + ' rad/s'}`] }
                            : Cls.IsCrack
                                ? { Label: 'Fracture', Resolve: () => [CrackModeLabels[Cls.CrackMode] || 'Crust', `heave ${(P.crackDepth ?? 0.42).toFixed(2)} m · speed ${(P.crackSpeed ?? 1.5).toFixed(1)}×`] }
                                : Cls.IsBeads
                                    ? { Label: 'Hanging Rig', Resolve: () => [BeadsModeLabels[Cls.BeadsMode] || 'Curtain', `${Math.round(P.dynRows ?? 24)} strands · ${(P.dynHeight ?? 3.0).toFixed(1)} m drop`] }
                                    : Cls.IsSeedHead
                                        ? { Label: 'Seed Dispersal', Resolve: () => [SeedHeadModeLabels[Cls.SeedHeadMode] || 'Dandelion', `detach > ${(P.dynTearThresh ?? 3.0).toFixed(1)} m/s`] }
                                        : Cls.IsJelly
                                            ? { Label: 'Soft Body', Resolve: () => [JellyModeLabels[Cls.JellyMode] || 'Jelly', `wobble ${(P.dynWaveSpeed ?? 4.5).toFixed(1)} Hz · tendril ${(P.dynTearThresh ?? 5.0).toFixed(1)} m`] }
                                            : Cls.IsMud
                                                ? { Label: 'Mud & Puddle', Resolve: () => [MudModeLabels[Cls.MudMode] || 'Splashes', P.mudSplashMode === 2 ? `car ${(P.carSpeed ?? 2.8).toFixed(1)} rad/s` : `pool ${(P.radius ?? 2.8).toFixed(1)} m`] }
                                                : Cls.IsDyn
                                                    ? { Label: 'Rigging / Flora', Resolve: () => [DynModeLabels[Cls.DynMode] || 'Dynamic', `stiff ${(P.dynStiff ?? 1.0).toFixed(2)}× · tear ${(P.dynTearThresh || 0).toFixed(1)} m/s`] }
                                                    : Cls.IsCurl
                                                        ? { Label: 'Manifold', Resolve: () => [['Planar wave', 'Sphere shell', 'Bokeh ribbon'][Cls.CurlMode - 1] || 'Curl field', `${(P.curlAmp ?? 2.2).toFixed(1)} m/s flow`] }
                                                        : Cls.IsBoids
                                                            ? { Label: 'Cruise speed', Resolve: () => [`${(P.boidsSpeed ?? 3.6).toFixed(1)} m/s`, `radius ${(P.swarmRadius ?? 1.5).toFixed(1)} m`] }
                                                            : { Label: 'Gravity', Resolve: () => [`${P.gravity.toFixed(2)} g`, `${P.drag.toFixed(2)} drag`] };

        C.MetricTrio(Page, [
            { Label: 'Shape', Resolve: () => [Cls.IsFirework ? 'Hanabi Stars & Tails' : Cls.IsFlare ? 'IR Magnesium & Smoke' : Cls.IsGrid ? (GridShapeLabels[Math.round(P.gridShape ?? 0)] || 'Grid Lattice') : Cls.IsFerro ? 'Rosensweig Spikes' : Cls.IsCrack ? 'Voronoi Slabs' : Cls.IsBeads ? BeadsModeLabels[Cls.BeadsMode] : Cls.IsSeedHead ? (Cls.SeedHeadMode === 1 ? 'Pappus Seeds' : 'Blossoms') : Cls.IsJelly ? 'Soft-Body Jelly' : (['Streak', 'Sphere', 'Leaf', 'Glow', 'Cube', 'Coin', 'Bubble', 'Snowflake', 'Hailstone', 'Bokeh Bead', 'Shockwave Ring', 'EMP Pulse Dome', 'Holo Glitch', 'Winged Boid', 'Viscous Drop', 'Radar / Sonar Sweep', 'Energy Shield Multi', 'Aurora Curtain', '3D Glass Shard', 'Volumetric Water Mist'][P.shape] || 'Strand'), P.blend === 'alpha' ? 'alpha blend' : 'additive glow'] },
            SecondTrio,
            { Label: 'Wind coupling', Highlight: true, Resolve: () => Cls.UsesWind ? [`${P.windCoupling.toFixed(2)} /s`, 'lattice coupling'] : ['Locked', Cls.IsAssembly ? 'analytic assembly' : Cls.IsGrid ? 'effector-driven' : (Cls.IsFerro || Cls.IsCrack) ? 'magnetic / seismic' : 'static HUD overlay'] }
        ]);

        Page.append(C.Construct('div', 'section-subhead', [C.Construct('span', null, 'Quick controls'), C.Construct('span', null, Preset?.group || 'Particles')]));
        const Grid = C.Construct('div', 'property-switches');
        C.ToggleTile(Grid, BindEffect(Effect, P, 'visible', { Label: 'Visible', Glyph: 'Eye' }));
        C.ToggleTile(Grid, {
            Label: 'Wind-driven', Glyph: 'Wind',
            Read: () => Cls.UsesWind && P.windCoupling > 0,
            Write: PE.Edits.Through('Wind coupling', On => { P.windCoupling = On ? (Effect.StoredCoupling || 1) : (Effect.StoredCoupling = P.windCoupling, 0); }),
            Availability: () => Cls.UsesWind ? { Enabled: true } : { Enabled: false, Reason: Cls.IsAssembly ? 'Analytic assembly ignores wind' : 'Stationary HUD / shield overlay ignores wind' }
        });
        C.ToggleTile(Grid, {
            Label: 'Additive glow', Glyph: 'Sparkles',
            Read: () => P.blend === 'add',
            Write: PE.Edits.Through('Blend', On => { P.blend = On ? 'add' : 'alpha'; })
        });
        C.ToggleTile(Grid, {
            Label: 'Floor bounce', Glyph: 'Activity',
            Read: () => !Cls.BounceBlockReason && P.bounce > 0,
            Write: PE.Edits.Through('Bounce', On => { P.bounce = On ? (Effect.StoredBounce || (Cls.Shape === 14 ? 0.28 : 0.4)) : (Effect.StoredBounce = P.bounce, 0); }),
            Availability: () => Cls.BounceBlockReason ? { Enabled: false, Reason: Cls.BounceBlockReason } : { Enabled: true }
        });
        Page.append(Grid);

        const BurstAction = Cls.IsArc
            ? { Label: 'Trigger Spark', Glyph: 'Zap', Tone: 'accent', Act: () => { TriggerArcStrike(Effect, Scene.Time); AppendLog('PARTICLES', `${Effect.Name}: fired spark arc`, `t=${Scene.Time.toFixed(2)}s`, 'live'); } }
            : Cls.IsFirework
                ? { Label: 'Next Shell', Glyph: 'Sparkles', Tone: 'accent', Act: () => { const Cyc = Math.max(2.2, P.burstEvery || 3.1); Scene.Time = (Math.floor(Scene.Time / Cyc) + 1) * Cyc + 0.01; Effect._fwSnapCache = null; C.Toast('Launched next firework shell!'); } }
                : Cls.IsFlare
                    ? { Label: 'Fire Salvo', Glyph: 'Flame', Tone: 'accent', Act: () => { const Cyc = Math.max(1.2, P.burstEvery || 2.6); Scene.Time = (Math.floor(Scene.Time / Cyc) + 1) * Cyc + 0.01; C.Toast('IR flare salvo deployed!'); } }
                    : Cls.IsCrack
                ? { Label: 'Fracture Ground', Glyph: 'Zap', Tone: 'accent', Act: () => { Effect.CrackHit = { x: P.origin[0], z: P.origin[2], t: Scene.Time }; C.Toast('Ground fractured!'); } }
                : (Cls.IsDyn || Cls.IsWeb || Cls.IsGrid || Cls.IsFerro || Cls.IsBeads || Cls.IsSeedHead || Cls.IsJelly || Cls.IsBoids || Cls.Shape === 16)
                    ? { Label: 'Throw Ball', Glyph: 'Ball', Tone: 'accent', Act: () => { ThrowBallAtTarget(P.origin, Scene.Ball.ThrowSpeed); C.Toast('Ball thrown!'); } }
                    : Cls.IsTransition
                        ? { Label: 'Replay Cycle', Glyph: 'Sparkles', Tone: 'accent', Act: () => { RebuildGpu(Effect); AppendLog('PARTICLES', `${Effect.Name}: replayed transition cycle`, `${Effect.Gpu.cap} pieces`, 'live'); } }
                        : Cls.IsSteadyOverlay
                            ? { Label: 'Burst', Glyph: 'Sparkles', Tone: 'accent', Act: () => {}, Availability: () => ({ Enabled: false, Reason: Cls.IsCurl ? 'Continuous curl stream pre-populates smoothly' : 'Steady-state HUD / shield overlay runs continuously' }) }
                            : Cls.IsExpandingWave
                                ? { Label: 'Emit Pulse', Glyph: 'Sparkles', Tone: 'accent', Act: () => { Effect.PendingBurst += Math.max(1, P.burstCount || 1); } }
                                : { Label: 'Burst', Glyph: 'Sparkles', Tone: 'accent', Act: () => { Effect.PendingBurst += Math.max(60, Math.round(Effect.Gpu.cap * 0.4)); } };

        C.ActionRow(Page, [
            BurstAction,
            { Label: 'Clear', Glyph: 'Reset', Act: () => RebuildGpu(Effect) },
            { Label: 'Duplicate', Glyph: 'Copy', Act: () => DuplicateEffect(Effect) },
            { Label: 'Delete', Glyph: 'Trash', Tone: 'danger', Act: () => RemoveEffect(Effect) }
        ]);

        if (Cls.IsFirework) AddFireworkSection(Page, '02 / PYROTECHNIC SPECTACLE');
        if (Cls.IsFlare) AddFlareSection(Page, '02 / IR COUNTERMEASURE SALVO');
        if (Cls.IsGrid) AddGridSection(Page, '02 / OBJECT & EFFECTOR GRID');
        if (Cls.IsDyn) AddDynSection(Page, '02 / RIGGING & FLORA');
        if (Cls.IsMud) AddMudSection(Page, '02 / PUDDLE & SPLASH');
        if (Cls.IsFerro) AddFerroSection(Page, '02 / FERROFLUID FLUX');
        if (Cls.IsCrack) AddCrackSection(Page, '02 / GROUND FRACTURE');
        if (Cls.IsBeads) AddBeadsSection(Page, '02 / HANGING CURTAINS & CHIMES');
        if (Cls.IsSeedHead) AddSeedHeadSection(Page, '02 / SEED HEADS & BLOSSOMS');
        if (Cls.IsJelly) AddJellySection(Page, '02 / SOFT-BODY JELLY');
        if (Cls.Shape === 16) AddShieldSection(Page, '02 / SHIELD PATTERNS');
        const HasSpecialCard = Cls.IsFirework || Cls.IsFlare || Cls.IsGrid || Cls.IsDyn || Cls.IsMud || Cls.IsFerro || Cls.IsCrack || Cls.IsBeads || Cls.IsSeedHead || Cls.IsJelly || Cls.Shape === 16;
        if (HasSpecialCard || Cls.IsWeb || Cls.IsBoids || P.arcMode >= 2)
            AddBallInteractionCard(Page, HasSpecialCard ? '03 / INTERACTIVE BALL' : '02 / INTERACTIVE BALL', !Cls.IsDyn);

        if (Cls.UsesLaunchCone)
        {
            const Opt = C.SectionCard(Page, { Title: 'Emitter cone & spectrum', Glyph: 'Aperture', Badge: '02 / OPTICS', Hint: 'Live launch direction, spread cone angle, speed band and colour ramp.' });
            C.EmitterInstrument(Opt.Inner, Effect, DisplayColour);
            const Ribbon = C.Construct('div', 'gradient-ribbon');
            Opt.Inner.append(Ribbon);
            C.Register(() => { Ribbon.style.background = SpectrumGradient(P); });
        }
        else
        {
            const SpecBadge = HasSpecialCard ? '04 / SPECTRUM' : ((Cls.IsWeb || Cls.IsBoids || P.arcMode >= 2) ? '03 / SPECTRUM' : '02 / SPECTRUM');
            const Spec = C.SectionCard(Page, { Title: 'Colour spectrum', Glyph: 'Palette', Badge: SpecBadge, Hint: Cls.IsAssembly ? 'Base surface tint used during analytic assembly.' : 'Active colour ramp rendered by this effect.' });
            const Ribbon = C.Construct('div', 'gradient-ribbon');
            Spec.Inner.append(Ribbon);
            C.Register(() => { Ribbon.style.background = SpectrumGradient(P); });
        }
        return;
    }

    if (InspectorTab === 'source')
    {
        const SrcHint = Cls.IsTransition
            ? 'Transition batches spawn full pool capacity together at each cycle.'
            : Cls.IsGrid
                ? 'Effector grids anchor surface lattices or 3D obsidian meshes whose scale, glow, and up/down cube height respond live to spatial waves, Force Fields, and thrown balls.'
                : Cls.IsDyn
                    ? 'Dynamic strand, cloth & botanical systems initialize all structural nodes immediately and deform continuously under wind and ball impacts.'
                    : Cls.IsObjectPour
                        ? 'Particles pour directly over the 3D object surface whose dimensions come from boxHalf.'
                        : Cls.CurlMode === 2
                            ? 'Particles spawn directly onto the spherical plasma shell around the origin.'
                            : 'Birth rate, emitter volume geometry and spawn origin.';
        const Src = C.SectionCard(Page, { Title: 'Emission source', Glyph: 'Target', Badge: '01 / SOURCE', Hint: SrcHint }).Inner;
        const Capacities = [...new Set([64, 96, 128, 220, 384, 512, 576, 720, 1024, 1200, 1536, 2048, 4096, 8192, P.capacity])].sort((A, B) => A - B);
        C.SelectRow(Src, BindEffect(Effect, P, 'capacity', { Label: 'Pool capacity', Options: Capacities.map(K => [FormatInteger(K), K]), Rebuild: true }));
        if (Cls.UsesRate)
            Range(Src, BindEffect(Effect, P, 'rate', { Label: 'Spawn rate' }), 0, 2000, 1, 0, '/s');
        if (Cls.UsesEmitVolume)
        {
            const EmitShapeB = BindEffect(Effect, P, 'emitShape', { Label: 'Volume shape', Options: [['Point', 0], ['Sphere volume', 1], ['Disc', 2], ['Box', 3], ['Box surface lattice', 4]] });
            const OrigEmitWrite = EmitShapeB.Write;
            EmitShapeB.Write = V => { OrigEmitWrite(V); ConstructInspectorPage(); };
            C.SelectRow(Src, EmitShapeB);
        }
        const OriginLabel = Cls.IsArc ? ((P.arcMode | 0) >= 2 ? 'Cage / Storm centre' : 'Sphere A position') : Cls.IsBeam ? 'Beam muzzle position' : Cls.IsObjectPour ? '3D object centre' : Cls.IsGrid ? 'Surface / grid centre' : Cls.IsFerro ? 'Vessel / dish centre' : Cls.IsCrack ? 'Fracture sheet centre' : Cls.IsMud ? 'Puddle / track centre' : Cls.IsBeads ? 'Lintel / chime hanger origin' : Cls.IsSeedHead ? 'Meadow / orchard root origin' : Cls.IsJelly ? 'Soft-body resting centre' : Cls.IsWeb ? 'Web anchor centre' : Cls.IsDyn ? 'Structure / root origin' : 'Emitter origin';
        C.VectorRow(Src, BindEffect(Effect, P, 'origin', { Label: OriginLabel, Unit: 'm', Step: 0.05, Digits: 2, Minimum: -20, Maximum: 20 }));
        if (Cls.CurlMode === 2)
            Range(Src, BindEffect(Effect, P, 'shellRadius', { Label: 'Sphere shell radius' }), 0.4, 5.0, 0.05, 2, 'm');
        if (Cls.UsesRadius)
        {
            const RadLabel = Cls.IsArc ? ((P.arcMode | 0) >= 2 ? 'Plasma cage radius' : 'Sphere A radius') : Cls.IsBeam ? 'Muzzle ring radius' : Cls.IsGrid ? 'Surface / hand scale radius' : Cls.IsFerro ? 'Dish / ferrofluid radius' : Cls.IsMud ? 'Puddle pool radius' : Cls.IsBeads ? 'Chime crown ring radius' : Cls.IsSeedHead ? 'Seed puff / blossom cluster radius' : Cls.IsJelly ? 'Jelly body radius' : Cls.IsWeb ? 'Orb web frame radius' : 'Emitter radius';
            Range(Src, BindEffect(Effect, P, 'radius', { Label: RadLabel }), 0.04, 5, 0.01, 2, 'm');
        }
        if (Cls.UsesBoxHalf)
        {
            const BoxLabel = Cls.IsObjectPour
                ? ((P.objectShape ?? 0) === 1 ? 'Object half extents (X = sphere radius)' : 'Object half extents')
                : Cls.CurlMode === 1
                    ? 'Wave field extents'
                    : Cls.IsGrid
                        ? 'Planar / cube grid half extents'
                        : Cls.IsCrack
                            ? 'Ground crust half extents'
                            : Cls.IsBeads
                                ? 'Doorway span width / depth'
                                : Cls.IsSeedHead
                                    ? 'Meadow / orchard patch extents'
                                    : Cls.IsWeb
                                        ? 'Corner cobweb volume'
                                        : Cls.IsDyn
                                            ? (Cls.DynMode === 3 ? 'Cloth span extents' : 'Rigging / patch half extents')
                                            : Cls.IsAssembly
                                                ? 'Target cube half extents'
                                                : 'Box half extents';
            C.VectorRow(Src, BindEffect(Effect, P, 'boxHalf', { Label: BoxLabel, Unit: 'm', Step: 0.05, Digits: 2, Minimum: 0, Maximum: 10 }));
        }
        if (Cls.IsFirework)
        {
            AddFireworkSection(Page, '02 / PYROTECHNIC SPECTACLE');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsFlare)
        {
            AddFlareSection(Page, '02 / IR COUNTERMEASURE SALVO');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsGrid)
        {
            AddGridSection(Page, '02 / EFFECTOR GRID');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsMud)
        {
            AddMudSection(Page, '02 / PUDDLE & SPLASH');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsFerro)
        {
            AddFerroSection(Page, '02 / FERROFLUID FLUX');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsCrack)
        {
            AddCrackSection(Page, '02 / GROUND FRACTURE');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsBeads)
        {
            AddBeadsSection(Page, '02 / HANGING CURTAINS & CHIMES');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsSeedHead)
        {
            AddSeedHeadSection(Page, '02 / SEED HEADS & BLOSSOMS');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsJelly)
        {
            AddJellySection(Page, '02 / SOFT-BODY JELLY');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsDyn)
        {
            AddDynSection(Page, '02 / RIGGING & FLORA');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsWeb)
        {
            const WebSecSrc = C.SectionCard(Page, { Title: 'Real-time particle web spinning', Glyph: 'Lattice', Badge: Cls.WebMode === 1 ? 'ORB-WEAVE' : 'COBWEBS' }).Inner;
            const WebModeB = BindEffect(Effect, P, 'webMode', { Label: 'Web architecture', Rebuild: true, Options: [['Orb-Weave Spiral Web', 1], ['Messy Attic Cobwebs', 2]] });
            const OrigWebW = WebModeB.Write;
            WebModeB.Write = V => { OrigWebW(Number(V)); ConstructInspectorPage(); };
            C.SelectRow(WebSecSrc, WebModeB);
            C.ActionRow(WebSecSrc, [{
                Label: 'Re-Spin Web From Scratch', Glyph: 'Reset', Tone: 'accent',
                Act: () => { RebuildGpu(Effect); AppendLog('PARTICLES', `${Effect.Name}: restarted real-time web spinning`, `mode=${P.webMode}`, 'live'); C.Toast('Spinning spiderweb...'); }
            }]);
            Range(WebSecSrc, BindEffect(Effect, P, 'webSpinRate', { Label: 'Spinning speed' }), 0.15, 4.0, 0.05, 2, '×');
            Range(WebSecSrc, BindEffect(Effect, P, 'webSpokes', { Label: Cls.WebMode === 1 ? 'Radial spokes' : 'Anchor chord density' }), 8, 28, 1, 0, '');
            Range(WebSecSrc, BindEffect(Effect, P, 'webRings', { Label: Cls.WebMode === 1 ? 'Capture spiral turns' : 'Tangle mesh depth' }), 6, 22, 1, 0, '');
            Range(WebSecSrc, BindEffect(Effect, P, 'webSag', { Label: 'Silk gravity catenary sag' }), 0.0, 0.65, 0.01, 2, 'm');
            Range(WebSecSrc, BindEffect(Effect, P, 'webDew', { Label: Cls.WebMode === 1 ? 'Morning dew glints' : 'Dust & gossamer shimmer' }), 0.0, 1.0, 0.02, 2, '');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsArc)
        {
            const ArcSrc = C.SectionCard(Page, { Title: 'Electric arc, plasma cage & storm web', Glyph: 'Zap', Badge: '02 / ELECTRODES' }).Inner;
            const ArcEnableB = {
                Label: 'Arc discharge mode',
                Options: [['Dielectric Spark Lightning', 0], ['Static Corona Filaments', 1], ['Plasma Cage Web (Ball Capture)', 2], ['Dynamic Lightning Web Mesh', 3], ['Off (standard emitter)', -1]],
                Read: () => IsArcMode(P.arcMode) ? P.arcMode : -1,
                Write: V =>
                {
                    const Mode = Number(V);
                    if (Mode < 0) delete P.arcMode;
                    else
                    {
                        P.arcMode = Mode;
                        if (!P.targetPos) P.targetPos = [P.origin[0] + 4.0, P.origin[1], P.origin[2]];
                        if (P.targetRadius === undefined) P.targetRadius = Math.max(0.25, P.radius || 0.45);
                        if (P.arcLoop === undefined) P.arcLoop = Mode >= 1;
                        if (P.arcHold === undefined) P.arcHold = Mode >= 2 ? 0.26 : (Mode === 1 ? 0.24 : 0.30);
                        if (P.arcInterval === undefined) P.arcInterval = Mode >= 2 ? 0.28 : (Mode === 1 ? 0.55 : 1.40);
                        if (P.arcBow === undefined) P.arcBow = Mode === 1 ? 0.32 : 0.14;
                        if (P.arcCount === undefined) P.arcCount = Mode >= 2 ? 4 : (Mode === 1 ? 3 : 1);
                        if (P.arcJitter === undefined) P.arcJitter = Mode >= 2 ? 0.68 : (Mode === 1 ? 0.42 : 0.62);
                        if (P.arcRate === undefined) P.arcRate = Mode === 1 ? 28 : 14;
                        if (P.showElectrodes === undefined) P.showElectrodes = true;
                        TriggerArcStrike(Effect, Scene.Time);
                    }
                    PE.Edits.Announce('Arc mode', Mode);
                    ConstructInspectorPage();
                }
            };
            C.SelectRow(ArcSrc, ArcEnableB);
            if (!P.targetPos) P.targetPos = [P.origin[0] + 4.0, P.origin[1], P.origin[2]];
            C.ActionRow(ArcSrc, [{
                Label: '⚡ Trigger Instantaneous Spark', Glyph: 'Zap', Tone: 'accent',
                Act: () => { TriggerArcStrike(Effect, Scene.Time); AppendLog('PARTICLES', `${Effect.Name}: fired spark arc`, `t=${Scene.Time.toFixed(2)}s`, 'live'); C.Toast('Spark arc fired'); }
            }]);
            const LoopB = BindEffect(Effect, P, 'arcLoop', { Label: 'Looping (optional auto-repeat)' });
            const OrigLoopW = LoopB.Write;
            LoopB.Write = V => { OrigLoopW(!!V); if (V) TriggerArcStrike(Effect, Scene.Time); ConstructInspectorPage(); };
            C.SwitchRow(ArcSrc, LoopB);
            if (P.arcLoop) Range(ArcSrc, BindEffect(Effect, P, 'arcInterval', { Label: 'Repeat interval' }), 0.14, 5.0, 0.04, 2, 's');
            Range(ArcSrc, BindEffect(Effect, P, 'arcHold', { Label: 'Spark discharge fade' }), 0.08, 0.90, 0.02, 2, 's');
            if ((P.arcMode | 0) <= 1)
            {
                C.VectorRow(ArcSrc, BindEffect(Effect, P, 'targetPos', { Label: 'Sphere B position (drag in 3D or type XYZ)', Unit: 'm', Step: 0.05, Digits: 2, Minimum: -20, Maximum: 20 }));
                Range(ArcSrc, BindEffect(Effect, P, 'targetRadius', { Label: 'Sphere B radius' }), 0.08, 4, 0.01, 2, 'm');
                Range(ArcSrc, BindEffect(Effect, P, 'arcBow', { Label: 'Thermal arch lift' }), -1.5, 2.5, 0.02, 2, 'm');
            }
            Range(ArcSrc, BindEffect(Effect, P, 'arcCount', { Label: (P.arcMode | 0) >= 2 ? 'Simultaneous plasma arcs' : 'Main discharge bolts' }), 1, 10, 1, 0, '');
            Range(ArcSrc, BindEffect(Effect, P, 'arcJitter', { Label: P.arcMode === 1 ? 'Filament curl' : 'Dielectric jaggedness' }), 0.08, 1.4, 0.02, 2, '');
            const ElecGrid = C.Construct('div', 'property-switches');
            ArcSrc.append(ElecGrid);
            C.ToggleTile(ElecGrid, BindEffect(Effect, P, 'showElectrodes', { Label: (P.arcMode | 0) >= 2 ? 'Show plasma nodes' : 'Show 3D spheres', Glyph: 'Target' }));
            if ((P.arcMode | 0) >= 2) AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsBeam)
        {
            const BeamSec = C.SectionCard(Page, { Title: 'Coherent beam & impact target', Glyph: 'Rays', Badge: '02B / BEAM' }).Inner;
            if (!P.targetPos) P.targetPos = [P.origin[0] + 4.8, P.origin[1] - 0.8, P.origin[2]];
            C.VectorRow(BeamSec, BindEffect(Effect, P, 'targetPos', { Label: 'Target impact position (drag in 3D)', Unit: 'm', Step: 0.05, Digits: 2, Minimum: -20, Maximum: 20 }));
            if (P.showElectrodes !== false)
                Range(BeamSec, BindEffect(Effect, P, 'targetRadius', { Label: 'Target ring radius' }), 0.08, 2.0, 0.01, 2, 'm');
            Range(BeamSec, BindEffect(Effect, P, 'beamWidth', { Label: 'Beam plasma core radius' }), 0.01, 0.25, 0.004, 3, 'm');
            Range(BeamSec, BindEffect(Effect, P, 'beamPulse', { Label: 'Plasma wave pulse speed' }), 0.5, 20, 0.25, 2, 'Hz');
            const BGrid = C.Construct('div', 'property-switches');
            BeamSec.append(BGrid);
            const ShowElecB = BindEffect(Effect, P, 'showElectrodes', { Label: 'Show muzzle/target rings', Glyph: 'Target' });
            const OrigElecW = ShowElecB.Write;
            ShowElecB.Write = V => { OrigElecW(!!V); ConstructInspectorPage(); };
            C.ToggleTile(BGrid, ShowElecB);
        }
        if (Cls.UsesDirVector || Cls.UsesSpread || Cls.UsesSpeed || Cls.UsesLife)
        {
            const Vel = C.SectionCard(Page, { Title: Cls.IsWeb ? 'Web facing orientation' : (Cls.IsDiscPlane && !Cls.UsesSpeed ? 'Orientation & lifetime' : 'Launch velocity & lifetime'), Glyph: 'Rays', Badge: '03 / KINEMATICS' }).Inner;
            if (Cls.UsesDirVector)
                C.VectorRow(Vel, BindEffect(Effect, P, 'dir', { Label: Cls.IsWeb ? 'Web plane normal' : (Cls.IsDiscPlane ? 'Disc / plane normal' : 'Launch direction'), Step: 0.01, Digits: 2, Minimum: -1, Maximum: 1 }));
            if (Cls.UsesSpread)
                Range(Vel, BindEffect(Effect, P, 'spread', { Label: 'Cone spread' }), 0, 3.1416, 0.01, 2, 'rad');
            if (Cls.UsesSpeed)
            {
                const SpdMinLbl = Cls.IsTransition ? 'Burst speed minimum' : Cls.IsArc || Cls.IsBeam ? 'Spark speed minimum' : 'Speed minimum';
                const SpdMaxLbl = Cls.IsTransition ? 'Burst speed maximum' : Cls.IsArc || Cls.IsBeam ? 'Spark speed maximum' : 'Speed maximum';
                Range(Vel, Object.assign(BindEffect(Effect, P, 'speedMin', { Label: SpdMinLbl }), { Write: PE.Edits.Through('Speed min', V => { P.speedMin = Math.min(V, P.speedMax); }) }), 0, 20, 0.05, 2, 'm/s');
                Range(Vel, Object.assign(BindEffect(Effect, P, 'speedMax', { Label: SpdMaxLbl }), { Write: PE.Edits.Through('Speed max', V => { P.speedMax = Math.max(V, P.speedMin); }) }), 0, 20, 0.05, 2, 'm/s');
            }
            if (Cls.UsesLife)
            {
                const LifeMinLbl = Cls.IsTransition ? 'Post-hold lifetime minimum' : 'Life minimum';
                const LifeMaxLbl = Cls.IsTransition ? 'Post-hold lifetime maximum' : 'Life maximum';
                Range(Vel, Object.assign(BindEffect(Effect, P, 'lifeMin', { Label: LifeMinLbl }), { Write: PE.Edits.Through('Life min', V => { P.lifeMin = Math.min(V, P.lifeMax); }) }), 0.05, 20, 0.05, 2, 's');
                Range(Vel, Object.assign(BindEffect(Effect, P, 'lifeMax', { Label: LifeMaxLbl }), { Write: PE.Edits.Through('Life max', V => { P.lifeMax = Math.max(V, P.lifeMin); }) }), 0.05, 20, 0.05, 2, 's');
            }
        }
        if (Cls.UsesTimedBurst)
        {
            const BurstSec = C.SectionCard(Page, { Title: 'Timed burst cadence', Glyph: 'Sparkles', Badge: '04 / BURSTS', Hint: 'Optional periodic bursts (e.g. fireworks, flares, shockwaves, coin showers). Set interval > 0 to repeat automatically.' }).Inner;
            Range(BurstSec, BindEffect(Effect, P, 'burstEvery', { Label: 'Burst interval (0 = off)' }), 0, 12, 0.1, 1, 's');
            Range(BurstSec, BindEffect(Effect, P, 'burstCount', { Label: 'Particles per burst' }), 1, Math.max(400, P.capacity || 400), 1, 0, '');
            C.SwitchRow(BurstSec, BindEffect(Effect, P, 'burstAtOrigin', { Label: 'Fire bursts from emitter origin (unchecked = random sky)' }));
        }
        return;
    }

    if (InspectorTab === 'motion')
    {
        const Look = C.SectionCard(Page, { Title: 'Particle appearance', Glyph: 'Palette', Badge: '01 / SHADING' }).Inner;
        const Ribbon = C.Construct('div', 'gradient-ribbon');
        Look.append(Ribbon);
        C.Register(() => { Ribbon.style.background = SpectrumGradient(P); });
        if (!Cls.HasCustomGeometry)
        {
            const ShapeB = BindEffect(Effect, P, 'shape', {
                Label: 'Billboard / mesh shape',
                Options: [
                    ['Streak', 0], ['Sphere', 1], ['Leaf / paper', 2], ['Soft glow', 3], ['Chip cube', 4],
                    ['Coin', 5], ['Bubble', 6], ['Snowflake', 7], ['Hailstone', 8], ['Bokeh glow bead', 9],
                    ['Shockwave ring', 10], ['EMP pulse dome', 11], ['Hologram glyph / scanline', 12],
                    ['Winged boid (bird/butterfly/fish)', 13], ['Viscous liquid droplet', 14], ['Radar sweep / sonar ping', 15],
                    ['Energy shield hex dome', 16], ['Aurora borealis curtain', 17], ['3D Glass Shard (mesh)', 18], ['Volumetric water mist', 19]
                ]
            });
            const OrigShapeW = ShapeB.Write;
            ShapeB.Write = V => { OrigShapeW(Number(V)); ConstructInspectorPage(); };
            C.SelectRow(Look, ShapeB);
        }
        C.SegmentedRow(Look, BindEffect(Effect, P, 'blend', { Label: 'Compositing blend', Options: [['Additive glow', 'add'], ['Alpha · premultiplied', 'alpha']] }));
        Range(Look, BindEffect(Effect, P, 'sizeStart', { Label: Cls.IsAssembly ? 'Piece size' : 'Size at birth' }), 0.002, 16, 0.004, 3, 'm');
        if (Cls.UsesSizeEnd)
            Range(Look, BindEffect(Effect, P, 'sizeEnd', { Label: 'Size at death' }), 0.002, 18, 0.01, 3, 'm');
        Range(Look, BindEffect(Effect, P, 'sizeScale', { Label: 'Size multiplier' }), 0.1, 4, 0.02, 2, '×');
        if (Cls.UsesColorRamp)
            AddColorRampEditor(Look, Effect, P, Cls);
        else
            C.ColourRow(Look, BindEffect(Effect, P, 'colA', { Label: 'Base surface colour', Alpha: true }));
        if (P.shape === 0 && !Cls.IsCurl && !(P.trailMode > 0))
            Range(Look, BindEffect(Effect, P, 'streakThin', { Label: 'Streak thinness' }), 0.1, 2.0, 0.02, 2, '×');
        if (P.shape === 2)
            C.SelectRow(Look, BindEffect(Effect, P, 'leafMode', { Label: 'Leaf / paper silhouette', Options: [['Botanical leaf', 0], ['Paper / confetti sheet', 1]] }));
        if (P.shape === 10)
            C.SelectRow(Look, BindEffect(Effect, P, 'leafMode', { Label: 'Shockwave style', Options: [['Supersonic Mach ring', 0], ['Ground-hugging impact dust ring', 1]] }));
        if (P.shape === 15)
        {
            const RadStyleB = BindEffect(Effect, P, 'leafMode', { Label: 'Radar / sonar style', Options: [['Tactical Radar Sweep & Blips', 0], ['Acoustic Sonar Pulse Ping', 1]] });
            const OrigRadW = RadStyleB.Write;
            RadStyleB.Write = V => { OrigRadW(Number(V)); ConstructInspectorPage(); };
            C.SelectRow(Look, RadStyleB);
            if ((P.leafMode || 0) === 0)
                Range(Look, BindEffect(Effect, P, 'curlRate', { Label: 'Sweep angular speed' }), 0.2, 6.0, 0.05, 2, 'rad/s');
        }
        if (P.shape === 16)
        {
            AddShieldSection(Page, '02 / SHIELD BARRIER');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (P.shape === 17)
        {
            Range(Look, BindEffect(Effect, P, 'curlRate', { Label: 'Curtain drift & shimmer' }), 0.05, 1.5, 0.02, 2, 'Hz');
            Range(Look, BindEffect(Effect, P, 'curlFreq', { Label: 'Ray wave frequency' }), 0.3, 2.5, 0.05, 2, '×');
        }
        if (P.shape === 12)
        {
            C.SelectRow(Look, BindEffect(Effect, P, 'leafMode', { Label: 'Hologram glyph mode', Options: [['Matrix glyphs + scanline slices', 0], ['Horizontal scanline slices only', 1]] }));
            Range(Look, BindEffect(Effect, P, 'glitchRate', { Label: 'Data glitch tick rate' }), 2, 40, 1, 0, 'Hz');
        }
        if (Cls.UsesTrailMode)
        {
            const TrailB = {
                Label: 'Tapered 3D ribbon trails',
                Options: [['Off', 0], ['Glowing projectile / drip ribbon trail', 1]],
                Read: () => (P.trailMode || 0) > 0 ? 1 : 0,
                Write: PE.Edits.Through('Trail mode', V => { P.trailMode = Number(V); RebuildGpu(Effect); ConstructInspectorPage(); })
            };
            C.SelectRow(Look, TrailB);
            if ((P.trailMode || 0) > 0) Range(Look, BindEffect(Effect, P, 'streakThin', { Label: 'Trail ribbon thinness' }), 0.12, 1.5, 0.02, 2, '×');
        }
        if (Cls.IsBoids)
        {
            const BoidsSec = C.SectionCard(Page, { Title: 'Boids flocking, murmuration & predator evade', Glyph: 'Wind', Badge: '02 / FLOCK' }).Inner;
            C.SelectRow(BoidsSec, BindEffect(Effect, P, 'leafMode', { Label: 'Creature silhouette', Options: [['Birds / Starlings (flexing V-wings)', 0], ['Butterflies / Wisps (dual wings)', 1], ['Fish school (undulating fin)', 2]] }));
            Range(BoidsSec, BindEffect(Effect, P, 'swarmRadius', { Label: 'Neighbour perception radius' }), 0.3, 4.0, 0.05, 2, 'm');
            Range(BoidsSec, BindEffect(Effect, P, 'boidsSpeed', { Label: 'Cruise flight speed' }), 0.5, 9.0, 0.1, 2, 'm/s');
            Range(BoidsSec, BindEffect(Effect, P, 'boidsCohere', { Label: 'Cohesion weight' }), 0.0, 5.0, 0.05, 2, '');
            Range(BoidsSec, BindEffect(Effect, P, 'boidsAlign', { Label: 'Alignment weight' }), 0.0, 5.0, 0.05, 2, '');
            Range(BoidsSec, BindEffect(Effect, P, 'boidsSeparate', { Label: 'Separation weight' }), 0.0, 6.0, 0.05, 2, '');
            Range(BoidsSec, BindEffect(Effect, P, 'pulseHz', { Label: 'Bioluminescent pulse rate' }), 0.0, 6.0, 0.05, 2, 'Hz');
            Range(BoidsSec, BindEffect(Effect, P, 'pulseDepth', { Label: 'Pulse glow depth' }), 0.0, 1.0, 0.02, 2, '');
            AddBallInteractionCard(Page, '03 / PREDATOR BALL');
        }
        if (Cls.IsBeads)
        {
            AddBeadsSection(Page, '02 / HANGING CURTAINS & CHIMES');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsSeedHead)
        {
            AddSeedHeadSection(Page, '02 / SEED HEADS & BLOSSOMS');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsJelly)
        {
            AddJellySection(Page, '02 / SOFT-BODY JELLY');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsWeb)
        {
            const WebMot = C.SectionCard(Page, { Title: 'Silk weaving & aerodynamics', Glyph: 'Lattice', Badge: '02 / SPIDERWEB' }).Inner;
            const WebModeB = BindEffect(Effect, P, 'webMode', { Label: 'Web architecture', Rebuild: true, Options: [['Orb-Weave Spiral Web', 1], ['Messy Attic Cobwebs', 2]] });
            const OrigWebW = WebModeB.Write;
            WebModeB.Write = V => { OrigWebW(Number(V)); ConstructInspectorPage(); };
            C.SelectRow(WebMot, WebModeB);
            C.ActionRow(WebMot, [{
                Label: 'Re-Spin Web From Scratch', Glyph: 'Reset', Tone: 'accent',
                Act: () => { RebuildGpu(Effect); C.Toast('Spinning spiderweb...'); }
            }]);
            Range(WebMot, BindEffect(Effect, P, 'webSpinRate', { Label: 'Spinning speed' }), 0.15, 4.0, 0.05, 2, '×');
            Range(WebMot, BindEffect(Effect, P, 'webSpokes', { Label: Cls.WebMode === 1 ? 'Radial spokes' : 'Anchor chord density' }), 8, 28, 1, 0, '');
            Range(WebMot, BindEffect(Effect, P, 'webRings', { Label: Cls.WebMode === 1 ? 'Capture spiral turns' : 'Tangle mesh depth' }), 6, 22, 1, 0, '');
            Range(WebMot, BindEffect(Effect, P, 'webSag', { Label: 'Silk gravity sag' }), 0.0, 0.65, 0.01, 2, 'm');
            Range(WebMot, BindEffect(Effect, P, 'webDew', { Label: Cls.WebMode === 1 ? 'Morning dew glints' : 'Dust & gossamer shimmer' }), 0.0, 1.0, 0.02, 2, '');
            Range(WebMot, BindEffect(Effect, P, 'streakThin', { Label: 'Silk strand thickness' }), 0.1, 2.0, 0.02, 2, '×');
            Range(WebMot, BindEffect(Effect, P, 'curlAmp', { Label: 'Gust flutter strength' }), 0.05, 3.0, 0.05, 2, 'm/s');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsGrid)
        {
            AddGridSection(Page, '02 / EFFECTOR GRID');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsMud)
        {
            AddMudSection(Page, '02 / PUDDLE & SPLASH');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsFerro)
        {
            AddFerroSection(Page, '02 / FERROFLUID FLUX');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsCrack)
        {
            AddCrackSection(Page, '02 / GROUND FRACTURE');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsDyn)
        {
            AddDynSection(Page, '02 / RIGGING & FLORA');
            AddBallInteractionCard(Page, '03 / INTERACTIVE BALL');
        }
        if (Cls.IsCurl)
        {
            const CurlSec = C.SectionCard(Page, { Title: 'Curl noise & manifold', Glyph: 'Wind', Badge: '02 / CURL FLOW' }).Inner;
            const CurlModeB = BindEffect(Effect, P, 'curlMode', { Label: 'Manifold mode', Rebuild: true, Options: [['Off (ballistic)', 0], ['Planar Wave Field', 1], ['Spherical Plasma Shell', 2], ['Bokeh Stream Ribbons', 3]] });
            const OrigCurlW = CurlModeB.Write;
            CurlModeB.Write = V => { OrigCurlW(Number(V)); ConstructInspectorPage(); };
            C.SelectRow(CurlSec, CurlModeB);
            Range(CurlSec, BindEffect(Effect, P, 'curlFreq', { Label: 'Curl noise frequency' }), 0.1, 3.0, 0.02, 2, '1/m');
            Range(CurlSec, BindEffect(Effect, P, 'curlAmp', { Label: 'Curl advection speed' }), 0.1, 8.0, 0.05, 2, 'm/s');
            Range(CurlSec, BindEffect(Effect, P, 'curlRate', { Label: 'Temporal evolution' }), 0.0, 1.5, 0.02, 2, 'Hz');
            if (Cls.CurlMode === 2) Range(CurlSec, BindEffect(Effect, P, 'shellRadius', { Label: 'Sphere shell radius' }), 0.4, 5.0, 0.05, 2, 'm');
            Range(CurlSec, BindEffect(Effect, P, 'streakThin', { Label: 'Filament thinness' }), 0.1, 2.0, 0.02, 2, '×');
            if (Cls.CurlMode === 3)
            {
                const PulseB = BindEffect(Effect, P, 'pulseMode', { Label: 'Object surface pour flow', Options: [['Off (free eddies)', 0], ['Continuous Surface Pour', 1], ['Pour + Pulse Wave', 2]] });
                const OrigPW = PulseB.Write;
                PulseB.Write = V => { OrigPW(Number(V)); ConstructInspectorPage(); };
                C.SelectRow(CurlSec, PulseB);
                if ((P.pulseMode || 0) > 0)
                {
                    C.SelectRow(CurlSec, BindEffect(Effect, P, 'objectShape', { Label: 'Object surface shape', Options: [['3D Sphere (Pour Over Ball)', 1], ['Cylinder Column', 2], ['Box / Slab', 0]] }));
                    C.SelectRow(CurlSec, BindEffect(Effect, P, 'pulseAxis', { Label: 'Pour direction axis', Options: [['Top → Bottom (Pour)', 1], ['Left → Right (X)', 0], ['Back → Front (Z)', 2]] }));
                    if ((P.pulseMode || 0) === 2)
                    {
                        Range(CurlSec, BindEffect(Effect, P, 'pulseSpeed', { Label: 'Pour pulse speed' }), 0.08, 1.5, 0.02, 2, 'Hz');
                        Range(CurlSec, BindEffect(Effect, P, 'pulseWidth', { Label: 'Wave band thickness' }), 0.08, 0.65, 0.02, 2, '');
                    }
                    const ObjGrid = C.Construct('div', 'property-switches');
                    CurlSec.append(ObjGrid);
                    C.ToggleTile(ObjGrid, BindEffect(Effect, P, 'showObject', { Label: 'Show inner 3D object', Glyph: 'Layers' }));
                }
                C.SelectRow(CurlSec, BindEffect(Effect, P, 'headStyle', { Label: 'Bokeh head style', Options: [['Mixed (per particle)', 0], ['Sharp core', 1], ['Creamy disc', 2], ['Rim ring', 3]] }));
                Range(CurlSec, BindEffect(Effect, P, 'headScale', { Label: 'Bokeh head size' }), 0.1, 3.0, 0.05, 2, '×');
            }
        }
        if (Cls.UsesDynamics)
        {
            const Mot = C.SectionCard(Page, { Title: 'Forces & motion response', Glyph: 'Wind', Badge: Cls.IsCurl || Cls.IsWeb || Cls.IsDyn || Cls.IsBoids ? '04 / DYNAMICS' : '02 / DYNAMICS' }).Inner;
            Range(Mot, BindEffect(Effect, P, 'windCoupling', { Label: 'Wind lattice coupling' }), 0, 5, 0.02, 2, '1/s');
            if (!Cls.IsCurl && !Cls.IsWeb && !Cls.IsDyn)
            {
                if (!Cls.IsBoids)
                    Range(Mot, BindEffect(Effect, P, 'drag', { Label: 'Aerodynamic drag' }), 0, 5, 0.02, 2, '1/s');
                Range(Mot, BindEffect(Effect, P, 'gravity', { Label: 'Gravity factor' }), -1, 2, 0.02, 2, 'g');
                Range(Mot, BindEffect(Effect, P, 'buoyancy', { Label: 'Thermal buoyancy' }), -4, 6, 0.05, 2, 'm/s²');
                if (!Cls.IsBoids)
                {
                    const BounceLbl = P.shape === 14 ? 'Floor restitution (<0.08 splats)' : 'Floor restitution';
                    Range(Mot, BindEffect(Effect, P, 'bounce', { Label: BounceLbl, Availability: () => Cls.BounceBlockReason ? { Enabled: false, Reason: Cls.BounceBlockReason } : { Enabled: true } }), 0, 1, 0.01, 2, '');
                }
                Range(Mot, BindEffect(Effect, P, 'flutter', { Label: 'Flutter turbulence' }), 0, 5, 0.05, 2, 'm/s²');
            }
        }
        return;
    }

    if (P.transition)
    {
        const Tr = P.transition;
        const Asm = Tr.assemble | 0;
        const Sec = C.SectionCard(Page, { Title: 'Derez / assembly wave', Glyph: 'Boxes', Badge: 'TRANSITION' }).Inner;
        const AsmB = BindEffect(Effect, Tr, 'assemble', { Label: 'Flight mode', Options: [['Burst or fall', 0], ['Rebuild from outside', 1], ['Stack in column', 2]] });
        const OrigAsmW = AsmB.Write;
        AsmB.Write = V => { OrigAsmW(Number(V)); RebuildGpu(Effect); ConstructInspectorPage(); };
        C.SelectRow(Sec, AsmB);
        if (Asm !== 2)
            C.SelectRow(Sec, BindEffect(Effect, Tr, 'wave', { Label: 'Release wave pattern', Options: [['Corner (derez)', 'corner'], ['Melt (patchy)', 'melt'], ['Glitch (blocky)', 'glitch']] }));
        if (Asm === 0)
            C.SelectRow(Sec, BindEffect(Effect, Tr, 'after', { Label: 'Post-hold outcome', Options: [['Standard fall / burst', 'none'], ['Morph to butterflies', 'butterfly']] }));
        Range(Sec, BindEffect(Effect, Tr, 'holdBase', { Label: 'Hold duration' }), 0, 10, 0.05, 2, 's');
        Range(Sec, BindEffect(Effect, Tr, 'holdSpread', { Label: 'Wave spread' }), 0, 10, 0.05, 2, 's');
        if (Asm === 0)
        {
            Range(Sec, BindEffect(Effect, Tr, 'burstShare', { Label: 'Outward burst fraction' }), 0, 1, 0.02, 2, '');
            if (P.shape === 4)
            {
                const FragB = BindEffect(Effect, Tr, 'fragment', { Label: 'Shatter into 8 cubes on floor impact' });
                const OrigFragW = FragB.Write;
                FragB.Write = V => { OrigFragW(!!V); ConstructInspectorPage(); };
                C.SwitchRow(Sec, FragB);
                if (Tr.fragment)
                    Range(Sec, BindEffect(Effect, Tr, 'childLife', { Label: 'Shatter fragment lifetime' }), 0.2, 4.0, 0.05, 2, 's');
            }
        }
        else
        {
            Range(Sec, BindEffect(Effect, Tr, 'shell', { Label: Asm === 1 ? 'Outer rebuild shell radius' : 'Column drop height' }), 0.5, 8.0, 0.1, 1, 'm');
        }
    }
    if (P.blackHole && P.attractor)
    {
        const BH = P.blackHole, A = P.attractor;
        const Sec = C.SectionCard(Page, { Title: 'Black hole lensing & well', Glyph: 'Singularity', Badge: 'RELATIVITY' }).Inner;
        Range(Sec, BindEffect(Effect, BH, 'horizon', { Label: 'Event horizon radius' }), 0.1, 2, 0.01, 2, 'm');
        Range(Sec, BindEffect(Effect, BH, 'einstein', { Label: 'Einstein ring lensing' }), 0, 6, 0.05, 2, '×');
        Range(Sec, BindEffect(Effect, A, 'strength', { Label: 'Gravity pull strength' }), 0, 30, 0.1, 1, 'm/s²');
        Range(Sec, BindEffect(Effect, A, 'swirl', { Label: 'Accretion swirl' }), 0, 12, 0.05, 2, 'm/s²');
        Range(Sec, BindEffect(Effect, A, 'swallow', { Label: 'Horizon swallow radius' }), 0, 2.5, 0.02, 2, 'm');
    }
    else if (P.attractor)
    {
        const A = P.attractor;
        const Sec = C.SectionCard(Page, { Title: 'Particle attractor well', Glyph: 'Attract', Badge: 'GRAVITY WELL' }).Inner;
        C.ToggleTile(Sec, BindEffect(Effect, A, 'enabled', { Label: 'Attractor active', Glyph: 'Attract' }));
        Range(Sec, BindEffect(Effect, A, 'strength', { Label: 'Pull strength' }), 0, 30, 0.1, 1, 'm/s²');
        Range(Sec, BindEffect(Effect, A, 'swirl', { Label: 'Tangential swirl' }), 0, 12, 0.05, 2, 'm/s²');
        Range(Sec, BindEffect(Effect, A, 'radius', { Label: 'Influence radius' }), 0.5, 25, 0.25, 2, 'm');
    }
    if (P.heatShimmer)
    {
        const Sh = P.heatShimmer;
        const Sec = C.SectionCard(Page, { Title: 'Heat shimmer distortion', Glyph: 'Heat', Badge: 'OPTICAL' }).Inner;
        Range(Sec, BindEffect(Effect, Sh, 'radius', { Label: 'Refraction radius' }), 0.2, 6, 0.05, 2, 'm');
        Range(Sec, BindEffect(Effect, Sh, 'strength', { Label: 'Distortion amplitude' }), 0, 0.08, 0.001, 3, '');
        Range(Sec, BindEffect(Effect, Sh, 'freq', { Label: 'Thermal ripple frequency' }), 2, 32, 0.5, 1, 'Hz');
        Range(Sec, BindEffect(Effect, Sh, 'rate', { Label: 'Rising plume speed' }), 0.2, 5, 0.05, 2, '×');
    }
    if (P.magneticField)
    {
        const M = P.magneticField;
        const Sec = C.SectionCard(Page, { Title: 'Magnetic dipole field', Glyph: 'Magnet', Badge: 'DIPOLE' }).Inner;
        Range(Sec, BindEffect(Effect, M, 'strength', { Label: 'Drive strength' }), 0, 10, 0.05, 2, 'm/s²');
        Range(Sec, BindEffect(Effect, M, 'guide', { Label: 'Field-line guide' }), 0, 12, 0.05, 2, '1/s');
        Range(Sec, BindEffect(Effect, M, 'radius', { Label: 'Dipole field reach' }), 1, 16, 0.1, 1, 'm');
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

function ResolveFormulaTarget(Src)
{
    if (Src) return Src;
    const Match = Scene.FieldScene.Sources.find(S => S.Token === Selected.replace(/^src:/, ''));
    return Match || Scene.FieldScene.Sources[0] || null;
}

function ConstructForcesInspector(Page, FocusMode, Src)
{
    if (!FORCE_TABS.has(InspectorTab)) InspectorTab = 'field';
    RenderInspectorTabs([
        ['field', 'Field'],
        ['formula', 'Formula'],
        ['layers', 'Layers'],
        ['cache', 'Cache']
    ]);

    if (InspectorTab === 'field') ConstructForcesFieldTab(Page, FocusMode, Src);
    else if (InspectorTab === 'formula') ConstructForcesFormulaTab(Page, Src);
    else if (InspectorTab === 'layers') ConstructForcesLayersTab(Page);
    else ConstructForcesCacheTab(Page, Src);
}

function ConstructForcesFieldTab(Page, FocusMode, Src)
{
    const Vz = Scene.FieldScene.Visualization;
    const W = Scene.FieldScene.Wind;

    if (FocusMode === 'field')
    {
        // Card 1: Full Beaufort Wind Anemometer (.pcard.wf-trace) replacing the old composite hero card
        const AnemBadge = C.Construct('span', 'mp-badge', 'Bft 0 · Calm');
        AnemBadge.id = 'AnemBeaufortBadge';
        const PlaceBtn = C.Construct('button', 'mp-place-btn', Scene.AnemometerPlacing ? 'Click Viewport…' : 'Place in Viewport');
        PlaceBtn.type = 'button';
        PlaceBtn.id = 'btn-place-anemometer';
        PlaceBtn.title = 'Click anywhere in the 3D viewport to place the Anemometer, or drag the pin anytime';
        PlaceBtn.classList.toggle('active', !!Scene.AnemometerPlacing);
        PlaceBtn.addEventListener('click', () =>
        {
            Scene.ProbeEnabled = true;
            Scene.AnemometerPlacing = !Scene.AnemometerPlacing;
            SyncAnemometerToolState();
            C.Broadcast();
        });

        const ToggleBtn = C.Construct('button', 'mp-place-btn', Scene.ProbeEnabled ? 'In Viewport' : 'Hidden');
        ToggleBtn.type = 'button';
        ToggleBtn.id = 'btn-toggle-anemometer';
        ToggleBtn.classList.toggle('active', !!Scene.ProbeEnabled);
        ToggleBtn.addEventListener('click', () =>
        {
            Scene.ProbeEnabled = !Scene.ProbeEnabled;
            if (!Scene.ProbeEnabled) Scene.AnemometerPlacing = false;
            SyncAnemometerToolState();
            C.Broadcast();
        });

        const AnemNum = C.Construct('div', 'mp-num');
        AnemNum.id = 'AnemSpeedDisplay';
        const AnemDirPill = C.Construct('div', 'mp-dir-pill', 'N · 0°');
        AnemDirPill.id = 'AnemDirPill';
        const SvgNode = (Tag, Attrs, Kids) =>
        {
            const El = C.ConstructSvg(Tag, Attrs);
            if (Array.isArray(Kids)) for (const K of Kids) if (K) El.append(K);
            return El;
        };
        const AnemDialSvg = SvgNode('svg', { viewBox: '0 0 42 42', width: 38, height: 38, style: 'flex-shrink:0' });
        const DialRing = SvgNode('circle', { cx: 21, cy: 21, r: 18, fill: 'rgba(13,18,24,0.85)', stroke: 'rgba(146,216,194,0.34)', 'stroke-width': 1.2 });
        const DialVane = SvgNode('g', { transform: 'translate(21 21)' }, [
            SvgNode('polygon', { points: '0,-16 -3,-4 3,-4', fill: '#f5c451' }),
            SvgNode('polygon', { points: '0,15 -2.2,5 2.2,5', fill: 'rgba(146,216,194,0.55)' })
        ]);
        const DialRotor = SvgNode('g', { transform: 'translate(21 21)' }, [
            SvgNode('line', { x1: 0, y1: 0, x2: 0, y2: -9, stroke: '#e8f5ef', 'stroke-width': 1.1 }),
            SvgNode('circle', { cx: 0, cy: -9, r: 2.1, fill: '#92d8c2' }),
            SvgNode('line', { x1: 0, y1: 0, x2: 7.8, y2: 4.5, stroke: '#e8f5ef', 'stroke-width': 1.1 }),
            SvgNode('circle', { cx: 7.8, cy: 4.5, r: 2.1, fill: '#92d8c2' }),
            SvgNode('line', { x1: 0, y1: 0, x2: -7.8, y2: 4.5, stroke: '#e8f5ef', 'stroke-width': 1.1 }),
            SvgNode('circle', { cx: -7.8, cy: 4.5, r: 2.1, fill: '#92d8c2' }),
            SvgNode('circle', { cx: 0, cy: 0, r: 2.4, fill: '#1e2832', stroke: '#f5c451', 'stroke-width': 1 })
        ]);
        AnemDialSvg.append(DialRing, DialVane, DialRotor);
        const AnemCanvas = document.createElement('canvas');
        AnemCanvas.id = 'AnemometerCanvas';
        AnemCanvas.height = 78;
        const AnemDesc = C.Construct('div', 'mp-spec');
        AnemDesc.id = 'AnemDescText';

        const AnemKnots = C.Construct('b', null, '0.0 kt'); AnemKnots.id = 'AnemKnots';
        const AnemKmh = C.Construct('b', null, '0.0'); AnemKmh.id = 'AnemKmh';
        const AnemDynQ = C.Construct('b', null, '0 Pa'); AnemDynQ.id = 'AnemDynQ';
        const AnemAlt = C.Construct('b', null, `${Scene.Probe[1].toFixed(1)} m`); AnemAlt.id = 'AnemAlt';
        const AnemPeak = C.Construct('b', null, '0.0 m/s'); AnemPeak.id = 'AnemPeak';
        const AnemLull = C.Construct('b', null, '0.0 m/s'); AnemLull.id = 'AnemLull';
        const BeaufortPin = C.Construct('span', 'wf-scale-pin');
        BeaufortPin.id = 'BeaufortPin';

        const AnemCard = C.Construct('div', 'pcard wf-trace', [
            C.Construct('div', 'mp-hd', [
                C.Construct('span', 'mp-k', 'Anemometer · Probe Wind'),
                C.Construct('div', 'mp-hd-actions', [PlaceBtn, ToggleBtn, AnemBadge])
            ]),
            C.Construct('div', 'mp-num-row', [AnemNum, AnemDialSvg, AnemDirPill]),
            AnemCanvas,
            C.Construct('div', 'mp-axis', [
                C.Construct('span', null, '-30 s'),
                C.Construct('span', null, '-15 s'),
                C.Construct('span', null, 'now')
            ]),
            AnemDesc,
            C.Construct('div', 'mp-rail', [
                C.Construct('div', 'mp-rail-cell', [C.Construct('small', null, 'Knots'), AnemKnots]),
                C.Construct('div', 'mp-rail-cell', [C.Construct('small', null, 'km/h'), AnemKmh]),
                C.Construct('div', 'mp-rail-cell', [C.Construct('small', null, 'Dyn q'), AnemDynQ]),
                C.Construct('div', 'mp-rail-cell', [C.Construct('small', null, 'Altitude'), AnemAlt])
            ]),
            C.Construct('div', 'mp-duo', [
                C.Construct('div', 'mp-duo-box', [C.Construct('small', null, 'Gusting to (peak)'), AnemPeak]),
                C.Construct('div', 'mp-duo-box', [C.Construct('small', null, 'Lulling to (lull)'), AnemLull])
            ]),
            C.Construct('div', 'wf-scale-bar', BeaufortPin),
            C.Construct('div', 'wf-scale-ticks', [
                C.Construct('span', null, '0 Calm'), C.Construct('span', null, '5 Fresh'),
                C.Construct('span', null, '8 Gale'), C.Construct('span', null, '12 Hurricane')
            ])
        ]);
        Page.append(AnemCard);

        // Card 2: Probe Coordinates, Differential Tensor Readout & Overlay Contribution
        const FlowVal = C.Construct('b', null, '0.0 m/s');
        const AccelVal = C.Construct('b', null, '0.0 m/s²');
        const CurlVal = C.Construct('b', null, '0.000 /s');
        const DivVal = C.Construct('b', null, '+0.000 /s');
        const ProbeSec = C.SectionCard(Page, { Title: 'Anemometer placement & probe tensor', Glyph: 'Crosshair', Badge: '3D PIN · DRAGGABLE' }).Inner;
        C.PillTabs(ProbeSec, {
            Options: [['Flow · m/s', 'Flow'], ['Accelerate · m/s²', 'Accelerate'], ['Damp · 1/s', 'Damp']],
            Read: () => Scene.Overlay.Contribution,
            Write: Cont =>
            {
                Scene.Overlay.Contribution = Cont;
                Scene.Overlay.Enabled = true;
                const Sel = document.getElementById('overlay-channel-select');
                if (Sel) Sel.value = Cont;
                AppendLog('VIEW', `Overlay set to ${Cont}`, null, 'live');
            }
        });
        C.VectorRow(ProbeSec, BindScene(Scene, 'Probe', { Label: 'Anemometer position (X, Y, Z) · Drag pin in viewport', Unit: 'm', Step: 0.05, Digits: 2, Minimum: -6, Maximum: 8 }, 'WIND', 'Probe'));
        ProbeSec.append(C.Construct('div', 'probe-tensor-grid', [
            C.Construct('div', 'probe-tensor-cell', [C.Construct('small', null, 'Flow Velocity'), FlowVal]),
            C.Construct('div', 'probe-tensor-cell', [C.Construct('small', null, 'Acceleration'), AccelVal]),
            C.Construct('div', 'probe-tensor-cell', [C.Construct('small', null, 'Vorticity |∇×F|'), CurlVal]),
            C.Construct('div', 'probe-tensor-cell', [C.Construct('small', null, 'Divergence ∇·F'), DivVal])
        ]));

        LiveRefreshers.push(() =>
        {
            const [Px, Py, Pz] = Scene.Probe;
            const Prep = Scene.PreparedField || FC().Prepare(Scene.FieldScene, Scene.Time);
            const All = FC().EvaluateAllPrepared(Scene.FieldScene, Prep, Px, Py, Pz, 0.5, Vz.Solo, 'Particles');
            const Cont = Scene.Overlay.Contribution;
            const Diff = FC().SampleDifferential(Scene.FieldScene, Prep, Cont === 'Damp' ? 'Flow' : Cont, Px, Py, Pz, 0.25, Vz.Solo, 'Particles');

            if (AnemHistory.length < 2)
            {
                while (AnemHistory.length < 24) AnemHistory.push(All.FlowMag);
            }

            let Peak = 0, Lull = Infinity, Sum = 0;
            for (const S of AnemHistory) { Sum += S; if (S > Peak) Peak = S; if (S < Lull) Lull = S; }
            const Mean = AnemHistory.length ? Sum / AnemHistory.length : All.FlowMag;
            const Bf = FC().ResolveBeaufort(Mean);

            const Str = All.FlowMag.toFixed(1);
            const [IntPart, DecPart] = Str.split('.');
            AnemNum.innerHTML = `${IntPart}<span class="mp-dec">.${DecPart}</span><span class="mp-unit">m/s</span>`;

            const Horiz = Math.hypot(All.Velocity[0], All.Velocity[2]);
            const Bearing = Horiz > 0.05 ? Wrap360(Math.atan2(All.Velocity[0], -All.Velocity[2]) * 180 / Math.PI) : 0;
            AnemDirPill.textContent = `${C.ResolveCompassPoint(Bearing)} · ${Math.round(Bearing)}°`;
            DialVane.setAttribute('transform', `translate(21 21) rotate(${Bearing.toFixed(1)})`);
            const SpinDeg = ((Scene.Time * (1.2 + All.FlowMag * 1.45) * 180) / Math.PI) % 360;
            DialRotor.setAttribute('transform', `translate(21 21) rotate(${SpinDeg.toFixed(1)})`);

            AnemBadge.textContent = `Bft ${Bf.Force} · ${Bf.Name}`;
            AnemDesc.innerHTML = `<strong>Beaufort ${Bf.Force}</strong> — ${Bf.Land}.`;
            AnemKnots.textContent = `${(All.FlowMag * 1.94384).toFixed(1)} kt`;
            AnemKmh.textContent = `${(All.FlowMag * 3.6).toFixed(1)}`;
            const DynQ = 0.5 * 1.225 * All.FlowMag * All.FlowMag;
            AnemDynQ.textContent = `${Math.round(DynQ)} Pa`;
            AnemAlt.textContent = `${Py.toFixed(1)} m`;
            AnemPeak.textContent = `${Peak.toFixed(1)} m/s`;
            AnemLull.textContent = `${(Lull === Infinity ? All.FlowMag : Lull).toFixed(1)} m/s`;
            BeaufortPin.style.left = `${(Clamp(All.FlowMag / 36, 0, 0.99) * 100).toFixed(1)}%`;

            C.DrawWindRoseInfographic(AnemCanvas, All.FlowMag, Bearing, AnemHistory);

            FlowVal.textContent = `${All.FlowMag.toFixed(2)} m/s`;
            AccelVal.textContent = `${All.AccelMag.toFixed(2)} m/s²`;
            CurlVal.textContent = `${Diff.Curl.toFixed(3)} /s`;
            DivVal.textContent = `${Diff.Div >= 0 ? '+' : ''}${Diff.Div.toFixed(3)} /s`;
            SyncAnemometerToolState();
        });

        MountInspectorVectorPreview(Page, null);

        const ActiveSec = C.SectionCard(Page, { Title: 'Placed force sources', Glyph: 'Target', Badge: `${Scene.FieldScene.Sources.length} IN SCENE` }).Inner;
        if (!Scene.FieldScene.Sources.length)
        {
            ActiveSec.append(C.Construct('p', 'property-hint', 'No force sources placed yet. Click + in the Outliner header to add a Flow, Accelerate, Damp, or Formula source.'));
        }
        else
        {
            const List = C.Construct('div', 'force-list');
            for (const S of Scene.FieldScene.Sources)
            {
                const Info = FC().TypeById[S.Type];
                if (!Info) continue;
                const Dot = C.Construct('div', 'row-glyph', C.ConstructGlyph(Info.Glyph || 'Target'));
                Dot.style.color = S.UseFormula ? '#ffb454' : (Info.Colour || ContributionColours[Info.Contribution]);
                const St = FC().ResolveActivation(S, Scene.Time).Status;
                const Item = C.Construct('button', 'force-item', [
                    Dot,
                    C.Construct('div', null, [
                        C.Construct('div', 'row-name', S.Name),
                        C.Construct('div', 'force-item-meta', SourceMetaSummary(S))
                    ]),
                    C.Construct('div', 'force-item-val', (StatusText[St] || 'off').toUpperCase())
                ]);
                Item.type = 'button';
                Item.addEventListener('click', () => SelectNode('src:' + S.Token, false));
                List.append(Item);
            }
            ActiveSec.append(List);
        }
        return;
    }

    if (FocusMode === 'wind')
    {
        C.MetricTrio(Page, [
            { Label: 'Base speed', Resolve: () => [`${W.Speed.toFixed(1)} m/s`, FC().ResolveBeaufort(W.Speed).Name] },
            { Label: 'Heading', Resolve: () => [`${Math.round(W.Bearing)}°`, C.ResolveCompassPoint(W.Bearing)] },
            { Label: 'Lattice gain', Highlight: true, Resolve: () => [`×${Scene.Wind.windScale.toFixed(2)}`, '24·12·24 grid'] }
        ]);
        MountInspectorVectorPreview(Page, null);

        const Sec = C.SectionCard(Page, { Title: 'Direction, speed & bounded region', Glyph: 'Wind', Badge: W.Enabled ? 'ACTIVE' : 'OFF' }).Inner;
        C.SwitchRow(Sec, BindScene(W, 'Enabled', { Label: 'Ambient wind enabled' }, 'WIND', 'AmbientWind'));
        C.PolarDial(Sec, {
            Label: 'Heading (blow toward azimuth)',
            Read: () => W.Bearing,
            Write: Deg => { W.Bearing = Deg; AppendLog('WIND', 'Ambient heading', `${Deg.toFixed(0)}°`, 'live'); },
            Resolve: () => ({ Caption: `Heading ${Math.round(W.Bearing)}° · ${C.ResolveCompassPoint(W.Bearing)}` })
        });
        Range(Sec, BindScene(W, 'Speed', { Label: 'Base speed' }, 'WIND', 'AmbientWind'), 0, 20, 0.1, 1, 'm/s');
        Range(Sec, BindScene(W, 'Turbulence', { Label: 'Eddy turbulence' }, 'WIND', 'AmbientWind'), 0, 1, 0.02, 2, '');
        C.SwitchRow(Sec, BindScene(W, 'Bounded', { Label: 'Bounded inside chamber region' }, 'WIND', 'AmbientWind'));
        Range(Sec, BindScene(W, 'RegionRadius', {
            Label: 'Chamber region radius',
            Availability: () => W.Bounded ? { Enabled: true } : { Enabled: false, Reason: 'Enable bounded chamber region first' }
        }, 'WIND', 'AmbientWind'), 3, 24, 0.25, 2, 'm');

        const ProfSec = C.SectionCard(Page, { Title: 'Gusting & vertical altitude profile', Glyph: 'Activity', Badge: 'HEIGHT LAW' }).Inner;
        Range(ProfSec, BindScene(W, 'GustStrength', { Label: 'Gust amplitude' }, 'WIND', 'AmbientWind'), 0, 1, 0.02, 2, '×');
        Range(ProfSec, BindScene(W, 'GustFrequency', { Label: 'Gust frequency' }, 'WIND', 'AmbientWind'), 0.02, 1.5, 0.02, 2, 'Hz');
        Range(ProfSec, BindScene(W, 'TurbulenceScale', { Label: 'Turbulence length scale' }, 'WIND', 'AmbientWind'), 0.4, 8, 0.1, 1, 'm');
        Range(ProfSec, BindScene(W, 'Shear', { Label: 'Shear per km altitude' }, 'WIND', 'AmbientWind'), 0, 0.8, 0.02, 2, '');
        Range(ProfSec, BindScene(W, 'Veer', { Label: 'Veer degrees per km' }, 'WIND', 'AmbientWind'), -45, 45, 1, 0, '°');
        return;
    }

    if (!Src) return;
    const Info = FC().TypeById[Src.Type];
    if (!Info) return;
    const EffectiveContrib = Src.UseFormula
        ? (FC().EnsureCompiledFormula(Src)?.OutputChannel === 'Flow' ? 'Flow' : 'Accelerate')
        : Info.Contribution;
    const Unit = ContributionUnits[EffectiveContrib];

    const Metric = C.Construct('div', 'metric-big');
    const Rate = C.Construct('div', 'rate');
    const Low = C.Construct('div', 'low');
    const Card = C.Construct('div', 'hero-card', [
        C.Construct('div', 'hero-top', [
            C.Construct('div', 'budget-chip', [C.Construct('div', 'mark live', C.ConstructGlyph('Check')), C.Construct('span', null, `${EffectiveContrib} · ${Src.UseFormula ? 'Symbolic Formula' : Info.Name}`)]),
            C.Construct('div', 'estimate-tag', Src.UseFormula ? 'FX(GPU/CPU)' : Info.Native.toUpperCase())
        ]),
        C.Construct('div', 'hero-title', Src.UseFormula ? (Src.FormulaCode || FC().DEFAULT_FORMULA_CODE) : Info.Blurb),
        C.Construct('div', 'hero-metrics', [Metric, C.Construct('div', 'metric-side', [Rate, Low])])
    ]);
    Page.append(Card);

    const ForceGraphic = C.ForceFieldInfographic(Card, Scene, Src, FC(), () => {
        RefreshLive();
        C.Broadcast();
    });
    LiveRefreshers.push(() => ForceGraphic.Refresh());

    LiveRefreshers.push(() =>
    {
        const St = FC().ResolveActivation(Src, Scene.Time).Status;
        C.ConstructMetric(Metric, Math.abs(Src.Strength), 2, Unit);
        Rate.textContent = `${Src.Radius.toFixed(1)} m`;
        Low.textContent = `${Src.Reach} · ${StatusText[St]}`;
    });

    MountInspectorVectorPreview(Page, Src);

    const Acts = [
        { Label: 'Solo', Glyph: 'Focus', Act: () => { Scene.FieldScene.Visualization.Solo = Scene.FieldScene.Visualization.Solo === Src.Token ? null : Src.Token; C.Broadcast(); } },
        { Label: Src.UseFormula ? 'Edit fx' : 'Formula', Glyph: 'Zap', Act: () => { InspectorTab = 'formula'; ConstructInspectorPage(); } },
        { Label: 'Duplicate', Glyph: 'Copy', Act: () => DuplicateForceSource(Src) },
        { Label: 'Delete', Glyph: 'Trash', Tone: 'danger', Act: () => RemoveForceSource(Src) }
    ];
    if (Src.Activation === 'Trigger')
        Acts.unshift({ Label: 'Trigger', Glyph: 'Zap', Tone: 'accent', Act: () => { Src.TriggeredAt = Scene.Time; Src.Enabled = true; AppendLog('FORCES', `Triggered ${Src.Name}`, null, 'live'); } });
    C.ActionRow(Page, Acts);

    const StrSec = C.SectionCard(Page, { Title: 'Strength & direction', Glyph: Info.Glyph || 'Target', Badge: EffectiveContrib.toUpperCase() }).Inner;
    for (const [Key, Label] of Info.Bearings || [])
    {
        C.PolarDial(StrSec, {
            Label,
            Read: () => Src[Key] || 0,
            Write: Deg => { Src[Key] = Deg; AppendLog('FORCES', `${Src.Name}: ${Label}`, `${Math.round(Deg)}°`, 'live'); },
            Resolve: () => ({ Caption: `Heading ${Math.round(Src[Key] || 0)}° · ${C.ResolveCompassPoint(Src[Key] || 0)}` })
        });
    }
    if (Info.Spin)
    {
        C.SegmentedRow(StrSec, {
            Label: 'Rotation sense',
            Options: [['Cyclonic (+)', 1], ['Anticyclonic (−)', -1]],
            Read: () => Src.Spin < 0 ? -1 : 1,
            Write: V => { Src.Spin = V; }
        });
    }
    for (const P of Info.Parameters)
    {
        const B = ParameterBounds(P, Scene.FieldScene);
        Range(StrSec, BindScene(Src, P.Key, { Label: P.Label }, 'FORCES', Src.Name), B.Min, B.Max, P.Step, P.Digits, P.Unit);
    }

    const ReachSec = C.SectionCard(Page, { Title: 'Placement & finite influence', Glyph: 'Target', Badge: `${Src.Reach.toUpperCase()} · ${Src.Falloff.toUpperCase()}` }).Inner;
    C.VectorRow(ReachSec, BindScene(Src, 'Centre', { Label: 'World centre (X, Y, Z)', Unit: 'm', Step: 0.05, Digits: 2, Minimum: -12, Maximum: 12 }, 'FORCES', Src.Name));
    C.SegmentedRow(ReachSec, {
        Label: 'Bounded shape',
        Options: FC().Reaches.map(R => [R, R]),
        Read: () => Src.Reach,
        Write: R => { Src.Reach = R; ConstructInspectorPage(); }
    });
    if (Src.Reach === 'Box')
    {
        Range(ReachSec, BindScene(Src, 'HalfX', { Label: 'Half extent X' }, 'FORCES', Src.Name), 0.3, 12, 0.05, 2, 'm');
        Range(ReachSec, BindScene(Src, 'HalfZ', { Label: 'Half extent Z' }, 'FORCES', Src.Name), 0.3, 12, 0.05, 2, 'm');
        Range(ReachSec, BindScene(Src, 'Height', { Label: 'Box height' }, 'FORCES', Src.Name), 0.5, 16, 0.05, 2, 'm');
    }
    else
    {
        Range(ReachSec, BindScene(Src, 'Radius', { Label: 'Influence radius' }, 'FORCES', Src.Name), 0.3, 14, 0.05, 2, 'm');
        if (Src.Reach === 'Column')
            Range(ReachSec, BindScene(Src, 'Height', { Label: 'Column height' }, 'FORCES', Src.Name), 0.5, 16, 0.05, 2, 'm');
    }
    C.SegmentedRow(ReachSec, {
        Label: 'Falloff law',
        Options: [['Smooth', 'Smooth'], ['Linear', 'Linear'], ['Inv²', 'InverseSquare'], ['None', 'None']],
        Read: () => Src.Falloff,
        Write: F => { Src.Falloff = F; }
    });

    const TimeSec = C.SectionCard(Page, { Title: 'Timing & target channels', Glyph: 'Clock', Badge: Src.Activation.toUpperCase() }).Inner;
    C.SegmentedRow(TimeSec, {
        Label: 'Activation mode',
        Options: FC().Activations.map(A => [A, A]),
        Read: () => Src.Activation,
        Write: A => { Src.Activation = A; ConstructInspectorPage(); }
    });
    if (Src.Activation === 'Window')
    {
        Range(TimeSec, BindScene(Src, 'Begins', { Label: 'Window opens at' }, 'FORCES', Src.Name), 0, 60, 0.1, 1, 's');
        Range(TimeSec, BindScene(Src, 'Lasts', { Label: 'Active duration (0=forever)' }, 'FORCES', Src.Name), 0, 60, 0.1, 1, 's');
        Range(TimeSec, BindScene(Src, 'Repeats', { Label: 'Repeat period (0=once)' }, 'FORCES', Src.Name), 0, 60, 0.1, 1, 's');
        Range(TimeSec, BindScene(Src, 'RampIn', { Label: 'Ramp-in time' }, 'FORCES', Src.Name), 0, 5, 0.05, 2, 's');
        Range(TimeSec, BindScene(Src, 'RampOut', { Label: 'Ramp-out time' }, 'FORCES', Src.Name), 0, 5, 0.05, 2, 's');
    }
    else if (Src.Activation === 'Trigger')
    {
        Range(TimeSec, BindScene(Src, 'Lasts', { Label: 'Pulse duration' }, 'FORCES', Src.Name), 0.2, 30, 0.1, 1, 's');
        C.ActionRow(TimeSec, [{ Label: 'Fire Trigger Now', Glyph: 'Zap', Tone: 'accent', Act: () => { Src.TriggeredAt = Scene.Time; Src.Enabled = true; AppendLog('FORCES', `Triggered ${Src.Name}`, null, 'live'); } }]);
    }
    C.ChipToggles(TimeSec, {
        Label: 'Applies to channels (empty = all)',
        Options: FC().Channels,
        Read: () => Src.Acts,
        Write: List => { Src.Acts = List; },
        Detail: List => List.length ? `${List.length} selective channel(s)` : 'Affects all 7 particle & world channels'
    });
}

function ConstructForcesFormulaTab(Page, Src)
{
    const Target = ResolveFormulaTarget(Src);
    if (!Target)
    {
        const Empty = C.SectionCard(Page, { Title: 'Symbolic Vector Language', Glyph: 'Zap', Badge: 'NO SOURCE' }).Inner;
        Empty.append(C.Construct('p', 'property-hint', 'No force source exists yet. Use the Outliner + button to add a Custom Formula Field or any force type, then edit its equation here.'));
        return;
    }
    if (!Number.isFinite(Target.Strength)) Target.Strength = 8;
    if (!Number.isFinite(Target.Swirl)) Target.Swirl = 2.5;

    if (!Src && Scene.FieldScene.Sources.length > 1)
    {
        const Picker = C.Construct('div', 'formula-target-row');
        for (const Item of Scene.FieldScene.Sources)
        {
            const Pill = C.Construct('button', 'formula-target-pill' + (Item === Target ? ' active' : ''), Item.Name);
            Pill.type = 'button';
            Pill.addEventListener('click', () => { SelectNode('src:' + Item.Token, false); });
            Picker.append(Pill);
        }
        Page.append(Picker);
    }

    const Sec = C.SectionCard(Page, { Title: 'Symbolic GPU / CPU language', Glyph: 'Zap', Badge: Target.Name }).Inner;
    C.SwitchRow(Sec, {
        Label: 'Evaluate custom formula',
        Read: () => !!Target.UseFormula,
        Write: On =>
        {
            Target.UseFormula = !!On;
            if (!Target.FormulaCode) Target.FormulaCode = FC().DEFAULT_FORMULA_CODE;
            FC().EnsureCompiledFormula(Target);
            AppendLog('FORCES', `${Target.Name}: formula ${On ? 'active' : 'off'}`, null, 'live');
            ConstructOutliner();
            ConstructInspectorPage();
        }
    });

    const PresetRow = C.Construct('div', 'vfl-preset-row');
    for (const P of FC().FORMULA_PRESETS)
    {
        const Btn = C.Construct('button', 'vfl-preset-btn', P.Name);
        Btn.type = 'button';
        Btn.addEventListener('click', () =>
        {
            Target.FormulaCode = P.Code;
            Target.UseFormula = true;
            Ta.value = P.Code;
            ApplyCode(P.Code);
            ConstructOutliner();
            AppendLog('FORCES', `${Target.Name}: loaded preset '${P.Name}'`, null, 'live');
        });
        PresetRow.append(Btn);
    }
    Sec.append(C.Construct('div', 'section-subhead', [C.Construct('span', null, 'Presets'), C.Construct('span', null, 'VectorFieldLang')]), PresetRow);

    const QuickSymbols = [
        ['·', ' · '], ['×', ' × '], ['r̂', 'r̂'], ['x̂', 'x̂'], ['ŷ', 'ŷ'], ['ẑ', 'ẑ'],
        ['θ̂', 'θ̂'], ['ω_swirl', 'ω_swirl'], ['A(r)', 'A(r)'], ['S', 'S'], ['R', 'R'], ['curlNoise', 'curlNoise(p, 4.0)']
    ];
    const Palette = C.Construct('div', 'vfl-palette-bar');
    for (const [Label, Ins] of QuickSymbols)
    {
        const B = C.Construct('button', 'vfl-sym-btn', Label);
        B.type = 'button';
        B.addEventListener('click', () =>
        {
            const S0 = Ta.selectionStart ?? Ta.value.length;
            const S1 = Ta.selectionEnd ?? Ta.value.length;
            Ta.value = Ta.value.slice(0, S0) + Ins + Ta.value.slice(S1);
            Ta.focus();
            Ta.setSelectionRange(S0 + Ins.length, S0 + Ins.length);
            ApplyCode(Ta.value);
        });
        Palette.append(B);
    }
    Sec.append(Palette);

    const Code = Target.FormulaCode || FC().DEFAULT_FORMULA_CODE;
    const Res = FC().CompileFieldProgram(Code, 0);

    const Wrap = C.Construct('div', 'vfl-editor-wrap' + (Res.Ok ? '' : ' has-err'));
    const Pre = C.Construct('pre', 'vfl-code-highlight');
    Pre.setAttribute('aria-hidden', 'true');
    Pre.innerHTML = Res.HighlightHtml;
    const Ta = document.createElement('textarea');
    Ta.className = 'vfl-code-input';
    Ta.spellcheck = false;
    Ta.rows = 5;
    Ta.value = Code;
    Wrap.append(Pre, Ta);
    Sec.append(Wrap);

    const Diag = C.Construct('div', 'vfl-diag-box ' + (Res.Ok ? 'ok' : 'err'));
    const GlslPre = C.Construct('pre', 'vfl-glsl-pre', Res.Ok ? Res.GlslCode : '// Fix syntax error above to emit GPU kernel');
    const SyncDiag = Compiled =>
    {
        Pre.innerHTML = Compiled.HighlightHtml;
        Wrap.classList.toggle('has-err', !Compiled.Ok);
        if (Compiled.Ok)
        {
            Diag.className = 'vfl-diag-box ok';
            Diag.innerHTML = `✓ Compiled cleanly · Output: <b>${Compiled.OutputChannel}</b> (vec3) · Bound live across visualizer &amp; solver`;
            GlslPre.textContent = Compiled.GlslCode;
        }
        else
        {
            const D = Compiled.Diagnostics[0] || { Line: 1, Col: 1, Message: 'Invalid syntax' };
            Diag.className = 'vfl-diag-box err';
            Diag.textContent = `✗ Error (Ln ${D.Line}, Col ${D.Col}): ${D.Message}`;
        }
    };
    SyncDiag(Res);
    Sec.append(Diag);

    const ApplyCode = NextCode =>
    {
        Target.FormulaCode = NextCode;
        const Compiled = FC().EnsureCompiledFormula(Target, 0);
        SyncDiag(Compiled);
        C.Broadcast();
    };
    Ta.addEventListener('input', () => ApplyCode(Ta.value));
    Ta.addEventListener('scroll', () => { Pre.scrollTop = Ta.scrollTop; Pre.scrollLeft = Ta.scrollLeft; });

    Range(Sec, BindScene(Target, 'Strength', { Label: 'Strength (S)' }, 'FORCES', Target.Name), -30, 30, 0.25, 2, '');
    Range(Sec, BindScene(Target, 'Swirl', { Label: 'Swirl (ω_swirl)' }, 'FORCES', Target.Name), -25, 25, 0.25, 2, '');

    const Det = document.createElement('details');
    Det.className = 'vfl-details';
    const Sum = document.createElement('summary');
    Sum.textContent = 'View Emitted GPU GLSL ES 3.00 Kernel';
    Det.append(Sum, GlslPre);
    Sec.append(Det);
}

function ConstructForcesLayersTab(Page)
{
    const Vz = Scene.FieldScene.Visualization;

    C.PillTabs(Page, {
        Options: [['Flow · m/s', 'Flow'], ['Accelerate · m/s²', 'Accelerate'], ['Damp · 1/s', 'Damp']],
        Read: () => Scene.Overlay.Contribution,
        Write: Cont =>
        {
            Scene.Overlay.Contribution = Cont;
            Scene.Overlay.Enabled = true;
            const Sel = document.getElementById('overlay-channel-select');
            if (Sel) Sel.value = Cont;
            AppendLog('VIEW', `Layer contribution: ${Cont}`, null, 'live');
        }
    });

    const MapSec = C.SectionCard(Page, { Title: 'Slice, heatmap & colorimetry', Glyph: 'Layers', Badge: Scene.Overlay.Contribution.toUpperCase() }).Inner;
    C.SelectRow(MapSec, BindScene(Vz, 'Quantity', {
        Label: 'Evaluated scalar quantity',
        Options: [['Speed magnitude', 'Speed'], ['East component (+X)', 'AlongX'], ['South component (+Z)', 'AlongZ'], ['Vertical (+Y)', 'Vertical'], ['Vorticity (curl ω)', 'Vorticity'], ['Divergence (∇·v)', 'Divergence']]
    }, 'VIEW', 'Heatmap'));
    C.SegmentedRow(MapSec, BindScene(Scene.Overlay, 'HeatmapStyle', { Label: 'Heatmap style', Options: [['Smooth LUT', 'Smooth'], ['Contour isolines', 'Contour']] }, 'VIEW', 'Heatmap'));
    Range(MapSec, BindScene(Vz, 'SliceHeight', { Label: 'Slice plane elevation Y' }, 'VIEW', 'SliceY'), 0.05, 7.5, 0.05, 2, 'm');
    Range(MapSec, BindScene(Scene.Overlay, 'Opacity', { Label: 'Heatmap opacity' }, 'VIEW', 'Opacity'), 0.08, 0.95, 0.02, 2, '');
    Range(MapSec, BindScene(Vz, 'QuietCutoff', { Label: 'Quiet cutoff threshold' }, 'VIEW', 'Layers'), 0, 0.25, 0.005, 3, '×');

    const FlowSec = C.SectionCard(Page, { Title: '3D Vector field & volumetric flow lines', Glyph: 'FlowLine', Badge: '3D FIELD' }).Inner;
    C.SwitchRow(FlowSec, Object.assign(BindScene(Scene.Wind, 'showArrows', { Label: '3D Vector field arrows' }, 'VIEW', 'Lattice'), {
        Write: On => { Scene.Wind.showArrows = !!On; document.getElementById('toggle-arrows-btn')?.classList.toggle('active', !!On); }
    }));
    C.SelectRow(FlowSec, BindScene(Scene.Wind, 'arrowStyle', {
        Label: 'Vector field style',
        Options: [['Architectural flow dashes (Holst)', 2], ['Direction spectrum (VectorayGen)', 0], ['Engineering magnitude (Teal–Coral)', 1]]
    }, 'VIEW', 'Lattice'));
    Range(FlowSec, Object.assign(BindScene(Scene.Wind, 'arrowStride', { Label: 'Lattice grid stride' }, 'VIEW', 'Lattice'), {
        Write: V => { Scene.Wind.arrowStride = Math.round(V); }
    }), 1, 4, 1, 0, 'cells');
    Range(FlowSec, BindScene(Scene.Wind, 'arrowRef', { Label: 'Color scale ceiling' }, 'VIEW', 'Lattice'), 4, 28, 0.5, 1, 'm/s');
    C.SwitchRow(FlowSec, BindScene(Scene.Overlay, 'Streamlines3D', { Label: '3D RK4 volumetric ribbons' }, 'VIEW', 'Streamlines3D'));
    C.SwitchRow(FlowSec, BindScene(Vz, 'FlowLines', { Label: '2D ground slice flow lines' }, 'VIEW', 'FlowLines'));
    Range(FlowSec, Object.assign(BindScene(Vz, 'FlowLineSpacing', { Label: 'Slice line separation' }, 'VIEW', 'FlowLines'), { Write: V => { Vz.FlowLineSpacing = Math.round(V); Visualization?.Invalidate(); InspectorViz?.Invalidate(); } }), 18, 96, 1, 0, 'px');
    C.SwitchRow(FlowSec, BindScene(Vz, 'Arrows', { Label: '2D ground slice arrows' }, 'VIEW', 'Arrows'));

    Page.append(C.Construct('div', 'section-subhead', [C.Construct('span', null, 'Stage & chamber layers'), C.Construct('span', null, 'ForceFieldVisualization')]));
    const Grid = C.Construct('div', 'property-switches');
    C.ToggleTile(Grid, BindScene(Scene.Overlay, 'Enabled', { Label: 'Field overlay', Glyph: 'Layers' }, 'VIEW', 'Overlay'));
    C.ToggleTile(Grid, BindScene(Vz, 'Heatmap', { Label: 'Heatmap', Glyph: 'Palette' }, 'VIEW', 'Heatmap'));
    C.ToggleTile(Grid, BindScene(Vz, 'ShowInfluence', { Label: 'Influence rings', Glyph: 'Target' }, 'VIEW', 'Sources'));
    C.ToggleTile(Grid, BindScene(Vz, 'Labels', { Label: 'Source labels', Glyph: 'Check' }, 'VIEW', 'Labels'));
    C.ToggleTile(Grid, BindScene(Scene.Overlay, 'Bounds', { Label: '3D Envelopes', Glyph: 'Boxes' }, 'VIEW', 'Bounds'));
    C.ToggleTile(Grid, BindScene(Scene.Overlay, 'Streamlines3D', { Label: '3D Flow lines', Glyph: 'Rays' }, 'VIEW', 'Streamlines3D'));
    C.ToggleTile(Grid, Object.assign(BindScene(Scene.Wind, 'showArrows', { Label: '3D Vector field', Glyph: 'Wind' }, 'VIEW', 'Lattice'), {
        Write: On => { Scene.Wind.showArrows = !!On; document.getElementById('toggle-arrows-btn')?.classList.toggle('active', !!On); }
    }));
    Page.append(Grid);

    const ChSec = C.SectionCard(Page, { Title: 'Chamber grid & coupling', Glyph: 'Lattice', Badge: '12×8×12 m' }).Inner;
    Range(ChSec, BindScene(Scene.Wind, 'windScale', { Label: 'Particle wind coupling gain' }, 'WIND', 'Wind'), 0, 3, 0.02, 2, '×');
    C.SwitchRow(ChSec, BindScene(Scene.Wind, 'showFloor', { Label: 'Ground reference grid' }, 'VIEW', 'Lattice'));
    C.SwitchRow(ChSec, BindScene(Scene.Wind, 'showDomain', { Label: 'Domain wire bounding box' }, 'VIEW', 'Lattice'));
}

function ConstructForcesCacheTab(Page, Src)
{
    const Budget = FC().ResolveBudget(Scene.FieldScene);
    const CacheSec = C.SectionCard(Page, { Title: 'Cascade clipmap & GPU cache', Glyph: 'Boxes', Badge: `${(Budget.TotalBytes / 1024).toFixed(0)} KiB` }).Inner;

    C.MetricTrio(CacheSec, [
        { Label: 'Lattice f16 windTex', Resolve: () => [`${Budget.Baked.length} baked`, '110.6 KiB rgba16f'] },
        { Label: 'GPU accel / damp', Resolve: () => [`${Budget.Live.length + Budget.Damp.length} / ${Budget.GpuSlots}`, 'fieldBuf 1040 B'] },
        { Label: 'Cascade tiers', Highlight: true, Resolve: () => [`${Budget.Levels.length} tiers`, `${(Budget.CascadeBytes / 1024).toFixed(0)} KiB/tier`] }
    ]);

    const Tiers = C.Construct('div', 'cache-tiers-grid');
    Budget.Levels.forEach((Lv, Idx) =>
    {
        Tiers.append(C.Construct('div', 'cache-tier-cell', [
            C.Construct('small', null, `Cascade ${Idx} (${Lv.Refresh}× frame)`),
            C.Construct('b', null, `${Lv.Cell.toFixed(2)}m cell · ${Lv.Extent.toFixed(1)}m span`)
        ]));
    });
    CacheSec.append(Tiers);

    C.ActionRow(CacheSec, [{
        Label: 'Re-bake Lattice & Invalidate Cache', Glyph: 'Reset', Tone: 'accent',
        Act: () =>
        {
            for (const S of Scene.FieldScene.Sources) delete S._CompiledFormula;
            Scene.PreparedField = null;
            Visualization?.Invalidate();
            InspectorViz?.Invalidate();
            AppendLog('FORCES', 'Re-baked windTex & invalidated visualization cache', null, 'live');
            C.Toast('Re-baked wind lattice & cache');
        }
    }]);

    if (Src)
    {
        C.SegmentedRow(CacheSec, {
            Label: 'TOML record scope',
            Options: [['Full ForceFieldSystem', 'system'], ['Selected Source only', 'source']],
            Read: () => CacheRecordScope,
            Write: Mode => { CacheRecordScope = Mode; ConstructInspectorPage(); }
        });
    }

    const LinesFn = () => (Src && CacheRecordScope === 'source')
        ? FC().EncodeLevelspace(Scene.FieldScene, Src.Token)
        : FC().EncodeLevelspace(Scene.FieldScene);

    const TomlSec = C.SectionCard(Page, { Title: '.levelspace TOML Cache', Glyph: 'Copy', Badge: 'TOML RECORD', Open: true }).Inner;
    const Block = C.Construct('pre', 'levelspace');
    C.ActionRow(TomlSec, [{
        Label: 'Copy TOML Record', Glyph: 'Copy',
        Act: () => { navigator.clipboard?.writeText(LinesFn().join('\n')).then(() => C.Toast('TOML record copied')); }
    }]);
    TomlSec.append(Block);
    C.Register(() => { Block.textContent = LinesFn().join('\n'); });
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

function EncodeEffect(Effect)
{
    const Preset = PE.presetById(Effect.PresetId);
    const Lines = ['[[Records]]', `Identity = "${Effect.Token}"`, 'Type = "ParticleEffect"', `Name = "${Effect.Name}"`, '[Records.Configuration]', `Enabled = ${Effect.P.visible}`, `Preset = "${Effect.PresetId}"`];
    for (const [K, V] of Object.entries(Effect.P))
    {
        if (K === 'visible' || V === undefined || V === null) continue;
        if (typeof V === 'object' && !Array.isArray(V))
        {
            if (!V || JSON.stringify(V) === JSON.stringify(Preset.p[K])) continue;
            const PascalKey = K.charAt(0).toUpperCase() + K.slice(1);
            Lines.push(`${PascalKey} = ${JSON.stringify(V)}`);
            continue;
        }
        if (Array.isArray(V) && V.some(Item => Item && typeof Item === 'object'))
        {
            if (JSON.stringify(V) === JSON.stringify(Preset.p[K])) continue;
            const PascalKey = K.charAt(0).toUpperCase() + K.slice(1);
            Lines.push(`${PascalKey} = ${JSON.stringify(V)}`);
            continue;
        }
        if (JSON.stringify(V) === JSON.stringify(Preset.p[K])) continue;
        const Fmt = Array.isArray(V) ? `[${V.map(N => Number(N.toFixed(3))).join(', ')}]` : typeof V === 'string' ? `"${V}"` : typeof V === 'boolean' ? String(V) : String(Number(V.toFixed(4)));
        const PascalKey = K.charAt(0).toUpperCase() + K.slice(1);
        Lines.push(`${PascalKey} = ${Fmt}`);
    }
    return Lines;
}

function ConstructInspectorPage()
{
    if (Scene.ActiveInfographic?.Destroy) Scene.ActiveInfographic.Destroy();
    Scene.ActiveInfographic = null;
    InspectorViz = null;
    InspectorDebugCanvas = null;
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
    const Source = SourceByToken(Selected);
    EnabledEl.disabled = false;

    if (Effect)
    {
        const Preset = PE.presetById(Effect.PresetId);
        Crumb.textContent = `Inspector / Effects / ${Preset?.group || 'Particles'}`;
        NameInput.value = Effect.Name;
        NameInput.disabled = false;
        NameInput.onchange = () => { Effect.Name = NameInput.value.trim() || Effect.Name; ConstructOutliner(); C.Broadcast(); };
        TypeLabel.textContent = `${(CategoryLabels[Effect.P.kind] || 'Particles').toUpperCase()} · ${Effect.P.blend.toUpperCase()}`;
        SymEl.textContent = ''; SymEl.append(C.ConstructGlyph(GlyphForPreset(Preset)));
        SymEl.style.color = DisplayColour(Effect.P.colA);
        EnabledEl.checked = !!Effect.P.visible;
        EnabledEl.onchange = () => { Effect.P.visible = EnabledEl.checked; C.Broadcast(); };
        VpObj.textContent = Effect.Name;
        VpSub.textContent = `${CategoryLabels[Effect.P.kind] || 'Particles'} · eos engine`;
        ConstructEffectInspector(Page, Effect);
    }
    else if (Source)
    {
        const Info = FC().TypeById[Source.Type];
        Crumb.textContent = `Inspector / Environment / ${Info.Contribution}`;
        NameInput.value = Source.Name;
        NameInput.disabled = false;
        NameInput.onchange = () => { Source.Name = NameInput.value.trim() || Source.Name; ConstructOutliner(); C.Broadcast(); };
        TypeLabel.textContent = `${Info.Contribution.toUpperCase()} · ${Info.Name.toUpperCase()}`;
        SymEl.textContent = ''; SymEl.append(C.ConstructGlyph(Info.Glyph || 'Target'));
        SymEl.style.color = Info.Colour || ContributionColours[Info.Contribution];
        EnabledEl.checked = !!Source.Enabled;
        EnabledEl.onchange = () => { Source.Enabled = EnabledEl.checked; C.Broadcast(); };
        VpObj.textContent = Source.Name;
        VpSub.textContent = `${Info.Name} · ${Source.Reach} (${Source.Radius.toFixed(1)} m)`;
        ConstructForcesInspector(Page, 'source', Source);
    }
    else if (Selected === 'Wind')
    {
        Crumb.textContent = 'Inspector / Environment / Ambient Wind';
        NameInput.value = 'Ambient Wind'; NameInput.disabled = true;
        TypeLabel.textContent = 'CONTINUOUS DOMAIN BREEZE & TURBULENCE';
        SymEl.textContent = ''; SymEl.append(C.ConstructGlyph('Wind')); SymEl.style.color = '#9fc7e8';
        EnabledEl.checked = !!Scene.FieldScene.Wind.Enabled;
        EnabledEl.onchange = () => { Scene.FieldScene.Wind.Enabled = EnabledEl.checked; C.Broadcast(); };
        VpObj.textContent = 'Ambient Wind';
        VpSub.textContent = `${Scene.FieldScene.Wind.Speed.toFixed(1)} m/s · ${Math.round(Scene.FieldScene.Wind.Bearing)}°`;
        ConstructForcesInspector(Page, 'wind', null);
    }
    else
    {
        Selected = 'Field';
        Crumb.textContent = 'Inspector / Environment';
        NameInput.value = 'Force Field'; NameInput.disabled = true;
        TypeLabel.textContent = 'MASTER FORCE FIELD & VISUALIZATION';
        SymEl.textContent = ''; SymEl.append(C.ConstructGlyph('Field')); SymEl.style.color = '#8fd3c4';
        EnabledEl.checked = !!Scene.Overlay.Enabled;
        EnabledEl.onchange = () => { Scene.Overlay.Enabled = EnabledEl.checked; C.Broadcast(); };
        VpObj.textContent = 'Force Field';
        VpSub.textContent = `${Scene.FieldScene.Sources.length} bounded force sources · ForceFieldVisualization`;
        ConstructForcesInspector(Page, 'field', null);
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
        E.AliveSamples.push(E.Gpu?.stats_?.alive ?? 0);
        if (E.AliveSamples.length > SampleCapacity) E.AliveSamples.shift();
    }
    const Prep = Scene.PreparedField || FC().Prepare(Scene.FieldScene, Scene.Time);
    const ProbeAll = FC().EvaluateAllPrepared(Scene.FieldScene, Prep, Scene.Probe[0], Scene.Probe[1], Scene.Probe[2], 0.5, Scene.FieldScene.Visualization.Solo, 'Particles');
    ProbeSamples.push(ProbeAll.FlowMag);
    if (ProbeSamples.length > SampleCapacity) ProbeSamples.shift();
    AnemHistory.push(ProbeAll.FlowMag);
    if (AnemHistory.length > 72) AnemHistory.shift();

    LiveRefreshers.forEach(Fn => Fn());
    PersistentRefreshers.forEach(Fn => Fn());

    const Hud = document.getElementById('ViewportHud');
    const Total = Scene.Effects.filter(E => E.P.visible).reduce((S, E) => S + (E.Gpu?.stats_?.alive ?? 0), 0);
    const Cap = Scene.Effects.filter(E => E.P.visible).reduce((S, E) => S + (E.Gpu?.cap || 0), 0);
    Hud.textContent = '';
    Hud.append(
        C.Construct('div', 'hud-pill', ['PARTICLES', C.Construct('b', null, `${FormatInteger(Total)} / ${FormatInteger(Cap)}`)]),
        C.Construct('div', 'hud-pill', ['RATE', C.Construct('b', null, `${Math.round(Scene.Rate)} fps`)]),
        C.Construct('div', 'hud-pill', ['CPU', C.Construct('b', null, `${Scene.CpuMilliseconds.toFixed(2)} ms`)])
    );

    const Readout = document.getElementById('ViewportReadout');
    Readout.classList.toggle('shown', Scene.ProbeEnabled);
    if (Scene.ProbeEnabled)
    {
        Readout.textContent =
            `PROBE (${Scene.Probe.map(V => V.toFixed(1)).join(', ')}) m\n` +
            `flow ${ProbeAll.Velocity.map(V => V.toFixed(2)).join('  ')} m/s\n` +
            `acc  ${ProbeAll.Acceleration.map(V => V.toFixed(2)).join('  ')} m/s²`;
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
    const W = Math.max(64, Math.round(Bounds.width * Ratio)), H = Math.max(64, Math.round(Bounds.height * Ratio));
    if (Canvas.width !== W || Canvas.height !== H)
    {
        Canvas.width = W; Canvas.height = H;
        Scene.Engine?.resize(W, H);
    }
    const Ov = document.getElementById('OverlayCanvas');
    if (Ov && (Ov.width !== W || Ov.height !== H)) { Ov.width = W; Ov.height = H; }
    const Mm = document.getElementById('MinimapCanvas');
    if (Mm && (Mm.width !== 156 || Mm.height !== 156)) { Mm.width = 156; Mm.height = 156; }
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
    const VToolIcons = { orbit: 'Orbit', handles: 'Target', probe: 'Crosshair', ball: 'Ball', turntable: 'Reset' };
    VTools.forEach(Btn =>
    {
        Btn.append(C.ConstructGlyph(VToolIcons[Btn.dataset.vtool] || 'Target'));
        if (Btn.dataset.vtool === 'handles') Btn.classList.toggle('active', !!Scene.ShowHandles);
        Btn.addEventListener('click', () =>
        {
            const T = Btn.dataset.vtool;
            if (T === 'handles')
            {
                Scene.ShowHandles = !Scene.ShowHandles;
                Btn.classList.toggle('active', !!Scene.ShowHandles);
                C.Toast(Scene.ShowHandles ? '3D Move Handles ON — drag points, objects & effectors (Shift: Y)' : '3D Move Handles hidden');
                return;
            }
            if (T === 'probe')
            {
                Scene.BallPlacing = false;
                Scene.ProbeEnabled = true;
                Scene.AnemometerPlacing = !Scene.AnemometerPlacing;
                SyncAnemometerToolState();
                SyncBallToolState();
                C.Broadcast();
                return;
            }
            if (T === 'ball')
            {
                Scene.AnemometerPlacing = false;
                Scene.BallPlacing = !Scene.BallPlacing;
                SyncAnemometerToolState();
                SyncBallToolState();
                C.Broadcast();
                return;
            }
            if (T === 'orbit')
            {
                Scene.AnemometerPlacing = false;
                Scene.BallPlacing = false;
                SyncAnemometerToolState();
                SyncBallToolState();
                return;
            }
            if (T === 'turntable') { Scene.Turntable = !Scene.Turntable; Btn.classList.toggle('active', Scene.Turntable); return; }
        });
    });

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

    const OverlaySel = document.getElementById('overlay-channel-select');
    OverlaySel.value = Scene.Overlay.Enabled ? Scene.Overlay.Contribution : 'off';
    OverlaySel.addEventListener('change', Ev =>
    {
        const V = Ev.target.value;
        if (V === 'off') Scene.Overlay.Enabled = false;
        else { Scene.Overlay.Enabled = true; Scene.Overlay.Contribution = V; }
        document.getElementById('toggle-overlay-btn').classList.toggle('active', Scene.Overlay.Enabled);
        C.Broadcast();
    });

    const ThrowBtn = document.getElementById('throw-ball-btn');
    if (ThrowBtn)
    {
        ThrowBtn.prepend(C.ConstructGlyph('Ball'));
        ThrowBtn.addEventListener('click', Ev =>
        {
            if (Ev.shiftKey)
            {
                Scene.AnemometerPlacing = false;
                Scene.BallPlacing = !Scene.BallPlacing;
                SyncAnemometerToolState();
                SyncBallToolState();
                return;
            }
            const Sel = EffectByToken(Selected);
            const Target = Sel ? Sel.P.origin : [0, 1.8, 0];
            ThrowBallAtTarget(Target, Scene.Ball.ThrowSpeed);
            C.Toast('Ball launched at ' + (Sel ? Sel.Name : 'scene center'));
            RefreshLive();
            C.Broadcast();
        });
    }
    document.getElementById('toggle-overlay-btn').classList.toggle('active', !!Scene.Overlay.Enabled);
    document.getElementById('toggle-overlay-btn').addEventListener('click', () =>
    {
        Scene.Overlay.Enabled = !Scene.Overlay.Enabled;
        document.getElementById('toggle-overlay-btn').classList.toggle('active', Scene.Overlay.Enabled);
        OverlaySel.value = Scene.Overlay.Enabled ? Scene.Overlay.Contribution : 'off';
        C.Broadcast();
    });

    document.getElementById('toggle-arrows-btn').classList.toggle('active', !!Scene.Wind.showArrows);
    document.getElementById('toggle-arrows-btn').addEventListener('click', Ev =>
    {
        Scene.Wind.showArrows = !Scene.Wind.showArrows;
        Ev.currentTarget.classList.toggle('active', Scene.Wind.showArrows);
        C.Broadcast();
    });

    document.getElementById('toggle-minimap-btn').classList.toggle('active', !!Scene.Overlay.Minimap);
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

    document.getElementById('compact-outliner').addEventListener('click', Ev =>
    {
        const Compact = document.getElementById('left-panel').classList.toggle('compact');
        Ev.currentTarget.setAttribute('aria-pressed', String(Compact));
        Ev.currentTarget.classList.toggle('active', Compact);
    });
    document.getElementById('scene-search').addEventListener('input', Ev => { OutlinerQuery = Ev.target.value.trim().toLowerCase(); C.Broadcast(); });
    document.getElementById('add-menu-preset-search')?.addEventListener('input', Ev => { AddMenuAssetQuery = Ev.target.value.trim().toLowerCase(); ConstructAddMenuAssets(); });
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

    const AddBtn = document.getElementById('add-button'), AddMenu = document.getElementById('add-menu');
    AddBtn.addEventListener('click', () =>
    {
        const Open = AddMenu.hidden;
        if (Open)
        {
            ConstructAddMenuAssets();
            const R = AddBtn.getBoundingClientRect();
            AddMenu.style.left = `${Math.max(8, Math.min(R.left, window.innerWidth - 304))}px`;
            AddMenu.style.top = `${R.bottom + 6}px`;
            AddMenu.style.maxHeight = `${Math.max(240, window.innerHeight - R.bottom - 18)}px`;
        }
        AddMenu.hidden = !Open;
        AddBtn.setAttribute('aria-expanded', String(Open));
        if (Open) document.getElementById('add-menu-preset-search')?.focus();
    });
    AddMenu.querySelectorAll('[data-add-effect]').forEach(B => B.addEventListener('click', () => { AddMenu.hidden = true; AddEffect(B.dataset.addEffect); }));
    AddMenu.querySelectorAll('[data-add-force]').forEach(B => B.addEventListener('click', () => { AddMenu.hidden = true; AddForceSource(B.dataset.addForce); }));
    AddMenu.querySelectorAll('[data-add-viz]').forEach(B => B.addEventListener('click', () =>
    {
        AddMenu.hidden = true;
        const Layer = B.dataset.addViz;
        const Vz = Scene.FieldScene.Visualization;
        if (Layer === 'Arrows')
        {
            Scene.Wind.showArrows = !Scene.Wind.showArrows;
            document.getElementById('toggle-arrows-btn')?.classList.toggle('active', Scene.Wind.showArrows);
        }
        else
        {
            Scene.Overlay.Enabled = true;
            if (Layer === 'FlowLines') Scene.Overlay.Streamlines3D = !Scene.Overlay.Streamlines3D;
            else if (Layer === 'ShowInfluence') { Scene.Overlay.Bounds = !Scene.Overlay.Bounds; Vz.ShowInfluence = !Vz.ShowInfluence; }
            else if (Layer in Vz) Vz[Layer] = !Vz[Layer];
        }
        document.getElementById('toggle-overlay-btn').classList.toggle('active', Scene.Overlay.Enabled);
        OverlaySel.value = Scene.Overlay.Enabled ? Scene.Overlay.Contribution : 'off';
        AppendLog('VIEW', `Toggled visualization layer: ${Layer}`, null, 'live');
        ConstructInspectorPage();
        C.Broadcast();
    }));
    AddMenu.querySelector('[data-action="open-library"]').addEventListener('click', () => { AddMenu.hidden = true; OpenFullLibrary(); });
    window.addEventListener('pointerdown', Ev => { if (!AddMenu.hidden && !AddMenu.contains(Ev.target) && !AddBtn.contains(Ev.target)) AddMenu.hidden = true; });

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
    document.getElementById('nav-log').addEventListener('click', () =>
    {
        document.getElementById('LibraryVeil').classList.remove('shown');
        LogOpen = !LogOpen;
        document.getElementById('log-drawer').hidden = !LogOpen;
        document.getElementById('toggle-log-btn').classList.toggle('active', LogOpen);
        SyncTopNav();
        setTimeout(ResizeCanvas, 20);
    });
    document.getElementById('new-scene-btn').addEventListener('click', () => LoadSceneTab({ Id: 'default', Name: 'New Blank Scene', Presets: [], Scenario: 'blank' }));
    document.getElementById('reset-selection-btn').addEventListener('click', () =>
    {
        const Eff = EffectByToken(Selected);
        if (Eff) { const Pr = PE.presetById(Eff.PresetId); if (Pr) { Eff.P = JSON.parse(JSON.stringify(Pr.p)); RebuildGpu(Eff); ConstructInspectorPage(); C.Broadcast(); C.Toast(`Reset ${Eff.Name}`); } }
        else ResetAll();
    });

    const ExportDlg = document.getElementById('export-dialog');
    document.getElementById('export-button').addEventListener('click', () =>
    {
        const Toml = [...Scene.Effects.map(E => EncodeEffect(E).join('\n')), FC().EncodeLevelspace(Scene.FieldScene).join('\n')].join('\n\n');
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
    Canvas.addEventListener('pointerdown', E =>
    {
        const Cam = Scene.LastCamera;
        if (E.button === 0 && Cam && (Scene.BallPlacing || (E.altKey && !Scene.AnemometerPlacing)))
        {
            ThrowBallFromScreen(Cam, E.clientX, E.clientY, Canvas, Scene.Ball.ThrowSpeed);
            C.Toast('Ball thrown!');
            RefreshLive();
            C.Broadcast();
            return;
        }
        if (E.button === 0 && Cam && Scene.AnemometerPlacing)
        {
            const Pt = ScreenToWorldPlane(Cam, E.clientX, E.clientY, Canvas, Scene.Probe[1]);
            Scene.ProbeEnabled = true;
            if (Pt)
            {
                Scene.Probe[0] = Clamp(Pt[0], -6, 6);
                Scene.Probe[2] = Clamp(Pt[2], -6, 6);
            }
            Scene.AnemometerDragging = true;
            Drag = { Mode: 'anem', X: E.clientX, Y: E.clientY, PlacedNow: true, ShiftAlt: E.shiftKey || E.altKey };
            Canvas.setPointerCapture(E.pointerId);
            SyncAnemometerToolState();
            RefreshLive();
            C.Broadcast();
            return;
        }
        if (E.button === 0 && Cam && HitTestAnemometer(Cam, E.clientX, E.clientY, Canvas))
        {
            Scene.AnemometerDragging = true;
            Drag = { Mode: 'anem', X: E.clientX, Y: E.clientY, PlacedNow: false, ShiftAlt: E.shiftKey || E.altKey };
            Canvas.setPointerCapture(E.pointerId);
            SyncAnemometerToolState();
            return;
        }
        const ArcHit = (E.button === 0 && Cam) ? HitTestArcSphere(Cam, E.clientX, E.clientY, Canvas) : null;
        if (ArcHit)
        {
            SelectNode(ArcHit.Effect.Token);
            Scene.ArcDragging = ArcHit;
            Drag = { Mode: 'arcsphere', Effect: ArcHit.Effect, Which: ArcHit.Which, Label: ArcHit.Label, X: E.clientX, Y: E.clientY, StartX: E.clientX, StartY: E.clientY, ShiftAlt: E.shiftKey || E.altKey };
            Canvas.setPointerCapture(E.pointerId);
            const Surface = document.getElementById('ViewportSurface');
            if (Surface) Surface.style.cursor = 'grabbing';
            const Banner = document.getElementById('ViewportPlaceBanner'), BannerText = document.getElementById('ViewportPlaceText');
            if (Banner) Banner.classList.add('shown');
            if (BannerText) BannerText.textContent = `Dragging ${ArcHit.Label} · move horizontally · hold Shift for altitude Y`;
            return;
        }
        const EffHit = (E.button === 0 && Cam) ? HitTestEffector(Cam, E.clientX, E.clientY, Canvas) : null;
        if (EffHit)
        {
            SelectNode(EffHit.Effect.Token);
            Scene.EffDragging = true;
            Scene.EffDragEffect = EffHit.Effect;
            Scene.EffDragWhich = EffHit.Which;
            Drag = { Mode: 'effector', Effect: EffHit.Effect, Which: EffHit.Which, Label: EffHit.Label, X: E.clientX, Y: E.clientY, StartX: E.clientX, StartY: E.clientY, ShiftAlt: E.shiftKey || E.altKey };
            Canvas.setPointerCapture(E.pointerId);
            const Surface = document.getElementById('ViewportSurface');
            if (Surface) Surface.style.cursor = 'grabbing';
            const Banner = document.getElementById('ViewportPlaceBanner'), BannerText = document.getElementById('ViewportPlaceText');
            if (Banner) Banner.classList.add('shown');
            if (BannerText) BannerText.textContent = `Dragging ${EffHit.Label} · move horizontally · hold Shift for altitude Y`;
            return;
        }
        const CtrlHit = (E.button === 0 && Cam) ? HitTestControlPoint(Cam, E.clientX, E.clientY, Canvas) : null;
        if (CtrlHit)
        {
            if (CtrlHit.Kind === 'source') SelectNode('src:' + CtrlHit.Source.Token, false);
            else if (CtrlHit.Effect) SelectNode(CtrlHit.Effect.Token, false);
            if (CtrlHit.Kind === 'ferroMagnet')
            {
                const P = CtrlHit.Effect.P;
                const FM = Math.max(1, Math.min(2, P.ferroMode | 0));
                const DishR = Math.max(0.8, P.radius || 2.1);
                const Cur = EvalFerroMagnetCPU(Scene.Time, P.origin, DishR, P.ferroMagnetSpeed ?? 1.35, FM, P);
                P.ferroMagnetFree = true;
                P.effPos = [Cur[0], Cur[1], Cur[2]];
            }
            Scene.PointDragging = CtrlHit;
            Drag = { Mode: 'ctrlpoint', Hit: CtrlHit, X: E.clientX, Y: E.clientY, ShiftAlt: E.shiftKey || E.altKey };
            Canvas.setPointerCapture(E.pointerId);
            const Surface = document.getElementById('ViewportSurface');
            if (Surface) Surface.style.cursor = 'grabbing';
            const Banner = document.getElementById('ViewportPlaceBanner'), BannerText = document.getElementById('ViewportPlaceText');
            if (Banner) Banner.classList.add('shown');
            if (BannerText) BannerText.textContent = `Moving ${CtrlHit.Label} · drag XZ · hold Shift for altitude Y`;
            return;
        }
        Drag = { Mode: (E.button === 2 || E.shiftKey || E.button === 1) ? 'pan' : 'orbit', X: E.clientX, Y: E.clientY, StartX: E.clientX, StartY: E.clientY };
        Canvas.setPointerCapture(E.pointerId);
    });
    Canvas.addEventListener('pointermove', E =>
    {
        if (!Drag)
        {
            const Hover = !!(Scene.LastCamera && Scene.ProbeEnabled && !Scene.AnemometerPlacing && HitTestAnemometer(Scene.LastCamera, E.clientX, E.clientY, Canvas));
            if (Hover !== Scene.AnemometerHover)
            {
                Scene.AnemometerHover = Hover;
                SyncAnemometerToolState();
            }
            const AH = (!Hover && !Scene.AnemometerPlacing && Scene.LastCamera) ? HitTestArcSphere(Scene.LastCamera, E.clientX, E.clientY, Canvas) : null;
            const EH = (!AH && !Hover && !Scene.AnemometerPlacing && Scene.LastCamera) ? HitTestEffector(Scene.LastCamera, E.clientX, E.clientY, Canvas) : null;
            const CH = (!EH && !AH && !Hover && !Scene.AnemometerPlacing && Scene.LastCamera) ? HitTestControlPoint(Scene.LastCamera, E.clientX, E.clientY, Canvas) : null;
            Scene.ArcHover = AH ? { EffectToken: AH.EffectToken, Which: AH.Which } : null;
            Scene.EffHover = EH ? { EffectToken: EH.EffectToken, Which: EH.Which } : null;
            Scene.PointHover = CH;
            const Surface = document.getElementById('ViewportSurface');
            if (Surface && !Scene.AnemometerPlacing && !Hover) Surface.style.cursor = (AH || EH || CH) ? 'grab' : '';
            return;
        }
        const Dx = E.clientX - Drag.X, Dy = E.clientY - Drag.Y;
        Drag.X = E.clientX; Drag.Y = E.clientY;
        if (Drag.Mode === 'ctrlpoint')
        {
            const H = Drag.Hit;
            const Vertical = E.shiftKey || E.altKey || Drag.ShiftAlt;
            let Pos = null;
            if (H.Kind === 'origin') Pos = H.Effect.P.origin;
            else if (H.Kind === 'ferroMagnet') Pos = H.Effect.P.effPos;
            else if (H.Kind === 'source') Pos = H.Source.Centre;
            else if (H.Kind === 'crackHit')
            {
                const Pt = ScreenToWorldPlane(Scene.LastCamera, E.clientX, E.clientY, Canvas, 0.05);
                if (Pt)
                {
                    const P = H.Effect.P;
                    const Ex = (Array.isArray(P.boxHalf) ? P.boxHalf[0] : 2.6) || 2.6;
                    const Ez = (Array.isArray(P.boxHalf) ? P.boxHalf[2] : 2.6) || 2.6;
                    H.Effect.CrackHit = {
                        x: Clamp(Pt[0], P.origin[0] - Ex, P.origin[0] + Ex),
                        z: Clamp(Pt[2], P.origin[2] - Ez, P.origin[2] + Ez),
                        t: Scene.Time
                    };
                    const BannerText = document.getElementById('ViewportPlaceText');
                    if (BannerText) BannerText.textContent = `${H.Label}: (${H.Effect.CrackHit.x.toFixed(2)}, ${H.Effect.CrackHit.z.toFixed(2)}) m`;
                }
                RefreshLive();
                C.Broadcast();
                return;
            }
            if (Pos)
            {
                if (Vertical)
                {
                    Pos[1] = Clamp(Pos[1] - Dy * 0.025, 0.0, 9.0);
                }
                else
                {
                    const Pt = ScreenToWorldPlane(Scene.LastCamera, E.clientX, E.clientY, Canvas, Pos[1]);
                    if (Pt)
                    {
                        Pos[0] = Clamp(Pt[0], -14, 14);
                        Pos[2] = Clamp(Pt[2], -14, 14);
                    }
                }
                const BannerText = document.getElementById('ViewportPlaceText');
                if (BannerText) BannerText.textContent = `${H.Label}: (${Pos.map(V => V.toFixed(2)).join(', ')}) m · Hold Shift for altitude Y`;
            }
            RefreshLive();
            C.Broadcast();
            return;
        }
        if (Drag.Mode === 'arcsphere')
        {
            const Pos = Drag.Effect.P[Drag.Which] || (Drag.Effect.P[Drag.Which] = [0, 2, 0]);
            if (E.shiftKey || E.altKey || Drag.ShiftAlt)
            {
                Pos[1] = Clamp(Pos[1] - Dy * 0.025, 0.2, 8.0);
            }
            else
            {
                const Pt = ScreenToWorldPlane(Scene.LastCamera, E.clientX, E.clientY, Canvas, Pos[1]);
                if (Pt)
                {
                    Pos[0] = Clamp(Pt[0], -12, 12);
                    Pos[2] = Clamp(Pt[2], -12, 12);
                }
            }
            const BannerText = document.getElementById('ViewportPlaceText');
            if (BannerText) BannerText.textContent = `${Drag.Label}: (${Pos.map(V => V.toFixed(2)).join(', ')}) m · Hold Shift for altitude Y`;
            RefreshLive();
            C.Broadcast();
            return;
        }
        if (Drag.Mode === 'anem')
        {
            if (E.shiftKey || E.altKey || Drag.ShiftAlt)
            {
                Scene.Probe[1] = Clamp(Scene.Probe[1] - Dy * 0.025, 0.1, 7.5);
            }
            else
            {
                const Pt = ScreenToWorldPlane(Scene.LastCamera, E.clientX, E.clientY, Canvas, Scene.Probe[1]);
                if (Pt)
                {
                    Scene.Probe[0] = Clamp(Pt[0], -6, 6);
                    Scene.Probe[2] = Clamp(Pt[2], -6, 6);
                }
            }
            SyncAnemometerToolState();
            RefreshLive();
            C.Broadcast();
            return;
        }
        if (Drag.Mode === 'effector')
        {
            const Pos = Drag.Effect.P[Drag.Which] || (Drag.Effect.P[Drag.Which] = [0, 1.5, 0]);
            if (E.shiftKey || E.altKey || Drag.ShiftAlt)
            {
                Pos[1] = Clamp(Pos[1] - Dy * 0.025, 0.05, 8.0);
            }
            else
            {
                const Pt = ScreenToWorldPlane(Scene.LastCamera, E.clientX, E.clientY, Canvas, Pos[1]);
                if (Pt)
                {
                    Pos[0] = Clamp(Pt[0], -12, 12);
                    Pos[2] = Clamp(Pt[2], -12, 12);
                }
            }
            const BannerText = document.getElementById('ViewportPlaceText');
            if (BannerText) BannerText.textContent = `${Drag.Label}: (${Pos.map(V => V.toFixed(2)).join(', ')}) m · Hold Shift for altitude Y`;
            RefreshLive();
            C.Broadcast();
            return;
        }
        const Cam = Scene.Camera;
        if (Drag.Mode === 'pan')
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
    const Release = Ev =>
    {
        if (Drag?.Mode === 'arcsphere')
        {
            const Pos = Drag.Effect.P[Drag.Which];
            Scene.ArcDragging = null;
            const Surface = document.getElementById('ViewportSurface');
            if (Surface) Surface.style.cursor = '';
            document.getElementById('ViewportPlaceBanner')?.classList.remove('shown');
            TriggerArcStrike(Drag.Effect, Scene.Time);
            PE.Edits.Announce(Drag.Label, Pos);
            AppendLog('PARTICLES', `${Drag.Effect.Name}: ${Drag.Label}`, `(${Pos.map(V => V.toFixed(2)).join(', ')}) m`, 'live');
            ConstructInspectorPage();
            C.Broadcast();
        }
        if (Drag?.Mode === 'effector')
        {
            const Pos = Drag.Effect.P[Drag.Which];
            const Surface = document.getElementById('ViewportSurface');
            if (Surface) Surface.style.cursor = '';
            document.getElementById('ViewportPlaceBanner')?.classList.remove('shown');
            Scene.EffDragging = false;
            Scene.EffDragEffect = null;
            Scene.EffDragWhich = null;
            PE.Edits.Announce(Drag.Label, Pos);
            AppendLog('PARTICLES', `${Drag.Effect.Name}: ${Drag.Label}`, `(${Pos.map(V => V.toFixed(2)).join(', ')}) m`, 'live');
            ConstructInspectorPage();
            C.Broadcast();
        }
        if (Drag?.Mode === 'ctrlpoint')
        {
            const H = Drag.Hit;
            Scene.PointDragging = null;
            const Surface = document.getElementById('ViewportSurface');
            if (Surface) Surface.style.cursor = '';
            document.getElementById('ViewportPlaceBanner')?.classList.remove('shown');
            if (H.Kind === 'source')
            {
                AppendLog('FORCES', `Moved ${H.Source.Name}`, `(${H.Source.Centre.map(V => V.toFixed(2)).join(', ')}) m`, 'live');
            }
            else if (H.Kind === 'crackHit' && H.Effect.CrackHit)
            {
                AppendLog('PARTICLES', `${H.Effect.Name}: Fracture epicenter`, `(${H.Effect.CrackHit.x.toFixed(2)}, ${H.Effect.CrackHit.z.toFixed(2)}) m`, 'live');
            }
            else if (H.Effect)
            {
                const Pos = H.Kind === 'ferroMagnet' ? H.Effect.P.effPos : H.Effect.P.origin;
                PE.Edits.Announce(H.Label, Pos);
                AppendLog('PARTICLES', `Moved ${H.Label}`, `(${Pos.map(V => V.toFixed(2)).join(', ')}) m`, 'live');
            }
            ConstructInspectorPage();
            C.Broadcast();
        }
        if (Drag?.Mode === 'anem')
        {
            const WasPlace = !!Drag.PlacedNow;
            Scene.AnemometerDragging = false;
            if (WasPlace)
            {
                Scene.AnemometerPlacing = false;
                C.Toast(`Anemometer placed at (${Scene.Probe.map(V => V.toFixed(1)).join(', ')}) m`);
            }
            AppendLog('WIND', WasPlace ? 'Anemometer placed in viewport' : 'Anemometer moved in viewport', `(${Scene.Probe.map(V => V.toFixed(2)).join(', ')}) m`, 'live');
            SyncAnemometerToolState();
            C.Broadcast();
        }
        Drag = null;
    };
    Canvas.addEventListener('pointerup', Release);
    Canvas.addEventListener('pointercancel', Release);
    Canvas.addEventListener('wheel', E => { E.preventDefault(); Scene.Camera.Distance = Clamp(Scene.Camera.Distance * Math.exp(E.deltaY * 0.001), 2, 60); }, { passive: false });

    // Drag-and-drop any .obj / .stl / .json / .facet 3D mesh straight onto the viewport to place it as a MoGraph particle object!
    const DropHost = document.getElementById('ViewportSurface') || Canvas;
    DropHost.addEventListener('dragover', E => { E.preventDefault(); });
    DropHost.addEventListener('drop', async E =>
    {
        E.preventDefault();
        const File = E.dataTransfer?.files?.[0];
        if (!File || !/\.(obj|stl|json|facet)$/i.test(File.name)) return;
        try
        {
            const Buf = await File.arrayBuffer();
            const Mesh = ParseCustomMeshFile(File.name, Buf);
            if (!Mesh || !Mesh.tris.length) { C.Toast('No valid 3D triangles found in ' + File.name); return; }
            const MeshId = RegisterCustomMesh(Mesh, File.name);
            let Target = EffectByToken(Selected);
            if (!Target || (Target.P.gridMode || 0) <= 0)
            {
                Target = AddEffect('effector-object-placer');
            }
            if (Target)
            {
                Target.P.customMeshId = MeshId;
                Target.P.customMeshName = File.name;
                Target.P.gridSurface = 16;
                if ((Target.P.radius || 0) < 0.8) Target.P.radius = 1.55;
                ConstructInspectorPage();
                C.Broadcast();
                AppendLog('PARTICLES', `Dropped 3D mesh ${File.name}`, `${Mesh.tris.length.toLocaleString()} triangles`, 'live');
                C.Toast(`Placed ${File.name} (${Mesh.tris.length.toLocaleString()} tris)`);
            }
        }
        catch (Err)
        {
            C.Toast('Mesh drop failed: ' + (Err?.message || Err));
        }
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INITIALIZATION
//------------------------------------------------------------------------------------------------------------------------

async function Boot()
{
    WireStaticIconsAndControls();
    ResizeCanvas();

    let Canvas = document.getElementById('ViewportCanvas');
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
        // WebGPU getContext('webgpu') permanently locks a canvas from getContext('2d'); replace cleanly if locked
        if (!Canvas.getContext('2d'))
        {
            const FreshCanvas = Canvas.cloneNode(false);
            Canvas.replaceWith(FreshCanvas);
            Canvas = FreshCanvas;
            ResizeCanvas();
        }
        Scene.Engine = new SoftwarePreviewEngine(Canvas);
        const Facts = await Scene.Engine.init();
        Scene.AdapterName = Facts.adapter;
        Scene.Readback = new SoftwareLatticeReadback(() => Scene.FieldScene, Scene.Wind, () => Scene.Time);
        const Pill = document.getElementById('engine-pill');
        Pill.classList.add('software');
        document.getElementById('engine-pill-text').textContent = 'PREVIEW · SOFTWARE 3D';
        document.getElementById('status-ready').innerHTML = '<i></i>Software 3D preview active (open in WebGPU Chrome/Edge for GPU compute)';
    }

    document.getElementById('status-adapter').textContent = Scene.AdapterName;
    const Query = new URLSearchParams(location.search).get('scene');
    const InitialIds = Query === null ? PE.defaultScene : (Query === 'none' || Query === '' ? [] : Query.split(','));
    for (const Id of InitialIds) if (PE.presetById(Id)) Scene.Effects.push(ConstructEffect(Id));
    Selected = Scene.Effects[0]?.Token || (Scene.FieldScene.Sources.length ? 'Field' : 'Wind');

    ConstructSceneTabs();
    ConstructOverlay();
    ConstructOutliner();
    ConstructAddMenuAssets();
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
        else if (Ev.key === 'b' || Ev.key === 'B') BurstAll();
        else if (Ev.key === 'r' || Ev.key === 'R') ResetAll();
        else if (Ev.key === 's' || Ev.key === 'S') { document.getElementById('step-button').click(); }
        else if (Ev.key === 'p' || Ev.key === 'P') { Scene.BallPlacing = false; Scene.ProbeEnabled = true; Scene.AnemometerPlacing = !Scene.AnemometerPlacing; SyncAnemometerToolState(); SyncBallToolState(); C.Broadcast(); }
        else if (Ev.key === 't' || Ev.key === 'T')
        {
            if (Ev.shiftKey)
            {
                Scene.AnemometerPlacing = false;
                Scene.BallPlacing = !Scene.BallPlacing;
                SyncAnemometerToolState();
                SyncBallToolState();
            }
            else
            {
                const Sel = EffectByToken(Selected);
                ThrowBallAtTarget(Sel ? Sel.P.origin : [0, 1.8, 0], Scene.Ball.ThrowSpeed);
                C.Toast('Ball thrown!');
            }
            RefreshLive();
            C.Broadcast();
        }
        else if (Ev.key === 'v' || Ev.key === 'V') { document.getElementById('toggle-overlay-btn').click(); }
        else if (Ev.key === 'g' || Ev.key === 'G') { document.querySelector('#ViewportTools [data-vtool="handles"]')?.click(); }
        else if (Ev.key === '/') { Ev.preventDefault(); const M = document.getElementById('add-menu'); if (M?.hidden) document.getElementById('add-button')?.click(); else document.getElementById('add-menu-preset-search')?.focus(); }
        else if (Ev.key === 'Escape') { Scene.AnemometerPlacing = false; Scene.BallPlacing = false; SyncAnemometerToolState(); SyncBallToolState(); document.getElementById('add-menu').hidden = true; document.getElementById('LibraryVeil').classList.remove('shown'); SyncTopNav(); }
    });

    SyncAnemometerToolState();
    SyncBallToolState();
    RefreshLive();
    AppendLog('PARTICLES', `Engine ready · ${Scene.AdapterName}`, `${Scene.Effects.length} effects · ${PE.Presets.length} presets`, 'live');
    AppendLog('FORCES', 'ForceFieldVisualization active', `${Scene.FieldScene.Sources.length} bounded sources + Ambient Wind`, 'live');
    HandleAssetDeepLink();

    C.Broadcast();
    window.ParticleEditorScene = Scene;
    window.ParticleEditorView = Visualization;
    window.ParticleEditorHold = En => { Held = !!En; if (!Held) { LastTick = 0; requestAnimationFrame(Tick); } else { Advance(performance.now()); RefreshLive(); } return Held; };
    requestAnimationFrame(Tick);
}

if (document.readyState === 'loading') window.addEventListener('DOMContentLoaded', Boot);
else Boot();
