/**
 * Non-Destructive Geological & SDF Terrain Layer Stack Model and Preset Library.
 *
 * Unifies the 19 battle-tested geological generators, parameters, and seeds (`seed: 4029`)
 * from `erosion-presets.js` with a true bottom-to-top composable Layer Stack pipeline:
 *   - Newest / final operations sit at the TOP of the UI (index 0) and evaluate LAST.
 *   - Base landform is the last terrain operation in the UI and evaluates FIRST.
 *   - One locked Environment system layer is pinned after it as the final
 *     Outliner row. It owns the sun angles and global location/seasonal
 *     climate state, but performs no terrain-generation pass of its own.
 *   - Surface palette, rock fabric, view modes, and particle display stay
 *     separate from the Layer Stack (zero fake colour/biome layers in the stack).
 */

import {
  DEFAULT_EROSION_PARAMS,
  EROSION_LANDSCAPES,
  MATERIAL_THEMES,
  ROCK_FABRIC_MODES,
  getVegetationDefaultsForLandscape,
} from './erosion-presets.js';
import {
  DEFAULT_FROST_PARAMS,
  createSnowEffectPreset,
  summarizeSnowEffects,
} from './frost-weathering.js';
import {
  createStrataColumnPreset,
  summarizeStrataColumn,
} from './stratigraphic-column.js';
import {
  DEFAULT_SEASONAL_CLIMATE_PARAMS,
  climateDefaultsForLandscape,
  climateRegimeFor,
  formatLatitude,
  normalizeSeasonalClimateParams,
} from './seasonal-climate.js';

let layerUidCounter = 1;

export function nextLayerId(prefix = 'lyr') {
  return `${prefix}-${layerUidCounter++}`;
}

export const ENVIRONMENT_LAYER_TYPE = 'environment-sun';
export const ENVIRONMENT_LAYER_NAME = 'Environment';

export const LAYER_CATEGORIES = {
  environment: {
    id: 'environment',
    label: 'Environment',
    eyebrow: 'ENVIRONMENT / SUN + SEASONAL CLIMATE — LOCKED LAST LAYER',
    color: '#ffd166',
  },
  landform: {
    id: 'landform',
    label: 'Landform & SDF',
    eyebrow: 'LANDFORM / SDF OPERATION',
    color: '#f0b35e',
  },
  simulation: {
    id: 'simulation',
    label: 'Simulation',
    eyebrow: 'SIMULATION PROCESS',
    color: '#4ade80',
  },
  spline: {
    id: 'spline',
    label: 'Spline Feature',
    eyebrow: 'SPATIAL SPLINE OPERATION',
    color: '#5ac8fa',
  },
  rocks: {
    id: 'rocks',
    label: 'Rock Placement',
    eyebrow: 'ROCK & BOULDER LAYER',
    color: '#f08a5a',
  },
  vegetation: {
    id: 'vegetation',
    label: 'Vegetation',
    eyebrow: 'PLANT & FOLIAGE LAYER',
    color: '#7ee06a',
  },
};

export const DEFAULT_LAYER_MASK = {
  enabled: false,
  mode: 'height', // 'height' | 'slope' | 'flow' | 'concavity' | 'radial' | 'noise'
  min: 0.20,
  max: 0.85,
  feather: 0.15,
  invert: false,
  scale: 2.2,
  seed: 1337,
};

/**
 * All 19 proven `INIT_TERRAIN_FRAG` geological landform generators (`terrainType = 0..18`),
 * keyed by `terrainType` so `base-landform` executes the exact `c5ae88b` / `2cc3d34` generator!
 */
export const BASE_ARCHETYPES = {};
for (const preset of Object.values(EROSION_LANDSCAPES)) {
  BASE_ARCHETYPES[preset.terrainType] = {
    id: preset.terrainType,
    presetId: preset.id,
    label: preset.name,
  };
}

// A flat start for hand-authored world masks; recipes can still choose any
// existing procedural generator or stack procedural landforms above it.
BASE_ARCHETYPES[19] = { id: 19, label: 'Flat base' };

export const COMBINE_MODES = {
  0: { id: 0, label: 'Add (Uplift)' },
  1: { id: 1, label: 'Subtract (Excavate)' },
  2: { id: 2, label: 'Max (Union)' },
  3: { id: 3, label: 'Min (Clip)' },
  4: { id: 4, label: 'Blend (Lerp)' },
};

export const SPLINE_MODES = {
  0: { id: 0, label: 'Canyon Gorge Cut' },
  1: { id: 1, label: 'Graded Road / Terrace' },
  2: { id: 2, label: 'Lake Basin Boundary' },
};

export const SPLINE_LANDFORM_MODES = {
  0: { id: 0, label: 'Mountain Ridge (Add)' },
  1: { id: 1, label: 'Canyon (Subtract)' },
  2: { id: 2, label: 'River Channel + Water' },
  3: { id: 3, label: 'Road Cut / Grade' },
  4: { id: 4, label: 'SDF Tunnel Through Terrain' },
};

export const SCULPT_BLEND_MODES = {
  0: { id: 0, label: 'Add / Subtract Delta' },
  1: { id: 1, label: 'Multiply / Scale Height' },
  2: { id: 2, label: 'Mix to Target Height' },
  3: { id: 3, label: 'Max / Min Union-Clip' },
};

export const VEG_SPECIES = {
  0: { id: 0, label: 'Alpine Green Grass' },
  1: { id: 1, label: 'Dry Steppe / Desert Grass' },
  2: { id: 2, label: 'Biome Moisture Auto-Blend' },
};

/**
 * All 10 regional geological surface palettes from `MATERIAL_THEMES` (`0..9`),
 * kept strictly separate from the Layer Stack.
 */
export const DERIVED_PALETTES = {};
for (const [k, theme] of Object.entries(MATERIAL_THEMES)) {
  const id = Number(k);
  DERIVED_PALETTES[id] = {
    id,
    label: theme.label,
    subtitle: theme.subtitle,
  };
}

export const DERIVED_FABRICS = {};
for (const [k, fab] of Object.entries(ROCK_FABRIC_MODES)) {
  const id = Number(k);
  DERIVED_FABRICS[id] = {
    id,
    label: fab.label,
  };
}

export const MAX_SPLINE_POINTS = 16;

/**
 * Complete registry of composable Terrain Stack Operations.
 * Each operation declares what terrain channels it READS and what it WRITES.
 */
