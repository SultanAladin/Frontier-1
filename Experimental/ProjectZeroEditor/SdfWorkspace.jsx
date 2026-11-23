//============================================================================================================================================
//                                                            SDFWORKSPACE.JSX
//============================================================================================================================================
// 🧰 SDF Bake workspace. Bakes the selection's surface into the Geometry Compiler's sparse 8 × 8 × 8 narrow-band distance field
// (SparseSdfBaker.js: Int16 bricks near the surface, a conservative Mip 1 everywhere) and reads it back slice by slice. Settings and the
// bake stamp live in Values["SDF Bake"]; the volume itself is rebuilt in the browser from them. No GPU upload happens here.

import React, { useEffect, useMemo, useState } from "react";
import { compileSparseSdfVolume, sampleSparseSdf, extractSdfSliceRgba, serializeVolumeFvol } from "../GeometryCompiler/Engine/SparseSdfBaker.js";
import { shortHash as ShortHash, formatLength as FormatLength, formatBytes as FormatBytes } from "../FractureEditor/FractureModel.js";
import { PrimitiveSurface, OrbitView, PaintPolygons, TexturedTriangle } from "./WorkspaceGeometry.js";
import { ShapeOf } from "./PrimitiveInspector.jsx";
import { Surface, useOrbit, Download, Clamp, GeometryKeys } from "./WorkspaceSurface.jsx";

export const SdfKey = "SDF Bake";
export const SdfResolutions = [32, 64, 128];
const Axes = [
  { Label: "Z · XY plane", Plane: ["X", "Y"], Normal: "Z" },
  { Label: "Y · XZ plane", Plane: ["X", "Z"], Normal: "Y" },
  { Label: "X · ZY plane", Plane: ["Z", "Y"], Normal: "X" },
];

export function SdfSettings(Stored) {
  const Raw = Stored && typeof Stored === "object" ? Stored : {};
  return {
    Resolution: SdfResolutions.includes(Number(Raw.Resolution)) ? Number(Raw.Resolution) : 64,
    Padding: Clamp(Number.isFinite(+Raw.Padding) ? +Raw.Padding : 0.16, 0.04, 0.5),
    Band: Clamp(Number.isFinite(+Raw.Band) ? +Raw.Band : 3.5, 2.5, 8),
    Baked: Raw.Baked && typeof Raw.Baked.Signature === "string" ? Raw.Baked : null,
  };
}

// Baked volumes shared by the viewport and the inspector, keyed by everything the bake reads. Baking runs on a timer so the
// "baking" state paints first; every mounted hook refreshes when a volume lands.
const Volumes = new Map();
const Pending = new Set();
const Refreshes = new Set();
function Request(Key, Build) {
  if (Volumes.has(Key) || Pending.has(Key)) return;
  Pending.add(Key);
  setTimeout(() => {
    try {
      Volumes.set(Key, Build());
    } catch (Problem) {
      Volumes.set(Key, { Problem: String(Problem?.message || Problem) });
    }
    Pending.delete(Key);
    while (Volumes.size > 8) Volumes.delete(Volumes.keys().next().value);
    Refreshes.forEach((Refresh) => Refresh());
  }, 20);
}

