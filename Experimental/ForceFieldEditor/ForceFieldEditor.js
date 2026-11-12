/*==========================================================================================================================================
                                                           FORCEFIELDEDITOR.JS
==========================================================================================================================================*/
// 🧩 Force Field & Wind Studio: interactive placement of Wind Generators + 16 bounded force fields, anti-clutter RK2 Flow Lines
//    + sparse Vector Field + finite glowing Influence envelopes, across Map 2D and 3D Field Stage.

import { ForceFieldCatalogue as F, Radian } from './Engine/ForceFieldCatalogue.js';
import { StudioControls as C } from './StudioControls.js';
import { ForceField3DViewport } from './Engine/ForceField3DViewport.js';

const V = globalThis.ForceFieldVisualization;
const StorageKey = 'frontier-remastered-forcefield-v1';
const SampleCapacity = 40;

const Scene = RestoreScene();
const Clock = { Time: 0, Playing: true, LastTick: 0 };
const PeakSamples = [], WindSamples = [];
const LiveRefreshers = [];
const LiveOutlinerRows = [];
let PageScope = null, OutlinerScope = null;
let Selected = Scene.Sources[0]?.Token || 'Field';
let PlacementType = null;
let PlacementDragPreview = null;
let ViewMode = 'Map'; // 'Map' | 'Stage3D'
let Visualization = null;
let Stage3D = null;
let HoverPoint = null;
let OutlinerQuery = '';
let SceneRevision = 0, PreparedRevision = -1;
let Prepared = F.Prepare(Scene, 0);
let PlayButton = null, ClockReading = null, PlayheadMark = null, TimelineSignature = '';

const Clamp = (Val, Lo, Hi) => Math.min(Hi, Math.max(Lo, Val));
const Wrap360 = (Deg) => ((Deg % 360) + 360) % 360;
const FormatDistance = (M) => V ? V.FormatDistance(M) : `${Math.round(M)} m`;
const FormatCount = (N) => N >= 1e6 ? (N / 1e6).toFixed(1) + ' M' : N >= 1e4 ? (N / 1e3).toFixed(0) + ' k' : N.toLocaleString('en-US');
const SourceByToken = (Token) => Scene.Sources.find((S) => S.Token === Token) || null;

//------------------------------------------------------------------------------------------------------------------------
//                                                 PERSISTENCE & SCENARIOS
//------------------------------------------------------------------------------------------------------------------------

function RestoreScene()
{
    const Defaults = F.ConstructScenario('lab');
    try
    {
        const Stored = JSON.parse(localStorage.getItem(StorageKey) || 'null');
        if (!Stored || !Array.isArray(Stored.Sources)) return Defaults;
        return {
            Scenario: Stored.Scenario || 'lab',
            Wind: Object.assign(Defaults.Wind, Stored.Wind),
            Field: Object.assign(Defaults.Field, Stored.Field),
            Sources: Stored.Sources.filter((S) => F.TypeById[S.Type]).map((S) => F.ConstructSource(S.Type, S)),
            Visualization: Object.assign(Defaults.Visualization, Stored.Visualization, { Solo: null }),
            Timeline: Object.assign(Defaults.Timeline, Stored.Timeline)
        };
    }
    catch { return Defaults; }
}

let PersistTimer = 0;
function Persist()
{
    clearTimeout(PersistTimer);
    PersistTimer = setTimeout(() => { try { localStorage.setItem(StorageKey, JSON.stringify(Scene)); } catch {} }, 300);
}

