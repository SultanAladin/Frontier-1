/*==========================================================================================================================================
                                                      TERRAINWORLDEDITOR.JS
==========================================================================================================================================*/
// Main Studio Controller for Frontier SDF Terrain Studio (styled 1:1 with 3D Clay Studio):
// - Infinite Sparse-Tile 2D World Mask + Independent Toggleable Terrain Bounds Boxes
// - Calibrated Non-Blinding Dark-Studio 3D Ocean & Landscape Renderer (ACES Tonemapped)
// - First-Class 3D Spline Landforms + Interactive 3D XYZ (Up/Down Y-Axis) Transform Gizmo
// - Space-Efficient Thin-Slab SDF Architecture + Composable Layer Stack + Per-Layer Boundary Falloffs

import { WorldMask2D } from './Engine/WorldMask2D.js';
import {
    BOUNDARY_FALLOFF_MODES,
    COMBINE_MODES,
    LAYER_TYPE_META,
    CreateTerrainLayer,
    EvaluateTerrainRegion,
    StitchAdjacentRegionBorders,
    ExportWorldTerrainsObj,
    ExportWorldTerrainsBinaryStl,
    SeedThreeDemoTerrains,
    Clamp
} from './Engine/TerrainLayerStack.js';
import { WorldMap2DCanvas } from './Engine/WorldMap2DCanvas.js';
import { TerrainOceanViewport } from './Engine/TerrainOceanViewport.js';
import {
    CreateClimateGroup, CreateErosionState, StepErosionDay, BakeErosionYears,
    ApplyErosionToRegion, ErosionStateSummary, SampleClimate, AdvanceClimateDay,
    CLIMATE_REGIMES, EROSION_DEFAULTS
} from './Engine/ErosionPhysicsEngine.js';
import {
    EROSION_PRESETS, GetErosionPreset, BuildPresetWorld,
    CreateSimulationGroup, InitSimulationGroup, StepSimulationGroupDay,
    BakeSimulationGroup, ApplySimulationGroup
} from './Engine/ErosionPresets.js';
import {
    DATA_MAP_CATALOG, ComputeDataMaps, STAMP_CATALOG, SampleStamp,
    SATMAP_RAMPS, ComposeSurfaceColors, ExportMapPngBytes, ExportSurfacePngBytes,
    ExportMapRawBytes
} from './Engine/GaeaTexturingAndStamps.js';
import {
    ConstructGlyph,
    MakeEl,
    MakePropertyGroup,
    MakeSliderPillRow,
    MakeSelectRow,
    MakeBoundaryFalloffGraph
} from './StudioControls.js';
import {
    FRACTURE_PRESETS,
    BASE_FORM_SHAPES_POLY,
    BASE_FORM_SHAPES_BASALT,
    getFracturePreset,
    createDefaultFractureParams,
    buildFracturedFormation,
    bakeFracturedFormationQefMesh
} from './Engine/RockCliff/PureSdfFractureEngine.js';
import {
    exportFractureMeshObj,
    exportFractureMeshBinaryStl,
    exportFractureMeshGlb
} from './Engine/RockCliff/Core/QefDualContour.js';
import { FractureLabViewport } from './Engine/RockCliff/FractureLabViewport.js';
import {
    ROCK2_COMBINE_MODES,
    ROCK2_STONE_TONES,
    ROCK2_NODE_CATALOG,
    ROCK2_STARTER_STACKS,
    getRock2NodeSpec,
    createRock2Generator,
    createRock2Layer,
    cloneRock2Layer,
    createDefaultRock2Document,
    evaluateRock2GeneratorInstances,
    compileRock2Stack,
    bakeRock2StackQefMesh,
    serializeRock2Document,
    deserializeRock2Document
} from './Engine/RockCliff/RockFormationStackEngine.js';
import { RockFormation2Viewport } from './Engine/RockCliff/RockFormation2Viewport.js';

const MASK_PALETTE = ['#58b982', '#5ac8fa', '#a3d977', '#e8c27a', '#f0935b', '#d6a2e8'];

const SPLINE_MODES = [
    ['3D Elevation Ramp (Cut & Fill — Gizmo Y ↕)', 'grade'],
    ['3D SDF Rock Bridge / Overhang Arch', 'arch3d'],
    ['3D Mountain Tunnel Bore (Arched Roof + Flat Floor)', 'tunnel'],
    ['Mountain Crest Uplift (+Height)', 'ridge'],
    ['River Valley (3D Graded Channel)', 'river'],
    ['Canyon Gorge (Deep Carve)', 'canyon'],
    ['Hydrostatic Lake Basin', 'lake']
];

const AUTOSAVE_DB_NAME = 'frontier-terrain-world-studio-db';
const AUTOSAVE_STORE = 'workspaces';
const AUTOSAVE_KEY = 'autosave-v1';

class TerrainWorldStudioApp
{
    constructor()
    {
        this.world = new WorldMask2D();
        SeedThreeDemoTerrains(this.world);

        const UrlParams = new URLSearchParams(window.location.search);
        const ReqWs = UrlParams.get('ws') || UrlParams.get('mode');
        this.workspaceMode = (ReqWs === '2d' || ReqWs === '3d' || ReqWs === 'split' || ReqWs === 'fracture' || ReqWs === 'rock2') ? ReqWs : 'rock2';
        this.inspectorTab = this.workspaceMode === 'rock2' ? 'rock2' : (this.workspaceMode === 'fracture' ? 'fracture' : 'layers');
        this.outlinerFilter = 'all';  // 'all' | 'terrains' | 'splines' | 'masks'
        this.outlinerSearch = '';

        // ---- Pure-SDF Brittle Fracture & Thermal Cracking Lab State (Rock Formation 1.0) ----
        this.fracture = {
            params: createDefaultFractureParams('cracked-monolith'),
            formation: null,
            qefMesh: null,
            qefStats: null,
            qefDirty: true,
            _qefTimer: null,
            savedOpenState: null
        };

        // ---- RockFormation 2.0 Composable Pure-SDF Plain Rock Layer Stack State ----
        const InitRock2Stack = createDefaultRock2Document('stack-cliff-outcrop');
        this.rock2 = {
            stack: InitRock2Stack,
            selectedLayerId: InitRock2Stack.layers[0]?.id || null,
            compiled: null,
            qefMesh: null,
            qefStats: null,
            qefDirty: true,
            meshResolution: InitRock2Stack.meshResolution || 72,
            _qefTimer: null,
            undoStack: [],
            redoStack: []
        };

        // ---- Physically based erosion simulation state (solo OR shared-weather group) ----
        this.sim = {
            group: null,              // SimulationGroup (shared weather across 2+ terrains)
            playing: false,
            stepRate: 4,              // simulated days per real second while playing
            accumulator: 0,
            presetId: 'icelandic-highlands',
            climateRegimeId: 'alpine-continental',
            latitudeDeg: 46,
            startDay: 120,
            bakeYears: 2,
            mapLensId: null,          // active Gaea data-map lens
            rampId: 'satelliteDesert',
            stampId: 'crossBeddedSandstone',
            stampStrength: 1.0,
            shareWeather: false       // false = solo (1-by-1) · true = shared climate group
        };

        this.activeRegionId = this.world.regions[0]?.id || null;
        this.selectedRegionIds = new Set(this.activeRegionId ? [this.activeRegionId] : []);
        // Select the Spline layer by default on Terrain 01 so the 3D Gizmo is immediately visible & ready!
        const InitReg = this.world.regions[0];
        const InitSpline = InitReg?.stack.find(L => L.type === 'spline');
        this.activeLayerId = InitSpline?.id || InitReg?.stack[0]?.id || null;

        this.undoStack = [];
        this.redoStack = [];
        this._evalTimer = null;
        this._autosaveTimer = null;
        this.partitionTileSizeM = 1100;

        // 1. Initialize Infinite 2D World Mask Canvas
        this.map2D = new WorldMap2DCanvas(document.getElementById('Canvas2D'), this.world, {
            onCursorMove: (Wx, Wz) =>
            {
                const El = document.getElementById('Hud2DCoords');
                if (El) El.textContent = `${(Wx / 1000).toFixed(2)}, ${(Wz / 1000).toFixed(2)} km`;
            },
            onBeforeMaskEdit: () => this.pushUndoSnapshot(),
            onMaskLivePaint: () => this._scheduleDirtyRegionsEvaluation(),
            onMaskStrokeFinished: () =>
            {
                this._evaluateDirtyRegionsNow();
                this.refreshChrome();
            },
            onRegionSelectionChanged: (PrimaryId, SelectedArr) =>
            {
                this._setRegionSelection(PrimaryId, SelectedArr);
            },
            onRegionBoundsEdited: (Region, Finished) =>
            {
                if (Finished)
                {
                    EvaluateTerrainRegion(this.world, Region, this.activeLayerId);
                    Region.meshDirty = true;
                    this.map2D.invalidateRegionPreview(Region.id);
                    this.refreshChrome();
                    this.rebuildInspector();
                }
                else
                {
                    this._scheduleDirtyRegionsEvaluation();
                }
            },
            onCreateRegionFromBox: Bounds =>
            {
                this.pushUndoSnapshot();
                this._createNewRegionWithBounds(Bounds);
            },
            onDoubleClickRegion: Region =>
            {
                this._setRegionSelection(Region.id, [Region.id]);
                if (this.workspaceMode === '2d') this.setWorkspaceMode('split');
                this.view3D.frameRegion(Region);
            },
            onLayerStackChanged: (_Region, LayerId) =>
            {
                this.activeLayerId = LayerId;
                this.view3D.activeSplineLayerId = LayerId;
                this.refreshChrome();
                this.rebuildInspector();
            },
            onSplineEdited: (Region, Layer, Finished, KnotIdx) =>
            {
                this.activeLayerId = Layer.id;
                this.view3D.activeSplineLayerId = Layer.id;
                if (Number.isInteger(KnotIdx)) this.view3D.selectedKnotIndex = KnotIdx;
                EvaluateTerrainRegion(this.world, Region, this.activeLayerId);
                Region.meshDirty = true;
                this.map2D.invalidateRegionPreview(Region.id);
                if (Finished)
                {
                    this.refreshChrome();
                    this.rebuildInspector();
                }
            }
        });

        // 2. Initialize Calibrated Dark-Studio 3D Ocean + Spline Gizmo Viewport
        this.view3D = new TerrainOceanViewport(
            document.getElementById('Canvas3D'),
            document.getElementById('Overlay3D'),
            this.world,
            {
                onPickRegionIn3D: (Region, MultiToggle) =>
                {
                    const NextSet = new Set(this.selectedRegionIds);
                    if (MultiToggle)
                    {
                        if (NextSet.has(Region.id) && NextSet.size > 1) NextSet.delete(Region.id);
                        else NextSet.add(Region.id);
                    }
                    else
                    {
                        NextSet.clear();
                        NextSet.add(Region.id);
                    }
                    const Primary = NextSet.has(Region.id) ? Region.id : Array.from(NextSet)[0];
                    this._setRegionSelection(Primary, Array.from(NextSet));
                },
                onSplineKnotSelected: (_Region, Layer, KnotIndex) =>
                {
                    this.activeLayerId = Layer.id;
                    this.view3D.activeSplineLayerId = Layer.id;
                    this.view3D.selectedKnotIndex = KnotIndex;
                    this.inspectorTab = 'spline';
                    this._syncInspectorTabButtons();
                    this.refreshChrome();
                    this.rebuildInspector();
                },
                onBeforeSplineGizmoEdit: () => this.pushUndoSnapshot(),
                onSplineGizmoLive: (Region, Layer) =>
                {
                    this.activeLayerId = Layer.id;
                    EvaluateTerrainRegion(this.world, Region, this.activeLayerId);
                    Region.meshDirty = true;
                    this.map2D.invalidateRegionPreview(Region.id);
                    this.map2D.requestRedraw();
                    // Live-update Y slider readout if open!
                    if (this._activeKnotYSlider)
                    {
                        const K = Layer.points[this.view3D.selectedKnotIndex];
                        if (K && Number.isFinite(K[2])) this._activeKnotYSlider.SetValue(K[2]);
                    }
                },
                onSplineGizmoFinished: (Region, Layer) =>
                {
                    EvaluateTerrainRegion(this.world, Region, Layer.id);
                    Region.meshDirty = true;
                    this.map2D.invalidateRegionPreview(Region.id);
                    this.refreshChrome();
                    this.rebuildInspector();
                },
                onSplineKnotAdded3D: (Region, Layer, KnotIdx) =>
                {
                    this.activeLayerId = Layer.id;
                    this.view3D.activeSplineLayerId = Layer.id;
                    this.view3D.selectedKnotIndex = KnotIdx;
                    EvaluateTerrainRegion(this.world, Region, Layer.id);
                    Region.meshDirty = true;
                    this.map2D.invalidateRegionPreview(Region.id);
                    this.inspectorTab = 'spline';
                    this._syncInspectorTabButtons();
                    this.refreshChrome();
                    this.rebuildInspector();
                },
                onBeforeSculpt3D: () => this.pushUndoSnapshot(),
                onSculptLive3D: Region =>
                {
                    if (Region.dirty) EvaluateTerrainRegion(this.world, Region, this.activeLayerId);
                    this.map2D.invalidateRegionPreview(Region.id);
                },
                onSculptStrokeFinished3D: Region =>
                {
                    EvaluateTerrainRegion(this.world, Region, this.activeLayerId);
                    Region.meshDirty = true;
                    this.map2D.invalidateRegionPreview(Region.id);
                    this.refreshChrome();
                    this.rebuildInspector();
                }
            }
        );

        if (InitSpline) this.view3D.activeSplineLayerId = InitSpline.id;

        // 3. Initialize Pure-SDF Brittle Fracture & QEF Dual-Contour 3D Lab Viewport
        const FracCanvas = document.getElementById('CanvasFracture');
        if (FracCanvas)
        {
            try
            {
                this.fractureView = new FractureLabViewport(FracCanvas);
                this._rebuildFractureFormationNow(true, true);
                this._bindFractureViewportToolbar();
            }
            catch (Err)
            {
                console.error('[FractureLab] Viewport init failed, falling back gracefully:', Err);
                this.fractureView = null;
            }
        }

        // 4. Initialize RockFormation 2.0 Composable Pure-SDF Plain Rock Layer Stack Viewport
        const Rock2Canvas = document.getElementById('CanvasRock2');
        if (Rock2Canvas)
        {
            try
            {
                this.rock2View = new RockFormation2Viewport(Rock2Canvas);
                this._rebuildRock2StackNow(true);
                this._bindRock2ViewportToolbar();
            }
            catch (Err)
            {
                console.error('[RockFormation 2.0] Viewport init failed, falling back gracefully:', Err);
                this.rock2View = null;
            }
        }

        this._mountStaticGlyphs();
        this._buildViewportToolRails();
        this._buildQuickAddLibrary();
        this._buildPresetStrip();
        this._bindHeaderAndTransport();
        this._populateMapLensOptions();
        this.setWorkspaceMode(this.workspaceMode);
        this._syncInspectorTabButtons();
        this.refreshChrome();
        this.rebuildInspector();

        requestAnimationFrame(() =>
        {
            this.map2D.frameAllRegions();
            this.view3D.frameVisibleWorld();
            this.fractureView?.frameFormation();
            this.rock2View?.frameStack();
            this.refreshChrome();
        });

        // Restore persisted workspace from IndexedDB / localStorage unless `?example` was requested
        if (!window.location.search.includes('example'))
        {
            this._restoreAutosaveIfAvailable();
        }

        this._startMainLoop();
    }

    /*======================================================================================================================================
                                                STATIC GLYPHS, TOOL RAILS & QUICK LIBRARY
    ======================================================================================================================================*/
    _mountStaticGlyphs()
    {
        const Mount = (Id, Glyph) =>
        {
            const El = document.getElementById(Id);
            if (El) { El.innerHTML = ''; El.appendChild(ConstructGlyph(Glyph)); }
        };
        Mount('undo-btn', 'Reset');
        Mount('redo-btn', 'Redo');
        Mount('compact-outliner', 'Layers');
        Mount('add-button', 'Plus');
        Mount('stat-active-icon', 'Mountain');
        Mount('stat-spline-icon', 'Spline');
        Mount('search-icon-slot', 'Search');
        Mount('viewport-title-icon', 'Planet');
        Mount('toggle-gizmo-btn', 'Gizmo');
        Mount('toggle-bounds-btn', 'Scan');
        Mount('toggle-contours-btn', 'Activity');
        Mount('focus-btn', 'Focus');
        Mount('maximize-btn', 'Maximize');
        Mount('object-symbol', 'Mountain');
    }

    _buildViewportToolRails()
    {
        // 2D World Mask & Bounds Floating Left Tool Rail
        const Rail2D = document.getElementById('Tools2DRail');
        Rail2D.innerHTML = '';
        this.btns2D = new Map();
        const Spec2D = [
            ['brush',   'Brush',   'Brush 2D Landmass (B)', 'B'],
            ['erase',   'Eraser',  'Erase Bay / Fjord (E)', 'E'],
            ['polygon', 'Polygon', 'Polygon Continent (P)', 'P'],
            ['sep'],
            ['bounds',  'Scan',    'Draw New Terrain Bounds Box (R)', 'R'],
            ['select',  'Move',    'Select / Move / 8-Handle Resize Bounds (V)', 'V'],
            ['spline',  'Spline',  'Edit Spline Knots in 2D Map (S)', 'S']
        ];
        for (const Item of Spec2D)
        {
            if (Item[0] === 'sep') { Rail2D.appendChild(MakeEl('div', 'tool-rail-sep')); continue; }
            const [Id, Glyph, Tip, Key] = Item;
            const Btn = MakeEl('button', 'tool-rail-btn' + (this.map2D.tool === Id ? ' active' : ''));
            Btn.type = 'button';
            Btn.title = Tip;
            Btn.append(ConstructGlyph(Glyph), MakeEl('small', '', Key));
            Btn.addEventListener('click', () => this.select2DTool(Id));
            this.btns2D.set(Id, Btn);
            Rail2D.appendChild(Btn);
        }

        // 3D Landscape, Spline Gizmo & Sculpt Floating Left Tool Rail
        const Rail3D = document.getElementById('Tools3DRail');
        Rail3D.innerHTML = '';
        this.btns3D = new Map();
        const Spec3D = [
            ['spline',     'Gizmo',     'Spline + 3D XYZ (Up/Down Y ↕) Gizmo', 'G'],
            ['spline-add', 'Plus',      'Click 3D Terrain to Place Spline Knots (K)', 'K'],
            ['navigate',   'Move',      'Orbit & Select Terrains', 'N'],
            ['sep'],
            ['raise',      'ArrowUp',   '3D SDF Raise Terrain Brush', ''],
            ['lower',      'ArrowDown', '3D SDF Carve / Lower Brush', ''],
            ['smooth',     'Sliders',   '3D SDF Smooth Relief Brush', ''],
            ['flatten',    'Layers',    '3D SDF Terrace / Flatten Brush', ''],
            ['overhang',   'Arch',      '3D SDF Volumetric Overhang / Sea Arch', ''],
            ['cave',       'Eraser',    '3D SDF Volumetric Cave / Tunnel Carve', ''],
            ['restore',    'Reset',     'Restore / Erase Sculpt Deltas to Base Landform', '']
        ];
        for (const Item of Spec3D)
        {
            if (Item[0] === 'sep') { Rail3D.appendChild(MakeEl('div', 'tool-rail-sep')); continue; }
            const [Id, Glyph, Tip, Key] = Item;
            const Btn = MakeEl('button', 'tool-rail-btn' + (this.view3D.tool3D === Id ? ' active' : ''));
            Btn.type = 'button';
            Btn.title = Tip;
            Btn.append(ConstructGlyph(Glyph), Key ? MakeEl('small', '', Key) : null);
            Btn.addEventListener('click', () => this.select3DTool(Id));
            this.btns3D.set(Id, Btn);
            Rail3D.appendChild(Btn);
        }
    }

    _buildQuickAddLibrary()
    {
        const List = document.getElementById('quick-add-list');
        if (!List) return;
        List.innerHTML = '';

        const TitleEl = document.getElementById('quick-lib-title');
        const CountEl = document.getElementById('quick-lib-count');

        if (this.workspaceMode === 'rock2')
        {
            const Q = (this.outlinerSearch || '').toLowerCase();
            const Items = ROCK2_NODE_CATALOG.filter(N => !Q || N.name.toLowerCase().includes(Q) || N.shortBadge.toLowerCase().includes(Q) || N.desc.toLowerCase().includes(Q));
            if (TitleEl) TitleEl.textContent = 'Add Basic Rock Node';
            if (CountEl) CountEl.textContent = String(Items.length);

            for (let I = 0; I < Items.length; I++)
            {
                const Node = Items[I];
                const Btn = MakeEl('button', 'quick-add-card' + (I === 0 ? ' featured-spline' : ''));
                Btn.type = 'button';
                Btn.title = `Add plain ${Node.name} SDF layer to the Layer Stack`;
                const Icon = MakeEl('span', 'quick-add-icon', ConstructGlyph(Node.glyph));
                const Copy = MakeEl('span', 'quick-add-copy', [
                    MakeEl('strong', '', `+ ${Node.name}`),
                    MakeEl('small', '', Node.desc)
                ]);
                const Badge = MakeEl('span', 'preset-badge', Node.shortBadge);
                Btn.append(Icon, Copy, Badge);
                Btn.addEventListener('click', () => this._addRock2Layer(Node.type));
                List.appendChild(Btn);
            }
            return;
        }

        if (TitleEl) TitleEl.textContent = 'Add Layer & Splines';

        const QuickItems = [
            {
                title: '+ 3D Graded Spline (Gizmo)',
                desc: 'Ramp, ridge, road or pass — drag Green Y ↕ arrow up/down',
                glyph: 'Gizmo',
                badge: '3D GIZMO',
                featured: true,
                action: () => this.addNew3DSplineLayer('grade', '3D Graded Spline Ramp')
            },
            {
                title: '+ 3D Rock Arch Bridge Spline',
                desc: 'True 3D overhanging rock arch swept along 3D knots',
                glyph: 'Arch',
                badge: '3D SDF',
                featured: false,
                action: () => this.addNew3DSplineLayer('arch3d', '3D Rock Arch Bridge')
            },
            {
                title: '+ 3D Mountain Tunnel Bore',
                desc: 'Arched roof + flat highway floor carved through mountains',
                glyph: 'Arch',
                badge: 'TUNNEL',
                featured: false,
                action: () => this.addNew3DSplineLayer('tunnel', '3D Mountain Tunnel Bore')
            },
            {
                title: '+ River / Canyon Spline',
                desc: 'Carve 3D graded river valley or gorge channel',
                glyph: 'Spline',
                badge: 'SPLINE',
                featured: false,
                action: () => this.addNew3DSplineLayer('river', '3D River Valley Spline')
            },
            {
                title: '+ Hybrid Multifractal',
                desc: 'Alluvial valleys + craggy continental peaks',
                glyph: 'Mountain',
                badge: 'FRACTAL',
                featured: false,
                action: () => this.addProceduralLayer('multifractal')
            },
            {
                title: '+ Ridged Mountain Spine',
                desc: 'Sharp tectonic arête crests & directional chains',
                glyph: 'Activity',
                badge: 'RIDGE',
                featured: false,
                action: () => this.addProceduralLayer('ridged')
            },
            {
                title: '+ Voronoi Cliff Mesa',
                desc: 'Stepped basalt blocks, craters, or fracture ridges',
                glyph: 'Boxes',
                badge: 'CELLULAR',
                featured: false,
                action: () => this.addProceduralLayer('voronoi')
            },
            {
                title: '+ Stratigraphic Terraces',
                desc: 'Geological dipping benches & canyon strata',
                glyph: 'Layers',
                badge: 'STRATA',
                featured: false,
                action: () => this.addProceduralLayer('terrace')
            }
        ];
        if (CountEl) CountEl.textContent = String(QuickItems.length);

        for (const Item of QuickItems)
        {
            const Btn = MakeEl('button', 'quick-add-card' + (Item.featured ? ' featured-spline' : ''));
            Btn.type = 'button';
            const Icon = MakeEl('span', 'quick-add-icon', ConstructGlyph(Item.glyph));
            const Copy = MakeEl('span', 'quick-add-copy', [
                MakeEl('strong', '', Item.title),
                MakeEl('small', '', Item.desc)
            ]);
            const Badge = MakeEl('span', 'preset-badge', Item.badge);
            Btn.append(Icon, Copy, Badge);
            Btn.addEventListener('click', Item.action);
            List.appendChild(Btn);
        }
    }

    _buildPresetStrip()
    {
        const Strip = document.getElementById('preset-strip');
        if (!Strip) return;
        Strip.innerHTML = '';

        const HeadingEl = document.getElementById('timeline-heading');
        const CounterEl = document.getElementById('stroke-counter');
        if (this.workspaceMode === 'rock2')
        {
            if (HeadingEl) HeadingEl.textContent = 'STARTER STACKS';
            if (CounterEl) CounterEl.textContent = `${ROCK2_STARTER_STACKS.length} PLAIN STACKS`;
            const ActiveId = this.rock2?.stack?.starterId || 'stack-cliff-outcrop';
            for (const S of ROCK2_STARTER_STACKS)
            {
                const Chip = MakeEl('button', 'history-chip' + (S.id === ActiveId ? ' current' : ''), S.name);
                Chip.type = 'button';
                Chip.title = `${S.name} (${S.badge})`;
                Chip.addEventListener('click', () => this._selectRock2StarterStack(S.id));
                Strip.appendChild(Chip);
            }
            return;
        }

        if (this.workspaceMode === 'fracture')
        {
            if (HeadingEl) HeadingEl.textContent = 'FORMATIONS';
            if (CounterEl) CounterEl.textContent = `${FRACTURE_PRESETS.length} ROCK FORMATIONS`;
            const ActiveId = this.fracture?.params?.presetId || 'cracked-monolith';
            for (const P of FRACTURE_PRESETS)
            {
                const Chip = MakeEl('button', 'history-chip' + (P.id === ActiveId ? ' current' : ''), P.name);
                Chip.type = 'button';
                Chip.addEventListener('click', () => this._selectFracturePreset(P.id));
                Strip.appendChild(Chip);
            }
            return;
        }

        if (HeadingEl) HeadingEl.textContent = 'WORLD';
        if (CounterEl) CounterEl.textContent = '3 DEMO MASKS';
        const Chips = [
            ['All 3 Demo Terrains', () => { this.view3D.viewScope = 'all'; document.getElementById('scope-select').value = 'all'; this.view3D.frameVisibleWorld(); this.refreshChrome(); }],
            ['Solo: Highland Massif (Soft Falloff)', () => this._focusSingleRegion(0)],
            ['Solo: Basalt Cliffs (No Falloff · Sheer Cliff)', () => this._focusSingleRegion(1)],
            ['Solo: Caldera Atoll (Rim Ring Falloff)', () => this._focusSingleRegion(2)],
            ['Inspect Thin SDF Slab (Ocean OFF)', () => { this.world.oceanVisible = false; this._syncOceanUI(); }],
            ['Export .STL Binary', () => this._exportStl()]
        ];

        for (let I = 0; I < Chips.length; ++I)
        {
            const [Label, Fn] = Chips[I];
            const Chip = MakeEl('button', 'history-chip' + (I === 0 ? ' current' : ''), Label);
            Chip.type = 'button';
            Chip.addEventListener('click', () =>
            {
                for (const C of Strip.querySelectorAll('.history-chip')) C.classList.remove('current');
                Chip.classList.add('current');
                Fn();
            });
            Strip.appendChild(Chip);
        }
    }

