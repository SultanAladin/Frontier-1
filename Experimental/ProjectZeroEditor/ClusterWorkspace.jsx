//============================================================================================================================================
//                                                          CLUSTERWORKSPACE.JSX
//============================================================================================================================================
// 🧰 LOD Clusters workspace, Nanite style. The selection's surface (optionally subdivided to a working density) goes through the
// Geometry Compiler's cluster DAG builder (ClusterHierarchy.js: clusters of ≤ N triangles, groups of neighbours simplified to half with
// locked borders, repeated level by level) and the run-time cut (ClusterSelection.js) picks clusters by projected error in pixels for
// the orbit camera. Settings and the bake stamp live in Values["LOD Clusters"]; the DAG is rebuilt in the browser from them.
// From the Component Viewer's Nanite port: the Shared hierarchy builder can keep surface parts in their own clusters (material families,
// seams held by the simplifier), and the cut can drop clusters whose whole normal cone faces away from the eye.

import React, { useMemo } from "react";
import { buildClusterHierarchy } from "../Shared/GeometryCompile/Engine/ClusterHierarchy.js";
import { createSelectionState, selectClusters } from "../GeometryCompiler/Engine/ClusterSelection.js";
import { shortHash as ShortHash, formatLength as FormatLength } from "../FractureEditor/FractureModel.js";
import { PrimitiveSurface, OrbitView, PaintPolygons, Normal } from "./WorkspaceGeometry.js";
import { ShapeOf } from "./PrimitiveInspector.jsx";
import { Surface, useOrbit, Download, Hue, Clamp, GeometryKeys } from "./WorkspaceSurface.jsx";

export const ClusterKey = "LOD Clusters";
const Densities = [
  { Label: "As authored", Target: 0 },
  { Label: "≈ 4k triangles", Target: 4000 },
  { Label: "≈ 16k triangles", Target: 16000 },
];
const ClusterSizes = [64, 128, 256];
const Colourings = ["Cluster", "Level", "Group", "Part"];
export const LevelTones = ["#7ab8ff", "#7be0ad", "#edc181", "#e59a8a", "#c5a3ff", "#8fd3d8", "#d8d08f", "#b0b0b0"];
// The cut is computed for a 1080-pixel-high view with a 60° vertical field, as a game camera would see it.
const Focal = 1080 / (2 * Math.tan(Math.PI / 6));

export function ClusterSettings(Stored) {
  const Raw = Stored && typeof Stored === "object" ? Stored : {};
  return {
    Density: Raw.Density != null && Number.isFinite(+Raw.Density) ? Clamp(Math.round(+Raw.Density), 0, Densities.length - 1) : 1,
    ClusterTriangles: ClusterSizes.includes(Number(Raw.ClusterTriangles)) ? Number(Raw.ClusterTriangles) : 128,
    GroupClusters: Clamp(Math.round(+Raw.GroupClusters) || 4, 2, 8),
    SimplifyRatio: Clamp(Number.isFinite(+Raw.SimplifyRatio) ? +Raw.SimplifyRatio : 0.5, 0.3, 0.8),
    KeepParts: Raw.KeepParts === true,
    Baked: Raw.Baked && typeof Raw.Baked.Signature === "string" ? Raw.Baked : null,
  };
}

// Midpoint subdivision (1 → 4) with shared edge midpoints, so the surface stays welded for the simplifier.
function Subdivide({ Positions, Indices }) {
  const P = Array.from(Positions), Next = [], Midpoints = new Map();
  const Middle = (A, B) => {
    const Key = A < B ? A + ":" + B : B + ":" + A;
    if (Midpoints.has(Key)) return Midpoints.get(Key);
    P.push((P[A * 3] + P[B * 3]) / 2, (P[A * 3 + 1] + P[B * 3 + 1]) / 2, (P[A * 3 + 2] + P[B * 3 + 2]) / 2);
    Midpoints.set(Key, P.length / 3 - 1);
    return P.length / 3 - 1;
  };
  for (let Slot = 0; Slot < Indices.length; Slot += 3) {
    const [A, B, C] = [Indices[Slot], Indices[Slot + 1], Indices[Slot + 2]];
    const Ab = Middle(A, B), Bc = Middle(B, C), Ca = Middle(C, A);
    Next.push(A, Ab, Ca, Ab, B, Bc, Ca, Bc, C, Ab, Bc, Ca);
  }
  return { Positions: new Float32Array(P), Indices: new Uint32Array(Next) };
}

