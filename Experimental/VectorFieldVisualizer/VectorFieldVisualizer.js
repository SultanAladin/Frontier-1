/*============================================================================================================================================
                                                    VECTORFIELDVISUALIZER.JS
============================================================================================================================================*/
// 🧩 Project Zero Editor + GPU WebGL2 VectorFieldVisualizer Controller

import {
    Clamp,
    CompassOf,
    ComputeMotionPathLength,
    ConstructForce,
    ConstructScenario,
    EForceDuration,
    EncodeSimulationSpaceToml,
    EnsureSourceMotionWaypoints,
    EvaluateComposite,
    EvaluateMotionPosition,
    ForceAttenuations,
    ForceModelById,
    ForceModels,
    GetActiveSliceAxes,
    MotionLoopModes,
    MotionModes,
    ReachShapes,
    ResolveCascadeBudget,
    RuntimeSystems,
    SampleDifferential,
    StepAnimatedSources
} from './Engine/VectorFieldCatalogue.js';
import { VectorFieldGPUViewport } from './Engine/VectorFieldGPUViewport.js';
import { GetForceModelSvgIcon } from './Engine/ForceIcons.js';
import {
    CompileFieldProgram,
    DEFAULT_FORMULA_CODE,
    FORMULA_PRESETS
} from './Engine/VectorFieldLang.js';
import {
    BeaufortBarShare,
    BuildAttenuationCurveSvg,
    DrawAnemometerTrace,
    SummarizeHistory
} from './Engine/InspectorTelemetry.js';

const SCENARIO_TABS = [
    { Key: 'lab', Title: 'Wind & Vortex Lab' },
    { Key: 'all19', Title: 'All 19 ForceModels' },
    { Key: 'maritime', Title: 'Maritime Wake' },
    { Key: 'turbulence', Title: 'Gusts & Thermals' },
    { Key: 'electromagnetic', Title: 'Magnetic & Dipole' }
];

const COLORMAP_GRADIENTS = {
    Engineering: 'linear-gradient(90deg, #0f1c26, #2e6e7d, #59a66e, #d4b566, #e86b4f)',
    Turbo: 'linear-gradient(90deg, #24171c, #4569f0, #21e0c4, #e3e02e, #e83317)',
    Plasma: 'linear-gradient(90deg, #120a52, #7d149e, #cc4778, #f79440, #f5f56e)',
    Diverging: 'linear-gradient(90deg, #2b7ae3, #6bb5f2, #dbe3e8, #f09957, #e03b33)'
};

const State = {
    ScenarioKey: 'lab',
    Scene: ConstructScenario('lab'),
    SelectedToken: null,
    SelectedWaypointIdx: null,
    RunMode: 'sim', // 'edit' | 'sim' | 'play'
    PlayPaused: false,
    InspectorTab: 'field', // 'field' | 'layers' | 'cache'
    SystemFilter: 'All',
    MuteFilter: 'All', // 'All' | 'Active' | 'Muted'
    SearchText: '',
    SimTime: 0.0,
    AnemHistory: [],
    LastTelemetryMs: 0,
    LastAnimMs: performance.now()
};

State.SelectedToken = State.Scene.Sources[0]?.Token || null;

//------------------------------------------------------------------------------------------------------------------------
//                                                    DOM REFERENCES & VIEWPORT
//------------------------------------------------------------------------------------------------------------------------

const El = (Id) => document.getElementById(Id);
let CompileHideTimer = null;

function HandleCompileProgress(Info)
{
    const Overlay = El('ShaderCompileOverlay');
    const Badge = El('ViewportGpuBadge');
    const ErrBox = El('ShaderCompileError');

    El('ShaderProgressTitle').textContent = Info.Label;
    El('ShaderProgressPct').textContent = `${Info.Percent}%`;
    El('ShaderProgressFill').style.width = `${Info.Percent}%`;

    const ListEl = El('ShaderStageList');
    ListEl.innerHTML = (Info.Stages || []).map((S, I) => `
        <div class="compile-stage-row">
            <span>${I + 1}/${Info.Total || 4} · ${S.Name}</span>
            <span class="${S.Ok ? 'ok' : 'err'}">${S.Ok ? `${S.Ms.toFixed(1)} ms` : 'ERR'}</span>
        </div>
    `).join('');

    if (Info.Error)
    {
        ErrBox.textContent = Info.Error;
        ErrBox.classList.remove('hidden');
        Overlay.classList.remove('hidden');
        Badge.classList.add('error');
        Badge.textContent = 'WebGL2 Shader Error';
        return;
    }

    ErrBox.classList.add('hidden');
    Badge.classList.remove('error');

    if (Info.Fallback)
    {
        Badge.classList.remove('error');
        Badge.textContent = 'Software 2D/Iso · Fallback';
        clearTimeout(CompileHideTimer);
        CompileHideTimer = setTimeout(() => Overlay.classList.add('hidden'), 1200);
        return;
    }

    if (Info.Done)
    {
        const ModeTag = Info.GpuInfo?.BakeMode?.includes('Float') ? 'Float16' : 'RGBA8';
        Badge.textContent = `GPU WebGL2 · 4/4 Shaders · ${ModeTag}`;
        clearTimeout(CompileHideTimer);
        CompileHideTimer = setTimeout(() => Overlay.classList.add('hidden'), 650);
    }
    else
    {
        Badge.textContent = `Compiling Shaders ${Info.Stage}/${Info.Total} (${Info.Percent}%)`;
        Overlay.classList.remove('hidden');
    }
}

const Viewport = new VectorFieldGPUViewport(El('GpuViewportMount'), {
    OnCompileProgress: HandleCompileProgress,
    OnZoomChange: (Pct) => {
        const ZEl = El('ZoomPctLabel');
        if (ZEl) ZEl.textContent = `${Pct}%`;
    },
    OnFormulaGpuCompiled: (Info) => {
        const Badge = El('ViewportGpuBadge');
        if (!Badge) return;
        if (Info.Ok)
        {
            Badge.classList.remove('error');
            Badge.textContent = Info.Slots > 0
                ? `GPU WebGL2 · ${Info.Slots} Formula(s) · ${Info.Ms.toFixed(1)} ms`
                : 'GPU WebGL2 · 5/5 Shaders';
        }
        else
        {
            Badge.classList.add('error');
            Badge.textContent = 'Formula GPU Error';
        }
    },
    OnSelectSource: (Token) => {
        State.SelectedToken = Token;
        State.SelectedWaypointIdx = null;
        Viewport.SetSelectedToken(Token, null);
        RenderOutliner();
        RenderSelectedForceCard();
        RenderFormulaCompilerCard();
    },
    OnSelectWaypoint: (Token, WaypointIdx) => {
        State.SelectedToken = Token;
        State.SelectedWaypointIdx = WaypointIdx;
        Viewport.SetSelectedWaypoint(Token, WaypointIdx);
        RenderOutliner();
        RenderSelectedForceCard();
        RenderFormulaCompilerCard();
    },
    OnMoveProbe: (PosM) => {
        State.Scene.Probe = [Math.round(PosM[0]), Math.round(Clamp(PosM[1], 0, 240)), Math.round(PosM[2])];
        Viewport.UpdateProbeAndHeatmapGeometry();
        SyncProbeInputs();
        UpdateTelemetry(true);
    },
    OnMoveSource: (Token, PosM) => {
        const S = State.Scene.Sources.find((Item) => Item.Token === Token);
        if (!S) return;
        S.Centre = [Math.round(PosM[0]), Math.round(Clamp(PosM[1], -80, 260)), Math.round(PosM[2])];
        S.RestCentre = [...S.Centre];
        Viewport.SyncSourceMoved(Token, false);
        SyncSelectedCentreInputs();
        UpdateTelemetry(true);
    },
    OnMoveWaypoint: (Token, WaypointIdx, PosM) => {
        const S = State.Scene.Sources.find((Item) => Item.Token === Token);
        const Wp = S?.Motion?.Waypoints?.[WaypointIdx];
        if (!S || !Wp) return;
        Wp[0] = Math.round(PosM[0]);
        Wp[1] = Math.round(Clamp(PosM[1], -80, 260));
        Wp[2] = Math.round(PosM[2]);
        if (State.RunMode === 'edit' || State.PlayPaused)
        {
            const PreviewPos = EvaluateMotionPosition(S, S.Motion.ScrubU ?? S.Motion.CurrentU ?? 0);
            S.Centre = PreviewPos;
        }
        Viewport.SyncSourceMoved(Token, true);
        SyncSelectedCentreInputs();
        SyncMotionCardInputs();
        UpdateTelemetry(true);
    }
});

//------------------------------------------------------------------------------------------------------------------------
//                                                 HELPERS & SCENARIO SWITCHING
//------------------------------------------------------------------------------------------------------------------------

function GetSelectedSource()
{
    return State.Scene.Sources.find((S) => S.Token === State.SelectedToken) || State.Scene.Sources[0] || null;
}

function SwitchInspectorTab(TabKey)
{
    State.InspectorTab = TabKey;
    El('InspectorTabs').querySelectorAll('button').forEach((B) => B.classList.toggle('active', B.dataset.tab === TabKey));
    El('TabPaneField').style.display = TabKey === 'field' ? 'flex' : 'none';
    El('TabPaneFormula').style.display = TabKey === 'formula' ? 'flex' : 'none';
    El('TabPaneLayers').style.display = TabKey === 'layers' ? 'flex' : 'none';
    El('TabPaneCache').style.display = TabKey === 'cache' ? 'flex' : 'none';
    if (TabKey === 'formula') RenderFormulaCompilerCard();
    if (TabKey === 'cache') RenderSpatialCacheCard();
}

function SyncSliderFill(Input)
{
    const Min = Number(Input.min || 0);
    const Max = Number(Input.max || 100);
    const Val = Number(Input.value || 0);
    const Pct = Max > Min ? Clamp(((Val - Min) / (Max - Min)) * 100, 0, 100) : 50;
    Input.style.setProperty('--fill', `${Pct.toFixed(1)}%`);
}

function LoadScenario(Key)
{
    State.ScenarioKey = Key;
    State.Scene = ConstructScenario(Key);
    State.SelectedToken = State.Scene.Sources[0]?.Token || null;
    State.SelectedWaypointIdx = null;
    State.SimTime = 0;
    State.PlayPaused = false;
    State.AnemHistory = [];
    Viewport.SelectedWaypointIdx = null;
    Viewport.SetScene(State.Scene, State.SelectedToken);
    Viewport.ResetCamera();
    RenderAll();
    SyncTransportUi();
}

