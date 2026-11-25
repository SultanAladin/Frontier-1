/**
 * Process-based snow / frost-weathering model shared by the Snow layer UI,
 * stack pipeline, GPU engine and deterministic tests.
 *
 * The important distinction from the old instant "nivation" term is time:
 * segregation ice and volumetric freezing first increase a sub-resolution
 * crack/porosity state. Only when the stored crack aperture and damage are
 * large enough for the active heightfield cell does the GPU convert that
 * state into visible pits/spalls. Loosened material is then available to
 * frost creep, solifluction, avalanche entrainment, meltwash and talus
 * transport instead of being manufactured as immobile sediment every step.
 */

export const MAX_SNOW_EFFECTS = 12;

let snowEffectUid = 1;

function nextSnowEffectId(type = 'snow-effect') {
  return `${type}-${snowEffectUid++}`;
}

const clamp = (value, min, max, fallback) => {
  const n = Number(value);
  return Number.isFinite(n) ? Math.max(min, Math.min(max, n)) : fallback;
};

const clamp01 = (value, fallback = 0) => clamp(value, 0, 1, fallback);

export const DEFAULT_FROST_PARAMS = Object.freeze({
  frostYearsPerStep: 1.6,
  frostMeanTempC: -4.2,
  frostSeasonalAmplitudeC: 13.0,
  frostCrackDepthM: 0.55,
  frostThermalDiffusivity: 1.0, // x10^-6 m^2/s
  frostMoisture: 0.68,
  frostSaturationPeak: 0.74,
  frostSnowInsulationM: 0.45,
  frostInitialDamage: 0.18,
  frostDamageThreshold: 0.52,
  frostVisibleCrackMm: 12,
  frostPitDepthM: 0.14,
  frostPitScaleCells: 1.7,
  frostDebrisRetention: 0.48,
});

export const SNOW_EFFECT_TYPES = Object.freeze({
  'segregation-ice': Object.freeze({
    id: 'segregation-ice',
    label: 'Segregation-ice frost cracking',
    short: 'Ice lenses',
    color: '#7dd3fc',
    description:
      'Ice lenses grow toward a freezing front while liquid films still migrate from warmer rock. Most effective around -3 to -8 C, with a thermal gradient, available pore water, permeability and weak/fractured rock.',
    defaults: Object.freeze({
      windowLowC: -8,
      windowHighC: -3,
      gradientSensitivity: 1.0,
      depthScale: 1.0,
    }),
  }),
  'volumetric-freeze-thaw': Object.freeze({
    id: 'volumetric-freeze-thaw',
    label: 'Volumetric freeze-thaw expansion',
    short: 'Freeze-thaw',
    color: '#a5b4fc',
    description:
      'Pore water expands on freezing during zero crossings. This is a shorter, shallower cracking pulse than segregation ice and needs comparatively high pore saturation.',
    defaults: Object.freeze({
      cycleSensitivity: 1.0,
      saturationFloor: 0.62,
      diurnalAmplitudeC: 5.0,
    }),
  }),
  'nivation-snowpatch': Object.freeze({
    id: 'nivation-snowpatch',
    label: 'Nivation snowpatch & meltwash',
    short: 'Nivation',
    color: '#f0abfc',
    description:
      'Late-lying snow patches and their margins combine frost weathering with meltwater sheetwash/rill export, snow creep and chemical wetting. Deep uniform snow often insulates instead of maximising cycles.',
    defaults: Object.freeze({
      optimalSnowDepthM: 0.45,
      snowDepthWidthM: 0.55,
      meltwashExport: 0.65,
    }),
  }),
  'frost-creep-solifluction': Object.freeze({
    id: 'frost-creep-solifluction',
    label: 'Frost creep & solifluction export',
    short: 'Frost creep',
    color: '#6ee7b7',
    description:
      'Saturated frost-loosened regolith creeps downslope even below the dry angle of repose. This is the main path that prevents every frost-produced grain from piling up on summits.',
    defaults: Object.freeze({
      creepRate: 0.65,
      minSlope: 0.08,
      coarseMobility: 0.35,
    }),
  }),
  'avalanche-scour': Object.freeze({
    id: 'avalanche-scour',
    label: 'Avalanche scour & entrainment',
    short: 'Avalanche',
    color: '#fda4af',
    description:
      'Steep snow-loaded couloirs strip loose sediment first and scour exposed bedrock second; entrained debris is moved downslope in the paired transport pass.',
    defaults: Object.freeze({
      slopeThreshold: 0.62,
      entrainment: 0.70,
      scourRate: 0.55,
    }),
  }),
  'glacier-plucking': Object.freeze({
    id: 'glacier-plucking',
    label: 'Glacier plucking & abrasion',
    short: 'Glacier',
    color: '#67e8f9',
    description:
      'Frost-damaged, closely jointed or thin beds are preferentially plucked; thick hard beds favour abrasion into rock flour. Debris becomes mobile subglacial till rather than automatically staying at the source cell.',
    defaults: Object.freeze({
      plucking: 0.80,
      abrasion: 0.55,
      jointSensitivity: 1.0,
    }),
  }),
  'snowmelt-runoff': Object.freeze({
    id: 'snowmelt-runoff',
    label: 'Seasonal snowmelt cycle & runoff erosion',
    short: 'Snowmelt',
    color: '#38bdf8',
    description:
      'Finite snowmelt runoff erosion. The locked Environment layer normally supplies the location calendar: cold-season snowfall is retained below the long-term snowline, then warm-season snow/ice melt becomes shallow-water runoff that cuts channels and moves debris. If Location seasons are off, the fallback cold-to-warm cycle below is used. No rain particles or rainfall are switched on.',
    defaults: Object.freeze({
      cyclePeriodSteps: 180,
      cyclePhase: 0.16,
      warmMeltBoost: 1.65,
      runoffErosion: 0.85,
      sedimentCapacity: 0.85,
      infiltration: 0.18,
    }),
  }),
  'legacy-instant-nivation': Object.freeze({
    id: 'legacy-instant-nivation',
    label: 'Legacy instant nivation (old model)',
    short: 'Legacy',
    color: '#fbbf24',
    description:
      'Compatibility effect for the previous model: snowy bedrock is converted immediately in place. It is useful for reproducing old looks, but is precisely why excessive summit sediment could appear.',
    defaults: Object.freeze({
      localRetention: 0.72,
      marginRate: 1.0,
    }),
  }),
});

