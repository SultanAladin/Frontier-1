/*==========================================================================================================================================
                                                             UVEDITOR.JS
==========================================================================================================================================*/
// Main Studio Application Controller for Frontier. UV Studio (Experimental Remastered):
//   - Multi-document workspace with sample assets & Wavefront .OBJ import/export
//   - Full Blender UV Toolset & Hotkeys (1-4 modes, G/R/S + axis lock + numeric + proportional O, B/C select, U/P/V/M/Y/Ctrl+V/Ctrl+A/Ctrl+E)
//   - Importance-Based Space Allocation Packer (select groups of islands -> target %, auto-fill remaining islands)
//   - Complete UDIM Tiles & Multi-Resolution Atlas Workflow (1001+)

import { EdgeKey, UdimOrigin, UdimFromUV, TopologySurface } from './Engine/TopologySurface.js';
import {
    UnwrapLscm,
    SmartUvProject,
    AnalyticProject,
    FollowActiveQuads,
    MinimizeStretch,
    AverageIslandScale,
    ApplyAlignOrRectify
} from './Engine/UnwrapSolver.js';
import { SolveImportancePack } from './Engine/ImportancePackingSolver.js';
import { SampleAssetCatalog } from './Engine/SampleAssets.js';
import { UVCanvas2D } from './Engine/UVCanvas2D.js';
import { UVViewport3D } from './Engine/UVViewport3D.js';
import { SerializeTopologySurfaceForBridge, SaveUvBridgeTransfer } from '../Shared/UvTextureBridge.js';
import {
    Construct,
    ConstructGlyph,
    OpenScope,
    CloseScope,
    ReleaseScope,
    Register,
    Broadcast,
    SectionCard,
    SliderRow,
    SwitchRow,
    SelectRow,
    SegmentedRow,
    ActionRow,
    MetricTrio,
    SmoothScroll,
    Toast
} from './StudioControls.js';

const GroupColorPalette = ['#eff4cf', '#9ecfff', '#e8c27a', '#9bdac5', '#f4979b', '#c4a8ff'];

const StudioState = {
    documents: [],
    activeDocId: null,
    stageMode: 'mesh', // 'split' | 'uv' | 'mesh'
    inspectorTab: 'packing', // 'packing' | 'unwrap' | 'udim' | 'check'
    outlinerFilter: 'all',
    searchQuery: '',
    compactOutliner: false,
    maximizedStage: false,
    logOpen: false,
    logs: [],
    selectionMode: 'island', // 'vertex' | 'edge' | 'face' | 'island'
    activeTool: 'select', // 'select' | 'move' | 'rotate' | 'scale' | 'box' | 'circle' | 'seam' | 'cursor'
    syncSelection: true,
    pivotMode: 'median', // 'median' | 'bounds' | 'cursor' | 'individual'
    snapMode: 'off', // 'off' | 'grid' | 'pixel' | 'vertex'
    overlayLens: 'importance',
    showChecker: true,
    showSeams: true,
    showBoundaries: true,
    showPins: true,
    showIslandBadges: true,
    proportional: { enabled: false, radius: 0.22 },
    cursorUV: [0.5, 0.5],
    liveUnwrap: false,
    smartAngleDeg: 55,
    sharpAngleDeg: 35,
    smartMargin: 0.015,
    relaxIterations: 18,
    targetTexelDensity: 8.5,
    selectSimilarTol: 0.14,
    selectSimilarAngle: 15,
    selectRandomRatio: 0.5,
    selectRandomSeed: 1337,
    packOptions: {
        budgetMode: 'strict-ratio', // 'strict-ratio' | 'fill-remainder'
        scope: 'active-udim', // 'active-udim' | 'per-udim' | 'auto-udim-spill' | 'selected-only'
        marginPx: 12,
        rotations: '90',
        equalizeWithinGroup: true
    },
    pendingGroupPct: 30,
    inspectorScope: null
};

let Canvas2D = null;
let Viewport3D = null;
let InspectorScroll = null;
let OutlinerScroll = null;
let HoverUvPos = [0.5, 0.5];
let HoverHit2D = null;
let LastPointerClient = [420, 320];
let ModalTransform = null;
let MarqueeBox = null;
let CircleSelect = { active: false, x: 0, y: 0, radiusPx: 42 };
let LassoPoints = null;
let PendingMirrorAxis = false;

function ActiveDoc()
{
    return StudioState.documents.find(D => D.id === StudioState.activeDocId) || StudioState.documents[0];
}

function LogEvent(Channel, Text, Tone = '')
{
    const Now = new Date();
    const Stamp = Now.toTimeString().slice(0, 8);
    StudioState.logs.unshift({ time: Stamp, channel: Channel, text: Text, tone: Tone });
    if (StudioState.logs.length > 80) StudioState.logs.length = 80;
    const StatusEl = document.getElementById('status-op');
    if (StatusEl) StatusEl.textContent = Text;
    RenderLogs();
}

function RenderLogs()
{
    const Box = document.getElementById('log-scroll');
    if (!Box) return;
    Box.textContent = '';
    for (const Entry of StudioState.logs)
    {
        Box.append(Construct('div', 'log-row', [
            Construct('span', 'log-time', Entry.time),
            Construct('span', `log-chan ${Entry.tone}`, Entry.channel),
            Construct('span', null, Entry.text)
        ]));
    }
}

function CreateDocumentFromFactory(FactorySpec, IdOverride = null)
{
    const Created = FactorySpec.create();
    const DocId = IdOverride || `doc-${Date.now()}-${Math.random().toString(16).slice(2, 6)}`;
    const Doc = {
        id: DocId,
        factoryId: FactorySpec.id,
        name: Created.surface.name,
        surface: Created.surface,
        importanceGroups: Created.importanceGroups,
        udimTiles: Created.udimTiles,
        activeUdim: Created.udimTiles[0]?.id || 1001,
        islands: [],
        diagnostics: null,
        lastPackReport: null
    };
    RefreshDocAnalysis(Doc);
    return Doc;
}

let Fast2DRafId = 0;
function ScheduleFast2DRender(Doc)
{
    if (!Canvas2D || !Doc || Fast2DRafId) return;
    Fast2DRafId = requestAnimationFrame(() =>
    {
        Fast2DRafId = 0;
        const CurDoc = ActiveDoc();
        if (!CurDoc) return;
        Canvas2D.Render({
            surface: CurDoc.surface,
            islands: CurDoc.islands,
            udimTiles: CurDoc.udimTiles,
            activeUdim: CurDoc.activeUdim,
            importanceGroups: CurDoc.importanceGroups,
            selectionMode: StudioState.selectionMode,
            activeTool: StudioState.activeTool,
            overlayLens: StudioState.overlayLens,
            showChecker: StudioState.showChecker,
            showSubGrid: true,
            showSeams: StudioState.showSeams,
            showBoundaries: StudioState.showBoundaries,
            showPins: StudioState.showPins,
            showIslandBadges: StudioState.showIslandBadges,
            targetTexelDensity: StudioState.targetTexelDensity,
            diagnostics: CurDoc.diagnostics,
            hoverHit: HoverHit2D,
            cursorUV: StudioState.cursorUV,
            pivotPoint: null,
            proportional: StudioState.proportional,
            modalTransform: ModalTransform,
            marqueeBox: MarqueeBox,
            circleSelect: CircleSelect,
            lassoPoints: LassoPoints
        });
    });
}

function FastPaintVisualSync(Doc)
{
    if (!Doc) return;
    if (Viewport3D)
    {
        Viewport3D.selectionMode = StudioState.selectionMode;
        Viewport3D.FastUpdateGpuFaceMask(Doc.surface, Doc.islands);
    }
    ScheduleFast2DRender(Doc);
}

function RefreshDocAnalysis(Doc = ActiveDoc())
{
    if (!Doc) return;
    const TileResMap = new Map(Doc.udimTiles.map(T => [T.id, T.resolution || 2048]));
    const TileSig = Doc.udimTiles.map(T => `${T.id}:${T.resolution || 2048}`).join(',');
    if (typeof Doc.surface.GetCachedAnalysis === 'function')
    {
        const Cached = Doc.surface.GetCachedAnalysis(TileId => TileResMap.get(TileId) || 2048, 100, TileSig);
        Doc.islands = Cached.islands;
        Doc.diagnostics = Cached.diagnostics;
    }
    else
    {
        Doc.islands = Doc.surface.ExtractIslands(TileId => TileResMap.get(TileId) || 2048, 100);
        Doc.diagnostics = Doc.surface.AnalyzeDiagnostics(Doc.islands);
    }
}

function SyncAllViews(UpdateInspector = false)
{
    const Doc = ActiveDoc();
    if (!Doc) return;
    RefreshDocAnalysis(Doc);

    if (Canvas2D)
    {
        Canvas2D.Render({
            surface: Doc.surface,
            islands: Doc.islands,
            udimTiles: Doc.udimTiles,
            activeUdim: Doc.activeUdim,
            importanceGroups: Doc.importanceGroups,
            selectionMode: StudioState.selectionMode,
            activeTool: StudioState.activeTool,
            overlayLens: StudioState.overlayLens,
            showChecker: StudioState.showChecker,
            showSubGrid: true,
            showSeams: StudioState.showSeams,
            showBoundaries: StudioState.showBoundaries,
            showPins: StudioState.showPins,
            showIslandBadges: StudioState.showIslandBadges,
            targetTexelDensity: StudioState.targetTexelDensity,
            diagnostics: Doc.diagnostics,
            hoverHit: HoverHit2D,
            cursorUV: StudioState.cursorUV,
            pivotPoint: ComputeSelectionPivot(Doc),
            proportional: StudioState.proportional,
            modalTransform: ModalTransform,
            marqueeBox: MarqueeBox,
            circleSelect: CircleSelect,
            lassoPoints: LassoPoints
        });
    }

    if (Viewport3D)
    {
        Viewport3D.selectionMode = StudioState.selectionMode;
        Viewport3D.paintRadiusPx = CircleSelect.radiusPx;
        Viewport3D.SetActiveTool(StudioState.activeTool);
        Viewport3D.showBoundaries = StudioState.showBoundaries;
        Viewport3D.SyncFromSurface(Doc.surface, Doc.islands, Doc.importanceGroups, StudioState.targetTexelDensity);
    }

    UpdateHeaderAndHud(Doc);
    RenderDocumentTabs();
    RenderUdimChipStrip(Doc);
    RenderOutliner(Doc);
    RenderLegendStrip(Doc);

    if (UpdateInspector) RebuildInspector();
    else Broadcast();
}

function ComputeSelectionPivot(Doc)
{
    if (StudioState.pivotMode === 'cursor') return [...StudioState.cursorUV];
    const Sel = Doc.surface.selectedUVs;
    if (!Sel.size) return null;
    if (StudioState.pivotMode === 'bounds')
    {
        let MinU = Infinity, MinV = Infinity, MaxU = -Infinity, MaxV = -Infinity;
        for (const UvIdx of Sel)
        {
            const P = Doc.surface.uvCoords[UvIdx];
            if (P[0] < MinU) MinU = P[0]; if (P[0] > MaxU) MaxU = P[0];
            if (P[1] < MinV) MinV = P[1]; if (P[1] > MaxV) MaxV = P[1];
        }
        return [0.5 * (MinU + MaxU), 0.5 * (MinV + MaxV)];
    }
    let SumU = 0, SumV = 0;
    for (const UvIdx of Sel)
    {
        const P = Doc.surface.uvCoords[UvIdx];
        SumU += P[0]; SumV += P[1];
    }
    return [SumU / Sel.size, SumV / Sel.size];
}

function UpdateHeaderAndHud(Doc)
{
    document.getElementById('stat-islands-count').textContent = String(Doc.islands.length);
    document.getElementById('stat-udim-count').textContent = String(Doc.udimTiles.length);

    const ActiveTile = Doc.udimTiles.find(T => T.id === Doc.activeUdim) || Doc.udimTiles[0];
    const ResK = ActiveTile ? (ActiveTile.resolution >= 1024 ? `${ActiveTile.resolution / 1024}K` : `${ActiveTile.resolution}px`) : '2K';
    document.getElementById('viewport-udim-pill').textContent = `UDIM ${Doc.activeUdim} · ${ResK}`;
    document.getElementById('mesh-object-pill').textContent = Doc.name;

    const Overlaps = Doc.diagnostics.overlapCount;
    const HealthTag = document.getElementById('atlas-health-tag');
    HealthTag.classList.toggle('warn', Overlaps > 0);
    HealthTag.querySelector('span').textContent = Overlaps > 0 ? `${Overlaps} OVERLAPS` : '0 OVERLAPS';

    document.getElementById('hud-cursor').innerHTML = `Cursor <b>${StudioState.cursorUV[0].toFixed(2)}, ${StudioState.cursorUV[1].toFixed(2)}</b>`;
    document.getElementById('hud-selection').innerHTML = `Sel <b>${Doc.surface.selectedEdges3D.size} ed · ${Doc.surface.selectedIslands.size} isl · ${Doc.surface.selectedUVs.size} uv</b>`;
    document.getElementById('hud-stretch').innerHTML = `Seams <b>${Doc.surface.seams.size}</b> · Stretch <b>${(Doc.diagnostics.meanAngleStretch * 100).toFixed(1)}%</b>`;

    const MeshHint = document.getElementById('mesh-hint-pill');
    if (MeshHint)
    {
        if (StudioState.activeTool === 'seam')
            MeshHint.textContent = 'Seam Brush (K): Click edge to toggle seam · Alt+Click seam loop';
        else if (StudioState.selectionMode === 'edge')
            MeshHint.textContent = 'Edge: Alt Loop · ^Alt Ring · ^Path · ⇧G Similar · RMB Menu';
        else if (StudioState.selectionMode === 'vertex')
            MeshHint.textContent = 'Vertex: Click · Alt Loop · ^Path · ⇧G Similar';
        else if (StudioState.selectionMode === 'face')
            MeshHint.textContent = 'Face: Alt Loop · ^Path · Dbl Island · ⇧G Similar · K Seam';
        else
            MeshHint.textContent = 'Island: Click / DblClick · ⇧G Select Similar · L Linked';
    }

    document.getElementById('undo-btn').disabled = !Doc.surface.CanUndo();
    document.getElementById('redo-btn').disabled = !Doc.surface.CanRedo();

    const NameInput = document.getElementById('inspector-object-name');
    if (document.activeElement !== NameInput) NameInput.value = Doc.name;

    const ExplicitPct = Doc.importanceGroups.reduce((S, G) => S + G.targetPercent, 0);
    const RemPct = Math.max(0, 100 - ExplicitPct);
    document.getElementById('inspector-object-sub').textContent =
        `${Doc.islands.length} ISLANDS · ${Doc.udimTiles.length} UDIM · ${Doc.importanceGroups.length} PRIORITY GROUPS · REST ${RemPct.toFixed(0)}%`;

    document.getElementById('inspector-footer-right').textContent = `UDIM ${Doc.activeUdim} · ${ActiveTile?.resolution || 2048} px`;
    document.getElementById('status-counts').textContent = `${Doc.surface.positions.length} verts · ${Doc.surface.faces.length} faces · ${Doc.islands.length} islands`;

    const AvgDen = Doc.islands.reduce((S, I) => S + I.texelDensity, 0) / Math.max(1, Doc.islands.length);
    document.getElementById('status-density').textContent = `Avg Texel: ${AvgDen.toFixed(2)} px/cm`;
    document.getElementById('status-pack-time').textContent = Doc.lastPackReport ? `Pack: ${Doc.lastPackReport.elapsedMs.toFixed(1)} ms` : 'Pack: Ready';
}

function RenderDocumentTabs()
{
    const Box = document.getElementById('document-tabs');
    Box.textContent = '';
    for (const Doc of StudioState.documents)
    {
        const Btn = Construct('button', 'document-tab' + (Doc.id === StudioState.activeDocId ? ' active' : ''), [
            Construct('span', 'document-label', Doc.name)
        ]);
        Btn.type = 'button';
        Btn.addEventListener('click', () =>
        {
            StudioState.activeDocId = Doc.id;
            SyncAllViews(true);
            Canvas2D.FrameUdimTiles(Doc.udimTiles);
            Viewport3D.CameraPreset('Persp');
            LogEvent('DOC', `Switched to "${Doc.name}"`);
        });
        Box.append(Btn);
    }
}

function RenderUdimChipStrip(Doc)
{
    const Strip = document.getElementById('udim-chip-strip');
    Strip.textContent = '';
    const OccMap = new Map();
    const CntMap = new Map();
    for (const Isl of Doc.islands)
    {
        if (Isl.hidden) continue;
        OccMap.set(Isl.udimTile, (OccMap.get(Isl.udimTile) || 0) + Isl.areaUV);
        CntMap.set(Isl.udimTile, (CntMap.get(Isl.udimTile) || 0) + 1);
    }
    for (const Tile of Doc.udimTiles)
    {
        const ResLabel = Tile.resolution >= 1024 ? `${Tile.resolution / 1024}K` : `${Tile.resolution}`;
        const Occ = Math.min(100, (OccMap.get(Tile.id) || 0) * 100);
        const Chip = Construct('button', 'udim-chip' + (Tile.id === Doc.activeUdim ? ' active' : ''), [
            Construct('strong', null, `${Tile.id} · ${ResLabel}`),
            Construct('small', null, `${CntMap.get(Tile.id) || 0} isl · ${Occ.toFixed(0)}%`)
        ]);
        Chip.type = 'button';
        Chip.addEventListener('click', () =>
        {
            Doc.activeUdim = Tile.id;
            const O = UdimOrigin(Tile.id);
            Canvas2D.FrameBounds(O.u, O.v, O.u + 1, O.v + 1, 0.12);
            SyncAllViews(true);
        });
        Strip.append(Chip);
    }
}

let _lastOutlinerSig = null;
function RenderOutliner(Doc)
{
    const SelIslList = [...Doc.surface.selectedIslands].sort((a, b) => a - b).join(',');
    const GrpSig = Doc.importanceGroups.map(G => `${G.id}:${G.name}:${G.targetPercent}:${G.color}`).join(';');
    const SurfSig = typeof Doc.surface.ComputeDataSignature === 'function' ? Doc.surface.ComputeDataSignature() : `${Doc.islands.length}`;
    const OutlinerSig = `${Doc.id}|${StudioState.outlinerFilter}|${StudioState.searchQuery.trim()}|${SurfSig}|${GrpSig}|${SelIslList}|${Doc.diagnostics?.overlapCount || 0}|${Doc.diagnostics?.flippedCount || 0}`;
    if (_lastOutlinerSig === OutlinerSig) return;
    _lastOutlinerSig = OutlinerSig;

    const Container = document.getElementById('outliner-list');
    Container.textContent = '';
    const Query = StudioState.searchQuery.trim().toLowerCase();
    const GroupMap = new Map(Doc.importanceGroups.map(G => [G.id, G]));

    const Matches = Isl =>
    {
        if (StudioState.outlinerFilter === 'selected' && !Doc.surface.selectedIslands.has(Isl.id)) return false;
        if (StudioState.outlinerFilter === 'grouped' && !Isl.importanceGroupId) return false;
        if (StudioState.outlinerFilter === 'issues')
        {
            const HasIssue = Isl.faceIndices.some(Fi => Doc.diagnostics.OverlappingFaces.has(Fi) || Doc.diagnostics.FlippedFaces.has(Fi));
            if (!HasIssue) return false;
        }
        if (Query)
        {
            const GrpName = GroupMap.get(Isl.importanceGroupId)?.name || '';
            const Hay = `${Isl.displayName} ${Isl.materialName} ${Isl.udimTile} ${GrpName}`.toLowerCase();
            if (!Hay.includes(Query)) return false;
        }
        return true;
    };

    // Group islands by Importance Group (plus Remaining Unassigned)
    const Buckets = [];
    for (const Grp of Doc.importanceGroups)
    {
        const List = Doc.islands.filter(I => I.importanceGroupId === Grp.id && Matches(I));
        if (List.length) Buckets.push({ title: `${Grp.name} (${Grp.targetPercent}%)`, color: Grp.color, islands: List });
    }
    const Rem = Doc.islands.filter(I => (!I.importanceGroupId || !GroupMap.has(I.importanceGroupId)) && Matches(I));
    if (Rem.length) Buckets.push({ title: 'Remaining Islands (Auto-Fill)', color: '#787f88', islands: Rem });

    for (const Bucket of Buckets)
    {
        const Section = Construct('div', 'tree-group');
        const Dot = Construct('span', 'tree-group-dot');
        Dot.style.background = Bucket.color;
        Section.append(Construct('div', 'tree-group-title', [
            Dot,
            Construct('span', null, Bucket.title),
            Construct('span', 'group-count', `${Bucket.islands.length}`)
        ]));

        for (const Isl of Bucket.islands)
        {
            const IsSel = Doc.surface.selectedIslands.has(Isl.id);
            const Row = Construct('div', 'tree-row' + (IsSel ? ' selected' : '') + (Isl.hidden ? ' is-off' : ''));
            Row.style.setProperty('--row-accent', Bucket.color);

            const Swatch = Construct('span', 'row-swatch');
            Swatch.style.background = Bucket.color;

            const MainBtn = Construct('button', 'row-select', [
                Swatch,
                Construct('div', 'row-copy', [
                    Construct('div', 'row-name', Isl.displayName),
                    Construct('div', 'row-meta', `${Isl.faceIndices.length}f · ${Isl.texelDensity.toFixed(1)} px/cm · ${(Isl.areaUV * 100).toFixed(1)}%`)
                ]),
                Construct('span', 'row-pill', String(Isl.udimTile))
            ]);
            MainBtn.type = 'button';
            MainBtn.addEventListener('click', E =>
            {
                Doc.surface.SelectIsland(Isl.id, E.shiftKey || E.ctrlKey);
                SyncAllViews(true);
            });
            MainBtn.addEventListener('dblclick', () =>
            {
                Doc.surface.SelectIsland(Isl.id, false);
                Canvas2D.FrameBounds(Isl.bounds.minU, Isl.bounds.minV, Isl.bounds.maxU, Isl.bounds.maxV, 0.25);
                SyncAllViews(true);
            });

            const LockBtn = Construct('button', 'row-action-btn' + (Isl.locked ? ' locked' : ''), ConstructGlyph('Lock'));
            LockBtn.type = 'button';
            LockBtn.title = Isl.locked ? 'Unlock Island for Packing' : 'Lock Island Position';
            LockBtn.addEventListener('click', E =>
            {
                E.stopPropagation();
                Doc.surface.SetIslandLocked(Isl.id, !Isl.locked);
                SyncAllViews();
            });

            const EyeBtn = Construct('button', 'row-action-btn', ConstructGlyph(Isl.hidden ? 'EyeOff' : 'Eye'));
            EyeBtn.type = 'button';
            EyeBtn.title = Isl.hidden ? 'Show Island' : 'Hide Island';
            EyeBtn.addEventListener('click', E =>
            {
                E.stopPropagation();
                Doc.surface.SetIslandHidden(Isl.id, !Isl.hidden);
                SyncAllViews();
            });

            Row.append(MainBtn, LockBtn, EyeBtn);
            Section.append(Row);
        }
        Container.append(Section);
    }

    if (!Buckets.length)
    {
        Container.append(Construct('p', 'property-hint', 'No UV islands match the active filter.'));
    }
}

