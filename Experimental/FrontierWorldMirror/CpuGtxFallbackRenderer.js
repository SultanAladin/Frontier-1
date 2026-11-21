//==========================================================================================================================================
// FILE: CpuGtxFallbackRenderer.js
// PURPOSE: Zero-Main-Thread Multi-Worker Atomic Double-Buffered Tier 1 GTX + Depth-Aware 2px Silhouette Outline + 13-Handle Slate GizmoPRO.
//          1:1 analytical port of GTX_FRAGMENT_SHADER (C1-smooth solid-angle GI/AO, soft-cone penumbra, 20-channel Material Atlas,
//          roughness-gated reflections, Crown-Glass Abbe dispersion, and Z-sorted Slate GizmoPRO).
//          NEVER blits partial scanlines: background Web Workers render horizontal bands in parallel and swap ONLY complete atomic frames.
//==========================================================================================================================================

import {
  ATLAS_WIDTH,
  ATLAS_COLS,
  ATLAS_TILE_SIZE,
  TILES_PER_PRIM,
} from "./MaterialAtlasCompiler.js";
import { BuildVehicleTlasAndInstances } from "./VehicleBvhAssets.js";
import {
  BuildSkyViewLut,
  GetSkyViewCacheKey,
  SKY_VIEW_HEIGHT,
  SKY_VIEW_WIDTH,
} from "./AtmosphereSky.js";