const SNOW_EFFECT_PARAM_LIMITS = Object.freeze({
  windowLowC: [-16, -4],
  windowHighC: [-7, -0.5],
  gradientSensitivity: [0, 2.5],
  depthScale: [0.25, 2.5],
  cycleSensitivity: [0, 2.5],
  saturationFloor: [0.25, 0.95],
  diurnalAmplitudeC: [0, 14],
  optimalSnowDepthM: [0.03, 2.5],
  snowDepthWidthM: [0.08, 2.5],
  meltwashExport: [0, 1],
  creepRate: [0, 2],
  minSlope: [0, 0.8],
  coarseMobility: [0, 1],
  slopeThreshold: [0.1, 1.6],
  entrainment: [0, 1.5],
  scourRate: [0, 2],
  plucking: [0, 2],
  abrasion: [0, 2],
  jointSensitivity: [0, 2.5],
  localRetention: [0, 1],
  marginRate: [0, 2],
  cyclePeriodSteps: [16, 720],
  cyclePhase: [0, 1],
  warmMeltBoost: [0, 4],
  runoffErosion: [0, 2],
  sedimentCapacity: [0, 2],
  infiltration: [0, 1],
});

export function createSnowEffect(type = 'segregation-ice', overrides = {}) {
  const spec = SNOW_EFFECT_TYPES[type] || SNOW_EFFECT_TYPES['segregation-ice'];
  const requestedId = typeof overrides.id === 'string'
    ? overrides.id.trim().replace(/[^a-zA-Z0-9_-]/g, '-').slice(0, 80)
    : '';
  const effect = {
    id: requestedId || nextSnowEffectId(spec.id),
    type: spec.id,
    name: typeof overrides.name === 'string' && overrides.name.trim() ? overrides.name.slice(0, 90) : spec.label,
    enabled: overrides.enabled !== false,
    intensity: clamp(overrides.intensity, 0, 2, 1),
    ...structuredClone(spec.defaults),
  };
  for (const key of Object.keys(spec.defaults)) {
    if (overrides[key] !== undefined) {
      const [min, max] = SNOW_EFFECT_PARAM_LIMITS[key] || [0, 1];
      effect[key] = clamp(overrides[key], min, max, spec.defaults[key]);
    }
  }
  if (spec.id === 'segregation-ice' && effect.windowHighC <= effect.windowLowC + 0.5) {
    effect.windowHighC = effect.windowLowC + 0.5;
  }
  return effect;
}

export function normalizeSnowEffect(effect, fallbackType = 'segregation-ice') {
  if (!effect || typeof effect !== 'object') return createSnowEffect(fallbackType);
  return createSnowEffect(effect.type || fallbackType, effect);
}

function effectList(entries = []) {
  return entries.map(([type, intensity = 1, overrides = {}]) => createSnowEffect(type, { intensity, ...overrides }));
}

