//==========================================================================================================================================
//                                                MODELTHUMBNAILRENDERER.JS
//==========================================================================================================================================
// Miniature 3D Analytical OpenPBR Studio Thumbnail Raytracer for Analytical Primitives, Multi-Part Props, Cameras & Studio Lights
//   Renders genuine raytraced 3-point studio thumbnails on a 100% TRANSPARENT BACKGROUND (Alpha = 0) with tight auto-fit camera framing
//   for Outliner rows, Asset Browser ("Models"), and Construct ("Geometry" / "Cameras" / "Lighting").

import { CpuGtxRenderer } from "./CpuGtxFallbackRenderer.js";
import { MaterialAtlasSynchronizer } from "./MaterialAtlasCompiler.js";
import { CompileLiveGtxScene } from "./ShowcaseGalleryScene.js";
import {
  BuildCylinderConeMaterial,
  BuildNeutralPrimitiveMaterial,
  BuildTrafficConeMaterial,
  WORLD_PLACEABLE_MODELS,
} from "./WorldDragDrop.js";
import {
  ComputePrimitivesTightBounds,
  GetWorldPropById,
  HasActiveFaceColors,
} from "./WorldPropLibrary.js";

const ThumbnailUrlCache = new Map();
const ThumbnailRgbaCache = new Map();

let SharedThumbAtlas = null;
let SharedThumbTracer = null;

function ResolveDefaultThumbMaterial(Shape) {
  if (Shape === "cone") return BuildTrafficConeMaterial();
  if (Shape === "cylinder-cone") return BuildCylinderConeMaterial();
  const Spec = WORLD_PLACEABLE_MODELS.find((M) => M.PrimitiveShape === Shape && !M.PropId);
  if (Spec && typeof Spec.BuildMaterial === "function") return Spec.BuildMaterial();
  return BuildNeutralPrimitiveMaterial("#d4d0c8", 0.30, 0.04);
}

function ResolveDefaultThumbScale(Shape) {
  if (Shape === "cone") return [0.62, 0.62, 0.96];
  if (Shape === "cylinder-cone") return [0.58, 0.58, 0.90];
  if (Shape === "torus") return [0.88, 0.88, 0.30];
  if (Shape === "cylinder") return [0.68, 0.68, 0.88];
  if (Shape === "sphere") return [0.84, 0.84, 0.84];
  return [0.76, 0.76, 0.76];
}

function ComputeThumbHashKey({ PropId, Shape, Scale, Material, FaceColors }) {
  if (PropId) {
    const Fc = HasActiveFaceColors(FaceColors) ? JSON.stringify(FaceColors) : "";
    const ColFill = Material?.Channels?.colour?.Fill || "";
    return `prop:${PropId}|${ColFill}|${Fc}`;
  }
  const S = Array.isArray(Scale) ? Scale.join(",") : "default";
  const Ch = Material?.Channels || {};
  const ColCh = Ch.colour || {};
  const StopsKey = Array.isArray(ColCh.Stops)
    ? ColCh.Stops.map((St) => `${St.Position}:${St.Value}`).join("|")
    : "";
  const Fc = HasActiveFaceColors(FaceColors) ? JSON.stringify(FaceColors) : "";
  return `${Shape}|${S}|${Material?.Type || "Standard"}|${ColCh.Source || "Fill"}|${ColCh.Fill || ""}|${StopsKey}|${Ch.metallic?.Fill ?? 0}|${Ch.roughness?.Fill ?? 0.34}|${Fc}`;
}

