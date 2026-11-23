import React, { useRef } from "react";
import { PresetGlyphs } from "./LightGlyphs.js";
// Styles: the shared effect-study rules in PostEffects.css.

// 🧩 Environment Light: image-based light from the whole sky (procedural sky or an HDRI preset) in the Lighting group.
//    A property sheet in the NativePanels format, its outliner row, the one-time scene migration, and a study card in
//    the camera-card style: a side view of the sky dome lighting a ball and a top view of where the brightest light
//    comes from. Dragging across the study rotates the environment.
// 📝 Browser study only. Nothing here reaches the native renderer.

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHEET AND ROW
//------------------------------------------------------------------------------------------------------------------------

export const EnvironmentPanel = "environment-light";

// Each source: zenith, horizon and ground colours, a reference illuminance at intensity 1 / 0 EV, and the bright
// patches (sun or softboxes) as [azimuth°, elevation°, colour, share of the light].
const Sources = [
  {
    Name: "Sky atmosphere",
    Overhead: "#3f7fe0",
    Skyline: "#bcd6f2",
    Ground: "#5a5248",
    Lux: 12000,
    Patches: [[140, 38, "#fff1c8", 0.55]],
  },
  {
    Name: "HDRI · studio",
    Overhead: "#2c2c2c",
    Skyline: "#707070",
    Ground: "#1e1e1e",
    Lux: 900,
    Patches: [
      [60, 34, "#ffffff", 0.35],
      [-60, 30, "#f2f6ff", 0.25],
    ],
  },
  {
    Name: "HDRI · overcast",
    Overhead: "#a4adb6",
    Skyline: "#d4d8dc",
    Ground: "#4a4a46",
    Lux: 2500,
    Patches: [],
  },
  {
    Name: "HDRI · sunset",
    Overhead: "#2a3f7a",
    Skyline: "#ff9a5a",
    Ground: "#3a2c24",
    Lux: 1400,
    Patches: [[250, 6, "#ffc46a", 0.6]],
  },
  {
    Name: "Uniform colour",
    Overhead: null,
    Skyline: null,
    Ground: null,
    Lux: 1000,
    Patches: [],
  },
];

export const EnvironmentSheet = [
  {
    Label: "Source",
    Control: "Select",
    Options: Sources.map((Source) => Source.Name),
    Default: 0,
    Group: "Source",
  },
  { Label: "Colour", Control: "Colour", Default: "#9fb4cc", Group: "Source" },
  {
    Label: "Rotation",
    Control: "Slider",
    Minimum: 0,
    Maximum: 360,
    Default: 0,
    Unit: "°",
    Decimals: 0,
    Group: "Source",
  },
  {
    Label: "Intensity",
    Control: "Slider",
    Minimum: 0,
    Maximum: 4,
    Default: 1,
    Unit: "×",
    Decimals: 2,
    Group: "Response",
  },
  {
    Label: "Exposure",
    Control: "Slider",
    Minimum: -4,
    Maximum: 4,
    Default: 0,
    Unit: "EV",
    Decimals: 1,
    Group: "Response",
  },
  {
    Label: "Saturation",
    Control: "Slider",
    Minimum: 0,
    Maximum: 2,
    Default: 1,
    Unit: "",
    Decimals: 2,
    Group: "Response",
  },
  {
    Label: "Ground bounce",
    Control: "Slider",
    Minimum: 0,
    Maximum: 1,
    Default: 0.3,
    Unit: "",
    Decimals: 2,
    Group: "Response",
  },
  { Label: "Diffuse", Control: "Switch", Default: true, Group: "Contribution" },
  {
    Label: "Specular",
    Control: "Switch",
    Default: true,
    Group: "Contribution",
  },
  {
    Label: "Ambient occlusion",
    Control: "Switch",
    Default: true,
    Group: "Contribution",
  },
  {
    Label: "Visible to camera",
    Control: "Switch",
    Default: false,
    Group: "Contribution",
  },
];

export const EnvironmentSymbols = {
  Diffuse: "gi",
  Specular: "starburst",
  "Ambient occlusion": "shadow",
  "Visible to camera": "visible",
};

export const EnvironmentRow = {
  Id: "environment-light",
  Name: "Environment Light",
  Panel: EnvironmentPanel,
  Icon: PresetGlyphs["Environment Light"],
  Parent: "lighting",
  Description: "Image-based sky light · HDRI or procedural sky",
};

