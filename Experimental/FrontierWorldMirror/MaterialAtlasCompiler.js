//==========================================================================================================================================
//                                                  MATERIALATLASCOMPILER.JS
//==========================================================================================================================================
// Compiles all 20 channels of MaterialPanel / MaterialSpecification.js (Fill, Gradient, Texture, and Type presets)
// into a 1024 x 768 RGBA8 WebGL2 texture atlas (16 x 12 grid of 64 x 64 UV tiles = 8 tiles per primitive across 24 primitives).
//
// Per-primitive 8-tile layout (tileSize = 64 px, 1 px inner clamp margin -> [1..62] UV interior to prevent bleed):
//   Slot 0: RGB = Base colour (linear),          A = Opacity [0..1]
//   Slot 1: R   = Metallic [0..1],               G = Roughness [0.02..1], B = (IOR - 1.0) / 1.5, A = Occlusion [0..1]
//   Slot 2: RGB = Tangent Normal [0..1],         A = (Displacement + 0.1) / 0.2
//   Slot 3: R   = (Anisotropy + 1) * 0.5,        G = Direction / (2 * PI), B = Coat [0..1],      A = CoatRoughness [0.02..1]
//   Slot 4: RGB = Sheen colour (linear),         A = SheenRoughness [0.04..1]
//   Slot 5: RGB = Subsurface colour (linear),    A = clamp(Thickness / 2.0, 0, 1)
//   Slot 6: RGB = Coat Tangent Normal [0..1],    A = Transmission [0..1]
//   Slot 7: R   = clamp(Emission / 20.0, 0, 1),  G = (Type === "Unlit" ? 1 : 0), B = DiffuseRoughness, A = 1.0

import {
  Clamp,
  GradientValue,
  MaterialChannels,
  ResolveMaterial,
  Rgb,
} from "./MaterialSpecification.js";
import { HasActiveFaceColors, NormalizeFaceSpec } from "./WorldPropLibrary.js";

export const ATLAS_TILE_SIZE = 64;
export const ATLAS_COLS = 32; // 4 primitives per row (8 tiles each)
export const MAX_ATLAS_PRIMS = 384;
export const ATLAS_ROWS = 96; // 96 rows * 4 = 384 primitives (2048 x 6144 <= 8192 WebGPU limit!)
export const ATLAS_WIDTH = ATLAS_COLS * ATLAS_TILE_SIZE;  // 2048
export const ATLAS_HEIGHT = ATLAS_ROWS * ATLAS_TILE_SIZE; // 6144
export const TILES_PER_PRIM = 8;

const FIELD_MAP = Object.fromEntries(
  MaterialChannels.map((Field) => [Field.Id, Field]),
);

const DecodedTextureCache = new Map();
const PendingDecodes = new Set();

function SrgbToLinear(S) {
  const C = Math.max(0, Math.min(1, Number(S) || 0));
  return C <= 0.04045 ? C / 12.92 : Math.pow((C + 0.055) / 1.055, 2.4);
}

export function EnsureMaterialTexturesDecoded(Primitives, OnTextureReady) {
  if (typeof window === "undefined" || typeof document === "undefined") return;
  for (const Prim of Primitives) {
    const Channels = Prim.ResolvedMaterial?.Channels;
    if (!Channels) continue;
    for (const Channel of Object.values(Channels)) {
      const DataUrl = Channel?.Texture?.Data;
      if (
        Channel?.Source !== "Texture" ||
        !DataUrl ||
        DecodedTextureCache.has(DataUrl) ||
        PendingDecodes.has(DataUrl)
      ) {
        continue;
      }
      PendingDecodes.add(DataUrl);
      const Img = new window.Image();
      Img.src = DataUrl;
      Img.decode()
        .then(() => {
          const Off = document.createElement("canvas");
          Off.width = 128;
          Off.height = 128;
          const Ctx = Off.getContext("2d");
          Ctx.drawImage(Img, 0, 0, 128, 128);
          DecodedTextureCache.set(
            DataUrl,
            Ctx.getImageData(0, 0, 128, 128).data,
          );
          PendingDecodes.delete(DataUrl);
          if (OnTextureReady) OnTextureReady();
        })
        .catch(() => {
          PendingDecodes.delete(DataUrl);
        });
    }
  }
}

