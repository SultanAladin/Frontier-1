//============================================================================================================================================
//                                                               SUNCARDS.JSX
//============================================================================================================================================
// 🧩 The Sun panel's first card: the real sky and sun at this time and place, rendered by SkyShader.js from the Sun's date, time and
// location (Solar.mjs) and the Atmosphere object's settings. Three framings, stored as "Sun card design" on the sun:
//   A · Sky     standing on the ground, facing the sun.
//   B · Day     a wide view of the sky towards noon with the sun's whole path for the day, sunrise and sunset on the horizon.
//   C · Disc    a close-up of the sun itself: its colour and shape through the air (low suns go orange and sink into the ground).
// Each shows the time and one line under it. Drag the picture (or use the arrow keys) to move through the day; the bar underneath
// is the whole day, night to night. In A and B the disc is drawn larger than life so it reads; in C it is true size.

import React, { useEffect, useRef, useState } from "react";
import { Solar } from "./Solar.mjs";
import { SkyMedium, PaintSky, ViewBasis, ProjectDirection, Direction, KelvinTint, SunAngularRadius } from "./SkyShader.js";

export const SunDesigns = [
  ["sky", "A", "Sky"],
  ["day", "B", "Day"],
  ["disc", "C", "Disc"],
];

const Rad = Math.PI / 180;
const Compass = ["north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west"];
const Facing = (Azimuth) => Compass[Math.round((((Azimuth % 360) + 360) % 360) / 45) % 8];
const Wrap24 = (Hours) => ((Hours % 24) + 24) % 24;
export const Clock = (Hours) => {
  const Minutes = Math.round(Wrap24(Hours) * 60) % 1440;
  return String(Math.floor(Minutes / 60)).padStart(2, "0") + ":" + String(Minutes % 60).padStart(2, "0");
};
const Span = (Hours) => `${Math.floor(Hours)} h ${String(Math.round((Hours % 1) * 60)).padStart(2, "0")}`;

// The day at the sun's place: where it is now, its path every six minutes, sunrise, sunset and the highest point.
export function SunDay(V) {
  const Place = { Month: V("Month"), Day: V("Day of Month"), Latitude: V("Latitude"), Longitude: V("Longitude") };
  const Hours = Wrap24(V("Local Hours") ?? 12);
  const Manual = !!V("Manual direction");
  const Solved = Solar({ ...Place, LocalHours: Hours });
  const Elevation = Manual ? V("Manual elevation") ?? Solved.Elevation : Solved.Elevation;
  const Azimuth = Manual ? V("Manual azimuth") ?? Solved.Azimuth : Solved.Azimuth;
  const Samples = Array.from({ length: 241 }, (_, Step) => ({ Hours: Step / 10, ...Solar({ ...Place, LocalHours: Step / 10 }) }));
  const Crossing = (Rising) => {
    for (let Step = 1; Step < Samples.length; Step++) {
      const [A, B] = [Samples[Step - 1].Elevation + 0.83, Samples[Step].Elevation + 0.83];
      if (Rising ? A < 0 && B >= 0 : A >= 0 && B < 0) return Samples[Step - 1].Hours + (A / (A - B)) * 0.1;
    }
    return null;
  };
  const Rise = Crossing(true), Sunset = Crossing(false);
  const Length = Rise != null && Sunset != null ? (Sunset - Rise + 24) % 24 : Samples[120].Elevation > 0 ? 24 : 0;
  const Peak = Samples.reduce((Best, Sample) => (Sample.Elevation > Best.Elevation ? Sample : Best), Samples[0]);
  return { Hours, Elevation, Azimuth, Manual, Samples, Rise, Sunset, Length, Peak, Kelvin: V("Temperature") ?? 6500 };
}

// The camera for each design, given the image's aspect.
export function SunView(Design, Day, Aspect) {
  if (Design === "day") {
    const Top = Math.min(80, Math.max(30, Day.Peak.Elevation + 14)) * Rad;
    const Shift = 0.56;
    return {
      Mode: "panorama", Basis: ViewBasis(Direction(0, Day.Peak.Azimuth)), Span: 240 * Rad, Shift,
      TanHalf: Math.tan(Top) / (1 + Shift), Disc: SunAngularRadius * 4, Glow: 0.5, Exposure: 1.1,
    };
  }
  if (Design === "disc") {
    const Low = Day.Elevation < -1.2;
    const Look = Low ? Direction(1.6, Day.Azimuth) : Direction(Day.Elevation, Day.Azimuth);
    return { Mode: "perspective", Basis: ViewBasis(Look), TanHalf: Math.tan((Low ? 9 : 2.4) * Rad), Disc: SunAngularRadius, Glow: 0.3, Exposure: Low ? 1.1 : 0.32, Low };
  }
  // A · facing the sun, horizon low in the frame; tilt up when the sun is high.
  const HalfHeight = 24;
  const Pitch = Math.max(HalfHeight * 0.55, Math.min(70, Day.Elevation - HalfHeight * 0.35));
  return { Mode: "perspective", Basis: ViewBasis(Direction(Pitch, Day.Azimuth)), TanHalf: Math.tan(HalfHeight * Rad), Disc: SunAngularRadius * 3, Glow: 0.45, Exposure: 1.1 };
}

