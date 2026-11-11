import {
  Prefix as FracturePrefix,
  Publish as PublishFracture,
  Describe as DescribeFracture,
  Normalize as NormalizeFracture,
  OpenFracture,
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
import ShaderPanel from "./ShaderPanel.jsx";
import React, { useState, useEffect, useRef, useMemo } from "react";
import { createRoot } from "react-dom/client";
import { Inspector, Icon, Glyph, Panels } from "./Inspectors.jsx";
import Notch from "./Notch.jsx";
import ActionIcon from "./ActionIcon.jsx";
import ConstructPanel from "./ConstructPanel.jsx";
import CheckerViewport, { PreviewVisible } from "./CheckerViewport.jsx";
import "./Editor.css";
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
  ["flare", "Lens Flare", "flare", "lens-flare", "sun", "Sun optical effect"],
  ["moon", "Moons", "moon", "moon", "world", "Lunar bodies · four slots"],
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
};
const DebugViews = [
  "Off",
  "Depth",
  "Visibility ID",
  "Motion Vectors",
  "Cluster ID",
  "HiZ (level 3)",
  "Albedo",
  "Normal",
  "Roughness",
  "Metalness",
  "Shading Normal",
  "Reservoir M",
  "Reservoir W",
  "Reservoir Age",
  "Patch Tiles",
  "Tiles + Wireframe",
];
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
function App() {
  const [Debug, SetDebug] = useState(0),
    [HiZ, ToggleHiZ] = useState(true),
    [Alias, ToggleAlias] = useState(true),
    [PatchError, SetPatchError] = useState(1);
  const [Rows, StoreRows] = useState(() =>
      EnsureEditorCamera(
        PrepareLightRows(
          Saved.ReferenceDesignRevision === 1
            ? Saved.Rows || EnsureReferenceLights(InitialRows)
            : EnsureReferenceLights(Saved.Rows || InitialRows),
          Saved.LightDesignRevision,
        ),
      ),
    ),
    [Selected, Select] = useState(
      new URLSearchParams(location.search).get("inspect") ||
        Saved.Selected ||
        "sun",
    ),
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
    [ViewportSettings, ShowViewportSettings] = useState(false);
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
  const Change = (Key, Value) =>
    AssignValues((Previous) => ({
      ...Previous,
      [Selected]: {
        ...Previous[Selected],
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
  useEffect(() => {
    try {
      localStorage.setItem(
        StorageKey,
        JSON.stringify({
          Rows,
          ReferenceDesignRevision: 1,
          LightDesignRevision: 1,
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
        SetDebug((Previous) => (Previous + (Event.shiftKey ? 15 : 1)) % 16);
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
        SetDebug(0);
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
    };
    window.addEventListener("keydown", Key);
    return () => window.removeEventListener("keydown", Key);
  }, [Selected, Construct, Shade, AssetsOpen, WindSubject]);
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
    Select(Id);
    ShowMenu(null);
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
              ? Row.Panel === "camera"
              : Filter === "Lights"
                ? ["sun", "light", "flare"].includes(Row.Panel)
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
        PrepareLightRows(
          Loaded.ReferenceDesignRevision === 1
            ? Loaded.Rows
            : EnsureReferenceLights(Loaded.Rows),
          Loaded.LightDesignRevision,
        ),
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
  const AddRow = (
    Template,
    Properties = Values[Template.Id] || {},
    Visible = true,
  ) => {
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
    AssignRows((Previous) => [
      ...Previous,
      {
        ...Template,
        Id,
        Name,
        Preview: true,
        Parent: Rows.some((Subject) => Subject.Id === Template.Parent)
          ? Template.Parent
          : null,
      },
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
  const RowMenu = (Event, Row) => {
    Event.preventDefault();
    Select(Row.Id);
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
        <small>Showcase · {Rows.length} nodes</small>
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
            aria-selected={Selected === Row.Id}
            aria-expanded={HasChildren(Row.Id) ? !Collapsed[Row.Id] : undefined}
            tabIndex={0}
            className={
              "outliner-row " +
              (Selected === Row.Id ? "selected " : "") +
              (Hidden[Row.Id] ? "hidden-row" : "")
            }
            style={{ "--depth": Math.min(3, Row.Depth) }}
            onClick={() => SelectRow(Row.Id)}
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
            <Icon Name={Row.Icon} Size={24} />
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
                <small title={OutlinerMetadata(Row, Values[Row.Id], Rows)}>
                  {OutlinerMetadata(Row, Values[Row.Id], Rows)}
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
          <b>1/4</b>
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
              aria-pressed={Debug > 0}
              title="Diagnostics · F3"
              onClick={() => SetDebug((Previous) => (Previous ? 0 : 1))}
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
          <label>
            Debug view
            <select
              autoFocus
              aria-label="Viewport debug view"
              value={Debug}
              onChange={(Event) => SetDebug(+Event.target.value)}
            >
              {DebugViews.map((View, Index) => (
                <option key={View} value={Index}>
                  {View}
                </option>
              ))}
            </select>
          </label>
          <label className="viewport-setting-toggle">
            HiZ overlay
            <input
              type="checkbox"
              aria-label="HiZ overlay"
              checked={HiZ}
              onChange={(Event) => ToggleHiZ(Event.target.checked)}
            />
          </label>
          <label className="viewport-setting-toggle">
            Alias overlay
            <input
              type="checkbox"
              aria-label="Alias overlay"
              checked={Alias}
              onChange={(Event) => ToggleAlias(Event.target.checked)}
            />
          </label>
          <label>
            Patch error budget
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
            F3 / Shift+F3 cycle debug views. These are HTML diagnostic
            selections; no native render buffers are connected.
          </p>
        </section>
      )}
      <div
        className={"scene-image " + (SplitView ? "split-view" : "")}
        title="Checkerboard authoring preview. Symbols represent constructed entities, not engine rendering."
      >
        <CheckerViewport
          Rows={Rows}
          Hidden={Hidden}
          Selected={Selected}
          Select={SelectRow}
        />
        {SplitView && (
          <CheckerViewport
            Rows={Rows}
            Hidden={Hidden}
            Selected={Selected}
            Select={SelectRow}
            View={2}
          />
        )}
        {Debug > 0 && (
          <DiagnosticsCard
            Rows={Rows}
            VisibleCount={SceneInventory.Visible}
            DebugName={DebugViews[Debug]}
            HiZ={HiZ}
            Alias={Alias}
            PatchError={PatchError}
            Close={() => {
              SetDebug(0);
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
              Values={Values[Selected] || {}}
              Change={Change}
              Hidden={Hidden[Selected]}
              ToggleHidden={() => ToggleHidden(Selected)}
              OpenShader={() => OpenShader(Selected)}
              OpenFracture={() => {
                try {
                  OpenFracture(Subject, Values[Selected] || {});
                } catch {
                  Toast(
                    "Browser storage is unavailable; cannot open the object fracture editor.",
                  );
                }
              }}
              OpenWind={() => {
                WindOpener.current = document.activeElement;
                EditWind(Selected);
              }}
              OpenWindField={(Id) => {
                WindOpener.current = document.activeElement;
                EditWind(Id);
              }}
              WindFields={WindFields}
              AllValues={Values}
              AllHidden={Hidden}
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
  return (
    <>
      <main
        className={"workspace " + (Wide ? "inspector-workspace" : "")}
        inert={Construct || Shade || AssetsOpen || WindSubject ? "" : undefined}
        style={{ "--left": LeftWidth + "px", "--right": RightWidth + "px" }}
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
      {ShaderFloating && (
        <div inert={Shade || AssetsOpen || WindSubject ? "" : undefined}>
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
                  Asset Browser
                </button>
                <button
                  onClick={() => {
                    ShowMenu(null);
                    if (
                      Subject.Panel !== "geometry" ||
                      !Values[Selected]?.Fracture?.Enabled
                    ) {
                      Toast(
                        "Select geometry and enable Fracture in its inspector first.",
                      );
                      return;
                    }
                    try {
                      OpenFracture(Subject, Values[Selected] || {});
                    } catch {
                      Toast("Browser storage is unavailable.");
                    }
                  }}
                >
                  Selected object fracture…
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
                      PrepareLightRows(EnsureReferenceLights(InitialRows), 1),
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
                <label>{Subject.Name}</label>
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
                <button
                  disabled={IsEditorCamera(Subject)}
                  onClick={() => {
                    ToggleHidden(Selected);
                    ShowMenu(null);
                  }}
                >
                  {Hidden[Selected] ? "Show" : "Hide"}
                </button>
                <button
                  onClick={() => {
                    Expand(true);
                    ShowMenu(null);
                  }}
                >
                  Inspect in workspace
                </button>
                {Subject.Panel === "geometry" && (
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
                <button onClick={() => AddRow(Subject)}>Duplicate</button>
                {Subject.Panel !== "group" && (
                  <button
                    disabled={IsEditorCamera(Subject)}
                    title={
                      IsEditorCamera(Subject)
                        ? "The editor camera cannot be deleted"
                        : undefined
                    }
                    onClick={() => {
                      if (IsEditorCamera(Subject)) return;
                      AssignRows((Previous) => {
                        const Removed = new Set([Selected]);
                        let Changed = true;
                        while (Changed) {
                          Changed = false;
                          for (const Row of Previous)
                            if (
                              Removed.has(Row.Parent) &&
                              !Removed.has(Row.Id)
                            ) {
                              Removed.add(Row.Id);
                              Changed = true;
                            }
                        }
                        return Previous.filter((Row) => !Removed.has(Row.Id));
                      });
                      Select("showcase");
                      ShowMenu(null);
                    }}
                  >
                    Remove from HTML scene
                  </button>
                )}
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
          Catalogue={[...PrepareLightRows(InitialRows, 1), ...ReferenceLights]
            .filter((Subject) => Subject.Panel !== "group")
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
createRoot(document.getElementById("Editor")).render(<App />);
window.ProjectZeroHtml = {
  Panels: Object.keys(Panels),
  NativeSource: "9864969",
  EngineConnected: false,
};
