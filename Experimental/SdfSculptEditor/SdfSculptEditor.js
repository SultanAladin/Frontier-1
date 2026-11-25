/*==========================================================================================================================================
                                                         SDFSCULPTEDITOR.JS
==========================================================================================================================================*/
// Main Studio Controller for Frontier Experimental Remastered — SDF Sculpt Studio.
// Orchestrates the Hybrid 3D RGBA16F Volume + Analytic CSG Raymarching Stage, 10 Volumetric Sculpting Brushes,
// 3D CSG Transform Gizmo, Searchable Outliner, Kitbash Library, 4-Tab Sun/Fluid Inspector, and SDF1/OBJ/STL Export.

import {
    Clamp,
    Vec3Sub,
    Vec3Add,
    Vec3Scale,
    Vec3Dot,
    HexToLinearRgb,
    LinearRgbToHex,
    SHAPE_TYPES,
    SHAPE_NAMES,
    BOOL_OPS,
    BOOL_OP_NAMES,
    FALLOFF_PRESETS
} from './Engine/SdfMath.js';
import { SdfVolume } from './Engine/SdfVolume.js';
import { SdfRaymarchViewport } from './Engine/SdfRaymarchViewport.js';
import {
    SCULPT_BRUSHES,
    MATERIAL_PRESETS,
    SHADING_LENSES,
    KITBASH_LIBRARY,
    MakeCsgNode,
    BuildStarterScene
} from './Engine/SdfPresets.js';
import {
    ExtractSurfaceNetsMesh,
    EncodeSdfSpaceBinary,
    DecodeSdfSpaceBinary,
    ExportMeshObj,
    ExportMeshBinaryStl,
    SerializeSceneToml
} from './Engine/SdfMesherAndCodec.js';
import {
    Construct,
    ConstructGlyph,
    OpenScope,
    CloseScope,
    ReleaseScope,
    Broadcast,
    SectionCard,
    SliderRow,
    SwitchRow,
    SelectRow,
    SegmentedRow,
    VectorRow,
    ColourRow,
    ToggleTile,
    ActionRow,
    MetricTrio,
    FalloffCurveEditor,
    DistanceHistogram,
    PolarDial,
    SmoothScroll,
    Toast
} from './StudioControls.js';

const STARTER_TABS = [
    { key: 'sculpt-bust', label: 'Sculpt Bust · Clay' },
    { key: 'mech-pod', label: 'Hard-Surface Pod' },
    { key: 'gyroid-relic', label: 'Gyroid Lattice' },
    { key: 'rock-monolith', label: 'Rock Monolith' }
];

const SHAPE_GLYPHS = {
    0: 'Planet',
    1: 'Boxes',
    2: 'Activity',
    3: 'Current',
    4: 'Orbit',
    5: 'Zap',
    6: 'Sparkles',
    7: 'Lattice',
    8: 'Planet',
    9: 'Magnet',
    10: 'Leaf',
    11: 'Atom'
};

const App = {
    volume: new SdfVolume(96),
    viewport: null,
    activeSceneKey: 'sculpt-bust',
    customSceneCount: 0,
    scenesMeta: [...STARTER_TABS],

    // Scene state
    name: 'Sculpt Bust · Clay',
    volumeName: 'Clay Volume',
    volumeVisible: true,
    volumeLocked: false,
    volumeRes: 96,
    csgNodes: [],

    // Selection: 'volume' or a csgNode.id
    selectedId: 'volume',
    inspectorTab: 'brush',
    inspectorScope: null,

    // Active tool: one of SCULPT_BRUSHES[].id or 'transform'
    activeTool: 'clay',
    brush: {
        radius: 0.24,
        strength: 0.68,
        smoothness: 0.34,
        spacing: 0.20,
        lazyRadius: 0.0,
        adaptivePlane: false,
        paintWithSculpt: true,
        brushColor: HexToLinearRgb('#bc795c'),
        stampShape: 1,
        stampOp: 0,
        falloffCurve: {
            Mode: 'smooth',
            Custom: false,
            Interpolation: 'hermite',
            Points: FALLOFF_PRESETS.smooth.Points.map(P => P.slice())
        }
    },

    symmetry: { x: true, y: false, z: false, radial: 1 },

    material: {
        preset: 'terracotta',
        roughness: 0.62,
        metallic: 0.03,
        subsurface: 0.38,
        cavity: 0.55
    },

    stage: {
        lens: 'pbr',
        sunAzimuth: 38,
        sunElevation: 46,
        showGrid: true,
        showGizmo: true,
        showBoundsCage: false,
        showMinimap: true,
        groundY: -1.14,
        sliceEnabled: false,
        sliceAxis: 2,
        sliceOffset: 0.0
    },

    meshPreview: {
        resolution: 64,
        isoLevel: 0.0,
        lastStats: null
    },

    // UI filters & runtime telemetry
    outlinerFilter: 'all',
    outlinerQuery: '',
    libraryCategory: 'All',
    libraryQuery: '',
    logs: [],
    telemetry: null,
    lastStrokeMs: 0.0,
    fps: 60,
    strokeCount: 0,
    brushHover: { active: false, pos: [0, 0, 0], normal: [0, 1, 0], radius: 0.24 },
    gesture: null,
    dirtySliceMinimap: true
};

/*==========================================================================================================================================
                                                          LOGGING & TELEMETRY
==========================================================================================================================================*/
function PushLog(Channel, Message, Tone = '')
{
    const Now = new Date();
    const Time = Now.toTimeString().slice(0, 8);
    App.logs.unshift({ Time, Channel, Message, Tone });
    if (App.logs.length > 80) App.logs.length = 80;

    const CountEl = document.getElementById('nav-log-count');
    if (CountEl) CountEl.textContent = String(App.logs.length);
    RenderLogStream();
}

function RenderLogStream()
{
    const Host = document.getElementById('LogContent');
    if (!Host) return;
    Host.textContent = '';
    for (const Entry of App.logs.slice(0, 40))
    {
        Host.append(Construct('div', 'log-row', [
            Construct('span', 'log-time tabular', Entry.Time),
            Construct('span', 'log-chan' + (Entry.Tone ? ' ' + Entry.Tone : ''), Entry.Channel),
            Construct('span', null, Entry.Message)
        ]));
    }
}

function RefreshTelemetry(RecomputeHistogram = true)
{
    if (RecomputeHistogram || !App.telemetry)
    {
        App.telemetry = App.volume.computeTelemetry();
    }
    App.dirtySliceMinimap = true;
    RenderOutliner();
    RenderHistoryStrip();
    SyncQuickDock();
    SyncStatusBar();
    Broadcast();
}

/*==========================================================================================================================================
                                                        SCENE INITIALIZATION
==========================================================================================================================================*/
function LoadStarterScene(PresetKey)
{
    const Built = BuildStarterScene(PresetKey, App.volume);
    App.activeSceneKey = PresetKey;
    App.name = Built.name;
    App.volumeName = 'Clay Volume';
    App.volumeVisible = true;
    App.volumeLocked = false;
    App.volumeRes = App.volume.nx;
    App.csgNodes = Built.csgNodes;
    App.symmetry = { ...Built.symmetry };
    App.stage.lens = Built.lens || 'pbr';
    ApplyMaterialPreset(Built.materialPreset || 'terracotta', false);

    App.selectedId = 'volume';
    App.volume.history = [];
    App.volume.historyIndex = -1;
    App.volume.pushHistorySnapshot('Initial ' + Built.name, App.csgNodes);
    App.meshPreview.lastStats = null;

    SyncSceneTabs();
    SyncSymmetryBar();
    document.getElementById('lens-select').value = App.stage.lens;
    document.getElementById('dock-res-select').value = String(App.volume.nx);

    RefreshTelemetry(true);
    RebuildInspector();
    PushLog('SCENE', `Loaded "${Built.name}" (${App.volume.nx}³ RGBA16F + ${App.csgNodes.length} CSG nodes)`, 'live');
}

function CreateBlankClayScene()
{
    App.customSceneCount++;
    const Key = `blank-${App.customSceneCount}`;
    const Title = `Clay Study ${App.customSceneCount}`;
    App.scenesMeta.push({ key: Key, label: Title, blank: true });

    const Col = HexToLinearRgb('#bc795c');
    App.activeSceneKey = Key;
    App.name = Title;
    App.volume.seedClayPrimitive(0, [0.72, 0.72, 0.72], Col, 0);
    App.csgNodes = [];
    App.selectedId = 'volume';
    App.symmetry = { x: true, y: false, z: false, radial: 1 };
    ApplyMaterialPreset('terracotta', false);

    App.volume.history = [];
    App.volume.historyIndex = -1;
    App.volume.pushHistorySnapshot('Fresh Clay Sphere', App.csgNodes);

    SyncSceneTabs();
    SyncSymmetryBar();
    RefreshTelemetry(true);
    RebuildInspector();
    Toast(`Created "${Title}" with 0.72 m clay sphere`);
    PushLog('SCENE', `Initialized blank clay sphere "${Title}"`, 'warm');
}

function ApplyMaterialPreset(PresetKey, Notify = true)
{
    const Spec = MATERIAL_PRESETS[PresetKey] || MATERIAL_PRESETS.terracotta;
    App.material.preset = PresetKey;
    App.material.roughness = Spec.roughness;
    App.material.metallic = Spec.metallic;
    App.material.subsurface = Spec.subsurface;
    App.material.cavity = Spec.cavity;
    App.brush.brushColor = HexToLinearRgb(Spec.albedoHex);
    if (Notify)
    {
        Broadcast();
        Toast(`Material: ${Spec.label}`);
    }
}

/*==========================================================================================================================================
                                                    HEADER, TABS & HISTORY STRIP
==========================================================================================================================================*/
function SyncSceneTabs()
{
    const Bar = document.getElementById('scene-tabs');
    Bar.textContent = '';
    for (const Item of App.scenesMeta)
    {
        const Btn = Construct('button', 'document-tab' + (Item.key === App.activeSceneKey ? ' active' : ''), [
            Construct('span', 'document-label', Item.label)
        ]);
        Btn.type = 'button';
        Btn.setAttribute('role', 'tab');
        Btn.setAttribute('aria-selected', String(Item.key === App.activeSceneKey));
        Btn.addEventListener('click', () =>
        {
            if (Item.key === App.activeSceneKey) return;
            if (Item.blank)
            {
                App.activeSceneKey = Item.key;
                App.name = Item.label;
                App.volume.seedClayPrimitive(0, [0.72, 0.72, 0.72], HexToLinearRgb('#bc795c'), 0);
                App.csgNodes = [];
                App.volume.history = [];
                App.volume.historyIndex = -1;
                App.volume.pushHistorySnapshot(Item.label, App.csgNodes);
                SyncSceneTabs();
                RefreshTelemetry(true);
                RebuildInspector();
            }
            else
            {
                LoadStarterScene(Item.key);
            }
        });
        Bar.append(Btn);
    }
}

function RecordHistory(Label)
{
    App.volume.pushHistorySnapshot(Label, App.csgNodes);
    RenderHistoryStrip();
    SyncUndoRedoButtons();
}

function JumpToHistory(Index)
{
    const Snap = App.volume.restoreHistoryAt(Index);
    if (!Snap) return;
    if (Snap.csg)
    {
        App.csgNodes = JSON.parse(JSON.stringify(Snap.csg));
        if (App.selectedId !== 'volume' && !App.csgNodes.some(N => N.id === App.selectedId))
        {
            App.selectedId = 'volume';
        }
    }
    App.volumeRes = App.volume.nx;
    document.getElementById('dock-res-select').value = String(App.volume.nx);
    SyncUndoRedoButtons();
    RefreshTelemetry(true);
    RebuildInspector();
    Toast(`Restored: ${Snap.label}`);
}

function PerformUndo()
{
    if (!App.volume.canUndo()) return;
    JumpToHistory(App.volume.historyIndex - 1);
}

function PerformRedo()
{
    if (!App.volume.canRedo()) return;
    JumpToHistory(App.volume.historyIndex + 1);
}

function SyncUndoRedoButtons()
{
    const U = document.getElementById('undo-btn');
    const R = document.getElementById('redo-btn');
    if (U) U.disabled = !App.volume.canUndo();
    if (R) R.disabled = !App.volume.canRedo();
}

function RenderHistoryStrip()
{
    const Strip = document.getElementById('history-strip');
    const Counter = document.getElementById('stroke-counter');
    if (!Strip) return;
    Strip.textContent = '';

    const Hist = App.volume.history;
    const Cur = App.volume.historyIndex;
    if (Counter)
    {
        Counter.textContent = `REV ${String(Cur + 1).padStart(2, '0')} / ${String(Hist.length).padStart(2, '0')}`;
    }

    Hist.forEach((Snap, Idx) =>
    {
        constCls: {
            const Cls = 'history-chip' + (Idx === Cur ? ' current' : Idx > Cur ? ' future' : '');
            const Chip = Construct('button', Cls, `${Idx + 1} · ${Snap.label}`);
            Chip.type = 'button';
            Chip.title = `Jump to revision ${Idx + 1}: ${Snap.label}`;
            Chip.addEventListener('click', () => JumpToHistory(Idx));
            Strip.append(Chip);
            break constCls;
        }
    });
    Strip.scrollLeft = Strip.scrollWidth;
    SyncUndoRedoButtons();
}

