// Top-down SVG prop library. Art is authored centred on (0,0) in world units.
// `radius` drives selection rings and the directional ground shadow.

export const PropCategories = ["Rocks", "Trees", "Plants", "Details"];

const facet = (pts, fill, stroke = "#00000055") =>
  `<path d="${pts}" fill="${fill}" stroke="${stroke}" stroke-width="1"/>`;

export const Props = {
  // ---- rocks -------------------------------------------------------------
  "rock-s": {
    label: "Rock · small",
    category: "Rocks",
    radius: 7,
    art: facet("M-6-3-1-7 5-5 7 1 3 6-4 6-7 1Z", "#8b8b90") +
      facet("M-1-7 5-5 2 0-3-1Z", "#a6a6ab"),
  },
  "rock-m": {
    label: "Rock · medium",
    category: "Rocks",
    radius: 11,
    art:
      facet("M-10-4-4-10 5-9 11-2 8 7 0 11-8 8-11 1Z", "#83838a") +
      facet("M-4-10 5-9 4-2-4-3Z", "#a2a2a9") +
      facet("M4-2 11-2 8 7 2 5Z", "#6d6d74"),
  },
  "rock-l": {
    label: "Rock · large",
    category: "Rocks",
    radius: 16,
    art:
      facet("M-15-6-6-15 6-14 16-4 12 9 1 16-11 12-16 2Z", "#7e7e86") +
      facet("M-6-15 6-14 5-4-6-5Z", "#9d9da6") +
      facet("M5-4 16-4 12 9 3 6Z", "#65656d") +
      `<path d="M-8 3-2 8 5 5" fill="none" stroke="#00000044" stroke-width="1.4"/>`,
  },
  boulder: {
    label: "Boulder",
    category: "Rocks",
    radius: 22,
    art:
      facet("M-21-8-10-21 8-20 22-6 17 12 2 22-15 17-22 3Z", "#77777f") +
      facet("M-10-21 8-20 7-7-10-8Z", "#9a9aa3") +
      facet("M7-7 22-6 17 12 5 8Z", "#5d5d66") +
      `<path d="M-12 4-4 12 6 9M-2-14 2-4-3 4" fill="none" stroke="#00000040" stroke-width="1.5"/>`,
  },

  // ---- trees (canopies seen from above) ---------------------------------
  "tree-oak": {
    label: "Oak",
    category: "Trees",
    radius: 20,
    sway: true,
    art:
      `<circle r="19" fill="#2f6b2b" stroke="#1f4a1d" stroke-width="2"/>` +
      `<circle cx="-6" cy="-5" r="12" fill="#3d8036"/>` +
      `<circle cx="7" cy="4" r="10" fill="#276024"/>` +
      `<circle cx="-3" cy="6" r="8" fill="#46913f" opacity=".85"/>` +
      `<circle cx="4" cy="-7" r="6" fill="#4fa046" opacity=".8"/>` +
      `<circle r="3" fill="#6b4a2c"/>`,
  },
  "tree-pine": {
    label: "Pine",
    category: "Trees",
    radius: 17,
    sway: true,
    art:
      `<path d="M0-18 4-8 13-13 9-3 19 0 9 4 13 13 4 9 0 19-4 9-13 13-9 4-19 0-9-3-13-13-4-8Z" fill="#275c3a" stroke="#1a4129" stroke-width="1.6"/>` +
      `<path d="M0-12 3-4 10-7 6 1 13 3 5 5 8 11 2 8 0 14-2 8-8 11-5 5-13 3-6 1-10-7-3-4Z" fill="#33724a"/>` +
      `<circle r="2.6" fill="#5b4028"/>`,
  },
  "tree-birch": {
    label: "Birch · autumn",
    category: "Trees",
    radius: 16,
    sway: true,
    art:
      `<circle r="15" fill="#c75b2a" stroke="#93401c" stroke-width="2"/>` +
      `<circle cx="-5" cy="-4" r="9" fill="#e07a38"/>` +
      `<circle cx="6" cy="5" r="8" fill="#a94a20"/>` +
      `<circle cx="2" cy="-6" r="6" fill="#f09a4c"/>` +
      `<circle cx="-4" cy="6" r="5" fill="#d96a2e"/>` +
      `<circle r="2.4" fill="#e8e0d0"/>`,
  },

  // ---- plants ------------------------------------------------------------
  bush: {
    label: "Bush",
    category: "Plants",
    radius: 10,
    sway: true,
    art:
      `<circle cx="-4" cy="0" r="7" fill="#37732f" stroke="#245021" stroke-width="1.4"/>` +
      `<circle cx="4" cy="-2" r="6.5" fill="#428a37"/>` +
      `<circle cx="1" cy="5" r="6" fill="#2e6529"/>` +
      `<circle cx="-2" cy="-3" r="4" fill="#4fa044"/>`,
  },
  grass: {
    label: "Grass tuft",
    category: "Plants",
    radius: 8,
    sway: true,
    art:
      `<path d="M0 7C-2 2-6-1-8-6M0 7C-1 1-1-4 0-8M0 7C2 2 6-1 8-6M0 7C1 3 4 0 6-3M0 7C-1 3-4 0-6-3" fill="none" stroke="#6fae45" stroke-width="1.8" stroke-linecap="round"/>` +
      `<path d="M0 7C0 2 1-2 3-6" fill="none" stroke="#8cc45c" stroke-width="1.4" stroke-linecap="round"/>`,
  },
  flowers: {
    label: "Flowers",
    category: "Plants",
    radius: 8,
    sway: true,
    art:
      `<path d="M0 7C-2 2-5-1-7-5M0 7C0 1 0-3 1-7M0 7C2 2 5-1 7-5" fill="none" stroke="#5d9b3c" stroke-width="1.6" stroke-linecap="round"/>` +
      `<circle cx="-7" cy="-6" r="2.4" fill="#e86fa0"/>` +
      `<circle cx="1" cy="-8" r="2.4" fill="#f2d15c"/>` +
      `<circle cx="7" cy="-6" r="2.4" fill="#9a7ce8"/>` +
      `<circle cx="0" cy="-1" r="2" fill="#f0f0f0" opacity=".9"/>`,
  },
  reeds: {
    label: "Reeds",
    category: "Plants",
    radius: 9,
    sway: true,
    art:
      `<path d="M-5 8C-6 2-7-3-6-9M0 8C0 1 0-5 1-11M5 8C6 2 7-2 6-8M-2 8C-2 3-3-2-4-6M3 8C3 2 4-3 5-7" fill="none" stroke="#7fae4e" stroke-width="1.7" stroke-linecap="round"/>` +
      `<ellipse cx="-6" cy="-10" rx="1.6" ry="3.4" fill="#8a6a34"/>` +
      `<ellipse cx="1" cy="-12" rx="1.6" ry="3.4" fill="#8a6a34"/>` +
      `<ellipse cx="6" cy="-9" rx="1.6" ry="3.4" fill="#8a6a34"/>`,
  },

  // ---- details / vehicle-flavoured ---------------------------------------
  log: {
    label: "Fallen log",
    category: "Details",
    radius: 14,
    art:
      `<rect x="-14" y="-5" width="28" height="10" rx="5" fill="#6b4a2c" stroke="#4c331c" stroke-width="1.6"/>` +
      `<path d="M-8-5V5M0-5V5M8-5V5" stroke="#00000030" stroke-width="1.2"/>` +
      `<ellipse cx="-14" cy="0" rx="2.6" ry="5" fill="#a57c4a" stroke="#4c331c" stroke-width="1.2"/>` +
      `<ellipse cx="-14" cy="0" rx="1.2" ry="2.4" fill="#7a5830"/>`,
  },
  stump: {
    label: "Stump",
    category: "Details",
    radius: 9,
    art:
      `<circle r="8.5" fill="#6b4a2c" stroke="#4c331c" stroke-width="1.6"/>` +
      `<circle r="6" fill="none" stroke="#a57c4a" stroke-width="1.4"/>` +
      `<circle r="3.4" fill="none" stroke="#a57c4a" stroke-width="1.2"/>` +
      `<circle r="1.2" fill="#a57c4a"/>`,
  },
  cone: {
    label: "Traffic cone",
    category: "Details",
    radius: 6,
    art:
      `<rect x="-5.5" y="-5.5" width="11" height="11" rx="1.5" fill="#c9541e" stroke="#8f3a12" stroke-width="1.2"/>` +
      `<circle r="4.4" fill="#e86a25"/>` +
      `<circle r="2.6" fill="#f5f5f5"/>` +
      `<circle r="1.2" fill="#e86a25"/>`,
  },
  tire: {
    label: "Tyre",
    category: "Details",
    radius: 9,
    art:
      `<circle r="8.5" fill="#1d1d1f" stroke="#000" stroke-width="1.4"/>` +
      `<circle r="5" fill="#3a3a3e"/>` +
      `<circle r="2.6" fill="#141416"/>` +
      `<path d="M0-8.5V-5M0 5V8.5M-8.5 0H-5M5 0H8.5" stroke="#0d0d0f" stroke-width="1.6"/>`,
  },
  crate: {
    label: "Crate",
    category: "Details",
    radius: 10,
    art:
      `<rect x="-9" y="-9" width="18" height="18" rx="1.5" fill="#9a7448" stroke="#6e5230" stroke-width="1.6"/>` +
      `<path d="M-9-9 9 9M9-9-9 9" stroke="#6e5230" stroke-width="1.6"/>` +
      `<rect x="-9" y="-9" width="18" height="18" rx="1.5" fill="none" stroke="#b8905c" stroke-width="1"/>`,
  },
  bridge: {
    label: "Plank bridge",
    category: "Details",
    radius: 26,
    art:
      `<rect x="-26" y="-11" width="52" height="22" rx="2" fill="#7a5836" stroke="#513a20" stroke-width="2"/>` +
      Array.from({ length: 7 }, (_, i) => {
        const x = -24 + i * 7;
        return `<rect x="${x}" y="-9" width="5.4" height="18" rx="1" fill="#96703f" stroke="#513a20" stroke-width="1"/>`;
      }).join("") +
      `<rect x="-26" y="-11" width="52" height="4" fill="#513a20" opacity=".55"/>` +
      `<rect x="-26" y="7" width="52" height="4" fill="#513a20" opacity=".55"/>`,
  },
};

export const PropKinds = Object.keys(Props);

export function propArt(kind) {
  return Props[kind] || Props["rock-s"];
}

// Ground shadow ellipse for a prop, laid flat toward the sun angle.
export function propShadow(kind, scale, sunAngle, shadowLength) {
  const { radius } = propArt(kind);
  const r = radius * scale;
  const a = (sunAngle * Math.PI) / 180;
  const len = r * (0.5 + shadowLength * 1.1);
  const cx = Math.cos(a) * len;
  const cy = Math.sin(a) * len;
  const rx = r * 0.95 + Math.abs(Math.cos(a)) * len * 0.35;
  const ry = r * 0.8 + Math.abs(Math.sin(a)) * len * 0.35;
  return { cx, cy, rx, ry, rotate: sunAngle };
}
