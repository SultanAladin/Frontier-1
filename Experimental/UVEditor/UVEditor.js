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
    ApplyAlignOrRectify,
    StackSimilarIslands,
    UnstackOverlappingIslands
} from './Engine/UnwrapSolver.js';
import { SolveImportancePack } from './Engine/ImportancePackingSolver.js';
import { SampleAssetCatalog } from './Engine/SampleAssets.js';
import { UVCanvas2D } from './Engine/UVCanvas2D.js';
import { UVViewport3D, MATCAP_STUDIO_CATALOG, BuildMatcapPreviewDataUrl } from './Engine/UVViewport3D.js';
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
    inspectorTab: 'packing', // 'packing' | 'unwrap' | 'udim' | 'shading' | 'check'
    udimFilter: 'all', // 'all' | UDIM tile number string ('1001', '1002', ...) — ONLY Search + UDIM filters!
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
    showSeams: false,
    showBoundaries: false,
    showPins: true,
    showIslandBadges: true,
    udimMatrixRows: 3,
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
    pendingGroupPct: 25,
    materialBrush: {
        matcapSlot: 5,
        pbrColor: '#4c7dc9',
        pbrMetallic: 0.60,
        pbrRoughness: 0.24,
        pbrSpecular: 0.68,
        matcapMix: 0.62
    },
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
    Doc.surface.objectId = DocId;
    RefreshDocAnalysis(Doc);
    Doc.surface.EnsureLayerFaceRefs(Doc.islands);
    Doc.surface.AutoAssignMatcapsByPart(Doc.islands);
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

function BindSceneLayerFaceRefs(Doc = ActiveDoc(), FilterUdimNum = null)
{
    if (!Doc?.surface) return null;
    const Cache = Doc.surface.EnsureLayerFaceRefs(Doc.islands);
    const ObjId = Doc.surface.objectId || Doc.id;
    for (let i = 0; i < Doc.importanceGroups.length; i++)
    {
        const Grp = Doc.importanceGroups[i];
        const FullRef = Doc.surface.GetLayerFaceRef(Grp.id, Doc.islands, null);
        if (!(Grp.faceRefs instanceof Map)) Grp.faceRefs = new Map();
        Grp.faceRefs.set(ObjId, FullRef);
        Grp.faceRef = FilterUdimNum === null
            ? FullRef
            : Doc.surface.GetLayerFaceRef(Grp.id, Doc.islands, FilterUdimNum);
    }
    return Cache;
}