export function useSdfVolume(Subject, Values) {
  const [, Refresh] = useState(0);
  useEffect(() => {
    const Wake = () => Refresh((Count) => Count + 1);
    Refreshes.add(Wake);
    return () => Refreshes.delete(Wake);
  }, []);
  const Shape = ShapeOf(Subject);
  const Scale = Array.isArray(Values.Scale) ? Values.Scale.map((Each) => Math.abs(+Each) || 1) : [1, 1, 1];
  const Settings = SdfSettings(Values[SdfKey]);
  const Closed = !((Shape === "cylinder" || Shape === "cone") && Values["Geometry Caps"] === false);
  const BodyKey = JSON.stringify([Shape, Scale, GeometryKeys.map((Name) => Values[Name])]);
  const Key = JSON.stringify([BodyKey, Settings.Resolution, Settings.Padding, Settings.Band, Closed]);
  const Body = useMemo(() => PrimitiveSurface(Shape, (Name, Initial) => (Values[Name] === undefined ? Initial : Values[Name]), Scale), [BodyKey]);
  useEffect(
    () => Request(Key, () => compileSparseSdfVolume({ positions: Body.Positions, indices: Body.Indices, closed: Closed }, { gridDim: Settings.Resolution, paddingFraction: Settings.Padding, narrowBandMult: Settings.Band })),
    [Key],
  );
  const Volume = Volumes.get(Key);
  const Signature = ShortHash(Key);
  const Standing = !Settings.Baked ? "unbaked" : Settings.Baked.Signature === Signature ? "ready" : "stale";
  return { Shape, Body, Settings, Key, Signature, Standing, Closed, Volume: Volume && !Volume.Problem ? Volume : null, Problem: Volume?.Problem };
}

// World point on the slice for slice coordinates (U right, V up), matching extractSdfSliceRgba's axis layout.
function SlicePoint(Volume, Axis, Depth, U, V) {
  const Min = Volume.boundsMin, Span = Volume.boundsMax[0] - Volume.boundsMin[0];
  const D = Clamp(Depth, 0.02, 0.98);
  if (Axis === 0) return [Min[0] + U * Span, Min[1] + V * Span, Min[2] + D * Span];
  if (Axis === 1) return [Min[0] + U * Span, Min[1] + D * Span, Min[2] + V * Span];
  return [Min[0] + D * Span, Min[1] + V * Span, Min[2] + U * Span];
}

export function useSliceImage(Sdf, Axis, Depth, Mip, Iso) {
  return useMemo(() => {
    if (!Sdf.Volume) return null;
    const Size = 192;
    const Pixels = extractSdfSliceRgba(Sdf.Volume, Axis, Depth, Size, Mip === 1, Iso);
    const Canvas = document.createElement("canvas");
    Canvas.width = Canvas.height = Size;
    Canvas.getContext("2d").putImageData(new ImageData(new Uint8ClampedArray(Pixels.buffer), Size, Size), 0, 0);
    return Canvas;
  }, [Sdf.Key, !!Sdf.Volume, Axis, Depth, Mip, Iso]);
}

const SliceOf = (Workspace) => ({ Axis: Workspace.SliceAxis ?? 0, Depth: Workspace.SliceDepth ?? 0.5, Mip: Workspace.Mip ?? 0, Iso: Workspace.Iso ?? 0 });

