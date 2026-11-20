import React, { useEffect, useRef, useState } from "react";
import {
  EvaluateWind,
  ResolveWind,
  NewWindComponent,
  WindTypes,
} from "./WindSpecification.js";
import "./WindPanel.css";

export function WindCanvas({
  Field,
  Active = true,
  Playing = true,
  Vectors = false,
  Gradient = true,
  Particles = true,
  Label = "Combined wind vector field",
}) {
  const Canvas = useRef(null),
    Latest = useRef({});
  Latest.current = { Field, Active, Playing, Vectors, Gradient, Particles };
  useEffect(() => {
    const Node = Canvas.current,
      Context = Node.getContext("2d"),
      Heat = document.createElement("canvas"),
      HeatContext = Heat.getContext("2d");
    Heat.width = 64;
    Heat.height = 48;
    let Frame,
      Before = 0,
      Time = 0,
      Count = 0;
    const Points = Array.from({ length: 190 }, (_, Index) => ({
      X: ((Index * 73) % 151) / 151,
      Y: ((Index * 43) % 149) / 149,
    }));
    const Paint = (Now) => {
      Frame = requestAnimationFrame(Paint);
      if (Now - Before < 32 || !Node.getClientRects().length || document.hidden)
        return;
      const DT = Math.min(0.05, (Now - (Before || Now)) / 1000);
      Before = Now;
      const { Field, Active, Playing, Vectors, Gradient, Particles } =
        Latest.current;
      if (Playing && Active) Time += DT;
      const Box = Node.getBoundingClientRect(),
        W = Box.width,
        H = Box.height,
        Ratio = Math.min(2, devicePixelRatio || 1);
      if (!W || !H) return;
      if (
        Node.width !== Math.round(W * Ratio) ||
        Node.height !== Math.round(H * Ratio)
      ) {
        Node.width = Math.round(W * Ratio);
        Node.height = Math.round(H * Ratio);
      }
      Context.setTransform(Ratio, 0, 0, Ratio, 0, 0);
      Context.fillStyle = "#111b20";
      Context.fillRect(0, 0, W, H);
      const Sample = (X, Y) =>
        Active
          ? EvaluateWind(
              Field,
              (X / W - 0.5) * Field.Width,
              (Y / H - 0.5) * Field.Depth,
              Time,
            )
          : [0, 0];
      const Colour = (Speed, Alpha = 1) =>
        `hsla(${190 - Math.min(1, Speed / 30) * 150},48%,${32 + Math.min(1, Speed / 30) * 28}%,${Alpha})`;
      if (Gradient) {
        for (let Y = 0; Y < 48; Y++)
          for (let X = 0; X < 64; X++) {
            const V = Sample(((X + 0.5) / 64) * W, ((Y + 0.5) / 48) * H);
            HeatContext.fillStyle = Colour(Math.hypot(...V));
            HeatContext.fillRect(X, Y, 1, 1);
          }
        Context.globalAlpha = 0.5;
        Context.imageSmoothingEnabled = true;
        Context.drawImage(Heat, 0, 0, W, H);
        Context.globalAlpha = 1;
      }
      Context.strokeStyle = "#ffffff09";
      Context.lineWidth = 1;
      Context.beginPath();
      for (let X = 0; X < W; X += 32) {
        Context.moveTo(X, 0);
        Context.lineTo(X, H);
      }
      for (let Y = 0; Y < H; Y += 32) {
        Context.moveTo(0, Y);
        Context.lineTo(W, Y);
      }
      Context.stroke();
      if (Vectors)
        for (let Y = 20; Y < H; Y += 32)
          for (let X = 20; X < W; X += 32) {
            const [VX, VZ] = Sample(X, Y),
              Speed = Math.hypot(VX, VZ);
            if (Speed < 0.01) continue;
            const Angle = Math.atan2(
                (VZ * H) / Field.Depth,
                (VX * W) / Field.Width,
              ),
              Length = Math.min(24, 5 + Speed * 0.7);
            Context.save();
            Context.translate(X, Y);
            Context.rotate(Angle);
            Context.strokeStyle = Colour(Speed, 0.9);
            Context.lineWidth = 1.2;
            Context.beginPath();
            Context.moveTo(-Length / 2, 0);
            Context.lineTo(Length / 2, 0);
            Context.lineTo(Length / 2 - 4, -3);
            Context.moveTo(Length / 2, 0);
            Context.lineTo(Length / 2 - 4, 3);
            Context.stroke();
            Context.restore();
          }
      if (Particles)
        for (const Point of Points) {
          const X = Point.X * W,
            Y = Point.Y * H,
            [VX, VZ] = Sample(X, Y);
          if (Playing && Active) {
            Point.X = (((Point.X + (VX * DT * 8) / Field.Width) % 1) + 1) % 1;
            Point.Y = (((Point.Y + (VZ * DT * 8) / Field.Depth) % 1) + 1) % 1;
          }
          const Length = Math.hypot(VX, VZ);
          if (Length < 0.01) continue;
          // Reference-style fading flow strokes, evaluated through the composite field.
          // Keep the existing blue/green/gold speed palette rather than the reference teal.
          const StrokeLength = 7 + Math.min(1, Length / 30) * 34;
          const Trail = [[X, Y]];
          let TailX = X,
            TailY = Y;
          for (let Step = 0; Step < 5; Step++) {
            const [VelocityX, VelocityZ] = Sample(TailX, TailY);
            const ScreenX = (VelocityX * W) / Field.Width,
              ScreenY = (VelocityZ * H) / Field.Depth;
            const ScreenSpeed = Math.hypot(ScreenX, ScreenY);
            if (ScreenSpeed < 0.0001) break;
            TailX -= ((ScreenX / ScreenSpeed) * StrokeLength) / 5;
            TailY -= ((ScreenY / ScreenSpeed) * StrokeLength) / 5;
            Trail.push([TailX, TailY]);
          }
          const Fade = Context.createLinearGradient(
            TailX,
            TailY,
            X + 0.001,
            Y + 0.001,
          );
          Fade.addColorStop(0, Colour(Length, 0));
          Fade.addColorStop(1, Colour(Length, 0.9));
          Context.strokeStyle = Fade;
          Context.lineWidth = 0.9 + Math.min(1, Length / 30) * 1.3;
          Context.beginPath();
          Trail.forEach(([TrailX, TrailY], Index) =>
            Index
              ? Context.lineTo(TrailX, TrailY)
              : Context.moveTo(TrailX, TrailY),
          );
          Context.stroke();
        }
      Context.fillStyle = "#d4e5df";
      Context.font = "11px sans-serif";
      Context.fillText(
        Active ? "XZ · COMBINED FIELD" : "FIELD DISABLED",
        14,
        20,
      );
      Node.dataset.frame = String(++Count);
      Node.dataset.time = Time.toFixed(2);
    };
    Frame = requestAnimationFrame(Paint);
    return () => cancelAnimationFrame(Frame);
  }, []);
  return (
    <div className="wind-visual">
      <canvas ref={Canvas} role="img" aria-label={Label} data-active={Active} />
      <div className="wind-legend">
        <span>0 m/s</span>
        <i />
        <span>30+ m/s</span>
      </div>
      <small>
        Fading flow lines · composite speed colours · 8× time · horizontal XZ
        slice
      </small>
    </div>
  );
}