/*==========================================================================================================================================
                                                 LEFT PANEL: OUTLINER & LIBRARY
==========================================================================================================================================*/
function SelectEntity(Id, SwitchTab = false)
{
    App.selectedId = Id;
    if (SwitchTab)
    {
        SetInspectorTab('node');
    }
    RenderOutliner();
    SyncInspectorHeader();
    if (App.inspectorTab === 'node') RebuildInspector();
    Broadcast();
}

function GetSelectedCsgNode()
{
    if (App.selectedId === 'volume') return null;
    return App.csgNodes.find(N => N.id === App.selectedId) || null;
}

function RenderOutliner()
{
    const Host = document.getElementById('OutlinerContent');
    if (!Host) return;
    Host.textContent = '';

    const Query = App.outlinerQuery.trim().toLowerCase();
    const Filter = App.outlinerFilter;

    let ActiveCount = App.volumeVisible ? 1 : 0;
    let CutterOrOffCount = App.volumeVisible ? 0 : 1;
    for (const N of App.csgNodes)
    {
        if (N.enabled === false || N.op === 1) CutterOrOffCount++;
        if (N.enabled !== false && N.op !== 1) ActiveCount++;
    }
    document.getElementById('active-sdf-count').textContent = String(ActiveCount);
    document.getElementById('cutter-sdf-count').textContent = String(CutterOrOffCount);
    document.getElementById('outliner-subtitle').textContent =
        `Hybrid SDF · ${App.volume.nx}³ clay + ${App.csgNodes.length} CSG`;

    // Group 1: Volumetric Sculpt Layer
    const ShowClay = (Filter === 'all' || Filter === 'clay') &&
        (!Query || App.volumeName.toLowerCase().includes(Query) || 'clay volume voxel'.includes(Query));

    if (ShowClay)
    {
        const VolGroup = Construct('div', 'tree-group');
        VolGroup.append(Construct('div', 'tree-group-title', [
            ConstructGlyph('Layers'),
            Construct('span', null, 'Volumetric Clay Grid'),
            Construct('span', 'group-count', `${App.volume.nx}³`)
        ]));

        const VolRow = Construct('div', 'tree-row' +
            (App.selectedId === 'volume' ? ' selected' : '') +
            (!App.volumeVisible ? ' is-off' : ''));
        VolRow.style.setProperty('--row-accent', '#e8c27a');

        const SelectBtn = Construct('button', 'row-select', [
            Construct('span', 'row-glyph', ConstructGlyph('Layers')),
            Construct('span', 'row-copy', [
                Construct('div', 'row-name', App.volumeName),
                Construct('div', 'row-meta', `${(App.telemetry?.liters ?? 0).toFixed(1)} L · ${(App.telemetry?.memoryMB ?? 7.08).toFixed(1)} MB RGBA16F`)
            ]),
            Construct('span', 'row-tag clay', 'CLAY')
        ]);
        SelectBtn.type = 'button';
        SelectBtn.addEventListener('click', () => SelectEntity('volume', true));

        const LockBtn = Construct('button', 'row-icon-btn' + (App.volumeLocked ? ' warn pinned' : ''),
            ConstructGlyph(App.volumeLocked ? 'Lock' : 'Unlock'));
        LockBtn.type = 'button';
        LockBtn.title = App.volumeLocked ? 'Clay Volume is locked (click to unlock)' : 'Lock Clay Volume from brush edits';
        LockBtn.setAttribute('aria-label', 'Toggle clay volume lock');
        LockBtn.addEventListener('click', E =>
        {
            E.stopPropagation();
            App.volumeLocked = !App.volumeLocked;
            RenderOutliner();
            Broadcast();
        });

        const EyeBtn = Construct('button', 'row-icon-btn' + (!App.volumeVisible ? ' off pinned' : ''),
            ConstructGlyph(App.volumeVisible ? 'Eye' : 'EyeOff'));
        EyeBtn.type = 'button';
        EyeBtn.title = App.volumeVisible ? 'Hide Clay Volume' : 'Show Clay Volume';
        EyeBtn.setAttribute('aria-label', 'Toggle clay volume visibility');
        EyeBtn.addEventListener('click', E =>
        {
            E.stopPropagation();
            App.volumeVisible = !App.volumeVisible;
            App.dirtySliceMinimap = true;
            RenderOutliner();
            SyncInspectorHeader();
            Broadcast();
        });

        VolRow.append(SelectBtn, LockBtn, EyeBtn);
        VolGroup.append(VolRow);
        Host.append(VolGroup);
    }

    // Group 2: Parametric CSG Stack
    const FilteredNodes = App.csgNodes.filter(Node =>
    {
        if (Filter === 'clay') return false;
        if (Filter === 'union' && Node.op !== 0 && Node.op !== 3) return false;
        if (Filter === 'subtract' && Node.op !== 1) return false;
        if (Filter === 'intersect' && Node.op !== 2) return false;
        if (Query && !Node.name.toLowerCase().includes(Query) && !(SHAPE_NAMES[Node.shape] || '').toLowerCase().includes(Query))
        {
            return false;
        }
        return true;
    });

    const CsgGroup = Construct('div', 'tree-group');
    CsgGroup.append(Construct('div', 'tree-group-title', [
        ConstructGlyph('Boxes'),
        Construct('span', null, 'Parametric CSG Nodes'),
        Construct('span', 'group-count', String(FilteredNodes.length))
    ]));

    if (FilteredNodes.length === 0)
    {
        const Empty = Construct('div', 'property-hint', 'No parametric CSG nodes match filter. Click + or pick a Kitbash card below to spawn one.');
        Empty.style.padding = '6px 10px';
        CsgGroup.append(Empty);
    }

    for (const Node of FilteredNodes)
    {
        const IsCut = Node.op === 1;
        const IsInt = Node.op === 2;
        const Accent = IsCut ? '#ff6b6b' : IsInt ? '#7ec8ff' : '#3ec46d';
        const TagClass = 'row-tag' + (IsCut ? ' cut' : IsInt ? ' int' : '');
        const TagText = IsCut ? 'CUT' : IsInt ? 'INT' : Node.op === 3 ? 'CHAMF' : Node.op === 4 ? 'TINT' : 'UNION';

        const Row = Construct('div', 'tree-row' +
            (App.selectedId === Node.id ? ' selected' : '') +
            (Node.enabled === false ? ' is-off' : ''));
        Row.style.setProperty('--row-accent', Accent);

        const SymNotes = [];
        if (Node.mirrorX) SymNotes.push('MirX');
        if ((Node.radialCount | 0) >= 2) SymNotes.push(`${Node.radialCount}×Rad`);
        const SubMeta = `${SHAPE_NAMES[Node.shape] || 'SDF'} · k ${(Node.blend ?? 0.06).toFixed(2)}m${SymNotes.length ? ' · ' + SymNotes.join(' ') : ''}`;

        const SelectBtn = Construct('button', 'row-select', [
            Construct('span', 'row-glyph', ConstructGlyph(SHAPE_GLYPHS[Node.shape] || 'Boxes')),
            Construct('span', 'row-copy', [
                Construct('div', 'row-name', Node.name),
                Construct('div', 'row-meta', SubMeta)
            ]),
            Construct('span', TagClass, TagText)
        ]);
        SelectBtn.type = 'button';
        SelectBtn.addEventListener('click', () => SelectEntity(Node.id, true));

        const LockBtn = Construct('button', 'row-icon-btn' + (Node.locked ? ' warn pinned' : ''),
            ConstructGlyph(Node.locked ? 'Lock' : 'Unlock'));
        LockBtn.type = 'button';
        LockBtn.title = Node.locked ? 'Node transform locked (click to unlock)' : 'Lock node transform';
        LockBtn.setAttribute('aria-label', `Lock ${Node.name}`);
        LockBtn.addEventListener('click', E =>
        {
            E.stopPropagation();
            Node.locked = !Node.locked;
            RenderOutliner();
            Broadcast();
        });

        const EyeBtn = Construct('button', 'row-icon-btn' + (Node.enabled === false ? ' off pinned' : ''),
            ConstructGlyph(Node.enabled !== false ? 'Eye' : 'EyeOff'));
        EyeBtn.type = 'button';
        EyeBtn.title = Node.enabled !== false ? 'Disable / hide CSG node' : 'Enable CSG node';
        EyeBtn.setAttribute('aria-label', `Toggle ${Node.name} visibility`);
        EyeBtn.addEventListener('click', E =>
        {
            E.stopPropagation();
            Node.enabled = Node.enabled === false;
            App.dirtySliceMinimap = true;
            RenderOutliner();
            SyncInspectorHeader();
            Broadcast();
        });

        Row.append(SelectBtn, LockBtn, EyeBtn);
        CsgGroup.append(Row);
    }

    Host.append(CsgGroup);
}

function SpawnLibraryItem(Item)
{
    if (Item.kind === 'blank')
    {
        RecordHistory('Before ' + Item.name);
        const Col = HexToLinearRgb(Item.blank.colorHex || '#bc795c');
        App.volume.seedClayPrimitive(Item.blank.shape, Item.blank.size, Col, Item.blank.round || 0);
        RecordHistory(Item.name);
        SelectEntity('volume', true);
        RefreshTelemetry(true);
        Toast(`Re-seeded Clay Volume: ${Item.name}`);
        PushLog('CLAY', `Seeded clay volume with "${Item.name}"`, 'warm');
        return;
    }

    if (App.csgNodes.length >= 24)
    {
        Toast('24 active CSG nodes reached — bake CSG to Clay first');
        return;
    }

    const Jitter = (App.csgNodes.length % 5 - 2) * 0.06;
    const Spec = { ...Item.node };
    if (!Spec.position) Spec.position = [Jitter, 0.18, 0.15];
    const Created = MakeCsgNode(Spec);
    App.csgNodes.push(Created);
    RecordHistory(`Add ${Created.name}`);
    SelectEntity(Created.id, true);
    RefreshTelemetry(false);
    Toast(`Added CSG node: ${Created.name}`);
    PushLog('CSG', `Spawned ${Created.name} (${BOOL_OP_NAMES[Created.op]})`, 'live');
}

function RenderKitbashLibrary()
{
    const Categories = ['All', 'Primitives', 'Cutters', 'Kitbash', 'Blanks'];
    const QuickFilters = document.getElementById('quick-preset-filters');
    const QuickList = document.getElementById('quick-presets-list');
    const ModalTabs = document.getElementById('modal-library-tabs');
    const ModalGrid = document.getElementById('modal-library-grid');

    const Query = App.libraryQuery.trim().toLowerCase();
    const Filtered = KITBASH_LIBRARY.filter(Item =>
    {
        if (App.libraryCategory !== 'All' && Item.category !== App.libraryCategory) return false;
        if (Query && !Item.name.toLowerCase().includes(Query) && !Item.blurb.toLowerCase().includes(Query)) return false;
        return true;
    });

    document.getElementById('quick-preset-count').textContent = String(Filtered.length);

    QuickFilters.textContent = '';
    ModalTabs.textContent = '';
    for (const Cat of Categories)
    {
        const Qb = Construct('button', Cat === App.libraryCategory ? 'active' : '', Cat);
        Qb.type = 'button';
        Qb.addEventListener('click', () => { App.libraryCategory = Cat; RenderKitbashLibrary(); });
        QuickFilters.append(Qb);

        const Mb = Construct('button', 'library-tab' + (Cat === App.libraryCategory ? ' active' : ''), Cat);
        Mb.type = 'button';
        Mb.addEventListener('click', () => { App.libraryCategory = Cat; RenderKitbashLibrary(); });
        ModalTabs.append(Mb);
    }

    QuickList.textContent = '';
    ModalGrid.textContent = '';
    for (const Item of Filtered)
    {
        const Swatch = Construct('span', 'preset-swatch', ConstructGlyph(Item.glyph));
        Swatch.style.color = Item.accent;
        const Card = Construct('button', 'preset-card', [
            Swatch,
            Construct('span', 'preset-copy', [
                Construct('strong', null, Item.name),
                Construct('small', null, Item.blurb)
            ]),
            Construct('span', 'preset-badge', Item.badge)
        ]);
        Card.type = 'button';
        Card.addEventListener('click', () => SpawnLibraryItem(Item));
        QuickList.append(Card);

        // Full modal card
        const GlyphBox = Construct('div', 'card-glyph', ConstructGlyph(Item.glyph));
        GlyphBox.style.background = 'rgba(255,255,255,0.05)';
        GlyphBox.style.color = Item.accent;
        const ModalCard = Construct('button', 'library-card', [
            Construct('div', 'card-top', [
                GlyphBox,
                Construct('div', null, [
                    Construct('div', 'card-name', Item.name),
                    Construct('div', 'card-native', `${Item.category.toUpperCase()} · ${Item.badge}`)
                ])
            ]),
            Construct('p', 'card-blurb', Item.blurb)
        ]);
        ModalCard.type = 'button';
        ModalCard.addEventListener('click', () =>
        {
            SpawnLibraryItem(Item);
            CloseLibraryModal();
        });
        ModalGrid.append(ModalCard);
    }
}