function RenderLegendStrip(Doc)
{
    const Strip = document.getElementById('uv-legend-strip');
    Strip.textContent = '';
    const ExplicitPct = Doc.importanceGroups.reduce((S, G) => S + G.targetPercent, 0);
    for (const Grp of Doc.importanceGroups)
    {
        const Dot = Construct('span', 'legend-dot');
        Dot.style.background = Grp.color;
        const Count = Doc.islands.filter(I => I.importanceGroupId === Grp.id).length;
        Strip.append(Construct('span', 'legend-pill', [Dot, `${Grp.name} · ${Grp.targetPercent}% (${Count})`]));
    }
    const RemCount = Doc.islands.filter(I => !I.importanceGroupId).length;
    if (RemCount > 0)
    {
        const Dot = Construct('span', 'legend-dot');
        Dot.style.background = '#828992';
        Strip.append(Construct('span', 'legend-pill', [Dot, `Remaining · ${Math.max(0, 100 - ExplicitPct).toFixed(0)}% (${RemCount})`]));
    }
}

/*==========================================================================================================================================
                                                   OPERATIONS & IMPORTANCE PACKING
==========================================================================================================================================*/
function RunWithUndo(Label, Fn)
{
    const Doc = ActiveDoc();
    if (!Doc) return;
    Doc.surface.PushUndo(Label);
    const Res = Fn(Doc);
    SyncAllViews(true);
    return Res;
}

function ExecuteImportancePack()
{
    const Doc = ActiveDoc();
    if (!Doc) return;
    Doc.surface.PushUndo('Importance Pack');
    const Report = SolveImportancePack(Doc.surface, {
        importanceGroups: Doc.importanceGroups,
        udimTiles: Doc.udimTiles,
        activeUdim: Doc.activeUdim,
        options: {
            ...StudioState.packOptions,
            pixelsPerUnitMultiplier: 100
        }
    });
    Doc.lastPackReport = Report;

    // Auto-create UDIM tiles if spillover created new UDIM IDs
    for (const NewId of Report.createdUdims)
    {
        if (!Doc.udimTiles.some(T => T.id === NewId))
        {
            Doc.udimTiles.push({ id: NewId, label: `Spill Tile ${NewId}`, resolution: 2048, checkerMode: 'color' });
        }
    }
    Doc.udimTiles.sort((A, B) => A.id - B.id);

    const MainRep = Report.tileReports.find(R => R.tileId === Doc.activeUdim) || Report.tileReports[0];
    const Summary = MainRep
        ? `Packed ${MainRep.packedCount} islands into UDIM ${MainRep.tileId} (${MainRep.tileUtilizationPct.toFixed(1)}% density) in ${Report.elapsedMs.toFixed(1)} ms`
        : 'Importance Pack completed';
    LogEvent('PACK', Summary, 'live');
    Toast(Summary);
    if (Report.warnings.length) Toast(Report.warnings[0]);
    SyncAllViews(true);
}

function AssignSelectedIslandsToNewGroup(Pct = StudioState.pendingGroupPct)
{
    const Doc = ActiveDoc();
    if (!Doc) return;
    let SelectedIds = [...Doc.surface.selectedIslands];
    if (!SelectedIds.length)
    {
        Toast('Select 1 or more islands first (click in 2D, 3D, or Outliner)');
        return;
    }
    Doc.surface.PushUndo('Create Importance Group');
    const NextIdx = Doc.importanceGroups.length;
    const Color = GroupColorPalette[NextIdx % GroupColorPalette.length];
    const NewGroup = {
        id: `grp-${Date.now().toString(36)}`,
        name: `Priority Group ${NextIdx + 1}`,
        targetPercent: Math.max(1, Math.min(98, Number(Pct) || 25)),
        color: Color,
        udimTarget: 'auto'
    };
    Doc.importanceGroups.push(NewGroup);
    Doc.surface.AssignImportanceGroup(SelectedIds, NewGroup.id);
    LogEvent('BUDGET', `Assigned ${SelectedIds.length} islands to "${NewGroup.name}" (${NewGroup.targetPercent}%)`, 'warm');
    Toast(`${SelectedIds.length} islands -> ${NewGroup.targetPercent}% budget`);
    SyncAllViews(true);
}

function AssignSelectedToGroup(GroupId)
{
    const Doc = ActiveDoc();
    if (!Doc) return;
    const SelectedIds = [...Doc.surface.selectedIslands];
    if (!SelectedIds.length) { Toast('Select islands first'); return; }
    Doc.surface.PushUndo('Assign Importance Group');
    Doc.surface.AssignImportanceGroup(SelectedIds, GroupId);
    const Grp = Doc.importanceGroups.find(G => G.id === GroupId);
    LogEvent('BUDGET', `Assigned ${SelectedIds.length} islands to ${Grp ? Grp.name : 'Remaining Auto-Fill'}`, 'warm');
    SyncAllViews(true);
}

function RunEdgeOrSeamOp(OpName)
{
    const Doc = ActiveDoc();
    if (!Doc) return 0;

    if (OpName === 'select-loop')
    {
        if (Doc.surface.activeEdge3D)
        {
            const [Va, Vb] = Doc.surface.activeEdge3D.split(':').map(Number);
            const Cnt = Doc.surface.SelectEdgeLoop3D(Va, Vb, true, Doc.islands);
            SetSelectionMode('edge');
            LogEvent('EDGE', `Selected 3D edge loop (${Cnt} edges)`);
            Toast(`Edge Loop: ${Cnt} edges`);
            return Cnt;
        }
        const UvKey = Doc.surface.activeEdgeUV || [...Doc.surface.selectedUVEdges][0];
        if (!UvKey) { Toast('Click or select an edge first (or Alt+Click any edge in 2D/3D)'); return 0; }
        const [Ua, Ub] = UvKey.split(':').map(Number);
        const Cnt = Doc.surface.SelectUvEdgeLoop(Ua, Ub, Doc.islands, true);
        SetSelectionMode('edge');
        LogEvent('EDGE', `Selected UV edge loop (${Cnt} edges)`);
        Toast(`UV Edge Loop: ${Cnt} edges`);
        return Cnt;
    }
    if (OpName === 'select-ring')
    {
        if (Doc.surface.activeEdge3D)
        {
            const [Va, Vb] = Doc.surface.activeEdge3D.split(':').map(Number);
            const Cnt = Doc.surface.SelectEdgeRing3D(Va, Vb, false, Doc.islands);
            SetSelectionMode('edge');
            LogEvent('EDGE', `Selected 3D edge ring (${Cnt} edges)`);
            Toast(`Edge Ring: ${Cnt} edges`);
            return Cnt;
        }
        const UvKey = Doc.surface.activeEdgeUV || [...Doc.surface.selectedUVEdges][0];
        if (!UvKey) { Toast('Click or select an edge first (or Ctrl+Alt+Click any edge)'); return 0; }
        const [Ua, Ub] = UvKey.split(':').map(Number);
        const Cnt = Doc.surface.SelectUvEdgeRing(Ua, Ub, Doc.islands, false);
        SetSelectionMode('edge');
        LogEvent('EDGE', `Selected UV edge ring (${Cnt} edges)`);
        Toast(`Edge Ring: ${Cnt} edges`);
        return Cnt;
    }
    if (OpName === 'select-boundary-auto' || OpName === 'select-boundary-mesh' || OpName === 'select-boundary-islands')
    {
        const Scope = OpName === 'select-boundary-mesh' ? 'mesh' : (OpName === 'select-boundary-islands' ? 'islands' : 'auto');
        const Res = Doc.surface.SelectBoundaryEdges(Scope, false, Doc.islands);
        SetSelectionMode('edge');
        const Label = Res.scope === 'selection' ? 'Selection Boundary Loop' : (Res.scope === 'mesh' ? 'Open Mesh Boundaries' : 'Boundary Edges');
        LogEvent('EDGE', `Selected ${Res.count3D} ${Label.toLowerCase()}`, 'live');
        Toast(`${Label}: ${Res.count3D} edges`);
        return Res.count3D;
    }
    if (OpName === 'select-sharp')
    {
        const Cnt = Doc.surface.SelectSharpEdges(StudioState.sharpAngleDeg, false, Doc.islands);
        SetSelectionMode('edge');
        LogEvent('EDGE', `Selected ${Cnt} sharp edges (>= ${StudioState.sharpAngleDeg}°)`, 'live');
        Toast(`Sharp Edges (>= ${StudioState.sharpAngleDeg}°): ${Cnt}`);
        return Cnt;
    }
    if (OpName === 'select-seams')
    {
        const Cnt = Doc.surface.SelectAllSeamEdges(false, Doc.islands);
        SetSelectionMode('edge');
        LogEvent('SEAM', `Selected all ${Cnt} seam edges`);
        Toast(`Selected ${Cnt} seam edges`);
        return Cnt;
    }
    if (OpName === 'grow-sel')
    {
        const Cnt = Doc.surface.GrowSelection(StudioState.selectionMode, Doc.islands);
        SyncAllViews(true);
        Toast(`Grew selection (${Cnt})`);
        return Cnt;
    }
    if (OpName === 'shrink-sel')
    {
        const Cnt = Doc.surface.ShrinkSelection(StudioState.selectionMode, Doc.islands);
        SyncAllViews(true);
        Toast(`Shrank selection (${Cnt})`);
        return Cnt;
    }
    if (OpName === 'mark-seam')
    {
        return RunWithUndo('Mark Seam', () =>
        {
            const Changed = Doc.surface.MarkSeamFromSelection(true, StudioState.selectionMode, Doc.islands);
            if (!Changed) { Toast('Select edges or face boundaries first (or use Seam Brush [K])'); return 0; }
            LogEvent('SEAM', `Marked ${Changed} UV seam edge(s) · Total: ${Doc.surface.seams.size}`, 'warm');
            Toast(`Marked ${Changed} seam edge(s)`);
            return Changed;
        });
    }
    if (OpName === 'clear-seam')
    {
        return RunWithUndo('Clear Seam', () =>
        {
            const Changed = Doc.surface.ClearSeamFromSelection(false, StudioState.selectionMode, Doc.islands);
            if (!Changed) { Toast('Select seam edges or boundary faces to clear'); return 0; }
            Doc.surface._hasClearedSeams = true;
            LogEvent('SEAM', `Cleared ${Changed} seam edge(s) (press U to re-unwrap healed islands)`, 'warm');
            Toast(`Cleared ${Changed} seam(s) · Press U to re-unwrap`);
            return Changed;
        });
    }
    if (OpName === 'clear-seam-weld')
    {
        return RunWithUndo('Clear Seam & Weld', () =>
        {
            const Changed = Doc.surface.ClearSeamFromSelection(true, StudioState.selectionMode, Doc.islands);
            if (!Changed) { Toast('Select seam edges to clear & weld'); return 0; }
            LogEvent('SEAM', `Cleared ${Changed} seam(s) and welded shared UV corners`, 'live');
            Toast(`Cleared ${Changed} seam(s) & welded UVs`);
            return Changed;
        });
    }
    if (OpName === 'toggle-seam')
    {
        return RunWithUndo('Toggle Seam', () =>
        {
            const Changed = Doc.surface.MarkSeamFromSelection(null, StudioState.selectionMode, Doc.islands);
            if (!Changed) { Toast('Select edges or faces first'); return 0; }
            Doc.surface._hasClearedSeams = true;
            LogEvent('SEAM', `Toggled ${Changed} seam edge(s)`, 'warm');
            Toast(`Toggled ${Changed} seam edge(s)`);
            return Changed;
        });
    }
    if (OpName === 'mark-boundary-seam')
    {
        return RunWithUndo('Mark Boundaries as Seams', () =>
        {
            const Res = Doc.surface.MarkBoundaryAsSeam('auto', Doc.islands);
            SetSelectionMode('edge');
            LogEvent('SEAM', `Marked ${Res.added} new boundary seams (${Res.total} total boundary edges)`, 'warm');
            Toast(`Boundary Seams: +${Res.added} (${Res.total} edges)`);
            return Res.added;
        });
    }
    if (OpName === 'mark-sharp-seam')
    {
        return RunWithUndo('Mark Sharp Edges as Seams', () =>
        {
            const Res = Doc.surface.MarkSharpAsSeam(StudioState.sharpAngleDeg, Doc.islands);
            SetSelectionMode('edge');
            LogEvent('SEAM', `Marked ${Res.added} sharp edges >= ${StudioState.sharpAngleDeg}° as seams (${Res.matched} sharp)`, 'warm');
            Toast(`Marked ${Res.matched} sharp edges (${StudioState.sharpAngleDeg}°) as seams`);
            return Res.matched;
        });
    }
    if (OpName === 'seams-from-islands')
    {
        return RunWithUndo('Seams from Islands', () =>
        {
            const Added = Doc.surface.SeamsFromIslands(Doc.islands);
            LogEvent('SEAM', `Created ${Added} new seams from island borders (Total: ${Doc.surface.seams.size})`, 'live');
            Toast(`Seams from Islands: +${Added} (${Doc.surface.seams.size} total)`);
            return Added;
        });
    }
    if (OpName === 'clear-all-seams')
    {
        return RunWithUndo('Clear All Seams', () =>
        {
            const Removed = Doc.surface.ClearAllSeams();
            Doc.surface._hasClearedSeams = true;
            LogEvent('SEAM', `Cleared all ${Removed} seam edges`, 'warm');
            Toast(`Cleared all ${Removed} seams`);
            return Removed;
        });
    }
    return 0;
}

function OpenEdgeSeamMenu(ClientX, ClientY)
{
    const Menu = document.getElementById('edge-seam-menu');
    if (!Menu) return;
    Menu.hidden = false;
    const W = 294;
    const H = Math.min(540, window.innerHeight - 24);
    const X = Math.max(8, Math.min(window.innerWidth - W - 8, ClientX));
    const Y = Math.max(8, Math.min(window.innerHeight - H - 8, ClientY));
    Menu.style.left = `${X}px`;
    Menu.style.top = `${Y}px`;
}

function CloseAllSelectMenus()
{
    const M1 = document.getElementById('select-menu');
    const M2 = document.getElementById('select-similar-menu');
    const M3 = document.getElementById('uv-context-menu');
    if (M1) M1.hidden = true;
    if (M2) M2.hidden = true;
    if (M3) M3.hidden = true;
}

function CloseUvContextMenu()
{
    const Menu = document.getElementById('uv-context-menu');
    if (Menu) Menu.hidden = true;
}

function OpenUvContextMenu(ClientX, ClientY, { source = '3d', hitEdge = null } = {})
{
    const Menu = document.getElementById('uv-context-menu');
    if (!Menu)
    {
        if (source === '3d' || StudioState.selectionMode === 'edge' || StudioState.activeTool === 'seam') OpenEdgeSeamMenu(ClientX, ClientY);
        else OpenSelectMenu(ClientX, ClientY);
        return;
    }
    document.getElementById('edge-seam-menu')?.setAttribute('hidden', '');
    document.getElementById('select-menu')?.setAttribute('hidden', '');
    document.getElementById('select-similar-menu')?.setAttribute('hidden', '');

    const Doc = ActiveDoc();
    if (hitEdge && Doc && StudioState.selectionMode === 'edge' && !Doc.surface.selectedEdges3D.has(hitEdge.key))
    {
        Doc.surface.SelectEdge3D(hitEdge.va, hitEdge.vb, false, Doc.islands);
        SyncAllViews(true);
    }

    const Badge = document.getElementById('ctx-sel-badge');
    if (Badge && Doc)
    {
        const Mode = StudioState.selectionMode;
        if (Mode === 'face') Badge.textContent = `${Doc.surface.selectedFaces.size} faces`;
        else if (Mode === 'edge') Badge.textContent = `${Doc.surface.selectedEdges3D.size} edges`;
        else if (Mode === 'vertex') Badge.textContent = `${Doc.surface.selectedUVs.size} verts`;
        else Badge.textContent = `${Doc.surface.selectedIslands.size} islands`;
    }

    Menu.querySelectorAll('[data-ctx-tool]').forEach(B => B.classList.toggle('active', B.dataset.ctxTool === StudioState.activeTool));
    Menu.querySelectorAll('[data-ctx-face-select]').forEach(B => B.classList.toggle('active', StudioState.selectionMode === 'face' && StudioState.activeTool === 'select'));
    Menu.querySelectorAll('[data-ctx-mode]').forEach(B => B.classList.toggle('active', B.dataset.ctxMode === StudioState.selectionMode));

    const RadSlider = document.getElementById('ctx-paint-radius-slider');
    const RadVal = document.getElementById('ctx-paint-radius-val');
    if (RadSlider) RadSlider.value = String(Math.round(CircleSelect.radiusPx));
    if (RadVal) RadVal.textContent = `${Math.round(CircleSelect.radiusPx)}px`;

    Menu.hidden = false;
    const W = 332;
    const H = Math.min(620, window.innerHeight - 20);
    const X = Math.max(8, Math.min(window.innerWidth - W - 8, ClientX));
    const Y = Math.max(8, Math.min(window.innerHeight - H - 8, ClientY));
    Menu.style.left = `${X}px`;
    Menu.style.top = `${Y}px`;
}

function RunUvContextOperation(OpId)
{
    const Doc = ActiveDoc();
    if (!Doc) return;
    if (OpId === 'unwrap') RunUnwrap('lscm');
    else if (OpId === 'smart') RunUnwrap('smart');
    else if (OpId === 'pack') ExecuteImportancePack();
    else if (OpId === 'relax') RunWithUndo('Minimize Stretch', () => { MinimizeStretch(Doc.surface, { iterations: StudioState.relaxIterations, onlySelected: Doc.surface.selectedUVs.size > 0, islands: Doc.islands }); });
    else if (OpId === 'avg-scale') RunWithUndo('Average Scale', () => { AverageIslandScale(Doc.surface, Doc.islands, Doc.surface.selectedIslands.size > 0); });
    else if (OpId === 'split') RunWithUndo('Split Island', () => { Doc.surface.SplitSelection(); });
    else if (OpId === 'stitch') RunWithUndo('Stitch', () => { const N = Doc.surface.StitchSelected(Doc.islands); Toast(`Stitched ${N} UV pairs`); });
    else if (OpId === 'weld') RunWithUndo('Weld', () => { const N = Doc.surface.WeldSelected(); Toast(`Welded ${N} UVs`); });
    else if (OpId === 'pin') RunWithUndo('Pin', () => { Doc.surface.PinSelected(true); });
    else if (OpId === 'unpin') RunWithUndo('Unpin', () => { Doc.surface.PinSelected(false); });
    else if (OpId === 'mirror-u') RunWithUndo('Mirror U', () => { const P = ComputeSelectionPivot(Doc) || StudioState.cursorUV; Doc.surface.TransformSelectedUVs({ pivot: P, scaleU: -1, scaleV: 1 }); });
    else if (OpId === 'rotate-90') RunWithUndo('Rotate 90°', () => { const P = ComputeSelectionPivot(Doc) || StudioState.cursorUV; Doc.surface.TransformSelectedUVs({ pivot: P, rotateRad: Math.PI / 2 }); });
    else if (OpId === 'open-edge-menu') OpenEdgeSeamMenu(LastPointerClient[0], LastPointerClient[1]);
}

function OpenSelectMenu(ClientX, ClientY)
{
    const Menu = document.getElementById('select-menu');
    if (!Menu) return;
    document.getElementById('select-similar-menu')?.setAttribute('hidden', '');
    Menu.hidden = false;
    const RatioIn = document.getElementById('sel-random-ratio-input');
    if (RatioIn) RatioIn.value = String(Math.round(StudioState.selectRandomRatio * 100));
    const W = 294;
    const H = Math.min(560, window.innerHeight - 24);
    const X = Math.max(8, Math.min(window.innerWidth - W - 8, ClientX));
    const Y = Math.max(8, Math.min(window.innerHeight - H - 8, ClientY));
    Menu.style.left = `${X}px`;
    Menu.style.top = `${Y}px`;
}

const SELECT_SIMILAR_DEFINITIONS = {
    vertex: [
        ['same_island', 'Same UV Island', 'Island'],
        ['vert_valence', 'Amount of Connected Edges', 'Valence'],
        ['vert_pinned', 'Pinned UV State', 'Pins'],
        ['vert_boundary', 'UV Island Boundary', 'Rim'],
        ['same_udim', 'Same UDIM Tile', 'UDIM'],
        ['same_part', 'Same Surface Part', 'Material']
    ],
    edge: [
        ['same_island', 'Same UV Island', 'Island'],
        ['edge_length_3d', 'Similar 3D Length', 'Length'],
        ['edge_length_uv', 'Similar UV Length', 'UV Len'],
        ['edge_direction', 'Similar 3D Direction', 'Parallel'],
        ['edge_face_angle', 'Similar Face Angle', 'Dihedral'],
        ['edge_seam', 'Seam State', 'Seam'],
        ['edge_boundary', 'Boundary / Rim State', 'Bound'],
        ['same_part', 'Same Surface Part', 'Material']
    ],
    face: [
        ['same_island', 'Same UV Island', 'Island'],
        ['perimeter_uv', 'Similar UV Perimeter', 'UV Perim'],
        ['perimeter_3d', 'Similar 3D Perimeter', '3D Perim'],
        ['area_3d', 'Similar 3D Area', '3D Area'],
        ['area_uv', 'Similar UV Area', 'UV Area'],
        ['normal', 'Similar Normal', 'Angle'],
        ['coplanar', 'Coplanar Faces', 'Plane'],
        ['sides', 'Polygon Sides (Tri/Quad)', 'Sides'],
        ['texel_density', 'Similar Texel Density', 'px/cm'],
        ['island_faces', 'Island Face Count', 'Isl Poly'],
        ['same_part', 'Same Surface Part', 'Material'],
        ['same_udim', 'Same UDIM Tile', 'UDIM']
    ],
    island: [
        ['same_island', 'Same UV Island', 'Island'],
        ['island_faces', 'Similar Island Poly Count', 'Polys'],
        ['perimeter_uv', 'Similar UV Perimeter', 'UV Perim'],
        ['perimeter_3d', 'Similar 3D Perimeter', '3D Perim'],
        ['area_3d', 'Similar 3D Area', '3D Area'],
        ['area_uv', 'Similar UV Area', 'UV Area'],
        ['texel_density', 'Similar Texel Density', 'px/cm'],
        ['same_part', 'Same Surface Part', 'Material'],
        ['same_udim', 'Same UDIM Tile', 'UDIM']
    ]
};

