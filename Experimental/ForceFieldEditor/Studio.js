/*============================================================================================================================================
                                                              STUDIO.JS
============================================================================================================================================*/
// Studio shell for the Frontier ForceField Editor HTML reference studio: Project-Zero dock layout, force generator registry,
// Construct drawer, canvas HUD, field meter, playback clock, adaptive quality, persistence, and the main render loop.

"use strict";

//------------------------------------------------------------------------------------------------------------------------
//                                                STATUS & DEFAULTS
//------------------------------------------------------------------------------------------------------------------------

const DefaultOverlay = () => ({
  HeatmapEnabled: true,
  Quantity: "Speed",
  Ramp: "Engineering",
  FixedMax: 24,
  HeatmapOpacity: 0.62,
  SliceDensityChoice: "Medium",
  HeatmapResolution: 72,
  ArrowsEnabled: true,
  ArrowSpacing: 0.5,
  VectorScale: 1.0,
  StreamlinesEnabled: true,
  StreamlineCount: 64,
  StreamlineThickness: 1.6,
  StreamDrift: 1.2,
  StreamColourMode: "Ramp",
  StreamColour: "#68dcff",
  ParticlesEnabled: true,
  ParticleCount: 2400,
  ParticleSize: 1.9,
  TrailLength: 6,
  ParticleRamp: "Engineering",
  EnvelopesEnabled: true,
  MeterEnabled: true,
  GridEnabled: true,
  SliceDepth: 0.25,
});

const StudioStatus = {
  Generators: [],
  SelectedId: null,
  GeneratorsSignature: "",
  Overlay: DefaultOverlay(),
  View: { Mode: "Top XY", Zoom: 150, Centre: [0, 0, 0.8] },
  Clock: { TimeSec: 0, Playing: true, TimeScale: 1.0 },
  ViewportDirty: true,
  DomainTint: "#4AA8FF",
  RequestDuplicate: null,
  RequestRemove: null,
  AssignViewMode: null,
  NotifyViewChanged: null,
  NotifyMeterVisibility: null,
  ApplyQualityPreset: null,
};

const SliceDensityTable = { Low: 44, Medium: 72, High: 108 };

let GeneratorSerial = 1;
let AutosaveTimer = null;
let TickDurationMs = 0;
let LastHeatmapSamples = 0;

//------------------------------------------------------------------------------------------------------------------------
//                                                    HELPERS
//------------------------------------------------------------------------------------------------------------------------

function El(Tag, ClassName, TextContent) {
  const Control = document.createElement(Tag);
  if (ClassName) Control.className = ClassName;
  if (TextContent !== undefined && TextContent !== null) Control.textContent = TextContent;
  return Control;
}

function Toast(Message) {
  const Layer = document.getElementById("ToastLayer");
  if (!Layer) return;
  Layer.textContent = "";
  const Note = El("div", "toast", Message);
  Layer.appendChild(Note);
  Note.classList.add("visible");
  window.clearTimeout(Toast.HideTimer);
  Toast.HideTimer = window.setTimeout(() => Note.classList.remove("visible"), 2600);
}

function RebuildGeneratorsSignature() {
  StudioStatus.GeneratorsSignature = StudioStatus.Generators.map((Gen) => {
    const F = Gen.Force;
    return [
      Gen.Id, F.Model, F.Enabled ? 1 : 0, F.Muted ? 1 : 0,
      F.Strength.toFixed(2), F.Radius.toFixed(2), F.CoreRadius.toFixed(2), F.SwirlRatio.toFixed(2),
      F.Attenuation, F.Reach, F.Duration, F.FalloffExponent.toFixed(2),
      F.YawDeg.toFixed(0), F.PitchDeg.toFixed(0),
      F.TurbulenceScale.toFixed(2), F.Octaves, F.Frequency.toFixed(2),
      F.MotionPattern, F.MotionSpeedHz.toFixed(2), F.MotionAmplitude.toFixed(2),
      (F.MotionWaypoints || []).map((W) => W.map((C) => C.toFixed(2)).join(",")).join(";"),
      Gen.Position.map((C) => C.toFixed(2)).join(","),
    ].join("|");
  }).join("##") + "#" + StudioStatus.Generators.length;
}

function TouchScene(NotifyViewport = true) {
  RebuildGeneratorsSignature();
  if (NotifyViewport) {
    StudioStatus.ViewportDirty = true;
    Viewport.NotifySceneChanged();
  }
  const Selected = StudioStatus.Generators.find((G) => G.Id === StudioStatus.SelectedId);
  StudioStatus.DomainTint = Selected ? (Selected.Force.Tint || GetForceLawMeta(Selected.Force.Model).Tint) : "#4AA8FF";
  RefreshRegistryRows();
  ScheduleAutosave();
}

function ScheduleAutosave() {
  window.clearTimeout(AutosaveTimer);
  AutosaveTimer = window.setTimeout(() => { PersistRig("save"); }, 700);
}

//------------------------------------------------------------------------------------------------------------------------
//                                            GENERATOR LIFE-CYCLE ACTIONS
//------------------------------------------------------------------------------------------------------------------------

function ConstructGeneratorFromLaw(LawId, PresetForce, PresetName) {
  const Law = GetForceLawMeta(LawId);
  const Count = StudioStatus.Generators.length;
  const SpreadAngle = Count * 2.39996; // golden-angle placement
  const RingRadius = Count === 0 ? 0 : 0.5 + 0.14 * Count;
  const BaseX = StudioStatus.View.Centre[0] + Math.cos(SpreadAngle) * RingRadius;
  const BaseY = StudioStatus.View.Centre[1] + Math.sin(SpreadAngle) * RingRadius;
  const BaseZ = Math.max(0.15, StudioStatus.View.Centre[2] || 0.6);
  const Force = ResolveForceValues(PresetForce ? { ...PresetForce, Enabled: true, Muted: false } : { Model: Law.Id });
  const Gen = {
    Id: "force-gen-" + GeneratorSerial + "-" + Math.random().toString(36).slice(2, 7),
    Name: PresetName || (Law.Name + (GeneratorSerial > 1 ? " " + GeneratorSerial : "")),
    Position: [Number(BaseX.toFixed(3)), Number(BaseY.toFixed(3)), Number(BaseZ.toFixed(3))],
    Rotation: [0, 0, 0],
    Force,
  };
  if ((Force.MotionPattern === "SplineCurve" || Force.MotionPattern === "LinearAB") && Force.MotionWaypoints.length < 2) {
    Force.MotionWaypoints = EnsureMotionWaypoints(Gen.Position, Force, Force.MotionPattern);
  }
  GeneratorSerial += 1;
  StudioStatus.Generators.push(Gen);
  StudioStatus.SelectedId = Gen.Id;
  TouchScene();
  RebuildInspector();
  RebuildRegistry();
  Toast("Constructed " + Gen.Name + " \u00B7 " + Law.Name);
  return Gen;
}

