import React, { useState, useEffect, useId } from "react";
import TransformPanel from "./TransformPanel.jsx";
import {
  FogShapes,
  ResolveFogShape,
  FogGeometry,
  ValidFogPolygon,
} from "./FogShape.js";
import "./FogShapePanel.css";
function Dimension({
  Name,
  Value,
  Min = 0.01,
  Max = 200000,
  Step = 1,
  Change,
}) {
  const [Draft, Write] = useState(String(Value));
  useEffect(() => Write(String(Value)), [Value]);
  const Commit = () => {
    const Next = Number(Draft);
    if (Draft.trim() && Number.isFinite(Next)) {
      const Result = Math.max(Min, Math.min(Max, Next));
      Change(Result);
      Write(String(Result));
    } else Write(String(Value));
  };
  return (
    <label>
      {Name}
      <input
        type="number"
        aria-label={`Fog ${Name}`}
        value={Draft}
        min={Min}
        max={Max}
        step={Step}
        onChange={(Event) => Write(Event.target.value)}
        onBlur={Commit}
        onKeyDown={(Event) => {
          if (Event.key === "Enter") Commit();
        }}
      />
    </label>
  );
}
export default function FogShapePanel({ Values, Change }) {
  const Glow = useId();
  const Shape = ResolveFogShape(Values),
    Mesh = FogGeometry(Shape),
    Centre = Array.isArray(Values.Centre) ? Values.Centre : [0, 0, 0];
  const [Draft, Write] = useState(
      Shape.Points.map((Point) => Point.join(", ")).join("\n"),
    ),
    [Error, Report] = useState("");
  const Store = (Key, Value) => Change("FogShape", { ...Shape, [Key]: Value });
  const Raw = Mesh.Vertices.map(([X, Y, Z]) => [
    (X - Y) * Math.SQRT1_2,
    (X + Y) / Math.sqrt(6) - Z * Math.sqrt(2 / 3),
  ]);
  const Low = [0, 1].map((Axis) =>
      Math.min(...Raw.map((Point) => Point[Axis])),
    ),
    High = [0, 1].map((Axis) => Math.max(...Raw.map((Point) => Point[Axis])));
  const Scale = Math.min(
    270 / Math.max(0.001, High[0] - Low[0]),
    190 / Math.max(0.001, High[1] - Low[1]),
  );
  const Projected = Raw.map(([X, Y]) => [
    160 + (X - (Low[0] + High[0]) / 2) * Scale,
    120 + (Y - (Low[1] + High[1]) / 2) * Scale,
  ]);
  const Apply = () => {
    const Points = Draft.trim()
      .split(/\n/)
      .map((Line) =>
        Line.trim()
          .split(/[,\s]+/)
          .map(Number),
      );
    if (
      Points.length < 3 ||
      Points.length > 32 ||
      Points.some(
        (Point) =>
          Point.length !== 2 ||
          Point.some(
            (Value) => !Number.isFinite(Value) || Math.abs(Value) > 100000,
          ),
      ) ||
      !ValidFogPolygon(Points)
    ) {
      Report(
        "Use 3–32 ordered X, Y vertices. The footprint must have area and no crossing edges; coordinates must be within ±100000 m.",
      );
      return;
    }
    Store("Points", Points);
    Report("");
  };
  const Round = (Value) =>
    (Math.abs(Value) < 0.00005 ? 0 : Value).toLocaleString("en", {
      maximumFractionDigits: 3,
    });
  return (
    <section
      className="property-card fog-shape-card"
      data-card="Fog volume shape"
    >
      <h3>Fog volume shape</h3>
      <label className="fog-shape-choice">
        Shape
        <select
          aria-label="Fog volume shape"
          value={Shape.Type}
          onChange={(Event) => Store("Type", Event.target.value)}
        >
          {FogShapes.map((Type) => (
            <option key={Type} value={Type}>
              {{
                Box: "Box / rectangular volume",
                Diamond: "Diamond / octahedron",
                Prism: "Regular polygon prism",
                Custom: "Custom polygon extrusion",
              }[Type] || Type}
            </option>
          ))}
        </select>
      </label>
      <svg
        className="fog-shape-preview"
        role="img"
        aria-label={`${Shape.Type} fog volume preview`}
        data-shape={Shape.Type}
        viewBox="0 0 320 250"
      >
        <defs>
          <radialGradient id={Glow}>
            <stop stopColor="#adcbd6" stopOpacity=".16" />
            <stop offset="1" stopColor="#adcbd6" stopOpacity="0" />
          </radialGradient>
        </defs>
        <rect width="320" height="250" rx="15" fill="#151b1d" />
        <ellipse cx="160" cy="125" rx="155" ry="112" fill={`url(#${Glow})`} />
        {Mesh.Faces.map((Face, Index) => (
          <polygon
            key={Index}
            points={Face.map((I) => Projected[I].join(",")).join(" ")}
            fill="#b8d9e107"
          />
        ))}
        {Mesh.Edges.map(([A, B], Index) => (
          <line
            key={Index}
            x1={Projected[A][0]}
            y1={Projected[A][1]}
            x2={Projected[B][0]}
            y2={Projected[B][1]}
            stroke="#afceda"
            strokeWidth=".65"
            opacity={
              Shape.Type === "Sphere" || Shape.Type === "Ellipsoid" ? 0.4 : 0.65
            }
          />
        ))}
        <text x="14" y="232" fill="#81969f" fontSize="9">
          {Shape.Type.toUpperCase()} · Z UP · FIT TO VOLUME
        </text>
      </svg>
      <div className="fog-dimensions">
        {(["Sphere", "Cylinder", "Cone", "Prism"].includes(Shape.Type)
          ? ["Radius"]
          : Shape.Type === "Custom"
            ? []
            : ["Width", "Depth"]
        ).map((Key) => (
          <Dimension
            key={Key}
            Name={`${Key} (m)`}
            Max={Key === "Radius" ? 100000 : 200000}
            Value={Shape[Key]}
            Change={(Value) => Store(Key, Value)}
          />
        ))}
        {Shape.Type !== "Sphere" && (
          <Dimension
            Name="Height (m)"
            Value={Shape.Height}
            Change={(Value) => Store("Height", Value)}
          />
        )}
        {Shape.Type === "Prism" && (
          <Dimension
            Name="Sides"
            Min={3}
            Max={16}
            Value={Shape.Sides}
            Change={(Value) => Store("Sides", Math.round(Value))}
          />
        )}
      </div>
      {Shape.Type === "Custom" && (
        <div className="fog-custom">
          <label>
            Footprint vertices · X, Y metres
            <textarea
              aria-label="Custom fog footprint"
              rows={6}
              value={Draft}
              onChange={(Event) => Write(Event.target.value)}
            />
          </label>
          <button onClick={Apply}>Apply footprint</button>
          {Error && <p role="alert">{Error}</p>}
          <p>
            Ordered polygon, extruded along Z. Supports concave footprints
            without crossing edges; this is not an arbitrary mesh/SDF importer.
          </p>
        </div>
      )}
      <TransformPanel
        Values={Values}
        Change={Change}
        Compact
        Space="WORLD SPACE"
        Rows={[["Centre", "m", [0, 0, 0], -100000, 100000, 1]]}
      />
      <div className="fog-derived-bounds">
        <h4>Calculated world bounds</h4>
        <table aria-label="Calculated fog bounds">
          <thead>
            <tr>
              <th>Metres</th>
              {["X", "Y", "Z"].map((Axis) => (
                <th key={Axis}>{Axis}</th>
              ))}
            </tr>
          </thead>
          <tbody>
            {[
              ["Min", Mesh.Min.map((V, I) => V + (Number(Centre[I]) || 0))],
              ["Max", Mesh.Max.map((V, I) => V + (Number(Centre[I]) || 0))],
              ["Size", Mesh.Size],
            ].map(([Label, List]) => (
              <tr key={Label}>
                <th>{Label}</th>
                {List.map((Value, Index) => (
                  <td
                    key={Index}
                    data-bound={`${Label}-${["X", "Y", "Z"][Index]}`}
                    data-value={Value}
                  >
                    {Round(Value)}
                  </td>
                ))}
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      <p>
        Shape and dimensions determine the bounds—not a fixed box. The preview
        fits its volume; centre changes update the world bounds. HTML authoring
        only; native fog masking is deferred.
      </p>
    </section>
  );
}
