import React, { useState, useRef, useEffect } from "react";
import { createPortal } from "react-dom";
import { Plot, Clamp } from "./LiveGraph.jsx";
export const AtmospherePresets = [
  {
    Name: "Arctic clear",
    X: 0.7,
    Y: 0.08,
    Colour: "#a5cadb",
    Settings: {
      Ozone: 1.3,
      "Rayleigh Scale H": 6500,
      "Mie Scale H": 500,
      "Sky Tint": "#cae6ff",
      "Ground Albedo": "#bbc9ce",
    },
  },
  {
    Name: "Alpine",
    X: 0.45,
    Y: 0.25,
    Colour: "#719fc5",
    Settings: {
      Ozone: 1.1,
      "Rayleigh Scale H": 7000,
      "Mie Scale H": 700,
      "Sky Tint": "#d5e6ff",
    },
  },
  {
    Name: "Temperate Earth",
    X: 1,
    Y: 1,
    Colour: "#769ec1",
    Settings: {
      Ozone: 1.2,
      "Rayleigh Scale H": 8000,
      "Mie Scale H": 1200,
      "Sky Tint": "#ffffff",
    },
  },
  {
    Name: "Tropical humid",
    X: 1.4,
    Y: 2,
    Colour: "#8cbdc0",
    Settings: {
      Ozone: 1,
      "Rayleigh Scale H": 9000,
      "Mie Scale H": 2400,
      "Sky Tint": "#e1f4ee",
    },
  },
  {
    Name: "Desert dust",
    X: 0.9,
    Y: 3.4,
    Colour: "#c7a784",
    Settings: {
      Ozone: 0.5,
      "Mie Anisotropy": 0.86,
      "Mie Scale H": 3000,
      "Sky Tint": "#edc4a3",
      "Ground Albedo": "#9a7451",
    },
  },
  {
    Name: "Urban haze",
    X: 1.8,
    Y: 3,
    Colour: "#a7aa99",
    Settings: { Ozone: 0.7, "Mie Scale H": 1500, "Sky Tint": "#ddd4b6" },
  },
  {
    Name: "Thick smog",
    X: 2.5,
    Y: 5.6,
    Colour: "#9f9270",
    Settings: {
      Ozone: 0.3,
      "Mie Scale H": 800,
      "Sky Tint": "#c9ad80",
      "Sky Brightness": 0.65,
    },
  },
  {
    Name: "Mars-like dust",
    X: 0.18,
    Y: 0.65,
    Colour: "#c68770",
    Settings: {
      Ozone: 0,
      "Planet radius": 3390,
      Atmosphere: 45000,
      "Rayleigh Scale H": 10000,
      "Mie Scale H": 3500,
      "Sky Tint": "#df9d80",
      "Ground Albedo": "#80513e",
    },
  },
  {
    Name: "Dense golden haze",
    X: 3.7,
    Y: 4.5,
    Colour: "#c4b177",
    Settings: {
      Ozone: 0,
      Atmosphere: 100000,
      "Rayleigh Scale H": 14000,
      "Mie Scale H": 4000,
      "Sky Tint": "#e7c895",
    },
  },
];
const Defaults = {
  Ozone: 1.2,
  "Mie Anisotropy": 0.78,
  "Rayleigh Scale H": 8000,
  "Mie Scale H": 1200,
  Atmosphere: 60000,
  "Sky Tint": "#ffffff",
  "Sky Brightness": 1,
  "Ground Albedo": "#796e60",
  "Planet radius": 6371,
  "Horizon Glow": 1,
  "White Line": 1,
};
function Hex(Text) {
  return /^#[\da-f]{6}$/i.test(Text)
    ? [1, 3, 5].map((I) => parseInt(Text.slice(I, I + 2), 16) / 255)
    : [1, 1, 1];
}
export function AtmosphereProfile({ V, Change, Mode }) {
  const Probe =
    V("Atmosphere probe " + Mode) ?? (Mode === "height" ? 8000 : 550);
  const Domain =
    Mode === "height"
      ? [0, V("Atmosphere")]
      : Mode === "haze"
        ? [0, 10]
        : [380, 780];
  let Series, Range, Note;
  if (Mode === "height") {
    Series = [
      {
        Name: "Molecules",
        At: (H) => Math.exp(-H / V("Rayleigh Scale H")),
        Colour: "#9dbdd6",
      },
      {
        Name: "Aerosols",
        At: (H) => Math.exp(-H / V("Mie Scale H")),
        Colour: "#d2b88d",
      },
    ];
    Range = [0, 1];
    Note =
      "Relative density exp(−altitude / scale height). Both scale heights contribute.";
  } else if (Mode === "haze") {
    Series = [
      {
        Name: "Transmission",
        At: (X) => 100 * Math.exp(-0.2 * V("Mie") * X),
        Colour: "#cbbd9b",
      },
    ];
    Range = [0, 100];
    Note =
      "Illustrative normalized path: exp(−0.2 × Mie × path), not measured visibility.";
  } else if (Mode === "ozone") {
    Series = [
      {
        Name: "Transmission",
        At: (L) =>
          100 * Math.exp(-V("Ozone") * Math.exp(-Math.pow((L - 600) / 80, 2))),
        Colour: "#b8a3c9",
      },
    ];
    Range = [0, 100];
    Note =
      "Illustrative visible-band absorption envelope; not a calibrated ozone cross-section.";
  } else {
    Series = [
      {
        Name: "Rayleigh",
        At: (L) => V("Rayleigh") * (550 / L) ** 4,
        Colour: "#98bad6",
      },
      {
        Name: "Mie",
        At: (L) => V("Mie") * (550 / L) ** 1.3,
        Colour: "#c9b997",
      },
    ];
    Range = [0, 20];
    Note =
      "Relative wavelength response. Rayleigh λ⁻⁴; illustrative aerosol λ⁻¹·³.";
  }
  return (
    <Plot
      Name={"Atmosphere " + Mode + " graph"}
      Domain={Domain}
      Range={Range}
      Series={Series}
      Value={Clamp(Probe, ...Domain)}
      Change={(Next) => Change("Atmosphere probe " + Mode, Next)}
      XUnit={Mode === "height" ? "m" : Mode === "haze" ? "path" : "nm"}
      YUnit={Mode === "haze" || Mode === "ozone" ? "%" : ""}
      Note={Note}
    />
  );
}
function Atlas({ V, Change }) {
  const Root = useRef(null),
    Drag = useRef(false),
    [Hover, Inspect] = useState(null);
  const Set = (E) => {
    const Point = Root.current.createSVGPoint();
    Point.x = E.clientX;
    Point.y = E.clientY;
    const Position = Point.matrixTransform(
      Root.current.getScreenCTM().inverse(),
    );
    const X = Clamp(Position.x, 40, 465),
      Y = Clamp(Position.y, 22, 264);
    Change("Rayleigh", +((4 * (X - 40)) / 425).toFixed(2));
    Change("Mie", +((6 * (264 - Y)) / 242).toFixed(2));
    Change("Atmosphere preset", "Custom");
  };
  const Apply = (P) => {
    for (const [Key, Value] of Object.entries({
      ...Defaults,
      ...P.Settings,
      Rayleigh: P.X,
      Mie: P.Y,
    }))
      Change(Key, Value);
    Change("Atmosphere preset", P.Name);
  };
  const Active = AtmospherePresets.find(
    (P) =>
      P.Name === V("Atmosphere preset") &&
      Object.entries({
        ...Defaults,
        ...P.Settings,
        Rayleigh: P.X,
        Mie: P.Y,
      }).every(([Key, Value]) => (V(Key) ?? Defaults[Key]) === Value),
  );
  return (
    <section
      className="property-card atmosphere-atlas"
      data-card="Atmosphere atlas"
    >
      <h3>
        Atmosphere atlas <small>AUTHORING PALETTE</small>
      </h3>
      <div className="graph-metric">{Active?.Name || "Custom atmosphere"}</div>
      <p>Molecular scattering → / aerosol load ↑</p>
      <svg
        ref={Root}
        className="atmosphere-map"
        viewBox="0 0 500 310"
        role="group"
        aria-label="Atmosphere type atlas"
        tabIndex={0}
        onKeyDown={(E) => {
          if (
            ["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown"].includes(E.key)
          ) {
            E.preventDefault();
            Change(
              "Rayleigh",
              +Clamp(
                V("Rayleigh") +
                  (E.key === "ArrowRight"
                    ? 0.04
                    : E.key === "ArrowLeft"
                      ? -0.04
                      : 0),
                0,
                4,
              ).toFixed(2),
            );
            Change(
              "Mie",
              +Clamp(
                V("Mie") +
                  (E.key === "ArrowUp"
                    ? 0.06
                    : E.key === "ArrowDown"
                      ? -0.06
                      : 0),
                0,
                6,
              ).toFixed(2),
            );
            Change("Atmosphere preset", "Custom");
          }
        }}
        onPointerDown={(E) => {
          if (E.button !== 0 || E.target.closest("[data-preset]")) return;
          Drag.current = true;
          Root.current.focus();
          Root.current.setPointerCapture(E.pointerId);
          Set(E);
        }}
        onPointerMove={(E) => {
          if (Drag.current) Set(E);
        }}
        onPointerUp={() => (Drag.current = false)}
        onPointerCancel={() => (Drag.current = false)}
        onLostPointerCapture={(E) => {
          if (E.target === Root.current) Drag.current = false;
        }}
      >
        {Array.from({ length: 24 * 16 }, (_, I) => {
          const X = ((I % 24) / 23) * 4,
            Y = ((15 - Math.floor(I / 24)) / 15) * 6;
          let Sum = 0,
            RGB = [0, 0, 0];
          for (const P of AtmospherePresets) {
            const W =
              1 / (0.02 + ((X - P.X) / 4) ** 2 + ((Y - P.Y) / 6) ** 2) ** 2;
            Sum += W;
            Hex(P.Colour).forEach((C, A) => (RGB[A] += C * W));
          }
          return (
            <rect
              key={I}
              x={40 + ((I % 24) * 425) / 24}
              y={22 + (Math.floor(I / 24) * 242) / 16}
              width={425 / 24 + 1}
              height={242 / 16 + 1}
              fill={`rgb(${RGB.map((C) => Math.round((C / Sum) * 255)).join(",")})`}
              opacity=".3"
            />
          );
        })}
        {[0, 1, 2, 3, 4].map((X) => (
          <g key={X}>
            <path d={`M${40 + (X / 4) * 425} 22V264`} stroke="#ffffff12" />
            <text x={40 + (X / 4) * 425} y="284" textAnchor="middle">
              {X}
            </text>
          </g>
        ))}
        {[0, 2, 4, 6].map((Y) => (
          <g key={Y}>
            <path d={`M40 ${264 - (Y / 6) * 242}H465`} stroke="#ffffff12" />
            <text x="27" y={268 - (Y / 6) * 242} textAnchor="end">
              {Y}
            </text>
          </g>
        ))}
        {AtmospherePresets.map((P) => (
          <g
            key={P.Name}
            role="button"
            tabIndex={0}
            data-preset={P.Name}
            aria-label={"Apply " + P.Name}
            onClick={() => Apply(P)}
            onKeyDown={(E) => {
              if (E.key === "Enter" || E.key === " ") {
                E.preventDefault();
                Apply(P);
              }
            }}
            onPointerEnter={() => Inspect(P.Name)}
            onPointerLeave={() => Inspect(null)}
          >
            <circle
              cx={40 + (P.X / 4) * 425}
              cy={264 - (P.Y / 6) * 242}
              r="12"
              fill="transparent"
            />
            <circle
              cx={40 + (P.X / 4) * 425}
              cy={264 - (P.Y / 6) * 242}
              r="4.5"
              fill={P.Colour}
              stroke="#ece9df"
            />
            <title>{P.Name}</title>
          </g>
        ))}
        <circle
          className="atlas-current"
          cx={40 + (V("Rayleigh") / 4) * 425}
          cy={264 - (V("Mie") / 6) * 242}
          r="9"
          stroke="#fff"
          strokeWidth="1.5"
          fill="none"
          pointerEvents="none"
        />
        <text x="250" y="306" textAnchor="middle">
          RAYLEIGH STRENGTH
        </text>
      </svg>
      <p className="atlas-hover">
        {Hover ||
          `Rayleigh ${V("Rayleigh").toFixed(2)} × · Mie ${V("Mie").toFixed(2)} ×`}
      </p>
      <div className="atlas-presets">
        {AtmospherePresets.map((P) => (
          <button
            key={P.Name}
            aria-label={"Atmosphere preset " + P.Name}
            aria-pressed={Active?.Name === P.Name}
            onClick={() => Apply(P)}
          >
            <i style={{ background: P.Colour }} />
            {P.Name}
          </button>
        ))}
      </div>
      <p className="graph-note">
        Reconstructed palette, not a recovered historical atlas or an exhaustive
        scientific classification. Named dots apply complete presets;
        empty-space dragging edits Rayleigh/Mie only. Mars-like is a stylized
        setting, not a composition-accurate Mars model.
      </p>
    </section>
  );
}
const Dot = (A, B) => A.reduce((S, V, I) => S + V * B[I], 0),
  Add = (A, B, S = 1) => A.map((V, I) => V + S * B[I]),
  Unit = (A) => {
    const L = Math.hypot(...A);
    return A.map((V) => V / L);
  };
