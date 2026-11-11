import { CloudEdit, CloudValue } from "./CloudSpecification.js";
import CloudDeckPanel from "./CloudDeckPanel.jsx";
import { FogBeamChamber } from "./FogPanel.jsx";
import HeightFogVisual from "./HeightFogVisual.jsx";
import FracturePanel from "./FracturePanel.jsx";
import AtmosphereLab, { AtmosphereProfile } from "./AtmosphereLab.jsx";
import {
  GraphContext,
  PropertyGraph,
  SunGraph,
  FogGraph,
  FogSpectrum,
  CloudSection,
  CloudBounds,
} from "./LiveGraph.jsx";
import FogShapePanel from "./FogShapePanel.jsx";
import { IsEditorCamera } from "./ScenePolicy.js";
import { WindInspector, WindBinding } from "./WindPanel.jsx";
import TransformPanel from "./TransformPanel.jsx";
import MaterialPanel from "./MaterialPanel.jsx";
import ActionIcon, { QuickSymbol } from "./ActionIcon.jsx";
import React, { useState, useEffect, useRef } from "react";
import { SunGizmo } from "../FrontierEditor/environment-graphics.jsx";
import { DepthOfField } from "../FrontierEditor/camera-graphics.jsx";
import { Iris, CloudCoverage } from "../FrontierEditor/property-graphics.jsx";
import {
  CelestialRotation,
  TwinkleSignal,
} from "../FrontierEditor/celestial-controls.jsx";
import { LunarSphere } from "../FrontierEditor/moon-controls.jsx";
import Native from "./NativePanels.json";
import { Solar } from "./Solar.mjs";
import { SensorField } from "./OpticalGraphic.jsx";

