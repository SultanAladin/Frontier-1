// Visuals for the Height Fog and Atmospheric Fog cards. Each fog kind gets pictures of what its own parameters do:
//   · Height Fog: a valley cut-away where the fog thins with altitude (Density, Falloff Height), and the glow
//     towards / away from the sun (Sun Scatter, Colour).
//   · Atmospheric Fog: ridges receding into haze (Density, Start), with a distance probe.
// Extinction follows FogDensity in LiveGraph.jsx, so the numbers agree with every other fog readout.
import React, { useId } from "react";
import { FogDensity } from "./LiveGraph.jsx";

const Clamp = (Value, Minimum, Maximum) => Math.max(Minimum, Math.min(Maximum, Value));
const Distance = (Metres) =>
  !isFinite(Metres) ? "clear" : Metres >= 1000 ? (Metres / 1000).toFixed(Metres >= 10000 ? 0 : 1) + " km" : Math.round(Metres) + " m";
export const Visibility = (Sigma) => (Sigma > 1e-9 ? 3.912 / Sigma : Infinity);
const Rgb = (Hex) => {
  const Packed = parseInt((Hex || "#ffffff").slice(1), 16);
  return [(Packed >> 16) & 255, (Packed >> 8) & 255, Packed & 255];
};
const Mix = (A, B, T) => "rgb(" + A.map((C, I) => Math.round(C + (B[I] - C) * T)).join(",") + ")";

// Keyboard and pointer editing shared by the two draggable markers.
function SliderProps({ Name, Value, Minimum, Maximum, Step, Change }) {
  return {
    role: "slider",
    tabIndex: 0,
    "aria-label": Name,
    "aria-valuemin": Minimum,
    "aria-valuemax": Maximum,
    "aria-valuenow": Math.round(Value),
    onKeyDown: (Event) => {
      const Next = {
        ArrowLeft: Value - Step,
        ArrowDown: Value - Step,
        ArrowRight: Value + Step,
        ArrowUp: Value + Step,
        Home: Minimum,
        End: Maximum,
      }[Event.key];
      if (Next === undefined) return;
      Event.preventDefault();
      Change(Clamp(Next, Minimum, Maximum));
    },
  };
}

// ── Height Fog · valley cut-away ────────────────────────────────────────────────────────────────────────────────
const Ceiling = 3000;
export function HeightFogLayer({ V, Change }) {
  const Id = useId().replaceAll(":", "");
  const Enabled = !!V("Enabled");
  const Falloff = Math.max(10, +V("Falloff Height") || 400);
  const Colour = V("Colour") || "#ffffff";
  const Left = 46, Right = 300, Top = 14, Bottom = 168;
  // Square-root altitude axis: the first few hundred metres, where people and buildings are, get the room.
  const Y = (Altitude) => Bottom - Math.sqrt(Clamp(Altitude, 0, Ceiling) / Ceiling) * (Bottom - Top);
  const Altitude = (Py) => Ceiling * ((Bottom - Clamp(Py, Top, Bottom)) / (Bottom - Top)) ** 2;
  const Sigma = (Height) => FogDensity("height-fog", V, Height, true);
  // Opacity of one kilometre of view at each altitude.
  const Bands = Array.from({ length: 40 }, (_, Index) => {
    const Low = Ceiling * (Index / 40) ** 2, High = Ceiling * ((Index + 1) / 40) ** 2;
    return { Low, High, Opacity: 1 - Math.exp(-Sigma((Low + High) / 2) * 1000) };
  });
  const Hills = `M${Left} ${Y(60)} C ${Left + 40} ${Y(240)}, ${Left + 70} ${Y(330)}, ${Left + 96} ${Y(300)} S ${Left + 140} ${Y(40)}, ${Left + 170} ${Y(20)} S ${Left + 220} ${Y(520)}, ${Right} ${Y(900)} L${Right} ${Bottom} L${Left} ${Bottom} Z`;
  const Ticks = [0, 100, 500, 1000, 3000];
  const Edit = (Event) => {
    const Box = Event.currentTarget.ownerSVGElement.getBoundingClientRect();
    Change("Falloff Height", Math.round(Clamp(Altitude(((Event.clientY - Box.top) / Box.height) * 190), 10, 3000)));
  };
  const Fy = Y(Falloff);
  return (
    <svg className="fog-scene" viewBox="0 0 300 190" aria-label="Height fog layer" data-enabled={Enabled} data-live-plot="">
      <defs>
        <clipPath id={Id + "clip"}>
          <rect x={Left} y={Top} width={Right - Left} height={Bottom - Top} rx="10" />
        </clipPath>
        <linearGradient id={Id + "sky"} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor="#1b2534" />
          <stop offset="1" stopColor="#3b4656" />
        </linearGradient>
      </defs>
      <g clipPath={`url(#${Id}clip)`}>
        <rect x={Left} y={Top} width={Right - Left} height={Bottom - Top} fill={`url(#${Id}sky)`} />
        <path d={Hills} fill="#11151a" />
        {/* A 120 m tower and a town, for scale. */}
        <rect x={Left + 118} y={Y(120)} width="3" height={Y(0) - Y(120)} fill="#0b0e12" />
        {[0, 1, 2, 3, 4].map((Index) => (
          <rect key={Index} x={Left + 126 + Index * 7} y={Y(18 + ((Index * 13) % 22))} width="5" height={Y(0) - Y(18 + ((Index * 13) % 22))} fill="#0b0e12" />
        ))}
        {Bands.map((Band, Index) => (
          <rect key={Index} x={Left} y={Y(Band.High)} width={Right - Left} height={Y(Band.Low) - Y(Band.High) + 0.4} fill={Colour} opacity={(Band.Opacity * (Enabled ? 0.85 : 0.5)).toFixed(3)} />
        ))}
      </g>
      {Ticks.map((Tick) => (
        <g key={Tick}>
          <line x1={Left - 4} x2={Left} y1={Y(Tick)} y2={Y(Tick)} stroke="#6a6a6a" />
          <text x={Left - 7} y={Y(Tick) + 3} textAnchor="end">
            {Tick >= 1000 ? Tick / 1000 + " km" : Tick + " m"}
          </text>
        </g>
      ))}
      <g className="fog-scene-handle" {...SliderProps({ Name: "Fog falloff height", Value: Falloff, Minimum: 10, Maximum: 3000, Step: 10, Change: (Next) => Change("Falloff Height", Next) })}
        onPointerDown={(Event) => {
          Event.currentTarget.setPointerCapture(Event.pointerId);
          Edit(Event);
        }}
        onPointerMove={(Event) => Event.currentTarget.hasPointerCapture(Event.pointerId) && Edit(Event)}
      >
        <rect x={Left} y={Fy - 8} width={Right - Left} height="16" fill="transparent" />
        <line x1={Left} x2={Right} y1={Fy} y2={Fy} stroke="#ffffff" strokeOpacity=".8" strokeDasharray="4 3" />
        <circle cx={Right - 8} cy={Fy} r="4.5" fill="#f2f2f2" />
      </g>
      <text x={Left + 6} y={Fy - 5} className="fog-scene-strong">
        falloff {Distance(Falloff)} · density ÷ e
      </text>
      <text x={Left + 6} y={Bottom - 6} className="fog-scene-strong">
        ground: see {Distance(Visibility(Sigma(0)))}
      </text>
      <text x={Left + 6} y={Math.max(Top + 10, Y(Math.min(Ceiling, Falloff * 3)) - 5)} className="fog-scene-faint">
        at {Distance(Math.min(Ceiling, Falloff * 3))}: see {Distance(Visibility(Sigma(Math.min(Ceiling, Falloff * 3))))}
      </text>
      <text x={Left} y="186">Drag the dashed line to set the falloff height</text>
    </svg>
  );
}