/*==========================================================================================================================================
                                            VIEWPORT TOOLS, BRUSHES & GESTURE INPUT
==========================================================================================================================================*/
function RenderViewportToolRail()
{
    const Rail = document.getElementById('ViewportTools');
    Rail.textContent = '';

    for (const B of SCULPT_BRUSHES)
    {
        const Btn = Construct('button', 'tool-rail-btn' + (App.activeTool === B.id ? ' active' : ''), [
            ConstructGlyph(B.glyph),
            Construct('small', null, B.key)
        ]);
        Btn.type = 'button';
        Btn.title = `${B.name} (${B.key}) — ${B.description}`;
        Btn.setAttribute('aria-label', B.name);
        Btn.addEventListener('click', () => SelectTool(B.id));
        Rail.append(Btn);
    }

    Rail.append(Construct('div', 'tool-rail-sep'));

    const GizmoBtn = Construct('button', 'tool-rail-btn' + (App.activeTool === 'transform' ? ' active' : ''), [
        ConstructGlyph('Move'),
        Construct('small', null, 'V')
    ]);
    GizmoBtn.type = 'button';
    GizmoBtn.title = 'Select & Transform Parametric CSG Nodes (V)';
    GizmoBtn.setAttribute('aria-label', 'Select and transform CSG node');
    GizmoBtn.addEventListener('click', () => SelectTool('transform'));
    Rail.append(GizmoBtn);
}

function SelectTool(ToolId)
{
    App.activeTool = ToolId;
    const Canvas = document.getElementById('ViewportCanvas');
    Canvas.classList.toggle('orbit-mode', ToolId === 'transform');

    const BrushSpec = SCULPT_BRUSHES.find(B => B.id === ToolId);
    if (BrushSpec)
    {
        document.getElementById('viewport-object').textContent = BrushSpec.name;
    }
    else
    {
        document.getElementById('viewport-object').textContent = 'CSG Transform Gizmo';
    }
    RenderViewportToolRail();
    if (App.inspectorTab === 'brush') RebuildInspector();
    Broadcast();
}

function SetupViewportInteractions()
{
    const Canvas = document.getElementById('ViewportCanvas');
    Canvas.addEventListener('contextmenu', E => E.preventDefault());

    Canvas.addEventListener('pointermove', Event =>
    {
        if (App.gesture)
        {
            HandleActiveGestureMove(Event);
            return;
        }
        UpdateHoverRaycast(Event);
    });

    Canvas.addEventListener('pointerleave', () =>
    {
        if (!App.gesture)
        {
            App.brushHover.active = false;
        }
    });

    Canvas.addEventListener('pointerdown', Event =>
    {
        Canvas.setPointerCapture(Event.pointerId);
        const IsRight = Event.button === 2;
        const IsMiddle = Event.button === 1;

        if (IsRight || IsMiddle)
        {
            App.gesture = {
                mode: IsMiddle || Event.shiftKey ? 'pan' : 'orbit',
                startX: Event.clientX,
                startY: Event.clientY,
                startYaw: App.viewport.camera.yaw,
                startPitch: App.viewport.camera.pitch,
                startTarget: App.viewport.camera.target.slice()
            };
            return;
        }

        // 1. Check 3D CSG Transform Gizmo handles first if a CSG node is selected
        const SelectedNode = GetSelectedCsgNode();
        if (SelectedNode && App.stage.showGizmo && !SelectedNode.locked)
        {
            const Handle = App.viewport.pickGizmoHandle(Event.clientX, Event.clientY);
            if (Handle)
            {
                App.gesture = {
                    mode: 'gizmo',
                    handle: Handle,
                    node: SelectedNode,
                    startX: Event.clientX,
                    startY: Event.clientY,
                    startPos: SelectedNode.position.slice(),
                    moved: false
                };
                return;
            }
        }

        // 2. Raycast against Hybrid SDF
        const Ray = App.viewport.screenToRay(Event.clientX, Event.clientY);
        const Hit = App.volume.raycastHybrid(Ray.origin, Ray.dir, App.csgNodes, App.volumeVisible);

        if (App.activeTool === 'transform')
        {
            if (Hit.hit && Hit.csgNodeId)
            {
                SelectEntity(Hit.csgNodeId, true);
            }
            else if (Hit.hit)
            {
                SelectEntity('volume', true);
            }
            App.gesture = {
                mode: Event.shiftKey ? 'pan' : 'orbit',
                startX: Event.clientX,
                startY: Event.clientY,
                startYaw: App.viewport.camera.yaw,
                startPitch: App.viewport.camera.pitch,
                startTarget: App.viewport.camera.target.slice()
            };
            return;
        }

        // 3. Sculpt Brush active: clicking on empty sky orbits (or Shift+sky pans); clicking on model sculpts!
        if (!Hit.hit)
        {
            App.gesture = {
                mode: Event.shiftKey ? 'pan' : 'orbit',
                startX: Event.clientX,
                startY: Event.clientY,
                startYaw: App.viewport.camera.yaw,
                startPitch: App.viewport.camera.pitch,
                startTarget: App.viewport.camera.target.slice()
            };
            return;
        }

        if (App.volumeLocked)
        {
            Toast('Clay Volume is locked — unlock it in the Outliner to sculpt');
            return;
        }
        if (!App.volumeVisible)
        {
            Toast('Clay Volume is hidden — enable visibility to sculpt');
            return;
        }

        const EffectiveTool = Event.shiftKey ? 'smooth' : App.activeTool;
        const Settings = BuildStrokeSettings(Event);
        const T0 = performance.now();
        App.volume.beginStroke(EffectiveTool, Hit.pos, Hit.normal, Settings);
        App.lastStrokeMs = performance.now() - T0;

        const Basis = App.viewport.computeCameraBasis();
        App.gesture = {
            mode: 'sculpt',
            tool: EffectiveTool,
            planeOrigin: Hit.pos.slice(),
            planeNormal: Vec3Scale(Basis.Fwd, -1), // Camera-facing drag plane for Grab / Snake / Stamp & off-silhouette extension
            surfaceNormal: Hit.normal.slice(),
            startBoundsRev: App.volume.boundsRevision,
            lastLoggedBoundsRev: App.volume.boundsRevision,
            dabs: 1
        };
        App.brushHover = {
            active: true,
            pos: Hit.pos,
            normal: Hit.normal,
            radius: App.brush.radius,
            accentCss: GetBrushAccentCss(EffectiveTool),
            accentLinear: HexToLinearRgb(GetBrushAccentCss(EffectiveTool))
        };
        App.dirtySliceMinimap = true;
    });

    const EndPointer = Event =>
    {
        if (!App.gesture) return;
        const G = App.gesture;
        App.gesture = null;

        if (G.mode === 'sculpt')
        {
            App.volume.endStroke();
            App.strokeCount++;
            const BrushSpec = SCULPT_BRUSHES.find(B => B.id === G.tool);
            RecordHistory(BrushSpec ? BrushSpec.short : 'Sculpt');
            if (App.volume.boundsRevision !== G.startBoundsRev)
            {
                Toast(`Domain auto-expanded to ${App.volume.span.toFixed(1)}³ m`);
            }
            RefreshTelemetry(true);
        }
        else if (G.mode === 'gizmo' && G.moved)
        {
            RecordHistory(`Move ${G.node.name}`);
            RefreshTelemetry(false);
        }
        if (Canvas.hasPointerCapture?.(Event.pointerId))
        {
            Canvas.releasePointerCapture(Event.pointerId);
        }
    };

    Canvas.addEventListener('pointerup', EndPointer);
    Canvas.addEventListener('pointercancel', EndPointer);

    Canvas.addEventListener('wheel', Event =>
    {
        Event.preventDefault();
        const Factor = Math.exp(Event.deltaY * 0.0011);
        App.viewport.camera.distance = Clamp(App.viewport.camera.distance * Factor, 0.85, 22.0);
    }, { passive: false });
}

function HandleActiveGestureMove(Event)
{
    const G = App.gesture;
    const Dx = Event.clientX - (G.startX || 0);
    const Dy = Event.clientY - (G.startY || 0);

    if (G.mode === 'orbit')
    {
        App.viewport.camera.turntable = false;
        document.getElementById('toggle-turntable-btn').classList.remove('active');
        App.viewport.camera.yaw = G.startYaw - Dx * 0.0068;
        App.viewport.camera.pitch = Clamp(G.startPitch + Dy * 0.0068, -1.48, 1.48);
        return;
    }

    if (G.mode === 'pan')
    {
        const Basis = App.viewport.computeCameraBasis();
        const Scale = App.viewport.camera.distance * 0.0016;
        App.viewport.camera.target = Vec3Add(
            G.startTarget,
            Vec3Add(Vec3Scale(Basis.Right, -Dx * Scale), Vec3Scale(Basis.Up, Dy * Scale))
        );
        return;
    }

    if (G.mode === 'gizmo')
    {
        const Basis = App.viewport.computeCameraBasis();
        const Rect = App.viewport.canvas.getBoundingClientRect();
        const WorldPerPx = (2.0 * App.viewport.camera.distance * Basis.TanHalfFov) / Math.max(120, Rect.height);
        const WorldDeltaScreen = Vec3Add(
            Vec3Scale(Basis.Right, Dx * WorldPerPx),
            Vec3Scale(Basis.Up, -Dy * WorldPerPx)
        );

        if (G.handle.type === 'translate-axis')
        {
            const AxisDir = G.handle.dir;
            const Proj = Vec3Dot(WorldDeltaScreen, AxisDir);
            G.node.position = Vec3Add(G.startPos, Vec3Scale(AxisDir, Proj));
        }
        else
        {
            G.node.position = Vec3Add(G.startPos, WorldDeltaScreen);
        }
        G.moved = true;
        App.dirtySliceMinimap = true;
        Broadcast();
        return;
    }

    if (G.mode === 'sculpt')
    {
        const Ray = App.viewport.screenToRay(Event.clientX, Event.clientY);
        const Settings = BuildStrokeSettings(Event);
        const T0 = performance.now();

        let HitPos = null;
        let HitNormal = G.surfaceNormal;

        if (G.tool === 'grab' || G.tool === 'snake' || G.tool === 'stamp')
        {
            // Intersect ray with camera-facing plane through stroke origin for unrestricted 3D pull!
            const Denom = Vec3Dot(Ray.dir, G.planeNormal);
            if (Math.abs(Denom) > 1e-5)
            {
                const TPlane = Vec3Dot(Vec3Sub(G.planeOrigin, Ray.origin), G.planeNormal) / Denom;
                if (TPlane > 0.05)
                {
                    HitPos = Vec3Add(Ray.origin, Vec3Scale(Ray.dir, TPlane));
                }
            }
        }
        else
        {
            const Hit = App.volume.raycastHybrid(Ray.origin, Ray.dir, App.csgNodes, App.volumeVisible);
            if (Hit.hit)
            {
                // Prevent abrupt depth jumps when grazing across a foreground silhouette onto a distant background surface
                const StepDist = Math.hypot(
                    Hit.pos[0] - G.planeOrigin[0],
                    Hit.pos[1] - G.planeOrigin[1],
                    Hit.pos[2] - G.planeOrigin[2]
                );
                const MaxJump = Math.max(0.45, App.brush.radius * 2.2);
                if (StepDist <= MaxJump)
                {
                    HitPos = Hit.pos;
                    HitNormal = Hit.normal;
                    G.planeOrigin = Hit.pos.slice();
                    G.surfaceNormal = Hit.normal.slice();
                }
                else
                {
                    const Denom = Vec3Dot(Ray.dir, G.planeNormal);
                    if (Math.abs(Denom) > 1e-5)
                    {
                        const TPlane = Vec3Dot(Vec3Sub(G.planeOrigin, Ray.origin), G.planeNormal) / Denom;
                        if (TPlane > 0.05) HitPos = Vec3Add(Ray.origin, Vec3Scale(Ray.dir, TPlane));
                    }
                }
            }
            else
            {
                const Denom = Vec3Dot(Ray.dir, G.planeNormal);
                if (Math.abs(Denom) > 1e-5)
                {
                    const TPlane = Vec3Dot(Vec3Sub(G.planeOrigin, Ray.origin), G.planeNormal) / Denom;
                    if (TPlane > 0.05)
                    {
                        HitPos = Vec3Add(Ray.origin, Vec3Scale(Ray.dir, TPlane));
                        G.planeOrigin = HitPos.slice();
                    }
                }
            }
        }

        if (HitPos)
        {
            const Touched = App.volume.continueStroke(HitPos, HitNormal, Settings);
            if (Touched > 0)
            {
                App.lastStrokeMs = performance.now() - T0;
                App.dirtySliceMinimap = true;
            }
            if (App.volume.boundsRevision !== G.lastLoggedBoundsRev)
            {
                G.lastLoggedBoundsRev = App.volume.boundsRevision;
                SyncQuickDock();
                SyncStatusBar();
                PushLog('BOUNDS', `Elastic domain auto-expanded to ${App.volume.span.toFixed(1)}³ m`, 'warm');
            }
            const ActiveStroke = App.volume.stroke;
            App.brushHover = {
                active: true,
                pos: HitPos,
                normal: HitNormal,
                radius: App.brush.radius,
                lazyFrom: (App.brush.lazyRadius > 0.002 && ActiveStroke) ? ActiveStroke.lazyPos : null,
                accentCss: GetBrushAccentCss(Settings.shiftSmooth ? 'smooth' : G.tool),
                accentLinear: HexToLinearRgb(GetBrushAccentCss(Settings.shiftSmooth ? 'smooth' : G.tool))
            };
        }
    }
}