    _focusSingleRegion(Index)
    {
        const Reg = this.world.regions[Index];
        if (!Reg) return;
        Reg.enabled = true;
        this._setRegionSelection(Reg.id, [Reg.id]);
        this.view3D.viewScope = 'solo';
        const Sel = document.getElementById('scope-select');
        if (Sel) Sel.value = 'solo';
        this.map2D.frameBounds(Reg.bounds, 1.32);
        this.view3D.frameRegion(Reg);
        this.refreshChrome();
    }

    /*======================================================================================================================================
                                       FIRST-CLASS 3D SPLINE LAYER CREATION & GIZMO ACTIVATION
    ======================================================================================================================================*/
    addNew3DSplineLayer(SplineMode = 'grade', CustomName = '3D Graded Spline Landform')
    {
        const Reg = this.getActiveRegion();
        if (!Reg) return;
        this.pushUndoSnapshot();

        const B = Reg.bounds;
        const SampleH = (Wx, Wz) => this.view3D.sampleTerrainSurfaceY(Reg, Wx, Wz);

        const P0x = Math.round(B.x + B.width * 0.28), P0z = Math.round(B.z + B.depth * 0.34);
        const P1x = Math.round(B.x + B.width * 0.50), P1z = Math.round(B.z + B.depth * 0.50);
        const P2x = Math.round(B.x + B.width * 0.72), P2z = Math.round(B.z + B.depth * 0.66);

        // Give the middle knot a clear initial +42m uplift so the 3D spline & Y-Gizmo effect is immediately dramatic!
        const MidBoost = SplineMode === 'arch3d' ? 68.0 : (SplineMode === 'canyon' || SplineMode === 'river' ? -18.0 : 42.0);
        const NewSpline = CreateTerrainLayer('spline', {
            name: CustomName,
            splineMode: SplineMode,
            width: SplineMode === 'arch3d' ? 64.0 : 110.0,
            amount: SplineMode === 'arch3d' ? 28.0 : 34.0,
            bankSmooth: 0.62,
            meander: SplineMode === 'river' ? 0.22 : 0.08,
            falloff: { mode: 'none', distance: 120.0, edgeFloor: 1.0 },
            points: [
                [P0x, P0z, Math.round((SampleH(P0x, P0z) + 6.0) * 10) / 10],
                [P1x, P1z, Math.round((SampleH(P1x, P1z) + MidBoost) * 10) / 10],
                [P2x, P2z, Math.round((SampleH(P2x, P2z) + 6.0) * 10) / 10]
            ]
        });

        Reg.stack.push(NewSpline);
        this.activeLayerId = NewSpline.id;
        this.view3D.activeSplineLayerId = NewSpline.id;
        this.view3D.selectedKnotIndex = 1; // Select middle knot so user can drag Green Y ↕ arrow immediately!
        this.view3D.showGizmo = true;
        this.select3DTool('spline');

        if (this.workspaceMode === '2d') this.setWorkspaceMode('split');

        EvaluateTerrainRegion(this.world, Reg, NewSpline.id);
        Reg.meshDirty = true;
        this.map2D.invalidateRegionPreview(Reg.id);

        this.inspectorTab = 'spline';
        this._syncInspectorTabButtons();
        this.refreshChrome();
        this.rebuildInspector();
    }

    addProceduralLayer(Type)
    {
        const Reg = this.getActiveRegion();
        if (!Reg) return;
        this.pushUndoSnapshot();
        const Lyr = CreateTerrainLayer(Type, {
            seed: Math.floor(1000 + Math.random() * 89000),
            falloff: { mode: Type === 'voronoi' ? 'none' : 'soft', distance: 280.0, edgeFloor: 0.0 }
        });
        Reg.stack.push(Lyr);
        this.activeLayerId = Lyr.id;
        EvaluateTerrainRegion(this.world, Reg, Lyr.id);
        Reg.meshDirty = true;
        this.map2D.invalidateRegionPreview(Reg.id);
        this.inspectorTab = 'layers';
        this._syncInspectorTabButtons();
        this.refreshChrome();
        this.rebuildInspector();
    }

    /*======================================================================================================================================
                                                SNAPSHOT UNDO / REDO & STATE HELPERS
    ======================================================================================================================================*/
    pushUndoSnapshot()
    {
        try
        {
            const Snap = JSON.stringify(this.world.toJSON());
            if (this.undoStack.length > 0 && this.undoStack[this.undoStack.length - 1] === Snap) return;
            this.undoStack.push(Snap);
            if (this.undoStack.length > 24) this.undoStack.shift();
            this.redoStack.length = 0;
            this._scheduleAutosave();
        }
        catch (_) { /* ignore */ }
    }

    _openAutosaveDb()
    {
        return new Promise((Resolve, Reject) =>
        {
            if (typeof indexedDB === 'undefined') return Reject(new Error('No IndexedDB'));
            const Req = indexedDB.open(AUTOSAVE_DB_NAME, 1);
            Req.onupgradeneeded = () =>
            {
                const Db = Req.result;
                if (!Db.objectStoreNames.contains(AUTOSAVE_STORE)) Db.createObjectStore(AUTOSAVE_STORE);
            };
            Req.onsuccess = () => Resolve(Req.result);
            Req.onerror = () => Reject(Req.error);
        });
    }

    _scheduleAutosave()
    {
        if (this._autosaveTimer) clearTimeout(this._autosaveTimer);
        this._autosaveTimer = setTimeout(async () =>
        {
            this._autosaveTimer = null;
            try
            {
                const Doc = this.world.toJSON();
                try
                {
                    const Db = await this._openAutosaveDb();
                    await new Promise((Res, Rej) =>
                    {
                        const Tx = Db.transaction(AUTOSAVE_STORE, 'readwrite');
                        Tx.objectStore(AUTOSAVE_STORE).put(Doc, AUTOSAVE_KEY);
                        Tx.oncomplete = () => Res();
                        Tx.onerror = () => Rej(Tx.error);
                    });
                    Db.close();
                }
                catch (_)
                {
                    localStorage?.setItem(AUTOSAVE_KEY, JSON.stringify(Doc));
                }
            }
            catch (_) { /* ignore storage quota errors */ }
        }, 650);
    }

    async _restoreAutosaveIfAvailable()
    {
        try
        {
            let Doc = null;
            try
            {
                const Db = await this._openAutosaveDb();
                Doc = await new Promise((Res, Rej) =>
                {
                    const Tx = Db.transaction(AUTOSAVE_STORE, 'readonly');
                    const Req = Tx.objectStore(AUTOSAVE_STORE).get(AUTOSAVE_KEY);
                    Req.onsuccess = () => Res(Req.result || null);
                    Req.onerror = () => Rej(Req.error);
                });
                Db.close();
            }
            catch (_)
            {
                const Raw = localStorage?.getItem(AUTOSAVE_KEY);
                if (Raw) Doc = JSON.parse(Raw);
            }
            if (Doc && Doc.format === 'frontier.terrain.world.v1' && Array.isArray(Doc.regions) && Doc.regions.length > 0)
            {
                this._restoreWorldData(Doc);
                this.map2D.frameAllRegions();
                this.view3D.frameVisibleWorld();
            }
        }
        catch (_) { /* ignore */ }
    }

    async _clearAutosave()
    {
        try
        {
            localStorage?.removeItem(AUTOSAVE_KEY);
            const Db = await this._openAutosaveDb();
            const Tx = Db.transaction(AUTOSAVE_STORE, 'readwrite');
            Tx.objectStore(AUTOSAVE_STORE).delete(AUTOSAVE_KEY);
            Db.close();
        }
        catch (_) { /* ignore */ }
    }

    undo()
    {
        if (this.workspaceMode === 'rock2')
        {
            if (!this.rock2.undoStack.length) return;
            this.rock2.redoStack.push(JSON.stringify(serializeRock2Document(this.rock2.stack)));
            this._restoreRock2StackData(JSON.parse(this.rock2.undoStack.pop()));
            return;
        }
        if (!this.undoStack.length) return;
        this.redoStack.push(JSON.stringify(this.world.toJSON()));
        this._restoreWorldData(JSON.parse(this.undoStack.pop()));
    }

    redo()
    {
        if (this.workspaceMode === 'rock2')
        {
            if (!this.rock2.redoStack.length) return;
            this.rock2.undoStack.push(JSON.stringify(serializeRock2Document(this.rock2.stack)));
            this._restoreRock2StackData(JSON.parse(this.rock2.redoStack.pop()));
            return;
        }
        if (!this.redoStack.length) return;
        this.undoStack.push(JSON.stringify(this.world.toJSON()));
        this._restoreWorldData(JSON.parse(this.redoStack.pop()));
    }

    _restoreWorldData(JsonObj)
    {
        this.world.loadFromJSON(JsonObj);
        for (const R of this.world.regions)
        {
            EvaluateTerrainRegion(this.world, R, this.activeLayerId);
            R.meshDirty = true;
        }
        const First = this.world.regions[0];
        this.activeRegionId = First?.id || null;
        this.selectedRegionIds = new Set(this.activeRegionId ? [this.activeRegionId] : []);
        const Sp = First?.stack.find(L => L.type === 'spline');
        this.activeLayerId = Sp?.id || First?.stack[0]?.id || null;
        this.view3D.activeSplineLayerId = Sp?.id || null;
        this.map2D.activeMaskLayerId = this.world.maskLayers[0]?.id || null;
        this.map2D.activeRegionId = this.activeRegionId;
        this.map2D.selectedRegionIds = new Set(this.selectedRegionIds);
        this.view3D.activeRegionId = this.activeRegionId;
        this.view3D.selectedRegionIds = new Set(this.selectedRegionIds);
        this.map2D.invalidateMaskCache();
        this._syncOceanUI();
        this.refreshChrome();
        this.rebuildInspector();
    }

    getActiveRegion()
    {
        return this.world.getRegion(this.activeRegionId) || this.world.regions[0] || null;
    }

    _scheduleDirtyRegionsEvaluation()
    {
        if (this._evalTimer) return;
        this._evalTimer = setTimeout(() =>
        {
            this._evalTimer = null;
            this._evaluateDirtyRegionsNow();
        }, 38);
    }

    _evaluateDirtyRegionsNow()
    {
        let AnyEvaluated = false;
        for (const R of this.world.regions)
        {
            if (R.dirty)
            {
                EvaluateTerrainRegion(this.world, R, R.id === this.activeRegionId ? this.activeLayerId : null);
                R.meshDirty = true;
                this.map2D.invalidateRegionPreview(R.id);
                AnyEvaluated = true;
            }
        }
        if (AnyEvaluated && this.world.regions.length >= 2)
        {
            StitchAdjacentRegionBorders(this.world);
        }
        if (AnyEvaluated) this._scheduleAutosave();
        this.map2D.requestRedraw();
    }

    _setRegionSelection(PrimaryId, SelectedIdsArray)
    {
        this.activeRegionId = PrimaryId;
        this.selectedRegionIds = new Set(SelectedIdsArray);
        this.map2D.activeRegionId = PrimaryId;
        this.map2D.selectedRegionIds = new Set(SelectedIdsArray);
        this.view3D.activeRegionId = PrimaryId;
        this.view3D.selectedRegionIds = new Set(SelectedIdsArray);

        const Reg = this.getActiveRegion();
        if (Reg)
        {
            if (Reg.maskLayerId) this.map2D.activeMaskLayerId = Reg.maskLayerId;
            if (!Reg.stack.some(L => L.id === this.activeLayerId))
            {
                const Sp = Reg.stack.find(L => L.type === 'spline');
                this.activeLayerId = Sp?.id || Reg.stack[Reg.stack.length - 1]?.id || null;
            }
            const ActiveSp = Reg.stack.find(L => L.id === this.activeLayerId && L.type === 'spline')
                          || Reg.stack.find(L => L.type === 'spline');
            this.view3D.activeSplineLayerId = ActiveSp?.id || null;
        }
        this.map2D.requestRedraw();
        this.refreshChrome();
        this.rebuildInspector();
    }

    _createNewRegionWithBounds(Bounds)
    {
        const Idx = this.world.regions.length + 1;
        const ActiveMask = this.world.getMaskLayer(this.map2D.activeMaskLayerId);
        // Automatically adopt whichever visible 2D Mask Layer actually has painted land inside Bounds!
        const AdoptedMaskId = this.world.findDominantMaskInBounds(Bounds, ActiveMask ? ActiveMask.id : null);
        const NewReg = this.world.addTerrainRegion({
            name: `Terrain ${String(Idx).padStart(2, '0')} · Custom`,
            maskLayerId: AdoptedMaskId,
            color: MASK_PALETTE[(Idx - 1) % MASK_PALETTE.length],
            bounds: Bounds,
            resolution: 136,
            baseElevation: 3.0,
            slabThickness: 6.5,
            shorelineBevel: 18.0,
            seamLock: true,
            stack: [
                CreateTerrainLayer('multifractal', {
                    name: 'Base Multifractal Landform',
                    amplitude: 165.0,
                    scale: 2.3,
                    falloff: { mode: 'soft', distance: Math.min(Bounds.width, Bounds.depth) * 0.24, edgeFloor: 0.02 }
                })
            ]
        });
        EvaluateTerrainRegion(this.world, NewReg);
        StitchAdjacentRegionBorders(this.world);
        NewReg.meshDirty = true;
        this._setRegionSelection(NewReg.id, [NewReg.id]);
        this._scheduleAutosave();
    }

    /*======================================================================================================================================
                                          WORKSPACE MODES, TOOLS & HEADER BINDINGS
    ======================================================================================================================================*/
    setWorkspaceMode(Mode)
    {
        const Prev = this.workspaceMode;
        this.workspaceMode = Mode;
        document.body.classList.toggle('ws-fracture', Mode === 'fracture');
        document.body.classList.toggle('ws-rock2', Mode === 'rock2');
        const Grid = document.getElementById('StageGrid');
        Grid.classList.remove('mode-2d', 'mode-3d', 'mode-split', 'mode-fracture', 'mode-rock2');
        Grid.classList.add(`mode-${Mode}`);

        for (const Btn of document.querySelectorAll('.top-nav .nav-button[data-ws]'))
        {
            Btn.classList.toggle('active', Btn.dataset.ws === Mode);
        }

        const ExportBtn = document.getElementById('export-button');
        if (ExportBtn)
        {
            ExportBtn.textContent = Mode === 'rock2' ? 'Export .GLB' : 'Export .OBJ';
            ExportBtn.title = Mode === 'rock2'
                ? 'Bake QEF Dual-Contour Mesh and export binary glTF 2.0 (.GLB)'
                : 'Export Visible SDF Mesh as Wavefront .OBJ Triangles';
        }

        const ResetBtn = document.getElementById('reset-demo-btn');
        if (ResetBtn)
        {
            ResetBtn.textContent = Mode === 'rock2' ? 'Reset Stack ↗' : '3 Demo Masks ↗';
            ResetBtn.title = Mode === 'rock2'
                ? 'Reset active starter Layer Stack to clean defaults'
                : 'Reset to the 3 Demonstration Masks & Terrains';
        }

        const AddBtn = document.getElementById('add-button');
        if (AddBtn)
        {
            AddBtn.title = Mode === 'rock2' ? 'Add a Plain Big Boulder Layer to Stack' : 'Quick-add a 3D Spline Landform Layer';
        }

        const ImpBtn = document.getElementById('import-btn');
        if (ImpBtn)
        {
            ImpBtn.title = Mode === 'rock2' ? 'Load RockFormation 2.0 Layer Stack (.json)' : 'Load Open-World Terrain .json project';
        }

        if (Mode === 'rock2')
        {
            this.inspectorTab = 'rock2';
            this._syncInspectorTabButtons();
        }
        else if (Mode === 'fracture')
        {
            this.inspectorTab = 'fracture';
            this._syncInspectorTabButtons();
        }
        else if ((Prev === 'fracture' || Prev === 'rock2') && (this.inspectorTab === 'fracture' || this.inspectorTab === 'rock2'))
        {
            this.inspectorTab = 'layers';
            this._syncInspectorTabButtons();
        }

        this._buildQuickAddLibrary();
        this._buildPresetStrip();
        if (this.map2D && this.view3D)
        {
            this.refreshChrome();
            this.rebuildInspector();
            requestAnimationFrame(() =>
            {
                this.map2D.requestRedraw();
                if (Mode === 'fracture') this.fractureView?.frameFormation();
                if (Mode === 'rock2') this.rock2View?.frameStack();
            });
        }
    }

    select2DTool(ToolId)
    {
        this.map2D.tool = ToolId;
        for (const [K, B] of this.btns2D.entries()) B.classList.toggle('active', K === ToolId);
        const Names = { brush: 'Brush Land', erase: 'Erase Bay', polygon: 'Polygon Continent', bounds: 'Draw Bounds Box', select: 'Resize Bounds', spline: '2D Spline Knots' };
        const Lbl = document.getElementById('hud-2d-tool');
        if (Lbl) Lbl.textContent = Names[ToolId] || ToolId;
        this.map2D.requestRedraw();
    }

    select3DTool(ToolId)
    {
        this.view3D.tool3D = ToolId;
        for (const [K, B] of this.btns3D.entries()) B.classList.toggle('active', K === ToolId);
        document.getElementById('pill-place-knot')?.classList.toggle('active', ToolId === 'spline-add');
        const Names = {
            spline: 'Spline + 3D Gizmo (Drag Y ↕)',
            'spline-add': '+ Click Terrain to Place Knot',
            navigate: 'Orbit & Select',
            raise: '3D Sculpt · Raise',
            lower: '3D Sculpt · Carve',
            smooth: '3D Sculpt · Smooth',
            flatten: '3D Sculpt · Terrace',
            overhang: '3D Sculpt · Overhang Arch',
            cave: '3D Sculpt · Cave / Tunnel Carve',
            restore: '3D Sculpt · Restore Base'
        };
        const Lbl = document.getElementById('hud-3d-tool');
        if (Lbl) Lbl.textContent = Names[ToolId] || ToolId;
    }

    _syncOceanUI()
    {
        const On = this.world.oceanVisible !== false;
        const Btn = document.getElementById('toggle-ocean-btn');
        if (Btn)
        {
            Btn.classList.toggle('active', On);
            Btn.textContent = On ? 'Ocean ON' : 'Ocean OFF (Slab)';
        }
    }

    _syncInspectorTabButtons()
    {
        for (const Btn of document.querySelectorAll('#inspector-tabs button'))
        {
            Btn.classList.toggle('active', Btn.dataset.itab === this.inspectorTab);
        }
    }

    _bindHeaderAndTransport()
    {
        // Workspace switcher
        for (const Btn of document.querySelectorAll('.top-nav .nav-button[data-ws]'))
        {
            Btn.addEventListener('click', () => this.setWorkspaceMode(Btn.dataset.ws));
        }

        // Inspector tabs
        for (const Btn of document.querySelectorAll('#inspector-tabs button'))
        {
            Btn.addEventListener('click', () =>
            {
                this.inspectorTab = Btn.dataset.itab;
                this._syncInspectorTabButtons();
                this.rebuildInspector();
            });
        }

        // Outliner search & category filters
        document.getElementById('scene-search')?.addEventListener('input', E =>
        {
            this.outlinerSearch = E.target.value.trim().toLowerCase();
            if (this.workspaceMode === 'rock2') this._buildQuickAddLibrary();
            this.refreshChrome();
        });
        for (const Btn of document.querySelectorAll('#scene-filters button'))
        {
            Btn.addEventListener('click', () =>
            {
                this.outlinerFilter = Btn.dataset.filter;
                for (const B of document.querySelectorAll('#scene-filters button')) B.classList.toggle('active', B === Btn);
                this.refreshChrome();
            });
        }

        document.getElementById('compact-outliner')?.addEventListener('click', E =>
        {
            document.getElementById('left-panel')?.classList.toggle('compact');
            E.currentTarget.classList.toggle('active');
        });

        // One-Click Add Layer buttons (in RockFormation 2.0 adds a Plain Rock Layer; in Terrain adds a 3D Spline)
        document.getElementById('add-button')?.addEventListener('click', () =>
        {
            if (this.workspaceMode === 'rock2')
            {
                this._addRock2Layer('big_boulder');
                return;
            }
            this.addNew3DSplineLayer('grade', '3D Graded Spline Ramp');
        });
        document.getElementById('pill-add-spline')?.addEventListener('click', () => this.addNew3DSplineLayer('grade', '3D Graded Spline Ramp'));
        document.getElementById('dock-new-spline-btn')?.addEventListener('click', () => this.addNew3DSplineLayer('grade', '3D Graded Spline Ramp'));

        document.getElementById('pill-place-knot')?.addEventListener('click', () =>
        {
            if (this.workspaceMode === '2d') this.setWorkspaceMode('split');
            this.select3DTool(this.view3D.tool3D === 'spline-add' ? 'spline' : 'spline-add');
        });

        document.getElementById('new-scene-btn')?.addEventListener('click', () =>
        {
            if (this.workspaceMode === '3d') this.setWorkspaceMode('split');
            this.select2DTool('bounds');
        });

        // Viewport top bar selects & toggles
        document.getElementById('scope-select')?.addEventListener('change', E =>
        {
            this.view3D.viewScope = E.target.value;
            this.refreshChrome();
        });
        document.getElementById('lens-select')?.addEventListener('change', E =>
        {
            const Val = E.target.value;
            if (Val.startsWith('map:'))
            {
                this.sim.mapLensId = Val.slice(4);
                this._applyMapLens();
            }
            else
            {
                this.sim.mapLensId = null;
                this._clearMapLens();
                this.view3D.lensMode = Number(Val);
            }
        });
        document.getElementById('toggle-ocean-btn')?.addEventListener('click', () =>
        {
            this.world.oceanVisible = !this.world.oceanVisible;
            this._syncOceanUI();
            this.rebuildInspector();
        });
        document.getElementById('toggle-gizmo-btn')?.addEventListener('click', E =>
        {
            this.view3D.showGizmo = !this.view3D.showGizmo;
            E.currentTarget.classList.toggle('active', this.view3D.showGizmo);
            this.rebuildInspector();
        });
        document.getElementById('toggle-bounds-btn')?.addEventListener('click', E =>
        {
            this.view3D.showBounds3D = !this.view3D.showBounds3D;
            E.currentTarget.classList.toggle('active', this.view3D.showBounds3D);
            this.rebuildInspector();
        });
        document.getElementById('toggle-contours-btn')?.addEventListener('click', E =>
        {
            this.view3D.showContours = !this.view3D.showContours;
            E.currentTarget.classList.toggle('active', this.view3D.showContours);
            this.rebuildInspector();
        });
        document.getElementById('focus-btn')?.addEventListener('click', () =>
        {
            if (this.workspaceMode === 'rock2')
            {
                this.rock2View?.frameStack();
                return;
            }
            if (this.workspaceMode === 'fracture')
            {
                this.fractureView?.frameFormation();
                return;
            }
            this.map2D.frameAllRegions();
            this.view3D.frameVisibleWorld();
        });
        document.getElementById('maximize-btn')?.addEventListener('click', E =>
        {
            document.getElementById('studio-workspace')?.classList.toggle('maximized');
            E.currentTarget.classList.toggle('active');
            requestAnimationFrame(() => this.map2D.requestRedraw());
        });

        // Object heading name & enabled switch
        document.getElementById('object-name')?.addEventListener('change', E =>
        {
            if (this.workspaceMode === 'rock2')
            {
                const Layer = this._getSelectedRock2Layer();
                if (Layer && E.target.value.trim())
                {
                    this._pushRock2UndoSnapshot();
                    Layer.name = E.target.value.trim();
                    this.refreshChrome();
                    this.rebuildInspector();
                }
                return;
            }
            if (this.workspaceMode === 'fracture') return;
            const Reg = this.getActiveRegion();
            if (Reg && E.target.value.trim())
            {
                Reg.name = E.target.value.trim();
                this.map2D.requestRedraw();
                this.refreshChrome();
            }
        });
        document.getElementById('object-enabled')?.addEventListener('change', E =>
        {
            if (this.workspaceMode === 'rock2')
            {
                const Layer = this._getSelectedRock2Layer();
                if (Layer)
                {
                    this._pushRock2UndoSnapshot();
                    Layer.enabled = E.target.checked;
                    this._rebuildRock2StackNow(false);
                    this.refreshChrome();
                    this.rebuildInspector();
                }
                return;
            }
            const Reg = this.getActiveRegion();
            if (Reg)
            {
                Reg.enabled = E.target.checked;
                this.map2D.requestRedraw();
                this.refreshChrome();
            }
        });

        // Reset 3 Demo Terrains (or Reset Starter Stack in RockFormation 2.0)
        document.getElementById('reset-demo-btn')?.addEventListener('click', () =>
        {
            if (this.workspaceMode === 'rock2')
            {
                this._selectRock2StarterStack(this.rock2?.stack?.starterId || 'stack-cliff-outcrop');
                return;
            }
            this.pushUndoSnapshot();
            this._clearAutosave();
            SeedThreeDemoTerrains(this.world);
            for (const R of this.world.regions) R.meshDirty = true;
            this.map2D.activeMaskLayerId = this.world.maskLayers[0]?.id || null;
            this._setRegionSelection(this.world.regions[0].id, [this.world.regions[0].id]);
            this.map2D.invalidateMaskCache();
            this._syncOceanUI();
            this.map2D.frameAllRegions();
            this.view3D.frameVisibleWorld();
        });

        // Lower Transport Sliders (Brush/Spline Width, Exposure, Sky Atmosphere, Fit Bounds, Save JSON)
        const RadRange = document.getElementById('dock-radius-range');
        const RadVal = document.getElementById('dock-radius-val');
        const SyncRadFill = V =>
        {
            RadRange.style.setProperty('--fill', `${(((V - 20) / 580) * 100).toFixed(1)}%`);
            RadVal.textContent = `${Math.round(V)} m`;
        };
        SyncRadFill(120);
        RadRange?.addEventListener('input', () =>
        {
            const V = parseFloat(RadRange.value);
            SyncRadFill(V);
            this.map2D.brushRadiusM = V;
            this.view3D.sculptRadiusM = V;
            this.map2D.requestRedraw();
        });

        const ExpRange = document.getElementById('dock-exposure-range');
        const ExpVal = document.getElementById('dock-exposure-val');
        const SyncExpFill = V =>
        {
            ExpRange.style.setProperty('--fill', `${(((V - 0.35) / 1.30) * 100).toFixed(1)}%`);
            ExpVal.textContent = V.toFixed(2);
        };
        SyncExpFill(this.view3D.exposure);
        ExpRange?.addEventListener('input', () =>
        {
            const V = parseFloat(ExpRange.value);
            SyncExpFill(V);
            this.view3D.exposure = V;
            if (this.fractureView) this.fractureView.exposure = V;
            if (this.rock2View) this.rock2View.exposure = V;
        });

        document.getElementById('dock-atmos-select')?.addEventListener('change', E =>
        {
            this.view3D.atmosphereMode = Number(E.target.value);
        });

        document.getElementById('dock-fit-mask-btn')?.addEventListener('click', () =>
        {
            const Reg = this.getActiveRegion();
            const Mask = Reg?.maskLayerId ? this.world.getMaskLayer(Reg.maskLayerId) : this.world.getMaskLayer(this.map2D.activeMaskLayerId);
            if (!Reg || !Mask) return;
            const Fitted = this.world.computeLayerPaintedBounds(Mask, 90.0);
            if (Fitted)
            {
                this.pushUndoSnapshot();
                Reg.bounds = Fitted;
                EvaluateTerrainRegion(this.world, Reg, this.activeLayerId);
                Reg.meshDirty = true;
                this.map2D.invalidateRegionPreview(Reg.id);
                this.refreshChrome();
                this.rebuildInspector();
            }
        });

        // Undo / Redo / Save / Load / Export
        document.getElementById('undo-btn')?.addEventListener('click', () => this.undo());
        document.getElementById('redo-btn')?.addEventListener('click', () => this.redo());
        document.getElementById('dock-save-json-btn')?.addEventListener('click', () =>
        {
            const Text = JSON.stringify(this.world.toJSON(), null, 2);
            this._triggerDownload(new Blob([Text], { type: 'application/json' }), 'frontier-terrain-world.json');
        });

        const FileInput = document.getElementById('FileLoadJson');
        document.getElementById('import-btn')?.addEventListener('click', () => FileInput?.click());
        FileInput?.addEventListener('change', async () =>
        {
            const F = FileInput.files?.[0];
            if (!F) return;
            try
            {
                const Parsed = JSON.parse(await F.text());
                if (Parsed && (Parsed.format === 'frontier-rockformation-2.0' || (this.workspaceMode === 'rock2' && Array.isArray(Parsed.layers))))
                {
                    this._pushRock2UndoSnapshot();
                    this._restoreRock2StackData(Parsed);
                    if (this.workspaceMode !== 'rock2') this.setWorkspaceMode('rock2');
                    this.rock2View?.frameStack();
                }
                else
                {
                    this.pushUndoSnapshot();
                    this._restoreWorldData(Parsed);
                    this.map2D.frameAllRegions();
                    this.view3D.frameVisibleWorld();
                }
            }
            catch (Err) { console.error(Err); }
            FileInput.value = '';
        });

        document.getElementById('export-button')?.addEventListener('click', () =>
        {
            if (this.workspaceMode === 'rock2')
            {
                this._exportRock2Asset('glb');
                return;
            }
            this._exportObj();
        });

        // Keyboard Shortcuts
        window.addEventListener('keydown', E =>
        {
            if (E.target && (E.target.tagName === 'INPUT' || E.target.tagName === 'TEXTAREA' || E.target.tagName === 'SELECT')) return;
            if (E.code === 'Space') this.map2D.spaceDown = true;

            if ((E.ctrlKey || E.metaKey) && E.key.toLowerCase() === 'z')
            {
                E.preventDefault();
                if (E.shiftKey) this.redo(); else this.undo();
                return;
            }
            if ((E.ctrlKey || E.metaKey) && E.key.toLowerCase() === 'y')
            {
                E.preventDefault();
                this.redo();
                return;
            }

            const K = E.key.toLowerCase();
            if (E.key === '1') this.setWorkspaceMode('2d');
            else if (E.key === '2') this.setWorkspaceMode('3d');
            else if (E.key === '3') this.setWorkspaceMode('split');
            else if (E.key === '4') this.setWorkspaceMode('fracture');
            else if (E.key === '5') this.setWorkspaceMode('rock2');
            else if (E.key === 'Tab')
            {
                E.preventDefault();
                document.getElementById('maximize-btn')?.click();
            }
            else if (K === 'g' && this.workspaceMode !== 'rock2') this.select3DTool('spline');
            else if (K === 'k' && this.workspaceMode !== 'rock2') this.select3DTool('spline-add');
            else if (K === 'n' && this.workspaceMode !== 'rock2') this.select3DTool('navigate');
            else if (K === 'b' && this.workspaceMode !== 'rock2') this.select2DTool('brush');
            else if (K === 'e' && this.workspaceMode !== 'rock2') this.select2DTool('erase');
            else if (K === 'p' && this.workspaceMode !== 'rock2') this.select2DTool('polygon');
            else if (K === 'r' && this.workspaceMode !== 'rock2') this.select2DTool('bounds');
            else if (K === 'v' && this.workspaceMode !== 'rock2') this.select2DTool('select');
            else if (K === 's' && !E.ctrlKey && this.workspaceMode !== 'rock2') this.select2DTool('spline');
            else if (K === 'w' && this.workspaceMode !== 'rock2') document.getElementById('toggle-ocean-btn')?.click();
            else if (K === 'f') document.getElementById('focus-btn')?.click();
            else if (E.key === 'Delete' || E.key === 'Backspace')
            {
                if (this.workspaceMode === 'rock2')
                {
                    if (this.rock2?.stack?.layers?.length > 1 && this.rock2.selectedLayerId)
                    {
                        this._deleteRock2Layer(this.rock2.selectedLayerId);
                    }
                    return;
                }
                // Delete selected 3D Spline Knot if > 2 knots remain
                const Ctx = this.view3D.getActiveSplineContext();
                if (Ctx && Ctx.layer.points.length > 2)
                {
                    this.pushUndoSnapshot();
                    Ctx.layer.points.splice(this.view3D.selectedKnotIndex, 1);
                    this.view3D.selectedKnotIndex = Math.max(0, this.view3D.selectedKnotIndex - 1);
                    EvaluateTerrainRegion(this.world, Ctx.region, Ctx.layer.id);
                    Ctx.region.meshDirty = true;
                    this.map2D.invalidateRegionPreview(Ctx.region.id);
                    this.rebuildInspector();
                }
            }
            else if (E.key === 'Enter' && this.map2D.tool === 'polygon')
            {
                this.map2D.commitPolygonDraft(E.shiftKey);
            }
            else if (E.key === 'Escape' && this.map2D.polygonDraft.length > 0)
            {
                this.map2D.polygonDraft = [];
                this.map2D.requestRedraw();
            }
        });
        window.addEventListener('keyup', E => { if (E.code === 'Space') this.map2D.spaceDown = false; });
    }

