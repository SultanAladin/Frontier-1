//============================================================================================================================================
//                                                          CAPTUREWORKSPACE.JSX
//============================================================================================================================================
// 🧩 Icon Capture workspace: a square capture box over the isolated selection, the Geometry Compiler's icon framing (IconCapture.js:
// three-quarter default angle, yaw 0.7 / pitch 0.38, the bounding sphere filling 0.82 of the box's half-height) and a transparent PNG
// at 128, 256 or 512 px. Only the framing rules come from the compiler; drawing reuses the workspace rasteriser. The saved thumbnail
// (128 px, so the scene stays small in browser storage) lives in Values["Icon Capture"]; Download keeps the full size.

import React, { useMemo, useState } from "react";
import { computeBounds } from "../Shared/GeometryCompile/Engine/MeshAnalysis.js";
import { PrimitiveSurface, OrbitView, PaintPolygons, Normal, Shade } from "./WorkspaceGeometry.js";
import { ShapeOf } from "./PrimitiveInspector.jsx";
import { Surface, useOrbit, Clamp, GeometryKeys } from "./WorkspaceSurface.jsx";

export const CaptureKey = "Icon Capture";
export const CaptureAngle = { Yaw: 0.7, Pitch: 0.38 };
export const CaptureFill = 0.82;
const Sizes = [128, 256, 512];
const Looks = ["Clay", "Normals"];
const ThumbnailSide = 128;

export function useCaptureBody(Subject, Values) {
  const Shape = ShapeOf(Subject);
  const Scale = Array.isArray(Values.Scale) ? Values.Scale.map((Each) => Math.abs(+Each) || 1) : [1, 1, 1];
  const Key = JSON.stringify([Shape, Scale, GeometryKeys.map((Name) => Values[Name])]);
  return useMemo(() => {
    const Body = PrimitiveSurface(Shape, (Name, Initial) => (Values[Name] === undefined ? Initial : Values[Name]), Scale);
    const Bounds = computeBounds(Body.Positions);
    // Centre the body on its bounds so the orthographic orbit turns about the middle of the box.
    const P = Body.Positions, Triangles = [];
    for (let T = 0; T < Body.Indices.length; T += 3) Triangles.push([0, 1, 2].map((K) => [0, 1, 2].map((Axis) => P[Body.Indices[T + K] * 3 + Axis] - Bounds.centre[Axis])));
    return { Triangles, Radius: Bounds.radius || 1, Size: Bounds.size };
  }, [Key]);
}

const CaptureOf = (Workspace) => ({
  Yaw: Workspace.Yaw ?? CaptureAngle.Yaw,
  Pitch: Workspace.Pitch ?? CaptureAngle.Pitch,
  Zoom: Workspace.Zoom ?? 1,
  Size: Sizes.includes(Workspace.CaptureSize) ? Workspace.CaptureSize : 256,
  Look: Clamp(Workspace.CaptureLook ?? 0, 0, Looks.length - 1),
});

// Draws the body into a square of `Side` pixels: with Zoom 1 the bounding sphere fills CaptureFill of the half side.
export function PaintIcon(Brush, Side, Body, { Yaw, Pitch, Zoom, Look }, OffsetX = 0, OffsetY = 0) {
  const View = OrbitView({ Yaw, Pitch, Radius: Body.Radius, Width: Side, Height: Side, Zoom: (Zoom * CaptureFill) / 0.8 });
  const Project = (Point) => {
    const [X, Y, Z] = View.Project(Point);
    return [X + OffsetX, Y + OffsetY, Z];
  };
  const Polygons = Body.Triangles.map((Points) => {
    const N = Normal(...Points);
    return {
      Points,
      Normal: N,
      Paint: (B, Screen, Lit) => {
        B.beginPath();
        Screen.forEach(([X, Y], Index) => (Index ? B.lineTo(X, Y) : B.moveTo(X, Y)));
        B.closePath();
        const Tone = Look === 1 ? `rgb(${N.map((Each) => Math.round(127.5 + 127.5 * Each)).join(",")})` : `rgb(${[200, 196, 190].map((Each) => Math.round(Each * Lit)).join(",")})`;
        B.fillStyle = Tone;
        B.strokeStyle = Tone;
        // Two anti-aliased edges never sum to full coverage, so a hairline seam would let the transparent background through; a
        // 1.4 px stroke in the face's own tone closes it (round joins keep acute corners from spiking).
        B.lineWidth = 1.4;
        B.lineJoin = "round";
        B.fill();
        B.stroke();
      },
    };
  });
  return PaintPolygons(Brush, { ...View, Project }, Polygons);
}

