import {
  Prefix as FracturePrefix,
  Publish as PublishFracture,
  Describe as DescribeFracture,
  Normalize as NormalizeFracture,
  ReadRecord as ReadFracture,
} from "../FractureEditor/FractureSpecification.js";
import { CloudProperties, CloudEdit } from "./CloudSpecification.js";
import {
  HeightFogChanges,
  NormalizeHeightFogValues,
  WithoutHeightFogDuplicates,
} from "./HeightFogSpecification.js";
import { LightContextDefaults } from "./LightSpecification.js";
import ReferencePanel, {
  EnsureReferenceLights,
  PrepareLightRows,
  ReferenceLights,
  HasReferencePanel,
} from "./ReferencePanel.jsx";
import FolderInspector from "./FolderInspector.jsx";
import { FolderInventory } from "./FolderInventory.mjs";
import DiagnosticsCard from "./DiagnosticsCard.jsx";
import { EnsureGodRays, GodRaysRow, MoonSlotCount, MaximumLocalClouds } from "./ProjectZeroSpecification.js";
import { DebugViews, DebugViewLabel } from "./DebugViews.js";
import ProjectBrowser, { RestoreProjects, StoreProjects, BlankSceneRows, BlankSceneValues, MaximumProjectVersions } from "./ProjectBrowser.jsx";
import ViewportRenderHeader from "./ViewportRenderHeader.jsx";
import { StartThoughtLog, useThoughtScope, OpenThought } from "./ThoughtLog.js";
import { ResolveRenderSettings, RenderSettingsKey } from "./RenderSettings.js";
import { WorkspaceView, WorkspaceRail } from "./Workspaces.jsx";
import { VehiclePanel, VehicleSummary, ComponentIcon, EnsureVehicleFolder, InitialVehicleRows, VehicleRevision } from "./VehicleComponents.jsx";
import { VehicleAssetPanel, VehicleAssetSummary } from "./VehicleAssetInspector.jsx";
import { ComponentPanel, ComponentSummary, WithObjectComponents, EnsureObjectComponents, ComponentRowsFor, OwnerKind, ComponentRevision, ComponentAddress } from "./ObjectComponents.jsx";
import { BrowserFPS } from "./BrowserTiming.js";
import {
  EnsureEditorCamera,
  IsEditorCamera,
  ConsolidateAtmosphere,
  ConsolidateAtmosphereValues,
  ConsolidateAtmosphereFlags,
} from "./ScenePolicy.js";
import WindEditor from "./WindPanel.jsx";
import AssetPanel from "./AssetPanel.jsx";
import { RestoreAssets } from "./AssetDepot.js";
import MaterialPanel from "./MaterialPanel.jsx";
import { ResolveMaterial, ChangeChannel } from "./MaterialSpecification.js";
import {
  ApplyToActiveColourSlot,
  GetActiveColourSlot,
} from "./ColourCodex.js";
import ShaderPanel from "./ShaderPanel.jsx";
import React, { useState, useEffect, useRef, useMemo } from "react";
import { createRoot } from "react-dom/client";
import { Inspector, Icon, Glyph, Panels } from "./Inspectors.jsx";
import Viewport3D from "./Viewport3D.jsx";
import { VehicleComponentPanel, VehicleOwner } from "./VehicleComponentViewer.jsx";
import { CameraBodies, CameraBodyOf } from "./EditorAssets.js";
// Cameras show their editor asset (EditorAssets.js) as their outliner icon; everything else its own.
const OutlinerIcon = (Row, Values) =>
  Row.Panel === "camera" && window.NativeAssets?.Icons
    ? CameraBodies.find((Body) => Body.Id === CameraBodyOf(Row.Id, Values[Row.Id]?.["Camera body"]))?.Icon || Row.Icon
    : Row.Panel === VehiclePanel
      ? ComponentIcon(Row, Values[Row.Id] || {})
      : Row.Icon;
import Notch from "./Notch.jsx";
import ActionIcon from "./ActionIcon.jsx";
import ConstructPanel from "./ConstructPanel.jsx";
import {
  EffectRows,
  EffectSummary,
  IsEffectPanel,
  ArrangeCameraEffects,
} from "./PostEffects.jsx";
import {
  EnsureEnvironmentLight,
  EnvironmentPanel,
  EnvironmentRow,
  EnvironmentSummary,
} from "./EnvironmentLight.jsx";
import CheckerViewport, { PreviewVisible } from "./CheckerViewport.jsx";
import ComponentViewerPanel from "./ComponentViewerPanel.jsx";
import "./Editor.css";
import "./ProjectZeroSpecification.css";
const InitialRows = [
  ["showcase", "Showcase", "group", "folder-scene", null, "Scene collection"],
  ["cube", "Cube", "geometry", "editor-cube", "showcase", "Geometry"],
  ["sphere", "Sphere", "geometry", "editor-sphere", "showcase", "Geometry"],
  [
    "cylinder",
    "Cylinder",
    "geometry",
    "editor-cylinder",
    "showcase",
    "Geometry",
  ],
  ["torus", "Torus", "geometry", "editor-torus", "showcase", "Geometry"],
  ["cone", "Cone", "geometry", "editor-cone", "showcase", "Geometry"],
  ["cameras", "Cameras", "group", "camera", null, "Camera collection"],
  [
    "camera",
    "Editor Camera",
    "camera",
    "camera",
    "cameras",
    "Permanent editor navigation camera · not a scene render camera",
  ],
  [
    "cine",
    "Cine Camera",
    "camera",
    "camera",
    "cameras",
    "Inactive optical study",
  ],
  [
    "flare",
    "Lens Flare",
    "flare",
    "lens-flare",
    "cameras",
    "Camera optical effect · sun-driven",
  ],
  [
    "post",
    "Post Process",
    "post",
    "environment-exposure",
    "cameras",
    "Post processing",
  ],
  ["world", "World", "group", "folder-world", null, "Environment"],
  [
    "atmosphere",
    "Atmosphere",
    "atmosphere",
    "sky-scattering",
    "world",
    "Procedural sky",
  ],
  ["sun", "Sun", "sun", "sun", "world", "Directional light"],
  [GodRaysRow.Id, GodRaysRow.Name, GodRaysRow.Panel, GodRaysRow.Icon, GodRaysRow.Parent, GodRaysRow.Description],
  ["moon", "Moons", "moon", "moon", "world", "Lunar bodies · six slots"],
  [
    "stars",
    "Stars",
    "stars",
    "outliner-stars",
    "world",
    "Catalogue field · scintillation · celestial pole",
  ],
  [
    "wind",
    "Wind",
    "wind",
    "wind",
    "world",
    "Wind / Air · shared advection field",
  ],
  [
    "height-fog",
    "Height Fog",
    "height-fog",
    "fog",
    "world",
    "Height fog · exponential vertical density",
  ],
  [
    "aerial-fog",
    "Atmospheric Fog",
    "aerial-fog",
    "fog",
    "world",
    "Aerial perspective · surface-distance fog",
  ],
  [
    "local-fog",
    "Local Fog",
    "local-fog",
    "local-fog",
    "world",
    "Local volumetric fog · bounded medium",
  ],
  ["clouds", "Clouds", "clouds", "clouds", "world", "Global cloud layer"],
  [
    "precipitation",
    "Precipitation",
    "precipitation",
    "outliner-precipitation",
    "clouds",
    "Clouds / Precipitation · world-space simulation",
  ],
  [
    "local-cloud",
    "Local Cloud",
    "local-cloud",
    "local-cloud",
    "world",
    "Bounded volumetric clouds",
  ],
  [
    "rainbow",
    "Rainbow",
    "rainbow",
    "rainbow",
    "atmosphere",
    "Rainbow · liquid-water optics",
  ],
  ["lighting", "Lighting", "group", "folder-generic", null, "Light collection"],
  ["light", "Area Light", "light", "editor-area-light", "lighting", "Light"],
].map(([Id, Name, Panel, Icon, Parent, Description]) => ({
  Id,
  Name,
  Panel,
  Icon,
  Parent,
  Description,
}));
// Motion Blur sits under Cameras before Lens Flare; the post effects follow Post Process.
InitialRows.splice(
  InitialRows.findIndex((Row) => Row.Id === "flare"),
  0,
  ...EffectRows.filter((Row) => Row.Parent === "cameras"),
);
InitialRows.splice(
  InitialRows.findIndex((Row) => Row.Id === "post") + 1,
  0,
  ...EffectRows.filter((Row) => Row.Parent === "post"),
);
InitialRows.push(...InitialVehicleRows());
// Showcase objects carry Mass, Rigid Body and Collision; cameras carry their render components (all outliner children).
InitialRows.splice(0, InitialRows.length, ...WithObjectComponents(InitialRows));
const DefaultSettings = {
  GI: 2,
  Reflection: 2,
  AA: true,
  FPS: false,
  Notifications: true,
  Quality: 2,
  Patches: false,
  Raytracing: true,
  Scale: 100,
  // The main view: "3d" is the three.js preview (Viewport3D.jsx); "markers" is the checkerboard authoring view.
  Viewport: "3d",
};
// Debug views 00–16 and the earlier editor views: DebugViews.js.
const StorageKey = "Frontier.ProjectZeroHtml.v1";
function Restore() {
  try {
    return JSON.parse(localStorage.getItem(StorageKey)) || {};
  } catch {
    return {};
  }
}
function OutlinerValue(Row, Record, Label) {
  const Stored = Record?.[Label];
  if (Stored !== undefined) return Stored;
  return Panels[Row.Panel]?.find((Field) => Field.Label === Label)?.Default;
}
function CompactNumber(Value, Digits = 1) {
  const NumberValue = Number(Value);
  if (!Number.isFinite(NumberValue)) return "—";
  return NumberValue.toLocaleString("en-US", {
    maximumFractionDigits: Digits,
  });
}
function CompactPosition(Value) {
  const Position = Array.isArray(Value) ? Value : [0, 0, 0];
  return Position.map((Axis) => CompactNumber(Axis, 1)).join(",");
}
function OutlinerMetadata(Row, Record = {}, Rows = []) {
  const V = (Label) => OutlinerValue(Row, Record, Label);
  const LightProperties = {
    ...(LightContextDefaults[Row.ReferencePreset] || {}),
    ...(Record.ReferenceInspector?.Properties || {}),
  };
  const Position = LightProperties.pos || Record.Position || [0, 0, 0];
  if (IsEditorCamera(Row)) return "Editor · permanent";
  if (Row.Panel === ComponentPanel) return ComponentSummary(Row, Rows.find((Each) => Each.Id === Row.Parent), Record);
  if (Row.Panel === "group") {
    const Count = Rows.filter((Item) => Item.Parent === Row.Id).length;
    return Count + (Count === 1 ? " item" : " items");
  }
  if (Row.Panel === "geometry")
    return "Position " + CompactPosition(Position) + " m";
  if (Row.Panel === "camera")
    return (
      CompactNumber(V("Focal Length"), 0) +
      " mm · f/" +
      CompactNumber(V("Aperture"))
    );
  if (IsEffectPanel(Row.Panel)) return EffectSummary(Row.Panel, V);
  if (Row.Panel === EnvironmentPanel) return EnvironmentSummary(V);
  if (Row.Panel === VehiclePanel) return VehicleSummary(Row, Record);
  if (Row.Panel === VehicleAssetPanel) return VehicleAssetSummary(Row, Record);
  if (Row.Panel === "post")
    return (
      "EV " +
      (V("EV Compensation") >= 0 ? "+" : "") +
      CompactNumber(V("EV Compensation"))
    );
  if (Row.Panel === "atmosphere")
    return (
      "Mie " +
      CompactNumber(V("Mie")) +
      "× · ozone " +
      CompactNumber(V("Ozone"))
    );
  if (Row.Panel === GodRaysRow.Panel)
    return V("Enabled") === false ? "Off · sun component" : CompactNumber(V("Intensity")) + "× · " + CompactNumber(V("Steps"), 0) + " steps";
  if (Row.Panel === "sun")
    return (
      CompactNumber(V("Intensity")) +
      "× · " +
      CompactNumber(V("Angular Diameter"), 2) +
      "°"
    );
  if (Row.Panel === "flare")
    return (
      CompactNumber(V("Intensity")) +
      "× · " +
      CompactNumber(V("Ghosts"), 0) +
      " ghosts"
    );
  if (Row.Panel === "moon") {
    const Phase = Record["moon0:Phase"] ?? V("Phase");
    return (
      "Lunar phase " +
      CompactNumber(Phase * 360, 0) +
      "° · " +
      CompactNumber(Phase * 100, 0) +
      "%"
    );
  }
  if (Row.Panel === "stars")
    return (
      CompactNumber(V("Limiting magnitude")) +
      " mag · " +
      CompactNumber(V("Brightness")) +
      "×"
    );
  if (Row.Panel === "wind")
    return (
      CompactNumber(V("Speed")) +
      " m/s · " +
      CompactNumber(V("Bearing"), 0) +
      "°"
    );
  if (Row.Panel === "height-fog")
    return (
      CompactNumber(V("Density"), 2) +
      "× · " +
      CompactNumber(V("Falloff Height"), 0) +
      " m"
    );
  if (Row.Panel === "aerial-fog")
    return (
      CompactNumber(V("Density")) +
      "× · starts " +
      CompactNumber(V("Start"), 0) +
      " m"
    );
  if (Row.Panel === "local-fog")
    return (
      CompactNumber(V("Density")) +
      "× · " +
      CompactNumber(V("Coverage") * 100, 0) +
      "%"
    );
  if (["clouds", "local-cloud"].includes(Row.Panel))
    return (
      CompactNumber(V("Coverage") * 100, 0) +
      "% · " +
      CompactNumber(V("Density")) +
      "×"
    );
  if (Row.Panel === "precipitation") {
    const Type =
      ["Rain", "Drizzle", "Hail", "Snow", "Sleet"][V("Precipitation")] ||
      "Rain";
    return Type + " · " + CompactNumber(V("Intensity")) + " mm/h";
  }
  if (Row.Panel === "rainbow")
    return (
      CompactNumber(V("Intensity")) +
      "× · " +
      CompactNumber(V("Minimum Path"), 0) +
      " m"
    );
  if (Row.Panel === "light") {
    const Properties = LightProperties;
    const Type = Row.ReferenceType || "arealight";
    const Names = {
      pointlight: "Point",
      spotlight: "Spot",
      ieslight: "IES",
      arealight: "Area",
      tubelight: "Tube",
      ledlight: "LED",
      ledstrip: "Strip",
    };
    let Output = V("Intensity"),
      Unit = "lx";
    if (Row.ReferenceType) {
      Unit = ["pointlight", "spotlight"].includes(Type) ? "cd" : "lm";
      Output =
        Type === "ledlight"
          ? (Properties.watts ?? 10) *
            (Properties.efficacy ?? 110) *
            (Properties.dimmer ?? 1)
          : Type === "ledstrip"
            ? (Properties.lumensPerMetre ?? 1000) *
              (Properties.length ?? 2.4) *
              (Properties.dimmer ?? 1)
            : (Properties[
                ["pointlight", "spotlight"].includes(Type)
                  ? "intensity"
                  : "lumens"
              ] ?? 32);
    }
    return (
      CompactNumber(Output, 0) +
      " " +
      Unit +
      " · " +
      Names[Type] +
      " · " +
      CompactPosition(Position)
    );
  }
  return Row.Panel.replaceAll("-", " ");
}