export const LAYER_OPERATION_SPECS = {
  'base-landform': {
    type: 'base-landform',
    category: 'landform',
    defaultName: 'Base landform',
    glyph: 'mountain',
    lockedPosition: false,
    reads: ['Domain XZ', 'Seed'],
    writes: ['Height', 'Sediment', 'Water'],
    summary: (p) => `${BASE_ARCHETYPES[p.archetype]?.label || 'Icelandic Highlands'} · ${Math.round(p.relief)}m · #${p.seed}`,
    defaults: {
      archetype: 6, // 0..18 -> exact `INIT_TERRAIN_FRAG` terrainType!
      seed: DEFAULT_EROSION_PARAMS.seed, // 4029
      relief: DEFAULT_EROSION_PARAMS.relief, // 168
      roughness: DEFAULT_EROSION_PARAMS.roughness, // 0.68
    },
  },

  'mountain-ridges': {
    type: 'mountain-ridges',
    category: 'landform',
    defaultName: 'Procedural landform',
    glyph: 'ridge',
    reads: ['Height', 'Mask'],
    writes: ['Height', 'FineSed'],
    summary: (p) => `${COMBINE_MODES[p.combineMode]?.label || 'Add'} · ${Math.round(p.amplitude)}m`,
    defaults: {
      generator: 0, // 0=Arête Ridges, 1=Mesa/Plateau, 2=Fault Escarpment, 3=Aeolian Dunes, 4=Impact Craters
      combineMode: 0, // 0=Add, 1=Subtract, 2=Max, 3=Min, 4=Blend
      amplitude: 48,
      scale: 2.2,
      sharpness: 0.60,
      azimuth: 28,
      opacity: 1.0,
      seed: DEFAULT_EROSION_PARAMS.seed,
    },
  },

  'mountain-noise': {
    type: 'mountain-noise',
    category: 'landform',
    defaultName: 'Mountain Noise (Gaea)',
    glyph: 'mountain',
    reads: ['Height', 'Mask'],
    writes: ['Height', 'Coarse Talus'],
    summary: (p) => `${COMBINE_MODES[p.combineMode]?.label || 'Add'} · ${Math.round(p.amplitude ?? 64)}m · fold ${Math.round((p.erosionFold ?? 0.58) * 100)}%`,
    defaults: {
      combineMode: 0,
      amplitude: 64,
      scale: 1.85,
      sharpness: 0.68,
      erosionFold: 0.58,
      ridgeMix: 0.54,
      azimuth: 22,
      screeAmount: 0.42,
      opacity: 1.0,
      seed: DEFAULT_EROSION_PARAMS.seed,
    },
  },

  'ridge-network': {
    type: 'ridge-network',
    category: 'landform',
    defaultName: 'Ridges & Spurs (Gaea)',
    glyph: 'ridge',
    reads: ['Height', 'Mask'],
    writes: ['Height'],
    summary: (p) => `${COMBINE_MODES[p.combineMode]?.label || 'Add'} · ${Math.round(p.amplitude ?? 56)}m · spurs ${Math.round((p.spurDensity ?? 0.62) * 100)}%`,
    defaults: {
      combineMode: 0,
      amplitude: 56,
      scale: 1.7,
      sharpness: 0.74,
      spurDensity: 0.62,
      colDepth: 0.36,
      flankConcavity: 0.64,
      azimuth: -18,
      opacity: 1.0,
      seed: DEFAULT_EROSION_PARAMS.seed,
    },
  },

  'rocky-outcrops': {
    type: 'rocky-outcrops',
    category: 'landform',
    defaultName: 'Rocky Crags & Scree (Gaea)',
    glyph: 'boulders',
    reads: ['Height', 'Slope', 'Mask'],
    writes: ['Height', 'Coarse Talus', '3D Vector Crag'],
    summary: (p) => `${Number(p.cragHeight ?? 14).toFixed(1)}m crags · angular ${Math.round((p.angularity ?? 0.82) * 100)}%`,
    defaults: {
      cragHeight: 14.0,
      cragScale: 2.1,
      angularity: 0.82,
      slopeMin: 18,
      screeAmount: 0.55,
      bedTilt: 14,
      azimuth: 26,
      valleyTors: 0.12,
      opacity: 1.0,
      seed: DEFAULT_EROSION_PARAMS.seed,
    },
  },

  'cliff-generator': {
    type: 'cliff-generator',
    category: 'landform',
    defaultName: 'Cliff & Escarpment Generator',
    glyph: 'cliff',
    reads: ['Height', 'Slope', 'Mask'],
    writes: ['Height', 'Coarse Talus', '3D Overhang'],
    summary: (p) => `${Math.round(p.terraceSteps ?? 4)} steps · ${Math.round((p.steepness ?? 0.84) * 100)}% steep · >${Math.round(p.slopeMin ?? 20)}°`,
    defaults: {
      terraceSteps: 4,
      steepness: 0.84,
      angularity: 0.78,
      slopeMin: 20,
      chuteDepth: 0.42,
      overhang: 0.48,
      talusToe: 0.56,
      featureScale: 1.8,
      opacity: 1.0,
      seed: DEFAULT_EROSION_PARAMS.seed,
    },
  },

  'stratigraphic-beds': {
    type: 'stratigraphic-beds',
    category: 'landform',
    defaultName: 'Stratigraphic beds',
    glyph: 'strata',
    reads: ['Height', '3D Strata'],
    writes: ['3D Hardness', 'Height', 'Overhang', 'Mineral Source', 'Authored Bed Column'],
    summary: (p) => (p.strataColumnEnabled
      ? `${summarizeStrataColumn(p.strataBeds)} · dip ${Math.round(p.strataDip ?? 0)}°`
      : `${Number(p.strataFreq).toFixed(1)} periodic beds · dip ${Math.round(p.strataDip)}° · Fe ${Math.round((p.mineralIron ?? 0.55) * 100)}% · Cu ${Math.round((p.mineralCopper ?? 0.16) * 100)}%`),
    defaults: {
      rockHardness: DEFAULT_EROSION_PARAMS.rockHardness,
      strataStrength: DEFAULT_EROSION_PARAMS.strataStrength,
      strataFreq: DEFAULT_EROSION_PARAMS.strataFreq,
      strataDip: DEFAULT_EROSION_PARAMS.strataDip,
      strataStrike: DEFAULT_EROSION_PARAMS.strataStrike,
      faultWeakness: DEFAULT_EROSION_PARAMS.faultWeakness,
      strataColumnEnabled: DEFAULT_EROSION_PARAMS.strataColumnEnabled,
      strataColumnPreset: DEFAULT_EROSION_PARAMS.strataColumnPreset,
      strataColumnBaseM: DEFAULT_EROSION_PARAMS.strataColumnBaseM,
      strataColumnColorStrength: DEFAULT_EROSION_PARAMS.strataColumnColorStrength,
      strataBeds: createStrataColumnPreset(DEFAULT_EROSION_PARAMS.strataColumnPreset),
      mineralIron: DEFAULT_EROSION_PARAMS.mineralIron,
      mineralCopper: DEFAULT_EROSION_PARAMS.mineralCopper,
      mineralCarbonate: DEFAULT_EROSION_PARAMS.mineralCarbonate,
      mineralSalt: DEFAULT_EROSION_PARAMS.mineralSalt,
      mineralHorizon: DEFAULT_EROSION_PARAMS.mineralHorizon,
      mineralHorizonWidth: DEFAULT_EROSION_PARAMS.mineralHorizonWidth,
      mineralBandStrength: DEFAULT_EROSION_PARAMS.mineralBandStrength,
      terraceCarve: 0.0,
      ledgeOverhang: 0.0,
      opacity: 1.0,
      seed: DEFAULT_EROSION_PARAMS.seed,
    },
  },

  'rock-stacks': {
    type: 'rock-stacks',
    category: 'landform',
    defaultName: 'Rock stacks & hoodoos',
    glyph: 'stacks',
    reads: ['Height', 'Slope', 'Mask'],
    writes: ['Height', '3D Overhang'],
    summary: (p) => `${Math.round(p.height)}m · ${Math.round(p.tiers)} tiers`,
    defaults: {
      height: 48,
      spacing: 68,
      radius: 15,
      taper: 0.42,
      tiers: 5,
      lean: 0.28,
      leanAzimuth: 35,
      caprockOverhang: 0.45,
      opacity: 1.0,
      seed: DEFAULT_EROSION_PARAMS.seed,
    },
  },

  'cliff-push-pull': {
    type: 'cliff-push-pull',
    category: 'landform',
    defaultName: 'Rugged cliff push–pull',
    glyph: 'cliff',
    reads: ['Height', 'Slope', 'Hardness'],
    writes: ['Height', '3D Overhang'],
    summary: (p) => `${Number(p.displacement).toFixed(1)}m · >${Math.round(p.slopeThreshold)}°`,
    defaults: {
      displacement: 10.5,
      featureScale: 2.6,
      slopeThreshold: 28,
      ledgeStrength: 0.62,
      overhangBias: 0.52,
      verticalCrease: 0.58,
      opacity: 1.0,
      seed: DEFAULT_EROSION_PARAMS.seed,
    },
  },

  'sculpt-layer': {
    type: 'sculpt-layer',
    category: 'landform',
    defaultName: 'SDF sculpt layer',
    glyph: 'sculpt',
    reads: ['Height', 'SDF Caves', 'Brush'],
    writes: ['Height', 'SDF Caves'],
    summary: (p) => `${p.strokeCount || 0} edits · ${Math.round((p.opacity ?? 1) * 100)}%`,
    defaults: {
      opacity: 1.0,
      brushOp: 'add', // 'add' | 'remove' | 'smooth' | 'cave-carve' | 'cave-fill'
      brushRadius: 34,
      brushStrength: 0.72,
      caveHeightOffset: 8.0,
      blendMode: 0,
      noiseAmount: 0.0,
      noiseScale: 1.6,
      noiseHeight: 6.0,
      targetHeight: 70.0,
      multiplyStrength: 0.65,
      seed: DEFAULT_EROSION_PARAMS.seed,
      strokeCount: 0,
    },
  },

  'sdf-caves': {
    type: 'sdf-caves',
    category: 'landform',
    defaultName: 'SDF caves & undercuts',
    glyph: 'cave',
    reads: ['Height', 'Slope', 'Hardness'],
    writes: ['3D SDF Volume', 'Cave Mesh'],
    summary: (p) => `${Math.round(p.carveDepth)}m depth · ${Math.round(p.archHeight)}m arch`,
    defaults: {
      carveDepth: 24,
      archHeight: 16,
      tunnelScale: 2.3,
      elevationCenter: 0.36,
      elevationBand: 0.28,
      slopeMin: 24,
      undercutStrength: 0.68,
      ribRoughness: 0.52,
      opacity: 1.0,
      seed: DEFAULT_EROSION_PARAMS.seed,
    },
  },

  'spline-feature': {
    type: 'spline-feature',
    category: 'spline',
    defaultName: 'Spline canyon / road',
    glyph: 'spline',
    reads: ['Height', 'Control Path'],
    writes: ['Height', 'Water'],
    summary: (p) => `${SPLINE_MODES[p.mode]?.label || 'Canyon Cut'} · ${Math.round(p.width)}m`,
    defaults: {
      mode: 0, // 0=Canyon Cut, 1=Graded Mountain Road, 2=Hydrostatic Lake Basin
      width: 44,
      depth: 48,
      wallSteepness: 0.74,
      meanderAmp: 0.32,
      waterFill: 0.28,
      bankFalloff: 0.55,
      opacity: 1.0,
      points: [
        [-190, -145],
        [-72, -38],
        [48, 26],
        [188, 152],
      ],
    },
  },

  'spline-landform': {
    type: 'spline-landform',
    category: 'spline',
    defaultName: 'Spline landform',
    glyph: 'spline',
    reads: ['Height', 'Sediment', 'Control Path'],
    writes: ['Height', 'Sediment', 'Water', 'SDF Tunnel'],
    summary: (p) => `${SPLINE_LANDFORM_MODES[p.mode]?.label || 'Landform'} · ${(p.points || []).length} pts`,
    defaults: {
      mode: 0, // 0=Mountain Ridge, 1=Canyon, 2=River, 3=Road, 4=SDF Tunnel
      width: 72,
      amount: 42,
      falloff: 0.62,
      sharpness: 0.62,
      noiseAmount: 0.38,
      noiseScale: 1.7,
      waterDepth: 1.2,
      sediment: 0.35,
      startElevation: 78,
      endElevation: 24,
      flattenStrength: 0.78,
      bankHeight: 1.6,
      tunnelRadius: 7.5,
      tunnelDepth: 22,
      tunnelFollowSurface: 0,
      tunnelRoughness: 0.32,
      opacity: 1.0,
      seed: DEFAULT_EROSION_PARAMS.seed,
      points: [
        [-190, -120],
        [-95, -55],
        [0, -5],
        [95, 55],
        [190, 120],
      ],
    },
  },

  'lake-basin': {
    type: 'lake-basin',
    category: 'spline',
    defaultName: 'Lake Basin & Shoreline',
    glyph: 'droplet',
    reads: ['Height', 'Sediment', 'Mask'],
    writes: ['Height', 'Sediment', 'Water'],
    summary: (p) => `${Math.round((p.radiusX ?? 78) * 2)}×${Math.round((p.radiusZ ?? 62) * 2)}m · ${Math.round(p.depth ?? 24)}m deep`,
    defaults: {
      centerX: 0,
      centerZ: 0,
      radiusX: 78,
      radiusZ: 62,
      azimuth: 18,
      depth: 24,
      waterLevelOffset: -1.5,
      shoreShelf: 0.62,
      irregularity: 0.44,
      spillwayNotch: 0.48,
      sedimentRim: 0.52,
      opacity: 1.0,
      seed: DEFAULT_EROSION_PARAMS.seed,
    },
  },

  'desert-oasis': {
    type: 'desert-oasis',
    category: 'spline',
    defaultName: 'Artesian Desert Oasis',
    glyph: 'grass',
    reads: ['Height', 'FineSed', 'Mask'],
    writes: ['Height', 'FineSed', 'Water'],
    summary: (p) => `Ø${Math.round((p.radius ?? 56) * 2)}m spring · ${Math.round((p.poolFill ?? 0.72) * 100)}% pool`,
    defaults: {
      centerX: 0,
      centerZ: 0,
      radius: 56,
      basinDepth: 16,
      poolFill: 0.72,
      shelterRim: 8.5,
      wetHaloWidth: 0.68,
      springDischarge: 0.55,
      irregularity: 0.38,
      windAzimuth: 35,
      opacity: 1.0,
      seed: DEFAULT_EROSION_PARAMS.seed,
    },
  },

  'river-incision': {
    type: 'river-incision',
    category: 'simulation',
    defaultName: 'River incision & drainage',
    glyph: 'river',
    reads: ['Height', '3D Hardness'],
    writes: ['Height', 'Flow', 'Water'],
    summary: (p) => `${Math.round(p.iterations)} steps · cut ${Math.round(p.incisionStrength * 100)}%`,
    defaults: {
      iterations: 20,
      incisionStrength: 0.85,
      headwaterRain: 0.68,
      basinPooling: 0.45,
      hardnessRespect: 0.80,
    },
  },

  'hydraulic-erosion': {
    type: 'hydraulic-erosion',
    category: 'simulation',
    defaultName: 'Hydraulic erosion',
    glyph: 'droplet',
    reads: ['Height', '3D Hardness', 'Water'],
    writes: ['Height', 'Sediment', 'Flow'],
    summary: (p) =>
      (p.bakeSteps > 0 ? `${Math.round(p.bakeSteps)} baked + live` : 'Live GPU sim') +
      ` · rain ${Math.round((p.rainIntensity ?? 0.68) * 100)}% · erod ${Math.round((p.erosionStrength ?? 0.70) * 100)}%`,
    defaults: {
      bakeSteps: 0,
      rainIntensity: DEFAULT_EROSION_PARAMS.rainIntensity,
      erosionStrength: DEFAULT_EROSION_PARAMS.erosionStrength,
      sedimentCapacity: DEFAULT_EROSION_PARAMS.sedimentCapacity,
      depositionRate: DEFAULT_EROSION_PARAMS.depositionRate,
      evaporation: DEFAULT_EROSION_PARAMS.evaporation,
      headwaterFlow: DEFAULT_EROSION_PARAMS.headwaterFlow,
      lakeRetention: DEFAULT_EROSION_PARAMS.lakeRetention,
      oasisSpring: 0,
    },
  },

  'thermal-talus': {
    type: 'thermal-talus',
    category: 'simulation',
    defaultName: 'Thermal erosion & talus',
    glyph: 'talus',
    reads: ['Height', '3D Hardness'],
    writes: ['Height', 'Coarse Talus'],
    summary: (p) => {
      const deg = Math.round((Math.atan(p.talusAngle ?? 0.58) * 180) / Math.PI);
      return `${p.bakePasses > 0 ? `${Math.round(p.bakePasses)} baked · ` : ''}${deg}° repose · spall ${Math.round((p.rockfallRate ?? 0.45) * 100)}%`;
    },
    defaults: {
      bakePasses: 0,
      talusAngle: DEFAULT_EROSION_PARAMS.talusAngle, // slope tangent (0.58 ≈ 30°)
      rockfallRate: DEFAULT_EROSION_PARAMS.rockfallRate,
    },
  },

  'chemical-weathering': {
    type: 'chemical-weathering',
    category: 'simulation',
    defaultName: 'Chemical weathering & dissolution',
    glyph: 'flask',
    reads: ['Height', 'Water', 'Flow', 'SnowIce', '3D Hardness'],
    writes: ['Height', 'Sediment', 'Chemical State'],
    summary: (p) => (p.bakeSteps > 0 ? `${Math.round(p.bakeSteps)} bake steps · ${Math.round((p.chemRate ?? DEFAULT_EROSION_PARAMS.chemRate) * 100)}% rate` : `Live chemical sim · ${Math.round((p.chemRate ?? DEFAULT_EROSION_PARAMS.chemRate) * 100)}% rate`),
    defaults: {
      bakeSteps: 0,
      chemRate: DEFAULT_EROSION_PARAMS.chemRate,
      chemRust: DEFAULT_EROSION_PARAMS.chemRust,
      chemHydrolysis: DEFAULT_EROSION_PARAMS.chemHydrolysis,
      chemDissolution: DEFAULT_EROSION_PARAMS.chemDissolution,
      chemSalt: DEFAULT_EROSION_PARAMS.chemSalt,
      chemVarnish: DEFAULT_EROSION_PARAMS.chemVarnish,
      chemCopper: DEFAULT_EROSION_PARAMS.chemCopper,
    },
  },
  'wind-erosion': {
    type: 'wind-erosion',
    category: 'simulation',
    defaultName: 'Wind erosion & sandblasting',
    glyph: 'wind',
    reads: ['Height', 'FineSed', 'Slope', '3D Hardness'],
    writes: ['Height', 'FineSed', 'CoarseSed'],
    summary: (p) => {
      const shiftMode = Number(p.windShiftMode ?? DEFAULT_EROSION_PARAMS.windShiftMode);
      const shift = shiftMode === 3
        ? ' · full wind rose (equal sectors)'
        : shiftMode === 2
          ? ` · weather-front sectors ±${Math.round(p.windShiftRange ?? DEFAULT_EROSION_PARAMS.windShiftRange)}°`
          : shiftMode === 1
            ? ` · variable wind rose, spread ${Math.round(p.windShiftRange ?? DEFAULT_EROSION_PARAMS.windShiftRange)}°`
            : ' · fixed direction';
      return `${p.bakeSteps > 0 ? `${Math.round(p.bakeSteps)} baked · ` : ''}${Math.round((p.windSpeed ?? DEFAULT_EROSION_PARAMS.windSpeed) * 100)}% @ ${Math.round(p.windAzimuth ?? DEFAULT_EROSION_PARAMS.windAzimuth)}°${shift} · abrasion ${Math.round((p.windAbrasion ?? DEFAULT_EROSION_PARAMS.windAbrasion) * 100)}%`;
    },
    defaults: {
      bakeSteps: 0,
      windSpeed: DEFAULT_EROSION_PARAMS.windSpeed,
      windAzimuth: DEFAULT_EROSION_PARAMS.windAzimuth,
      windShiftMode: DEFAULT_EROSION_PARAMS.windShiftMode,
      windShiftRange: DEFAULT_EROSION_PARAMS.windShiftRange,
      windShiftPeriod: DEFAULT_EROSION_PARAMS.windShiftPeriod,
      dryAeolianMode: DEFAULT_EROSION_PARAMS.dryAeolianMode,
      windErosionRate: DEFAULT_EROSION_PARAMS.windErosionRate,
      windAbrasion: DEFAULT_EROSION_PARAMS.windAbrasion,
      windDeflation: DEFAULT_EROSION_PARAMS.windDeflation,
      windDeposition: DEFAULT_EROSION_PARAMS.windDeposition,
      windTurbulence: DEFAULT_EROSION_PARAMS.windTurbulence,
      windGustiness: DEFAULT_EROSION_PARAMS.windGustiness,
      windSandSupply: DEFAULT_EROSION_PARAMS.windSandSupply,
      windCurtainHeight: DEFAULT_EROSION_PARAMS.windCurtainHeight,
      windStreamlining: DEFAULT_EROSION_PARAMS.windStreamlining,
      windFluting: DEFAULT_EROSION_PARAMS.windFluting,
    },
  },

  'aeolian-glacial': {
    type: 'aeolian-glacial',
    category: 'simulation',
    defaultName: 'Snowpack & glaciers',
    glyph: 'snow',
    reads: ['Height', 'FineSed', '3D Hardness', 'Bed Porosity/Fractures'],
    writes: ['Height', 'FineSed', 'CoarseSed', 'SnowIce', 'Frost Damage'],
    summary: (p) => `Melt line ${Math.round((p.snowline ?? 0.42) * 100)}% · snowfall ${Math.round((p.snowfallRate ?? 0.82) * 100)}% · ${summarizeSnowEffects(p.snowEffects)}`,
    defaults: {
      bakePasses: 0,
      snowline: DEFAULT_EROSION_PARAMS.snowline,
      snowfallRate: DEFAULT_EROSION_PARAMS.snowfallRate,
      glacierStrength: DEFAULT_EROSION_PARAMS.glacierStrength,
      iceMeltRate: DEFAULT_EROSION_PARAMS.iceMeltRate,
      snowEffectPreset: DEFAULT_EROSION_PARAMS.snowEffectPreset,
      snowEffects: createSnowEffectPreset(DEFAULT_EROSION_PARAMS.snowEffectPreset).effects,
      ...structuredClone(DEFAULT_FROST_PARAMS),
      // Legacy fallback fields only; new Snow layers do not secretly start
      // a Wind season. Add Wind Erosion when wind is wanted.
      windSpeed: 0,
      windAzimuth: DEFAULT_EROSION_PARAMS.windAzimuth,
      dryAeolianMode: 0,
    },
  },

  'rock-placement': {
    type: 'rock-placement',
    category: 'rocks',
    defaultName: 'Distributed rocks & boulders',
    glyph: 'boulders',
    reads: ['Height', 'Slope', 'Coarse Talus', 'Water'],
    writes: ['3D Rock Instances'],
    summary: (p) => `${Math.round((p.density ?? 0.65) * 100)}% density · #${p.seed ?? 4029}`,
    defaults: {
      density: DEFAULT_EROSION_PARAMS.boulderDensity,
      talusAffinity: DEFAULT_EROSION_PARAMS.rockfallRate,
      settleSteps: 0,
      seed: DEFAULT_EROSION_PARAMS.seed,
    },
  },

  'vegetation-placement': {
    type: 'vegetation-placement',
    category: 'vegetation',
    defaultName: '3D grass & plants',
    glyph: 'grass',
    reads: ['Height', 'Slope', 'Water', 'Sediment', 'SnowIce'],
    writes: ['3D Plant Instances'],
    summary: (p) => `${VEG_SPECIES[p.species]?.label || 'Green Grass'} · ${Math.round((p.density ?? 0.55) * 100)}%`,
    defaults: {
      species: DEFAULT_EROSION_PARAMS.grassMode,
      density: DEFAULT_EROSION_PARAMS.grassDensity,
      bladeHeight: DEFAULT_EROSION_PARAMS.grassHeight,
      slopeMax: DEFAULT_EROSION_PARAMS.grassSlopeMax, // slope tangent (0.62 ≈ 32°)
      elevationMax: DEFAULT_EROSION_PARAMS.grassElevMax,
      patchiness: DEFAULT_EROSION_PARAMS.grassPatchiness,
      windSway: DEFAULT_EROSION_PARAMS.grassWindSway,
      dryBlend: DEFAULT_EROSION_PARAMS.grassDryBlend,
      seed: DEFAULT_EROSION_PARAMS.seed,
    },
  },

  [ENVIRONMENT_LAYER_TYPE]: {
    type: ENVIRONMENT_LAYER_TYPE,
    category: 'environment',
    defaultName: ENVIRONMENT_LAYER_NAME,
    glyph: 'sun',
    // System singleton: pipeline/UI code must never offer this in the Add
    // menu, duplicate it, delete it, mute it, mask it, or move another
    // layer below it. It is not a terrain-generation or erosion pass.
    systemLayer: true,
    singleton: true,
    addable: false,
    lockedPosition: 'last',
    reads: ['Sun Azimuth', 'Sun Elevation', 'Latitude', 'Climate Regime'],
    writes: ['Sun Direction', 'Seasonal Climate'],
    summary: (p) => {
      const climate = normalizeSeasonalClimateParams(p);
      const sun = `Sun ${Math.round(p.sunAzimuth ?? DEFAULT_EROSION_PARAMS.sunAzimuth)}°/${Math.round(p.sunElevation ?? DEFAULT_EROSION_PARAMS.sunElevation)}°`;
      if (!climate.seasonalClimateMode) return `${sun} · seasons off · locked`;
      const regime = climateRegimeFor(climate.climateRegime).label.split(' /')[0];
      const run = climate.climateYearsToSimulate > 0 ? `${Math.round(climate.climateYearsToSimulate)} yr` : 'continuous';
      return `${sun} · ${regime} · ${formatLatitude(climate.climateLatitudeDeg)} · ${run} · locked`;
    },
    defaults: {
      sunAzimuth: DEFAULT_EROSION_PARAMS.sunAzimuth,
      sunElevation: DEFAULT_EROSION_PARAMS.sunElevation,
      ...DEFAULT_SEASONAL_CLIMATE_PARAMS,
    },
  },
};

