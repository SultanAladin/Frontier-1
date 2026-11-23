//============================================================================================================================================
//                                                              BAKEDMAPS.JS
//============================================================================================================================================
// 🧩 The browser bakes behind the 3D viewport, shared with the inspectors' BAKING sections so both show the same images:
//   • Atmosphere: the transmittance LUT (sunlight left after the air, by view elevation and altitude) and the sky-view map (the
//     360° × 180° equirectangular sky the viewport draws as its background);
//   • Sun: the lighting LUT (the sun beam and the sky fill the viewport's lights take, across sun elevations) and the disc map (the sun
//     as the viewport draws it, close up);
//   • Stars: the star map (the catalogue the viewport's star field draws, in celestial coordinates);
//   • Lens Flare: the flare atlas (LensFlare.js) the viewport's flare is put together from.
// Each bake is held by everything it reads, so the viewport and an open inspector never bake the same map twice. Browser bakes only: no
// native targets or files are written (the cards' Save PNG downloads the image).

import { PaintSky, ViewBasis, Direction, Transmittance, SunAngularRadius, KelvinTint } from "./SkyShader.js";
import { PaintFlareAtlas, FlareCells } from "./LensFlare.js";

const Clamp = (Amount, Low = 0, High = 1) => Math.max(Low, Math.min(High, Amount));
const Linear = (Channel) => (Channel <= 0.04045 ? Channel / 12.92 : ((Channel + 0.055) / 1.055) ** 2.4);
const Encode = (Channel) => Math.round(255 * Clamp(Channel <= 0.0031308 ? Channel * 12.92 : 1.055 * Channel ** (1 / 2.4) - 0.055));
const HexLinear = (Text) => [1, 3, 5].map((At) => Linear(parseInt(Text.slice(At, At + 2), 16) / 255));

// ── The store ───────────────────────────────────────────────────────────────────────────────────────────────────────────────────
const Held = new Map();
const Watchers = new Set();
// Called with a bake's name whenever it is baked again.
export function WatchBakes(Callback) {
  Watchers.add(Callback);
  return () => Watchers.delete(Callback);
}
// The bake held under a name (from any settings), or null when it was never baked.
export const HeldBake = (Name) => Held.get(Name) || null;
function Bake(Name, Key, Make, Again = false) {
  const Before = Held.get(Name);
  if (!Again && Before && Before.Key === Key) return Before;
  const Start = performance.now();
  const Canvas = Before?.Canvas || document.createElement("canvas");
  const Extra = Make(Canvas) || {};
  const Result = { Name, Key, Canvas, Width: Canvas.width, Height: Canvas.height, Milliseconds: performance.now() - Start, At: Date.now(), Count: (Before?.Count || 0) + 1, ...Extra };
  Held.set(Name, Result);
  queueMicrotask(() => Watchers.forEach((Each) => Each(Name)));
  return Result;
}

// ── Lights (shared with the viewport) ───────────────────────────────────────────────────────────────────────────────────────────
// Below −2° of sun the night look comes in: airglow in the sky and a moonlit fill.
export const NightOf = (Elevation) => Clamp((-Elevation - 2) / 10);
// The sun beam: its temperature's colour times the sunlight left after the air; it fades out across the horizon.
export function SunBeam(Medium, Tint, Elevation) {
  const Through = Transmittance(Medium, Elevation);
  const Colour = Tint.map((C, Channel) => C * Through[Channel]);
  const Strength = Math.max(...Colour);
  return { Colour: Colour.map((C) => (Strength ? C / Strength : 1)), Strength, Intensity: 2.6 * Strength * Clamp((Elevation + 1) / 3) };
}
const Moonlit = HexLinear("#7f95c8"), MoonGround = HexLinear("#2a2f3c");
// The hemisphere fill from the sky's mean colour 30–50° up (linear): colour, ground bounce and strength, moonlit after dusk.
export function SkyFill(Mean, Elevation, Medium, Strength) {
  const Brightest = Math.max(...Mean, 1e-4);
  let Sky = Mean.map((C) => C / Brightest);
  let Ground = Medium.Ground.map((C) => Math.min(1, C * 2.2 * (0.3 + Strength)));
  let Intensity = 0.15 + 1.25 * Math.min(1, Brightest * 1.6);
  const Night = NightOf(Elevation);
  if (Night > 0) {
    Sky = Sky.map((C, Channel) => C + (Moonlit[Channel] - C) * Night);
    Ground = Ground.map((C, Channel) => C + (MoonGround[Channel] - C) * Night);
    Intensity = Math.max(Intensity, 0.9 * Night);
  }
  return { Sky, Ground, Intensity };
}

