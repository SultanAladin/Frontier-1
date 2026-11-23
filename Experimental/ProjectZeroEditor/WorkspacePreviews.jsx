//============================================================================================================================================
//                                                          WORKSPACEPREVIEWS.JSX
//============================================================================================================================================
// 🧰 Inspector preview cards for the edit-mode workspaces, one per carousel page: the UV card shows the unwrap in its UDIM tiles, the
// fracture card the current shatter, the SDF card the centre slice of the distance field and the LOD card the cluster levels. Each
// opens its full workspace and reads the same data the workspace edits. Follow-up 46: each card has a preview slider (fracture explode,
// SDF slice depth, LOD level) that moves only the card's own picture; it is not stored and the viewport is unchanged.

import React, { useMemo, useState } from "react";
import { pieceStats as PieceStats, bakeState as BakeState, storageEstimate as StorageEstimate, STATE_TEXT, MATERIAL, formatEnergy as FormatEnergy, formatLength as FormatLength, formatBytes as FormatBytes } from "../FractureEditor/FractureModel.js";
import { useUvLayout, useShatter, PaintFracture, WorkspaceGlyph } from "./Workspaces.jsx";
import { Surface, Hue } from "./WorkspaceSurface.jsx";
import { CheckerTexture } from "./WorkspaceGeometry.js";
import { useSdfVolume, useSliceImage } from "./SdfWorkspace.jsx";
import { useClusters, LevelTones, ErrorText, Cut, PaintCut } from "./ClusterWorkspace.jsx";

// UDIM numbering: 1001 + U tile + 10 × V tile, ten tiles per row.
export const UdimOf = (U, V) => 1001 + Math.max(0, Math.min(9, Math.floor(U))) + 10 * Math.max(0, Math.floor(V));

function IslandTiles(Layout, Uvs) {
  return Layout.charts.map((Chart) => {
    let MinU = Infinity, MinV = Infinity, MaxU = -Infinity, MaxV = -Infinity;
    for (const Triangle of Chart.triangles)
      for (let K = 0; K < 6; K += 2) {
        const U = Uvs[Triangle * 6 + K], V = Uvs[Triangle * 6 + K + 1];
        MinU = Math.min(MinU, U); MaxU = Math.max(MaxU, U);
        MinV = Math.min(MinV, V); MaxV = Math.max(MaxV, V);
      }
    return UdimOf((MinU + MaxU) / 2, (MinV + MaxV) / 2);
  });
}

function OpenButton({ Tool, Label, Open }) {
  return (
    <div className="ws-actions">
      <button type="button" className="ws-open" disabled={!Open} onClick={Open}>
        <WorkspaceGlyph Tool={Tool} Size={14} />
        {Label}
      </button>
    </div>
  );
}