// Surface parts of a primitive from flat-face normals: the cube's six sides, the caps of cylinders and cones apart from their walls.
// Curved primitives (sphere, torus) are one part. These are the material families the hierarchy builder keeps apart.
export function SurfaceParts(Shape, { Positions, Indices }) {
  const Parts = new Uint32Array(Indices.length / 3);
  if (Shape === "sphere" || Shape === "torus") return { Parts, Count: 1 };
  const Names = new Map();
  for (let T = 0; T < Parts.length; T++) {
    const [A, B, C] = [0, 1, 2].map((K) => [0, 1, 2].map((Axis) => Positions[Indices[T * 3 + K] * 3 + Axis]));
    const N = Normal(A, B, C);
    let Name = "wall";
    if (Shape === "cube") {
      const Axis = [0, 1, 2].reduce((Best, Each) => (Math.abs(N[Each]) > Math.abs(N[Best]) ? Each : Best), 0);
      Name = "xyz"[Axis] + (N[Axis] > 0 ? "+" : "-");
    } else if (Math.abs(N[1]) > 0.999) Name = N[1] > 0 ? "top" : "bottom";
    if (!Names.has(Name)) Names.set(Name, Names.size);
    Parts[T] = Names.get(Name);
  }
  return { Parts, Count: Names.size };
}

const Built = new Map();
export function useClusters(Subject, Values) {
  const Shape = ShapeOf(Subject);
  const Scale = Array.isArray(Values.Scale) ? Values.Scale.map((Each) => Math.abs(+Each) || 1) : [1, 1, 1];
  const Settings = ClusterSettings(Values[ClusterKey]);
  const Key = JSON.stringify([Shape, Scale, GeometryKeys.map((Name) => Values[Name]), Settings.Density, Settings.ClusterTriangles, Settings.GroupClusters, Settings.SimplifyRatio, Settings.KeepParts]);
  const Result = useMemo(() => {
    if (Built.has(Key)) return Built.get(Key);
    let Body = PrimitiveSurface(Shape, (Name, Initial) => (Values[Name] === undefined ? Initial : Values[Name]), Scale);
    const Authored = Body.Indices.length / 3;
    const Target = Densities[Settings.Density].Target;
    while (Target && Body.Indices.length / 3 < Target * 0.75) Body = Subdivide(Body);
    const Split = SurfaceParts(Shape, Body);
    let Dag = null, Problem = null;
    try {
      Dag = buildClusterHierarchy(Body.Positions, Body.Indices, {
        clusterTriangles: Settings.ClusterTriangles,
        groupClusters: Settings.GroupClusters,
        simplifyRatio: Settings.SimplifyRatio,
        families: Split.Parts,
        preserveMaterials: Settings.KeepParts && Split.Count > 1,
      });
    } catch (Error_) {
      Problem = String(Error_?.message || Error_);
    }
    let Far = 0;
    for (let Index = 0; Index < Body.Positions.length; Index += 3) Far = Math.max(Far, Math.hypot(Body.Positions[Index], Body.Positions[Index + 1], Body.Positions[Index + 2]));
    const Entry = { Body, Authored, Parts: Split.Count, Dag, Problem, Radius: Far || 1, Selection: Dag ? createSelectionState(Dag) : null };
    Built.set(Key, Entry);
    while (Built.size > 6) Built.delete(Built.keys().next().value);
    return Entry;
  }, [Key]);
  const Signature = ShortHash(Key);
  const Standing = !Settings.Baked ? "unbaked" : Settings.Baked.Signature === Signature ? "ready" : "stale";
  return { Shape, Settings, Key, Signature, Standing, ...Result };
}