// ── Atmosphere ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────
let Scratch = null;
// The sky as the viewport draws it, onto `Target` (any 2:1 size): SkyShader.js in equirectangular form with the sun's disc drawn ×4 so
// it reads at viewport sizes, mirrored left to right (three.js reads equirectangular images the other way round), the night airglow
// over the upper half, and below the horizon the colour just above it carried down (the three.js ground covers that half).
function SkyPicture(Target, Medium, Light) {
  Scratch ||= document.createElement("canvas");
  Scratch.width = Target.width;
  Scratch.height = Target.height;
  PaintSky(Scratch, Medium, { Sun: Light.Direction, Tint: Light.Tint }, { Mode: "equirect", Basis: ViewBasis([0, 0, 1]), Warp: true, Disc: SunAngularRadius * 4, Glow: 0.5, Stars: 0, Exposure: 1.1 });
  const Brush = Target.getContext("2d", { willReadFrequently: true });
  const [Width, Height] = [Target.width, Target.height];
  Brush.setTransform(-1, 0, 0, 1, Width, 0);
  Brush.drawImage(Scratch, 0, 0);
  Brush.setTransform(1, 0, 0, 1, 0, 0);
  const Horizon = Math.round(Height / 2);
  const Night = NightOf(Light.Elevation);
  if (Night > 0) {
    const Glow = Brush.createLinearGradient(0, 0, 0, Horizon);
    Glow.addColorStop(0, `rgba(6, 9, 20, ${Night})`);
    Glow.addColorStop(0.8, `rgba(14, 20, 40, ${Night})`);
    Glow.addColorStop(1, `rgba(26, 34, 60, ${Night})`);
    Brush.globalCompositeOperation = "lighter";
    Brush.fillStyle = Glow;
    Brush.fillRect(0, 0, Width, Horizon);
    Brush.globalCompositeOperation = "source-over";
  }
  const Above = Math.max(1, Math.round((3 * Height) / 512));
  Brush.drawImage(Target, 0, Horizon - Above, Width, 1, 0, Horizon, Width, Height - Horizon);
}
// Mean linear colour of an equirectangular image's rows between two elevations (degrees).
function Band(Canvas, High, Low) {
  const Brush = Canvas.getContext("2d", { willReadFrequently: true });
  const Top = Math.round((0.5 - High / 180) * Canvas.height), Rows = Math.max(1, Math.round(((High - Low) / 180) * Canvas.height));
  const Pixels = Brush.getImageData(0, Top, Canvas.width, Rows).data;
  const Mean = [0, 0, 0];
  for (let At = 0; At < Pixels.length; At += 4) for (let Channel = 0; Channel < 3; Channel++) Mean[Channel] += Linear(Pixels[At + Channel] / 255) / (Pixels.length / 4);
  return Mean;
}
const LightKey = (Light) => [Light.Elevation, Light.Azimuth, Light.Tint];

// Sky-view map, 1024 × 512: the viewport's background. `Band(High, Low)` gives the colours its lights and haze take.
export function SkyViewMap(Medium, Light, Again = false) {
  return Bake("Sky-view map", JSON.stringify([Medium, LightKey(Light)]), (Canvas) => {
    Canvas.width = 1024;
    Canvas.height = 512;
    SkyPicture(Canvas, Medium, Light);
    return { Format: "RGBA8 sRGB · equirectangular", Band: (High, Low) => Band(Canvas, High, Low) };
  }, Again);
}

// Transmittance LUT, 256 × 64: the sunlight left after crossing the air to space, from a point at an altitude (rows: the ground at the
// bottom, the top of the atmosphere at the top, squared so the dense low air gets more rows) looking up at an elevation (columns: −12°
// to 90°, squared so the horizon gets more columns). Below the planet's edge it is black. The same march as the viewport's sun colour.
export const TransmittanceSpan = { Low: -12, High: 90 };
export const TransmittanceElevation = (U) => TransmittanceSpan.Low + (TransmittanceSpan.High - TransmittanceSpan.Low) * U * U;
export function TransmittanceMap(Medium, Again = false) {
  return Bake("Transmittance LUT", JSON.stringify(Medium), (Canvas) => {
    const [Width, Height] = [256, 64];
    Canvas.width = Width;
    Canvas.height = Height;
    const Brush = Canvas.getContext("2d");
    const Picture = Brush.createImageData(Width, Height);
    for (let Row = 0; Row < Height; Row++) {
      const Up = (Height - Row - 0.5) / Height, Altitude = 2 + (Medium.Top - 2) * Up * Up;
      for (let Column = 0; Column < Width; Column++) {
        const Through = Transmittance(Medium, TransmittanceElevation((Column + 0.5) / Width), Altitude);
        const At = (Row * Width + Column) * 4;
        for (let Channel = 0; Channel < 3; Channel++) Picture.data[At + Channel] = Encode(Through[Channel]);
        Picture.data[At + 3] = 255;
      }
    }
    Brush.putImageData(Picture, 0, 0);
    return { Format: "RGBA8 sRGB · elevation × altitude" };
  }, Again);
}