function SampleChannel(Material, Id, U, V) {
  const Channel = Material.Channels[Id];
  const Field = FIELD_MAP[Id];
  const IsColour = Field.Kind === "Colour";
  const Source = Channel.Source;

  if (Source === "Texture" && Channel.Texture?.Data) {
    const TexData = DecodedTextureCache.get(Channel.Texture.Data);
    if (TexData) {
      const Rep = Channel.Repeat || 1;
      const Tu = (((U * Rep) % 1) + 1) % 1;
      const Tv = (((V * Rep) % 1) + 1) % 1;
      const X = Math.min(127, Math.floor(Tu * 128));
      const Y = Math.min(127, Math.floor((1 - Tv) * 128));
      const P = (Y * 128 + X) * 4;
      const CompIdx = { R: 0, G: 1, B: 2, A: 3 }[Channel.Component];
      if (IsColour) {
        if (CompIdx === undefined) {
          return [TexData[P] / 255, TexData[P + 1] / 255, TexData[P + 2] / 255];
        }
        const Vc = TexData[P + CompIdx] / 255;
        return [Vc, Vc, Vc];
      }
      const Norm = TexData[P + (CompIdx ?? 0)] / 255;
      return Field.Minimum + Norm * (Field.Maximum - Field.Minimum);
    }
  }

  if (Source === "Gradient") {
    const Coord = Channel.Axis === "V" ? V : U;
    const Val = GradientValue(Channel, Coord, IsColour);
    return IsColour ? Rgb(Val) : +Val;
  }

  return IsColour ? Rgb(Channel.Fill) : +Channel.Fill;
}

function EvaluateFacePatternColor(Spec, Fu, Fv) {
  const BaseRgb = Rgb(Spec.Fill || "#c8c6c2");
  const AccRgb = Rgb(Spec.Accent || "#f8fafc");
  let Out = BaseRgb;
  let Rough = Spec.Roughness ?? 0.42;
  let Metal = Spec.Metallic ?? 0.0;
  let Emit = Spec.Emission ?? 0.0;

  const Pat = Spec.Pattern;
  if (Pat === "chevrons") {
    const Diag = ((Fu * 5.0 + Fv * 2.5) % 1.0 + 1.0) % 1.0;
    if (Diag > 0.5) Out = AccRgb;
  } else if (Pat === "stripes") {
    const Band = ((Fu * 6.0) % 1.0 + 1.0) % 1.0;
    if (Band > 0.5) Out = AccRgb;
  } else if (Pat === "led_grid") {
    const Border = Fu < 0.06 || Fu > 0.94 || Fv < 0.08 || Fv > 0.92;
    if (Border) {
      Out = [0.08, 0.09, 0.11];
      Emit = 0.0;
      Rough = 0.4;
      Metal = 0.7;
    } else {
      const CellX = Math.abs(((Fu * 10) % 1) - 0.5);
      const CellY = Math.abs(((Fv * 8) % 1) - 0.5);
      const IsDiode = CellX < 0.34 && CellY < 0.34;
      Out = IsDiode ? (((Math.floor(Fu * 10) + Math.floor(Fv * 8)) & 1) ? AccRgb : BaseRgb) : BaseRgb.map((C) => C * 0.78);
    }
  } else if (Pat === "oled_telemetry") {
    const InScreen = Fu > 0.14 && Fu < 0.86 && Fv > 0.22 && Fv < 0.82;
    if (!InScreen) {
      Out = [0.11, 0.13, 0.17];
      Emit = 0.0;
      Rough = 0.38;
      Metal = 0.65;
    } else {
      const Bar = Fv > 0.62 && Fv < 0.72 || (Fv > 0.36 && Fv < 0.44 && Fu < 0.62);
      Out = Bar ? AccRgb : BaseRgb;
      Emit = Bar ? Math.max(2.2, Emit * 1.35) : Math.max(0.6, Emit * 0.55);
    }
  } else if (Pat === "drawer_front") {
    const Row = (Fv * 6.0) % 1.0;
    if (Row < 0.09) {
      Out = [0.09, 0.10, 0.12];
      Rough = 0.6;
    } else if (Row > 0.70 && Row < 0.84 && Fu > 0.12 && Fu < 0.88) {
      Out = AccRgb;
      Metal = 0.92;
      Rough = 0.16;
    }
  } else if (Pat === "brake_100m") {
    const Border = Fu < 0.07 || Fu > 0.93 || Fv < 0.08 || Fv > 0.92;
    const TopBanner = Fv > 0.72;
    const Digit1 = Fu > 0.16 && Fu < 0.25 && Fv > 0.22 && Fv < 0.64;
    const Digit0A =
      Fu > 0.33 && Fu < 0.55 && Fv > 0.22 && Fv < 0.64 &&
      !(Fu > 0.40 && Fu < 0.48 && Fv > 0.30 && Fv < 0.56);
    const Digit0B =
      Fu > 0.61 && Fu < 0.83 && Fv > 0.22 && Fv < 0.64 &&
      !(Fu > 0.68 && Fu < 0.76 && Fv > 0.30 && Fv < 0.56);
    if (Border || TopBanner) {
      Out = AccRgb;
    } else if (Digit1 || Digit0A || Digit0B) {
      Out = [0.08, 0.09, 0.12];
    }
  } else if (Pat === "tire_sidewall") {
    const Dx = Fu - 0.5, Dy = Fv - 0.5;
    const R = Math.hypot(Dx, Dy) * 2.0;
    if (R < 0.18) {
      Out = AccRgb;
      Metal = 0.75;
      Rough = 0.24;
    } else if (R < 0.56) {
      Out = [0.24, 0.27, 0.32];
      Metal = 0.85;
      Rough = 0.26;
    } else if (R > 0.68 && R < 0.78 && Math.abs(Dx) > 0.18) {
      Out = AccRgb;
      Rough = 0.45;
    }
  }

  return { Out, Rough, Metal, Emit };
}