// Renders the PNG off screen. Returns the canvas, its data URL and how much of the square the object covers.
export function CaptureIcon(Body, Capture) {
  const Canvas = document.createElement("canvas");
  Canvas.width = Canvas.height = Capture.Size;
  const Brush = Canvas.getContext("2d");
  PaintIcon(Brush, Capture.Size, Body, Capture);
  const Pixels = Brush.getImageData(0, 0, Capture.Size, Capture.Size).data;
  let Filled = 0;
  for (let Index = 3; Index < Pixels.length; Index += 4) if (Pixels[Index]) Filled++;
  return { Canvas, Url: Canvas.toDataURL("image/png"), Coverage: Filled / (Capture.Size * Capture.Size) };
}

function Thumbnail(Canvas) {
  const Small = document.createElement("canvas");
  Small.width = Small.height = ThumbnailSide;
  const Brush = Small.getContext("2d");
  Brush.imageSmoothingQuality = "high";
  Brush.drawImage(Canvas, 0, 0, ThumbnailSide, ThumbnailSide);
  return Small.toDataURL("image/png");
}

export function CaptureView({ Subject, Values, Workspace, SetWorkspace }) {
  const Body = useCaptureBody(Subject, Values);
  const Orbit = useOrbit(Workspace, SetWorkspace);
  const Capture = CaptureOf(Workspace);
  const Paint = (Brush, Width, Height) => {
    // The capture box is the exact crop of the PNG: 80 % of the pane's short side, centred.
    const Side = Math.max(120, Math.round(Math.min(Width, Height) * 0.8)), Left = (Width - Side) / 2, Top = (Height - Side) / 2;
    Brush.save();
    Brush.globalAlpha = 0.35;
    // Same framing unclipped, faint, so a part pushed outside the box still shows where it went.
    PaintIcon(Brush, Side, Body, Capture, Left, Top);
    Brush.restore();
    Brush.save();
    Brush.beginPath();
    Brush.rect(Left, Top, Side, Side);
    Brush.clip();
    const Cell = Math.max(8, Side / 16);
    for (let Y = 0; Y < Side; Y += Cell)
      for (let X = 0; X < Side; X += Cell) {
        Brush.fillStyle = (Math.floor(X / Cell) + Math.floor(Y / Cell)) % 2 ? "#202020" : "#181818";
        Brush.fillRect(Left + X, Top + Y, Cell, Cell);
      }
    PaintIcon(Brush, Side, Body, Capture, Left, Top);
    Brush.restore();
    Brush.strokeStyle = "#e8e8e8";
    Brush.lineWidth = 1;
    Brush.setLineDash([]);
    Brush.strokeRect(Left + 0.5, Top + 0.5, Side - 1, Side - 1);
    const Corner = Math.min(18, Side / 8);
    Brush.lineWidth = 2.5;
    for (const [X, Y, Dx, Dy] of [[Left, Top, 1, 1], [Left + Side, Top, -1, 1], [Left, Top + Side, 1, -1], [Left + Side, Top + Side, -1, -1]]) {
      Brush.beginPath();
      Brush.moveTo(X + Dx * Corner, Y);
      Brush.lineTo(X, Y);
      Brush.lineTo(X, Y + Dy * Corner);
      Brush.stroke();
    }
    // Fill guide: the circle the bounding sphere fills at the default zoom.
    Brush.strokeStyle = "#ffffff30";
    Brush.lineWidth = 1;
    Brush.setLineDash([3, 4]);
    Brush.beginPath();
    Brush.arc(Left + Side / 2, Top + Side / 2, (CaptureFill * Side) / 2, 0, Math.PI * 2);
    Brush.stroke();
    Brush.setLineDash([]);
    Brush.fillStyle = "#cfcfcf";
    Brush.font = "600 9.5px sans-serif";
    Brush.textAlign = "left";
    Brush.fillText(`ICON · ${Capture.Size} × ${Capture.Size} · transparent`, Left, Top - 8);
  };
  const Saved = Values[CaptureKey];
  return (
    <div className="ws-view ws-view-capture">
      <div className="ws-pane">
        <span className="ws-pane-label">Drag to turn · wheel to zoom · the box is the exact crop of the PNG</span>
        <Surface Paint={Paint} Label={`${Subject.Name} icon capture box`} Deps={[Capture.Yaw, Capture.Pitch, Capture.Zoom, Capture.Size, Capture.Look]} Handlers={{ ...Orbit, onPointerUp: (Event) => Orbit.End(Event) }} />
      </div>
      <div className="ws-pane ws-capture-shelf">
        <span className="ws-pane-label">{Workspace.CaptureDraft ? "Captured · not saved yet" : Saved ? "Saved thumbnail" : "Nothing captured yet"}</span>
        <div className="ws-capture-plate">
          {Workspace.CaptureDraft || Saved?.Url ? <img src={Workspace.CaptureDraft?.Url || Saved.Url} alt={`${Subject.Name} icon`} /> : <small>Set the angle, then Capture.</small>}
        </div>
      </div>
    </div>
  );
}

