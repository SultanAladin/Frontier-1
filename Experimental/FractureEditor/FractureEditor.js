/*==========================================================================================================================================
                                                           FRACTUREEDITOR.JS
==========================================================================================================================================*/
// 🧩 Frontier · Fracture Studio:
//    Unmodified eb353da2 Fracture & Celestial engines (Navier–Cauchy Engineering FEM, Fast Boussinesq Flat-BSP,
//    Griffith, LEFM Wavefront, Clustered Voronoi, Dendritic Spall, Piece SDF, RigidBodyDynamics & Sun/Atmos/Clouds)
//    redesigned into the Sun Inspector + Fluid Editor UI architecture.

import {
    MaterialCatalogue,
    ConstructSolidComplex,
    ExecuteFracturePartition,
    EvaluateSdfAtlasFootprint
} from './Engine/ConvexPartitioning.js';
import { FractureKinematicsTimeline } from './Engine/RigidBodyDynamics.js';
import {
    CreateDefaultSunParameters,
    CreateDefaultAtmosphereParameters,
    CreateDefaultCloudsParameters,
    EvaluateSolarEphemeris
} from './Engine/CelestialAtmosphereModel.js';
import { ViewportRasterizationHost } from './Engine/ViewportRasterization.js';
import {
    DrawGriffithWaveformDiagram,
    DrawPolarAnisotropyRadar,
    DrawPieceSdfSliceCanvas,
    DrawSolarDiurnalArcCanvas,
    DrawObserverWorldMapCanvas,
    DrawAtmosphereLutCanvas,
    DrawAtmosphereDensityCurvesCanvas,
    DrawCloudDensityMaskCanvas,
    DrawCloudAltitudeSlabCanvas,
    DrawSunDiscPreviewCanvas,
    DrawLocalCloudBoundsCanvas,
    DrawEngineeringStressSliceCanvas
} from './Engine/InfographicDiagrams.js';
import * as C from './StudioControls.js';

const Clamp = (V, Lo, Hi) => Math.min(Hi, Math.max(Lo, V));
const FormatInt = N => Math.round(N).toLocaleString('en-US');

//------------------------------------------------------------------------------------------------------------------------
//                                              INITIAL CONSTRUCTS & PRESETS
//------------------------------------------------------------------------------------------------------------------------

function CreateInitialConstructs()
{
    return [
        {
            Id: 'construct-monolith',
            Title: 'Monolith Pillar',
            SolidCategory: 'monolith',
            Scale: [1, 1, 1],
            Pipeline: 'baked',
            BakedResident: true,
            BakedSignature: '',
            Spec: {
                Solver: 'engineering_fem', Material: 'concrete', Energy: 4600, Seed: 42, Ceiling: 32, MinimumSize: 0.04,
                GrainAngleDeg: 0, BondStrengthScale: 1.0, PieceSdf: true, SdfResolution: 64,
                ImpactPoint: [0.05, 0.24, 0.48], ImpactNormal: [0, 0, -1]
            }
        },
        {
            Id: 'construct-tempered',
            Title: 'Tempered Safety Glazing',
            SolidCategory: 'curtain_pane',
            Scale: [1, 1, 1],
            Pipeline: 'realtime',
            BakedResident: false,
            BakedSignature: '',
            Spec: {
                Solver: 'runtime_boussinesq', Material: 'tempered', Energy: 1200, Seed: 108, Ceiling: 64, MinimumSize: 0.032,
                GrainAngleDeg: 0, BondStrengthScale: 0.5, PieceSdf: false, SdfResolution: 64,
                ImpactPoint: [0.18, 0.10, 0.032], ImpactNormal: [0, 0, -1]
            }
        },
        {
            Id: 'construct-float-glass',
            Title: 'Annealed Float Pane',
            SolidCategory: 'curtain_pane',
            Scale: [1, 1, 1],
            Pipeline: 'realtime',
            BakedResident: false,
            BakedSignature: '',
            Spec: {
                Solver: 'wavefront', Material: 'glass', Energy: 1850, Seed: 77, Ceiling: 44, MinimumSize: 0.035,
                GrainAngleDeg: 0, BondStrengthScale: 0.7, PieceSdf: false, SdfResolution: 64,
                ImpactPoint: [-0.12, 0.08, 0.032], ImpactNormal: [0, 0, -1]
            }
        },
        {
            Id: 'construct-granite',
            Title: 'Bedded Granite Erratic',
            SolidCategory: 'boulder',
            Scale: [1, 1, 1],
            Pipeline: 'baked',
            BakedResident: true,
            BakedSignature: '',
            Spec: {
                Solver: 'engineering_fem', Material: 'granite', Energy: 5200, Seed: 314, Ceiling: 32, MinimumSize: 0.05,
                GrainAngleDeg: 18, BondStrengthScale: 1.15, PieceSdf: true, SdfResolution: 64,
                ImpactPoint: [0.15, 0.22, 0.55], ImpactNormal: [-0.15, -0.1, -0.98]
            }
        },
        {
            Id: 'construct-spruce',
            Title: 'Orthotropic Timber Beam',
            SolidCategory: 'timber_beam',
            Scale: [1, 1, 1],
            Pipeline: 'realtime',
            BakedResident: false,
            BakedSignature: '',
            Spec: {
                Solver: 'runtime_boussinesq', Material: 'wood', Energy: 2600, Seed: 64, Ceiling: 30, MinimumSize: 0.038,
                GrainAngleDeg: 0, BondStrengthScale: 0.95, PieceSdf: false, SdfResolution: 32,
                ImpactPoint: [0.0, 0.14, 0.14], ImpactNormal: [0, -0.5, -0.86]
            }
        },
        {
            Id: 'construct-voronoi',
            Title: 'Voronoi Vault Cube',
            SolidCategory: 'cube',
            Scale: [1, 1, 1],
            Pipeline: 'baked',
            BakedResident: true,
            BakedSignature: '',
            Spec: {
                Solver: 'voronoi', Material: 'concrete', Energy: 3900, Seed: 91, Ceiling: 52, MinimumSize: 0.05,
                GrainAngleDeg: 0, BondStrengthScale: 1.05, PieceSdf: true, SdfResolution: 128,
                ImpactPoint: [0.22, 0.16, 0.66], ImpactNormal: [0, 0, -1]
            }
        },
        {
            Id: 'construct-polymer',
            Title: 'Ductile ABS Containment',
            SolidCategory: 'cylinder',
            Scale: [1, 1, 1],
            Pipeline: 'realtime',
            BakedResident: false,
            BakedSignature: '',
            Spec: {
                Solver: 'griffith', Material: 'plastic', Energy: 4100, Seed: 23, Ceiling: 16, MinimumSize: 0.075,
                GrainAngleDeg: 0, BondStrengthScale: 1.3, PieceSdf: false, SdfResolution: 32,
                ImpactPoint: [0.1, 0.25, 0.60], ImpactNormal: [0, 0, -1]
            }
        }
    ];
}

const SolidSpawnPresets = [
    { Cat: 'monolith', Mat: 'concrete', Solver: 'engineering_fem', Group: 'Engineering', Title: 'Calibration Monolith', Blurb: '3D Navier–Cauchy concrete pillar + H=0.78 interlocking' },
    { Cat: 'curtain_pane', Mat: 'tempered', Solver: 'runtime_boussinesq', Group: 'Brittle', Title: 'Tempered Curtain Lite', Blurb: 'Fast Boussinesq Flat-BSP 46 kJ/m³ dicing curtain pane' },
    { Cat: 'curtain_pane', Mat: 'glass', Solver: 'wavefront', Group: 'Brittle', Title: 'Annealed Crown Plate', Blurb: 'LEFM Mott radial star spokes & T-junction rings' },
    { Cat: 'boulder', Mat: 'granite', Solver: 'engineering_fem', Group: 'Stone & Timber', Title: 'Bedded Basalt Erratic', Blurb: 'Anisotropic bedding planes + Mode I/II granite fractography' },
    { Cat: 'timber_beam', Mat: 'wood', Solver: 'runtime_boussinesq', Group: 'Stone & Timber', Title: 'Orthotropic Spruce Joist', Blurb: 'High-anisotropy longitudinal splintering along grain' },
    { Cat: 'cube', Mat: 'concrete', Solver: 'voronoi', Group: 'Voronoi', Title: 'Voronoi Delaunay Block', Blurb: 'Clustered impact-biased Voronoi cells + tensile bond graph' },
    { Cat: 'cone', Mat: 'glass', Solver: 'dendritic', Group: 'Brittle', Title: 'Obsidian Ballistic Spire', Blurb: 'Dendritic core-rind lightning spall cones' },
    { Cat: 'cylinder', Mat: 'plastic', Solver: 'griffith', Group: 'Ductile', Title: 'Ductile ABS Drum', Blurb: 'Griffith stress-budgeted plastic containment cylinder' }
];

const SolverCatalogue = [
    { Id: 'runtime_boussinesq', Name: 'Fast Runtime Boussinesq Flat-BSP', Lineage: 'Zero-Alloc CSR Arena + 1-Draw DataTexture', Cpu: '0.25–1.25 ms', Realtime: 'Primary (<1 ms)', Baked: 'Convex Proxy', Mechanism: 'Closed-form Boussinesq–Hertz stress + 1-clip CSR bisectors + O(N) CutToken bonds' },
    { Id: 'engineering_fem', Name: 'Engineering Navier–Cauchy + Fractography', Lineage: '3D Continuum Lattice + Interlocking', Cpu: '20–110 ms', Realtime: 'Pre-Baked + Hybrid', Baked: 'Primary Bake', Mechanism: '3×3 Cauchy eigenstresses, Rankine/Mohr–Coulomb, wake T-junctions, Bouchaud H=0.78 roughness' },
    { Id: 'griffith', Name: 'Griffith Stress-Budgeted Clipping', Lineage: 'Frontier- (Fragmentation)', Cpu: '4.5–15 ms', Realtime: 'Interactive (≤24)', Baked: 'Supported + SDF', Mechanism: 'Hertzian cone + 2·Gc·A surface energy budget + grain anisotropy + tempering dicing' },
    { Id: 'wavefront', Name: 'LEFM Crack-Tip Wavefront', Lineage: 'Frontier- (Propagation)', Cpu: '0.8–2.0 ms', Realtime: 'Thin-Plate Live', Baked: 'Supported', Mechanism: 'Mott Rayleigh crack speed 0.6 cR, radial star spokes + flexural T-junction rings' },
    { Id: 'voronoi', Name: 'Clustered 3D Voronoi + Strain Bonds', Lineage: 'Frontier (EverestInterface)', Cpu: '4.0–26 ms', Realtime: 'Hybrid Bonds', Baked: 'Supported + 128³ SDF', Mechanism: 'Golden-angle Poisson seed bisectors + impact envelope + Delaunay tensile bonds' },
    { Id: 'dendritic', Name: 'Dendritic Lightning & Core-Rind Spall', Lineage: 'RockGenerator CSG Paradigm', Cpu: '9.0–120 ms', Realtime: 'Bake Only', Baked: 'Supported + SDF', Mechanism: 'Outer spall slabs + stochastic branching internal lightning fracture polylines' }
];