export function createStackLayer(type, overrides = {}) {
  const spec = LAYER_OPERATION_SPECS[type];
  if (!spec) throw new Error(`Unknown terrain layer operation: ${type}`);
  const { name, enabled = true, locked = false, mask, params = {}, ...restParams } = overrides;
  const isEnvironment = type === ENVIRONMENT_LAYER_TYPE;
  return {
    id: nextLayerId(type.slice(0, 4)),
    type,
    name: isEnvironment ? ENVIRONMENT_LAYER_NAME : (name || spec.defaultName),
    enabled: isEnvironment ? true : enabled,
    locked: isEnvironment ? true : locked,
    mask: {
      ...DEFAULT_LAYER_MASK,
      ...(mask || {}),
      ...(isEnvironment ? { enabled: false } : {}),
    },
    params: {
      ...structuredClone(spec.defaults),
      ...structuredClone(restParams),
      ...structuredClone(params),
    },
  };
}

export function createEnvironmentLayer(overrides = {}) {
  const params = { ...(overrides.params || {}) };
  if (Number.isFinite(Number(overrides.sunAzimuth))) params.sunAzimuth = Number(overrides.sunAzimuth);
  if (Number.isFinite(Number(overrides.sunElevation))) params.sunElevation = Number(overrides.sunElevation);
  for (const key of Object.keys(DEFAULT_SEASONAL_CLIMATE_PARAMS)) {
    if (Number.isFinite(Number(overrides[key]))) params[key] = Number(overrides[key]);
  }
  Object.assign(params, normalizeSeasonalClimateParams(params));
  return createStackLayer(ENVIRONMENT_LAYER_TYPE, {
    name: ENVIRONMENT_LAYER_NAME,
    enabled: true,
    locked: true,
    mask: { enabled: false },
    params,
  });
}