//==========================================================================================================================================
// HERMETIC BAND KERNEL FACTORY (Zero free outer variables -> 100% safe for Blob Web Worker serialization even under esbuild --minify!)
//==========================================================================================================================================
export function CreateGtxBandKernel(
  AtlasWidth,
  AtlasCols,
  AtlasTileSize,
  TilesPerPrim,
  SkyLutW = 96,
  SkyLutH = 48,
) {
  const PI = Math.PI;

  function Clamp(V, Lo, Hi) {
    return V < Lo ? Lo : V > Hi ? Hi : V;
  }

  function Mix(A, B, T) {
    return A + (B - A) * T;
  }

  function Smoothstep(E0, E1, X) {
    const Den = E1 - E0;
    const T = Clamp((X - E0) / (Math.abs(Den) < 1e-6 ? 1e-6 : Den), 0.0, 1.0);
    return T * T * (3.0 - 2.0 * T);
  }

  function Fract(X) {
    return X - Math.floor(X);
  }

  function Len2(X, Y) {
    return Math.sqrt(X * X + Y * Y);
  }

  function Len3(X, Y, Z) {
    return Math.sqrt(X * X + Y * Y + Z * Z);
  }

  function AcesFilmChannel(X) {
    return Clamp((X * (2.51 * X + 0.03)) / (X * (2.43 * X + 0.59) + 0.14), 0.0, 1.0);
  }

  function Hash12(Px, Py) {
    let X = Fract(Px * 0.1031);
    let Y = Fract(Py * 0.1031);
    let Z = X;
    const D = X * (Y + 33.33) + Y * (Z + 33.33) + Z * (X + 33.33);
    X += D; Y += D; Z += D;
    return Fract((X + Y) * Z);
  }

  function FresnelDielectric(CosThetaI, Ior) {
    const R0Sqrt = (1.0 - Ior) / (1.0 + Ior);
    const R0 = R0Sqrt * R0Sqrt;
    const C = 1.0 - Clamp(CosThetaI, 0.0, 1.0);
    const C2 = C * C;
    return R0 + (1.0 - R0) * C2 * C2 * C;
  }

  function SampleAtlasSlot(AtlasPixels, PrimIdx, Slot, U, V, Out4) {
    const TileIdx = PrimIdx * TilesPerPrim + Slot;
    const Col = TileIdx % AtlasCols;
    const Row = (TileIdx / AtlasCols) | 0;
    const BaseX = Col * AtlasTileSize;
    const BaseY = Row * AtlasTileSize;

    const Cu = Clamp(U, 0.0, 1.0);
    const Cv = Clamp(V, 0.0, 1.0);
    const Fx = 0.5 + Cu * (AtlasTileSize - 2.0);
    const Fy = 0.5 + Cv * (AtlasTileSize - 2.0);
    const X0 = Clamp(Fx | 0, 0, AtlasTileSize - 1);
    const Y0 = Clamp(Fy | 0, 0, AtlasTileSize - 1);
    const X1 = X0 + 1 < AtlasTileSize ? X0 + 1 : AtlasTileSize - 1;
    const Y1 = Y0 + 1 < AtlasTileSize ? Y0 + 1 : AtlasTileSize - 1;
    const Tx = Fx - X0;
    const Ty = Fy - Y0;

    const I00 = ((BaseY + Y0) * AtlasWidth + (BaseX + X0)) * 4;
    const I10 = ((BaseY + Y0) * AtlasWidth + (BaseX + X1)) * 4;
    const I01 = ((BaseY + Y1) * AtlasWidth + (BaseX + X0)) * 4;
    const I11 = ((BaseY + Y1) * AtlasWidth + (BaseX + X1)) * 4;

    const W00 = (1 - Tx) * (1 - Ty) * (1 / 255);
    const W10 = Tx * (1 - Ty) * (1 / 255);
    const W01 = (1 - Tx) * Ty * (1 / 255);
    const W11 = Tx * Ty * (1 / 255);

    Out4[0] = AtlasPixels[I00] * W00 + AtlasPixels[I10] * W10 + AtlasPixels[I01] * W01 + AtlasPixels[I11] * W11;
    Out4[1] = AtlasPixels[I00 + 1] * W00 + AtlasPixels[I10 + 1] * W10 + AtlasPixels[I01 + 1] * W01 + AtlasPixels[I11 + 1] * W11;
    Out4[2] = AtlasPixels[I00 + 2] * W00 + AtlasPixels[I10 + 2] * W10 + AtlasPixels[I01 + 2] * W01 + AtlasPixels[I11 + 2] * W11;
    Out4[3] = AtlasPixels[I00 + 3] * W00 + AtlasPixels[I10 + 3] * W10 + AtlasPixels[I01 + 3] * W01 + AtlasPixels[I11 + 3] * W11;
  }

  function IsPrimAtlasUniform(AtlasPixels, PrimIdx) {
    if (!AtlasPixels) return false;
    for (let Slot = 0; Slot < TilesPerPrim; ++Slot) {
      const TileIdx = PrimIdx * TilesPerPrim + Slot;
      const BaseX = (TileIdx % AtlasCols) * AtlasTileSize;
      const BaseY = ((TileIdx / AtlasCols) | 0) * AtlasTileSize;
      const Ref = ((BaseY + 2) * AtlasWidth + (BaseX + 2)) * 4;
      const R0 = AtlasPixels[Ref], G0 = AtlasPixels[Ref + 1], B0 = AtlasPixels[Ref + 2], A0 = AtlasPixels[Ref + 3];
      for (let K = 10; K <= 58; K += 12) {
        const C1 = ((BaseY + K) * AtlasWidth + (BaseX + K)) * 4;
        const C2 = ((BaseY + K) * AtlasWidth + (BaseX + (62 - K))) * 4;
        if (
          AtlasPixels[C1] !== R0 || AtlasPixels[C1 + 1] !== G0 ||
          AtlasPixels[C1 + 2] !== B0 || AtlasPixels[C1 + 3] !== A0 ||
          AtlasPixels[C2] !== R0 || AtlasPixels[C2 + 1] !== G0 ||
          AtlasPixels[C2 + 2] !== B0 || AtlasPixels[C2 + 3] !== A0
        ) {
          return false;
        }
      }
    }
    return true;
  }

  class GtxBandEngine {
    constructor() {
      this._Tmp4 = new Float32Array(4);
      this._GiOut = [0, 0, 0, 1];
      this._TlasStack = new Int32Array(48);
      this._BlasStack = new Int32Array(48);
      this._Hit = {
        hit: false, t: 50, primIdx: -1,
        isVehicle: false, vehMatId: 6, vehPr: 0.65, vehPg: 0.06, vehPb: 0.08,
        vehRough: 0.22, vehCoat: 1.0, vehPickCode: 0,
        px: 0, py: 0, pz: 0,
        nx: 0, ny: 0, nz: 1,
        tx: 1, ty: 0, tz: 0,
        u: 0.5, v: 0.5,
      };
      this._SecHit = { ...this._Hit };
      this._Mat = {
        baseR: 0.8, baseG: 0.8, baseB: 0.8, opacity: 1,
        metallic: 0, roughness: 0.5, ior: 1.5, ao: 1,
        normX: 0, normY: 0, normZ: 1,
        anisotropy: 0, anisoRot: 0, coat: 0, coatRoughness: 0.1,
        sheenR: 0, sheenG: 0, sheenB: 0, sheenRoughness: 0.3,
        subR: 0, subG: 0, subB: 0, thickness: 0,
        coatNormX: 0, coatNormY: 0, coatNormZ: 1,
        transmission: 0, emission: 0, diffRoughness: 0, unlit: false,
      };
      this._SecMat = { ...this._Mat };
    }

    PackScene(CompiledScene, AtlasPixels, MaxPrims = 384, MaxLights = 12) {
      const PCount = Math.min(MaxPrims, CompiledScene.Primitives.length);
      const Prims = new Array(PCount);
      for (let I = 0; I < PCount; ++I) {
        const P = CompiledScene.Primitives[I];
        const [Rx, Ry, Rz] = P.RotationRad;
        const Cx = Math.cos(Rx), Sx = Math.sin(Rx);
        const Cy = Math.cos(Ry), Sy = Math.sin(Ry);
        const Cz = Math.cos(Rz), Sz = Math.sin(Rz);
        const DiffW = P.Emission > 0.01 ? 1.0 : (1.0 - P.Metallic * 0.75);
        const Hx = Math.max(1e-4, P.HalfExtents[0]);
        const Hy = Math.max(1e-4, P.HalfExtents[1]);
        const Hz = Math.max(1e-4, P.HalfExtents[2]);
        const BoundR = Len3(Hx, Hy, Hz) * 1.06;
        const IsUniform = !P.FaceAtlas && IsPrimAtlasUniform(AtlasPixels, I);
        let ConstMat = null;
        if (IsUniform && AtlasPixels) {
          ConstMat = { ...this._Mat };
          this.FetchMaterialSlow(AtlasPixels, I, 0.5, 0.5, P.Transmission, ConstMat);
        }
        Prims[I] = {
          Cx: P.Center[0], Cy: P.Center[1], Cz: P.Center[2],
          Shape: P.Shape | 0,
          PickCode: P.PickCode || (1 + I),
          FaceAtlas: !!P.FaceAtlas,
          Hx, Hy, Hz,
          BoundR,
          BoundR2: BoundR * BoundR,
          CastShadows: !!P.CastShadows,
          GiContributor: !!P.GiContributor,
          GlassShell: !!P.GlassShell,
          R0x: Cz * Cy, R0y: Sz * Cy, R0z: -Sy,
          R1x: Cz * Sy * Sx - Sz * Cx, R1y: Sz * Sy * Sx + Cz * Cx, R1z: Cy * Sx,
          R2x: Cz * Sy * Cx + Sz * Sx, R2y: Sz * Sy * Cx - Cz * Sx, R2z: Cy * Cx,
          Abbe: Math.max(14.0, P.Abbe > 1.0 ? P.Abbe : 26.0),
          EdgeR: P.EdgeTintF82[0], EdgeG: P.EdgeTintF82[1], EdgeB: P.EdgeTintF82[2],
          Transmission: P.Transmission,
          BounceR: P.BaseColor[0] * DiffW,
          BounceG: P.BaseColor[1] * DiffW,
          BounceB: P.BaseColor[2] * DiffW,
          Emission: P.Emission,
          ConstMat,
        };
      }

      const LCount = Math.min(MaxLights, CompiledScene.AreaLights.length);
      const Lights = new Array(LCount);
      for (let I = 0; I < LCount; ++I) {
        const L = CompiledScene.AreaLights[I];
        const Right = L.Right || [1, 0, 0];
        const Up = L.Up || [0, 1, 0];
        const Normal = L.Normal || [0, 0, -1];
        Lights[I] = {
          Cx: L.Center[0], Cy: L.Center[1], Cz: L.Center[2],
          Rx: Right[0], Ry: Right[1], Rz: Right[2],
          Ux: Up[0], Uy: Up[1], Uz: Up[2],
          Nx: Normal[0], Ny: Normal[1], Nz: Normal[2],
          Omni: Boolean(L.Omni),
          Hw: L.HalfWidth, Hh: L.HalfHeight,
          QuadVisible: L.QuadVisible !== false,
          PickCode: L.PickCode || (400 + I),
          RadR: L.Radiance[0], RadG: L.Radiance[1], RadB: L.Radiance[2],
        };
      }
      return { Prims, Lights };
    }

    IntersectPrimT(P, Rox, Roy, Roz, Rdx, Rdy, Rdz, TMax, Backface) {
      const Dx = Rox - P.Cx, Dy = Roy - P.Cy, Dz = Roz - P.Cz;
      const D2 = Dx * Dx + Dy * Dy + Dz * Dz;
      if (D2 > P.BoundR2) {
        const TClosest = -(Dx * Rdx + Dy * Rdy + Dz * Rdz);
        if (TClosest < 0 || TClosest - P.BoundR > TMax) return TMax;
        if (D2 - TClosest * TClosest > P.BoundR2) return TMax;
      }

      const Lox = P.R0x * Dx + P.R0y * Dy + P.R0z * Dz;
      const Loy = P.R1x * Dx + P.R1y * Dy + P.R1z * Dz;
      const Loz = P.R2x * Dx + P.R2y * Dy + P.R2z * Dz;
      const Ldx = P.R0x * Rdx + P.R0y * Rdy + P.R0z * Rdz;
      const Ldy = P.R1x * Rdx + P.R1y * Rdy + P.R1z * Rdz;
      const Ldz = P.R2x * Rdx + P.R2y * Rdy + P.R2z * Rdz;
      const Hx = P.Hx, Hy = P.Hy, Hz = P.Hz;
      const Shape = P.Shape;

      if (Shape === 1) {
        const Ox = Lox / Hx, Oy = Loy / Hy, Oz = Loz / Hz;
        const Vx = Ldx / Hx, Vy = Ldy / Hy, Vz = Ldz / Hz;
        const A = Vx * Vx + Vy * Vy + Vz * Vz;
        const B = Ox * Vx + Oy * Vy + Oz * Vz;
        const Disc = B * B - A * (Ox * Ox + Oy * Oy + Oz * Oz - 1.0);
        if (Disc < 0.0) return TMax;
        const Sq = Math.sqrt(Disc);
        const T0 = (-B - Sq) / A;
        const T1 = (-B + Sq) / A;
        const TCand = Backface ? T1 : (T0 >= 0.0015 ? T0 : T1);
        return TCand > 0.0015 && TCand < TMax ? TCand : TMax;
      }

      if (Shape === 0) {
        const InvX = 1.0 / (Math.abs(Ldx) < 1e-7 ? (Ldx >= 0 ? 1e-7 : -1e-7) : Ldx);
        const InvY = 1.0 / (Math.abs(Ldy) < 1e-7 ? (Ldy >= 0 ? 1e-7 : -1e-7) : Ldy);
        const InvZ = 1.0 / (Math.abs(Ldz) < 1e-7 ? (Ldz >= 0 ? 1e-7 : -1e-7) : Ldz);
        const T0x = (-Hx - Lox) * InvX, T1x = (Hx - Lox) * InvX;
        const T0y = (-Hy - Loy) * InvY, T1y = (Hy - Loy) * InvY;
        const T0z = (-Hz - Loz) * InvZ, T1z = (Hz - Loz) * InvZ;
        const TNear = Math.max(T0x < T1x ? T0x : T1x, T0y < T1y ? T0y : T1y, T0z < T1z ? T0z : T1z);
        const TFar = Math.min(T0x > T1x ? T0x : T1x, T0y > T1y ? T0y : T1y, T0z > T1z ? T0z : T1z);
        if (TFar < 0.0015 || TNear > TFar) return TMax;
        const TCand = Backface ? TFar : (TNear >= 0.0015 ? TNear : TFar);
        return TCand > 0.0015 && TCand < TMax ? TCand : TMax;
      }

      if (Shape === 4) {
        const InvX = 1.0 / (Math.abs(Ldx) < 1e-6 ? 1e-6 : Ldx);
        const InvY = 1.0 / (Math.abs(Ldy) < 1e-6 ? 1e-6 : Ldy);
        const InvZ = 1.0 / (Math.abs(Ldz) < 1e-6 ? 1e-6 : Ldz);
        const Bx = Hx * 1.08, By = Hy * 1.08, Bz = Hz * 1.15;
        const TNear = Math.max(
          0.002,
          Math.min((-Bx - Lox) * InvX, (Bx - Lox) * InvX),
          Math.min((-By - Loy) * InvY, (By - Loy) * InvY),
          Math.min((-Bz - Loz) * InvZ, (Bz - Loz) * InvZ),
        );
        const TFar = Math.min(
          TMax,
          Math.max((-Bx - Lox) * InvX, (Bx - Lox) * InvX),
          Math.max((-By - Loy) * InvY, (By - Loy) * InvY),
          Math.max((-Bz - Loz) * InvZ, (Bz - Loz) * InvZ),
        );
        if (TNear > TFar) return TMax;
        const RMinor = Hz;
        const RMajor = Math.max(RMinor * 1.25, Math.max(Hx, Hy) - RMinor);
        let T = TNear;
        for (let S = 0; S < 24; ++S) {
          const Px = Lox + Ldx * T, Py = Loy + Ldy * T, Pz = Loz + Ldz * T;
          const Dist = Len2(Len2(Px, Py) - RMajor, Pz) - RMinor;
          if (Math.abs(Dist) < 0.0014) return T < TMax ? T : TMax;
          T += Dist * 0.94;
          if (T > TFar) break;
        }
        return TMax;
      }

      const SolveFrustum = (Z0, Z1, R0, R1, CurBest) => {
        let Out = CurBest;
        const Zc = 0.5 * (Z0 + Z1);
        const Hc = 0.5 * (Z1 - Z0);
        const Rm = 0.5 * (R0 + R1);
        const K = Hc > 1e-5 ? (R1 - R0) / (2.0 * Hc) : 0.0;
        const Oz = Loz - Zc;
        const RAtOz = Rm + K * Oz;
        const A = Ldx * Ldx + Ldy * Ldy - K * K * Ldz * Ldz;
        const B = Lox * Ldx + Loy * Ldy - K * RAtOz * Ldz;
        const C = Lox * Lox + Loy * Loy - RAtOz * RAtOz;
        const Disc = B * B - A * C;
        if (Math.abs(A) > 1e-6 && Disc >= 0.0) {
          const Sq = Math.sqrt(Disc);
          const Ra = (-B - Sq) / A, Rb = (-B + Sq) / A;
          const TMinR = Math.min(Ra, Rb), TFarR = Math.max(Ra, Rb);
          if (Backface) {
            if (TFarR > 0.0015 && TFarR < Out && Math.abs(Oz + Ldz * TFarR) <= Hc) Out = TFarR;
            else if (TMinR > 0.0015 && TMinR < Out && Math.abs(Oz + Ldz * TMinR) <= Hc) Out = TMinR;
          } else {
            if (TMinR > 0.0015 && TMinR < Out && Math.abs(Oz + Ldz * TMinR) <= Hc) Out = TMinR;
            else if (TFarR > 0.0015 && TFarR < Out && Math.abs(Oz + Ldz * TFarR) <= Hc) Out = TFarR;
          }
        }
        if (!Backface && Math.abs(Ldz) > 1e-6) {
          const TC0 = (Z0 - Loz) / Ldz;
          if (TC0 > 0.0015 && TC0 < Out) {
            const X0 = Lox + Ldx * TC0, Y0 = Loy + Ldy * TC0;
            if (X0 * X0 + Y0 * Y0 <= R0 * R0) Out = TC0;
          }
          const TC1 = (Z1 - Loz) / Ldz;
          if (TC1 > 0.0015 && TC1 < Out) {
            const X1 = Lox + Ldx * TC1, Y1 = Loy + Ldy * TC1;
            if (X1 * X1 + Y1 * Y1 <= R1 * R1) Out = TC1;
          }
        }
        return Out;
      };

      const Rad = Math.max(Hx, Hy);
      if (Shape === 3) {
        const ZBase = -0.78 * Hz;
        let BestT = TMax;
        const InvX = Math.abs(Ldx) > 1e-6 ? 1.0 / Ldx : 1e6;
        const InvY = Math.abs(Ldy) > 1e-6 ? 1.0 / Ldy : 1e6;
        const InvZ = Math.abs(Ldz) > 1e-6 ? 1.0 / Ldz : 1e6;
        const Tx0 = (-Rad - Lox) * InvX, Tx1 = (Rad - Lox) * InvX;
        const Ty0 = (-Rad - Loy) * InvY, Ty1 = (Rad - Loy) * InvY;
        const Tz0 = (-Hz - Loz) * InvZ, Tz1 = (ZBase - Loz) * InvZ;
        const BNear = Math.max(Math.min(Tx0, Tx1), Math.min(Ty0, Ty1), Math.min(Tz0, Tz1));
        const BFar = Math.min(Math.max(Tx0, Tx1), Math.max(Ty0, Ty1), Math.max(Tz0, Tz1));
        if (BNear <= BFar && BFar > 0.0015) {
          const TCand = Backface ? BFar : (BNear >= 0.0015 ? BNear : BFar);
          if (TCand > 0.0015 && TCand < BestT) BestT = TCand;
        }
        return SolveFrustum(ZBase, Hz, 0.72 * Rad, 0.13 * Rad, BestT);
      }

      if (Shape === 5) {
        const ZBase = -0.84 * Hz;
        const BestBase = SolveFrustum(-Hz, ZBase, Rad, 0.94 * Rad, TMax);
        return SolveFrustum(ZBase, Hz, 0.50 * Rad, 0.37 * Rad, BestBase);
      }

      return SolveFrustum(-Hz, Hz, Rad, Rad, TMax);
    }

    FinalizeHit(Prims, Idx, Rox, Roy, Roz, Rdx, Rdy, Rdz, T, Backface, OutHit) {
      const P = Prims[Idx];
      OutHit.hit = true;
      OutHit.t = T;
      OutHit.primIdx = Idx;
      OutHit.px = Rox + Rdx * T;
      OutHit.py = Roy + Rdy * T;
      OutHit.pz = Roz + Rdz * T;

      const Dx = OutHit.px - P.Cx, Dy = OutHit.py - P.Cy, Dz = OutHit.pz - P.Cz;
      const Lpx = P.R0x * Dx + P.R0y * Dy + P.R0z * Dz;
      const Lpy = P.R1x * Dx + P.R1y * Dy + P.R1z * Dz;
      const Lpz = P.R2x * Dx + P.R2y * Dy + P.R2z * Dz;
      const Hx = P.Hx, Hy = P.Hy, Hz = P.Hz;
      const Shape = P.Shape;

      let Lnx = 0, Lny = 0, Lnz = 1;
      let Ltx = 1, Lty = 0, Ltz = 0;
      let U = 0.5, V = 0.5;

      if (Shape === 1) {
        Lnx = Lpx / (Hx * Hx); Lny = Lpy / (Hy * Hy); Lnz = Lpz / (Hz * Hz);
        if (!P.ConstMat) {
          const Phi = Math.atan2(Lpy / Hy, Lpx / Hx);
          U = Phi / (2.0 * PI) + 0.5;
          V = Math.acos(Clamp(Lpz / Hz, -1, 1)) / PI;
          Ltx = -Math.sin(Phi); Lty = Math.cos(Phi); Ltz = 0;
        }
      } else if (Shape === 4) {
        const RMinor = Hz;
        const RMajor = Math.max(RMinor * 1.25, Math.max(Hx, Hy) - RMinor);
        const Rxy = Math.max(1e-5, Len2(Lpx, Lpy));
        Lnx = Lpx - (Lpx / Rxy) * RMajor;
        Lny = Lpy - (Lpy / Rxy) * RMajor;
        Lnz = Lpz;
        const Phi = Math.atan2(Lpy, Lpx);
        U = Phi / (2.0 * PI) + 0.5;
        V = Math.atan2(Lpz, Rxy - RMajor) / (2.0 * PI) + 0.5;
        Ltx = -Math.sin(Phi); Lty = Math.cos(Phi); Ltz = 0;
      } else if (Shape >= 2) {
        const Phi = Math.atan2(Lpy, Lpx);
        const Rad = Math.max(Hx, Hy);
        const Rxy = Math.max(1e-5, Len2(Lpx, Lpy));
        if (Shape === 3) {
          const ZBase = -0.78 * Hz;
          if (Lpz <= ZBase + 0.0035) {
            if (Lpz >= ZBase - 0.004 && Math.max(Math.abs(Lpx), Math.abs(Lpy)) > 0.70 * Rad) {
              Lnx = 0; Lny = 0; Lnz = 1;
            } else if (Lpz <= -Hz + 0.004) {
              Lnx = 0; Lny = 0; Lnz = -1;
            } else if (Math.abs(Lpx) >= Math.abs(Lpy)) {
              Lnx = Lpx >= 0 ? 1 : -1; Lny = 0; Lnz = 0;
            } else {
              Lnx = 0; Lny = Lpy >= 0 ? 1 : -1; Lnz = 0;
            }
          } else if (Math.abs(Lpz - Hz) < 0.004) {
            Lnx = 0; Lny = 0; Lnz = 1;
          } else {
            const Slope = (0.72 * Rad - 0.13 * Rad) / Math.max(1e-4, Hz - ZBase);
            Lnx = Lpx / Rxy; Lny = Lpy / Rxy; Lnz = Slope;
          }
          Ltx = -Math.sin(Phi); Lty = Math.cos(Phi);
        } else if (Shape === 5) {
          const ZBase = -0.84 * Hz;
          if (Math.abs(Lpz - Hz) < 0.004) {
            Lnx = 0; Lny = 0; Lnz = 1;
          } else if (Math.abs(Lpz + Hz) < 0.004) {
            Lnx = 0; Lny = 0; Lnz = -1;
          } else if (Math.abs(Lpz - ZBase) < 0.0045 && Rxy > 0.49 * Rad) {
            Lnx = 0; Lny = 0; Lnz = 1;
          } else if (Lpz < ZBase) {
            Lnx = Lpx / Rxy; Lny = Lpy / Rxy; Lnz = 0.28;
          } else {
            const Slope = (0.50 * Rad - 0.37 * Rad) / Math.max(1e-4, Hz - ZBase);
            Lnx = Lpx / Rxy; Lny = Lpy / Rxy; Lnz = Slope;
          }
          Ltx = -Math.sin(Phi); Lty = Math.cos(Phi);
        } else if (Math.abs(Math.abs(Lpz) - Hz) < 0.004) {
          Lnx = 0; Lny = 0; Lnz = Lpz >= 0 ? 1 : -1;
        } else {
          Lnx = Lpx; Lny = Lpy; Lnz = 0;
          Ltx = -Math.sin(Phi); Lty = Math.cos(Phi);
        }
        U = Phi / (2.0 * PI) + 0.5;
        V = Lpz / (2.0 * Hz) + 0.5;
      } else {
        const Qx = Math.abs(Lpx) / Hx, Qy = Math.abs(Lpy) / Hy, Qz = Math.abs(Lpz) / Hz;
        if (Qx >= Qy && Qx >= Qz) {
          Lnx = Lpx >= 0 ? 1 : -1; Lny = 0; Lnz = 0;
          Ltx = 0; Lty = 1; Ltz = 0;
          U = (Lpy / Hy) * 0.5 + 0.5; V = (Lpz / Hz) * 0.5 + 0.5;
        } else if (Qy >= Qx && Qy >= Qz) {
          Lnx = 0; Lny = Lpy >= 0 ? 1 : -1; Lnz = 0;
          Ltx = 1; Lty = 0; Ltz = 0;
          U = (Lpx / Hx) * 0.5 + 0.5; V = (Lpz / Hz) * 0.5 + 0.5;
        } else {
          Lnx = 0; Lny = 0; Lnz = Lpz >= 0 ? 1 : -1;
          Ltx = 1; Lty = 0; Ltz = 0;
          U = (Lpx / Hx) * 0.5 + 0.5; V = (Lpy / Hy) * 0.5 + 0.5;
        }
      }

      const RawNLen = Math.max(1e-6, Len3(Lnx, Lny, Lnz));
      const Unx = Lnx / RawNLen, Uny = Lny / RawNLen, Unz = Lnz / RawNLen;
      if (P.FaceAtlas) {
        let FaceIdx = 2;
        let Fu = Clamp(U, 0, 1);
        let Fv = Clamp(V, 0, 1);
        if (Shape >= 2) {
          if (Unz > 0.85) {
            FaceIdx = 5;
            Fu = Clamp((Lpx / Math.max(1e-4, Hx)) * 0.5 + 0.5, 0, 1);
            Fv = Clamp((Lpy / Math.max(1e-4, Hy)) * 0.5 + 0.5, 0, 1);
          } else if (Unz < -0.85) {
            FaceIdx = 4;
            Fu = Clamp((Lpx / Math.max(1e-4, Hx)) * 0.5 + 0.5, 0, 1);
            Fv = Clamp((Lpy / Math.max(1e-4, Hy)) * 0.5 + 0.5, 0, 1);
          } else {
            FaceIdx = 2;
          }
        } else {
          const Ax = Math.abs(Unx), Ay = Math.abs(Uny), Az = Math.abs(Unz);
          if (Ax >= Ay && Ax >= Az) FaceIdx = Unx > 0 ? 1 : 0;
          else if (Ay >= Ax && Ay >= Az) FaceIdx = Uny > 0 ? 3 : 2;
          else FaceIdx = Unz > 0 ? 5 : 4;
        }
        const CellCol = FaceIdx % 3;
        const CellRow = (FaceIdx / 3) | 0;
        U = (CellCol + 0.08 + Fu * 0.84) / 3.0;
        V = (CellRow + 0.08 + Fv * 0.84) / 2.0;
      }

      if (Backface) { Lnx = -Unx; Lny = -Uny; Lnz = -Unz; }
      else { Lnx = Unx; Lny = Uny; Lnz = Unz; }

      const Wnx = P.R0x * Lnx + P.R1x * Lny + P.R2x * Lnz;
      const Wny = P.R0y * Lnx + P.R1y * Lny + P.R2y * Lnz;
      const Wnz = P.R0z * Lnx + P.R1z * Lny + P.R2z * Lnz;
      const WnLen = Math.max(1e-6, Len3(Wnx, Wny, Wnz));
      OutHit.nx = Wnx / WnLen; OutHit.ny = Wny / WnLen; OutHit.nz = Wnz / WnLen;

      const Wtx = P.R0x * Ltx + P.R1x * Lty + P.R2x * Ltz;
      const Wty = P.R0y * Ltx + P.R1y * Lty + P.R2y * Ltz;
      const Wtz = P.R0z * Ltx + P.R1z * Lty + P.R2z * Ltz;
      const WtLen = Math.max(1e-6, Len3(Wtx, Wty, Wtz));
      OutHit.tx = Wtx / WtLen; OutHit.ty = Wty / WtLen; OutHit.tz = Wtz / WtLen;
      OutHit.u = U; OutHit.v = V;
      return OutHit;
    }

    IntersectAabbBvh(Rox, Roy, Roz, InvX, InvY, InvZ, MinX, MinY, MinZ, MaxX, MaxY, MaxZ, TMax) {
      const T0x = (MinX - Rox) * InvX, T1x = (MaxX - Rox) * InvX;
      const T0y = (MinY - Roy) * InvY, T1y = (MaxY - Roy) * InvY;
      const T0z = (MinZ - Roz) * InvZ, T1z = (MaxZ - Roz) * InvZ;
      const TMin = Math.max(T0x < T1x ? T0x : T1x, T0y < T1y ? T0y : T1y, T0z < T1z ? T0z : T1z);
      const TMaxBox = Math.min(T0x > T1x ? T0x : T1x, T0y > T1y ? T0y : T1y, T0z > T1z ? T0z : T1z);
      return (TMaxBox >= (TMin > 0 ? TMin : 0) && TMin < TMax) ? (TMin > 0 ? TMin : 0) : 1e20;
    }

    TraceVehicleBvh(Veh, Rox, Roy, Roz, Rdx, Rdy, Rdz, TMax, ShadowOnly, OutHit) {
      if (!Veh || !Veh.instCount || !Veh.nodes || !Veh.tris || !Veh.insts) return false;
      const Nodes = Veh.nodes;
      const Tris = Veh.tris;
      const Insts = Veh.insts;
      const InvWx = 1.0 / (Math.abs(Rdx) < 1e-7 ? (Rdx >= 0 ? 1e-7 : -1e-7) : Rdx);
      const InvWy = 1.0 / (Math.abs(Rdy) < 1e-7 ? (Rdy >= 0 ? 1e-7 : -1e-7) : Rdy);
      const InvWz = 1.0 / (Math.abs(Rdz) < 1e-7 ? (Rdz >= 0 ? 1e-7 : -1e-7) : Rdz);

      const TlasStack = this._TlasStack;
      const BlasStack = this._BlasStack;
      let TlasTop = 1;
      TlasStack[0] = 0;
      let BestT = TMax;
      let BestTri = -1;
      let BestInst = -1;
      let BestU = 0.0;
      let BestV = 0.0;
      const MinHitT = ShadowOnly ? 0.004 : 0.0005;

      while (TlasTop > 0) {
        const NodeIdx = TlasStack[--TlasTop];
        const Nb = NodeIdx * 8;
        if (this.IntersectAabbBvh(Rox, Roy, Roz, InvWx, InvWy, InvWz, Nodes[Nb], Nodes[Nb + 1], Nodes[Nb + 2], Nodes[Nb + 4], Nodes[Nb + 5], Nodes[Nb + 6], BestT) >= BestT) continue;
        const LeftFirst = Math.round(Nodes[Nb + 3]);
        const Count = Math.round(Nodes[Nb + 7]);
        if (Count < 0) {
          const InstIdx = (-Count) - 1;
          if (InstIdx >= 0 && InstIdx < Veh.instCount) {
            const Ib = InstIdx * 40;
            const BlasRoot = LeftFirst;
            if (ShadowOnly && Insts[Ib + 34] < 0.5) continue;

            // w2l stored column-major at Ib + 16..31
            const Lox = Insts[Ib + 16] * Rox + Insts[Ib + 20] * Roy + Insts[Ib + 24] * Roz + Insts[Ib + 28];
            const Loy = Insts[Ib + 17] * Rox + Insts[Ib + 21] * Roy + Insts[Ib + 25] * Roz + Insts[Ib + 29];
            const Loz = Insts[Ib + 18] * Rox + Insts[Ib + 22] * Roy + Insts[Ib + 26] * Roz + Insts[Ib + 30];
            const Ldx = Insts[Ib + 16] * Rdx + Insts[Ib + 20] * Rdy + Insts[Ib + 24] * Rdz;
            const Ldy = Insts[Ib + 17] * Rdx + Insts[Ib + 21] * Rdy + Insts[Ib + 25] * Rdz;
            const Ldz = Insts[Ib + 18] * Rdx + Insts[Ib + 22] * Rdy + Insts[Ib + 26] * Rdz;
            const InvLx = 1.0 / (Math.abs(Ldx) < 1e-7 ? (Ldx >= 0 ? 1e-7 : -1e-7) : Ldx);
            const InvLy = 1.0 / (Math.abs(Ldy) < 1e-7 ? (Ldy >= 0 ? 1e-7 : -1e-7) : Ldy);
            const InvLz = 1.0 / (Math.abs(Ldz) < 1e-7 ? (Ldz >= 0 ? 1e-7 : -1e-7) : Ldz);

            let BlasTop = 1;
            BlasStack[0] = BlasRoot;
            while (BlasTop > 0) {
              const BIdx = BlasStack[--BlasTop];
              const Bb = BIdx * 8;
              if (this.IntersectAabbBvh(Lox, Loy, Loz, InvLx, InvLy, InvLz, Nodes[Bb], Nodes[Bb + 1], Nodes[Bb + 2], Nodes[Bb + 4], Nodes[Bb + 5], Nodes[Bb + 6], BestT) >= BestT) continue;
              const BLeft = Math.round(Nodes[Bb + 3]);
              const BCnt = Math.round(Nodes[Bb + 7]);
              if (BCnt > 0) {
                for (let Ti = 0; Ti < BCnt; ++Ti) {
                  const TriIdx = BLeft + Ti;
                  const Tb = TriIdx * 16;
                  const V0x = Tris[Tb], V0y = Tris[Tb + 1], V0z = Tris[Tb + 2];
                  const E1x = Tris[Tb + 4] - V0x, E1y = Tris[Tb + 5] - V0y, E1z = Tris[Tb + 6] - V0z;
                  const E2x = Tris[Tb + 8] - V0x, E2y = Tris[Tb + 9] - V0y, E2z = Tris[Tb + 10] - V0z;
                  const Pvx = Ldy * E2z - Ldz * E2y;
                  const Pvy = Ldz * E2x - Ldx * E2z;
                  const Pvz = Ldx * E2y - Ldy * E2x;
                  const Det = E1x * Pvx + E1y * Pvy + E1z * Pvz;
                  if (Math.abs(Det) < 1e-8) continue;
                  const InvDet = 1.0 / Det;
                  const Tvx = Lox - V0x, Tvy = Loy - V0y, Tvz = Loz - V0z;
                  const U = (Tvx * Pvx + Tvy * Pvy + Tvz * Pvz) * InvDet;
                  if (U < 0.0 || U > 1.0) continue;
                  const Qvx = Tvy * E1z - Tvz * E1y;
                  const Qvy = Tvz * E1x - Tvx * E1z;
                  const Qvz = Tvx * E1y - Tvy * E1x;
                  const V = (Ldx * Qvx + Ldy * Qvy + Ldz * Qvz) * InvDet;
                  if (V < 0.0 || U + V > 1.0) continue;
                  const THit = (E2x * Qvx + E2y * Qvy + E2z * Qvz) * InvDet;
                  if (THit > MinHitT && THit < BestT) {
                    BestT = THit;
                    BestTri = TriIdx;
                    BestInst = InstIdx;
                    BestU = U;
                    BestV = V;
                    if (ShadowOnly) {
                      if (OutHit) OutHit.t = BestT;
                      return true;
                    }
                  }
                }
              } else {
                const BRight = BLeft + 1;
                const BLb = BLeft * 8, BRb = BRight * 8;
                const DL = this.IntersectAabbBvh(Lox, Loy, Loz, InvLx, InvLy, InvLz, Nodes[BLb], Nodes[BLb + 1], Nodes[BLb + 2], Nodes[BLb + 4], Nodes[BLb + 5], Nodes[BLb + 6], BestT);
                const DR = this.IntersectAabbBvh(Lox, Loy, Loz, InvLx, InvLy, InvLz, Nodes[BRb], Nodes[BRb + 1], Nodes[BRb + 2], Nodes[BRb + 4], Nodes[BRb + 5], Nodes[BRb + 6], BestT);
                if (DL < DR) {
                  if (DR < BestT && BlasTop < 47) BlasStack[BlasTop++] = BRight;
                  if (DL < BestT && BlasTop < 47) BlasStack[BlasTop++] = BLeft;
                } else {
                  if (DL < BestT && BlasTop < 47) BlasStack[BlasTop++] = BLeft;
                  if (DR < BestT && BlasTop < 47) BlasStack[BlasTop++] = BRight;
                }
              }
            }
          }
        } else {
          const RChild = LeftFirst + 1;
          const Lb = LeftFirst * 8, Rb = RChild * 8;
          const DL = this.IntersectAabbBvh(Rox, Roy, Roz, InvWx, InvWy, InvWz, Nodes[Lb], Nodes[Lb + 1], Nodes[Lb + 2], Nodes[Lb + 4], Nodes[Lb + 5], Nodes[Lb + 6], BestT);
          const DR = this.IntersectAabbBvh(Rox, Roy, Roz, InvWx, InvWy, InvWz, Nodes[Rb], Nodes[Rb + 1], Nodes[Rb + 2], Nodes[Rb + 4], Nodes[Rb + 5], Nodes[Rb + 6], BestT);
          if (DL < DR) {
            if (DR < BestT && TlasTop < 47) TlasStack[TlasTop++] = RChild;
            if (DL < BestT && TlasTop < 47) TlasStack[TlasTop++] = LeftFirst;
          } else {
            if (DL < BestT && TlasTop < 47) TlasStack[TlasTop++] = LeftFirst;
            if (DR < BestT && TlasTop < 47) TlasStack[TlasTop++] = RChild;
          }
        }
      }

      if (BestTri >= 0 && BestInst >= 0 && OutHit) {
        const Ib = BestInst * 40;
        const Tb = BestTri * 16;
        const UnpackOct = (pf) => {
          const p = Math.round(pf);
          const u = ((p & 1023) / 1023.0) * 2.0 - 1.0;
          const v = (((p >> 10) & 1023) / 1023.0) * 2.0 - 1.0;
          let x = u, y = v, z = 1.0 - Math.abs(u) - Math.abs(v);
          if (z < 0.0) {
            const ox = x, oy = y;
            x = (1.0 - Math.abs(oy)) * (ox >= 0.0 ? 1.0 : -1.0);
            y = (1.0 - Math.abs(ox)) * (oy >= 0.0 ? 1.0 : -1.0);
          }
          const l = Math.hypot(x, y, z) || 1.0;
          return [x / l, y / l, z / l];
        };
        const n0 = UnpackOct(Tris[Tb + 7]);
        const n1 = UnpackOct(Tris[Tb + 11]);
        const n2 = UnpackOct(Tris[Tb + 15]);
        const bw = Math.max(0.0, 1.0 - BestU - BestV);
        let Nlx = n0[0] * bw + n1[0] * BestU + n2[0] * BestV;
        let Nly = n0[1] * bw + n1[1] * BestU + n2[1] * BestV;
        let Nlz = n0[2] * bw + n1[2] * BestU + n2[2] * BestV;
        const Nll = Math.hypot(Nlx, Nly, Nlz) || 1.0;
        Nlx /= Nll; Nly /= Nll; Nlz /= Nll;
        const e1x = Tris[Tb + 4] - Tris[Tb], e1y = Tris[Tb + 5] - Tris[Tb + 1], e1z = Tris[Tb + 6] - Tris[Tb + 2];
        const e2x = Tris[Tb + 8] - Tris[Tb], e2y = Tris[Tb + 9] - Tris[Tb + 1], e2z = Tris[Tb + 10] - Tris[Tb + 2];
        const gx = e1y * e2z - e1z * e2y, gy = e1z * e2x - e1x * e2z, gz = e1x * e2y - e1y * e2x;
        if (Nlx * gx + Nly * gy + Nlz * gz < 0.0) { Nlx = -Nlx; Nly = -Nly; Nlz = -Nlz; }
        let Wnx = Insts[Ib + 16] * Nlx + Insts[Ib + 17] * Nly + Insts[Ib + 18] * Nlz;
        let Wny = Insts[Ib + 20] * Nlx + Insts[Ib + 21] * Nly + Insts[Ib + 22] * Nlz;
        let Wnz = Insts[Ib + 24] * Nlx + Insts[Ib + 25] * Nly + Insts[Ib + 26] * Nlz;
        const NL = Math.max(1e-6, Len3(Wnx, Wny, Wnz));
        Wnx /= NL; Wny /= NL; Wnz /= NL;
        const RdDot = Wnx * Rdx + Wny * Rdy + Wnz * Rdz;
        if (RdDot > -0.02) {
          Wnx -= Rdx * (RdDot + 0.02);
          Wny -= Rdy * (RdDot + 0.02);
          Wnz -= Rdz * (RdDot + 0.02);
          const L2 = Math.max(1e-6, Len3(Wnx, Wny, Wnz));
          Wnx /= L2; Wny /= L2; Wnz /= L2;
        }
        OutHit.hit = true;
        OutHit.isVehicle = true;
        OutHit.t = BestT;
        OutHit.primIdx = -1;
        OutHit.px = Rox + Rdx * BestT;
        OutHit.py = Roy + Rdy * BestT;
        OutHit.pz = Roz + Rdz * BestT;
        OutHit.nx = Wnx; OutHit.ny = Wny; OutHit.nz = Wnz;
        OutHit.tx = 1; OutHit.ty = 0; OutHit.tz = 0;
        OutHit.u = 0.5; OutHit.v = 0.5;
        const OverrideMat = Math.round(Insts[Ib + 32]) - 1;
        OutHit.vehMatId = OverrideMat >= 0 ? OverrideMat : Math.round(Tris[Tb + 3]);
        OutHit.vehPickCode = Math.round(Insts[Ib + 33]);
        OutHit.vehCoat = Insts[Ib + 35];
        OutHit.vehPr = Insts[Ib + 36];
        OutHit.vehPg = Insts[Ib + 37];
        OutHit.vehPb = Insts[Ib + 38];
        OutHit.vehRough = Insts[Ib + 39];
        return true;
      }
      return false;
    }

    BuildVehicleMaterialInto(Hit, M) {
      M.baseR = Hit.vehPr; M.baseG = Hit.vehPg; M.baseB = Hit.vehPb; M.opacity = 1.0;
      M.metallic = 0.0; M.roughness = Math.max(0.04, Hit.vehRough); M.ior = 1.5; M.ao = 1.0;
      M.normX = 0; M.normY = 0; M.normZ = 1;
      M.anisotropy = 0; M.anisoRot = 0; M.coat = 0; M.coatRoughness = 0.06;
      M.sheenR = 0; M.sheenG = 0; M.sheenB = 0; M.sheenRoughness = 0.5;
      M.subR = 0; M.subG = 0; M.subB = 0; M.thickness = 0;
      M.coatNormX = 0; M.coatNormY = 0; M.coatNormZ = 1;
      M.transmission = 0; M.emission = 0; M.diffRoughness = 0.35; M.unlit = false;
      const Id = Hit.vehMatId;
      if (Id === 6) {
        M.metallic = 0.25; M.roughness = Math.max(0.06, Hit.vehRough); M.coat = Hit.vehCoat; M.coatRoughness = 0.045;
      } else if (Id === 7) {
        M.baseR = 0.045; M.baseG = 0.052; M.baseB = 0.070; M.metallic = 0.08; M.roughness = 0.035; M.coat = 1.0; M.coatRoughness = 0.025; M.ior = 1.54;
      } else if (Id === 8 || Id === 15 || Id === 16) {
        M.baseR = 0.075; M.baseG = 0.08; M.baseB = 0.09; M.metallic = 0.15; M.roughness = 0.28; M.coat = 0.32;
      } else if (Id === 9) {
        M.baseR = 0.045; M.baseG = 0.048; M.baseB = 0.055; M.metallic = 0.06; M.roughness = 0.12; M.coat = 0.85; M.coatRoughness = 0.04;
      } else if (Id === 10) {
        M.baseR = 0.76; M.baseG = 0.79; M.baseB = 0.84; M.metallic = 0.92; M.roughness = 0.15; M.coat = 0.45;
      } else if (Id === 11) {
        M.baseR = 0.055; M.baseG = 0.058; M.baseB = 0.065; M.metallic = 0.02; M.roughness = 0.66;
      } else if (Id === 12) {
        M.baseR = 0.84; M.baseG = 0.86; M.baseB = 0.90; M.metallic = 0.95; M.roughness = 0.10;
      } else if (Id === 13) {
        M.baseR = 1.0; M.baseG = 0.97; M.baseB = 0.90; M.roughness = 0.08; M.emission = 5.0;
      } else if (Id === 14) {
        M.baseR = 1.0; M.baseG = 0.08; M.baseB = 0.06; M.roughness = 0.10; M.emission = 3.8;
      }
      return M;
    }

    TraceScene(Prims, Veh, Rox, Roy, Roz, Rdx, Rdy, Rdz, TMax, SkipIdx, SkipGlass, OutHit) {
      let BestT = TMax;
      let BestIdx = -1;
      for (let I = 0; I < Prims.length; ++I) {
        if (I === SkipIdx) continue;
        if (SkipGlass && Prims[I].GlassShell) continue;
        const T = this.IntersectPrimT(Prims[I], Rox, Roy, Roz, Rdx, Rdy, Rdz, BestT, false);
        if (T < BestT) {
          BestT = T;
          BestIdx = I;
        }
      }
      if (Veh && this.TraceVehicleBvh(Veh, Rox, Roy, Roz, Rdx, Rdy, Rdz, BestT, false, OutHit)) {
        return OutHit;
      }
      if (BestIdx < 0) {
        OutHit.hit = false;
        OutHit.isVehicle = false;
        OutHit.t = TMax;
        OutHit.primIdx = -1;
        return OutHit;
      }
      OutHit.isVehicle = false;
      return this.FinalizeHit(Prims, BestIdx, Rox, Roy, Roz, Rdx, Rdy, Rdz, BestT, false, OutHit);
    }

    TraceShadowRayAny(Prims, Veh, Ox, Oy, Oz, DirX, DirY, DirZ, MaxDist, SelfIdx, SkipPickCode = -1) {
      for (let I = 0; I < Prims.length; ++I) {
        if (I === SelfIdx) continue;
        const P = Prims[I];
        if (SkipPickCode >= 0 && P.PickCode === SkipPickCode) continue;
        if (!P.CastShadows || P.GlassShell) continue;
        const T = this.IntersectPrimT(P, Ox, Oy, Oz, DirX, DirY, DirZ, MaxDist, false);
        if (T < MaxDist) return true;
      }
      if (Veh && this.TraceVehicleBvh(Veh, Ox, Oy, Oz, DirX, DirY, DirZ, MaxDist, true, null)) {
        return true;
      }
      return false;
    }

    EvaluateRayShadow(Prims, Veh, Ox, Oy, Oz, DirX, DirY, DirZ, MaxDist, ConeSin, RayCnt, MinVis, SelfIdx, SkipPickCode = -1) {
      const Cnt = Clamp(RayCnt | 0, 1, 8);
      if (Cnt <= 1 || ConeSin <= 1e-4) {
        return this.TraceShadowRayAny(Prims, Veh, Ox, Oy, Oz, DirX, DirY, DirZ, MaxDist, SelfIdx, SkipPickCode) ? MinVis : 1.0;
      }
      let Ax = 0, Ay = 0, Az = 1;
      if (Math.abs(DirZ) > 0.9) { Ax = 1; Ay = 0; Az = 0; }
      let Tx = DirY * Az - DirZ * Ay, Ty = DirZ * Ax - DirX * Az, Tz = DirX * Ay - DirY * Ax;
      const Tl = Math.max(1e-6, Len3(Tx, Ty, Tz));
      Tx /= Tl; Ty /= Tl; Tz /= Tl;
      const Bx = DirY * Tz - DirZ * Ty, By = DirZ * Tx - DirX * Tz, Bz = DirX * Ty - DirY * Tx;
      const Phi = Hash12(Ox * 173.1 + Oz * 59.7, Oy * 91.3) * 2.0 * PI;
      let Vis = 0.0;
      for (let I = 0; I < Cnt; ++I) {
        const R = Math.sqrt((I + 0.5) / Cnt) * ConeSin;
        const Th = I * 2.3999632 + Phi;
        const Ux = Math.cos(Th) * R, Uy = Math.sin(Th) * R;
        let Sx = DirX + Tx * Ux + Bx * Uy;
        let Sy = DirY + Ty * Ux + By * Uy;
        let Sz = DirZ + Tz * Ux + Bz * Uy;
        const Sl = Math.max(1e-6, Len3(Sx, Sy, Sz));
        if (!this.TraceShadowRayAny(Prims, Veh, Ox, Oy, Oz, Sx / Sl, Sy / Sl, Sz / Sl, MaxDist, SelfIdx, SkipPickCode)) {
          Vis += 1.0;
        }
      }
      return Mix(MinVis, 1.0, Vis / Cnt);
    }

    EvalStarlightAmbient(Nx, Ny, Nz, SunDir, OutRgb, CompiledScene = null) {
      const St = CompiledScene?.Stars;
      if (St && St.enabled === false) {
        OutRgb[0] = 0; OutRgb[1] = 0; OutRgb[2] = 0;
        return;
      }
      const MagGain = Clamp((St?.limMag ?? 5.5) / 5.5, 0.35, 1.85);
      const StarBright = Math.max(0.0, St?.brightness ?? 1.0);
      const StarFade = Clamp(1.0 - (SunDir[2] + 0.04) * 5.0, 0.12, 1.0);
      const Hemi = 0.35 + 0.65 * Clamp(Nz * 0.5 + 0.5, 0.0, 1.0);
      const K = StarBright * MagGain * StarFade * Hemi;
      OutRgb[0] = 0.00048 * K;
      OutRgb[1] = 0.00062 * K;
      OutRgb[2] = 0.00105 * K;
    }

    EvalMoonIrradiance(CompiledScene, OutRgb) {
      const Mn = CompiledScene?.Moon;
      if (!Mn?.visible || !Mn.dir || Mn.dir[2] <= 0.01) {
        OutRgb[0] = 0; OutRgb[1] = 0; OutRgb[2] = 0;
        return false;
      }
      const SunDir = CompiledScene.SunDir || [0, 0, 1];
      const DayAtt = Mix(1.0, 0.12, Smoothstep(-0.08, 0.14, SunDir[2]));
      const PhaseGain = 0.25 + 0.75 * Math.sin(Clamp(Mn.phase ?? 0.62, 0, 1) * PI);
      const HorFade = Smoothstep(0.01, 0.12, Mn.dir[2]);
      const Mt = Mn.tint || [0.94, 0.93, 0.90];
      const Scale = (Mn.bright ?? 1.6) * 0.085 * PhaseGain * DayAtt * HorFade;
      OutRgb[0] = Mt[0] * Scale;
      OutRgb[1] = Mt[1] * Scale;
      OutRgb[2] = Mt[2] * Scale;
      return OutRgb[0] + OutRgb[1] + OutRgb[2] > 1e-5;
    }

    EvaluateGtxConeGiAndAo(Prims, VehicleBvhData, Lights, PosX, PosY, PosZ, Nx, Ny, Nz, SelfIdx, SkyRad, SunDir, SunIrr, SkyLut, EmissiveShadowed, OutGiAo, CompiledScene = null) {
      const SkyOut = this._Tmp4;
      const Sz = Nz > 0.25 ? Nz : 0.25;
      const SLen = Len3(Nx * 0.35, Ny * 0.35, Sz);
      this.EvalSky((Nx * 0.35) / SLen, (Ny * 0.35) / SLen, Sz / SLen, SkyRad, SunDir, SunIrr, SkyOut, SkyLut);
      const SkyW = Clamp(Nz * 0.5 + 0.5, 0.0, 1.0);
      const DayF = Clamp(SunDir[2] * 4.0 + 0.25, 0.0, 1.0);
      const SkyAmbR = Mix(Mix(0.0012, 0.055, DayF), SkyOut[0], SkyW) * Mix(0.18, 0.78, DayF);
      const SkyAmbG = Mix(Mix(0.0015, 0.053, DayF), SkyOut[1], SkyW) * Mix(0.18, 0.78, DayF);
      const SkyAmbB = Mix(Mix(0.0024, 0.052, DayF), SkyOut[2], SkyW) * Mix(0.18, 0.78, DayF);
      const StarAmb = [0, 0, 0];
      this.EvalStarlightAmbient(Nx, Ny, Nz, SunDir, StarAmb, CompiledScene);
      const MoonIrr = [0, 0, 0];
      const HasMoon = this.EvalMoonIrradiance(CompiledScene, MoonIrr);
      const MoonDir = CompiledScene?.Moon?.dir || [0, 0, 1];

      let BounceR = 0, BounceG = 0, BounceB = 0;
      let AoOcc = 0;

      for (let I = 0; I < Prims.length; ++I) {
        if (I === SelfIdx) continue;
        const P = Prims[I];
        if (P.GlassShell && P.Emission <= 0.05) continue;

        let Occ = 0, GiFF = 0;
        let EmNx = -Nx, EmNy = -Ny, EmNz = -Nz;
        let TgtX = P.Cx, TgtY = P.Cy, TgtZ = P.Cz;

        if (P.Shape === 1) {
          const Tx = P.Cx - PosX, Ty = P.Cy - PosY, Tz = P.Cz - PosZ;
          const Dist2 = Math.max(1e-4, Tx * Tx + Ty * Ty + Tz * Tz);
          const Dist = Math.sqrt(Dist2);
          const Dx = Tx / Dist, Dy = Ty / Dist, Dz = Tz / Dist;
          const REff = (P.Hx + P.Hy + P.Hz) * 0.3333333;
          const CosN = Nx * Dx + Ny * Dy + Nz * Dz;
          const SinH = Clamp(REff / Math.max(Dist, REff + 1e-3), 0.0, 0.98);
          const Horizon = Smoothstep(-SinH, SinH * 0.85 + 0.25, CosN);
          const SolidRatio = (REff * REff) / Math.max(Dist2, REff * REff * 1.05);
          const NearFade = Math.exp(-Math.max(0.0, Dist - REff) / 0.55);
          Occ = P.GlassShell ? 0 : Horizon * SolidRatio * NearFade * 0.75;
          GiFF = Horizon * SolidRatio * 0.85;
          EmNx = -Dx; EmNy = -Dy; EmNz = -Dz;
          TgtX = P.Cx - Dx * REff; TgtY = P.Cy - Dy * REff; TgtZ = P.Cz - Dz * REff;
        } else {
          const Dx = PosX - P.Cx, Dy = PosY - P.Cy, Dz = PosZ - P.Cz;
          const Lpx = P.R0x * Dx + P.R0y * Dy + P.R0z * Dz;
          const Lpy = P.R1x * Dx + P.R1y * Dy + P.R1z * Dz;
          const Lpz = P.R2x * Dx + P.R2y * Dy + P.R2z * Dz;
          const Lnx = P.R0x * Nx + P.R0y * Ny + P.R0z * Nz;
          const Lny = P.R1x * Nx + P.R1y * Ny + P.R1z * Nz;
          const Lnz = P.R2x * Nx + P.R2y * Ny + P.R2z * Nz;

          const Fcx = (Lnx >= 0 ? 1 : -1) * P.Hx;
          const Fcy = (Lny >= 0 ? 1 : -1) * P.Hy;
          const Fcz = (Lnz >= 0 ? 1 : -1) * P.Hz;
          const MaxAbove = Lnx * (Fcx - Lpx) + Lny * (Fcy - Lpy) + Lnz * (Fcz - Lpz);

          if (MaxAbove > 0.002) {
            const Cplx = Clamp(Lpx, -P.Hx, P.Hx);
            const Cply = Clamp(Lpy, -P.Hy, P.Hy);
            const Cplz = Clamp(Lpz, -P.Hz, P.Hz);
            TgtX = P.Cx + P.R0x * Cplx + P.R1x * Cply + P.R2x * Cplz;
            TgtY = P.Cy + P.R0y * Cplx + P.R1y * Cply + P.R2y * Cplz;
            TgtZ = P.Cz + P.R0z * Cplx + P.R1z * Cply + P.R2z * Cplz;
            const Clx = Cplx - Lpx, Cly = Cply - Lpy, Clz = Cplz - Lpz;
            const Dist2 = Clx * Clx + Cly * Cly + Clz * Clz;
            const Dist = Math.sqrt(Dist2 > 1e-5 ? Dist2 : 1e-5);
            const InvD = 1.0 / Dist;

            const Arx = Math.abs(Lpx) / (P.Hx > 0.02 ? P.Hx : 0.02);
            const Ary = Math.abs(Lpy) / (P.Hy > 0.02 ? P.Hy : 0.02);
            const Arz = Math.abs(Lpz) / (P.Hz > 0.02 ? P.Hz : 0.02);
            let Bfx = 0, Bfy = 0, Bfz = 0, FaceArea = 1;
            if (Arx >= Ary && Arx >= Arz) {
              Bfx = Lpx >= 0 ? 1 : -1; FaceArea = 4.0 * P.Hy * P.Hz;
            } else if (Ary >= Arx && Ary >= Arz) {
              Bfy = Lpy >= 0 ? 1 : -1; FaceArea = 4.0 * P.Hx * P.Hz;
            } else {
              Bfz = Lpz >= 0 ? 1 : -1; FaceArea = 4.0 * P.Hx * P.Hy;
            }
            EmNx = P.R0x * Bfx + P.R1x * Bfy + P.R2x * Bfz;
            EmNy = P.R0y * Bfx + P.R1y * Bfy + P.R2y * Bfz;
            EmNz = P.R0z * Bfx + P.R1z * Bfy + P.R2z * Bfz;

            const Idx2 = Clx * InvD, Idy2 = Cly * InvD, Idz2 = Clz * InvD;
            const RecCos = Clamp((Lnx * Idx2 + Lny * Idy2 + Lnz * Idz2) * 0.5 + 0.5, 0.0, 1.0);
            const HorFade = Clamp(MaxAbove / (Dist + 0.45), 0.0, 1.0);
            const Contact = Math.exp(-Dist / 0.38) * HorFade * RecCos;
            const SolidOmega = FaceArea / (FaceArea + PI * (Dist2 + 0.28));
            Occ = P.GlassShell ? 0 : Contact * 0.38 + SolidOmega * Math.exp(-Dist / 1.1) * HorFade * 0.12;
            const EmitCos = Math.max(0.0, -(Bfx * Idx2 + Bfy * Idy2 + Bfz * Idz2));
            GiFF = SolidOmega * HorFade * (0.35 + 0.65 * RecCos) * (0.35 + 0.65 * EmitCos);
          }
        }

        AoOcc += Occ;
        if (GiFF > 1e-4 && (P.GiContributor || P.Emission > 0.05)) {
          let Pr = 0, Pg = 0, Pb = 0;
          if (P.Emission > 0.05) {
            let EmitVis = 1.0;
            if (EmissiveShadowed) {
              const Tx2 = TgtX - PosX, Ty2 = TgtY - PosY, Tz2 = TgtZ - PosZ;
              const DistT = Len3(Tx2, Ty2, Tz2);
              if (DistT > 0.04) {
                const Blk = this.TraceShadowRayAny(Prims, VehicleBvhData, PosX + Nx * 0.014, PosY + Ny * 0.014, PosZ + Nz * 0.014, Tx2 / DistT, Ty2 / DistT, Tz2 / DistT, Math.max(0.02, DistT - 0.04), SelfIdx, P.PickCode);
                if (Blk) EmitVis = 0.05;
              }
            }
            Pr = P.BounceR * P.Emission * 2.4 * EmitVis;
            Pg = P.BounceG * P.Emission * 2.4 * EmitVis;
            Pb = P.BounceB * P.Emission * 2.4 * EmitVis;
          }
          if (P.GiContributor && !P.GlassShell) {
            const SunNoL = Math.max(0.0, EmNx * SunDir[0] + EmNy * SunDir[1] + EmNz * SunDir[2]);
            const SunFactor = (SunNoL * 0.62 + 0.10) / PI;
            Pr += P.BounceR * SunIrr[0] * SunFactor;
            Pg += P.BounceG * SunIrr[1] * SunFactor;
            Pb += P.BounceB * SunIrr[2] * SunFactor;
            if (HasMoon) {
              const MNoL = Math.max(0.0, EmNx * MoonDir[0] + EmNy * MoonDir[1] + EmNz * MoonDir[2]);
              const MFactor = (MNoL * 0.72 + 0.08) / PI;
              Pr += P.BounceR * MoonIrr[0] * MFactor;
              Pg += P.BounceG * MoonIrr[1] * MFactor;
              Pb += P.BounceB * MoonIrr[2] * MFactor;
            }
            const EmStar = [0, 0, 0];
            this.EvalStarlightAmbient(EmNx, EmNy, EmNz, SunDir, EmStar, CompiledScene);
            Pr += (P.BounceR / PI) * EmStar[0];
            Pg += (P.BounceG / PI) * EmStar[1];
            Pb += (P.BounceB / PI) * EmStar[2];

            for (let Li = 0; Li < Lights.length && Li < 6; ++Li) {
              const L0 = Lights[Li];
              const Tlx = L0.Cx - P.Cx, Tly = L0.Cy - P.Cy, Tlz = L0.Cz - P.Cz;
              const D2 = Math.max(0.5, Tlx * Tlx + Tly * Tly + Tlz * Tlz);
              const InvL = 1.0 / Math.sqrt(D2);
              const Ldx2 = Tlx * InvL, Ldy2 = Tly * InvL, Ldz2 = Tlz * InvL;
              const Area = 4.0 * L0.Hw * L0.Hh;
              const CEmit = L0.Omni ? 0.65 : Math.max(0.08, -(Ldx2 * L0.Nx + Ldy2 * L0.Ny + Ldz2 * L0.Nz));
              const LScale = (Math.max(0.15, EmNx * Ldx2 + EmNy * Ldy2 + EmNz * Ldz2) * CEmit * Area) / (PI * PI * D2);
              Pr += P.BounceR * L0.RadR * LScale;
              Pg += P.BounceG * L0.RadG * LScale;
              Pb += P.BounceB * L0.RadB * LScale;
            }

            for (let Ei = 0; Ei < Prims.length; ++Ei) {
              if (Ei === I) continue;
              const Ep = Prims[Ei];
              if (Ep.Emission > 0.05) {
                const Ex = Ep.Cx - P.Cx, Ey = Ep.Cy - P.Cy, Ez = Ep.Cz - P.Cz;
                const D2e = Math.max(0.35, Ex * Ex + Ey * Ey + Ez * Ez);
                const InvE = 1.0 / Math.sqrt(D2e);
                const Edx = Ex * InvE, Edy = Ey * InvE, Edz = Ez * InvE;
                const Re = Math.max(0.08, (Ep.Hx + Ep.Hy + Ep.Hz) * 0.3333333);
                const Omega = Math.min(1.5, (Re * Re) / Math.max(0.12, D2e - Re * Re * 0.35));
                const K = (Math.max(0.12, EmNx * Edx + EmNy * Edy + EmNz * Edz) * Omega * 1.35 * Ep.Emission) / PI;
                Pr += P.BounceR * Ep.BounceR * K;
                Pg += P.BounceG * Ep.BounceG * K;
                Pb += P.BounceB * Ep.BounceB * K;
              }
            }
          }
          BounceR += Pr * GiFF * 1.55;
          BounceG += Pg * GiFF * 1.55;
          BounceB += Pb * GiFF * 1.55;
        }
      }

      const Ao = Clamp(1.0 - AoOcc, 0.26, 1.0);
      const SkyScale = Mix(0.45, 1.0, Ao);
      OutGiAo[0] = (SkyAmbR + StarAmb[0]) * SkyScale + BounceR;
      OutGiAo[1] = (SkyAmbG + StarAmb[1]) * SkyScale + BounceG;
      OutGiAo[2] = (SkyAmbB + StarAmb[2]) * SkyScale + BounceB;
      OutGiAo[3] = Ao;
    }

    FetchMaterialSlow(AtlasPixels, PrimIdx, U, V, PrimTransmission, M) {
      const T = this._Tmp4;
      SampleAtlasSlot(AtlasPixels, PrimIdx, 0, U, V, T);
      M.baseR = T[0]; M.baseG = T[1]; M.baseB = T[2]; M.opacity = T[3];

      SampleAtlasSlot(AtlasPixels, PrimIdx, 1, U, V, T);
      M.metallic = T[0]; M.roughness = Math.max(0.025, T[1]); M.ior = 1.0 + T[2] * 1.5; M.ao = T[3];

      SampleAtlasSlot(AtlasPixels, PrimIdx, 2, U, V, T);
      let Nx = T[0] * 2 - 1, Ny = T[1] * 2 - 1, Nz = T[2] * 2 - 1;
      const NL = Math.max(1e-6, Len3(Nx, Ny, Nz));
      M.normX = Nx / NL; M.normY = Ny / NL; M.normZ = Nz / NL;

      SampleAtlasSlot(AtlasPixels, PrimIdx, 3, U, V, T);
      M.anisotropy = T[0] * 2 - 1; M.anisoRot = T[1] * 2 * PI; M.coat = T[2]; M.coatRoughness = Math.max(0.025, T[3]);

      SampleAtlasSlot(AtlasPixels, PrimIdx, 4, U, V, T);
      M.sheenR = T[0]; M.sheenG = T[1]; M.sheenB = T[2]; M.sheenRoughness = Math.max(0.04, T[3]);

      SampleAtlasSlot(AtlasPixels, PrimIdx, 5, U, V, T);
      M.subR = T[0]; M.subG = T[1]; M.subB = T[2]; M.thickness = T[3] * 2.0;

      SampleAtlasSlot(AtlasPixels, PrimIdx, 6, U, V, T);
      let Cnx = T[0] * 2 - 1, Cny = T[1] * 2 - 1, Cnz = T[2] * 2 - 1;
      const CNL = Math.max(1e-6, Len3(Cnx, Cny, Cnz));
      M.coatNormX = Cnx / CNL; M.coatNormY = Cny / CNL; M.coatNormZ = Cnz / CNL;
      M.transmission = Math.max(T[3], PrimTransmission);

      SampleAtlasSlot(AtlasPixels, PrimIdx, 7, U, V, T);
      M.emission = T[0] * 20.0;
      M.unlit = T[1] > 0.5;
      M.diffRoughness = T[2];
      return M;
    }

    FetchMaterialInto(Prims, AtlasPixels, PrimIdx, U, V, M) {
      const P = Prims[PrimIdx];
      const C = P.ConstMat;
      if (C) {
        M.baseR = C.baseR; M.baseG = C.baseG; M.baseB = C.baseB; M.opacity = C.opacity;
        M.metallic = C.metallic; M.roughness = C.roughness; M.ior = C.ior; M.ao = C.ao;
        M.normX = C.normX; M.normY = C.normY; M.normZ = C.normZ;
        M.anisotropy = C.anisotropy; M.anisoRot = C.anisoRot; M.coat = C.coat; M.coatRoughness = C.coatRoughness;
        M.sheenR = C.sheenR; M.sheenG = C.sheenG; M.sheenB = C.sheenB; M.sheenRoughness = C.sheenRoughness;
        M.subR = C.subR; M.subG = C.subG; M.subB = C.subB; M.thickness = C.thickness;
        M.coatNormX = C.coatNormX; M.coatNormY = C.coatNormY; M.coatNormZ = C.coatNormZ;
        M.transmission = C.transmission; M.emission = C.emission; M.diffRoughness = C.diffRoughness; M.unlit = C.unlit;
        return M;
      }
      return this.FetchMaterialSlow(AtlasPixels, PrimIdx, U, V, P.Transmission, M);
    }

    EvalSky(Rdx, Rdy, Rdz, SkyRad, SunDir, SunIrr, OutRgb, SkyLut = null) {
      if (SkyLut && SkyLut.length >= SkyLutW * SkyLutH * 4) {
        this.SampleSkyViewInto(Rdx, Rdy, Rdz, SkyLut, OutRgb);
        return;
      }
      const Z = Clamp(Rdz * 0.5 + 0.5, 0.0, 1.0);
      const OneMinusZ = 1.0 - Math.abs(Rdz);
      const Hor = OneMinusZ * OneMinusZ * OneMinusZ;
      let R = Mix(SkyRad[0] * 0.28, SkyRad[0] * 1.18, Z) + SkyRad[0] * 0.55 * Hor + 0.08 * Hor;
      let G = Mix(SkyRad[1] * 0.30, SkyRad[1] * 1.22, Z) + SkyRad[1] * 0.52 * Hor + 0.06 * Hor;
      let B = Mix(SkyRad[2] * 0.34, SkyRad[2] * 1.28, Z) + SkyRad[2] * 0.46 * Hor + 0.05 * Hor;
      const SunCos = Rdx * SunDir[0] + Rdy * SunDir[1] + Rdz * SunDir[2];
      if (SunCos > 0.7) {
        const Disc = Smoothstep(0.9994, 0.99992, SunCos);
        const Glow = Math.pow(SunCos, 96.0) * 0.35;
        R += SunIrr[0] * (Disc * 3.5 + Glow);
        G += SunIrr[1] * (Disc * 3.5 + Glow);
        B += SunIrr[2] * (Disc * 3.5 + Glow);
      }
      OutRgb[0] = R; OutRgb[1] = G; OutRgb[2] = B;
    }

    SampleSkyViewInto(Rdx, Rdy, Rdz, SkyLut, OutRgb) {
      const Az = Math.atan2(Rdy, Rdx);
      const El = Math.asin(Clamp(Rdz, -1.0, 1.0));
      let U = Az * (0.5 / PI) + 0.5;
      U = U - Math.floor(U);
      const V = Clamp(0.5 - 0.5 * (El < 0 ? -1 : 1) * Math.sqrt(Math.abs(El) / (PI * 0.5)), 0.0, 1.0);
      const Fx = U * SkyLutW - 0.5;
      const Fy = V * (SkyLutH - 1);
      const X0 = ((Math.floor(Fx) % SkyLutW) + SkyLutW) % SkyLutW;
      const X1 = (X0 + 1) % SkyLutW;
      const Y0 = Math.max(0, Math.min(SkyLutH - 1, Math.floor(Fy)));
      const Y1 = Math.max(0, Math.min(SkyLutH - 1, Y0 + 1));
      const Tx = Fx - Math.floor(Fx);
      const Ty = Fy - Y0;
      const I00 = (Y0 * SkyLutW + X0) * 4;
      const I10 = (Y0 * SkyLutW + X1) * 4;
      const I01 = (Y1 * SkyLutW + X0) * 4;
      const I11 = (Y1 * SkyLutW + X1) * 4;
      const W00 = (1 - Tx) * (1 - Ty), W10 = Tx * (1 - Ty), W01 = (1 - Tx) * Ty, W11 = Tx * Ty;
      OutRgb[0] = SkyLut[I00] * W00 + SkyLut[I10] * W10 + SkyLut[I01] * W01 + SkyLut[I11] * W11;
      OutRgb[1] = SkyLut[I00 + 1] * W00 + SkyLut[I10 + 1] * W10 + SkyLut[I01 + 1] * W01 + SkyLut[I11 + 1] * W11;
      OutRgb[2] = SkyLut[I00 + 2] * W00 + SkyLut[I10 + 2] * W10 + SkyLut[I01 + 2] * W01 + SkyLut[I11 + 2] * W11;
    }

    _Hash31(X, Y, Z) {
      let Qx = Fract(X * 0.1031), Qy = Fract(Y * 0.1031), Qz = Fract(Z * 0.1031);
      const D = Qx * (Qy + 33.33) + Qy * (Qz + 33.33) + Qz * (Qx + 33.33);
      Qx += D; Qy += D; Qz += D;
      return Fract((Qx + Qy) * Qz);
    }

    _Noise3(X, Y, Z) {
      const Ix = Math.floor(X), Iy = Math.floor(Y), Iz = Math.floor(Z);
      const Fx = X - Ix, Fy = Y - Iy, Fz = Z - Iz;
      const Ux = Fx * Fx * (3 - 2 * Fx), Uy = Fy * Fy * (3 - 2 * Fy), Uz = Fz * Fz * (3 - 2 * Fz);
      const A00 = Mix(this._Hash31(Ix, Iy, Iz), this._Hash31(Ix + 1, Iy, Iz), Ux);
      const A10 = Mix(this._Hash31(Ix, Iy + 1, Iz), this._Hash31(Ix + 1, Iy + 1, Iz), Ux);
      const A01 = Mix(this._Hash31(Ix, Iy, Iz + 1), this._Hash31(Ix + 1, Iy, Iz + 1), Ux);
      const A11 = Mix(this._Hash31(Ix, Iy + 1, Iz + 1), this._Hash31(Ix + 1, Iy + 1, Iz + 1), Ux);
      return Mix(Mix(A00, A10, Uy), Mix(A01, A11, Uy), Uz);
    }

    _CloudDens(Px, Py, Pz, BaseM, ThickM, Cov, DensMul, FScale, Anvil, CType, Wx, Wy) {
      const EarthR = 6360000.0;
      const Alt = Math.sqrt(Px * Px + Py * Py + Pz * Pz) - EarthR;
      const H = Clamp((Alt - BaseM) / ThickM, 0, 1);
      let Bot = Smoothstep(0, 0.16, H), Top = Smoothstep(1, 0.72 + 0.18 * Anvil, H);
      if (CType === 1) { Bot = Smoothstep(0, 0.25, H); Top = Smoothstep(1, 0.55, H); }
      else if (CType === 2) { Bot = Smoothstep(0.15, 0.45, H); Top = Smoothstep(0.95, 0.55, H); }
      const Prof = Bot * Top;
      if (Prof <= 0.001) return 0;
      const ShapeCov = Clamp(Cov + Anvil * H * H * 0.25, 0, 1);
      const Str = CType === 2 ? 2.2 : 1.0;
      let Qx = (Px + Wx) * FScale * Str, Qy = (Py + Wy) * FScale * Str, Qz = (Alt - BaseM) * FScale * 1.45;
      let Sum = 0, Amp = 0.52;
      for (let O = 0; O < 3; ++O) {
        Sum += Amp * this._Noise3(Qx, Qy, Qz);
        Qx = Qx * 2.07 + 13.1; Qy = Qy * 2.07 + 7.7; Qz = Qz * 2.07 + 5.3;
        Amp *= 0.48;
      }
      const NNorm = Clamp((Sum - 0.28) / 0.44, 0, 1);
      return Math.max(0, (NNorm - (1 - ShapeCov)) / Math.max(0.18, ShapeCov)) * Prof * DensMul;
    }

    EvalSkyPrimary(Rdx, Rdy, Rdz, CompiledScene, SkyLut, MoonAtlas, TimeSec, Px, Py, OutRgb) {
      const { SkyRadiance, SunDir, SunIrradiance, SunAngularRadiusRad } = CompiledScene;
      this.EvalSky(Rdx, Rdy, Rdz, SkyRadiance, SunDir, SunIrradiance, OutRgb, SkyLut);
      let R = OutRgb[0], G = OutRgb[1], B = OutRgb[2];

      // Sun disc + limb darkening
      const SunAng = Math.max(0.002, SunAngularRadiusRad || 0.01);
      const CosS = Rdx * SunDir[0] + Rdy * SunDir[1] + Rdz * SunDir[2];
      if (Rdz > -0.02 && CosS > 0.7) {
        const Edge = Smoothstep(Math.cos(SunAng * 1.25), Math.cos(SunAng * 0.65), CosS);
        const Mu = Clamp((CosS - Math.cos(SunAng)) / Math.max(1e-5, 1.0 - Math.cos(SunAng)), 0, 1);
        const Limb = 0.65 + 0.35 * Math.pow(Mu, 0.45);
        const Aur = Math.pow(Math.max(0, CosS), 160.0) * 0.25;
        const F = Edge * Limb * 4.2 + Aur;
        R += SunIrradiance[0] * F;
        G += SunIrradiance[1] * F;
        B += SunIrradiance[2] * F;
      }

      // Moon disc + glow
      const Mn = CompiledScene.Moon;
      if (Mn?.visible && Rdz > -0.05) {
        const Md = Mn.dir || [-0.5, 0.4, 0.45];
        const AngR = Math.max(0.002, Mn.sizeRad ?? 0.012);
        const CosM = Rdx * Md[0] + Rdy * Md[1] + Rdz * Md[2];
        const Mt = Mn.tint || [0.94, 0.93, 0.90];
        const Mb = Mn.bright ?? 1.6;
        const Mg = Math.pow(Math.max(0, CosM), 420.0) * (Mn.glow ?? 0.8) * 0.28 * Mb;
        R += Mt[0] * Mg; G += Mt[1] * Mg; B += Mt[2] * Mg;
        if (CosM > Math.cos(AngR)) {
          const Uz = Math.abs(Md[2]) < 0.98 ? 1 : 0, Ux = Uz ? 0 : 1;
          let Rx = 0 * Md[2] - Uz * Md[1], Ry = Uz * Md[0] - Ux * Md[2], Rz = Ux * Md[1] - 0 * Md[0];
          const RL = Math.max(1e-6, Len3(Rx, Ry, Rz));
          Rx /= RL; Ry /= RL; Rz /= RL;
          const Ux2 = Md[1] * Rz - Md[2] * Ry, Uy2 = Md[2] * Rx - Md[0] * Rz, Uz2 = Md[0] * Ry - Md[1] * Rx;
          const Mpx = (Rdx * Rx + Rdy * Ry + Rdz * Rz) / AngR;
          const Mpy = (Rdx * Ux2 + Rdy * Uy2 + Rdz * Uz2) / AngR;
          const R2 = Mpx * Mpx + Mpy * Mpy;
          if (R2 < 1.0) {
            const Mpz = Math.sqrt(Math.max(0, 1.0 - R2));
            const PhA = ((Mn.phase ?? 0.62) - 0.5) * 6.2831853;
            let Lx = Math.sin(PhA), Ly = 0.18, Lz = Math.cos(PhA);
            const LL = Len3(Lx, Ly, Lz);
            Lx /= LL; Ly /= LL; Lz /= LL;
            const Ndl = Math.max(0, Mpx * Lx + Mpy * Ly + Mpz * Lz);
            let AlbR = 0.68 + 0.32 * this._Noise3(Mpx * 3.8 + 4.1, Mpy * 3.8 - 2.3, Mpz * 3.8);
            let AlbG = AlbR, AlbB = AlbR;
            if (MoonAtlas?.ready && MoonAtlas.pixels) {
              const Mw = MoonAtlas.width || 512, Mh = MoonAtlas.height || 1536;
              const UMoon = Fract(Math.atan2(Mpx, Mpz) * (0.5 / PI) + 0.5);
              const VMoon = Clamp(0.5 - Math.asin(Clamp(Mpy, -1, 1)) / PI, 0.015, 0.985);
              const TileV = (Clamp(Mn.preset ?? 0, 0, 5) + VMoon) / 6.0;
              const Tx = Math.min(Mw - 1, Math.max(0, (UMoon * Mw) | 0));
              const Ty = Math.min(Mh - 1, Math.max(0, (TileV * Mh) | 0));
              const Off = (Ty * Mw + Tx) * 4;
              AlbR = Math.pow(MoonAtlas.pixels[Off] / 255.0, 2.2) * 1.45;
              AlbG = Math.pow(MoonAtlas.pixels[Off + 1] / 255.0, 2.2) * 1.45;
              AlbB = Math.pow(MoonAtlas.pixels[Off + 2] / 255.0, 2.2) * 1.45;
            }
            const Lum = (0.014 + Ndl * 1.55) * Mb * Clamp((1 - Math.sqrt(R2)) * 14, 0, 1);
            R += Mt[0] * AlbR * Lum; G += Mt[1] * AlbG * Lum; B += Mt[2] * AlbB * Lum;
          }
        }
      }

      // Stars
      const St = CompiledScene.Stars;
      if (St?.enabled && Rdz > 0.005) {
        const DayFade = Clamp(1.0 - (SunDir[2] + 0.08) * 5.0, 0, 1);
        if (DayFade > 0.01) {
          const Rot = St.rotationRad ?? 0;
          const Cr = Math.cos(Rot), Sr = Math.sin(Rot);
          const Gx = (Rdx * Cr - Rdy * Sr) * 175.0, Gy = (Rdx * Sr + Rdy * Cr) * 175.0, Gz = Rdz * 175.0;
          const Cx = Math.floor(Gx), Cy = Math.floor(Gy), Cz = Math.floor(Gz);
          const Hx = this._Hash31(Cx, Cy, Cz);
          if (Hx > 1.0 - 0.18 * Clamp((St.limMag ?? 5.5) / 8.5, 0.05, 0.98)) {
            const Hy = this._Hash31(Cx + 17.3, Cy - 5.1, Cz + 9.7);
            const Hz = this._Hash31(Cx - 3.9, Cy + 11.4, Cz - 2.2);
            const Dx = Gx - (Cx + 0.2 + 0.6 * Hy);
            const Dy = Gy - (Cy + 0.2 + 0.6 * Hz);
            const Dz = Gz - (Cz + 0.2 + 0.6 * Fract(Hx * 7.13));
            const D2 = Dx * Dx + Dy * Dy + Dz * Dz;
            const Rad = 0.08 * Clamp(St.pointSize ?? 1.0, 0.4, 3.0);
            if (D2 < Rad * Rad) {
              const Fall = Math.exp(-D2 / (Rad * Rad * 0.35));
              const Tw = St.twinkle !== false ? 1 - (St.twinkleDepth ?? 30) * 0.008 * (0.5 + 0.5 * Math.sin(TimeSec * (St.twinkleRate ?? 1.2) * 5 + Hy * 40)) : 1;
              const Int = (0.45 + 1.55 * Hz * Hz) * (St.brightness ?? 1.0) * DayFade * Tw * Fall * Clamp(Rdz * 4, 0, 1);
              R += (0.78 + 0.42 * Hy) * Int;
              G += (0.88 + 0.12 * Hy) * Int;
              B += (1.20 - 0.35 * Hy) * Int;
            }
          }
        }
      }

      // Volumetric 3D Clouds
      const Cl = CompiledScene.Clouds;
      if (Cl?.enabled && Rdz > 0.006 && (Cl.coverage ?? 0) > 0.01 && (Cl.density ?? 0) > 0.01) {
        const EarthR = 6360000.0, OrigZ = EarthR + 100.0;
        const BaseM = Math.max(200, Cl.baseM ?? 1400), ThickM = Math.max(150, Cl.thicknessM ?? 1100);
        const Sh = (Rad) => {
          const B0 = OrigZ * Rdz, C0 = OrigZ * OrigZ - Rad * Rad, D0 = B0 * B0 - C0;
          return D0 < 0 ? -1 : -B0 + Math.sqrt(D0);
        };
        const T0 = Sh(EarthR + BaseM), T1 = Math.min(Sh(EarthR + BaseM + ThickM), 45000);
        if (T0 >= 0 && T1 > T0) {
          const Steps = 10, Dt = (T1 - T0) / Steps;
          const Cov = Clamp(Cl.coverage ?? 0.42, 0, 1);
          const DensMul = Math.max(0, Cl.density ?? 2.2) * 0.008;
          const FScale = 0.00065 / Math.max(0.08, Cl.featureScale ?? 0.4);
          const Anvil = Clamp(Cl.anvil ?? 0.5, 0, 1);
          const Gp = Clamp(Cl.anisotropy ?? 0.48, -0.5, 0.9), G2 = Gp * Gp;
          const CosT = Rdx * SunDir[0] + Rdy * SunDir[1] + Rdz * SunDir[2];
          const Phase = 0.78 * ((1 - G2) / (4 * PI * Math.pow(Math.max(1e-3, 1 + G2 - 2 * Gp * CosT), 1.5)))
            + 0.22 * ((1 - 0.0625) / (4 * PI * Math.pow(Math.max(1e-3, 1 + 0.0625 + 0.5 * CosT), 1.5)));
          const Wx = (Cl.windX ?? 6) * TimeSec, Wy = (Cl.windY ?? 2) * TimeSec;
          const Jitter = Fract(Math.sin(Px * 12.9898 + Py * 78.233 + TimeSec * 7.1) * 43758.5453);
          const SStep = (ThickM * 0.55) / 2.0;
          let Trans = 1.0, Ar = 0, Ag = 0, Ab = 0;
          for (let I = 0; I < Steps; ++I) {
            const T = T0 + (I + 0.15 + 0.7 * Jitter) * Dt;
            const Cx = Rdx * T, Cy = Rdy * T, Cz = OrigZ + Rdz * T;
            const D = this._CloudDens(Cx, Cy, Cz, BaseM, ThickM, Cov, DensMul, FScale, Anvil, Cl.type | 0, Wx, Wy);
            if (D > 1e-6) {
              let Tau = 0;
              if (Cl.shadowEnabled !== false) {
                for (let S = 1; S <= 2; ++S) {
                  Tau += this._CloudDens(Cx + SunDir[0] * S * SStep, Cy + SunDir[1] * S * SStep, Cz + SunDir[2] * S * SStep, BaseM, ThickM, Cov, DensMul, FScale, Anvil, Cl.type | 0, Wx, Wy) * SStep;
                }
              }
              const SunAtt = Math.exp(-Tau * 1.15) * (0.35 + 0.65 * (1 - Math.exp(-Tau * 2.2 + 0.25)));
              const SegT = Math.exp(-D * Dt);
              const H = Clamp((Math.sqrt(Cx * Cx + Cy * Cy + Cz * Cz) - EarthR - BaseM) / ThickM, 0, 1);
              const Amb = 0.28 + 0.52 * H;
              Ar += Trans * (SunIrradiance[0] * SunAtt * Phase * 2.4 + OutRgb[0] * Amb * 0.55) * (1 - SegT);
              Ag += Trans * (SunIrradiance[1] * SunAtt * Phase * 2.4 + OutRgb[1] * Amb * 0.55) * (1 - SegT);
              Ab += Trans * (SunIrradiance[2] * SunAtt * Phase * 2.4 + OutRgb[2] * Amb * 0.55) * (1 - SegT);
              Trans *= SegT;
              if (Trans < 0.02) break;
            }
          }
          const Haze = 1 - Math.exp(-T0 * 0.00004);
          R = R * Trans + Mix(Ar, OutRgb[0] * (1 - Trans), Haze);
          G = G * Trans + Mix(Ag, OutRgb[1] * (1 - Trans), Haze);
          B = B * Trans + Mix(Ab, OutRgb[2] * (1 - Trans), Haze);
        }
      }
      OutRgb[0] = R; OutRgb[1] = G; OutRgb[2] = B;
    }

    ShadeSecondaryBounce(Prims, Lights, AtlasPixels, Hit, Vx, Vy, Vz, SkyRad, SunDir, SunIrr, OutRgb, CompiledScene = null) {
      const M = Hit.isVehicle
        ? this.BuildVehicleMaterialInto(Hit, this._SecMat)
        : this.FetchMaterialInto(Prims, AtlasPixels, Hit.primIdx, Hit.u, Hit.v, this._SecMat);
      if (M.unlit) {
        const E = 1.0 + M.emission;
        OutRgb[0] = M.baseR * E; OutRgb[1] = M.baseG * E; OutRgb[2] = M.baseB * E;
        return;
      }
      const Nx = Hit.nx, Ny = Hit.ny, Nz = Hit.nz;
      let R = M.baseR * M.emission, G = M.baseG * M.emission, B = M.baseB * M.emission;

      const NSun = Math.max(0.0, Nx * SunDir[0] + Ny * SunDir[1] + Nz * SunDir[2]);
      if (NSun > 0.0) {
        let Hx = Vx + SunDir[0], Hy = Vy + SunDir[1], Hz = Vz + SunDir[2];
        const Hl = Math.max(1e-6, Len3(Hx, Hy, Hz));
        Hx /= Hl; Hy /= Hl; Hz /= Hl;
        const Spec = Math.pow(Math.max(0, Nx * Hx + Ny * Hy + Nz * Hz), Mix(16.0, 256.0, 1.0 - M.roughness)) * 0.45;
        const Fr = Mix(0.04, M.baseR, M.metallic);
        const Fg = Mix(0.04, M.baseG, M.metallic);
        const Fb = Mix(0.04, M.baseB, M.metallic);
        const RoomOpen = 0.45 + 0.55 * Smoothstep(-2.4, 0.2, -Math.abs(Hit.px) + 1.1);
        const Diff = ((1.0 - M.metallic) / PI) * NSun;
        R += (M.baseR * Diff + Fr * Spec) * SunIrr[0] * RoomOpen;
        G += (M.baseG * Diff + Fg * Spec) * SunIrr[1] * RoomOpen;
        B += (M.baseB * Diff + Fb * Spec) * SunIrr[2] * RoomOpen;
      }

      const MoonIrr = [0, 0, 0];
      if (this.EvalMoonIrradiance(CompiledScene, MoonIrr)) {
        const Md = CompiledScene.Moon.dir;
        const NMoon = Math.max(0.0, Nx * Md[0] + Ny * Md[1] + Nz * Md[2]);
        if (NMoon > 0.0) {
          const K = ((1.0 - M.metallic) / PI) * NMoon;
          R += M.baseR * MoonIrr[0] * K;
          G += M.baseG * MoonIrr[1] * K;
          B += M.baseB * MoonIrr[2] * K;
        }
      }

      for (let Li = 0; Li < Lights.length; ++Li) {
        const L0 = Lights[Li];
        const Tlx = L0.Cx - Hit.px, Tly = L0.Cy - Hit.py, Tlz = L0.Cz - Hit.pz;
        const D2 = Math.max(0.25, Tlx * Tlx + Tly * Tly + Tlz * Tlz);
        const InvD = 1.0 / Math.sqrt(D2);
        const Ldx = Tlx * InvD, Ldy = Tly * InvD, Ldz = Tlz * InvD;
        const Area = 4.0 * L0.Hw * L0.Hh;
        const CEmit = L0.Omni ? 0.65 : Math.max(0.0, -(Ldx * L0.Nx + Ldy * L0.Ny + Ldz * L0.Nz));
        const K = ((1.0 - M.metallic) / PI) * (Math.max(0, Nx * Ldx + Ny * Ldy + Nz * Ldz) * CEmit * Area * 1.45) / D2;
        R += M.baseR * L0.RadR * K;
        G += M.baseG * L0.RadG * K;
        B += M.baseB * L0.RadB * K;
      }

      const SecIdx = Hit.isVehicle ? -1 : Hit.primIdx;
      for (let Ei = 0; Ei < Prims.length; ++Ei) {
        if (Ei === SecIdx) continue;
        const Ep = Prims[Ei];
        if (Ep.Emission > 0.05) {
          const Ex = Ep.Cx - Hit.px, Ey = Ep.Cy - Hit.py, Ez = Ep.Cz - Hit.pz;
          const D2e = Math.max(0.08, Ex * Ex + Ey * Ey + Ez * Ez);
          const InvE = 1.0 / Math.sqrt(D2e);
          const Edx = Ex * InvE, Edy = Ey * InvE, Edz = Ez * InvE;
          const Ne = Math.max(0, Nx * Edx + Ny * Edy + Nz * Edz);
          if (Ne > 0) {
            const Re = Math.max(0.08, (Ep.Hx + Ep.Hy + Ep.Hz) * 0.3333333);
            const Omega = Math.min(1.6, (Re * Re) / Math.max(0.05, D2e - Re * Re * 0.4));
            const K = ((1.0 - M.metallic) / PI) * Omega * Ne * 1.45 * Ep.Emission;
            R += M.baseR * Ep.BounceR * K;
            G += M.baseG * Ep.BounceG * K;
            B += M.baseB * Ep.BounceB * K;
          }
        }
      }

      const SkyTmp = this._Tmp4;
      this.EvalSky(Nx, Ny, Nz, SkyRad, SunDir, SunIrr, SkyTmp);
      const StarTmp = [0, 0, 0];
      this.EvalStarlightAmbient(Nx, Ny, Nz, SunDir, StarTmp, CompiledScene);
      const DayF = Clamp(SunDir[2] * 4.0 + 0.25, 0.0, 1.0);
      const AmbR = SkyTmp[0] * 0.35 + StarTmp[0] + (0.16 * Math.max(-Nx, 0) + 0.015 * Math.max(Nx, 0) + 0.12) * DayF;
      const AmbG = SkyTmp[1] * 0.35 + StarTmp[1] + (0.015 * Math.max(-Nx, 0) + 0.15 * Math.max(Nx, 0) + 0.12) * DayF;
      const AmbB = SkyTmp[2] * 0.35 + StarTmp[2] + (0.012 * Math.max(-Nx, 0) + 0.025 * Math.max(Nx, 0) + 0.12) * DayF;
      const Kd = (1.0 - M.metallic * 0.65) * M.ao;
      OutRgb[0] = R + M.baseR * Kd * AmbR;
      OutRgb[1] = G + M.baseG * Kd * AmbG;
      OutRgb[2] = B + M.baseB * Kd * AmbB;
    }

    TraceLightQuadT(Lights, Rox, Roy, Roz, Rdx, Rdy, Rdz, TMax, OutEmit) {
      let BestT = TMax;
      let HitIdx = -1;
      if (Math.abs(Rdz) > 1e-5) {
        for (let Li = 0; Li < Lights.length; ++Li) {
          const L = Lights[Li];
          if (L.QuadVisible === false) continue;
          const Tq = (L.Cz - Roz) / Rdz;
          if (Tq > 0.002 && Tq < BestT) {
            if (Math.abs(Rox + Rdx * Tq - L.Cx) <= L.Hw && Math.abs(Roy + Rdy * Tq - L.Cy) <= L.Hh) {
              BestT = Tq;
              HitIdx = Li;
            }
          }
        }
      }
      if (HitIdx >= 0) {
        const L = Lights[HitIdx];
        OutEmit[0] = L.RadR * 1.15;
        OutEmit[1] = L.RadG * 1.15;
        OutEmit[2] = L.RadB * 1.15;
        OutEmit[3] = HitIdx;
      }
      return BestT;
    }

    // Render horizontal band [StartRow, EndRow) into compact band-local buffers (offset 0 = StartRow * Width)
    // or full-frame buffers when OutputIsBandLocal === false.
    RenderBand(Params, RgbaOut, DepthOut, EntityOut, OutputIsBandLocal = true) {
      const {
        CompiledScene,
        AtlasPixels,
        VehicleBvhData = null,
        Width,
        Height,
        StartRow,
        EndRow,
        ViewModeCode,
        ShowGrid,
        TransparentBackground = false,
        SkyLut = null,
        MoonAtlas = null,
        TimeSec = 0,
      } = Params;

      const { Prims, Lights } = this.PackScene(CompiledScene, AtlasPixels);
      const {
        CameraOrigin, CameraTarget, WorldUp,
        SunDir, SunIrradiance, SunAngularRadiusRad, SkyRadiance, ExposureGain,
      } = CompiledScene;
      const Rs = CompiledScene.RenderSettings || {};
      const EngineCodeMap = {
        matcap: 0,
        openpbr_direct: 1,
        gtx_sdf_gi: 2,
        highend_bvh4_gi: 3,
        pt_realtime: 4,
        pt_final: 5,
      };
      const PenumbraCodeMap = {
        point_sdf: 0,
        analytic_cone: 1,
        voxel_clipmap: 2,
        voxel_cone_gi: 3,
        hybrid_sobol_cap: 4,
        sdf_mc_ref: 5,
      };
      const EngineMode = EngineCodeMap[Rs.RenderEngine] ?? 2;
      const PenumbraMode = PenumbraCodeMap[Rs.SdfPenumbraMode] ?? 3;
      const ShadowRays = Clamp((Rs.ShadowRays ?? 4) | 0, 1, 12);
      const ShadowSoftness = Rs.ShadowSoftness ?? 1.0;
      const EmissiveShadowed = Rs.EmissiveShadowMode !== "unshadowed";
      const BaseGiAlg = Rs.GiAlgorithm === "off" ? 2 : Rs.GiAlgorithm === "bvh" ? 1 : 0;
      const GiAlg = EngineMode === 1 ? 2 : EngineMode >= 3 ? 1 : BaseGiAlg;
      const GiSamples = EngineMode >= 4 ? (EngineMode === 5 ? 16 : 10) : Clamp((Rs.GiSamples ?? 6) | 0, 2, 16);
      const GiIntensity = (Rs.GiIntensity ?? 1.0) * (PenumbraMode === 3 && EngineMode === 2 && Lights.length > 0 ? 1.06 : 1.0);
      const FillBoost = Rs.FillBoost ?? 1.0;
      const ReflOff = Rs.ReflectionAlgorithm === "env";
      const SinSun = Math.sin(SunAngularRadiusRad || 0.009) * ShadowSoftness;

      let FwdX = CameraTarget[0] - CameraOrigin[0];
      let FwdY = CameraTarget[1] - CameraOrigin[1];
      let FwdZ = CameraTarget[2] - CameraOrigin[2];
      const FLen = Math.max(1e-6, Len3(FwdX, FwdY, FwdZ));
      FwdX /= FLen; FwdY /= FLen; FwdZ /= FLen;

      let Rx = FwdY * WorldUp[2] - FwdZ * WorldUp[1];
      let Ry = FwdZ * WorldUp[0] - FwdX * WorldUp[2];
      let Rz = FwdX * WorldUp[1] - FwdY * WorldUp[0];
      const RLen = Math.max(1e-6, Len3(Rx, Ry, Rz));
      Rx /= RLen; Ry /= RLen; Rz /= RLen;

      const Ux = Ry * FwdZ - Rz * FwdY;
      const Uy = Rz * FwdX - Rx * FwdZ;
      const Uz = Rx * FwdY - Ry * FwdX;

      const Aspect = Width / Math.max(1, Height);
      const TanHalfFov = Math.tan(CompiledScene.VerticalFovRad * 0.5);
      const OrthoH = CompiledScene.OrthoHalfHeight;

      const SkyOut = [0, 0, 0];
      const SecOut = [0, 0, 0];
      const QuadOut = [0, 0, 0, 0];
      const RowBaseOffset = OutputIsBandLocal ? StartRow * Width : 0;

      for (let Py = StartRow; Py < EndRow; ++Py) {
        const NdcY = 1.0 - ((Py + 0.5) / Height) * 2.0;
        for (let Px = 0; Px < Width; ++Px) {
          const NdcX = ((Px + 0.5) / Width) * 2.0 - 1.0;
          let Rox, Roy, Roz, Rdx, Rdy, Rdz;
          if (OrthoH > 0.0) {
            Rox = CameraOrigin[0] + Rx * (NdcX * OrthoH * Aspect) + Ux * (NdcY * OrthoH);
            Roy = CameraOrigin[1] + Ry * (NdcX * OrthoH * Aspect) + Uy * (NdcY * OrthoH);
            Roz = CameraOrigin[2] + Rz * (NdcX * OrthoH * Aspect) + Uz * (NdcY * OrthoH);
            Rdx = FwdX; Rdy = FwdY; Rdz = FwdZ;
          } else {
            Rox = CameraOrigin[0]; Roy = CameraOrigin[1]; Roz = CameraOrigin[2];
            Rdx = FwdX + Rx * (NdcX * Aspect * TanHalfFov) + Ux * (NdcY * TanHalfFov);
            Rdy = FwdY + Ry * (NdcX * Aspect * TanHalfFov) + Uy * (NdcY * TanHalfFov);
            Rdz = FwdZ + Rz * (NdcX * Aspect * TanHalfFov) + Uz * (NdcY * TanHalfFov);
            const InvD = 1.0 / Len3(Rdx, Rdy, Rdz);
            Rdx *= InvD; Rdy *= InvD; Rdz *= InvD;
          }

          const Hit = this.TraceScene(Prims, VehicleBvhData, Rox, Roy, Roz, Rdx, Rdy, Rdz, 50.0, -1, false, this._Hit);
          const TQuad = this.TraceLightQuadT(Lights, Rox, Roy, Roz, Rdx, Rdy, Rdz, Hit.t, QuadOut);

          const PixIdx = Py * Width + Px - RowBaseOffset;
          let LinR = 0, LinG = 0, LinB = 0;
          let EntityCode = 0;
          let HitDist = 50.0;

          if (ViewModeCode >= 10) {
            const Dbg = ViewModeCode - 10;
            if (Hit.hit) {
              EntityCode = Hit.isVehicle ? Hit.vehPickCode : (Prims[Hit.primIdx].PickCode || (1 + Hit.primIdx));
              HitDist = Hit.t;
              const Mat = Hit.isVehicle
                ? this.BuildVehicleMaterialInto(Hit, this._Mat)
                : this.FetchMaterialInto(Prims, AtlasPixels, Hit.primIdx, Hit.u, Hit.v, this._Mat);
              if (Dbg === 1) {
                const Dv = 1.0 - Clamp(Hit.t / 12.0, 0, 1);
                LinR = Dv; LinG = Dv; LinB = Dv;
              } else if (Dbg === 2) {
                LinR = Fract(EntityCode * 0.37); LinG = Fract(EntityCode * 0.61); LinB = Fract(EntityCode * 0.83);
              } else if (Dbg === 3) {
                LinR = 0.5 + 0.5 * Rdx; LinG = 0.5 + 0.5 * Rdy; LinB = 0.5;
              } else if (Dbg === 4) {
                const Ci = Hit.isVehicle ? (Hit.vehMatId + 17) : (Hit.primIdx + 1);
                LinR = Fract(Ci * 0.29); LinG = Fract(Ci * 0.53); LinB = Fract(Ci * 0.79);
              } else if (Dbg === 5) {
                const Hv = Math.floor((1.0 - Clamp(Hit.t / 14.0, 0, 1)) * 8.0) / 8.0;
                LinR = Hv; LinG = Hv; LinB = Hv;
              } else if (Dbg === 6) {
                LinR = Math.pow(Clamp(Mat.baseR, 0, 1), 1 / 2.2);
                LinG = Math.pow(Clamp(Mat.baseG, 0, 1), 1 / 2.2);
                LinB = Math.pow(Clamp(Mat.baseB, 0, 1), 1 / 2.2);
              } else if (Dbg === 7 || Dbg === 10) {
                LinR = Hit.nx * 0.5 + 0.5; LinG = Hit.ny * 0.5 + 0.5; LinB = Hit.nz * 0.5 + 0.5;
              } else if (Dbg === 8) {
                LinR = Mat.roughness; LinG = Mat.roughness; LinB = Mat.roughness;
              } else if (Dbg === 9) {
                LinR = Mat.metallic; LinG = Mat.metallic; LinB = Mat.metallic;
              } else if (Dbg === 11) {
                const Rm = Clamp(1.0 - Mat.roughness * 0.6, 0.2, 1.0);
                LinR = 0.15 * Rm; LinG = 0.78 * Rm; LinB = 0.52 * Rm;
              } else if (Dbg === 12) {
                const Rw = Clamp(Math.max(0.08, Hit.nx * SunDir[0] + Hit.ny * SunDir[1] + Hit.nz * SunDir[2]), 0, 1);
                LinR = Rw; LinG = Rw; LinB = Rw;
              } else if (Dbg === 13) {
                const Ra = Clamp(Mat.ao, 0.1, 1.0);
                LinR = 0.28 * Ra; LinG = 0.56 * Ra; LinB = 0.96 * Ra;
              } else if (Dbg === 14) {
                const Par = (((Px >> 4) + (Py >> 4)) & 1) ? 0.35 : 0.0;
                LinR = Mix(Mat.baseR * 0.55, 0.95, Par);
                LinG = Mix(Mat.baseG * 0.55, 0.68, Par);
                LinB = Mix(Mat.baseB * 0.55, 0.22, Par);
              } else {
                const Gx = Math.abs(Fract(Hit.px * 4) - 0.5);
                const Gy = Math.abs(Fract(Hit.py * 4) - 0.5);
                const Gz = Math.abs(Fract(Hit.pz * 4) - 0.5);
                const W = Math.min(Gx, Gy, Gz) < 0.04 ? 1.0 : 0.0;
                LinR = Mix(Hit.nx * 0.3 + 0.3, 0.38, W);
                LinG = Mix(Hit.ny * 0.3 + 0.3, 0.84, W);
                LinB = Mix(Hit.nz * 0.3 + 0.3, 1.0, W);
              }
            } else {
              LinR = 0.06; LinG = 0.07; LinB = 0.09;
            }
            DepthOut[PixIdx] = HitDist;
            EntityOut[PixIdx] = EntityCode;
            const O4 = PixIdx * 4;
            RgbaOut[O4] = Clamp(LinR * 255, 0, 255);
            RgbaOut[O4 + 1] = Clamp(LinG * 255, 0, 255);
            RgbaOut[O4 + 2] = Clamp(LinB * 255, 0, 255);
            RgbaOut[O4 + 3] = 255;
            continue;
          }

          if (TQuad < Hit.t) {
            LinR = QuadOut[0]; LinG = QuadOut[1]; LinB = QuadOut[2];
            const LiHit = QuadOut[3] | 0;
            EntityCode = Lights[LiHit]?.PickCode || (112 + LiHit);
            HitDist = TQuad;
          } else if (Hit.hit) {
            const P = Hit.isVehicle ? null : Prims[Hit.primIdx];
            EntityCode = Hit.isVehicle ? Hit.vehPickCode : (P.PickCode || (1 + Hit.primIdx));
            HitDist = Hit.t;
            const Mat = Hit.isVehicle
              ? this.BuildVehicleMaterialInto(Hit, this._Mat)
              : this.FetchMaterialInto(Prims, AtlasPixels, Hit.primIdx, Hit.u, Hit.v, this._Mat);
            const PrimEmission = P ? P.Emission : 0.0;

            const Bx = Hit.ny * Hit.tz - Hit.nz * Hit.ty;
            const By = Hit.nz * Hit.tx - Hit.nx * Hit.tz;
            const Bz = Hit.nx * Hit.ty - Hit.ny * Hit.tx;
            let Nx = Hit.tx * Mat.normX + Bx * Mat.normY + Hit.nx * Mat.normZ;
            let Ny = Hit.ty * Mat.normX + By * Mat.normY + Hit.ny * Mat.normZ;
            let Nz = Hit.tz * Mat.normX + Bz * Mat.normY + Hit.nz * Mat.normZ;
            const NL = Math.max(1e-6, Len3(Nx, Ny, Nz));
            Nx /= NL; Ny /= NL; Nz /= NL;
            if (Nx * Hit.nx + Ny * Hit.ny + Nz * Hit.nz < 0.08) {
              Nx = Hit.nx; Ny = Hit.ny; Nz = Hit.nz;
            }

            if (ViewModeCode === 2) {
              LinR = Nx * 0.5 + 0.5; LinG = Ny * 0.5 + 0.5; LinB = Nz * 0.5 + 0.5;
            } else if (Mat.unlit && ViewModeCode === 0) {
              const Boost = 1.0 + PrimEmission + Mat.emission;
              LinR = Mat.baseR * Boost; LinG = Mat.baseG * Boost; LinB = Mat.baseB * Boost;
            } else if (ViewModeCode === 3) {
              const EdgeU = Math.min(Fract(Hit.u * 8), 1 - Fract(Hit.u * 8));
              const EdgeV = Math.min(Fract(Hit.v * 8), 1 - Fract(Hit.v * 8));
              const Wire = Math.min(EdgeU, EdgeV) < 0.045 ? 1.0 : 0.08;
              LinR = Mat.baseR * Wire + (Wire > 0.5 ? 0.22 : 0);
              LinG = Mat.baseG * Wire + (Wire > 0.5 ? 0.38 : 0);
              LinB = Mat.baseB * Wire + (Wire > 0.5 ? 0.55 : 0);
            } else if (P && P.GlassShell && ViewModeCode === 0) {
              // Fast 1-ray 2-layer glass refraction + analytical Crown-Glass Abbe dispersion (exact match to GTX_FRAGMENT_SHADER!)
              const Vx = -Rdx, Vy = -Rdy, Vz = -Rdz;
              const CosView = Math.max(0, Nx * Vx + Ny * Vy + Nz * Vz);
              const BaseIor = Math.max(1.05, Mat.ior);
              const F = FresnelDielectric(CosView, BaseIor);
              const DotRN = Rdx * Nx + Rdy * Ny + Rdz * Nz;
              const RflX = Rdx - 2.0 * DotRN * Nx;
              const RflY = Rdy - 2.0 * DotRN * Ny;
              const RflZ = Rdz - 2.0 * DotRN * Nz;

              let ReflR = 0, ReflG = 0, ReflB = 0;
              const RHit = this.TraceScene(
                Prims, VehicleBvhData, Hit.px + Nx * 0.012, Hit.py + Ny * 0.012, Hit.pz + Nz * 0.012,
                RflX, RflY, RflZ, 26.0, Hit.primIdx, true, this._SecHit,
              );
              const TqRefl = this.TraceLightQuadT(
                Lights, Hit.px + Nx * 0.012, Hit.py + Ny * 0.012, Hit.pz + Nz * 0.012,
                RflX, RflY, RflZ, RHit.t, QuadOut,
              );
              if (TqRefl < RHit.t) {
                ReflR = QuadOut[0]; ReflG = QuadOut[1]; ReflB = QuadOut[2];
              } else if (RHit.hit) {
                this.ShadeSecondaryBounce(Prims, Lights, AtlasPixels, RHit, -RflX, -RflY, -RflZ, SkyRadiance, SunDir, SunIrradiance, SecOut);
                ReflR = SecOut[0]; ReflG = SecOut[1]; ReflB = SecOut[2];
              } else {
                this.EvalSky(RflX, RflY, RflZ, SkyRadiance, SunDir, SunIrradiance, SkyOut);
                ReflR = SkyOut[0]; ReflG = SkyOut[1]; ReflB = SkyOut[2];
              }

              const Eta = 1.0 / BaseIor;
              const CosI = -DotRN;
              const K2 = 1.0 - Eta * Eta * (1.0 - CosI * CosI);
              let ExPosX = Hit.px + Rdx * 0.02, ExPosY = Hit.py + Rdy * 0.02, ExPosZ = Hit.pz + Rdz * 0.02;
              let ExDirX = Rdx, ExDirY = Rdy, ExDirZ = Rdz;
              let PathThickness = 0.06;

              if (K2 > 0.0) {
                const SqK = Math.sqrt(K2);
                const TInX = Eta * Rdx + (Eta * CosI - SqK) * Nx;
                const TInY = Eta * Rdy + (Eta * CosI - SqK) * Ny;
                const TInZ = Eta * Rdz + (Eta * CosI - SqK) * Nz;
                const Bx0 = Hit.px - Nx * 0.008, By0 = Hit.py - Ny * 0.008, Bz0 = Hit.pz - Nz * 0.008;
                const TExit = this.IntersectPrimT(P, Bx0, By0, Bz0, TInX, TInY, TInZ, 6.0, true);
                if (TExit < 6.0) {
                  PathThickness = TExit;
                  const ExHit = this.FinalizeHit(Prims, Hit.primIdx, Bx0, By0, Bz0, TInX, TInY, TInZ, TExit, true, this._SecHit);
                  ExPosX = ExHit.px; ExPosY = ExHit.py; ExPosZ = ExHit.pz;
                  const Cos2 = -(TInX * ExHit.nx + TInY * ExHit.ny + TInZ * ExHit.nz);
                  const KOut = 1.0 - BaseIor * BaseIor * (1.0 - Cos2 * Cos2);
                  if (KOut > 0.0) {
                    const SqO = Math.sqrt(KOut);
                    ExDirX = BaseIor * TInX + (BaseIor * Cos2 - SqO) * ExHit.nx;
                    ExDirY = BaseIor * TInY + (BaseIor * Cos2 - SqO) * ExHit.ny;
                    ExDirZ = BaseIor * TInZ + (BaseIor * Cos2 - SqO) * ExHit.nz;
                  } else {
                    const D2b = TInX * ExHit.nx + TInY * ExHit.ny + TInZ * ExHit.nz;
                    ExDirX = TInX - 2.0 * D2b * ExHit.nx;
                    ExDirY = TInY - 2.0 * D2b * ExHit.ny;
                    ExDirZ = TInZ - 2.0 * D2b * ExHit.nz;
                  }
                } else {
                  PathThickness = 0.35;
                  ExPosX = Hit.px + TInX * 0.35;
                  ExPosY = Hit.py + TInY * 0.35;
                  ExPosZ = Hit.pz + TInZ * 0.35;
                  ExDirX = TInX; ExDirY = TInY; ExDirZ = TInZ;
                }
              }

              const BgOx = ExPosX + ExDirX * 0.012, BgOy = ExPosY + ExDirY * 0.012, BgOz = ExPosZ + ExDirZ * 0.012;
              const BgHit = this.TraceScene(Prims, VehicleBvhData, BgOx, BgOy, BgOz, ExDirX, ExDirY, ExDirZ, 32.0, Hit.primIdx, true, this._SecHit);
              const TBgQ = this.TraceLightQuadT(Lights, BgOx, BgOy, BgOz, ExDirX, ExDirY, ExDirZ, BgHit.t, QuadOut);
              let TrR = 0, TrG = 0, TrB = 0;
              if (TBgQ < BgHit.t) {
                TrR = QuadOut[0]; TrG = QuadOut[1]; TrB = QuadOut[2];
              } else if (BgHit.hit) {
                this.ShadeSecondaryBounce(Prims, Lights, AtlasPixels, BgHit, -ExDirX, -ExDirY, -ExDirZ, SkyRadiance, SunDir, SunIrradiance, SecOut);
                TrR = SecOut[0]; TrG = SecOut[1]; TrB = SecOut[2];
              } else {
                this.EvalSky(ExDirX, ExDirY, ExDirZ, SkyRadiance, SunDir, SunIrradiance, SkyOut);
                TrR = SkyOut[0]; TrG = SkyOut[1]; TrB = SkyOut[2];
              }

              const Spread = (BaseIor - 1.0) / P.Abbe;
              const EdgeDisp = Math.pow(1.0 - CosView, 1.5) * Spread * 18.0;
              const ChrR = Clamp(1.0 + EdgeDisp, 0.74, 1.30);
              const ChrB = Clamp(1.0 - EdgeDisp, 0.74, 1.30);
              const AttExp = PathThickness * 1.45;
              const AttR = Math.pow(Clamp(Mat.baseR, 0.04, 1.0), AttExp);
              const AttG = Math.pow(Clamp(Mat.baseG, 0.04, 1.0), AttExp);
              const AttB = Math.pow(Clamp(Mat.baseB, 0.04, 1.0), AttExp);
              const TransScale = (1.0 - F) * Math.max(0.25, Mat.transmission);

              let Shx = Vx + SunDir[0], Shy = Vy + SunDir[1], Shz = Vz + SunDir[2];
              const Shl = Math.max(1e-6, Len3(Shx, Shy, Shz));
              const SunGlint = Math.pow(Math.max(0.0, (Nx * Shx + Ny * Shy + Nz * Shz) / Shl), 512.0) * 14.0;

              LinR = ReflR * F + TrR * ChrR * AttR * TransScale + SunIrradiance[0] * SunGlint + Mat.baseR * Mat.emission;
              LinG = ReflG * F + TrG * AttG * TransScale + SunIrradiance[1] * SunGlint + Mat.baseG * Mat.emission;
              LinB = ReflB * F + TrB * ChrB * AttB * TransScale + SunIrradiance[2] * SunGlint + Mat.baseB * Mat.emission;
            } else if (EngineMode === 0 && ViewModeCode === 0) {
              // Order 1 Studio MatCap Shading (Zero GI, Zero SDF)
              let Vnx = Nx * Rx + Ny * Ry + Nz * Rz;
              let Vny = Nx * Ux + Ny * Uy + Nz * Uz;
              let Vnz = Math.max(0.0, -(Nx * FwdX + Ny * FwdY + Nz * FwdZ));
              const Vnl = Math.max(1e-6, Len3(Vnx, Vny, Vnz));
              Vnx /= Vnl; Vny /= Vnl; Vnz /= Vnl;
              const KeyDiff = Math.max(0.0, Vnx * 0.4158 + Vny * 0.6138 + Vnz * 0.6712);
              const FillDiff = Math.max(0.0, Vnx * -0.6844 + Vny * 0.2516 + Vnz * 0.6844);
              const RimGlow = Math.pow(Clamp(1.0 - Vnz, 0.0, 1.0), 2.6) * (0.35 + 0.65 * Math.max(0.0, Vny * 0.8531 + Vnz * 0.5217));
              const SpecPow = Mix(96.0, 6.0, Clamp(Mat.roughness, 0.02, 1.0));
              const Khx = 0.2323, Khy = 0.3429, Khz = 0.9102;
              const KeySpec = Math.pow(Math.max(0.0, Vnx * Khx + Vny * Khy + Vnz * Khz), SpecPow);
              const SpecGain = Mix(1.45, 0.18, Clamp(Mat.roughness, 0.0, 1.0));
              const StudioL = 0.14 + 0.88 * KeyDiff + 0.34 * FillDiff;
              const SpecNonMetal = KeySpec * SpecGain * (1.0 - Mat.metallic);
              LinR = Mat.baseR * (StudioL * (1.0 - Mat.metallic * 0.78) + KeySpec * SpecGain * Mat.metallic) + SpecNonMetal + 0.26 * RimGlow + Mat.baseR * (PrimEmission + Mat.emission);
              LinG = Mat.baseG * (StudioL * (1.0 - Mat.metallic * 0.78) + KeySpec * SpecGain * Mat.metallic) + SpecNonMetal + 0.28 * RimGlow + Mat.baseG * (PrimEmission + Mat.emission);
              LinB = Mat.baseB * (StudioL * (1.0 - Mat.metallic * 0.78) + KeySpec * SpecGain * Mat.metallic) + SpecNonMetal + 0.32 * RimGlow + Mat.baseB * (PrimEmission + Mat.emission);
            } else {
              if (ViewModeCode === 1) {
                Mat.baseR = 0.72; Mat.baseG = 0.71; Mat.baseB = 0.69;
                Mat.metallic = 0.0; Mat.roughness = 0.58; Mat.coat = 0.0;
                Mat.sheenR = 0; Mat.sheenG = 0; Mat.sheenB = 0;
                Mat.subR = 0; Mat.subG = 0; Mat.subB = 0; Mat.thickness = 0;
                Mat.transmission = 0; Mat.emission = 0;
              }

              const Vx = -Rdx, Vy = -Rdy, Vz = -Rdz;
              const VdotN = Math.max(1e-4, Vx * Nx + Vy * Ny + Vz * Nz);
              LinR = Mat.baseR * (PrimEmission + Mat.emission);
              LinG = Mat.baseG * (PrimEmission + Mat.emission);
              LinB = Mat.baseB * (PrimEmission + Mat.emission);

              // 1. Solar Direct + Analytical Non-SDF Raytraced Shadow
              const SunNoL = Nx * SunDir[0] + Ny * SunDir[1] + Nz * SunDir[2];
              if (SunNoL > 0.0) {
                const SunVis = this.EvaluateRayShadow(
                  Prims, VehicleBvhData,
                  Hit.px + Nx * 0.006, Hit.py + Ny * 0.006, Hit.pz + Nz * 0.006,
                  SunDir[0], SunDir[1], SunDir[2], 28.0, SinSun, ShadowRays, 0.0, Hit.primIdx,
                );
                if (SunVis > 0.0) {
                  let Hx = Vx + SunDir[0], Hy = Vy + SunDir[1], Hz = Vz + SunDir[2];
                  const Hl = Math.max(1e-6, Len3(Hx, Hy, Hz));
                  Hx /= Hl; Hy /= Hl; Hz /= Hl;
                  const NdotH = Math.max(0, Nx * Hx + Ny * Hy + Nz * Hz);
                  const VdotH = Math.max(0, Vx * Hx + Vy * Hy + Vz * Hz);
                  const Alpha = Math.max(0.02, Mat.roughness * Mat.roughness);
                  const A2 = Alpha * Alpha;
                  const Den = NdotH * NdotH * (A2 - 1.0) + 1.0;
                  const D = A2 / Math.max(1e-6, PI * Den * Den);
                  const K = (Alpha * 0.5) + 0.02;
                  const GSmith = 0.25 / Math.max(0.05, (VdotN * (1 - K) + K) * (SunNoL * (1 - K) + K));
                  const DielF = FresnelDielectric(VdotH, Mat.ior);
                  const Fr = Mix(DielF, Mat.baseR, Mat.metallic);
                  const Fg = Mix(DielF, Mat.baseG, Mat.metallic);
                  const Fb = Mix(DielF, Mat.baseB, Mat.metallic);
                  const Kd = ((1.0 - Mat.metallic) * (1.0 - Mat.transmission)) / PI;
                  LinR += ((1 - Fr) * Mat.baseR * Kd + Fr * D * GSmith) * SunNoL * SunIrradiance[0] * SunVis;
                  LinG += ((1 - Fg) * Mat.baseG * Kd + Fg * D * GSmith) * SunNoL * SunIrradiance[1] * SunVis;
                  LinB += ((1 - Fb) * Mat.baseB * Kd + Fb * D * GSmith) * SunNoL * SunIrradiance[2] * SunVis;
                }
              }

              // 2. Clipped 4-Subquad Solid-Angle Area Lights + Directed Studio Light Soft Shadows
              const DotVN = Vx * Nx + Vy * Ny + Vz * Nz;
              const Rx2 = -Vx + 2.0 * DotVN * Nx;
              const Ry2 = -Vy + 2.0 * DotVN * Ny;
              const Rz2 = -Vz + 2.0 * DotVN * Nz;

              for (let Li = 0; Li < Lights.length; ++Li) {
                const L = Lights[Li];
                let DiffR = 0, DiffG = 0, DiffB = 0;
                for (let Sy = -1; Sy <= 1; Sy += 2) {
                  for (let Sx = -1; Sx <= 1; Sx += 2) {
                    const UOff = Sx * L.Hw * 0.5;
                    const VOff = Sy * L.Hh * 0.5;
                    const Dvx = L.Cx + L.Rx * UOff + L.Ux * VOff - Hit.px;
                    const Dvy = L.Cy + L.Ry * UOff + L.Uy * VOff - Hit.py;
                    const Dvz = L.Cz + L.Rz * UOff + L.Uz * VOff - Hit.pz;
                    const D2 = Math.max(Dvx * Dvx + Dvy * Dvy + Dvz * Dvz, 0.08);
                    const InvD = 1.0 / Math.sqrt(D2);
                    const Wix = Dvx * InvD, Wiy = Dvy * InvD, Wiz = Dvz * InvD;
                    const CSurf = Math.max(0, Nx * Wix + Ny * Wiy + Nz * Wiz);
                    const CEmit = L.Omni ? 0.65 : Math.max(0, -(Wix * L.Nx + Wiy * L.Ny + Wiz * L.Nz));
                    if (CSurf > 0 && CEmit > 0) {
                      const W = (CSurf * Math.min((L.Hw * L.Hh * CEmit) / D2, 0.85)) / PI;
                      DiffR += L.RadR * W;
                      DiffG += L.RadG * W;
                      DiffB += L.RadB * W;
                    }
                  }
                }
                if (DiffR + DiffG + DiffB > 1e-5) {
                  let AreaVis = 1.0;
                  const Tlx = L.Cx - Hit.px, Tly = L.Cy - Hit.py, Tlz = L.Cz - Hit.pz;
                  const DistL = Len3(Tlx, Tly, Tlz);
                  if (EmissiveShadowed && DistL >= 0.05) {
                    const OxA = Hit.px + Nx * 0.008, OyA = Hit.py + Ny * 0.008, OzA = Hit.pz + Nz * 0.008;
                    if (PenumbraMode === 0) {
                      const Dx = L.Cx - OxA, Dy = L.Cy - OyA, Dz = L.Cz - OzA;
                      const D = Len3(Dx, Dy, Dz);
                      let Lit = 0.0;
                      if (D > 1e-4 && (Nx * Dx + Ny * Dy + Nz * Dz) > 0.002 * D) {
                        if (!this.TraceShadowRayAny(Prims, VehicleBvhData, OxA, OyA, OzA, Dx / D, Dy / D, Dz / D, Math.max(0.04, D - 0.14), Hit.primIdx, L.PickCode)) {
                          Lit = 1.0;
                        }
                      }
                      AreaVis = Mix(0.03, 1.0, Lit);
                    } else {
                      const RayCnt = PenumbraMode >= 4 ? (PenumbraMode === 5 ? 12 : 8) : Clamp(ShadowRays, 2, 12);
                      const SoftScale = PenumbraMode === 1 ? 0.72 : PenumbraMode >= 4 ? 1.12 : 1.0;
                      const R2x = Hash12(Px * 131.7, Py * 73.1);
                      const R2y = Hash12(Px * 91.3 + 37.1, Py * 53.7 + 13.7);
                      let VisAcc = 0;
                      for (let S = 0; S < RayCnt; ++S) {
                        const Sx = (Fract(R2x + S * 0.75487766) * 2.0 - 1.0) * 0.94 * L.Hw * ShadowSoftness * SoftScale;
                        const Sy = (Fract(R2y + S * 0.56984029) * 2.0 - 1.0) * 0.94 * L.Hh * ShadowSoftness * SoftScale;
                        const Dx = L.Cx + L.Rx * Sx + L.Ux * Sy - OxA;
                        const Dy = L.Cy + L.Ry * Sx + L.Uy * Sy - OyA;
                        const Dz = L.Cz + L.Rz * Sx + L.Uz * Sy - OzA;
                        const D = Len3(Dx, Dy, Dz);
                        if (D > 1e-4 && (Nx * Dx + Ny * Dy + Nz * Dz) > 0.002 * D) {
                          if (!this.TraceShadowRayAny(Prims, VehicleBvhData, OxA, OyA, OzA, Dx / D, Dy / D, Dz / D, Math.max(0.04, D - 0.14), Hit.primIdx, L.PickCode)) {
                            VisAcc += 1.0 / RayCnt;
                          }
                        }
                      }
                      AreaVis = Mix(0.03, 1.0, VisAcc);
                    }
                  }
                  const Kd = (1.0 - Mat.metallic) * (1.0 - Mat.transmission);
                  LinR += Kd * Mat.baseR * DiffR * AreaVis;
                  LinG += Kd * Mat.baseG * DiffG * AreaVis;
                  LinB += Kd * Mat.baseB * DiffB * AreaVis;

                  const RDenom = Rx2 * L.Nx + Ry2 * L.Ny + Rz2 * L.Nz;
                  if (Math.abs(RDenom) > 1e-4) {
                    const TPlane = ((L.Cx - Hit.px) * L.Nx + (L.Cy - Hit.py) * L.Ny + (L.Cz - Hit.pz) * L.Nz) / RDenom;
                    if (TPlane > 0.01 && (L.Omni || RDenom < 0.0)) {
                      const RelX = Hit.px + Rx2 * TPlane - L.Cx;
                      const RelY = Hit.py + Ry2 * TPlane - L.Cy;
                      const RelZ = Hit.pz + Rz2 * TPlane - L.Cz;
                      const UClamp = Clamp(RelX * L.Rx + RelY * L.Ry + RelZ * L.Rz, -L.Hw, L.Hw);
                      const VClamp = Clamp(RelX * L.Ux + RelY * L.Uy + RelZ * L.Uz, -L.Hh, L.Hh);
                      const Clx = (L.Cx + L.Rx * UClamp + L.Ux * VClamp) - Hit.px;
                      const Cly = (L.Cy + L.Ry * UClamp + L.Uy * VClamp) - Hit.py;
                      const Clz = (L.Cz + L.Rz * UClamp + L.Uz * VClamp) - Hit.pz;
                      const ClLen = Math.max(1e-5, Len3(Clx, Cly, Clz));
                      const Lsx = Clx / ClLen, Lsy = Cly / ClLen, Lsz = Clz / ClLen;
                      const SpecSpread = Math.max(0.04, Mat.roughness * Mat.roughness);
                      const Solid = Clamp((L.Hw * L.Hh) / Math.max(0.15, TPlane * TPlane * SpecSpread * 6.0), 0.0, 1.2);
                      let Hx = Vx + Lsx, Hy = Vy + Lsy, Hz = Vz + Lsz;
                      const Hl = Math.max(1e-5, Len3(Hx, Hy, Hz));
                      const VdotH = Math.max(0, (Vx * Hx + Vy * Hy + Vz * Hz) / Hl);
                      const DielF = FresnelDielectric(VdotH, Mat.ior);
                      const Fr = Mix(DielF, Mat.baseR, Mat.metallic);
                      const Fg = Mix(DielF, Mat.baseG, Mat.metallic);
                      const Fb = Mix(DielF, Mat.baseB, Mat.metallic);
                      const SpecK = Solid * Math.max(0, Nx * Lsx + Ny * Lsy + Nz * Lsz) * AreaVis * 0.55;
                      LinR += Fr * L.RadR * SpecK;
                      LinG += Fg * L.RadG * SpecK;
                      LinB += Fb * L.RadB * SpecK;
                    }
                  }
                }

                if (Mat.thickness > 0.02 && Mat.metallic < 0.5) {
                  const Tlx = L.Cx - Hit.px, Tly = L.Cy - Hit.py, Tlz = L.Cz - Hit.pz;
                  const Dl = Math.max(0.2, Len3(Tlx, Tly, Tlz));
                  const NoL = (Nx * Tlx + Ny * Tly + Nz * Tlz) / Dl;
                  const Wrap = Clamp((NoL + 0.55) / 1.55, 0.0, 1.0);
                  const Sss = Wrap * Wrap * (0.14 * Clamp(Mat.thickness, 0, 1)) * (1 - Mat.metallic) / (Dl * Dl * 0.35 + 1.0);
                  LinR += Mat.subR * Mat.baseR * L.RadR * Sss;
                  LinG += Mat.subG * Mat.baseG * L.RadG * Sss;
                  LinB += Mat.subB * Mat.baseB * L.RadB * Sss;
                }
              }

              // 2B. Physical Moonlight (night, twilight & visible daytime moon) + Direct Shadowed Emissive Primitives
              const MoonIrrDir = [0, 0, 0];
              if (this.EvalMoonIrradiance(CompiledScene, MoonIrrDir)) {
                const Md = CompiledScene.Moon.dir;
                const MNoL = Math.max(0.0, Nx * Md[0] + Ny * Md[1] + Nz * Md[2]);
                if (MNoL > 0.0) {
                  const MVis = this.EvaluateRayShadow(Prims, VehicleBvhData, Hit.px + Nx * 0.008, Hit.py + Ny * 0.008, Hit.pz + Nz * 0.008, Md[0], Md[1], Md[2], 24.0, 0.02, 2, 0.04, Hit.primIdx, -1);
                  const Ms = MNoL * MVis * (1.0 - Mat.metallic) / PI;
                  LinR += Mat.baseR * MoonIrrDir[0] * Ms;
                  LinG += Mat.baseG * MoonIrrDir[1] * Ms;
                  LinB += Mat.baseB * MoonIrrDir[2] * Ms;
                }
              }
              for (let Ei = 0; Ei < Prims.length; ++Ei) {
                if (Ei === Hit.primIdx) continue;
                const Ep = Prims[Ei];
                if (Ep.Emission > 0.05) {
                  let Tpx = Ep.Cx, Tpy = Ep.Cy, Tpz = Ep.Cz;
                  if (Ep.Shape !== 1) {
                    const Dx = Hit.px - Ep.Cx, Dy = Hit.py - Ep.Cy, Dz = Hit.pz - Ep.Cz;
                    const Lpx = Clamp(Ep.R0x * Dx + Ep.R0y * Dy + Ep.R0z * Dz, -Ep.Hx, Ep.Hx);
                    const Lpy = Clamp(Ep.R1x * Dx + Ep.R1y * Dy + Ep.R1z * Dz, -Ep.Hy, Ep.Hy);
                    const Lpz = Clamp(Ep.R2x * Dx + Ep.R2y * Dy + Ep.R2z * Dz, -Ep.Hz, Ep.Hz);
                    Tpx = Ep.Cx + Ep.R0x * Lpx + Ep.R1x * Lpy + Ep.R2x * Lpz;
                    Tpy = Ep.Cy + Ep.R0y * Lpx + Ep.R1y * Lpy + Ep.R2y * Lpz;
                    Tpz = Ep.Cz + Ep.R0z * Lpx + Ep.R1z * Lpy + Ep.R2z * Lpz;
                  }
                  const Ex = Tpx - Hit.px, Ey = Tpy - Hit.py, Ez = Tpz - Hit.pz;
                  const D2 = Math.max(0.04, Ex * Ex + Ey * Ey + Ez * Ez);
                  const Dist = Math.sqrt(D2);
                  const Edx = Ex / Dist, Edy = Ey / Dist, Edz = Ez / Dist;
                  const NdotE = Math.max(0, Nx * Edx + Ny * Edy + Nz * Edz);
                  if (NdotE > 0) {
                    const REff = Math.max(0.08, (Ep.Hx + Ep.Hy + Ep.Hz) * 0.3333333);
                    const Omega = Math.min(2.0, (REff * REff) / D2);
                    let EVis = 1.0;
                    if (EmissiveShadowed) {
                      const ConeE = Clamp(REff / Math.max(Dist, 0.35), 0.04, 0.24) * ShadowSoftness;
                      EVis = this.EvaluateRayShadow(
                        Prims, VehicleBvhData,
                        Hit.px + Nx * 0.012, Hit.py + Ny * 0.012, Hit.pz + Nz * 0.012,
                        Edx, Edy, Edz, Math.max(0.02, Dist - 0.03), ConeE, 2, 0.03, Hit.primIdx, Ep.PickCode,
                      );
                    }
                    const EK = Omega * (NdotE / PI) * EVis * 1.45 * Ep.Emission;
                    const Kd = (1.0 - Mat.metallic) * (1.0 - Mat.transmission);
                    LinR += Kd * Mat.baseR * Ep.BounceR * EK;
                    LinG += Kd * Mat.baseG * Ep.BounceG * EK;
                    LinB += Kd * Mat.baseB * Ep.BounceB * EK;
                    let Hx = Vx + Edx, Hy = Vy + Edy, Hz = Vz + Edz;
                    const Hl = Math.max(1e-6, Len3(Hx, Hy, Hz));
                    Hx /= Hl; Hy /= Hl; Hz /= Hl;
                    const NdotH = Math.max(0, Nx * Hx + Ny * Hy + Nz * Hz);
                    const VdotH = Math.max(0, Vx * Hx + Vy * Hy + Vz * Hz);
                    const Alpha = Math.max(0.025, Mat.roughness * Mat.roughness);
                    const A2 = Alpha * Alpha;
                    const Den = NdotH * NdotH * (A2 - 1.0) + 1.0;
                    const D = A2 / Math.max(1e-6, PI * Den * Den);
                    const K = (Alpha * 0.5) + 0.02;
                    const GSmith = 0.25 / Math.max(0.05, (VdotN * (1 - K) + K) * (NdotE * (1 - K) + K));
                    const DielF = FresnelDielectric(VdotH, Mat.ior);
                    const Fr = Mix(DielF, Mat.baseR, Mat.metallic);
                    const Fg = Mix(DielF, Mat.baseG, Mat.metallic);
                    const Fb = Mix(DielF, Mat.baseB, Mat.metallic);
                    LinR += Fr * D * GSmith * Ep.BounceR * EK * PI;
                    LinG += Fg * D * GSmith * Ep.BounceG * EK * PI;
                    LinB += Fb * D * GSmith * Ep.BounceB * EK * PI;
                  }
                }
              }

              // 3. Multi-mode GI + AO (Cone GI / 2-bounce BVH GI / Off + Starlight)
              const GiAo = this._GiOut;
              if (GiAlg === 2) {
                const SkyW = Clamp(Nz * 0.5 + 0.5, 0.0, 1.0);
                const Sz = Nz > 0.25 ? Nz : 0.25;
                const SL = Len3(Nx * 0.35, Ny * 0.35, Sz);
                this.EvalSky((Nx * 0.35) / SL, (Ny * 0.35) / SL, Sz / SL, SkyRadiance, SunDir, SunIrradiance, SkyOut, SkyLut);
                const DayF = Clamp(SunDir[2] * 4.0 + 0.25, 0.0, 1.0);
                const StarGi = [0, 0, 0];
                this.EvalStarlightAmbient(Nx, Ny, Nz, SunDir, StarGi, CompiledScene);
                GiAo[0] = (Mix(Mix(0.0012, 0.055, DayF), SkyOut[0], SkyW) * Mix(0.18, 0.78, DayF) + StarGi[0]) * FillBoost;
                GiAo[1] = (Mix(Mix(0.0015, 0.053, DayF), SkyOut[1], SkyW) * Mix(0.18, 0.78, DayF) + StarGi[1]) * FillBoost;
                GiAo[2] = (Mix(Mix(0.0024, 0.052, DayF), SkyOut[2], SkyW) * Mix(0.18, 0.78, DayF) + StarGi[2]) * FillBoost;
                GiAo[3] = 1.0;
              } else if (GiAlg === 1) {
                const Cnt = Clamp(GiSamples | 0, 2, 16);
                const Phi = Hash12(Px * 149.3 + Hit.pz * 67.1, Py * 91.7) * 2.0 * PI;
                let AccR = 0, AccG = 0, AccB = 0, OccAcc = 0;
                const StarGi = [0, 0, 0];
                for (let S = 0; S < Cnt; ++S) {
                  let Cpx = Hit.px, Cpy = Hit.py, Cpz = Hit.pz;
                  let Cnx = Nx, Cny = Ny, Cnz = Nz;
                  let CSkip = Hit.primIdx;
                  let ThrR = 1.0, ThrG = 1.0, ThrB = 1.0;
                  let PathR = 0.0, PathG = 0.0, PathB = 0.0;
                  for (let B = 1; B <= 2; ++B) {
                    let Tx = 0, Ty = 0, Tz = 1;
                    if (Math.abs(Cnx) > 0.9) { Tx = 0; Ty = 1; Tz = 0; }
                    else {
                      Tx = Cny; Ty = -Cnx; Tz = 0;
                      const Tl = Math.max(1e-6, Len3(Tx, Ty, Tz));
                      Tx /= Tl; Ty /= Tl; Tz /= Tl;
                    }
                    const Bx = Cny * Tz - Cnz * Ty, By = Cnz * Tx - Cnx * Tz, Bz = Cnx * Ty - Cny * Tx;
                    const U1 = (S + 0.5) / Cnt;
                    const U2 = Fract(Phi / (2.0 * PI) + S * 0.61803399 + (B - 1) * 0.381966);
                    const R = Math.sqrt(U1);
                    const Th = U2 * 2.0 * PI;
                    const Cth = Math.cos(Th) * R, Sth = Math.sin(Th) * R, Zup = Math.sqrt(Math.max(0, 1.0 - U1));
                    let Gdx = Tx * Cth + Bx * Sth + Cnx * Zup;
                    let Gdy = Ty * Cth + By * Sth + Cny * Zup;
                    let Gdz = Tz * Cth + Bz * Sth + Cnz * Zup;
                    const Gl = Math.max(1e-6, Len3(Gdx, Gdy, Gdz));
                    Gdx /= Gl; Gdy /= Gl; Gdz /= Gl;
                    const Rox2 = Cpx + Cnx * 0.012, Roy2 = Cpy + Cny * 0.012, Roz2 = Cpz + Cnz * 0.012;
                    const GHit = this.TraceScene(Prims, VehicleBvhData, Rox2, Roy2, Roz2, Gdx, Gdy, Gdz, B === 1 ? 24.0 : 16.0, CSkip, true, this._SecHit);
                    const TqGi = this.TraceLightQuadT(Lights, Rox2, Roy2, Roz2, Gdx, Gdy, Gdz, GHit.t, QuadOut);
                    if (TqGi < GHit.t) {
                      PathR += ThrR * QuadOut[0]; PathG += ThrG * QuadOut[1]; PathB += ThrB * QuadOut[2];
                      break;
                    }
                    if (!GHit.hit) {
                      this.EvalSky(Gdx, Gdy, Gdz, SkyRadiance, SunDir, SunIrradiance, SkyOut, SkyLut);
                      this.EvalStarlightAmbient(Cnx, Cny, Cnz, SunDir, StarGi, CompiledScene);
                      PathR += ThrR * (SkyOut[0] * 0.65 * FillBoost + StarGi[0]);
                      PathG += ThrG * (SkyOut[1] * 0.65 * FillBoost + StarGi[1]);
                      PathB += ThrB * (SkyOut[2] * 0.65 * FillBoost + StarGi[2]);
                      break;
                    }
                    if (B === 1) OccAcc += Math.exp(-GHit.t * 0.85);
                    this.ShadeSecondaryBounce(Prims, Lights, AtlasPixels, GHit, -Gdx, -Gdy, -Gdz, SkyRadiance, SunDir, SunIrradiance, SecOut, CompiledScene);
                    PathR += ThrR * SecOut[0]; PathG += ThrG * SecOut[1]; PathB += ThrB * SecOut[2];
                    if (B === 2) break;
                    const HM = GHit.isVehicle
                      ? this.BuildVehicleMaterialInto(GHit, this._SecMat)
                      : this.FetchMaterialInto(Prims, AtlasPixels, GHit.primIdx, GHit.u, GHit.v, this._SecMat);
                    if (HM.unlit || HM.emission > 0.1) break;
                    ThrR *= HM.baseR * (1.0 - HM.metallic) * 0.88;
                    ThrG *= HM.baseG * (1.0 - HM.metallic) * 0.88;
                    ThrB *= HM.baseB * (1.0 - HM.metallic) * 0.88;
                    if (Math.max(ThrR, ThrG, ThrB) < 0.025) break;
                    const Flp = (GHit.nx * -Gdx + GHit.ny * -Gdy + GHit.nz * -Gdz) >= 0 ? 1 : -1;
                    Cnx = GHit.nx * Flp; Cny = GHit.ny * Flp; Cnz = GHit.nz * Flp;
                    Cpx = GHit.px; Cpy = GHit.py; Cpz = GHit.pz;
                    CSkip = GHit.primIdx;
                  }
                  AccR += PathR; AccG += PathG; AccB += PathB;
                }
                GiAo[0] = (AccR / Cnt) * GiIntensity;
                GiAo[1] = (AccG / Cnt) * GiIntensity;
                GiAo[2] = (AccB / Cnt) * GiIntensity;
                GiAo[3] = Clamp(1.0 - (OccAcc / Cnt) * 0.72, 0.25, 1.0);
              } else {
                this.EvaluateGtxConeGiAndAo(
                  Prims, VehicleBvhData, Lights, Hit.px, Hit.py, Hit.pz, Nx, Ny, Nz, Hit.primIdx,
                  SkyRadiance, SunDir, SunIrradiance, SkyLut, EmissiveShadowed, GiAo, CompiledScene,
                );
                GiAo[0] *= GiIntensity * FillBoost;
                GiAo[1] *= GiIntensity * FillBoost;
                GiAo[2] *= GiIntensity * FillBoost;
              }
              const TotalAo = GiAo[3] * Mat.ao;
              const DielEnv = FresnelDielectric(VdotN, Mat.ior);
              const FenvR = Mix(DielEnv, Mat.baseR, Mat.metallic);
              const FenvG = Mix(DielEnv, Mat.baseG, Mat.metallic);
              const FenvB = Mix(DielEnv, Mat.baseB, Mat.metallic);
              const KdScale = (1.0 - Mat.metallic) * (1.0 - Mat.transmission) * TotalAo;
              LinR += (1.0 - FenvR) * Mat.baseR * GiAo[0] * KdScale;
              LinG += (1.0 - FenvG) * Mat.baseG * GiAo[1] * KdScale;
              LinB += (1.0 - FenvB) * Mat.baseB * GiAo[2] * KdScale;

              if (Mat.sheenR + Mat.sheenG + Mat.sheenB > 0.003) {
                const ShK = Math.pow(1.0 - VdotN, 3.0) * 0.45 * TotalAo;
                LinR += Mat.sheenR * GiAo[0] * ShK;
                LinG += Mat.sheenG * GiAo[1] * ShK;
                LinB += Mat.sheenB * GiAo[2] * ShK;
              }

              // 4. Roughness-Gated Specular Reflection + Light Quad Mirror Glint
              const BaseMirror = Smoothstep(0.28, 0.04, Mat.roughness) * Mix(0.35, 1.0, Mat.metallic);
              const CoatMirror = Mat.coat * Smoothstep(0.26, 0.02, Mat.coatRoughness);
              const RaySharpness = Math.max(BaseMirror, CoatMirror);

              this.EvalSky(Rx2, Ry2, Rz2, SkyRadiance, SunDir, SunIrradiance, SkyOut, SkyLut);
              let SpecEnvR = Mix(GiAo[0], SkyOut[0], 0.55);
              let SpecEnvG = Mix(GiAo[1], SkyOut[1], 0.55);
              let SpecEnvB = Mix(GiAo[2], SkyOut[2], 0.55);

              if (ViewModeCode === 0 && !ReflOff && RaySharpness > 0.02) {
                const MaxB = Clamp((Rs.ReflBounces ?? 2) | 0, 1, 3);
                let Cpx = Hit.px, Cpy = Hit.py, Cpz = Hit.pz;
                let Cnx = Nx, Cny = Ny, Cnz = Nz;
                let RdxR = Rx2, RdyR = Ry2, RdzR = Rz2;
                let CSkip = Hit.primIdx;
                let ThrR = 1.0, ThrG = 1.0, ThrB = 1.0;
                let SharpR = 0.0, SharpG = 0.0, SharpB = 0.0;
                for (let B = 1; B <= MaxB; ++B) {
                  const Ox2 = Cpx + Cnx * 0.006, Oy2 = Cpy + Cny * 0.006, Oz2 = Cpz + Cnz * 0.006;
                  const RHit = this.TraceScene(Prims, VehicleBvhData, Ox2, Oy2, Oz2, RdxR, RdyR, RdzR, 60.0, CSkip, true, this._SecHit);
                  const TqRefl = this.TraceLightQuadT(Lights, Ox2, Oy2, Oz2, RdxR, RdyR, RdzR, RHit.t, QuadOut);
                  if (TqRefl < RHit.t) {
                    SharpR += ThrR * QuadOut[0]; SharpG += ThrG * QuadOut[1]; SharpB += ThrB * QuadOut[2];
                    break;
                  }
                  if (!RHit.hit) {
                    this.EvalSky(RdxR, RdyR, RdzR, SkyRadiance, SunDir, SunIrradiance, SkyOut, SkyLut);
                    SharpR += ThrR * SkyOut[0]; SharpG += ThrG * SkyOut[1]; SharpB += ThrB * SkyOut[2];
                    break;
                  }
                  this.ShadeSecondaryBounce(Prims, Lights, AtlasPixels, RHit, -RdxR, -RdyR, -RdzR, SkyRadiance, SunDir, SunIrradiance, SecOut, CompiledScene);
                  const HM = RHit.isVehicle
                    ? this.BuildVehicleMaterialInto(RHit, this._SecMat)
                    : this.FetchMaterialInto(Prims, AtlasPixels, RHit.primIdx, RHit.u, RHit.v, this._SecMat);
                  const MRefl = Clamp((HM.metallic * 0.75 + HM.coat * 0.45) * (1.0 - HM.roughness), 0.0, 0.95);
                  if (B === MaxB || MRefl < 0.04) {
                    SharpR += ThrR * SecOut[0]; SharpG += ThrG * SecOut[1]; SharpB += ThrB * SecOut[2];
                    break;
                  }
                  const NoV2 = Math.max(0.02, RHit.nx * -RdxR + RHit.ny * -RdyR + RHit.nz * -RdzR);
                  const Fr2 = Clamp(MRefl + (1.0 - MRefl) * Math.pow(Math.max(1.0 - NoV2, 1e-4), 5.0) * 0.45, 0.0, 0.92);
                  SharpR += ThrR * SecOut[0] * (1.0 - Fr2 * 0.55);
                  SharpG += ThrG * SecOut[1] * (1.0 - Fr2 * 0.55);
                  SharpB += ThrB * SecOut[2] * (1.0 - Fr2 * 0.55);
                  ThrR *= Fr2; ThrG *= Fr2; ThrB *= Fr2;
                  if (Math.max(ThrR, ThrG, ThrB) < 0.025) break;
                  const Dot2 = 2.0 * (RdxR * RHit.nx + RdyR * RHit.ny + RdzR * RHit.nz);
                  RdxR -= Dot2 * RHit.nx; RdyR -= Dot2 * RHit.ny; RdzR -= Dot2 * RHit.nz;
                  Cpx = RHit.px; Cpy = RHit.py; Cpz = RHit.pz;
                  Cnx = RHit.nx; Cny = RHit.ny; Cnz = RHit.nz;
                  CSkip = RHit.primIdx;
                }
                SpecEnvR = Mix(SpecEnvR, SharpR, RaySharpness);
                SpecEnvG = Mix(SpecEnvG, SharpG, RaySharpness);
                SpecEnvB = Mix(SpecEnvB, SharpB, RaySharpness);
              }

              const EnvEnergy = 1.0 - Mat.roughness * 0.78;
              const EScale = EnvEnergy * EnvEnergy * Mix(0.28, 1.0, Math.max(Mat.metallic, RaySharpness)) * TotalAo;
              LinR += SpecEnvR * FenvR * EScale;
              LinG += SpecEnvG * FenvG * EScale;
              LinB += SpecEnvB * FenvB * EScale;
            }
          } else {
            if (TransparentBackground) {
              DepthOut[PixIdx] = HitDist;
              EntityOut[PixIdx] = 0;
              const O4 = PixIdx * 4;
              RgbaOut[O4] = 0;
              RgbaOut[O4 + 1] = 0;
              RgbaOut[O4 + 2] = 0;
              RgbaOut[O4 + 3] = 0;
              continue;
            }
            this.EvalSkyPrimary(Rdx, Rdy, Rdz, CompiledScene, SkyLut, MoonAtlas, TimeSec, Px, Py, SkyOut);
            LinR = SkyOut[0]; LinG = SkyOut[1]; LinB = SkyOut[2];
            if (ShowGrid && Math.abs(Rdz) > 1e-4) {
              const TFloor = (-0.80 - Roz) / Rdz;
              if (TFloor > 0.0 && TFloor < 36.0) {
                const Gpx = Rox + Rdx * TFloor;
                const Gpy = Roy + Rdy * TFloor;
                const PxWorld = Math.max(
                  0.004,
                  ((OrthoH > 0 ? OrthoH * 2.0 : TFloor * 2.0 * TanHalfFov) / Math.max(Height, 1.0)) * 1.6,
                );
                const Gx = Math.abs(Fract(Gpx) - 0.5) / PxWorld;
                const Gy = Math.abs(Fract(Gpy) - 0.5) / PxWorld;
                const Line = 1.0 - Math.min(Math.min(Gx, Gy), 1.0);
                const Fade = Math.exp(-TFloor * 0.09) * 0.45 * Line;
                LinR = Mix(LinR, 0.28, Fade);
                LinG = Mix(LinG, 0.34, Fade);
                LinB = Mix(LinB, 0.42, Fade);
              }
            }
          }

          DepthOut[PixIdx] = HitDist;
          EntityOut[PixIdx] = EntityCode;

          let Sr, Sg, Sb;
          if (ViewModeCode === 2) {
            Sr = Clamp(LinR, 0, 1);
            Sg = Clamp(LinG, 0, 1);
            Sb = Clamp(LinB, 0, 1);
          } else {
            Sr = Math.pow(AcesFilmChannel(Math.max(0, LinR) * ExposureGain), 1.0 / 2.2);
            Sg = Math.pow(AcesFilmChannel(Math.max(0, LinG) * ExposureGain), 1.0 / 2.2);
            Sb = Math.pow(AcesFilmChannel(Math.max(0, LinB) * ExposureGain), 1.0 / 2.2);
          }
          const O4 = PixIdx * 4;
          RgbaOut[O4] = (Sr * 255 + 0.5) | 0;
          RgbaOut[O4 + 1] = (Sg * 255 + 0.5) | 0;
          RgbaOut[O4 + 2] = (Sb * 255 + 0.5) | 0;
          RgbaOut[O4 + 3] = 255;
        }
      }
    }
  }

  return { GtxBandEngine, Clamp, Mix };
}