function UpdateHoverRaycast(Event)
{
    const Ray = App.viewport.screenToRay(Event.clientX, Event.clientY);
    const Hit = App.volume.raycastHybrid(Ray.origin, Ray.dir, App.csgNodes, App.volumeVisible);
    const CursorEl = document.getElementById('status-cursor');

    if (Hit.hit)
    {
        const Tool = Event.shiftKey ? 'smooth' : App.activeTool;
        const Accent = GetBrushAccentCss(Tool);
        App.brushHover = {
            active: App.activeTool !== 'transform',
            pos: Hit.pos,
            normal: Hit.normal,
            radius: App.brush.radius,
            accentCss: Accent,
            accentLinear: HexToLinearRgb(Accent)
        };
        if (CursorEl)
        {
            CursorEl.textContent = `XYZ (${Hit.pos[0].toFixed(2)}, ${Hit.pos[1].toFixed(2)}, ${Hit.pos[2].toFixed(2)}) m`;
        }
    }
    else
    {
        App.brushHover.active = false;
        if (CursorEl) CursorEl.textContent = 'Cursor: stage sky (drag to orbit)';
    }
}

function GetBrushAccentCss(ToolId)
{
    return SCULPT_BRUSHES.find(B => B.id === ToolId)?.accent || '#e8c27a';
}

function BuildStrokeSettings(Event)
{
    return {
        radius: App.brush.radius,
        strength: App.brush.strength,
        smoothness: App.brush.smoothness,
        spacing: App.brush.spacing,
        lazyRadius: App.brush.lazyRadius,
        adaptivePlane: App.brush.adaptivePlane,
        paintWithSculpt: App.brush.paintWithSculpt,
        brushColor: App.brush.brushColor,
        stampShape: App.brush.stampShape,
        stampOp: App.brush.stampOp,
        falloffCurve: App.brush.falloffCurve,
        invert: !!Event.altKey,
        shiftSmooth: !!Event.shiftKey && App.activeTool !== 'smooth',
        symmetry: App.symmetry
    };
}

/*==========================================================================================================================================
                                             RIGHT PANEL: 4-TAB SUN + FLUID INSPECTOR
==========================================================================================================================================*/
function SetInspectorTab(TabKey)
{
    App.inspectorTab = TabKey;
    document.querySelectorAll('#inspector-tabs button').forEach(Btn =>
    {
        Btn.classList.toggle('active', Btn.dataset.itab === TabKey);
    });
    RebuildInspector();
}

function SyncInspectorHeader()
{
    const Crumb = document.getElementById('inspector-crumb');
    const SymbolSlot = document.getElementById('object-symbol');
    const NameInput = document.getElementById('object-name');
    const TypeSpan = document.getElementById('object-type');
    const EnabledChk = document.getElementById('object-enabled');

    SymbolSlot.textContent = '';
    const Node = GetSelectedCsgNode();
    if (!Node)
    {
        Crumb.textContent = 'Inspector / Volumetric SDF / Clay';
        SymbolSlot.append(ConstructGlyph('Layers'));
        SymbolSlot.style.color = '#e8c27a';
        if (document.activeElement !== NameInput) NameInput.value = App.volumeName;
        TypeSpan.textContent = `VOLUMETRIC SDF · ${App.volume.nx}³ RGBA16F`;
        EnabledChk.checked = App.volumeVisible;
    }
    else
    {
        Crumb.textContent = `Inspector / CSG / ${SHAPE_NAMES[Node.shape] || 'Primitive'}`;
        SymbolSlot.append(ConstructGlyph(SHAPE_GLYPHS[Node.shape] || 'Boxes'));
        SymbolSlot.style.color = Node.op === 1 ? '#ff6b6b' : Node.op === 2 ? '#7ec8ff' : '#3ec46d';
        if (document.activeElement !== NameInput) NameInput.value = Node.name;
        TypeSpan.textContent = `${(SHAPE_NAMES[Node.shape] || 'SDF').toUpperCase()} · ${(BOOL_OP_NAMES[Node.op] || 'UNION').toUpperCase()}`;
        EnabledChk.checked = Node.enabled !== false;
    }
}

function RebuildInspector()
{
    ReleaseScope(App.inspectorScope);
    const Content = document.getElementById('InspectorContent');
    Content.textContent = '';
    SyncInspectorHeader();

    const Scope = OpenScope();
    App.inspectorScope = Scope;

    if (App.inspectorTab === 'brush') BuildBrushInspectorTab(Content);
    else if (App.inspectorTab === 'node') BuildNodeInspectorTab(Content);
    else if (App.inspectorTab === 'material') BuildMaterialInspectorTab(Content);
    else BuildExportInspectorTab(Content);

    CloseScope(Scope);
    Broadcast();
}

function BuildBrushInspectorTab(Host)
{
    // 1. 10-Brush Visual Selector Grid
    Host.append(Construct('div', 'section-subhead', [
        Construct('span', null, 'Sculpt Brushes'),
        Construct('span', null, 'Keys 1 – 0')
    ]));
    const Grid = Construct('div', 'brush-grid');
    for (const B of SCULPT_BRUSHES)
    {
        const Icon = Construct('span', 'brush-card-icon', ConstructGlyph(B.glyph));
        Icon.style.color = B.accent;
        const Card = Construct('button', 'brush-card' + (App.activeTool === B.id ? ' active' : ''), [
            Icon,
            Construct('span', 'brush-card-copy', [
                Construct('strong', null, B.short),
                Construct('small', null, B.id === 'clay' ? 'C1 Union' : B.id === 'carve' ? 'Subtract' : B.id === 'grab' ? '3D Warp' : 'Volumetric')
            ]),
            Construct('span', 'brush-key', B.key)
        ]);
        Card.type = 'button';
        Card.title = B.description;
        Card.addEventListener('click', () => SelectTool(B.id));
        Grid.append(Card);
    }
    Host.append(Grid);

    const ActiveBrushSpec = SCULPT_BRUSHES.find(B => B.id === App.activeTool) || SCULPT_BRUSHES[0];

    // 2. Stroke & Kernel Parameters Card
    const StrokeSec = SectionCard(Host, {
        Id: 'brush-params',
        Title: ActiveBrushSpec.name,
        Glyph: ActiveBrushSpec.glyph,
        Badge: `KEY ${ActiveBrushSpec.key}`,
        Hint: ActiveBrushSpec.description
    });

    SliderRow(StrokeSec.Inner, {
        Label: 'Brush radius',
        Minimum: 0.04, Maximum: 1.10, Step: 0.01, Digits: 2, Unit: 'm',
        Read: () => App.brush.radius,
        Write: V => { App.brush.radius = V; App.brushHover.radius = V; SyncQuickDock(); }
    });
    SliderRow(StrokeSec.Inner, {
        Label: 'Sculpt strength',
        Minimum: 0.05, Maximum: 1.00, Step: 0.01, Digits: 2, Unit: '×',
        Read: () => App.brush.strength,
        Write: V => { App.brush.strength = V; SyncQuickDock(); }
    });
    SliderRow(StrokeSec.Inner, {
        Label: 'Boolean fillet smoothness (k)',
        Minimum: 0.05, Maximum: 1.00, Step: 0.01, Digits: 2, Unit: 'R',
        Read: () => App.brush.smoothness,
        Write: V => { App.brush.smoothness = V; }
    });
    SliderRow(StrokeSec.Inner, {
        Label: 'Dab spacing',
        Minimum: 0.08, Maximum: 0.65, Step: 0.01, Digits: 2, Unit: 'R',
        Read: () => App.brush.spacing,
        Write: V => { App.brush.spacing = V; }
    });
    SliderRow(StrokeSec.Inner, {
        Label: 'Lazy-mouse steady rope',
        Minimum: 0.00, Maximum: 0.35, Step: 0.01, Digits: 2, Unit: 'm',
        Read: () => App.brush.lazyRadius,
        Write: V => { App.brush.lazyRadius = V; }
    });

    if (App.activeTool === 'stamp')
    {
        SelectRow(StrokeSec.Inner, {
            Label: 'Stamp SDF shape',
            Options: SHAPE_TYPES,
            Read: () => App.brush.stampShape,
            Write: V => { App.brush.stampShape = Number(V); }
        });
        SegmentedRow(StrokeSec.Inner, {
            Label: 'Stamp boolean mode',
            Options: [['Union', 0], ['Subtract', 1], ['Chamfer', 3]],
            Read: () => App.brush.stampOp,
            Write: V => { App.brush.stampOp = Number(V); }
        });
    }

    if (App.activeTool === 'flatten')
    {
        SwitchRow(StrokeSec.Inner, {
            Label: 'Adaptive surface plane',
            Detail: 'Resample trim plane normal continuously along stroke',
            Read: () => App.brush.adaptivePlane,
            Write: V => { App.brush.adaptivePlane = V; }
        });
    }

    // 3. Interactive Brush Falloff Curve Card
    const CurveSec = SectionCard(Host, {
        Id: 'falloff-curve',
        Title: 'Radial Falloff Profile',
        Glyph: 'Activity',
        Status: () => App.brush.falloffCurve.Custom ? 'CUSTOM' : App.brush.falloffCurve.Mode.toUpperCase()
    });
    FalloffCurveEditor(CurveSec.Inner, App.brush.falloffCurve);

    // 4. Polypaint & Symmetry Card
    const SymSec = SectionCard(Host, {
        Id: 'brush-sym-paint',
        Title: 'Polypaint & Mirror Symmetry',
        Glyph: 'Symmetry',
        Badge: 'LIVE'
    });
    ColourRow(SymSec.Inner, {
        Label: 'Brush clay albedo',
        Read: () => App.brush.brushColor,
        Write: C => { App.brush.brushColor = C; }
    });
    SwitchRow(SymSec.Inner, {
        Label: 'Deposit albedo with sculpt',
        Detail: 'Blends brush RGB into RGBA16F volume during Clay/Carve/Stamp',
        Read: () => App.brush.paintWithSculpt,
        Write: V => { App.brush.paintWithSculpt = V; }
    });

    const TileGrid = Construct('div', 'property-switches');
    ToggleTile(TileGrid, {
        Label: 'Mirror X', Glyph: 'Symmetry',
        Read: () => App.symmetry.x,
        Write: V => { App.symmetry.x = V; SyncSymmetryBar(); }
    });
    ToggleTile(TileGrid, {
        Label: 'Mirror Y', Glyph: 'Symmetry',
        Read: () => App.symmetry.y,
        Write: V => { App.symmetry.y = V; SyncSymmetryBar(); }
    });
    ToggleTile(TileGrid, {
        Label: 'Mirror Z', Glyph: 'Symmetry',
        Read: () => App.symmetry.z,
        Write: V => { App.symmetry.z = V; SyncSymmetryBar(); }
    });
    ToggleTile(TileGrid, {
        Label: 'Lock Clay', Glyph: 'Lock',
        Read: () => App.volumeLocked,
        Write: V => { App.volumeLocked = V; RenderOutliner(); }
    });
    SymSec.Inner.append(TileGrid);

    SliderRow(SymSec.Inner, {
        Label: 'Radial Y-axis symmetry',
        Minimum: 1, Maximum: 12, Step: 1, Digits: 0, Unit: '×',
        Read: () => App.symmetry.radial,
        Write: V => { App.symmetry.radial = V | 0; SyncSymmetryBar(); }
    });
}