function DuplicateGenerator(SourceId) {
  const Source = StudioStatus.Generators.find((G) => G.Id === SourceId);
  if (!Source) return;
  const Copy = JSON.parse(JSON.stringify(Source));
  Copy.Id = "force-gen-" + GeneratorSerial + "-" + Math.random().toString(36).slice(2, 7);
  Copy.Name = Source.Name + " Copy";
  Copy.Position = [Source.Position[0] + 0.3, Source.Position[1] + 0.3, Source.Position[2]];
  GeneratorSerial += 1;
  StudioStatus.Generators.push(Copy);
  StudioStatus.SelectedId = Copy.Id;
  TouchScene();
  RebuildInspector();
  RebuildRegistry();
  Toast("Duplicated " + Source.Name);
}

function RemoveGenerator(TargetId) {
  const Idx = StudioStatus.Generators.findIndex((G) => G.Id === TargetId);
  if (Idx < 0) return;
  const [Gone] = StudioStatus.Generators.splice(Idx, 1);
  if (StudioStatus.SelectedId === TargetId) StudioStatus.SelectedId = null;
  TouchScene();
  RebuildInspector();
  RebuildRegistry();
  Toast("Removed " + Gone.Name);
}

StudioStatus.RequestDuplicate = DuplicateGenerator;
StudioStatus.RequestRemove = RemoveGenerator;

//------------------------------------------------------------------------------------------------------------------------
//                                                LEFT DOCK: REGISTRY
//------------------------------------------------------------------------------------------------------------------------

let RegistryListEl = null;

function RebuildRegistry() {
  if (!RegistryListEl) return;
  RegistryListEl.textContent = "";
  for (const Gen of StudioStatus.Generators) {
    RegistryListEl.appendChild(BuildRegistryRow(Gen));
  }
  if (!StudioStatus.Generators.length) {
    const Empty = El("div", "registry-empty", "No force generators yet. Construct one below.");
    RegistryListEl.appendChild(Empty);
  }
  UpdateRegistrySummary();
}

function BuildRegistryRow(Gen) {
  const Meta = GetForceLawMeta(Gen.Force.Model);
  const Tint = Gen.Force.Tint || Meta.Tint;
  const Row = El("div", "registry-row" + (Gen.Id === StudioStatus.SelectedId ? " selected" : ""));
  Row.dataset.GenId = Gen.Id;

  Row.appendChild(Inspector.BuildLawIcon(Gen.Force.Model, Tint));

  const TextCol = El("div", "registry-text");
  TextCol.appendChild(El("span", "registry-name", Gen.Name));
  TextCol.appendChild(El("span", "registry-law" + (Gen.Force.Enabled ? "" : " disabled"), Meta.Name));
  Row.appendChild(TextCol);

  const Controls = El("div", "registry-controls");
  const EyeBtn = El("button", "registry-button" + (Gen.Force.Enabled ? "" : " off"));
  EyeBtn.type = "button";
  EyeBtn.title = Gen.Force.Enabled ? "Disable generator" : "Enable generator";
  EyeBtn.innerHTML =
    '<svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"><path d="M1.5 8s2.4-4.2 6.5-4.2S14.5 8 14.5 8s-2.4 4.2-6.5 4.2S1.5 8 1.5 8z"/><circle cx="8" cy="8" r="2.1"/>' +
    (Gen.Force.Enabled ? "" : '<path d="M2.5 13.5L13.5 2.5"/>') +
    "</svg>";
  EyeBtn.addEventListener("click", (Ev) => {
    Ev.stopPropagation();
    Gen.Force.Enabled = !Gen.Force.Enabled;
    TouchScene();
    RebuildInspector();
    RebuildRegistry();
  });
  Controls.appendChild(EyeBtn);

  const MuteBtn = El("button", "registry-button" + (Gen.Force.Muted ? " off" : ""));
  MuteBtn.type = "button";
  MuteBtn.title = Gen.Force.Muted ? "Unmute contribution" : "Mute contribution";
  MuteBtn.textContent = Gen.Force.Muted ? "\u00D7" : "\u2013";
  MuteBtn.addEventListener("click", (Ev) => {
    Ev.stopPropagation();
    Gen.Force.Muted = !Gen.Force.Muted;
    TouchScene();
    RebuildInspector();
    RebuildRegistry();
  });
  Controls.appendChild(MuteBtn);

  const RemoveBtn = El("button", "registry-button subtle");
  RemoveBtn.type = "button";
  RemoveBtn.title = "Remove generator";
  RemoveBtn.textContent = "\u2715";
  RemoveBtn.addEventListener("click", (Ev) => {
    Ev.stopPropagation();
    RemoveGenerator(Gen.Id);
  });
  Controls.appendChild(RemoveBtn);
  Row.appendChild(Controls);

  Row.addEventListener("click", () => SelectGenerator(Gen.Id));
  return Row;
}

function RefreshRegistryRows() {
  if (!RegistryListEl) return;
  for (const Row of RegistryListEl.querySelectorAll(".registry-row")) {
    const Gen = StudioStatus.Generators.find((G) => G.Id === Row.dataset.GenId);
    if (!Gen) continue;
    Row.classList.toggle("selected", Gen.Id === StudioStatus.SelectedId);
  }
}

function SelectGenerator(GenId) {
  StudioStatus.SelectedId = GenId;
  TouchScene(false);
  RebuildInspector();
  RefreshRegistryRows();
}

function UpdateRegistrySummary() {
  const Summary = document.getElementById("RegistrySummary");
  if (!Summary) return;
  const Total = StudioStatus.Generators.length;
  const Live = StudioStatus.Generators.filter((G) => G.Force.Enabled && !G.Force.Muted).length;
  Summary.textContent = Total === 0 ? "empty scene" : Live + " of " + Total + " contributing";
}