export const SNOW_EFFECT_PRESETS = Object.freeze({
  'research-balanced': Object.freeze({
    id: 'research-balanced',
    label: 'Research balanced periglacial',
    description: 'Slow segregation-ice damage, modest freeze-thaw, snowpatch nivation, frost-creep export, joint-aware glacier plucking and a seasonal cold-accumulation / warm-melt runoff cycle.',
    globals: Object.freeze({ ...DEFAULT_FROST_PARAMS }),
    effects: () => effectList([
      ['segregation-ice', 0.85],
      ['volumetric-freeze-thaw', 0.45],
      ['nivation-snowpatch', 0.65],
      ['frost-creep-solifluction', 0.60],
      ['avalanche-scour', 0.35],
      ['glacier-plucking', 0.65],
      ['snowmelt-runoff', 0.70],
    ]),
  }),
  'segregation-pitting': Object.freeze({
    id: 'segregation-pitting',
    label: 'Segregation-ice pitting showcase',
    description: 'Long residence in the -3 to -8 C cracking window, near-optimal saturation and a low release depth so stored millimetre cracks coalesce into visible pits.',
    globals: Object.freeze({
      ...DEFAULT_FROST_PARAMS,
      frostYearsPerStep: 2.4,
      frostMeanTempC: -4.6,
      frostSeasonalAmplitudeC: 12.5,
      frostCrackDepthM: 0.65,
      frostMoisture: 0.74,
      frostInitialDamage: 0.38,
      frostDamageThreshold: 0.47,
      frostVisibleCrackMm: 10,
      frostPitDepthM: 0.17,
      frostPitScaleCells: 1.45,
      frostDebrisRetention: 0.38,
    }),
    effects: () => effectList([
      ['segregation-ice', 1.25, { gradientSensitivity: 1.25 }],
      ['nivation-snowpatch', 0.80],
      ['frost-creep-solifluction', 0.75, { creepRate: 0.82 }],
      ['volumetric-freeze-thaw', 0.25],
      ['avalanche-scour', 0.30],
      ['glacier-plucking', 0.55],
      ['snowmelt-runoff', 0.50, { cyclePeriodSteps: 150, runoffErosion: 0.65 }],
    ]),
  }),
  'freeze-thaw-shatter': Object.freeze({
    id: 'freeze-thaw-shatter',
    label: 'Wet freeze-thaw shattering',
    description: 'Shallow, frequently wetted rock with strong diurnal zero crossings; useful on porous sandstone, shale and fractured limestone.',
    globals: Object.freeze({
      ...DEFAULT_FROST_PARAMS,
      frostYearsPerStep: 2.0,
      frostMeanTempC: -2.6,
      frostSeasonalAmplitudeC: 15,
      frostCrackDepthM: 0.28,
      frostMoisture: 0.82,
      frostInitialDamage: 0.30,
      frostDamageThreshold: 0.50,
      frostVisibleCrackMm: 9,
      frostPitDepthM: 0.12,
      frostDebrisRetention: 0.42,
    }),
    effects: () => effectList([
      ['volumetric-freeze-thaw', 1.20, { cycleSensitivity: 1.25, diurnalAmplitudeC: 7 }],
      ['segregation-ice', 0.55],
      ['frost-creep-solifluction', 0.85],
      ['avalanche-scour', 0.45],
      ['nivation-snowpatch', 0.45],
      ['snowmelt-runoff', 0.80, { cyclePeriodSteps: 140, runoffErosion: 0.90 }],
    ]),
  }),
  'nivation-cirque': Object.freeze({
    id: 'nivation-cirque',
    label: 'Nivation cirque & snowpatch hollow',
    description: 'Patch-margin weathering plus meltwash and solifluction export, tuned to deepen hollows/headwalls without burying their floors in source-cell debris.',
    globals: Object.freeze({
      ...DEFAULT_FROST_PARAMS,
      frostYearsPerStep: 2.1,
      frostMeanTempC: -3.4,
      frostSeasonalAmplitudeC: 14,
      frostCrackDepthM: 0.50,
      frostMoisture: 0.76,
      frostInitialDamage: 0.34,
      frostDamageThreshold: 0.49,
      frostPitDepthM: 0.15,
      frostPitScaleCells: 2.1,
      frostDebrisRetention: 0.30,
    }),
    effects: () => effectList([
      ['nivation-snowpatch', 1.25, { optimalSnowDepthM: 0.38, meltwashExport: 0.85 }],
      ['segregation-ice', 0.80],
      ['frost-creep-solifluction', 0.95, { creepRate: 0.95 }],
      ['avalanche-scour', 0.65],
      ['volumetric-freeze-thaw', 0.35],
      ['glacier-plucking', 0.45],
      ['snowmelt-runoff', 1.05, { cyclePeriodSteps: 130, warmMeltBoost: 1.9, runoffErosion: 1.05, sedimentCapacity: 1.0 }],
    ]),
  }),
  'glacier-plucking': Object.freeze({
    id: 'glacier-plucking',
    label: 'Frost-weakened glacier plucking',
    description: 'Segregation ice pre-weakens joints, then glacier flow preferentially plucks fractured/thin beds and exports till downslope.',
    globals: Object.freeze({
      ...DEFAULT_FROST_PARAMS,
      frostYearsPerStep: 1.8,
      frostMeanTempC: -5.2,
      frostSeasonalAmplitudeC: 11,
      frostCrackDepthM: 0.8,
      frostMoisture: 0.70,
      frostInitialDamage: 0.26,
      frostDamageThreshold: 0.55,
      frostDebrisRetention: 0.32,
    }),
    effects: () => effectList([
      ['glacier-plucking', 1.20, { plucking: 1.1, abrasion: 0.65, jointSensitivity: 1.3 }],
      ['segregation-ice', 0.70],
      ['frost-creep-solifluction', 0.55],
      ['avalanche-scour', 0.25],
      ['snowmelt-runoff', 0.55, { cyclePeriodSteps: 220, runoffErosion: 0.60 }],
    ]),
  }),
  'spring-snowmelt': Object.freeze({
    id: 'spring-snowmelt',
    label: 'Valley snow & spring melt flood',
    description: 'Cold-phase snowfall is retained on valley floors and in drifts well below the melt-equilibrium line, then a warm/dry phase turns the stored pack into sediment-carrying meltwater channels.',
    globals: Object.freeze({
      ...DEFAULT_FROST_PARAMS,
      frostYearsPerStep: 1.6,
      frostMeanTempC: -2.8,
      frostSeasonalAmplitudeC: 15,
      frostMoisture: 0.72,
      frostInitialDamage: 0.18,
      frostDamageThreshold: 0.55,
      frostDebrisRetention: 0.30,
    }),
    effects: () => effectList([
      ['snowmelt-runoff', 1.25, { cyclePeriodSteps: 120, cyclePhase: 0.10, warmMeltBoost: 2.1, runoffErosion: 1.15, sedimentCapacity: 1.05, infiltration: 0.12 }],
      ['nivation-snowpatch', 0.70, { meltwashExport: 0.80 }],
      ['frost-creep-solifluction', 0.60],
      ['segregation-ice', 0.40],
    ]),
  }),
  'snowpack-only': Object.freeze({
    id: 'snowpack-only',
    label: 'Snowpack & ice flow only',
    description: 'Full-domain snow accumulation, drift, avalanching, finite melt routing and firn/ice creep remain, but snow does not weather/pluck bedrock and meltwater channel cutting is off.',
    globals: Object.freeze({ ...DEFAULT_FROST_PARAMS, frostInitialDamage: 0 }),
    effects: () => [],
  }),
  'legacy-instant': Object.freeze({
    id: 'legacy-instant',
    label: 'Legacy instant snow erosion',
    description: 'Previous immediate in-place conversion, retained only for comparison/reproducing older scenes.',
    globals: Object.freeze({ ...DEFAULT_FROST_PARAMS, frostInitialDamage: 0, frostDebrisRetention: 0.72 }),
    effects: () => effectList([
      ['legacy-instant-nivation', 1.0],
      ['avalanche-scour', 1.0, { entrainment: 0.25 }],
      ['glacier-plucking', 1.0, { plucking: 1.0, abrasion: 1.0, jointSensitivity: 0.25 }],
    ]),
  }),
});