function BuildNodeInspectorTab(Host)
{
    const Node = GetSelectedCsgNode();

    if (!Node)
    {
        // Volumetric Clay Inspector (Sun Hero Card + Histogram + KPI Trio + Quick Tiles + Volume Ops)
        const Hero = Construct('div', 'hero-card');
        const EikonalOk = Math.abs((App.telemetry?.meanEikonal ?? 1) - 1) < 0.28;
        Hero.append(
            Construct('div', 'hero-top', [
                Construct('div', 'budget-chip', [
                    Construct('span', 'mark' + (EikonalOk ? '' : ' warm'), ConstructGlyph(EikonalOk ? 'Check' : 'Alert')),
                    Construct('span', null, EikonalOk ? 'Eikonal field calibrated' : 'Drift detected — Normalize |∇d|')
                ]),
                Construct('span', 'estimate-tag', '01 / RGBA16F VOLUME')
            ]),
            Construct('div', 'hero-title', 'Occupied interior clay voxels (d ≤ 0)'),
            Construct('div', 'hero-metrics', [
                Construct('div', 'metric-big', [
                    document.createTextNode((App.telemetry?.occupiedVoxels ?? 0).toLocaleString('en-US')),
                    Construct('small', null, 'voxels')
                ]),
                Construct('div', 'metric-side', [
                    Construct('div', 'rate', `|∇d| ${(App.telemetry?.meanEikonal ?? 1).toFixed(2)}`),
                    Construct('div', 'low', `Step ${(App.volume.voxelSize * 100).toFixed(2)} cm`)
                ])
            ])
        );
        DistanceHistogram(Hero, () => App.telemetry?.bins || []);
        Host.append(Hero);

        MetricTrio(Host, [
            {
                Label: 'Grid lattice',
                Resolve: () => [`${App.volume.nx}³`, `${(App.volume.voxelCount / 1000).toFixed(0)}k cells`]
            },
            {
                Label: 'GPU footprint',
                Resolve: () => [`${(App.telemetry?.memoryMB ?? 7.08).toFixed(1)} MB`, 'RGBA16F 3D']
            },
            {
                Label: 'Iso-volume',
                Highlight: true,
                Resolve: () => [`${(App.telemetry?.liters ?? 0).toFixed(1)} L`, 'Closed SDF']
            }
        ]);

        Host.append(Construct('div', 'section-subhead', [
            Construct('span', null, 'Quick Controls'),
            Construct('span', null, 'CLAY VOLUME')
        ]));
        const Tiles = Construct('div', 'property-switches');
        ToggleTile(Tiles, {
            Label: 'Visible', Glyph: 'Eye',
            Read: () => App.volumeVisible,
            Write: V => { App.volumeVisible = V; App.dirtySliceMinimap = true; RenderOutliner(); SyncInspectorHeader(); }
        });
        ToggleTile(Tiles, {
            Label: 'Locked', Glyph: 'Lock',
            Read: () => App.volumeLocked,
            Write: V => { App.volumeLocked = V; RenderOutliner(); }
        });
        ToggleTile(Tiles, {
            Label: 'Cut Plane', Glyph: 'Slice',
            Read: () => App.stage.sliceEnabled,
            Write: V => { App.stage.sliceEnabled = V; document.getElementById('toggle-slice-btn').classList.toggle('active', V); }
        });
        ToggleTile(Tiles, {
            Label: '3D Grid', Glyph: 'Grid',
            Read: () => App.stage.showGrid,
            Write: V => { App.stage.showGrid = V; document.getElementById('toggle-grid-btn').classList.toggle('active', V); }
        });
        Host.append(Tiles);

        ActionRow(Host, [
            {
                Label: 'Normalize |∇d|', Glyph: 'Gauge', Tone: 'accent',
                Title: 'Godunov upwind Eikonal re-distancing',
                Act: () =>
                {
                    App.volume.normalizeEikonal(3);
                    RecordHistory('Normalize |∇d|');
                    RefreshTelemetry(true);
                    Toast('Normalized SDF Eikonal gradient |∇d| → 1.0');
                }
            },
            {
                Label: 'Smooth All', Glyph: 'Drop',
                Act: () =>
                {
                    App.volume.smoothEntireVolume(0.42);
                    RecordHistory('Global Smooth');
                    RefreshTelemetry(true);
                    Toast('Applied global 3D Laplacian relax');
                }
            }
        ]);

        ActionRow(Host, [
            {
                Label: 'Hollow Shell', Glyph: 'Compass',
                Act: () =>
                {
                    App.volume.shellHollowVolume(0.10);
                    RecordHistory('Hollow Shell');
                    RefreshTelemetry(true);
                    Toast('Converted clay into 10 cm hollow onion shell');
                }
            },
            {
                Label: 'Clear Clay', Glyph: 'Trash', Tone: 'danger',
                Act: () =>
                {
                    App.volume.clearVolume(App.brush.brushColor);
                    RecordHistory('Clear Clay');
                    RefreshTelemetry(true);
                    Toast('Cleared Sculpt Volume');
                }
            }
        ]);

        const ResSec = SectionCard(Host, {
            Id: 'vol-res-slice',
            Title: 'Elastic Bounds, Resolution & Slicer',
            Glyph: 'Boxes',
            Status: () => `${App.volume.span.toFixed(1)}³ m · ${App.volume.boundsMode.toUpperCase()}`
        });
        SegmentedRow(ResSec.Inner, {
            Label: 'Out-of-bounds behavior (when strokes cross edge)',
            Options: [['Auto-Expand', 'expand'], ['Soft Cap', 'soft'], ['Hard Clip', 'hard']],
            Read: () => App.volume.boundsMode,
            Write: Mode =>
            {
                App.volume.boundsMode = Mode;
                if (Mode !== 'hard') { App.volume.applySoftBoundaryCushion(); App.volume.markAllDirty(); }
                const Sel = document.getElementById('dock-bounds-select');
                if (Sel) Sel.value = Mode;
                SyncQuickDock();
                Toast(Mode === 'expand'
                    ? 'Auto-Expand active: domain grows up to 12³ m as you sculpt outward'
                    : Mode === 'soft'
                    ? 'Soft Cushion Cap active: outer walls smoothly round clay'
                    : 'Hard Clip active');
            }
        });
        SwitchRow(ResSec.Inner, {
            Label: 'Always show 3D domain bounds cage',
            Detail: 'Corner brackets also auto-glow whenever your brush approaches an edge',
            Read: () => App.stage.showBoundsCage,
            Write: V =>
            {
                App.stage.showBoundsCage = V;
                document.getElementById('toggle-bounds-btn').classList.toggle('active', V);
            }
        });
        ActionRow(ResSec.Inner, [
            {
                Label: 'Compact / Fit Bounds to Clay', Glyph: 'Maximize', Tone: 'accent',
                Title: 'Tightly re-center and fit the cubic voxel grid around sculpted clay to maximize detail density',
                Act: () => FitVolumeBoundsToClay()
            }
        ]);
        SegmentedRow(ResSec.Inner, {
            Label: 'Voxel grid resolution (trilinear resample)',
            Options: [['64³ Fast', 64], ['96³ Studio', 96], ['128³ Fine', 128]],
            Read: () => App.volume.nx,
            Write: NewN => ChangeVolumeResolution(NewN)
        });
        SegmentedRow(ResSec.Inner, {
            Label: 'Cross-section slice axis',
            Options: [['X Plane', 0], ['Y Plane', 1], ['Z Plane', 2]],
            Read: () => App.stage.sliceAxis,
            Write: Ax => { App.stage.sliceAxis = Ax; App.dirtySliceMinimap = true; }
        });
        SliderRow(ResSec.Inner, {
            Label: 'Slice plane offset',
            Minimum: -4.0, Maximum: 4.0, Step: 0.02, Digits: 2, Unit: 'm',
            Read: () => App.stage.sliceOffset,
            Write: V => { App.stage.sliceOffset = V; App.dirtySliceMinimap = true; }
        });
        return;
    }

    // Selected Parametric CSG Node Inspector!
    MetricTrio(Host, [
        {
            Label: 'Primitive',
            Resolve: () => [SHAPE_NAMES[Node.shape] || 'Sphere', Node.locked ? 'Locked' : 'Editable']
        },
        {
            Label: 'Boolean mode',
            Resolve: () => [(BOOL_OP_NAMES[Node.op] || 'Union').replace('Smooth ', ''), Node.enabled !== false ? 'Active' : 'Muted']
        },
        {
            Label: 'Fillet blend k',
            Highlight: true,
            Resolve: () => [`${(Node.blend ?? 0.06).toFixed(3)} m`, 'C1 polynomial']
        }
    ]);

    const Tiles = Construct('div', 'property-switches');
    ToggleTile(Tiles, {
        Label: 'Enabled', Glyph: 'Eye',
        Read: () => Node.enabled !== false,
        Write: V => { Node.enabled = V; App.dirtySliceMinimap = true; RenderOutliner(); SyncInspectorHeader(); }
    });
    ToggleTile(Tiles, {
        Label: 'Locked', Glyph: 'Lock',
        Read: () => !!Node.locked,
        Write: V => { Node.locked = V; RenderOutliner(); }
    });
    ToggleTile(Tiles, {
        Label: 'Mirror X', Glyph: 'Symmetry',
        Read: () => !!Node.mirrorX,
        Write: V => { Node.mirrorX = V; App.dirtySliceMinimap = true; RenderOutliner(); }
    });
    ToggleTile(Tiles, {
        Label: 'Mirror Z', Glyph: 'Symmetry',
        Read: () => !!Node.mirrorZ,
        Write: V => { Node.mirrorZ = V; App.dirtySliceMinimap = true; RenderOutliner(); }
    });
    Host.append(Tiles);

    ActionRow(Host, [
        {
            Label: 'Bake → Clay', Glyph: 'Layers', Tone: 'accent',
            Title: 'Voxelize this CSG node into the sculptable Clay Volume and remove the parametric node',
            Availability: () => ({ Enabled: !App.volumeLocked, Reason: 'Unlock Clay Volume first' }),
            Act: () =>
            {
                App.volume.bakeCsgNodes([Node], false);
                App.csgNodes = App.csgNodes.filter(N => N.id !== Node.id);
                App.selectedId = 'volume';
                RecordHistory(`Bake ${Node.name}`);
                RefreshTelemetry(true);
                RebuildInspector();
                Toast(`Baked "${Node.name}" into Clay Volume`);
                PushLog('BAKE', `Voxelized CSG node "${Node.name}" into ${App.volume.nx}³ clay`, 'warm');
            }
        },
        {
            Label: 'Duplicate', Glyph: 'Copy',
            Act: () =>
            {
                if (App.csgNodes.length >= 24) { Toast('Max 24 CSG nodes reached'); return; }
                const Copy = MakeCsgNode({
                    ...JSON.parse(JSON.stringify(Node)),
                    id: undefined,
                    name: Node.name + ' Copy',
                    position: [Node.position[0] + 0.14, Node.position[1] + 0.08, Node.position[2]]
                });
                App.csgNodes.push(Copy);
                RecordHistory(`Duplicate ${Node.name}`);
                SelectEntity(Copy.id, true);
                RefreshTelemetry(false);
            }
        },
        {
            Label: 'Delete', Glyph: 'Trash', Tone: 'danger',
            Act: () =>
            {
                App.csgNodes = App.csgNodes.filter(N => N.id !== Node.id);
                App.selectedId = 'volume';
                RecordHistory(`Delete ${Node.name}`);
                RefreshTelemetry(false);
                RebuildInspector();
                Toast(`Removed "${Node.name}"`);
            }
        }
    ]);

    const Editable = () => ({ Enabled: !Node.locked, Reason: 'Node is locked' });

    const CsgSec = SectionCard(Host, {
        Id: 'csg-shape-op',
        Title: 'Shape & Boolean Operator',
        Glyph: 'Boxes',
        Badge: 'ANALYTIC SDF'
    });
    SelectRow(CsgSec.Inner, {
        Label: 'Analytical SDF primitive',
        Options: SHAPE_TYPES,
        Availability: Editable,
        Read: () => Node.shape,
        Write: V => { Node.shape = Number(V); App.dirtySliceMinimap = true; RenderOutliner(); SyncInspectorHeader(); }
    });
    SelectRow(CsgSec.Inner, {
        Label: 'CSG boolean operation',
        Options: BOOL_OPS,
        Availability: Editable,
        Read: () => Node.op,
        Write: V => { Node.op = Number(V); App.dirtySliceMinimap = true; RenderOutliner(); SyncInspectorHeader(); }
    });
    SliderRow(CsgSec.Inner, {
        Label: 'Smooth blend radius (k)',
        Minimum: 0.005, Maximum: 0.50, Step: 0.005, Digits: 3, Unit: 'm',
        Availability: Editable,
        Read: () => Node.blend ?? 0.06,
        Write: V => { Node.blend = V; App.dirtySliceMinimap = true; RenderOutliner(); }
    });
    ColourRow(CsgSec.Inner, {
        Label: 'Node linear albedo',
        Availability: Editable,
        Read: () => Node.color,
        Write: C => { Node.color = C; }
    });

    const XformSec = SectionCard(Host, {
        Id: 'csg-transform',
        Title: '3D Transform & Extents',
        Glyph: 'Move',
        Badge: 'METRES'
    });
    VectorRow(XformSec.Inner, {
        Label: 'World position', Unit: 'm', Step: 0.02, Digits: 2,
        Availability: Editable,
        Read: () => Node.position,
        Write: V => { Node.position = V; App.dirtySliceMinimap = true; }
    });
    VectorRow(XformSec.Inner, {
        Label: 'Euler rotation', Unit: 'deg', Step: 2, Digits: 1,
        Availability: Editable,
        Read: () => Node.rotation,
        Write: V => { Node.rotation = V; App.dirtySliceMinimap = true; }
    });
    VectorRow(XformSec.Inner, {
        Label: 'Half-extents / parameters', Unit: 'm', Step: 0.02, Digits: 2, Minimum: 0.01, Maximum: 1.8,
        Availability: Editable,
        Read: () => Node.size,
        Write: V => { Node.size = V.map(X => Math.max(0.01, X)); App.dirtySliceMinimap = true; }
    });
    SliderRow(XformSec.Inner, {
        Label: 'Uniform scale',
        Minimum: 0.15, Maximum: 3.00, Step: 0.01, Digits: 2, Unit: '×',
        Availability: Editable,
        Read: () => Node.scale ?? 1.0,
        Write: V => { Node.scale = V; App.dirtySliceMinimap = true; }
    });

    const ModSec = SectionCard(Host, {
        Id: 'csg-modifiers',
        Title: 'Domain & Shell Modifiers',
        Glyph: 'Sparkles',
        Badge: 'NON-DESTRUCTIVE'
    });
    SliderRow(ModSec.Inner, {
        Label: 'Corner bevel rounding',
        Minimum: 0.00, Maximum: 0.25, Step: 0.005, Digits: 3, Unit: 'm',
        Availability: Editable,
        Read: () => Node.round ?? 0,
        Write: V => { Node.round = V; App.dirtySliceMinimap = true; }
    });
    SliderRow(ModSec.Inner, {
        Label: 'Onion hollow shell thickness',
        Minimum: 0.00, Maximum: 0.20, Step: 0.005, Digits: 3, Unit: 'm',
        Availability: Editable,
        Read: () => Node.shell ?? 0,
        Write: V => { Node.shell = V; App.dirtySliceMinimap = true; }
    });
    SliderRow(ModSec.Inner, {
        Label: 'Y-axis domain twist',
        Minimum: -4.0, Maximum: 4.0, Step: 0.05, Digits: 2, Unit: 'rad',
        Availability: Editable,
        Read: () => Node.twist ?? 0,
        Write: V => { Node.twist = V; App.dirtySliceMinimap = true; }
    });
    SliderRow(ModSec.Inner, {
        Label: 'Polar radial array count',
        Minimum: 1, Maximum: 12, Step: 1, Digits: 0, Unit: '×',
        Availability: Editable,
        Read: () => Node.radialCount || 1,
        Write: V => { Node.radialCount = V | 0; App.dirtySliceMinimap = true; RenderOutliner(); }
    });
}

