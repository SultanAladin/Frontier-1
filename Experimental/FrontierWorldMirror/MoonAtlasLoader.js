//==========================================================================================================================================
// FILE: MoonAtlasLoader.js
// PURPOSE: Decodes the 6 embedded 2K-derived celestial moon textures (luna, ember, glacier, sulfur, shroud, shard) from
//          window.NativeAssets.Moons into a single stacked RGBA8 texture atlas (6 vertical equirectangular tiles) for
//          WebGPU, WebGL2, and CpuGtxFallbackRenderer.
//==========================================================================================================================================

export const MOON_TILE_W = 512;
export const MOON_TILE_H = 256;
export const MOON_TILE_COUNT = 6;
export const MOON_ATLAS_W = MOON_TILE_W;
export const MOON_ATLAS_H = MOON_TILE_H * MOON_TILE_COUNT;
export const MOON_PRESET_NAMES = ["luna", "ember", "glacier", "sulfur", "shroud", "shard"];

let CachedMoonAtlas = null;
let LoadPromise = null;
const ReadyListeners = new Set();

function CreateFallbackMoonAtlas() {
  const Pixels = new Uint8Array(MOON_ATLAS_W * MOON_ATLAS_H * 4);
  for (let i = 0; i < Pixels.length; i += 4) {
    Pixels[i] = 212;
    Pixels[i + 1] = 210;
    Pixels[i + 2] = 204;
    Pixels[i + 3] = 255;
  }
  return {
    width: MOON_ATLAS_W,
    height: MOON_ATLAS_H,
    pixels: Pixels,
    ready: false,
    version: 0,
  };
}

export function GetMoonAtlasSnapshot() {
  if (!CachedMoonAtlas) {
    CachedMoonAtlas = CreateFallbackMoonAtlas();
  }
  return CachedMoonAtlas;
}

export function LoadMoonAtlas(OnReady) {
  if (typeof OnReady === "function") {
    if (CachedMoonAtlas?.ready) {
      OnReady(CachedMoonAtlas);
    } else {
      ReadyListeners.add(OnReady);
    }
  }
  if (CachedMoonAtlas?.ready) return Promise.resolve(CachedMoonAtlas);
  if (LoadPromise) return LoadPromise;

  const Moons = typeof window !== "undefined" ? window.NativeAssets?.Moons : null;
  if (!Array.isArray(Moons) || Moons.length === 0 || typeof document === "undefined") {
    return Promise.resolve(GetMoonAtlasSnapshot());
  }

  LoadPromise = (async () => {
    try {
      const Canvas = document.createElement("canvas");
      Canvas.width = MOON_ATLAS_W;
      Canvas.height = MOON_ATLAS_H;
      const Ctx = Canvas.getContext("2d", { willReadFrequently: true });
      if (!Ctx) return GetMoonAtlasSnapshot();

      const LoadOne = (Src) =>
        new Promise((Resolve) => {
          if (!Src) return Resolve(null);
          const Img = new Image();
          Img.onload = () => Resolve(Img);
          Img.onerror = () => Resolve(null);
          Img.src = Src;
        });

      const Imgs = await Promise.all(
        Array.from({ length: MOON_TILE_COUNT }, (_, Idx) => LoadOne(Moons[Idx])),
      );

      let LoadedCount = 0;
      Ctx.fillStyle = "#d4d2cc";
      Ctx.fillRect(0, 0, MOON_ATLAS_W, MOON_ATLAS_H);
      for (let Idx = 0; Idx < MOON_TILE_COUNT; Idx++) {
        const Img = Imgs[Idx];
        if (Img) {
          Ctx.drawImage(Img, 0, Idx * MOON_TILE_H, MOON_TILE_W, MOON_TILE_H);
          LoadedCount++;
        }
      }

      if (LoadedCount > 0) {
        const Data = Ctx.getImageData(0, 0, MOON_ATLAS_W, MOON_ATLAS_H);
        CachedMoonAtlas = {
          width: MOON_ATLAS_W,
          height: MOON_ATLAS_H,
          pixels: new Uint8Array(Data.data.buffer, Data.data.byteOffset, Data.data.byteLength),
          ready: true,
          version: (CachedMoonAtlas?.version || 0) + 1,
        };
        for (const Cb of ReadyListeners) {
          try { Cb(CachedMoonAtlas); } catch {}
        }
        ReadyListeners.clear();
      }
    } catch {}
    return GetMoonAtlasSnapshot();
  })();

  return LoadPromise;
}