function MaterialSignature(Prim) {
  const Mat = Prim.ResolvedMaterial;
  if (!Mat) return "";
  let Sig = Mat.Type + "|" + (Prim.DiffuseRoughness ?? 0.5);
  if (HasActiveFaceColors(Prim.FaceColors)) {
    Sig += "|FC:" + JSON.stringify(Prim.FaceColors);
  }
  for (const Field of MaterialChannels) {
    const Ch = Mat.Channels[Field.Id];
    Sig +=
      ";" +
      Field.Id +
      ":" +
      Ch.Source +
      ":" +
      Ch.Fill +
      ":" +
      Ch.Axis +
      ":" +
      Ch.Repeat +
      ":" +
      Ch.Component;
    if (Ch.Source === "Gradient") {
      for (const S of Ch.Stops) Sig += `@${S.Position}=${S.Value}`;
    } else if (Ch.Source === "Texture" && Ch.Texture?.Data) {
      const Ready = DecodedTextureCache.has(Ch.Texture.Data) ? "1" : "0";
      Sig += `#${Ch.Texture.Data.length}_${Ch.Texture.Data.slice(-24)}_${Ready}`;
    }
  }
  return Sig;
}

function IsAllFillConstant(Material) {
  for (const Field of MaterialChannels) {
    if (Material.Channels[Field.Id].Source !== "Fill") return false;
  }
  return true;
}

function WriteSolidTile(Buffer, TileCol, TileRow, R, G, B, A) {
  const BaseX = TileCol * ATLAS_TILE_SIZE;
  const BaseY = TileRow * ATLAS_TILE_SIZE;
  for (let Y = 0; Y < ATLAS_TILE_SIZE; ++Y) {
    let RowOff = ((BaseY + Y) * ATLAS_WIDTH + BaseX) * 4;
    for (let X = 0; X < ATLAS_TILE_SIZE; ++X) {
      Buffer[RowOff] = R;
      Buffer[RowOff + 1] = G;
      Buffer[RowOff + 2] = B;
      Buffer[RowOff + 3] = A;
      RowOff += 4;
    }
  }
}