const PhaseTone = (Elevation) =>
  Elevation > 6 ? "#f3d9a4" : Elevation > -0.83 ? "#f0965c" : Elevation > -6 ? "#5e5aa8" : Elevation > -12 ? "#2f3576" : Elevation > -18 ? "#1a1f45" : "#0c0e1e";

function useSkyPicture(Paint, Key) {
  const Host = useRef(null), Canvas = useRef(null), Pending = useRef(0);
  const [Size, Resize] = useState([0, 0]);
  const [Unsupported, MarkUnsupported] = useState(false);
  useEffect(() => {
    const Observer = new ResizeObserver(([Entry]) => Resize([Math.round(Entry.contentRect.width), Math.round(Entry.contentRect.height)]));
    Observer.observe(Host.current);
    return () => Observer.disconnect();
  }, []);
  useEffect(() => {
    if (!Size[0]) return;
    cancelAnimationFrame(Pending.current);
    Pending.current = requestAnimationFrame(() => {
      const Target = Canvas.current;
      if (!Target) return;
      const Density = Math.min(1.5, devicePixelRatio || 1);
      Target.width = Math.round(Size[0] * Density);
      Target.height = Math.round(Size[1] * Density);
      MarkUnsupported(!Paint(Target));
      Target.dataset.painted = Key + Size.join("x");
    });
    return () => cancelAnimationFrame(Pending.current);
  }, [Key, Size[0], Size[1]]);
  return { Host, Canvas, Size, Unsupported };
}