const LocalKernel = CreateGtxBandKernel(
  ATLAS_WIDTH,
  ATLAS_COLS,
  ATLAS_TILE_SIZE,
  TILES_PER_PRIM,
  SKY_VIEW_WIDTH,
  SKY_VIEW_HEIGHT,
);

//==========================================================================================================================================
// MULTI-WORKER ATOMIC DOUBLE-BUFFERED RENDERER (0.0 ms UI-thread raytracing, 0 partial scanlines ever blitted!)
//==========================================================================================================================================
export class CpuGtxRenderer {
  constructor() {
    this._LocalEngine = new LocalKernel.GtxBandEngine();

    // Front Display Buffer (ONLY ever populated by 100% completed frames!)
    this.BufW = 0;
    this.BufH = 0;
    this.DepthBuf = null;
    this.EntityBuf = null;
    this.OffscreenCanvas = null;
    this.OffscreenCtx = null;
    this.OffscreenImg = null;
    this.OutlineImg = null;
    this.FrontRevision = 0;
    this._BakedRevision = -1;
    this._BakedSelectedCode = -1;
    this.HasFrontFrame = false;

    // Synchronous Staging Back-Buffer (for headless validation / no-Worker environments)
    this._BackW = 0;
    this._BackH = 0;
    this._BackRgba = null;
    this._BackDepth = null;
    this._BackEntity = null;
    this.NextRow = 0;
    this.Complete = false;

    // Parallel Web Worker Pool
    this._Workers = null;
    this._WorkerBlobUrl = null;
    this._JobCounter = 0;
    this._ActiveJob = null;
    this._QueuedJob = null;
    this._SyncedAtlasVersion = -1;
    this._SyncedMoonVersion = -1;
    this._SyncedVehicleTris = false;
    this._VehInstBuf = new Float32Array(128 * 40);
    this._SkyLutFloat = new Float32Array(SKY_VIEW_WIDTH * SKY_VIEW_HEIGHT * 4);
    this._LastSkyLutKey = "";
    this._SyncedSkyLutKey = "";
  }