//------------------------------------------------------------------------------------------------------------------------
//                                              CONSTRUCT DRAWER
//------------------------------------------------------------------------------------------------------------------------

function BuildConstructDrawer(LayerEl) {
  LayerEl.textContent = "";
  const Drawer = El("div", "construct-drawer");
  Drawer.appendChild(El("div", "drawer-title", "Construct Force Generator"));
  Drawer.appendChild(El("p", "drawer-subtitle", "Nineteen canonical force laws across three domains. Each construct spawns at the view centre with its law defaults."));

  for (const DomainKey of Object.keys(ForceDomains)) {
    const Domain = ForceDomains[DomainKey];
    const Group = El("div", "drawer-group");
    const Head = El("div", "drawer-group-head");
    Head.appendChild(El("span", "domain-dot"));
    Head.querySelector(".domain-dot").style.background = Domain.Tint;
    Head.appendChild(El("span", "drawer-group-title", Domain.Label));
    Group.appendChild(Head);
    Group.appendChild(El("p", "drawer-group-subtitle", Domain.Subtitle));
    for (const Law of ForceLawRepository.filter((L) => L.Domain === DomainKey)) {
      const RowEl = El("button", "drawer-law-row");
      RowEl.type = "button";
      RowEl.appendChild(Inspector.BuildLawIcon(Law.Id, Law.Tint));
      const TextCol = El("div", "drawer-law-text");
      TextCol.appendChild(El("span", "drawer-law-name", Law.Name));
      TextCol.appendChild(El("span", "drawer-law-channel", Law.Channel + " \u00B7 " + Law.Unit));
      RowEl.appendChild(TextCol);
      const Add = El("span", "drawer-law-add", "+");
      RowEl.appendChild(Add);
      RowEl.addEventListener("click", () => {
        ConstructGeneratorFromLaw(Law.Id);
        CloseConstructDrawer();
      });
      Group.appendChild(RowEl);
    }
    Drawer.appendChild(Group);
  }

  const PresetGroup = El("div", "drawer-group");
  PresetGroup.appendChild(El("div", "drawer-group-title", "Curated Engine Presets"));
  for (const Preset of ForcePresetRepository) {
    const Law = GetForceLawMeta(Preset.LawId);
    const RowEl = El("button", "drawer-law-row preset");
    RowEl.type = "button";
    RowEl.appendChild(Inspector.BuildLawIcon(Preset.LawId, Law.Tint));
    const TextCol = El("div", "drawer-law-text");
    TextCol.appendChild(El("span", "drawer-law-name", Preset.Name));
    TextCol.appendChild(El("span", "drawer-law-channel", Preset.Summary));
    RowEl.appendChild(TextCol);
    RowEl.appendChild(El("span", "drawer-law-add", "+"));
    RowEl.addEventListener("click", () => {
      ConstructGeneratorFromLaw(Preset.LawId, Preset.Force, Preset.Name);
      CloseConstructDrawer();
    });
    PresetGroup.appendChild(RowEl);
  }
  Drawer.appendChild(PresetGroup);

  const CloseBar = El("div", "drawer-close-bar");
  const CloseBtn = El("button", "construct-button slim", "Close");
  CloseBtn.type = "button";
  CloseBtn.addEventListener("click", CloseConstructDrawer);
  CloseBar.appendChild(CloseBtn);
  Drawer.appendChild(CloseBar);

  LayerEl.appendChild(Drawer);
}

function OpenConstructDrawer() {
  const Layer = document.getElementById("DrawerLayer");
  if (!Layer) return;
  BuildConstructDrawer(Layer);
  Layer.classList.add("open");
}