/**
 * Return a stack containing exactly one canonical Environment layer, pinned
 * as the final (last) row. Existing Environment sun values win over caller
 * defaults, which makes this safe for preset loads, restored snapshots and
 * ordinary stack mutations.
 */
export function ensureEnvironmentLayer(layers = [], sunDefaults = {}) {
  const source = Array.isArray(layers) ? layers : [];
  const existing = source.find((layer) => layer?.type === ENVIRONMENT_LAYER_TYPE);
  const rest = source.filter((layer) => layer?.type !== ENVIRONMENT_LAYER_TYPE);
  const environment = existing || createEnvironmentLayer(sunDefaults);
  const defaults = LAYER_OPERATION_SPECS[ENVIRONMENT_LAYER_TYPE].defaults;
  const defaultSource = sunDefaults?.params && typeof sunDefaults.params === 'object'
    ? { ...sunDefaults, ...sunDefaults.params }
    : (sunDefaults || {});
  const cleanSunDefaults = {};
  if (Number.isFinite(Number(defaultSource.sunAzimuth))) cleanSunDefaults.sunAzimuth = Number(defaultSource.sunAzimuth);
  if (Number.isFinite(Number(defaultSource.sunElevation))) cleanSunDefaults.sunElevation = Number(defaultSource.sunElevation);
  if (Object.keys(DEFAULT_SEASONAL_CLIMATE_PARAMS).some((key) => Number.isFinite(Number(defaultSource[key])))) {
    Object.assign(cleanSunDefaults, normalizeSeasonalClimateParams(defaultSource));
  }

  environment.name = ENVIRONMENT_LAYER_NAME;
  environment.enabled = true;
  environment.locked = true;
  environment.mask = {
    ...structuredClone(DEFAULT_LAYER_MASK),
    ...(environment.mask || {}),
    enabled: false,
  };
  environment.params = {
    ...structuredClone(defaults),
    ...cleanSunDefaults,
    ...(environment.params || {}),
  };
  Object.assign(environment.params, normalizeSeasonalClimateParams(environment.params));
  return [...rest, environment];
}

/**
 * Decomposes any of the 19 canonical `EROSION_LANDSCAPES` (`erosion-presets.js`) into its exact
 * bottom-to-top editable Layer Stack (index 0 = TOP / evaluated LAST; Base is
 * the first terrain pass and the locked Environment row follows it last)
 * while preserving 100% identical generator parameters, seed (`4029`), and simulation behavior!
 */