export default function SunCard({ V, Change, Air = () => undefined }) {
  const Day = SunDay(V);
  const Stored = V("Sun card design");
  const Design = SunDesigns.some(([Id]) => Id === Stored) ? Stored : "sky";
  // The ground in these views is seen at a grazing angle and reads much brighter than the sky; draw it at half its albedo.
  const Authored = SkyMedium(Air);
  const Medium = { ...Authored, Ground: Authored.Ground.map((C) => C * 0.5) };
  const Light = { Sun: Direction(Day.Elevation, Day.Azimuth), Tint: KelvinTint(Day.Kelvin) };
  const Key = JSON.stringify([Design, Medium, Day.Elevation, Day.Azimuth, Day.Kelvin]);
  const Picture = useSkyPicture((Target) => PaintSky(Target, Medium, Light, SunView(Design, Day, Target.width / Target.height)), Key);
  const [Width, Height] = Picture.Size;
  const View = Width ? SunView(Design, Day, Width / Height) : null;
  const SetHours = (Hours) => Change("Local Hours", Math.round(Wrap24(Hours) * 100) / 100);
  const Grab = useRef(null);
  // Drag: in B the time follows the pointer along the path; elsewhere moving across the picture moves through the day.
  const PathPoints = View && Design === "day" ? Day.Samples.map((Sample) => [Sample.Hours, ProjectDirection(View, Direction(Sample.Elevation, Sample.Azimuth), Width / Height)]) : [];
  const Pick = (Event) => {
    const Box = Picture.Host.current.getBoundingClientRect();
    const X = (Event.clientX - Box.left) / Box.width, Y = (Event.clientY - Box.top) / Box.height;
    if (Design === "day") {
      const Above = PathPoints.filter(([, At]) => At && At[1] < 0.8);
      const Nearest = (Above.length ? Above : PathPoints).reduce((Best, Each) => {
        const Distance = Each[1] ? Math.hypot((Each[1][0] - X) * Width, (Each[1][1] - Y) * Height) : Infinity;
        return Distance < Best[0] ? [Distance, Each[0]] : Best;
      }, [Infinity, Day.Hours]);
      SetHours(Nearest[1]);
    } else if (Grab.current) {
      SetHours(Grab.current.Hours + ((Event.clientX - Grab.current.X) / Box.width) * (Design === "disc" ? 3 : 8));
    }
  };
  const Keys = (Event) => {
    const Step = { ArrowLeft: -0.25, ArrowRight: 0.25, ArrowDown: -1, ArrowUp: 1, PageDown: -3, PageUp: 3 }[Event.key];
    if (Step == null) return;
    Event.preventDefault();
    SetHours(Day.Hours + Step);
  };
  const Up = Day.Elevation > -0.83;
  const Line =
    Design === "day"
      ? Day.Rise != null && Day.Sunset != null
        ? `${Span(Day.Length)} of daylight · highest ${Day.Peak.Elevation.toFixed(0)}° at ${Clock(Day.Peak.Hours)}`
        : Day.Length >= 24
          ? "The sun doesn't set today"
          : "The sun doesn't rise today"
      : Design === "disc"
        ? Up
          ? `${Day.Elevation.toFixed(1)}° above the horizon`
          : `${Math.abs(Day.Elevation).toFixed(1)}° below the horizon`
        : Up
          ? `${Day.Elevation.toFixed(0)}° up, facing ${Facing(Day.Azimuth)}`
          : `Below the horizon, ${Facing(Day.Azimuth)}`;
  const Path = PathPoints.filter(([, At]) => At).map(([, At]) => [At[0] * Width, At[1] * Height]);
  const Marks = Design === "day" ? [["rise", Day.Rise], ["set", Day.Sunset]].filter(([, Hours]) => Hours != null) : [];
  const BarAt = (Hours) => `${(Wrap24(Hours) / 24) * 100}%`;
  const Bar = useRef(null);
  const BarPick = (Event) => {
    const Box = Bar.current.getBoundingClientRect();
    SetHours(Math.max(0, Math.min(23.99, ((Event.clientX - Box.left) / Box.width) * 24)));
  };
  return (
    <div className="sun-card" data-design={Design}>
      <div
        className="sun-picture"
        ref={Picture.Host}
        role="slider"
        tabIndex={0}
        aria-label="Time of day"
        aria-valuemin={0}
        aria-valuemax={24}
        aria-valuenow={Math.round(Day.Hours * 100) / 100}
        aria-valuetext={Clock(Day.Hours)}
        data-sun-design={Design}
        onKeyDown={Keys}
        onPointerDown={(Event) => {
          Event.currentTarget.setPointerCapture(Event.pointerId);
          Grab.current = { X: Event.clientX, Hours: Day.Hours };
          if (Design === "day") Pick(Event);
        }}
        onPointerMove={(Event) => Event.buttons & 1 && Pick(Event)}
        onPointerUp={() => (Grab.current = null)}
      >
        <canvas ref={Picture.Canvas} aria-hidden="true" data-sun-picture="" />
        {Picture.Unsupported && <p className="sun-picture-fallback">This browser has no WebGL2, so the sky can't be drawn.</p>}
        {Width > 0 && Design === "day" && (
          <svg className="sun-picture-overlay" viewBox={`0 0 ${Width} ${Height}`} aria-hidden="true">
            <polyline data-sun-path="" points={Path.map(([X, Y]) => `${X.toFixed(1)},${Y.toFixed(1)}`).join(" ")} />
            {Marks.map(([Name, Hours]) => {
              const At = ProjectDirection(View, Direction(-0.83, Day.Samples[Math.round(Hours * 10) % 240].Azimuth), Width / Height);
              return (
                At && (
                  <g key={Name} data-sun-mark={Name} aria-label={`${Name === "rise" ? "Sunrise" : "Sunset"} ${Clock(Hours)}`} transform={`translate(${Math.max(20, Math.min(Width - 20, At[0] * Width)).toFixed(1)} ${(At[1] * Height).toFixed(1)})`}>
                    <line y1="-6" y2="6" />
                  </g>
                )
              );
            })}
          </svg>
        )}
        <div className="sun-picture-text">
          <b data-sun-time="">{Clock(Day.Hours)}</b>
          <span data-sun-line="">{Line}</span>
        </div>
        <div className="sun-picture-designs" role="radiogroup" aria-label="Card design" onPointerDown={(Event) => Event.stopPropagation()}>
          {SunDesigns.map(([Id, Letter, Name]) => (
            <button key={Id} type="button" role="radio" aria-checked={Design === Id} aria-label={`${Letter} · ${Name}`} title={Name} data-sun-design-pick={Id} onClick={() => Change("Sun card design", Id)}>
              {Letter}
            </button>
          ))}
        </div>
      </div>
      <div
        className="sun-day-bar"
        ref={Bar}
        style={{ background: `linear-gradient(90deg, ${Day.Samples.filter((_, Index) => Index % 5 === 0).map((Sample) => `${PhaseTone(Sample.Elevation)} ${((Sample.Hours / 24) * 100).toFixed(1)}%`).join(", ")})` }}
        onPointerDown={(Event) => {
          Event.currentTarget.setPointerCapture(Event.pointerId);
          BarPick(Event);
        }}
        onPointerMove={(Event) => Event.buttons & 1 && BarPick(Event)}
        data-sun-day-bar=""
        aria-hidden="true"
      >
        <i style={{ left: BarAt(Day.Hours) }} />
      </div>
      <div className="sun-day-labels" aria-hidden="true">
        {Day.Rise != null && <span style={{ left: BarAt(Day.Rise) }} data-sun-fact="Sunrise">{Clock(Day.Rise)}</span>}
        {Day.Sunset != null && <span style={{ left: BarAt(Day.Sunset) }} data-sun-fact="Sunset">{Clock(Day.Sunset)}</span>}
      </div>
    </div>
  );
}