// Resample non-transparent silhouette tightly into [Margin .. Target-Margin] with premultiplied-alpha bilinear filtering
function FitSilhouetteRgba(SrcPixels, SrcW, SrcH, DstW, DstH, MarginRatio = 0.06) {
  let MinX = SrcW, MinY = SrcH, MaxX = -1, MaxY = -1;
  for (let Y = 0; Y < SrcH; ++Y) {
    for (let X = 0; X < SrcW; ++X) {
      const A = SrcPixels[(Y * SrcW + X) * 4 + 3];
      if (A > 8) {
        if (X < MinX) MinX = X;
        if (X > MaxX) MaxX = X;
        if (Y < MinY) MinY = Y;
        if (Y > MaxY) MaxY = Y;
      }
    }
  }

  const Out = new Uint8ClampedArray(DstW * DstH * 4);
  if (MaxX < MinX || MaxY < MinY) return Out;

  const BoxW = Math.max(1, MaxX - MinX + 1);
  const BoxH = Math.max(1, MaxY - MinY + 1);
  const PadX = DstW * MarginRatio;
  const PadY = DstH * MarginRatio;
  const AvailW = Math.max(2, DstW - 2 * PadX);
  const AvailH = Math.max(2, DstH - 2 * PadY);
  const FitScale = Math.min(AvailW / BoxW, AvailH / BoxH);

  const DrawW = BoxW * FitScale;
  const DrawH = BoxH * FitScale;
  const DstLeft = (DstW - DrawW) * 0.5;
  const DstTop = (DstH - DrawH) * 0.5;

  for (let Dy = 0; Dy < DstH; ++Dy) {
    const Sy = MinY + ((Dy + 0.5 - DstTop) / FitScale) - 0.5;
    if (Sy < MinY - 1 || Sy > MaxY + 1) continue;
    const Y0 = Math.max(0, Math.min(SrcH - 1, Math.floor(Sy)));
    const Y1 = Math.max(0, Math.min(SrcH - 1, Y0 + 1));
    const Ty = Math.max(0, Math.min(1, Sy - Math.floor(Sy)));

    for (let Dx = 0; Dx < DstW; ++Dx) {
      const Sx = MinX + ((Dx + 0.5 - DstLeft) / FitScale) - 0.5;
      if (Sx < MinX - 1 || Sx > MaxX + 1) continue;
      const X0 = Math.max(0, Math.min(SrcW - 1, Math.floor(Sx)));
      const X1 = Math.max(0, Math.min(SrcW - 1, X0 + 1));
      const Tx = Math.max(0, Math.min(1, Sx - Math.floor(Sx)));

      const W00 = (1 - Tx) * (1 - Ty);
      const W10 = Tx * (1 - Ty);
      const W01 = (1 - Tx) * Ty;
      const W11 = Tx * Ty;

      const I00 = (Y0 * SrcW + X0) * 4;
      const I10 = (Y0 * SrcW + X1) * 4;
      const I01 = (Y1 * SrcW + X0) * 4;
      const I11 = (Y1 * SrcW + X1) * 4;

      const A00 = SrcPixels[I00 + 3] * W00;
      const A10 = SrcPixels[I10 + 3] * W10;
      const A01 = SrcPixels[I01 + 3] * W01;
      const A11 = SrcPixels[I11 + 3] * W11;
      const Alpha = A00 + A10 + A01 + A11;
      if (Alpha < 1.0) continue;

      const InvA = 1.0 / Alpha;
      const R = (SrcPixels[I00] * A00 + SrcPixels[I10] * A10 + SrcPixels[I01] * A01 + SrcPixels[I11] * A11) * InvA;
      const G = (SrcPixels[I00 + 1] * A00 + SrcPixels[I10 + 1] * A10 + SrcPixels[I01 + 1] * A01 + SrcPixels[I11 + 1] * A11) * InvA;
      const B = (SrcPixels[I00 + 2] * A00 + SrcPixels[I10 + 2] * A10 + SrcPixels[I01 + 2] * A01 + SrcPixels[I11 + 2] * A11) * InvA;

      const DstIdx = (Dy * DstW + Dx) * 4;
      Out[DstIdx] = Math.round(R);
      Out[DstIdx + 1] = Math.round(G);
      Out[DstIdx + 2] = Math.round(B);
      Out[DstIdx + 3] = Math.min(255, Math.round(Alpha));
    }
  }
  return Out;
}