const ComputeRecipeSignature = Entry =>
{
    const { PieceSdf, SdfResolution, ...Recipe } = Entry.Spec;
    return JSON.stringify({ Solid: Entry.SolidCategory, Scale: Entry.Scale, Recipe });
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    STUDIO STATE
//------------------------------------------------------------------------------------------------------------------------

const Studio = {
    Constructs: CreateInitialConstructs(),
    ActiveConstructId: 'construct-monolith',
    SelectedId: 'construct-monolith',
    InspectorTab: 'overview',
    OutlinerFilter: 'all',
    OutlinerQuery: '',
    PresetGroup: 'All',
    PresetQuery: '',
    LogChannel: 'All',
    LogOpen: false,
    SpawnCounter: 1,
    Revisions: 0,

    SunParams: CreateDefaultSunParameters(),
    AtmosParams: CreateDefaultAtmosphereParameters(),
    CloudsParams: CreateDefaultCloudsParameters(),
    SolarSolved: null,

    Timeline: new FractureKinematicsTimeline(),
    Partition: null,
    Viewport: null
};

Studio.SolarSolved = EvaluateSolarEphemeris(Studio.SunParams);
Studio.AtmosParams.BakedSunDirection = [...Studio.SolarSolved.Direction];
for (const Entry of Studio.Constructs)
    if (Entry.BakedResident) Entry.BakedSignature = ComputeRecipeSignature(Entry);

const ActiveConstruct = () => Studio.Constructs.find(E => E.Id === Studio.ActiveConstructId) || Studio.Constructs[0];
const SelectedConstruct = () => Studio.Constructs.find(E => E.Id === Studio.SelectedId) || ActiveConstruct();

let OutlinerScope = null;
let PageScope = null;
const LogEntries = [];
const PendingRedraws = [];

//------------------------------------------------------------------------------------------------------------------------
//                                              LOGGING & CORE COMPUTE
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
    for (const Ch of ['All', 'FRACTURE', 'PHYSICS', 'SDF', 'SUN', 'ATMOS', 'CLOUDS'])
    {
        const Btn = C.Construct('button', 'log-pill' + (Studio.LogChannel === Ch ? ' active' : ''), Ch === 'All' ? `All (${LogEntries.length})` : Ch);
        Btn.type = 'button';
        Btn.addEventListener('click', () => { Studio.LogChannel = Ch; RefreshLogDrawer(); });
        Head.append(Btn);
    }
    Content.textContent = '';
    for (const E of LogEntries.filter(X => Studio.LogChannel === 'All' || X.Channel === Studio.LogChannel).slice(0, 40))
    {
        Content.append(C.Construct('div', 'log-row', [
            C.Construct('span', 'log-time tabular', E.Time),
            C.Construct('span', 'log-chan ' + (E.Tone || ''), E.Channel),
            C.Construct('span', null, E.Detail ? `${E.Message} — ${E.Detail}` : E.Message)
        ]));
    }
}

function RecomputePartition(AutoPlay = false, Note = null)
{
    const Construct = ActiveConstruct();
    const Base = ConstructSolidComplex(Construct.SolidCategory, Construct.Scale, Construct.Spec.Seed);
    Studio.Partition = ExecuteFracturePartition(Base, Construct.Spec);
    Studio.Viewport.RebuildPartitionVisuals(Studio.Partition, Construct.Spec.ImpactPoint, Construct.Spec.ImpactNormal);
    Studio.Timeline.BuildSimulation(Studio.Partition, Construct.Pipeline, 0, Studio.Viewport.ObjectLiftY);
    if (AutoPlay)
    {
        Studio.Timeline.ExplodeAmount = 0;
        document.getElementById('explodeRangeInput').value = '0';
        document.getElementById('explodeValueText').textContent = '0.00';
        Studio.Timeline.ActiveTime = 0.02;
        Studio.Timeline.Playing = true;
        UpdatePlayIcon();
    }
    SyncTimelineFrame();
    RenderViewportHud();
    if (Note) AppendLog('FRACTURE', Note, `${Studio.Partition.Receipt.FragmentCount} shards · ${Studio.Partition.Receipt.ComputeDurationMs.toFixed(2)} ms`, 'live');
    document.getElementById('inspector-revision').textContent = `Parameters · Revision ${++Studio.Revisions}`;
}

function SyncTimelineFrame()
{
    if (!Studio.Partition) return;
    const Construct = ActiveConstruct();
    const Sample = Studio.Timeline.SampleAtTime(
        Studio.Timeline.ActiveTime,
        Studio.Partition.Fragments,
        Studio.Timeline.ExplodeAmount,
        Studio.Partition.BaseMetrics.Centroid
    );
    Studio.Viewport.ApplyTimelineSample(Sample, Studio.Partition.Fragments, Construct.Pipeline);

    const T = Studio.Timeline.ActiveTime, Dur = Studio.Timeline.DurationSeconds;
    const Ms = Math.floor((T % 1) * 1000), Secs = Math.floor(T % 60), Mins = Math.floor(T / 60);
    document.getElementById('timecode').textContent = `${String(Mins).padStart(2, '0')}:${String(Secs).padStart(2, '0')}.${String(Ms).padStart(3, '0')}`;
    document.getElementById('frame-counter').textContent = `${T.toFixed(2)}s / ${Dur.toFixed(2)}s`;
    document.getElementById('playhead').style.left = `${Clamp((T / Dur) * 100, 0, 100).toFixed(2)}%`;
    document.getElementById('live-pill').classList.toggle('paused', !Studio.Timeline.Playing);
    document.getElementById('live-pill-label').textContent = Studio.Timeline.Playing ? 'LIVE' : 'STANDBY';
}

function UpdatePlayIcon()
{
    const PlayBtn = document.getElementById('play-button');
    PlayBtn.textContent = '';
    PlayBtn.append(C.ConstructGlyph(Studio.Timeline.Playing ? 'Pause' : 'Play'));
}

function TriggerStrike()
{
    Studio.Timeline.ExplodeAmount = 0;
    document.getElementById('explodeRangeInput').value = '0';
    document.getElementById('explodeValueText').textContent = '0.00';
    Studio.Timeline.ActiveTime = 0.02;
    Studio.Timeline.Playing = true;
    UpdatePlayIcon();
    AppendLog('PHYSICS', `Impact strike fired on ${ActiveConstruct().Title}`, `E=${ActiveConstruct().Spec.Energy} J`, 'warm');
}

function SelectDirectoryItem(Id, ResetTab = true)
{
    Studio.SelectedId = Id;
    if (Studio.Constructs.some(X => X.Id === Id))
    {
        if (Studio.ActiveConstructId !== Id)
        {
            Studio.ActiveConstructId = Id;
            const Cst = ActiveConstruct();
            RecomputePartition(false);
            Studio.Timeline.Playing = false;
            Studio.Timeline.ActiveTime = Cst.Pipeline === 'baked' ? 0.0 : 0.24;
            Studio.Timeline.ExplodeAmount = Cst.Pipeline === 'baked' ? 0.18 : 0.06;
            document.getElementById('explodeRangeInput').value = String(Studio.Timeline.ExplodeAmount);
            document.getElementById('explodeValueText').textContent = Studio.Timeline.ExplodeAmount.toFixed(2);
            SyncTimelineFrame();
        }
    }
    if (Id === 'celestial-clouds-local') Studio.CloudsParams.ActiveScope = 'local';
    if (Id === 'celestial-clouds-global') Studio.CloudsParams.ActiveScope = 'global';
    Studio.Viewport.LocalCloudCage.visible = (Id === 'celestial-clouds-local' || (Id === 'celestial-clouds-global' && Studio.CloudsParams.ActiveScope === 'local')) && Studio.CloudsParams.Local.Enabled;

    if (ResetTab) Studio.InspectorTab = 'overview';
    ConstructDockTabs();
    ConstructOutliner();
    ConstructInspectorPage();
    SyncViewportSelectors();
    C.Broadcast();
}

//------------------------------------------------------------------------------------------------------------------------
//                                      SLANTED DOCK TABS & PRESET GALLERY
//------------------------------------------------------------------------------------------------------------------------

function ConstructDockTabs()
{
    const Host = document.getElementById('construct-tabs');
    Host.textContent = '';
    for (const Entry of Studio.Constructs)
    {
        const Btn = C.Construct('button', 'document-tab' + (Studio.ActiveConstructId === Entry.Id && Studio.SelectedId === Entry.Id ? ' active' : ''), [
            C.Construct('span', 'document-label', Entry.Title)
        ]);
        Btn.type = 'button';
        Btn.addEventListener('click', () => SelectDirectoryItem(Entry.Id));
        Host.append(Btn);
    }
}

function ConstructQuickLibrary()
{
    // Solid/solver/material browsing moved out to the shared Asset Browser.
    const FilterHost = document.getElementById('quick-preset-filters');
    const ListHost = document.getElementById('quick-presets-list');
    FilterHost.style.display = 'none';
    const Search = document.getElementById('quick-preset-search');
    if (Search && Search.closest('label')) Search.closest('label').style.display = 'none';
    document.getElementById('quick-preset-count').textContent = '27';
    ListHost.textContent = '';
    const Btn = C.Construct('button', 'preset-card', [
        C.Construct('span', 'preset-swatch', C.ConstructGlyph('Boxes')),
        C.Construct('span', 'preset-copy', [
            C.Construct('strong', null, 'Browse fracture assets\u2026'),
            C.Construct('small', null, 'Materials \u00B7 solvers \u00B7 presets \u00B7 constructs')
        ]),
        C.Construct('span', 'preset-arrow', '\u2197')
    ]);
    Btn.type = 'button';
    Btn.addEventListener('click', () => { location.href = '../AssetBrowser/?ws=Fracture%20Lab'; });
    ListHost.append(Btn);
}

/* Asset Browser deep link: ?asset=fracture[-mat|-solver]/<id> spawns/selects/applies it. */
function HandleAssetDeepLink()
{
    const Raw = new URLSearchParams(location.search).get('asset') || '';
    if (!Raw.startsWith('fracture')) return;
    const Refresh = Msg => { RecomputePartition(false, Msg); ConstructOutliner(); ConstructInspectorPage(); SyncViewportSelectors(); };
    let M;
    if ((M = Raw.match(/^fracture-mat\/([a-z_]+)$/)) && MaterialCatalogue[M[1]])
    {
        const Cst = ActiveConstruct();
        Cst.Spec.Material = M[1];
        Refresh(`Material: ${MaterialCatalogue[M[1]].Label}`);
        AppendLog('FRACTURE', `Material \u201C${MaterialCatalogue[M[1]].Label}\u201D applied from the Asset Browser`, Cst.Title, 'live');
    }
    else if ((M = Raw.match(/^fracture-solver\/([a-z_]+)$/)) && SolverCatalogue.some(S => S.Id === M[1]))
    {
        const Cst = ActiveConstruct();
        Cst.Spec.Solver = M[1];
        Refresh(`Algorithm: ${M[1]}`);
        AppendLog('FRACTURE', `Solver \u201C${M[1]}\u201D applied from the Asset Browser`, Cst.Title, 'live');
    }
    else if ((M = Raw.match(/^fracture\/(construct-[a-z-]+)$/)) && Studio.Constructs.some(X => X.Id === M[1]))
    {
        SelectDirectoryItem(M[1]);
        AppendLog('FRACTURE', `Construct \u201C${ActiveConstruct().Title}\u201D opened from the Asset Browser`, null, 'live');
    }
    else if ((M = Raw.match(/^fracture\/([a-z_]+)-([a-z]+)$/)))
    {
        const P = SolidSpawnPresets.find(X => X.Cat === M[1] && X.Mat === M[2]);
        if (P) SpawnConstructPreset(P.Cat, P.Mat, P.Title, P.Solver);
        else AppendLog('FRACTURE', `Asset \u201C${Raw}\u201D not found in the spawn catalogue`, null, 'warn');
    }
}