function OpenSelectSimilarMenu(ClientX, ClientY)
{
    const Menu = document.getElementById('select-similar-menu');
    const Box = document.getElementById('select-similar-items');
    const Heading = document.getElementById('select-similar-heading');
    if (!Menu || !Box) return;

    document.getElementById('select-menu')?.setAttribute('hidden', '');
    const Mode = StudioState.selectionMode || 'face';
    const ModeLabels = { vertex: 'VERTEX', edge: 'EDGE', face: 'FACE', island: 'ISLAND' };
    if (Heading) Heading.textContent = `SELECT SIMILAR · ${ModeLabels[Mode] || 'FACE'} (SHIFT+G)`;

    const TolIn = document.getElementById('sim-tol-input');
    const AngIn = document.getElementById('sim-angle-input');
    if (TolIn) TolIn.value = String(Math.round(StudioState.selectSimilarTol * 100));
    if (AngIn) AngIn.value = String(Math.round(StudioState.selectSimilarAngle));

    Box.textContent = '';
    const List = SELECT_SIMILAR_DEFINITIONS[Mode] || SELECT_SIMILAR_DEFINITIONS.face;
    for (const [Key, Label, Tag] of List)
    {
        const Btn = document.createElement('button');
        Btn.type = 'button';
        Btn.innerHTML = `<span>${Label}</span><kbd>${Tag}</kbd>`;
        Btn.addEventListener('click', () =>
        {
            Menu.hidden = true;
            RunSelectOp(`sim:${Key}`);
        });
        Box.append(Btn);
    }

    Menu.hidden = false;
    const W = 294;
    const H = Math.min(540, window.innerHeight - 24);
    const X = Math.max(8, Math.min(window.innerWidth - W - 8, ClientX));
    const Y = Math.max(8, Math.min(window.innerHeight - H - 8, ClientY));
    Menu.style.left = `${X}px`;
    Menu.style.top = `${Y}px`;
}

function RunSelectOp(OpId, Extra = {})
{
    const Doc = ActiveDoc();
    if (!Doc) return 0;

    if (OpId === 'all')
    {
        Doc.surface.SelectAll(Doc.islands);
        SyncAllViews(true);
        Toast(`Selected All (${Doc.surface.selectedFaces.size} faces)`);
        return Doc.surface.selectedFaces.size;
    }
    if (OpId === 'none')
    {
        Doc.surface.ClearSelection();
        SyncAllViews(true);
        Toast('Cleared Selection');
        return 0;
    }
    if (OpId === 'invert')
    {
        Doc.surface.InvertSelection(StudioState.selectionMode, Doc.islands);
        SyncAllViews(true);
        Toast('Inverted Selection');
        return Doc.surface.selectedUVs.size;
    }
    if (OpId === 'grow') return RunEdgeOrSeamOp('grow-sel');
    if (OpId === 'shrink') return RunEdgeOrSeamOp('shrink-sel');
    if (OpId === 'ring') return RunEdgeOrSeamOp('select-ring');
    if (OpId === 'boundary-loop') return RunEdgeOrSeamOp('select-boundary-auto');
    if (OpId === 'open-similar')
    {
        OpenSelectSimilarMenu(LastPointerClient[0], LastPointerClient[1]);
        return 0;
    }

    if (OpId === 'loop')
    {
        if (StudioState.selectionMode === 'face' || (!Doc.surface.selectedEdges3D.size && Doc.surface.selectedFaces.size))
        {
            const SeedFace = Doc.surface.activeFace ?? [...Doc.surface.selectedFaces][0];
            if (SeedFace == null)
            {
                Toast('Click a face first (or Alt+Click an edge in 3D/2D Face mode)');
                return 0;
            }
            const Cnt = Doc.surface.SelectFaceLoop3D(SeedFace, null, null, Extra.additive || false, Doc.islands);
            SyncAllViews(true);
            LogEvent('SEL', `Selected face loop (${Cnt} faces)`);
            Toast(`Face Loop: ${Cnt} faces`);
            return Cnt;
        }
        return RunEdgeOrSeamOp('select-loop');
    }

    if (OpId === 'loop-inner-region')
    {
        const Cnt = Doc.surface.SelectLoopInnerRegion(Doc.islands);
        if (!Cnt)
        {
            Toast('Select a closed edge boundary loop first');
            return 0;
        }
        SetSelectionMode('face');
        SyncAllViews(true);
        LogEvent('SEL', `Selected loop inner region (${Cnt} faces)`);
        Toast(`Loop Inner Region: ${Cnt} faces`);
        return Cnt;
    }

    if (OpId === 'same-island')
    {
        const Cnt = Doc.surface.SelectSameUvIsland(Extra.additive || false, Doc.islands);
        if (!Cnt)
        {
            Toast('Select any vertex, edge, face, or island first');
            return 0;
        }
        SyncAllViews(true);
        LogEvent('SEL', `Selected ${Doc.surface.selectedIslands.size} linked UV island(s) (${Cnt} faces)`);
        Toast(`Same UV Island: ${Doc.surface.selectedIslands.size} isl · ${Cnt} faces`);
        return Cnt;
    }

    if (OpId === 'linked-flat')
    {
        const Seed = Doc.surface.activeFace ?? [...Doc.surface.selectedFaces][0] ?? 0;
        const Cnt = Doc.surface.SelectLinkedFlatFaces(Seed, {
            angleDeg: StudioState.selectSimilarAngle,
            respectSeams: true,
            additive: Extra.additive || false,
            islands: Doc.islands
        });
        SetSelectionMode('face');
        SyncAllViews(true);
        LogEvent('SEL', `Selected ${Cnt} linked flat faces (<= ${StudioState.selectSimilarAngle}°)`);
        Toast(`Linked Flat Faces: ${Cnt} faces`);
        return Cnt;
    }

    if (OpId.startsWith('sim:'))
    {
        const Criterion = OpId.slice(4);
        const Cnt = Doc.surface.SelectSimilar(Criterion, {
            threshold: StudioState.selectSimilarTol,
            angleDeg: StudioState.selectSimilarAngle,
            additive: Extra.additive || false,
            islands: Doc.islands
        });
        if (!Cnt)
        {
            Toast('Select a seed element first to match similar properties');
            return 0;
        }
        SyncAllViews(true);
        LogEvent('SEL', `Select Similar (${Criterion}): ${Cnt} matched`);
        Toast(`Select Similar (${Criterion.replace(/_/g, ' ')}): ${Cnt}`);
        return Cnt;
    }

    if (OpId.startsWith('random'))
    {
        let Ratio = StudioState.selectRandomRatio;
        let Action = 'select';
        if (OpId === 'random-10') Ratio = 0.1;
        else if (OpId === 'random-25') Ratio = 0.25;
        else if (OpId === 'random-50') Ratio = 0.5;
        else if (OpId === 'random-75') Ratio = 0.75;
        else if (OpId === 'random-desel') { Ratio = 0.5; Action = 'deselect'; }
        else if (typeof Extra.ratio === 'number') Ratio = Extra.ratio;

        StudioState.selectRandomRatio = Ratio;
        StudioState.selectRandomSeed = (StudioState.selectRandomSeed + 101) & 0x7fffffff;
        const Mode = StudioState.selectionMode || 'face';
        const Cnt = Doc.surface.SelectRandom({
            ratio: Ratio,
            seed: StudioState.selectRandomSeed,
            action: Action,
            mode: Mode,
            additive: false,
            withinSelection: Action === 'deselect',
            islands: Doc.islands
        });
        SyncAllViews(true);
        LogEvent('SEL', `Random ${Action} (${Math.round(Ratio * 100)}% ${Mode}): ${Cnt} items`);
        Toast(`Random ${Action === 'deselect' ? 'Deselect' : 'Select'} (${Math.round(Ratio * 100)}%): ${Cnt}`);
        return Cnt;
    }

    if (OpId === 'checker')
    {
        const Mode = StudioState.selectionMode || 'face';
        const Cnt = Doc.surface.CheckerDeselect({ nth: 2, skip: 1, offset: 0, mode: Mode, islands: Doc.islands });
        SyncAllViews(true);
        LogEvent('SEL', `Checker deselect (${Mode}): ${Cnt} kept`);
        Toast(`Checker Deselect: ${Cnt} remaining`);
        return Cnt;
    }

    if (OpId === 'pinned')
    {
        Doc.surface.ClearSelection();
        for (const U of Doc.surface.pinned) Doc.surface.selectedUVs.add(U);
        Doc.surface.SyncSelectionsFromMode('vertex', Doc.islands);
        SetSelectionMode('vertex');
        SyncAllViews(true);
        Toast(`Selected ${Doc.surface.selectedUVs.size} pinned UV(s)`);
        return Doc.surface.selectedUVs.size;
    }

    return 0;
}

function RunUnwrap(Kind)
{
    const Doc = ActiveDoc();
    if (!Doc) return;
    RunWithUndo(`Unwrap: ${Kind}`, () =>
    {
        const T0 = performance.now();
        if (Kind === 'lscm')
        {
            const OnlySel = Doc.surface.selectedFaces.size > 0 && Doc.surface.selectedFaces.size < Doc.surface.faces.length;
            if (Doc.surface._hasClearedSeams)
            {
                Doc.surface.WeldNonSeamCorners(OnlySel ? [...Doc.surface.selectedFaces] : null);
                Doc.surface.SplitUVsAlongSeams();
                Doc.surface._hasClearedSeams = false;
            }
            const Count = UnwrapLscm(Doc.surface, { onlySelected: OnlySel });
            const Ms = performance.now() - T0;
            LogEvent('UNWRAP', `LSCM unwrapped ${Count} island(s) in ${Ms.toFixed(1)} ms`, 'live');
            Toast(`LSCM Unwrap: ${Count} islands`);
        }
        else if (Kind === 'smart')
        {
            const Count = SmartUvProject(Doc.surface, { angleLimitDeg: StudioState.smartAngleDeg, onlySelectedFaces: false });
            LogEvent('UNWRAP', `Smart UV Project (${StudioState.smartAngleDeg}°) -> ${Count} islands`, 'live');
            Toast(`Smart Project: ${Count} islands`);
        }
        else if (Kind === 'follow-quads')
        {
            const Count = FollowActiveQuads(Doc.surface);
            LogEvent('UNWRAP', `Follow Active Quads aligned ${Count} faces`, 'live');
            Toast(`Follow Quads: ${Count} faces`);
        }
        else
        {
            const Count = AnalyticProject(Doc.surface, Kind, { onlySelectedFaces: Doc.surface.selectedFaces.size > 0 && Doc.surface.selectedFaces.size < Doc.surface.faces.length });
            LogEvent('UNWRAP', `${Kind.toUpperCase()} projection (${Count} cluster/faces)`, 'live');
            Toast(`${Kind.toUpperCase()} Projection complete`);
        }
    });
}

/*==========================================================================================================================================
                                                 RIGHT INSPECTOR (SUN + FLUID UI)
==========================================================================================================================================*/
function RebuildInspector()
{
    const Doc = ActiveDoc();
    if (!Doc) return;

    if (StudioState.inspectorScope) ReleaseScope(StudioState.inspectorScope);
    const Scope = OpenScope();
    StudioState.inspectorScope = Scope;

    const Body = document.getElementById('inspector-body');
    Body.textContent = '';

    const Crumb = {
        packing: 'UV Studio / Importance Packing',
        unwrap: 'UV Studio / Unwrap & Topology',
        udim: 'UV Studio / UDIM Atlas Manager',
        check: 'UV Studio / Diagnostics & Export'
    }[StudioState.inspectorTab];
    document.getElementById('inspector-crumb').textContent = Crumb;

    if (StudioState.inspectorTab === 'packing') RenderPackingTab(Body, Doc);
    else if (StudioState.inspectorTab === 'unwrap') RenderUnwrapTab(Body, Doc);
    else if (StudioState.inspectorTab === 'udim') RenderUdimTab(Body, Doc);
    else RenderDiagnosticsTab(Body, Doc);

    CloseScope(Scope);
    Broadcast();
}

function RenderPackingTab(Body, Doc)
{
    // 1. Sun Hero Card: Tile Utilization + Live Importance Spectrum Bar
    const ActiveReport = Doc.lastPackReport?.tileReports?.find(R => R.tileId === Doc.activeUdim) || Doc.lastPackReport?.tileReports?.[0];
    const CurTileIslands = Doc.islands.filter(I => I.udimTile === Doc.activeUdim && !I.hidden);
    const RawTileOcc = CurTileIslands.reduce((S, I) => S + I.areaUV, 0) * 100;
    const DisplayOcc = ActiveReport ? ActiveReport.tileUtilizationPct : RawTileOcc;

    const Hero = Construct('div', 'hero-card');
    const GroupAreaById = new Map();
    let TotalUvAll = 0;
    for (const Isl of CurTileIslands)
    {
        TotalUvAll += Isl.areaUV;
        GroupAreaById.set(Isl.importanceGroupId || null, (GroupAreaById.get(Isl.importanceGroupId || null) || 0) + Isl.areaUV);
    }

    const Spectrum = Construct('div', 'allocation-spectrum');
    const ChipsRow = Construct('div', 'alloc-chips-row');

    const ExplicitSum = Doc.importanceGroups.reduce((S, G) => S + G.targetPercent, 0);
    for (const Grp of Doc.importanceGroups)
    {
        const ActualShare = TotalUvAll > 0 ? ((GroupAreaById.get(Grp.id) || 0) / TotalUvAll) * 100 : Grp.targetPercent;
        const Seg = Construct('div', 'alloc-segment');
        Seg.style.width = `${Math.max(2, ActualShare)}%`;
        Seg.style.background = Grp.color;
        Spectrum.append(Seg);
        const Dot = Construct('span', 'legend-dot');
        Dot.style.background = Grp.color;
        ChipsRow.append(Construct('span', 'alloc-chip', [Dot, `${Grp.name}: ${ActualShare.toFixed(1)}% / ${Grp.targetPercent}%`]));
    }
    const RemActual = TotalUvAll > 0 ? ((GroupAreaById.get(null) || 0) / TotalUvAll) * 100 : Math.max(0, 100 - ExplicitSum);
    if (RemActual > 0.1 || Doc.islands.some(I => !I.importanceGroupId))
    {
        const Seg = Construct('div', 'alloc-segment');
        Seg.style.width = `${Math.max(2, RemActual)}%`;
        Seg.style.background = '#6e7680';
        Spectrum.append(Seg);
        const Dot = Construct('span', 'legend-dot');
        Dot.style.background = '#6e7680';
        ChipsRow.append(Construct('span', 'alloc-chip', [Dot, `Remaining: ${RemActual.toFixed(1)}% / ${Math.max(0, 100 - ExplicitSum).toFixed(0)}%`]));
    }

    Hero.append(
        Construct('div', 'hero-top', [
            Construct('div', 'budget-chip', [
                Construct('span', 'mark', ConstructGlyph('Pack')),
                Construct('span', null, `UDIM ${Doc.activeUdim} Space Budget`)
            ]),
            Construct('span', 'estimate-tag', StudioState.packOptions.budgetMode === 'strict-ratio' ? 'STRICT RATIO' : 'FILL POCKETS')
        ]),
        Construct('div', 'hero-metrics', [
            Construct('div', 'metric-big', [document.createTextNode(DisplayOcc.toFixed(1)), Construct('small', null, '% filled')]),
            Construct('div', 'metric-side', [
                Construct('div', 'rate', `${CurTileIslands.length} islands`),
                Construct('div', 'low', `${Doc.importanceGroups.length} explicit · ${Doc.islands.filter(I => !I.importanceGroupId).length} auto`)
            ])
        ]),
        Spectrum,
        ChipsRow
    );
    Body.append(Hero);

    // 2. KPI Trio
    MetricTrio(Body, [
        {
            Label: 'Pack Utilization',
            Highlight: true,
            Read: () => ({
                value: `${DisplayOcc.toFixed(1)}%`,
                sub: `UDIM ${Doc.activeUdim} density`
            })
        },
        {
            Label: 'Selected Islands',
            Read: () => ({
                value: `${Doc.surface.selectedIslands.size}`,
                sub: Doc.surface.selectedIslands.size ? 'Ready to assign %' : 'Click islands to group'
            })
        },
        {
            Label: 'Remaining Pool',
            Read: () =>
            {
                const RemIslands = Doc.islands.filter(I => !I.importanceGroupId).length;
                const RemPct = Math.max(0, 100 - Doc.importanceGroups.reduce((S, G) => S + G.targetPercent, 0));
                return { value: `${RemPct.toFixed(0)}%`, sub: `${RemIslands} islands auto-fill` };
            }
        }
    ]);

    // 3. Importance Space Allocator Card (Interactive Selection -> Group % -> Remaining Auto)
    const AllocSec = SectionCard(Body, {
        Id: 'importance-allocator',
        Title: 'Importance Space Allocation',
        Glyph: 'Layers',
        Badge: 'TARGET %',
        Hint: 'Select islands (in 2D, 3D, or Outliner), assign a target UV space %, repeat for secondary groups, and remaining islands automatically fill leftover space.'
    });

    const SelCount = Doc.surface.selectedIslands.size;
    const SelArea3D = Doc.islands.filter(I => Doc.surface.selectedIslands.has(I.id)).reduce((S, I) => S + I.area3D, 0);
    const SelBox = Construct('div', 'selection-alloc-box');
    SelBox.append(
        Construct('div', 'sel-alloc-head', [
            Construct('strong', null, SelCount ? `${SelCount} Island${SelCount > 1 ? 's' : ''} Selected` : 'Select Islands to Allocate Space'),
            Construct('span', null, `${SelArea3D.toFixed(2)} m² 3D`)
        ])
    );

    const QuickPctRow = Construct('div', 'quick-pct-row');
    for (const Pct of [70, 50, 30, 20, 10, 5])
    {
        const B = Construct('button', 'quick-pct-btn', `${Pct}%`);
        B.type = 'button';
        B.addEventListener('click', () => { StudioState.pendingGroupPct = Pct; AssignSelectedIslandsToNewGroup(Pct); });
        QuickPctRow.append(B);
    }
    SelBox.append(QuickPctRow);

    SliderRow(SelBox, {
        Label: 'Custom Target Space',
        Minimum: 1,
        Maximum: 95,
        Step: 1,
        Digits: 0,
        Unit: '%',
        Read: () => StudioState.pendingGroupPct,
        Write: V => { StudioState.pendingGroupPct = V; }
    });

    ActionRow(SelBox, [
        { Label: 'Assign Selection -> New Group', Glyph: 'Plus', Tone: 'accent', Act: () => AssignSelectedIslandsToNewGroup(StudioState.pendingGroupPct) },
        { Label: 'Unassign -> Remainder', Glyph: 'Reset', Act: () => AssignSelectedToGroup(null) }
    ]);
    AllocSec.Inner.append(SelBox);

    // Explicit Group Cards
    for (const Grp of Doc.importanceGroups)
    {
        const Members = Doc.islands.filter(I => I.importanceGroupId === Grp.id);
        const ActualUv = Members.reduce((S, I) => S + I.areaUV, 0);
        const AllTileUv = Doc.islands.filter(I => I.udimTile === (Grp.udimTarget !== 'auto' ? Number(Grp.udimTarget) : Doc.activeUdim)).reduce((S, I) => S + I.areaUV, 0);
        const ActualPct = AllTileUv > 0 ? (ActualUv / AllTileUv) * 100 : 0;

        const Card = Construct('div', 'budget-group-card');
        const Dot = Construct('span', 'budget-color-dot');
        Dot.style.background = Grp.color;

        const NameIn = Construct('input', 'budget-name-input');
        NameIn.value = Grp.name;
        NameIn.addEventListener('change', () => { Grp.name = NameIn.value || Grp.name; SyncAllViews(); });

        const CountPill = Construct('button', 'budget-count-pill', `${Members.length} isl`);
        CountPill.type = 'button';
        CountPill.title = 'Click to select all islands in this group (or Shift+Click to add current selection)';
        CountPill.addEventListener('click', E =>
        {
            if (E.shiftKey) AssignSelectedToGroup(Grp.id);
            else
            {
                Doc.surface.SelectIslands(Members.map(M => M.id), false);
                SyncAllViews(true);
            }
        });

        const UdimSel = Construct('select', 'budget-udim-select');
        const AutoOpt = Construct('option', null, 'UDIM: Auto');
        AutoOpt.value = 'auto';
        UdimSel.append(AutoOpt);
        for (const T of Doc.udimTiles)
        {
            const O = Construct('option', null, `${T.id}`);
            O.value = String(T.id);
            UdimSel.append(O);
        }
        UdimSel.value = String(Grp.udimTarget || 'auto');
        UdimSel.addEventListener('change', () => { Grp.udimTarget = UdimSel.value; Broadcast(); });
        const UdimWrap = Construct('div', 'select-pill-wrap vp-compact-select budget-udim-wrap', [
            UdimSel,
            Construct('span', 'pill-caret-divider', ConstructGlyph('ChevronDown', 'glyph chev'))
        ]);

        const AddSelBtn = Construct('button', 'row-action-btn', ConstructGlyph('Plus'));
        AddSelBtn.type = 'button';
        AddSelBtn.title = 'Add currently selected islands to this group';
        AddSelBtn.addEventListener('click', () => AssignSelectedToGroup(Grp.id));

        const DelBtn = Construct('button', 'row-action-btn', ConstructGlyph('Trash'));
        DelBtn.type = 'button';
        DelBtn.title = 'Remove group (islands return to auto-fill remainder)';
        DelBtn.addEventListener('click', () =>
        {
            Doc.surface.PushUndo('Delete Budget Group');
            for (const M of Members) Doc.surface.AssignImportanceGroup([M.id], null);
            Doc.importanceGroups = Doc.importanceGroups.filter(G => G.id !== Grp.id);
            SyncAllViews(true);
        });

        Card.append(Construct('div', 'budget-group-head', [Dot, NameIn, CountPill, UdimWrap, AddSelBtn, DelBtn]));

        SliderRow(Card, {
            Label: 'Target Space Share',
            Minimum: 1,
            Maximum: 98,
            Step: 1,
            Digits: 0,
            Unit: '%',
            Read: () => Grp.targetPercent,
            Write: V => { Grp.targetPercent = V; RenderLegendStrip(Doc); }
        });

        const Drift = ActualPct - Grp.targetPercent;
        Card.append(Construct('div', 'budget-actual-row', [
            Construct('span', null, `Actual Share: ${ActualPct.toFixed(1)}%`),
            Construct('span', null, `${Drift >= 0 ? '+' : ''}${Drift.toFixed(1)}% vs target`)
        ]));
        AllocSec.Inner.append(Card);
    }

    // Automatic Remainder Pool Row
    const RemMembers = Doc.islands.filter(I => !I.importanceGroupId);
    const SumPct = Doc.importanceGroups.reduce((S, G) => S + G.targetPercent, 0);
    const RemTarget = Math.max(0, 100 - SumPct);
    AllocSec.Inner.append(Construct('div', 'budget-remainder-card', [
        Construct('div', null, [
            Construct('strong', null, 'Remaining Unassigned Islands'),
            Construct('small', null, `${RemMembers.length} islands automatically fill leftover space`)
        ]),
        Construct('span', 'remainder-pct-badge', `${RemTarget.toFixed(0)}%`)
    ]));

    // 4. Packing Solver Options Card
    const PackSec = SectionCard(Body, {
        Id: 'pack-options',
        Title: 'Concave Packing Engine',
        Glyph: 'Pack',
        Badge: 'EXACT SHAPE'
    });
    SegmentedRow(PackSec.Inner, {
        Label: 'Budget Enforcement',
        Options: [['Strict Ratio', 'strict-ratio'], ['Fill Pockets', 'fill-remainder']],
        Read: () => StudioState.packOptions.budgetMode,
        Write: V => { StudioState.packOptions.budgetMode = V; }
    });
    SelectRow(PackSec.Inner, {
        Label: 'UDIM Packing Scope',
        Options: [
            ['Pack into Active UDIM Tile', 'active-udim'],
            ['Pack Per Assigned UDIM Tile', 'per-udim'],
            ['Auto-Spill Overflow to New UDIMs', 'auto-udim-spill'],
            ['Pack Selected Islands Only', 'selected-only']
        ],
        Read: () => StudioState.packOptions.scope,
        Write: V => { StudioState.packOptions.scope = V; }
    });
    SliderRow(PackSec.Inner, {
        Label: 'Island Margin Padding',
        Minimum: 0,
        Maximum: 64,
        Step: 2,
        Digits: 0,
        Unit: 'px',
        Read: () => StudioState.packOptions.marginPx,
        Write: V => { StudioState.packOptions.marginPx = V; }
    });
    SegmentedRow(PackSec.Inner, {
        Label: 'Rotation Steps',
        Options: [['0° Locked', '0'], ['90° Ortho', '90'], ['45° Oct', '45'], ['15° Fine', '15']],
        Read: () => StudioState.packOptions.rotations,
        Write: V => { StudioState.packOptions.rotations = V; }
    });
    SwitchRow(PackSec.Inner, {
        Label: 'Equalize Intra-Group Scale',
        Detail: 'Normalize texel density inside each priority group before scaling',
        Read: () => StudioState.packOptions.equalizeWithinGroup,
        Write: V => { StudioState.packOptions.equalizeWithinGroup = V; }
    });
    ActionRow(PackSec.Inner, [
        { Label: 'Pack with Importance Budgets (Shift+P)', Glyph: 'Pack', Tone: 'accent', Act: ExecuteImportancePack }
    ]);
}

