/**
 * Frontier Studio — Non-Destructive Terrain Layer Stack, Inspector, Preset Shelf & 3D Spline Gizmo UI.
 *
 * Implements the Frontier Editor / ProjectZero 3-column dark studio architecture:
 *   - Left Dock: Layer Stack (newest at TOP, evaluated BOTTOM -> TOP)
 *   - Center Viewport: Derived Surface Shading bar, Authoring Tool bar, 3D Spline Gizmo, Preset Shelf
 *   - Right Dock: Contextual Layer Inspector (identity card, Reads/Writes channels, .tp-property-card controls, B&W Mask)
 */

import {
  BASE_ARCHETYPES,
  COMBINE_MODES,
  DERIVED_FABRICS,
  DERIVED_PALETTES,
  ENVIRONMENT_LAYER_NAME,
  ENVIRONMENT_LAYER_TYPE,
  LAYER_CATEGORIES,
  LAYER_OPERATION_SPECS,
  MAX_SPLINE_POINTS,
  SCULPT_BLEND_MODES,
  SPLINE_LANDFORM_MODES,
  SPLINE_MODES,
  TERRAIN_STACK_PRESETS,
  VEG_SPECIES,
} from './terrain-stack-presets.js';
import { DEFAULT_EROSION_PARAMS } from './erosion-presets.js';
import { enhanceAmountSliders, enhanceSwitches } from '../ui/StudioControls.js';
import {
  DEFAULT_FROST_PARAMS,
  MAX_SNOW_EFFECTS,
  SNOW_EFFECT_PRESETS,
  SNOW_EFFECT_TYPES,
  createSnowEffect,
  createSnowEffectPreset,
  deriveSnowEffectWeights,
  estimateAnnualFrostWindow,
  normalizeFrostParams,
  normalizeSnowEffect,
  normalizeSnowEffects,
  summarizeSnowEffects,
  snowmeltCycleAtStep,
  visibleCrackThresholdMm,
} from './frost-weathering.js';
import {
  MAX_STRATA_BEDS,
  STRATA_COLUMN_PRESETS,
  STRATA_LITHOLOGIES,
  applyLithologyDefaults,
  createStrataBed,
  createStrataColumnPreset,
  deriveBedProperties,
  normalizeHexColor,
  normalizeStrataColumn,
  strataColumnThickness,
} from './stratigraphic-column.js';
import {
  azimuthForWindFromBearing,
  buildWindVectorField,
  windCompassFromAzimuth,
  windSpeedColor,
} from './wind-field-debug.js';
import {
  CLIMATE_REGIMES,
  formatLatitude,
  normalizeSeasonalClimateParams,
  seasonalClimateAt,
} from './seasonal-climate.js';
import {
  WIND_SHIFT_MODES,
  describeWindRegime,
  effectiveWindAzimuthAtStep,
  normalizeWindShiftMode,
} from './wind-regime.js';
import {
  Brush,
  Check,
  Columns3,
  Contrast,
  Droplets,
  Eye,
  EyeOff,
  FlaskConical,
  Gem,
  Layers,
  Lock,
  Mountain,
  MountainSnow,
  MoveHorizontal,
  Paintbrush,
  Rainbow,
  Route,
  Snowflake,
  Spline,
  Sprout,
  Sun,
  Triangle,
  Wind,
} from 'lucide';

// Lucide icon set (ISC-licensed lucide package). Glyphs are stroke icons
// rendered with `currentColor`, so the ICON ITSELF carries the per-type
// colour (see LAYER_ICON_COLORS below) — icon tiles stay neutral.
function lucideIcon(iconNode, size = 18) {
  const body = (iconNode || [])
    .map(([tag, attrs]) => `<${tag} ${Object.entries(attrs || {}).map(([k, v]) => `${k}="${v}"`).join(' ')}/>`)
    .join('');
  return `<svg viewBox="0 0 24 24" width="${size}" height="${size}" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${body}</svg>`;
}

const SVG_ICONS = {
  mountain: lucideIcon(Mountain),
  ridge: lucideIcon(MountainSnow),
  strata: lucideIcon(Layers),
  stacks: lucideIcon(Columns3),
  cliff: lucideIcon(MoveHorizontal),
  cave: lucideIcon(Rainbow),
  sculpt: lucideIcon(Paintbrush),
  river: lucideIcon(Route),
  droplet: lucideIcon(Droplets),
  talus: lucideIcon(Triangle),
  snow: lucideIcon(Snowflake),
  wind: lucideIcon(Wind),
  flask: lucideIcon(FlaskConical),
  spline: lucideIcon(Spline),
  boulders: lucideIcon(Gem),
  grass: lucideIcon(Sprout),
  sun: lucideIcon(Sun),
  lock: lucideIcon(Lock, 16),
  eyeOpen: lucideIcon(Eye, 16),
  eyeClosed: lucideIcon(EyeOff, 16),
  mask: lucideIcon(Contrast, 16),
  check: lucideIcon(Check, 16),
  brush: lucideIcon(Brush),
};

// Per-layer-type icon colours (Exhibits outliner style: every node carries
// its own saturated glyph colour, not one muted colour per category).
const LAYER_ICON_COLORS = {
  'base-landform': '#f0b35e',
  'mountain-ridges': '#ff8a5c',
  'mountain-noise': '#f59e0b',
  'ridge-network': '#fb7185',
  'rocky-outcrops': '#d97706',
  'cliff-generator': '#f87171',
  'stratigraphic-beds': '#e8d06a',
  'rock-stacks': '#d98cb3',
  'cliff-push-pull': '#ff6b6b',
  'sdf-caves': '#9d7bff',
  'sculpt-layer': '#94a3ff',
  'river-incision': '#4aa8ff',
  'hydraulic-erosion': '#38bdf8',
  'thermal-talus': '#e08a5a',
  'aeolian-glacial': '#a8e6ff',
  'wind-erosion': '#5eead4',
  'chemical-weathering': '#e879f9',
  'spline-feature': '#67e8f9',
  'spline-landform': '#818cf8',
  'lake-basin': '#0ea5e9',
  'desert-oasis': '#34d399',
  'rock-placement': '#d6a05a',
  'vegetation-placement': '#6ee76a',
  [ENVIRONMENT_LAYER_TYPE]: '#ffd166',
};

function layerIconColor(type, cat) {
  return LAYER_ICON_COLORS[type] || (cat && cat.color) || '#8b9b82';
}

const OP_DESCRIPTIONS = {
  'base-landform': 'Constructs foundational bedrock elevation and base rock competency as the first terrain pass, immediately above the locked Environment layer.',
  'mountain-ridges': 'Adds, subtracts, or blends procedural ridges, plateaus, fault escarpments, dunes, or craters.',
  'mountain-noise': 'Gaea-style folded mountain generator: couples Voronoi-ridged crests with slope-guided dendritic gully folding and scree chutes.',
  'ridge-network': 'Gaea-style polygonal divide arêtes with orthogonal herringbone flank spurs, col saddles, and concave scree-swept slopes.',
  'rocky-outcrops': 'Gaea-style angular cleaved Voronoi bedrock crags and inter-crag scree pockets gated by hillside steepness.',
  'cliff-generator': 'Gaea-style stepped escarpment & terrace generator with planar rock-joint facets, vertical couloir chutes, 3D caprock overhangs, and cliff-toe talus.',
  'stratigraphic-beds': 'Carves dipping sedimentary bed benches and differential hardness. An optional authored measured section gives every bed its own thickness, flat colour, hardness, porosity, fracture, clay and Fe/Cu/carbonate/salt makeup; the sequence clamps at its ends instead of repeating.',
  'rock-stacks': 'Synthesizes jointed angular boulder tors, polygonal rock columns, and stepped caprock pillars.',
  'cliff-push-pull': 'Applies 3D lateral planar-faceted crag buttress and ledge overhang vector displacement on steep faces.',
  'sdf-caves': 'Subtracts real 3D volumetric karst cave tunnels and cliff-base undercuts using boolean SDFs.',
  'sculpt-layer': 'Stores non-destructive 3D brush edits separately from procedural layers, then Add/Subtract, Multiply/Scale, Mix-to-Target or Max/Min them with seeded noise; also carve/fill 3D caves.',
  'river-incision': 'Integrates catchment drainage discharge and carves bedrock gorges weighted by 3D rock hardness.',
  'hydraulic-erosion': 'Simulates rainfall runoff detachment, sediment capacity transport, and piedmont alluvial fans.',
  'thermal-talus': 'Spalls over-steepened cliff faces and relaxes loose scree cones to their angle of repose.',
  'aeolian-glacial': 'Simulates snowpack, firn/glacier flow and a stackable frost-weathering process list. Segregation ice and freeze-thaw first store sub-resolution cracks; only coalesced, resolution-visible damage becomes pits/spalls, then frost creep, meltwash, avalanches and till export the debris.',
  'wind-erosion': 'Simulates turbulent wind flow around rock: sand saltation, deflation, lee deposition, differential sandstone abrasion, basal undercutting, yardang streamlining and warped vertical fluting for fins and spires.',
  'chemical-weathering': 'Reacts water, snowmelt and evaporation with mineral veins: iron rust, copper patina, hydrolysis to clay, dissolution, salt bursting and desert varnish.',
  'spline-feature': 'Legacy interactive spline (up to 16 points) for canyon gorges, graded roads, or lake basins.',
  'spline-landform': 'Smooth 16-point 3D Catmull-Rom spline: add an angular mountain ridge, carve a terraced canyon, grade an asymmetric meandering river or cut-and-fill hillside road, or bore an SDF tunnel.',
  'lake-basin': 'Excavates a hydrostatic alpine/valley lake basin calibrated to the local terrain sill, with wave-cut littoral shelves, beach silt, optional outlet spillway notch, and flat water fill.',
  'desert-oasis': 'Carves an artesian interdune spring bowl with a perched water pool, wind-sheltered crescent rim, and capillary-moist fertile silt halo.',
  'rock-placement': 'Distributes gravity-settled 3D loose boulders across cliff toes and crowns (distinct from bedrock landforms).',
  'vegetation-placement': 'Instantiates up to 44,000 real 3D multi-blade grass tufts masked by slope, elevation, snow, and water.',
  [ENVIRONMENT_LAYER_TYPE]: 'The locked final stack layer. It owns scene sun direction plus the global location-based seasonal climate: signed latitude/hemisphere, climate regime, calendar pacing, simulated run length and year-to-year variability. It is live state, not a terrain-generating pass, and it never enables a paused Rain/Snow/Wind/Chemical process by itself.',
};

const pctFmt = (v) => `${Math.round(v * 100)}%`;

const FROST_GLOBAL_CONTROLS = [
  { key: 'frostYearsPerStep', label: 'Simulated Years / Step', min: 0.05, max: 8, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)} yr`, help: 'Physical time represented by one simulation step. Frost damage is intrinsically slow; this is acceleration, not an erosion-strength fudge.' },
  { key: 'frostMeanTempC', label: 'Mean Annual Rock Temp', min: -18, max: 4, step: 0.1, fmt: (v) => `${Number(v).toFixed(1)}°C`, help: 'Reference rock temperature before elevation, aspect, snow-cover and seasonal/diurnal skin-depth corrections.' },
  { key: 'frostSeasonalAmplitudeC', label: 'Seasonal Temp Amplitude', min: 0, max: 28, step: 0.25, fmt: (v) => `±${Number(v).toFixed(1)}°C`, help: 'Annual surface-temperature swing. At crack depth it is attenuated and phase-lagged by thermal diffusion.' },
  { key: 'frostCrackDepthM', label: 'Active Crack Depth', min: 0.05, max: 2.5, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)} m`, help: 'Depth of the frost-cracking horizon. Shallow layers feel diurnal cycles; deeper layers mainly feel slow seasonal segregation-ice growth.' },
  { key: 'frostThermalDiffusivity', label: 'Thermal Diffusivity', min: 0.15, max: 2.5, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)} ×10⁻⁶ m²/s`, help: 'Controls thermal skin depth. Dense crystalline rock conducts seasonal waves deeper than porous, clay-rich rock.' },
  { key: 'frostMoisture', label: 'Pore-Water Supply', min: 0, max: 1, step: 0.02, fmt: pctFmt, help: 'Available water from snowmelt, wet sediment and climate before porosity/permeability turn it into stored pore saturation.' },
  { key: 'frostSaturationPeak', label: 'Peak-Cracking Saturation', min: 0.45, max: 0.95, step: 0.01, fmt: pctFmt, help: 'Research-based optimum for segregation-ice cracking. Very dry pores cannot feed lenses; completely filled pores leave less accommodation space.' },
  { key: 'frostSnowInsulationM', label: 'Snow Insulation Depth', min: 0.05, max: 1.5, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)} m`, help: 'Snow thickness at which the rock surface is strongly thermally buffered. Thin snow can wet rock; deep snow often suppresses freeze-thaw cycling.' },
  { key: 'frostInitialDamage', label: 'Initial Sub-Resolution Damage', min: 0, max: 0.85, step: 0.01, fmt: pctFmt, help: 'Pre-aged invisible crack/porosity state used when the stack is evaluated. It does not carve geometry by itself.' },
  { key: 'frostDamageThreshold', label: 'Visible Damage Threshold', min: 0.1, max: 0.95, step: 0.01, fmt: pctFmt, help: 'Damage must reach this fraction before any bed material can detach as a pit or spall.' },
  { key: 'frostVisibleCrackMm', label: 'Requested Visible Crack Width', min: 0.5, max: 80, step: 0.5, fmt: (v) => `${Number(v).toFixed(1)} mm`, help: 'Aperture the user considers visible. The solver also requires the crack to be representable at the active terrain-cell size.' },
  { key: 'frostPitDepthM', label: 'Pit / Spall Depth per Release', min: 0.02, max: 0.6, step: 0.01, fmt: (v) => `${Number(v).toFixed(2)} m`, help: 'Cell-equivalent bedrock thickness detached after both damage and aperture thresholds are crossed.' },
  { key: 'frostPitScaleCells', label: 'Pit Clustering Scale', min: 0.7, max: 4, step: 0.1, fmt: (v) => `${Number(v).toFixed(1)} cells`, help: 'Spatial scale of coalescing pitted patches. Bed porosity/fracture still decides where clusters can mature.' },
  { key: 'frostDebrisRetention', label: 'Local Debris Retention', min: 0, max: 1, step: 0.02, fmt: pctFmt, help: 'Fraction of frost-loosened equivalent initially left in the source cell. The rest becomes available to meltwash/creep/avalanche/till export.' },
];