function IsFaceGroupRefSelected(Surface, Ref)
{
    if (!Surface || !Ref || Ref.faceCount === 0) return false;
    const F = Ref.faceIndices;
    if (Surface.selectedFaces.size < F.length) return false;
    if (!Surface.selectedFaces.has(F[0]) || !Surface.selectedFaces.has(F[F.length - 1])) return false;
    const Mid = F[F.length >> 1];
    return Surface.selectedFaces.has(Mid);
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

function RefreshLayerBudgetLive(Doc = ActiveDoc())
{
    if (!Doc) return;
    const ExplicitSum = Doc.importanceGroups.reduce((S, G) => S + G.targetPercent, 0);
    const Rem = Math.max(0, 100 - ExplicitSum);

    for (const Grp of Doc.importanceGroups)
    {
        const PctNode = document.querySelector(`#outliner-list .pz-uv-layer-card[data-layer-id="${Grp.id}"] [data-layer-pct]`);
        if (PctNode) PctNode.textContent = `${Math.round(Grp.targetPercent)}%`;
    }
    const RemPctNode = document.querySelector('#outliner-list .pz-uv-layer-card[data-layer-id="__unassigned__"] [data-layer-pct]');
    if (RemPctNode)
    {
        RemPctNode.textContent = ExplicitSum > 100 ? `${ExplicitSum.toFixed(0)}% !` : `${Rem.toFixed(0)}%`;
        RemPctNode.classList.toggle('warn', ExplicitSum > 100);
    }

    const RemValEl = document.getElementById('lp-kpi-rem-val');
    if (RemValEl) RemValEl.textContent = `${Rem.toFixed(0)}%`;

    _lastBudgetCardSig = null;
    RenderUdimSpaceBudgetCard(Doc);
    RenderLegendStrip(Doc);
}

function FastSyncSelectionViews(Doc = ActiveDoc(), RefreshInspector = true)
{
    if (!Doc) return;
    if (Viewport3D)
    {
        Viewport3D.selectionMode = StudioState.selectionMode;
        Viewport3D.FastUpdateGpuFaceMask(Doc.surface, Doc.islands);
    }
    ScheduleFast2DRender(Doc);

    const FilterUdimNum = StudioState.udimFilter === 'all' ? null : Number(StudioState.udimFilter);
    const Cards = document.querySelectorAll('#outliner-list .pz-uv-layer-card[data-layer-id]');
    for (const Card of Cards)
    {
        const Lid = Card.dataset.layerId;
        const Ref = Doc.surface.GetLayerFaceRef(Lid === '__unassigned__' ? null : Lid, Doc.islands, FilterUdimNum);
        const IsActiveOrSel = (Doc.activeLayerId === Lid) || IsFaceGroupRefSelected(Doc.surface, Ref);
        Card.classList.toggle('selected', IsActiveOrSel);
    }

    const SelIslCount = Doc.surface.selectedIslands.size;
    const SelVal = document.getElementById('lp-kpi-sel-val');
    const SelSub = document.getElementById('lp-kpi-sel-sub');
    if (SelVal) SelVal.textContent = `${SelIslCount} / ${Doc.islands.length}`;
    if (SelSub) SelSub.textContent = SelIslCount ? `${Doc.surface.selectedFaces.size.toLocaleString()} faces` : 'Total islands';

    const HudSel = document.getElementById('hud-selection');
    if (HudSel) HudSel.innerHTML = `Sel <b>${Doc.surface.selectedEdges3D.size} ed · ${SelIslCount} isl · ${Doc.surface.selectedUVs.size} uv</b>`;

    const SelInfo = ComputeInspectorSelectionInfo(Doc);
    const NameInput = document.getElementById('inspector-object-name');
    if (NameInput && document.activeElement !== NameInput) NameInput.value = SelInfo.title;
    const SubEl = document.getElementById('inspector-object-sub');
    if (SubEl) SubEl.textContent = SelInfo.subtitle;
    const FootLeftEl = document.getElementById('inspector-footer-left');
    if (FootLeftEl) FootLeftEl.textContent = SelInfo.footerLeft;

    StudioState._lastInspectorSelSig = `${Doc.id}|${SelIslCount}:${[...Doc.surface.selectedIslands].slice(0, 6).join(',')}|${Doc.surface.selectedFaces.size}:${Doc.surface.activeFace ?? -1}|${Doc.surface.selectedEdges3D.size}|${Doc.surface.selectedUVs.size}`;
    if (RefreshInspector) RebuildInspector();
    else Broadcast();
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

    const SelSig = `${Doc.id}|${Doc.surface.selectedIslands.size}:${[...Doc.surface.selectedIslands].slice(0, 6).join(',')}|${Doc.surface.selectedFaces.size}:${Doc.surface.activeFace ?? -1}|${Doc.surface.selectedEdges3D.size}|${Doc.surface.selectedUVs.size}`;
    const SelectionChanged = SelSig !== StudioState._lastInspectorSelSig;
    StudioState._lastInspectorSelSig = SelSig;

    if (UpdateInspector || SelectionChanged) RebuildInspector();
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

function CollectTargetUvIndices(Doc)
{
    const UvSet = new Set();
    if (!Doc?.surface) return UvSet;
    if (Doc.surface.selectedUVs.size > 0)
    {
        for (const U of Doc.surface.selectedUVs) UvSet.add(U);
        return UvSet;
    }
    if (Doc.surface.selectedFaces.size > 0)
    {
        for (const Fi of Doc.surface.selectedFaces)
        {
            const F = Doc.surface.faces[Fi];
            if (F) for (const C of F.corners) UvSet.add(C.uv);
        }
        return UvSet;
    }
    if (Doc.surface.selectedIslands.size > 0)
    {
        for (const Isl of Doc.islands)
        {
            if (!Doc.surface.selectedIslands.has(Isl.id)) continue;
            for (const Fi of Isl.faceIndices)
            {
                const F = Doc.surface.faces[Fi];
                if (F) for (const C of F.corners) UvSet.add(C.uv);
            }
        }
        return UvSet;
    }
    for (let i = 0; i < Doc.surface.uvCoords.length; i++) UvSet.add(i);
    return UvSet;
}

function ComputeSelectionUvTransformStats(Doc)
{
    const UvSet = CollectTargetUvIndices(Doc);
    if (!UvSet.size)
    {
        return { cx: 0.5, cy: 0.5, du: 1.0, dv: 1.0, udim: Doc?.activeUdim || 1001, count: 0, hasExplicitSel: false };
    }
    let MinU = Infinity, MinV = Infinity, MaxU = -Infinity, MaxV = -Infinity;
    for (const Idx of UvSet)
    {
        const P = Doc.surface.uvCoords[Idx];
        if (!P) continue;
        if (P[0] < MinU) MinU = P[0];
        if (P[0] > MaxU) MaxU = P[0];
        if (P[1] < MinV) MinV = P[1];
        if (P[1] > MaxV) MaxV = P[1];
    }
    const Cx = Number.isFinite(MinU) ? 0.5 * (MinU + MaxU) : 0.5;
    const Cy = Number.isFinite(MinV) ? 0.5 * (MinV + MaxV) : 0.5;
    const Du = Number.isFinite(MinU) ? Math.max(0.0001, MaxU - MinU) : 1.0;
    const Dv = Number.isFinite(MinV) ? Math.max(0.0001, MaxV - MinV) : 1.0;
    const HasExplicit = Doc.surface.selectedUVs.size > 0 || Doc.surface.selectedFaces.size > 0 || Doc.surface.selectedIslands.size > 0;
    return {
        cx: Cx,
        cy: Cy,
        du: Du,
        dv: Dv,
        udim: UdimFromUV(Cx, Cy),
        count: UvSet.size,
        hasExplicitSel: HasExplicit
    };
}

function ClassifyIslandHealth(Doc, Isl)
{
    const HasError = Isl.faceIndices.some(Fi =>
        Doc.diagnostics?.OverlappingFaces?.has(Fi) || Doc.diagnostics?.FlippedFaces?.has(Fi));
    if (HasError) return 'error';

    let Pinned = false;
    for (const Fi of Isl.faceIndices)
    {
        const F = Doc.surface.faces[Fi];
        if (!F) continue;
        for (const C of F.corners) if (Doc.surface.pinned.has(C.uv)) { Pinned = true; break; }
        if (Pinned) break;
    }
    if ((Isl.angleStretch ?? 0) > 0.30) return 'caution';
    if (Pinned) return 'pinned';
    return 'ok';
}

function ComputeInspectorSelectionInfo(Doc)
{
    const S = Doc.surface;
    const SelIslList = Doc.islands.filter(I => S.selectedIslands.has(I.id));
    const FaceSet = new Set(S.selectedFaces);
    for (const Isl of SelIslList)
    {
        for (const Fi of Isl.faceIndices) FaceSet.add(Fi);
    }
    if (FaceSet.size === 0 && (S.selectedEdges3D.size > 0 || S.selectedUVs.size > 0))
    {
        const Adj = S.EnsureAdjacencyIndices();
        for (const K3 of S.selectedEdges3D)
        {
            for (const Fi of Adj.edge3dFaces.get(K3) || []) FaceSet.add(Fi);
        }
        for (const UvId of S.selectedUVs)
        {
            for (const Fi of Adj.uvToFaces[UvId] || []) FaceSet.add(Fi);
        }
    }

    const HasSelection = FaceSet.size > 0;
    const TargetFaces = HasSelection ? [...FaceSet] : S.faces.map((_, i) => i);
    const MatcapTotal = MATCAP_STUDIO_CATALOG.length;
    const MatcapCounts = new Int32Array(MatcapTotal);
    let OverlapCount = 0, FlippedCount = 0;
    let StretchWeighted = 0, Area3DSum = 0, AreaUvSum = 0;
    const UvSet = new Set();
    const UdimSet = new Set();

    const Diag = Doc.diagnostics;
    for (const Fi of TargetFaces)
    {
        const F = S.faces[Fi];
        if (!F) continue;
        const Slot = (F.matcapSlot || 0) & 15;
        MatcapCounts[Slot]++;
        if (Diag?.OverlappingFaces?.has(Fi)) OverlapCount++;
        if (Diag?.FlippedFaces?.has(Fi)) FlippedCount++;
        const D = typeof S.GetFaceDistortionFast === 'function' ? S.GetFaceDistortionFast(Fi) : { area3D: S.FaceArea3D(Fi), angleStretch: S.FaceTrianglesDistortion(Fi).angleStretch };
        Area3DSum += D.area3D;
        StretchWeighted += D.angleStretch * Math.max(1e-7, D.area3D);
        AreaUvSum += Math.abs(S.FaceSignedAreaUV(Fi));
        for (const C of F.corners) UvSet.add(C.uv);
    }

    let DominantSlot = 0, BestSlotCnt = -1, DistinctSlots = 0;
    for (let i = 0; i < MatcapTotal; i++)
    {
        if (MatcapCounts[i] > 0) DistinctSlots++;
        if (MatcapCounts[i] > BestSlotCnt) { BestSlotCnt = MatcapCounts[i]; DominantSlot = i; }
    }

    let PinnedCount = 0;
    for (const U of UvSet) if (S.pinned.has(U)) PinnedCount++;

    const RefIslands = SelIslList.length > 0
        ? SelIslList
        : (HasSelection ? Doc.islands.filter(I => I.faceIndices.some(Fi => FaceSet.has(Fi))) : Doc.islands);
    for (const I of RefIslands) UdimSet.add(I.udimTile);
    const AvgDensity = RefIslands.length
        ? RefIslands.reduce((Sum, I) => Sum + (I.texelDensity || 0), 0) / RefIslands.length
        : 0;
    const MeanStretchPct = Area3DSum > 1e-8 ? (StretchWeighted / Area3DSum) * 100 : 0;

    const DominantSpec = MATCAP_STUDIO_CATALOG[DominantSlot] || MATCAP_STUDIO_CATALOG[0];
    let Title = Doc.name;
    let Subtitle = `${Doc.islands.length} charts · ${S.faces.length} faces · Whole Surface`;
    let ScopeTag = `Entire Surface (${S.faces.length} faces)`;

    if (HasSelection)
    {
        if (SelIslList.length === 1)
        {
            const Isl = SelIslList[0];
            Title = Isl.displayName || Isl.partId || `Chart #${Isl.id + 1}`;
            Subtitle = `${TargetFaces.length} faces · ${UvSet.size} UVs · UDIM ${Isl.udimTile} · ${AvgDensity.toFixed(1)} px/cm`;
            ScopeTag = `${Title} · ${TargetFaces.length} faces`;
        }
        else if (SelIslList.length > 1)
        {
            const UniqueParts = [...new Set(SelIslList.map(I => I.partId || I.displayName).filter(Boolean))];
            Title = UniqueParts.length === 1 ? `${UniqueParts[0]} (${SelIslList.length} charts)` : `${SelIslList.length} UV Charts Selected`;
            Subtitle = `${TargetFaces.length} faces · ${UvSet.size} UVs · Stretch ${MeanStretchPct.toFixed(1)}% · ${AvgDensity.toFixed(1)} px/cm`;
            ScopeTag = `${SelIslList.length} charts · ${TargetFaces.length} faces`;
        }
        else if (S.selectedEdges3D.size > 0 && S.selectedFaces.size === 0)
        {
            Title = `${S.selectedEdges3D.size} Edge${S.selectedEdges3D.size > 1 ? 's' : ''} Selected`;
            Subtitle = `${TargetFaces.length} adj faces · ${UvSet.size} UVs · ${S.seams.size} total seams`;
            ScopeTag = `${S.selectedEdges3D.size} edges (${TargetFaces.length} adj faces)`;
        }
        else if (S.selectedUVs.size > 0 && S.selectedFaces.size === 0)
        {
            Title = `${S.selectedUVs.size} UV Vert${S.selectedUVs.size > 1 ? 's' : ''} Selected`;
            Subtitle = `${PinnedCount} pinned · ${TargetFaces.length} adj faces · Stretch ${MeanStretchPct.toFixed(1)}%`;
            ScopeTag = `${S.selectedUVs.size} UVs (${PinnedCount} pinned)`;
        }
        else
        {
            const SamplePart = S.faces[TargetFaces[0]]?.partId || 'Surface';
            Title = TargetFaces.length === 1 ? `${SamplePart} · Face #${TargetFaces[0]}` : `${SamplePart} · ${TargetFaces.length} Faces`;
            Subtitle = `${UvSet.size} UVs · Stretch ${MeanStretchPct.toFixed(1)}% · ${AvgDensity.toFixed(1)} px/cm`;
            ScopeTag = `${TargetFaces.length} selected faces`;
        }
    }

    const FooterLeft = Viewport3D?.shadingMode === 'fast-pbr'
        ? `Material · M${Math.round((Viewport3D.pbrMetallic ?? 0.55) * 100)} R${Math.round((Viewport3D.pbrRoughness ?? 0.28) * 100)}`
        : DistinctSlots > 1
            ? `${DistinctSlots} MatCaps (${DominantSpec.short}+)`
            : `MatCap: ${DominantSpec.label}`;

    return {
        hasSelection: HasSelection,
        title: Title,
        subtitle: Subtitle,
        scopeTag: ScopeTag,
        footerLeft: FooterLeft,
        targetFaces: TargetFaces,
        faceCount: TargetFaces.length,
        uvCount: UvSet.size,
        islandCount: SelIslList.length,
        matcapCounts: MatcapCounts,
        distinctSlots: DistinctSlots,
        dominantSlot: DominantSlot,
        overlapCount: OverlapCount,
        flippedCount: FlippedCount,
        meanStretchPct: MeanStretchPct,
        avgDensity: AvgDensity,
        area3D: Area3DSum,
        areaUV: AreaUvSum,
        pinnedCount: PinnedCount,
        udims: [...UdimSet]
    };
}

let _lastUdimFilterSig = null;
function RenderUdimFilterBar(Doc)
{
    const Bar = document.getElementById('outliner-filters');
    if (!Bar || !Doc) return;
    if (StudioState.udimFilter !== 'all' && !Doc.udimTiles.some(T => String(T.id) === String(StudioState.udimFilter)))
    {
        StudioState.udimFilter = 'all';
    }
    const Sig = `${Doc.id}|${StudioState.udimFilter}|${Doc.udimTiles.map(T => T.id).join(',')}`;
    if (_lastUdimFilterSig === Sig) return;
    _lastUdimFilterSig = Sig;

    Bar.textContent = '';
    const MakeBtn = (Label, Val) =>
    {
        const Btn = Construct('button', String(StudioState.udimFilter) === String(Val) ? 'active' : '', Label);
        Btn.type = 'button';
        Btn.dataset.udim = String(Val);
        Btn.addEventListener('click', () =>
        {
            StudioState.udimFilter = String(Val);
            if (Val !== 'all')
            {
                Doc.activeUdim = Number(Val);
                const O = UdimOrigin(Number(Val));
                Canvas2D.FrameBounds(O.u, O.v, O.u + 1, O.v + 1, 0.14);
            }
            _lastUdimFilterSig = null;
            _lastOutlinerSig = null;
            SyncAllViews(true);
        });
        return Btn;
    };

    Bar.append(MakeBtn('All UDIMs', 'all'));
    for (const Tile of [...Doc.udimTiles].sort((A, B) => A.id - B.id))
    {
        Bar.append(MakeBtn(`UDIM ${Tile.id}`, String(Tile.id)));
    }
}

let _lastBudgetCardSig = null;
function RenderUdimSpaceBudgetCard(Doc)
{
    const Footer = document.getElementById('lp-udim-budget-card');
    if (!Footer || !Doc) return;

    const FilterTileId = StudioState.udimFilter === 'all' ? Doc.activeUdim : Number(StudioState.udimFilter);
    const FilterUdimNum = StudioState.udimFilter === 'all' ? null : Number(StudioState.udimFilter);
    const RefCache = Doc.surface.EnsureLayerFaceRefs(Doc.islands);
    const ActiveReport = Doc.lastPackReport?.tileReports?.find(R => R.tileId === FilterTileId) || Doc.lastPackReport?.tileReports?.[0];
    const TileTotals = RefCache.udimTotals.get(FilterTileId) || { islands: 0, uvArea: 0 };
    const RawTileOcc = TileTotals.uvArea * 100;
    const DisplayOcc = (ActiveReport && ActiveReport.tileId === FilterTileId) ? ActiveReport.tileUtilizationPct : RawTileOcc;

    const GrpKey = Doc.importanceGroups.map(G => `${G.id}:${G.targetPercent}:${G.color}`).join(';');
    const BudgetSig = `${Doc.id}|${StudioState.udimFilter}|${FilterTileId}|${StudioState.packOptions.budgetMode}|${RefCache.sig}|${DisplayOcc.toFixed(2)}|${GrpKey}`;
    if (_lastBudgetCardSig === BudgetSig) return;
    _lastBudgetCardSig = BudgetSig;

    Footer.textContent = '';

    const Spectrum = Construct('div', 'allocation-spectrum');
    const ExplicitSum = Doc.importanceGroups.reduce((S, G) => S + G.targetPercent, 0);

    for (const Grp of Doc.importanceGroups)
    {
        if (Grp.targetPercent <= 0) continue;
        const Seg = Construct('div', 'alloc-segment');
        Seg.style.width = `${Math.max(1.5, Grp.targetPercent)}%`;
        Seg.style.background = Grp.color;
        Spectrum.append(Seg);
    }

    const RemRef = Doc.surface.GetLayerFaceRef(null, Doc.islands, FilterUdimNum);
    const RemTarget = Math.max(0, 100 - ExplicitSum);
    if (RemTarget > 0.1 || RemRef.faceCount > 0)
    {
        const Seg = Construct('div', 'alloc-segment');
        Seg.style.width = `${Math.max(1.5, RemTarget)}%`;
        Seg.style.background = '#6e6e6e';
        Spectrum.append(Seg);
    }

    // Sticky Outliner Footer: UDIM Space Budget + colour spectrum ONLY (no per-layer % text chips!)
    Footer.append(
        Construct('div', 'pz-budget-footer-top', [
            Construct('div', 'pz-budget-footer-title', [
                ConstructGlyph('Pack'),
                Construct('span', null, `UDIM ${FilterTileId} Space Budget`)
            ]),
            Construct('span', 'pz-budget-footer-fill', `${DisplayOcc.toFixed(1)}% filled · ${StudioState.packOptions.budgetMode === 'strict-ratio' ? 'STRICT' : 'FILL'}`)
        ]),
        Spectrum
    );
}

function UpdateHeaderAndHud(Doc)
{
    RenderUdimFilterBar(Doc);
    RenderUdimSpaceBudgetCard(Doc);

    let ErrorCharts = 0, CautionCharts = 0;
    for (const Isl of Doc.islands)
    {
        const St = ClassifyIslandHealth(Doc, Isl);
        if (St === 'error') ErrorCharts++;
        else if (St === 'caution') CautionCharts++;
    }
    const Overlaps = Doc.diagnostics.overlapCount;
    const OverlapMetric = Overlaps + (Doc.diagnostics.flippedCount || 0);

    // 1. Update the 3 Pack Allocation Tiles in UV Space Left Panel
    const FilterTileId = StudioState.udimFilter === 'all' ? Doc.activeUdim : Number(StudioState.udimFilter);
    const ActiveReport = Doc.lastPackReport?.tileReports?.find(R => R.tileId === FilterTileId) || Doc.lastPackReport?.tileReports?.[0];
    const CurTileIslands = Doc.islands.filter(I => I.udimTile === FilterTileId && !I.hidden);
    const RawTileOcc = CurTileIslands.reduce((S, I) => S + I.areaUV, 0) * 100;
    const DisplayOcc = (ActiveReport && ActiveReport.tileId === FilterTileId) ? ActiveReport.tileUtilizationPct : RawTileOcc;
    const SelIslCount = Doc.surface.selectedIslands.size;
    const RemIslands = Doc.islands.filter(I => !I.importanceGroupId).length;
    const RemPct = Math.max(0, 100 - Doc.importanceGroups.reduce((S, G) => S + G.targetPercent, 0));

    const UtilVal = document.getElementById('lp-kpi-util-val');
    const UtilSub = document.getElementById('lp-kpi-util-sub');
    if (UtilVal) UtilVal.textContent = `${DisplayOcc.toFixed(1)}%`;
    if (UtilSub) UtilSub.textContent = `UDIM ${FilterTileId} · ${CurTileIslands.length} isl`;

    const SelVal = document.getElementById('lp-kpi-sel-val');
    const SelSub = document.getElementById('lp-kpi-sel-sub');
    if (SelVal) SelVal.textContent = `${SelIslCount} / ${Doc.islands.length}`;
    if (SelSub) SelSub.textContent = SelIslCount ? 'Selected islands' : 'Total islands';

    const RemVal = document.getElementById('lp-kpi-rem-val');
    const RemSub = document.getElementById('lp-kpi-rem-sub');
    if (RemVal) RemVal.textContent = `${RemPct.toFixed(0)}%`;
    if (RemSub) RemSub.textContent = `${RemIslands} / ${Doc.islands.length} isl`;

    // 2. Update Stretch Caution (Amber) + Overlaps (Red) Bento Cards
    document.getElementById('stat-udim-count').textContent = String(CautionCharts);
    document.getElementById('stat-islands-count').textContent = String(OverlapMetric);

    const IssuesCard = document.getElementById('stat-issues-card');
    if (IssuesCard) IssuesCard.classList.toggle('has-caution', CautionCharts > 0);

    const IssuesLbl = document.getElementById('stat-issues-label');
    if (IssuesLbl) IssuesLbl.textContent = 'Stretch Caution';

    const OverlapCard = document.getElementById('stat-overlap-card');
    if (OverlapCard) OverlapCard.classList.toggle('has-error', OverlapMetric > 0 || ErrorCharts > 0);

    const NodeSummary = document.getElementById('pz-outliner-node-summary');
    if (NodeSummary) NodeSummary.textContent = `${Doc.islands.length} total islands · ${Doc.udimTiles.length} UDIM${Doc.udimTiles.length === 1 ? '' : 's'}`;

    const ActiveTile = Doc.udimTiles.find(T => T.id === Doc.activeUdim) || Doc.udimTiles[0];
    const ResK = ActiveTile ? (ActiveTile.resolution >= 1024 ? `${ActiveTile.resolution / 1024}K` : `${ActiveTile.resolution}px`) : '2K';
    document.getElementById('viewport-udim-pill').textContent = `UDIM ${Doc.activeUdim} · ${ResK}`;
    document.getElementById('mesh-object-pill').textContent = Doc.name;

    const HealthTag = document.getElementById('atlas-health-tag');
    HealthTag.classList.toggle('warn', Overlaps > 0);
    HealthTag.querySelector('span').textContent = Overlaps > 0 ? `${Overlaps} OVERLAPS` : '0 OVERLAPS';

    document.getElementById('hud-cursor').innerHTML = `Cursor <b>${StudioState.cursorUV[0].toFixed(2)}, ${StudioState.cursorUV[1].toFixed(2)}</b>`;
    document.getElementById('hud-selection').innerHTML = `Sel <b>${Doc.surface.selectedEdges3D.size} ed · ${Doc.surface.selectedIslands.size} isl · ${Doc.surface.selectedUVs.size} uv</b>`;
    document.getElementById('hud-stretch').innerHTML = `Seams <b>${Doc.surface.seams.size}</b> · Stretch <b>${(Doc.diagnostics.meanAngleStretch * 100).toFixed(1)}%</b>`;

    // Update Live 2D UV Selection Position & Extent Pill
    const Tf = ComputeSelectionUvTransformStats(Doc);
    const PosUEl = document.getElementById('pz-hud-pos-u');
    const PosVEl = document.getElementById('pz-hud-pos-v');
    const SizeEl = document.getElementById('pz-hud-size');
    const UdimEl = document.getElementById('pz-hud-udim');
    if (PosUEl) PosUEl.textContent = `U ${Tf.cx.toFixed(3)}`;
    if (PosVEl) PosVEl.textContent = `V ${Tf.cy.toFixed(3)}`;
    if (SizeEl) SizeEl.textContent = `Δ ${Tf.du.toFixed(2)} × ${Tf.dv.toFixed(2)}`;
    if (UdimEl) UdimEl.textContent = `UDIM ${Tf.udim}`;

    document.getElementById('undo-btn').disabled = !Doc.surface.CanUndo();
    document.getElementById('redo-btn').disabled = !Doc.surface.CanRedo();

    const SelInfo = ComputeInspectorSelectionInfo(Doc);
    const NameInput = document.getElementById('inspector-object-name');
    if (document.activeElement !== NameInput) NameInput.value = SelInfo.title;

    document.getElementById('inspector-object-sub').textContent = SelInfo.subtitle;
    const FootLeftEl = document.getElementById('inspector-footer-left');
    if (FootLeftEl) FootLeftEl.textContent = SelInfo.footerLeft;

    const TargetUdim = SelInfo.udims.length === 1 ? SelInfo.udims[0] : Doc.activeUdim;
    const TargetTile = Doc.udimTiles.find(T => T.id === TargetUdim) || ActiveTile;
    document.getElementById('inspector-footer-right').textContent = `UDIM ${TargetUdim} · ${TargetTile?.resolution || 2048} px`;
    document.getElementById('status-counts').innerHTML =
        `<b>${Doc.surface.positions.length}</b> verts · <b>${Doc.surface.faces.length}</b> faces · <b>${Doc.islands.length}</b> charts`;

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
    if (!Strip || Strip.hidden) return;
    Strip.textContent = '';
}

let _lastOutlinerSig = null;

function FrameFaceGroupRefBounds(Doc, FaceRef)
{
    if (!Doc || !FaceRef || FaceRef.islandCount === 0) return;
    const IslSet = new Set(FaceRef.islandIds);
    let MinU = Infinity, MinV = Infinity, MaxU = -Infinity, MaxV = -Infinity;
    for (let i = 0; i < Doc.islands.length; i++)
    {
        const Isl = Doc.islands[i];
        if (!IslSet.has(Isl.id)) continue;
        if (Isl.bounds.minU < MinU) MinU = Isl.bounds.minU;
        if (Isl.bounds.minV < MinV) MinV = Isl.bounds.minV;
        if (Isl.bounds.maxU > MaxU) MaxU = Isl.bounds.maxU;
        if (Isl.bounds.maxV > MaxV) MaxV = Isl.bounds.maxV;
    }
    if (Number.isFinite(MinU)) Canvas2D.FrameBounds(MinU, MinV, MaxU, MaxV, 0.18);
}

function RenderOutliner(Doc)
{
    RenderUdimFilterBar(Doc);

    const FilterUdimNum = StudioState.udimFilter === 'all' ? null : Number(StudioState.udimFilter);
    const RefCache = BindSceneLayerFaceRefs(Doc, FilterUdimNum);
    if (!Doc.activeLayerId && Doc.importanceGroups.length > 0) Doc.activeLayerId = Doc.importanceGroups[0].id;

    const GrpSig = Doc.importanceGroups.map(G => `${G.id}:${G.name}:${G.targetPercent}:${G.color}:${G.udimTarget || 'auto'}`).join(';');
    const TileSig = Doc.udimTiles.map(T => T.id).join(',');
    const OutlinerSig = `${Doc.id}|${StudioState.udimFilter}|${StudioState.searchQuery.trim()}|${RefCache?.sig || ''}|${GrpSig}|${TileSig}`;
    if (_lastOutlinerSig === OutlinerSig)
    {
        const Cards = document.querySelectorAll('#outliner-list .pz-uv-layer-card[data-layer-id]');
        for (const Card of Cards)
        {
            const Lid = Card.dataset.layerId;
            const Ref = Doc.surface.GetLayerFaceRef(Lid === '__unassigned__' ? null : Lid, Doc.islands, FilterUdimNum);
            const IsActiveOrSel = (Doc.activeLayerId === Lid) || IsFaceGroupRefSelected(Doc.surface, Ref);
            Card.classList.toggle('selected', IsActiveOrSel);
        }
        return;
    }
    _lastOutlinerSig = OutlinerSig;

    const Container = document.getElementById('outliner-list');
    Container.textContent = '';
    const Query = StudioState.searchQuery.trim().toLowerCase();

    const MakeToolIconBtn = (GlyphName, Tip, OnClick) =>
    {
        const B = Construct('button', 'pz-icon-btn', [ConstructGlyph(GlyphName)]);
        B.type = 'button';
        B.title = Tip;
        B.addEventListener('click', OnClick);
        return B;
    };

    // Frontier Editor Outliner Section Header (NO duplicate Add Layer button — '+' lives in Outliner header!)
    Container.append(Construct('div', 'pz-outliner-sec-head', [
        Construct('span', null, `LAYERS (${String(Doc.importanceGroups.length).padStart(2, '0')})`),
        Construct('div', 'pz-prio-tools', [
            MakeToolIconBtn('StackUv', 'Stack duplicate/similar UV islands onto one shared footprint', RunStackSimilarOp),
            MakeToolIconBtn('UnstackUv', 'Unstack overlapping duplicate UV islands', RunUnstackOp),
            MakeToolIconBtn('Reset', 'Unassign selected faces from layer', () => AssignSelectedToGroup(null))
        ])
    ]));

    const StackWrap = Construct('div', 'pz-layer-stack pz-frontier-outliner-boxes');

    for (let LayerIdx = 0; LayerIdx < Doc.importanceGroups.length; LayerIdx++)
    {
        const Grp = Doc.importanceGroups[LayerIdx];
        if (Query && !Grp.name.toLowerCase().includes(Query)) continue;

        const SavedRef = Grp.faceRef || Doc.surface.GetLayerFaceRef(Grp.id, Doc.islands, FilterUdimNum);
        const AssignedUdim = Grp.udimTarget && Grp.udimTarget !== 'auto' ? Number(Grp.udimTarget) : null;
        if (FilterUdimNum !== null && SavedRef.faceCount === 0 && AssignedUdim !== FilterUdimNum && Doc.importanceGroups.length > 1) continue;

        const IsSel = (Doc.activeLayerId === Grp.id) || IsFaceGroupRefSelected(Doc.surface, SavedRef);
        const UdimTag = Grp.udimTarget && Grp.udimTarget !== 'auto'
            ? `UDIM ${Grp.udimTarget}`
            : (SavedRef.udims.length === 1 ? `UDIM ${SavedRef.udims[0]}` : 'Auto UDIM');

        // Sharp unrounded (0px) colour-coded rectangular box matching Frontier Editor OutlinerPanel
        const LayerBox = Construct('div', `pz-uv-layer-card pz-frontier-layer-box${IsSel ? ' selected' : ''}`);
        LayerBox.dataset.layerId = Grp.id;
        LayerBox.style.setProperty('--layer-color', Grp.color);

        const ColorInput = Construct('input', 'pz-layer-color-native');
        ColorInput.type = 'color';
        ColorInput.value = /^#[0-9a-fA-F]{6}$/.test(Grp.color || '') ? Grp.color : '#eff4cf';
        const ColorSquare = Construct('label', 'pz-layer-sq-swatch', [ColorInput]);
        ColorSquare.style.background = ColorInput.value;
        ColorSquare.title = 'Click to pick layer colour';
        ColorSquare.addEventListener('click', E => E.stopPropagation());
        ColorInput.addEventListener('input', () =>
        {
            Grp.color = ColorInput.value;
            ColorSquare.style.background = Grp.color;
            LayerBox.style.setProperty('--layer-color', Grp.color);
            RefreshLayerBudgetLive(Doc);
            if (Viewport3D && Viewport3D.shadingMode === 'importance')
            {
                Viewport3D._lastMeshTopoSig = null;
                Viewport3D.SyncFromSurface(Doc.surface, Doc.islands, Doc.importanceGroups, StudioState.targetTexelDensity);
            }
            ScheduleFast2DRender(Doc);
            if (StudioState.inspectorTab === 'packing') RebuildInspector();
        });
        ColorInput.addEventListener('change', () => { _lastOutlinerSig = null; });

        const TitleEl = Construct('strong', 'pz-layer-box-title', Grp.name);
        TitleEl.dataset.layerTitle = Grp.id;
        const SubEl = Construct('small', 'pz-layer-box-sub', `${UdimTag} · ${SavedRef.faceCount.toLocaleString()} faces`);
        SubEl.dataset.layerSub = Grp.id;
        const CopyCol = Construct('div', 'pz-layer-box-copy', [TitleEl, SubEl]);

        const PctBadge = Construct('span', 'pz-layer-pct-box', `${Math.round(Grp.targetPercent)}%`);
        PctBadge.dataset.layerPct = Grp.id;

        const AssignBtn = Construct('button', 'pz-prio-act', ConstructGlyph('Plus'));
        AssignBtn.type = 'button';
        AssignBtn.title = `Bind currently selected faces into "${Grp.name}"`;
        AssignBtn.addEventListener('click', E =>
        {
            E.stopPropagation();
            Doc.activeLayerId = Grp.id;
            AssignSelectedToGroup(Grp.id);
        });

        const DelBtn = Construct('button', 'pz-prio-act danger', ConstructGlyph('Close'));
        DelBtn.type = 'button';
        DelBtn.title = `Delete "${Grp.name}"`;
        DelBtn.addEventListener('click', E =>
        {
            E.stopPropagation();
            RemoveImportanceGroup(Grp.id);
        });

        // Direct O(|faces|) selection from saved FaceGroupRef + focus in Pack Inspector!
        LayerBox.addEventListener('click', E =>
        {
            Doc.activeLayerId = Grp.id;
            const Ref = Doc.surface.GetLayerFaceRef(Grp.id, Doc.islands, FilterUdimNum);
            Doc.surface.SelectFaceGroupRef(Ref, E.shiftKey || E.ctrlKey || E.metaKey);
            FastSyncSelectionViews(Doc, true);
        });
        LayerBox.addEventListener('dblclick', E =>
        {
            if (E.target.closest('input, button, label')) return;
            Doc.activeLayerId = Grp.id;
            const Ref = Doc.surface.GetLayerFaceRef(Grp.id, Doc.islands, FilterUdimNum);
            Doc.surface.SelectFaceGroupRef(Ref, false);
            FrameFaceGroupRefBounds(Doc, Ref);
            FastSyncSelectionViews(Doc, true);
        });

        LayerBox.append(ColorSquare, CopyCol, PctBadge, AssignBtn, DelBtn);
        StackWrap.append(LayerBox);
    }

    // Remainder Box (unrounded Frontier Editor box when unassigned faces remain or shares < 100%)
    const ExplicitSum = Doc.importanceGroups.reduce((S, G) => S + G.targetPercent, 0);
    const RemTarget = Math.max(0, 100 - ExplicitSum);
    const RemRef = Doc.surface.GetLayerFaceRef(null, Doc.islands, FilterUdimNum);

    if (RemRef.faceCount > 0 || Doc.importanceGroups.length === 0 || ExplicitSum < 100)
    {
        const RemPctBadge = Construct('span', `pz-layer-pct-box${ExplicitSum > 100 ? ' warn' : ''}`,
            ExplicitSum > 100 ? `${ExplicitSum.toFixed(0)}% !` : `${RemTarget.toFixed(0)}%`);
        RemPctBadge.dataset.layerPct = '__unassigned__';

        const RemSel = IsFaceGroupRefSelected(Doc.surface, RemRef);
        const RemSquare = Construct('span', 'pz-layer-sq-swatch');
        RemSquare.style.background = '#6e6e6e';

        const BaseBox = Construct('div', `pz-uv-layer-card pz-frontier-layer-box base-layer${RemSel ? ' selected' : ''}`, [
            RemSquare,
            Construct('div', 'pz-layer-box-copy', [
                Construct('strong', 'pz-layer-box-title', 'Unassigned Remainder'),
                Construct('small', 'pz-layer-box-sub', `${RemRef.faceCount.toLocaleString()} faces`)
            ]),
            RemPctBadge
        ]);
        BaseBox.dataset.layerId = '__unassigned__';
        BaseBox.style.setProperty('--layer-color', '#6e6e6e');
        BaseBox.title = `Click to select ${RemRef.faceCount.toLocaleString()} unassigned faces instantaneously`;
        BaseBox.addEventListener('click', E =>
        {
            const Ref = Doc.surface.GetLayerFaceRef(null, Doc.islands, FilterUdimNum);
            if (Ref.faceCount === 0) return;
            Doc.surface.SelectFaceGroupRef(Ref, E.shiftKey || E.ctrlKey || E.metaKey);
            FastSyncSelectionViews(Doc, true);
        });
        BaseBox.addEventListener('dblclick', () =>
        {
            const Ref = Doc.surface.GetLayerFaceRef(null, Doc.islands, FilterUdimNum);
            if (Ref.faceCount === 0) return;
            Doc.surface.SelectFaceGroupRef(Ref, false);
            FrameFaceGroupRefBounds(Doc, Ref);
            FastSyncSelectionViews(Doc, true);
        });
        StackWrap.append(BaseBox);
    }

    Container.append(StackWrap);
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
        const Count = Doc.surface.GetLayerFaceRef(Grp.id, Doc.islands).islandCount;
        Strip.append(Construct('span', 'legend-pill', [Dot, `${Grp.name} · ${Grp.targetPercent}% (${Count})`]));
    }
    const RemCount = Doc.surface.GetLayerFaceRef(null, Doc.islands).islandCount;
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

function ResolveSelectedIslandIds(Doc)
{
    if (!Doc) return [];
    if (Doc.surface.selectedIslands.size > 0) return [...Doc.surface.selectedIslands];
    if (Doc.surface.selectedFaces.size > 0)
    {
        const Out = [];
        for (const Isl of Doc.islands)
        {
            if (Isl.faceIndices.some(Fi => Doc.surface.selectedFaces.has(Fi))) Out.push(Isl.id);
        }
        return Out;
    }
    return [];
}

function AssignSelectedIslandsToNewGroup(Pct = StudioState.pendingGroupPct, CustomName = '')
{
    const Doc = ActiveDoc();
    if (!Doc) return;
    const TargetFaces = Doc.surface.GetSelectedTargetFaces(Doc.islands);
    Doc.surface.PushUndo('Create UV Layer');
    const NextIdx = Doc.importanceGroups.length;
    const Color = GroupColorPalette[NextIdx % GroupColorPalette.length];
    const DefaultShare = Doc.importanceGroups.length === 0 ? 100 : Math.max(0, Math.min(100, Number(Pct) || 25));
    const NewGroup = {
        id: `grp-${Date.now().toString(36)}`,
        name: CustomName || `Layer ${NextIdx + 1}`,
        targetPercent: DefaultShare,
        color: Color,
        udimTarget: 'auto'
    };
    Doc.importanceGroups.push(NewGroup);
    Doc.activeLayerId = NewGroup.id;
    if (TargetFaces.size > 0)
    {
        Doc.surface.AssignFacesToLayer(TargetFaces, NewGroup.id);
        LogEvent('BUDGET', `Saved ${TargetFaces.size} face(s) into "${NewGroup.name}" (${NewGroup.targetPercent}%)`, 'warm');
        Toast(`${TargetFaces.size} face(s) -> "${NewGroup.name}" (${NewGroup.targetPercent}%)`);
    }
    else
    {
        Toast(`Added "${NewGroup.name}" (${NewGroup.targetPercent}%) — select faces/islands and click +`);
    }
    _lastOutlinerSig = null;
    _lastBudgetCardSig = null;
    SyncAllViews(true);
}

function RemoveImportanceGroup(GroupId)
{
    const Doc = ActiveDoc();
    if (!Doc) return;
    const Grp = Doc.importanceGroups.find(G => G.id === GroupId);
    if (!Grp) return;
    const SavedRef = Doc.surface.GetLayerFaceRef(GroupId, Doc.islands, null);
    Doc.surface.PushUndo('Delete UV Layer');
    if (SavedRef.faceCount > 0) Doc.surface.AssignFacesToLayer(SavedRef.faceIndices, null);
    Doc.importanceGroups = Doc.importanceGroups.filter(G => G.id !== GroupId);
    if (Doc.activeLayerId === GroupId) Doc.activeLayerId = Doc.importanceGroups[0]?.id || null;
    _lastOutlinerSig = null;
    _lastBudgetCardSig = null;
    Toast(`Removed "${Grp.name}"`);
    SyncAllViews(true);
}

function AssignSelectedToGroup(GroupId)
{
    const Doc = ActiveDoc();
    if (!Doc) return;
    const TargetFaces = Doc.surface.GetSelectedTargetFaces(Doc.islands);
    if (!TargetFaces.size) { Toast('Select faces or islands first'); return; }
    Doc.surface.PushUndo('Assign UV Layer Faces');
    Doc.surface.AssignFacesToLayer(TargetFaces, GroupId);
    const Grp = Doc.importanceGroups.find(G => G.id === GroupId);
    _lastOutlinerSig = null;
    _lastBudgetCardSig = null;
    LogEvent('BUDGET', `Saved ${TargetFaces.size} face(s) into ${Grp ? Grp.name : 'Unassigned Remainder'}`, 'warm');
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

let CtxMatcapStripBuilt = false;
function EnsureContextMatcapStripBuilt()
{
    if (CtxMatcapStripBuilt) return;
    const CtxMatcapStrip = document.getElementById('ctx-matcap-strip');
    if (!CtxMatcapStrip) return;
    CtxMatcapStripBuilt = true;
    for (const Spec of MATCAP_STUDIO_CATALOG)
    {
        const Img = Construct('img');
        Img.src = GetCachedMatcapThumb(Spec.slot);
        Img.alt = Spec.label;
        const ShortName = Spec.short || Spec.label.split(' ')[0];
        const Btn = Construct('button', 'ctx-matcap-btn', [Img, Construct('span', null, ShortName)]);
        Btn.type = 'button';
        Btn.title = `Assign "${Spec.label}" to selected faces`;
        Btn.addEventListener('click', () =>
        {
            CloseUvContextMenu();
            const Doc = ActiveDoc();
            if (!Doc) return;
            const SelFaces = Doc.surface.GetSelectedTargetFaces(Doc.islands);
            if (SelFaces.size === 0)
            {
                Toast('Select faces or islands first');
                return;
            }
            RunWithUndo(`Assign MatCap: ${Spec.label}`, () =>
            {
                const Changed = Doc.surface.AssignSurfaceMaterialToFaces(SelFaces, { matcapSlot: Spec.slot });
                Toast(`Assigned "${Spec.label}" (${Changed} selected faces)`);
            });
        });
        CtxMatcapStrip.append(Btn);
    }
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

    const CtxMatcapSec = document.getElementById('ctx-matcap-section');
    if (CtxMatcapSec)
    {
        const CurShade = Viewport3D ? Viewport3D.shadingMode : 'fast-pbr';
        const ShowCtxMatcaps = ['multi-matcap', 'lit-matcap', 'matcap', 'matcap-satin'].includes(CurShade);
        CtxMatcapSec.hidden = !ShowCtxMatcaps;
        if (ShowCtxMatcaps) EnsureContextMatcapStripBuilt();
    }

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
    else if (OpId === 'rotate-90') RunWithUndo('Rotate 90°', () => { const P = ComputeSelectionPivot(Doc) || StudioState.cursorUV; Doc.surface.TransformSelectedUVs({ pivot: P, angle: Math.PI / 2 }); });
    else if (OpId === 'follow-quads') RunUnwrap('follow-quads');
    else if (OpId === 'stack-similar') RunStackSimilarOp();
    else if (OpId === 'open-edge-menu') OpenEdgeSeamMenu(LastPointerClient[0], LastPointerClient[1]);
}

function RunStackSimilarOp()
{
    const Doc = ActiveDoc();
    if (!Doc) return;
    RunWithUndo('Stack Similar UV Islands', () =>
    {
        const Res = StackSimilarIslands(Doc.surface, { islands: Doc.islands, tolerance: StudioState.selectSimilarTol || 0.08 });
        if (!Res.stackedIslands)
        {
            Toast('No duplicate/similar UV charts found in current scope');
            return;
        }
        LogEvent('STACK', `Stacked ${Res.stackedIslands} similar UV island(s) onto ${Res.masterGroups} master chart(s)`, 'live');
        Toast(`Stacked ${Res.stackedIslands} similar UV chart(s) onto ${Res.masterGroups} master(s)`);
    });
}

function RunUnstackOp()
{
    const Doc = ActiveDoc();
    if (!Doc) return;
    RunWithUndo('Unstack UV Islands', () =>
    {
        const Moved = UnstackOverlappingIslands(Doc.surface, { islands: Doc.islands });
        if (!Moved) { Toast('No stacked duplicate UV charts detected'); return; }
        LogEvent('STACK', `Unstacked ${Moved} overlapping duplicate UV chart(s)`, 'live');
        Toast(`Spread ${Moved} stacked UV chart(s)`);
    });
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
            const Count = FollowActiveQuads(Doc.surface, 'length-avg', Doc.islands);
            if (!Count)
            {
                Toast('Select quad faces/charts first (pure triangle meshes have 0 quad faces)');
                return;
            }
            LogEvent('UNWRAP', `Follow Active Quads aligned ${Count} quad face(s)`, 'live');
            Toast(`Follow Active Quads: ${Count} faces aligned`);
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
        shading: 'Inspector / Surface & Multi-MatCap',
        packing: 'Inspector / Importance Packing',
        unwrap: 'Inspector / Unwrap & Tools',
        udim: 'Inspector / Spatial UDIM Matrix',
        check: 'Inspector / Checks, Studio & Flat Debug'
    }[StudioState.inspectorTab] || 'Inspector / UV Workspace';
    document.getElementById('inspector-crumb').textContent = Crumb;
    const ActiveLbl = document.getElementById('pz-active-surface-label');
    if (ActiveLbl) ActiveLbl.textContent = Doc.name;

    if (StudioState.inspectorTab === 'shading') RenderShadingTab(Body, Doc);
    else if (StudioState.inspectorTab === 'packing') RenderPackingTab(Body, Doc);
    else if (StudioState.inspectorTab === 'unwrap') RenderUnwrapTab(Body, Doc);
    else if (StudioState.inspectorTab === 'udim') RenderUdimTab(Body, Doc);
    else RenderDiagnosticsTab(Body, Doc);

    CloseScope(Scope);
    Broadcast();
}

const MatcapThumbCache = new Map();
function GetCachedMatcapThumb(Slot)
{
    if (!MatcapThumbCache.has(Slot)) MatcapThumbCache.set(Slot, BuildMatcapPreviewDataUrl(Slot, 72));
    return MatcapThumbCache.get(Slot);
}

function RenderUvSelectionTransformCard(Parent, Doc)
{
    const Stats = ComputeSelectionUvTransformStats(Doc);
    const ScopeLabel = Stats.hasExplicitSel ? `UV SPACE · ${Stats.count} UVS` : `UV SPACE · ALL`;

    const MakeScrubInput = (InitVal, Step, Digits, ApplyFn) =>
    {
        const Inp = Construct('input');
        Inp.type = 'number';
        Inp.step = String(Step);
        Inp.value = Number(InitVal).toFixed(Digits);
        Inp.addEventListener('change', () =>
        {
            const Next = Number(Inp.value);
            if (Number.isFinite(Next)) ApplyFn(Next);
        });
        let Drag = null;
        Inp.addEventListener('pointerdown', E =>
        {
            if (E.button !== 0 || document.activeElement === Inp) return;
            Drag = { x: E.clientX, start: Number(Inp.value) || 0, moved: false };
            Inp.setPointerCapture(E.pointerId);
        });
        Inp.addEventListener('pointermove', E =>
        {
            if (!Drag) return;
            const Dx = E.clientX - Drag.x;
            if (!Drag.moved && Math.abs(Dx) < 3) return;
            if (!Drag.moved) { Drag.moved = true; Doc.surface.PushUndo('Transform UV Selection'); }
            const Mult = E.shiftKey ? 0.15 : 1.0;
            const Next = Drag.start + Dx * Step * Mult;
            Inp.value = Next.toFixed(Digits);
            ApplyFn(Next, true);
        });
        const Stop = () =>
        {
            if (Drag && Drag.moved) SyncAllViews(true);
            Drag = null;
        };
        Inp.addEventListener('pointerup', Stop);
        Inp.addEventListener('pointercancel', Stop);
        return Inp;
    };

    const TranslateTo = (TargetAxis, TargetVal, LiveOnly = false) =>
    {
        const Cur = ComputeSelectionUvTransformStats(Doc);
        const Delta = TargetVal - (TargetAxis === 0 ? Cur.cx : Cur.cy);
        if (Math.abs(Delta) < 1e-6) return;
        const Action = () =>
        {
            for (const Idx of CollectTargetUvIndices(Doc))
            {
                Doc.surface.uvCoords[Idx][TargetAxis] += Delta;
            }
            Doc.surface.MarkUvDirty?.();
        };
        if (LiveOnly) { Action(); FastPaintVisualSync(Doc); if (Viewport3D) Viewport3D.SyncFromSurface(Doc.surface, Doc.islands, Doc.importanceGroups, StudioState.targetTexelDensity); }
        else RunWithUndo('Move UV Selection', Action);
    };

    let LastRotDeg = 0;
    const RotateDelta = (NextDeg, LiveOnly = false) =>
    {
        const DeltaDeg = NextDeg - LastRotDeg;
        if (Math.abs(DeltaDeg) < 1e-4) return;
        LastRotDeg = NextDeg;
        const Rad = (DeltaDeg * Math.PI) / 180.0;
        const Cos = Math.cos(Rad), Sin = Math.sin(Rad);
        const Cur = ComputeSelectionUvTransformStats(Doc);
        const Action = () =>
        {
            for (const Idx of CollectTargetUvIndices(Doc))
            {
                const P = Doc.surface.uvCoords[Idx];
                const Du = P[0] - Cur.cx, Dv = P[1] - Cur.cy;
                P[0] = Cur.cx + Du * Cos - Dv * Sin;
                P[1] = Cur.cy + Du * Sin + Dv * Cos;
            }
            Doc.surface.MarkUvDirty?.();
        };
        if (LiveOnly) { Action(); FastPaintVisualSync(Doc); }
        else RunWithUndo('Rotate UV Selection', Action);
    };

    const ScaleAxis = (Axis, NextExtent, LiveOnly = false) =>
    {
        const Cur = ComputeSelectionUvTransformStats(Doc);
        const CurSpan = Axis === 0 ? Cur.du : Axis === 1 ? Cur.dv : Math.max(Cur.du, Cur.dv);
        const Factor = Math.max(0.02, Math.min(20.0, NextExtent / Math.max(1e-4, CurSpan)));
        if (Math.abs(Factor - 1.0) < 1e-4) return;
        const Action = () =>
        {
            for (const Idx of CollectTargetUvIndices(Doc))
            {
                const P = Doc.surface.uvCoords[Idx];
                if (Axis === 0 || Axis === 2) P[0] = Cur.cx + (P[0] - Cur.cx) * Factor;
                if (Axis === 1 || Axis === 2) P[1] = Cur.cy + (P[1] - Cur.cy) * Factor;
            }
            Doc.surface.MarkUvDirty?.();
        };
        if (LiveOnly) { Action(); FastPaintVisualSync(Doc); }
        else RunWithUndo('Scale UV Selection', Action);
    };

    const PosUIn = MakeScrubInput(Stats.cx, 0.005, 3, (V, Live) => TranslateTo(0, V, Live));
    const PosVIn = MakeScrubInput(Stats.cy, 0.005, 3, (V, Live) => TranslateTo(1, V, Live));
    const UdimIn = Construct('input');
    UdimIn.type = 'number';
    UdimIn.step = '1';
    UdimIn.value = String(Stats.udim);
    UdimIn.title = 'Target UDIM Tile (1001+)';
    UdimIn.addEventListener('change', () =>
    {
        const TargetTile = Math.max(1001, Math.round(Number(UdimIn.value) || 1001));
        const CurOrigin = UdimOrigin(Stats.udim);
        const NextOrigin = UdimOrigin(TargetTile);
        const Du = NextOrigin.u - CurOrigin.u, Dv = NextOrigin.v - CurOrigin.v;
        if (Du === 0 && Dv === 0) return;
        RunWithUndo(`Move Selection to UDIM ${TargetTile}`, () =>
        {
            for (const Idx of CollectTargetUvIndices(Doc))
            {
                Doc.surface.uvCoords[Idx][0] += Du;
                Doc.surface.uvCoords[Idx][1] += Dv;
            }
            Doc.activeUdim = TargetTile;
        });
    });

    const RotIn = MakeScrubInput(0, 1, 1, (V, Live) => RotateDelta(V, Live));
    const SpanURead = Construct('input');
    SpanURead.type = 'text';
    SpanURead.readOnly = true;
    SpanURead.value = `${(Doc.diagnostics?.meanAngleStretch * 100 || 0).toFixed(1)}%`;
    SpanURead.title = 'Mean Angle Stretch';
    const PinsRead = Construct('input');
    PinsRead.type = 'text';
    PinsRead.readOnly = true;
    PinsRead.value = `${Doc.surface.pinned.size}`;
    PinsRead.title = 'Pinned UV Vertices';

    const SizeUIn = MakeScrubInput(Stats.du, 0.01, 3, (V, Live) => ScaleAxis(0, Math.max(0.01, V), Live));
    const SizeVIn = MakeScrubInput(Stats.dv, 0.01, 3, (V, Live) => ScaleAxis(1, Math.max(0.01, V), Live));
    const SizeUniIn = MakeScrubInput(Math.max(Stats.du, Stats.dv), 0.01, 3, (V, Live) => ScaleAxis(2, Math.max(0.01, V), Live));

    const Table = Construct('table', 'pz-transform-table', [
        Construct('thead', null, [
            Construct('tr', null, [
                Construct('th', null, 'COMPONENT'),
                Construct('th', 'axis-U', 'U'),
                Construct('th', 'axis-V', 'V'),
                Construct('th', 'axis-W', 'UDIM')
            ])
        ]),
        Construct('tbody', null, [
            Construct('tr', null, [
                Construct('th', null, ['Position', Construct('small', null, 'uv')]),
                Construct('td', null, [PosUIn]),
                Construct('td', null, [PosVIn]),
                Construct('td', null, [UdimIn])
            ]),
            Construct('tr', null, [
                Construct('th', null, ['Extent', Construct('small', null, 'size ×')]),
                Construct('td', null, [SizeUIn]),
                Construct('td', null, [SizeVIn]),
                Construct('td', null, [SizeUniIn])
            ]),
            Construct('tr', null, [
                Construct('th', null, ['Orient', Construct('small', null, 'deg / str')]),
                Construct('td', null, [RotIn]),
                Construct('td', null, [SpanURead]),
                Construct('td', null, [PinsRead])
            ])
        ])
    ]);

    const CenterBtn = Construct('button', null, 'Center Tile');
    CenterBtn.type = 'button';
    CenterBtn.addEventListener('click', () =>
    {
        const Cur = ComputeSelectionUvTransformStats(Doc);
        const Orig = UdimOrigin(Cur.udim);
        RunWithUndo('Center in UDIM Tile', () =>
        {
            const Du = (Orig.u + 0.5) - Cur.cx, Dv = (Orig.v + 0.5) - Cur.cy;
            for (const Idx of CollectTargetUvIndices(Doc))
            {
                Doc.surface.uvCoords[Idx][0] += Du;
                Doc.surface.uvCoords[Idx][1] += Dv;
            }
        });
    });

    const Rot90Btn = Construct('button', null, 'Rotate 90°');
    Rot90Btn.type = 'button';
    Rot90Btn.addEventListener('click', () => RotateDelta(LastRotDeg + 90, false));

    const FitBtn = Construct('button', null, 'Frame UV');
    FitBtn.type = 'button';
    FitBtn.addEventListener('click', () =>
    {
        const Cur = ComputeSelectionUvTransformStats(Doc);
        Canvas2D.FrameBounds(Cur.cx - Cur.du * 0.5, Cur.cy - Cur.dv * 0.5, Cur.cx + Cur.du * 0.5, Cur.cy + Cur.dv * 0.5, 0.22);
        SyncAllViews();
    });

    const Card = Construct('section', 'pz-transform-card', [
        Construct('header', null, [
            Construct('h3', null, 'Transform · 2D UV'),
            Construct('span', null, ScopeLabel)
        ]),
        Table,
        Construct('div', 'pz-transform-resets', [CenterBtn, Rot90Btn, FitBtn])
    ]);
    Parent.append(Card);
}

function IconStripBlock(Parent, { Label, Meta = '', Items = [] })
{
    const Wrap = Construct('div', 'pz-icon-strip-block');
    if (Label)
    {
        Wrap.append(Construct('div', 'pz-icon-strip-head', [
            Construct('span', null, Label),
            Meta ? Construct('span', null, Meta) : null
        ]));
    }
    const Strip = Construct('div', 'pz-icon-strip');
    for (const Item of Items)
    {
        if (Item === '|')
        {
            Strip.append(Construct('span', 'pz-icon-sep'));
            continue;
        }
        const ToneCls = Item.Tone ? ` ${Item.Tone}` : '';
        const Parts = [ConstructGlyph(Item.Glyph)];
        if (Item.Short) Parts.push(Construct('span', null, Item.Short));
        const Btn = Construct('button', `pz-icon-btn${ToneCls}`, Parts);
        Btn.type = 'button';
        if (Item.Title) Btn.title = Item.Title;
        Btn.addEventListener('click', Item.Act);
        Strip.append(Btn);
    }
    Wrap.append(Strip);
    Parent.append(Wrap);
    return Wrap;
}

function DescribeHexColorName(HexStr)
{
    const Clean = String(HexStr || '#4c7dc9').replace('#', '').toLowerCase();
    const Known = {
        '4c7dc9': 'Steel Blue',
        'c4ccd8': 'Machined Alu',
        'c7bdb0': 'Titanium',
        '363b44': 'Gunmetal',
        'd9a448': 'Turned Brass',
        '7b9478': 'CAD Sage',
        'eff4cf': 'Studio Ivory'
    };
    if (Known[Clean]) return Known[Clean];
    const R = parseInt(Clean.slice(0, 2), 16) / 255;
    const G = parseInt(Clean.slice(2, 4), 16) / 255;
    const B = parseInt(Clean.slice(4, 6), 16) / 255;
    const Max = Math.max(R, G, B), Min = Math.min(R, G, B);
    const L = (Max + Min) * 0.5;
    const D = Max - Min;
    if (D < 0.08)
    {
        if (L < 0.16) return 'Obsidian';
        if (L < 0.36) return 'Charcoal';
        if (L < 0.68) return 'Neutral Grey';
        if (L < 0.88) return 'Silver';
        return 'Ivory White';
    }
    let H = 0;
    if (Max === R) H = ((G - B) / D + (G < B ? 6 : 0)) * 60;
    else if (Max === G) H = ((B - R) / D + 2) * 60;
    else H = ((R - G) / D + 4) * 60;
    if (H < 18 || H >= 345) return 'Crimson';
    if (H < 42) return 'Copper';
    if (H < 66) return 'Warm Gold';
    if (H < 155) return 'Sage Green';
    if (H < 195) return 'Teal';
    if (H < 250) return 'Cobalt Blue';
    if (H < 295) return 'Violet';
    return 'Magenta';
}

function RenderShadingTab(Body, Doc)
{
    const Sel = ComputeInspectorSelectionInfo(Doc);
    const Counts = Sel.matcapCounts;
    const DistinctMatcaps = Sel.distinctSlots;

    // Auto-inspect selected face's per-face PBR + MatCap whenever a new selection is made
    const SelKey = Sel.hasSelection ? `${Doc.id}:${Sel.targetFaces[0]}:${Sel.faceCount}` : null;
    if (SelKey && StudioState._lastSampledSelKey !== SelKey)
    {
        StudioState._lastSampledSelKey = SelKey;
        const RefF = Doc.surface.faces[Sel.targetFaces[0]];
        if (RefF)
        {
            StudioState.materialBrush.matcapSlot = (RefF.matcapSlot || 0) & 15;
            StudioState.materialBrush.pbrColor = (RefF.pbrColor || '#4c7dc9').toLowerCase();
            StudioState.materialBrush.pbrMetallic = RefF.pbrMetallic ?? 0.60;
            StudioState.materialBrush.pbrRoughness = RefF.pbrRoughness ?? 0.24;
            StudioState.materialBrush.pbrSpecular = RefF.pbrSpecular ?? 0.68;
            StudioState.materialBrush.matcapMix = RefF.matcapMix ?? 0.62;
        }
    }
    else if (!SelKey)
    {
        StudioState._lastSampledSelKey = null;
    }

    const CurMode = Viewport3D ? Viewport3D.shadingMode : 'fast-pbr';
    const IsMatcapMode = ['multi-matcap', 'lit-matcap', 'matcap', 'matcap-satin'].includes(CurMode);
    const IsPbrMode = ['fast-pbr', 'lit-matcap'].includes(CurMode);
    const ModePretty = {
        'fast-pbr': 'Surface Material',
        'multi-matcap': 'MatCap (Per-Face)',
        'lit-matcap': 'Material + MatCap',
        'lit-eon': 'Studio Clay',
        flat: 'Flat · UV Charts',
        'flat-slots': 'Flat · Face Slots',
        checker: 'Flat · UV Checker',
        matcap: 'CNC Aluminum',
        'matcap-satin': 'Grade-5 Titanium',
        importance: 'Flat · Priority',
        texel: 'Flat · Density',
        stretch: 'Flat · Stretch',
        overlap: 'Flat · Overlaps'
    }[CurMode] || CurMode;

    // 1A. Mode-Aware Visual Studio Hero Banner (MatCap spheres shown ONLY when a MatCap mode is active!)
    const SlotBarColors = [
        '#9fb2c8', '#b8b3aa', '#a8a29a', '#dbeafe', '#4b5563', '#3b82f6', '#d9a441', '#34d399',
        '#2e323b', '#818cf8', '#2563eb', '#e11d48', '#475569', '#64748b', '#9333ea', '#4d7c5f'
    ];
    const TotalFaces = Math.max(1, Sel.faceCount);
    let HeroBanner = null;
    if (IsMatcapMode)
    {
        const ActiveOrbImgs = [];
        for (const Spec of MATCAP_STUDIO_CATALOG)
        {
            if (Counts[Spec.slot] > 0)
            {
                const Im = Construct('img');
                Im.src = GetCachedMatcapThumb(Spec.slot);
                Im.alt = Spec.label;
                ActiveOrbImgs.push(Im);
                if (ActiveOrbImgs.length >= 5) break;
            }
        }
        const BarSpans = [];
        for (let s = 0; s < MATCAP_STUDIO_CATALOG.length; s++)
        {
            if (Counts[s] <= 0) continue;
            const Seg = Construct('span');
            Seg.style.width = `${Math.max(3.5, (Counts[s] / TotalFaces) * 100).toFixed(1)}%`;
            Seg.style.background = SlotBarColors[s % SlotBarColors.length];
            BarSpans.push(Seg);
        }
        HeroBanner = Construct('div', 'pz-shading-hero', [
            Construct('div', 'pz-hero-top', [
                Construct('div', null, [
                    Construct('span', 'pz-hero-eyebrow', Sel.hasSelection ? `SELECTED · ${Sel.title}` : ModePretty),
                    Construct('div', 'pz-hero-big', [
                        document.createTextNode(String(DistinctMatcaps)),
                        Construct('small', null, Sel.hasSelection ? 'MatCap(s) on Sel' : `/ ${MATCAP_STUDIO_CATALOG.length} MatCaps`)
                    ])
                ]),
                Construct('div', 'pz-hero-orb-stack', ActiveOrbImgs)
            ]),
            Construct('div', 'pz-hero-bar', BarSpans),
            Construct('div', 'pz-hero-foot', [
                Construct('span', null, Sel.scopeTag),
                Construct('span', null, `${Sel.area3D.toFixed(2)} m² · ${Sel.uvCount} UVs`)
            ])
        ]);
    }
    else
    {
        const PbrBarSeg = Construct('span');
        PbrBarSeg.style.width = '100%';
        PbrBarSeg.style.background = StudioState.materialBrush.pbrColor || '#4c7dc9';
        HeroBanner = Construct('div', 'pz-shading-hero', [
            Construct('div', 'pz-hero-top', [
                Construct('div', null, [
                    Construct('span', 'pz-hero-eyebrow', Sel.hasSelection ? `SELECTED · ${Sel.title}` : 'VIEWPORT SHADER'),
                    Construct('div', 'pz-hero-big', [
                        document.createTextNode(ModePretty),
                        Construct('small', null, `${Doc.islands.length} charts`)
                    ])
                ])
            ]),
            Construct('div', 'pz-hero-bar', [PbrBarSeg]),
            Construct('div', 'pz-hero-foot', [
                Construct('span', null, Sel.scopeTag),
                Construct('span', null, `${Sel.area3D.toFixed(2)} m² · ${Sel.uvCount} UVs`)
            ])
        ]);
    }
    Body.append(HeroBanner);

    // 1B. Infographic Bento Tiles (Preserved design, driven dynamically by Selected Object / Surface!)
    const TotalIssues = Sel.overlapCount + Sel.flippedCount;
    const BentoGrid = Construct('div', 'pz-bento-grid', [
        Construct('div', 'pz-bento-tile', [
            Construct('span', `pz-bento-icon ${TotalIssues > 0 ? 'coral' : 'emerald'}`, TotalIssues > 0 ? '!' : '✓'),
            Construct('span', 'pz-bento-label', TotalIssues > 0 ? 'Overlaps' : 'UV Health'),
            Construct('div', 'pz-bento-num', [
                document.createTextNode(String(TotalIssues)),
                Construct('small', null, TotalIssues > 0 ? 'err' : 'ok')
            ])
        ]),
        Construct('div', 'pz-bento-tile', [
            Construct('span', 'pz-bento-icon amber', 'α'),
            Construct('span', 'pz-bento-label', 'Stretch'),
            Construct('div', 'pz-bento-num', [
                document.createTextNode(Sel.meanStretchPct.toFixed(1)),
                Construct('small', null, '%')
            ])
        ]),
        Construct('div', 'pz-bento-tile', [
            Construct('span', 'pz-bento-icon cobalt', '✦'),
            Construct('span', 'pz-bento-label', 'Density'),
            Construct('div', 'pz-bento-num', [
                document.createTextNode(Sel.avgDensity.toFixed(1)),
                Construct('small', null, 'px')
            ])
        ]),
        Construct('div', 'pz-bento-tile', [
            Construct('span', 'pz-bento-icon emerald', '▣'),
            Construct('span', 'pz-bento-label', Sel.hasSelection ? 'Selected' : 'Surface'),
            Construct('div', 'pz-bento-num', [
                document.createTextNode(String(Sel.faceCount)),
                Construct('small', null, 'f')
            ])
        ])
    ]);
    Body.append(BentoGrid);

    // 2. Unified Surface Shading Card (MatCaps shown ONLY when a MatCap mode is selected!)
    const Brush = StudioState.materialBrush;
    const CardTitle = CurMode === 'multi-matcap'
        ? 'Per-Face MatCap Studio'
        : (CurMode === 'lit-matcap' ? 'Surface Finish · Material + MatCap' : 'Surface Finish');
    const MatSec = SectionCard(Body, {
        Id: 'unified-surface-shading',
        Title: CardTitle,
        Glyph: 'Sparkles',
        Badge: Sel.hasSelection ? `${Sel.faceCount} SEL` : (IsMatcapMode ? 'MATCAP' : 'SURFACE')
    });

    SelectRow(MatSec.Inner, {
        Label: 'Shader',
        Options: [
            ['Surface Material', 'fast-pbr'],
            ['MatCap (Per-Face)', 'multi-matcap'],
            ['Material + MatCap', 'lit-matcap'],
            ['Studio Clay', 'lit-eon'],
            ['Flat · UV Charts', 'flat'],
            ['Flat · Face Slots', 'flat-slots'],
            ['Flat · UV Checker', 'checker']
        ],
        Read: () => Viewport3D?.shadingMode || 'fast-pbr',
        Write: Val => Set3DShadingMode(Val)
    });

    if (IsMatcapMode || IsPbrMode)
    {
        const SampleBtn = Construct('button', 'pz-mini-pill-btn', 'Sample');
        SampleBtn.type = 'button';
        SampleBtn.title = 'Sample material from active/selected face into the brush';
        SampleBtn.addEventListener('click', () =>
        {
            const SrcIdx = Sel.hasSelection ? Sel.targetFaces[0] : (Doc.surface.activeFace ?? 0);
            const SrcF = Doc.surface.faces[SrcIdx];
            if (!SrcF) return;
            Brush.matcapSlot = (SrcF.matcapSlot || 0) & 15;
            Brush.pbrColor = (SrcF.pbrColor || '#4c7dc9').toLowerCase();
            Brush.pbrMetallic = SrcF.pbrMetallic ?? 0.60;
            Brush.pbrRoughness = SrcF.pbrRoughness ?? 0.24;
            Brush.pbrSpecular = SrcF.pbrSpecular ?? 0.68;
            Brush.matcapMix = SrcF.matcapMix ?? 0.62;
            RebuildInspector();
            Toast(`Sampled material from Face #${SrcIdx}`);
        });

        const AutoPartBtn = Construct('button', 'pz-mini-pill-btn', 'Auto Parts');
        AutoPartBtn.type = 'button';
        AutoPartBtn.title = 'Auto-assign distinct materials across surface parts';
        AutoPartBtn.addEventListener('click', () =>
        {
            RunWithUndo('Auto-Assign Part Materials', () =>
            {
                Doc.surface.AutoAssignMatcapsByPart(Doc.islands);
                Toast(`Auto-assigned part materials across ${Doc.islands.length} charts`);
            });
        });

        MatSec.Inner.append(Construct('div', 'pz-sel-scope-bar', [
            Construct('strong', null, Sel.hasSelection ? `Selected: ${Sel.title} (${Sel.faceCount}f)` : 'No faces selected'),
            Construct('div', 'pz-mat-scope-actions', [SampleBtn, AutoPartBtn])
        ]));

        const BuildActiveModePatch = () =>
        {
            if (CurMode === 'multi-matcap') return { matcapSlot: Brush.matcapSlot };
            if (CurMode === 'fast-pbr')
            {
                return {
                    pbrColor: Brush.pbrColor,
                    pbrMetallic: Brush.pbrMetallic,
                    pbrRoughness: Brush.pbrRoughness,
                    pbrSpecular: Brush.pbrSpecular
                };
            }
            return { ...Brush };
        };

        ActionRow(MatSec.Inner, [
            {
                Label: 'Apply Selected',
                Glyph: 'Check',
                Tone: 'accent',
                Act: () =>
                {
                    const SelFaces = Doc.surface.GetSelectedTargetFaces(Doc.islands);
                    if (SelFaces.size === 0)
                    {
                        Toast('Select faces or islands first — or click Fill All');
                        return;
                    }
                    RunWithUndo('Apply Material to Selected Faces', () =>
                    {
                        const N = Doc.surface.AssignSurfaceMaterialToFaces(SelFaces, BuildActiveModePatch());
                        Toast(`Applied to ${N} selected face(s)`);
                    });
                }
            },
            {
                Label: 'Fill All',
                Glyph: 'Layers',
                Act: () =>
                {
                    RunWithUndo('Fill Material Across Geometry', () =>
                    {
                        const N = Doc.surface.FillSurfaceMaterial(BuildActiveModePatch());
                        Toast(`Filled entire geometry (${N} faces)`);
                    });
                }
            }
        ]);
    }

    const LivePreviewSelectedOnly = Patch =>
    {
        Object.assign(Brush, Patch);
        const SelFaces = Doc.surface.GetSelectedTargetFaces(Doc.islands);
        if (SelFaces.size > 0)
        {
            Doc.surface.AssignSurfaceMaterialToFaces(SelFaces, Patch);
            if (Viewport3D) Viewport3D.FastUpdateGpuFaceMask(Doc.surface, Doc.islands);
            UpdateHeaderAndHud(Doc);
        }
    };

    // Sub-section A: 16-Orb Procedural MatCap Library — SHOWN ONLY WHEN MATCAP MODE IS SELECTED!
    if (IsMatcapMode)
    {
        const ActiveSlotSpec = MATCAP_STUDIO_CATALOG[Brush.matcapSlot & 15] || MATCAP_STUDIO_CATALOG[0];
        const SlotSubName = Construct('span', null, ActiveSlotSpec.label);
        MatSec.Inner.append(Construct('div', 'pz-mat-subhead', [
            document.createTextNode(`MatCap Library (${MATCAP_STUDIO_CATALOG.length})`),
            SlotSubName
        ]));

        const OrbGrid = Construct('div', 'pz-matcap-grid');
        const OrbBtns = [];
        for (const Spec of MATCAP_STUDIO_CATALOG)
        {
            const UsedCount = Counts[Spec.slot];
            const IsActiveOrb = (Brush.matcapSlot & 15) === Spec.slot;
            const OrbImg = Construct('img');
            OrbImg.src = GetCachedMatcapThumb(Spec.slot);
            OrbImg.alt = Spec.label;
            const Children = [OrbImg, Construct('small', null, Spec.label)];
            if (UsedCount > 0) Children.push(Construct('span', 'pz-matcap-count', `${UsedCount}`));
            const Btn = Construct('button', `pz-matcap-orb-btn${IsActiveOrb ? ' active' : ''}`, Children);
            Btn.type = 'button';
            Btn.title = Sel.hasSelection
                ? `Select "${Spec.label}" & apply to ${Sel.faceCount} selected face(s)`
                : `Select "${Spec.label}" in brush (click Apply Selected or Fill All)`;
            Btn.addEventListener('click', () =>
            {
                Brush.matcapSlot = Spec.slot;
                SlotSubName.textContent = Spec.label;
                for (const [Ob, Os] of OrbBtns) Ob.classList.toggle('active', Os === Spec.slot);
                const SelFaces = Doc.surface.GetSelectedTargetFaces(Doc.islands);
                if (SelFaces.size > 0)
                {
                    RunWithUndo(`Assign MatCap: ${Spec.label}`, () =>
                    {
                        const N = Doc.surface.AssignSurfaceMaterialToFaces(SelFaces, { matcapSlot: Spec.slot });
                        Toast(`Assigned "${Spec.label}" to ${N} selected face(s)`);
                    });
                }
            });
            OrbBtns.push([Btn, Spec.slot]);
            OrbGrid.append(Btn);
        }
        MatSec.Inner.append(OrbGrid);

        if (CurMode === 'lit-matcap')
        {
            SliderRow(MatSec.Inner, {
                Label: 'MatCap Mix',
                Minimum: 0,
                Maximum: 100,
                Step: 2,
                Digits: 0,
                Unit: '%',
                Read: () => Math.round((Brush.matcapMix ?? 0.62) * 100),
                Write: Val => LivePreviewSelectedOnly({ matcapMix: Val / 100 })
            });
        }
    }

    // Sub-section B: Clean C++-Portable Surface Finish (Colour [(circle) Name · #HEX] + Metallic, Roughness, Specular)
    if (IsPbrMode)
    {
        const CurHex = (Brush.pbrColor || '#4c7dc9').toLowerCase();
        const SwatchCircle = Construct('span', 'pz-cpp-color-circle');
        SwatchCircle.style.background = CurHex;
        const ColorNameEl = Construct('span', 'pz-cpp-color-name', DescribeHexColorName(CurHex));
        const ColorHexEl = Construct('span', 'pz-cpp-color-hex', CurHex.toUpperCase());
        const ColorInput = Construct('input', 'pz-cpp-color-input');
        ColorInput.type = 'color';
        ColorInput.value = CurHex;

        ColorInput.addEventListener('input', () =>
        {
            const Norm = String(ColorInput.value || '#4c7dc9').toLowerCase();
            SwatchCircle.style.background = Norm;
            ColorNameEl.textContent = DescribeHexColorName(Norm);
            ColorHexEl.textContent = Norm.toUpperCase();
            LivePreviewSelectedOnly({ pbrColor: Norm });
        });

        const PickerPill = Construct('label', 'pz-cpp-color-picker', [
            SwatchCircle,
            Construct('span', 'pz-cpp-color-text', [ColorNameEl, ColorHexEl]),
            ColorInput
        ]);
        PickerPill.title = 'Click to pick surface colour';

        MatSec.Inner.append(Construct('div', 'pz-cpp-color-row', [
            Construct('label', 'property-label', 'Colour'),
            PickerPill
        ]));

        SliderRow(MatSec.Inner, {
            Label: 'Metallic',
            Minimum: 0.0,
            Maximum: 1.0,
            Step: 0.01,
            Digits: 2,
            Read: () => Brush.pbrMetallic ?? 0.60,
            Write: Val => LivePreviewSelectedOnly({ pbrMetallic: Val })
        });

        SliderRow(MatSec.Inner, {
            Label: 'Roughness',
            Minimum: 0.04,
            Maximum: 1.0,
            Step: 0.01,
            Digits: 2,
            Read: () => Brush.pbrRoughness ?? 0.24,
            Write: Val => LivePreviewSelectedOnly({ pbrRoughness: Val })
        });

        SliderRow(MatSec.Inner, {
            Label: 'Specular',
            Minimum: 0.0,
            Maximum: 1.0,
            Step: 0.01,
            Digits: 2,
            Read: () => Brush.pbrSpecular ?? 0.68,
            Write: Val => LivePreviewSelectedOnly({ pbrSpecular: Val })
        });
    }
}

function RenderPackingTab(Body, Doc)
{
    const FilterTileId = StudioState.udimFilter === 'all' ? Doc.activeUdim : Number(StudioState.udimFilter);
    const ComputeTileSelectionStats = () =>
    {
        let TotalInTile = 0;
        let SelInTile = 0;
        let SelFacesInTile = 0;
        let RawUvArea = 0;
        for (const Isl of Doc.islands)
        {
            if (Isl.hidden || Isl.udimTile !== FilterTileId) continue;
            TotalInTile++;
            RawUvArea += Isl.areaUV || 0;
            if (Doc.surface.selectedIslands.has(Isl.id))
            {
                SelInTile++;
                SelFacesInTile += Isl.faces.length;
            }
            else if (Doc.surface.selectedFaces.size > 0)
            {
                let HitFace = 0;
                for (let k = 0; k < Isl.faces.length; k++)
                {
                    if (Doc.surface.selectedFaces.has(Isl.faces[k])) HitFace++;
                }
                if (HitFace > 0)
                {
                    SelInTile++;
                    SelFacesInTile += HitFace;
                }
            }
        }
        const ActiveReport = Doc.lastPackReport?.tileReports?.find(R => R.tileId === FilterTileId) || Doc.lastPackReport?.tileReports?.[0];
        const DisplayOcc = (ActiveReport && ActiveReport.tileId === FilterTileId) ? ActiveReport.tileUtilizationPct : (RawUvArea * 100);
        const TileObj = Doc.udimTiles.find(T => T.id === FilterTileId);
        const TileRes = TileObj?.resolution || 2048;
        const ExplicitSum = Doc.importanceGroups.reduce((S, G) => S + G.targetPercent, 0);
        const RemPct = Math.max(0, 100 - ExplicitSum);
        return { TotalInTile, SelInTile, SelFacesInTile, DisplayOcc, TileRes, RemPct };
    };

    const InitialStats = ComputeTileSelectionStats();

    // 0. Live UDIM Tile & Selected Objects Cards (Top of Pack Inspector)
    MetricTrio(Body, [
        {
            Label: `Selected · UDIM ${FilterTileId}`,
            Highlight: InitialStats.SelInTile > 0,
            Read: () =>
            {
                const S = ComputeTileSelectionStats();
                return {
                    value: `${S.SelInTile} / ${S.TotalInTile}`,
                    sub: S.SelFacesInTile > 0 ? `${S.SelFacesInTile.toLocaleString()} faces selected` : `Objects in UDIM ${FilterTileId}`
                };
            }
        },
        {
            Label: `UDIM ${FilterTileId} Fill`,
            Read: () =>
            {
                const S = ComputeTileSelectionStats();
                return {
                    value: `${S.DisplayOcc.toFixed(1)}%`,
                    sub: `${S.TotalInTile} charts · ${S.TileRes}px`
                };
            }
        },
        {
            Label: 'Auto Remainder',
            Read: () =>
            {
                const S = ComputeTileSelectionStats();
                return {
                    value: `${S.RemPct.toFixed(0)}%`,
                    sub: `${Doc.importanceGroups.length} layer${Doc.importanceGroups.length === 1 ? '' : 's'} active`
                };
            }
        }
    ]);

    // 1. Importance Space Allocation Settings (Inspector SliderRow + %, Rename, Colour & UDIM)
    const ActiveLayer = Doc.importanceGroups.find(G => G.id === Doc.activeLayerId) || Doc.importanceGroups[0] || null;
    if (ActiveLayer && Doc.activeLayerId !== ActiveLayer.id) Doc.activeLayerId = ActiveLayer.id;

    const AllocSec = SectionCard(Body, {
        Id: 'pack-layer-allocation',
        Title: 'Importance Space Allocation',
        Glyph: 'Layers',
        Status: () => ActiveLayer ? `${Math.round(ActiveLayer.targetPercent)}%` : '0 LAYERS'
    });

    if (!ActiveLayer)
    {
        SliderRow(AllocSec.Inner, {
            Label: 'New Layer Share',
            Minimum: 0,
            Maximum: 100,
            Step: 1,
            Digits: 0,
            Unit: '%',
            Read: () => StudioState.pendingGroupPct,
            Write: Val => { StudioState.pendingGroupPct = Math.max(0, Math.min(100, Math.round(Val))); }
        });
        ActionRow(AllocSec.Inner, [
            { Label: 'Create Layer from Selection', Glyph: 'Plus', Tone: 'accent', Act: () => AssignSelectedIslandsToNewGroup(StudioState.pendingGroupPct) }
        ]);
    }
    else
    {
        if (Doc.importanceGroups.length > 1)
        {
            SelectRow(AllocSec.Inner, {
                Label: 'Active Layer',
                Options: Doc.importanceGroups.map(G => [`${G.name} (${Math.round(G.targetPercent)}%)`, G.id]),
                Read: () => ActiveLayer.id,
                Write: Val =>
                {
                    Doc.activeLayerId = Val;
                    const Ref = Doc.surface.GetLayerFaceRef(Val, Doc.islands, null);
                    Doc.surface.SelectFaceGroupRef(Ref, false);
                    FastSyncSelectionViews(Doc, true);
                }
            });
        }

        // Space Share (%) — Front-and-center Inspector SliderRow component!
        SliderRow(AllocSec.Inner, {
            Label: 'Space Share',
            Minimum: 0,
            Maximum: 100,
            Step: 1,
            Digits: 0,
            Unit: '%',
            Read: () => ActiveLayer.targetPercent,
            Write: Val =>
            {
                ActiveLayer.targetPercent = Math.max(0, Math.min(100, Math.round(Val)));
                RefreshLayerBudgetLive(Doc);
            }
        });

        // Layer Name (Rename)
        const NameInput = Construct('input', 'pz-inspector-text-input');
        NameInput.type = 'text';
        NameInput.value = ActiveLayer.name;
        NameInput.spellcheck = false;
        NameInput.placeholder = 'Layer name';
        NameInput.addEventListener('input', () =>
        {
            const Trimmed = NameInput.value.trim();
            if (!Trimmed) return;
            ActiveLayer.name = Trimmed;
            const TitleEl = document.querySelector(`#outliner-list [data-layer-title="${ActiveLayer.id}"]`);
            if (TitleEl) TitleEl.textContent = Trimmed;
            RenderLegendStrip(Doc);
        });
        NameInput.addEventListener('change', () =>
        {
            if (!NameInput.value.trim()) NameInput.value = ActiveLayer.name;
            _lastOutlinerSig = null;
        });
        AllocSec.Inner.append(Construct('div', 'property-row select-row', [
            Construct('label', 'property-label', 'Layer Name'),
            NameInput
        ]));

        // Layer Colour (Reusable C++ Colour Picker Pill)
        const CurHex = /^#[0-9a-fA-F]{6}$/.test(ActiveLayer.color || '') ? ActiveLayer.color.toLowerCase() : '#eff4cf';
        const SwatchCircle = Construct('span', 'pz-cpp-color-circle');
        SwatchCircle.style.background = CurHex;
        const ColorNameEl = Construct('span', 'pz-cpp-color-name', DescribeHexColorName(CurHex));
        const ColorHexEl = Construct('span', 'pz-cpp-color-hex', CurHex.toUpperCase());
        const ColorInput = Construct('input', 'pz-cpp-color-input');
        ColorInput.type = 'color';
        ColorInput.value = CurHex;
        ColorInput.addEventListener('input', () =>
        {
            const Norm = String(ColorInput.value || '#eff4cf').toLowerCase();
            ActiveLayer.color = Norm;
            SwatchCircle.style.background = Norm;
            ColorNameEl.textContent = DescribeHexColorName(Norm);
            ColorHexEl.textContent = Norm.toUpperCase();
            const BoxEl = document.querySelector(`#outliner-list .pz-uv-layer-card[data-layer-id="${ActiveLayer.id}"]`);
            if (BoxEl)
            {
                BoxEl.style.setProperty('--layer-color', Norm);
                const Sq = BoxEl.querySelector('.pz-layer-sq-swatch');
                if (Sq) Sq.style.background = Norm;
            }
            RefreshLayerBudgetLive(Doc);
            ScheduleFast2DRender(Doc);
        });
        ColorInput.addEventListener('change', () => { _lastOutlinerSig = null; });

        AllocSec.Inner.append(Construct('div', 'pz-cpp-color-row', [
            Construct('label', 'property-label', 'Layer Colour'),
            Construct('label', 'pz-cpp-color-picker', [
                SwatchCircle,
                Construct('span', 'pz-cpp-color-text', [ColorNameEl, ColorHexEl]),
                ColorInput
            ])
        ]));

        // Target UDIM
        const SortedTiles = [...Doc.udimTiles].sort((A, B) => A.id - B.id);
        SelectRow(AllocSec.Inner, {
            Label: 'Target UDIM',
            Options: [['Auto UDIM', 'auto'], ...SortedTiles.map(T => [`UDIM ${T.id}`, String(T.id)])],
            Read: () => (ActiveLayer.udimTarget && ActiveLayer.udimTarget !== 'auto') ? String(ActiveLayer.udimTarget) : 'auto',
            Write: Val =>
            {
                ActiveLayer.udimTarget = Val;
                const FullRef = Doc.surface.GetLayerFaceRef(ActiveLayer.id, Doc.islands, null);
                if (Val !== 'auto' && FullRef.islandCount > 0)
                {
                    Doc.surface.PushUndo(`Move "${ActiveLayer.name}" to UDIM ${Val}`);
                    Doc.surface.MoveIslandsToUdim([...FullRef.islandIds], Number(Val), Doc.islands);
                    _lastOutlinerSig = null;
                    _lastBudgetCardSig = null;
                    SyncAllViews(true);
                }
                else
                {
                    _lastOutlinerSig = null;
                    RenderOutliner(Doc);
                }
            }
        });

        ActionRow(AllocSec.Inner, [
            { Label: 'Bind Selected Faces', Glyph: 'Plus', Tone: 'accent', Act: () => AssignSelectedToGroup(ActiveLayer.id) },
            { Label: 'Unassign Selected', Glyph: 'Reset', Act: () => AssignSelectedToGroup(null) }
        ]);
    }

    // 2. Concave Packing Engine
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
    const Sel = ComputeInspectorSelectionInfo(Doc);
    RenderUvSelectionTransformCard(Body, Doc);

    // 1. Unwrap, Projections, Follow Active Quads & Stack Similar UVs (Compact Icon-Driven)
    const UnwrapSec = SectionCard(Body, {
        Id: 'unwrap-ops',
        Title: 'Unwrap, Quads & Stacking',
        Glyph: 'Unwrap',
        Badge: Sel.hasSelection ? `${Sel.faceCount}F` : 'SURFACE'
    });
    ActionRow(UnwrapSec.Inner, [
        { Label: 'LSCM Unwrap (U)', Glyph: 'Unwrap', Tone: 'accent', Act: () => RunUnwrap('lscm') },
        { Label: 'Smart UV', Glyph: 'Sparkles', Act: () => RunUnwrap('smart') }
    ]);

    IconStripBlock(UnwrapSec.Inner, {
        Label: 'Quad Strip & Similar Stacking',
        Meta: '1-click',
        Items: [
            { Glyph: 'FollowQuads', Short: 'Follow Quads', Tone: 'warm', Title: 'Follow Active Quads — unwrap connected quads into an orthogonal UV strip', Act: () => RunUnwrap('follow-quads') },
            { Glyph: 'StackUv', Short: 'Stack Similar', Tone: 'accent', Title: 'Stack Similar UVs — align & snap matching UV islands directly on top of each other', Act: RunStackSimilarOp },
            { Glyph: 'UnstackUv', Title: 'Spread / Unstack overlapping duplicate UV islands', Act: RunUnstackOp }
        ]
    });

    IconStripBlock(UnwrapSec.Inner, {
        Label: 'Analytic Projections',
        Meta: `${StudioState.smartAngleDeg}°`,
        Items: [
            { Glyph: 'Cube', Title: 'Cube Projection', Act: () => RunUnwrap('cube') },
            { Glyph: 'Cylinder', Title: 'Cylinder Projection', Act: () => RunUnwrap('cylinder') },
            { Glyph: 'Sphere', Title: 'Sphere Projection', Act: () => RunUnwrap('sphere') },
            { Glyph: 'CameraView', Title: 'Project from Camera View', Act: () => RunUnwrap('view') },
            '|',
            { Glyph: 'Reset', Title: 'Reset Face UVs to [0..1]', Act: () => RunUnwrap('reset') }
        ]
    });

    SliderRow(UnwrapSec.Inner, {
        Label: 'Smart Angle Limit',
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
        Detail: 'Continuously re-solve LSCM while moving pins',
        Read: () => StudioState.liveUnwrap,
        Write: V => { StudioState.liveUnwrap = V; }
    });

    // 2. Unified Icon Toolbars: Alignment, Pinning, Seam & Weld (Zero Text Clutter!)
    const ToolSec = SectionCard(Body, {
        Id: 'align-rectify',
        Title: 'Align, Pin & Seam Icons',
        Glyph: 'Align',
        Badge: `${Doc.surface.pinned.size} PIN · ${Doc.surface.seams.size} SEAM`
    });

    const ExecAlign = (Lbl, Op) => RunWithUndo(Lbl, () =>
    {
        const Piv = ComputeSelectionPivot(Doc) || StudioState.cursorUV;
        ApplyAlignOrRectify(Doc.surface, Op, Piv);
        Toast(`${Lbl} applied`);
    });

    IconStripBlock(ToolSec.Inner, {
        Label: 'Alignment & Rectify',
        Meta: 'U / V',
        Items: [
            { Glyph: 'AlignLeft', Title: 'Align Left (Min U)', Act: () => ExecAlign('Align Min U', 'align-u-min') },
            { Glyph: 'AlignCenterH', Title: 'Align Horizontal Center (U)', Act: () => ExecAlign('Align U Center', 'align-u-center') },
            { Glyph: 'AlignRight', Title: 'Align Right (Max U)', Act: () => ExecAlign('Align Max U', 'align-u-max') },
            '|',
            { Glyph: 'AlignTop', Title: 'Align Top (Max V)', Act: () => ExecAlign('Align Top V', 'align-v-max') },
            { Glyph: 'AlignCenterV', Title: 'Align Vertical Middle (V)', Act: () => ExecAlign('Align V Center', 'align-v-center') },
            { Glyph: 'AlignBottom', Title: 'Align Bottom (Min V)', Act: () => ExecAlign('Align Bottom V', 'align-v-min') },
            '|',
            { Glyph: 'Straighten', Title: 'Straighten UV Line', Act: () => ExecAlign('Straighten', 'straighten') },
            { Glyph: 'Rectify', Title: 'Rectify Selected Quads', Act: () => ExecAlign('Rectify Quads', 'rectify-quads') },
            { Glyph: 'Mirror', Title: 'Mirror Horizontal (U)', Act: () => ExecAlign('Mirror U', 'mirror-u') },
            { Glyph: 'MirrorV', Title: 'Mirror Vertical (V)', Act: () => ExecAlign('Mirror V', 'mirror-v') }
        ]
    });

    IconStripBlock(ToolSec.Inner, {
        Label: 'Pin & Topology',
        Meta: `${Sel.pinnedCount}/${Doc.surface.pinned.size} pinned`,
        Items: [
            { Glyph: 'Pin', Tone: 'warm', Title: 'Pin Selected UVs (P)', Act: () => RunWithUndo('Pin', () => { const C = Doc.surface.PinSelected(true); Toast(`Pinned ${C} UV(s)`); }) },
            { Glyph: 'PinOff', Title: 'Unpin Selected UVs (Alt+P)', Act: () => RunWithUndo('Unpin', () => { const C = Doc.surface.PinSelected(false); Toast(`Unpinned ${C} UV(s)`); }) },
            { Glyph: 'PinBorder', Title: 'Pin Chart Boundary UVs', Act: () => RunWithUndo('Pin Boundary', () => { const C = Doc.surface.PinIslandBoundaries(); Toast(`Pinned ${C} boundary UVs`); }) },
            { Glyph: 'Trash', Title: 'Clear All Pins on Surface', Act: () => RunWithUndo('Clear Pins', () => { const N = Doc.surface.pinned.size; Doc.surface.pinned.clear(); Toast(`Cleared ${N} pin(s)`); }) },
            '|',
            { Glyph: 'Split', Title: 'Split UV Island (Y)', Act: () => RunWithUndo('Split Island', () => { Doc.surface.SplitSelection(); }) },
            { Glyph: 'Stitch', Title: 'Stitch Shared Boundary UVs (V)', Act: () => RunWithUndo('Stitch', () => { const N = Doc.surface.StitchSelected(Doc.islands); Toast(`Stitched ${N} UV pairs`); }) },
            { Glyph: 'Merge', Title: 'Weld / Merge Selected UVs (M)', Act: () => RunWithUndo('Weld', () => { const N = Doc.surface.WeldSelected(); Toast(`Welded ${N} UVs`); }) }
        ]
    });

    IconStripBlock(ToolSec.Inner, {
        Label: 'Seams & Loops',
        Meta: `${Doc.surface.seams.size} seams`,
        Items: [
            { Glyph: 'Scissors', Tone: 'accent', Title: 'Mark Seam on Selected Edges/Bounds', Act: () => RunEdgeOrSeamOp('mark-seam') },
            { Glyph: 'Close', Title: 'Clear Seam on Selection', Act: () => RunEdgeOrSeamOp('clear-seam') },
            { Glyph: 'SeamWeld', Title: 'Clear Seam & Weld Shared UVs', Act: () => RunEdgeOrSeamOp('clear-seam-weld') },
            '|',
            { Glyph: 'SharpEdge', Title: `Mark Sharp Edges (>= ${StudioState.sharpAngleDeg}°) as Seams`, Act: () => RunEdgeOrSeamOp('mark-sharp-seam') },
            { Glyph: 'Island', Title: 'Mark Seams from Current UV Island Borders', Act: () => RunEdgeOrSeamOp('seams-from-islands') },
            { Glyph: 'Edge', Title: 'Select Edge Loop (Alt+Click)', Act: () => RunEdgeOrSeamOp('select-loop') }
        ]
    });

    // 3. Relax & Texel Density (Compact)
    const RelaxSec = SectionCard(Body, {
        Id: 'relax-density',
        Title: 'Relax & Texel Density',
        Glyph: 'Relax',
        Badge: `${Sel.avgDensity.toFixed(1)} PX/CM`
    });
    SliderRow(RelaxSec.Inner, {
        Label: 'Relax Passes',
        Minimum: 4,
        Maximum: 60,
        Step: 2,
        Digits: 0,
        Unit: 'it',
        Read: () => StudioState.relaxIterations,
        Write: V => { StudioState.relaxIterations = V; }
    });
    SliderRow(RelaxSec.Inner, {
        Label: 'Target Density',
        Minimum: 1,
        Maximum: 64,
        Step: 0.5,
        Digits: 1,
        Unit: 'px/cm',
        Read: () => StudioState.targetTexelDensity,
        Write: V => { StudioState.targetTexelDensity = V; SyncAllViews(); }
    });
    IconStripBlock(RelaxSec.Inner, {
        Label: 'Stretch & Density Actions',
        Items: [
            {
                Glyph: 'Relax', Short: 'Relax', Tone: 'warm', Title: 'Minimize Stretch (Ctrl+V)',
                Act: () => RunWithUndo('Minimize Stretch', () =>
                {
                    const Moved = MinimizeStretch(Doc.surface, { iterations: StudioState.relaxIterations, onlySelected: Doc.surface.selectedUVs.size > 0, islands: Doc.islands });
                    LogEvent('RELAX', `Minimized stretch (${Moved} UVs, ${StudioState.relaxIterations} passes)`, 'live');
                    Toast(`Stretch relaxed (${Moved} UVs)`);
                })
            },
            {
                Glyph: 'Scale', Short: 'Avg Scale', Title: 'Average Island Scale (Ctrl+A)',
                Act: () => RunWithUndo('Average Scale', () =>
                {
                    AverageIslandScale(Doc.surface, Doc.islands, Doc.surface.selectedIslands.size > 0);
                    LogEvent('SCALE', 'Equalized island texel density');
                    Toast('Average Island Scale applied');
                })
            },
            {
                Glyph: 'Gauge', Short: 'Set px/cm', Title: `Scale target charts to ${StudioState.targetTexelDensity} px/cm`,
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
        ]
    });
}

function RenderUdimTab(Body, Doc)
{
    const IslandsByTile = new Map();
    const OccByTile = new Map();
    for (const Isl of Doc.islands)
    {
        if (!IslandsByTile.has(Isl.udimTile)) IslandsByTile.set(Isl.udimTile, []);
        IslandsByTile.get(Isl.udimTile).push(Isl);
        if (!Isl.hidden) OccByTile.set(Isl.udimTile, (OccByTile.get(Isl.udimTile) || 0) + Isl.areaUV);
    }

    const ActiveTile = Doc.udimTiles.find(T => T.id === Doc.activeUdim) || Doc.udimTiles[0];
    const TotalMpx = Doc.udimTiles.reduce((S, T) => S + ((T.resolution || 2048) * (T.resolution || 2048)) / 1e6, 0);
    const GlobalAvgDen = Doc.islands.length
        ? Doc.islands.reduce((S, I) => S + I.texelDensity, 0) / Doc.islands.length
        : 0;

    // 1. Check-Style Neutral Metric Trio (#eff4cf cream-olive highlight + #1a1a1a neutral cards)
    MetricTrio(Body, [
        {
            Label: 'Active UDIM',
            Highlight: true,
            Read: () =>
            {
                const Res = ActiveTile?.resolution || 2048;
                const Tag = Res >= 1024 ? `${Res / 1024}K` : `${Res}px`;
                return { value: `${ActiveTile ? ActiveTile.id : 1001}`, sub: `${Tag} · ${((Res * Res) / 1e6).toFixed(1)} MP` };
            }
        },
        {
            Label: 'Atlas Tiles',
            Read: () => ({ value: `${Doc.udimTiles.length}`, sub: `${TotalMpx.toFixed(1)} MP total` })
        },
        {
            Label: 'Texel Density',
            Read: () => ({ value: `${GlobalAvgDen.toFixed(1)}`, sub: 'px/cm average' })
        }
    ]);

    // 2. Clean Neutral UDIM Tile Table (Matches Check & Priority Layer — zero Spatial Matrix clutter or blue cast!)
    const AtlasSec = SectionCard(Body, {
        Id: 'udim-atlas-list',
        Title: 'UDIM Tiles',
        Glyph: 'Grid',
        Badge: `${Doc.udimTiles.length} TILE${Doc.udimTiles.length === 1 ? '' : 'S'}`
    });

    const AddTileBtn = Construct('button', 'pz-prio-add-btn', [ConstructGlyph('Plus'), Construct('span', null, 'Add UDIM Tile')]);
    AddTileBtn.type = 'button';
    AddTileBtn.title = 'Allocate the next sequential UDIM tile (1002, 1003, ...)';
    AddTileBtn.addEventListener('click', () => AddNextUdimTile());

    const MoveSelBtn = Construct('button', 'pz-icon-btn', ConstructGlyph('Move'));
    MoveSelBtn.type = 'button';
    MoveSelBtn.title = `Move selected UV charts into active UDIM ${Doc.activeUdim}`;
    MoveSelBtn.addEventListener('click', () => RunWithUndo('Move to UDIM', () =>
    {
        if (!Doc.surface.selectedIslands.size) { Toast('Select UV charts first'); return; }
        const Moved = Doc.surface.MoveIslandsToUdim([...Doc.surface.selectedIslands], Doc.activeUdim, Doc.islands);
        Toast(`Moved ${Moved} chart(s) to UDIM ${Doc.activeUdim}`);
    }));

    const MatchDenBtn = Construct('button', 'pz-icon-btn', ConstructGlyph('Gauge'));
    MatchDenBtn.type = 'button';
    MatchDenBtn.title = 'Equalize texel density across mixed-resolution UDIM tiles';
    MatchDenBtn.addEventListener('click', () => RunWithUndo('Cross-UDIM Density Match', () =>
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
        Toast(`Matched density across UDIMs (${MeanDen.toFixed(1)} px/cm)`);
    }));

    const FrameAllBtn = Construct('button', 'pz-icon-btn', ConstructGlyph('Maximize'));
    FrameAllBtn.type = 'button';
    FrameAllBtn.title = 'Frame all allocated UDIM tiles in 2D viewport';
    FrameAllBtn.addEventListener('click', () => Canvas2D.FrameUdimTiles(Doc.udimTiles));

    const LoadTexBtn = Construct('button', 'pz-icon-btn', ConstructGlyph('Upload'));
    LoadTexBtn.type = 'button';
    LoadTexBtn.title = 'Load reference texture onto active UDIM tile';
    LoadTexBtn.addEventListener('click', () => document.getElementById('tex-file-input').click());

    AtlasSec.Inner.append(Construct('div', 'pz-prio-toolbar', [
        AddTileBtn,
        Construct('div', 'pz-prio-tools', [MoveSelBtn, MatchDenBtn, FrameAllBtn, LoadTexBtn])
    ]));

    const Table = Construct('div', 'pz-udim-table', [
        Construct('div', 'pz-udim-colhead', [
            Construct('span', null, ''),
            Construct('span', null, 'Tile'),
            Construct('span', null, 'Charts'),
            Construct('span', null, 'Res'),
            Construct('span', null, '')
        ])
    ]);

    const ResCycle = [512, 1024, 2048, 4096, 8192];
    const SortedTiles = [...Doc.udimTiles].sort((A, B) => A.id - B.id);
    for (const Tile of SortedTiles)
    {
        const IsAct = Tile.id === Doc.activeUdim;
        const TileIslands = IslandsByTile.get(Tile.id) || [];
        const OccPct = Math.min(100, (OccByTile.get(Tile.id) || 0) * 100);
        const Res = Tile.resolution || 2048;
        const ResLabel = Res >= 1024 ? `${Res / 1024}K` : `${Res}`;
        const Orig = UdimOrigin(Tile.id);

        const ResChip = Construct('button', 'pz-udim-res-chip', ResLabel);
        ResChip.type = 'button';
        ResChip.title = `Click to cycle UDIM ${Tile.id} resolution (512 / 1K / 2K / 4K / 8K)`;
        ResChip.addEventListener('click', E =>
        {
            E.stopPropagation();
            const Next = ResCycle[(ResCycle.indexOf(Res) + 1) % ResCycle.length];
            Tile.resolution = Next;
            Doc.activeUdim = Tile.id;
            SyncAllViews(true);
            Toast(`UDIM ${Tile.id} → ${Next >= 1024 ? Next / 1024 + 'K' : Next + 'px'}`);
        });

        const DelBtn = Construct('button', 'pz-prio-act danger', ConstructGlyph('Close'));
        DelBtn.type = 'button';
        DelBtn.title = `Remove UDIM ${Tile.id}`;
        DelBtn.addEventListener('click', E =>
        {
            E.stopPropagation();
            if (Doc.udimTiles.length <= 1) { Toast('At least 1 UDIM tile is required'); return; }
            Doc.udimTiles = Doc.udimTiles.filter(T => T.id !== Tile.id);
            if (Doc.activeUdim === Tile.id) Doc.activeUdim = Doc.udimTiles[0].id;
            SyncAllViews(true);
        });

        const FillLine = Construct('span', 'pz-prio-bar');
        FillLine.style.width = `${Math.max(2, OccPct)}%`;
        FillLine.style.background = IsAct ? '#eff4cf' : '#585858';

        const Row = Construct('div', `pz-udim-row${IsAct ? ' active' : ''}`, [
            Construct('span', 'pz-udim-dot'),
            Construct('div', 'pz-udim-id-cell', [
                Construct('strong', null, String(Tile.id)),
                Construct('small', null, `u${Orig.u}, v${Orig.v} · ${OccPct.toFixed(0)}%`)
            ]),
            Construct('span', 'pz-udim-occ-cell', `${TileIslands.length}`),
            ResChip,
            DelBtn,
            FillLine
        ]);
        Row.title = `Focus UDIM ${Tile.id} (Shift+Click moves selected charts into ${Tile.id})`;
        Row.addEventListener('click', E =>
        {
            Doc.activeUdim = Tile.id;
            if (E.shiftKey && Doc.surface.selectedIslands.size > 0)
            {
                RunWithUndo(`Move Selection to UDIM ${Tile.id}`, () =>
                {
                    const Moved = Doc.surface.MoveIslandsToUdim([...Doc.surface.selectedIslands], Tile.id, Doc.islands);
                    Toast(`Moved ${Moved} chart(s) to UDIM ${Tile.id}`);
                });
                return;
            }
            Canvas2D.FrameBounds(Orig.u, Orig.v, Orig.u + 1, Orig.v + 1, 0.14);
            SyncAllViews(true);
        });
        Table.append(Row);
    }
    AtlasSec.Inner.append(Table);

    // 3. Active Tile Resolution & Backing
    if (ActiveTile)
    {
        const ActiveIslands = IslandsByTile.get(ActiveTile.id) || [];
        const TileAvgDen = ActiveIslands.length
            ? ActiveIslands.reduce((S, I) => S + I.texelDensity, 0) / ActiveIslands.length
            : 0;

        const TileSec = SectionCard(Body, {
            Id: 'udim-tile-props',
            Title: `UDIM ${ActiveTile.id} Settings`,
            Glyph: 'Sliders',
            Badge: `${TileAvgDen.toFixed(1)} px/cm`
        });

        const ResDeck = Construct('div', 'pz-udim-res-pills');
        for (const [Tag, Sub, Px] of [
            ['512', '0.3 MP', 512],
            ['1K', '1.0 MP', 1024],
            ['2K', '4.2 MP', 2048],
            ['4K', '16.8 MP', 4096],
            ['8K', '67.1 MP', 8192]
        ])
        {
            const Pill = Construct('button', `pz-udim-res-pill${ActiveTile.resolution === Px ? ' active' : ''}`, [
                Construct('strong', null, Tag),
                Construct('small', null, Sub)
            ]);
            Pill.type = 'button';
            Pill.title = `Set UDIM ${ActiveTile.id} resolution to ${Px}×${Px}`;
            Pill.addEventListener('click', () =>
            {
                ActiveTile.resolution = Px;
                SyncAllViews(true);
                Toast(`UDIM ${ActiveTile.id} set to ${Px}×${Px} (${Tag})`);
            });
            ResDeck.append(Pill);
        }
        TileSec.Inner.append(ResDeck);

        SegmentedRow(TileSec.Inner, {
            Label: 'Backing',
            Options: [['Color', 'color'], ['UV Code', 'uv'], ['Checker', 'checker'], ['Slate', 'blank']],
            Read: () => ActiveTile.checkerMode || 'color',
            Write: V => { ActiveTile.checkerMode = V; SyncAllViews(); }
        });
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
        const PickedIds = [];
        for (const Isl of Doc.islands)
        {
            if (Isl.hidden) continue;
            if (Isl.bounds.maxU >= MinU && Isl.bounds.minU <= MaxU && Isl.bounds.maxV >= MinV && Isl.bounds.minV <= MaxV)
            {
                const HasVertInBox = Isl.uvIndices.some(Idx => InBox(Doc.surface.uvCoords[Idx][0], Doc.surface.uvCoords[Idx][1]));
                if (HasVertInBox) PickedIds.push(Isl.id);
            }
        }
        if (PickedIds.length) Doc.surface.SelectIslands(PickedIds, true, Doc.islands);
    }
    else if (StudioState.selectionMode === 'face')
    {
        const LoopFaces = CandFaces || Doc.surface.faces.map((_, i) => i);
        let Added = false;
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
            if (InBox(Cx / C.length, Cy / C.length))
            {
                Doc.surface.selectedFaces.add(fi);
                Doc.surface.activeFace = fi;
                Added = true;
            }
        }
        if (Added) Doc.surface.SyncSelectionsFromMode('face', Doc.islands);
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
            const Order = ['lit-matcap', 'fast-pbr', 'multi-matcap', 'flat', 'flat-slots', 'checker', 'stretch', 'texel'];
            const Next = Order[(Order.indexOf(Viewport3D.shadingMode) + 1) % Order.length];
            Set3DShadingMode(Next);
            const Names = {
                'lit-matcap': 'Material + MatCap',
                'fast-pbr': 'Surface Material',
                'multi-matcap': 'MatCap Studio',
                flat: 'Flat · UV Charts',
                'flat-slots': 'Flat · Face Slots',
                checker: 'Flat · UV Checker',
                stretch: 'Flat · UV Stretch',
                texel: 'Flat · Texel Density'
            };
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
    SyncAllViews(StudioState.inspectorTab === 'shading');
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
    document.querySelectorAll('#pz-context-rail [data-rail-tool]').forEach(B => B.classList.toggle('active', B.dataset.railTool === StudioState.activeTool));
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

    // UV Space Search (ONLY Search + dynamic UDIM filters in RenderUdimFilterBar!)
    document.getElementById('outliner-search').addEventListener('input', E =>
    {
        StudioState.searchQuery = E.target.value;
        _lastOutlinerSig = null;
        RenderOutliner(ActiveDoc());
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
    document.getElementById('uv-lens-select').addEventListener('change', E =>
    {
        const Val = E.target.value;
        StudioState.overlayLens = Val;
        if (Val === 'stretch-angle' || Val === 'stretch-area') Set3DShadingMode('stretch');
        else if (Val === 'texel') Set3DShadingMode('texel');
        else if (Val === 'clean') Set3DShadingMode('lit-matcap');
        SyncAllViews();
    });
    document.getElementById('mesh-shade-select').addEventListener('change', E => Set3DShadingMode(E.target.value));
    document.querySelectorAll('.mesh-camera-bar button[data-shade3d]').forEach(B => B.addEventListener('click', () => Set3DShadingMode(B.dataset.shade3d)));
    document.getElementById('toggle-checker-btn').addEventListener('click', E => { StudioState.showChecker = !StudioState.showChecker; E.currentTarget.classList.toggle('active', StudioState.showChecker); ScheduleFast2DRender(ActiveDoc()); });
    document.getElementById('toggle-seams-btn').addEventListener('click', E =>
    {
        StudioState.showSeams = !StudioState.showSeams;
        StudioState.showBoundaries = StudioState.showSeams;
        Viewport3D.showSeams = StudioState.showSeams;
        Viewport3D.showBoundaries = StudioState.showBoundaries;
        E.currentTarget.classList.toggle('active', StudioState.showSeams);
        document.getElementById('toggle-boundaries-btn')?.classList.toggle('active', StudioState.showBoundaries);
        Viewport3D.SyncOverlayStates();
        ScheduleFast2DRender(ActiveDoc());
        RebuildInspector();
    });
    document.getElementById('toggle-boundaries-btn').addEventListener('click', E => { StudioState.showBoundaries = !StudioState.showBoundaries; Viewport3D.showBoundaries = StudioState.showBoundaries; E.currentTarget.classList.toggle('active', StudioState.showBoundaries); Viewport3D.SyncOverlayStates(); ScheduleFast2DRender(ActiveDoc()); });
    document.getElementById('toggle-wire3d-btn').addEventListener('click', E => { Viewport3D.showWireframe = !Viewport3D.showWireframe; E.currentTarget.classList.toggle('active', Viewport3D.showWireframe); Viewport3D.SyncOverlayStates(); RebuildInspector(); });

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

    // Asset / Selected Part Name edit + Reset Asset
    document.getElementById('inspector-object-name').addEventListener('change', E =>
    {
        const Doc = ActiveDoc();
        const NewName = E.target.value.trim();
        if (!NewName) { SyncAllViews(); return; }
        const Sel = ComputeInspectorSelectionInfo(Doc);
        if (Sel.hasSelection && Sel.faceIndices.length > 0)
        {
            for (const Fi of Sel.faceIndices)
            {
                if (Doc.surface.faces[Fi]) Doc.surface.faces[Fi].partId = NewName;
            }
            SyncAllViews(true);
            Toast(`Renamed selected part to "${NewName}"`);
        }
        else
        {
            Doc.name = Doc.surface.name = NewName;
            RenderDocumentTabs();
            SyncAllViews();
        }
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

    // Project-Zero Notch, Secondary Ops Shelf & Sample Surface Pull-Up Tab
    document.getElementById('pz-notch-toggle')?.addEventListener('click', E =>
    {
        const Drawer = document.getElementById('pz-notch-drawer');
        if (!Drawer) return;
        Drawer.hidden = !Drawer.hidden;
        E.currentTarget.classList.toggle('open', !Drawer.hidden);
        requestAnimationFrame(() => SyncAllViews());
    });
    document.getElementById('pz-more-ops-btn')?.addEventListener('click', E =>
    {
        const Ops = document.getElementById('pz-secondary-ops');
        if (!Ops) return;
        Ops.hidden = !Ops.hidden;
        E.currentTarget.classList.toggle('active', !Ops.hidden);
        requestAnimationFrame(() => SyncAllViews());
    });
    document.getElementById('pz-viewport-plus-btn')?.addEventListener('click', E =>
    {
        const R = E.currentTarget.getBoundingClientRect();
        SpawnMenu.style.left = `${R.left}px`;
        SpawnMenu.style.top = `${R.bottom + 6}px`;
        SpawnMenu.hidden = !SpawnMenu.hidden;
    });
    document.getElementById('pz-sample-drawer-btn')?.addEventListener('click', E =>
    {
        const R = E.currentTarget.getBoundingClientRect();
        SpawnMenu.style.left = `${Math.max(12, R.left - 80)}px`;
        SpawnMenu.style.top = `${Math.max(12, R.top - 290)}px`;
        SpawnMenu.hidden = !SpawnMenu.hidden;
    });
    document.getElementById('pz-inspector-export-plus')?.addEventListener('click', OpenExportModal);
    document.getElementById('pz-export-pill-btn')?.addEventListener('click', OpenExportModal);

    // UV Space 5 Infographic Cards (3 Pack Tiles + Stretch Caution [Amber] + Overlaps [Red])
    document.getElementById('lp-kpi-util')?.addEventListener('click', () => ActivateInspectorTab('packing'));
    document.getElementById('lp-kpi-selected')?.addEventListener('click', () =>
    {
        const Doc = ActiveDoc();
        if (!Doc || !Doc.surface.selectedIslands.size) return;
        FrameFaceGroupRefBounds(Doc, { islandCount: Doc.surface.selectedIslands.size, islandIds: [...Doc.surface.selectedIslands] });
        FastSyncSelectionViews(Doc);
    });
    document.getElementById('lp-kpi-remaining')?.addEventListener('click', () =>
    {
        const Doc = ActiveDoc();
        if (!Doc) return;
        const RemRef = Doc.surface.GetLayerFaceRef(null, Doc.islands);
        if (!RemRef.faceCount) return;
        Doc.surface.SelectFaceGroupRef(RemRef, false);
        FastSyncSelectionViews(Doc);
    });
    document.getElementById('stat-issues-card')?.addEventListener('click', () =>
    {
        const Doc = ActiveDoc();
        if (!Doc) return;
        const StretchIds = Doc.islands.filter(I => ClassifyIslandHealth(Doc, I) === 'caution').map(I => I.id);
        if (StretchIds.length) Doc.surface.SelectIslands(StretchIds, false);
        StudioState.overlayLens = 'stretch';
        const LensSel = document.getElementById('uv-lens-select');
        if (LensSel) LensSel.value = 'stretch';
        SyncAllViews(true);
    });
    document.getElementById('stat-overlap-card')?.addEventListener('click', () =>
    {
        const Doc = ActiveDoc();
        if (!Doc) return;
        const D = Doc.diagnostics;
        Doc.surface.ClearSelection();
        for (const Fi of [...(D.OverlappingFaces || []), ...(D.FlippedFaces || [])]) Doc.surface.selectedFaces.add(Fi);
        Doc.surface.SyncDerivedSelectionFromFaces();
        StudioState.overlayLens = 'overlap';
        const LensSel = document.getElementById('uv-lens-select');
        if (LensSel) LensSel.value = 'overlap';
        SyncAllViews(true);
        Toast(Doc.surface.selectedFaces.size ? `Highlighted ${Doc.surface.selectedFaces.size} overlapping/flipped faces` : '0 UV overlaps detected');
    });
    document.getElementById('pz-new-folder-btn')?.addEventListener('click', () =>
    {
        AssignSelectedIslandsToNewGroup(StudioState.pendingGroupPct);
    });

    // Viewport Camera Dropdown (Persp / Top / Front / Side / Frame)
    document.getElementById('pz-cam-select')?.addEventListener('change', E =>
    {
        const Val = E.target.value;
        const Doc = ActiveDoc();
        if (Val === 'Frame')
        {
            if (Doc) Canvas2D.FrameUdimTiles(Doc.udimTiles);
            Viewport3D?.CameraPreset('Persp');
            E.target.value = 'Persp';
        }
        else
        {
            Viewport3D?.CameraPreset(Val);
        }
        SyncAllViews();
    });

    // Floating Left Context Tool Rail (Same Quick Tools as Right-Click Context Menu)
    const RailGlyphMap = {
        select: 'Select',
        box: 'BoxSel',
        circle: 'CircleSel',
        move: 'Move',
        rotate: 'Rotate',
        scale: 'Scale',
        seam: 'Scissors',
        'mark-seam': 'Edge',
        'clear-seam-weld': 'Merge',
        pin: 'Pin',
        relax: 'Relax',
        'select-menu': 'Sparkles'
    };
    document.querySelectorAll('#pz-context-rail button').forEach(Btn =>
    {
        const Key = Btn.dataset.railTool || Btn.dataset.railOp;
        if (Key && RailGlyphMap[Key]) Btn.append(ConstructGlyph(RailGlyphMap[Key]));
        if (Btn.dataset.railTool)
        {
            Btn.addEventListener('click', () =>
            {
                const Tool = Btn.dataset.railTool;
                if ((Tool === 'move' || Tool === 'rotate' || Tool === 'scale') && StudioState.stageMode === 'mesh')
                {
                    // Automatically open Split view when a 2D UV transform tool is clicked from 3D mode
                    document.querySelector('[data-stage-mode="split"]')?.click();
                }
                SetActiveTool(Tool);
            });
        }
        else if (Btn.dataset.railOp)
        {
            Btn.addEventListener('click', E =>
            {
                const Op = Btn.dataset.railOp;
                const Doc = ActiveDoc();
                if (!Doc) return;
                if (Op === 'mark-seam') document.getElementById('op-seam')?.click();
                else if (Op === 'clear-seam-weld') RunEdgeMenuOp('clear-seam-weld');
                else if (Op === 'pin') document.getElementById('op-pin')?.click();
                else if (Op === 'relax') RunWithUndo('Relax Stretch', () => MinimizeStretch(Doc.surface, { iterations: StudioState.relaxIterations, onlySelected: Doc.surface.selectedUVs.size > 0, islands: Doc.islands }));
                else if (Op === 'select-menu')
                {
                    const R = Btn.getBoundingClientRect();
                    OpenSelectMenu(R.right + 8, R.top);
                }
            });
        }
    });

    // Context Menu Per-Face Multi-MatCap Quick Strip is built lazily via EnsureContextMatcapStripBuilt() when MatCap mode is active!

    // Project-Zero Draggable Left/Right Dock Splitters
    function WireDockResizer(ElementId, CssVarName, MinPx, MaxPx, IsRightSide)
    {
        const Handle = document.getElementById(ElementId);
        if (!Handle) return;
        let StartX = 0;
        let StartWidth = 0;
        Handle.addEventListener('pointerdown', E =>
        {
            E.preventDefault();
            StartX = E.clientX;
            const Current = parseFloat(getComputedStyle(document.body).getPropertyValue(CssVarName)) || (IsRightSide ? 368 : 300);
            StartWidth = Current;
            Handle.classList.add('dragging');
            Handle.setPointerCapture(E.pointerId);
        });
        Handle.addEventListener('pointermove', E =>
        {
            if (!Handle.classList.contains('dragging')) return;
            const Delta = IsRightSide ? (StartX - E.clientX) : (E.clientX - StartX);
            const NextW = Math.max(MinPx, Math.min(MaxPx, Math.round(StartWidth + Delta)));
            document.body.style.setProperty(CssVarName, `${NextW}px`);
            SyncAllViews();
        });
        const StopDrag = () => Handle.classList.remove('dragging');
        Handle.addEventListener('pointerup', StopDrag);
        Handle.addEventListener('pointercancel', StopDrag);
    }
    WireDockResizer('pz-resizer-left', '--pz-left-w', 220, 460, false);
    WireDockResizer('pz-resizer-right', '--pz-right-w', 290, 520, true);
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
        const DocId = `obj-${Date.now()}`;
        Surf.objectId = DocId;
        const DefaultLayer = {
            id: 'layer-default',
            name: 'Layer 1',
            targetPercent: 100,
            color: '#eff4cf',
            udimTarget: '1001'
        };
        const AllFaceIdx = new Uint32Array(Surf.faces.length);
        for (let i = 0; i < Surf.faces.length; i++) AllFaceIdx[i] = i;
        Surf.AssignFacesToLayer(AllFaceIdx, DefaultLayer.id);
        const Doc = {
            id: DocId,
            factoryId: 'custom',
            name: Surf.name,
            surface: Surf,
            importanceGroups: Surf.faces.length > 0 ? [DefaultLayer] : [],
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
                const PickedIds = [];
                for (const Isl of Doc.islands)
                {
                    if (Isl.hidden) continue;
                    if (Isl.faceIndices.some(Fi => FaceSet.has(Fi)))
                    {
                        if (subtract) { for (const Fi of Isl.faceIndices) Doc.surface.selectedFaces.delete(Fi); }
                        else PickedIds.push(Isl.id);
                    }
                }
                if (subtract) Doc.surface.SyncSelectionsFromMode('face', Doc.islands);
                else if (PickedIds.length) Doc.surface.SelectIslands(PickedIds, true, Doc.islands);
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
    OutlinerScroll = SmoothScroll(document.getElementById('outliner-scroll'), document.getElementById('outliner-scroll-content'));

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
