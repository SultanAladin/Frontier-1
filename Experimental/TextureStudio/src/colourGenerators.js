import { hexToHSV, hsvToHex } from "./colourConversion.js";

// Deterministic 32-bit PRNG (mulberry32) so a repeated seed always reproduces the same suggestion.
function mulberry32(seed) {
  let state = seed >>> 0;
  return function next() {
    state = (state + 0x6d2b79f5) | 0;
    let t = Math.imul(state ^ (state >>> 15), 1 | state);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// Named colour generators shared by every colour control in the studio. Each produces a single
// authored hex value from the current colour (and a seed, for the randomised ones) rather than a
// spatial mask — the existing CHANNEL_GENERATORS stay the procedural-texture toolkit for channels
// that vary across a surface.
export const COLOUR_GENERATORS = [
  {
    id: "complementary",
    label: "Complementary",
    note: "Opposite hue at the same saturation and brightness.",
  },
  {
    id: "harmonicHue",
    label: "Harmonic hue",
    note: "Golden-angle hue rotation from the current colour.",
  },
  {
    id: "analogous",
    label: "Analogous",
    note: "Neighbouring hue with a small seeded variation.",
  },
  {
    id: "proceduralNoise",
    label: "Procedural noise",
    note: "Seeded random hue, saturation and brightness sample.",
  },
];

export function generateColour(id, baseHex, seed = Date.now()) {
  const { h, s, v } = hexToHSV(/^#[0-9a-f]{6}$/i.test(baseHex || "") ? baseHex : "#808080");
  const random = mulberry32(Math.abs(Math.floor(seed)) >>> 0);
  switch (id) {
    case "complementary":
      return hsvToHex(h + 180, s, v);
    case "harmonicHue":
      return hsvToHex(h + 137.50776405, s, v);
    case "analogous":
      return hsvToHex(h + 20 + random() * 25, s, v);
    case "proceduralNoise":
    default:
      return hsvToHex(random() * 360, 0.35 + random() * 0.5, 0.45 + random() * 0.45);
  }
}
