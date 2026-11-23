//============================================================================================================================================
//                                                              BAKEDMAPCARDS.JSX
//============================================================================================================================================
// 🧩 The BAKING sections' maps (BakedMaps.js): unbaked, the bake tab with the planned resolution and format; baked, the image the 3D
// viewport uses on labelled axes with its resolution, format, bake time and a cursor readout, with Bake again and Save PNG. Baked maps
// bake again by themselves when their settings change. Also the Lens
// Flare's composite card preview, drawn by the viewport's own flare code (LensFlare.js) over a dusk sky.

import React, { useEffect, useRef, useState } from "react";
import { FlareAtlasMap, HeldBake, WatchBakes } from "./BakedMaps.js";
import { PaintFlare } from "./LensFlare.js";
import "./BakedMapCards.css";

// One map. Make(Again) bakes it (or returns the held bake when nothing it reads changed); Signature is everything it reads. A map nobody
// has baked yet (here or in the viewport) shows the bake tab (Kit.Tile, Kit.Glyph from the inspector) with its planned resolution and
// format; once baked it stays live, baking again when its settings change. Axes ({ X, Y }: { Name, At(0–1) → amount or label, Unit,
// Ticks }) label the image and give the cursor readout. Marker ({ At: 0–1 across, Label }) points at a column.
const Reading = (Axis, Fraction, Fine) => {
  const Amount = Axis.At(Fraction);
  return typeof Amount === "number" ? Amount.toFixed(Fine ? (Math.abs(Amount) < 10 ? 2 : 1) : 0) + (Axis.Unit || "") : Amount;
};
export function BakedMap({ Name, Make, Signature, Planned, Axes = null, Marker = null, Pixelated = false, Kit }) {
  const Shown = useRef(null);
  const [Result, Keep] = useState(() => HeldBake(Name));
  const [Cursor, Point] = useState(null);
  const Run = (Again) => {
    try {
      Keep(Make(Again));
    } catch (Problem) {
      Keep({ Problem: String(Problem?.message || Problem) });
    }
  };
  useEffect(() => {
    const Timer = setTimeout(() => (HeldBake(Name) ? Run(false) : Keep(null)), 40);
    return () => clearTimeout(Timer);
  }, [Signature]);
  // The viewport may bake it first.
  useEffect(() => WatchBakes((Baked) => Baked === Name && !Result && Run(false)), [Result]);
  useEffect(() => {
    const Target = Shown.current;
    if (!Target || !Result?.Canvas) return;
    Target.width = Result.Width;
    Target.height = Result.Height;
    Target.getContext("2d").drawImage(Result.Canvas, 0, 0);
  }, [Result]);
  const Save = () =>
    Shown.current?.toBlob((Picture) => {
      if (!Picture) return;
      const Link = document.createElement("a");
      Link.href = URL.createObjectURL(Picture);
      Link.download = Name.replace(/\s+/g, "-") + ".png";
      Link.click();
      setTimeout(() => URL.revokeObjectURL(Link.href), 2000);
    }, "image/png");
  const Baked = !!Result?.Canvas;
  if (!Baked && !Result?.Problem)
    return (
      <figure className="baked-map unbaked" data-baked-map={Name} data-baked-count={0} data-baked-size="" data-baked-state="unbaked">
        <div className="bake-content">
          <Kit.Tile Label="Use baked image" Disabled IconName="bake" />
          <div>
            <button type="button" onClick={() => Run(true)} aria-label={"Bake " + Name}>
              <Kit.Glyph Name="bake" /> Bake
            </button>
            <button disabled>↥　Load image</button>
            <p>Not baked</p>
          </div>
        </div>
        <dl className="baked-map-facts">
          <dt>Map</dt>
          <dd>{Name}</dd>
          <dt>Resolution</dt>
          <dd>{Planned[0]}</dd>
          <dt>Format</dt>
          <dd>{Planned[1]}</dd>
        </dl>
      </figure>
    );
  const Ticks = (Axis) => (Axis.Ticks || [0, 0.25, 0.5, 0.75, 1]).map((Fraction) => [Fraction, Reading(Axis, Fraction, false)]);
  const Track = (Event) => {
    const Rect = Event.currentTarget.getBoundingClientRect();
    const U = Math.max(0, Math.min(0.9999, (Event.clientX - Rect.left) / Rect.width)), V = Math.max(0, Math.min(0.9999, (Event.clientY - Rect.top) / Rect.height));
    const Pixel = Result.Canvas.getContext("2d").getImageData(Math.floor(U * Result.Width), Math.floor(V * Result.Height), 1, 1).data;
    Point({ U, V, Colour: [Pixel[0], Pixel[1], Pixel[2]] });
  };
  return (
    <figure className="baked-map" data-baked-map={Name} data-baked-count={Result?.Count || 0} data-baked-size={Baked ? `${Result.Width}x${Result.Height}` : ""} data-baked-state="baked">
      <div className={"baked-map-plot" + (Axes ? " with-axes" : "")}>
        {Axes && (
          <div className="baked-map-y" aria-hidden="true">
            {Ticks(Axes.Y).map(([Fraction, Label]) => (
              <span key={Fraction} style={{ top: Fraction * 100 + "%" }}>
                {Label}
              </span>
            ))}
          </div>
        )}
        <div
          className={"baked-map-image" + (Pixelated ? " pixelated" : "")}
          style={{ aspectRatio: Baked ? `${Result.Width} / ${Result.Height}` : "2 / 1" }}
          onPointerMove={Baked ? Track : undefined}
          onPointerLeave={() => Point(null)}
        >
          <canvas ref={Shown} aria-label={Name} />
          {Result?.Problem && <span className="baked-map-wait">Bake failed · {Result.Problem}</span>}
          {Marker && (
            <i className="baked-map-marker" style={{ left: `${Math.max(0, Math.min(1, Marker.At)) * 100}%` }}>
              <span>{Marker.Label}</span>
            </i>
          )}
          {Cursor && <i className="baked-map-cursor" style={{ left: Cursor.U * 100 + "%", top: Cursor.V * 100 + "%" }} />}
        </div>
        {Axes && (
          <div className="baked-map-x" aria-hidden="true">
            {Ticks(Axes.X).map(([Fraction, Label]) => (
              <span key={Fraction} style={{ left: Fraction * 100 + "%" }}>
                {Label}
              </span>
            ))}
          </div>
        )}
      </div>
      <dl className="baked-map-facts">
        <dt>Map</dt>
        <dd>{Name}</dd>
        <dt>Resolution</dt>
        <dd data-baked-resolution>{Baked ? `${Result.Width} × ${Result.Height}` : "–"}</dd>
        <dt>Format</dt>
        <dd>{Result?.Format || "–"}</dd>
        <dt>Baked</dt>
        <dd>{Baked ? `${new Date(Result.At).toLocaleTimeString()} · ${Result.Milliseconds.toFixed(0)} ms · #${Result.Count}` : "–"}</dd>
        {Axes && (
          <>
            <dt>X</dt>
            <dd>{`${Axes.X.Name} ${Reading(Axes.X, 0, false)} → ${Reading(Axes.X, 1, false)}`}</dd>
            <dt>Y</dt>
            <dd>{`${Axes.Y.Name} ${Reading(Axes.Y, 0, false)} → ${Reading(Axes.Y, 1, false)}`}</dd>
          </>
        )}
        {Baked && (
          <>
            <dt>Cursor</dt>
            <dd data-baked-cursor>
              {Cursor ? (
                <>
                  {Axes ? `${Reading(Axes.X, Cursor.U, true)}, ${Reading(Axes.Y, Cursor.V, true)}` : `${Math.floor(Cursor.U * Result.Width)}, ${Math.floor(Cursor.V * Result.Height)} px`}
                  <b style={{ background: `rgb(${Cursor.Colour.join(",")})` }} />
                  {`rgb(${Cursor.Colour.join(", ")})`}
                </>
              ) : (
                "–"
              )}
            </dd>
          </>
        )}
      </dl>
      <div className="baked-map-status">
        <button type="button" onClick={() => Run(true)}>
          Bake again
        </button>
        <button type="button" onClick={Save} disabled={!Baked}>
          Save PNG
        </button>
      </div>
    </figure>
  );
}

