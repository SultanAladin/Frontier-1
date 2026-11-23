// A deterministic digital dab engine shared by UV painting, mask painting and
// the brush studio. These are digital tips, not simulations of physical media.
export const DIGITAL_BRUSH_PRESETS = [
  {
    id: "round",
    name: "Studio round",
    note: "A clean, pressure-sensitive round tip",
    shape: "round",
    hardness: 0.92,
  },
  {
    id: "soft",
    name: "Soft round",
    note: "A smooth digital falloff",
    shape: "round",
    hardness: 0.1,
    flow: 0.3,
    spacing: 0.12,
  },
  {
    id: "flat",
    name: "Flat tip",
    note: "An elliptical tip following the stroke",
    shape: "round",
    roundness: 0.3,
    follow: true,
  },
  {
    id: "pixel",
    name: "Pixel pen",
    note: "A hard square digital tip",
    shape: "square",
    hardness: 1,
    size: 12,
    sizePressure: 0,
    spacing: 0.08,
  },
  {
    id: "grain",
    name: "Grain round",
    note: "Procedural digital texture, not physical paint",
    grain: "noise",
    grainAmount: 0.5,
    grainScale: 4,
    hardness: 0.8,
  },
];
export const DIGITAL_BRUSH_DEFAULTS = Object.freeze({
  preset: "round",
  tool: "paint",
  color: "#d78c59",
  size: 24,
  opacity: 1,
  flow: 1,
  hardness: 0.92,
  spacing: 0.16,
  stabilization: 0,
  taperStart: 0,
  taperEnd: 0,
  sizePressure: 0.65,
  opacityPressure: 0,
  sizeJitter: 0,
  opacityJitter: 0,
  scatter: 0,
  shape: "round",
  roundness: 1,
  rotation: 0,
  rotationJitter: 0,
  follow: false,
  grain: "none",
  grainAmount: 0,
  grainScale: 4,
  strokeHue: 0,
  strokeSaturation: 0,
  strokeBrightness: 0,
  dabHue: 0,
  dabSaturation: 0,
  dabBrightness: 0,
  pressureBrightness: 0,
  heightStrength: 0.4,
  fillScope: "object",
});
export function normalizeDigitalBrush(raw = {}) {
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) raw = {};
  const result = { ...DIGITAL_BRUSH_DEFAULTS };
  for (const [key, fallback] of Object.entries(result)) {
    if (typeof fallback !== "number") continue;
    const n = Number(raw[key]);
    const max =
      key === "size"
        ? 128
        : key === "rotation"
          ? 360
          : key === "grainScale"
            ? 32
            : key === "heightStrength"
              ? 2
              : key === "spacing"
                ? 2
                : 1;
    const min = key === "grainScale" ? 1 : key === "spacing" ? 0.05 : 0;
    result[key] = Number.isFinite(n)
      ? Math.max(min, Math.min(max, n))
      : fallback;
  }
  result.roundness = Math.max(0.05, result.roundness);
  result.preset = DIGITAL_BRUSH_PRESETS.some((p) => p.id === raw.preset)
    ? raw.preset
    : "round";
  result.tool = ["paint", "erase", "fill"].includes(raw.tool)
    ? raw.tool
    : "paint";
  result.color = /^#[\da-f]{6}$/i.test(raw.color || "")
    ? raw.color.toLowerCase()
    : result.color;
  result.shape = raw.shape === "square" ? "square" : "round";
  result.grain = ["none", "noise", "lines", "checker"].includes(raw.grain)
    ? raw.grain
    : "none";
  result.follow = raw.follow === true;
  result.fillScope = ["face", "island", "object", "tile"].includes(
    raw.fillScope,
  )
    ? raw.fillScope
    : "object";
  return result;
}
export function selectDigitalBrush(current, id) {
  const preset =
    DIGITAL_BRUSH_PRESETS.find((p) => p.id === id) || DIGITAL_BRUSH_PRESETS[0];
  const { id: presetId, name, note, ...properties } = preset;
  return normalizeDigitalBrush({
    ...DIGITAL_BRUSH_DEFAULTS,
    ...properties,
    preset: presetId,
    color: current.color,
    tool: current.tool,
  });
}
export function seededRandom(seed) {
  let state = seed >>> 0;
  return () => {
    state = (state + 0x6d2b79f5) >>> 0;
    let x = Math.imul(state ^ (state >>> 15), state | 1);
    x ^= x + Math.imul(x ^ (x >>> 7), x | 61);
    return ((x ^ (x >>> 14)) >>> 0) / 4294967296;
  };
}
const clamp = (x, lo = 0, hi = 1) => Math.max(lo, Math.min(hi, x));
function hsv(hex) {
  const rgb = [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16) / 255);
  const max = Math.max(...rgb),
    min = Math.min(...rgb),
    d = max - min;
  let h =
    d === 0
      ? 0
      : max === rgb[0]
        ? ((rgb[1] - rgb[2]) / d) % 6
        : max === rgb[1]
          ? (rgb[2] - rgb[0]) / d + 2
          : (rgb[0] - rgb[1]) / d + 4;
  return [(h / 6 + 1) % 1, max ? d / max : 0, max];
}
function hexHSV(h, s, v) {
  h = ((h % 1) + 1) % 1;
  s = clamp(s);
  v = clamp(v);
  const i = Math.floor(h * 6),
    f = h * 6 - i,
    p = v * (1 - s),
    q = v * (1 - f * s),
    t = v * (1 - (1 - f) * s);
  const rgb = [
    [v, t, p],
    [q, v, p],
    [p, v, t],
    [p, q, v],
    [t, p, v],
    [v, p, q],
  ][i % 6];
  return (
    "#" +
    rgb
      .map((c) =>
        Math.round(c * 255)
          .toString(16)
          .padStart(2, "0"),
      )
      .join("")
  );
}
export const DIGITAL_DAB_LIMIT = 16384;
// 📝 Seeded draws consumed per dab, in loop order: size jitter (1), scatter
//    x/y (2), opacity jitter (1), rotation jitter (1), dab hue/sat/value (3).
//    Every draw is unconditional, so dab i always starts at draw 3 + i * 8
//    (after the 3 stroke-level draws). Resuming a stream only has to advance
//    the generator; it must never re-derive colours for skipped dabs.
const DRAWS_PER_DAB = 8;
// points are [x,y,pressure]. unit converts coordinates to brush pixels.
// Spacing is measured along arc length, not event count or animation frames.
export function digitalBrushDabCount(points, settings, unit = 256) {
  const b = normalizeDigitalBrush(settings);
  if (!points?.length || b.size <= 0 || b.opacity <= 0 || b.flow <= 0)
    return 0;
  let total = 0;
  for (let i = 1; i < points.length; i++)
    total +=
      Math.hypot(points[i][0] - points[i - 1][0], points[i][1] - points[i - 1][1]) *
      unit;
  const count = Math.max(
    1,
    Math.ceil(total / Math.max(0.4, b.size * b.spacing)) + 1,
  );
  if (count > DIGITAL_DAB_LIMIT)
    throw new Error(
      "This stroke exceeds the digital dab budget. Increase brush size or spacing.",
    );
  return count;
}
function digitalBrushDabRange(
  points,
  settings,
  seed = 1,
  unit = 256,
  color = settings.color,
  colorDynamics = true,
  fromDab = 0,
) {
  const b = normalizeDigitalBrush(settings);
  if (!points?.length || b.size <= 0 || b.opacity <= 0 || b.flow <= 0)
    return [];
  const path = points.map((p) => [p[0] * unit, p[1] * unit, clamp(p[2] ?? 1)]);
  const lengths = [0];
  for (let i = 1; i < path.length; i++)
    lengths.push(
      lengths[i - 1] +
        Math.hypot(path[i][0] - path[i - 1][0], path[i][1] - path[i - 1][1]),
    );
  const total = lengths.at(-1),
    step = Math.max(0.4, b.size * b.spacing),
    count = Math.max(1, Math.ceil(total / step) + 1);
  if (count > DIGITAL_DAB_LIMIT)
    throw new Error(
      "This stroke exceeds the digital dab budget. Increase brush size or spacing.",
    );
  const first = Math.max(0, Math.min(count, Math.floor(fromDab) || 0));
  const random = seededRandom(seed),
    signed = () => random() * 2 - 1;
  const original = hsv(color),
    stroke = [
      original[0] + signed() * b.strokeHue,
      original[1] + signed() * b.strokeSaturation,
      original[2] + signed() * b.strokeBrightness,
    ];
  // ⚠️ Fast-forward must consume exactly the draws the skipped dabs would
  //    have consumed, or every resumed dab changes colour, size and scatter.
  for (let skipped = first * DRAWS_PER_DAB; skipped > 0; skipped--) random();
  const dabs = [];
  let segment = 1;
  for (let i = first; i < count; i++) {
    const distance = Math.min(total, i * step),
      progress = total ? distance / total : 0.5;
    while (segment < path.length - 1 && lengths[segment] < distance) segment++;
    const a = path[Math.max(0, segment - 1)] || path[0],
      c = path[segment] || a;
    const delta = lengths[segment] - (lengths[segment - 1] || 0),
      f = delta > 0 ? clamp((distance - lengths[segment - 1]) / delta) : 0;
    const pressure = a[2] + (c[2] - a[2]) * f;
    const taper = Math.min(
      b.taperStart ? clamp(progress / b.taperStart, 0.04) : 1,
      b.taperEnd ? clamp((1 - progress) / b.taperEnd, 0.04) : 1,
    );
    const radius =
      b.size *
      0.5 *
      (1 - b.sizePressure + b.sizePressure * Math.max(0.04, pressure)) *
      (1 - random() * b.sizeJitter) *
      taper;
    const x = a[0] + (c[0] - a[0]) * f + signed() * b.scatter * b.size;
    const y = a[1] + (c[1] - a[1]) * f + signed() * b.scatter * b.size;
    const opacity =
      b.flow *
      (1 - random() * b.opacityJitter) *
      (1 - b.opacityPressure + b.opacityPressure * pressure);
    const angle =
      (b.rotation * Math.PI) / 180 +
      signed() * b.rotationJitter * Math.PI +
      (b.follow ? Math.atan2(c[1] - a[1], c[0] - a[0]) : 0);
    const h = stroke[0] + signed() * b.dabHue,
      s = stroke[1] + signed() * b.dabSaturation,
      v =
        stroke[2] +
        signed() * b.dabBrightness +
        (pressure - 1) * b.pressureBrightness;
    dabs.push({
      x,
      y,
      radius,
      opacity,
      angle,
      color: colorDynamics ? hexHSV(h, s, v) : color,
      pressure,
    });
  }
  return dabs;
}
export function digitalBrushDabs(
  points,
  settings,
  seed = 1,
  unit = 256,
  color = settings.color,
  colorDynamics = true,
) {
  return digitalBrushDabRange(points, settings, seed, unit, color, colorDynamics, 0);
}
// Dabs [fromDab, end) of the same deterministic stream digitalBrushDabs
// returns. Appending samples to a stroke only appends dabs (spacing is arc
// length from the stroke origin), so a live stroke can deposit just the new
// tail instead of replaying every dab on every animation frame.
export function digitalBrushDabsFrom(
  points,
  settings,
  seed = 1,
  unit = 256,
  color = settings.color,
  colorDynamics = true,
  fromDab = 0,
) {
  return digitalBrushDabRange(
    points,
    settings,
    seed,
    unit,
    color,
    colorDynamics,
    fromDab,
  );
}
// The final dab only — what the brush cursor displays. Generating the whole
// stream (and an HSV hex string per dab) just to keep .at(-1) made cursor
// tracking cost grow with stroke length.
export function digitalBrushLastDab(
  points,
  settings,
  seed = 1,
  unit = 256,
  color = settings.color,
  colorDynamics = true,
) {
  const count = digitalBrushDabCount(points, settings, unit);
  if (!count) return null;
  return (
    digitalBrushDabRange(
      points,
      settings,
      seed,
      unit,
      color,
      colorDynamics,
      count - 1,
    )[0] ?? null
  );
}
// Keep the live endpoint at the pointer. Filtering the endpoint itself makes
// ink lag behind the cursor (especially after short, fast movements). Smooth
// only the now-interior sample, and do not discard sub-pixel endpoint moves.
export function appendDigitalBrushSample(
  points,
  sample,
  settings,
  accepts = () => true,
) {
  const previous = points.at(-1);
  if (previous && previous.every((n, i) => Math.abs(n - sample[i]) < 1e-9))
    return false;
  const next = sample.slice();
  if (
    previous &&
    points.length > 1 &&
    Math.hypot(next[0] - previous[0], next[1] - previous[1]) < 0.3 / 256
  ) {
    points[points.length - 1] = next;
    return true;
  }
  points.push(next);
  if (points.length > 2 && settings.stabilization > 0) {
    const a = points.at(-3),
      middle = points.at(-2),
      strength = settings.stabilization * 0.5;
    const smooth = [
      middle[0] + ((a[0] + next[0]) * 0.5 - middle[0]) * strength,
      middle[1] + ((a[1] + next[1]) * 0.5 - middle[1]) * strength,
      middle[2],
    ];
    // A concave chart/selected face must not gain an out-of-domain sample.
    if (accepts(smooth)) points[points.length - 2] = smooth;
  }
  return true;
}

