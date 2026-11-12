import { DocumentSequence } from "./DocumentSequence.js";
import { FlipbookSequence } from "./FlipbookSequence.js";
import { ProjectBounds } from "./BoundsProjection.js";
import { OrbitCamera } from "./camera.js";
import { WebGL2PyroEngine } from "./engine-webgl2.js";
import { WebGPUPyroEngine } from "./engine-webgpu.js";
import { PRESETS, DEBUG_CHANNELS, OBSTACLE_TYPES } from "./presets.js";
import {
  InitialParameters,
  ControlSpecification,
  ControlLabels,
  PresetPresentation,
  SourceRevision,
  ValidateParameter,
  ValidateScene,
  ConstructPresetParameters,
  ComputeColliderPosition,
} from "./SceneSpecification.js";
import { ConstructSampleScene, SampleSummary } from "./SampleScenes.js";
import { WriteSceneToml, SceneExtension } from "./SceneTomlCodec.js";

const Select = (Selector) => document.querySelector(Selector);
const SelectAll = (Selector) => [...document.querySelectorAll(Selector)];
const Escape = (Text) =>
  String(Text).replace(
    /[&<>"']/g,
    (Character) =>
      ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[
        Character
      ],
  );
const GlyphPaths = {
  flame:
    '<path d="M13 2c2 6-5 6-2 11-3-1-3-4-3-4C0 18 10 25 17 20c5-4 2-11-4-18Z"/>',
  smoke:
    '<path d="M6 20h12M5 15c-4-5 2-7 4-5-3-8 8-9 7-3 7-1 7 8 2 8H5Zm4 1v4m6-4v4"/>',
  help: '<circle cx="12" cy="12" r="9"/><path d="M9.5 9a2.5 2.5 0 1 1 4 2c-1.5 1-1.5 1.5-1.5 3m0 3h.01"/>',
  download: '<path d="M12 3v12m-5-5 5 5 5-5M4 16v5h16v-5"/>',
  file: '<path d="M14 2H5v20h14V7Zm0 0v6h5M8 13h8m-8 4h6"/>',
  folder: '<path d="M3 6h7l2 3h9l-2 11H3V6Zm0 3h18"/>',
  plus: '<path d="M12 5v14M5 12h14"/>',
  layers: '<path d="m12 3 10 5-10 5L2 8Zm-9 10 9 5 9-5M3 17l9 5 9-5"/>',
  box: '<path d="m12 2 9 5v10l-9 5-9-5V7Zm0 10L3 7m9 5 9-5m-9 5v10M7 4.5 17 10"/>',
  link: '<path d="m10 14 4-4m-6 2-2 2a3.5 3.5 0 0 0 5 5l2-2m-2-10 2-2a3.5 3.5 0 0 1 5 5l-2 2"/>',
  grid: '<rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/>',
  search: '<circle cx="10" cy="10" r="6"/><path d="m15 15 6 6"/>',
  viewport:
    '<rect x="3" y="4" width="18" height="14" rx="2"/><path d="M8 22h8m-4-4v4"/>',
  focus:
    '<path d="M3 8V3h5m8 0h5v5m0 8v5h-5M8 21H3v-5"/><circle cx="12" cy="12" r="3"/>',
  maximize:
    '<path d="M8 3H3v5m0-5 7 7m6 11h5v-5m0 5-7-7M16 3h5v5m0-5-7 7M8 21H3v-5m0 5 7-7"/>',
  orbit:
    '<circle cx="12" cy="12" r="4"/><ellipse cx="12" cy="12" rx="10" ry="6" transform="rotate(-35 12 12)"/>',
  brush: '<path d="m14 3 7 7-10 10H4v-7Zm-6 7 7 7M3 21h7"/>',
  burst: '<path d="m12 2 2 6 6-4-3 7 5 3-7 1-1 7-4-6-7 4 4-7-5-4 7 1Z"/>',
  rotate: '<path d="M4 10a8 8 0 1 1 1 7M4 3v7h7"/>',
  warning: '<path d="m12 3 10 18H2Zm0 6v5m0 3h.01"/>',
  rewind: '<path d="M4 5v14m14-14L7 12l11 7Z"/>',
  pause: '<path d="M8 5v14m8-14v14"/>',
  play: '<path d="m7 4 13 8-13 8Z"/>',
  step: '<path d="m4 5 11 7-11 7Zm15 0v14"/>',
  clock: '<circle cx="12" cy="12" r="9"/><path d="M12 6v6l4 2"/>',
  close: '<path d="m6 6 12 12M6 18 18 6"/>',
  sphere:
    '<circle cx="12" cy="12" r="9"/><ellipse cx="12" cy="12" rx="4" ry="9"/><path d="M3 12h18"/>',
  wall: '<path d="M3 6h18v14H3Zm0 7h18M9 6v7m6 0v7"/>',
  cylinder:
    '<ellipse cx="12" cy="5" rx="8" ry="3"/><path d="M4 5v14c0 4 16 4 16 0V5"/>',
  sun: '<circle cx="12" cy="12" r="4"/><path d="M12 1v3m0 16v3M1 12h3m16 0h3M4 4l2 2m12 12 2 2M4 20l2-2M18 6l2-2"/>',
  eye: '<path d="M2 12c5-9 15-9 20 0-5 9-15 9-20 0Z"/><circle cx="12" cy="12" r="3"/>',
  hidden: '<path d="m3 3 18 18M9 5c5-2 10 1 13 7l-4 5M6 6l-4 6c3 6 8 9 14 6"/>',
};
const Icon = (Name) =>
  `<svg viewBox="0 0 24 24" aria-hidden="true">${GlyphPaths[Name] || GlyphPaths.box}</svg>`;
const FillIcons = () =>
  SelectAll("[data-icon]").forEach((Element) => {
    Element.innerHTML = Icon(Element.dataset.icon);
  });
const FormatNumber = (Value, Step = 0.01) =>
  Number(Value).toFixed(Number.isInteger(Step) ? 0 : Step < 0.01 ? 3 : 2);

class FluidPanel {
  constructor() {
    this.Parameters = { ...InitialParameters };
    if (new URLSearchParams(location.search).get("quality") === "low")
      Object.assign(this.Parameters, {
        gridResolution: 24,
        raymarchSteps: 40,
        pressureIterations: 10,
        renderScale: 0.5,
        shadowSteps: 3,
        emberCount: 100,
      });
    this.Camera = new OrbitCamera();
    this.Camera.targetDistance = 4.1;
    this.Camera.targetCenter = [0, 0.3, 0];
    this.Engine = null;
    this.Canvas = Select("#pyro-canvas");
    this.Names = {
      domain: "Gas domain",
      emitter: "Fire emitter",
      collider: "Sphere collider",
      sun: "Directional light",
    };
    this.Selection = "emitter";
    this.Tab = "source";
    this.Preset = "";
    this.Filter = "all";
    this.SceneFilter = "all";
    this.Dirty = false;
    this.LastTime = performance.now();
    this.Frames = 0;
    this.Elapsed = 0;
    this.FramesPerSecond = 0;
    this.SingleStep = false;
    this.Switching = false;
    this.Bursts = [];
    this.SavedCollider = 1;
    this.SavedSun = this.Parameters.sunIntensity;
    this.VisibleKeys = [];
    FillIcons();
    this.Documents = new DocumentSequence(this, Icon);
    this.Flipbook = new FlipbookSequence(this);
    this.ConnectInterface();
    this.ConstructPresetCards();
    this.ConstructSceneRows();
    this.ConstructInspector();
    this.RefreshControls();
    new ResizeObserver(() => this.ResizeViewport()).observe(
      Select("#viewport"),
    );
    this.InitializeRenderer("webgl2");
    requestAnimationFrame((Time) => this.Frame(Time));
  }

  InitializeDocumentState() {
    this.Parameters = { ...InitialParameters };
    if (new URLSearchParams(location.search).get("quality") === "low")
      Object.assign(this.Parameters, {
        gridResolution: 24,
        raymarchSteps: 40,
        pressureIterations: 10,
        renderScale: 0.5,
        shadowSteps: 3,
        emberCount: 100,
      });
    this.Camera = new OrbitCamera();
    this.Camera.targetDistance = 4.1;
    this.Camera.targetCenter = [0, 0.3, 0];
    this.Names = {
      domain: "Gas domain",
      emitter: "Fire emitter",
      collider: "Sphere collider",
      sun: "Directional light",
    };
    this.Selection = "emitter";
    this.Tab = "source";
    this.Preset = "";
    this.Dirty = false;
    this.Bursts = [];
    this.SavedCollider = 1;
    this.SavedSun = this.Parameters.sunIntensity;
  }

  RenderBounds() {
    const Overlay = Select("#domain-bounds");
    const Visible = this.Parameters.showBoundingBox && !!this.Engine;
    Overlay.toggleAttribute("hidden", !Visible);
    if (!Visible) return;
    const Rectangle = Select("#viewport").getBoundingClientRect();
    Overlay.setAttribute(
      "viewBox",
      `0 0 ${Rectangle.width} ${Rectangle.height}`,
    );
    const Lines = ProjectBounds(
      this.Camera,
      this.Engine.getBoundsBox(),
      Rectangle.width,
      Rectangle.height,
    );
    Overlay.innerHTML = Lines.map(
      (Line) =>
        `<line x1="${Line[0].toFixed(2)}" y1="${Line[1].toFixed(2)}" x2="${Line[2].toFixed(2)}" y2="${Line[3].toFixed(2)}"/>`,
    ).join("");
  }

  async InitializeRenderer(Backend) {
    if (this.Switching) return;
    this.Switching = true;
    this.Bursts = [];
    Select("#backend-select").disabled = true;
    Select("#gpu-error").hidden = true;
    Select("#status-ready").textContent = "Initializing GPU";
    this.Engine?.destroy();
    this.Engine = null;
    const PreviousCanvas = this.Canvas;
    this.Canvas = PreviousCanvas.cloneNode(false);
    PreviousCanvas.replaceWith(this.Canvas);
    this.ConnectViewport();
    try {
      if (Backend === "webgpu") {
        try {
          this.Engine = await WebGPUPyroEngine.create(
            this.Canvas,
            this.Parameters,
          );
        } catch (ErrorValue) {
          this.Notify(
            `WebGPU unavailable. Using WebGL2. ${ErrorValue.message}`,
          );
          const FailedCanvas = this.Canvas;
          this.Canvas = FailedCanvas.cloneNode(false);
          FailedCanvas.replaceWith(this.Canvas);
          this.ConnectViewport();
          Backend = "webgl2";
        }
      }
      if (Backend === "webgl2")
        this.Engine = new WebGL2PyroEngine(this.Canvas, this.Parameters);
      if (this.Engine.gl && !this.Engine.extColorBufferFloat)
        throw new Error(
          "This device needs EXT_color_buffer_float to run the voxel solver.",
        );
      this.Engine.setGridResolution(this.Parameters.gridResolution);
      this.Backend = Backend;
      Select("#backend-select").value = Backend;
      Select("#viewport-subtitle").textContent =
        Backend === "webgpu"
          ? "Experimental gas path · WebGL2 provides full effects"
          : "Eulerian volume · physically shaded";
      this.ResizeViewport();
      Select("#status-ready").innerHTML = "<i></i>GPU ready";
      if (Backend === "webgpu") {
        this.Notify(
          "Experimental WebGPU: the upstream renderer has fewer visual features than WebGL2.",
        );
        const CurrentEngine = this.Engine;
        CurrentEngine.device.lost.then((Information) => {
          const Message = Information.message || "WebGPU device lost.";
          if (this.Engine === CurrentEngine) this.ShowGpuError(Message);
          else {
            const Document = this.Documents?.Slots.find(
              (Slot) => Slot.State?.Engine === CurrentEngine,
            );
            if (Document) Document.State.Fault = Message;
          }
        });
      }
    } catch (ErrorValue) {
      this.Engine?.destroy();
      this.Engine = null;
      this.ShowGpuError(ErrorValue.message);
    } finally {
      this.Switching = false;
      Select("#backend-select").disabled = false;
      this.RefreshControls();
    }
  }

  ShowGpuError(Message) {
    Select("#gpu-error-message").textContent = Message;
    Select("#gpu-error").hidden = false;
    Select("#status-ready").textContent = "GPU unavailable";
    this.Parameters.paused = true;
    this.RefreshControls();
  }

  ResizeViewport() {
    const Rectangle = Select("#viewport").getBoundingClientRect();
    const Scale =
      Math.min(devicePixelRatio || 1, 1.5) * this.Parameters.renderScale;
    const Width = Math.max(2, Math.floor(Rectangle.width * Scale));
    const Height = Math.max(2, Math.floor(Rectangle.height * Scale));
    if (this.Canvas.width !== Width || this.Canvas.height !== Height) {
      this.Canvas.width = Width;
      this.Canvas.height = Height;
    }
    this.Camera.aspect = Width / Height;
  }

  MarkDirty() {
    this.Dirty = true;
    Select("#dirty-indicator").classList.remove("clean");
    this.Documents?.RefreshLabels();
  }
  Notify(Message) {
    clearTimeout(this.ToastTimeout);
    Select("#toast").textContent = Message;
    Select("#toast").hidden = false;
    this.ToastTimeout = setTimeout(() => {
      Select("#toast").hidden = true;
    }, 4500);
  }

  ApplyParameter(Key, Value) {
    try {
      Value = ValidateParameter(Key, Value);
    } catch (ErrorValue) {
      this.Notify(ErrorValue.message);
      this.RefreshControls();
      return;
    }
    if (this.Parameters[Key] === Value) return;
    const Previous = this.Parameters[Key];
    this.Parameters[Key] = Value;
    try {
      if (Key === "gridResolution") {
        this.Engine?.setGridResolution(Value);
        this.Restart();
        this.Notify(`Grid rebuilt at ${Value}³. Simulation restarted.`);
      }
      if (Key === "renderScale") this.ResizeViewport();
      if (Key === "interactionMode") this.Engine?.setBrush(null, null, false);
    } catch (ErrorValue) {
      this.Parameters[Key] = Previous;
      this.ShowGpuError(ErrorValue.message);
    }
    this.MarkDirty();
    if (["emitterEnabled", "obstacleType", "sunIntensity"].includes(Key))
      this.ConstructSceneRows();
    this.RefreshControls();
  }

  RefreshControls() {
    SelectAll("[data-param]").forEach((Element) => {
      const Value = this.Parameters[Element.dataset.param];
      if (Element.type === "checkbox") Element.checked = Value;
      else if (Element.type === "number")
        Element.value = FormatNumber(
          Value,
          ControlSpecification[Element.dataset.param]?.step,
        );
      else Element.value = Value;
      if (Element.type === "range") {
        const Fraction = Math.max(
          0,
          Math.min(
            1,
            (Value - Number(Element.min)) /
              (Number(Element.max) - Number(Element.min)),
          ),
        );
        Element.style.setProperty("--fraction", Fraction);
      }
    });
    Select("#render-channel").value = this.Parameters.renderChannel;
    Select("#diagnostic-channel").value = this.Parameters.renderChannel;
    Select("#atlas-toggle").checked = this.Parameters.showAtlasMinimap;
    Select("#overlays-button").classList.toggle(
      "active",
      this.Parameters.showBoundingBox,
    );
    Select("#turntable-button").classList.toggle(
      "active",
      this.Parameters.autoTurntable,
    );
    SelectAll("[data-tool]").forEach((Button) => {
      Button.classList.toggle(
        "active",
        Button.dataset.tool === this.Parameters.interactionMode,
      );
      Button.setAttribute(
        "aria-pressed",
        String(Button.dataset.tool === this.Parameters.interactionMode),
      );
    });
    Select("#play-button").innerHTML = Icon(
      this.Parameters.paused ? "play" : "pause",
    );
    Select("#play-button").setAttribute(
      "aria-label",
      this.Parameters.paused ? "Play simulation" : "Pause simulation",
    );
    Select("#live-pill span").textContent = this.Engine
      ? this.Parameters.paused
        ? "PAUSED"
        : "LIVE"
      : "OFFLINE";
    Select("#live-pill").classList.toggle("paused", this.Parameters.paused);
    const SpeedSelect = Select("#speed-select");
    SpeedSelect.querySelector("[data-custom]")?.remove();
    if (![0.25, 0.5, 1, 2].includes(this.Parameters.timeScale))
      SpeedSelect.insertAdjacentHTML(
        "beforeend",
        `<option data-custom value="${this.Parameters.timeScale}">${this.Parameters.timeScale.toFixed(2)}×</option>`,
      );
    SpeedSelect.value = this.Parameters.timeScale;
    Select("#grid-hud").textContent = `${this.Parameters.gridResolution}³ GRID`;
    Select("#voxel-count").textContent =
      `${(this.Parameters.gridResolution ** 3).toLocaleString()} voxels`;
    Select("#source-track-label").textContent = this.Parameters.emitterEnabled
      ? "Continuous emission"
      : "Burst-only simulation";
    const Enabled = Select("#object-enabled");
    Enabled.disabled = this.Selection === "domain";
    Enabled.checked =
      this.Selection === "emitter"
        ? this.Parameters.emitterEnabled
        : this.Selection === "collider"
          ? this.Parameters.obstacleType > 0
          : this.Selection === "sun"
            ? this.Parameters.sunIntensity > 0
            : true;
    Select("#dirty-indicator").classList.toggle("clean", !this.Dirty);
    this.Documents?.RefreshLabels();
  }

  ConstructSceneRows() {
    const Objects = [
      ["domain", "box", "VOLUME", true],
      [
        "emitter",
        this.Parameters.emitterFuel === 0 ? "smoke" : "flame",
        "",
        this.Parameters.emitterEnabled,
      ],
      ...(this.Parameters.obstacleType > 0 || this.Selection === "collider"
        ? [["collider", "sphere", "", this.Parameters.obstacleType > 0]]
        : []),
      ["sun", "sun", "", this.Parameters.sunIntensity > 0],
    ];
    Select("#scene-count").textContent = Objects.length;
    Select("#enabled-count").textContent = Objects.filter(
      (ObjectSlot) => ObjectSlot[3],
    ).length;
    Select("#disabled-count").textContent = Objects.filter(
      (ObjectSlot) => !ObjectSlot[3],
    ).length;
    const Categories = {
      domain: "gas",
      emitter: "gas",
      collider: "geometry",
      sun: "light",
    };
    const Search = Select("#scene-search").value.toLowerCase().trim();
    const Matches = Objects.filter(
      ([Key]) =>
        (this.SceneFilter === "all" || Categories[Key] === this.SceneFilter) &&
        this.Names[Key].toLowerCase().includes(Search),
    );
    Select("#scene-tree").hidden = Search
      ? false
      : Select("#collection-toggle").getAttribute("aria-expanded") === "false";
    const RowMarkup =
      Matches.map(
        ([Key, Glyph, Badge, Enabled]) =>
          `<div class="scene-row ${this.Selection === Key ? "selected" : ""} ${!Enabled ? "muted" : ""}" data-object="${Key}" data-category="${Categories[Key]}" role="treeitem" aria-selected="${this.Selection === Key}" aria-level="1" tabindex="0"><span class="row-chevron"></span><span class="row-icon">${Icon(Glyph)}</span><span class="scene-label">${Escape(this.Names[Key])}</span>${Badge ? `<span class="row-badge">${Badge}</span>` : `<button class="icon-button row-toggle" data-toggle-object="${Key}" title="${Enabled ? "Disable" : "Enable"} ${Escape(this.Names[Key])}" aria-label="${Enabled ? "Disable" : "Enable"} ${Escape(this.Names[Key])}">${Icon(Enabled ? "eye" : "hidden")}</button>`}</div>`,
      ).join("") ||
      '<div class="outliner-empty" role="status">No matching objects.</div>';
    const RowSignature = JSON.stringify(
      Matches.map((ObjectSlot) => [...ObjectSlot, this.Names[ObjectSlot[0]]]),
    );
    if (this.SceneRowSignature !== RowSignature) {
      Select("#scene-tree").innerHTML = RowMarkup;
      this.SceneRowSignature = RowSignature;
    } else {
      SelectAll("#scene-tree [data-object]").forEach((Row) => {
        const Selected = Row.dataset.object === this.Selection;
        Row.classList.toggle("selected", Selected);
        Row.setAttribute("aria-selected", String(Selected));
      });
    }
    SelectAll("[data-scene-filter]").forEach((Button) => {
      const Selected = Button.dataset.sceneFilter === this.SceneFilter;
      Button.classList.toggle("active", Selected);
      Button.setAttribute("aria-pressed", String(Selected));
    });
  }

  SelectObject(Key) {
    this.Selection = Key;
    this.Tab =
      Key === "domain" ? "simulation" : Key === "sun" ? "rendering" : "source";
    this.ConstructSceneRows();
    this.ConstructInspector();
    this.RefreshControls();
  }

  ToggleObject(Key) {
    if (Key === "emitter")
      this.ApplyParameter("emitterEnabled", !this.Parameters.emitterEnabled);
    if (Key === "collider") {
      if (this.Parameters.obstacleType) {
        this.SavedCollider = this.Parameters.obstacleType;
        this.ApplyParameter("obstacleType", 0);
      } else this.ApplyParameter("obstacleType", this.SavedCollider);
    }
    if (Key === "sun") {
      if (this.Parameters.sunIntensity) {
        this.SavedSun = this.Parameters.sunIntensity;
        this.ApplyParameter("sunIntensity", 0);
      } else this.ApplyParameter("sunIntensity", this.SavedSun || 2.2);
    }
  }

  ConstructPresetCards() {
    const Search = Select("#preset-search").value.toLowerCase().trim();
    const Keys = Object.keys(PresetPresentation).filter((Key) => {
      const Presentation = PresetPresentation[Key];
      return (
        (this.Filter === "all" || Presentation[1] === this.Filter) &&
        `${Presentation.join(" ")} ${PRESETS[Key].description}`
          .toLowerCase()
          .includes(Search)
      );
    });
    Select("#preset-count").textContent = Object.keys(PRESETS).length;
    Select("#presets-list").innerHTML = Keys.map((Key) => {
      const [Name, Category, Description, Glyph] = PresetPresentation[Key];
      return `<button class="preset-card ${this.Preset === Key ? "active" : ""}" data-preset="${Key}" data-category="${Category}" title="${Escape(PRESETS[Key].description)}" aria-label="Load ${Name}"><span class="preset-swatch">${Icon(Glyph)}</span><span class="preset-copy"><strong>${Name}</strong><small>${Description}</small></span><span class="preset-arrow">↗</span></button>`;
    }).join("");
    Select("#empty-presets").hidden = Keys.length > 0;
    SelectAll("[data-filter]").forEach((Button) =>
      Button.classList.toggle("active", Button.dataset.filter === this.Filter),
    );
  }

  LoadPreset(Key) {
    this.ApplySceneParameters(ConstructPresetParameters(Key));
    this.Preset = Key;
    this.Names.emitter =
      this.Parameters.emitterFuel === 0 ? "Smoke emitter" : "Fire emitter";
    this.Names.collider =
      OBSTACLE_TYPES.find(
        (Type) => Type.id === (this.Parameters.obstacleType || 1),
      )?.label + " collider";
    Select("#document-name").value = PresetPresentation[Key][0];
    this.Selection = this.Parameters.emitterEnabled ? "emitter" : "domain";
    this.Tab = this.Parameters.emitterEnabled ? "source" : "simulation";
    this.Camera.reset();
    this.Camera.targetDistance = Math.min(
      9.5,
      Math.max(this.Parameters.boundsWidth, this.Parameters.boundsHeight) * 2.3,
    );
    if (PRESETS[Key].triggerExplosionOnLoad) this.Engine?.triggerExplosion();
    this.ConstructPresetCards();
    this.ConstructSceneRows();
    this.ConstructInspector();
    this.RefreshControls();
    this.Notify(
      `${PresetPresentation[Key][0]} loaded · ${this.Parameters.gridResolution}³ grid`,
    );
  }

  ApplySceneParameters(Parameters) {
    const PreviousResolution = this.Parameters.gridResolution;
    Object.assign(this.Parameters, Parameters);
    try {
      if (PreviousResolution !== this.Parameters.gridResolution)
        this.Engine?.setGridResolution(this.Parameters.gridResolution);
      this.Restart();
      this.ResizeViewport();
    } catch (ErrorValue) {
      this.ShowGpuError(ErrorValue.message);
    }
    this.MarkDirty();
  }

  RenderControl(Key) {
    const Control = ControlSpecification[Key];
    const Label = Escape(ControlLabels[Key] || Control.label);
    const Value = this.Parameters[Key];
    this.VisibleKeys.push(Key);
    if (Control.type === "toggle")
      return `<div class="property-row toggle-row"><label class="property-label" for="property-${Key}">${Label}</label><label class="switch"><input id="property-${Key}" type="checkbox" data-param="${Key}" ${Value ? "checked" : ""}/><span></span></label></div>`;
    if (Control.type === "select")
      return `<div class="property-row select-row"><label class="property-label" for="property-${Key}">${Label}</label><select id="property-${Key}" data-param="${Key}">${Control.options.map((Option) => `<option value="${Option.id ?? Option.value}" ${(Option.id ?? Option.value) === Value ? "selected" : ""}>${Escape(Option.label)}</option>`).join("")}</select></div>`;
    const Unit =
      {
        boundsWidth: "m",
        boundsHeight: "m",
        emitterRadius: "m",
        emitterHeight: "m",
        obstacleRadius: "m",
        sunElevation: "°",
        sunAzimuth: "°",
        timeScale: "×",
        renderScale: "×",
        dynamicBoundsMax: "×",
        emitterRate: "×",
        temperatureScale: "×",
        emberLifetime: "×",
      }[Key] || "—";
    return `<div class="property-row slider-row"><label class="property-label" for="property-${Key}">${Label}</label><div class="slider-control"><div class="value-pill"><input id="property-${Key}" data-param="${Key}" type="number" value="${FormatNumber(Value, Control.step)}" min="${Control.min}" max="${Control.max}" step="${Control.step}"/><span class="unit-cell" aria-hidden="true">${Unit}</span></div><input type="range" aria-label="${Label} slider" data-param="${Key}" min="${Control.min}" max="${Control.max}" step="${Control.step}" value="${Value}"/></div></div>`;
  }

  ConstructInspector() {
    this.VisibleKeys = [];
    const Key = this.Selection;
    Select("#object-name").value = this.Names[Key];
    Select("#object-type").textContent = {
      domain: "EULERIAN VOLUME",
      emitter: "CONTINUOUS SOURCE",
      collider: "SIGNED DISTANCE COLLIDER",
      sun: "DIRECTIONAL LIGHT",
    }[Key];
    Select("#object-symbol").innerHTML = Icon(
      { domain: "box", emitter: "flame", collider: "sphere", sun: "sun" }[Key],
    );
    Select("#viewport-object").textContent = this.Names[Key];
    SelectAll("[data-tab]").forEach((Button) =>
      Button.classList.toggle("active", this.Tab === Button.dataset.tab),
    );
    const Groups = [];
    const AddGroup = (Title, Keys, Badge = "", Open = true, Hint = "") =>
      Groups.push(
        `<details class="property-group" ${Open ? "open" : ""}><summary>${Title}<span class="section-badge">${Badge}</span></summary><div class="group-content">${Hint ? `<p class="property-hint">${Hint}</p>` : ""}${Keys.map((Property) => this.RenderControl(Property)).join("")}</div></details>`,
      );
    if (this.Tab === "source") {
      if (Key === "collider")
        AddGroup(
          "Collider",
          [
            "obstacleType",
            "obstacleX",
            "obstacleY",
            "obstacleZ",
            "obstacleRadius",
            "colliderAutoMove",
            "colliderSpeed",
          ],
          "SDF",
          true,
          "Position is normalized within the domain. The collider changes the simulated flow without restarting it.",
        );
      else {
        AddGroup(
          "Emission",
          ["emitterEnabled", "emitterRate", "emitterRadius", "emitterHeight"],
          "SOURCE",
          true,
          "A continuous source at the domain’s X/Z centre. Height and radius are live simulation parameters.",
        );
        AddGroup(
          "Fuel & temperature",
          ["emitterTemperature", "emitterFuel", "emitterSmoke"],
          "INJECTION",
        );
        AddGroup("Velocity", ["emitterUpwardVelocity", "emitterSwirl"], "FLOW");
      }
      AddGroup(
        "Burst generator",
        [
          "shrapnelEnabled",
          "blastStrength",
          "blastRadius",
          "blastTemperature",
          "blastFuel",
          "blastSmoke",
          "blastLobes",
        ],
        "IMPULSE",
        false,
      );
      if (Key !== "collider")
        AddGroup(
          "Collider",
          [
            "obstacleType",
            "obstacleX",
            "obstacleY",
            "obstacleZ",
            "obstacleRadius",
            "colliderAutoMove",
            "colliderSpeed",
          ],
          "SDF",
          false,
        );
    }
    if (this.Tab === "simulation") {
      AddGroup(
        "Domain",
        ["boundsWidth", "boundsHeight", "dynamicBounds", "dynamicBoundsMax"],
        "3D",
      );
      AddGroup(
        "Voxel grid",
        [
          "gridResolution",
          "pressureIterations",
          "macCormackAdvection",
          "enclosedBox",
          "timeScale",
        ],
        "SOLVER",
        true,
        "Changing voxel resolution rebuilds the grid and restarts the simulation. Other properties update live.",
      );
      AddGroup(
        "Forces & turbulence",
        [
          "vorticityConfinement",
          "buoyancy",
          "smokeWeight",
          "turbulenceStrength",
          "turbulenceScale",
          "windX",
          "windZ",
          "velocityDamping",
        ],
        "FLOW",
        false,
      );
      AddGroup(
        "Combustion",
        [
          "burnRate",
          "burnHeat",
          "sootGeneration",
          "combustionExpansion",
          "coolingRate",
          "smokeDissipation",
        ],
        "THERMAL",
        false,
      );
    }
    if (this.Tab === "rendering") {
      if (Key === "sun")
        AddGroup(
          "Directional light",
          ["sunIntensity", "sunAzimuth", "sunElevation", "ambientIntensity"],
          "LIGHT",
        );
      AddGroup(
        "Volume shading",
        [
          "colorPalette",
          "fireIntensity",
          "densityExtinction",
          "smokeAlbedo",
          "exposure",
        ],
        "VOLUME",
      );
      if (Key !== "sun")
        AddGroup(
          "Directional light",
          ["sunIntensity", "sunAzimuth", "sunElevation", "ambientIntensity"],
          "LIGHT",
          false,
        );
      AddGroup(
        "Scattering & glow",
        [
          "bloomIntensity",
          "godRaysIntensity",
          "shockwaveStrength",
          "temperatureScale",
          "internalScattering",
          "shadowDensity",
          "phaseAnisotropy",
        ],
        "OPTICS",
        false,
      );
      AddGroup(
        "Render quality",
        [
          "raymarchSteps",
          "shadowSteps",
          "renderScale",
          "autoGpuGovernor",
          "voxelQuantization",
        ],
        "GPU",
        false,
      );
      AddGroup(
        "Embers & ash",
        [
          "showEmbers",
          "emberCount",
          "emberSize",
          "emberIntensity",
          "emberLifetime",
          "emberAshiness",
        ],
        "PARTICLES",
        false,
      );
      AddGroup(
        "Overlays & diagnostics",
        [
          "showBoundingBox",
          "showVoxelGridLines",
          "showFloorGrid",
          "showActiveVoxelCells",
          "showAtlasMinimap",
          "atlasMinimapField",
          "renderChannel",
          "sliceAxis",
          "slicePosition",
        ],
        "DEBUG",
        false,
      );
    }
    if (this.Tab === "source" && ["emitter", "collider"].includes(Key))
      Groups.push(
        `<button class="remove-object" id="remove-object">${Key === "emitter" ? "Disable source" : "Remove collider"}</button>`,
      );
    Select("#inspector-body").innerHTML = Groups.join("");
    Select("#inspector-body").scrollTop = 0;
    this.RefreshControls();
  }

  AddObject(Kind) {
    Select("#add-menu").hidden = true;
    Select("#add-button").setAttribute("aria-expanded", "false");
    if (["fire", "smoke"].includes(Kind)) {
      Object.assign(this.Parameters, {
        emitterEnabled: true,
        emitterFuel: Kind === "fire" ? 2.8 : 0,
        emitterTemperature: Kind === "fire" ? 3.5 : 0.8,
        emitterSmoke: Kind === "fire" ? 1.4 : 2.5,
      });
      this.Names.emitter = Kind === "fire" ? "Fire emitter" : "Smoke emitter";
      this.SelectObject("emitter");
    } else {
      this.Parameters.obstacleType = { sphere: 1, cylinder: 2, slab: 4 }[Kind];
      this.Parameters.obstacleY = 0.35;
      this.Parameters.colliderAutoMove = false;
      this.Names.collider = {
        sphere: "Sphere collider",
        cylinder: "Cylinder collider",
        slab: "Deflector slab",
      }[Kind];
      this.SelectObject("collider");
    }
    this.MarkDirty();
    this.Notify("Scene slot updated. Existing simulation preserved.");
  }

  Restart() {
    this.Bursts = [];
    if (this.Engine) {
      this.Engine.clearGrid();
      this.Engine.pendingBlasts = [];
      this.Engine.time = 0;
      this.Engine.stepCount = 0;
      this.Engine.dynamicBoundsSurge = 0;
      this.Engine.dynamicBoundsTarget = 0;
      this.Engine.setBrush(null, null, false);
    }
    this.SingleStep = false;
  }

  TriggerBurst(Kind = Select("#burst-kind").value, Center = null) {
    if (!this.Engine)
      return this.Notify(
        "The GPU renderer must be available to create a burst.",
      );
    if (this.Parameters.paused)
      this.Notify("Burst queued. Press Space or step once to simulate.");
    const Schedule = (Delay, Options) =>
      this.Bursts.push({ Time: this.Engine.time + Delay, Options });
    if (Center) {
      Schedule(0, { center: Center });
      return;
    }
    if (Kind === "salvo")
      [
        [0.32, 0.16, 0.34],
        [0.68, 0.18, 0.33],
        [0.33, 0.2, 0.67],
        [0.66, 0.22, 0.66],
      ].forEach((Position, Index) =>
        Schedule(Index * 0.17, {
          center: Position,
          radius: this.Parameters.blastRadius * 0.85,
          strength: this.Parameters.blastStrength * 0.95,
          temp: this.Parameters.blastTemperature * 1.05,
          smoke: this.Parameters.blastSmoke * 1.1,
          lobes: 7,
        }),
      );
    else if (Kind === "collide")
      [-1, 1].forEach((Direction, Index) =>
        Schedule(Index * 0.03, {
          center: [Direction < 0 ? 0.22 : 0.78, 0.3, 0.5],
          radius: 0.16,
          strength: 7.5,
          temp: 6.2,
          fuel: 4.8,
          smoke: 2.5,
          lobes: 6,
          directionalVel: [-Direction * 14, 1.5, 0],
        }),
      );
    else if (Kind === "mushroom") {
      this.Restart();
      Schedule(0, {
        center: [0.5, 0.14, 0.5],
        radius: 0.23,
        strength: 12.5,
        temp: 6.5,
        fuel: 5.5,
        smoke: 3.2,
        lobes: 8,
      });
      Schedule(0.15, {
        center: [0.5, 0.29, 0.5],
        radius: 0.19,
        strength: 9.5,
        temp: 6.8,
        fuel: 4.8,
        smoke: 2.4,
        lobes: 6,
      });
    } else
      Schedule(0, {
        spawnShrapnel: Kind === "shrapnel" || this.Parameters.shrapnelEnabled,
      });
  }

  SaveScene() {
    const Scene = {
      format: "frontier-fluid-scene",
      version: 1,
      sourceRevision: SourceRevision,
      name: Select("#document-name").value.trim() || "Untitled scene",
      params: { ...this.Parameters },
      names: { ...this.Names },
      camera: {
        theta: this.Camera.targetTheta,
        phi: this.Camera.targetPhi,
        distance: this.Camera.targetDistance,
        center: [...this.Camera.targetCenter],
      },
    };
    const Url = URL.createObjectURL(
      new Blob([JSON.stringify(Scene, null, 2)], { type: "application/json" }),
    );
    const Link = document.createElement("a");
    Link.href = Url;
    Link.download = `${Scene.name.replace(/[^a-z0-9 _-]/gi, "").trim() || "scene"}.fluid.json`;
    Link.click();
    setTimeout(() => URL.revokeObjectURL(Url), 1000);
    this.Dirty = false;
    this.RefreshControls();
    this.Notify("Scene settings saved. Live voxel fields are not cached.");
  }

  // The parity corpus. Loading one goes through exactly the path an opened file goes through, including
  // ValidateScene, so a sample that would not survive a save and reload cannot be loaded here either.
  LoadSample(Identity) {
    if (!Identity) return;
    try {
      const Scene = ValidateScene(ConstructSampleScene(Identity));
      this.ApplySceneParameters(Scene.Parameters);
      this.Names = Scene.Names;
      Select("#document-name").value = Scene.Name;
      if (Scene.Camera) {
        this.Camera.targetTheta = Scene.Camera.theta;
        this.Camera.targetPhi = Scene.Camera.phi;
        this.Camera.targetDistance = Scene.Camera.distance;
        this.Camera.targetCenter = Scene.Camera.center;
      }
      this.Preset = "";
      this.Selection = "domain";
      this.Dirty = false;
      this.ConstructPresetCards();
      this.ConstructSceneRows();
      this.ConstructInspector();
      this.RefreshControls();
      this.Notify(`Loaded sample: ${Scene.Name}`);
    } catch (Failure) {
      this.Notify(`Could not load that sample: ${Failure.message}`);
    }
  }

  // 🔴 The engine reads TOML, not JSON. This writes the exact text Engine/VolumetricDynamics parses with
  //    toml++, from the same scene object the JSON path writes, so the two cannot describe different scenes.
  SaveSceneToml() {
    const Name = Select("#document-name").value.trim() || "Untitled scene";
    const Scene = {
      format: "frontier-fluid-scene",
      version: 1,
      name: Name,
      params: { ...this.Parameters },
      names: { ...this.Names },
      camera: {
        theta: this.Camera.targetTheta,
        phi: this.Camera.targetPhi,
        distance: this.Camera.targetDistance,
        center: [...this.Camera.targetCenter],
      },
    };
    const Url = URL.createObjectURL(
      new Blob([WriteSceneToml(Scene)], { type: "text/plain" }),
    );
    const Link = document.createElement("a");
    Link.href = Url;
    Link.download = `${Name.replace(/[^a-z0-9 _-]/gi, "").trim() || "scene"}${SceneExtension}`;
    Link.click();
    setTimeout(() => URL.revokeObjectURL(Url), 1000);
    this.Notify(`Saved ${SceneExtension} — this is the file the engine reads.`);
  }

  async OpenScene(File) {
    if (!File) return;
    const DocumentIdentity = this.Documents.ActiveIdentity;
    try {
      if (File.size > 256 * 1024)
        throw new Error("Scene settings must be smaller than 256 KB.");
      const Scene = ValidateScene(JSON.parse(await File.text()));
      if (
        DocumentIdentity !== this.Documents.ActiveIdentity ||
        this.Baking ||
        this.Switching
      )
        throw new Error(
          "The active document changed or is busy. Select the intended tab and open the scene again.",
        );
      this.ApplySceneParameters(Scene.Parameters);
      this.Names = Scene.Names;
      Select("#document-name").value = Scene.Name;
      if (Scene.Camera) {
        this.Camera.targetTheta = Scene.Camera.theta;
        this.Camera.targetPhi = Scene.Camera.phi;
        this.Camera.targetDistance = Scene.Camera.distance;
        this.Camera.targetCenter = Scene.Camera.center;
      }
      this.Preset = "";
      this.Selection = "domain";
      this.Tab = "simulation";
      this.Dirty = false;
      this.ConstructPresetCards();
      this.ConstructSceneRows();
      this.ConstructInspector();
      this.RefreshControls();
      this.Notify(
        "Scene settings opened. Simulation restarted from an empty grid.",
      );
    } catch (ErrorValue) {
      this.Notify(`Scene not opened: ${ErrorValue.message}`);
    } finally {
      Select("#import-file").value = "";
    }
  }

  ConnectInterface() {
    ["#render-channel", "#diagnostic-channel"].forEach((Selector) => {
      Select(Selector).innerHTML = DEBUG_CHANNELS.map(
        (Channel) => `<option value="${Channel.id}">${Channel.label}</option>`,
      ).join("");
      Select(Selector).addEventListener("change", (Event) =>
        this.ApplyParameter("renderChannel", Number(Event.target.value)),
      );
    });
    Select("#scene-tree").addEventListener("click", (Event) => {
      const Toggle = Event.target.closest("[data-toggle-object]");
      if (Toggle) return this.ToggleObject(Toggle.dataset.toggleObject);
      const Row = Event.target.closest("[data-object]");
      if (Row) this.SelectObject(Row.dataset.object);
    });
    Select("#scene-search").addEventListener("input", () =>
      this.ConstructSceneRows(),
    );
    Select("#scene-filters").addEventListener("click", (Event) => {
      const Filter = Event.target.closest("[data-scene-filter]");
      if (Filter) {
        this.SceneFilter = Filter.dataset.sceneFilter;
        this.ConstructSceneRows();
      }
    });
    Select("#compact-outliner").addEventListener("click", () => {
      const Compact =
        Select(".left-panel").classList.toggle("compact-outliner");
      Select("#compact-outliner").setAttribute("aria-pressed", String(Compact));
    });
    Select("#collection-toggle").addEventListener("click", () => {
      const Expanded =
        Select("#collection-toggle").getAttribute("aria-expanded") === "true";
      Select("#collection-toggle").setAttribute(
        "aria-expanded",
        String(!Expanded),
      );
      Select("#scene-tree").hidden = Expanded;
    });
    Select("#scene-tree").addEventListener("keydown", (Event) => {
      if (!Event.target.matches("[data-object]")) return;
      if (Event.key === "Enter") this.SelectObject(Event.target.dataset.object);
      const Rows = SelectAll("#scene-tree [data-object]");
      const Index = Rows.indexOf(Event.target);
      if (Event.key === "ArrowDown" || Event.key === "ArrowUp") {
        Event.preventDefault();
        Rows[
          Math.max(
            0,
            Math.min(
              Rows.length - 1,
              Index + (Event.key === "ArrowDown" ? 1 : -1),
            ),
          )
        ].focus();
      }
    });
    Select("#scene-tree").addEventListener("dblclick", (Event) => {
      if (Event.target.closest("[data-object]")) {
        Select("#object-name").focus();
        Select("#object-name").select();
      }
    });
    Select("#object-name").addEventListener("change", (Event) => {
      this.Names[this.Selection] =
        Event.target.value.trim() || this.Names[this.Selection];
      Event.target.value = this.Names[this.Selection];
      this.ConstructSceneRows();
      Select("#viewport-object").textContent = this.Names[this.Selection];
      this.MarkDirty();
    });
    Select("#document-name").addEventListener("input", () => this.MarkDirty());
    Select("#object-enabled").addEventListener("change", () =>
      this.ToggleObject(this.Selection),
    );
    Select("#presets-list").addEventListener("click", (Event) => {
      const Button = Event.target.closest("[data-preset]");
      if (Button) this.LoadPreset(Button.dataset.preset);
    });
    Select("#preset-search").addEventListener("input", () =>
      this.ConstructPresetCards(),
    );
    Select("#reset-search").addEventListener("click", () => {
      Select("#preset-search").value = "";
      this.Filter = "all";
      this.ConstructPresetCards();
    });
    Select("#preset-filters").addEventListener("click", (Event) => {
      if (Event.target.dataset.filter) {
        this.Filter = Event.target.dataset.filter;
        this.ConstructPresetCards();
      }
    });
    Select("#inspector-tabs").addEventListener("click", (Event) => {
      if (Event.target.dataset.tab) {
        this.Tab = Event.target.dataset.tab;
        this.ConstructInspector();
      }
    });
    let Scrub = null;
    Select("#inspector-body").addEventListener("pointerdown", (Event) => {
      if (
        !Event.target.matches(".value-pill input[type=number]") ||
        Event.button !== 0
      )
        return;
      Scrub = {
        Element: Event.target,
        Pointer: Event.pointerId,
        X: Event.clientX,
        Value: Number(Event.target.value),
        Moved: false,
      };
      Event.target.setPointerCapture(Event.pointerId);
    });
    Select("#inspector-body").addEventListener("pointermove", (Event) => {
      if (!Scrub || Scrub.Pointer !== Event.pointerId) return;
      const Delta = Event.clientX - Scrub.X;
      if (!Scrub.Moved && Math.abs(Delta) < 3) return;
      Event.preventDefault();
      Scrub.Moved = true;
      Scrub.Element.classList.add("scrubbing");
      const Control = ControlSpecification[Scrub.Element.dataset.param];
      const Scale = Event.shiftKey ? 0.1 : Event.altKey ? 10 : 1;
      const Value =
        Math.round(
          (Scrub.Value + Delta * Control.step * Scale) / Control.step,
        ) * Control.step;
      this.ApplyParameter(
        Control.key,
        Number(Math.max(Control.min, Math.min(Control.max, Value)).toFixed(6)),
      );
    });
    for (const Name of ["pointerup", "pointercancel", "lostpointercapture"])
      Select("#inspector-body").addEventListener(Name, () => {
        if (Scrub) Scrub.Element.classList.remove("scrubbing");
        Scrub = null;
      });
    Select("#inspector-body").addEventListener("input", (Event) => {
      if (Event.target.type === "range")
        this.ApplyParameter(
          Event.target.dataset.param,
          Number(Event.target.value),
        );
    });
    Select("#inspector-body").addEventListener("change", (Event) => {
      const Element = Event.target;
      if (Element.dataset.param)
        this.ApplyParameter(
          Element.dataset.param,
          Element.type === "checkbox" ? Element.checked : Number(Element.value),
        );
    });
    Select("#inspector-body").addEventListener("click", (Event) => {
      if (Event.target.id === "remove-object") {
        if (this.Selection === "emitter")
          this.ApplyParameter("emitterEnabled", false);
        else {
          this.ApplyParameter("obstacleType", 0);
          this.SelectObject("domain");
        }
      }
    });
    Select("#reset-properties").addEventListener("click", () => {
      this.VisibleKeys.forEach((Key) =>
        this.ApplyParameter(Key, InitialParameters[Key]),
      );
      this.Notify("Visible inspector properties restored to editor defaults.");
    });
    Select("#add-button").addEventListener("click", () => {
      const Menu = Select("#add-menu");
      const Rectangle = Select("#add-button").getBoundingClientRect();
      Menu.hidden = !Menu.hidden;
      Menu.style.top = `${Rectangle.bottom + 5}px`;
      Menu.style.left = `${Rectangle.left - 12}px`;
      Select("#add-button").setAttribute("aria-expanded", String(!Menu.hidden));
    });
    Select("#add-menu").addEventListener("click", (Event) => {
      const Button = Event.target.closest("[data-add]");
      if (Button) this.AddObject(Button.dataset.add);
    });
    document.addEventListener("pointerdown", (Event) => {
      if (!Event.target.closest("#add-menu, #add-button")) {
        Select("#add-menu").hidden = true;
        Select("#add-button").setAttribute("aria-expanded", "false");
      }
    });
    SelectAll("[data-tool]").forEach((Button) =>
      Button.addEventListener("click", () =>
        this.ApplyParameter("interactionMode", Button.dataset.tool),
      ),
    );
    Select("#play-button").addEventListener("click", () =>
      this.ApplyParameter("paused", !this.Parameters.paused),
    );
    Select("#step-button").addEventListener("click", () => {
      this.ApplyParameter("paused", true);
      this.SingleStep = true;
    });
    Select("#reset-button").addEventListener("click", () => {
      this.Restart();
      this.Notify("Simulation restarted. Scene settings preserved.");
    });
    Select("#burst-button").addEventListener("click", () =>
      this.TriggerBurst(),
    );
    Select("#speed-select").addEventListener("change", (Event) =>
      this.ApplyParameter("timeScale", Number(Event.target.value)),
    );
    Select("#overlays-button").addEventListener("click", () =>
      this.ApplyParameter("showBoundingBox", !this.Parameters.showBoundingBox),
    );
    Select("#turntable-button").addEventListener("click", () =>
      this.ApplyParameter("autoTurntable", !this.Parameters.autoTurntable),
    );
    Select("#focus-button").addEventListener("click", () => this.FocusVolume());
    Select("#maximize-button").addEventListener("click", () => {
      Select("#app").classList.toggle("maximized");
    });
    Select("#camera-view").addEventListener("change", (Event) => {
      const View = Event.target.value;
      if (View === "perspective") this.FocusVolume();
      else {
        this.Camera.targetTheta = View === "side" ? Math.PI / 2 : 0;
        this.Camera.targetPhi = View === "top" ? 0.03 : Math.PI / 2;
        this.MarkDirty();
      }
    });
    Select("#atlas-toggle").addEventListener("change", (Event) =>
      this.ApplyParameter("showAtlasMinimap", Event.target.checked),
    );
    Select("#backend-select").addEventListener("change", (Event) => {
      this.InitializeRenderer(Event.target.value);
      this.Notify("Backend change restarts the simulation.");
    });
    Select("#retry-gpu").addEventListener("click", () =>
      this.InitializeRenderer("webgl2"),
    );
    Select("#export-button").addEventListener("click", () => this.SaveScene());
    Select("#sample-toml-button").addEventListener("click", () => this.SaveSceneToml());
    const Picker = Select("#sample-picker");
    for (const Entry of SampleSummary()) {
      const Choice = document.createElement("option");
      Choice.value = Entry.identity;
      Choice.textContent = Entry.name;
      Choice.title = Entry.covers;
      Picker.append(Choice);
    }
    Picker.addEventListener("change", (Event) => {
      this.LoadSample(Event.target.value);
      Event.target.value = "";
    });
    Select("#import-button").addEventListener("click", () =>
      Select("#import-file").click(),
    );
    Select("#import-file").addEventListener("change", (Event) =>
      this.OpenScene(Event.target.files[0]),
    );
    Select("#help-button").addEventListener("click", () =>
      Select("#help-dialog").showModal(),
    );
    Select("#close-help").addEventListener("click", () =>
      Select("#help-dialog").close(),
    );
    Select("#diagnostics-button").addEventListener("click", () => {
      Select("#diagnostics").hidden = !Select("#diagnostics").hidden;
    });
    Select("#close-diagnostics").addEventListener("click", () => {
      Select("#diagnostics").hidden = true;
    });
    Select("#library-button").addEventListener("click", () => {
      Select("#app").classList.toggle("mobile-library");
      Select("#preset-search").focus();
    });
    Select("#workspace-button").addEventListener("click", () => {
      Select("#app").classList.remove("mobile-library", "maximized");
      Select("#diagnostics").hidden = true;
    });
    document.addEventListener("keydown", (Event) => this.KeyPress(Event));
    document.addEventListener("visibilitychange", () => {
      this.LastTime = performance.now();
      if (document.hidden) this.Engine?.setBrush(null, null, false);
    });
    window.addEventListener("blur", () =>
      this.Engine?.setBrush(null, null, false),
    );
  }

  FocusVolume() {
    this.Camera.reset();
    const Bounds = this.Engine?.getBoundsBox();
    this.Camera.targetDistance = Math.min(
      9.5,
      Math.max(
        Bounds?.effWidth || this.Parameters.boundsWidth,
        Bounds?.effHeight || this.Parameters.boundsHeight,
      ) * 2.3,
    );
    Select("#camera-view").value = "perspective";
    this.MarkDirty();
  }

  KeyPress(Event) {
    if (Select("#flipbook-dialog").open) return;
    if (
      (Event.ctrlKey || Event.metaKey) &&
      Event.shiftKey &&
      Event.key.toLowerCase() === "f"
    ) {
      Event.preventDefault();
      Select("#scene-search").focus();
      return;
    }
    if ((Event.ctrlKey || Event.metaKey) && Event.key.toLowerCase() === "s") {
      Event.preventDefault();
      document.activeElement?.blur();
      this.SaveScene();
      return;
    }
    if (
      Select("#help-dialog").open ||
      Event.target.closest("input,select,textarea,[contenteditable]")
    )
      return;
    if (Event.ctrlKey || Event.metaKey || Event.altKey || Event.repeat) return;
    const Key = Event.key.toLowerCase();
    const Actions = {
      " ": () => this.ApplyParameter("paused", !this.Parameters.paused),
      s: () => {
        this.ApplyParameter("paused", true);
        this.SingleStep = true;
      },
      r: () => this.Restart(),
      d: () => this.TriggerBurst(),
      f: () => this.FocusVolume(),
      b: () =>
        this.ApplyParameter(
          "showBoundingBox",
          !this.Parameters.showBoundingBox,
        ),
      t: () =>
        this.ApplyParameter("autoTurntable", !this.Parameters.autoTurntable),
      1: () => this.ApplyParameter("interactionMode", "orbit"),
      2: () => this.ApplyParameter("interactionMode", "flamethrower"),
      3: () => this.ApplyParameter("interactionMode", "detonate"),
      "/": () => {
        Select("#app").classList.add("mobile-library");
        Select("#preset-search").focus();
      },
      escape: () => {
        Select("#app").classList.remove("maximized", "mobile-library");
        Select("#add-menu").hidden = true;
        Select("#diagnostics").hidden = true;
      },
    };
    if (Actions[Key]) {
      Event.preventDefault();
      Actions[Key]();
    }
  }

  ConnectViewport() {
    let Drag = null;
    const Hit = (Event) => {
      if (!this.Engine) return null;
      const Rectangle = this.Canvas.getBoundingClientRect();
      const Bounds = this.Engine.getBoundsBox();
      return this.Camera.raycastVolumeUVW(
        ((Event.clientX - Rectangle.left) / Rectangle.width) * 2 - 1,
        1 - ((Event.clientY - Rectangle.top) / Rectangle.height) * 2,
        Bounds.boxMin,
        Bounds.boxMax,
      );
    };
    this.Canvas.addEventListener("contextmenu", (Event) =>
      Event.preventDefault(),
    );
    this.Canvas.addEventListener("pointerdown", (Event) => {
      if (Event.button > 2) return;
      this.Canvas.setPointerCapture(Event.pointerId);
      const Mode =
        Event.button === 2 || Event.button === 1
          ? "pan"
          : Event.shiftKey
            ? "flamethrower"
            : this.Parameters.interactionMode;
      Drag = { X: Event.clientX, Y: Event.clientY, Mode, Hit: Hit(Event) };
      if (Mode === "detonate") {
        if (Drag.Hit) this.TriggerBurst("burst", Drag.Hit);
        else this.Notify("Click inside the domain to place a burst.");
      }
      if (Mode === "flamethrower" && Drag.Hit)
        this.Engine?.setBrush(Drag.Hit, [0, 3, 0], true);
    });
    this.Canvas.addEventListener("pointermove", (Event) => {
      if (!Drag) return;
      const DeltaX = Event.clientX - Drag.X;
      const DeltaY = Event.clientY - Drag.Y;
      if (Drag.Mode === "orbit") {
        this.Camera.orbit(DeltaX, DeltaY);
        this.MarkDirty();
      }
      if (Drag.Mode === "pan") {
        this.Camera.pan(DeltaX, DeltaY);
        this.MarkDirty();
      }
      if (Drag.Mode === "flamethrower") {
        const Position = Hit(Event);
        const Velocity = Position?.map(
          (Value, Index) =>
            (Value - (Drag.Hit?.[Index] ?? Value)) * 55 + (Index === 1 ? 3 : 0),
        );
        this.Engine?.setBrush(Position, Velocity, !!Position);
        Drag.Hit = Position;
      }
      Drag.X = Event.clientX;
      Drag.Y = Event.clientY;
    });
    ["pointerup", "pointercancel", "lostpointercapture"].forEach((Name) =>
      this.Canvas.addEventListener(Name, () => {
        Drag = null;
        this.Engine?.setBrush(null, null, false);
      }),
    );
    this.Canvas.addEventListener(
      "wheel",
      (Event) => {
        Event.preventDefault();
        this.Camera.zoom(Math.max(-100, Math.min(100, Event.deltaY)));
        this.MarkDirty();
      },
      { passive: false },
    );
    const ConnectedCanvas = this.Canvas;
    this.Canvas.addEventListener("webglcontextlost", (Event) => {
      Event.preventDefault();
      if (ConnectedCanvas !== this.Canvas) {
        const Document = this.Documents?.Slots.find(
          (Slot) => Slot.State?.Canvas === ConnectedCanvas,
        );
        if (Document)
          Document.State.Fault =
            "This document lost its GPU context. Retry to rebuild it from its settings.";
        return;
      }
      this.ShowGpuError(
        "WebGL context lost. Retry to rebuild the simulation; settings are preserved.",
      );
    });
  }

  Frame(Time) {
    requestAnimationFrame((NextTime) => this.Frame(NextTime));
    const Delta = Math.max(0.001, (Time - this.LastTime) / 1000);
    this.LastTime = Time;
    if (
      document.hidden ||
      this.Switching ||
      this.Baking ||
      !Select("#gpu-error").hidden
    )
      return;
    this.Elapsed += Delta;
    this.Frames++;
    if (this.Parameters.autoTurntable)
      this.Camera.targetTheta += Math.min(Delta, 0.05) * 0.35;
    this.Camera.update(Math.min(Delta, 0.1));
    if (this.Engine) {
      const BasePosition = [
        this.Parameters.obstacleX,
        this.Parameters.obstacleY,
        this.Parameters.obstacleZ,
      ];
      const Position = ComputeColliderPosition(
        this.Parameters,
        this.Engine.time,
      );
      [
        this.Parameters.obstacleX,
        this.Parameters.obstacleY,
        this.Parameters.obstacleZ,
      ] = Position;
      try {
        if (!this.Parameters.paused || this.SingleStep) {
          const Ready = this.Bursts.filter(
            (Burst) => Burst.Time <= this.Engine.time,
          );
          this.Bursts = this.Bursts.filter(
            (Burst) => Burst.Time > this.Engine.time,
          );
          Ready.forEach((Burst) => this.Engine.triggerExplosion(Burst.Options));
          this.Engine.stepSimulation(this.SingleStep ? 1 / 60 : Delta);
          this.SingleStep = false;
        }
        this.Engine.render(this.Camera);
        this.RenderBounds();
      } catch (ErrorValue) {
        this.ShowGpuError(ErrorValue.message);
      } finally {
        [
          this.Parameters.obstacleX,
          this.Parameters.obstacleY,
          this.Parameters.obstacleZ,
        ] = BasePosition;
      }
    }
    if (this.Elapsed >= 0.5) {
      this.FramesPerSecond = Math.round(this.Frames / this.Elapsed);
      this.Frames = 0;
      this.Elapsed = 0;
      Select("#fps").textContent = this.Parameters.paused
        ? "—"
        : this.FramesPerSecond;
      const Seconds = this.Engine?.time || 0;
      Select("#timecode").textContent =
        `${String(Math.floor(Seconds / 60)).padStart(2, "0")}:${(Seconds % 60).toFixed(3).padStart(6, "0")}`;
      Select("#frame-counter").textContent =
        `FRAME ${String(this.Engine?.stepCount || 0).padStart(4, "0")}`;
      Select("#playhead").style.left = `${((Seconds % 30) / 30) * 100}%`;
      const Origin = Math.floor(Seconds / 30) * 30;
      SelectAll(".ruler-numbers span").forEach((Element, Index) => {
        Element.textContent = `${Origin + Index * 5}s`;
      });
      const Bounds = this.Engine?.getBoundsBox();
      if (Bounds)
        Select("#bounds-status").textContent =
          `${Bounds.effWidth.toFixed(1)} × ${Bounds.effHeight.toFixed(1)} × ${Bounds.effWidth.toFixed(1)} m`;
      Select("#diagnostic-values").innerHTML =
        `<dt>Backend</dt><dd>${this.Backend === "webgpu" ? "WebGPU" : "WebGL2"}</dd><dt>Voxel grid</dt><dd>${this.Parameters.gridResolution}³</dd><dt>Canvas</dt><dd>${this.Canvas.width} × ${this.Canvas.height}</dd><dt>Ray / shadow steps</dt><dd>${this.Parameters.raymarchSteps} / ${this.Parameters.shadowSteps}</dd><dt>Simulation steps</dt><dd>${this.Engine?.stepCount || 0}</dd><dt>CPU submission</dt><dd>${(this.Engine?.simDurationMs || 0).toFixed(2)} ms</dd><dt>Queued bursts</dt><dd>${this.Bursts.length + (this.Engine?.pendingBlasts.length || 0)}</dd>`;
      if (this.Parameters.autoGpuGovernor && !this.Parameters.paused) {
        if (this.FramesPerSecond < 35 && this.Parameters.raymarchSteps > 36)
          this.ApplyParameter(
            "raymarchSteps",
            Math.max(36, this.Parameters.raymarchSteps - 4),
          );
        else if (
          this.FramesPerSecond >= 58 &&
          this.Parameters.raymarchSteps < 96
        )
          this.ApplyParameter(
            "raymarchSteps",
            Math.min(96, this.Parameters.raymarchSteps + 4),
          );
      }
    }
  }
}

window.fluidEditor = new FluidPanel();