function buildStackRecipeFromLandscape(landscapeId) {
  const preset = EROSION_LANDSCAPES[landscapeId];
  const d = { ...DEFAULT_EROSION_PARAMS, ...(preset.defaults || {}) };
  const veg = getVegetationDefaultsForLandscape(landscapeId);
  const seed = d.seed ?? 4029;

  const layers = []; // Push TOP -> BOTTOM

  // 1. Top: 3D Grass Vegetation Placement (enabled on biomes with grass)
  if (veg.grassEnabled) {
    layers.push(
      createStackLayer('vegetation-placement', {
        name: veg.grassMode === 1 ? 'Dry desert / canyon grass' : veg.grassMode === 2 ? 'Biome-blended 3D turf' : 'Lush highland 3D turf',
        species: veg.grassMode,
        density: veg.grassDensity,
        bladeHeight: veg.grassHeight,
        slopeMax: veg.grassSlopeMax,
        elevationMax: veg.grassElevMax,
        patchiness: veg.grassPatchiness,
        windSway: DEFAULT_EROSION_PARAMS.grassWindSway,
        dryBlend: veg.grassDryBlend,
        seed,
      })
    );
  }

  // 2. 3D Polyhedral Outcrops & Calved Cliff Boulders
  layers.push(
    createStackLayer('rock-placement', {
      name: '3D outcrops & talus boulders',
      enabled: d.showOutcrops !== false,
      density: d.boulderDensity ?? DEFAULT_EROSION_PARAMS.boulderDensity,
      talusAffinity: d.rockfallRate ?? DEFAULT_EROSION_PARAMS.rockfallRate,
      settleSteps: 0,
      seed,
    })
  );

  // 3. Cryosphere and Wind are separate simulation layers / seasons.
  // Snowpack & glaciers no longer hide the wind controls: wind direction,
  // abrasion, saltation, turbulence and fluting live on Wind Erosion.
  const hasCryo = (d.snowfallRate ?? 0) > 0.015 || (d.glacierStrength ?? 0) > 0.015;
  const hasWind = (d.windSpeed ?? 0) > 0.04;
  if (hasCryo) {
    layers.push(
      createStackLayer('aeolian-glacial', {
        name: 'Snowpack & glaciers',
        bakePasses: 0,
        snowline: d.snowline,
        snowfallRate: d.snowfallRate,
        glacierStrength: d.glacierStrength,
        iceMeltRate: d.iceMeltRate,
        // Wind is owned by the dedicated layer below when present. These
        // legacy fields remain only as a fallback for manually built stacks.
        windSpeed: hasWind ? 0 : d.windSpeed,
        windAzimuth: d.windAzimuth,
        dryAeolianMode: 0,
      })
    );
  }
  if (hasWind) {
    const dryWind = (d.dryAeolianMode ?? 0) > 0.4;
    const rockyWind = preset.terrainType === 1 || preset.terrainType === 4 || preset.terrainType === 12 || preset.terrainType === 15;
    layers.push(
      createStackLayer('wind-erosion', {
        name: dryWind ? 'Wind erosion, saltation & sandblasting' : 'Wind erosion & deflation',
        bakeSteps: 0,
        windSpeed: d.windSpeed,
        windAzimuth: d.windAzimuth,
        windShiftMode: d.windShiftMode ?? DEFAULT_EROSION_PARAMS.windShiftMode,
        windShiftRange: d.windShiftRange ?? DEFAULT_EROSION_PARAMS.windShiftRange,
        windShiftPeriod: d.windShiftPeriod ?? DEFAULT_EROSION_PARAMS.windShiftPeriod,
        dryAeolianMode: d.dryAeolianMode,
        windErosionRate: d.windErosionRate ?? DEFAULT_EROSION_PARAMS.windErosionRate,
        windAbrasion: d.windAbrasion ?? (rockyWind ? 1.0 : DEFAULT_EROSION_PARAMS.windAbrasion),
        windDeflation: d.windDeflation ?? DEFAULT_EROSION_PARAMS.windDeflation,
        windDeposition: d.windDeposition ?? DEFAULT_EROSION_PARAMS.windDeposition,
        windTurbulence: d.windTurbulence ?? (rockyWind ? 0.72 : DEFAULT_EROSION_PARAMS.windTurbulence),
        windGustiness: d.windGustiness ?? DEFAULT_EROSION_PARAMS.windGustiness,
        windSandSupply: d.windSandSupply ?? (dryWind ? 0.82 : DEFAULT_EROSION_PARAMS.windSandSupply),
        windCurtainHeight: d.windCurtainHeight ?? DEFAULT_EROSION_PARAMS.windCurtainHeight,
        windStreamlining: d.windStreamlining ?? (preset.terrainType === 12 || preset.terrainType === 15 ? 0.74 : DEFAULT_EROSION_PARAMS.windStreamlining),
        windFluting: d.windFluting ?? (rockyWind ? 0.78 : DEFAULT_EROSION_PARAMS.windFluting),
      })
    );
  }

  // 4. Thermal Cliff Spalling & Talus Scree Repose (when active in preset)
  if ((d.rockfallRate ?? 0) > 0.015) {
    layers.push(
      createStackLayer('thermal-talus', {
        name: 'Cliff spalling & talus repose',
        bakePasses: 0,
        talusAngle: d.talusAngle,
        rockfallRate: d.rockfallRate,
      })
    );
  }

  // 4B. Chemical weathering & dissolution: reacts with the water, flow, snowmelt,
  // mineral veins and hardness fields produced below it, then weakens/dissolves
  // bedrock and produces clay fines for the talus + hydraulic processes above it.
  // The airless lunar material theme (7) has no aqueous chemistry layer.
  const themeCodeChem = preset.materialTheme ?? d.materialTheme ?? 0;
  if (themeCodeChem !== 7 && (d.chemRate ?? DEFAULT_EROSION_PARAMS.chemRate) > 0.001) {
    const aridChem = (d.dryAeolianMode ?? 0) > 0.4 || themeCodeChem === 5 || themeCodeChem === 6 || themeCodeChem === 9;
    layers.push(
      createStackLayer('chemical-weathering', {
        name: aridChem ? 'Salt, varnish & chemical weathering' : 'Chemical weathering & dissolution',
        bakeSteps: 0,
        chemRate: d.chemRate,
        chemRust: d.chemRust,
        chemHydrolysis: d.chemHydrolysis,
        chemDissolution: d.chemDissolution,
        chemSalt: aridChem ? Math.min(1.0, (d.chemSalt ?? DEFAULT_EROSION_PARAMS.chemSalt) * 1.35) : d.chemSalt,
        chemVarnish: aridChem ? Math.min(1.0, (d.chemVarnish ?? DEFAULT_EROSION_PARAMS.chemVarnish) * 1.4) : d.chemVarnish,
        chemCopper: d.chemCopper,
      })
    );
  }

  // 5. Hydraulic Rain, Shallow-Water Pipe Flow & Alluvial Sedimentation (when active in preset)
  const hasHydro =
    (d.rainIntensity ?? 0) > 0.005 ||
    (d.headwaterFlow ?? 0) > 0.005 ||
    Boolean(d.oasisSpring);
  if (hasHydro) {
    layers.push(
      createStackLayer('hydraulic-erosion', {
        name: d.oasisSpring ? 'Artesian oasis spring & hydrology' : (d.headwaterFlow ?? 0) > 0.25 ? 'River discharge & hydraulic erosion' : 'Hydraulic rain & alluvial runoff',
        bakeSteps: 0,
        rainIntensity: d.rainIntensity,
        erosionStrength: d.erosionStrength,
        sedimentCapacity: d.sedimentCapacity,
        depositionRate: d.depositionRate,
        evaporation: d.evaporation,
        headwaterFlow: d.headwaterFlow ?? 0,
        lakeRetention: d.lakeRetention ?? 0.4,
        oasisSpring: d.oasisSpring ? 1 : 0,
      })
    );
  }

  // 6. 3D Stratigraphic Bedding, Differential Rock Hardness & Mineral Beds.
  // These sedimentary layers carry the iron/copper/carbonate/salt that the
  // chemical season later dissolves and transports with surface water.
  const mineralTheme = preset.materialTheme ?? d.materialTheme ?? 0;
  const mineralArid = (d.dryAeolianMode ?? 0) > 0.4 || mineralTheme === 0 || mineralTheme === 3 || mineralTheme === 5 || mineralTheme === 6 || mineralTheme === 9;
  const mineralBasalt = mineralTheme === 1 || mineralTheme === 4 || mineralTheme === 8;
  const mineralScale = mineralTheme === 7 ? 0 : 1;
  layers.push(
    createStackLayer('stratigraphic-beds', {
      name: '3D stratigraphy, hardness & mineral beds',
      rockHardness: d.rockHardness,
      strataStrength: d.strataStrength,
      strataFreq: d.strataFreq,
      strataDip: d.strataDip,
      strataStrike: d.strataStrike,
      faultWeakness: d.faultWeakness,
      mineralIron: mineralScale * (d.mineralIron ?? (mineralArid ? 0.72 : mineralBasalt ? 0.64 : DEFAULT_EROSION_PARAMS.mineralIron)),
      mineralCopper: mineralScale * (d.mineralCopper ?? (mineralArid ? 0.20 : DEFAULT_EROSION_PARAMS.mineralCopper)),
      mineralCarbonate: mineralScale * (d.mineralCarbonate ?? (mineralArid ? 0.58 : DEFAULT_EROSION_PARAMS.mineralCarbonate)),
      mineralSalt: mineralScale * (d.mineralSalt ?? (mineralArid ? 0.52 : DEFAULT_EROSION_PARAMS.mineralSalt)),
      mineralHorizon: d.mineralHorizon ?? DEFAULT_EROSION_PARAMS.mineralHorizon,
      mineralHorizonWidth: d.mineralHorizonWidth ?? DEFAULT_EROSION_PARAMS.mineralHorizonWidth,
      mineralBandStrength: d.mineralBandStrength ?? DEFAULT_EROSION_PARAMS.mineralBandStrength,
      terraceCarve: 0.0,
      ledgeOverhang: 0.0,
      opacity: 1.0,
      seed,
    })
  );

  // 7. Bottom Foundation: Exact `INIT_TERRAIN_FRAG` Landform Generator (`terrainType` 0..18, `seed: 4029`)
  layers.push(
    createStackLayer('base-landform', {
      name: `${preset.name} base`,
      archetype: preset.terrainType,
      seed,
      relief: d.relief,
      roughness: d.roughness,
    })
  );

  // 8. Locked Environment singleton: always the final Outliner row. It
  // carries this preset's exact sun angles and geographic seasonal climate,
  // but performs no terrain pass of its own.
  layers.push(createEnvironmentLayer({
    sunAzimuth: d.sunAzimuth ?? DEFAULT_EROSION_PARAMS.sunAzimuth,
    sunElevation: d.sunElevation ?? DEFAULT_EROSION_PARAMS.sunElevation,
    params: climateDefaultsForLandscape(landscapeId),
  }));

  return {
    id: preset.id,
    name: preset.name,
    subtitle: preset.subtitle,
    category: preset.category,
    terrainType: preset.terrainType,
    palette: preset.materialTheme ?? 0,
    fabricMode: d.fabricMode ?? 0,
    emitterMode: d.emitterMode ?? DEFAULT_EROSION_PARAMS.emitterMode,
    particleCount: d.particleCount ?? DEFAULT_EROSION_PARAMS.particleCount,
    particleInertia: d.particleInertia ?? DEFAULT_EROSION_PARAMS.particleInertia,
    pipeFlowBlend: d.pipeFlowBlend ?? DEFAULT_EROSION_PARAMS.pipeFlowBlend,
    sunAzimuth: d.sunAzimuth ?? DEFAULT_EROSION_PARAMS.sunAzimuth,
    sunElevation: d.sunElevation ?? DEFAULT_EROSION_PARAMS.sunElevation,
    showWater: d.showWater ?? true,
    showParticles: d.showParticles ?? true,
    layers: () => structuredClone(layers).map((l) => ({ ...l, id: nextLayerId(l.type.slice(0, 4)) })),
  };
}

/**
 * Curated Utah stack requested for wind-led canyon work: rocky rugged-desert
 * base + strong sedimentary strata + Rain + dedicated Wind Erosion only.
 * Deliberately contains no snow/glacier, thermal rockfall, rock-placement,
 * chemical, vegetation or cave layers, so the two live seasons are exactly
 * Rain and Wind and either can be simulated one at a time.
 */
