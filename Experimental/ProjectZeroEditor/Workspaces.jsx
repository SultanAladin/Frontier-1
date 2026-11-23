//============================================================================================================================================
//                                                              WORKSPACES.JSX
//============================================================================================================================================
// 🧰 Edit-mode workspaces. Instead of a dedicated editor window per tool, the selection is isolated in the viewport and the tool works on
// it there, with its controls in the inspector. UV (unwrap, islands, checker), Fracture (recipe, impact, pieces), SDF Bake (sparse
// distance field, slices), LOD Clusters (cluster DAG, error cut, back-face cone culling), Collision Fit (fitted proxies, mesh check) and
// Icon Capture (square box, transparent PNG thumbnail) register in `WorkspaceTools`. Browser authoring only; the native renderer is not connected.

import React, { useEffect, useMemo, useRef, useState } from "react";
import { unwrapCharts, packCharts, cornerUvs, layoutStatistics } from "../GeometryCompiler/Engine/UvCharts.js";
import { formatObj, formatUvSvg } from "../GeometryCompiler/Engine/ObjExport.js";
import {
  normalize as NormalizeRecipe,
  shatter as Shatter,
  intact as Intact,
  pieceStats as PieceStats,
  histogram as Histogram,
  signature as RecipeSignature,
  bakeState as BakeState,
  STATE_TEXT,
  MATERIALS,
  MATERIAL,
  ENERGY_PRESETS,
  LIMITS,
  MAX_POINTS,
  PIECE_COLLISIONS,
  formatEnergy as FormatEnergy,
  formatLength as FormatLength,
} from "../FractureEditor/FractureModel.js";
import { PrimitiveSurface, OrbitView, PaintPolygons, TexturedTriangle, CheckerTexture, Normal } from "./WorkspaceGeometry.js";
import { ShapeOf } from "./PrimitiveInspector.jsx";
import { Surface, useOrbit, Download, Hue, Clamp, GeometryKeys } from "./WorkspaceSurface.jsx";
import { SdfView, SdfPanel } from "./SdfWorkspace.jsx";
import { ClusterView, ClusterPanel } from "./ClusterWorkspace.jsx";
import { CollisionView, CollisionPanel, Outline as ColliderOutline } from "./CollisionWorkspace.jsx";
import { pieceColliders } from "./PieceCollision.js";
import { CaptureView, CapturePanel } from "./CaptureWorkspace.jsx";
import "./Workspaces.css";

export const WorkspaceTools = [
  { Tool: "uv", Name: "UV", Caption: "Unwrap islands, move and pack them; the object shows the checker." },
  { Tool: "fracture", Name: "Fracture", Caption: "Shatter with a recipe; click the object to place the impact." },
  { Tool: "sdf", Name: "SDF Bake", Caption: "Bake a sparse signed distance field from the surface and read it slice by slice." },
  { Tool: "lod", Name: "LOD Clusters", Caption: "Split the surface into clusters, simplify them into a level DAG and pick a cut by screen error." },
  { Tool: "collision", Name: "Collision Fit", Caption: "Fit box, sphere and capsule proxies, compare how tightly each wraps the object and check the surface is closed." },
  { Tool: "capture", Name: "Icon Capture", Caption: "Frame the object in a square box and capture a transparent PNG thumbnail." },
];
// One view and one panel per tool; Fracture is the fallback.
const ToolViews = { uv: UvView, sdf: SdfView, lod: ClusterView, collision: CollisionView, capture: CaptureView };
const ToolPanels = { uv: UvPanel, sdf: SdfPanel, lod: ClusterPanel, collision: CollisionPanel, capture: CapturePanel };
//------------------------------------------------------------------------------------------------------------------------
//                                                   UV
//------------------------------------------------------------------------------------------------------------------------


// The unwrap for the selection, with any island edits stored in Values["UV Layout"] re-applied when the mesh has not changed since.
export function useUvLayout(Subject, Values) {
  const Shape = ShapeOf(Subject);
  const Scale = Array.isArray(Values.Scale) ? Values.Scale.map((Each) => Math.abs(+Each) || 1) : [1, 1, 1];
  const Stored = Values["UV Layout"] || {};
  const Angle = Clamp(+Stored.Angle || 60, 15, 89);
  const Key = JSON.stringify([Shape, Scale, Angle, GeometryKeys.map((Name) => Values[Name])]);
  const Base = useMemo(() => {
    const Read = (Name, Initial) => (Values[Name] === undefined ? Initial : Values[Name]);
    const Mesh = PrimitiveSurface(Shape, Read, Scale);
    const Layout = packCharts(unwrapCharts(Mesh.Positions, Mesh.Indices, { normalLimitDeg: Angle }));
    return { Mesh, Layout, Packed: Layout.charts.map((Chart) => ({ R: Chart.rotation, M: !!Chart.mirror, C: Chart.center.slice() })) };
  }, [Key]);
  const Edits = Stored.Key === Key && Array.isArray(Stored.Charts) && Stored.Charts.length === Base.Layout.charts.length ? Stored.Charts : Base.Packed;
  // Apply edits on a shallow copy so the memoised base stays the packed layout.
  const Layout = { ...Base.Layout, charts: Base.Layout.charts.map((Chart, Index) => ({ ...Chart, rotation: Edits[Index].R & 3, mirror: !!Edits[Index].M, center: Edits[Index].C })) };
  const Uvs = cornerUvs(Layout);
  return { Shape, Mesh: Base.Mesh, Layout, Uvs, Key, Edits, Packed: Base.Packed, Angle, Statistics: layoutStatistics(Layout, Uvs) };
}

function IslandOf(Layout, Uvs, U, V) {
  for (let Index = Layout.charts.length - 1; Index >= 0; Index--)
    for (const Triangle of Layout.charts[Index].triangles) {
      const A = Triangle * 6;
      const [X0, Y0, X1, Y1, X2, Y2] = [Uvs[A], Uvs[A + 1], Uvs[A + 2], Uvs[A + 3], Uvs[A + 4], Uvs[A + 5]];
      const D = (Y1 - Y2) * (X0 - X2) + (X2 - X1) * (Y0 - Y2);
      if (Math.abs(D) < 1e-12) continue;
      const L0 = ((Y1 - Y2) * (U - X2) + (X2 - X1) * (V - Y2)) / D, L1 = ((Y2 - Y0) * (U - X2) + (X0 - X2) * (V - Y2)) / D;
      if (L0 >= 0 && L1 >= 0 && L0 + L1 <= 1) return Index;
    }
  return -1;
}