function CloseConstructDrawer() {
  const Layer = document.getElementById("DrawerLayer");
  if (!Layer) return;
  Layer.classList.remove("open");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 TOP STRIP & FOOTER
//------------------------------------------------------------------------------------------------------------------------

function BuildTopStrip(HostEl) {
  const Brand = El("div", "brand");
  Brand.appendChild(El("span", "brand-star", "\u2B50"));
  Brand.appendChild(El("span", "brand-name", "Frontier"));
  Brand.appendChild(El("span", "brand-studio", "ForceField Editor"));
  HostEl.appendChild(Brand);

  const Actions = El("div", "strip-actions");
  const ConstructBtn = El("button", "construct-button", "Construct\u2026");
  ConstructBtn.type = "button";
  ConstructBtn.title = "Add a force generator (C)";
  ConstructBtn.addEventListener("click", OpenConstructDrawer);
  Actions.appendChild(ConstructBtn);

  const ExportBtn = El("button", "strip-button", "Export Rig\u2026");
  ExportBtn.type = "button";
  ExportBtn.addEventListener("click", () => PersistRig("export"));
  Actions.appendChild(ExportBtn);

  const ImportBtn = El("button", "strip-button", "Import Rig\u2026");
  ImportBtn.type = "button";
  ImportBtn.addEventListener("click", () => document.getElementById("ImportInput").click());
  Actions.appendChild(ImportBtn);

  const ResetBtn = El("button", "strip-button", "Reset Scene");
  ResetBtn.type = "button";
  ResetBtn.addEventListener("click", ResetScene);
  Actions.appendChild(ResetBtn);
  HostEl.appendChild(Actions);

  const Tags = El("div", "strip-tags");
  Tags.appendChild(El("span", "strip-tag", "+Z Up"));
  Tags.appendChild(El("span", "strip-tag", "HTML Reference Studio"));
  Tags.appendChild(El("span", "strip-tag live", "EngineConnected: false"));
  HostEl.appendChild(Tags);
}

function BuildFooter(HostEl) {
  const Left = El("div", "footer-cell");
  Left.appendChild(El("span", "footer-dot"));
  Left.appendChild(El("span", null, "+Z Up \u00B7 Canvas-2D advected transport \u00B7 zero rigid-body physics"));
  HostEl.appendChild(Left);

  const Centre = El("div", "footer-cell centre");
  Centre.id = "TelemetryCell";
  HostEl.appendChild(Centre);

  const Right = El("div", "footer-cell right");
  Right.textContent = "ForceLaw Repository \u00B7 19 laws \u00B7 Frontier";
  HostEl.appendChild(Right);
}

function UpdateTelemetry() {
  const Cell = document.getElementById("TelemetryCell");
  if (!Cell) return;
  const Particles = StudioStatus.Overlay.ParticlesEnabled ? Math.min(StudioStatus.Overlay.ParticleCount, StudioStatus.Overlay.ParticleCount) : 0;
  Cell.textContent =
    "t " + StudioStatus.Clock.TimeSec.toFixed(1) + "s \u00B7 tick " + TickDurationMs.toFixed(1) + "ms \u00B7 " +
    StudioStatus.Generators.length + " generators \u00B7 " + LastHeatmapSamples + " slice samples \u00B7 " +
    Particles + " particles";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    VIEWPORT HUD
//------------------------------------------------------------------------------------------------------------------------

function BuildViewportHud(HostEl, CanvasEl) {
  const HudTopLeft = El("div", "hud hud-top-left");
  const PlaneGroup = El("div", "hud-plane-group");
  for (const Mode of SlicePlanes) {
    const Btn = El("button", "hud-plane-choice");
    Btn.type = "button";
    Btn.textContent = Mode;
    Btn.addEventListener("click", () => AssignViewMode(Mode));
    PlaneGroup.appendChild(Btn);
  }
  HudTopLeft.appendChild(PlaneGroup);
  const Depth = El("label", "hud-depth");
  Depth.appendChild(El("span", null, "Slice depth"));
  const DepthTrack = El("input");
  DepthTrack.type = "range";
  DepthTrack.min = -3.4;
  DepthTrack.max = 3.4;
  DepthTrack.step = 0.05;
  DepthTrack.setAttribute("aria-label", "Slice depth");
  DepthTrack.addEventListener("input", () => {
    StudioStatus.Overlay.SliceDepth = Number(DepthTrack.value);
    StudioStatus.NotifyViewChanged();
    TouchScene(false);
  });
  Depth.appendChild(DepthTrack);
  HudTopLeft.appendChild(Depth);
  HostEl.appendChild(HudTopLeft);

  const HudTopRight = El("div", "hud hud-top-right");
  const QuickToggles = [
    ["Heatmap", "HeatmapEnabled"],
    ["Arrows", "ArrowsEnabled"],
    ["Streamlines", "StreamlinesEnabled"],
    ["Particles", "ParticlesEnabled"],
    ["Shells", "EnvelopesEnabled"],
    ["Grid", "GridEnabled"],
  ];
  for (const [Label, Key] of QuickToggles) {
    const Btn = El("button", "hud-quick-toggle");
    Btn.type = "button";
    Btn.textContent = Label;
    Btn.addEventListener("click", () => {
      StudioStatus.Overlay[Key] = !StudioStatus.Overlay[Key];
      SyncHudToggles();
      ScheduleAutosave();
    });
    Btn.dataset.OverlayKey = Key;
    HudTopRight.appendChild(Btn);
  }
  HostEl.appendChild(HudTopRight);

  const MeterPanel = El("div", "hud hud-meter");
  MeterPanel.id = "FieldMeter";
  MeterPanel.appendChild(El("div", "meter-title", "Field Meter"));
  const MeterBody = El("div", "meter-body");
  MeterBody.id = "MeterBody";
  MeterBody.appendChild(El("div", "meter-line muted", "Hover the viewport to sample the composite field."));
  MeterPanel.appendChild(MeterBody);
  HostEl.appendChild(MeterPanel);

  const LegendPanel = El("div", "hud hud-legend");
  LegendPanel.appendChild(El("div", "legend-title", "Colour Ramp"));
  const RampBar = El("div", "legend-ramp");
  RampBar.id = "LegendRamp";
  LegendPanel.appendChild(RampBar);
  const LegendRow = El("div", "legend-labels");
  const LowLabel = El("span", null, "0");
  const HighLabel = El("span", null, "");
  HighLabel.id = "LegendHigh";
  LegendRow.appendChild(LowLabel);
  LegendRow.appendChild(HighLabel);
  LegendPanel.appendChild(LegendRow);
  LegendPanel.appendChild(El("div", "legend-note muted", "Slice quantity \u00B7 colour ramp"));
  HostEl.appendChild(LegendPanel);

  const Playback = El("div", "hud hud-playback");
  const PlayBtn = El("button", "playback-button");
  PlayBtn.id = "PlayButton";
  PlayBtn.type = "button";
  PlayBtn.addEventListener("click", TogglePlayback);
  Playback.appendChild(PlayBtn);
  const ResetClockBtn = El("button", "playback-button");
  ResetClockBtn.type = "button";
  ResetClockBtn.title = "Reset clock & particles";
  ResetClockBtn.innerHTML = '<svg width="12" height="12" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"><path d="M3 8a5 5 0 1 0 1.5-3.6"/><path d="M3 2.5V5h2.5"/></svg>';
  ResetClockBtn.addEventListener("click", ResetClock);
  Playback.appendChild(ResetClockBtn);
  const SpeedLabel = El("label", "playback-speed");
  SpeedLabel.appendChild(El("span", null, "Time scale"));
  const SpeedTrack = El("input");
  SpeedTrack.type = "range";
  SpeedTrack.min = 0;
  SpeedTrack.max = 3;
  SpeedTrack.step = 0.05;
  SpeedTrack.setAttribute("aria-label", "Time scale");
  SpeedTrack.addEventListener("input", () => {
    StudioStatus.Clock.TimeScale = Number(SpeedTrack.value);
    SyncPlaybackUi();
  });
  SpeedLabel.appendChild(SpeedTrack);
  SpeedLabel.dataset.Role = "speed";
  Playback.appendChild(SpeedLabel);
  HostEl.appendChild(Playback);

  SyncHudToggles();
  SyncPlaybackUi();
}

function SyncHudToggles() {
  const Buttons = document.querySelectorAll(".hud-quick-toggle");
  for (const Btn of Buttons) {
    const Key = Btn.dataset.OverlayKey;
    Btn.classList.toggle("checked", !!StudioStatus.Overlay[Key]);
  }
}

function SyncPlaybackUi() {
  const PlayBtn = document.getElementById("PlayButton");
  if (PlayBtn) {
    PlayBtn.innerHTML = StudioStatus.Clock.Playing
      ? '<svg width="12" height="12" viewBox="0 0 16 16" fill="currentColor"><rect x="3" y="2.5" width="3.6" height="11" rx="0.8"/><rect x="9.4" y="2.5" width="3.6" height="11" rx="0.8"/></svg>'
      : '<svg width="12" height="12" viewBox="0 0 16 16" fill="currentColor"><path d="M4 2.5l9 5.5-9 5.5z"/></svg>';
    PlayBtn.title = StudioStatus.Clock.Playing ? "Pause simulation (Space)" : "Play simulation (Space)";
    PlayBtn.classList.toggle("playing", StudioStatus.Clock.Playing);
  }
  const SpeedTrack = document.querySelector('.playback-speed input[type="range"]');
  if (SpeedTrack) SpeedTrack.value = String(StudioStatus.Clock.TimeScale);
}

function TogglePlayback() {
  StudioStatus.Clock.Playing = !StudioStatus.Clock.Playing;
  SyncPlaybackUi();
}

function ResetClock() {
  StudioStatus.Clock.TimeSec = 0;
  Viewport.ResetParticles();
  Viewport.NotifySceneChanged();
  Toast("Clock reset \u00B7 particles re-seeded");
}

function AssignViewMode(Mode) {
  StudioStatus.View.Mode = Mode;
  StudioStatus.NotifyViewChanged();
  TouchScene(false);
  SyncPlaneChoices();
  UpdateLegend();
}

function SyncPlaneChoices() {
  const Choices = document.querySelectorAll(".hud-plane-choice");
  for (const Btn of Choices) Btn.classList.toggle("active", Btn.textContent === StudioStatus.View.Mode);
}

function UpdateLegend() {
  const RampBar = document.getElementById("LegendRamp");
  const HighLabel = document.getElementById("LegendHigh");
  if (RampBar) RampBar.style.background = RampGradients[StudioStatus.Overlay.Ramp] || RampGradients.Engineering;
  if (HighLabel) {
    const Unit = StudioStatus.Overlay.Quantity === "Speed" ? " m/s" : StudioStatus.Overlay.Quantity === "Pressure" ? " Pa" : " \u00B7 a.u.";
    HighLabel.textContent = StudioStatus.Overlay.FixedMax.toFixed(1) + Unit;
  }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   FIELD METER
//------------------------------------------------------------------------------------------------------------------------

let LastMeterSample = 0;
let MeterWorldPoint = null;

function UpdateFieldMeter(TimeSec) {
  const Body = document.getElementById("MeterBody");
  const Panel = document.getElementById("FieldMeter");
  if (!Body || !Panel) return;
  Panel.classList.toggle("hidden", !StudioStatus.Overlay.MeterEnabled);
  if (!MeterWorldPoint || performance.now() - LastMeterSample < 90) return;
  LastMeterSample = performance.now();
  const P = MeterWorldPoint;
  const F = EvaluateCompositeForceAt(StudioStatus.Generators, P[0], P[1], P[2], TimeSec);
  const Speed = Math.hypot(F[0], F[1], F[2]);
  const CurlMag = EvaluateSliceQuantityAt(StudioStatus.Generators, "Curl", P[0], P[1], P[2], TimeSec, StudioStatus.View.Mode, 0.08);
  const Div = EvaluateSliceQuantityAt(StudioStatus.Generators, "Divergence", P[0], P[1], P[2], TimeSec, StudioStatus.View.Mode, 0.08);
  const Bearing = Math.atan2(F[1], F[0]) * (180 / Math.PI);
  Body.textContent = "";
  const Line1 = El("div", "meter-line");
  Line1.appendChild(El("span", "meter-key", "p"));
  Line1.appendChild(El("span", null, "(" + P.map((C) => C.toFixed(2)).join(", ") + ") m"));
  Body.appendChild(Line1);
  const Line2 = El("div", "meter-line");
  Line2.appendChild(El("span", "meter-key", "F"));
  Line2.appendChild(El("span", null, "(" + F.map((C) => C.toFixed(1)).join(", ") + ") m/s\u00B2"));
  Body.appendChild(Line2);
  const Line3 = El("div", "meter-line");
  Line3.appendChild(El("span", "meter-key", "|F|"));
  Line3.appendChild(El("span", null, Speed.toFixed(2) + " m/s \u00B7 " + ClassifyBeaufortScale(Speed)));
  Body.appendChild(Line3);
  const Line4 = El("div", "meter-line");
  Line4.appendChild(El("span", "meter-key", "\u2207"));
  Line4.appendChild(El("span", null, "curl " + CurlMag.toFixed(2) + " \u00B7 div " + (Div >= 0 ? "+" : "") + Div.toFixed(2) + " \u00B7 " + CompassFromBearingDeg(Bearing)));
  Body.appendChild(Line4);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              POINTER & KEY INTERACTION
//------------------------------------------------------------------------------------------------------------------------

let DragMode = null; // "MoveGenerator" | "PanView"
let DragGeneratorId = null;
let DragLastScreen = null;

function BindViewportInteraction(CanvasEl) {
  CanvasEl.addEventListener("pointerdown", (Ev) => {
    CanvasEl.setPointerCapture(Ev.pointerId);
    const Rect = CanvasEl.getBoundingClientRect();
    const Sx = Ev.clientX - Rect.left;
    const Sy = Ev.clientY - Rect.top;
    const Picked = Viewport.PickGenerator(Sx, Sy);
    if (Picked && !Ev.shiftKey) {
      SelectGenerator(Picked.Id);
      DragMode = "MoveGenerator";
      DragGeneratorId = Picked.Id;
    } else {
      DragMode = "PanView";
    }
    DragLastScreen = [Ev.clientX, Ev.clientY];
    CanvasEl.classList.add("grabbing");
  });

  CanvasEl.addEventListener("pointermove", (Ev) => {
    const Rect = CanvasEl.getBoundingClientRect();
    const Sx = Ev.clientX - Rect.left;
    const Sy = Ev.clientY - Rect.top;
    Viewport.NotifyPointerMove(Sx, Sy);
    MeterWorldPoint = Viewport.ScreenToWorldPlane(Sx, Sy);
    if (!DragMode) return;
    const DxScreen = Ev.clientX - DragLastScreen[0];
    const DyScreen = Ev.clientY - DragLastScreen[1];
    DragLastScreen = [Ev.clientX, Ev.clientY];
    if (DragMode === "MoveGenerator") {
      const Gen = StudioStatus.Generators.find((G) => G.Id === DragGeneratorId);
      if (Gen) {
        const Delta = Viewport.ScreenDeltaToWorld(DxScreen, DyScreen);
        Gen.Position = [Gen.Position[0] + Delta[0], Gen.Position[1] + Delta[1], Gen.Position[2] + Delta[2]];
        TouchScene(false);
      }
    } else if (DragMode === "PanView") {
      const Delta = Viewport.ScreenDeltaToWorld(DxScreen, DyScreen);
      const Centre = StudioStatus.View.Centre;
      const Axes = Viewport.PlaneAxes(StudioStatus.View.Mode);
      Centre[Axes.A] = Math.max(-9, Math.min(9, Centre[Axes.A] - Delta[0]));
      Centre[Axes.B] = Math.max(-9, Math.min(9, Centre[Axes.B] - Delta[1]));
      StudioStatus.NotifyViewChanged();
    }
  });

  const FinishDrag = (Ev) => {
    if (DragMode === "MoveGenerator") TouchScene();
    DragMode = null;
    DragGeneratorId = null;
    if (CanvasEl.classList.contains("grabbing")) CanvasEl.classList.remove("grabbing");
    if (Ev && Ev.pointerId !== undefined && CanvasEl.hasPointerCapture && CanvasEl.hasPointerCapture(Ev.pointerId)) {
      CanvasEl.releasePointerCapture(Ev.pointerId);
    }
  };
  CanvasEl.addEventListener("pointerup", FinishDrag);
  CanvasEl.addEventListener("pointercancel", FinishDrag);
  CanvasEl.addEventListener("pointerleave", () => {
    Viewport.NotifyPointerMove(null);
    MeterWorldPoint = null;
  });

  CanvasEl.addEventListener("wheel", (Ev) => {
    Ev.preventDefault();
    const ZoomFactor = Math.exp(-Ev.deltaY * 0.0011);
    const Next = Math.max(40, Math.min(900, StudioStatus.View.Zoom * ZoomFactor));
    StudioStatus.View.Zoom = Next;
    StudioStatus.NotifyViewChanged();
  }, { passive: false });

  CanvasEl.addEventListener("dblclick", (Ev) => {
    const Rect = CanvasEl.getBoundingClientRect();
    const Sx = Ev.clientX - Rect.left;
    const Sy = Ev.clientY - Rect.top;
    const Picked = Viewport.PickGenerator(Sx, Sy);
    if (Picked) {
      StudioStatus.View.Centre = [Picked.Position[0], Picked.Position[1], Picked.Position[2]];
      StudioStatus.NotifyViewChanged();
    }
  });
}

function BindKeyboard() {
  window.addEventListener("keydown", (Ev) => {
    const Tag = document.activeElement ? document.activeElement.tagName : "";
    if (Tag === "INPUT" || Tag === "TEXTAREA" || Tag === "SELECT") return;
    if (Ev.code === "Space") {
      Ev.preventDefault();
      TogglePlayback();
    } else if (Ev.key === "Delete" || Ev.key === "Backspace") {
      if (StudioStatus.SelectedId) RemoveGenerator(StudioStatus.SelectedId);
    } else if (Ev.key === "f" || Ev.key === "F") {
      const Selected = StudioStatus.Generators.find((G) => G.Id === StudioStatus.SelectedId);
      if (Selected) {
        StudioStatus.View.Centre = [Selected.Position[0], Selected.Position[1], Selected.Position[2]];
        StudioStatus.NotifyViewChanged();
      }
    } else if (Ev.key === "1") AssignViewMode("Top XY");
    else if (Ev.key === "2") AssignViewMode("Front XZ");
    else if (Ev.key === "3") AssignViewMode("Side YZ");
    else if (Ev.key === "c" || Ev.key === "C") OpenConstructDrawer();
    else if (Ev.key === "Escape") CloseConstructDrawer();
  });
}

//------------------------------------------------------------------------------------------------------------------------
//                                         ADAPTIVE QUALITY & MAIN LOOP
//------------------------------------------------------------------------------------------------------------------------

const SliceDensityLadder = [36, 52, 72, 96, 124];
let SliceDensityIndex = 2;
let TickEma = 12;

function ApplyQualityPreset(Choice) {
  StudioStatus.Overlay.SliceDensityChoice = Choice;
  if (Choice === "Low") { StudioStatus.Overlay.HeatmapResolution = 44; StudioStatus.Overlay.ParticleCount = Math.min(StudioStatus.Overlay.ParticleCount, 1200); }
  else if (Choice === "Medium") StudioStatus.Overlay.HeatmapResolution = 72;
  else if (Choice === "High") StudioStatus.Overlay.HeatmapResolution = 108;
  Viewport.NotifySceneChanged();
}
StudioStatus.ApplyQualityPreset = ApplyQualityPreset;

function AdaptQuality(TickMs) {
  if (StudioStatus.Overlay.SliceDensityChoice !== "Medium") return; // manual presets are authoritative
  TickEma = TickEma * 0.9 + TickMs * 0.1;
  if (TickEma > 30 && SliceDensityIndex > 0) {
    SliceDensityIndex -= 1;
    StudioStatus.Overlay.HeatmapResolution = SliceDensityLadder[SliceDensityIndex];
    StudioStatus.Overlay.ParticleCount = Math.max(600, Math.round(StudioStatus.Overlay.ParticleCount * 0.85));
    Viewport.NotifySceneChanged();
    TickEma = 16;
  } else if (TickEma < 11 && SliceDensityIndex < 2) {
    SliceDensityIndex += 1;
    StudioStatus.Overlay.HeatmapResolution = SliceDensityLadder[SliceDensityIndex];
    Viewport.NotifySceneChanged();
    TickEma = 16;
  }
}

let PreviousTimestamp = 0;

function MainLoop(TimestampMs) {
  const RawDelta = PreviousTimestamp ? (TimestampMs - PreviousTimestamp) / 1000 : 1 / 60;
  PreviousTimestamp = TimestampMs;
  const DtReal = Math.max(0.001, Math.min(0.05, RawDelta));
  const Clock = StudioStatus.Clock;
  const DtSim = Clock.Playing ? DtReal * Clock.TimeScale : 0;
  Clock.TimeSec += DtSim;

  const TickStart = performance.now();
  const RenderReport = Viewport.RenderTick(Clock.TimeSec, DtSim || 1 / 60);
  TickDurationMs = performance.now() - TickStart;
  LastHeatmapSamples = RenderReport.HeatmapSamples || 0;
  AdaptQuality(TickDurationMs);

  UpdateFieldMeter(Clock.TimeSec);
  UpdateTelemetry();
  UpdateLegend();
  requestAnimationFrame(MainLoop);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PERSISTENCE
//------------------------------------------------------------------------------------------------------------------------

const PersistenceKey = "frontier-forcefield-editor.v2";

function SerializeRig() {
  return JSON.stringify({
    Version: 2,
    GeneratorSerial,
    Generators: StudioStatus.Generators,
    Overlay: StudioStatus.Overlay,
    View: StudioStatus.View,
    SavedAt: new Date().toISOString(),
  });
}

function PersistRig(Mode) {
  try {
    if (Mode === "export") {
      const Blob = new Blob([SerializeRig()], { type: "application/json" });
      const Anchor = document.createElement("a");
      Anchor.href = URL.createObjectURL(Blob);
      Anchor.download = "ForceFieldRig.json";
      Anchor.click();
      URL.revokeObjectURL(Anchor.href);
      Toast("Exported ForceFieldRig.json");
      return;
    }
    window.localStorage.setItem(PersistenceKey, SerializeRig());
  } catch (Err) {
    Toast("Persistence unavailable in this browser profile.");
  }
}

function RestoreRig() {
  let Payload = null;
  try {
    Payload = JSON.parse(window.localStorage.getItem(PersistenceKey) || "null");
  } catch (Err) {
    Payload = null;
  }
  if (Payload && Payload.Version === 2 && Array.isArray(Payload.Generators)) {
    StudioStatus.Generators = Payload.Generators.map((Gen) => ({
      Id: String(Gen.Id || "force-gen-restored"),
      Name: String(Gen.Name || "Restored Generator"),
      Position: Array.isArray(Gen.Position) ? Gen.Position.map((C) => Number(C) || 0) : [0, 0, 0.6],
      Rotation: Array.isArray(Gen.Rotation) ? Gen.Rotation.map((C) => Number(C) || 0) : [0, 0, 0],
      Force: ResolveForceValues(Gen.Force || {}),
    }));
    Object.assign(StudioStatus.Overlay, DefaultOverlay(), Payload.Overlay || {});
    if (Array.isArray(Payload.View) === false && Payload.View) {
      StudioStatus.View.Mode = SlicePlanes.includes(Payload.View.Mode) ? Payload.View.Mode : "Top XY";
      StudioStatus.View.Zoom = Math.max(40, Math.min(900, Number(Payload.View.Zoom) || 150));
      if (Array.isArray(Payload.View.Centre)) StudioStatus.View.Centre = Payload.View.Centre.map((C) => Number(C) || 0);
    }
    GeneratorSerial = Number(Payload.GeneratorSerial) || StudioStatus.Generators.length + 1;
    Toast("Restored saved rig \u00B7 " + StudioStatus.Generators.length + " generators");
  } else {
    StudioStatus.Generators = ConstructDefaultForceRig();
    Toast("Loaded canonical wind-tunnel diagnostic rig");
  }
  StudioStatus.SelectedId = StudioStatus.Generators[0] ? StudioStatus.Generators[0].Id : null;
  TouchScene();
}

function ResetScene() {
  StudioStatus.Generators = ConstructDefaultForceRig();
  StudioStatus.Overlay = DefaultOverlay();
  StudioStatus.View = { Mode: "Top XY", Zoom: 150, Centre: [0, 0, 0.8] };
  StudioStatus.Clock.TimeSec = 0;
  StudioStatus.Clock.Playing = true;
  StudioStatus.SelectedId = StudioStatus.Generators[0].Id;
  SliceDensityIndex = 2;
  TouchScene();
  RebuildInspector();
  RebuildRegistry();
  RebuildOverlayTab();
  Viewport.ResetParticles();
  Viewport.NotifyViewChanged();
  SyncHudToggles();
  SyncPlaneChoices();
  SyncPlaybackUi();
  Toast("Scene reset \u00B7 canonical diagnostic rig restored");
}

function ImportRigFile(File) {
  const Reader = new FileReader();
  Reader.addEventListener("load", () => {
    try {
      const Payload = JSON.parse(String(Reader.result));
      if (!Array.isArray(Payload.Generators)) throw new Error("missing Generators");
      StudioStatus.Generators = Payload.Generators.map((Gen) => ({
        Id: String(Gen.Id || "force-gen-imported"),
        Name: String(Gen.Name || "Imported Generator"),
        Position: Array.isArray(Gen.Position) ? Gen.Position.map((C) => Number(C) || 0) : [0, 0, 0.6],
        Rotation: Array.isArray(Gen.Rotation) ? Gen.Rotation.map((C) => Number(C) || 0) : [0, 0, 0],
        Force: ResolveForceValues(Gen.Force || {}),
      }));
      if (Payload.Overlay && typeof Payload.Overlay === "object") {
        Object.assign(StudioStatus.Overlay, DefaultOverlay(), Payload.Overlay);
      }
      StudioStatus.SelectedId = StudioStatus.Generators[0] ? StudioStatus.Generators[0].Id : null;
      GeneratorSerial = StudioStatus.Generators.length + 1;
      TouchScene();
      RebuildInspector();
      RebuildRegistry();
      RebuildOverlayTab();
      Viewport.ResetParticles();
      Viewport.NotifyViewChanged();
      SyncHudToggles();
      Toast("Imported " + StudioStatus.Generators.length + " generators from " + File.name);
    } catch (Err) {
      Toast("Import failed \u00B7 not a ForceField rig document");
    }
  });
  Reader.readAsText(File);
}

StudioStatus.AssignViewMode = AssignViewMode;
StudioStatus.NotifyViewChanged = () => Viewport.NotifyViewChanged();
StudioStatus.NotifyMeterVisibility = () => UpdateFieldMeter(0);

//------------------------------------------------------------------------------------------------------------------------
//                                                 INSPECTOR BINDING
//------------------------------------------------------------------------------------------------------------------------

let InspectorGeneratorHost = null;
let InspectorOverlayHost = null;
let InspectorActiveTab = "Generator";

function RebuildInspector() {
  if (!InspectorGeneratorHost) return;
  Inspector.BuildGeneratorTab(InspectorGeneratorHost, StudioStatus, () => TouchScene());
  UpdateRegistrySummary();
}

function RebuildOverlayTab() {
  if (!InspectorOverlayHost) return;
  Inspector.BuildOverlayTab(InspectorOverlayHost, StudioStatus, () => {
    TouchScene();
    SyncHudToggles();
  });
  SyncHudToggles();
  SyncPlaneChoices();
}

function AssignInspectorTab(TabName) {
  InspectorActiveTab = TabName;
  const GenTab = document.getElementById("InspectorGeneratorTab");
  const OverlayTab = document.getElementById("InspectorOverlayTab");
  const GenBtn = document.getElementById("TabButtonGenerator");
  const OverlayBtn = document.getElementById("TabButtonOverlay");
  if (GenTab) GenTab.classList.toggle("active", TabName === "Generator");
  if (OverlayTab) OverlayTab.classList.toggle("active", TabName === "Overlay");
  if (GenBtn) GenBtn.classList.toggle("active", TabName === "Generator");
  if (OverlayBtn) OverlayBtn.classList.toggle("active", TabName === "Overlay");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        BOOT
//------------------------------------------------------------------------------------------------------------------------

function Boot() {
  if (Boot.Ran) return; // idempotent: guard against duplicate DOMContentLoaded dispatches
  Boot.Ran = true;
  const Root = document.getElementById("Editor");
  Root.textContent = "";

  // Top strip
  const Strip = El("header", "top-strip");
  BuildTopStrip(Strip);
  Root.appendChild(Strip);

  // Workspace: left dock, centre viewport, right dock
  const Workspace = El("div", "workspace");

  const LeftDock = El("aside", "dock left");
  const RegistryHead = El("div", "dock-head");
  RegistryHead.appendChild(El("span", "dock-title", "Force Generator Registry"));
  RegistryHead.appendChild(El("span", "dock-summary", ""));
  RegistryHead.lastChild.id = "RegistrySummary";
  LeftDock.appendChild(RegistryHead);
  RegistryListEl = El("div", "registry-list");
  RegistryListEl.id = "RegistryList";
  LeftDock.appendChild(RegistryListEl);
  const ConstructBar = El("div", "dock-foot");
  const ConstructBtn = El("button", "construct-button wide", "+ Construct Force Generator");
  ConstructBtn.type = "button";
  ConstructBtn.addEventListener("click", OpenConstructDrawer);
  ConstructBar.appendChild(ConstructBtn);
  LeftDock.appendChild(ConstructBar);
  Workspace.appendChild(LeftDock);

  const CentreDock = El("section", "dock centre");
  const CanvasWrap = El("div", "viewport-wrap");
  const CanvasEl = El("canvas", "viewport-canvas");
  CanvasEl.id = "ViewportCanvas";
  CanvasWrap.appendChild(CanvasEl);
  BuildViewportHud(CanvasWrap, CanvasEl);
  CentreDock.appendChild(CanvasWrap);
  Workspace.appendChild(CentreDock);

  const RightDock = El("aside", "dock right");
  const TabBar = El("div", "inspector-tab-bar");
  const GenTabBtn = El("button", "inspector-tab-button active", "Generator");
  GenTabBtn.id = "TabButtonGenerator";
  GenTabBtn.type = "button";
  GenTabBtn.addEventListener("click", () => AssignInspectorTab("Generator"));
  const OverlayTabBtn = El("button", "inspector-tab-button", "Overlay");
  OverlayTabBtn.id = "TabButtonOverlay";
  OverlayTabBtn.type = "button";
  OverlayTabBtn.addEventListener("click", () => AssignInspectorTab("Overlay"));
  TabBar.appendChild(GenTabBtn);
  TabBar.appendChild(OverlayTabBtn);
  RightDock.appendChild(TabBar);
  InspectorGeneratorHost = El("div", "inspector-tab active");
  InspectorGeneratorHost.id = "InspectorGeneratorTab";
  InspectorOverlayHost = El("div", "inspector-tab");
  InspectorOverlayHost.id = "InspectorOverlayTab";
  RightDock.appendChild(InspectorGeneratorHost);
  RightDock.appendChild(InspectorOverlayHost);
  Workspace.appendChild(RightDock);

  Root.appendChild(Workspace);

  // Footer
  const Footer = El("footer", "status-footer");
  BuildFooter(Footer);
  Root.appendChild(Footer);

  // Drawer layer + toast layer + hidden import input
  const DrawerLayer = El("div", "drawer-layer");
  DrawerLayer.id = "DrawerLayer";
  DrawerLayer.addEventListener("pointerdown", (Ev) => {
    if (Ev.target === DrawerLayer) CloseConstructDrawer();
  });
  Root.appendChild(DrawerLayer);
  const ToastLayer = El("div", "toast-layer");
  ToastLayer.id = "ToastLayer";
  Root.appendChild(ToastLayer);
  const ImportInput = El("input");
  ImportInput.type = "file";
  ImportInput.accept = "application/json";
  ImportInput.id = "ImportInput";
  ImportInput.hidden = true;
  ImportInput.addEventListener("change", () => {
    if (ImportInput.files && ImportInput.files[0]) ImportRigFile(ImportInput.files[0]);
    ImportInput.value = "";
  });
  Root.appendChild(ImportInput);

  // Viewport wiring
  Viewport.AssignCanvas(CanvasEl, StudioStatus);
  BindViewportInteraction(CanvasEl);
  BindKeyboard();

  // Restore or default scene, build inspector, start loop
  RestoreRig();
  RebuildInspector();
  RebuildOverlayTab();
  RebuildRegistry();
  SyncPlaneChoices();
  UpdateLegend();
  AssignInspectorTab("Generator");

  requestAnimationFrame(MainLoop);
}

document.addEventListener("DOMContentLoaded", Boot);

// Deliberate studio interface for validation tooling and the later C++ parity work.
window.ForceFieldStudio = {
  StudioStatus,
  Viewport,
  ForceLawRepository,
  ForcePresetRepository,
  ForceDomains,
  ResolveForceValues,
  EvaluateCompositeForceAt,
  EvaluateSliceQuantityAt,
  TraceSliceStreamline,
  SampleColourRamp,
  ClassifyBeaufortScale,
  ConstructGeneratorFromLaw,
  DuplicateGenerator,
  RemoveGenerator,
  SerializeRig,
};
