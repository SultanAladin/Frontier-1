import React, {
  createContext,
  useContext,
  useRef,
  useState,
  useId,
  useEffect,
} from "react";
import { Solar } from "./Solar.mjs";
import "./LiveGraph.css";
export const GraphContext = createContext(null);
export const Clamp = (V, A, B) => Math.max(A, Math.min(B, Number(V) || 0));
const Format = (V) =>
  Math.abs(Number(V)) > 0 && Math.abs(Number(V)) < 0.001
    ? Number(V).toExponential(2)
    : Number(V).toLocaleString("en", { maximumFractionDigits: 3 });
function useGraphWidth(Root) {
  const [Width, Measure] = useState(420);
  useEffect(() => {
    if (!Root.current) return;
    const Observer = new ResizeObserver((Entries) =>
      Measure(Math.max(180, Math.round(Entries[0].contentRect.width))),
    );
    Observer.observe(Root.current);
    return () => Observer.disconnect();
  });
  return Width;
}
export function Plot({
  Name,
  Domain = [0, 1],
  Range = [0, 1],
  Series,
  Value,
  Change,
  XUnit = "",
  YUnit = "",
  Note = "Drag to sample · arrow keys for precision",
}) {
  const Id = useId(),
    Root = useRef(null),
    Drag = useRef(false),
    [Hover, Inspect] = useState(null);
  const Width = useGraphWidth(Root);
  const X = Clamp(Hover ?? Value ?? Domain[0], ...Domain),
    PX = (V) => 42 + ((V - Domain[0]) / (Domain[1] - Domain[0])) * (Width - 60),
    PY = (V) =>
      174 - ((Clamp(V, ...Range) - Range[0]) / (Range[1] - Range[0])) * 146;
  const Sample = (Event) => {
    const Box = Root.current.getBoundingClientRect(),
      U = Clamp(
        ((Event.clientX - Box.left) / Box.width) * Width,
        42,
        Width - 18,
      );
    return Domain[0] + ((U - 42) / (Width - 60)) * (Domain[1] - Domain[0]);
  };
  const Move = (Event) => {
    const Next = Sample(Event);
    Inspect(Next);
    if (Drag.current) Change?.(Next);
  };
  return (
    <div className="live-plot" data-live-plot={Name}>
      <div className="live-plot-value">
        <span>
          {Format(X)} <small>{XUnit}</small>
        </span>
        <span>
          {Series.map((Line) => (
            <output key={Line.Name} style={{ color: Line.Colour || "#dfd3b8" }}>
              {Line.Name} {Format(Line.At(X))}
              <small> {YUnit}</small>
            </output>
          ))}
        </span>
      </div>
      <svg
        ref={Root}
        viewBox={`0 0 ${Width} 215`}
        role="slider"
        tabIndex={0}
        aria-label={Name}
        aria-valuemin={Domain[0]}
        aria-valuemax={Domain[1]}
        aria-valuenow={Value ?? Domain[0]}
        aria-valuetext={`${Format(Value ?? Domain[0])} ${XUnit}`}
        onPointerDown={(Event) => {
          if (Event.button !== 0) return;
          Drag.current = true;
          Root.current.setPointerCapture(Event.pointerId);
          Root.current.focus();
          Move(Event);
        }}
        onPointerMove={Move}
        onPointerUp={() => (Drag.current = false)}
        onPointerCancel={() => {
          Drag.current = false;
          Inspect(null);
        }}
        onLostPointerCapture={(Event) => {
          if (Event.target === Root.current) Drag.current = false;
        }}
        onPointerLeave={() => {
          if (!Drag.current) Inspect(null);
        }}
        onKeyDown={(Event) => {
          if (!["ArrowLeft", "ArrowRight", "Home", "End"].includes(Event.key))
            return;
          Event.preventDefault();
          const Next =
            Event.key === "Home"
              ? Domain[0]
              : Event.key === "End"
                ? Domain[1]
                : Clamp(
                    (Value ?? Domain[0]) +
                      ((Event.key === "ArrowLeft" ? -1 : 1) *
                        (Domain[1] - Domain[0])) /
                        (Event.shiftKey ? 1000 : 100),
                    ...Domain,
                  );
          Inspect(null);
          Change?.(Next);
        }}
      >
        <defs>
          <linearGradient id={Id} x2="0" y2="1">
            <stop
              stopColor={Series[0]?.Colour || "#dac7a2"}
              stopOpacity=".14"
            />
            <stop offset="1" stopColor="#212121" stopOpacity="0" />
          </linearGradient>
        </defs>
        {[0, 0.25, 0.5, 0.75, 1].map((T) => (
          <g key={T}>
            <path
              d={`M42 ${28 + 146 * T}H${Width - 18}`}
              stroke="#ffffff16"
              strokeDasharray="4 7"
            />
            <text x="35" y={32 + 146 * T} textAnchor="end">
              {Format(Range[1] - (Range[1] - Range[0]) * T)}
            </text>
          </g>
        ))}
        {Series.map((Line, Index) => {
          const Samples = [
            ...Array.from(
              { length: 101 },
              (_, I) => Domain[0] + ((Domain[1] - Domain[0]) * I) / 100,
            ),
            ...(Line.Points || []),
          ]
            .filter((A) => A >= Domain[0] && A <= Domain[1])
            .sort((A, B) => A - B);
          const Points = Samples.map(
            (A, I) => `${I ? "L" : "M"}${PX(A)} ${PY(Line.At(A))}`,
          ).join(" ");
          return (
            <g key={Line.Name}>
              {Index === 0 && (
                <path
                  d={`${Points}L${Width - 18} 174H42Z`}
                  fill={`url(#${Id})`}
                />
              )}
              <path
                data-series={Line.Name}
                d={Points}
                stroke={Line.Colour || "#d9d5cb"}
                strokeWidth="1.6"
                fill="none"
              />
              <circle
                cx={PX(X)}
                cy={PY(Line.At(X))}
                r="4"
                fill={Line.Colour || "#e7dfcf"}
              />
            </g>
          );
        })}
        <path d={`M${PX(X)} 28V174`} stroke="#e5d7bd77" strokeDasharray="3 5" />
        {[0, 0.5, 1].map((T) => (
          <text key={T} x={42 + (Width - 60) * T} y="195" textAnchor="middle">
            {Format(Domain[0] + (Domain[1] - Domain[0]) * T)} {XUnit}
          </text>
        ))}
      </svg>
      <p className="graph-note">{Note}</p>
    </div>
  );
}
// Exact values/ranges of controls in this card, not invented simulation telemetry.
export function PropertyGraph({ Children, Title }) {
  const Context = useContext(GraphContext),
    [Open, Expand] = useState(true),
    [Picked, Pick] = useState(0),
    Root = useRef(null),
    Drag = useRef(null);
  const Width = useGraphWidth(Root);
  if (!Context) return null;
  const Fields = [];
  const Walk = (Nodes) =>
    React.Children.forEach(Nodes, (Node) => {
      if (!React.isValidElement(Node)) return;
      const Field = Node.props.Field;
      if (
        Field?.Control === "Slider" &&
        !Node.props.Disabled &&
        Number.isFinite(Field.Minimum) &&
        Number.isFinite(Field.Maximum)
      )
        Fields.push({
          Field,
          Value: Node.props.Value,
          Change: Node.props.Change,
        });
      else if (
        Field?.Control === "Colour" &&
        !Node.props.Disabled &&
        /^#[a-f0-9]{6}$/i.test(Node.props.Value)
      ) {
        const RGB = [1, 3, 5].map((Index) =>
          parseInt(Node.props.Value.slice(Index, Index + 2), 16),
        );
        RGB.forEach((Value, Index) =>
          Fields.push({
            Field: {
              Label: Field.Label + " " + ["R", "G", "B"][Index],
              Minimum: 0,
              Maximum: 255,
              Decimals: 0,
              Unit: "sRGB",
            },
            Value,
            Change: (Next) => {
              const Colour = [...RGB];
              Colour[Index] = Next;
              Node.props.Change(
                "#" +
                  Colour.map((C) =>
                    Math.round(C).toString(16).padStart(2, "0"),
                  ).join(""),
              );
            },
          }),
        );
      } else if (Node.props.children) Walk(Node.props.children);
    });
  Walk(Children);
  if (!Fields.length) return null;
  const Active = Fields[Math.min(Picked, Fields.length - 1)];
  const Edit = (Index, Event) => {
    const Item = Fields[Index],
      Box = Root.current.getBoundingClientRect(),
      T = Clamp(
        1 - (((Event.clientY - Box.top) / Box.height) * 210 - 22) / 140,
        0,
        1,
      );
    Item.Change(
      +(
        Item.Field.Minimum +
        T * (Item.Field.Maximum - Item.Field.Minimum)
      ).toFixed(Item.Field.Decimals ?? 2),
    );
    Pick(Index);
  };
  return (
    <div className="property-graph" data-property-graph={Title}>
      <button
        className="graph-disclosure"
        aria-expanded={Open}
        onClick={() => Expand(!Open)}
      >
        Live parameter map <span>{Open ? "−" : "+"}</span>
      </button>
      {Open && (
        <>
          <svg
            ref={Root}
            className="parameter-plot"
            viewBox={`0 0 ${Width} 210`}
            aria-label={Title + " parameter graph"}
            role="group"
            onPointerMove={(Event) => {
              if (Drag.current !== null) Edit(Drag.current, Event);
            }}
            onPointerUp={() => (Drag.current = null)}
            onPointerCancel={() => (Drag.current = null)}
            onLostPointerCapture={(Event) => {
              if (Event.target === Root.current) Drag.current = null;
            }}
          >
            {[0, 0.5, 1].map((T) => (
              <g key={T}>
                <path
                  d={`M32 ${162 - T * 140}H${Width - 15}`}
                  stroke="#ffffff16"
                  strokeDasharray="4 7"
                />
                <text x="27" y={166 - T * 140} textAnchor="end">
                  {T * 100}%
                </text>
              </g>
            ))}
            {Fields.map(({ Field, Value, Change }, Index) => {
              const X = 50 + (Index * (Width - 60)) / Fields.length,
                W = Math.min(55, (Width - 85) / Fields.length),
                Y =
                  162 -
                  Clamp(
                    (Value - Field.Minimum) / (Field.Maximum - Field.Minimum),
                    0,
                    1,
                  ) *
                    140;
              return (
                <g
                  key={Field.Label}
                  role="slider"
                  tabIndex={0}
                  aria-label={`${Title}: ${Field.Label} chart`}
                  aria-valuemin={Field.Minimum}
                  aria-valuemax={Field.Maximum}
                  aria-valuenow={Value}
                  onFocus={() => Pick(Index)}
                  onPointerEnter={() => Pick(Index)}
                  onPointerDown={(Event) => {
                    if (Event.button !== 0) return;
                    Drag.current = Index;
                    Root.current.setPointerCapture(Event.pointerId);
                    Event.currentTarget.focus();
                    Edit(Index, Event);
                  }}
                  onKeyDown={(Event) => {
                    if (
                      !["ArrowUp", "ArrowDown", "Home", "End"].includes(
                        Event.key,
                      )
                    )
                      return;
                    Event.preventDefault();
                    Change(
                      Event.key === "Home"
                        ? Field.Minimum
                        : Event.key === "End"
                          ? Field.Maximum
                          : +Clamp(
                              Value +
                                (Event.key === "ArrowUp" ? 1 : -1) *
                                  10 ** -(Field.Decimals ?? 2),
                              Field.Minimum,
                              Field.Maximum,
                            ).toFixed(Field.Decimals ?? 2),
                    );
                  }}
                >
                  <rect
                    x={X - 10}
                    y="12"
                    width={W + 20}
                    height="157"
                    fill="transparent"
                  />
                  <rect
                    x={X}
                    y={Y}
                    width={W}
                    height={162 - Y}
                    fill={Index === Picked ? "#c0ad8529" : "#b2c5b515"}
                  />
                  <path
                    d={`M${X} ${Y}h${W}`}
                    stroke={Index === Picked ? "#d2bb8c" : "#8e9b88"}
                  />
                  <circle
                    cx={X + W / 2}
                    cy={Y}
                    r="4"
                    fill={Index === Picked ? "#ebddc1" : "#bdc6b8"}
                  />
                  <text x={X + W / 2} y="185" textAnchor="middle">
                    {Fields.length > 2 ? Index + 1 : Field.Label.slice(0, 13)}
                  </text>
                </g>
              );
            })}
          </svg>
          <div className="parameter-readout">
            {Active.Field.Label} <strong>{Format(Active.Value)}</strong>{" "}
            {Active.Field.Unit}
          </div>
          <p className="graph-note">
            Actual authored values normalized to their control ranges · drag a
            column or use ↑/↓. Not runtime telemetry.
          </p>
        </>
      )}
    </div>
  );
}
export function FogDensity(Kind, V, Height, AuthoredPreview = false) {
  if (!V("Enabled") && !AuthoredPreview) return 0;
  if (Kind === "height-fog")
    return (
      V("Density") *
      Math.exp(-Math.max(0, Height) / Math.max(10, V("Falloff Height")))
    );
  return Kind === "aerial-fog"
    ? V("Density") * 0.001
    : V("Density") * V("Coverage") * 0.01;
}
function DecimalMetric({ Value }) {
  const Parts = String(Value).split(".");
  return (
    <>
      {Parts[0]}
      {Parts.length > 1 && (
        <span className="graph-fraction">.{Parts.slice(1).join(".")}</span>
      )}
    </>
  );
}
export function FogGraph({ Kind, V, Change, Density = false }) {
  const Altitude = 2,
    Distance = V("Probe distance") ?? 200,
    Local = Kind === "local-fog";
  const Sigma = FogDensity(Kind, V, Altitude, true),
    At = (D) => Math.exp(-Sigma * Math.max(0, D)) * 100;
  return (
    <>
      <div className="graph-status">
        <i className={V("Enabled") ? "green" : "red"} />
        {V("Enabled")
          ? "LIVE AUTHORING PROBE"
          : "AUTHORED PREVIEW · MEDIUM DISABLED"}
      </div>
      <div className="graph-metric">
        <DecimalMetric
          Value={
            Density
              ? Sigma > 0 && Sigma < 0.0001
                ? Sigma.toExponential(2)
                : Sigma.toFixed(4)
              : At(Distance).toFixed(2)
          }
        />
        <small>{Density ? "m⁻¹" : "%"}</small>
      </div>
      <p>
        {Density
          ? "Extinction at probe altitude"
          : `${V("Enabled") ? "Light" : "Preview light"} transmitted after the selected in-medium distance`}
      </p>
      <Plot
        Name={Density ? "Fog altitude probe" : "Fog distance probe"}
        Domain={Density ? [0, 3000] : [0, 2000]}
        Range={Density ? [0, Math.max(0.001, V("Density"))] : [0, 100]}
        Series={[
          {
            Name: Density ? "Density" : "Transmission",
            At: Density ? (H) => FogDensity(Kind, V, H, true) : At,
            Colour: Density ? "#a9c5bc" : "#d8cbb0",
          },
        ]}
        Value={Density ? Altitude : Distance}
        Change={(Next) =>
          Change(
            Density ? "Probe altitude" : "Probe distance",
            +Next.toFixed(2),
          )
        }
        XUnit="m"
        YUnit={Density ? "m⁻¹" : "%"}
      />
      <div className="graph-probes">
        <label>
          Distance · m
          <input
            aria-label="Fog probe distance"
            type="number"
            min={0}
            max={2000}
            value={Distance}
            onChange={(E) =>
              Change("Probe distance", Clamp(E.target.value, 0, 2000))
            }
          />
        </label>
      </div>
      <p className="graph-note">
        {Kind === "height-fog"
          ? "σ(z) = density × exp(−z / falloff height); T = exp(−σ × distance). Both cards share this probe."
          : Local
            ? "Homogeneous authoring probe: σ = 0.01 × density × coverage / m. Does not integrate the volume shape."
            : "Illustrative aerial probe: σ = 0.001 × density / m; distance is travel through the medium after Start. Disabled state remains an authored preview. Not a native atmosphere march."}
      </p>
    </>
  );
}
export function SunGraph({ V, Change, Mode }) {
  const Base = {
    Latitude: V("Latitude"),
    Longitude: V("Longitude"),
    Month: V("Month"),
    Day: V("Day of Month"),
  };
  if (Mode === "day")
    return (
      <Plot
        Name="Sun daylight graph"
        Domain={[0, 24]}
        Range={[-90, 90]}
        Series={[
          {
            Name: "Elevation",
            At: (H) => Solar({ ...Base, LocalHours: H }).Elevation,
            Colour: "#deccaa",
          },
        ]}
        Value={V("Local Hours")}
        Change={(N) => Change("Local Hours", N)}
        XUnit="h"
        YUnit="°"
      />
    );
  if (Mode === "year")
    return (
      <Plot
        Name="Sun seasonal graph"
        Domain={[1, 365]}
        Range={[-90, 90]}
        Series={[
          {
            Name: "Noon elevation",
            At: (D) => {
              const DateValue = new Date(Date.UTC(2026, 0, Math.round(D)));
              return Solar({
                ...Base,
                Month: DateValue.getUTCMonth() + 1,
                Day: DateValue.getUTCDate(),
                LocalHours: 12,
              }).Elevation;
            },
            Colour: "#bbc6ac",
          },
        ]}
        Value={Math.round(
          (Date.UTC(2026, Base.Month - 1, Base.Day) - Date.UTC(2026, 0, 0)) /
            86400000,
        )}
        Change={(D) => {
          const DateValue = new Date(Date.UTC(2026, 0, Math.round(D)));
          Change("Month", DateValue.getUTCMonth() + 1);
          Change("Day of Month", DateValue.getUTCDate());
        }}
        XUnit="day"
        YUnit="°"
        Note="Drag to set the date · noon elevation through 2026 at this location. NOAA geometric model."
      />
    );
  if (Mode === "gain")
    return (
      <Plot
        Name="Sunlight gain graph"
        Domain={[0, 60]}
        Range={[0, 300]}
        Series={[
          {
            Name: "Combined gain",
            At: (I) => I * V("Direct"),
            Colour: "#d9c6a1",
          },
        ]}
        Value={V("Intensity")}
        Change={(N) => Change("Intensity", +N.toFixed(1))}
        XUnit="× intensity"
        YUnit="×"
        Note="Intensity × Direct · relative authored gain, not measured lux."
      />
    );
  const T = V("Temperature"),
    B = (L) => 1 / (Math.pow(L, 5) * Math.expm1(14387769 / (L * T))),
    Peak = Math.max(...Array.from({ length: 81 }, (_, I) => B(380 + I * 5)));
  return (
    <Plot
      Name="Sun colour spectrum"
      Domain={[380, 780]}
      Range={[0, 1]}
      Series={[
        { Name: "Relative power", At: (L) => B(L) / Peak, Colour: "#d2bead" },
      ]}
      Value={V("Spectrum wavelength") ?? 550}
      Change={(N) => Change("Spectrum wavelength", N)}
      XUnit="nm"
      Note="Normalized Planck spectrum for the temperature draft. RGB tint mode does not use this blackbody spectrum."
    />
  );
}