// ── Height Fog · light towards and away from the sun ────────────────────────────────────────────────────────────
export function FogLightStudy({ V }) {
  const Id = useId().replaceAll(":", "");
  const Scatter = Math.max(0, +V("Sun Scatter") || 0);
  const Tint = Rgb(V("Colour"));
  const Haze = Clamp(V("Density") / 0.05, 0.15, 1);
  const Panel = (X, Towards) => {
    const Glow = Towards ? Clamp(Scatter / 2, 0, 1) : 0;
    const Sky = Mix([38, 48, 62], Tint, 0.55 * Haze);
    const Low = Mix([70, 80, 92], Tint, 0.85 * Haze);
    return (
      <g>
        <defs>
          <linearGradient id={Id + X} x1="0" y1="0" x2="0" y2="1">
            <stop offset="0" stopColor={Sky} />
            <stop offset="1" stopColor={Low} />
          </linearGradient>
          <radialGradient id={Id + X + "g"}>
            <stop offset="0" stopColor="#fff6dc" stopOpacity={0.15 + 0.85 * Glow} />
            <stop offset=".4" stopColor={Mix([255, 236, 200], Tint, 0.4)} stopOpacity={0.45 * Glow} />
            <stop offset="1" stopColor="#ffffff" stopOpacity="0" />
          </radialGradient>
          <clipPath id={Id + X + "c"}>
            <rect x={X} y="0" width="146" height="96" rx="12" />
          </clipPath>
        </defs>
        <g clipPath={`url(#${Id + X}c)`}>
          <rect x={X} y="0" width="146" height="96" fill={`url(#${Id + X})`} />
          {Towards && <circle cx={X + 73} cy="58" r={18 + 46 * Glow} fill={`url(#${Id + X}g)`} />}
          {Towards && <circle cx={X + 73} cy="58" r="5" fill="#fffbef" opacity={1 - 0.6 * Haze} />}
          <path d={`M${X} 74 Q ${X + 30} 66 ${X + 60} 72 T ${X + 146} 70 V 96 H ${X} Z`} fill={Mix([18, 20, 24], Tint, 0.3 * Haze)} />
        </g>
        <text x={X + 8} y="14" className="fog-scene-strong">{Towards ? "TOWARDS THE SUN" : "AWAY FROM THE SUN"}</text>
      </g>
    );
  };
  return (
    <svg className="fog-scene" viewBox="0 0 300 112" role="img" aria-label="Fog light study">
      {Panel(0, true)}
      {Panel(154, false)}
      <text x="0" y="109">forward glow {Scatter.toFixed(2)} ×</text>
      <text x="300" y="109" textAnchor="end">tinted by the fog colour</text>
    </svg>
  );
}