export function createSnowEffectPreset(presetId = 'research-balanced') {
  const preset = SNOW_EFFECT_PRESETS[presetId] || SNOW_EFFECT_PRESETS['research-balanced'];
  return {
    presetId: preset.id,
    effects: preset.effects(),
    globals: structuredClone(preset.globals),
  };
}

export function normalizeSnowEffects(effects, presetId = 'research-balanced') {
  if (!Array.isArray(effects)) return createSnowEffectPreset(presetId).effects;
  const normalised = effects.slice(0, MAX_SNOW_EFFECTS).map((effect) => normalizeSnowEffect(effect));
  const usedIds = new Set();
  for (const effect of normalised) {
    while (usedIds.has(effect.id)) effect.id = nextSnowEffectId(effect.type);
    usedIds.add(effect.id);
  }
  return normalised;
}

export function normalizeFrostParams(params = {}) {
  const p = params && typeof params === 'object' ? params : {};
  return {
    frostYearsPerStep: clamp(p.frostYearsPerStep, 0.02, 20, DEFAULT_FROST_PARAMS.frostYearsPerStep),
    frostMeanTempC: clamp(p.frostMeanTempC, -24, 8, DEFAULT_FROST_PARAMS.frostMeanTempC),
    frostSeasonalAmplitudeC: clamp(p.frostSeasonalAmplitudeC, 0, 32, DEFAULT_FROST_PARAMS.frostSeasonalAmplitudeC),
    frostCrackDepthM: clamp(p.frostCrackDepthM, 0.03, 3, DEFAULT_FROST_PARAMS.frostCrackDepthM),
    frostThermalDiffusivity: clamp(p.frostThermalDiffusivity, 0.08, 3, DEFAULT_FROST_PARAMS.frostThermalDiffusivity),
    frostMoisture: clamp01(p.frostMoisture, DEFAULT_FROST_PARAMS.frostMoisture),
    frostSaturationPeak: clamp(p.frostSaturationPeak, 0.4, 0.95, DEFAULT_FROST_PARAMS.frostSaturationPeak),
    frostSnowInsulationM: clamp(p.frostSnowInsulationM, 0.03, 2.5, DEFAULT_FROST_PARAMS.frostSnowInsulationM),
    frostInitialDamage: clamp01(p.frostInitialDamage, DEFAULT_FROST_PARAMS.frostInitialDamage),
    frostDamageThreshold: clamp(p.frostDamageThreshold, 0.08, 0.95, DEFAULT_FROST_PARAMS.frostDamageThreshold),
    frostVisibleCrackMm: clamp(p.frostVisibleCrackMm, 0.5, 120, DEFAULT_FROST_PARAMS.frostVisibleCrackMm),
    frostPitDepthM: clamp(p.frostPitDepthM, 0.005, 0.85, DEFAULT_FROST_PARAMS.frostPitDepthM),
    frostPitScaleCells: clamp(p.frostPitScaleCells, 0.65, 5, DEFAULT_FROST_PARAMS.frostPitScaleCells),
    frostDebrisRetention: clamp01(p.frostDebrisRetention, DEFAULT_FROST_PARAMS.frostDebrisRetention),
  };
}

function weightedAverage(entries, key, fallback) {
  let weightSum = 0;
  let valueSum = 0;
  for (const entry of entries) {
    const w = Math.max(0, Number(entry.intensity) || 0);
    if (!entry.enabled || w <= 0 || !Number.isFinite(Number(entry[key]))) continue;
    weightSum += w;
    valueSum += w * Number(entry[key]);
  }
  return weightSum > 1e-9 ? valueSum / weightSum : fallback;
}