export function CapturePanel({ Subject, Values, Change, Workspace, SetWorkspace, Kit }) {
  const { Card, Control, Metric } = Kit;
  const Body = useCaptureBody(Subject, Values);
  const Capture = CaptureOf(Workspace);
  const Draft = Workspace.CaptureDraft;
  const Saved = Values[CaptureKey];
  const [Notice, SetNotice] = useState("");
  const Look = (Patch) => SetWorkspace((Previous) => ({ ...Previous, ...Patch, CaptureDraft: null }));
  const Name = String(Subject.Name || "object").replace(/[^a-z0-9]+/gi, "_");
  const Shoot = () => {
    const Result = CaptureIcon(Body, Capture);
    if (!Result.Coverage) return SetNotice("Nothing inside the box: zoom in.");
    SetWorkspace((Previous) => ({ ...Previous, CaptureDraft: { Url: Result.Url, Thumbnail: Thumbnail(Result.Canvas), Coverage: Result.Coverage, Size: Capture.Size, Look: Capture.Look } }));
    SetNotice(`Captured ${Capture.Size} × ${Capture.Size}.`);
  };
  const Save = () => {
    Change(CaptureKey, { Url: Draft.Thumbnail, Side: ThumbnailSide, Size: Draft.Size, Look: Looks[Draft.Look], Yaw: +Capture.Yaw.toFixed(3), Pitch: +Capture.Pitch.toFixed(3), Coverage: +Draft.Coverage.toFixed(4), At: Date.now() });
    SetNotice("Saved as the object's thumbnail.");
  };
  const DownloadPng = () => {
    const Link = document.createElement("a");
    Link.href = Draft.Url;
    Link.download = `${Name}_icon_${Draft.Size}.png`;
    Link.click();
  };
  const Copy = async () => {
    try {
      const Blob_ = await (await fetch(Draft.Url)).blob();
      await navigator.clipboard.write([new ClipboardItem({ "image/png": Blob_ })]);
      SetNotice("Copied the PNG.");
    } catch {
      SetNotice("Clipboard blocked: use Download PNG.");
    }
  };
  const Degrees = (Radians) => `${Math.round((Radians * 180) / Math.PI)}°`;
  return (
    <>
      <Card Title="Capture box" Class="ws-card">
        <Metric Value={`${Capture.Size}`} Unit="px square · transparent" Caption={`${Degrees(Capture.Yaw)} yaw · ${Degrees(Capture.Pitch)} pitch · ${Capture.Zoom.toFixed(2)}× framing`} />
        <Control Field={{ Label: "Output size", Control: "Select", Options: Sizes.map((Each) => `${Each} × ${Each}`) }} Value={Sizes.indexOf(Capture.Size)} Change={(Index) => Look({ CaptureSize: Sizes[Index] })} />
        <Control Field={{ Label: "Look", Control: "Select", Options: Looks }} Value={Capture.Look} Change={(Index) => Look({ CaptureLook: Index })} />
        <Control Field={{ Label: "Framing", Minimum: 0.4, Maximum: 2.5, Decimals: 2, Unit: "×", Default: 1 }} Value={Capture.Zoom} Change={(Next) => Look({ Zoom: Next })} />
        <div className="ws-actions">
          <button type="button" onClick={() => Look({ Yaw: CaptureAngle.Yaw, Pitch: CaptureAngle.Pitch, Zoom: 1 })}>
            Default angle
          </button>
          <button type="button" className="ws-primary" data-capture="shoot" onClick={Shoot}>
            Capture
          </button>
        </div>
        <p className="ws-note">Default angle is the three-quarter view; at 1× the bounding sphere fills {Math.round(CaptureFill * 100)} % of the box, so every object gets the same margin.</p>
      </Card>
      <Card Title="Icon" Class="ws-card">
        <Metric
          Value={Draft ? `${Math.round(Draft.Coverage * 100)}` : Saved ? `${Math.round(Saved.Coverage * 100)}` : "–"}
          Unit="% of the box covered"
          Caption={Draft ? `${Draft.Size} px · ${Looks[Draft.Look]} · not saved` : Saved ? `Saved ${new Date(Saved.At).toLocaleTimeString()} · ${Saved.Size} px ${Saved.Look} · thumbnail ${Saved.Side} px` : "Capture to see it here."}
        />
        <div className="ws-actions">
          <button type="button" disabled={!Draft} onClick={Save}>
            {Saved ? "Replace thumbnail" : "Save as thumbnail"}
          </button>
          <button type="button" disabled={!Saved} onClick={() => Change(CaptureKey, null)}>
            Remove
          </button>
        </div>
        <div className="ws-actions">
          <button type="button" disabled={!Draft} onClick={DownloadPng}>
            Download PNG
          </button>
          <button type="button" disabled={!Draft} onClick={Copy}>
            Copy PNG
          </button>
        </div>
        {Notice && <p className="ws-note ws-notice" role="status">{Notice}</p>}
        <p className="ws-note">The object keeps a {ThumbnailSide} px copy; Download and Copy use the full size.</p>
      </Card>
    </>
  );
}
