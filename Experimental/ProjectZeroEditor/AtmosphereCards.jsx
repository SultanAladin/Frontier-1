//============================================================================================================================================
//                                                           ATMOSPHERECARDS.JSX
//============================================================================================================================================
// 🧩 The Atmosphere panel's first card: the planet's limb seen from orbit, rendered by SkyShader.js from the authored atmosphere
// (Rayleigh, Mie, ozone, scale heights, tint, brightness, ground albedo). The image fills the card. Drag the dashed line (the top of
// the atmosphere) up or down to set "Atmosphere"; the slider under the image moves the sun at the limb (day on the left, the sunset band,
// night on the right). The planet is drawn at 1:12 so the air layer is readable; heights are true to the settings.

import React, { useEffect, useRef, useState } from "react";
import { SkyMedium, PaintSky, ViewBasis, ProjectDirection, RayThrough } from "./SkyShader.js";

const Rad = Math.PI / 180;
export const LimbScale = 12;
export const TopRange = [10000, 120000];

// The limb camera: above a 1:12 planet, looking just over its edge.
export function LimbView(Medium, Aspect) {
  const Planet = Medium.Planet / LimbScale, Altitude = 0.42 * Planet;
  const Eye = [0, Planet + Altitude, 0];
  const Dip = Math.acos(Planet / (Planet + Altitude));
  const TanHalf = Math.tan(17 * Rad);
  const Pitch = Dip - Math.atan(0.26 * TanHalf);
  const Basis = ViewBasis([0, -Math.sin(Pitch), Math.cos(Pitch)]);
  return { Mode: "perspective", Eye, Planet, Dip, TanHalf, Basis, Aspect, Warp: false, Disc: 0, Glow: 0, Stars: 1, Exposure: 0.9 };
}
// Sun direction with its elevation `Elevation` at the middle of the visible limb, coming from the left (+x is screen left here).
export function LimbSun(View, Elevation) {
  const Normal = [0, Math.cos(View.Dip), Math.sin(View.Dip)];
  return [Math.cos(Elevation * Rad) + Normal[0] * Math.sin(Elevation * Rad), Normal[1] * Math.sin(Elevation * Rad), Normal[2] * Math.sin(Elevation * Rad)];
}
// Screen outline of the air layer's top: the silhouette of a sphere of radius Planet + Top, as fractions of the image.
export function TopOutline(View, Top) {
  const Distance = Math.hypot(...View.Eye), Half = Math.asin(Math.min(1, (View.Planet + Top) / Distance));
  const Points = [];
  for (let Step = 0; Step <= 96; Step++) {
    const Around = (Step / 96) * Math.PI;
    const Ray = [Math.sin(Half) * Math.cos(Around), -Math.cos(Half), Math.sin(Half) * Math.sin(Around)];
    const At = ProjectDirection(View, Ray, View.Aspect);
    if (At && At[0] > -0.1 && At[0] < 1.1) Points.push(At);
  }
  return Points;
}
// The top that puts the outline through a point of the image (fractions), clamped to the field's range.
export function TopThrough(View, X, Y) {
  const Ray = RayThrough(View, X, Y, View.Aspect);
  const Off = Math.acos(Math.max(-1, Math.min(1, -Ray[1])));
  const Top = Math.hypot(...View.Eye) * Math.sin(Off) - View.Planet;
  return Math.round(Math.max(TopRange[0], Math.min(TopRange[1], Top)) / 500) * 500;
}