function buildUtahWindRainRecipe() {
  const seed = DEFAULT_EROSION_PARAMS.seed;
  const layers = [
    createStackLayer('wind-erosion', {
      name: 'Utah wind erosion & sandblasting',
      bakeSteps: 0,
      windSpeed: 0.92,
      windAzimuth: 32,
      // Desert fronts do not sandblast from one bearing forever: each
      // weather cycle anchors spells in every sector either side of the
      // mean and veers/backs smoothly through the bearings in between.
      windShiftMode: 2,
      windShiftRange: 70,
      windShiftPeriod: 720,
      dryAeolianMode: 0.90,
      windErosionRate: 1.05,
      windAbrasion: 1.08,
      windDeflation: 0.75,
      windDeposition: 0.45,
      windTurbulence: 0.78,
      windGustiness: 0.55,
      windSandSupply: 0.85,
      windCurtainHeight: 0.48,
      windStreamlining: 0.72,
      windFluting: 0.82,
    }),
    createStackLayer('hydraulic-erosion', {
      name: 'Desert flash rain & wash runoff',
      bakeSteps: 0,
      rainIntensity: 0.36,
      erosionStrength: 0.72,
      sedimentCapacity: 0.74,
      depositionRate: 0.48,
      evaporation: 0.38,
      headwaterFlow: 0,
      lakeRetention: 0.12,
      oasisSpring: 0,
    }),
    createStackLayer('stratigraphic-beds', {
      name: 'Utah sandstone / shale strata',
      rockHardness: 0.50,
      strataStrength: 0.92,
      strataFreq: 11.0,
      strataDip: 5,
      strataStrike: 32,
      faultWeakness: 0.70,
      mineralIron: 0.68,
      mineralCopper: 0.10,
      mineralCarbonate: 0.52,
      mineralSalt: 0.34,
      mineralHorizon: 0.42,
      mineralHorizonWidth: 0.55,
      mineralBandStrength: 0.80,
      terraceCarve: 0.16,
      ledgeOverhang: 0.10,
      opacity: 1.0,
      seed,
    }),
    createStackLayer('base-landform', {
      name: 'Utah rugged desert crags base',
      archetype: 15,
      seed,
      relief: 158,
      roughness: 0.68,
    }),
    createEnvironmentLayer({ sunAzimuth: 55, sunElevation: 24, params: climateDefaultsForLandscape('utah-wind-rain-canyon') }),
  ];
  return {
    manualRecipe: true,
    expectedRelief: 158,
    expectedRoughness: 0.68,
    id: 'utah-wind-rain-canyon',
    name: 'Utah Wind + Rain Canyon',
    subtitle: 'Rocky Utah sandstone with Rain + turbulent Wind Erosion only — no snow, rockfall or chemical layers',
    category: 'Wind + Rain Stack',
    terrainType: 15,
    palette: 0,
    fabricMode: 0,
    emitterMode: 'all-weather',
    particleCount: DEFAULT_EROSION_PARAMS.particleCount,
    particleInertia: 0.38,
    pipeFlowBlend: 0.50,
    sunAzimuth: 55,
    sunElevation: 24,
    showWater: true,
    showParticles: true,
    layers: () => structuredClone(layers).map((l) => ({ ...l, id: nextLayerId(l.type.slice(0, 4)) })),
  };
}

/**
 * Standalone authoring test terrains for the parameterized Spline Landform
 * layer. These are deliberately NOT woven into the 19 canonical landscape
 * recipes: existing terrains keep their exact stacks, while these two new
 * terrains exercise ridge/canyon/river/road/tunnel splines plus an empty,
 * ready-to-paint sculpt layer with noise blending.
 */
function buildSplineCanyonRiverRecipe() {
  const seed = DEFAULT_EROSION_PARAMS.seed;
  const layers = [
    createStackLayer('hydraulic-erosion', {
      name: 'Rain after spline river & canyon',
      bakeSteps: 0,
      rainIntensity: 0.52,
      erosionStrength: 0.78,
      sedimentCapacity: 0.82,
      depositionRate: 0.52,
      evaporation: 0.24,
      headwaterFlow: 0.35,
      lakeRetention: 0.28,
      oasisSpring: 0,
    }),
    createStackLayer('sculpt-layer', {
      name: 'Author sculpt — empty, paint basic shapes here',
      brushRadius: 46,
      brushStrength: 0.85,
      blendMode: 0,
      noiseAmount: 0.35,
      noiseScale: 1.8,
      noiseHeight: 5.0,
      targetHeight: 82,
      multiplyStrength: 0.65,
      seed,
    }),
    createStackLayer('spline-landform', {
      name: 'Directed river spline',
      mode: 2,
      width: 30,
      amount: 17,
      falloff: 0.58,
      sharpness: 0.48,
      noiseAmount: 0.30,
      noiseScale: 1.9,
      waterDepth: 1.45,
      sediment: 0.48,
      startElevation: 82,
      endElevation: 18,
      flattenStrength: 0.92,
      bankHeight: 1.1,
      seed: 7101,
      points: [[-178, -196], [-92, -82], [-18, 18], [62, 96], [178, 184]],
    }),
    createStackLayer('spline-landform', {
      name: 'Subtractive canyon spline',
      mode: 1,
      width: 62,
      amount: 50,
      falloff: 0.60,
      sharpness: 0.80,
      noiseAmount: 0.44,
      noiseScale: 1.55,
      sediment: 0.30,
      startElevation: 96,
      endElevation: 28,
      flattenStrength: 0.30,
      bankHeight: 2.2,
      seed: 7102,
      points: [[-205, 86], [-102, 38], [8, -8], [118, -48], [205, -92]],
    }),
    createStackLayer('spline-landform', {
      name: 'Additive mountain-ridge spline',
      mode: 0,
      width: 88,
      amount: 54,
      falloff: 0.64,
      sharpness: 0.70,
      noiseAmount: 0.46,
      noiseScale: 1.45,
      sediment: 0.22,
      bankHeight: 0.8,
      seed: 7103,
      points: [[-205, -128], [-112, -52], [-8, 4], [98, 66], [205, 128]],
    }),
    createStackLayer('stratigraphic-beds', {
      name: 'Spline test sedimentary strata',
      rockHardness: 0.52,
      strataStrength: 0.86,
      strataFreq: 10.0,
      strataDip: 6,
      strataStrike: 24,
      faultWeakness: 0.62,
      mineralIron: 0.56,
      mineralCopper: 0.14,
      mineralCarbonate: 0.48,
      mineralSalt: 0.22,
      mineralHorizon: 0.44,
      mineralHorizonWidth: 0.52,
      mineralBandStrength: 0.72,
      terraceCarve: 0.10,
      ledgeOverhang: 0.05,
      opacity: 1.0,
      seed,
    }),
    createStackLayer('base-landform', {
      name: 'Spline canyon/river test base',
      archetype: 1,
      seed,
      relief: 132,
      roughness: 0.58,
    }),
    createEnvironmentLayer({ sunAzimuth: 52, sunElevation: 25, params: climateDefaultsForLandscape('spline-canyon-river-test') }),
  ];
  return {
    manualRecipe: true,
    expectedRelief: 132,
    expectedRoughness: 0.58,
    id: 'spline-canyon-river-test',
    name: 'Spline Canyon + River Test',
    subtitle: 'New isolated test terrain: additive ridge spline, subtractive canyon spline, directed river spline and empty noise-blended sculpt layer',
    category: 'Spline Authoring Test',
    terrainType: 1,
    palette: 0,
    fabricMode: 0,
    emitterMode: 'all-weather',
    particleCount: DEFAULT_EROSION_PARAMS.particleCount,
    particleInertia: 0.38,
    pipeFlowBlend: 0.50,
    sunAzimuth: 52,
    sunElevation: 25,
    showWater: true,
    showParticles: true,
    layers: () => structuredClone(layers).map((l) => ({ ...l, id: nextLayerId(l.type.slice(0, 4)) })),
  };
}

function buildSplineRoadTunnelRecipe() {
  const seed = DEFAULT_EROSION_PARAMS.seed;
  const layers = [
    createStackLayer('sculpt-layer', {
      name: 'Author sculpt — mix shapes to target height',
      brushRadius: 42,
      brushStrength: 0.80,
      blendMode: 2,
      noiseAmount: 0.30,
      noiseScale: 1.7,
      noiseHeight: 4.0,
      targetHeight: 86,
      multiplyStrength: 0.65,
      seed,
    }),
    createStackLayer('spline-landform', {
      name: 'SDF tunnel spline through the divide',
      mode: 4,
      width: 18,
      amount: 0,
      startElevation: 48,
      endElevation: 44,
      tunnelRadius: 8.0,
      tunnelDepth: 22,
      tunnelFollowSurface: 0,
      tunnelRoughness: 0.34,
      seed: 7201,
      points: [[-232, -62], [-122, -30], [0, 0], [122, 30], [232, 62]],
    }),
    createStackLayer('spline-landform', {
      name: 'Graded mountain road spline',
      mode: 3,
      width: 24,
      amount: 38,
      falloff: 0.52,
      sharpness: 0.82,
      noiseAmount: 0.16,
      noiseScale: 2.2,
      sediment: 0.34,
      startElevation: 56,
      endElevation: 39,
      flattenStrength: 0.90,
      bankHeight: 1.3,
      seed: 7202,
      points: [[-214, 154], [-112, 108], [0, 128], [112, 94], [214, 134]],
    }),
    createStackLayer('stratigraphic-beds', {
      name: 'Road/tunnel test hard strata',
      rockHardness: 0.62,
      strataStrength: 0.78,
      strataFreq: 9.0,
      strataDip: 4,
      strataStrike: 18,
      faultWeakness: 0.48,
      mineralIron: 0.44,
      mineralCopper: 0.18,
      mineralCarbonate: 0.36,
      mineralSalt: 0.14,
      mineralHorizon: 0.46,
      mineralHorizonWidth: 0.48,
      mineralBandStrength: 0.66,
      terraceCarve: 0.06,
      ledgeOverhang: 0.03,
      opacity: 1.0,
      seed,
    }),
    createStackLayer('base-landform', {
      name: 'Spline road/tunnel alpine test base',
      archetype: 0,
      seed,
      relief: 172,
      roughness: 0.72,
    }),
    createEnvironmentLayer({ sunAzimuth: 48, sunElevation: 24, params: climateDefaultsForLandscape('spline-road-tunnel-test') }),
  ];
  return {
    manualRecipe: true,
    expectedRelief: 172,
    expectedRoughness: 0.72,
    id: 'spline-road-tunnel-test',
    name: 'Spline Road + Tunnel Test',
    subtitle: 'New isolated test terrain: graded road spline plus an SDF spline tunnel passing through the alpine divide',
    category: 'Spline Authoring Test',
    terrainType: 0,
    palette: 2,
    fabricMode: 0,
    emitterMode: 'all-weather',
    particleCount: DEFAULT_EROSION_PARAMS.particleCount,
    particleInertia: 0.38,
    pipeFlowBlend: 0.50,
    sunAzimuth: 48,
    sunElevation: 24,
    showWater: true,
    showParticles: true,
    layers: () => structuredClone(layers).map((l) => ({ ...l, id: nextLayerId(l.type.slice(0, 4)) })),
  };
}