  _EnsureSkyLut(CompiledScene) {
    const Atmo = CompiledScene?.Atmosphere || {};
    const SunDir = CompiledScene?.SunDir || [0.42, -0.55, 0.72];
    const Key = GetSkyViewCacheKey(SunDir, Atmo);
    if (Key !== this._LastSkyLutKey) {
      this._LastSkyLutKey = Key;
      BuildSkyViewLut(SunDir, Atmo, this._SkyLutFloat);
    }
    return Key;
  }

  _BuildVehicleBvhData(CompiledScene, VehicleBvhAssets) {
    if (!VehicleBvhAssets || !CompiledScene?.Vehicles?.length) return null;
    const Built = BuildVehicleTlasAndInstances(CompiledScene.Vehicles, VehicleBvhAssets);
    if (!Built.instanceCount) return null;
    return {
      nodes: VehicleBvhAssets.nodeTexData,
      tris: VehicleBvhAssets.triTexData,
      insts: Built.instanceBuffer,
      instCount: Built.instanceCount,
    };
  }

  _EnsureFrontBuffers(W, H) {
    if (this.BufW === W && this.BufH === H && this.OffscreenImg) return;
    this.BufW = W;
    this.BufH = H;
    const N = W * H;
    this.DepthBuf = new Float32Array(N);
    this.EntityBuf = new Uint16Array(N);
    if (typeof document !== "undefined") {
      if (!this.OffscreenCanvas) {
        this.OffscreenCanvas = document.createElement("canvas");
        this.OffscreenCtx = this.OffscreenCanvas.getContext("2d");
      }
      this.OffscreenCanvas.width = W;
      this.OffscreenCanvas.height = H;
    }
    if (this.OffscreenCtx) {
      this.OffscreenImg = this.OffscreenCtx.createImageData(W, H);
      this.OutlineImg = this.OffscreenCtx.createImageData(W, H);
    } else {
      this.OffscreenImg = { width: W, height: H, data: new Uint8ClampedArray(N * 4) };
      this.OutlineImg = { width: W, height: H, data: new Uint8ClampedArray(N * 4) };
    }
    this._BakedRevision = -1;
  }