export default function AtmosphereCard({ V, Change }) {
  const Canvas = useRef(null), Host = useRef(null), Pending = useRef(0);
  const [Size, Resize] = useState([0, 0]);
  const [Unsupported, MarkUnsupported] = useState(false);
  const Medium = SkyMedium(V);
  const SunAt = V("Limb sun elevation") ?? 6;
  const Aspect = Size[1] ? Size[0] / Size[1] : 1.45;
  const View = LimbView(Medium, Aspect);
  const Sun = LimbSun(View, SunAt);
  const Key = JSON.stringify([Medium, SunAt, Size]);
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
      // Seen from orbit the planet is mostly ocean: its ground is drawn at 25 % of the authored albedo so the air layer reads.
      MarkUnsupported(!PaintSky(Target, { ...Medium, Planet: View.Planet, Ground: Medium.Ground.map((C) => C * 0.25) }, { Sun }, View));
      Target.dataset.painted = Key;
    });
    return () => cancelAnimationFrame(Pending.current);
  }, [Key]);
  const Outline = Size[0] ? TopOutline(View, Medium.Top) : [];
  const Middle = Outline.reduce((Best, Point) => (Math.abs(Point[0] - 0.5) < Math.abs(Best[0] - 0.5) ? Point : Best), Outline[0] || [0.5, 0.3]);
  const Drag = (Event) => {
    const Box = Host.current.getBoundingClientRect();
    Change("Atmosphere", TopThrough(View, (Event.clientX - Box.left) / Box.width, (Event.clientY - Box.top) / Box.height));
  };
  const Nudge = (Event) => {
    const Step = { ArrowUp: 1000, ArrowRight: 1000, ArrowDown: -1000, ArrowLeft: -1000, PageUp: 10000, PageDown: -10000 }[Event.key];
    if (!Step) return;
    Event.preventDefault();
    Change("Atmosphere", Math.max(TopRange[0], Math.min(TopRange[1], Medium.Top + Step)));
  };
  return (
    <section className="property-card atmosphere-card" data-card="Atmosphere overview">
      <header className="atmosphere-card-head">
        <h3>Atmosphere overview</h3>
      </header>
      <div className="atmosphere-limb" ref={Host}>
        <canvas ref={Canvas} role="img" aria-label="The planet's limb from orbit, rendered from the atmosphere settings" data-atmosphere-limb="" />
        {Unsupported && <p className="atmosphere-limb-fallback">This browser has no WebGL2, so the limb can't be drawn.</p>}
        {Size[0] > 0 && (
          <svg viewBox={`0 0 ${Size[0]} ${Size[1]}`} className="atmosphere-limb-overlay">
            <polyline
              data-top-outline=""
              points={Outline.map(([X, Y]) => `${(X * Size[0]).toFixed(1)},${(Y * Size[1]).toFixed(1)}`).join(" ")}
              fill="none"
            />
            <polyline
              className="atmosphere-limb-grab"
              points={Outline.map(([X, Y]) => `${(X * Size[0]).toFixed(1)},${(Y * Size[1]).toFixed(1)}`).join(" ")}
              fill="none"
              onPointerDown={(Event) => {
                Event.currentTarget.setPointerCapture(Event.pointerId);
                Drag(Event);
              }}
              onPointerMove={(Event) => Event.buttons & 1 && Drag(Event)}
            />
            <g
              className="atmosphere-limb-handle"
              transform={`translate(${(Middle[0] * Size[0]).toFixed(1)} ${(Middle[1] * Size[1]).toFixed(1)})`}
              role="slider"
              tabIndex={0}
              aria-label="Atmosphere height"
              aria-valuemin={TopRange[0] / 1000}
              aria-valuemax={TopRange[1] / 1000}
              aria-valuenow={Math.round(Medium.Top / 1000)}
              aria-valuetext={`${Math.round(Medium.Top / 1000)} km`}
              data-top-handle=""
              onKeyDown={Nudge}
              onPointerDown={(Event) => {
                Event.currentTarget.setPointerCapture(Event.pointerId);
              }}
              onPointerMove={(Event) => Event.buttons & 1 && Drag(Event)}
            >
              <circle r="11" className="atmosphere-limb-hit" />
              <circle r="4.5" />
              <text y="-10" textAnchor="middle">{Math.round(Medium.Top / 1000)} km</text>
            </g>
          </svg>
        )}
      </div>
      <label className="atmosphere-card-sun">
        <span>Sun at the limb</span>
        <input type="range" min={-12} max={40} step={0.5} value={SunAt} aria-label="Sun at the limb" onChange={(Event) => Change("Limb sun elevation", +Event.target.value)} />
        <b>{SunAt.toFixed(1)}°</b>
      </label>
    </section>
  );
}