/**
 * Two isolated showcase terrains for the research frost-damage state and
 * authored non-repeating stratigraphic columns. They deliberately reuse the
 * proven Himalayan/Icelandic base generators, but every new Snow and strata
 * behaviour is contained in these new stacks rather than being retrofitted
 * into an old showcase terrain's identity.
 */
function buildFrostPittedRainbowRangeRecipe() {
  const seed = DEFAULT_EROSION_PARAMS.seed;
  const snowPreset = createSnowEffectPreset('segregation-pitting');
  const layers = [
    createStackLayer('rock-placement', {
      name: 'Frost-shattered summit blocks & boulders',
      density: 0.42,
      talusAffinity: 0.52,
      settleSteps: 0,
      seed,
    }),
    createStackLayer('aeolian-glacial', {
      name: 'Segregation-ice frost cracking & pitting',
      bakePasses: 42,
      snowline: 0.38,
      snowfallRate: 0.86,
      glacierStrength: 0.46,
      iceMeltRate: 0.30,
      snowEffectPreset: snowPreset.presetId,
      snowEffects: snowPreset.effects,
      ...snowPreset.globals,
      frostInitialDamage: 0.42,
      frostYearsPerStep: 2.6,
      windSpeed: 0,
      windAzimuth: 35,
      dryAeolianMode: 0,
    }),
    createStackLayer('thermal-talus', {
      name: 'Scree repose below frost-pitted walls',
      bakePasses: 0,
      talusAngle: 0.62,
      rockfallRate: 0.38,
    }),
    createStackLayer('chemical-weathering', {
      name: 'Bed chemistry: rust, copper & carbonate films',
      bakeSteps: 0,
      chemRate: 0.58,
      chemRust: 0.82,
      chemHydrolysis: 0.55,
      chemDissolution: 0.62,
      chemSalt: 0.38,
      chemVarnish: 0.28,
      chemCopper: 0.58,
    }),
    createStackLayer('hydraulic-erosion', {
      name: 'Snowmelt wash & light rain',
      bakeSteps: 0,
      rainIntensity: 0.22,
      erosionStrength: 0.50,
      sedimentCapacity: 0.68,
      depositionRate: 0.52,
      evaporation: 0.16,
      headwaterFlow: 0,
      lakeRetention: 0.28,
      oasisSpring: 0,
    }),
    createStackLayer('stratigraphic-beds', {
      name: 'Rainbow Andes authored bed column',
      rockHardness: 0.54,
      strataStrength: 0.96,
      strataFreq: 10.5,
      strataDip: 9,
      strataStrike: 24,
      faultWeakness: 0.58,
      strataColumnEnabled: true,
      strataColumnPreset: 'rainbow-andes',
      strataColumnBaseM: -10,
      strataColumnColorStrength: 0.96,
      strataBeds: createStrataColumnPreset('rainbow-andes'),
      mineralIron: 0.58,
      mineralCopper: 0.22,
      mineralCarbonate: 0.48,
      mineralSalt: 0.16,
      mineralHorizon: 0.48,
      mineralHorizonWidth: 0.62,
      mineralBandStrength: 0.25,
      terraceCarve: 0.20,
      ledgeOverhang: 0.08,
      opacity: 1.0,
      seed,
    }),
    createStackLayer('base-landform', {
      name: 'Rainbow frost range base',
      archetype: 7,
      seed,
      relief: 215,
      roughness: 0.70,
    }),
    createEnvironmentLayer({ sunAzimuth: 46, sunElevation: 25, params: climateDefaultsForLandscape('frost-pitted-rainbow-range') }),
  ];
  return {
    manualRecipe: true,
    expectedRelief: 215,
    expectedRoughness: 0.70,
    id: 'frost-pitted-rainbow-range',
    name: 'Frost-Pitted Rainbow Range',
    subtitle: 'New test terrain: sub-resolution segregation-ice cracks become visible pits across 14 non-repeating painted beds',
    category: 'Frost Weathering Test',
    terrainType: 7,
    palette: 2,
    fabricMode: 0,
    emitterMode: 'glacial-blizzard',
    particleCount: DEFAULT_EROSION_PARAMS.particleCount,
    particleInertia: 0.34,
    pipeFlowBlend: 0.50,
    sunAzimuth: 46,
    sunElevation: 25,
    showWater: true,
    showParticles: true,
    layers: () => structuredClone(layers).map((l) => ({ ...l, id: nextLayerId(l.type.slice(0, 4)) })),
  };
}

function buildNivationCirqueStrataRecipe() {
  const seed = DEFAULT_EROSION_PARAMS.seed;
  const snowPreset = createSnowEffectPreset('nivation-cirque');
  const layers = [
    createStackLayer('rock-placement', {
      name: 'Cirque-floor blocks & frost debris',
      density: 0.50,
      talusAffinity: 0.55,
      settleSteps: 0,
      seed,
    }),
    createStackLayer('aeolian-glacial', {
      name: 'Nivation, frost creep & snowpatch pitting',
      bakePasses: 48,
      snowline: 0.48,
      snowfallRate: 0.68,
      glacierStrength: 0.62,
      iceMeltRate: 0.38,
      snowEffectPreset: snowPreset.presetId,
      snowEffects: snowPreset.effects,
      ...snowPreset.globals,
      frostInitialDamage: 0.36,
      frostYearsPerStep: 2.2,
      windSpeed: 0,
      windAzimuth: -18,
      dryAeolianMode: 0,
    }),
    createStackLayer('thermal-talus', {
      name: 'Solifluction lobes & talus repose',
      bakePasses: 0,
      talusAngle: 0.58,
      rockfallRate: 0.48,
    }),
    createStackLayer('chemical-weathering', {
      name: 'Carbonate dissolution & mineral staining',
      bakeSteps: 0,
      chemRate: 0.62,
      chemRust: 0.66,
      chemHydrolysis: 0.58,
      chemDissolution: 0.78,
      chemSalt: 0.48,
      chemVarnish: 0.24,
      chemCopper: 0.46,
    }),
    createStackLayer('hydraulic-erosion', {
      name: 'Meltwater rills & alpine rain',
      bakeSteps: 0,
      rainIntensity: 0.30,
      erosionStrength: 0.56,
      sedimentCapacity: 0.72,
      depositionRate: 0.54,
      evaporation: 0.14,
      headwaterFlow: 0,
      lakeRetention: 0.34,
      oasisSpring: 0,
    }),
    createStackLayer('stratigraphic-beds', {
      name: 'Alpine limestone / shale authored column',
      rockHardness: 0.58,
      strataStrength: 0.94,
      strataFreq: 9.0,
      strataDip: 14,
      strataStrike: -18,
      faultWeakness: 0.52,
      strataColumnEnabled: true,
      strataColumnPreset: 'alpine-limestone-shale',
      strataColumnBaseM: -6,
      strataColumnColorStrength: 0.94,
      strataBeds: createStrataColumnPreset('alpine-limestone-shale'),
      mineralIron: 0.28,
      mineralCopper: 0.16,
      mineralCarbonate: 0.82,
      mineralSalt: 0.18,
      mineralHorizon: 0.46,
      mineralHorizonWidth: 0.60,
      mineralBandStrength: 0.25,
      terraceCarve: 0.22,
      ledgeOverhang: 0.07,
      opacity: 1.0,
      seed,
    }),
    createStackLayer('base-landform', {
      name: 'Nivation cirque strata base',
      archetype: 6,
      seed,
      relief: 178,
      roughness: 0.66,
    }),
    createEnvironmentLayer({ sunAzimuth: 38, sunElevation: 23, params: climateDefaultsForLandscape('nivation-cirque-strata') }),
  ];
  return {
    manualRecipe: true,
    expectedRelief: 178,
    expectedRoughness: 0.66,
    id: 'nivation-cirque-strata',
    name: 'Nivation Cirque & Painted Strata',
    subtitle: 'New test terrain: snowpatch-margin frost damage, solifluction export and an 11-bed limestone/shale column',
    category: 'Frost Weathering Test',
    terrainType: 6,
    palette: 1,
    fabricMode: 0,
    emitterMode: 'glacial-blizzard',
    particleCount: DEFAULT_EROSION_PARAMS.particleCount,
    particleInertia: 0.34,
    pipeFlowBlend: 0.52,
    sunAzimuth: 38,
    sunElevation: 23,
    showWater: true,
    showParticles: true,
    layers: () => structuredClone(layers).map((l) => ({ ...l, id: nextLayerId(l.type.slice(0, 4)) })),
  };
}