export function WindInspector({ Values, Change, Open, Hidden }) {
  const Field = ResolveWind(Values),
    [Playing, Play] = useState(true);
  return (
    <section className="property-card wind-inspector" data-card="Wind field">
      <div className="wind-section-head">
        <div>
          <span className="eyebrow">COMPOSITE FLOW</span>
          <h2>Wind field</h2>
        </div>
        <button
          onClick={Open}
          aria-label="Expand WindEditor"
          title="Expand WindEditor"
        >
          ↗
        </button>
      </div>
      <WindCanvas Field={Field} Active={!Hidden} Playing={Playing} />
      <div className="wind-section-head">
        <span>
          {Field.Components.filter((Item) => Item.Enabled).length} active /{" "}
          {Field.Components.length} components
        </span>
        <button aria-pressed={!Playing} onClick={() => Play(!Playing)}>
          {Playing ? "Pause preview" : "Resume preview"}
        </button>
      </div>
      <div className="wind-component-summary">
        {Field.Components.map((Item) => (
          <button
            key={Item.Id}
            aria-pressed={Item.Enabled}
            onClick={() =>
              Change("WindField", {
                ...Field,
                Components: Field.Components.map((Part) =>
                  Part.Id === Item.Id
                    ? { ...Part, Enabled: !Part.Enabled }
                    : Part,
                ),
              })
            }
          >
            <i className={Item.Enabled ? "green" : "red"} />
            <span>{Item.Name}</span>
            <small>
              {Item.Type} · {Item.Strength} m/s
            </small>
          </button>
        ))}
      </div>
      <button className="wind-wide-button" onClick={Open}>
        Open WindEditor · place and combine components
      </button>
      <p>
        All enabled components sum into one field. Browser evaluation only;
        native simulation binding is deferred.
      </p>
    </section>
  );
}