export function deriveSnowEffectWeights(effectsInput) {
  const effects = normalizeSnowEffects(effectsInput);
  const byType = new Map();
  for (const effect of effects) {
    if (!byType.has(effect.type)) byType.set(effect.type, []);
    byType.get(effect.type).push(effect);
  }
  const weight = (type) => {
    const entries = byType.get(type) || [];
    return clamp(entries.reduce((sum, e) => sum + (e.enabled ? e.intensity : 0), 0), 0, 2.5, 0);
  };
  const segregation = byType.get('segregation-ice') || [];
  const volumetric = byType.get('volumetric-freeze-thaw') || [];
  const nivation = byType.get('nivation-snowpatch') || [];
  const creep = byType.get('frost-creep-solifluction') || [];
  const avalanche = byType.get('avalanche-scour') || [];
  const glacier = byType.get('glacier-plucking') || [];
  const legacy = byType.get('legacy-instant-nivation') || [];
  const snowmelt = byType.get('snowmelt-runoff') || [];

  const weights = {
    segregation: weight('segregation-ice'),
    volumetric: weight('volumetric-freeze-thaw'),
    nivation: weight('nivation-snowpatch'),
    frostCreep: weight('frost-creep-solifluction'),
    avalanche: weight('avalanche-scour'),
    glacierPlucking: weight('glacier-plucking'),
    legacy: weight('legacy-instant-nivation'),
    snowmelt: weight('snowmelt-runoff'),
  };
  const params = {
    windowLowC: weightedAverage(segregation, 'windowLowC', -8),
    windowHighC: weightedAverage(segregation, 'windowHighC', -3),
    gradientSensitivity: weightedAverage(segregation, 'gradientSensitivity', 1),
    depthScale: weightedAverage(segregation, 'depthScale', 1),
    cycleSensitivity: weightedAverage(volumetric, 'cycleSensitivity', 1),
    saturationFloor: weightedAverage(volumetric, 'saturationFloor', 0.62),
    diurnalAmplitudeC: weightedAverage(volumetric, 'diurnalAmplitudeC', 5),
    optimalSnowDepthM: weightedAverage(nivation, 'optimalSnowDepthM', 0.45),
    snowDepthWidthM: weightedAverage(nivation, 'snowDepthWidthM', 0.55),
    meltwashExport: weightedAverage(nivation, 'meltwashExport', 0.65),
    creepRate: weightedAverage(creep, 'creepRate', 0.65),
    creepMinSlope: weightedAverage(creep, 'minSlope', 0.08),
    coarseMobility: weightedAverage(creep, 'coarseMobility', 0.35),
    avalancheSlope: weightedAverage(avalanche, 'slopeThreshold', 0.62),
    avalancheEntrainment: weightedAverage(avalanche, 'entrainment', 0.70),
    avalancheScour: weightedAverage(avalanche, 'scourRate', 0.55),
    glacierPlucking: weightedAverage(glacier, 'plucking', 0.80),
    glacierAbrasion: weightedAverage(glacier, 'abrasion', 0.55),
    jointSensitivity: weightedAverage(glacier, 'jointSensitivity', 1),
    legacyRetention: weightedAverage(legacy, 'localRetention', 0.72),
    legacyMarginRate: weightedAverage(legacy, 'marginRate', 1),
    cyclePeriodSteps: weightedAverage(snowmelt, 'cyclePeriodSteps', 180),
    cyclePhase: weightedAverage(snowmelt, 'cyclePhase', 0.16),
    warmMeltBoost: weightedAverage(snowmelt, 'warmMeltBoost', 1.65),
    runoffErosion: weightedAverage(snowmelt, 'runoffErosion', 0.85),
    sedimentCapacity: weightedAverage(snowmelt, 'sedimentCapacity', 0.85),
    infiltration: weightedAverage(snowmelt, 'infiltration', 0.18),
  };
  if (params.windowHighC <= params.windowLowC + 0.5) params.windowHighC = params.windowLowC + 0.5;
  return {
    effects,
    weights,
    params,
    anyDamage: weights.segregation + weights.volumetric + weights.nivation > 0.001,
    anyTransport: weights.frostCreep + weights.avalanche + weights.glacierPlucking + weights.snowmelt > 0.001,
    anyWeathering:
      weights.segregation + weights.volumetric + weights.nivation + weights.avalanche +
      weights.glacierPlucking + weights.legacy + weights.snowmelt > 0.001,
  };
}

/**
 * Cold-accumulation / warm-dry-melt cycle used by the snow pack.
 *
 * Phase 0 is the coldest point and phase 0.5 the warmest. The returned
 * low-elevation retention is deliberately non-zero even without the effect:
 * the snowline is a long-term melt-equilibrium line, not a physical switch
 * that prevents falling snow from landing in valleys. When the global
 * location-based seasonal climate is enabled it supplies the phase, snowfall,
 * retention and finite melt directly; the snowmelt-runoff effect then only
 * adds its runoff-erosion emphasis. The independent step cycle below remains
 * the fallback when seasonal climate is switched off.
 */