const Saved = Restore();
const SceneRows = Saved.Rows ? ConsolidateAtmosphere(Saved.Rows) : undefined;
const SkyAlias = !SceneRows?.some((Row) => Row.Id === "sky");
Saved.Values = NormalizeHeightFogValues(
  ConsolidateAtmosphereValues(Saved.Values, Saved.Rows),
);
Saved.Hidden = ConsolidateAtmosphereFlags(Saved.Hidden, Saved.Rows);
Saved.Collapsed = ConsolidateAtmosphereFlags(Saved.Collapsed, Saved.Rows);
if (SceneRows)
  Saved.Rows = PrepareLightRows(SceneRows, Saved.LightDesignRevision);
if (SkyAlias && Saved.Selected === "sky") Saved.Selected = "atmosphere";
if (SkyAlias && new URLSearchParams(location.search).get("inspect") === "sky") {
  const Address = new URL(location.href);
  Address.searchParams.set("inspect", "atmosphere");
  history.replaceState(null, "", Address);
}
// Viewport raster scope for the Thought Log: render plus commit of one checker viewport.
// The Atmosphere object's values with their defaults, for the Sun card and the 3D viewport's sky.
const AirOf = (Values) => (Name) => Values.atmosphere?.[Name] ?? Panels.atmosphere?.find((Field) => Field.Label === Name)?.Default;
// Readers for the Sun, Stars and Lens Flare objects (their values, with the sheet's defaults behind them).
const ReaderOf = (Values, Id) => (Name) => Values[Id]?.[Name] ?? Panels[Id]?.find((Field) => Field.Label === Name)?.Default;
function MeasuredViewport(Props) {
  useThoughtScope("Editor.ViewportRaster");
  const Select = (...Arguments) => {
    const Close = OpenThought("Editor.ViewportInteraction");
    Props.Select(...Arguments);
    Close();
  };
  return <CheckerViewport {...Props} Select={Select} />;
}
function App() {
  useThoughtScope("Editor.Tick");
  // The open edit workspace ({ Id, Tool, …view }) or null. It belongs to one selection and closes when the selection changes.
  const [WorkspaceState, StoreWorkspace] = useState(null);
  const SetWorkspace = (Next) =>
    StoreWorkspace((Previous) => {
      const Value = typeof Next === "function" ? Next(Previous) : Next;
      return Value ? { ...Value } : null;
    });
  // Debug is the viewport debug view (0 = 00 Lit); Diagnostics is the overlay card, toggled separately.
  const [Debug, SetDebug] = useState(0),
    [Diagnostics, ShowDiagnostics] = useState(false),
    [ProjectsOpen, OpenProjects] = useState(false),
    [ComponentViewerOpen, OpenComponentViewer] = useState(false),
    [ProjectCatalogue, StoreProjectCatalogue] = useState(RestoreProjects),
    [HiZ, ToggleHiZ] = useState(true),
    [Alias, ToggleAlias] = useState(true),
    [PatchError, SetPatchError] = useState(1);
  const [Rows, StoreRows] = useState(() =>
      EnsureGodRays(EnsureObjectComponents(EnsureVehicleFolder(EnsureEnvironmentLight(
        ArrangeCameraEffects(
          EnsureEditorCamera(
            PrepareLightRows(
              Saved.ReferenceDesignRevision === 1
                ? Saved.Rows || EnsureReferenceLights(InitialRows)
                : EnsureReferenceLights(Saved.Rows || InitialRows),
              Saved.LightDesignRevision,
            ),
          ),
          Saved.Rows ? Saved.CameraEffectsRevision : 1,
        ),
        Saved.Rows ? Saved.EnvironmentLightRevision : 0,
      ), Saved.Rows ? Saved.VehicleRevision : 0), Saved.Rows ? Saved.ComponentRevision : 0)),
    ),
    // An older component address (`cube-mass`) selects the object and opens that entry of its component stack.
    [Selected, Select] = useState(() => {
      const Wanted = new URLSearchParams(location.search).get("inspect") || Saved.Selected || "sun";
      return ComponentAddress(Wanted, Rows)?.Owner || Wanted;
    }),
    [ComponentFocus, FocusComponent] = useState(() => {
      const Address = ComponentAddress(new URLSearchParams(location.search).get("inspect") || Saved.Selected, Rows);
      return Address ? { ...Address, Stamp: 1 } : null;
    }),
    [Values, AssignValues] = useState(() => {
      const Restored = { ...Saved.Values };
      for (const Row of Saved.Rows || InitialRows) {
        const Record = ReadFracture(Row.Id);
        if (
          Row.Panel === "geometry" &&
          Record?.Settings?.EditedAt >
            (Restored[Row.Id]?.Fracture?.EditedAt || 0)
        )
          Restored[Row.Id] = {
            ...Restored[Row.Id],
            Fracture: NormalizeFracture(Record.Settings),
          };
      }
      return Restored;
    }),
    [Hidden, AssignHidden] = useState({ ...Saved.Hidden, camera: false }),
    [Collapsed, Collapse] = useState(Saved.Collapsed || {}),
    [Query, Search] = useState(""),
    [Filters, Filter] = useState([]),
    [Menu, ShowMenu] = useState(null),
    [Shade, OpenShade] = useState(false),
    [Settings, ChangeSettings] = useState({
      ...DefaultSettings,
      ...Saved.Settings,
    }),
    [Wide, Expand] = useState(
      () =>
        new URLSearchParams(location.search).get("workspace") === "inspector",
    ),
    [Name, RenameProject] = useState(Saved.Name || "Project-Zero"),
    [Rename, RenameRow] = useState(null),
    [Construct, OpenConstruct] = useState(false),
    [Projection, SetProjection] = useState("PERSP"),
    [SplitView, SetSplitView] = useState(false),
    // Outliner multi-select: rows chosen alongside Selected (Ctrl or Shift click). Selected stays the one the Inspector shows.
    [Picked, AssignPicked] = useState([]),
    // The last outliner delete, so it can be undone once (Ctrl+Z or the row menu).
    [LastDeleted, KeepDeleted] = useState(null),
    [FrameToken, AskFrame] = useState(0),
    [RunMode, SetRunMode] = useState("EDIT"),
    [Message, Notify] = useState(""),
    [Console, ShowConsole] = useState(false),
    [PaneTabs, AssignTabs] = useState({
      Left: ["Outliner"],
      Centre: ["Viewport"],
      Right: ["Inspector"],
    }),
    [Active, Activate] = useState({
      Left: "Outliner",
      Centre: "Viewport",
      Right: "Inspector",
    }),
    [LeftWidth, ResizeLeft] = useState(316),
    [RightWidth, ResizeRight] = useState(340);
  const PreviousFractureRows = useRef(Rows);
  useEffect(() => {
    const Receive = (Event) => {
      if (!Event.key?.startsWith(FracturePrefix) || !Event.newValue) return;
      try {
        const Record = JSON.parse(Event.newValue);
        const Id = Event.key.slice(FracturePrefix.length);
        if (
          Record.Owner?.Id !== Id ||
          !Rows.some((Row) => Row.Id === Id && Row.Panel === "geometry")
        )
          return;
        AssignValues((Previous) =>
          (Record.Settings?.EditedAt || 0) <
          (Previous[Id]?.Fracture?.EditedAt || 0)
            ? Previous
            : {
                ...Previous,
                [Id]: {
                  ...Previous[Id],
                  Fracture: NormalizeFracture(Record.Settings),
                },
              },
        );
      } catch {
        /* A malformed external record never changes the scene. */
      }
    };
    window.addEventListener("storage", Receive);
    return () => window.removeEventListener("storage", Receive);
  }, [Rows]);
  useEffect(() => {
    for (const Removed of PreviousFractureRows.current) {
      if (
        Removed.Panel !== "geometry" ||
        Rows.some((Row) => Row.Id === Removed.Id)
      )
        continue;
      try {
        const Record = ReadFracture(Removed.Id);
        if (Record)
          PublishFracture(
            { ...Record.Owner, Removed: true },
            { ...Record.Settings, Enabled: false, EditedAt: Date.now() },
          );
      } catch {
        Notify("Could not notify the removed object's fracture editor.");
      }
    }
    PreviousFractureRows.current = Rows;
    for (const Row of Rows) {
      if (Row.Panel !== "geometry" || !Values[Row.Id]?.Fracture) continue;
      try {
        PublishFracture(
          DescribeFracture(Row, Values[Row.Id]),
          Values[Row.Id].Fracture,
        );
      } catch {
        Notify(
          "Fracture settings could not be saved. Browser storage may be full or disabled.",
        );
      }
    }
  }, [Rows, Values]);
  const AssignRows = (Update) =>
    StoreRows((Previous) =>
      EnsureEditorCamera(
        ConsolidateAtmosphere(
          typeof Update === "function" ? Update(Previous) : Update,
        ),
      ),
    );
  const [AssetRecords, StoreAssets] = useState(RestoreAssets(Saved.Assets)),
    [AssetsOpen, OpenAssets] = useState(false),
    [AssetsStage, SetAssetsStage] = useState("closed"),
    [DraggingColour, SetDraggingColour] = useState(false),
    [FocusedColourLabel, SetFocusedColourLabel] = useState(""),
    [ViewportSettings, ShowViewportSettings] = useState(false);
  useEffect(() => {
    const OnSlotChange = (Event) => {
      if (Event.detail?.Label) SetFocusedColourLabel(Event.detail.Label);
    };
    const OnApplied = (Event) => {
      if (Event.detail?.Colour && Event.detail?.TargetLabel) {
        Notify(
          `Applied ${Event.detail.Colour.Name} (${Event.detail.Colour.Hex}) → ${Event.detail.TargetLabel}`,
        );
      }
    };
    const OnFrameMessage = (Event) => {
      if (!Event.data?.ReferenceInspector) return;
      if (Event.data.Type === "colour-slot-focus" && Event.data.Label) {
        SetFocusedColourLabel(Event.data.Label);
      } else if (
        Event.data.Type === "colour-applied" &&
        Event.data.Colour &&
        Event.data.TargetLabel
      ) {
        Notify(
          `Applied ${Event.data.Colour.Name} (${Event.data.Colour.Hex}) → ${Event.data.TargetLabel}`,
        );
      }
    };
    window.addEventListener("frontier:colour-slot-change", OnSlotChange);
    window.addEventListener("frontier:colour-applied", OnApplied);
    window.addEventListener("message", OnFrameMessage);
    return () => {
      window.removeEventListener("frontier:colour-slot-change", OnSlotChange);
      window.removeEventListener("frontier:colour-applied", OnApplied);
      window.removeEventListener("message", OnFrameMessage);
    };
  }, []);
  const ToggleShade = (Next) => {
    OpenShade(Next);
    if (Next) OpenAssets(false);
  };
  const ToggleAssets = (Next) => {
    OpenAssets(Next);
    if (Next) OpenShade(false);
  };
  const [WindTarget, EditWind] = useState(null);
  const WindOpener = useRef(null);
  const CloseWind = () => {
    EditWind(null);
    requestAnimationFrame(() => {
      const Target = WindOpener.current?.isConnected
        ? WindOpener.current
        : document.querySelector('[aria-label="Expand WindEditor"]');
      Target?.focus({ preventScroll: true });
    });
  };
  const WindSubject = Rows.find(
    (Row) => Row.Id === WindTarget && Row.Panel === "wind",
  );
  const WindFields = Rows.filter((Row) => Row.Panel === "wind");
  const [ShaderTarget, SelectShaderTarget] = useState(null),
    [ShaderFloating, FloatShader] = useState(false);
  const ShaderAsset = AssetRecords.find(
    (Asset) => Asset.Id === ShaderTarget && Asset.Kind === "Materials",
  );
  const ShaderSubject =
    ShaderAsset ||
    Rows.find((Row) => Row.Id === ShaderTarget && Row.Panel === "geometry");
  const RemoveShaderTabs = () => {
    AssignTabs((Previous) =>
      Object.fromEntries(
        Object.entries(Previous).map(([Side, Tabs]) => [
          Side,
          Tabs.filter((Tab) => Tab !== "ShaderEditor"),
        ]),
      ),
    );
    Activate((Previous) =>
      Object.fromEntries(
        Object.entries(Previous).map(([Side, Tab]) => [
          Side,
          Tab === "ShaderEditor"
            ? PaneTabs[Side]?.find((Item) => Item !== "ShaderEditor") || ""
            : Tab,
        ]),
      ),
    );
  };
  const OpenShader = (Id) => {
    OpenAssets(false);
    SelectShaderTarget(Id);
    RemoveShaderTabs();
    FloatShader(true);
  };
  const DockShader = (Side) => {
    FloatShader(false);
    RemoveShaderTabs();
    AssignTabs((Previous) => ({
      ...Previous,
      [Side]: [...Previous[Side], "ShaderEditor"],
    }));
    Activate((Previous) => ({ ...Previous, [Side]: "ShaderEditor" }));
  };
  const ShaderBody = (Docked = false) => (
    <div className="shader-document-body">
      <div className="shader-target">
        <label>
          Bound to
          <select
            aria-label="ShaderEditor target"
            value={ShaderSubject?.Id || ""}
            onChange={(Event) => SelectShaderTarget(Event.target.value)}
          >
            <option value="" disabled>
              Select a material owner
            </option>
            {AssetRecords.filter((Asset) => Asset.Kind === "Materials").map(
              (Asset) => (
                <option key={Asset.Id} value={Asset.Id}>
                  {Asset.Name} · library material
                </option>
              ),
            )}
            {Rows.filter((Row) => Row.Panel === "geometry").map((Row) => (
              <option key={Row.Id} value={Row.Id}>
                {Row.Name} · surface
              </option>
            ))}
          </select>
        </label>
        {Docked && (
          <button
            aria-label="Undock ShaderEditor"
            onClick={() => {
              RemoveShaderTabs();
              FloatShader(true);
            }}
          >
            ↗ Float window
          </button>
        )}
      </div>
      {ShaderSubject ? (
        <MaterialPanel
          key={ShaderSubject.Id}
          Subject={ShaderSubject}
          Values={
            ShaderAsset
              ? { Material: ShaderAsset.Material }
              : Values[ShaderSubject.Id] || {}
          }
          Change={(Key, Value) =>
            ShaderAsset
              ? StoreAssets((Previous) =>
                  Previous.map((Asset) =>
                    Asset.Id === ShaderAsset.Id
                      ? {
                          ...Asset,
                          Material: Value,
                          Name: Value.Name || Asset.Name,
                        }
                      : Asset,
                  ),
                )
              : AssignValues((Previous) => ({
                  ...Previous,
                  [ShaderSubject.Id]: {
                    ...Previous[ShaderSubject.Id],
                    [Key]: Value,
                  },
                }))
          }
        />
      ) : (
        <div className="empty-dock">
          The material owner is missing. Choose an object above.
        </div>
      )}
    </div>
  );
  const FileInput = useRef(null),
    SearchInput = useRef(null),
    Divider = useRef(null);
  const Subject = Rows.find((Row) => Row.Id === Selected) || Rows[0];
  const Assign = (Key, Value) => {
    const Aliases = {
      "Global Illumination": "GI",
      Reflections: "Reflection",
      "FPS Overlay": "FPS",
    };
    ChangeSettings((Previous) => ({
      ...Previous,
      [Key]: Value,
      ...(Aliases[Key] ? { [Aliases[Key]]: Value } : {}),
    }));
  };
  const ApplyReference = (Records) => {
    if (!Array.isArray(Records)) return;
    const Known = new Map(Rows.map((Row) => [Row.Id, Row]));
    const Accepted = Records.filter((Record) => Known.has(Record.Id));
    AssignValues((Previous) => {
      const Next = { ...Previous };
      for (const Record of Accepted) {
        if (!HasReferencePanel(Known.get(Record.Id))) continue;
        const Panel = Known.get(Record.Id).Panel,
          CloudChanges = {},
          HeightChanges =
            Panel === "height-fog"
              ? HeightFogChanges(Previous[Record.Id] || {}, Record.Properties)
              : {};
        if (["clouds", "local-cloud"].includes(Panel)) {
          for (const [Name, Key] of Object.entries(CloudProperties)) {
            if (
              Record.Properties[Key] !==
              Previous[Record.Id]?.ReferenceInspector?.Properties?.[Key]
            )
              Object.assign(
                CloudChanges,
                CloudEdit(
                  Previous[Record.Id] || {},
                  Name,
                  Record.Properties[Key] * (Name === "Density" ? 4 : 1),
                  Panel,
                ),
              );
          }
        }
        Next[Record.Id] = {
          ...Next[Record.Id],
          ...CloudChanges,
          ...HeightChanges,
          ReferenceInspector: {
            Properties:
              Panel === "height-fog"
                ? WithoutHeightFogDuplicates(Record.Properties)
                : Record.Properties,
            Locked: Record.Locked,
            Dynamic: Record.Dynamic,
            Notes: Record.Notes,
          },
        };
      }
      return Next;
    });
    AssignHidden((Previous) => ({
      ...Previous,
      ...Object.fromEntries(
        Accepted.map((Record) => [
          Record.Id,
          Record.Id === "camera" ? false : !Record.Visible,
        ]),
      ),
    }));
    Collapse((Previous) => ({
      ...Previous,
      ...Object.fromEntries(
        Accepted.map((Record) => [Record.Id, !Record.Open]),
      ),
    }));
    AssignRows((Previous) =>
      Previous.map((Row) => {
        const Record = Accepted.find((Value) => Value.Id === Row.Id);
        return Record && !IsEditorCamera(Row) && typeof Record.Name === "string"
          ? { ...Row, Name: Record.Name }
          : Row;
      }),
    );
  };
  // Components store their values on the object that owns them.
  const SelectedRow = Rows.find((Row) => Row.Id === Selected);
  const ValueOwner = SelectedRow?.Panel === ComponentPanel ? SelectedRow.Parent : Selected;
  const Change = (Key, Value) =>
    AssignValues((Previous) => ({
      ...Previous,
      [ValueOwner]: {
        ...Previous[ValueOwner],
        [Key]: Key === "Fracture" ? { ...Value, EditedAt: Date.now() } : Value,
      },
    }));
  useEffect(() => {
    document
      .querySelector(".outliner-row.selected")
      ?.scrollIntoView({ block: "nearest" });
  }, [Selected]);
  const ToggleHidden = (Id) => {
    if (Id === "camera") return;
    AssignHidden((Previous) => ({ ...Previous, [Id]: !Previous[Id] }));
  };
  const Toast = (Text) => {
    Notify(Text);
    setTimeout(() => Notify(""), 3200);
  };
  // Project Browser (§14): switching projects keeps the scene being left as that project's newest version (three kept).
  const OpenProject = (Project, VersionIndex = -1, Catalogue = ProjectCatalogue) => {
    const Leaving = Catalogue.Active;
    if (Project.Token === Leaving && VersionIndex < 0) {
      OpenProjects(false);
      return;
    }
    const Stamp = new Date();
    const Kept = { Label: "Saved " + Stamp.toLocaleString("en-GB", { day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" }), Time: Stamp.getTime(), Rows, Values, Hidden };
    const Next = {
      ...Catalogue,
      Active: Project.Token,
      Projects: Catalogue.Projects.map((Each) => (Each.Token === Leaving ? { ...Each, Versions: [...(Each.Versions || []), Kept].slice(-MaximumProjectVersions) } : Each)),
    };
    const Target = Next.Projects.find((Each) => Each.Token === Project.Token) || Project;
    const Version = VersionIndex >= 0 ? Target.Versions?.[VersionIndex] : Target.Versions?.at(-1);
    const Blank = !Version && Project.Token !== "default";
    AssignRows(
      Version
        ? Version.Rows
        : Blank
          ? BlankSceneRows.map((Row) => ({ ...Row }))
          : EnsureGodRays(EnsureObjectComponents(EnsureVehicleFolder(EnsureEnvironmentLight(ArrangeCameraEffects(PrepareLightRows(EnsureReferenceLights(InitialRows)), 1), 0), 0), 0)),
    );
    AssignValues(Version ? Version.Values : Blank ? { ...BlankSceneValues } : {});
    AssignHidden(Version ? { ...Version.Hidden, camera: false } : { camera: false });
    Select("sun");
    SetWorkspace(null);
    StoreProjectCatalogue(Next);
    if (!StoreProjects(Next)) Toast("Browser storage is full; older project versions were not kept");
    OpenProjects(false);
    Toast(`${Target.Name} opened in Frontier${Blank ? " · blank scene: ground plane and clear sky" : ""}`);
  };
  const CreateProject = (Name, Location) => {
    const Token = "project-" + crypto.randomUUID();
    const Project = { Token, Name, Location: Location.replace(/\/?$/, "/") + Name + "/" + Name + ".projectspace", Created: Date.now(), Versions: [] };
    OpenProject(Project, -1, { ...ProjectCatalogue, Projects: [...ProjectCatalogue.Projects, Project] });
  };
  useEffect(() => {
    try {
      localStorage.setItem(
        StorageKey,
        JSON.stringify({
          Rows,
          ReferenceDesignRevision: 1,
          LightDesignRevision: 1,
          CameraEffectsRevision: 1,
          EnvironmentLightRevision: 1,
          VehicleRevision,
          ComponentRevision,
          Assets: AssetRecords,
          Selected,
          Values,
          Hidden,
          Collapsed,
          Settings,
          Name,
        }),
      );
    } catch {
      Notify(
        "Browser storage is full or unavailable. Export the scene to keep your material drafts.",
      );
    }
  }, [Rows, Selected, Values, Hidden, Collapsed, Settings, Name, AssetRecords]);
  useEffect(() => {
    const Key = (Event) => {
      if (
        Event.target.closest?.(
          'input, textarea, select, [contenteditable]:not([contenteditable="false"])',
        )
      )
        return;
      if (Construct || Shade || AssetsOpen || WindSubject) return;
      if (Event.key === "F3") {
        Event.preventDefault();
        SetDebug((Previous) => (Previous + (Event.shiftKey ? DebugViews.length - 1 : 1)) % DebugViews.length);
        // F3 also opens the diagnostics overlay, as before; its Close button hides the overlay without resetting the view.
        ShowDiagnostics(true);
      }
      if (Event.key === "F4") {
        Event.preventDefault();
        ToggleHiZ((Previous) => !Previous);
      }
      if (Event.key === "F5") {
        Event.preventDefault();
        ToggleAlias((Previous) => !Previous);
      }
      if (Event.key === "F6") {
        Event.preventDefault();
        SetPatchError((Previous) => (Previous >= 4 ? 0.5 : Previous * 2));
      }
      if (Event.key === "Escape") {
        SetWorkspace(null);
        SetDebug(0);
        ShowDiagnostics(false);
        ShowViewportSettings(false);
        ShowMenu(null);
        OpenConstruct(false);
        RenameRow(null);
      }
      if (
        (Event.ctrlKey || Event.metaKey) &&
        !Event.shiftKey &&
        !Event.altKey &&
        !Event.repeat &&
        Event.code === "KeyA"
      ) {
        Event.preventDefault();
        OpenConstruct(true);
      }
      if ((Event.ctrlKey || Event.metaKey) && Event.key.toLowerCase() === "f") {
        Event.preventDefault();
        SearchInput.current?.focus();
      }
      if (Event.key === "F2" && Selected !== "camera") RenameRow(Selected);
      if (Event.key === "`") ShowConsole((Previous) => !Previous);
      const Typing = /^(INPUT|TEXTAREA|SELECT)$/.test(document.activeElement?.tagName || "") || document.activeElement?.isContentEditable;
      if (!Typing && (Event.ctrlKey || Event.metaKey) && !Event.shiftKey && Event.code === "KeyZ" && LastDeleted) {
        Event.preventDefault();
        UndoDelete();
      }
    };
    window.addEventListener("keydown", Key);
    return () => window.removeEventListener("keydown", Key);
  }, [Selected, Construct, Shade, AssetsOpen, WindSubject, LastDeleted]);
  useEffect(() => {
    const Move = (Event) => {
      if (!Divider.current) return;
      const { Side, Width, X } = Divider.current;
      if (Side === "Left")
        ResizeLeft(
          Math.max(240, Math.min(innerWidth * 0.4, Width + Event.clientX - X)),
        );
      else
        ResizeRight(
          Math.max(
            300,
            Math.min(innerWidth - LeftWidth - 150, Width + X - Event.clientX),
          ),
        );
    };
    const Up = () => {
      Divider.current = null;
      document.body.classList.remove("resizing");
    };
    window.addEventListener("pointermove", Move);
    window.addEventListener("pointerup", Up);
    return () => {
      window.removeEventListener("pointermove", Move);
      window.removeEventListener("pointerup", Up);
    };
  }, [LeftWidth]);
  const SelectRow = (Id) => {
    AssignPicked([]);
    const Address = ComponentAddress(Id, Rows);
    Select(Address ? Address.Owner : Id);
    FocusComponent(Address ? { ...Address, Stamp: Date.now() } : null);
    ShowMenu(null);
  };
  // Opens one entry of an object's component stack, from anywhere (the Overview card, search, an older link).
  const OpenComponent = (Owner, Component) => {
    Select(Owner);
    FocusComponent({ Owner, Component, Stamp: Date.now() });
    ShowMenu(null);
  };
  // A component focus belongs to its object; selecting anything else drops it, so coming back opens the Overview again.
  useEffect(() => {
    if (ComponentFocus && ComponentFocus.Owner !== Selected) FocusComponent(null);
  }, [Selected]);
  const PatchRow = (Id, Patch) => AssignRows((Previous) => Previous.map((Row) => (Row.Id === Id ? { ...Row, ...Patch } : Row)));
  const Workspace = WorkspaceState && WorkspaceState.Id === Selected ? WorkspaceState : null;
  // Follow-up 62: the Controlled Vehicle's components are inspected in its Component Viewer panel, not here. Its inspector opens the
  // panel; choosing one of its components anywhere (outliner, search, a link, ?inspect=) selects the vehicle and opens the panel on it.
  const [VehiclePanelFocus, FocusVehiclePanel] = useState(null);
  const OpenVehiclePanel = (Id = VehicleOwner) => FocusVehiclePanel({ Id, Stamp: Date.now() });
  useEffect(() => {
    if (Subject?.Panel !== VehiclePanel || Subject.Parent !== VehicleOwner) return;
    OpenVehiclePanel(Subject.Id);
    Select(VehicleOwner);
  }, [Selected]);
  const AddVehicleComponent = (Row) => InsertUnder(VehicleOwner, [Row]);
  const RemoveVehicleComponent = (Id) => {
    AssignRows((Previous) => Previous.filter((Row) => Row.Id !== Id));
    AssignValues((Previous) => {
      const { [Id]: Gone, ...Kept } = Previous;
      return Kept;
    });
  };
  // The panel's Details: the native inspector of the component chosen in the panel, editing that component's own values.
  const VehicleDetails = (Row, PanelSelect) => (
    <Inspector
      Subject={Row}
      Values={Values[Row.Id] || {}}
      Change={(Key, Amount) => AssignValues((Previous) => ({ ...Previous, [Row.Id]: { ...Previous[Row.Id], [Key]: Amount } }))}
      Hidden={Hidden[Row.Id]}
      ToggleHidden={() => ToggleHidden(Row.Id)}
      OpenShader={() => {}}
      OpenFracture={() => {}}
      OpenWind={() => {}}
      Scene={{ Rows, Values, SelectRow: PanelSelect, AddRows: InsertUnder, PatchRow, OpenComponent, Air: AirOf(Values), Sun: ReaderOf(Values, "sun") }}
    />
  );
  const OpenWorkspace = (Tool) => {
    SetRunMode("EDIT");
    SetWorkspace((Previous) => ({ ...(Previous?.Id === Selected ? Previous : {}), Id: Selected, Tool, Selection: [] }));
  };
  const SceneInventory = useMemo(
    () => FolderInventory(Rows, null, Hidden),
    [Rows, Hidden],
  );
  const HasChildren = (Id) => SceneInventory.Children.has(Id);
  const DisplayRows = () => {
    const Result = [];
    const Seen = new Set(),
      Stack = [...(SceneInventory.Children.get(null) || [])]
        .reverse()
        .map((Row) => ({ Row, Depth: 0 }));
    while (Stack.length) {
      const { Row, Depth } = Stack.pop();
      if (Seen.has(Row.Id)) continue;
      Seen.add(Row.Id);
      const Match =
        (!Query || Row.Name.toLowerCase().includes(Query.toLowerCase())) &&
        (!Filters.length ||
          Filters.some((Filter) =>
            Filter === "Camera"
              ? Row.Panel === "camera" ||
                Row.Panel === "post" ||
                Row.Panel === "flare" ||
                IsEffectPanel(Row.Panel)
              : Filter === "Lights"
                ? ["sun", "light", EnvironmentPanel].includes(Row.Panel)
                : Filter === "Geometry"
                  ? Row.Panel === "geometry"
                  : Filter === "Bodies"
                    ? Row.Panel === "moon"
                    : [
                        "atmosphere",
                        "clouds",
                        "local-cloud",
                        "stars",
                        "height-fog",
                        "aerial-fog",
                        "local-fog",
                        "wind",
                        "rainbow",
                        "precipitation",
                      ].includes(Row.Panel),
          ));
      if (Match) Result.push({ ...Row, Depth });
      if (!Collapsed[Row.Id] || Query || Filters.length) {
        const Children = SceneInventory.Children.get(Row.Id) || [];
        for (let I = Children.length - 1; I >= 0; I--)
          Stack.push({ Row: Children[I], Depth: Depth + 1 });
      }
    }
    return Result;
  };
  const SaveFile = () => {
    const BlobContent = new Blob(
      [
        JSON.stringify(
          {
            Format: "Frontier HTML UI study",
            Rows,
            ReferenceDesignRevision: 1,
            LightDesignRevision: 1,
            CameraEffectsRevision: 1,
            EnvironmentLightRevision: 1,
            VehicleRevision,
            ComponentRevision,
            Collapsed,
            Assets: AssetRecords,
            Values,
            Hidden,
            Settings,
            Name,
          },
          null,
          2,
        ),
      ],
      { type: "application/json" },
    );
    const Url = URL.createObjectURL(BlobContent),
      Link = document.createElement("a");
    Link.href = Url;
    Link.download = "ProjectZero-UI.json";
    Link.click();
    setTimeout(() => URL.revokeObjectURL(Url), 1000);
    ShowMenu(null);
  };
  const RestoreFile = async (Event) => {
    try {
      const Loaded = JSON.parse(await Event.target.files[0].text());
      if (
        Loaded.Format !== "Frontier HTML UI study" ||
        !Array.isArray(Loaded.Rows) ||
        Loaded.Rows.length > 4096
      )
        throw Error();
      if (
        Loaded.Rows.some(
          (Row) =>
            typeof Row.Id !== "string" ||
            typeof Row.Name !== "string" ||
            !Panels[Row.Panel],
        )
      )
        throw Error();
      AssignRows(
        EnsureGodRays(EnsureObjectComponents(EnsureVehicleFolder(EnsureEnvironmentLight(
          ArrangeCameraEffects(
            PrepareLightRows(
              Loaded.ReferenceDesignRevision === 1
                ? Loaded.Rows
                : EnsureReferenceLights(Loaded.Rows),
              Loaded.LightDesignRevision,
            ),
            Loaded.CameraEffectsRevision,
          ),
          Loaded.EnvironmentLightRevision,
        ), Loaded.VehicleRevision), Loaded.ComponentRevision)),
      );
      Collapse(ConsolidateAtmosphereFlags(Loaded.Collapsed, Loaded.Rows));
      const ImportedValues = NormalizeHeightFogValues(
        ConsolidateAtmosphereValues(Loaded.Values, Loaded.Rows),
      );
      for (const Row of Loaded.Rows)
        if (Row.Panel === "geometry")
          ImportedValues[Row.Id] = {
            ...ImportedValues[Row.Id],
            Fracture: NormalizeFracture({
              ...ImportedValues[Row.Id]?.Fracture,
              EditedAt: Date.now(),
            }),
          };
      AssignValues(ImportedValues);
      StoreAssets(RestoreAssets(Loaded.Assets));
      AssignHidden({
        ...ConsolidateAtmosphereFlags(Loaded.Hidden, Loaded.Rows),
        camera: false,
      });
      ChangeSettings({ ...DefaultSettings, ...Loaded.Settings });
      RenameProject(Loaded.Name || "Project-Zero");
      Select(
        Loaded.Rows[0]?.Id === "sky" && Loaded.Rows[0]?.Panel === "atmosphere"
          ? "atmosphere"
          : Loaded.Rows[0]?.Id || "camera",
      );
      Toast("HTML UI state imported");
    } catch {
      Toast("Not a valid HTML UI study file");
    }
    Event.target.value = "";
  };
  // Adds rows under an owner, after the owner's existing children.
  const InsertUnder = (OwnerId, Added) =>
    AssignRows((Previous) => {
      const Fresh = Added.filter((Row) => !Previous.some((Each) => Each.Id === Row.Id));
      let At = Previous.findIndex((Row) => Row.Id === OwnerId);
      if (At < 0 || !Fresh.length) return Previous;
      while (At + 1 < Previous.length && Previous[At + 1].Parent === OwnerId) At++;
      return [...Previous.slice(0, At + 1), ...Fresh, ...Previous.slice(At + 1)];
    });
  const AddRow = (
    Template,
    Properties = Values[Template.Id] || {},
    Visible = true,
  ) => {
    // §5: at most eight local cloud volumes per scene.
    if (Template.Panel === "local-cloud" && Rows.filter((Row) => Row.Panel === "local-cloud").length >= MaximumLocalClouds) {
      Toast(`A scene holds at most ${MaximumLocalClouds} local clouds`);
      return;
    }
    if (IsEditorCamera(Template))
      Template = {
        ...Template,
        Name: "Main Camera",
        Description:
          "Scene camera · separate from the editor navigation camera",
      };
    const Id = Template.Panel + "-" + crypto.randomUUID();
    const Names = new Set(Rows.map((Subject) => Subject.Name));
    const Stem =
      Template.Name.replace(/[\x00-\x1f\x7f]/g, "").trim() || "Untitled";
    let Name = Stem,
      Suffix = 2;
    while (Names.has(Name)) Name = Stem + " " + Suffix++;
    const Placed = {
      ...Template,
      Id,
      Name,
      Preview: true,
      Parent: Rows.some((Subject) => Subject.Id === Template.Parent)
        ? Template.Parent
        : null,
    };
    // A new object gets its own components.
    AssignRows((Previous) => [
      ...Previous,
      Placed,
      ...(OwnerKind(Placed) ? ComponentRowsFor(Placed) : []),
    ]);
    AssignValues((Previous) => ({
      ...Previous,
      [Id]: {
        ...Properties,
        ...(Properties.Fracture
          ? { Fracture: { ...Properties.Fracture, Baked: undefined } }
          : {}),
      },
    }));
    // Show a new placement even when the destination collection was hidden.
    AssignHidden((Previous) => {
      const Next = { ...Previous, [Id]: !Visible };
      let Owner = Rows.find((Subject) => Subject.Id === Template.Parent);
      const Seen = new Set();
      while (Owner && !Seen.has(Owner.Id)) {
        Seen.add(Owner.Id);
        Next[Owner.Id] = false;
        Owner = Rows.find((Subject) => Subject.Id === Owner.Parent);
      }
      return Next;
    });
    Select(Id);
    OpenConstruct(false);
    ShowMenu(null);
    Expand(false);
    Search("");
    Filter([]);
    Collapse({});
    Toast("Added " + Name + " · analytical HTML preview");
    return Id;
  };
  const TabStrip = (Side) => (
    <div
      className="tab-strip"
      onDragOver={(Event) => Event.preventDefault()}
      onDrop={(Event) => {
        Event.preventDefault();
        try {
          const Transfer = JSON.parse(
            Event.dataTransfer.getData("application/frontier-tab"),
          );
          AssignTabs((Previous) => {
            const Next = {};
            Object.keys(Previous).forEach(
              (Key) =>
                (Next[Key] = Previous[Key].filter(
                  (Tab) => Tab !== Transfer.Tab,
                )),
            );
            Next[Side].push(Transfer.Tab);
            return Next;
          });
          Activate((Previous) => ({
            ...Previous,
            [Transfer.Side]:
              PaneTabs[Transfer.Side].find((Tab) => Tab !== Transfer.Tab) || "",
            [Side]: Transfer.Tab,
          }));
        } catch {}
      }}
    >
      {PaneTabs[Side].map((Tab) => (
        <div
          className={"document-tab " + (Active[Side] === Tab ? "active" : "")}
          key={Tab}
          draggable
          onDragStart={(Event) =>
            Event.dataTransfer.setData(
              "application/frontier-tab",
              JSON.stringify({ Tab, Side }),
            )
          }
          onClick={() => Activate((Previous) => ({ ...Previous, [Side]: Tab }))}
          onDoubleClick={() => {
            if (Tab === "Inspector") Expand((Previous) => !Previous);
          }}
        >
          <span>{Tab}</span>
          <button
            aria-label={"Close " + Tab + " tab"}
            onClick={(Event) => {
              Event.stopPropagation();
              AssignTabs((Previous) => ({
                ...Previous,
                [Side]: Previous[Side].filter((Item) => Item !== Tab),
              }));
              Activate((Previous) => ({
                ...Previous,
                [Side]: PaneTabs[Side].find((Item) => Item !== Tab) || "",
              }));
            }}
          >
            <Glyph Name="close" Size={13} />
          </button>
        </div>
      ))}
      {Side === "Left" && (
        <button
          className="tab-add"
          aria-label="Add editor tab or construct"
          onClick={(Event) => {
            const Rect = Event.currentTarget.getBoundingClientRect();
            ShowMenu({ Type: "Tabs", X: Rect.left, Y: Rect.bottom + 4 });
          }}
        >
          <Glyph Name="plus" Size={14} />
        </button>
      )}
    </div>
  );
  const RestoreTab = (Tab) => {
    if (Tab === "ShaderEditor") {
      SelectShaderTarget(
        Subject.Panel === "geometry"
          ? Selected
          : Rows.find((Row) => Row.Panel === "geometry")?.Id,
      );
      DockShader("Centre");
      ShowMenu(null);
      return;
    }
    const Side =
      Tab === "Outliner" ? "Left" : Tab === "Inspector" ? "Right" : "Centre";
    AssignTabs((Previous) =>
      Object.fromEntries(
        Object.entries(Previous).map(([Key, Tabs]) => [
          Key,
          Key === Side
            ? [...new Set([...Tabs, Tab])]
            : Tabs.filter((Item) => Item !== Tab),
        ]),
      ),
    );
    Activate((Previous) =>
      Object.fromEntries(
        Object.entries(Previous).map(([Key, Label]) => [
          Key,
          Key === Side
            ? Tab
            : Label === Tab
              ? PaneTabs[Key].find((Item) => Item !== Tab) || ""
              : Label,
        ]),
      ),
    );
    Expand(false);
    ShowMenu(null);
  };
  // ─── Outliner multi-select and the operations on the chosen rows ───
  const Chosen = [Selected, ...Picked.filter((Id) => Id !== Selected && Rows.some((Row) => Row.Id === Id))];
  const Descendants = (Ids) => {
    const Found = new Set(Ids);
    let Grew = true;
    while (Grew) {
      Grew = false;
      for (const Row of Rows)
        if (Found.has(Row.Parent) && !Found.has(Row.Id)) {
          Found.add(Row.Id);
          Grew = true;
        }
    }
    return Found;
  };
  // Ctrl/Cmd click toggles a row in the selection (the clicked row becomes the one inspected); Shift click takes the visible rows
  // between the inspected row and the clicked one.
  const PickRow = (Id, Mode) => {
    ShowMenu(null);
    if (Mode === "range") {
      const Shown = DisplayRows().map((Row) => Row.Id);
      const [From, To] = [Shown.indexOf(Selected), Shown.indexOf(Id)];
      if (From < 0 || To < 0) return SelectRow(Id);
      AssignPicked(Shown.slice(Math.min(From, To), Math.max(From, To) + 1).filter((Each) => Each !== Selected));
      return;
    }
    if (Id === Selected) {
      if (!Picked.length) return;
      const [Next, ...Rest] = Chosen.slice(1);
      AssignPicked(Rest);
      Select(Next);
      return;
    }
    if (Chosen.includes(Id)) return AssignPicked(Picked.filter((Each) => Each !== Id));
    AssignPicked([...Chosen]);
    Select(Id);
  };
  const Deletable = (Row) => Row && !IsEditorCamera(Row) && (Row.Panel !== "group" || Row.Id.startsWith("group-"));
  const DeleteRows = (Ids) => {
    const Starting = Ids.map((Id) => Rows.find((Row) => Row.Id === Id)).filter(Deletable).map((Row) => Row.Id);
    if (!Starting.length) return;
    const Removed = Descendants(Starting);
    KeepDeleted({ Rows, Values, Hidden, Selected, Count: Starting.length });
    AssignRows((Previous) => Previous.filter((Row) => !Removed.has(Row.Id)));
    AssignValues((Previous) => Object.fromEntries(Object.entries(Previous).filter(([Id]) => !Removed.has(Id))));
    AssignPicked([]);
    // Land on the first deleted row's collection when it survives, else the Showcase.
    const Holder = Rows.find((Row) => Row.Id === Starting[0])?.Parent;
    if (Removed.has(Selected)) Select(Holder && !Removed.has(Holder) ? Holder : "showcase");
    ShowMenu(null);
    Toast(Starting.length === 1 ? `Deleted ${Rows.find((Row) => Row.Id === Starting[0]).Name} · Ctrl+Z to undo` : `Deleted ${Starting.length} objects · Ctrl+Z to undo`);
  };
  const UndoDelete = () => {
    if (!LastDeleted) return;
    AssignRows(LastDeleted.Rows);
    AssignValues(LastDeleted.Values);
    AssignHidden(LastDeleted.Hidden);
    Select(LastDeleted.Selected);
    KeepDeleted(null);
    ShowMenu(null);
    Toast(LastDeleted.Count === 1 ? "Restored the deleted object" : `Restored ${LastDeleted.Count} objects`);
  };
  const SetChosenHidden = (Hide) => {
    AssignHidden((Previous) => ({ ...Previous, ...Object.fromEntries(Chosen.filter((Id) => Id !== "camera").map((Id) => [Id, Hide])) }));
    ShowMenu(null);
  };
  // Isolate: hide every other object, keeping the chosen rows' collections (and what's inside the chosen ones) visible.
  const Isolate = () => {
    const Keep = Descendants(Chosen);
    for (const Id of Chosen) for (let Row = Rows.find((Each) => Each.Id === Id), Depth = 0; Row?.Parent && Depth < 64; Row = Rows.find((Each) => Each.Id === Row.Parent), Depth++) Keep.add(Row.Parent);
    AssignHidden(Object.fromEntries(Rows.filter((Row) => !Keep.has(Row.Id) && Row.Id !== "camera" && Row.Panel !== "group").map((Row) => [Row.Id, true])));
    ShowMenu(null);
    Toast("Isolated · Show all in the outliner header brings everything back");
  };
  // Group: a new collection where the first chosen row was, holding all the chosen rows (collections that are already holding
  // a chosen row keep it inside them).
  const GroupChosen = () => {
    const Moving = Chosen.map((Id) => Rows.find((Row) => Row.Id === Id)).filter((Row) => Row && !IsEditorCamera(Row) && Row.Panel !== ComponentPanel);
    if (!Moving.length) return;
    const Moved = new Set(Moving.map((Row) => Row.Id));
    const Top = Moving.filter((Row) => !Moving.some((Other) => Other !== Row && Descendants([Other.Id]).has(Row.Id)));
    const Names = new Set(Rows.map((Row) => Row.Name));
    let Name = "Collection", Suffix = 2;
    while (Names.has(Name)) Name = "Collection " + Suffix++;
    const Folder = { Id: "group-" + crypto.randomUUID(), Name, Panel: "group", Icon: "folder-generic", Parent: Top[0].Parent ?? null, Description: "Collection" };
    AssignRows((Previous) => {
      const At = Previous.findIndex((Row) => Row.Id === Top[0].Id);
      const Placed = Previous.map((Row) => (Top.some((Each) => Each.Id === Row.Id) ? { ...Row, Parent: Folder.Id } : Row));
      return [...Placed.slice(0, At), Folder, ...Placed.slice(At)];
    });
    AssignPicked([]);
    Select(Folder.Id);
    ShowMenu(null);
    Toast(`Grouped ${Moved.size} into ${Name}`);
  };
  const SelectChildren = () => {
    const All = [...Descendants(Chosen)].filter((Id) => !Chosen.includes(Id));
    AssignPicked([...Chosen.slice(1), ...All]);
    Collapse((Previous) => ({ ...Previous, ...Object.fromEntries(Chosen.map((Id) => [Id, false])) }));
    ShowMenu(null);
  };
  const CopyNames = () => {
    const Text = Chosen.map((Id) => Rows.find((Row) => Row.Id === Id)?.Name).filter(Boolean).join("\n");
    navigator.clipboard?.writeText(Text).catch(() => {});
    ShowMenu(null);
    Toast(Chosen.length === 1 ? "Copied the name" : `Copied ${Chosen.length} names`);
  };
  const RowMenu = (Event, Row) => {
    Event.preventDefault();
    // Right-clicking inside the multi-selection keeps it; anywhere else selects just that row.
    if (!Chosen.includes(Row.Id)) {
      AssignPicked([]);
      Select(Row.Id);
    }
    ShowMenu({
      Type: "Row",
      X: Math.min(innerWidth - 230, Event.clientX),
      Y: Math.min(innerHeight - 240, Event.clientY),
    });
  };
  const Outliner = () => (
    <>
      <div className="outliner-heading">
        <h1>Outliner</h1>
        <small>{ProjectCatalogue.Projects.find((Each) => Each.Token === ProjectCatalogue.Active)?.Name || "Default Project"} · {Rows.length} nodes</small>
        <button
          aria-label="Outliner menu"
          onClick={(Event) => {
            const Rect = Event.currentTarget.getBoundingClientRect();
            ShowMenu({ Type: "File", X: Rect.left, Y: Rect.bottom });
          }}
        >
          <Glyph Name="filter" Size={15} />
        </button>
      </div>
      <div className="stats">
        <button onClick={() => AssignHidden({})}>
          <span className="status-disc">
            <Glyph Name="check" Size={15} />
          </span>
          <small>Visible</small>
          <strong>{Rows.filter((Row) => !Hidden[Row.Id]).length}</strong>
        </button>
        <button
          onClick={() => {
            AssignHidden(Object.fromEntries(Rows.map((Row) => [Row.Id, true])));
          }}
        >
          <span className="muted-disc">△</span>
          <small>Hidden</small>
          <strong>{Rows.filter((Row) => Hidden[Row.Id]).length}</strong>
        </button>
      </div>
      <div className="outliner-search">
        <label>
          <Glyph Name="search" Size={15} />
          <input
            ref={SearchInput}
            aria-label="Search outliner"
            placeholder="Search   Ctrl+F"
            value={Query}
            onChange={(Event) => Search(Event.target.value)}
          />
        </label>
        <button
          aria-label="Filter outliner"
          onClick={(Event) => {
            const Rect = Event.currentTarget.getBoundingClientRect();
            ShowMenu({ Type: "Filter", X: Rect.left, Y: Rect.bottom + 4 });
          }}
        >
          <Glyph Name="filter" Size={13} />
          Filter
          <Glyph Name="chevron" Size={12} />
        </button>
      </div>
      {!!Filters.length && (
        <div className="filter-chips">
          {Filters.map((Label) => (
            <button
              key={Label}
              onClick={() =>
                Filter((Previous) => Previous.filter((Item) => Item !== Label))
              }
            >
              {Label} ×
            </button>
          ))}
        </div>
      )}
      <div className="outliner-rows" role="tree" aria-label="Scene outliner">
        {DisplayRows().map((Row) => (
          <div
            key={Row.Id}
            role="treeitem"
            data-outliner-row={Row.Id}
            aria-selected={Chosen.includes(Row.Id)}
            aria-expanded={HasChildren(Row.Id) ? !Collapsed[Row.Id] : undefined}
            tabIndex={0}
            className={
              "outliner-row " +
              (Selected === Row.Id ? "selected " : Chosen.includes(Row.Id) ? "picked " : "") +
              (Hidden[Row.Id] ? "hidden-row" : "")
            }
            style={{ "--depth": Math.min(3, Row.Depth) }}
            onClick={(Event) => (Event.ctrlKey || Event.metaKey ? PickRow(Row.Id, "toggle") : Event.shiftKey ? PickRow(Row.Id, "range") : SelectRow(Row.Id))}
            onContextMenu={(Event) => RowMenu(Event, Row)}
            onDoubleClick={() => {
              if (HasChildren(Row.Id))
                Collapse((Previous) => ({
                  ...Previous,
                  [Row.Id]: !Previous[Row.Id],
                }));
              else {
                SelectRow(Row.Id);
                Expand(true);
              }
            }}
            onKeyDown={(Event) => {
              if (Event.key === "Enter") SelectRow(Row.Id);
              if (Event.key === "F2" && !IsEditorCamera(Row)) RenameRow(Row.Id);
              if (Event.key === "Delete" && Rename !== Row.Id) DeleteRows(Chosen.includes(Row.Id) ? Chosen : [Row.Id]);
              if (Event.key === "ArrowRight")
                Collapse((Previous) => ({ ...Previous, [Row.Id]: false }));
              if (Event.key === "ArrowLeft")
                Collapse((Previous) => ({ ...Previous, [Row.Id]: true }));
            }}
          >
            <button
              className={
                "row-disclosure " + (!HasChildren(Row.Id) ? "empty" : "")
              }
              aria-label={"Expand " + Row.Name}
              onClick={(Event) => {
                Event.stopPropagation();
                Collapse((Previous) => ({
                  ...Previous,
                  [Row.Id]: !Previous[Row.Id],
                }));
              }}
              style={{
                transform: Collapsed[Row.Id] ? "rotate(-90deg)" : undefined,
              }}
            >
              <Glyph Name="chevron" Size={12} />
            </button>
            <Icon Name={OutlinerIcon(Row, Values)} Size={24} />
            {Rename === Row.Id && !IsEditorCamera(Row) ? (
              <input
                autoFocus
                aria-label="Rename object"
                value={Row.Name}
                onClick={(Event) => Event.stopPropagation()}
                onChange={(Event) =>
                  AssignRows((Previous) =>
                    Previous.map((Item) =>
                      Item.Id === Row.Id
                        ? { ...Item, Name: Event.target.value }
                        : Item,
                    ),
                  )
                }
                onKeyDown={(Event) => {
                  Event.stopPropagation();
                  if (Event.key === "Enter" || Event.key === "Escape")
                    RenameRow(null);
                }}
                onBlur={() => RenameRow(null)}
              />
            ) : (
              <div className="row-identity">
                <span className="row-name">{Row.Name}</span>
                <small title={OutlinerMetadata(Row, Values[Row.Panel === ComponentPanel ? Row.Parent : Row.Id], Rows)}>
                  {OutlinerMetadata(Row, Values[Row.Panel === ComponentPanel ? Row.Parent : Row.Id], Rows)}
                </small>
              </div>
            )}
            {Row.Panel !== "group" && (
              <span className="row-status">
                <Glyph Name="check" Size={11} />
              </span>
            )}
            <button
              className="visibility"
              aria-label={"Toggle " + Row.Name + " visibility"}
              disabled={IsEditorCamera(Row)}
              title={
                IsEditorCamera(Row) ? "Permanent editor camera" : undefined
              }
              onClick={(Event) => {
                Event.stopPropagation();
                ToggleHidden(Row.Id);
              }}
            >
              <Glyph Name="eye" Size={13} />
            </button>
          </div>
        ))}
        {DisplayRows().length === 0 && (
          <p className="empty-results">No matching objects</p>
        )}
      </div>
      <footer className="outliner-footer">
        <span>
          <small>REALTIME</small>
          <b>
            — <em>fps</em>
          </b>
        </span>
        <span>
          <small>QUALITY</small>
          <b>
            {
              ["Minimal", "Economy", "Standard", "Ultra", "Reference"][
                Settings.Quality
              ]
            }
          </b>
        </span>
        <span>
          <small>SUN</small>
          <b>5.2°</b>
        </span>
        <span>
          <small>MOONS</small>
          <b>1/{MoonSlotCount}</b>
        </span>
        <span>
          <small>CAM</small>
          <b>0, 2, 0</b>
        </span>
      </footer>
    </>
  );
  const Viewport = () => (
    <>
      <div
        className="viewport-toolbar"
        role="toolbar"
        aria-label="Viewport tools"
      >
        <div className="viewport-heading-row">
          <div className="viewport-identity">
            <Icon Name="folder-scene" Size={24} />
            <div>
              <strong>Scene</strong>
              <span>
                {Rows.filter((Entry) => Entry.Preview).length} constructed
              </span>
            </div>
            <span
              className="viewport-preview-badge"
              title="Analytical HTML preview; the native renderer is not connected"
            >
              PREVIEW
            </span>
          </div>
          <div className="viewport-modes" role="group" aria-label="Editor mode">
            {[
              ["EDIT", "edit", "Edit"],
              ["SIM", "simulate", "Sim"],
              ["PLAY", "play", "Play"],
              ["PAUSE", "pause", "Pause"],
            ].map(([Mode, Symbol, Label]) => (
              <button
                key={Mode}
                aria-label={Label + " mode"}
                aria-pressed={RunMode === Mode}
                className={RunMode === Mode ? "active" : ""}
                title={
                  Mode === "EDIT"
                    ? "Edit the HTML scene"
                    : "UI mode only — native simulation is not connected"
                }
                onClick={() => {
                  SetRunMode(Mode);
                  if (Mode !== "EDIT")
                    Toast(
                      Label +
                        " selected · UI preview only; no native simulation runs",
                    );
                }}
              >
                <ActionIcon Name={Symbol} Size={13} />
                <span>{Label}</span>
              </button>
            ))}
          </div>
        </div>
        <div className="viewport-action-row">
          <button
            className="viewport-construct"
            aria-label="Construct"
            title="Construct an entity · Ctrl+A"
            onClick={() => OpenConstruct(true)}
          >
            <ActionIcon Name="construct" Size={18} />
            <span>Construct</span>
            <kbd>Ctrl+A</kbd>
          </button>
          <button
            className="viewport-icon-button viewport-projects"
            aria-label="Project Browser"
            title={"Project Browser · " + (ProjectCatalogue.Projects.find((Each) => Each.Token === ProjectCatalogue.Active)?.Name || "Default Project")}
            onClick={() => OpenProjects(true)}
          >
            <Icon Name="folder-scene" Size={18} />
          </button>
          <button
            className="viewport-icon-button viewport-component-viewer"
            aria-label="Component Viewer"
            aria-pressed={ComponentViewerOpen}
            title="Component Viewer · inspect, compile and bake components"
            onClick={() => OpenComponentViewer((Previous) => !Previous)}
          >
            <Icon Name="component-viewer" Size={18} />
          </button>
          <button
            className="viewport-icon-button"
            aria-label="Frame selected"
            title={
              Subject.Preview
                ? "Frame selected marker"
                : "Select a constructed marker to frame it"
            }
            disabled={
              !Subject.Preview || !PreviewVisible(Subject, Rows, Hidden)
            }
            onClick={() => {
              const Marker = [
                ...document.querySelectorAll("[data-preview-id]"),
              ].find((Entry) => Entry.dataset.previewId === Selected);
              Marker?.scrollIntoView({
                block: "center",
                inline: "center",
                behavior: "smooth",
              });
            }}
          >
            <ActionIcon Name="focus" Size={17} />
          </button>
          <span className="viewport-tool-divider" />
          <label
            className="viewport-projection"
            title="Projection preference only; the analytical preview stays 2D"
          >
            <Icon Name="camera" Size={16} />
            <select
              aria-label="Viewport projection"
              value={Projection}
              onChange={(Event) => SetProjection(Event.target.value)}
            >
              {[
                ["PERSP", "Perspective"],
                ["ORTHO", "Orthographic"],
                ["TOP", "Top"],
                ["FRONT", "Front"],
                ["RIGHT", "Right"],
              ].map(([Key, Label]) => (
                <option key={Key} value={Key}>
                  {Label}
                </option>
              ))}
            </select>
          </label>
          <div className="viewport-secondary-tools">
            <button
              className="viewport-icon-button viewport-view-switch"
              aria-label="3D view"
              aria-pressed={Settings.Viewport !== "markers"}
              data-viewport-switch={Settings.Viewport === "markers" ? "markers" : "3d"}
              title={Settings.Viewport === "markers" ? "Show the 3D view" : "Show the marker view (checkerboard)"}
              onClick={() => Assign("Viewport", Settings.Viewport === "markers" ? "3d" : "markers")}
            >
              3D
            </button>
            <button
              className="viewport-icon-button"
              aria-label="Split viewport"
              aria-pressed={SplitView}
              title={SplitView ? "Return to single view" : "Split viewport"}
              onClick={() => SetSplitView((Previous) => !Previous)}
            >
              <ActionIcon Name="split" Size={17} />
            </button>
            <button
              className="viewport-icon-button"
              aria-label="Viewport diagnostics"
              aria-pressed={Diagnostics}
              title="Diagnostics overlay · Timing, Scene, Render, Profiler"
              onClick={() => ShowDiagnostics((Previous) => !Previous)}
            >
              <ActionIcon Name="diagnostics" Size={16} />
            </button>
            <button
              className="viewport-icon-button"
              aria-label="Viewport settings"
              aria-expanded={ViewportSettings}
              title="Viewport settings and debug views"
              onClick={() => ShowViewportSettings(!ViewportSettings)}
            >
              <ActionIcon Name="settings" Size={17} />
            </button>
          </div>
        </div>
      </div>
      {ViewportSettings && (
        <section
          className="viewport-settings"
          role="dialog"
          aria-label="Viewport settings"
          onKeyDown={(Event) => {
            if (Event.key === "Escape") {
              Event.stopPropagation();
              ShowViewportSettings(false);
            }
          }}
        >
          <header>
            Viewport settings
            <button
              aria-label="Close viewport settings"
              onClick={() => ShowViewportSettings(false)}
            >
              ×
            </button>
          </header>
          <ViewportRenderHeader
            Render={ResolveRenderSettings(Values.camera?.[RenderSettingsKey])}
            AssignRender={(Patch) =>
              AssignValues((Previous) => ({
                ...Previous,
                camera: {
                  ...Previous.camera,
                  [RenderSettingsKey]: { ...ResolveRenderSettings(Previous.camera?.[RenderSettingsKey]), ...Patch },
                },
              }))
            }
            Settings={Settings}
            AssignSetting={Assign}
            Hidden={Hidden}
            SetVisible={(Id, On) => AssignHidden((Previous) => ({ ...Previous, [Id]: !On }))}
            CloudShadows={Values.clouds?.["Shadow Enabled"] ?? true}
            SetCloudShadows={(On) => AssignValues((Previous) => ({ ...Previous, clouds: { ...Previous.clouds, "Shadow Enabled": On } }))}
            SplitView={SplitView}
            SetSplitView={SetSplitView}
          />
          <label>
            Debug view
            <select
              autoFocus
              aria-label="Viewport debug view"
              value={Debug}
              onChange={(Event) => SetDebug(+Event.target.value)}
            >
              {DebugViews.map((View, Index) => (
                <option key={View.Name} value={Index} title={View.Shows}>
                  {DebugViewLabel(View)}
                </option>
              ))}
            </select>
          </label>
          <label className="viewport-setting-toggle" title="📝 Not in the Project Zero specification · kept for you to decide">
            📝 HiZ overlay
            <input
              type="checkbox"
              aria-label="HiZ overlay"
              checked={HiZ}
              onChange={(Event) => ToggleHiZ(Event.target.checked)}
            />
          </label>
          <label className="viewport-setting-toggle" title="📝 Not in the Project Zero specification · kept for you to decide">
            📝 Alias overlay
            <input
              type="checkbox"
              aria-label="Alias overlay"
              checked={Alias}
              onChange={(Event) => ToggleAlias(Event.target.checked)}
            />
          </label>
          <label title="📝 Not in the Project Zero specification · kept for you to decide">
            📝 Patch error budget
            <select
              aria-label="Patch error budget"
              value={PatchError}
              onChange={(Event) => SetPatchError(+Event.target.value)}
            >
              {[0.5, 1, 2, 4].map((Value) => (
                <option key={Value} value={Value}>
                  {Value} px
                </option>
              ))}
            </select>
          </label>
          <p>
            F3 / Shift+F3 cycle debug views (00–16, then the earlier 📝 views).
            HTML selections only; no native render buffers are connected.
          </p>
        </section>
      )}
      <div
        className={"scene-image " + (SplitView ? "split-view" : "")}
        data-viewport={Settings.Viewport === "markers" ? "markers" : "3d"}
        title={Settings.Viewport === "markers" ? "Checkerboard authoring preview. Symbols represent constructed entities, not engine rendering." : undefined}
      >
        {Subject?.Panel === "geometry" && Subject?.Parent === "showcase" && (
          <WorkspaceRail Active={Workspace?.Tool || null} Open={OpenWorkspace} Leave={() => SetWorkspace(null)} />
        )}
        {Workspace ? (
          <WorkspaceView
            Subject={Subject}
            Values={Values[Selected] || {}}
            Change={Change}
            Workspace={Workspace}
            SetWorkspace={SetWorkspace}
          />
        ) : Settings.Viewport === "markers" ? (
          <MeasuredViewport
            Rows={Rows}
            Hidden={Hidden}
            Selected={Selected}
            Select={SelectRow}
          />
        ) : (
          <Viewport3D
            Rows={Rows}
            Values={Values}
            Hidden={Hidden}
            Selected={Selected}
            Picked={Picked}
            FrameToken={FrameToken}
            Select={(Id, { Add } = {}) => (Add ? PickRow(Id, "toggle") : SelectRow(Id))}
            Air={AirOf(Values)}
            Sun={ReaderOf(Values, "sun")}
            Stars={ReaderOf(Values, "stars")}
            Flare={ReaderOf(Values, "flare")}
            StarsOff={!!Hidden.stars || !Rows.some((Row) => Row.Id === "stars")}
            FlareOff={!!Hidden.flare || !Rows.some((Row) => Row.Id === "flare")}
          />
        )}
        {SplitView && !Workspace && (
          <MeasuredViewport
            Rows={Rows}
            Hidden={Hidden}
            Selected={Selected}
            Select={SelectRow}
            View={2}
          />
        )}
        {ComponentViewerOpen && <ComponentViewerPanel Close={() => OpenComponentViewer(false)} Icon={Icon} />}
        {ProjectsOpen && (
          <ProjectBrowser
            Catalogue={ProjectCatalogue}
            Open={(Project, Version) => OpenProject(Project, Version)}
            Create={CreateProject}
            OpenFile={(File_) => {
              if (/\.json$/i.test(File_.name) || File_.type === "application/json") {
                RestoreFile({ target: { files: [File_] } });
                OpenProjects(false);
              } else Toast(`${File_.name}: the native .projectspace decoder is not connected in the HTML editor`);
            }}
            OpenPath={(Path) => Toast(`${Path} recorded for the native hand-off; the HTML editor cannot read disk paths`)}
            Close={() => OpenProjects(false)}
          />
        )}
        {Diagnostics && (
          <DiagnosticsCard
            Rows={Rows}
            VisibleCount={SceneInventory.Visible}
            DebugName={DebugViewLabel(DebugViews[Debug])}
            DebugView={DebugViews[Debug]}
            RunMode={RunMode}
            HiZ={HiZ}
            Alias={Alias}
            PatchError={PatchError}
            Close={() => {
              ShowDiagnostics(false);
              document
                .querySelector('[aria-label="Viewport diagnostics"]')
                ?.focus();
            }}
          />
        )}
        {Settings.FPS && (
          <div className="fps-overlay">
            <BrowserFPS />
          </div>
        )}
        {Console && (
          <div className="console">
            <input
              aria-label="Command line"
              placeholder="find"
              onKeyDown={(Event) => {
                if (Event.key === "Enter") {
                  Search(Event.currentTarget.value.replace(/^find\s*/, ""));
                  Event.currentTarget.value = "";
                  Toast("Outliner search updated");
                }
              }}
            />
            <button onClick={() => ShowConsole(false)}>×</button>
          </div>
        )}
      </div>
      <footer
        className="viewport-footer"
        title="Browser-local analytical symbols, not native geometry or renderer telemetry"
      >
        HTML PREVIEW · CONSTRUCTED{" "}
        <b>{Rows.filter((Subject) => Subject.Preview).length}</b> ·{" "}
        {
          Rows.filter(
            (Subject) =>
              Subject.Preview && PreviewVisible(Subject, Rows, Hidden),
          ).length
        }{" "}
        visible · NO ENGINE CONNECTION
      </footer>
    </>
  );
  const ReferenceCards = (
    Slice = ["clouds", "local-cloud"].includes(Subject.Panel)
      ? "summary"
      : "all",
  ) => (
    <ReferencePanel
      Subject={Subject}
      Rows={Rows}
      Values={Values}
      Hidden={Hidden}
      Collapsed={Collapsed}
      Apply={ApplyReference}
      Slice={Slice}
    />
  );
  const PanelBody = (Tab) =>
    Tab === "Outliner" ? (
      Outliner()
    ) : Tab === "Viewport" ? (
      Viewport()
    ) : Tab === "ShaderEditor" ? (
      ShaderBody(true)
    ) : Tab === "Inspector" ? (
      <>
        <div className="inspector-scroll" key={Subject.Id}>
          {Subject.Panel === "group" ? (
            <FolderInspector
              Subject={Subject}
              Rows={Rows}
              Hidden={Hidden}
              Values={Values[Selected] || {}}
              Change={Change}
              Select={(Id) => {
                const Seen = new Set();
                let Row = SceneInventory.ById.get(Id);
                const Parents = [];
                while (Row?.Parent && !Seen.has(Row.Parent)) {
                  Seen.add(Row.Parent);
                  Parents.push(Row.Parent);
                  Row = SceneInventory.ById.get(Row.Parent);
                }
                Collapse((Previous) => ({
                  ...Previous,
                  ...Object.fromEntries(
                    Parents.map((Parent) => [Parent, false]),
                  ),
                }));
                Search("");
                Filter([]);
                SelectRow(Id);
              }}
              ToggleHidden={ToggleHidden}
            />
          ) : (
            <Inspector
              Subject={Subject}
              Values={Values[ValueOwner] || {}}
              Change={Change}
              Hidden={Hidden[Selected]}
              ToggleHidden={() => ToggleHidden(Selected)}
              OpenShader={() => OpenShader(Selected)}
              OpenFracture={() => OpenWorkspace("fracture")}
              Workspace={Workspace}
              OpenWorkspace={OpenWorkspace}
              SetWorkspace={SetWorkspace}
              Scene={{ Rows, SelectRow, AddRows: InsertUnder, PatchRow, OpenComponent, Focus: ComponentFocus, Air: AirOf(Values), Sun: ReaderOf(Values, "sun"), OpenComponentViewer: OpenVehiclePanel, Values }}
              OpenWind={() => {
                WindOpener.current = document.activeElement;
                EditWind(Selected);
              }}
              ReferenceDetailCards={
                Subject.Panel === "light" && !Subject.ReferenceOnly
                  ? ReferenceCards("details")
                  : null
              }
              CloudCoverageCards={
                ["clouds", "local-cloud"].includes(Subject.Panel)
                  ? ReferenceCards("coverage")
                  : null
              }
              ReferenceCards={
                ["sun", "wind", "clouds", "local-cloud", "light"].includes(
                  Subject.Panel,
                )
                  ? ReferenceCards(
                      Subject.Panel === "light" && !Subject.ReferenceOnly
                        ? "summary"
                        : undefined,
                    )
                  : null
              }
            />
          )}
          {![
            "sun",
            "height-fog",
            "wind",
            "clouds",
            "local-cloud",
            "light",
          ].includes(Subject.Panel) &&
            HasReferencePanel(Subject) &&
            ReferenceCards()}
        </div>
        <footer className="inspector-footer">
          <span>{Subject.Description}</span>
          <span>HTML preview</span>
        </footer>
      </>
    ) : (
      <div className="empty-dock">Choose a panel from the + tab menu</div>
    );
  const AssetsBlocking =
    AssetsOpen && AssetsStage === "full" && !DraggingColour;
  const ResolveFallbackColourSlot = () => {
    if (ShaderAsset) {
      return {
        Label: ShaderAsset.Name + " · Base colour",
        AssignHex: (Hex) =>
          StoreAssets((Previous) =>
            Previous.map((Asset) =>
              Asset.Id === ShaderAsset.Id
                ? {
                    ...Asset,
                    Material: ChangeChannel(
                      ResolveMaterial({ Material: Asset.Material }),
                      "colour",
                      { Source: "Fill", Fill: Hex },
                    ),
                  }
                : Asset,
            ),
          ),
      };
    }
    if (Subject?.Panel === "geometry") {
      return {
        Label: Subject.Name + " · Base colour",
        AssignHex: (Hex) =>
          AssignValues((Previous) => ({
            ...Previous,
            [Subject.Id]: {
              ...Previous[Subject.Id],
              Material: ChangeChannel(
                ResolveMaterial(Previous[Subject.Id] || {}),
                "colour",
                { Source: "Fill", Fill: Hex },
              ),
            },
          })),
      };
    }
    if (Subject?.Panel === "group") {
      return {
        Label: Subject.Name + " · Collection accent",
        AssignHex: (Hex) =>
          AssignValues((Previous) => ({
            ...Previous,
            [Subject.Id]: { ...Previous[Subject.Id], Tint: Hex },
          })),
      };
    }
    if (Subject?.Panel === "sun") {
      return {
        Label: Subject.Name + " · Sun Tint",
        AssignHex: (Hex) =>
          AssignValues((Previous) => ({
            ...Previous,
            [Subject.Id]: {
              ...Previous[Subject.Id],
              "Colour source": false,
              "Sun Tint": Hex,
            },
          })),
      };
    }
    const SheetFields = Panels[Subject?.Panel] || [];
    const PrimaryColourField = SheetFields.find(
      (Field) => Field.Control === "Colour" && !Field.ReadOnly,
    );
    if (Subject && PrimaryColourField) {
      return {
        Label: Subject.Name + " · " + PrimaryColourField.Label,
        AssignHex: (Hex) =>
          AssignValues((Previous) => ({
            ...Previous,
            [Subject.Id]: {
              ...Previous[Subject.Id],
              [PrimaryColourField.Label]: Hex,
            },
          })),
      };
    }
    if (Subject && HasReferencePanel(Subject)) {
      return {
        Label: Subject.Name + " · Inspector colour",
        AssignHex: (Hex) => {
          for (const Frame of document.querySelectorAll(
            "iframe.reference-inspector-frame",
          )) {
            Frame.contentWindow?.postMessage(
              { ReferenceApplyColour: true, Hex },
              "*",
            );
          }
        },
      };
    }
    const FirstGeometry = Rows.find((Row) => Row.Panel === "geometry");
    if (FirstGeometry) {
      return {
        Label: FirstGeometry.Name + " · Base colour",
        AssignHex: (Hex) =>
          AssignValues((Previous) => ({
            ...Previous,
            [FirstGeometry.Id]: {
              ...Previous[FirstGeometry.Id],
              Material: ChangeChannel(
                ResolveMaterial(Previous[FirstGeometry.Id] || {}),
                "colour",
                { Source: "Fill", Fill: Hex },
              ),
            },
          })),
      };
    }
    return null;
  };
  const ActiveSlot = GetActiveColourSlot();
  const FallbackSlot = ResolveFallbackColourSlot();
  const ActiveColourTargetLabel =
    ActiveSlot && (!ActiveSlot.Element || ActiveSlot.Element.isConnected)
      ? ActiveSlot.Label
      : FallbackSlot?.Label || FocusedColourLabel;
  const WorkspaceThemeTable = {
    OLED: { Strip: "#040405", Dock: "#0a0a0c", Tab: "#16161a", TabHover: "#222228", Text: "#f2f2f5", Muted: "#8d9099", Gap: "#1e1e22" },
    Dark: { Strip: "#0a0a0a", Dock: "#121212", Tab: "#26262c", TabHover: "#32323a", Text: "#f0f0f0", Muted: "#999999", Gap: "#262626" },
    Light: { Strip: "#dfe4ec", Dock: "#f6f7fa", Tab: "#e9edf3", TabHover: "#d6dde8", Text: "#15171c", Muted: "#5e6470", Gap: "#cdd4df" },
    Sepia: { Strip: "#e2d6c1", Dock: "#f5efe3", Tab: "#ebe0cd", TabHover: "#ded0b7", Text: "#2b2216", Muted: "#726149", Gap: "#d2c3aa" },
    Dracula: { Strip: "#191a23", Dock: "#22232f", Tab: "#2d2f3f", TabHover: "#3a3d52", Text: "#f8f8f2", Muted: "#9ea4c2", Gap: "#343746" },
    Nord: { Strip: "#1f242d", Dock: "#272c36", Tab: "#323946", TabHover: "#3d4656", Text: "#eceff4", Muted: "#9aa5b8", Gap: "#3b4252" },
    GitHub: { Strip: "#080b10", Dock: "#0d1117", Tab: "#181e26", TabHover: "#222a35", Text: "#e6edf3", Muted: "#8b949e", Gap: "#21262d" },
  };
  const ActiveWsTheme = WorkspaceThemeTable[Settings.Theme] || WorkspaceThemeTable.Dark;
  return (
    <>
      <main
        className={"workspace " + (Wide ? "inspector-workspace" : "")}
        inert={
          Construct || Shade || AssetsBlocking || WindSubject ? "" : undefined
        }
        style={{
          "--left": LeftWidth + "px",
          "--right": RightWidth + "px",
          "--ws-strip": ActiveWsTheme.Strip,
          "--ws-dock": ActiveWsTheme.Dock,
          "--ws-tab": ActiveWsTheme.Tab,
          "--ws-tab-hover": ActiveWsTheme.TabHover,
          "--ws-text": ActiveWsTheme.Text,
          "--ws-muted": ActiveWsTheme.Muted,
          "--ws-gap": ActiveWsTheme.Gap,
        }}
      >
        {["Left", "Centre", "Right"].map((Side) => (
          <section
            className={"dock " + Side.toLowerCase()}
            key={Side}
            aria-label={Side + " editor dock"}
          >
            {TabStrip(Side)}
            {PanelBody(
              PaneTabs[Side].includes(Active[Side])
                ? Active[Side]
                : PaneTabs[Side][0],
            )}
            {Side !== "Centre" && (
              <div
                className={"divider " + Side.toLowerCase()}
                role="separator"
                aria-orientation="vertical"
                aria-label={"Resize " + Side + " panel"}
                onPointerDown={(Event) => {
                  Divider.current = {
                    Side,
                    Width: Side === "Left" ? LeftWidth : RightWidth,
                    X: Event.clientX,
                  };
                  document.body.classList.add("resizing");
                }}
              />
            )}
          </section>
        ))}
      </main>
      {/* The Controlled Vehicle's Component Viewer panel (Follow-up 62) sits above every layout. */}
      {VehiclePanelFocus && (
        <VehicleComponentPanel
          Rows={Rows}
          Values={Values}
          Hidden={Hidden}
          Focus={VehiclePanelFocus}
          ToggleHidden={ToggleHidden}
          Close={() => FocusVehiclePanel(null)}
          Icon={Icon}
          Details={VehicleDetails}
          Add={AddVehicleComponent}
          Remove={RemoveVehicleComponent}
        />
      )}
      {ShaderFloating && (
        <div inert={Shade || AssetsBlocking || WindSubject ? "" : undefined}>
          <ShaderPanel
            Close={() => FloatShader(false)}
            Dock={DockShader}
            Children={ShaderBody()}
          />
        </div>
      )}
      <div inert={WindSubject ? "" : undefined}>
        <Notch
          Open={Shade}
          Toggle={ToggleShade}
          Activate={() => OpenAssets(false)}
          Settings={Settings}
          Assign={Assign}
          Name={Name}
        />
        <AssetPanel
          Open={AssetsOpen}
          Toggle={ToggleAssets}
          Activate={() => OpenShade(false)}
          Assets={AssetRecords}
          Store={StoreAssets}
          Subject={Subject}
          Values={Values[Selected] || {}}
          Targets={Rows.filter((Row) => Row.Panel === "geometry")}
          Apply={(Material, Id) =>
            AssignValues((Previous) => ({
              ...Previous,
              [Id]: { ...Previous[Id], Material },
            }))
          }
          EditMaterial={OpenShader}
          ApplyColour={(ColourEntry) =>
            ApplyToActiveColourSlot(ColourEntry, ResolveFallbackColourSlot())
          }
          ActiveColourTargetLabel={ActiveColourTargetLabel}
          StageChange={SetAssetsStage}
          OnColourDragChange={SetDraggingColour}
          Appearance={Settings}
        />
        {Wide && (
          <button
            className="return-viewport"
            onClick={() => Expand(false)}
            title="Return to three-column layout"
          >
            ← Viewport
          </button>
        )}
      </div>
      {Menu && (
        <>
          <div className="menu-dismiss" onPointerDown={() => ShowMenu(null)} />
          <div
            className="popup-menu"
            style={{
              left: Math.max(4, Math.min(innerWidth - 235, Menu.X)),
              top: Menu.Y,
            }}
            // A long menu near the bottom of the window moves up so all of it is on screen.
            ref={(Shown) => {
              if (!Shown) return;
              Shown.style.top = Menu.Y + "px";
              const Height = Shown.getBoundingClientRect().height;
              if (Menu.Y + Height > innerHeight - 6) Shown.style.top = Math.max(6, innerHeight - Height - 6) + "px";
            }}
            role="menu"
          >
            {Menu.Type === "Tabs" ? (
              <>
                <label>WORKSPACE</label>
                <button
                  onClick={() => {
                    ShowMenu(null);
                    ToggleAssets(true);
                  }}
                >
                  ContentBrowser
                </button>
                <button
                  onClick={() => {
                    ShowMenu(null);
                    if (Subject.Panel !== "geometry") {
                      Toast("Select geometry to open its fracture workspace.");
                      return;
                    }
                    OpenWorkspace("fracture");
                  }}
                >
                  Fracture workspace
                </button>
                {["Outliner", "Viewport", "Inspector", "ShaderEditor"].map(
                  (Tab) => (
                    <button key={Tab} onClick={() => RestoreTab(Tab)}>
                      {Tab}
                    </button>
                  ),
                )}
                <hr />
                <button
                  onClick={() => {
                    ShowMenu(null);
                    OpenConstruct(true);
                  }}
                >
                  Construct… <small>Ctrl+A</small>
                </button>
                <button
                  onClick={() => {
                    Expand(!Wide);
                    ShowMenu(null);
                  }}
                >
                  {Wide ? "Three-column editor" : "Inspector workspace"}
                </button>
              </>
            ) : Menu.Type === "Filter" ? (
              <>
                <label>FILTER BY</label>
                {["Lights", "Sky", "Bodies", "Geometry", "Camera"].map(
                  (Label) => (
                    <button
                      key={Label}
                      onClick={() =>
                        Filter((Previous) =>
                          Previous.includes(Label)
                            ? Previous.filter((Value) => Value !== Label)
                            : [...Previous, Label],
                        )
                      }
                    >
                      <i className={Filters.includes(Label) ? "green" : ""} />
                      {Label}
                      {Filters.includes(Label) && (
                        <Glyph Name="check" Size={14} />
                      )}
                    </button>
                  ),
                )}
                <hr />
                <button
                  onClick={() => {
                    Filter([]);
                    ShowMenu(null);
                  }}
                >
                  Clear filters
                </button>
              </>
            ) : Menu.Type === "File" ? (
              <>
                <button onClick={SaveFile}>Export HTML UI state…</button>
                <button
                  onClick={() => {
                    FileInput.current.click();
                    ShowMenu(null);
                  }}
                >
                  Import HTML UI state…
                </button>
                <hr />
                <button
                  onClick={() => {
                    AssignRows(
                      EnsureEnvironmentLight(
                        PrepareLightRows(EnsureReferenceLights(InitialRows), 1),
                      ),
                    );
                    AssignValues(
                      Object.fromEntries(
                        InitialRows.filter(
                          (Row) => Row.Panel === "geometry",
                        ).map((Row) => [
                          Row.Id,
                          {
                            Fracture: NormalizeFracture({
                              EditedAt: Date.now(),
                            }),
                          },
                        ]),
                      ),
                    );
                    AssignHidden({});
                    Collapse({});
                    Filter([]);
                    Search("");
                    Select("sun");
                    ResizeLeft(316);
                    ResizeRight(340);
                    ChangeSettings(DefaultSettings);
                    ShowMenu(null);
                  }}
                >
                  Restore native baseline
                </button>
                <button
                  onClick={() => {
                    ShowMenu(null);
                    ShowConsole(!Console);
                  }}
                >
                  Command line <small>`</small>
                </button>
              </>
            ) : (
              <>
                <label data-row-menu-count={Chosen.length}>{Chosen.length > 1 ? `${Chosen.length} selected` : Subject.Name}</label>
                {Chosen.length === 1 && (
                  <button
                    disabled={IsEditorCamera(Subject)}
                    onClick={() => {
                      if (IsEditorCamera(Subject)) return;
                      RenameRow(Selected);
                      ShowMenu(null);
                    }}
                  >
                    Rename <small>F2</small>
                  </button>
                )}
                {Chosen.some((Id) => Hidden[Id]) && <button onClick={() => SetChosenHidden(false)}>Show</button>}
                {Chosen.some((Id) => !Hidden[Id] && Id !== "camera") && <button onClick={() => SetChosenHidden(true)}>Hide</button>}
                <button onClick={Isolate}>Isolate</button>
                {Settings.Viewport !== "markers" && Chosen.some((Id) => Rows.find((Row) => Row.Id === Id)?.Panel === "geometry") && (
                  <button
                    onClick={() => {
                      AskFrame(Date.now());
                      ShowMenu(null);
                    }}
                  >
                    Focus in viewport
                  </button>
                )}
                {Chosen.length === 1 && (
                  <button
                    onClick={() => {
                      Expand(true);
                      ShowMenu(null);
                    }}
                  >
                    Inspect in workspace
                  </button>
                )}
                {Chosen.length === 1 && Subject.Panel === "geometry" && (
                  <button
                    onClick={() => {
                      Change("Fracture", {
                        ...NormalizeFracture(Values[Selected]?.Fracture),
                        Enabled: !Values[Selected]?.Fracture?.Enabled,
                      });
                      ShowMenu(null);
                    }}
                  >
                    {Values[Selected]?.Fracture?.Enabled
                      ? "Disable fracture"
                      : "Enable fracture"}
                  </button>
                )}
                <hr />
                {Chosen.some((Id) => Rows.some((Row) => Row.Parent === Id)) && <button onClick={SelectChildren}>Select children</button>}
                {Chosen.some((Id) => { const Row = Rows.find((Each) => Each.Id === Id); return Row && !IsEditorCamera(Row) && Row.Panel !== ComponentPanel; }) && (
                  <button onClick={GroupChosen}>{Chosen.length > 1 ? "Group into collection" : "Move into new collection"}</button>
                )}
                {Chosen.some((Id) => Rows.find((Row) => Row.Id === Id)?.Panel !== ComponentPanel) && (
                  <button
                    onClick={() => {
                      for (const Id of Chosen) {
                        const Row = Rows.find((Each) => Each.Id === Id);
                        if (Row && Row.Panel !== ComponentPanel) AddRow(Row);
                      }
                    }}
                  >
                    Duplicate{Chosen.length > 1 ? ` ${Chosen.length}` : ""}
                  </button>
                )}
                <button onClick={CopyNames}>Copy name{Chosen.length > 1 ? "s" : ""}</button>
                {LastDeleted && (
                  <button onClick={UndoDelete}>
                    Undo delete <small>Ctrl+Z</small>
                  </button>
                )}
                <hr />
                {(() => {
                  const Removable = Chosen.filter((Id) => Deletable(Rows.find((Row) => Row.Id === Id)));
                  const Why = IsEditorCamera(Subject) ? "The editor camera cannot be deleted" : Subject.Panel === "group" ? "Built-in collections cannot be deleted" : undefined;
                  return (
                    <button className="danger" data-row-delete="" disabled={!Removable.length} title={!Removable.length ? Why : undefined} onClick={() => DeleteRows(Removable)}>
                      Delete{Chosen.length > 1 ? ` ${Removable.length}` : ""} <small>Del</small>
                    </button>
                  );
                })()}
              </>
            )}
          </div>
        </>
      )}
      {WindSubject && (
        <WindEditor
          key={WindSubject.Id}
          Subject={WindSubject}
          Values={Values[WindSubject.Id] || {}}
          Fields={WindFields}
          Hidden={Hidden[WindSubject.Id]}
          SelectField={EditWind}
          NewField={() =>
            EditWind(
              AddRow(
                InitialRows.find((Row) => Row.Panel === "wind"),
                {},
              ),
            )
          }
          Rename={(Name) =>
            AssignRows((Previous) =>
              Previous.map((Row) =>
                Row.Id === WindSubject.Id ? { ...Row, Name } : Row,
              ),
            )
          }
          Change={(Key, Value) =>
            AssignValues((Previous) => ({
              ...Previous,
              [WindSubject.Id]: { ...Previous[WindSubject.Id], [Key]: Value },
            }))
          }
          Close={CloseWind}
        />
      )}
      {Construct && (
        <ConstructPanel
          Catalogue={[
            ...PrepareLightRows(InitialRows, 1),
            EnvironmentRow,
            ...ReferenceLights,
          ]
            .filter((Subject) => Subject.Panel !== "group" && Subject.Panel !== ComponentPanel)
            .map((Row) =>
              IsEditorCamera(Row)
                ? { ...Row, Name: "Main Camera", Description: "Scene camera" }
                : Row,
            )}
          Add={AddRow}
          Close={() => OpenConstruct(false)}
        />
      )}
      <input
        type="file"
        accept="application/json"
        hidden
        ref={FileInput}
        onChange={RestoreFile}
      />
      {Message && (
        <div className="toast" role="status">
          {Message}
        </div>
      )}
    </>
  );
}
StartThoughtLog();
createRoot(document.getElementById("Editor")).render(<App />);
window.ProjectZeroHtml = {
  Panels: Object.keys(Panels),
  NativeSource: "9864969",
  EngineConnected: false,
};