// ── Sun ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// Lighting LUT, 256 × 64, across sun elevations −12° to 90° (left to right): the top half is the sun beam the viewport's directional
// light takes (colour times strength), the bottom half the sky fill its hemisphere light takes (from a small sky bake every 3°, with the
// moonlit fill after dusk).
export const LightingSpan = { Low: -12, High: 90 };
export function SunLightingMap(Medium, Tint, Again = false) {
  return Bake("Sun lighting LUT", JSON.stringify([Medium, Tint]), (Canvas) => {
    const [Width, Height] = [256, 64];
    const Samples = [];
    const Small = document.createElement("canvas");
    Small.width = 64;
    Small.height = 32;
    for (let Elevation = LightingSpan.Low; Elevation <= LightingSpan.High + 0.01; Elevation += 3) {
      SkyPicture(Small, Medium, { Direction: Direction(Elevation, 180), Tint, Elevation });
      Samples.push(SkyFill(Band(Small, 50, 30), Elevation, Medium, SunBeam(Medium, Tint, Elevation).Strength));
    }
    Canvas.width = Width;
    Canvas.height = Height;
    const Brush = Canvas.getContext("2d");
    const Picture = Brush.createImageData(Width, Height);
    for (let Column = 0; Column < Width; Column++) {
      const Elevation = LightingSpan.Low + ((LightingSpan.High - LightingSpan.Low) * Column) / (Width - 1);
      const Beam = SunBeam(Medium, Tint, Elevation);
      const Place = (Elevation - LightingSpan.Low) / 3, Lower = Math.min(Samples.length - 1, Math.floor(Place)), Upper = Math.min(Samples.length - 1, Lower + 1), T = Place - Lower;
      const Fill = Samples[Lower].Sky.map((C, Channel) => (C * Samples[Lower].Intensity * (1 - T) + Samples[Upper].Sky[Channel] * Samples[Upper].Intensity * T) / 1.4);
      const Sun = Beam.Colour.map((C) => (C * Beam.Intensity) / 2.6);
      for (let Row = 0; Row < Height; Row++) {
        const Colour = Row < Height / 2 - 1 ? Sun : Row < Height / 2 + 1 ? [0, 0, 0] : Fill;
        const At = (Row * Width + Column) * 4;
        for (let Channel = 0; Channel < 3; Channel++) Picture.data[At + Channel] = Encode(Colour[Channel]);
        Picture.data[At + 3] = 255;
      }
    }
    Brush.putImageData(Picture, 0, 0);
    return { Format: "RGBA8 sRGB · beam over fill" };
  }, Again);
}

// Disc map, 192 × 192: the sun as the viewport's sky draws it (same disc ×4, glow and exposure), looked at straight on, about 24 sun
// radii across. Below the horizon it shows the ground in the way.
export function SunDiscMap(Medium, Light, Again = false) {
  return Bake("Sun disc map", JSON.stringify([Medium, LightKey(Light)]), (Canvas) => {
    Canvas.width = Canvas.height = 192;
    const Aim = Light.Direction;
    const Basis = ViewBasis(Aim, Math.abs(Aim[1]) > 0.98 ? [0, 0, 1] : [0, 1, 0]);
    PaintSky(Canvas, Medium, { Sun: Aim, Tint: Light.Tint }, { Mode: "perspective", Basis, TanHalf: Math.tan(SunAngularRadius * 12), Warp: true, Disc: SunAngularRadius * 4, Glow: 0.5, Stars: 0, Exposure: 1.1 });
    return { Format: "RGBA8 sRGB · perspective" };
  }, Again);
}