function LoadScenario(Key)
{
    const Fresh = F.ConstructScenario(Key);
    Object.assign(Scene, Fresh);
    Selected = Scene.Sources[0]?.Token || 'Field';
    Clock.Time = 0;
    PeakSamples.length = 0;
    WindSamples.length = 0;
    CancelPlacement();
    if (Visualization)
    {
        Visualization.View.CentreX = 0;
        Visualization.View.CentreZ = 0;
        Visualization.View.Span = Scene.Visualization.Span || 1400;
        Visualization.Invalidate();
    }
    ConstructOutliner();
    ConstructInspectorPage();
    ConstructTimeline();
    SyncTopUi();
    SceneRevision++;
    C.Broadcast();
    AppendLog('SCENARIO', `Loaded preset: ${Key}`, `${Scene.Sources.length} bounded forcefields`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      LOG STREAM
//------------------------------------------------------------------------------------------------------------------------

const LogEntries = [];
let LogPaused = false;

function AppendLog(Channel, Message, Detail, Key)
{
    if (LogPaused) return;
    const Now = new Date();
    const Last = LogEntries[0];
    if (Key && Last && Last.Key === Key && Now - Last.When < 1400) Object.assign(Last, { Message, Detail, When: Now });
    else LogEntries.unshift({ Channel, Message, Detail, Key, When: Now });
    if (LogEntries.length > 80) LogEntries.pop();
    RefreshLog();
}

function RefreshLog()
{
    const Content = document.getElementById('LogContent');
    if (!Content) return;
    Content.textContent = '';
    for (const Entry of LogEntries)
    {
        Content.append(C.Construct('div', 'log-row', [
            C.Construct('span', 'when', Entry.When.toTimeString().slice(0, 8)),
            C.Construct('span', 'channel', Entry.Channel),
            C.Construct('span', 'message', [C.Construct('b', null, Entry.Message), Entry.Detail ? C.Construct('span', null, ' · ' + Entry.Detail) : null])
        ]));
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   BINDING HELPERS
//------------------------------------------------------------------------------------------------------------------------

function Bind(Target, Key, Extra = {}, Channel = 'FORCES', Owner = 'Force Field')
{
    const Label = Extra.Label || Key;
    return Object.assign({
        Read: () => Target[Key],
        Write: (Value) =>
        {
            Target[Key] = Value;
            SceneRevision++;
            Visualization?.Invalidate();
            const Shown = typeof Value === 'boolean' ? (Value ? 'on' : 'off')
                : typeof Value === 'number' ? (Extra.Distance ? FormatDistance(Value) : `${Number(Value.toFixed(Extra.Digits ?? 2))}${Extra.Unit ? ' ' + Extra.Unit : ''}`)
                : Array.isArray(Value) ? `[${Value.map((V) => Number(Number(V).toFixed(1))).join(', ')}]` : String(Value);
            AppendLog(Channel, `${Owner}: ${Label} → ${Shown}`, null, `${Owner}.${Key}`);
            Sync3DIfVisible();
        }
    }, Extra);
}

const DistanceFormat = (Q) => Q >= 1000 ? [(Q / 1000).toFixed(Q >= 10000 ? 1 : 2), 'km'] : [Q.toFixed(Q < 10 ? 1 : 0), 'm'];
function DistanceParse(Text, Unit)
{
    const M = /(-?[\d.,]+)\s*(km|m)?/i.exec(Text.trim());
    if (!M) return NaN;
    return parseFloat(M[1].replace(',', '.')) * ((M[2] || Unit || 'm').toLowerCase() === 'km' ? 1000 : 1);
}

function DistanceBinding(Target, Key, Extra, Channel, Owner)
{
    return Bind(Target, Key, Object.assign({ Scale: 'Logarithmic', Format: DistanceFormat, Parse: DistanceParse, Step: 1, Distance: true }, Extra), Channel, Owner);
}

function ParameterBinding(Source, Param)
{
    const Extra = { Label: Param.Label, Minimum: Param.Minimum, Maximum: Param.Maximum, Step: Param.Step, Unit: Param.Unit, Digits: Param.Digits ?? 1 };
    return Param.Distance ? DistanceBinding(Source, Param.Key, Extra, 'FORCES', Source.Name) : Bind(Source, Param.Key, Extra, 'FORCES', Source.Name);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 PLACEMENT & MUTATIONS
//------------------------------------------------------------------------------------------------------------------------

function StartPlacement(TypeId)
{
    if (!F.TypeById[TypeId]) return;
    PlacementType = TypeId;
    PlacementDragPreview = null;
    CloseLibrary();
    ApplyVisualization();
    SyncTopUi();
    Sync3DIfVisible();
    const Type = F.TypeById[TypeId];
    const Banner = document.getElementById('PlaceBanner');
    const Text = document.getElementById('PlaceBannerText');
    if (Banner && Text)
    {
        Text.textContent = `Place ${Type.Name} · Click or drag in viewport to aim & size influence`;
        Banner.hidden = false;
    }
    AppendLog('PLACE', `Placement armed: ${Type.Name}`, 'click viewport to drop', 'Place');
}

function CancelPlacement()
{
    PlacementType = null;
    PlacementDragPreview = null;
    const Banner = document.getElementById('PlaceBanner');
    if (Banner) Banner.hidden = true;
    ApplyVisualization();
    SyncTopUi();
    Sync3DIfVisible();
}

function PlaceSourceAt(TypeId, X, Z, BearingDeg = null, RadiusMetres = null)
{
    const Type = F.TypeById[TypeId] || F.TypeById.WindGenerator;
    const CountSame = Scene.Sources.filter((S) => S.Type === TypeId).length + 1;
    const Overrides = {
        Name: `${Type.Name} ${CountSame}`,
        Centre: [Number(X.toFixed(1)), Type.Defaults.CentreY ?? (TypeId === 'WindGenerator' ? 0 : 10), Number(Z.toFixed(1))]
    };
    if (BearingDeg !== null && BearingDeg !== undefined) Overrides.Bearing = Math.round(BearingDeg);
    if (RadiusMetres !== null && RadiusMetres !== undefined)
    {
        const ClampedR = Math.round(Clamp(RadiusMetres, 15, 25000));
        Overrides.Radius = ClampedR;
        if (Type.Reach === 'Box') { Overrides.HalfX = Math.round(ClampedR * 0.75); Overrides.HalfZ = Math.round(ClampedR * 0.75); }
    }
    const Src = F.ConstructSource(TypeId, Overrides);
    Scene.Sources.push(Src);
    Selected = Src.Token;
    SceneRevision++;
    Visualization?.Invalidate();
    CancelPlacement();
    ConstructOutliner();
    ConstructInspectorPage();
    ConstructTimeline();
    Sync3DIfVisible();
    C.Broadcast();
    C.Toast(`Placed ${Src.Name} (${FormatDistance(Src.Radius)} influence)`);
    AppendLog('PLACE', `Placed ${Src.Name}`, `(${Math.round(X)}, ${Math.round(Z)}) m · ${FormatDistance(Src.Radius)}`, Src.Token);
}

function RemoveSource(Token)
{
    const Index = Scene.Sources.findIndex((S) => S.Token === Token);
    if (Index < 0) return;
    const [Removed] = Scene.Sources.splice(Index, 1);
    if (Scene.Visualization.Solo === Token) Scene.Visualization.Solo = null;
    Selected = Scene.Sources[Math.min(Index, Scene.Sources.length - 1)]?.Token || 'Field';
    SceneRevision++;
    Visualization?.Invalidate();
    ConstructOutliner();
    ConstructInspectorPage();
    ConstructTimeline();
    Sync3DIfVisible();
    C.Broadcast();
    AppendLog('FORCES', `Removed ${Removed.Name}`, null, 'Remove');
}

function DuplicateSource(Source)
{
    const Copy = F.ConstructSource(Source.Type, Object.assign({}, JSON.parse(JSON.stringify(Source)), {
        Token: undefined,
        Name: `${Source.Name} copy`,
        Centre: [Source.Centre[0] + Math.max(40, Source.Radius * 0.35), Source.Centre[1], Source.Centre[2] + Math.max(40, Source.Radius * 0.35)]
    }));
    Scene.Sources.push(Copy);
    Selected = Copy.Token;
    SceneRevision++;
    Visualization?.Invalidate();
    ConstructOutliner();
    ConstructInspectorPage();
    ConstructTimeline();
    Sync3DIfVisible();
    C.Broadcast();
    AppendLog('FORCES', `Duplicated ${Copy.Name}`, null, Copy.Token);
}

function FireSource(Source)
{
    Source.TriggeredAt = Clock.Time;
    Source.Enabled = true;
    SceneRevision++;
    Visualization?.Invalidate();
    ConstructTimeline();
    Sync3DIfVisible();
    C.Broadcast();
    AppendLog('TRIGGER', `Fired ${Source.Name}`, `at t = ${Clock.Time.toFixed(1)} s`, Source.Token + '.Fire');
}

function FrameSource(Source)
{
    const R = Math.max(80, Source.Radius || 160);
    if (Visualization)
    {
        Visualization.View.CentreX = Source.Centre[0];
        Visualization.View.CentreZ = Source.Centre[2];
        Visualization.View.Span = Clamp(R * 4.2, 160, 120000);
        Visualization.Invalidate();
    }
    if (Stage3D && ViewMode === 'Stage3D')
    {
        Stage3D.Controls?.target?.set(Source.Centre[0] * Stage3D.ScaleFactor, Source.Centre[1] * Stage3D.ScaleFactor, Source.Centre[2] * Stage3D.ScaleFactor);
    }
    C.Broadcast();
}

function JumpToSpan(Span)
{
    Scene.Visualization.Span = Span;
    if (Visualization) { Visualization.View.Span = Span; Visualization.Invalidate(); }
    SyncTopUi();
    Sync3DIfVisible();
    C.Broadcast();
}

function SwitchViewMode(Mode)
{
    ViewMode = Mode;
    const Canvas2D = document.getElementById('ViewportCanvas');
    const Mount3D = document.getElementById('Stage3DMount');
    const Minimap = document.getElementById('MinimapInset');
    if (Mode === 'Stage3D')
    {
        Canvas2D.hidden = true;
        Mount3D.hidden = false;
        Minimap.classList.remove('shown');
        if (!Stage3D)
        {
            Stage3D = new ForceField3DViewport(Mount3D, {
                OnSourcePick: (Token) => { if (Selected !== Token) { Selected = Token; ConstructInspectorPage(); C.Broadcast(); Sync3DIfVisible(); } },
                OnPlace: (X, Z) => { if (PlacementType) PlaceSourceAt(PlacementType, X, Z); }
            });
        }
        Stage3D.Resize();
        Sync3DIfVisible();
    }
    else
    {
        Mount3D.hidden = true;
        Canvas2D.hidden = false;
        Minimap.classList.add('shown');
        Scene.Visualization.Mode = 'Map';
        ViewMode = 'Map';
        if (Visualization) { Visualization.View.Mode = 'Map'; Visualization.Resize(); Visualization.Invalidate(); }
    }
    SyncTopUi();
    C.Broadcast();
}

function Sync3DIfVisible()
{
    if (Stage3D && ViewMode === 'Stage3D')
    {
        Stage3D.SyncScene(Scene, Selected, Clock.Time, {
            PlacementMode: Boolean(PlacementType),
            PlacementType: PlacementType || 'WindGenerator',
            Span: Visualization?.View?.Span || Scene.Visualization.Span || 1400
        });
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   VISUALIZATION WIREUP
//------------------------------------------------------------------------------------------------------------------------

function SampleField(Contribution, X, Y, Z, Time, CellSpan, Out)
{
    if (Prepared.Time !== Time || PreparedRevision !== SceneRevision)
    {
        Prepared = F.Prepare(Scene, Time);
        PreparedRevision = SceneRevision;
    }
    return F.EvaluatePrepared(Scene, Prepared, Contribution, X, Y, Z, CellSpan, Out, Scene.Visualization.Solo);
}

function DescribeSources()
{
    const Shown = Scene.Visualization.Contribution;
    const Now = F.Prepare(Scene, Clock.Time);
    return Now.Entries.map((Entry) =>
    {
        const Source = Entry.Source;
        const Dec = {
            Id: Source.Token, X: Entry.Centre[0], Y: Entry.Centre[1], Z: Entry.Centre[2],
            Contribution: Entry.Contribution, Colour: F.ResolveSourceColour(Source), Label: Source.Name, Active: Entry.Envelope,
            Selected: Selected === Source.Token, Dim: Entry.Contribution !== Shown,
            Shape: Source.Reach === 'Box' ? 'Box' : 'Circle', Radius: Source.Radius
        };
        if (Source.Reach === 'Box' || Source.Type === 'GustFront')
        {
            const Ext = F.ResolveBoxExtents(Source);
            const Turn = (Source.Bearing || 0) * Radian;
            Object.assign(Dec, {
                Shape: 'Box', HalfX: Ext.HalfX, HalfZ: Ext.HalfZ, Rotation: Source.Bearing || 0,
                OutlineX: Entry.Centre[0] + Math.sin(Turn) * Ext.OffsetZ,
                OutlineZ: Entry.Centre[2] - Math.cos(Turn) * Ext.OffsetZ,
                Bearing: Source.Type === 'Drag' ? undefined : Source.Bearing
            });
        }
        if (Source.Type === 'Blast' && Entry.Envelope > 0.001) Dec.Radius = Math.min(Source.Radius, Source.InitialRadius + Source.Expansion * Entry.Age);
        if (Source.Type === 'Tornado' || Source.Type === 'Thermal' || Source.Type === 'Microburst') { Dec.InnerRadius = Source.InnerRadius; Dec.Height = Source.Height; }
        if (Source.Type === 'WindGenerator' || Source.Type === 'Puff') Dec.Bearing = Source.Bearing;
        if (Source.Type === 'Tornado' && Source.TrackSpeed > 0)
        {
            const Turn = Source.TrackBearing * Radian, Travel = Source.TrackSpeed * Scene.Timeline.Duration;
            Dec.Track = [[Source.Centre[0], Source.Centre[2]], [Source.Centre[0] + Math.sin(Turn) * Travel, Source.Centre[2] - Math.cos(Turn) * Travel]];
        }
        return Dec;
    });
}

function ApplyVisualization()
{
    if (!Visualization) return;
    const Settings = Scene.Visualization;
    let Preview = null;
    if (PlacementType && PlacementDragPreview)
    {
        const T = F.TypeById[PlacementType] || F.TypeById.WindGenerator;
        Preview = {
            X: PlacementDragPreview.X, Z: PlacementDragPreview.Z,
            Radius: PlacementDragPreview.Radius || T.Defaults.Radius || 200,
            Shape: T.Reach === 'Box' ? 'Box' : 'Circle',
            HalfX: T.Defaults.HalfX || T.Defaults.Radius || 140,
            HalfZ: T.Defaults.HalfZ || T.Defaults.Radius || 140,
            Bearing: PlacementDragPreview.Bearing ?? T.Defaults.Bearing,
            Contribution: T.Contribution,
            Colour: F.ResolveSourceColour(PlacementType),
            Label: T.Name
        };
    }

    Visualization.Configure({
        AllowOblique: false,
        Quantity: Settings.Quantity,
        Contribution: Settings.Contribution,
        ScaleMode: Settings.ScaleMode,
        FixedScale: Settings.FixedScale,
        Heatmap: Settings.Heatmap,
        HeatmapStyle: Settings.HeatmapStyle,
        Opacity: Settings.Opacity,
        Arrows: Settings.Arrows,
        ArrowSpacing: Settings.ArrowSpacing,
        QuietCutoff: Settings.QuietCutoff ?? 0.03,
        FlowLines: Settings.FlowLines !== false,
        FlowLineSpacing: Settings.FlowLineSpacing ?? 46,
        FlowLineLength: Settings.FlowLineLength ?? 22,
        FlowLinePulse: Settings.FlowLinePulse !== false,
        Tracers: Boolean(Settings.Tracers),
        TracerCount: Settings.TracerCount,
        SliceHeight: Settings.SliceHeight,
        CellPixels: Settings.CellPixels,
        ShowSources: Settings.ShowInfluence !== false,
        ShowLabels: Settings.Labels,
        MinimalLabels: Boolean(Settings.MinimalLabels),
        ShowCascades: Settings.Cascades,
        Cascades: F.ResolveBudget(Scene).Levels.map((L, I) => ({ Extent: L.Extent, Cell: L.Cell, Label: `C${I} · ${FormatDistance(L.Extent)}` })),
        CascadeFocus: [Visualization.View.CentreX, Visualization.View.CentreZ],
        Sources: DescribeSources(),
        PlacementMode: Boolean(PlacementType),
        PlacementPreview: Preview
    });
}

function ConstructVisualization()
{
    if (!V) return;
    const Canvas = document.getElementById('ViewportCanvas');
    const Extent = Scene.Field.Extent / 2;
    Visualization = V.Create(Canvas, {
        Sample: SampleField, MinimumSpan: 40, MaximumSpan: 2.4e5,
        OnSourcePick: (Token) => { if (Selected !== Token) { Selected = Token; ConstructInspectorPage(); C.Broadcast(); } },
        OnSourceMove: (Token, X, Z) =>
        {
            const Source = SourceByToken(Token);
            if (!Source) return;
            const Entry = F.PrepareSource(Source, Scene.Wind, Clock.Time);
            const DX = Entry.Centre[0] - Source.Centre[0], DZ = Entry.Centre[2] - Source.Centre[2];
            Source.Centre = [Number((X - DX).toFixed(1)), Source.Centre[1], Number((Z - DZ).toFixed(1))];
            SceneRevision++;
            Visualization.Invalidate();
            AppendLog('FORCES', `${Source.Name}: moved`, `(${Math.round(Source.Centre[0])}, ${Math.round(Source.Centre[2])}) m`, Token + '.Move');
            C.Broadcast();
        },
        OnSourceRadiusChange: (Token, NewR) =>
        {
            const Source = SourceByToken(Token);
            if (!Source) return;
            Source.Radius = Math.round(Clamp(NewR, 10, 50000));
            if (Source.Reach === 'Box') { Source.HalfX = Math.round(Source.Radius * 0.8); Source.HalfZ = Math.round(Source.Radius * 0.8); }
            SceneRevision++;
            Visualization.Invalidate();
            AppendLog('INFLUENCE', `${Source.Name}: influence ${FormatDistance(Source.Radius)}`, null, Token + '.Radius');
            C.Broadcast();
        },
        OnSourceBearingChange: (Token, Deg) =>
        {
            const Source = SourceByToken(Token);
            if (!Source) return;
            if (Source.Type === 'Tornado') Source.TrackBearing = Math.round(Deg);
            else Source.Bearing = Math.round(Deg);
            SceneRevision++;
            Visualization.Invalidate();
            AppendLog('FORCES', `${Source.Name}: bearing ${Math.round(Deg)}°`, null, Token + '.Bearing');
            C.Broadcast();
        },
        OnPlacementHover: (X, Z, DragSpec) =>
        {
            PlacementDragPreview = Object.assign({ X, Z }, DragSpec || {});
        },
        OnPlace: (X, Z, DragBearing, DragRadius) =>
        {
            if (PlacementType) PlaceSourceAt(PlacementType, X, Z, DragBearing, DragRadius);
        },
        OnHover: (Point) => { HoverPoint = Point; },
        OnViewChange: () => { SyncTopUi(); }
    });
    Visualization.Configure({
        Extent: { MinimumX: -Extent, MinimumZ: -Extent, MaximumX: Extent, MaximumZ: Extent },
        Minimap: document.getElementById('MinimapCanvas')
    });
    Object.assign(Visualization.View, { Mode: 'Map', CentreX: 0, CentreZ: 0, Span: Scene.Visualization.Span || 1400, Yaw: Math.PI / 4, Pitch: 0.62 });
    Visualization.AttachNavigation();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        OUTLINER
//------------------------------------------------------------------------------------------------------------------------

function DescribeSourceMeta(Source)
{
    const Type = F.TypeById[Source.Type] || F.TypeById.WindGenerator;
    const Unit = F.Contributions[Type.Contribution].Unit;
    return `${Type.Contribution} · ${Number(Source.Strength.toFixed(1))} ${Unit} · R ${FormatDistance(Source.Radius)}`;
}

function ConstructTreeRow(Host, Row)
{
    const Meta = C.Construct('div', 'row-meta');
    const Glyph = C.Construct('div', 'row-glyph', C.ConstructGlyph(Row.Glyph));
    Glyph.style.color = Row.Colour;
    Glyph.style.background = Row.Colour + '22';
    const Status = C.Construct('span', 'row-status');
    const Toggle = C.Construct('button', 'row-toggle', C.ConstructGlyph('Eye'));
    Toggle.type = 'button';
    const TreeRow = C.Construct('div', 'tree-row' + (Row.Depth === 1 ? ' nested' : ''), [
        Glyph,
        C.Construct('div', 'row-text', [C.Construct('div', 'row-name', Row.Name()), Meta]),
        Row.Status ? Status : null,
        Toggle
    ]);
    TreeRow.style?.setProperty?.('--row-accent', Row.Colour);
    TreeRow.addEventListener('click', () => { Selected = Row.Id; ConstructInspectorPage(); C.Broadcast(); Sync3DIfVisible(); });
    Toggle.addEventListener('click', (E) => { E.stopPropagation(); Row.Flip(); C.Broadcast(); Sync3DIfVisible(); });
    C.Register(() =>
    {
        TreeRow.querySelector('.row-name').textContent = Row.Name();
        Meta.textContent = Row.Meta();
        const Enabled = Row.Enabled();
        TreeRow.classList.toggle('selected', Selected === Row.Id);
        TreeRow.classList.toggle('muted-row', !Enabled);
        Toggle.textContent = '';
        Toggle.append(C.ConstructGlyph(Enabled ? 'Eye' : 'EyeOff'));
        const Hidden = OutlinerQuery && !Row.Name().toLowerCase().includes(OutlinerQuery) && Row.Depth > 0 && Row.Id !== 'Wind';
        TreeRow.style.display = Hidden ? 'none' : '';
    });
    if (Row.Status) LiveOutlinerRows.push(() => { const St = Row.Status(); Status.textContent = St; Status.className = 'row-status ' + St.toLowerCase(); });
    Host.append(TreeRow);
}

function ConstructOutliner()
{
    if (OutlinerScope) C.ReleaseScope(OutlinerScope);
    OutlinerScope = C.OpenScope();
    LiveOutlinerRows.length = 0;
    const Host = document.getElementById('OutlinerContent');
    if (!Host) return;
    Host.textContent = '';

    const ActiveEl = C.Construct('strong', null, '0');
    const WaitingEl = C.Construct('strong', null, '0');
    Host.append(C.Construct('div', 'outliner-metrics', [
        C.Construct('div', 'mini-card', [ActiveEl, C.Construct('span', null, 'Acting now')]),
        C.Construct('div', 'mini-card', [WaitingEl, C.Construct('span', null, 'Waiting or off')])
    ]));

    const Search = C.Construct('input', 'search-box');
    Search.placeholder = 'Search forcefields & wind…';
    Search.value = OutlinerQuery;
    Search.addEventListener('input', () => { OutlinerQuery = Search.value.trim().toLowerCase(); C.Broadcast(); });
    Host.append(Search);

    Host.append(C.Construct('div', 'outliner-group', 'Master & Ambient Wind'));
    ConstructTreeRow(Host, {
        Id: 'Field', Depth: 0, Glyph: 'Field', Colour: '#8fd3c4',
        Name: () => 'Force Field Studio',
        Meta: () => `${Scene.Sources.length} bounded fields · ${Scene.Visualization.FlowLines !== false ? 'Flow lines' : 'Vectors'}`,
        Enabled: () => Scene.Field.Enabled,
        Flip: () => { Scene.Field.Enabled = !Scene.Field.Enabled; SceneRevision++; Visualization?.Invalidate(); }
    });
    ConstructTreeRow(Host, {
        Id: 'Wind', Depth: 1, Glyph: 'Wind', Colour: '#8fd3c4',
        Name: () => 'Ambient Wind',
        Meta: () => Scene.Wind.Enabled ? `${Scene.Wind.Speed.toFixed(1)} m/s @ ${Math.round(Scene.Wind.Bearing)}°${Scene.Wind.Bounded ? ' (bounded)' : ''}` : 'Off · Placed generators only',
        Enabled: () => Scene.Wind.Enabled,
        Flip: () => { Scene.Wind.Enabled = !Scene.Wind.Enabled; SceneRevision++; Visualization?.Invalidate(); }
    });

    const FlowSources = Scene.Sources.filter((S) => F.TypeById[S.Type]?.Contribution === 'Flow');
    const OtherSources = Scene.Sources.filter((S) => F.TypeById[S.Type]?.Contribution !== 'Flow');

    if (FlowSources.length)
    {
        Host.append(C.Construct('div', 'outliner-group', `Wind & Flow Generators (${FlowSources.length})`));
        for (const S of FlowSources)
        {
            const Type = F.TypeById[S.Type];
            ConstructTreeRow(Host, {
                Id: S.Token, Depth: 1, Glyph: Type.Glyph, Colour: F.ResolveSourceColour(S),
                Name: () => S.Name,
                Meta: () => DescribeSourceMeta(S),
                Enabled: () => S.Enabled,
                Status: () => (Prepared.Entries.find((E) => E.Source === S)?.Status || F.ResolveActivation(S, Clock.Time).Status),
                Flip: () => { S.Enabled = !S.Enabled; SceneRevision++; Visualization?.Invalidate(); }
            });
        }
    }

    if (OtherSources.length)
    {
        Host.append(C.Construct('div', 'outliner-group', `Accelerate & Damp Zones (${OtherSources.length})`));
        for (const S of OtherSources)
        {
            const Type = F.TypeById[S.Type];
            const Col = F.ResolveSourceColour(S);
            ConstructTreeRow(Host, {
                Id: S.Token, Depth: 1, Glyph: Type.Glyph, Colour: Col,
                Name: () => S.Name,
                Meta: () => DescribeSourceMeta(S),
                Enabled: () => S.Enabled,
                Status: () => (Prepared.Entries.find((E) => E.Source === S)?.Status || F.ResolveActivation(S, Clock.Time).Status),
                Flip: () => { S.Enabled = !S.Enabled; SceneRevision++; Visualization?.Invalidate(); }
            });
        }
    }

    LiveOutlinerRows.push(() =>
    {
        let Active = Scene.Wind.Enabled ? 1 : 0, Wait = Scene.Wind.Enabled ? 0 : 1;
        for (const S of Scene.Sources)
        {
            const St = F.ResolveActivation(S, Clock.Time).Status;
            if (St === 'Active') Active++; else Wait++;
        }
        ActiveEl.textContent = String(Active);
        WaitingEl.textContent = String(Wait);
        const StatusFooter = document.getElementById('OutlinerStatus');
        if (StatusFooter) StatusFooter.textContent = `${Scene.Sources.length} bounded fields · zero infinite leakage`;
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      TIMELINE
//------------------------------------------------------------------------------------------------------------------------

function ConstructTimeline()
{
    const Strip = document.getElementById('TimelineStrip');
    if (!Strip) return;
    Strip.textContent = '';

    const Transport = C.Construct('div', 'timeline-transport');
    PlayButton = C.Construct('button', 'round-btn' + (Clock.Playing ? ' on' : ''), C.ConstructGlyph(Clock.Playing ? 'Pause' : 'Play'));
    PlayButton.type = 'button';
    PlayButton.addEventListener('click', TogglePlay);
    ClockReading = C.Construct('div', 'timeline-clock');

    const SpeedRow = C.Construct('div', 'transport-row');
    SpeedRow.append(PlayButton, ClockReading);
    for (const Rate of [1, 10])
    {
        const Btn = C.Construct('button', 'tool-pill' + (Scene.Timeline.Rate === Rate ? ' is-placing' : ''), `×${Rate}`);
        Btn.type = 'button';
        Btn.addEventListener('click', () => { Scene.Timeline.Rate = Rate; ConstructTimeline(); });
        SpeedRow.append(Btn);
    }
    Transport.append(SpeedRow);
    Strip.append(Transport);

    const Tracks = C.Construct('div', 'timeline-tracks');
    const Ruler = C.Construct('div', 'timeline-ruler');
    const Duration = Scene.Timeline.Duration || 60;
    for (let P = 0; P <= 4; ++P)
    {
        const Tick = C.Construct('div', 'tick', C.Construct('span', null, `${Math.round((P / 4) * Duration)}s`));
        Tick.style.left = `${(P / 4) * 100}%`;
        Ruler.append(Tick);
    }
    const Scrub = (E) =>
    {
        const R = Ruler.getBoundingClientRect();
        Clock.Time = Clamp((E.clientX - R.left) / Math.max(1, R.width), 0, 1) * Duration;
        SceneRevision++;
        Visualization?.Invalidate();
        RefreshPlayhead();
        Sync3DIfVisible();
    };
    let DragRuler = false;
    Ruler.addEventListener('pointerdown', (E) => { DragRuler = true; Ruler.setPointerCapture(E.pointerId); Scrub(E); });
    Ruler.addEventListener('pointermove', (E) => { if (DragRuler) Scrub(E); });
    Ruler.addEventListener('pointerup', () => { DragRuler = false; });

    const Lanes = C.Construct('div', 'timeline-lanes');
    const Timed = Scene.Sources.filter((S) => S.Activation !== 'Always');
    for (const S of Timed)
    {
        const Track = C.Construct('div', 'lane-track');
        const Contrib = (F.TypeById[S.Type]?.Contribution || 'Flow').toLowerCase();
        const Windows = [];
        if (S.Activation === 'Window')
        {
            const Len = S.Lasts > 0 ? S.Lasts : Duration;
            const Rep = S.Repeats > 0 ? S.Repeats : Duration * 2;
            for (let T = S.Begins; T < Duration; T += Rep) Windows.push([T, Math.min(Duration, T + Len)]);
        }
        else if (S.TriggeredAt !== null && S.TriggeredAt !== undefined)
        {
            Windows.push([S.TriggeredAt, S.Lasts > 0 ? Math.min(Duration, S.TriggeredAt + S.Lasts) : Duration]);
        }
        for (const [A, B] of Windows)
        {
            const Win = C.Construct('i', 'lane-window ' + Contrib);
            Win.style.left = `${(A / Duration) * 100}%`;
            Win.style.width = `${Math.max(1, ((B - A) / Duration) * 100)}%`;
            Track.append(Win);
        }
        const Fire = C.Construct('button', 'lane-fire', S.Activation === 'Trigger' ? 'FIRE' : '');
        Fire.type = 'button';
        Fire.addEventListener('click', (E) => { E.stopPropagation(); FireSource(S); });
        const Lane = C.Construct('div', 'timeline-lane' + (Selected === S.Token ? ' selected' : ''), [C.Construct('span', 'lane-name', S.Name), Track, Fire]);
        Lane.addEventListener('click', () => { Selected = S.Token; ConstructInspectorPage(); C.Broadcast(); });
        Lanes.append(Lane);
    }
    if (!Timed.length) Lanes.append(C.Construct('div', 'timeline-hint', 'All active forcefields are steady · assign Window or Trigger timing in Inspector.'));

    PlayheadMark = C.Construct('div', 'timeline-playhead');
    Tracks.append(Ruler, Lanes, PlayheadMark);
    Strip.append(Tracks);
    RefreshPlayhead();
}

function RefreshPlayhead()
{
    if (!PlayheadMark || !ClockReading) return;
    const Duration = Scene.Timeline.Duration || 60;
    const Ruler = PlayheadMark.parentElement?.querySelector('.timeline-ruler');
    if (Ruler) PlayheadMark.style.left = `${(Clock.Time / Duration) * 100}%`;
    ClockReading.textContent = '';
    ClockReading.append(document.createTextNode(`${Clock.Time.toFixed(1)} s`), C.Construct('small', null, ` / ${Duration}s`));
}

function TogglePlay()
{
    Clock.Playing = !Clock.Playing;
    if (PlayButton) { PlayButton.classList.toggle('on', Clock.Playing); PlayButton.textContent = ''; PlayButton.append(C.ConstructGlyph(Clock.Playing ? 'Pause' : 'Play')); }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSPECTOR PAGES
//------------------------------------------------------------------------------------------------------------------------

function ConstructInspectorPage()
{
    if (PageScope) C.ReleaseScope(PageScope);
    PageScope = C.OpenScope();
    LiveRefreshers.length = 0;

    const Page = document.getElementById('InspectorContent');
    if (!Page) return;
    Page.textContent = '';

    if (Selected === 'Field') ConstructFieldPage(Page);
    else if (Selected === 'Wind') ConstructWindPage(Page);
    else
    {
        const Source = SourceByToken(Selected);
        if (Source) ConstructSourcePage(Page, Source);
        else { Selected = 'Field'; ConstructFieldPage(Page); }
    }
}

function ConstructFieldPage(Page)
{
    const Viz = Scene.Visualization;
    Page.append(
        C.Construct('div', 'breadcrumb-row', 'Inspector / Master Studio'),
        C.Construct('div', 'page-title', [C.Construct('h2', null, 'Force Field Studio'), C.Construct('span', 'live-pill', 'LIVE')]),
        C.Construct('div', 'page-subtitle', 'Master spatial field: clean RK2 flow lines, sparse vector field, and finite influence envelopes.')
    );

    C.PillTabs(Page, {
        Options: [['Flow · m/s', 'Flow'], ['Accelerate · m/s²', 'Accelerate'], ['Damp · 1/s', 'Damp']],
        Read: () => Viz.Contribution,
        Write: (Cont) => { Viz.Contribution = Cont; if (Cont === 'Damp') Viz.Quantity = 'Speed'; PeakSamples.length = 0; SceneRevision++; Visualization?.Invalidate(); Sync3DIfVisible(); }
    });

    const Mark = C.Construct('div', 'mark live', C.ConstructGlyph('Check'));
    const BText = C.Construct('span', null, 'Calm');
    const Metric = C.Construct('div', 'metric-big');
    const Side = C.Construct('div', 'metric-side');
    const Card = C.Construct('div', 'hero-card', [
        C.Construct('div', 'hero-top', [C.Construct('div', 'budget-chip', [Mark, BText]), C.Construct('div', 'estimate-tag', 'In view peak')]),
        C.Construct('div', 'hero-metrics', [Metric, Side])
    ]);
    Page.append(Card);

    const Series = C.TimeSeries(Card, { Samples: PeakSamples, Capacity: SampleCapacity, Target: 17.2, Height: 90, Headroom: 1.25 });
    Card.append(C.Construct('div', 'hero-note', 'Quiet dead zones outside forcefield influence envelopes are cleanly omitted so the workspace stays legible.'));

    LiveRefreshers.push(() =>
    {
        const St = Visualization?.Statistics || { Peak: 0, CellSpan: 2, Streamlines: 0 };
        const Unit = V ? V.ResolveUnit(Viz.Contribution, Viz.Quantity) : 'm/s';
        C.ConstructMetric(Metric, St.Peak, St.Peak >= 100 ? 0 : 1, Unit);
        Side.textContent = `${FormatDistance(Visualization?.View?.Span || 1400)} span · ${St.Streamlines || 0} flow lines`;
        if (Viz.Contribution === 'Flow')
        {
            const B = F.ResolveBeaufort(St.Peak);
            BText.textContent = `Beaufort ${B.Force} · ${B.Name}`;
            Mark.className = 'mark ' + (B.Force >= 10 ? 'danger' : B.Force >= 8 ? 'warm' : 'live');
        }
        else { BText.textContent = `${Viz.Contribution} peak`; Mark.className = 'mark live'; }
        Series.Refresh();
    });

    const Grid = C.Construct('div', 'toggle-grid');
    C.ToggleTile(Grid, Bind(Viz, 'FlowLines', { Label: 'Flow lines', Glyph: 'FlowLine' }, 'VIEW', 'Field'));
    C.ToggleTile(Grid, Bind(Viz, 'Arrows', { Label: 'Vector field', Glyph: 'Move' }, 'VIEW', 'Field'));
    C.ToggleTile(Grid, Bind(Viz, 'ShowInfluence', { Label: 'Influence', Glyph: 'Target' }, 'VIEW', 'Field'));
    C.ToggleTile(Grid, Bind(Viz, 'Heatmap', { Label: 'Heatmap', Glyph: 'Layers' }, 'VIEW', 'Field'));
    C.ToggleTile(Grid, Bind(Viz, 'MinimalLabels', { Label: 'Clean labels', Glyph: 'Eye' }, 'VIEW', 'Field'));
    C.ToggleTile(Grid, Bind(Scene.Wind, 'Enabled', { Label: 'Ambient wind', Glyph: 'Wind' }, 'WIND', 'Wind'));
    Page.append(Grid);

    const CleanSec = C.SectionCard(Page, { Id: 'declutter', Title: 'Vectors & Flow Lines (Anti-Clutter)', Glyph: 'FlowLine', Status: () => `${Viz.ArrowSpacing}px · ${Viz.FlowLineSpacing ?? 46}px` });
    C.SegmentedRow(CleanSec.Inner, Bind(Viz, 'Quantity', { Label: 'Mapped scalar', Options: [['Speed', 'Speed'], ['Vertical', 'Vertical'], ['Vorticity ω', 'Vorticity'], ['Divergence ∇·v', 'Divergence']] }, 'VIEW', 'View'));
    C.SliderRow(CleanSec.Inner, Bind(Viz, 'ArrowSpacing', { Label: 'Vector arrow spacing', Minimum: 30, Maximum: 96, Step: 2, Digits: 0, Unit: 'px', Labels: ['denser', 'cleaner'] }, 'VIEW', 'View'));
    C.SliderRow(CleanSec.Inner, Bind(Viz, 'FlowLineSpacing', { Label: 'Flow line separation', Minimum: 24, Maximum: 88, Step: 2, Digits: 0, Unit: 'px', Labels: ['more lines', 'minimal'] }, 'VIEW', 'View'));
    C.SliderRow(CleanSec.Inner, Bind(Viz, 'FlowLineLength', { Label: 'Flow line arc length', Minimum: 10, Maximum: 40, Step: 2, Digits: 0, Unit: 'steps' }, 'VIEW', 'View'));
    C.SliderRow(CleanSec.Inner, Bind(Viz, 'QuietCutoff', { Label: 'Quiet cutoff threshold', Minimum: 0, Maximum: 0.2, Step: 0.005, Digits: 3, Unit: '×', Labels: ['draw all', 'skip weak'] }, 'VIEW', 'View'));
    C.SliderRow(CleanSec.Inner, DistanceBinding(Viz, 'SliceHeight', { Label: 'Slice elevation Y', Minimum: 1, Maximum: 2000 }, 'VIEW', 'View'));
    C.SwitchRow(CleanSec.Inner, Bind(Viz, 'FlowLinePulse', { Label: 'Animate directional pulses', Detail: 'Subtle moving packets along streamlines show speed and direction.' }, 'VIEW', 'View'));
    C.SwitchRow(CleanSec.Inner, Bind(Viz, 'Cascades', { Label: 'Show cascade boundaries' }, 'VIEW', 'View'));

    const ChSec = C.SectionCard(Page, { Id: 'receivers', Title: 'Receivers & Budget', Glyph: 'Sparkles', Status: () => `${Scene.Field.Channels.length} channels` });
    C.ChipToggles(ChSec.Inner, Bind(Scene.Field, 'Channels', { Label: 'Coupled receivers', Options: F.Channels, Detail: (K) => `${K.length} receiver types sample this field.` }, 'FORCES', 'Field'));
    const Budget = F.ResolveBudget(Scene);
    C.StatGrid(ChSec.Inner, [
        ['Baked flow slots', () => `${Budget.Baked.length} / 16`],
        ['Live accel / damp', () => `${Budget.Live.length + Budget.Damp.length}`],
        ['Active sources', () => `${Scene.Sources.filter((S) => S.Enabled).length}`],
        ['All bounded?', () => '100% Yes']
    ]);
    C.ActionRow(ChSec.Inner, [{ Label: 'Place new forcefield / wind…', Glyph: 'Plus', Tone: 'accent', Act: OpenLibrary }]);

    const LvlSec = C.SectionCard(Page, { Id: 'lvl', Title: 'Levelspace Export', Glyph: 'Copy', Status: () => 'TOML' });
    LvlSec.Card.classList.add('folded');
    const Pre = C.Construct('pre', 'levelspace-pre');
    C.ActionRow(LvlSec.Inner, [
        { Label: 'Copy Levelspace', Glyph: 'Copy', Act: () => { navigator.clipboard?.writeText(F.EncodeLevelspace(Scene).join('\n')); C.Toast('Copied Levelspace records'); } },
        { Label: 'Reset Preset', Glyph: 'Reset', Act: () => LoadScenario(Scene.Scenario || 'lab') }
    ]);
    LvlSec.Inner.append(Pre);
    C.Register(() => { Pre.textContent = F.EncodeLevelspace(Scene).join('\n'); });
}

function ConstructWindPage(Page)
{
    const Wind = Scene.Wind;
    Page.append(
        C.Construct('div', 'breadcrumb-row', 'Inspector / Ambient Wind'),
        C.Construct('div', 'page-title', [C.Construct('h2', null, 'Ambient Wind'), C.Construct('span', 'guest-pill', 'GUEST')]),
        C.Construct('div', 'page-subtitle', 'Regional atmospheric background profile (disabled by default in Lab so placed Wind Generators stand out cleanly).')
    );

    const Metric = C.Construct('div', 'metric-big');
    const Side = C.Construct('div', 'metric-side');
    const Card = C.Construct('div', 'hero-card', [
        C.Construct('div', 'hero-top', [C.Construct('div', 'budget-chip', [C.Construct('span', null, 'Wind at 10 m')]), C.Construct('div', 'estimate-tag', 'PlanetaryWindProfile')]),
        C.Construct('div', 'hero-metrics', [Metric, Side])
    ]);
    Page.append(Card);
    const Series = C.TimeSeries(Card, { Samples: WindSamples, Capacity: SampleCapacity, Target: () => Wind.Speed, Height: 86 });
    LiveRefreshers.push(() =>
    {
        const Spd = Wind.Enabled ? F.WindSpeedAt(Wind, Scene.Field, 0, Wind.GroundHeight + 10, 0, Clock.Time) : 0;
        C.ConstructMetric(Metric, Spd, 1, 'm/s');
        Side.textContent = Wind.Enabled ? `From ${Wrap360(Wind.Bearing + 180).toFixed(0)}° ${C.ResolveCompassPoint(Wind.Bearing + 180)}` : 'Ambient wind OFF';
        Series.Refresh();
    });

    const FlowSec = C.SectionCard(Page, { Id: 'wflow', Title: 'Ambient Direction & Bounded Region', Glyph: 'Compass', Status: () => Wind.Enabled ? `${Wind.Speed.toFixed(1)} m/s` : 'Off' });
    C.SwitchRow(FlowSec.Inner, Bind(Wind, 'Enabled', { Label: 'Enable ambient wind', Detail: 'Turn off to isolate placed Wind Generators & Forcefields.' }, 'WIND', 'Wind'));
    C.SwitchRow(FlowSec.Inner, Bind(Wind, 'Bounded', { Label: 'Finite regional radius', Detail: 'Bounds ambient wind smoothly around origin instead of infinite space.' }, 'WIND', 'Wind'));
    C.SliderRow(FlowSec.Inner, DistanceBinding(Wind, 'RegionRadius', { Label: 'Regional radius', Minimum: 100, Maximum: 50000, Availability: () => ({ Enabled: Boolean(Wind.Bounded) }) }, 'WIND', 'Wind'));
    C.PolarDial(FlowSec.Inner, Bind(Wind, 'Bearing', { Label: 'Travel bearing', Digits: 0, Unit: '°' }, 'WIND', 'Wind'));
    C.SliderRow(FlowSec.Inner, Bind(Wind, 'Speed', { Label: 'Base speed', Minimum: 0, Maximum: 40, Step: 0.5, Digits: 1, Unit: 'm/s' }, 'WIND', 'Wind'));

    const GustSec = C.SectionCard(Page, { Id: 'wgst', Title: 'Gusts, Turbulence & Ekman Veer', Glyph: 'Activity', Status: () => `${(Wind.GustStrength * 100).toFixed(0)}%` });
    C.SliderRow(GustSec.Inner, Bind(Wind, 'GustStrength', { Label: 'Gust amplitude', Minimum: 0, Maximum: 1, Step: 0.02, Digits: 2, Unit: '×' }, 'WIND', 'Wind'));
    C.SliderRow(GustSec.Inner, Bind(Wind, 'GustFrequency', { Label: 'Gust frequency', Minimum: 0.01, Maximum: 1.5, Step: 0.01, Digits: 2, Unit: 'Hz' }, 'WIND', 'Wind'));
    C.SliderRow(GustSec.Inner, Bind(Wind, 'Turbulence', { Label: 'Curl eddies', Minimum: 0, Maximum: 1.5, Step: 0.02, Digits: 2 }, 'WIND', 'Wind'));
    C.SliderRow(GustSec.Inner, Bind(Wind, 'Shear', { Label: 'Ekman shear (/km)', Minimum: 0, Maximum: 0.8, Step: 0.02, Digits: 2 }, 'WIND', 'Wind'));
    C.SliderRow(GustSec.Inner, Bind(Wind, 'Veer', { Label: 'Ekman veer (°/km)', Minimum: -30, Maximum: 30, Step: 1, Digits: 0, Unit: '°' }, 'WIND', 'Wind'));
}

function ConstructSourcePage(Page, Source)
{
    const Type = F.TypeById[Source.Type] || F.TypeById.WindGenerator;
    const Col = F.ResolveSourceColour(Source);

    Page.append(C.Construct('div', 'breadcrumb-row', `Inspector / ${Type.Contribution} / ${Type.Native}`));

    const SymbolBox = C.Construct('div', 'object-symbol', C.ConstructGlyph(Type.Glyph));
    SymbolBox.style.color = Col;
    SymbolBox.style.background = Col + '20';
    SymbolBox.style.borderColor = Col + '44';

    const NameInput = C.Construct('input', 'object-name-input');
    NameInput.type = 'text';
    NameInput.value = Source.Name;
    NameInput.addEventListener('change', () => { Source.Name = NameInput.value.trim() || Type.Name; ConstructOutliner(); ConstructTimeline(); C.Broadcast(); });

    const ObjectCard = C.Construct('div', 'object-heading', [
        SymbolBox,
        C.Construct('div', 'object-heading-copy', [NameInput, C.Construct('span', null, `${Type.Native} · ${Type.Contribution} · ${Source.Reach}`)]),
        C.Construct('span', 'entity-glow-pill', 'BOUNDED')
    ]);
    ObjectCard.style?.setProperty?.('--entity-glow', Col);
    Page.append(ObjectCard, C.Construct('div', 'page-subtitle', Type.Blurb));

    // Live radial influence profile plot (clearly drops to 0.00 at influence boundary!)
    const Metric = C.Construct('div', 'metric-big');
    const Side = C.Construct('div', 'metric-side');
    const Card = C.Construct('div', 'hero-card', [
        C.Construct('div', 'hero-top', [C.Construct('div', 'budget-chip', [C.Construct('span', null, 'Bounded Spatial Influence Profile')]), C.Construct('div', 'estimate-tag', `${Source.Reach} · ${Source.Falloff}`)]),
        C.Construct('div', 'hero-metrics', [Metric, Side])
    ]);
    Page.append(Card);

    C.CurvePlot(Card, {
        Height: 112,
        Resolve: () =>
        {
            const Prof = F.ResolveProfile(Scene, Source, Scene.Visualization.SliceHeight);
            const MaxY = Math.max(1, ...Prof.Points.map((P) => P[1])) * 1.15;
            return {
                XRange: [0, Prof.Reach],
                YRange: [0, MaxY],
                Curves: [{ Points: Prof.Points, Stroke: Col, Fill: Col + '26' }],
                Guides: [{ Y: 0, Text: `0 outside R = ${FormatDistance(Source.Radius)}` }],
                XTicks: [[0, '0'], [Prof.Reach * 0.5, FormatDistance(Prof.Reach * 0.5)], [Prof.Reach, FormatDistance(Prof.Reach)]],
                YTicks: [[0, '0'], [MaxY * 0.5, (MaxY * 0.5).toFixed(1)], [MaxY, MaxY.toFixed(1)]],
                Caption: `Finite reach: strictly 0 ${Prof.Unit} beyond boundary`
            };
        }
    });

    LiveRefreshers.push(() =>
    {
        C.ConstructMetric(Metric, Source.Strength, Source.Strength < 10 ? 2 : 1, F.Contributions[Type.Contribution].Unit);
        Side.textContent = `R ${FormatDistance(Source.Radius)} · ${Source.Falloff}`;
    });

    const Grid = C.Construct('div', 'toggle-grid');
    C.ToggleTile(Grid, Bind(Source, 'Enabled', { Label: 'Enabled', Glyph: 'Check' }, 'FORCES', Source.Name));
    C.ToggleTile(Grid, { Label: 'Solo field', Glyph: 'Target', Read: () => Scene.Visualization.Solo === Source.Token, Write: (On) => { Scene.Visualization.Solo = On ? Source.Token : null; SceneRevision++; Visualization?.Invalidate(); Sync3DIfVisible(); } });
    C.ToggleTile(Grid, { Label: 'Follow', Glyph: 'Compass', Read: () => Scene.Visualization.Follow === Source.Token, Write: (On) => { Scene.Visualization.Follow = On ? Source.Token : null; } });
    Page.append(Grid);

    C.ActionRow(Page, [
        { Label: 'Frame', Glyph: 'Ruler', Act: () => FrameSource(Source) },
        { Label: 'Duplicate', Glyph: 'Copy', Act: () => DuplicateSource(Source) },
        { Label: 'Remove', Glyph: 'Trash', Tone: 'danger', Act: () => RemoveSource(Source.Token) }
    ]);

    // Section 1: Placement & Finite Spatial Influence
    const PlaceSec = C.SectionCard(Page, { Id: 'place', Title: 'Placement & Finite Influence', Glyph: 'Target', Status: () => `${Source.Reach} · ${FormatDistance(Source.Radius)}` });
    C.VectorRow(PlaceSec.Inner, Bind(Source, 'Centre', { Label: 'Centre position (m)', Step: 5, Digits: 1 }, 'FORCES', Source.Name));
    C.SegmentedRow(PlaceSec.Inner, Bind(Source, 'Reach', { Label: 'Influence shape (finite only)', Options: F.Reaches }, 'INFLUENCE', Source.Name));
    C.SliderRow(PlaceSec.Inner, DistanceBinding(Source, 'Radius', { Label: 'Influence radius', Minimum: 10, Maximum: 40000 }, 'INFLUENCE', Source.Name));
    if (Source.Reach === 'Column' || Source.Reach === 'Box')
    {
        C.SliderRow(PlaceSec.Inner, DistanceBinding(Source, 'Height', { Label: 'Vertical height', Minimum: 5, Maximum: 15000 }, 'INFLUENCE', Source.Name));
    }
    if (Source.Reach === 'Box' && Source.Type !== 'Jet' && Source.Type !== 'Wake')
    {
        C.SliderRow(PlaceSec.Inner, DistanceBinding(Source, 'HalfX', { Label: 'Box half-width X', Minimum: 5, Maximum: 30000 }, 'INFLUENCE', Source.Name));
        C.SliderRow(PlaceSec.Inner, DistanceBinding(Source, 'HalfZ', { Label: 'Box half-length Z', Minimum: 5, Maximum: 30000 }, 'INFLUENCE', Source.Name));
    }
    C.SegmentedRow(PlaceSec.Inner, Bind(Source, 'Falloff', { Label: 'Boundary falloff', Options: F.Falloffs }, 'INFLUENCE', Source.Name));

    // Section 2: Strength & Bearing
    const StrSec = C.SectionCard(Page, { Id: 'strength', Title: 'Strength & Direction', Glyph: Type.Glyph, Status: () => `${Number(Source.Strength.toFixed(1))} ${F.Contributions[Type.Contribution].Unit}` });
    for (const [BKey, BLabel] of Type.Bearings || [])
    {
        C.PolarDial(StrSec.Inner, Bind(Source, BKey, { Label: BLabel, Digits: 0, Unit: '°' }, 'FORCES', Source.Name));
    }
    if (Type.Spin)
    {
        C.SegmentedRow(StrSec.Inner, Bind(Source, 'Spin', { Label: 'Rotation', Options: [['Cyclonic (+)', 1], ['Anticyclonic (−)', -1]] }, 'FORCES', Source.Name));
    }
    for (const Param of Type.Parameters) C.SliderRow(StrSec.Inner, ParameterBinding(Source, Param));

    // Section 3: Timing & Receivers
    const TimeSec = C.SectionCard(Page, { Id: 'timing', Title: 'Timing & Receivers', Glyph: 'Clock', Status: () => Source.Activation });
    C.SegmentedRow(TimeSec.Inner, Bind(Source, 'Activation', { Label: 'Activation mode', Options: F.Activations }, 'TIMELINE', Source.Name));
    if (Source.Activation !== 'Always')
    {
        if (Source.Activation === 'Window')
        {
            C.SliderRow(TimeSec.Inner, Bind(Source, 'Begins', { Label: 'Begins', Minimum: 0, Maximum: 120, Step: 0.5, Digits: 1, Unit: 's' }, 'TIMELINE', Source.Name));
            C.SliderRow(TimeSec.Inner, Bind(Source, 'Repeats', { Label: 'Repeats (0 = once)', Minimum: 0, Maximum: 180, Step: 1, Digits: 0, Unit: 's' }, 'TIMELINE', Source.Name));
        }
        C.SliderRow(TimeSec.Inner, Bind(Source, 'Lasts', { Label: 'Duration (0 = forever)', Minimum: 0, Maximum: 120, Step: 0.5, Digits: 1, Unit: 's' }, 'TIMELINE', Source.Name));
        C.SliderRow(TimeSec.Inner, Bind(Source, 'RampIn', { Label: 'Ramp in', Minimum: 0, Maximum: 10, Step: 0.1, Digits: 2, Unit: 's' }, 'TIMELINE', Source.Name));
        C.SliderRow(TimeSec.Inner, Bind(Source, 'RampOut', { Label: 'Ramp out', Minimum: 0, Maximum: 10, Step: 0.1, Digits: 2, Unit: 's' }, 'TIMELINE', Source.Name));
        if (Source.Activation === 'Trigger')
        {
            C.ActionRow(TimeSec.Inner, [{ Label: 'FIRE PULSE NOW', Glyph: 'Zap', Tone: 'accent', Act: () => FireSource(Source) }]);
        }
    }
    C.ChipToggles(TimeSec.Inner, Bind(Source, 'Acts', { Label: 'Filter receivers (empty = all enabled)', Options: F.Channels, Detail: (K) => K.length ? `Restricted to ${K.join(', ')}` : 'Acts on all enabled receivers within influence.' }, 'FORCES', Source.Name));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  LIBRARY SHEET MODAL
//------------------------------------------------------------------------------------------------------------------------

function OpenLibrary()
{
    // The 17-type library moved out to the shared Asset Browser.
    location.href = '../AssetBrowser/?ws=Force%20Fields';
}

/* Asset Browser deep link: ?asset=forcefield/<TypeId> arms placement for that type. */
function HandleAssetDeepLink()
{
    const Raw = new URLSearchParams(location.search).get('asset') || '';
    const M = Raw.match(/^forcefield\/([A-Za-z]+)$/);
    if (!M) return;
    if (F.TypeById[M[1]]) StartPlacement(M[1]);
    else AppendLog('PLACE', `Asset \u201C${Raw}\u201D is not a forcefield type`, null, 'Place');
}

function CloseLibrary()
{
    const Veil = document.getElementById('LibraryVeil');
    if (Veil) Veil.hidden = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TOP HUD & BOOT
//------------------------------------------------------------------------------------------------------------------------

function SyncTopUi()
{
    document.querySelectorAll('#ScenarioTabs .doc-tab').forEach((Btn) => Btn.classList.toggle('is-active', Btn.dataset.scenario === Scene.Scenario));
    document.querySelectorAll('#QuickPlaceGroup [data-place-type]').forEach((Btn) =>
    {
        const Glow = F.ResolveSourceColour(Btn.dataset.placeType);
        Btn.style?.setProperty?.('--pill-glow', Glow);
        Btn.classList.toggle('is-placing', Btn.dataset.placeType === PlacementType);
    });
    document.querySelectorAll('#ViewportBar [data-mode]').forEach((Btn) => Btn.classList.toggle('is-active', Btn.dataset.mode === ViewMode));
    const CurSpan = Visualization?.View?.Span || Scene.Visualization.Span || 1400;
    document.querySelectorAll('#SpanPresetGroup [data-span]').forEach((Btn) => Btn.classList.toggle('is-active', Math.abs(Number(Btn.dataset.span) - CurSpan) < CurSpan * 0.25));

    const V = Scene.Visualization;
    document.getElementById('ToggleVectors')?.classList.toggle('is-active', V.Arrows !== false);
    document.getElementById('ToggleFlowLines')?.classList.toggle('is-active', V.FlowLines !== false);
    document.getElementById('ToggleInfluence')?.classList.toggle('is-active', V.ShowInfluence !== false);
    document.getElementById('ToggleHeatmap')?.classList.toggle('is-active', Boolean(V.Heatmap));
    const HeatmapLegend = document.getElementById('HeatmapLegend');
    if (HeatmapLegend) HeatmapLegend.hidden = !(ViewMode === 'Stage3D' && V.Heatmap);
    document.getElementById('ToggleMinimal')?.classList.toggle('is-active', Boolean(V.MinimalLabels) && V.ArrowSpacing >= 64);

    const MinLabel = document.getElementById('MinimapLabel');
    if (MinLabel) MinLabel.textContent = `${FormatDistance(Scene.Field.Extent)}`;
}

function UpdateHoverReadout()
{
    const El = document.getElementById('ViewportReadout');
    if (!El) return;
    if (!HoverPoint || !Visualization || ViewMode === 'Stage3D') { El.innerHTML = ''; return; }
    const P = Visualization.ProbeAt(HoverPoint.ScreenX, HoverPoint.ScreenY, Clock.Time);
    if (!P) { El.innerHTML = ''; return; }
    El.innerHTML = `<b>${P.Speed.toFixed(1)} ${P.Unit}</b> · ${Math.round(P.Bearing)}° ${C.ResolveCompassPoint(P.Bearing)}<br>`
        + `(${FormatDistance(P.X)}, ${FormatDistance(P.Z)}) · ω ${P.Vorticity.toFixed(2)} · ∇·v ${P.Divergence.toFixed(2)}`;
}

function SetupEvents()
{
    document.querySelectorAll('#ScenarioTabs .doc-tab').forEach((Btn) => Btn.addEventListener('click', () => LoadScenario(Btn.dataset.scenario)));
    document.querySelectorAll('#QuickPlaceGroup [data-place-type]').forEach((Btn) => Btn.addEventListener('click', () =>
    {
        if (PlacementType === Btn.dataset.placeType) CancelPlacement();
        else StartPlacement(Btn.dataset.placeType);
    }));
    document.getElementById('BtnMoreTypes')?.addEventListener('click', OpenLibrary);
    document.getElementById('BtnOpenLibrary')?.addEventListener('click', OpenLibrary);
    document.getElementById('BtnCancelPlace')?.addEventListener('click', CancelPlacement);
    document.getElementById('LibraryVeil')?.addEventListener('click', (E) => { if (E.target.id === 'LibraryVeil') CloseLibrary(); });

    document.querySelectorAll('#ViewportBar [data-mode]').forEach((Btn) => Btn.addEventListener('click', () => SwitchViewMode(Btn.dataset.mode)));
    document.querySelectorAll('#SpanPresetGroup [data-span]').forEach((Btn) => Btn.addEventListener('click', () => JumpToSpan(Number(Btn.dataset.span))));

    const Viz = Scene.Visualization;
    const ToggleViz = (Key, Msg) => () => { Viz[Key] = !(Viz[Key] !== false); SceneRevision++; Visualization?.Invalidate(); SyncTopUi(); Sync3DIfVisible(); C.Broadcast(); AppendLog('VIEW', Msg, Viz[Key] ? 'shown' : 'hidden', Key); };
    document.getElementById('ToggleVectors')?.addEventListener('click', ToggleViz('Arrows', 'Vector field'));
    document.getElementById('ToggleFlowLines')?.addEventListener('click', ToggleViz('FlowLines', 'RK2 Flow lines'));
    document.getElementById('ToggleInfluence')?.addEventListener('click', ToggleViz('ShowInfluence', 'Influence bounds'));
    document.getElementById('ToggleHeatmap')?.addEventListener('click', () => { Viz.Heatmap = !Viz.Heatmap; Visualization?.Invalidate(); SyncTopUi(); Sync3DIfVisible(); C.Broadcast(); });
    document.getElementById('ToggleMinimal')?.addEventListener('click', () =>
    {
        const Minimal = !(Boolean(Viz.MinimalLabels) && Viz.ArrowSpacing >= 64);
        Viz.MinimalLabels = Minimal;
        Viz.ArrowSpacing = Minimal ? 70 : 54;
        Viz.FlowLineSpacing = Minimal ? 60 : 46;
        Viz.QuietCutoff = Minimal ? 0.05 : 0.03;
        Viz.Heatmap = Minimal ? false : Viz.Heatmap;
        Visualization?.Invalidate();
        SyncTopUi();
        Sync3DIfVisible();
        C.Broadcast();
    });

    const Drawer = document.getElementById('LogDrawer');
    document.getElementById('BtnToggleLog')?.addEventListener('click', () => { if (Drawer) { Drawer.hidden = !Drawer.hidden; Visualization?.Resize(); Stage3D?.Resize(); } });

    const LogHead = document.getElementById('LogHead');
    if (LogHead)
    {
        const Clear = C.Construct('button', 'tool-pill', 'Clear');
        Clear.type = 'button';
        Clear.addEventListener('click', () => { LogEntries.length = 0; RefreshLog(); });
        LogHead.append(Clear);
    }

    window.addEventListener('keydown', (E) =>
    {
        if (E.target.tagName === 'INPUT' || E.target.tagName === 'TEXTAREA') return;
        if (E.key === 'Escape') { CancelPlacement(); CloseLibrary(); }
        else if (E.code === 'Space') { E.preventDefault(); TogglePlay(); }
        else if (E.key === '1') JumpToSpan(250);
        else if (E.key === '2') JumpToSpan(1400);
        else if (E.key === '3') JumpToSpan(6000);
        else if (E.key === '4') JumpToSpan(100000);
        else if ((E.key === 'Delete' || E.key === 'Backspace') && Selected !== 'Field' && Selected !== 'Wind') RemoveSource(Selected);
    });
}

let LastLiveCheck = 0, LastStageSync = 0;
function Tick(Now)
{
    requestAnimationFrame(Tick);
    const Delta = Clock.LastTick ? Math.min(0.05, (Now - Clock.LastTick) / 1000) : 0;
    Clock.LastTick = Now;
    if (Clock.Playing)
    {
        Clock.Time += Delta * (Scene.Timeline.Rate || 1);
        const Dur = Scene.Timeline.Duration || 60;
        if (Clock.Time > Dur) Clock.Time = Scene.Timeline.Loop ? (Clock.Time % Dur) : Dur;
    }

    if (Visualization && ViewMode !== 'Stage3D')
    {
        ApplyVisualization();
        Visualization.Render(Clock.Time);
    }
    RefreshPlayhead();
    UpdateHoverReadout();

    if (Now - LastLiveCheck > 140)
    {
        LastLiveCheck = Now;
        const St = Visualization?.Statistics?.Peak ?? 0;
        PeakSamples.push(St); if (PeakSamples.length > SampleCapacity) PeakSamples.shift();
        const WS = Scene.Wind.Enabled ? F.WindSpeedAt(Scene.Wind, Scene.Field, 0, Scene.Wind.GroundHeight + 10, 0, Clock.Time) : 0;
        WindSamples.push(WS); if (WindSamples.length > SampleCapacity) WindSamples.shift();
        for (const Fn of LiveRefreshers) Fn();
        for (const Fn of LiveOutlinerRows) Fn();

        const Sig = JSON.stringify([Scene.Timeline, Scene.Sources.map((S) => [S.Token, S.Name, S.Activation, S.Begins, S.Lasts, S.Repeats, S.TriggeredAt, S.Enabled]), Selected]);
        if (Sig !== TimelineSignature) { TimelineSignature = Sig; ConstructTimeline(); }
    }
    if (Stage3D && ViewMode === 'Stage3D' && Clock.Playing && Now - LastStageSync > 220)
    {
        LastStageSync = Now;
        Sync3DIfVisible();
    }
}

ConstructVisualization();
ConstructOutliner();
ConstructInspectorPage();
ConstructTimeline();
SetupEvents();
SyncTopUi();
C.Register(Persist);
AppendLog('STUDIO', 'Force Field + Wind Studio ready', `${Scene.Sources.length} bounded fields loaded`, 'Boot');
HandleAssetDeepLink();
requestAnimationFrame(Tick);