const SNOW_EFFECT_PARAM_CONTROLS = {
  'segregation-ice': [
    { key: 'windowLowC', label: 'Cold Window Edge', min: -16, max: -4, step: 0.25, fmt: (v) => `${Number(v).toFixed(1)}°C` },
    { key: 'windowHighC', label: 'Warm Window Edge', min: -7, max: -0.5, step: 0.25, fmt: (v) => `${Number(v).toFixed(1)}°C` },
    { key: 'gradientSensitivity', label: 'Thermal-Gradient Sensitivity', min: 0, max: 2.5, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)}×` },
    { key: 'depthScale', label: 'Segregation Depth Scale', min: 0.25, max: 2.5, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)}×` },
  ],
  'volumetric-freeze-thaw': [
    { key: 'cycleSensitivity', label: 'Zero-Crossing Sensitivity', min: 0, max: 2.5, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)}×` },
    { key: 'saturationFloor', label: 'Expansion Saturation Floor', min: 0.25, max: 0.95, step: 0.01, fmt: pctFmt },
    { key: 'diurnalAmplitudeC', label: 'Diurnal Temp Amplitude', min: 0, max: 14, step: 0.25, fmt: (v) => `±${Number(v).toFixed(1)}°C` },
  ],
  'nivation-snowpatch': [
    { key: 'optimalSnowDepthM', label: 'Most Active Snow Depth', min: 0.03, max: 2.5, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)} m` },
    { key: 'snowDepthWidthM', label: 'Active Depth Width', min: 0.08, max: 2.5, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)} m` },
    { key: 'meltwashExport', label: 'Meltwash Export', min: 0, max: 1, step: 0.02, fmt: pctFmt },
  ],
  'frost-creep-solifluction': [
    { key: 'creepRate', label: 'Saturated Creep Rate', min: 0, max: 2, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)}×` },
    { key: 'minSlope', label: 'Minimum Creep Slope', min: 0, max: 0.8, step: 0.01, fmt: (v) => `${(Math.atan(v) * 180 / Math.PI).toFixed(1)}°` },
    { key: 'coarseMobility', label: 'Coarse-Clast Mobility', min: 0, max: 1, step: 0.02, fmt: pctFmt },
  ],
  'avalanche-scour': [
    { key: 'slopeThreshold', label: 'Avalanche Release Slope', min: 0.1, max: 1.6, step: 0.02, fmt: (v) => `${(Math.atan(v) * 180 / Math.PI).toFixed(1)}°` },
    { key: 'entrainment', label: 'Loose-Debris Entrainment', min: 0, max: 1.5, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)}×` },
    { key: 'scourRate', label: 'Exposed-Bedrock Scour', min: 0, max: 2, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)}×` },
  ],
  'glacier-plucking': [
    { key: 'plucking', label: 'Joint-Block Plucking', min: 0, max: 2, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)}×` },
    { key: 'abrasion', label: 'Basal Abrasion / Rock Flour', min: 0, max: 2, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)}×` },
    { key: 'jointSensitivity', label: 'Joint & Thin-Bed Sensitivity', min: 0, max: 2.5, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)}×` },
  ],
  'legacy-instant-nivation': [
    { key: 'localRetention', label: 'Legacy Local Retention', min: 0, max: 1, step: 0.02, fmt: pctFmt },
    { key: 'marginRate', label: 'Legacy Margin Rate', min: 0, max: 2, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)}×` },
  ],
  'snowmelt-runoff': [
    { key: 'cyclePeriodSteps', label: 'Fallback Cycle Period (Seasonal Climate Off)', min: 16, max: 720, step: 2, fmt: (v) => `${Math.round(v)} steps` },
    { key: 'cyclePhase', label: 'Fallback Start Phase (0 cold · 0.5 warm)', min: 0, max: 1, step: 0.01, fmt: (v) => `${Math.round(v * 100)}%` },
    { key: 'warmMeltBoost', label: 'Warm-Phase Melt Boost', min: 0, max: 4, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)}×` },
    { key: 'runoffErosion', label: 'Meltwater Channel Cutting', min: 0, max: 2, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)}×` },
    { key: 'sedimentCapacity', label: 'Meltwater Sediment Capacity', min: 0, max: 2, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)}×` },
    { key: 'infiltration', label: 'Meltwater Infiltration', min: 0, max: 1, step: 0.02, fmt: pctFmt },
  ],
};

const STRATA_BED_CONTROLS = [
  { key: 'thicknessM', label: 'Thickness', min: 0.4, max: 90, step: 0.5, fmt: (v) => `${Number(v).toFixed(1)} m` },
  { key: 'hardness', label: 'Rock Hardness / Competence', min: 0.04, max: 0.98, step: 0.01, fmt: pctFmt },
  { key: 'porosity', label: 'Porosity', min: 0.003, max: 0.55, step: 0.005, fmt: pctFmt },
  { key: 'fracture', label: 'Jointing / Fracture Density', min: 0, max: 1, step: 0.02, fmt: pctFmt },
  { key: 'clay', label: 'Clay Fraction', min: 0, max: 1, step: 0.02, fmt: pctFmt },
  { key: 'iron', label: 'Iron (Fe)', min: 0, max: 1, step: 0.02, fmt: pctFmt },
  { key: 'copper', label: 'Copper (Cu)', min: 0, max: 1, step: 0.02, fmt: pctFmt },
  { key: 'carbonate', label: 'Carbonate', min: 0, max: 1, step: 0.02, fmt: pctFmt },
  { key: 'salt', label: 'Salt / Evaporite', min: 0, max: 1, step: 0.02, fmt: pctFmt },
];

// Complete Inspector Control Schema per Layer Type (1:1 aligned with canonical generators & conservative physics)
const INSPECTOR_SCHEMAS = {
  'base-landform': {
    selects: [
      { key: 'archetype', label: 'Landform Generator', options: Object.values(BASE_ARCHETYPES).map((o) => ({ value: o.id, label: o.label })) },
    ],
    sliders: [
      { key: 'relief', label: 'Vertical Relief', min: 55, max: 260, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'roughness', label: 'Tectonic Roughness', min: 0.05, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
    ],
    hasSeed: true,
  },
  'mountain-ridges': {
    selects: [
      {
        key: 'generator',
        label: 'Landform Generator',
        options: [
          { value: 0, label: 'Sharp Arête Ridges & Spurs' },
          { value: 1, label: 'Flat-Topped Mesa / Plateau' },
          { value: 2, label: 'Tectonic Fault Escarpment' },
          { value: 3, label: 'Asymmetric Aeolian Dunes' },
          { value: 4, label: 'Impact Crater Bowl & Rim' },
        ],
      },
      { key: 'combineMode', label: 'Combine Operation', options: Object.values(COMBINE_MODES).map((o) => ({ value: o.id, label: o.label })) },
    ],
    sliders: [
      { key: 'amplitude', label: 'Elevation Amplitude', min: 8, max: 160, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'scale', label: 'Spatial Frequency', min: 0.6, max: 5.0, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)}×` },
      { key: 'sharpness', label: 'Crest / Rim Sharpness', min: 0.05, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'azimuth', label: 'Strike / Wind Azimuth', min: -180, max: 180, step: 1, fmt: (v) => `${Math.round(v)}°` },
      { key: 'opacity', label: 'Layer Blend Weight', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
    ],
    hasSeed: true,
  },
  'mountain-noise': {
    selects: [
      { key: 'combineMode', label: 'Combine Operation', options: Object.values(COMBINE_MODES).map((o) => ({ value: o.id, label: o.label })) },
    ],
    sliders: [
      { key: 'amplitude', label: 'Mountain Relief', min: 10, max: 180, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'scale', label: 'Spatial Frequency', min: 0.5, max: 4.5, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)}×` },
      { key: 'sharpness', label: 'Arête / Peak Sharpness', min: 0.05, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'erosionFold', label: 'Slope Gully Folding (Gaea)', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'ridgeMix', label: 'Voronoi Crest Faceting', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'screeAmount', label: 'Gully Scree Apron', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'azimuth', label: 'Tectonic Strike Azimuth', min: -180, max: 180, step: 1, fmt: (v) => `${Math.round(v)}°` },
      { key: 'opacity', label: 'Layer Blend Weight', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
    ],
    hasSeed: true,
  },
  'ridge-network': {
    selects: [
      { key: 'combineMode', label: 'Combine Operation', options: Object.values(COMBINE_MODES).map((o) => ({ value: o.id, label: o.label })) },
    ],
    sliders: [
      { key: 'amplitude', label: 'Divide Prominence', min: 8, max: 165, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'scale', label: 'Divide Network Scale', min: 0.5, max: 4.5, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)}×` },
      { key: 'sharpness', label: 'Crest Razor Sharpness', min: 0.05, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'spurDensity', label: 'Dendritic Flank Spurs', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'colDepth', label: 'Col / Saddle Notching', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'flankConcavity', label: 'Flank Sweep Concavity', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'azimuth', label: 'Divide Strike Azimuth', min: -180, max: 180, step: 1, fmt: (v) => `${Math.round(v)}°` },
      { key: 'opacity', label: 'Layer Blend Weight', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
    ],
    hasSeed: true,
  },
  'rocky-outcrops': {
    selects: [],
    sliders: [
      { key: 'cragHeight', label: 'Outcrop Crag Relief', min: 2, max: 42, step: 0.5, fmt: (v) => `${Number(v).toFixed(1)} m` },
      { key: 'cragScale', label: 'Block & Joint Frequency', min: 0.6, max: 5.0, step: 0.1, fmt: (v) => `${Number(v).toFixed(1)}×` },
      { key: 'angularity', label: 'Cleaved Planar Angularity', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'slopeMin', label: 'Hillside Emergence Slope', min: 4, max: 52, step: 1, fmt: (v) => `${Math.round(v)}°` },
      { key: 'screeAmount', label: 'Inter-Crag Scree Pockets', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'bedTilt', label: 'Joint / Bedding Tilt', min: -45, max: 45, step: 1, fmt: (v) => `${Math.round(v)}°` },
      { key: 'azimuth', label: 'Strike Orientation', min: -180, max: 180, step: 1, fmt: (v) => `${Math.round(v)}°` },
      { key: 'valleyTors', label: 'Flatland / Summit Tors', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'opacity', label: 'Layer Blend Weight', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
    ],
    hasSeed: true,
  },
  'cliff-generator': {
    selects: [],
    sliders: [
      { key: 'terraceSteps', label: 'Escarpment Cliff Steps', min: 2, max: 12, step: 1, fmt: (v) => `${Math.round(v)} steps` },
      { key: 'steepness', label: 'Cliff Riser Verticality', min: 0.1, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'angularity', label: 'Planar Joint Faceting', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'slopeMin', label: 'Minimum Hillside Slope', min: 6, max: 52, step: 1, fmt: (v) => `${Math.round(v)}°` },
      { key: 'chuteDepth', label: 'Vertical Couloir Chutes', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'overhang', label: '3D Caprock Lip Overhang', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'talusToe', label: 'Cliff-Toe Talus Apron', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'featureScale', label: 'Joint & Couloir Scale', min: 0.6, max: 4.5, step: 0.1, fmt: (v) => `${Number(v).toFixed(1)}×` },
      { key: 'opacity', label: 'Layer Blend Weight', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
    ],
    hasSeed: true,
  },
  'stratigraphic-beds': {
    selects: [],
    sliders: [
      { key: 'rockHardness', label: 'Base Rock Hardness', min: 0.08, max: 0.96, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'strataStrength', label: '3D Strata Contrast', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'strataFreq', label: 'Legacy Periodic Bed Count', min: 2, max: 24, step: 0.5, fmt: (v) => `${Number(v).toFixed(1)} cycles` },
      { key: 'strataDip', label: 'Bedding Dip Angle', min: 0, max: 45, step: 1, fmt: (v) => `${Math.round(v)}°` },
      { key: 'strataStrike', label: 'Strike Azimuth', min: -180, max: 180, step: 1, fmt: (v) => `${Math.round(v)}°` },
      { key: 'faultWeakness', label: 'Fracture & Fault Weakness', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'strataColumnBaseM', label: 'Authored Column Base Elevation', min: -120, max: 180, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'strataColumnColorStrength', label: 'Authored Bed Colour Strength', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'mineralIron', label: 'Legacy Horizon Iron (Fe → Rust)', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'mineralCopper', label: 'Copper Veins (Cu → Patina)', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'mineralCarbonate', label: 'Carbonate / Soluble Beds', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'mineralSalt', label: 'Salt / Evaporite Beds', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'mineralHorizon', label: 'Mineral Horizon Centre', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}% height` },
      { key: 'mineralHorizonWidth', label: 'Mineral Horizon Thickness', min: 0.05, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'mineralBandStrength', label: 'Bed-Banded Mineralisation', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'terraceCarve', label: 'Geometric Terrace Carve', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'ledgeOverhang', label: '3D Caprock Ledge Overhang', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'opacity', label: 'Layer Blend Weight', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
    ],
    hasSeed: true,
  },
  'rock-stacks': {
    selects: [],
    sliders: [
      { key: 'height', label: 'Stack / Tor Height', min: 12, max: 115, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'spacing', label: 'Cluster Spacing', min: 32, max: 140, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'radius', label: 'Column Base Radius', min: 6, max: 38, step: 0.5, fmt: (v) => `${Number(v).toFixed(1)} m` },
      { key: 'tiers', label: 'Stacked Bedding Tiers', min: 2, max: 9, step: 1, fmt: (v) => `${Math.round(v)} tiers` },
      { key: 'taper', label: 'Pillar Taper', min: 0.05, max: 0.9, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'caprockOverhang', label: '3D Caprock Overhang', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'lean', label: '3D Column Lean', min: 0.0, max: 0.85, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'leanAzimuth', label: 'Lean Direction', min: -180, max: 180, step: 1, fmt: (v) => `${Math.round(v)}°` },
      { key: 'opacity', label: 'Layer Blend Weight', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
    ],
    hasSeed: true,
  },
  'cliff-push-pull': {
    selects: [],
    sliders: [
      { key: 'displacement', label: '3D Push–Pull Amplitude', min: 0, max: 26, step: 0.5, fmt: (v) => `${Number(v).toFixed(1)} m` },
      { key: 'featureScale', label: 'Buttress & Crag Scale', min: 0.8, max: 5.5, step: 0.1, fmt: (v) => `${Number(v).toFixed(1)}×` },
      { key: 'slopeThreshold', label: 'Minimum Cliff Slope', min: 14, max: 54, step: 1, fmt: (v) => `${Math.round(v)}°` },
      { key: 'ledgeStrength', label: 'Stepped Ledge Ribs', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'overhangBias', label: 'Outward Overhang Bias', min: -0.4, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'verticalCrease', label: 'Vertical Fracture Chutes', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'opacity', label: 'Layer Blend Weight', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
    ],
    hasSeed: true,
  },
  'sdf-caves': {
    selects: [],
    sliders: [
      { key: 'carveDepth', label: '3D Cave Carve Depth', min: 4, max: 36, step: 0.5, fmt: (v) => `${Number(v).toFixed(1)} m` },
      { key: 'undercutStrength', label: 'Cliff Undercut Strength', min: 0.0, max: 1.2, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'archHeight', label: 'Portal Arch Height', min: 6, max: 30, step: 0.5, fmt: (v) => `${Number(v).toFixed(1)} m` },
      { key: 'tunnelScale', label: 'Cave Network Frequency', min: 0.8, max: 4.5, step: 0.1, fmt: (v) => `${Number(v).toFixed(1)}×` },
      { key: 'elevationCenter', label: 'Cliff Band Elevation', min: 0.1, max: 0.85, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'elevationBand', label: 'Vertical Horizon Span', min: 0.1, max: 0.65, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'slopeMin', label: 'Cliff Face Slope Gate', min: 14, max: 48, step: 1, fmt: (v) => `${Math.round(v)}°` },
      { key: 'ribRoughness', label: 'Bedding Rib Roughness', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'opacity', label: 'Cave Carve Strength', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
    ],
    hasSeed: true,
  },
  'sculpt-layer': {
    selects: [
      { key: 'blendMode', label: 'Sculpt / Noise Blend Operation', options: Object.values(SCULPT_BLEND_MODES).map((o) => ({ value: o.id, label: o.label })) },
    ],
    sliders: [
      { key: 'brushRadius', label: 'Sculpt / Cave Brush Radius', min: 8, max: 96, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'brushStrength', label: 'Stamp Strength', min: 0.1, max: 1.5, step: 0.05, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'caveHeightOffset', label: '3D Cave Depth Below Surface', min: 2, max: 24, step: 0.5, fmt: (v) => `${Number(v).toFixed(1)} m` },
      { key: 'noiseAmount', label: 'Procedural Noise Mixed Into Sculpt', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'noiseScale', label: 'Sculpt Noise Frequency', min: 0.3, max: 4.0, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)}×` },
      { key: 'noiseHeight', label: 'Noise Relief Inside Sculpt Footprint', min: 0, max: 30, step: 0.5, fmt: (v) => `${Number(v).toFixed(1)} m` },
      { key: 'targetHeight', label: 'Mix / Max-Min Target Height', min: 2, max: 220, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'multiplyStrength', label: 'Multiply Height Strength', min: 0.0, max: 1.5, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'opacity', label: 'Layer Contribution', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
    ],
    hasSeed: true,
  },
  'river-incision': {
    selects: [],
    sliders: [
      { key: 'iterations', label: 'Drainage Solver Passes', min: 2, max: 80, step: 1, fmt: (v) => `${Math.round(v)} passes` },
      { key: 'incisionStrength', label: 'Gorge Cutting Power', min: 0.05, max: 1.4, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'headwaterRain', label: 'Catchment Runoff Supply', min: 0.05, max: 1.3, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'basinPooling', label: 'Channel & Basin Water Fill', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'hardnessRespect', label: '3D Strata Hardness Respect', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
    ],
    hasSeed: false,
  },
  'hydraulic-erosion': {
    selects: [],
    sliders: [
      { key: 'bakeSteps', label: 'Stack Bake Steps', min: 0, max: 120, step: 2, fmt: (v) => (v > 0 ? `${Math.round(v)} steps` : 'Live sim') },
      { key: 'rainIntensity', label: 'Rainfall Intensity', min: 0.0, max: 1.2, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'erosionStrength', label: 'Hydraulic Detachment', min: 0.05, max: 1.3, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'sedimentCapacity', label: 'Sediment Capacity', min: 0.05, max: 1.4, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'depositionRate', label: 'Alluvial Deposition', min: 0.05, max: 1.2, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'evaporation', label: 'Evaporation Rate', min: 0.01, max: 0.60, step: 0.01, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'headwaterFlow', label: 'River / Spring Discharge', min: 0.0, max: 1.2, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'lakeRetention', label: 'Basin Lake Retention', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
    ],
    hasSeed: false,
  },
  'thermal-talus': {
    selects: [],
    sliders: [
      { key: 'bakePasses', label: 'Stack Bake Passes', min: 0, max: 80, step: 2, fmt: (v) => (v > 0 ? `${Math.round(v)} passes` : 'Live sim') },
      { key: 'talusAngle', label: 'Angle of Repose', min: 0.30, max: 1.10, step: 0.02, fmt: (v) => `${Math.round((Math.atan(v) * 180) / Math.PI)}°` },
      { key: 'rockfallRate', label: 'Cliff Spalling & Rockfall', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
    ],
    hasSeed: false,
  },
  'chemical-weathering': {
    selects: [],
    sliders: [
      { key: 'bakeSteps', label: 'Stack Bake Steps', min: 0, max: 160, step: 2, fmt: (v) => (v > 0 ? `${Math.round(v)} steps` : 'Live sim') },
      { key: 'chemRate', label: 'Overall Reaction Rate', min: 0.0, max: 1.5, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'chemRust', label: 'Iron Oxidation (Rust)', min: 0.0, max: 1.5, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'chemHydrolysis', label: 'Hydrolysis to Clay', min: 0.0, max: 1.5, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'chemDissolution', label: 'Carbonate Dissolution', min: 0.0, max: 1.5, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'chemSalt', label: 'Salt Efflorescence & Bursting', min: 0.0, max: 1.5, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'chemVarnish', label: 'Desert Varnish', min: 0.0, max: 1.5, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'chemCopper', label: 'Copper Patina', min: 0.0, max: 1.5, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
    ],
    hasSeed: false,
  },
  'wind-erosion': {
    selects: [
      {
        key: 'windShiftMode',
        label: 'Direction Over Time',
        options: WIND_SHIFT_MODES.map((mode) => ({ value: mode.value, label: mode.label })),
      },
    ],
    sliders: [
      { key: 'bakeSteps', label: 'Stack Bake Steps', min: 0, max: 160, step: 2, fmt: (v) => (v > 0 ? `${Math.round(v)} steps` : 'Live sim') },
      { key: 'windAzimuth', label: 'Prevailing Wind Direction', min: -180, max: 180, step: 1, fmt: (v) => `${Math.round(v)}°` },
      { key: 'windShiftRange', label: 'Wind-Rose Spread / Sector Range', min: 0, max: 180, step: 5, fmt: (v) => `${Math.round(v)}°` },
      { key: 'windShiftPeriod', label: 'Weather-Pattern Cycle Length', min: 240, max: 3600, step: 10, fmt: (v) => `${Math.round(v)} steps` },
      { key: 'windSpeed', label: 'Wind Speed', min: 0.0, max: 1.4, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'windErosionRate', label: 'Overall Wind Erosion Rate', min: 0.0, max: 1.5, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'windAbrasion', label: 'Rock Sandblasting / Abrasion', min: 0.0, max: 1.5, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'windDeflation', label: 'Loose-Sand Deflation Pickup', min: 0.0, max: 1.5, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'windDeposition', label: 'Lee / Slipface Deposition', min: 0.0, max: 1.5, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'windTurbulence', label: 'Turbulent Direction Wander', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'windGustiness', label: 'Gust Strength Variation', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'windSandSupply', label: 'Abrasive Sand Supply', min: 0.0, max: 1.2, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'windCurtainHeight', label: 'Saltation Curtain / Undercut Height', min: 0.05, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}% relief` },
      { key: 'windStreamlining', label: 'Wrap Around Terrain & Lee Shelter', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'windFluting', label: 'Vertical Fluting / Fin Separation', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'dryAeolianMode', label: 'Arid Saltation Regime', min: 0.0, max: 1.0, step: 0.05, fmt: (v) => `${Math.round(v * 100)}%` },
    ],
    hasSeed: false,
  },
  'aeolian-glacial': {
    selects: [],
    sliders: [
      { key: 'bakePasses', label: 'Stack Bake Passes', min: 0, max: 64, step: 2, fmt: (v) => (v > 0 ? `${Math.round(v)} passes` : 'Live sim') },
      { key: 'snowline', label: 'Snowline / Melt Equilibrium', min: 0.05, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'snowfallRate', label: 'Snowfall Rate', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'glacierStrength', label: 'Glacier Ice Strength', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'iceMeltRate', label: 'Glacial Meltwater Recharge', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
    ],
    hasSeed: false,
  },
  'spline-feature': {
    selects: [
      { key: 'mode', label: 'Spline Corridor Operation', options: Object.values(SPLINE_MODES).map((o) => ({ value: o.id, label: o.label })) },
    ],
    sliders: [
      { key: 'width', label: 'Corridor / Basin Width', min: 10, max: 130, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'depth', label: 'Incision / Excavation Depth', min: 4, max: 110, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'wallSteepness', label: 'Wall / Cut Slope Steepness', min: 0.1, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'meanderAmp', label: 'Natural Wall Perturbation', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'waterFill', label: 'River / Lake Water Level', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'bankFalloff', label: 'Embankment Grading Width', min: 0.1, max: 1.2, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'opacity', label: 'Layer Blend Weight', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
    ],
    hasSeed: false,
  },
  'spline-landform': {
    selects: [
      { key: 'mode', label: 'Spline Landform Operation', options: Object.values(SPLINE_LANDFORM_MODES).map((o) => ({ value: o.id, label: o.label })) },
      {
        key: 'tunnelFollowSurface',
        label: 'Tunnel Vertical Path',
        options: [
          { value: 0, label: 'Fixed Start → End Elevation Grade' },
          { value: 1, label: 'Follow Surface at Tunnel Depth' },
        ],
      },
    ],
    sliders: [
      { key: 'width', label: 'Influence Width', min: 6, max: 180, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'amount', label: 'Raise / Cut / Road Amount', min: 0, max: 140, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'falloff', label: 'Edge Falloff Softness', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'sharpness', label: 'Crest / Wall Sharpness', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'noiseAmount', label: 'Natural Noise Variation', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'noiseScale', label: 'Noise Frequency', min: 0.3, max: 4.0, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)}×` },
      { key: 'startElevation', label: 'Start Elevation (River/Road/Tunnel)', min: 2, max: 220, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'endElevation', label: 'End Elevation (River/Road/Tunnel)', min: 2, max: 220, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'flattenStrength', label: 'Grade Flatten Strength', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'waterDepth', label: 'River Water Depth', min: 0.0, max: 5.0, step: 0.1, fmt: (v) => `${Number(v).toFixed(1)} m` },
      { key: 'sediment', label: 'Channel / Road Sediment', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'bankHeight', label: 'Bank / Shoulder Height', min: 0.0, max: 6.0, step: 0.1, fmt: (v) => `${Number(v).toFixed(1)} m` },
      { key: 'tunnelRadius', label: 'SDF Tunnel Radius', min: 2, max: 18, step: 0.5, fmt: (v) => `${Number(v).toFixed(1)} m` },
      { key: 'tunnelDepth', label: 'Tunnel Depth Below Surface (Follow Mode)', min: 6, max: 70, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'tunnelRoughness', label: 'Tunnel Wall Roughness', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'opacity', label: 'Layer Blend Weight', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
    ],
    hasSeed: true,
  },
  'lake-basin': {
    selects: [],
    sliders: [
      { key: 'centerX', label: 'Basin Center X (East–West)', min: -210, max: 210, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'centerZ', label: 'Basin Center Z (North–South)', min: -210, max: 210, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'radiusX', label: 'Major Semi-Axis Radius', min: 18, max: 190, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'radiusZ', label: 'Minor Semi-Axis Radius', min: 18, max: 190, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'azimuth', label: 'Basin Orientation Azimuth', min: -180, max: 180, step: 1, fmt: (v) => `${Math.round(v)}°` },
      { key: 'depth', label: 'Maximum Bathymetric Depth', min: 3, max: 85, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'waterLevelOffset', label: 'Water Level vs. Rim Sill', min: -14, max: 8, step: 0.2, fmt: (v) => `${Number(v) >= 0 ? '+' : ''}${Number(v).toFixed(1)} m` },
      { key: 'shoreShelf', label: 'Wave-Cut Littoral Shelf', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'irregularity', label: 'Shoreline Cove Irregularity', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'spillwayNotch', label: 'Outlet Spillway Sill Notch', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'sedimentRim', label: 'Beach Gravel & Lacustrine Silt', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'opacity', label: 'Layer Blend Weight', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
    ],
    hasSeed: true,
  },
  'desert-oasis': {
    selects: [],
    sliders: [
      { key: 'centerX', label: 'Oasis Center X (East–West)', min: -210, max: 210, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'centerZ', label: 'Oasis Center Z (North–South)', min: -210, max: 210, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'radius', label: 'Spring Depression Radius', min: 16, max: 145, step: 1, fmt: (v) => `${Math.round(v)} m` },
      { key: 'basinDepth', label: 'Deflation Hollow Depth', min: 2, max: 42, step: 0.5, fmt: (v) => `${Number(v).toFixed(1)} m` },
      { key: 'poolFill', label: 'Artesian Pool Water Table', min: 0.05, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'shelterRim', label: 'Shelter Crescent Rim Height', min: 0.0, max: 26, step: 0.5, fmt: (v) => `${Number(v).toFixed(1)} m` },
      { key: 'wetHaloWidth', label: 'Capillary Moist Silt Halo', min: 0.15, max: 1.4, step: 0.02, fmt: pctFmt },
      { key: 'springDischarge', label: 'Live Artesian Recharge', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'irregularity', label: 'Organic Spring Irregularity', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
      { key: 'windAzimuth', label: 'Shelter Dune Wind Azimuth', min: -180, max: 180, step: 1, fmt: (v) => `${Math.round(v)}°` },
      { key: 'opacity', label: 'Layer Blend Weight', min: 0.0, max: 1.0, step: 0.02, fmt: pctFmt },
    ],
    hasSeed: true,
  },
  'rock-placement': {
    selects: [],
    sliders: [
      { key: 'density', label: '3D Boulder Scatter Density', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'talusAffinity', label: 'Cliff-Toe & Rockfall Rate', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'settleSteps', label: 'Pre-Settle Tumble Steps', min: 0, max: 56, step: 2, fmt: (v) => `${Math.round(v)} steps` },
    ],
    hasSeed: true,
  },
  'vegetation-placement': {
    selects: [
      { key: 'species', label: '3D Plant Species', options: Object.values(VEG_SPECIES).map((o) => ({ value: o.id, label: o.label })) },
    ],
    sliders: [
      { key: 'density', label: '3D Tuft Scatter Density', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'bladeHeight', label: '3D Blade Height', min: 0.4, max: 2.4, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)} m` },
      { key: 'slopeMax', label: 'Maximum Slope Cutoff', min: 0.20, max: 1.30, step: 0.02, fmt: (v) => `${Math.round((Math.atan(v) * 180) / Math.PI)}°` },
      { key: 'elevationMax', label: 'Treeline / Meadow Ceiling', min: 0.15, max: 0.98, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'patchiness', label: 'Meadow Clumping', min: 0.0, max: 1.0, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
      { key: 'windSway', label: 'Wind Gust Sway', min: 0.0, max: 1.4, step: 0.05, fmt: (v) => `${Math.round(v * 100)}%` },
    ],
    hasSeed: true,
  },
  [ENVIRONMENT_LAYER_TYPE]: {
    selects: [
      {
        key: 'seasonalClimateMode',
        label: 'Seasonal Climate',
        options: [
          { value: 1, label: 'Location seasons — latitude + climate regime' },
          { value: 0, label: 'Off — constant annual forcing' },
        ],
      },
      {
        key: 'climateRegime',
        label: 'Climate Regime / Location Type',
        options: CLIMATE_REGIMES.map((regime) => ({ value: regime.code, label: regime.label })),
      },
    ],
    sliders: [
      { key: 'sunAzimuth', label: 'Sun Azimuth', min: -180, max: 180, step: 1, fmt: (v) => `${Math.round(v)}°`, liveParam: true },
      { key: 'sunElevation', label: 'Sun Elevation', min: 4, max: 85, step: 1, fmt: (v) => `${Math.round(v)}°`, liveParam: true },
      { key: 'climateLatitudeDeg', label: 'Latitude (negative = Southern Hemisphere)', min: -82, max: 82, step: 0.5, fmt: (v) => formatLatitude(v), liveParam: true },
      { key: 'climateStartDayOfYear', label: 'Simulation Start Day of Year', min: 1, max: 365, step: 1, fmt: (v) => `Day ${Math.round(v)} / 365`, liveParam: true },
      { key: 'climateStepsPerYear', label: 'Simulation Steps per Climate Year', min: 24, max: 1460, step: 12, fmt: (v) => `${Math.round(v)} steps/yr`, liveParam: true },
      { key: 'climateYearsToSimulate', label: 'Years per Simulate Run (0 = continuous)', min: 0, max: 100, step: 1, fmt: (v) => (Number(v) <= 0 ? 'Continuous' : `${Math.round(v)} years`), liveParam: true },
      { key: 'climateInterannualVariability', label: 'Year-to-Year Wet/Dry + Warm/Cold Variation', min: 0, max: 1, step: 0.02, fmt: pctFmt, liveParam: true },
      { key: 'climatePrecipitationScale', label: 'Seasonal Precipitation Scale', min: 0, max: 3, step: 0.05, fmt: (v) => `${Number(v).toFixed(2)}×`, liveParam: true },
      { key: 'climateTemperatureOffsetC', label: 'Location Temperature Offset (Altitude / Local Adjustment)', min: -15, max: 15, step: 0.25, fmt: (v) => `${Number(v) > 0 ? '+' : ''}${Number(v).toFixed(1)}°C`, liveParam: true },
    ],
    hasSeed: false,
    liveOnly: true,
  },
};

function isSplineAuthoringLayer(layer) {
  return layer?.type === 'spline-feature' || layer?.type === 'spline-landform';
}

function isEnvironmentLayer(layer) {
  return layer?.type === ENVIRONMENT_LAYER_TYPE;
}

function isPositionLockedLayer(layer, spec = LAYER_OPERATION_SPECS[layer?.type]) {
  // Deliberately narrower than the legacy `locked` flag: Base/Spline layers
  // are delete/duplicate/drag-safe but retain arrow reordering. Environment
  // alone is locked to a stack position.
  return Boolean(spec?.systemLayer || spec?.lockedPosition);
}

function summarizeLayer(layer) {
  const spec = LAYER_OPERATION_SPECS[layer.type];
  if (spec && typeof spec.summary === 'function') {
    return spec.summary(layer.params);
  }
  return '';
}

export class ErosionLayerStackUI {
  constructor(engine, opts = {}) {
    this.engine = engine;
    this.pipeline = engine.pipeline;
    this.onPresetChanged = opts.onPresetChanged || null;
    this.searchQuery = '';
    this.addMenuOpen = false;
    this.shelfOpen = false;
    this.shadingCardOpen = false;
    this.dragFromIdx = null;
    this.draggingSplinePt = -1;
    this.selectedSplinePt = -1;
    this.draggingSplineAxis = null;
    this._splineDrag = null;
    this.showWindDebug = false;
    this.selectedSnowEffectId = null;
    this.selectedStrataBedId = null;
    this._windField = null;
    this._windFieldKey = '';
    this._windFieldBuiltAt = 0;

    // Bind callbacks from engine
    this.engine.onStackMutated = () => {
      this.renderStackList();
      this.renderInspector();
    };
    this.engine.onFrameRendered = () => {
      this.updateSplineGizmoOverlay();
      this.updateWindDebugOverlay();
    };

    this._cacheDom();
    this.pipeline.addSimulationStateListener(() => {
      this.renderSimulationPanel();
      this._syncHeaderSimButton();
    });
    this._bindGlobalEvents();
    this.renderAll();
  }

  _cacheDom() {
    this.elList = document.getElementById('tp-layer-list');
    this.elCountBadge = document.getElementById('tp-layer-count');
    this.elStatVisible = document.getElementById('tp-stat-visible');
    this.elStatHidden = document.getElementById('tp-stat-hidden');
    this.elSearch = document.getElementById('tp-layer-search-input');
    this.elAddBtn = document.getElementById('tp-btn-add-layer');
    this.elAddMenu = document.getElementById('tp-add-layer-menu');
    this.elInspector = document.getElementById('tp-inspector-content');
    this.elPresetGrid = document.getElementById('cb-preset-grid');
    this.elShelfDrawer = document.getElementById('cb-shelf-drawer');
    this.elShelfToggle = document.getElementById('cb-btn-toggle');
    this.elDocTitle = document.getElementById('tp-doc-title');
    this.elSceneCaption = document.getElementById('tp-scene-caption');
    this.elShadingSelect = document.getElementById('tp-select-shading');
    this.elPaletteSelect = document.getElementById('tp-select-palette');
    this.elAuthorTools = document.getElementById('tp-author-tools');
    this.elSplineSvg = document.getElementById('spline-gizmo-svg');
    this.elWindCanvas = document.getElementById('wind-debug-canvas');
    this.elSimPanel = document.getElementById('tp-sim-panel');
    this.elHeaderPlay = document.getElementById('btn-er-play');
  }

  _bindGlobalEvents() {
    if (this.elSearch) {
      this.elSearch.addEventListener('input', () => {
        this.searchQuery = this.elSearch.value.trim().toLowerCase();
        this.renderStackList();
      });
    }

    if (this.elAddBtn && this.elAddMenu) {
      this._buildAddLayerMenu();
      this.elAddBtn.addEventListener('click', (e) => {
        e.stopPropagation();
        this.addMenuOpen = !this.addMenuOpen;
        this.elAddMenu.hidden = !this.addMenuOpen;
      });
      document.addEventListener('click', (e) => {
        if (this.addMenuOpen && !this.elAddMenu.contains(e.target) && e.target !== this.elAddBtn) {
          this.addMenuOpen = false;
          this.elAddMenu.hidden = true;
        }
      });
    }

    if (this.elShelfToggle && this.elShelfDrawer) {
      const syncShelf = () => {
        this.elShelfDrawer.hidden = !this.shelfOpen;
        this.elShelfToggle.classList.toggle('open', this.shelfOpen);
        this.elShelfToggle.setAttribute?.('aria-expanded', this.shelfOpen ? 'true' : 'false');
      };
      syncShelf();
      this.elShelfToggle.addEventListener('click', () => {
        this.shelfOpen = !this.shelfOpen;
        syncShelf();
      });
    }

    if (this.elShadingSelect) {
      this.elShadingSelect.addEventListener('change', () => {
        this.pipeline.shadingMode = Number(this.elShadingSelect.value);
        this.renderInspector();
      });
    }

    if (this.elPaletteSelect) {
      this.elPaletteSelect.innerHTML = Object.values(DERIVED_PALETTES).map(
        (p) => `<option value="${p.id}">${p.label}</option>`
      ).join('');
      this.elPaletteSelect.value = String(this.pipeline.paletteId);
      this.elPaletteSelect.addEventListener('change', () => {
        this.pipeline.paletteId = Number(this.elPaletteSelect.value);
      });
    }

    if (this.elAuthorTools) {
      this.elAuthorTools.querySelectorAll('button[data-tool]').forEach((btn) => {
        btn.addEventListener('click', () => {
          this.setAuthorTool(btn.dataset.tool);
        });
      });
    }

    this._bindSplineGizmoEvents();
  }

  setAuthorTool(tool) {
    this.pipeline.authorTool = tool;
    if (this.elAuthorTools) {
      this.elAuthorTools.querySelectorAll('button[data-tool]').forEach((b) => {
        b.classList.toggle('active', b.dataset.tool === tool);
      });
    }
    // Automatically select or highlight relevant layer when entering spline or sculpt tool
    if (tool === 'spline-edit') {
      const sel = this.pipeline.getSelectedLayer();
      if (!isSplineAuthoringLayer(sel)) {
        const sp = this.pipeline.layers.find((l) => isSplineAuthoringLayer(l));
        if (sp) {
          this.pipeline.selectedLayerId = sp.id;
          this.renderStackList();
          this.renderInspector();
        }
      }
    } else if (tool.startsWith('sculpt-') || tool.startsWith('cave-')) {
      const sel = this.pipeline.getSelectedLayer();
      if (!sel || sel.type !== 'sculpt-layer') {
        const sc = this.pipeline.layers.find((l) => l.type === 'sculpt-layer');
        if (sc) {
          this.pipeline.selectedLayerId = sc.id;
          this.renderStackList();
          this.renderInspector();
        }
      }
    }
    this.updateSplineGizmoOverlay();
  }

  _buildAddLayerMenu() {
    const groups = [
      { id: 'landform', title: '1. Landform & 3D SDF Geometry' },
      { id: 'simulation', title: '2. Simulation & Erosion Passes' },
      { id: 'spline', title: '3. Spatial Spline Features' },
      { id: 'rocks', title: '4. Loose Rock Placement' },
      { id: 'vegetation', title: '5. 3D Vegetation Placement' },
    ];
    let html = '';
    for (const g of groups) {
      const specs = Object.values(LAYER_OPERATION_SPECS).filter(
        (s) => s.category === g.id && s.type !== 'base-landform' && s.addable !== false
      );
      if (!specs.length) continue;
      html += `<div class="tp-menu-group"><div class="tp-menu-heading">${g.title}</div>`;
      for (const s of specs) {
        const cat = LAYER_CATEGORIES[s.category];
        const desc = OP_DESCRIPTIONS[s.type] || '';
        html += `
          <button type="button" class="tp-menu-item" data-add-type="${s.type}">
            <span class="tp-menu-dot" style="--cat-accent:${layerIconColor(s.type, cat)}">${SVG_ICONS[s.glyph] || SVG_ICONS.mountain}</span>
            <span class="tp-menu-item-body">
              <strong>${s.defaultName}</strong>
              <small>${desc}</small>
            </span>
          </button>`;
      }
      html += `</div>`;
    }
    this.elAddMenu.innerHTML = html;
    this.elAddMenu.querySelectorAll('button[data-add-type]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const type = btn.dataset.addType;
        this.addMenuOpen = false;
        this.elAddMenu.hidden = true;
        this.pipeline.addLayer(type);
        if (type === 'sculpt-layer') this.setAuthorTool('sculpt-add');
        else if (type === 'spline-feature' || type === 'spline-landform') this.setAuthorTool('spline-edit');
        this.renderAll();
      });
    });
  }

  renderAll() {
    if (this.elDocTitle) this.elDocTitle.textContent = this.pipeline.projectTitle;
    if (this.elPaletteSelect) this.elPaletteSelect.value = String(this.pipeline.paletteId);
    if (this.elShadingSelect) this.elShadingSelect.value = String(this.pipeline.shadingMode);
    this.renderSimulationPanel();
    this._syncHeaderSimButton();
    this.renderStackList();
    this.renderInspector();
    this.renderPresetShelf();
    this.updateSceneCaption();
    this.updateSplineGizmoOverlay();
  }

  updateSceneCaption() {
    if (!this.elSceneCaption) return;
    const terrainLayers = this.pipeline.layers.filter((layer) => !isEnvironmentLayer(layer));
    const enabledCount = terrainLayers.filter((layer) => layer.enabled).length;
    const caveTris = Math.round(this.pipeline.caveSystem.vertexCount / 3);
    this.elSceneCaption.textContent =
      `${enabledCount}/${terrainLayers.length} passes · ${this.pipeline.lastEvalMs.toFixed(1)} ms` +
      (caveTris > 0 ? ` · ${caveTris.toLocaleString()} cave tris` : '');
  }

  _syncHeaderSimButton() {
    const btn = this.elHeaderPlay || document.getElementById('btn-er-play');
    if (!btn) return;
    const state = this.pipeline.getSimulationControlState();
    btn.textContent = state.running ? '⏸ Pause' : '▶ Simulate';
    btn.classList.toggle('active-play', state.running);
  }

  renderSimulationPanel() {
    if (!this.elSimPanel) return;
    const state = this.pipeline.getSimulationControlState();
    const runningNames = state.processes.filter((proc) => proc.playing).map((proc) => proc.short).join(' + ');
    const solo = state.processes.find((proc) => proc.id === state.soloProcess);
    const status = state.running
      ? `Running: ${runningNames || 'no process selected'}`
      : (state.pauseReason || 'Paused — press Simulate');
    const masterLabel = state.running
      ? '⏸ Pause simulation'
      : (state.mode === 'solo' ? `▶ Simulate ${solo ? solo.short : 'season'} only` : '▶ Simulate enabled seasons');
    const climateState = state.climate?.climate || null;
    const climateRun = state.climate?.run || null;
    const seasonalOn = state.seasonalEnabled !== undefined ? Boolean(state.seasonalEnabled) : Boolean(climateState?.enabled);
    const climateLine = seasonalOn && climateState?.enabled
      ? `Climate Year ${climateState.year} · ${climateState.monthName} ${climateState.dayOfMonth} · ${climateState.seasonName} · ${climateState.regime.label} · ${formatLatitude(climateState.climateLatitudeDeg ?? climateState.params?.climateLatitudeDeg ?? 0)}${climateRun?.targetYears > 0 ? ` · ${climateRun.runElapsedYears.toFixed(1)}/${climateRun.targetYears} yr this run` : ' · continuous'}`
      : 'Seasonal simulation OFF — enabled processes use steady non-seasonal forcing.';

    const chips = state.processes.map((proc) => {
      const armed = state.mode === 'solo' ? proc.solo : proc.enabled;
      const cls = [
        'tp-sim-chip',
        armed && proc.available ? 'active' : '',
        proc.playing ? 'playing' : '',
      ].join(' ');
      const title = !proc.available
        ? `${proc.label}: add/enable its simulation layer first`
        : state.mode === 'solo'
          ? `Simulate ${proc.label} only (other seasons frozen)`
          : `${proc.enabled ? 'Pause' : 'Arm'} ${proc.label} in simultaneous mode`;
      const mark = proc.playing ? '⏸' : (state.mode === 'all' ? (proc.enabled ? '●' : '○') : '▶');
      return `<button type="button" class="${cls}" data-sim-process="${proc.id}" ${proc.available ? '' : 'disabled'} title="${title}">${mark} ${proc.short}</button>`;
    }).join('');

    this.elSimPanel.innerHTML = `
      <div class="tp-sim-head">
        <span class="tp-sim-title">SIMULATION SEASONS</span>
        <span class="tp-sim-live-dot ${state.running ? 'running' : ''}" title="${state.running ? 'Simulation running' : 'Simulation paused'}"></span>
      </div>
      <div class="tp-sim-seasonal-row">
        <span class="tp-sim-seasonal-label">Seasonal Cycle</span>
        <button type="button" class="tp-sim-seasonal-toggle ${seasonalOn ? 'active' : 'off'}" data-sim-seasonal-toggle="1" aria-pressed="${seasonalOn ? 'true' : 'false'}" title="Toggle location-based seasonal climate cycling ON or OFF (steady constant forcing)">
          ${seasonalOn ? 'ON · Location Seasons' : 'OFF · Constant Forcing'}
        </button>
      </div>
      <div class="tp-sim-status ${state.running ? 'running' : ''}">${this._esc(status)}</div>
      <div class="tp-sim-note">${this._esc(climateLine)}</div>
      <div class="tp-sim-modes" role="group" aria-label="Simulation playback mode">
        <button type="button" data-sim-mode="solo" class="${state.mode === 'solo' ? 'active' : ''}" title="Run exactly one season at a time: snow, then rain, then wind, then chemical">One at a time</button>
        <button type="button" data-sim-mode="all" class="${state.mode === 'all' ? 'active' : ''}" title="Run every armed season together">Simultaneous</button>
      </div>
      <div class="tp-sim-chips">${chips}</div>
      <button type="button" class="tp-sim-master" data-sim-master="1">${masterLabel}</button>
      <div class="tp-sim-note">Stack edits auto-pause. Turn Seasonal Cycle OFF for continuous constant precipitation/erosion, or keep ON for latitude/calendar seasons.</div>
    `;

    this.elSimPanel.querySelector('[data-sim-seasonal-toggle]')?.addEventListener('click', () => {
      if (typeof this.pipeline.toggleSeasonalClimate === 'function') {
        this.pipeline.toggleSeasonalClimate();
      }
      this.renderSimulationPanel();
      this.renderStackList();
      this.renderInspector();
    });

    this.elSimPanel.querySelectorAll('[data-sim-mode]').forEach((btn) => {
      btn.addEventListener('click', () => {
        this.pipeline.setSimPlaybackMode(btn.dataset.simMode);
        this.renderSimulationPanel();
        this._syncHeaderSimButton();
        this.renderInspector();
      });
    });
    this.elSimPanel.querySelectorAll('[data-sim-process]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const id = btn.dataset.simProcess;
        const fresh = this.pipeline.getSimulationControlState();
        if (fresh.mode === 'solo') {
          this.pipeline.toggleSimulation(id);
        } else {
          const proc = fresh.processes.find((x) => x.id === id);
          this.pipeline.setSimulationProcessEnabled(id, !(proc && proc.enabled));
        }
        this.renderSimulationPanel();
        this._syncHeaderSimButton();
        this.renderInspector();
      });
    });
    const master = this.elSimPanel.querySelector('[data-sim-master]');
    if (master) {
      master.addEventListener('click', () => {
        if (this.pipeline.liveSimRunning) {
          this.pipeline.pauseSimulation('');
        } else if (this.pipeline.simPlaybackMode === 'solo') {
          const selected = this.pipeline.getSelectedLayer();
          const procs = this.pipeline.simulationProcessesForLayer(selected);
          const fresh = this.pipeline.getSimulationControlState();
          const first = (procs.includes(fresh.soloProcess) && fresh.processes.some((proc) => proc.id === fresh.soloProcess && proc.available)) ? fresh.soloProcess : procs.find((id) => fresh.processes.some((proc) => proc.id === id && proc.available));
          if (first) this.pipeline.startSimulation(first);
          else this.pipeline.startSimulation(null);
        } else {
          this.pipeline.startSimulation(null);
        }
        this.renderSimulationPanel();
        this._syncHeaderSimButton();
        this.renderInspector();
      });
    }
  }

  renderStackList() {
    if (!this.elList) return;
    const layers = this.pipeline.layers;
    const visCount = layers.filter((l) => l.enabled).length;
    const hidCount = layers.length - visCount;
    const lastTerrainIdx = layers.reduce(
      (last, layer, layerIdx) => (isEnvironmentLayer(layer) ? last : layerIdx),
      -1,
    );

    if (this.elCountBadge) this.elCountBadge.textContent = String(layers.length);
    if (this.elStatVisible) this.elStatVisible.textContent = String(visCount);
    if (this.elStatHidden) this.elStatHidden.textContent = String(hidCount);

    let html = '';
    layers.forEach((layer, idx) => {
      const spec = LAYER_OPERATION_SPECS[layer.type] || LAYER_OPERATION_SPECS['mountain-ridges'];
      const cat = LAYER_CATEGORIES[spec.category] || LAYER_CATEGORIES.landform;
      const summary = summarizeLayer(layer);
      const specLabel = spec.defaultName || layer.type;
      if (
        this.searchQuery &&
        !layer.name.toLowerCase().includes(this.searchQuery) &&
        !specLabel.toLowerCase().includes(this.searchQuery) &&
        !summary.toLowerCase().includes(this.searchQuery)
      ) {
        return;
      }

      const isSelected = layer.id === this.pipeline.selectedLayerId;
      const environmentLayer = isEnvironmentLayer(layer);
      const positionLocked = isPositionLockedLayer(layer, spec);
      // Environment is render state, not a terrain evaluation pass, so it
      // does not consume a numbered step. Terrain keeps 1 = bottom/base.
      const evalOrderNum = layers.slice(idx).filter((candidate) => !isEnvironmentLayer(candidate)).length;
      const stepLabel = environmentLayer ? 'LAST' : `#${evalOrderNum}`;
      const iconSvg = SVG_ICONS[spec.glyph] || SVG_ICONS.mountain;
      const hasMask = !environmentLayer && layer.mask && layer.mask.enabled;
      const supportsMask = !environmentLayer && layer.type !== 'base-landform';
      const canMoveDown = !positionLocked && idx < lastTerrainIdx;

      html += `
        <div class="tp-layer-item-wrap" data-layer-id="${layer.id}" data-idx="${idx}">
          <div class="tp-layer-row ${isSelected ? 'selected' : ''} ${layer.enabled ? '' : 'muted'} ${environmentLayer ? 'environment locked-last' : ''}"
               draggable="${layer.locked || positionLocked ? 'false' : 'true'}"
               data-select-id="${layer.id}" data-idx="${idx}">
            <span class="tp-drag-grip ${positionLocked ? 'locked' : ''}" title="${positionLocked ? 'Environment is locked as the last layer' : 'Drag to reorder pipeline stage (Bottom → Top)'}">${positionLocked ? SVG_ICONS.lock : '⋮⋮'}</span>
            <span class="tp-layer-icon" data-layer-type="${layer.type}" style="--cat-accent:${layerIconColor(layer.type, cat)}">
              ${iconSvg}
            </span>
            <div class="tp-layer-meta">
              <div class="tp-layer-title-line">
                <span class="tp-layer-name">${this._esc(layer.name)}</span>
                <span class="tp-step-pill ${environmentLayer ? 'locked-last' : ''}" title="${environmentLayer ? 'Locked last layer — lighting only, not a terrain pass' : `Evaluated at step #${evalOrderNum} from bottom`}">${stepLabel}</span>
              </div>
              <div class="tp-layer-sub">${this._esc(summary)}</div>
            </div>
            <span class="tp-layer-status ${environmentLayer ? 'locked' : (layer.enabled ? 'on' : 'off')}" title="${environmentLayer ? 'Locked last layer' : (layer.enabled ? 'Pass active' : 'Pass muted')}">${environmentLayer ? SVG_ICONS.lock : SVG_ICONS.check}</span>
            <div class="tp-layer-controls">
              ${
                environmentLayer
                  ? `<span class="tp-lock-chip" title="Environment is always on and locked as the last layer">${SVG_ICONS.lock}<span>Locked</span></span>`
                  : `
                    ${
                      supportsMask
                        ? `<button type="button" class="tp-icon-btn ${hasMask ? 'mask-on' : ''}" data-toggle-mask="${layer.id}" title="${hasMask ? 'Disable B&W spatial mask' : 'Add B&W spatial mask'}">${SVG_ICONS.mask}</button>`
                        : ''
                    }
                    <button type="button" class="tp-icon-btn ${layer.enabled ? 'vis-on' : 'vis-off'}" data-toggle-vis="${layer.id}" title="${layer.enabled ? 'Mute pass' : 'Enable pass'}">
                      ${layer.enabled ? SVG_ICONS.eyeOpen : SVG_ICONS.eyeClosed}
                    </button>
                  `
              }
            </div>
          </div>
          ${
            hasMask
              ? `<div class="tp-mask-row ${isSelected ? 'selected' : ''}" data-select-mask="${layer.id}">
                   <span class="tp-mask-chip">B&amp;W MASK</span>
                   <span class="tp-mask-desc">${layer.mask.mode.toUpperCase()} [${Math.round(layer.mask.min * 100)}–${Math.round(layer.mask.max * 100)}%]${layer.mask.invert ? ' · INV' : ''}</span>
                   <button type="button" class="tp-mask-eye ${this.pipeline.shadingMode === 5 && isSelected ? 'active' : ''}" data-preview-mask="${layer.id}" title="Inspect B&W Mask on 3D Terrain">B&amp;W</button>
                 </div>`
              : ''
          }
          ${
            isSelected
              ? (environmentLayer
                ? `<div class="tp-layer-actions environment-locked">
                     <span class="tp-locked-tag">${SVG_ICONS.lock} Locked · Last layer</span>
                   </div>`
                : `<div class="tp-layer-actions">
                     <button type="button" data-move-up="${idx}" ${idx === 0 ? 'disabled' : ''} title="Move later in pipeline (up)">↑ Up</button>
                     <button type="button" data-move-down="${idx}" ${canMoveDown ? '' : 'disabled'} title="${canMoveDown ? 'Move earlier in pipeline (down)' : 'Environment is locked below this layer'}">↓ Down</button>
                     ${!layer.locked ? `<button type="button" data-dup="${layer.id}" title="Duplicate layer">Duplicate</button>` : ''}
                     ${!layer.locked ? `<button type="button" class="danger" data-del="${layer.id}" title="Delete layer">Delete</button>` : '<span class="tp-locked-tag">Foundation</span>'}
                   </div>`)
              : ''
          }
        </div>
      `;
    });

    this.elList.innerHTML = html;

    // Bind row selection, visibility, mask, reorder & drag-and-drop
    this.elList.querySelectorAll('[data-select-id]').forEach((row) => {
      row.addEventListener('click', (e) => {
        if (e.target.closest('button')) return;
        this.pipeline.selectedLayerId = row.dataset.selectId;
        const selectedLayer = this.pipeline.getSelectedLayer();
        const seasonIds = this.pipeline.simulationProcessesForLayer(selectedLayer);
        if (seasonIds.length && this.pipeline.simPlaybackMode === 'solo') {
          const state0 = this.pipeline.getSimulationControlState();
          const firstSeason = seasonIds.find((id) => state0.processes.some((proc) => proc.id === id && proc.available)) || seasonIds[0];
          this.pipeline.selectSimulationProcess(firstSeason);
        }
        if (this.pipeline.shadingMode === 5) {
          this.pipeline.evaluateStack({ reseedPlacements: false, fastPreview: true });
        }
        this.renderStackList();
        this.renderInspector();
        this.updateSplineGizmoOverlay();
      });

      // HTML5 Drag-and-Drop Reordering
      row.addEventListener('dragstart', (e) => {
        const sourceLayer = this.pipeline.layers[Number(row.dataset.idx)];
        if (!sourceLayer || isPositionLockedLayer(sourceLayer)) {
          e.preventDefault();
          this.dragFromIdx = null;
          return;
        }
        this.dragFromIdx = Number(row.dataset.idx);
        row.classList.add('dragging');
        e.dataTransfer.effectAllowed = 'move';
      });
      row.addEventListener('dragend', () => {
        row.classList.remove('dragging');
        this.elList.querySelectorAll('.drop-target').forEach((el) => el.classList.remove('drop-target'));
      });
      row.addEventListener('dragover', (e) => {
        e.preventDefault();
        row.classList.add('drop-target');
      });
      row.addEventListener('dragleave', () => {
        row.classList.remove('drop-target');
      });
      row.addEventListener('drop', (e) => {
        e.preventDefault();
        row.classList.remove('drop-target');
        const toIdx = Number(row.dataset.idx);
        if (this.dragFromIdx !== null && this.dragFromIdx !== toIdx) {
          this.pipeline.moveLayer(this.dragFromIdx, toIdx);
          this.dragFromIdx = null;
          this.renderAll();
        }
      });
    });

    this.elList.querySelectorAll('[data-toggle-vis]').forEach((btn) => {
      btn.addEventListener('click', (e) => {
        e.stopPropagation();
        const l = this.pipeline.layers.find((x) => x.id === btn.dataset.toggleVis);
        if (!l || isEnvironmentLayer(l)) return;
        l.enabled = !l.enabled;
        this.pipeline.evaluateStack({ reseedPlacements: true });
        this.renderAll();
      });
    });

    this.elList.querySelectorAll('[data-toggle-mask]').forEach((btn) => {
      btn.addEventListener('click', (e) => {
        e.stopPropagation();
        const l = this.pipeline.layers.find((x) => x.id === btn.dataset.toggleMask);
        if (!l || !l.mask || isEnvironmentLayer(l)) return;
        l.mask.enabled = !l.mask.enabled;
        this.pipeline.selectedLayerId = l.id;
        this.pipeline.evaluateStack({ reseedPlacements: false });
        this.renderAll();
      });
    });

    this.elList.querySelectorAll('[data-preview-mask]').forEach((btn) => {
      btn.addEventListener('click', (e) => {
        e.stopPropagation();
        this.pipeline.selectedLayerId = btn.dataset.previewMask;
        this.pipeline.shadingMode = this.pipeline.shadingMode === 5 ? 0 : 5;
        if (this.elShadingSelect) this.elShadingSelect.value = String(this.pipeline.shadingMode);
        this.pipeline.evaluateStack({ reseedPlacements: false, fastPreview: true });
        this.renderAll();
      });
    });

    this.elList.querySelectorAll('[data-move-up]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const idx = Number(btn.dataset.moveUp);
        this.pipeline.moveLayer(idx, idx - 1);
        this.renderAll();
      });
    });

    this.elList.querySelectorAll('[data-move-down]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const idx = Number(btn.dataset.moveDown);
        this.pipeline.moveLayer(idx, idx + 1);
        this.renderAll();
      });
    });

    this.elList.querySelectorAll('[data-dup]').forEach((btn) => {
      btn.addEventListener('click', () => {
        this.pipeline.duplicateLayer(btn.dataset.dup);
        this.renderAll();
      });
    });

    this.elList.querySelectorAll('[data-del]').forEach((btn) => {
      btn.addEventListener('click', () => {
        this.pipeline.deleteLayer(btn.dataset.del);
        this.renderAll();
      });
    });

    this.updateSceneCaption();
  }

  _contrastTextForHex(hex) {
    const safe = normalizeHexColor(hex);
    const n = parseInt(safe.slice(1), 16);
    const r = (n >> 16) & 255;
    const g = (n >> 8) & 255;
    const b = n & 255;
    const luminance = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255;
    return luminance > 0.58 ? '#211b15' : '#fff8ec';
  }

  _rangeControlHtml(def, value, attrs) {
    const numeric = Number.isFinite(Number(value)) ? Number(value) : def.min;
    const pct = Math.max(0, Math.min(100, ((numeric - def.min) / Math.max(1e-6, def.max - def.min)) * 100));
    return `
      <div class="tp-control-row" ${def.help ? `title="${this._esc(def.help)}"` : ''}>
        <div class="tp-control-label"><span>${def.label}</span></div>
        <div class="tp-amount" style="--fill:${pct.toFixed(1)}%">
          <input type="range" min="${def.min}" max="${def.max}" step="${def.step}" value="${numeric}" ${attrs} />
          <output>${def.fmt(numeric)}</output>
        </div>
      </div>`;
  }

  _updateRangeOutput(input, def) {
    const value = Number(input.value);
    const amount = input.parentElement;
    const output = amount?.querySelector?.('output');
    if (output && def) output.textContent = def.fmt(value);
    const pct = ((value - Number(input.min)) / Math.max(1e-6, Number(input.max) - Number(input.min))) * 100;
    amount?.style?.setProperty?.('--fill', `${pct.toFixed(1)}%`);
    return value;
  }

  _snowEffectsForInspector(layer) {
    const effects = Array.isArray(layer.params.snowEffects)
      ? normalizeSnowEffects(layer.params.snowEffects)
      : createSnowEffectPreset(layer.params.snowEffectPreset ?? DEFAULT_EROSION_PARAMS.snowEffectPreset).effects;
    layer.params.snowEffects = effects;
    if (!effects.some((effect) => effect.id === this.selectedSnowEffectId)) {
      this.selectedSnowEffectId = effects[0]?.id ?? null;
    }
    return effects;
  }

  _strataBedsForInspector(layer) {
    const beds = normalizeStrataColumn(
      layer.params.strataBeds,
      layer.params.strataColumnPreset ?? DEFAULT_EROSION_PARAMS.strataColumnPreset,
    );
    layer.params.strataBeds = beds;
    if (!beds.some((bed) => bed.id === this.selectedStrataBedId)) {
      this.selectedStrataBedId = beds[beds.length - 1]?.id ?? null;
    }
    return beds;
  }

  _commitSnowLiveChange(layer, reason = 'Snow / frost settings changed', rerender = true) {
    layer.params.snowEffects = normalizeSnowEffects(layer.params.snowEffects);
    Object.assign(layer.params, normalizeFrostParams(layer.params));
    const appliedLive = this.pipeline.applyLiveSnowLayerParams?.(layer, reason);
    if (!appliedLive) {
      this.pipeline.evaluateStack({ reseedPlacements: true });
      this.renderAll();
      return false;
    }
    this.renderStackList();
    if (rerender) this.renderInspector();
    this.updateSceneCaption();
    return true;
  }

  _commitStrataChange(layer, { fast = false, rerender = true } = {}) {
    layer.params.strataBeds = normalizeStrataColumn(
      layer.params.strataBeds,
      layer.params.strataColumnPreset ?? DEFAULT_EROSION_PARAMS.strataColumnPreset,
    );
    this.pipeline.evaluateStack({ reseedPlacements: true, fastPreview: fast });
    if (rerender) this.renderAll();
    else {
      this.renderStackList();
      this.updateSceneCaption();
    }
  }

  _seasonalClimateCardHtml(layer, iconColor) {
    const params = normalizeSeasonalClimateParams(layer.params);
    const elapsedYears = Math.max(0, Number(this.engine?.climateElapsedYears) || 0);
    const seed = Number(this.engine?.params?.seed) || 4029;
    const state = seasonalClimateAt(params, elapsedYears, seed);
    const engineRun = typeof this.engine?.getSeasonalClimateRunState === 'function'
      ? this.engine.getSeasonalClimateRunState(state)
      : null;
    const runStart = Math.max(0, Number(this.engine?.climateRunStartYears) || 0);
    const run = engineRun || {
      runElapsedYears: Math.max(0, elapsedYears - runStart),
      targetYears: params.climateYearsToSimulate,
      progress: params.climateYearsToSimulate > 0
        ? Math.max(0, Math.min(1, Math.max(0, elapsedYears - runStart) / params.climateYearsToSimulate))
        : 0,
      complete: false,
    };
    const substeps = Math.max(1, Number(this.engine?.params?.substepsPerFrame) || 2);
    const secondsPerYear = params.climateStepsPerYear / (60 * substeps);
    const runSeconds = run.targetYears > 0 ? secondsPerYear * run.targetYears : 0;
    const hemisphereText = state.hemisphere === 'south'
      ? 'Southern hemisphere — seasons shifted six months'
      : state.hemisphere === 'north'
        ? 'Northern hemisphere'
        : 'Equatorial — weak temperature season, wet/dry timing dominates';
    const runText = !state.enabled
      ? 'Seasonal climate off'
      : run.targetYears > 0
        ? `${run.runElapsedYears.toFixed(2)} / ${run.targetYears} years · ${Math.round(run.progress * 100)}%`
        : `${elapsedYears.toFixed(2)} years elapsed · continuous`;
    const precipText = `${Math.round(state.referenceAnnualPrecipMm)} mm/yr`;
    const snowText = `${state.referenceAnnualSnowMmSWE < 10 ? state.referenceAnnualSnowMmSWE.toFixed(1) : Math.round(state.referenceAnnualSnowMmSWE)} mm snow-water/yr`;
    return `
      <section class="tp-property-card tp-seasonal-climate-card">
        <div class="tp-card-head-row">
          <h2><i style="background:${iconColor}"></i><span>Location Seasonal Climate</span></h2>
          <button type="button" class="tp-mini-btn ${state.enabled ? 'active' : ''}" data-env-seasonal-toggle="1" aria-pressed="${state.enabled ? 'true' : 'false'}" title="Turn seasonal simulation ON or OFF (constant annual forcing)">
            ${state.enabled ? 'Seasonal: ON' : 'Seasonal: OFF'}
          </button>
        </div>
        <p class="tp-card-note"><strong>${this._esc(state.regime.label)}.</strong> ${this._esc(state.regime.description)} Latitude reverses the calendar in the southern hemisphere and weakens the temperature swing near the equator. Rain/Snow/Wind/Chemical layers still own their annual magnitudes and play/pause gates; this climate decides when precipitation falls, whether it is rain or snow, when pore water freezes, when snow melts and how fast water evaporates.</p>
        <div class="tp-frost-diagnostics">
          <div><span>Climate calendar</span><strong>Year ${state.year} · ${this._esc(state.monthName)} ${state.dayOfMonth}</strong><small>${this._esc(state.seasonName)} · ${this._esc(hemisphereText)}</small></div>
          <div><span>Reference temperature</span><strong>${state.temperatureC.toFixed(1)}°C · night ${state.nightMinC.toFixed(1)}°C</strong><small>noon sun ${state.noonSunElevationDeg.toFixed(0)}° at this latitude/date; terrain altitude and aspect still modify snow/frost locally</small></div>
          <div><span>Precipitation now</span><strong>Rain ${state.rainMultiplier.toFixed(2)}× · Snow ${state.snowMultiplier.toFixed(2)}×</strong><small>${state.stormPulse ? 'short storm pulse active' : 'background/dry-spell forcing'} · total precipitation ${state.precipitationMultiplier.toFixed(2)}×</small></div>
          <div><span>Melt & drying</span><strong>Melt ${state.meltMultiplier.toFixed(2)}× · Evap ${state.evaporationMultiplier.toFixed(2)}×</strong><small>valley snow retention ${Math.round(state.lowElevationRetention * 100)}%; melt only uses water already stored as snow/ice</small></div>
          <div><span>Frost & moisture</span><strong>Frost ${state.frostMultiplier.toFixed(2)}× · Moisture ${state.moistureMultiplier.toFixed(2)}×</strong><small>climate normal ≈ ${state.frostNightsPerYear} frost nights/yr; with a frost-capable Snow layer (even at zero snowfall), a dry high desert can freeze existing pore water without building a snowpack</small></div>
          <div><span>Climate normal</span><strong>${precipText} · ${snowText}</strong><small>regime reference before the individual Rain/Snow layer magnitudes; ${formatLatitude(params.climateLatitudeDeg)}</small></div>
          <div><span>Simulated run</span><strong>${this._esc(runText)}</strong><small>${params.climateStepsPerYear} steps/yr ≈ ${secondsPerYear.toFixed(1)} sec/yr at ${substeps} substeps/frame${runSeconds ? ` · about ${runSeconds.toFixed(0)} sec for this run` : ''}</small></div>
          <div><span>Year character</span><strong>${state.enabled ? `${state.temperatureAnomalyC >= 0 ? '+' : ''}${state.temperatureAnomalyC.toFixed(1)}°C · precip ${(state.wetAnomaly ?? 1).toFixed(2)}×` : 'constant'}</strong><small>deterministic wet/dry and warm/cold variation makes each simulated year related, not identical</small></div>
        </div>
        <p class="tp-card-note">Examples: a hot arid desert normal is only ~35 mm/yr with almost no snow and rare storm pulses; a semi-arid high desert has sparse rain/snow but cold clear nights, so—when a frost effect layer is present—existing water can freeze in rock pores. Environment never adds that process to a Wind+Rain-only stack by itself. Set Years per Simulate Run to 20 and the live simulation auto-pauses after twenty location years; stack bakes do not consume calendar time.</p>
      </section>`;
  }

  _snowFrostCardHtml(layer, iconColor) {
    const effects = this._snowEffectsForInspector(layer);
    const frost = normalizeFrostParams(layer.params);
    Object.assign(layer.params, frost);
    const selected = effects.find((effect) => effect.id === this.selectedSnowEffectId) || effects[0] || null;
    const derived = deriveSnowEffectWeights(effects);
    const seasonalClimate = typeof this.engine._getSeasonalClimate === 'function'
      ? this.engine._getSeasonalClimate()
      : seasonalClimateAt(this.engine.params || {}, this.engine.climateElapsedYears || 0, this.engine.params?.seed || 4029);
    const meltCycle = snowmeltCycleAtStep(derived, this.engine.cryoStepCount ?? this.engine.stepCount ?? 0, seasonalClimate);
    const annual = estimateAnnualFrostWindow(frost, 0.35);
    const cellSizeM = (this.engine.params.worldSpan || 500) / Math.max(32, this.engine.gridRes || this.pipeline.R || 256);
    const visibleMm = visibleCrackThresholdMm(cellSizeM, frost.frostVisibleCrackMm);
    const presetId = SNOW_EFFECT_PRESETS[layer.params.snowEffectPreset]
      ? layer.params.snowEffectPreset
      : 'custom';
    const presetOptions = [
      ...(presetId === 'custom' ? '<option value="custom" selected>Custom effect stack</option>' : ''),
      ...Object.values(SNOW_EFFECT_PRESETS).map((preset) => `<option value="${preset.id}" ${preset.id === presetId ? 'selected' : ''}>${preset.label}</option>`),
    ].join('');
    const presetDescription = presetId === 'custom'
      ? 'This stack has been edited. Duplicate process entries add their intensity (capped), while their physical sub-parameters are intensity-weighted.'
      : SNOW_EFFECT_PRESETS[presetId].description;
    const weightChips = [
      ['Ice lenses', derived.weights.segregation],
      ['Freeze–thaw', derived.weights.volumetric],
      ['Nivation', derived.weights.nivation],
      ['Frost creep', derived.weights.frostCreep],
      ['Avalanche', derived.weights.avalanche],
      ['Plucking', derived.weights.glacierPlucking],
      ['Snowmelt runoff', derived.weights.snowmelt],
      ['Legacy instant', derived.weights.legacy],
    ].filter(([, value]) => value > 0.001)
      .map(([label, value]) => `<span class="tp-frost-weight-chip">${label} ${Math.round(value * 100)}%</span>`)
      .join('') || '<span class="tp-frost-weight-chip muted">Snowpack only — no bedrock frost weathering</span>';
    const globalControls = FROST_GLOBAL_CONTROLS
      .map((def) => this._rangeControlHtml(def, frost[def.key], `data-frost-global="${def.key}"`))
      .join('');
    const addOptions = Object.values(SNOW_EFFECT_TYPES)
      .map((type) => `<option value="${type.id}">${type.label}</option>`)
      .join('');
    const effectRows = effects.map((effect, index) => {
      const spec = SNOW_EFFECT_TYPES[effect.type] || SNOW_EFFECT_TYPES['segregation-ice'];
      return `
        <div class="tp-snow-effect-row ${selected?.id === effect.id ? 'selected' : ''} ${effect.enabled ? '' : 'disabled'}" data-snow-effect-row="${effect.id}">
          <button type="button" class="tp-effect-main" data-snow-effect-select="${effect.id}" title="Select and edit this Snow effect">
            <span class="tp-effect-dot" style="background:${spec.color}"></span>
            <span class="tp-effect-copy">
              <strong>${this._esc(effect.name)}</strong>
              <small>${spec.short} · ${Math.round(effect.intensity * 100)}% intensity</small>
            </span>
          </button>
          <label class="tp-effect-enable" title="Enable this effect">
            <input type="checkbox" data-snow-effect-enabled="${effect.id}" ${effect.enabled ? 'checked' : ''} />
            <span>On</span>
          </label>
          <div class="tp-effect-row-actions">
            <button type="button" data-snow-effect-move="-1:${effect.id}" ${index === 0 ? 'disabled' : ''} title="Move effect up in the stack">↑</button>
            <button type="button" data-snow-effect-move="1:${effect.id}" ${index === effects.length - 1 ? 'disabled' : ''} title="Move effect down in the stack">↓</button>
            <button type="button" data-snow-effect-duplicate="${effect.id}" ${effects.length >= MAX_SNOW_EFFECTS ? 'disabled' : ''} title="Duplicate effect (same-process intensities add)">⧉</button>
            <button type="button" class="danger" data-snow-effect-delete="${effect.id}" title="Delete effect">×</button>
          </div>
        </div>`;
    }).join('');
    let selectedHtml = '<p class="tp-card-note">No frost-weathering effects. Snow/ice can still accumulate and flow, but it will not crack, pit or pluck bedrock.</p>';
    if (selected) {
      const spec = SNOW_EFFECT_TYPES[selected.type] || SNOW_EFFECT_TYPES['segregation-ice'];
      const typeOptions = Object.values(SNOW_EFFECT_TYPES)
        .map((type) => `<option value="${type.id}" ${type.id === selected.type ? 'selected' : ''}>${type.label}</option>`)
        .join('');
      const paramControls = (SNOW_EFFECT_PARAM_CONTROLS[selected.type] || [])
        .map((def) => this._rangeControlHtml(def, selected[def.key], `data-snow-effect-param="${def.key}"`))
        .join('');
      selectedHtml = `
        <div class="tp-selected-effect-panel">
          <div class="tp-selected-effect-head">
            <span class="tp-effect-dot large" style="background:${spec.color}"></span>
            <div>
              <strong>${spec.label}</strong>
              <small>${summarizeSnowEffects(effects)}</small>
            </div>
          </div>
          <p class="tp-card-note">${spec.description}</p>
          <label class="tp-field-row">
            <span>Effect Name</span>
            <input type="text" class="tp-text-input" data-snow-effect-name value="${this._esc(selected.name)}" maxlength="90" />
          </label>
          <label class="tp-field-row">
            <span>Process Type</span>
            <select class="tp-select" data-snow-effect-type>${typeOptions}</select>
          </label>
          ${this._rangeControlHtml(
            { key: 'intensity', label: 'Effect Intensity', min: 0, max: 2, step: 0.02, fmt: (v) => `${Math.round(v * 100)}%` },
            selected.intensity,
            'data-snow-effect-intensity="1"',
          )}
          ${paramControls}
        </div>`;
    }
    return `
      <section class="tp-property-card tp-snow-frost-card">
        <h2><i style="background:${iconColor}"></i><span>Frost Cracking, Pitting & Snow Effect Stack</span></h2>
        <p class="tp-card-note"><strong>Why old Snow could bury summits:</strong> it converted snowy bedrock to sediment immediately in the same cell. Frost debris is real, but real ice-lens damage needs water, a thermal gradient and time. This model stores hairline cracks invisibly, releases pits only after both thresholds below are crossed, then exports loosened debris through creep, meltwash, avalanches and subglacial till. Snowfall lands across the storm field at every elevation; the Snowline slider is the long-term melt-equilibrium line. The locked Environment layer's latitude/regime calendar now supplies winter accumulation, valley retention, frost moisture and warm-season finite melt, so stored snow can become erosive runoff without Rain being enabled.</p>
        <label class="tp-field-row">
          <span>Snow Effect Preset</span>
          <select class="tp-select" id="tp-snow-effect-preset">${presetOptions}</select>
        </label>
        <p class="tp-card-note">${presetDescription}</p>
        <div class="tp-card-footer-btns">
          <button type="button" class="tp-mini-btn" id="tp-btn-apply-snow-preset">Apply Preset</button>
          <button type="button" class="tp-mini-btn ${this.pipeline.shadingMode === 10 ? 'active' : ''}" id="tp-btn-frost-diagnostic">Frost Damage View</button>
        </div>
        <div class="tp-frost-diagnostics">
          <div><span>Segregation window</span><strong>${Math.round(annual.windowFraction * 100)}% of year</strong><small>at ${frost.frostCrackDepthM.toFixed(2)} m depth, 0.35 m snow</small></div>
          <div><span>Thermal skin depth</span><strong>${annual.skinDepthM.toFixed(2)} m</strong><small>surface amplitude ±${annual.amplitudeC.toFixed(1)}°C after insulation</small></div>
          <div><span>Mean gradient</span><strong>${annual.meanGradientCPerM.toFixed(1)} °C/m</strong><small>drives liquid-film migration to ice lenses</small></div>
          <div><span>Saturation efficacy</span><strong>${Math.round(annual.saturationEfficacy * 100)}%</strong><small>peak at ${Math.round(frost.frostSaturationPeak * 100)}% pore saturation</small></div>
          <div><span>Visible crack gate</span><strong>${visibleMm.toFixed(1)} mm</strong><small>= max(requested ${frost.frostVisibleCrackMm.toFixed(1)} mm, ${(cellSizeM * 8).toFixed(1)} mm at ${cellSizeM.toFixed(2)} m cells)</small></div>
          <div><span>Damage gate</span><strong>${Math.round(frost.frostDamageThreshold * 100)}%</strong><small>below this, natural view and geometry stay unchanged</small></div>
          <div><span>Location snow season</span><strong>${meltCycle.climateDriven ? `Year ${seasonalClimate.year} · ${seasonalClimate.monthName} ${seasonalClimate.dayOfMonth} · ${seasonalClimate.seasonName}` : (meltCycle.active ? `${Math.round(meltCycle.phase * 100)}% · ${meltCycle.season < 0 ? 'cold accumulation' : 'warm/dry melt'}` : 'seasonal climate off')}</strong><small>${meltCycle.climateDriven ? `${seasonalClimate.regime.label} · snowfall ${meltCycle.snowfallMultiplier.toFixed(2)}× · melt ${meltCycle.meltMultiplier.toFixed(2)}× · valley retention ${Math.round(meltCycle.lowElevationRetention * 100)}% · runoff effect ${meltCycle.active ? 'on' : 'off'}` : (meltCycle.active ? `fallback cycle · snowfall ${meltCycle.snowfallMultiplier.toFixed(2)}× · melt ${meltCycle.meltMultiplier.toFixed(2)}× · valley retention ${Math.round(meltCycle.lowElevationRetention * 100)}%` : 'enable Location seasons on Environment, or add the snowmelt-runoff effect for the fallback cycle')}</small></div>
        </div>
        <div class="tp-frost-weight-row">${weightChips}</div>
        <details class="tp-frost-globals" open>
          <summary>Climate, saturation &amp; sub-resolution release</summary>
          <p class="tp-card-note">These are live Snow-season controls on the topmost enabled Snow layer: editing them pauses, but does not rebuild the terrain or erase stored cracks. Initial damage is the exception—it is applied when the stack next evaluates.</p>
          ${globalControls}
        </details>
        <div class="tp-effect-add-row">
          <select class="tp-select" id="tp-snow-effect-add-type" aria-label="Snow effect type to add">${addOptions}</select>
          <button type="button" class="tp-mini-btn" id="tp-btn-add-snow-effect" ${effects.length >= MAX_SNOW_EFFECTS ? 'disabled' : ''}>+ Add Effect</button>
        </div>
        <div class="tp-snow-effect-list">${effectRows || '<div class="tp-empty-inline">No Snow effects in this stack.</div>'}</div>
        ${selectedHtml}
      </section>`;
  }

  _strataColumnCardHtml(layer, iconColor) {
    const beds = this._strataBedsForInspector(layer);
    const enabled = layer.params.strataColumnEnabled === true;
    const presetId = STRATA_COLUMN_PRESETS[layer.params.strataColumnPreset]
      ? layer.params.strataColumnPreset
      : 'custom';
    const totalM = strataColumnThickness(beds);
    const baseM = Number(layer.params.strataColumnBaseM ?? DEFAULT_EROSION_PARAMS.strataColumnBaseM) || 0;
    const selectedIndex = Math.max(0, beds.findIndex((bed) => bed.id === this.selectedStrataBedId));
    const selected = beds[selectedIndex] || beds[beds.length - 1];
    const selectedBottom = beds.slice(0, selectedIndex).reduce((sum, bed) => sum + bed.thicknessM, 0);
    const presetOptions = [
      ...(presetId === 'custom' ? '<option value="custom" selected>Custom geological column</option>' : ''),
      ...Object.values(STRATA_COLUMN_PRESETS).map((preset) => `<option value="${preset.id}" ${preset.id === presetId ? 'selected' : ''}>${preset.label}</option>`),
    ].join('');
    const presetDescription = presetId === 'custom'
      ? 'Custom measured section. Every bed below is used in order exactly once; beyond the top/bottom, the end bed continues instead of the palette repeating.'
      : STRATA_COLUMN_PRESETS[presetId].description;
    const columnRows = beds.map((bed, index) => ({ bed, index })).reverse().map(({ bed, index }) => {
      const lith = STRATA_LITHOLOGIES[bed.lithology] || STRATA_LITHOLOGIES.sandstone;
      const heightPx = Math.max(20, Math.min(52, 13 + bed.thicknessM * 1.15));
      return `
        <button type="button" class="tp-strata-bed-row ${selected?.id === bed.id ? 'selected' : ''}" data-strata-bed-select="${bed.id}"
          style="--bed-color:${bed.color};--bed-text:${this._contrastTextForHex(bed.color)};height:${heightPx.toFixed(0)}px"
          title="Bed ${index + 1} from bottom: ${this._esc(bed.name)} — ${bed.thicknessM.toFixed(1)} m ${lith.label}">
          <span class="tp-bed-order">${index + 1}</span>
          <span class="tp-bed-name">${this._esc(bed.name)}</span>
          <span class="tp-bed-meta">${bed.thicknessM.toFixed(1)} m · H ${Math.round(bed.hardness * 100)}%</span>
        </button>`;
    }).join('');
    let selectedHtml = '';
    if (selected) {
      const derived = deriveBedProperties(selected);
      const lithOptions = Object.values(STRATA_LITHOLOGIES)
        .map((lith) => `<option value="${lith.id}" ${lith.id === selected.lithology ? 'selected' : ''}>${lith.label}</option>`)
        .join('');
      const controls = STRATA_BED_CONTROLS
        .map((def) => this._rangeControlHtml(def, selected[def.key], `data-strata-bed-param="${def.key}"`))
        .join('');
      selectedHtml = `
        <div class="tp-selected-bed-panel">
          <div class="tp-selected-bed-head">
            <span class="tp-bed-swatch" style="background:${selected.color}"></span>
            <div>
              <strong>Bed ${selectedIndex + 1} of ${beds.length} from bottom</strong>
              <small>Stratigraphic interval ${(baseM + selectedBottom).toFixed(1)}–${(baseM + selectedBottom + selected.thicknessM).toFixed(1)} m before dip/warp</small>
            </div>
          </div>
          <label class="tp-field-row">
            <span>Bed Name</span>
            <input type="text" class="tp-text-input" data-strata-bed-name value="${this._esc(selected.name)}" maxlength="80" />
          </label>
          <label class="tp-field-row">
            <span>Lithology Template</span>
            <select class="tp-select" data-strata-bed-lithology>${lithOptions}</select>
          </label>
          <div class="tp-bed-color-row">
            <label>
              <span>Flat Bed Colour</span>
              <input type="color" data-strata-bed-color value="${selected.color}" />
            </label>
            <input type="text" class="tp-text-input" data-strata-bed-color-hex value="${selected.color}" maxlength="7" aria-label="Bed colour hex" />
          </div>
          ${controls}
          <div class="tp-bed-derived-grid">
            <div><span>Permeability</span><strong>${derived.permeability.toFixed(2)}×</strong></div>
            <div><span>Tensile resistance</span><strong>${derived.tensileResistance.toFixed(2)}×</strong></div>
            <div><span>Frost susceptibility</span><strong>${derived.frostSusceptibility.toFixed(2)}×</strong></div>
            <div><span>Solubility</span><strong>${Math.round(derived.solubility * 100)}%</strong></div>
            <div><span>Glacier plucking</span><strong>${Math.round(derived.pluckingSusceptibility * 100)}%</strong></div>
          </div>
          <div class="tp-card-footer-btns">
            <button type="button" class="tp-mini-btn" data-strata-bed-move="1" ${selectedIndex >= beds.length - 1 ? 'disabled' : ''}>Move Younger ↑</button>
            <button type="button" class="tp-mini-btn" data-strata-bed-move="-1" ${selectedIndex <= 0 ? 'disabled' : ''}>Move Older ↓</button>
            <button type="button" class="tp-mini-btn" data-strata-bed-duplicate="1" ${beds.length >= MAX_STRATA_BEDS ? 'disabled' : ''}>Duplicate</button>
            <button type="button" class="tp-mini-btn danger" data-strata-bed-delete="1" ${beds.length <= 1 ? 'disabled' : ''}>Delete Bed</button>
          </div>
        </div>`;
    }
    return `
      <section class="tp-property-card tp-strata-column-card">
        <h2><i style="background:${iconColor}"></i><span>Authored Geological Column</span></h2>
        <p class="tp-card-note">A measured section, youngest at the top and oldest at the bottom. Thickness, colour and chemistry are independent for every bed. The same bed drives hardness, benching, frost response, mineral staining and flat terrain colour—there is no procedural colour fabric and no repeating colour cycle.</p>
        <label class="tp-check-row">
          <input type="checkbox" id="tp-chk-strata-column-enabled" ${enabled ? 'checked' : ''} />
          <span>Use authored non-repeating beds (instead of legacy periodic cycle)</span>
        </label>
        <label class="tp-field-row">
          <span>Column Preset</span>
          <select class="tp-select" id="tp-strata-column-preset">${presetOptions}</select>
        </label>
        <p class="tp-card-note">${presetDescription}</p>
        <div class="tp-strata-summary">
          <span><strong>${beds.length}</strong> beds</span>
          <span><strong>${totalM.toFixed(1)} m</strong> total</span>
          <span>${baseM.toFixed(0)} → ${(baseM + totalM).toFixed(0)} m</span>
          <span>${enabled ? 'Active in simulation' : 'Inactive — legacy cycle in use'}</span>
        </div>
        <div class="tp-card-footer-btns">
          <button type="button" class="tp-mini-btn" id="tp-btn-apply-strata-preset">Apply Column Preset</button>
          <button type="button" class="tp-mini-btn" id="tp-btn-add-strata-bed" ${beds.length >= MAX_STRATA_BEDS ? 'disabled' : ''}>+ Add Youngest Bed</button>
        </div>
        <div class="tp-strata-column-head"><span>Youngest ↑</span><span>Click a bed to edit its chemistry</span><span>↓ Oldest</span></div>
        <div class="tp-strata-column ${enabled ? '' : 'inactive'}">${columnRows}</div>
        ${selectedHtml}
      </section>`;
  }

  _wireSnowFrostInspector(layer) {
    if (!this.elInspector || layer.type !== 'aeolian-glacial') return;
    const root = this.elInspector;
    const getEffects = () => this._snowEffectsForInspector(layer);
    const getSelected = () => getEffects().find((effect) => effect.id === this.selectedSnowEffectId) || null;
    const markCustom = () => { layer.params.snowEffectPreset = 'custom'; };
    const applyLive = (rerender = true, fastNonAuthority = false) => {
      layer.params.snowEffects = normalizeSnowEffects(getEffects());
      Object.assign(layer.params, normalizeFrostParams(layer.params));
      if (this.pipeline.applyLiveSnowLayerParams?.(layer)) {
        this.renderStackList();
        if (rerender) this.renderInspector();
        this.updateSceneCaption();
        return true;
      }
      this.pipeline.evaluateStack({ reseedPlacements: true, fastPreview: fastNonAuthority });
      if (rerender) this.renderAll();
      else {
        this.renderStackList();
        this.updateSceneCaption();
      }
      return false;
    };

    document.getElementById('tp-btn-apply-snow-preset')?.addEventListener('click', () => {
      const select = document.getElementById('tp-snow-effect-preset');
      const presetId = select?.value;
      if (!presetId || presetId === 'custom' || !SNOW_EFFECT_PRESETS[presetId]) return;
      const preset = createSnowEffectPreset(presetId);
      layer.params.snowEffectPreset = preset.presetId;
      layer.params.snowEffects = preset.effects;
      Object.assign(layer.params, preset.globals);
      this.selectedSnowEffectId = preset.effects[0]?.id ?? null;
      applyLive(true);
    });

    document.getElementById('tp-btn-frost-diagnostic')?.addEventListener('click', () => {
      this.pipeline.shadingMode = this.pipeline.shadingMode === 10 ? 0 : 10;
      if (this.elShadingSelect) this.elShadingSelect.value = String(this.pipeline.shadingMode);
      this.renderInspector();
      this.updateSceneCaption();
    });

    root.querySelectorAll('[data-frost-global]').forEach((input) => {
      const key = input.dataset.frostGlobal;
      const def = FROST_GLOBAL_CONTROLS.find((entry) => entry.key === key);
      input.addEventListener('input', () => {
        const value = this._updateRangeOutput(input, def);
        layer.params[key] = value;
        if (key !== 'frostInitialDamage') {
          layer.params.snowEffects = normalizeSnowEffects(getEffects());
          if (!this.pipeline.applyLiveSnowLayerParams?.(layer)) {
            this.pipeline.evaluateStack({ reseedPlacements: true, fastPreview: true });
            this.renderStackList();
          }
        }
        this.updateSceneCaption();
      });
      input.addEventListener('change', () => {
        markCustom();
        if (key === 'frostInitialDamage') {
          // Pre-aging is an evaluated initial condition, not a live mutation
          // of cracks that have already grown during this season.
          this.pipeline.evaluateStack({ reseedPlacements: true });
          this.renderAll();
        } else {
          applyLive(true);
        }
      });
    });

    document.getElementById('tp-btn-add-snow-effect')?.addEventListener('click', () => {
      const effects = getEffects();
      if (effects.length >= MAX_SNOW_EFFECTS) return;
      const type = document.getElementById('tp-snow-effect-add-type')?.value || 'segregation-ice';
      const effect = createSnowEffect(type);
      effects.push(effect);
      this.selectedSnowEffectId = effect.id;
      markCustom();
      applyLive(true);
    });

    root.querySelectorAll('[data-snow-effect-select]').forEach((button) => {
      button.addEventListener('click', () => {
        this.selectedSnowEffectId = button.dataset.snowEffectSelect;
        this.renderInspector();
      });
    });

    root.querySelectorAll('[data-snow-effect-enabled]').forEach((input) => {
      input.addEventListener('change', () => {
        const effect = getEffects().find((entry) => entry.id === input.dataset.snowEffectEnabled);
        if (!effect) return;
        effect.enabled = Boolean(input.checked);
        this.selectedSnowEffectId = effect.id;
        markCustom();
        applyLive(true);
      });
    });

    root.querySelectorAll('[data-snow-effect-move]').forEach((button) => {
      button.addEventListener('click', () => {
        const [deltaRaw, effectId] = String(button.dataset.snowEffectMove).split(':');
        const effects = getEffects();
        const index = effects.findIndex((entry) => entry.id === effectId);
        const nextIndex = index + Number(deltaRaw);
        if (index < 0 || nextIndex < 0 || nextIndex >= effects.length) return;
        [effects[index], effects[nextIndex]] = [effects[nextIndex], effects[index]];
        this.selectedSnowEffectId = effectId;
        markCustom();
        applyLive(true);
      });
    });

    root.querySelectorAll('[data-snow-effect-duplicate]').forEach((button) => {
      button.addEventListener('click', () => {
        const effects = getEffects();
        if (effects.length >= MAX_SNOW_EFFECTS) return;
        const index = effects.findIndex((entry) => entry.id === button.dataset.snowEffectDuplicate);
        if (index < 0) return;
        const source = effects[index];
        const copy = createSnowEffect(source.type, { ...source, id: undefined, name: `${source.name} copy` });
        effects.splice(index + 1, 0, copy);
        this.selectedSnowEffectId = copy.id;
        markCustom();
        applyLive(true);
      });
    });

    root.querySelectorAll('[data-snow-effect-delete]').forEach((button) => {
      button.addEventListener('click', () => {
        const effects = getEffects();
        const index = effects.findIndex((entry) => entry.id === button.dataset.snowEffectDelete);
        if (index < 0) return;
        effects.splice(index, 1);
        this.selectedSnowEffectId = effects[Math.min(index, effects.length - 1)]?.id ?? null;
        markCustom();
        applyLive(true);
      });
    });

    const initialSelected = getSelected();
    if (!initialSelected) return;
    const selectedId = initialSelected.id;
    const selectedType = initialSelected.type;

    root.querySelector('[data-snow-effect-name]')?.addEventListener('change', (event) => {
      const effect = getSelected();
      if (!effect) return;
      effect.name = String(event.target.value || '').trim() || SNOW_EFFECT_TYPES[effect.type].label;
      markCustom();
      applyLive(true);
    });

    root.querySelector('[data-snow-effect-type]')?.addEventListener('change', (event) => {
      const effects = getEffects();
      const index = effects.findIndex((entry) => entry.id === selectedId);
      if (index < 0) return;
      const current = effects[index];
      effects[index] = createSnowEffect(event.target.value, {
        id: current.id,
        name: current.name,
        enabled: current.enabled,
        intensity: current.intensity,
      });
      this.selectedSnowEffectId = effects[index].id;
      markCustom();
      applyLive(true);
    });

    const bindEffectRange = (input, key, def) => {
      input.addEventListener('input', () => {
        // Each commit normalises into fresh effect objects. Resolve the
        // selected entry by id for every input event; retaining the object
        // captured when the Inspector was wired would make a slider drag
        // stop updating after its first tick.
        const effect = getSelected();
        if (!effect) return;
        effect[key] = this._updateRangeOutput(input, def);
        layer.params.snowEffects = normalizeSnowEffects(layer.params.snowEffects);
        if (!this.pipeline.applyLiveSnowLayerParams?.(layer)) {
          this.pipeline.evaluateStack({ reseedPlacements: true, fastPreview: true });
          this.renderStackList();
        }
        this.updateSceneCaption();
      });
      input.addEventListener('change', () => {
        const effects = getEffects();
        const index = effects.findIndex((entry) => entry.id === selectedId);
        if (index >= 0) effects[index] = normalizeSnowEffect(effects[index]);
        markCustom();
        applyLive(true);
      });
    };

    root.querySelectorAll('[data-snow-effect-intensity]').forEach((input) => {
      bindEffectRange(input, 'intensity', { min: 0, max: 2, fmt: (v) => `${Math.round(v * 100)}%` });
    });
    root.querySelectorAll('[data-snow-effect-param]').forEach((input) => {
      const key = input.dataset.snowEffectParam;
      const def = (SNOW_EFFECT_PARAM_CONTROLS[selectedType] || []).find((entry) => entry.key === key);
      if (def) bindEffectRange(input, key, def);
    });
  }

  _wireStrataColumnInspector(layer) {
    if (!this.elInspector || layer.type !== 'stratigraphic-beds') return;
    const root = this.elInspector;
    const getBeds = () => this._strataBedsForInspector(layer);
    const selectedIndexIn = (beds) => Math.max(0, beds.findIndex((bed) => bed.id === this.selectedStrataBedId));
    const getSelected = () => {
      const beds = getBeds();
      return beds[selectedIndexIn(beds)] || null;
    };
    const markCustom = () => { layer.params.strataColumnPreset = 'custom'; };

    document.getElementById('tp-chk-strata-column-enabled')?.addEventListener('change', (event) => {
      layer.params.strataColumnEnabled = Boolean(event.target.checked);
      getBeds();
      this._commitStrataChange(layer);
    });

    document.getElementById('tp-btn-apply-strata-preset')?.addEventListener('click', () => {
      const presetId = document.getElementById('tp-strata-column-preset')?.value;
      if (!presetId || presetId === 'custom' || !STRATA_COLUMN_PRESETS[presetId]) return;
      layer.params.strataColumnPreset = presetId;
      layer.params.strataBeds = createStrataColumnPreset(presetId);
      layer.params.strataColumnEnabled = true;
      this.selectedStrataBedId = layer.params.strataBeds[layer.params.strataBeds.length - 1]?.id ?? null;
      this._commitStrataChange(layer);
    });

    document.getElementById('tp-btn-add-strata-bed')?.addEventListener('click', () => {
      const beds = getBeds();
      if (beds.length >= MAX_STRATA_BEDS) return;
      const source = beds[selectedIndexIn(beds)] || beds[beds.length - 1];
      const bed = createStrataBed(source?.lithology || 'sandstone', {
        name: 'New youngest bed',
        thicknessM: 12,
      });
      beds.push(bed);
      this.selectedStrataBedId = bed.id;
      markCustom();
      this._commitStrataChange(layer);
    });

    root.querySelectorAll('[data-strata-bed-select]').forEach((button) => {
      button.addEventListener('click', () => {
        this.selectedStrataBedId = button.dataset.strataBedSelect;
        this.renderInspector();
      });
    });

    if (!getSelected()) return;

    root.querySelector('[data-strata-bed-name]')?.addEventListener('change', (event) => {
      const bed = getSelected();
      if (!bed) return;
      bed.name = String(event.target.value || '').trim() || STRATA_LITHOLOGIES[bed.lithology].label;
      markCustom();
      this._commitStrataChange(layer);
    });

    root.querySelector('[data-strata-bed-lithology]')?.addEventListener('change', (event) => {
      const beds = getBeds();
      const index = selectedIndexIn(beds);
      beds[index] = applyLithologyDefaults(beds[index], event.target.value);
      this.selectedStrataBedId = beds[index].id;
      markCustom();
      this._commitStrataChange(layer);
    });

    const colorInput = root.querySelector('[data-strata-bed-color]');
    colorInput?.addEventListener('input', () => {
      // Resolve by id on every colour/slider event: each commit rebuilds
      // the normalised bed objects, so a captured bed would go stale after
      // the first tick of a continuous drag.
      const bed = getSelected();
      if (!bed) return;
      bed.color = normalizeHexColor(colorInput.value, bed.color);
      const hexInput = root.querySelector('[data-strata-bed-color-hex]');
      if (hexInput) hexInput.value = bed.color;
      markCustom();
      this._commitStrataChange(layer, { fast: true, rerender: false });
    });
    colorInput?.addEventListener('change', () => this._commitStrataChange(layer));

    root.querySelector('[data-strata-bed-color-hex]')?.addEventListener('change', (event) => {
      const bed = getSelected();
      if (!bed) return;
      bed.color = normalizeHexColor(event.target.value, bed.color);
      markCustom();
      this._commitStrataChange(layer);
    });

    root.querySelectorAll('[data-strata-bed-param]').forEach((input) => {
      const key = input.dataset.strataBedParam;
      const def = STRATA_BED_CONTROLS.find((entry) => entry.key === key);
      input.addEventListener('input', () => {
        const bed = getSelected();
        if (!bed) return;
        bed[key] = this._updateRangeOutput(input, def);
        markCustom();
        this._commitStrataChange(layer, { fast: true, rerender: false });
      });
      input.addEventListener('change', () => this._commitStrataChange(layer));
    });

    root.querySelectorAll('[data-strata-bed-move]').forEach((button) => {
      button.addEventListener('click', () => {
        const beds = getBeds();
        const index = selectedIndexIn(beds);
        const nextIndex = index + Number(button.dataset.strataBedMove);
        if (nextIndex < 0 || nextIndex >= beds.length) return;
        [beds[index], beds[nextIndex]] = [beds[nextIndex], beds[index]];
        this.selectedStrataBedId = beds[nextIndex].id;
        markCustom();
        this._commitStrataChange(layer);
      });
    });

    root.querySelector('[data-strata-bed-duplicate]')?.addEventListener('click', () => {
      const beds = getBeds();
      if (beds.length >= MAX_STRATA_BEDS) return;
      const index = selectedIndexIn(beds);
      const source = beds[index];
      const copy = createStrataBed(source.lithology, { ...source, id: undefined, name: `${source.name} copy` });
      beds.splice(index + 1, 0, copy);
      this.selectedStrataBedId = copy.id;
      markCustom();
      this._commitStrataChange(layer);
    });

    root.querySelector('[data-strata-bed-delete]')?.addEventListener('click', () => {
      const beds = getBeds();
      if (beds.length <= 1) return;
      const index = selectedIndexIn(beds);
      beds.splice(index, 1);
      this.selectedStrataBedId = beds[Math.min(index, beds.length - 1)]?.id ?? null;
      markCustom();
      this._commitStrataChange(layer);
    });
  }

  renderInspector() {
    if (!this.elInspector) return;
    const layer = this.pipeline.getSelectedLayer();
    if (!layer) {
      this.elInspector.innerHTML = `<div class="tp-empty-state">Select a layer in the Outliner to inspect its parameters and spatial mask.</div>`;
      return;
    }

    const layers = this.pipeline.layers;
    const idx = layers.indexOf(layer);
    const environmentLayer = isEnvironmentLayer(layer);
    const terrainLayerCount = layers.filter((candidate) => !isEnvironmentLayer(candidate)).length;
    const stepNum = layers.slice(idx).filter((candidate) => !isEnvironmentLayer(candidate)).length;
    const positionLabel = environmentLayer
      ? 'Locked last · below Base Landform'
      : `Step #${stepNum} of ${terrainLayerCount}`;
    const spec = LAYER_OPERATION_SPECS[layer.type] || LAYER_OPERATION_SPECS['mountain-ridges'];
    const cat = LAYER_CATEGORIES[spec.category] || LAYER_CATEGORIES.landform;
    const iconColor = layerIconColor(layer.type, cat);
    const schema = INSPECTOR_SCHEMAS[layer.type] || { selects: [], sliders: [], hasSeed: false };
    const iconSvg = SVG_ICONS[spec.glyph] || SVG_ICONS.mountain;
    const specLabel = spec.defaultName || layer.type;
    const specDesc = OP_DESCRIPTIONS[layer.type] || '';
    const supportsMask = !environmentLayer && layer.type !== 'base-landform';

    const readsBadges = (spec.reads.length ? spec.reads : ['domain XZ']).map((r) => `<span class="tp-io-pill read">R: ${r}</span>`).join('');
    const writesBadges = spec.writes.map((w) => `<span class="tp-io-pill write">W: ${w}</span>`).join('');

    let controlsHtml = '';
    if (environmentLayer) {
      controlsHtml += `
        <p class="tp-card-note tp-environment-lock-note">${SVG_ICONS.lock}<span><strong>Locked as the last layer.</strong> Environment cannot be added, duplicated, moved, muted, masked or deleted elsewhere. Sun and seasonal-climate changes update live state without rebuilding or erasing terrain; the climate calendar advances only while a simulation process is running.</span></p>`;
    }
    for (const sel of schema.selects) {
      const curVal = layer.params[sel.key] ?? 0;
      controlsHtml += `
        <label class="tp-field-row">
          <span>${sel.label}</span>
          <select class="tp-select" data-param-select="${sel.key}">
            ${sel.options.map((o) => `<option value="${o.value}" ${Number(curVal) === Number(o.value) ? 'selected' : ''}>${o.label}</option>`).join('')}
          </select>
        </label>`;
    }

    for (const sl of schema.sliders) {
      const val = layer.params[sl.key] ?? sl.min;
      const pct = Math.max(0, Math.min(100, ((val - sl.min) / Math.max(1e-6, sl.max - sl.min)) * 100));
      controlsHtml += `
        <div class="tp-control-row">
          <div class="tp-control-label">
            <span>${sl.label}</span>
          </div>
          <div class="tp-amount" style="--fill:${pct.toFixed(1)}%">
            <input type="range" min="${sl.min}" max="${sl.max}" step="${sl.step}" value="${val}" data-param-slider="${sl.key}" />
            <output data-param-out="${sl.key}">${sl.fmt(val)}</output>
          </div>
        </div>`;
    }

    if (schema.hasSeed) {
      controlsHtml += `
        <div class="tp-seed-bar">
          <span>Procedural Seed</span>
          <div class="tp-seed-controls">
            <input type="number" class="tp-seed-input" value="${layer.params.seed ?? 4029}" data-param-seed="seed" />
            <button type="button" class="tp-mini-btn" data-random-seed="1">Randomize</button>
          </div>
        </div>`;
    }

    // Contextual Authoring Card for Sculpt Layer or Spline Layer
    let authoringCardHtml = '';
    if (layer.type === 'sculpt-layer') {
      const curT = this.pipeline.authorTool;
      const aboveSims = Boolean(this.pipeline.isSculptAboveSimulations?.(layer.id));
      authoringCardHtml = `
        <section class="tp-property-card">
          <h2><i style="background:${iconColor}"></i><span>Non-Destructive Sculpt Brushes</span></h2>
          <p class="tp-card-note">Paint raised/carved/smoothed landforms directly on the 3D terrain. Sculpt layers automatically place below Simulation layers so Hydraulic, Snowpack, Wind, Rockfall, and Chemical simulations erode your sculpted terrain naturally.</p>
          ${aboveSims ? '<p class="tp-card-note"><strong>Order note:</strong> This sculpt layer is currently above one or more Simulation layers, so baked passes run before it. Click <em>Move Below Simulations</em> or <em>Simulate on Sculpted Terrain</em> to place it below simulations.</p>' : ''}
          <div class="tp-brush-grid">
            <button type="button" class="${curT === 'sculpt-add' ? 'active' : ''}" data-set-tool="sculpt-add">+ Raise</button>
            <button type="button" class="${curT === 'sculpt-remove' ? 'active' : ''}" data-set-tool="sculpt-remove">− Carve</button>
            <button type="button" class="${curT === 'sculpt-smooth' ? 'active' : ''}" data-set-tool="sculpt-smooth">≈ Smooth</button>
            <button type="button" class="${curT === 'cave-carve' ? 'active' : ''}" data-set-tool="cave-carve">◔ Carve 3D Cave</button>
            <button type="button" class="${curT === 'cave-fill' ? 'active' : ''}" data-set-tool="cave-fill">● Fill 3D Cave</button>
            <button type="button" class="${curT === 'navigate' ? 'active' : ''}" data-set-tool="navigate">Orbit / Fly</button>
          </div>
          <div class="tp-card-footer-btns">
            <button type="button" class="tp-mini-btn active" id="tp-btn-simulate-sculpt">▶ Simulate on Sculpted Terrain</button>
            ${aboveSims ? '<button type="button" class="tp-mini-btn" id="tp-btn-sculpt-below-sims">↓ Move Below Simulations</button>' : ''}
            <button type="button" class="tp-mini-btn danger" id="tp-btn-clear-sculpt">Clear Layer Strokes (${layer.params.strokeCount || 0})</button>
          </div>
        </section>`;
    } else if (isSplineAuthoringLayer(layer)) {
      const pts = layer.params.points || [];
      const spModeLabel = layer.type === 'spline-landform'
        ? (SPLINE_LANDFORM_MODES[layer.params.mode ?? 0]?.label || 'spline landform')
        : (SPLINE_MODES[layer.params.mode ?? 0]?.label || 'canyon cut');
      const splineNote = layer.type === 'spline-landform' && Number(layer.params.mode ?? 0) === 4
        ? 'This path becomes a continuous 3D SDF tube under the heightfield, with portals where it exits the terrain. Point Y sets the bore height directly.'
        : layer.type === 'spline-landform'
          ? 'The smooth path is an influence mask: it can add a ridge, subtract a canyon, or grade a river/road. Point Y overrides the Start→End grade locally.'
          : 'Legacy spline behaviour is unchanged for existing terrains; Y only re-grades the Graded Road mode.';
      const usesY = this._splineUsesElevation(layer);
      const anyY = pts.some((pt) => Number.isFinite(pt?.[2]));
      if (this.selectedSplinePt < 0 || this.selectedSplinePt >= pts.length) this.selectedSplinePt = pts.length ? 0 : -1;
      const ptRows = pts.map((pt, i) => {
        const hasY = Number.isFinite(pt?.[2]);
        const autoY = Math.round(this._splineEffectiveY(layer, i));
        return `
          <div class="tp-spline-pt-row ${i === this.selectedSplinePt ? 'selected' : ''}">
            <button type="button" class="tp-pt-badge" data-pt-select="${i}" title="Select point ${i + 1} (shows the 3D gizmo)">${i + 1}</button>
            <input type="number" class="tp-pt-num" data-pt-axis="0" data-pt-idx="${i}" value="${Math.round(pt[0] ?? 0)}" step="1" title="Point ${i + 1} X — east-west position in metres" aria-label="Point ${i + 1} X" />
            <input type="number" class="tp-pt-num" data-pt-axis="1" data-pt-idx="${i}" value="${Math.round(pt[1] ?? 0)}" step="1" title="Point ${i + 1} Z — north-south position in metres" aria-label="Point ${i + 1} Z" />
            <input type="number" class="tp-pt-num" data-pt-axis="2" data-pt-idx="${i}" value="${hasY ? Math.round(pt[2]) : ''}" placeholder="${autoY}" step="1" ${usesY ? '' : 'disabled'} title="${usesY ? `Point ${i + 1} elevation Y in metres. Empty = automatic grade (currently ${autoY} m).` : 'This spline mode follows the terrain heightfield, so point elevation is not used.'}" aria-label="Point ${i + 1} elevation" />
            <button type="button" class="tp-pt-mini" data-pt-insert="${i}" ${pts.length >= MAX_SPLINE_POINTS ? 'disabled' : ''} title="Insert a point after point ${i + 1}">+</button>
            <button type="button" class="tp-pt-mini danger" data-pt-delete="${i}" ${pts.length <= 2 ? 'disabled' : ''} title="Delete point ${i + 1}">×</button>
          </div>`;
      }).join('');
      authoringCardHtml = `
        <section class="tp-property-card">
          <h2><i style="background:${iconColor}"></i><span>3D Spline Control Path (${pts.length}/${MAX_SPLINE_POINTS} pts)</span></h2>
          <p class="tp-card-note">Select a point, then drag it on the terrain or pull the gizmo axes — <b style="color:#ff8a7a">X</b> east-west, <b style="color:#65e6b2">Y</b> elevation, <b style="color:#7ab8ff">Z</b> north-south — to steer this ${spModeLabel.toLowerCase()}. ${splineNote} Empty Y keeps the automatic grade; Delete removes the selected point.</p>
          <div class="tp-spline-pt-head"><span>#</span><span>X (m)</span><span>Z (m)</span><span>Y elev (m)</span><span></span><span></span></div>
          <div class="tp-spline-pt-list">${ptRows}</div>
          <div class="tp-card-footer-btns">
            <button type="button" class="tp-mini-btn ${this.pipeline.authorTool === 'spline-edit' ? 'active' : ''}" data-set-tool="spline-edit">Edit Points in Viewport</button>
            <button type="button" class="tp-mini-btn" id="tp-btn-add-spline-pt" ${pts.length >= MAX_SPLINE_POINTS ? 'disabled' : ''}>+ Append Point</button>
            <button type="button" class="tp-mini-btn" id="tp-btn-spline-subdivide" ${pts.length >= MAX_SPLINE_POINTS ? 'disabled' : ''} title="Insert smooth midpoints along segments (up to 16 points)">Subdivide</button>
            <button type="button" class="tp-mini-btn" id="tp-btn-spline-smooth" ${pts.length < 3 ? 'disabled' : ''} title="Gently relax interior control points for smooth natural meanders or road curves">Smooth Path</button>
            <button type="button" class="tp-mini-btn" id="tp-btn-pop-spline-pt" ${pts.length <= 2 ? 'disabled' : ''}>− Remove Last</button>
            ${usesY && !anyY ? '<button type="button" class="tp-mini-btn" id="tp-btn-spline-bake-y" title="Write the current automatic grade into every point so each height can be edited or dragged individually">Bake Elevations to Points</button>': ''}
            ${anyY ? '<button type="button" class="tp-mini-btn" id="tp-btn-spline-clear-y" title="Remove authored per-point elevations; the path returns to the automatic start→end grade">Clear Elevations</button>' : ''}
          </div>
        </section>`;
    }

    const seasonalClimateCardHtml = environmentLayer ? this._seasonalClimateCardHtml(layer, iconColor) : '';
    const snowFrostCardHtml = layer.type === 'aeolian-glacial' ? this._snowFrostCardHtml(layer, iconColor) : '';
    const strataColumnCardHtml = layer.type === 'stratigraphic-beds' ? this._strataColumnCardHtml(layer, iconColor) : '';

    // Per-layer season trigger: simulate ONLY this layer's process(es).
    let simCardHtml = '';
    const layerSeasonIds = this.pipeline.simulationProcessesForLayer(layer);
    if (layerSeasonIds.length) {
      const simState = this.pipeline.getSimulationControlState();
      const seasonButtons = layerSeasonIds.map((id) => {
        const proc = simState.processes.find((x) => x.id === id);
        if (!proc) return '';
        const label = proc.playing && simState.mode === 'solo' && proc.solo ? `⏸ Pause ${proc.short}` : `▶ Simulate ${proc.short} only`;
        return `<button type="button" class="tp-mini-btn ${proc.playing || proc.solo ? 'active' : ''}" data-simulate-process="${proc.id}" ${proc.available ? '' : 'disabled'}>${label}</button>`;
      }).join('');
      simCardHtml = `
        <section class="tp-property-card">
          <h2><i style="background:#8ab49a"></i><span>Simulate This Season</span></h2>
          <p class="tp-card-note">Runs only this process; snow, rain, wind, rockfall and chemical seasons stay frozen unless Simultaneous mode is chosen in the Simulation Seasons panel. Editing the base stack auto-pauses again.</p>
          <div class="tp-card-footer-btns">${seasonButtons}</div>
          ${layer.type === 'chemical-weathering' ? '<p class="tp-card-note">Tip: run a Rain season first (or Simultaneous) — dissolved Fe/Cu stains follow the water from the mineral beds set on Stratigraphic Beds layers.</p>' : ''}
          ${layer.type === 'wind-erosion' ? '<p class="tp-card-note">This is the independent Wind season. Set its prevailing direction plus Direction Over Time — fixed, a prevailing-weighted variable wind rose, smooth weather-front shifts or an equal full wind rose — along with speed, abrasion, turbulence, sand supply, undercut height and fluting. Direction changes are weather spells joined by gradual veer/back transitions, never instant compass jumps. Snow drift uses the current effective wind only when Snow and Wind run simultaneously.</p>' : ''}
          ${layer.type === 'aeolian-glacial' && layerSeasonIds.includes('wind') ? '<p class="tp-card-note">Legacy combined stack: this snow layer can still drive Wind until a dedicated Wind Erosion layer is added.</p>' : ''}
        </section>`;
    }

    // Wind Flow Debug Card: prevailing-wind readout, vector-field toggle and
    // one-click "wind FROM <compass>" azimuth setters.
    let windDebugCardHtml = '';
    if (layer.type === 'wind-erosion') {
      const fromButtons = [[0, 'From N'], [45, 'From NE'], [90, 'From E'], [135, 'From SE'], [180, 'From S'], [225, 'From SW'], [270, 'From W'], [315, 'From NW']]
        .map(([bearing, label]) => {
          const active = azimuthForWindFromBearing(bearing) === Math.round(layer.params.windAzimuth ?? NaN);
          return `<button type="button" class="${active ? 'active' : ''}" data-wind-from="${bearing}" title="Set Prevailing Wind Direction so the wind blows from the ${label.slice(5)}">${label}</button>`;
        }).join('');
      windDebugCardHtml = `
        <section class="tp-property-card">
          <h2><i style="background:#7ab8ff"></i><span>Wind Flow Debug</span></h2>
          <p class="tp-card-note" id="tp-wind-debug-readout"></p>
          <label class="tp-check-row">
            <input type="checkbox" id="tp-chk-wind-debug" ${this.showWindDebug ? 'checked' : ''} />
            <span>Show wind vector field on terrain</span>
          </label>
          <div class="tp-wind-from-grid">${fromButtons}</div>
          <div class="tp-wind-legend"></div>
          <div class="tp-wind-legend-labels"><span>Sheltered · slow</span><span>Fast · erosive</span></div>
          <p class="tp-card-note">Arrows preview the actual erosion flow at the current simulation step (same flow model as the simulation): the base direction follows Direction Over Time, moving gusts advect across the terrain, and wrap-around flow bends around massifs, contours and saddles/valleys (strength = Wrap Around Terrain slider; at 0% it rides straight over). Run the Wind season and the field persists through a weather spell, then veers or backs gradually through the intervening bearings into the next sector; larger turns take longer. Length &amp; colour show local speed with moving gusts, crest/flank speedup, immediate lee sheltering and downstream wake recovery — long/warm is erosive, short/cool marks sheltered deposition. Faint rings = wind erosion blocked by water or snow. Compass: −Z = North, +X = East.</p>
        </section>`;
    }

    // Black & White Spatial Mask Card
    let maskCardHtml = '';
    if (supportsMask && layer.mask) {
      const m = layer.mask;
      const pctMin = (m.min * 100).toFixed(1);
      const pctMax = (m.max * 100).toFixed(1);
      const pctFeat = ((m.feather / 0.5) * 100).toFixed(1);
      maskCardHtml = `
        <section class="tp-property-card">
          <div class="tp-card-head-row">
            <h2><i style="background:#d9b281"></i><span>Black &amp; White Spatial Mask</span></h2>
            <div class="tp-mask-head-actions">
              <button type="button" class="tp-mini-btn ${this.pipeline.shadingMode === 5 ? 'active' : ''}" id="tp-btn-inspect-mask">B&amp;W View</button>
              <label class="tp-switch">
                <input type="checkbox" id="tp-chk-mask-enabled" ${m.enabled ? 'checked' : ''} />
                <span>${m.enabled ? 'On' : 'Off'}</span>
              </label>
            </div>
          </div>
          <div class="tp-mask-controls ${m.enabled ? '' : 'disabled-group'}">
            <label class="tp-field-row">
              <span>Mask Driver</span>
              <select class="tp-select" id="tp-mask-mode">
                <option value="height" ${m.mode === 'height' ? 'selected' : ''}>Elevation Band</option>
                <option value="slope" ${m.mode === 'slope' ? 'selected' : ''}>Slope Steepness</option>
                <option value="flow" ${m.mode === 'flow' ? 'selected' : ''}>Drainage Flow</option>
                <option value="concavity" ${m.mode === 'concavity' ? 'selected' : ''}>Concavity / Hollows</option>
                <option value="radial" ${m.mode === 'radial' ? 'selected' : ''}>Radial Distance</option>
                <option value="noise" ${m.mode === 'noise' ? 'selected' : ''}>Procedural Noise</option>
              </select>
            </label>
            <div class="tp-control-row">
              <div class="tp-control-label"><span>Range Min</span></div>
              <div class="tp-amount" style="--fill:${pctMin}%">
                <input type="range" min="0" max="1" step="0.01" value="${m.min}" data-mask-slider="min" />
                <output data-mask-out="min">${Math.round(m.min * 100)}%</output>
              </div>
            </div>
            <div class="tp-control-row">
              <div class="tp-control-label"><span>Range Max</span></div>
              <div class="tp-amount" style="--fill:${pctMax}%">
                <input type="range" min="0" max="1" step="0.01" value="${m.max}" data-mask-slider="max" />
                <output data-mask-out="max">${Math.round(m.max * 100)}%</output>
              </div>
            </div>
            <div class="tp-control-row">
              <div class="tp-control-label"><span>Edge Feather</span></div>
              <div class="tp-amount" style="--fill:${pctFeat}%">
                <input type="range" min="0.02" max="0.50" step="0.01" value="${m.feather}" data-mask-slider="feather" />
                <output data-mask-out="feather">${Math.round(m.feather * 100)}%</output>
              </div>
            </div>
            <label class="tp-check-row">
              <input type="checkbox" id="tp-chk-mask-invert" ${m.invert ? 'checked' : ''} />
              <span>Invert Black &amp; White Mask</span>
            </label>
          </div>
        </section>`;
    }

    // Global Derived Surface Shading card. Sun direction is intentionally
    // absent here: the locked final Environment layer is its single source
    // of truth, so the two controls can never drift apart.
    const curFab = this.pipeline.fabricMode ?? 0;
    const fabOpts = Object.values(DERIVED_FABRICS)
      .map((f) => `<option value="${f.id}" ${Number(curFab) === f.id ? 'selected' : ''}>${f.label}</option>`)
      .join('');

    const shadingCardHtml = `
      <details class="tp-property-card tp-collapsible-card" id="tp-shading-card" ${this.shadingCardOpen ? 'open' : ''}>
        <summary class="tp-card-summary">
          <span class="tp-card-summary-title"><i style="background:#82939e"></i>Derived Surface Shading</span>
          <span class="tp-card-chevron" aria-hidden="true">▾</span>
        </summary>
        <p class="tp-card-note">Global viewport shading from AO, height, hardness, sediment and flow — independent of the selected layer. Sun direction lives on the locked Environment layer at the end of the stack.</p>
        <div class="tp-control-row">
          <div class="tp-control-label"><span>Rock Micro-Fabric</span></div>
          <select class="tp-select" id="tp-select-fabric">${fabOpts}</select>
        </div>
        <label class="tp-check-row">
          <input type="checkbox" id="tp-chk-show-water" ${this.engine.params.showWater !== false ? 'checked' : ''} />
          <span>Render 2.5D River &amp; Basin Water</span>
        </label>
        <label class="tp-check-row">
          <input type="checkbox" id="tp-chk-show-snow" ${this.engine.params.showSnow !== false ? 'checked' : ''} />
          <span>Render Snowpack, Glacier Ice &amp; Snowfall</span>
        </label>
        <label class="tp-check-row">
          <input type="checkbox" id="tp-chk-show-rain" ${this.engine.params.showRain !== false ? 'checked' : ''} />
          <span>Show Rain / Runoff Particles</span>
        </label>
        <label class="tp-check-row">
          <input type="checkbox" id="tp-chk-show-particles" ${this.engine.params.showParticles !== false ? 'checked' : ''} />
          <span>Show All 3D Weather Particles</span>
        </label>
      </details>`;

    this.elInspector.innerHTML = `
      <div class="tp-inspector-trail">
        <span>Inspector</span>
        <i>/</i>
        <span>${cat.label}</span>
        <i>›</i>
        <span>${positionLabel}</span>
      </div>

      <div class="tp-inspector-identity">
        <div class="tp-identity-badge" style="--cat-accent:${iconColor}">
          ${iconSvg}
        </div>
        <div class="tp-identity-copy">
          <span class="tp-eyebrow" style="color:${iconColor}">${cat.eyebrow || cat.label.toUpperCase()}</span>
          <input type="text" class="tp-identity-title-input ${environmentLayer ? 'locked' : ''}" id="tp-input-layer-name" value="${this._esc(environmentLayer ? ENVIRONMENT_LAYER_NAME : layer.name)}" ${environmentLayer ? 'readonly aria-readonly="true" title="Environment is a locked system layer"' : ''} />
          <p class="tp-identity-desc">${specDesc}</p>
        </div>
      </div>

      <div class="tp-inspector-status">
        ${readsBadges}
        ${writesBadges}
      </div>

      <section class="tp-property-card">
        <h2><i style="background:${iconColor}"></i><span>${specLabel} Parameters</span></h2>
        ${controlsHtml}
      </section>

      ${authoringCardHtml}
      ${seasonalClimateCardHtml}
      ${snowFrostCardHtml}
      ${strataColumnCardHtml}
      ${simCardHtml}
      ${windDebugCardHtml}
      ${maskCardHtml}
      ${shadingCardHtml}
    `;

    // Wire Inspector Event Listeners
    const nameInput = document.getElementById('tp-input-layer-name');
    if (nameInput) {
      nameInput.addEventListener('change', () => {
        if (environmentLayer) {
          layer.name = ENVIRONMENT_LAYER_NAME;
          nameInput.value = ENVIRONMENT_LAYER_NAME;
          this.renderStackList();
          return;
        }
        layer.name = nameInput.value.trim() || specLabel;
        this.renderStackList();
      });
    }

    this.elInspector.querySelectorAll('[data-param-select]').forEach((sel) => {
      sel.addEventListener('change', () => {
        const key = sel.dataset.paramSelect;
        layer.params[key] = Number(sel.value);
        // Wind direction regime is a live season setting: changing it must
        // not rebuild (and erase) the terrain already eroded this season.
        // Environment is likewise live render state, never a stack rebuild.
        if (environmentLayer) {
          this.pipeline.applyLiveEnvironmentLayerParams(layer);
        } else {
          const isLiveWindSelect = layer.type === 'wind-erosion' && key === 'windShiftMode';
          if (!(isLiveWindSelect && this.pipeline.applyLiveWindLayerParams(layer))) {
            this.pipeline.evaluateStack({ reseedPlacements: true });
          }
        }
        this._windFieldBuiltAt = 0;
        this.renderStackList();
        this.updateSplineGizmoOverlay();
        this.updateWindDebugOverlay();
        this.updateSceneCaption();
        if (environmentLayer) {
          this.renderInspector();
          this.renderSimulationPanel();
        }
      });
    });

    this.elInspector.querySelectorAll('[data-param-slider]').forEach((inp) => {
      const key = inp.dataset.paramSlider;
      const slDef = schema.sliders.find((s) => s.key === key);
      const out = this.elInspector.querySelector(`[data-param-out="${key}"]`);
      inp.addEventListener('input', () => {
        const v = Number(inp.value);
        layer.params[key] = v;
        if (out && slDef) out.textContent = slDef.fmt(v);
        const pct = ((v - Number(inp.min)) / Math.max(1e-6, Number(inp.max) - Number(inp.min))) * 100;
        inp.parentElement.style.setProperty('--fill', `${pct.toFixed(1)}%`);
        const isLiveEnvironmentParam = environmentLayer && Boolean(slDef?.liveParam || schema.liveOnly);
        if (isLiveEnvironmentParam) {
          this.pipeline.applyLiveEnvironmentLayerParams(layer);
          if (out && slDef) out.textContent = slDef.fmt(layer.params[key]);
          const summaryEl = this.elList?.querySelector(`[data-layer-id="${layer.id}"] .tp-layer-sub`);
          if (summaryEl) summaryEl.textContent = summarizeLayer(layer);
          this.updateSceneCaption();
          return;
        }
        const isLiveWindParam = layer.type === 'wind-erosion' && key !== 'bakeSteps';
        if (!(isLiveWindParam && this.pipeline.applyLiveWindLayerParams(layer))) {
          this.pipeline.evaluateStack({
            reseedPlacements: layer.type === 'rock-placement',
            fastPreview: true,
          });
        }
        this.updateSceneCaption();
      });
      inp.addEventListener('change', () => {
        const isLiveEnvironmentParam = environmentLayer && Boolean(slDef?.liveParam || schema.liveOnly);
        if (isLiveEnvironmentParam) {
          this.pipeline.applyLiveEnvironmentLayerParams(layer);
          this.renderStackList();
          this.updateSceneCaption();
          this.renderInspector();
          this.renderSimulationPanel();
          return;
        }
        const isLiveWindParam = layer.type === 'wind-erosion' && key !== 'bakeSteps';
        if (isLiveWindParam && this.pipeline.applyLiveWindLayerParams(layer)) {
          this.renderStackList();
          return;
        }
        this.pipeline.evaluateStack({ reseedPlacements: true, fastPreview: false });
        this.renderStackList();
      });
    });

    const seedInp = this.elInspector.querySelector('[data-param-seed]');
    if (seedInp) {
      seedInp.addEventListener('change', () => {
        layer.params.seed = Math.round(Number(seedInp.value) || 1);
        this.pipeline.evaluateStack({ reseedPlacements: true });
        this.renderStackList();
      });
    }
    const rndBtn = this.elInspector.querySelector('[data-random-seed]');
    if (rndBtn) {
      rndBtn.addEventListener('click', () => {
        layer.params.seed = Math.floor(1000 + Math.random() * 8999);
        if (seedInp) seedInp.value = String(layer.params.seed);
        this.pipeline.evaluateStack({ reseedPlacements: true });
        this.renderStackList();
      });
    }

    this._wireSnowFrostInspector(layer);
    this._wireStrataColumnInspector(layer);

    this.elInspector.querySelectorAll('[data-simulate-process]').forEach((b) => {
      b.addEventListener('click', () => {
        this.pipeline.toggleSimulation(b.dataset.simulateProcess);
        this.renderSimulationPanel();
        this._syncHeaderSimButton();
        this.renderInspector();
      });
    });

    this.elInspector.querySelectorAll('[data-set-tool]').forEach((b) => {
      b.addEventListener('click', () => {
        this.setAuthorTool(b.dataset.setTool);
        this.renderInspector();
      });
    });

    this.elInspector.querySelector('[data-env-seasonal-toggle]')?.addEventListener('click', () => {
      if (typeof this.pipeline.toggleSeasonalClimate === 'function') {
        this.pipeline.toggleSeasonalClimate();
      }
      this.renderStackList();
      this.renderSimulationPanel();
      this.renderInspector();
      this.updateSceneCaption();
    });

    const moveSculptBtn = document.getElementById('tp-btn-sculpt-below-sims');
    if (moveSculptBtn) {
      moveSculptBtn.addEventListener('click', () => {
        this.pipeline.moveSculptBelowSimulations?.(layer.id);
        this.renderAll();
      });
    }

    const simSculptBtn = document.getElementById('tp-btn-simulate-sculpt');
    if (simSculptBtn) {
      simSculptBtn.addEventListener('click', () => {
        if (this.pipeline.isSculptAboveSimulations?.(layer.id)) {
          this.pipeline.moveSculptBelowSimulations?.(layer.id);
        }
        this.pipeline.finishSculptStroke?.();
        this.setAuthorTool('navigate');
        this.pipeline.startSimulation(null);
        this.renderAll();
        this._syncHeaderSimButton();
      });
    }

    const clearSculptBtn = document.getElementById('tp-btn-clear-sculpt');
    if (clearSculptBtn) {
      clearSculptBtn.addEventListener('click', () => {
        this.pipeline.clearSculptLayer(layer.id);
        this.renderAll();
      });
    }

    const addPtBtn = document.getElementById('tp-btn-add-spline-pt');
    if (addPtBtn) {
      addPtBtn.addEventListener('click', () => {
        const pts = layer.params.points || [];
        if (pts.length >= MAX_SPLINE_POINTS) return;
        const last = pts[pts.length - 1] || [0, 0];
        const prev = pts[pts.length - 2] || [-40, -40];
        const nx = Math.max(-230, Math.min(230, last[0] + (last[0] - prev[0]) * 0.6));
        const nz = Math.max(-230, Math.min(230, last[1] + (last[1] - prev[1]) * 0.6));
        const newPt = [Math.round(nx), Math.round(nz)];
        if (Number.isFinite(last[2])) newPt.push(last[2]);
        pts.push(newPt);
        this.selectedSplinePt = pts.length - 1;
        this.pipeline.evaluateStack({ reseedPlacements: true });
        this.renderAll();
      });
    }

    const subdivPtBtn = document.getElementById('tp-btn-spline-subdivide');
    if (subdivPtBtn) {
      subdivPtBtn.addEventListener('click', () => {
        const pts = layer.params.points || [];
        if (pts.length < 2 || pts.length >= MAX_SPLINE_POINTS) return;
        const halfSpan = (this.engine.params.worldSpan || 500) * 0.5;
        const clampXZ = (v) => Math.max(-halfSpan, Math.min(halfSpan, v));
        const out = [pts[0]];
        for (let i = 0; i < pts.length - 1; i++) {
          const a = pts[i];
          const b = pts[i + 1];
          const remainingBudget = MAX_SPLINE_POINTS - (out.length + (pts.length - 1 - i));
          if (remainingBudget > 0) {
            const p0 = pts[Math.max(0, i - 1)];
            const p3 = pts[Math.min(pts.length - 1, i + 2)];
            const mx = 0.5 * (a[0] + b[0]) + 0.0625 * ((a[0] - p0[0]) + (b[0] - p3[0]));
            const mz = 0.5 * (a[1] + b[1]) + 0.0625 * ((a[1] - p0[1]) + (b[1] - p3[1]));
            const mid = [Math.round(clampXZ(mx)), Math.round(clampXZ(mz))];
            if (Number.isFinite(a[2]) && Number.isFinite(b[2])) {
              mid.push(Math.round((a[2] + b[2]) * 0.5));
            }
            out.push(mid);
          }
          out.push(b);
        }
        layer.params.points = out.slice(0, MAX_SPLINE_POINTS);
        this.selectedSplinePt = Math.min(this.selectedSplinePt, layer.params.points.length - 1);
        this.pipeline.evaluateStack({ reseedPlacements: true });
        this.renderAll();
      });
    }

    const smoothPtBtn = document.getElementById('tp-btn-spline-smooth');
    if (smoothPtBtn) {
      smoothPtBtn.addEventListener('click', () => {
        const pts = layer.params.points || [];
        if (pts.length < 3) return;
        const next = pts.map((pt) => pt.slice());
        for (let i = 1; i < pts.length - 1; i++) {
          next[i][0] = Math.round(0.25 * pts[i - 1][0] + 0.5 * pts[i][0] + 0.25 * pts[i + 1][0]);
          next[i][1] = Math.round(0.25 * pts[i - 1][1] + 0.5 * pts[i][1] + 0.25 * pts[i + 1][1]);
          if (Number.isFinite(pts[i - 1][2]) && Number.isFinite(pts[i][2]) && Number.isFinite(pts[i + 1][2])) {
            next[i][2] = Math.round(0.25 * pts[i - 1][2] + 0.5 * pts[i][2] + 0.25 * pts[i + 1][2]);
          }
        }
        layer.params.points = next;
        this.pipeline.evaluateStack({ reseedPlacements: true });
        this.renderAll();
      });
    }

    const popPtBtn = document.getElementById('tp-btn-pop-spline-pt');
    if (popPtBtn) {
      popPtBtn.addEventListener('click', () => {
        const pts = layer.params.points || [];
        if (pts.length <= 2) return;
        pts.pop();
        this.selectedSplinePt = Math.max(0, Math.min(this.selectedSplinePt, pts.length - 1));
        this.pipeline.evaluateStack({ reseedPlacements: true });
        this.renderAll();
      });
    }

    // Per-point numeric editor: X / Z / Y fields, select, insert, delete.
    if (isSplineAuthoringLayer(layer) && this.elInspector) {
      const halfSpan = (this.engine.params.worldSpan || 500) * 0.5;
      const clampXZ = (v) => Math.max(-halfSpan, Math.min(halfSpan, v));

      this.elInspector.querySelectorAll('input[data-pt-axis]').forEach((input) => {
        input.addEventListener('change', () => {
          const idx = Number(input.dataset.ptIdx);
          const axis = Number(input.dataset.ptAxis);
          const pt = (layer.params.points || [])[idx];
          if (!pt) return;
          if (axis === 2) {
            const raw = String(input.value).trim();
            if (raw === '') {
              pt.length = 2; // back to the automatic start->end grade
            } else {
              const v = Number(raw);
              if (!Number.isFinite(v)) { this.renderInspector(); return; }
              pt[2] = Math.max(1, Math.min(300, Math.round(v)));
            }
          } else {
            const v = Number(input.value);
            if (!Number.isFinite(v)) { this.renderInspector(); return; }
            pt[axis] = Math.round(clampXZ(v));
          }
          this.selectedSplinePt = idx;
          this.pipeline.evaluateStack({ reseedPlacements: true });
          this.renderStackList();
          this.renderInspector();
          this.updateSplineGizmoOverlay();
        });
      });

      this.elInspector.querySelectorAll('[data-pt-select]').forEach((btn) => {
        btn.addEventListener('click', () => {
          this.selectedSplinePt = Number(btn.dataset.ptSelect);
          if (this.pipeline.authorTool !== 'spline-edit') this.setAuthorTool('spline-edit');
          this.renderInspector();
          this.updateSplineGizmoOverlay();
        });
      });

      this.elInspector.querySelectorAll('[data-pt-insert]').forEach((btn) => {
        btn.addEventListener('click', () => {
          const pts = layer.params.points || [];
          if (pts.length >= MAX_SPLINE_POINTS) return;
          const idx = Number(btn.dataset.ptInsert);
          const a = pts[idx];
          if (!a) return;
          const isLast = idx === pts.length - 1;
          const b = isLast ? (pts[idx - 1] || a) : pts[idx + 1];
          let nx;
          let nz;
          if (isLast) {
            nx = clampXZ(a[0] + (a[0] - (b[0] || 0)) * 0.5);
            nz = clampXZ(a[1] + (a[1] - (b[1] || 0)) * 0.5);
          } else {
            nx = ((a[0] || 0) + (b[0] || 0)) / 2;
            nz = ((a[1] || 0) + (b[1] || 0)) / 2;
          }
          const mid = [Math.round(nx), Math.round(nz)];
          if (!isLast && Number.isFinite(a[2]) && Number.isFinite(b[2])) mid.push(Math.round((a[2] + b[2]) / 2));
          else if (isLast && Number.isFinite(a[2])) mid.push(a[2]);
          pts.splice(idx + 1, 0, mid);
          this.selectedSplinePt = idx + 1;
          this.pipeline.evaluateStack({ reseedPlacements: true });
          this.renderAll();
        });
      });

      this.elInspector.querySelectorAll('[data-pt-delete]').forEach((btn) => {
        btn.addEventListener('click', () => {
          const pts = layer.params.points || [];
          if (pts.length <= 2) return;
          const idx = Number(btn.dataset.ptDelete);
          pts.splice(idx, 1);
          this.selectedSplinePt = Math.max(0, Math.min(idx, pts.length - 1));
          this.pipeline.evaluateStack({ reseedPlacements: true });
          this.renderAll();
        });
      });

      const bakeYBtn = document.getElementById('tp-btn-spline-bake-y');
      if (bakeYBtn) {
        bakeYBtn.addEventListener('click', () => {
          const pts = layer.params.points || [];
          pts.forEach((pt, i) => { pt[2] = Math.round(this._splineEffectiveY(layer, i)); });
          this.pipeline.evaluateStack({ reseedPlacements: true });
          this.renderAll();
        });
      }

      const clearYBtn = document.getElementById('tp-btn-spline-clear-y');
      if (clearYBtn) {
        clearYBtn.addEventListener('click', () => {
          const pts = layer.params.points || [];
          pts.forEach((pt) => { if (pt.length > 2) pt.length = 2; });
          this.pipeline.evaluateStack({ reseedPlacements: true });
          this.renderAll();
        });
      }
    }

    // Wind flow debug bindings
    const windDebugChk = document.getElementById('tp-chk-wind-debug');
    if (windDebugChk) {
      windDebugChk.addEventListener('change', () => {
        this.showWindDebug = !!windDebugChk.checked;
        this._windFieldBuiltAt = 0;
        this.updateWindDebugOverlay();
      });
    }
    if (layer.type === 'wind-erosion' && this.elInspector) {
      this.elInspector.querySelectorAll('[data-wind-from]').forEach((btn) => {
        btn.addEventListener('click', () => {
          layer.params.windAzimuth = azimuthForWindFromBearing(Number(btn.dataset.windFrom));
          if (!this.pipeline.applyLiveWindLayerParams(layer)) {
            this.pipeline.evaluateStack({ reseedPlacements: false });
          }
          this._windFieldBuiltAt = 0;
          this.renderStackList();
          this.renderInspector();
          this.updateWindDebugOverlay();
          this.updateSceneCaption();
        });
      });
    }

    // Mask bindings
    const chkMask = document.getElementById('tp-chk-mask-enabled');
    if (chkMask) {
      chkMask.addEventListener('change', () => {
        layer.mask.enabled = chkMask.checked;
        this.pipeline.evaluateStack({ reseedPlacements: false });
        this.renderAll();
      });
    }

    const btnInspMask = document.getElementById('tp-btn-inspect-mask');
    if (btnInspMask) {
      btnInspMask.addEventListener('click', () => {
        this.pipeline.shadingMode = this.pipeline.shadingMode === 5 ? 0 : 5;
        if (this.elShadingSelect) this.elShadingSelect.value = String(this.pipeline.shadingMode);
        this.pipeline.evaluateStack({ reseedPlacements: false, fastPreview: true });
        this.renderAll();
      });
    }

    const selMaskMode = document.getElementById('tp-mask-mode');
    if (selMaskMode) {
      selMaskMode.addEventListener('change', () => {
        layer.mask.mode = selMaskMode.value;
        this.pipeline.evaluateStack({ reseedPlacements: false });
        this.renderStackList();
      });
    }

    this.elInspector.querySelectorAll('[data-mask-slider]').forEach((inp) => {
      const k = inp.dataset.maskSlider;
      const out = this.elInspector.querySelector(`[data-mask-out="${k}"]`);
      inp.addEventListener('input', () => {
        const v = Number(inp.value);
        layer.mask[k] = v;
        if (out) out.textContent = `${Math.round(v * 100)}%`;
        const pct = ((v - Number(inp.min)) / Math.max(1e-6, Number(inp.max) - Number(inp.min))) * 100;
        inp.parentElement.style.setProperty('--fill', `${pct.toFixed(1)}%`);
        this.pipeline.evaluateStack({ reseedPlacements: false, fastPreview: true });
      });
      inp.addEventListener('change', () => {
        this.pipeline.evaluateStack({ reseedPlacements: true });
        this.renderStackList();
      });
    });

    const chkInv = document.getElementById('tp-chk-mask-invert');
    if (chkInv) {
      chkInv.addEventListener('change', () => {
        layer.mask.invert = chkInv.checked;
        this.pipeline.evaluateStack({ reseedPlacements: false });
        this.renderStackList();
      });
    }

    // Fabric, Water & Particle bindings. Sun controls are bound through the
    // Environment layer's live parameter sliders above.
    const selFab = document.getElementById('tp-select-fabric');
    if (selFab) {
      selFab.addEventListener('change', () => {
        this.pipeline.fabricMode = Number(selFab.value);
        this.engine.params.fabricMode = this.pipeline.fabricMode;
      });
    }
    const chkWater = document.getElementById('tp-chk-show-water');
    if (chkWater) {
      chkWater.addEventListener('change', () => {
        this.engine.params.showWater = chkWater.checked;
        const bar = document.getElementById('tp-vis-water');
        if (bar) bar.checked = chkWater.checked;
      });
    }
    const chkSnow = document.getElementById('tp-chk-show-snow');
    if (chkSnow) {
      chkSnow.addEventListener('change', () => {
        this.engine.params.showSnow = chkSnow.checked;
        const bar = document.getElementById('tp-vis-snow');
        if (bar) bar.checked = chkSnow.checked;
      });
    }
    const chkRain = document.getElementById('tp-chk-show-rain');
    if (chkRain) {
      chkRain.addEventListener('change', () => {
        this.engine.params.showRain = chkRain.checked;
        const bar = document.getElementById('tp-vis-rain');
        if (bar) bar.checked = chkRain.checked;
      });
    }
    const chkPart = document.getElementById('tp-chk-show-particles');
    if (chkPart) {
      chkPart.addEventListener('change', () => {
        this.engine.params.showParticles = chkPart.checked;
      });
    }
    const shadingCard = document.getElementById('tp-shading-card');
    if (shadingCard) {
      shadingCard.addEventListener('toggle', () => {
        this.shadingCardOpen = Boolean(shadingCard.open);
      });
    }

    enhanceAmountSliders(this.elInspector);
    enhanceSwitches(this.elInspector);
  }

  renderPresetShelf() {
    if (!this.elPresetGrid) return;
    let html = '';
    for (const preset of Object.values(TERRAIN_STACK_PRESETS)) {
      const isAct = preset.id === this.pipeline.activePresetId;
      const sampleLayers = preset.layers();
      html += `
        <button type="button" class="cb-preset-card ${isAct ? 'active' : ''}" data-preset-id="${preset.id}">
          <div class="cb-card-top">
            <span class="cb-badge">${preset.category || 'Stack Recipe'}</span>
            <span class="cb-pass-count">${sampleLayers.length} layers</span>
          </div>
          <div class="cb-card-title">${preset.name}</div>
          <div class="cb-card-sub">${preset.subtitle}</div>
        </button>`;
    }
    this.elPresetGrid.innerHTML = html;
    this.elPresetGrid.querySelectorAll('[data-preset-id]').forEach((card) => {
      card.addEventListener('click', () => {
        const id = card.dataset.presetId;
        if (this.pipeline.worldBaseMaskEnabled && !window.confirm('Load this recipe on the selected world terrain? It replaces the current terrain layers, sculpting and live erosion progress, but keeps the world mask and bounds.')) return;
        this.pipeline.loadPreset(id);
        this.renderAll();
        if (this.onPresetChanged) this.onPresetChanged(id);
      });
    });
  }

  // =========================================================================
  // WIND EROSION DEBUG VECTOR-FIELD OVERLAY
  // =========================================================================

  // Resolve the wind settings the debug field should preview: the wind layer
  // being inspected, else the stack's authoritative (topmost enabled) wind
  // layer, else the engine's synced wind params.
  _windDebugParams() {
    const sel = this.pipeline.getSelectedLayer();
    const layer = (sel && sel.type === 'wind-erosion')
      ? sel
      : (this.pipeline.layers || []).find((l) => l.enabled && l.type === 'wind-erosion') || null;
    const src = layer ? layer.params : this.engine.params;
    const num = (v, fb) => (Number.isFinite(Number(v)) ? Number(v) : fb);
    const regimeParams = {
      windAzimuth: num(src.windAzimuth, DEFAULT_EROSION_PARAMS.windAzimuth),
      windShiftMode: src.windShiftMode ?? DEFAULT_EROSION_PARAMS.windShiftMode,
      windShiftRange: num(src.windShiftRange, DEFAULT_EROSION_PARAMS.windShiftRange),
      windShiftPeriod: num(src.windShiftPeriod, DEFAULT_EROSION_PARAMS.windShiftPeriod),
      windShiftSectors: num(src.windShiftSectors, 5),
      windShiftEpisodes: src.windShiftEpisodes,
      // Match the GPU engine's p.seed exactly, otherwise the seeded sector
      // dwell schedule shown by the debug arrows could differ from erosion.
      seed: num(this.engine.params.seed, 1),
    };
    return {
      layer,
      // Arrows show the direction the simulation is using at the current
      // step, not merely the centre of the shifting regime.
      azimuthDeg: effectiveWindAzimuthAtStep(regimeParams, this.engine.stepCount || 0),
      prevailingAzimuthDeg: regimeParams.windAzimuth,
      shiftMode: normalizeWindShiftMode(regimeParams.windShiftMode),
      shiftRangeDeg: regimeParams.windShiftRange,
      shiftPeriodSteps: regimeParams.windShiftPeriod,
      regimeLabel: describeWindRegime(regimeParams),
      windSpeed: num(src.windSpeed, DEFAULT_EROSION_PARAMS.windSpeed),
      turbulence: num(src.windTurbulence, DEFAULT_EROSION_PARAMS.windTurbulence),
      streamlining: num(src.windStreamlining, DEFAULT_EROSION_PARAMS.windStreamlining),
      seed: num(this.engine.params.seed, 1),
      phase: this.engine.stepCount || 0,
    };
  }

  // Fast bilinear samplers over the CPU height/snow grids (total height and
  // the water/snow erosion gate), avoiding full sampleTerrain() cost per tap.
  _windHeightSampler() {
    const rc = this.engine.rockCalving;
    const g = rc?.heightGrid;
    const N = rc?.gridRes || 0;
    const span = rc?.worldSpan || this.engine.params.worldSpan || 500;
    if (!g || N < 2) return null;
    const sg = rc.snowGrid;
    const cellAt = (wx, wz) => {
      const u = Math.max(0.001, Math.min(0.999, wx / span + 0.5)) * (N - 1);
      const v = Math.max(0.001, Math.min(0.999, wz / span + 0.5)) * (N - 1);
      const ix = Math.min(N - 2, Math.max(0, Math.floor(u)));
      const iz = Math.min(N - 2, Math.max(0, Math.floor(v)));
      return { idx: (iz * N + ix) * 4, fx: u - ix, fz: v - iz };
    };
    const sample = (wx, wz) => {
      const { idx, fx, fz } = cellAt(wx, wz);
      const hAt = (o) => g[o] + g[o + 1] + g[o + 2];
      const a = hAt(idx) * (1 - fx) + hAt(idx + 4) * fx;
      const b = hAt(idx + N * 4) * (1 - fx) + hAt(idx + N * 4 + 4) * fx;
      return a * (1 - fz) + b * fz;
    };
    const gate = (wx, wz) => {
      const { idx } = cellAt(wx, wz);
      return { water: g[idx + 3] || 0, snow: sg ? (sg[idx] || 0) : 0 };
    };
    return { sample, gate, span };
  }

  _updateWindReadout() {
    const el = document.getElementById('tp-wind-debug-readout');
    if (!el) return;
    const wp = this._windDebugParams();
    const c = windCompassFromAzimuth(wp.azimuthDeg);
    const prevailing = windCompassFromAzimuth(wp.prevailingAzimuthDeg);
    // The simulation is driven by the topmost enabled Wind layer; say so when
    // the previewed (selected) layer is a different one, so the arrows are
    // never silently showing a field the sim won't use.
    const authority = (this.pipeline.layers || []).find((l) => l.enabled && l.type === 'wind-erosion');
    const note = wp.layer && authority && authority.id !== wp.layer.id
      ? ' Previewing the selected layer — the topmost Wind layer drives the simulation.'
      : '';
    const regime = wp.shiftMode === 'fixed'
      ? `Prevailing wind blows toward ${c.towardPoint} (${Math.round(c.towardDeg)}°) — a wind FROM ${c.fromPoint} (${Math.round(c.fromDeg)}°).`
      : `Wind is currently blowing toward ${c.towardPoint} (${Math.round(c.towardDeg)}°) — FROM ${c.fromPoint} (${Math.round(c.fromDeg)}°). It moves around the prevailing ${prevailing.towardPoint} centre as a ${wp.regimeLabel}; step ${this.engine.stepCount || 0}.`;
    const txt = `${regime} Speed ${Math.round((wp.windSpeed || 0) * 100)}%.${note}`;
    if (el.textContent !== txt) el.textContent = txt;
  }

  updateWindDebugOverlay() {
    // Keep the inspector readout fresh even while the field itself is hidden
    // (e.g. while dragging the azimuth slider).
    this._updateWindReadout();

    const canvas = this.elWindCanvas;
    if (!canvas || typeof canvas.getContext !== 'function') return;
    const wp = this._windDebugParams();
    const show = this.showWindDebug && this.engine.active && (!!wp.layer || (wp.windSpeed || 0) > 0.03);
    if (!show) {
      if (canvas.style.display !== 'none') canvas.style.display = 'none';
      return;
    }
    const sampler = this._windHeightSampler();
    if (!sampler) {
      canvas.style.display = 'none';
      return;
    }

    // Rebuild the CPU field when wind settings change (rate-limited so
    // slider drags stay smooth), and a few times a second while visible so
    // it tracks the eroding terrain.
    const now = performance.now();
    const key = [wp.azimuthDeg, wp.windSpeed, wp.turbulence, wp.streamlining, wp.seed, wp.phase, this.engine.gridRes].join('|');
    const age = now - this._windFieldBuiltAt;
    if (!this._windField || (key !== this._windFieldKey && age > 60) || age > 240) {
      this._windField = buildWindVectorField({
        heightAt: sampler.sample,
        gateAt: sampler.gate,
        span: sampler.span,
        divisions: 21,
        azimuthDeg: wp.azimuthDeg,
        windSpeed: wp.windSpeed,
        turbulence: wp.turbulence,
        streamlining: wp.streamlining,
        seed: wp.seed,
        phase: wp.phase,
        cellSize: sampler.span / Math.max(2, this.engine.gridRes || 512),
        lift: 5,
      });
      this._windFieldKey = key;
      this._windFieldBuiltAt = now;
    }

    const w = canvas.clientWidth || this.engine.canvas.clientWidth || 800;
    const h = canvas.clientHeight || this.engine.canvas.clientHeight || 600;
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    const pw = Math.max(2, Math.round(w * dpr));
    const ph = Math.max(2, Math.round(h * dpr));
    if (canvas.width !== pw || canvas.height !== ph) {
      canvas.width = pw;
      canvas.height = ph;
    }
    canvas.style.display = 'block';
    const ctx = canvas.getContext('2d');
    if (!ctx || typeof ctx.clearRect !== 'function') return;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);
    ctx.lineCap = 'round';

    const project = (x, y, z) => {
      const pr = this.pipeline.projectWorldToScreen(x, y, z);
      return pr ? { x: pr.u * w, y: pr.v * h } : null;
    };

    const arrows = this._windField.arrows;
    for (let i = 0; i < arrows.length; i++) {
      const a = arrows[i];
      const b = project(a.x, a.y, a.z);
      if (!b || b.x < -60 || b.x > w + 60 || b.y < -60 || b.y > h + 60) continue;

      if (!a.active) {
        // Wind erosion is gated off under water/snow here.
        ctx.strokeStyle = 'rgba(200,215,225,0.35)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.arc(b.x, b.y, 2.2, 0, 6.2832);
        ctx.stroke();
        continue;
      }

      const t = project(a.tipX, a.tipY, a.tipZ);
      if (!t) continue;
      const dx = t.x - b.x;
      const dy = t.y - b.y;
      const dl = Math.hypot(dx, dy);
      if (dl < 3) continue;

      const [r, g, bl] = windSpeedColor(a.speedNorm);
      const alpha = 0.35 + 0.60 * a.exposure;
      ctx.strokeStyle = `rgba(${r},${g},${bl},${alpha.toFixed(2)})`;
      ctx.fillStyle = ctx.strokeStyle;
      ctx.lineWidth = 1.4 + 1.1 * a.speedNorm;
      ctx.beginPath();
      ctx.moveTo(b.x, b.y);
      ctx.lineTo(t.x, t.y);
      ctx.stroke();

      const ux = dx / dl;
      const uy = dy / dl;
      const ah = Math.min(8, 3 + dl * 0.22);
      const aw = ah * 0.45;
      ctx.beginPath();
      ctx.moveTo(t.x, t.y);
      ctx.lineTo(t.x - ux * ah - uy * aw, t.y - uy * ah + ux * aw);
      ctx.lineTo(t.x - ux * ah + uy * aw, t.y - uy * ah - ux * aw);
      ctx.closePath();
      ctx.fill();

      // Travelling pulse so the flow direction reads at a glance.
      if (dl > 14) {
        const phase = (now * 0.00035 * (0.4 + a.speedNorm) + ((i * 37) % 101) / 101) % 1;
        ctx.fillStyle = 'rgba(255,255,255,0.85)';
        ctx.beginPath();
        ctx.arc(b.x + dx * phase, b.y + dy * phase, 1.6, 0, 6.2832);
        ctx.fill();
      }
    }
  }

  // =========================================================================
  // INTERACTIVE 3D SPLINE GIZMO OVERLAY
  // =========================================================================

  // Modes whose geometry actually consumes the longitudinal elevation
  // profile, i.e. where a point's Y (third coordinate) does something.
  _splineUsesElevation(layer) {
    if (!layer) return false;
    if (layer.type === 'spline-landform') return true; // ridge crest + canyon/river/road grades + tunnel bore
    if (layer.type === 'spline-feature') return Number(layer.params.mode ?? 0) === 1; // legacy graded road only
    return false;
  }

  // The elevation a control point effectively sits at before the user authors
  // an explicit Y: its slot on the start->end grade (or tunnel depth path).
  _splineEffectiveY(layer, idx) {
    const pts = layer?.params?.points || [];
    const pt = pts[idx];
    if (!pt) return 0;
    if (Number.isFinite(pt[2])) return pt[2];
    const t = pts.length > 1 ? idx / (pts.length - 1) : 0;
    const p = layer.params;
    if (layer.type === 'spline-landform') {
      if (Number(p.mode ?? 0) === 4 && Number(p.tunnelFollowSurface ?? 0) > 0.5) {
        const st = this.engine.rockCalving.sampleTerrain(pt[0], pt[1]);
        return st.surfaceH - Math.max(4, p.tunnelDepth ?? 22);
      }
      const a = p.startElevation ?? 78;
      const b = p.endElevation ?? 24;
      return a + (b - a) * t;
    }
    if (layer.type === 'spline-feature') {
      const mr = this.pipeline._maxReliefEstimate ? this.pipeline._maxReliefEstimate() : 180;
      return mr * (0.24 + (0.56 - 0.24) * t);
    }
    return this.engine.rockCalving.sampleTerrain(pt[0], pt[1]).surfaceH + 5.0;
  }

  // World-space position of a control point's gizmo handle: authored Y when
  // set, otherwise the effective grade height for elevation-driven modes or
  // a terrain drape for purely heightfield-relative modes.
  _splineHandleWorld(layer, idx) {
    const pt = layer.params.points[idx];
    const x = pt[0] || 0;
    const z = pt[1] || 0;
    let y;
    if (Number.isFinite(pt[2])) y = pt[2];
    else if (this._splineUsesElevation(layer)) y = this._splineEffectiveY(layer, idx);
    else y = this.engine.rockCalving.sampleTerrain(x, z).surfaceH + 5.0;
    return [x, y, z];
  }

  // Camera ray through a pointer position (same construction the engine's
  // XZ raycaster uses, but keeping the full 3D ray for axis dragging).
  _splinePointerRay(e) {
    const canvas = this.engine.canvas;
    const rect = canvas.getBoundingClientRect();
    if (!rect || rect.width < 2 || rect.height < 2) return null;
    const basis = this.engine._getCameraBasis ? this.engine._getCameraBasis() : null;
    if (!basis) return null;
    const { eye, forward, right, up } = basis;
    const ndcX = ((e.clientX - rect.left) / rect.width) * 2 - 1;
    const ndcY = 1 - ((e.clientY - rect.top) / rect.height) * 2;
    const aspect = rect.width / rect.height;
    const tanHalf = Math.tan(((this.engine.camera.fovDeg || 42) * Math.PI) / 360);
    const sx = ndcX * aspect * tanHalf;
    const sy = ndcY * tanHalf;
    let dx = forward[0] + right[0] * sx + up[0] * sy;
    let dy = forward[1] + right[1] * sx + up[1] * sy;
    let dz = forward[2] + right[2] * sx + up[2] * sy;
    const dl = Math.hypot(dx, dy, dz) || 1;
    return { origin: eye, dir: [dx / dl, dy / dl, dz / dl] };
  }

  // Parameter t of the point on an infinite axis line closest to a ray.
  _rayAxisParam(ray, axisOrigin, axisDir) {
    const [dx, dy, dz] = ray.dir;
    const [ax, ay, az] = axisDir;
    const wx = ray.origin[0] - axisOrigin[0];
    const wy = ray.origin[1] - axisOrigin[1];
    const wz = ray.origin[2] - axisOrigin[2];
    const b = dx * ax + dy * ay + dz * az;
    const d0 = dx * wx + dy * wy + dz * wz;
    const e0 = ax * wx + ay * wy + az * wz;
    const denom = 1 - b * b;
    if (Math.abs(denom) < 1e-6) return null;
    return (e0 - b * d0) / denom;
  }

  _bindSplineGizmoEvents() {
    if (!this.elSplineSvg) return;

    window.addEventListener('pointermove', (e) => {
      const sel = this.pipeline.getSelectedLayer();

      // Axis drag: slide the selected point along one world axis (X / Y / Z)
      // by tracking the closest point between the pointer ray and the axis.
      if (this.draggingSplineAxis && this._splineDrag) {
        if (!isSplineAuthoringLayer(sel)) {
          this.draggingSplineAxis = null;
          this._splineDrag = null;
          return;
        }
        const drag = this._splineDrag;
        const pt = sel.params.points?.[drag.idx];
        if (!pt) return;
        const ray = this._splinePointerRay(e);
        if (!ray) return;
        const t = this._rayAxisParam(ray, drag.origin, drag.dir);
        if (t === null || !Number.isFinite(t)) return;
        const delta = t - drag.startT;
        const half = (this.engine.params.worldSpan || 500) * 0.5;
        if (drag.axis === 'x') pt[0] = Math.max(-half, Math.min(half, Math.round(drag.origX + delta)));
        else if (drag.axis === 'z') pt[1] = Math.max(-half, Math.min(half, Math.round(drag.origZ + delta)));
        else if (drag.axis === 'y') pt[2] = Math.max(1, Math.min(300, Math.round((drag.origY + delta) * 2) / 2));
        this.pipeline.evaluateStack({ reseedPlacements: false, fastPreview: true });
        this.updateSplineGizmoOverlay();
        return;
      }

      // Centre-handle drag: move the point across the terrain (XZ), as before.
      if (this.draggingSplinePt < 0) return;
      if (!isSplineAuthoringLayer(sel)) {
        this.draggingSplinePt = -1;
        return;
      }
      const hit = this.engine._raycastPlaneXZ(e.clientX, e.clientY);
      if (hit && sel.params.points[this.draggingSplinePt]) {
        sel.params.points[this.draggingSplinePt][0] = Math.round(hit[0]);
        sel.params.points[this.draggingSplinePt][1] = Math.round(hit[1]);
        this.pipeline.evaluateStack({ reseedPlacements: false, fastPreview: true });
        this.updateSplineGizmoOverlay();
      }
    });

    window.addEventListener('pointerup', () => {
      if (this.draggingSplinePt >= 0 || this.draggingSplineAxis) {
        this.draggingSplinePt = -1;
        this.draggingSplineAxis = null;
        this._splineDrag = null;
        this.pipeline.evaluateStack({ reseedPlacements: true, fastPreview: false });
        this.renderStackList();
        this.renderInspector();
        this.updateSplineGizmoOverlay();
      }
    });

    // Delete removes the selected control point while spline editing.
    window.addEventListener('keydown', (e) => {
      if (e.key !== 'Delete' && e.key !== 'Backspace') return;
      const tag = e.target?.tagName;
      if (tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA') return;
      if (this.pipeline.authorTool !== 'spline-edit') return;
      const sel = this.pipeline.getSelectedLayer();
      if (!isSplineAuthoringLayer(sel)) return;
      const pts = sel.params.points || [];
      if (pts.length <= 2 || this.selectedSplinePt < 0 || this.selectedSplinePt >= pts.length) return;
      e.preventDefault();
      pts.splice(this.selectedSplinePt, 1);
      this.selectedSplinePt = Math.max(0, Math.min(this.selectedSplinePt, pts.length - 1));
      this.pipeline.evaluateStack({ reseedPlacements: true });
      this.renderAll();
    });
  }

  updateSplineGizmoOverlay() {
    const svg = this.elSplineSvg;
    if (!svg) return;
    const sel = this.pipeline.getSelectedLayer();
    const showGizmo =
      this.engine.active &&
      sel &&
      isSplineAuthoringLayer(sel) &&
      sel.enabled &&
      Array.isArray(sel.params.points) &&
      sel.params.points.length >= 2;

    if (!showGizmo) {
      if (svg.innerHTML !== '') svg.innerHTML = '';
      svg.style.pointerEvents = 'none';
      return;
    }

    const w = svg.clientWidth || this.engine.canvas.clientWidth || 800;
    const h = svg.clientHeight || this.engine.canvas.clientHeight || 600;
    svg.setAttribute('viewBox', `0 0 ${w} ${h}`);

    const pts = sel.params.points;
    if (this.selectedSplinePt < 0 || this.selectedSplinePt >= pts.length) this.selectedSplinePt = 0;

    const project = (wx, wy, wz) => {
      const pr = this.pipeline.projectWorldToScreen(wx, wy, wz);
      return pr ? { x: pr.u * w, y: pr.v * h } : null;
    };

    // Control handles sit at their true 3D positions; a dashed drop line to
    // the terrain surface keeps elevated / buried points readable.
    const worlds = pts.map((_, i) => this._splineHandleWorld(sel, i));
    const handles = [];
    worlds.forEach((world, i) => {
      const scr = project(world[0], world[1], world[2]);
      if (!scr) return;
      const surfY = this.engine.rockCalving.sampleTerrain(world[0], world[2]).surfaceH;
      const gnd = project(world[0], surfY, world[2]);
      handles.push({ i, world, surfY, x: scr.x, y: scr.y, gnd });
    });

    if (handles.length < 2) {
      svg.innerHTML = '';
      return;
    }

    // Path preview: smooth 3D Catmull-Rom for the landform spline (matching
    // the shader), straight segments for the legacy corridor spline.
    const pathPts = [];
    if (sel.type === 'spline-landform' && worlds.length >= 2) {
      const cr1 = (p0, p1, p2, p3, t) => {
        const t2 = t * t;
        const t3 = t2 * t;
        return 0.5 * ((2.0 * p1) + (-p0 + p2) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
      };
      const at = (idx) => worlds[Math.max(0, Math.min(worlds.length - 1, idx))];
      for (let i = 0; i < worlds.length - 1; i++) {
        for (let j = 0; j < 8; j++) {
          const t = j / 8;
          const s = project(
            cr1(at(i - 1)[0], worlds[i][0], worlds[i + 1][0], at(i + 2)[0], t),
            cr1(at(i - 1)[1], worlds[i][1], worlds[i + 1][1], at(i + 2)[1], t),
            cr1(at(i - 1)[2], worlds[i][2], worlds[i + 1][2], at(i + 2)[2], t)
          );
          if (s) pathPts.push(`${s.x.toFixed(1)},${s.y.toFixed(1)}`);
        }
      }
      const lastW = worlds[worlds.length - 1];
      const lastS = project(lastW[0], lastW[1], lastW[2]);
      if (lastS) pathPts.push(`${lastS.x.toFixed(1)},${lastS.y.toFixed(1)}`);
    } else {
      for (const p of handles) pathPts.push(`${p.x.toFixed(1)},${p.y.toFixed(1)}`);
    }

    let drops = '';
    for (const p of handles) {
      if (p.gnd && Math.abs(p.world[1] - p.surfY) > 2.5) {
        drops += `
          <line x1="${p.x.toFixed(1)}" y1="${p.y.toFixed(1)}" x2="${p.gnd.x.toFixed(1)}" y2="${p.gnd.y.toFixed(1)}" stroke="rgba(217,255,235,0.4)" stroke-width="1" stroke-dasharray="3 4" />
          <circle cx="${p.gnd.x.toFixed(1)}" cy="${p.gnd.y.toFixed(1)}" r="3" fill="none" stroke="rgba(217,255,235,0.55)" stroke-width="1" />`;
      }
    }

    // Translation gizmo on the selected point: X (east-west), Y (elevation,
    // only for modes that consume it) and Z (north-south) axis arrows.
    let axesSvg = '';
    const selH = handles.find((hh) => hh.i === this.selectedSplinePt);
    if (selH) {
      const axisLen = Math.max(10, Math.min(80, (this.engine.camera.distance || 700) * 0.085));
      const axes = [
        { id: 'x', dir: [1, 0, 0], color: '#ff7a6b', label: 'X' },
        ...(this._splineUsesElevation(sel) ? [{ id: 'y', dir: [0, 1, 0], color: '#65e6b2', label: 'Y' }] : []),
        { id: 'z', dir: [0, 0, 1], color: '#7ab8ff', label: 'Z' },
      ];
      for (const ax of axes) {
        const tip = project(
          selH.world[0] + ax.dir[0] * axisLen,
          selH.world[1] + ax.dir[1] * axisLen,
          selH.world[2] + ax.dir[2] * axisLen
        );
        if (!tip) continue;
        const dx = tip.x - selH.x;
        const dy = tip.y - selH.y;
        const dl = Math.hypot(dx, dy) || 1;
        const ux = dx / dl;
        const uy = dy / dl;
        const px = -uy;
        const py = ux;
        const ah = 9;
        const aw = 4.2;
        const active = this.draggingSplineAxis === ax.id;
        axesSvg += `
          <line x1="${selH.x.toFixed(1)}" y1="${selH.y.toFixed(1)}" x2="${tip.x.toFixed(1)}" y2="${tip.y.toFixed(1)}" stroke="${ax.color}" stroke-width="${active ? 3.4 : 2.4}" stroke-linecap="round" opacity="0.95" />
          <polygon points="${tip.x.toFixed(1)},${tip.y.toFixed(1)} ${(tip.x - ux * ah + px * aw).toFixed(1)},${(tip.y - uy * ah + py * aw).toFixed(1)} ${(tip.x - ux * ah - px * aw).toFixed(1)},${(tip.y - uy * ah - py * aw).toFixed(1)}" fill="${ax.color}" />
          <text x="${(tip.x + ux * 13).toFixed(1)}" y="${(tip.y + uy * 13 + 3).toFixed(1)}" text-anchor="middle" fill="${ax.color}" font-size="10" font-weight="700" font-family="Inter, sans-serif">${ax.label}</text>
          <line class="spline-axis-hit" data-gizmo-axis="${ax.id}" data-pt-idx="${selH.i}" x1="${selH.x.toFixed(1)}" y1="${selH.y.toFixed(1)}" x2="${tip.x.toFixed(1)}" y2="${tip.y.toFixed(1)}" stroke="rgba(0,0,0,0)" stroke-width="16" stroke-linecap="round" />`;
      }
    }

    let circles = '';
    for (const p of handles) {
      const isSel = p.i === this.selectedSplinePt;
      circles += `
        <g class="spline-handle" data-pt-idx="${p.i}" transform="translate(${p.x.toFixed(1)},${p.y.toFixed(1)})">
          <circle r="${isSel ? 11 : 9}" fill="${isSel ? 'rgba(101,230,178,0.95)' : 'rgba(5,18,14,0.82)'}" stroke="${isSel ? '#ffffff' : '#65e6b2'}" stroke-width="${isSel ? 2.4 : 2}" />
          <text y="3.5" text-anchor="middle" fill="${isSel ? '#04231a' : '#dededc'}" font-size="9" font-weight="${isSel ? 700 : 400}" font-family="Inter, sans-serif">${p.i + 1}</text>
        </g>`;
    }

    svg.innerHTML = `
      <polyline points="${pathPts.join(' ')}" fill="none" stroke="rgba(101,230,178,0.88)" stroke-width="2.2" stroke-dasharray="6 4" />
      ${drops}
      ${axesSvg}
      ${circles}
    `;

    svg.querySelectorAll('.spline-axis-hit').forEach((line) => {
      line.style.cursor = 'grab';
      line.style.pointerEvents = 'all';
      line.addEventListener('pointerdown', (e) => {
        e.stopPropagation();
        e.preventDefault();
        const idx = Number(line.dataset.ptIdx);
        const axis = line.dataset.gizmoAxis;
        const pt = sel.params.points[idx];
        if (!pt) return;
        this.selectedSplinePt = idx;
        this.draggingSplineAxis = axis;
        const origin = this._splineHandleWorld(sel, idx);
        const dirVec = axis === 'x' ? [1, 0, 0] : axis === 'y' ? [0, 1, 0] : [0, 0, 1];
        const ray = this._splinePointerRay(e);
        const startT = ray ? this._rayAxisParam(ray, origin, dirVec) : null;
        this._splineDrag = {
          axis,
          idx,
          origin,
          dir: dirVec,
          startT: startT ?? 0,
          origX: pt[0] || 0,
          origZ: pt[1] || 0,
          origY: this._splineEffectiveY(sel, idx),
        };
      });
    });

    svg.querySelectorAll('.spline-handle').forEach((g) => {
      g.style.cursor = 'grab';
      g.style.pointerEvents = 'all';
      g.addEventListener('pointerdown', (e) => {
        e.stopPropagation();
        e.preventDefault();
        const idx = Number(g.dataset.ptIdx);
        const changed = idx !== this.selectedSplinePt;
        this.selectedSplinePt = idx;
        this.draggingSplinePt = idx;
        if (changed) this.renderInspector();
      });
    });
  }

  _esc(str) {
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }
}