export function UvPreviewCard({ Subject, Values, Open, Kit }) {
  const { Card, Control, Metric } = Kit;
  const { Layout, Uvs, Statistics, Angle } = useUvLayout(Subject, Values);
  const Tiles = IslandTiles(Layout, Uvs);
  const Counts = new Map();
  Tiles.forEach((Tile) => Counts.set(Tile, (Counts.get(Tile) || 0) + 1));
  const Strip = [1001, 1002, 1003, 1004];
  const Elsewhere = [...Counts.keys()].filter((Tile) => !Strip.includes(Tile));
  // Main tile on the left, the next three UDIMs stacked on the right; each tile draws the islands that land in it.
  const Paint = (Brush, Width, Height) => {
    const Side = Math.min(Height - 8, Width - 78), Small = (Side - 12) / 3;
    const Rects = [[1001, 4, 4, Side], ...[1002, 1003, 1004].map((Tile, Index) => [Tile, Side + 16, 4 + Index * (Small + 6), Small])];
    const Checker = CheckerTexture(8);
    for (const [Tile, Left, Top, Size] of Rects) {
      const Offset = [(Tile - 1001) % 10, Math.floor((Tile - 1001) / 10)];
      Brush.save();
      Brush.beginPath();
      Brush.roundRect(Left, Top, Size, Size, 6);
      Brush.fillStyle = "#151515";
      Brush.fill();
      Brush.clip();
      Brush.globalAlpha = 0.16;
      Brush.drawImage(Checker, Left, Top, Size, Size);
      Brush.globalAlpha = 1;
      const X = (U) => Left + (U - Offset[0]) * Size, Y = (V) => Top + (1 - (V - Offset[1])) * Size;
      Layout.charts.forEach((Chart, Index) => {
        if (Tiles[Index] !== Tile) return;
        Brush.beginPath();
        for (const Triangle of Chart.triangles) {
          const A = Triangle * 6;
          Brush.moveTo(X(Uvs[A]), Y(Uvs[A + 1]));
          Brush.lineTo(X(Uvs[A + 2]), Y(Uvs[A + 3]));
          Brush.lineTo(X(Uvs[A + 4]), Y(Uvs[A + 5]));
          Brush.closePath();
        }
        Brush.fillStyle = `hsla(${Hue(Index)} 38% 56% / 0.5)`;
        Brush.fill();
        Brush.strokeStyle = "#ffffff30";
        Brush.lineWidth = 0.5;
        Brush.stroke();
      });
      Brush.restore();
      Brush.strokeStyle = Counts.get(Tile) ? "#ffffff40" : "#ffffff14";
      Brush.beginPath();
      Brush.roundRect(Left + 0.5, Top + 0.5, Size - 1, Size - 1, 6);
      Brush.stroke();
      Brush.fillStyle = Counts.get(Tile) ? "#d6d6d6" : "#6a6a6a";
      Brush.font = "9px 'DM Sans', sans-serif";
      Brush.fillText(String(Tile), Left + 6, Top + Size - 6);
    }
  };
  return (
    <Card Title="UV preview" Class="ws-preview-card">
      <Metric
        Value={(Statistics.uvArea * 100).toFixed(1)}
        Unit="% of 1001 used"
        Caption={`${Layout.charts.length} islands · ${Layout.seams.length} seam edges · ${Counts.size} UDIM tile${Counts.size === 1 ? "" : "s"} in use`}
      />
      <button type="button" className="ws-preview-canvas ws-preview-uv" onClick={Open} disabled={!Open} aria-label="Open the UV workspace">
        <Surface Paint={Paint} Label={`${Subject.Name} UV tiles`} Deps={[Values["UV Layout"]]} />
      </button>
      <div className="ws-udims" aria-label="UDIM tiles">
        {Strip.map((Tile) => (
          <span key={Tile} className={Counts.get(Tile) ? "on" : ""}>
            <b>{Tile}</b>
            <small>{Counts.get(Tile) ? `${Counts.get(Tile)} island${Counts.get(Tile) > 1 ? "s" : ""}` : "empty"}</small>
          </span>
        ))}
      </div>
      {Elsewhere.length > 0 && <p className="ws-note">Also in {Elsewhere.sort((A, B) => A - B).join(", ")}.</p>}
      <Control Field={{ Label: "Texel scale", Control: "Readout" }} Value={Statistics.texelScale.toFixed(3) + " UV/m"} />
      <Control Field={{ Label: "Unwrap angle", Control: "Readout" }} Value={Angle + "°"} />
      <OpenButton Tool="uv" Label="Edit in UV workspace" Open={Open} />
    </Card>
  );
}

export function FracturePreviewCard({ Subject, Values, Open, Kit }) {
  const { Card, Control, Metric } = Kit;
  const { Owner, Settings, Result } = useShatter(Subject, Values);
  const [Explode, SetExplode] = useState(0.3);
  const Stats = PieceStats(Result.cells);
  const Standing = BakeState(Owner, Settings);
  const Material = MATERIAL[Settings.Material];
  const Storage = StorageEstimate(Stats.count, Settings);
  const Readout = (Label, Text) => <Control key={Label} Field={{ Label, Control: "Readout" }} Value={Text} />;
  const Paint = (Brush, Width, Height) => PaintFracture(Brush, Width, Height, { Owner, Settings, Result, Yaw: 0.7, Pitch: 0.42, Zoom: 1.05, Explode: Settings.Enabled ? Explode : 0 });
  return (
    <Card Title="Fracture preview" Class="ws-preview-card">
      <Metric
        Value={Settings.Enabled ? String(Stats.count) : "Off"}
        Unit={Settings.Enabled ? "pieces" : ""}
        Caption={Settings.Enabled ? `${Material.note} ${FormatEnergy(Settings.Energy)} impact · limited by ${Result.budget.limiter}.` : "Fracture is off for this object. The preview shows it whole."}
      />
      <button type="button" className="ws-preview-canvas ws-preview-fracture" onClick={Open} disabled={!Open} aria-label="Open the fracture workspace">
        <Surface Paint={Paint} Label={`${Subject.Name} fracture preview`} Deps={[Explode]} />
      </button>
      <div className="ws-preview-control" data-preview-control="explode">
        <Control Field={{ Label: "Explode", Minimum: 0, Maximum: 1, Decimals: 2, Unit: "" }} Value={Explode} Disabled={!Settings.Enabled} Change={SetExplode} />
      </div>
      {Readout("Material", Material.name)}
      {Readout("Impact energy", FormatEnergy(Settings.Energy))}
      {Settings.Points?.length > 1 ? Readout("Fracture points", `${Settings.Points.length} points`) : Readout("Impact point", `${Settings.X.toFixed(2)}, ${Settings.Y.toFixed(2)}, ${Settings.Z.toFixed(2)} m`)}
      {Readout("Median piece", Settings.Enabled ? FormatLength(Stats.median) : "–")}
      {Readout("Largest / smallest", Settings.Enabled ? `${FormatLength(Stats.largest)} / ${FormatLength(Stats.smallest)}` : "–")}
      {Readout("Ceiling · seed", `${Settings.Ceiling} pieces · ${Settings.Seed}`)}
      {Readout("Execution", Settings.Mode === "baked" ? "Baked" : "Dynamic")}
      {Readout("Baked storage", Settings.Enabled ? FormatBytes(Storage.total) : "–")}
      <p className="ws-note">{STATE_TEXT[Standing]}</p>
      <OpenButton Tool="fracture" Label="Edit in fracture workspace" Open={Open} />
    </Card>
  );
}