function RenderUnwrapTab(Body, Doc)
{
    const UnwrapSec = SectionCard(Body, {
        Id: 'unwrap-ops',
        Title: 'Unwrap & Analytic Projections',
        Glyph: 'Unwrap',
        Badge: 'BLENDER TOOLSET'
    });
    ActionRow(UnwrapSec.Inner, [
        { Label: 'LSCM Unwrap (U)', Glyph: 'Unwrap', Tone: 'accent', Act: () => RunUnwrap('lscm') },
        { Label: 'Smart UV Project', Glyph: 'Sparkles', Act: () => RunUnwrap('smart') }
    ]);
    ActionRow(UnwrapSec.Inner, [
        { Label: 'Cube', Act: () => RunUnwrap('cube') },
        { Label: 'Cylinder', Act: () => RunUnwrap('cylinder') },
        { Label: 'Sphere', Act: () => RunUnwrap('sphere') },
        { Label: 'Follow Quads', Act: () => RunUnwrap('follow-quads') }
    ]);
    ActionRow(UnwrapSec.Inner, [
        { Label: 'View Project', Act: () => RunUnwrap('view') },
        { Label: 'Reset UVs', Act: () => RunUnwrap('reset') }
    ]);
    SliderRow(UnwrapSec.Inner, {
        Label: 'Smart Project Angle Limit',
        Minimum: 10,
        Maximum: 89,
        Step: 1,
        Digits: 0,
        Unit: '°',
        Read: () => StudioState.smartAngleDeg,
        Write: V => { StudioState.smartAngleDeg = V; }
    });
    SwitchRow(UnwrapSec.Inner, {
        Label: 'Live Unwrap',
        Detail: 'Re-solve LSCM continuously when pinning or moving pins',
        Read: () => StudioState.liveUnwrap,
        Write: V => { StudioState.liveUnwrap = V; }
    });

    const RelaxSec = SectionCard(Body, {
        Id: 'relax-density',
        Title: 'Minimize Stretch & Texel Density',
        Glyph: 'Relax',
        Badge: 'PX/CM'
    });
    SliderRow(RelaxSec.Inner, {
        Label: 'Relax Iterations',
        Minimum: 4,
        Maximum: 60,
        Step: 2,
        Digits: 0,
        Unit: 'it',
        Read: () => StudioState.relaxIterations,
        Write: V => { StudioState.relaxIterations = V; }
    });
    SliderRow(RelaxSec.Inner, {
        Label: 'Target Texel Density',
        Minimum: 1,
        Maximum: 64,
        Step: 0.5,
        Digits: 1,
        Unit: 'px/cm',
        Read: () => StudioState.targetTexelDensity,
        Write: V => { StudioState.targetTexelDensity = V; SyncAllViews(); }
    });
    ActionRow(RelaxSec.Inner, [
        {
            Label: 'Minimize Stretch (Ctrl+V)',
            Glyph: 'Relax',
            Tone: 'warm',
            Act: () => RunWithUndo('Minimize Stretch', () =>
            {
                const Moved = MinimizeStretch(Doc.surface, { iterations: StudioState.relaxIterations, onlySelected: Doc.surface.selectedUVs.size > 0, islands: Doc.islands });
                LogEvent('RELAX', `Minimized stretch (${Moved} UVs, ${StudioState.relaxIterations} passes)`, 'live');
                Toast(`Stretch relaxed (${Moved} UVs)`);
            })
        },
        {
            Label: 'Average Scale (Ctrl+A)',
            Glyph: 'Scale',
            Act: () => RunWithUndo('Average Scale', () =>
            {
                AverageIslandScale(Doc.surface, Doc.islands, Doc.surface.selectedIslands.size > 0);
                LogEvent('SCALE', 'Equalized island texel density');
                Toast('Average Island Scale applied');
            })
        }
    ]);
    ActionRow(RelaxSec.Inner, [
        {
            Label: 'Apply Target Texel Density',
            Glyph: 'Gauge',
            Act: () => RunWithUndo('Apply Texel Density', () =>
            {
                const TileResMap = new Map(Doc.udimTiles.map(T => [T.id, T.resolution || 2048]));
                const Islands = Doc.surface.ExtractIslands(Id => TileResMap.get(Id) || 2048, 100);
                const Targets = Doc.surface.selectedIslands.size
                    ? Islands.filter(I => Doc.surface.selectedIslands.has(I.id))
                    : Islands;
                for (const Isl of Targets)
                {
                    if (Isl.texelDensity <= 1e-6) continue;
                    const K = StudioState.targetTexelDensity / Isl.texelDensity;
                    const Cx = Isl.bounds.centerU, Cy = Isl.bounds.centerV;
                    for (const UvIdx of Isl.uvIndices)
                    {
                        Doc.surface.uvCoords[UvIdx][0] = Cx + (Doc.surface.uvCoords[UvIdx][0] - Cx) * K;
                        Doc.surface.uvCoords[UvIdx][1] = Cy + (Doc.surface.uvCoords[UvIdx][1] - Cy) * K;
                    }
                }
                Toast(`Scaled ${Targets.length} islands to ${StudioState.targetTexelDensity} px/cm`);
            })
        }
    ]);

    const TopoSec = SectionCard(Body, {
        Id: 'edges-boundaries-seams',
        Title: 'Edges, Boundaries & Seams',
        Glyph: 'Scissors',
        Badge: `${Doc.surface.seams.size} SEAMS`,
        Hint: 'Select 2 Edge mode to click edges (Alt Loop · Ctrl+Alt Ring · Ctrl Shortest Path) or use Seam Brush [K].'
    });
    SliderRow(TopoSec.Inner, {
        Label: 'Sharp Edge Angle Threshold',
        Minimum: 5,
        Maximum: 120,
        Step: 1,
        Digits: 0,
        Unit: '°',
        Read: () => StudioState.sharpAngleDeg,
        Write: V => { StudioState.sharpAngleDeg = V; }
    });
    ActionRow(TopoSec.Inner, [
        { Label: 'Edge Loop', Glyph: 'Edge', Act: () => RunEdgeOrSeamOp('select-loop') },
        { Label: 'Edge Ring', Act: () => RunEdgeOrSeamOp('select-ring') },
        { Label: 'Boundary Loop', Act: () => RunEdgeOrSeamOp('select-boundary-auto') },
        { Label: 'Sharp Edges', Act: () => RunEdgeOrSeamOp('select-sharp') }
    ]);
    ActionRow(TopoSec.Inner, [
        { Label: 'Select Seams', Act: () => RunEdgeOrSeamOp('select-seams') },
        { Label: 'Open Mesh Bounds', Act: () => RunEdgeOrSeamOp('select-boundary-mesh') },
        { Label: 'Grow (+)', Act: () => RunEdgeOrSeamOp('grow-sel') },
        { Label: 'Shrink (-)', Act: () => RunEdgeOrSeamOp('shrink-sel') }
    ]);
    ActionRow(TopoSec.Inner, [
        { Label: 'Mark Seam', Glyph: 'Scissors', Tone: 'accent', Act: () => RunEdgeOrSeamOp('mark-seam') },
        { Label: 'Clear Seam', Act: () => RunEdgeOrSeamOp('clear-seam') },
        { Label: 'Clear & Weld', Act: () => RunEdgeOrSeamOp('clear-seam-weld') }
    ]);
    ActionRow(TopoSec.Inner, [
        { Label: 'Mark Bounds -> Seams', Tone: 'warm', Act: () => RunEdgeOrSeamOp('mark-boundary-seam') },
        { Label: 'Mark Sharp -> Seams', Act: () => RunEdgeOrSeamOp('mark-sharp-seam') },
        { Label: 'Seams from Islands', Act: () => RunEdgeOrSeamOp('seams-from-islands') }
    ]);

    const PinWeldSec = SectionCard(Body, {
        Id: 'topo-pins',
        Title: 'Pins, Split, Stitch & Weld',
        Glyph: 'Pin',
        Badge: 'WELD / SPLIT'
    });
    ActionRow(PinWeldSec.Inner, [
        { Label: 'Pin (P)', Glyph: 'Pin', Act: () => RunWithUndo('Pin', () => { Doc.surface.PinSelected(true); }) },
        { Label: 'Unpin (Alt+P)', Glyph: 'PinOff', Act: () => RunWithUndo('Unpin', () => { Doc.surface.PinSelected(false); }) },
        { Label: 'Pin Boundary', Act: () => RunWithUndo('Pin Boundary', () => { const C = Doc.surface.PinIslandBoundaries(); Toast(`Pinned ${C} boundary verts`); }) }
    ]);
    ActionRow(PinWeldSec.Inner, [
        { Label: 'Split Island (Y)', Glyph: 'Split', Act: () => RunWithUndo('Split Island', () => { Doc.surface.SplitSelection(); }) },
        { Label: 'Stitch (V)', Glyph: 'Stitch', Act: () => RunWithUndo('Stitch', () => { const N = Doc.surface.StitchSelected(Doc.islands); Toast(`Stitched ${N} UV pairs`); }) },
        { Label: 'Merge / Weld (M)', Glyph: 'Merge', Act: () => RunWithUndo('Weld', () => { const N = Doc.surface.WeldSelected(); Toast(`Merged ${N} UVs`); }) }
    ]);

    const AlignSec = SectionCard(Body, {
        Id: 'align-rectify',
        Title: 'Align, Straighten, Mirror & Rectify',
        Glyph: 'Align',
        Badge: 'UV PRECISION'
    });
    const AlignGrid = Construct('div', 'align-grid');
    const AlignOps = [
        ['Align U Ctr', 'align-u-center'], ['Align V Ctr', 'align-v-center'], ['Straighten', 'straighten'],
        ['Align Min U', 'align-u-min'], ['Align Max U', 'align-u-max'], ['Rectify Quads', 'rectify-quads'],
        ['Align Top V', 'align-v-max'], ['Align Bot V', 'align-v-min'], ['Mirror U', 'mirror-u']
    ];
    for (const [Lbl, Op] of AlignOps)
    {
        const Btn = Construct('button', 'action-button', Lbl);
        Btn.type = 'button';
        Btn.addEventListener('click', () =>
        {
            RunWithUndo(Lbl, () =>
            {
                const Piv = ComputeSelectionPivot(Doc) || StudioState.cursorUV;
                ApplyAlignOrRectify(Doc.surface, Op, Piv);
                Toast(`${Lbl} applied`);
            });
        });
        AlignGrid.append(Btn);
    }
    AlignSec.Inner.append(AlignGrid);

    const BlenderSelSec = SectionCard(Body, {
        Id: 'blender-selection-tools',
        Title: 'Blender Selection, Loops & Random',
        Glyph: 'Select',
        Badge: 'SHIFT+G / RANDOM'
    });
    SliderPill(BlenderSelSec.Inner, {
        Label: 'Similar Tolerance',
        Min: 1,
        Max: 80,
        Step: 1,
        Digits: 0,
        Unit: '%',
        Read: () => Math.round(StudioState.selectSimilarTol * 100),
        Write: V => { StudioState.selectSimilarTol = V / 100; }
    });
    SliderPill(BlenderSelSec.Inner, {
        Label: 'Normal / Flat Angle',
        Min: 1,
        Max: 90,
        Step: 1,
        Digits: 0,
        Unit: '°',
        Read: () => StudioState.selectSimilarAngle,
        Write: V => { StudioState.selectSimilarAngle = V; }
    });
    SliderPill(BlenderSelSec.Inner, {
        Label: 'Select Random Ratio',
        Min: 5,
        Max: 95,
        Step: 5,
        Digits: 0,
        Unit: '%',
        Read: () => Math.round(StudioState.selectRandomRatio * 100),
        Write: V => { StudioState.selectRandomRatio = V / 100; }
    });
    ActionRow(BlenderSelSec.Inner, [
        { Label: 'Face / Edge Loop', Act: () => RunSelectOp('loop') },
        { Label: 'Inner Region', Act: () => RunSelectOp('loop-inner-region') },
        { Label: 'Flat Faces', Act: () => RunSelectOp('linked-flat') },
        { Label: 'Same UV Island', Tone: 'accent', Act: () => RunSelectOp('same-island') }
    ]);
    ActionRow(BlenderSelSec.Inner, [
        { Label: 'Sim: UV Perim', Act: () => RunSelectOp('sim:perimeter_uv') },
        { Label: 'Sim: 3D Perim', Act: () => RunSelectOp('sim:perimeter_3d') },
        { Label: 'Sim: 3D Area', Act: () => RunSelectOp('sim:area_3d') },
        { Label: 'Sim: Normal', Act: () => RunSelectOp('sim:normal') }
    ]);
    ActionRow(BlenderSelSec.Inner, [
        { Label: 'Select Random', Tone: 'warm', Act: () => RunSelectOp('random') },
        { Label: 'Checker Desel', Act: () => RunSelectOp('checker') },
        { Label: 'Shift+G Menu', Act: () => OpenSelectSimilarMenu(LastPointerClient[0], LastPointerClient[1]) }
    ]);
}

function RenderUdimTab(Body, Doc)
{
    const GridSec = SectionCard(Body, {
        Id: 'udim-matrix',
        Title: 'UDIM Tile Matrix (1001+)',
        Glyph: 'Grid',
        Badge: `${Doc.udimTiles.length} ACTIVE`,
        Hint: 'Mari / Blender UDIM addressing: 1001 + u + v*10. Click any slot to focus or activate.'
    });

    const Matrix = Construct('div', 'udim-matrix-grid');
    const TileMap = new Map(Doc.udimTiles.map(T => [T.id, T]));
    const CntMap = new Map();
    for (const Isl of Doc.islands) CntMap.set(Isl.udimTile, (CntMap.get(Isl.udimTile) || 0) + 1);

    for (let row = 2; row >= 0; row--)
    {
        for (let col = 0; col < 4; col++)
        {
            const Id = 1001 + col + row * 10;
            const Tile = TileMap.get(Id);
            const Cell = Construct('button', 'udim-cell-btn' + (Doc.activeUdim === Id ? ' active' : ''), [
                Construct('strong', null, String(Id)),
                Construct('small', null, Tile ? `${Tile.resolution >= 1024 ? Tile.resolution / 1024 + 'K' : Tile.resolution} · ${CntMap.get(Id) || 0} isl` : 'Empty (+)')
            ]);
            Cell.type = 'button';
            Cell.addEventListener('click', () =>
            {
                if (!Tile)
                {
                    Doc.udimTiles.push({ id: Id, label: `Tile ${Id}`, resolution: 2048, checkerMode: 'color' });
                    Doc.udimTiles.sort((A, B) => A.id - B.id);
                    LogEvent('UDIM', `Added UDIM tile ${Id}`, 'live');
                }
                Doc.activeUdim = Id;
                const O = UdimOrigin(Id);
                Canvas2D.FrameBounds(O.u, O.v, O.u + 1, O.v + 1, 0.14);
                SyncAllViews(true);
            });
            Matrix.append(Cell);
        }
    }
    GridSec.Inner.append(Matrix);

    ActionRow(GridSec.Inner, [
        {
            Label: 'Move Selected -> Active UDIM',
            Glyph: 'Move',
            Tone: 'accent',
            Act: () => RunWithUndo('Move to UDIM', () =>
            {
                if (!Doc.surface.selectedIslands.size) { Toast('Select islands first'); return; }
                const Moved = Doc.surface.MoveIslandsToUdim([...Doc.surface.selectedIslands], Doc.activeUdim, Doc.islands);
                Toast(`Moved ${Moved} island(s) to UDIM ${Doc.activeUdim}`);
            })
        },
        {
            Label: 'Equalize Cross-UDIM Density',
            Glyph: 'Gauge',
            Act: () => RunWithUndo('Cross-UDIM Density Match', () =>
            {
                const TileResMap = new Map(Doc.udimTiles.map(T => [T.id, T.resolution || 2048]));
                const Islands = Doc.surface.ExtractIslands(Id => TileResMap.get(Id) || 2048, 100);
                const MeanDen = Islands.reduce((S, I) => S + I.texelDensity, 0) / Math.max(1, Islands.length);
                for (const Isl of Islands)
                {
                    if (Isl.texelDensity <= 1e-6) continue;
                    const K = MeanDen / Isl.texelDensity;
                    const Cx = Isl.bounds.centerU, Cy = Isl.bounds.centerV;
                    for (const UvIdx of Isl.uvIndices)
                    {
                        Doc.surface.uvCoords[UvIdx][0] = Cx + (Doc.surface.uvCoords[UvIdx][0] - Cx) * K;
                        Doc.surface.uvCoords[UvIdx][1] = Cy + (Doc.surface.uvCoords[UvIdx][1] - Cy) * K;
                    }
                }
                Toast(`Matched texel density across mixed-resolution UDIMs (${MeanDen.toFixed(1)} px/cm)`);
            })
        }
    ]);

    const ActiveTile = Doc.udimTiles.find(T => T.id === Doc.activeUdim) || Doc.udimTiles[0];
    if (ActiveTile)
    {
        const TileSec = SectionCard(Body, {
            Id: 'udim-tile-props',
            Title: `UDIM ${ActiveTile.id} Properties`,
            Glyph: 'Sliders',
            Badge: ActiveTile.label || 'TILE'
        });

        SelectRow(TileSec.Inner, {
            Label: 'Tile Texture Resolution',
            Options: [['512 px', 512], ['1024 px (1K)', 1024], ['2048 px (2K)', 2048], ['4096 px (4K)', 4096], ['8192 px (8K)', 8192]],
            Read: () => ActiveTile.resolution,
            Write: V => { ActiveTile.resolution = Number(V); SyncAllViews(); }
        });
        SelectRow(TileSec.Inner, {
            Label: 'Tile Backing Pattern',
            Options: [['Color Calibration Grid', 'color'], ['UV Letter Grid', 'uv'], ['Classic Checkerboard', 'checker'], ['Solid Slate', 'blank']],
            Read: () => ActiveTile.checkerMode || 'color',
            Write: V => { ActiveTile.checkerMode = V; SyncAllViews(); }
        });
        ActionRow(TileSec.Inner, [
            { Label: 'Load Custom Texture…', Glyph: 'Upload', Act: () => document.getElementById('tex-file-input').click() },
            {
                Label: 'Remove Tile',
                Glyph: 'Trash',
                Availability: () => ({ Enabled: Doc.udimTiles.length > 1, Reason: 'At least 1 UDIM tile is required' }),
                Act: () =>
                {
                    Doc.udimTiles = Doc.udimTiles.filter(T => T.id !== ActiveTile.id);
                    Doc.activeUdim = Doc.udimTiles[0].id;
                    SyncAllViews(true);
                }
            }
        ]);
    }
}