  _EnsureBackBuffers(W, H) {
    if (this._BackW === W && this._BackH === H && this._BackRgba) return;
    this._BackW = W;
    this._BackH = H;
    const N = W * H;
    this._BackRgba = new Uint8ClampedArray(N * 4);
    this._BackDepth = new Float32Array(N);
    this._BackEntity = new Uint16Array(N);
  }

  _SwapBackToFront(W, H) {
    this._EnsureFrontBuffers(W, H);
    this.OffscreenImg.data.set(this._BackRgba);
    this.DepthBuf.set(this._BackDepth);
    this.EntityBuf.set(this._BackEntity);
    this.FrontRevision += 1;
    this.HasFrontFrame = true;
    this.Complete = true;
  }

  // Legacy/Validation entrypoints: stage into BackBuffer and ONLY swap to FrontBuffer once 100% of rows finish!
  EnsureBuffers(W, H) {
    this._EnsureBackBuffers(W, H);
    if (!this.HasFrontFrame) {
      this._EnsureFrontBuffers(W, H);
    }
    this.NextRow = 0;
    this.Complete = false;
  }

  Invalidate() {
    this.NextRow = 0;
    this.Complete = false;
  }

  RenderSlice(Params, RowBudget = 64) {
    const { Width, Height } = Params;
    this._EnsureBackBuffers(Width, Height);
    if (this.Complete) return true;

    this._EnsureSkyLut(Params.CompiledScene);
    const StartRow = this.NextRow;
    const EndRow = Math.min(Height, StartRow + Math.max(1, RowBudget));
    const VehicleBvhData = Params.VehicleBvhData || this._BuildVehicleBvhData(Params.CompiledScene, Params.VehicleBvhAssets);
    this._LocalEngine.RenderBand(
      { ...Params, VehicleBvhData, SkyLut: this._SkyLutFloat, StartRow, EndRow },
      this._BackRgba,
      this._BackDepth,
      this._BackEntity,
      false,
    );
    this.NextRow = EndRow;
    if (this.NextRow >= Height) {
      this._SwapBackToFront(Width, Height);
    }
    return this.Complete;
  }