// Simplification error in object units; flat faces simplify with no error at all.
export const ErrorText = (Error_, Level) => (!Level ? "0" : Error_ < 1e-6 ? "≈ 0 (flat)" : Error_ < 0.001 ? (Error_ * 1000).toFixed(3) + " mm" : FormatLength(Error_));

const CutOf = (Workspace) => ({ Threshold: Workspace.LodThreshold ?? 1, Distance: Workspace.LodDistance ?? 4, Level: Workspace.LodLevel ?? -1, Colouring: Workspace.LodColour ?? 0, Wire: Workspace.LodWire !== false, Backface: Workspace.LodBackface === true });

// The cut for the orbit camera: eye along the view direction at the chosen distance.
export function Cut(Build, Workspace) {
  const { Threshold, Distance, Level, Backface } = CutOf(Workspace);
  const View = OrbitView({ Yaw: Workspace.Yaw ?? 0.7, Pitch: Workspace.Pitch ?? 0.45, Radius: 1, Width: 1, Height: 1 });
  const Eye = View.Toward.map((Each) => Each * Distance);
  const Picked = selectClusters(Build.Dag, Build.Selection, { eye: Eye, focal: Focal, near: 0.05, threshold: Threshold, planes: null, backface: Backface, forcedLevel: Level });
  const Clusters = new Set();
  for (let Range = 0; Range < Picked.rangeCount; Range++)
    for (let Corner = Picked.firsts[Range], End = Corner + Picked.counts[Range]; Corner < End; Corner += 3) Clusters.add(Build.Dag.cornerCluster[Corner]);
  return {
    Ranges: Array.from({ length: Picked.rangeCount }, (_, Index) => [Picked.firsts[Index], Picked.counts[Index]]),
    Clusters,
    Triangles: Picked.triangles,
    Tests: Picked.tests,
    CulledBackface: Picked.culledBackface,
    LevelTriangles: Array.from(Picked.levelTriangles.slice(0, Build.Dag.levels.length)),
    LevelClusters: Array.from(Picked.levelClusters.slice(0, Build.Dag.levels.length)),
  };
}

const ClusterTone = (Build, Cluster, Colouring, Lit) => {
  const H = Build.Dag;
  if (Colouring === 1) {
    const Tone = LevelTones[H.clusterLevel[Cluster] % LevelTones.length];
    return { Tone, Shade: (1 - Lit) * 0.7 };
  }
  const Index = Colouring === 2 ? Math.max(0, H.clusterGroupSelf[Cluster]) + 7 : Colouring === 3 ? Math.max(0, H.clusterFamily[Cluster]) * 3 + 2 : Cluster;
  return { Tone: `hsl(${Hue(Index)} 40% ${Math.round(30 + 34 * Lit)}%)`, Shade: 0 };
};

// The cut drawn from an orbit view: one polygon per picked triangle, toned by cluster, level or group. Shared with the LOD preview card.
export function PaintCut(Brush, Width, Height, Build, Current, { Yaw = 0.7, Pitch = 0.45, Zoom = 1, Colouring = 0, Wire = true } = {}) {
  const View = OrbitView({ Yaw, Pitch, Radius: Build.Radius, Width, Height, Zoom });
  const H = Build.Dag, P = Build.Body.Positions, Polygons = [];
  for (const [First, Count] of Current.Ranges)
    for (let Corner = First; Corner < First + Count; Corner += 3) {
      const Cluster = H.cornerCluster[Corner];
      Polygons.push({
        Points: [0, 1, 2].map((Offset) => {
          const V = H.cornerVertex[Corner + Offset] * 3;
          return [P[V], P[V + 1], P[V + 2]];
        }),
        Paint: (B, Screen, Lit) => {
          const { Tone, Shade } = ClusterTone(Build, Cluster, Colouring, Lit);
          B.beginPath();
          Screen.forEach(([X, Y], Index) => (Index ? B.lineTo(X, Y) : B.moveTo(X, Y)));
          B.closePath();
          B.fillStyle = Tone;
          B.fill();
          if (Shade) {
            B.fillStyle = `rgba(0,0,0,${Shade})`;
            B.fill();
          }
          B.strokeStyle = Wire ? "#00000070" : Tone;
          B.lineWidth = Wire ? 0.5 : 0.6;
          B.stroke();
        },
      });
    }
  PaintPolygons(Brush, View, Polygons);
}

