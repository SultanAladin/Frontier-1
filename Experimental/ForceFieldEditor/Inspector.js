/*============================================================================================================================================
                                                              INSPECTOR.JS
============================================================================================================================================*/
// Right-dock inspector for the Frontier ForceField Editor: parameter controls for the selected force generator plus the
// global overlay (visualization) controls. Project-Zero style compact rows: capsule scalar inputs, range tracks, segmented
// exclusive selectors, dropdowns, toggles, and colour swatches.

"use strict";

const Inspector = (function ConstructInspector() {
  const RefreshQueue = [];

  //----------------------------------------------------------------------------------------------------------------------
  //                                                 CONTROL FACTORIES
  //----------------------------------------------------------------------------------------------------------------------

  function El(Tag, ClassName, TextContent) {
    const Control = document.createElement(Tag);
    if (ClassName) Control.className = ClassName;
    if (TextContent !== undefined && TextContent !== null) Control.textContent = TextContent;
    return Control;
  }

  function BuildScalarRow(Options) {
    const { Label, Min, Max, Step, Unit, Decimals, GetScalar, AssignScalar } = Options;
    const Row = El("label", "field compact");
    const Caption = El("span", null, Label);
    const Capsule = El("div", "slider-capsule");
    const Split = El("div", "split-number");
    const NumberInput = El("input");
    NumberInput.type = "number";
    NumberInput.min = Min;
    NumberInput.max = Max;
    NumberInput.step = Step;
    NumberInput.setAttribute("aria-label", Label + " number");
    Split.appendChild(NumberInput);
    if (Unit) Split.appendChild(El("small", null, Unit));
    const Track = El("input");
    Track.type = "range";
    Track.min = Min;
    Track.max = Max;
    Track.step = Step;
    Track.setAttribute("aria-label", Label);
    Capsule.appendChild(Split);
    Capsule.appendChild(Track);
    Row.appendChild(Caption);
    Row.appendChild(Capsule);

    const Present = (Scalar) => Number(Scalar).toFixed(Decimals === undefined ? 2 : Decimals);
    const Sync = () => {
      const Scalar = Number(GetScalar());
      NumberInput.value = Present(Scalar);
      Track.value = String(Scalar);
    };
    const Publish = (Raw) => {
      const N = Number(Raw);
      if (!Number.isFinite(N)) return;
      AssignScalar(Math.max(Min, Math.min(Max, N)));
      Sync();
      Options.NotifyEdit();
    };
    NumberInput.addEventListener("change", () => Publish(NumberInput.value));
    Track.addEventListener("input", () => Publish(Track.value));
    Sync();
    RefreshQueue.push(Sync);
    return Row;
  }

  function BuildSegmentedRow(Options) {
    const { Label, Choices, GetCurrent, AssignOption } = Options;
    const Row = El("label", "field compact segmented-row");
    if (Label) Row.appendChild(El("span", null, Label));
    const Group = El("div", "segmented");
    const Buttons = Choices.map((Choice) => {
      const Btn = El("button", "segmented-choice", Choice);
      Btn.type = "button";
      Btn.addEventListener("click", () => {
        AssignOption(Choice);
        Sync();
        Options.NotifyEdit();
      });
      Group.appendChild(Btn);
      return { Choice, Btn };
    });
    const Sync = () => {
      const Current = GetCurrent();
      for (const Entry of Buttons) Entry.Btn.classList.toggle("active", Entry.Choice === Current);
    };
    Row.appendChild(Group);
    Sync();
    if (Options.Refreshable !== false) RefreshQueue.push(Sync);
    return Row;
  }

  function BuildDropdownRow(Options) {
    const { Label, Entries, GetCurrent, AssignOption } = Options;
    const Row = El("label", "field compact dropdown-row");
    Row.appendChild(El("span", null, Label));
    const Select = El("select");
    Select.setAttribute("aria-label", Label);
    for (const Entry of Entries) {
      const Opt = El("option", null, Entry.Label);
      Opt.value = Entry.Id;
      Select.appendChild(Opt);
    }
    Select.addEventListener("change", () => {
      AssignOption(Select.value);
      Sync();
      Options.NotifyEdit();
    });
    const Sync = () => { Select.value = GetCurrent(); };
    Row.appendChild(Select);
    Sync();
    if (Options.Refreshable !== false) RefreshQueue.push(Sync);
    return Row;
  }

  function BuildToggleRow(Options) {
    const { Label, GetEnabled, AssignEnabled } = Options;
    const Row = El("label", "field compact toggle-row");
    Row.appendChild(El("span", null, Label));
    const Switch = El("button", "toggle-switch");
    Switch.type = "button";
    Switch.setAttribute("role", "switch");
    const Knob = El("i", "knob");
    Switch.appendChild(Knob);
    Switch.addEventListener("click", () => {
      AssignEnabled(!GetEnabled());
      Sync();
      Options.NotifyEdit();
    });
    const Sync = () => {
      Switch.classList.toggle("checked", !!GetEnabled());
      Switch.setAttribute("aria-checked", GetEnabled() ? "true" : "false");
    };
    Row.appendChild(Switch);
    Sync();
    if (Options.Refreshable !== false) RefreshQueue.push(Sync);
    return Row;
  }

  function BuildColourRow(Options) {
    const { Label, GetColour, AssignColour } = Options;
    const Row = El("label", "field compact colour-row");
    Row.appendChild(El("span", null, Label));
    const Swatch = El("input", "colour-swatch");
    Swatch.type = "color";
    Swatch.setAttribute("aria-label", Label);
    Swatch.addEventListener("input", () => {
      AssignColour(Swatch.value);
      Options.NotifyEdit();
    });
    const Sync = () => { Swatch.value = GetColour(); };
    Row.appendChild(Swatch);
    Sync();
    if (Options.Refreshable !== false) RefreshQueue.push(Sync);
    return Row;
  }

  function BuildTextRow(Options) {
    const { Label, GetText, AssignText } = Options;
    const Row = El("label", "field compact text-row");
    Row.appendChild(El("span", null, Label));
    const Input = El("input", "text-field");
    Input.type = "text";
    Input.setAttribute("aria-label", Label);
    Input.addEventListener("change", () => {
      AssignText(Input.value);
      Options.NotifyEdit();
    });
    const Sync = () => { Input.value = GetText(); };
    Row.appendChild(Input);
    Sync();
    if (Options.Refreshable !== false) RefreshQueue.push(Sync);
    return Row;
  }

  function BuildVector3Row(Options) {
    const { Label, AxisLabels, GetVec, AssignVec, Step, NotifyEdit } = Options;
    const Row = El("div", "field compact vector-row");
    Row.appendChild(El("span", null, Label));
    const Trio = El("div", "vector-trio");
    const Inputs = [0, 1, 2].map((Axis) => {
      const Cell = El("label", "axis-cell");
      Cell.appendChild(El("small", null, AxisLabels[Axis]));
      const Input = El("input");
      Input.type = "number";
      Input.step = Step === undefined ? 0.05 : Step;
      Input.setAttribute("aria-label", Label + " " + AxisLabels[Axis]);
      Input.addEventListener("change", () => {
        const N = Number(Input.value);
        if (Number.isFinite(N)) {
          const Next = GetVec().slice();
          Next[Axis] = N;
          AssignVec(Next);
          Sync();
          NotifyEdit();
        }
      });
      Cell.appendChild(Input);
      Trio.appendChild(Cell);
      return Input;
    });
    const Sync = () => {
      const Vec = GetVec();
      for (let Axis = 0; Axis < 3; ++Axis) Inputs[Axis].value = Number(Vec[Axis]).toFixed(2);
    };
    Row.appendChild(Trio);
    Sync();
    RefreshQueue.push(Sync);
    return Row;
  }

  function SectionTitle(Text) {
    return El("div", "section-title", Text);
  }

  function FlushRefreshQueue() {
    for (const Sync of RefreshQueue) Sync();
  }

  //----------------------------------------------------------------------------------------------------------------------
  //                                              SELECTED GENERATOR TAB
  //----------------------------------------------------------------------------------------------------------------------

  function BuildGeneratorTab(HostEl, Status, NotifyEdit) {
    HostEl.textContent = "";
    const Gen = Status.Generators.find((G) => G.Id === Status.SelectedId);
    if (!Gen) {
      const Empty = El("div", "inspector-empty");
      Empty.appendChild(El("p", null, "No force generator selected."));
      Empty.appendChild(El("p", "muted", "Construct one from the left dock registry, then edit its law parameters here."));
      HostEl.appendChild(Empty);
      return;
    }
    const F = Gen.Force;
    const Meta = GetForceLawMeta(F.Model);

    HostEl.appendChild(SectionTitle("Identity"));
    HostEl.appendChild(BuildTextRow({
      Label: "Name", GetText: () => Gen.Name, AssignText: (Text) => { Gen.Name = String(Text).slice(0, 48) || Meta.Name; }, NotifyEdit,
    }));
    HostEl.appendChild(BuildDropdownRow({
      Label: "Force Law",
      Entries: ForceLawRepository.map((Law) => ({ Id: Law.Id, Label: Law.Ordinal + " \u00B7 " + Law.Name })),
      GetCurrent: () => F.Model,
      AssignOption: (LawId) => {
        const Fresh = ResolveForceValues({ Model: LawId, Enabled: F.Enabled, Muted: F.Muted, Tint: F.Tint });
        Gen.Force = Fresh;
      },
      NotifyEdit,
    }));
    HostEl.appendChild(BuildToggleRow({ Label: "Enabled", GetEnabled: () => F.Enabled, AssignEnabled: (V) => { F.Enabled = V; }, NotifyEdit }));
    HostEl.appendChild(BuildToggleRow({ Label: "Muted", GetEnabled: () => F.Muted, AssignEnabled: (V) => { F.Muted = V; }, NotifyEdit }));
    HostEl.appendChild(BuildColourRow({ Label: "Tint", GetColour: () => F.Tint || Meta.Tint, AssignColour: (Hex) => { F.Tint = Hex; }, NotifyEdit }));

    const LawNote = El("div", "law-note");
    LawNote.appendChild(BuildLawIcon(F.Model, F.Tint || Meta.Tint));
    const NoteText = El("div", "law-note-text");
    NoteText.appendChild(El("code", "law-equation", Meta.Equation));
    NoteText.appendChild(El("p", "muted", Meta.Summary));
    NoteText.appendChild(El("p", "muted", Meta.Channel + " \u00B7 " + Meta.Unit));
    LawNote.appendChild(NoteText);
    HostEl.appendChild(LawNote);

    HostEl.appendChild(SectionTitle("Placement"));
    HostEl.appendChild(BuildVector3Row({
      Label: "Centre (m)", AxisLabels: ["X", "Y", "Z"],
      GetVec: () => Gen.Position, AssignVec: (Next) => { Gen.Position = Next; }, NotifyEdit,
    }));
    HostEl.appendChild(BuildVector3Row({
      Label: "Rotation (\u00B0)", AxisLabels: ["R", "P", "Yw"],
      GetVec: () => Gen.Rotation, AssignVec: (Next) => { Gen.Rotation = Next; }, NotifyEdit, Step: 1,
    }));

    HostEl.appendChild(SectionTitle("Law Parameters"));
    HostEl.appendChild(BuildScalarRow({ Label: "Strength", Min: -80, Max: 80, Step: 0.25, Unit: Meta.Unit, GetScalar: () => F.Strength, AssignScalar: (V) => { F.Strength = V; }, NotifyEdit }));
    HostEl.appendChild(BuildScalarRow({ Label: "Reach Radius", Min: 0.35, Max: 12, Step: 0.05, Unit: "m", GetScalar: () => F.Radius, AssignScalar: (V) => { F.Radius = V; }, NotifyEdit }));
    HostEl.appendChild(BuildScalarRow({ Label: "Inner Radius", Min: 0.08, Max: 8, Step: 0.05, Unit: "m", GetScalar: () => F.CoreRadius, AssignScalar: (V) => { F.CoreRadius = V; }, NotifyEdit }));
    HostEl.appendChild(BuildScalarRow({ Label: "Swirl Ratio", Min: -3, Max: 3, Step: 0.05, GetScalar: () => F.SwirlRatio, AssignScalar: (V) => { F.SwirlRatio = V; }, NotifyEdit }));
    HostEl.appendChild(BuildScalarRow({ Label: "Falloff Exponent", Min: 0.2, Max: 6, Step: 0.05, GetScalar: () => F.FalloffExponent, AssignScalar: (V) => { F.FalloffExponent = V; }, NotifyEdit }));
    if (UsesNoise(F.Model)) {
      HostEl.appendChild(BuildScalarRow({ Label: "Turbulence Scale", Min: 0.15, Max: 6, Step: 0.05, Unit: "m", GetScalar: () => F.TurbulenceScale, AssignScalar: (V) => { F.TurbulenceScale = V; }, NotifyEdit }));
      HostEl.appendChild(BuildScalarRow({ Label: "Noise Octaves", Min: 1, Max: 5, Step: 1, Decimals: 0, GetScalar: () => F.Octaves, AssignScalar: (V) => { F.Octaves = Math.round(V); }, NotifyEdit }));
    }
    HostEl.appendChild(BuildScalarRow({ Label: "Pulse Frequency", Min: 0.02, Max: 5, Step: 0.01, Unit: "Hz", GetScalar: () => F.Frequency, AssignScalar: (V) => { F.Frequency = V; }, NotifyEdit }));
    HostEl.appendChild(BuildScalarRow({ Label: "Azimuth", Min: -360, Max: 360, Step: 1, Unit: "\u00B0", Decimals: 0, GetScalar: () => F.YawDeg, AssignScalar: (V) => { F.YawDeg = V; }, NotifyEdit }));
    HostEl.appendChild(BuildScalarRow({ Label: "Elevation", Min: -90, Max: 90, Step: 1, Unit: "\u00B0", Decimals: 0, GetScalar: () => F.PitchDeg, AssignScalar: (V) => { F.PitchDeg = V; }, NotifyEdit }));

    HostEl.appendChild(SectionTitle("Envelope & Timing"));
    HostEl.appendChild(BuildSegmentedRow({ Label: "Reach", Choices: ReachEnvelopes.slice(), GetCurrent: () => F.Reach, AssignOption: (V) => { F.Reach = V; }, NotifyEdit }));
    HostEl.appendChild(BuildSegmentedRow({ Label: "Attenuation", Choices: AttenuationCurves.slice(), GetCurrent: () => F.Attenuation, AssignOption: (V) => { F.Attenuation = V; }, NotifyEdit }));
    HostEl.appendChild(BuildSegmentedRow({ Label: "Duration", Choices: DurationModes.slice(), GetCurrent: () => F.Duration, AssignOption: (V) => { F.Duration = V; }, NotifyEdit }));

    HostEl.appendChild(SectionTitle("Motion Path"));
    HostEl.appendChild(BuildSegmentedRow({ Label: "Pattern", Choices: MotionPatterns.slice(), GetCurrent: () => F.MotionPattern, AssignOption: (V) => {
      F.MotionPattern = V;
      if ((V === "SplineCurve" || V === "LinearAB") && (!Array.isArray(F.MotionWaypoints) || F.MotionWaypoints.length < 2)) {
        F.MotionWaypoints = EnsureMotionWaypoints(Gen.Position, F, V);
      }
    }, NotifyEdit }));
    HostEl.appendChild(BuildSegmentedRow({ Label: "Loop", Choices: MotionLoopModes.slice(), GetCurrent: () => F.MotionLoopMode, AssignOption: (V) => { F.MotionLoopMode = V; }, NotifyEdit }));
    HostEl.appendChild(BuildScalarRow({ Label: "Path Speed", Min: 0.02, Max: 4, Step: 0.01, Unit: "Hz", GetScalar: () => F.MotionSpeedHz, AssignScalar: (V) => { F.MotionSpeedHz = V; }, NotifyEdit }));
    HostEl.appendChild(BuildScalarRow({ Label: "Amplitude", Min: 0, Max: 6, Step: 0.05, Unit: "m", GetScalar: () => F.MotionAmplitude, AssignScalar: (V) => { F.MotionAmplitude = V; }, NotifyEdit }));
    HostEl.appendChild(BuildScalarRow({ Label: "Phase", Min: 0, Max: 1, Step: 0.01, GetScalar: () => F.MotionPhase, AssignScalar: (V) => { F.MotionPhase = V; }, NotifyEdit }));
    if (F.MotionPattern === "SplineCurve" || F.MotionPattern === "LinearAB") {
      const ShapeBtn = El("button", "construct-button slim", "Re-shape Waypoints");
      ShapeBtn.type = "button";
      ShapeBtn.addEventListener("click", () => {
        F.MotionWaypoints = EnsureMotionWaypoints(Gen.Position, F, F.MotionPattern);
        NotifyEdit();
      });
      HostEl.appendChild(ShapeBtn);
    }

    HostEl.appendChild(SectionTitle("Actions"));
    const Actions = El("div", "inspector-actions");
    const DuplicateBtn = El("button", "construct-button slim", "Duplicate");
    DuplicateBtn.type = "button";
    DuplicateBtn.addEventListener("click", () => { Status.RequestDuplicate(Gen.Id); });
    const RemoveBtn = El("button", "danger-button slim", "Remove");
    RemoveBtn.type = "button";
    RemoveBtn.addEventListener("click", () => { Status.RequestRemove(Gen.Id); });
    Actions.appendChild(DuplicateBtn);
    Actions.appendChild(RemoveBtn);
    HostEl.appendChild(Actions);
  }

  function UsesNoise(ModelId) {
    return ModelId === "CurlNoise" || ModelId === "AtmosphericGust" || ModelId === "VorticityConfinement" || ModelId === "BrownianNoise";
  }

  function BuildLawIcon(LawId, Tint) {
    const Meta = GetForceLawMeta(LawId);
    const Markup = ForceLawIconPaths[Meta.Id] || '<circle cx="8" cy="8" r="4"/>';
    const Wrap = document.createElement("span");
    Wrap.className = "law-icon";
    Wrap.innerHTML =
      '<svg width="18" height="18" viewBox="0 0 16 16" fill="none" stroke="' + (Tint || Meta.Tint) + '" ' +
      'stroke-width="1.45" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' + Markup + "</svg>";
    return Wrap;
  }

  //----------------------------------------------------------------------------------------------------------------------
  //                                                 OVERLAY TAB
  //----------------------------------------------------------------------------------------------------------------------

  function BuildOverlayTab(HostEl, Status, NotifyEdit) {
    HostEl.textContent = "";
    const O = Status.Overlay;

    HostEl.appendChild(SectionTitle("Slice & View"));
    HostEl.appendChild(BuildSegmentedRow({ Label: "Plane", Choices: SlicePlanes.slice(), GetCurrent: () => Status.View.Mode, AssignOption: (V) => { Status.AssignViewMode(V); }, NotifyEdit }));
    HostEl.appendChild(BuildScalarRow({ Label: "Slice Depth", Min: -3.4, Max: 3.4, Step: 0.05, Unit: "m", GetScalar: () => O.SliceDepth, AssignScalar: (V) => { O.SliceDepth = V; Status.NotifyViewChanged(); }, NotifyEdit }));
    HostEl.appendChild(BuildToggleRow({ Label: "Grid", GetEnabled: () => O.GridEnabled, AssignEnabled: (V) => { O.GridEnabled = V; }, NotifyEdit }));

    HostEl.appendChild(SectionTitle("Heatmap Slice"));
    HostEl.appendChild(BuildToggleRow({ Label: "Show Heatmap", GetEnabled: () => O.HeatmapEnabled, AssignEnabled: (V) => { O.HeatmapEnabled = V; }, NotifyEdit }));
    HostEl.appendChild(BuildDropdownRow({
      Label: "Quantity",
      Entries: MeterQuantities.map((Q) => ({ Id: Q, Label: Q })),
      GetCurrent: () => O.Quantity, AssignOption: (V) => { O.Quantity = V; }, NotifyEdit,
    }));
    HostEl.appendChild(BuildDropdownRow({
      Label: "Colour Ramp",
      Entries: ColourRampModes.map((R) => ({ Id: R, Label: R })),
      GetCurrent: () => O.Ramp, AssignOption: (V) => { O.Ramp = V; }, NotifyEdit,
    }));
    HostEl.appendChild(BuildScalarRow({ Label: "Fixed Maximum", Min: 1, Max: 60, Step: 0.5, Unit: O.Quantity === "Speed" ? "m/s" : "\u2014", GetScalar: () => O.FixedMax, AssignScalar: (V) => { O.FixedMax = V; }, NotifyEdit }));
    HostEl.appendChild(BuildScalarRow({ Label: "Slice Opacity", Min: 0.05, Max: 1, Step: 0.01, GetScalar: () => O.HeatmapOpacity, AssignScalar: (V) => { O.HeatmapOpacity = V; }, NotifyEdit }));
    HostEl.appendChild(BuildSegmentedRow({
      Label: "Slice Density",
      Choices: ["Low", "Medium", "High"],
      GetCurrent: () => O.SliceDensityChoice,
      AssignOption: (V) => { O.SliceDensityChoice = V; Status.ApplyQualityPreset(V); }, NotifyEdit,
    }));

    HostEl.appendChild(SectionTitle("Lattice Arrows"));
    HostEl.appendChild(BuildToggleRow({ Label: "Show Arrows", GetEnabled: () => O.ArrowsEnabled, AssignEnabled: (V) => { O.ArrowsEnabled = V; }, NotifyEdit }));
    HostEl.appendChild(BuildScalarRow({ Label: "Lattice Spacing", Min: 0.15, Max: 1.6, Step: 0.05, Unit: "m", GetScalar: () => O.ArrowSpacing, AssignScalar: (V) => { O.ArrowSpacing = V; }, NotifyEdit }));
    HostEl.appendChild(BuildScalarRow({ Label: "Vector Scale", Min: 0.2, Max: 3, Step: 0.05, GetScalar: () => O.VectorScale, AssignScalar: (V) => { O.VectorScale = V; }, NotifyEdit }));

    HostEl.appendChild(SectionTitle("Streamlines"));
    HostEl.appendChild(BuildToggleRow({ Label: "Show Streamlines", GetEnabled: () => O.StreamlinesEnabled, AssignEnabled: (V) => { O.StreamlinesEnabled = V; }, NotifyEdit }));
    HostEl.appendChild(BuildScalarRow({ Label: "Seed Count", Min: 9, Max: 196, Step: 1, Decimals: 0, GetScalar: () => O.StreamlineCount, AssignScalar: (V) => { O.StreamlineCount = Math.round(V); Status.NotifyViewChanged(); }, NotifyEdit }));
    HostEl.appendChild(BuildScalarRow({ Label: "Thickness", Min: 0.6, Max: 4, Step: 0.05, Unit: "px", GetScalar: () => O.StreamlineThickness, AssignScalar: (V) => { O.StreamlineThickness = V; }, NotifyEdit }));
    HostEl.appendChild(BuildScalarRow({ Label: "Drift Speed", Min: 0.1, Max: 5, Step: 0.05, GetScalar: () => O.StreamDrift, AssignScalar: (V) => { O.StreamDrift = V; }, NotifyEdit }));
    HostEl.appendChild(BuildSegmentedRow({ Label: "Colouring", Choices: ["Ramp", "Single"], GetCurrent: () => O.StreamColourMode, AssignOption: (V) => { O.StreamColourMode = V; }, NotifyEdit }));
    HostEl.appendChild(BuildColourRow({ Label: "Single Colour", GetColour: () => O.StreamColour, AssignColour: (Hex) => { O.StreamColour = Hex; }, NotifyEdit }));

    HostEl.appendChild(SectionTitle("Advected Particles"));
    HostEl.appendChild(BuildToggleRow({ Label: "Show Particles", GetEnabled: () => O.ParticlesEnabled, AssignEnabled: (V) => { O.ParticlesEnabled = V; }, NotifyEdit }));
    HostEl.appendChild(BuildScalarRow({ Label: "Particle Count", Min: 200, Max: 9000, Step: 100, Decimals: 0, GetScalar: () => O.ParticleCount, AssignScalar: (V) => { O.ParticleCount = Math.round(V); }, NotifyEdit }));
    HostEl.appendChild(BuildScalarRow({ Label: "Particle Size", Min: 0.8, Max: 5, Step: 0.1, Unit: "px", GetScalar: () => O.ParticleSize, AssignScalar: (V) => { O.ParticleSize = V; }, NotifyEdit }));
    HostEl.appendChild(BuildScalarRow({ Label: "Trail Length", Min: 2, Max: 7, Step: 1, Decimals: 0, GetScalar: () => O.TrailLength, AssignScalar: (V) => { O.TrailLength = Math.round(V); }, NotifyEdit }));
    HostEl.appendChild(BuildDropdownRow({
      Label: "Particle Ramp",
      Entries: ColourRampModes.map((R) => ({ Id: R, Label: R })),
      GetCurrent: () => O.ParticleRamp, AssignOption: (V) => { O.ParticleRamp = V; }, NotifyEdit,
    }));

    HostEl.appendChild(SectionTitle("Indicators"));
    HostEl.appendChild(BuildToggleRow({ Label: "Envelope Shells", GetEnabled: () => O.EnvelopesEnabled, AssignEnabled: (V) => { O.EnvelopesEnabled = V; }, NotifyEdit }));
    HostEl.appendChild(BuildToggleRow({ Label: "Field Meter", GetEnabled: () => O.MeterEnabled, AssignEnabled: (V) => { O.MeterEnabled = V; Status.NotifyMeterVisibility(); }, NotifyEdit }));
  }

  return {
    BuildGeneratorTab,
    BuildOverlayTab,
    BuildLawIcon,
    FlushRefreshQueue,
    ControlFactories: { BuildScalarRow, BuildSegmentedRow, BuildDropdownRow, BuildToggleRow, BuildColourRow, BuildTextRow, BuildVector3Row, El, SectionTitle },
  };
})();