export function WindBinding({
  Values,
  Change,
  Fields,
  AllValues,
  Hidden,
  Open,
}) {
  const Id = Values.WindFieldId ?? Fields[0]?.Id ?? "",
    Subject = Fields.find((Item) => Item.Id === Id),
    Following = Values["Follow Wind"] ?? true;
  return (
    <section className="property-card wind-binding" data-card="Wind binding">
      <h3>Wind binding</h3>
      <label>
        Wind field
        <select
          aria-label="Cloud wind field"
          value={Id}
          onChange={(Event) => Change("WindFieldId", Event.target.value)}
        >
          <option value="">None · still air</option>
          {!Subject && Id && (
            <option value={Id}>Missing field · choose another</option>
          )}
          {Fields.map((Item) => (
            <option key={Item.Id} value={Item.Id}>
              {Item.Name}
              {Hidden[Item.Id] ? " (hidden)" : ""}
            </option>
          ))}
        </select>
      </label>
      {Subject ? (
        <>
          <button
            className="wind-wide-button"
            onClick={() => Open?.(Subject.Id)}
          >
            Edit wind field ↗
          </button>
          <WindCanvas
            Field={ResolveWind(AllValues[Subject.Id])}
            Active={Following && !Hidden[Subject.Id]}
            Label="Cloud selected wind field preview"
          />
          <p>
            {Following
              ? `Using all components of ${Subject.Name}.`
              : "Follow Wind is off; field selection is retained."}{" "}
            HTML binding preview; no native cloud advection.
          </p>
        </>
      ) : (
        <p>
          {Id
            ? "The assigned field was removed. Select another field."
            : "No wind field assigned."}
        </p>
      )}
    </section>
  );
}

function Placement({ Field, Selection, Select, Update }) {
  const Drag = useRef(null);
  const Move = (Event) => {
    if (!Drag.current || Drag.current.PointerId !== Event.pointerId) return;
    const Box = Event.currentTarget.getBoundingClientRect();
    Update(Drag.current.Id, {
      X: Math.round(
        ((Event.clientX - Box.left) / Box.width - 0.5) * Field.Width,
      ),
      Z: Math.round(
        ((Event.clientY - Box.top) / Box.height - 0.5) * Field.Depth,
      ),
    });
  };
  return (
    <svg
      className="wind-placement"
      viewBox="0 0 600 400"
      preserveAspectRatio="none"
      aria-label="Wind component placement map"
      onPointerMove={Move}
      onPointerUp={() => (Drag.current = null)}
      onPointerCancel={() => (Drag.current = null)}
      onLostPointerCapture={(Event) => {
        if (Event.target === Event.currentTarget) Drag.current = null;
      }}
    >
      <rect width="600" height="400" fill="#15191b" />
      {Array.from({ length: 21 }, (_, Index) => (
        <path
          key={Index}
          d={`M${Index * 30} 0V400 M0 ${Index * 20}H600`}
          stroke="#ffffff09"
        />
      ))}
      <path d="M300 0V400M0 200H600" stroke="#ffffff22" strokeDasharray="4 5" />
      {Field.Components.map((Part, Index) => {
        const X =
            Part.Type === "Directional"
              ? 30
              : (Part.X / Field.Width + 0.5) * 600,
          Z =
            Part.Type === "Directional"
              ? 52 + Index * 34
              : (Part.Z / Field.Depth + 0.5) * 400,
          Selected = Part.Id === Selection;
        return (
          <g
            key={Part.Id}
            role="button"
            tabIndex={0}
            aria-label={`${Part.Name} position`}
            aria-pressed={Selected}
            opacity={Part.Enabled ? 1 : 0.4}
            onPointerDown={(Event) => {
              if (
                Event.button !== 0 ||
                Event.isPrimary === false ||
                Drag.current
              )
                return;
              Event.preventDefault();
              if (document.activeElement instanceof HTMLInputElement)
                document.activeElement.blur();
              Select(Part.Id);
              if (Part.Type !== "Directional") {
                Drag.current = { Id: Part.Id, PointerId: Event.pointerId };
                Event.currentTarget.ownerSVGElement.setPointerCapture(
                  Event.pointerId,
                );
              }
            }}
            onKeyDown={(Event) => {
              if (Event.key === "Enter" || Event.key === " ") {
                Event.preventDefault();
                Select(Part.Id);
              }
              if (
                Part.Type !== "Directional" &&
                ["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown"].includes(
                  Event.key,
                )
              ) {
                Event.preventDefault();
                Update(Part.Id, {
                  X:
                    Part.X +
                    (Event.key === "ArrowLeft"
                      ? -10
                      : Event.key === "ArrowRight"
                        ? 10
                        : 0),
                  Z:
                    Part.Z +
                    (Event.key === "ArrowUp"
                      ? -10
                      : Event.key === "ArrowDown"
                        ? 10
                        : 0),
                });
              }
            }}
          >
            {Part.Type !== "Directional" && (
              <ellipse
                cx={X}
                cy={Z}
                rx={(Part.Radius / Field.Width) * 600}
                ry={(Part.Radius / Field.Depth) * 400}
                fill={Selected ? "#d4be8b0e" : "#84c8bd08"}
                stroke={Selected ? "#d4be8b88" : "#84c8bd33"}
                strokeDasharray="5 5"
                pointerEvents="none"
              />
            )}
            <circle
              cx={X}
              cy={Z}
              r={Selected ? 16 : 13}
              fill="#222d2d"
              stroke={Selected ? "#e0c593" : "#7eaca3"}
            />
            <text
              x={X}
              y={Z + 4}
              textAnchor="middle"
              fill="#e8e1d3"
              fontSize="11"
            >
              {Index + 1}
            </text>
            <text
              x={X + 22}
              y={Z - 12}
              fill={Selected ? "#e0c593" : "#8eaaa4"}
              fontSize="11"
            >
              {Part.Name}
              {Part.Type === "Directional" ? " (global)" : ""}
            </text>
          </g>
        );
      })}
      <text x="12" y="20" fill="#9caaa5" fontSize="10">
        −X / −Z · METRES
      </text>
      <text x="470" y="384" fill="#9caaa5" fontSize="10">
        +X / +Z
      </text>
    </svg>
  );
}