function RenderDiagnosticsTab(Body, Doc)
{
    const D = Doc.diagnostics;
    MetricTrio(Body, [
        { Label: 'Overlapping Faces', Highlight: D.overlapCount === 0, Read: () => ({ value: `${D.overlapCount}`, sub: D.overlapCount ? 'Check Overlap lens' : 'Clean non-overlapping' }) },
        { Label: 'Flipped Winding', Read: () => ({ value: `${D.flippedCount}`, sub: D.flippedCount ? 'Inverted UV normals' : 'All positive facing' }) },
        { Label: 'Mean Angle Stretch', Read: () => ({ value: `${(D.meanAngleStretch * 100).toFixed(1)}%`, sub: `Max ${(D.maxAngleStretch * 100).toFixed(1)}%` }) }
    ]);

    const RepairSec = SectionCard(Body, {
        Id: 'diag-repair',
        Title: 'UV Integrity & Repair',
        Glyph: 'Check',
        Badge: D.overlapCount ? 'ATTENTION' : 'CLEAN'
    });
    ActionRow(RepairSec.Inner, [
        {
            Label: 'Select Overlaps',
            Glyph: 'Alert',
            Act: () =>
            {
                Doc.surface.ClearSelection();
                for (const Fi of D.OverlappingFaces) Doc.surface.selectedFaces.add(Fi);
                Doc.surface.SyncDerivedSelectionFromFaces();
                StudioState.overlayLens = 'overlap';
                document.getElementById('uv-lens-select').value = 'overlap';
                SyncAllViews();
                Toast(`Selected ${D.overlapCount} overlapping faces`);
            }
        },
        {
            Label: 'Select Flipped',
            Act: () =>
            {
                Doc.surface.ClearSelection();
                for (const Fi of D.FlippedFaces) Doc.surface.selectedFaces.add(Fi);
                Doc.surface.SyncDerivedSelectionFromFaces();
                StudioState.overlayLens = 'overlap';
                document.getElementById('uv-lens-select').value = 'overlap';
                SyncAllViews();
                Toast(`Selected ${D.flippedCount} flipped faces`);
            }
        }
    ]);
    ActionRow(RepairSec.Inner, [
        {
            Label: 'Flip UV Winding',
            Glyph: 'Mirror',
            Act: () => RunWithUndo('Flip UVs', () =>
            {
                const Piv = ComputeSelectionPivot(Doc) || [0.5, 0.5];
                Doc.surface.TransformSelectedUVs({ pivot: Piv, scaleU: -1, scaleV: 1 });
                Toast('Flipped selected UVs');
            })
        },
        {
            Label: 'Auto-Relax & Repack',
            Glyph: 'Sparkles',
            Tone: 'accent',
            Act: () =>
            {
                Doc.surface.PushUndo('Auto Repair & Pack');
                MinimizeStretch(Doc.surface, { iterations: 16, onlySelected: false, islands: Doc.islands });
                SolveImportancePack(Doc.surface, {
                    importanceGroups: Doc.importanceGroups,
                    udimTiles: Doc.udimTiles,
                    activeUdim: Doc.activeUdim,
                    options: StudioState.packOptions
                });
                Toast('Relaxed & repacked atlas with 0 overlaps');
                SyncAllViews(true);
            }
        }
    ]);
}

/*==========================================================================================================================================
                                                 2D UV CANVAS INTERACTION & HOTKEYS
==========================================================================================================================================*/
function SnapUV(Doc, U, V, ExcludeUVs)
{
    if (StudioState.snapMode === 'grid')
    {
        const Step = 1 / 16;
        return [Math.round(U / Step) * Step, Math.round(V / Step) * Step];
    }
    if (StudioState.snapMode === 'pixel')
    {
        const ActiveTile = Doc.udimTiles.find(T => T.id === Doc.activeUdim) || Doc.udimTiles[0];
        const Res = ActiveTile?.resolution || 2048;
        return [Math.round(U * Res) / Res, Math.round(V * Res) / Res];
    }
    if (StudioState.snapMode === 'vertex')
    {
        let BestU = U, BestV = V, BestD = 14 / Canvas2D.scalePx;
        for (let i = 0; i < Doc.surface.uvCoords.length; i++)
        {
            if (ExcludeUVs && ExcludeUVs.has(i)) continue;
            const P = Doc.surface.uvCoords[i];
            const D = Math.hypot(P[0] - U, P[1] - V);
            if (D < BestD) { BestD = D; BestU = P[0]; BestV = P[1]; }
        }
        return [BestU, BestV];
    }
    return [U, V];
}

function BeginModalTransform(Mode, PointerX = null, PointerY = null)
{
    const Doc = ActiveDoc();
    if (!Doc || !Doc.surface.selectedUVs.size)
    {
        Toast('Select vertices, edges, faces, or islands before transforming');
        return;
    }
    const Pivot = ComputeSelectionPivot(Doc) || StudioState.cursorUV;
    const Sx = PointerX ?? Canvas2D.UvToScreen(HoverUvPos[0], HoverUvPos[1])[0];
    const Sy = PointerY ?? Canvas2D.UvToScreen(HoverUvPos[0], HoverUvPos[1])[1];
    const [Su, Sv] = Canvas2D.ScreenToUv(Sx, Sy);

    const OriginalCoords = new Map();
    const TargetPool = StudioState.proportional.enabled
        ? Doc.surface.uvCoords.map((_, idx) => idx)
        : [...Doc.surface.selectedUVs];
    for (const UvIdx of TargetPool)
    {
        OriginalCoords.set(UvIdx, [Doc.surface.uvCoords[UvIdx][0], Doc.surface.uvCoords[UvIdx][1]]);
    }

    ModalTransform = {
        mode: Mode, // 'move' | 'rotate' | 'scale'
        axis: null, // null | 'x' | 'y'
        numericBuffer: '',
        pivot: Pivot,
        startScreenX: Sx,
        startScreenY: Sy,
        curScreenX: Sx,
        curScreenY: Sy,
        startUV: [Su, Sv],
        originalCoords: OriginalCoords,
        selectedSet: new Set(Doc.surface.selectedUVs),
        undoSnapshot: Doc.surface.CreateSnapshot('Modal Transform')
    };
    UpdateModalTransform(Sx, Sy);
}

function UpdateModalTransform(Sx, Sy)
{
    if (!ModalTransform) return;
    const Doc = ActiveDoc();
    ModalTransform.curScreenX = Sx;
    ModalTransform.curScreenY = Sy;

    const [Cu, Cv] = Canvas2D.ScreenToUv(Sx, Sy);
    const [Su, Sv] = ModalTransform.startUV;
    const [Pu, Pv] = ModalTransform.pivot;

    let Du = 0, Dv = 0, Angle = 0, ScaleU = 1, ScaleV = 1;
    const HasNum = ModalTransform.numericBuffer !== '' && ModalTransform.numericBuffer !== '-';
    const NumVal = HasNum ? Number(ModalTransform.numericBuffer) : NaN;

    if (ModalTransform.mode === 'move')
    {
        Du = Number.isFinite(NumVal) ? NumVal : (Cu - Su);
        Dv = Number.isFinite(NumVal) ? (ModalTransform.axis === 'y' ? NumVal : 0) : (Cv - Sv);
        if (ModalTransform.axis === 'x') Dv = 0;
        else if (ModalTransform.axis === 'y') { if (!Number.isFinite(NumVal)) Du = 0; else Du = 0; }
        if (!Number.isFinite(NumVal) && StudioState.snapMode !== 'off')
        {
            const Snapped = SnapUV(Doc, Pu + Du, Pv + Dv, ModalTransform.selectedSet);
            Du = Snapped[0] - Pu;
            Dv = Snapped[1] - Pv;
        }
    }
    else if (ModalTransform.mode === 'rotate')
    {
        if (Number.isFinite(NumVal)) Angle = (NumVal * Math.PI) / 180;
        else
        {
            const A0 = Math.atan2(Sv - Pv, Su - Pu);
            const A1 = Math.atan2(Cv - Pv, Cu - Pu);
            Angle = A1 - A0;
        }
    }
    else if (ModalTransform.mode === 'scale')
    {
        let Factor = 1;
        if (Number.isFinite(NumVal)) Factor = NumVal;
        else
        {
            const D0 = Math.max(0.02, Math.hypot(Su - Pu, Sv - Pv));
            const D1 = Math.hypot(Cu - Pu, Cv - Pv);
            Factor = Math.max(0.04, D1 / D0);
        }
        ScaleU = ModalTransform.axis === 'y' ? 1 : Factor;
        ScaleV = ModalTransform.axis === 'x' ? 1 : Factor;
    }

    const Cos = Math.cos(Angle), Sin = Math.sin(Angle);
    const PropOn = StudioState.proportional.enabled;
    const PropR = Math.max(0.01, StudioState.proportional.radius);

    for (const [UvIdx, Orig] of ModalTransform.originalCoords)
    {
        let W = ModalTransform.selectedSet.has(UvIdx) ? 1 : 0;
        if (W === 0 && PropOn)
        {
            const Dist = Math.hypot(Orig[0] - Pu, Orig[1] - Pv);
            if (Dist < PropR)
            {
                const T = 1 - Dist / PropR;
                W = T * T * (3 - 2 * T);
            }
        }
        if (W <= 0)
        {
            Doc.surface.uvCoords[UvIdx][0] = Orig[0];
            Doc.surface.uvCoords[UvIdx][1] = Orig[1];
            continue;
        }
        let Nu = Orig[0] - Pu, Nv = Orig[1] - Pv;
        if (ModalTransform.mode === 'scale') { Nu *= ScaleU; Nv *= ScaleV; }
        else if (ModalTransform.mode === 'rotate') { const Ru = Nu * Cos - Nv * Sin, Rv = Nu * Sin + Nv * Cos; Nu = Ru; Nv = Rv; }
        const Tx = Pu + Nu + Du, Ty = Pv + Nv + Dv;
        Doc.surface.uvCoords[UvIdx][0] = Orig[0] + (Tx - Orig[0]) * W;
        Doc.surface.uvCoords[UvIdx][1] = Orig[1] + (Ty - Orig[1]) * W;
    }

    const Banner = document.getElementById('modal-transform-banner');
    Banner.hidden = false;
    const AxisStr = ModalTransform.axis ? ` [${ModalTransform.axis.toUpperCase()} LOCK]` : '';
    if (ModalTransform.mode === 'move') Banner.textContent = `GRAB${AxisStr}: dU ${Du.toFixed(3)}, dV ${Dv.toFixed(3)} · LMB/Enter confirm · RMB/Esc cancel`;
    else if (ModalTransform.mode === 'rotate') Banner.textContent = `ROTATE: ${(Angle * 180 / Math.PI).toFixed(1)}° · Type degrees · LMB/Enter confirm`;
    else Banner.textContent = `SCALE${AxisStr}: ${ScaleU.toFixed(2)}×, ${ScaleV.toFixed(2)}× · LMB/Enter confirm`;

    if (Viewport3D) Viewport3D.FastUpdateUvsOnly(Doc.surface);
    ScheduleFast2DRender(Doc);
}

function ConfirmModalTransform()
{
    if (!ModalTransform) return;
    const Doc = ActiveDoc();
    Doc.surface.undoStack.push(ModalTransform.undoSnapshot);
    Doc.surface.redoStack = [];
    if (Doc.surface.undoStack.length > 60) Doc.surface.undoStack.shift();
    if (StudioState.liveUnwrap) UnwrapLscm(Doc.surface, {});
    const Mode = ModalTransform.mode;
    ModalTransform = null;
    document.getElementById('modal-transform-banner').hidden = true;
    LogEvent('EDIT', `Committed ${Mode} transform`);
    SyncAllViews(true);
}

function CancelModalTransform()
{
    if (!ModalTransform) return;
    const Doc = ActiveDoc();
    Doc.surface.RestoreSnapshot(ModalTransform.undoSnapshot);
    ModalTransform = null;
    document.getElementById('modal-transform-banner').hidden = true;
    SyncAllViews(false);
}

function SetupCanvas2DInteraction()
{
    const CanvasEl = document.getElementById('uv-canvas-2d');
    let DragAction = null;

    CanvasEl.addEventListener('contextmenu', E => E.preventDefault());

    CanvasEl.addEventListener('dblclick', E =>
    {
        const Doc = ActiveDoc();
        if (!Doc || StudioState.activeTool === 'seam') return;
        const Rect = CanvasEl.getBoundingClientRect();
        const Sx = E.clientX - Rect.left, Sy = E.clientY - Rect.top;
        if (StudioState.selectionMode === 'edge')
        {
            const Hit = Canvas2D.HitTest(Sx, Sy, {
                surface: Doc.surface, islands: Doc.islands, mode: 'edge', activeTool: StudioState.activeTool, udimTiles: Doc.udimTiles
            });
            if (Hit?.kind === 'edge')
            {
                const Cnt = Doc.surface.SelectUvEdgeLoop(Hit.uvA, Hit.uvB, Doc.islands, E.shiftKey);
                Toast(`UV Edge Loop: ${Cnt} edges`);
                SyncAllViews(true);
            }
            return;
        }
        if (StudioState.selectionMode === 'face' || StudioState.selectionMode === 'island')
        {
            const Hit = Canvas2D.HitTest(Sx, Sy, {
                surface: Doc.surface, islands: Doc.islands, mode: 'island', activeTool: StudioState.activeTool, udimTiles: Doc.udimTiles
            });
            if (Hit?.islandId !== undefined)
            {
                Doc.surface.SelectIsland(Hit.islandId, E.shiftKey, Doc.islands);
                Toast(`Linked UV Island selected`);
                SyncAllViews(true);
            }
        }
    });

    CanvasEl.addEventListener('pointerdown', E =>
    {
        const Doc = ActiveDoc();
        if (!Doc) return;
        LastPointerClient = [E.clientX, E.clientY];
        const Rect = CanvasEl.getBoundingClientRect();
        const Sx = E.clientX - Rect.left, Sy = E.clientY - Rect.top;
        HoverUvPos = Canvas2D.ScreenToUv(Sx, Sy);

        // If modal G/R/S is active, LMB confirms and RMB cancels!
        if (ModalTransform)
        {
            if (E.button === 0) ConfirmModalTransform();
            else CancelModalTransform();
            return;
        }

        // Shift+RMB places 2D Cursor directly (Blender convention!)
        if (E.button === 2 && E.shiftKey)
        {
            StudioState.cursorUV = [HoverUvPos[0], HoverUvPos[1]];
            SyncAllViews();
            return;
        }

        // Ctrl+Alt+LMB in Edge Mode -> Edge Ring immediately
        if (E.button === 0 && E.ctrlKey && E.altKey && StudioState.selectionMode === 'edge')
        {
            const Hit = Canvas2D.HitTest(Sx, Sy, {
                surface: Doc.surface, islands: Doc.islands, mode: 'edge', activeTool: StudioState.activeTool, udimTiles: Doc.udimTiles
            });
            if (Hit?.kind === 'edge')
            {
                const Cnt = Doc.surface.SelectUvEdgeRing(Hit.uvA, Hit.uvB, Doc.islands, E.shiftKey);
                Toast(`UV Edge Ring: ${Cnt} edges`);
                SyncAllViews(true);
                return;
            }
        }

        // Panning: Middle Mouse, Right Mouse drag (short RMB click in edge mode opens Edge Menu!), or Alt+Left Mouse drag (short Alt+Click picks Edge/Face/Vertex Loop!)
        if (E.button === 1 || E.button === 2 || (E.button === 0 && E.altKey))
        {
            DragAction = {
                kind: 'pan',
                lastX: Sx,
                startX: Sx,
                lastY: Sy,
                startY: Sy,
                button: E.button,
                altClickEdge: E.button === 0 && E.altKey && (StudioState.selectionMode === 'edge' || StudioState.activeTool === 'seam'),
                altClickFace: E.button === 0 && E.altKey && StudioState.selectionMode === 'face' && StudioState.activeTool !== 'seam',
                altClickVertex: E.button === 0 && E.altKey && StudioState.selectionMode === 'vertex' && StudioState.activeTool !== 'seam',
                shiftKey: E.shiftKey,
                clientX: E.clientX,
                clientY: E.clientY
            };
            CanvasEl.setPointerCapture(E.pointerId);
            return;
        }

        if (E.button !== 0) return;

        // Seam Brush tool in 2D: click any UV edge to toggle its 3D seam
        if (StudioState.activeTool === 'seam')
        {
            const Hit = Canvas2D.HitTest(Sx, Sy, {
                surface: Doc.surface, islands: Doc.islands, mode: 'edge', activeTool: 'seam', udimTiles: Doc.udimTiles
            });
            if (Hit?.kind === 'edge')
            {
                RunWithUndo('Toggle Seam Edge', () =>
                {
                    const Marked = Doc.surface.ToggleSeamEdge3D(Hit.vA, Hit.vB, null, Doc.islands);
                    if (!Marked) Doc.surface._hasClearedSeams = true;
                    LogEvent('SEAM', `${Marked ? 'Marked' : 'Cleared'} seam on edge ${Hit.key3d}`, 'warm');
                    Toast(Marked ? 'Marked Seam Edge' : 'Cleared Seam Edge');
                });
            }
            return;
        }

        if (StudioState.activeTool === 'cursor')
        {
            StudioState.cursorUV = [HoverUvPos[0], HoverUvPos[1]];
            SyncAllViews();
            return;
        }

        if (StudioState.activeTool === 'circle' || CircleSelect.active)
        {
            ApplyCircleSelectAt(Doc, Sx, Sy, !E.shiftKey && E.ctrlKey);
            DragAction = { kind: 'circle', subtract: E.ctrlKey };
            CanvasEl.setPointerCapture(E.pointerId);
            return;
        }

        if (StudioState.activeTool === 'box')
        {
            MarqueeBox = { x0: Sx, y0: Sy, x1: Sx, y1: Sy, additive: E.shiftKey };
            DragAction = { kind: 'box' };
            CanvasEl.setPointerCapture(E.pointerId);
            return;
        }

        if (StudioState.activeTool === 'move' || StudioState.activeTool === 'rotate' || StudioState.activeTool === 'scale')
        {
            const Hit = Canvas2D.HitTest(Sx, Sy, {
                surface: Doc.surface, islands: Doc.islands, mode: StudioState.selectionMode, activeTool: StudioState.activeTool, udimTiles: Doc.udimTiles
            });
            if (Hit && Hit.kind !== 'udim' && !Doc.surface.selectedUVs.size) ApplyHitSelection(Doc, Hit, E.shiftKey, E);
            BeginModalTransform(StudioState.activeTool, Sx, Sy);
            return;
        }

        // Standard Select / Drag Marquee
        const Hit = Canvas2D.HitTest(Sx, Sy, {
            surface: Doc.surface, islands: Doc.islands, mode: StudioState.selectionMode, activeTool: StudioState.activeTool, udimTiles: Doc.udimTiles
        });
        if (Hit?.kind === 'udim')
        {
            Doc.activeUdim = Hit.tileId;
            SyncAllViews(true);
            return;
        }
        if (Hit)
        {
            ApplyHitSelection(Doc, Hit, E.shiftKey, E);
            DragAction = { kind: 'tweak-wait', startX: Sx, startY: Sy };
            CanvasEl.setPointerCapture(E.pointerId);
            SyncAllViews(true);
        }
        else
        {
            MarqueeBox = { x0: Sx, y0: Sy, x1: Sx, y1: Sy, additive: E.shiftKey };
            DragAction = { kind: 'box' };
            CanvasEl.setPointerCapture(E.pointerId);
        }
    });

    CanvasEl.addEventListener('pointermove', E =>
    {
        const Doc = ActiveDoc();
        LastPointerClient = [E.clientX, E.clientY];
        const Rect = CanvasEl.getBoundingClientRect();
        const Sx = E.clientX - Rect.left, Sy = E.clientY - Rect.top;
        HoverUvPos = Canvas2D.ScreenToUv(Sx, Sy);

        if (ModalTransform)
        {
            UpdateModalTransform(Sx, Sy);
            return;
        }
        if (CircleSelect.active)
        {
            CircleSelect.x = Sx; CircleSelect.y = Sy;
            if (DragAction?.kind === 'circle') ApplyCircleSelectAt(Doc, Sx, Sy, DragAction.subtract, { commit: false });
            else ScheduleFast2DRender(Doc);
            return;
        }
        if (!DragAction)
        {
            const WantHover = Doc && (StudioState.activeTool === 'seam' || StudioState.selectionMode === 'edge' || StudioState.selectionMode === 'vertex');
            const NextHover = WantHover
                ? Canvas2D.HitTest(Sx, Sy, {
                    surface: Doc.surface,
                    islands: Doc.islands,
                    mode: StudioState.selectionMode,
                    activeTool: StudioState.activeTool,
                    udimTiles: Doc.udimTiles
                })
                : null;
            const Filtered = NextHover && (NextHover.kind === 'edge' || NextHover.kind === 'vertex') ? NextHover : null;
            const PrevKey = HoverHit2D ? `${HoverHit2D.kind}:${HoverHit2D.edgeKey ?? HoverHit2D.uvIndex}` : '';
            const NextKey = Filtered ? `${Filtered.kind}:${Filtered.edgeKey ?? Filtered.uvIndex}` : '';
            CanvasEl.style.cursor = StudioState.activeTool === 'seam' ? 'crosshair' : (Filtered ? 'pointer' : 'default');
            if (PrevKey !== NextKey)
            {
                HoverHit2D = Filtered;
                SyncAllViews(false);
            }
            return;
        }

        if (DragAction.kind === 'pan')
        {
            const Dx = Sx - DragAction.lastX, Dy = Sy - DragAction.lastY;
            DragAction.lastX = Sx; DragAction.lastY = Sy;
            Canvas2D.panU -= Dx / Canvas2D.scalePx;
            Canvas2D.panV += Dy / Canvas2D.scalePx;
            SyncAllViews();
        }
        else if (DragAction.kind === 'box' && MarqueeBox)
        {
            MarqueeBox.x1 = Sx; MarqueeBox.y1 = Sy;
            SyncAllViews();
        }
        else if (DragAction.kind === 'tweak-wait' && Math.hypot(Sx - DragAction.startX, Sy - DragAction.startY) > 6)
        {
            const StartX = DragAction.startX, StartY = DragAction.startY;
            DragAction = null;
            BeginModalTransform('move', StartX, StartY);
            if (ModalTransform) ModalTransform.startedFromDrag = true;
        }
    });

    CanvasEl.addEventListener('pointerleave', () =>
    {
        if (HoverHit2D) { HoverHit2D = null; ScheduleFast2DRender(ActiveDoc()); }
    });

    const FinishPointer = E =>
    {
        if (ModalTransform?.startedFromDrag)
        {
            ConfirmModalTransform();
            DragAction = null;
            return;
        }
        if (DragAction?.kind === 'pan')
        {
            const Dist = E ? Math.hypot((E.clientX - CanvasEl.getBoundingClientRect().left) - DragAction.startX, (E.clientY - CanvasEl.getBoundingClientRect().top) - DragAction.startY) : 99;
            if (Dist < 5 && DragAction.altClickEdge)
            {
                const Doc = ActiveDoc();
                const Hit = Canvas2D.HitTest(DragAction.startX, DragAction.startY, {
                    surface: Doc.surface, islands: Doc.islands, mode: 'edge', activeTool: StudioState.activeTool, udimTiles: Doc.udimTiles
                });
                if (Hit?.kind === 'edge')
                {
                    if (StudioState.activeTool === 'seam')
                    {
                        RunWithUndo('Toggle Seam Loop', () =>
                        {
                            const Res = Doc.surface.ToggleSeamLoop3D(Hit.vA, Hit.vB, null, Doc.islands);
                            if (!Res.marked) Doc.surface._hasClearedSeams = true;
                            LogEvent('SEAM', `${Res.marked ? 'Marked' : 'Cleared'} seam loop (${Res.count} edges)`, 'warm');
                            Toast(`${Res.marked ? 'Marked' : 'Cleared'} Seam Loop (${Res.count} edges)`);
                        });
                    }
                    else
                    {
                        const Cnt = Doc.surface.SelectUvEdgeLoop(Hit.uvA, Hit.uvB, Doc.islands, DragAction.shiftKey);
                        Toast(`UV Edge Loop: ${Cnt} edges`);
                        SyncAllViews(true);
                    }
                }
            }
            else if (Dist < 5 && DragAction.altClickFace)
            {
                const Doc = ActiveDoc();
                const Hit = Canvas2D.HitTest(DragAction.startX, DragAction.startY, {
                    surface: Doc.surface, islands: Doc.islands, mode: 'face', activeTool: StudioState.activeTool, udimTiles: Doc.udimTiles
                });
                if (Hit?.kind === 'face')
                {
                    const Cnt = Doc.surface.SelectFaceLoop3D(Hit.faceIndex, Hit.edgeVa, Hit.edgeVb, DragAction.shiftKey, Doc.islands);
                    Toast(`Face Loop: ${Cnt} faces`);
                    SyncAllViews(true);
                }
            }
            else if (Dist < 5 && DragAction.altClickVertex)
            {
                const Doc = ActiveDoc();
                const Hit = Canvas2D.HitTest(DragAction.startX, DragAction.startY, {
                    surface: Doc.surface, islands: Doc.islands, mode: 'edge', activeTool: StudioState.activeTool, udimTiles: Doc.udimTiles
                });
                if (Hit?.kind === 'edge')
                {
                    const Cnt = Doc.surface.SelectUvEdgeLoop(Hit.uvA, Hit.uvB, Doc.islands, DragAction.shiftKey);
                    Doc.surface.SyncSelectionsFromMode('edge', Doc.islands);
                    Toast(`Vertex Loop (${Cnt} edges)`);
                    SyncAllViews(true);
                }
            }
            else if (Dist < 5 && DragAction.button === 2)
            {
                OpenUvContextMenu(DragAction.clientX, DragAction.clientY, { source: '2d' });
            }
            DragAction = null;
            return;
        }
        if (DragAction?.kind === 'circle')
        {
            const Doc = ActiveDoc();
            try { CanvasEl.releasePointerCapture(E.pointerId); } catch {}
            DragAction = null;
            if (Doc)
            {
                Doc.surface.SyncSelectionsFromMode(StudioState.selectionMode, Doc.islands);
                SyncAllViews(true);
            }
            return;
        }
        if (DragAction?.kind === 'box' && MarqueeBox)
        {
            const Doc = ActiveDoc();
            const BoxW = Math.abs(MarqueeBox.x1 - MarqueeBox.x0);
            const BoxH = Math.abs(MarqueeBox.y1 - MarqueeBox.y0);
            if (BoxW < 4 && BoxH < 4 && !MarqueeBox.additive)
            {
                Doc.surface.ClearSelection();
            }
            else
            {
                const [U0, V1] = Canvas2D.ScreenToUv(Math.min(MarqueeBox.x0, MarqueeBox.x1), Math.min(MarqueeBox.y0, MarqueeBox.y1));
                const [U1, V0] = Canvas2D.ScreenToUv(Math.max(MarqueeBox.x0, MarqueeBox.x1), Math.max(MarqueeBox.y0, MarqueeBox.y1));
                if (!MarqueeBox.additive) Doc.surface.ClearSelection();
                SelectInUVBox(Doc, U0, V0, U1, V1);
            }
            MarqueeBox = null;
            DragAction = null;
            SyncAllViews(true);
            return;
        }
        DragAction = null;
    };

    CanvasEl.addEventListener('pointerup', FinishPointer);
    CanvasEl.addEventListener('pointercancel', FinishPointer);

    CanvasEl.addEventListener('wheel', E =>
    {
        E.preventDefault();
        if (ModalTransform && StudioState.proportional.enabled)
        {
            const Factor = E.deltaY < 0 ? 1.15 : 0.87;
            StudioState.proportional.radius = Math.max(0.02, Math.min(1.5, StudioState.proportional.radius * Factor));
            UpdateModalTransform(ModalTransform.curScreenX, ModalTransform.curScreenY);
            return;
        }
        if (CircleSelect.active)
        {
            CircleSelect.radiusPx = Math.max(12, Math.min(160, CircleSelect.radiusPx + (E.deltaY < 0 ? 6 : -6)));
            ScheduleFast2DRender(ActiveDoc());
            return;
        }

        const Rect = CanvasEl.getBoundingClientRect();
        const Sx = E.clientX - Rect.left, Sy = E.clientY - Rect.top;
        const [BeforeU, BeforeV] = Canvas2D.ScreenToUv(Sx, Sy);
        const Factor = E.deltaY < 0 ? 1.14 : 0.88;
        Canvas2D.scalePx = Math.max(35, Math.min(6000, Canvas2D.scalePx * Factor));
        const [AfterU, AfterV] = Canvas2D.ScreenToUv(Sx, Sy);
        Canvas2D.panU += BeforeU - AfterU;
        Canvas2D.panV += BeforeV - AfterV;
        ScheduleFast2DRender(ActiveDoc());
    }, { passive: false });
}