function buildGaeaAlpineTarnLakeRecipe() {
  const seed = DEFAULT_EROSION_PARAMS.seed;
  const layers = [
    createStackLayer('rock-placement', {
      name: 'Cirque cliff rockfall & tarn boulders',
      density: 0.54,
      talusAffinity: 0.62,
      settleSteps: 8,
      seed,
    }),
    createStackLayer('spline-landform', {
      name: '12-point meandering spillway outlet stream',
      mode: 2,
      width: 34,
      amount: 16,
      falloff: 0.58,
      sharpness: 0.66,
      noiseAmount: 0.24,
      noiseScale: 1.8,
      waterDepth: 1.4,
      sediment: 0.44,
      startElevation: 56,
      endElevation: 16,
      flattenStrength: 0.86,
      bankHeight: 1.1,
      seed: 7411,
      points: [
        [44, 14], [68, 26], [88, 12], [108, 28],
        [126, 46], [142, 34], [158, 56], [172, 78],
        [186, 68], [198, 92], [212, 114], [228, 136],
      ],
    }),
    createStackLayer('lake-basin', {
      name: 'Glacial cirque tarn lake & littoral shelf',
      centerX: -18,
      centerZ: -8,
      radiusX: 74,
      radiusZ: 56,
      azimuth: 22,
      depth: 26,
      waterLevelOffset: -1.2,
      shoreShelf: 0.66,
      irregularity: 0.46,
      spillwayNotch: 0.65,
      sedimentRim: 0.58,
      opacity: 1.0,
      seed: 7409,
    }),
    createStackLayer('rocky-outcrops', {
      name: 'Angular cleaved crags & scree pockets',
      cragHeight: 13.5,
      cragScale: 2.2,
      angularity: 0.86,
      slopeMin: 20,
      screeAmount: 0.58,
      bedTilt: 16,
      azimuth: 24,
      valleyTors: 0.08,
      opacity: 0.92,
      seed,
    }),
    createStackLayer('cliff-generator', {
      name: 'Stepped alpine headwall escarpments',
      terraceSteps: 4,
      steepness: 0.86,
      angularity: 0.82,
      slopeMin: 22,
      chuteDepth: 0.46,
      overhang: 0.44,
      talusToe: 0.60,
      featureScale: 1.9,
      opacity: 0.88,
      seed,
    }),
    createStackLayer('ridge-network', {
      name: 'Arête divide & dendritic spurs',
      combineMode: 0,
      amplitude: 38,
      scale: 1.65,
      sharpness: 0.78,
      spurDensity: 0.64,
      colDepth: 0.34,
      flankConcavity: 0.68,
      azimuth: -22,
      opacity: 0.76,
      seed,
    }),
    createStackLayer('mountain-noise', {
      name: 'Gaea folded mountain uplift',
      combineMode: 0,
      amplitude: 44,
      scale: 1.75,
      sharpness: 0.70,
      erosionFold: 0.62,
      ridgeMix: 0.56,
      azimuth: 18,
      screeAmount: 0.44,
      opacity: 0.72,
      seed,
    }),
    createStackLayer('stratigraphic-beds', {
      name: 'Alpine gneiss & ironstone beds',
      rockHardness: 0.68,
      strataStrength: 0.76,
      strataFreq: 9.5,
      strataDip: 12,
      strataStrike: -16,
      faultWeakness: 0.46,
      mineralIron: 0.52,
      mineralCopper: 0.18,
      mineralCarbonate: 0.34,
      mineralSalt: 0.12,
      mineralHorizon: 0.48,
      mineralHorizonWidth: 0.50,
      mineralBandStrength: 0.64,
      terraceCarve: 0.14,
      ledgeOverhang: 0.06,
      opacity: 1.0,
      seed,
    }),
    createStackLayer('base-landform', {
      name: 'Alpine watershed base',
      archetype: 0,
      seed,
      relief: 166,
      roughness: 0.68,
    }),
    createEnvironmentLayer({ sunAzimuth: 44, sunElevation: 26, params: climateDefaultsForLandscape('glacial-lake-watershed') }),
  ];
  return {
    manualRecipe: true,
    expectedRelief: 166,
    expectedRoughness: 0.68,
    id: 'gaea-alpine-tarn-lake',
    name: 'Gaea Alpine Crags + Tarn Lake',
    subtitle: 'Gaea Mountain Noise, Ridges, Cliff Generator & Rocky Crags feeding a cirque Lake Basin and 12-point outlet river',
    category: 'Gaea & Hydrology Showcase',
    terrainType: 0,
    palette: 2,
    fabricMode: 0,
    emitterMode: 'all-weather',
    particleCount: DEFAULT_EROSION_PARAMS.particleCount,
    particleInertia: 0.36,
    pipeFlowBlend: 0.50,
    sunAzimuth: 44,
    sunElevation: 26,
    showWater: true,
    showParticles: true,
    layers: () => structuredClone(layers).map((l) => ({ ...l, id: nextLayerId(l.type.slice(0, 4)) })),
  };
}

function buildSaharanArtesianOasisRecipe() {
  const seed = DEFAULT_EROSION_PARAMS.seed;
  const layers = [
    createStackLayer('vegetation-placement', {
      name: 'Oasis riparian palms & sedge tufts',
      species: 1,
      density: 0.72,
      bladeHeight: 1.45,
      slopeMax: 0.58,
      elevationMax: 0.58,
      patchiness: 0.46,
      windSway: 0.65,
      seed,
    }),
    createStackLayer('spline-landform', {
      name: '10-point cut-and-fill caravan road',
      mode: 3,
      width: 22,
      amount: 26,
      falloff: 0.54,
      sharpness: 0.78,
      noiseAmount: 0.10,
      noiseScale: 2.0,
      sediment: 0.42,
      startElevation: 48,
      endElevation: 28,
      flattenStrength: 0.92,
      bankHeight: 1.1,
      seed: 7502,
      points: [
        [-224, -148], [-174, -112], [-128, -86], [-86, -54], [-58, -18],
        [-52, 28], [-24, 68], [28, 92], [112, 118], [216, 152],
      ],
    }),
    createStackLayer('desert-oasis', {
      name: 'Perched artesian spring oasis & silt halo',
      centerX: 12,
      centerZ: -8,
      radius: 62,
      basinDepth: 18,
      poolFill: 0.78,
      shelterRim: 9.5,
      wetHaloWidth: 0.78,
      springDischarge: 0.65,
      irregularity: 0.42,
      windAzimuth: 35,
      opacity: 1.0,
      seed: 7501,
    }),
    createStackLayer('rock-stacks', {
      name: 'Angular sandstone tors & caprock pillars',
      height: 36,
      spacing: 88,
      radius: 13,
      tiers: 5,
      taper: 0.38,
      caprockOverhang: 0.48,
      lean: 0.18,
      leanAzimuth: 32,
      opacity: 0.85,
      seed,
      mask: { enabled: true, mode: 'elevation', min: 0.38, max: 0.95, feather: 0.18, invert: false },
    }),
    createStackLayer('stratigraphic-beds', {
      name: 'Nubian red sandstone & evaporite beds',
      rockHardness: 0.56,
      strataStrength: 0.82,
      strataFreq: 8.0,
      strataDip: 5,
      strataStrike: 24,
      faultWeakness: 0.42,
      mineralIron: 0.62,
      mineralCopper: 0.12,
      mineralCarbonate: 0.44,
      mineralSalt: 0.52,
      mineralHorizon: 0.42,
      mineralHorizonWidth: 0.52,
      mineralBandStrength: 0.68,
      terraceCarve: 0.16,
      ledgeOverhang: 0.08,
      opacity: 1.0,
      seed,
    }),
    createStackLayer('base-landform', {
      name: 'Erg dune & interdune depression base',
      archetype: 8,
      seed,
      relief: 118,
      roughness: 0.52,
    }),
    createEnvironmentLayer({ sunAzimuth: 58, sunElevation: 28, params: climateDefaultsForLandscape('desert-oasis-erg') }),
  ];
  return {
    manualRecipe: true,
    expectedRelief: 118,
    expectedRoughness: 0.52,
    id: 'saharan-artesian-oasis-basin',
    name: 'Artesian Oasis + Winding Caravan Road',
    subtitle: 'Dedicated Artesian Desert Oasis layer, 10-point cut-and-fill road, angular sandstone tors and spring-fed vegetation',
    category: 'Gaea & Hydrology Showcase',
    terrainType: 8,
    palette: 3,
    fabricMode: 0,
    emitterMode: 'desert-sandstorm',
    particleCount: DEFAULT_EROSION_PARAMS.particleCount,
    particleInertia: 0.40,
    pipeFlowBlend: 0.48,
    sunAzimuth: 58,
    sunElevation: 28,
    showWater: true,
    showParticles: true,
    layers: () => structuredClone(layers).map((l) => ({ ...l, id: nextLayerId(l.type.slice(0, 4)) })),
  };
}

/**
 * Curated showcase order across all 19 original `EROSION_LANDSCAPES` (`icelandic-highlands` default first!).
 */
const PRESET_ORDER = [
  'icelandic-highlands',
  'himalayan-karakoram',
  'grand-canyon',
  'studlagil-basalt',
  'fjadrargljufur-palagonite',
  'icelandic-green-highlands',
  'namib-erg-dunes',
  'sahara-tassili-erg',
  'desert-oasis-erg',
  'rugged-desert-crags',
  'yardang-wind-corridor',
  'glacial-lake-watershed',
  'lunar-mare-craters',
  'layered-plateau',
  'utah-wind-rain-canyon',
  'spline-canyon-river-test',
  'spline-road-tunnel-test',
  'gaea-alpine-tarn-lake',
  'saharan-artesian-oasis-basin',
  'frost-pitted-rainbow-range',
  'nivation-cirque-strata',
  'alpine-divide',
  'fault-escarpment',
  'volcanic-caldera',
  'badlands-domes',
  'coastal-massif',
];

export const TERRAIN_STACK_PRESETS = {};
for (const id of PRESET_ORDER) {
  if (id === 'utah-wind-rain-canyon') {
    TERRAIN_STACK_PRESETS[id] = buildUtahWindRainRecipe();
  } else if (id === 'spline-canyon-river-test') {
    TERRAIN_STACK_PRESETS[id] = buildSplineCanyonRiverRecipe();
  } else if (id === 'spline-road-tunnel-test') {
    TERRAIN_STACK_PRESETS[id] = buildSplineRoadTunnelRecipe();
  } else if (id === 'gaea-alpine-tarn-lake') {
    TERRAIN_STACK_PRESETS[id] = buildGaeaAlpineTarnLakeRecipe();
  } else if (id === 'saharan-artesian-oasis-basin') {
    TERRAIN_STACK_PRESETS[id] = buildSaharanArtesianOasisRecipe();
  } else if (id === 'frost-pitted-rainbow-range') {
    TERRAIN_STACK_PRESETS[id] = buildFrostPittedRainbowRangeRecipe();
  } else if (id === 'nivation-cirque-strata') {
    TERRAIN_STACK_PRESETS[id] = buildNivationCirqueStrataRecipe();
  } else if (EROSION_LANDSCAPES[id]) {
    TERRAIN_STACK_PRESETS[id] = buildStackRecipeFromLandscape(id);
  }
}
for (const id of Object.keys(EROSION_LANDSCAPES)) {
  if (!TERRAIN_STACK_PRESETS[id]) {
    TERRAIN_STACK_PRESETS[id] = buildStackRecipeFromLandscape(id);
  }
}
