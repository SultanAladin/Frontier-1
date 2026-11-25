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

        this.workspaceMode = 'split'; // '2d' | '3d' | 'split'
        this.inspectorTab = 'layers'; // 'layers' | 'spline' | 'slab' | 'stage' | 'sim' | 'texturing'
        this.outlinerFilter = 'all';  // 'all' | 'terrains' | 'splines' | 'masks'
        this.outlinerSearch = '';

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

        this._mountStaticGlyphs();
        this._buildViewportToolRails();
        this._buildQuickAddLibrary();
        this._buildPresetStrip();
        this._bindHeaderAndTransport();
        this._populateMapLensOptions();
        this.refreshChrome();
        this.rebuildInspector();

        requestAnimationFrame(() =>
        {
            this.map2D.frameAllRegions();
            this.view3D.frameVisibleWorld();
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
        List.innerHTML = '';

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
        if (!this.undoStack.length) return;
        this.redoStack.push(JSON.stringify(this.world.toJSON()));
        this._restoreWorldData(JSON.parse(this.undoStack.pop()));
    }

    redo()
    {
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
        this.workspaceMode = Mode;
        const Grid = document.getElementById('StageGrid');
        Grid.classList.remove('mode-2d', 'mode-3d', 'mode-split');
        Grid.classList.add(`mode-${Mode}`);

        for (const Btn of document.querySelectorAll('.top-nav .nav-button[data-ws]'))
        {
            Btn.classList.toggle('active', Btn.dataset.ws === Mode);
        }
        requestAnimationFrame(() => this.map2D.requestRedraw());
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

        // One-Click Add 3D Spline buttons (Outliner '+', Stage Bar '+ Add Spline', Transport '+ New 3D Spline')
        document.getElementById('add-button')?.addEventListener('click', () => this.addNew3DSplineLayer('grade', '3D Graded Spline Ramp'));
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
            const Reg = this.getActiveRegion();
            if (Reg)
            {
                Reg.enabled = E.target.checked;
                this.map2D.requestRedraw();
                this.refreshChrome();
            }
        });

        // Reset 3 Demo Terrains
        document.getElementById('reset-demo-btn')?.addEventListener('click', () =>
        {
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
                this.pushUndoSnapshot();
                this._restoreWorldData(Parsed);
                this.map2D.frameAllRegions();
                this.view3D.frameVisibleWorld();
            }
            catch (Err) { console.error(Err); }
            FileInput.value = '';
        });

        document.getElementById('export-button')?.addEventListener('click', () => this._exportObj());

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
            else if (E.key === 'Tab')
            {
                E.preventDefault();
                document.getElementById('maximize-btn')?.click();
            }
            else if (K === 'g') this.select3DTool('spline');
            else if (K === 'k') this.select3DTool('spline-add');
            else if (K === 'n') this.select3DTool('navigate');
            else if (K === 'b') this.select2DTool('brush');
            else if (K === 'e') this.select2DTool('erase');
            else if (K === 'p') this.select2DTool('polygon');
            else if (K === 'r') this.select2DTool('bounds');
            else if (K === 'v') this.select2DTool('select');
            else if (K === 's' && !E.ctrlKey) this.select2DTool('spline');
            else if (K === 'w') document.getElementById('toggle-ocean-btn')?.click();
            else if (K === 'f') document.getElementById('focus-btn')?.click();
            else if (E.key === 'Delete' || E.key === 'Backspace')
            {
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
        const Targets = this.view3D.getVisibleRegions();
        const { text } = ExportWorldTerrainsObj(this.world, Targets.length ? Targets : this.world.regions);
        this._triggerDownload(new Blob([text], { type: 'text/plain' }), 'frontier-sdf-terrains.obj');
    }

    _exportStl()
    {
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
                    this._setRegionSelection(R.id, Array.from(Next));
                });
                TabsEl.appendChild(Tab);
            }
        }

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
            if (NameInp && document.activeElement !== NameInp) NameInp.value = Reg.name;
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

            if (this.workspaceMode !== '3d') this.map2D.render();
            if (this.workspaceMode !== '2d') this.view3D.render(this.activeLayerId);

            Frames++;
            if (Now - LastFpsT >= 500)
            {
                const Fps = Math.round((Frames * 1000) / (Now - LastFpsT));
                Frames = 0;
                LastFpsT = Now;
                const Tris = this.view3D.totalVisibleTriangles;
                const Verts = this.view3D.totalVisibleVertices;
                if (EnginePill) EnginePill.textContent = `THIN-SLAB · ${Fps} FPS`;
                if (HudTris) HudTris.textContent = `${(Tris / 1000).toFixed(1)}k`;
                if (StatMesh) StatMesh.textContent = `Triangles: ${Tris.toLocaleString()} · Vertices: ${Verts.toLocaleString()}`;
            }
            requestAnimationFrame(Tick);
        };
        requestAnimationFrame(Tick);
    }
}

window.TerrainStudio = new TerrainWorldStudioApp();