function SphereRay(O, D, R) {
  const B = Dot(O, D),
    C = Dot(O, O) - R * R,
    H = B * B - C;
  if (H < 0) return null;
  return [-B - Math.sqrt(H), -B + Math.sqrt(H)];
}
function Space({ V, Change, Expand, Turn, Orbit }) {
  const Canvas = useRef(null),
    Drag = useRef(null);
  useEffect(() => {
    let Frame = requestAnimationFrame(() => {
      if (Canvas.current.closest("[inert]")) return;
      const C = Canvas.current,
        W = 360,
        H = 232,
        Context = C.getContext("2d");
      C.width = W;
      C.height = H;
      const Image = Context.createImageData(W, H);
      const Rayleigh = V("Rayleigh"),
        Mie = V("Mie"),
        OzoneStrength = V("Ozone"),
        Glow = V("Horizon Glow"),
        White = V("White Line"),
        R = V("Planet radius") ?? 6371,
        Alt = Clamp(V("Space altitude") ?? 15000, 200, 40000),
        Thick = V("Atmosphere") / 1000,
        Ex = V("Limb exaggeration") ?? 8,
        Outer = R + Thick * Ex;
      const Yaw = ((V("Space yaw") ?? 25) * Math.PI) / 180,
        Pitch = ((V("Space pitch") ?? 15) * Math.PI) / 180,
        Eye = Unit([
          Math.cos(Pitch) * Math.cos(Yaw),
          Math.cos(Pitch) * Math.sin(Yaw),
          Math.sin(Pitch),
        ]).map((N) => N * (R + Alt));
      const Forward = Unit(Eye.map((N) => -N)),
        Right = Unit([-Forward[1], Forward[0], 0]),
        Up = [
          Right[1] * Forward[2],
          -Right[0] * Forward[2],
          Right[0] * Forward[1] - Right[1] * Forward[0],
        ];
      const SunAngle = ((V("Space sun angle") ?? 35) * Math.PI) / 180,
        Sun = Unit([Math.cos(SunAngle), -0.35, Math.sin(SunAngle)]),
        Tint = Hex(V("Sky Tint")),
        Ground = Hex(V("Ground Albedo")),
        Brightness = V("Sky Brightness"),
        HR = V("Rayleigh Scale H") / 1000,
        HM = V("Mie Scale H") / 1000,
        G = V("Mie Anisotropy");
      for (let Y = 0; Y < H; Y++)
        for (let X = 0; X < W; X++) {
          const D = Unit(
              Add(
                Add(Forward, Right, ((X / W - 0.5) * 1.25 * W) / H),
                Up,
                (0.5 - Y / H) * 1.25,
              ),
            ),
            Hit = SphereRay(Eye, D, Outer);
          let RGB = [0.00003, 0.00005, 0.0001];
          if (Hit && Hit[1] > 0) {
            const Planet = SphereRay(Eye, D, R),
              Surface = Planet && Planet[0] > 0 ? Planet[0] : null,
              Start = Math.max(0, Hit[0]),
              End = Surface ?? Hit[1],
              Trans = [1, 1, 1],
              Light = [0, 0, 0],
              DS = Math.max(0, End - Start) / 20,
              Mu = Dot(D, Sun),
              RayPhase = 0.75 * (1 + Mu * Mu),
              MiePhase = Math.min(
                8,
                (1 - G * G) / Math.max(0.01, (1 + G * G - 2 * G * Mu) ** 1.5),
              );
            for (let I = 0; I < 20; I++) {
              const P = Add(Eye, D, Start + (I + 0.5) * DS),
                Height = Math.max(0, (Math.hypot(...P) - R) / Ex),
                DR = Math.exp(-Height / HR),
                DM = Math.exp(-Height / HM),
                Ozone = Math.exp(-Math.pow((Height - 25) / 15, 2)),
                Shadow = SphereRay(P, Sun, R),
                Lit = !(Shadow && Shadow[0] > 0),
                BetaR = [0.0058, 0.0135, 0.0331].map((B) => B * Rayleigh * DR),
                BetaM = 0.02 * Mie * DM;
              for (let A = 0; A < 3; A++) {
                const Absorb =
                    [0.00065, 0.00188, 0.00009][A] * OzoneStrength * Ozone,
                  Sigma = BetaR[A] + BetaM + Absorb,
                  Step = DS / Ex;
                Light[A] +=
                  Trans[A] *
                  (BetaR[A] * RayPhase + BetaM * MiePhase) *
                  (Lit ? 1 : 0.015) *
                  Step *
                  0.65 *
                  Tint[A];
                Trans[A] *= Math.exp(-Sigma * Step);
              }
            }
            if (Surface) {
              const P = Unit(Add(Eye, D, Surface)),
                Diffuse = Math.max(0, Dot(P, Sun)),
                Noise =
                  Math.sin(P[0] * 4 + P[2] * 3.1) +
                  0.5 * Math.sin(P[1] * 9 - P[2] * 5 + P[0]) +
                  0.25 * Math.sin(P[0] * 17 + P[1] * 11) +
                  0.15 * Math.cos(P[1] * 23 + P[2] * 17),
                Land = Clamp((Noise - 0.12) * 4, 0, 1),
                Terrestrial = R > 5000;
              RGB = Ground.map(
                (B, A) =>
                  (Terrestrial
                    ? Land * B * 0.22 + (1 - Land) * [0.01, 0.035, 0.07][A]
                    : B * 0.22 * (0.85 + Noise * 0.08)) *
                  (0.008 + Diffuse),
              );
            }
            RGB = RGB.map(
              (B, A) =>
                B * Trans[A] +
                Light[A] * Brightness * (0.8 + 0.12 * Glow + 0.08 * White),
            );
          }
          const Offset = (Y * W + X) * 4;
          for (let A = 0; A < 3; A++)
            Image.data[Offset + A] = Math.round(
              255 * (1 - Math.exp(-Math.max(0, RGB[A]) * 1.8)) ** (1 / 2.2),
            );
          Image.data[Offset + 3] = 255;
        }
      Context.putImageData(Image, 0, 0);
      C.dataset.render = String(performance.now());
    });
    return () => cancelAnimationFrame(Frame);
  });
  const Pointer = (E) => {
    if (!Drag.current) return;
    Change(
      "Space yaw",
      (Drag.current.Yaw + (E.clientX - Drag.current.X) * 0.5 + 360) % 360,
    );
    Change(
      "Space pitch",
      Clamp(Drag.current.Pitch - (E.clientY - Drag.current.Y) * 0.4, -75, 75),
    );
  };
  return (
    <section
      className="property-card atmosphere-space"
      data-card="Atmosphere from space"
    >
      <header>
        <h3>Atmosphere from space</h3>
        {Expand && (
          <button aria-label="Expand AtmosphereLab" onClick={Expand}>
            ↗
          </button>
        )}
      </header>
      <canvas
        ref={Canvas}
        role="img"
        tabIndex={0}
        aria-label="Atmosphere planet preview; drag to orbit"
        onPointerDown={(E) => {
          if (E.button !== 0) return;
          Orbit(false);
          E.currentTarget.setPointerCapture(E.pointerId);
          Drag.current = {
            X: E.clientX,
            Y: E.clientY,
            Yaw: V("Space yaw") ?? 25,
            Pitch: V("Space pitch") ?? 15,
          };
        }}
        onPointerMove={Pointer}
        onPointerUp={() => (Drag.current = null)}
        onPointerCancel={() => (Drag.current = null)}
        onLostPointerCapture={() => (Drag.current = null)}
        onKeyDown={(E) => {
          if (
            ["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown"].includes(E.key)
          ) {
            E.preventDefault();
            Change(
              "Space yaw",
              ((V("Space yaw") ?? 25) +
                (E.key === "ArrowLeft" ? -5 : E.key === "ArrowRight" ? 5 : 0) +
                360) %
                360,
            );
            Change(
              "Space pitch",
              Clamp(
                (V("Space pitch") ?? 15) +
                  (E.key === "ArrowUp" ? 5 : E.key === "ArrowDown" ? -5 : 0),
                -75,
                75,
              ),
            );
          }
        }}
      />
      <div className="graph-status">
        CPU SINGLE-SCATTER STUDY · NOT NATIVE SKY RENDERING
      </div>
      <button aria-pressed={Turn} onClick={() => Orbit(!Turn)}>
        {Turn ? "Pause orbit" : "Animate orbit"}
      </button>
      <div className="space-controls">
        {[
          ["Space altitude", "Observer altitude · km", 200, 40000, 15000],
          ["Space sun angle", "Preview sun angle · °", -90, 90, 35],
          ["Limb exaggeration", "Limb thickness · visual ×", 1, 16, 8],
        ].map(([Key, Label, Min, Max, Default]) => (
          <label key={Key}>
            {Label}
            <output>{Math.round(V(Key) ?? Default)}</output>
            <input
              aria-label={Label}
              type="range"
              min={Min}
              max={Max}
              value={V(Key) ?? Default}
              onChange={(E) => Change(Key, Number(E.target.value))}
            />
          </label>
        ))}
      </div>
      <p className="graph-note">
        Drag / arrow keys to orbit. Exaggerated shell thickness is a view aid,
        not planet scale. Procedural surface, simplified absorption and single
        scattering; no native bake, measured planet imagery or multiple
        scattering.
      </p>
    </section>
  );
}
export default function AtmosphereLab({ V, Change }) {
  const [Open, Expand] = useState(false),
    Root = useRef(null),
    Opener = useRef(null),
    [Turn, Orbit] = useState(false),
    Latest = useRef({ V, Change });
  Latest.current = { V, Change };
  useEffect(() => {
    if (!Turn) return;
    const Timer = setInterval(
      () =>
        Latest.current.Change(
          "Space yaw",
          ((Latest.current.V("Space yaw") ?? 25) + 2) % 360,
        ),
      180,
    );
    return () => clearInterval(Timer);
  }, [Turn]);
  useEffect(() => {
    if (!Open) return;
    const Editor = document.getElementById("Editor");
    const Previous = Editor.inert;
    Editor.inert = true;
    Root.current.querySelector("button").focus();
    const Escape = (Event) => {
      if (Event.key === "Escape") {
        Event.preventDefault();
        Event.stopPropagation();
        Expand(false);
      }
    };
    document.addEventListener("keydown", Escape, true);
    return () => {
      document.removeEventListener("keydown", Escape, true);
      Editor.inert = Previous;
      Opener.current?.focus();
    };
  }, [Open]);
  const Key = (E) => {
    if (E.key === "Escape") {
      E.stopPropagation();
      Expand(false);
    }
    if (E.key === "Tab") {
      const Nodes = [
        ...Root.current.querySelectorAll('button,input,select,[tabindex="0"]'),
      ].filter((N) => N.getClientRects().length);
      if (E.shiftKey && document.activeElement === Nodes[0]) {
        E.preventDefault();
        Nodes.at(-1).focus();
      } else if (!E.shiftKey && document.activeElement === Nodes.at(-1)) {
        E.preventDefault();
        Nodes[0].focus();
      }
    }
  };
  return (
    <>
      <div className="card-grid atmosphere-lab-cards">
        <Atlas V={V} Change={Change} />
        <Space
          Turn={Turn}
          Orbit={Orbit}
          V={V}
          Change={Change}
          Expand={() => {
            Opener.current = document.activeElement;
            Expand(true);
          }}
        />
      </div>
      {Open &&
        createPortal(
          <div className="atmosphere-lab-backdrop">
            <section
              className="atmosphere-lab-window"
              ref={Root}
              role="dialog"
              aria-modal="true"
              aria-label="AtmosphereLab"
              onKeyDown={Key}
            >
              <header>
                <div>
                  <small>ENVIRONMENT / INTERACTIVE STUDIES</small>
                  <h1>AtmosphereLab</h1>
                </div>
                <button
                  aria-label="Close AtmosphereLab"
                  onClick={() => Expand(false)}
                >
                  ×
                </button>
              </header>
              <div className="atmosphere-lab-body">
                <Atlas V={V} Change={Change} />
                <Space V={V} Change={Change} Turn={Turn} Orbit={Orbit} />
              </div>
            </section>
          </div>,
          document.body,
        )}
    </>
  );
}