export function FogSpectrum({ V, Change }) {
  const Distance = V("Probe distance") ?? 200,
    Sigma = FogDensity("aerial-fog", V, 2, true),
    Path = Math.max(0, Distance),
    Mix = V("Mie Blend");
  return (
    <Plot
      Name="Aerial fog spectrum"
      Domain={[380, 780]}
      Range={[0, 100]}
      Series={[
        {
          Name: "Transmission",
          At: (L) =>
            100 *
            Math.exp(
              -Sigma *
                Path *
                ((1 - Mix) * Math.pow(550 / L, 4) +
                  Mix * Math.pow(550 / L, 1.3)),
            ),
          Colour: "#9eb8d0",
        },
      ]}
      Value={V("Spectral wavelength") ?? 550}
      Change={(N) => Change("Spectral wavelength", N)}
      XUnit="nm"
      YUnit="%"
      Note={`Drag to sample wavelength. Uses Density, Mie Blend and ${Distance.toFixed(0)} m of travel through the medium after Start; the disabled state remains an authored preview. At 550 nm this matches Visibility. Not native spectral rendering.`}
    />
  );
}
export function CloudSection({ V, Change, Local = false }) {
  const Thickness = Local ? V("Half Size")[2] * 2 : V("Thickness"),
    Base = Local ? V("Centre")[2] - Thickness / 2 : V("Base"),
    Margin = Math.max(Local ? 20 : 1000, Thickness * 0.5),
    Density = V("Enabled") === false ? 0 : V("Density") * V("Coverage");
  return (
    <Plot
      Name="Cloud vertical section"
      Domain={[Base - Margin, Base + Thickness + Margin]}
      Range={[0, 4]}
      Series={[
        {
          Name: "Density study",
          Points: [Base, Base + Thickness / 2, Base + Thickness],
          At: (Z) =>
            Z < Base || Z > Base + Thickness
              ? 0
              : Density *
                Math.sin((Math.PI * (Z - Base)) / Math.max(0.001, Thickness)) **
                  2,
          Colour: "#b0c3ce",
        },
      ]}
      Value={V("Cloud probe altitude") ?? Base + Thickness / 2}
      Change={(N) => Change("Cloud probe altitude", N)}
      XUnit="m Z"
      YUnit="×"
      Note="Drag to sample an illustrative unlit vertical envelope: density × coverage × sine². Uses actual base/centre and thickness; not native cloud-field sampling."
    />
  );
}
export function CloudBounds({ V, Change }) {
  const Root = useRef(null),
    Drag = useRef(null),
    Half = V("Half Size"),
    Centre = V("Centre"),
    Basis = [
      [Math.SQRT1_2, 1 / Math.sqrt(6)],
      [-Math.SQRT1_2, 1 / Math.sqrt(6)],
      [0, -Math.sqrt(2 / 3)],
    ],
    Scale = 85 / Math.max(1, ...Half),
    Project = (P) => [
      170 + P.reduce((S, N, I) => S + N * Basis[I][0] * Scale, 0),
      105 + P.reduce((S, N, I) => S + N * Basis[I][1] * Scale, 0),
    ],
    Vertices = Array.from({ length: 8 }, (_, I) =>
      Project(Half.map((N, A) => N * ((I >> A) & 1 ? 1 : -1))),
    );
  const Position = (E) => {
    const P = Root.current.createSVGPoint();
    P.x = E.clientX;
    P.y = E.clientY;
    return P.matrixTransform(Root.current.getScreenCTM().inverse());
  };
  const Write = (Axis, Next) =>
    Change(
      "Half Size",
      Half.map((N, I) =>
        I === Axis ? +Clamp(Next, 0.001, 100000).toFixed(3) : N,
      ),
    );
  return (
    <>
      <svg
        ref={Root}
        className="cloud-bounds-plot"
        viewBox="0 0 340 230"
        aria-label="Live local cloud bounds"
        onPointerMove={(E) => {
          if (!Drag.current) return;
          const P = Position(E),
            { Axis, Start, Value, Unit } = Drag.current,
            B = Basis[Axis],
            Delta =
              ((P.x - Start.x) * B[0] + (P.y - Start.y) * B[1]) /
              (B[0] ** 2 + B[1] ** 2);
          Write(Axis, Value + Delta / Unit);
        }}
        onPointerUp={() => (Drag.current = null)}
        onPointerCancel={() => (Drag.current = null)}
        onLostPointerCapture={() => (Drag.current = null)}
      >
        {Vertices.flatMap((P, I) =>
          [0, 1, 2]
            .filter((A) => !(I & (1 << A)))
            .map((A) => {
              const Q = Vertices[I | (1 << A)];
              return (
                <path
                  key={I + "-" + A}
                  d={`M${P[0]} ${P[1]}L${Q[0]} ${Q[1]}`}
                  fill="none"
                  stroke="#9eb9d7"
                  strokeWidth="1.3"
                />
              );
            }),
        )}
        {Half.map((N, Axis) => {
          const P = Project(Half.map((H, I) => (I === Axis ? H : 0)));
          return (
            <g
              key={Axis}
              role="slider"
              tabIndex={0}
              aria-label={`Cloud half size ${"XYZ"[Axis]} graph`}
              aria-valuemin={0.001}
              aria-valuemax={100000}
              aria-valuenow={N}
              onPointerDown={(E) => {
                if (E.button !== 0) return;
                E.currentTarget.focus();
                Drag.current = {
                  Axis,
                  Start: Position(E),
                  Value: N,
                  Unit: Scale,
                };
                Root.current.setPointerCapture(E.pointerId);
              }}
              onKeyDown={(E) => {
                if (["ArrowUp", "ArrowDown"].includes(E.key)) {
                  E.preventDefault();
                  Write(
                    Axis,
                    N + (E.key === "ArrowUp" ? 1 : -1) * (E.shiftKey ? 10 : 1),
                  );
                }
              }}
            >
              <circle
                cx={P[0]}
                cy={P[1]}
                r="10"
                fill={["#cb887f", "#99bb96", "#90b6da"][Axis]}
              />
              <text x={P[0] + 14} y={P[1] + 4}>
                {"XYZ"[Axis]}
              </text>
            </g>
          );
        })}
      </svg>
      <p className="graph-note">
        Drag the X / Y / Z handles to resize · arrow keys ±1 m · auto-fit, Z up.
      </p>
      <div className="cloud-bounds-data">
        {["Min", "Max", "Size"].map((Name, I) => (
          <div key={Name}>
            <span>{Name} · m</span>
            <output>
              {Half.map((H, A) =>
                Format(I === 2 ? H * 2 : Centre[A] + (I === 0 ? -H : H)),
              ).join(" / ")}
            </output>
          </div>
        ))}
      </div>
    </>
  );
}