export function ClusterView({ Subject, Values, Workspace, SetWorkspace }) {
  const Build = useClusters(Subject, Values);
  const Orbit = useOrbit(Workspace, SetWorkspace);
  const { Colouring, Wire } = CutOf(Workspace);
  const Current = Build.Dag ? Cut(Build, Workspace) : null;

  const PaintObject = (Brush, Width, Height) =>
    Current && PaintCut(Brush, Width, Height, Build, Current, { Yaw: Workspace.Yaw ?? 0.7, Pitch: Workspace.Pitch ?? 0.45, Zoom: Workspace.Zoom ?? 1, Colouring, Wire });

  // DAG levels, coarsest at the top: one block per cluster, lit when it is part of the current cut.
  const PaintLevels = (Brush, Width, Height) => {
    if (!Current) {
      Brush.fillStyle = "#8a8a8a";
      Brush.font = "11px 'DM Sans', sans-serif";
      Brush.fillText(Build.Problem || "No clusters", 16, 40);
      return;
    }
    const H = Build.Dag, Levels = H.levels, Top = 40, Gap = 8;
    const Row = Math.min(64, (Height - Top - 16 - Gap * (Levels.length - 1)) / Levels.length);
    const Left = 96, Room = Width - Left - 16;
    const ByLevel = Levels.map(() => []);
    for (let Cluster = 0; Cluster < H.clusterCount; Cluster++) ByLevel[H.clusterLevel[Cluster]].push(Cluster);
    Brush.font = "10px 'DM Sans', sans-serif";
    Levels.forEach((Level, Index) => {
      const Y = Top + (Levels.length - 1 - Index) * (Row + Gap);
      const Members = ByLevel[Index];
      // Largest block size whose wrapped rows still fit inside this level's band.
      let Cell = Math.min(Row, Room);
      while (Cell > 2 && Math.ceil(Members.length / Math.max(1, Math.floor(Room / Cell))) * Cell > Row) Cell *= 0.94;
      const PerRow = Math.max(1, Math.floor(Room / Cell)), Size = Math.max(1.5, Cell - (Cell > 6 ? 2 : 0.5));
      Members.forEach((Cluster, Slot) => {
        const X = Left + (Slot % PerRow) * Cell, Yc = Y + Math.floor(Slot / PerRow) * Cell;
        const On = Current.Clusters.has(Cluster);
        Brush.fillStyle = On ? (Colouring === 1 ? LevelTones[Index % LevelTones.length] : `hsl(${Hue(Colouring === 2 ? Math.max(0, H.clusterGroupSelf[Cluster]) + 7 : Colouring === 3 ? Math.max(0, H.clusterFamily[Cluster]) * 3 + 2 : Cluster)} 40% 58%)`) : "#262626";
        Brush.fillRect(X, Yc, Size, Size);
      });
      Brush.fillStyle = Current.LevelClusters[Index] ? "#e6e6e6" : "#7a7a7a";
      Brush.textAlign = "left";
      Brush.fillText("L" + Index, 16, Y + 11);
      Brush.fillStyle = "#7a7a7a";
      Brush.fillText(`${Level.clusters} · ${Level.triangles}`, 16, Y + 24);
    });
  };
  return (
    <div className="ws-view ws-view-lod">
      <div className="ws-pane">
        <span className="ws-pane-label">
          {Current ? `${Current.Triangles.toLocaleString("en-US")} triangles · ${Current.Clusters.size} clusters in the cut` : Build.Problem || "Building clusters…"}
        </span>
        <Surface Paint={PaintObject} Label={`${Subject.Name} cluster cut`} Handlers={{ ...Orbit, onPointerUp: (Event) => Orbit.End(Event) }} />
      </div>
      <div className="ws-pane">
        <span className="ws-pane-label">Cluster DAG · coarsest level on top · lit blocks are drawn</span>
        <Surface Paint={PaintLevels} Label="Cluster DAG levels" Class="ws-uv" />
      </div>
    </div>
  );
}