  RenderFullFrame(Params) {
    const { Width, Height } = Params;
    this._EnsureBackBuffers(Width, Height);
    this._EnsureSkyLut(Params.CompiledScene);
    const VehicleBvhData = Params.VehicleBvhData || this._BuildVehicleBvhData(Params.CompiledScene, Params.VehicleBvhAssets);
    this._LocalEngine.RenderBand(
      { ...Params, VehicleBvhData, SkyLut: this._SkyLutFloat, StartRow: 0, EndRow: Height },
      this._BackRgba,
      this._BackDepth,
      this._BackEntity,
      false,
    );
    this._SwapBackToFront(Width, Height);
    return true;
  }

  _EnsureWorkerPool() {
    if (this._Workers !== null) return this._Workers.length > 0;
    if (typeof Worker === "undefined" || typeof Blob === "undefined" || typeof URL === "undefined") {
      this._Workers = [];
      return false;
    }
    try {
      const Hw = (typeof navigator !== "undefined" && navigator.hardwareConcurrency) || 8;
      const WorkerCount = Math.min(10, Math.max(4, Hw));
      const Source = [
        `"use strict";`,
        `const Kernel = (${CreateGtxBandKernel.toString()})(${ATLAS_WIDTH}, ${ATLAS_COLS}, ${ATLAS_TILE_SIZE}, ${TILES_PER_PRIM}, ${SKY_VIEW_WIDTH}, ${SKY_VIEW_HEIGHT});`,
        `const Engine = new Kernel.GtxBandEngine();`,
        `let CachedAtlas = null;`,
        `let CachedMoonAtlas = null;`,
        `let CachedSkyLut = null;`,
        `let CachedVehTris = null;`,
        `let CachedVehNodes = null;`,
        `let CachedVehInsts = null;`,
        `let CachedVehCount = 0;`,
        `self.onmessage = (Ev) => {`,
        `  const Msg = Ev.data;`,
        `  if (!Msg) return;`,
        `  if (Msg.Kind === "atlas") {`,
        `    CachedAtlas = new Uint8Array(Msg.AtlasBuffer);`,
        `    return;`,
        `  }`,
        `  if (Msg.Kind === "moonAtlas") {`,
        `    CachedMoonAtlas = { width: Msg.Width, height: Msg.Height, ready: Msg.Ready, pixels: new Uint8Array(Msg.PixelsBuffer) };`,
        `    return;`,
        `  }`,
        `  if (Msg.Kind === "skyLut") {`,
        `    CachedSkyLut = new Float32Array(Msg.SkyLutBuffer);`,
        `    return;`,
        `  }`,
        `  if (Msg.Kind === "vehTris") {`,
        `    CachedVehTris = new Float32Array(Msg.TrisBuffer);`,
        `    return;`,
        `  }`,
        `  if (Msg.Kind === "vehScene") {`,
        `    CachedVehNodes = new Float32Array(Msg.NodesBuffer);`,
        `    CachedVehInsts = new Float32Array(Msg.InstsBuffer);`,
        `    CachedVehCount = Msg.InstCount | 0;`,
        `    return;`,
        `  }`,
        `  if (Msg.Kind === "band") {`,
        `    const { JobId, CompiledScene, Width, Height, StartRow, EndRow, ViewModeCode, ShowGrid, TimeSec } = Msg;`,
        `    const Rows = Math.max(0, EndRow - StartRow);`,
        `    const Pix = Width * Rows;`,
        `    const Rgba = new Uint8ClampedArray(Pix * 4);`,
        `    const Depth = new Float32Array(Pix);`,
        `    const Entity = new Uint16Array(Pix);`,
        `    const VehicleBvhData = (CachedVehCount > 0 && CachedVehTris && CachedVehNodes && CachedVehInsts)`,
        `      ? { nodes: CachedVehNodes, tris: CachedVehTris, insts: CachedVehInsts, instCount: CachedVehCount }`,
        `      : null;`,
        `    Engine.RenderBand({ CompiledScene, AtlasPixels: CachedAtlas, MoonAtlas: CachedMoonAtlas, SkyLut: CachedSkyLut, VehicleBvhData, Width, Height, StartRow, EndRow, ViewModeCode, ShowGrid, TimeSec }, Rgba, Depth, Entity, true);`,
        `    self.postMessage({ Kind: "bandDone", JobId, Width, Height, StartRow, EndRow, RgbaBuffer: Rgba.buffer, DepthBuffer: Depth.buffer, EntityBuffer: Entity.buffer }, [Rgba.buffer, Depth.buffer, Entity.buffer]);`,
        `  }`,
        `};`,
      ].join("\n");

      const BlobObj = new Blob([Source], { type: "text/javascript" });
      this._WorkerBlobUrl = URL.createObjectURL(BlobObj);
      this._Workers = [];
      for (let I = 0; I < WorkerCount; ++I) {
        const Wk = new Worker(this._WorkerBlobUrl);
        Wk.onmessage = (Ev) => this._HandleWorkerMessage(Ev.data);
        Wk.onerror = () => {
          this.DisposeWorkers();
          this._Workers = [];
        };
        this._Workers.push(Wk);
      }
      this._SyncedAtlasVersion = -1;
      this._SyncedMoonVersion = -1;
      this._SyncedVehicleTris = false;
      this._SyncedSkyLutKey = "";
      return true;
    } catch {
      this._Workers = [];
      return false;
    }
  }