// Bake status line shared by the SDF and LOD cards.
function BakeLine({ Standing, Baked }) {
  return (
    <div className="ws-bake-status" data-status={Standing}>
      <i />
      {Standing === "ready" ? `Baked ${new Date(Baked.At).toLocaleTimeString()} · up to date` : Standing === "stale" ? "Changed since the last bake." : "Not baked yet."}
    </div>
  );
}

export function SdfPreviewCard({ Subject, Values, Open, Kit }) {
  const { Card, Control, Metric } = Kit;
  const Sdf = useSdfVolume(Subject, Values);
  const { Volume, Settings } = Sdf;
  const [Depth, SetDepth] = useState(0.5);
  const Image = useSliceImage(Sdf, 0, Depth, 0, 0);
  const Readout = (Label, Text) => <Control key={Label} Field={{ Label, Control: "Readout" }} Value={Text} />;
  const Paint = (Brush, Width, Height) => {
    Brush.fillStyle = "#121212";
    Brush.fillRect(0, 0, Width, Height);
    if (!Image) {
      Brush.fillStyle = "#6a6a6a";
      Brush.font = "11px 'DM Sans', sans-serif";
      Brush.textAlign = "center";
      Brush.fillText("Baking the distance field…", Width / 2, Height / 2);
      return;
    }
    const Side = Math.min(Width, Height) - 16;
    Brush.imageSmoothingEnabled = false;
    Brush.drawImage(Image, (Width - Side) / 2, 8, Side, Side);
    Brush.fillStyle = "#8a8a8a";
    Brush.font = "9px 'DM Sans', sans-serif";
    Brush.textAlign = "left";
    Brush.fillText(Math.abs(Depth - 0.5) < 0.005 ? "centre slice · XY" : `slice at ${Math.round(Depth * 100)}% · XY`, 10, Height - 10);
  };
  return (
    <Card Title="SDF preview" Class="ws-preview-card">
      <Metric
        Value={Volume ? String(Volume.allocatedBrickCount) : "…"}
        Unit={Volume ? `of ${Volume.totalBricks} bricks` : "baking"}
        Caption={Volume ? `${Volume.dim}³ voxels · ${Volume.prunedPercent}% of bricks pruned` : Sdf.Problem || "Building the sparse volume from the surface."}
      />
      <button type="button" className="ws-preview-canvas ws-preview-sdf" onClick={Open} disabled={!Open} aria-label="Open the SDF workspace">
        <Surface Paint={Paint} Label={`${Subject.Name} distance field slice`} Deps={[Image, Depth]} />
      </button>
      <div className="ws-preview-control" data-preview-control="slice">
        <Control Field={{ Label: "Slice depth", Minimum: 0, Maximum: 1, Decimals: 2, Unit: "", DisplayScale: 100 }} Value={Depth} Disabled={!Volume} Change={SetDepth} />
      </div>
      {Readout("Resolution", `${Settings.Resolution}³ · ${Volume ? FormatLength(Volume.voxelStep) + " voxel" : "–"}`)}
      {Readout("Narrow band", Volume ? `${Settings.Band.toFixed(1)} voxels · ${FormatLength(Volume.narrowBandMeters)}` : "–")}
      {Readout("Memory", Volume ? `${FormatBytes(Volume.mip0Bytes + Volume.mip1Bytes)} · dense ${FormatBytes(Volume.denseBytes)}` : "–")}
      {Readout("Source", `${(Sdf.Body.Indices.length / 3).toLocaleString("en-US")} triangles · ${Sdf.Closed ? "closed" : "open shell"}`)}
      <BakeLine Standing={Sdf.Standing} Baked={Settings.Baked} />
      <OpenButton Tool="sdf" Label="Edit in SDF workspace" Open={Open} />
    </Card>
  );
}