// Pure-JS / Node-safe RGBA buffer render (100% transparent background + tight camera auto-fit)
export function RenderAnalyticalThumbnailRgba({
  PropId = null,
  Shape = "cone",
  Scale = null,
  Material = null,
  FaceColors = null,
  Width = 96,
  Height = 96,
} = {}) {
  const PropDef = PropId ? GetWorldPropById(PropId) : null;
  const ResolvedMat = Material || (PropDef ? null : ResolveDefaultThumbMaterial(Shape));
  const ResolvedScale = Array.isArray(Scale)
    ? Scale
    : PropDef
      ? PropDef.NominalScale || [1, 1, 1]
      : ResolveDefaultThumbScale(Shape);

  const CacheKey =
    `${Width}x${Height}|` +
    ComputeThumbHashKey({
      PropId: PropDef ? PropDef.PropId : null,
      Shape,
      Scale: ResolvedScale,
      Material: ResolvedMat,
      FaceColors,
    });

  if (ThumbnailRgbaCache.has(CacheKey)) {
    return ThumbnailRgbaCache.get(CacheKey);
  }

  if (!SharedThumbAtlas) SharedThumbAtlas = new MaterialAtlasSynchronizer();
  if (!SharedThumbTracer) SharedThumbTracer = new CpuGtxRenderer();

  const ObjRotation = PropDef
    ? [0, 0, -24]
    : Shape === "torus"
      ? [32, -16, 12]
      : Shape === "box"
        ? [0, 0, 34]
        : [0, 0, 0];

  const HeroPanel = PropDef ? PropDef.Panel || "geometry" : "geometry";
  const ThumbRows = [
    {
      Id: "thumb-hero",
      Name: "Thumbnail Specimen",
      Panel: HeroPanel,
      PropId: PropDef ? PropDef.PropId : undefined,
      PrimitiveShape: PropDef ? undefined : Shape,
      Preview: true,
    },
    { Id: "camera", Name: "Thumb Camera", Panel: "camera" },
    { Id: "post", Name: "Thumb Post", Panel: "post" },
    { Id: "sun", Name: "Thumb Key", Panel: "sun", Preview: true },
    { Id: "key-light", Name: "Thumb Softbox", Panel: "light", Preview: true },
  ];

  const HeroValues = {
    Position: [0, 0, 0],
    Rotation: ObjRotation,
    Scale: ResolvedScale,
    "Cast shadows": true,
    GI: true,
  };
  if (ResolvedMat) HeroValues.Material = ResolvedMat;
  if (HasActiveFaceColors(FaceColors)) HeroValues.FaceColors = FaceColors;

  const ThumbValues = {
    "thumb-hero": HeroValues,
    camera: {
      Position: [-1.0, -1.5, 0.65],
      Target: [0, 0, 0],
      "Focal Length": 45,
      "Sensor Width": 36,
    },
    post: { "EV Compensation": 0.28 },
    sun: {
      Direction: [-0.48, -0.62, 0.62],
      SolarIrradiance: [5.2, 4.95, 4.55],
      "Angular Diameter": 2.2,
      SkyRadiance: [0.30, 0.35, 0.44],
    },
    "key-light": {
      Position: [0.65, -0.75, 2.2],
      Intensity: 9.2,
      Scale: [2.2, 1.8, 1.0],
      EmittedRadiance: [9.2, 8.8, 8.2],
    },
  };

  const Compiled = CompileLiveGtxScene(ThumbRows, ThumbValues, {}, "PERSP");
  for (const L of Compiled.AreaLights) {
    L.QuadVisible = false;
  }

  // Tight auto-fit camera framing to exact world-space AABB of all sub-parts
  const Bounds = ComputePrimitivesTightBounds(Compiled.Primitives);
  const ViewOffsetDir = PropDef?.Category === "Cameras"
    ? [-0.64, -0.68, 0.36]
    : [-0.54, -0.76, 0.37];
  const DirLen = Math.hypot(...ViewOffsetDir);
  const Vx = ViewOffsetDir[0] / DirLen;
  const Vy = ViewOffsetDir[1] / DirLen;
  const Vz = ViewOffsetDir[2] / DirLen;

  const TanHalf = Math.tan(Compiled.VerticalFovRad * 0.5);
  const Dist = Math.max(0.28, (Bounds.Radius * 1.18) / Math.max(0.18, TanHalf));
  Compiled.CameraTarget = [...Bounds.Center];
  Compiled.CameraOrigin = [
    Bounds.Center[0] + Vx * Dist,
    Bounds.Center[1] + Vy * Dist,
    Bounds.Center[2] + Vz * Dist,
  ];

  SharedThumbAtlas.InvalidateAll();
  SharedThumbAtlas.UpdatePrimitives(Compiled.Primitives);

  const SuperW = Math.min(168, Math.max(72, Math.round(Width * 1.45)));
  const SuperH = Math.min(168, Math.max(72, Math.round(Height * 1.45)));

  SharedThumbTracer.Invalidate();
  SharedThumbTracer.RenderSlice(
    {
      CompiledScene: Compiled,
      AtlasPixels: SharedThumbAtlas.Pixels,
      Width: SuperW,
      Height: SuperH,
      ViewModeCode: 0,
      ShowGrid: 0,
      TransparentBackground: true,
    },
    SuperH,
  );

  const RawSuper = SharedThumbTracer.OffscreenImg.data;
  const Pixels = FitSilhouetteRgba(RawSuper, SuperW, SuperH, Width, Height, 0.055);
  const Result = { Width, Height, Pixels };
  ThumbnailRgbaCache.set(CacheKey, Result);
  return Result;
}