const Byte = (V) => Math.max(0, Math.min(255, Math.round(V * 255)));

export class MaterialAtlasSynchronizer {
  constructor() {
    this.Pixels = new Uint8Array(ATLAS_WIDTH * ATLAS_HEIGHT * 4);
    this.CachedSignatures = new Array(MAX_ATLAS_PRIMS).fill("");
    this.Dirty = true;
  }

  InvalidateAll() {
    this.CachedSignatures.fill("");
    this.Dirty = true;
  }

  UpdatePrimitives(Primitives) {
    let Changed = false;
    const Count = Math.min(MAX_ATLAS_PRIMS, Primitives.length);
    for (let PrimIdx = 0; PrimIdx < Count; ++PrimIdx) {
      const Prim = Primitives[PrimIdx];
      const Sig = MaterialSignature(Prim);
      if (this.CachedSignatures[PrimIdx] === Sig) continue;
      this.CachedSignatures[PrimIdx] = Sig;
      this.BakePrimitiveTiles(PrimIdx, Prim);
      Changed = true;
    }
    if (Changed) this.Dirty = true;
    return this.Dirty;
  }

  BakePrimitiveTiles(PrimIdx, Prim) {
    const Mat = Prim.ResolvedMaterial || ResolveMaterial({});
    const BaseTileIndex = PrimIdx * TILES_PER_PRIM;
    const UnlitBit = Mat.Type === "Unlit" ? 255 : 0;
    const DiffRoughByte = Byte(Clamp(Prim.DiffuseRoughness ?? 0.5, 0, 1));
    const UseFaceColors = HasActiveFaceColors(Prim.FaceColors);

    const EncodeAt = (U, V, Out8) => {
      const Col = SampleChannel(Mat, "colour", U, V);
      const Opacity = Clamp(SampleChannel(Mat, "opacity", U, V), 0, 1);
      const Metallic = Clamp(SampleChannel(Mat, "metallic", U, V), 0, 1);
      const Roughness = Clamp(SampleChannel(Mat, "roughness", U, V), 0.02, 1);
      const Ior = Clamp(SampleChannel(Mat, "ior", U, V), 1.0, 2.5);
      const Occlusion = Clamp(SampleChannel(Mat, "occlusion", U, V), 0, 1);
      const NormRgb = SampleChannel(Mat, "normal", U, V);
      const Disp = Clamp(SampleChannel(Mat, "displacement", U, V), -0.1, 0.1);
      const Aniso = Clamp(SampleChannel(Mat, "anisotropy", U, V), -1, 1);
      const AnisoDir = Clamp(SampleChannel(Mat, "direction", U, V), 0, 6.2831853);
      const Coat = Clamp(SampleChannel(Mat, "coat", U, V), 0, 1);
      const CoatRough = Clamp(SampleChannel(Mat, "coatRoughness", U, V), 0.02, 1);
      const SheenSrgb = SampleChannel(Mat, "sheen", U, V);
      const SheenRough = Clamp(SampleChannel(Mat, "sheenRoughness", U, V), 0.04, 1);
      const SubsurfCh = Mat.Channels.subsurface;
      const SubsurfaceActive =
        Mat.Type === "Subsurface" ||
        (SubsurfCh &&
          (SubsurfCh.Source !== "Fill" ||
            String(SubsurfCh.Fill || "#cccccc").toLowerCase() !== "#cccccc"));
      const SubsurfSrgb = SubsurfaceActive
        ? SampleChannel(Mat, "subsurface", U, V)
        : [0, 0, 0];
      const Thickness = Clamp(SampleChannel(Mat, "thickness", U, V), 0, 2);
      const CoatNormRgb = SampleChannel(Mat, "coatNormal", U, V);
      const Transmission =
        Mat.Type === "Glass"
          ? Math.max(0.85, Clamp(SampleChannel(Mat, "transmission", U, V), 0, 1))
          : Clamp(SampleChannel(Mat, "transmission", U, V), 0, 1);
      const Emission = Clamp(SampleChannel(Mat, "emission", U, V), 0, 20);

      // Slot 0: Linear BaseColour + Opacity
      Out8[0] = Byte(SrgbToLinear(Col[0]));
      Out8[1] = Byte(SrgbToLinear(Col[1]));
      Out8[2] = Byte(SrgbToLinear(Col[2]));
      Out8[3] = Byte(Opacity);

      // Slot 1: Metallic, Roughness, Normalized IOR, Occlusion
      Out8[4] = Byte(Metallic);
      Out8[5] = Byte(Roughness);
      Out8[6] = Byte((Ior - 1.0) / 1.5);
      Out8[7] = Byte(Occlusion);

      // Slot 2: Tangent Normal + Normalized Displacement
      Out8[8] = Byte(NormRgb[0]);
      Out8[9] = Byte(NormRgb[1]);
      Out8[10] = Byte(NormRgb[2]);
      Out8[11] = Byte((Disp + 0.1) / 0.2);

      // Slot 3: Anisotropy, AnisoDirection, ClearCoat, CoatRoughness
      Out8[12] = Byte((Aniso + 1.0) * 0.5);
      Out8[13] = Byte(AnisoDir / 6.2831853);
      Out8[14] = Byte(Coat);
      Out8[15] = Byte(CoatRough);

      // Slot 4: Linear Sheen + SheenRoughness
      Out8[16] = Byte(SrgbToLinear(SheenSrgb[0]));
      Out8[17] = Byte(SrgbToLinear(SheenSrgb[1]));
      Out8[18] = Byte(SrgbToLinear(SheenSrgb[2]));
      Out8[19] = Byte(SheenRough);

      // Slot 5: Linear Subsurface + Normalized Thickness
      Out8[20] = Byte(SrgbToLinear(SubsurfSrgb[0]));
      Out8[21] = Byte(SrgbToLinear(SubsurfSrgb[1]));
      Out8[22] = Byte(SrgbToLinear(SubsurfSrgb[2]));
      Out8[23] = Byte(Thickness / 2.0);

      // Slot 6: Coat Tangent Normal + Transmission
      Out8[24] = Byte(CoatNormRgb[0]);
      Out8[25] = Byte(CoatNormRgb[1]);
      Out8[26] = Byte(CoatNormRgb[2]);
      Out8[27] = Byte(Transmission);

      // Slot 7: Normalized Emission (0..20 nit), Unlit flag, DiffuseRoughness
      Out8[28] = Byte(Emission / 20.0);
      Out8[29] = UnlitBit;
      Out8[30] = DiffRoughByte;
      Out8[31] = 255;
    };

    const Scratch = new Uint8Array(32);

    // Per-Face Individual Colour & Pattern Atlas (3x2 face cells inside 64x64 tile: 0=-X, 1=+X, 2=-Y, 3=+Y, 4=-Z, 5=+Z)
    if (UseFaceColors) {
      const FallbackHex = Mat.Channels.colour?.Fill || "#c8c6c2";
      const FallbackRough = Clamp(Number(Mat.Channels.roughness?.Fill ?? 0.42), 0.02, 1);
      const FallbackMetal = Clamp(Number(Mat.Channels.metallic?.Fill ?? 0.0), 0, 1);
      const Specs = [0, 1, 2, 3, 4, 5].map((Fi) =>
        NormalizeFaceSpec(Prim.FaceColors, Fi, FallbackHex, FallbackRough, FallbackMetal),
      );
      EncodeAt(0.5, 0.5, Scratch);
      for (let Py = 0; Py < ATLAS_TILE_SIZE; ++Py) {
        const Tv = Math.max(0, Math.min(0.9999, (Py - 0.5) / (ATLAS_TILE_SIZE - 2)));
        const CellRow = Tv < 0.5 ? 0 : 1;
        const RawFv = Tv * 2.0 - CellRow;
        const Fv = Math.max(0, Math.min(1, (RawFv - 0.08) / 0.84));
        for (let Px = 0; Px < ATLAS_TILE_SIZE; ++Px) {
          const Tu = Math.max(0, Math.min(0.9999, (Px - 0.5) / (ATLAS_TILE_SIZE - 2)));
          const CellCol = Tu < 1 / 3 ? 0 : Tu < 2 / 3 ? 1 : 2;
          const RawFu = Tu * 3.0 - CellCol;
          const Fu = Math.max(0, Math.min(1, (RawFu - 0.08) / 0.84));
          const FaceIdx = CellRow * 3 + CellCol;
          const Eval = EvaluateFacePatternColor(Specs[FaceIdx], Fu, Fv);
          Scratch[0] = Byte(SrgbToLinear(Eval.Out[0]));
          Scratch[1] = Byte(SrgbToLinear(Eval.Out[1]));
          Scratch[2] = Byte(SrgbToLinear(Eval.Out[2]));
          Scratch[4] = Byte(Clamp(Eval.Metal, 0, 1));
          Scratch[5] = Byte(Clamp(Eval.Rough, 0.02, 1));
          Scratch[28] = Byte(Clamp(Eval.Emit / 20.0, 0, 1));
          Scratch[29] = Eval.Emit > 3.5 ? 255 : UnlitBit;

          for (let Slot = 0; Slot < TILES_PER_PRIM; ++Slot) {
            const TileIdx = BaseTileIndex + Slot;
            const TileCol = TileIdx % ATLAS_COLS;
            const TileRow = Math.floor(TileIdx / ATLAS_COLS);
            const DstIdx =
              ((TileRow * ATLAS_TILE_SIZE + Py) * ATLAS_WIDTH +
                (TileCol * ATLAS_TILE_SIZE + Px)) *
              4;
            const SrcOff = Slot * 4;
            this.Pixels[DstIdx] = Scratch[SrcOff];
            this.Pixels[DstIdx + 1] = Scratch[SrcOff + 1];
            this.Pixels[DstIdx + 2] = Scratch[SrcOff + 2];
            this.Pixels[DstIdx + 3] = Scratch[SrcOff + 3];
          }
        }
      }
      return;
    }

    // Fast path when all 20 channels are constant Fill!
    if (IsAllFillConstant(Mat)) {
      EncodeAt(0.5, 0.5, Scratch);
      for (let Slot = 0; Slot < TILES_PER_PRIM; ++Slot) {
        const TileIdx = BaseTileIndex + Slot;
        const TileCol = TileIdx % ATLAS_COLS;
        const TileRow = Math.floor(TileIdx / ATLAS_COLS);
        const Off = Slot * 4;
        WriteSolidTile(
          this.Pixels,
          TileCol,
          TileRow,
          Scratch[Off],
          Scratch[Off + 1],
          Scratch[Off + 2],
          Scratch[Off + 3],
        );
      }
      return;
    }

    // Full 64 x 64 UV raster bake for Gradient / Texture channels
    for (let Py = 0; Py < ATLAS_TILE_SIZE; ++Py) {
      const V = Math.max(0, Math.min(1, (Py - 0.5) / (ATLAS_TILE_SIZE - 2)));
      for (let Px = 0; Px < ATLAS_TILE_SIZE; ++Px) {
        const U = Math.max(0, Math.min(1, (Px - 0.5) / (ATLAS_TILE_SIZE - 2)));
        EncodeAt(U, V, Scratch);
        for (let Slot = 0; Slot < TILES_PER_PRIM; ++Slot) {
          const TileIdx = BaseTileIndex + Slot;
          const TileCol = TileIdx % ATLAS_COLS;
          const TileRow = Math.floor(TileIdx / ATLAS_COLS);
          const DstIdx =
            ((TileRow * ATLAS_TILE_SIZE + Py) * ATLAS_WIDTH +
              (TileCol * ATLAS_TILE_SIZE + Px)) *
            4;
          const SrcOff = Slot * 4;
          this.Pixels[DstIdx] = Scratch[SrcOff];
          this.Pixels[DstIdx + 1] = Scratch[SrcOff + 1];
          this.Pixels[DstIdx + 2] = Scratch[SrcOff + 2];
          this.Pixels[DstIdx + 3] = Scratch[SrcOff + 3];
        }
      }
    }
  }
}