export function snowmeltCycleAtStep(derivedOrEffects, stepCount = 0, seasonalClimate = null) {
  const derived = derivedOrEffects && derivedOrEffects.weights && derivedOrEffects.params
    ? derivedOrEffects
    : deriveSnowEffectWeights(derivedOrEffects);
  const weight = clamp(derived.weights.snowmelt, 0, 2.5, 0);
  const active = weight > 0.001;
  if (seasonalClimate && seasonalClimate.enabled) {
    const season = clamp(seasonalClimate.warmth, -1, 1, 0);
    const warm = smoothstep(-0.12, 0.88, season);
    const cold = 1 - smoothstep(-0.82, 0.35, season);
    const snowfallMultiplier = clamp(seasonalClimate.snowMultiplier, 0, 2.6, 1);
    const runoffBoost = active
      ? 0.72 + 0.28 * clamp(derived.params.warmMeltBoost, 0, 3, 1.65) * clamp(weight, 0, 1.8, 0)
      : 1;
    const meltMultiplier = clamp((Number(seasonalClimate.meltMultiplier) || 1) * runoffBoost, 0.02, 4.6, 1);
    const lowElevationRetention = clamp(seasonalClimate.lowElevationRetention, 0.05, 0.95, 0.34);
    const phase = ((Number(seasonalClimate.phaseFromColdest) || 0) % 1 + 1) % 1;
    return {
      active,
      weight,
      phase,
      season,
      warm,
      cold,
      snowfallMultiplier,
      meltMultiplier,
      lowElevationRetention,
      climateDriven: true,
      vec: [snowfallMultiplier, meltMultiplier, lowElevationRetention, season],
    };
  }
  if (seasonalClimate && seasonalClimate.enabled === false) {
    // Explicit non-seasonal mode (Seasonal Simulation OFF or deterministic stack bake):
    // hold snowfall, melt, and valley snow retention steady rather than running a
    // hidden sinusoidal winter/summer cycle.
    return {
      active,
      weight,
      phase: 0,
      season: 0,
      warm: 0,
      cold: 0,
      snowfallMultiplier: 1,
      meltMultiplier: 1,
      lowElevationRetention: 0.34,
      climateDriven: false,
      vec: [1, 1, 0.34, 0],
    };
  }
  const period = clamp(derived.params.cyclePeriodSteps, 16, 720, 180);
  const phaseRaw = (Number(stepCount) || 0) / period + clamp(derived.params.cyclePhase, 0, 1, 0.16);
  const phase = ((phaseRaw % 1) + 1) % 1;
  const season = active ? -Math.cos(phase * Math.PI * 2) : 0; // -1 cold, +1 warm; neutral without the effect
  const warm = smoothstep(-0.12, 0.88, season);
  const cold = 1 - smoothstep(-0.82, 0.35, season);
  const strength = clamp(weight, 0, 1.5, 0);
  const cycleBlend = clamp(strength, 0, 1, 0);
  const snowfallMultiplier = active
    ? clamp(1.10 + 0.22 * cold * strength - 0.78 * warm * strength, 0.06, 1.38, 1)
    : 1;
  const fullCycleMelt = 0.05 + derived.params.warmMeltBoost * warm * Math.max(strength, 1);
  const meltMultiplier = active
    ? clamp((1 - cycleBlend) + cycleBlend * fullCycleMelt, 0.02, 4.2, 1)
    : 1;
  const lowElevationRetention = active
    ? clamp(0.34 + cycleBlend * ((0.14 + 0.78 * cold) - 0.34), 0.08, 0.95, 0.34)
    : 0.34;
  return {
    active,
    weight,
    phase,
    season,
    warm,
    cold,
    snowfallMultiplier,
    meltMultiplier,
    lowElevationRetention,
    climateDriven: false,
    vec: [snowfallMultiplier, meltMultiplier, lowElevationRetention, season],
  };
}

export function summarizeSnowEffects(effectsInput) {
  const { effects, weights } = deriveSnowEffectWeights(effectsInput);
  const active = effects.filter((e) => e.enabled && e.intensity > 0.001);
  if (!active.length) return 'snowpack only';
  const parts = [];
  if (weights.segregation > 0.001) parts.push(`ice-lens ${Math.round(weights.segregation * 100)}%`);
  if (weights.volumetric > 0.001) parts.push(`freeze-thaw ${Math.round(weights.volumetric * 100)}%`);
  if (weights.nivation > 0.001) parts.push(`nivation ${Math.round(weights.nivation * 100)}%`);
  if (weights.frostCreep > 0.001) parts.push(`creep ${Math.round(weights.frostCreep * 100)}%`);
  if (weights.snowmelt > 0.001) parts.push(`snowmelt runoff ${Math.round(weights.snowmelt * 100)}%`);
  if (weights.legacy > 0.001) parts.push('legacy instant');
  return parts.slice(0, 3).join(' · ') || `${active.length} snow effects`;
}

const smoothstep = (a, b, x) => {
  const t = clamp((x - a) / Math.max(1e-9, b - a), 0, 1, 0);
  return t * t * (3 - 2 * t);
};

/** Smooth -8..-3 C (by default) segregation-ice window. */
export function frostWindowWeight(tempC, lowC = -8, highC = -3) {
  return smoothstep(lowC - 1.4, lowC + 0.7, tempC) * (1 - smoothstep(highC - 0.7, highC + 1.4, tempC));
}

/**
 * Nonlinear saturation response. Segregation ice needs connected liquid
 * films but loses some efficiency in completely flooded, low-gradient pores;
 * volumetric expansion instead keeps rising toward high saturation.
 */
export function saturationEfficacy(saturation, peak = 0.74, mode = 'segregation') {
  const s = clamp01(saturation, 0);
  if (mode === 'volumetric') return smoothstep(0.45, 0.86, s);
  const dryGate = smoothstep(Math.max(0.05, peak - 0.38), Math.max(0.12, peak - 0.07), s);
  const flooded = 1 - 0.38 * smoothstep(Math.min(0.98, peak + 0.10), 0.995, s);
  return dryGate * flooded;
}