export function SdfView({ Subject, Values, Workspace, SetWorkspace }) {
  const Sdf = useSdfVolume(Subject, Values);
  const { Axis, Depth, Mip, Iso } = SliceOf(Workspace);
  const Image = useSliceImage(Sdf, Axis, Depth, Mip, Iso);
  const Orbit = useOrbit(Workspace, SetWorkspace);
  const Radius = useMemo(() => {
    let Far = 0;
    const P = Sdf.Body.Positions;
    for (let Index = 0; Index < P.length; Index += 3) Far = Math.max(Far, Math.hypot(P[Index], P[Index + 1], P[Index + 2]));
    return (Far || 1) * 1.25;
  }, [Sdf.Body]);
  const Probe = Workspace.Probe && Sdf.Volume ? Workspace.Probe : null;

  // Object with the slice plane cut through it, textured with the current slice.
  const PaintObject = (Brush, Width, Height) => {
    const View = OrbitView({ Yaw: Workspace.Yaw ?? 0.7, Pitch: Workspace.Pitch ?? 0.45, Radius, Width, Height, Zoom: Workspace.Zoom ?? 1 });
    const P = Sdf.Body.Positions, I = Sdf.Body.Indices, Polygons = [];
    for (let Triangle = 0; Triangle < I.length / 3; Triangle++)
      Polygons.push({
        Points: [0, 1, 2].map((Corner) => {
          const V = I[Triangle * 3 + Corner] * 3;
          return [P[V], P[V + 1], P[V + 2]];
        }),
        Paint: (B, Screen, Lit) => {
          B.beginPath();
          Screen.forEach(([X, Y], Index) => (Index ? B.lineTo(X, Y) : B.moveTo(X, Y)));
          B.closePath();
          const Light = Math.round(22 + 34 * Lit);
          B.fillStyle = `hsl(210 6% ${Light}%)`;
          B.strokeStyle = `hsl(210 6% ${Light}%)`;
          B.lineWidth = 0.6;
          B.fill();
          B.stroke();
        },
      });
    PaintPolygons(Brush, View, Polygons);
    if (!Sdf.Volume || !Image) return;
    const Corner = (U, V) => View.Project(SlicePoint(Sdf.Volume, Axis, Depth, U, V));
    const [A, B, C, D] = [Corner(0, 0), Corner(1, 0), Corner(1, 1), Corner(0, 1)];
    Brush.globalAlpha = 0.72;
    TexturedTriangle(Brush, Image, [A, B, C], [0, 0, 1, 0, 1, 1]);
    TexturedTriangle(Brush, Image, [A, C, D], [0, 0, 1, 1, 0, 1]);
    Brush.globalAlpha = 1;
    Brush.strokeStyle = "#ffb15a";
    Brush.lineWidth = 1.2;
    Brush.beginPath();
    [A, B, C, D].forEach(([X, Y], Index) => (Index ? Brush.lineTo(X, Y) : Brush.moveTo(X, Y)));
    Brush.closePath();
    Brush.stroke();
    if (Probe) {
      const [X, Y] = View.Project(Probe);
      Brush.fillStyle = "#ffffff";
      Brush.beginPath();
      Brush.arc(X, Y, 3.5, 0, Math.PI * 2);
      Brush.fill();
    }
  };

  const Square = (Width, Height) => {
    const Side = Math.min(Width, Height) - 56;
    return { Left: (Width - Side) / 2, Top: (Height - Side) / 2 + 6, Side };
  };
  const PaintSlice = (Brush, Width, Height) => {
    const { Left, Top, Side } = Square(Width, Height);
    if (!Image) {
      Brush.fillStyle = "#8a8a8a";
      Brush.font = "11px 'DM Sans', sans-serif";
      Brush.textAlign = "center";
      Brush.fillText(Sdf.Problem ? "Bake failed · " + Sdf.Problem : "Baking distance field…", Width / 2, Height / 2);
      return;
    }
    Brush.imageSmoothingEnabled = true;
    Brush.drawImage(Image, Left, Top, Side, Side);
    Brush.strokeStyle = "#ffffff30";
    Brush.strokeRect(Left + 0.5, Top + 0.5, Side, Side);
    // Brick lattice: 8-voxel bricks.
    const Bricks = Sdf.Volume.bricksPerAxis;
    Brush.strokeStyle = "#ffffff0d";
    Brush.beginPath();
    for (let Index = 1; Index < Bricks; Index++) {
      const T = Left + (Index / Bricks) * Side;
      Brush.moveTo(T, Top);
      Brush.lineTo(T, Top + Side);
      Brush.moveTo(Left, Top + (Index / Bricks) * Side);
      Brush.lineTo(Left + Side, Top + (Index / Bricks) * Side);
    }
    Brush.stroke();
    Brush.fillStyle = "#8a8a8a";
    Brush.font = "10px 'DM Sans', sans-serif";
    Brush.textAlign = "center";
    Brush.fillText(Axes[Axis].Plane[0] + " →", Left + Side / 2, Top + Side + 16);
    Brush.save();
    Brush.translate(Left - 10, Top + Side / 2);
    Brush.rotate(-Math.PI / 2);
    Brush.fillText(Axes[Axis].Plane[1] + " →", 0, 0);
    Brush.restore();
    if (Workspace.ProbeUv && Probe) {
      const [U, V] = Workspace.ProbeUv;
      const X = Left + U * Side, Y = Top + (1 - V) * Side;
      Brush.strokeStyle = "#ffffff";
      Brush.lineWidth = 1;
      Brush.beginPath();
      Brush.moveTo(X - 7, Y);
      Brush.lineTo(X + 7, Y);
      Brush.moveTo(X, Y - 7);
      Brush.lineTo(X, Y + 7);
      Brush.stroke();
    }
  };
  const SliceHandlers = {
    onPointerDown: (Event) => {
      if (!Sdf.Volume) return;
      const Rect = Event.currentTarget.getBoundingClientRect();
      const { Left, Top, Side } = Square(Rect.width, Rect.height);
      const U = (Event.clientX - Rect.left - Left) / Side, V = 1 - (Event.clientY - Rect.top - Top) / Side;
      if (U < 0 || U > 1 || V < 0 || V > 1) return;
      SetWorkspace((Previous) => ({ ...Previous, ProbeUv: [U, V], Probe: SlicePoint(Sdf.Volume, Axis, Depth, U, V) }));
    },
    onWheel: (Event) => SetWorkspace((Previous) => ({ ...Previous, SliceDepth: Clamp((Previous.SliceDepth ?? 0.5) + (Event.deltaY > 0 ? -0.02 : 0.02), 0.02, 0.98), Probe: null })),
  };
  return (
    <div className="ws-view ws-view-sdf">
      <div className="ws-pane">
        <span className="ws-pane-label">Object · slice plane through the baked field</span>
        <Surface Paint={PaintObject} Label={`${Subject.Name} with distance field slice`} Handlers={{ ...Orbit, onPointerUp: (Event) => Orbit.End(Event) }} />
      </div>
      <div className="ws-pane">
        <span className="ws-pane-label">
          {Axes[Axis].Normal} slice at {(Depth * 100).toFixed(0)} % · {Sdf.Settings.Resolution}³ · click to probe, wheel to move
        </span>
        <Surface Paint={PaintSlice} Label="Distance field slice" Class="ws-slice" Handlers={SliceHandlers} />
      </div>
    </div>
  );
}