function WindNumber({ Value, Commit, ...Props }) {
  const [Draft, Write] = useState(String(Value));
  useEffect(() => Write(String(Value)), [Value]);
  const Save = () => {
    const Next = Number(Draft);
    if (Draft.trim() !== "" && Number.isFinite(Next)) {
      const Clamped = Math.max(Props.min, Math.min(Props.max, Next));
      Write(String(Clamped));
      Commit(Clamped);
    } else Write(String(Value));
  };
  return (
    <input
      {...Props}
      type="number"
      value={Draft}
      onChange={(Event) => Write(Event.target.value)}
      onBlur={Save}
      onKeyDown={(Event) => {
        if (Event.key === "Enter") {
          Event.preventDefault();
          Save();
        }
      }}
    />
  );
}

export default function WindEditor({
  Subject,
  Values,
  Change,
  Fields,
  SelectField,
  NewField,
  Rename,
  Close,
  Hidden,
}) {
  const Field = ResolveWind(Values),
    [Selection, Select] = useState(Field.Components[0]?.Id),
    [Playing, Play] = useState(true),
    [Vectors, ShowVectors] = useState(false),
    [Gradient, ShowGradient] = useState(true),
    [Particles, ShowParticles] = useState(true),
    Root = useRef(null),
    Latest = useRef(Field);
  Latest.current = Field;
  const Current = Field.Components.find((Item) => Item.Id === Selection);
  const Store = (Next) => {
    const Clean = ResolveWind({ WindField: Next });
    Latest.current = Clean;
    Change("WindField", Clean);
  };
  const Update = (Id, Next) =>
    Store({
      ...Latest.current,
      Components: Latest.current.Components.map((Item) =>
        Item.Id === Id ? { ...Item, ...Next } : Item,
      ),
    });
  const Add = (Type) => {
    if (Field.Components.length >= 64) return;
    const Part = NewWindComponent(Type);
    const Names = new Set(Field.Components.map((Item) => Item.Name));
    let Number = 1;
    while (Names.has(`${Type} ${Number}`)) Number++;
    Part.Name = `${Type} ${Number}`;
    Part.X = Type === "Directional" ? 0 : Field.Width * 0.12;
    Part.Z = Type === "Directional" ? 0 : -Field.Depth * 0.08;
    Store({ ...Field, Components: [...Field.Components, Part] });
    Select(Part.Id);
  };
  useEffect(() => {
    const Before = document.activeElement;
    Root.current.querySelector("button")?.focus();
    return () => Before?.isConnected && Before.focus();
  }, []);
  const Key = (Event) => {
    if (Event.key === "Escape") {
      Event.stopPropagation();
      Close();
    }
    if (Event.key === "Tab") {
      const Nodes = [
        ...Root.current.querySelectorAll(
          'button:not(:disabled),input:not(:disabled),select:not(:disabled),[tabindex="0"]',
        ),
      ].filter((Node) => Node.getClientRects().length);
      const First = Nodes[0],
        Last = Nodes.at(-1);
      if (Event.shiftKey && document.activeElement === First) {
        Event.preventDefault();
        Last.focus();
      } else if (!Event.shiftKey && document.activeElement === Last) {
        Event.preventDefault();
        First.focus();
      }
    }
  };
  const NumberField = (Label, Key, Min, Max, Step = 1) => (
    <label key={Key}>
      {Label}
      <WindNumber
        key={Current.Id + Key}
        aria-label={`Component ${Label}`}
        min={Min}
        max={Max}
        step={Step}
        Value={Current[Key]}
        disabled={
          Current.Type === "Directional" && ["X", "Z", "Radius"].includes(Key)
        }
        Commit={(Next) => Update(Current.Id, { [Key]: Next })}
      />
    </label>
  );
  return (
    <div className="wind-editor-backdrop">
      <section
        className="wind-editor"
        role="dialog"
        aria-modal="true"
        aria-label="WindEditor"
        ref={Root}
        onKeyDown={Key}
      >
        <header className="wind-editor-header">
          <div>
            <span className="eyebrow">ENVIRONMENT / FIELD AUTHORING</span>
            <h1>WindEditor</h1>
          </div>
          <select
            aria-label="Edited wind field"
            value={Subject.Id}
            onChange={(Event) => SelectField(Event.target.value)}
          >
            {Fields.map((Item) => (
              <option key={Item.Id} value={Item.Id}>
                {Item.Name}
              </option>
            ))}
          </select>
          <button onClick={NewField}>+ Wind field</button>
          <button onClick={() => Play(!Playing)} aria-pressed={!Playing}>
            {Playing ? "Pause preview" : "Resume preview"}
          </button>
          <button
            className="wind-close"
            aria-label="Close WindEditor"
            onClick={Close}
          >
            ×
          </button>
        </header>
        <div className="wind-editor-body">
          <aside className="wind-component-list">
            <label className="wind-name">
              Field name
              <input
                aria-label="Wind field name"
                value={Subject.Name}
                maxLength={80}
                onChange={(Event) => Rename(Event.target.value)}
              />
            </label>
            <div className="wind-section-head">
              <h3>Components</h3>
              <small>{Field.Components.length} / 64</small>
            </div>
            <div className="wind-add-buttons">
              {WindTypes.map((Type) => (
                <button
                  key={Type}
                  disabled={Field.Components.length >= 64}
                  onClick={() => Add(Type)}
                >
                  + {Type}
                </button>
              ))}
            </div>
            <div className="wind-parts">
              {Field.Components.map((Part, Index) => (
                <div
                  className={Part.Id === Selection ? "selected" : ""}
                  key={Part.Id}
                >
                  <button
                    className="wind-enable"
                    aria-label={`Enable ${Part.Name}`}
                    aria-pressed={Part.Enabled}
                    onClick={() => Update(Part.Id, { Enabled: !Part.Enabled })}
                  >
                    <i className={Part.Enabled ? "green" : "red"} />
                  </button>
                  <button
                    aria-label={`Select ${Part.Name}`}
                    onClick={() => Select(Part.Id)}
                  >
                    <small>
                      {String(Index + 1).padStart(2, "0")} / {Part.Type}
                    </small>
                    <span>{Part.Name}</span>
                  </button>
                </div>
              ))}
            </div>
            <div className="wind-dimensions">
              <h3>Preview bounds · m</h3>
              {["Width", "Depth"].map((Key) => (
                <label key={Key}>
                  {Key}
                  <WindNumber
                    aria-label={`Field ${Key}`}
                    min={100}
                    max={10000}
                    step={100}
                    Value={Field[Key]}
                    Commit={(Next) => Store({ ...Field, [Key]: Next })}
                  />
                </label>
              ))}
            </div>
          </aside>
          <div className="wind-editor-main">
            <div className="wind-view-pair">
              <section>
                <div className="wind-section-head">
                  <h2>Place components</h2>
                  <span>01 / XZ PLANE</span>
                </div>
                <Placement
                  Field={Field}
                  Selection={Selection}
                  Select={Select}
                  Update={Update}
                />
                <p>
                  Drag numbered handles · arrow keys move 10 m · directional
                  flow is global.
                </p>
              </section>
              <section>
                <div className="wind-section-head">
                  <h2>Combined vector field</h2>
                  <span>02 / EVALUATED SUM</span>
                </div>
                <WindCanvas
                  Field={Field}
                  Active={!Hidden}
                  Playing={Playing}
                  Vectors={Vectors}
                  Gradient={Gradient}
                  Particles={Particles}
                />
                <div className="wind-view-switches">
                  {[
                    ["Vectors", Vectors, ShowVectors],
                    ["Speed gradient", Gradient, ShowGradient],
                    ["Particles", Particles, ShowParticles],
                  ].map(([Label, On, Set]) => (
                    <label key={Label}>
                      <input
                        type="checkbox"
                        checked={On}
                        onChange={(Event) => Set(Event.target.checked)}
                      />
                      {Label}
                    </label>
                  ))}
                </div>
              </section>
            </div>
            <section className="wind-component-properties">
              <div className="wind-section-head">
                <h2>
                  {Current ? "Selected component" : "Select or add a component"}
                </h2>
                {Current && (
                  <button
                    aria-label="Remove wind component"
                    onClick={() => {
                      Store({
                        ...Field,
                        Components: Field.Components.filter(
                          (Item) => Item.Id !== Current.Id,
                        ),
                      });
                      Select(
                        Field.Components.find((Item) => Item.Id !== Current.Id)
                          ?.Id,
                      );
                    }}
                  >
                    Remove component
                  </button>
                )}
              </div>
              {Current && (
                <>
                  <div className="wind-property-grid">
                    <label>
                      Name
                      <input
                        aria-label="Component name"
                        maxLength={80}
                        value={Current.Name}
                        onChange={(Event) =>
                          Update(Current.Id, { Name: Event.target.value })
                        }
                      />
                    </label>
                    <label>
                      Type
                      <select
                        aria-label="Component type"
                        value={Current.Type}
                        onChange={(Event) =>
                          Update(Current.Id, { Type: Event.target.value })
                        }
                      >
                        {WindTypes.map((Type) => (
                          <option key={Type}>{Type}</option>
                        ))}
                      </select>
                    </label>
                    {NumberField("Strength (m/s)", "Strength", 0, 100, 0.5)}
                    {NumberField(
                      "X (m)",
                      "X",
                      -Field.Width / 2,
                      Field.Width / 2,
                    )}
                    {NumberField(
                      "Z (m)",
                      "Z",
                      -Field.Depth / 2,
                      Field.Depth / 2,
                    )}
                    {NumberField("Radius (m)", "Radius", 10, 10000)}
                    {["Directional", "Gust"].includes(Current.Type) &&
                      NumberField("Bearing (°)", "Bearing", 0, 360)}
                    {Current.Type === "Gust" &&
                      NumberField("Frequency (Hz)", "Frequency", 0, 5, 0.05)}
                  </div>
                  <p>
                    {Current.Type === "Tornado"
                      ? "Tangential vortex with inward pull; radius controls the local influence. This is a horizontal slice, not a vertical tornado simulation."
                      : Current.Type === "Gust"
                        ? "Local directional pulse with smooth radial falloff. Frequency drives its time variation."
                        : Current.Type === "Radial"
                          ? "Local outward flow with smooth radial falloff."
                          : "Uniform background flow across the field. Bearing is the direction of travel: 0° north, 90° east."}
                  </p>
                </>
              )}
            </section>
          </div>
        </div>
        <footer>
          <span>
            <i className={Hidden ? "red" : "green"} />
            {Hidden
              ? "Field hidden · zero output"
              : "All enabled components evaluated together"}
          </span>
          <span>
            Saved with scene · HTML evaluator / native wind and cloud simulation
            pending
          </span>
        </footer>
      </section>
    </div>
  );
}