function AddForceSource(ModelId, Extra = {})
{
    const OffsetAngle = (State.Scene.Sources.length * 1.35) % (Math.PI * 2);
    const Dist = State.Scene.Sources.length === 0 ? 0 : 110 + (State.Scene.Sources.length % 3) * 45;
    const Cx = Math.round(State.Scene.Probe[0] * 0.4 + Math.cos(OffsetAngle) * Dist);
    const Cz = Math.round(State.Scene.Probe[2] * 0.4 + Math.sin(OffsetAngle) * Dist);
    const Model = ForceModelById[ModelId];
    const NewSrc = ConstructForce(ModelId, {
        Centre: [Cx, Model?.Reach === 'Sphere' ? 18 : 0, Cz],
        ...Extra
    });
    State.Scene.Sources.push(NewSrc);
    State.SelectedToken = NewSrc.Token;
    Viewport.SetScene(State.Scene, State.SelectedToken);
    Viewport.FocusOn(NewSrc.Centre);
    RenderAll();
}

function AddFormulaSource()
{
    AddForceSource('Vortex', {
        Name: 'Symbolic GPU Swirl Equation',
        Reach: 'Sphere',
        Strength: 34,
        Swirl: 22,
        Radius: 210,
        UseFormula: true,
        FormulaCode: DEFAULT_FORMULA_CODE
    });
    SwitchInspectorTab('formula');
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 TAB STRIP & OUTLINER RENDER
//------------------------------------------------------------------------------------------------------------------------

function RenderTabs()
{
    const Host = El('ScenarioTabs');
    Host.innerHTML = SCENARIO_TABS.map((Tab) => `
        <div class="document-tab ${Tab.Key === State.ScenarioKey ? 'active' : ''}" data-scenario="${Tab.Key}">
            <span class="tab-dot"></span>
            <span class="tab-title">${Tab.Title}</span>
        </div>
    `).join('');

    Host.querySelectorAll('.document-tab').forEach((Node) => {
        Node.addEventListener('click', () => LoadScenario(Node.dataset.scenario));
    });
    El('ViewportScenarioTitle').textContent = State.Scene.Name;
}

function RenderOutliner()
{
    const Sources = State.Scene.Sources;
    const ActiveCount = Sources.filter((S) => S.Enabled).length;
    const MutedCount = Sources.length - ActiveCount;
    El('StatActiveCount').textContent = String(ActiveCount);
    El('StatMutedCount').textContent = String(MutedCount);
    El('OutlinerSubtitle').textContent = `${Sources.length} / 20 GPU Slots`;

    El('BtnStatActive').classList.toggle('active-filter', State.MuteFilter === 'Active');
    El('BtnStatMuted').classList.toggle('active-filter', State.MuteFilter === 'Muted');
    El('BtnOutlinerMuteFilter').textContent = State.MuteFilter;

    const Query = State.SearchText.trim().toLowerCase();
    const Filtered = Sources.filter((S) => {
        if (State.SystemFilter !== 'All' && S.System !== State.SystemFilter) return false;
        if (State.MuteFilter === 'Active' && !S.Enabled) return false;
        if (State.MuteFilter === 'Muted' && S.Enabled) return false;
        if (Query)
        {
            const M = ForceModelById[S.Model];
            const Hay = `${S.Name} ${S.Model} ${S.System} ${M?.Blurb || ''}`.toLowerCase();
            if (!Hay.includes(Query)) return false;
        }
        return true;
    });

    const Container = El('OutlinerRows');
    const SoloToken = State.Scene.Viz.SoloToken;
    Container.innerHTML = Filtered.map((S) => {
        const Model = ForceModelById[S.Model];
        const IsSel = S.Token === State.SelectedToken;
        const MotionTag = S.Motion?.Mode && S.Motion.Mode !== 'None'
            ? `<span class="row-motion-pill" title="Animated ${S.Motion.Mode} (${S.Motion.SpeedMps} m/s)">${S.Motion.Mode === 'LineAB' ? 'A↔B' : (S.Motion.Mode === 'Orbit' ? 'Orbit' : 'Spline')}</span>`
            : '';
        return `
            <div class="outliner-row ${IsSel ? 'selected' : ''} ${S.Enabled ? '' : 'hidden-row'}" data-token="${S.Token}">
                ${GetForceModelSvgIcon(S.Model, S.UseFormula ? '#ffb454' : (Model?.Colour || '#78d2c6'))}
                <span class="row-name" title="${S.Name}">${S.Name}</span>
                ${MotionTag}
                <span class="row-badge" style="${S.UseFormula ? 'color:#ffb454;' : ''}">${S.UseFormula ? 'fx(GPU)' : S.Model}</span>
                <button type="button" class="row-solo ${SoloToken === S.Token ? 'active' : ''}" data-solo="${S.Token}" title="Solo Force">S</button>
                <button type="button" class="row-status ${S.Enabled ? '' : 'off'}" data-toggle="${S.Token}" title="Toggle Active">${S.Enabled ? '✓' : '·'}</button>
            </div>
        `;
    }).join('');

    Container.querySelectorAll('.outliner-row').forEach((Row) => {
        Row.addEventListener('click', (E) => {
            if (E.target.closest('button')) return;
            State.SelectedToken = Row.dataset.token;
            State.SelectedWaypointIdx = null;
            Viewport.SetSelectedToken(State.SelectedToken, null);
            RenderOutliner();
            RenderSelectedForceCard();
            RenderFormulaCompilerCard();
        });
        Row.addEventListener('dblclick', () => {
            const Src = State.Scene.Sources.find((Item) => Item.Token === Row.dataset.token);
            if (Src) Viewport.FocusOn(Src.Centre);
        });
    });

    Container.querySelectorAll('[data-toggle]').forEach((Btn) => {
        Btn.addEventListener('click', (E) => {
            E.stopPropagation();
            const Src = State.Scene.Sources.find((Item) => Item.Token === Btn.dataset.toggle);
            if (!Src) return;
            Src.Enabled = !Src.Enabled;
            Viewport.SetScene(State.Scene, State.SelectedToken);
            RenderOutliner();
            RenderSelectedForceCard();
        });
    });

    Container.querySelectorAll('[data-solo]').forEach((Btn) => {
        Btn.addEventListener('click', (E) => {
            E.stopPropagation();
            const Tok = Btn.dataset.solo;
            State.Scene.Viz.SoloToken = State.Scene.Viz.SoloToken === Tok ? null : Tok;
            Viewport.SetScene(State.Scene, State.SelectedToken);
            RenderOutliner();
        });
    });

    const Budget = ResolveCascadeBudget(State.Scene);
    El('FooterBudget').textContent = `${State.Scene.Lattice.Cascades} × ${(Budget.BytesPerCascade / (1024 * 1024)).toFixed(1)} MB`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           INSPECTOR CARDS (PROJECT ZERO SLIDERS)
//------------------------------------------------------------------------------------------------------------------------

function SyncProbeInputs()
{
    El('ProbeInputX').value = Math.round(State.Scene.Probe[0]);
    El('ProbeInputY').value = Math.round(State.Scene.Probe[1]);
    El('ProbeInputZ').value = Math.round(State.Scene.Probe[2]);
}

function SyncSelectedCentreInputs()
{
    const Source = GetSelectedSource();
    const Card = El('SelectedForceCard');
    if (!Source || !Card) return;
    Card.querySelectorAll('[data-centre]').forEach((Inp) => {
        const Ax = Number(Inp.dataset.centre);
        if (Number.isFinite(Ax) && document.activeElement !== Inp)
        {
            Inp.value = Math.round(Source.Centre[Ax] || 0);
        }
    });
    const RngScrub = Card.querySelector('#RngMotionScrub');
    const LblScrub = Card.querySelector('#LblMotionScrubPct');
    const U = Source.Motion?.CurrentU ?? Source.Motion?.ScrubU ?? 0;
    if (RngScrub && document.activeElement !== RngScrub)
    {
        RngScrub.value = U.toFixed(3);
        SyncSliderFill(RngScrub);
    }
    if (LblScrub) LblScrub.textContent = `${Math.round(U * 100)}%`;
}

function SyncMotionCardInputs()
{
    const Source = GetSelectedSource();
    const Card = El('SelectedForceCard');
    if (!Source || !Card || !Source.Motion?.Waypoints) return;
    Source.Motion.Waypoints.forEach((Wp, WIdx) => {
        for (let Ax = 0; Ax < 3; ++Ax)
        {
            const Inp = Card.querySelector(`[data-wp-idx="${WIdx}"][data-wp-axis="${Ax}"]`);
            if (Inp && document.activeElement !== Inp) Inp.value = Math.round(Wp[Ax] || 0);
        }
    });
}

function SyncTransportUi()
{
    const ModesEl = El('ViewportModes');
    if (ModesEl)
    {
        ModesEl.querySelectorAll('button[data-mode]').forEach((B) => {
            B.classList.toggle('active', B.dataset.mode === State.RunMode);
        });
    }
    const BtnPP = El('BtnPlayPause');
    if (BtnPP)
    {
        const IsPaused = State.RunMode === 'edit' || State.PlayPaused;
        BtnPP.textContent = IsPaused ? '▶' : '⏸';
        BtnPP.classList.toggle('paused', IsPaused);
    }
    const TimeEl = El('ViewportTimeBadge');
    if (TimeEl) TimeEl.textContent = `t ${State.SimTime.toFixed(1)}s`;
}

function RenderSelectedForceCard()
{
    const Source = GetSelectedSource();
    const Card = El('SelectedForceCard');
    if (!Source)
    {
        El('InspectorTitle').textContent = 'No Source Selected';
        Card.innerHTML = '<small>Click + Construct (Ctrl+A) to add one of the 19 ForceModels.</small>';
        return;
    }

    const Model = ForceModelById[Source.Model];
    El('InspectorTitle').textContent = Source.Name;

    const ParamsHtml = (Model?.Params || []).map((P) => {
        const Val = Source[P.Key] ?? Model.Defaults[P.Key] ?? 0;
        return `
            <div class="prop-row">
                <div class="prop-row-head">
                    <span>${P.Label}</span>
                    <small>${P.Unit}</small>
                </div>
                <div class="slider-row">
                    <input type="range" data-param="${P.Key}" min="${P.Min}" max="${P.Max}" step="${P.Step}" value="${Val}" />
                    <div class="split-value">
                        <input type="number" data-param-num="${P.Key}" min="${P.Min}" max="${P.Max}" step="${P.Step}" value="${Number(Val).toFixed(P.Digits ?? 1)}" />
                        <span>${P.Unit || 'u'}</span>
                    </div>
                </div>
            </div>
        `;
    }).join('');

    Card.innerHTML = `
        <div class="property-card-head">
            <h3>${Model?.Name || Source.Model}</h3>
            <small style="color:${Model?.Colour || '#78d2c6'}">${Source.System} · ${Source.Channel}</small>
        </div>
        <div class="equation-box" style="display:flex; align-items:center; justify-content:space-between; gap:8px;">
            <span>${Source.UseFormula ? Source.FormulaCode : (Model?.Equation || '')}</span>
            <button type="button" class="tab-chip-btn" id="BtnOpenFormulaEditor" style="flex:none; height:22px; min-height:22px; font-size:10px;">${Source.UseFormula ? 'Edit fx' : 'Formula'}</button>
        </div>

        <div class="axes">
            <label class="axis-box x"><b>X</b><input type="number" data-centre="0" step="5" value="${Math.round(Source.Centre[0])}" /></label>
            <label class="axis-box y"><b>Y</b><input type="number" data-centre="1" step="2" value="${Math.round(Source.Centre[1])}" /></label>
            <label class="axis-box z"><b>Z</b><input type="number" data-centre="2" step="5" value="${Math.round(Source.Centre[2])}" /></label>
        </div>

        <div class="metric-grid">
            <div class="prop-row">
                <div class="prop-row-head"><span>ReachShape</span></div>
                <select id="SelForceReach">
                    ${ReachShapes.map((R) => `<option value="${R}" ${R === Source.Reach ? 'selected' : ''}>${R}</option>`).join('')}
                </select>
            </div>
            <div class="prop-row">
                <div class="prop-row-head"><span>Attenuation</span></div>
                <select id="SelForceAtten">
                    ${ForceAttenuations.map((A) => `<option value="${A}" ${A === Source.Attenuation ? 'selected' : ''}>${A}</option>`).join('')}
                </select>
            </div>
        </div>

        <div class="metric-grid">
            <div class="prop-row">
                <div class="prop-row-head"><span>EForceDuration</span></div>
                <select id="SelForceDuration">
                    ${EForceDuration.map((D) => `<option value="${D}" ${D === Source.Duration ? 'selected' : ''}>${D}</option>`).join('')}
                </select>
            </div>
            <div class="prop-row">
                <div class="prop-row-head"><span>Radius (m)</span></div>
                <div class="split-value" style="width:100%;">
                    <input type="number" id="NumForceRadius" min="15" max="700" step="5" value="${Math.round(Source.Radius)}" />
                    <span>m</span>
                </div>
            </div>
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>Bounded Radius</span><small>15 – 700 m</small></div>
            <div class="slider-row">
                <input type="range" id="RngForceRadius" min="15" max="700" step="5" value="${Source.Radius}" />
                <div class="split-value"><input type="number" readonly value="${Math.round(Source.Radius)}" /><span>m</span></div>
            </div>
        </div>

        ${ParamsHtml}

        <div class="prop-row">
            <div class="prop-row-head"><span>Radial Envelope f(r)</span><small>Solid=Mag · Dashed=A(r)</small></div>
            ${BuildAttenuationCurveSvg(Source, State.SimTime)}
        </div>

        <div class="motion-card" id="MotionPathCard">
            ${(() => {
                const M = Source.Motion || { Mode: 'None', Loop: 'PingPong', SpeedMps: 40, Phase: 0, ScrubU: 0, CurrentU: 0, OrbitRadius: 110, Waypoints: [] };
                const PathLen = ComputeMotionPathLength(Source);
                const Wps = M.Waypoints || [];
                const HasPath = M.Mode !== 'None';
                return `
                    <div class="motion-head">
                        <h4>Curve &amp; Path Animation</h4>
                        <small style="color:${HasPath ? '#ffb454' : '#7b8794'}">${HasPath ? `${Math.round(PathLen)} m · ${(PathLen / Math.max(1, M.SpeedMps || 40)).toFixed(1)} s` : 'Static'}</small>
                    </div>
                    <div class="motion-quick-row">
                        <button type="button" class="tab-chip-btn" id="BtnMotionAddLine">A → B Line</button>
                        <button type="button" class="tab-chip-btn" id="BtnMotionAddSpline">3D Spline Curve</button>
                        <button type="button" class="tab-chip-btn" id="BtnMotionAddOrbit">Orbit</button>
                        ${HasPath ? '<button type="button" class="tab-chip-btn" id="BtnMotionClear">Clear</button>' : ''}
                    </div>
                    <div class="metric-grid">
                        <div class="prop-row">
                            <div class="prop-row-head"><span>Path Mode</span></div>
                            <select id="SelMotionMode">
                                ${MotionModes.map((Md) => `<option value="${Md}" ${Md === M.Mode ? 'selected' : ''}>${Md === 'None' ? 'Static (None)' : (Md === 'LineAB' ? 'Linear A → B' : (Md === 'SplineCurve' ? '3D Spline Curve' : 'Orbit Loop'))}</option>`).join('')}
                            </select>
                        </div>
                        <div class="prop-row">
                            <div class="prop-row-head"><span>Loop Mode</span></div>
                            <select id="SelMotionLoop" ${HasPath ? '' : 'disabled'}>
                                ${MotionLoopModes.map((Lm) => `<option value="${Lm}" ${Lm === M.Loop ? 'selected' : ''}>${Lm}</option>`).join('')}
                            </select>
                        </div>
                    </div>
                    ${HasPath ? `
                        <div class="prop-row">
                            <div class="prop-row-head"><span>Travel Speed</span><small>m/s</small></div>
                            <div class="slider-row">
                                <input type="range" id="RngMotionSpeed" min="2" max="240" step="2" value="${M.SpeedMps ?? 40}" />
                                <div class="split-value"><input type="number" id="NumMotionSpeed" min="0" max="500" step="2" value="${Math.round(M.SpeedMps ?? 40)}" /><span>m/s</span></div>
                            </div>
                        </div>
                        <div class="prop-row">
                            <div class="prop-row-head"><span>Path Scrub / Progress</span><small id="LblMotionScrubPct">${Math.round((M.CurrentU ?? M.ScrubU ?? 0) * 100)}%</small></div>
                            <div class="slider-row">
                                <input type="range" id="RngMotionScrub" min="0" max="1" step="0.01" value="${(M.CurrentU ?? M.ScrubU ?? 0).toFixed(3)}" />
                                <div class="split-value"><input type="number" id="NumMotionPhase" min="0" max="1" step="0.05" value="${Number(M.Phase ?? 0).toFixed(2)}" title="Start Phase [0..1]" /><span>φ</span></div>
                            </div>
                        </div>
                        ${M.Mode === 'Orbit' ? `
                            <div class="prop-row">
                                <div class="prop-row-head"><span>Orbit Radius</span><small>m</small></div>
                                <div class="slider-row">
                                    <input type="range" id="RngOrbitRadius" min="15" max="420" step="5" value="${M.OrbitRadius ?? 110}" />
                                    <div class="split-value"><input type="number" id="NumOrbitRadius" min="10" max="500" step="5" value="${Math.round(M.OrbitRadius ?? 110)}" /><span>m</span></div>
                                </div>
                            </div>
                        ` : `
                            <div class="motion-head" style="margin-top:2px;">
                                <small style="color:#8fa0b0">Click tag or 3D sphere to move gizmo onto waypoint</small>
                                <button type="button" class="tab-chip-btn" id="BtnAddWaypoint" style="height:20px; min-height:20px; font-size:9px;">+ Knot</button>
                            </div>
                            <div class="waypoint-list">
                                ${Wps.map((Wp, WIdx) => {
                                    const Tag = WIdx === 0 ? 'A' : (WIdx === Wps.length - 1 ? 'B' : `P${WIdx}`);
                                    const ActiveWp = State.SelectedWaypointIdx === WIdx;
                                    return `
                                        <div class="waypoint-row ${ActiveWp ? 'active' : ''}">
                                            <button type="button" class="waypoint-tag" data-wp-focus="${WIdx}" title="Select Waypoint ${Tag} for 3D Gizmo">${Tag}</button>
                                            <input type="number" step="5" data-wp-idx="${WIdx}" data-wp-axis="0" value="${Math.round(Wp[0])}" />
                                            <input type="number" step="2" data-wp-idx="${WIdx}" data-wp-axis="1" value="${Math.round(Wp[1])}" />
                                            <input type="number" step="5" data-wp-idx="${WIdx}" data-wp-axis="2" value="${Math.round(Wp[2])}" />
                                            <button type="button" class="waypoint-del" data-wp-del="${WIdx}" ${Wps.length <= 2 ? 'disabled style="opacity:0.35;"' : ''} title="Remove Waypoint">×</button>
                                        </div>
                                    `;
                                }).join('')}
                            </div>
                        `}
                    ` : ''}
                `;
            })()}
        </div>
    `;

    Card.querySelectorAll('input[type="range"]').forEach(SyncSliderFill);

    const ApplyUpdate = () => {
        Viewport.SetScene(State.Scene, State.SelectedToken);
        UpdateTelemetry(true);
    };

    const RefreshMotionEdit = (RebuildCard = true) => {
        Viewport.RebuildMotionPaths();
        Viewport.UpdateGizmoPlacement();
        RenderOutliner();
        if (RebuildCard) RenderSelectedForceCard();
        UpdateTelemetry(true);
    };

    Card.querySelector('#BtnMotionAddLine')?.addEventListener('click', () => {
        EnsureSourceMotionWaypoints(Source, 'LineAB');
        State.SelectedWaypointIdx = 1;
        Viewport.SelectedWaypointIdx = 1;
        RefreshMotionEdit(true);
    });
    Card.querySelector('#BtnMotionAddSpline')?.addEventListener('click', () => {
        EnsureSourceMotionWaypoints(Source, 'SplineCurve');
        State.SelectedWaypointIdx = 1;
        Viewport.SelectedWaypointIdx = 1;
        RefreshMotionEdit(true);
    });
    Card.querySelector('#BtnMotionAddOrbit')?.addEventListener('click', () => {
        EnsureSourceMotionWaypoints(Source, 'Orbit');
        State.SelectedWaypointIdx = 0;
        Viewport.SelectedWaypointIdx = 0;
        RefreshMotionEdit(true);
    });
    Card.querySelector('#BtnMotionClear')?.addEventListener('click', () => {
        if (Source.Motion) Source.Motion.Mode = 'None';
        State.SelectedWaypointIdx = null;
        Viewport.SelectedWaypointIdx = null;
        RefreshMotionEdit(true);
    });
    El('SelMotionMode')?.addEventListener('change', (E) => {
        const Md = E.target.value;
        if (Md === 'None') Source.Motion.Mode = 'None';
        else EnsureSourceMotionWaypoints(Source, Md);
        State.SelectedWaypointIdx = null;
        Viewport.SelectedWaypointIdx = null;
        RefreshMotionEdit(true);
    });
    El('SelMotionLoop')?.addEventListener('change', (E) => {
        Source.Motion.Loop = E.target.value;
    });
    const BindMotionNum = (RngId, NumId, Prop, RebuildPath = false) => {
        const R = El(RngId), N = El(NumId);
        R?.addEventListener('input', () => {
            Source.Motion[Prop] = Number(R.value);
            if (N) N.value = R.value;
            SyncSliderFill(R);
            if (RebuildPath) { Viewport.RebuildMotionPaths(); Viewport.SyncSourceMoved(Source.Token, true); }
        });
        N?.addEventListener('input', () => {
            const V = Number(N.value);
            if (!Number.isFinite(V)) return;
            Source.Motion[Prop] = V;
            if (R) { R.value = V; SyncSliderFill(R); }
            if (RebuildPath) { Viewport.RebuildMotionPaths(); Viewport.SyncSourceMoved(Source.Token, true); }
        });
    };
    BindMotionNum('RngMotionSpeed', 'NumMotionSpeed', 'SpeedMps', false);
    BindMotionNum('RngOrbitRadius', 'NumOrbitRadius', 'OrbitRadius', true);
    El('NumMotionPhase')?.addEventListener('input', (E) => {
        const V = Clamp(Number(E.target.value) || 0, 0, 1);
        Source.Motion.Phase = V;
    });
    El('RngMotionScrub')?.addEventListener('input', (E) => {
        const U = Clamp(Number(E.target.value) || 0, 0, 1);
        Source.Motion.ScrubU = U;
        Source.Motion.CurrentU = U;
        SyncSliderFill(E.target);
        const Lbl = Card.querySelector('#LblMotionScrubPct');
        if (Lbl) Lbl.textContent = `${Math.round(U * 100)}%`;
        Source.Centre = EvaluateMotionPosition(Source, U);
        Viewport.SyncSourceMoved(Source.Token, false);
        SyncSelectedCentreInputs();
        UpdateTelemetry(true);
    });
    Card.querySelector('#BtnAddWaypoint')?.addEventListener('click', () => {
        const Wps = Source.Motion?.Waypoints;
        if (!Wps || Wps.length < 2) return;
        const P0 = Wps[Wps.length - 2], P1 = Wps[Wps.length - 1];
        const Mid = [Math.round((P0[0] + P1[0]) * 0.5), Math.round(Math.max(P0[1], P1[1]) + 32), Math.round((P0[2] + P1[2]) * 0.5)];
        Wps.splice(Wps.length - 1, 0, Mid);
        if (Source.Motion.Mode === 'LineAB') Source.Motion.Mode = 'SplineCurve';
        State.SelectedWaypointIdx = Wps.length - 2;
        Viewport.SelectedWaypointIdx = State.SelectedWaypointIdx;
        RefreshMotionEdit(true);
    });
    Card.querySelectorAll('[data-wp-focus]').forEach((Btn) => {
        Btn.addEventListener('click', () => {
            const Idx = Number(Btn.dataset.wpFocus);
            State.SelectedWaypointIdx = Idx;
            Viewport.SetSelectedWaypoint(Source.Token, Idx);
            RenderSelectedForceCard();
        });
    });
    Card.querySelectorAll('[data-wp-del]').forEach((Btn) => {
        Btn.addEventListener('click', () => {
            const Idx = Number(Btn.dataset.wpDel);
            if (!Source.Motion?.Waypoints || Source.Motion.Waypoints.length <= 2) return;
            Source.Motion.Waypoints.splice(Idx, 1);
            State.SelectedWaypointIdx = null;
            Viewport.SelectedWaypointIdx = null;
            RefreshMotionEdit(true);
        });
    });
    Card.querySelectorAll('[data-wp-idx]').forEach((Inp) => {
        Inp.addEventListener('input', () => {
            const WIdx = Number(Inp.dataset.wpIdx);
            const Ax = Number(Inp.dataset.wpAxis);
            const V = Number(Inp.value);
            const Wp = Source.Motion?.Waypoints?.[WIdx];
            if (!Wp || !Number.isFinite(V)) return;
            Wp[Ax] = V;
            if (State.RunMode === 'edit' || State.PlayPaused)
            {
                Source.Centre = EvaluateMotionPosition(Source, Source.Motion.ScrubU ?? Source.Motion.CurrentU ?? 0);
            }
            Viewport.SyncSourceMoved(Source.Token, true);
            SyncSelectedCentreInputs();
            UpdateTelemetry(true);
        });
    });

    Card.querySelectorAll('[data-param]').forEach((Rng) => {
        const Key = Rng.dataset.param;
        const Num = Card.querySelector(`[data-param-num="${Key}"]`);
        Rng.addEventListener('input', () => {
            Source[Key] = Number(Rng.value);
            if (Num) Num.value = Rng.value;
            SyncSliderFill(Rng);
            ApplyUpdate();
        });
        if (Num)
        {
            Num.addEventListener('input', () => {
                const V = Number(Num.value);
                if (!Number.isFinite(V)) return;
                Source[Key] = V;
                Rng.value = V;
                SyncSliderFill(Rng);
                ApplyUpdate();
            });
        }
    });

    Card.querySelectorAll('[data-centre]').forEach((Inp) => {
        Inp.addEventListener('input', () => {
            const Ax = Number(Inp.dataset.centre);
            const V = Number(Inp.value);
            if (Number.isFinite(V))
            {
                Source.Centre[Ax] = V;
                Viewport.SyncSourceMoved(Source.Token);
                UpdateTelemetry(true);
            }
        });
    });

    El('SelForceReach')?.addEventListener('change', (E) => { Source.Reach = E.target.value; ApplyUpdate(); RenderSelectedForceCard(); });
    El('SelForceAtten')?.addEventListener('change', (E) => { Source.Attenuation = E.target.value; ApplyUpdate(); RenderSelectedForceCard(); });
    El('SelForceDuration')?.addEventListener('change', (E) => { Source.Duration = E.target.value; ApplyUpdate(); RenderSelectedForceCard(); });

    const RngRad = El('RngForceRadius');
    const NumRad = El('NumForceRadius');
    RngRad?.addEventListener('input', () => {
        Source.Radius = Number(RngRad.value);
        if (NumRad) NumRad.value = RngRad.value;
        SyncSliderFill(RngRad);
        ApplyUpdate();
    });
    NumRad?.addEventListener('input', () => {
        const V = Clamp(Number(NumRad.value) || 180, 15, 700);
        Source.Radius = V;
        if (RngRad) { RngRad.value = V; SyncSliderFill(RngRad); }
        ApplyUpdate();
    });

    El('BtnOpenFormulaEditor')?.addEventListener('click', () => {
        if (!Source.UseFormula)
        {
            Source.UseFormula = true;
            if (!Source.FormulaCode) Source.FormulaCode = DEFAULT_FORMULA_CODE;
            Viewport.SetScene(State.Scene, State.SelectedToken);
            RenderOutliner();
        }
        SwitchInspectorTab('formula');
    });
}

function RenderFormulaCompilerCard()
{
    const Source = GetSelectedSource();
    const Card = El('FormulaCompilerCard');
    if (!Card) return;
    if (!Source)
    {
        Card.innerHTML = '<small>Select or add a source to compile a symbolic GPU vector equation.</small>';
        return;
    }

    const Code = Source.FormulaCode || DEFAULT_FORMULA_CODE;
    const Res = CompileFieldProgram(Code, 0);

    const QuickSymbols = [
        { Label: '·', Insert: ' · ' },
        { Label: '×', Insert: ' × ' },
        { Label: 'r̂', Insert: 'r̂' },
        { Label: 'x̂', Insert: 'x̂' },
        { Label: 'ŷ', Insert: 'ŷ' },
        { Label: 'ẑ', Insert: 'ẑ' },
        { Label: 'θ̂', Insert: 'θ̂' },
        { Label: 'ω_swirl', Insert: 'ω_swirl' },
        { Label: 'A(r)', Insert: 'A(r)' },
        { Label: 'S', Insert: 'S' },
        { Label: 'R', Insert: 'R' },
        { Label: 'curlNoise(p)', Insert: 'curlNoise(p, 48.0)' }
    ];

    Card.innerHTML = `
        <div class="property-card-head">
            <h3>Symbolic GPU Language</h3>
            <small style="color:#ffb454">${Source.Name}</small>
        </div>

        <div class="toggle-row ${Source.UseFormula ? 'on' : ''}" id="RowToggleFormulaActive">
            <div class="toggle-meta">
                <b>Evaluate Custom Formula on GPU</b>
                <small>${Source.UseFormula ? 'Active — compiled to GLSL ES 3.00 & JS' : 'Click to override this source with formula below'}</small>
            </div>
            <span class="toggle-switch"></span>
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>Presets</span><small>Click to load</small></div>
            <div class="vfl-preset-row">
                ${FORMULA_PRESETS.map((P) => `<button type="button" class="tab-chip-btn" data-vfl-preset="${P.Id}" style="height:24px; min-height:24px; font-size:10px;">${P.Name}</button>`).join('')}
            </div>
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>Symbol Palette</span><small>Insert at cursor</small></div>
            <div class="vfl-palette-bar">
                ${QuickSymbols.map((Q) => `<button type="button" class="vfl-sym-btn" data-vfl-insert="${Q.Insert}">${Q.Label}</button>`).join('')}
            </div>
        </div>

        <div class="vfl-editor-wrap ${Res.Ok ? '' : 'has-err'}" id="VflEditorWrap">
            <pre class="vfl-code-highlight" id="VflHighlightPre" aria-hidden="true">${Res.HighlightHtml}</pre>
            <textarea class="vfl-code-input" id="VflCodeInput" spellcheck="false" autocomplete="off" autocorrect="off" autocapitalize="off" rows="5"></textarea>
        </div>

        <div class="vfl-diag-box ${Res.Ok ? 'ok' : 'err'}" id="VflDiagBox">
            ${Res.Ok
                ? `✓ Compiled cleanly · Output: <b>${Res.OutputChannel}</b> (vec3) · GPU GLSL3 + CPU JS synchronized`
                : `✗ Syntax / Type Error (Ln ${Res.Diagnostics[0]?.Line || 1}, Col ${Res.Diagnostics[0]?.Col || 1}): ${Res.Diagnostics[0]?.Message || 'Invalid expression'}`
            }
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>Strength (S)</span><small>m/s or m/s²</small></div>
            <div class="slider-row">
                <input type="range" id="RngVflStrength" min="-80" max="80" step="1" value="${Source.Strength ?? 24}" />
                <div class="split-value"><input type="number" id="NumVflStrength" step="1" value="${Math.round(Source.Strength ?? 24)}" /><span>S</span></div>
            </div>
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>Swirl (ω_swirl)</span><small>rad·m/s</small></div>
            <div class="slider-row">
                <input type="range" id="RngVflSwirl" min="-60" max="60" step="1" value="${Source.Swirl ?? 16}" />
                <div class="split-value"><input type="number" id="NumVflSwirl" step="1" value="${Math.round(Source.Swirl ?? 16)}" /><span>ω</span></div>
            </div>
        </div>

        <details style="font-size:10px; color:#8d969e;">
            <summary style="cursor:pointer; user-select:none; color:#9be0c6;">View Emitted GLSL ES 3.00 GPU Kernel</summary>
            <pre class="vfl-glsl-pre" id="VflGlslCode">${Res.Ok ? Res.GlslCode : '// Fix syntax error above to emit GLSL ES 3.00'}</pre>
        </details>
    `;

    const Ta = El('VflCodeInput');
    const Pre = El('VflHighlightPre');
    const Wrap = El('VflEditorWrap');
    const DiagBox = El('VflDiagBox');
    const GlslPre = El('VflGlslCode');
    Ta.value = Code;

    Card.querySelectorAll('input[type="range"]').forEach(SyncSliderFill);

    const ApplyCompileUpdate = (NextCode) => {
        Source.FormulaCode = NextCode;
        const Compiled = CompileFieldProgram(NextCode, 0);
        Pre.innerHTML = Compiled.HighlightHtml;
        Wrap.classList.toggle('has-err', !Compiled.Ok);
        if (Compiled.Ok)
        {
            DiagBox.className = 'vfl-diag-box ok';
            DiagBox.innerHTML = `✓ Compiled cleanly · Output: <b>${Compiled.OutputChannel}</b> (vec3) · GPU GLSL3 + CPU JS synchronized`;
            if (GlslPre) GlslPre.textContent = Compiled.GlslCode;
            if (Source.UseFormula)
            {
                Viewport.SetScene(State.Scene, State.SelectedToken);
                UpdateTelemetry(true);
            }
        }
        else
        {
            const D = Compiled.Diagnostics[0] || { Line: 1, Col: 1, Message: 'Invalid syntax' };
            DiagBox.className = 'vfl-diag-box err';
            DiagBox.textContent = `✗ Error (Ln ${D.Line}, Col ${D.Col}): ${D.Message}`;
        }
    };

    Ta.addEventListener('input', () => ApplyCompileUpdate(Ta.value));
    Ta.addEventListener('scroll', () => {
        Pre.scrollTop = Ta.scrollTop;
        Pre.scrollLeft = Ta.scrollLeft;
    });

    El('RowToggleFormulaActive').addEventListener('click', () => {
        Source.UseFormula = !Source.UseFormula;
        if (!Source.FormulaCode) Source.FormulaCode = DEFAULT_FORMULA_CODE;
        Viewport.SetScene(State.Scene, State.SelectedToken);
        RenderOutliner();
        RenderSelectedForceCard();
        RenderFormulaCompilerCard();
        UpdateTelemetry(true);
    });

    Card.querySelectorAll('[data-vfl-preset]').forEach((Btn) => {
        Btn.addEventListener('click', () => {
            const P = FORMULA_PRESETS.find((Item) => Item.Id === Btn.dataset.vflPreset);
            if (!P) return;
            Ta.value = P.Code;
            Source.UseFormula = true;
            ApplyCompileUpdate(P.Code);
            RenderOutliner();
            RenderSelectedForceCard();
        });
    });

    Card.querySelectorAll('[data-vfl-insert]').forEach((Btn) => {
        Btn.addEventListener('click', () => {
            const Ins = Btn.dataset.vflInsert;
            const S0 = Ta.selectionStart ?? Ta.value.length;
            const S1 = Ta.selectionEnd ?? Ta.value.length;
            Ta.value = Ta.value.slice(0, S0) + Ins + Ta.value.slice(S1);
            Ta.focus();
            Ta.setSelectionRange(S0 + Ins.length, S0 + Ins.length);
            ApplyCompileUpdate(Ta.value);
        });
    });

    const BindNumRange = (RngId, NumId, Prop) => {
        const R = El(RngId), N = El(NumId);
        R?.addEventListener('input', () => {
            Source[Prop] = Number(R.value);
            if (N) N.value = R.value;
            SyncSliderFill(R);
            if (Source.UseFormula) { Viewport.SetScene(State.Scene, State.SelectedToken); UpdateTelemetry(true); }
        });
        N?.addEventListener('input', () => {
            const V = Number(N.value);
            if (!Number.isFinite(V)) return;
            Source[Prop] = V;
            if (R) { R.value = V; SyncSliderFill(R); }
            if (Source.UseFormula) { Viewport.SetScene(State.Scene, State.SelectedToken); UpdateTelemetry(true); }
        });
    };
    BindNumRange('RngVflStrength', 'NumVflStrength', 'Strength');
    BindNumRange('RngVflSwirl', 'NumVflSwirl', 'Swirl');
}

function RenderGpuLayersCard()
{
    const Viz = State.Scene.Viz;
    const Card = El('GpuLayersCard');
    Card.innerHTML = `
        <div class="property-card-head">
            <h3>GPU Slice &amp; Colorimetry</h3>
            <small>${GetActiveSliceAxes(Viz).join('+')} @ ${Viz.SliceHeight} m</small>
        </div>

        <div class="metric-grid">
            <div class="prop-row">
                <div class="prop-row-head"><span>Evaluated Field</span></div>
                <select id="SelVizQuantity">
                    ${['Speed', 'Acceleration', 'Curl', 'Divergence', 'Pressure'].map((Q) => `<option value="${Q}" ${Q === Viz.Quantity ? 'selected' : ''}>${Q}</option>`).join('')}
                </select>
            </div>
            <div class="prop-row">
                <div class="prop-row-head"><span>Color Ramp</span></div>
                <select id="SelVizColormap">
                    ${['Engineering', 'Turbo', 'Plasma', 'Diverging'].map((C) => `<option value="${C}" ${C === Viz.Colormap ? 'selected' : ''}>${C}</option>`).join('')}
                </select>
            </div>
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>Slice Plane Offset</span><small>m</small></div>
            <div class="slider-row">
                <input type="range" id="RngSliceHeight" min="-60" max="180" step="2" value="${Viz.SliceHeight}" />
                <div class="split-value"><input type="number" id="NumSliceHeight" value="${Viz.SliceHeight}" /><span>m</span></div>
            </div>
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>Fixed Scale Ceiling</span><small>m/s</small></div>
            <div class="slider-row">
                <input type="range" id="RngFixedMax" min="10" max="80" step="1" value="${Viz.FixedMax}" />
                <div class="split-value"><input type="number" id="NumFixedMax" value="${Viz.FixedMax}" /><span>max</span></div>
            </div>
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>Arrow Grid Spacing</span><small>m</small></div>
            <div class="slider-row">
                <input type="range" id="RngArrowStep" min="26" max="86" step="2" value="${Viz.ArrowSpacing}" />
                <div class="split-value"><input type="number" id="NumArrowStep" value="${Viz.ArrowSpacing}" /><span>m</span></div>
            </div>
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>Heatmap Opacity</span><small>%</small></div>
            <div class="slider-row">
                <input type="range" id="RngHeatOpacity" min="0.1" max="0.95" step="0.02" value="${Viz.Opacity}" />
                <div class="split-value"><input type="number" id="NumHeatOpacity" value="${Math.round(Viz.Opacity * 100)}" /><span>%</span></div>
            </div>
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>3D Arrow Layers</span><small>1 – 3 planes</small></div>
            <div class="slider-row">
                <input type="range" id="RngVolLayers" min="1" max="3" step="1" value="${Viz.VolumeLayers}" />
                <div class="split-value"><input type="number" readonly value="${Viz.VolumeLayers}" /><span>lvl</span></div>
            </div>
        </div>

        <div class="toggle-grid">
            ${[
                ['Heatmap', 'GPU Heatmap'],
                ['Vectors', '3D GPU Arrows'],
                ['BokehRibbons', 'Bokeh Stream Ribbons'],
                ['Streamlines', 'RK4 Streamlines'],
                ['Bounds', 'Reach Bounds']
            ].map(([Key, Label]) => `
                <div class="toggle-row ${Viz[Key] ? 'on' : ''}" data-viz-toggle="${Key}">
                    <span>${Label}</span>
                    <span class="toggle-pill"></span>
                </div>
            `).join('')}
        </div>
    `;

    Card.querySelectorAll('input[type="range"]').forEach(SyncSliderFill);

    El('SelVizQuantity')?.addEventListener('change', (E) => { Viz.Quantity = E.target.value; SyncVizUi(); });
    El('SelVizColormap')?.addEventListener('change', (E) => { Viz.Colormap = E.target.value; SyncVizUi(); RenderStreamlineLayersCard(); });

    const WireRange = (RngId, NumId, Key, Transform = (V) => V) => {
        const R = El(RngId), N = El(NumId);
        R?.addEventListener('input', () => {
            Viz[Key] = Number(R.value);
            if (N) N.value = Transform(Number(R.value));
            SyncSliderFill(R);
            SyncVizUi();
        });
    };
    WireRange('RngSliceHeight', 'NumSliceHeight', 'SliceHeight');
    WireRange('RngFixedMax', 'NumFixedMax', 'FixedMax');
    WireRange('RngArrowStep', 'NumArrowStep', 'ArrowSpacing');
    WireRange('RngHeatOpacity', 'NumHeatOpacity', 'Opacity', (V) => Math.round(V * 100));

    const Lyr = El('RngVolLayers');
    Lyr?.addEventListener('input', () => {
        Viz.VolumeLayers = Number(Lyr.value);
        SyncSliderFill(Lyr);
        Lyr.parentElement.querySelector('.split-value input').value = Lyr.value;
        SyncVizUi();
    });

    Card.querySelectorAll('[data-viz-toggle]').forEach((Row) => {
        Row.addEventListener('click', () => {
            const K = Row.dataset.vizToggle;
            Viz[K] = !Viz[K];
            Row.classList.toggle('on', Viz[K]);
            SyncVizUi();
        });
    });
}

const STREAM_COLOR_PRESETS = [
    { Color: '#68dcff', Label: 'Classic Cyan' },
    { Color: '#84c7ff', Label: 'Ice Blue' },
    { Color: '#54e6a4', Label: 'Emerald' },
    { Color: '#ffc857', Label: 'Amber Gold' },
    { Color: '#ff7461', Label: 'Coral Glow' }
];

function BuildStreamlineControlsHtml(Viz, IsPopover = false)
{
    const BokehPal = Viz.BokehPalette || 'BokehTeal';
    const BokehCount = Viz.BokehCount ?? 320;
    const BokehSpeed = Viz.BokehSpeed ?? 0.45;
    const BokehThick = Viz.BokehThickness ?? 1.0;
    const BokehGlow = Viz.BokehEmissive ?? 1.0;
    const Mode = Viz.StreamColorMode || 'FieldPalette';
    const Col = Viz.StreamSingleColor || '#68dcff';
    const Thick = Viz.StreamThickness ?? 0.62;
    const Emissive = Viz.StreamEmissive ?? 1.0;
    const Opacity = Viz.StreamOpacity ?? 0.92;
    const Res = Viz.StreamResolution ?? 28;
    const Count = Viz.StreamLineCount ?? 220;

    return `
        <div class="property-card-head">
            <h3>${IsPopover ? 'Bokeh &amp; Streamline Visualizers' : 'Bokeh &amp; RK4 Streamlines'}</h3>
            <small>${BokehCount} bokeh · ${BokehSpeed.toFixed(2)}×</small>
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>Bokeh Ribbon Palette</span><small>DOF + Wake</small></div>
            <select data-stream-select="BokehPalette">
                <option value="BokehTeal" ${BokehPal === 'BokehTeal' ? 'selected' : ''}>Bokeh Mist Teal &amp; White</option>
                <option value="FollowStreams" ${BokehPal === 'FollowStreams' ? 'selected' : ''}>Follow Streamline Palette</option>
            </select>
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>Bokeh Flow Speed</span><small>${BokehSpeed.toFixed(2)}×</small></div>
            <div class="slider-row">
                <input type="range" data-stream-range="BokehSpeed" min="0.10" max="2.00" step="0.05" value="${BokehSpeed}" />
                <div class="split-value"><input type="number" data-stream-num="BokehSpeed" min="0.10" max="2.00" step="0.05" value="${BokehSpeed.toFixed(2)}" /><span>×</span></div>
            </div>
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>Bokeh Ribbon Count</span><small>${BokehCount} trails</small></div>
            <div class="slider-row">
                <input type="range" data-stream-range="BokehCount" min="80" max="1100" step="20" value="${BokehCount}" />
                <div class="split-value"><input type="number" data-stream-num="BokehCount" min="80" max="1100" step="20" value="${BokehCount}" /><span>tr</span></div>
            </div>
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>Bokeh Width &amp; Glow</span><small>${BokehThick.toFixed(2)}× · ${Math.round(BokehGlow * 100)}%</small></div>
            <div class="slider-row">
                <input type="range" data-stream-range="BokehThickness" min="0.35" max="2.20" step="0.05" value="${BokehThick}" />
                <div class="split-value"><input type="number" data-stream-num="BokehThickness" min="0.35" max="2.20" step="0.05" value="${BokehThick.toFixed(2)}" /><span>×</span></div>
            </div>
            <div class="slider-row" style="margin-top:4px;">
                <input type="range" data-stream-range="BokehEmissive" min="0.35" max="2.20" step="0.05" value="${BokehGlow}" />
                <div class="split-value"><input type="number" data-stream-num="BokehEmissive" min="0.35" max="2.20" step="0.05" value="${BokehGlow.toFixed(2)}" /><span>em</span></div>
            </div>
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>Color Visualization</span><small>Shader + LUT</small></div>
            <select data-stream-select="StreamColorMode">
                <option value="FieldPalette" ${Mode === 'FieldPalette' ? 'selected' : ''}>Speed Color Ramp (${Viz.Colormap})</option>
                <option value="CyanSingle" ${Mode === 'CyanSingle' ? 'selected' : ''}>Single Color · Classic Cyan</option>
                <option value="CustomSingle" ${Mode === 'CustomSingle' ? 'selected' : ''}>Single Color · Custom</option>
                <option value="Elevation" ${Mode === 'Elevation' ? 'selected' : ''}>Altitude 3D Gradient</option>
            </select>
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>Single Color / Tint</span><small>${Mode === 'CyanSingle' ? '#68dcff' : Col.toUpperCase()}</small></div>
            <div class="stream-color-bar">
                <input type="color" class="stream-color-input" data-stream-color value="${Mode === 'CyanSingle' ? '#68dcff' : Col}" title="Pick Single Streamline Color" />
                <div class="stream-swatch-row">
                    ${STREAM_COLOR_PRESETS.map((P) => `
                        <button type="button" class="stream-swatch ${(Mode === 'CyanSingle' && P.Color === '#68dcff') || (Mode === 'CustomSingle' && Col.toLowerCase() === P.Color.toLowerCase()) ? 'active' : ''}" data-stream-swatch="${P.Color}" style="background:${P.Color};" title="${P.Label}"></button>
                    `).join('')}
                </div>
            </div>
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>Thickness</span><small>${Thick.toFixed(2)}×</small></div>
            <div class="slider-row">
                <input type="range" data-stream-range="StreamThickness" min="0.25" max="2.00" step="0.05" value="${Thick}" />
                <div class="split-value"><input type="number" data-stream-num="StreamThickness" min="0.25" max="2.00" step="0.05" value="${Thick.toFixed(2)}" /><span>px</span></div>
            </div>
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>Emissive Glow</span><small>${Math.round(Emissive * 100)}%</small></div>
            <div class="slider-row">
                <input type="range" data-stream-range="StreamEmissive" min="0.25" max="2.40" step="0.05" value="${Emissive}" />
                <div class="split-value"><input type="number" data-stream-num="StreamEmissive" min="0.25" max="2.40" step="0.05" value="${Emissive.toFixed(2)}" /><span>×</span></div>
            </div>
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>Ribbon Opacity</span><small>${Math.round(Opacity * 100)}%</small></div>
            <div class="slider-row">
                <input type="range" data-stream-range="StreamOpacity" min="0.15" max="1.00" step="0.03" value="${Opacity}" />
                <div class="split-value"><input type="number" data-stream-num="StreamOpacity" min="15" max="100" step="3" value="${Math.round(Opacity * 100)}" /><span>%</span></div>
            </div>
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>Curve Resolution</span><small>steps / dir</small></div>
            <div class="slider-row">
                <input type="range" data-stream-range="StreamResolution" min="10" max="64" step="2" value="${Res}" />
                <div class="split-value"><input type="number" data-stream-num="StreamResolution" min="10" max="64" step="2" value="${Res}" /><span>seg</span></div>
            </div>
        </div>

        <div class="prop-row">
            <div class="prop-row-head"><span>Number of Lines</span><small>3D seeds</small></div>
            <div class="slider-row">
                <input type="range" data-stream-range="StreamLineCount" min="40" max="480" step="10" value="${Count}" />
                <div class="split-value"><input type="number" data-stream-num="StreamLineCount" min="40" max="480" step="10" value="${Count}" /><span>ln</span></div>
            </div>
        </div>
    `;
}

function SyncAllStreamlineContainers(SourceHost = null)
{
    const Viz = State.Scene.Viz;
    const SelTop = El('SelectStreamVis');
    if (SelTop) SelTop.value = Viz.StreamColorMode || 'FieldPalette';
    for (const HostId of ['StreamOptionsPopover', 'StreamlineLayersCard'])
    {
        const Host = El(HostId);
        if (!Host || Host === SourceHost) continue;
        RenderStreamlineHost(Host, HostId === 'StreamOptionsPopover');
    }
}

function RenderStreamlineHost(Host, IsPopover = false)
{
    if (!Host) return;
    const Viz = State.Scene.Viz;
    Host.innerHTML = BuildStreamlineControlsHtml(Viz, IsPopover);
    Host.querySelectorAll('input[type="range"]').forEach(SyncSliderFill);

    const ApplyUniformInstant = () => {
        Viewport.SyncUniforms(State.SimTime, true);
        Viewport.FallbackHeatDirty = true;
    };

    const UpdateHeaderMeta = () => {
        const Sm = Host.querySelector('.property-card-head small');
        if (Sm) Sm.textContent = `${Viz.BokehCount ?? 320} bokeh · ${(Viz.BokehSpeed ?? 0.45).toFixed(2)}×`;
    };

    Host.querySelector('[data-stream-select="BokehPalette"]')?.addEventListener('change', (E) => {
        Viz.BokehPalette = E.target.value;
        SyncVizUi();
        RenderStreamlineHost(Host, IsPopover);
        SyncAllStreamlineContainers(Host);
    });

    Host.querySelector('[data-stream-select="StreamColorMode"]')?.addEventListener('change', (E) => {
        Viz.StreamColorMode = E.target.value;
        if (Viz.StreamColorMode === 'CyanSingle') Viz.StreamSingleColor = '#68dcff';
        SyncVizUi();
        RenderStreamlineHost(Host, IsPopover);
        SyncAllStreamlineContainers(Host);
    });

    Host.querySelector('[data-stream-color]')?.addEventListener('input', (E) => {
        Viz.StreamSingleColor = E.target.value;
        Viz.StreamColorMode = E.target.value.toLowerCase() === '#68dcff' ? 'CyanSingle' : 'CustomSingle';
        SyncVizUi();
        const Sel = Host.querySelector('[data-stream-select="StreamColorMode"]');
        if (Sel) Sel.value = Viz.StreamColorMode;
        Host.querySelectorAll('[data-stream-swatch]').forEach((B) => B.classList.toggle('active', B.dataset.streamSwatch.toLowerCase() === Viz.StreamSingleColor.toLowerCase()));
        SyncAllStreamlineContainers(Host);
    });

    Host.querySelectorAll('[data-stream-swatch]').forEach((Btn) => {
        Btn.addEventListener('click', () => {
            Viz.StreamSingleColor = Btn.dataset.streamSwatch;
            Viz.StreamColorMode = Viz.StreamSingleColor.toLowerCase() === '#68dcff' ? 'CyanSingle' : 'CustomSingle';
            SyncVizUi();
            RenderStreamlineHost(Host, IsPopover);
            SyncAllStreamlineContainers(Host);
        });
    });

    const UniformKeys = new Set(['StreamThickness', 'StreamEmissive', 'StreamOpacity', 'BokehThickness', 'BokehEmissive', 'BokehSpeed']);
    Host.querySelectorAll('[data-stream-range]').forEach((Rng) => {
        const Key = Rng.dataset.streamRange;
        const Num = Host.querySelector(`[data-stream-num="${Key}"]`);
        const Small = Rng.closest('.prop-row')?.querySelector('.prop-row-head small');
        const FormatNum = (V) => Key === 'StreamOpacity' ? Math.round(V * 100) : (UniformKeys.has(Key) ? V.toFixed(2) : Math.round(V));
        const FormatTag = (V) => {
            if (Key === 'BokehThickness' || Key === 'BokehEmissive') return `${(Viz.BokehThickness ?? 1).toFixed(2)}× · ${Math.round((Viz.BokehEmissive ?? 1) * 100)}%`;
            if (Key === 'BokehSpeed') return `${V.toFixed(2)}×`;
            if (Key === 'BokehCount') return `${Math.round(V)} trails`;
            if (Key === 'StreamThickness') return `${V.toFixed(2)}×`;
            if (Key === 'StreamEmissive' || Key === 'StreamOpacity') return `${Math.round(V * 100)}%`;
            return Small?.textContent;
        };

        Rng.addEventListener('input', () => {
            const V = Number(Rng.value);
            Viz[Key] = V;
            if (Num) Num.value = FormatNum(V);
            if (Small) Small.textContent = FormatTag(V);
            SyncSliderFill(Rng);
            UpdateHeaderMeta();
            if (UniformKeys.has(Key)) ApplyUniformInstant();
            else SyncVizUi();
            SyncAllStreamlineContainers(Host);
        });

        Num?.addEventListener('input', () => {
            const Raw = Number(Num.value);
            if (!Number.isFinite(Raw)) return;
            const V = Key === 'StreamOpacity' ? Clamp(Raw / 100, 0.15, 1.0) : Clamp(Raw, Number(Rng.min), Number(Rng.max));
            Viz[Key] = V;
            Rng.value = V;
            if (Small) Small.textContent = FormatTag(V);
            SyncSliderFill(Rng);
            UpdateHeaderMeta();
            if (UniformKeys.has(Key)) ApplyUniformInstant();
            else SyncVizUi();
            SyncAllStreamlineContainers(Host);
        });
    });
}

function RenderStreamlineLayersCard()
{
    RenderStreamlineHost(El('StreamOptionsPopover'), true);
    RenderStreamlineHost(El('StreamlineLayersCard'), false);
}

function RenderSpatialCacheCard()
{
    const Budget = ResolveCascadeBudget(State.Scene);
    const TomlText = EncodeSimulationSpaceToml(State.Scene).join('\n');
    const Card = El('SpatialCacheCard');
    Card.innerHTML = `
        <div class="property-card-head">
            <h3>4-Tier Cascade Clipmap</h3>
            <small>${(Budget.TotalBytes / (1024 * 1024)).toFixed(1)} MB Total</small>
        </div>
        <div class="metric-grid">
            ${Budget.Tiers.map((T) => `
                <div class="metric-cell">
                    <small>Cascade ${T.Tier}</small>
                    <b>${T.CellMetres}m · ${T.SpanMetres}m</b>
                </div>
            `).join('')}
        </div>
        <div class="property-card-head" style="margin-top:4px;">
            <h3>SimulationSpace TOML</h3>
            <button type="button" class="tab-chip-btn" id="BtnCopyToml">Copy</button>
        </div>
        <textarea readonly rows="11">${TomlText}</textarea>
    `;

    El('BtnCopyToml')?.addEventListener('click', () => {
        navigator.clipboard?.writeText(TomlText);
        El('BtnCopyToml').textContent = 'Copied';
        setTimeout(() => { if (El('BtnCopyToml')) El('BtnCopyToml').textContent = 'Copy'; }, 1200);
    });
}

function SyncVizUi()
{
    const Viz = State.Scene.Viz;
    Viewport.SyncUniforms(State.SimTime, true);
    Viewport.UpdateProbeAndHeatmapGeometry();
    Viewport.MarkStreamlinesDirty();

    El('ColorbarLabel').textContent = Viz.Quantity;
    El('ColorbarRamp').style.background = Viz.Quantity === 'Divergence'
        ? COLORMAP_GRADIENTS.Diverging
        : (COLORMAP_GRADIENTS[Viz.Colormap] || COLORMAP_GRADIENTS.Engineering);
    El('ColorbarMax').textContent = `${Viz.FixedMax} ${Viz.Quantity === 'Pressure' ? 'Pa' : 'm/s'}`;

    const ActiveAxes = GetActiveSliceAxes(Viz);
    const All3 = ActiveAxes.length === 3;
    El('SliceAxisButtons').querySelectorAll('button').forEach((B) => {
        const Ax = B.dataset.axis;
        B.classList.toggle('active', Ax === 'ALL' ? All3 : ActiveAxes.includes(Ax));
    });
    El('VizLayerToggles').querySelectorAll('button').forEach((B) => {
        B.classList.toggle('active', Boolean(Viz[B.dataset.layer]));
    });
    const SelStream = El('SelectStreamVis');
    if (SelStream) SelStream.value = Viz.StreamColorMode || 'FieldPalette';
    const FooterStream = El('FooterStreamMode');
    if (FooterStream)
    {
        FooterStream.textContent = (Viz.BokehRibbons && Viz.Streamlines)
            ? 'BOKEH + RK4'
            : (Viz.BokehRibbons ? 'BOKEH RIBBONS' : (Viz.Streamlines ? 'RK4 RIBBONS' : 'HIDDEN'));
    }

    const ProjMode = Viewport.ViewMode || '3d';
    const AxisTag = All3 ? 'XZ+XY+YZ · Vol' : ActiveAxes.join('+');
    El('LeftPaneBadge').textContent = ProjMode === '2d'
        ? `2D Cross-Section (${Viz.SliceAxis}) · Ortho`
        : `3D Field (${AxisTag})`;
    El('RightPaneBadge').textContent = `2D Cross-Section (${Viz.SliceAxis}) · Ortho`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      WIND ANEMOMETER & PROBE TELEMETRY LOOP (~12 HZ)
//------------------------------------------------------------------------------------------------------------------------

function UpdateTelemetry(ForcePushHistory = false)
{
    const [Px, Py, Pz] = State.Scene.Probe;
    const Comp = EvaluateComposite(State.Scene, Px, Py, Pz, State.SimTime, {
        Solo: State.Scene.Viz.SoloToken || null,
        IncludeBreakdown: true
    });
    const Diff = SampleDifferential(State.Scene, Px, Py, Pz, State.SimTime, 4.0, {
        Solo: State.Scene.Viz.SoloToken || null
    });

    if (ForcePushHistory || State.AnemHistory.length === 0 || State.AnemHistory.length < 72)
    {
        State.AnemHistory.push(Comp.TotalMag);
        if (State.AnemHistory.length > 72) State.AnemHistory.shift();
    }

    // Wind Anemometer Hero Number (split integer + dimmed decimal)
    const Str = Comp.TotalMag.toFixed(1);
    const [IntPart, DecPart] = Str.split('.');
    El('AnemSpeedDisplay').innerHTML = `${IntPart}<span class="mp-dec">.${DecPart}</span><span class="mp-unit">m/s</span>`;

    // Compass bearing of horizontal flow
    const Horiz = Math.hypot(Comp.TotalVec[0], Comp.TotalVec[2]);
    let Deg = 0;
    if (Horiz > 0.05)
    {
        Deg = (Math.atan2(Comp.TotalVec[0], -Comp.TotalVec[2]) * 180 / Math.PI + 360) % 360;
    }
    El('AnemDirPill').textContent = `${CompassOf(Deg)} · ${Math.round(Deg)}°`;

    const Sum = SummarizeHistory(State.AnemHistory);
    El('AnemBeaufortBadge').textContent = `Bft ${Sum.Beaufort.Force} · ${Sum.Beaufort.Name}`;
    El('AnemDescText').innerHTML = `<strong>Beaufort ${Sum.Beaufort.Force}</strong> — ${Sum.Beaufort.Land}.`;
    El('AnemKnots').textContent = `${(Comp.TotalMag * 1.94384).toFixed(1)} kt`;
    El('AnemKmh').textContent = `${(Comp.TotalMag * 3.6).toFixed(1)}`;
    El('AnemDynQ').textContent = `${Math.round(Comp.DynamicPressurePa)} Pa`;
    El('AnemAlt').textContent = `${Math.round(Py)} m`;
    El('AnemPeak').textContent = `${Sum.Peak.toFixed(1)} m/s`;
    El('AnemLull').textContent = `${Sum.Lull.toFixed(1)} m/s`;
    El('BeaufortPin').style.left = `${(BeaufortBarShare(Comp.TotalMag) * 100).toFixed(1)}%`;

    DrawAnemometerTrace(El('AnemometerCanvas'), State.AnemHistory);

    // Probe Tensor Metrics & Footer
    El('ProbeChannelCount').textContent = `${Comp.Breakdown?.length || 0} Contributing`;
    El('ProbeFlowVal').textContent = `${Comp.Speed.toFixed(1)} m/s`;
    El('ProbeAccelVal').textContent = `${Comp.AccelMag.toFixed(1)} m/s²`;
    El('ProbeCurlVal').textContent = `${Diff.CurlMag.toFixed(3)} /s`;
    El('ProbeDivVal').textContent = `${Diff.Divergence >= 0 ? '+' : ''}${Diff.Divergence.toFixed(3)} /s`;

    El('FooterProbePos').textContent = `[${Math.round(Px)}, ${Math.round(Py)}, ${Math.round(Pz)}] m`;
    El('FooterProbeMag').textContent = `${Comp.TotalMag.toFixed(1)} m/s`;
    El('FooterProbeCurl').textContent = `${Diff.CurlMag.toFixed(3)} /s`;
    El('FooterProbeDiv').textContent = `${Diff.Divergence >= 0 ? '+' : ''}${Diff.Divergence.toFixed(3)} /s`;

    El('FooterFps').textContent = `REALTIME · ${Math.round(Viewport.FpsSmoothed)} FPS`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                         .CONSTRUCT-DIALOG (19-LAW MODAL PALETTE)
//------------------------------------------------------------------------------------------------------------------------

function RenderConstructDialog()
{
    const Query = El('ConstructSearchInput').value.trim().toLowerCase();
    const Container = El('ConstructGridContainer');
    const Order = ['VectorField', 'FlowField', 'TurbulenceField'];

    Container.innerHTML = Order.map((SysKey) => {
        const Sys = RuntimeSystems[SysKey];
        const Items = ForceModels.filter((M) => M.System === SysKey && (!Query || `${M.Name} ${M.Id} ${M.Blurb}`.toLowerCase().includes(Query)));
        if (!Items.length) return '';
        return `
            <div>
                <div class="construct-cat-title" style="color:${Sys.Colour}">${Sys.Label} (${Items.length})</div>
                <div class="construct-grid">
                    ${Items.map((M) => `
                        <button type="button" class="construct-tile" data-spawn-model="${M.Id}">
                            <span class="construct-tile-top">
                                ${GetForceModelSvgIcon(M.Id, M.Colour)}
                                <span>${M.Name}</span>
                            </span>
                            <small>${M.Blurb}</small>
                        </button>
                    `).join('')}
                </div>
            </div>
        `;
    }).join('');

    Container.querySelectorAll('[data-spawn-model]').forEach((Btn) => {
        Btn.addEventListener('click', () => {
            AddForceSource(Btn.dataset.spawnModel);
            El('ConstructBackdrop').classList.add('hidden');
        });
    });
}

function OpenConstructDialog()
{
    RenderConstructDialog();
    El('ConstructBackdrop').classList.remove('hidden');
    El('ConstructSearchInput').focus();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    GLOBAL UI WIRING
//------------------------------------------------------------------------------------------------------------------------

function RenderAll()
{
    RenderTabs();
    RenderOutliner();
    SyncProbeInputs();
    RenderSelectedForceCard();
    RenderFormulaCompilerCard();
    RenderGpuLayersCard();
    RenderStreamlineLayersCard();
    RenderSpatialCacheCard();
    SyncVizUi();
    UpdateTelemetry(true);
    const ZEl = El('ZoomPctLabel');
    if (ZEl) ZEl.textContent = `${Viewport.GetZoomPercent()}%`;
}

El('BtnConstructOpen').addEventListener('click', OpenConstructDialog);
El('BtnQuickFormula').addEventListener('click', AddFormulaSource);
El('BtnZoomIn').addEventListener('click', () => Viewport.ZoomIn());
El('BtnZoomOut').addEventListener('click', () => Viewport.ZoomOut());
El('BtnZoomFit').addEventListener('click', () => Viewport.FitScene());
El('ViewportGpuBadge').addEventListener('click', () => {
    El('ShaderCompileOverlay').classList.toggle('hidden');
});
El('BtnTabAdd').addEventListener('click', OpenConstructDialog);
El('BtnConstructClose').addEventListener('click', () => El('ConstructBackdrop').classList.add('hidden'));
El('ConstructBackdrop').addEventListener('click', (E) => {
    if (E.target === El('ConstructBackdrop')) El('ConstructBackdrop').classList.add('hidden');
});
El('ConstructSearchInput').addEventListener('input', RenderConstructDialog);

window.addEventListener('keydown', (E) => {
    const Editing = ['INPUT', 'TEXTAREA', 'SELECT'].includes(document.activeElement?.tagName);
    if ((E.ctrlKey || E.metaKey) && E.key.toLowerCase() === 'a' && !Editing)
    {
        E.preventDefault();
        OpenConstructDialog();
    }
    else if (E.key === 'Escape')
    {
        El('ConstructBackdrop').classList.add('hidden');
        El('StreamOptionsPopover')?.classList.add('hidden');
        El('BtnStreamOptions')?.classList.remove('open');
    }
    else if (!Editing && E.code === 'Space')
    {
        E.preventDefault();
        if (State.RunMode === 'edit')
        {
            State.RunMode = 'play';
            State.PlayPaused = false;
        }
        else
        {
            State.PlayPaused = !State.PlayPaused;
        }
        SyncTransportUi();
    }
    else if (!Editing && (E.key === '=' || E.key === '+'))
    {
        Viewport.ZoomIn();
    }
    else if (!Editing && E.key === '-')
    {
        Viewport.ZoomOut();
    }
    else if (!Editing && E.key === '0')
    {
        Viewport.FitScene();
    }
});

// Notch & Top Buttons
const ToggleNotch = () => El('ControlNotch').classList.toggle('hidden');
El('BtnToggleNotch').addEventListener('click', ToggleNotch);
El('BtnOutlinerNotch').addEventListener('click', ToggleNotch);

El('BtnTriggerImpulse').addEventListener('click', () => {
    for (const S of State.Scene.Sources)
    {
        if (S.Duration === 'Impulse') S.TriggeredAt = State.SimTime;
    }
    Viewport.SetScene(State.Scene, State.SelectedToken);
});
El('BtnResetCamera').addEventListener('click', () => Viewport.ResetCamera());
El('BtnFocusSelected').addEventListener('click', () => {
    const S = GetSelectedSource();
    if (S) Viewport.FocusOn(S.Centre);
});
El('BtnResetProbe').addEventListener('click', () => {
    State.Scene.Probe = [0, State.Scene.Viz.SliceHeight, 0];
    SyncProbeInputs();
    Viewport.UpdateProbeAndHeatmapGeometry();
    UpdateTelemetry(true);
});
El('BtnClearSolo').addEventListener('click', () => {
    State.Scene.Viz.SoloToken = null;
    Viewport.SetScene(State.Scene, State.SelectedToken);
    RenderOutliner();
});
document.querySelectorAll('[data-quick-add]').forEach((Btn) => {
    Btn.addEventListener('click', () => AddForceSource(Btn.dataset.quickAdd));
});

// Outliner filters & search
El('OutlinerSearchInput').addEventListener('input', (E) => {
    State.SearchText = E.target.value;
    RenderOutliner();
});
El('BtnStatActive').addEventListener('click', () => {
    State.MuteFilter = State.MuteFilter === 'Active' ? 'All' : 'Active';
    RenderOutliner();
});
El('BtnStatMuted').addEventListener('click', () => {
    State.MuteFilter = State.MuteFilter === 'Muted' ? 'All' : 'Muted';
    RenderOutliner();
});
El('BtnOutlinerMuteFilter').addEventListener('click', () => {
    const Cycle = ['All', 'Active', 'Muted'];
    State.MuteFilter = Cycle[(Cycle.indexOf(State.MuteFilter) + 1) % Cycle.length];
    RenderOutliner();
});
El('SystemFilterChips').querySelectorAll('button').forEach((Btn) => {
    Btn.addEventListener('click', () => {
        State.SystemFilter = Btn.dataset.sys;
        El('SystemFilterChips').querySelectorAll('button').forEach((B) => B.classList.toggle('active', B === Btn));
        RenderOutliner();
    });
});

// Viewport Toolbar (Edit / Simulate / Play + Pause + Rewind)
El('ViewportModes').querySelectorAll('button[data-mode]').forEach((Btn) => {
    Btn.addEventListener('click', () => {
        State.RunMode = Btn.dataset.mode;
        if (State.RunMode !== 'edit') State.PlayPaused = false;
        SyncTransportUi();
    });
});
El('BtnPlayPause')?.addEventListener('click', () => {
    if (State.RunMode === 'edit')
    {
        State.RunMode = 'play';
        State.PlayPaused = false;
    }
    else
    {
        State.PlayPaused = !State.PlayPaused;
    }
    SyncTransportUi();
});
El('BtnResetTime')?.addEventListener('click', () => {
    State.SimTime = 0;
    for (const S of State.Scene.Sources)
    {
        if (S.Duration === 'Impulse') S.TriggeredAt = 0;
    }
    const Changed = StepAnimatedSources(State.Scene, 0, true);
    if (Changed.length) Viewport.SyncAnimatedSourcesFrame(Changed);
    SyncSelectedCentreInputs();
    SyncTransportUi();
    UpdateTelemetry(true);
});
El('SelectViewProjection').addEventListener('change', (E) => {
    const Mode = E.target.value;
    Viewport.SetViewMode(Mode);
    El('SceneImageFrame').classList.toggle('split-active', Mode === 'split');
    SyncVizUi();
});
El('SliceAxisButtons').querySelectorAll('button').forEach((Btn) => {
    Btn.addEventListener('click', () => {
        const Viz = State.Scene.Viz;
        if (!Viz.SliceAxes) Viz.SliceAxes = { XZ: Viz.SliceAxis === 'XZ', XY: Viz.SliceAxis === 'XY', YZ: Viz.SliceAxis === 'YZ' };
        const Axis = Btn.dataset.axis;
        if (Axis === 'ALL')
        {
            const AllOn = Viz.SliceAxes.XZ && Viz.SliceAxes.XY && Viz.SliceAxes.YZ;
            if (AllOn)
            {
                const Keep = Viz.SliceAxis || 'XZ';
                Viz.SliceAxes = { XZ: Keep === 'XZ', XY: Keep === 'XY', YZ: Keep === 'YZ' };
            }
            else
            {
                Viz.SliceAxes = { XZ: true, XY: true, YZ: true };
            }
        }
        else
        {
            const ActiveList = GetActiveSliceAxes(Viz);
            if (Viz.SliceAxes[Axis] && ActiveList.length === 1) return;
            Viz.SliceAxes[Axis] = !Viz.SliceAxes[Axis];
            Viz.SliceAxis = Viz.SliceAxes[Axis] ? Axis : GetActiveSliceAxes(Viz)[0];
        }
        SyncVizUi();
        Viewport.RebuildSourceBeacons();
        RenderGpuLayersCard();
    });
});
El('VizLayerToggles').querySelectorAll('button').forEach((Btn) => {
    Btn.addEventListener('click', () => {
        const Key = Btn.dataset.layer;
        State.Scene.Viz[Key] = !State.Scene.Viz[Key];
        SyncVizUi();
        RenderGpuLayersCard();
    });
});
El('SelectStreamVis')?.addEventListener('change', (E) => {
    State.Scene.Viz.StreamColorMode = E.target.value;
    if (E.target.value === 'CyanSingle') State.Scene.Viz.StreamSingleColor = '#68dcff';
    SyncVizUi();
    RenderStreamlineLayersCard();
});
El('BtnStreamOptions')?.addEventListener('click', (E) => {
    E.stopPropagation();
    const Pop = El('StreamOptionsPopover');
    const Open = Pop.classList.toggle('hidden') === false;
    El('BtnStreamOptions').classList.toggle('open', Open);
});
window.addEventListener('pointerdown', (E) => {
    const Wrap = El('StreamVisWrap');
    const Pop = El('StreamOptionsPopover');
    if (Pop && !Pop.classList.contains('hidden') && Wrap && !Wrap.contains(E.target))
    {
        Pop.classList.add('hidden');
        El('BtnStreamOptions')?.classList.remove('open');
    }
});

// Inspector Tabs & Actions
El('InspectorTabs').querySelectorAll('button').forEach((Btn) => {
    Btn.addEventListener('click', () => SwitchInspectorTab(Btn.dataset.tab));
});
El('BtnDeleteSelected').addEventListener('click', () => {
    const Idx = State.Scene.Sources.findIndex((S) => S.Token === State.SelectedToken);
    if (Idx < 0) return;
    State.Scene.Sources.splice(Idx, 1);
    State.SelectedToken = State.Scene.Sources[Math.max(0, Idx - 1)]?.Token || null;
    Viewport.SetScene(State.Scene, State.SelectedToken);
    RenderAll();
});

// Probe Coordinate Inputs
['ProbeInputX', 'ProbeInputY', 'ProbeInputZ'].forEach((Id, Axis) => {
    El(Id).addEventListener('input', () => {
        const V = Number(El(Id).value);
        if (!Number.isFinite(V)) return;
        State.Scene.Probe[Axis] = V;
        Viewport.UpdateProbeAndHeatmapGeometry();
        UpdateTelemetry(true);
    });
});

window.addEventListener('resize', () => Viewport.Resize());

// Initial Boot & Unthrottled 60-120 FPS WebGL2 GPU Render Loop
Viewport.SetScene(State.Scene, State.SelectedToken);
RenderAll();
SyncTransportUi();

function Tick(NowMs)
{
    const Dt = Math.min(0.05, Math.max(0.001, (NowMs - State.LastAnimMs) * 0.001));
    State.LastAnimMs = NowMs;

    const StepMult = State.RunMode === 'play' ? 1.35 : 1.0;
    const IsRunning = State.RunMode !== 'edit' && !State.PlayPaused;
    if (IsRunning)
    {
        State.SimTime += Dt * StepMult;
        const ChangedTokens = StepAnimatedSources(State.Scene, State.SimTime, true);
        if (ChangedTokens.length > 0)
        {
            Viewport.SyncAnimatedSourcesFrame(ChangedTokens);
            SyncSelectedCentreInputs();
        }
    }

    Viewport.RenderFrame(State.SimTime, Dt * StepMult, IsRunning);

    if (NowMs - State.LastTelemetryMs >= 140)
    {
        State.LastTelemetryMs = NowMs;
        SyncTransportUi();
        UpdateTelemetry(true);
    }
    requestAnimationFrame(Tick);
}
requestAnimationFrame(Tick);