function UvView({ Subject, Values, Change, Workspace, SetWorkspace }) {
  const Unwrap = useUvLayout(Subject, Values);
  const { Mesh, Layout, Uvs } = Unwrap;
  const Selection = new Set(Workspace.Selection || []);
  const Orbit = useOrbit(Workspace, SetWorkspace);
  const Checker = CheckerTexture(Workspace.Cells || 8);
  const Radius = useMemo(() => {
    let Far = 0;
    for (let Index = 0; Index < Mesh.Positions.length; Index += 3) Far = Math.max(Far, Math.hypot(Mesh.Positions[Index], Mesh.Positions[Index + 1], Mesh.Positions[Index + 2]));
    return Far || 1;
  }, [Mesh]);
  const ChartOf = useMemo(() => {
    const Map_ = new Int32Array(Layout.triangleCount);
    Layout.charts.forEach((Chart, Index) => Chart.triangles.forEach((Triangle) => (Map_[Triangle] = Index)));
    return Map_;
  }, [Unwrap.Key]);

  // 3D: every triangle textured with the checker through its current UVs, shaded, selected islands tinted.
  const PaintObject = (Brush, Width, Height) => {
    const View = OrbitView({ Yaw: Workspace.Yaw ?? 0.7, Pitch: Workspace.Pitch ?? 0.45, Radius, Width, Height, Zoom: Workspace.Zoom ?? 1 });
    const P = Mesh.Positions, I = Mesh.Indices, Polygons = [];
    for (let Triangle = 0; Triangle < I.length / 3; Triangle++) {
      const Points = [0, 1, 2].map((Corner) => {
        const V = I[Triangle * 3 + Corner] * 3;
        return [P[V], P[V + 1], P[V + 2]];
      });
      const Island = ChartOf[Triangle], Uv = Uvs.subarray(Triangle * 6, Triangle * 6 + 6);
      Polygons.push({
        Points,
        Paint: (B, Screen, Lit) => {
          if (Workspace.Checker !== false) TexturedTriangle(B, Checker, Screen, Uv);
          else {
            B.beginPath();
            Screen.forEach(([X, Y], Index) => (Index ? B.lineTo(X, Y) : B.moveTo(X, Y)));
            B.closePath();
            B.fillStyle = `hsl(${Hue(Island)} 34% 52%)`;
            B.fill();
          }
          B.beginPath();
          Screen.forEach(([X, Y], Index) => (Index ? B.lineTo(X, Y) : B.moveTo(X, Y)));
          B.closePath();
          B.fillStyle = `rgba(0,0,0,${(1 - Lit) * 0.75})`;
          B.fill();
          if (Selection.has(Island)) {
            B.fillStyle = "rgba(255,190,110,0.28)";
            B.fill();
          }
        },
      });
    }
    PaintPolygons(Brush, View, Polygons);
    // Seams on the object: edges between islands.
    if (Workspace.Seams !== false) {
      Brush.strokeStyle = "#ffb15a";
      Brush.lineWidth = 1.4;
      Brush.beginPath();
      for (const Slot of Layout.seams) {
        const Triangle = (Slot / 3) | 0, Corner = Slot % 3;
        const A = I[Triangle * 3 + Corner] * 3, B_ = I[Triangle * 3 + ((Corner + 1) % 3)] * 3;
        const N = Normal(...[0, 1, 2].map((C) => { const V = I[Triangle * 3 + C] * 3; return [P[V], P[V + 1], P[V + 2]]; }));
        if (N[0] * View.Toward[0] + N[1] * View.Toward[1] + N[2] * View.Toward[2] <= 0) continue;
        const S0 = View.Project([P[A], P[A + 1], P[A + 2]]), S1 = View.Project([P[B_], P[B_ + 1], P[B_ + 2]]);
        Brush.moveTo(S0[0], S0[1]);
        Brush.lineTo(S1[0], S1[1]);
      }
      Brush.stroke();
    }
  };

  // UV square: checker, island fills, outlines, selection.
  const Square = (Width, Height) => {
    const Side = Math.min(Width, Height) - 28;
    return { Left: (Width - Side) / 2, Top: (Height - Side) / 2, Side };
  };
  const PaintUv = (Brush, Width, Height) => {
    const { Left, Top, Side } = Square(Width, Height);
    Brush.globalAlpha = Workspace.Checker !== false ? 0.35 : 0.08;
    Brush.drawImage(Checker, Left, Top, Side, Side);
    Brush.globalAlpha = 1;
    Brush.strokeStyle = "#ffffff30";
    Brush.strokeRect(Left + 0.5, Top + 0.5, Side, Side);
    const X = (U) => Left + U * Side, Y = (V) => Top + (1 - V) * Side;
    Layout.charts.forEach((Chart, Index) => {
      const Selected = Selection.has(Index);
      Brush.beginPath();
      for (const Triangle of Chart.triangles) {
        const A = Triangle * 6;
        Brush.moveTo(X(Uvs[A]), Y(Uvs[A + 1]));
        Brush.lineTo(X(Uvs[A + 2]), Y(Uvs[A + 3]));
        Brush.lineTo(X(Uvs[A + 4]), Y(Uvs[A + 5]));
        Brush.closePath();
      }
      Brush.fillStyle = Selected ? "rgba(255,177,90,0.42)" : `hsla(${Hue(Index)} 40% 55% / 0.32)`;
      Brush.fill();
      Brush.strokeStyle = Selected ? "#ffcf96" : "#ffffff26";
      Brush.lineWidth = 0.6;
      Brush.stroke();
    });
    if (Workspace.Seams !== false) {
      Brush.strokeStyle = "#ffb15a";
      Brush.lineWidth = 1.2;
      Brush.beginPath();
      for (const Slot of Layout.seams) {
        const Triangle = (Slot / 3) | 0, Corner = Slot % 3;
        const A = (Triangle * 3 + Corner) * 2, B = (Triangle * 3 + ((Corner + 1) % 3)) * 2;
        Brush.moveTo(X(Uvs[A]), Y(Uvs[A + 1]));
        Brush.lineTo(X(Uvs[B]), Y(Uvs[B + 1]));
      }
      Brush.stroke();
    }
    Brush.fillStyle = "#8a8a8a";
    Brush.font = "9px sans-serif";
    Brush.fillText("0,0", Left, Top + Side + 12);
    Brush.textAlign = "right";
    Brush.fillText("1,1", Left + Side, Top - 4);
  };
  const UvDrag = useRef(null);
  const UvAt = (Event) => {
    const Rect = Event.currentTarget.getBoundingClientRect();
    const { Left, Top, Side } = Square(Rect.width, Rect.height);
    return [(Event.clientX - Rect.left - Left) / Side, 1 - (Event.clientY - Rect.top - Top) / Side];
  };
  const WriteEdits = (Charts) => Change("UV Layout", { Key: Unwrap.Key, Angle: Unwrap.Angle, Charts });
  const UvHandlers = {
    onPointerDown: (Event) => {
      const [U, V] = UvAt(Event);
      const Island = IslandOf(Layout, Uvs, U, V);
      let Next = new Set(Selection);
      if (Island < 0) Next = Event.shiftKey ? Next : new Set();
      else if (Event.shiftKey || Event.ctrlKey || Event.metaKey) Next.has(Island) ? Next.delete(Island) : Next.add(Island);
      else if (!Next.has(Island)) Next = new Set([Island]);
      SetWorkspace((Previous) => ({ ...Previous, Selection: [...Next] }));
      if (Island >= 0 && Next.has(Island)) {
        UvDrag.current = { U, V, Start: Unwrap.Edits.map((Edit) => ({ ...Edit, C: Edit.C.slice() })), Moving: [...Next] };
        Event.currentTarget.setPointerCapture(Event.pointerId);
      }
    },
    onPointerMove: (Event) => {
      const Drag = UvDrag.current;
      if (!Drag) return;
      const [U, V] = UvAt(Event);
      const Du = U - Drag.U, Dv = V - Drag.V;
      WriteEdits(Drag.Start.map((Edit, Index) => (Drag.Moving.includes(Index) ? { ...Edit, C: [Edit.C[0] + Du, Edit.C[1] + Dv] } : Edit)));
    },
    onPointerUp: () => (UvDrag.current = null),
  };
  return (
    <div className="ws-view ws-view-uv">
      <div className="ws-pane">
        <span className="ws-pane-label">Object · checker through current UVs</span>
        <Surface Paint={PaintObject} Label={`${Subject.Name} with UV checker`} Handlers={{ ...Orbit, onPointerUp: (Event) => Orbit.End(Event) }} />
      </div>
      <div className="ws-pane">
        <span className="ws-pane-label">UV space · {Layout.charts.length} islands · drag to move</span>
        <Surface Paint={PaintUv} Label="UV layout" Class="ws-uv" Handlers={UvHandlers} />
      </div>
    </div>
  );
}