export function ClusterPanel({ Subject, Values, Change, Workspace, SetWorkspace, Kit }) {
  const { Card, Control, Metric } = Kit;
  const Build = useClusters(Subject, Values);
  const { Settings, Dag } = Build;
  const { Threshold, Distance, Level, Colouring, Wire, Backface } = CutOf(Workspace);
  const Current = Dag ? Cut(Build, Workspace) : null;
  const Assign = (Patch) => Change(ClusterKey, { ...Settings, ...Patch });
  const Look = (Patch) => SetWorkspace((Previous) => ({ ...Previous, ...Patch }));
  const Readout = (Label, Text) => <Control key={Label} Field={{ Label, Control: "Readout" }} Value={Text} />;
  const Source = Build.Body.Indices.length / 3;
  const Name = String(Subject.Name || "geometry").replace(/[^a-z0-9]+/gi, "_");
  // Welded OBJ of the triangles in the current cut; every level indexes the one shared vertex buffer.
  const ExportCut = () => {
    const Renumber = new Map(), Lines = [`# Frontier LOD cluster cut · ${Current.Triangles} triangles · ${Current.Clusters.size} clusters`, `o ${Name}_lod_cut`], Faces = [];
    const P = Build.Body.Positions;
    for (const [First, Count] of Current.Ranges)
      for (let Corner = First; Corner < First + Count; Corner += 3) {
        const Face = [0, 1, 2].map((Offset) => {
          const Vertex = Dag.cornerVertex[Corner + Offset];
          if (!Renumber.has(Vertex)) {
            Renumber.set(Vertex, Renumber.size + 1);
            Lines.push(`v ${P[Vertex * 3].toFixed(6)} ${P[Vertex * 3 + 1].toFixed(6)} ${P[Vertex * 3 + 2].toFixed(6)}`);
          }
          return Renumber.get(Vertex);
        });
        Faces.push("f " + Face.join(" "));
      }
    Download(`${Name}_lod_cut.obj`, [...Lines, ...Faces].join("\n") + "\n");
  };
  return (
    <>
      <Card Title="Cluster DAG" Class="ws-card">
        <Metric
          Value={Dag ? String(Dag.levels.length) : "–"}
          Unit="levels"
          Caption={Dag ? `${Dag.clusterCount} clusters in ${Dag.groupCount} groups · ${Source.toLocaleString("en-US")} source triangles · built in ${Dag.buildMs.toFixed(0)} ms` : Build.Problem}
        />
        <Control Field={{ Label: "Working density", Control: "Select", Options: Densities.map((Each) => Each.Label) }} Value={Settings.Density} Change={(Index) => Assign({ Density: Index })} />
        <Control Field={{ Label: "Cluster size", Control: "Select", Options: ClusterSizes.map((Each) => `${Each} triangles`) }} Value={ClusterSizes.indexOf(Settings.ClusterTriangles)} Change={(Index) => Assign({ ClusterTriangles: ClusterSizes[Index] })} />
        <Control Field={{ Label: "Clusters per group", Minimum: 2, Maximum: 8, Decimals: 0, Unit: "", Default: 4 }} Value={Settings.GroupClusters} Change={(Next) => Assign({ GroupClusters: Math.round(Next) })} />
        <Control Field={{ Label: "Simplify to", Minimum: 0.3, Maximum: 0.8, Decimals: 2, Unit: "×", Default: 0.5 }} Value={Settings.SimplifyRatio} Change={(Next) => Assign({ SimplifyRatio: Next })} />
        <Control Field={{ Label: "Keep surface parts apart", Control: "Switch" }} Value={Settings.KeepParts} Disabled={Build.Parts < 2} Change={(Next) => Assign({ KeepParts: Next })} />
        {Dag && (
          <div className="ws-level-table" role="table" aria-label="Cluster levels">
            <b>LEVEL</b>
            <b>CLUSTERS</b>
            <b>TRIANGLES</b>
            <b>ERROR</b>
            {Dag.levels.map((Each, Index) => (
              <React.Fragment key={Index}>
                <span className={Current?.LevelClusters[Index] ? "cut" : ""}>L{Index}</span>
                <span>{Each.clusters}</span>
                <span>{Each.triangles.toLocaleString("en-US")}</span>
                <span>{ErrorText(Each.error, Index)}</span>
              </React.Fragment>
            ))}
          </div>
        )}
        <p className="ws-note">
          {Build.Authored < Source ? `Authored surface has ${Build.Authored.toLocaleString("en-US")} triangles; it is subdivided to the working density first. ` : ""}
          Group borders stay locked while a level is simplified, so any mix of levels across a border is crack-free.
          {Build.Parts > 1 ? ` ${Build.Parts} surface parts${Settings.KeepParts ? "; no cluster mixes two of them and their seams are held." : "; clusters may span them."}` : " One curved surface part."}
          {Dag?.stalled ? " The last level stopped reducing; more than one root remains." : ""}
        </p>
      </Card>
      <Card Title="Error cut" Class="ws-card">
        <Metric
          Value={Current ? Current.Triangles.toLocaleString("en-US") : "–"}
          Unit="triangles drawn"
          Caption={Current ? `${Current.Clusters.size} clusters · ${(100 * Current.Triangles / Source).toFixed(1)}% of source · ${Current.Tests} DAG tests${Backface ? ` · ${Current.CulledBackface} culled facing away` : ""}` : ""}
        />
        <Control Field={{ Label: "Pixel error", Minimum: 0.25, Maximum: 32, Decimals: 2, Unit: "px", Default: 1, Curve: 2 }} Value={Threshold} Disabled={Level >= 0} Change={(Next) => Look({ LodThreshold: Next })} />
        <Control Field={{ Label: "View distance", Minimum: 0.5, Maximum: 200, Decimals: 1, Unit: "m", Default: 4, Curve: 3 }} Value={Distance} Disabled={Level >= 0} Change={(Next) => Look({ LodDistance: Next })} />
        <Control
          Field={{ Label: "Level", Control: "Select", Options: ["Automatic (error cut)", ...(Dag?.levels || []).map((_, Index) => `Force L${Index}`)] }}
          Value={Level + 1}
          Change={(Index) => Look({ LodLevel: Index - 1 })}
        />
        <Control Field={{ Label: "Cull back-facing clusters", Control: "Switch" }} Value={Backface} Change={(Next) => Look({ LodBackface: Next })} />
        <Control Field={{ Label: "Colour by", Control: "Select", Options: Colourings }} Value={Colouring} Change={(Index) => Look({ LodColour: Index })} />
        <Control Field={{ Label: "Triangle edges", Control: "Switch" }} Value={Wire} Change={(Next) => Look({ LodWire: Next })} />
        <p className="ws-note">Cut for a 1080 p view with a 60° field from the orbit direction. A cluster is drawn when its own error projects under the threshold and its parent's does not. Back-face culling drops a cluster when every normal in its cone points away from the eye.</p>
      </Card>
      <Card Title="Bake" Class="ws-card">
        <div className="ws-bake-status" data-status={Build.Standing}>
          <i />
          {Build.Standing === "ready" ? `Baked ${new Date(Settings.Baked.At).toLocaleTimeString()} · up to date` : Build.Standing === "stale" ? "Geometry or settings changed since the last bake." : "Not baked yet."}
        </div>
        <div className="ws-actions">
          <button type="button" disabled={!Dag} onClick={() => Assign({ Baked: { Signature: Build.Signature, At: Date.now(), Levels: Dag.levels.length, Clusters: Dag.clusterCount, Groups: Dag.groupCount } })}>
            {Build.Standing === "ready" ? "Bake again" : "Bake clusters"}
          </button>
          <button type="button" disabled={!Current} onClick={ExportCut}>
            Export cut as OBJ
          </button>
        </div>
        <p className="ws-note">The object stores these settings and the bake stamp. Clusters are rebuilt in the browser from them; native streaming is pending.</p>
      </Card>
    </>
  );
}