export function SdfPanel({ Subject, Values, Change, Workspace, SetWorkspace, Kit }) {
  const { Card, Control, Metric } = Kit;
  const Sdf = useSdfVolume(Subject, Values);
  const { Settings, Volume } = Sdf;
  const { Axis, Depth, Mip, Iso } = SliceOf(Workspace);
  const Assign = (Patch) => Change(SdfKey, { ...Settings, ...Patch });
  const Look = (Patch) => SetWorkspace((Previous) => ({ ...Previous, ...Patch }));
  const Readout = (Label, Text) => <Control key={Label} Field={{ Label, Control: "Readout" }} Value={Text} />;
  const Distance = Volume && Workspace.Probe ? sampleSparseSdf(Volume, ...Workspace.Probe, Mip === 1) - Iso : null;
  const Name = String(Subject.Name || "geometry").replace(/[^a-z0-9]+/gi, "_");
  return (
    <>
      <Card Title="Distance field" Class="ws-card">
        <Metric
          Value={Volume ? String(Volume.allocatedBrickCount) : "…"}
          Unit={Volume ? `of ${Volume.totalBricks} bricks` : "baking"}
          Caption={Volume ? `${Volume.dim}³ voxels · ${FormatLength(Volume.voxelStep)} voxel · ${Volume.prunedPercent}% of bricks pruned` : Sdf.Problem || "Building the sparse volume from the surface."}
        />
        <Control Field={{ Label: "Resolution", Control: "Select", Options: SdfResolutions.map((Each) => `${Each}³ voxels`) }} Value={SdfResolutions.indexOf(Settings.Resolution)} Change={(Index) => Assign({ Resolution: SdfResolutions[Index] })} />
        <Control Field={{ Label: "Bounds padding", Minimum: 0.04, Maximum: 0.5, Decimals: 2, Unit: "×", Default: 0.16 }} Value={Settings.Padding} Change={(Next) => Assign({ Padding: Next })} />
        <Control Field={{ Label: "Narrow band", Minimum: 2.5, Maximum: 8, Decimals: 1, Unit: "voxels", Default: 3.5 }} Value={Settings.Band} Change={(Next) => Assign({ Band: Next })} />
        <div className="ws-readouts">
          {Readout("Band width", Volume ? FormatLength(Volume.narrowBandMeters) : "–")}
          {Readout("Mip 0 bricks", Volume ? FormatBytes(Volume.mip0Bytes) : "–")}
          {Readout("Mip 1 field", Volume ? `${Volume.mip1Dim}³ · ${FormatBytes(Volume.mip1Bytes)}` : "–")}
          {Readout("Dense equivalent", Volume ? FormatBytes(Volume.denseBytes) : "–")}
          {Readout("Mip 1 ≤ Mip 0", Volume ? (Volume.monotonicityVerified ? "verified" : "violated") : "–")}
          {Readout("Source", `${(Sdf.Body.Indices.length / 3).toLocaleString("en-US")} triangles · ${Sdf.Closed ? "closed" : "open shell"}`)}
          {Readout("Bake time", Volume ? `${Volume.bakeMs.toFixed(0)} ms` : "–")}
        </div>
      </Card>
      <Card Title="Slice" Class="ws-card">
        <Metric Value={Distance === null ? "–" : (Distance * 100).toFixed(1)} Unit={Distance === null ? "" : "cm to surface"} Caption={Distance === null ? "Click the slice to probe a distance." : (Distance < 0 ? "Probe is inside the surface." : "Probe is outside the surface.") + (Math.abs(Distance) > (Volume?.narrowBandMeters || 0) ? " Beyond the narrow band the value is the brick's coarse estimate." : "")} />
        <Control Field={{ Label: "Slice axis", Control: "Select", Options: Axes.map((Each) => Each.Label) }} Value={Axis} Change={(Index) => Look({ SliceAxis: Index, Probe: null })} />
        <Control Field={{ Label: "Slice depth", Minimum: 0.02, Maximum: 0.98, Decimals: 2, Unit: "", Default: 0.5 }} Value={Depth} Change={(Next) => Look({ SliceDepth: Next, Probe: null })} />
        <Control
          Field={{ Label: "Iso offset", Minimum: -(Volume?.narrowBandMeters || 0.1), Maximum: Volume?.narrowBandMeters || 0.1, Decimals: 3, Unit: "m", Default: 0 }}
          Value={Iso}
          Change={(Next) => Look({ Iso: Next })}
        />
        <Control Field={{ Label: "Level", Control: "Select", Options: ["Mip 0 · sparse bricks", "Mip 1 · conservative"] }} Value={Mip} Change={(Index) => Look({ Mip: Index })} />
        <p className="ws-note">Blue is inside, dark is outside and the amber line is the zero surface. Iso offset shows the surface grown or shrunk by that distance.</p>
      </Card>
      <Card Title="Bake" Class="ws-card">
        <div className="ws-bake-status" data-status={Sdf.Standing}>
          <i />
          {Sdf.Standing === "ready" ? `Baked ${new Date(Settings.Baked.At).toLocaleTimeString()} · up to date` : Sdf.Standing === "stale" ? "Geometry or settings changed since the last bake." : "Not baked yet."}
        </div>
        <div className="ws-actions">
          <button
            type="button"
            disabled={!Volume}
            onClick={() => Assign({ Baked: { Signature: Sdf.Signature, At: Date.now(), Resolution: Volume.dim, Bricks: Volume.allocatedBrickCount, Bytes: Volume.mip0Bytes + Volume.mip1Bytes } })}
          >
            {Sdf.Standing === "ready" ? "Bake again" : "Bake distance field"}
          </button>
          <button type="button" disabled={!Volume} onClick={() => Download(Name + ".fvol.json", serializeVolumeFvol(Volume, Name), "application/json")}>
            Export .fvol
          </button>
        </div>
        <p className="ws-note">The object stores these settings and the bake stamp. The volume is rebuilt in the browser from them; native GPU upload is pending.</p>
      </Card>
    </>
  );
}