function UvPanel({ Subject, Values, Change, Workspace, SetWorkspace, Kit }) {
  const { Card, Control, Metric } = Kit;
  const Unwrap = useUvLayout(Subject, Values);
  const { Layout, Uvs, Statistics, Edits } = Unwrap;
  const Selection = (Workspace.Selection || []).filter((Index) => Index < Layout.charts.length);
  const Coverage = Statistics.uvArea;
  const WriteEdits = (Charts) => Change("UV Layout", { Key: Unwrap.Key, Angle: Unwrap.Angle, Charts });
  const Targets = Selection.length ? Selection : Layout.charts.map((_, Index) => Index);
  const Apply = (Edit) => WriteEdits(Edits.map((Each, Index) => (Targets.includes(Index) ? Edit(Each) : Each)));
  const Bounds = (Index) => {
    let MinU = Infinity, MinV = Infinity, MaxU = -Infinity, MaxV = -Infinity;
    for (const Triangle of Layout.charts[Index].triangles)
      for (let K = 0; K < 6; K += 2) {
        MinU = Math.min(MinU, Uvs[Triangle * 6 + K]);
        MaxU = Math.max(MaxU, Uvs[Triangle * 6 + K]);
        MinV = Math.min(MinV, Uvs[Triangle * 6 + K + 1]);
        MaxV = Math.max(MaxV, Uvs[Triangle * 6 + K + 1]);
      }
    return [MinU, MinV, MaxU, MaxV];
  };
  const Outside = Layout.charts.filter((_, Index) => {
    const [A, B, C, D] = Bounds(Index);
    return A < -1e-4 || B < -1e-4 || C > 1.0001 || D > 1.0001;
  }).length;
  const Name = String(Subject.Name || "geometry").replace(/[^a-z0-9]+/gi, "_");
  return (
    <>
      <Card Title="UV layout" Class="ws-card">
        <Metric Value={(Coverage * 100).toFixed(1)} Unit="% used" Caption={`${Layout.charts.length} islands · ${Layout.seams.length} seam edges · ${Outside ? Outside + " outside 0–1" : "all inside 0–1"}`} />
        <div className="ws-readouts">
          <Control Field={{ Label: "Texel scale", Control: "Readout" }} Value={(Statistics.texelScale).toFixed(3) + " UV/m"} />
          <Control Field={{ Label: "Selected", Control: "Readout" }} Value={Selection.length ? `${Selection.length} island${Selection.length > 1 ? "s" : ""}` : "none · actions apply to all"} />
        </div>
        <Control
          Field={{ Label: "Unwrap angle", Minimum: 15, Maximum: 89, Decimals: 0, Unit: "°", Default: 60 }}
          Value={Unwrap.Angle}
          Change={(Next) => Change("UV Layout", { Angle: Next })}
        />
        <p className="ws-note">Faces whose normals stay within this angle share an island. Lower splits more; higher makes fewer, more stretched islands.</p>
        <div className="ws-actions">
          <button type="button" onClick={() => Apply((Each) => ({ ...Each, R: (Each.R + 1) & 3 }))}>Rotate 90°</button>
          <button type="button" onClick={() => Apply((Each) => ({ ...Each, M: !Each.M }))}>Mirror U</button>
          <button type="button" onClick={() => {
            // Repack with the edited rotations: re-run the shelf packer on a copy.
            const Copy = { ...Layout, charts: Layout.charts.map((Chart, Index) => ({ ...Chart, rotation: Edits[Index].R & 3, mirror: Edits[Index].M })) };
            packCharts(Copy);
            WriteEdits(Copy.charts.map((Chart) => ({ R: Chart.rotation, M: !!Chart.mirror, C: Chart.center.slice() })));
          }}>Pack</button>
          <button type="button" onClick={() => Change("UV Layout", { Angle: Unwrap.Angle })}>Reset unwrap</button>
        </div>
      </Card>
      <Card Title="Display & export" Class="ws-card">
        <Control Field={{ Label: "Checker on object", Control: "Switch" }} Value={Workspace.Checker !== false} Change={(Next) => SetWorkspace((Previous) => ({ ...Previous, Checker: Next }))} />
        <Control Field={{ Label: "Show seams", Control: "Switch" }} Value={Workspace.Seams !== false} Change={(Next) => SetWorkspace((Previous) => ({ ...Previous, Seams: Next }))} />
        <Control
          Field={{ Label: "Checker cells", Control: "Select", Options: ["4 × 4", "8 × 8", "16 × 16"] }}
          Value={[4, 8, 16].indexOf(Workspace.Cells || 8)}
          Change={(Index) => SetWorkspace((Previous) => ({ ...Previous, Cells: [4, 8, 16][Index] }))}
        />
        <div className="ws-actions">
          <button type="button" onClick={() => {
            const Flat = new Float32Array(Layout.triangleCount * 9), Index = new Uint32Array(Layout.triangleCount * 3);
            for (let Slot = 0; Slot < Layout.triangleCount * 3; Slot++) {
              const V = Unwrap.Mesh.Indices[Slot] * 3;
              Flat.set(Unwrap.Mesh.Positions.subarray(V, V + 3), Slot * 3);
              Index[Slot] = Slot;
            }
            Download(Name + ".obj", formatObj(Flat, Index, Uvs, Name));
          }}>Export OBJ</button>
          <button type="button" onClick={() => Download(Name + "_uv.svg", formatUvSvg(Layout, Uvs, Name), "image/svg+xml")}>Export UV SVG</button>
        </div>
        <p className="ws-note">Projection unwrap with one shared texel density. Island edits are stored on this object and reset when its geometry changes.</p>
      </Card>
    </>
  );
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   FRACTURE
//------------------------------------------------------------------------------------------------------------------------

const OwnerOf = (Subject, Values) => ({
  Id: Subject.Id,
  Name: Subject.Name,
  Primitive: ShapeOf(Subject),
  Scale: [0, 1, 2].map((Index) => {
    const Amount = Number(Values.Scale?.[Index] ?? 1);
    return Number.isFinite(Amount) ? Math.min(1000, Math.max(0.001, Amount)) : 1;
  }),
});
// Fracture points as a list. One point is the recipe's own X / Y / Z / Energy; two or more are Settings.Points.
export const PointsOf = (Settings) => (Settings.Points?.length > 1 ? Settings.Points : [{ X: Settings.X, Y: Settings.Y, Z: Settings.Z, Energy: Settings.Energy }]);
export const PointsPatch = (List) => (List.length > 1 ? { Points: List, ...List[0], Energy: List.reduce((Sum, Each) => Sum + Each.Energy, 0) } : { Points: null, ...List[0] });
const PointTones = ["#ffb15a", "#7cc8ff", "#ff7aa8", "#9be38a", "#c9a2ff", "#ffe07a", "#6fe3d6", "#ff8f6b"];
export const PointTone = (Index) => PointTones[Index % PointTones.length];

export function useShatter(Subject, Values) {
  const Owner = OwnerOf(Subject, Values);
  const Settings = NormalizeRecipe(Values.Fracture);
  const Key = RecipeSignature(Owner, Settings) + Settings.Enabled;
  return { Owner, Settings, Result: useMemo(() => (Settings.Enabled ? Shatter(Owner, Settings) : Intact(Owner)), [Key]) };
}

// Exploded pieces with the impact rings; shared by the fracture workspace and the inspector's preview card.
// Points: [{ At, Energy }] markers (defaults to the result's impacts); Selected: highlighted point; Colliders: pieceColliders() output.
export function PaintFracture(Brush, Width, Height, { Owner, Settings, Result, Yaw = 0.7, Pitch = 0.45, Zoom = 1, Explode = 0.25, Points = null, Selected = -1, Colliders = null }) {
  const Tone = MATERIAL[Settings.Material]?.tone || "#a29d94";
  const Radius = Math.hypot(...Owner.Scale) / 2;
  const Offset = (Cell) => {
    const From = Result.impacts?.[Cell.nearest] || Result.impact;
    const D = [Cell.centroid[0] - From[0] * 0.3, Cell.centroid[1] - From[1] * 0.3, Cell.centroid[2] - From[2] * 0.3];
    const Length = Math.hypot(...D) || 1;
    return D.map((Each) => (Each / Length) * Explode * Radius * (0.4 + Length / Radius));
  };
  const View = OrbitView({ Yaw, Pitch, Radius: Radius * (1 + Explode * 0.9), Width, Height, Zoom });
  const Polygons = [];
  const Biggest = Math.max(...Result.cells.map((Cell) => Cell.size)) || 1;
  Result.cells.forEach((Cell, Index) => {
    const Shift = Settings.Enabled ? Offset(Cell) : [0, 0, 0];
    for (const Face of Cell.faces) {
      if (Face.pts.length < 3) continue;
      const Points = Face.pts.map((Point) => [Point[0] + Shift[0], Point[1] + Shift[1], Point[2] + Shift[2]]);
      Polygons.push({
        Points,
        Normal: Normal(Face.pts[0], Face.pts[1], Face.pts[2]),
        Cell: Index,
        Face,
        Shift,
        Paint: (B, Screen, Lit) => {
          B.beginPath();
          Screen.forEach(([X, Y], Slot) => (Slot ? B.lineTo(X, Y) : B.moveTo(X, Y)));
          B.closePath();
          const Inner = Face.kind === "inner";
          const Size = Cell.size / Biggest;
          const Light = Math.round((Inner ? 70 : 40 + Cell.shade * 14 + Size * 10) * Lit);
          B.fillStyle = Inner ? `hsl(28 30% ${Light}%)` : Tone;
          B.fill();
          if (!Inner) {
            B.fillStyle = `rgba(0,0,0,${(1 - Lit) * 0.7 + (1 - Size) * 0.12})`;
            B.fill();
          }
          B.strokeStyle = Inner ? "#00000055" : "#00000080";
          B.lineWidth = 0.7;
          B.stroke();
        },
      });
    }
  });
  const Drawn = PaintPolygons(Brush, View, Polygons);
  // Piece colliders, moved with their pieces.
  if (Settings.Enabled && Colliders) {
    Brush.strokeStyle = "#7cf0c8";
    Brush.lineWidth = 1;
    Brush.globalAlpha = 0.75;
    Result.cells.forEach((Cell, Index) => {
      const Fit = Colliders.Pieces[Index]?.Fit;
      if (!Fit) return;
      const Shift = Offset(Cell);
      for (const Line of ColliderOutline(Fit)) {
        Brush.beginPath();
        Line.map((Point) => View.Project([Point[0] + Shift[0], Point[1] + Shift[1], Point[2] + Shift[2]])).forEach(([X, Y], Slot) => (Slot ? Brush.lineTo(X, Y) : Brush.moveTo(X, Y)));
        Brush.stroke();
      }
    });
    Brush.globalAlpha = 1;
  }
  // Fracture point markers: rings grow with each point's energy; the selected point gets a solid disc and its number.
  const Markers = [];
  if (Settings.Enabled) {
    const List = Points || (Result.impacts || [Result.impact]).map((At, Index) => ({ At, Energy: PointsOf(Settings)[Index]?.Energy ?? Settings.Energy }));
    List.forEach(({ At, Energy }, Index) => {
      const [X, Y] = View.Project(At), Grow = 0.7 + 0.6 * Math.min(1, Math.sqrt(Energy / 20000)), Colour = PointTone(Index);
      Brush.strokeStyle = Colour;
      Brush.lineWidth = 1.5;
      for (const Ring of [5, 10, 16]) {
        Brush.globalAlpha = 1 - Ring / 20;
        Brush.beginPath();
        Brush.arc(X, Y, Ring * Grow, 0, Math.PI * 2);
        Brush.stroke();
      }
      Brush.globalAlpha = 1;
      if (List.length > 1 || Index === Selected) {
        Brush.fillStyle = Index === Selected ? Colour : "#111111dd";
        Brush.beginPath();
        Brush.arc(X, Y, 7, 0, Math.PI * 2);
        Brush.fill();
        Brush.fillStyle = Index === Selected ? "#111" : Colour;
        Brush.font = "600 9px Inter, system-ui, sans-serif";
        Brush.textAlign = "center";
        Brush.textBaseline = "middle";
        Brush.fillText(String(Index + 1), X, Y + 0.5);
      }
      Markers.push({ Index, X, Y });
    });
  }
  return { Drawn, View, Markers };
}

function FractureView({ Subject, Values, Change, Workspace, SetWorkspace }) {
  const { Owner, Settings, Result } = useShatter(Subject, Values);
  const Orbit = useOrbit(Workspace, SetWorkspace);
  const Last = useRef({ Drawn: [], View: null, Markers: [] });
  const Grab = useRef(null);
  const [Moving, SetMoving] = React.useState(null);
  const List = PointsOf(Settings);
  const Selected = Math.min(Workspace.FracturePoint ?? 0, List.length - 1);
  const Colliders = useMemo(() => (Settings.Enabled && Workspace.ShowPieceColliders ? pieceColliders(Result.cells, Settings.PieceCollision) : null), [Result, Settings.PieceCollision, Workspace.ShowPieceColliders]);
  const Shown = List.map((Each, Index) => ({ At: Moving && Moving.Index === Index ? Moving.At : Result.impacts?.[Index] || [Each.X, Each.Y, Each.Z], Energy: Each.Energy }));
  const Paint = (Brush, Width, Height) => {
    Last.current = PaintFracture(Brush, Width, Height, { Owner, Settings, Result, Yaw: Workspace.Yaw ?? 0.7, Pitch: Workspace.Pitch ?? 0.45, Zoom: Workspace.Zoom ?? 1, Explode: Workspace.Explode ?? 0.25, Points: Shown, Selected, Colliders });
    // Marker positions in CSS pixels, for keyboardless tools and checks.
    Brush.canvas.dataset.markers = JSON.stringify(Last.current.Markers.map(({ X, Y }) => [Math.round(X), Math.round(Y)]));
  };
  // Front-most face under the pointer, then the ray/plane hit in the object's unexploded space.
  const Hit = (Event) => {
    const Rect = Event.currentTarget.getBoundingClientRect(), Sx = Event.clientX - Rect.left, Sy = Event.clientY - Rect.top;
    const { Drawn, View } = Last.current;
    for (let Index = Drawn.length - 1; Index >= 0; Index--) {
      const { Screen, Polygon } = Drawn[Index];
      let Inside = false;
      for (let A = 0, B = Screen.length - 1; A < Screen.length; B = A++)
        if ((Screen[A][1] > Sy) !== (Screen[B][1] > Sy) && Sx < ((Screen[B][0] - Screen[A][0]) * (Sy - Screen[A][1])) / (Screen[B][1] - Screen[A][1]) + Screen[A][0]) Inside = !Inside;
      if (!Inside) continue;
      const Ray = View.Unproject(Sx, Sy), N = Polygon.Normal, P0 = Polygon.Points[0];
      const Denominator = N[0] * Ray.Direction[0] + N[1] * Ray.Direction[1] + N[2] * Ray.Direction[2];
      if (Math.abs(Denominator) < 1e-9) return null;
      const T = (N[0] * (P0[0] - Ray.Origin[0]) + N[1] * (P0[1] - Ray.Origin[1]) + N[2] * (P0[2] - Ray.Origin[2])) / Denominator;
      return [0, 1, 2].map((Axis) => +(Ray.Origin[Axis] + Ray.Direction[Axis] * T - Polygon.Shift[Axis]).toFixed(3));
    }
    return null;
  };
  const Store = (Next) => Change("Fracture", { ...Settings, ...PointsPatch(Next), EditedAt: Date.now() });
  const Place = (Index, At) => Store(List.map((Each, Slot) => (Slot === Index ? { ...Each, X: At[0], Y: At[1], Z: At[2] } : Each)));
  // Click on the object: adds a point (up to MAX_POINTS) or, in move mode, moves the selected one.
  const Pick = (Event) => {
    if (!Settings.Enabled) return;
    const At = Hit(Event);
    if (!At) return;
    if ((Workspace.FractureClick || "add") === "add" && List.length < MAX_POINTS) {
      Store([...List, { X: At[0], Y: At[1], Z: At[2], Energy: List[Selected]?.Energy ?? 2500 }]);
      SetWorkspace((Previous) => ({ ...Previous, FracturePoint: List.length }));
    } else Place(Selected, At);
  };
  const Near = (Event) => {
    const Rect = Event.currentTarget.getBoundingClientRect(), Sx = Event.clientX - Rect.left, Sy = Event.clientY - Rect.top;
    let Best = null;
    for (const Marker of Last.current.Markers) {
      const Distance = Math.hypot(Marker.X - Sx, Marker.Y - Sy);
      if (Distance < 12 && (!Best || Distance < Best.Distance)) Best = { ...Marker, Distance };
    }
    return Best;
  };
  const Handlers = {
    onWheel: Orbit.onWheel,
    onPointerDown: (Event) => {
      const Marker = Settings.Enabled ? Near(Event) : null;
      if (!Marker) return Orbit.onPointerDown(Event);
      Grab.current = { Index: Marker.Index, X: Event.clientX, Y: Event.clientY, Moved: false };
      Event.currentTarget.setPointerCapture(Event.pointerId);
      SetWorkspace((Previous) => ({ ...Previous, FracturePoint: Marker.Index }));
    },
    onPointerMove: (Event) => {
      const Held = Grab.current;
      if (!Held) return Orbit.onPointerMove(Event);
      if (Math.hypot(Event.clientX - Held.X, Event.clientY - Held.Y) > 3) Held.Moved = true;
      if (!Held.Moved) return;
      const At = Hit(Event);
      if (At) SetMoving({ Index: Held.Index, At });
    },
    onPointerUp: (Event) => {
      const Held = Grab.current;
      Grab.current = null;
      if (!Held) return Orbit.End(Event, Pick);
      if (Held.Moved && Moving) Place(Held.Index, Moving.At);
      SetMoving(null);
    },
  };
  const Mode = (Workspace.FractureClick || "add") === "add" && List.length < MAX_POINTS ? "click to add a point" : `click to move point ${Selected + 1}`;
  return (
    <div className="ws-view ws-view-fracture">
      <div className="ws-pane">
        <span className="ws-pane-label">
          {Settings.Enabled ? `${Result.cells.length} pieces · ${MATERIAL[Settings.Material].name} · ${List.length} point${List.length === 1 ? "" : "s"} · ${Mode} · drag a point to move it` : "Fracture is off · enable it in the inspector"}
        </span>
        <Surface Paint={Paint} Label={`${Subject.Name} fracture preview`} Deps={[Moving, Selected, Colliders]} Handlers={Handlers} />
      </div>
    </div>
  );
}

function FracturePanelBody({ Subject, Values, Change, Workspace, SetWorkspace, Kit }) {
  const { Card, Control, Metric } = Kit;
  const { Owner, Settings, Result } = useShatter(Subject, Values);
  const Assign = (Patch) => Change("Fracture", { ...Settings, ...Patch, EditedAt: Date.now() });
  const Stats = PieceStats(Result.cells), Bars = Histogram(Result.cells, 14), Peak = Math.max(1, ...Bars.bins);
  const State = BakeState(Owner, Settings);
  const Material = MATERIAL[Settings.Material];
  const List = PointsOf(Settings);
  const Selected = Math.min(Workspace.FracturePoint ?? 0, List.length - 1);
  const Choose = (Index) => SetWorkspace((Previous) => ({ ...Previous, FracturePoint: Index }));
  const Store = (Next) => Assign(PointsPatch(Next));
  const Edit = (Index, Patch) => Store(List.map((Each, Slot) => (Slot === Index ? { ...Each, ...Patch } : Each)));
  const Total = List.reduce((Sum, Each) => Sum + Each.Energy, 0);
  const Colliders = useMemo(() => (Settings.Enabled ? pieceColliders(Result.cells, Settings.PieceCollision) : null), [Result, Settings.PieceCollision, Settings.Enabled]);
  const ClickMode = Workspace.FractureClick || "add";
  return (
    <>
      <Card Title="Shatter" Class="ws-card">
        <Metric Value={Settings.Enabled ? String(Stats.count) : "Off"} Unit={Settings.Enabled ? "pieces" : ""} Caption={Settings.Enabled ? `${Material.note} Limited by ${Result.budget.limiter}.` : "Turn fracture on to shatter this object."} />
        {Settings.Enabled && (
          <figure className="ws-histogram" aria-label="Piece size distribution">
            <div className="ws-bars">
              {Bars.bins.map((Count, Index) => (
                <i key={Index} style={{ height: `${(Count / Peak) * 100}%` }} title={`${Count} pieces`} />
              ))}
            </div>
            <figcaption>
              <span>{FormatLength(Bars.lo)}</span>
              <span>piece size · median {FormatLength(Stats.median)}</span>
              <span>{FormatLength(Bars.hi)}</span>
            </figcaption>
          </figure>
        )}
        <Control Field={{ Label: "Enable fracture", Control: "Switch" }} Value={Settings.Enabled} Change={(Next) => Assign({ Enabled: Next })} />
        <Control Field={{ Label: "Explode view", Minimum: 0, Maximum: 1, Decimals: 2, Unit: "", Default: 0.25 }} Value={Workspace.Explode ?? 0.25} Change={(Next) => SetWorkspace((Previous) => ({ ...Previous, Explode: Next }))} />
      </Card>
      <Card Title="Fracture points" Class="ws-card">
        <Metric Value={String(List.length)} Unit={List.length === 1 ? "point" : "points"} Caption={`${FormatEnergy(Total)} in total · each point pulls cracks toward itself by its share of the energy.`} />
        <ol className="ws-points" aria-label="Fracture points">
          {List.map((Each, Index) => (
            <li key={Index} data-point={Index} aria-current={Index === Selected} style={{ "--tone": PointTone(Index) }}>
              <button type="button" className="ws-point-pick" disabled={!Settings.Enabled} onClick={() => Choose(Index)}>
                <i>{Index + 1}</i>
                <span>{Each.X.toFixed(2)}, {Each.Y.toFixed(2)}, {Each.Z.toFixed(2)} m</span>
                <b>{FormatEnergy(Each.Energy)}</b>
              </button>
              <button type="button" className="ws-point-remove" aria-label={`Remove point ${Index + 1}`} disabled={!Settings.Enabled || List.length === 1} onClick={() => { Store(List.filter((_, Slot) => Slot !== Index)); Choose(Math.max(0, Selected - (Index <= Selected ? 1 : 0))); }}>
                ×
              </button>
            </li>
          ))}
        </ol>
        <Control
          Field={{ Label: `Energy · point ${Selected + 1}`, Minimum: LIMITS.Energy[0], Maximum: LIMITS.Energy[1], Decimals: 0, Unit: "J", Default: 2500, Curve: 3 }}
          Value={List[Selected].Energy}
          Disabled={!Settings.Enabled}
          Change={(Next) => Edit(Selected, { Energy: Math.round(Next) })}
        />
        <div className="ws-actions">
          {ENERGY_PRESETS.map((Preset) => (
            <button key={Preset.id} type="button" disabled={!Settings.Enabled} aria-pressed={List[Selected].Energy === Preset.joules} onClick={() => Edit(Selected, { Energy: Preset.joules })}>
              {Preset.label}
            </button>
          ))}
        </div>
        <div className="ws-actions" role="group" aria-label="Clicking the object">
          <button type="button" disabled={!Settings.Enabled || List.length >= MAX_POINTS} aria-pressed={ClickMode === "add" && List.length < MAX_POINTS} onClick={() => SetWorkspace((Previous) => ({ ...Previous, FractureClick: "add" }))}>
            Click adds a point
          </button>
          <button type="button" disabled={!Settings.Enabled} aria-pressed={ClickMode === "move" || List.length >= MAX_POINTS} onClick={() => SetWorkspace((Previous) => ({ ...Previous, FractureClick: "move" }))}>
            Click moves point {Selected + 1}
          </button>
        </div>
        <div className="ws-actions">
          <button type="button" disabled={!Settings.Enabled || List.length >= MAX_POINTS} onClick={() => { Store([...List, { X: 0, Y: 0, Z: 0, Energy: List[Selected].Energy }]); Choose(List.length); }}>
            Add at centre
          </button>
          <button type="button" disabled={!Settings.Enabled} onClick={() => Edit(Selected, { X: 0, Y: 0, Z: 0 })}>
            Centre point {Selected + 1}
          </button>
          <button type="button" disabled={!Settings.Enabled || List.length === 1} onClick={() => { Store([List[Selected]]); Choose(0); }}>
            Keep only point {Selected + 1}
          </button>
        </div>
        <p className="ws-note">Up to {MAX_POINTS} points. Drag a marker in the viewport to move it; it snaps to the surface under the pointer.</p>
      </Card>
      <Card Title="Piece collision" Class="ws-card">
        <Metric
          Value={Colliders ? `${Math.round(Colliders.Fill * 100)}%` : "–"}
          Unit={Colliders ? "filled" : ""}
          Caption={Colliders ? `${Result.cells.length} colliders · ${Colliders.Cost.toLocaleString("en-US")} ${Colliders.Unit} in total · how much of its collider each piece fills, summed.` : "Turn fracture on to give the pieces colliders."}
        />
        <div className="ws-materials ws-piece-collision" role="radiogroup" aria-label="Piece collision">
          {PIECE_COLLISIONS.map((Each) => (
            <button key={Each.id} type="button" role="radio" data-piece-collision={Each.id} aria-checked={Settings.PieceCollision === Each.id} title={Each.note} disabled={!Settings.Enabled} onClick={() => Assign({ PieceCollision: Each.id })}>
              {Each.name}
            </button>
          ))}
        </div>
        <p className="ws-note">{PIECE_COLLISIONS.find((Each) => Each.id === Settings.PieceCollision)?.note}</p>
        {Settings.PieceCollision === "triangles" && Settings.Mode === "dynamic" && <p className="ws-note ws-notice">⚠️ Flying pieces can't collide as triangles; physics uses their hulls until they come to rest.</p>}
        <Control Field={{ Label: "Show colliders", Control: "Switch" }} Value={!!Workspace.ShowPieceColliders} Disabled={!Settings.Enabled} Change={(Next) => SetWorkspace((Previous) => ({ ...Previous, ShowPieceColliders: Next }))} />
      </Card>
      <Card Title="Recipe" Class="ws-card">
        <div className="ws-materials" role="group" aria-label="Fracture material">
          {MATERIALS.map((Each) => (
            <button key={Each.id} type="button" aria-pressed={Settings.Material === Each.id} onClick={() => Assign({ Material: Each.id })} disabled={!Settings.Enabled}>
              <i style={{ background: Each.tone }} />
              {Each.name}
            </button>
          ))}
        </div>
        <Control Field={{ Label: "Piece ceiling", Minimum: LIMITS.Ceiling[0], Maximum: LIMITS.Ceiling[1], Decimals: 0, Unit: "", Default: 48 }} Value={Settings.Ceiling} Disabled={!Settings.Enabled} Change={(Next) => Assign({ Ceiling: Math.round(Next) })} />
        <Control Field={{ Label: "Minimum size", Minimum: LIMITS.MinimumSize[0], Maximum: LIMITS.MinimumSize[1], Decimals: 3, Unit: "m", Default: 0.045 }} Value={Settings.MinimumSize} Disabled={!Settings.Enabled} Change={(Next) => Assign({ MinimumSize: Next })} />
        <Control Field={{ Label: "Seed", Minimum: 1, Maximum: 9999, Decimals: 0, Unit: "", Default: 42 }} Value={Math.min(9999, Settings.Seed)} Disabled={!Settings.Enabled} Change={(Next) => Assign({ Seed: Math.round(Next) })} />
        <div className="ws-readouts">
          <Control Field={{ Label: "Largest / smallest", Control: "Readout" }} Value={Settings.Enabled ? `${FormatLength(Stats.largest)} / ${FormatLength(Stats.smallest)}` : "–"} />
        </div>
        <div className="ws-actions">
          <button type="button" disabled={!Settings.Enabled} onClick={() => Assign({ Seed: 1 + Math.floor(Math.random() * 9998) })}>New seed</button>
        </div>
      </Card>
      <Card Title="Bake" Class="ws-card">
        <div className="ws-actions" role="group" aria-label="Fracture execution">
          {[["dynamic", "Dynamic"], ["baked", "Baked"]].map(([Mode, Label]) => (
            <button key={Mode} type="button" disabled={!Settings.Enabled} aria-pressed={Settings.Mode === Mode} onClick={() => Assign({ Mode })}>
              {Label}
            </button>
          ))}
        </div>
        <p className="ws-note">{STATE_TEXT[State]}</p>
        {Settings.Mode === "baked" && Settings.Enabled && (
          <div className="ws-actions">
            <button type="button" onClick={() => Change("Fracture", { ...Settings, Baked: { Signature: RecipeSignature(Owner, Settings), Pieces: Stats.count, At: Date.now() } })}>
              {State === "ready" ? "Bake again" : "Bake pieces"}
            </button>
          </div>
        )}
      </Card>
    </>
  );
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SHARED FRAME
//------------------------------------------------------------------------------------------------------------------------

// The viewport while a workspace is open: the selection alone, with the workspace's own view.
export function WorkspaceView(Props) {
  const { Subject, Workspace, SetWorkspace } = Props;
  const Tool = WorkspaceTools.find((Each) => Each.Tool === Workspace.Tool);
  return (
    <div className="ws-viewport" role="region" aria-label={`${Tool?.Name} workspace viewport`}>
      <div className="ws-banner">
        <span className="ws-banner-mode">EDIT</span>
        <span>
          <b>{Subject.Name}</b> isolated · {Tool?.Name} workspace
        </span>
        <button type="button" onClick={() => SetWorkspace(null)} aria-label="Leave workspace">
          Leave <kbd>Esc</kbd>
        </button>
      </div>
      {React.createElement(ToolViews[Workspace.Tool] || FractureView, Props)}
    </div>
  );
}

// Inspector: workspace switcher plus the workspace's own cards.
export function WorkspacePanel(Props) {
  const { Subject, Workspace, SetWorkspace } = Props;
  return (
    <div className="ws-panel">
      <section className="ws-switcher" aria-label="Workspace">
        <header>
          <span>
            <WorkspaceGlyph Tool={Workspace.Tool} />
            {WorkspaceTools.find((Each) => Each.Tool === Workspace.Tool)?.Name} · {Subject.Name}
          </span>
          <button type="button" onClick={() => SetWorkspace(null)}>Done</button>
        </header>
      </section>
      {React.createElement(ToolPanels[Workspace.Tool] || FracturePanelBody, Props)}
    </div>
  );
}

// The tool rail on the viewport's left edge: opens a workspace on the selected primitive, switches between them inside one, and the
// lit tool leaves it again. Each button carries its name as a label that slides out on hover.
export function WorkspaceRail({ Active = null, Open, Leave }) {
  return (
    <nav className="ws-rail" aria-label="Workspaces" data-active={Active || ""}>
      <span className="ws-rail-caption">EDIT</span>
      {WorkspaceTools.map((Each) => (
        <button
          key={Each.Tool}
          type="button"
          aria-label={Each.Name}
          aria-pressed={Active === Each.Tool}
          data-tool={Each.Tool}
          title={`${Each.Name} · ${Each.Caption}`}
          onClick={() => (Active === Each.Tool ? Leave() : Open(Each.Tool))}
        >
          <WorkspaceGlyph Tool={Each.Tool} Size={18} />
          <span className="ws-rail-name">{Each.Name}</span>
        </button>
      ))}
    </nav>
  );
}

export function WorkspaceGlyph({ Tool, Size = 16 }) {
  return (
    <svg viewBox="0 0 24 24" width={Size} height={Size} aria-hidden="true" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round">
      {Tool === "sdf" ? (
        <>
          <circle cx="12" cy="12" r="3.2" />
          <circle cx="12" cy="12" r="6.4" opacity="0.6" />
          <circle cx="12" cy="12" r="9.4" opacity="0.32" />
        </>
      ) : Tool === "lod" ? (
        <>
          <rect x="9" y="3.5" width="6" height="4.5" rx="1.2" />
          <rect x="4" y="11" width="6" height="4" rx="1.1" opacity="0.8" />
          <rect x="14" y="11" width="6" height="4" rx="1.1" opacity="0.8" />
          <path d="M12 8v1.6M7 11V9.6h10V11M3.5 18.5h3M8.5 18.5h3M12.5 18.5h3M17.5 18.5h3" opacity="0.55" />
        </>
      ) : Tool === "collision" ? (
        <>
          <path d="M12 3.5 19 7.5v9L12 20.5 5 16.5v-9z" strokeDasharray="2.4 2.2" opacity="0.7" />
          <path d="M12 8.2 15.4 10v4L12 15.8 8.6 14v-4z" fill="currentColor" stroke="none" />
        </>
      ) : Tool === "capture" ? (
        <>
          <path d="M4 8.5V5.5a1.5 1.5 0 0 1 1.5-1.5h3M15.5 4h3A1.5 1.5 0 0 1 20 5.5v3M20 15.5v3a1.5 1.5 0 0 1-1.5 1.5h-3M8.5 20h-3A1.5 1.5 0 0 1 4 18.5v-3" />
          <path d="M12 8.2 15.3 10v3.9L12 15.8 8.7 13.9V10z" opacity="0.8" />
          <path d="M8.7 10 12 11.9 15.3 10M12 11.9v3.9" opacity="0.5" />
        </>
      ) : Tool === "uv" ? (
        <>
          <rect x="4" y="4" width="16" height="16" rx="3.5" />
          <path d="M4 12h16M12 4v16" opacity="0.55" />
          <rect x="6.5" y="6.5" width="4" height="4" rx="1.2" fill="currentColor" stroke="none" />
        </>
      ) : (
        <>
          <path d="M5.5 6.5 12 3.5l6.5 3v8.5L12 20.5 5.5 15z" />
          <path d="M12 3.5 10.5 10l4 2.5-2.5 8M10.5 10 5.5 12" opacity="0.7" />
        </>
      )}
    </svg>
  );
}