function ApplyHitSelection(Doc, Hit, Additive, Event = null)
{
    if (Hit.kind === 'island') Doc.surface.SelectIsland(Hit.islandId, Additive, Doc.islands);
    else if (Hit.kind === 'face')
    {
        if (Event?.ctrlKey && !Event?.altKey && Doc.surface.activeFace != null)
        {
            const Cnt = Doc.surface.SelectShortestFacePath3D(Doc.surface.activeFace, Hit.faceIndex, Additive, Doc.islands);
            Toast(`Shortest Face Path: ${Cnt} faces`);
            return;
        }
        Doc.surface.SelectFace(Hit.faceIndex, Additive, Doc.islands);
    }
    else if (Hit.kind === 'edge')
    {
        if (Event?.ctrlKey && !Event?.altKey && Doc.surface.activeEdgeUV)
        {
            const Cnt = Doc.surface.SelectUvShortestPath(Doc.surface.activeEdgeUV, Hit.edgeKey, Doc.islands, Additive);
            Toast(`Shortest UV Path: ${Cnt} edges`);
            return;
        }
        if (!Additive) Doc.surface.ClearSelection();
        if (Additive && Doc.surface.selectedUVEdges.has(Hit.edgeKey)) Doc.surface.selectedUVEdges.delete(Hit.edgeKey);
        else
        {
            Doc.surface.selectedUVEdges.add(Hit.edgeKey);
            Doc.surface.activeEdgeUV = Hit.edgeKey;
            if (Hit.key3d) Doc.surface.activeEdge3D = Hit.key3d;
        }
        Doc.surface.SyncSelectionsFromMode('edge', Doc.islands);
    }
    else if (Hit.kind === 'vertex')
    {
        let V3 = null;
        for (const F of Doc.surface.faces)
        {
            for (const C of F.corners) { if (C.uv === Hit.uvIndex) { V3 = C.v; break; } }
            if (V3 !== null) break;
        }
        if (Event?.ctrlKey && !Event?.altKey && Doc.surface.activeVertex3D != null && V3 !== null)
        {
            const Cnt = Doc.surface.SelectShortestVertexPath3D(Doc.surface.activeVertex3D, V3, Additive, Doc.islands);
            Toast(`Shortest Vertex Path: ${Cnt} verts`);
            return;
        }
        if (!Additive) Doc.surface.ClearSelection();
        if (Additive && Doc.surface.selectedUVs.has(Hit.uvIndex)) Doc.surface.selectedUVs.delete(Hit.uvIndex);
        else
        {
            Doc.surface.selectedUVs.add(Hit.uvIndex);
            if (V3 !== null) Doc.surface.activeVertex3D = V3;
        }
        Doc.surface.SyncSelectionsFromMode('vertex', Doc.islands);
    }
}

function SelectInUVBox(Doc, MinU, MinV, MaxU, MaxV)
{
    const InBox = (U, V) => U >= MinU && U <= MaxU && V >= MinV && V <= MaxV;
    const HasSpatial = typeof Doc.surface.QueryUvSpatialFaces === 'function';
    const CandFaces = HasSpatial ? Doc.surface.QueryUvSpatialFaces(MinU, MinV, MaxU, MaxV) : null;

    if (StudioState.selectionMode === 'island')
    {
        for (const Isl of Doc.islands)
        {
            if (Isl.hidden) continue;
            if (Isl.bounds.maxU >= MinU && Isl.bounds.minU <= MaxU && Isl.bounds.maxV >= MinV && Isl.bounds.minV <= MaxV)
            {
                const HasVertInBox = Isl.uvIndices.some(Idx => InBox(Doc.surface.uvCoords[Idx][0], Doc.surface.uvCoords[Idx][1]));
                if (HasVertInBox) Doc.surface.SelectIsland(Isl.id, true, Doc.islands);
            }
        }
    }
    else if (StudioState.selectionMode === 'face')
    {
        const LoopFaces = CandFaces || Doc.surface.faces.map((_, i) => i);
        for (let i = 0; i < LoopFaces.length; i++)
        {
            const fi = LoopFaces[i];
            const C = Doc.surface.faces[fi].corners;
            let Cx = 0, Cy = 0;
            for (let k = 0; k < C.length; k++)
            {
                const P = Doc.surface.uvCoords[C[k].uv];
                Cx += P[0]; Cy += P[1];
            }
            if (InBox(Cx / C.length, Cy / C.length)) Doc.surface.SelectFace(fi, true, Doc.islands);
        }
    }
    else if (CandFaces)
    {
        for (let i = 0; i < CandFaces.length; i++)
        {
            const Cr = Doc.surface.faces[CandFaces[i]].corners;
            for (let k = 0; k < Cr.length; k++)
            {
                const UvIdx = Cr[k].uv;
                const P = Doc.surface.uvCoords[UvIdx];
                if (InBox(P[0], P[1])) Doc.surface.selectedUVs.add(UvIdx);
            }
            if (StudioState.selectionMode === 'edge')
            {
                for (let k = 0; k < Cr.length; k++)
                {
                    const Ua = Cr[k].uv, Ub = Cr[(k + 1) % Cr.length].uv;
                    if (Doc.surface.selectedUVs.has(Ua) && Doc.surface.selectedUVs.has(Ub))
                    {
                        Doc.surface.selectedUVEdges.add(EdgeKey(Ua, Ub));
                    }
                }
            }
        }
        Doc.surface.SyncSelectionsFromMode(StudioState.selectionMode, Doc.islands);
    }
    else
    {
        for (let i = 0; i < Doc.surface.uvCoords.length; i++)
        {
            const P = Doc.surface.uvCoords[i];
            if (InBox(P[0], P[1])) Doc.surface.selectedUVs.add(i);
        }
        if (StudioState.selectionMode === 'edge')
        {
            for (const F of Doc.surface.faces)
            {
                for (let k = 0; k < F.corners.length; k++)
                {
                    const Ua = F.corners[k].uv, Ub = F.corners[(k + 1) % F.corners.length].uv;
                    if (Doc.surface.selectedUVs.has(Ua) && Doc.surface.selectedUVs.has(Ub))
                    {
                        Doc.surface.selectedUVEdges.add(EdgeKey(Ua, Ub));
                    }
                }
            }
        }
    }
}

function ApplyCircleSelectAt(Doc, Sx, Sy, Subtract = false, { commit = false } = {})
{
    const [Cu, Cv] = Canvas2D.ScreenToUv(Sx, Sy);
    const Ruv = CircleSelect.radiusPx / Canvas2D.scalePx;
    const Mode = StudioState.selectionMode;
    const HasSpatial = typeof Doc.surface.QueryUvSpatialFaces === 'function';
    const CandFaces = HasSpatial ? Doc.surface.QueryUvSpatialFaces(Cu - Ruv, Cv - Ruv, Cu + Ruv, Cv + Ruv) : null;

    if (Mode === 'face')
    {
        const Loop = CandFaces || Doc.surface.faces.map((_, i) => i);
        for (let i = 0; i < Loop.length; i++)
        {
            const fi = Loop[i];
            const C = Doc.surface.faces[fi].corners;
            let Cx = 0, Cy = 0;
            let Hit = false;
            for (const cr of C)
            {
                const P = Doc.surface.uvCoords[cr.uv];
                Cx += P[0]; Cy += P[1];
                if (Math.hypot(P[0] - Cu, P[1] - Cv) <= Ruv) Hit = true;
            }
            if (Hit || Math.hypot(Cx / C.length - Cu, Cy / C.length - Cv) <= Ruv)
            {
                if (Subtract) Doc.surface.selectedFaces.delete(fi);
                else Doc.surface.selectedFaces.add(fi);
            }
        }
        if (commit) Doc.surface.SyncSelectionsFromMode('face', Doc.islands);
    }
    else if (Mode === 'edge')
    {
        const Loop = CandFaces || Doc.surface.faces.map((_, i) => i);
        for (let i = 0; i < Loop.length; i++)
        {
            const F = Doc.surface.faces[Loop[i]];
            for (let k = 0; k < F.corners.length; k++)
            {
                const Ua = F.corners[k].uv, Ub = F.corners[(k + 1) % F.corners.length].uv;
                const Pa = Doc.surface.uvCoords[Ua], Pb = Doc.surface.uvCoords[Ub];
                const Mx = 0.5 * (Pa[0] + Pb[0]), My = 0.5 * (Pa[1] + Pb[1]);
                if (Math.hypot(Mx - Cu, My - Cv) <= Ruv)
                {
                    const Ek = EdgeKey(Ua, Ub);
                    if (Subtract) Doc.surface.selectedUVEdges.delete(Ek);
                    else Doc.surface.selectedUVEdges.add(Ek);
                }
            }
        }
        if (commit) Doc.surface.SyncSelectionsFromMode('edge', Doc.islands);
    }
    else if (Mode === 'vertex' && CandFaces)
    {
        for (let i = 0; i < CandFaces.length; i++)
        {
            const Cr = Doc.surface.faces[CandFaces[i]].corners;
            for (let k = 0; k < Cr.length; k++)
            {
                const UvIdx = Cr[k].uv;
                const P = Doc.surface.uvCoords[UvIdx];
                if (Math.hypot(P[0] - Cu, P[1] - Cv) <= Ruv)
                {
                    if (Subtract) Doc.surface.selectedUVs.delete(UvIdx);
                    else Doc.surface.selectedUVs.add(UvIdx);
                }
            }
        }
        if (commit) Doc.surface.SyncSelectionsFromMode('vertex', Doc.islands);
    }
    else
    {
        for (const Isl of Doc.islands)
        {
            if (Isl.hidden) continue;
            if (Isl.bounds.maxU < Cu - Ruv || Isl.bounds.minU > Cu + Ruv ||
                Isl.bounds.maxV < Cv - Ruv || Isl.bounds.minV > Cv + Ruv) continue;
            for (const UvIdx of Isl.uvIndices)
            {
                const P = Doc.surface.uvCoords[UvIdx];
                if (Math.hypot(P[0] - Cu, P[1] - Cv) <= Ruv)
                {
                    if (Subtract)
                    {
                        Doc.surface.selectedUVs.delete(UvIdx);
                        if (Mode === 'island')
                        {
                            Doc.surface.selectedIslands.delete(Isl.id);
                            for (const Fi of Isl.faceIndices) Doc.surface.selectedFaces.delete(Fi);
                        }
                    }
                    else if (Mode === 'island') Doc.surface.SelectIsland(Isl.id, true, Doc.islands);
                    else Doc.surface.selectedUVs.add(UvIdx);
                }
            }
        }
        if (commit && Mode !== 'island') Doc.surface.SyncSelectionsFromMode('vertex', Doc.islands);
    }
    if (commit) SyncAllViews(true);
    else FastPaintVisualSync(Doc);
}

function SetupKeyboardShortcuts()
{
    window.addEventListener('pointermove', E => { LastPointerClient = [E.clientX, E.clientY]; }, { passive: true });
    window.addEventListener('keydown', E =>
    {
        const Tag = document.activeElement?.tagName;
        if (Tag === 'INPUT' || Tag === 'SELECT' || Tag === 'TEXTAREA')
        {
            if (E.key === 'Escape') document.activeElement.blur();
            return;
        }
        const EdgeMenu = document.getElementById('edge-seam-menu');
        const SelMenu = document.getElementById('select-menu');
        const SimMenu = document.getElementById('select-similar-menu');
        const CtxMenu = document.getElementById('uv-context-menu');
        if (E.key === 'Escape' && ((EdgeMenu && !EdgeMenu.hidden) || (SelMenu && !SelMenu.hidden) || (SimMenu && !SimMenu.hidden) || (CtxMenu && !CtxMenu.hidden)))
        {
            E.preventDefault();
            if (EdgeMenu) EdgeMenu.hidden = true;
            CloseAllSelectMenus();
            return;
        }
        const Doc = ActiveDoc();
        if (!Doc) return;

        // Pending Mirror axis confirmation (Ctrl+M -> X or Y)
        if (PendingMirrorAxis)
        {
            PendingMirrorAxis = false;
            const K = E.key.toLowerCase();
            if (K === 'x' || K === 'u' || K === 'y' || K === 'v')
            {
                E.preventDefault();
                RunWithUndo('Mirror UVs', () =>
                {
                    const Piv = ComputeSelectionPivot(Doc) || StudioState.cursorUV;
                    Doc.surface.TransformSelectedUVs({
                        pivot: Piv,
                        scaleU: (K === 'x' || K === 'u') ? -1 : 1,
                        scaleV: (K === 'y' || K === 'v') ? -1 : 1
                    });
                    Toast(`Mirrored along ${K.toUpperCase()}`);
                });
                return;
            }
        }

        // Modal G/R/S controls
        if (ModalTransform)
        {
            const K = E.key.toLowerCase();
            if (E.key === 'Escape') { E.preventDefault(); CancelModalTransform(); return; }
            if (E.key === 'Enter' || E.key === ' ') { E.preventDefault(); ConfirmModalTransform(); return; }
            if (K === 'x' || K === 'u') { E.preventDefault(); ModalTransform.axis = ModalTransform.axis === 'x' ? null : 'x'; UpdateModalTransform(ModalTransform.curScreenX, ModalTransform.curScreenY); return; }
            if (K === 'y' || K === 'v') { E.preventDefault(); ModalTransform.axis = ModalTransform.axis === 'y' ? null : 'y'; UpdateModalTransform(ModalTransform.curScreenX, ModalTransform.curScreenY); return; }
            if (/^[0-9.\-]$/.test(E.key))
            {
                E.preventDefault();
                ModalTransform.numericBuffer += E.key;
                UpdateModalTransform(ModalTransform.curScreenX, ModalTransform.curScreenY);
                return;
            }
            if (E.key === 'Backspace')
            {
                E.preventDefault();
                ModalTransform.numericBuffer = ModalTransform.numericBuffer.slice(0, -1);
                UpdateModalTransform(ModalTransform.curScreenX, ModalTransform.curScreenY);
                return;
            }
        }

        if (CircleSelect.active && (E.key === 'Escape' || E.key.toLowerCase() === 'c'))
        {
            E.preventDefault();
            CircleSelect.active = false;
            StudioState.activeTool = 'select';
            UpdateToolRailActive();
            SyncAllViews();
            return;
        }

        const Mod = E.ctrlKey || E.metaKey;
        const K = E.key.toLowerCase();

        // Undo / Redo
        if (Mod && K === 'z' && !E.shiftKey) { E.preventDefault(); if (Doc.surface.Undo()) SyncAllViews(true); return; }
        if ((Mod && E.shiftKey && K === 'z') || (Mod && K === 'y')) { E.preventDefault(); if (Doc.surface.Redo()) SyncAllViews(true); return; }
        if (Mod && E.shiftKey && K === 'f') { E.preventDefault(); RunSelectOp('linked-flat'); return; }
        if (Mod && K === 'f') { E.preventDefault(); document.getElementById('outliner-search').focus(); return; }

        // Selection Modes 1-4
        if (!Mod && ['1', '2', '3', '4'].includes(E.key))
        {
            const Modes = { '1': 'vertex', '2': 'edge', '3': 'face', '4': 'island' };
            SetSelectionMode(Modes[E.key]);
            return;
        }

        // Blender Selection Shortcuts
        if (!Mod && K === 'a')
        {
            E.preventDefault();
            if (E.altKey) Doc.surface.ClearSelection();
            else Doc.surface.SelectAll(Doc.islands);
            SyncAllViews(true);
            return;
        }
        if (Mod && K === 'i') { E.preventDefault(); Doc.surface.InvertSelection(StudioState.selectionMode, Doc.islands); SyncAllViews(true); return; }
        if (E.shiftKey && !Mod && K === 'g')
        {
            E.preventDefault();
            OpenSelectSimilarMenu(LastPointerClient[0], LastPointerClient[1]);
            return;
        }
        if (K === 'l')
        {
            E.preventDefault();
            const Hit = Canvas2D.HitTest(Canvas2D.UvToScreen(HoverUvPos[0], HoverUvPos[1])[0], Canvas2D.UvToScreen(HoverUvPos[0], HoverUvPos[1])[1], {
                surface: Doc.surface, islands: Doc.islands, mode: 'island', udimTiles: Doc.udimTiles
            });
            if (Hit?.islandId !== undefined) Doc.surface.SelectIsland(Hit.islandId, true, Doc.islands);
            else if (Doc.surface.selectedFaces.size || Doc.surface.selectedUVs.size || Doc.surface.selectedEdges3D.size) Doc.surface.SelectSameUvIsland(E.shiftKey, Doc.islands);
            else Doc.surface.SelectLinked(Doc.islands);
            SyncAllViews(true);
            return;
        }
        if (!Mod && K === 'b') { E.preventDefault(); SetActiveTool('box'); Toast('Box Select (2D & 3D): Drag rectangle · Shift Add · Ctrl Subtract'); return; }
        if (!Mod && K === 'c') { E.preventDefault(); CircleSelect.active = true; SetActiveTool('circle'); Toast('Paint Select (2D & 3D): LMB brush · Ctrl Erase · [ / ] Radius · Esc exit'); return; }
        if (!Mod && (E.key === '[' || E.key === ']'))
        {
            E.preventDefault();
            CircleSelect.radiusPx = Math.max(12, Math.min(160, CircleSelect.radiusPx + (E.key === ']' ? 6 : -6)));
            if (Viewport3D) Viewport3D.paintRadiusPx = CircleSelect.radiusPx;
            Toast(`Paint Select Radius: ${Math.round(CircleSelect.radiusPx)}px`);
            SyncAllViews();
            return;
        }

        // Blender Modal Transform
        if (!Mod && K === 'g') { E.preventDefault(); BeginModalTransform('move'); return; }
        if (!Mod && K === 'r') { E.preventDefault(); BeginModalTransform('rotate'); return; }
        if (!Mod && K === 's' && !E.shiftKey) { E.preventDefault(); BeginModalTransform('scale'); return; }
        if (Mod && K === 'm') { E.preventDefault(); PendingMirrorAxis = true; Toast('Mirror: press X/U or Y/V'); return; }
        if (!Mod && K === 'o') { E.preventDefault(); StudioState.proportional.enabled = !StudioState.proportional.enabled; document.getElementById('prop-edit-btn').classList.toggle('active', StudioState.proportional.enabled); SyncAllViews(); return; }
        if (E.key === '.')
        {
            const List = ['median', 'bounds', 'cursor', 'individual'];
            StudioState.pivotMode = List[(List.indexOf(StudioState.pivotMode) + 1) % List.length];
            document.getElementById('pivot-select').value = StudioState.pivotMode;
            SyncAllViews();
            return;
        }
        if (E.shiftKey && E.key === 'Tab')
        {
            E.preventDefault();
            const List = ['off', 'grid', 'pixel', 'vertex'];
            StudioState.snapMode = List[(List.indexOf(StudioState.snapMode) + 1) % List.length];
            document.getElementById('snap-select').value = StudioState.snapMode;
            Toast(`Snap: ${StudioState.snapMode}`);
            return;
        }

        if (!Mod && K === 'w') { E.preventDefault(); SetActiveTool('select'); return; }
        if (!Mod && K === 'k')
        {
            E.preventDefault();
            const NextTool = StudioState.activeTool === 'seam' ? 'select' : 'seam';
            SetActiveTool(NextTool);
            Toast(NextTool === 'seam' ? 'Seam Brush Active: Click edge to toggle seam (Alt+Click loop)' : 'Select Tool Active');
            return;
        }
        if (Mod && (E.key === '=' || E.key === '+')) { E.preventDefault(); RunEdgeOrSeamOp('grow-sel'); return; }
        if (Mod && E.key === '-') { E.preventDefault(); RunEdgeOrSeamOp('shrink-sel'); return; }

        // Blender UV Operators
        if (!Mod && K === 'u') { E.preventDefault(); RunUnwrap('lscm'); return; }
        if (E.shiftKey && K === 'p') { E.preventDefault(); ExecuteImportancePack(); return; }
        if (!Mod && K === 'p')
        {
            E.preventDefault();
            RunWithUndo(E.altKey ? 'Unpin' : 'Pin', () => { Doc.surface.PinSelected(!E.altKey); });
            return;
        }
        if (Mod && K === 'e') { E.preventDefault(); OpenEdgeSeamMenu(LastPointerClient[0], LastPointerClient[1]); return; }
        if (Mod && K === 'v') { E.preventDefault(); RunWithUndo('Minimize Stretch', () => { MinimizeStretch(Doc.surface, { iterations: StudioState.relaxIterations, onlySelected: Doc.surface.selectedUVs.size > 0, islands: Doc.islands }); }); return; }
        if (Mod && K === 'a') { E.preventDefault(); RunWithUndo('Average Scale', () => { AverageIslandScale(Doc.surface, Doc.islands, Doc.surface.selectedIslands.size > 0); }); return; }
        if (!Mod && K === 'v') { E.preventDefault(); RunWithUndo('Stitch', () => { const N = Doc.surface.StitchSelected(Doc.islands); Toast(`Stitched ${N} UVs`); }); return; }
        if (!Mod && K === 'm') { E.preventDefault(); RunWithUndo('Weld', () => { const N = Doc.surface.WeldSelected(); Toast(`Merged ${N} UVs`); }); return; }
        if (!Mod && K === 'y') { E.preventDefault(); RunWithUndo('Split', () => { Doc.surface.SplitSelection(); }); return; }
        if (E.key === 'Home' || (!Mod && K === 'f'))
        {
            E.preventDefault();
            if (Doc.surface.selectedUVs.size)
            {
                let MinU = Infinity, MinV = Infinity, MaxU = -Infinity, MaxV = -Infinity;
                for (const Idx of Doc.surface.selectedUVs)
                {
                    const P = Doc.surface.uvCoords[Idx];
                    if (P[0] < MinU) MinU = P[0]; if (P[0] > MaxU) MaxU = P[0];
                    if (P[1] < MinV) MinV = P[1]; if (P[1] > MaxV) MaxV = P[1];
                }
                Canvas2D.FrameBounds(MinU, MinV, MaxU, MaxV, 0.25);
            }
            else Canvas2D.FrameUdimTiles(Doc.udimTiles);
            SyncAllViews();
            return;
        }
        if (!Mod && K === 'z')
        {
            E.preventDefault();
            const Order = ['checker', 'flat', 'matcap', 'matcap-satin'];
            const Next = Order[(Order.indexOf(Viewport3D.shadingMode) + 1) % Order.length];
            Set3DShadingMode(Next);
            const Names = { checker: 'UV Checker', flat: 'Flat Shaded', matcap: 'Matcap Clay', 'matcap-satin': 'Matcap Satin' };
            Toast(`3D Shading: ${Names[Next] || Next}`);
            return;
        }
        if (E.key === '?') { E.preventDefault(); document.getElementById('help-dialog').showModal(); }
    });
}