  _HandleWorkerMessage(Msg) {
    if (!Msg || Msg.Kind !== "bandDone") return;
    const Job = this._ActiveJob;
    if (!Job || Msg.JobId !== Job.JobId) return;

    const { Width, StartRow, RgbaBuffer, DepthBuffer, EntityBuffer } = Msg;
    const PixOffset = StartRow * Width;
    Job.Rgba.set(new Uint8ClampedArray(RgbaBuffer), PixOffset * 4);
    Job.Depth.set(new Float32Array(DepthBuffer), PixOffset);
    Job.Entity.set(new Uint16Array(EntityBuffer), PixOffset);

    Job.DoneBands += 1;
    if (Job.DoneBands < Job.TotalBands) return;

    // 100% OF BANDS COMPLETE -> ATOMIC SINGLE-SWAP TO FRONT BUFFER!
    const ElapsedMs = Math.max(0.5, performance.now() - Job.StartedAt);
    this._EnsureFrontBuffers(Job.Width, Job.Height);
    this.OffscreenImg.data.set(Job.Rgba);
    this.DepthBuf.set(Job.Depth);
    this.EntityBuf.set(Job.Entity);
    this.FrontRevision += 1;
    this.HasFrontFrame = true;
    this.Complete = true;

    const Callback = Job.OnFrameReady;
    this._ActiveJob = null;

    if (Callback) Callback(ElapsedMs);

    if (this._QueuedJob) {
      const Next = this._QueuedJob;
      this._QueuedJob = null;
      this._DispatchWorkerJob(Next.Params, Next.OnFrameReady);
    }
  }