export function thermalSkinDepthM(diffusivityE6 = 1, periodDays = 365.25) {
  const kappa = Math.max(0.01, Number(diffusivityE6) || 1) * 1e-6;
  const omega = (2 * Math.PI) / (Math.max(1, periodDays) * 86400);
  return Math.sqrt((2 * kappa) / omega);
}

export function temperatureAtDepth({
  meanC = -4,
  amplitudeC = 12,
  phaseYears = 0,
  depthM = 0.5,
  diffusivityE6 = 1,
} = {}) {
  const skin = thermalSkinDepthM(diffusivityE6, 365.25);
  const z = Math.max(0, Number(depthM) || 0);
  return meanC + amplitudeC * Math.exp(-z / skin) * Math.sin(2 * Math.PI * phaseYears - z / skin);
}

export function visibleCrackThresholdMm(cellSizeM, requestedMm = DEFAULT_FROST_PARAMS.frostVisibleCrackMm) {
  const cell = Math.max(0.05, Number(cellSizeM) || 1);
  return Math.max(clamp(requestedMm, 0.5, 120, DEFAULT_FROST_PARAMS.frostVisibleCrackMm), cell * 8);
}

export function estimateAnnualFrostWindow(paramsInput = {}, snowDepthM = 0.35) {
  const p = normalizeFrostParams(paramsInput);
  const insulation = Math.exp(-Math.max(0, snowDepthM) / Math.max(0.03, p.frostSnowInsulationM));
  const amplitude = p.frostSeasonalAmplitudeC * (0.12 + 0.88 * insulation);
  const mean = p.frostMeanTempC + (1 - insulation) * Math.max(0, -1.5 - p.frostMeanTempC) * 0.45;
  let windowSum = 0;
  let gradientSum = 0;
  const samples = 48;
  for (let i = 0; i < samples; i++) {
    const phase = i / samples;
    const surface = mean + amplitude * Math.sin(2 * Math.PI * phase);
    const depthTemp = temperatureAtDepth({
      meanC: mean,
      amplitudeC: amplitude,
      phaseYears: phase,
      depthM: p.frostCrackDepthM,
      diffusivityE6: p.frostThermalDiffusivity,
    });
    windowSum += frostWindowWeight(depthTemp);
    gradientSum += Math.abs(surface - depthTemp) / Math.max(0.08, p.frostCrackDepthM);
  }
  return {
    meanC: mean,
    amplitudeC: amplitude,
    windowFraction: windowSum / samples,
    meanGradientCPerM: gradientSum / samples,
    skinDepthM: thermalSkinDepthM(p.frostThermalDiffusivity),
    saturationEfficacy: saturationEfficacy(p.frostMoisture, p.frostSaturationPeak, 'segregation'),
  };
}

/**
 * CPU reference implementation of the GPU state transition, deliberately
 * kept close to FROST_DAMAGE_FRAG. It gives the Inspector a live diagnostic
 * and lets tests prove the dry/saturated, insulation and sub-resolution
 * release behaviour without a WebGL context.
 */