function BuildMaterialInspectorTab(Host)
{
    const LensSec = SectionCard(Host, {
        Id: 'stage-lens-mat',
        Title: 'Surface Material & Studio Lens',
        Glyph: 'Palette',
        Badge: 'ACES FILMIC'
    });

    SelectRow(LensSec.Inner, {
        Label: 'Viewport shading / diagnostic lens',
        Options: SHADING_LENSES,
        Read: () => App.stage.lens,
        Write: V =>
        {
            App.stage.lens = String(V);
            document.getElementById('lens-select').value = App.stage.lens;
        }
    });

    const ChipsWrap = Construct('div', 'material-chips');
    for (const [Key, Spec] of Object.entries(MATERIAL_PRESETS))
    {
        const Dot = Construct('span', 'mat-dot');
        Dot.style.background = Spec.albedoHex;
        const Chip = Construct('button', 'mat-chip' + (App.material.preset === Key ? ' active' : ''), [
            Dot,
            Construct('span', null, Spec.label)
        ]);
        Chip.type = 'button';
        Chip.addEventListener('click', () =>
        {
            ApplyMaterialPreset(Key, true);
            RebuildInspector();
        });
        ChipsWrap.append(Chip);
    }
    LensSec.Inner.append(ChipsWrap);

    SliderRow(LensSec.Inner, {
        Label: 'Microfacet roughness',
        Minimum: 0.06, Maximum: 1.00, Step: 0.01, Digits: 2, Unit: 'α',
        Read: () => App.material.roughness,
        Write: V => { App.material.roughness = V; }
    });
    SliderRow(LensSec.Inner, {
        Label: 'Metallic reflectance',
        Minimum: 0.00, Maximum: 1.00, Step: 0.01, Digits: 2, Unit: 'F0',
        Read: () => App.material.metallic,
        Write: V => { App.material.metallic = V; }
    });
    SliderRow(LensSec.Inner, {
        Label: 'Clay subsurface wrap',
        Minimum: 0.00, Maximum: 1.00, Step: 0.01, Digits: 2, Unit: 'SSS',
        Read: () => App.material.subsurface,
        Write: V => { App.material.subsurface = V; }
    });
    SliderRow(LensSec.Inner, {
        Label: 'Mean curvature cavity accent',
        Minimum: 0.00, Maximum: 1.00, Step: 0.01, Digits: 2, Unit: '×',
        Read: () => App.material.cavity,
        Write: V => { App.material.cavity = V; }
    });

    const LightSec = SectionCard(Host, {
        Id: 'sun-rig',
        Title: 'Key Sun & Stage Calibration',
        Glyph: 'Sun',
        Badge: 'SOFT SHADOWS'
    });
    PolarDial(LightSec.Inner, {
        Label: 'Sun Azimuth',
        Read: () => App.stage.sunAzimuth,
        Write: Deg => { App.stage.sunAzimuth = Deg; },
        Resolve: () => ({ Caption: `Elevation ${App.stage.sunElevation.toFixed(0)}° · 28-step penumbra` })
    });
    SliderRow(LightSec.Inner, {
        Label: 'Sun elevation angle',
        Minimum: 8, Maximum: 85, Step: 1, Digits: 0, Unit: 'deg',
        Read: () => App.stage.sunElevation,
        Write: V => { App.stage.sunElevation = V; }
    });
    SliderRow(LightSec.Inner, {
        Label: '3D ground plane height',
        Minimum: -1.9, Maximum: 0.0, Step: 0.02, Digits: 2, Unit: 'm',
        Read: () => App.stage.groundY,
        Write: V => { App.stage.groundY = V; }
    });
}

function BuildExportInspectorTab(Host)
{
    if (!App.meshPreview.lastStats)
    {
        App.meshPreview.lastStats = ExtractSurfaceNetsMesh(App.volume, App.csgNodes, {
            resolution: App.meshPreview.resolution,
            isoLevel: App.meshPreview.isoLevel,
            volumeVisible: App.volumeVisible
        });
    }
    const Stats = App.meshPreview.lastStats;

    MetricTrio(Host, [
        {
            Label: 'Vertices',
            Resolve: () => [Stats.vertexCount.toLocaleString('en-US'), `${Stats.resolution}³ Dual Grid`]
        },
        {
            Label: 'Triangles',
            Resolve: () => [Stats.triangleCount.toLocaleString('en-US'), `${Stats.areaM2.toFixed(2)} m² area`]
        },
        {
            Label: 'Solid volume',
            Highlight: true,
            Resolve: () => [`${Stats.volumeLiters.toFixed(1)} L`, 'CCW Manifold']
        }
    ]);

    const MeshSec = SectionCard(Host, {
        Id: 'surface-nets-mesher',
        Title: 'Dual Surface Nets Mesher',
        Glyph: 'Lattice',
        Badge: 'WATERTIGHT'
    });
    SegmentedRow(MeshSec.Inner, {
        Label: 'Mesh extraction grid',
        Options: [['48³ Draft', 48], ['64³ Clean', 64], ['84³ Dense', 84]],
        Read: () => App.meshPreview.resolution,
        Write: Res =>
        {
            App.meshPreview.resolution = Res;
            App.meshPreview.lastStats = ExtractSurfaceNetsMesh(App.volume, App.csgNodes, {
                resolution: Res, isoLevel: App.meshPreview.isoLevel, volumeVisible: App.volumeVisible
            });
            RebuildInspector();
        }
    });
    SliderRow(MeshSec.Inner, {
        Label: 'Iso-surface offset',
        Minimum: -0.08, Maximum: 0.08, Step: 0.005, Digits: 3, Unit: 'm',
        Read: () => App.meshPreview.isoLevel,
        Write: V => { App.meshPreview.isoLevel = V; }
    });

    ActionRow(MeshSec.Inner, [
        {
            Label: 'Refresh Mesh Metrics', Glyph: 'Reset',
            Act: () =>
            {
                App.meshPreview.lastStats = ExtractSurfaceNetsMesh(App.volume, App.csgNodes, {
                    resolution: App.meshPreview.resolution,
                    isoLevel: App.meshPreview.isoLevel,
                    volumeVisible: App.volumeVisible
                });
                RebuildInspector();
                Toast(`Extracted ${App.meshPreview.lastStats.triangleCount.toLocaleString()} triangles`);
            }
        }
    ]);

    ActionRow(Host, [
        { Label: '.sdfspace (SDF1)', Glyph: 'Download', Tone: 'accent', Act: () => DownloadExport('sdfspace') },
        { Label: 'Export .OBJ', Glyph: 'Download', Act: () => DownloadExport('obj') }
    ]);
    ActionRow(Host, [
        { Label: 'Export .STL', Glyph: 'Download', Act: () => DownloadExport('stl') },
        { Label: 'Export .TOML', Glyph: 'Download', Act: () => DownloadExport('toml') }
    ]);

    const TomlSec = SectionCard(Host, {
        Id: 'toml-spec-preview',
        Title: 'Scene Specification (.sdfsculpt.toml)',
        Glyph: 'Terminal',
        Badge: 'FRONTIER'
    });
    const Pre = Construct('pre', 'levelspace', SerializeSceneToml(App, App.telemetry));
    TomlSec.Inner.append(Pre);
}

/*==========================================================================================================================================
                                                 IMPORT / EXPORT & ACTIONS
==========================================================================================================================================*/
function ChangeVolumeResolution(NewRes)
{
    if (NewRes === App.volume.nx) return;
    App.volume.resampleResolution(NewRes);
    App.volumeRes = NewRes;
    App.volume.pushHistorySnapshot(`Resample ${NewRes}³`, App.csgNodes);
    document.getElementById('dock-res-select').value = String(NewRes);
    document.getElementById('live-pill-label').textContent = `SCULPT · ${NewRes}³`;
    RefreshTelemetry(true);
    SyncInspectorHeader();
    Toast(`Resampled Sculpt Volume to ${NewRes}³ (${(App.volume.voxelCount / 1000).toFixed(0)}k voxels)`);
    PushLog('VOLUME', `Trilinearly resampled Clay Volume to ${NewRes}³`, 'live');
}

function FitVolumeBoundsToClay()
{
    if (App.volumeLocked)
    {
        Toast('Unlock Clay Volume first before refitting bounds');
        return;
    }
    const NewSpan = App.volume.compactBoundsToClay(3.2, 0.45);
    RecordHistory(`Fit ${NewSpan.toFixed(1)}m`);
    RefreshTelemetry(true);
    Toast(`Compacted & centered domain to ${NewSpan.toFixed(1)}³ m`);
    PushLog('BOUNDS', `Compacted voxel domain around clay (${NewSpan.toFixed(1)}³ m, step ${(App.volume.voxelSize * 100).toFixed(2)} cm)`, 'live');
}

function BakeAllCsgIntoClay()
{
    if (App.volumeLocked)
    {
        Toast('Unlock Clay Volume first before baking CSG');
        return;
    }
    const Active = App.csgNodes.filter(N => N.enabled !== false);
    if (!Active.length)
    {
        Toast('No active CSG nodes to bake');
        return;
    }
    App.volume.bakeCsgNodes(Active, false);
    App.csgNodes = App.csgNodes.filter(N => N.enabled === false);
    App.selectedId = 'volume';
    RecordHistory(`Bake ${Active.length} CSG`);
    RefreshTelemetry(true);
    RebuildInspector();
    Toast(`Baked ${Active.length} CSG node(s) into Clay Volume`);
    PushLog('BAKE', `Committed ${Active.length} parametric CSG nodes into ${App.volume.nx}³ clay`, 'warm');
}

function TriggerBlobDownload(BytesOrString, Filename, MimeType)
{
    const BlobObj = new Blob([BytesOrString], { type: MimeType });
    const Url = URL.createObjectURL(BlobObj);
    const Link = document.createElement('a');
    Link.href = Url;
    Link.download = Filename;
    document.body.append(Link);
    Link.click();
    Link.remove();
    setTimeout(() => URL.revokeObjectURL(Url), 1500);
}

function SafeSlug()
{
    return (App.name || 'frontier-sdf-sculpt').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'sdf-sculpt';
}