function SpawnConstructPreset(Cat, Mat, Title, Solver = 'griffith')
{
    const Id = `construct-spawn-${++Studio.SpawnCounter}`;
    Studio.Constructs.unshift({
        Id, Title: `${Title} #${Studio.SpawnCounter}`, SolidCategory: Cat, Scale: [1, 1, 1],
        Pipeline: Solver === 'engineering_fem' ? 'baked' : 'realtime', BakedResident: false, BakedSignature: '',
        Spec: {
            Solver, Material: Mat, Energy: 3200, Seed: 100 + Studio.SpawnCounter * 19, Ceiling: 40, MinimumSize: 0.04,
            GrainAngleDeg: 0, BondStrengthScale: 1.0, PieceSdf: Solver === 'engineering_fem', SdfResolution: 64,
            ImpactPoint: [0.08, 0.18, 0.44], ImpactNormal: [0, 0, -1]
        }
    });
    AppendLog('FRACTURE', `Spawned ${Title} #${Studio.SpawnCounter}`, `${Mat} · ${Solver}`, 'live');
    SelectDirectoryItem(Id);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              PROJECT ZERO OUTLINER
//------------------------------------------------------------------------------------------------------------------------

function ConstructOutliner()
{
    if (OutlinerScope) C.ReleaseScope(OutlinerScope);
    OutlinerScope = C.OpenScope();
    const Content = document.getElementById('OutlinerContent');
    Content.textContent = '';

    const Active = ActiveConstruct();
    const Frags = Studio.Partition ? Studio.Partition.Fragments : [];
    const BakedCount = Studio.Constructs.filter(X => X.BakedResident).length;
    document.getElementById('shards-count').textContent = String(Frags.length);
    document.getElementById('baked-count').textContent = String(BakedCount);
    document.getElementById('outliner-subtitle').textContent = `SceneDirectory · ${Studio.Constructs.length + 4} nodes`;

    const MakeGroup = (Title, Count) => C.Construct('div', 'tree-group', [
        C.Construct('div', 'tree-group-title', [C.ConstructGlyph('Chevron'), C.Construct('span', null, Title), C.Construct('span', 'group-count tabular', String(Count))])
    ]);

    if (Studio.OutlinerFilter === 'all' || Studio.OutlinerFilter === 'celestial')
    {
        const Env = MakeGroup('Celestial & Volumetrics', 4);
        const Rows = [
            { Id: 'celestial-sun', Name: 'Directional Sun', Glyph: 'Sun', Col: '#e8c27a', Pill: `${Studio.SunParams.LocalHours.toFixed(1)}h`,
              Meta: () => `El ${Studio.SolarSolved.ElevationDeg.toFixed(1)}° · AM ${Studio.SolarSolved.AirMass.toFixed(2)}` },
            { Id: 'celestial-atmosphere', Name: 'Sky Atmosphere', Glyph: 'Aperture', Col: '#9fc7e8', Pill: Studio.AtmosParams.BakedFetchEnabled ? 'LUT ON' : 'Analytic',
              Meta: () => `RGBA16F 256×512 · H_R ${Studio.AtmosParams.RayleighScaleHeightKm.toFixed(1)} km` },
            { Id: 'celestial-clouds-global', Name: 'Global Cloud Slab', Glyph: 'Cloud', Col: '#8fd3c4', Pill: Studio.CloudsParams.Global.Morphology,
              Meta: () => `${Math.round(Studio.CloudsParams.Global.Coverage * 100)}% cov · ${Math.round(Studio.CloudsParams.Global.BaseHeightM)} m Z` },
            { Id: 'celestial-clouds-local', Name: 'Local Cloud Volume', Glyph: 'Boxes', Col: '#3ec46d', Pill: Studio.CloudsParams.Local.Enabled ? 'Active' : 'Muted',
              Meta: () => `Box ${(Studio.CloudsParams.Local.HalfExtentX * 2).toFixed(1)}×${(Studio.CloudsParams.Local.HalfExtentY * 2).toFixed(1)}×${(Studio.CloudsParams.Local.HalfExtentZ * 2).toFixed(1)} m` }
        ];
        for (const R of Rows)
        {
            const Match = !Studio.OutlinerQuery || `${R.Name} ${R.Meta()}`.toLowerCase().includes(Studio.OutlinerQuery);
            if (!Match) continue;
            const El = C.Construct('div', 'tree-row' + (Studio.SelectedId === R.Id ? ' selected' : ''));
            El.style.setProperty('--row-accent', R.Col);
            const Gly = C.Construct('div', 'row-glyph', C.ConstructGlyph(R.Glyph));
            const Btn = C.Construct('button', 'row-select', [Gly, C.Construct('div', 'row-copy', [C.Construct('div', 'row-name', R.Name), C.Construct('div', 'row-meta', R.Meta())])]);
            Btn.type = 'button';
            Btn.addEventListener('click', () => SelectDirectoryItem(R.Id));
            El.append(Btn, C.Construct('span', 'row-pill', R.Pill));
            Env.append(El);
        }
        Content.append(Env);
    }

    if (Studio.OutlinerFilter === 'all' || Studio.OutlinerFilter === 'constructs' || Studio.OutlinerFilter === 'baked')
    {
        const List = Studio.Constructs.filter(X =>
            (Studio.OutlinerFilter !== 'baked' || X.BakedResident)
            && (!Studio.OutlinerQuery || `${X.Title} ${X.Spec.Material} ${X.Spec.Solver}`.toLowerCase().includes(Studio.OutlinerQuery))
        );
        const Solids = MakeGroup('Destructible Solids', List.length);
        for (const Entry of List)
        {
            const Mat = MaterialCatalogue[Entry.Spec.Material] || MaterialCatalogue.concrete;
            const MatchBake = Entry.BakedResident && Entry.BakedSignature === ComputeRecipeSignature(Entry);
            const StatusText = Entry.BakedResident ? (MatchBake ? 'Baked' : 'Stale') : Entry.Pipeline === 'hybrid' ? 'Bonds' : 'Live';
            const PillCls = Entry.BakedResident ? (MatchBake ? 'row-pill baked' : 'row-pill stale') : 'row-pill';

            const El = C.Construct('div', 'tree-row' + (Studio.SelectedId === Entry.Id ? ' selected' : ''));
            El.style.setProperty('--row-accent', Mat.ExteriorHex);
            const Gly = C.Construct('div', 'row-glyph', C.ConstructGlyph('Fracture'));
            const Btn = C.Construct('button', 'row-select', [Gly, C.Construct('div', 'row-copy', [
                C.Construct('div', 'row-name', Entry.Title),
                C.Construct('div', 'row-meta', `${Mat.ShortLabel} · ${Entry.Spec.Solver}`)
            ])]);
            Btn.type = 'button';
            Btn.addEventListener('click', () => SelectDirectoryItem(Entry.Id));
            El.append(Btn, C.Construct('span', PillCls, StatusText));
            Solids.append(El);
        }
        Content.append(Solids);

        if (Studio.Partition && Frags.length)
        {
            const StreamGroup = MakeGroup(`Fragment Stream · ${Active.Title}`, Frags.length);
            const Stream = C.Construct('div', 'fragment-stream');
            for (const Frag of Frags.slice(0, 32))
            {
                const Row = C.Construct('div', 'fragment-row' + (Studio.Viewport.SelectedFragmentIndex === Frag.Index ? ' selected' : ''), [
                    C.Construct('span', null, `${Frag.Token} · ${Frag.Mass.toFixed(1)} kg`),
                    C.Construct('span', 'tabular', `${Math.round(Frag.StressRatio * 100)}%`)
                ]);
                const EyeBtn = C.Construct('button', 'row-eye', C.ConstructGlyph(Frag.Visible === false ? 'EyeOff' : 'Eye'));
                EyeBtn.type = 'button';
                EyeBtn.addEventListener('click', Ev =>
                {
                    Ev.stopPropagation();
                    Frag.Visible = Frag.Visible === false;
                    Studio.Viewport.SetFragmentVisibility(Frag.Index, Frag.Visible);
                    ConstructOutliner();
                });
                Row.append(EyeBtn);
                Row.addEventListener('click', () =>
                {
                    Studio.Viewport.SelectFragment(Frag.Index);
                    UpdateHoverBadge(Frag.Index);
                    ConstructOutliner();
                });
                Stream.append(Row);
            }
            StreamGroup.append(Stream);
            Content.append(StreamGroup);
        }
    }

    C.CloseScope(OutlinerScope);
}

//------------------------------------------------------------------------------------------------------------------------
//                                          VIEWPORT HUD & COMPARISON MATRIX
//------------------------------------------------------------------------------------------------------------------------

function SyncViewportSelectors()
{
    const Cst = ActiveConstruct();
    document.getElementById('pipeline-select').value = Cst.Pipeline;
    document.getElementById('lens-select').value = Studio.Viewport?.ShadingLens || 'pbr';
}

function RenderViewportHud()
{
    if (!Studio.Partition) return;
    const T = Studio.Partition.Receipt, M = Studio.Partition.MaterialSpec;
    const BudgetPct = Math.min(100, Math.round((T.SpentSurfaceEnergy / Math.max(1, T.TotalEnergyBudget)) * 100));
    const ManifoldPct = T.WatertightManifold ? 100 : 92;

    document.getElementById('reticleCalloutText').innerHTML =
        `<div><strong>Gc ${M.Gc} J/m²</strong> · cR ${Math.round(T.RayleighSpeedMps)} m/s</div><div class="reticle-callout-sub">${T.FragmentCount} convex hulls · ${T.ComputeDurationMs.toFixed(1)} ms</div>`;

    const Ring = (Val, Col) =>
    {
        const Circ = 2 * Math.PI * 10, Dash = (Val / 100) * Circ;
        return `<svg width="24" height="24" viewBox="0 0 26 26"><circle cx="13" cy="13" r="10" stroke="rgba(255,255,255,0.12)" stroke-width="2.4" fill="none"/><circle cx="13" cy="13" r="10" stroke="${Col}" stroke-width="2.4" fill="none" stroke-dasharray="${Dash.toFixed(1)} ${Circ.toFixed(1)}" transform="rotate(-90 13 13)" stroke-linecap="round"/></svg>`;
    };

    document.getElementById('viewportIntegrityCluster').innerHTML = `
        <div class="sector-stat-row">${Ring(ManifoldPct, '#3ec46d')}<div><div class="sector-stat-val">${ManifoldPct}%</div><div class="sector-stat-lbl">Manifold ΔV ${(T.VolumeDriftRatio * 100).toFixed(3)}%</div></div></div>
        <div class="sector-stat-row">${Ring(BudgetPct, '#e8c27a')}<div><div class="sector-stat-val">${BudgetPct}%</div><div class="sector-stat-lbl">Griffith Budget</div></div></div>`;

    const Hud = document.getElementById('ViewportHud');
    Hud.textContent = '';
    Hud.append(
        C.Construct('div', 'hud-pill', ['SHARDS', C.Construct('b', null, `${T.FragmentCount} / ${ActiveConstruct().Spec.Ceiling}`)]),
        C.Construct('div', 'hud-pill', ['SOLVE', C.Construct('b', null, `${T.ComputeDurationMs.toFixed(2)} ms`)]),
        C.Construct('div', 'hud-pill', ['BONDS', C.Construct('b', null, `${T.IntactBondCount} / ${T.BondCount}`)])
    );

    document.getElementById('status-shards').textContent = `${T.FragmentCount} shards · ${T.IntactBondCount}/${T.BondCount} bonds`;
    document.getElementById('status-drift').textContent = `ΣV drift ${T.VolumeDriftRatio.toExponential(2)} · crack area ${T.TotalCrackArea.toFixed(2)} m²`;

    UpdateHoverBadge(Studio.Viewport.SelectedFragmentIndex >= 0 ? Studio.Viewport.SelectedFragmentIndex : 0);
}

function UpdateHoverBadge(Idx)
{
    const Badge = document.getElementById('hoverFragmentBadge');
    const Frag = Studio.Partition?.Fragments?.[Idx];
    if (!Frag)
    {
        Badge.innerHTML = `<span class="swatch-dot" style="background:var(--warm)"></span><span>${ActiveConstruct().Title} · Click mesh to strike</span>`;
        return;
    }
    const Col = Frag.StressRatio > 0.65 ? '#e8c27a' : '#3ec46d';
    Badge.innerHTML = `<span class="swatch-dot" style="background:${Col}"></span><span><strong>${Frag.Token}</strong> · ${Frag.Mass.toFixed(2)} kg · ${(Frag.Volume * 1e3).toFixed(1)} L · Stress ${Math.round(Frag.StressRatio * 100)}%</span>`;
}

function OpenSolverMatrix()
{
    const Veil = document.getElementById('SolverVeil');
    const Sheet = document.getElementById('SolverSheet');
    const Cst = ActiveConstruct();

    const Paint = () =>
    {
        Sheet.textContent = '';
        const Close = C.Construct('button', 'icon-button', C.ConstructGlyph('Close'));
        Close.type = 'button';
        Close.addEventListener('click', () => { Veil.classList.remove('shown'); SyncTopNav(); });

        Sheet.append(C.Construct('div', 'library-head', [
            C.Construct('div', null, [
                C.Construct('h2', null, 'Fracture Solver Architecture Matrix'),
                C.Construct('div', 'outliner-subtitle', `Compare all 6 upstream & engineering fracture engines live on ${Cst.Title}`)
            ]),
            Close
        ]));

        const Table = C.Construct('table', 'solver-matrix-table');
        Table.innerHTML = `<thead><tr><th>Solver Engine</th><th>Lineage</th><th>CPU Cost</th><th>Realtime</th><th>Baked + SDF</th><th>Physical Mechanism</th><th>Action</th></tr></thead><tbody></tbody>`;
        const Body = Table.querySelector('tbody');
        for (const Row of SolverCatalogue)
        {
            const Active = Cst.Spec.Solver === Row.Id;
            const Tr = C.Construct('tr', Active ? 'active-solver-row' : '');
            const ActBtn = C.Construct('button', 'pill-btn' + (Active ? ' active' : ''), Active ? 'Active' : 'Activate');
            ActBtn.type = 'button';
            ActBtn.addEventListener('click', () =>
            {
                Cst.Spec.Solver = Row.Id;
                Cst.Pipeline = Row.Id === 'runtime_boussinesq' ? 'realtime' : Row.Id === 'engineering_fem' ? 'baked' : Cst.Pipeline;
                RecomputePartition(true, `Switched solver to ${Row.Name}`);
                ConstructOutliner();
                ConstructInspectorPage();
                SyncViewportSelectors();
                Veil.classList.remove('shown');
                SyncTopNav();
            });
            Tr.innerHTML = `<td><strong>${Row.Name}</strong></td><td style="color:#8f8f8f">${Row.Lineage}</td><td>${Row.Cpu}</td><td style="color:#8fe0ad">${Row.Realtime}</td><td style="color:#e8c27a">${Row.Baked}</td><td style="color:#a0a0a0">${Row.Mechanism}</td><td></td>`;
            Tr.lastElementChild.append(ActBtn);
            Body.append(Tr);
        }
        Sheet.append(C.Construct('div', 'solver-table-wrap', Table));
    };

    Paint();
    Veil.classList.add('shown');
    SyncTopNav();
    Veil.onclick = Ev => { if (Ev.target === Veil) { Veil.classList.remove('shown'); SyncTopNav(); } };
}

//------------------------------------------------------------------------------------------------------------------------
//                                  SUN + FLUID HYBRID INSPECTOR PAGES
//------------------------------------------------------------------------------------------------------------------------

const Range = (Host, Binding, Min, Max, Step, Digits, Unit) =>
    C.SliderRow(Host, Object.assign(Binding, { Minimum: Min, Maximum: Max, Step, Digits, Unit }));

function RenderInspectorTabs(Defs)
{
    const Bar = document.getElementById('inspector-tabs');
    Bar.textContent = '';
    if (!Defs.some(D => D[0] === Studio.InspectorTab)) Studio.InspectorTab = Defs[0][0];
    for (const [Id, Label] of Defs)
    {
        const Btn = C.Construct('button', Studio.InspectorTab === Id ? 'active' : '', Label);
        Btn.type = 'button';
        Btn.addEventListener('click', () => { Studio.InspectorTab = Id; ConstructInspectorPage(); C.Broadcast(); });
        Bar.append(Btn);
    }
}

function RedrawDiagrams()
{
    for (const Fn of PendingRedraws) Fn();
}

function ConstructFractureInspector(Page, Cst)
{
    const Spec = Cst.Spec;
    const Mat = MaterialCatalogue[Spec.Material] || MaterialCatalogue.concrete;
    const Rec = Studio.Partition.Receipt;
    const Eng = Studio.Partition.EngineeringReport || null;
    const SdfFoot = EvaluateSdfAtlasFootprint(Rec.FragmentCount, Spec.SdfResolution, Spec.PieceSdf);
    const BakedValid = Cst.BakedResident && Cst.BakedSignature === ComputeRecipeSignature(Cst);

    RenderInspectorTabs([
        ['overview', 'Overview'],
        ['physics', 'Physics & Grain'],
        ['bonds', 'Bonds & Locus'],
        ['sdf', 'SDF & Record']
    ]);

    if (Studio.InspectorTab === 'overview')
    {
        // Card 01: Sun Hero Partition Receipt + Griffith/Navier-Cauchy Canvas
        const Hero = C.Construct('div', 'hero-card', [
            C.Construct('div', 'hero-top', [
                C.Construct('div', 'budget-chip', [
                    C.Construct('div', 'mark ' + (BakedValid || !Cst.BakedResident ? 'live' : 'warm'), C.ConstructGlyph('Check')),
                    C.Construct('span', null, BakedValid ? 'Baked archive resident' : Cst.BakedResident ? 'Bake stale · needs refresh' : 'Realtime solver ready')
                ]),
                C.Construct('div', 'estimate-tag', '01 / PARTITION RECEIPT')
            ]),
            C.Construct('div', 'hero-title', Eng ? 'Navier–Cauchy interlocking fractography' : 'Watertight convex hull decomposition'),
            C.Construct('div', 'hero-metrics', [
                C.Construct('div', 'metric-big', [String(Rec.FragmentCount), C.Construct('small', null, `/ ${Spec.Ceiling} shards`)]),
                C.Construct('div', 'metric-side', [
                    C.Construct('div', 'rate', [`${Rec.ComputeDurationMs.toFixed(2)}`, C.Construct('small', null, 'ms CPU')]),
                    C.Construct('div', 'low', Eng ? `${Eng.NonPlanarTriangles} interlocking tris` : `${Studio.Viewport.ActiveDrawCalls} GPU draw calls`)
                ])
            ])
        ]);

        if (Eng)
        {
            const EngCanvas = C.Construct('canvas', 'diagram-canvas');
            EngCanvas.height = 122;
            Hero.append(EngCanvas);
            PendingRedraws.push(() => DrawEngineeringStressSliceCanvas(EngCanvas, Eng, '#e8c27a'));
        }
        const WfCanvas = C.Construct('canvas', 'diagram-canvas');
        WfCanvas.height = 128;
        Hero.append(WfCanvas);
        PendingRedraws.push(() => DrawGriffithWaveformDiagram(WfCanvas, Studio.Partition, Spec, '#e8c27a'));
        Page.append(Hero);

        // Signature Sun / Fluid 3-Card KPI Highlight Strip
        C.MetricTrio(Page, [
            { Label: 'Crack area', Resolve: () => [`${Rec.TotalCrackArea.toFixed(2)} m²`, `2·Gc = ${2 * Mat.Gc} J/m²`] },
            { Label: 'Energy', Resolve: () => [`${(Spec.Energy / 1e3).toFixed(2)} kJ`, `seed #${Spec.Seed}`] },
            { Label: Eng ? 'Fractography H' : 'Wave cR', Highlight: true, Resolve: () => Eng ? [`H = ${Eng.HurstExponent.toFixed(2)}`, `${Eng.RoughnessRmsMm.toFixed(2)} mm rms`] : [`${Math.round(Rec.RayleighSpeedMps)} m/s`, 'Rayleigh speed'] }
        ]);

        // Quick Toggle Tiles
        Page.append(C.Construct('div', 'section-subhead', [C.Construct('span', null, 'Quick controls'), C.Construct('span', null, Mat.ShortLabel)]));
        const Grid = C.Construct('div', 'property-switches');
        C.ToggleTile(Grid, {
            Label: 'Piece SDF', Glyph: 'Layers',
            Read: () => Spec.PieceSdf,
            Write: V => { Spec.PieceSdf = V; if (V && Cst.Pipeline === 'realtime') Cst.Pipeline = 'baked'; RecomputePartition(false, `Piece SDF ${V ? 'enabled' : 'disabled'}`); ConstructInspectorPage(); SyncViewportSelectors(); }
        });
        C.ToggleTile(Grid, {
            Label: 'Eng. Bake', Glyph: 'Archive',
            Read: () => Spec.Solver === 'engineering_fem',
            Write: V => { Spec.Solver = V ? 'engineering_fem' : 'runtime_boussinesq'; Cst.Pipeline = V ? 'baked' : 'realtime'; RecomputePartition(false, `Solver: ${Spec.Solver}`); ConstructInspectorPage(); SyncViewportSelectors(); }
        });
        C.ToggleTile(Grid, {
            Label: 'Hybrid Bonds', Glyph: 'Activity',
            Read: () => Cst.Pipeline === 'hybrid',
            Write: V => { Cst.Pipeline = V ? 'hybrid' : 'realtime'; RecomputePartition(false, `Pipeline: ${Cst.Pipeline}`); ConstructInspectorPage(); SyncViewportSelectors(); }
        });
        C.ToggleTile(Grid, {
            Label: 'Bake Cache', Glyph: 'Check',
            Read: () => BakedValid,
            Write: () => { Cst.BakedResident = true; Cst.BakedSignature = ComputeRecipeSignature(Cst); Cst.Pipeline = 'baked'; ConstructOutliner(); ConstructInspectorPage(); SyncViewportSelectors(); AppendLog('FRACTURE', `Baked resident archive for ${Cst.Title}`, `${Rec.FragmentCount} shards`, 'live'); }
        });
        Page.append(Grid);

        // Quick Pipeline & Solver Selection Card
        const PipeCard = C.SectionCard(Page, { Title: 'Execution pipeline & solver', Glyph: 'Sliders', Badge: Spec.Solver.toUpperCase() }).Inner;
        C.SegmentedRow(PipeCard, {
            Label: 'Pipeline mode',
            Options: [['Realtime (<1ms)', 'realtime'], ['Eng. Bake', 'baked'], ['Hybrid Bonds', 'hybrid']],
            Read: () => Cst.Pipeline,
            Write: V => { Cst.Pipeline = V; if (V === 'realtime') Spec.Solver = 'runtime_boussinesq'; else if (V === 'baked' && Spec.Solver === 'runtime_boussinesq') Spec.Solver = 'engineering_fem'; RecomputePartition(V === 'baked', `Pipeline: ${V}`); ConstructOutliner(); ConstructInspectorPage(); SyncViewportSelectors(); }
        });
        C.SelectRow(PipeCard, {
            Label: 'Active fracture algorithm',
            Options: SolverCatalogue.map(S => [S.Name, S.Id]),
            Read: () => Spec.Solver,
            Write: V => { Spec.Solver = V; RecomputePartition(false, `Algorithm: ${V}`); ConstructOutliner(); ConstructInspectorPage(); }
        });

        C.ActionRow(Page, [
            { Label: 'Strike Mesh', Glyph: 'Zap', Tone: 'accent', Act: TriggerStrike },
            { Label: 'Re-seed #', Glyph: 'Reset', Act: () => { Spec.Seed = Math.floor(1 + Math.random() * 9999); RecomputePartition(true, `New seed #${Spec.Seed}`); ConstructInspectorPage(); } },
            { Label: 'Matrix (6)', Glyph: 'Grid', Act: OpenSolverMatrix }
        ]);
        return;
    }

    if (Studio.InspectorTab === 'physics')
    {
        const MatSec = C.SectionCard(Page, { Title: 'Constitutive material & grain', Glyph: 'Atom', Badge: Mat.FractureCategory.toUpperCase() }).Inner;
        const Chips = C.Construct('div', 'chip-grid');
        for (const [Key, Info] of Object.entries(MaterialCatalogue))
        {
            const Btn = C.Construct('button', 'choice-chip' + (Spec.Material === Key ? ' active' : ''), [
                C.Construct('span', 'swatch-dot'),
                C.Construct('span', null, Info.ShortLabel)
            ]);
            Btn.firstChild.style.background = Info.ExteriorHex;
            Btn.type = 'button';
            Btn.addEventListener('click', () => { Spec.Material = Key; RecomputePartition(false, `Material: ${Info.Label}`); ConstructOutliner(); ConstructInspectorPage(); });
            Chips.append(Btn);
        }
        MatSec.append(Chips);

        // Spectrum Bars & Interactive Draggable Polar Anisotropy Rose
        const Polar = C.Construct('canvas', 'diagram-canvas');
        Polar.height = 128;
        Polar.title = 'Click/drag to rotate grain & bedding orientation';
        Polar.addEventListener('pointerdown', Ev =>
        {
            const R = Polar.getBoundingClientRect();
            let Deg = (Math.atan2(Ev.clientY - (R.top + R.height * 0.52), Ev.clientX - (R.left + R.width * 0.5)) * 180) / Math.PI;
            if (Deg < 0) Deg += 180;
            Spec.GrainAngleDeg = Math.round(Deg);
            RecomputePartition(false);
            ConstructInspectorPage();
        });
        PendingRedraws.push(() => DrawPolarAnisotropyRadar(Polar, Mat, Spec.GrainAngleDeg, '#e8c27a'));

        const Specs = C.Construct('div', 'spectrum-band-list', [
            MakeSpectrumRow('Gc Toughness', Math.min(100, (Mat.Gc / 500) * 100), '#e8c27a', `${Mat.Gc} J/m²`),
            MakeSpectrumRow('Anisotropy', Math.min(100, (Mat.AnisotropyRatio / 11) * 100), '#3ec46d', `${Mat.AnisotropyRatio.toFixed(1)}×`),
            MakeSpectrumRow('Wave cR', Math.min(100, (Rec.RayleighSpeedMps / 3400) * 100), '#60a5fa', `${Math.round(Rec.RayleighSpeedMps)} m/s`)
        ]);
        MatSec.append(C.Construct('div', 'bento-two-col', [Polar, Specs]));

        const Tune = C.SectionCard(Page, { Title: 'Energy budget & crack limits', Glyph: 'Gauge', Badge: 'WEIBULL' }).Inner;
        Range(Tune, { Label: 'Impact kinetic energy', Read: () => Spec.Energy, Write: V => { Spec.Energy = V; RecomputePartition(false); } }, 100, 15000, 50, 0, 'J');
        Range(Tune, { Label: 'Shard ceiling', Read: () => Spec.Ceiling, Write: V => { Spec.Ceiling = Math.round(V); RecomputePartition(false); } }, 4, 140, 1, 0, 'pcs');
        Range(Tune, { Label: 'Minimum shard span', Read: () => Spec.MinimumSize, Write: V => { Spec.MinimumSize = V; RecomputePartition(false); } }, 0.015, 0.22, 0.005, 3, 'm');
        Range(Tune, { Label: 'Orthotropic grain axis θ', Read: () => Spec.GrainAngleDeg, Write: V => { Spec.GrainAngleDeg = Math.round(V); RecomputePartition(false); } }, 0, 180, 1, 0, '°');
        return;
    }

    if (Studio.InspectorTab === 'bonds')
    {
        const BondSec = C.SectionCard(Page, { Title: 'Adjacency strain bonds', Glyph: 'Lattice', Badge: `${Rec.IntactBondCount}/${Rec.BondCount}` }).Inner;
        BondSec.append(C.Construct('div', 'hatched-compare-grid', [
            C.Construct('div', 'hatched-block', [C.Construct('span', null, 'Intact bonds'), C.Construct('strong', null, String(Rec.IntactBondCount))]),
            C.Construct('div', 'solid-compare-block', [C.Construct('span', null, 'Ruptured'), C.Construct('strong', null, String(Rec.BondCount - Rec.IntactBondCount))])
        ]));
        Range(BondSec, { Label: 'Bond tensile capacity scale', Read: () => Spec.BondStrengthScale, Write: V => { Spec.BondStrengthScale = V; RecomputePartition(false); } }, 0.15, 2.5, 0.05, 2, '×');

        const LocSec = C.SectionCard(Page, { Title: 'Impact locus & strike vector', Glyph: 'Crosshair', Badge: '3D RAYCAST' }).Inner;
        C.VectorRow(LocSec, {
            Label: 'Impact point (local solid space)', Unit: 'm', Step: 0.02, Digits: 2, Minimum: -1, Maximum: 1,
            Read: () => Spec.ImpactPoint, Write: V => { Spec.ImpactPoint = V; RecomputePartition(false); }
        });
        C.VectorRow(LocSec, {
            Label: 'Strike surface normal', Unit: '', Step: 0.05, Digits: 2, Minimum: -1, Maximum: 1,
            Read: () => Spec.ImpactNormal, Write: V => { Spec.ImpactNormal = V; RecomputePartition(false); }
        });
        return;
    }

    // SDF & Record tab
    const SdfSec = C.SectionCard(Page, { Title: 'Piece SDF voxel atlas', Glyph: 'Boxes', Badge: Spec.PieceSdf ? `${SdfFoot.Megabytes.toFixed(2)} MB` : 'OFF' }).Inner;
    C.SwitchRow(SdfSec, { Label: 'Bake per-piece signed distance bricks', Read: () => Spec.PieceSdf, Write: V => { Spec.PieceSdf = V; ConstructInspectorPage(); } });
    C.SegmentedRow(SdfSec, {
        Label: 'SDF grid resolution', Options: [['32³', 32], ['64³', 64], ['128³', 128]],
        Read: () => Spec.SdfResolution,
        Write: V => { Spec.SdfResolution = V; Spec.PieceSdf = true; Studio.Viewport.RebuildSdfSliceVisual(Math.min(96, V)); ConstructInspectorPage(); }
    });
    const SdfCanvas = C.Construct('canvas', 'diagram-canvas');
    SdfCanvas.height = 112;
    SdfSec.append(SdfCanvas);
    PendingRedraws.push(() => DrawPieceSdfSliceCanvas(SdfCanvas, Studio.Partition.Fragments, Studio.Viewport.SdfSliceY, Spec.SdfResolution, Spec.PieceSdf));

    const LitCount = Math.min(32, Math.round((Rec.FragmentCount / 140) * 32));
    const LedGrid = C.Construct('div', 'led-matrix-grid');
    LedGrid.style.marginTop = '10px';
    LedGrid.innerHTML = Array.from({ length: 32 }, (_, I) => `<div class="led-dot ${Spec.PieceSdf && I < LitCount ? 'lit' : ''}"></div>`).join('');
    SdfSec.append(LedGrid);
    Range(SdfSec, { Label: 'SDF iso-slice elevation Y', Read: () => Studio.Viewport.SdfSliceY, Write: V => { Studio.Viewport.SdfSliceY = V; Studio.Viewport.RebuildSdfSliceVisual(Math.min(96, Spec.SdfResolution)); RedrawDiagrams(); } }, -0.75, 0.75, 0.03, 2, 'm');

    const Arch = C.SectionCard(Page, { Title: 'Baked archive & TOML record', Glyph: 'Archive', Badge: 'PERSISTENCE' }).Inner;
    const BakeCard = C.Construct('div', 'pocket-card' + (BakedValid ? ' active' : ''), [
        C.Construct('div', null, C.Construct('strong', null, 'Finalize Bake Archive')),
        C.Construct('div', 'pocket-folder-art', [
            C.Construct('div', 'fanned-sheet s1'), C.Construct('div', 'fanned-sheet s2'), C.Construct('div', 'fanned-sheet s3'),
            C.Construct('div', 'folder-front-lip', BakedValid ? 'Resident ✓' : `Bake ${Rec.FragmentCount} pcs`)
        ])
    ]);
    BakeCard.addEventListener('click', () => { Cst.BakedResident = true; Cst.BakedSignature = ComputeRecipeSignature(Cst); Cst.Pipeline = 'baked'; ConstructOutliner(); ConstructInspectorPage(); C.Toast(`Baked ${Cst.Title}`); });
    const ExpCard = C.Construct('div', 'pocket-card', [
        C.Construct('div', null, C.Construct('strong', null, 'Download .fracture.toml')),
        C.Construct('div', 'pocket-folder-art', [
            C.Construct('div', 'fanned-sheet s1'), C.Construct('div', 'fanned-sheet s2'), C.Construct('div', 'fanned-sheet s3'),
            C.Construct('div', 'folder-front-lip', 'Frontier TOML')
        ])
    ]);
    ExpCard.addEventListener('click', () => document.getElementById('export-button').click());
    Arch.append(C.Construct('div', 'pocket-archive-grid', [BakeCard, ExpCard]));
    const Pre = C.Construct('pre', 'levelspace', EncodeFractureToml(Cst));
    Arch.append(Pre);
}

function MakeSpectrumRow(Label, Pct, Color, Text)
{
    const Track = C.Construct('div', 'spectrum-track');
    const Fill = C.Construct('div', 'spectrum-fill');
    Fill.style.width = `${Pct}%`;
    Fill.style.background = Color;
    Track.append(Fill);
    return C.Construct('div', 'spectrum-band-row', [
        C.Construct('span', null, Label), Track, C.Construct('span', 'tabular', Text)
    ]);
}

function ConstructSunInspector(Page)
{
    const P = Studio.SunParams, S = Studio.SolarSolved;
    const FmtClock = H => H == null ? '--:--' : `${String(Math.floor(H) % 24).padStart(2, '0')}:${String(Math.floor((H % 1) * 60)).padStart(2, '0')}`;
    const RefreshSun = (Rebuild = false) =>
    {
        Studio.SolarSolved = EvaluateSolarEphemeris(Studio.SunParams);
        AppendLog('SUN', `Sun updated: El ${Studio.SolarSolved.ElevationDeg.toFixed(1)}°`, `Az ${Studio.SolarSolved.AzimuthDeg.toFixed(1)}°`, 'warm');
        ConstructOutliner();
        if (Rebuild) ConstructInspectorPage(); else RedrawDiagrams();
    };

    RenderInspectorTabs([
        ['overview', 'Cycle & Arc'],
        ['optics', 'Disc & Tint'],
        ['coords', 'Coordinates'],
        ['notice', 'Notice']
    ]);

    if (Studio.InspectorTab === 'overview')
    {
        Page.append(C.Construct('div', 'bento-two-col', [
            C.Construct('div', 'mini-metric-box', [C.Construct('span', 'mini-metric-lbl', 'Solar Elevation α'), C.Construct('div', 'mini-metric-val', `${S.ElevationDeg.toFixed(1)}°`)]),
            C.Construct('div', 'mini-metric-box', [C.Construct('span', 'mini-metric-lbl', 'Solar Azimuth γ'), C.Construct('div', 'mini-metric-val', `${S.AzimuthDeg.toFixed(1)}°`)])
        ]));

        const ArcCard = C.SectionCard(Page, { Title: '24-Hour Diurnal Solar Path', Glyph: 'Sun', Badge: FmtClock(P.LocalHours), Hint: `Sunrise ${FmtClock(S.SunriseHour)} · Noon ${FmtClock(S.SolarNoonHour)} · Sunset ${FmtClock(S.SunsetHour)}` }).Inner;
        const Arc = C.Construct('canvas', 'diagram-canvas');
        Arc.height = 138;
        let Drag = false;
        const Scrub = Ev => { const R = Arc.getBoundingClientRect(); P.LocalHours = Number((Clamp((Ev.clientX - R.left - 10) / Math.max(1, R.width - 44), 0, 1) * 24).toFixed(2)); RefreshSun(true); };
        Arc.addEventListener('pointerdown', Ev => { Drag = true; Scrub(Ev); });
        window.addEventListener('pointermove', Ev => { if (Drag) Scrub(Ev); });
        window.addEventListener('pointerup', () => { Drag = false; });
        ArcCard.append(Arc);
        PendingRedraws.push(() => DrawSolarDiurnalArcCanvas(Arc, P, S, '#e8c27a'));

        Range(ArcCard, { Label: 'Local solar hours', Read: () => P.LocalHours, Write: V => { P.LocalHours = V; RefreshSun(false); } }, 0, 24, 0.05, 2, 'h');
        C.SwitchRow(ArcCard, { Label: 'Animate diurnal orbit', Read: () => P.AnimateCycle, Write: V => { P.AnimateCycle = V; RefreshSun(false); } });
        C.SegmentedRow(ArcCard, { Label: 'Orbit speed multiplier', Options: [['0.5×', 0.5], ['1×', 1], ['6×', 6], ['24×', 24]], Read: () => P.CycleSpeed, Write: V => { P.CycleSpeed = V; } });
        return;
    }

    if (Studio.InspectorTab === 'optics')
    {
        const Sec = C.SectionCard(Page, { Title: 'Solar disc & spectral tint', Glyph: 'Aperture', Badge: `AM ${S.AirMass.toFixed(2)}` }).Inner;
        const Disc = C.Construct('canvas', 'diagram-canvas');
        Disc.height = 84;
        Sec.append(Disc);
        PendingRedraws.push(() => DrawSunDiscPreviewCanvas(Disc, P));
        Range(Sec, { Label: 'Angular diameter', Read: () => P.AngularDiameterDeg, Write: V => { P.AngularDiameterDeg = V; RedrawDiagrams(); } }, 0.1, 4.0, 0.01, 3, '°');
        Range(Sec, { Label: 'Tint red', Read: () => P.TintRgb[0], Write: V => { P.TintRgb[0] = V; RedrawDiagrams(); } }, 0.2, 1.4, 0.01, 2, '');
        Range(Sec, { Label: 'Tint green', Read: () => P.TintRgb[1], Write: V => { P.TintRgb[1] = V; RedrawDiagrams(); } }, 0.2, 1.4, 0.01, 2, '');
        Range(Sec, { Label: 'Tint blue', Read: () => P.TintRgb[2], Write: V => { P.TintRgb[2] = V; RedrawDiagrams(); } }, 0.2, 1.4, 0.01, 2, '');

        const Gain = C.SectionCard(Page, { Title: 'Sunlight gain multipliers', Glyph: 'Rays', Badge: 'GAINS' }).Inner;
        Range(Gain, { Label: 'Sky scatter multiplier', Read: () => P.SkyGainMultiplier, Write: V => { P.SkyGainMultiplier = V; } }, 0, 3, 0.05, 2, '×');
        Range(Gain, { Label: 'Direct beam multiplier', Read: () => P.DirectBeamMultiplier, Write: V => { P.DirectBeamMultiplier = V; } }, 0, 4, 0.05, 2, '×');
        return;
    }

    if (Studio.InspectorTab === 'coords')
    {
        const MapSec = C.SectionCard(Page, { Title: 'Observer geodetic graticule', Glyph: 'Crosshair', Badge: `Month ${P.Month}/${P.Day}` }).Inner;
        const MapCanvas = C.Construct('canvas', 'diagram-canvas');
        MapCanvas.height = 104;
        MapCanvas.addEventListener('pointerdown', Ev =>
        {
            const R = MapCanvas.getBoundingClientRect();
            P.LongitudeDeg = Number((-180 + Clamp((Ev.clientX - R.left) / Math.max(1, R.width), 0, 1) * 360).toFixed(1));
            P.LatitudeDeg = Number((90 - Clamp((Ev.clientY - R.top) / Math.max(1, R.height), 0, 1) * 180).toFixed(1));
            RefreshSun(true);
        });
        MapSec.append(MapCanvas);
        PendingRedraws.push(() => DrawObserverWorldMapCanvas(MapCanvas, P.LatitudeDeg, P.LongitudeDeg, '#e8c27a'));
        Range(MapSec, { Label: 'Latitude', Read: () => P.LatitudeDeg, Write: V => { P.LatitudeDeg = V; RefreshSun(false); } }, -85, 85, 0.5, 1, '°');
        Range(MapSec, { Label: 'Longitude', Read: () => P.LongitudeDeg, Write: V => { P.LongitudeDeg = V; RefreshSun(false); } }, -180, 180, 0.5, 1, '°');
        Range(MapSec, { Label: 'Calendar month', Read: () => P.Month, Write: V => { P.Month = Math.round(V); RefreshSun(false); } }, 1, 12, 1, 0, '');
        Range(MapSec, { Label: 'Calendar day', Read: () => P.Day, Write: V => { P.Day = Math.round(V); RefreshSun(false); } }, 1, 31, 1, 0, '');
        return;
    }

    Page.append(C.Construct('div', 'capability-notice', 'Directional Sun is solved analytically per frame (Declination δ, Equation of Time, Kasten–Young Air Mass, solar disc chromaticity). Octahedral 256×512 RGBA16F HDR sky baking lives in Sky Atmosphere.'));
}

function ConstructAtmosphereInspector(Page)
{
    const P = Studio.AtmosParams;
    RenderInspectorTabs([
        ['overview', 'LUT Bake'],
        ['profiles', 'Optical Curves'],
        ['twilight', 'Twilight & Extent'],
        ['defaults', 'Specs']
    ]);

    if (Studio.InspectorTab === 'overview')
    {
        const Sec = C.SectionCard(Page, { Title: '256 × 512 RGBA16F Sky Bake Atlas', Glyph: 'Aperture', Badge: `REV #${P.BakedRevision}` }).Inner;
        C.SegmentedRow(Sec, {
            Label: 'Atlas preview mode',
            Options: [['Altitude', 'altitude'], ['Panorama 4:1', 'panorama'], ['Transmit', 'transmittance'], ['Sphere 2:1', 'sphere']],
            Read: () => P.PreviewMode, Write: V => { P.PreviewMode = V; RedrawDiagrams(); }
        });
        const Lut = C.Construct('canvas', 'diagram-canvas');
        Lut.height = 114;
        Sec.append(Lut);
        PendingRedraws.push(() => DrawAtmosphereLutCanvas(Lut, P, Studio.SunParams, Studio.SolarSolved));
        C.SwitchRow(Sec, { Label: 'Sample baked octahedral LUT', Read: () => P.BakedFetchEnabled, Write: V => { P.BakedFetchEnabled = V; ConstructOutliner(); } });
        C.SwitchRow(Sec, { Label: 'Analytic civil horizon band', Read: () => P.CivilTwilightEnabled, Write: V => { P.CivilTwilightEnabled = V; P.HorizonWhiteLine = V ? 0.24 : 0.04; RedrawDiagrams(); } });
        C.ActionRow(Sec, [{ Label: 'Re-bake 256×512 Sky Atlas', Glyph: 'Sparkles', Tone: 'accent', Act: () => { P.BakedSunDirection = [...Studio.SolarSolved.Direction]; P.BakedRevision++; AppendLog('ATMOS', `Baked sky atlas rev #${P.BakedRevision}`, null, 'live'); ConstructInspectorPage(); } }]);
        return;
    }

    if (Studio.InspectorTab === 'profiles')
    {
        const Sec = C.SectionCard(Page, { Title: 'Rayleigh, Mie & Ozone Optical Profiles', Glyph: 'Activity', Badge: '0–100 KM' }).Inner;
        const Curves = C.Construct('canvas', 'diagram-canvas');
        Curves.height = 132;
        Sec.append(Curves);
        PendingRedraws.push(() => DrawAtmosphereDensityCurvesCanvas(Curves, P, '#e8c27a'));
        Range(Sec, { Label: 'Rayleigh scatter ×', Read: () => P.RayleighMultiplier, Write: V => { P.RayleighMultiplier = V; RedrawDiagrams(); } }, 0.1, 3.0, 0.05, 2, '×');
        Range(Sec, { Label: 'Rayleigh scale H_R', Read: () => P.RayleighScaleHeightKm, Write: V => { P.RayleighScaleHeightKm = V; RedrawDiagrams(); } }, 2.0, 16.0, 0.2, 1, 'km');
        Range(Sec, { Label: 'Mie haze ×', Read: () => P.MieMultiplier, Write: V => { P.MieMultiplier = V; RedrawDiagrams(); } }, 0.05, 4.0, 0.05, 2, '×');
        Range(Sec, { Label: 'Mie phase g', Read: () => P.MiePhaseG, Write: V => { P.MiePhaseG = V; RedrawDiagrams(); } }, -0.5, 0.95, 0.01, 2, '');
        Range(Sec, { Label: 'Mie scale H_M', Read: () => P.MieScaleHeightKm, Write: V => { P.MieScaleHeightKm = V; RedrawDiagrams(); } }, 0.3, 5.0, 0.1, 2, 'km');
        Range(Sec, { Label: 'Ozone tent ×', Read: () => P.OzoneMultiplier, Write: V => { P.OzoneMultiplier = V; RedrawDiagrams(); } }, 0, 3, 0.05, 2, '×');
        return;
    }

    if (Studio.InspectorTab === 'twilight')
    {
        const Sec = C.SectionCard(Page, { Title: 'Planet extent, twilight & albedo', Glyph: 'Planet', Badge: `TOP ${P.GroundRadiusKm + P.AtmosphereHeightKm} KM` }).Inner;
        Range(Sec, { Label: 'Atmosphere shell thickness', Read: () => P.AtmosphereHeightKm, Write: V => { P.AtmosphereHeightKm = V; RedrawDiagrams(); } }, 50, 140, 2, 0, 'km');
        Range(Sec, { Label: 'Sky brightness multiplier', Read: () => P.SkyBrightness, Write: V => { P.SkyBrightness = V; } }, 0.2, 2.5, 0.05, 2, '×');
        Range(Sec, { Label: 'Twilight scattering glow', Read: () => P.TwilightGlow, Write: V => { P.TwilightGlow = V; RedrawDiagrams(); } }, 0, 1.5, 0.05, 2, '');
        Range(Sec, { Label: 'Ground Lambert albedo', Read: () => P.GroundAlbedo, Write: V => { P.GroundAlbedo = V; } }, 0, 0.9, 0.02, 2, '');
        Range(Sec, { Label: 'Mie absorption ×', Read: () => P.MieAbsorptionMultiplier, Write: V => { P.MieAbsorptionMultiplier = V; } }, 0, 3, 0.05, 2, '×');
        Range(Sec, { Label: 'Horizon white band', Read: () => P.HorizonWhiteLine, Write: V => { P.HorizonWhiteLine = V; } }, 0, 1, 0.02, 2, '');
        return;
    }

    Page.append(C.Construct('div', 'capability-notice', `18 Native Bruneton & Neyret / Hillaire atmospheric parameters active. Ground radius Rg=${P.GroundRadiusKm} km, Top radius Rt=${P.GroundRadiusKm + P.AtmosphereHeightKm} km.`));
}

function ConstructCloudsInspector(Page)
{
    const Scope = Studio.CloudsParams.ActiveScope, Glob = Studio.CloudsParams.Global, Loc = Studio.CloudsParams.Local;
    const Cur = Scope === 'global' ? Glob : Loc;

    RenderInspectorTabs([
        ['overview', 'Density & Phase'],
        ['slab', Scope === 'global' ? 'World-Z Slab' : 'Local Volume'],
        ['wind', 'Wind & Shadows']
    ]);

    if (Studio.InspectorTab === 'overview')
    {
        const Top = C.SectionCard(Page, { Title: 'Volumetric scope & switches', Glyph: 'Cloud', Badge: Scope.toUpperCase() }).Inner;
        C.SegmentedRow(Top, {
            Label: 'Active cloud volume', Options: [['Global Cloud Slab', 'global'], ['Local Volume Box', 'local']],
            Read: () => Studio.CloudsParams.ActiveScope,
            Write: V => { Studio.CloudsParams.ActiveScope = V; SelectDirectoryItem(V === 'local' ? 'celestial-clouds-local' : 'celestial-clouds-global', false); }
        });
        C.SwitchRow(Top, { Label: 'Medium enabled', Read: () => Cur.Enabled, Write: V => { Cur.Enabled = V; ConstructOutliner(); } });
        C.SwitchRow(Top, { Label: 'Follow wind advection', Read: () => Cur.FollowWind, Write: V => { Cur.FollowWind = V; } });

        const MaskSec = C.SectionCard(Page, { Title: 'Top-down density field 1 − exp(−3ρ)', Glyph: 'Layers', Badge: `${Math.round(Cur.Coverage * 100)}% COV` }).Inner;
        const Mask = C.Construct('canvas', 'diagram-canvas');
        Mask.height = 120;
        MaskSec.append(Mask);
        PendingRedraws.push(() => DrawCloudDensityMaskCanvas(Mask, Studio.CloudsParams));
        Range(MaskSec, { Label: 'Noise coverage', Read: () => Cur.Coverage * 100, Write: V => { Cur.Coverage = V / 100; ConstructOutliner(); RedrawDiagrams(); } }, 0, 100, 1, 0, '%');
        Range(MaskSec, { Label: 'Extinction density scale', Read: () => Cur.DensityScale, Write: V => { Cur.DensityScale = V; RedrawDiagrams(); } }, 0.1, 3.0, 0.05, 2, '');
        Range(MaskSec, { Label: 'Feature scale', Read: () => Cur.FeatureScale, Write: V => { Cur.FeatureScale = V; RedrawDiagrams(); } }, 0.3, 3.0, 0.05, 2, '×');
        Range(MaskSec, { Label: 'Henyey-Greenstein g', Read: () => Cur.PhaseG, Write: V => { Cur.PhaseG = V; } }, -0.4, 0.85, 0.02, 2, '');
        return;
    }

    if (Studio.InspectorTab === 'slab')
    {
        if (Scope === 'global')
        {
            const Sec = C.SectionCard(Page, { Title: 'World-Z slab & morphology', Glyph: 'Cloud', Badge: Glob.Morphology.toUpperCase() }).Inner;
            const Slab = C.Construct('canvas', 'diagram-canvas');
            Slab.height = 124;
            Slab.addEventListener('pointerdown', Ev =>
            {
                const R = Slab.getBoundingClientRect(), F = 1 - Clamp((Ev.clientY - R.top - 10) / Math.max(1, R.height - 26), 0, 1);
                Glob.BaseHeightM = Clamp(Math.round((F * 6000) / 100) * 100, 200, Glob.CeilingHeightM - 100);
                ConstructOutliner(); ConstructInspectorPage();
            });
            Sec.append(Slab);
            PendingRedraws.push(() => DrawCloudAltitudeSlabCanvas(Slab, Studio.CloudsParams, '#e8c27a'));
            C.SelectRow(Sec, { Label: 'Cloud morphology', Options: ['cumulus', 'stratocumulus', 'cirrus', 'cumulonimbus', 'stratus', 'altocumulus'].map(M => [M, M]), Read: () => Glob.Morphology, Write: V => { Glob.Morphology = V; ConstructOutliner(); RedrawDiagrams(); } });
            Range(Sec, { Label: 'Base world Z', Read: () => Glob.BaseHeightM, Write: V => { Glob.BaseHeightM = Math.min(V, Glob.CeilingHeightM - 100); RedrawDiagrams(); } }, 200, 5000, 100, 0, 'm');
            Range(Sec, { Label: 'Slab thickness', Read: () => Glob.ThicknessM, Write: V => { Glob.ThicknessM = V; RedrawDiagrams(); } }, 200, 3500, 50, 0, 'm');
            Range(Sec, { Label: 'Ceiling limit', Read: () => Glob.CeilingHeightM, Write: V => { Glob.CeilingHeightM = V; RedrawDiagrams(); } }, 800, 6000, 100, 0, 'm');
            Range(Sec, { Label: 'Cb anvil overhang', Read: () => Glob.AnvilAmount, Write: V => { Glob.AnvilAmount = V; RedrawDiagrams(); } }, 0, 1, 0.02, 2, '');
        }
        else
        {
            const Sec = C.SectionCard(Page, { Title: 'Local volume world bounds', Glyph: 'Boxes', Badge: 'CAGE LIVE' }).Inner;
            const Box = C.Construct('canvas', 'diagram-canvas');
            Box.height = 114;
            Sec.append(Box);
            PendingRedraws.push(() => DrawLocalCloudBoundsCanvas(Box, Loc, '#3ec46d'));
            Range(Sec, { Label: 'Centre X', Read: () => Loc.CentreX, Write: V => { Loc.CentreX = V; RedrawDiagrams(); } }, -4, 4, 0.1, 2, 'm');
            Range(Sec, { Label: 'Centre Y (Z-Up)', Read: () => Loc.CentreY, Write: V => { Loc.CentreY = V; RedrawDiagrams(); } }, 0.4, 5, 0.1, 2, 'm');
            Range(Sec, { Label: 'Centre Z', Read: () => Loc.CentreZ, Write: V => { Loc.CentreZ = V; RedrawDiagrams(); } }, -4, 4, 0.1, 2, 'm');
            Range(Sec, { Label: 'Half-extent X', Read: () => Loc.HalfExtentX, Write: V => { Loc.HalfExtentX = V; RedrawDiagrams(); } }, 0.2, 4, 0.1, 2, 'm');
            Range(Sec, { Label: 'Half-extent Y', Read: () => Loc.HalfExtentY, Write: V => { Loc.HalfExtentY = V; RedrawDiagrams(); } }, 0.2, 2.5, 0.05, 2, 'm');
            Range(Sec, { Label: 'Half-extent Z', Read: () => Loc.HalfExtentZ, Write: V => { Loc.HalfExtentZ = V; RedrawDiagrams(); } }, 0.2, 4, 0.1, 2, 'm');
        }
        return;
    }

    const Sec = C.SectionCard(Page, { Title: 'Wind vector & GPU cloud shadows', Glyph: 'Wind', Badge: 'WEATHER' }).Inner;
    Range(Sec, { Label: 'Wind X', Read: () => Cur.WindXMps, Write: V => { Cur.WindXMps = V; } }, -20, 20, 0.5, 1, 'm/s');
    Range(Sec, { Label: 'Wind Y', Read: () => Cur.WindYMps, Write: V => { Cur.WindYMps = V; } }, -20, 20, 0.5, 1, 'm/s');
    Range(Sec, { Label: 'Ground shadow opacity', Read: () => Glob.ShadowStrength, Write: V => { Glob.ShadowStrength = V; } }, 0, 1, 0.02, 2, '');
}

function ConstructInspectorPage()
{
    if (PageScope) C.ReleaseScope(PageScope);
    PageScope = C.OpenScope();
    PendingRedraws.length = 0;

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

    if (Studio.SelectedId === 'celestial-sun')
    {
        Crumb.textContent = 'Inspector / Environment / Sun';
        NameInput.value = 'Directional Sun'; NameInput.disabled = true;
        TypeLabel.textContent = `15 NATIVE PROPERTIES · ${Studio.SolarSolved.PhaseLabel.toUpperCase()}`;
        SymEl.textContent = ''; SymEl.append(C.ConstructGlyph('Sun')); SymEl.style.color = '#e8c27a';
        EnabledEl.checked = true;
        ConstructSunInspector(Page);
    }
    else if (Studio.SelectedId === 'celestial-atmosphere')
    {
        Crumb.textContent = 'Inspector / Environment / Atmosphere';
        NameInput.value = 'Sky Atmosphere'; NameInput.disabled = true;
        TypeLabel.textContent = '18 NATIVE FIELDS · BRUNETON & HILLAIRE';
        SymEl.textContent = ''; SymEl.append(C.ConstructGlyph('Aperture')); SymEl.style.color = '#9fc7e8';
        EnabledEl.checked = Studio.AtmosParams.BakedFetchEnabled;
        EnabledEl.onchange = () => { Studio.AtmosParams.BakedFetchEnabled = EnabledEl.checked; ConstructOutliner(); ConstructInspectorPage(); };
        ConstructAtmosphereInspector(Page);
    }
    else if (Studio.SelectedId === 'celestial-clouds-global' || Studio.SelectedId === 'celestial-clouds-local')
    {
        const IsLocal = Studio.CloudsParams.ActiveScope === 'local';
        Crumb.textContent = 'Inspector / Environment / Clouds';
        NameInput.value = IsLocal ? 'Local Cloud Volume' : 'Global Cloud Slab'; NameInput.disabled = true;
        TypeLabel.textContent = IsLocal ? 'LOCAL VOLUMETRIC BOX · EMERALD CAGE' : 'GLOBAL VOLUMETRIC SLAB · WORLD Z';
        SymEl.textContent = ''; SymEl.append(C.ConstructGlyph('Cloud')); SymEl.style.color = '#8fd3c4';
        const Cur = IsLocal ? Studio.CloudsParams.Local : Studio.CloudsParams.Global;
        EnabledEl.checked = Cur.Enabled;
        EnabledEl.onchange = () => { Cur.Enabled = EnabledEl.checked; ConstructOutliner(); };
        ConstructCloudsInspector(Page);
    }
    else
    {
        const Cst = SelectedConstruct();
        const Mat = MaterialCatalogue[Cst.Spec.Material] || MaterialCatalogue.concrete;
        Crumb.textContent = `Inspector / Fracture / ${Cst.SolidCategory}`;
        NameInput.value = Cst.Title; NameInput.disabled = false;
        NameInput.onchange = () => { Cst.Title = NameInput.value.trim() || Cst.Title; ConstructDockTabs(); ConstructOutliner(); };
        TypeLabel.textContent = `${Mat.ShortLabel.toUpperCase()} · ${Cst.Spec.Solver.toUpperCase()}`;
        SymEl.textContent = ''; SymEl.append(C.ConstructGlyph('Fracture')); SymEl.style.color = Mat.ExteriorHex;
        EnabledEl.checked = Cst.BakedResident;
        EnabledEl.onchange = () => { Cst.BakedResident = EnabledEl.checked; if (EnabledEl.checked) Cst.BakedSignature = ComputeRecipeSignature(Cst); ConstructOutliner(); };
        VpObj.textContent = Cst.Title;
        VpSub.textContent = `${Mat.Label} · ${Cst.Spec.Solver}`;
        ConstructFractureInspector(Page, Cst);
    }

    C.CloseScope(PageScope);
    requestAnimationFrame(RedrawDiagrams);
    document.getElementById('InspectorScroll').scrollTop = 0;
}

function EncodeFractureToml(Cst)
{
    const Spec = Cst.Spec, Rec = Studio.Partition.Receipt;
    return [
        '# Frontier Fracture Studio Archive',
        '[Construct]',
        `Identifier = "${Cst.Id}"`,
        `Label = "${Cst.Title}"`,
        `SolidCategory = "${Cst.SolidCategory}"`,
        `Pipeline = "${Cst.Pipeline}"`,
        '',
        '[FractureRecipe]',
        `Solver = "${Spec.Solver}"`,
        `Material = "${Spec.Material}"`,
        `EnergyJoules = ${Spec.Energy}`,
        `DeterministicSeed = ${Spec.Seed}`,
        `ShardCeiling = ${Spec.Ceiling}`,
        `MinimumSpanMetres = ${Spec.MinimumSize}`,
        `GrainAngleDegrees = ${Spec.GrainAngleDeg}`,
        `BondStrengthScale = ${Spec.BondStrengthScale}`,
        `ImpactPoint = [${Spec.ImpactPoint.join(', ')}]`,
        `ImpactNormal = [${Spec.ImpactNormal.join(', ')}]`,
        '',
        '[PieceSdf]',
        `Enabled = ${Spec.PieceSdf}`,
        `VoxelResolution = ${Spec.SdfResolution}`,
        '',
        '[GeometryReceipt]',
        `FragmentCount = ${Rec.FragmentCount}`,
        `TotalCrackAreaM2 = ${Rec.TotalCrackArea.toFixed(4)}`,
        `VolumeDriftRatio = ${Rec.VolumeDriftRatio.toExponential(4)}`,
        `WatertightManifold = ${Rec.WatertightManifold}`
    ].join('\n');
}

function SyncTopNav()
{
    const MatrixOpen = document.getElementById('SolverVeil').classList.contains('shown');
    document.getElementById('nav-workspace').classList.toggle('active', !MatrixOpen && !Studio.LogOpen);
    document.getElementById('nav-solvers').classList.toggle('active', MatrixOpen);
    document.getElementById('nav-log').classList.toggle('active', !MatrixOpen && Studio.LogOpen);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    WIRING & BOOT
//------------------------------------------------------------------------------------------------------------------------

function WireStaticControls()
{
    const SetG = (Id, G) => { const El = document.getElementById(Id); if (El) { El.textContent = ''; El.append(C.ConstructGlyph(G)); } };
    SetG('help-button', 'Help');
    SetG('compact-outliner', 'Compact');
    SetG('add-button', 'Plus');
    SetG('stat-shards-icon', 'Check');
    SetG('stat-baked-icon', 'Archive');
    SetG('search-icon-slot', 'Search');
    SetG('preset-search-icon-slot', 'Search');
    SetG('viewport-title-icon', 'Fracture');
    SetG('toggle-log-btn', 'Terminal');
    SetG('focus-btn', 'Focus');
    SetG('maximize-btn', 'Maximize');
    SetG('reset-button', 'Reset');
    SetG('play-button', 'Play');
    SetG('step-button', 'Step');
    SetG('close-export', 'Close');
    SetG('close-help', 'Close');

    const VToolMap = { strike: 'Zap', orbit: 'Orbit', gizmo: 'Crosshair' };
    document.querySelectorAll('#ViewportTools [data-tool]').forEach(B =>
    {
        B.append(C.ConstructGlyph(VToolMap[B.dataset.tool]));
        B.addEventListener('click', () =>
        {
            Studio.Viewport.ActiveTool = B.dataset.tool;
            document.querySelectorAll('#ViewportTools [data-tool]').forEach(X => X.classList.toggle('active', X === B));
        });
    });
    const ReasmBtn = document.getElementById('reassemble-quick-btn');
    ReasmBtn.append(C.ConstructGlyph('Reset'));
    ReasmBtn.addEventListener('click', () =>
    {
        Studio.Timeline.Playing = false; Studio.Timeline.ActiveTime = 0; Studio.Timeline.ExplodeAmount = 0;
        document.getElementById('explodeRangeInput').value = '0';
        document.getElementById('explodeValueText').textContent = '0.00';
        Studio.Viewport.ResetCamera();
        SyncTimelineFrame(); UpdatePlayIcon();
    });

    document.getElementById('play-button').addEventListener('click', () =>
    {
        if (Studio.Timeline.ActiveTime >= Studio.Timeline.DurationSeconds - 0.03) Studio.Timeline.ActiveTime = 0;
        Studio.Timeline.Playing = !Studio.Timeline.Playing;
        UpdatePlayIcon(); SyncTimelineFrame();
    });
    document.getElementById('reset-button').addEventListener('click', () => ReasmBtn.click());
    document.getElementById('step-button').addEventListener('click', () =>
    {
        Studio.Timeline.Playing = false; UpdatePlayIcon();
        Studio.Timeline.ActiveTime = Math.min(Studio.Timeline.DurationSeconds, Studio.Timeline.ActiveTime + 0.016 * Studio.Timeline.PlaybackRate);
        SyncTimelineFrame();
    });
    document.getElementById('strike-button').addEventListener('click', TriggerStrike);
    document.getElementById('reticleStrikeBtn').addEventListener('click', TriggerStrike);
    document.getElementById('bake-button').addEventListener('click', () =>
    {
        const Cst = ActiveConstruct();
        Cst.BakedResident = true; Cst.BakedSignature = ComputeRecipeSignature(Cst); Cst.Pipeline = 'baked';
        ConstructOutliner(); ConstructInspectorPage(); SyncViewportSelectors();
        C.Toast(`Baked ${Cst.Title}`);
    });
    document.getElementById('speed-select').addEventListener('change', Ev => { Studio.Timeline.PlaybackRate = Number(Ev.target.value); });
    document.getElementById('explodeRangeInput').addEventListener('input', Ev =>
    {
        Studio.Timeline.ExplodeAmount = Number(Ev.target.value);
        document.getElementById('explodeValueText').textContent = Studio.Timeline.ExplodeAmount.toFixed(2);
        SyncTimelineFrame();
    });

    // Ruler scrub
    let Scrub = false;
    const Track = document.getElementById('timelineScrubberTrack');
    const MoveHead = Ev =>
    {
        const R = Track.getBoundingClientRect();
        Studio.Timeline.Playing = false; UpdatePlayIcon();
        Studio.Timeline.ActiveTime = Clamp((Ev.clientX - R.left) / Math.max(1, R.width), 0, 1) * Studio.Timeline.DurationSeconds;
        SyncTimelineFrame();
    };
    Track.addEventListener('pointerdown', Ev => { Scrub = true; MoveHead(Ev); });
    window.addEventListener('pointermove', Ev => { if (Scrub) MoveHead(Ev); });
    window.addEventListener('pointerup', () => { Scrub = false; });

    document.getElementById('camera-view-select').addEventListener('change', Ev => Studio.Viewport.SetCameraView(Ev.target.value));
    document.getElementById('pipeline-select').addEventListener('change', Ev =>
    {
        const Cst = ActiveConstruct();
        Cst.Pipeline = Ev.target.value;
        Cst.Spec.Solver = Cst.Pipeline === 'realtime' ? 'runtime_boussinesq' : Cst.Pipeline === 'baked' ? 'engineering_fem' : Cst.Spec.Solver;
        RecomputePartition(Cst.Pipeline === 'baked', `Pipeline: ${Cst.Pipeline}`);
        ConstructOutliner(); ConstructInspectorPage();
    });
    document.getElementById('lens-select').addEventListener('change', Ev => Studio.Viewport.SetShadingLens(Ev.target.value));
    document.getElementById('toggle-matrix-btn').addEventListener('click', OpenSolverMatrix);
    document.getElementById('open-solvers-btn').addEventListener('click', OpenSolverMatrix);
    document.getElementById('nav-solvers').addEventListener('click', OpenSolverMatrix);
    document.getElementById('focus-btn').addEventListener('click', () => Studio.Viewport.ResetCamera());
    document.getElementById('maximize-btn').addEventListener('click', Ev =>
    {
        const Max = document.getElementById('studio-workspace').classList.toggle('maximized');
        Ev.currentTarget.classList.toggle('active', Max);
        setTimeout(() => Studio.Viewport.Resize(), 20);
    });
    document.getElementById('toggle-log-btn').addEventListener('click', () =>
    {
        Studio.LogOpen = !Studio.LogOpen;
        document.getElementById('log-drawer').hidden = !Studio.LogOpen;
        document.getElementById('toggle-log-btn').classList.toggle('active', Studio.LogOpen);
        SyncTopNav(); setTimeout(() => Studio.Viewport.Resize(), 20);
    });
    document.getElementById('nav-log').addEventListener('click', () => document.getElementById('toggle-log-btn').click());
    document.getElementById('nav-workspace').addEventListener('click', () =>
    {
        document.getElementById('SolverVeil').classList.remove('shown');
        Studio.LogOpen = false;
        document.getElementById('log-drawer').hidden = true;
        document.getElementById('toggle-log-btn').classList.remove('active');
        SyncTopNav(); setTimeout(() => Studio.Viewport.Resize(), 20);
    });

    // Outliner & Presets
    document.getElementById('compact-outliner').addEventListener('click', Ev =>
    {
        const Compact = document.getElementById('left-panel').classList.toggle('compact');
        Ev.currentTarget.setAttribute('aria-pressed', String(Compact));
        Ev.currentTarget.classList.toggle('active', Compact);
    });
    document.getElementById('scene-search').addEventListener('input', Ev => { Studio.OutlinerQuery = Ev.target.value.trim().toLowerCase(); ConstructOutliner(); });
    document.getElementById('quick-preset-search').addEventListener('input', Ev => { Studio.PresetQuery = Ev.target.value.trim().toLowerCase(); ConstructQuickLibrary(); });
    document.querySelectorAll('#scene-filters button').forEach(Btn =>
        Btn.addEventListener('click', () =>
        {
            Studio.OutlinerFilter = Btn.dataset.filter;
            document.querySelectorAll('#scene-filters button').forEach(B => B.classList.toggle('active', B === Btn));
            ConstructOutliner();
        }));

    // Add Popover & Dock '+' button
    const AddBtn = document.getElementById('add-button'), AddMenu = document.getElementById('add-menu');
    AddBtn.addEventListener('click', () =>
    {
        const Open = AddMenu.hidden;
        if (Open) { const R = AddBtn.getBoundingClientRect(); AddMenu.style.left = `${R.left}px`; AddMenu.style.top = `${R.bottom + 6}px`; }
        AddMenu.hidden = !Open; AddBtn.setAttribute('aria-expanded', String(Open));
    });
    AddMenu.querySelectorAll('[data-spawn-solid]').forEach(B => B.addEventListener('click', () =>
    {
        AddMenu.hidden = true;
        const [Cat, Mat, Title] = B.dataset.spawnSolid.split('|');
        SpawnConstructPreset(Cat, Mat, Title);
    }));
    AddMenu.querySelector('[data-action="open-solvers"]').addEventListener('click', () => { AddMenu.hidden = true; OpenSolverMatrix(); });
    window.addEventListener('pointerdown', Ev => { if (!AddMenu.hidden && !AddMenu.contains(Ev.target) && !AddBtn.contains(Ev.target)) AddMenu.hidden = true; });

    document.getElementById('spawn-tab-btn').addEventListener('click', () => SpawnConstructPreset('cube', 'concrete', 'Voronoi Calibration Cube', 'voronoi'));
    document.getElementById('reset-selection-btn').addEventListener('click', () => { RecomputePartition(false, 'Rebuilt current solid'); ConstructInspectorPage(); });

    const ExportDlg = document.getElementById('export-dialog');
    document.getElementById('export-button').addEventListener('click', () => { document.getElementById('export-preview').textContent = EncodeFractureToml(ActiveConstruct()); ExportDlg.showModal(); });
    document.getElementById('close-export').addEventListener('click', () => ExportDlg.close());
    document.getElementById('copy-export-btn').addEventListener('click', () => navigator.clipboard?.writeText(document.getElementById('export-preview').textContent).then(() => C.Toast('Copied .fracture.toml')));
    document.getElementById('download-export-btn').addEventListener('click', () =>
    {
        const BlobOut = new Blob([document.getElementById('export-preview').textContent], { type: 'text/plain' });
        const Url = URL.createObjectURL(BlobOut); const A = document.createElement('a'); A.href = Url; A.download = `${ActiveConstruct().Id}.fracture.toml`; A.click(); URL.revokeObjectURL(Url);
    });

    const HelpDlg = document.getElementById('help-dialog');
    document.getElementById('help-button').addEventListener('click', () => HelpDlg.showModal());
    document.getElementById('close-help').addEventListener('click', () => HelpDlg.close());
}

function Boot()
{
    WireStaticControls();

    Studio.Viewport = new ViewportRasterizationHost(document.getElementById('viewportCanvasMount'), {
        OnViewportStrike: (Pt, Norm, AutoPlay) =>
        {
            const Cst = ActiveConstruct();
            Cst.Spec.ImpactPoint = Pt.map(V => Number(V.toFixed(3)));
            Cst.Spec.ImpactNormal = Norm.map(V => Number(V.toFixed(3)));
            RecomputePartition(AutoPlay, `Viewport strike at (${Pt.map(V => V.toFixed(2)).join(', ')})`);
            ConstructOutliner();
            ConstructInspectorPage();
        },
        OnHoverFragment: Idx => UpdateHoverBadge(Idx),
        OnSelectFragment: Idx => { ConstructOutliner(); UpdateHoverBadge(Idx); }
    });
    Studio.Viewport.SetAccentColor('#e8c27a');
    Studio.Viewport.Resize();

    if (!Studio.Viewport.WebGlReady)
    {
        document.getElementById('engine-pill').classList.add('software');
        document.getElementById('engine-pill-text').textContent = 'PREVIEW · SOFTWARE 3D';
        document.getElementById('status-adapter').textContent = 'Software 3D fallback active';
    }

    RecomputePartition(false);
    Studio.Timeline.ActiveTime = 0.28;
    Studio.Timeline.ExplodeAmount = 0.08;
    document.getElementById('explodeRangeInput').value = '0.08';
    document.getElementById('explodeValueText').textContent = '0.08';
    SyncTimelineFrame();

    ConstructDockTabs();
    ConstructOutliner();
    ConstructQuickLibrary();
    ConstructInspectorPage();
    SyncViewportSelectors();

    C.SmoothScroll(document.getElementById('OutlinerScroll'), document.getElementById('OutlinerContent'));
    C.SmoothScroll(document.getElementById('InspectorScroll'), document.getElementById('InspectorContent'));

    window.addEventListener('resize', () => { Studio.Viewport.Resize(); RedrawDiagrams(); });
    new ResizeObserver(() => Studio.Viewport.Resize()).observe(document.getElementById('ViewportSurface'));

    window.addEventListener('keydown', Ev =>
    {
        if (Ev.target.matches('input, select, textarea')) return;
        if ((Ev.ctrlKey || Ev.metaKey) && Ev.key.toLowerCase() === 'f') { Ev.preventDefault(); document.getElementById('scene-search').focus(); }
        else if (Ev.code === 'Space') { Ev.preventDefault(); document.getElementById('play-button').click(); }
        else if (Ev.key === 'l' || Ev.key === 'L') TriggerStrike();
        else if (Ev.key === 'b' || Ev.key === 'B') document.getElementById('bake-button').click();
        else if (Ev.key === 'r' || Ev.key === 'R') document.getElementById('reset-button').click();
        else if (Ev.key === 's' || Ev.key === 'S') document.getElementById('step-button').click();
        else if (Ev.key === 'm' || Ev.key === 'M') OpenSolverMatrix();
        else if (Ev.key === '/') { Ev.preventDefault(); location.href = '../AssetBrowser/?ws=Fracture%20Lab'; }
        else if (Ev.key === 'Escape') { document.getElementById('SolverVeil').classList.remove('shown'); SyncTopNav(); }
    });

    HandleAssetDeepLink();
    AppendLog('FRACTURE', 'Fracture Studio initialized', `${Studio.Constructs.length} solids · 6 solvers active`, 'live');
    AppendLog('SDF', 'Engineering Navier–Cauchy + Fast Boussinesq Flat-BSP ready', null, 'live');
    C.Broadcast();

    let Prev = performance.now();
    const Frame = Now =>
    {
        requestAnimationFrame(Frame);
        const Dt = Math.min(0.05, Math.max(0.001, (Now - Prev) / 1000));
        Prev = Now;

        if (Studio.Timeline.Playing)
        {
            Studio.Timeline.ActiveTime += Dt * Studio.Timeline.PlaybackRate;
            if (Studio.Timeline.ActiveTime >= Studio.Timeline.DurationSeconds)
            {
                Studio.Timeline.ActiveTime = Studio.Timeline.DurationSeconds;
                Studio.Timeline.Playing = false;
                UpdatePlayIcon();
            }
            SyncTimelineFrame();
        }

        if (Studio.SunParams.AnimateCycle)
        {
            Studio.SunParams.LocalHours = (Studio.SunParams.LocalHours + Dt * 0.65 * Studio.SunParams.CycleSpeed) % 24;
            Studio.SolarSolved = EvaluateSolarEphemeris(Studio.SunParams);
            if (Studio.SelectedId === 'celestial-sun') RedrawDiagrams();
        }

        Studio.Viewport.UpdateEnvironmentUniforms(Studio.SunParams, Studio.SolarSolved, Studio.AtmosParams, Studio.CloudsParams, Dt);
        Studio.Viewport.RenderFrame();

        const Proj = Studio.Viewport.ProjectImpactScreenCoordinates();
        const Reticle = document.getElementById('impactReticleHud');
        const ShowRet = Proj.visible && Studio.Timeline.ActiveTime < 0.48 && Studio.Timeline.ExplodeAmount < 0.35;
        Reticle.style.opacity = ShowRet ? '1' : '0';
        Reticle.style.left = `${Proj.x.toFixed(1)}px`;
        Reticle.style.top = `${Proj.y.toFixed(1)}px`;
    };
    window.FractureStudio = Studio;
    requestAnimationFrame(Frame);
}

if (document.readyState === 'loading') window.addEventListener('DOMContentLoaded', Boot);
else Boot();