export function RenderAnalyticalThumbnailDataUrl({
  PropId = null,
  Shape = "cone",
  Scale = null,
  Material = null,
  FaceColors = null,
  Size = 96,
} = {}) {
  const PropDef = PropId ? GetWorldPropById(PropId) : null;
  const ResolvedMat = Material || (PropDef ? null : ResolveDefaultThumbMaterial(Shape));
  const ResolvedScale = Array.isArray(Scale)
    ? Scale
    : PropDef
      ? PropDef.NominalScale || [1, 1, 1]
      : ResolveDefaultThumbScale(Shape);
  const UrlKey =
    `${Size}|` +
    ComputeThumbHashKey({
      PropId: PropDef ? PropDef.PropId : null,
      Shape,
      Scale: ResolvedScale,
      Material: ResolvedMat,
      FaceColors,
    });

  if (ThumbnailUrlCache.has(UrlKey)) {
    return ThumbnailUrlCache.get(UrlKey);
  }

  if (typeof document === "undefined") return "";

  const DstCanvas = document.createElement("canvas");
  const DstCtx = DstCanvas.getContext("2d");
  if (!DstCtx) {
    ThumbnailUrlCache.set(UrlKey, "");
    return "";
  }
  DstCanvas.width = Size;
  DstCanvas.height = Size;

  const Raw = RenderAnalyticalThumbnailRgba({
    PropId: PropDef ? PropDef.PropId : null,
    Shape,
    Scale: ResolvedScale,
    Material: ResolvedMat,
    FaceColors,
    Width: Size,
    Height: Size,
  });

  const ImgData = DstCtx.createImageData(Size, Size);
  ImgData.data.set(Raw.Pixels);
  DstCtx.putImageData(ImgData, 0, 0);

  const DataUrl = DstCanvas.toDataURL("image/png");
  ThumbnailUrlCache.set(UrlKey, DataUrl);
  return DataUrl;
}

export function ClearAnalyticalThumbnailCache() {
  ThumbnailUrlCache.clear();
  ThumbnailRgbaCache.clear();
}