export function advanceFrostState(stateInput = {}, envInput = {}, paramsInput = {}, effectsInput = null, lithInput = {}) {
  const p = normalizeFrostParams(paramsInput);
  const derived = deriveSnowEffectWeights(effectsInput ?? paramsInput.snowEffects ?? createSnowEffectPreset().effects);
  const w = derived.weights;
  const ep = derived.params;
  const env = {
    snowDepthM: Math.max(0, Number(envInput.snowDepthM) || 0),
    iceDepthM: Math.max(0, Number(envInput.iceDepthM) || 0),
    meltM: Math.max(0, Number(envInput.meltM) || 0),
    waterM: Math.max(0, Number(envInput.waterM) || 0),
    slope: Math.max(0, Number(envInput.slope) || 0),
    sedimentM: Math.max(0, Number(envInput.sedimentM) || 0),
    elevationNorm: clamp(envInput.elevationNorm, -0.25, 1.5, 0.75),
    snowlineNorm: clamp(envInput.snowlineNorm, 0, 1.1, 0.45),
    snowMargin: clamp01(envInput.snowMargin, 0.35),
    hollow: clamp01(envInput.hollow, 0.2),
    cellSizeM: Math.max(0.05, Number(envInput.cellSizeM) || 1),
  };
  const lith = {
    hardness: clamp(lithInput.hardness, 0.04, 0.99, 0.55),
    porosity: clamp(lithInput.porosity, 0.005, 0.55, 0.14),
    fracture: clamp01(lithInput.fracture, 0.45),
    clay: clamp01(lithInput.clay, 0.25),
  };
  const prev = {
    damage: clamp(stateInput.damage, 0, 1.25, 0),
    crackMm: clamp(stateInput.crackMm, 0, 250, 0.05),
    saturation: clamp01(stateInput.saturation, p.frostMoisture),
    detachedM: 0,
  };

  const saturationTarget = clamp01(
    p.frostMoisture * 0.72
      + Math.min(0.28, env.meltM * 0.9 + env.waterM * 0.65)
      + Math.min(0.18, env.snowDepthM * 0.12)
      + Math.min(0.14, env.sedimentM * 0.16) * (0.45 + lith.clay)
      - Math.min(0.22, env.slope * 0.10),
    p.frostMoisture,
  );
  const saturationResponse = 1 - Math.exp(-Math.max(0.02, p.frostYearsPerStep) * 0.55);
  const saturation = prev.saturation + (saturationTarget - prev.saturation) * saturationResponse;

  const localMean = p.frostMeanTempC + (env.snowlineNorm - env.elevationNorm) * 20;
  const insulation = Math.exp(-env.snowDepthM / Math.max(0.03, p.frostSnowInsulationM));
  const amplitude = p.frostSeasonalAmplitudeC * (0.12 + 0.88 * insulation);
  const mean = localMean + (1 - insulation) * Math.max(0, -1.5 - localMean) * 0.45;
  const crackDepth = p.frostCrackDepthM * ep.depthScale;

  let segregationIndex = 0;
  let zeroCrossIndex = 0;
  const samples = 16;
  for (let i = 0; i < samples; i++) {
    const phase = i / samples;
    const surface = mean + amplitude * Math.sin(2 * Math.PI * phase);
    const atDepth = temperatureAtDepth({
      meanC: mean,
      amplitudeC: amplitude,
      phaseYears: phase,
      depthM: crackDepth,
      diffusivityE6: p.frostThermalDiffusivity,
    });
    const gradient = Math.abs(surface - atDepth) / Math.max(0.08, crackDepth);
    const gradientFactor = gradient / (gradient + 4 / Math.max(0.05, ep.gradientSensitivity));
    const warmSource = 0.25 + 0.75 * smoothstep(-1.5, 1.5, surface);
    segregationIndex += frostWindowWeight(atDepth, ep.windowLowC, ep.windowHighC) * gradientFactor * warmSource;
    const dailyMean = atDepth;
    const dailyAmplitude = ep.diurnalAmplitudeC * Math.exp(-crackDepth / thermalSkinDepthM(p.frostThermalDiffusivity, 1));
    zeroCrossIndex += Math.exp(-Math.pow(dailyMean / Math.max(1.2, dailyAmplitude + 1.4), 2));
  }
  segregationIndex /= samples;
  zeroCrossIndex /= samples;

  const poreFactor = clamp(lith.porosity / 0.16, 0.08, 2.4, 1);
  const permeability = Math.pow(poreFactor, 1.35) * (1 - 0.62 * lith.clay) * (0.30 + lith.fracture);
  const fractureFactor = 0.35 + 1.25 * lith.fracture;
  const tensileResistance = 0.25 + 2.25 * lith.hardness * lith.hardness;
  const exposure = 0.16 + 0.84 * Math.exp(-env.sedimentM / 0.32);
  const iceShield = 1 - 0.62 * smoothstep(0.08, 1.2, env.iceDepthM);
  const optimalSnow = Math.exp(-Math.pow((env.snowDepthM - ep.optimalSnowDepthM) / Math.max(0.08, ep.snowDepthWidthM), 2));
  const nivationIndex = optimalSnow * (0.35 + 0.65 * env.snowMargin) * (0.70 + 0.55 * env.hollow)
    * (0.55 + 0.45 * smoothstep(0.05, 0.65, env.slope));

  const segRate = w.segregation * segregationIndex
    * saturationEfficacy(saturation, p.frostSaturationPeak, 'segregation')
    * permeability * fractureFactor / tensileResistance;
  const volRate = w.volumetric * ep.cycleSensitivity * zeroCrossIndex
    * saturationEfficacy(saturation, p.frostSaturationPeak, 'volumetric')
    * smoothstep(ep.saturationFloor - 0.12, ep.saturationFloor + 0.10, saturation)
    * poreFactor * (0.40 + lith.fracture) / tensileResistance;
  const nivRate = w.nivation * nivationIndex
    * saturationEfficacy(saturation, p.frostSaturationPeak, 'segregation')
    * (0.45 + Math.min(1.2, env.meltM * 2.2 + env.waterM))
    * poreFactor * fractureFactor / tensileResistance;

  const damageRate = Math.max(0, (segRate * 0.045 + volRate * 0.022 + nivRate * 0.007) * exposure * iceShield);
  const damageIncrement = damageRate * p.frostYearsPerStep;
  let damage = Math.min(1.2, prev.damage + damageIncrement);
  let crackMm = Math.min(220, prev.crackMm + damageIncrement * (16 + prev.crackMm * 0.18) * (0.50 + damage));

  const thresholdMm = visibleCrackThresholdMm(env.cellSizeM, p.frostVisibleCrackMm);
  let detachedM = 0;
  if (damage >= p.frostDamageThreshold && crackMm >= thresholdMm) {
    const excess = Math.min(1.5,
      (damage - p.frostDamageThreshold) * 1.7 + Math.max(0, crackMm / thresholdMm - 1) * 0.35);
    detachedM = Math.min(p.frostPitDepthM, p.frostPitDepthM * (0.28 + 0.72 * excess));
    const release = clamp(detachedM / Math.max(0.005, p.frostPitDepthM), 0, 1, 0);
    damage = Math.max(0.04, damage - release * (0.34 + 0.38 * p.frostDamageThreshold));
    crackMm = Math.max(0.08, crackMm * (1 - 0.62 * release));
  }

  return {
    damage,
    crackMm,
    saturation,
    detachedM,
    diagnostics: {
      meanC: mean,
      amplitudeC: amplitude,
      segregationIndex,
      zeroCrossIndex,
      nivationIndex,
      damageRate,
      thresholdMm,
      permeability,
      exposure,
    },
  };
}