// Scene migration: scenes saved before the Environment Light existed get it once, as the first light in Lighting.
// Runs once per saved scene (EnvironmentLightRevision), so a later deletion is respected.
export function EnsureEnvironmentLight(Rows = [], Revision) {
  if (Revision === 1 || !Array.isArray(Rows)) return Rows;
  if (Rows.some((Row) => Row?.Id === EnvironmentRow.Id)) return Rows;
  const Group = Rows.findIndex((Row) => Row?.Id === "lighting");
  if (Group < 0) return Rows;
  const Result = [...Rows];
  Result.splice(Group + 1, 0, { ...EnvironmentRow });
  return Result;
}

function Illuminance(V) {
  const Source = Sources[+V("Source")] || Sources[0];
  return Source.Lux * Math.max(0, +V("Intensity")) * 2 ** +V("Exposure");
}

const Grouped = (Amount) =>
  Amount >= 10000
    ? Math.round(Amount / 100) / 10 + "k"
    : Math.round(Amount).toLocaleString("en-US");

export function EnvironmentSummary(V) {
  const Source = Sources[+V("Source")] || Sources[0];
  return (
    Source.Name.replace("HDRI · ", "") + " · " + Grouped(Illuminance(V)) + " lx"
  );
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       STUDY
//------------------------------------------------------------------------------------------------------------------------

const Clamp = (Amount, Low, High) => Math.max(Low, Math.min(High, Amount));
const Channels = (Hex) =>
  [1, 3, 5].map((At) => parseInt(Hex.slice(At, At + 2), 16) / 255);
const Hex = (Rgb) =>
  "#" +
  Rgb.map((Channel) =>
    Math.round(Clamp(Channel, 0, 1) * 255)
      .toString(16)
      .padStart(2, "0"),
  ).join("");

// Colour as the viewer sees it: saturation around the luma, then exposure with a soft shoulder so bright skies roll off.
function Develop(Colour, Exposure, Saturation) {
  const Rgb = Channels(Colour);
  const Luma = 0.2126 * Rgb[0] + 0.7152 * Rgb[1] + 0.0722 * Rgb[2];
  const Shoulder = (Amount) =>
    (1 - Math.exp(-1.6 * Amount)) / (1 - Math.exp(-1.6));
  return Hex(
    Rgb.map((Channel) =>
      Shoulder((Luma + (Channel - Luma) * Saturation) * Exposure),
    ),
  );
}

function Palette(V) {
  const Source = Sources[+V("Source")] || Sources[0];
  const Tint = V("Colour") || "#9fb4cc";
  const Exposure = Math.max(0, +V("Intensity")) * 2 ** +V("Exposure");
  const Saturation = +V("Saturation");
  const Bounce = +V("Ground bounce");
  const Colour = (Authored) => Develop(Authored || Tint, Exposure, Saturation);
  return {
    Source,
    Exposure,
    Overhead: Colour(Source.Overhead),
    Skyline: Colour(Source.Skyline),
    Ground: Develop(
      Source.Ground || Tint,
      Exposure * (0.35 + Bounce),
      Saturation,
    ),
    Bounce,
    Patches: Source.Patches.map(([Azimuth, Elevation, Shade, Share]) => ({
      Azimuth: (((Azimuth + +V("Rotation")) % 360) + 360) % 360,
      Elevation,
      Shade: Develop(Shade, Math.max(0.6, Exposure), Saturation),
      Share,
    })),
  };
}

function Compass(Azimuth) {
  return ["N", "NE", "E", "SE", "S", "SW", "W", "NW"][
    Math.round(Azimuth / 45) % 8
  ];
}

function DomeChart({ V, Turn }) {
  const Paint = Palette(V);
  const Diffuse = !!V("Diffuse"),
    Specular = !!V("Specular"),
    Occlusion = !!V("Ambient occlusion");
  // Side view: dome centred at (96, 112), radius 78; viewer looks north, so azimuth 90° (east) is to the right.
  const Cx = 96,
    Cy = 116,
    R = 82;
  const Side = (Azimuth, Elevation) => {
    const A = (Azimuth * Math.PI) / 180,
      E = (Elevation * Math.PI) / 180;
    return {
      X: Cx + R * Math.cos(E) * Math.sin(A),
      Y: Cy - R * Math.sin(E),
      Behind: Math.cos(A) < 0,
    };
  };
  const Key = Paint.Patches[0];
  const KeySide = Key ? Side(Key.Azimuth, Key.Elevation) : null;
  // Ball highlight follows the key patch in the side view.
  const Lean = KeySide ? Clamp((KeySide.X - Cx) / R, -1, 1) : 0;
  // Top view: compass disc at (262, 86).
  const Tx = 262,
    Ty = 82,
    Tr = 52;
  const Top = (Azimuth, Elevation) => {
    const A = (Azimuth * Math.PI) / 180,
      Reach = Tr * (1 - Elevation / 90);
    return { X: Tx + Reach * Math.sin(A), Y: Ty - Reach * Math.cos(A) };
  };
  return (
    <svg
      viewBox="0 0 320 172"
      role="img"
      aria-label="Sky dome lighting a ball, and the top view of the environment"
      data-environment-chart
      {...Turn}
    >
      <defs>
        <linearGradient id="envSky" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor={Paint.Overhead} />
          <stop offset="1" stopColor={Paint.Skyline} />
        </linearGradient>
        <radialGradient id="envBall" cx={0.5 + Lean * 0.22} cy="0.3" r="0.8">
          <stop
            offset="0"
            stopColor={Diffuse ? (Key ? Key.Shade : Paint.Overhead) : "#2a2a2a"}
          />
          <stop offset=".55" stopColor={Diffuse ? Paint.Skyline : "#202020"} />
          <stop offset="1" stopColor={Diffuse ? Paint.Ground : "#151515"} />
        </radialGradient>
        <radialGradient id="envTop" cx="0.5" cy="0.5" r="0.5">
          <stop offset="0" stopColor={Paint.Overhead} />
          <stop offset="1" stopColor={Paint.Skyline} />
        </radialGradient>
        <radialGradient id="envPatch" cx="0.5" cy="0.5" r="0.5">
          <stop offset="0" stopColor="#ffffff" />
          <stop
            offset=".35"
            stopColor={Key?.Shade || "#ffffff"}
            stopOpacity=".85"
          />
          <stop
            offset="1"
            stopColor={Key?.Shade || "#ffffff"}
            stopOpacity="0"
          />
        </radialGradient>
        <clipPath id="envDome">
          <path d={`M${Cx - R} ${Cy}A${R} ${R} 0 0 1 ${Cx + R} ${Cy}Z`} />
        </clipPath>
      </defs>
      <g>
        <path
          d={`M${Cx - R} ${Cy}A${R} ${R} 0 0 1 ${Cx + R} ${Cy}Z`}
          fill="url(#envSky)"
          opacity=".9"
        />
        <path
          d={`M${Cx - R} ${Cy}A${R} ${R} 0 0 1 ${Cx + R} ${Cy}`}
          fill="none"
          stroke="#ffffff22"
        />
        <g clipPath="url(#envDome)">
          {Paint.Patches.map((Patch, Index) => {
            const At = Side(Patch.Azimuth, Patch.Elevation);
            return (
              <circle
                key={Index}
                cx={At.X}
                cy={At.Y}
                r={8 + Patch.Share * 16}
                fill="url(#envPatch)"
                opacity={At.Behind ? 0.4 : 1}
              />
            );
          })}
        </g>
        {/* Light arriving at the ball: rays from the zenith and the key patch, bounce from the ground. */}
        {Diffuse &&
          [-48, -24, 0, 24, 48].map((Offset) => (
            <line
              key={Offset}
              x1={Cx + Offset}
              y1={Cy - Math.sqrt(R * R - Offset * Offset) + 6}
              x2={Cx + Offset * 0.25}
              y2={Cy - 22}
              stroke="#ffffff"
              strokeOpacity=".16"
              strokeDasharray="2 3"
            />
          ))}
        {KeySide && !KeySide.Behind && (
          <line
            x1={KeySide.X}
            y1={KeySide.Y}
            x2={Cx + Lean * 8}
            y2={Cy - 20}
            stroke={Key.Shade}
            strokeOpacity=".75"
            strokeWidth="1.2"
          />
        )}
        <rect
          x={Cx - R - 6}
          y={Cy}
          width={2 * R + 12}
          height="14"
          rx="4"
          fill={Paint.Ground}
        />
        {Paint.Bounce > 0.02 &&
          Diffuse &&
          [-20, 0, 20].map((Offset) => (
            <path
              key={Offset}
              d={`M${Cx + Offset} ${Cy + 1}l${-Offset * 0.3} -5`}
              stroke="#ffffff"
              strokeOpacity={0.15 + Paint.Bounce * 0.5}
              strokeWidth="1.2"
              strokeLinecap="round"
            />
          ))}
        {Occlusion && (
          <ellipse
            cx={Cx + Lean * -6}
            cy={Cy + 1}
            rx="16"
            ry="3"
            fill="#000"
            opacity=".55"
          />
        )}
        <circle cx={Cx} cy={Cy - 15} r="15" fill="url(#envBall)" />
        {Specular && Key && (
          <circle
            cx={Cx + Lean * 7}
            cy={Cy - 22}
            r="2.6"
            fill="#ffffff"
            opacity=".9"
          />
        )}
        {Specular && !Key && (
          <ellipse
            cx={Cx}
            cy={Cy - 25}
            rx="6"
            ry="2"
            fill="#ffffff"
            opacity=".35"
          />
        )}
        <text x={Cx - R} y="166">
          W
        </text>
        <text x={Cx + R} y="166" textAnchor="end">
          E
        </text>
        <text x={Cx} y="166" textAnchor="middle">
          side view · looking north
        </text>

        <circle cx={Tx} cy={Ty} r={Tr} fill="url(#envTop)" opacity=".9" />
        <circle
          cx={Tx}
          cy={Ty}
          r={Tr * (1 - 30 / 90)}
          fill="none"
          stroke="#ffffff30"
          strokeDasharray="2 3"
        />
        <circle
          cx={Tx}
          cy={Ty}
          r={Tr * (1 - 60 / 90)}
          fill="none"
          stroke="#ffffff30"
          strokeDasharray="2 3"
        />
        {Paint.Patches.map((Patch, Index) => {
          const At = Top(Patch.Azimuth, Patch.Elevation);
          return (
            <g key={Index}>
              <line
                x1={Tx}
                y1={Ty}
                x2={At.X}
                y2={At.Y}
                stroke={Patch.Shade}
                strokeOpacity=".7"
              />
              <circle
                cx={At.X}
                cy={At.Y}
                r={3 + Patch.Share * 6}
                fill="url(#envPatch)"
              />
              <circle cx={At.X} cy={At.Y} r="1.8" fill="#ffffff" />
            </g>
          );
        })}
        <circle cx={Tx} cy={Ty} r="2" fill="#141414" />
        <text x={Tx} y={Ty - Tr - 4} textAnchor="middle" className="fx-accent">
          N
        </text>
        <text x={Tx + Tr + 4} y={Ty + 3}>
          E
        </text>
        <text x={Tx - Tr - 10} y={Ty + 3}>
          W
        </text>
        <text x={Tx} y={Ty + Tr + 10} textAnchor="middle">
          S
        </text>
        <text x={Tx} y="166" textAnchor="middle">
          top view · rings 30° / 60°
        </text>
      </g>
    </svg>
  );
}

export function EnvironmentStudy({ Read: V, Assign, Active }) {
  const Paint = Palette(V);
  const Lux = Illuminance(V);
  const Key = Paint.Patches[0];
  const Direct = Paint.Patches.reduce((Sum, Patch) => Sum + Patch.Share, 0);
  // Horizontal drag rotates the environment: one chart width is a full turn.
  const Drag = useRef(null);
  const Turn = {
    onPointerDown: (Event) => {
      Drag.current = {
        Start: Event.clientX,
        From: +V("Rotation"),
        Width: Event.currentTarget.getBoundingClientRect().width,
      };
      Event.currentTarget.setPointerCapture(Event.pointerId);
    },
    onPointerMove: (Event) => {
      const At = Drag.current;
      if (!At || !Assign) return;
      Assign(
        "Rotation",
        Math.round(
          (((At.From + ((Event.clientX - At.Start) / At.Width) * 360) % 360) +
            360) %
            360,
        ),
      );
    },
    onPointerUp: () => (Drag.current = null),
    onPointerCancel: () => (Drag.current = null),
  };
  const Readings = [
    [
      "Key light",
      Key ? Compass(Key.Azimuth) + " · " + Key.Elevation + "°" : "Even sky",
    ],
    ["Direct", Math.round(Direct * 100) + "%"],
    ["Bounce", Math.round(Paint.Bounce * 100) + "%"],
  ];
  return (
    <section
      className="generic-card effect-study environment-study"
      data-card="Environment study"
    >
      <header className="effect-study-head">
        <h3>Sky dome</h3>
        <span>{Active ? Paint.Source.Name : "Light disabled"}</span>
      </header>
      <div className="effect-figure">
        <span className="effect-figure-number">{Grouped(Lux)}</span>
        <small>lx on a level surface</small>
      </div>
      <div className={"effect-chart" + (Active ? "" : " is-off")}>
        <DomeChart V={V} Turn={Turn} />
      </div>
      <div className="effect-readouts">
        {Readings.map(([Label, Text]) => (
          <div key={Label}>
            <span>{Label}</span>
            <b>{Text}</b>
          </div>
        ))}
      </div>
      <p className="graph-note">
        Light arriving from the whole sky · drag to rotate the environment.
        Diagram of the authored values; not the native renderer.
      </p>
    </section>
  );
}