  _DispatchWorkerJob(Params, OnFrameReady) {
    const {
      CompiledScene,
      AtlasPixels,
      AtlasVersion = 0,
      MoonAtlas = null,
      VehicleBvhAssets = null,
      Width,
      Height,
      ViewModeCode,
      ShowGrid,
      TimeSec = 0,
    } = Params;

    if (MoonAtlas?.pixels && this._SyncedMoonVersion !== MoonAtlas.version) {
      this._SyncedMoonVersion = MoonAtlas.version;
      for (let I = 0; I < this._Workers.length; ++I) {
        const Copy = MoonAtlas.pixels.slice();
        this._Workers[I].postMessage(
          { Kind: "moonAtlas", Width: MoonAtlas.width, Height: MoonAtlas.height, Ready: MoonAtlas.ready, PixelsBuffer: Copy.buffer },
          [Copy.buffer],
        );
      }
    }

    const SkyKey = this._EnsureSkyLut(CompiledScene);
    if (SkyKey !== this._SyncedSkyLutKey) {
      this._SyncedSkyLutKey = SkyKey;
      for (let I = 0; I < this._Workers.length; ++I) {
        const Copy = this._SkyLutFloat.slice();
        this._Workers[I].postMessage({ Kind: "skyLut", SkyLutBuffer: Copy.buffer }, [Copy.buffer]);
      }
    }

    if (AtlasPixels && this._SyncedAtlasVersion !== AtlasVersion) {
      this._SyncedAtlasVersion = AtlasVersion;
      for (let I = 0; I < this._Workers.length; ++I) {
        const Copy = AtlasPixels.slice();
        this._Workers[I].postMessage({ Kind: "atlas", AtlasBuffer: Copy.buffer }, [Copy.buffer]);
      }
    }

    if (VehicleBvhAssets) {
      if (!this._SyncedVehicleTris) {
        this._SyncedVehicleTris = true;
        for (let I = 0; I < this._Workers.length; ++I) {
          const TriCopy = VehicleBvhAssets.triTexData.slice();
          this._Workers[I].postMessage({ Kind: "vehTris", TrisBuffer: TriCopy.buffer }, [TriCopy.buffer]);
        }
      }
      const BuiltVeh = this._BuildVehicleBvhData(CompiledScene, VehicleBvhAssets);
      const InstCount = BuiltVeh ? BuiltVeh.instCount : 0;
      const InstSlice = BuiltVeh ? BuiltVeh.insts : new Float32Array(4);
      for (let I = 0; I < this._Workers.length; ++I) {
        const NodeCopy = VehicleBvhAssets.nodeTexData.slice();
        const InstCopy = InstSlice.slice();
        this._Workers[I].postMessage(
          { Kind: "vehScene", NodesBuffer: NodeCopy.buffer, InstsBuffer: InstCopy.buffer, InstCount },
          [NodeCopy.buffer, InstCopy.buffer],
        );
      }
    }

    const WorkerCount = Math.min(this._Workers.length, Height);
    const JobId = ++this._JobCounter;
    const N = Width * Height;
    this._ActiveJob = {
      JobId,
      Width,
      Height,
      TotalBands: WorkerCount,
      DoneBands: 0,
      Rgba: new Uint8ClampedArray(N * 4),
      Depth: new Float32Array(N),
      Entity: new Uint16Array(N),
      StartedAt: performance.now(),
      OnFrameReady,
    };

    for (let I = 0; I < WorkerCount; ++I) {
      const StartRow = Math.floor((I * Height) / WorkerCount);
      const EndRow = Math.floor(((I + 1) * Height) / WorkerCount);
      this._Workers[I].postMessage({
        Kind: "band",
        JobId,
        CompiledScene,
        Width,
        Height,
        StartRow,
        EndRow,
        ViewModeCode,
        ShowGrid,
        TimeSec,
      });
    }
  }

  // Non-blocking atomic full-frame request: coalesces rapid camera inputs so only the newest pose renders next!
  RequestAtomicFrame(Params, OnFrameReady) {
    if (this._EnsureWorkerPool()) {
      if (this._ActiveJob) {
        this._QueuedJob = { Params, OnFrameReady };
        return;
      }
      this._DispatchWorkerJob(Params, OnFrameReady);
      return;
    }
    const T0 = typeof performance !== "undefined" ? performance.now() : 0;
    this.RenderFullFrame(Params);
    const Elapsed = typeof performance !== "undefined" ? Math.max(1, performance.now() - T0) : 8;
    OnFrameReady?.(Elapsed);
  }

  DisposeWorkers() {
    if (this._Workers) {
      for (const Wk of this._Workers) {
        try {
          Wk.terminate();
        } catch {}
      }
    }
    if (this._WorkerBlobUrl && typeof URL !== "undefined") {
      try {
        URL.revokeObjectURL(this._WorkerBlobUrl);
      } catch {}
      this._WorkerBlobUrl = null;
    }
    this._ActiveJob = null;
    this._QueuedJob = null;
    this._Workers = null;
  }

  CompositeToCanvas(Ctx, CanvasW, CanvasH, SelectedCode, BuiltGizmo, ViewProj, HighlightedHandle) {
    if (!this.HasFrontFrame || !this.OffscreenImg || !this.OffscreenCtx) return;
    const W = this.BufW;
    const H = this.BufH;

    // Cache baked 2px outer silhouette outline on OffscreenCanvas so 60Hz gizmo hover costs 0.15ms!
    if (this._BakedRevision !== this.FrontRevision || this._BakedSelectedCode !== SelectedCode) {
      this._BakedRevision = this.FrontRevision;
      this._BakedSelectedCode = SelectedCode;

      if (SelectedCode > 0 && this.OutlineImg) {
        this.OutlineImg.data.set(this.OffscreenImg.data);
        const Out = this.OutlineImg.data;
        const Ent = this.EntityBuf;
        const Dep = this.DepthBuf;
        const OffX = [1, -1, 0, 0, 1, -1, 1, -1, 2, -2, 0, 0];
        const OffY = [0, 0, 1, -1, 1, 1, -1, -1, 0, 0, 2, -2];
        const ClampIdx = LocalKernel.Clamp;
        const MixCh = LocalKernel.Mix;

        for (let Y = 0; Y < H; ++Y) {
          const RowOff = Y * W;
          for (let X = 0; X < W; ++X) {
            const Idx = RowOff + X;
            const CenterSel = Ent[Idx] === SelectedCode;
            const D0 = Dep[Idx];
            let Edge = 0.0;
            for (let K = 0; K < 12; ++K) {
              const Qx = ClampIdx(X + OffX[K], 0, W - 1);
              const Qy = ClampIdx(Y + OffY[K], 0, H - 1);
              const QIdx = Qy * W + Qx;
              const NeighSel = Ent[QIdx] === SelectedCode;
              const Dq = Dep[QIdx];
              if (!CenterSel && NeighSel && Dq <= D0 + 0.12) {
                const Wk = K < 4 ? 1.0 : K < 8 ? 0.88 : 0.62;
                if (Wk > Edge) Edge = Wk;
              } else if (CenterSel && !NeighSel && K < 4 && D0 <= Dq + 0.12) {
                if (0.35 > Edge) Edge = 0.35;
              }
            }
            if (Edge > 0.0) {
              const Alpha = Edge * 0.94;
              const O4 = Idx * 4;
              Out[O4] = (MixCh(Out[O4], 246, Alpha) + 0.5) | 0;
              Out[O4 + 1] = (MixCh(Out[O4 + 1], 198, Alpha) + 0.5) | 0;
              Out[O4 + 2] = (MixCh(Out[O4 + 2], 107, Alpha) + 0.5) | 0;
            }
          }
        }
        this.OffscreenCtx.putImageData(this.OutlineImg, 0, 0);
      } else {
        this.OffscreenCtx.putImageData(this.OffscreenImg, 0, 0);
      }
    }

    Ctx.imageSmoothingEnabled = true;
    Ctx.imageSmoothingQuality = "high";
    Ctx.drawImage(this.OffscreenCanvas, 0, 0, CanvasW, CanvasH);

    if (BuiltGizmo && ViewProj) {
      DrawSlateGizmo2D(Ctx, CanvasW, CanvasH, BuiltGizmo, ViewProj, HighlightedHandle);
    }
  }
}

function ProjectVertex(Buf, Base, M, W, H, Out) {
  const X = Buf[Base], Y = Buf[Base + 1], Z = Buf[Base + 2];
  const Cx = M[0] * X + M[4] * Y + M[8] * Z + M[12];
  const Cy = M[1] * X + M[5] * Y + M[9] * Z + M[13];
  const Cz = M[2] * X + M[6] * Y + M[10] * Z + M[14];
  const Cw = M[3] * X + M[7] * Y + M[11] * Z + M[15];
  if (Cw <= 0.04) return false;
  const InvW = 1.0 / Cw;
  Out[0] = (Cx * InvW * 0.5 + 0.5) * W;
  Out[1] = (0.5 - Cy * InvW * 0.5) * H;
  Out[2] = Cz * InvW;
  return true;
}

export function DrawSlateGizmo2D(Ctx, CanvasW, CanvasH, BuiltGizmo, ViewProj, HighlightedHandle) {
  const Tris = [];
  const P0 = [0, 0, 0], P1 = [0, 0, 0], P2 = [0, 0, 0];
  const ClampV = LocalKernel.Clamp;
  const MixV = LocalKernel.Mix;

  const PushBuffer = (Buf, VertCount, IsTranslucent) => {
    for (let V = 0; V + 2 < VertCount; V += 3) {
      const B0 = V * 12, B1 = (V + 1) * 12, B2 = (V + 2) * 12;
      if (!ProjectVertex(Buf, B0, ViewProj, CanvasW, CanvasH, P0)) continue;
      if (!ProjectVertex(Buf, B1, ViewProj, CanvasW, CanvasH, P1)) continue;
      if (!ProjectVertex(Buf, B2, ViewProj, CanvasW, CanvasH, P2)) continue;

      const Nx = (Buf[B0 + 3] + Buf[B1 + 3] + Buf[B2 + 3]) / 3;
      const Ny = (Buf[B0 + 4] + Buf[B1 + 4] + Buf[B2 + 4]) / 3;
      const Nz = (Buf[B0 + 5] + Buf[B1 + 5] + Buf[B2 + 5]) / 3;
      const NLen = Math.max(1e-5, Math.sqrt(Nx * Nx + Ny * Ny + Nz * Nz));

      let Cr = Buf[B0 + 6], Cg = Buf[B0 + 7], Cb = Buf[B0 + 8], Ca = Buf[B0 + 9];
      const HandleId = Buf[B0 + 10];
      const Unlit = Buf[B0 + 11];

      if (Math.abs(HandleId - HighlightedHandle) < 0.25 && HighlightedHandle > 0.5) {
        Cr = MixV(Cr * 1.24 + 0.10, 1.0, 0.22);
        Cg = MixV(Cg * 1.24 + 0.10, 0.98, 0.22);
        Cb = MixV(Cb * 1.24 + 0.10, 0.88, 0.22);
        Ca = Math.min(1.0, Ca * 1.45 + 0.14);
      }

      if (Unlit <= 0.5) {
        const Key = Math.max((Nx * 0.46 - Ny * 0.52 + Nz * 0.72) / NLen, 0.0);
        const Fill = Math.max((-Nx * 0.55 + Ny * 0.62 + Nz * 0.56) / NLen, 0.0);
        const Shade = 0.56 + 0.34 * Key + 0.14 * Fill;
        Cr *= Shade; Cg *= Shade; Cb *= Shade;
      }

      const R8 = ClampV((Cr * 255 + 0.5) | 0, 0, 255);
      const G8 = ClampV((Cg * 255 + 0.5) | 0, 0, 255);
      const B8 = ClampV((Cb * 255 + 0.5) | 0, 0, 255);
      const A = ClampV(Ca, 0, 1);

      Tris.push({
        x0: P0[0], y0: P0[1],
        x1: P1[0], y1: P1[1],
        x2: P2[0], y2: P2[1],
        depth: (P0[2] + P1[2] + P2[2]) / 3 + (IsTranslucent ? 0.002 : 0),
        style: `rgba(${R8},${G8},${B8},${A.toFixed(3)})`,
      });
    }
  };

  PushBuffer(BuiltGizmo.OpaqueBuffer, BuiltGizmo.OpaqueVertexCount, false);
  PushBuffer(BuiltGizmo.TranslucentBuffer, BuiltGizmo.TranslucentVertexCount, true);

  Tris.sort((A, B) => B.depth - A.depth);

  Ctx.save();
  Ctx.lineJoin = "round";
  Ctx.lineWidth = 0.65;
  for (let I = 0; I < Tris.length; ++I) {
    const T = Tris[I];
    Ctx.fillStyle = T.style;
    Ctx.strokeStyle = T.style;
    Ctx.beginPath();
    Ctx.moveTo(T.x0, T.y0);
    Ctx.lineTo(T.x1, T.y1);
    Ctx.lineTo(T.x2, T.y2);
    Ctx.closePath();
    Ctx.fill();
    Ctx.stroke();
  }
  Ctx.restore();
}