export const Panels = Native.Panels;
export function Glyph({ Name, Size = 18 }) {
  if (Name === "celestial-orbit")
    return (
      <svg
        width={Size}
        height={Size}
        viewBox="0 0 24 24"
        fill="none"
        stroke="currentColor"
        strokeWidth="1.5"
      >
        <path d="M19.071 19.071A10 10 0 1 1 20.91 7.46" />
        <circle cx="19" cy="5" r="2" />
        <circle cx="5" cy="19" r="2" />
        <circle cx="12" cy="12" r="3" />
      </svg>
    );
  const Aliases = {
    gear: "SettingsGear",
    sun: "SunIllumination",
    filter: "SlidersQuality",
    eye: "EyeVisible",
    back: "ChevronBack",
    arrow: "ChevronForward",
    chevron: "ChevronDown",
    close: "CloseCross",
  };
  const NativePath = window.NativeAssets?.Glyphs[Aliases[Name] || Name];
  if (NativePath)
    return (
      <svg
        width={Size}
        height={Size}
        viewBox="0 0 24 24"
        fill="none"
        stroke="currentColor"
        strokeWidth="1.5"
        strokeLinecap="round"
        strokeLinejoin="round"
        aria-hidden="true"
      >
        <path d={NativePath} />
      </svg>
    );
  return (
    <svg
      width={Size}
      height={Size}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.5"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      {Name === "close" ? (
        <path d="m7 7 10 10M17 7 7 17" />
      ) : Name === "plus" ? (
        <path d="M12 5v14M5 12h14" />
      ) : Name === "chevron" ? (
        <path d="m8 10 4 4 4-4" />
      ) : Name === "search" ? (
        <>
          <circle cx="10" cy="10" r="6" />
          <path d="m15 15 5 5" />
        </>
      ) : Name === "eye" ? (
        <>
          <path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12Z" />
          <circle cx="12" cy="12" r="3" />
        </>
      ) : Name === "power" ? (
        <>
          <path d="M12 3v9M7 5a8 8 0 1 0 10 0" />
        </>
      ) : Name === "arrow" ? (
        <path d="m9 5 7 7-7 7" />
      ) : Name === "back" ? (
        <path d="m15 5-7 7 7 7" />
      ) : Name === "gear" ? (
        <>
          <path d="m10 2 4 0 .7 3 2 .9 2.7-.8 2 3.5-2.2 2.1v2.6l2.2 2.2-2 3.5-2.7-.8-2 .9-.7 3h-4l-.7-3-2-.9-2.7.8-2-3.5L4.8 13v-2.6L2.6 8.3l2-3.5 2.7.8 2-.9Z" />
          <circle cx="12" cy="12" r="3" />
        </>
      ) : Name === "filter" ? (
        <>
          <path d="M4 6h16M4 12h16M4 18h16" />
          <circle cx="8" cy="6" r="2" fill="#171717" />
          <circle cx="16" cy="12" r="2" fill="#171717" />
          <circle cx="10" cy="18" r="2" fill="#171717" />
        </>
      ) : Name === "check" ? (
        <path d="m5 12 4 4L19 6" />
      ) : Name === "lock" ? (
        <>
          <rect x="6" y="10" width="12" height="11" rx="3" />
          <path d="M8 10V7a4 4 0 0 1 8 0v3" />
        </>
      ) : Name === "bake" ? (
        <>
          <path d="M12 5h7l2 2v12l-2 2H7l-2-2v-6M2 9h11L9 5m4 4-4 4M3 2v3M1.5 3.5h3" />
          <rect x="14" y="14" width="3" height="3" />
        </>
      ) : Name === "play" ? (
        <path d="m9 6 9 6-9 6Z" />
      ) : Name === "pause" ? (
        <path d="M8 6v12M16 6v12" />
      ) : (
        <>
          <circle cx="12" cy="12" r="4" />
          {Array.from({ length: 8 }, (_, Index) => (
            <path
              key={Index}
              transform={`rotate(${Index * 45} 12 12)`}
              d="M12 2v2"
            />
          ))}
        </>
      )}
    </svg>
  );
}
export function Icon({ Name, Size = 24 }) {
  if (Name === "outliner-precipitation")
    return (
      <span
        className="native-icon precipitation-symbol"
        data-native-icon={Name}
        style={{ color: "#91b7cd", display: "inline-flex" }}
      >
        <ActionIcon Name="rain" Size={Size} />
      </span>
    );
  const Source = window.NativeAssets?.Icons[Name];
  return Source ? (
    <img
      className="native-icon"
      width={Size}
      height={Size}
      src={Source}
      alt=""
      data-native-icon={Name}
    />
  ) : (
    <Glyph Name={Name} Size={Size} />
  );
}
export function Control({
  Field,
  Value,
  Change,
  Disabled = false,
  Compact = false,
}) {
  if (!Field) return null;
  const ReadOnly = Disabled || Field.ReadOnly;
  const Label = Field.Label;
  if (Field.Control === "Readout")
    return (
      <div className="readout">
        <span>{Label}</span>
        <output>{Value ?? Field.Default}</output>
      </div>
    );
  if (Field.Control === "Switch")
    return (
      <label className="switch-row">
        <span>{Label}</span>
        <button
          className={"toggle " + (Value ? "on" : "")}
          role="switch"
          aria-checked={!!Value}
          aria-label={Label}
          disabled={ReadOnly}
          onClick={() => Change(!Value)}
        >
          <i />
        </button>
      </label>
    );
  if (Field.Control === "Select")
    return (
      <label className="field">
        <span>{Label}</span>
        <select
          value={Value ?? 0}
          disabled={ReadOnly}
          aria-label={Label}
          onChange={(Event) => Change(+Event.target.value)}
        >
          {Field.Options.map((Option, Index) => (
            <option value={Index} key={Index}>
              {Option}
            </option>
          ))}
        </select>
      </label>
    );
  if (Field.Control === "Colour")
    return (
      <label className="colour-field">
        <span>{Label}</span>
        <input
          aria-label={Label}
          type="color"
          value={Value || "#ffffff"}
          disabled={ReadOnly}
          onChange={(Event) => Change(Event.target.value)}
        />
        <output>{(Value || "#ffffff").toUpperCase()}</output>
      </label>
    );
  if (Field.Control === "Axes")
    return (
      <div className="field">
        <span>{Label}</span>
        <div className="axes">
          {["X", "Y", "Z"].map((Axis, Index) => (
            <label key={Axis}>
              <b>{Axis}</b>
              <input
                type="number"
                aria-label={Label + " " + Axis}
                disabled={ReadOnly}
                value={Value?.[Index] ?? 0}
                onChange={(Event) =>
                  Change(
                    (Value || [0, 0, 0]).map((Number, Slot) =>
                      Slot === Index ? +Event.target.value : Number,
                    ),
                  )
                }
              />
            </label>
          ))}
        </div>
      </div>
    );
  const Minimum = Field.Minimum ?? 0,
    Maximum = Field.Maximum ?? 100,
    Decimals = Field.Decimals ?? 2;
  const Safe = Number.isFinite(+Value) ? +Value : Field.Default;
  const Write = (Input) => {
    const Numeric = +Input;
    if (Number.isFinite(Numeric))
      Change(Math.min(Maximum, Math.max(Minimum, Numeric)));
  };
  return (
    <label
      className={"field " + (Compact ? "compact" : "")}
      data-native-property={Label}
    >
      <span>{Label}</span>
      <div className="slider-pill">
        <div className="split-value">
          <input
            aria-label={Label + " value"}
            type="number"
            min={Minimum}
            max={Maximum}
            step={10 ** -Decimals}
            disabled={ReadOnly}
            value={Number(Safe.toFixed(Decimals))}
            onChange={(Event) => Write(Event.target.value)}
          />
          <small>{Field.Unit}</small>
        </div>
        <input
          aria-label={Label}
          type="range"
          min={Minimum}
          max={Maximum}
          step={10 ** -Decimals}
          value={Safe}
          disabled={ReadOnly}
          style={{
            "--fill": `${(100 * (Safe - Minimum)) / (Maximum - Minimum)}%`,
          }}
          onChange={(Event) => Write(Event.target.value)}
        />
      </div>
    </label>
  );
}
export function Card({
  Title,
  Children,
  children,
  Height,
  Wide = false,
  Accent,
  Class = "",
  GraphHandled = false,
}) {
  const Mark = {
    "Sun direction": "celestial-orbit",
    "Sunlight intensity": "sun",
    Temperature: "SparklesAntiAliasing",
    "Daylight cycle": "sun",
    "Sun disc": "sun",
    "Sun lighting bake": "bake",
    "Sun disk bake": "bake",
    "Dynamic settings": "celestial-orbit",
  }[Title];
  return (
    <section
      className={"property-card " + (Wide ? "span-two " : "") + Class}
      style={{ minHeight: Height, "--accent": Accent || "#bababa" }}
      data-card={Title}
    >
      <h3>
        {Mark && <Glyph Name={Mark} Size={16} />}
        <span>{Title}</span>
      </h3>
      {children || Children}
      {!GraphHandled && (
        <PropertyGraph Children={children || Children} Title={Title} />
      )}
    </section>
  );
}
export function Tile({
  Label,
  On,
  Action,
  Disabled = false,
  IconName = "visible",
  Context = "",
  Large = false,
}) {
  return (
    <button
      className={"quick-tile " + (Large ? "large " : "") + (On ? "on" : "off")}
      disabled={Disabled}
      title={Disabled ? "Not connected in the HTML UI copy" : Label}
      onClick={Action}
      aria-pressed={Disabled ? undefined : !!On}
      aria-label={Label}
      data-quick-action={Label}
    >
      <span className="quick-icon-seat">
        <ActionIcon Name={QuickSymbol(Label, Context, IconName)} Size={26} />
      </span>
      <span className="quick-label">{Label}</span>
      <small className="quick-status">
        {Disabled ? "UNAVAILABLE" : On ? "ON" : "OFF"}
      </small>
    </button>
  );
}
function Metric({ Value, Unit, Caption }) {
  return (
    <>
      <div className="metric">
        {typeof Value === "string" || typeof Value === "number" ? (
          <>
            <span>{String(Value).split(".")[0]}</span>
            {String(Value).includes(".") && (
              <span className="metric-fraction">
                .{String(Value).split(".").slice(1).join(".")}
              </span>
            )}
          </>
        ) : (
          Value
        )}
        <small>{Unit}</small>
      </div>
      {Caption && <p>{Caption}</p>}
    </>
  );
}
export function EntityNotes({ Value = "", Change }) {
  const [Open, SetOpen] = useState(Boolean(Value));
  if (!Open)
    return (
      <button
        className="entity-notes-add"
        onClick={() => SetOpen(true)}
        aria-label="Add notes"
      >
        <Glyph Name="plus" Size={13} />
        Add notes
      </button>
    );
  return (
    <section className="entity-notes" aria-label="Optional entity notes">
      <div>
        <strong>Notes</strong>
        <small>Optional</small>
        <button onClick={() => SetOpen(false)}>Hide</button>
      </div>
      <textarea
        aria-label="Instance notes"
        placeholder="Purpose, ownership, review notes…"
        value={Value}
        onChange={(Event) => Change(Event.target.value)}
      />
    </section>
  );
}