    _exportObj()
    {
        if (this.workspaceMode === 'rock2')
        {
            this._exportRock2Asset('obj');
            return;
        }
        if (this.workspaceMode === 'fracture')
        {
            this._exportFractureAsset('obj');
            return;
        }
        const Targets = this.view3D.getVisibleRegions();
        const { text } = ExportWorldTerrainsObj(this.world, Targets.length ? Targets : this.world.regions);
        this._triggerDownload(new Blob([text], { type: 'text/plain' }), 'frontier-sdf-terrains.obj');
    }

    _exportStl()
    {
        if (this.workspaceMode === 'rock2')
        {
            this._exportRock2Asset('stl');
            return;
        }
        if (this.workspaceMode === 'fracture')
        {
            this._exportFractureAsset('stl');
            return;
        }
        const Targets = this.view3D.getVisibleRegions();
        const { bytes } = ExportWorldTerrainsBinaryStl(this.world, Targets.length ? Targets : this.world.regions);
        this._triggerDownload(new Blob([bytes], { type: 'application/octet-stream' }), 'frontier-sdf-terrains.stl');
    }

    _triggerDownload(BlobObj, Filename)
    {
        const Url = URL.createObjectURL(BlobObj);
        const A = document.createElement('a');
        A.href = Url;
        A.download = Filename;
        document.body.appendChild(A);
        A.click();
        A.remove();
        setTimeout(() => URL.revokeObjectURL(Url), 2500);
    }

    /*======================================================================================================================================
                                       TRAPEZOIDAL TABS, OUTLINER TREE & TELEMETRY REFRESH
    ======================================================================================================================================*/
    refreshChrome()
    {
        const Reg = this.getActiveRegion();

        // 1. Slanted Trapezoidal Region Tabs in Header
        const TabsEl = document.getElementById('region-tabs');
        if (TabsEl)
        {
            TabsEl.innerHTML = '';
            for (const R of this.world.regions)
            {
                const Tab = MakeEl('button', 'document-tab' + (R.id === this.activeRegionId ? ' active' : ''));
                Tab.type = 'button';
                Tab.appendChild(MakeEl('span', 'document-label', R.name));
                Tab.addEventListener('click', E =>
                {
                    const Multi = E.shiftKey || E.ctrlKey || E.metaKey;
                    const Next = new Set(this.selectedRegionIds);
                    if (Multi)
                    {
                        if (Next.has(R.id) && Next.size > 1) Next.delete(R.id);
                        else Next.add(R.id);
                    }
                    else { Next.clear(); Next.add(R.id); }
                    if (this.workspaceMode === 'fracture' || this.workspaceMode === 'rock2') this.setWorkspaceMode('split');
                    this._setRegionSelection(R.id, Array.from(Next));
                });
                TabsEl.appendChild(Tab);
            }
        }

        // If RockFormation 2.0 mode is active, populate Left Panel with the Composable Plain-Rock Layer Stack!
        if (this.workspaceMode === 'rock2')
        {
            this._buildRock2Outliner();
            return;
        }

        // If Rock Formation 1.0 mode is active, populate Left Outliner with the 7 Rock Formations!
        if (this.workspaceMode === 'fracture')
        {
            this._buildFractureOutliner();
            return;
        }

        // Restore Terrain Studio labels when in 2D / 3D / Split modes
        const LeftTitle = document.getElementById('left-panel-title');
        if (LeftTitle) LeftTitle.textContent = 'Outliner';
        const StatLbl1 = document.getElementById('stat-label-1');
        const StatLbl2 = document.getElementById('stat-label-2');
        if (StatLbl1) StatLbl1.textContent = 'Terrains ON';
        if (StatLbl2) StatLbl2.textContent = 'Layers & Splines';
        const SearchInp = document.getElementById('scene-search');
        if (SearchInp) SearchInp.placeholder = 'Search terrains, masks, splines…';
        const LeftFoot = document.getElementById('left-footer-label');
        if (LeftFoot) LeftFoot.textContent = 'Infinite Sparse 2D Mask · Thin-Slab SDF';
        const StageTitle = document.getElementById('stage-bar-title');
        if (StageTitle) StageTitle.textContent = 'Stage';
        const InspFootL = document.getElementById('inspector-foot-left');
        const InspFootR = document.getElementById('inspector-foot-right');
        if (InspFootL) InspFootL.textContent = 'Thin-Slab SDF · Zero deep-ocean waste';
        if (InspFootR) InspFootR.textContent = 'Live triangle mesher ↗';

        // 2. Census Stats & Subtitle
        const OnCount = this.world.regions.filter(R => R.enabled).length;
        const TotalLayers = this.world.regions.reduce((Acc, R) => Acc + R.stack.length, 0);
        const TilesCount = this.world.totalAllocatedTiles();
        document.getElementById('stat-terrains-on').textContent = String(OnCount);
        document.getElementById('stat-layers-total').textContent = String(TotalLayers);
        document.getElementById('outliner-subtitle').textContent = `${this.world.regions.length} Terrains · ${TilesCount} Sparse Tiles`;

        // 3. Outliner Tree (Terrains + Nested Layer/Spline Stack + 2D Masks)
        const Outliner = document.getElementById('OutlinerContent');
        Outliner.innerHTML = '';
        const Q = this.outlinerSearch;

        if (this.outlinerFilter !== 'masks')
        {
            const Group = MakeEl('div', 'tree-group');
            Group.appendChild(MakeEl('div', 'tree-group-title', [
                ConstructGlyph('Scan'),
                MakeEl('span', '', 'Terrain Bounds & Layer Stacks'),
                MakeEl('span', 'group-count', String(this.world.regions.length))
            ]));

            for (const R of this.world.regions)
            {
                const MatchReg = !Q || R.name.toLowerCase().includes(Q);
                if (this.outlinerFilter !== 'splines' && MatchReg)
                {
                    const IsPrim = R.id === this.activeRegionId;
                    const IsMulti = this.selectedRegionIds.has(R.id);
                    const Row = MakeEl('div', 'tree-row' + (IsPrim ? ' selected' : '') + (IsMulti ? ' multi-selected' : '') + (!R.enabled ? ' is-off' : ''));
                    Row.style.setProperty('--row-accent', '#eff4cf');

                    const SelBtn = MakeEl('button', 'row-select');
                    SelBtn.type = 'button';
                    const GlyphSpan = MakeEl('span', 'row-glyph', ConstructGlyph('Mountain'));
                    GlyphSpan.style.color = R.color || '#e8c27a';
                    const B = R.bounds;
                    const Copy = MakeEl('span', 'row-copy', [
                        MakeEl('div', 'row-name', R.name),
                        MakeEl('div', 'row-meta', `${(B.width / 1000).toFixed(2)}×${(B.depth / 1000).toFixed(2)} km · ${R.stack.length} layers`)
                    ]);
                    SelBtn.append(GlyphSpan, Copy);
                    SelBtn.addEventListener('click', E =>
                    {
                        const Multi = E.shiftKey || E.ctrlKey || E.metaKey;
                        const Next = new Set(this.selectedRegionIds);
                        if (Multi) { if (Next.has(R.id) && Next.size > 1) Next.delete(R.id); else Next.add(R.id); }
                        else { Next.clear(); Next.add(R.id); }
                        this._setRegionSelection(R.id, Array.from(Next));
                    });

                    const End = MakeEl('div', 'row-end-actions');
                    const Eye = MakeEl('button', 'row-mini-btn' + (R.enabled ? ' eye-on' : ''), ConstructGlyph(R.enabled ? 'Eye' : 'EyeOff'));
                    Eye.type = 'button';
                    Eye.title = 'Toggle Terrain Bounds ON / OFF';
                    Eye.addEventListener('click', E =>
                    {
                        E.stopPropagation();
                        R.enabled = !R.enabled;
                        this.map2D.requestRedraw();
                        this.refreshChrome();
                    });
                    const Foc = MakeEl('button', 'row-mini-btn', ConstructGlyph('Focus'));
                    Foc.type = 'button';
                    Foc.title = 'Frame Terrain in 2D & 3D';
                    Foc.addEventListener('click', E =>
                    {
                        E.stopPropagation();
                        this._setRegionSelection(R.id, [R.id]);
                        this.map2D.frameBounds(R.bounds, 1.32);
                        this.view3D.frameRegion(R);
                    });
                    End.append(Eye, Foc);
                    Row.append(SelBtn, End);
                    Group.appendChild(Row);
                }

                // Show nested layers for active region (or all spline layers when filtering by 'splines')
                if ((R.id === this.activeRegionId && this.outlinerFilter === 'all') || this.outlinerFilter === 'splines')
                {
                    for (let I = R.stack.length - 1; I >= 0; --I)
                    {
                        const Lyr = R.stack[I];
                        if (this.outlinerFilter === 'splines' && Lyr.type !== 'spline') continue;
                        if (Q && !Lyr.name.toLowerCase().includes(Q)) continue;

                        const IsSelLyr = R.id === this.activeRegionId && Lyr.id === this.activeLayerId;
                        const LRow = MakeEl('div', 'tree-row child-layer' + (IsSelLyr ? ' selected' : '') + (!Lyr.enabled ? ' is-off' : ''));
                        const Meta = LAYER_TYPE_META[Lyr.type] || LAYER_TYPE_META.multifractal;
                        LRow.style.setProperty('--row-accent', Lyr.type === 'spline' ? '#eff4cf' : Meta.accent);

                        const LSel = MakeEl('button', 'row-select');
                        LSel.type = 'button';
                        const LGlyph = MakeEl('span', 'row-glyph', ConstructGlyph(Lyr.type === 'spline' ? 'Spline' : 'Layers'));
                        const FallTag = Lyr.type === 'spline'
                            ? `${Lyr.points?.length || 0} 3D knots · ${Lyr.splineMode}`
                            : (Lyr.falloff?.mode === 'none' ? 'Falloff: NONE (Cliff)' : `Falloff: ${Lyr.falloff?.mode} (${Math.round(Lyr.falloff?.distance || 260)}m)`);
                        LSel.append(LGlyph, MakeEl('span', 'row-copy', [
                            MakeEl('div', 'row-name', Lyr.name),
                            MakeEl('div', 'row-meta', FallTag)
                        ]));
                        LSel.addEventListener('click', () =>
                        {
                            this.activeRegionId = R.id;
                            this.activeLayerId = Lyr.id;
                            if (Lyr.type === 'spline')
                            {
                                this.view3D.activeSplineLayerId = Lyr.id;
                                this.inspectorTab = 'spline';
                            }
                            else
                            {
                                this.inspectorTab = 'layers';
                            }
                            this._syncInspectorTabButtons();
                            EvaluateTerrainRegion(this.world, R, Lyr.id);
                            R.meshDirty = true;
                            this.refreshChrome();
                            this.rebuildInspector();
                        });

                        const LEnd = MakeEl('div', 'row-end-actions');
                        const LEye = MakeEl('button', 'row-mini-btn' + (Lyr.enabled ? ' eye-on' : ''), ConstructGlyph(Lyr.enabled ? 'Eye' : 'EyeOff'));
                        LEye.type = 'button';
                        LEye.addEventListener('click', E =>
                        {
                            E.stopPropagation();
                            Lyr.enabled = !Lyr.enabled;
                            EvaluateTerrainRegion(this.world, R, this.activeLayerId);
                            R.meshDirty = true;
                            this.map2D.invalidateRegionPreview(R.id);
                            this.refreshChrome();
                            this.rebuildInspector();
                        });
                        const LDel = MakeEl('button', 'row-mini-btn', ConstructGlyph('Trash'));
                        LDel.type = 'button';
                        LDel.addEventListener('click', E =>
                        {
                            E.stopPropagation();
                            this.pushUndoSnapshot();
                            R.stack.splice(I, 1);
                            if (this.activeLayerId === Lyr.id) this.activeLayerId = R.stack[R.stack.length - 1]?.id || null;
                            EvaluateTerrainRegion(this.world, R, this.activeLayerId);
                            R.meshDirty = true;
                            this.map2D.invalidateRegionPreview(R.id);
                            this.refreshChrome();
                            this.rebuildInspector();
                        });
                        LEnd.append(LEye, LDel);
                        LRow.append(LSel, LEnd);
                        Group.appendChild(LRow);
                    }
                }
            }
            Outliner.appendChild(Group);
        }

        // 2D Mask Layers Group
        if (this.outlinerFilter === 'all' || this.outlinerFilter === 'masks')
        {
            const MGroup = MakeEl('div', 'tree-group');
            MGroup.appendChild(MakeEl('div', 'tree-group-title', [
                ConstructGlyph('Map'),
                MakeEl('span', '', 'Infinite 2D Mask Layers'),
                MakeEl('span', 'group-count', String(this.world.maskLayers.length))
            ]));
            for (const M of this.world.maskLayers)
            {
                if (Q && !M.name.toLowerCase().includes(Q)) continue;
                const IsAct = M.id === this.map2D.activeMaskLayerId;
                const MRow = MakeEl('div', 'tree-row' + (IsAct ? ' selected' : '') + (!M.visible ? ' is-off' : ''));
                MRow.style.setProperty('--row-accent', M.color);

                const MSel = MakeEl('button', 'row-select');
                MSel.type = 'button';
                const MGlyph = MakeEl('span', 'row-glyph', ConstructGlyph('Map'));
                MGlyph.style.color = M.color;
                MSel.append(MGlyph, MakeEl('span', 'row-copy', [
                    MakeEl('div', 'row-name', M.name),
                    MakeEl('div', 'row-meta', `${M.tiles.size} sparse 1×1 km tiles`)
                ]));
                MSel.addEventListener('click', () =>
                {
                    this.map2D.activeMaskLayerId = M.id;
                    this.refreshChrome();
                });

                const MEnd = MakeEl('div', 'row-end-actions');
                const MEye = MakeEl('button', 'row-mini-btn' + (M.visible ? ' eye-on' : ''), ConstructGlyph(M.visible ? 'Eye' : 'EyeOff'));
                MEye.type = 'button';
                MEye.addEventListener('click', E =>
                {
                    E.stopPropagation();
                    M.visible = !M.visible;
                    this.world.markRegionsDirtyInRect(-1e7, -1e7, 1e7, 1e7, null);
                    this._evaluateDirtyRegionsNow();
                    this.map2D.requestRedraw();
                    this.refreshChrome();
                });
                MEnd.appendChild(MEye);
                MRow.append(MSel, MEnd);
                MGroup.appendChild(MRow);
            }
            Outliner.appendChild(MGroup);
        }

        // 4. Sync Right Object Header & Footer Status
        if (Reg)
        {
            document.getElementById('inspector-crumb').textContent = `Inspector / ${Reg.name}`;
            const NameInp = document.getElementById('object-name');
            if (NameInp)
            {
                NameInp.readOnly = false;
                if (document.activeElement !== NameInp) NameInp.value = Reg.name;
            }
            document.getElementById('object-type').textContent = `THIN-SLAB SDF · ${Reg.resolution} CELLS · ${Reg.stack.length} LAYERS`;
            document.getElementById('object-enabled').checked = Reg.enabled;

            const Floor = ((this.world.seaLevel ?? 0) - Reg.slabThickness).toFixed(1);
            const Peak = Math.round(Reg.runtime?.maxHeight ?? 0);
            document.getElementById('hud-slab-floor').textContent = `${Floor} m`;
            document.getElementById('status-main').textContent = this.selectedRegionIds.size > 1
                ? `${Reg.name} (+${this.selectedRegionIds.size - 1} multi-selected)`
                : Reg.name;
            document.getElementById('status-slab').textContent = `Thin SDF Base: ${Floor} m → +${Peak} m (Zero Deep-Ocean Waste)`;
        }

        const VisCount = this.view3D.getVisibleRegions().length;
        document.getElementById('hud-3d-scope').textContent = `${VisCount} TERRAIN${VisCount === 1 ? '' : 'S'}`;
        document.getElementById('status-tiles').textContent = `Sparse 2D Tiles: ${TilesCount} (${(this.world.totalAllocatedBytes() / 1024).toFixed(0)} KB)`;
    }

    /*======================================================================================================================================
                              RIGHT CONTEXTUAL INSPECTOR (KPI TRIO + 4 TABS MATCHING 3D CLAY STUDIO)
    ======================================================================================================================================*/
    rebuildInspector()
    {
        const Content = document.getElementById('InspectorContent');
        Content.innerHTML = '';
        this._activeKnotYSlider = null;

        if (this.workspaceMode === 'rock2' || this.inspectorTab === 'rock2')
        {
            this._buildRock2InspectorTab(Content);
            return;
        }

        if (this.inspectorTab === 'fracture')
        {
            this._buildFractureInspectorTab(Content);
            return;
        }

        const Region = this.getActiveRegion();
        if (!Region) return;

        // Top KPI Trio (#eff4cf highlight card just like 3D Clay Studio!)
        const PeakM = Math.round(Region.runtime?.maxHeight ?? 0);
        const AreaKm = (Region.runtime?.landAreaKm2 ?? 0).toFixed(2);
        const Trio = MakeEl('div', 'kpi-trio', [
            MakeEl('div', 'kpi-card highlight', [
                MakeEl('span', 'kpi-label', 'Peak Relief'),
                MakeEl('strong', 'kpi-val', `+${PeakM} m`),
                MakeEl('span', 'kpi-sub', `${AreaKm} km² landmass`)
            ]),
            MakeEl('div', 'kpi-card', [
                MakeEl('span', 'kpi-label', 'Thin SDF Wafer'),
                MakeEl('strong', 'kpi-val', `-${Region.slabThickness.toFixed(1)} m`),
                MakeEl('span', 'kpi-sub', 'Zero deep waste')
            ]),
            MakeEl('div', 'kpi-card', [
                MakeEl('span', 'kpi-label', 'Layer Stack'),
                MakeEl('strong', 'kpi-val', String(Region.stack.length)),
                MakeEl('span', 'kpi-sub', `${Region.stack.filter(L => L.type === 'spline').length} 3D spline(s)`)
            ])
        ]);
        Content.appendChild(Trio);

        // 4 Sun Quick-Control Feature Tiles
        const Switches = MakeEl('div', 'property-switches');
        const MakeTile = (Label, Glyph, IsOn, OnClick) =>
        {
            const B = MakeEl('button', 'property-switch' + (IsOn ? '' : ' is-off'));
            B.type = 'button';
            B.append(
                MakeEl('span', 'switch-icon', ConstructGlyph(Glyph)),
                MakeEl('span', 'switch-name', Label),
                MakeEl('span', 'switch-state', IsOn ? 'ON' : 'OFF')
            );
            B.addEventListener('click', OnClick);
            return B;
        };
        Switches.append(
            MakeTile('Ocean', 'Water', this.world.oceanVisible !== false, () => document.getElementById('toggle-ocean-btn')?.click()),
            MakeTile('3D Gizmo', 'Gizmo', this.view3D.showGizmo, () => document.getElementById('toggle-gizmo-btn')?.click()),
            MakeTile('Bounds', 'Scan', this.view3D.showBounds3D, () => document.getElementById('toggle-bounds-btn')?.click()),
            MakeTile('Contours', 'Activity', this.view3D.showContours, () => document.getElementById('toggle-contours-btn')?.click())
        );
        Content.appendChild(Switches);

        if (this.inspectorTab === 'spline')
        {
            this._buildSplineInspectorTab(Content, Region);
        }
        else if (this.inspectorTab === 'slab')
        {
            this._buildThinSlabInspectorTab(Content, Region);
        }
        else if (this.inspectorTab === 'stage')
        {
            this._buildStageAndExportTab(Content, Region);
        }
        else if (this.inspectorTab === 'sim')
        {
            this._buildSimulationTab(Content, Region);
        }
        else if (this.inspectorTab === 'texturing')
        {
            this._buildTexturingTab(Content, Region);
        }
        else
        {
            this._buildLayersAndFalloffTab(Content, Region);
        }
    }