function Set3DShadingMode(Mode)
{
    if (Viewport3D) Viewport3D.shadingMode = Mode;
    const Sel = document.getElementById('mesh-shade-select');
    if (Sel) Sel.value = Mode;
    document.querySelectorAll('.mesh-camera-bar button[data-shade3d]').forEach(B =>
    {
        B.classList.toggle('active', B.dataset.shade3d === Mode);
    });
    SyncAllViews();
}

function SetSelectionMode(Mode)
{
    StudioState.selectionMode = Mode;
    document.querySelectorAll('#sel-mode-pills button').forEach(B => B.classList.toggle('active', B.dataset.mode === Mode));
    SyncMeshHintPill();
    SyncAllViews();
}

function SetActiveTool(Tool)
{
    StudioState.activeTool = Tool;
    CircleSelect.active = Tool === 'circle';
    UpdateToolRailActive();
    SyncMeshHintPill();
    SyncAllViews();
}

function SyncMeshHintPill()
{
    const El = document.getElementById('mesh-hint-pill');
    if (!El) return;
    if (StudioState.activeTool === 'box')
    {
        El.textContent = 'Box Select (2D & 3D): LMB Drag Box · Shift Add · Ctrl Subtract · MMB/Alt Orbit · RMB Menu';
    }
    else if (StudioState.activeTool === 'circle')
    {
        El.textContent = 'Paint Select (2D & 3D): LMB Brush · Ctrl Subtract · [ / ] or Shift+Wheel Size · MMB/Alt Orbit · RMB Menu';
    }
    else if (StudioState.activeTool === 'seam')
    {
        El.textContent = 'Seam Brush: Click Edge to Toggle · Alt+Click Loop · RMB Context Menu';
    }
    else
    {
        const ModeTitle = { vertex: 'Vert', edge: 'Edge', face: 'Face', island: 'Island' }[StudioState.selectionMode] || 'Face';
        El.textContent = `${ModeTitle} Mode: Click Select · Shift Multi · Alt Loop · Ctrl Path · RMB Tools & Ops`;
    }
}

function UpdateToolRailActive()
{
    document.querySelectorAll('#uv-tool-rail button').forEach(B => B.classList.toggle('active', B.dataset.tool === StudioState.activeTool));
}

/*==========================================================================================================================================
                                                     STATIC WIREUP & BOOTSTRAP
==========================================================================================================================================*/
function BuildQuickUnwrapDrawer()
{
    const List = document.getElementById('quick-unwrap-list');
    const Presets = [
        { title: 'LSCM Conformal Unwrap', sub: 'Angle-preserving unwrap honoring seams & pins', kbd: 'U', glyph: 'Unwrap', act: () => RunUnwrap('lscm') },
        { title: 'Smart UV Project', sub: 'Auto-cut hard edges by dihedral angle', kbd: 'AUTO', glyph: 'Sparkles', act: () => RunUnwrap('smart') },
        { title: 'Cube Tri-Planar Projection', sub: 'Clean orthogonal hard-surface boxes', kbd: 'BOX', glyph: 'Grid', act: () => RunUnwrap('cube') },
        { title: 'Cylindrical Unwrap', sub: 'Wheels, pipes, columns & barrels', kbd: 'CYL', glyph: 'Rotate', act: () => RunUnwrap('cylinder') },
        { title: 'Follow Active Quads', sub: 'Straighten quad strip loops cleanly', kbd: 'QUAD', glyph: 'Align', act: () => RunUnwrap('follow-quads') }
    ];
    for (const P of Presets)
    {
        const Card = Construct('button', 'preset-card', [
            Construct('div', 'preset-swatch', ConstructGlyph(P.glyph)),
            Construct('div', 'preset-copy', [Construct('strong', null, P.title), Construct('small', null, P.sub)]),
            Construct('span', 'preset-arrow', P.kbd)
        ]);
        Card.type = 'button';
        Card.addEventListener('click', P.act);
        List.append(Card);
    }
}

function OpenExportModal()
{
    const Doc = ActiveDoc();
    if (!Doc) return;
    const ObjText = Doc.surface.ExportObj();
    document.getElementById('export-preview').textContent = ObjText.split('\n').slice(0, 32).join('\n') + '\n...';
    document.getElementById('export-dialog').showModal();
}

function DownloadBlob(Filename, Text, Mime = 'text/plain')
{
    const BlobObj = new Blob([Text], { type: Mime });
    const Url = URL.createObjectURL(BlobObj);
    const A = document.createElement('a');
    A.href = Url;
    A.download = Filename;
    A.click();
    URL.revokeObjectURL(Url);
}

function BuildSvgAtlasExport(Doc)
{
    const Tiles = Doc.udimTiles;
    let MinU = 0, MinV = 0, MaxU = 1, MaxV = 1;
    for (const T of Tiles)
    {
        const O = UdimOrigin(T.id);
        MaxU = Math.max(MaxU, O.u + 1);
        MaxV = Math.max(MaxV, O.v + 1);
    }
    const W = (MaxU - MinU) * 1024, H = (MaxV - MinV) * 1024;
    const GroupColors = new Map(Doc.importanceGroups.map(G => [G.id, G.color]));
    const Paths = [];
    for (const Isl of Doc.islands)
    {
        const Fill = GroupColors.get(Isl.importanceGroupId) || '#8c949e';
        for (const Fi of Isl.faceIndices)
        {
            const Pts = Doc.surface.faces[Fi].corners.map(C =>
            {
                const P = Doc.surface.uvCoords[C.uv];
                return `${((P[0] - MinU) * 1024).toFixed(2)},${(H - (P[1] - MinV) * 1024).toFixed(2)}`;
            }).join(' ');
            Paths.push(`<polygon points="${Pts}" fill="${Fill}" fill-opacity="0.22" stroke="${Fill}" stroke-width="1"/>`);
        }
    }
    return `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}" style="background:#0d0e10">\n${Paths.join('\n')}\n</svg>`;
}

function InitStudioIconsAndBindings()
{
    // Inject Crisp Frontier SVG Glyphs
    document.getElementById('undo-btn').append(ConstructGlyph('Undo'));
    document.getElementById('redo-btn').append(ConstructGlyph('Redo'));
    document.getElementById('help-btn').append(ConstructGlyph('Help'));
    document.getElementById('compact-outliner').append(ConstructGlyph('Compact'));
    document.getElementById('add-udim-quick').append(ConstructGlyph('Plus'));
    document.getElementById('stat-islands-icon').append(ConstructGlyph('Island'));
    document.getElementById('stat-udim-icon').append(ConstructGlyph('Grid'));
    document.getElementById('search-icon').append(ConstructGlyph('Search'));
    document.getElementById('maximize-stage-btn').append(ConstructGlyph('Maximize'));
    document.getElementById('toggle-log-btn').append(ConstructGlyph('Terminal'));
    document.getElementById('inspector-object-icon').append(ConstructGlyph('Pack'));

    const ToolIcons = { select: 'Select', move: 'Move', rotate: 'Rotate', scale: 'Scale', box: 'BoxSel', circle: 'CircleSel', seam: 'Scissors', cursor: 'Cursor' };
    document.querySelectorAll('#uv-tool-rail button').forEach(Btn =>
    {
        Btn.append(ConstructGlyph(ToolIcons[Btn.dataset.tool]));
        Btn.addEventListener('click', () => SetActiveTool(Btn.dataset.tool));
    });

    // Top Stage Mode Nav
    const ApplyStageMode = (Mode) =>
    {
        StudioState.stageMode = Mode;
        document.querySelectorAll('#stage-layout-nav button').forEach(B => B.classList.toggle('active', B.dataset.stageMode === Mode));
        const Split = document.getElementById('stage-split');
        Split.classList.toggle('mode-uv', Mode === 'uv');
        Split.classList.toggle('mode-mesh', Mode === 'mesh');
        requestAnimationFrame(() =>
        {
            SyncAllViews();
            if (Mode !== 'mesh' && ActiveDoc()) Canvas2D.FrameUdimTiles(ActiveDoc().udimTiles);
        });
    };
    ApplyStageMode(StudioState.stageMode);
    document.querySelectorAll('#stage-layout-nav button').forEach(Btn =>
    {
        Btn.addEventListener('click', () => ApplyStageMode(Btn.dataset.stageMode));
    });

    // Selection Mode Pills
    document.querySelectorAll('#sel-mode-pills button').forEach(Btn => Btn.addEventListener('click', () => SetSelectionMode(Btn.dataset.mode)));

    // Inspector Tabs
    document.querySelectorAll('#inspector-tabs button').forEach(Btn =>
    {
        Btn.addEventListener('click', () =>
        {
            StudioState.inspectorTab = Btn.dataset.tab;
            document.querySelectorAll('#inspector-tabs button').forEach(B => B.classList.toggle('active', B === Btn));
            RebuildInspector();
        });
    });

    // Outliner search & filters
    document.getElementById('outliner-search').addEventListener('input', E => { StudioState.searchQuery = E.target.value; RenderOutliner(ActiveDoc()); });
    document.querySelectorAll('#outliner-filters button').forEach(Btn =>
    {
        Btn.addEventListener('click', () =>
        {
            StudioState.outlinerFilter = Btn.dataset.filter;
            document.querySelectorAll('#outliner-filters button').forEach(B => B.classList.toggle('active', B === Btn));
            RenderOutliner(ActiveDoc());
        });
    });

    // Header & Viewport actions
    document.getElementById('undo-btn').addEventListener('click', () => { if (ActiveDoc().surface.Undo()) SyncAllViews(true); });
    document.getElementById('redo-btn').addEventListener('click', () => { if (ActiveDoc().surface.Redo()) SyncAllViews(true); });
    document.getElementById('compact-outliner').addEventListener('click', () =>
    {
        StudioState.compactOutliner = !StudioState.compactOutliner;
        document.getElementById('left-panel').classList.toggle('compact', StudioState.compactOutliner);
    });
    document.getElementById('maximize-stage-btn').addEventListener('click', () =>
    {
        StudioState.maximizedStage = !StudioState.maximizedStage;
        document.getElementById('workspace').classList.toggle('maximized', StudioState.maximizedStage);
        requestAnimationFrame(() => SyncAllViews());
    });
    document.getElementById('add-udim-quick').addEventListener('click', () =>
    {
        const Doc = ActiveDoc();
        const MaxId = Doc.udimTiles.reduce((M, T) => Math.max(M, T.id), 1000);
        const NextId = MaxId + 1;
        Doc.udimTiles.push({ id: NextId, label: `Tile ${NextId}`, resolution: 2048, checkerMode: 'color' });
        Doc.activeUdim = NextId;
        SyncAllViews(true);
        Toast(`Added UDIM ${NextId}`);
    });
    document.getElementById('frame-udims-btn').addEventListener('click', () => { Canvas2D.FrameUdimTiles(ActiveDoc().udimTiles); SyncAllViews(); });
    document.getElementById('btn-smart-project-quick').addEventListener('click', () => RunUnwrap('smart'));

    document.getElementById('sync-sel-btn').addEventListener('click', E => { StudioState.syncSelection = !StudioState.syncSelection; E.currentTarget.classList.toggle('active', StudioState.syncSelection); });
    document.getElementById('prop-edit-btn').addEventListener('click', E => { StudioState.proportional.enabled = !StudioState.proportional.enabled; E.currentTarget.classList.toggle('active', StudioState.proportional.enabled); SyncAllViews(); });
    document.getElementById('pivot-select').addEventListener('change', E => { StudioState.pivotMode = E.target.value; SyncAllViews(); });
    document.getElementById('snap-select').addEventListener('change', E => { StudioState.snapMode = E.target.value; });
    document.getElementById('uv-lens-select').addEventListener('change', E => { StudioState.overlayLens = E.target.value; SyncAllViews(); });
    document.getElementById('mesh-shade-select').addEventListener('change', E => Set3DShadingMode(E.target.value));
    document.querySelectorAll('.mesh-camera-bar button[data-shade3d]').forEach(B => B.addEventListener('click', () => Set3DShadingMode(B.dataset.shade3d)));
    document.getElementById('toggle-checker-btn').addEventListener('click', E => { StudioState.showChecker = !StudioState.showChecker; E.currentTarget.classList.toggle('active', StudioState.showChecker); ScheduleFast2DRender(ActiveDoc()); });
    document.getElementById('toggle-seams-btn').addEventListener('click', E => { StudioState.showSeams = !StudioState.showSeams; Viewport3D.showSeams = StudioState.showSeams; E.currentTarget.classList.toggle('active', StudioState.showSeams); Viewport3D.SyncOverlayStates(); ScheduleFast2DRender(ActiveDoc()); });
    document.getElementById('toggle-boundaries-btn').addEventListener('click', E => { StudioState.showBoundaries = !StudioState.showBoundaries; Viewport3D.showBoundaries = StudioState.showBoundaries; E.currentTarget.classList.toggle('active', StudioState.showBoundaries); Viewport3D.SyncOverlayStates(); ScheduleFast2DRender(ActiveDoc()); });
    document.getElementById('toggle-wire3d-btn').addEventListener('click', E => { Viewport3D.showWireframe = !Viewport3D.showWireframe; E.currentTarget.classList.toggle('active', Viewport3D.showWireframe); Viewport3D.SyncOverlayStates(); });

    document.querySelectorAll('.mesh-camera-bar button[data-cam]').forEach(B => B.addEventListener('click', () => Viewport3D.CameraPreset(B.dataset.cam)));

    // Quick Operator Dock
    document.getElementById('op-unwrap').addEventListener('click', () => RunUnwrap('lscm'));
    document.getElementById('op-pack-importance').addEventListener('click', ExecuteImportancePack);
    document.getElementById('op-relax').addEventListener('click', () => RunWithUndo('Minimize Stretch', () => { MinimizeStretch(ActiveDoc().surface, { iterations: StudioState.relaxIterations, onlySelected: ActiveDoc().surface.selectedUVs.size > 0, islands: ActiveDoc().islands }); }));
    document.getElementById('op-avg-scale').addEventListener('click', () => RunWithUndo('Average Scale', () => { AverageIslandScale(ActiveDoc().surface, ActiveDoc().islands, ActiveDoc().surface.selectedIslands.size > 0); }));
    document.getElementById('op-pin').addEventListener('click', () => RunWithUndo('Pin', () => { ActiveDoc().surface.PinSelected(true); }));
    document.getElementById('op-unpin').addEventListener('click', () => RunWithUndo('Unpin', () => { ActiveDoc().surface.PinSelected(false); }));
    document.getElementById('op-seam').addEventListener('click', () => RunEdgeOrSeamOp('mark-seam'));
    document.getElementById('op-clear-seam').addEventListener('click', () => RunEdgeOrSeamOp('clear-seam'));
    document.getElementById('op-edge-menu').addEventListener('click', E =>
    {
        const Menu = document.getElementById('edge-seam-menu');
        if (!Menu.hidden) { Menu.hidden = true; return; }
        const R = E.currentTarget.getBoundingClientRect();
        OpenEdgeSeamMenu(R.left, Math.max(8, R.top - 420));
    });
    document.querySelectorAll('#edge-seam-menu button[data-edge-op]').forEach(Btn =>
    {
        Btn.addEventListener('click', () =>
        {
            document.getElementById('edge-seam-menu').hidden = true;
            RunEdgeOrSeamOp(Btn.dataset.edgeOp);
        });
    });
    document.getElementById('op-stitch').addEventListener('click', () => RunWithUndo('Stitch', () => { const N = ActiveDoc().surface.StitchSelected(ActiveDoc().islands); Toast(`Stitched ${N} UV pairs`); }));
    document.getElementById('op-merge').addEventListener('click', () => RunWithUndo('Weld', () => { const N = ActiveDoc().surface.WeldSelected(); Toast(`Welded ${N} UVs`); }));
    document.getElementById('op-split').addEventListener('click', () => RunWithUndo('Split Island', () => { ActiveDoc().surface.SplitSelection(); }));

    document.getElementById('toggle-log-btn').addEventListener('click', () =>
    {
        StudioState.logOpen = !StudioState.logOpen;
        document.getElementById('log-drawer').hidden = !StudioState.logOpen;
        requestAnimationFrame(() => SyncAllViews());
    });
    document.getElementById('clear-log-btn').addEventListener('click', () => { StudioState.logs = []; RenderLogs(); });

    // Resizable Split Divider
    const Divider = document.getElementById('split-divider');
    const UvStage = document.getElementById('uv-stage');
    const MeshStage = document.getElementById('mesh-stage');
    let SplitDrag = false;
    Divider.addEventListener('pointerdown', E => { SplitDrag = true; Divider.setPointerCapture(E.pointerId); });
    Divider.addEventListener('pointermove', E =>
    {
        if (!SplitDrag) return;
        const SplitRect = document.getElementById('stage-split').getBoundingClientRect();
        const Ratio = Math.max(0.25, Math.min(0.82, (E.clientX - SplitRect.left) / SplitRect.width));
        UvStage.style.flex = `${Ratio} 1 0`;
        MeshStage.style.flex = `${1 - Ratio} 1 0`;
        SyncAllViews();
    });
    Divider.addEventListener('pointerup', () => { SplitDrag = false; });

    // Asset Name edit + Reset Asset
    document.getElementById('inspector-object-name').addEventListener('change', E =>
    {
        const Doc = ActiveDoc();
        Doc.name = Doc.surface.name = E.target.value || Doc.name;
        RenderDocumentTabs();
        SyncAllViews();
    });
    document.getElementById('reset-doc-btn').addEventListener('click', () =>
    {
        const Doc = ActiveDoc();
        const Spec = SampleAssetCatalog.find(S => S.id === Doc.factoryId) || SampleAssetCatalog[0];
        const Fresh = CreateDocumentFromFactory(Spec, Doc.id);
        const Idx = StudioState.documents.findIndex(D => D.id === Doc.id);
        StudioState.documents[Idx] = Fresh;
        Canvas2D.FrameUdimTiles(Fresh.udimTiles);
        SyncAllViews(true);
        Toast(`Reset "${Fresh.name}"`);
    });

    // Spawn Sample Menu
    const SpawnMenu = document.getElementById('spawn-menu');
    const SpawnItems = document.getElementById('spawn-menu-items');
    for (const Sample of SampleAssetCatalog)
    {
        const Btn = Construct('button', null, [
            Construct('span', null, Sample.label),
            Construct('small', null, Sample.subtitle)
        ]);
        Btn.type = 'button';
        Btn.addEventListener('click', () =>
        {
            SpawnMenu.hidden = true;
            const Existing = StudioState.documents.find(D => D.factoryId === Sample.id);
            if (Existing) StudioState.activeDocId = Existing.id;
            else
            {
                const Doc = CreateDocumentFromFactory(Sample);
                StudioState.documents.push(Doc);
                StudioState.activeDocId = Doc.id;
            }
            Canvas2D.FrameUdimTiles(ActiveDoc().udimTiles);
            SyncAllViews(true);
        });
        SpawnItems.append(Btn);
    }
    document.getElementById('spawn-tab-btn').addEventListener('click', E =>
    {
        const R = E.currentTarget.getBoundingClientRect();
        SpawnMenu.style.left = `${R.left}px`;
        SpawnMenu.style.top = `${R.bottom + 6}px`;
        SpawnMenu.hidden = !SpawnMenu.hidden;
    });
    document.getElementById('select-menu-btn')?.addEventListener('click', E =>
    {
        const Menu = document.getElementById('select-menu');
        if (!Menu) return;
        if (!Menu.hidden) { Menu.hidden = true; return; }
        const R = E.currentTarget.getBoundingClientRect();
        OpenSelectMenu(R.left, R.bottom + 6);
    });
    document.querySelectorAll('#select-menu [data-sel-op]').forEach(Btn =>
    {
        Btn.addEventListener('click', () =>
        {
            const Op = Btn.dataset.selOp;
            if (Op === 'open-similar')
            {
                const R = document.getElementById('select-menu-btn')?.getBoundingClientRect();
                CloseAllSelectMenus();
                OpenSelectSimilarMenu(R ? R.left : LastPointerClient[0], R ? R.bottom + 6 : LastPointerClient[1]);
                return;
            }
            CloseAllSelectMenus();
            RunSelectOp(Op);
        });
    });
    document.getElementById('sel-random-apply-btn')?.addEventListener('click', () =>
    {
        const Pct = Math.max(1, Math.min(99, Number(document.getElementById('sel-random-ratio-input')?.value) || 50));
        CloseAllSelectMenus();
        RunSelectOp('random', { ratio: Pct / 100 });
    });
    document.getElementById('sim-tol-input')?.addEventListener('change', E =>
    {
        const Pct = Math.max(1, Math.min(100, Number(E.target.value) || 14));
        StudioState.selectSimilarTol = Pct / 100;
    });
    document.getElementById('sim-angle-input')?.addEventListener('change', E =>
    {
        StudioState.selectSimilarAngle = Math.max(1, Math.min(90, Number(E.target.value) || 15));
    });

    document.querySelectorAll('#uv-context-menu [data-ctx-tool]').forEach(Btn =>
    {
        Btn.addEventListener('click', () =>
        {
            const Tool = Btn.dataset.ctxTool;
            CloseUvContextMenu();
            SetActiveTool(Tool);
            if (Tool === 'box') Toast('Box Select Active — drag marquee in 2D or 3D viewport');
            else if (Tool === 'circle') Toast('Paint Select Active — brush over faces in 2D or 3D viewport');
            else Toast('Click / Tweak Select Active');
        });
    });
    document.querySelectorAll('#uv-context-menu [data-ctx-face-select]').forEach(Btn =>
    {
        Btn.addEventListener('click', () =>
        {
            CloseUvContextMenu();
            SetSelectionMode('face');
            SetActiveTool('select');
            Toast('Face Select Mode Active (Click or Shift+Click multiple faces)');
        });
    });
    document.querySelectorAll('#uv-context-menu [data-ctx-mode]').forEach(Btn =>
    {
        Btn.addEventListener('click', () =>
        {
            const Mode = Btn.dataset.ctxMode;
            CloseUvContextMenu();
            SetSelectionMode(Mode);
            Toast(`Selection Mode: ${Mode.toUpperCase()}`);
        });
    });
    document.getElementById('ctx-paint-radius-slider')?.addEventListener('input', E =>
    {
        const R = Math.max(12, Math.min(160, Number(E.target.value) || 36));
        CircleSelect.radiusPx = R;
        const ValEl = document.getElementById('ctx-paint-radius-val');
        if (ValEl) ValEl.textContent = `${Math.round(R)}px`;
        if (Viewport3D) Viewport3D.paintRadiusPx = R;
        SyncAllViews();
    });
    document.querySelectorAll('#uv-context-menu [data-ctx-sel]').forEach(Btn =>
    {
        Btn.addEventListener('click', () =>
        {
            const Op = Btn.dataset.ctxSel;
            CloseUvContextMenu();
            if (Op === 'open-similar')
            {
                OpenSelectSimilarMenu(LastPointerClient[0], LastPointerClient[1]);
                return;
            }
            RunSelectOp(Op);
        });
    });
    document.querySelectorAll('#uv-context-menu [data-ctx-op]').forEach(Btn =>
    {
        Btn.addEventListener('click', () =>
        {
            const Op = Btn.dataset.ctxOp;
            CloseUvContextMenu();
            RunUvContextOperation(Op);
        });
    });
    document.querySelectorAll('#uv-context-menu [data-ctx-edge]').forEach(Btn =>
    {
        Btn.addEventListener('click', () =>
        {
            const Op = Btn.dataset.ctxEdge;
            CloseUvContextMenu();
            RunEdgeOrSeamOp(Op);
        });
    });

    document.addEventListener('pointerdown', E =>
    {
        if (!SpawnMenu.hidden && !SpawnMenu.contains(E.target) && E.target.id !== 'spawn-tab-btn') SpawnMenu.hidden = true;
        const EdgeMenu = document.getElementById('edge-seam-menu');
        if (EdgeMenu && !EdgeMenu.hidden && !EdgeMenu.contains(E.target) && E.target.id !== 'op-edge-menu') EdgeMenu.hidden = true;
        const SelMenu = document.getElementById('select-menu');
        if (SelMenu && !SelMenu.hidden && !SelMenu.contains(E.target) && E.target.id !== 'select-menu-btn') SelMenu.hidden = true;
        const SimMenu = document.getElementById('select-similar-menu');
        if (SimMenu && !SimMenu.hidden && !SimMenu.contains(E.target) && E.target.id !== 'select-menu-btn') SimMenu.hidden = true;
        const CtxMenu = document.getElementById('uv-context-menu');
        if (CtxMenu && !CtxMenu.hidden && !CtxMenu.contains(E.target)) CtxMenu.hidden = true;
    });

    // Import OBJ & Custom UDIM Texture
    document.getElementById('import-obj-btn').addEventListener('click', () => document.getElementById('obj-file-input').click());
    document.getElementById('spawn-import-obj').addEventListener('click', () => { SpawnMenu.hidden = true; document.getElementById('obj-file-input').click(); });
    document.getElementById('obj-file-input').addEventListener('change', async E =>
    {
        const File = E.target.files?.[0];
        if (!File) return;
        const Text = await File.text();
        const { surface: Surf, hadUVs } = TopologySurface.ParseObj(Text, File.name.replace(/\.obj$/i, ''));
        const Doc = {
            id: `obj-${Date.now()}`,
            factoryId: 'custom',
            name: Surf.name,
            surface: Surf,
            importanceGroups: [],
            udimTiles: [{ id: 1001, label: 'Imported Tile', resolution: 2048, checkerMode: 'color' }],
            activeUdim: 1001,
            islands: [],
            diagnostics: null,
            lastPackReport: null
        };
        if (!hadUVs) SmartUvProject(Surf, { angleLimitDeg: 55 });
        StudioState.documents.push(Doc);
        StudioState.activeDocId = Doc.id;
        Canvas2D.FrameUdimTiles(Doc.udimTiles);
        Viewport3D.CameraPreset('Persp');
        SyncAllViews(true);
        LogEvent('IMPORT', `Loaded ${File.name} (${Surf.faces.length} faces)`, 'live');
        Toast(`Imported ${File.name}`);
        E.target.value = '';
    });

    document.getElementById('tex-file-input').addEventListener('change', E =>
    {
        const File = E.target.files?.[0];
        if (!File) return;
        const Img = new Image();
        Img.onload = () =>
        {
            Canvas2D.SetTileCustomImage(ActiveDoc().activeUdim, Img);
            SyncAllViews();
            Toast(`Loaded custom backdrop on UDIM ${ActiveDoc().activeUdim}`);
        };
        Img.src = URL.createObjectURL(File);
        E.target.value = '';
    });

    // Dialogs & Exports
    document.getElementById('help-btn').addEventListener('click', () => document.getElementById('help-dialog').showModal());
    document.getElementById('help-close-btn').addEventListener('click', () => document.getElementById('help-dialog').close());
    document.getElementById('help-ok-btn').addEventListener('click', () => document.getElementById('help-dialog').close());
    document.getElementById('export-obj-btn').addEventListener('click', OpenExportModal);
    document.getElementById('send-to-texture-studio-btn').addEventListener('click', async () =>
    {
        const Doc = ActiveDoc();
        if (!Doc) return;
        const Btn = document.getElementById('send-to-texture-studio-btn');
        Btn.disabled = true;
        try
        {
            const Payload = SerializeTopologySurfaceForBridge(Doc.surface, {
                udimTiles: Doc.udimTiles,
                importanceGroups: Doc.importanceGroups
            });
            await SaveUvBridgeTransfer(Payload, 'latest');
            LogEvent('BRIDGE', `Sent "${Doc.name}" (${Payload.triCount} tris, ${Payload.partNames.length} parts) to Texture Studio`, 'live');
            Toast(`Transferred "${Doc.name}" to Texture Studio`);
            window.location.href = '../TextureStudio/?uvImport=latest#paint';
        }
        catch (Err)
        {
            LogEvent('ERR', `Bridge transfer failed: ${Err.message}`);
            Toast(`Bridge failed: ${Err.message}`);
            Btn.disabled = false;
        }
    });
    document.getElementById('export-close-btn').addEventListener('click', () => document.getElementById('export-dialog').close());
    document.getElementById('download-obj-btn').addEventListener('click', () =>
    {
        const Doc = ActiveDoc();
        DownloadBlob(`${Doc.name.replace(/\s+/g, '_')}.obj`, Doc.surface.ExportObj(), 'text/plain');
    });
    document.getElementById('download-svg-btn').addEventListener('click', () =>
    {
        const Doc = ActiveDoc();
        DownloadBlob(`${Doc.name.replace(/\s+/g, '_')}_UV.svg`, BuildSvgAtlasExport(Doc), 'image/svg+xml');
    });
    document.getElementById('download-json-btn').addEventListener('click', () =>
    {
        const Doc = ActiveDoc();
        const Payload = {
            asset: Doc.name,
            udimTiles: Doc.udimTiles,
            importanceGroups: Doc.importanceGroups.map(G => ({
                ...G,
                islands: Doc.islands.filter(I => I.importanceGroupId === G.id).map(I => I.displayName)
            }))
        };
        DownloadBlob(`${Doc.name.replace(/\s+/g, '_')}_Budgets.json`, JSON.stringify(Payload, null, 2), 'application/json');
    });
}