const tipCache = new Map();
export function digitalBrushTip(settings, color) {
  const b = normalizeDigitalBrush(settings),
    key = JSON.stringify([
      b.shape,
      b.hardness,
      b.grain,
      b.grainAmount,
      b.grainScale,
      color,
    ]);
  if (tipCache.has(key)) return tipCache.get(key);
  const canvas = document.createElement("canvas");
  canvas.width = canvas.height = 64;
  const ctx = canvas.getContext("2d"),
    image = ctx.createImageData(64, 64),
    rgb = [1, 3, 5].map((i) => parseInt(color.slice(i, i + 2), 16));
  for (let y = 0; y < 64; y++)
    for (let x = 0; x < 64; x++) {
      const dx = (x + 0.5 - 32) / 31,
        dy = (y + 0.5 - 32) / 31;
      const r =
        b.shape === "square"
          ? Math.max(Math.abs(dx), Math.abs(dy))
          : Math.hypot(dx, dy);
      let a =
        r > 1
          ? 0
          : b.hardness >= 1
            ? 1
            : 1 - clamp((r - b.hardness) / (1 - b.hardness));
      a = a * a * (3 - 2 * a);
      let grain = 1;
      const gx = Math.floor(x / b.grainScale),
        gy = Math.floor(y / b.grainScale);
      if (b.grain === "noise")
        grain = seededRandom(
          Math.imul(gx + 17, 92837111) ^ Math.imul(gy + 5, 689287499),
        )();
      if (b.grain === "lines") grain = gx % 2 ? 0.25 : 1;
      if (b.grain === "checker") grain = (gx + gy) % 2 ? 0.2 : 1;
      a *= 1 - b.grainAmount + b.grainAmount * grain;
      const k = (y * 64 + x) * 4;
      image.data.set([...rgb, Math.round(a * 255)], k);
    }
  ctx.putImageData(image, 0, 0);
  tipCache.set(key, canvas);
  if (tipCache.size > 128) tipCache.delete(tipCache.keys().next().value);
  return canvas;
}
export function drawDigitalDabs(ctx, dabs, settings) {
  for (const d of dabs) {
    if (d.radius <= 0 || d.opacity <= 0) continue;
    ctx.save();
    ctx.translate(d.x, d.y);
    ctx.rotate(d.angle);
    ctx.scale(1, settings.roundness);
    ctx.globalAlpha = d.opacity;
    ctx.imageSmoothingEnabled = settings.preset !== "pixel";
    ctx.drawImage(
      digitalBrushTip(settings, d.color),
      -d.radius,
      -d.radius,
      d.radius * 2,
      d.radius * 2,
    );
    ctx.restore();
  }
}
export function digitalPreviewPoints() {
  return Array.from({ length: 121 }, (_, i) => {
    const t = i / 120;
    return [
      0.13 + t * 3.5,
      0.34 + 0.2 * Math.sin(t * Math.PI * 2.5),
      0.18 + 0.82 * Math.sin(t * Math.PI),
    ];
  });
}