    /*--------------------------------------------------------------------------------------------------------------------------------------
                                              TAB 1: LAYERS & PER-LAYER BOUNDARY FALLOFF
    --------------------------------------------------------------------------------------------------------------------------------------*/
    _buildLayersAndFalloffTab(Content, Region)
    {
        const ActiveLayer = Region.stack.find(L => L.id === this.activeLayerId) || Region.stack[Region.stack.length - 1];

        // Quick Add Bar at top of Layers tab
        const StackGroup = MakePropertyGroup('Composable Layer Stack', 'Layers', `${Region.stack.length} LAYERS`, true,
            'Click any layer in the Outliner or add a procedural generator / 3D spline below.');
        const BtnRow = MakeEl('div', 'studio-btn-row');
        const BtnAddSp = MakeEl('button', 'button export-button', '+ Add 3D Spline (Gizmo)');
        BtnAddSp.type = 'button';
        BtnAddSp.addEventListener('click', () => this.addNew3DSplineLayer('grade', '3D Graded Spline Ramp'));

        const BtnAddFrac = MakeEl('button', 'button', '+ Multifractal');
        BtnAddFrac.type = 'button';
        BtnAddFrac.addEventListener('click', () => this.addProceduralLayer('multifractal'));

        const BtnAddVor = MakeEl('button', 'button', '+ Voronoi Cliff');
        BtnAddVor.type = 'button';
        BtnAddVor.addEventListener('click', () => this.addProceduralLayer('voronoi'));

        BtnRow.append(BtnAddSp, BtnAddFrac, BtnAddVor);
        StackGroup.Body.appendChild(BtnRow);
        Content.appendChild(StackGroup.Group);

        if (!ActiveLayer) return;

        // Per-Layer Boundary Falloff Card
        const FallGroup = MakePropertyGroup('Boundary Falloff (Edge → Center)', 'Sliders', ActiveLayer.falloff.mode.toUpperCase(), true,
            'Control whether landforms fade softly near the 2D mask coastline or hold 100% strength right to sheer sea cliffs.');

        FallGroup.Body.appendChild(MakeSelectRow('Falloff Profile Mode', BOUNDARY_FALLOFF_MODES, ActiveLayer.falloff.mode, Val =>
        {
            this.pushUndoSnapshot();
            ActiveLayer.falloff.mode = Val;
            EvaluateTerrainRegion(this.world, Region, ActiveLayer.id);
            Region.meshDirty = true;
            this.map2D.invalidateRegionPreview(Region.id);
            this.refreshChrome();
            this.rebuildInspector();
        }));

        const CurveGraph = MakeBoundaryFalloffGraph(
            ActiveLayer.falloff,
            () => { Region.dirty = true; this._scheduleDirtyRegionsEvaluation(); },
            () => this._evaluateDirtyRegionsNow()
        );
        FallGroup.Body.appendChild(CurveGraph);

        if (ActiveLayer.falloff.mode !== 'none')
        {
            FallGroup.Body.appendChild(MakeSliderPillRow('Inland Ramp Distance', 15, 1100, 5, ActiveLayer.falloff.distance, 0, 'm', V =>
            {
                ActiveLayer.falloff.distance = V;
                CurveGraph.Redraw();
                Region.dirty = true;
                this._scheduleDirtyRegionsEvaluation();
            }, () => this.refreshChrome()));

            FallGroup.Body.appendChild(MakeSliderPillRow('Coastline Edge Floor', 0.0, 1.0, 0.01, ActiveLayer.falloff.edgeFloor, 2, 'w', V =>
            {
                ActiveLayer.falloff.edgeFloor = V;
                CurveGraph.Redraw();
                Region.dirty = true;
                this._scheduleDirtyRegionsEvaluation();
            }));
        }
        Content.appendChild(FallGroup.Group);

        // Selected Generator Parameters Card
        const TriggerLive = () => { Region.dirty = true; this._scheduleDirtyRegionsEvaluation(); };
        const TriggerCommit = () => { this._evaluateDirtyRegionsNow(); this.refreshChrome(); };

        const GenGroup = MakePropertyGroup(ActiveLayer.name, 'Mountain', ActiveLayer.type.toUpperCase(), true);
        GenGroup.Body.appendChild(MakeSliderPillRow('Layer Blend Opacity', 0.0, 1.0, 0.01, ActiveLayer.opacity ?? 1.0, 2, 'w', V => { ActiveLayer.opacity = V; TriggerLive(); }, TriggerCommit));

        if (!['terrace', 'spline', 'sculpt'].includes(ActiveLayer.type))
        {
            GenGroup.Body.appendChild(MakeSelectRow('Combine Mode', COMBINE_MODES, ActiveLayer.combine || 'add', Val =>
            {
                this.pushUndoSnapshot();
                ActiveLayer.combine = Val;
                TriggerLive();
                TriggerCommit();
            }));
        }

        if (ActiveLayer.type === 'multifractal')
        {
            GenGroup.Body.appendChild(MakeSliderPillRow('Elevation Amplitude', 10, 460, 2, ActiveLayer.amplitude, 0, 'm', V => { ActiveLayer.amplitude = V; TriggerLive(); }, TriggerCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Feature Frequency', 0.4, 8.0, 0.05, ActiveLayer.scale, 2, '×', V => { ActiveLayer.scale = V; TriggerLive(); }, TriggerCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Fractal Octaves', 2, 8, 1, ActiveLayer.octaves, 0, 'oct', V => { ActiveLayer.octaves = V; TriggerLive(); }, TriggerCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Detail Roughness', 0.25, 0.78, 0.01, ActiveLayer.gain, 2, 'g', V => { ActiveLayer.gain = V; TriggerLive(); }, TriggerCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Tectonic Warp', 0.0, 1.2, 0.02, ActiveLayer.warp, 2, 'w', V => { ActiveLayer.warp = V; TriggerLive(); }, TriggerCommit));
        }
        else if (ActiveLayer.type === 'ridged')
        {
            GenGroup.Body.appendChild(MakeSliderPillRow('Ridge Relief Height', 10, 420, 2, ActiveLayer.amplitude, 0, 'm', V => { ActiveLayer.amplitude = V; TriggerLive(); }, TriggerCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Chain Frequency', 0.4, 8.0, 0.05, ActiveLayer.scale, 2, '×', V => { ActiveLayer.scale = V; TriggerLive(); }, TriggerCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Crest Sharpness', 0.05, 1.0, 0.01, ActiveLayer.sharpness, 2, 's', V => { ActiveLayer.sharpness = V; TriggerLive(); }, TriggerCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Strike Azimuth', 0, 180, 1, ActiveLayer.azimuth, 0, '°', V => { ActiveLayer.azimuth = V; TriggerLive(); }, TriggerCommit));
        }
        else if (ActiveLayer.type === 'voronoi')
        {
            GenGroup.Body.appendChild(MakeSelectRow('Cellular Morphology', [
                ['Stepped Mesa / Basalt Blocks', 0],
                ['Polygonal Fracture Ridges (F2−F1)', 1],
                ['Caldera & Crater Bowls', 2],
                ['Pillow Domes (1−F1)', 3]
            ], ActiveLayer.variant | 0, Val => { ActiveLayer.variant = Number(Val); TriggerLive(); TriggerCommit(); }));
            GenGroup.Body.appendChild(MakeSliderPillRow('Mesa / Block Height', 10, 380, 2, ActiveLayer.amplitude, 0, 'm', V => { ActiveLayer.amplitude = V; TriggerLive(); }, TriggerCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Cell Density', 0.5, 9.0, 0.05, ActiveLayer.scale, 2, '×', V => { ActiveLayer.scale = V; TriggerLive(); }, TriggerCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Rim Cliff Sharpness', 0.05, 1.0, 0.02, ActiveLayer.sharpness, 2, 's', V => { ActiveLayer.sharpness = V; TriggerLive(); }, TriggerCommit));
        }
        else if (ActiveLayer.type === 'billow')
        {
            GenGroup.Body.appendChild(MakeSliderPillRow('Hill / Dune Amplitude', 5, 260, 2, ActiveLayer.amplitude, 0, 'm', V => { ActiveLayer.amplitude = V; TriggerLive(); }, TriggerCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Fold Frequency', 0.5, 9.0, 0.05, ActiveLayer.scale, 2, '×', V => { ActiveLayer.scale = V; TriggerLive(); }, TriggerCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Directional Dune Mix', 0.0, 1.0, 0.02, ActiveLayer.duneMix, 2, 'w', V => { ActiveLayer.duneMix = V; TriggerLive(); }, TriggerCommit));
        }
        else if (ActiveLayer.type === 'terrace')
        {
            GenGroup.Body.appendChild(MakeSliderPillRow('Stratigraphic Beds', 2, 24, 1, ActiveLayer.bedCount, 0, 'bed', V => { ActiveLayer.bedCount = V; TriggerLive(); }, TriggerCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Bench Flatness', 0.1, 0.98, 0.01, ActiveLayer.strength, 2, 'w', V => { ActiveLayer.strength = V; TriggerLive(); }, TriggerCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Bedding Dip Angle', 0, 25, 0.5, ActiveLayer.dipDeg, 1, '°', V => { ActiveLayer.dipDeg = V; TriggerLive(); }, TriggerCommit));
        }
        else if (ActiveLayer.type === 'spline')
        {
            const JumpBtn = MakeEl('button', 'button export-button', 'Open 3D Spline & Gizmo Controls ↗');
            JumpBtn.type = 'button';
            JumpBtn.style.width = '100%';
            JumpBtn.style.justifyContent = 'center';
            JumpBtn.addEventListener('click', () =>
            {
                this.inspectorTab = 'spline';
                this._syncInspectorTabButtons();
                this.rebuildInspector();
            });
            GenGroup.Body.appendChild(JumpBtn);
        }
        else if (ActiveLayer.type === 'rock_formation')
        {
            const B = Region.bounds;
            GenGroup.Body.appendChild(MakeSliderPillRow('Formation World Scale', 35, 420, 5, ActiveLayer.worldScaleM ?? 165, 0, 'm', V =>
            {
                ActiveLayer.worldScaleM = V;
                Region.meshDirty = true;
            }, () => this.refreshChrome()));
            GenGroup.Body.appendChild(MakeSliderPillRow('Yaw Orientation', 0, 360, 2, ActiveLayer.yawDeg ?? 0, 0, '°', V =>
            {
                ActiveLayer.yawDeg = V;
                Region.meshDirty = true;
            }, () => this.refreshChrome()));
            GenGroup.Body.appendChild(MakeSliderPillRow('World Position X', Math.round(B.x), Math.round(B.x + B.width), 5, ActiveLayer.worldX ?? (B.x + B.width * 0.5), 0, 'm', V =>
            {
                ActiveLayer.worldX = V;
                Region.meshDirty = true;
            }, () => this.refreshChrome()));
            GenGroup.Body.appendChild(MakeSliderPillRow('World Position Z', Math.round(B.z), Math.round(B.z + B.depth), 5, ActiveLayer.worldZ ?? (B.z + B.depth * 0.5), 0, 'm', V =>
            {
                ActiveLayer.worldZ = V;
                Region.meshDirty = true;
            }, () => this.refreshChrome()));
        }

        if (['multifractal', 'ridged', 'voronoi', 'billow'].includes(ActiveLayer.type))
        {
            const SeedRow = MakeEl('div', 'studio-btn-row');
            const SeedBtn = MakeEl('button', 'button', `Randomize Seed (#${ActiveLayer.seed})`);
            SeedBtn.type = 'button';
            SeedBtn.addEventListener('click', () =>
            {
                this.pushUndoSnapshot();
                ActiveLayer.seed = Math.floor(1000 + Math.random() * 89000);
                TriggerLive();
                TriggerCommit();
                this.rebuildInspector();
            });
            SeedRow.appendChild(SeedBtn);
            GenGroup.Body.appendChild(SeedRow);
        }

        Content.appendChild(GenGroup.Group);
    }

    /*--------------------------------------------------------------------------------------------------------------------------------------
                                     TAB 2: DEDICATED 3D SPLINE & XYZ UP/DOWN GIZMO AUTHORING TAB
    --------------------------------------------------------------------------------------------------------------------------------------*/
    _buildSplineInspectorTab(Content, Region)
    {
        const CreateCard = MakePropertyGroup('Create 3D Spline Landforms', 'Spline', '3D GIZMO', true,
            'Add a 3D Spline Layer, click any numbered knot in 3D, and drag the Green Y ↕ Gizmo arrow UP or DOWN (or Shift+Click terrain to add knots).');

        const AddBtns = MakeEl('div', 'studio-btn-row');
        const B1 = MakeEl('button', 'button export-button', '+ Graded Ramp / Ridge');
        B1.type = 'button';
        B1.addEventListener('click', () => this.addNew3DSplineLayer('grade', '3D Graded Spline Ramp'));

        const B2 = MakeEl('button', 'button', '+ 3D Rock Arch Bridge');
        B2.type = 'button';
        B2.addEventListener('click', () => this.addNew3DSplineLayer('arch3d', '3D Rock Arch Bridge'));

        const B2b = MakeEl('button', 'button', '+ 3D Mountain Tunnel');
        B2b.type = 'button';
        B2b.addEventListener('click', () => this.addNew3DSplineLayer('tunnel', '3D Mountain Tunnel Bore'));

        const B3 = MakeEl('button', 'button', '+ Place Knots on Terrain (K)');
        B3.type = 'button';
        B3.addEventListener('click', () =>
        {
            if (this.workspaceMode === '2d') this.setWorkspaceMode('split');
            this.select3DTool('spline-add');
        });
        AddBtns.append(B1, B2);
        CreateCard.Body.append(AddBtns);
        const PlaceWrap = MakeEl('div', 'studio-btn-row');
        PlaceWrap.append(B2b, B3);
        CreateCard.Body.appendChild(PlaceWrap);
        Content.appendChild(CreateCard.Group);

        // Find active spline layer on Region
        let SplineLayer = Region.stack.find(L => L.id === this.activeLayerId && L.type === 'spline')
                       || Region.stack.find(L => L.id === this.view3D.activeSplineLayerId && L.type === 'spline')
                       || Region.stack.find(L => L.type === 'spline');

        if (!SplineLayer) return;
        this.view3D.activeSplineLayerId = SplineLayer.id;
        this.activeLayerId = SplineLayer.id;

        const TriggerSpline = () =>
        {
            EvaluateTerrainRegion(this.world, Region, SplineLayer.id);
            Region.meshDirty = true;
            this.map2D.invalidateRegionPreview(Region.id);
            this.map2D.requestRedraw();
        };

        // Selected Knot 3D XYZ (Up/Down Y) Gizmo Inspector Card
        const KnotCount = SplineLayer.points.length;
        const KIdx = Clamp(this.view3D.selectedKnotIndex | 0, 0, Math.max(0, KnotCount - 1));
        this.view3D.selectedKnotIndex = KIdx;
        const Knot = SplineLayer.points[KIdx];

        if (Knot)
        {
            const KnotGroup = MakePropertyGroup(`Selected Knot #${KIdx + 1} · 3D Gizmo`, 'Gizmo', `OF ${KnotCount}`, true,
                'Drag the Green Y ↕ arrow in the 3D viewport or scrub Elevation Y below to move this spline point up/down.');

            // Y Elevation (Up / Down) SliderPill!
            const CurY = Number.isFinite(Knot[2]) ? Knot[2] : this.view3D.sampleTerrainSurfaceY(Region, Knot[0], Knot[1]);
            const YRow = MakeSliderPillRow('Knot Elevation Y (Up / Down ↕)', -60, 520, 0.5, CurY, 1, 'm', V =>
            {
                Knot[2] = V;
                TriggerSpline();
            }, () => this.rebuildInspector());
            this._activeKnotYSlider = YRow;
            KnotGroup.Body.appendChild(YRow);

            const B = Region.bounds;
            KnotGroup.Body.appendChild(MakeSliderPillRow('Knot World X (East / West)', B.x - 400, B.x + B.width + 400, 1, Knot[0], 0, 'm', V =>
            {
                Knot[0] = Math.round(V);
                TriggerSpline();
            }, () => this.rebuildInspector()));

            KnotGroup.Body.appendChild(MakeSliderPillRow('Knot World Z (North / South)', B.z - 400, B.z + B.depth + 400, 1, Knot[1], 0, 'm', V =>
            {
                Knot[1] = Math.round(V);
                TriggerSpline();
            }, () => this.rebuildInspector()));

            const KnotActions = MakeEl('div', 'studio-btn-row');
            const BtnSnapGround = MakeEl('button', 'button', 'Snap Y to Terrain');
            BtnSnapGround.type = 'button';
            BtnSnapGround.addEventListener('click', () =>
            {
                this.pushUndoSnapshot();
                Knot[2] = Math.round(this.view3D.sampleTerrainSurfaceY(Region, Knot[0], Knot[1]) * 10) / 10;
                TriggerSpline();
                this.rebuildInspector();
            });

            const BtnRaise25 = MakeEl('button', 'button', 'Y +25 m ↑');
            BtnRaise25.type = 'button';
            BtnRaise25.addEventListener('click', () =>
            {
                this.pushUndoSnapshot();
                Knot[2] = Math.round(((Knot[2] ?? 20) + 25) * 10) / 10;
                TriggerSpline();
                this.rebuildInspector();
            });

            const BtnLower25 = MakeEl('button', 'button', 'Y −25 m ↓');
            BtnLower25.type = 'button';
            BtnLower25.addEventListener('click', () =>
            {
                this.pushUndoSnapshot();
                Knot[2] = Math.round(((Knot[2] ?? 20) - 25) * 10) / 10;
                TriggerSpline();
                this.rebuildInspector();
            });

            const BtnDelKnot = MakeEl('button', 'button', 'Delete Knot');
            BtnDelKnot.type = 'button';
            BtnDelKnot.disabled = KnotCount <= 2;
            BtnDelKnot.addEventListener('click', () =>
            {
                if (SplineLayer.points.length <= 2) return;
                this.pushUndoSnapshot();
                SplineLayer.points.splice(KIdx, 1);
                this.view3D.selectedKnotIndex = Math.max(0, KIdx - 1);
                TriggerSpline();
                this.rebuildInspector();
            });

            KnotActions.append(BtnRaise25, BtnLower25, BtnSnapGround, BtnDelKnot);
            KnotGroup.Body.appendChild(KnotActions);

            // Clickable list of all knots on this spline
            const KList = MakeEl('div', 'knot-list');
            SplineLayer.points.forEach((Pt, Idx) =>
            {
                const Row = MakeEl('div', 'knot-chip-row' + (Idx === KIdx ? ' active' : ''));
                const Wy = Number.isFinite(Pt[2]) ? Pt[2] : 0;
                Row.append(
                    MakeEl('span', 'knot-badge', String(Idx + 1)),
                    MakeEl('span', 'knot-coords', `X ${Pt[0]} m · Z ${Pt[1]} m`),
                    MakeEl('span', 'knot-elev-pill', `Y ${Wy >= 0 ? '+' : ''}${Wy.toFixed(1)} m`)
                );
                Row.addEventListener('click', () =>
                {
                    this.view3D.selectedKnotIndex = Idx;
                    this.select3DTool('spline');
                    this.rebuildInspector();
                });
                KList.appendChild(Row);
            });
            KnotGroup.Body.appendChild(KList);
            Content.appendChild(KnotGroup.Group);
        }

        // Spline Corridor Profile Card
        const ProfGroup = MakePropertyGroup(`${SplineLayer.name} · Profile`, 'Sliders', SplineLayer.splineMode.toUpperCase(), true);
        ProfGroup.Body.appendChild(MakeSelectRow('Spline Landform Mode', SPLINE_MODES, SplineLayer.splineMode || 'grade', Val =>
        {
            this.pushUndoSnapshot();
            SplineLayer.splineMode = Val;
            TriggerSpline();
            this.refreshChrome();
            this.rebuildInspector();
        }));
        ProfGroup.Body.appendChild(MakeSliderPillRow('Corridor Width', 16, 380, 2, SplineLayer.width, 0, 'm', V => { SplineLayer.width = V; TriggerSpline(); }));
        ProfGroup.Body.appendChild(MakeSliderPillRow(
            SplineLayer.splineMode === 'arch3d' ? 'Arch Tube Thickness'
                : (SplineLayer.splineMode === 'tunnel' ? 'Tunnel Arch Clearance' : 'Height / Carve Depth'),
            4, 220, 2, SplineLayer.amount, 0, 'm', V => { SplineLayer.amount = V; TriggerSpline(); }
        ));
        ProfGroup.Body.appendChild(MakeSliderPillRow('Bank Smoothness', 0.05, 1.0, 0.02, SplineLayer.bankSmooth, 2, 'w', V => { SplineLayer.bankSmooth = V; TriggerSpline(); }));
        ProfGroup.Body.appendChild(MakeSliderPillRow('Natural Meander', 0.0, 0.8, 0.02, SplineLayer.meander, 2, 'w', V => { SplineLayer.meander = V; TriggerSpline(); }));
        Content.appendChild(ProfGroup.Group);
    }

    /*--------------------------------------------------------------------------------------------------------------------------------------
                                          TAB 3: THIN-SLAB SDF ARCHITECTURE SETTINGS
    --------------------------------------------------------------------------------------------------------------------------------------*/
    _buildThinSlabInspectorTab(Content, Region)
    {
        const SlabGroup = MakePropertyGroup('Thin-Slab SDF Geometry', 'Boxes', 'SPACE-EFFICIENT', true,
            '2D masks initialize as a thin flat wafer near sea level and build upward — zero SDF space or triangles wasted deep under the ocean.');

        const MaskOpts = [['All Visible 2D Masks (Union)', '__all__']];
        for (const M of this.world.maskLayers) MaskOpts.push([M.name, M.id]);
        SlabGroup.Body.appendChild(MakeSelectRow('Linked 2D Mask Source', MaskOpts, Region.maskLayerId || '__all__', Val =>
        {
            this.pushUndoSnapshot();
            Region.maskLayerId = Val === '__all__' ? null : Val;
            EvaluateTerrainRegion(this.world, Region, this.activeLayerId);
            Region.meshDirty = true;
            this.map2D.invalidateRegionPreview(Region.id);
            this.refreshChrome();
        }));

        SlabGroup.Body.appendChild(MakeSliderPillRow('SDF Grid Resolution', 64, 224, 8, Region.resolution, 0, 'res', V =>
        {
            Region.resolution = V;
            Region.dirty = true;
            this._scheduleDirtyRegionsEvaluation();
        }, () => { this._evaluateDirtyRegionsNow(); this.refreshChrome(); }));

        SlabGroup.Body.appendChild(MakeSliderPillRow('Base Flat Surface Elev', -2.0, 35.0, 0.5, Region.baseElevation, 1, 'm', V =>
        {
            Region.baseElevation = V;
            Region.dirty = true;
            this._scheduleDirtyRegionsEvaluation();
        }));

        SlabGroup.Body.appendChild(MakeSliderPillRow('Thin Underside Wafer', 2.0, 22.0, 0.5, Region.slabThickness, 1, 'm', V =>
        {
            Region.slabThickness = V;
            Region.dirty = true;
            this._scheduleDirtyRegionsEvaluation();
        }, () => this.refreshChrome()));

        SlabGroup.Body.appendChild(MakeSliderPillRow('Shoreline Bevel (0 = Cliff)', 0.0, 65.0, 1.0, Region.shorelineBevel, 0, 'm', V =>
        {
            Region.shorelineBevel = V;
            Region.dirty = true;
            this._scheduleDirtyRegionsEvaluation();
        }));

        Content.appendChild(SlabGroup.Group);

        // Large-World -> Manageable Sub-World Partitioner & Seamless Border Stitching Card!
        const PartGroup = MakePropertyGroup('Large-World Sub-World Partitioner', 'Scan', 'MULTI-TILE', true,
            'Split a large continent mask into small, manageable Terrain sub-worlds so each tile can be sculpted & simulated 1-by-1 with seamless stitched borders.');

        PartGroup.Body.appendChild(MakeSliderPillRow('Sub-World Tile Span', 600, 2600, 50, this.partitionTileSizeM, 0, 'm', V =>
        {
            this.partitionTileSizeM = V;
        }));

        PartGroup.Body.appendChild(MakeSelectRow('Adjacent Border Seam Lock', [
            ['ON · Auto-Stitch Shared Sub-World Seams', 'on'],
            ['OFF · Independent Raw Tile Edges', 'off']
        ], Region.seamLock !== false ? 'on' : 'off', Val =>
        {
            Region.seamLock = Val === 'on';
            if (Region.seamLock) StitchAdjacentRegionBorders(this.world);
            else EvaluateTerrainRegion(this.world, Region, this.activeLayerId);
            Region.meshDirty = true;
            this._scheduleAutosave();
        }));

        const PartBtns = MakeEl('div', 'studio-btn-row');
        const BtnPartition = MakeEl('button', 'button export-button', 'Partition Mask → Sub-Worlds');
        BtnPartition.type = 'button';
        BtnPartition.addEventListener('click', () =>
        {
            const TargetMask = (Region.maskLayerId && this.world.getMaskLayer(Region.maskLayerId))
                || this.world.getMaskLayer(this.map2D.activeMaskLayerId)
                || this.world.maskLayers[0];
            if (!TargetMask) return;
            this.pushUndoSnapshot();
            const SubTiles = this.world.partitionMaskIntoSubRegions(TargetMask, {
                tileSizeM: this.partitionTileSizeM,
                resolution: Region.resolution,
                templateRegion: Region
            });
            if (SubTiles.length > 0)
            {
                for (const T of SubTiles)
                {
                    EvaluateTerrainRegion(this.world, T);
                    T.meshDirty = true;
                }
                StitchAdjacentRegionBorders(this.world, SubTiles);
                this._setRegionSelection(SubTiles[0].id, SubTiles.map(T => T.id));
                this.map2D.invalidateMaskCache();
                this.map2D.frameAllRegions();
                this.view3D.frameVisibleWorld();
                this._scheduleAutosave();
            }
        });

        const BtnStitchNow = MakeEl('button', 'button', 'Stitch Borders Now');
        BtnStitchNow.type = 'button';
        BtnStitchNow.addEventListener('click', () =>
        {
            StitchAdjacentRegionBorders(this.world);
            for (const R of this.world.regions) this.map2D.invalidateRegionPreview(R.id);
            this.map2D.requestRedraw();
            this._scheduleAutosave();
        });

        PartBtns.append(BtnPartition, BtnStitchNow);
        PartGroup.Body.appendChild(PartBtns);
        Content.appendChild(PartGroup.Group);
    }

    /*--------------------------------------------------------------------------------------------------------------------------------------
                                      TAB 4: STAGE LIGHTING, OCEAN & TRIANGLE MESH EXPORT
    --------------------------------------------------------------------------------------------------------------------------------------*/
    _buildStageAndExportTab(Content, _Region)
    {
        const LightGroup = MakePropertyGroup('Stage Exposure & Ocean', 'Sun', 'ACES FILMIC', true,
            'Calibrated non-blinding dark-studio lighting with ACES filmic tonemapping.');

        LightGroup.Body.appendChild(MakeSelectRow('Sky & Atmosphere Theme', [
            ['Obsidian Studio (Default Dark)', 0],
            ['Warm Coastal Dusk', 1],
            ['Soft Twilight Daylight', 2]
        ], this.view3D.atmosphereMode, Val =>
        {
            this.view3D.atmosphereMode = Number(Val);
            const DockSel = document.getElementById('dock-atmos-select');
            if (DockSel) DockSel.value = String(Val);
        }));

        LightGroup.Body.appendChild(MakeSliderPillRow('Scene Exposure', 0.35, 1.65, 0.02, this.view3D.exposure, 2, 'ev', V =>
        {
            this.view3D.exposure = V;
            const ExpRange = document.getElementById('dock-exposure-range');
            const ExpVal = document.getElementById('dock-exposure-val');
            if (ExpRange) { ExpRange.value = String(V); ExpRange.style.setProperty('--fill', `${(((V - 0.35) / 1.30) * 100).toFixed(1)}%`); }
            if (ExpVal) ExpVal.textContent = V.toFixed(2);
        }));

        LightGroup.Body.appendChild(MakeSliderPillRow('World Sea Level', -35, 55, 0.5, this.world.seaLevel, 1, 'm', V =>
        {
            this.world.seaLevel = V;
            for (const R of this.world.regions) R.dirty = true;
            this._scheduleDirtyRegionsEvaluation();
        }));

        LightGroup.Body.appendChild(MakeSliderPillRow('Ocean Wave Height', 0.0, 4.5, 0.05, this.view3D.waveAmplitude, 2, 'm', V => { this.view3D.waveAmplitude = V; }));
        LightGroup.Body.appendChild(MakeSelectRow('3D Inland Lakes & Trunk Rivers', [
            ['Visible (Priority-Flood Closed Basins + Rivers)', 'on'],
            ['Hidden (Dry Basins & Channels Only)', 'off']
        ], this.view3D.showInlandWater ? 'on' : 'off', Val =>
        {
            this.view3D.showInlandWater = Val === 'on';
        }));
        LightGroup.Body.appendChild(MakeSliderPillRow('Sun Azimuth', 0, 360, 1, this.view3D.sunAzimuthDeg, 0, '°', V => { this.view3D.sunAzimuthDeg = V; }));
        LightGroup.Body.appendChild(MakeSliderPillRow('Sun Elevation', 6, 85, 1, this.view3D.sunElevationDeg, 0, '°', V => { this.view3D.sunElevationDeg = V; }));
        Content.appendChild(LightGroup.Group);

        const ExpGroup = MakePropertyGroup('Triangle Mesh & World Export', 'Download', '.OBJ / .STL', true,
            'Converts visible Thin-Slab SDF terrains (including 3D overhang arches & underside wafer) into watertight triangles.');
        const ExpBtns = MakeEl('div', 'studio-btn-row');
        const BtnObj = MakeEl('button', 'button export-button', 'Export .OBJ Triangles');
        BtnObj.type = 'button';
        BtnObj.addEventListener('click', () => this._exportObj());

        const BtnStl = MakeEl('button', 'button', 'Export Binary .STL');
        BtnStl.type = 'button';
        BtnStl.addEventListener('click', () => this._exportStl());

        ExpBtns.append(BtnObj, BtnStl);
        ExpGroup.Body.appendChild(ExpBtns);
        Content.appendChild(ExpGroup.Group);
    }

    /*======================================================================================================================================
                            SIMULATION — PHYSICS EROSION ENGINE CONTROLS
    ======================================================================================================================================*/
    _simTargetRegions()
    {
        if (this.sim.shareWeather)
        {
            // Shared Weather Group: every multi-selected terrain evolves under ONE cycle.
            const Targets = this.world.regions.filter(R => this.selectedRegionIds.has(R.id) && R.enabled);
            return Targets.length >= 2 ? Targets : this.world.regions.filter(R => R.enabled).slice(0, 2);
        }
        // Solo 1-by-1: only the active region simulates.
        const Solo = this.world.getRegion?.(this.activeRegionId) || this.world.regions.find(R => R.id === this.activeRegionId);
        return Solo ? [Solo] : [this.world.regions[0]].filter(Boolean);
    }

    _simEnsureGroup(Force = false)
    {
        const Targets = this._simTargetRegions();
        if (!Targets.length) return null;
        const SameTargets = this.sim.group
            && this.sim.group.members.length === Targets.length
            && this.sim.group.members.every((M, IX) => M.region === Targets[IX]);
        if (this.sim.group && SameTargets && !Force) return this.sim.group;

        this.sim.group = CreateSimulationGroup(
            this.sim.shareWeather ? 'Shared Weather Group' : 'Solo Simulation',
            Targets,
            {
                regimeId: this.sim.climateRegimeId,
                latitudeDeg: this.sim.latitudeDeg,
                startDayOfYear: this.sim.startDay,
                seed: 4029
            },
            {}
        );
        InitSimulationGroup(this.world, this.sim.group, 4029);
        return this.sim.group;
    }

    _simStepDays(Days)
    {
        const Group = this._simEnsureGroup();
        if (!Group) return;
        for (const Member of Group.members)
        {
            if (!Member.region.runtime || Member.region.dirty) EvaluateTerrainRegion(this.world, Member.region, this.activeLayerId);
        }
        if (Group.members.some(M => !M.state)) InitSimulationGroup(this.world, Group, 4029);
        for (let D = 0; D < Days; ++D) StepSimulationGroupDay(this.world, Group);
        for (const Member of Group.members)
        {
            ApplyErosionToRegion(this.world, Member.region, Member.state);
            this.map2D.invalidateRegionPreview(Member.region.id);
        }
        if (this.world.regions.length >= 2) StitchAdjacentRegionBorders(this.world);
        this._simRefreshHud();
        if (this.sim.mapLensId) this._applyMapLens();
        this._scheduleAutosave();
    }

    _simBakeYears(Years)
    {
        const Group = this._simEnsureGroup();
        if (!Group) return;
        for (const Member of Group.members)
        {
            if (!Member.region.runtime || Member.region.dirty) EvaluateTerrainRegion(this.world, Member.region, this.activeLayerId);
        }
        InitSimulationGroup(this.world, Group, 4029);
        BakeSimulationGroup(this.world, Group, Years, null);
        ApplySimulationGroup(this.world, Group);
        if (this.world.regions.length >= 2) StitchAdjacentRegionBorders(this.world);
        for (const Member of Group.members) this.map2D.invalidateRegionPreview(Member.region.id);
        this._simRefreshHud();
        if (this.sim.mapLensId) this._applyMapLens();
        this.refreshChrome();
        this._scheduleAutosave();
    }

    _simReset()
    {
        this.sim.playing = false;
        const Group = this.sim.group;
        if (!Group) return;
        for (const Member of Group.members)
        {
            Member.state = null;
            Member.region.dirty = true;
        }
        this._scheduleDirtyRegionsEvaluation();
        this._simRefreshHud();
    }

    _simRefreshHud()
    {
        const Set = (Id, Txt) => { const E = document.getElementById(Id); if (E) E.textContent = Txt; };
        const Group = this.sim.group;
        const Member = Group?.members.find(M => M.region.id === this.activeRegionId) || Group?.members[0];
        const State = Member?.state;
        const Climate = Group ? Group.climate.lastSample || SampleClimate(Group.climate) : null;
        if (Climate)
        {
            Set('sim-hud-date', `Day ${Group.climate.dayOfYear} · Year ${Group.climate.year + 1}`);
            Set('sim-hud-season', Climate.seasonName);
            Set('sim-hud-temp', `${Climate.tempC.toFixed(1)} °C`);
            Set('sim-hud-precip', `${Climate.rainMm.toFixed(1)} mm rain · ${Climate.snowMm.toFixed(1)} mm snow`);
            Set('sim-hud-wind', `${Climate.windSpeedMs.toFixed(1)} m/s @ ${Math.round(Climate.windAzimuthDeg)}°`);
            Set('sim-hud-regime', CLIMATE_REGIMES.find(R => R.id === this.sim.climateRegimeId)?.label || '—');
        }
        if (State)
        {
            const S = ErosionStateSummary(State);
            Set('sim-hud-stats', `Incised ${S.incisedM.toFixed(1)} m · Deposited ${S.depositedM.toFixed(1)} m · Pits ${S.pitCells} · Flood risk ${S.floodedCells}`);
            Set('sim-hud-day', `${State.day} sim-days baked`);
        }
        Set('sim-hud-mode', this.sim.shareWeather ? `Shared weather · ${Group?.members.length || 0} terrains` : 'Solo · 1-by-1');
    }

    _populateMapLensOptions()
    {
        const GroupEl = document.getElementById('lens-map-group');
        if (!GroupEl) return;
        for (const Meta of DATA_MAP_CATALOG)
        {
            const Opt = document.createElement('option');
            Opt.value = `map:${Meta.id}`;
            Opt.textContent = `Map · ${Meta.label}`;
            GroupEl.appendChild(Opt);
        }
    }

    _applyMapLens()
    {
        const MapId = this.sim.mapLensId;
        if (!MapId) return;
        for (const R of this.world.regions)
        {
            if (!R.runtime) continue;
            const Er = R.runtime.erosion;
            const Maps = ComputeDataMaps(R, Er || null);
            const M = Maps[MapId];
            if (M)
            {
                R.runtime.mapLensData = M.data;
                R.meshDirty = true;
            }
        }
    }

    _clearMapLens()
    {
        for (const R of this.world.regions)
        {
            if (R.runtime && R.runtime.mapLensData)
            {
                R.runtime.mapLensData = null;
                R.meshDirty = true;
            }
        }
    }

    /*--------------------------------------------------------------------------------------------------------------------------------------
                                        TAB 5: SIMULATION & SEASONAL CLIMATE
    --------------------------------------------------------------------------------------------------------------------------------------*/
    _buildSimulationTab(Content, Region)
    {
        // ---------- Preset Gallery ----------
        const PresetGroup = MakePropertyGroup('Geological Landscape Presets', 'Planet', `${EROSION_PRESETS.length} PRESETS`, true,
            'All c7egoist/Frontier landscapes, re-engineered for the physically based multi-process erosion solver.');
        PresetGroup.Body.appendChild(MakeSelectRow('Erosion Preset', EROSION_PRESETS.map(P => [P.name, P.id]), this.sim.presetId, Val =>
        {
            this.sim.presetId = Val;
            const P = GetErosionPreset(Val);
            this.sim.climateRegimeId = P.climate.regimeId;
            this.sim.latitudeDeg = P.climate.latitudeDeg;
            this.sim.startDay = P.climate.startDayOfYear;
            this.sim.rampId = P.texture.rampId;
            this.sim.stampId = P.texture.stampIds[0];
            this.rebuildInspector();
        }));
        const LoadBtn = MakeEl('button', 'button export-button', 'Load Preset World (replaces demo masks)');
        LoadBtn.type = 'button';
        LoadBtn.style.width = '100%';
        LoadBtn.style.justifyContent = 'center';
        LoadBtn.addEventListener('click', () =>
        {
            const Built = BuildPresetWorld(this.world, this.sim.presetId, { resolution: 148 });
            this.activeRegionId = Built.region.id;
            this.selectedRegionIds = new Set([Built.region.id]);
            this.sim.group = null;
            for (const R of this.world.regions) R.dirty = true;
            this._evaluateDirtyRegionsNow();
            this.map2D.frameAllRegions?.();
            this.view3D.frameVisibleWorld?.();
            this.refreshChrome();
            this.rebuildInspector();
        });
        PresetGroup.Body.appendChild(LoadBtn);
        Content.appendChild(PresetGroup.Group);

        // ---------- Shared Weather / Solo Mode ----------
        const ModeGroup = MakePropertyGroup('Simulation Grouping', 'Layers', this.sim.shareWeather ? 'SHARED' : 'SOLO', true,
            'Simulate terrains 1-by-1 (solo) — or bind 2+ similar mask layers to ONE shared weather / pattern / seasonal cycle.');
        ModeGroup.Body.appendChild(MakeSelectRow('Simulation Mode', [
            ['Solo · 1-by-1 (active terrain only)', 'solo'],
            ['Shared Weather Group (2+ terrains, same cycle)', 'shared']
        ], this.sim.shareWeather ? 'shared' : 'solo', Val =>
        {
            this.sim.shareWeather = Val === 'shared';
            this.sim.group = null;
            this.rebuildInspector();
        }));
        const Targets = this._simTargetRegions();
        const ShareInfo = MakeEl('div', 'studio-btn-row');
        ShareInfo.appendChild(MakeEl('span', 'dock-readout', `Members: ${Targets.map(R => R.name).join(' · ') || '—'}`));
        ModeGroup.Body.appendChild(ShareInfo);
        Content.appendChild(ModeGroup.Group);

        // ---------- Seasonal Climate ----------
        const ClimGroup = MakePropertyGroup('Seasonal Climate Forcing', 'Sun', 'DAY-OF-YEAR', true,
            'Latitude + 9 climate regimes drive temperature, rain-vs-snow partitioning, freeze-thaw nights, spring melt floods & dry-season wind cycles.');
        ClimGroup.Body.appendChild(MakeSelectRow('Climate Regime', CLIMATE_REGIMES.map(R => [R.label, R.id]), this.sim.climateRegimeId, Val =>
        {
            this.sim.climateRegimeId = Val;
            this.sim.group = null;
            this._simRefreshHud();
        }));
        ClimGroup.Body.appendChild(MakeSliderPillRow('Latitude', -82, 82, 1, this.sim.latitudeDeg, 0, '°', V =>
        {
            this.sim.latitudeDeg = V;
            this.sim.group = null;
        }));
        ClimGroup.Body.appendChild(MakeSliderPillRow('Start Day of Year', 1, 365, 1, this.sim.startDay, 0, 'doy', V =>
        {
            this.sim.startDay = V;
            this.sim.group = null;
        }));
        ClimGroup.Body.appendChild(MakeSliderPillRow('Playback Speed', 1, 40, 1, this.sim.stepRate, 0, 'd/s', V => { this.sim.stepRate = V; }));
        Content.appendChild(ClimGroup.Group);

        // ---------- Transport ----------
        const RunGroup = MakePropertyGroup('Erosion Simulation Transport', 'Layers', 'LIVE / BAKE', true,
            'Play live day-by-day, single-step, or bake whole years in one click. All six physics processes run every simulated day.');
        const Row1 = MakeEl('div', 'studio-btn-row');
        const PlayBtn = MakeEl('button', 'button export-button' + (this.sim.playing ? ' accent' : ''), this.sim.playing ? '⏸ Pause' : '▶ Play');
        PlayBtn.type = 'button';
        PlayBtn.addEventListener('click', () =>
        {
            this.sim.playing = !this.sim.playing;
            this.sim.accumulator = 0;
            this.rebuildInspector();
        });
        const StepBtn = MakeEl('button', 'button', '+1 Day');
        StepBtn.type = 'button';
        StepBtn.addEventListener('click', () => { this._simStepDays(1); this.rebuildInspector(); });
        const MonthBtn = MakeEl('button', 'button', '+30 Days');
        MonthBtn.type = 'button';
        MonthBtn.addEventListener('click', () => { this._simStepDays(30); this.rebuildInspector(); });
        Row1.append(PlayBtn, StepBtn, MonthBtn);
        RunGroup.Body.appendChild(Row1);

        const Row2 = MakeEl('div', 'studio-btn-row');
        const BakeInput = MakeEl('input', 'pill-range');
        BakeInput.type = 'number'; BakeInput.min = '1'; BakeInput.max = '200'; BakeInput.value = String(this.sim.bakeYears);
        BakeInput.style.width = '68px';
        BakeInput.addEventListener('change', E => { this.sim.bakeYears = Clamp(Number(E.target.value) || 1, 1, 200); });
        const BakeBtn = MakeEl('button', 'button export-button accent', 'Bake Years');
        BakeBtn.type = 'button';
        BakeBtn.addEventListener('click', () =>
        {
            this.sim.playing = false;
            this._simBakeYears(this.sim.bakeYears);
            this.rebuildInspector();
        });
        const ResetBtn = MakeEl('button', 'button', 'Reset');
        ResetBtn.type = 'button';
        ResetBtn.addEventListener('click', () => { this._simReset(); this.rebuildInspector(); });
        Row2.append(MakeEl('span', 'dock-readout', 'Years:'), BakeInput, BakeBtn, ResetBtn);
        RunGroup.Body.appendChild(Row2);

        if (Region?.runtime?.erosionStale)
        {
            const StaleRow = MakeEl('div', 'studio-btn-row');
            const ReBakeBtn = MakeEl('button', 'button export-button accent', '⚠ Upstream Layers Changed — Re-Bake Clean Erosion');
            ReBakeBtn.type = 'button';
            ReBakeBtn.style.width = '100%';
            ReBakeBtn.style.justifyContent = 'center';
            ReBakeBtn.addEventListener('click', () =>
            {
                this.pushUndoSnapshot();
                Region.stack = Region.stack.filter(L => L.name !== 'Erosion Baked Surface');
                Region.erosionLayer = null;
                Region.erosionBaseSig = null;
                EvaluateTerrainRegion(this.world, Region, this.activeLayerId);
                this.sim.group = null;
                this._simBakeYears(this.sim.bakeYears);
                this.rebuildInspector();
            });
            StaleRow.appendChild(ReBakeBtn);
            RunGroup.Body.appendChild(StaleRow);
        }

        Content.appendChild(RunGroup.Group);

        // ---------- Seasonal HUD ----------
        const HudGroup = MakePropertyGroup('Seasonal Calendar HUD', 'Layers', 'LIVE WEATHER', true);
        const HudGrid = MakeEl('div', 'sim-hud-grid');
        const HudItem = (Id, Label, Initial) =>
        {
            const Cell = MakeEl('div', 'sim-hud-cell');
            Cell.append(MakeEl('small', '', Label), MakeEl('b', 'tabular', Initial));
            Cell.querySelector('b').id = Id;
            return Cell;
        };
        HudGrid.append(
            HudItem('sim-hud-date', 'Calendar', 'Day 120 · Year 1'),
            HudItem('sim-hud-season', 'Season', 'Spring'),
            HudItem('sim-hud-temp', 'Temp', '—'),
            HudItem('sim-hud-precip', 'Precip', '—'),
            HudItem('sim-hud-wind', 'Wind', '—'),
            HudItem('sim-hud-regime', 'Regime', '—'),
            HudItem('sim-hud-mode', 'Mode', 'Solo · 1-by-1'),
            HudItem('sim-hud-day', 'Baked', '0 sim-days'),
            HudItem('sim-hud-stats', 'Erosion Totals', '—')
        );
        HudGroup.Body.appendChild(HudGrid);
        Content.appendChild(HudGroup.Group);
        this._simRefreshHud();
    }

    /*--------------------------------------------------------------------------------------------------------------------------------------
                            TAB 6: GAEA TEXTURING · DATA MAPS · STAMPS
    --------------------------------------------------------------------------------------------------------------------------------------*/
    _buildTexturingTab(Content, Region)
    {
        // ---------- Data Map Lens ----------
        const MapGroup = MakePropertyGroup('Gaea Data Maps', 'Layers', `${DATA_MAP_CATALOG.length} MAPS`, true,
            'Simulated & derived maps — click any map to view it as a false-color diagnostic lens in the 3D viewport.');
        const MapList = MakeEl('div', 'data-map-grid');
        for (const Meta of DATA_MAP_CATALOG)
        {
            const Btn = MakeEl('button', 'data-map-btn' + (this.sim.mapLensId === Meta.id ? ' active' : ''));
            Btn.type = 'button';
            Btn.title = `${Meta.label} (${Meta.category})`;
            const Dot = MakeEl('span', 'data-map-dot');
            Dot.style.background = Meta.color;
            Btn.append(Dot, MakeEl('span', '', Meta.label));
            Btn.addEventListener('click', () =>
            {
                this.sim.mapLensId = this.sim.mapLensId === Meta.id ? null : Meta.id;
                if (this.sim.mapLensId)
                {
                    this._applyMapLens();
                    const Sel = document.getElementById('lens-select');
                    if (Sel) Sel.value = `map:${Meta.id}`;
                }
                else
                {
                    this._clearMapLens();
                    const Sel = document.getElementById('lens-select');
                    if (Sel) Sel.value = '0';
                }
                this.rebuildInspector();
            });
            MapList.appendChild(Btn);
        }
        MapGroup.Body.appendChild(MapList);

        const ExpRow = MakeEl('div', 'studio-btn-row');
        const PngBtn = MakeEl('button', 'button', 'Export Lens Map .PNG');
        PngBtn.type = 'button';
        PngBtn.addEventListener('click', () => this._exportDataMap('png'));
        const RawBtn = MakeEl('button', 'button', 'Export .RAW Float32');
        RawBtn.type = 'button';
        RawBtn.addEventListener('click', () => this._exportDataMap('raw'));
        ExpRow.append(PngBtn, RawBtn);
        MapGroup.Body.appendChild(ExpRow);
        Content.appendChild(MapGroup.Group);

        // ---------- SatMap Ramps + Surface Preview ----------
        const RampGroup = MakePropertyGroup('SatMap Color Ramps & Surface Stack', 'Sun', 'MULTI-STOP', true,
            'Multi-stop satellite ramps combined with slope shading, chemical stains, snow/ice, sand sheets & stamps.');
        RampGroup.Body.appendChild(MakeSelectRow('SatMap Ramp', SATMAP_RAMPS.map(R => [R.label, R.id]), this.sim.rampId, Val =>
        {
            this.sim.rampId = Val;
        }));
        const SurfBtn = MakeEl('button', 'button export-button', 'Export Composed Surface .PNG');
        SurfBtn.type = 'button';
        SurfBtn.style.width = '100%';
        SurfBtn.style.justifyContent = 'center';
        SurfBtn.addEventListener('click', () =>
        {
            const R = this.world.getRegion?.(this.activeRegionId) || this.world.regions[0];
            if (!R?.runtime) return;
            const Colors = ComposeSurfaceColors(R, R.runtime.erosion || null, { rampId: this.sim.rampId, stampId: this.sim.stampId });
            const Png = ExportSurfacePngBytes(Colors, R.runtime.nx, R.runtime.nz);
            this._downloadBytes(Png, `surface-${R.id}-${this.sim.rampId}.png`, 'image/png');
        });
        RampGroup.Body.appendChild(SurfBtn);
        Content.appendChild(RampGroup.Group);

        // ---------- Geological Surface Stamps ----------
        const StampGroup = MakePropertyGroup('Procedural Geological Stamps', 'Mountain', `${STAMP_CATALOG.length} STAMPS`, true,
            'Gaea-style surface stamps: cross-bedded sandstone, hexagonal basalt, tafoni pits, braided alluvium, ripples, scree, grikes, playa cracks, striations.');
        StampGroup.Body.appendChild(MakeSelectRow('Surface Stamp', STAMP_CATALOG.map(S => [S.label, S.id]), this.sim.stampId, Val =>
        {
            this.sim.stampId = Val;
        }));
        StampGroup.Body.appendChild(MakeSliderPillRow('Stamp Strength', 0.0, 2.0, 0.05, this.sim.stampStrength, 2, '×', V => { this.sim.stampStrength = V; }));
        const ApplyBtn = MakeEl('button', 'button export-button', 'Apply Stamp as Persistent Sculpt Layer');
        ApplyBtn.type = 'button';
        ApplyBtn.style.width = '100%';
        ApplyBtn.style.justifyContent = 'center';
        ApplyBtn.addEventListener('click', () =>
        {
            this.pushUndoSnapshot();
            this._applyStampAsSculptLayer(Region);
            this.refreshChrome();
        });
        StampGroup.Body.appendChild(ApplyBtn);
        Content.appendChild(StampGroup.Group);
    }

    _applyStampAsSculptLayer(Region)
    {
        if (!Region.runtime) return;
        const Rt = Region.runtime;
        const Nx = Rt.nx, Nz = Rt.nz;
        const Meta = STAMP_CATALOG.find(S => S.id === this.sim.stampId) || STAMP_CATALOG[0];
        // Persistent sculpt layer so the stamp survives stack re-evaluation.
        let Layer = Region.stack.find(L => L.type === 'sculpt' && L.name === `Stamp · ${Meta.label}`);
        if (!Layer)
        {
            Layer = CreateTerrainLayer('sculpt', {
                name: `Stamp · ${Meta.label}`,
                combine: 'add',
                falloff: { mode: 'none', distance: 100, edgeFloor: 1.0 }
            });
            Region.stack.push(Layer);
        }
        if (!Layer.deltas || Layer.deltaRes !== Nx * Nz)
        {
            Layer.deltas = new Float32Array(Nx * Nz);
            Layer.deltaRes = Nx * Nz;
        }
        const B = Region.bounds;
        for (let Z = 0; Z < Nz; ++Z)
        {
            for (let X = 0; X < Nx; ++X)
            {
                const I = Z * Nx + X;
                if (Rt.sdf2D[I] > 0) continue;
                const S = SampleStamp(this.sim.stampId, B.x + X * Rt.stepX, B.z + Z * Rt.stepZ, {});
                Layer.deltas[I] += S.height * this.sim.stampStrength;
            }
        }
        Layer.strokeCount = (Layer.strokeCount || 0) + 1;
        Region.dirty = true;
        Region.meshDirty = true;
        this._scheduleDirtyRegionsEvaluation();
    }

    _exportDataMap(Format)
    {
        const R = this.world.getRegion?.(this.activeRegionId) || this.world.regions[0];
        if (!R?.runtime) return;
        const Maps = ComputeDataMaps(R, R.runtime.erosion || null);
        const MapId = this.sim.mapLensId || 'altitude';
        const M = Maps[MapId];
        if (!M) return;
        if (Format === 'png')
        {
            const Png = ExportMapPngBytes(M.data, R.runtime.nx, R.runtime.nz, R.runtime.sdf2D);
            this._downloadBytes(Png, `map-${MapId}-${R.id}.png`, 'image/png');
        }
        else
        {
            const Raw = ExportMapRawBytes(M.data);
            this._downloadBytes(Raw, `map-${MapId}-${R.id}.raw`, 'application/octet-stream');
        }
    }

    _downloadBytes(Bytes, Filename, Mime)
    {
        const Blob_ = new Blob([Bytes], { type: Mime });
        const Url = URL.createObjectURL(Blob_);
        const A = document.createElement('a');
        A.href = Url;
        A.download = Filename;
        document.body.appendChild(A);
        A.click();
        setTimeout(() => { URL.revokeObjectURL(Url); A.remove(); }, 800);
    }

    /*======================================================================================================================================
                             PURE-SDF BRITTLE FRACTURE, THERMAL CRAZING & QEF DUAL-CONTOUR LAB CONTROLLER
    ======================================================================================================================================*/
    _rebuildFractureFormationNow(Reframe = false, StructuralChanged = true, CutawayOnly = false)
    {
        if (!this.fracture) return;
        let F = this.fracture.formation;
        if (StructuralChanged || !F)
        {
            F = buildFracturedFormation(this.fracture.params);
            this.fracture.formation = F;
        }
        else
        {
            F.params = { ...F.params, ...this.fracture.params };
            const Az = (F.params.sunAzimuthDeg ?? 48) * (Math.PI / 180);
            const El = (F.params.sunElevationDeg ?? 40) * (Math.PI / 180);
            F.sunDir = [Math.cos(El) * Math.sin(Az), Math.sin(El), Math.cos(El) * Math.cos(Az)];
        }
        this.fracture.qefDirty = true;

        if (this.fractureView)
        {
            this.fractureView.setFormation(F, StructuralChanged, CutawayOnly);
            if (Reframe) this.fractureView.frameFormation();
        }

        const Preset = getFracturePreset(this.fracture.params.presetId);
        const HudName = document.getElementById('hud-fracture-preset');
        if (HudName) HudName.textContent = Preset.name;
        const HudBlk = document.getElementById('hud-frac-blocks');
        if (HudBlk) HudBlk.textContent = String(F.blocks.length);

        if (this.fractureView?.renderMode === 'qef-mesh')
        {
            this._scheduleFractureQefBake(150);
        }
        else
        {
            const HudTris = document.getElementById('hud-frac-tris');
            if (HudTris)
            {
                HudTris.textContent = this.fracture.qefStats
                    ? `${(this.fracture.qefStats.triangles / 1000).toFixed(1)}k`
                    : 'GPU SDF';
            }
        }
    }

    _scheduleFractureQefBake(DelayMs = 150)
    {
        if (this.fracture._qefTimer) clearTimeout(this.fracture._qefTimer);
        this.fracture._qefTimer = setTimeout(() =>
        {
            this.fracture._qefTimer = null;
            this._bakeFractureQefMeshNow();
            if (this.inspectorTab === 'fracture') this.rebuildInspector();
            this.refreshChrome();
        }, DelayMs);
    }

    _bakeFractureQefMeshNow(ResOverride = null)
    {
        if (!this.fracture?.formation) this._rebuildFractureFormationNow(false);
        const Res = ResOverride ?? this.fracture.params.meshResolution ?? 72;
        const Baked = bakeFracturedFormationQefMesh(this.fracture.formation, { resolution: Res });
        this.fracture.qefMesh = Baked.mesh;
        this.fracture.qefStats = Baked.sdfStats;
        this.fracture.qefDirty = false;
        this.fractureView?.uploadQefMesh(Baked.mesh, Baked.sdfStats);

        const HudTris = document.getElementById('hud-frac-tris');
        if (HudTris) HudTris.textContent = `${(Baked.sdfStats.triangles / 1000).toFixed(1)}k`;
        return Baked;
    }

    _selectFracturePreset(PresetId)
    {
        const KeepCutaway = !!this.fracture.params.cutawayEnabled;
        this.fracture.params = createDefaultFractureParams(PresetId);
        this.fracture.params.cutawayEnabled = KeepCutaway;
        this.fracture.savedOpenState = null;
        this.fracture.qefStats = null;
        this._rebuildFractureFormationNow(true);
        if (this.fractureView?.renderMode === 'qef-mesh')
        {
            this._bakeFractureQefMeshNow();
        }
        this._syncFractureToolbarButtons();
        this._buildPresetStrip();
        this.refreshChrome();
        this.rebuildInspector();
    }

    _syncFractureToolbarButtons()
    {
        const IsQef = this.fractureView?.renderMode === 'qef-mesh';
        document.getElementById('frac-mode-ray-btn')?.classList.toggle('active', !IsQef);
        document.getElementById('frac-mode-qef-btn')?.classList.toggle('active', !!IsQef);
        const WireBtn = document.getElementById('frac-wire-btn');
        if (WireBtn)
        {
            WireBtn.style.display = IsQef ? '' : 'none';
            WireBtn.classList.toggle('active', !!(IsQef && this.fractureView?.showWireframe));
        }
        document.getElementById('frac-cutaway-btn')?.classList.toggle('cutaway-on', !!this.fracture?.params?.cutawayEnabled);
        const LensSel = document.getElementById('frac-lens-select');
        if (LensSel && this.fractureView) LensSel.value = String(this.fractureView.lensMode);
    }

    _bindFractureViewportToolbar()
    {
        document.getElementById('frac-mode-ray-btn')?.addEventListener('click', () =>
        {
            if (!this.fractureView) return;
            this.fractureView.renderMode = 'raymarch';
            this._syncFractureToolbarButtons();
            this.rebuildInspector();
            this.refreshChrome();
        });

        document.getElementById('frac-mode-qef-btn')?.addEventListener('click', () =>
        {
            if (!this.fractureView) return;
            this.fractureView.renderMode = 'qef-mesh';
            if (this.fracture.qefDirty || !this.fracture.qefMesh)
            {
                this._bakeFractureQefMeshNow();
            }
            this._syncFractureToolbarButtons();
            this.rebuildInspector();
            this.refreshChrome();
        });

        document.getElementById('frac-wire-btn')?.addEventListener('click', () =>
        {
            if (!this.fractureView) return;
            if (this.fractureView.renderMode !== 'qef-mesh')
            {
                this.fractureView.renderMode = 'qef-mesh';
                if (this.fracture.qefDirty || !this.fracture.qefMesh) this._bakeFractureQefMeshNow();
                this.fractureView.showWireframe = true;
            }
            else
            {
                this.fractureView.showWireframe = !this.fractureView.showWireframe;
            }
            this._syncFractureToolbarButtons();
            this.rebuildInspector();
        });

        document.getElementById('frac-cutaway-btn')?.addEventListener('click', () =>
        {
            this.fracture.params.cutawayEnabled = !this.fracture.params.cutawayEnabled;
            this._rebuildFractureFormationNow(false, false, true);
            this._syncFractureToolbarButtons();
            this.rebuildInspector();
        });

        document.getElementById('frac-lens-select')?.addEventListener('change', E =>
        {
            if (!this.fractureView) return;
            this.fractureView.lensMode = Number(E.target.value) | 0;
        });
    }

    _exportFractureAsset(Format = 'glb')
    {
        if (this.fracture.qefDirty || !this.fracture.qefMesh)
        {
            this._bakeFractureQefMeshNow();
        }
        const Mesh = this.fracture.qefMesh;
        if (!Mesh) return;
        const BaseName = `pure-sdf-${this.fracture.params.presetId}-${this.fracture.params.seed}`;
        if (Format === 'obj')
        {
            const Text = exportFractureMeshObj(Mesh, BaseName);
            this._triggerDownload(new Blob([Text], { type: 'text/plain' }), `${BaseName}.obj`);
        }
        else if (Format === 'stl')
        {
            const Bytes = exportFractureMeshBinaryStl(Mesh, BaseName);
            this._triggerDownload(new Blob([Bytes], { type: 'application/octet-stream' }), `${BaseName}.stl`);
        }
        else
        {
            const Bytes = exportFractureMeshGlb(Mesh, this.fracture.params, this.fracture.qefStats || {});
            this._triggerDownload(new Blob([Bytes], { type: 'model/gltf-binary' }), `${BaseName}.glb`);
        }
    }

    _stampFractureToActiveTerrain()
    {
        const Reg = this.getActiveRegion();
        if (!Reg) return;
        this.pushUndoSnapshot();

        const Preset = getFracturePreset(this.fracture.params.presetId);
        const B = Reg.bounds;
        // Find highest central highland spot inside `Reg` so the fractured monolith crowns the terrain dramatically!
        let BestWx = B.x + B.width * 0.50;
        let BestWz = B.z + B.depth * 0.50;
        if (Reg.runtime?.heights)
        {
            const Rt = Reg.runtime;
            let BestH = -1e9;
            for (let Z = Math.floor(Rt.nz * 0.25); Z < Math.floor(Rt.nz * 0.75); Z += 3)
            {
                for (let X = Math.floor(Rt.nx * 0.25); X < Math.floor(Rt.nx * 0.75); X += 3)
                {
                    const Idx = Z * Rt.nx + X;
                    if (Rt.sdf2D[Idx] < -40 && Rt.heights[Idx] > BestH)
                    {
                        BestH = Rt.heights[Idx];
                        BestWx = Math.round(B.x + X * Rt.stepX);
                        BestWz = Math.round(B.z + Z * Rt.stepZ);
                    }
                }
            }
        }

        const RockLayer = CreateTerrainLayer('rock_formation', {
            name: `SDF Cliff · ${Preset.name.replace(/^\d+\s*·\s*/, '')}`,
            worldX: BestWx,
            worldZ: BestWz,
            worldScaleM: 185.0,
            yawDeg: 18.0,
            embedDepthM: 9.0,
            fractureParams: {
                ...this.fracture.params,
                cutawayEnabled: false,
                meshResolution: 68
            }
        });

        Reg.stack.push(RockLayer);
        this.activeLayerId = RockLayer.id;
        EvaluateTerrainRegion(this.world, Reg, RockLayer.id);
        Reg.meshDirty = true;

        this.inspectorTab = 'layers';
        this._syncInspectorTabButtons();
        this.setWorkspaceMode('3d');
        this.view3D.viewScope = 'solo';
        const ScopeSel = document.getElementById('scope-select');
        if (ScopeSel) ScopeSel.value = 'solo';
        this.view3D.frameRegion(Reg);
        this.refreshChrome();
        this.rebuildInspector();
    }

    _buildFractureOutliner()
    {
        const F = this.fracture.formation;
        const Stats = this.fracture.qefStats;
        const Preset = getFracturePreset(this.fracture.params.presetId);

        const LeftTitle = document.getElementById('left-panel-title');
        if (LeftTitle) LeftTitle.textContent = 'Outliner';
        const StatLbl1 = document.getElementById('stat-label-1');
        const StatLbl2 = document.getElementById('stat-label-2');
        if (StatLbl1) StatLbl1.textContent = 'Rock Blocks';
        if (StatLbl2) StatLbl2.textContent = 'SDF Planes';
        const SearchInp = document.getElementById('scene-search');
        if (SearchInp) SearchInp.placeholder = 'Search rock formations…';
        const LeftFoot = document.getElementById('left-footer-label');
        if (LeftFoot) LeftFoot.textContent = 'Pure-SDF Rock Formation · 0 Mesh→SDF';
        const StageTitle = document.getElementById('stage-bar-title');
        if (StageTitle) StageTitle.textContent = 'Rock Formation';
        const InspFootL = document.getElementById('inspector-foot-left');
        const InspFootR = document.getElementById('inspector-foot-right');
        if (InspFootL) InspFootL.textContent = 'Pure-SDF Rock Formation · 0 Mesh→SDF';
        if (InspFootR) InspFootR.textContent = 'QEF Dual-Contour Export';

        document.getElementById('stat-terrains-on').textContent = String(F ? F.blocks.length : 0);
        document.getElementById('stat-layers-total').textContent = String(F ? F.totalPlanes : 0);
        document.getElementById('outliner-subtitle').textContent = `${FRACTURE_PRESETS.length} Rock Formations · Pure SDF`;

        const Outliner = document.getElementById('OutlinerContent');
        Outliner.innerHTML = '';

        const Group = MakeEl('div', 'tree-group');
        Group.appendChild(MakeEl('div', 'tree-group-title', [
            ConstructGlyph('Boxes'),
            MakeEl('span', '', 'Rock Formations'),
            MakeEl('span', 'group-count', String(FRACTURE_PRESETS.length))
        ]));

        const Q = (this.outlinerSearch || '').toLowerCase();
        for (const P of FRACTURE_PRESETS)
        {
            if (Q && !P.name.toLowerCase().includes(Q) && !P.subtitle.toLowerCase().includes(Q)) continue;
            const IsSel = P.id === this.fracture.params.presetId;
            const Card = MakeEl('button', 'fracture-preset-card' + (IsSel ? ' selected' : ''));
            Card.type = 'button';
            Card.append(
                MakeEl('div', 'fracture-preset-head', [
                    MakeEl('strong', '', P.name),
                    MakeEl('span', 'preset-badge', P.badge)
                ]),
                MakeEl('small', '', P.subtitle)
            );
            Card.addEventListener('click', () => this._selectFracturePreset(P.id));
            Group.appendChild(Card);
        }
        Outliner.appendChild(Group);

        const LithName = (Preset.lithology || 'granite').replace(/([A-Z])/g, ' $1').toUpperCase();

        // Sync Right Object Header & Bottom Status Bar
        document.getElementById('inspector-crumb').textContent = 'Inspector / Rock Formation';
        const NameInp = document.getElementById('object-name');
        if (NameInp)
        {
            NameInp.readOnly = true;
            NameInp.value = Preset.name;
        }
        document.getElementById('object-type').textContent =
            `${LithName} · ${F ? F.blocks.length : 0} BLOCKS · ${F ? F.totalPlanes : 0} SDF PLANES`;

        document.getElementById('status-main').textContent = `Rock Formation · ${Preset.name}`;
        document.getElementById('status-tiles').textContent = Stats
            ? `4³ Active Bricks: ${Stats.activeBricks}/${Stats.totalBricks} (${Stats.brickOccupancyPct}%)`
            : `128³ GPU Pure-SDF Volume · ${F ? F.blocks.length : 0} Polyhedral Blocks`;
        document.getElementById('status-slab').textContent =
            `Lithology: ${LithName} · Scale: ${(this.fracture.params.scale ?? 7.0).toFixed(1)} m`;
    }

    _buildFractureInspectorTab(Content)
    {
        const P = this.fracture.params;
        const F = this.fracture.formation;
        const Stats = this.fracture.qefStats;
        const IsQef = this.fractureView?.renderMode === 'qef-mesh';
        const Preset = getFracturePreset(P.presetId);
        const LithLabel = (Preset.lithology || 'granite').replace(/([A-Z])/g, ' $1');

        // 1. Top KPI Trio (#eff4cf highlight card)
        const Trio = MakeEl('div', 'kpi-trio', [
            MakeEl('div', 'kpi-card highlight', [
                MakeEl('span', 'kpi-label', 'Rock Blocks'),
                MakeEl('strong', 'kpi-val', String(F ? F.blocks.length : 0)),
                MakeEl('span', 'kpi-sub', `${F ? F.totalPlanes : 0} SDF planes`)
            ]),
            MakeEl('div', 'kpi-card', [
                MakeEl('span', 'kpi-label', IsQef ? 'QEF Triangles' : 'Render Mode'),
                MakeEl('strong', 'kpi-val', Stats ? `${(Stats.triangles / 1000).toFixed(1)}k` : '60 FPS'),
                MakeEl('span', 'kpi-sub', Stats ? `${Stats.bakeMs} ms QEF bake` : '128³ GPU SDF')
            ]),
            MakeEl('div', 'kpi-card', [
                MakeEl('span', 'kpi-label', 'Lithology'),
                MakeEl('strong', 'kpi-val', Preset.badge.split(' ')[0]),
                MakeEl('span', 'kpi-sub', LithLabel.charAt(0).toUpperCase() + LithLabel.slice(1))
            ])
        ]);
        Content.appendChild(Trio);

        // 2. Quick-Toggle Feature Tiles (only show active/functional toggles!)
        const Switches = MakeEl('div', 'property-switches');
        const MakeTile = (Label, Glyph, IsOn, OnClick) =>
        {
            const B = MakeEl('button', 'property-switch' + (IsOn ? '' : ' is-off'));
            B.type = 'button';
            B.append(
                MakeEl('span', 'switch-icon', ConstructGlyph(Glyph)),
                MakeEl('span', 'switch-name', Label),
                MakeEl('span', 'switch-state', IsOn ? 'ON' : 'OFF')
            );
            B.addEventListener('click', OnClick);
            return B;
        };
        const IsSplitActive = (P.splitMode ?? 'single') !== 'none';
        const HasSplitMotion = IsSplitActive && (Preset.defaults.chasmAperture > 0.001 || Preset.defaults.hingeTiltDeg > 0.01 || Preset.defaults.faultHeave > 0.001);
        const IsClosedJigsaw = (P.chasmAperture <= 0.001 && P.hingeTiltDeg <= 0.01 && P.faultHeave <= 0.001);

        Switches.appendChild(MakeTile('QEF Mesh', 'Boxes', !!IsQef, () =>
        {
            document.getElementById(IsQef ? 'frac-mode-ray-btn' : 'frac-mode-qef-btn')?.click();
        }));
        if (IsQef)
        {
            Switches.appendChild(MakeTile('Wireframe', 'Scan', !!this.fractureView?.showWireframe, () =>
            {
                document.getElementById('frac-wire-btn')?.click();
            }));
        }
        Switches.appendChild(MakeTile('Cutaway', 'Layers', !!P.cutawayEnabled, () =>
        {
            document.getElementById('frac-cutaway-btn')?.click();
        }));
        if (HasSplitMotion)
        {
            Switches.appendChild(MakeTile('Close Gaps', 'Focus', IsClosedJigsaw, () =>
            {
                if (!IsClosedJigsaw)
                {
                    this.fracture.savedOpenState = {
                        chasmAperture: P.chasmAperture,
                        hingeTiltDeg: P.hingeTiltDeg,
                        faultHeave: P.faultHeave
                    };
                    P.chasmAperture = 0.0;
                    P.hingeTiltDeg = 0.0;
                    P.faultHeave = 0.0;
                }
                else
                {
                    const Saved = this.fracture.savedOpenState || Preset.defaults;
                    P.chasmAperture = Saved.chasmAperture ?? Preset.defaults.chasmAperture ?? 0.50;
                    P.hingeTiltDeg = Saved.hingeTiltDeg ?? Preset.defaults.hingeTiltDeg ?? 3.0;
                    P.faultHeave = Saved.faultHeave ?? Preset.defaults.faultHeave ?? 0.25;
                }
                this._rebuildFractureFormationNow(false);
                this.rebuildInspector();
                this.refreshChrome();
            }));
        }
        Content.appendChild(Switches);

        const OnChangeStructural = () =>
        {
            this._rebuildFractureFormationNow(false, true);
        };
        const OnChangeShader = () =>
        {
            this._rebuildFractureFormationNow(false, false);
        };
        const OnCommit = () =>
        {
            this.refreshChrome();
        };

        // 3A. Rock Formation Preset & Seed
        const PresetGroup = MakePropertyGroup(
            'Rock Formation & Seed',
            'Mountain',
            Preset.badge,
            true,
            Preset.subtitle
        );
        PresetGroup.Body.appendChild(MakeSelectRow(
            'Rock Formation',
            FRACTURE_PRESETS.map(Pr => [Pr.name, Pr.id]),
            P.presetId,
            Val => this._selectFracturePreset(Val)
        ));

        const SeedActionRow = MakeEl('div', 'studio-btn-row');
        const SeedBtn = MakeEl('button', 'button', `New Seed (#${P.seed})`);
        SeedBtn.type = 'button';
        SeedBtn.addEventListener('click', () =>
        {
            P.seed = Math.floor(1000 + Math.random() * 89999);
            this._rebuildFractureFormationNow(false, true);
            if (this.fractureView?.renderMode === 'qef-mesh') this._bakeFractureQefMeshNow();
            this.rebuildInspector();
            this.refreshChrome();
        });
        const ResetRockBtn = MakeEl('button', 'button', 'Reset Defaults');
        ResetRockBtn.type = 'button';
        ResetRockBtn.addEventListener('click', () =>
        {
            this._selectFracturePreset(P.presetId);
        });
        SeedActionRow.append(SeedBtn, ResetRockBtn);
        PresetGroup.Body.appendChild(SeedActionRow);
        Content.appendChild(PresetGroup.Group);

        // 3B. Unified Pure-SDF Base Rock Form Generator (available across ALL rock formations!)
        const BaseFormOptions = Preset.isBasaltColonnade ? BASE_FORM_SHAPES_BASALT : BASE_FORM_SHAPES_POLY;
        const BaseFormGroup = MakePropertyGroup(
            'Base Rock Form Generator',
            'Sliders',
            'BASE FORM',
            true,
            Preset.isBasaltColonnade
                ? 'Choose stepped cliff, volcanic plug mound, causeway ridge or sea-cove amphitheater — and stretch width, height & length.'
                : 'Choose base boulder/dome/spire/fin/mesa/pedestal form — and freely stretch width, height, length, crown taper & skew.'
        );
        BaseFormGroup.Body.appendChild(MakeSelectRow(
            'Base Form Shape',
            BaseFormOptions,
            P.baseFormShape ?? Preset.defaults.baseFormShape ?? 'default',
            Val =>
            {
                P.baseFormShape = Val;
                OnChangeStructural();
                this.refreshChrome();
            }
        ));
        BaseFormGroup.Body.appendChild(MakeSliderPillRow('Width (X Span)', 0.40, 2.20, 0.02, P.formWidth ?? 1.0, 2, '×', V =>
        {
            P.formWidth = V;
            OnChangeStructural();
        }, OnCommit));
        BaseFormGroup.Body.appendChild(MakeSliderPillRow('Height (Y Tall)', 0.35, 2.00, 0.02, P.formHeight ?? 1.0, 2, '×', V =>
        {
            P.formHeight = V;
            OnChangeStructural();
        }, OnCommit));
        BaseFormGroup.Body.appendChild(MakeSliderPillRow('Length (Z Depth)', 0.40, 2.20, 0.02, P.formLength ?? 1.0, 2, '×', V =>
        {
            P.formLength = V;
            OnChangeStructural();
        }, OnCommit));
        BaseFormGroup.Body.appendChild(MakeSliderPillRow('Crown Taper / Flare', -0.55, 0.75, 0.02, P.formTaper ?? 0.0, 2, 'w', V =>
        {
            P.formTaper = V;
            OnChangeStructural();
        }, OnCommit));
        BaseFormGroup.Body.appendChild(MakeSliderPillRow('Asymmetric Skew', -0.65, 0.65, 0.02, P.formAsymmetry ?? 0.0, 2, 'w', V =>
        {
            P.formAsymmetry = V;
            OnChangeStructural();
        }, OnCommit));
        BaseFormGroup.Body.appendChild(MakeSliderPillRow('Overall Scale', 4.0, 13.0, 0.1, P.scale ?? 7.0, 1, 'm', V =>
        {
            P.scale = V;
            OnChangeStructural();
        }, OnCommit));
        Content.appendChild(BaseFormGroup.Group);

        // 4. Tailored Per-Rock Control Groups (specific to the selected Rock Formation!)
        for (const GrpSpec of (Preset.controlGroups || []))
        {
            const Grp = MakePropertyGroup(
                GrpSpec.title,
                GrpSpec.glyph || 'Sliders',
                GrpSpec.badge || 'ROCK',
                true,
                GrpSpec.hint || ''
            );
            let VisibleCount = 0;
            for (const Ctrl of (GrpSpec.controls || []))
            {
                if (Ctrl.whenSplit && !IsSplitActive) continue;
                if (Ctrl.showForModes && !Ctrl.showForModes.includes(P.splitMode ?? Preset.defaults.splitMode)) continue;

                VisibleCount++;
                if (Ctrl.type === 'select')
                {
                    const CurSel = P[Ctrl.key] ?? Preset.defaults[Ctrl.key];
                    Grp.Body.appendChild(MakeSelectRow(
                        Ctrl.label,
                        Ctrl.options,
                        CurSel,
                        Val =>
                        {
                            P[Ctrl.key] = Val;
                            if (Ctrl.structural) OnChangeStructural();
                            else OnChangeShader();
                            if (Ctrl.rebuildInspector) this.rebuildInspector();
                            this.refreshChrome();
                        }
                    ));
                    continue;
                }

                const CurVal = P[Ctrl.key] ?? Preset.defaults[Ctrl.key] ?? Ctrl.min;
                Grp.Body.appendChild(MakeSliderPillRow(
                    Ctrl.label,
                    Ctrl.min,
                    Ctrl.max,
                    Ctrl.step,
                    CurVal,
                    Ctrl.decimals ?? 2,
                    Ctrl.unit ?? '',
                    V =>
                    {
                        P[Ctrl.key] = V;
                        if (Ctrl.structural) OnChangeStructural();
                        else OnChangeShader();
                    },
                    OnCommit
                ));
            }
            if (VisibleCount > 0) Content.appendChild(Grp.Group);
        }

        // 5. Lighting & Interior Cutaway Slice (Slice Offset shown ONLY when Cutaway is enabled!)
        const LightSliceGroup = MakePropertyGroup(
            'Lighting & Interior Cutaway',
            'Layers',
            P.cutawayEnabled ? 'SLICED' : 'SOLID',
            true,
            'Adjust solar lighting direction, exposure, or slice the 3D SDF volume open to inspect interior cracks & cavities.'
        );
        LightSliceGroup.Body.appendChild(MakeSliderPillRow('Sun Azimuth', 0, 360, 2, P.sunAzimuthDeg ?? 46, 0, '°', V => { P.sunAzimuthDeg = V; OnChangeShader(); }, OnCommit));
        LightSliceGroup.Body.appendChild(MakeSliderPillRow('Sun Elevation', 12, 82, 1, P.sunElevationDeg ?? 42, 0, '°', V => { P.sunElevationDeg = V; OnChangeShader(); }, OnCommit));
        LightSliceGroup.Body.appendChild(MakeSliderPillRow('Viewport Exposure', 0.45, 1.60, 0.02, this.fractureView?.exposure ?? 1.15, 2, 'ev', V =>
        {
            if (this.fractureView) this.fractureView.exposure = V;
        }, OnCommit));
        LightSliceGroup.Body.appendChild(MakeSelectRow('Cutaway Slice', [
            ['Off (Solid Rock Formation)', 'off'],
            ['Slice X-Axis (Cross-Section)', 'x'],
            ['Slice Z-Axis (Cross-Chasm)', 'z']
        ], !P.cutawayEnabled ? 'off' : ((P.cutawayAxis ?? 0) === 0 ? 'x' : 'z'), Val =>
        {
            if (Val === 'off') P.cutawayEnabled = false;
            else { P.cutawayEnabled = true; P.cutawayAxis = Val === 'x' ? 0 : 2; }
            this._syncFractureToolbarButtons();
            this._rebuildFractureFormationNow(false, false, true);
            this.rebuildInspector();
        }));
        if (P.cutawayEnabled)
        {
            LightSliceGroup.Body.appendChild(MakeSliderPillRow('Cutaway Slice Offset', -2.6, 2.6, 0.02, P.cutawayOffset ?? 0.25, 2, 'm', V =>
            {
                P.cutawayOffset = V;
                this._rebuildFractureFormationNow(false, false, true);
            }, OnCommit));
        }
        Content.appendChild(LightSliceGroup.Group);

        // 6. QEF Dual-Contour Mesh & 3D Asset Export (Grid Res slider shown ONLY when QEF Mesh mode is active!)
        const BakeGroup = MakePropertyGroup(
            'QEF Mesh & 3D Export',
            'Boxes',
            IsQef ? `RES ${P.meshResolution}` : 'EXPORT',
            true,
            'Extracts sharp-creased manifold triangles via 4³ Sparse Bricks + 3×3 QEF Dual Contouring for .GLB, .OBJ & .STL export.'
        );
        if (IsQef)
        {
            BakeGroup.Body.appendChild(MakeSliderPillRow('QEF Voxel Grid Res', 44, 116, 4, P.meshResolution ?? 84, 0, 'vx', V =>
            {
                P.meshResolution = V;
                this.fracture.qefDirty = true;
                this._scheduleFractureQefBake(180);
            }, OnCommit));
        }

        const BakeRow = MakeEl('div', 'studio-btn-row');
        const BakeBtn = MakeEl('button', 'button export-button', IsQef ? 'Rebake QEF Mesh' : 'Switch to QEF Mesh');
        BakeBtn.type = 'button';
        BakeBtn.addEventListener('click', () =>
        {
            this._bakeFractureQefMeshNow();
            if (this.fractureView) this.fractureView.renderMode = 'qef-mesh';
            this._syncFractureToolbarButtons();
            this.rebuildInspector();
            this.refreshChrome();
        });
        if (IsQef)
        {
            const RayBtn = MakeEl('button', 'button', 'Back to 60 FPS SDF');
            RayBtn.type = 'button';
            RayBtn.addEventListener('click', () =>
            {
                document.getElementById('frac-mode-ray-btn')?.click();
            });
            BakeRow.append(BakeBtn, RayBtn);
        }
        else
        {
            BakeRow.appendChild(BakeBtn);
        }
        BakeGroup.Body.appendChild(BakeRow);

        const ExpRow = MakeEl('div', 'studio-btn-row');
        const BtnGlb = MakeEl('button', 'button', 'Export .GLB');
        BtnGlb.type = 'button';
        BtnGlb.addEventListener('click', () => this._exportFractureAsset('glb'));
        const BtnObj = MakeEl('button', 'button', 'Export .OBJ');
        BtnObj.type = 'button';
        BtnObj.addEventListener('click', () => this._exportFractureAsset('obj'));
        const BtnStl = MakeEl('button', 'button', 'Export .STL');
        BtnStl.type = 'button';
        BtnStl.addEventListener('click', () => this._exportFractureAsset('stl'));
        ExpRow.append(BtnGlb, BtnObj, BtnStl);
        BakeGroup.Body.appendChild(ExpRow);

        Content.appendChild(BakeGroup.Group);
    }

    /*======================================================================================================================================
                           ROCKFORMATION 2.0 · COMPOSABLE PURE-SDF PLAIN ROCK LAYER STACK CONTROLLER
    ======================================================================================================================================*/
    _bindRock2ViewportToolbar()
    {
        const RayBtn = document.getElementById('rock2-mode-ray-btn');
        const QefBtn = document.getElementById('rock2-mode-qef-btn');
        const WireBtn = document.getElementById('rock2-wire-btn');
        const LensSel = document.getElementById('rock2-lens-select');

        RayBtn?.addEventListener('click', () =>
        {
            if (this.rock2View) this.rock2View.renderMode = 'raymarch';
            this._syncRock2ToolbarButtons();
            if (this.workspaceMode === 'rock2') this.rebuildInspector();
        });

        QefBtn?.addEventListener('click', () =>
        {
            if (this.rock2.qefDirty || !this.rock2.qefMesh) this._bakeRock2QefMeshNow();
            if (this.rock2View) this.rock2View.renderMode = 'qef-mesh';
            this._syncRock2ToolbarButtons();
            if (this.workspaceMode === 'rock2') this.rebuildInspector();
        });

        WireBtn?.addEventListener('click', () =>
        {
            if (!this.rock2View) return;
            if (this.rock2View.renderMode !== 'qef-mesh')
            {
                if (this.rock2.qefDirty || !this.rock2.qefMesh) this._bakeRock2QefMeshNow();
                this.rock2View.renderMode = 'qef-mesh';
            }
            this.rock2View.showWireframe = !this.rock2View.showWireframe;
            this._syncRock2ToolbarButtons();
        });

        LensSel?.addEventListener('change', E =>
        {
            if (this.rock2View) this.rock2View.lens = Number(E.target.value) | 0;
        });

        this._syncRock2ToolbarButtons();
    }

    _syncRock2ToolbarButtons()
    {
        const IsQef = this.rock2View?.renderMode === 'qef-mesh';
        document.getElementById('rock2-mode-ray-btn')?.classList.toggle('active', !IsQef);
        document.getElementById('rock2-mode-qef-btn')?.classList.toggle('active', !!IsQef);
        const WireBtn = document.getElementById('rock2-wire-btn');
        if (WireBtn)
        {
            WireBtn.style.display = IsQef ? '' : 'none';
            WireBtn.classList.toggle('active', !!(IsQef && this.rock2View?.showWireframe));
        }
    }

    _pushRock2UndoSnapshot()
    {
        if (!this.rock2?.stack) return;
        this.rock2.undoStack.push(JSON.stringify(serializeRock2Document(this.rock2.stack)));
        if (this.rock2.undoStack.length > 40) this.rock2.undoStack.shift();
        this.rock2.redoStack.length = 0;
    }

    _restoreRock2StackData(Doc)
    {
        const Restored = deserializeRock2Document(Doc);
        this.rock2.stack = Restored;
        if (!Restored.layers.some(L => L.id === this.rock2.selectedLayerId))
        {
            this.rock2.selectedLayerId = Restored.layers[0]?.id || null;
        }
        this._rebuildRock2StackNow(false);
        this._buildPresetStrip();
        this.refreshChrome();
        this.rebuildInspector();
    }

    _getSelectedRock2Layer()
    {
        const Layers = this.rock2?.stack?.layers || [];
        return Layers.find(L => L.id === this.rock2.selectedLayerId) || Layers[0] || null;
    }

    _selectRock2StarterStack(StarterId)
    {
        this._pushRock2UndoSnapshot();
        const Next = createDefaultRock2Document(StarterId);
        this.rock2.stack = Next;
        this.rock2.selectedLayerId = Next.layers[0]?.id || null;
        this._rebuildRock2StackNow(true);
        this._buildPresetStrip();
        this.refreshChrome();
        this.rebuildInspector();
    }

    _addRock2Layer(NodeType)
    {
        this._pushRock2UndoSnapshot();
        const Stack = this.rock2.stack;
        const Count = Stack.layers.length;
        // Gently offset newly stacked rock layers when > 0 so they are immediately visible in 3D space!
        const Angle = Count * 2.15;
        const OffsetR = Count === 0 ? 0.0 : Math.min(2.2, 0.95 + Count * 0.25);
        const PosX = Count === 0 ? 0.0 : Number((Math.cos(Angle) * OffsetR).toFixed(2));
        const PosZ = Count === 0 ? 0.0 : Number((Math.sin(Angle) * OffsetR).toFixed(2));

        const NewLayer = createRock2Layer(NodeType, {
            combineMode: Count === 0 ? 'union' : 'smooth_union',
            blendRadius: 0.32,
            posX: PosX,
            posY: 0.0,
            posZ: PosZ,
            seed: 101 + Count * 137
        });
        Stack.layers.push(NewLayer);
        this.rock2.selectedLayerId = NewLayer.id;
        this._rebuildRock2StackNow(false);
        this.refreshChrome();
        this.rebuildInspector();
    }

    _moveRock2Layer(LayerId, Dir)
    {
        const Layers = this.rock2.stack.layers;
        const Idx = Layers.findIndex(L => L.id === LayerId);
        const Target = Idx + Dir;
        if (Idx < 0 || Target < 0 || Target >= Layers.length) return;
        this._pushRock2UndoSnapshot();
        const [Moved] = Layers.splice(Idx, 1);
        Layers.splice(Target, 0, Moved);
        this._rebuildRock2StackNow(false);
        this.refreshChrome();
        this.rebuildInspector();
    }

    _toggleSoloRock2Layer(LayerId)
    {
        const Layers = this.rock2.stack.layers;
        const Target = Layers.find(L => L.id === LayerId);
        if (!Target) return;
        this._pushRock2UndoSnapshot();
        const NextSolo = !Target.solo;
        for (const L of Layers) L.solo = false;
        Target.solo = NextSolo;
        if (NextSolo) Target.enabled = true;
        this.rock2.selectedLayerId = Target.id;
        this._rebuildRock2StackNow(false);
        this.refreshChrome();
        this.rebuildInspector();
    }

    _duplicateRock2Layer(LayerId)
    {
        const Layers = this.rock2.stack.layers;
        const Idx = Layers.findIndex(L => L.id === LayerId);
        if (Idx < 0) return;
        this._pushRock2UndoSnapshot();
        const Copy = cloneRock2Layer(Layers[Idx]);
        Copy.params.posX = Number(((Copy.params.posX || 0) + 0.85).toFixed(2));
        Layers.splice(Idx + 1, 0, Copy);
        this.rock2.selectedLayerId = Copy.id;
        this._rebuildRock2StackNow(false);
        this.refreshChrome();
        this.rebuildInspector();
    }

    _deleteRock2Layer(LayerId)
    {
        const Layers = this.rock2.stack.layers;
        if (Layers.length <= 1) return;
        const Idx = Layers.findIndex(L => L.id === LayerId);
        if (Idx < 0) return;
        this._pushRock2UndoSnapshot();
        Layers.splice(Idx, 1);
        if (this.rock2.selectedLayerId === LayerId)
        {
            this.rock2.selectedLayerId = Layers[Math.max(0, Idx - 1)]?.id || Layers[0]?.id || null;
        }
        this._rebuildRock2StackNow(false);
        this.refreshChrome();
        this.rebuildInspector();
    }

    _rebuildRock2StackNow(Reframe = false)
    {
        const Compiled = compileRock2Stack(this.rock2.stack);
        this.rock2.compiled = Compiled;
        this.rock2.qefDirty = true;

        if (this.rock2View)
        {
            this.rock2View.setCompiledStack(Compiled, { reframe: Reframe });
            if (this.rock2View.renderMode === 'qef-mesh')
            {
                this._scheduleRock2QefBake(150);
            }
        }

        const StackLbl = document.getElementById('hud-rock2-stack');
        if (StackLbl) StackLbl.textContent = this.rock2.stack.name || 'Plain Rock Stack';
        const LyrHud = document.getElementById('hud-rock2-layers');
        if (LyrHud) LyrHud.textContent = String(Compiled.compiledLayers.length);
        const BlkHud = document.getElementById('hud-rock2-blocks');
        if (BlkHud) BlkHud.textContent = String(Compiled.primitives.length);
        const TrisHud = document.getElementById('hud-rock2-tris');
        if (TrisHud && this.rock2.qefDirty) TrisHud.textContent = 'Live SDF';
    }

    _scheduleRock2QefBake(DelayMs = 150)
    {
        if (this.rock2._qefTimer) clearTimeout(this.rock2._qefTimer);
        this.rock2._qefTimer = setTimeout(() =>
        {
            this.rock2._qefTimer = null;
            this._bakeRock2QefMeshNow();
        }, DelayMs);
    }

    _bakeRock2QefMeshNow()
    {
        if (!this.rock2.compiled) this._rebuildRock2StackNow(false);
        const Res = this.rock2.meshResolution || this.rock2.stack.meshResolution || 72;
        const { mesh, sdfStats } = bakeRock2StackQefMesh(this.rock2.compiled, { resolution: Res });
        this.rock2.qefMesh = mesh;
        this.rock2.qefStats = sdfStats;
        this.rock2.qefDirty = false;
        this.rock2View?.uploadQefMesh(mesh, sdfStats);

        const TrisHud = document.getElementById('hud-rock2-tris');
        if (TrisHud) TrisHud.textContent = `${(sdfStats.triangles / 1000).toFixed(1)}k`;
        if (this.workspaceMode === 'rock2') this.refreshChrome();
    }

    _exportRock2Asset(Format)
    {
        const SafeSlug = (this.rock2.stack.name || 'rockformation-2-stack')
            .toLowerCase()
            .replace(/[^a-z0-9]+/g, '-')
            .replace(/^-|-$/g, '') || 'rockformation-2-stack';

        if (Format === 'json')
        {
            const JsonText = JSON.stringify(serializeRock2Document(this.rock2.stack), null, 2);
            this._triggerDownload(new Blob([JsonText], { type: 'application/json' }), `${SafeSlug}.rock2.json`);
            return;
        }

        if (this.rock2.qefDirty || !this.rock2.qefMesh)
        {
            this._bakeRock2QefMeshNow();
        }
        const Mesh = this.rock2.qefMesh;
        if (!Mesh || !Mesh.indices.length) return;

        if (Format === 'glb')
        {
            const Bytes = exportFractureMeshGlb(Mesh, { name: SafeSlug, format: 'frontier-rockformation-2.0' }, this.rock2.qefStats || {});
            this._triggerDownload(new Blob([Bytes], { type: 'model/gltf-binary' }), `${SafeSlug}.glb`);
        }
        else if (Format === 'stl')
        {
            const Bytes = exportFractureMeshBinaryStl(Mesh, SafeSlug);
            this._triggerDownload(new Blob([Bytes], { type: 'application/octet-stream' }), `${SafeSlug}.stl`);
        }
        else
        {
            const Text = exportFractureMeshObj(Mesh, SafeSlug);
            this._triggerDownload(new Blob([Text], { type: 'text/plain' }), `${SafeSlug}.obj`);
        }
    }

    _buildRock2Outliner()
    {
        const Stack = this.rock2.stack;
        const Compiled = this.rock2.compiled;
        const Stats = this.rock2.qefStats;
        const HasSolo = Stack.layers.some(L => L.enabled && L.solo);

        const LeftTitle = document.getElementById('left-panel-title');
        if (LeftTitle) LeftTitle.textContent = 'Layer Stack';
        const StatLbl1 = document.getElementById('stat-label-1');
        const StatLbl2 = document.getElementById('stat-label-2');
        if (StatLbl1) StatLbl1.textContent = 'Layers ON';
        if (StatLbl2) StatLbl2.textContent = 'Plain Blocks';
        const SearchInp = document.getElementById('scene-search');
        if (SearchInp) SearchInp.placeholder = 'Filter stack or plain rock nodes…';
        const LeftFoot = document.getElementById('left-footer-label');
        if (LeftFoot) LeftFoot.textContent = 'Plain Rock SDF Stack · Zero Surface Noise';
        const StageTitle = document.getElementById('stage-bar-title');
        if (StageTitle) StageTitle.textContent = 'RockFormation 2.0 · Layer Stack';
        const InspFootL = document.getElementById('inspector-foot-left');
        const InspFootR = document.getElementById('inspector-foot-right');
        if (InspFootL) InspFootL.textContent = 'Plain Basic Rock SDF · Zero Noise';
        if (InspFootR) InspFootR.textContent = 'Composable Layer Stack';

        const ActiveLayerCount = Compiled ? Compiled.compiledLayers.length : 0;
        const TotalBlocks = Compiled ? Compiled.primitives.length : 0;
        document.getElementById('stat-terrains-on').textContent = String(ActiveLayerCount);
        document.getElementById('stat-layers-total').textContent = String(TotalBlocks);
        document.getElementById('outliner-subtitle').textContent = `${Stack.layers.length} Layer${Stack.layers.length === 1 ? '' : 's'} · ${TotalBlocks} Plain Primitives`;

        const Outliner = document.getElementById('OutlinerContent');
        Outliner.innerHTML = '';

        const Group = MakeEl('div', 'tree-group');
        Group.appendChild(MakeEl('div', 'tree-group-title', [
            ConstructGlyph('Layers'),
            MakeEl('span', '', HasSolo ? 'SDF Stack (SOLO ACTIVE)' : 'Ordered SDF Layer Stack (#01 → Top)'),
            MakeEl('span', 'group-count', String(Stack.layers.length))
        ]));

        const Q = (this.outlinerSearch || '').toLowerCase();
        const OpShort = {
            union: 'Union',
            smooth_union: 'Smooth',
            chamfer_union: 'Chamfer',
            subtract: 'Subtract',
            intersect: 'Intersect'
        };

        for (let I = 0; I < Stack.layers.length; I++)
        {
            const Lyr = Stack.layers[I];
            const Def = getRock2NodeSpec(Lyr.type);
            if (Q && !Lyr.name.toLowerCase().includes(Q) && !Def.name.toLowerCase().includes(Q)) continue;

            const IsSel = Lyr.id === this.rock2.selectedLayerId;
            const IsMuted = !Lyr.enabled || (HasSolo && !Lyr.solo);
            const ToneKey = Lyr.params?.stoneTone || 'clay';
            const Tone = ROCK2_STONE_TONES[ToneKey] || ROCK2_STONE_TONES.clay;

            const Card = MakeEl('div', 'rock2-layer-card' + (IsSel ? ' selected' : '') + (IsMuted ? ' muted' : ''));
            Card.addEventListener('click', () =>
            {
                this.rock2.selectedLayerId = Lyr.id;
                this.refreshChrome();
                this.rebuildInspector();
            });

            const IdxBadge = MakeEl('span', 'rock2-layer-idx', `#${String(I + 1).padStart(2, '0')}`);
            const Dot = MakeEl('span', 'rock2-tone-dot');
            Dot.style.background = Tone.hex;
            const NameEl = MakeEl('span', 'rock2-layer-name', Lyr.name);
            const TopRow = MakeEl('div', 'rock2-layer-top', [IdxBadge, Dot, NameEl]);

            const OpLabel = I === 0 ? 'Base' : (OpShort[Lyr.combineMode] || 'Union');
            const OpClass = I === 0 ? 'op-union' : `op-${Lyr.combineMode}`;
            const OpPill = MakeEl('span', `rock2-op-pill ${OpClass}`, OpLabel);
            const NodeTag = MakeEl('span', 'rock2-node-tag', Def.name);
            const MetaItems = [OpPill, NodeTag];

            const GenType = Lyr.generator?.type || 'none';
            if (GenType !== 'none')
            {
                const CompLyr = Compiled?.compiledLayers?.find(C => C.id === Lyr.id);
                const InstN = CompLyr ? CompLyr.instanceCount : evaluateRock2GeneratorInstances(Lyr.generator, Lyr.params).instances.length;
                const GenShort = GenType === 'array' && Lyr.generator?.arrayMode === 'hex'
                    ? 'HEX'
                    : GenType.toUpperCase();
                MetaItems.push(MakeEl('span', 'rock2-gen-pill', `${GenShort} ×${InstN}`));
            }
            const Meta = MakeEl('div', 'rock2-layer-meta', MetaItems);

            const Actions = MakeEl('div', 'rock2-layer-actions');

            const BtnUp = MakeEl('button', 'rock2-mini-btn', '↑');
            BtnUp.type = 'button';
            BtnUp.title = 'Move layer earlier in stack (toward #01 Base)';
            BtnUp.disabled = (I === 0);
            BtnUp.addEventListener('click', E => { E.stopPropagation(); this._moveRock2Layer(Lyr.id, -1); });

            const BtnDn = MakeEl('button', 'rock2-mini-btn', '↓');
            BtnDn.type = 'button';
            BtnDn.title = 'Move layer later in stack';
            BtnDn.disabled = (I === Stack.layers.length - 1);
            BtnDn.addEventListener('click', E => { E.stopPropagation(); this._moveRock2Layer(Lyr.id, +1); });

            const BtnSolo = MakeEl('button', 'rock2-mini-btn' + (Lyr.solo ? ' solo-on' : ''), 'S');
            BtnSolo.type = 'button';
            BtnSolo.title = 'Solo isolate this rock layer';
            BtnSolo.addEventListener('click', E => { E.stopPropagation(); this._toggleSoloRock2Layer(Lyr.id); });

            const BtnEye = MakeEl('button', 'rock2-mini-btn' + (Lyr.enabled ? ' eye-on' : ''), ConstructGlyph(Lyr.enabled ? 'Eye' : 'EyeOff'));
            BtnEye.type = 'button';
            BtnEye.title = Lyr.enabled ? 'Mute / hide layer' : 'Enable layer';
            BtnEye.addEventListener('click', E =>
            {
                E.stopPropagation();
                this._pushRock2UndoSnapshot();
                Lyr.enabled = !Lyr.enabled;
                this._rebuildRock2StackNow(false);
                this.refreshChrome();
                this.rebuildInspector();
            });

            const BtnDup = MakeEl('button', 'rock2-mini-btn', ConstructGlyph('Copy'));
            BtnDup.type = 'button';
            BtnDup.title = 'Duplicate layer';
            BtnDup.addEventListener('click', E => { E.stopPropagation(); this._duplicateRock2Layer(Lyr.id); });

            const BtnDel = MakeEl('button', 'rock2-mini-btn', ConstructGlyph('Trash'));
            BtnDel.type = 'button';
            BtnDel.title = Stack.layers.length <= 1 ? 'At least 1 base layer is kept in stack' : 'Delete layer';
            BtnDel.disabled = (Stack.layers.length <= 1);
            BtnDel.addEventListener('click', E => { E.stopPropagation(); this._deleteRock2Layer(Lyr.id); });

            Actions.append(BtnUp, BtnDn, BtnSolo, BtnEye, BtnDup, BtnDel);
            const BotRow = MakeEl('div', 'rock2-layer-bottom', [Meta, Actions]);

            Card.append(TopRow, BotRow);
            Group.appendChild(Card);
        }

        Outliner.appendChild(Group);

        // Sync bottom telemetry status bar
        const MainStat = document.getElementById('status-main');
        const TilesStat = document.getElementById('status-tiles');
        const SlabStat = document.getElementById('status-slab');
        const SelLayer = this._getSelectedRock2Layer();
        if (MainStat) MainStat.textContent = `${Stack.name} · ${SelLayer ? SelLayer.name : 'Layer Stack'}`;
        if (TilesStat)
        {
            TilesStat.textContent = Stats && !this.rock2.qefDirty
                ? `4³ Sparse Bricks: ${Stats.activeBricks} (${Stats.brickOccupancyPct.toFixed(1)}% occ)`
                : `Active Layers: ${ActiveLayerCount}/${Stack.layers.length} · Plain Blocks: ${TotalBlocks}`;
        }
        if (SlabStat) SlabStat.textContent = `SDF Planes: ${Compiled ? Compiled.totalPlanes : 0} · Zero Fractures · Zero Noise`;
    }

    _buildRock2InspectorTab(Content)
    {
        if (!this.rock2.savedOpenState) this.rock2.savedOpenState = new Map();
        for (const Det of Content.querySelectorAll('details.property-group'))
        {
            const Title = Det.querySelector('.section-title')?.textContent;
            if (Title) this.rock2.savedOpenState.set(Title, Det.open);
        }
        Content.innerHTML = '';

        const Stack = this.rock2.stack;
        const Layer = this._getSelectedRock2Layer();
        if (!Layer) return;

        const P = Layer.params;
        const LayerIdx = Stack.layers.findIndex(L => L.id === Layer.id);
        const Def = getRock2NodeSpec(Layer.type);
        const IsQef = this.rock2View?.renderMode === 'qef-mesh';

        // Sync Right Inspector Heading to the Selected Layer!
        const Crumb = document.getElementById('inspector-crumb');
        const ObjName = document.getElementById('object-name');
        const ObjType = document.getElementById('object-type');
        const ObjEn = document.getElementById('object-enabled');
        const ObjSym = document.getElementById('object-symbol');
        if (Crumb) Crumb.textContent = `Layer Stack / #${String(LayerIdx + 1).padStart(2, '0')} ${Def.name}`;
        if (ObjName) ObjName.value = Layer.name;
        if (ObjType)
        {
            const OpTag = LayerIdx === 0 ? 'BASE LAYER' : Layer.combineMode.replace('_', ' ').toUpperCase();
            ObjType.textContent = `${Def.shortBadge} · ${OpTag} · PLAIN SDF`;
        }
        if (ObjEn) ObjEn.checked = !!Layer.enabled;
        if (ObjSym) { ObjSym.innerHTML = ''; ObjSym.appendChild(ConstructGlyph(Def.glyph || 'Mountain')); }

        const SavedOpen = (Title, DefaultOpen) =>
            this.rock2.savedOpenState.has(Title) ? this.rock2.savedOpenState.get(Title) : DefaultOpen;

        const OnLive = () => this._rebuildRock2StackNow(false);
        const OnCommit = () =>
        {
            this._pushRock2UndoSnapshot();
            this.refreshChrome();
        };

        // Top KPI Trio
        const Compiled = this.rock2.compiled;
        const Trio = MakeEl('div', 'kpi-trio', [
            MakeEl('div', 'kpi-card highlight', [
                MakeEl('span', 'kpi-label', 'Selected Node'),
                MakeEl('strong', 'kpi-val', `#${String(LayerIdx + 1).padStart(2, '0')} ${Def.shortBadge}`),
                MakeEl('span', 'kpi-sub', Def.name)
            ]),
            MakeEl('div', 'kpi-card', [
                MakeEl('span', 'kpi-label', 'Stack Primitives'),
                MakeEl('strong', 'kpi-val', String(Compiled ? Compiled.primitives.length : 0)),
                MakeEl('span', 'kpi-sub', `${Compiled ? Compiled.totalPlanes : 0} half-spaces`)
            ]),
            MakeEl('div', 'kpi-card', [
                MakeEl('span', 'kpi-label', 'Surface Purity'),
                MakeEl('strong', 'kpi-val', 'Plain'),
                MakeEl('span', 'kpi-sub', '0 fractures · 0 noise')
            ])
        ]);
        Content.appendChild(Trio);

        // 1. Layer Combine & Stone Tone
        const CompTitle = 'Stack Combine & Stone Tone';
        const CompGroup = MakePropertyGroup(
            CompTitle,
            'Layers',
            LayerIdx === 0 ? 'BASE #01' : Layer.combineMode.replace('_', ' ').toUpperCase(),
            SavedOpen(CompTitle, true),
            LayerIdx === 0
                ? 'Base Layer (#01) establishes the primary SDF rock volume. Add layers above it to Smooth-Union, Chamfer, Carve, or Intersect.'
                : 'Choose how this plain rock layer combines with the SDF stack below it.'
        );

        // Quick Swap Base Rock Shape (preserves attached Spline / Array / Splatter / Scatter generator!)
        CompGroup.Body.appendChild(MakeSelectRow(
            'Base Rock Shape',
            ROCK2_NODE_CATALOG.map(N => [N.name, N.type]),
            Layer.type,
            NextType =>
            {
                this._pushRock2UndoSnapshot();
                const Fresh = createRock2Layer(NextType, {
                    id: Layer.id,
                    enabled: Layer.enabled,
                    solo: Layer.solo,
                    combineMode: Layer.combineMode,
                    blendRadius: Layer.blendRadius,
                    stoneTone: P.stoneTone,
                    posX: P.posX, posY: P.posY, posZ: P.posZ,
                    yawDeg: P.yawDeg, pitchDeg: P.pitchDeg, rollDeg: P.rollDeg,
                    seed: P.seed,
                    generator: Layer.generator
                });
                Object.assign(Layer, Fresh);
                this._rebuildRock2StackNow(false);
                this.refreshChrome();
                this.rebuildInspector();
            }
        ));

        if (LayerIdx > 0)
        {
            CompGroup.Body.appendChild(MakeSelectRow(
                'Combine Mode',
                ROCK2_COMBINE_MODES,
                Layer.combineMode,
                Val =>
                {
                    this._pushRock2UndoSnapshot();
                    Layer.combineMode = Val;
                    this._rebuildRock2StackNow(false);
                    this.refreshChrome();
                    this.rebuildInspector();
                }
            ));

            if (Layer.combineMode !== 'union')
            {
                CompGroup.Body.appendChild(MakeSliderPillRow(
                    Layer.combineMode === 'chamfer_union' ? 'Chamfer Width' : 'Blend Radius',
                    0.0, 2.20, 0.02, Layer.blendRadius ?? 0.35, 2, 'm',
                    V => { Layer.blendRadius = V; OnLive(); },
                    OnCommit
                ));
            }
        }

        // Stone Tone Swatches
        const SwatchGrid = MakeEl('div', 'rock2-swatch-grid');
        for (const [ToneId, Tone] of Object.entries(ROCK2_STONE_TONES))
        {
            const Sw = MakeEl('button', 'rock2-swatch-btn' + (P.stoneTone === ToneId ? ' active' : ''));
            Sw.type = 'button';
            const Dot = MakeEl('i');
            Dot.style.background = Tone.hex;
            Sw.append(Dot, MakeEl('span', '', Tone.label));
            Sw.addEventListener('click', () =>
            {
                this._pushRock2UndoSnapshot();
                P.stoneTone = ToneId;
                this._rebuildRock2StackNow(false);
                this.refreshChrome();
                this.rebuildInspector();
            });
            SwatchGrid.appendChild(Sw);
        }
        CompGroup.Body.appendChild(SwatchGrid);

        // Layer Quick Actions
        const ActRow = MakeEl('div', 'studio-btn-row');
        const BtnSolo = MakeEl('button', 'button' + (Layer.solo ? ' export-button' : ''), Layer.solo ? 'Unsolo Layer' : 'Solo Layer');
        BtnSolo.type = 'button';
        BtnSolo.addEventListener('click', () => this._toggleSoloRock2Layer(Layer.id));

        const BtnDup = MakeEl('button', 'button', 'Duplicate');
        BtnDup.type = 'button';
        BtnDup.addEventListener('click', () => this._duplicateRock2Layer(Layer.id));

        ActRow.append(BtnSolo, BtnDup);
        if (Stack.layers.length > 1)
        {
            const BtnDel = MakeEl('button', 'button', 'Delete');
            BtnDel.type = 'button';
            BtnDel.addEventListener('click', () => this._deleteRock2Layer(Layer.id));
            ActRow.appendChild(BtnDel);
        }
        CompGroup.Body.appendChild(ActRow);
        Content.appendChild(CompGroup.Group);

        // 2. Composable Per-Layer Placement Generator (Spline / Array / Splatter / Scatter)
        if (!Layer.generator) Layer.generator = createRock2Generator('none');
        const G = Layer.generator;
        const GenType = G.type || 'none';
        const CompLayerInfo = Compiled?.compiledLayers?.find(C => C.id === Layer.id);
        const GenCount = CompLayerInfo
            ? CompLayerInfo.instanceCount
            : evaluateRock2GeneratorInstances(G, P).instances.length;

        const GenBadge = GenType === 'none'
            ? 'SINGLE ×1'
            : `${(GenType === 'array' && G.arrayMode === 'hex' ? 'HEX' : GenType).toUpperCase()} ×${GenCount}`;
        const GenTitle = 'Placement Generator';
        const GenGroup = MakePropertyGroup(
            GenTitle,
            'Spline',
            GenBadge,
            SavedOpen(GenTitle, true),
            GenType === 'none'
                ? 'Single plain rock. Attach Spline, Array, Splatter, or Scatter to distribute this layer\'s Base Rock without hardcoding.'
                : `Distributing ${GenCount} instances of ${Def.name} on this layer.`
        );

        const GenTabs = MakeEl('div', 'rock2-gen-tabs');
        for (const [TabLabel, TabId] of [
            ['Single', 'none'],
            ['Spline', 'spline'],
            ['Array', 'array'],
            ['Splatter', 'splatter'],
            ['Scatter', 'scatter']
        ])
        {
            const TabBtn = MakeEl('button', 'rock2-gen-tab' + (GenType === TabId ? ' active' : ''), TabLabel);
            TabBtn.type = 'button';
            TabBtn.addEventListener('click', () =>
            {
                if (GenType === TabId) return;
                this._pushRock2UndoSnapshot();
                Layer.generator = createRock2Generator(TabId, { seed: G.seed ?? 701 });
                this._rebuildRock2StackNow(false);
                this.refreshChrome();
                this.rebuildInspector();
            });
            GenTabs.appendChild(TabBtn);
        }
        GenGroup.Body.appendChild(GenTabs);

        if (GenType === 'spline')
        {
            GenGroup.Body.appendChild(MakeSelectRow(
                'Spline Path Shape',
                [
                    ['Curved Cliff / Ridge Arc', 'arc'],
                    ['S-Curve Meander', 'scurve'],
                    ['Straight Line Run', 'line'],
                    ['Closed Circle Ring', 'ring'],
                    ['Custom 3-Knot Bezier (XYZ)', 'custom']
                ],
                G.splineShape || 'arc',
                Val =>
                {
                    this._pushRock2UndoSnapshot();
                    G.splineShape = Val;
                    this._rebuildRock2StackNow(false);
                    this.refreshChrome();
                    this.rebuildInspector();
                }
            ));
            GenGroup.Body.appendChild(MakeSliderPillRow('Rock Count', 2, 48, 1, G.count ?? 6, 0, '#', V => { G.count = Math.round(V); OnLive(); }, OnCommit));

            const SpShape = G.splineShape || 'arc';
            if (SpShape === 'ring')
            {
                GenGroup.Body.appendChild(MakeSliderPillRow('Ring Radius', 0.6, 12.0, 0.1, G.ringRadius ?? 3.8, 2, 'm', V => { G.ringRadius = V; OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('Ring Wave Height', -4.0, 4.0, 0.1, G.elevationRise ?? 0.0, 2, 'm', V => { G.elevationRise = V; OnLive(); }, OnCommit));
            }
            else if (SpShape === 'custom')
            {
                GenGroup.Body.appendChild(MakeSliderPillRow('Start Knot X', -10.0, 10.0, 0.1, G.startX ?? -4.5, 1, 'm', V => { G.startX = V; OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('Start Knot Y', -3.0, 6.0, 0.1, G.startY ?? 0.0, 1, 'm', V => { G.startY = V; OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('Start Knot Z', -10.0, 10.0, 0.1, G.startZ ?? 0.8, 1, 'm', V => { G.startZ = V; OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('Mid Knot X', -10.0, 10.0, 0.1, G.midX ?? 0.0, 1, 'm', V => { G.midX = V; OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('Mid Knot Y', -3.0, 6.0, 0.1, G.midY ?? 0.4, 1, 'm', V => { G.midY = V; OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('Mid Knot Z', -10.0, 10.0, 0.1, G.midZ ?? -1.8, 1, 'm', V => { G.midZ = V; OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('End Knot X', -10.0, 10.0, 0.1, G.endX ?? 4.5, 1, 'm', V => { G.endX = V; OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('End Knot Y', -3.0, 6.0, 0.1, G.endY ?? 0.0, 1, 'm', V => { G.endY = V; OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('End Knot Z', -10.0, 10.0, 0.1, G.endZ ?? 0.8, 1, 'm', V => { G.endZ = V; OnLive(); }, OnCommit));
            }
            else
            {
                GenGroup.Body.appendChild(MakeSliderPillRow('Spline Length', 1.0, 22.0, 0.2, G.pathLength ?? 9.0, 1, 'm', V => { G.pathLength = V; OnLive(); }, OnCommit));
                if (SpShape !== 'line')
                {
                    GenGroup.Body.appendChild(MakeSliderPillRow('Curve Bend Depth', -8.0, 8.0, 0.1, G.curveBend ?? 2.2, 2, 'm', V => { G.curveBend = V; OnLive(); }, OnCommit));
                }
                GenGroup.Body.appendChild(MakeSliderPillRow('Start→End Rise (Y)', -6.0, 6.0, 0.1, G.elevationRise ?? 0.0, 2, 'm', V => { G.elevationRise = V; OnLive(); }, OnCommit));
            }

            GenGroup.Body.appendChild(MakeSliderPillRow('Align to Tangent', 0, 1, 1, G.alignToTangent ?? 1, 0, '', V => { G.alignToTangent = Math.round(V); OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Yaw Offset', -180, 180, 2, G.yawOffsetDeg ?? 0, 0, '°', V => { G.yawOffsetDeg = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Lateral Jitter', 0.0, 2.5, 0.05, G.lateralJitter ?? 0.0, 2, 'm', V => { G.lateralJitter = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Start Scale', 0.25, 2.2, 0.05, G.scaleStart ?? 1.0, 2, '×', V => { G.scaleStart = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('End Scale', 0.25, 2.2, 0.05, G.scaleEnd ?? 1.0, 2, '×', V => { G.scaleEnd = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Scale Variation', 0.0, 0.75, 0.02, G.scaleVariation ?? 0.14, 2, 'w', V => { G.scaleVariation = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Height Variation', 0.0, 0.75, 0.02, G.heightVariation ?? 0.16, 2, 'w', V => { G.heightVariation = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Yaw Variation', 0, 180, 2, G.yawVariationDeg ?? 10, 0, '°', V => { G.yawVariationDeg = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Tilt Variation', 0, 45, 1, G.tiltVariationDeg ?? 4, 0, '°', V => { G.tiltVariationDeg = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Intra-Layer Fuse', 0.0, 1.8, 0.02, G.intraBlend ?? 0.20, 2, 'm', V => { G.intraBlend = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Unique Rock Seeds', 0, 1, 1, G.uniqueSeeds ?? 1, 0, '', V => { G.uniqueSeeds = Math.round(V); OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Generator Seed', 1, 9999, 1, G.seed ?? 701, 0, '#', V => { G.seed = Math.round(V); OnLive(); }, OnCommit));
        }
        else if (GenType === 'array')
        {
            const AMode = G.arrayMode || 'linear';
            GenGroup.Body.appendChild(MakeSelectRow(
                'Array Pattern',
                [
                    ['Linear / Stepped 1D Run', 'linear'],
                    ['2D Rectangular / Staggered Grid', 'grid2d'],
                    ['3D Stacked Lattice', 'grid3d'],
                    ['Hexagonal Honeycomb Pack (Basalt)', 'hex'],
                    ['Radial Circle / Arc', 'radial']
                ],
                AMode,
                Val =>
                {
                    this._pushRock2UndoSnapshot();
                    G.arrayMode = Val;
                    this._rebuildRock2StackNow(false);
                    this.refreshChrome();
                    this.rebuildInspector();
                }
            ));

            if (AMode === 'linear')
            {
                GenGroup.Body.appendChild(MakeSliderPillRow('Count', 2, 36, 1, G.countX ?? 5, 0, '#', V => { G.countX = Math.round(V); OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('Step X', -6.0, 6.0, 0.05, G.stepX ?? 2.2, 2, 'm', V => { G.stepX = V; OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('Step Y (Rise)', -3.0, 3.0, 0.05, G.stepY ?? 0.0, 2, 'm', V => { G.stepY = V; OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('Step Z', -6.0, 6.0, 0.05, G.stepZ ?? 0.0, 2, 'm', V => { G.stepZ = V; OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('Progressive Yaw', -45, 45, 1, G.progressiveYawDeg ?? 0, 0, '°', V => { G.progressiveYawDeg = V; OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('End Scale Decay', -0.45, 0.75, 0.02, G.scaleDecay ?? 0.0, 2, 'w', V => { G.scaleDecay = V; OnLive(); }, OnCommit));
            }
            else if (AMode === 'grid2d' || AMode === 'grid3d')
            {
                GenGroup.Body.appendChild(MakeSliderPillRow('Columns X', 1, 8, 1, G.countX ?? 3, 0, '#', V => { G.countX = Math.round(V); OnLive(); }, OnCommit));
                if (AMode === 'grid3d')
                {
                    GenGroup.Body.appendChild(MakeSliderPillRow('Stacks Y', 1, 4, 1, G.countY ?? 2, 0, '#', V => { G.countY = Math.round(V); OnLive(); }, OnCommit));
                }
                GenGroup.Body.appendChild(MakeSliderPillRow('Rows Z', 1, 8, 1, G.countZ ?? 3, 0, '#', V => { G.countZ = Math.round(V); OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('Spacing X', 0.4, 6.0, 0.05, G.stepX ?? 2.2, 2, 'm', V => { G.stepX = V; OnLive(); }, OnCommit));
                if (AMode === 'grid3d')
                {
                    GenGroup.Body.appendChild(MakeSliderPillRow('Spacing Y', 0.4, 5.0, 0.05, G.stepY ?? 1.8, 2, 'm', V => { G.stepY = V; OnLive(); }, OnCommit));
                }
                GenGroup.Body.appendChild(MakeSliderPillRow('Spacing Z', 0.4, 6.0, 0.05, G.stepZ ?? 2.2, 2, 'm', V => { G.stepZ = V; OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('Row Brick Stagger', 0.0, 0.5, 0.05, G.rowStagger ?? 0.0, 2, 'w', V => { G.rowStagger = V; OnLive(); }, OnCommit));
            }
            else if (AMode === 'hex')
            {
                GenGroup.Body.appendChild(MakeSliderPillRow('Hexagon Rings', 1, 4, 1, G.hexRings ?? 2, 0, 'rg', V => { G.hexRings = Math.round(V); OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('Cell Spacing', 0.35, 4.0, 0.02, G.cellSpacing ?? 1.02, 2, 'm', V => { G.cellSpacing = V; OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('Terrace Height Steps', 1, 12, 1, G.terraceSteps ?? 5, 0, 'st', V => { G.terraceSteps = Math.round(V); OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('Outer Terrace Drop', 0.0, 0.85, 0.02, G.terraceDrop ?? 0.55, 2, 'w', V => { G.terraceDrop = V; OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('Radial Fan Lean', -18, 18, 0.5, G.radialLeanDeg ?? 2.5, 1, '°', V => { G.radialLeanDeg = V; OnLive(); }, OnCommit));
            }
            else if (AMode === 'radial')
            {
                GenGroup.Body.appendChild(MakeSliderPillRow('Ring Count', 3, 36, 1, G.radialCount ?? 8, 0, '#', V => { G.radialCount = Math.round(V); OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('Ring Radius', 0.5, 10.0, 0.1, G.ringRadius ?? 3.6, 2, 'm', V => { G.ringRadius = V; OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('Arc Span', 45, 360, 5, G.arcAngleDeg ?? 360, 0, '°', V => { G.arcAngleDeg = V; OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('Face Ring Center', 0, 1, 1, G.faceCenter ?? 1, 0, '', V => { G.faceCenter = Math.round(V); OnLive(); }, OnCommit));
            }

            GenGroup.Body.appendChild(MakeSliderPillRow('Position Jitter', 0.0, 1.5, 0.02, G.posJitter ?? 0.0, 2, 'm', V => { G.posJitter = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Scale Variation', 0.0, 0.65, 0.02, G.scaleVariation ?? 0.10, 2, 'w', V => { G.scaleVariation = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Height Variation', 0.0, 0.65, 0.02, G.heightVariation ?? 0.12, 2, 'w', V => { G.heightVariation = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Yaw Jitter', 0, 180, 2, G.yawJitterDeg ?? 8, 0, '°', V => { G.yawJitterDeg = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Tilt Jitter', 0, 35, 1, G.tiltJitterDeg ?? 3, 0, '°', V => { G.tiltJitterDeg = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Intra-Layer Fuse', 0.0, 1.8, 0.02, G.intraBlend ?? 0.0, 2, 'm', V => { G.intraBlend = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Unique Rock Seeds', 0, 1, 1, G.uniqueSeeds ?? 1, 0, '', V => { G.uniqueSeeds = Math.round(V); OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Generator Seed', 1, 9999, 1, G.seed ?? 809, 0, '#', V => { G.seed = Math.round(V); OnLive(); }, OnCommit));
        }
        else if (GenType === 'splatter')
        {
            const SMode = G.splatterMode || 'burst';
            GenGroup.Body.appendChild(MakeSelectRow(
                'Splatter Pattern',
                [
                    ['Radial Burst Clump', 'burst'],
                    ['Directional Talus Fan', 'talus_fan'],
                    ['Multi-Pocket Rock Clumps', 'clusters'],
                    ['Concentric Debris Rings', 'rings']
                ],
                SMode,
                Val =>
                {
                    this._pushRock2UndoSnapshot();
                    G.splatterMode = Val;
                    this._rebuildRock2StackNow(false);
                    this.refreshChrome();
                    this.rebuildInspector();
                }
            ));
            GenGroup.Body.appendChild(MakeSliderPillRow('Rock Count', 2, 56, 1, G.count ?? 14, 0, '#', V => { G.count = Math.round(V); OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Outer Radius', 0.6, 12.0, 0.1, G.outerRadius ?? 3.8, 2, 'm', V => { G.outerRadius = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Inner Clearing Ratio', 0.0, 0.85, 0.02, G.innerRadiusRatio ?? 0.0, 2, 'w', V => { G.innerRadiusRatio = V; OnLive(); }, OnCommit));
            if (SMode === 'talus_fan')
            {
                GenGroup.Body.appendChild(MakeSliderPillRow('Fan Spread Angle', 20, 320, 5, G.fanSpreadDeg ?? 110, 0, '°', V => { G.fanSpreadDeg = V; OnLive(); }, OnCommit));
                GenGroup.Body.appendChild(MakeSliderPillRow('Fan Direction', -180, 180, 2, G.fanAzimuthDeg ?? 0, 0, '°', V => { G.fanAzimuthDeg = V; OnLive(); }, OnCommit));
            }
            GenGroup.Body.appendChild(MakeSliderPillRow('Center Pile Mound', 0.0, 3.0, 0.05, G.pileHeight ?? 0.35, 2, 'm', V => { G.pileHeight = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Center→Rim Shrink', -0.75, 0.85, 0.02, G.radialScaleFalloff ?? 0.45, 2, 'w', V => { G.radialScaleFalloff = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Min Rock Scale', 0.20, 2.0, 0.05, G.scaleMin ?? 0.45, 2, '×', V => { G.scaleMin = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Max Rock Scale', 0.25, 2.6, 0.05, G.scaleMax ?? 1.25, 2, '×', V => { G.scaleMax = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Yaw Randomness', 0.0, 1.0, 0.05, G.yawRandom ?? 1.0, 2, 'w', V => { G.yawRandom = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Tilt Randomness', 0, 45, 1, G.tiltRandomDeg ?? 16, 0, '°', V => { G.tiltRandomDeg = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Ground Embed', 0.0, 0.65, 0.02, G.embedDepth ?? 0.12, 2, 'w', V => { G.embedDepth = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Intra-Layer Fuse', 0.0, 1.5, 0.02, G.intraBlend ?? 0.0, 2, 'm', V => { G.intraBlend = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Unique Rock Seeds', 0, 1, 1, G.uniqueSeeds ?? 1, 0, '', V => { G.uniqueSeeds = Math.round(V); OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Generator Seed', 1, 9999, 1, G.seed ?? 919, 0, '#', V => { G.seed = Math.round(V); OnLive(); }, OnCommit));
        }
        else if (GenType === 'scatter')
        {
            GenGroup.Body.appendChild(MakeSliderPillRow('Rock Count', 2, 56, 1, G.count ?? 16, 0, '#', V => { G.count = Math.round(V); OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Scatter Width (X)', 1.0, 18.0, 0.2, G.scatterWidth ?? 6.8, 1, 'm', V => { G.scatterWidth = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Scatter Length (Z)', 1.0, 18.0, 0.2, G.scatterLength ?? 6.2, 1, 'm', V => { G.scatterLength = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Anti-Overlap Spacing', 0.0, 1.0, 0.05, G.minSpacing ?? 0.72, 2, 'w', V => { G.minSpacing = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Center Mound Lift', 0.0, 2.5, 0.05, G.moundHeight ?? 0.0, 2, 'm', V => { G.moundHeight = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Min Rock Scale', 0.20, 2.0, 0.05, G.scaleMin ?? 0.45, 2, '×', V => { G.scaleMin = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Max Rock Scale', 0.25, 2.6, 0.05, G.scaleMax ?? 1.30, 2, '×', V => { G.scaleMax = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Small↔Large Bias', 0.35, 3.0, 0.05, G.sizeBias ?? 1.55, 2, 'e', V => { G.sizeBias = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Yaw Randomness', 0.0, 1.0, 0.05, G.yawRandom ?? 1.0, 2, 'w', V => { G.yawRandom = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Tilt Randomness', 0, 45, 1, G.tiltRandomDeg ?? 18, 0, '°', V => { G.tiltRandomDeg = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Ground Embed', 0.0, 0.65, 0.02, G.embedDepth ?? 0.14, 2, 'w', V => { G.embedDepth = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Intra-Layer Fuse', 0.0, 1.5, 0.02, G.intraBlend ?? 0.0, 2, 'm', V => { G.intraBlend = V; OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Unique Rock Seeds', 0, 1, 1, G.uniqueSeeds ?? 1, 0, '', V => { G.uniqueSeeds = Math.round(V); OnLive(); }, OnCommit));
            GenGroup.Body.appendChild(MakeSliderPillRow('Generator Seed', 1, 9999, 1, G.seed ?? 1031, 0, '#', V => { G.seed = Math.round(V); OnLive(); }, OnCommit));
        }

        Content.appendChild(GenGroup.Group);

        // 3. Base Rock Dimensions & Layer 3D Transform
        const XfTitle = 'Base Rock Size & Layer Transform';
        const XfGroup = MakePropertyGroup(
            XfTitle,
            'Move',
            `${(P.width ?? 4).toFixed(1)}×${(P.height ?? 3).toFixed(1)}×${(P.length ?? 4).toFixed(1)}m`,
            SavedOpen(XfTitle, true),
            'Base rock metric dimensions (instanced by any attached generator) and whole-layer 3D pose.'
        );
        XfGroup.Body.appendChild(MakeSliderPillRow('Width (X)', 0.15, 14.0, 0.05, P.width ?? 4.0, 2, 'm', V => { P.width = V; OnLive(); }, OnCommit));
        XfGroup.Body.appendChild(MakeSliderPillRow('Height (Y)', 0.15, 14.0, 0.05, P.height ?? 3.5, 2, 'm', V => { P.height = V; OnLive(); }, OnCommit));
        XfGroup.Body.appendChild(MakeSliderPillRow('Length (Z)', 0.15, 14.0, 0.05, P.length ?? 4.0, 2, 'm', V => { P.length = V; OnLive(); }, OnCommit));

        XfGroup.Body.appendChild(MakeSliderPillRow('Position X', -8.0, 8.0, 0.05, P.posX ?? 0, 2, 'm', V => { P.posX = V; OnLive(); }, OnCommit));
        XfGroup.Body.appendChild(MakeSliderPillRow('Position Y', -3.0, 8.0, 0.05, P.posY ?? 0, 2, 'm', V => { P.posY = V; OnLive(); }, OnCommit));
        XfGroup.Body.appendChild(MakeSliderPillRow('Position Z', -8.0, 8.0, 0.05, P.posZ ?? 0, 2, 'm', V => { P.posZ = V; OnLive(); }, OnCommit));

        XfGroup.Body.appendChild(MakeSliderPillRow('Yaw', -180, 180, 1, P.yawDeg ?? 0, 0, '°', V => { P.yawDeg = V; OnLive(); }, OnCommit));
        XfGroup.Body.appendChild(MakeSliderPillRow('Pitch', -90, 90, 1, P.pitchDeg ?? 0, 0, '°', V => { P.pitchDeg = V; OnLive(); }, OnCommit));
        XfGroup.Body.appendChild(MakeSliderPillRow('Roll', -90, 90, 1, P.rollDeg ?? 0, 0, '°', V => { P.rollDeg = V; OnLive(); }, OnCommit));
        XfGroup.Body.appendChild(MakeSliderPillRow('Shape Seed', 1, 9999, 1, P.seed ?? 101, 0, '#', V => { P.seed = Math.round(V); OnLive(); }, OnCommit));

        const XfBtns = MakeEl('div', 'studio-btn-row');
        const MutBtn = MakeEl('button', 'button', '🎲 Mutate Seed');
        MutBtn.type = 'button';
        MutBtn.addEventListener('click', () =>
        {
            this._pushRock2UndoSnapshot();
            P.seed = (((P.seed || 101) * 1664525 + 1013904223) >>> 0) % 9999 + 1;
            this._rebuildRock2StackNow(false);
            this.rebuildInspector();
        });
        const ZeroBtn = MakeEl('button', 'button', 'Reset Pose');
        ZeroBtn.type = 'button';
        ZeroBtn.addEventListener('click', () =>
        {
            this._pushRock2UndoSnapshot();
            P.posX = 0; P.posY = 0; P.posZ = 0;
            P.yawDeg = 0; P.pitchDeg = 0; P.rollDeg = 0;
            this._rebuildRock2StackNow(false);
            this.rebuildInspector();
        });
        XfBtns.append(MutBtn, ZeroBtn);
        XfGroup.Body.appendChild(XfBtns);
        Content.appendChild(XfGroup.Group);

        // 3. Node-Specific Plain Shape Groups
        for (const GrpSpec of Def.controlGroups)
        {
            const Grp = MakePropertyGroup(
                GrpSpec.title,
                GrpSpec.glyph || 'Sliders',
                GrpSpec.badge || 'PLAIN SHAPE',
                SavedOpen(GrpSpec.title, true),
                GrpSpec.hint || ''
            );

            for (const Ctrl of GrpSpec.controls)
            {
                if (Ctrl.type === 'select')
                {
                    Grp.Body.appendChild(MakeSelectRow(
                        Ctrl.label,
                        Ctrl.options,
                        String(P[Ctrl.key] ?? Ctrl.options?.[0]?.[1] ?? ''),
                        Val =>
                        {
                            this._pushRock2UndoSnapshot();
                            const AsNum = Number(Val);
                            P[Ctrl.key] = (Val !== '' && !Number.isNaN(AsNum)) ? AsNum : Val;
                            this._rebuildRock2StackNow(false);
                            this.rebuildInspector();
                        }
                    ));
                }
                else
                {
                    Grp.Body.appendChild(MakeSliderPillRow(
                        Ctrl.label,
                        Ctrl.min,
                        Ctrl.max,
                        Ctrl.step,
                        P[Ctrl.key],
                        Ctrl.decimals,
                        Ctrl.unit,
                        V => { P[Ctrl.key] = V; OnLive(); },
                        OnCommit
                    ));
                }
            }
            Content.appendChild(Grp.Group);
        }

        // 4. Studio Lighting, QEF Dual-Contour Mesh & Export
        const StageTitle = 'Lighting, QEF Mesh & Export';
        const StageGrp = MakePropertyGroup(
            StageTitle,
            'Boxes',
            IsQef ? `QEF ${this.rock2.meshResolution}` : '60 FPS SDF',
            SavedOpen(StageTitle, true),
            'Adjust studio sun angle or extract a manifold 4³ Sparse-Brick QEF Dual-Contour mesh for .GLB / .OBJ / .STL export.'
        );
        StageGrp.Body.appendChild(MakeSliderPillRow('Sun Azimuth', 0, 360, 2, Stack.sunAzimuthDeg ?? 46, 0, '°', V => { Stack.sunAzimuthDeg = V; OnLive(); }, OnCommit));
        StageGrp.Body.appendChild(MakeSliderPillRow('Sun Elevation', 12, 82, 1, Stack.sunElevationDeg ?? 42, 0, '°', V => { Stack.sunElevationDeg = V; OnLive(); }, OnCommit));
        StageGrp.Body.appendChild(MakeSliderPillRow('Viewport Exposure', 0.45, 1.65, 0.02, this.rock2View?.exposure ?? 1.12, 2, 'ev', V =>
        {
            if (this.rock2View) this.rock2View.exposure = V;
        }));

        if (IsQef)
        {
            StageGrp.Body.appendChild(MakeSliderPillRow('QEF Voxel Grid Res', 36, 112, 4, this.rock2.meshResolution ?? 72, 0, 'vx', V =>
            {
                this.rock2.meshResolution = Math.round(V);
                Stack.meshResolution = this.rock2.meshResolution;
                this.rock2.qefDirty = true;
                this._scheduleRock2QefBake(160);
            }));
        }

        const ModeRow = MakeEl('div', 'studio-btn-row');
        const BakeBtn = MakeEl('button', 'button export-button', IsQef ? 'Rebake QEF Mesh' : 'Switch to QEF Mesh');
        BakeBtn.type = 'button';
        BakeBtn.addEventListener('click', () =>
        {
            this._bakeRock2QefMeshNow();
            if (this.rock2View) this.rock2View.renderMode = 'qef-mesh';
            this._syncRock2ToolbarButtons();
            this.rebuildInspector();
            this.refreshChrome();
        });
        ModeRow.appendChild(BakeBtn);
        if (IsQef)
        {
            const BackBtn = MakeEl('button', 'button', 'Back to 60 FPS SDF');
            BackBtn.type = 'button';
            BackBtn.addEventListener('click', () => document.getElementById('rock2-mode-ray-btn')?.click());
            ModeRow.appendChild(BackBtn);
        }
        StageGrp.Body.appendChild(ModeRow);

        const ExpRow = MakeEl('div', 'studio-btn-row');
        for (const [Label, Fmt] of [['Export .GLB', 'glb'], ['Export .OBJ', 'obj'], ['Export .STL', 'stl']])
        {
            const B = MakeEl('button', 'button', Label);
            B.type = 'button';
            B.addEventListener('click', () => this._exportRock2Asset(Fmt));
            ExpRow.appendChild(B);
        }
        StageGrp.Body.appendChild(ExpRow);

        const JsonRow = MakeEl('div', 'studio-btn-row');
        const SaveJsonBtn = MakeEl('button', 'button', 'Save Layer Stack (.JSON)');
        SaveJsonBtn.type = 'button';
        SaveJsonBtn.addEventListener('click', () => this._exportRock2Asset('json'));
        JsonRow.appendChild(SaveJsonBtn);
        StageGrp.Body.appendChild(JsonRow);

        Content.appendChild(StageGrp.Group);
    }

    /*======================================================================================================================================
                                                      MAIN RENDER LOOP & TELEMETRY
    ======================================================================================================================================*/
    _startMainLoop()
    {
        let Frames = 0;
        let LastFpsT = performance.now();
        let LastSimT = performance.now();
        const EnginePill = document.getElementById('engine-pill-text');
        const HudTris = document.getElementById('hud-tris-count');
        const StatMesh = document.getElementById('status-mesh');

        const Tick = Now =>
        {
            // ---- Live erosion simulation stepping (play mode) ----
            if (this.sim.playing && this.sim.group)
            {
                this.sim.accumulator += (Now - LastSimT) / 1000 * this.sim.stepRate;
                LastSimT = Now;
                const Days = Math.floor(this.sim.accumulator);
                if (Days >= 1)
                {
                    this.sim.accumulator -= Days;
                    this._simStepDays(Math.min(Days, 10));
                    if (this.inspectorTab === 'sim') this._simRefreshHud();
                }
            }
            else LastSimT = Now;

            if (this.workspaceMode === 'rock2')
            {
                this.rock2View?.render();
            }
            else if (this.workspaceMode === 'fracture')
            {
                this.fractureView?.render();
            }
            else
            {
                if (this.workspaceMode !== '3d') this.map2D.render();
                if (this.workspaceMode !== '2d') this.view3D.render(this.activeLayerId);
            }

            Frames++;
            if (Now - LastFpsT >= 500)
            {
                const Fps = Math.round((Frames * 1000) / (Now - LastFpsT));
                Frames = 0;
                LastFpsT = Now;

                if (this.workspaceMode === 'rock2')
                {
                    const IsQef = this.rock2View?.renderMode === 'qef-mesh';
                    const S = this.rock2?.qefStats;
                    const C = this.rock2?.compiled;
                    if (EnginePill) EnginePill.textContent = IsQef ? `ROCK 2.0 QEF · ${Fps} FPS` : `ROCK 2.0 SDF · ${Fps} FPS`;
                    if (StatMesh)
                    {
                        StatMesh.textContent = (IsQef && S)
                            ? `QEF Triangles: ${S.triangles.toLocaleString()} · Vertices: ${S.vertices.toLocaleString()} · Plain SDF Stack`
                            : `Plain SDF Layer Stack · ${C?.compiledLayers?.length || 0} Layers · ${C?.primitives?.length || 0} Primitives · 0 Noise`;
                    }
                }
                else if (this.workspaceMode === 'fracture')
                {
                    const IsQef = this.fractureView?.renderMode === 'qef-mesh';
                    const S = this.fracture?.qefStats;
                    if (EnginePill) EnginePill.textContent = IsQef ? `QEF DUAL-CONTOUR · ${Fps} FPS` : `PURE-SDF RAYMARCH · ${Fps} FPS`;
                    if (StatMesh)
                    {
                        StatMesh.textContent = S
                            ? `QEF Triangles: ${S.triangles.toLocaleString()} · Vertices: ${S.vertices.toLocaleString()} · 0 Mesh→SDF`
                            : `GPU Pure-SDF Raymarching · ${this.fracture?.formation?.blocks?.length || 0} Blocks · 0 Mesh→SDF`;
                    }
                }
                else
                {
                    const Tris = this.view3D.totalVisibleTriangles;
                    const Verts = this.view3D.totalVisibleVertices;
                    if (EnginePill) EnginePill.textContent = `THIN-SLAB · ${Fps} FPS`;
                    if (HudTris) HudTris.textContent = `${(Tris / 1000).toFixed(1)}k`;
                    if (StatMesh) StatMesh.textContent = `Triangles: ${Tris.toLocaleString()} · Vertices: ${Verts.toLocaleString()}`;
                }
            }
            requestAnimationFrame(Tick);
        };
        requestAnimationFrame(Tick);
    }
}

window.TerrainStudio = new TerrainWorldStudioApp();