function Bootstrap()
{
    Canvas2D = new UVCanvas2D(document.getElementById('uv-canvas-2d'));
    Viewport3D = new UVViewport3D(document.getElementById('mesh-viewport-mount'), {
        onPickFace: (FaceIndex, Additive, { loop = false, path = false, island = false, edgeVa = null, edgeVb = null } = {}) =>
        {
            const Doc = ActiveDoc();
            if (!StudioState.syncSelection) return;
            if (island || StudioState.selectionMode === 'island')
            {
                const Isl = Doc.islands.find(I => I.faceIndices.includes(FaceIndex));
                if (Isl) Doc.surface.SelectIsland(Isl.id, Additive, Doc.islands);
                Toast(`Linked UV Island selected`);
            }
            else if (loop)
            {
                const Cnt = Doc.surface.SelectFaceLoop3D(FaceIndex, edgeVa, edgeVb, Additive, Doc.islands);
                LogEvent('SEL', `Selected 3D face loop (${Cnt} faces)`);
                Toast(`3D Face Loop: ${Cnt} faces`);
            }
            else if (path && Doc.surface.activeFace != null)
            {
                const Cnt = Doc.surface.SelectShortestFacePath3D(Doc.surface.activeFace, FaceIndex, Additive, Doc.islands);
                LogEvent('SEL', `Selected shortest face path (${Cnt} faces)`);
                Toast(`Shortest Face Path: ${Cnt} faces`);
            }
            else Doc.surface.SelectFace(FaceIndex, Additive, Doc.islands);
            SyncAllViews(true);
        },
        onPickEdge: (Va, Vb, { additive = false, loop = false, ring = false, path = false } = {}) =>
        {
            const Doc = ActiveDoc();
            if (!Doc) return;
            if (ring)
            {
                const Cnt = Doc.surface.SelectEdgeRing3D(Va, Vb, additive, Doc.islands);
                LogEvent('EDGE', `Selected 3D edge ring (${Cnt} edges)`);
                Toast(`3D Edge Ring: ${Cnt} edges`);
            }
            else if (loop)
            {
                const Cnt = Doc.surface.SelectEdgeLoop3D(Va, Vb, additive, Doc.islands);
                LogEvent('EDGE', `Selected 3D edge loop (${Cnt} edges)`);
                Toast(`3D Edge Loop: ${Cnt} edges`);
            }
            else if (path && Doc.surface.activeEdge3D)
            {
                const Cnt = Doc.surface.SelectShortestPath3D(Doc.surface.activeEdge3D, EdgeKey(Va, Vb), additive, Doc.islands);
                LogEvent('EDGE', `Selected 3D shortest edge path (${Cnt} edges)`);
                Toast(`Shortest Path: ${Cnt} edges`);
            }
            else
            {
                const Cnt = Doc.surface.SelectEdge3D(Va, Vb, additive, Doc.islands);
                LogEvent('EDGE', `Selected ${Cnt} 3D edge(s)`);
            }
            SyncAllViews(true);
        },
        onPickVertex: (Vert3D, { additive = false, loop = false, path = false, edgeVa = null, edgeVb = null } = {}) =>
        {
            const Doc = ActiveDoc();
            if (!Doc) return;
            if (loop && edgeVa != null && edgeVb != null)
            {
                const Cnt = Doc.surface.SelectEdgeLoop3D(edgeVa, edgeVb, additive, Doc.islands);
                Doc.surface.SyncSelectionsFromMode('vertex', Doc.islands);
                Toast(`3D Vertex Loop (${Cnt} edges)`);
            }
            else if (path && Doc.surface.activeVertex3D != null)
            {
                const Cnt = Doc.surface.SelectShortestVertexPath3D(Doc.surface.activeVertex3D, Vert3D, additive, Doc.islands);
                Toast(`Shortest Vertex Path: ${Cnt} verts`);
            }
            else
            {
                if (!additive) Doc.surface.ClearSelection();
                for (const F of Doc.surface.faces)
                {
                    for (const C of F.corners)
                    {
                        if (C.v === Vert3D) Doc.surface.selectedUVs.add(C.uv);
                    }
                }
                Doc.surface.activeVertex3D = Vert3D;
                Doc.surface.SyncSelectionsFromMode('vertex', Doc.islands);
            }
            SyncAllViews(true);
        },
        onToggleEdgeSeam: (Va, Vb, { loop = false } = {}) =>
        {
            RunWithUndo(loop ? 'Toggle 3D Seam Loop' : 'Toggle 3D Seam', Doc =>
            {
                if (loop)
                {
                    const Res = Doc.surface.ToggleSeamLoop3D(Va, Vb, null, Doc.islands);
                    if (!Res.marked) Doc.surface._hasClearedSeams = true;
                    LogEvent('SEAM', `${Res.marked ? 'Marked' : 'Cleared'} 3D seam loop (${Res.count} edges)`, 'warm');
                    Toast(`${Res.marked ? 'Marked' : 'Cleared'} 3D Seam Loop (${Res.count} edges)`);
                }
                else
                {
                    const Marked = Doc.surface.ToggleSeamEdge3D(Va, Vb, null, Doc.islands);
                    if (!Marked) Doc.surface._hasClearedSeams = true;
                    LogEvent('SEAM', `${Marked ? 'Marked' : 'Cleared'} 3D UV seam edge`, 'warm');
                    Toast(Marked ? 'Marked 3D UV Seam' : 'Cleared 3D UV Seam');
                }
            });
        },
        onBoxSelect3D: ({ faces, verts3d, edges3d }, { additive = false, subtract = false } = {}) =>
        {
            const Doc = ActiveDoc();
            if (!Doc || !StudioState.syncSelection) return;
            const Mode = StudioState.selectionMode;
            if (!additive && !subtract) Doc.surface.ClearSelection();

            if (Mode === 'island')
            {
                const FaceSet = new Set(faces);
                for (const Isl of Doc.islands)
                {
                    if (Isl.hidden) continue;
                    if (Isl.faceIndices.some(Fi => FaceSet.has(Fi)))
                    {
                        if (subtract) { for (const Fi of Isl.faceIndices) Doc.surface.selectedFaces.delete(Fi); }
                        else Doc.surface.SelectIsland(Isl.id, true, Doc.islands);
                    }
                }
                if (subtract) Doc.surface.SyncSelectionsFromMode('face', Doc.islands);
            }
            else if (Mode === 'face')
            {
                for (const Fi of faces)
                {
                    if (subtract) Doc.surface.selectedFaces.delete(Fi);
                    else Doc.surface.selectedFaces.add(Fi);
                }
                Doc.surface.SyncSelectionsFromMode('face', Doc.islands);
                if (faces.length) Doc.surface.activeFace = faces[faces.length - 1];
            }
            else if (Mode === 'edge')
            {
                for (const Ek3 of edges3d)
                {
                    if (subtract) Doc.surface.selectedEdges3D.delete(Ek3);
                    else Doc.surface.selectedEdges3D.add(Ek3);
                }
                Doc.surface.SyncSelectionsFromMode('edge', Doc.islands);
            }
            else
            {
                const VSet = new Set(verts3d);
                for (const F of Doc.surface.faces)
                {
                    for (const C of F.corners)
                    {
                        if (VSet.has(C.v))
                        {
                            if (subtract) Doc.surface.selectedUVs.delete(C.uv);
                            else Doc.surface.selectedUVs.add(C.uv);
                        }
                    }
                }
                Doc.surface.SyncSelectionsFromMode('vertex', Doc.islands);
            }
            SyncAllViews(true);
            Toast(`3D Box Select: ${Doc.surface.selectedFaces.size} faces`);
        },
        onPaintSelect3D: ({ faces, verts3d, edges3d }, { subtract = false, commit = false } = {}) =>
        {
            const Doc = ActiveDoc();
            if (!Doc || !StudioState.syncSelection) return;
            const Mode = StudioState.selectionMode;

            if (faces.length || verts3d.length || edges3d.length)
            {
                if (Mode === 'island')
                {
                    const FaceSet = new Set(faces);
                    for (const Isl of Doc.islands)
                    {
                        if (Isl.hidden) continue;
                        if (Isl.faceIndices.some(Fi => FaceSet.has(Fi)))
                        {
                            if (subtract)
                            {
                                Doc.surface.selectedIslands.delete(Isl.id);
                                for (const Fi of Isl.faceIndices) Doc.surface.selectedFaces.delete(Fi);
                            }
                            else
                            {
                                Doc.surface.selectedIslands.add(Isl.id);
                                for (const Fi of Isl.faceIndices) Doc.surface.selectedFaces.add(Fi);
                            }
                        }
                    }
                }
                else if (Mode === 'face')
                {
                    for (const Fi of faces)
                    {
                        if (subtract) Doc.surface.selectedFaces.delete(Fi);
                        else Doc.surface.selectedFaces.add(Fi);
                    }
                    if (!subtract && faces.length) Doc.surface.activeFace = faces[faces.length - 1];
                }
                else if (Mode === 'edge')
                {
                    for (const Ek3 of edges3d)
                    {
                        if (subtract) Doc.surface.selectedEdges3D.delete(Ek3);
                        else Doc.surface.selectedEdges3D.add(Ek3);
                    }
                }
                else
                {
                    const VSet = new Set(verts3d);
                    for (const Fi of faces)
                    {
                        const F = Doc.surface.faces[Fi];
                        if (!F) continue;
                        for (const C of F.corners)
                        {
                            if (VSet.has(C.v))
                            {
                                if (subtract) Doc.surface.selectedUVs.delete(C.uv);
                                else Doc.surface.selectedUVs.add(C.uv);
                            }
                        }
                    }
                }
            }

            if (commit)
            {
                Doc.surface.SyncSelectionsFromMode(Mode, Doc.islands);
                SyncAllViews(true);
            }
            else
            {
                FastPaintVisualSync(Doc);
            }
        },
        onContextMenu: (ClientX, ClientY, HitEdge) =>
        {
            OpenUvContextMenu(ClientX, ClientY, { source: '3d', hitEdge: HitEdge });
        }
    });

    InspectorScroll = SmoothScroll(document.getElementById('inspector-scroll'), document.getElementById('inspector-body'));
    OutlinerScroll = SmoothScroll(document.getElementById('outliner-scroll'), document.getElementById('outliner-list'));

    // Initialize default workspace documents from SampleAssetCatalog
    for (const Sample of SampleAssetCatalog)
    {
        StudioState.documents.push(CreateDocumentFromFactory(Sample));
    }
    StudioState.activeDocId = StudioState.documents[0].id;

    InitStudioIconsAndBindings();
    BuildQuickUnwrapDrawer();
    SetupCanvas2DInteraction();
    SetupKeyboardShortcuts();

    window.addEventListener('resize', () => SyncAllViews());
    requestAnimationFrame(() =>
    {
        SyncAllViews(true);
        Canvas2D.FrameUdimTiles(ActiveDoc().udimTiles);
        Viewport3D.CameraPreset('Persp');
        LogEvent('INIT', `Frontier UV Studio initialized (${SampleAssetCatalog.length} assets, Controlled Integrator active)`, 'live');
    });
}

Bootstrap();