// The Flare composite card: the flare as the viewport draws it (same atlas, same compositing) for a sun at Preview X / Y over a dusk
// sky. Dragging moves the sun; only this preview changes.
export function FlarePreview({ Settings, Tint, X, Y, Move }) {
  const Box = useRef(null), Shown = useRef(null);
  const [Size, Resize] = useState([320, 196]);
  useEffect(() => {
    // A shut tab measures 0 × 0; keep the last real size.
    const Watch = new ResizeObserver(() => Box.current?.clientWidth && Resize([Box.current.clientWidth, Box.current.clientHeight]));
    Watch.observe(Box.current);
    return () => Watch.disconnect();
  }, []);
  const Key = JSON.stringify([Settings, Tint, X, Y, Size]);
  useEffect(() => {
    const Target = Shown.current;
    if (!Target) return;
    const [Width, Height] = Size, Ratio = window.devicePixelRatio || 1;
    Target.width = Math.max(1, Math.round(Width * Ratio));
    Target.height = Math.max(1, Math.round(Height * Ratio));
    const Brush = Target.getContext("2d");
    Brush.setTransform(Ratio, 0, 0, Ratio, 0, 0);
    const Sky = Brush.createLinearGradient(0, 0, 0, Height);
    Sky.addColorStop(0, "#0d1426");
    Sky.addColorStop(0.62, "#2a2c3e");
    Sky.addColorStop(0.8, "#5a4038");
    Sky.addColorStop(0.81, "#141210");
    Sky.addColorStop(1, "#0a0909");
    Brush.fillStyle = Sky;
    Brush.fillRect(0, 0, Width, Height);
    const Atlas = Settings.Enabled ? FlareAtlasMap(Settings, Tint).Canvas : null;
    const Drawn = PaintFlare(Brush, Atlas, Settings, { X: X * Width, Y: Y * Height, Width, Height, Strength: 1 });
    Target.dataset.sprites = String(Drawn);
  }, [Key]);
  return (
    <div
      ref={Box}
      className="flare-preview"
      onPointerDown={(Event) => {
        Event.currentTarget.setPointerCapture(Event.pointerId);
        Drag(Event);
      }}
      onPointerMove={(Event) => Event.buttons && Drag(Event)}
    >
      <canvas ref={Shown} data-flare-preview aria-label="Lens flare preview" />
    </div>
  );
  function Drag(Event) {
    const Rect = Event.currentTarget.getBoundingClientRect();
    Move(Math.max(0.06, Math.min(0.94, (Event.clientX - Rect.left) / Rect.width)), Math.max(0.08, Math.min(0.92, (Event.clientY - Rect.top) / Rect.height)));
  }
}