// ── Stars ───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// The catalogue: 40,000 stars spread evenly over the celestial sphere (pole on +y), with magnitudes in the real sky's spread (each
// magnitude fainter holds about twice as many stars) and colours from orange to blue-white. Seeded, so it is the same every time.
export const StarCount = 40000;
let Catalogue = null;
export function StarCatalogue() {
  if (Catalogue) return Catalogue;
  let Seed = 20261116;
  const Random = () => (Seed = (Seed * 1664525 + 1013904223) >>> 0) / 4294967296;
  const Positions = new Float32Array(StarCount * 3), Colours = new Float32Array(StarCount * 3), Magnitudes = new Float32Array(StarCount);
  for (let Index = 0; Index < StarCount; Index++) {
    const Y = Random() * 2 - 1, Around = Random() * Math.PI * 2, Flat = Math.sqrt(1 - Y * Y);
    Positions.set([Flat * Math.cos(Around), Y, Flat * Math.sin(Around)], Index * 3);
    Magnitudes[Index] = Math.max(-1.5, 8.5 + Math.log10(Math.max(1e-9, Random())) / 0.3);
    const Tint = KelvinTint(3200 + Math.pow(Random(), 0.7) * 9000);
    const Peak = Math.max(...Tint);
    Colours.set(Tint.map((C) => C / Peak), Index * 3);
  }
  return (Catalogue = { Positions, Colours, Magnitudes });
}
// The Stars object's settings for the field and the map.
export function StarSettings(Read) {
  const N = (Name, Fallback) => (Number.isFinite(+Read(Name)) && Read(Name) !== null && Read(Name) !== "" ? +Read(Name) : Fallback);
  return {
    Shown: Read("Star field") == null ? true : !!Read("Star field"),
    Brightness: N("Brightness", 1),
    Size: N("Point Size", 1),
    Limit: N("Limiting magnitude", 5.5),
    Rotation: N("Celestial rotation", 0),
  };
}
// How a star of a magnitude looks under a limiting magnitude: Seen fades it out just past the limit; Bright (0–1) sets its size and
// gain. The viewport's star shader does the same sums.
export function StarLook(Magnitude, Limit) {
  const Lead = Clamp((Limit - Magnitude) / (Limit + 1.5));
  const Seen = Clamp((Limit + 0.6 - Magnitude) / 0.8);
  return { Seen: Seen * Seen * (3 - 2 * Seen), Bright: Lead ** 2.2 };
}
// Star map, 1024 × 512: the catalogue in celestial coordinates (right ascension across, declination up), as the field draws it.
export function StarMap(Settings, Again = false) {
  return Bake("Star map", JSON.stringify(Settings), (Canvas) => {
    Canvas.width = 1024;
    Canvas.height = 512;
    const Brush = Canvas.getContext("2d");
    Brush.fillStyle = "#04050a";
    Brush.fillRect(0, 0, Canvas.width, Canvas.height);
    const { Positions, Colours, Magnitudes } = StarCatalogue();
    let Count = 0;
    Brush.globalCompositeOperation = "lighter";
    for (let Index = 0; Index < StarCount; Index++) {
      const { Seen, Bright } = StarLook(Magnitudes[Index], Settings.Limit);
      if (Seen <= 0) continue;
      Count++;
      const [X, Y, Z] = [Positions[Index * 3], Positions[Index * 3 + 1], Positions[Index * 3 + 2]];
      const U = (Math.atan2(Z, X) / (Math.PI * 2) + 1) % 1, V = 0.5 - Math.asin(Y) / Math.PI;
      const Gain = Math.min(1, (0.3 + 0.7 * Bright) * Settings.Brightness * Seen);
      const Colour = [0, 1, 2].map((Channel) => Math.round(Colours[Index * 3 + Channel] * 255));
      const Radius = ((1.1 + Bright * 2.6) * Settings.Size) / 2.4;
      Brush.fillStyle = `rgba(${Colour.join(",")},${Gain})`;
      if (Radius < 0.8) Brush.fillRect(U * Canvas.width, V * Canvas.height, 1, 1);
      else {
        Brush.beginPath();
        Brush.arc(U * Canvas.width, V * Canvas.height, Radius, 0, Math.PI * 2);
        Brush.fill();
      }
    }
    Brush.globalCompositeOperation = "source-over";
    return { Format: "RGBA8 sRGB · equirectangular, celestial", Stars: Count };
  }, Again);
}

// ── Lens flare ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// Flare atlas, 1024 × 512: eight 256 px sprites (LensFlare.js), tinted by the sun's colour after the air.
export function FlareAtlasMap(Settings, Tint, Again = false) {
  const Rounded = Tint.map((C) => +C.toFixed(2));
  const Shape = { Blades: Settings.Blades, Chromatic: Settings.Chromatic, HaloWidth: Settings.HaloWidth, GhostShape: Settings.GhostShape };
  return Bake("Flare atlas", JSON.stringify([Shape, Rounded]), (Canvas) => {
    PaintFlareAtlas(Canvas, Settings, Rounded);
    return { Format: "RGBA8 sRGB · 4 × 2 sprites", Cells: FlareCells };
  }, Again);
}