export function LodPreviewCard({ Subject, Values, Open, Kit }) {
  const { Card, Control, Metric } = Kit;
  const Build = useClusters(Subject, Values);
  const { Dag, Settings } = Build;
  const Source = Build.Body.Indices.length / 3;
  const Levels = Dag?.levels || [];
  const Top = Math.log10(Math.max(10, Source));
  const [Picked, Pick] = useState(0);
  const Level = Math.min(Picked, Math.max(0, Levels.length - 1));
  const Current = useMemo(() => (Dag ? Cut(Build, { LodLevel: Level }) : null), [Build, Dag, Level]);
  const Readout = (Label, Text) => <Control key={Label} Field={{ Label, Control: "Readout" }} Value={Text} />;
  const Paint = (Brush, Width, Height) => {
    Brush.fillStyle = "#121212";
    Brush.fillRect(0, 0, Width, Height);
    if (!Current) return;
    PaintCut(Brush, Width, Height, Build, Current, { Yaw: 0.7, Pitch: 0.42, Zoom: 1.05, Colouring: 0, Wire: true });
    Brush.fillStyle = "#8a8a8a";
    Brush.font = "9px 'DM Sans', sans-serif";
    Brush.textAlign = "left";
    Brush.fillText(`L${Level} · ${Current.Triangles.toLocaleString("en-US")} triangles · ${Current.Clusters.size} clusters`, 10, Height - 10);
  };
  return (
    <Card Title="LOD preview" Class="ws-preview-card">
      <Metric
        Value={Dag ? String(Levels.length) : "–"}
        Unit="levels"
        Caption={Dag ? `${Dag.clusterCount} clusters in ${Dag.groupCount} groups · ${Source.toLocaleString("en-US")} source triangles` : Build.Problem || ""}
      />
      <button type="button" className="ws-preview-canvas ws-preview-lod" onClick={Open} disabled={!Open} aria-label="Open the LOD workspace">
        <svg viewBox="0 0 300 150" role="img" aria-label="Triangles per level">
          {Levels.map((Each, Index) => {
            const Width = 268 / Levels.length, X = 22 + Index * Width, Height = Math.max(3, (Math.log10(Math.max(1, Each.triangles)) / Top) * 104);
            return (
              <g key={Index}>
                <rect x={X + 3} y={122 - Height} width={Width - 6} height={Height} rx="3" fill={LevelTones[Index % LevelTones.length]} opacity={Index === Level ? 1 : 0.35} stroke={Index === Level ? "#fff" : "none"} strokeWidth="1.2" data-level={Index} />
                <text x={X + Width / 2} y={116 - Height} textAnchor="middle">{Each.triangles.toLocaleString("en-US")}</text>
                <text x={X + Width / 2} y="138" textAnchor="middle">L{Index}</text>
              </g>
            );
          })}
          <line x1="22" x2="290" y1="122" y2="122" stroke="#ffffff22" />
        </svg>
      </button>
      <div className="ws-preview-canvas ws-preview-lod-cut" data-level={Level}>
        <Surface Paint={Paint} Label={`${Subject.Name} at LOD ${Level}`} Deps={[Current?.Triangles, Level, Dag]} />
      </div>
      <div className="ws-preview-control" data-preview-control="level">
        <Control Field={{ Label: "Preview level", Minimum: 0, Maximum: Math.max(1, Levels.length - 1), Decimals: 0, Unit: Levels.length ? `of ${Levels.length - 1}` : "" }} Value={Level} Disabled={Levels.length < 2} Change={(Next) => Pick(Math.round(Next))} />
      </div>
      {Readout("Cluster size", `${Settings.ClusterTriangles} triangles · ${Settings.GroupClusters} per group`)}
      {Readout("Simplify to", `${Settings.SimplifyRatio.toFixed(2)} × per level`)}
      {Readout("Coarsest level", Levels.length ? `${Levels[Levels.length - 1].triangles.toLocaleString("en-US")} triangles · ${ErrorText(Levels[Levels.length - 1].error, Levels.length - 1)} error` : "–")}
      {Readout("Build time", Dag ? `${Dag.buildMs.toFixed(0)} ms` : "–")}
      <BakeLine Standing={Build.Standing} Baked={Settings.Baked} />
      <OpenButton Tool="lod" Label="Edit in LOD workspace" Open={Open} />
    </Card>
  );
}
