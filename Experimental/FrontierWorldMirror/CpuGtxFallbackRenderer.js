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

//==========================================================================================================================================
// HERMETIC BAND KERNEL FACTORY (Zero free outer variables -> 100% safe for Blob Web Worker serialization even under esbuild --minify!)
//==========================================================================================================================================
export function CreateGtxBandKernel(AtlasWidth, AtlasCols, AtlasTileSize, TilesPerPrim) {
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

  function SdBox(Lx, Ly, Lz, Hx, Hy, Hz) {
    const Qx = Math.abs(Lx) - Hx;
    const Qy = Math.abs(Ly) - Hy;
    const Qz = Math.abs(Lz) - Hz;
    const Ox = Qx > 0 ? Qx : 0;
    const Oy = Qy > 0 ? Qy : 0;
    const Oz = Qz > 0 ? Qz : 0;
    const InMax = Math.max(Qx, Qy, Qz);
    return Math.sqrt(Ox * Ox + Oy * Oy + Oz * Oz) + (InMax < 0 ? InMax : 0);
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
      this._Hit = {
        hit: false, t: 50, primIdx: -1,
        px: 0, py: 0, pz: 0,
        nx: 0, ny: 0, nz: 1,
        tx: 1, ty: 0, tz: 0,
        u: 0.5, v: 0.5,
      };
      this._SecHit = {
        hit: false, t: 50, primIdx: -1,
        px: 0, py: 0, pz: 0,
        nx: 0, ny: 0, nz: 1,
        tx: 1, ty: 0, tz: 0,
        u: 0.5, v: 0.5,
      };
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
        const DiffW = 1.0 - P.Metallic * 0.75;
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

    TraceScene(Prims, Rox, Roy, Roz, Rdx, Rdy, Rdz, TMax, SkipIdx, SkipGlass, OutHit) {
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
      if (BestIdx < 0) {
        OutHit.hit = false;
        OutHit.t = TMax;
        OutHit.primIdx = -1;
        return OutHit;
      }
      return this.FinalizeHit(Prims, BestIdx, Rox, Roy, Roz, Rdx, Rdy, Rdz, BestT, false, OutHit);
    }

    EvalLocalPrimSdf(Shape, Lpx, Lpy, Lpz, Hx, Hy, Hz) {
      if (Shape === 4) {
        const MajorR = Math.max(Hx, Hy) * 0.68;
        const MinorR = Math.max(0.04, Hz * 0.55);
        return Len2(Len2(Lpx, Lpy) - MajorR, Lpz) - MinorR;
      }
      if (Shape === 3) {
        const Rad = Math.max(Hx, Hy);
        const ZBase = -0.78 * Hz;
        const DPlinth = SdBox(Lpx, Lpy, Lpz - 0.5 * (-Hz + ZBase), Rad, Rad, 0.5 * (ZBase + Hz));
        const TAlpha = Clamp((Lpz - ZBase) / Math.max(1e-4, Hz - ZBase), 0.0, 1.0);
        const RAtZ = Mix(0.72 * Rad, 0.13 * Rad, TAlpha);
        const Dx = Len2(Lpx, Lpy) - RAtZ;
        const Dy = Math.abs(Lpz - 0.5 * (ZBase + Hz)) - 0.5 * (Hz - ZBase);
        const DCone = Math.min(Math.max(Dx, Dy), 0.0) + Len2(Math.max(Dx, 0.0), Math.max(Dy, 0.0));
        return Math.min(DPlinth, DCone);
      }
      if (Shape === 5) {
        const Rad = Math.max(Hx, Hy);
        const ZBase = -0.84 * Hz;
        const Bx = Len2(Lpx, Lpy) - 0.97 * Rad;
        const By = Math.abs(Lpz - 0.5 * (-Hz + ZBase)) - 0.5 * (ZBase + Hz);
        const DBot = Math.min(Math.max(Bx, By), 0.0) + Len2(Math.max(Bx, 0.0), Math.max(By, 0.0));
        const TAlpha = Clamp((Lpz - ZBase) / Math.max(1e-4, Hz - ZBase), 0.0, 1.0);
        const RAtZ = Mix(0.50 * Rad, 0.37 * Rad, TAlpha);
        const Tx = Len2(Lpx, Lpy) - RAtZ;
        const Ty = Math.abs(Lpz - 0.5 * (ZBase + Hz)) - 0.5 * (Hz - ZBase);
        const DTop = Math.min(Math.max(Tx, Ty), 0.0) + Len2(Math.max(Tx, 0.0), Math.max(Ty, 0.0));
        return Math.min(DBot, DTop);
      }
      if (Shape >= 2) {
        const Cx = Len2(Lpx, Lpy) - Math.max(Hx, Hy);
        const Cy = Math.abs(Lpz) - Hz;
        return Math.min(Math.max(Cx, Cy), 0.0) + Len2(Math.max(Cx, 0.0), Math.max(Cy, 0.0));
      }
      return SdBox(Lpx, Lpy, Lpz, Hx, Hy, Hz);
    }

    ClosestRayPrimSoftVis(P, Ox, Oy, Oz, DirX, DirY, DirZ, TanSun) {
      const Dx = Ox - P.Cx, Dy = Oy - P.Cy, Dz = Oz - P.Cz;
      const Lox = P.R0x * Dx + P.R0y * Dy + P.R0z * Dz;
      const Loy = P.R1x * Dx + P.R1y * Dy + P.R1z * Dz;
      const Loz = P.R2x * Dx + P.R2y * Dy + P.R2z * Dz;
      const Ldx = P.R0x * DirX + P.R0y * DirY + P.R0z * DirZ;
      const Ldy = P.R1x * DirX + P.R1y * DirY + P.R1z * DirZ;
      const Ldz = P.R2x * DirX + P.R2y * DirY + P.R2z * DirZ;
      const Hx = P.Hx, Hy = P.Hy, Hz = P.Hz;

      const BoundR = P.BoundR * 1.27;
      const TCenter = -(Lox * Ldx + Loy * Ldy + Loz * Ldz);
      if (TCenter < -BoundR) return 1.0;
      const MaxPen = 0.004 + Math.max(0.04, TCenter) * TanSun;
      const Tc = TCenter > 0 ? TCenter : 0;
      const Perp = Len3(Lox + Ldx * Tc, Loy + Ldy * Tc, Loz + Ldz * Tc);
      if (Perp > BoundR + MaxPen) return 1.0;

      if (P.Shape === 1) {
        const Ux = Lox / Hx, Uy = Loy / Hy, Uz = Loz / Hz;
        const Vx = Ldx / Hx, Vy = Ldy / Hy, Vz = Ldz / Hz;
        const DLen = Math.max(Len3(Vx, Vy, Vz), 1e-5);
        const Dnx = Vx / DLen, Dny = Vy / DLen, Dnz = Vz / DLen;
        const TUnit = -(Ux * Dnx + Uy * Dny + Uz * Dnz);
        if (TUnit <= 0.0) return 1.0;
        const RMean = (Hx + Hy + Hz) * 0.3333333;
        const WorldT = TUnit / DLen;
        const Clearance = (Len3(Ux + Dnx * TUnit, Uy + Dny * TUnit, Uz + Dnz * TUnit) - 1.0) * RMean;
        const ConeR = 0.0025 + WorldT * TanSun;
        return Smoothstep(-ConeR * 0.45, ConeR, Clearance);
      }

      const Hpx = Hx + MaxPen, Hpy = Hy + MaxPen, Hpz = Hz + MaxPen;
      const InvX = 1.0 / (Math.abs(Ldx) < 1e-6 ? (Ldx >= 0 ? 1e-6 : -1e-6) : Ldx);
      const InvY = 1.0 / (Math.abs(Ldy) < 1e-6 ? (Ldy >= 0 ? 1e-6 : -1e-6) : Ldy);
      const InvZ = 1.0 / (Math.abs(Ldz) < 1e-6 ? (Ldz >= 0 ? 1e-6 : -1e-6) : Ldz);
      const T0x = (-Hpx - Lox) * InvX, T1x = (Hpx - Lox) * InvX;
      const T0y = (-Hpy - Loy) * InvY, T1y = (Hpy - Loy) * InvY;
      const T0z = (-Hpz - Loz) * InvZ, T1z = (Hpz - Loz) * InvZ;
      const TNear = Math.max(0.003, T0x < T1x ? T0x : T1x, T0y < T1y ? T0y : T1y, T0z < T1z ? T0z : T1z);
      const TFar = Math.min(24.0, T0x > T1x ? T0x : T1x, T0y > T1y ? T0y : T1y, T0z > T1z ? T0z : T1z);
      if (TNear > TFar) return 1.0;

      let BestVis = 1.0;
      if (P.Shape === 0) {
        const Tb0x = (-Hx - Lox) * InvX, Tb1x = (Hx - Lox) * InvX;
        const Tb0y = (-Hy - Loy) * InvY, Tb1y = (Hy - Loy) * InvY;
        const Tb0z = (-Hz - Loz) * InvZ, Tb1z = (Hz - Loz) * InvZ;
        const TbNear = Math.max(Tb0x < Tb1x ? Tb0x : Tb1x, Tb0y < Tb1y ? Tb0y : Tb1y, Tb0z < Tb1z ? Tb0z : Tb1z);
        const TbFar = Math.min(Tb0x > Tb1x ? Tb0x : Tb1x, Tb0y > Tb1y ? Tb0y : Tb1y, Tb0z > Tb1z ? Tb0z : Tb1z);
        if (TbNear <= TbFar && TbFar > 0.003) {
          const TSeed = Clamp(0.5 * (Math.max(0.003, TbNear) + TbFar), TNear, TFar);
          const DSeed = SdBox(Lox + Ldx * TSeed, Loy + Ldy * TSeed, Loz + Ldz * TSeed, Hx, Hy, Hz);
          const CSeed = 0.002 + TSeed * TanSun;
          BestVis = Math.min(BestVis, Smoothstep(-CSeed, CSeed, DSeed));
        }
      } else if (P.Shape === 4) {
        const MajorR = Math.max(Hx, Hy) * 0.68;
        const MinorR = Math.max(0.04, Hz * 0.55);
        const ARing = Ldx * Ldx + Ldy * Ldy;
        if (ARing > 1e-6) {
          const BRing = Lox * Ldx + Loy * Ldy;
          const CRing = Lox * Lox + Loy * Loy - MajorR * MajorR;
          const DiscR = BRing * BRing - ARing * CRing;
          if (DiscR >= 0.0) {
            const SqR = Math.sqrt(DiscR);
            const Tr0 = Clamp((-BRing - SqR) / ARing, TNear, TFar);
            const Tr1 = Clamp((-BRing + SqR) / ARing, TNear, TFar);
            const C0R = 0.002 + Tr0 * TanSun;
            const C1R = 0.002 + Tr1 * TanSun;
            const D0 = Len2(Len2(Lox + Ldx * Tr0, Loy + Ldy * Tr0) - MajorR, Loz + Ldz * Tr0) - MinorR;
            const D1 = Len2(Len2(Lox + Ldx * Tr1, Loy + Ldy * Tr1) - MajorR, Loz + Ldz * Tr1) - MinorR;
            BestVis = Math.min(BestVis, Smoothstep(-C0R, C0R, D0), Smoothstep(-C1R, C1R, D1));
          } else {
            const TCl = Clamp(-BRing / ARing, TNear, TFar);
            const CCl = 0.002 + TCl * TanSun;
            const DCl = Len2(Len2(Lox + Ldx * TCl, Loy + Ldy * TCl) - MajorR, Loz + Ldz * TCl) - MinorR;
            BestVis = Math.min(BestVis, Smoothstep(-CCl, CCl, DCl));
          }
        }
        if (Math.abs(Ldz) > 1e-5) {
          const Tz0 = Clamp(-Loz / Ldz, TNear, TFar);
          const Cz0 = 0.002 + Tz0 * TanSun;
          const Dz0 = Len2(Len2(Lox + Ldx * Tz0, Loy + Ldy * Tz0) - MajorR, Loz + Ldz * Tz0) - MinorR;
          BestVis = Math.min(BestVis, Smoothstep(-Cz0, Cz0, Dz0));
        }
      } else {
        const XyLen2 = Ldx * Ldx + Ldy * Ldy;
        if (XyLen2 > 1e-6) {
          const TAxis = Clamp(-(Lox * Ldx + Loy * Ldy) / XyLen2, TNear, TFar);
          const DAxis = this.EvalLocalPrimSdf(P.Shape, Lox + Ldx * TAxis, Loy + Ldy * TAxis, Loz + Ldz * TAxis, Hx, Hy, Hz);
          const CAxis = 0.002 + TAxis * TanSun;
          BestVis = Math.min(BestVis, Smoothstep(-CAxis, CAxis, DAxis));
        }
      }

      const Span = Math.max(0.004, TFar - TNear);
      const StepU = Span / 16.0;
      for (let S = 0; S <= 16; ++S) {
        const T = TNear + StepU * S;
        const D = this.EvalLocalPrimSdf(P.Shape, Lox + Ldx * T, Loy + Ldy * T, Loz + Ldz * T, Hx, Hy, Hz);
        const ConeR = 0.002 + T * TanSun;
        const V = Smoothstep(-ConeR, ConeR, D);
        if (V < BestVis) BestVis = V;
        if (BestVis <= 0.002) return 0.0;
      }
      return BestVis;
    }

    EvaluateSoftConeVis(Prims, Ox, Oy, Oz, DirX, DirY, DirZ, MaxDist, TanAngle, MinVis, SelfIdx, SkipPickCode = -1) {
      let Vis = 1.0;
      for (let I = 0; I < Prims.length; ++I) {
        if (I === SelfIdx) continue;
        const P = Prims[I];
        if (SkipPickCode >= 0 && P.PickCode === SkipPickCode) continue;
        if (!P.CastShadows || P.GlassShell) continue;
        const TProj = (P.Cx - Ox) * DirX + (P.Cy - Oy) * DirY + (P.Cz - Oz) * DirZ;
        if (TProj <= -0.5 || TProj >= MaxDist) continue;
        const SoftV = Mix(MinVis, 1.0, this.ClosestRayPrimSoftVis(P, Ox, Oy, Oz, DirX, DirY, DirZ, TanAngle));
        if (SoftV < Vis) Vis = SoftV;
        if (Vis <= MinVis + 0.002) return MinVis;
      }
      return Vis;
    }

    EvaluateGtxConeGiAndAo(Prims, Lights, PosX, PosY, PosZ, Nx, Ny, Nz, SelfIdx, SkyRad, SunDir, SunIrr, OutGiAo) {
      const SkyOut = this._Tmp4;
      const Sz = Nz > 0.25 ? Nz : 0.25;
      const SLen = Len3(Nx * 0.35, Ny * 0.35, Sz);
      this.EvalSky((Nx * 0.35) / SLen, (Ny * 0.35) / SLen, Sz / SLen, SkyRad, SunDir, SunIrr, SkyOut);
      const SkyW = Clamp(Nz * 0.5 + 0.5, 0.0, 1.0);
      const SkyAmbR = Mix(0.055, SkyOut[0], SkyW) * 0.78;
      const SkyAmbG = Mix(0.053, SkyOut[1], SkyW) * 0.78;
      const SkyAmbB = Mix(0.052, SkyOut[2], SkyW) * 0.78;

      let BounceR = 0, BounceG = 0, BounceB = 0;
      let AoOcc = 0;

      for (let I = 0; I < Prims.length; ++I) {
        if (I === SelfIdx) continue;
        const P = Prims[I];
        if (P.GlassShell) continue;

        let Occ = 0, GiFF = 0;
        let EmNx = -Nx, EmNy = -Ny, EmNz = -Nz;

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
          Occ = Horizon * SolidRatio * NearFade * 0.75;
          GiFF = Horizon * SolidRatio * 0.35;
          EmNx = -Dx; EmNy = -Dy; EmNz = -Dz;
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
            const Clx = Clamp(Lpx, -P.Hx, P.Hx) - Lpx;
            const Cly = Clamp(Lpy, -P.Hy, P.Hy) - Lpy;
            const Clz = Clamp(Lpz, -P.Hz, P.Hz) - Lpz;
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
            Occ = Contact * 0.38 + SolidOmega * Math.exp(-Dist / 1.1) * HorFade * 0.12;
            const EmitCos = Math.max(0.0, -(Bfx * Idx2 + Bfy * Idy2 + Bfz * Idz2));
            GiFF = SolidOmega * HorFade * (0.35 + 0.65 * RecCos) * (0.35 + 0.65 * EmitCos);
          }
        }

        AoOcc += Occ;
        if (GiFF > 1e-4 && P.GiContributor) {
          let Pr = P.BounceR * P.Emission;
          let Pg = P.BounceG * P.Emission;
          let Pb = P.BounceB * P.Emission;
          const SunNoL = Math.max(0.0, EmNx * SunDir[0] + EmNy * SunDir[1] + EmNz * SunDir[2]);
          const SunFactor = (SunNoL * 0.62 + 0.10) / PI;
          Pr += P.BounceR * SunIrr[0] * SunFactor;
          Pg += P.BounceG * SunIrr[1] * SunFactor;
          Pb += P.BounceB * SunIrr[2] * SunFactor;

          if (Lights.length > 0) {
            const L0 = Lights[0];
            const Tlx = L0.Cx - P.Cx, Tly = L0.Cy - P.Cy, Tlz = L0.Cz - P.Cz;
            const D2 = Math.max(0.6, Tlx * Tlx + Tly * Tly + Tlz * Tlz);
            const InvL = 1.0 / Math.sqrt(D2);
            const Ldx2 = Tlx * InvL, Ldy2 = Tly * InvL, Ldz2 = Tlz * InvL;
            const Area = 4.0 * L0.Hw * L0.Hh;
            const LScale = (Math.max(0.15, EmNx * Ldx2 + EmNy * Ldy2 + EmNz * Ldz2) * Math.max(0.1, -Ldz2) * Area) / (PI * PI * D2);
            Pr += P.BounceR * L0.RadR * LScale;
            Pg += P.BounceG * L0.RadG * LScale;
            Pb += P.BounceB * L0.RadB * LScale;
          }
          BounceR += Pr * GiFF * 1.35;
          BounceG += Pg * GiFF * 1.35;
          BounceB += Pb * GiFF * 1.35;
        }
      }

      const Ao = Clamp(1.0 - AoOcc, 0.26, 1.0);
      const SkyScale = Mix(0.45, 1.0, Ao);
      OutGiAo[0] = SkyAmbR * SkyScale + BounceR;
      OutGiAo[1] = SkyAmbG * SkyScale + BounceG;
      OutGiAo[2] = SkyAmbB * SkyScale + BounceB;
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

    EvalSky(Rdx, Rdy, Rdz, SkyRad, SunDir, SunIrr, OutRgb) {
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

    ShadeSecondaryBounce(Prims, Lights, AtlasPixels, Hit, Vx, Vy, Vz, SkyRad, SunDir, SunIrr, OutRgb) {
      const M = this.FetchMaterialInto(Prims, AtlasPixels, Hit.primIdx, Hit.u, Hit.v, this._SecMat);
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

      if (Lights.length > 0) {
        const L0 = Lights[0];
        const Tlx = L0.Cx - Hit.px, Tly = L0.Cy - Hit.py, Tlz = L0.Cz - Hit.pz;
        const D2 = Math.max(0.25, Tlx * Tlx + Tly * Tly + Tlz * Tlz);
        const InvD = 1.0 / Math.sqrt(D2);
        const Ldx = Tlx * InvD, Ldy = Tly * InvD, Ldz = Tlz * InvD;
        const Area = 4.0 * L0.Hw * L0.Hh;
        const K = ((1.0 - M.metallic) / PI) * (Math.max(0, Nx * Ldx + Ny * Ldy + Nz * Ldz) * Math.max(0, -Ldz) * Area * 1.45) / D2;
        R += M.baseR * L0.RadR * K;
        G += M.baseG * L0.RadG * K;
        B += M.baseB * L0.RadB * K;
      }

      const SkyTmp = this._Tmp4;
      this.EvalSky(Nx, Ny, Nz, SkyRad, SunDir, SunIrr, SkyTmp);
      const AmbR = SkyTmp[0] * 0.35 + 0.16 * Math.max(-Nx, 0) + 0.015 * Math.max(Nx, 0) + 0.12;
      const AmbG = SkyTmp[1] * 0.35 + 0.015 * Math.max(-Nx, 0) + 0.15 * Math.max(Nx, 0) + 0.12;
      const AmbB = SkyTmp[2] * 0.35 + 0.012 * Math.max(-Nx, 0) + 0.025 * Math.max(Nx, 0) + 0.12;
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
        Width,
        Height,
        StartRow,
        EndRow,
        ViewModeCode,
        ShowGrid,
        TransparentBackground = false,
      } = Params;

      const { Prims, Lights } = this.PackScene(CompiledScene, AtlasPixels);
      const {
        CameraOrigin, CameraTarget, WorldUp,
        SunDir, SunIrradiance, SunAngularRadiusRad, SkyRadiance, ExposureGain,
      } = CompiledScene;
      const TanSun = Math.tan((SunAngularRadiusRad || 0.009) * 1.15);

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

          const Hit = this.TraceScene(Prims, Rox, Roy, Roz, Rdx, Rdy, Rdz, 50.0, -1, false, this._Hit);
          const TQuad = this.TraceLightQuadT(Lights, Rox, Roy, Roz, Rdx, Rdy, Rdz, Hit.t, QuadOut);

          const PixIdx = Py * Width + Px - RowBaseOffset;
          let LinR = 0, LinG = 0, LinB = 0;
          let EntityCode = 0;
          let HitDist = 50.0;

          if (ViewModeCode >= 10) {
            const Dbg = ViewModeCode - 10;
            if (Hit.hit) {
              EntityCode = Prims[Hit.primIdx].PickCode || (1 + Hit.primIdx);
              HitDist = Hit.t;
              const Mat = this.FetchMaterialInto(Prims, AtlasPixels, Hit.primIdx, Hit.u, Hit.v, this._Mat);
              if (Dbg === 1) {
                const Dv = 1.0 - Clamp(Hit.t / 12.0, 0, 1);
                LinR = Dv; LinG = Dv; LinB = Dv;
              } else if (Dbg === 2) {
                LinR = Fract(EntityCode * 0.37); LinG = Fract(EntityCode * 0.61); LinB = Fract(EntityCode * 0.83);
              } else if (Dbg === 3) {
                LinR = 0.5 + 0.5 * Rdx; LinG = 0.5 + 0.5 * Rdy; LinB = 0.5;
              } else if (Dbg === 4) {
                const Ci = Hit.primIdx + 1;
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
            const P = Prims[Hit.primIdx];
            EntityCode = P.PickCode || (1 + Hit.primIdx);
            HitDist = Hit.t;
            const Mat = this.FetchMaterialInto(Prims, AtlasPixels, Hit.primIdx, Hit.u, Hit.v, this._Mat);

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
              const Boost = 1.0 + P.Emission + Mat.emission;
              LinR = Mat.baseR * Boost; LinG = Mat.baseG * Boost; LinB = Mat.baseB * Boost;
            } else if (ViewModeCode === 3) {
              const EdgeU = Math.min(Fract(Hit.u * 8), 1 - Fract(Hit.u * 8));
              const EdgeV = Math.min(Fract(Hit.v * 8), 1 - Fract(Hit.v * 8));
              const Wire = Math.min(EdgeU, EdgeV) < 0.045 ? 1.0 : 0.08;
              LinR = Mat.baseR * Wire + (Wire > 0.5 ? 0.22 : 0);
              LinG = Mat.baseG * Wire + (Wire > 0.5 ? 0.38 : 0);
              LinB = Mat.baseB * Wire + (Wire > 0.5 ? 0.55 : 0);
            } else if (P.GlassShell && ViewModeCode === 0) {
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
                Prims, Hit.px + Nx * 0.012, Hit.py + Ny * 0.012, Hit.pz + Nz * 0.012,
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
              const BgHit = this.TraceScene(Prims, BgOx, BgOy, BgOz, ExDirX, ExDirY, ExDirZ, 32.0, Hit.primIdx, true, this._SecHit);
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
              LinR = Mat.baseR * (P.Emission + Mat.emission);
              LinG = Mat.baseG * (P.Emission + Mat.emission);
              LinB = Mat.baseB * (P.Emission + Mat.emission);

              // 1. Solar Direct + Analytical Soft Cone Shadow
              const SunNoL = Nx * SunDir[0] + Ny * SunDir[1] + Nz * SunDir[2];
              if (SunNoL > 0.0) {
                const SunVis = this.EvaluateSoftConeVis(
                  Prims,
                  Hit.px + Nx * 0.006, Hit.py + Ny * 0.006, Hit.pz + Nz * 0.006,
                  SunDir[0], SunDir[1], SunDir[2], 28.0, TanSun, 0.0, Hit.primIdx,
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
                  if (DistL >= 0.05) {
                    const TanArea = Clamp((Len2(L.Hw, L.Hh) * 0.48) / Math.max(DistL, 0.35), 0.10, 0.38);
                    AreaVis = this.EvaluateSoftConeVis(
                      Prims,
                      Hit.px + Nx * 0.007, Hit.py + Ny * 0.007, Hit.pz + Nz * 0.007,
                      Tlx / DistL, Tly / DistL, Tlz / DistL, DistL - 0.05, TanArea, 0.18, Hit.primIdx, L.PickCode,
                    );
                  }
                  const Kd = (1.0 - Mat.metallic) * (1.0 - Mat.transmission);
                  LinR += Kd * Mat.baseR * DiffR * AreaVis;
                  LinG += Kd * Mat.baseG * DiffG * AreaVis;
                  LinB += Kd * Mat.baseB * DiffG * 0 + Kd * Mat.baseB * DiffB * AreaVis;

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

              // 3. Smooth Analytical Solid-Angle GI + AO
              const GiAo = this._GiOut;
              this.EvaluateGtxConeGiAndAo(
                Prims, Lights, Hit.px, Hit.py, Hit.pz, Nx, Ny, Nz, Hit.primIdx,
                SkyRadiance, SunDir, SunIrradiance, GiAo,
              );
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

              this.EvalSky(Rx2, Ry2, Rz2, SkyRadiance, SunDir, SunIrradiance, SkyOut);
              let SpecEnvR = Mix(GiAo[0], SkyOut[0], 0.55);
              let SpecEnvG = Mix(GiAo[1], SkyOut[1], 0.55);
              let SpecEnvB = Mix(GiAo[2], SkyOut[2], 0.55);

              if (ViewModeCode === 0 && RaySharpness > 0.02) {
                const Ox2 = Hit.px + Nx * 0.012, Oy2 = Hit.py + Ny * 0.012, Oz2 = Hit.pz + Nz * 0.012;
                const RHit = this.TraceScene(Prims, Ox2, Oy2, Oz2, Rx2, Ry2, Rz2, 22.0, Hit.primIdx, true, this._SecHit);
                const TqRefl = this.TraceLightQuadT(Lights, Ox2, Oy2, Oz2, Rx2, Ry2, Rz2, RHit.t, QuadOut);
                let SharpR = SkyOut[0], SharpG = SkyOut[1], SharpB = SkyOut[2];
                if (TqRefl < RHit.t) {
                  SharpR = QuadOut[0]; SharpG = QuadOut[1]; SharpB = QuadOut[2];
                } else if (RHit.hit) {
                  this.ShadeSecondaryBounce(Prims, Lights, AtlasPixels, RHit, -Rx2, -Ry2, -Rz2, SkyRadiance, SunDir, SunIrradiance, SecOut);
                  SharpR = SecOut[0]; SharpG = SecOut[1]; SharpB = SecOut[2];
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
            this.EvalSky(Rdx, Rdy, Rdz, SkyRadiance, SunDir, SunIrradiance, SkyOut);
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
  }

  _EnsureFrontBuffers(W, H) {
    if (this.BufW === W && this.BufH === H && this.OffscreenImg) return;
    this.BufW = W;
    this.BufH = H;
    const N = W * H;
    this.DepthBuf = new Float32Array(N);
    this.EntityBuf = new Uint8Array(N);
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
    this._BackEntity = new Uint8Array(N);
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

    const StartRow = this.NextRow;
    const EndRow = Math.min(Height, StartRow + Math.max(1, RowBudget));
    this._LocalEngine.RenderBand(
      { ...Params, StartRow, EndRow },
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
    this._LocalEngine.RenderBand(
      { ...Params, StartRow: 0, EndRow: Height },
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
        `const Kernel = (${CreateGtxBandKernel.toString()})(${ATLAS_WIDTH}, ${ATLAS_COLS}, ${ATLAS_TILE_SIZE}, ${TILES_PER_PRIM});`,
        `const Engine = new Kernel.GtxBandEngine();`,
        `let CachedAtlas = null;`,
        `self.onmessage = (Ev) => {`,
        `  const Msg = Ev.data;`,
        `  if (!Msg) return;`,
        `  if (Msg.Kind === "atlas") {`,
        `    CachedAtlas = new Uint8Array(Msg.AtlasBuffer);`,
        `    return;`,
        `  }`,
        `  if (Msg.Kind === "band") {`,
        `    const { JobId, CompiledScene, Width, Height, StartRow, EndRow, ViewModeCode, ShowGrid } = Msg;`,
        `    const Rows = Math.max(0, EndRow - StartRow);`,
        `    const Pix = Width * Rows;`,
        `    const Rgba = new Uint8ClampedArray(Pix * 4);`,
        `    const Depth = new Float32Array(Pix);`,
        `    const Entity = new Uint8Array(Pix);`,
        `    Engine.RenderBand({ CompiledScene, AtlasPixels: CachedAtlas, Width, Height, StartRow, EndRow, ViewModeCode, ShowGrid }, Rgba, Depth, Entity, true);`,
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
    Job.Entity.set(new Uint8Array(EntityBuffer), PixOffset);

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
      Width,
      Height,
      ViewModeCode,
      ShowGrid,
    } = Params;

    if (AtlasPixels && this._SyncedAtlasVersion !== AtlasVersion) {
      this._SyncedAtlasVersion = AtlasVersion;
      for (let I = 0; I < this._Workers.length; ++I) {
        const Copy = AtlasPixels.slice();
        this._Workers[I].postMessage({ Kind: "atlas", AtlasBuffer: Copy.buffer }, [Copy.buffer]);
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
      Entity: new Uint8Array(N),
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