function DownloadExport(Format)
{
    const Slug = SafeSlug();
    if (Format === 'sdfspace')
    {
        const Bytes = EncodeSdfSpaceBinary(App.volume, App.csgNodes, true);
        TriggerBlobDownload(Bytes, `${Slug}.sdfspace`, 'application/octet-stream');
        Toast(`Exported ${Slug}.sdfspace (${(Bytes.byteLength / 1024).toFixed(0)} KB SDF1 Float16)`);
        PushLog('EXPORT', `Wrote Frontier SDF1 binary ${Slug}.sdfspace (${App.volume.nx}³)`, 'live');
        return;
    }
    if (Format === 'obj' || Format === 'stl')
    {
        const Mesh = ExtractSurfaceNetsMesh(App.volume, App.csgNodes, {
            resolution: App.meshPreview.resolution,
            isoLevel: App.meshPreview.isoLevel,
            volumeVisible: App.volumeVisible
        });
        App.meshPreview.lastStats = Mesh;
        if (Format === 'obj')
        {
            const ObjText = ExportMeshObj(Mesh, App.name);
            TriggerBlobDownload(ObjText, `${Slug}.obj`, 'text/plain;charset=utf-8');
            Toast(`Exported ${Slug}.obj (${Mesh.triangleCount.toLocaleString()} tris)`);
            PushLog('EXPORT', `Exported Wavefront OBJ (${Mesh.vertexCount} verts, ${Mesh.triangleCount} tris)`, 'live');
        }
        else
        {
            const StlBytes = ExportMeshBinaryStl(Mesh, App.name);
            TriggerBlobDownload(StlBytes, `${Slug}.stl`, 'application/octet-stream');
            Toast(`Exported ${Slug}.stl (${Mesh.triangleCount.toLocaleString()} tris)`);
            PushLog('EXPORT', `Exported Binary STL (${(StlBytes.byteLength / 1024).toFixed(0)} KB)`, 'live');
        }
        return;
    }
    if (Format === 'json')
    {
        // Encode clay volume via SDF1 base64 + full CSG & material metadata
        const SdfBytes = EncodeSdfSpaceBinary(App.volume, [], false);
        let BinStr = '';
        const Chunk = 0x8000;
        for (let I = 0; I < SdfBytes.length; I += Chunk)
        {
            BinStr += String.fromCharCode(...SdfBytes.subarray(I, I + Chunk));
        }
        const Payload = JSON.stringify({
            format: 'frontier.sdfsculpt.v2',
            name: App.name,
            volumeVisible: App.volumeVisible,
            symmetry: App.symmetry,
            material: App.material,
            stage: App.stage,
            csgNodes: App.csgNodes,
            sdf1Base64: btoa(BinStr)
        }, null, 2);
        TriggerBlobDownload(Payload, `${Slug}.sdfsculpt.json`, 'application/json');
        Toast(`Exported project bundle ${Slug}.sdfsculpt.json`);
        return;
    }
    if (Format === 'toml')
    {
        const Toml = SerializeSceneToml(App, App.telemetry);
        TriggerBlobDownload(Toml, `${Slug}.sdfsculpt.toml`, 'text/plain;charset=utf-8');
        Toast(`Exported ${Slug}.sdfsculpt.toml`);
    }
}

async function HandleFileImport(File)
{
    if (!File) return;
    try
    {
        if (File.name.toLowerCase().endsWith('.json'))
        {
            const Text = await File.text();
            const Data = JSON.parse(Text);
            if (Data.sdf1Base64)
            {
                const Bin = atob(Data.sdf1Base64);
                const U8 = new Uint8Array(Bin.length);
                for (let I = 0; I < Bin.length; ++I) U8[I] = Bin.charCodeAt(I);
                DecodeSdfSpaceBinary(U8, App.volume);
            }
            App.name = Data.name || File.name.replace(/\.json$/i, '');
            App.csgNodes = (Data.csgNodes || []).map(N => MakeCsgNode(N));
            if (Data.symmetry) App.symmetry = { ...App.symmetry, ...Data.symmetry };
            if (Data.material) App.material = { ...App.material, ...Data.material };
            RecordHistory(`Import ${App.name}`);
            SyncSymmetryBar();
            RefreshTelemetry(true);
            RebuildInspector();
            Toast(`Imported project "${App.name}"`);
            PushLog('IMPORT', `Loaded scene bundle ${File.name}`, 'live');
        }
        else
        {
            const Buf = await File.arrayBuffer();
            const Info = DecodeSdfSpaceBinary(new Uint8Array(Buf), App.volume);
            App.name = File.name.replace(/\.sdfspace$/i, '');
            App.volumeRes = App.volume.nx;
            document.getElementById('dock-res-select').value = String(App.volume.nx);
            RecordHistory(`Import ${App.name}`);
            SelectEntity('volume', true);
            RefreshTelemetry(true);
            RebuildInspector();
            Toast(`Imported Frontier SDF1 (${Info.nx}×${Info.ny}×${Info.nz})`);
            PushLog('IMPORT', `Decoded SDF1 archive ${File.name} (${Info.voxelCount.toLocaleString()} voxels)`, 'live');
        }
    }
    catch (Err)
    {
        console.error(Err);
        Toast(`Import failed: ${Err.message}`);
        PushLog('ERROR', `Import error: ${Err.message}`, 'warm');
    }
}

/*==========================================================================================================================================
                                               UI BINDINGS, MODALS & RENDER LOOP
==========================================================================================================================================*/
function SyncQuickDock()
{
    const RRange = document.getElementById('dock-radius-range');
    const RVal = document.getElementById('dock-radius-val');
    const SRange = document.getElementById('dock-strength-range');
    const SVal = document.getElementById('dock-strength-val');

    if (RRange)
    {
        RRange.value = String(App.brush.radius);
        const FracR = Clamp((App.brush.radius - 0.04) / (1.0 - 0.04), 0, 1);
        RRange.style.setProperty('--fraction', FracR.toFixed(3));
        RVal.textContent = `${App.brush.radius.toFixed(2)} m`;
    }
    if (SRange)
    {
        SRange.value = String(App.brush.strength);
        const FracS = Clamp((App.brush.strength - 0.05) / (1.0 - 0.05), 0, 1);
        SRange.style.setProperty('--fraction', FracS.toFixed(3));
        SVal.textContent = `${Math.round(App.brush.strength * 100)}%`;
    }
    document.getElementById('live-pill-label').textContent = `SCULPT · ${App.volume.nx}³ · ${App.volume.span.toFixed(1)}m`;
    const BoundsSel = document.getElementById('dock-bounds-select');
    if (BoundsSel) BoundsSel.value = App.volume.boundsMode;
}

function SyncSymmetryBar()
{
    document.getElementById('sym-x-btn').classList.toggle('active', !!App.symmetry.x);
    document.getElementById('sym-y-btn').classList.toggle('active', !!App.symmetry.y);
    document.getElementById('sym-z-btn').classList.toggle('active', !!App.symmetry.z);
    const RadBtn = document.getElementById('sym-rad-btn');
    RadBtn.textContent = `R${App.symmetry.radial || 1}×`;
    RadBtn.classList.toggle('active', (App.symmetry.radial | 0) >= 2);
}

function SyncStatusBar()
{
    const VoxEl = document.getElementById('status-voxels');
    if (VoxEl)
    {
        VoxEl.textContent = `${App.volume.nx}³ · ${App.volume.span.toFixed(1)}³ m domain (${(App.telemetry?.memoryMB ?? 7.08).toFixed(2)} MB RGBA16F)`;
    }
    const Hud = document.getElementById('ViewportHud');
    if (Hud)
    {
        Hud.innerHTML =
            `<span class="hud-pill">CLAY<b>${(App.telemetry?.liters ?? 0).toFixed(1)} L</b></span>` +
            `<span class="hud-pill">CSG<b>${App.csgNodes.filter(N => N.enabled !== false).length}/${24}</b></span>` +
            `<span class="hud-pill">DAB<b>${App.lastStrokeMs.toFixed(2)} ms</b></span>` +
            `<span class="hud-pill">${Math.round(App.fps)}<b>fps</b></span>`;
    }
}

function OpenLibraryModal()
{
    document.getElementById('library-veil').classList.add('shown');
    document.getElementById('nav-library').classList.add('active');
    document.getElementById('nav-workspace').classList.remove('active');
}

function CloseLibraryModal()
{
    document.getElementById('library-veil').classList.remove('shown');
    document.getElementById('nav-library').classList.remove('active');
    document.getElementById('nav-workspace').classList.add('active');
}