function Bake({ Title, Sun = false }) {
  return (
    <Card Title={Title} Height={Sun ? 225 : 162}>
      {Sun && <small className="procedural-source">Procedural source</small>}
      <div className="bake-content">
        <Tile Label="Use baked image" Disabled Large={Sun} IconName="bake" />
        <div>
          <button disabled>
            <Glyph Name="bake" /> Bake
          </button>
          <button disabled>↥　Load image</button>
          <p>No image source connected.</p>
        </div>
      </div>
      <p className="card-foot">Renderer / imported-image binding unavailable</p>
    </Card>
  );
}
function MoonImage({ Preset = 0, Phase = 0.62, Size = 150, Rotation = 0 }) {
  return (
    <div
      className="moon-disc"
      style={{
        width: Size,
        height: Size,
        transform: `rotate(${Rotation}deg)`,
        backgroundImage: `linear-gradient(90deg,rgba(0,0,0,${Math.abs(0.5 - Phase) * 1.5}),transparent 65%),url(${window.NativeAssets.Moons[Preset]})`,
      }}
    />
  );
}
export function Inspector({
  Subject,
  Values,
  Change,
  Hidden,
  ToggleHidden,
  OpenShader,
  OpenFracture,
  OpenWind,
  OpenWindField,
  WindFields = [],
  AllValues = {},
  AllHidden = {},
  ReferenceCards = null,
  CloudCoverageCards = null,
  ReferenceDetailCards = null,
}) {
  const Sheet = Panels[Subject.Panel] || Panels.geometry;
  const [MoonSlot, SelectMoon] = useState(0);
  const Prefix = Subject.Panel === "moon" ? "moon" + MoonSlot + ":" : "";
  const V = (Name) =>
    (["clouds", "local-cloud"].includes(Subject.Panel)
      ? CloudValue(Values, Name, Subject.Panel)
      : Values[Prefix + Name]) ??
    (Subject.Panel === "moon" && Name === "Preset"
      ? MoonSlot
      : Subject.Panel === "moon" && Name === "Visible"
        ? MoonSlot === 0
        : Subject.Panel === "moon" && Name === "Size"
          ? [0.52, 1.6, 1.2, 1.1][MoonSlot]
          : Sheet.find((Field) => Field.Label === Name)?.Default);
  const AssignProperty = (Name, Next) => {
    if (
      Subject.Panel === "local-cloud" &&
      ["Base", "Thickness"].includes(Name)
    ) {
      for (const [Key, Numeric] of Object.entries(
        CloudEdit(Values, Name, Next, Subject.Panel),
      ))
        Change(Key, Numeric);
      return;
    }
    Change(Prefix + Name, Next);
    if (Subject.Panel === "moon" && Name === "Preset")
      Change(Prefix + "Size", [0.52, 1.6, 1.2, 1.1, 2.4, 0.6][Next]);
    if (Subject.Panel === "sun" && Name === "Speed" && Next < 4)
      Change("Day duration", 24 / [1, 8, 30, 100][Next]);
    if (Subject.Panel === "sun" && Name === "Day duration") Change("Speed", 4);
  };
  const Latest = useRef(Values);
  Latest.current = Values;
  useEffect(() => {
    if (Subject.Panel !== "sun" || !Values.Animate) return;
    let Before = performance.now();
    const Timer = setInterval(() => {
      const Now = performance.now(),
        Hours = Latest.current["Local Hours"] ?? 12,
        Duration = Latest.current["Day duration"] ?? 3;
      Change(
        "Local Hours",
        (Hours + (((Now - Before) / 1000) * 24) / Duration / 3600) % 24,
      );
      Before = Now;
    }, 100);
    return () => clearInterval(Timer);
  }, [Subject.Id, Values.Animate]);
  const F = (Name, Disabled = false) => {
    const Field = Sheet.find((Item) => Item.Label === Name);
    return Field ? (
      <Control
        key={Name}
        Field={Field}
        Value={V(Name)}
        Change={(Next) => AssignProperty(Name, Next)}
        Disabled={Disabled}
      />
    ) : null;
  };
  const Fields = (...Names) => Names.map((Name) => F(Name));
  const Tiles = (Names, Large = false) => (
    <div className="tiles">
      {Names.map((Name) => (
        <Tile
          key={Name}
          Label={Name}
          Context={Subject.Panel}
          On={V(Name)}
          Large={Large}
          Action={() => AssignProperty(Name, !V(Name))}
        />
      ))}
    </div>
  );
  const Header = (Category, Title = Subject.Name) => (
    <header className="inspector-heading">
      <div className="breadcrumbs">Inspector / {Category}</div>
      <h1>{Title}</h1>
      <p className="eyebrow">{Subject.Description}</p>
      <EntityNotes
        Value={Values.Notes || ""}
        Change={(Next) => Change("Notes", Next)}
      />
    </header>
  );
  const Capabilities = () => (
    <section
      className="generic-card entity-capabilities"
      data-card="Quick controls"
    >
      <h4>QUICK CONTROLS</h4>
      <div className="tiles">
        <Tile
          Label={Subject.Panel === "post" ? "Enabled" : "Visible"}
          Context={Subject.Panel}
          IconName="visible"
          On={!Hidden}
          Action={ToggleHidden}
        />
        {(Subject.Panel === "geometry"
          ? [
              ["Dynamic", "DYNAMIC", false, "motion"],
              ["Cast shadows", "Cast shadows", true, "shadow"],
              ["GI", "GI", true, "gi"],
              ["Physics", "PHYSICS", false, "collision"],
              ["Locked", "LOCKED", false, "lock"],
            ]
          : Subject.Panel === "light"
            ? [
                ["Cast shadows", "Cast shadows", true, "shadow"],
                ["GI", "GI", true, "gi"],
              ]
            : []
        ).map(([Label, Key, Default, Symbol]) => (
          <Tile
            key={Key}
            Label={Label}
            IconName={Symbol}
            On={Values[Key] ?? Default}
            Action={() => {
              const Next = !(Values[Key] ?? Default);
              Change(Key, Next);
              if (Key === "PHYSICS" && Next) Change("DYNAMIC", true);
              if (Key === "DYNAMIC" && !Next) Change("PHYSICS", false);
            }}
          />
        ))}
      </div>
      {Subject.Panel === "geometry" && (
        <p>
          {Values.DYNAMIC ? "Dynamic object" : "Static object"} ·{" "}
          {Values.PHYSICS ? "Physics requested" : "Physics off"}. Shadows, GI
          and physics are authored flags; native execution is pending.
        </p>
      )}
      {Subject.Panel === "light" && (
        <p>
          Shadow and GI participation are authored flags; native execution is
          pending.
        </p>
      )}
    </section>
  );
  const Generic = () => (
    <>
      {Header(
        Subject.Panel === "light"
          ? "Lighting"
          : Subject.Panel === "geometry"
            ? "Geometry"
            : Subject.Panel === "post"
              ? "Post processing"
              : "Scene",
      )}
      {Subject.Panel === "light" && ReferenceCards}
      {Subject.Panel === "light" && Values.ReferenceInspector?.Locked && (
        <button
          onClick={() =>
            Change("ReferenceInspector", {
              ...Values.ReferenceInspector,
              Locked: false,
            })
          }
        >
          Unlock editing
        </button>
      )}
      {Subject.Panel === "geometry" && (
        <TransformPanel Values={Values} Change={Change} />
      )}
      {Capabilities()}
      {Subject.Panel === "light" && ReferenceDetailCards}
      {Subject.Panel === "geometry" && (
        <MaterialPanel
          Subject={Subject}
          Values={Values}
          Change={Change}
          Expand={OpenShader}
          Compact
        />
      )}
      {[...new Set(Sheet.map((Field) => Field.Group))]
        .filter(
          (Group) =>
            Subject.Panel !== "geometry" ||
            !["Transform", "Surface"].includes(Group),
        )
        .map((Group) => (
          <details open className="generic-card" key={Group}>
            <summary>{Group}</summary>
            {Sheet.filter((Field) => Field.Group === Group).map((Field) =>
              F(Field.Label),
            )}
            {["light", "post"].includes(Subject.Panel) && (
              <PropertyGraph
                Title={Group}
                Children={Sheet.filter((Field) => Field.Group === Group).map(
                  (Field) => F(Field.Label),
                )}
              />
            )}
          </details>
        ))}
      {Subject.Panel === "geometry" && (
        <FracturePanel
          Subject={Subject}
          Values={Values}
          Change={Change}
          Expand={OpenFracture}
        />
      )}
    </>
  );
  if (["geometry", "light", "post", "group"].includes(Subject.Panel))
    return (
      <GraphContext.Provider
        value={
          ["light", "post"].includes(Subject.Panel)
            ? { Panel: Subject.Panel }
            : null
        }
      >
        <div
          className={
            "generic-inspector " +
            (["light", "post"].includes(Subject.Panel)
              ? "environment-live"
              : "")
          }
          data-panel={Subject.Panel}
        >
          {Subject.ReferenceOnly && Subject.Panel === "light" ? (
            <>
              {Header("Lighting")}
              {Values.ReferenceInspector?.Locked && (
                <button
                  onClick={() =>
                    Change("ReferenceInspector", {
                      ...Values.ReferenceInspector,
                      Locked: false,
                    })
                  }
                >
                  Unlock editing
                </button>
              )}
              {ReferenceCards}
            </>
          ) : (
            Generic()
          )}
        </div>
      </GraphContext.Provider>
    );
  let Content;
  if (Subject.Panel === "sun") {
    const Time = V("Local Hours");
    const Solved = Solar({
      LocalHours: Time,
      Month: V("Month"),
      Day: V("Day of Month"),
      Latitude: V("Latitude"),
      Longitude: V("Longitude"),
    });
    const { Elevation, Azimuth } = Solved;
    Content = (
      <>
        {Header("Environment")}
        {ReferenceCards}
        <div className="section-caption">
          PROPERTIES <small>Light, direction & atmosphere</small>
        </div>
        <div className="tiles sun-tiles">
          <Tile
            Label="Enabled"
            Context="sun"
            On={!Hidden}
            Action={ToggleHidden}
            Large
          />
          <button disabled>↺ Reset</button>
          <Tile Label="Sunlight" Large Disabled IconName="sun" />
          <Tile Label="Sun disc" Large Disabled IconName="sun" />
          <Tile
            Label="Day cycle"
            Large
            IconName="celestial-orbit"
            On={V("Animate")}
            Action={() => AssignProperty("Animate", !V("Animate"))}
          />
          <Tile
            Label={V("Animate") ? "Dynamic" : "Static"}
            Large
            IconName="celestial-orbit"
            On={V("Animate")}
            Action={() => AssignProperty("Animate", !V("Animate"))}
          />
        </div>
        <div className="subcomponent">
          <Icon Name="lens-flare" />
          <div>
            Lens Flare<small>Optical effect subcomponent</small>
          </div>
          <Glyph Name="arrow" />
        </div>
        <div className="card-grid sun-properties">
          <Card Title="Sun direction" Height={711} Class="direction-card">
            <Metric
              Value={Elevation.toFixed(1) + "°"}
              Caption={
                Elevation < 0 ? "Below the horizon" : "Above the horizon"
              }
            />
            <div
              className="graphic tall solved-gizmo"
              title="Solved direction. Edit Local Hours to change it."
            >
              <SunGizmo
                azimuth={Azimuth}
                elevation={Elevation}
                onChange={() => {}}
              />
            </div>
            {F("Local Hours")}
            <div className="solved-directions">
              <div>
                <small>Azimuth</small>
                <Metric Value={Azimuth.toFixed(1) + "°"} />
                <input
                  type="range"
                  disabled
                  value={Azimuth}
                  min="0"
                  max="360"
                />
              </div>
              <div>
                <small>Elevation</small>
                <Metric Value={Elevation.toFixed(1) + "°"} />
                <input
                  type="range"
                  disabled
                  value={Elevation}
                  min="-90"
                  max="90"
                />
              </div>
            </div>
            <p>Shared time above · angles solved by date / location</p>
          </Card>
          <Card Title="Sunlight intensity" Height={347} GraphHandled>
            <Metric
              Value={Number(V("Intensity")).toFixed(1)}
              Unit="×"
              Caption="Sunlight intensity multiplier"
            />
            <SunGraph V={V} Change={AssignProperty} Mode="gain" />
            {F("Intensity")}
            <div className="ends">
              <span>0 ×</span>
              <span>60 ×</span>
            </div>
          </Card>
          <Card Title="Temperature" Height={347} GraphHandled>
            <Metric
              Value={V("Colour source") ? V("Temperature") : "RGB"}
              Unit={V("Colour source") ? "K" : ""}
            />
            {F("Colour source")}
            <div className="temperature-spectrum" />
            <SunGraph V={V} Change={AssignProperty} Mode="temperature" />
            <div className="ends">
              <span>Warm</span>
              <span>Cool</span>
            </div>
            {F("Temperature", !V("Colour source"))}
            <p>Blackbody tint approximation · linear RGB</p>
            {F("Sun Tint", !!V("Colour source"))}
          </Card>
          <Card Title="Daylight cycle" Height={329} GraphHandled>
            <Metric
              Value={`${String(Math.floor(Time)).padStart(2, "0")}:${String(Math.floor((Time % 1) * 60)).padStart(2, "0")}`}
            />
            <SunGraph V={V} Change={AssignProperty} Mode="day" />
            {F("Local Hours")}
          </Card>
          <Card Title="Sun disc" Height={329}>
            <Metric Value={Number(V("Angular Diameter")).toFixed(2) + "°"} />
            <div
              className="solar-disc"
              style={{
                "--diameter":
                  Math.min(120, 20 + V("Angular Diameter") * 30) + "px",
              }}
            />
            {F("Angular Diameter")}
          </Card>
        </div>
        <Card Title="Dynamic settings" Wide GraphHandled>
          <SunGraph V={V} Change={AssignProperty} Mode="year" />
          {Fields(
            "Animate",
            "Day duration",
            "Speed",
            "Direct",
            "Latitude",
            "Longitude",
            "Day of Month",
            "Month",
          )}
          <div className="readout">
            <span>Declination</span>
            <output>{Solved.Declination.toFixed(2)}°</output>
          </div>
          <div className="readout">
            <span>Equation of Time</span>
            <output>{Solved.EquationOfTime.toFixed(2)} min</output>
          </div>
        </Card>
        <p className="native-note">
          Solar position uses the native NOAA calculation, translated to
          JavaScript. No GPU lighting is running.
        </p>
        <div className="section-caption">
          BAKING <small>Native target bindings pending</small>
        </div>
        <div className="card-grid">
          <Bake Title="Sun lighting bake" Sun />
          <Bake Title="Sun disk bake" Sun />
        </div>
      </>
    );
  } else if (Subject.Panel === "camera") {
    const Focal = V("Focal Length"),
      Sensor = V("Sensor Width"),
      Aperture = V("Aperture"),
      Distance = V("Subject Distance"),
      Aspect = Subject.Id === "cine" ? 1.5 : 16 / 9,
      H = (Focal * Focal) / (Aperture * 0.03) + Focal,
      S = Distance * 1000;
    const Optics = {
      horizontalFov: (2 * Math.atan(Sensor / (2 * Focal)) * 180) / Math.PI,
      verticalFov:
        (2 * Math.atan(Sensor / Aspect / (2 * Focal)) * 180) / Math.PI,
      pupil: Focal / Aperture,
      near: (H * S) / (H + S - Focal) / 1000,
      far: H - (S - Focal) > 0 ? (H * S) / (H - S + Focal) / 1000 : Infinity,
      hyperfocal: H / 1000,
      frameWidth: (Distance * Sensor) / Focal,
      frameHeight: (Distance * Sensor) / Aspect / Focal,
    };
    Content = (
      <>
        {Header(
          "Scene / Cameras",
          Subject.Id === "cine" ? "Cine Camera · lens study" : Subject.Name,
        )}
        {IsEditorCamera(Subject) && (
          <div className="editor-camera-note">
            Editor navigation camera · always present. Cannot be deleted or
            hidden. Construct adds separate scene cameras.
          </div>
        )}
        <Card Title="Lens + field of view" Height={440}>
          <Metric Value={Focal.toFixed(1)} Unit="mm" />
          <div className="graphic lens">
            <SensorField
              sensor={Sensor}
              aspect={Aspect}
              focal={Focal}
              aperture={Aperture}
              focus={Distance}
              optics={Optics}
            />
          </div>
          <p>
            Optical schematic · not to scale. Sensor height follows the viewport
            aspect for the live camera.
          </p>
          {F("Focal Length")}
          <p>
            Vertical FOV {Optics.verticalFov.toFixed(1)}° · frame at subject{" "}
            {Optics.frameWidth.toFixed(2)} × {Optics.frameHeight.toFixed(2)} m
          </p>
        </Card>
        <div className="card-grid">
          <Card Title="Aperture study" Height={464}>
            <Metric Value={"f / " + Aperture.toFixed(1)} />
            <p>
              Entrance pupil and acceptable-sharpness limits update together. No
              renderer exposure or blur is changed.
            </p>
            <div className="graphic iris">
              <Iris opening={(16 / Aperture - 1) / (16 / 1.4 - 1)} />
            </div>
            {F("Aperture")}
            <p>Pupil diameter {Optics.pupil.toFixed(1)} mm</p>
          </Card>
          <Card Title="Subject plane + sharpness" Height={464}>
            <Metric Value={Distance.toFixed(1)} Unit="m" />
            <p>
              Drag the subject plane. Thin-lens diagnostic · circle of confusion
              0.03 mm.
            </p>
            <DepthOfField
              focus={Distance}
              optics={Optics}
              enabled
              onChange={(Next) => AssignProperty("Subject Distance", Next)}
            />
            {F("Subject Distance")}
            <p>
              Near {Optics.near.toFixed(1)} m / far{" "}
              {Number.isFinite(Optics.far)
                ? Optics.far.toFixed(1) + " m"
                : "infinity"}
            </p>
          </Card>
        </div>
        <Card Title="Sensor / support" Height={196}>
          {F("Sensor Width")}
          <p>
            Aspect {Aspect.toFixed(3)} · hyperfocal{" "}
            {Optics.hyperfocal.toFixed(1)} m · pinhole renderer
          </p>
        </Card>
      </>
    );
  } else if (Subject.Panel === "stars") {
    Content = (
      <>
        {Header("Environment", "Stars")}
        <Card Title="Star field" Height={444}>
          <Metric
            Value={V("Limiting magnitude").toFixed(1)}
            Unit="mag"
            Caption="Higher values reveal fainter stars"
          />
          <svg
            className="star-field"
            viewBox="0 0 420 194"
            role="img"
            aria-label="Equatorial star chart"
          >
            {Array.from({ length: 300 }, (_, Index) => {
              let X = (Math.sin(Index * 78.233) * 43758.54) % 1,
                Y = (Math.sin(Index * 32.213) * 22578.3) % 1;
              return (
                <circle
                  key={Index}
                  cx={Math.abs(X) * 420}
                  cy={Math.abs(Y) * 194}
                  r={0.4 + Math.abs(X) * 1.2}
                  fill="#cfc6eb"
                  opacity={
                    V("Star field") && Index % 8 <= V("Limiting magnitude")
                      ? 0.7
                      : 0
                  }
                />
              );
            })}
          </svg>
          {F("Limiting magnitude")}
          <p>Equatorial chart study · no live star catalogue connection</p>
        </Card>
        <Card Title="Stars settings" Height={165}>
          <div className="tiles">
            {["Star field", "Twinkle"].map((Name) => (
              <Tile
                key={Name}
                Label={Name}
                On={V(Name)}
                Action={() => AssignProperty(Name, !V(Name))}
              />
            ))}
          </div>
          <p>Catalogue field controls.</p>
        </Card>
        <div className="card-grid">
          <Card Title="Twinkle" Height={547}>
            <Metric
              Value={V("Twinkle depth")}
              Unit="%"
              Caption="Modulation depth"
            />
            <TwinkleSignal
              strength={V("Twinkle depth")}
              rate={V("Twinkle rate")}
              active={V("Twinkle") && V("Star field")}
            />
            {Fields("Twinkle depth", "Twinkle rate")}
          </Card>
          <Card Title="Celestial rotation" Height={547}>
            <Metric
              Value={V("Celestial rotation").toFixed(0) + "°"}
              Caption="Rotate the field around the celestial pole"
            />
            <CelestialRotation
              angle={V("Celestial rotation")}
              onChange={(Next) => AssignProperty("Celestial rotation", Next)}
            />
            {F("Celestial rotation")}
          </Card>
        </div>
        <Card Title="Renderer scale" Height={168}>
          {Fields("Brightness", "Point Size", "Loaded", "Sidereal Time")}
          <p>
            The chart omits atmosphere and daylight extinction. The pole dial is
            a schematic.
          </p>
        </Card>
        <div className="section-caption">BAKING</div>
        <Card Title="Star field bake">
          <div className="tiles">
            <Tile Label="Bake" Disabled IconName="bake" />
            <Tile Label="Use baked" Disabled IconName="bake" />
          </div>
          <p>Baked-star playback is not implemented.</p>
        </Card>
      </>
    );
  } else if (Subject.Panel === "moon") {
    const Preset = V("Preset");
    Content = (
      <>
        {Header("Environment", "Moon")}
        <Card Title="Lunar phase" Height={443}>
          <Metric
            Value={(V("Phase") * 29.53).toFixed(1)}
            Unit="days"
            Caption={`${Math.round((1 - Math.cos(V("Phase") * Math.PI * 2)) * 0.5 * 100)}% illuminated`}
          />
          <p>Position in the 29.53-day synodic cycle</p>
          <MoonImage Preset={Preset} Phase={V("Phase")} />
          {F("Phase", V("Follow Sky"))}
        </Card>
        <Card Title="Moon settings" Height={178}>
          <select
            className="moon-instance"
            aria-label="Active moon instance"
            value={MoonSlot}
            onChange={(Event) => SelectMoon(+Event.target.value)}
          >
            {[0, 1, 2, 3].map((Slot) => (
              <option key={Slot} value={Slot}>
                {
                  ["Luna", "Ember", "Glacier", "Sulfur", "Shroud", "Shard"][
                    Values["moon" + Slot + ":Preset"] ?? Slot
                  ]
                }
              </option>
            ))}
          </select>
          <div className="moon-settings-tiles">
            {["Visible", "Follow Sky"].map((Label) => (
              <Tile
                key={Label}
                Label={Label}
                Large
                IconName="celestial-orbit"
                On={V(Label)}
                Action={() => AssignProperty(Label, !V(Label))}
              />
            ))}
          </div>
        </Card>
        <Card Title="Moon catalogue" Height={216}>
          <div className="moon-catalogue">
            {["Luna", "Ember", "Glacier", "Sulfur", "Shroud", "Shard"].map(
              (Name, Index) => (
                <button
                  key={Name}
                  className={Preset === Index ? "selected" : ""}
                  onClick={() => AssignProperty("Preset", Index)}
                >
                  <MoonImage Preset={Index} Size={88} />
                  {Name}
                </button>
              ),
            )}
          </div>
        </Card>
        <div className="card-grid">
          <Card Title="Moonlight" Height={452}>
            <Metric
              Value={V("Bright").toFixed(2)}
              Unit="×"
              Caption="Full-disc brightness multiplier · not calibrated lux"
            />
            <MoonImage Preset={Preset} Size={86} />
            {Fields("Bright", "Glow")}
          </Card>
          <Card Title="Moon size" Height={452}>
            <Metric
              Value={V("Size").toFixed(2) + "°"}
              Caption="Angular diameter · compressed preview for oversized moons"
            />
            <MoonImage
              Preset={Preset}
              Size={100 + Math.min(60, V("Size") * 2)}
            />
            {F("Size")}
            <button onClick={() => AssignProperty("Size", 0.52)}>
              Natural size · 0.52°
            </button>
          </Card>
          <Card Title="Lunar position" Height={484}>
            <p>
              {V("Follow Sky")
                ? "Solved direction · Follow sky is ON"
                : "Position in the sky · drag the spherical dial"}
            </p>
            <LunarSphere
              azimuth={V("Azimuth")}
              elevation={V("Elevation")}
              rotation={V("Roll")}
              size={V("Size")}
              onChange={(Az, El) => {
                if (!V("Follow Sky")) {
                  AssignProperty("Azimuth", Az);
                  AssignProperty("Elevation", El);
                }
              }}
            />
            {F("Azimuth", V("Follow Sky"))}
            {F("Elevation", V("Follow Sky"))}
          </Card>
          <Card Title="Moon rotation" Height={532}>
            <p>Surface orientation · independent of sky position</p>
            <MoonImage Preset={Preset} Rotation={V("Roll")} Size={175} />
            {Fields("Roll", "Pitch")}
            <button
              onClick={() => {
                AssignProperty("Roll", 0);
                AssignProperty("Pitch", 0);
              }}
            >
              Reset orientation
            </button>
          </Card>
        </div>
      </>
    );
  } else if (Subject.Panel === "atmosphere") {
    Content = (
      <>
        {Header("Environment", "Atmosphere")}
        <AtmosphereLab V={V} Change={AssignProperty} />
        <Card Title="Atmosphere settings">{Tiles(["Line at civil only"])}</Card>
        <Card Title="Atmospheric scattering" Height={485} GraphHandled>
          <Metric
            Value={V("Rayleigh").toFixed(1)}
            Unit="×"
            Caption="How air molecules scatter sunlight"
          />
          <AtmosphereProfile V={V} Change={AssignProperty} Mode="spectrum" />
          {F("Rayleigh")}
        </Card>
        <div className="card-grid">
          <Card Title="Aerosol haze" Height={380} GraphHandled>
            <Metric
              Value={V("Mie").toFixed(2)}
              Unit="×"
              Caption="Suspended particles soften and attenuate light"
            />
            <AtmosphereProfile V={V} Change={AssignProperty} Mode="haze" />
            {F("Mie")}
          </Card>
          <Card Title="Ozone absorption" Height={380} GraphHandled>
            <Metric
              Value={V("Ozone").toFixed(2)}
              Unit="×"
              Caption="Selective absorption across visible wavelengths"
            />
            <AtmosphereProfile V={V} Change={AssignProperty} Mode="ozone" />
            {F("Ozone")}
          </Card>
        </div>
        <Card Title="Density falloff" Height={410} GraphHandled>
          {F("Rayleigh Scale H")}
          <AtmosphereProfile V={V} Change={AssignProperty} Mode="height" />
          {F("Mie Scale H")}
        </Card>
        <Card Title="Ground reflectance" Height={150}>
          {F("Ground Albedo")}
          <p>
            Original RGB ground albedo · applied separately, not stored in the
            smooth dome bake.
          </p>
        </Card>
        <Card Title="Native atmosphere controls">
          {Fields(
            "Mie Anisotropy",
            "Atmosphere",
            "Horizon Glow",
            "White Line",
            "Sky Tint",
            "Sky Brightness",
            "Air Mass",
            "Tier Samples",
          )}
        </Card>
        <div className="section-caption">BAKING</div>
        <Bake Title="Atmosphere bake" Sun />
        <Card Title="Baked atmosphere" Height={210}>
          <div className="empty-image">No resident atmosphere image</div>
          <p>
            Load / bake requires the native engine. No completed bake is
            simulated.
          </p>
          {F("Fetch Baked Dome", true)}
          {Fields("Dome Path", "Sheet")}
        </Card>
      </>
    );
  } else if (Subject.Panel === "flare") {
    Content = (
      <>
        {Header("Environment / Optics", "Lens Flare")}
        <Card Title="Flare composite" Height={430}>
          <Metric Value={V("Intensity").toFixed(2)} Unit="×" />
          <div
            className="flare-preview"
            onPointerDown={(Event) => {
              Event.currentTarget.setPointerCapture(Event.pointerId);
            }}
            onPointerMove={(Event) => {
              if (Event.buttons) {
                const Rect = Event.currentTarget.getBoundingClientRect();
                AssignProperty(
                  "Preview X",
                  Math.max(
                    0.06,
                    Math.min(0.94, (Event.clientX - Rect.left) / Rect.width),
                  ),
                );
                AssignProperty(
                  "Preview Y",
                  Math.max(
                    0.08,
                    Math.min(0.92, (Event.clientY - Rect.top) / Rect.height),
                  ),
                );
              }
            }}
          >
            <div
              className="flare-source"
              style={{
                left: V("Preview X") * 100 + "%",
                top: V("Preview Y") * 100 + "%",
                opacity: V("Enabled") ? Math.min(1, V("Intensity")) : 0,
              }}
            />
            {Array.from({ length: Math.min(24, V("Ghosts")) }, (_, Index) => (
              <i
                key={Index}
                style={{
                  left: ((10 + Index * 12) % 95) + "%",
                  top: ((20 + Index * 9) % 85) + "%",
                  width: 12 + Index * 6,
                  height: 12 + Index * 6,
                }}
              />
            ))}
          </div>
          <p>
            Drag the source · HTML optical illustration, not the renderer
            composite.
          </p>
        </Card>
        <Card Title="Flare settings">
          {Tiles(["Enabled", "Anamorphic", "Streaks", "Starburst"])}
        </Card>
        <Card Title="Flare source">{Fields("Intensity", "Type")}</Card>
        <div className="card-grid">
          <Card Title="Flare layers" Height={565}>
            <p>
              Combine layers instead of choosing just one. Editing a layer
              selects Custom layers.
            </p>
            {Fields("Spread", "Rotation", "Ray pairs", "Streak gain")}
          </Card>
          <Card Title="Lens ghosts" Height={565}>
            <Metric Value={V("Ghosts")} Unit="elements" />
            {Fields(
              "Ghosts",
              "Ghost brightness",
              "Ghost spacing",
              "Ghost shape",
            )}
          </Card>
        </div>
        <Card Title="Halo & optical response">
          {Fields("Halo Radius", "Halo brightness", "Halo width", "Chromatic")}
        </Card>
        <Card Title="Legacy preset settings">
          {Fields("Aperture Blades", "Preview X", "Preview Y")}
        </Card>
        <div className="section-caption">BAKING</div>
        <Bake Title="Lens flare image" />
      </>
    );
  } else if (Subject.Panel === "wind") {
    Content = (
      <>
        {Header("Environment", Subject.Name)}
        {ReferenceCards}
        <Card Title="Wind controls">
          <div className="tiles">
            <Tile
              Label="Enabled"
              Context="wind"
              On={!Hidden}
              Action={ToggleHidden}
            />
            <Tile
              Label="Air shear"
              Context="wind"
              On={V("Air shear")}
              Action={() => AssignProperty("Air shear", !V("Air shear"))}
            />
          </div>
        </Card>
        <WindInspector
          Values={Values}
          Change={Change}
          Open={OpenWind}
          Hidden={Hidden}
        />
        <Card Title="Atmospheric modifiers · native draft">
          {Fields("Shear", "Veer", "Turbulence", "Steadiness")}
          {F("Force")}
          <p>
            Stored atmospheric descriptors for the native mirror. Altitude
            shear, veer and turbulence are not evaluated by this horizontal 2D
            field preview.
          </p>
        </Card>
      </>
    );
  } else if (Subject.Panel === "precipitation") {
    Content = (
      <>
        {Header("Environment", "Precipitation")}
        <Card Title="Precipitation type" Height={270}>
          <div className="precip-types">
            {["Rain", "Drizzle", "Hail", "Snow", "Sleet"].map((Name, Index) => (
              <button
                className={V("Precipitation") === Index ? "selected" : ""}
                key={Name}
                onClick={() => AssignProperty("Precipitation", Index)}
              >
                <Icon
                  Name={
                    Index === 2
                      ? "weather-hail"
                      : Index >= 3
                        ? "weather-snow"
                        : "weather-rain"
                  }
                  Size={32}
                />
                {Name}
              </button>
            ))}
          </div>
        </Card>
        <Card Title="Emission + collision" Height={224}>
          {Tiles([
            "Enabled",
            "Follow Wind",
            "Spawn from Clouds",
            "Ground Collision",
          ])}
        </Card>
        <div className="card-grid">
          <Card Title="Fall + density" Height={446}>
            {Fields("Intensity", "Density", "Particle Size", "Wind Drift")}
          </Card>
          <Card Title="Particle scale" Height={446}>
            <Metric
              Value={(V("Particle Size") * 3).toFixed(2)}
              Unit="mm"
              Caption="Nominal diameter · type radius × size scale"
            />
            <div className="particle-scale">
              <Icon Name="weather-rain" Size={96} />
            </div>
            <p>
              Magnified physical-size reference, not a particle count preview.
            </p>
          </Card>
        </div>
        <Card Title="Simulation + settling" Height={290}>
          {Fields("Accumulation", "Particles", "Snow Depth")}
          <p>
            Native simulation disconnected in HTML. No live particle count is
            fabricated.
          </p>
        </Card>
      </>
    );
  } else if (Subject.Panel === "rainbow") {
    Content = (
      <>
        {Header("Environment", "Rainbow")}
        <Card Title="Optical preview">
          <div
            className="rainbow-preview"
            style={{ opacity: V("Enabled") ? Math.min(1, V("Intensity")) : 0 }}
          >
            <div />
          </div>
          <p>
            Liquid-water optics · illustrative HTML preview, not the spectral
            kernel.
          </p>
        </Card>
        <Card Title="Visibility" Height={154}>
          {Tiles(["Enabled", "Alexander's Band"])}
        </Card>
        <Card Title="Bow response" Height={458}>
          {Fields("Intensity", "Width", "Secondary", "Minimum Path")}
        </Card>
        <Bake Title="Bake / image" />
      </>
    );
  } else if (["clouds", "local-cloud"].includes(Subject.Panel)) {
    const Local = Subject.Panel === "local-cloud";
    Content = (
      <>
        {Header("Environment", Local ? "Local Cloud" : "Clouds")}
        {Values.ReferenceInspector?.Locked && (
          <button
            onClick={() =>
              Change("ReferenceInspector", {
                ...Values.ReferenceInspector,
                Locked: false,
              })
            }
          >
            Unlock editing
          </button>
        )}
        {ReferenceCards}
        <Card Title="Cloud settings" Height={165}>
          {Tiles(["Enabled", "Follow Wind"])}
        </Card>
        <section
          className="cloud-coverage-replacement"
          data-card="Cloud coverage"
        >
          {CloudCoverageCards}
          {F("Coverage")}
        </section>
        <div className="card-grid">
          <Card Title="Cloud base" Height={452} GraphHandled>
            <Metric
              Value={(V("Base") / 1000).toFixed(2)}
              Unit="km"
              Caption={
                Local
                  ? "Lower bound · centre Z − half height"
                  : "World Z altitude · drag the base line"
              }
            />
            <CloudDeckPanel V={V} Change={AssignProperty} Local={Local} />
            {!Local && F("Base")}
          </Card>
          <Card Title="Layer thickness" Height={452} GraphHandled>
            <Metric
              Value={(V("Thickness") / 1000).toFixed(2)}
              Unit="km"
              Caption={
                Local
                  ? "Bounded depth · twice half height"
                  : "Vertical development · density profile"
              }
            />
            <CloudSection V={V} Change={AssignProperty} Local={Local} />
            {!Local && F("Thickness")}
          </Card>
        </div>
        {Local && (
          <Card Title="Local bounds" Height={452}>
            <TransformPanel
              Values={Values}
              Change={Change}
              Compact
              Space="WORLD SPACE"
              Rows={[
                [
                  "Centre",
                  "m",
                  Sheet.find((Field) => Field.Label === "Centre").Default,
                  -100000,
                  100000,
                  1,
                ],
                [
                  "Half Size",
                  "m",
                  Sheet.find((Field) => Field.Label === "Half Size").Default,
                  0.001,
                  100000,
                  1,
                ],
              ]}
            />
            <CloudBounds V={V} Change={AssignProperty} />
          </Card>
        )}
        <Card Title="Cloud body">
          {Fields(
            "Density",
            "Feature Scale",
            "Anisotropy",
            ...(!Local ? ["Type", "Anvil", "Ceiling"] : []),
          )}
        </Card>
        {!Local && (
          <details className="property-card">
            <summary>GPU cloud shadows · separate field</summary>
            {Sheet.filter((Field) =>
              ["Cloud Shadows", "Shadow Clock", "Tier Budget"].includes(
                Field.Group,
              ),
            ).map((Field) => F(Field.Label))}
            <PropertyGraph
              Title="Cloud shadows"
              Children={Sheet.filter((Field) =>
                ["Cloud Shadows", "Shadow Clock", "Tier Budget"].includes(
                  Field.Group,
                ),
              ).map((Field) => F(Field.Label))}
            />
          </details>
        )}
        <WindBinding
          Values={Values}
          Change={Change}
          Fields={WindFields}
          Open={OpenWindField}
          AllValues={AllValues}
          Hidden={AllHidden}
        />
      </>
    );
  } else if (Subject.Panel.includes("fog")) {
    const Local = Subject.Panel === "local-fog",
      Aerial = Subject.Panel === "aerial-fog",
      Height = !Local && !Aerial;
    Content = (
      <>
        {Header("Environment")}
        <Card Title="Fog settings" Height={142}>
          {Tiles(Local ? ["Enabled", "Follow Wind"] : ["Enabled"])}
        </Card>
        <Card Title="Visibility through fog" Height={435} GraphHandled>
          <FogGraph Kind={Subject.Panel} V={V} Change={AssignProperty} />
        </Card>
        <div className="card-grid fog-model-cards">
          <Card Title="Medium" Height={416}>
            {Fields(
              "Density",
              Local ? "Coverage" : Aerial ? "Start" : "Falloff Height",
              Local ? "Feature Scale" : Aerial ? "Mie Blend" : "Sun Scatter",
              ...(Local ? ["Anisotropy"] : []),
            )}
            <FogBeamChamber Kind={Subject.Panel} V={V} />
          </Card>
          {Local ? (
            <FogShapePanel Values={Values} Change={Change} />
          ) : (
            <Card
              Title={Aerial ? "Spectral transmission" : "Height and tint"}
              Height={416}
              GraphHandled
            >
              {Aerial ? (
                <FogSpectrum V={V} Change={AssignProperty} />
              ) : (
                <HeightFogVisual V={V} Change={AssignProperty} />
              )}
              {Height && F("Colour")}
            </Card>
          )}
        </div>
        <Card Title="Wind binding">
          {Fields("Wind source", "Owned wind component")}
        </Card>
      </>
    );
  } else Content = Generic();
  return (
    <GraphContext.Provider value={{ Panel: Subject.Panel }}>
      <div
        className={"specialized-inspector environment-live " + Subject.Panel}
        data-panel={Subject.Panel}
      >
        {Content}
      </div>
    </GraphContext.Provider>
  );
}
