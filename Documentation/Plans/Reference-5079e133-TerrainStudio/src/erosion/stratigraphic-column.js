/**
 * Authored, non-repeating stratigraphic columns.
 *
 * Beds are stored bottom-to-top (oldest first), as in a measured geological
 * section. The GPU receives cumulative bed tops in metres plus physical and
 * chemical properties for at most 16 beds; coordinates outside the authored
 * interval clamp to the bottom/top bed instead of modulo-repeating the
 * sequence. The same bed therefore controls differential hardness, flat bed
 * colour, geometric benching, mineral-source chemistry and frost response.
 */

export const MAX_STRATA_BEDS = 16;

let strataBedUid = 1;

const clamp = (value, min, max, fallback) => {
  const n = Number(value);
  return Number.isFinite(n) ? Math.max(min, Math.min(max, n)) : fallback;
};
const clamp01 = (value, fallback = 0) => clamp(value, 0, 1, fallback);

export function hexToRgb(hex, fallback = '#8a6a52') {
  const raw = typeof hex === 'string' ? hex.trim() : '';
  const match = raw.match(/^#?([0-9a-f]{6})$/i) || fallback.match(/^#?([0-9a-f]{6})$/i);
  const value = parseInt(match?.[1] || '8a6a52', 16);
  return [((value >> 16) & 255) / 255, ((value >> 8) & 255) / 255, (value & 255) / 255];
}

export function rgbToHex(rgb = [0.54, 0.42, 0.32]) {
  const part = (v) => Math.round(clamp01(v, 0) * 255).toString(16).padStart(2, '0');
  return `#${part(rgb[0])}${part(rgb[1])}${part(rgb[2])}`;
}

export function normalizeHexColor(hex, fallback = '#8a6a52') {
  return rgbToHex(hexToRgb(hex, fallback));
}

const srgbToLinear = (c) => Math.pow(clamp01(c, 0), 2.2);

export const STRATA_LITHOLOGIES = Object.freeze({
  shale: Object.freeze({
    id: 'shale', label: 'Shale', hardness: 0.27, porosity: 0.16, fracture: 0.78, clay: 0.84,
    iron: 0.42, copper: 0.05, carbonate: 0.16, salt: 0.08, color: '#7a4a3b',
  }),
  mudstone: Object.freeze({
    id: 'mudstone', label: 'Mudstone', hardness: 0.34, porosity: 0.19, fracture: 0.62, clay: 0.72,
    iron: 0.48, copper: 0.07, carbonate: 0.22, salt: 0.10, color: '#9a6650',
  }),
  siltstone: Object.freeze({
    id: 'siltstone', label: 'Siltstone', hardness: 0.44, porosity: 0.15, fracture: 0.52, clay: 0.45,
    iron: 0.38, copper: 0.05, carbonate: 0.26, salt: 0.08, color: '#c2a181',
  }),
  sandstone: Object.freeze({
    id: 'sandstone', label: 'Sandstone', hardness: 0.58, porosity: 0.21, fracture: 0.44, clay: 0.16,
    iron: 0.52, copper: 0.06, carbonate: 0.18, salt: 0.05, color: '#c98d5f',
  }),
  arkose: Object.freeze({
    id: 'arkose', label: 'Arkose / feldspathic sandstone', hardness: 0.55, porosity: 0.18, fracture: 0.48, clay: 0.28,
    iron: 0.36, copper: 0.04, carbonate: 0.12, salt: 0.04, color: '#cf9a7d',
  }),
  limestone: Object.freeze({
    id: 'limestone', label: 'Limestone', hardness: 0.66, porosity: 0.10, fracture: 0.58, clay: 0.08,
    iron: 0.10, copper: 0.03, carbonate: 0.92, salt: 0.04, color: '#cbbfa6',
  }),
  dolomite: Object.freeze({
    id: 'dolomite', label: 'Dolomite', hardness: 0.74, porosity: 0.08, fracture: 0.50, clay: 0.05,
    iron: 0.14, copper: 0.03, carbonate: 0.84, salt: 0.06, color: '#b8a88e',
  }),
  marl: Object.freeze({
    id: 'marl', label: 'Marl / calcareous mudstone', hardness: 0.38, porosity: 0.18, fracture: 0.64, clay: 0.48,
    iron: 0.18, copper: 0.08, carbonate: 0.62, salt: 0.10, color: '#a8ad8b',
  }),
  conglomerate: Object.freeze({
    id: 'conglomerate', label: 'Conglomerate', hardness: 0.62, porosity: 0.13, fracture: 0.40, clay: 0.12,
    iron: 0.30, copper: 0.05, carbonate: 0.16, salt: 0.04, color: '#97765e',
  }),
  evaporite: Object.freeze({
    id: 'evaporite', label: 'Gypsum / evaporite', hardness: 0.22, porosity: 0.09, fracture: 0.66, clay: 0.10,
    iron: 0.04, copper: 0.02, carbonate: 0.28, salt: 0.94, color: '#e3dcc9',
  }),
  basalt: Object.freeze({
    id: 'basalt', label: 'Basalt / volcanic basement', hardness: 0.82, porosity: 0.045, fracture: 0.56, clay: 0.03,
    iron: 0.58, copper: 0.16, carbonate: 0.04, salt: 0.01, color: '#4b4d52',
  }),
  gneiss: Object.freeze({
    id: 'gneiss', label: 'Gneiss / crystalline basement', hardness: 0.88, porosity: 0.025, fracture: 0.38, clay: 0.02,
    iron: 0.28, copper: 0.08, carbonate: 0.03, salt: 0.01, color: '#77716d',
  }),
});

function nextStrataBedId() {
  return `bed-${strataBedUid++}`;
}

export function createStrataBed(lithology = 'sandstone', overrides = {}) {
  const base = STRATA_LITHOLOGIES[lithology] || STRATA_LITHOLOGIES.sandstone;
  const requestedId = typeof overrides.id === 'string'
    ? overrides.id.trim().replace(/[^a-zA-Z0-9_-]/g, '-').slice(0, 80)
    : '';
  const bed = {
    id: requestedId || nextStrataBedId(),
    name: typeof overrides.name === 'string' && overrides.name.trim() ? overrides.name.slice(0, 80) : base.label,
    lithology: base.id,
    thicknessM: clamp(overrides.thicknessM, 0.4, 90, 12),
    hardness: clamp(overrides.hardness, 0.04, 0.98, base.hardness),
    porosity: clamp(overrides.porosity, 0.003, 0.55, base.porosity),
    fracture: clamp01(overrides.fracture, base.fracture),
    clay: clamp01(overrides.clay, base.clay),
    iron: clamp01(overrides.iron, base.iron),
    copper: clamp01(overrides.copper, base.copper),
    carbonate: clamp01(overrides.carbonate, base.carbonate),
    salt: clamp01(overrides.salt, base.salt),
    color: normalizeHexColor(overrides.color, base.color),
  };
  return bed;
}

export function applyLithologyDefaults(bedInput, lithology) {
  const bed = normalizeStrataBed(bedInput);
  const base = STRATA_LITHOLOGIES[lithology] || STRATA_LITHOLOGIES.sandstone;
  return {
    ...bed,
    name: bed.name || base.label,
    lithology: base.id,
    hardness: base.hardness,
    porosity: base.porosity,
    fracture: base.fracture,
    clay: base.clay,
    iron: base.iron,
    copper: base.copper,
    carbonate: base.carbonate,
    salt: base.salt,
    color: base.color,
  };
}

export function normalizeStrataBed(bedInput, index = 0) {
  if (!bedInput || typeof bedInput !== 'object') {
    return createStrataBed(index % 2 ? 'shale' : 'sandstone');
  }
  return createStrataBed(bedInput.lithology || 'sandstone', bedInput);
}

const bed = (lithology, name, thicknessM, color, overrides = {}) => ({
  lithology,
  name,
  thicknessM,
  color,
  ...overrides,
});

export const STRATA_COLUMN_PRESETS = Object.freeze({
  'rainbow-andes': Object.freeze({
    id: 'rainbow-andes',
    label: 'Rainbow Andes painted beds',
    description: 'Fourteen one-way beds: dark basement, green marl, peach siltstone, red shale, yellow and rust sandstones, cream dolomite and a hard pale cap. No sequence repeat.',
    beds: () => [
      bed('basalt', 'Dark volcanic basement', 26, '#49454d', { iron: 0.62, copper: 0.22 }),
      bed('marl', 'Green chloritic marl', 14, '#71805f', { copper: 0.18, carbonate: 0.58 }),
      bed('siltstone', 'Peach siltstone', 9, '#d9a27e', { iron: 0.34 }),
      bed('shale', 'Brick-red shale', 18, '#94513e', { iron: 0.68 }),
      bed('sandstone', 'Ochre-yellow sandstone', 12, '#c9a45c', { iron: 0.42 }),
      bed('mudstone', 'Mauve mudstone', 7, '#97717a', { iron: 0.36, copper: 0.10 }),
      bed('limestone', 'Pale limestone', 16, '#cdbfa4', { carbonate: 0.94 }),
      bed('sandstone', 'Rust-red sandstone', 11, '#a85f3d', { iron: 0.74 }),
      bed('marl', 'Sage-green marl', 8, '#929d79', { copper: 0.24, carbonate: 0.64 }),
      bed('siltstone', 'Golden siltstone', 15, '#c19a68', { iron: 0.46 }),
      bed('shale', 'Plum-brown shale', 10, '#7d5148', { iron: 0.52 }),
      bed('dolomite', 'Cream dolomite', 19, '#c2b295', { carbonate: 0.88 }),
      bed('arkose', 'Rose arkose', 13, '#c4876d', { iron: 0.44 }),
      bed('sandstone', 'Hard buff caprock', 22, '#d0b48d', { hardness: 0.78, porosity: 0.13, fracture: 0.34 }),
    ],
  }),
  'grand-canyon': Object.freeze({
    id: 'grand-canyon',
    label: 'Grand Canyon measured section',
    description: 'Crystalline basement through Tapeats/Bright Angel/Muav/Redwall/Supai/Coconino to a Kaibab-style cap, with real hard-soft alternation and unequal thicknesses.',
    beds: () => [
      bed('gneiss', 'Vishnu-style crystalline basement', 30, '#625d63', { iron: 0.30, copper: 0.12 }),
      bed('sandstone', 'Tapeats-style brown sandstone', 16, '#8d6248', { hardness: 0.68, iron: 0.48 }),
      bed('shale', 'Bright Angel green shale', 22, '#77806a', { copper: 0.14, iron: 0.24 }),
      bed('limestone', 'Muav grey limestone', 14, '#aaa294', { carbonate: 0.90 }),
      bed('limestone', 'Redwall cliff limestone', 24, '#b49a82', { hardness: 0.76, iron: 0.26 }),
      bed('shale', 'Lower Supai red shale', 12, '#96513e', { iron: 0.66 }),
      bed('sandstone', 'Supai ledge sandstone', 18, '#b4744e', { iron: 0.62 }),
      bed('mudstone', 'Hermit slope mudstone', 10, '#8c5a4b', { iron: 0.58 }),
      bed('sandstone', 'Coconino pale sandstone', 20, '#d5bd9d', { iron: 0.22, porosity: 0.24 }),
      bed('siltstone', 'Toroweap slope siltstone', 15, '#bd9b78', { iron: 0.34, carbonate: 0.38 }),
      bed('limestone', 'Kaibab cap limestone', 26, '#c9bda6', { hardness: 0.72, carbonate: 0.94 }),
    ],
  }),
  'alpine-limestone-shale': Object.freeze({
    id: 'alpine-limestone-shale',
    label: 'Alpine limestone, shale & dolomite',
    description: 'Cold-climate carbonate/shale alternation with thin weak marl and evaporite seams between thick frost-resistant ledges.',
    beds: () => [
      bed('shale', 'Dark basal shale', 24, '#5c554f', { iron: 0.34, clay: 0.88 }),
      bed('limestone', 'Thick grey limestone', 30, '#aaa696', { carbonate: 0.94, fracture: 0.62 }),
      bed('marl', 'Thin olive marl', 8, '#8d9072', { carbonate: 0.62, copper: 0.10 }),
      bed('dolomite', 'Buff dolomite ledge', 18, '#b8a586', { carbonate: 0.88 }),
      bed('shale', 'Black fissile shale', 12, '#4e4b4c', { fracture: 0.88, iron: 0.30 }),
      bed('sandstone', 'Green-grey sandstone', 22, '#8b9076', { copper: 0.16, carbonate: 0.28 }),
      bed('mudstone', 'Rusty mudstone', 9, '#96634e', { iron: 0.62 }),
      bed('limestone', 'Pale cliff limestone', 28, '#c4bba7', { carbonate: 0.95, hardness: 0.72 }),
      bed('evaporite', 'White gypsum seam', 7, '#e0dac9', { salt: 0.96 }),
      bed('siltstone', 'Yellow-brown siltstone', 16, '#bd9e72', { iron: 0.40 }),
      bed('dolomite', 'Hard dolomite cap', 20, '#b3a48e', { hardness: 0.80, carbonate: 0.90 }),
    ],
  }),
  'painted-desert': Object.freeze({
    id: 'painted-desert',
    label: 'Painted Desert bentonite & sandstone',
    description: 'Soft variegated mudstones in lavender, blue-grey, yellow and red separated by harder sandstone lenses and a pale cap.',
    beds: () => [
      bed('mudstone', 'Blue-grey bentonitic mudstone', 18, '#7f8894', { clay: 0.88, iron: 0.18 }),
      bed('shale', 'Lavender shale', 9, '#978394', { iron: 0.28 }),
      bed('siltstone', 'Cream siltstone', 14, '#cfbd9e', { iron: 0.22 }),
      bed('mudstone', 'Rose mudstone', 7, '#bd8272', { iron: 0.50 }),
      bed('sandstone', 'Orange sandstone lens', 20, '#c17d4e', { iron: 0.66 }),
      bed('shale', 'Grey-green shale', 11, '#7f8c78', { copper: 0.16 }),
      bed('mudstone', 'Yellow mudstone', 8, '#c5a967', { iron: 0.46 }),
      bed('sandstone', 'Red-brown channel sandstone', 16, '#9d5b40', { iron: 0.70 }),
      bed('evaporite', 'Pale gypsum stringer', 6, '#ded7c6', { salt: 0.88 }),
      bed('siltstone', 'Peach siltstone', 13, '#d3a37f', { iron: 0.38 }),
      bed('mudstone', 'Mauve bentonite', 10, '#9a7984', { clay: 0.86 }),
      bed('arkose', 'Pink arkose', 18, '#c58c72', { iron: 0.42 }),
      bed('sandstone', 'Pale hard caprock', 15, '#d2b992', { hardness: 0.74 }),
    ],
  }),
});

export function createStrataColumnPreset(presetId = 'rainbow-andes') {
  const preset = STRATA_COLUMN_PRESETS[presetId] || STRATA_COLUMN_PRESETS['rainbow-andes'];
  return preset.beds().map((entry, index) => normalizeStrataBed(entry, index));
}

export function normalizeStrataColumn(bedsInput, fallbackPreset = 'rainbow-andes') {
  if (!Array.isArray(bedsInput) || bedsInput.length === 0) return createStrataColumnPreset(fallbackPreset);
  const normalised = bedsInput.slice(0, MAX_STRATA_BEDS).map((entry, index) => normalizeStrataBed(entry, index));
  const usedIds = new Set();
  for (const bedEntry of normalised) {
    while (usedIds.has(bedEntry.id)) bedEntry.id = nextStrataBedId();
    usedIds.add(bedEntry.id);
  }
  return normalised;
}

export function strataColumnThickness(bedsInput) {
  const beds = normalizeStrataColumn(bedsInput);
  return beds.reduce((sum, entry) => sum + entry.thicknessM, 0);
}

export function summarizeStrataColumn(bedsInput) {
  const beds = normalizeStrataColumn(bedsInput);
  return `${beds.length} authored beds · ${Math.round(strataColumnThickness(beds))} m · non-repeating`;
}

export function sampleStrataColumn(bedsInput, stratigraphicM, baseM = 0) {
  const beds = normalizeStrataColumn(bedsInput);
  const local = clamp(stratigraphicM, -10000, 10000, 0) - clamp(baseM, -1000, 1000, 0);
  let bottom = 0;
  for (let i = 0; i < beds.length; i++) {
    const top = bottom + beds[i].thicknessM;
    if (local <= top || i === beds.length - 1) {
      return {
        bed: beds[i],
        index: i,
        bottomM: bottom,
        topM: top,
        localM: local,
        fraction: clamp01((local - bottom) / Math.max(0.05, beds[i].thicknessM), local < bottom ? 0 : 1),
        belowColumn: local < 0,
        aboveColumn: local > top && i === beds.length - 1,
      };
    }
    bottom = top;
  }
  const last = beds[beds.length - 1];
  return { bed: last, index: beds.length - 1, bottomM: 0, topM: last.thicknessM, localM: local, fraction: 1, belowColumn: false, aboveColumn: true };
}

export function deriveBedProperties(bedInput) {
  const b = normalizeStrataBed(bedInput);
  const poreFactor = clamp(b.porosity / 0.16, 0.05, 2.6, 1);
  const permeability = Math.pow(poreFactor, 1.35) * (1 - 0.62 * b.clay) * (0.30 + b.fracture);
  const tensileResistance = 0.25 + 2.25 * b.hardness * b.hardness;
  const frostSusceptibility = clamp(
    (permeability * (0.35 + 1.25 * b.fracture)) / Math.max(0.15, tensileResistance),
    0,
    2.5,
    0,
  );
  const solubility = clamp01(0.10 + b.carbonate * 0.72 + b.salt * 0.55 + b.clay * 0.16 - b.hardness * 0.12);
  const pluckingSusceptibility = clamp01(
    0.15 + b.fracture * 0.58 + (1 - clamp01(b.thicknessM / 28)) * 0.28 - b.hardness * 0.18,
  );
  return {
    permeability,
    tensileResistance,
    frostSusceptibility,
    solubility,
    pluckingSusceptibility,
  };
}

/**
 * Packs the column for COMMON_GEOLOGY_GLSL:
 * A=(cumulativeTopM, hardness, porosity, fracture)
 * B=(iron, copper, carbonate, salt)
 * C=(linear R, linear G, linear B, clay)
 * meta=(enabled, bedCount, baseM, colourStrength)
 */
export function packStrataColumnUniforms(bedsInput, options = {}) {
  const beds = normalizeStrataColumn(bedsInput, options.presetId || 'rainbow-andes');
  const a = new Float32Array(MAX_STRATA_BEDS * 4);
  const b = new Float32Array(MAX_STRATA_BEDS * 4);
  const c = new Float32Array(MAX_STRATA_BEDS * 4);
  let cumulative = 0;
  for (let i = 0; i < MAX_STRATA_BEDS; i++) {
    const bedEntry = beds[Math.min(i, beds.length - 1)];
    if (i < beds.length) cumulative += bedEntry.thicknessM;
    const rgb = hexToRgb(bedEntry.color);
    a.set([cumulative, bedEntry.hardness, bedEntry.porosity, bedEntry.fracture], i * 4);
    b.set([bedEntry.iron, bedEntry.copper, bedEntry.carbonate, bedEntry.salt], i * 4);
    c.set([srgbToLinear(rgb[0]), srgbToLinear(rgb[1]), srgbToLinear(rgb[2]), bedEntry.clay], i * 4);
  }
  return {
    beds,
    meta: new Float32Array([
      options.enabled ? 1 : 0,
      beds.length,
      clamp(options.baseM, -400, 400, 0),
      clamp01(options.colorStrength, 0.88),
    ]),
    bedsA: a,
    bedsB: b,
    bedsC: c,
    totalThicknessM: cumulative,
  };
}