function WireStaticChrome()
{
    // Populate static SVG icons so there are zero raw Unicode hacks
    const PutIcon = (Id, Name) => { const El = document.getElementById(Id); if (El) El.append(ConstructGlyph(Name)); };
    PutIcon('undo-btn', 'Reset');
    PutIcon('redo-btn', 'Redo');
    PutIcon('help-button', 'Help');
    PutIcon('compact-outliner', 'Compact');
    PutIcon('add-button', 'Plus');
    PutIcon('stat-active-icon', 'Check');
    PutIcon('stat-cutter-icon', 'Zap');
    PutIcon('search-icon-slot', 'Search');
    PutIcon('preset-search-icon-slot', 'Search');
    PutIcon('modal-search-icon', 'Search');
    PutIcon('viewport-title-icon', 'Monitor');
    PutIcon('toggle-bounds-btn', 'Boxes');
    PutIcon('toggle-gizmo-btn', 'Move');
    PutIcon('toggle-grid-btn', 'Grid');
    PutIcon('toggle-minimap-btn', 'Slice');
    PutIcon('toggle-turntable-btn', 'Orbit');
    PutIcon('toggle-log-btn', 'Terminal');
    PutIcon('focus-btn', 'Focus');
    PutIcon('maximize-btn', 'Maximize');
    PutIcon('close-library-btn', 'Close');
    PutIcon('close-export-btn', 'Close');
    PutIcon('close-help-btn', 'Close');

    // Lens dropdown
    const LensSelect = document.getElementById('lens-select');
    for (const [Label, Val] of SHADING_LENSES)
    {
        const Opt = Construct('option', null, Label);
        Opt.value = Val;
        LensSelect.append(Opt);
    }
    LensSelect.addEventListener('change', () => { App.stage.lens = LensSelect.value; Broadcast(); });

    // Camera view preset dropdown
    document.getElementById('camera-view-select').addEventListener('change', E =>
    {
        App.viewport.setViewPreset(E.target.value);
    });

    // Symmetry pills
    document.getElementById('sym-x-btn').addEventListener('click', () => { App.symmetry.x = !App.symmetry.x; SyncSymmetryBar(); Broadcast(); });
    document.getElementById('sym-y-btn').addEventListener('click', () => { App.symmetry.y = !App.symmetry.y; SyncSymmetryBar(); Broadcast(); });
    document.getElementById('sym-z-btn').addEventListener('click', () => { App.symmetry.z = !App.symmetry.z; SyncSymmetryBar(); Broadcast(); });
    document.getElementById('sym-rad-btn').addEventListener('click', () =>
    {
        const Cycle = [1, 4, 6, 8];
        const Next = Cycle[(Cycle.indexOf(App.symmetry.radial) + 1) % Cycle.length];
        App.symmetry.radial = Next;
        SyncSymmetryBar();
        Broadcast();
        Toast(Next > 1 ? `Radial symmetry: ${Next}× around Y` : 'Radial symmetry off');
    });

    // Stage bar toggles
    document.getElementById('toggle-slice-btn').addEventListener('click', E =>
    {
        App.stage.sliceEnabled = !App.stage.sliceEnabled;
        E.currentTarget.classList.toggle('active', App.stage.sliceEnabled);
        Broadcast();
    });
    document.getElementById('toggle-bounds-btn').addEventListener('click', E =>
    {
        App.stage.showBoundsCage = !App.stage.showBoundsCage;
        E.currentTarget.classList.toggle('active', App.stage.showBoundsCage);
        Broadcast();
    });
    document.getElementById('toggle-gizmo-btn').addEventListener('click', E =>
    {
        App.stage.showGizmo = !App.stage.showGizmo;
        E.currentTarget.classList.toggle('active', App.stage.showGizmo);
    });
    document.getElementById('toggle-grid-btn').addEventListener('click', E =>
    {
        App.stage.showGrid = !App.stage.showGrid;
        E.currentTarget.classList.toggle('active', App.stage.showGrid);
        Broadcast();
    });
    document.getElementById('toggle-minimap-btn').addEventListener('click', E =>
    {
        App.stage.showMinimap = !App.stage.showMinimap;
        E.currentTarget.classList.toggle('active', App.stage.showMinimap);
        document.getElementById('MinimapInset').classList.toggle('shown', App.stage.showMinimap);
    });
    document.getElementById('toggle-turntable-btn').addEventListener('click', E =>
    {
        App.viewport.camera.turntable = !App.viewport.camera.turntable;
        E.currentTarget.classList.toggle('active', App.viewport.camera.turntable);
    });
    const ToggleLog = () =>
    {
        const Drawer = document.getElementById('log-drawer');
        Drawer.hidden = !Drawer.hidden;
        document.getElementById('toggle-log-btn').classList.toggle('active', !Drawer.hidden);
        document.getElementById('nav-log').classList.toggle('active', !Drawer.hidden);
    };
    document.getElementById('toggle-log-btn').addEventListener('click', ToggleLog);
    document.getElementById('nav-log').addEventListener('click', ToggleLog);
    document.getElementById('clear-log-btn').addEventListener('click', () => { App.logs = []; RenderLogStream(); document.getElementById('nav-log-count').textContent = '0'; });

    document.getElementById('focus-btn').addEventListener('click', () =>
    {
        const Node = GetSelectedCsgNode();
        const VolCenter = [
            0.5 * (App.volume.min[0] + App.volume.max[0]),
            0.5 * (App.volume.min[1] + App.volume.max[1]),
            0.5 * (App.volume.min[2] + App.volume.max[2])
        ];
        App.viewport.camera.target = Node ? Node.position.slice() : VolCenter;
        App.viewport.camera.distance = Node ? 3.15 : Math.max(3.15, App.volume.span * 0.78);
        Toast('Camera framed on selection');
    });
    document.getElementById('maximize-btn').addEventListener('click', E =>
    {
        const Ws = document.getElementById('studio-workspace');
        Ws.classList.toggle('maximized');
        E.currentTarget.classList.toggle('active', Ws.classList.contains('maximized'));
    });

    // Header actions
    document.getElementById('new-scene-btn').addEventListener('click', CreateBlankClayScene);
    document.getElementById('undo-btn').addEventListener('click', PerformUndo);
    document.getElementById('redo-btn').addEventListener('click', PerformRedo);

    const FileInput = document.getElementById('import-file-input');
    document.getElementById('import-btn').addEventListener('click', () => FileInput.click());
    FileInput.addEventListener('change', () => { if (FileInput.files?.[0]) HandleFileImport(FileInput.files[0]); FileInput.value = ''; });

    // Library modal
    document.getElementById('nav-workspace').addEventListener('click', CloseLibraryModal);
    document.getElementById('nav-library').addEventListener('click', OpenLibraryModal);
    document.getElementById('open-full-library').addEventListener('click', OpenLibraryModal);
    document.getElementById('close-library-btn').addEventListener('click', CloseLibraryModal);
    document.getElementById('library-veil').addEventListener('click', E => { if (E.target.id === 'library-veil') CloseLibraryModal(); });

    // Outliner controls & Add Popover
    document.getElementById('compact-outliner').addEventListener('click', E =>
    {
        const Left = document.getElementById('left-panel');
        const On = Left.classList.toggle('compact');
        E.currentTarget.classList.toggle('active', On);
        E.currentTarget.setAttribute('aria-pressed', String(On));
    });

    const Popover = document.getElementById('add-popover');
    Popover.append(Construct('div', 'popover-label', 'ADD ANALYTIC CSG OR BLANK'));
    for (const PresetId of ['prim-sphere', 'prim-rbox', 'prim-capsule', 'prim-torus', 'cut-scoop', 'cut-panel', 'kit-gyroid', 'blank-sphere'])
    {
        const Item = KITBASH_LIBRARY.find(K => K.id === PresetId);
        if (!Item) continue;
        const Btn = Construct('button', null, [
            Construct('span', null, Item.name),
            Construct('small', null, `${Item.category} · ${Item.badge}`)
        ]);
        Btn.type = 'button';
        Btn.addEventListener('click', () => { Popover.hidden = true; SpawnLibraryItem(Item); });
        Popover.append(Btn);
    }

    document.getElementById('add-button').addEventListener('click', E =>
    {
        E.stopPropagation();
        const Rect = E.currentTarget.getBoundingClientRect();
        Popover.style.left = `${Math.min(window.innerWidth - 268, Rect.left)}px`;
        Popover.style.top = `${Rect.bottom + 6}px`;
        Popover.hidden = !Popover.hidden;
    });
    document.addEventListener('click', E =>
    {
        if (!Popover.hidden && !Popover.contains(E.target) && E.target.id !== 'add-button') Popover.hidden = true;
    });

    // Searches & filters
    document.getElementById('scene-search').addEventListener('input', E => { App.outlinerQuery = E.target.value; RenderOutliner(); });
    document.querySelectorAll('#scene-filters button').forEach(Btn =>
    {
        Btn.addEventListener('click', () =>
        {
            App.outlinerFilter = Btn.dataset.filter;
            document.querySelectorAll('#scene-filters button').forEach(B => B.classList.toggle('active', B === Btn));
            RenderOutliner();
        });
    });

    const SyncLibSearch = Val =>
    {
        App.libraryQuery = Val;
        document.getElementById('quick-preset-search').value = Val;
        document.getElementById('modal-library-search').value = Val;
        RenderKitbashLibrary();
    };
    document.getElementById('quick-preset-search').addEventListener('input', E => SyncLibSearch(E.target.value));
    document.getElementById('modal-library-search').addEventListener('input', E => SyncLibSearch(E.target.value));

    // Lower Quick Dock controls
    document.getElementById('dock-radius-range').addEventListener('input', E =>
    {
        App.brush.radius = Number(E.target.value);
        App.brushHover.radius = App.brush.radius;
        SyncQuickDock();
        Broadcast();
    });
    document.getElementById('dock-strength-range').addEventListener('input', E =>
    {
        App.brush.strength = Number(E.target.value);
        SyncQuickDock();
        Broadcast();
    });
    document.getElementById('dock-res-select').addEventListener('change', E => ChangeVolumeResolution(Number(E.target.value)));
    document.getElementById('dock-bounds-select').addEventListener('change', E =>
    {
        App.volume.boundsMode = E.target.value;
        if (App.volume.boundsMode !== 'hard')
        {
            App.volume.applySoftBoundaryCushion();
            App.volume.markAllDirty();
        }
        SyncQuickDock();
        Broadcast();
        Toast(App.volume.boundsMode === 'expand'
            ? 'Bounds: Auto-Expand (grows up to 12³ m)'
            : App.volume.boundsMode === 'soft'
            ? 'Bounds: Soft Cushion Cap (rounded closed wall)'
            : 'Bounds: Hard Clip');
    });
    document.getElementById('dock-fit-bounds-btn').addEventListener('click', FitVolumeBoundsToClay);
    document.getElementById('dock-eikonal-btn').addEventListener('click', () =>
    {
        App.volume.normalizeEikonal(3);
        RecordHistory('Normalize |∇d|');
        RefreshTelemetry(true);
        Toast('Normalized SDF Eikonal gradient |∇d| → 1.0');
    });
    document.getElementById('dock-bake-csg-btn').addEventListener('click', BakeAllCsgIntoClay);

    // Inspector header bindings
    document.getElementById('reset-selection-btn').addEventListener('click', () =>
    {
        LoadStarterScene(STARTER_TABS.some(T => T.key === App.activeSceneKey) ? App.activeSceneKey : 'sculpt-bust');
        Toast('Scene reset to starter configuration');
    });
    document.getElementById('object-name').addEventListener('change', E =>
    {
        const Val = E.target.value.trim() || 'SDF Entity';
        const Node = GetSelectedCsgNode();
        if (Node) Node.name = Val;
        else App.volumeName = Val;
        RenderOutliner();
    });
    document.getElementById('object-enabled').addEventListener('change', E =>
    {
        const Node = GetSelectedCsgNode();
        if (Node) Node.enabled = E.target.checked;
        else App.volumeVisible = E.target.checked;
        App.dirtySliceMinimap = true;
        RenderOutliner();
        Broadcast();
    });
    document.querySelectorAll('#inspector-tabs button').forEach(Btn =>
    {
        Btn.addEventListener('click', () => SetInspectorTab(Btn.dataset.itab));
    });

    // Export & Help Dialogs
    const ExportDlg = document.getElementById('export-dialog');
    document.getElementById('export-button').addEventListener('click', () =>
    {
        document.getElementById('export-toml-preview').textContent = SerializeSceneToml(App, App.telemetry);
        ExportDlg.showModal();
    });
    document.getElementById('close-export-btn').addEventListener('click', () => ExportDlg.close());
    document.getElementById('dl-sdfspace-btn').addEventListener('click', () => DownloadExport('sdfspace'));
    document.getElementById('dl-obj-btn').addEventListener('click', () => DownloadExport('obj'));
    document.getElementById('dl-stl-btn').addEventListener('click', () => DownloadExport('stl'));
    document.getElementById('dl-json-btn').addEventListener('click', () => DownloadExport('json'));
    document.getElementById('dl-toml-btn').addEventListener('click', () => DownloadExport('toml'));
    document.getElementById('copy-toml-btn').addEventListener('click', async () =>
    {
        await navigator.clipboard?.writeText(SerializeSceneToml(App, App.telemetry));
        Toast('Copied .sdfsculpt.toml to clipboard');
    });

    const HelpDlg = document.getElementById('help-dialog');
    document.getElementById('help-button').addEventListener('click', () => HelpDlg.showModal());
    document.getElementById('close-help-btn').addEventListener('click', () => HelpDlg.close());
    document.getElementById('ok-help-btn').addEventListener('click', () => HelpDlg.close());

    // Keyboard shortcuts
    window.addEventListener('keydown', E =>
    {
        if (E.target.matches('input, select, textarea')) return;
        const Key = E.key.toLowerCase();

        if ((E.ctrlKey || E.metaKey) && Key === 'z')
        {
            E.preventDefault();
            if (E.shiftKey) PerformRedo();
            else PerformUndo();
            return;
        }
        if ((E.ctrlKey || E.metaKey) && Key === 'y')
        {
            E.preventDefault();
            PerformRedo();
            return;
        }
        if ((E.ctrlKey || E.metaKey) && Key === 'f')
        {
            E.preventDefault();
            document.getElementById('scene-search').focus();
            return;
        }

        const BrushByKey = SCULPT_BRUSHES.find(B => B.key === E.key);
        if (BrushByKey) { SelectTool(BrushByKey.id); return; }

        if (Key === 'v') { SelectTool('transform'); return; }
        if (Key === 'g') { document.getElementById('toggle-gizmo-btn').click(); return; }
        if (Key === 'm') { document.getElementById('toggle-minimap-btn').click(); return; }
        if (Key === 'x') { document.getElementById('sym-x-btn').click(); return; }
        if (Key === 'b') { BakeAllCsgIntoClay(); return; }
        if (Key === 'f') { document.getElementById('focus-btn').click(); return; }
        if (E.key === '[')
        {
            App.brush.radius = Clamp(App.brush.radius - 0.03, 0.04, 1.10);
            App.brushHover.radius = App.brush.radius;
            SyncQuickDock();
            Broadcast();
        }
        if (E.key === ']')
        {
            App.brush.radius = Clamp(App.brush.radius + 0.03, 0.04, 1.10);
            App.brushHover.radius = App.brush.radius;
            SyncQuickDock();
            Broadcast();
        }
        if (E.key === 'Tab')
        {
            E.preventDefault();
            document.getElementById('maximize-btn').click();
        }
    });

    SmoothScroll(document.getElementById('OutlinerScroll'), document.getElementById('OutlinerContent'));
    SmoothScroll(document.getElementById('InspectorScroll'), document.getElementById('InspectorContent'));
}

/*==========================================================================================================================================
                                                             BOOTSTRAP
==========================================================================================================================================*/
function Boot()
{
    WireStaticChrome();

    App.viewport = new SdfRaymarchViewport(
        document.getElementById('ViewportCanvas'),
        document.getElementById('OverlayCanvas'),
        document.getElementById('SliceCanvas')
    );

    const Pill = document.getElementById('engine-pill');
    const PillText = document.getElementById('engine-pill-text');
    const BackendText = document.getElementById('status-backend');
    if (App.viewport.backend === 'webgl2')
    {
        PillText.textContent = 'WEBGL2 · RAYMARCH 3D';
        BackendText.textContent = 'WebGL2 RGBA16F + Analytic CSG';
    }
    else
    {
        Pill.classList.add('software');
        PillText.textContent = 'PREVIEW · SOFTWARE 3D';
        BackendText.textContent = 'Software 3D Fallback';
    }

    RenderViewportToolRail();
    RenderKitbashLibrary();
    SetupViewportInteractions();
    LoadStarterScene('sculpt-bust');

    // Real-time 60fps animation & raymarch loop
    let LastTime = performance.now();
    let FrameCounter = 0;
    const Frame = Now =>
    {
        const Dt = Math.min(0.1, (Now - LastTime) / 1000);
        LastTime = Now;
        App.fps = App.fps * 0.92 + (1 / Math.max(1e-4, Dt)) * 0.08;

        if (App.viewport.camera.turntable && !App.gesture)
        {
            App.viewport.camera.yaw += Dt * 0.42;
        }

        App.viewport.render(App.volume, {
            volumeVisible: App.volumeVisible,
            csgNodes: App.csgNodes,
            selectedCsgNode: GetSelectedCsgNode(),
            showGizmo: App.stage.showGizmo,
            showBoundsCage: App.stage.showBoundsCage,
            activeGizmoAxis: App.gesture?.mode === 'gizmo' ? App.gesture.handle.axis : null,
            sunAzimuth: App.stage.sunAzimuth,
            sunElevation: App.stage.sunElevation,
            roughness: App.material.roughness,
            metallic: App.material.metallic,
            subsurface: App.material.subsurface,
            cavity: App.material.cavity,
            lens: App.stage.lens,
            showGrid: App.stage.showGrid,
            groundY: App.stage.groundY,
            sliceEnabled: App.stage.sliceEnabled,
            sliceAxis: App.stage.sliceAxis,
            sliceOffset: App.stage.sliceOffset,
            brushHover: App.brushHover,
            symmetry: App.symmetry
        });

        if (App.stage.showMinimap && (App.dirtySliceMinimap || (FrameCounter % 24 === 0)))
        {
            App.dirtySliceMinimap = false;
            const AxisNames = ['ZY', 'XZ', 'XY'];
            const CoordNames = ['x', 'y', 'z'];
            const Ax = App.stage.sliceAxis ?? 2;
            const Off = App.brushHover.active ? App.brushHover.pos[Ax] : App.stage.sliceOffset;
            App.viewport.renderSliceMinimap(App.volume, App.csgNodes, Ax, Off, App.volumeVisible);
            const InsetLbl = document.getElementById('SliceInsetLabel');
            if (InsetLbl)
            {
                InsetLbl.innerHTML = `<span>SDF Slice · ${AxisNames[Ax]}</span><span>${CoordNames[Ax]} = ${Off.toFixed(2)} m</span>`;
            }
        }

        if (FrameCounter % 15 === 0) SyncStatusBar();
        FrameCounter++;
        requestAnimationFrame(Frame);
    };
    requestAnimationFrame(Frame);
}

Boot();