// ── Atmospheric Fog · ridges receding into haze ─────────────────────────────────────────────────────────────────
const Ridges = [
  { At: 2000, Base: 70, Lift: 26, Seed: 1 },
  { At: 1400, Base: 84, Lift: 24, Seed: 2 },
  { At: 900, Base: 98, Lift: 22, Seed: 3 },
  { At: 500, Base: 112, Lift: 20, Seed: 4 },
  { At: 180, Base: 128, Lift: 18, Seed: 5 },
];
export function AerialRidges({ V, Change }) {
  const Id = useId().replaceAll(":", "");
  const Sigma = FogDensity("aerial-fog", V, 2, true);
  const Start = Math.max(0, +V("Start") || 0);
  const Probe = Clamp(V("Probe distance") ?? 200, 0, 2000);
  const Blend = Clamp(+V("Mie Blend") || 0, 0, 1);
  // Rayleigh haze is blue; aerosol (Mie) haze is grey-white.
  const Haze = Rgb("#8fb0d8").map((C, I) => C + (Rgb("#d2d4d8")[I] - C) * Blend);
  const Land = [24, 30, 26];
  const Transmit = (Metres) => Math.exp(-Sigma * Math.max(0, Metres));
  const Left = 0, Right = 300, Width = Right - Left;
  const X = (Metres) => Left + (Metres / 2000) * Width;
  const Ridge = ({ Base, Lift, Seed }) => {
    const Points = Array.from({ length: 31 }, (_, Index) => {
      const T = Index / 30;
      const H = Math.sin(T * 7 + Seed * 1.7) * 0.5 + Math.sin(T * 17 + Seed * 3.1) * 0.25 + Math.sin(T * 3 + Seed) * 0.6;
      return [Left + T * Width, Base - Lift * (0.5 + 0.4 * H)];
    });
    return "M" + Points.map((Point) => Point.map((Value) => Value.toFixed(1)).join(" ")).join("L") + `L${Right} 150L${Left} 150Z`;
  };
  const Edit = (Event) => {
    const Box = Event.currentTarget.ownerSVGElement.getBoundingClientRect();
    Change("Probe distance", Math.round(Clamp(((Event.clientX - Box.left) / Box.width) * 2000, 0, 2000)));
  };
  return (
    <svg className="fog-scene" viewBox="0 0 300 186" aria-label="Aerial perspective" data-live-plot="">
      <defs>
        <linearGradient id={Id + "sky"} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor={Mix([40, 70, 120], Haze, 0.25)} />
          <stop offset="1" stopColor={Mix([40, 70, 120], Haze, 0.85)} />
        </linearGradient>
        <clipPath id={Id + "clip"}>
          <rect x={Left} y="0" width={Width} height="150" rx="12" />
        </clipPath>
      </defs>
      <g clipPath={`url(#${Id}clip)`}>
        <rect x={Left} y="0" width={Width} height="150" fill={`url(#${Id}sky)`} />
        {Ridges.map((Each) => (
          <path key={Each.At} d={Ridge(Each)} fill={Mix(Land, Haze, 1 - Transmit(Each.At))} />
        ))}
        {Ridges.map((Each) => (
          <text key={Each.At} x={Right - 6} y={Each.Base - 2} textAnchor="end" className="fog-scene-faint">
            {Distance(Start + Each.At)} · {Math.round(Transmit(Each.At) * 100)}%
          </text>
        ))}
      </g>
      <g {...SliderProps({ Name: "Fog distance probe", Value: Probe, Minimum: 0, Maximum: 2000, Step: 50, Change: (Next) => Change("Probe distance", Next) })}
        className="fog-scene-handle"
        onPointerDown={(Event) => {
          Event.currentTarget.setPointerCapture(Event.pointerId);
          Edit(Event);
        }}
        onPointerMove={(Event) => Event.currentTarget.hasPointerCapture(Event.pointerId) && Edit(Event)}
      >
        <rect x={Left} y="150" width={Width} height="22" fill="transparent" />
        <line x1={Left} x2={Right} y1="158" y2="158" stroke="#3a3a3a" strokeWidth="3" strokeLinecap="round" />
        <line x1={Left} x2={X(Probe)} y1="158" y2="158" stroke="#cfcfcf" strokeWidth="3" strokeLinecap="round" />
        <line x1={X(Probe)} x2={X(Probe)} y1="0" y2="150" stroke="#ffffff" strokeOpacity=".55" strokeDasharray="3 3" />
        <circle cx={X(Probe)} cy="158" r="5" fill="#f2f2f2" />
      </g>
      <text x={Left} y="182">Start {Distance(Start)}: clear air before the haze begins</text>
      <text x={Right} y="182" textAnchor="end">probe {Distance(Start + Probe)}</text>
    </svg>
  );
}
